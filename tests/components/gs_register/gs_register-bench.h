/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */


#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include "gs_memory.h"
#include "reg_router.h"
#include "registers.h"
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>
#include <vector>

class RegisterTestBench : public TestBench
{
public:
    SCP_LOGGER();

protected:
    InitiatorTester m_initiator;
    gs::gs_memory<> m_reg_memory;
    gs::reg_router<> m_reg_router;
    gs::gs_register<uint32_t> FIFO0;
    gs::gs_register<uint32_t> CMD0;
    gs::gs_field<uint32_t> CMD0_OPCODE;
    gs::gs_field<uint32_t> CMD0_PARAM;
    gs::gs_register<uint64_t> STATUS64;
    uint32_t last_written_value;
    uint64_t last_written_idx;
    uint32_t last_used_mask;
    std::vector<uint32_t> last_read_vec;
    bool is_array;

public:
    RegisterTestBench(const sc_core::sc_module_name& n);
    virtual ~RegisterTestBench() {}

protected:
    void end_of_elaboration();
};