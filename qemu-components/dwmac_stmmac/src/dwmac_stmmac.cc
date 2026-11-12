/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <dwmac_stmmac.h>

void module_register() { GSC_MODULE_REGISTER_C(dwmac_stmmac, sc_core::sc_object*); }
