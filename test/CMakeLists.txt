add_subdirectory(functional)

#
# Unit tests (P0 guardrail harness). Each file under test/unit is a
# standalone binary built on the mini-harness in test/unit/bonefish_test.hpp
# so the same sources compile against the Boost-based baseline tree and the
# Boost-free ported tree without changes.
#

function(bonefish_unit_test name)
    add_executable(${name} unit/${name}.cpp)
    add_dependencies(${name} bonefish)
    target_link_libraries(${name}
        bonefish
        ${Boost_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT})
    if (RT_FOUND)
        target_link_libraries(${name} ${RT_LIBRARIES})
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

bonefish_unit_test(test_wamp_uri)
bonefish_unit_test(test_messages)
bonefish_unit_test(test_trace)

if(NOT MSVC)
    # The vendored msgpack-c headers trigger -Wclass-memaccess under newer
    # GCC; keep -Werror working for the test's own code.
    target_compile_options(test_messages PRIVATE
        -Wno-error=class-memaccess -Wno-error=deprecated-declarations)
endif()

#
# End-to-end suite: drives a built daemon binary over RawSocket (TCP +
# UNIX domain socket) and WebSocket (msgpack + JSON), plus the CLI matrix.
# Requires python3 with the msgpack and websockets modules installed.
#

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME e2e_daemon
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/e2e/e2e_driver.py
                --daemon $<TARGET_FILE:bonefish_bin>
                --example $<TARGET_FILE:integration>)
    set_tests_properties(e2e_daemon PROPERTIES TIMEOUT 600)
endif()
