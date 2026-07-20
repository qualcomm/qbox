SET(LIBQEMU_TARGETS "riscv64")

macro(gs_add_test test lua_file)
    add_executable(${test} ${test}.cc)
    target_link_libraries(${test} PRIVATE router gs_memory qbox-common gtest gmock)
    add_test(NAME ${test} COMMAND ${test} --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/${lua_file}.lua)
    set_tests_properties(${test} PROPERTIES TIMEOUT 20)
endmacro(gs_add_test test lua_file)

gs_add_test(factory_platform factory_platform)