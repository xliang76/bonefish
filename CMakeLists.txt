cmake_minimum_required(VERSION 3.16)
project(bonefish VERSION 0.1.0 LANGUAGES CXX)

# C++20 is still blocked on the vendored websocketpp pin (0.8.2, xliang76
# fork master): its endpoint/logger headers use template-ids for constructors
# and destructors, which C++20 rejects (verified: the full tree builds clean
# under -std=c++20 with a 13-line mechanical fix to 5 headers; patch saved as
# websocketpp-cpp20-fix in the program notes). The fix exists upstream but
# only on the zaphoyd develop branch, which the xliang76 fork has not synced,
# so pinning it would break the fetch-proven xliang76-only rule (GIT-
# OWNERSHIP.md). Unblock path: push the patch to xliang76/websocketpp
# (owner approval), re-pin, set CMAKE_CXX_STANDARD 20 here.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(shared "build bonefish as a shared library" OFF)
option(stdlib "When building with clang, you can choose libc++ (default) or libstdc++" OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (NOT stdlib)
        set(stdlib "libc++")
    endif()
    if (CMAKE_GENERATOR STREQUAL "Ninja")
        set(CMAKE_CXX_FLAGS "-fcolor-diagnostics ${CMAKE_CXX_FLAGS}")
    endif()
    if (stdlib STREQUAL "libstdc++")
        # libstdc++ defines template<typename> friend class hash, which causes warnings
        set(CMAKE_CXX_FLAGS "-Wno-mismatched-tags ${CMAKE_CXX_FLAGS}")
    endif()
    set(CMAKE_CXX_FLAGS "-stdlib=${stdlib} -Wno-unused-local-typedefs -Wno-unknown-warning-option ${CMAKE_CXX_FLAGS}")
else()
    if(stdlib)
        message(STATUS "stdlib option (set to ${stdlib}) is ignored for ${CMAKE_CXX_COMPILER_ID} compiler")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if (CMAKE_GENERATOR STREQUAL "Ninja")
        set(CMAKE_CXX_FLAGS "-fdiagnostics-color=always ${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "-fdiagnostics-color=auto ${CMAKE_CXX_FLAGS}")
    endif()
endif()

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "-Wall -Werror -Wno-unused-variable ${CMAKE_CXX_FLAGS}")
else()
    add_definitions(-D_WEBSOCKETPP_CPP11_CHRONO_)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O0 ${CMAKE_CXX_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_DEBUG "-O0 ${CMAKE_C_FLAGS_DEBUG}")

set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules/")

if(shared)
    set(RT_USE_STATIC_LIBS OFF)
else()
    set(RT_USE_STATIC_LIBS ON)
endif()

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0500)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

# Vendored standalone Asio (third-party/asio) replaces Boost.Asio
# (BOOST-REMOVAL.md step 2f'); ASIO_STANDALONE also switches websocketpp
# (>=0.8.0) onto the same Asio + std::error_code. No find_package(Boost)
# remains: the Boost removal completed in step 2g'.
add_definitions(-DASIO_STANDALONE)

find_package(RT)
find_package(Threads)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third-party/asio/asio/include
    ${CMAKE_SOURCE_DIR}/third-party/cppcodec
    ${CMAKE_SOURCE_DIR}/third-party/json-msgpack/include
    ${CMAKE_SOURCE_DIR}/third-party/msgpack-c/include
    ${CMAKE_SOURCE_DIR}/third-party/rapidjson/include
    ${CMAKE_SOURCE_DIR}/third-party/websocketpp
)

link_directories(${CMAKE_INSTALL_PREFIX}/lib)

enable_testing()

add_subdirectory(examples)
add_subdirectory(src)
add_subdirectory(daemon)
add_subdirectory(test)
add_subdirectory(bench)
