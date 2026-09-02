/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BASE_COMPONENTS_TESTS_ADDRTR_TEST_BENCH_H
#define _BASE_COMPONENTS_TESTS_ADDRTR_TEST_BENCH_H

#include <functional>

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
    uint64_t dmi_range_size = 0;

    uint64_t mapped_addr() const { return MAPPED_BASE_ADDR + (sent_addr - TARGET_BASE_ADDR); }

    /* Initiator callback */
    void invalidate_direct_mem_ptr(uint64_t start_range, uint64_t end_range)
    {
        EXPECT_EQ(start_range, TARGET_BASE_ADDR);
        EXPECT_EQ(end_range, TARGET_BASE_ADDR + TARGET_MMIO_SIZE - 1);
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
        dmi_data.allow_read_write();
        dmi_data.set_start_address(addr);
        dmi_data.set_end_address(return_dmi_range ? addr + dmi_range_size - 1 : addr);
        return true;
    }

protected:
    void do_txn(uint64_t addr, bool dbg)
    {
        uint64_t data = 0x42ULL;
        TlmGenericPayload txn;
        sent_addr = addr;
        if (!dbg) {
            ASSERT_EQ(tlm::TLM_OK_RESPONSE, m_initiator.do_write(addr, data, dbg));
        } else {
            ASSERT_EQ(0, m_initiator.do_write(addr, data, dbg));
        }
    }

    void do_dmi(uint64_t addr)
    {
        sent_addr = addr;
        return_dmi_range = false;
        dmi_range_size = 0;
        ASSERT_TRUE(m_initiator.do_dmi_request(addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), sent_addr);
        ASSERT_EQ(dmi.get_end_address(), sent_addr);
        m_target.do_dmi_invalidate(MAPPED_BASE_ADDR, MAPPED_BASE_ADDR + TARGET_MMIO_SIZE - 1);
    }

    void do_dmi_with_range(uint64_t addr, uint64_t range_size)
    {
        sent_addr = addr;
        return_dmi_range = true;
        dmi_range_size = range_size;
        ASSERT_TRUE(m_initiator.do_dmi_request(addr));
        const auto& dmi = m_initiator.get_last_dmi_data();
        ASSERT_EQ(dmi.get_start_address(), sent_addr);
        ASSERT_EQ(dmi.get_end_address(), sent_addr + range_size - 1);
    }

public:
    AddrtrTestBench(const sc_core::sc_module_name& n)
        : TestBench(n)
        , m_addrtr("exclusive_addrtr")
        , m_initiator("initiator-tester")
        , m_target("target-tester", TARGET_MMIO_SIZE)
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
