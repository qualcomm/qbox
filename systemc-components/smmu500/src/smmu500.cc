/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_smmu500_, __FILE__ "_config.zip");

#include "smmu500.h"

namespace gs {

static std::string make_cb_reg_name(const std::string& prefix, int cb) { return prefix + std::to_string(cb); }

static std::string make_cb_reg_path(const std::string& prefix, int cb)
{
    return "smmu500." + prefix + std::to_string(cb);
}

template <unsigned int BUSWIDTH>
smmu500<BUSWIDTH>::smmu500(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(
          zip_open_from_source(zip_source_buffer_create(gZipArchive_smmu500_Data, gZipArchive_smmu500_Size, 0, nullptr),
                               ZIP_RDONLY, nullptr))
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
    bind_regs(M);

    /* Create per-CB page registers */
    for (int cb = 0; cb < p_num_cb; cb++) {
        uint32_t cb_base = (p_num_pages + cb) * SMMU_PAGESIZE;

#define MAKE_CB_REG(vec, suffix, offset)                                                                           \
    do {                                                                                                           \
        auto rn = make_cb_reg_name("SMMU_CB_" suffix, cb);                                                         \
        auto rp = make_cb_reg_path("SMMU_CB_" suffix, cb);                                                         \
        vec.push_back(std::make_shared<gs::gs_register<uint32_t>>(rn.c_str(), rp.c_str(), cb_base + (offset), 1)); \
        M.bind_reg(*vec.back());                                                                                   \
    } while (0)

        MAKE_CB_REG(SMMU_CB_SCTLR, "SCTLR", 0x0);
        MAKE_CB_REG(SMMU_CB_ACTLR, "ACTLR", 0x4);
        MAKE_CB_REG(SMMU_CB_RESUME, "RESUME", 0x8);
        MAKE_CB_REG(SMMU_CB_TCR2, "TCR2", 0x10);
        MAKE_CB_REG(SMMU_CB_TTBR0_LOW, "TTBR0_LOW", 0x20);
        MAKE_CB_REG(SMMU_CB_TTBR0_HIGH, "TTBR0_HIGH", 0x24);
        MAKE_CB_REG(SMMU_CB_TTBR1_LOW, "TTBR1_LOW", 0x28);
        MAKE_CB_REG(SMMU_CB_TTBR1_HIGH, "TTBR1_HIGH", 0x2c);
        MAKE_CB_REG(SMMU_CB_TCR_LPAE, "TCR_LPAE", 0x30);
        MAKE_CB_REG(SMMU_CB_CONTEXTIDR, "CONTEXTIDR", 0x34);
        MAKE_CB_REG(SMMU_CB_PRRR_MAIR0, "PRRR_MAIR0", 0x38);
        MAKE_CB_REG(SMMU_CB_NMRR_MAIR1, "NMRR_MAIR1", 0x3c);
        MAKE_CB_REG(SMMU_CB_FSR, "FSR", 0x58);
        MAKE_CB_REG(SMMU_CB_FSRRESTORE, "FSRRESTORE", 0x5c);
        MAKE_CB_REG(SMMU_CB_FAR_LOW, "FAR_LOW", 0x60);
        MAKE_CB_REG(SMMU_CB_FAR_HIGH, "FAR_HIGH", 0x64);
        MAKE_CB_REG(SMMU_CB_FSYNR0, "FSYNR0", 0x68);
        MAKE_CB_REG(SMMU_CB_IPAFAR_LOW, "IPAFAR_LOW", 0x70);
        MAKE_CB_REG(SMMU_CB_IPAFAR_HIGH, "IPAFAR_HIGH", 0x74);
        MAKE_CB_REG(SMMU_CB_TLBIASID, "TLBIASID", 0x610);
        MAKE_CB_REG(SMMU_CB_TLBIALL, "TLBIALL", 0x618);
        MAKE_CB_REG(SMMU_CB_TLBSYNC, "TLBSYNC", 0x7f0);
        MAKE_CB_REG(SMMU_CB_TLBSTATUS, "TLBSTATUS", 0x7f4);

#undef MAKE_CB_REG
    }

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
    for (int cb = 0; cb < p_num_cb; cb++) {
        /* FSR post_write - update context IRQs for all CBs */
        SMMU_CB_FSR[cb]->post_write([this](TXN(txn)) {
            for (unsigned int i = 0; i < p_num_cb; i++) smmu500_update_ctx_irq(i);
        });

        /* TLBIASID post_write - TLB flush by value */
        SMMU_CB_TLBIASID[cb]->post_write([this](TXN(txn)) {
            uint32_t val = *(uint32_t*)txn.get_data_ptr();
            for (auto tbu : tbus) {
                tbu->start_invalidates();
            }
            for (auto tbu : tbus) {
                tbu->invalidate(val);
            }
            for (auto tbu : tbus) {
                tbu->stop_invalidates();
            }
        });

        /* TLBIALL post_write - TLB flush all for this CB */
        SMMU_CB_TLBIALL[cb]->post_write([this, cb](TXN(txn)) {
            SCP_DEBUG(()) << "TLBIALL write for CB" << cb;
            for (auto tbu : tbus) {
                tbu->start_invalidates();
            }
            for (auto tbu : tbus) {
                tbu->invalidate(cb);
            }
            for (auto tbu : tbus) {
                tbu->stop_invalidates();
            }
        });
    }
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
