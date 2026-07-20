macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
    target_include_directories(${test} PRIVATE
        ${PROJECT_SOURCE_DIR}/tests/qbox/include
        ${PROJECT_SOURCE_DIR}/systemc-components/common/include
        ${PROJECT_SOURCE_DIR}/systemc-components/common/include/tests
    )
     target_link_libraries(${test} PRIVATE gtest gmock router pass qbox-common)
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()
gs_add_test(router-tests)
gs_add_test(router-tests-extended)
gs_add_test(router-addressmap-tests)
gs_add_test(router-cache-bench-enhanced)
gs_add_test(router-advanced-overlap-tests)
gs_add_test(router-shadowing-warning-test)
gs_add_test(router-tests-new)
gs_add_test(router-thread-safety-test)
gs_add_test(router-coverage-tests)

set_tests_properties(router-cache-bench-enhanced PROPERTIES TIMEOUT 60 SKIP_TEST TRUE)
