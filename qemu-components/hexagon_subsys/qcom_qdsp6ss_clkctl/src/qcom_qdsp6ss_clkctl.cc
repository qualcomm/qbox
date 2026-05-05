/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qcom_qdsp6ss_clkctl.h>

void module_register() { GSC_MODULE_REGISTER_C(qcom_qdsp6ss_clkctl, sc_core::sc_object*); }
