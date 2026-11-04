/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef qupv3_qupv3_se_wrapper_se0_i2c_H
#define qupv3_qupv3_se_wrapper_se0_i2c_H

#include "qupv3_qupv3_se_wrapper_se0.h"

namespace quic {
class qupv3_qupv3_se_wrapper_se0_i2c : public qupv3_qupv3_se_wrapper_se0, public qupv3_qupv3_se_wrapper_se0_gen_i2c
{
    uint32_t bytes_in_fifo = 0;
    uint32_t tx_trans_len = 0;
    uint32_t rx_trans_len = 0;      // Decremented once SW reads the byte in GENI_RX_FIFO.post_read
    int32_t rx_remaining_bytes = 0; // Decremented in receive method

    struct Configure {
        bool opcode_set;
        bool tx_len_set;
        bool rx_len_set;
        void reset()
        {
            opcode_set = false;
            tx_len_set = false;
            rx_len_set = false;
        }
    };
    Configure config; // only send transaction if opcode and trans_len are configured, set to false again once the
                      // send_txn is called

    void update_irq() override;
    void receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t) override;
    void update_RX_FIFO_STATUS();
    void send_txn();

public:
    qupv3_qupv3_se_wrapper_se0_i2c(sc_core::sc_module_name _name);
    void start_of_simulation() override;
    void end_of_elaboration() override;
    void do_reset(bool value) override;
};
} // namespace quic
#endif
