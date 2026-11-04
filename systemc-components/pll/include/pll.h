/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <scp/report.h>
#include <tlm_utils/simple_target_socket.h>

#include <fstream>
#include <systemc>
#include <tlm>
#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>

namespace gs {

#define SA8540_QDSP6SS_CORE_CMD_RCGR 0x1020
#define SA8540_QDSP6SS_CORE_CFG_RCGR 0x1024

#define SA8775_QDSP6SS_CORE_CMD_RCGR 0x8020
#define SA8775_QDSP6SS_CORE_CFG_RCGR 0x8024
#define SA8775_QDSP6SS_CORE_CBCR     0x8040

#define SA8775_QDSP6SS_CMD_DIRTY_CFG 0x10
#define SA8775_QDSP6SS_CMD_UPDATE    0x01
#define SA8775_QDSP6SS_CBCR_CLK_OFF  0x80000000
#define SA8775_QDSP6SS_CBCR_CLK_EN   0x00000001

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class pll : public sc_core::sc_module
{
    uint32_t regs[0x10000] = {};

protected:
    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        unsigned int len = txn.get_data_length();
        unsigned char* ptr = txn.get_data_ptr();
        sc_dt::uint64 addr = txn.get_address();

        if (txn.get_streaming_width() < len) {
            SCP_ERR(SCMOD) << "not supported.";
        }

        /* Boot firmware leaves the Q6 Lucid-Evo PLL locked and enabled. */
        regs[0x0] |= 0x80000005;
        regs[0x4000] = 0;

        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND:
            *(uint32_t*)ptr = regs[addr];
            break;
        case tlm::TLM_WRITE_COMMAND:
            regs[addr] = (*(uint32_t*)ptr);
            if ((addr & 0xffff) == 0) {
                /* Model the Lucid-Evo asynchronous latch handshake. */
                if (regs[0] & 0x4000) {
                    regs[0] |= 0x2000;
                } else {
                    regs[0] &= ~0x2000;
                }
                regs[0] |= 0x80000005;
            }
            if ((addr & 0xffff) == SA8540_QDSP6SS_CORE_CFG_RCGR) {
                regs[SA8540_QDSP6SS_CORE_CMD_RCGR] |= 0b100;
            }
            if (((addr & 0xffff) == SA8540_QDSP6SS_CORE_CMD_RCGR) && (regs[SA8540_QDSP6SS_CORE_CMD_RCGR] & 0b1)) {
                regs[SA8540_QDSP6SS_CORE_CMD_RCGR] &= ~0b101;
            }
            if ((addr & 0xffff) == SA8775_QDSP6SS_CORE_CFG_RCGR) {
                regs[SA8775_QDSP6SS_CORE_CMD_RCGR] |= SA8775_QDSP6SS_CMD_DIRTY_CFG;
            }
            if (((addr & 0xffff) == SA8775_QDSP6SS_CORE_CMD_RCGR) &&
                (regs[SA8775_QDSP6SS_CORE_CMD_RCGR] & SA8775_QDSP6SS_CMD_UPDATE)) {
                regs[SA8775_QDSP6SS_CORE_CMD_RCGR] &= ~(SA8775_QDSP6SS_CMD_DIRTY_CFG | SA8775_QDSP6SS_CMD_UPDATE);
            }
            if ((addr & 0xffff) == SA8775_QDSP6SS_CORE_CBCR) {
                if (regs[SA8775_QDSP6SS_CORE_CBCR] & SA8775_QDSP6SS_CBCR_CLK_EN) {
                    regs[SA8775_QDSP6SS_CORE_CBCR] &= ~SA8775_QDSP6SS_CBCR_CLK_OFF;
                } else {
                    regs[SA8775_QDSP6SS_CORE_CBCR] |= SA8775_QDSP6SS_CBCR_CLK_OFF;
                }
            }
            break;
        default:
            break;
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);

        txn.set_dmi_allowed(false);
    }

public:
    tlm_utils::simple_target_socket<pll, BUSWIDTH> socket;

    pll(sc_core::sc_module_name name): socket("socket") { socket.register_b_transport(this, &pll::b_transport); }
};
} // namespace gs
extern "C" void module_register();
