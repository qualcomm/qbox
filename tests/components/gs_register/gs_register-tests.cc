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
    , last_written_value(0)
    , last_written_idx(0)
    , last_used_mask(gs::gs_full_mask<uint32_t>())
    , is_array(false)
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
    });

    ::testing::InitGoogleTest(&argc, argv);
    int status = RUN_ALL_TESTS();
    return status;
}