/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <gtest/gtest.h>
#include <cci/utils/broker.h>
#include <scp/report.h>

#include "router-bench.h" // Includes RouterTestBenchSimple

// Test bench for simple single target read/write tests
class SimpleSingleTargetTestBench : public RouterTestBenchSimple
{
public:
    SimpleSingleTargetTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup a single target using the composed bench instance
        setup_target(0, 0x1000, 0x100); // ID 0, base address 0x1000, size 0x100
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Perform a read and verify it hits the correct target
        do_load_and_check(0, 0x1000, 4); // Access address 0x1000, 4 bytes, expects target ID 0

        // Perform a write and verify it hits the correct target
        do_store_and_check(0, 0x1008, 8); // Access address 0x1008, 8 bytes, expects target ID 0
    }
};

// Test bench for unmapped address error tests
class UnmappedAddressTestBench : public RouterTestBenchSimple
{
public:
    UnmappedAddressTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup targets in a way that 0x2000 is unmapped
        setup_target(1, 0x1000, 0x100); // ID 1, base address 0x1000, size 0x100
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Attempt to access an address outside the mapped range
        do_load_and_check(-1, 0x2000, 4, false,
                          tlm::TLM_ADDRESS_ERROR_RESPONSE); // -1 indicates no target hit expected
        do_store_and_check(-1, 0x2004, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// Test case: Simple Read/Write to a single target
TEST_BENCH(SimpleSingleTargetTestBench, SimpleSingleTargetReadWrite)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Access to an unmapped address
TEST_BENCH(UnmappedAddressTestBench, UnmappedAddressError)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

int sc_main(int argc, char* argv[])
{
    // Initialize Google Test first
    ::testing::InitGoogleTest(&argc, argv);

    // Initialize CCI broker
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    // Set CCI parameters for the default target once here in sc_main using the already existing broker
    // This needs to match the naming convention in RouterTestBenchSimple::SetUp
    std::string default_target_socket_name = "SimpleSingleTargetTestBench_DefaultTarget.simple_target_socket_0";
    cci::cci_originator originator("sc_main"); // Create a default originator

    broker.set_preset_cci_value(default_target_socket_name + ".address", cci::cci_value(static_cast<uint64_t>(0)),
                                originator);
    broker.set_preset_cci_value(default_target_socket_name + ".size",
                                cci::cci_value(std::numeric_limits<uint64_t>::max()), originator);
    broker.set_preset_cci_value(default_target_socket_name + ".relative_addresses", cci::cci_value(true), originator);
    broker.set_preset_cci_value(default_target_socket_name + ".priority", cci::cci_value(-100),
                                originator); // Lower priority

    // Also set for the second test bench
    std::string default_target_socket_name2 = "UnmappedAddressTestBench_DefaultTarget.simple_target_socket_0";
    broker.set_preset_cci_value(default_target_socket_name2 + ".address", cci::cci_value(static_cast<uint64_t>(0)),
                                originator);
    broker.set_preset_cci_value(default_target_socket_name2 + ".size",
                                cci::cci_value(std::numeric_limits<uint64_t>::max()), originator);
    broker.set_preset_cci_value(default_target_socket_name2 + ".relative_addresses", cci::cci_value(true), originator);
    broker.set_preset_cci_value(default_target_socket_name2 + ".priority", cci::cci_value(-100),
                                originator); // Lower priority

    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::DBGTRACE) // set log level to DBGTRACE = TRACEALL
                                        .msgTypeFieldWidth(50));      // make the msg type column a bit tighter

    // Run all Google Tests. The TEST_BENCH macro will handle SystemC simulation
    // setup and teardown automatically for each test.
    int result = RUN_ALL_TESTS();

    return result;
}
