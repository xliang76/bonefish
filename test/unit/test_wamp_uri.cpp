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

// Accept/reject parity table for bonefish::is_valid_uri(). The expected
// values below were generated from the baseline regex implementation
// (test/unit is part of the P0 guardrail harness), so replacing the regex
// engine with the hand-rolled validator in the Boost-removal phase must
// reproduce this table EXACTLY, including the relaxed-mode quirks:
//   - relaxed (non-strict) mode accepts any character except whitespace,
//     '.' and '#' as URI components (so "a-.b" and "a\\b.c" are valid);
//   - allow_empty_components additionally accepts empty components and
//     even the empty string / bare dots;
//   - strict mode restricts components to [0-9a-z_].

#include "bonefish_test.hpp"

#include <bonefish/utility/wamp_uri.hpp>

#include <string>
#include <vector>

using bonefish::is_valid_uri;
using bonefish::uri_flags::allow_empty_components;
using bonefish::uri_flags::strict;

namespace {

struct uri_case {
    const char* uri;
    int relaxed;            // flags = 0
    int relaxed_empty;      // flags = allow_empty_components
    int strict_mode;        // flags = strict
    int strict_empty;       // flags = strict | allow_empty_components
};

// Generated against the baseline implementation - do not edit by hand.
const uri_case table[] = {
    {"com.example.topic", 1, 1, 1, 1},
    {"com.example.procedure2", 1, 1, 1, 1},
    {"a", 1, 1, 1, 1},
    {"a.b", 1, 1, 1, 1},
    {"a.b.c.d.e.f", 1, 1, 1, 1},
    {"1.2", 1, 1, 1, 1},
    {"0", 1, 1, 1, 1},
    {"_", 1, 1, 1, 1},
    {"_private.key", 1, 1, 1, 1},
    {"a.b_c", 1, 1, 1, 1},
    {"COM.EXAMPLE", 1, 1, 0, 0},
    {"com.Example.Topic", 1, 1, 0, 0},
    {"com.example.", 0, 1, 0, 1},
    {".com.example", 0, 1, 0, 1},
    {"com..example", 0, 1, 0, 1},
    {".", 0, 1, 0, 1},
    {"..", 0, 1, 0, 1},
    {"", 0, 1, 0, 1},
    {"a.", 0, 1, 0, 1},
    {".a", 0, 1, 0, 1},
    {"a..b", 0, 1, 0, 1},
    {"a.b.", 0, 1, 0, 1},
    {"a...b", 0, 1, 0, 1},
    {"with space.x", 0, 0, 0, 0},
    {"tab\tx.y", 0, 0, 0, 0},
    {"newline\nx.y", 0, 0, 0, 0},
    {"hash#.x", 0, 0, 0, 0},
    {"x.y#hash", 0, 0, 0, 0},
    {"a-.b", 1, 1, 0, 0},
    {"a.b%s.c", 1, 1, 0, 0},
    {"tcp://host/x", 1, 1, 0, 0},
    {"a\\b.c", 1, 1, 0, 0},
    {"\xc3\xa4.b", 1, 1, 0, 0},
    {"wamp.error.not_authorized", 1, 1, 1, 1},
    {"com.example/topic", 1, 1, 0, 0},
    {"x.", 0, 1, 0, 1},
    {"a._b", 1, 1, 1, 1},
    {"a.b~c", 1, 1, 0, 0},
    {"a b", 0, 0, 0, 0},
};

} // anonymous namespace

BF_TEST(uri_parity_table)
{
    for (const auto& c : table) {
        std::string uri(c.uri);
        BF_ASSERT_EQ(is_valid_uri(uri, 0) ? 1 : 0, c.relaxed);
        BF_ASSERT_EQ(is_valid_uri(uri, allow_empty_components) ? 1 : 0,
                c.relaxed_empty);
        BF_ASSERT_EQ(is_valid_uri(uri, strict) ? 1 : 0, c.strict_mode);
        BF_ASSERT_EQ(is_valid_uri(uri, strict | allow_empty_components) ? 1 : 0,
                c.strict_empty);
    }
}

BF_RUN_ALL("wamp_uri")
