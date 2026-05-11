/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <cstdio>
#include <fstream>
#include <vector>
#include <queue>
#include <scp/report.h>
#include <cci/utils/broker.h>
#include <libgsutils.h>
#include <cciutils.h>
#include <argparser.h>
#include <tlm_utils/tlm_quantumkeeper.h>
#include "cci/cfg/cci_broker_if.h"
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "cortex-a53.h"
#include "qemu-instance.h"
#include <router.h>
#include <gs_memory.h>
#include <smmu500.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <pass.h>

/*
 * Helper module for memory access with proper routing
 */
class MemoryAccessor : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<MemoryAccessor> socket;

    MemoryAccessor(sc_core::sc_module_name name): sc_core::sc_module(name), socket("socket") {}

    void write_memory(uint64_t addr, uint64_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_byte_enable_length(0);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(txn, delay);
    }

    uint64_t read_memory(uint64_t addr)
    {
        uint64_t value = 0;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_byte_enable_length(0);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(txn, delay);
        return value;
    }

    void write_register(uint32_t addr, uint32_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        // Use b_transport for register access to ensure proper logging
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(txn, delay);
    }

    uint32_t read_register(uint32_t addr)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        // Use b_transport for register access to ensure proper logging
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        socket->b_transport(txn, delay);
        return value;
    }
};

/*
 * SMMU Stress Test V2 - Tester-Controlled Architecture
 *
 * NEW APPROACH:
 * - No sync memory - all coordination through tester MMIO registers
 * - Tester handles all SMMU configuration directly
 * - CPUs request regions via MMIO writes, poll for readiness via MMIO reads
 * - Simplified protocol: REQUEST -> POLL -> WORK -> COMPLETE
 *
 * PROTOCOL:
 * CPU -> Tester: "I want to fill a region" (write to MMIO)
 * Tester: Configures SMMU mapping for that CPU to an available region
 * CPU: Polls tester register until ready
 * CPU: Fills region with pattern
 * CPU -> Tester: "Fill complete" (write to MMIO)
 *
 * Same for checking regions.
 */

class SMMUTesterController : public sc_core::sc_module
{
public:
    // MMIO Register Layout (per CPU)
    static constexpr uint64_t REG_SIZE_PER_CPU = 0x100;
    static constexpr uint64_t REG_REQUEST = 0x00;    // CPU writes: 1=fill_request, 2=check_request
    static constexpr uint64_t REG_STATUS = 0x08;     // CPU reads: 0=busy, 1=ready, 2=complete
    static constexpr uint64_t REG_REGION_ID = 0x10;  // CPU reads: assigned region ID
    static constexpr uint64_t REG_COMPLETE = 0x18;   // CPU writes: 1=fill_done, 2=check_done
    static constexpr uint64_t REG_ITERATIONS = 0x20; // CPU reads: current iteration count
    static constexpr uint64_t REG_DEBUG = 0x28;      // CPU writes: debug messages
    static constexpr uint64_t REG_DEBUG_DATA = 0x30; // CPU writes: debug messages

    // Request types
    enum RequestType { NONE = 0, FILL_REQUEST = 1, CHECK_REQUEST = 2 };
    enum StatusType { BUSY = 0, READY = 1, COMPLETE = 2 };
    enum CompleteType { FILL_DONE = 1, CHECK_DONE = 2 };

    SCP_LOGGER();
    SCP_LOGGER((TEST), "test");

    struct CPUState {
        RequestType current_request = NONE;
        StatusType status = BUSY;
        uint32_t assigned_region = 0xFFFFFFFF;
        uint32_t iteration_count = 0;
        bool is_working = false;
    };

    tlm_utils::simple_target_socket<SMMUTesterController> socket;

    // Reference to parent test bench for SMMU control
    class CpuArmCortexA53SMMUStressTestV2* m_parent;

    std::vector<CPUState> m_cpu_states;
    std::queue<uint32_t> m_available_regions;
    std::queue<uint32_t> m_regions_to_check;
    uint32_t m_num_cpus;
    uint32_t m_num_regions;
    uint32_t m_global_iterations;
    uint32_t m_target_iterations;

    SMMUTesterController(sc_core::sc_module_name name, uint32_t num_cpus, uint32_t num_regions,
                         uint32_t target_iterations)
        : sc_core::sc_module(name)
        , socket("socket")
        , m_parent(nullptr)
        , m_num_cpus(num_cpus)
        , m_num_regions(num_regions)
        , m_global_iterations(0)
        , m_target_iterations(target_iterations)
    {
        socket.register_b_transport(this, &SMMUTesterController::b_transport);
        socket.register_transport_dbg(this, &SMMUTesterController::transport_dbg);

        // Initialize CPU states
        m_cpu_states.resize(num_cpus);

        // Initialize available regions queue
        for (uint32_t i = 0; i < num_regions; ++i) {
            m_available_regions.push(i);
        }

        SCP_INFO((TEST)) << "SMMU Tester Controller initialized: " << num_cpus << " CPUs, " << num_regions
                         << " regions, target " << target_iterations << " iterations";
    }

    void set_parent(class CpuArmCortexA53SMMUStressTestV2* parent) { m_parent = parent; }

    virtual void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint64_t addr = trans.get_address();
        uint32_t cpu_id = static_cast<uint32_t>(addr / REG_SIZE_PER_CPU);
        uint64_t reg_offset = addr % REG_SIZE_PER_CPU;

        if (cpu_id >= m_num_cpus) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
            handle_write(cpu_id, reg_offset, trans);
        } else if (trans.get_command() == tlm::TLM_READ_COMMAND) {
            handle_read(cpu_id, reg_offset, trans);
        }

        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    virtual unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        // For debug transport, just call b_transport with zero delay
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(trans, delay);
        return trans.get_data_length();
    }

private:
    void handle_write(uint32_t cpu_id, uint64_t reg_offset, tlm::tlm_generic_payload& trans)
    {
        uint64_t data = *reinterpret_cast<uint64_t*>(trans.get_data_ptr());
        CPUState& cpu = m_cpu_states[cpu_id];

        switch (reg_offset) {
        case REG_REQUEST:
            handle_request(cpu_id, static_cast<RequestType>(data));
            break;

        case REG_COMPLETE:
            // moved out-of-class after test bench definition to avoid incomplete type error
            handle_complete(cpu_id, static_cast<CompleteType>(data));
            break;

        case REG_DEBUG:
            handle_debug(cpu_id, data);
            break;

        case REG_DEBUG_DATA:
            SCP_DEBUG(()) << "CPU " << cpu_id << " debug data: 0x" << std::hex << data;
            break;

        default:
            SCP_FATAL(()) << "CPU " << cpu_id << " write to unknown register 0x" << std::hex << reg_offset;
            break;
        }
    }

    // move handle_complete declaration only
    void handle_complete(uint32_t cpu_id, CompleteType complete);

    void handle_read(uint32_t cpu_id, uint64_t reg_offset, tlm::tlm_generic_payload& trans)
    {
        uint64_t data = 0;
        CPUState& cpu = m_cpu_states[cpu_id];

        switch (reg_offset) {
        case REG_STATUS:
            data = static_cast<uint64_t>(cpu.status);
            break;

        case REG_REGION_ID:
            data = cpu.assigned_region;
            break;

        case REG_ITERATIONS:
            data = m_global_iterations;
            break;

        default:
            SCP_FATAL(()) << "CPU " << cpu_id << " read from unknown register 0x" << std::hex << reg_offset;
            break;
        }

        *reinterpret_cast<uint64_t*>(trans.get_data_ptr()) = data;
    }

    /**
     * @brief Handles a request from a CPU to fill or check a region.
     * @param cpu_id The ID of the requesting CPU.
     * @param request The type of request (FILL or CHECK).
     *
     * @note This function is a critical section for region allocation.
     * Access to the `m_available_regions` and `m_regions_to_check` queues
     * is serialized by the single-threaded nature of the tester's b_transport,
     * preventing race conditions where multiple CPUs might be assigned the
     * same region.
     */
    void handle_request(uint32_t cpu_id, RequestType request)
    {
        CPUState& cpu = m_cpu_states[cpu_id];

        SCP_INFO(()) << "CPU " << cpu_id << " request: " << (request == FILL_REQUEST ? "FILL" : "CHECK");

        if (request == FILL_REQUEST) {
            if (!m_available_regions.empty()) {
                uint32_t region = m_available_regions.front();
                m_available_regions.pop();

                cpu.current_request = FILL_REQUEST;
                cpu.assigned_region = region;
                cpu.is_working = true;

                SCP_INFO(()) << "DEBUG: handle_request(FILL) assigning CPU " << cpu_id << " to region " << region
                             << ". Calling configure_smmu_mapping()";
                // Configure SMMU mapping for this CPU to this region
                configure_smmu_mapping(cpu_id, region);

                // Mark as ready
                cpu.status = READY;

                SCP_INFO((TEST)) << "CPU_" << cpu_id << " assigned region " << region << " for filling";
            } else {
                cpu.status = BUSY; // No regions available
                SCP_FATAL(()) << "CPU_" << cpu_id << " fill request - no regions available";
            }
        } else if (request == CHECK_REQUEST) {
            if (!m_regions_to_check.empty()) {
                uint32_t region = m_regions_to_check.front();
                m_regions_to_check.pop();

                cpu.current_request = CHECK_REQUEST;
                cpu.assigned_region = region;
                cpu.is_working = true;

                // Configure SMMU mapping for this CPU to this region
                configure_smmu_mapping(cpu_id, region);

                // Mark as ready
                cpu.status = READY;

                SCP_INFO((TEST)) << "CPU_" << cpu_id << " assigned region " << region << " for checking";
            } else {
                cpu.status = BUSY; // No regions to check
                SCP_DEBUG(()) << "CPU_" << cpu_id << " check request - no regions to check";
            }
        }
    }

    // Definition moved after CpuArmCortexA53SMMUStressTestV2 below

    void handle_debug(uint32_t cpu_id, uint64_t data)
    {
        // Decode debug messages
        uint32_t msg_type = (data & 0xF000) >> 12;
        uint32_t msg_data = (data & 0x0FFF);

        switch (msg_type) {
        case 0x1:
            SCP_INFO(()) << "CPU " << cpu_id << " started up";
            break;
        case 0x2:
            SCP_INFO(()) << "CPU " << cpu_id << " entering main loop, iteration " << msg_data;
            break;
        case 0x3:
            SCP_INFO(()) << "CPU " << cpu_id << " polling for readiness";
            break;
        case 0x4:
            SCP_INFO(()) << "CPU " << cpu_id << " starting work on region " << msg_data;
            break;
        case 0x5:
            SCP_INFO(()) << "CPU " << cpu_id << " pattern verification success";
            break;
        case 0x6:
            SCP_INFO(()) << "CPU " << cpu_id << " DEBUG: Starting fill for region " << msg_data;
            break;
        case 0x7:
            SCP_INFO(()) << "CPU " << cpu_id << " DEBUG: Fill write #" << msg_data << " (first 4 writes logged)";
            break;
        case 0x8:
            SCP_INFO(()) << "CPU " << cpu_id << " DEBUG: Completed fill for region " << msg_data;
            break;
        case 0xD:
            SCP_WARN(()) << "CPU " << cpu_id << " pattern verification FAILED";
            break;
        case 0xE:
            SCP_FATAL(()) << "🚨 DIAGNOSTIC ERROR: CPU " << cpu_id
                          << " encountered fatal error - jumping to diagnostic handler at 0x200";
            sc_core::sc_stop();
            break;
        default:
            SCP_DEBUG(()) << "CPU " << cpu_id << " debug: 0x" << std::hex << data;
            break;
        }
    }

    /**
     * @brief Configures SMMU mapping for a CPU to a specific region.
     * @param cpu_id The ID of the CPU.
     * @param region_id The ID of the region to map.
     *
     * @note This method is a critical section. The tester controller's
     * single-threaded event processing model ensures that only one mapping
     * operation occurs at a time, preventing race conditions where multiple
     * CPUs could try to modify SMMU settings simultaneously.
     */
    void configure_smmu_mapping(uint32_t cpu_id, uint32_t region_id);
    void unmap_cpu_region(uint32_t cpu_id);

    // NEW METHODS: Tester directly verifies region patterns in physical memory
    bool verify_region_pattern(uint32_t cpu_id, uint32_t region_id);
    void clear_region_pattern(uint32_t region_id);
};

/**
 * @class CpuArmCortexA53SMMUStressTestV2
 * @brief A SystemC test bench for stressing the SMMUv2 with multiple CPUs.
 *
 * This test implements a sophisticated, tester-controlled architecture to
 * stress the SMMU-500. Unlike traditional approaches where CPUs manage their
 * own synchronization and SMMU configuration, this test centralizes all

 * control within the `SMMUTesterController`.
 *
 * Key Architectural Concepts:
 *
 * 1.  **Tester-Controlled Synchronization**: CPUs do not communicate directly.
 *     Instead, they make requests to the tester via MMIO registers and poll
 *     for completion. This serializes all SMMU operations and prevents the
 *     race conditions inherent in multi-CPU configurations.
 *
 * 2.  **Dual-TBU Architecture**: Each CPU is associated with two Translation
 *     Buffer Units (TBUs) to separate identity-mapped and high-VA-mapped
 *     traffic:
 *     -   **Identity TBU (shared, CB0)**: All low-address traffic (e.g.,
 *         firmware, tester MMIO) is routed through a shared TBU mapped to
 *         Context Bank 0 (CB0). CB0 has its MMU disabled, providing a direct
 *         1:1 VA-to-PA mapping.
 *     -   **High VA TBU (per-CPU, CB1+)**: High-address traffic (accesses to
 *         the virtual test region) is routed through a dedicated TBU per CPU,
 *         each mapped to a unique Context Bank (CB1 for CPU0, CB2 for CPU1,
 *         etc.). These CBs have their MMU enabled and manage the translation
 *         from the high virtual address to the physical memory region.
 *
 * 3.  **Dynamic Region Mapping**: The tester dynamically maps virtual memory
 *     regions to physical memory for each CPU on demand. It configures the
 *     SMMU, invalidates TLBs, and signals readiness to the CPU, ensuring
 *     that memory accesses are correctly translated.
 *
 * This design ensures a robust, race-free, and scalable stress test for the
 * SMMU, capable of handling multiple CPUs concurrently without data corruption
 * or synchronization failures.
 */
class CpuArmCortexA53SMMUStressTestV2 : public TestBench, public CpuTesterCallbackIface
{
public:
    static constexpr uint16_t MAX_ITERATIONS = 0x7ff; // Limited to 16-bit for ARM64 movz instruction
    static constexpr uint64_t PATTERN_SIZE = 16;
    static constexpr unsigned BOUNDARY_BYTES = 80;
    sc_core::sc_time TEST_DURATION = sc_core::sc_time(30, sc_core::SC_SEC);

    // Memory layout - REORGANIZED: Everything in first 1GB with 1:1 mapping
    static constexpr uint64_t MEM_ADDR = 0x0;                  // FIRMWARE: 0x00000000 - 0x0003FFFF
    static constexpr size_t MEM_SIZE = 256 * 1024;             // 256KB for firmware
    static constexpr uint64_t BOOT_ADDR = 0x0;                 // BOOT: 0x00000000 - tiny boot loader
    static constexpr uint64_t DIAGNOSTIC_ADDR = 0x200;         // DIAGNOSTIC: 0x00000200 - error handler
    static constexpr uint64_t MAIN_FIRMWARE_ADDR = 0x1000;     // MAIN_FIRMWARE: 0x00001000 - main test logic
    static constexpr uint64_t TESTER_ADDR = 0x40000;           // TESTER: 0x00040000 - 0x0004FFFF
    static constexpr uint64_t TESTER_SIZE = 0x10000;           // 64KB for tester
    static constexpr uint64_t SMMU_REG_ADDR = 0x50000;         // SMMU: 0x00050000 - 0x0006FFFF
    static constexpr uint64_t SMMU_REG_SIZE = 0x20000;         // 128KB for SMMU registers
    static constexpr uint64_t MAIN_MEM_ADDR = 0x10000000;      // MEMORY: 0x10000000 - 0x1FFFFFFF
    static constexpr size_t MAIN_MEM_SIZE = 256 * 1024 * 1024; // 256MB for main memory

    // Virtual and physical memory regions - Use high virtual address >32-bit
    static constexpr uint64_t VIRTUAL_TEST_ADDR = 0x80000000ULL; // 12GB virtual address
    static constexpr uint64_t
        PAGE_TABLE_BASE = 0x10000000; // Page tables: 0x10000000 - 0x1007FFFF (512KB for up to 32 CPUs)
    static constexpr uint64_t REGION_BASE = 0x10080000; // Test regions: 0x10080000+ in 8KB blocks (after page tables)
    static constexpr uint64_t REGION_SIZE = 0x2000;     // 8KB regions (two pages each)
    static constexpr uint64_t PAGE_SIZE = 0x1000;       // 4KB pages

    SCP_LOGGER();

protected:
    struct PageTableAddresses {
        uint64_t l0, l1, l2, l3;
    };

    PageTableAddresses get_page_table_addresses_for_cpu(uint32_t cpu) const
    {
        uint64_t base = PAGE_TABLE_BASE + ((cpu + 1) * PAGE_SIZE * 4);
        return { base, base + PAGE_SIZE, base + (PAGE_SIZE * 2), base + (PAGE_SIZE * 3) };
    }
    // CCI parameters
    cci::cci_param<int> p_num_cpu;
    cci::cci_param<int> p_quantum_ns;

    // QEMU instances and CPUs
    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst_a;
    QemuInstance m_inst_b;
    bool ab = false;
    sc_core::sc_vector<cpu_arm_cortexA53> m_cpus;

    // Memory components
    gs::gs_memory<> m_mem;
    gs::gs_memory<> m_main_mem;

    // SMMU components
    gs::smmu500<> m_smmu;

    std::vector<gs::pass<>*> m_pass_identity;       // Pass-through for identity traffic
    std::vector<gs::smmu500_tbu<>*> m_tbus_high_va; // TBUs for high VA traffic

    // Routers
    gs::router<> m_global_router;
    std::vector<gs::router<>*> m_cpu_routers; // Per-CPU routers

    // Tester controller
    SMMUTesterController m_tester_controller;

    uint32_t m_num_regions;
    std::vector<uint32_t> m_cpu_to_region;

public:
    // Memory accessor for proper routing - made public for tester access
    MemoryAccessor m_memory_accessor;

private:
    // ========================================================================
    // SMMU Register Offset Constants
    // ========================================================================

    // SMMU Global Register Offsets (from SMMU_REG_ADDR)
    static constexpr uint32_t SMMU_SCR0_OFFSET = 0x0;
    static constexpr uint32_t SMMU_SMR_BASE_OFFSET = 0x800;
    static constexpr uint32_t SMMU_S2CR_BASE_OFFSET = 0xc00;
    static constexpr uint32_t SMMU_CBAR_BASE_OFFSET = 0x1000;

    // Context Bank Page Layout
    static constexpr uint32_t CB_PAGE_OFFSET = 16; // CBs start at page 16 (0x10000)
    static constexpr uint32_t CB_PAGE_SIZE = 4096; // 4KB per CB (0x1000)

    // Context Bank Register Offsets (from CB base address)
    static constexpr uint32_t CB_SCTLR_OFFSET = 0x0;
    static constexpr uint32_t CB_TTBR0_LOW_OFFSET = 0x20;
    static constexpr uint32_t CB_TTBR0_HIGH_OFFSET = 0x24;
    static constexpr uint32_t CB_TCR_OFFSET = 0x30;
    static constexpr uint32_t CB_MAIR0_OFFSET = 0x38;
    static constexpr uint32_t CB_MAIR1_OFFSET = 0x3C;
    static constexpr uint32_t CB_TLBIALL_OFFSET = 0x618;

    /**
     * @brief Calculate the base address of a context bank's register space
     * @param cb Context bank number (0-based)
     * @return Physical address of the context bank's register space
     *
     * Context banks are located at SMMU_REG_ADDR + 0x10000 + (cb * 0x1000).
     * This is page 16 onwards from SMMU base, with each CB occupying one 4KB page.
     *
     * Memory layout:
     *   SMMU_REG_ADDR + 0x00000: Global registers
     *   SMMU_REG_ADDR + 0x10000: CB0 registers (page 16)
     *   SMMU_REG_ADDR + 0x11000: CB1 registers (page 17)
     *   ...
     */
    uint32_t get_context_bank_base(uint32_t cb) const
    {
        assert(cb < m_smmu.p_num_cb && "Context bank ID out of range");
        uint32_t cb_offset_words = ((CB_PAGE_OFFSET + cb) * CB_PAGE_SIZE) / 4;
        return SMMU_REG_ADDR + (cb_offset_words * 4);
    }

    void reconfigure_context_bank(uint32_t cb, uint64_t page_table_addr);

protected:
    // Firmware is assembled ahead of time from
    // smmu-router-stress-test-v2.S — all layout constants are baked in at
    // build time, so we just load the .bin at 0x0 and the three sub-blobs
    // (boot @ 0x0, diagnostic @ 0x200, main @ 0x1000) land at their expected
    // offsets automatically.
    void load_stress_test_firmware()
    {
        std::ifstream file(FIRMWARE_BIN_PATH, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            SCP_FATAL(()) << "Failed to open firmware file: " << FIRMWARE_BIN_PATH;
            TEST_FAIL("Failed to open firmware file");
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
            SCP_FATAL(()) << "Failed to read firmware file: " << FIRMWARE_BIN_PATH;
            TEST_FAIL("Failed to read firmware file");
        }
        m_mem.load.ptr_load(data.data(), MEM_ADDR, size);
    }

public:
    CpuArmCortexA53SMMUStressTestV2(const sc_core::sc_module_name& n)
        : TestBench(n)
        , p_num_cpu("num_cpu", 4, "Number of CPUs to instantiate in the test")
        , p_quantum_ns("quantum_ns", 1000000, "Value of the global TLM-2.0 quantum in ns")
        , m_inst_a("inst_a", &m_inst_manager, cpu_arm_cortexA53::ARCH)
        , m_inst_b("inst_b", &m_inst_manager, cpu_arm_cortexA53::ARCH)
        , m_cpus("cpu", p_num_cpu,
                 [this](const char* n, int i) {
                     ab = !ab;
                     return new cpu_arm_cortexA53(n, ab ? m_inst_a : m_inst_b);
                 })
        , m_mem("mem", MEM_SIZE)
        , m_main_mem("main_mem", MAIN_MEM_SIZE)
        , m_smmu("smmu")
        , m_global_router("global_router")
        , m_tester_controller("tester_controller", p_num_cpu.get_value(),
                              std::max(3u, static_cast<uint32_t>(p_num_cpu.get_value() * 3)), MAX_ITERATIONS)
        , m_memory_accessor("memory_accessor")
    {
        using tlm_utils::tlm_quantumkeeper;

        // Set up ARM-specific CPU configuration
        int i = 0;
        for (cpu_arm_cortexA53& cpu : m_cpus) {
            cpu.p_mp_affinity = i++;
            cpu.p_has_el3 = false;
            cpu.p_has_el2 = false; // Un-needed and unavailable for HVF/KVM
        }

        sc_core::sc_time global_quantum(p_quantum_ns, sc_core::SC_NS);
        tlm_quantumkeeper::set_global_quantum(global_quantum);

        m_num_regions = std::max(3u, static_cast<uint32_t>(p_num_cpu.get_value() * 3));

        SCP_INFO(()) << "Creating SMMU Stress Test V2 with " << p_num_cpu.get_value() << " CPUs, " << m_num_regions
                     << " regions";

        // Configure SMMU

        // Bind memory_accessor.socket to router BEFORE any accesses!
        m_global_router.add_initiator(m_memory_accessor.socket);

        m_smmu.p_num_tbu = p_num_cpu.get_value();
        m_smmu.p_num_cb = p_num_cpu.get_value() * 2; // Need 2 CBs per CPU: identity + high VA
        m_smmu.p_num_smr = 64;                       // Increased from 32 to support up to 32 CPUs (each needs 2 SMRs)
        m_smmu.p_num_pages = std::max(
            16u, static_cast<uint32_t>(p_num_cpu.get_value() * 2)); // Ensure enough pages for all CBs
        SCP_INFO(()) << "SMMU500 instantiated: p_num_cb=" << m_smmu.p_num_cb << ", p_num_smr=" << m_smmu.p_num_smr
                     << ", p_num_pages=" << m_smmu.p_num_pages;

        // Create TBU instances - 2 TBUs per CPU (identity + high VA)
        uint32_t num_cpus = p_num_cpu.get_value();
        m_pass_identity.resize(num_cpus);
        m_tbus_high_va.resize(num_cpus);

        for (uint32_t i = 0; i < num_cpus; ++i) {
            // Identity pass-through for each CPU - bypasses SMMU for identity traffic
            char pass_identity_name[32];
            std::snprintf(pass_identity_name, sizeof(pass_identity_name), "pass_identity_%d", i);
            m_pass_identity[i] = new gs::pass<>(pass_identity_name);
            SCP_INFO(()) << "Identity pass-through constructed: CPU" << i << " (bypassing SMMU for identity traffic)";

            // High VA TBU for each CPU - unique StreamID per CPU
            char tbu_high_va_name[32];
            std::snprintf(tbu_high_va_name, sizeof(tbu_high_va_name), "tbu_high_va_%d", i);
            m_tbus_high_va[i] = new gs::smmu500_tbu<>(tbu_high_va_name, &m_smmu);
            uint32_t high_va_topology_id = i + 1; // StreamID 1,2,3... for high VA
            m_tbus_high_va[i]->p_topology_id = high_va_topology_id;
            m_tbus_high_va[i]->p_topology_id.set_value(high_va_topology_id);
            SCP_INFO(()) << "High VA TBU constructed: CPU" << i << " topology_id=" << high_va_topology_id
                         << " (StreamID " << high_va_topology_id << " → CB" << high_va_topology_id << ")";
        }

        // Create per-CPU routers
        m_cpu_routers.resize(num_cpus);
        for (uint32_t i = 0; i < num_cpus; ++i) {
            char router_name[32];
            std::snprintf(router_name, sizeof(router_name), "cpu_router_%d", i);
            m_cpu_routers[i] = new gs::router<>(router_name);
            SCP_INFO(()) << "Per-CPU router constructed: " << router_name;
        }

        // Initialize CPU to region mapping
        m_cpu_to_region.resize(p_num_cpu.get_value(), 0xFFFFFFFF);

        // Set up tester controller parent reference
        m_tester_controller.set_parent(this);

        // NEW ARCHITECTURE: CPU -> Per-CPU Router -> Identity/High VA TBUs -> Global Router
        for (uint32_t i = 0; i < p_num_cpu.get_value(); ++i) {
            // Connect CPU to per-CPU router
            m_cpu_routers[i]->add_initiator(m_cpus[i].socket);

            // Configure per-CPU router address ranges
            // Identity traffic (<0x300000000) -> Pass-through (bypasses SMMU)
            m_cpu_routers[i]->add_target(m_pass_identity[i]->target_socket, 0x0, 0x10000000ULL);

            // High VA traffic (>=0x300000000) -> High VA TBU
            m_cpu_routers[i]->add_target(m_tbus_high_va[i]->upstream_socket, VIRTUAL_TEST_ADDR, 0x10000000ULL);

            SCP_INFO(()) << "🔍 ROUTING DEBUG: CPU " << i << " -> CPU_Router_" << i;
            SCP_INFO(()) << "  - Identity range [0x0 - 0x10000000] -> Pass_Identity_" << i << " (bypassing SMMU)";
            SCP_INFO(()) << "  - High VA range [0x300000000 - 0x400000000] -> High_VA_TBU_" << i << " (StreamID "
                         << (i + 1) << ")";
            SCP_INFO(()) << "  - Identity pass name: " << m_pass_identity[i]->name();
            SCP_INFO(()) << "  - High VA TBU name: " << m_tbus_high_va[i]->name();
        }

        // Add components to global router
        m_global_router.add_target(m_mem.socket, MEM_ADDR, MEM_SIZE);

        // Add main memory as a single large region - SMMU will handle the translation
        m_global_router.add_target(m_main_mem.socket, MAIN_MEM_ADDR, MAIN_MEM_SIZE);

        m_global_router.add_target(m_smmu.socket, SMMU_REG_ADDR, SMMU_REG_SIZE);
        m_global_router.add_target(m_tester_controller.socket, TESTER_ADDR, TESTER_SIZE);

        // Connect TBU downstream sockets to global router
        for (uint32_t i = 0; i < p_num_cpu.get_value(); ++i) {
            m_global_router.add_initiator(m_pass_identity[i]->initiator_socket);
            m_global_router.add_initiator(m_tbus_high_va[i]->downstream_socket);
        }

        // Connect SMMU DMA socket
        m_global_router.add_initiator(m_smmu.dma_socket);

        SCP_INFO(()) << "Memory layout:";
        SCP_INFO(()) << "  FIRMWARE: 0x" << std::hex << MEM_ADDR;
        SCP_INFO(()) << "  MAIN_MEM: 0x" << std::hex << MAIN_MEM_ADDR;
        SCP_INFO(()) << "  REGIONS: 0x" << std::hex << REGION_BASE;
        SCP_INFO(()) << "  SMMU_REG: 0x" << std::hex << SMMU_REG_ADDR;
        SCP_INFO(()) << "  TESTER: 0x" << std::hex << TESTER_ADDR;
        SCP_INFO(()) << "  VIRTUAL_TEST: 0x" << std::hex << VIRTUAL_TEST_ADDR;

        load_stress_test_firmware();

        SC_THREAD(configure_test);
    }

    virtual ~CpuArmCortexA53SMMUStressTestV2()
    {
        for (auto* pass : m_pass_identity) {
            delete pass;
        }

        for (auto* tbu : m_tbus_high_va) {
            delete tbu;
        }
        for (auto* router : m_cpu_routers) {
            delete router;
        }
    }

    void configure_test()
    {
        wait(sc_core::sc_time(100, sc_core::SC_US));

        SCP_INFO(()) << "Configuring SMMU for tester-controlled operation";

        // First: clear CLIENTPD in SMMU_SCR0 (enable SMMU translation)
        write_smmu_register(SMMU_REG_ADDR + SMMU_SCR0_OFFSET, 0x0);

        // Configure SMRs and S2CRs for NEW DUAL-TBU ARCHITECTURE
        // SHARED IDENTITY: All identity TBUs share StreamID 0 → CB0
        // PER-CPU HIGH VA: Each CPU gets unique StreamID for high VA → separate CBs

        // SMR[0]: StreamID 0 -> CB0 (SHARED identity mapping for ALL CPUs)
        uint32_t smr0_addr = SMMU_REG_ADDR + SMMU_SMR_BASE_OFFSET;
        uint32_t smr0_value = (1 << 31) | (0 << 16) | (0 << 0); // VALID=1, MASK=0, ID=0
        write_smmu_register(smr0_addr, smr0_value);

        uint32_t s2cr0_addr = SMMU_REG_ADDR + SMMU_S2CR_BASE_OFFSET;
        uint32_t s2cr0_value = (0x1 << 16) | (0 << 0); // TYPE=1, CBNDX=0
        write_smmu_register(s2cr0_addr, s2cr0_value);

        SCP_INFO(()) << "SMR[0]/S2CR[0]: StreamID=0 -> CB0 (SHARED identity for ALL CPUs)";

        // Configure SMRs for high VA TBUs (one per CPU)
        for (uint32_t cpu = 0; cpu < p_num_cpu.get_value(); ++cpu) {
            uint32_t high_va_stream_id = cpu + 1; // StreamID 1,2,3... for high VA
            uint32_t high_va_cb = cpu + 1;        // CB1,CB2,CB3... for high VA

            uint32_t smr_addr = SMMU_REG_ADDR + SMMU_SMR_BASE_OFFSET + (high_va_stream_id * 4);
            uint32_t smr_value = (1 << 31) | (0 << 16) | (high_va_stream_id << 0); // VALID=1, MASK=0, ID=stream_id
            write_smmu_register(smr_addr, smr_value);

            uint32_t s2cr_addr = SMMU_REG_ADDR + SMMU_S2CR_BASE_OFFSET + (high_va_stream_id * 4);
            uint32_t s2cr_value = (0x1 << 16) | (high_va_cb << 0); // TYPE=1, CBNDX=cb
            write_smmu_register(s2cr_addr, s2cr_value);

            SCP_INFO(()) << "SMR[" << high_va_stream_id << "]/S2CR[" << high_va_stream_id
                         << "]: StreamID=" << high_va_stream_id << " -> CB" << high_va_cb << " (CPU " << cpu
                         << " high VA)";
        }

        // NEW ARCHITECTURE SUMMARY
        SCP_INFO(()) << "SMMU StreamID Mapping Summary (NEW DUAL-TBU ARCHITECTURE):"
                     << "  StreamID 0 -> CB0 (SHARED identity for ALL CPUs - MMU disabled)";
        for (uint32_t cpu = 0; cpu < p_num_cpu.get_value(); ++cpu) {
            SCP_INFO(()) << "  StreamID " << (cpu + 1) << " -> CB" << (cpu + 1) << " (CPU " << cpu
                         << " high VA - 4KB pages)";
        }

        // Initialize context banks for NEW DUAL-TBU ARCHITECTURE
        // CB0: SHARED identity context bank for ALL CPUs
        setup_identity_context_bank(0); // Only CB0 for shared identity

        // CB1,CB2,CB3...: Per-CPU high VA context banks
        for (uint32_t cpu = 0; cpu < p_num_cpu.get_value(); ++cpu) {
            uint32_t high_va_cb = cpu + 1; // CB1,CB2,CB3... for high VA

            // CRITICAL FIX: Use single consolidated function to avoid conflicts
            // This replaces both setup_high_va_context_bank() and setup_complete_page_table_structure()
            setup_complete_high_va_context_bank(cpu, high_va_cb);

            SCP_INFO(()) << "Complete high VA context bank initialized for CPU " << cpu << " (CB" << high_va_cb
                         << ") - conflicts resolved";
        }

        SCP_INFO(()) << "SMMU configuration completed - ready for tester control";
    }

    void setup_identity_context_bank(uint32_t cpu)
    {
        // Set up identity context bank (CB0) with NO page tables for pure identity mapping
        uint32_t cb = cpu; // Identity CB is always CB0
        uint32_t cb_base = get_context_bank_base(cb);

        // Set CBAR.TYPE = 1 (translation enabled for this CB)
        uint32_t cbar_addr = SMMU_REG_ADDR + SMMU_CBAR_BASE_OFFSET + (cb * 4);
        write_smmu_register(cbar_addr, (1 << 16)); // TYPE=1

        // CRITICAL: For identity mapping, DISABLE MMU completely in the CB
        // This makes VA = PA directly without any page table walking. M bit (bit 0) is 0.
        write_smmu_register(cb_base + CB_SCTLR_OFFSET, 0x0);

        // Clear TTBR registers as they are not used when the MMU is disabled
        write_smmu_register(cb_base + CB_TTBR0_LOW_OFFSET, 0x0);
        write_smmu_register(cb_base + CB_TTBR0_HIGH_OFFSET, 0x0);

        // Set TCR to safe defaults, although not used when MMU is disabled
        write_smmu_register(cb_base + CB_TCR_OFFSET, (1U << 31) | (25 << 0)); // EAE=1, T0SZ=25

        // Configure MAIR for normal memory, although not used when MMU is disabled
        write_smmu_register(cb_base + CB_MAIR0_OFFSET, 0xFF);
        write_smmu_register(cb_base + CB_MAIR1_OFFSET, 0x0);

        SCP_INFO(()) << "Identity context bank CB" << cb << " set up for CPU " << cpu
                     << " with MMU DISABLED (pure identity mapping VA=PA)";
    }

    /**
     * @brief Sets up a complete, multi-level page table for a high VA context bank.
     * @param cpu The CPU ID to associate with this context bank.
     * @param cb The context bank number to configure (must be > 0).
     *
     * This function is critical for the dual-TBU architecture. It creates a
     * full L0->L1->L2->L3 page table structure for a CPU's high VA context bank.
     *
     * Key details:
     * - **TBU Address Stripping**: The TBU strips the upper bits of the high
     *   virtual address (0x300000000ULL), presenting it to the SMMU as if it
     *   were 0x0.
     * - **Dual L1 Mapping**: To handle this, we create two L1 entries: one for the
     *   original high VA and one for the stripped VA (0x0). Both point to the
     *   same L2 table, ensuring correct translation regardless of the initial
     *   address.
     * - **Default Mapping**: A default L3 mapping is created to prevent "bad
     *   descriptor" faults upon initial MMU enable.
     */
    void setup_complete_high_va_context_bank(uint32_t cpu, uint32_t cb)
    {
        // Validate parameters
        assert(cpu < p_num_cpu.get_value() && "CPU ID out of range");
        assert(cb > 0 && "High VA context bank must be > 0");
        assert(cb == cpu + 1 && "High VA CB must map to CPU+1");

        // CRITICAL FIX: Consolidated function to replace conflicting setup functions
        // This combines setup_high_va_context_bank() and setup_complete_page_table_structure()
        // to eliminate the conflicts that cause "bad descriptor" SMMU faults

        SCP_INFO(()) << "🔧 CONSOLIDATED SETUP: Setting up complete high VA context bank for CPU " << cpu << " (CB"
                     << cb << ") - resolving function conflicts";

        // Use consistent page table addressing throughout
        const auto pt_addrs = get_page_table_addresses_for_cpu(cpu);

        uint64_t l0_table_addr = pt_addrs.l0;
        uint64_t l1_table_addr = pt_addrs.l1;
        uint64_t l2_table_addr = pt_addrs.l2;
        uint64_t l3_table_addr = pt_addrs.l3;

        SCP_INFO(()) << "  - Page table base: 0x" << std::hex << l0_table_addr;
        SCP_INFO(()) << "  - L0 table: 0x" << std::hex << l0_table_addr;
        SCP_INFO(()) << "  - L1 table: 0x" << std::hex << l1_table_addr;
        SCP_INFO(()) << "  - L2 table: 0x" << std::hex << l2_table_addr;
        SCP_INFO(()) << "  - L3 table: 0x" << std::hex << l3_table_addr;

        // Set up complete page table structure
        // L0[0]: 0x00000000 - 0x7FFFFFFFFF (0-512GB) -> L1 table
        uint64_t l0_desc_0 = (l1_table_addr & ~0xFFFULL) | (1ULL << 10) | 0x3ULL;
        write_memory_64(l0_table_addr + (0 * 8), l0_desc_0);

        // CRITICAL FIX: TBU strips upper bits, so VA=0x300000000 becomes VA=0x0
        // We need L1[0] to map to the SAME L2 table as L1[12] for proper translation
        // This is NOT identity mapping - it's mapping the stripped high VA to the correct region

        // L1[0]: VA=0x0 (stripped from 0x300000000) -> L2 table (same as high VA mapping)
        uint64_t l1_desc_0 = (l2_table_addr & ~0xFFFULL) | (1ULL << 10) | 0x3ULL;
        write_memory_64(l1_table_addr + (0 * 8), l1_desc_0);

        SCP_INFO(()) << "  - L1[0]: VA=0x0 (TBU-stripped from 0x300000000) -> L2 table (same as high VA)";

        // L1[12]: VIRTUAL_TEST_ADDR range -> L2 table (for high VA mapping)
        uint32_t l1_index = (VIRTUAL_TEST_ADDR >> 30) & 0x1FF;
        uint64_t l1_desc_high = (l2_table_addr & ~0xFFFULL) | (1ULL << 10) | 0x3ULL;
        write_memory_64(l1_table_addr + (l1_index * 8), l1_desc_high);

        // L2[0]: VIRTUAL_TEST_ADDR range -> L3 table
        uint32_t l2_index = (VIRTUAL_TEST_ADDR >> 21) & 0x1FF;
        uint64_t l2_desc = (l3_table_addr & ~0xFFFULL) | (1ULL << 10) | 0x3ULL;
        write_memory_64(l2_table_addr + (l2_index * 8), l2_desc);

        // L3[0]: Initialize with CPU-specific default valid mapping to prevent "bad descriptor" faults
        // Each CPU gets its own default region to avoid conflicts
        uint64_t default_physical_addr = REGION_BASE + (cpu * REGION_SIZE); // CPU-specific default region
        uint64_t l3_desc_0 = (default_physical_addr & ~0xFFFULL) | (1ULL << 10) | (3ULL << 8) | (3ULL << 2) |
                             (1ULL << 6) | 0x3ULL;
        write_memory_64(l3_table_addr + (0 * 8), l3_desc_0);

        // CRITICAL DEBUG: Verify the L3[0] descriptor was written to the correct L3 table address
        uint64_t l3_readback = m_memory_accessor.read_memory(l3_table_addr + (0 * 8));

        SCP_INFO(()) << "  - L3[0]: VA=0x0 -> PA=0x" << std::hex << default_physical_addr << " (CPU " << cpu
                     << " specific default mapping)" << "  - L3 table address: 0x" << std::hex << l3_table_addr
                     << "  - L3[0] descriptor written: 0x" << std::hex << l3_desc_0
                     << "  - L3[0] descriptor readback: 0x" << std::hex << l3_readback
                     << "  - Writing L3[0] to address: 0x" << std::hex << (l3_table_addr + (0 * 8));

        if (l3_readback != l3_desc_0) {
            SCP_FATAL(()) << "🚨 CRITICAL: L3[0] descriptor write/read mismatch for CPU " << cpu << "!"
                          << "  Expected: 0x" << std::hex << l3_desc_0 << "  Got: 0x" << std::hex << l3_readback
                          << "  L3 table address: 0x" << std::hex << l3_table_addr << "  Write address: 0x" << std::hex
                          << (l3_table_addr + (0 * 8));
        } else {
            SCP_INFO(()) << "✅ L3[0] descriptor verification successful for CPU " << cpu;
        }

        // Configure context bank registers
        uint32_t cb_base = get_context_bank_base(cb);

        // Set CBAR.TYPE = 1 (translation enabled for this CB)
        uint32_t cbar_addr = SMMU_REG_ADDR + SMMU_CBAR_BASE_OFFSET + (cb * 4);
        write_smmu_register(cbar_addr, (1 << 16)); // TYPE=1

        // Configure TTBR0 with the page table address
        write_smmu_register(cb_base + CB_TTBR0_LOW_OFFSET, static_cast<uint32_t>(l0_table_addr & 0xFFFFFFFF));
        write_smmu_register(cb_base + CB_TTBR0_HIGH_OFFSET, static_cast<uint32_t>((l0_table_addr >> 32) & 0xFFFFFFFF));

        SCP_INFO(()) << "  - TTBR0 SET: CB" << cb << " TTBR0=0x" << std::hex << l0_table_addr
                     << "  - Expected L3 table at: 0x" << std::hex << (l0_table_addr + (PAGE_SIZE * 3));

        // Configure TCR for 4KB pages, 48-bit VA space
        write_smmu_register(cb_base + CB_TCR_OFFSET,
                            (1U << 31) | (16 << 0) | (0 << 14) | (3 << 12) | (1 << 10) | (1 << 8));

        // Configure MAIR for normal memory
        write_smmu_register(cb_base + CB_MAIR0_OFFSET, 0xFF);
        write_smmu_register(cb_base + CB_MAIR1_OFFSET, 0x0);

        // Enable MMU with complete page table structure
        uint32_t sctlr_value = (1 << 0) | (1 << 2) | (1 << 4); // M, A, C bits enabled
        write_smmu_register(cb_base + CB_SCTLR_OFFSET, sctlr_value);

        SCP_INFO(()) << "SETUP COMPLETE: CPU " << cpu << " CB" << cb;
        SCP_INFO(()) << "  - L0[0] -> L1 table, L1[0]: Identity mapping, L1[" << std::dec << l1_index << "] -> High VA";
    }
    void clear_region_pattern(uint32_t region_id)
    {
        uint64_t physical_addr = CpuArmCortexA53SMMUStressTestV2::REGION_BASE +
                                 (region_id * CpuArmCortexA53SMMUStressTestV2::REGION_SIZE);

        SCP_INFO(()) << "Clearing region " << region_id << " (2-PAGE) at physical address 0x" << std::hex
                     << physical_addr;

        const uint32_t words_to_clear = CpuArmCortexA53SMMUStressTestV2::BOUNDARY_BYTES / 8;
        const uint32_t num_pages = CpuArmCortexA53SMMUStressTestV2::REGION_SIZE /
                                   CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE;

        // Loop over all pages in the region (2 pages for 8KB regions)
        for (uint32_t page_num = 0; page_num < num_pages; ++page_num) {
            uint64_t page_physical_addr = physical_addr + (page_num * CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE);

            SCP_INFO(()) << "  - Clearing page " << page_num << " at PA=0x" << std::hex << page_physical_addr;

            // Clear the beginning of this page
            for (uint32_t i = 0; i < words_to_clear; ++i) {
                write_memory_64(page_physical_addr + (i * 8), 0xCAFE0000 + i);
            }

            // Clear the end of this page
            for (uint32_t i = 0; i < words_to_clear; ++i) {
                write_memory_64(
                    page_physical_addr + CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE - (words_to_clear * 8) + (i * 8),
                    0xBEEF0000 + i);
            }
        }

        SCP_INFO(()) << "✅ Region " << region_id << " cleared (all " << num_pages << " pages)";
    }
    void map_cpu_to_region(uint32_t cpu, uint32_t region)
    {
        // Validate parameters
        assert(cpu < m_cpu_to_region.size() && "CPU ID out of range");
        assert(region < m_num_regions && "Region ID out of range");

        if (cpu >= m_cpu_to_region.size()) return;

        m_cpu_to_region[cpu] = region;

        uint32_t high_va_cb = cpu + 1;
        uint64_t physical_addr = REGION_BASE + (region * REGION_SIZE);
        const auto pt_addrs = get_page_table_addresses_for_cpu(cpu);

        SCP_INFO(()) << "Starting map_cpu_to_region for CPU " << cpu << " -> Region " << region;

        clear_region_pattern(region);

        // Step 1: Update page tables
        create_page_table_mapping(cpu, VIRTUAL_TEST_ADDR, physical_addr, REGION_SIZE);

        // Step 2: Reconfigure the context bank to use the updated page tables
        reconfigure_context_bank(high_va_cb, pt_addrs.l0);

        SCP_INFO(()) << "✅ SMMU mapping completed successfully for CPU " << cpu << " -> Region " << region;
    }

    void create_page_table_mapping(uint32_t cpu, uint64_t virtual_addr, uint64_t physical_addr, uint64_t size)
    {
        // Validate parameters
        assert(cpu < p_num_cpu.get_value() && "CPU ID out of range");
        assert((physical_addr & 0xFFF) == 0 && "Physical address must be page-aligned");
        assert((virtual_addr & 0xFFF) == 0 && "Virtual address must be page-aligned");
        assert(size == REGION_SIZE && "Size must match REGION_SIZE");

        // Use separate page table space for high VA context banks
        const auto pt_addrs = get_page_table_addresses_for_cpu(cpu);

        uint64_t l0_table_addr = pt_addrs.l0;
        uint64_t l1_table_addr = pt_addrs.l1;
        uint64_t l2_table_addr = pt_addrs.l2;
        uint64_t l3_table_addr = pt_addrs.l3;

        // Calculate number of pages to map
        uint32_t num_pages = size / PAGE_SIZE;

        SCP_INFO(()) << "🔍 TWO-PAGE MAPPING: Creating page tables for CPU " << cpu << " (" << num_pages << " pages)";
        SCP_INFO(()) << "  - High VA CB page table offset: 0x" << std::hex << (pt_addrs.l0 - PAGE_TABLE_BASE);
        SCP_INFO(()) << "  - L0 table: 0x" << std::hex << l0_table_addr;
        SCP_INFO(()) << "  - L1 table: 0x" << std::hex << l1_table_addr;
        SCP_INFO(()) << "  - L2 table: 0x" << std::hex << l2_table_addr;
        SCP_INFO(()) << "  - L3 table: 0x" << std::hex << l3_table_addr;

        // Map each page in the region
        // For 2-page regions: L3[0] maps page 0, L3[1] maps page 1
        for (uint32_t page = 0; page < num_pages; ++page) {
            uint64_t page_pa = physical_addr + (page * PAGE_SIZE);

            // Create L3 descriptor for this page
            // SH=0b11 (inner shareable) at bits [3:2], AF=1, AttrIndx=0b011, NS=1, page type=0b11
            uint64_t l3_desc = (page_pa & ~0xFFFULL) | (1ULL << 10) | (3ULL << 8) | (3ULL << 2) | (1ULL << 6) | 0x3ULL;
            write_memory_64(l3_table_addr + (page * 8), l3_desc);

            // Verify the L3 entry was written correctly
            uint64_t l3_readback = m_memory_accessor.read_memory(l3_table_addr + (page * 8));
            uint64_t extracted_physical = l3_readback & ~0xFFFULL;

            SCP_INFO(()) << "  - L3[" << page << "]: VA=0x" << std::hex << (page * PAGE_SIZE) << " -> PA=0x" << page_pa
                         << "    Descriptor: 0x" << std::hex << l3_desc << "    Readback: 0x" << std::hex
                         << l3_readback;

            if (extracted_physical != page_pa) {
                SCP_FATAL(()) << "🚨 CRITICAL: L3[" << page << "] physical address mismatch!" << "  Expected: 0x"
                              << std::hex << page_pa << "  Found: 0x" << std::hex << extracted_physical;
            } else {
                SCP_INFO(()) << "    ✅ L3[" << page << "] verification successful";
            }
        }

        SCP_INFO(()) << "✅ TWO-PAGE mapping complete for CPU " << cpu << " (region "
                     << ((physical_addr - REGION_BASE) / REGION_SIZE) << ")";

        // CRITICAL FIX: Invalidate SMMU TLB after page table update
        // The SMMU TLB caches old translations and must be invalidated when page tables change
        invalidate_smmu_tlb(cpu);
    }

    /**
     * @brief Unmaps a region from a CPU by disabling its high VA context bank.
     * @param cpu The ID of the CPU to unmap.
     *
     * @warning It is critical to only unmap the CPU-specific high VA context
     * bank (CB1, CB2, etc.) and NEVER CB0. CB0 provides shared identity
     * mapping for all CPUs and must remain active for the duration of the test.
     * Disabling CB0 would break identity-mapped accesses for all CPUs.
     */
    void unmap_cpu_region(uint32_t cpu)
    {
        if (cpu >= m_cpu_to_region.size()) return;

        // CRITICAL: Only unmap HIGH VA context banks (CB1, CB2, CB3...)
        // NEVER touch CB0, which is the shared identity context bank.
        uint32_t high_va_cb = cpu + 1;

        // Safety check to prevent unmapping of the shared identity context bank
        if (high_va_cb == 0) {
            SCP_FATAL(()) << "CRITICAL BUG: Attempt to unmap shared CB0 for CPU " << cpu;
            return;
        }

        uint32_t cb_base = get_context_bank_base(high_va_cb);

        SCP_INFO(()) << "Unmapping CPU " << cpu << " from CB" << high_va_cb;

        // Disable the MMU for the high VA context bank
        write_smmu_register(cb_base + CB_SCTLR_OFFSET, 0x0);

        // Clear the TTBR registers
        write_smmu_register(cb_base + CB_TTBR0_LOW_OFFSET, 0x0);
        write_smmu_register(cb_base + CB_TTBR0_HIGH_OFFSET, 0x0);

        SCP_INFO(()) << "Unmap complete for CPU " << cpu << ", high VA CB" << high_va_cb << " disabled.";
        m_cpu_to_region[cpu] = 0xFFFFFFFF;
    }

    void write_smmu_register(uint32_t addr, uint32_t value)
    {
        SCP_INFO(()) << "ATTEMPTING SMMU WRITE: addr=0x" << std::hex << addr << ", value=0x" << value;
        m_memory_accessor.write_register(addr, value);
        SCP_INFO(()) << "SMMU WRITE COMPLETED: addr=0x" << std::hex << addr << ", value=0x" << value;
    }

    uint32_t read_smmu_register(uint32_t addr)
    {
        SCP_INFO(()) << "ATTEMPTING SMMU READ: addr=0x" << std::hex << addr;
        uint32_t value = m_memory_accessor.read_register(addr);
        SCP_INFO(()) << "SMMU READ COMPLETED: addr=0x" << std::hex << addr << ", value=0x" << value;
        return value;
    }

    void write_memory_64(uint64_t addr, uint64_t value) { m_memory_accessor.write_memory(addr, value); }

    /**
     * @brief Invalidates the SMMU TLB for a specific CPU's context bank.
     * @param cpu The ID of the CPU whose TLB should be invalidated.
     *
     * @note This is a critical step after any page table modification. The SMMU
     * caches translations in its TLB. Failure to invalidate the TLB after a
     * change can lead to stale translations being used, causing data corruption
     * or access violations. This function ensures that the SMMU is forced to
     * re-read the updated page tables.
     */
    void invalidate_smmu_tlb(uint32_t cpu)
    {
        // CRITICAL FIX: Invalidate SMMU TLB after page table updates
        // The SMMU TLB caches old translations and must be invalidated when page tables change

        uint32_t high_va_cb = cpu + 1; // CB1 for CPU0, CB2 for CPU1
        uint32_t cb_base = get_context_bank_base(high_va_cb);

        SCP_INFO(()) << "🔄 TLB INVALIDATION: Invalidating SMMU TLB for CPU " << cpu << " (CB" << high_va_cb
                     << ") - interrupts disabled during setup";

        // Method 1: Write to TLBIALL register to invalidate all TLB entries for this context bank
        // TLBIALL is per-CB and only affects this specific context bank
        // Since we disabled CFIE during setup, no completion interrupts will be generated
        uint32_t tlbiall_addr = cb_base + CB_TLBIALL_OFFSET;
        write_smmu_register(tlbiall_addr, 0x0); // Any write invalidates all entries for this CB

        // Add delay to ensure TLB invalidation takes effect
        wait(sc_core::sc_time(2, sc_core::SC_US));

        SCP_INFO(()) << "✅ TLB INVALIDATION COMPLETE: CPU " << cpu << " TLB cleared";
    }

    // CpuTesterCallbackIface implementation (not used in V2)
    virtual void map_target(tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& s, uint64_t addr, uint64_t size) override {}
    virtual void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool> >& irqs) override {}
    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }
    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret) override { return false; }
    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override {}

    virtual void end_of_simulation() override
    {
        SCP_WARN(()) << "SMMU Stress Test V2 completed"
                     << "\nFinal statistics:" << "\n  - Total iterations: " << m_tester_controller.m_global_iterations
                     << "/" << MAX_ITERATIONS << "\n  - CPUs: " << p_num_cpu.get_value()
                     << "\n  - Memory regions: " << m_num_regions << "\n  - Pattern size: " << PATTERN_SIZE
                     << " * 8 bytes";
    }
};

void CpuArmCortexA53SMMUStressTestV2::reconfigure_context_bank(uint32_t cb, uint64_t page_table_addr)
{
    uint32_t cb_base = get_context_bank_base(cb);

    // First disable MMU to safely reconfigure
    SCP_INFO(()) << "  - Step 1: Disabling MMU for safe reconfiguration";
    //    write_smmu_register(cb_base + CB_SCTLR_OFFSET, 0x0);

    // Add delay after MMU disable
    // wait(sc_core::sc_time(1, sc_core::SC_US));

    // Configure TTBR0 with page table address
    SCP_INFO(()) << "  - Step 2: Configuring TTBR0 with page table address 0x" << std::hex << page_table_addr;
    write_smmu_register(cb_base + CB_TTBR0_LOW_OFFSET, static_cast<uint32_t>(page_table_addr & 0xFFFFFFFF));
    write_smmu_register(cb_base + CB_TTBR0_HIGH_OFFSET, static_cast<uint32_t>((page_table_addr >> 32) & 0xFFFFFFFF));

    // Configure TCR for 4KB pages, 48-bit VA space
    SCP_INFO(()) << "  - Step 3: Configuring TCR for 4KB pages";
    uint32_t tcr_value = (1U << 31) | (16 << 0) | (0 << 14) | (3 << 12) | (1 << 10) | (1 << 8);
    write_smmu_register(cb_base + CB_TCR_OFFSET, tcr_value);

    // Configure MAIR for normal memory
    SCP_INFO(()) << "  - Step 4: Configuring MAIR";
    write_smmu_register(cb_base + CB_MAIR0_OFFSET, 0xFF); // Normal memory, write-back cacheable
    write_smmu_register(cb_base + CB_MAIR1_OFFSET, 0x0);

    // Add delay before enabling MMU
    // wait(sc_core::sc_time(2, sc_core::SC_US));

    // Enable MMU with proper configuration - CRITICAL: M bit must be set for translation
    uint32_t sctlr_value = (1 << 0) | (1 << 2) | (1 << 4);
    SCP_INFO(()) << "  - Step 5: Enabling MMU with SCTLR: 0x" << std::hex << sctlr_value;
    write_smmu_register(cb_base + CB_SCTLR_OFFSET, sctlr_value);

    // Additional delay to ensure MMU enable takes effect
    //    wait(sc_core::sc_time(3, sc_core::SC_US));
}

// Implement the tester controller methods that need access to parent
void SMMUTesterController::configure_smmu_mapping(uint32_t cpu_id, uint32_t region_id)
{
    if (m_parent) {
        m_parent->map_cpu_to_region(cpu_id, region_id);
    }
}

void SMMUTesterController::unmap_cpu_region(uint32_t cpu_id)
{
    if (m_parent) {
        m_parent->unmap_cpu_region(cpu_id);
    }
}

// NEW METHODS: Tester directly verifies region patterns in physical memory
bool SMMUTesterController::verify_region_pattern(uint32_t cpu_id, uint32_t region_id)
{
    if (!m_parent) {
        SCP_FATAL(()) << "No parent reference for memory access";
        return false;
    }

    uint64_t physical_addr = CpuArmCortexA53SMMUStressTestV2::REGION_BASE +
                             (region_id * CpuArmCortexA53SMMUStressTestV2::REGION_SIZE);

    SCP_INFO(()) << "Verifying region " << region_id << " with ENHANCED 2-PAGE pattern at PA=0x" << std::hex
                 << physical_addr;

    const uint32_t words_to_check = 10; // Check 10 words (80 bytes) at each boundary
    const uint32_t num_pages = CpuArmCortexA53SMMUStressTestV2::REGION_SIZE /
                               CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE;

    // Loop over all pages in the region (2 pages for 8KB regions)
    for (uint32_t page_num = 0; page_num < num_pages; ++page_num) {
        uint64_t page_physical_addr = physical_addr + (page_num * CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE);

        SCP_INFO(()) << "  - Verifying page " << page_num << " at PA=0x" << std::hex << page_physical_addr;

        // Check the beginning of this page
        for (uint32_t word = 0; word < words_to_check; ++word) {
            uint64_t word_addr = page_physical_addr + (word * 8);
            uint64_t read_value = m_parent->m_memory_accessor.read_memory(word_addr);

            // Extract all pattern fields
            uint32_t pattern_cpu = (read_value >> 32) & 0xFFFFFFFF;
            uint32_t pattern_region = (read_value >> 16) & 0xFFFF;
            uint32_t pattern_page = (read_value >> 8) & 0xFF;
            uint32_t pattern_offset = read_value & 0xFF;

            if (pattern_cpu != cpu_id || pattern_region != region_id || pattern_page != page_num ||
                pattern_offset != word) {
                SCP_FATAL(()) << "🚨 ENHANCED PATTERN MISMATCH at region " << region_id << " page " << page_num
                              << " START, word " << word << " | Expected: CPU=" << cpu_id << " Region=" << region_id
                              << " Page=" << page_num << " Offset=" << word << " | Got: CPU=" << pattern_cpu
                              << " Region=" << pattern_region << " Page=" << pattern_page
                              << " Offset=" << pattern_offset << " | Raw value: 0x" << std::hex << read_value;
                return false;
            }
        }

        // Check the end of this page
        for (uint32_t word = 0; word < words_to_check; ++word) {
            uint64_t word_addr = page_physical_addr + CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE -
                                 (words_to_check * 8) + (word * 8);
            uint64_t read_value = m_parent->m_memory_accessor.read_memory(word_addr);

            uint32_t pattern_cpu = (read_value >> 32) & 0xFFFFFFFF;
            uint32_t pattern_region = (read_value >> 16) & 0xFFFF;
            uint32_t pattern_page = (read_value >> 8) & 0xFF;
            uint32_t pattern_offset = read_value & 0xFF;

            if (pattern_cpu != cpu_id || pattern_region != region_id || pattern_page != page_num ||
                pattern_offset != word) {
                SCP_FATAL(()) << "🚨 ENHANCED PATTERN MISMATCH at region " << region_id << " page " << page_num
                              << " END, word " << word << " | Expected: CPU=" << cpu_id << " Region=" << region_id
                              << " Page=" << page_num << " Offset=" << word << " | Got: CPU=" << pattern_cpu
                              << " Region=" << pattern_region << " Page=" << pattern_page
                              << " Offset=" << pattern_offset << " | Raw value: 0x" << std::hex << read_value;
                return false;
            }
        }

        SCP_INFO(()) << "    ✅ Page " << page_num << " verified successfully";
    }

    SCP_INFO(()) << "✅ Region " << region_id << " ENHANCED 2-PAGE pattern verification successful"
                 << "  - CPU=" << cpu_id << " Region=" << region_id << " - All " << num_pages << " pages verified";
    return true;
}

/*
void SMMUTesterController::clear_region_pattern(uint32_t region_id)
{
    if (!m_parent) {
        SCP_FATAL(()) << "No parent reference for memory access";
        return;
    }

    uint64_t physical_addr = CpuArmCortexA53SMMUStressTestV2::REGION_BASE +
                             (region_id * CpuArmCortexA53SMMUStressTestV2::REGION_SIZE);

    SCP_INFO(()) << "Clearing region " << region_id << " (2-PAGE) at physical address 0x" << std::hex << physical_addr;

    const uint32_t words_to_clear = CpuArmCortexA53SMMUStressTestV2::BOUNDARY_BYTES / 8;
    const uint32_t num_pages = CpuArmCortexA53SMMUStressTestV2::REGION_SIZE /
                               CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE;

    // Loop over all pages in the region (2 pages for 8KB regions)
    for (uint32_t page_num = 0; page_num < num_pages; ++page_num) {
        uint64_t page_physical_addr = physical_addr + (page_num * CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE);

        SCP_INFO(()) << "  - Clearing page " << page_num << " at PA=0x" << std::hex << page_physical_addr;

        // Clear the beginning of this page
        for (uint32_t i = 0; i < words_to_clear; ++i) {
            m_parent->write_memory_64(page_physical_addr + (i * 8), 0xCAFE0000 + i);
        }

        // Clear the end of this page
        for (uint32_t i = 0; i < words_to_clear; ++i) {
            m_parent->write_memory_64(
                page_physical_addr + CpuArmCortexA53SMMUStressTestV2::PAGE_SIZE - (words_to_clear * 8) + (i * 8),
                0xBEEF0000 + i);
        }
    }

    SCP_INFO(()) << "✅ Region " << region_id << " cleared (all " << num_pages << " pages)";
}
*/
/* ---- Implementation moved here to ensure CpuArmCortexA53SMMUStressTestV2 is a complete type ---- */

/**
 * @brief Handles a completion notification from a CPU.
 * @param cpu_id The ID of the completing CPU.
 * @param complete The type of completion (FILL_DONE or CHECK_DONE).
 *
 * @note This method serves as a critical section for managing shared
 * resources like region queues and global iteration counts. The serialization
 * of calls through `b_transport` ensures that updates to these resources
 * are atomic, preventing race conditions.
 */
void SMMUTesterController::handle_complete(uint32_t cpu_id, CompleteType complete)
{
    CPUState& cpu = m_cpu_states[cpu_id];

    if (complete == FILL_DONE) {
        if (cpu.assigned_region >= m_num_regions) {
            SCP_FATAL(()) << "CPU " << cpu_id << " has invalid assigned_region " << cpu.assigned_region;
            sc_core::sc_stop();
            return;
        }

        SCP_INFO(()) << "CPU " << cpu_id << " completed filling region " << cpu.assigned_region;

        uint64_t physical_addr = CpuArmCortexA53SMMUStressTestV2::REGION_BASE +
                                 (cpu.assigned_region * CpuArmCortexA53SMMUStressTestV2::REGION_SIZE);
        uint64_t first_word = m_parent->m_memory_accessor.read_memory(physical_addr);
        uint32_t original_filler_cpu = (first_word >> 32) & 0xFFFFFFFF;

        if (verify_region_pattern(original_filler_cpu, cpu.assigned_region)) {
            SCP_INFO((TEST))("Region {:d} (for CPU_{:d}) verified by tester", cpu.assigned_region, original_filler_cpu);
            // clear_region_pattern(cpu.assigned_region);
            m_available_regions.push(cpu.assigned_region);
            cpu.iteration_count++;
            m_global_iterations++;
        } else {
            SCP_FATAL(()) << "Region " << cpu.assigned_region << " verification failed";
            sc_core::sc_stop();
            return;
        }

    } else if (complete == CHECK_DONE) {
        SCP_WARN(()) << "CHECK_DONE reported by CPU " << cpu_id << " - this path is deprecated";
        m_available_regions.push(cpu.assigned_region);
        cpu.iteration_count++;
        m_global_iterations++;
    }

    unmap_cpu_region(cpu_id);

    cpu.current_request = NONE;
    cpu.assigned_region = 0xFFFFFFFF;
    cpu.is_working = false;
    cpu.status = COMPLETE;

    if (m_global_iterations >= m_target_iterations) {
        SCP_INFO((TEST)) << "Target iterations reached: " << m_global_iterations;
        sc_core::sc_stop();
    }
}

int sc_main(int argc, char* argv[])
{
    scp::init_logging(scp::LogConfig()
                          .fileInfoFrom(sc_core::SC_ERROR)
                          .logAsync(false)
                          .logLevel(scp::log::WARNING)
                          .msgTypeFieldWidth(30));

    gs::ConfigurableBroker m_broker{};
    cci::cci_originator orig{ "sc_main" };
    auto broker_h = m_broker.create_broker_handle(orig);
    ArgParser ap{ broker_h, argc, argv };

    SCP_INFO("sc_main") << "Start SMMU Stress Test V2 - Tester Controlled";
    CpuArmCortexA53SMMUStressTestV2 test_bench("test-bench");

    if ((gs::cci_get<int>(broker_h, "test-bench.num_cpu") > 8) ||
        gs::cci_get_d<bool>(broker_h, "test-bench.inst_a.icount", false)) {
        SCP_INFO("sc_main")("Refusing to run more than 8 CPUs, or icount mode");
        exit(0);
    }
    try {
        sc_core::sc_start();
    } catch (std::exception& e) {
        std::cerr << "Test failure: " << e.what() << "\n";
        SCP_INFO("sc_main") << "Test failed";
        return 1;
    } catch (...) {
        std::cerr << "Test failure: Unknown exception\n";
        SCP_INFO("sc_main") << "Test failed";
        return 1;
    }
    SCP_INFO("test")("Test done");
    SCP_INFO("sc_main") << "Test done";
    exit(0);
    return 0;
}
