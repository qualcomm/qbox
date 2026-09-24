/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "nor_flash_backend.h"

nor_flash_backend::nor_flash_backend(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , biflow_socket("biflow_socket")
    , last_write_fragment_signal("last_write_fragment_signal")
    , p_binary_path("binary_path", "", "Path to the NOR flash binary image file")
    , p_sfdp_data("sfdp_data", "", "Comma-separated hex string of SFDP table bytes")
    , p_device_id("device_id", "0x20,0xBB,0x19,0x00", "Comma-separated hex string of device ID bytes")
    , m_status_reg(0x00)
    , m_flag_status_reg(0x80)
    , m_volatile_cfg(0xFB)
    , m_enhanced_vol_cfg(0xFF)
{
    biflow_socket.register_b_transport(this, &nor_flash_backend::b_transport);
    last_write_fragment_signal.register_value_changed_cb([&](bool value) { m_last_write_fragment = value; });
}

void nor_flash_backend::end_of_elaboration()
{
    std::string bin_path = p_binary_path.get_value();
    if (!bin_path.empty()) {
        if (!load_binary(bin_path)) {
            SCP_ERR(()) << "Failed to load binary: " << bin_path.c_str();
        } else {
            SCP_INFO(()) << "Loaded binary '" << bin_path << "' (" << m_binary_content.size() << " bytes)";
        }
    } else {
        SCP_WARN(()) << "No binary_path configured";
    }

    m_sfdp_data = parse_hex_string(p_sfdp_data.get_value());
    m_device_id = parse_hex_string(p_device_id.get_value());

    biflow_socket.can_receive_any();
}

bool nor_flash_backend::load_binary(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    m_binary_content.resize(static_cast<size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(m_binary_content.data()), size));
}

std::vector<uint8_t> nor_flash_backend::parse_hex_string(const std::string& hex_str)
{
    std::vector<uint8_t> result;
    std::stringstream ss(hex_str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        token = token.substr(start);
        size_t end = token.find_last_not_of(" \t");
        if (end != std::string::npos) token = token.substr(0, end + 1);
        unsigned long val = std::strtoul(token.c_str(), nullptr, 0);
        result.push_back(static_cast<uint8_t>(val));
    }
    return result;
}

void nor_flash_backend::send_data_to_qspi(const uint8_t* data, uint32_t length)
{
    /*
     * Use enqueue() to send data back to the QSPI controller.
     * We must NOT use force_send() here because b_transport is called from
     * within the QSPI module's force_send() context — calling force_send()
     * back would cause re-entrancy into the QSPI module's receive() method.
     * enqueue() queues data for flow-controlled delivery, avoiding this issue.
     */
    SCP_DEBUG(()) << "send_data_to_qspi: enqueueing " << length << " bytes";
    for (uint32_t i = 0; i < length; i++) {
        biflow_socket.enqueue(data[i]);
    }
}

void nor_flash_backend::send_single_byte(uint8_t value) { send_data_to_qspi(&value, 1); }

void nor_flash_backend::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    uint8_t opcode = static_cast<uint8_t>(trans.get_command());
    uint64_t address = trans.get_address();
    uint32_t tx_len = trans.get_data_length();
    uint8_t* data_ptr = trans.get_data_ptr();

    SCP_DEBUG(()) << "b_transport: opcode=0x" << std::hex << static_cast<int>(opcode) << " addr=0x" << address
                  << " len=" << std::dec << tx_len;

    switch (opcode) {
    // ---- READ commands that return data ----
    case READ_SFDP_CMD:
        SCP_DEBUG(()) << "READ_SFDP_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        handle_read_sfdp(address, tx_len);
        break;
    case READ_ID_CMD:
        SCP_DEBUG(()) << "READ_ID_CMD len=" << tx_len;
        handle_read_id(tx_len);
        break;
    case READ_STATUS_CMD:
        SCP_DEBUG(()) << "READ_STATUS_CMD status_reg=0x" << std::hex << static_cast<int>(m_status_reg);
        handle_read_status();
        break;
    case READ_FLAG_STATUS_CMD:
        SCP_DEBUG(()) << "READ_FLAG_STATUS_CMD flag=0x" << std::hex << static_cast<int>(m_flag_status_reg);
        handle_read_flag_status();
        break;
    case READ_ENHANCED_VOL_CFG_CMD:
        SCP_DEBUG(()) << "READ_ENHANCED_VOL_CFG_CMD val=0x" << std::hex << static_cast<int>(m_enhanced_vol_cfg);
        handle_read_enhanced_vol_cfg();
        break;
    case READ_VOLATILE_CFG_CMD:
        SCP_DEBUG(()) << "READ_VOLATILE_CFG_CMD val=0x" << std::hex << static_cast<int>(m_volatile_cfg);
        handle_read_volatile_cfg();
        break;
    case READ_CMD:
        SCP_DEBUG(()) << "READ_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        handle_read_memory(address, tx_len, nullptr);
        break;

    // ---- DMA read commands ----
    case READ4_CMD:
    case FAST_READ4_CMD:
        SCP_DEBUG(()) << "READ4/FAST_READ4 addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        handle_read_memory(address, tx_len, data_ptr);
        break;

    case OUTPUT_FAST_READ4_CMD:
    case QIOR4_CMD:
        SCP_DEBUG(()) << "OUTPUT_FAST_READ4/QIOR4 addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        handle_read_memory(address, tx_len, data_ptr);
        break;

    // ---- WRITE commands ----
    case WRITE_ENABLE_CMD:
        SCP_DEBUG(()) << "WRITE_ENABLE_CMD";
        m_status_reg |= 0x02; // set WEL (bit 1)
        break;
    case WRITE_DISABLE_CMD:
        SCP_DEBUG(()) << "WRITE_DISABLE_CMD";
        m_status_reg &= ~0x02; // clear WEL (bit 1)
        break;
    case WRITE_ENHANCED_VOL_CFG_CMD:
        SCP_DEBUG(()) << "WRITE_ENHANCED_VOL_CFG_CMD val=0x" << std::hex
                      << (data_ptr ? static_cast<int>(data_ptr[0]) : -1);
        if (data_ptr) m_enhanced_vol_cfg = data_ptr[0];
        break;
    case WRITE_VOLATILE_CFG_CMD:
        SCP_DEBUG(()) << "WRITE_VOLATILE_CFG_CMD val=0x" << std::hex << (data_ptr ? static_cast<int>(data_ptr[0]) : -1);
        if (data_ptr) m_volatile_cfg = data_ptr[0];
        break;
    case PP4_CMD:
    case PP4_QUAD_CMD:
        SCP_DEBUG(()) << "PP4_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        handle_write_memory(address, data_ptr, tx_len);
        break;

    case WRITE_STATUS_CMD:
        if (!data_ptr || !(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "WRITE_STATUS: WEL=0, ignoring";
            break;
        }
        m_status_reg = data_ptr[0] & ~(SR_WEL | 0x01); // WEL and WIP are read-only
        m_status_reg &= ~SR_WEL;                       // HW clears WEL after operation
        break;

    // ---- No-op commands (acknowledged but no action needed) ----
    case ENTER_4B_ADDR_CMD:
    case RESET_ENABLE_CMD:
    case RESET_CMD:
    case ENTER_DPD:
    case EXIT_DPD:
    case READ_STATUS_2_CMD:
    case READ_CFG1_CMD:
    case READ_CFG_REG_CMD:
    case WRITE_CFG2_CMD:
    case READ_SECURITY_CMD:
    case WRITE_OCTAL_EN_STATUS_2_CMD:
    case ENABLE_8S_8S_8S_MODE_SEQ:
    case CLEAR_ERR_REGS:
    case CLEAR_FLAG_STATUS_REG:
        SCP_DEBUG(()) << "CLEAR_FLAG_STATUS_REG";
        m_flag_status_reg = FSR_READY;
        break;

    // ---- Erase commands ----
    case ERASE4_4K_CMD:
    case ERASE4_SECTOR_CMD:
    case ERASE_4K_CMD:
    case ERASE_32K_CMD:
    case ERASE_SECTOR_CMD: {
        uint32_t erase_size;
        if (opcode == ERASE4_4K_CMD || opcode == ERASE_4K_CMD)
            erase_size = SECTOR_SIZE_4K;
        else if (opcode == ERASE_32K_CMD)
            erase_size = SECTOR_SIZE_32K;
        else
            erase_size = SECTOR_SIZE_64K;
        SCP_DEBUG(()) << "ERASE addr=0x" << std::hex << address << " size=" << erase_size;
        if (!(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "Erase: WEL=0, setting ERASE_ERR";
            m_flag_status_reg |= FSR_ERASE_ERR;
            break;
        }
        if (!m_binary_content.empty() && address < m_binary_content.size()) {
            uint32_t erase_end = std::min(static_cast<uint64_t>(address + erase_size),
                                          static_cast<uint64_t>(m_binary_content.size()));
            memset(&m_binary_content[address], 0xFF, erase_end - address);
        }
        m_status_reg &= ~SR_WEL;
        break;
    }
    case BULK_ERASE_CMD:
    case BULK_ERASE_CMD2:
        SCP_DEBUG(()) << "BULK_ERASE";
        if (!(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "Bulk erase: WEL=0, setting ERASE_ERR";
            m_flag_status_reg |= FSR_ERASE_ERR;
            break;
        }
        memset(m_binary_content.data(), 0xFF, m_binary_content.size());
        m_status_reg &= ~SR_WEL;
        break;

    default:
        SCP_WARN(()) << "Unknown opcode 0x" << std::hex << static_cast<int>(opcode);
        break;
    }
    trans.set_response_status(tlm::TLM_OK_RESPONSE);
}

void nor_flash_backend::handle_read_sfdp(uint64_t address, uint32_t tx_len)
{
    if (m_sfdp_data.empty()) {
        SCP_WARN(()) << "SFDP data not configured";
        return;
    }
    if (address >= m_sfdp_data.size() || address + tx_len > m_sfdp_data.size()) {
        SCP_WARN(()) << "SFDP read out of range: addr=0x" << std::hex << address << " len=" << std::dec << tx_len
                     << " sfdp_size=" << m_sfdp_data.size();
        return;
    }
    send_data_to_qspi(&m_sfdp_data[address], tx_len);
}

void nor_flash_backend::handle_read_id(uint32_t tx_len)
{
    uint32_t n = std::min(tx_len, static_cast<uint32_t>(m_device_id.size()));
    send_data_to_qspi(m_device_id.data(), n);
}

void nor_flash_backend::handle_read_status() { send_single_byte(m_status_reg); }

void nor_flash_backend::handle_read_flag_status() { send_single_byte(m_flag_status_reg); }

void nor_flash_backend::handle_read_enhanced_vol_cfg() { send_single_byte(m_enhanced_vol_cfg); }

void nor_flash_backend::handle_read_volatile_cfg() { send_single_byte(m_volatile_cfg); }

void nor_flash_backend::handle_read_memory(uint64_t address, uint32_t tx_len, uint8_t* out_buf)
{
    if (m_binary_content.empty() || address + tx_len > m_binary_content.size()) {
        SCP_WARN(()) << "read out of range: addr=0x" << std::hex << address << " len=" << std::dec << tx_len
                     << ", returning 0xFF";
        if (out_buf) {
            memset(out_buf, 0xFF, tx_len);
        } else {
            std::vector<uint8_t> ff_data(tx_len, 0xFF);
            send_data_to_qspi(ff_data.data(), tx_len);
        }
        return;
    }
    SCP_DEBUG(()) << "read memory: addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
    if (out_buf) {
        memcpy(out_buf, &m_binary_content[address], tx_len);
    } else {
        send_data_to_qspi(&m_binary_content[address], tx_len);
    }
}

void nor_flash_backend::handle_write_memory(uint64_t address, uint8_t* data_ptr, uint32_t tx_len)
{
    if (!data_ptr || m_binary_content.empty()) {
        SCP_WARN(()) << "Write: no data or no binary loaded";
        return;
    }
    if (!(m_status_reg & SR_WEL)) {
        SCP_WARN(()) << "Write: WEL=0, setting PROG_ERR";
        m_flag_status_reg |= FSR_PROG_ERR;
        return;
    }
    if (address + tx_len > m_binary_content.size()) {
        SCP_DEBUG(()) << "Write: expanding binary to " << std::hex << (address + tx_len);
        m_binary_content.resize(static_cast<size_t>(address + tx_len), 0xFF);
    }
    for (uint32_t i = 0; i < tx_len; i++) {
        m_binary_content[address + i] &= data_ptr[i];
    }
    if (m_last_write_fragment) {
        m_status_reg &= ~SR_WEL;
    }
}

void module_register() { GSC_MODULE_REGISTER_C(nor_flash_backend); }
