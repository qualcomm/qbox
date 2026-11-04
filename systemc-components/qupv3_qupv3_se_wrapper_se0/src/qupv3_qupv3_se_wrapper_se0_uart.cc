/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "qupv3_qupv3_se_wrapper_se0_uart.h"
#include <reg_model_maker/reg_model_maker.h>
/* UART Opcode */
enum class UART_OPCODE : uint32_t {
    START_TX = 1,
};

#define UART_RX_FIFO_MAX     16
#define FIFO_SLOT_ENTRY_SIZE 4
#define MAX_BYTES            (UART_RX_FIFO_MAX * FIFO_SLOT_ENTRY_SIZE)

/* DMA helper functions */
static inline void set_dma_ptr_l(uint64_t& addr, uint32_t val)
{
    addr = (addr & 0xFFFFFFFF00000000ULL) | static_cast<uint64_t>(val);
}

static inline void set_dma_ptr_h(uint64_t& addr, uint32_t val)
{
    addr = (addr & 0x00000000FFFFFFFFULL) | (static_cast<uint64_t>(val) << 32);
}

namespace quic {
qupv3_qupv3_se_wrapper_se0_uart::qupv3_qupv3_se_wrapper_se0_uart(sc_core::sc_module_name _name)
    : qupv3_qupv3_se_wrapper_se0(_name)
{
    bind_regs_uart(M);
}

void qupv3_qupv3_se_wrapper_se0_uart::update_irq()
{
    bool level;
    if (dma_enabled) {
        level = (SE_IRQ_EN[SE_DMA_RX_IRQ_EN] && ((DMA_RX_IRQ_STAT & DMA_RX_IRQ_EN) != 0)) ||
                (SE_IRQ_EN[SE_DMA_TX_IRQ_EN] && ((DMA_TX_IRQ_STAT & DMA_TX_IRQ_EN) != 0));
    } else {
        level = (SE_IRQ_EN[GENI_M_IRQ_EN] && ((GENI_M_IRQ_STATUS & GENI_M_IRQ_ENABLE) != 0)) ||
                (SE_IRQ_EN[GENI_S_IRQ_EN] && ((GENI_S_IRQ_STATUS & GENI_S_IRQ_ENABLE) != 0));
    }

    irq->write(level);
    SCP_TRACE(())("Writing IRQ = {}", level);
}

void qupv3_qupv3_se_wrapper_se0_uart::set_rx_fifo_status()
{
    SCP_TRACE(())("Bytes in fifo: {}", bytes_in_fifo);

    uint32_t word_count = 0;
    uint32_t last_byte_valid = 0;
    uint32_t byte_gran = GENI_RX_BYTE_GRANULARITY;
    switch (byte_gran) {
    case 0:
        // 4 * char size = 8
        word_count = (bytes_in_fifo + 4 - 1) / 4;
        // RX_LAST_BYTE_VALID=0 means the final FIFO line is fully populated.
        last_byte_valid = bytes_in_fifo % 4;
        break;
    case 1:
        // 2 * char size = 16
        word_count = (bytes_in_fifo + 2 - 1) / 2;
        last_byte_valid = (bytes_in_fifo % 2 ? 2 : 4);
        break;
    case 2:
        // 1 * char size = 32
        word_count = (bytes_in_fifo + 4 - 1) / 4;
        last_byte_valid = 4;
        break;
    default:
        SCP_FATAL(()) << "GENI_RX_BYTE_GRANULARITY can only take 0, 1 or 2 values!";
        break;
    }
    uint32_t rx_watermark = static_cast<uint32_t>(GENI_RX_WATERMARK_REG);
    bool has_data = (bytes_in_fifo > 0);
    bool watermark_reached = (word_count >= rx_watermark) && (rx_watermark > 0);

    // update RX FIFO status register
    GENI_RX_FIFO_STATUS[RX_FIFO_WC] = word_count;
    GENI_RX_FIFO_STATUS[RX_LAST] = has_data;
    GENI_RX_FIFO_STATUS[RX_LAST_BYTE_VALID] = last_byte_valid;

    // update interrupt status bits
    GENI_M_IRQ_STATUS[M_RX_FIFO_LAST] = has_data;
    GENI_M_IRQ_STATUS[M_RX_FIFO_WATERMARK] = watermark_reached;
    GENI_S_IRQ_STATUS[S_RX_FIFO_LAST] = has_data;
    GENI_S_IRQ_STATUS[S_RX_FIFO_WATERMARK] = watermark_reached;
}

void qupv3_qupv3_se_wrapper_se0_uart::fifo_mode_receive(tlm::tlm_generic_payload& txn)
{
    sc_assert(bytes_in_fifo < MAX_BYTES);
    unsigned char* data = txn.get_data_ptr();
    uint32_t current_rx_len = static_cast<uint32_t>(txn.get_data_length());

    if (data == nullptr || current_rx_len == 0) {
        SCP_FATAL(())("FIFO RX: Invalid data pointer or zero length");
    }
    unsigned char* fifo = reinterpret_cast<unsigned char*>(&GENI_RX_FIFO[0]);
    uint32_t write_bytes = std::min(MAX_BYTES - bytes_in_fifo, current_rx_len);
    if (write_bytes < current_rx_len) {
        SCP_DEBUG(())("FIFO RX: Only wrote {} of {} bytes, FIFO full", write_bytes, current_rx_len);
    }
    memcpy(&fifo[bytes_in_fifo], data, write_bytes);
    bytes_in_fifo += write_bytes;
    SCP_TRACE(())
    ("received: {} bytes ({:.{}}) (fifo: {:.{}})", write_bytes, (char*)data, write_bytes, (char*)fifo, bytes_in_fifo);
    set_rx_fifo_status();
}
void qupv3_qupv3_se_wrapper_se0_uart::dma_mode_receive(tlm::tlm_generic_payload& txn)
{
    unsigned char* data = txn.get_data_ptr();
    uint32_t current_rx_len = static_cast<uint32_t>(txn.get_data_length());

    if (!dma_rx_address_is_vaild || current_rx_len == 0) {
        SCP_FATAL(())("DMA RX: Invalid address or zero length");
        sc_assert(false);
    }
    if (current_rx_len > DMA_RX_LENGTH) { // DMA_RX_LENGTH is the length of current RX transaction
        SCP_FATAL(())
        ("DMA RX: current_rx_len {} exceeds DMA_RX_LENGTH {}", current_rx_len, static_cast<uint32_t>(DMA_RX_LENGTH));
        sc_assert(false);
    }
    // Perform DMA write and check return value
    if (!dma_write(dma_rx_address, data, current_rx_len)) {
        DMA_RX_IRQ_STAT[RX_SBE] = 1; // indicates an AHB master bus error during DMA transaction
        SCP_WARN(())("DMA RX: dma_write failed at address 0x{:x}", dma_rx_address);
        return;
    }
    DMA_RX_LENGTH_IN = current_rx_len; // buffer mode: DMA reports back the received number of bytes in DMA_RX_LENGTH_IN
    SCP_TRACE(())
    ("DMA:receive bus_write: dma_rx_address = 0x{:x}, current_rx_len = {}", dma_rx_address, current_rx_len);
    DMA_RX_LENGTH -= current_rx_len;

    if (DMA_RX_LENGTH == 0) {
        DMA_RX_IRQ_STAT[RX_DMA_DONE] = 1; //  if SBE bit is not set then DMA completed successfully
        dma_rx_address_is_vaild = false;
        dma_rx_address = 0;
        SCP_TRACE(()) << "DMA RECEIVE DONE";
    } else {
        SCP_DEBUG(()) << "THERE IS DATA LEFT TO RECEIVE";
        dma_rx_address = dma_rx_address + current_rx_len;
    }
}
void qupv3_qupv3_se_wrapper_se0_uart::receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
{
    if (dma_enabled) {
        dma_mode_receive(txn);
    } else {
        fifo_mode_receive(txn);
    }
    irq_update.notify(sc_core::SC_ZERO_TIME);
}

void qupv3_qupv3_se_wrapper_se0_uart::start_of_simulation() { do_reset(true); }

void qupv3_qupv3_se_wrapper_se0_uart::do_reset(bool value)
{
    if (value) {
        /* NB writes should not happen before simulation starts, it's safe to do this here. */
        GENI_FW_REVISION_RO = 0x2ff; // implement UART protocol
        SE_IRQ_EN = 0xF;
        bytes_in_fifo = 0;
        dma_enabled = false;
        dma_tx_address_is_vaild = false;
        dma_tx_address = 0;
        dma_rx_address_is_vaild = false;
        dma_rx_address = 0;
    }
}

void qupv3_qupv3_se_wrapper_se0_uart::end_of_elaboration()
{
    backend_socket.can_receive_set(MAX_BYTES);
    qupv3_qupv3_se_wrapper_se0::end_of_elaboration();

    /* FIFO MODE */
    /* FIFO READ functionality */
    GENI_RX_FIFO.post_read([&](TXN(txn)) {
        sc_assert(bytes_in_fifo > 0);
        unsigned char* data = txn.get_data_ptr();
        unsigned char* fifo = reinterpret_cast<unsigned char*>(&GENI_RX_FIFO[0]);
        uint32_t read_bytes = std::min(bytes_in_fifo, static_cast<uint32_t>(txn.get_data_length()));
        memcpy(data, fifo, read_bytes);
        bytes_in_fifo -= read_bytes;
        std::memmove(fifo, &(fifo[read_bytes]), bytes_in_fifo);

        SCP_TRACE(())
        ("GENI_RX_FIFO.post_read: {} bytes read ({:.{}}) (fifo: {:.{}})", read_bytes, (char*)data, read_bytes,
         (char*)fifo, bytes_in_fifo);

        backend_socket.can_receive_more(read_bytes);
        set_rx_fifo_status();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });

    /* FIFO WRITE functionality */
    GENI_TX_FIFO.post_write([&](TXN(txn)) {
        sc_assert(txn.get_address() == 0); // must be first element in fifo.
        unsigned char* data = txn.get_data_ptr();
        uint32_t data_len = txn.get_data_length();
        if ((GENI_M_CMD0[OPCODE] != static_cast<uint32_t>(UART_OPCODE::START_TX)) || (UART_TX_TRANS_LEN <= 0)) {
            SCP_WARN(())
            ("M_CMD_0 {} or UART_TX_LEN {} not set ({:.{}})", static_cast<uint32_t>(GENI_M_CMD0[OPCODE]),
             static_cast<uint32_t>(UART_TX_TRANS_LEN), (char*)data, data_len);
        }

        uint32_t bytes_to_send = std::min(data_len, static_cast<uint32_t>(UART_TX_TRANS_LEN));
        for (uint32_t i = 0; i < bytes_to_send; i++) {
            SCP_TRACE(())("GENI_TX_FIFO.post_write: sending 0x{:02x}", data[i]);
            backend_socket.enqueue(data[i]);
        }
        UART_TX_TRANS_LEN -= bytes_to_send;
        if (UART_TX_TRANS_LEN == 0) {
            GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
            GENI_M_CMD0 = 0x0;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
    });
    GENI_M_IRQ_STATUS.pre_read([&](TXN()) {
        if (UART_TX_TRANS_LEN > 0) {
            GENI_M_IRQ_STATUS[TX_FIFO_WATERMARK] = 1;
        }
    });
    UART_MANUAL_RFR.post_write([&](TXN()) {
        if (UART_MANUAL_RFR[UART_MANUAL_RFR_EN] && UART_MANUAL_RFR[UART_RFR_NOT_READY]) {
            backend_socket.reset();
        } else {
            backend_socket.can_receive_set(MAX_BYTES - bytes_in_fifo);
        }
    });
    GENI_RX_WATERMARK_REG.post_write([&](TXN()) {
        set_rx_fifo_status();
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_TX_WATERMARK_REG.post_write([&](TXN()) {
        GENI_M_IRQ_STATUS[TX_FIFO_WATERMARK] = 1;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_RX_FIFO_CTRL.post_write([&](TXN()) {
        if (GENI_RX_FIFO_CTRL[SW_RX_FIFO_FLUSH]) {
            // Flush RX FIFO: clear byte counter and FIFO buffer
            bytes_in_fifo = 0;
            memset(&GENI_RX_FIFO[0], 0, sizeof(GENI_RX_FIFO));
            GENI_RX_FIFO_CTRL[SW_RX_FIFO_FLUSH] = 0;
            GENI_M_IRQ_STATUS[M_RX_FIFO_LAST] = 1; // in FIFO mode Flush operation will end in RX_LAST IRQ
            GENI_S_IRQ_STATUS[S_RX_FIFO_LAST] = 1;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
    });

    /* DMA MODE */
    GENI_DMA_MODE_EN.post_write([&](TXN()) {
        dma_enabled = static_cast<bool>(GENI_DMA_MODE_EN);
        // When set, GENI works with DMA, otherwise in FIFO mode.
        // move to parent class once DMA is enabled for other qups (spi and i2c)
    });

    /* DMA WRITE functionality */
    DMA_TX_PTR_L.post_write([&](TXN()) {
        set_dma_ptr_l(dma_tx_address, DMA_TX_PTR_L);
        dma_tx_address_is_vaild = false; // invalidate until both halves written
    });
    DMA_TX_PTR_H.post_write([&](TXN()) {
        set_dma_ptr_h(dma_tx_address, DMA_TX_PTR_H);
        dma_tx_address_is_vaild = true; // valid after both halves written
        SCP_TRACE(())("dma_tx_address = 0x{:x}", dma_tx_address);
    });
    DMA_TX_ATTR.post_write([&](TXN()) {
        if (DMA_TX_ATTR[TX_DMA_OPERATION] != 0) { // buffer mode = 0, immediate data = 1, gather mode = 2
            SCP_FATAL(())("DMA_TX_ATTR: currently only DMA buffer mode is supported");
            sc_assert(false);
        }
        // SW will set DMA_TX_ATTR[EOT]= 1 means current buffer is EOT type
    });
    DMA_TX_LENGTH.post_write([&](TXN()) {
        uint32_t dma_tx_trans_len = static_cast<uint32_t>(DMA_TX_LENGTH);
        SCP_TRACE(())("DMA_TX_LENGTH.post_write() = {}", dma_tx_trans_len);

        if (!dma_tx_address_is_vaild) {
            SCP_FATAL(())("DMA TX: Invalid address");
            sc_assert(false);
        }
        if (dma_tx_trans_len == 0) {
            SCP_FATAL(())("DMA TX: Zero length");
            sc_assert(false);
        }
        unsigned char* tx_buffer = new unsigned char[dma_tx_trans_len];
        // Perform DMA read and check return value
        if (!dma_read(dma_tx_address, tx_buffer, dma_tx_trans_len)) {
            DMA_TX_IRQ_STAT[TX_SBE] = 1; // Indicates an AHB master bus error during DMA transaction
            SCP_WARN(())("DMA TX: dma_read failed at address 0x{:x}", dma_tx_address);
            delete[] tx_buffer;
            irq_update.notify(sc_core::SC_ZERO_TIME);
            return;
        }

        for (unsigned int i = 0; i < dma_tx_trans_len; i++) {
            backend_socket.enqueue(tx_buffer[i]);
        }
        delete[] tx_buffer;
        DMA_TX_LENGTH_IN = dma_tx_trans_len; // buffer mode: DMA reports back the sent number of bytes in
                                             // DMA_TX_LENGTH_IN
        DMA_TX_LENGTH = 0;
        DMA_TX_IRQ_STAT[TX_DMA_DONE] = 1; // if SBE bit is not set then DMA completed successfully
        DMA_TX_IRQ_STAT[TX_EOT] = static_cast<uint32_t>(DMA_TX_ATTR[EOT]); // Set only for the final DMA buffer.
        dma_tx_address = 0;
        dma_tx_address_is_vaild = false;
        irq_update.notify(sc_core::SC_ZERO_TIME);
        SCP_TRACE(())("DMA_TX completed, IRQ_STAT[TX_DMA_DONE] set");
    });

    /* DMA READ functionality */
    DMA_RX_PTR_L.post_write([&](TXN()) {
        set_dma_ptr_l(dma_rx_address, DMA_RX_PTR_L);
        dma_rx_address_is_vaild = false; // Invalidate until both halves written
    });
    DMA_RX_PTR_H.post_write([&](TXN()) {
        set_dma_ptr_h(dma_rx_address, DMA_RX_PTR_H);
        dma_rx_address_is_vaild = true; // Valid after both halves written
        SCP_TRACE(())("dma_rx_address = 0x{:x}", dma_rx_address);
    });

    DMA_RX_ATTR.post_write([&](TXN()) { // buffer mode = 0, immediate data = 1, gather mode = 2
        if (DMA_RX_ATTR[RX_DMA_OPERATION] != 0) {
            SCP_FATAL(())("DMA_RX_ATTR: currently only DMA buffer mode is supported");
            sc_assert(false);
        }
        // DMA_RX_ATTR does not have EOT bit
    });
}
} // namespace quic
