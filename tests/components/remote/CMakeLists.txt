macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
    target_link_libraries(${test} PRIVATE gtest gmock router gs_memory pass ${TARGET_LIBS} rpc)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

add_executable(remote-tests-remote remote.cc)
target_include_directories(remote-tests-remote
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../include/greensocs/base-components/
)
target_link_libraries(remote-tests-remote PRIVATE router gs_memory pass ${TARGET_LIBS} rpc)

gs_add_test(remote-tests)
