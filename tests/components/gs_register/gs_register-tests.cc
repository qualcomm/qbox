/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "gs_register-bench.h"
#include <cci/utils/broker.h>

#define REG_MEM_ADDR  0x0UL
#define REG_MEM_SZ    0x10000UL
#define FIFO0_ADDR    0x100UL
#define FIFO0_LEN     16UL
#define CMD0_ADDR     0x0UL
#define CMD0_LEN      1UL
#define STATUS64_ADDR 0x200UL
#define STATUS64_LEN  1UL
#define BANK64_ADDR   0x280UL

#define BANK1D_ADDR   0x300UL
#define BANK1D_LEN    4UL
#define BANK1D_STRIDE 4UL

#define BANK_GAP_ADDR   0x400UL
#define BANK_GAP_LEN    4UL
#define BANK_GAP_STRIDE 8UL

#define BANK2D_ADDR       0x500UL
#define BANK2D_ROWS       3UL
#define BANK2D_COLS       4UL
#define BANK2D_ROW_STRIDE 0x10UL
#define BANK2D_COL_STRIDE 0x4UL

#define ALIAS_ADDR 0x800UL

#define BANK3D_A_ADDR 0x900UL
#define BANK3D_B_ADDR 0x904UL
#define BANK3D_D0     2UL
#define BANK3D_D1     2UL
#define BANK3D_D2     2UL
#define BANK3D_S0     0x40UL
#define BANK3D_S1     0x10UL
#define BANK3D_S2     0x8UL

#define LOWPRI_ADDR ALIAS_ADDR

#define SHADOW_ADDR   0x0UL
#define SHADOW_LEN    0x400UL
#define SHADOW_STRIDE 4UL

RegisterTestBench::RegisterTestBench(const sc_core::sc_module_name& n)
    : TestBench(n)
    , m_initiator("initiator")
    , m_reg_memory("reg_memory")
    , m_reg_router("reg_router")
    , FIFO0("FIFO0", "FIFO0", FIFO0_ADDR, FIFO0_LEN)
    , CMD0("CMD0", "CMD0", CMD0_ADDR, CMD0_LEN)
    , CMD0_OPCODE(CMD0, CMD0.get_regname() + ".OPCODE", 27UL, 5UL)
    , CMD0_PARAM(CMD0, CMD0.get_regname() + ".PARAM", 0UL, 27UL)
    , STATUS64("STATUS64", "STATUS64", STATUS64_ADDR, STATUS64_LEN)
    , BANK1D("BANK1D", "BANK1D", BANK1D_ADDR, BANK1D_LEN, BANK1D_STRIDE)
    , BANK_GAP("BANK_GAP", "BANK_GAP", BANK_GAP_ADDR, BANK_GAP_LEN, BANK_GAP_STRIDE)
    , BANK2D("BANK2D", "BANK2D", BANK2D_ADDR, std::vector<uint64_t>{ BANK2D_ROWS, BANK2D_COLS },
             std::vector<uint64_t>{ BANK2D_ROW_STRIDE, BANK2D_COL_STRIDE })
    , BANK64("BANK64", "BANK64", BANK64_ADDR, 1UL)
    , ALIAS_A("ALIAS_A", "ALIAS_A", ALIAS_ADDR, 1UL)
    , ALIAS_B("ALIAS_B", "ALIAS_B", ALIAS_ADDR, 1UL)
    , BANK3D_A("BANK3D_A", "BANK3D_A", BANK3D_A_ADDR, std::vector<uint64_t>{ BANK3D_D0, BANK3D_D1, BANK3D_D2 },
               std::vector<uint64_t>{ BANK3D_S0, BANK3D_S1, BANK3D_S2 })
    , BANK3D_B("BANK3D_B", "BANK3D_B", BANK3D_B_ADDR, std::vector<uint64_t>{ BANK3D_D0, BANK3D_D1, BANK3D_D2 },
               std::vector<uint64_t>{ BANK3D_S0, BANK3D_S1, BANK3D_S2 })
    , LOWPRI("LOWPRI", "LOWPRI", LOWPRI_ADDR, 1UL)
    , SHADOW("SHADOW", "SHADOW", SHADOW_ADDR, SHADOW_LEN, SHADOW_STRIDE)
    , BANK1D_STATUS_DESC(BANK1D, "BANK1D_STATUS_DESC", 8UL, 4UL)
    , BANK2D_STATUS(BANK2D, "BANK2D_STATUS", 0UL, 4UL)
    , BANK2D_PAYLOAD(BANK2D, "BANK2D_PAYLOAD", 4UL, 24UL)
    , BANK2D_STATUS_DESC(BANK2D, "BANK2D_STATUS_DESC", 0UL, 4UL)
    , BANK64_FULL(BANK64, "BANK64_FULL", 0UL, 64UL)
    , B3D_LO(BANK3D_A, "B3D_LO", 0UL, 8UL)
    , B3D_HI(BANK3D_A, "B3D_HI", 8UL, 16UL)
    , last_written_value(0)
    , last_written_idx(0)
    , last_used_mask(gs::gs_full_mask<uint32_t>())
    , is_array(false)
    , bank1d_pre_writes(0)
    , bank1d_post_writes(0)
    , bank1d_pre_reads(0)
    , bank_gap_pre_writes(0)
    , bank2d_post_writes(0)
    , bank2d_last_write_addr(0)
    , alias_a_hits(0)
    , alias_b_hits(0)
    , bank3d_a_hits(0)
    , bank3d_b_hits(0)
    , bank3d_a_last_rel_addr(0)
    , bank3d_b_last_rel_addr(0)
    , lowpri_hits(0)
    , shadow_hits(0)
    , shadow_last_rel_addr(0)
{
    m_initiator.socket.bind(m_reg_router.target_socket);

    m_reg_router.initiator_socket.bind(m_reg_memory.socket);

    FIFO0.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(FIFO0);
    m_reg_router.rename_last(std::string(this->name()) + ".FIFO0.target_socket");

    CMD0.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(CMD0);
    m_reg_router.rename_last(std::string(this->name()) + ".CMD0.target_socket");

    STATUS64.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(STATUS64);
    m_reg_router.rename_last(std::string(this->name()) + ".STATUS64.target_socket");

    BANK64.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK64);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK64.target_socket");

    BANK1D.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK1D);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK1D.target_socket");

    BANK_GAP.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK_GAP);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK_GAP.target_socket");

    BANK2D.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK2D);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK2D.target_socket");

    ALIAS_A.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(ALIAS_A);
    m_reg_router.rename_last(std::string(this->name()) + ".ALIAS_A.target_socket");

    ALIAS_B.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(ALIAS_B);
    m_reg_router.rename_last(std::string(this->name()) + ".ALIAS_B.target_socket");

    BANK3D_A.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK3D_A);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK3D_A.target_socket");

    BANK3D_B.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(BANK3D_B);
    m_reg_router.rename_last(std::string(this->name()) + ".BANK3D_B.target_socket");

    LOWPRI.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(LOWPRI);
    m_reg_router.rename_last(std::string(this->name()) + ".LOWPRI.target_socket");

    SHADOW.initiator_socket.bind(m_reg_memory.socket);
    m_reg_router.initiator_socket.bind(SHADOW);
    m_reg_router.rename_last(std::string(this->name()) + ".SHADOW.target_socket");
}

void RegisterTestBench::end_of_elaboration()
{
    // first post_write registered callback
    FIFO0.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        if (!is_array) {
            if (last_used_mask == gs::gs_full_mask<uint32_t>()) {
                ASSERT_EQ(FIFO0[last_written_idx], last_written_value);
            } else {
                uint32_t read_value = 0;
                FIFO0.get(&read_value, last_written_idx, 1ULL);
                ASSERT_EQ(read_value, last_written_value);
            }
        }
    });

    // second post_write registered callback
    FIFO0.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        if (is_array) {
            std::vector<uint32_t> curr_data(16, 0);
            FIFO0.get(curr_data.data(), 0, 16UL);
            for (uint32_t i = 0; i < curr_data.size(); i++) {
                ASSERT_EQ(curr_data[i], last_read_vec[i]);
            }
        }
    });

    // pre_write registered callback
    FIFO0.pre_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        if (is_array) {
            std::vector<uint32_t> updated_data(16, 0xCDCDCDCDUL);
            FIFO0.set_mask(0xFFFFFFFFUL);
            FIFO0.set(updated_data.data(), 0, 16UL);
            FIFO0.set_mask(last_used_mask);
        }
    });

    CMD0.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        uint32_t reg_value = CMD0;
        ASSERT_EQ(reg_value, last_written_value);
    });

    CMD0.pre_read([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { CMD0 += 1; });

    CMD0.post_read([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { CMD0 *= 2; });

    STATUS64.pre_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        STATUS64.set_mask(0x0ULL); /*Read Only*/
    });

    BANK1D.pre_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { bank1d_pre_writes++; });
    BANK1D.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        bank1d_post_writes++;
        auto a = BANK1D.decode_access(trans);
        ASSERT_TRUE(static_cast<bool>(a));
        ASSERT_EQ(a.indices.size(), 1u);
    });
    BANK1D.pre_read([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { bank1d_pre_reads++; });

    BANK_GAP.pre_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { bank_gap_pre_writes++; });

    BANK2D.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        bank2d_post_writes++;
        bank2d_last_write_addr = trans.get_address();
    });

    ALIAS_A.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { alias_a_hits++; });
    ALIAS_B.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { alias_b_hits++; });

    // trans.get_address() inside the callback is the router-stripped relative
    // offset (use_offset=true), so we record it as-is for later assertions.
    BANK3D_A.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        bank3d_a_hits++;
        bank3d_a_last_rel_addr = trans.get_address();
    });
    BANK3D_B.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        bank3d_b_hits++;
        bank3d_b_last_rel_addr = trans.get_address();
    });

    LOWPRI.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) { lowpri_hits++; });

    SHADOW.post_write([&](tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        shadow_hits++;
        shadow_last_rel_addr = trans.get_address();
    });
}

TEST_BENCH(RegisterTestBench, test_registers)
{
    uint32_t write_value = 0UL;
    SCP_DEBUG(()) << "****************Testing register array****************" << std::endl;
    SCP_DEBUG(()) << "Assert FIFO0 array register default mask is 0xFFFFFFFF";
    ASSERT_EQ(FIFO0.get_mask(), 0xFFFFFFFFUL);

    SCP_DEBUG(()) << "write 1 value to the register array with default mask";
    write_value = 0xABABABABUL;
    last_written_idx = 0;
    last_written_value = write_value;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR, write_value);

    last_written_idx = 1UL;
    write_value = 0xCDCDCDCDUL;
    last_written_value = write_value;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR + 4UL, write_value);

    SCP_DEBUG(()) << "write 1 value to the register array with mask";
    write_value = 0xEFEFEFEFUL;
    last_written_idx = 0UL;
    last_used_mask = 0x00FF00FFUL;
    FIFO0.set_mask(last_used_mask);
    last_written_value = 0xABEFABEFUL;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR, write_value);

    write_value = 0xEFEFEFEFUL;
    last_written_idx = 1UL;
    last_used_mask = 0xFF00FF00UL;
    FIFO0.set_mask(last_used_mask);
    last_written_value = 0xEFCDEFCDUL;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR + 4UL, write_value);

    SCP_DEBUG(()) << "set mask to 0 to test WRITEONCE";
    write_value = 0xEFEFEFEFUL;
    last_written_idx = 1UL;
    last_used_mask = 0x0UL; // latch previous written value (WRITEONCE)
    FIFO0.set_mask(last_used_mask);
    last_written_value = 0xEFCDEFCDUL;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR + 4UL, write_value);

    SCP_DEBUG(()) << "write 2 values to the register array with mask using set() and read using get()";
    std::vector<uint32_t> write_vec(2, 0x10101010UL);
    std::vector<uint32_t> read_vec(2, 0x0UL);
    last_used_mask = 0xFFFFFF00UL;
    FIFO0.set_mask(last_used_mask);
    FIFO0.set(write_vec.data(), 2, 2);
    FIFO0.get(read_vec.data(), 2, 2);
    for (const auto& it : read_vec) {
        ASSERT_EQ(it, 0x10101000UL);
    }

    SCP_DEBUG(()) << "write 2 values to the register array using the initiator socket and a mask";
    write_vec.clear();
    write_vec.resize(FIFO0_LEN);
    last_read_vec.clear();
    last_read_vec.resize(FIFO0_LEN);
    is_array = true;
    last_used_mask = 0xFF00FF00UL;
    FIFO0.set_mask(last_used_mask);
    for (auto& it : write_vec) {
        it = 0xABABABABUL; // write the values with 0xFF00FF00UL first to the memory
    }
    for (auto& it : last_read_vec) {
        it = 0xABCDABCDUL;
    }
    tlm::tlm_generic_payload trans;
    trans.set_address(FIFO0_ADDR);
    trans.set_data_length(FIFO0_LEN * sizeof(uint32_t));
    trans.set_streaming_width(FIFO0_LEN * sizeof(uint32_t));
    trans.set_data_ptr(reinterpret_cast<unsigned char*>(write_vec.data()));
    trans.set_command(tlm::tlm_command::TLM_WRITE_COMMAND);
    trans.set_response_status(tlm::tlm_response_status::TLM_INCOMPLETE_RESPONSE);
    ASSERT_EQ(m_initiator.do_b_transport(trans), tlm::TLM_OK_RESPONSE);

    last_used_mask = 0xFF00FF00UL;
    FIFO0.set_mask(last_used_mask);
    for (auto& it : write_vec) {
        it = 0xEFEFEFEFUL; // write the values with 0xFF00FF00UL first to the memory
    }
    for (auto& it : last_read_vec) {
        it = 0xEFCDEFCDUL;
    }
    ASSERT_EQ(m_initiator.do_b_transport(trans), tlm::TLM_OK_RESPONSE);

    SCP_DEBUG(()) << "****************Testing normal register (not register array case)****************" << std::endl;
    SCP_DEBUG(()) << "Assert CMD0 register default mask is 0xFFFFFFFF";
    ASSERT_EQ(CMD0.get_mask(), 0xFFFFFFFFUL);

    SCP_DEBUG(()) << "Assert STATUS64 register default mask is 0xFFFFFFFFFFFFFFFF";
    ASSERT_EQ(STATUS64.get_mask(), 0xFFFFFFFFFFFFFFFFULL);

    SCP_DEBUG(()) << "write value to the register with default mask";
    write_value = 0xABABABABUL;
    last_written_value = write_value;
    m_initiator.do_write<uint32_t>(CMD0_ADDR, write_value);

    SCP_DEBUG(()) << "write value to register with mask";
    write_value = 0xEFEFEFEFUL;
    last_used_mask = 0x00FF00FFUL;
    CMD0.set_mask(last_used_mask);
    last_written_value = 0xABEFABEFUL;
    m_initiator.do_write<uint32_t>(CMD0_ADDR, write_value);

    SCP_DEBUG(()) << "set mask to 0 to test WRITEONCE";
    write_value = 0xCDCDCDCDUL;
    last_used_mask = 0x0UL; // latch previous written value (WRITEONCE)
    CMD0.set_mask(last_used_mask);
    last_written_value = 0xABEFABEFUL;
    m_initiator.do_write<uint32_t>(CMD0_ADDR, write_value);

    SCP_DEBUG(()) << "test register operations";
    last_used_mask = 0xFFFFFFFFUL;
    CMD0.set_mask(last_used_mask);
    CMD0 = 1UL;
    CMD0 += 1UL;
    uint32_t reg_value = CMD0;
    ASSERT_EQ(reg_value, 2UL);
    CMD0 *= 2;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 4UL);
    CMD0 /= 2;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 2UL);
    CMD0 -= 1;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 1UL);
    CMD0 |= 2;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 3UL);
    CMD0 &= 1;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 1UL);
    CMD0 ^= 0;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 1UL);
    CMD0 <<= 3;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 8UL);
    CMD0 >>= 3;
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 1UL);

    SCP_DEBUG(()) << "test field access";
    CMD0 = 0xABCDEF88;
    reg_value = CMD0[CMD0_PARAM]; // 0 - 27
    ASSERT_EQ(reg_value, 0x3CDEF88UL);

    SCP_DEBUG(()) << "test pre/post read CBs";
    CMD0 = 25UL;
    reg_value = 0;
    m_initiator.do_read<uint32_t>(CMD0_ADDR, reg_value);
    ASSERT_EQ(reg_value, 26UL); // because of pre_read CB
    reg_value = CMD0;
    ASSERT_EQ(reg_value, 52UL); // because of post_read CB

    SCP_DEBUG(()) << "test 64 bit reagister access";
    uint64_t write_value_64 = 0xABABABABCDCDCDCDULL;
    uint64_t mask_64 = gs::gs_full_mask<uint64_t>();
    STATUS64.set_mask(mask_64);
    STATUS64 = write_value_64;
    uint64_t read_value_64 = 0x0ULL;
    m_initiator.do_read<uint64_t>(STATUS64_ADDR, read_value_64);
    ASSERT_EQ(read_value_64, write_value_64);
    uint64_t new_write_value_64 = 0xFF00FF00FF00FF00ULL;
    m_initiator.do_write<uint64_t>(STATUS64_ADDR, new_write_value_64);
    m_initiator.do_read<uint64_t>(STATUS64_ADDR, read_value_64);
    ASSERT_EQ(read_value_64, write_value_64); // 0x0ULL mask will be applied in pre_pread callback, so the value of the
                                              // register shouldn't change at write
}

TEST_BENCH(RegisterTestBench, test_registers_bank)
{
    SCP_DEBUG(()) << "**************** Register bank: shape metadata ****************";
    ASSERT_EQ(BANK1D.get_regname(), "BANK1D");
    ASSERT_EQ(BANK1D.get_offset(), BANK1D_ADDR);
    ASSERT_EQ(BANK1D.get_dims(), 1u);
    ASSERT_EQ(BANK1D.get_dim_count(0), BANK1D_LEN);
    ASSERT_EQ(BANK1D.get_dim_stride(0), BANK1D_STRIDE);
    ASSERT_EQ(BANK1D.get_total_count(), BANK1D_LEN);
    // span = (n-1)*stride + sizeof(elem) = 3*4 + 4 = 16
    ASSERT_EQ(BANK1D.get_size(), 3UL * BANK1D_STRIDE + sizeof(uint32_t));
    ASSERT_EQ(BANK1D.get_mask(), gs::gs_full_mask<uint32_t>());

    ASSERT_EQ(BANK_GAP.get_dim_stride(0), BANK_GAP_STRIDE);
    // gapped span = 3*8 + 4 = 28
    ASSERT_EQ(BANK_GAP.get_size(), 3UL * BANK_GAP_STRIDE + sizeof(uint32_t));

    ASSERT_EQ(BANK2D.get_dims(), 2u);
    ASSERT_EQ(BANK2D.get_dim_count(0), BANK2D_ROWS);
    ASSERT_EQ(BANK2D.get_dim_count(1), BANK2D_COLS);
    ASSERT_EQ(BANK2D.get_dim_stride(0), BANK2D_ROW_STRIDE);
    ASSERT_EQ(BANK2D.get_dim_stride(1), BANK2D_COL_STRIDE);
    ASSERT_EQ(BANK2D.get_total_count(), BANK2D_ROWS * BANK2D_COLS);
    // span = (rows-1)*row_stride + (cols-1)*col_stride + sizeof(elem)
    ASSERT_EQ(BANK2D.get_size(),
              (BANK2D_ROWS - 1) * BANK2D_ROW_STRIDE + (BANK2D_COLS - 1) * BANK2D_COL_STRIDE + sizeof(uint32_t));

    SCP_DEBUG(()) << "**************** BANK1D: element access via cursor ****************";
    for (uint32_t i = 0; i < BANK1D_LEN; i++) {
        BANK1D[i] = 0xDEAD0000UL | i;
    }
    for (uint32_t i = 0; i < BANK1D_LEN; i++) {
        uint32_t v = BANK1D[i];
        ASSERT_EQ(v, 0xDEAD0000UL | i);
    }

    // The corresponding memory bytes must reflect the writes at base + stride*i.
    for (uint32_t i = 0; i < BANK1D_LEN; i++) {
        uint32_t mem_v = 0;
        m_initiator.do_read<uint32_t>(BANK1D_ADDR + BANK1D_STRIDE * i, mem_v);
        ASSERT_EQ(mem_v, 0xDEAD0000UL | i);
    }

    SCP_DEBUG(()) << "**************** BANK1D: initiator writes reach the callback and DMI ****************";
    int prev_pre = bank1d_pre_writes;
    int prev_post = bank1d_post_writes;
    m_initiator.do_write<uint32_t>(BANK1D_ADDR + BANK1D_STRIDE * 2, 0xCAFEBABEUL);
    ASSERT_EQ(bank1d_pre_writes, prev_pre + 1);
    ASSERT_EQ(bank1d_post_writes, prev_post + 1);
    uint32_t v = BANK1D[2];
    ASSERT_EQ(v, 0xCAFEBABEUL);

    SCP_DEBUG(()) << "**************** BANK1D: RMW operators on cursor ****************";
    BANK1D[0] = 10UL;
    BANK1D[0] += 5UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 15UL);
    BANK1D[0] *= 2UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 30UL);
    BANK1D[0] /= 3UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 10UL);
    BANK1D[0] |= 0x1UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 11UL);
    BANK1D[0] &= 0x2UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 2UL);
    BANK1D[0] <<= 2UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 8UL);
    BANK1D[0] >>= 1UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 4UL);
    BANK1D[0] -= 4UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 0UL);
    BANK1D[0] ^= 0xF0F0UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 0xF0F0UL);

    SCP_DEBUG(()) << "**************** BANK1D: bit-field access on cursor ****************";
    // CMD0_PARAM = bits [0, 27), CMD0_OPCODE = bits [27, 32). Reused only for their
    // bit_start/bit_length metadata; they are unrelated to BANK1D otherwise.
    BANK1D[1] = 0UL;
    BANK1D[1][CMD0_PARAM] = 0x123456UL;
    BANK1D[1][CMD0_OPCODE] = 0x1FUL;
    uint32_t full = BANK1D[1];
    ASSERT_EQ(full, (0x1FUL << 27) | 0x123456UL);
    uint32_t param_readback = BANK1D[1][CMD0_PARAM];
    uint32_t opcode_readback = BANK1D[1][CMD0_OPCODE];
    ASSERT_EQ(param_readback, 0x123456UL);
    ASSERT_EQ(opcode_readback, 0x1FUL);

    SCP_DEBUG(()) << "**************** BANK1D: bulk set()/get() (contiguous path) ****************";
    std::vector<uint32_t> src(BANK1D_LEN, 0);
    for (uint32_t i = 0; i < BANK1D_LEN; i++) src[i] = 0xA0A0A000UL + i;
    BANK1D.set(src.data(), 0, BANK1D_LEN);
    std::vector<uint32_t> dst(BANK1D_LEN, 0);
    BANK1D.get(dst.data(), 0, BANK1D_LEN);
    for (uint32_t i = 0; i < BANK1D_LEN; i++) ASSERT_EQ(dst[i], 0xA0A0A000UL + i);

    SCP_DEBUG(()) << "**************** BANK1D: mask / RMW via capture_txn_pre + handle_mask_post ****************";
    // Prime element 3 with a known baseline, then write via initiator with a
    // partial mask; the register must combine incoming bits masked-in with
    // the previously stored bits.
    BANK1D.set_mask(gs::gs_full_mask<uint32_t>());
    BANK1D[3] = 0xAAAAAAAAUL;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[3]), 0xAAAAAAAAUL);
    BANK1D.set_mask(0x00FF00FFUL); // only low bytes of each 16-bit half writable
    m_initiator.do_write<uint32_t>(BANK1D_ADDR + BANK1D_STRIDE * 3, 0x55555555UL);
    // stored[3] = (0xAAAAAAAA & ~0x00FF00FF) | (0x55555555 & 0x00FF00FF)
    //           = 0xAA00AA00 | 0x00550055 = 0xAA55AA55
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[3]), 0xAA55AA55UL);

    SCP_DEBUG(()) << "**************** BANK1D: masked contiguous burst updates every element ****************";
    BANK1D.set_mask(gs::gs_full_mask<uint32_t>());
    BANK1D[0] = 0xAAAAAAAAUL;
    BANK1D[1] = 0xBBBBBBBBUL;
    BANK1D.set_mask(0x00FF00FFUL);
    std::vector<uint32_t> masked_burst_data{ 0x55555555UL, 0x66666666UL };
    tlm::tlm_generic_payload masked_burst;
    masked_burst.set_command(tlm::TLM_WRITE_COMMAND);
    masked_burst.set_address(BANK1D_ADDR);
    masked_burst.set_data_ptr(reinterpret_cast<unsigned char*>(masked_burst_data.data()));
    masked_burst.set_data_length(masked_burst_data.size() * sizeof(uint32_t));
    masked_burst.set_streaming_width(masked_burst.get_data_length());
    masked_burst.set_byte_enable_length(0);
    masked_burst.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    ASSERT_EQ(m_initiator.do_b_transport(masked_burst), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 0xAA55AA55UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[1]), 0xBB66BB66UL);
    BANK1D.set_mask(gs::gs_full_mask<uint32_t>()); // restore

    SCP_DEBUG(()) << "**************** BANK1D: transport_dbg on a bank element ****************";
    BANK1D[0] = 0x11111111UL;
    tlm::tlm_generic_payload dbg;
    uint32_t dbg_val = 0;
    dbg.set_command(tlm::TLM_READ_COMMAND);
    dbg.set_address(BANK1D_ADDR + BANK1D_STRIDE * 0);
    dbg.set_data_length(sizeof(uint32_t));
    dbg.set_data_ptr(reinterpret_cast<unsigned char*>(&dbg_val));
    dbg.set_streaming_width(sizeof(uint32_t));
    unsigned int got = m_initiator.socket->transport_dbg(dbg);
    // transport_dbg on the router hits gs_memory here; either path is fine so
    // long as we read back the stored value.
    ASSERT_GT(got, 0u);
    ASSERT_EQ(dbg_val, 0x11111111UL);

    SCP_DEBUG(()) << "**************** BANK_GAP: element vs gap addresses ****************";
    // Element addresses: 0x400, 0x408, 0x410, 0x418. Gap addresses at +4 within
    // each slot (0x404, 0x40C, 0x414, 0x41C) are inside the bank's span but must
    // not fire the bank callback and must not be treated as an element by
    // byte_offset_is_element / decode_access.
    ASSERT_TRUE(BANK_GAP.byte_offset_is_element(0));
    ASSERT_FALSE(BANK_GAP.byte_offset_is_element(4));
    ASSERT_TRUE(BANK_GAP.byte_offset_is_element(8));
    ASSERT_FALSE(BANK_GAP.byte_offset_is_element(12));

    // Assign each element via cursor.
    for (uint32_t i = 0; i < BANK_GAP_LEN; i++) {
        BANK_GAP[i] = 0xB0B00000UL + i;
    }
    for (uint32_t i = 0; i < BANK_GAP_LEN; i++) {
        uint32_t got_v = BANK_GAP[i];
        ASSERT_EQ(got_v, 0xB0B00000UL + i);
        uint32_t mem_v = 0;
        m_initiator.do_read<uint32_t>(BANK_GAP_ADDR + BANK_GAP_STRIDE * i, mem_v);
        ASSERT_EQ(mem_v, 0xB0B00000UL + i);
    }

    SCP_DEBUG(()) << "**************** BANK_GAP: gap-address writes bypass the bank callback ****************";
    int gap_pre_before = bank_gap_pre_writes;
    // Address 0x404 sits in the gap between elements 0 and 1. reg_router's
    // claims() rejects it for BANK_GAP, so only reg_memory is reached.
    m_initiator.do_write<uint32_t>(BANK_GAP_ADDR + 4UL, 0xDEADBEEFUL);
    ASSERT_EQ(bank_gap_pre_writes, gap_pre_before); // callback did NOT fire
    uint32_t gap_mem = 0;
    m_initiator.do_read<uint32_t>(BANK_GAP_ADDR + 4UL, gap_mem);
    ASSERT_EQ(gap_mem, 0xDEADBEEFUL); // memory still holds the byte

    // The gap write must not perturb bank element 0 (still at its old value).
    uint32_t elem0_after_gap_write = BANK_GAP[0];
    ASSERT_EQ(elem0_after_gap_write, 0xB0B00000UL);

    SCP_DEBUG(()) << "**************** BANK_GAP: masked burst is rejected ****************";
    BANK_GAP.set_mask(gs::gs_full_mask<uint32_t>());
    BANK_GAP[0] = 0xAAAAAAAAUL;
    BANK_GAP[1] = 0xBBBBBBBBUL;
    BANK_GAP.set_mask(0x00FF00FFUL);
    std::vector<uint32_t> gapped_burst_data{ 0x55555555UL, 0x66666666UL };
    tlm::tlm_generic_payload gapped_burst;
    gapped_burst.set_command(tlm::TLM_WRITE_COMMAND);
    gapped_burst.set_address(BANK_GAP_ADDR);
    gapped_burst.set_data_ptr(reinterpret_cast<unsigned char*>(gapped_burst_data.data()));
    gapped_burst.set_data_length(gapped_burst_data.size() * sizeof(uint32_t));
    gapped_burst.set_streaming_width(gapped_burst.get_data_length());
    gapped_burst.set_byte_enable_length(0);
    gapped_burst.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    ASSERT_EQ(m_initiator.do_b_transport(gapped_burst), tlm::TLM_BURST_ERROR_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(BANK_GAP[0]), 0xAAAAAAAAUL);
    ASSERT_EQ(static_cast<uint32_t>(BANK_GAP[1]), 0xBBBBBBBBUL);
    BANK_GAP.set_mask(gs::gs_full_mask<uint32_t>());

    SCP_DEBUG(()) << "**************** BANK_GAP: decode_access on element and gap ****************";
    // p_relative_addresses defaults to true, so decode_access consumes the
    // relative offset - the router strips the base before the callback runs.
    tlm::tlm_generic_payload probe;
    probe.set_address(BANK_GAP_STRIDE * 2);
    auto acc_elem = BANK_GAP.decode_access(probe);
    ASSERT_TRUE(bool(acc_elem));
    ASSERT_EQ(acc_elem.index, 2u);
    ASSERT_EQ(acc_elem.indices.size(), 1u);
    ASSERT_EQ(acc_elem.indices[0], 2u);

    probe.set_address(BANK_GAP_STRIDE * 2 + 4);
    auto acc_gap = BANK_GAP.decode_access(probe);
    ASSERT_FALSE(bool(acc_gap));
    ASSERT_FALSE(acc_gap.aligned);

    SCP_DEBUG(()) << "**************** BANK_GAP: bulk get()/set() (strided path) ****************";
    std::vector<uint32_t> gsrc(BANK_GAP_LEN, 0);
    for (uint32_t i = 0; i < BANK_GAP_LEN; i++) gsrc[i] = 0xC0C00000UL + i;
    BANK_GAP.set(gsrc.data(), 0, BANK_GAP_LEN);
    std::vector<uint32_t> gdst(BANK_GAP_LEN, 0);
    BANK_GAP.get(gdst.data(), 0, BANK_GAP_LEN);
    for (uint32_t i = 0; i < BANK_GAP_LEN; i++) ASSERT_EQ(gdst[i], 0xC0C00000UL + i);

    SCP_DEBUG(()) << "**************** BANK2D: element access via three index forms ****************";
    // Write via bank[i][j], read via bank(i, j) and bank.at({i, j}) - all three
    // must agree, and the underlying memory must be at row-major offset
    // i*row_stride + j*col_stride.
    for (uint32_t i = 0; i < BANK2D_ROWS; i++) {
        for (uint32_t j = 0; j < BANK2D_COLS; j++) {
            BANK2D[i][j] = 0x20000UL | (i << 8) | j;
        }
    }
    for (uint32_t i = 0; i < BANK2D_ROWS; i++) {
        for (uint32_t j = 0; j < BANK2D_COLS; j++) {
            uint32_t expected = 0x20000UL | (i << 8) | j;
            uint32_t via_paren = BANK2D(i, j);
            uint32_t via_at_ilist = BANK2D.at({ (uint64_t)i, (uint64_t)j });
            uint32_t via_at_vec = BANK2D.at(std::vector<uint64_t>{ i, j });
            ASSERT_EQ(via_paren, expected);
            ASSERT_EQ(via_at_ilist, expected);
            ASSERT_EQ(via_at_vec, expected);
            uint32_t mem_v = 0;
            m_initiator.do_read<uint32_t>(BANK2D_ADDR + i * BANK2D_ROW_STRIDE + j * BANK2D_COL_STRIDE, mem_v);
            ASSERT_EQ(mem_v, expected);
        }
    }

    SCP_DEBUG(()) << "**************** BANK2D: alternate write forms are equivalent ****************";
    BANK2D(2, 1) = 0xABCDEF01UL;
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[2][1]), 0xABCDEF01UL);
    BANK2D.at({ 1, 3 }) = 0x02468ACEUL;
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[1][3]), 0x02468ACEUL);

    SCP_DEBUG(()) << "**************** BANK2D: initiator write routes to the bank callback ****************";
    int prev_2d_post = bank2d_post_writes;
    uint64_t target_addr = BANK2D_ADDR + 2 * BANK2D_ROW_STRIDE + 3 * BANK2D_COL_STRIDE;
    m_initiator.do_write<uint32_t>(target_addr, 0xFEEDFACEUL);
    ASSERT_EQ(bank2d_post_writes, prev_2d_post + 1);
    // The callback observes the router-stripped relative address (use_offset).
    ASSERT_EQ(bank2d_last_write_addr, 2 * BANK2D_ROW_STRIDE + 3 * BANK2D_COL_STRIDE);
    ASSERT_EQ(static_cast<uint32_t>(BANK2D(2, 3)), 0xFEEDFACEUL);

    SCP_DEBUG(()) << "**************** BANK2D: byte_offset_for / byte_offset_for_flat ****************";
    {
        uint64_t idx2[2] = { 2, 3 };
        ASSERT_EQ(BANK2D.byte_offset_for(idx2, 2), 2 * BANK2D_ROW_STRIDE + 3 * BANK2D_COL_STRIDE);
        // Row-major flat: (i=2, j=3) -> 2 * COLS + 3 = 11
        ASSERT_EQ(BANK2D.byte_offset_for_flat(2 * BANK2D_COLS + 3), 2 * BANK2D_ROW_STRIDE + 3 * BANK2D_COL_STRIDE);
        ASSERT_TRUE(BANK2D.byte_offset_is_element(0));
        ASSERT_TRUE(BANK2D.byte_offset_is_element((BANK2D_ROWS - 1) * BANK2D_ROW_STRIDE +
                                                  (BANK2D_COLS - 1) * BANK2D_COL_STRIDE));
    }

    SCP_DEBUG(()) << "**************** BANK2D: decode_access returns per-dim indices ****************";
    tlm::tlm_generic_payload probe2d;
    // Relative address into the bank (router strips the base for callbacks).
    probe2d.set_address(1 * BANK2D_ROW_STRIDE + 2 * BANK2D_COL_STRIDE);
    auto acc2d = BANK2D.decode_access(probe2d);
    ASSERT_TRUE(bool(acc2d));
    ASSERT_EQ(acc2d.indices.size(), 2u);
    ASSERT_EQ(acc2d.indices[0], 1u);
    ASSERT_EQ(acc2d.indices[1], 2u);
    // Row-major flat: 1 * COLS + 2 = 6
    ASSERT_EQ(acc2d.index, 1u * BANK2D_COLS + 2u);

    SCP_DEBUG(()) << "**************** BANK2D: bulk get()/set() (row-major flat) ****************";
    const uint64_t total = BANK2D_ROWS * BANK2D_COLS;
    std::vector<uint32_t> b2d_src(total, 0);
    for (uint64_t k = 0; k < total; k++) b2d_src[k] = 0x30000UL + static_cast<uint32_t>(k);
    BANK2D.set(b2d_src.data(), 0, total);
    std::vector<uint32_t> b2d_dst(total, 0);
    BANK2D.get(b2d_dst.data(), 0, total);
    for (uint64_t k = 0; k < total; k++) ASSERT_EQ(b2d_dst[k], 0x30000UL + static_cast<uint32_t>(k));
    // And the per-element positions must match the row-major flat mapping.
    for (uint32_t i = 0; i < BANK2D_ROWS; i++) {
        for (uint32_t j = 0; j < BANK2D_COLS; j++) {
            uint32_t v_cursor = BANK2D[i][j];
            ASSERT_EQ(v_cursor, 0x30000UL + static_cast<uint32_t>(i * BANK2D_COLS + j));
        }
    }

    SCP_DEBUG(()) << "**************** BANK2D: bitfield access on a bank element ****************";
    // Seed element (1,2) with a known value, then read and write fields.
    BANK2D[1][2] = 0x00000000UL;
    // Write STATUS=0xA, PAYLOAD=0x123456 -> bits[27:4]=0x123456, bits[3:0]=0xA
    // stored value = (0x123456 << 4) | 0xA = 0x0123456A
    BANK2D[1][2][BANK2D_STATUS] = 0xAu;
    BANK2D[1][2][BANK2D_PAYLOAD] = 0x123456u;
    uint32_t elem_val = BANK2D[1][2];
    ASSERT_EQ(elem_val, 0x0123456AUL);

    // Read each field back independently.
    uint32_t status_val = BANK2D[1][2][BANK2D_STATUS];
    uint32_t payload_val = BANK2D[1][2][BANK2D_PAYLOAD];
    ASSERT_EQ(status_val, 0xAu);
    ASSERT_EQ(payload_val, 0x123456u);

    SCP_DEBUG(()) << "**************** BANK fields: bank-owned descriptors ****************";
    BANK1D[1] = 0x00000000UL;
    BANK1D[1][BANK1D_STATUS_DESC] = 0xAu;
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[1]), 0x00000A00UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[1][BANK1D_STATUS_DESC]), 0xAu);

    BANK2D[0][3] = 0x00000000UL;
    BANK2D[0][3][BANK2D_STATUS_DESC] = 0x5u;
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[0][3]), 0x5u);
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[0][3][BANK2D_STATUS_DESC]), 0x5u);

    // RMW: update only STATUS, leave PAYLOAD intact.
    BANK2D[1][2][BANK2D_STATUS] = 0x5u;
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[1][2]), 0x01234565UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK2D[1][2][BANK2D_PAYLOAD]), 0x123456u);

    SCP_DEBUG(()) << "**************** BANK64: full-width field access ****************";
    BANK64[0] = 0;
    BANK64[0][BANK64_FULL] = 0xFEDCBA9876543210ULL;
    ASSERT_EQ(static_cast<uint64_t>(BANK64[0]), 0xFEDCBA9876543210ULL);
    ASSERT_EQ(static_cast<uint64_t>(BANK64[0][BANK64_FULL]), 0xFEDCBA9876543210ULL);

    // Same test on a 3D element via BANK3D_A[i][j][k][field].
    BANK3D_A[0][0][0] = 0xDEADC0DEul; // sentinel: must survive adjacent field writes
    BANK3D_A[0][1][0] = 0x00000000UL;
    BANK3D_A[0][1][0][B3D_LO] = 0xABu;
    BANK3D_A[0][1][0][B3D_HI] = 0xCDEFu;
    uint32_t b3d_val = BANK3D_A[0][1][0];
    ASSERT_EQ(b3d_val, 0x00CDEFABUL);
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_A[0][1][0][B3D_LO]), 0xABu);
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_A[0][1][0][B3D_HI]), 0xCDEFu);

    // Neighbouring element must be untouched by the field writes above.
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_A[0][0][0]), 0xDEADC0DEul);

    SCP_DEBUG(()) << "**************** Router: fan-out to two callbacks at the same address ****************";
    // ALIAS_A and ALIAS_B are both cb_targets at ALIAS_ADDR with default priority
    // 0. reg_router's do_callbacks must invoke BOTH per transaction (once at
    // pre-, once at post-, so each register's post_write CB fires once per
    // initiator write). LOWPRI shares the address at priority 1 and is filtered
    // out - the router keeps only the strongest-priority claimants.
    int a_before = alias_a_hits;
    int b_before = alias_b_hits;
    int lowpri_before = lowpri_hits;
    m_initiator.do_write<uint32_t>(ALIAS_ADDR, 0x77777777UL);
    ASSERT_EQ(alias_a_hits, a_before + 1);
    ASSERT_EQ(alias_b_hits, b_before + 1);
    ASSERT_EQ(lowpri_hits, lowpri_before); // priority-1 target must not fire
    m_initiator.do_write<uint32_t>(ALIAS_ADDR, 0x88888888UL);
    ASSERT_EQ(alias_a_hits, a_before + 2);
    ASSERT_EQ(alias_b_hits, b_before + 2);
    ASSERT_EQ(lowpri_hits, lowpri_before);
}

TEST_BENCH(RegisterTestBench, test_registers_bank_extras)
{
    SCP_DEBUG(()) << "**************** per-element masks: flat and multidimensional APIs ****************";
    const uint32_t full_mask = gs::gs_full_mask<uint32_t>();
    const uint32_t low_half_mask = 0x0000FFFFUL;
    const uint32_t high_half_mask = 0xFFFF0000UL;

    ASSERT_EQ(BANK1D.get_element_mask(0), full_mask);
    BANK1D[0] = 0xAAAAAAAAUL;
    BANK1D[1] = 0xBBBBBBBBUL;
    BANK1D.set_element_mask(0, low_half_mask);
    BANK1D.set_element_mask({ 1 }, high_half_mask);
    ASSERT_EQ(BANK1D.get_element_mask(0), low_half_mask);
    ASSERT_EQ(BANK1D.get_element_mask({ 1 }), high_half_mask);
    m_initiator.do_write<uint32_t>(BANK1D_ADDR, 0x55555555UL);
    m_initiator.do_write<uint32_t>(BANK1D_ADDR + BANK1D_STRIDE, 0x66666666UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[0]), 0xAAAA5555UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK1D[1]), 0x6666BBBBUL);
    BANK1D.clear_element_mask(0);
    BANK1D.clear_element_mask({ 1 });
    ASSERT_EQ(BANK1D.get_element_mask(0), full_mask);
    ASSERT_EQ(BANK1D.get_element_mask(1), full_mask);

    BANK_GAP[1] = 0xAAAAAAAAUL;
    BANK_GAP.set_element_mask(1, low_half_mask);
    m_initiator.do_write<uint32_t>(BANK_GAP_ADDR + BANK_GAP_STRIDE, 0x55555555UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK_GAP[1]), 0xAAAA5555UL);
    BANK_GAP.clear_element_mask(1);

    BANK2D(1, 2) = 0xAAAAAAAAUL;
    std::vector<uint64_t> bank2d_indices{ 1, 2 };
    BANK2D.set_element_mask(bank2d_indices, low_half_mask);
    ASSERT_EQ(BANK2D.get_element_mask({ 1, 2 }), low_half_mask);
    const uint64_t bank2d_flat_index = 1 * BANK2D_COLS + 2;
    ASSERT_EQ(BANK2D.get_element_mask(bank2d_flat_index), low_half_mask);
    const uint64_t bank2d_addr = BANK2D_ADDR + BANK2D_ROW_STRIDE + 2 * BANK2D_COL_STRIDE;
    m_initiator.do_write<uint32_t>(bank2d_addr, 0x55555555UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK2D(1, 2)), 0xAAAA5555UL);
    BANK2D.clear_element_mask(bank2d_indices);
    ASSERT_EQ(BANK2D.get_element_mask(bank2d_flat_index), full_mask);

    SCP_DEBUG(()) << "**************** 3D banks: shape and layout constraint ****************";
    // Sanity: both banks are 3D, 2x2x2, and share the same stride triple.
    ASSERT_EQ(BANK3D_A.get_dims(), 3u);
    ASSERT_EQ(BANK3D_B.get_dims(), 3u);
    ASSERT_EQ(BANK3D_A.get_dim_count(0), BANK3D_D0);
    ASSERT_EQ(BANK3D_A.get_dim_count(1), BANK3D_D1);
    ASSERT_EQ(BANK3D_A.get_dim_count(2), BANK3D_D2);
    ASSERT_EQ(BANK3D_A.get_dim_stride(0), BANK3D_S0);
    ASSERT_EQ(BANK3D_A.get_dim_stride(1), BANK3D_S1);
    ASSERT_EQ(BANK3D_A.get_dim_stride(2), BANK3D_S2);
    // Span = (D0-1)*S0 + (D1-1)*S1 + (D2-1)*S2 + sizeof(elem)
    const uint64_t bank3d_span = (BANK3D_D0 - 1) * BANK3D_S0 + (BANK3D_D1 - 1) * BANK3D_S1 +
                                 (BANK3D_D2 - 1) * BANK3D_S2 + sizeof(uint32_t);
    ASSERT_EQ(BANK3D_A.get_size(), bank3d_span);
    ASSERT_EQ(BANK3D_B.get_size(), bank3d_span);

    SCP_DEBUG(()) << "**************** 3D banks: address ranges overlap, element sets are disjoint ****************";
    // Range overlap check: A ends at 0x900+0x5C=0x95C, B starts at 0x904, so
    // 0x904..0x95C is physically shared. Element sets, however, don't collide:
    // A element (0,0,0) occupies bytes 0x900..0x903 and B element (0,0,0)
    // occupies bytes 0x904..0x907, and so on. reg_router's claims() rejects
    // any address that maps to a bank's inter-element gap, so every element
    // address belongs to exactly one bank.
    ASSERT_LT(BANK3D_B_ADDR, BANK3D_A_ADDR + bank3d_span); // ranges overlap
    ASSERT_LT(BANK3D_A_ADDR, BANK3D_B_ADDR + bank3d_span);

    // A claims its own element addresses and rejects B's.
    for (uint64_t i = 0; i < BANK3D_D0; i++) {
        for (uint64_t j = 0; j < BANK3D_D1; j++) {
            for (uint64_t k = 0; k < BANK3D_D2; k++) {
                uint64_t a_rel = i * BANK3D_S0 + j * BANK3D_S1 + k * BANK3D_S2;
                ASSERT_TRUE(BANK3D_A.byte_offset_is_element(a_rel));
                // B's element offsets, expressed relative to A, sit +4 bytes off
                // A's element positions - inside A's span but off-element for A.
                uint64_t b_rel_via_a = a_rel + (BANK3D_B_ADDR - BANK3D_A_ADDR);
                if (b_rel_via_a < BANK3D_A.get_size()) {
                    ASSERT_FALSE(BANK3D_A.byte_offset_is_element(b_rel_via_a));
                }
                ASSERT_TRUE(BANK3D_B.byte_offset_is_element(a_rel));
            }
        }
    }

    SCP_DEBUG(()) << "**************** 3D banks: writes reach the correct bank, never the other ****************";
    // Prime both banks with mutually-distinct patterns then verify each bank's
    // read-back mirrors only its own writes.
    for (uint64_t i = 0; i < BANK3D_D0; i++) {
        for (uint64_t j = 0; j < BANK3D_D1; j++) {
            for (uint64_t k = 0; k < BANK3D_D2; k++) {
                BANK3D_A[i][j][k] = 0xAAAA0000UL | (i << 8) | (j << 4) | k;
                BANK3D_B[i][j][k] = 0xBBBB0000UL | (i << 8) | (j << 4) | k;
            }
        }
    }
    for (uint64_t i = 0; i < BANK3D_D0; i++) {
        for (uint64_t j = 0; j < BANK3D_D1; j++) {
            for (uint64_t k = 0; k < BANK3D_D2; k++) {
                uint32_t va = BANK3D_A(i, j, k);
                uint32_t vb = BANK3D_B(i, j, k);
                ASSERT_EQ(va, 0xAAAA0000UL | (i << 8) | (j << 4) | k);
                ASSERT_EQ(vb, 0xBBBB0000UL | (i << 8) | (j << 4) | k);
                // And each element sits at a physically distinct 4-byte slot in
                // memory (A on even 4-byte boundaries within its stride mesh,
                // B on the neighbouring odd 4-byte boundaries).
                uint32_t mem_a = 0, mem_b = 0;
                m_initiator.do_read<uint32_t>(BANK3D_A_ADDR + i * BANK3D_S0 + j * BANK3D_S1 + k * BANK3D_S2, mem_a);
                m_initiator.do_read<uint32_t>(BANK3D_B_ADDR + i * BANK3D_S0 + j * BANK3D_S1 + k * BANK3D_S2, mem_b);
                ASSERT_EQ(mem_a, 0xAAAA0000UL | (i << 8) | (j << 4) | k);
                ASSERT_EQ(mem_b, 0xBBBB0000UL | (i << 8) | (j << 4) | k);
            }
        }
    }

    SCP_DEBUG(()) << "**************** 3D banks: initiator writes route to exactly one bank ****************";
    // Write A(1,0,1): abs = 0x900 + 0x40 + 0 + 8 = 0x948. B does NOT claim 0x948
    // (offset 0x44 into B: 0x44 % 0x40 = 0x4, 0x4 % 0x10 = 0x4, 0x4 % 8 = 0x4 >=
    // elem_size), so only A fires.
    int a_before = bank3d_a_hits;
    int b_before = bank3d_b_hits;
    uint64_t addr_A_101 = BANK3D_A_ADDR + 1 * BANK3D_S0 + 0 * BANK3D_S1 + 1 * BANK3D_S2;
    m_initiator.do_write<uint32_t>(addr_A_101, 0xA1010101UL);
    ASSERT_EQ(bank3d_a_hits, a_before + 1);
    ASSERT_EQ(bank3d_b_hits, b_before); // no cross-fire onto B
    ASSERT_EQ(bank3d_a_last_rel_addr, 1UL * BANK3D_S0 + 1UL * BANK3D_S2);

    // Symmetrically, a write to B(1,0,1) hits B and not A.
    uint64_t addr_B_101 = BANK3D_B_ADDR + 1 * BANK3D_S0 + 0 * BANK3D_S1 + 1 * BANK3D_S2;
    m_initiator.do_write<uint32_t>(addr_B_101, 0xB1010101UL);
    ASSERT_EQ(bank3d_a_hits, a_before + 1); // A didn't fire again
    ASSERT_EQ(bank3d_b_hits, b_before + 1);
    ASSERT_EQ(bank3d_b_last_rel_addr, 1UL * BANK3D_S0 + 1UL * BANK3D_S2);

    // Read-back through the cursor confirms neither bank stomped on the other.
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_A(1, 0, 1)), 0xA1010101UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_B(1, 0, 1)), 0xB1010101UL);

    SCP_DEBUG(())
        << "**************** 3D banks: writes in overlapping range but into the gap of the other ****************";
    // A does NOT claim 0x904 (B's element (0,0,0) sitting in A's gap). Writing
    // to 0x904 must land only on B, leaving A(0,0,0) untouched.
    uint32_t a_000_before = BANK3D_A(0, 0, 0);
    int a_before2 = bank3d_a_hits;
    int b_before2 = bank3d_b_hits;
    m_initiator.do_write<uint32_t>(BANK3D_B_ADDR, 0x0BEEF001UL);
    ASSERT_EQ(bank3d_a_hits, a_before2); // A untouched
    ASSERT_EQ(bank3d_b_hits, b_before2 + 1);
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_B(0, 0, 0)), 0x0BEEF001UL);
    ASSERT_EQ(static_cast<uint32_t>(BANK3D_A(0, 0, 0)), a_000_before);

    SCP_DEBUG(()) << "**************** Priority: LOWPRI (prio 1) is filtered out at ALIAS_ADDR ****************";
    // At ALIAS_ADDR (=0x800), the claimants are: ALIAS_A (prio 0), ALIAS_B
    // (prio 0), LOWPRI (prio 1). SHADOW's grid covers [0, 0x1000) too, so it
    // is also a prio-0 claimant. reg_router keeps only the strongest priority
    // set, so LOWPRI never fires.
    int prio_a_before = alias_a_hits;
    int prio_b_before = alias_b_hits;
    int prio_low_before = lowpri_hits;
    int prio_shadow_before = shadow_hits;
    m_initiator.do_write<uint32_t>(ALIAS_ADDR, 0xCAFECAFEUL);
    ASSERT_EQ(alias_a_hits, prio_a_before + 1);
    ASSERT_EQ(alias_b_hits, prio_b_before + 1);
    ASSERT_EQ(shadow_hits, prio_shadow_before + 1);
    ASSERT_EQ(lowpri_hits, prio_low_before);
    // Multiple writes confirm the filter is applied every time, not just once.
    m_initiator.do_write<uint32_t>(ALIAS_ADDR, 0x0);
    m_initiator.do_write<uint32_t>(ALIAS_ADDR, 0x0);
    ASSERT_EQ(alias_a_hits, prio_a_before + 3);
    ASSERT_EQ(alias_b_hits, prio_b_before + 3);
    ASSERT_EQ(lowpri_hits, prio_low_before);

    SCP_DEBUG(()) << "**************** SHADOW: fires alongside every other prio-0 callback ****************";
    // SHADOW.dim_counts = {0x400}, dim_strides = {4} -> element grid covers
    // every 4-byte-aligned address in [0, 0x1000). Same priority (0) as every
    // other callback in the fixture, so every priority-0 access must fan out
    // to SHADOW in addition to the specific register that owns the address.
    ASSERT_EQ(SHADOW.get_dims(), 1u);
    ASSERT_EQ(SHADOW.get_dim_count(0), SHADOW_LEN);
    ASSERT_EQ(SHADOW.get_dim_stride(0), SHADOW_STRIDE);

    // Baseline the counter, then hit a variety of unrelated registers and
    // watch shadow_hits increment once per priority-0 write. Each of the
    // targets we touch here has its own post_write callback whose assertions
    // (defined in end_of_elaboration) depend on last_written_*/last_used_mask
    // trackers, so we prime those before each write.
    int sh_before = shadow_hits;
    int bank1d_before = bank1d_post_writes;
    int bank2d_before = bank2d_post_writes;

    // CMD0: post_write asserts read-back equals last_written_value.
    last_used_mask = gs::gs_full_mask<uint32_t>();
    CMD0.set_mask(last_used_mask);
    last_written_value = 0x11111111UL;
    m_initiator.do_write<uint32_t>(CMD0_ADDR, 0x11111111UL);

    // FIFO0: first post_write asserts FIFO0[last_written_idx] == last_written_value.
    last_written_idx = 1UL;
    FIFO0.set_mask(gs::gs_full_mask<uint32_t>());
    last_used_mask = gs::gs_full_mask<uint32_t>();
    last_written_value = 0x22222222UL;
    m_initiator.do_write<uint32_t>(FIFO0_ADDR + 4UL, 0x22222222UL);

    m_initiator.do_write<uint32_t>(BANK1D_ADDR, 0x33333333UL);      // hits BANK1D[0]
    m_initiator.do_write<uint32_t>(BANK2D_ADDR + BANK2D_ROW_STRIDE, // hits BANK2D[1][0]
                                   0x44444444UL);
    m_initiator.do_write<uint32_t>(addr_A_101, 0x55555555UL); // hits BANK3D_A(1,0,1)

    ASSERT_EQ(shadow_hits, sh_before + 5);
    // And the corresponding "owning" register callbacks still fired for the
    // two we track post_write on directly.
    ASSERT_EQ(bank1d_post_writes, bank1d_before + 1);
    ASSERT_EQ(bank2d_post_writes, bank2d_before + 1);

    SCP_DEBUG(()) << "**************** SHADOW: address decoded is relative and 4-byte-indexed ****************";
    // The last shadow write was to BANK3D_A(1,0,1) at absolute 0x948. With
    // SHADOW at base 0, use_offset=true, the callback sees address 0x948 and
    // decode_access maps it to element 0x948/4 = 0x252.
    ASSERT_EQ(shadow_last_rel_addr, addr_A_101);
    tlm::tlm_generic_payload probe;
    probe.set_address(shadow_last_rel_addr);
    auto sh_acc = SHADOW.decode_access(probe);
    ASSERT_TRUE(bool(sh_acc));
    ASSERT_EQ(sh_acc.index, shadow_last_rel_addr / SHADOW_STRIDE);
    ASSERT_EQ(sh_acc.indices.size(), 1u);
    ASSERT_EQ(sh_acc.indices[0], shadow_last_rel_addr / SHADOW_STRIDE);

    SCP_DEBUG(()) << "**************** SHADOW: does not fire on unclaimed addresses ****************";
    // A read at BANK_GAP_ADDR+4 lands in BANK_GAP's inter-element gap. SHADOW's
    // grid is 4-byte stride/elem, so 0x404 is a SHADOW element and SHADOW does
    // fire. Read that address, then read an address outside the register file
    // (still in reg_memory) and verify SHADOW's counter for that OOR-of-shadow
    // access does not increment beyond what it would otherwise.
    int sh_before_oor = shadow_hits;
    // 0x1004 is inside reg_memory but outside SHADOW's element grid (grid ends
    // at 0x1000). Only reg_memory should service it - no callbacks fire.
    m_initiator.do_write<uint32_t>(0x1004UL, 0x66666666UL);
    ASSERT_EQ(shadow_hits, sh_before_oor);
}

int sc_main(int argc, char* argv[])
{
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::DBGTRACE) // set log level to DBGTRACE = TRACEALL
                                        .msgTypeFieldWidth(50));      // make the msg type column a bit tighter

    gs::ConfigurableBroker m_broker({
        { "test_registers.reg_memory.target_socket.address", cci::cci_value(REG_MEM_ADDR) },
        { "test_registers.reg_memory.target_socket.size", cci::cci_value(REG_MEM_SZ) },
        { "test_registers.reg_memory.target_socket.relative_addresses", cci::cci_value(false) },
        { "test_registers.reg_memory.verbose", cci::cci_value(true) },
        { "test_registers.reg_memory.init_mem", cci::cci_value(true) },
        { "test_registers.LOWPRI.target_socket.priority", cci::cci_value(1UL) },

        { "test_registers_bank.reg_memory.target_socket.address", cci::cci_value(REG_MEM_ADDR) },
        { "test_registers_bank.reg_memory.target_socket.size", cci::cci_value(REG_MEM_SZ) },
        { "test_registers_bank.reg_memory.target_socket.relative_addresses", cci::cci_value(false) },
        { "test_registers_bank.reg_memory.verbose", cci::cci_value(true) },
        { "test_registers_bank.reg_memory.init_mem", cci::cci_value(true) },
        { "test_registers_bank.LOWPRI.target_socket.priority", cci::cci_value(1UL) },

        { "test_registers_bank_extras.reg_memory.target_socket.address", cci::cci_value(REG_MEM_ADDR) },
        { "test_registers_bank_extras.reg_memory.target_socket.size", cci::cci_value(REG_MEM_SZ) },
        { "test_registers_bank_extras.reg_memory.target_socket.relative_addresses", cci::cci_value(false) },
        { "test_registers_bank_extras.reg_memory.verbose", cci::cci_value(true) },
        { "test_registers_bank_extras.reg_memory.init_mem", cci::cci_value(true) },
        { "test_registers_bank_extras.LOWPRI.target_socket.priority", cci::cci_value(1UL) },
    });

    ::testing::InitGoogleTest(&argc, argv);
    int status = RUN_ALL_TESTS();
    return status;
}
