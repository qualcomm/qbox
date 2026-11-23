/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

ENTRY(_start)

SECTIONS
{
    . = 0x0;
    .text : { *(.text) }
    .data : { *(.data) }
}
