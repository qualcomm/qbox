/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "nor_flash_backend.h"

#include <algorithm>

namespace {
constexpr uint8_t qspi_sfdp[] = {
    0x53, 0x46, 0x44, 0x50, 0x06, 0x01, 0x01, 0xFF, 0x00, 0x06, 0x01, 0x10, 0x30, 0x00, 0x00, 0xFF,
    0x84, 0x00, 0x01, 0x02, 0x80, 0x00, 0x00, 0xFF, 0x03, 0x00, 0x01, 0x02, 0x00, 0x01, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xE5, 0x20, 0x8A, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x29, 0xEB, 0x27, 0x6B, 0x27, 0x3B, 0x27, 0xBB,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x27, 0xBB, 0xFF, 0xFF, 0x29, 0xEB, 0x0C, 0x20, 0x10, 0xD8,
    0x0F, 0x52, 0x00, 0x00, 0x24, 0x4A, 0x99, 0x00, 0x8B, 0x8E, 0x03, 0xD4, 0xAC, 0x01, 0x27, 0x38,
    0x7A, 0x75, 0x7A, 0x75, 0xFB, 0xBD, 0xD5, 0x5C, 0x4A, 0x0F, 0x82, 0xFF, 0x81, 0xFD, 0x1E, 0x36,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xE7, 0xFF, 0xFF, 0x21, 0xDC, 0x5C, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
constexpr uint8_t qspi_id[] = { 0x20, 0xBB, 0x19, 0x00 };
constexpr uint8_t ospi_id[] = { 0x2C, 0x5B, 0x1B, 0x10 };

// MT35X SCCR SFDP Addendum Rev. D (06/2026), MT35XU01GBBA1G12-A0AAT, 1Gb/1.8V.
constexpr uint8_t ospi_sfdp[] = {
    0x53, 0x46, 0x44, 0x50, 0x0A, 0x01, 0x04, 0xFF, 0x00, 0x07, 0x01, 0x17, 0x30, 0x00, 0x00, 0xFF,
    0x84, 0x00, 0x01, 0x02, 0x90, 0x00, 0x00, 0xFF, 0x05, 0x00, 0x01, 0x06, 0xA0, 0x00, 0x00, 0xFF,
    0x0A, 0x00, 0x01, 0x08, 0xC0, 0x00, 0x00, 0xFF, 0x87, 0x01, 0x01, 0x1C, 0x00, 0x01, 0x00, 0xFF,
    0xE5, 0x20, 0x8A, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x0C, 0x20, 0x11, 0xD8,
    0x0F, 0x52, 0x00, 0x00, 0x39, 0x61, 0x99, 0x00, 0x87, 0x0E, 0x00, 0xD3, 0xAC, 0xA1, 0x27, 0x3D,
    0x7A, 0x75, 0x7A, 0x75, 0xFB, 0xBD, 0xD5, 0x5C, 0x00, 0x00, 0x70, 0xFF, 0x81, 0x50, 0xF8, 0xA1,
    0x2F, 0xCB, 0x27, 0x8B, 0x00, 0x00, 0x04, 0x01, 0x00, 0x06, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0x84,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0x43, 0x0E, 0xFF, 0xFF, 0x21, 0xDC, 0x5C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x0B, 0x80, 0x9E, 0xB1, 0x81, 0xB5, 0x85, 0x00, 0xF0, 0xFF, 0x9F, 0x50, 0x0A, 0x00, 0x00,
    0x28, 0xD5, 0x5A, 0x8C, 0x10, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x05, 0x00, 0x00, 0x14, 0x01,
    0x00, 0x00, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x05, 0x00, 0x00, 0xE7, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xFE, 0xFF, 0xE7, 0x08, 0xFE, 0xFF, 0xE7,
    0x00, 0x05, 0x88, 0x80, 0x06, 0x05, 0x88, 0xA1, 0x50, 0x70, 0x88, 0x84, 0x50, 0x70, 0x88, 0x85,
    0x81, 0x85, 0x01, 0xF0, 0xB1, 0xB5, 0x01, 0xF0, 0xD8, 0xB8, 0xCE, 0xFB, 0xB0, 0x2E, 0x2C, 0xD3,
    0x88, 0xA4, 0x89, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x81, 0x85, 0x03, 0xD0, 0xB1, 0xB5, 0x03, 0xD0, 0x00, 0x00, 0x00, 0xF8,
};

using Op = nor_flash_backend::Opcode;
constexpr Op qspi_opcodes[] = {
    { 0x66, nor_flash_backend::RESET_ENABLE }, { 0x99, nor_flash_backend::RESET }, { 0x05, nor_flash_backend::READ_STATUS },
    { 0x06, nor_flash_backend::WRITE_ENABLE }, { 0x04, nor_flash_backend::WRITE_DISABLE }, { 0x9F, nor_flash_backend::READ_ID }, { 0x9E, nor_flash_backend::READ_ID },
    { 0x5A, nor_flash_backend::READ_SFDP }, { 0x35, nor_flash_backend::ENTER_QUAD_MODE }, { 0x70, nor_flash_backend::READ_FLAG_STATUS },
    { 0x01, nor_flash_backend::WRITE_STATUS }, { 0xB5, nor_flash_backend::READ_NONVOLATILE_CFG }, { 0xB1, nor_flash_backend::WRITE_NONVOLATILE_CFG },
    { 0x65, nor_flash_backend::READ_ENHANCED_VOL_CFG }, { 0x61, nor_flash_backend::WRITE_ENHANCED_VOL_CFG },
    { 0x85, nor_flash_backend::READ_VOLATILE_CFG }, { 0x81, nor_flash_backend::WRITE_VOLATILE_CFG },
    { 0xB7, nor_flash_backend::ENTER_4B_ADDR }, { 0xE9, nor_flash_backend::EXIT_4B_ADDR }, { 0xB9, nor_flash_backend::ENTER_DPD }, { 0xAB, nor_flash_backend::RELEASE_DEEP_POWER_DOWN }, { 0x50, nor_flash_backend::CLEAR_FLAG_STATUS },
    { 0x03, nor_flash_backend::READ }, { 0x0B, nor_flash_backend::READ_DMA }, { 0x13, nor_flash_backend::READ_DMA }, { 0x0C, nor_flash_backend::READ_DMA },
    { 0x3B, nor_flash_backend::READ_DMA }, { 0x3C, nor_flash_backend::READ_DMA }, { 0x6B, nor_flash_backend::READ_DMA }, { 0x6C, nor_flash_backend::READ_DMA },
    { 0xBB, nor_flash_backend::READ_DMA }, { 0xBC, nor_flash_backend::READ_DMA }, { 0xEB, nor_flash_backend::READ_DMA }, { 0xEC, nor_flash_backend::READ_DMA },
    { 0x02, nor_flash_backend::PROGRAM }, { 0x12, nor_flash_backend::PROGRAM }, { 0x34, nor_flash_backend::PROGRAM }, { 0x3E, nor_flash_backend::PROGRAM },
    { 0x20, nor_flash_backend::ERASE_4K }, { 0x21, nor_flash_backend::ERASE_4K },
    { 0x52, nor_flash_backend::ERASE_32K }, { 0xD8, nor_flash_backend::ERASE_64K }, { 0xDC, nor_flash_backend::ERASE_64K }, { 0xC7, nor_flash_backend::BULK_ERASE }, { 0x60, nor_flash_backend::BULK_ERASE },
};
constexpr Op ospi_opcodes[] = {
    { 0x66, nor_flash_backend::RESET_ENABLE }, { 0x99, nor_flash_backend::RESET }, { 0x05, nor_flash_backend::READ_STATUS }, { 0x70, nor_flash_backend::READ_FLAG_STATUS },
    { 0x06, nor_flash_backend::WRITE_ENABLE }, { 0x04, nor_flash_backend::WRITE_DISABLE }, { 0x9E, nor_flash_backend::READ_ID }, { 0x9F, nor_flash_backend::READ_ID },
    { 0x5A, nor_flash_backend::READ_SFDP }, { 0xB5, nor_flash_backend::READ_NONVOLATILE_CFG }, { 0x85, nor_flash_backend::READ_VOLATILE_CFG },
    { 0x01, nor_flash_backend::WRITE_STATUS }, { 0xB1, nor_flash_backend::WRITE_NONVOLATILE_CFG }, { 0x81, nor_flash_backend::WRITE_VOLATILE_CFG }, { 0x50, nor_flash_backend::CLEAR_FLAG_STATUS },
    { 0xB7, nor_flash_backend::ENTER_4B_ADDR }, { 0xE9, nor_flash_backend::EXIT_4B_ADDR }, { 0xB9, nor_flash_backend::ENTER_DPD }, { 0xAB, nor_flash_backend::RELEASE_DEEP_POWER_DOWN },
    { 0x03, nor_flash_backend::READ }, { 0x0B, nor_flash_backend::READ_DMA }, { 0x8B, nor_flash_backend::READ_DMA }, { 0xCB, nor_flash_backend::READ_DMA },
    { 0x9D, nor_flash_backend::READ_DMA }, { 0xFD, nor_flash_backend::READ_DMA }, { 0x13, nor_flash_backend::READ_DMA }, { 0x0C, nor_flash_backend::READ_DMA },
    { 0x7C, nor_flash_backend::READ_DMA }, { 0xCC, nor_flash_backend::READ_DMA }, { 0x02, nor_flash_backend::PROGRAM }, { 0x82, nor_flash_backend::PROGRAM },
    { 0xC2, nor_flash_backend::PROGRAM }, { 0x12, nor_flash_backend::PROGRAM }, { 0x84, nor_flash_backend::PROGRAM }, { 0x8E, nor_flash_backend::PROGRAM },
    { 0x20, nor_flash_backend::ERASE_4K }, { 0x52, nor_flash_backend::ERASE_32K }, { 0xD8, nor_flash_backend::ERASE_64K }, { 0xC4, nor_flash_backend::BULK_ERASE },
    { 0x21, nor_flash_backend::ERASE_4K }, { 0x5C, nor_flash_backend::ERASE_32K }, { 0xDC, nor_flash_backend::ERASE_64K },
};
constexpr nor_flash_backend::FlashProfile profiles[] = {
    { nor_flash_backend::Profile::MT25, "qspi", qspi_sfdp, sizeof(qspi_sfdp), qspi_id, sizeof(qspi_id), qspi_opcodes, sizeof(qspi_opcodes) / sizeof(*qspi_opcodes), 32ull * 1024 * 1024 },
    { nor_flash_backend::Profile::MT35, "ospi", ospi_sfdp, sizeof(ospi_sfdp), ospi_id, sizeof(ospi_id), ospi_opcodes, sizeof(ospi_opcodes) / sizeof(*ospi_opcodes), 128ull * 1024 * 1024 },
};
static_assert(sizeof(qspi_sfdp) == 256, "MT25 shim must remain byte-for-byte compatible with Lua");
static_assert(sizeof(ospi_sfdp) == 0x170, "MT35X SCCR SFDP ends at documented byte 0x16F");
static_assert(qspi_sfdp[0] == 0x53 && ospi_sfdp[0] == 0x53 && ospi_sfdp[1] == 0x46 && ospi_sfdp[2] == 0x44 && ospi_sfdp[3] == 0x50,
              "SFDP signature");
static_assert(ospi_sfdp[6] == 4 && ospi_sfdp[8] == 0 && ospi_sfdp[9] == 7 && ospi_sfdp[11] == 0x17 && ospi_sfdp[12] == 0x30 &&
                  ospi_sfdp[16] == 0x84 && ospi_sfdp[19] == 2 && ospi_sfdp[20] == 0x90 && ospi_sfdp[24] == 5 && ospi_sfdp[27] == 6 &&
                  ospi_sfdp[28] == 0xA0 && ospi_sfdp[32] == 0x0A && ospi_sfdp[35] == 8 && ospi_sfdp[36] == 0xC0 &&
                  ospi_sfdp[40] == 0x87 && ospi_sfdp[41] == 1 && ospi_sfdp[43] == 0x1C && ospi_sfdp[44] == 0 && ospi_sfdp[45] == 1 && ospi_sfdp[47] == 0xFF,
               "MT35X SFDP headers and pointers");
static_assert([] {
    size_t end = 8 + (ospi_sfdp[6] + 1) * 8;
    for (size_t h = 8; h < 48; h += 8) {
        const size_t pointer = ospi_sfdp[h + 4] | (ospi_sfdp[h + 5] << 8) | (ospi_sfdp[h + 6] << 16);
        if (pointer < end) return false;
        end = pointer + ospi_sfdp[h + 3] * 4;
        if (end > sizeof(ospi_sfdp)) return false;
    }
    return end == sizeof(ospi_sfdp);
}(), "MT35X non-overlapping tables within the blob");
static_assert(ospi_sfdp[0x34] == 0xFF && ospi_sfdp[0x35] == 0xFF && ospi_sfdp[0x36] == 0xFF && ospi_sfdp[0x37] == 0x3F &&
                  profiles[1].capacity == 128ull * 1024 * 1024, "MT35X 1Gb density and capacity");
static_assert(ospi_sfdp[0x90] == 0x43 && ospi_sfdp[0xA1] == 0x0B && ospi_sfdp[0xC2] == 0x06 && ospi_sfdp[0xDF] == 0x00 &&
                  ospi_sfdp[0x108] == 0x08 && ospi_sfdp[0x111] == 0x05 && ospi_sfdp[0x16F] == 0xF8,
               "MT35X SFDP table bytes");
static_assert(qspi_id[0] == 0x20 && ospi_id[0] == 0x2C && ospi_id[1] == 0x5B && ospi_id[2] == 0x1B && ospi_id[3] == 0x10 &&
                  profiles[0].device_id_size == sizeof(qspi_id) && profiles[1].device_id_size == sizeof(ospi_id),
              "documented IDs");
static_assert(sizeof(qspi_opcodes) / sizeof(*qspi_opcodes) >= 34 && sizeof(ospi_opcodes) / sizeof(*ospi_opcodes) >= 38,
              "each profile needs its complete opcode table");

bool valid_mt35_cfg(uint8_t index, uint8_t value, bool nonvolatile)
{
    // MT35X Rev. I, Tables 6-7; configuration option 1 (boot in SDR x1).
    switch (index) {
    case 0x00:
        return value == 0xFF || value == 0xDF || value == 0xE7 || value == 0xC7;
    case 0x01:
        return value <= 0x1F;
    case 0x03:
    case 0x07:
        return value >= 0xFC;
    case 0x05:
        return value >= 0xFE;
    case 0x06:
        return value >= 0xFE || (nonvolatile && (value == 0xFD || value == 0xF8));
    default:
        return false;
    }
}
} // namespace

nor_flash_backend::nor_flash_backend(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , biflow_socket("biflow_socket")
    , last_write_fragment_signal("last_write_fragment_signal")
    , p_binary_path("binary_path", "", "Path to the NOR flash binary image file")
    , p_flash_type("flash_type", "qspi", "NOR flash type: qspi (MT25) or ospi (MT35XU01GBBA1G12-A0AAT)")
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

    const std::string flash_type = p_flash_type.get_value();
    for (const auto& profile : profiles) {
        if (flash_type == profile.name) m_profile = &profile;
    }
    if (!m_profile) {
        SCP_FATAL(()) << "Invalid flash_type '" << flash_type << "': expected 'qspi' or 'ospi'";
    }

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

nor_flash_backend::FlashOperation nor_flash_backend::decode_opcode(uint8_t opcode) const
{
    return operation_for(m_profile->profile, opcode);
}

nor_flash_backend::FlashOperation nor_flash_backend::operation_for(Profile profile, uint8_t opcode)
{
    const Op* operations;
    size_t operation_count;
    switch (profile) {
    case Profile::MT25:
        operations = qspi_opcodes;
        operation_count = sizeof(qspi_opcodes) / sizeof(*qspi_opcodes);
        break;
    case Profile::MT35:
        operations = ospi_opcodes;
        operation_count = sizeof(ospi_opcodes) / sizeof(*ospi_opcodes);
        break;
    default:
        return INVALID_OPERATION;
    }
    for (size_t i = 0; i < operation_count; ++i)
        if (operations[i].code == opcode) return operations[i].operation;
    return INVALID_OPERATION;
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

void nor_flash_backend::send_repeated_byte(uint8_t value, uint32_t length, uint8_t* out_buf)
{
    if (out_buf) {
        std::fill_n(out_buf, length, value);
        return;
    }
    for (uint32_t i = 0; i < length; ++i) biflow_socket.enqueue(value);
}

void nor_flash_backend::b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
{
    uint8_t opcode = static_cast<uint8_t>(trans.get_command());
    uint64_t address = trans.get_address();
    uint32_t tx_len = trans.get_data_length();
    uint8_t* data_ptr = trans.get_data_ptr();

    SCP_DEBUG(()) << "b_transport: opcode=0x" << std::hex << static_cast<int>(opcode) << " addr=0x" << address
                  << " len=" << std::dec << tx_len;

    if (!m_profile) {
        SCP_ERR(()) << "NOR flash profile was not selected";
        trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        return;
    }
    const FlashOperation operation = decode_opcode(opcode);
    const bool mt35_reset = m_reset_enabled && operation == RESET;
    if (m_profile->profile == Profile::MT35) {
        // Table 23: RESET MEMORY must follow RESET ENABLE.
        m_reset_enabled = operation == RESET_ENABLE;
    }
    if (m_profile->profile == Profile::MT35 &&
        (operation == READ_VOLATILE_CFG || operation == READ_NONVOLATILE_CFG ||
         operation == WRITE_VOLATILE_CFG || operation == WRITE_NONVOLATILE_CFG)) {
        const bool read = operation == READ_VOLATILE_CFG || operation == READ_NONVOLATILE_CFG;
        const bool nonvolatile = operation == READ_NONVOLATILE_CFG || operation == WRITE_NONVOLATILE_CFG;
        uint8_t* bank = nonvolatile ? m_mt35_nonvolatile_cfg : m_mt35_volatile_cfg;
        const uint8_t index = static_cast<uint8_t>(address);
        // ponytail: reject split register writes; buffer them if support is needed.
        if (!tx_len || (!read && (!data_ptr || tx_len != 1 || !m_last_write_fragment))) {
            trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        if (read) {
            send_repeated_byte(index < sizeof(m_mt35_volatile_cfg) ? bank[index] : 0xFF, tx_len, data_ptr);
        } else if (m_status_reg & SR_WEL) {
            m_status_reg &= ~SR_WEL;
            if (valid_mt35_cfg(index, data_ptr[0], nonvolatile)) {
                bank[index] = data_ptr[0];
                if (!nonvolatile && index == 0x05)
                    m_flag_status_reg = (m_flag_status_reg & ~0x01) | (bank[index] == 0xFE ? 0x01 : 0x00);
            } else {
                m_flag_status_reg |= 0x02; // FSR protection error; reserved setting ignored.
            }
        }
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
        return;
    }
    bool ok = true;
    switch (operation) {
    // ---- READ commands that return data ----
    case READ_SFDP:
        SCP_DEBUG(()) << "READ_SFDP_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        ok = handle_read_sfdp(address, tx_len);
        break;
    case READ_ID:
        SCP_DEBUG(()) << "READ_ID_CMD len=" << tx_len;
        if (!handle_read_id(address, tx_len, data_ptr)) {
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }
        break;
    case READ_NONVOLATILE_CFG:
        ok = handle_read_nonvolatile_cfg(tx_len, data_ptr);
        break;
    case READ_STATUS:
        SCP_DEBUG(()) << "READ_STATUS_CMD status_reg=0x" << std::hex << static_cast<int>(m_status_reg);
        send_repeated_byte(m_status_reg, tx_len, data_ptr);
        break;
    case READ_FLAG_STATUS:
        SCP_DEBUG(()) << "READ_FLAG_STATUS_CMD flag=0x" << std::hex << static_cast<int>(m_flag_status_reg);
        send_repeated_byte(m_flag_status_reg, tx_len, data_ptr);
        break;
    case READ_ENHANCED_VOL_CFG:
        SCP_DEBUG(()) << "READ_ENHANCED_VOL_CFG_CMD val=0x" << std::hex << static_cast<int>(m_enhanced_vol_cfg);
        send_repeated_byte(m_enhanced_vol_cfg, tx_len, data_ptr);
        break;
    case READ_VOLATILE_CFG:
        SCP_DEBUG(()) << "READ_VOLATILE_CFG_CMD val=0x" << std::hex << static_cast<int>(m_volatile_cfg);
        send_repeated_byte(m_volatile_cfg, tx_len, data_ptr);
        break;
    case READ:
        SCP_DEBUG(()) << "READ_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        ok = handle_read_memory(address, tx_len, nullptr);
        break;
    case READ_DMA:
        SCP_DEBUG(()) << "READ_DMA addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        ok = handle_read_memory(address, tx_len, data_ptr);
        break;

    // ---- DMA read commands ----
    // ---- WRITE commands ----
    case WRITE_ENABLE:
        SCP_DEBUG(()) << "WRITE_ENABLE_CMD";
        m_status_reg |= 0x02; // set WEL (bit 1)
        break;
    case WRITE_DISABLE:
        SCP_DEBUG(()) << "WRITE_DISABLE_CMD";
        m_status_reg &= ~0x02; // clear WEL (bit 1)
        break;
    case WRITE_ENHANCED_VOL_CFG:
        SCP_DEBUG(()) << "WRITE_ENHANCED_VOL_CFG_CMD val=0x" << std::hex
                      << (data_ptr ? static_cast<int>(data_ptr[0]) : -1);
        if (data_ptr) m_enhanced_vol_cfg = data_ptr[0];
        break;
    case WRITE_VOLATILE_CFG:
        SCP_DEBUG(()) << "WRITE_VOLATILE_CFG_CMD val=0x" << std::hex << (data_ptr ? static_cast<int>(data_ptr[0]) : -1);
        if (!data_ptr || !tx_len || !(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "WRITE_VOLATILE_CFG: WEL=0 or missing data, ignoring";
            break;
        }
        m_volatile_cfg = data_ptr[0];
        m_status_reg &= ~SR_WEL;
        break;
    case WRITE_NONVOLATILE_CFG:
        if (!data_ptr || tx_len < sizeof(m_nonvolatile_cfg)) {
            SCP_WARN(()) << "WRITE_NONVOLATILE_CFG requires two bytes";
            ok = false;
        } else {
            memcpy(m_nonvolatile_cfg, data_ptr, sizeof(m_nonvolatile_cfg));
        }
        break;
    case PROGRAM:
        SCP_DEBUG(()) << "PP4_CMD addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        ok = handle_write_memory(address, data_ptr, tx_len);
        break;

    case WRITE_STATUS:
        if (!data_ptr || !(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "WRITE_STATUS: WEL=0, ignoring";
            break;
        }
        m_status_reg = data_ptr[0] & ~(SR_WEL | 0x01); // WEL and WIP are read-only
        m_status_reg &= ~SR_WEL;                       // HW clears WEL after operation
        break;
    case ENTER_4B_ADDR:
        SCP_INFO(()) << "ENTER 4B address mode";
        if (m_profile->profile == Profile::MT35) m_flag_status_reg |= 0x01;
        break;
    case EXIT_4B_ADDR:
        SCP_INFO(()) << "EXIT 4B address mode";
        if (m_profile->profile == Profile::MT35) m_flag_status_reg &= ~0x01;
        break;

    case RESET_ENABLE:
        break;
    case RESET:
        if (m_profile->profile == Profile::MT35 && mt35_reset) {
            // Rev. I, pp. 15, 17-21, 37: preserve nonvolatile SR bits and array.
            memcpy(m_mt35_volatile_cfg, m_mt35_nonvolatile_cfg, sizeof(m_mt35_volatile_cfg));
            m_status_reg &= ~(SR_WEL | 0x01);
            m_flag_status_reg = FSR_READY | (m_mt35_volatile_cfg[0x05] == 0xFE ? 0x01 : 0x00);
        }
        break;
    // ---- No-op commands (acknowledged but no action needed) ----
    case ENTER_DPD:
    case ENTER_QUAD_MODE:
        break;
    case RELEASE_DEEP_POWER_DOWN:
        SCP_DEBUG(()) << "RELEASE_DEEP_POWER_DOWN";
        break;
    case CLEAR_FLAG_STATUS:
        SCP_DEBUG(()) << "CLEAR_FLAG_STATUS_REG";
        m_flag_status_reg = FSR_READY | (m_profile->profile == Profile::MT35 ? m_flag_status_reg & 0x01 : 0x00);
        break;

    // ---- Erase commands ----
    case ERASE_4K:
    case ERASE_32K:
    case ERASE_64K: {
        uint32_t erase_size;
        if (decode_opcode(opcode) == ERASE_4K)
            erase_size = SECTOR_SIZE_4K;
        else if (decode_opcode(opcode) == ERASE_32K)
            erase_size = SECTOR_SIZE_32K;
        else
            erase_size = SECTOR_SIZE_64K;
        SCP_DEBUG(()) << "ERASE addr=0x" << std::hex << address << " size=" << erase_size;
        if (!(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "Erase: WEL=0, setting ERASE_ERR";
            m_flag_status_reg |= FSR_ERASE_ERR;
            break;
        }
        if (!range_within_capacity(address, erase_size)) {
            SCP_WARN(()) << "Erase out of range: addr=0x" << std::hex << address;
            ok = false;
            break;
        }
        if (address < m_binary_content.size()) {
            const uint64_t erase_end = std::min<uint64_t>(address + erase_size, m_binary_content.size());
            memset(&m_binary_content[address], 0xFF, erase_end - address);
        }
        for (auto it = m_sparse_content.begin(); it != m_sparse_content.end();)
            if (it->first >= address && it->first - address < erase_size)
                it = m_sparse_content.erase(it);
            else
                ++it;
        m_status_reg &= ~SR_WEL;
        break;
    }
    case BULK_ERASE:
        SCP_DEBUG(()) << "BULK_ERASE";
        if (!(m_status_reg & SR_WEL)) {
            SCP_WARN(()) << "Bulk erase: WEL=0, setting ERASE_ERR";
            m_flag_status_reg |= FSR_ERASE_ERR;
            break;
        }
        std::fill(m_binary_content.begin(), m_binary_content.end(), 0xFF);
        m_sparse_content.clear();
        m_status_reg &= ~SR_WEL;
        break;

    default:
        SCP_WARN(()) << "Unknown opcode 0x" << std::hex << static_cast<int>(opcode);
        trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
        return;
    }
    trans.set_response_status(ok ? tlm::TLM_OK_RESPONSE : tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

bool nor_flash_backend::handle_read_sfdp(uint64_t address, uint32_t tx_len)
{
    if (address > m_profile->sfdp_size || tx_len > m_profile->sfdp_size - address) {
        SCP_WARN(()) << "SFDP read out of range: addr=0x" << std::hex << address << " len=" << std::dec << tx_len
                     << " sfdp_size=" << m_profile->sfdp_size;
        return false;
    }
    send_data_to_qspi(m_profile->sfdp_data + address, tx_len);
    return true;
}

bool nor_flash_backend::handle_read_id(uint64_t address, uint32_t tx_len, uint8_t* out_buf)
{
    if (!m_profile->device_id_size || address >= m_profile->device_id_size) {
        SCP_WARN(()) << "READ_ID is not documented for this flash profile";
        return false;
    }
    uint32_t n = std::min<uint64_t>(tx_len, m_profile->device_id_size - address);
    if (out_buf)
        memcpy(out_buf, m_profile->device_id + address, n);
    else
        send_data_to_qspi(m_profile->device_id + address, n);
    return true;
}

bool nor_flash_backend::handle_read_nonvolatile_cfg(uint32_t tx_len, uint8_t* out_buf)
{
    if (out_buf) {
        for (uint32_t i = 0; i < tx_len; ++i) out_buf[i] = m_nonvolatile_cfg[i % sizeof(m_nonvolatile_cfg)];
    } else {
        for (uint32_t i = 0; i < tx_len; ++i) biflow_socket.enqueue(m_nonvolatile_cfg[i % sizeof(m_nonvolatile_cfg)]);
    }
    return true;
}

bool nor_flash_backend::handle_read_memory(uint64_t address, uint32_t tx_len, uint8_t* out_buf)
{
    if (!range_within_capacity(address, tx_len)) {
        SCP_WARN(()) << "Read out of range: addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        return false;
    }
    SCP_DEBUG(()) << "read memory: addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
    if (out_buf) {
        for (uint32_t i = 0; i < tx_len; ++i) {
            const uint64_t current = address + i;
            const auto sparse = m_sparse_content.find(current);
            out_buf[i] = current < m_binary_content.size() ? m_binary_content[current] :
                         sparse != m_sparse_content.end() ? sparse->second : 0xFF;
        }
    } else {
        std::vector<uint8_t> data(tx_len);
        if (!handle_read_memory(address, tx_len, data.data())) return false;
        send_data_to_qspi(data.data(), tx_len);
    }
    return true;
}

bool nor_flash_backend::handle_write_memory(uint64_t address, uint8_t* data_ptr, uint32_t tx_len)
{
    if (!data_ptr) {
        SCP_WARN(()) << "Write: no data";
        return false;
    }
    if (!(m_status_reg & SR_WEL)) {
        SCP_WARN(()) << "Write: WEL=0, setting PROG_ERR";
        m_flag_status_reg |= FSR_PROG_ERR;
        return false;
    }
    if (!range_within_capacity(address, tx_len)) {
        SCP_WARN(()) << "Write out of range: addr=0x" << std::hex << address << " len=" << std::dec << tx_len;
        m_flag_status_reg |= FSR_PROG_ERR;
        return false;
    }
    for (uint32_t i = 0; i < tx_len; i++) {
        const uint64_t current = address + i;
        if (current < m_binary_content.size())
            m_binary_content[current] &= data_ptr[i];
        else {
            const auto sparse = m_sparse_content.find(current);
            m_sparse_content[current] = (sparse == m_sparse_content.end() ? 0xFF : sparse->second) & data_ptr[i];
        }
    }
    if (m_last_write_fragment) {
        m_status_reg &= ~SR_WEL;
    }
    return true;
}

bool nor_flash_backend::range_within_capacity(uint64_t address, uint64_t length) const
{
    return address <= m_profile->capacity && length <= m_profile->capacity - address;
}

void module_register() { GSC_MODULE_REGISTER_C(nor_flash_backend); }
