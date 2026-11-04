/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @brief Configuring I2C on Linux
 *
 * Follow these steps to configure I2C communication:
 *
 * 1. Add a node in the device tree for I2C, as shown below:
 *
 *    ```
 *    geniqup@ac0000 {
 *        compatible = "qcom,geni-se-qup";
 *        reg = <0 0x00ac0000 0 0x6000>;
 *        clock-names = "m-ahb", "s-ahb";
 *        clocks = <0x8000>, <0x8000>;
 *        #address-cells = <2>;
 *        #size-cells = <2>;
 *        ranges;
 *        status = "okay";
 *
 *        i2c@a88000 {
 *            compatible = "qcom,geni-i2c";
 *            reg = <0x0 0xa88000 0x0 0x4000>;
 *            #address-cells = <1>;
 *            #size-cells = <0>;
 *            interrupts = <0x00 0x163 0x04>;
 *            clock-names = "se";
 *            clocks = <0x8000>;
 *            status = "okay";
 *        };
 *    };
 *    ```
 *
 * 2. In `conf.lua`, set the module type and slave_addr:
 *
 *    Example:
 *      {irq = 586, backend = py_i2c_backend{"i2c", 4, slave_addr = "8"}},
 *      -- pass the python_dir path as -p python_dir=\"/path/to/py-models/\"
 */

/**
 * @brief Bi-Directional Flow for I2C Communication
 *
 * The bi-directional flow serves as an I2C bus, facilitating communication between I2C master and slaves.
 *
 * When the master requests writing to a specific slave, the method `set_default_txn()` is used to set up the slave
 * address and the requested data length. Subsequently, the method `enqueue()` is used to push data to the slave.
 *
 * When the master requests reading from a specific slave, the method `force_send()` is utilized to send a transaction
 * to the slave at the required address, requesting a read command for a specific data length.
 *
 *
 * The I2C GENI_RX_FIFO can receive up to 64 bytes of data from slaves.
 *
 *
 * NOTE: Refer to the test qupv3_qupv3_se_wrapper_se0_i2c-test and Qbox/py-models/py-i2c.py for detailed instructions on
 * initializing I2C Master and for performing data read/write operations to spicific slaves
 */
#include "qupv3_qupv3_se_wrapper_se0_i2c.h"
#include <reg_model_maker/reg_model_maker.h>
/* I2C Opcodes */
enum class I2C_OPCODE : uint32_t {
    I2C_WRITE = 1,
    I2C_READ = 2,
    I2C_WRITE_READ = 3,
    I2C_ADDR_ONLY = 4,
    I2C_BUS_CLEAR = 6,
    I2C_STOP_ON_BUS = 7,
};

#define I2C_RX_FIFO_MAX      16
#define FIFO_SLOT_ENTRY_SIZE 4
#define MAX_BYTES            (I2C_RX_FIFO_MAX * FIFO_SLOT_ENTRY_SIZE)

namespace quic {
qupv3_qupv3_se_wrapper_se0_i2c::qupv3_qupv3_se_wrapper_se0_i2c(sc_core::sc_module_name _name)
    : qupv3_qupv3_se_wrapper_se0(_name)
{
    bind_regs_i2c(M);
    config.reset();
}

void qupv3_qupv3_se_wrapper_se0_i2c::update_irq()
{
    update_RX_FIFO_STATUS();
    bool level = (SE_IRQ_EN[GENI_M_IRQ_EN] && ((GENI_M_IRQ_STATUS & GENI_M_IRQ_ENABLE) != 0)) ||
                 (SE_IRQ_EN[GENI_S_IRQ_EN] && ((GENI_S_IRQ_STATUS & GENI_S_IRQ_ENABLE) != 0));
    irq->write(level);
    SCP_DEBUG(())("I2C::Writing IRQ = {}", level);
}

void qupv3_qupv3_se_wrapper_se0_i2c::update_RX_FIFO_STATUS()
{
    SCP_TRACE(())("Bytes in fifo {}", bytes_in_fifo);
    GENI_RX_FIFO_STATUS[RX_FIFO_WC] = bytes_in_fifo / FIFO_SLOT_ENTRY_SIZE +
                                      ((bytes_in_fifo % FIFO_SLOT_ENTRY_SIZE) ? 1 : 0);
    GENI_RX_FIFO_STATUS[RX_LAST] = (bytes_in_fifo ? 1 : 0);
    GENI_M_IRQ_STATUS[M_RX_FIFO_LAST] = (bytes_in_fifo ? 1 : 0);
    GENI_M_IRQ_STATUS[S_RX_FIFO_LAST] = (bytes_in_fifo ? 1 : 0);
    GENI_RX_FIFO_STATUS[RX_LAST_BYTE_VALID] = (bytes_in_fifo > FIFO_SLOT_ENTRY_SIZE ? FIFO_SLOT_ENTRY_SIZE
                                                                                    : bytes_in_fifo);

    GENI_M_IRQ_STATUS[M_RX_FIFO_WATERMARK] = (GENI_RX_FIFO_STATUS[RX_FIFO_WC] > GENI_RX_WATERMARK_REG) ? 1 : 0;
}
void qupv3_qupv3_se_wrapper_se0_i2c::receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
{
    uint8_t slave_addr = static_cast<uint8_t>((GENI_M_CMD0[PARAM] >> 9) &
                                              0x7f); // 7 bit-address mode (Slave address = 15-9)
    SCP_DEBUG(())("requested txn from slave at address:  {}", slave_addr);
    SCP_DEBUG(())("received txn from slave at address: {}", txn.get_address());
    if (txn.get_address() == slave_addr) {
        unsigned char* data = txn.get_data_ptr();
        if (bytes_in_fifo >= MAX_BYTES) {
            GENI_M_IRQ_STATUS[RX_FIFO_WR_ERR] = 1;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
        sc_assert(bytes_in_fifo < MAX_BYTES);

        unsigned char* fifo = (unsigned char*)(&(GENI_RX_FIFO[0]));
        uint32_t write_bytes = std::min(MAX_BYTES - bytes_in_fifo, static_cast<uint32_t>(txn.get_streaming_width()));
        memcpy(&fifo[bytes_in_fifo], data, write_bytes);
        bytes_in_fifo += write_bytes;
        SCP_TRACE(())
        ("received: {} byte has been received ({:.{}}) (fifo: {:.{}})", write_bytes, (char*)data, write_bytes,
         (char*)fifo, bytes_in_fifo);
        rx_remaining_bytes -= static_cast<int32_t>(txn.get_streaming_width());
        sc_assert(rx_remaining_bytes >= 0);
        if (rx_remaining_bytes == 0) {
            GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
        }
        update_RX_FIFO_STATUS();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    } else {
        SCP_ERR(()) << "Error: i2c address is not set properly";
    }
}

void qupv3_qupv3_se_wrapper_se0_i2c::send_txn()
{
    GENI_M_IRQ_STATUS &= ~GENI_M_IRQ_STATUS;
    GENI_S_IRQ_STATUS &= ~GENI_S_IRQ_STATUS;
    tlm::tlm_generic_payload cmd_txn;
    unsigned char cmd_data[] = { 0 };
    uint8_t addr = static_cast<uint8_t>((GENI_M_CMD0[PARAM] >> 9) & 0x7f);  // 7 bit-address mode (Slave address = 15-9)
    uint8_t stall = static_cast<uint8_t>((GENI_M_CMD0[PARAM] >> 2) & 0x01); // stop(0)/stall(1)
    SCP_DEBUG(())("GENI_M_CMD0 {:x}", static_cast<uint32_t>(GENI_M_CMD0));
    SCP_DEBUG(())("GENI_M_CMD0[PARAM]_address() {}", addr);
    SCP_DEBUG(())("GENI_M_CMD0[stall] {}", stall);

    if (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_WRITE)) {
        if (tx_trans_len == 0) {
            SCP_ERR(()) << "MASTER tx_trans_len=0x0.";
        }
        if (stall) { // QNX Platform: i2c_combined_writeread , SW will write offset if GENI_M_CMD0[STOP/STALL]=1
            cmd_txn.set_command(static_cast<tlm::tlm_command>(I2C_OPCODE::I2C_ADDR_ONLY));
        } else {
            cmd_txn.set_command(tlm::TLM_WRITE_COMMAND);
        }

        cmd_txn.set_address(addr);
        cmd_txn.set_data_length(tx_trans_len);
        backend_socket.set_default_txn(cmd_txn);
        GENI_M_IRQ_STATUS[TX_FIFO_WATERMARK] = 1;
        config.opcode_set = false;
        config.tx_len_set = false;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    }

    else if (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_READ)) {
        if (rx_trans_len == 0) {
            SCP_ERR(()) << "rx_trans_len=0x0.";
        }
        cmd_txn.set_command(tlm::TLM_READ_COMMAND);
        cmd_txn.set_address(addr);
        cmd_txn.set_data_length(rx_trans_len);
        cmd_txn.set_data_ptr(cmd_data);
        backend_socket.force_send(cmd_txn);
        config.opcode_set = false;
        config.rx_len_set = false;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    }

    else if (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_WRITE_READ)) {
        if (tx_trans_len == 0 || rx_trans_len == 0) {
            return; // wait for rx_trans_len to be set, this funtion will be called again in
                    // I2C_RX_TRANS_LEN.post_write()
        }
        cmd_txn.set_command(static_cast<tlm::tlm_command>(I2C_OPCODE::I2C_WRITE_READ));
        cmd_txn.set_address(addr);
        cmd_txn.set_data_length(rx_trans_len); // assume tx_trans_len is 1 (offset length).
        backend_socket.set_default_txn(cmd_txn);
        GENI_M_IRQ_STATUS[TX_FIFO_WATERMARK] = 1;
        config.reset();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    } else if (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_ADDR_ONLY)) {
        SCP_WARN(()) << "WIP, not Implemented yet";
    } else {
        SCP_ERR(()) << "OPCODE is not supported, check if GENI_M_CMD0 correctly configured ";
    }
}

void qupv3_qupv3_se_wrapper_se0_i2c::start_of_simulation() { do_reset(true); }

void qupv3_qupv3_se_wrapper_se0_i2c::do_reset(bool value)
{
    if (value) {
        /* NB writes should not happen before simulation starts, it's safe to do this here. */
        GENI_FW_REVISION_RO = 0x3ff; // I2C protocol
        SE_IRQ_EN = 0xF;
        DMA_GENERAL_CFG = 0x0;      // Forcing contents to zero since DMA mode is not enabled
        SE_HW_PARAM_0 = 0x201029E4; // value read is 2040xxxx force it to the correct value
    }
}

void qupv3_qupv3_se_wrapper_se0_i2c::end_of_elaboration()
{
    backend_socket.can_receive_set(MAX_BYTES); // can receive 16*4 bytes
    qupv3_qupv3_se_wrapper_se0::end_of_elaboration();

    /* READ functionality */

    GENI_RX_FIFO.post_read([&](TXN(txn)) {
        if (bytes_in_fifo <= 0) {
            GENI_M_IRQ_STATUS[RX_FIFO_RD_ERR] = 1;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
        sc_assert(bytes_in_fifo > 0);
        unsigned char* data = txn.get_data_ptr();
        unsigned char* fifo = (unsigned char*)(&(GENI_RX_FIFO[0]));

        uint32_t read_bytes = std::min(
            bytes_in_fifo, std::min(static_cast<uint32_t>(rx_trans_len), static_cast<uint32_t>(txn.get_data_length())));
        memcpy(data, fifo, read_bytes);

        rx_trans_len = rx_trans_len - read_bytes;
        bytes_in_fifo -= read_bytes;
        std::memmove(fifo, &(fifo[read_bytes]), bytes_in_fifo);

        SCP_TRACE(())
        ("GENI_RX_FIFO.post_read: {} byte has been read ({:.{}}) (fifo: {:.{}})", read_bytes, (char*)data, read_bytes,
         (char*)fifo, bytes_in_fifo);

        backend_socket.can_receive_more(read_bytes);

        if (rx_trans_len == 0) {
            GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
            SCP_DEBUG(())("GENI_RX_FIFO.post_read:  M_CMD_DONE");
            GENI_M_CMD0 = 0x0;
        }

        update_RX_FIFO_STATUS();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });

    /* WRITE functionality */
    GENI_DMA_MODE_EN.post_write([&](TXN()) {
        GENI_DMA_MODE_EN = 0x0; // Only FIFO mode suported.
    });

    GENI_RX_RFR_WATERMARK_REG.post_write([&](TXN()) {
        if (GENI_RX_RFR_WATERMARK_REG == 0x0) {
            GENI_M_IRQ_STATUS[M_RX_FIFO_WATERMARK] = 0;
        }
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    I2C_TX_TRANS_LEN.post_write([&](TXN()) {
        tx_trans_len = static_cast<uint32_t>(I2C_TX_TRANS_LEN);
        SCP_DEBUG(())("I2C_TX_TRANS_LEN {}", tx_trans_len);
        config.tx_len_set = true;
        if ((config.opcode_set) && ((config.tx_len_set) || (config.rx_len_set))) {
            send_txn();
        }
    });

    I2C_RX_TRANS_LEN.post_write([&](TXN()) {
        SCP_DEBUG(())("I2C_RX_TRANS_LEN {}", rx_trans_len);

        rx_trans_len = static_cast<uint32_t>(I2C_RX_TRANS_LEN);
        rx_remaining_bytes = static_cast<int32_t>(I2C_RX_TRANS_LEN);
        config.rx_len_set = true;
        if ((config.opcode_set) && ((config.tx_len_set) || (config.rx_len_set))) {
            send_txn();
        }
    });

    GENI_M_CMD0.post_write([&](TXN(txn)) {
        SCP_DEBUG(())("GENI_M_CMD0.post_write {}", static_cast<uint32_t>(GENI_M_CMD0));
        config.opcode_set = true;
        if ((config.opcode_set) && ((config.tx_len_set) || (config.rx_len_set))) {
            send_txn();
        }
    });

    GENI_TX_FIFO.post_write([&](TXN(txn)) {
        sc_assert(tx_trans_len > 0);
        unsigned char* data = txn.get_data_ptr();

        if ((GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_WRITE)) ||
            (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_WRITE_READ))) {
            unsigned int txn_len = txn.get_data_length();
            for (unsigned int i = 0; (i < txn_len) && (tx_trans_len > 0); i++) {
                backend_socket.enqueue(data[i]);
                SCP_DEBUG(())("GENI_TX_FIFO.post_write: sending {}", data[i]);
                tx_trans_len = tx_trans_len - 1;
            }
            if (tx_trans_len == 0) {
                GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
                SCP_DEBUG(())("GENI_TX_FIFO.post_write:  M_CMD_DONE");
                if (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(I2C_OPCODE::I2C_WRITE)) {
                    GENI_M_CMD0 = 0x0; // in case, it is write_read, GENI_M_CMD0 will be cleared in
                                       // GENI_RX_FIFO.post_read
                }
                irq_update.notify(sc_core::SC_ZERO_TIME);
            }

        } else {
            SCP_ERR(()) << "Error: M_CMD_0 is not set properly";
        }
    });

    GENI_RX_WATERMARK_REG.post_write([&](TXN()) {
        update_RX_FIFO_STATUS();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_TX_WATERMARK_REG.post_write([&](TXN()) {
        GENI_M_IRQ_STATUS[TX_FIFO_WATERMARK] = (GENI_TX_FIFO_STATUS[TX_FIFO_WC] < GENI_TX_WATERMARK_REG) ? 1 : 0;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    /* Ensure GENI_FW_REVISION_RO always returns the I2C protocol value. */
    GENI_FW_REVISION_RO.pre_read([&](TXN()) { GENI_FW_REVISION_RO = 0x3ff; });
}
} // namespace quic
