/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_rsc_, __FILE__ "_config.zip");

#include "rsc.h"
#include <zip.h>

namespace quic {

rsc::rsc(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(
          zip_source_buffer_create(gZipArchive_rsc_Data, gZipArchive_rsc_Size, 0, nullptr), ZIP_RDONLY,
          nullptr))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".apss_rsc_apps_rscc"))
    , M("apss_rsc_apps_rscc", m_jza)
    , target_socket("target_socket")
    , reset("reset")
    , irq("irq", NUM_DRV)
{
    SCP_TRACE(())("Constructor");
    sc_assert(loaded_ok);
    bind_regs(M);

    m_drv[0] = {&TCS_DRV0_CONTROL, &TCS_DRV0_STATUS, &TCS_DRV0_CMD_ENABLE, &TCS_CMD_DRV0_STATUS,
                &TCS_CMD_DRV0_READ_RESPONSE_DATA, &TCS_AMC_MODE_IRQ_ENABLE_DRV0, &TCS_AMC_MODE_IRQ_STATUS_DRV0,
                &TCS_AMC_MODE_IRQ_CLEAR_DRV0, 4};
    m_drv[1] = {&TCS_DRV1_CONTROL, &TCS_DRV1_STATUS, &TCS_DRV1_CMD_ENABLE, &TCS_CMD_DRV1_STATUS,
                &TCS_CMD_DRV1_READ_RESPONSE_DATA, &TCS_AMC_MODE_IRQ_ENABLE_DRV1, &TCS_AMC_MODE_IRQ_STATUS_DRV1,
                &TCS_AMC_MODE_IRQ_CLEAR_DRV1, 4};
    m_drv[2] = {&TCS_DRV2_CONTROL, &TCS_DRV2_STATUS, &TCS_DRV2_CMD_ENABLE, &TCS_CMD_DRV2_STATUS,
                &TCS_CMD_DRV2_READ_RESPONSE_DATA, &TCS_AMC_MODE_IRQ_ENABLE_DRV2, &TCS_AMC_MODE_IRQ_STATUS_DRV2,
                &TCS_AMC_MODE_IRQ_CLEAR_DRV2, 8};
    m_drv[3] = {&TCS_DRV3_CONTROL, &TCS_DRV3_STATUS, &TCS_DRV3_CMD_ENABLE, &TCS_CMD_DRV3_STATUS,
                &TCS_CMD_DRV3_READ_RESPONSE_DATA, &TCS_AMC_MODE_IRQ_ENABLE_DRV3, &TCS_AMC_MODE_IRQ_STATUS_DRV3,
                &TCS_AMC_MODE_IRQ_CLEAR_DRV3, 3};

    target_socket.bind(M.target_socket);
    reset.register_value_changed_cb([&](bool value) {
        if (value) {
            SCP_WARN(()) << "Reset";
        }
        M.reset(value);
        if (!value) {
            for (uint32_t d = 0; d < NUM_DRV; d++) {
                update_irq(d);
            }
        }
    });

    SC_METHOD(set_irq);
    sensitive << set_irq_ev;
    dont_initialize();
    SC_METHOD(clear_irq);
    sensitive << clear_irq_ev;
    dont_initialize();
}

void rsc::before_end_of_elaboration()
{
    for (uint32_t d = 0; d < NUM_DRV; d++) {
        reg_bank* control = m_drv[d].control;
        control->post_write([this, d, control](TXN(txn)) {
            const auto access = control->decode_access(txn);
            if (!access) return;
            const uint32_t tcs = access.indices[0];
            const uint32_t ctl = static_cast<uint32_t>((*control)[tcs]);
            if ((ctl & AMC_MODE_EN) && (ctl & AMC_MODE_TRIGGER)) {
                trigger_tcs(d, tcs);
            }
        });

        // AMC completion IRQ clear: write-1-to-clear of the status bits.
        gs::gs_register<uint32_t>* clr = m_drv[d].irq_clear;
        clr->post_write([this, d, clr](TXN()) {
            const uint32_t w = static_cast<uint32_t>(*clr);
            *m_drv[d].irq_status = static_cast<uint32_t>(*m_drv[d].irq_status) & ~w;
            *clr = 0;
            clear_irq_ev.notify(sc_core::SC_ZERO_TIME);
        });

        gs::gs_register<uint32_t>* en = m_drv[d].irq_enable;
        en->post_write([this, d](TXN()) { update_irq(d); });
    }
}

void rsc::trigger_tcs(uint32_t drv, uint32_t tcs)
{
    SCP_DEBUG(()) << "Trigger drv=" << drv << " tcs=" << tcs;
    drv_view& v = m_drv[drv];
    if (tcs >= v.num_tcs) return;

    const uint32_t cmd_en = static_cast<uint32_t>((*v.cmd_enable)[tcs]);
    (*v.status)[tcs] = static_cast<uint32_t>((*v.status)[tcs]) & ~TCS_STATUS_IDLE;
    for (uint32_t n = 0; n < CMDS_PER_TCS; n++) {
        if (!(cmd_en & (1u << n))) continue;
        (*v.cmd_status)(tcs, n) = CMD_STATUS_DONE;
        (*v.cmd_read_resp)(tcs, n) = 0;
    }
    (*v.status)[tcs] = TCS_STATUS_IDLE;
    *v.irq_status = static_cast<uint32_t>(*v.irq_status) | (1u << tcs);
    set_irq_ev.notify(sc_core::SC_ZERO_TIME);
}

void rsc::update_irq(uint32_t drv)
{
    set_irq_ev.notify(sc_core::SC_ZERO_TIME);
    clear_irq_ev.notify(sc_core::SC_ZERO_TIME);
    (void)drv;
}

void rsc::set_irq()
{
    for (uint32_t d = 0; d < NUM_DRV; d++) {
        if (irq[d].size() == 0) continue;
        const uint32_t pending = static_cast<uint32_t>(*m_drv[d].irq_status) & static_cast<uint32_t>(*m_drv[d].irq_enable);
        if (pending) {
            SCP_DEBUG(()) << "IRQ Assert drv=" << d;
            irq[d]->write(true);
        }
    }
}

void rsc::clear_irq()
{
    for (uint32_t d = 0; d < NUM_DRV; d++) {
        if (irq[d].size() == 0) continue;
        const uint32_t pending = static_cast<uint32_t>(*m_drv[d].irq_status) & static_cast<uint32_t>(*m_drv[d].irq_enable);
        if (!pending) {
            SCP_DEBUG(()) << "IRQ Clear drv=" << d;
            irq[d]->write(false);
        }
    }
}

} // namespace quic

typedef quic::rsc rsc;

void module_register() { GSC_MODULE_REGISTER_C(rsc); }
