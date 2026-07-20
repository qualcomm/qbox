add_executable(lua_test lua_test.cc)
target_link_libraries(lua_test qbox-common gtest gmock)
add_test(NAME lua_test COMMAND lua_test --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/../test.lua --param top.cmdvalue=1010 --p top.allvalue=1050)
set_tests_properties(lua_test PROPERTIES TIMEOUT 30)