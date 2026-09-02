/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <libgsutils.h>
#include "addrtr-bench.h"

/*
 * Regular load and stores. Check that the monitor does not introduce bugs when
 * no exclusive transaction are in use.
 */
TEST_BENCH(AddrtrTestBench, Tester)
{
    do_txn(TARGET_BASE_ADDR, 0);
    do_txn(TARGET_BASE_ADDR, 1);
    do_dmi(TARGET_BASE_ADDR);
}

TEST_BENCH(AddrtrTestBench, DmiRange)
{
    const auto addr = TARGET_BASE_ADDR + AddrtrTestBench::DMI_RANGE_OFFSET;
    do_dmi_with_range(addr, AddrtrTestBench::DMI_RANGE_SIZE);
}

TEST_BENCH(AddrtrTestBench, DmiBoundary) { do_dmi_with_range(TARGET_BASE_ADDR, AddrtrTestBench::TARGET_MMIO_SIZE); }

int sc_main(int argc, char* argv[])
{
    gs::ConfigurableBroker m_broker({
        { "log_level", cci::cci_value(5) },
        { "Tester.exclusive_addrtr.target_socket.address", cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "Tester.exclusive_addrtr.target_socket.size", cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "Tester.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiRange.exclusive_addrtr.target_socket.address", cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiRange.exclusive_addrtr.target_socket.size", cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiRange.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiBoundary.exclusive_addrtr.target_socket.address", cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiBoundary.exclusive_addrtr.target_socket.size", cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiBoundary.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
    });

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
