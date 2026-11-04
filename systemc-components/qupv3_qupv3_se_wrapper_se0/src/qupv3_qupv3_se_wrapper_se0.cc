/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_qupv3_qupv3_se_wrapper_se0_, __FILE__ "_config.zip");

#include <reg_model_maker/reg_model_maker.h>
#include "qupv3_qupv3_se_wrapper_se0.h"
#include <zip.h>
#include "qupv3_qupv3_se_wrapper_se0_uart.h"
#include "qupv3_qupv3_se_wrapper_se0_spi.h"
#include "qupv3_qupv3_se_wrapper_se0_i2c.h"
namespace quic {

qupv3_qupv3_se_wrapper_se0::qupv3_qupv3_se_wrapper_se0(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(zip_source_buffer_create(gZipArchive_qupv3_qupv3_se_wrapper_se0_Data,
                                                          gZipArchive_qupv3_qupv3_se_wrapper_se0_Size, 0, nullptr),
                                 ZIP_RDONLY, nullptr))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".se0"))
    , M("se0", m_jza)
    , target_socket("target_socket")
    , reset("reset")
    , backend_socket("backend_socket")
    , irq("irq")
    , dma_socket("dma_socket")
    , m_dmi_valid(false)
{
    SCP_TRACE(())("Constructor");
    sc_assert(loaded_ok);
    bind_regs(M);
    target_socket.bind(M.target_socket);
    SC_METHOD(update_irq);
    sensitive << irq_update;
    backend_socket.register_b_transport(this, &qupv3_qupv3_se_wrapper_se0::receive);
    reset.register_value_changed_cb([&](bool value) {
        if (value) {
            SCP_WARN(()) << "Reset";
        }
        M.reset(value);
        do_reset(value);
    });
}

// This function will be overriden
void qupv3_qupv3_se_wrapper_se0::do_reset(bool value)
{
    SCP_FATAL(())
        << "concrete reset functioanlity should be implemented in each child of the class: qupv3_qupv3_se_wrapper_se0";
}

// DMA helper methods
bool qupv3_qupv3_se_wrapper_se0::dma_read(uint64_t addr, uint8_t* data, uint32_t size)
{
    if (data == nullptr || size == 0) return false;
    if (addr + size < addr) return false;

    if (m_dmi_valid && addr >= dmi_data.get_start_address() && (addr + size - 1) <= dmi_data.get_end_address()) {
        if (dmi_data.is_read_allowed()) {
            uint64_t off = addr - dmi_data.get_start_address();
            unsigned char* ptr = dmi_data.get_dmi_ptr();
            memcpy(data, ptr + off, size);
            return true;
        }
    }

    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_READ_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(data));
    trans.set_data_length(size);
    trans.set_streaming_width(size);
    trans.set_byte_enable_length(0);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    sc_core::sc_time delay(sc_core::SC_ZERO_TIME);
    dma_socket->b_transport(trans, delay);

    if (trans.is_dmi_allowed()) {
        m_dmi_valid = dma_socket->get_direct_mem_ptr(trans, dmi_data);
    }

    return (trans.get_response_status() == tlm::TLM_OK_RESPONSE);
}

bool qupv3_qupv3_se_wrapper_se0::dma_write(uint64_t addr, uint8_t* data, uint32_t size)
{
    if (data == nullptr || size == 0) return false;
    if (addr + size < addr) return false;

    if (m_dmi_valid && addr >= dmi_data.get_start_address() && (addr + size - 1) <= dmi_data.get_end_address()) {
        if (dmi_data.is_write_allowed()) {
            uint64_t off = addr - dmi_data.get_start_address();
            unsigned char* ptr = dmi_data.get_dmi_ptr();
            memcpy(ptr + off, data, size);
            return true;
        }
    }

    tlm::tlm_generic_payload trans;
    trans.set_command(tlm::TLM_WRITE_COMMAND);
    trans.set_address(addr);
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(data));
    trans.set_data_length(size);
    trans.set_streaming_width(size);
    trans.set_byte_enable_length(0);
    trans.set_dmi_allowed(false);
    trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    sc_core::sc_time delay(sc_core::SC_ZERO_TIME);
    dma_socket->b_transport(trans, delay);

    if (trans.is_dmi_allowed()) {
        m_dmi_valid = dma_socket->get_direct_mem_ptr(trans, dmi_data);
    }

    return (trans.get_response_status() == tlm::TLM_OK_RESPONSE);
}

void qupv3_qupv3_se_wrapper_se0::end_of_elaboration()
{
    GENI_M_IRQ_CLEAR.post_write([&](TXN()) {
        GENI_M_IRQ_STATUS &= ~GENI_M_IRQ_CLEAR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_S_IRQ_CLEAR.post_write([&](TXN()) {
        GENI_S_IRQ_STATUS &= ~GENI_S_IRQ_CLEAR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_M_IRQ_EN_CLEAR.post_write([&](TXN()) {
        GENI_M_IRQ_ENABLE &= ~GENI_M_IRQ_EN_CLEAR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_S_IRQ_EN_CLEAR.post_write([&](TXN()) {
        GENI_S_IRQ_ENABLE &= ~GENI_S_IRQ_EN_CLEAR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_M_IRQ_EN_SET.post_write([&](TXN()) {
        if (!SE_IRQ_EN[GENI_M_IRQ_EN]) {
            GENI_M_IRQ_EN_SET = 0x0;
        }
        GENI_M_IRQ_ENABLE |= GENI_M_IRQ_EN_SET;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_S_IRQ_EN_SET.post_write([&](TXN()) {
        if (!SE_IRQ_EN[GENI_S_IRQ_EN]) {
            GENI_S_IRQ_EN_SET = 0x0;
        }
        GENI_S_IRQ_ENABLE |= GENI_S_IRQ_EN_SET;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_M_IRQ_ENABLE.post_write([&](TXN()) {
        // SW usually writes to GENI_M_IRQ_EN_SET
        if (!SE_IRQ_EN[GENI_M_IRQ_EN]) {
            GENI_M_IRQ_ENABLE = 0x0;
        }
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_S_IRQ_ENABLE.post_write([&](TXN()) {
        // SW usually writes to GENI_S_IRQ_EN_SET
        if (!SE_IRQ_EN[GENI_S_IRQ_EN]) {
            GENI_S_IRQ_ENABLE = 0x0;
        }
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_M_CMD_CTRL_REG.post_write([&](TXN()) {
        if (GENI_M_CMD_CTRL_REG[M_GENI_CMD_ABORT]) GENI_M_IRQ_STATUS[M_CMD_ABORT] = 1;   // Abort sequence steps
        if (GENI_M_CMD_CTRL_REG[M_GENI_CMD_CANCEL]) GENI_M_IRQ_STATUS[M_CMD_CANCEL] = 1; // Cancel sequence steps
        GENI_M_IRQ_STATUS[M_CMD_DONE] = 1;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    GENI_S_CMD_CTRL_REG.post_write([&](TXN()) {
        if (GENI_S_CMD_CTRL_REG[S_GENI_CMD_ABORT]) GENI_S_IRQ_STATUS[S_CMD_ABORT] = 1;
        if (GENI_S_CMD_CTRL_REG[S_GENI_CMD_CANCEL]) GENI_S_IRQ_STATUS[S_CMD_CANCEL] = 1;
        GENI_S_IRQ_STATUS[S_CMD_DONE] = 1;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });

    SE_IRQ_EN.post_write([&](TXN()) { irq_update.notify(sc_core::SC_ZERO_TIME); });
    DMA_RX_IRQ_CLR.post_write([&](TXN()) {
        DMA_RX_IRQ_STAT &= ~DMA_RX_IRQ_CLR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_TX_IRQ_CLR.post_write([&](TXN()) {
        DMA_TX_IRQ_STAT &= ~DMA_TX_IRQ_CLR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_RX_IRQ_EN_CLR.post_write([&](TXN()) {
        DMA_RX_IRQ_EN &= ~DMA_RX_IRQ_EN_CLR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_TX_IRQ_EN_CLR.post_write([&](TXN()) {
        DMA_TX_IRQ_EN &= ~DMA_TX_IRQ_EN_CLR;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_RX_IRQ_EN_SET.post_write([&](TXN()) {
        DMA_RX_IRQ_EN |= DMA_RX_IRQ_EN_SET;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_TX_IRQ_EN_SET.post_write([&](TXN()) {
        DMA_TX_IRQ_EN |= DMA_TX_IRQ_EN_SET;
        irq_update.notify(sc_core::SC_ZERO_TIME);
    });
    DMA_RX_FSM_RST.post_write([&](TXN()) {
        if (DMA_RX_FSM_RST) {
            DMA_RX_IRQ_STAT[RX_RESET_DONE] = 1;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
    });
    DMA_TX_FSM_RST.post_write([&](TXN()) {
        if (DMA_TX_FSM_RST) {
            DMA_TX_IRQ_STAT[TX_RESET_DONE] = 1;
            irq_update.notify(sc_core::SC_ZERO_TIME);
        }
    });
}
} // namespace quic

typedef quic::qupv3_qupv3_se_wrapper_se0 qupv3_qupv3_se_wrapper_se0;
typedef quic::qupv3_qupv3_se_wrapper_se0_i2c qupv3_qupv3_se_wrapper_se0_i2c;
typedef quic::qupv3_qupv3_se_wrapper_se0_uart qupv3_qupv3_se_wrapper_se0_uart;
typedef quic::qupv3_qupv3_se_wrapper_se0_spi qupv3_qupv3_se_wrapper_se0_spi;

void module_register()
{
    GSC_MODULE_REGISTER_C(qupv3_qupv3_se_wrapper_se0_uart);
    GSC_MODULE_REGISTER_C(qupv3_qupv3_se_wrapper_se0_spi);
    GSC_MODULE_REGISTER_C(qupv3_qupv3_se_wrapper_se0_i2c);
}
