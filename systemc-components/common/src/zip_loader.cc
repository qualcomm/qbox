/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zip_loader.h>

#include <scp/report.h>
#include <zip.h>

namespace gs {

zip_t* zip_open_from_memory(const void* data, std::size_t size)
{
    zip_source_t* src = zip_source_buffer_create(data, size, 0, nullptr);
    if (!src) return nullptr;
    zip_t* archive = zip_open_from_source(src, ZIP_RDONLY, nullptr);
    if (!archive) {
        zip_source_free(src);
        return nullptr;
    }
    return archive;
}

static zip_int64_t zip_read_into(zip_t* z_archive, const std::string& archive_name, const zip_stat_t& z_stat,
                                 std::vector<uint8_t>& data, uint64_t file_offset, uint64_t file_data_len)
{
    zip_file_t* fd = zip_fopen(z_archive, z_stat.name, ZIP_FL_NOCASE);
    if (!fd) SCP_FATAL("zip_loader") << "Can't open file: " << z_stat.name << " in zip archive: " << archive_name;

    zip_int64_t used_file_data_len = 0;
    if (file_data_len == 0)
        used_file_data_len = z_stat.size;
    else if ((file_offset < z_stat.size) && ((file_data_len + file_offset) > z_stat.size))
        used_file_data_len = z_stat.size - file_offset;
    else if (file_offset > z_stat.size)
        SCP_FATAL("zip_loader") << "file offset (" << file_offset << ") is bigger than the size (" << z_stat.size
                                << ") of " << z_stat.name << " in zip archive: " << archive_name;
    else
        used_file_data_len = file_data_len;

    uint8_t* ptr = data.data();
    zip_int64_t rem = z_stat.size;
    while (rem > 0) {
        zip_int64_t read_bytes = zip_fread(fd, ptr, rem);
        if (read_bytes < 0) {
            SCP_FATAL("zip_loader") << "Can't read from " << z_stat.name << " in zip archive: " << archive_name;
        }
        ptr += read_bytes;
        rem -= read_bytes;
    }
    zip_fclose(fd);
    return used_file_data_len;
}

std::vector<uint8_t> zip_extract(zip_t* archive, const std::string& archive_name, const std::string& file_name,
                                 uint64_t file_offset, uint64_t file_data_len)
{
    zip_t* z = archive;
    bool opened_here = false;
    if (!z) {
        if (archive_name.empty()) SCP_FATAL("zip_loader") << "Missing zip archive name!";
        z = zip_open(archive_name.c_str(), 0, nullptr);
        if (!z) SCP_FATAL("zip_loader") << "Can't open zip archive: " << archive_name;
        opened_here = true;
    }

    zip_int64_t num_entries = zip_get_num_entries(z, ZIP_FL_UNCHANGED);
    if (num_entries < 0) SCP_FATAL("zip_loader") << "Can't get the number of entries in zip archive: " << archive_name;
    if (num_entries == 0) SCP_FATAL("zip_loader") << "There are no files in the zip archive: " << archive_name;

    zip_stat_t z_stat;
    if (num_entries > 1) {
        if (file_name.empty())
            SCP_FATAL("zip_loader") << "Multiple files in zip archive: " << archive_name
                                    << " but file name is not specified";
        if (zip_stat(z, file_name.c_str(), ZIP_FL_NOCASE, &z_stat) < 0)
            SCP_FATAL("zip_loader") << "Can't find file: " << file_name << " in zip archive: " << archive_name;
    } else {
        if (zip_stat_index(z, 0, ZIP_FL_NOCASE, &z_stat) < 0)
            SCP_FATAL("zip_loader") << "Can't stat file in zip archive: " << archive_name;
    }

    std::vector<uint8_t> data(z_stat.size);
    zip_int64_t used_len = zip_read_into(z, archive_name, z_stat, data, file_offset, file_data_len);
    if (opened_here) zip_close(z);

    // Return only the requested portion
    if (file_offset > 0 || (uint64_t)used_len < data.size()) {
        std::vector<uint8_t> result(data.begin() + file_offset, data.begin() + file_offset + used_len);
        return result;
    }
    return data;
}

std::vector<uint8_t> zip_extract_nested(zip_t* archive, const std::string& archive_name,
                                        const std::string& compressed_file_name, const std::string& inner_file_name,
                                        uint64_t file_offset, uint64_t file_data_len)
{
    zip_t* z = archive;
    bool opened_here = false;
    if (!z) {
        if (archive_name.empty()) SCP_FATAL("zip_loader") << "Missing zip archive name!";
        z = zip_open(archive_name.c_str(), 0, nullptr);
        if (!z) SCP_FATAL("zip_loader") << "Can't open zip archive: " << archive_name;
        opened_here = true;
    }

    // Get the compressed file from the outer archive
    zip_stat_t z_stat;
    if (zip_stat(z, compressed_file_name.c_str(), ZIP_FL_NOCASE, &z_stat) < 0)
        SCP_FATAL("zip_loader") << "Can't find file: " << compressed_file_name << " in zip archive: " << archive_name;

    std::vector<uint8_t> compressed_data(z_stat.size);
    zip_read_into(z, compressed_file_name, z_stat, compressed_data, 0, z_stat.size);
    if (opened_here) zip_close(z);

    // Open the compressed data as a zip archive
    zip_t* inner_archive = zip_open_from_source(
        zip_source_buffer_create(compressed_data.data(), compressed_data.size(), 0, nullptr), ZIP_RDONLY, nullptr);
    if (!inner_archive) SCP_FATAL("zip_loader") << "Can't open inner zip archive: " << compressed_file_name;

    zip_int64_t inner_entries = zip_get_num_entries(inner_archive, ZIP_FL_UNCHANGED);
    if (inner_entries != 1)
        SCP_FATAL("zip_loader") << "Inner archive " << compressed_file_name << " in " << archive_name
                                << " is corrupted!";

    zip_stat_t inner_stat;
    if (zip_stat(inner_archive, inner_file_name.c_str(), ZIP_FL_NOCASE, &inner_stat) < 0)
        SCP_FATAL("zip_loader") << "Can't find file: " << inner_file_name
                                << " in inner archive: " << compressed_file_name;

    std::vector<uint8_t> data(inner_stat.size);
    zip_int64_t used_len = zip_read_into(inner_archive, compressed_file_name, inner_stat, data, file_offset,
                                         file_data_len);
    zip_close(inner_archive);

    if (file_offset > 0 || (uint64_t)used_len < data.size()) {
        std::vector<uint8_t> result(data.begin() + file_offset, data.begin() + file_offset + used_len);
        return result;
    }
    return data;
}

} // namespace gs
