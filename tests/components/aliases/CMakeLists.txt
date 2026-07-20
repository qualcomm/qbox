macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
     target_link_libraries(${test}  gtest gmock gs_memory router qbox-common)
    add_test(NAME ${test} COMMAND ${test} --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/conf-test.lua)
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

gs_add_test(aliases-mapping-test)
