// Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef MMIO_PROBE_H
#define MMIO_PROBE_H

#include <cstring>

#include <ports/initiator-signal-socket.h>
#include <router.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_sockets_buswidth.h>

// Requires TEST_FAIL macro from caller (e.g., test/test.h)

struct MmioReader {
    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) = 0;
};

struct MmioWriter {
    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) = 0;
};

class MmioProbe : public sc_core::sc_module
{
public:
    static constexpr uint64_t MMIO_ADDR = 0x80000000;
    static constexpr size_t MMIO_SIZE = 1024;

    enum SocketId {
        SOCKET_MMIO = 0,
    };

private:
    MmioReader* mmio_reader = nullptr;
    MmioWriter* mmio_writer = nullptr;

    tlm_utils::simple_target_socket_tagged<MmioProbe, DEFAULT_TLM_BUSWIDTH> socket;

    void b_transport(int id, tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        const uint64_t addr = txn.get_address();
        const size_t len = txn.get_data_length();

        uint64_t data = 0;
        uint64_t* ptr = reinterpret_cast<uint64_t*>(txn.get_data_ptr());

        if (len > 8) {
            TEST_FAIL("Unsupported b_transport data length");
        }

        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND:
            if (mmio_reader) {
                data = mmio_reader->mmio_read(id, addr, len);
                std::memcpy(ptr, &data, len);
            }
            break;
        case tlm::TLM_WRITE_COMMAND:
            if (mmio_writer) {
                std::memcpy(&data, ptr, len);
                mmio_writer->mmio_write(id, addr, *ptr, len);
            }
            break;
        default:
            TEST_FAIL("TLM command not supported");
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

public:
    MmioProbe(const sc_core::sc_module_name& module_name, gs::router<>& router): sc_core::sc_module(module_name)
    {
        socket.register_b_transport(this, &MmioProbe::b_transport, SOCKET_MMIO);
        router.add_target(socket, MMIO_ADDR, MMIO_SIZE);
    }

    void connect_to_mmio_reader(MmioReader* reader) { mmio_reader = reader; }

    void connect_to_mmio_writer(MmioWriter* writer) { mmio_writer = writer; }
};

#endif
