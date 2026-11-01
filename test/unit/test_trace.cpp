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

// Locks the exact BONEFISH_TRACE output format:
//   [<base file name>:<line>][<function>] <formatted payload>\n
// on std::cerr, and the enable/disable switch. The Boost-free port swaps
// boost::format for a std-only formatter and must produce byte-identical
// output, which this test enforces.

#include "bonefish_test.hpp"

#include <bonefish/trace/trace.hpp>

#include <iostream>
#include <regex>
#include <sstream>
#include <string>

using bonefish::trace::is_enabled;
using bonefish::trace::set_enabled;

namespace {

class cerr_capture {
public:
    cerr_capture()
        : m_saved(std::cerr.rdbuf(m_buffer.rdbuf()))
    {
    }

    ~cerr_capture()
    {
        std::cerr.rdbuf(m_saved);
    }

    std::string str() const
    {
        return m_buffer.str();
    }

private:
    std::ostringstream m_buffer;
    std::streambuf* m_saved;
};

// Escaped file name expected in trace lines (__FILE__ base name).
const char* const TRACE_FILE = "test_trace.cpp";

std::string line_for(const std::string& captured)
{
    // Trace output ends with std::endl; strip exactly one trailing newline.
    if (!captured.empty() && captured.back() == '\n') {
        return captured.substr(0, captured.size() - 1);
    }
    return captured;
}

bool matches(const std::string& line, const std::string& function,
        const std::string& payload)
{
    std::ostringstream pattern;
    pattern << "^\\[" << TRACE_FILE << ":[0-9]+\\]\\[" << function
            << "\\] " << payload << "$";
    return std::regex_match(line, std::regex(pattern.str()));
}

void trace_disabled_emits_nothing_impl()
{
    set_enabled(false);
    BF_ASSERT_TRUE(!is_enabled());
    cerr_capture capture;
    BONEFISH_TRACE("invisible %1%", 1);
    BF_ASSERT_EQ(capture.str(), std::string(""));
}

void trace_noargs_format_impl()
{
    set_enabled(true);
    cerr_capture capture;
    BONEFISH_TRACE("plain message");
    BF_ASSERT_TRUE(matches(line_for(capture.str()),
                "trace_noargs_format_impl", "plain message"));
    set_enabled(false);
}

void trace_chained_args_format_impl()
{
    // The multi-value BONEFISH_TRACE pattern after the std-only port: one
    // argument per %N% placeholder (the baseline's boost::format operator%
    // chaining syntax was replaced at every call site; the emitted bytes
    // must be identical). This also locks the fixed behavior of the
    // 3-macro-argument case, which under the baseline BOOST_PP macro hit a
    // comma-operator quirk (wamp_dealer.cpp printed only the trailing
    // value); the variadic-template port formats all arguments.
    set_enabled(true);
    cerr_capture capture;
    BONEFISH_TRACE("%1%, %2%", std::string("left"), std::string("right"));
    BF_ASSERT_TRUE(matches(line_for(capture.str()),
                "trace_chained_args_format_impl", "left, right"));
    set_enabled(false);
}

void trace_two_placeholder_single_arg_impl()
{
    // "sending message: %1%:%2%" style (websocket_server.cpp): two
    // placeholders filled from two arguments, including an unsigned short.
    set_enabled(true);
    cerr_capture capture;
    std::string ip = "127.0.0.1";
    unsigned short port = 9999;
    BONEFISH_TRACE("starting websocket server: %1%:%2%", ip, port);
    BF_ASSERT_TRUE(matches(line_for(capture.str()),
                "trace_two_placeholder_single_arg_impl",
                "starting websocket server: 127\\.0\\.0\\.1:9999"));
    set_enabled(false);
}

void trace_base_file_name_strips_path_impl()
{
    BF_ASSERT_EQ(std::string(bonefish::trace::base_file_name(
                    "/some/deep/path/file.cpp")), std::string("file.cpp"));
    BF_ASSERT_EQ(std::string(bonefish::trace::base_file_name(
                    "file.cpp")), std::string("file.cpp"));
}

} // anonymous namespace

BF_TEST(trace_disabled_emits_nothing)   { trace_disabled_emits_nothing_impl(); }
BF_TEST(trace_noargs_format)            { trace_noargs_format_impl(); }
BF_TEST(trace_chained_args_format)      { trace_chained_args_format_impl(); }
BF_TEST(trace_two_placeholder_single_arg) { trace_two_placeholder_single_arg_impl(); }
BF_TEST(trace_base_file_name_strips_path) { trace_base_file_name_strips_path_impl(); }

BF_RUN_ALL("trace")
