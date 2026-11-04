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

// Test bench for multiple target non-overlapping tests
class MultipleTargetNonOverlappingTestBench : public RouterTestBenchSimple
{
public:
    MultipleTargetNonOverlappingTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup two targets with non-overlapping address ranges
        setup_target(0, 0x1000, 0x100, true, 0); // ID 0, base 0x1000, size 0x100
        setup_target(1, 0x2000, 0x200, true, 0); // ID 1, base 0x2000, size 0x200
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access target 0
        do_load_and_check(0, 0x1000, 4);
        do_store_and_check(0, 0x1050, 8);

        // Access target 1
        do_load_and_check(1, 0x2000, 4);
        do_store_and_check(1, 0x2100, 16);

        // Attempt to access an unmapped region
        do_load_and_check(-1, 0x3000, 4, false, tlm::TLM_ADDRESS_ERROR_RESPONSE);
    }
};

// Test bench for overlapping address priority tests
class OverlappingAddressPriorityTestBench : public RouterTestBenchSimple
{
public:
    OverlappingAddressPriorityTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup two targets with overlapping address ranges but different priorities
        // Higher priority value means higher actual priority (0 beats negative values)
        setup_target(0, 0x1000, 0x200, true, -10); // ID 0, base 0x1000, size 0x200, lower priority
        setup_target(1, 0x1050, 0x100, true, 0);   // ID 1, base 0x1050, size 0x100, HIGHER priority
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access within the overlapping region (0x1050 - 0x114F)
        // Expect target 1 (higher priority) to receive the transaction
        do_load_and_check(1, 0x1050, 4);
        do_store_and_check(1, 0x10A0, 8);

        // Access outside the overlapping region but within target 0's range (e.g., 0x1000 - 0x104F)
        // Expect target 0 to receive the transaction
        do_load_and_check(0, 0x1000, 4);
        do_store_and_check(0, 0x1030, 8);
    }
};

// Test bench for relative addressing tests
class RelativeAddressingTestBench : public RouterTestBenchSimple
{
public:
    RelativeAddressingTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Target 0: use_offset = true (default)
        setup_target(0, 0x1000, 0x100, true);

        // Target 1: use_offset = false
        setup_target(1, 0x2000, 0x200, false);
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access target 0 with relative addressing
        // Expect m_last_target_txn.addr to be relative to 0x1000
        do_load_and_check(0, 0x1010, 4); // Access 0x1010, expect target 0 to see 0x10
        ASSERT_EQ(get_last_target_txn().addr, 0x10);

        do_store_and_check(0, 0x10A0, 8); // Access 0x10A0, expect target 0 to see 0xA0
        ASSERT_EQ(get_last_target_txn().addr, 0xA0);

        // Access target 1 with absolute addressing
        // Expect m_last_target_txn.addr to be the absolute address
        do_load_and_check(1, 0x2020, 4); // Access 0x2020, expect target 1 to see 0x2020
        ASSERT_EQ(get_last_target_txn().addr, 0x2020);

        do_store_and_check(1, 0x2150, 8); // Access 0x2150, expect target 1 to see 0x2150
        ASSERT_EQ(get_last_target_txn().addr, 0x2150);
    }
};

// Test bench for debug transport tests
class DebugTransportTestBench : public RouterTestBenchSimple
{
public:
    DebugTransportTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // ID 0, base address 0x1000, size 0x100
        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Perform debug read to a mapped address
        do_load_and_check(0, 0x1010, 4, true);                        // true for debug
        ASSERT_EQ(get_initiator().get_last_transport_debug_ret(), 4); // Expect 4 bytes transferred

        // Perform debug write to a mapped address
        do_store_and_check(0, 0x1020, 8, true);                       // true for debug
        ASSERT_EQ(get_initiator().get_last_transport_debug_ret(), 8); // Expect 8 bytes transferred

        // Perform debug read to an unmapped address
        do_load_and_check(-1, 0x3000, 4, true, tlm::TLM_ADDRESS_ERROR_RESPONSE); // true for debug
        ASSERT_EQ(get_initiator().get_last_transport_debug_ret(),
                  0); // Expect 0 bytes transferred for unmapped

        // Perform debug write to an unmapped address
        do_store_and_check(-1, 0x3004, 4, true, tlm::TLM_ADDRESS_ERROR_RESPONSE); // true for debug
        ASSERT_EQ(get_initiator().get_last_transport_debug_ret(),
                  0); // Expect 0 bytes transferred for unmapped
    }
};

// Test case: Multiple Target Routing (Non-Overlapping)
TEST_BENCH(MultipleTargetNonOverlappingTestBench, MultipleTargetNonOverlapping)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Overlapping Address Ranges (Priority Test)
TEST_BENCH(OverlappingAddressPriorityTestBench, OverlappingAddressPriority)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Relative Addressing
TEST_BENCH(RelativeAddressingTestBench, RelativeAddressing)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: TLM Debug Transport
TEST_BENCH(DebugTransportTestBench, DebugTransport)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test bench for chained target (exercises b_transport logging suppression)
class ChainedTargetTestBench : public RouterTestBenchSimple
{
public:
    ChainedTargetTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // ID 0, base 0x1000, size 0x100
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Transaction through chained target should succeed (TLM_OK_RESPONSE)
        // The code path `if (!ti->chained)` is exercised as false
        do_load_and_check(0, 0x1010, 4);
        do_store_and_check(0, 0x1050, 8);
    }
};

// Test bench for lazy_init=true (exercises lazy_initialize during simulation)
class LazyInitTestBench : public RouterTestBenchSimple
{
public:
    LazyInitTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // ID 0, base 0x1000, size 0x100
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // First b_transport triggers lazy_initialize() during simulation
        do_load_and_check(0, 0x1010, 4);
        do_store_and_check(0, 0x1050, 8);
    }
};

// Test bench for pre-set response status guard
class PreSetResponseTestBench : public RouterTestBenchSimple
{
public:
    PreSetResponseTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // ID 0, base 0x1000, size 0x100
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Manually craft a transaction with pre-set error response
        tlm::tlm_generic_payload txn;
        uint8_t data[4] = { 0 };
        txn.set_address(0x1010);
        txn.set_data_length(4);
        txn.set_data_ptr(data);
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_streaming_width(4);
        txn.set_byte_enable_ptr(nullptr);
        txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE); // Pre-set error!

        m_last_target_txn.valid = false;
        m_initiator.do_b_transport(txn);

        // The router finds the target but skips b_transport due to response status guard
        // The target callback should NOT have been invoked
        ASSERT_FALSE(m_last_target_txn.valid);
        // Response status should remain as we set it
        ASSERT_EQ(txn.get_response_status(), tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
};

// Test bench for TLM_IGNORE_COMMAND (exercises txn_tostring IGNORE case)
class IgnoreCommandTestBench : public RouterTestBenchSimple
{
public:
    IgnoreCommandTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        setup_target(0, 0x1000, 0x100); // ID 0, base 0x1000, size 0x100
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Manually craft a transaction with TLM_IGNORE_COMMAND
        tlm::tlm_generic_payload txn;
        uint8_t data[4] = { 0 };
        txn.set_address(0x1010);
        txn.set_data_length(4);
        txn.set_data_ptr(data);
        txn.set_command(tlm::TLM_IGNORE_COMMAND);
        txn.set_streaming_width(4);
        txn.set_byte_enable_ptr(nullptr);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        m_initiator.do_b_transport(txn);

        // The transaction should succeed (target returns TLM_OK_RESPONSE for IGNORE)
        ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    }
};

// Test case: Chained Target
TEST_BENCH(ChainedTargetTestBench, ChainedTarget)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Lazy Initialization
TEST_BENCH(LazyInitTestBench, LazyInit)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Pre-Set Response Status Guard
TEST_BENCH(PreSetResponseTestBench, PreSetResponse)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: TLM_IGNORE_COMMAND
TEST_BENCH(IgnoreCommandTestBench, IgnoreCommand)
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
    std::vector<std::string> test_bench_names = { "MultipleTargetNonOverlappingTestBench",
                                                  "OverlappingAddressPriorityTestBench",
                                                  "RelativeAddressingTestBench",
                                                  "DebugTransportTestBench",
                                                  "ChainedTargetTestBench",
                                                  "LazyInitTestBench",
                                                  "PreSetResponseTestBench",
                                                  "IgnoreCommandTestBench" };

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

    // Set chained=true for ChainedTarget's target socket
    broker.set_preset_cci_value("ChainedTarget.Target_0.simple_target_socket_0.chained", cci::cci_value(true),
                                originator);

    // Set lazy_init=true for LazyInit's router
    broker.set_preset_cci_value("LazyInit.router.lazy_init", cci::cci_value(true), originator);

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
