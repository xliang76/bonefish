#
# Benchmark harness for the P4 performance gate (PROGRESS.md). These are
# measurement tools, not correctness tests, so they are built but not
# registered with CTest. bench/BASELINE.md records the recorded baseline
# numbers; every migration phase must re-run them and stay inside the
# agreed +/-5% window.
#

# Micro-benchmark of bonefish::is_valid_uri(); links the library under test.
add_executable(uri_bench uri_bench.cpp)
add_dependencies(uri_bench bonefish)
target_link_libraries(uri_bench
    bonefish
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT})
if (RT_FOUND)
    target_link_libraries(uri_bench ${RT_LIBRARIES})
endif()

# RawSocket throughput/latency client for a live daemon. Talks the wire
# protocol directly (plain BSD sockets + msgpack-c headers), so the same
# binary drives the Boost baseline and the Boost-free port and needs no
# Boost itself.
add_executable(rawsocket_bench rawsocket_bench.cpp)
target_link_libraries(rawsocket_bench ${CMAKE_THREAD_LIBS_INIT})

if(NOT MSVC)
    target_compile_options(rawsocket_bench PRIVATE
        -Wno-error=class-memaccess -Wno-error=deprecated-declarations)
endif()
