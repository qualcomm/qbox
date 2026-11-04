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
#include <unordered_map>

/**
 * @brief SystemC NOR Flash Backend for QSPI/OSPI
 *
 * CCI Parameters:
 *   - binary_path: Path to the binary image file
 *   - flash_type:  "qspi" (MT25, default) or "ospi" (MT35XU01GBBA1G12-A0AAT)
 */
class nor_flash_backend : public sc_core::sc_module
{
public:
    SCP_LOGGER();
    gs::biflow_socket<nor_flash_backend> biflow_socket;
    TargetSignalSocket<bool> last_write_fragment_signal;

    cci::cci_param<std::string> p_binary_path;
    cci::cci_param<std::string> p_flash_type;

    nor_flash_backend(sc_core::sc_module_name name);

    /** @brief Load the image and select the configured flash profile. */
    void end_of_elaboration() override;
    /** @brief Dispatch one flash command and set its TLM response. */
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay);

public:
    enum class Profile : uint8_t { MT25, MT35 };
    enum FlashOperation : uint8_t {
        RESET_ENABLE, RESET, READ_STATUS, WRITE_ENABLE, WRITE_DISABLE, READ_ID, READ_SFDP,
        ENTER_QUAD_MODE, READ_FLAG_STATUS, WRITE_STATUS, ENTER_4B_ADDR, EXIT_4B_ADDR,
        READ_NONVOLATILE_CFG, WRITE_NONVOLATILE_CFG,
        READ_ENHANCED_VOL_CFG, ENTER_DPD, RELEASE_DEEP_POWER_DOWN, WRITE_ENHANCED_VOL_CFG, CLEAR_FLAG_STATUS,
        READ_VOLATILE_CFG, WRITE_VOLATILE_CFG, READ, READ_DMA, PROGRAM, ERASE_4K, ERASE_32K, ERASE_64K, BULK_ERASE,
        INVALID_OPERATION = 0xff
    };
    struct Opcode { uint8_t code; FlashOperation operation; };
    struct FlashProfile {
        Profile profile;
        const char* name;
        const uint8_t* sfdp_data;
        uint32_t sfdp_size;
        const uint8_t* device_id;
        uint32_t device_id_size;
        const Opcode* opcodes;
        uint32_t opcode_count;
        uint64_t capacity;
    };
    /** @brief Return the backend operation supported for a profile opcode. */
    static FlashOperation operation_for(Profile profile, uint8_t opcode);
private:

    std::vector<uint8_t> m_binary_content;
    std::unordered_map<uint64_t, uint8_t> m_sparse_content;
    const FlashProfile* m_profile = nullptr;

    uint8_t m_status_reg;       ///< SR1  [7]=SRWD [6:3]=BP3:0 [1]=WEL [0]=WIP
    uint8_t m_flag_status_reg;  ///< FSR  [7]=READY [6]=ERASE_SUSP [5]=ERASE_ERR [4]=PROG_ERR [3]=VPP_ERR [2]=PROG_SUSP
                                ///< [1]=PROT_ERR [0]=ADDR_MODE
    uint8_t m_volatile_cfg;     ///< VCR  [7:4]=DUMMY_CYCLES [3]=XIP_DIS [2:1]=rsvd [0]=WRAP_DIS  (default 0xFB)
    uint8_t m_nonvolatile_cfg[2] = { 0xFF, 0xFF };
    // MT35X Rev. I, Tables 6-7: erased defaults, indexed by address LSB.
    uint8_t m_mt35_volatile_cfg[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t m_mt35_nonvolatile_cfg[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    uint8_t m_enhanced_vol_cfg; ///< EVCR [7]=DRV_STR [6]=HOLD_DIS [5]=QUAD_DIS [4]=DUAL_DIS [3:0]=rsvd  (default 0xFF =
                                ///< SPI mode)
    bool m_last_write_fragment = true; ///< true = current PP4 is the last DMA fragment → clear WEL after write
    bool m_reset_enabled = false;

    static constexpr uint8_t SR_WEL = 0x02;              ///< Status reg: Write Enable Latch (bit 1)
    static constexpr uint8_t FSR_READY = 0x80;           ///< Flag status: operation complete (bit 7)
    static constexpr uint8_t FSR_ERASE_ERR = 0x20;       ///< Flag status: erase error (bit 5)
    static constexpr uint8_t FSR_PROG_ERR = 0x10;        ///< Flag status: program error (bit 4)
    static constexpr uint32_t SECTOR_SIZE_4K = 0x1000;   ///< 4 KB subsector
    static constexpr uint32_t SECTOR_SIZE_32K = 0x8000;  ///< 32 KB subsector
    static constexpr uint32_t SECTOR_SIZE_64K = 0x10000; ///< 64 KB sector

    /** @brief Load the configured flash image into the backend. */
    bool load_binary(const std::string& path);
    /** @brief Map an opcode to the selected profile operation. */
    FlashOperation decode_opcode(uint8_t opcode) const;
    /** @brief Queue response bytes for delivery to QSPI. */
    void send_data_to_qspi(const uint8_t* data, uint32_t length);
    /** @brief Fill or queue a repeated register response byte. */
    void send_repeated_byte(uint8_t value, uint32_t length, uint8_t* out_buf);

    /** @brief Handle an SFDP table read with bounds checking. */
    bool handle_read_sfdp(uint64_t address, uint32_t tx_len);
    /** @brief Handle a profile-specific JEDEC ID read. */
    bool handle_read_id(uint64_t address, uint32_t tx_len, uint8_t* out_buf);
    /** @brief Return the nonvolatile configuration register bytes. */
    bool handle_read_nonvolatile_cfg(uint32_t tx_len, uint8_t* out_buf);
    /** @brief Read flash contents from dense or sparse storage. */
    bool handle_read_memory(uint64_t address, uint32_t tx_len, uint8_t* out_buf);
    /** @brief Program flash contents while enforcing WEL and capacity. */
    bool handle_write_memory(uint64_t address, uint8_t* data_ptr, uint32_t tx_len);
    /** @brief Check that an address range fits the selected flash capacity. */
    bool range_within_capacity(uint64_t address, uint64_t length) const;
};

extern "C" void module_register();

#endif // NOR_FLASH_BACKEND_H
