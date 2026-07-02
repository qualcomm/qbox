/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include "loader-test-bench.h"

// Simple read into the memory and write with elf file
TEST_BENCH(LoaderTest, SimpleReadELFFile)
{
    uint8_t data;
    /* Target 1 */
    ASSERT_EQ(m_initiator.do_read(0x0003, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xde);

    ASSERT_EQ(m_initiator.do_read(0x0002, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xad);

    ASSERT_EQ(m_initiator.do_read(0x0001, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xbe);

    ASSERT_EQ(m_initiator.do_read(0x0000, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xaf);

    /* CRLF bytes at offset 0x0004-0x0005: without O_BINARY on Windows, 0x0D 0x0A
     * is collapsed to 0x0A by the CRT text-mode translation, corrupting the data. */
    ASSERT_EQ(m_initiator.do_read(0x0004, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0x0d);

    ASSERT_EQ(m_initiator.do_read(0x0005, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0x0a);
}

TEST_BENCH(LoaderTest, SimpleReadData)
{
    uint8_t data;
    /* Target 1 */
    ASSERT_EQ(m_initiator.do_read(0x0503, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xde);

    ASSERT_EQ(m_initiator.do_read(0x0502, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xad);

    ASSERT_EQ(m_initiator.do_read(0x0501, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xbe);

    ASSERT_EQ(m_initiator.do_read(0x0000, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xaf);
}

TEST_BENCH(LoaderTest, SimpleReadBinFile)
{
    uint8_t data;
    /* Target 2 */
    ASSERT_EQ(m_initiator.do_read(0x1003, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xde);

    ASSERT_EQ(m_initiator.do_read(0x1002, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xad);

    ASSERT_EQ(m_initiator.do_read(0x1001, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xbe);

    ASSERT_EQ(m_initiator.do_read(0x1000, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xaf);
}

// Simple read into the memory and write with csv file
TEST_BENCH(LoaderTest, SimpleReadCSVFile)
{
    uint32_t data;
    /* Target 3 */
    ASSERT_EQ(m_initiator.do_read(0x2000, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 11);

    ASSERT_EQ(m_initiator.do_read(0x2004, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 12);

    ASSERT_EQ(m_initiator.do_read(0x2008, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 15);

    uint64_t data64;
    ASSERT_EQ(m_initiator.do_read(0x200c, data), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, 0xafafb5b5);
    ASSERT_EQ(m_initiator.do_read(0x200c, data64), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data64, 0xf00afafb5b5);
}

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker m_broker{};
    cci::cci_originator orig{ "sc_main" };
    auto broker_h = m_broker.create_broker_handle(orig);
    ArgParser ap{ broker_h, argc, argv };

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
