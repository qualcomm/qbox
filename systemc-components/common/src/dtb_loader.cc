/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "dtb_loader.h"

#include <fstream>
#include <iterator>
#include <new>

#include <libfdt.h>

namespace gs {

namespace {

bool read_file(const std::string& path, std::vector<uint8_t>& data, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        error = "unable to open '" + path + "'";
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        error = "unable to read '" + path + "'";
        return false;
    }
    return true;
}

/* Full structural check: libfdt's overlay code assumes well-formed trees. */
bool check_blob(const std::vector<uint8_t>& blob, const std::string& path, std::string& error)
{
    if (blob.size() < sizeof(struct fdt_header)) {
        error = "'" + path + "' is too small to be a DTB";
        return false;
    }
    int err = fdt_check_header(blob.data());
    if (!err && fdt_totalsize(blob.data()) > blob.size()) err = -FDT_ERR_TRUNCATED;
    if (!err) err = fdt_check_full(blob.data(), fdt_totalsize(blob.data()));
    if (err) {
        error = "'" + path + "' is not a valid DTB: " + fdt_strerror(err);
        return false;
    }
    return true;
}

/*
 * Copy @p tree into a fresh, writable buffer of @p size bytes. This also
 * normalizes the block layout/version so that libfdt read-write operations
 * (including fdt_pack) are accepted.
 */
int open_into(const std::vector<uint8_t>& tree, std::vector<uint8_t>& out, size_t size)
{
    if (size > INT32_MAX) return -FDT_ERR_NOSPACE;
    out.assign(size, 0);
    return fdt_open_into(tree.data(), out.data(), static_cast<int>(size));
}

std::vector<uint8_t> build(const std::string& base, const std::vector<std::string>& overlays, std::string& error)
{
    std::vector<uint8_t> tree;
    if (!read_file(base, tree, error) || !check_blob(tree, base, error)) return {};

    std::vector<uint8_t> out;
    int err = open_into(tree, out, size_t(fdt_totalsize(tree.data())));
    if (err) {
        error = "unable to open '" + base + "': " + fdt_strerror(err);
        return {};
    }
    tree.swap(out);

    for (const auto& path : overlays) {
        std::vector<uint8_t> overlay;
        if (!read_file(path, overlay, error) || !check_blob(overlay, path, error)) return {};

        // fdt_overlay_apply corrupts the base and the overlay on failure, so
        // every attempt starts from fresh copies; grow the output on NOSPACE.
        size_t size = size_t(fdt_totalsize(tree.data())) + size_t(fdt_totalsize(overlay.data())) + 0x1000;
        err = -FDT_ERR_NOSPACE;
        for (int attempt = 0; attempt < 8 && err == -FDT_ERR_NOSPACE; ++attempt, size *= 2) {
            err = open_into(tree, out, size);
            if (err) {
                error = "unable to expand tree for overlay '" + path + "': " + fdt_strerror(err);
                return {};
            }
            std::vector<uint8_t> ov(overlay);
            err = fdt_overlay_apply(out.data(), ov.data());
        }
        if (err) {
            error = "applying overlay '" + path + "' failed: " + fdt_strerror(err);
            return {};
        }
        tree.swap(out);
    }

    err = fdt_pack(tree.data());
    if (!err) err = fdt_check_full(tree.data(), fdt_totalsize(tree.data()));
    if (err) {
        error = std::string("final DTB is invalid: ") + fdt_strerror(err);
        return {};
    }
    tree.resize(fdt_totalsize(tree.data()));
    return tree;
}

} // namespace

std::vector<uint8_t> dtb_build(const std::string& base, const std::vector<std::string>& overlays, std::string& error)
{
    try {
        return build(base, overlays, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory while building DTB from '" + base + "'";
        return {};
    }
}

} // namespace gs
