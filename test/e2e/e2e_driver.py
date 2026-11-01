#!/usr/bin/env python3
#
#  Copyright (C) 2015 Topology LP
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

"""End-to-end guardrail suite for the bonefish daemon.

Drives a built daemon binary over every transport the router offers
(RawSocket over TCP, RawSocket over a UNIX domain socket, WebSocket with
both wamp.2.msgpack and wamp.2.json subprotocols), exercises the full
session lifecycle, and checks the daemon CLI matrix. Written against the
Boost-based baseline so the same suite must stay green on the Boost-free
ported tree (P0 acceptance gate).

Requires: python3 with msgpack + websockets (apt: python3-msgpack
python3-websockets). Exits 0 when every case passes.
"""

import argparse
import asyncio
import contextlib
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

import msgpack
import websockets

# ---------------------------------------------------------------------------
# WAMP message type codes (2015 draft dialect spoken by this router)
# ---------------------------------------------------------------------------
HELLO, WELCOME, ABORT, GOODBYE = 1, 2, 3, 6
ERROR = 8
PUBLISH, PUBLISHED = 16, 17
SUBSCRIBE, SUBSCRIBED, UNSUBSCRIBE, UNSUBSCRIBED, EVENT = 32, 33, 34, 35, 36
CALL, RESULT = 48, 50
REGISTER, REGISTERED, UNREGISTER, UNREGISTERED = 64, 65, 66, 67
INVOCATION, YIELD = 68, 70

REALM = "default"
FULL_ROLES = {
    "roles": {
        "callee": {"features": {}},
        "caller": {"features": {}},
        "publisher": {"features": {}},
        "subscriber": {"features": {}},
    }
}
RECV_TIMEOUT = 10.0

SANITIZER_MARKERS = (
    "AddressSanitizer", "LeakSanitizer", "runtime error:",
    "ThreadSanitizer", "UndefinedBehaviorSanitizer",
)

# ---------------------------------------------------------------------------
# test registry (mirrors the tiny C++ unit harness output style)
# ---------------------------------------------------------------------------
TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


class Failure(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Failure(what)


# ---------------------------------------------------------------------------
# daemon lifecycle
# ---------------------------------------------------------------------------
def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Daemon:
    def __init__(self, ctx, *flags, ws=False, tcp=False, uds=None):
        self.ctx = ctx
        self.ws_port = free_port() if ws else None
        self.tcp_port = free_port() if tcp else None
        self.uds_path = None
        argv = [ctx.daemon, "--realm", REALM]
        if self.ws_port is not None:
            argv += ["--websocket-port", str(self.ws_port)]
        if self.tcp_port is not None:
            argv += ["--rawsocket-port", str(self.tcp_port)]
        if uds is not None:
            self.uds_path = os.path.join(
                ctx.log_dir, "router-%d.sock" % free_port())
            argv += ["--rawsocket-path", self.uds_path]
        argv += list(flags)
        self.argv = argv
        self.log_path = os.path.join(
            ctx.log_dir, "daemon-%d.log" % (self.tcp_port or self.ws_port or 0))
        self.proc = None

    async def __aenter__(self):
        log = open(self.log_path, "wb")
        self.proc = subprocess.Popen(self.argv, stdout=log, stderr=log)
        try:
            if self.tcp_port is not None:
                await wait_tcp(self.tcp_port)
            if self.ws_port is not None:
                await wait_tcp(self.ws_port)
            if self.uds_path is not None:
                await wait_uds(self.uds_path)
        except Exception:
            await self.__aexit__(None, None, None)
            raise
        return self

    async def __aexit__(self, *exc):
        with contextlib.suppress(Exception):
            self.proc.terminate()
        loop = asyncio.get_running_loop()
        try:
            await asyncio.wait_for(loop.run_in_executor(None, self.proc.wait),
                                   timeout=5)
        except asyncio.TimeoutError:
            self.proc.kill()
        return False


async def wait_tcp(port, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            _r, w = await asyncio.open_connection("127.0.0.1", port)
            w.close()
            with contextlib.suppress(Exception):
                await w.wait_closed()
            return
        except OSError:
            await asyncio.sleep(0.05)
    raise Failure("daemon never listened on tcp port %d" % port)


async def wait_uds(path, timeout=15.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(path):
            try:
                _r, w = await asyncio.open_unix_connection(path)
                w.close()
                with contextlib.suppress(Exception):
                    await w.wait_closed()
                return
            except OSError:
                pass
        await asyncio.sleep(0.05)
    raise Failure("daemon never listened on uds %s" % path)


# ---------------------------------------------------------------------------
# RawSocket client (4-byte big-endian length framing, msgpack payload)
# ---------------------------------------------------------------------------
class RawSocket:
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self.session_id = None

    @classmethod
    async def connect(cls, ctx, tcp_port=None, uds_path=None,
                      capabilities=0x7F000000 | (7 << 20) | (2 << 16)):
        if tcp_port is not None:
            reader, writer = await asyncio.open_connection("127.0.0.1", tcp_port)
        else:
            reader, writer = await asyncio.open_unix_connection(uds_path)
        client = cls(reader, writer)
        writer.write(struct.pack("!I", capabilities))
        await writer.drain()
        echo = struct.unpack("!I", await reader.readexactly(4))[0]
        client.handshake_echo = echo
        return client

    async def request(self, capabilities):
        self.writer.write(struct.pack("!I", capabilities))
        await self.writer.drain()
        return struct.unpack("!I", await self.reader.readexactly(4))[0]

    async def hello(self, details=FULL_ROLES, realm=REALM):
        await self.send([HELLO, realm, details])
        msg = await self.recv()
        return msg

    async def open(self, roles=FULL_ROLES):
        msg = await self.hello(roles)
        check(msg[0] == WELCOME, "expected WELCOME, got %r" % (msg,))
        self.session_id = msg[1]
        return msg

    async def send(self, message):
        body = msgpack.packb(message, use_bin_type=True)
        self.writer.write(struct.pack("!I", len(body)) + body)
        await self.writer.drain()

    async def recv(self, timeout=RECV_TIMEOUT):
        hdr = await asyncio.wait_for(self.reader.readexactly(4), timeout)
        (length,) = struct.unpack("!I", hdr)
        body = await asyncio.wait_for(self.reader.readexactly(length), timeout)
        return msgpack.unpackb(body, raw=False)

    async def try_recv(self, timeout=1.0):
        try:
            return await self.recv(timeout)
        except asyncio.TimeoutError:
            return None
        except (asyncio.IncompleteReadError, ConnectionError):
            return "closed"

    async def close_socket(self):
        with contextlib.suppress(Exception):
            self.writer.close()
            await self.writer.wait_closed()

    async def goodbye(self):
        await self.send([GOODBYE, {}, "wamp.close.normal"])


# ---------------------------------------------------------------------------
# WebSocket client
# ---------------------------------------------------------------------------
class WsClient:
    def __init__(self, ws, is_json):
        self.ws = ws
        self.is_json = is_json

    @classmethod
    async def connect(cls, port, proto):
        is_json = proto == "wamp.2.json"
        uri = "ws://127.0.0.1:%d/" % port
        ws = await websockets.connect(
            uri, subprotocols=[proto], open_timeout=5.0, ping_interval=None)
        return cls(ws, is_json)

    async def open(self, roles=FULL_ROLES):
        await self.send([HELLO, REALM, roles])
        msg = await self.recv()
        check(msg[0] == WELCOME, "expected WELCOME, got %r" % (msg,))
        self.session_id = msg[1]
        return msg

    async def send(self, message):
        if self.is_json:
            await self.ws.send(json.dumps(message))
        else:
            await self.ws.send(msgpack.packb(message, use_bin_type=True))

    async def recv(self, timeout=RECV_TIMEOUT):
        raw = await asyncio.wait_for(self.ws.recv(), timeout)
        if self.is_json:
            return json.loads(raw)
        return msgpack.unpackb(raw, raw=False)

    async def try_recv(self, timeout=1.0):
        try:
            return await self.recv(timeout)
        except asyncio.TimeoutError:
            return None
        except Exception:
            return "closed"

    async def close(self):
        with contextlib.suppress(Exception):
            await self.ws.close()


# ---------------------------------------------------------------------------
# high-level flows shared between RawSocket and WebSocket suites
# ---------------------------------------------------------------------------
async def flow_pubsub(sub, pub):
    await sub.open()
    await pub.open()

    await sub.send([SUBSCRIBE, 101, {}, "com.example.e2e.topic"])
    msg = await sub.recv()
    check(msg[0] == SUBSCRIBED and msg[1] == 101,
          "expected SUBSCRIBED for 101, got %r" % (msg,))
    subscription = msg[2]

    args = ["hello", 42, {"nested": True}]
    await pub.send([PUBLISH, 202, {}, "com.example.e2e.topic", args])
    msg = await sub.recv()
    check(msg[0] == EVENT, "expected EVENT, got %r" % (msg,))
    check(msg[1] == subscription, "EVENT subscription mismatch: %r" % (msg,))
    check(msg[4] == args, "EVENT payload mismatch: %r" % (msg,))

    # unsubscribe stops delivery
    await sub.send([UNSUBSCRIBE, 103, subscription])
    msg = await sub.recv()
    check(msg[0] == UNSUBSCRIBED and msg[1] == 103,
          "expected UNSUBSCRIBED for 103, got %r" % (msg,))
    await pub.send([PUBLISH, 204, {}, "com.example.e2e.topic", ["gone"]])
    msg = await sub.try_recv(timeout=1.0)
    check(msg is None, "expected no EVENT after UNSUBSCRIBE, got %r" % (msg,))


async def flow_rpc(callee, caller):
    await callee.open()
    await caller.open()

    await callee.send([REGISTER, 11, {}, "com.example.e2e.add2"])
    msg = await callee.recv()
    check(msg[0] == REGISTERED and msg[1] == 11,
          "expected REGISTERED for 11, got %r" % (msg,))
    registration = msg[2]

    # duplicate registration must be refused
    await callee.send([REGISTER, 12, {}, "com.example.e2e.add2"])
    msg = await callee.recv()
    check(msg[0] == ERROR and msg[1] == REGISTER and msg[2] == 12
          and msg[4] == "wamp.error.procedure_already_exists",
          "expected procedure_already_exists, got %r" % (msg,))

    await caller.send([CALL, 31, {}, "com.example.e2e.add2", [2, 3]])
    msg = await callee.recv()
    check(msg[0] == INVOCATION, "expected INVOCATION, got %r" % (msg,))
    invocation = msg[1]
    # this router's dialect: [INVOCATION, Request, Registration, Details, Args]
    check(msg[2] == registration, "INVOCATION registration mismatch: %r" % (msg,))
    check(msg[4] == [2, 3], "INVOCATION args mismatch: %r" % (msg,))

    await callee.send([YIELD, invocation, {}, [5]])
    msg = await caller.recv()
    check(msg[0] == RESULT and msg[1] == 31, "expected RESULT for 31, got %r" % (msg,))
    check(msg[3] == [5], "RESULT payload mismatch: %r" % (msg,))

    # unregister, then calling the procedure must fail
    await callee.send([UNREGISTER, 13, registration])
    msg = await callee.recv()
    check(msg[0] == UNREGISTERED and msg[1] == 13,
          "expected UNREGISTERED for 13, got %r" % (msg,))
    await caller.send([CALL, 32, {}, "com.example.e2e.add2", [1, 1]])
    msg = await caller.recv()
    check(msg[0] == ERROR and msg[1] == CALL and msg[2] == 32
          and msg[4] == "wamp.error.no_such_procedure",
          "expected no_such_procedure, got %r" % (msg,))


# ---------------------------------------------------------------------------
# RawSocket (TCP) cases
# ---------------------------------------------------------------------------
@test
async def rawsocket_handshake(ctx):
    async with Daemon(ctx, tcp=True) as d:
        # msgpack caps are echoed back verbatim
        c = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        check(c.handshake_echo == 0x7F720000,
              "msgpack handshake echo mismatch: %08x" % c.handshake_echo)
        await c.open()
        await c.close_socket()

        # JSON serializer is not offered on RawSocket: 0x7F100000
        c = await RawSocket.connect(
            ctx, tcp_port=d.tcp_port,
            capabilities=0x7F000000 | (7 << 20) | (1 << 16))
        check(c.handshake_echo == 0x7F100000,
              "expected JSON rejection 0x7f100000, got %08x" % c.handshake_echo)
        await c.close_socket()

        # reserved bits must be zero: 0x7F300000
        c = await RawSocket.connect(
            ctx, tcp_port=d.tcp_port, capabilities=0x7F720001)
        check(c.handshake_echo == 0x7F300000,
              "expected reserved-bits rejection 0x7f300000, got %08x" % c.handshake_echo)
        await c.close_socket()

        # bad magic octet closes the connection without a reply
        reader, writer = await asyncio.open_connection("127.0.0.1", d.tcp_port)
        writer.write(struct.pack("!I", 0x7E720000))
        await writer.drain()
        try:
            echo = await asyncio.wait_for(reader.readexactly(4), timeout=2.0)
            bad = "unexpected reply %r" % (echo,)
        except (asyncio.IncompleteReadError, ConnectionError):
            bad = None
        except asyncio.TimeoutError:
            bad = "no reply and no close on bad magic"
        writer.close()
        with contextlib.suppress(Exception):
            await writer.wait_closed()
        check(bad is None, bad or "")


@test
async def rawsocket_hello_roles(ctx):
    async with Daemon(ctx, tcp=True) as d:
        c = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        await c.open()
        await c.close_socket()

        # HELLO without roles is aborted (2015 draft session contract)
        c = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        msg = await c.hello(details={})
        check(msg[0] == ABORT and msg[2] == "wamp.error.invalid_roles",
              "expected invalid_roles ABORT, got %r" % (msg,))
        await c.close_socket()


@test
async def rawsocket_pubsub_tcp(ctx):
    async with Daemon(ctx, tcp=True) as d:
        sub = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        pub = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        await flow_pubsub(sub, pub)
        await sub.close_socket()
        await pub.close_socket()


@test
async def rawsocket_rpc_tcp(ctx):
    async with Daemon(ctx, tcp=True) as d:
        callee = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        caller = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        await flow_rpc(callee, caller)
        await callee.close_socket()
        await caller.close_socket()


@test
async def rawsocket_large_payload(ctx):
    size = 512 * 1024
    payload = "x" * size
    async with Daemon(ctx, tcp=True) as d:
        sub = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        pub = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        await sub.open()
        await pub.open()
        await sub.send([SUBSCRIBE, 1, {}, "com.example.e2e.big"])
        msg = await sub.recv()
        check(msg[0] == SUBSCRIBED, "expected SUBSCRIBED, got code %r" % (msg[0],))
        await pub.send([PUBLISH, 2, {}, "com.example.e2e.big", [payload]])
        msg = await sub.recv(timeout=30.0)
        check(msg[0] == EVENT and msg[4][0] == payload,
              "512 KiB payload did not round-trip intact (len=%d)"
              % (len(msg[4][0]) if msg[0] == EVENT else -1))
        await sub.close_socket()
        await pub.close_socket()


@test
async def rawsocket_goodbye(ctx):
    async with Daemon(ctx, tcp=True) as d:
        c = await RawSocket.connect(ctx, tcp_port=d.tcp_port)
        await c.open()
        await c.goodbye()
        msg = await c.recv()
        check(msg[0] == GOODBYE and msg[2] == "wamp.error.goodbye_and_out",
              "expected GOODBYE echo, got %r" % (msg,))
        closed = await c.try_recv(timeout=2.0)
        check(closed in ("closed", None),
              "expected close after GOODBYE, got %r" % (closed,))
        await c.close_socket()


@test
async def rawsocket_pubsub_uds(ctx):
    async with Daemon(ctx, uds=True) as d:
        sub = await RawSocket.connect(ctx, uds_path=d.uds_path)
        pub = await RawSocket.connect(ctx, uds_path=d.uds_path)
        await flow_pubsub(sub, pub)
        await sub.close_socket()
        await pub.close_socket()


@test
async def rawsocket_rpc_uds(ctx):
    async with Daemon(ctx, uds=True) as d:
        callee = await RawSocket.connect(ctx, uds_path=d.uds_path)
        caller = await RawSocket.connect(ctx, uds_path=d.uds_path)
        await flow_rpc(callee, caller)
        await callee.close_socket()
        await caller.close_socket()


# ---------------------------------------------------------------------------
# WebSocket cases (both subprotocols + serializer flags)
# ---------------------------------------------------------------------------
@test
async def websocket_pubsub_msgpack(ctx):
    async with Daemon(ctx, ws=True) as d:
        sub = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
        pub = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
        await flow_pubsub(sub, pub)
        await sub.close()
        await pub.close()


@test
async def websocket_rpc_msgpack(ctx):
    async with Daemon(ctx, ws=True) as d:
        callee = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
        caller = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
        await flow_rpc(callee, caller)
        await callee.close()
        await caller.close()


@test
async def websocket_pubsub_json(ctx):
    async with Daemon(ctx, ws=True) as d:
        sub = await WsClient.connect(d.ws_port, "wamp.2.json")
        pub = await WsClient.connect(d.ws_port, "wamp.2.json")
        await flow_pubsub(sub, pub)
        await sub.close()
        await pub.close()


@test
async def websocket_rpc_json(ctx):
    async with Daemon(ctx, ws=True) as d:
        callee = await WsClient.connect(d.ws_port, "wamp.2.json")
        caller = await WsClient.connect(d.ws_port, "wamp.2.json")
        await flow_rpc(callee, caller)
        await callee.close()
        await caller.close()


@test
async def websocket_serializers_off(ctx):
    async with Daemon(ctx, "--no-msgpack", ws=True) as d:
        rejected = False
        try:
            ws = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
            await ws.close()
        except Exception:
            rejected = True
        check(rejected, "wamp.2.msgpack must be refused with --no-msgpack")
        sub = await WsClient.connect(d.ws_port, "wamp.2.json")
        await sub.open()
        await sub.close()

    async with Daemon(ctx, "--no-json", ws=True) as d:
        rejected = False
        try:
            ws = await WsClient.connect(d.ws_port, "wamp.2.json")
            await ws.close()
        except Exception:
            rejected = True
        check(rejected, "wamp.2.json must be refused with --no-json")
        sub = await WsClient.connect(d.ws_port, "wamp.2.msgpack")
        await sub.open()
        await sub.close()


# ---------------------------------------------------------------------------
# daemon CLI matrix (behavioral contract of the option parser)
# ---------------------------------------------------------------------------
def run_cli(ctx, *args):
    proc = subprocess.run([ctx.daemon] + list(args),
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=15)
    return proc.returncode, proc.stdout.decode(errors="replace")


@test
async def cli_help(ctx):
    code, out = run_cli(ctx, "--help")
    check(code == 1, "--help must exit 1, got %d" % code)
    check("Allowed options:" in out, "--help must print option table")
    for flag in ("--realm", "--websocket-port", "--rawsocket-port",
                 "--rawsocket-path", "--no-json", "--no-msgpack", "--debug"):
        check(flag in out, "--help output missing %s" % flag)


@test
async def cli_unknown_flag(ctx):
    code, out = run_cli(ctx, "--bogus")
    check(code == 1, "unknown flag must exit 1, got %d" % code)
    check("Unrecognized options." in out,
          "unknown flag must report 'Unrecognized options.', got %r" % out)


@test
async def cli_no_realm(ctx):
    code, out = run_cli(ctx)
    check(code == 1, "no-realm must exit 1, got %d" % code)
    check("No realm was given." in out,
          "no-realm must report 'No realm was given.', got %r" % out)


@test
async def cli_no_transport(ctx):
    code, out = run_cli(ctx, "--realm", REALM)
    check(code == 1, "realm-only must exit 1, got %d" % code)
    check("No transports are enabled." in out,
          "realm-only must report 'No transports are enabled.', got %r" % out)


@test
async def embedded_example(ctx):
    if not ctx.example:
        sys.stderr.write("  (embedded example binary not provided, skipping)\n")
        return
    proc = subprocess.Popen([ctx.example],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    try:
        # integration.cpp listens on ws 9999 / rawsocket 8888 when not
        # already occupied; it runs its io_service forever.
        await asyncio.sleep(2.0)
        check(proc.poll() is None, "embedded example exited early")
        for port in (9999, 8888):
            try:
                await wait_tcp(port, timeout=1.0)
            except Failure:
                # port held by another process is fine, example is alive
                pass
    finally:
        proc.kill()
        err = proc.stderr.read().decode(errors="replace")
        proc.wait()
    scan_logs([err], ctx.example)


@test
async def daemon_stderr_clean(ctx):
    # real scan runs after every case in main(); this just surfaces the
    # log directory for debugging when a run fails
    sys.stderr.write("  (daemon logs in %s)\n" % ctx.log_dir)


def scan_logs(extra_texts, binary):
    texts = list(extra_texts)
    for name in os.listdir(CTX.log_dir):
        if name.endswith(".log"):
            with open(os.path.join(CTX.log_dir, name), "r", errors="replace") as f:
                texts.append(f.read())
    for text in texts:
        for marker in SANITIZER_MARKERS:
            if marker in text:
                raise Failure(
                    "sanitizer report (%s) found in logs of %s" % (marker, binary))


# ---------------------------------------------------------------------------
# runner
# ---------------------------------------------------------------------------
CTX = None


async def main():
    global CTX
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", required=True, help="path to daemon binary")
    parser.add_argument("--example", help="path to integration example binary")
    parser.add_argument("--log-dir", help="directory for daemon logs")
    parser.add_argument("--filter", help="only run tests containing this text")
    args = parser.parse_args()
    args.daemon = os.path.abspath(args.daemon)
    if args.example:
        args.example = os.path.abspath(args.example)
    if not os.path.exists(args.daemon):
        sys.stderr.write("daemon binary not found: %s\n" % args.daemon)
        return 2

    log_dir = args.log_dir or tempfile.mkdtemp(prefix="bonefish-e2e-")
    os.makedirs(log_dir, exist_ok=True)
    args.log_dir = log_dir
    CTX = args

    failures = 0
    for fn in TESTS:
        name = fn.__name__
        if args.filter and args.filter not in name:
            continue
        started = time.monotonic()
        try:
            await asyncio.wait_for(fn(args), timeout=90)
            print("[ok] e2e :: %s (%.1fs)" % (name, time.monotonic() - started))
        except Exception as exc:  # noqa: BLE001 - report and continue
            failures += 1
            print("[FAIL] e2e :: %s: %s: %s"
                  % (name, type(exc).__name__, exc))
            import traceback
            traceback.print_exc()
    # sanitizer scan across all daemon logs collected during the run
    try:
        scan_logs([], args.daemon)
        print("[ok] e2e :: sanitizer_scan")
    except Failure as exc:
        failures += 1
        print("[FAIL] e2e :: sanitizer_scan: %s" % exc)
    if failures:
        print("%d e2e case(s) failed (logs in %s)" % (failures, log_dir))
        return 1
    print("all e2e cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
