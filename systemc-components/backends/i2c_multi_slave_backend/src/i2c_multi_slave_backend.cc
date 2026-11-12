/*
 * This file is part of libqbox
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <i2c_multi_slave_backend.h>
#include <module_factory_registery.h>

void module_register() { GSC_MODULE_REGISTER_C(i2c_multi_slave_backend); }
