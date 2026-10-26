/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _QBOX_DTB_LOADER_H
#define _QBOX_DTB_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

namespace gs {

/**
 * @brief Build a flattened device tree from a base DTB and ordered overlays.
 *
 * Reads @p base, applies each overlay in @p overlays in order (libfdt
 * fdt_overlay_apply, i.e. the same operation as `fdtoverlay`), packs the
 * result and validates it with fdt_check_full.
 *
 * The base DTB must have been compiled with symbols (`dtc -@`) when the
 * overlays reference labels, and the overlays must be compiled as plugins.
 *
 * @param base     Path to the base DTB.
 * @param overlays Paths to DTBO files, applied in order.
 * @param error    Set to a human readable message on failure.
 * @return The final DTB, or an empty vector on failure.
 */
std::vector<uint8_t> dtb_build(const std::string& base, const std::vector<std::string>& overlays, std::string& error);

} // namespace gs

#endif
