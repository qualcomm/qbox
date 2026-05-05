/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qcom_turing_pll.h>

void module_register() { GSC_MODULE_REGISTER_C(qcom_turing_pll, sc_core::sc_object*); }
