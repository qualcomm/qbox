/*
 * This file is part of libqbox
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * @brief Configurable address-routed I2C slave backend.
 *
 * Each configured address owns a socket, register file, and read pointer.
 */

#ifndef _GS_I2C_MULTI_SLAVE_BACKEND_H_
#define _GS_I2C_MULTI_SLAVE_BACKEND_H_

#include <systemc>
#include <tlm.h>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <ports/biflow-socket.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class i2c_multi_slave_backend : public sc_core::sc_module
{
    SCP_LOGGER();

    using socket_type = gs::biflow_socket<i2c_multi_slave_backend>;

    /* These command values identify combined transfers and offset selection. */
    static constexpr uint32_t write_read_opcode = 3;
    static constexpr uint32_t addr_only_opcode = 4;

    struct slave {
        std::unique_ptr<socket_type> socket; // Connects this slave to the I2C bus.
        std::array<uint8_t, 256> data{};     // 256-byte storage addressed by an 8-bit offset.
        uint8_t offset = 0;                  // Current read position set by an offset write or combined write/read.
    };

    std::unordered_map<uint8_t, slave> m_slaves;

public:
    /** Number of entries required in @ref p_slave_addresses. */
    cci::cci_param<unsigned int> p_num_slaves;

    /** List of unique 7-bit addresses to model. */
    cci::cci_param<std::vector<unsigned int>> p_slave_addresses;

    /**
     * Construct the backend and create one socket for every configured slave.
     *
     * Invalid, duplicate, or mismatched address configuration is fatal.
     */
    i2c_multi_slave_backend(sc_core::sc_module_name name)
        : sc_core::sc_module(name)
        , p_num_slaves("num_slaves", 0, "number of configured I2C slave addresses")
        , p_slave_addresses("slave_addresses",
                            gs::cci_get_vector<unsigned int>(cci::cci_get_broker(),
                                                             std::string(sc_module::name()) + ".slave_addresses"),
                            "configured 7-bit I2C slave addresses")
    {
        const auto& addresses = p_slave_addresses.get_value();
        const unsigned int num_slaves = p_num_slaves.get_value();
        if (num_slaves != addresses.size()) {
            SCP_FATAL(())("num_slaves ({}) does not match slave_addresses ({})", num_slaves, addresses.size());
        }

        for (unsigned int raw_addr : addresses) {
            if (raw_addr > 0x7f) {
                SCP_FATAL(())("invalid I2C slave address 0x{:x}; expected a 7-bit address", raw_addr);
            }
            const uint8_t addr = static_cast<uint8_t>(raw_addr);
            auto [slave_it, inserted] = m_slaves.try_emplace(addr);
            if (!inserted) {
                SCP_FATAL(())("duplicate I2C slave address 0x{:02x}", addr);
            }
            const auto socket_name = "backend_socket_" + std::to_string(addr);
            slave_it->second.socket = std::make_unique<socket_type>(socket_name.c_str());
            slave_it->second.socket->register_b_transport(this, &i2c_multi_slave_backend::b_transport);
            SCP_DEBUG(())("Created {} for slave 0x{:02x}", socket_name, addr);
        }
    }

    /** Return the backend socket bound to a configured slave address. */
    socket_type* slave_socket_for(uint8_t slave_address)
    {
        auto slave_it = m_slaves.find(slave_address);
        return (slave_it != m_slaves.end()) ? slave_it->second.socket.get() : nullptr;
    }

    /** Allow each bound slave socket to receive transactions from the master. */
    void end_of_elaboration()
    {
        for (auto& slave_entry : m_slaves) {
            if (slave_entry.second.socket->is_bound()) slave_entry.second.socket->can_receive_any();
        }
    }

    /**
     * Handle a write, read, or combined write/read transaction for one slave.
     *
     * Combined transactions carry the write length in streaming_width and the
     * requested read length in data_length.
     */
    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        const uint8_t slave_address = static_cast<uint8_t>(txn.get_address());
        auto slave_it = m_slaves.find(slave_address);
        if (slave_it == m_slaves.end()) {
            SCP_WARN(())("b_transport: no slave modelled at address 0x{:02x}", slave_address);
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        slave& i2c_slave = slave_it->second;
        uint8_t* data = txn.get_data_ptr();
        const auto command = txn.get_command();
        const uint32_t command_value = static_cast<uint32_t>(command);

        const bool is_combined = command_value == write_read_opcode;
        const bool uses_offset = command_value == addr_only_opcode;
        const uint32_t data_length = txn.get_data_length();
        const uint32_t streaming_width = txn.get_streaming_width();
        const uint32_t normal_write_length = streaming_width == 0 ? data_length
                                                                  : std::min(data_length, streaming_width);
        const uint32_t write_len = is_combined ? streaming_width : normal_write_length;
        if (data == nullptr && (command == tlm::TLM_WRITE_COMMAND || uses_offset || write_len != 0)) {
            SCP_WARN(())("b_transport: null data pointer (slave 0x{:02x})", slave_address);
            txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        // A one-byte offset write selects the next read position. A one-byte
        // value write stores its byte at offset zero. The address-only command
        // identifies the offset write.
        if (uses_offset) {
            if (write_len == 0) {
                SCP_WARN(())("b_transport: empty offset write (slave 0x{:02x})", slave_address);
                txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
                return;
            }
            i2c_slave.offset = data[0];
            txn.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }

        // A one-byte write is value-only; longer writes start with an offset.
        if (command == tlm::TLM_WRITE_COMMAND) {
            if (write_len == 1) {
                i2c_slave.data[0] = data[0];
                SCP_DEBUG(())("WRITE slave 0x{:02x} value 0x{:02x}", slave_address, data[0]);
            } else {
                write(slave_address, i2c_slave, data, write_len);
            }
            // Apply the write bytes, then return bytes from the selected offset.
        } else if (is_combined) {
            write(slave_address, i2c_slave, data, write_len);
            read(slave_address, i2c_slave, i2c_slave.offset, txn.get_data_length());
            // Return bytes from the current read position.
        } else if (command == tlm::TLM_READ_COMMAND) {
            read(slave_address, i2c_slave, i2c_slave.offset, txn.get_data_length());
            // Reject commands outside the supported I2C operations.
        } else {
            SCP_WARN(())
            ("b_transport: unsupported command {} (slave 0x{:02x})", static_cast<int>(command), slave_address);
            txn.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

private:
    /** Select an offset and write the remaining bytes. */
    void write(uint8_t slave_address, slave& i2c_slave, const uint8_t* data, uint32_t data_length)
    {
        if (data_length == 0) return;

        i2c_slave.offset = data[0];
        for (uint32_t i = 1; i < data_length; i++) {
            i2c_slave.data[static_cast<uint8_t>(i2c_slave.offset + (i - 1))] = data[i];
        }
        SCP_DEBUG(())
        ("WRITE slave 0x{:02x} offset 0x{:02x} ({} data byte(s))", slave_address, i2c_slave.offset, data_length - 1);
    }

    /** Return bytes starting at an offset. */
    void read(uint8_t slave_address, slave& i2c_slave, uint8_t offset, uint32_t data_length)
    {
        if (data_length == 0) return;
        std::vector<uint8_t> data(data_length);
        for (uint32_t i = 0; i < data_length; i++) {
            data[i] = i2c_slave.data[static_cast<uint8_t>(offset + i)];
        }

        tlm::tlm_generic_payload r;
        r.set_address(slave_address);
        r.set_command(tlm::TLM_WRITE_COMMAND);
        r.set_data_ptr(data.data());
        r.set_data_length(data_length);
        r.set_streaming_width(data_length);
        r.set_response_status(tlm::TLM_OK_RESPONSE);

        i2c_slave.socket->set_default_txn(r);
        for (uint8_t value : data) i2c_slave.socket->enqueue(value);
        i2c_slave.offset = static_cast<uint8_t>(offset + data_length);
        SCP_DEBUG(())("REPLY slave 0x{:02x} offset 0x{:02x} len {}", slave_address, offset, data_length);
    }
};

/** Register the backend with the qbox dynamic-module factory. */
extern "C" void module_register();

#endif // _GS_I2C_MULTI_SLAVE_BACKEND_H_
