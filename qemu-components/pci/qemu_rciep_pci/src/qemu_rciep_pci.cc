/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <qemu_rciep_pci.h>

void module_register() { GSC_MODULE_REGISTER_C(qemu_rciep_pci, sc_core::sc_object*, sc_core::sc_object*); }
