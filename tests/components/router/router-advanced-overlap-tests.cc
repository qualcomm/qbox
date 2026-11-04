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
 * @brief Advanced router tests for complex region splitting scenarios
 *
 * These tests specifically target the new addressMap implementation with
 * automatic region splitting and priority-based conflict resolution.
 * They cover edge cases and complex scenarios not covered by basic tests.
 */

// Test bench for three-way overlap
class ThreeWayOverlapTestBench : public RouterTestBenchSimple
{
public:
    ThreeWayOverlapTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup three overlapping regions:
        // Region A: 0x1000-0x1FFF (priority -30 - lowest)
        // Region B: 0x1200-0x17FF (priority -10 - medium)
        // Region C: 0x1400-0x15FF (priority 0 - highest)
        setup_target(0, 0x1000, 0x1000, true, -30); // Lowest priority
        setup_target(1, 0x1200, 0x600, true, -10);  // Medium priority
        setup_target(2, 0x1400, 0x200, true, 0);    // Highest priority

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test address 0x1100 - only in Region A
        do_load_and_check(0, 0x1100, 4);

        // Test address 0x1300 - in A and B, B should win
        do_load_and_check(1, 0x1300, 4);

        // Test address 0x1450 - in all three, C should win
        do_load_and_check(2, 0x1450, 4);

        // Test address 0x1700 - in A and B, B should win
        do_load_and_check(1, 0x1700, 4);

        // Test address 0x1900 - only in A
        do_load_and_check(0, 0x1900, 4);
    }
};

// Test bench for partial overlaps
class PartialOverlapsTestBench : public RouterTestBenchSimple
{
public:
    PartialOverlapsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup partially overlapping regions:
        // Region A: 0x1000-0x17FF (priority -10)
        // Region B: 0x1400-0x1BFF (priority -5) - higher priority
        setup_target(0, 0x1000, 0x800, true, -10); // Lower priority
        setup_target(1, 0x1400, 0x800, true, -5);  // Higher priority

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test non-overlapping part of A (0x1000-0x13FF)
        do_load_and_check(0, 0x1200, 4);

        // Test overlapping part (0x1400-0x17FF) - B should win
        do_load_and_check(1, 0x1500, 4);

        // Test non-overlapping part of B (0x1800-0x1BFF)
        do_load_and_check(1, 0x1A00, 4);

        // Test boundaries
        do_load_and_check(0, 0x13FF, 1); // Last byte of A-only region
        do_load_and_check(1, 0x1400, 1); // First byte of overlap (B wins)
        do_load_and_check(1, 0x17FF, 1); // Last byte of overlap (B wins)
        do_load_and_check(1, 0x1800, 1); // First byte of B-only region
    }
};

// Test bench for chain overlaps
class ChainOverlapsTestBench : public RouterTestBenchSimple
{
public:
    ChainOverlapsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup chain overlapping regions:
        // Region A: 0x1000-0x17FF (priority -20)
        // Region B: 0x1600-0x1DFF (priority -10) - overlaps A
        // Region C: 0x1C00-0x23FF (priority -5)  - overlaps B, higher priority
        setup_target(0, 0x1000, 0x800, true, -20); // Lowest priority
        setup_target(1, 0x1600, 0x800, true, -10); // Medium priority
        setup_target(2, 0x1C00, 0x800, true, -5);  // Highest priority

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test A-only region (0x1000-0x15FF)
        do_load_and_check(0, 0x1300, 4);

        // Test A-B overlap (0x1600-0x17FF) - B should win
        do_load_and_check(1, 0x1700, 4);

        // Test B-only region (0x1800-0x1BFF)
        do_load_and_check(1, 0x1A00, 4);

        // Test B-C overlap (0x1C00-0x1DFF) - C should win
        do_load_and_check(2, 0x1D00, 4);

        // Test C-only region (0x1E00-0x23FF)
        do_load_and_check(2, 0x2000, 4);
    }
};

// Test bench for complex nested overlaps
class ComplexNestedOverlapsTestBench : public RouterTestBenchSimple
{
public:
    ComplexNestedOverlapsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup complex nested regions:
        // Outer:  0x1000-0x1FFF (priority -50 - lowest)
        // Middle: 0x1200-0x1DFF (priority -25)
        // Inner1: 0x1400-0x15FF (priority -10)
        // Inner2: 0x1600-0x17FF (priority -5)  - highest priority
        // Inner3: 0x1800-0x19FF (priority -15)
        setup_target(0, 0x1000, 0x1000, true, -50); // Outer - lowest priority
        setup_target(1, 0x1200, 0xC00, true, -25);  // Middle
        setup_target(2, 0x1400, 0x200, true, -10);  // Inner1
        setup_target(3, 0x1600, 0x200, true, -5);   // Inner2 - highest priority
        setup_target(4, 0x1800, 0x200, true, -15);  // Inner3

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test outer-only region
        do_load_and_check(0, 0x1100, 4);

        // Test middle region (not covered by inner)
        do_load_and_check(1, 0x1300, 4);

        // Test inner1 region
        do_load_and_check(2, 0x1500, 4);

        // Test inner2 region (highest priority)
        do_load_and_check(3, 0x1700, 4);

        // Test inner3 region
        do_load_and_check(4, 0x1900, 4);

        // Test back to middle region
        do_load_and_check(1, 0x1B00, 4);

        // Test back to outer region
        do_load_and_check(0, 0x1E00, 4);
    }
};

// Test bench for same priority conflicts
class SamePriorityConflictsTestBench : public RouterTestBenchSimple
{
public:
    SamePriorityConflictsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup regions with same priority - behavior should be deterministic
        // Region A: 0x1000-0x17FF (priority -10)
        // Region B: 0x1400-0x1BFF (priority -10) - same priority
        setup_target(0, 0x1000, 0x800, true, -10);
        setup_target(1, 0x1400, 0x800, true, -10);

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // The behavior should be deterministic - first added should win
        // or implementation-defined consistent behavior

        // Test non-overlapping regions
        do_load_and_check(0, 0x1200, 4); // A-only region
        do_load_and_check(1, 0x1A00, 4); // B-only region

        // Test overlapping region - should consistently route to one target
        // We don't specify which one wins, but it should be consistent
        int expected_target = -1; // Will be determined by first access

        // Make first access to determine which target wins
        do_load_and_check(-1, 0x1500, 4, false, tlm::TLM_OK_RESPONSE);
        expected_target = get_last_target_txn().id;

        // Subsequent accesses should go to the same target
        do_load_and_check(expected_target, 0x1600, 4);
        do_load_and_check(expected_target, 0x1700, 4);
    }
};

// Test bench for boundary conditions
class BoundaryConditionsTestBench : public RouterTestBenchSimple
{
public:
    BoundaryConditionsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup regions with exact boundary conditions
        // Region A: 0x1000-0x1FFF (priority -10)
        // Region B: 0x2000-0x2FFF (priority -10) - adjacent, no overlap
        // Region C: 0x1FFF-0x2000 (priority -5)  - single byte overlap with both
        setup_target(0, 0x1000, 0x1000, true, -10);
        setup_target(1, 0x2000, 0x1000, true, -10);
        setup_target(2, 0x1FFF, 0x2, true, -5); // Higher priority, covers boundary

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Test regions
        do_load_and_check(0, 0x1500, 4); // A region
        do_load_and_check(1, 0x2500, 4); // B region

        // Test boundary bytes - C should win due to higher priority
        do_load_and_check(2, 0x1FFF, 1); // Last byte of A, first byte of C
        do_load_and_check(2, 0x2000, 1); // First byte of B, second byte of C

        // Test adjacent bytes
        do_load_and_check(0, 0x1FFE, 1); // Just before boundary in A
        do_load_and_check(1, 0x2001, 1); // Just after boundary in B
    }
};

// Test bench for many overlapping regions
class ManyOverlappingRegionsTestBench : public RouterTestBenchSimple
{
public:
    ManyOverlappingRegionsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        const int NUM_REGIONS = 10;
        const uint64_t BASE_ADDR = 0x10000;
        const uint64_t REGION_SIZE = 0x1000;
        const uint64_t OVERLAP_OFFSET = 0x800; // 50% overlap

        // Create many overlapping regions with decreasing priority
        // (lower index = higher priority)
        for (int i = 0; i < NUM_REGIONS; i++) {
            uint64_t addr = BASE_ADDR + (i * OVERLAP_OFFSET);
            int priority = -(NUM_REGIONS - i); // Higher index = lower priority
            setup_target(i, addr, REGION_SIZE, true, priority);
        }

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        const int NUM_REGIONS = 10;
        const uint64_t BASE_ADDR = 0x10000;
        const uint64_t REGION_SIZE = 0x1000;
        const uint64_t OVERLAP_OFFSET = 0x800; // 50% overlap

        // Test that highest priority region wins in heavily overlapped area
        uint64_t heavily_overlapped_addr = BASE_ADDR + (NUM_REGIONS - 1) * OVERLAP_OFFSET;
        do_load_and_check(NUM_REGIONS - 1, heavily_overlapped_addr, 4);

        // Test regions with less overlap
        for (int i = 0; i < NUM_REGIONS; i++) {
            uint64_t test_addr = BASE_ADDR + (i * OVERLAP_OFFSET) + REGION_SIZE - 0x100;
            // Find expected target (highest priority that covers this address)
            int expected_target = i;
            for (int j = i + 1; j < NUM_REGIONS; j++) {
                uint64_t region_start = BASE_ADDR + (j * OVERLAP_OFFSET);
                if (test_addr >= region_start && test_addr < region_start + REGION_SIZE) {
                    expected_target = j; // Higher priority (lower priority value)
                }
            }
            do_load_and_check(expected_target, test_addr, 4);
        }
    }
};

// Test bench for DMI with complex overlaps
class DmiWithComplexOverlapsTestBench : public RouterTestBenchSimple
{
public:
    DmiWithComplexOverlapsTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup overlapping regions for DMI testing
        // Region A: 0x5000-0x5FFF (priority -10)
        // Region B: 0x5800-0x67FF (priority -5) - higher priority, overlaps A
        setup_target(0, 0x5000, 0x1000, true, -10);
        setup_target(1, 0x5800, 0x1000, true, -5);

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // DMI request in A-only region should return A's full boundaries
        do_good_dmi_request_and_check(0x5400, 0x5000, 0x5FFF, 0);

        // DMI request in overlap region should return B's full boundaries
        // (B wins due to higher priority)
        do_good_dmi_request_and_check(0x5A00, 0x5800, 0x67FF, 1);

        // DMI request in B-only region should return B's full boundaries
        do_good_dmi_request_and_check(0x6400, 0x5800, 0x67FF, 1);
    }
};

// Test bench for cache invalidation after splitting
class CacheInvalidationAfterSplittingTestBench : public RouterTestBenchSimple
{
public:
    CacheInvalidationAfterSplittingTestBench(const sc_core::sc_module_name& name): RouterTestBenchSimple(name) {}

    void before_end_of_elaboration() override
    {
        // Setup initial region and overlapping region during elaboration
        // (Note: The original test tried to add targets dynamically, which violates our architecture)
        setup_target(0, 0x8000, 0x1000, true, -10);
        setup_target(1, 0x8400, 0x800, true, -5); // Higher priority, overlaps target 0

        // Call parent to process the targets
        RouterTestBenchSimple::before_end_of_elaboration();
    }

    void test_bench_body() override
    {
        // Access overlapping part - should go to target 1 (higher priority)
        do_load_and_check(1, 0x8500, 4);

        // Access non-overlapping part of original region
        do_load_and_check(0, 0x8200, 4);

        // Access overlapping part multiple times to test cache consistency
        do_load_and_check(1, 0x8600, 4);
        do_load_and_check(1, 0x8700, 4);
    }
};

// Test case: Three overlapping regions with different priorities
TEST_BENCH(ThreeWayOverlapTestBench, ThreeWayOverlap)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Partial overlaps (regions that don't fully contain each other)
TEST_BENCH(PartialOverlapsTestBench, PartialOverlaps)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Chain overlaps (A overlaps B, B overlaps C, but A doesn't overlap C)
TEST_BENCH(ChainOverlapsTestBench, ChainOverlaps)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Complex nested overlaps
TEST_BENCH(ComplexNestedOverlapsTestBench, ComplexNestedOverlaps)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Same priority conflicts (should be deterministic)
TEST_BENCH(SamePriorityConflictsTestBench, SamePriorityConflicts)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Boundary conditions and edge cases
TEST_BENCH(BoundaryConditionsTestBench, BoundaryConditions)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Large number of overlapping regions (stress test)
TEST_BENCH(ManyOverlappingRegionsTestBench, ManyOverlappingRegions)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: DMI with complex overlaps
TEST_BENCH(DmiWithComplexOverlapsTestBench, DmiWithComplexOverlaps)
{
    // Test logic is in test_bench_body() method of the TestBench class
}

// Test case: Cache behavior with region splitting
TEST_BENCH(CacheInvalidationAfterSplittingTestBench, CacheInvalidationAfterSplitting)
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
        "ThreeWayOverlapTestBench",        "PartialOverlapsTestBench",        "ChainOverlapsTestBench",
        "ComplexNestedOverlapsTestBench",  "SamePriorityConflictsTestBench",  "BoundaryConditionsTestBench",
        "ManyOverlappingRegionsTestBench", "DmiWithComplexOverlapsTestBench", "CacheInvalidationAfterSplittingTestBench"
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

    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::WARNING)
                                        .msgTypeFieldWidth(50));

    // Run all Google Tests. The TEST_BENCH macro will handle SystemC simulation
    // setup and teardown automatically for each test.
    int result = RUN_ALL_TESTS();

    return result;
}
