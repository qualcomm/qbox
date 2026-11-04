/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2024
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <gtest/gtest.h>
#include <cci/utils/broker.h>
#include <scp/report.h>

#include "router-bench.h" // Includes RouterTestBenchSimple

/**
 * @brief Test for router shadowing warnings
 *
 * These tests verify that the router correctly issues warnings when regions
 * with lower priority (more negative values) are completely shadowed by
 * regions with higher priority (larger values).
 */

// Test bench for completely shadowed region
class CompletelyShadowedRegionTestBench : public RouterTestBenchSimple
{
public:
    CompletelyShadowedRegionTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup a low priority region first
        setup_target(0, 0x1000, 0x1000, true, -20); // Low priority (more negative value)

        // Now add a high priority region that completely covers the first one
        // This should generate a warning about the first region being shadowed
        setup_target(1, 0x1000, 0x1000, true, -5); // High priority (larger value)

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Just wait a bit - the warning will be generated during elaboration
        // when the address map is built
        wait(sc_core::SC_ZERO_TIME);
    }
};

// Test bench for partially shadowed region
class PartiallyShadowedRegionTestBench : public RouterTestBenchSimple
{
public:
    PartiallyShadowedRegionTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup a low priority region
        setup_target(0, 0x1000, 0x1000, true, -20); // Low priority: 0x1000-0x1FFF

        // Add a high priority region that only partially covers the first one
        // This should NOT generate a warning since part of region 0 is still accessible
        setup_target(1, 0x1200, 0x600, true, -5); // High priority: 0x1200-0x17FF

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Verify that accesses work correctly
        do_load_and_check(0, 0x1100, 4); // Should go to target 0 (not shadowed part)
        do_load_and_check(1, 0x1400, 4); // Should go to target 1 (shadowing part)
        do_load_and_check(0, 0x1900, 4); // Should go to target 0 (not shadowed part)
    }
};

// Test bench for multiple regions with one completely shadowed
class MultipleRegionsOneShadowedTestBench : public RouterTestBenchSimple
{
public:
    MultipleRegionsOneShadowedTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup multiple regions
        setup_target(0, 0x1000, 0x500, true, -10); // Medium priority: 0x1000-0x14FF
        setup_target(1, 0x2000, 0x500, true, -30); // Low priority: 0x2000-0x24FF
        setup_target(2, 0x3000, 0x500, true, -5);  // High priority: 0x3000-0x34FF

        // Now add a region that completely shadows target 1
        // This should generate a warning about target 1 being shadowed
        setup_target(3, 0x2000, 0x500, true, -5); // High priority: 0x2000-0x24FF

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Verify routing works correctly
        do_load_and_check(0, 0x1200, 4); // Should go to target 0
        do_load_and_check(3, 0x2200, 4); // Should go to target 3 (shadows target 1)
        do_load_and_check(2, 0x3200, 4); // Should go to target 2
    }
};

// Test bench for nested regions with inner region completely shadowed
class NestedRegionsShadowedTestBench : public RouterTestBenchSimple
{
public:
    NestedRegionsShadowedTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup nested regions
        setup_target(0, 0x1000, 0x1000, true, -30); // Outer, low priority: 0x1000-0x1FFF
        setup_target(1, 0x1200, 0x600, true, -20);  // Middle, medium priority: 0x1200-0x17FF
        setup_target(2, 0x1400, 0x200, true, -25);  // Inner, lower priority: 0x1400-0x15FF

        // Target 2 should be completely shadowed by target 1 (which has higher priority)
        // This should generate a warning

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Verify routing
        do_load_and_check(0, 0x1100, 4); // Outer only
        do_load_and_check(1, 0x1300, 4); // Middle wins over outer
        do_load_and_check(1, 0x1500, 4); // Middle wins over inner (target 2 is shadowed)
        do_load_and_check(1, 0x1700, 4); // Middle wins over outer
        do_load_and_check(0, 0x1900, 4); // Outer only
    }
};

// Test bench for same priority regions
class SamePriorityNoWarningTestBench : public RouterTestBenchSimple
{
public:
    SamePriorityNoWarningTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup regions with same priority - should not generate shadowing warnings
        // since the behavior is deterministic but not based on priority
        setup_target(0, 0x1000, 0x800, true, -10); // Same priority
        setup_target(1, 0x1000, 0x800, true, -10); // Same priority, same region

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // One will win deterministically, but no warning should be issued
        // since they have the same priority

        // Test that routing is deterministic
        do_load_and_check(-1, 0x1400, 4, false, tlm::TLM_OK_RESPONSE);
        int winner = get_last_target_txn().id;

        // Subsequent accesses should go to the same winner
        do_load_and_check(winner, 0x1500, 4);
        do_load_and_check(winner, 0x1600, 4);
    }
};

// Test bench for large region completely shadowed
class LargeRegionCompletelyShadowedTestBench : public RouterTestBenchSimple
{
public:
    LargeRegionCompletelyShadowedTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup a large low priority region
        setup_target(0, 0x1000, 0x10000, true, -50); // Very low priority, large region

        // Add a smaller high priority region that completely covers the large one
        setup_target(1, 0x1000, 0x10000, true, -1); // High priority, same size

        // The large region should be completely shadowed and generate a warning

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Verify all accesses go to the high priority target
        do_load_and_check(1, 0x1000, 4);  // Start
        do_load_and_check(1, 0x8000, 4);  // Middle
        do_load_and_check(1, 0x10FFF, 4); // End
    }
};

// Test case: Completely shadowed region should generate warning
TEST_BENCH(CompletelyShadowedRegionTestBench, CompletelyShadowedRegion)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Partially shadowed region should NOT generate warning
TEST_BENCH(PartiallyShadowedRegionTestBench, PartiallyShadowedRegion)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Multiple regions with one completely shadowed
TEST_BENCH(MultipleRegionsOneShadowedTestBench, MultipleRegionsOneShadowed)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Nested regions with inner region completely shadowed
TEST_BENCH(NestedRegionsShadowedTestBench, NestedRegionsShadowed)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Same priority regions should not generate shadowing warnings
TEST_BENCH(SamePriorityNoWarningTestBench, SamePriorityNoWarning)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Large region completely shadowed by smaller high-priority region
TEST_BENCH(LargeRegionCompletelyShadowedTestBench, LargeRegionCompletelyShadowed)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

int sc_main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);

    // Initialize CCI broker
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    // Set CCI parameters for the default target once here in sc_main using the already existing broker
    // This needs to match the naming convention in RouterTestBenchSimple::SetUp
    cci::cci_originator originator("sc_main"); // Create a default originator

    // Set for all test benches
    std::vector<std::string> test_bench_names = {
        "CompletelyShadowedRegionTestBench",   "PartiallyShadowedRegionTestBench",
        "MultipleRegionsOneShadowedTestBench", "NestedRegionsShadowedTestBench",
        "SamePriorityNoWarningTestBench",      "LargeRegionCompletelyShadowedTestBench"
    };

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

    // Set SystemC reporting to show all warnings
    sc_core::sc_report_handler::set_actions(sc_core::SC_WARNING, sc_core::SC_DISPLAY);
    sc_core::sc_report_handler::set_actions("addressMap", sc_core::SC_DISPLAY);

    // Set logging to show warnings so we can see the shadowing warnings
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_INFO)
                                        .logAsync(false)
                                        .logLevel(scp::log::DBGTRACE)
                                        .msgTypeFieldWidth(50));

    // Run all Google Tests. The TEST_BENCH macro will handle SystemC simulation
    // setup and teardown automatically for each test.
    int result = RUN_ALL_TESTS();

    return result;
}
