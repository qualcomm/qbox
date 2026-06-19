/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <cortex-a78c.h>

void module_register() { GSC_MODULE_REGISTER_C(cpu_arm_cortexA78C, sc_core::sc_object*); }
