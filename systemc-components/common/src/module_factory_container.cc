/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "module_factory_container.h"

#ifndef WITHOUT_QEMU
#include "qemu-instance.h"
#endif

// Module registrations - moved from header to avoid duplicate registrations
// when header is included in multiple translation units
GSC_MODULE_REGISTER(Container);
GSC_MODULE_REGISTER(ContainerDeferModulesConstruct);
GSC_MODULE_REGISTER(ContainerWithArgs, sc_core::sc_object*);
