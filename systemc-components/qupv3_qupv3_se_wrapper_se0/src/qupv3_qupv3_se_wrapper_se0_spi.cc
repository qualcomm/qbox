/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @brief Configuring SPI on Linux
 *
 * 1. The Linux driver `drivers/spi/spidev.c`
 * ([source](https://elixir.bootlin.com/linux/v5.15/source/drivers/spi/spidev.c)) lacks support for `qcom,spidev`. To
 * address this:
 *
 * Add the following entry to the spidev_dt_ids struct :
 *    `{ .compatible = "qcom,spidev" }`
 *    After making this change, rebuild the initrd image.
 *
 * 2. Add a node in the device tree for SPI, as shown below:
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
 *        spi@a88000 {
 *            compatible = "qcom,geni-spi";
 *            reg = <0x0 0xa88000 0x0 0x4000>;
 *            #address-cells = <1>;
 *            #size-cells = <0>;
 *            interrupts = <0x0 0x163 0x4>;
 *            clocks = <0x8000>;
 *            clock-names = "se";
 *            status = "okay";
 *
 *            spidevmaster@0 {
 *                compatible = "qcom,spidev";
 *                reg = <0x00>;
 *                spi-max-frequency = <0x989680>;
 *                status = "okay";
 *            };
 *        };
 *    };
 *    ```
 *
 * 3. In `conf.lua`, set the module type and connect the IRQs:
 *
 *    Example:
 *    ```
 *    backend["biflow_socket"] = { bind = "&spi_qup_" .. tostring(qup_index) .. ".backend_socket" };
 *    platform["backend_" .. tostring(qup_index)] = backend;
 *    local qup = {
 *        moduletype = "qupv3_qupv3_se_wrapper_se0_spi",
 *        target_socket = { address = bank.addr + (i_0 * QUP_PITCH),
 *                          size = QUP_SIZE,
 *                          bind = "&router.initiator_socket" },
 *        irq = { bind = "&gic_0.spi_in_" .. spi_irq },
 *    };
 *    platform["spi_qup_" .. tostring(qup_index)] = qup;
 *    ```
 */

/**
 * @brief Bi-Directional Flow for SPI Communication
 *
 * The bi-directional flow controlled socket is the main key for spi_master and spi_slave to communicate with each
 * others.
 *
 * The SPI GENI_RX_FIFO can receive up to 64 bytes of data.
 *
 *
 * NOTE: Refer to the test qupv3_qupv3_se_wrapper_se0_spi-test for detailed instructions on
 * initializing SPI Master/Slave and performing data read/write operations.
 */

#include "qupv3_qupv3_se_wrapper_se0_spi.h"
#include <reg_model_maker/reg_model_maker.h>
/* SPI Opcodes */
enum class SPI_OPCODE : uint32_t {
    SPI_TX_ONLY_OPCODE = 1,
    SPI_RX_ONLY_OPCODE = 2,
    SPI_FULL_DUPLEX_OPCODE = 3,
};

#define SPI_RX_FIFO_MAX      16
#define FIFO_SLOT_ENTRY_SIZE 4
#define MAX_BYTES            (SPI_RX_FIFO_MAX * FIFO_SLOT_ENTRY_SIZE)

namespace quic {
qupv3_qupv3_se_wrapper_se0_spi::qupv3_qupv3_se_wrapper_se0_spi(sc_core::sc_module_name _name)
    : qupv3_qupv3_se_wrapper_se0(_name), p_master("master", true, "type of QUP SPI: true if master - false if slave")
{
    bind_regs_spi(M);
}

void qupv3_qupv3_se_wrapper_se0_spi::update_irq()
{
    update_RX_FIFO_STATUS();
    bool level = (SE_IRQ_EN[GENI_M_IRQ_EN] && ((GENI_M_IRQ_STATUS & GENI_M_IRQ_ENABLE) != 0)) ||
                 (SE_IRQ_EN[GENI_S_IRQ_EN] && ((GENI_S_IRQ_STATUS & GENI_S_IRQ_ENABLE) != 0));
    irq->write(level);
    SCP_DEBUG(())("SPI::Writing IRQ = {}", level);
}

void qupv3_qupv3_se_wrapper_se0_spi::update_RX_FIFO_STATUS()
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
void qupv3_qupv3_se_wrapper_se0_spi::receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
{
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
    ("received: {} byte has been received ({:.{}}) (fifo: {:.{}})", write_bytes, (char*)data, write_bytes, (char*)fifo,
     bytes_in_fifo);

    update_RX_FIFO_STATUS();
    irq_update.notify(sc_core::SC_ZERO_TIME);
}

void qupv3_qupv3_se_wrapper_se0_spi::start_of_simulation() { do_reset(true); }

void qupv3_qupv3_se_wrapper_se0_spi::do_reset(bool value)
{
    if (value) {
        /* NB writes should not happen before simulation starts, it's safe to do this here. */
        GENI_FW_REVISION_RO = 0x1ff; // SPI protocol
        // need to add QUPV3_1_QUPV3_HW_VERSION for Major and Minor values (required to include QUPV3_1_QUPV3_COMMON
        // registers)
        /* should be remove later once we get the correct reset values */
        SE_IRQ_EN = 0xF;
        DMA_GENERAL_CFG = 0x0;                         // Forcing contents to zero since DMA mode is not enabled
        SE_HW_PARAM_0 = 0x201029E4;                    // value read is 2040xxxx force it to the correct value
        GENI_FW_REVISION = (p_master) ? 0x1FF : 0x5FF; // Slave or Master protocol
        SPI_SLAVE_EN = (p_master) ? 0x0 : 0x1;
        GENI_CFG_SEQ_START = (p_master) ? 0x0 : 0x1;
    }
}

void qupv3_qupv3_se_wrapper_se0_spi::end_of_elaboration()
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

        if (tx_trans_len == 0 && rx_trans_len == 0) {
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

    SPI_WORD_LEN.post_write([&](TXN()) {
        SPI_WORD_LEN = 0x4;
        SCP_DEBUG(())("SPI::SPI_WORD_LEN force set to  {} bits ", SPI_WORD_LEN + 4); // Only byte transfer supported.
    });
    GENI_RX_RFR_WATERMARK_REG.post_write([&](TXN()) {
        if (GENI_RX_RFR_WATERMARK_REG == 0x0) {
            GENI_M_IRQ_STATUS[M_RX_FIFO_WATERMARK] = 0;
        }
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    SPI_TX_TRANS_LEN.post_write([&](TXN()) {
        tx_trans_len = static_cast<uint32_t>(SPI_TX_TRANS_LEN);
        SCP_DEBUG(())("SPI_TX_TRANS_LEN {}", tx_trans_len);
    });

    SPI_RX_TRANS_LEN.post_write([&](TXN()) {
        rx_trans_len = static_cast<uint32_t>(SPI_RX_TRANS_LEN);
        SCP_DEBUG(())("SPI_RX_TRANS_LEN {}", rx_trans_len);
    });

    GENI_TX_FIFO.post_write([&](TXN(txn)) {
        sc_assert(txn.get_address() == 0);
        sc_assert(tx_trans_len > 0);
        unsigned char* data = txn.get_data_ptr();

        if ((GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(SPI_OPCODE::SPI_TX_ONLY_OPCODE)) ||
            (GENI_M_CMD0[OPCODE] == static_cast<uint32_t>(SPI_OPCODE::SPI_FULL_DUPLEX_OPCODE))) {
            for (unsigned int i = 0; (i < txn.get_data_length()) && (tx_trans_len > 0); i++) {
                backend_socket.enqueue(data[i]);
                SCP_DEBUG(())("GENI_TX_FIFO.post_write: sending {}", data[i]);
                tx_trans_len = tx_trans_len - 1;
            }
            if (tx_trans_len == 0 && rx_trans_len == 0) {
                GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
                SCP_DEBUG(())("GENI_TX_FIFO.post_write:  M_CMD_DONE");
                GENI_M_CMD0 = 0x0;
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
}
} // namespace quic
