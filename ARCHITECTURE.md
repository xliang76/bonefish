# ARCHITECTURE.md - Bonefish upgrade program

Companion docs: `PROGRESS.md` (tracker), `GIT-OWNERSHIP.md` (goal 1),
`PLATFORM.md` (goal 2), `BOOST-REMOVAL.md` (goal 3).

## What Bonefish is

A WAMP v2 router in C++: a **broker** (pub/sub) + **dealer** (RPC) per *realm*,
behind three transports, usable as a standalone daemon or an embedded library.

## Current architecture (as-is)

> **Status 2026-11-01:** this section describes the P0 starting point and is
> kept as history; the to-be section below is now the shipped architecture
> (only the C++20 line is deferred - websocketpp 0.8.2 template-id ctor/dtor
> headers, see BOOST-REMOVAL.md 2g').

```
 daemon/bonefish.cpp  ----------+
 examples/integration.cpp ------+--> libbonefish (src/bonefish/)
                                |
    transports/    websocket (websocketpp), rawsocket (TCP+UDS), native (in-proc)
    serialization/ msgpack + json (msgpack-c, rapidjson, json-msgpack, cppcodec)
    router/        wamp_router_impl --> broker/ (pubsub) + dealer/ (RPC)
    messages/      24 WAMP message classes (marshal/unmarshal, wire codes)
    utility/       wamp_uri (regex validation), trace/, identifiers/
                                |
    platform layer: Boost.Asio io_service + deadline_timer, boost::future,
                    boost::system::error_code, program_options, format, regex
```

- Build: CMake 2.8-era scripts, `-std=c++11`, `-Wall -Werror`, static lib default.
- Required Boost libs today: `program_options system thread` (+`regex` on GCC < 4.9).
- No automated tests (no CTest; `test/smoke` are manual Python-2-era scripts).

## Target architecture (to-be)

The **wire protocol, module structure, and threading model do not change**.
Only the platform layer is replaced:

```
    platform layer: standalone Asio (io_context + steady_timer, ASIO_STANDALONE,
                    header-only, vendored as third-party/asio pinned fork),
                    std::future/promise, std::error_code, std::regex-free
                    hand-rolled URI validator, hand-rolled CLI parser,
                    std::chrono, lambdas
```

- C++20, `cmake_minimum_required(3.16)`, zero Boost: no `find_package(Boost)`,
  no Boost symbols in `libbonefish.a` or linked binaries (verified by `nm`/`ldd`).
- `websocketpp` submodule bumped to a standalone-Asio-capable pin (0.7.0 predates
  `ASIO_STANDALONE` support).
- Test + benchmark harness wired into CTest: unit (wire formats, URI, trace),
  e2e (RawSocket TCP/UDS + WebSocket JSON/msgpack full pub/sub + RPC flows),
  throughput/latency benchmarks gated against the as-is baseline.
- CI matrix on native GitHub runners: ubuntu-22.04 + ubuntu-24.04 x Debug/Release,
  plus an ASan/UBSan job.

## Key architectural decisions

1. **Networking stays a library (standalone Asio), not STL.** The C++ standard
   has no async I/O; Networking TS is abandoned and `std::execution` is not
   shippable on GCC 11. Standalone Asio *is* the same engine Boost.Asio wraps,
   so the swap is performance-neutral and functionally identical. Revisit when
   `std::execution` reaches GCC 11 baseline.
2. **Hand-rolled URI validation instead of `std::regex`.** `is_valid_uri()` runs
   inside every message marshal/unmarshal (hot path). libstdc++ `std::regex` is
   far slower than Boost.Regex; the four patterns are simple character-class
   sequences, so a hand-written checker is both STL-pure and faster.
3. **Hand-rolled CLI parser** for the daemon's ~10 flags (replaces
   `boost::program_options`); same flags, same exit codes.
4. **`deadline_timer` -> `steady_timer`**: fixes wall-clock jumps in CALL
   timeout handling; small intentional behavior change, documented.
5. **Test-first**: functionality/performance gates (P0 below) exist before any
   port commit lands, per the "keep functional and performance" requirement.
6. **Everything under xliang76**: main repo + all submodules (including the new
   `asio` fork) resolve from `github.com/xliang76/*` only. See GIT-OWNERSHIP.md.
