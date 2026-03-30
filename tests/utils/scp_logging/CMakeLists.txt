add_executable(scp_logging_test scp_logging_test.cc)
target_link_libraries(scp_logging_test ${TARGET_LIBS} gtest gmock scp::tlm_extensions::initiator_id scp::tlm_extensions::path_trace)
add_test(NAME scp_logging_test COMMAND scp_logging_test --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/../test.lua --param log_level=0)
set_tests_properties(scp_logging_test PROPERTIES TIMEOUT 30)