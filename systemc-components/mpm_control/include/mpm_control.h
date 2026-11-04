/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <ports/initiator-signal-socket.h>
#include <module_factory_container.h>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>

#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

#define REGION_SIZE 0x1000
#define REGION_MASK REGION_SIZE - 1

enum {
    MPM_CONTROL_CNTCR = 0x0,
    MPM_CONTROL_CNTSR = 0x4,
    MPM_CONTROL_CNTCV_L = 0x8,
    MPM_CONTROL_CNTCV_HI = 0xC,
    MPM_CONTROL_CNTFID0 = 0x20,
    MPM_CONTROL_ID = 0xFD0,
};

class mpm_control : public sc_core::sc_module
{
private:
    sc_core::sc_event update_ev;

public:
    tlm_utils::simple_target_socket<mpm_control, DEFAULT_TLM_BUSWIDTH> socket;

private:
    // Called after dev_write.
    void dev_update() {}

    // Explicit designated initializers are not needed but can insure
    // more reliable initial state.
    uint32_t register_range[REGION_SIZE / 4];

    void dev_write(uint64_t offset, uint32_t val, unsigned size)
    {
        SCP_DEBUG(SCMOD) << std::hex << "write offset " << offset;
        uint32_t reg_index = offset / 4;
        switch (offset) {
        case MPM_CONTROL_CNTCR:
            register_range[reg_index] = val;
            break;
        case MPM_CONTROL_CNTSR:
            SCP_WARN(SCMOD) << "Read Only Register MPM_CONTROL_CNTSR";
            break;
        case MPM_CONTROL_CNTCV_L:
            register_range[reg_index] = val;
            break;
        case MPM_CONTROL_CNTCV_HI:
            register_range[reg_index] = val;
            break;
        case MPM_CONTROL_CNTFID0:
            register_range[reg_index] = val;
            break;
        case MPM_CONTROL_ID:
            SCP_WARN(SCMOD) << "Read Only Register MPM_CONTROL_ID";
            break;
        default:
            SCP_WARN(SCMOD) << "Unimplemented write";
            break;
        }

        update_ev.notify();
    }

    uint32_t dev_read(uint64_t offset, unsigned size)
    {
        SCP_DEBUG(SCMOD) << std::hex << "read offset " << offset;
        double time = sc_core::sc_time_stamp().to_seconds();
        uint64_t clocks = time * register_range[MPM_CONTROL_CNTFID0 / 4];
        uint32_t reg_index = offset / 4;
        switch (offset) {
        case MPM_CONTROL_CNTCR:
            return *(register_range + reg_index);
            break;
        case MPM_CONTROL_CNTSR:
            return *(register_range + reg_index);
            break;
        case MPM_CONTROL_CNTCV_L:
            register_range[reg_index] = (uint32_t)clocks;
            return register_range[reg_index];
            break;
        case MPM_CONTROL_CNTCV_HI:
            register_range[reg_index] = (uint32_t)(clocks >> 32);
            return register_range[reg_index];
            break;
        case MPM_CONTROL_CNTFID0:
            return *(register_range + reg_index);
            break;
        case MPM_CONTROL_ID:
            return *(register_range + reg_index);
            break;
        default:
            SCP_WARN(SCMOD) << "Unimplemented read";
            return 0x0;
        }
        return 0;
    }

    void dev_reset()
    {
        memset(register_range, 0, sizeof(register_range));
        register_range[MPM_CONTROL_CNTFID0 / 4] = 0x124F800;
        register_range[MPM_CONTROL_ID / 4] = 0x10000000;
        dev_update();
    }

    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        unsigned int len = txn.get_data_length();
        unsigned char* ptr = txn.get_data_ptr();
        sc_dt::uint64 addr = txn.get_address();
        assert(len == 4);

        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND: {
            uint32_t data = dev_read(addr & REGION_MASK, len);
            memcpy(ptr, &data, len);
            SCP_INFO(SCMOD) << "b_transport read " << std::hex << addr << " " << data;
            break;
        }
        case tlm::TLM_WRITE_COMMAND: {
            uint32_t data;
            memcpy(&data, ptr, len);
            SCP_INFO(SCMOD) << "b_transport write " << std::hex << addr << " " << data;
            dev_write(addr & REGION_MASK, data, len);
            break;
        }
        default:
            SCP_ERR(SCMOD) << "TLM command not supported";
            break;
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);
    }

public:
    mpm_control(sc_core::sc_module_name name): socket("socket")
    {
        socket.register_b_transport(this, &mpm_control::b_transport);
        SC_THREAD(dev_reset);

        SC_METHOD(dev_update);
        sensitive << update_ev;
    }
};

extern "C" void module_register();
