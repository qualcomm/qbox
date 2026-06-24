/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Forward-decl-only glib.h shim, on libqemu-cxx's PRIVATE include path
 * BEFORE the system glib path.
 *
 * Why: libqemu's generated typedefs.h is wrapped in extern "C" by
 * libqemu/libqemu.h, and qemu-plugin.h does `#include <glib.h>`. Modern
 * glib.h has C++ template inline functions (g_steal_pointer, ...) that
 * fail with "template with C linkage" inside extern "C". qbox's plugin
 * API surface only references @c GArray and @c GByteArray as opaque
 * pointers, so opaque forward-decls are sufficient — and shadowing real
 * glib for the libqemu-cxx target only is safe because nothing in qbox
 * C++ uses real glib internals.
 */
#ifndef GLIB_QBOX_SHIM_H
#define GLIB_QBOX_SHIM_H

typedef struct _GArray GArray;
typedef struct _GByteArray GByteArray;

#endif
