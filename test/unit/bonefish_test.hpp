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

#ifndef BONEFISH_TEST_UNIT_BONEFISH_TEST_HPP
#define BONEFISH_TEST_UNIT_BONEFISH_TEST_HPP

// Minimal assertion harness for the bonefish unit tests. No external
// dependencies so the same tests build against the Boost-based baseline
// tree and the Boost-free ported tree without changes.

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace bonefish_test {

template<typename T>
std::string to_display(const T& value)
{
    // Diagnostics only, never affects pass/fail.
    std::ostringstream os;
    os << value;
    return os.str();
}

template<>
inline std::string to_display<bool>(const bool& value)
{
    return value ? "true" : "false";
}

template<>
inline std::string to_display<std::string>(const std::string& value)
{
    return "\"" + value + "\"";
}

template<>
inline std::string to_display<const char*>(const char* const& value)
{
    return std::string("\"") + value + "\"";
}

struct case_entry {
    const char* name;
    void (*fn)();
};

inline std::vector<case_entry>& registry()
{
    static std::vector<case_entry> cases;
    return cases;
}

inline int& failure_count()
{
    static int failures = 0;
    return failures;
}

struct registrar {
    registrar(const char* name, void (*fn)())
    {
        registry().push_back(case_entry{ name, fn });
    }
};

inline void fail(const char* file, int line, const std::string& what)
{
    ++failure_count();
    std::fprintf(stderr, "FAILED %s:%d: %s\n", file, line, what.c_str());
}

inline void check_true(bool cond, const char* file, int line, const char* expr)
{
    if (!cond) {
        fail(file, line, expr);
    }
}

template<typename T, typename U>
void check_eq(const T& actual, const U& expected,
        const char* file, int line, const char* expr)
{
    if (!(actual == expected)) {
        std::string msg = expr;
        msg += " (actual=";
        msg += to_display(actual);
        msg += ", expected=";
        msg += to_display(expected);
        msg += ")";
        fail(file, line, msg);
    }
}

inline int run_all(const char* suite_name)
{
    int cases_run = 0;
    for (const auto& c : registry()) {
        int before = failure_count();
        c.fn();
        ++cases_run;
        const char* status = (failure_count() == before) ? "ok" : "FAIL";
        std::printf("[%s] %s :: %s\n", status, suite_name, c.name);
    }
    if (failure_count() != 0) {
        std::printf("%d assertion(s) failed\n", failure_count());
        return 1;
    }
    std::printf("%d case(s) passed\n", cases_run);
    return 0;
}

} // namespace bonefish_test

#define BF_TEST(name) \
    static void name(); \
    static ::bonefish_test::registrar registrar_##name(#name, &name); \
    static void name()

#define BF_ASSERT_EQ(actual, expected) \
    ::bonefish_test::check_eq((actual), (expected), __FILE__, __LINE__, \
            #actual " == " #expected)

#define BF_ASSERT_TRUE(cond) \
    ::bonefish_test::check_true((cond), __FILE__, __LINE__, "true == " #cond)

#define BF_RUN_ALL(suite_name) \
    int main() { return ::bonefish_test::run_all(suite_name); }

#endif // BONEFISH_TEST_UNIT_BONEFISH_TEST_HPP
