/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <aplic-sifive.h>

void module_register() { GSC_MODULE_REGISTER_C(aplic_sifive, sc_core::sc_object*); }
