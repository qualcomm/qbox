/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Forward-decl-only glib.h shim on qbox's PUBLIC include path, before the
 * system glib path, for both build and install interfaces. This also lets
 * find_package(qbox) consumers build without real glib.
 *
 * Why: <libqemu/libqemu.h> opens an extern "C" block and, through its
 * generated typedefs.h, includes qemu-plugin.h, which includes <glib.h>.
 * Modern glib.h defines C++ inline template functions (g_steal_pointer,
 * ...), which are illegal inside extern "C" ("template with C linkage").
 * Putting this shim first makes that include resolve here instead. libqemu's
 * public API uses GArray and GByteArray only as opaque pointers, so these
 * forward declarations suffice; neither qbox nor its consumers use real glib
 * internals.
 */
#ifndef GLIB_QBOX_SHIM_H
#define GLIB_QBOX_SHIM_H

typedef struct _GArray GArray;
typedef struct _GByteArray GByteArray;

#endif
