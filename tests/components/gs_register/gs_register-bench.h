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
    gs::gs_register<gs::gs_bank_of<uint32_t>> BANK1D;
    gs::gs_register<gs::gs_bank_of<uint32_t>> BANK_GAP;
    gs::gs_register<gs::gs_bank_of<uint32_t>> BANK2D;
    gs::gs_register<gs::gs_bank_of<uint64_t>> BANK64;
    gs::gs_register<uint32_t> ALIAS_A;
    gs::gs_register<uint32_t> ALIAS_B;
    gs::gs_register<gs::gs_bank_of<uint32_t>> BANK3D_A;
    gs::gs_register<gs::gs_bank_of<uint32_t>> BANK3D_B;
    gs::gs_register<uint32_t> LOWPRI;
    gs::gs_register<gs::gs_bank_of<uint32_t>> SHADOW;
    gs::gs_field<uint32_t> BANK1D_STATUS_DESC;
    gs::gs_field<uint32_t> BANK2D_STATUS;
    gs::gs_field<uint32_t> BANK2D_PAYLOAD;
    gs::gs_field<uint32_t> BANK2D_STATUS_DESC;
    gs::gs_field<uint64_t> BANK64_FULL;
    gs::gs_field<uint32_t> B3D_LO;
    gs::gs_field<uint32_t> B3D_HI;

    uint32_t last_written_value;
    uint64_t last_written_idx;
    uint32_t last_used_mask;
    std::vector<uint32_t> last_read_vec;
    bool is_array;

    // Bank callback observation.
    int bank1d_pre_writes;
    int bank1d_post_writes;
    int bank1d_pre_reads;
    int bank_gap_pre_writes;
    int bank2d_post_writes;
    uint64_t bank2d_last_write_addr;
    int alias_a_hits;
    int alias_b_hits;
    int bank3d_a_hits;
    int bank3d_b_hits;
    uint64_t bank3d_a_last_rel_addr;
    uint64_t bank3d_b_last_rel_addr;
    int lowpri_hits;
    int shadow_hits;
    uint64_t shadow_last_rel_addr;

public:
    RegisterTestBench(const sc_core::sc_module_name& n);
    virtual ~RegisterTestBench() {}

protected:
    void end_of_elaboration();
};
