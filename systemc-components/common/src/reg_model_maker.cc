/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <reg_model_maker/reg_model_maker.h>

#include <zip.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace gs {

json_zip_archive::json_zip_archive(zip_t* archive)
    : m_archive(archive)
    , m_base_address_captured(false)
    , m_base_address(0)
    , m_parent_model_base_addr(0)
    , m_is_platform_top(false)
    , m_is_platform_model(false)
    , m_max_size(0)
    , m_router(nullptr)
    , m_memory(nullptr)
{
    SCP_LOGGER_NAME().features[0] = "json_zip_archive";
    if (m_archive == nullptr) {
        SCP_FATAL(())("Can't open zip archive");
    }
    std::string json_top_file_name = get_json_topname();
    m_archive_name = json_top_file_name + ".zip";
    SCP_DEBUG(())("Loaded zip archive: {}", m_archive_name);
}

zip_t* json_zip_archive::get_zip_archive_ptr() { return m_archive; }

std::string json_zip_archive::get_archive_name() { return m_archive_name; }

std::vector<std::string> json_zip_archive::get_file_names_in_zip_archive()
{
    std::vector<std::string> ret;
    zip_stat_t stat;
    zip_int64_t num_of_entries = zip_get_num_entries(m_archive, 0);
    if (num_of_entries < 0) SCP_FATAL(())("Can't get number of entries in the zip archive");
    for (zip_int64_t i = 0; i < num_of_entries; i++) {
        if (zip_stat_index(m_archive, i, ZIP_FL_NOCASE, &stat) < 0) {
            SCP_FATAL(())
            ("Can't execute zip_stat() on function get_file_names_in_zip_archive()");
        }
        ret.push_back(stat.name);
    }
    return ret;
}

std::string json_zip_archive::get_json_topname()
{
    auto zip_arch_file_names = get_file_names_in_zip_archive();
    std::string ret = "";
    for (const auto& file_name : zip_arch_file_names) {
        if (file_name.find(".json", file_name.length() - std::string(".json").length()) != file_name.npos) {
            ret = file_name.substr(0, file_name.length() - std::string(".json").length());
            break;
        }
    }
    if (ret.empty()) SCP_FATAL(())
    ("Can't find any json config files in the zip archive");
    return ret;
}

std::string json_zip_archive::get_bin_file_name()
{
    auto zip_arch_file_names = get_file_names_in_zip_archive();
    std::string ret = "";
    for (const auto& file_name : zip_arch_file_names) {
        if (file_name.find(".bin", file_name.length() - std::string(".bin").length()) != file_name.npos) {
            ret = file_name;
            break;
        }
    }
    if (ret.empty()) SCP_FATAL(())
    ("Can't find any register binary reset values files in the zip archive: {}", get_archive_name());
    return ret;
}

uint64_t json_zip_archive::get_bin_file_size()
{
    zip_stat_t z_stat;
    auto zip_arch_file_names = get_file_names_in_zip_archive();
    std::string bin_file_name;
    for (const auto& file_name : zip_arch_file_names) {
        if (file_name.find(".bin", file_name.length() - std::string(".bin").length()) != file_name.npos) {
            bin_file_name = file_name;
            break;
        }
    }
    if (bin_file_name.empty()) {
        SCP_FATAL(())
        ("Can't find any register binary reset values files in the zip archive: {}", get_archive_name());
    }
    if (zip_stat(m_archive, bin_file_name.c_str(), ZIP_FL_NOCASE, &z_stat) < 0) {
        SCP_FATAL(())
        ("Can't execute zip_stat() on {} in archive: {}", bin_file_name, get_archive_name());
    }
    return z_stat.size;
}

rapidjson::Document& json_zip_archive::get_json(std::string file, rapidjson::Document& d)
{
    zip_stat_t stat;
    SCP_TRACE(())("Looking for {} in archive: {}", file.c_str(), get_archive_name());

    if (file.empty()) {
        file = get_json_topname() + ".json";
    }
    if (zip_stat(m_archive, file.c_str(), ZIP_FL_NOCASE, &stat) != 0) {
        SCP_DEBUG(())("Unable to find {} in archive: {}", file.c_str(), get_archive_name());
        return d;
    }

    char* json_info = (char*)malloc(stat.size + 1);
    zip_file_t* fd = zip_fopen(m_archive, file.c_str(), ZIP_FL_NOCASE);
    if (zip_fread(fd, json_info, stat.size) != stat.size) {
        SCP_FATAL(())("Unable to read {} in archive: {}", file, get_archive_name());
    }
    json_info[stat.size] = '\0';

    d.Parse(json_info);
    if (d.HasParseError()) {
        SCP_FATAL(())("Unable to parse {} in archive: {}", stat.name, get_archive_name());
    }
    zip_fclose(fd);
    free(json_info);
    return d;
}

} // namespace gs
