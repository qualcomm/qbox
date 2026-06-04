/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <e1000e_pci.h>

void module_register() { GSC_MODULE_REGISTER_C(e1000e_pci, sc_core::sc_object*, sc_core::sc_object*); }
