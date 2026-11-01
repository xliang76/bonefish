/**
 *  Copyright (C) 2015 Topology LP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

// RawSocket benchmark client for a live bonefish daemon. Measures the
// three end-to-end numbers used by the P4 performance gate:
//
//   pubsub_msgs_per_sec      small-message broker throughput
//   rpc_latency_*            sequential CALL->INVOCATION->YIELD->RESULT
//   large_payload_mbps       512 KiB EVENT payload throughput
//
// Speaks the wire protocol directly (4-byte big-endian length + msgpack)
// with plain BSD sockets and no dependencies beyond msgpack-c headers, so
// the identical binary drives the Boost baseline and the Boost-free port.

#include <msgpack.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// WAMP message type codes of this router's dialect
enum {
    WAMP_HELLO = 1, WAMP_WELCOME = 2, WAMP_GOODBYE = 6,
    WAMP_PUBLISH = 16, WAMP_SUBSCRIBE = 32, WAMP_SUBSCRIBED = 33,
    WAMP_EVENT = 36, WAMP_CALL = 48, WAMP_RESULT = 50,
    WAMP_REGISTER = 64, WAMP_REGISTERED = 65,
    WAMP_INVOCATION = 68, WAMP_YIELD = 70
};

namespace {

typedef std::chrono::steady_clock clock_type;

double elapsed_us(clock_type::time_point start, clock_type::time_point stop)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            stop - start).count() / 1000.0;
}

// 2015-draft HELLO details carrying all four client roles, pre-encoded so
// the bench does not depend on any message-building code.
const unsigned char HELLO_BODY[] = {
    0x93, 0x01, 0xa7, 'd', 'e', 'f', 'a', 'u', 'l', 't',
    0x81, 0xa5, 'r', 'o', 'l', 'e', 's',
    0x84,
    0xa6, 'c', 'a', 'l', 'l', 'e', 'e', 0x81, 0xa8, 'f', 'e', 'a', 't', 'u', 'r', 'e', 's', 0x80,
    0xa6, 'c', 'a', 'l', 'l', 'e', 'r', 0x81, 0xa8, 'f', 'e', 'a', 't', 'u', 'r', 'e', 's', 0x80,
    0xa9, 'p', 'u', 'b', 'l', 'i', 's', 'h', 'e', 'r', 0x81, 0xa8, 'f', 'e', 'a', 't', 'u', 'r', 'e', 's', 0x80,
    0xaa, 's', 'u', 'b', 's', 'c', 'r', 'i', 'b', 'e', 'r', 0x81, 0xa8, 'f', 'e', 'a', 't', 'u', 'r', 'e', 's', 0x80
};

// One fully msgpack-encoded value (a single field of a wire array). The
// message builder below encodes every field with plain msgpack::pack()
// calls on simple types, then concatenates the chunks behind a fixarray
// header - no msgpack zones or object internals involved.
typedef std::vector<unsigned char> chunk;

template<typename T>
chunk to_chunk(const T& value)
{
    msgpack::sbuffer buf;
    msgpack::pack(buf, value);
    return chunk(buf.data(), buf.data() + buf.size());
}

// Builds one WAMP message as a msgpack array of 2..15 fields.
class msg_builder {
public:
    explicit msg_builder(size_t field_count)
        : m_fields(field_count)
        , m_index(0)
    {
    }

    void put_type(int type)
    {
        put_chunk(to_chunk(type));
    }

    void put_id(uint64_t value)
    {
        put_chunk(to_chunk(value));
    }

    void put_empty_map()
    {
        put_chunk(chunk(1, static_cast<unsigned char>(0x80)));
    }

    void put_str(const char* value)
    {
        put_chunk(to_chunk(std::string(value)));
    }

    void put_list_of_str(const char* value)
    {
        std::vector<std::string> list;
        list.push_back(std::string(value));
        put_chunk(to_chunk(list));
    }

    void put_list_of_ids(uint64_t a, uint64_t b)
    {
        std::vector<uint64_t> list;
        list.push_back(a);
        list.push_back(b);
        put_chunk(to_chunk(list));
    }

    // Arguments list holding a single `size`-byte payload string.
    void put_blob(size_t size)
    {
        std::vector<std::string> list;
        list.push_back(std::string(size, 'x'));
        put_chunk(to_chunk(list));
    }

    std::vector<unsigned char> finish() const
    {
        std::vector<unsigned char> body;
        size_t n = m_fields.size();
        body.push_back(static_cast<unsigned char>(0x90u + n)); // fixarray
        for (size_t i = 0; i < n; ++i) {
            body.insert(body.end(), m_fields[i].begin(), m_fields[i].end());
        }
        return body;
    }

private:
    void put_chunk(const chunk& value)
    {
        if (m_index >= m_fields.size()) {
            throw std::logic_error("msg_builder field overflow");
        }
        m_fields[m_index++] = value;
    }

    std::vector<chunk> m_fields;
    size_t m_index;
};

class connection {
public:
    connection() : m_fd(-1) {}
    ~connection() { close(); }

    void connect(uint16_t port)
    {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0) {
            throw std::runtime_error("socket() failed");
        }
        int one = 1;
        setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("connect() failed");
        }
        uint32_t caps = htonl(0x7F000000u | (7u << 20) | (2u << 16));
        write_all(&caps, 4);
        uint32_t echo;
        read_all(&echo, 4);
        echo = ntohl(echo);
        if (echo != 0x7F720000u) {
            throw std::runtime_error("unexpected handshake echo");
        }
        // HELLO with roles, then expect WELCOME
        uint32_t len = htonl(static_cast<uint32_t>(sizeof(HELLO_BODY)));
        write_all(&len, 4);
        write_all(HELLO_BODY, sizeof(HELLO_BODY));
        std::vector<msgpack::object> fields = recv_msg();
        if (fields.empty() || fields[0].as<int>() != WAMP_WELCOME) {
            throw std::runtime_error("expected WELCOME");
        }
    }

    void close()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    void send_raw(const std::vector<unsigned char>& body)
    {
        uint32_t len = htonl(static_cast<uint32_t>(body.size()));
        write_all(&len, 4);
        write_all(body.data(), body.size());
    }

    std::vector<msgpack::object> recv_msg()
    {
        uint32_t len;
        read_all(&len, 4);
        len = ntohl(len);
        std::vector<char> body(len);
        read_all(body.data(), len);
        msgpack::object_handle handle =
                msgpack::unpack(body.data(), body.size());
        return handle.get().as<std::vector<msgpack::object> >();
    }

private:
    void write_all(const void* data, size_t length)
    {
        const char* p = static_cast<const char*>(data);
        while (length > 0) {
            ssize_t n = ::write(m_fd, p, length);
            if (n <= 0) {
                throw std::runtime_error("write failed");
            }
            p += n;
            length -= static_cast<size_t>(n);
        }
    }

    void read_all(void* data, size_t length)
    {
        char* p = static_cast<char*>(data);
        while (length > 0) {
            ssize_t n = ::read(m_fd, p, length);
            if (n <= 0) {
                throw std::runtime_error("read failed");
            }
            p += n;
            length -= static_cast<size_t>(n);
        }
    }

    int m_fd;
};

} // anonymous namespace

int main(int argc, char** argv)
{
    uint16_t port = 0;
    int messages = 100000;
    int calls = 20000;
    int large = 256;
    const size_t large_size = 512 * 1024;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--messages" && i + 1 < argc) {
            messages = std::atoi(argv[++i]);
        } else if (arg == "--calls" && i + 1 < argc) {
            calls = std::atoi(argv[++i]);
        } else if (arg == "--large" && i + 1 < argc) {
            large = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr,
                    "usage: %s --port N [--messages N] [--calls N] "
                    "[--large N]\n", argv[0]);
            return 2;
        }
    }
    if (port == 0) {
        std::fprintf(stderr, "missing --port\n");
        return 2;
    }

    try {
        // ---- pub/sub throughput -------------------------------------
        {
            connection sub, pub;
            sub.connect(port);
            pub.connect(port);

            msg_builder s(4);
            s.put_type(WAMP_SUBSCRIBE);
            s.put_id(1);
            s.put_empty_map();
            s.put_str("bench.topic");
            sub.send_raw(s.finish());
            if (sub.recv_msg()[0].as<int>() != WAMP_SUBSCRIBED) {
                throw std::runtime_error("expected SUBSCRIBED");
            }

            int received = 0;
            std::thread reader([&] {
                try {
                    for (int i = 0; i < messages; ++i) {
                        std::vector<msgpack::object> ev = sub.recv_msg();
                        if (ev[0].as<int>() != WAMP_EVENT) {
                            return;
                        }
                        ++received;
                    }
                } catch (...) {
                }
            });

            auto start = clock_type::now();
            for (int i = 0; i < messages; ++i) {
                msg_builder p(5);
                p.put_type(WAMP_PUBLISH);
                p.put_id(static_cast<uint64_t>(i));
                p.put_empty_map();
                p.put_str("bench.topic");
                p.put_list_of_str("bench");
                pub.send_raw(p.finish());
            }
            reader.join();
            auto stop = clock_type::now();
            double secs = elapsed_us(start, stop) / 1e6;
            std::printf("pubsub_msgs_per_sec %.0f (n=%d received=%d)\n",
                    received / secs, messages, received);
        }

        // ---- RPC sequential latency ----------------------------------
        {
            connection callee, caller;
            callee.connect(port);
            caller.connect(port);

            msg_builder r(4);
            r.put_type(WAMP_REGISTER);
            r.put_id(1);
            r.put_empty_map();
            r.put_str("bench.add2");
            callee.send_raw(r.finish());
            if (callee.recv_msg()[0].as<int>() != WAMP_REGISTERED) {
                throw std::runtime_error("expected REGISTERED");
            }

            std::vector<double> latencies;
            latencies.reserve(calls);
            uint64_t request = 0;
            for (int i = 0; i < calls; ++i) {
                ++request;
                auto start = clock_type::now();
                msg_builder c(5);
                c.put_type(WAMP_CALL);
                c.put_id(request);
                c.put_empty_map();
                c.put_str("bench.add2");
                c.put_list_of_ids(2, 3);
                caller.send_raw(c.finish());

                std::vector<msgpack::object> inv = callee.recv_msg();
                uint64_t invocation = inv[1].as<uint64_t>();
                msg_builder y(4);
                y.put_type(WAMP_YIELD);
                y.put_id(invocation);
                y.put_empty_map();
                y.put_list_of_ids(5, 0);
                callee.send_raw(y.finish());

                std::vector<msgpack::object> res = caller.recv_msg();
                auto stop = clock_type::now();
                if (res[0].as<int>() != WAMP_RESULT ||
                        res[1].as<uint64_t>() != request) {
                    throw std::runtime_error("unexpected RESULT");
                }
                latencies.push_back(elapsed_us(start, stop));
            }
            std::sort(latencies.begin(), latencies.end());
            double total = 0.0;
            for (double v : latencies) {
                total += v;
            }
            std::printf("rpc_latency_us mean %.2f p50 %.2f p99 %.2f max %.2f (n=%d)\n",
                    total / latencies.size(),
                    latencies[latencies.size() / 2],
                    latencies[static_cast<size_t>(latencies.size() * 0.99)],
                    latencies.back(), calls);
        }

        // ---- 512 KiB payload throughput -------------------------------
        {
            connection sub, pub;
            sub.connect(port);
            pub.connect(port);

            msg_builder s(4);
            s.put_type(WAMP_SUBSCRIBE);
            s.put_id(1);
            s.put_empty_map();
            s.put_str("bench.big");
            sub.send_raw(s.finish());
            if (sub.recv_msg()[0].as<int>() != WAMP_SUBSCRIBED) {
                throw std::runtime_error("expected SUBSCRIBED");
            }

            int received = 0;
            std::thread reader([&] {
                try {
                    for (int i = 0; i < large; ++i) {
                        std::vector<msgpack::object> ev = sub.recv_msg();
                        if (ev[0].as<int>() != WAMP_EVENT) {
                            return;
                        }
                        ++received;
                    }
                } catch (...) {
                }
            });

            auto start = clock_type::now();
            for (int i = 0; i < large; ++i) {
                msg_builder p(5);
                p.put_type(WAMP_PUBLISH);
                p.put_id(static_cast<uint64_t>(i));
                p.put_empty_map();
                p.put_str("bench.big");
                p.put_blob(large_size);
                pub.send_raw(p.finish());
            }
            reader.join();
            auto stop = clock_type::now();
            double secs = elapsed_us(start, stop) / 1e6;
            double mb = (received * large_size) / (1024.0 * 1024.0);
            std::printf("large_payload_mbps %.1f (event_us %.0f n=%d received=%d)\n",
                    mb / secs, secs * 1e6 / (received ? received : 1),
                    large, received);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench error: %s\n", e.what());
        return 1;
    }
    return 0;
}
