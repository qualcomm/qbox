/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qcom_turing_lmh.h>

void module_register() { GSC_MODULE_REGISTER_C(qcom_turing_lmh, sc_core::sc_object*); }
