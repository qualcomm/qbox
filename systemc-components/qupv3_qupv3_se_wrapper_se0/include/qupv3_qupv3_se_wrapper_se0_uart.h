/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef qupv3_qupv3_se_wrapper_se0_uart_H
#define qupv3_qupv3_se_wrapper_se0_uart_H

#include "qupv3_qupv3_se_wrapper_se0.h"

namespace quic {

class qupv3_qupv3_se_wrapper_se0_uart : public qupv3_qupv3_se_wrapper_se0, public qupv3_qupv3_se_wrapper_se0_gen_uart
{
    uint32_t bytes_in_fifo = 0;
    bool dma_enabled = false;
    bool dma_tx_address_is_vaild = false;
    uint64_t dma_tx_address = 0;
    bool dma_rx_address_is_vaild = false;
    uint64_t dma_rx_address = 0;

    void update_irq() override;
    void fifo_mode_receive(tlm::tlm_generic_payload& txn);
    void dma_mode_receive(tlm::tlm_generic_payload& txn);
    void receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t) override;
    void set_rx_fifo_status();

public:
    qupv3_qupv3_se_wrapper_se0_uart(sc_core::sc_module_name _name);
    void start_of_simulation() override;
    void end_of_elaboration() override;
    void do_reset(bool value) override;
};
} // namespace quic

#endif
