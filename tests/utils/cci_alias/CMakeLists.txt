add_executable(cci_alias_test cci_alias_test.cc)
target_link_libraries(cci_alias_test qbox-common gtest gmock)
add_test(NAME cci_alias_test COMMAND cci_alias_test --param MyTopThree.lua_conf=\"${CMAKE_CURRENT_SOURCE_DIR}/../test.lua\")
set_tests_properties(cci_alias_test PROPERTIES TIMEOUT 30)