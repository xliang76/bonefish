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

#ifndef BONEFISH_WAMP_TRACE_HPP
#define BONEFISH_WAMP_TRACE_HPP

#include <iostream>
#include <sstream>
#include <string>
#include <string.h>
#include <vector>

namespace bonefish {
namespace trace {

extern bool _enabled;

inline bool is_enabled()
{
    return _enabled;
}

inline void set_enabled(bool is_enabled)
{
    _enabled = is_enabled;
}

inline const char* base_file_name(const char* file_path)
{
    const char* file_name = strrchr(file_path, '/');
    return file_name == nullptr ? file_path : file_name + 1;
}

/*!
 * Converts one trace argument to its string representation using stream
 * insertion, matching how boost::format rendered values.
 */
template<typename T>
inline std::string to_trace_string(const T& value)
{
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

inline void collect_arguments(std::vector<std::string>&)
{
}

template<typename T, typename... Rest>
inline void collect_arguments(
        std::vector<std::string>& out, const T& value, const Rest&... rest)
{
    out.push_back(to_trace_string(value));
    collect_arguments(out, rest...);
}

/*!
 * Replaces %N% placeholders (1-based) in the format string with the
 * corresponding argument, reproducing the subset of boost::format
 * semantics the codebase relies on: repeated %N% placeholders reuse the
 * same argument and %% is a literal percent sign. Placeholders without a
 * matching argument are left in place verbatim.
 */
inline std::string expand_placeholders(
        const char* format, const std::vector<std::string>& arguments)
{
    std::string out;
    for (const char* p = format; *p != '\0'; ++p) {
        if (*p == '%') {
            if (p[1] == '%') {
                out += '%';
                ++p;
                continue;
            }
            const char* q = p + 1;
            unsigned index = 0;
            bool has_digits = false;
            while (*q >= '0' && *q <= '9') {
                index = index * 10 + static_cast<unsigned>(*q - '0');
                has_digits = true;
                ++q;
            }
            if (has_digits && *q == '%' && index >= 1 &&
                    index <= arguments.size()) {
                out += arguments[index - 1];
                p = q;
                continue;
            }
        }
        out += *p;
    }
    return out;
}

template<typename... Args>
inline std::string format(const char* format_string, const Args&... args)
{
    std::vector<std::string> arguments;
    arguments.reserve(sizeof...(Args));
    collect_arguments(arguments, args...);
    return expand_placeholders(format_string, arguments);
}

} // namespace trace
} // namespace bonefish

// Macro for facilitating debug trace logging.
#define BONEFISH_TRACE(fmt, ...) \
    if (bonefish::trace::is_enabled()) { \
        std::cerr << "[" << bonefish::trace::base_file_name(__FILE__) << ":" << __LINE__ << "][" << __FUNCTION__ << "] " \
                << bonefish::trace::format(fmt, ##__VA_ARGS__) << std::endl; \
    }

#endif // BONEFISH_WAMP_TRACE_HPP
