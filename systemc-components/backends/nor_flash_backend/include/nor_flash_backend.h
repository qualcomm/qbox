/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NOR_FLASH_BACKEND_H
#define NOR_FLASH_BACKEND_H

#include <systemc>
#include <tlm>
#include <cci_configuration>
#include <scp/report.h>
#include <ports/biflow-socket.h>
#include <ports/target-signal-socket.h>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

/**
 * @brief SystemC NOR Flash Backend for QSPI
 *
 * CCI Parameters:
 *   - binary_path: Path to the binary image file
 *   - sfdp_data:   Comma-separated hex string of SFDP table bytes
 *   - device_id:   Comma-separated hex string of device ID bytes
 */
class nor_flash_backend : public sc_core::sc_module
{
public:
    SCP_LOGGER();
    gs::biflow_socket<nor_flash_backend> biflow_socket;
    TargetSignalSocket<bool> last_write_fragment_signal;

    cci::cci_param<std::string> p_binary_path;
    cci::cci_param<std::string> p_sfdp_data;
    cci::cci_param<std::string> p_device_id;

    nor_flash_backend(sc_core::sc_module_name name);

    void end_of_elaboration() override;
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

private:
    // Flash command opcodes
    enum FlashOpcode : uint8_t {
        RESET_ENABLE_CMD = 0x66,
        RESET_CMD = 0x99,
        READ_STATUS_CMD = 0x05,
        READ_STATUS_2_CMD = 0x3F,
        WRITE_STATUS_2_CMD = 0x3E,
        WRITE_ENABLE_CMD = 0x06,
        WRITE_DISABLE_CMD = 0x04,
        READ_ID_CMD = 0x9F,
        READ_SFDP_CMD = 0x5A,
        READ_CFG1_CMD = 0x35,
        READ_FLAG_STATUS_CMD = 0x70,
        READ_SECURITY_CMD = 0x2B,
        WRITE_STATUS_CMD = 0x01,
        ENTER_4B_ADDR_CMD = 0xB7,
        READ_CFG_REG_CMD = 0x15,
        READ_ENHANCED_VOL_CFG_CMD = 0x65,
        ENTER_DPD = 0xB9,
        EXIT_DPD = 0xAB,
        WRITE_ENHANCED_VOL_CFG_CMD = 0x61,
        WRITE_OCTAL_EN_STATUS_2_CMD = 0x31,
        CLEAR_OCTAL_EN_STATUS_2_CMD = 0x3E,
        ENABLE_8S_8S_8S_MODE_SEQ = 0xE8,
        CLEAR_ERR_REGS = 0xB6,
        CLEAR_FLAG_STATUS_REG = 0x50,
        WRITE_CFG2_CMD = 0x72,
        READ_VOLATILE_CFG_CMD = 0x85,
        WRITE_VOLATILE_CFG_CMD = 0x81,
        READ_CMD = 0x03,
        READ4_CMD = 0x13,
        FAST_READ4_CMD = 0x0C,
        OUTPUT_FAST_READ4_CMD = 0x6C,
        QIOR4_CMD = 0xEC,
        ERASE4_SECTOR_CMD = 0xDC,
        ERASE4_4K_CMD = 0x21,
        ERASE_SECTOR_CMD = 0xD8,
        ERASE_4K_CMD = 0x20,
        ERASE_32K_CMD = 0x52,
        BULK_ERASE_CMD = 0xC7,
        BULK_ERASE_CMD2 = 0x60,
        PP4_CMD = 0x12,
        PP4_QUAD_CMD = 0x34
    };

    std::vector<uint8_t> m_binary_content;
    std::vector<uint8_t> m_sfdp_data;
    std::vector<uint8_t> m_device_id;

    uint8_t m_status_reg;       ///< SR1  [7]=SRWD [6:3]=BP3:0 [1]=WEL [0]=WIP
    uint8_t m_flag_status_reg;  ///< FSR  [7]=READY [6]=ERASE_SUSP [5]=ERASE_ERR [4]=PROG_ERR [3]=VPP_ERR [2]=PROG_SUSP
                                ///< [1]=PROT_ERR [0]=ADDR_MODE
    uint8_t m_volatile_cfg;     ///< VCR  [7:4]=DUMMY_CYCLES [3]=XIP_DIS [2:1]=rsvd [0]=WRAP_DIS  (default 0xFB)
    uint8_t m_enhanced_vol_cfg; ///< EVCR [7]=DRV_STR [6]=HOLD_DIS [5]=QUAD_DIS [4]=DUAL_DIS [3:0]=rsvd  (default 0xFF =
                                ///< SPI mode)
    bool m_last_write_fragment = true; ///< true = current PP4 is the last DMA fragment → clear WEL after write

    static constexpr uint8_t SR_WEL = 0x02;              ///< Status reg: Write Enable Latch (bit 1)
    static constexpr uint8_t FSR_READY = 0x80;           ///< Flag status: operation complete (bit 7)
    static constexpr uint8_t FSR_ERASE_ERR = 0x20;       ///< Flag status: erase error (bit 5)
    static constexpr uint8_t FSR_PROG_ERR = 0x10;        ///< Flag status: program error (bit 4)
    static constexpr uint32_t SECTOR_SIZE_4K = 0x1000;   ///< 4 KB subsector
    static constexpr uint32_t SECTOR_SIZE_32K = 0x8000;  ///< 32 KB subsector
    static constexpr uint32_t SECTOR_SIZE_64K = 0x10000; ///< 64 KB sector

    bool load_binary(const std::string& path);
    std::vector<uint8_t> parse_hex_string(const std::string& hex_str);
    void send_data_to_qspi(const uint8_t* data, uint32_t length);
    void send_single_byte(uint8_t value);

    void handle_read_sfdp(uint64_t address, uint32_t tx_len);
    void handle_read_id(uint32_t tx_len);
    void handle_read_status();
    void handle_read_flag_status();
    void handle_read_enhanced_vol_cfg();
    void handle_read_volatile_cfg();
    void handle_read_memory(uint64_t address, uint32_t tx_len, uint8_t* out_buf);
    void handle_write_memory(uint64_t address, uint8_t* data_ptr, uint32_t tx_len);
};

extern "C" void module_register();

#endif // NOR_FLASH_BACKEND_H