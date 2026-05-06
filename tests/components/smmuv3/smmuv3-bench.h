/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SMMUV3_BENCH_H
#define SMMUV3_BENCH_H

#include <tests/test-bench.h>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <cci_configuration>
#include <gs_memory.h>
#include <router.h>
#include <ports/target-signal-socket.h>
#include <ports/initiator-signal-socket.h>
#include "smmuv3.h"

static constexpr uint64_t SMMUV3_MMIO_BASE = 0x15000000ULL;
static constexpr uint64_t SMMUV3_MMIO_SIZE = 0x20000ULL;
static constexpr uint64_t DRAM_BASE = 0x80000000ULL;
static constexpr uint64_t DRAM_SIZE = 0x10000000ULL;

namespace smmuv3_regs {
constexpr uint64_t IDR0 = 0x000;
constexpr uint64_t IDR1 = 0x004;
constexpr uint64_t IDR5 = 0x014;
constexpr uint64_t IIDR = 0x018;
constexpr uint64_t CR0 = 0x020;
constexpr uint64_t CR0ACK = 0x024;
constexpr uint64_t GBPA = 0x044;
constexpr uint64_t IRQ_CTRL = 0x050;
constexpr uint64_t IRQ_CTRL_ACK = 0x054;
constexpr uint64_t GERROR = 0x060;
constexpr uint64_t GERRORN = 0x064;
constexpr uint64_t STRTAB_BASE_LO = 0x080;
constexpr uint64_t STRTAB_BASE_HI = 0x084;
constexpr uint64_t STRTAB_BASE_CFG = 0x088;
constexpr uint64_t CMDQ_BASE_LO = 0x090;
constexpr uint64_t CMDQ_BASE_HI = 0x094;
constexpr uint64_t CMDQ_PROD = 0x098;
constexpr uint64_t CMDQ_CONS = 0x09C;
constexpr uint64_t EVENTQ_BASE_LO = 0x0A0;
constexpr uint64_t EVENTQ_BASE_HI = 0x0A4;
constexpr uint64_t EVENTQ_PROD = 0x0A8;
constexpr uint64_t EVENTQ_CONS = 0x0AC;
constexpr uint64_t PRIQ_BASE_LO = 0x0C0;
constexpr uint64_t PRIQ_BASE_HI = 0x0C4;
constexpr uint64_t PRIQ_PROD = 0x0C8;
constexpr uint64_t PRIQ_CONS = 0x0CC;
constexpr uint64_t GATOS_CTRL = 0x100;
constexpr uint64_t GATOS_SID = 0x108;
constexpr uint64_t GATOS_ADDR_LO = 0x110;
constexpr uint64_t GATOS_ADDR_HI = 0x114;
constexpr uint64_t GATOS_PAR_LO = 0x118;
constexpr uint64_t GATOS_PAR_HI = 0x11C;

constexpr uint64_t PAGE1_OFFSET = 0x10000;
constexpr uint64_t S_IDR0 = 0x8000;
constexpr uint64_t S_IDR1 = 0x8004;
constexpr uint64_t S_CR0 = 0x8020;
constexpr uint64_t S_CR0ACK = 0x8024;
constexpr uint64_t S_INIT = 0x803C;
constexpr uint64_t S_GBPA = 0x8044;
constexpr uint64_t S_IRQ_CTRL = 0x8050;
constexpr uint64_t S_IRQ_CTRL_ACK = 0x8054;
constexpr uint64_t S_GERROR = 0x8060;
constexpr uint64_t S_GERRORN = 0x8064;
constexpr uint64_t S_GERROR_IRQ_CFG0_LO = 0x8068;
constexpr uint64_t S_GERROR_IRQ_CFG0_HI = 0x806C;
constexpr uint64_t S_GERROR_IRQ_CFG1 = 0x8070;
constexpr uint64_t S_GERROR_IRQ_CFG2 = 0x8074;
constexpr uint64_t S_EVENTQ_IRQ_CFG0_LO = 0x80B0;
constexpr uint64_t S_EVENTQ_IRQ_CFG0_HI = 0x80B4;
constexpr uint64_t S_EVENTQ_IRQ_CFG1 = 0x80B8;
constexpr uint64_t S_EVENTQ_IRQ_CFG2 = 0x80BC;
constexpr uint64_t S_STRTAB_BASE_LO = 0x8080;
constexpr uint64_t S_STRTAB_BASE_HI = 0x8084;
constexpr uint64_t S_STRTAB_BASE_CFG = 0x8088;
constexpr uint64_t S_CMDQ_BASE_LO = 0x8090;
constexpr uint64_t S_CMDQ_BASE_HI = 0x8094;
constexpr uint64_t S_CMDQ_PROD = 0x8098;
constexpr uint64_t S_CMDQ_CONS = 0x809C;
constexpr uint64_t S_EVENTQ_BASE_LO = 0x80A0;
constexpr uint64_t S_EVENTQ_BASE_HI = 0x80A4;
constexpr uint64_t S_EVENTQ_PROD = 0x80A8;
constexpr uint64_t S_EVENTQ_CONS = 0x80AC;

constexpr uint64_t GERROR_IRQ_CFG0_LO = 0x068;
constexpr uint64_t GERROR_IRQ_CFG0_HI = 0x06C;
constexpr uint64_t GERROR_IRQ_CFG1 = 0x070;
constexpr uint64_t GERROR_IRQ_CFG2 = 0x074;
constexpr uint64_t EVENTQ_IRQ_CFG0_LO = 0x0B0;
constexpr uint64_t EVENTQ_IRQ_CFG0_HI = 0x0B4;
constexpr uint64_t EVENTQ_IRQ_CFG1 = 0x0B8;
constexpr uint64_t EVENTQ_IRQ_CFG2 = 0x0BC;
constexpr uint64_t PRIQ_IRQ_CFG0_LO = 0x0D0;
constexpr uint64_t PRIQ_IRQ_CFG0_HI = 0x0D4;
constexpr uint64_t PRIQ_IRQ_CFG1 = 0x0D8;
constexpr uint64_t PRIQ_IRQ_CFG2 = 0x0DC;

constexpr uint32_t CR0_SMMUEN = 1u << 0;
constexpr uint32_t CR0_PRIQEN = 1u << 1;
constexpr uint32_t CR0_EVENTQEN = 1u << 2;
constexpr uint32_t CR0_CMDQEN = 1u << 3;

constexpr uint32_t IRQ_CTRL_ALL = 0x7;

constexpr uint32_t GBPA_ABORT = 1u << 20;
constexpr uint32_t GBPA_UPDATE = 1u << 31;

constexpr uint32_t S_INIT_INV_ALL = 1u << 0;

constexpr uint32_t GATOS_CTRL_RUN = 1u << 0;
constexpr uint32_t GATOS_CTRL_READ_NWRITE = 1u << 8;
} // namespace smmuv3_regs

namespace smmuv3_bench_consts {
constexpr uint32_t STE_BYTES = 64;
constexpr uint32_t CD_BYTES = 64;
constexpr uint32_t CMD_BYTES = 16;
constexpr uint32_t EVENT_BYTES = 32;
constexpr uint32_t PRI_BYTES = 16;
constexpr uint32_t DESC_BYTES = 8;
constexpr uint32_t L1STD_BYTES = 8;

constexpr uint64_t STE_VALID = 0x1ULL;
constexpr uint32_t STE_CONFIG_SHIFT = 1;
constexpr uint64_t STE_S1CTXPTR_MASK = 0x000FFFFFFFFFFFC0ULL;
constexpr uint64_t STE_S2TTB_MASK = 0x000FFFFFFFFFFFF0ULL;

constexpr uint32_t STRTAB_LOG2SIZE_MASK = 0x3F;
constexpr uint32_t STRTAB_SPLIT_SHIFT = 6;
constexpr uint32_t STRTAB_SPLIT_MASK = 0x1F;
constexpr uint32_t STRTAB_FMT_2LVL_SHIFT = 16;

constexpr uint64_t CD_VALID_BIT = 1ULL << 31;
constexpr uint64_t CD_AARCH64_BIT = 1ULL << 9;
constexpr uint64_t CD_EPD0_BIT = 1ULL << 14;
constexpr uint64_t CD_EPD1_BIT = 1ULL << 30;
constexpr uint64_t CD_AFFD_BIT = 1ULL << 35;
constexpr uint64_t CD_HA_BIT = 1ULL << 43;
constexpr uint64_t CD_HD_BIT = 1ULL << 42;
constexpr uint32_t CD_T0SZ_MASK = 0x3F;
constexpr uint32_t CD_TG_MASK = 0x3;
constexpr uint32_t CD_TG_SHIFT = 6;
constexpr uint32_t CD_T1SZ_SHIFT = 16;
constexpr uint32_t CD_TG1_SHIFT = 22;
constexpr uint32_t CD_IPS_MASK = 0x7;
constexpr uint32_t CD_ASID_SHIFT = 16;
constexpr uint32_t CD_TTB_LO_MASK = 0xFFFFFFF0u;
constexpr uint32_t CD_TTB_HI_MASK = 0xFFFFFu;

constexpr uint64_t S2_VTCR_T0SZ_MASK = 0x3F;
constexpr uint32_t S2_VTCR_SL0_SHIFT = 6;
constexpr uint64_t S2_VTCR_SL0_MASK = 0x3;
constexpr uint32_t S2_VTCR_TG_SHIFT = 14;
constexpr uint64_t S2_VTCR_TG_MASK = 0x3;
constexpr uint32_t S2_VTCR_PS_SHIFT = 16;
constexpr uint64_t S2_VTCR_PS_MASK = 0x7;
constexpr uint32_t S2_VTCR_DW2_SHIFT = 32;

constexpr uint64_t TABLE_DESC_PA_MASK = 0x0000FFFFFFFFF000ULL;
constexpr uint64_t TABLE_DESC_TYPE = 0x3;

constexpr uint64_t BLOCK_1G_PA_MASK = ~0x3FFFFFFFULL;
constexpr uint64_t BLOCK_2M_PA_MASK = ~0x1FFFFFULL;
constexpr uint64_t PAGE_4K_PA_MASK = ~0xFFFULL;
constexpr uint64_t PAGE_16K_PA_MASK = ~0x3FFFULL;
constexpr uint64_t PAGE_64K_PA_MASK = ~0xFFFFULL;

constexpr uint64_t DESC_FLAGS_BLOCK = (1ULL << 0) | (1ULL << 6) | (1ULL << 10);
constexpr uint64_t DESC_FLAGS_PAGE = (1ULL << 0) | (1ULL << 1) | (1ULL << 6) | (1ULL << 10);
constexpr uint64_t DESC_RO_BIT = 1ULL << 7;
constexpr uint64_t S2_DESC_FLAGS_RW = (1ULL << 0) | (1ULL << 1) | (1ULL << 6) | (1ULL << 7) | (1ULL << 10);
constexpr uint64_t S2_DESC_FLAGS_BLOCK_RW = (1ULL << 0) | (1ULL << 6) | (1ULL << 7) | (1ULL << 10);

constexpr uint32_t QUEUE_LOG2_MASK = 0x1F;
} // namespace smmuv3_bench_consts

class irq_counter : public sc_core::sc_module
{
public:
    TargetSignalSocket<bool> in;
    uint32_t count;

    irq_counter(sc_core::sc_module_name n): sc_core::sc_module(n), in("in"), count(0)
    {
        in.register_value_changed_cb([this](bool v) {
            if (v) ++count;
        });
    }
};

class dmi_invalidate_helper : public sc_core::sc_module
{
public:
    tlm_utils::simple_target_socket<dmi_invalidate_helper> target_socket;

    dmi_invalidate_helper(sc_core::sc_module_name n): sc_core::sc_module(n), target_socket("target_socket")
    {
        target_socket.register_b_transport(this, &dmi_invalidate_helper::b_transport);
        target_socket.register_get_direct_mem_ptr(this, &dmi_invalidate_helper::get_direct_mem_ptr);
    }

    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    bool get_direct_mem_ptr(tlm::tlm_generic_payload&, tlm::tlm_dmi&) { return false; }

    void fire_invalidate(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        target_socket->invalidate_direct_mem_ptr(start, end);
    }
};

class dmi_invalidate_observer : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<dmi_invalidate_observer> initiator_socket;
    std::vector<std::pair<sc_dt::uint64, sc_dt::uint64>> ranges;

    dmi_invalidate_observer(sc_core::sc_module_name n): sc_core::sc_module(n), initiator_socket("initiator_socket")
    {
        initiator_socket.register_invalidate_direct_mem_ptr(this, &dmi_invalidate_observer::on_invalidate);
    }

    void on_invalidate(sc_dt::uint64 start, sc_dt::uint64 end) { ranges.emplace_back(start, end); }
};

class smmuv3_bench : public TestBench
{
private:
    struct preset_setter {
        preset_setter(const std::string& bench_name) { set_presets(bench_name); }
    };

public:
    preset_setter _presets;
    gs::smmuv3<32> smmu;
    gs::smmuv3_tbu<32> tbu;
    gs::smmuv3_tbu<32> tbu1;
    gs::smmuv3_tbu<32> tbu_dmi_inv;
    gs::gs_memory<> main_mem;
    gs::router<> router;

    tlm_utils::simple_initiator_socket<smmuv3_bench> tbu_initiator;
    tlm_utils::simple_initiator_socket<smmuv3_bench> tbu1_initiator;
    tlm_utils::simple_initiator_socket<smmuv3_bench> mmio_initiator;

    irq_counter irq_eventq_cnt;
    irq_counter irq_priq_cnt;
    irq_counter irq_cmd_sync_cnt;
    irq_counter irq_gerror_cnt;

    InitiatorSignalSocket<bool> reset_drv;

    dmi_invalidate_observer dmi_inv_observer;
    dmi_invalidate_helper dmi_inv_helper;

    static void set_presets(const std::string& bench_name)
    {
        auto broker = cci::cci_get_broker();
        broker.set_preset_cci_value(bench_name + ".smmu.target_socket.address", cci::cci_value(SMMUV3_MMIO_BASE));
        broker.set_preset_cci_value(bench_name + ".smmu.target_socket.size", cci::cci_value(SMMUV3_MMIO_SIZE));
        broker.set_preset_cci_value(bench_name + ".smmu.target_socket.relative_addresses", cci::cci_value(true));
        broker.set_preset_cci_value(bench_name + ".main_mem.target_socket.address", cci::cci_value(DRAM_BASE));
        broker.set_preset_cci_value(bench_name + ".main_mem.target_socket.size", cci::cci_value(DRAM_SIZE));
        broker.set_preset_cci_value(bench_name + ".main_mem.target_socket.relative_addresses", cci::cci_value(false));
        broker.set_preset_cci_value(bench_name + ".tbu1.topology_id", cci::cci_value(1u));
    }

    smmuv3_bench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , _presets(static_cast<const char*>(n))
        , smmu("smmu")
        , tbu("tbu", &smmu)
        , tbu1("tbu1", &smmu)
        , tbu_dmi_inv("tbu_dmi_inv", &smmu)
        , main_mem("main_mem")
        , router("router")
        , tbu_initiator("tbu_initiator")
        , tbu1_initiator("tbu1_initiator")
        , mmio_initiator("mmio_initiator")
        , irq_eventq_cnt("irq_eventq_cnt")
        , irq_priq_cnt("irq_priq_cnt")
        , irq_cmd_sync_cnt("irq_cmd_sync_cnt")
        , irq_gerror_cnt("irq_gerror_cnt")
        , reset_drv("reset_drv")
        , dmi_inv_observer("dmi_inv_observer")
        , dmi_inv_helper("dmi_inv_helper")
    {
        router.initiator_socket.bind(smmu.socket);
        router.initiator_socket.bind(main_mem.socket);
        tbu_initiator.bind(tbu.upstream_socket);
        tbu.downstream_socket.bind(router.target_socket);
        tbu1_initiator.bind(tbu1.upstream_socket);
        tbu1.downstream_socket.bind(router.target_socket);
        smmu.dma_socket.bind(router.target_socket);
        mmio_initiator.bind(router.target_socket);

        dmi_inv_observer.initiator_socket.bind(tbu_dmi_inv.upstream_socket);
        tbu_dmi_inv.downstream_socket.bind(dmi_inv_helper.target_socket);

        smmu.irq_eventq.bind(irq_eventq_cnt.in);
        smmu.irq_priq.bind(irq_priq_cnt.in);
        smmu.irq_cmd_sync.bind(irq_cmd_sync_cnt.in);
        smmu.irq_gerror.bind(irq_gerror_cnt.in);
        reset_drv.bind(smmu.reset);
    }

    void write_dram(uint64_t addr, const void* data, size_t len)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data)));
        txn.set_data_length(len);
        txn.set_streaming_width(len);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio_initiator->b_transport(txn, delay);
    }

    void read_dram(uint64_t addr, void* data, size_t len)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(data));
        txn.set_data_length(len);
        txn.set_streaming_width(len);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio_initiator->b_transport(txn, delay);
    }

    tlm::tlm_response_status mmio_write32(uint64_t offset, uint32_t value)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(SMMUV3_MMIO_BASE + offset);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio_initiator->b_transport(txn, delay);
        return txn.get_response_status();
    }

    uint32_t mmio_read32(uint64_t offset)
    {
        uint32_t value = 0;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(SMMUV3_MMIO_BASE + offset);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        mmio_initiator->b_transport(txn, delay);
        return value;
    }

    tlm::tlm_response_status tbu_txn(uint64_t iova, bool write, uint32_t data)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        txn.set_address(iova);
        uint32_t local = data;
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&local));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        tbu_initiator->b_transport(txn, delay);
        return txn.get_response_status();
    }

    unsigned int tbu_dbg_read(uint64_t iova, uint32_t& out)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(iova);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&out));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        return tbu_initiator->transport_dbg(txn);
    }

    void pulse_reset()
    {
        reset_drv->write(true);
        sc_core::wait(1, sc_core::SC_NS);
        reset_drv->write(false);
        sc_core::wait(1, sc_core::SC_NS);
    }

    struct EventRecord {
        uint8_t type;
        bool ssv;
        uint32_t ssid;
        uint32_t streamid;
        uint16_t stag;
        bool rnw;
        uint8_t class_;
        bool stall;
        uint8_t reason;
        uint64_t iova;
        uint64_t ipa;
    };

    EventRecord read_event(uint32_t idx, uint64_t eventq_base = 0x90000000ULL)
    {
        std::array<uint64_t, 4> w{};
        read_dram(eventq_base + idx * smmuv3_bench_consts::EVENT_BYTES, w.data(), smmuv3_bench_consts::EVENT_BYTES);
        EventRecord ev;
        ev.type = static_cast<uint8_t>(w[0] & 0xFF);
        ev.ssv = (w[0] >> 12) & 0x1;
        ev.ssid = (w[0] >> 13) & 0xFFFFF;
        ev.streamid = static_cast<uint32_t>(w[0] >> 32);
        ev.stag = static_cast<uint16_t>(w[1] & 0xFFFF);
        ev.rnw = (w[1] >> 17) & 0x1;
        ev.class_ = (w[1] >> 20) & 0x3;
        ev.stall = (w[1] >> 23) & 0x1;
        ev.reason = (w[1] >> 24) & 0x7;
        ev.iova = w[2];
        ev.ipa = w[3];
        return ev;
    }

    EventRecord read_last_event(uint64_t eventq_base = 0x90000000ULL)
    {
        uint32_t prod = mmio_read32(smmuv3_regs::EVENTQ_PROD);
        uint32_t cons = mmio_read32(smmuv3_regs::EVENTQ_CONS);
        uint32_t count = (prod ^ cons) & ~(1u << 19);
        if (count == 0) return EventRecord{};
        uint32_t last_idx = (prod - 1) & ((1u << 19) - 1);
        return read_event(last_idx, eventq_base);
    }

    bool drain_events()
    {
        uint32_t prod = mmio_read32(smmuv3_regs::EVENTQ_PROD);
        mmio_write32(smmuv3_regs::EVENTQ_CONS, prod);
        return true;
    }

    void attach_substream(tlm::tlm_generic_payload& txn, uint32_t ssid, bool ssv = true)
    {
        auto* ext = new gs::smmuv3_ss_extension();
        ext->substream_id = ssid;
        ext->ssv = ssv;
        txn.set_extension(ext);
    }

    void attach_secure(tlm::tlm_generic_payload& txn, bool secure = true)
    {
        auto* ext = new gs::smmuv3_secure_extension();
        ext->secure = secure;
        txn.set_extension(ext);
    }

    bool tbu_get_dmi(uint64_t iova, bool write, tlm::tlm_dmi& dmi)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        txn.set_address(iova);
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(true);
        return tbu_initiator->get_direct_mem_ptr(txn, dmi);
    }

    tlm::tlm_response_status tbu1_txn(uint64_t iova, bool write, uint32_t data)
    {
        tlm::tlm_generic_payload txn;
        txn.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        txn.set_address(iova);
        uint32_t local = data;
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&local));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        tbu1_initiator->b_transport(txn, delay);
        return txn.get_response_status();
    }

    void setup_linear_stream_table(uint64_t base_addr, uint32_t log2size)
    {
        mmio_write32(smmuv3_regs::STRTAB_BASE_LO, static_cast<uint32_t>(base_addr));
        mmio_write32(smmuv3_regs::STRTAB_BASE_HI, static_cast<uint32_t>(base_addr >> 32));
        mmio_write32(smmuv3_regs::STRTAB_BASE_CFG, log2size & smmuv3_bench_consts::STRTAB_LOG2SIZE_MASK);
    }

    void setup_2level_stream_table(uint64_t base_addr, uint32_t log2size, uint32_t split)
    {
        mmio_write32(smmuv3_regs::STRTAB_BASE_LO, static_cast<uint32_t>(base_addr));
        mmio_write32(smmuv3_regs::STRTAB_BASE_HI, static_cast<uint32_t>(base_addr >> 32));
        mmio_write32(smmuv3_regs::STRTAB_BASE_CFG,
                     (log2size & smmuv3_bench_consts::STRTAB_LOG2SIZE_MASK) |
                         ((split & smmuv3_bench_consts::STRTAB_SPLIT_MASK) << smmuv3_bench_consts::STRTAB_SPLIT_SHIFT) |
                         (1u << smmuv3_bench_consts::STRTAB_FMT_2LVL_SHIFT));
    }

    void enable_smmu()
    {
        mmio_write32(smmuv3_regs::CR0, mmio_read32(smmuv3_regs::CR0) | smmuv3_regs::CR0_SMMUEN);
        sc_core::wait(1, sc_core::SC_NS);
    }

    void enable_cmdq()
    {
        mmio_write32(smmuv3_regs::CR0, mmio_read32(smmuv3_regs::CR0) | smmuv3_regs::CR0_CMDQEN);
        sc_core::wait(1, sc_core::SC_NS);
    }

    void write_ste_bypass(uint64_t ste_addr)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID |
                (static_cast<uint64_t>(gs::STE_CONFIG_BYPASS) << smmuv3_bench_consts::STE_CONFIG_SHIFT);
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_ste_invalid(uint64_t ste_addr)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_ste_abort(uint64_t ste_addr)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID;
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_ste_s1(uint64_t ste_addr, uint64_t cd_ptr)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID |
                (static_cast<uint64_t>(gs::STE_CONFIG_S1) << smmuv3_bench_consts::STE_CONFIG_SHIFT) |
                (cd_ptr & smmuv3_bench_consts::STE_S1CTXPTR_MASK);
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    // write_ste_s1_strw: same as write_ste_s1 but also stamps a STRW (StreamWorld) value into STE.W1[30:31].
    // strw_val: 0 = NS-EL1, 2 = NS-EL2 (further qualified by CR2.E2H at translate time).
    void write_ste_s1_strw(uint64_t ste_addr, uint64_t cd_ptr, uint8_t strw_val)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID |
                (static_cast<uint64_t>(gs::STE_CONFIG_S1) << smmuv3_bench_consts::STE_CONFIG_SHIFT) |
                (cd_ptr & smmuv3_bench_consts::STE_S1CTXPTR_MASK);
        dw[1] = (static_cast<uint64_t>(strw_val) & 0x3ULL) << 30;
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_ste_s2(uint64_t ste_addr, uint64_t s2ttb, uint32_t t0sz, uint32_t sl0, uint32_t tg, uint32_t ps,
                      uint16_t vmid = 0)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID |
                (static_cast<uint64_t>(gs::STE_CONFIG_S2) << smmuv3_bench_consts::STE_CONFIG_SHIFT);
        uint64_t vtcr = (static_cast<uint64_t>(t0sz) & smmuv3_bench_consts::S2_VTCR_T0SZ_MASK) |
                        ((static_cast<uint64_t>(sl0) & smmuv3_bench_consts::S2_VTCR_SL0_MASK)
                         << smmuv3_bench_consts::S2_VTCR_SL0_SHIFT) |
                        ((static_cast<uint64_t>(tg) & smmuv3_bench_consts::S2_VTCR_TG_MASK)
                         << smmuv3_bench_consts::S2_VTCR_TG_SHIFT) |
                        ((static_cast<uint64_t>(ps) & smmuv3_bench_consts::S2_VTCR_PS_MASK)
                         << smmuv3_bench_consts::S2_VTCR_PS_SHIFT);
        dw[2] = static_cast<uint64_t>(vmid) | (vtcr << smmuv3_bench_consts::S2_VTCR_DW2_SHIFT);
        dw[3] = s2ttb & smmuv3_bench_consts::STE_S2TTB_MASK;
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_ste_nested(uint64_t ste_addr, uint64_t cd_ptr, uint64_t s2ttb, uint32_t t0sz, uint32_t sl0, uint32_t tg,
                          uint32_t ps, uint16_t vmid = 0)
    {
        std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
        dw[0] = smmuv3_bench_consts::STE_VALID |
                (static_cast<uint64_t>(gs::STE_CONFIG_NESTED) << smmuv3_bench_consts::STE_CONFIG_SHIFT) |
                (cd_ptr & smmuv3_bench_consts::STE_S1CTXPTR_MASK);
        uint64_t vtcr = (static_cast<uint64_t>(t0sz) & smmuv3_bench_consts::S2_VTCR_T0SZ_MASK) |
                        ((static_cast<uint64_t>(sl0) & smmuv3_bench_consts::S2_VTCR_SL0_MASK)
                         << smmuv3_bench_consts::S2_VTCR_SL0_SHIFT) |
                        ((static_cast<uint64_t>(tg) & smmuv3_bench_consts::S2_VTCR_TG_MASK)
                         << smmuv3_bench_consts::S2_VTCR_TG_SHIFT) |
                        ((static_cast<uint64_t>(ps) & smmuv3_bench_consts::S2_VTCR_PS_MASK)
                         << smmuv3_bench_consts::S2_VTCR_PS_SHIFT);
        dw[2] = static_cast<uint64_t>(vmid) | (vtcr << smmuv3_bench_consts::S2_VTCR_DW2_SHIFT);
        dw[3] = s2ttb & smmuv3_bench_consts::STE_S2TTB_MASK;
        write_dram(ste_addr, ste.data(), smmuv3_bench_consts::STE_BYTES);
    }

    void write_l1_std(uint64_t l1_addr, uint32_t idx, uint64_t l2_ste_table)
    {
        uint64_t std_entry = (l2_ste_table & smmuv3_bench_consts::STE_S1CTXPTR_MASK) | smmuv3_bench_consts::STE_VALID;
        write_dram(l1_addr + idx * smmuv3_bench_consts::L1STD_BYTES, &std_entry, smmuv3_bench_consts::L1STD_BYTES);
    }

    void write_cd_identity(uint64_t cd_addr, uint64_t ttb, uint32_t tsz, uint32_t tg, uint32_t ips, uint16_t asid = 0,
                           bool affd = true, bool ha = false, bool hd = false)
    {
        std::array<uint8_t, smmuv3_bench_consts::CD_BYTES> cd{};
        uint64_t word0 = (tsz & smmuv3_bench_consts::CD_T0SZ_MASK) |
                         ((tg & smmuv3_bench_consts::CD_TG_MASK) << smmuv3_bench_consts::CD_TG_SHIFT) |
                         smmuv3_bench_consts::CD_VALID_BIT;
        if (affd) word0 |= smmuv3_bench_consts::CD_AFFD_BIT;
        if (ha) word0 |= smmuv3_bench_consts::CD_HA_BIT;
        if (hd) word0 |= smmuv3_bench_consts::CD_HD_BIT;

        uint32_t* w = reinterpret_cast<uint32_t*>(cd.data());
        w[0] = static_cast<uint32_t>(word0);
        w[1] = static_cast<uint32_t>(word0 >> 32) | (ips & smmuv3_bench_consts::CD_IPS_MASK) |
               static_cast<uint32_t>(smmuv3_bench_consts::CD_AARCH64_BIT) |
               (static_cast<uint32_t>(asid) << smmuv3_bench_consts::CD_ASID_SHIFT);
        w[2] = static_cast<uint32_t>(ttb) & smmuv3_bench_consts::CD_TTB_LO_MASK;
        w[3] = static_cast<uint32_t>(ttb >> 32) & smmuv3_bench_consts::CD_TTB_HI_MASK;

        write_dram(cd_addr, cd.data(), smmuv3_bench_consts::CD_BYTES);
    }

    void write_cd_ttbr1(uint64_t cd_addr, uint64_t ttb1, uint32_t t1sz, uint32_t tg1_enc, uint32_t ips)
    {
        std::array<uint8_t, smmuv3_bench_consts::CD_BYTES> cd{};
        uint32_t* w = reinterpret_cast<uint32_t*>(cd.data());
        w[0] = ((t1sz & smmuv3_bench_consts::CD_T0SZ_MASK) << smmuv3_bench_consts::CD_T1SZ_SHIFT) |
               ((tg1_enc & smmuv3_bench_consts::CD_TG_MASK) << smmuv3_bench_consts::CD_TG1_SHIFT) |
               static_cast<uint32_t>(smmuv3_bench_consts::CD_EPD0_BIT) |
               static_cast<uint32_t>(smmuv3_bench_consts::CD_VALID_BIT);
        w[1] = (ips & smmuv3_bench_consts::CD_IPS_MASK) | static_cast<uint32_t>(smmuv3_bench_consts::CD_AARCH64_BIT);
        w[4] = static_cast<uint32_t>(ttb1) & smmuv3_bench_consts::CD_TTB_LO_MASK;
        w[5] = static_cast<uint32_t>(ttb1 >> 32) & smmuv3_bench_consts::CD_TTB_HI_MASK;
        write_dram(cd_addr, cd.data(), smmuv3_bench_consts::CD_BYTES);
    }

    void write_cd_invalid(uint64_t cd_addr)
    {
        std::array<uint8_t, smmuv3_bench_consts::CD_BYTES> cd{};
        write_dram(cd_addr, cd.data(), smmuv3_bench_consts::CD_BYTES);
    }

    void write_table_desc(uint64_t table_addr, uint32_t idx, uint64_t next_table)
    {
        uint64_t desc = (next_table & smmuv3_bench_consts::TABLE_DESC_PA_MASK) | smmuv3_bench_consts::TABLE_DESC_TYPE;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_raw_desc(uint64_t table_addr, uint32_t idx, uint64_t desc)
    {
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_block_1gb(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::BLOCK_1G_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_BLOCK;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_block_2mb(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::BLOCK_2M_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_BLOCK;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_page_4k(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_page_16k(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::PAGE_16K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }
    void write_page_64k(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::PAGE_64K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_page_4k_ro(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE |
                        smmuv3_bench_consts::DESC_RO_BIT;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_page_4k_s2(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | smmuv3_bench_consts::S2_DESC_FLAGS_RW;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void write_block_1gb_s2(uint64_t table_addr, uint32_t idx, uint64_t pa)
    {
        uint64_t desc = (pa & smmuv3_bench_consts::BLOCK_1G_PA_MASK) | smmuv3_bench_consts::S2_DESC_FLAGS_BLOCK_RW;
        write_dram(table_addr + idx * smmuv3_bench_consts::DESC_BYTES, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    void setup_cmdq(uint64_t base_addr, uint32_t log2size)
    {
        uint32_t lo = static_cast<uint32_t>(base_addr) | (log2size & smmuv3_bench_consts::QUEUE_LOG2_MASK);
        uint32_t hi = static_cast<uint32_t>(base_addr >> 32);
        mmio_write32(smmuv3_regs::CMDQ_BASE_LO, lo);
        mmio_write32(smmuv3_regs::CMDQ_BASE_HI, hi);
        mmio_write32(smmuv3_regs::CMDQ_PROD, 0);
        mmio_write32(smmuv3_regs::CMDQ_CONS, 0);
    }

    void issue_cmd(uint64_t cmdq_base, uint32_t idx, const uint8_t cmd[16])
    {
        write_dram(cmdq_base + idx * smmuv3_bench_consts::CMD_BYTES, cmd, smmuv3_bench_consts::CMD_BYTES);
        mmio_write32(smmuv3_regs::CMDQ_PROD, idx + 1);
        sc_core::wait(10, sc_core::SC_NS);
    }

    void prime_translation_and_caches(uint64_t cmdq_base)
    {
        const uint64_t strtab = DRAM_BASE;
        const uint64_t cd_base = DRAM_BASE + 0x1000;
        const uint64_t l0 = DRAM_BASE + 0x2000;
        const uint64_t l1 = DRAM_BASE + 0x3000;
        const uint64_t l2 = DRAM_BASE + 0x4000;
        const uint64_t l3 = DRAM_BASE + 0x5000;
        const uint64_t page_pa = DRAM_BASE + 0x0A000000;

        setup_linear_stream_table(strtab, 4);
        write_ste_s1(strtab, cd_base);
        write_cd_identity(cd_base, l0, 16, 0, 5);
        write_table_desc(l0, 0, l1);
        write_table_desc(l1, 0, l2);
        write_table_desc(l2, 0, l3);
        write_page_4k(l3, 0, page_pa);
        setup_cmdq(cmdq_base, 4);
        enable_cmdq();
        enable_smmu();
        (void)tbu_txn(0x0, true, 0xC0DEBEEFu);
    }

    void setup_eventq(uint64_t base_addr, uint32_t log2size)
    {
        uint32_t lo = static_cast<uint32_t>(base_addr) | (log2size & smmuv3_bench_consts::QUEUE_LOG2_MASK);
        uint32_t hi = static_cast<uint32_t>(base_addr >> 32);
        mmio_write32(smmuv3_regs::EVENTQ_BASE_LO, lo);
        mmio_write32(smmuv3_regs::EVENTQ_BASE_HI, hi);
        mmio_write32(smmuv3_regs::EVENTQ_PROD, 0);
        mmio_write32(smmuv3_regs::EVENTQ_CONS, 0);
    }

    void setup_secure_cmdq(uint64_t base_addr, uint32_t log2size)
    {
        uint32_t lo = static_cast<uint32_t>(base_addr) | (log2size & smmuv3_bench_consts::QUEUE_LOG2_MASK);
        uint32_t hi = static_cast<uint32_t>(base_addr >> 32);
        mmio_write32(smmuv3_regs::S_CMDQ_BASE_LO, lo);
        mmio_write32(smmuv3_regs::S_CMDQ_BASE_HI, hi);
        mmio_write32(smmuv3_regs::S_CMDQ_PROD, 0);
        mmio_write32(smmuv3_regs::S_CMDQ_CONS, 0);
    }

    void enable_secure_cmdq()
    {
        mmio_write32(smmuv3_regs::S_CR0, mmio_read32(smmuv3_regs::S_CR0) | smmuv3_regs::CR0_CMDQEN);
        sc_core::wait(1, sc_core::SC_NS);
    }

    void attach_ats_tr(tlm::tlm_generic_payload& txn, uint32_t prg_index, gs::smmuv3_ats_extension** out = nullptr)
    {
        auto* ext = new gs::smmuv3_ats_extension();
        ext->is_translation_request = true;
        ext->is_translated = false;
        ext->prg_index = prg_index;
        txn.set_extension(ext);
        if (out) *out = ext;
    }

    void attach_ats_translated(tlm::tlm_generic_payload& txn)
    {
        auto* ext = new gs::smmuv3_ats_extension();
        ext->is_translation_request = false;
        ext->is_translated = true;
        txn.set_extension(ext);
    }

    void enable_eventq()
    {
        mmio_write32(smmuv3_regs::CR0, mmio_read32(smmuv3_regs::CR0) | smmuv3_regs::CR0_EVENTQEN);
        sc_core::wait(1, sc_core::SC_NS);
    }

    void read_event_record(uint64_t eventq_base, uint32_t idx, uint8_t* out)
    {
        read_dram(eventq_base + idx * smmuv3_bench_consts::EVENT_BYTES, out, smmuv3_bench_consts::EVENT_BYTES);
    }

    uint8_t event_type_at(uint64_t eventq_base, uint32_t idx)
    {
        std::array<uint8_t, smmuv3_bench_consts::EVENT_BYTES> rec{};
        read_event_record(eventq_base, idx, rec.data());
        return rec[0];
    }

    void setup_priq(uint64_t base_addr, uint32_t log2size)
    {
        uint32_t lo = static_cast<uint32_t>(base_addr) | (log2size & smmuv3_bench_consts::QUEUE_LOG2_MASK);
        uint32_t hi = static_cast<uint32_t>(base_addr >> 32);
        mmio_write32(smmuv3_regs::PRIQ_BASE_LO, lo);
        mmio_write32(smmuv3_regs::PRIQ_BASE_HI, hi);
        mmio_write32(smmuv3_regs::PRIQ_PROD, 0);
        mmio_write32(smmuv3_regs::PRIQ_CONS, 0);
    }

    void enable_priq()
    {
        mmio_write32(smmuv3_regs::CR0, mmio_read32(smmuv3_regs::CR0) | smmuv3_regs::CR0_PRIQEN);
        sc_core::wait(1, sc_core::SC_NS);
    }

    void enable_irqs() { mmio_write32(smmuv3_regs::IRQ_CTRL, smmuv3_regs::IRQ_CTRL_ALL); }

    uint32_t read_gerror() { return mmio_read32(smmuv3_regs::GERROR); }
    uint32_t read_gerrorn() { return mmio_read32(smmuv3_regs::GERRORN); }
    void write_gerrorn(uint32_t v) { mmio_write32(smmuv3_regs::GERRORN, v); }
};

struct iotlb_size_preset_ {
    iotlb_size_preset_(const std::string& bench_name, uint32_t sz)
    {
        cci::cci_get_global_broker(cci::cci_originator("iotlb_preset"))
            .set_preset_cci_value(bench_name + ".smmu.iotlb_size", cci::cci_value(sz));
    }
};

struct pamax_preset_ {
    pamax_preset_(const std::string& bench_name, uint32_t px)
    {
        cci::cci_get_global_broker(cci::cci_originator("pamax_preset"))
            .set_preset_cci_value(bench_name + ".smmu.pamax", cci::cci_value(px));
    }
};

struct iidr_preset_ {
    iidr_preset_(const std::string& bench_name, uint32_t v)
    {
        cci::cci_get_global_broker(cci::cci_originator("iidr_preset"))
            .set_preset_cci_value(bench_name + ".smmu.iidr", cci::cci_value(v));
    }
};

class smmuv3_bench_iidr_custom : private iidr_preset_, public smmuv3_bench
{
public:
    static constexpr uint32_t IIDR_OVERRIDE = 0xCAFEBABEu;
    smmuv3_bench_iidr_custom(const sc_core::sc_module_name& n)
        : iidr_preset_(static_cast<const char*>(n), IIDR_OVERRIDE), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_iotlb4 : private iotlb_size_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_iotlb4(const sc_core::sc_module_name& n)
        : iotlb_size_preset_(static_cast<const char*>(n), 4), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_pamax32 : private pamax_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_pamax32(const sc_core::sc_module_name& n)
        : pamax_preset_(static_cast<const char*>(n), 32), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_pamax36 : private pamax_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_pamax36(const sc_core::sc_module_name& n)
        : pamax_preset_(static_cast<const char*>(n), 36), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_pamax40 : private pamax_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_pamax40(const sc_core::sc_module_name& n)
        : pamax_preset_(static_cast<const char*>(n), 40), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_pamax42 : private pamax_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_pamax42(const sc_core::sc_module_name& n)
        : pamax_preset_(static_cast<const char*>(n), 42), smmuv3_bench(n)
    {
    }
};

class smmuv3_bench_pamax44 : private pamax_preset_, public smmuv3_bench
{
public:
    smmuv3_bench_pamax44(const sc_core::sc_module_name& n)
        : pamax_preset_(static_cast<const char*>(n), 44), smmuv3_bench(n)
    {
    }
};

#endif
