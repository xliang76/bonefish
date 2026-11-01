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

#include <bonefish/utility/wamp_uri.hpp>

namespace bonefish {

namespace uri_flags {
unsigned int allow_empty_components = 1;
unsigned int strict = 1 << 1;
} // namespace uri_flags

// Hand-rolled character-class validator replacing the former four
// std::regex patterns (libstdc++ std::regex costs ~565 ns per match on the
// hot marshal path; see bench/uri_bench.cpp). The semantics below mirror
// the exact patterns they replace, including their quirks, and are locked
// case-by-case by test/unit/test_wamp_uri.cpp:
//
//   strict, allow_empty:  ^(([0-9a-z_]+\.)|\.)*([0-9a-z_]+)?$
//   relaxed, allow_empty: ^(([^\s\.#]+\.|\.)*)([^\s\.#]+)?$
//   strict:               ^([0-9a-z_]+\.)*([0-9a-z_]+)$
//   relaxed:              ^([^\s\.#]+\.)*([^\s\.#]+)$
//
// With allow_empty_components every iteration of the leading star group
// consumes either a non-empty segment plus its dot or a bare dot, so a
// string matches exactly when each character is either '.' or a valid
// segment character (empty segments anywhere, including leading/trailing,
// and the empty string all match). Without the flag every dot-separated
// segment must be non-empty and made of valid characters.
bool is_valid_uri(const std::string& uri, int flags)
{
    const bool allow_empty = (flags & uri_flags::allow_empty_components) != 0;
    const bool strict = (flags & uri_flags::strict) != 0;

    // strict: [0-9a-z_]; relaxed: [^\s\.#] with \s = [\t\n\v\f\r ]
    auto is_valid_char = [strict](char c) {
        if (strict) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '_';
        }
        return !(c == '.' || c == '#' || c == ' ' || c == '\t' || c == '\n' ||
                 c == '\v' || c == '\f' || c == '\r');
    };

    if (allow_empty) {
        for (char c : uri) {
            if (c != '.' && !is_valid_char(c)) {
                return false;
            }
        }
        return true;
    }

    if (uri.empty()) {
        return false;
    }

    bool segment_started = false;
    for (size_t i = 0; i < uri.size(); ++i) {
        const char c = uri[i];
        if (c == '.') {
            // a dot must close a non-empty segment and start a new one
            if (!segment_started || i + 1 == uri.size()) {
                return false;
            }
            segment_started = false;
        } else if (is_valid_char(c)) {
            segment_started = true;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace bonefish
