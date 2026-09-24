/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#include <cci/utils/broker.h>
#include <libgsutils.h>
#include <gs_memory.h>
#include <router.h>
#include <smmu500.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm-extensions/qemu-memtx-attrs.h>

#include "cortex-a53.h"
#include "qemu-instance.h"
#include "test/cpu.h"

namespace {

constexpr uint64_t FIRMWARE_ADDR = 0;
constexpr size_t FIRMWARE_SIZE = 256 * 1024;
constexpr size_t PAGE_SIZE = 0x1000;
constexpr uint64_t SECURE_IOVA = 0x100000000ULL;
constexpr uint64_t NON_SECURE_IOVA = 0x200000000ULL;
constexpr uint64_t IOVA_WINDOW_SIZE = NON_SECURE_IOVA - SECURE_IOVA + PAGE_SIZE;
constexpr uint64_t COMPLETE_ADDR = 0x81000000ULL;
constexpr size_t COMPLETE_SIZE = 0x1000;
constexpr uint64_t SECURE_PHYS_ADDR = 0x10000;
constexpr uint64_t NON_SECURE_PHYS_ADDR = SECURE_PHYS_ADDR + PAGE_SIZE;
constexpr uint64_t SECURE_L0_ADDR = 0x1000;
constexpr uint64_t NON_SECURE_L0_ADDR = SECURE_L0_ADDR + (4 * PAGE_SIZE);
constexpr uint64_t SECURE_PRIME_VALUE = 0x5345435f5052494dULL;
constexpr uint64_t SECURE_WRITE_VALUE = 0x534543555245ULL;
constexpr uint64_t NON_SECURE_PRIME_VALUE = 0x4e535f5052494d45ULL;
constexpr uint64_t NON_SECURE_WRITE_VALUE = 0x4e4f4e534543ULL;
constexpr uint64_t COMPLETE_VALUE = 0x434f4d504c455445ULL;
constexpr uint64_t WRONG_EXCEPTION_LEVEL_VALUE = 0x454c4641494cULL;
constexpr uint64_t SMMU_REG_ADDR = 0x90000000ULL;
constexpr size_t SMMU_REG_SIZE = 0x20000;

class security_observing_memory : public gs::gs_memory<>
{
public:
    std::vector<bool> data_write_security;
    std::vector<bool> data_dmi_security;
    std::vector<bool> page_table_security;

    security_observing_memory(const sc_core::sc_module_name& name, uint64_t size): gs::gs_memory<>(name, size) {}

    uint64_t read_u64(uint64_t address)
    {
        uint64_t value = 0;
        TEST_ASSERT(read(reinterpret_cast<uint8_t*>(&value), address, sizeof(value)));
        return value;
    }

protected:
    static bool is_data_address(uint64_t address)
    {
        return address == SECURE_PHYS_ADDR || address == NON_SECURE_PHYS_ADDR;
    }

    static bool is_page_table_address(uint64_t address)
    {
        return (address >= SECURE_L0_ADDR && address < SECURE_L0_ADDR + (4 * PAGE_SIZE)) ||
               (address >= NON_SECURE_L0_ADDR && address < NON_SECURE_L0_ADDR + (4 * PAGE_SIZE));
    }

    static bool transaction_is_secure(const tlm::tlm_generic_payload& txn)
    {
        const auto* secure = txn.get_extension<gs::QemuMemTxSecureTlmExtension>();
        TEST_ASSERT(secure != nullptr);
        return secure->value;
    }

    void b_transport(int id, tlm::tlm_generic_payload& txn, sc_core::sc_time& delay) override
    {
        if (txn.get_command() == tlm::TLM_WRITE_COMMAND && is_data_address(txn.get_address())) {
            data_write_security.push_back(transaction_is_secure(txn));
        }
        if (txn.get_command() == tlm::TLM_READ_COMMAND && is_page_table_address(txn.get_address())) {
            page_table_security.push_back(transaction_is_secure(txn));
        }
        gs::gs_memory<>::b_transport(id, txn, delay);
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi) override
    {
        if (is_data_address(txn.get_address())) {
            data_dmi_security.push_back(transaction_is_secure(txn));
        }
        return gs::gs_memory<>::get_direct_mem_ptr(id, txn, dmi);
    }
};

class completion_target : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<completion_target> socket;
    unsigned int writes = 0;
    uint64_t value = 0;

    completion_target(const sc_core::sc_module_name& name, sc_core::sc_event& completion_event)
        : sc_core::sc_module(name), socket("socket"), m_completion_event(completion_event)
    {
        socket.register_b_transport(this, &completion_target::b_transport);
    }

private:
    sc_core::sc_event& m_completion_event;

    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        TEST_ASSERT(txn.get_command() == tlm::TLM_WRITE_COMMAND);
        TEST_ASSERT(txn.get_data_length() == sizeof(value));
        TEST_ASSERT(writes == 0);
        std::memcpy(&value, txn.get_data_ptr(), sizeof(value));
        ++writes;
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
        m_completion_event.notify(sc_core::SC_ZERO_TIME);
    }
};

class CpuArmCortexA53SMMU500SecureAttrsTest : public TestBench
{
    cci::cci_param<int> p_num_cpu;
    QemuInstanceManager m_instance_manager;
    QemuInstance m_instance;
    cpu_arm_cortexA53 m_cpu;
    gs::router<> m_router;
    security_observing_memory m_memory;
    gs::smmu500<> m_smmu;
    gs::smmu500_tbu<> m_tbu;
    gs::async_event m_completion_guard;
    sc_core::sc_event m_completion_event;
    completion_target m_completion_target;

public:
    CpuArmCortexA53SMMU500SecureAttrsTest(const sc_core::sc_module_name& name)
        : TestBench(name)
        , p_num_cpu("num_cpu", 1, "This test requires one CPU")
        , m_instance("inst_a", &m_instance_manager, cpu_arm_cortexA53::ARCH)
        , m_cpu("cpu", m_instance)
        , m_router("router")
        , m_memory("memory", FIRMWARE_SIZE)
        , m_smmu("smmu")
        , m_tbu("tbu", &m_smmu)
        , m_completion_guard("completion_guard")
        , m_completion_target("completion_target", m_completion_event)
    {
        TEST_ASSERT(p_num_cpu == 1);

        m_completion_guard.async_attach_suspending();
        SC_METHOD(finish_test);
        sensitive << m_completion_event;
        dont_initialize();

        // AArch64 resets at secure EL3, then switches to Non-secure EL1. Both
        // QEMU address spaces converge on this one router and one TBU.
        m_cpu.p_has_el3 = true;
        m_cpu.p_has_el2 = false;
        m_cpu.p_has_secure_memory = true;
        m_memory.p_max_block_size = PAGE_SIZE;
        m_tbu.p_topology_id = 0;
        m_smmu.p_num_cb = 2;
        m_smmu.p_num_smr = 2;
        m_smmu.p_num_pages = 18;

        m_router.add_initiator(m_cpu.secure_mem);
        m_router.add_initiator(m_cpu.socket);
        m_router.add_target(m_memory.socket, FIRMWARE_ADDR, FIRMWARE_SIZE);
        // Keep the IOVA intact so bits [35:32] select a distinct stream ID
        // while the SMMU walks VA zero for each context bank.
        m_router.add_target(m_tbu.upstream_socket, SECURE_IOVA, IOVA_WINDOW_SIZE, false);
        m_router.add_target(m_completion_target.socket, COMPLETE_ADDR, COMPLETE_SIZE);
        m_router.add_target(m_smmu.socket, SMMU_REG_ADDR, SMMU_REG_SIZE);

        // Keep translated DMI responses out of the upstream router: routing a
        // TBU DMI lookup back through the router that initiated it would
        // re-enter its DMI lock. The CPU-side paths still share that router
        // and TBU before both translations reach distinct memory offsets.
        m_tbu.downstream_socket.bind(m_memory.socket);
        m_smmu.dma_socket.bind(m_memory.socket);

        load_firmware_binary(FIRMWARE_BIN_PATH, FIRMWARE_ADDR,
                             { SECURE_IOVA, NON_SECURE_IOVA, SECURE_PRIME_VALUE, SECURE_WRITE_VALUE,
                               NON_SECURE_PRIME_VALUE, NON_SECURE_WRITE_VALUE, COMPLETE_ADDR, COMPLETE_VALUE,
                               WRONG_EXCEPTION_LEVEL_VALUE });
    }

    void end_of_elaboration() override
    {
        // The shared TBU decodes each IOVA's CSID into a stream ID. Both
        // contexts walk VA zero and map to separate pages in one gs_memory.
        m_smmu.SMMU_SCR0 = 0;
        m_smmu.SMMU_NSCR0 = 0;
        configure_context_bank(0, 1, SECURE_L0_ADDR, SECURE_PHYS_ADDR);
        configure_context_bank(1, 2, NON_SECURE_L0_ADDR, NON_SECURE_PHYS_ADDR);
    }

    void end_of_simulation() override
    {
        TEST_ASSERT(m_completion_target.writes == 1);
        TEST_ASSERT(m_completion_target.value == COMPLETE_VALUE);
        TEST_ASSERT(m_memory.data_write_security.size() == 2);
        TEST_ASSERT(m_memory.data_write_security[0]);
        TEST_ASSERT(!m_memory.data_write_security[1]);
        TEST_ASSERT(std::find(m_memory.data_dmi_security.begin(), m_memory.data_dmi_security.end(), true) !=
                    m_memory.data_dmi_security.end());
        TEST_ASSERT(std::find(m_memory.data_dmi_security.begin(), m_memory.data_dmi_security.end(), false) !=
                    m_memory.data_dmi_security.end());
        TEST_ASSERT(std::find(m_memory.page_table_security.begin(), m_memory.page_table_security.end(), true) !=
                    m_memory.page_table_security.end());
        TEST_ASSERT(std::find(m_memory.page_table_security.begin(), m_memory.page_table_security.end(), false) !=
                    m_memory.page_table_security.end());
        TEST_ASSERT(m_memory.read_u64(SECURE_PHYS_ADDR) == SECURE_WRITE_VALUE);
        TEST_ASSERT(m_memory.read_u64(NON_SECURE_PHYS_ADDR) == NON_SECURE_WRITE_VALUE);
    }

private:
    void configure_context_bank(unsigned int cb, uint32_t stream_id, uint64_t l0_addr, uint64_t phys_addr)
    {
        constexpr uint64_t table_desc_flags = (1ULL << 10) | 0x3ULL;
        constexpr uint64_t page_desc_flags = (1ULL << 10) | (3ULL << 8) | (3ULL << 2) | (1ULL << 6) | 0x3ULL;
        const uint64_t l1_addr = l0_addr + PAGE_SIZE;
        const uint64_t l2_addr = l1_addr + PAGE_SIZE;
        const uint64_t l3_addr = l2_addr + PAGE_SIZE;
        // The TBU strips the CSID carrier bits before walking the page tables.
        constexpr uint64_t l1_index = 0;

        write_firmware_u64(l0_addr, l1_addr | table_desc_flags);
        write_firmware_u64(l1_addr + (l1_index * sizeof(uint64_t)), l2_addr | table_desc_flags);
        write_firmware_u64(l2_addr, l3_addr | table_desc_flags);
        write_firmware_u64(l3_addr, phys_addr | page_desc_flags);

        m_smmu.SMMU_SMR[cb][m_smmu.SMR_ID] = stream_id;
        m_smmu.SMMU_SMR[cb][m_smmu.SMR_MASK] = 0;
        m_smmu.SMMU_SMR[cb][m_smmu.SMR_VALID] = 1;
        m_smmu.SMMU_S2CR[cb][m_smmu.S2CR_CBNDX_VMID] = cb;
        m_smmu.SMMU_S2CR[cb][m_smmu.S2CR_TYPE] = 0;
        m_smmu.SMMU_CBAR[cb][m_smmu.CBAR_TYPE] = 1;
        m_smmu.SMMU_CBA2R[cb][m_smmu.CBA2R_VA64] = 1;
        m_smmu.SMMU_CB_TTBR0_LOW[cb] = static_cast<uint32_t>(l0_addr);
        m_smmu.SMMU_CB_TTBR0_HIGH[cb] = static_cast<uint32_t>(l0_addr >> 32);
        m_smmu.SMMU_CB_TCR_LPAE[cb] = (1U << 31) | 16U;
        m_smmu.SMMU_CB_SCTLR[cb][m_smmu.CB_SCTLR_M] = 1;
    }

    void write_firmware_u64(uint64_t address, uint64_t value)
    {
        m_memory.load.ptr_load(reinterpret_cast<uint8_t*>(&value), address, sizeof(value));
    }

    void finish_test()
    {
        m_completion_guard.async_detach_suspending();
    }

    void load_firmware_binary(const char* path, uint64_t address, std::initializer_list<uint64_t> patches)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        TEST_ASSERT(file.is_open());

        const std::streamsize size = file.tellg();
        std::vector<uint8_t> firmware(size);
        file.seekg(0, std::ios::beg);
        TEST_ASSERT(file.read(reinterpret_cast<char*>(firmware.data()), size));
        TEST_ASSERT(firmware.size() >= patches.size() * sizeof(uint64_t));

        size_t index = 0;
        for (const uint64_t patch : patches) {
            const size_t offset = firmware.size() - (patches.size() - index) * sizeof(patch);
            std::memcpy(firmware.data() + offset, &patch, sizeof(patch));
            ++index;
        }
        m_memory.load.ptr_load(firmware.data(), address, firmware.size());
    }
};

} // namespace

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53SMMU500SecureAttrsTest>(argc, argv); }
