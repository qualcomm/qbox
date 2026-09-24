/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SMMU500_TEST_BENCH_H
#define SMMU500_TEST_BENCH_H

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>

#include <cci_configuration>
#include <gs_memory.h>
#include <ports/target-signal-socket.h>
#include <tests/test-bench.h>

#include "smmu500.h"

namespace smmu500_test_regs {
constexpr uint64_t SCR0 = 0x00;
constexpr uint64_t SCR1 = 0x04;
constexpr uint64_t SCR2 = 0x08;
constexpr uint64_t SACR = 0x10;
constexpr uint64_t NSCR0 = 0x400;
constexpr uint64_t NSCR2 = 0x408;
constexpr uint64_t NSACR = 0x410;
constexpr uint64_t SIDR0 = 0x20;
constexpr uint64_t SIDR1 = 0x24;
constexpr uint64_t SIDR2 = 0x28;
constexpr uint64_t SIDR7 = 0x3c;
constexpr uint64_t SGFSR = 0x48;
constexpr uint64_t NSGFSR = 0x448;
constexpr uint64_t NSTLBGSTATUS = 0x474;
constexpr uint64_t STLBIALL = 0x60;
constexpr uint64_t TLBIVMID = 0x64;
constexpr uint64_t TLBIALLNSNH = 0x68;
constexpr uint64_t TLBIALLH = 0x6c;
constexpr uint64_t GATS1PR = 0x110;
constexpr uint64_t GATS1PR_H = 0x114;
constexpr uint64_t GATS1PW = 0x118;
constexpr uint64_t GATS1PW_H = 0x11C;
constexpr uint64_t GATS1UR = 0x100;
constexpr uint64_t GATS1UR_H = 0x104;
constexpr uint64_t GATS1UW = 0x108;
constexpr uint64_t GATS1UW_H = 0x10C;
constexpr uint64_t GATS12PR = 0x130;
constexpr uint64_t GATS12PR_H = 0x134;
constexpr uint64_t GATS12PW = 0x138;
constexpr uint64_t GATS12PW_H = 0x13C;
constexpr uint64_t GATS12UR = 0x120;
constexpr uint64_t GATS12UR_H = 0x124;
constexpr uint64_t GATS12UW = 0x128;
constexpr uint64_t GATS12UW_H = 0x12C;
constexpr uint64_t GPAR = 0x180;
constexpr uint64_t GPAR_H = 0x184;
constexpr uint64_t NSGATS1UR = 0x500;
constexpr uint64_t NSGATS1UR_H = 0x504;
constexpr uint64_t NSGPAR = 0x580;
constexpr uint64_t TBU_PWR_STATUS = 0x2204;
constexpr uint32_t ICF = 1u << 0;
constexpr uint32_t USF = 1u << 1;
constexpr uint32_t SMCF = 1u << 2;
constexpr uint32_t CB_FSR_TF = 1u << 1;
constexpr uint32_t CB_FSR_AFF = 1u << 2;
constexpr uint32_t CB_FSR_PF = 1u << 3;
constexpr uint32_t CB_FSR_EF = 1u << 4;
constexpr uint32_t CB_FSR_ASF = 1u << 7;
constexpr uint32_t CB_FSR_MULTI = 1u << 31;
constexpr uint32_t FSYNR0_WNR = 1u << 4;
constexpr uint32_t FSYNR0_PNU = 1u << 5;
constexpr uint32_t FSYNR0_IND = 1u << 6;
constexpr uint32_t FSYNR0_NSATTR = 1u << 8;
constexpr uint32_t FSYNR0_ATOF = 1u << 9;
constexpr uint32_t FSYNR0_PTWF = 1u << 10;
constexpr uint32_t FSYNR0_AFR = 1u << 11;
constexpr uint32_t GFRE = 1u << 1;
constexpr uint32_t GFIE = 1u << 2;
constexpr uint32_t USFCFG = 1u << 10;
constexpr uint32_t SMCFCFG = 1u << 21;
constexpr uint32_t GASRAE = 1u << 24;
constexpr uint32_t S2CR_TRANSLATION = 0;
constexpr uint32_t S2CR_BYPASS = 1;
constexpr uint32_t S2CR_FAULT = 2;
} // namespace smmu500_test_regs

class smmu500_irq_probe : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> input;
    bool level = false;
    unsigned int rising_edges = 0;

    smmu500_irq_probe(const sc_core::sc_module_name& n): sc_core::sc_module(n), input("input")
    {
        input.register_value_changed_cb([this](bool value) {
            level = value;
            if (value) ++rising_edges;
        });
    }
};

class smmu500_security_observing_memory : public gs::gs_memory<>
{
public:
    std::vector<std::optional<bool>> security_states;

    smmu500_security_observing_memory(const sc_core::sc_module_name& n): gs::gs_memory<>(n) {}

    void clear_security_states() { security_states.clear(); }

protected:
    void b_transport(int id, tlm::tlm_generic_payload& txn, sc_core::sc_time& delay) override
    {
        if (const auto* security = txn.get_extension<gs::QemuMemTxSecureTlmExtension>()) {
            security_states.emplace_back(security->value);
        } else {
            security_states.emplace_back(std::nullopt);
        }
        gs::gs_memory<>::b_transport(id, txn, delay);
    }
};

class smmu500_test_bench : public TestBench
{
private:
    struct preset_setter {
        preset_setter(const std::string& bench_name, uint16_t num_cb, uint8_t num_tbu, uint32_t pamax)
        {
            auto broker = cci::cci_get_broker();
            broker.set_preset_cci_value(bench_name + ".smmu.target_socket.address", cci::cci_value(0ULL));
            broker.set_preset_cci_value(bench_name + ".smmu.target_socket.size", cci::cci_value(0x200000ULL));
            broker.set_preset_cci_value(bench_name + ".smmu.target_socket.relative_addresses", cci::cci_value(true));
            broker.set_preset_cci_value(bench_name + ".smmu.num_smr", cci::cci_value(static_cast<uint16_t>(2)));
            broker.set_preset_cci_value(bench_name + ".smmu.num_cb", cci::cci_value(num_cb));
            broker.set_preset_cci_value(bench_name + ".smmu.num_tbu", cci::cci_value(num_tbu));
            broker.set_preset_cci_value(bench_name + ".smmu.pamax", cci::cci_value(pamax));
            broker.set_preset_cci_value(bench_name + ".memory.target_socket.address", cci::cci_value(0ULL));
            broker.set_preset_cci_value(bench_name + ".memory.target_socket.size", cci::cci_value(0x400000ULL));
            broker.set_preset_cci_value(bench_name + ".memory.target_socket.relative_addresses", cci::cci_value(true));
        }
    };

    preset_setter m_presets;

protected:
    gs::smmu500<> smmu;
    gs::smmu500_tbu<> tbu;
    smmu500_security_observing_memory memory;
    tlm_utils::simple_initiator_socket<smmu500_test_bench> mmio;
    tlm_utils::simple_initiator_socket<smmu500_test_bench> tbu_initiator;
    tlm_utils::simple_initiator_socket<smmu500_test_bench> memory_initiator;
    InitiatorSignalSocket<bool> reset_drv;
    std::vector<std::pair<uint64_t, uint64_t>> dmi_invalidations;

    smmu500_test_bench(const sc_core::sc_module_name& n): smmu500_test_bench(n, 1, 1, 48) {}

    smmu500_test_bench(const sc_core::sc_module_name& n, uint16_t num_cb, uint8_t num_tbu, uint32_t pamax)
        : TestBench(n)
        , m_presets(static_cast<const char*>(n), num_cb, num_tbu, pamax)
        , smmu("smmu")
        , tbu("tbu", &smmu)
        , memory("memory")
        , mmio("mmio")
        , tbu_initiator("tbu_initiator")
        , memory_initiator("memory_initiator")
        , reset_drv("reset_drv")
    {
        mmio.bind(smmu.socket);
        tbu_initiator.bind(tbu.upstream_socket);
        tbu.downstream_socket.bind(memory.socket);
        smmu.dma_socket.bind(memory.socket);
        memory_initiator.bind(memory.socket);
        reset_drv.bind(smmu.reset);
        tbu_initiator.register_invalidate_direct_mem_ptr(this, &smmu500_test_bench::on_dmi_invalidate);
    }

    void on_dmi_invalidate(sc_dt::uint64 start, sc_dt::uint64 end) { dmi_invalidations.emplace_back(start, end); }

    void configure_stream(uint32_t type, uint32_t cbndx = 0)
    {
        smmu.SMMU_SCR0 = 0;
        smmu.SMMU_NSCR0 = 0;
        smmu.SMMU_SGFSR = 0;
        smmu.SMMU_NSGFSR = 0;
        smmu.SMMU_SMR[0][smmu.SMR_ID] = 0;
        smmu.SMMU_SMR[0][smmu.SMR_MASK] = 0;
        smmu.SMMU_SMR[0][smmu.SMR_VALID] = 1;
        smmu.SMMU_S2CR[0][smmu.S2CR_CBNDX_VMID] = cbndx;
        smmu.SMMU_S2CR[0][smmu.S2CR_TYPE] = type;
    }

    void configure_identity_context()
    {
        configure_identity_context(0);
    }

    void configure_identity_context(unsigned int cb)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION, cb);
        smmu.SMMU_CBAR[cb][smmu.CBAR_TYPE] = 1;
        smmu.SMMU_CB_SCTLR[cb][smmu.CB_SCTLR_M] = 0;
    }

    void configure_page_table(uint64_t page_pa, bool read_only = false, bool access_flag = true)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 1;
        smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25;
        smmu.SMMU_CB_TTBR0_LOW[0] = 0x4000;
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;

        write_memory64(0x4000, 0x5000 | 0x3);
        write_memory64(0x5000, 0x6000 | 0x3);
        write_memory64(0x6008, page_pa | 0x3 | (1ULL << 6) | (access_flag ? (1ULL << 10) : 0) |
                                      (read_only ? (1ULL << 7) : 0));
    }

    void configure_stage2_page_table(uint64_t page_pa)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 0;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25 | (5u << 16);
        smmu.SMMU_CB_TTBR0_LOW[0] = 0x200000;
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;

        write_memory64(0x200000, 0x201000 | 0x3);
        write_memory64(0x201008, page_pa | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
    }

    void configure_nested_identity_context()
    {
        configure_page_table(0x8000);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 3;
        smmu.SMMU_CBAR[0][smmu.CBAR_S2_CBNDX] = 1;
        smmu.SMMU_CBAR[1][smmu.CBAR_TYPE] = 0;
        smmu.SMMU_CB_SCTLR[1][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[1] = 25 | (5u << 16);
        smmu.SMMU_CB_TTBR0_LOW[1] = 0x200000;

        write_memory64(0x200000, 0x201000 | 0x3);
        write_memory64(0x201020, 0x4000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201028, 0x5000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201030, 0x6000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201040, 0x8000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
    }

    void configure_ttbr1_page_table(uint64_t page_pa)
    {
        configure_page_table(page_pa);
        smmu.SMMU_CB_TTBR0_LOW[0] = 0;
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;
        smmu.SMMU_CB_TTBR1_LOW[0] = 0x4000;
        smmu.SMMU_CB_TTBR1_HIGH[0] = 0;
        smmu.SMMU_CB_TCR_LPAE[0] = 25 | (25u << 16) | (2u << 30);
    }

    void configure_ttbr1_large_granule_page_table(uint32_t tg1, uint64_t page_pa)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 1;
        smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25 | (25u << 16) | (tg1 << 30);
        smmu.SMMU_CB_TTBR0_LOW[0] = 0;
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;
        smmu.SMMU_CB_TTBR1_LOW[0] = 0x100000;
        smmu.SMMU_CB_TTBR1_HIGH[0] = 0;

        if (tg1 == 1) { // 16 KB
            write_memory64(0x100000, 0x104000 | 0x3);
            write_memory64(0x104000, 0x108000 | 0x3);
            write_memory64(0x108000, page_pa | 0x3 | (1ULL << 6) | (1ULL << 10));
        } else { // 64 KB
            write_memory64(0x100000, 0x110000 | 0x3);
            write_memory64(0x110000, page_pa | 0x3 | (1ULL << 6) | (1ULL << 10));
        }
    }

    void configure_nested_stage2_fault_from_cb1()
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION, 1);

        smmu.SMMU_CBAR[1][smmu.CBAR_TYPE] = 3;
        smmu.SMMU_CBAR[1][smmu.CBAR_S2_CBNDX] = 0;
        smmu.SMMU_CBA2R[1][smmu.CBA2R_VA64] = 1;
        smmu.SMMU_CB_SCTLR[1][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[1] = 25;
        smmu.SMMU_CB_TTBR0_LOW[1] = 0x4000;
        smmu.SMMU_CB_TTBR0_HIGH[1] = 0;
        write_memory64(0x4000, 0x5000 | 0x3);
        write_memory64(0x5000, 0x6000 | 0x3);
        write_memory64(0x6008, 0x8000 | 0x3 | (1ULL << 6) | (1ULL << 10));

        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 0;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25 | (5u << 16);
        smmu.SMMU_CB_TTBR0_LOW[0] = 0x200000;
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;
        write_memory64(0x200000, 0x201000 | 0x3);
        write_memory64(0x201020, 0x4000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201028, 0x5000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201030, 0x6000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));
        write_memory64(0x201040, 0);
    }

    void configure_large_granule_page_table(uint32_t tg, uint64_t page_pa)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 1;
        smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25 | (tg << 14);

        if (tg == 2) {
            smmu.SMMU_CB_TTBR0_LOW[0] = 0x100000;
            write_memory64(0x100000, 0x104000 | 0x3);
            write_memory64(0x104000, 0x108000 | 0x3);
            write_memory64(0x108000, page_pa | 0x3 | (1ULL << 6) | (1ULL << 10));
        } else {
            smmu.SMMU_CB_TTBR0_LOW[0] = 0x100000;
            write_memory64(0x100000, 0x110000 | 0x3);
            write_memory64(0x110000, page_pa | 0x3 | (1ULL << 6) | (1ULL << 10));
        }
        smmu.SMMU_CB_TTBR0_HIGH[0] = 0;
    }

    void configure_block_descriptor(uint64_t page_pa)
    {
        configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
        smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 1;
        smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;
        smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
        smmu.SMMU_CB_TCR_LPAE[0] = 25;
        smmu.SMMU_CB_TTBR0_LOW[0] = 0x4000;
        write_memory64(0x4000, page_pa | 0x1 | (1ULL << 6) | (1ULL << 10));
    }

    gs::smmu500<>::IOMMUTLBEntry translate(uint64_t address = 0x1234)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        return smmu.smmu500_translate(txn, 0);
    }

    uint32_t mmio_read32(uint64_t address)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio->b_transport(txn, delay);
        EXPECT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
        return value;
    }

    void mmio_write32(uint64_t address, uint32_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio->b_transport(txn, delay);
        EXPECT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    }

    tlm::tlm_response_status mmio_write32_with_security(uint64_t address, uint32_t value, bool secure)
    {
        tlm::tlm_generic_payload txn;
        gs::QemuMemTxSecureTlmExtension security;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        security.value = secure;
        txn.set_extension(&security);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio->b_transport(txn, delay);
        txn.clear_extension(&security);
        return txn.get_response_status();
    }

    tlm::tlm_response_status mmio_read32_with_security(uint64_t address, bool secure, uint32_t* value_out = nullptr)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload txn;
        gs::QemuMemTxSecureTlmExtension security;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        security.value = secure;
        txn.set_extension(&security);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio->b_transport(txn, delay);
        txn.clear_extension(&security);
        if (value_out) *value_out = value;
        return txn.get_response_status();
    }

    void write_memory64(uint64_t address, uint64_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        memory_initiator->b_transport(txn, delay);
        EXPECT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    }

    uint32_t read_memory32(uint64_t address)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        memory_initiator->b_transport(txn, delay);
        EXPECT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
        return value;
    }

    tlm::tlm_response_status tbu_write32(uint64_t address, uint32_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        tbu_initiator->b_transport(txn, delay);
        return txn.get_response_status();
    }

    tlm::tlm_response_status tbu_read32(uint64_t address, uint32_t& value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        tbu_initiator->b_transport(txn, delay);
        return txn.get_response_status();
    }

    unsigned int tbu_debug_write32(uint64_t address, uint32_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return tbu_initiator->transport_dbg(txn);
    }

    unsigned int tbu_debug_read32(uint64_t address, uint32_t& value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return tbu_initiator->transport_dbg(txn);
    }

    bool tbu_get_dmi(uint64_t address, tlm::tlm_dmi& dmi)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        return tbu_initiator->get_direct_mem_ptr(txn, dmi);
    }

    bool tbu_get_dmi_with_security(uint64_t address, tlm::tlm_dmi& dmi, bool secure)
    {
        tlm::tlm_generic_payload txn;
        gs::QemuMemTxSecureTlmExtension security;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        security.value = secure;
        txn.set_extension(&security);
        const bool ret = tbu_initiator->get_direct_mem_ptr(txn, dmi);
        txn.clear_extension(&security);
        return ret;
    }

    bool tbu_get_dmi_with_underlying(uint64_t address, tlm::tlm_dmi& dmi, gs::UnderlyingDMITlmExtension& underlying)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        txn.set_extension(&underlying);
        bool ret = tbu_initiator->get_direct_mem_ptr(txn, dmi);
        txn.clear_extension(&underlying);
        return ret;
    }
};

class smmu500_irq_test_bench : public smmu500_test_bench
{
protected:
    smmu500_irq_probe irq;

    smmu500_irq_test_bench(const sc_core::sc_module_name& n)
        : smmu500_test_bench(n)
        , irq("irq")
    {
        smmu.irq_global.bind(irq.input);
    }
};

class smmu500_multi_tbu_test_bench : public smmu500_test_bench
{
protected:
    gs::smmu500_tbu<> tbu_second;
    tlm_utils::simple_initiator_socket<smmu500_multi_tbu_test_bench> tbu_second_initiator;

    smmu500_multi_tbu_test_bench(const sc_core::sc_module_name& n)
        : smmu500_test_bench(n, 2, 2, 48)
        , tbu_second("tbu_second", &smmu)
        , tbu_second_initiator("tbu_second_initiator")
    {
        tbu_second_initiator.bind(tbu_second.upstream_socket);
        tbu_second.downstream_socket.bind(memory.socket);
        tbu_second_initiator.register_invalidate_direct_mem_ptr(
            this, &smmu500_multi_tbu_test_bench::on_second_dmi_invalidate);
    }

    void on_second_dmi_invalidate(sc_dt::uint64 start, sc_dt::uint64 end) { on_dmi_invalidate(start, end); }

    bool tbu_second_get_dmi(uint64_t address, tlm::tlm_dmi& dmi)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(address);
        return tbu_second_initiator->get_direct_mem_ptr(txn, dmi);
    }
};

class smmu500_pamax32_test_bench : public smmu500_test_bench
{
protected:
    smmu500_pamax32_test_bench(const sc_core::sc_module_name& n): smmu500_test_bench(n, 1, 1, 32) {}
};

class smmu500_fastrpc_test_bench : public smmu500_test_bench
{
protected:
    smmu500_fastrpc_test_bench(const sc_core::sc_module_name& n): smmu500_test_bench(n, 64, 1, 36) {}
};

class smmu500_context_irq_test_bench : public smmu500_test_bench
{
protected:
    smmu500_irq_probe irq;

    smmu500_context_irq_test_bench(const sc_core::sc_module_name& n)
        : smmu500_test_bench(n)
        , irq("irq")
    {
        smmu.irq_context[0].bind(irq.input);
    }
};

#endif
