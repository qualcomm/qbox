/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QBOX_ZIP_LOADER_H
#define _QBOX_ZIP_LOADER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct zip;
typedef struct zip zip_t;

namespace gs {

zip_t* zip_open_from_memory(const void* data, std::size_t size);

/**
 * @brief Extract data from a zip archive.
 *
 * @param archive       Live zip handle (may be nullptr; if nullptr, archive_name is opened from disk).
 * @param archive_name  Path to the zip archive file (used when archive is nullptr, and in messages).
 * @param file_name     Name of the file within the archive (empty = first entry).
 * @param file_offset   Offset within the extracted file data.
 * @param file_data_len Length to extract (0 = entire file).
 * @return Extracted data as a byte vector.
 */
std::vector<uint8_t> zip_extract(zip_t* archive, const std::string& archive_name, const std::string& file_name = "",
                                 uint64_t file_offset = 0, uint64_t file_data_len = 0);

/**
 * @brief Extract compressed data from a nested zip archive.
 *
 * Extracts a compressed file from the outer archive, then extracts data
 * from the inner archive.
 *
 * @param archive               Live zip handle for the outer archive (may be nullptr; if nullptr, archive_name is
 *                              opened from disk).
 * @param archive_name          Path to the outer zip archive (used when archive is nullptr, and in messages).
 * @param compressed_file_name  Name of the compressed file in the outer archive.
 * @param inner_file_name       Name of the file in the inner archive.
 * @param file_offset           Offset within the extracted data.
 * @param file_data_len         Length to extract (0 = entire file).
 * @return Extracted data as a byte vector.
 */
std::vector<uint8_t> zip_extract_nested(zip_t* archive, const std::string& archive_name,
                                        const std::string& compressed_file_name, const std::string& inner_file_name,
                                        uint64_t file_offset = 0, uint64_t file_data_len = 0);

} // namespace gs

#endif
