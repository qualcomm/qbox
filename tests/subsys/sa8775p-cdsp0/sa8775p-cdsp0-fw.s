/*
 * SA8775P CDSP0 subsystem test firmware
 *
 * Reads device version/ID registers and writes results to an MMIO probe.
 * Loops back to _start after each iteration so the C++ harness can observe
 * interrupt propagation across iterations.
 *
 * The firmware derives L2VIC and WDOG addresses from the config table's
 * subsystem_base entry, mimicking real DSP firmware behavior.
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Device base addresses (devices not derived from config table)
.set IPCC_BASE,         0x00400000
.set TCSR_HW_VERSION,   0x01FC8000   // TCSR_BASE + 0x8000
.set DSPSS_PUB_BASE,    0x26300000
.set RSC_BASE,          0x260A4000
.set CDSP_PLL_LVAL,     0x26340010   // CSR_BASE + 0x40000 + 0x10
.set CORE0_PLL_LVAL,    0x26000010   // CORE0_PLL_BASE + 0x10

// Config table byte offsets (used with memw(cfgtable_base + #offset))
.set CFG_SUBSYSTEM_BASE,     0x08
.set CFG_FASTL2VIC_BASE,     0x28
.set CFG_JTLB_SIZE,          0x2C
.set CFG_VTCM_SIZE_KB,       0x3C
.set CFG_THREAD_ENABLE_MASK, 0x48

// L2VIC register offsets from L2VIC base
.set L2VIC_OFF_INT_ENABLE,   0x100
.set L2VIC_OFF_ENABLE_SET,   0x200
.set L2VIC_OFF_INT_TYPE,     0x280
.set L2VIC_OFF_SOFT_INT,     0x480
.set L2VIC_OFF_PENDING,      0x500

// MMIO probe addresses (slot N = PROBE_BASE + N*4)
.set PROBE_BASE,        0x80000000 - 0x100000000   // 0x80000000 as signed imm32 (older llvm-mc warns otherwise)

// Done marker
.set DONE_MARKER,       0xCAFECAFE - 0x100000000   // 0xCAFECAFE as signed imm32

    .text
    .globl _start

_start:
    // ===== Read config table and derive device addresses =====
    //
    // cfgbase register holds config_table_addr >> 16.
    // We shift left by 16 to recover the physical base address, then use
    // regular memw to read entries (the config table ROM is accessible via
    // the normal memory bus).

    // r16 = config table base address
    r16 = cfgbase
    r16 = asl(r16, #16)

    // r17 = subsystem_base entry from config table (offset 0x08)
    r17 = memw(r16 + #CFG_SUBSYSTEM_BASE)

    // r18 = L2VIC base = (subsystem_base << 16) + 0x10000
    r18 = asl(r17, #16)
    r18 = add(r18, ##0x10000)

    // r19 = WDOG magic address = (subsystem_base << 16) + 0x400C
    r19 = asl(r17, #16)
    r19 = add(r19, ##0x400C)

    // r20 = PROBE_BASE (persistent for all probe writes)
    r20 = ##PROBE_BASE

    // ===== Device version/ID register reads =====

    // Slot 0: IPCC version register
    r0 = ##IPCC_BASE
    r1 = memw(r0 + #0)
    memw(r20 + #0) = r1

    // Slot 1: TCSR SOC_HW_VERSION
    r0 = ##TCSR_HW_VERSION
    r1 = memw(r0 + #0)
    memw(r20 + #4) = r1

    // Slot 2: DSPSS_PUB version
    r0 = ##DSPSS_PUB_BASE
    r1 = memw(r0 + #0)
    memw(r20 + #8) = r1

    // Slot 3: WDOG magic (derived from config table)
    r1 = memw(r19 + #0)
    memw(r20 + #12) = r1

    // Slot 4: Turing RSC ID
    r0 = ##RSC_BASE
    r1 = memw(r0 + #0)
    memw(r20 + #16) = r1

    // Slot 5: CDSP PLL L_VAL
    r0 = ##CDSP_PLL_LVAL
    r1 = memw(r0 + #0)
    memw(r20 + #20) = r1

    // Slot 6: Core0 PLL L_VAL
    r0 = ##CORE0_PLL_LVAL
    r1 = memw(r0 + #0)
    memw(r20 + #24) = r1

    // Slot 7: L2VIC INT_ENABLE[0] at reset (derived from config table)
    r0 = add(r18, #L2VIC_OFF_INT_ENABLE)
    r1 = memw(r0 + #0)
    memw(r20 + #28) = r1

    // Slot 8: L2VIC INT_PENDING[0] baseline (derived from config table)
    r0 = add(r18, ##L2VIC_OFF_PENDING)
    r1 = memw(r0 + #0)
    memw(r20 + #32) = r1

    // ===== L2VIC soft interrupt test (IRQ 5, edge-triggered) =====

    // Set INT_TYPE[0] bit 5 as edge-triggered
    r0 = add(r18, ##L2VIC_OFF_INT_TYPE)
    r1 = #0x20
    memw(r0 + #0) = r1

    // Enable IRQ 5
    r0 = add(r18, ##L2VIC_OFF_ENABLE_SET)
    r1 = #0x20
    memw(r0 + #0) = r1

    // Trigger soft IRQ 5
    r0 = add(r18, ##L2VIC_OFF_SOFT_INT)
    r1 = #0x20
    memw(r0 + #0) = r1

    // Slot 9: Read L2VIC INT_PENDING[0] - bit 5 should be set
    r0 = add(r18, ##L2VIC_OFF_PENDING)
    r1 = memw(r0 + #0)
    memw(r20 + #36) = r1

    // ===== Config table verification reads =====

    // Slot 10: subsystem_base (already in r17)
    memw(r20 + #40) = r17

    // Slot 11: fastl2vic_base
    r1 = memw(r16 + #CFG_FASTL2VIC_BASE)
    memw(r20 + #44) = r1

    // Slot 12: jtlb_size_entries
    r1 = memw(r16 + #CFG_JTLB_SIZE)
    memw(r20 + #48) = r1

    // Slot 13: vtcm_size_kb
    r1 = memw(r16 + #CFG_VTCM_SIZE_KB)
    memw(r20 + #52) = r1

    // Slot 14: thread_enable_mask
    r1 = memw(r16 + #CFG_THREAD_ENABLE_MASK)
    memw(r20 + #56) = r1

    // ===== Done marker =====
    // Slot 15: Write done marker
    r1 = ##DONE_MARKER
    memw(r20 + #60) = r1

    // Wait for interrupt, then loop back to re-check all registers
    .align 4
end:
    // `wait(r0)` -- encoded literally because some versions of llvm-mc
    // (e.g. Ubuntu's llvm-14/18) don't accept the mnemonic; the bytes match
    // the machine encoding. See tests/qbox/cpu/hexagon/ld-st-mmio.S.
    .word 0x6440c000
    jump _start
