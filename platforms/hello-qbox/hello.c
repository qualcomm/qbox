/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define UART_DR ((volatile unsigned int*)0x09000000)

/* PSCI function IDs (SMCCC compliant). */
#define PSCI_SYSTEM_OFF 0x84000008

void __attribute__((naked)) _start(void)
{
    /* Set up a stack at the top of the 256 MB RAM region,
     * then branch to main. */
    __asm__ volatile(
        "ldr x0, =0x90000000\n"
        "mov sp, x0\n"
        "bl main\n"
        "1: b 1b\n");
}

static inline void psci_system_off(void)
{
    register unsigned long x0 __asm__("x0") = PSCI_SYSTEM_OFF;
    __asm__ volatile("hvc #0" : : "r"(x0));
    __builtin_unreachable();
}

void main(void)
{
    const char* msg = "Hello from Qbox!\r\n";
    while (*msg) *UART_DR = *msg++;
    psci_system_off();
}
