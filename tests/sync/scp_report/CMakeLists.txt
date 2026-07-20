macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
    target_link_libraries(${test} qbox-common gtest gmock)
    add_test(NAME ${test} COMMAND ${test} --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/lua_test.lua)
    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

gs_add_test(scp_report_thread)
