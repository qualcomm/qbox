add_executable(cci_test cci_test.cc)
target_link_libraries(cci_test ${TARGET_LIBS} gtest gmock)
add_test(NAME cci_test COMMAND cci_test --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/../test.lua -p MyTopThree.MyCtrl.log_level=0)
set_tests_properties(cci_test PROPERTIES TIMEOUT 30)