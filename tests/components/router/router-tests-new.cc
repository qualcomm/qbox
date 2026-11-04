/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <gtest/gtest.h>
#include <cci/utils/broker.h>

#include "router-bench.h" // Includes RouterTestBenchSimple

// Test bench for multiple distinct targets
class MultipleDistinctTargetsTestBench : public RouterTestBenchSimple
{
public:
    MultipleDistinctTargetsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup three targets
        setup_target(0, 0x1000, 0x100); // ID 0, base address 0x1000, size 0x100
        setup_target(1, 0x2000, 0x200); // ID 1, base address 0x2000, size 0x200
        setup_target(2, 0x3000, 0x300); // ID 2, base address 0x3000, size 0x300
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test access to target 0
        do_load_and_check(0, 0x1050, 4);
        do_store_and_check(0, 0x10A0, 8);

        // Test access to target 1
        do_load_and_check(1, 0x2100, 4);
        do_store_and_check(1, 0x21B0, 8);

        // Test access to target 2
        do_load_and_check(2, 0x3200, 4);
        do_store_and_check(2, 0x32C0, 8);
    }
};

// Test bench for unmapped areas with multiple targets
class UnmappedWithMultipleTargetsTestBench : public RouterTestBenchSimple
{
public:
    UnmappedWithMultipleTargetsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100);
        setup_target(1, 0x3000, 0x100);
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access an address between target 0 and target 1
        do_load_and_check(-1, 0x2500, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);
        do_store_and_check(-1, 0x2504, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);

        // Access an address beyond all mapped targets
        do_load_and_check(-1, 0x4000, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);
        do_store_and_check(-1, 0x4004, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// Test bench for overlapping ranges with different priorities
class OverlappingRangesDifferentPrioritiesTestBench : public RouterTestBenchSimple
{
public:
    OverlappingRangesDifferentPrioritiesTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup Target 0: higher priority (priority = 0)
        setup_target(0, 0x5000, 0x1000, true, 0); // ID 0, base 0x5000, size 0x1000, priority 0
        // Setup Target 1: lower priority (priority = -1)
        setup_target(1, 0x5500, 0x500, true, -1); // ID 1, base 0x5500, size 0x500, priority -1
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access an address that falls within both ranges (e.g., 0x5600)
        // Expect it to be routed to Target 0 (higher priority)
        do_load_and_check(0, 0x5600, 4);
        do_store_and_check(0, 0x5604, 4);

        // Access an address only in Target 0's range (e.g., 0x5100)
        do_load_and_check(0, 0x5100, 4);
        do_store_and_check(0, 0x5104, 4);
    }
};

// Test bench for transactions crossing boundaries
class TransactionCrossesBoundaryTestBench : public RouterTestBenchSimple
{
public:
    TransactionCrossesBoundaryTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // Target 0: 0x1000 - 0x10FF
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Try a write that starts in Target 0 but extends beyond its boundary
        // Expected: TLM_ADDRESS_ERROR_RESPONSE
        do_store_and_check(-1, 0x10FE, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE); // 0x10FE-0x1101
        // Try a read that starts in Target 0 but extends beyond its boundary
        // Expected: TLM_ADDRESS_ERROR_RESPONSE
        do_load_and_check(-1, 0x10FD, 8, false, tlm::TLM_ADDRESS_ERROR_RESPONSE); // 0x10FD-0x1104
    }
};

// Test bench for DMI requests
class DmiRequestsTestBench : public RouterTestBenchSimple
{
public:
    DmiRequestsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x7000, 0x1000); // Target 0 for DMI testing
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Perform a valid DMI request
        do_good_dmi_request_and_check(0x7050, 0x7000, 0x7FFF, 0); // Test with address inside target, expect target 0

        // Perform a bad DMI request (outside mapped range)
        do_bad_dmi_request_and_check(0x8000); // Address outside target 0
    }
};

// Test bench for debug transport
class DebugTransportTestBench : public RouterTestBenchSimple
{
public:
    DebugTransportTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x9000, 0x100); // Target 0 for Debug testing
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Perform a debug write and check
        do_store_and_check(0, 0x9010, 4, true, tlm::TLM_OK_RESPONSE); // Debug write, expect to hit Target 0
        do_load_and_check(0, 0x9010, 4, true, tlm::TLM_OK_RESPONSE);  // Debug read, expect to hit Target 0

        // Perform a debug write to unmapped address
        do_store_and_check(-1, 0x9500, 4, true, tlm::TLM_ADDRESS_ERROR_RESPONSE); // Debug write to unmapped
        do_load_and_check(-1, 0x9500, 4, true, tlm::TLM_ADDRESS_ERROR_RESPONSE);  // Debug read to unmapped
    }
};

// Test case: Multiple targets mapped to distinct address ranges
TEST_BENCH(MultipleDistinctTargetsTestBench, MultipleDistinctTargets)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Transactions to unmapped areas when multiple targets are present
TEST_BENCH(UnmappedWithMultipleTargetsTestBench, UnmappedWithMultipleTargets)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Overlapping address ranges with different priorities
TEST_BENCH(OverlappingRangesDifferentPrioritiesTestBench, OverlappingRangesDifferentPriorities)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Transaction crossing target boundaries (into unmapped space)
TEST_BENCH(TransactionCrossesBoundaryTestBench, TransactionCrossesBoundary)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: DMI requests
TEST_BENCH(DmiRequestsTestBench, DmiRequests)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Debug Transport Interface
TEST_BENCH(DebugTransportTestBench, DebugTransport)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test bench for DMI without use_offset (exercises the use_offset=false path in get_direct_mem_ptr)
class DmiAbsoluteAddressingTestBench : public RouterTestBenchSimple
{
public:
    DmiAbsoluteAddressingTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x5000, 0x1000, false); // use_offset=false
        RouterTestBenchSimple::before_end_of_elaboration();

        // Override the DMI callback to return absolute addresses
        // When use_offset=false, the target works with absolute addresses
        m_targets[0]->register_get_direct_mem_ptr_cb([](uint64_t addr, TlmDmi& dmi_data) -> bool {
            dmi_data.allow_read_write();
            dmi_data.set_dmi_ptr(nullptr);
            dmi_data.set_start_address(0x5000); // Absolute start
            dmi_data.set_end_address(0x5FFF);   // Absolute end
            return true;
        });
    }

    void test_bench_body() override
    {
        // Request DMI at address inside target
        // With use_offset=false, the if(ti->use_offset) branch is NOT taken
        // DMI addresses from the target are used as-is (no translation)
        bool ret = m_initiator.do_dmi_request(0x5050);
        ASSERT_TRUE(ret);

        const tlm::tlm_dmi& dmi_data = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi_data.get_start_address(), 0x5000u);
        ASSERT_EQ(dmi_data.get_end_address(), 0x5FFFu);
    }
};

// Test case: DMI Absolute Addressing (use_offset=false)
TEST_BENCH(DmiAbsoluteAddressingTestBench, DmiAbsoluteAddressing)
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
    cci::cci_originator originator("sc_main"); // Create a default originator

    // Set for all test benches
    std::vector<std::string> test_bench_names = { "MultipleDistinctTargetsTestBench",
                                                  "UnmappedWithMultipleTargetsTestBench",
                                                  "OverlappingRangesDifferentPrioritiesTestBench",
                                                  "TransactionCrossesBoundaryTestBench",
                                                  "DmiRequestsTestBench",
                                                  "DebugTransportTestBench",
                                                  "DmiAbsoluteAddressingTestBench" };

    for (const auto& bench_name : test_bench_names) {
        std::string default_target_socket_name = bench_name + "_DefaultTarget.simple_target_socket_0";
        broker.set_preset_cci_value(default_target_socket_name + ".address", cci::cci_value(static_cast<uint64_t>(0)),
                                    originator);
        broker.set_preset_cci_value(default_target_socket_name + ".size",
                                    cci::cci_value(std::numeric_limits<uint64_t>::max()), originator);
        broker.set_preset_cci_value(default_target_socket_name + ".relative_addresses", cci::cci_value(true),
                                    originator);
        broker.set_preset_cci_value(default_target_socket_name + ".priority", cci::cci_value(-100),
                                    originator); // Lower priority
    }

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
