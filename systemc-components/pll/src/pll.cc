
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include "pll.h"

typedef gs::pll<> pll;

void module_register() { GSC_MODULE_REGISTER_C(pll); }