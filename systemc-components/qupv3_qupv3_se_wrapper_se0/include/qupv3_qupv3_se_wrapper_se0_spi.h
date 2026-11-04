/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef qupv3_qupv3_se_wrapper_se0_spi_H
#define qupv3_qupv3_se_wrapper_se0_spi_H

#include "qupv3_qupv3_se_wrapper_se0.h"

namespace quic {
class qupv3_qupv3_se_wrapper_se0_spi : public qupv3_qupv3_se_wrapper_se0, public qupv3_qupv3_se_wrapper_se0_gen_spi
{
    cci::cci_param<bool> p_master; // true for master, false for slave

    uint32_t bytes_in_fifo = 0;
    uint32_t rx_trans_len = 0;
    uint32_t tx_trans_len = 0;

    void update_irq() override;
    void receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t) override;
    void update_RX_FIFO_STATUS();

public:
    qupv3_qupv3_se_wrapper_se0_spi(sc_core::sc_module_name _name);
    void start_of_simulation() override;
    void end_of_elaboration() override;
    void do_reset(bool value) override;
};
} // namespace quic
#endif
