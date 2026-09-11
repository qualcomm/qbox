/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BASE_COMPONENTS_TESTS_ADDRTR_TEST_BENCH_H
#define _BASE_COMPONENTS_TESTS_ADDRTR_TEST_BENCH_H

#include <functional>
#include <array>
#include <optional>

#include <systemc>
#include <tlm>

#include <tests/test-bench.h>
#include <tests/initiator-tester.h>
#include <tests/target-tester.h>

#include "addrtr.h"

class AddrtrTestBench : public TestBench
{
public:
    static constexpr size_t TARGET_MMIO_SIZE = 1024;
    static constexpr uint64_t TARGET_BASE_ADDR = 0x1000;
    static constexpr uint64_t MAPPED_BASE_ADDR = 0x110;
    static constexpr uint64_t DMI_RANGE_OFFSET = 0x20;
    static constexpr uint64_t DMI_RANGE_SIZE = 0x100;
    static constexpr uint64_t TARGET_END_ADDR = TARGET_BASE_ADDR + TARGET_MMIO_SIZE - 1;
    static constexpr uint64_t MAPPED_END_ADDR = MAPPED_BASE_ADDR + TARGET_MMIO_SIZE - 1;
    static constexpr uint64_t DOWNSTREAM_TESTER_SIZE = MAPPED_BASE_ADDR + TARGET_MMIO_SIZE + 16;

    class TargetTesterDMIinv : public TargetTester
    {
        using TargetTester::TargetTester;

    public:
        void do_dmi_invalidate(uint64_t start, uint64_t end) { socket->invalidate_direct_mem_ptr(start, end); }
    };

public:
    using TlmResponseStatus = InitiatorTester::TlmResponseStatus;
    using TlmGenericPayload = InitiatorTester::TlmGenericPayload;
    using TlmDmi = InitiatorTester::TlmDmi;

private:
    addrtr m_addrtr;

    InitiatorTester m_initiator;
    TargetTesterDMIinv m_target;

    uint64_t sent_addr = 0;
    bool return_dmi_range = false;
    bool return_dmi_from_zero = false;
    bool dmi_return_value = true;
    bool use_explicit_dmi_range = false;
    uint64_t dmi_range_size = 0;
    uint64_t explicit_dmi_start = 0;
    uint64_t explicit_dmi_end = 0;
    uint64_t explicit_dmi_ptr_offset = 0;
    std::array<unsigned char, MAPPED_BASE_ADDR + TARGET_MMIO_SIZE> dmi_memory{};
    std::optional<std::pair<uint64_t, uint64_t>> expected_invalidation;
    unsigned int invalidation_count = 0;

    uint64_t mapped_addr() const { return MAPPED_BASE_ADDR + (sent_addr - TARGET_BASE_ADDR); }

    /* Initiator callback */
    void invalidate_direct_mem_ptr(uint64_t start_range, uint64_t end_range)
    {
        invalidation_count++;
        ASSERT_TRUE(expected_invalidation.has_value());
        EXPECT_EQ(start_range, expected_invalidation->first);
        EXPECT_EQ(end_range, expected_invalidation->second);
    }

    /* Target callbacks */
    TlmResponseStatus target_access(uint64_t addr, uint8_t* data, size_t len)
    {
        EXPECT_EQ(mapped_addr(), addr);
        return tlm::TLM_OK_RESPONSE;
    }

    bool get_direct_mem_ptr(uint64_t addr, TlmDmi& dmi_data)
    {
        EXPECT_EQ(addr, mapped_addr());
        if (!dmi_return_value) {
            return false;
        }
        dmi_data.allow_read_write();
        if (use_explicit_dmi_range) {
            dmi_data.set_dmi_ptr(dmi_memory.data() + explicit_dmi_ptr_offset);
            dmi_data.set_start_address(explicit_dmi_start);
            dmi_data.set_end_address(explicit_dmi_end);
        } else if (return_dmi_from_zero) {
            dmi_data.set_dmi_ptr(dmi_memory.data());
            dmi_data.set_start_address(0);
            dmi_data.set_end_address(MAPPED_END_ADDR);
        } else {
            dmi_data.set_start_address(addr);
            dmi_data.set_end_address(return_dmi_range ? addr + dmi_range_size - 1 : addr);
        }
        return true;
    }

protected:
    void do_txn(uint64_t addr, bool dbg)
    {
        uint64_t data = 0x42ULL;
        TlmGenericPayload txn;
        sent_addr = addr;
        dmi_return_value = true;
        if (!dbg) {
            ASSERT_EQ(tlm::TLM_OK_RESPONSE, m_initiator.do_write(addr, data, dbg));
        } else {
            ASSERT_EQ(0, m_initiator.do_write(addr, data, dbg));
        }
    }

    void do_txn_with_len(uint64_t addr, size_t len)
    {
        std::array<uint8_t, 16> data{};
        TlmGenericPayload txn;

        ASSERT_LE(len, data.size());
        sent_addr = addr;
        dmi_return_value = true;
        ASSERT_EQ(tlm::TLM_OK_RESPONSE, m_initiator.do_write_with_txn_and_ptr(txn, addr, data.data(), len));
        EXPECT_EQ(txn.get_address(), addr);
    }

    void do_dmi(uint64_t addr)
    {
        sent_addr = addr;
        dmi_return_value = true;
        use_explicit_dmi_range = false;
        return_dmi_range = false;
        return_dmi_from_zero = false;
        dmi_range_size = 0;
        ASSERT_TRUE(m_initiator.do_dmi_request(addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), sent_addr);
        ASSERT_EQ(dmi.get_end_address(), sent_addr);
        expected_invalidation = std::make_pair(TARGET_BASE_ADDR, TARGET_END_ADDR);
        invalidation_count = 0;
        m_target.do_dmi_invalidate(MAPPED_BASE_ADDR, MAPPED_END_ADDR);
        EXPECT_EQ(invalidation_count, 1u);
    }

    void do_dmi_with_range(uint64_t addr, uint64_t range_size)
    {
        sent_addr = addr;
        dmi_return_value = true;
        use_explicit_dmi_range = false;
        return_dmi_range = true;
        return_dmi_from_zero = false;
        dmi_range_size = range_size;
        ASSERT_TRUE(m_initiator.do_dmi_request(addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), sent_addr);
        ASSERT_EQ(dmi.get_end_address(), sent_addr + range_size - 1);
    }

    void do_dmi_with_clipped_downstream_start()
    {
        sent_addr = TARGET_BASE_ADDR;
        dmi_return_value = true;
        use_explicit_dmi_range = false;
        return_dmi_range = false;
        return_dmi_from_zero = true;
        ASSERT_TRUE(m_initiator.do_dmi_request(sent_addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), TARGET_BASE_ADDR);
        ASSERT_EQ(dmi.get_end_address(), TARGET_END_ADDR);
        ASSERT_EQ(dmi.get_dmi_ptr(), dmi_memory.data() + MAPPED_BASE_ADDR);
    }

    void do_dmi_with_explicit_range(uint64_t addr, uint64_t downstream_start, uint64_t downstream_end,
                                    uint64_t ptr_offset, uint64_t expected_start, uint64_t expected_end,
                                    uint64_t expected_ptr_offset)
    {
        sent_addr = addr;
        dmi_return_value = true;
        use_explicit_dmi_range = true;
        return_dmi_range = false;
        return_dmi_from_zero = false;
        explicit_dmi_start = downstream_start;
        explicit_dmi_end = downstream_end;
        explicit_dmi_ptr_offset = ptr_offset;

        ASSERT_TRUE(m_initiator.do_dmi_request(addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), expected_start);
        ASSERT_EQ(dmi.get_end_address(), expected_end);
        ASSERT_EQ(dmi.get_dmi_ptr(), dmi_memory.data() + expected_ptr_offset);
    }

    void do_dmi_denied(uint64_t addr)
    {
        sent_addr = addr;
        dmi_return_value = false;
        use_explicit_dmi_range = false;
        return_dmi_range = false;
        return_dmi_from_zero = false;
        ASSERT_FALSE(m_initiator.do_dmi_request(addr));
    }

    void do_dmi_non_overlapping_grant_returns_false(uint64_t addr, uint64_t downstream_start, uint64_t downstream_end)
    {
        sent_addr = addr;
        dmi_return_value = true;
        use_explicit_dmi_range = true;
        return_dmi_range = false;
        return_dmi_from_zero = false;
        explicit_dmi_start = downstream_start;
        explicit_dmi_end = downstream_end;
        explicit_dmi_ptr_offset = 0;
        ASSERT_FALSE(m_initiator.do_dmi_request(addr));
    }

    void do_invalidate(uint64_t downstream_start, uint64_t downstream_end, uint64_t expected_start,
                       uint64_t expected_end)
    {
        expected_invalidation = std::make_pair(expected_start, expected_end);
        invalidation_count = 0;
        m_target.do_dmi_invalidate(downstream_start, downstream_end);
        EXPECT_EQ(invalidation_count, 1u);
        expected_invalidation.reset();
    }

    void do_non_overlapping_invalidate(uint64_t downstream_start, uint64_t downstream_end)
    {
        expected_invalidation.reset();
        invalidation_count = 0;
        m_target.do_dmi_invalidate(downstream_start, downstream_end);
        EXPECT_EQ(invalidation_count, 0u);
    }

public:
    AddrtrTestBench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , m_addrtr("exclusive_addrtr")
        , m_initiator("initiator-tester")
        , m_target("target-tester", DOWNSTREAM_TESTER_SIZE)
    {
        using namespace std::placeholders;

        m_initiator.register_invalidate_direct_mem_ptr(
            std::bind(&AddrtrTestBench::invalidate_direct_mem_ptr, this, _1, _2));
        m_target.register_read_cb(std::bind(&AddrtrTestBench::target_access, this, _1, _2, _3));
        m_target.register_write_cb(std::bind(&AddrtrTestBench::target_access, this, _1, _2, _3));
        m_target.register_debug_write_cb(std::bind(&AddrtrTestBench::target_access, this, _1, _2, _3));
        m_target.register_get_direct_mem_ptr_cb(std::bind(&AddrtrTestBench::get_direct_mem_ptr, this, _1, _2));

        m_addrtr.target_socket.bind(m_initiator.socket);
        m_addrtr.initiator_socket.bind(m_target.socket);
    }

    virtual ~AddrtrTestBench() {}
};

#endif
