/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

.text
.global _start
_start:
    li sp, 0x10000
    la t0, mmio_addr
    lw s0, 0(t0)
    la t0, control_addr
    lw s3, 0(t0)
    la t0, interrupt_addr
    lw s1, 0(t0)
    li t0, 0x800
    add s4, s1, t0
    li s2, 0
    li s5, 1

    la t0, handler
    ori t0, t0, 2
    csrw mtvec, t0
    csrr t1, mtvec
    bne t0, t1, fail

    # Control socket and neighboring byte lanes must remain independent.
    li s5, 2
    li t0, 1
    sb t0, 0(s3)
    lbu t1, 0(s3)
    bne t0, t1, fail
    sb t0, 0x414(s1)         # Enabled neighbors have no asserted input.
    sb t0, 0x416(s1)
    sb zero, 0x415(s1)       # Disable only source 21.
    li t0, 0x20
    li s5, 3
    sb t0, 0x15(s4)         # Source 21 priority/configuration.
    lbu t1, 0x15(s4)
    li t0, 0x2f
    bne t0, t1, fail
    li t0, 8
    csrs mstatus, t0

    li t0, 1
    sw t0, 0(s0)            # Testbench asserts source 21 once.
pending:
    li s5, 4
    lbu t0, 0x15(s1)
    beqz t0, pending
    li t1, 1
    bne t0, t1, fail
    bnez s2, fail
    sw s2, 4(s0)            # Disabled-phase checkpoint: no handler.

    sb t1, 0x415(s1)        # Enable the existing request, without a new edge.
delivered:
    beqz s2, delivered
    li s5, 5
    li t0, 1
    bne s2, t0, fail
    li s5, 6
    lbu t0, 0x15(s1)
    bnez t0, fail
    li s5, 7
    li t1, 1
    lbu t0, 0x414(s1)
    bne t0, t1, fail
    lbu t0, 0x416(s1)
    bne t0, t1, fail
    sw s2, 16(s0)           # Returned from handler; pending=0; count=1.
done:
    wfi                     # Yield after completion so SystemC can stop.
    j done

.balign 4
handler:
    addi sp, sp, -16
    sw t0, 0(sp)
    sw t1, 4(sp)
    csrr t0, mcause
    li s5, 8
    li t1, 0x80000015
    bne t0, t1, fail
    addi s2, s2, 1
    sw t0, 8(s0)
    # External pending cannot be cleared by writing its CLIC pending byte.
    li s5, 9
    sb zero, 0x15(s1)
    lbu t0, 0x15(s1)
    li t1, 1
    bne t0, t1, fail
    sw t1, 12(s0)           # Service source: testbench deasserts before mret.
    li s5, 10
    lbu t0, 0x15(s1)
    bnez t0, fail
    lw t0, 0(sp)
    lw t1, 4(sp)
    addi sp, sp, 16
    mret

fail:
    sw s5, 20(s0)
    wfi
    j fail

.data
.balign 4
mmio_addr:
    .word 0
control_addr:
    .word 0
interrupt_addr:
    .word 0
