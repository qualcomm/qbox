/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <cortex-m55.h>

void module_register() { GSC_MODULE_REGISTER_C(cpu_arm_cortexM55, sc_core::sc_object*, sc_core::sc_object*); }