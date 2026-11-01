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

// Micro-benchmark for bonefish::is_valid_uri(), which runs inside message
// marshal/unmarshal on every routed WAMP message. The Boost-removal phase
// replaces the regex engine with a hand-rolled validator; this benchmark
// proves the replacement is not slower (PROGRESS.md P4 gate).

#include <bonefish/utility/wamp_uri.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using bonefish::is_valid_uri;
using bonefish::uri_flags::allow_empty_components;
using bonefish::uri_flags::strict;

namespace {

const char* const WORKLOAD[] = {
    // mix of strict-valid, strict-invalid-but-relaxed-valid, and invalid
    "com.example.topic",
    "com.example.procedure.add2",
    "a.b.c.d.e.f.g.h",
    "wamp.error.not_authorized",
    "Com.Example.Upper",
    "com.example-with-dash.topic",
    "com.example~tilde",
    "this is invalid",
    "bad#hash.uri",
    "",
    "single",
    "some.longer.application.level.procedure.name.with.many.components"
};

const int FLAG_SETS[] = {
    0,
    static_cast<int>(allow_empty_components),
    static_cast<int>(strict),
    static_cast<int>(strict | allow_empty_components)
};

const char* const FLAG_NAMES[] = { "relaxed", "relaxed_empty",
                                   "strict", "strict_empty" };

const int ITERATIONS = 20000;

} // anonymous namespace

int main()
{
    std::vector<std::string> workload;
    for (const char* uri : WORKLOAD) {
        workload.push_back(std::string(uri));
    }

    for (size_t f = 0; f < sizeof(FLAG_SETS) / sizeof(int); ++f) {
        int flags = FLAG_SETS[f];
        size_t checksum = 0;

        // warmup
        for (const auto& uri : workload) {
            checksum += is_valid_uri(uri, flags) ? 1 : 0;
        }

        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < ITERATIONS; ++i) {
            for (const auto& uri : workload) {
                checksum += is_valid_uri(uri, flags) ? 1 : 0;
            }
        }
        auto stop = std::chrono::steady_clock::now();

        double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                stop - start).count();
        size_t calls = static_cast<size_t>(ITERATIONS) * workload.size();
        std::printf("uri_%s_ns_per_op %.1f\n", FLAG_NAMES[f], ns / calls);
        if (checksum == 123456789u) { // keep the compiler honest
            std::printf("unreachable\n");
        }
    }
    return 0;
}
