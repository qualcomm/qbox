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

INCBIN(ZipArchive_smmu500_, __FILE__ "_config.zip");

#include "smmu500.h"

namespace gs {

template <unsigned int BUSWIDTH>
smmu500<BUSWIDTH>::smmu500(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(gs::zip_open_from_memory(gZipArchive_smmu500_Data, gZipArchive_smmu500_Size))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".smmu500"))
    , M("smmu500", m_jza)
    , p_pamax("pamax", 48, "")
    , p_num_smr("num_smr", 48, "")
    , p_num_cb("num_cb", 16, "")
    , p_num_pages("num_pages", 16, "")
    , p_ato("ato", true, "")
    , p_version("version", 0x21, "")
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

    socket.bind(M.target_socket);
    reset.register_value_changed_cb([&](bool value) {
        if (value) {
            SCP_WARN(()) << "Reset";
            M.reset(value);
            start_of_simulation();
        }
    });
}

template <unsigned int BUSWIDTH>
void smmu500<BUSWIDTH>::start_of_simulation()
{
    unsigned int num_pages_log2 = 31 - clz32(p_num_pages);
    SIDR0_ATOSNS = (uint32_t)p_ato;
    SIDR0_NUMSMRG = (uint32_t)p_num_smr;
    SIDR1_NUMCB = (uint32_t)p_num_cb;
    SIDR1_NUMPAGENDXB = num_pages_log2 - 1;
    SCR1_NSNUMCBO = (uint32_t)p_num_cb;
    SCR1_NSNUMSMRGO = (uint32_t)p_num_smr;
    SMMU_SIDR7 = (uint32_t)p_version;
    SMMU_TBU_PWR_STATUS = (1u << (uint32_t)p_num_tbu) - 1;
}

template <unsigned int BUSWIDTH>
void smmu500<BUSWIDTH>::before_end_of_elaboration()
{
    SCP_TRACE(())("Before End of Elaboration, registering callbacks");

    /* GATS callbacks - triggered on H register write */
    SMMU_GATS1PR_H.post_write([this](TXN(txn)) {
        uint64_t val = ((uint64_t)(uint32_t)SMMU_GATS1PR_H << 32) | (uint32_t)SMMU_GATS1PR;
        smmu500_gat(val, false, false);
    });
    SMMU_GATS1PW_H.post_write([this](TXN(txn)) {
        uint64_t val = ((uint64_t)(uint32_t)SMMU_GATS1PW_H << 32) | (uint32_t)SMMU_GATS1PW;
        smmu500_gat(val, true, false);
    });
    SMMU_GATS12PR_H.post_write([this](TXN(txn)) {
        uint64_t val = ((uint64_t)(uint32_t)SMMU_GATS12PR_H << 32) | (uint32_t)SMMU_GATS12PR;
        smmu500_gat(val, false, true);
    });
    SMMU_GATS12PW_H.post_write([this](TXN(txn)) {
        uint64_t val = ((uint64_t)(uint32_t)SMMU_GATS12PW_H << 32) | (uint32_t)SMMU_GATS12PW;
        smmu500_gat(val, true, true);
    });

    /* NSCR0 post_write - sync to SCR0 */
    SMMU_NSCR0.post_write([this](TXN(txn)) { SMMU_SCR0 = (uint32_t)SMMU_NSCR0; });

    /* Per-CB callbacks */
    /* FSR post_write - update context IRQs for all CBs */
    SMMU_CB_FSR.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_FSR.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        for (unsigned int i = 0; i < p_num_cb; i++) smmu500_update_ctx_irq(i);
    });

    /* TLBIASID post_write - TLB flush by value */
    SMMU_CB_TLBIASID.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBIASID.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        uint32_t val = *(uint32_t*)txn.get_data_ptr();
        for (auto tbu : tbus) tbu->start_invalidates();
        for (auto tbu : tbus) tbu->invalidate(val);
        for (auto tbu : tbus) tbu->stop_invalidates();
    });

    /* TLBIALL post_write - TLB flush all for this CB */
    SMMU_CB_TLBIALL.post_write([this](TXN(txn)) {
        const auto access = SMMU_CB_TLBIALL.decode_access(txn);
        if (!access || access.indices.size() != 1 || access.indices[0] >= p_num_cb) return;
        const unsigned int cb = static_cast<unsigned int>(access.indices[0]);
        SCP_DEBUG(()) << "TLBIALL write for CB" << cb;
        for (auto tbu : tbus) tbu->start_invalidates();
        for (auto tbu : tbus) tbu->invalidate(cb);
        for (auto tbu : tbus) tbu->stop_invalidates();
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
