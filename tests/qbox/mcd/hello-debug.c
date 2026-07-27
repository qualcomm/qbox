/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Debug firmware for the MCD integration test. It spins forever rather than
 * powering the board off (PSCI), which would end the SystemC simulation; the
 * modelled CPU must stay live for the MCD debugger to attach and control it.
 */

#define UART_DR ((volatile unsigned int*)0x09000000)

/* Counter the spin loop bumps every iteration, at a fixed RAM address (below
 * the stack at 0x90000000, clear of the scratch region the memory tests use)
 * so the test can arm a write-watchpoint on it without parsing the ELF. */
#define COUNTER ((volatile unsigned long*)0x8ff00000)

void __attribute__((naked)) _start(void)
{
    /* 0x90000000 is the top of the 256 MB RAM region: the stack. */
    __asm__ volatile(
        "ldr x0, =0x90000000\n"
        "mov sp, x0\n"
        "bl main\n"
        "1: b 1b\n");
}

void main(void)
{
    const char* msg = "Hello from Qbox (debug)!\r\n";
    while (*msg) *UART_DR = *msg++;

    *COUNTER = 0;

    for (;;) {
        (*COUNTER)++;
        __asm__ volatile("nop");
    }
}
