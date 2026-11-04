add_executable(lua_test lua_test.cc)
target_link_libraries(lua_test qbox-common gtest gmock)
add_test(NAME lua_test COMMAND lua_test --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/../test.lua --param top.cmdvalue=1010 --p top.allvalue=1050)
set_tests_properties(lua_test PROPERTIES TIMEOUT 30)

add_executable(lua_error_test lua_error_test.cc)
target_link_libraries(lua_error_test qbox-common gtest gmock)
add_test(NAME lua_error_test COMMAND lua_error_test
    ${CMAKE_CURRENT_SOURCE_DIR}/error.lua)
set_tests_properties(lua_error_test PROPERTIES TIMEOUT 30)
