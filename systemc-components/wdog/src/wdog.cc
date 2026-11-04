/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include "wdog.h"

typedef gs::wdog<> wdog;

void module_register() { GSC_MODULE_REGISTER_C(wdog); }