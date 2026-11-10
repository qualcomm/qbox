/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

.text
.global _start

_start:
    adr x0, secure_iova
    ldr x0, [x0]
    adr x1, secure_prime_value
    ldr x1, [x1]
    str x1, [x0]
    adr x1, secure_write_value
    ldr x1, [x1]
    str x1, [x0]

    // EL3 is Secure after reset. Switch the next instruction stream to
    // AArch64 Non-secure EL1, then use the matching non-secure route.
    mov x2, #0x401              // SCR_EL3.NS | SCR_EL3.RW
    msr scr_el3, x2
    adr x2, nonsecure_entry
    msr elr_el3, x2
    mov x2, #0x3c5              // DAIF masked, EL1h
    msr spsr_el3, x2
    isb
    eret

nonsecure_entry:
    adr x0, nonsecure_iova
    ldr x0, [x0]
    mrs x2, CurrentEL
    cmp x2, #0x4
    b.ne wrong_exception_level
    adr x1, nonsecure_prime_value
    ldr x1, [x1]
    str x1, [x0]
    adr x1, nonsecure_write_value
    ldr x1, [x1]
    str x1, [x0]
    adr x0, complete_addr
    ldr x0, [x0]
    adr x1, complete_value
    ldr x1, [x1]
    str x1, [x0]
    b 1f

wrong_exception_level:
    adr x0, complete_addr
    ldr x0, [x0]
    adr x1, wrong_exception_level_value
    ldr x1, [x1]
    str x1, [x0]

1:
    wfi
    b 1b

.align 3
secure_iova:
    .quad 0
nonsecure_iova:
    .quad 0
secure_prime_value:
    .quad 0
secure_write_value:
    .quad 0
nonsecure_prime_value:
    .quad 0
nonsecure_write_value:
    .quad 0
complete_addr:
    .quad 0
complete_value:
    .quad 0
wrong_exception_level_value:
    .quad 0
