/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <memory>
#include <string>
#include <filesystem>

namespace qemu {

class LibraryIface
{
public:
    virtual bool symbol_exists(const char* symbol) = 0;
    virtual void* get_symbol(const char* symbol) = 0;
};

class LibraryLoaderIface
{
public:
    using LibraryIfacePtr = std::shared_ptr<LibraryIface>;

    virtual LibraryIfacePtr load_library(const std::string& lib_name, bool load_as_separate_instance = false) = 0;

    virtual const char* get_lib_ext() = 0;
    virtual const char* get_last_error() = 0;

    virtual ~LibraryLoaderIface() = default;
};

LibraryLoaderIface& get_default_lib_loader();
std::filesystem::path resolve_library_path(const std::string& name);

} // namespace qemu
