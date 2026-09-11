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

TEST_BENCH(AddrtrTestBench, DmiClippedStartPointer) { do_dmi_with_clipped_downstream_start(); }

TEST_BENCH(AddrtrTestBench, LastByteTransaction) { do_txn_with_len(TARGET_END_ADDR, 1); }

TEST_BENCH(AddrtrTestBench, MultiByteTransactionAtEnd) { do_txn_with_len(TARGET_END_ADDR - 3, 4); }

TEST_BENCH(AddrtrTestBench, ZeroLengthTransactionAtEnd) { do_txn_with_len(TARGET_END_ADDR, 0); }

TEST_BENCH(AddrtrTestBench, DmiClippedEnd)
{
    do_dmi_with_explicit_range(TARGET_BASE_ADDR + DMI_RANGE_OFFSET, MAPPED_BASE_ADDR + DMI_RANGE_OFFSET,
                               MAPPED_END_ADDR + 0x40, MAPPED_BASE_ADDR + DMI_RANGE_OFFSET,
                               TARGET_BASE_ADDR + DMI_RANGE_OFFSET, TARGET_END_ADDR,
                               MAPPED_BASE_ADDR + DMI_RANGE_OFFSET);
}

TEST_BENCH(AddrtrTestBench, DmiClippedBothEnds)
{
    do_dmi_with_explicit_range(TARGET_BASE_ADDR + DMI_RANGE_OFFSET, 0, MAPPED_END_ADDR + 0x40, 0, TARGET_BASE_ADDR,
                               TARGET_END_ADDR, MAPPED_BASE_ADDR);
}

TEST_BENCH(AddrtrTestBench, DmiDeniedPropagates) { do_dmi_denied(TARGET_BASE_ADDR + DMI_RANGE_OFFSET); }

TEST_BENCH(AddrtrTestBench, DmiNonOverlappingGrantReturnsFalse)
{
    do_dmi_non_overlapping_grant_returns_false(TARGET_BASE_ADDR + DMI_RANGE_OFFSET, MAPPED_END_ADDR + 1,
                                               MAPPED_END_ADDR + 0x100);
}

TEST_BENCH(AddrtrTestBench, DmiGrantMustContainRequestedAddress)
{
    do_dmi_non_overlapping_grant_returns_false(TARGET_BASE_ADDR + DMI_RANGE_OFFSET, MAPPED_BASE_ADDR + 0x80,
                                               MAPPED_BASE_ADDR + 0x90);
}

TEST_BENCH(AddrtrTestBench, InvalidateClippedStart)
{
    do_invalidate(0, MAPPED_BASE_ADDR + DMI_RANGE_OFFSET, TARGET_BASE_ADDR, TARGET_BASE_ADDR + DMI_RANGE_OFFSET);
}

TEST_BENCH(AddrtrTestBench, InvalidateClippedEnd)
{
    do_invalidate(MAPPED_BASE_ADDR + DMI_RANGE_OFFSET, MAPPED_END_ADDR + 0x100, TARGET_BASE_ADDR + DMI_RANGE_OFFSET,
                  TARGET_END_ADDR);
}

TEST_BENCH(AddrtrTestBench, InvalidateNonOverlappingLowIgnored)
{
    do_non_overlapping_invalidate(0, MAPPED_BASE_ADDR - 1);
}

TEST_BENCH(AddrtrTestBench, InvalidateNonOverlappingHighIgnored)
{
    do_non_overlapping_invalidate(MAPPED_END_ADDR + 1, MAPPED_END_ADDR + 0x100);
}

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
        { "DmiClippedStartPointer.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiClippedStartPointer.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiClippedStartPointer.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "LastByteTransaction.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "LastByteTransaction.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "LastByteTransaction.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "MultiByteTransactionAtEnd.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "MultiByteTransactionAtEnd.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "MultiByteTransactionAtEnd.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "ZeroLengthTransactionAtEnd.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "ZeroLengthTransactionAtEnd.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "ZeroLengthTransactionAtEnd.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiClippedEnd.exclusive_addrtr.target_socket.address", cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiClippedEnd.exclusive_addrtr.target_socket.size", cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiClippedEnd.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiClippedBothEnds.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiClippedBothEnds.exclusive_addrtr.target_socket.size", cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiClippedBothEnds.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiDeniedPropagates.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiDeniedPropagates.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiDeniedPropagates.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiNonOverlappingGrantReturnsFalse.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiNonOverlappingGrantReturnsFalse.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiNonOverlappingGrantReturnsFalse.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "DmiGrantMustContainRequestedAddress.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "DmiGrantMustContainRequestedAddress.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "DmiGrantMustContainRequestedAddress.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "InvalidateClippedStart.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "InvalidateClippedStart.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "InvalidateClippedStart.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "InvalidateClippedEnd.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "InvalidateClippedEnd.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "InvalidateClippedEnd.exclusive_addrtr.mapped_base_addr", cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "InvalidateNonOverlappingLowIgnored.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "InvalidateNonOverlappingLowIgnored.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "InvalidateNonOverlappingLowIgnored.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
        { "InvalidateNonOverlappingHighIgnored.exclusive_addrtr.target_socket.address",
          cci::cci_value(AddrtrTestBench::TARGET_BASE_ADDR) },
        { "InvalidateNonOverlappingHighIgnored.exclusive_addrtr.target_socket.size",
          cci::cci_value(AddrtrTestBench::TARGET_MMIO_SIZE) },
        { "InvalidateNonOverlappingHighIgnored.exclusive_addrtr.mapped_base_addr",
          cci::cci_value(AddrtrTestBench::MAPPED_BASE_ADDR) },
    });

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
