/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cci_configuration>
#include <cciutils.h>
#include <gtest/gtest.h>
#include <luafile_tool.h>
#include <systemc>

namespace {
const char* error_script;
}

TEST(lua_error_test, execution_failure_stops_before_cci_conversion)
{
    gs::ConfigurableBroker broker;
    auto handle = broker.create_broker_handle(cci::cci_originator("lua_error_test"));
    gs::LuaFile_Tool lua;

    EXPECT_NE(lua.config(handle, error_script), 0);
    EXPECT_FALSE(handle.has_preset_value("partial_value"));
}

int sc_main(int argc, char** argv)
{
    if (argc != 2) {
        return 2;
    }
    error_script = argv[1];
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
