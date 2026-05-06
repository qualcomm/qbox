macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
    target_link_libraries(${test} PRIVATE gtest gmock gs_memory reg_router smmuv3 ${TARGET_LIBS})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

gs_add_test(smmuv3-tests)
