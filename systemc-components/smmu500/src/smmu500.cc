/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>
#include <zip_loader.h>

#include <cstring>

INCBIN(ZipArchive_smmu500_, __FILE__ "_config.zip");

#include "smmu500.h"

namespace {

uint32_t transaction_data_u32(const tlm::tlm_generic_payload& txn)
{
    assert(txn.get_data_ptr() != nullptr);
    assert(txn.get_data_length() >= sizeof(uint32_t));

    uint32_t value = 0;
    std::memcpy(&value, txn.get_data_ptr(), sizeof(value));
    return value;
}

} // namespace

namespace gs {

template <unsigned int BUSWIDTH>
smmu500<BUSWIDTH>::smmu500(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(gs::zip_open_from_memory(gZipArchive_smmu500_Data, gZipArchive_smmu500_Size))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".smmu500"))
    , M("smmu500", m_jza)
    , m_register_socket("register_socket")
    , p_pamax("pamax", 48, "")
    , p_num_smr("num_smr", 224, "")
    , p_num_cb("num_cb", 16, "")
    , p_num_pages("num_pages", 16, "")
    , p_ato("ato", true, "")
    , p_version("version", 0x21, "")
    , p_idr2("idr2", 0x7111, "")
    , p_num_tbu("num_tbu", 1, "")
    , socket("target_socket")
    , dma_socket("dma")
    , irq_global("irq_global")
    , irq_context("irq_context", p_num_cb,
                  [this](const char* n, size_t i) { return new InitiatorSignalSocket<bool>(n); })
    , reset("reset")
{
    SCP_TRACE(())("Constructor");
    sc_assert(loaded_ok);
    set_cb_bank_base(static_cast<uint64_t>(p_num_pages) * SMMU_PAGESIZE);
    bind_regs(M);

    socket.register_b_transport(this, &smmu500::smmu500_register_b_transport);
    socket.register_transport_dbg(this, &smmu500::smmu500_register_transport_dbg);
    socket.register_get_direct_mem_ptr(this, &smmu500::smmu500_register_get_direct_mem_ptr);
    m_register_socket.bind(M.target_socket);
    reset.register_value_changed_cb([&](bool value) {
        if (value) {
            SCP_WARN(()) << "Reset";
            for (auto tbu : tbus) tbu->reset_dmi();
            /* DMI requests can invoke translation from a QEMU thread. Reset
             * the register model under the same lock used by translation. Do
             * this after reset_dmi(): a DMI request takes the TBU lock before
             * this lock, so reversing that order can deadlock. */
            std::lock_guard<std::recursive_mutex> translation_lock(m_translation_lock);
            M.reset(value);
            start_of_simulation();
        }
    });
}

template <unsigned int BUSWIDTH>
void smmu500<BUSWIDTH>::start_of_simulation()
{
    sc_assert(p_num_smr <= SMMU_MAX_SMR);
    sc_assert(p_num_cb <= SMMU_MAX_CB);
    smmu500_program_id_registers();
    SCP_DEBUG(()) << "SMMU register DMI disabled; security-aware MMIO active";
    m_secure_gr0_shadow.fill(0);
    m_non_secure_gr0_shadow.fill(0);
    const uint32_t sacr = static_cast<uint32_t>(SMMU_SACR);
    std::memcpy(m_non_secure_gr0_shadow.data() + 0x010, &sacr, sizeof(sacr));
    SCR1_NSNUMCBO = static_cast<uint32_t>(p_num_cb);
    SCR1_NSNUMSMRGO = static_cast<uint32_t>(p_num_smr);
    SCP_DEBUG(()) << "SMMU configured SMR count " << static_cast<unsigned int>(p_num_smr)
                 << ", SIDR0.NUMSMRG 0x" << std::hex << static_cast<uint32_t>(SIDR0_NUMSMRG);
    SMMU_TBU_PWR_STATUS = (1u << static_cast<uint32_t>(p_num_tbu)) - 1;
    smmu500_update_global_irq();
    for (unsigned int cb = 0; cb < p_num_cb; ++cb) smmu500_update_ctx_irq(cb);
}

template <unsigned int BUSWIDTH>
void smmu500<BUSWIDTH>::before_end_of_elaboration()
{
    SCP_TRACE(())("Before End of Elaboration, registering callbacks");

    /* GATS callbacks - triggered on H register write */
    SMMU_GATS1PR_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS1PR_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS1PR);
        smmu500_gat(val, false, false, false);
    });
    SMMU_GATS1PW_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS1PW_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS1PW);
        smmu500_gat(val, true, false, false);
    });
    SMMU_GATS1UR_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS1UR_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS1UR);
        smmu500_gat(val, false, false, true);
    });
    SMMU_GATS1UW_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS1UW_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS1UW);
        smmu500_gat(val, true, false, true);
    });
    SMMU_GATS12PR_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS12PR_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS12PR);
        smmu500_gat(val, false, true, false);
    });
    SMMU_GATS12PW_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS12PW_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS12PW);
        smmu500_gat(val, true, true, false);
    });
    SMMU_GATS12UR_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS12UR_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS12UR);
        smmu500_gat(val, false, true, true);
    });
    SMMU_GATS12UW_H.post_write([this](TXN(txn)) {
        uint64_t val = (static_cast<uint64_t>(static_cast<uint32_t>(SMMU_GATS12UW_H)) << 32) |
                       static_cast<uint32_t>(SMMU_GATS12UW);
        smmu500_gat(val, true, true, true);
    });

    /* Global control/status callbacks */
    SMMU_SCR0.post_write([this](TXN(txn)) {
        smmu500_invalidate_all();
        smmu500_update_global_irq();
    });
    SMMU_NSCR0.post_write([this](TXN(txn)) {
        smmu500_invalidate_all();
        smmu500_update_global_irq();
    });
    SMMU_SGFSR.pre_write([this](TXN(txn)) { m_sgfsr_before_write = static_cast<uint32_t>(SMMU_SGFSR); });
    SMMU_SGFSR.post_write([this](TXN(txn)) {
        const uint32_t written = transaction_data_u32(txn);
        SMMU_SGFSR = m_sgfsr_before_write & ~(written & GLOBAL_FAULT_MASK);
        smmu500_update_global_irq();
    });
    SMMU_NSGFSR.pre_write([this](TXN(txn)) { m_nsgfsr_before_write = static_cast<uint32_t>(SMMU_NSGFSR); });
    SMMU_NSGFSR.post_write([this](TXN(txn)) {
        const uint32_t written = transaction_data_u32(txn);
        SMMU_NSGFSR = m_nsgfsr_before_write & ~(written & GLOBAL_FAULT_MASK);
        smmu500_update_global_irq();
    });

    SMMU_SIDR0.post_write([this](TXN(txn)) { smmu500_program_id_registers(); });
    SMMU_SIDR1.post_write([this](TXN(txn)) { smmu500_program_id_registers(); });
    SMMU_SIDR2.post_write([this](TXN(txn)) { smmu500_program_id_registers(); });
    SMMU_SIDR7.post_write([this](TXN(txn)) { smmu500_program_id_registers(); });

    auto global_tlbi_all = [this](TXN(txn)) {
        smmu500_invalidate_all();
        smmu500_clear_global_tlbi_regs();
    };
    SMMU_STLBIALL.post_write(global_tlbi_all);
    SMMU_TLBIALLNSNH.post_write(global_tlbi_all);
    SMMU_TLBIALLH.post_write(global_tlbi_all);
    SMMU_TLBIVAH_LOW.post_write(global_tlbi_all);
    SMMU_STLBIVALM_LOW.post_write(global_tlbi_all);
    SMMU_STLBIVALM_HIGH.post_write(global_tlbi_all);
    SMMU_STLBIVAM_LOW.post_write(global_tlbi_all);
    SMMU_STLBIVAM_HIGH.post_write(global_tlbi_all);
    SMMU_TLBIVALH64_LOW.post_write(global_tlbi_all);
    SMMU_TLBIVALH64_HIGH.post_write(global_tlbi_all);
    SMMU_STLBIALLM.post_write(global_tlbi_all);
    SMMU_TLBIVAH64_LOW.post_write(global_tlbi_all);
    SMMU_TLBIVAH64_HIGH.post_write(global_tlbi_all);
    SMMU_TLBIVMID.post_write([this](TXN(txn)) {
        const uint32_t vmid = transaction_data_u32(txn) & 0xffu;
        smmu500_invalidate_vmid(vmid);
        smmu500_clear_global_tlbi_regs();
    });
    SMMU_TLBIVMIDS1.post_write([this](TXN(txn)) {
        const uint32_t vmid = transaction_data_u32(txn) & 0xffu;
        smmu500_invalidate_vmid(vmid);
        smmu500_clear_global_tlbi_regs();
    });

    /* Per-CB callbacks */
    SMMU_CB_SCTLR.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_SCTLR.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        smmu500_update_ctx_irq(static_cast<unsigned int>(access.indices[0]));
    });

    /* FSR is write-one-to-clear; retain the fault status until software clears it. */
    SMMU_CB_FSR.pre_write([this](TXN(txn)) {
        const auto access = SMMU_CB_FSR.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        m_cb_fsr_before_write[access.indices[0]] = static_cast<uint32_t>(SMMU_CB_FSR[access.indices[0]]);
    });

    /* FSR post_write - apply W1C and update context IRQs for all CBs */
    SMMU_CB_FSR.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_FSR.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        const uint32_t written = transaction_data_u32(txn);
        constexpr uint32_t status_mask = CB_FSR_FAULT_MASK | CB_FSR_MULTI_MASK;
        SMMU_CB_FSR[cb] = m_cb_fsr_before_write[cb] & ~(written & status_mask);
        for (unsigned int i = 0; i < p_num_cb; i++) smmu500_update_ctx_irq(i);
    });

    /* TLBIASID post_write - TLB flush by value */
    SMMU_CB_TLBIASID.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBIASID.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        // DMI views are tracked per context bank, not per ASID. This model has
        // no ASID-tagged TLB cache, so invalidating the addressed bank is the
        // conservative equivalent of a TLBIASID operation.
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        smmu500_invalidate_cb(cb);
        smmu500_clear_cb_tlbi_regs(cb);
    });

    /* TLBIALL post_write - TLB flush all for this CB */
    SMMU_CB_TLBIALL.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBIALL.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        SCP_DEBUG(()) << "TLBIALL write for CB" << cb;
        smmu500_invalidate_cb(cb);
        smmu500_clear_cb_tlbi_regs(cb);
    });

    auto cb_tlbi = [this](auto& reg, TXN(txn)) {
        const auto access = reg.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        smmu500_invalidate_cb(cb);
        smmu500_clear_cb_tlbi_regs(cb);
    };
    SMMU_CB_TLBIVA_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVA_LOW, txn, delay); });
    SMMU_CB_TLBIVA_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVA_HIGH, txn, delay); });
    SMMU_CB_TLBIVAA_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAA_LOW, txn, delay); });
    SMMU_CB_TLBIVAA_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAA_HIGH, txn, delay); });
    SMMU_CB_TLBIVAL_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAL_LOW, txn, delay); });
    SMMU_CB_TLBIVAL_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAL_HIGH, txn, delay); });
    SMMU_CB_TLBIVAAL_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAAL_LOW, txn, delay); });
    SMMU_CB_TLBIVAAL_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIVAAL_HIGH, txn, delay); });
    SMMU_CB_TLBIIPAS2_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIIPAS2_LOW, txn, delay); });
    SMMU_CB_TLBIIPAS2_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIIPAS2_HIGH, txn, delay); });
    SMMU_CB_TLBIIPAS2L_LOW.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIIPAS2L_LOW, txn, delay); });
    SMMU_CB_TLBIIPAS2L_HIGH.post_write([this, cb_tlbi](TXN(txn)) { cb_tlbi(SMMU_CB_TLBIIPAS2L_HIGH, txn, delay); });
    SMMU_CB_TLBSYNC.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBSYNC.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        SMMU_CB_TLBSYNC[cb] = 0;
        SMMU_CB_TLBSTATUS[cb] = 0;
    });
    SMMU_CB_TLBSTATUS.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBSTATUS.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        SMMU_CB_TLBSTATUS[access.indices[0]] = 0;
    });

}

template class smmu500<32>;

} // namespace gs

typedef gs::smmu500<> smmu500;
typedef gs::smmu500_tbu<> smmu500_tbu;

void module_register()
{
    GSC_MODULE_REGISTER_C(smmu500);
    GSC_MODULE_REGISTER_C(smmu500_tbu, sc_core::sc_object*);
}
