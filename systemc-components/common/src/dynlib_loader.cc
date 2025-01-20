/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <map>
#include <atomic>
#include <filesystem>

#include <dynlib_loader.h>
#include <scp/report.h>
#include <cci_configuration>

extern "C" {
/*
 * This function sets the CCI parameter. It gets the broker and parameter handle,
 * then sets the value if the handle is valid, or creates a new parameter if it is not.
 */
void global_set_cci_param(char* key, uint64_t val)
{
    auto b = cci::cci_get_broker();
    auto b_key = b.get_param_handle(key);

    if (b_key.is_valid()) {
        b_key.set_cci_value(cci::cci_value(val));
    } else {
        b.set_preset_cci_value(key, cci::cci_value(val));
    }
}
}

#if defined(_WIN32)
#include <Lmcons.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif
#endif

namespace fs = std::filesystem;

#if defined(_WIN32)
/**
 * Gets the path to the dll_cleanup_helper executable.
 * It should be located in the same directory as the current executable.
 */
static fs::path get_cleanup_helper_path()
{
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path helper_path = fs::path(exe_path).parent_path() / "dll_cleanup_helper.exe";
        if (fs::exists(helper_path)) {
            return helper_path;
        }
    }

    // Fallback: search PATH
    char found[MAX_PATH];
    if (SearchPathA(NULL, "dll_cleanup_helper.exe", NULL, MAX_PATH, found, NULL)) {
        return fs::path(found);
    }

    return "";
}

/**
 * Spawns a cleanup process that waits for the parent process to exit,
 * then deletes the specified DLL file.
 *
 * The child process receives an inheritable handle to the parent process
 * and the path to the DLL. It waits for the parent to terminate, then
 * deletes the file.
 */
static bool spawn_cleanup_process(const fs::path& dll_path)
{
    // Find the cleanup helper executable
    fs::path helper_path = get_cleanup_helper_path();
    if (helper_path.empty() || !fs::exists(helper_path)) {
        return false;
    }

    // Get a handle to the current process that can be inherited by the child
    HANDLE current_process = GetCurrentProcess();
    HANDLE inheritable_handle = NULL;

    if (!DuplicateHandle(current_process, current_process, current_process, &inheritable_handle, SYNCHRONIZE, TRUE,
                         0)) {
        return false;
    }

    // Build the command line: dll_cleanup_helper.exe <handle> <dll_path>
    std::ostringstream cmd;
    cmd << "\"" << helper_path.string() << "\" " << reinterpret_cast<uintptr_t>(inheritable_handle) << " "
        << "\"" << dll_path.string() << "\"";

    std::string cmd_str = cmd.str();

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};

    // Create the process with handle inheritance enabled
    BOOL result = CreateProcessA(NULL, const_cast<char*>(cmd_str.c_str()), NULL, NULL,
                                 TRUE, // Inherit handles
                                 CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    if (result) {
        // Close our references to the child process handles
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        // If we failed to create the process, close the duplicated handle
        CloseHandle(inheritable_handle);
    }

    return result != 0;
}
#endif

class Library : public qemu::LibraryIface
{
private:
    void* m_lib;

public:
    Library(void* lib): m_lib(lib) {}

    bool symbol_exists(const char* name) { return get_symbol(name) != NULL; }

    void* get_symbol(const char* name)
    {
        void* symbol = nullptr;
#if defined(_WIN32)
        symbol = reinterpret_cast<void*>(GetProcAddress((HMODULE)m_lib, name));
#else
        symbol = dlsym(m_lib, name);
#endif
        return symbol;
    }

    ~Library() {}
};

class DefaultLibraryLoader : public qemu::LibraryLoaderIface
{
    SCP_LOGGER(());

private:
    std::string m_last_error;

    bool is_library_loaded(const std::string& lib_path)
    {
#if defined(_WIN32)
        // Extract just the filename from the path for GetModuleHandle
        std::string filename = fs::path(lib_path).filename().string();
        HMODULE handle = GetModuleHandleA(filename.c_str());
        return (handle != nullptr);
#else
        void* handle = dlopen(lib_path.c_str(), RTLD_NOLOAD | RTLD_NOW);
        if (handle != nullptr) {
            // Matching dlclose is needed to decrement the reference count
            dlclose(handle);
            return true;
        }
        return false;
#endif
    }

    const char* dlpath(void* handle)
    {
        const char* path = NULL;
#ifdef __APPLE__
        for (int32_t i = _dyld_image_count(); i >= 0; i--) {
            bool found = FALSE;
            const char* probe_path = _dyld_get_image_name(i);
            void* probe_handle = dlopen(probe_path, RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);

            if (handle == probe_handle) {
                found = TRUE;
                path = probe_path;
            }

            dlclose(probe_handle);

            if (found) break;
        }
#elif defined(__linux__)
        struct link_map* map;
        dlinfo(handle, RTLD_DI_LINKMAP, &map);

        if (map) path = map->l_name;
#elif defined(_WIN32)
        // Not applicable on Windows
        path = nullptr;
#endif
        return path;
    }

    void* load_library_internal(const std::string& lib_name)
    {
        void* handle = nullptr;
#if defined(_WIN32)
        handle = LoadLibraryExA(lib_name.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
        handle = dlopen(lib_name.c_str(), RTLD_NOW);
        if (handle == nullptr) {
            m_last_error = "load_library_internal: Library " + lib_name + " is not loaded (" + dlerror() + ").";
        }
#endif
        return handle;
    }

    fs::path get_loaded_library_path(const std::string& lib_name)
    {
#ifdef _WIN32
        HMODULE handle = GetModuleHandleA(lib_name.c_str());
        if (handle == nullptr) {
            m_last_error = "get_loaded_library_path: Library " + lib_name + " is not loaded.";
            return "";
        }

        char path[MAX_PATH];
        if (GetModuleFileNameA(handle, path, MAX_PATH) == 0) {
            m_last_error = "Failed to get the loaded library path.";
            return "";
        }

        return fs::path(path);
#else
        void* handle = dlopen(lib_name.c_str(), RTLD_NOLOAD | RTLD_NOW);
        if (handle == nullptr) {
            m_last_error = "get_loaded_library_path: Library " + lib_name + " is not loaded.";
            return "";
        }

        const char* path = dlpath(handle);
        dlclose(handle);

        if (path == nullptr) {
            m_last_error = "Failed to get the loaded library path.";
            return "";
        }

        return fs::path(path);
#endif
    }

    fs::path create_temp_path(const std::string& libname)
    {
        static std::atomic<uint64_t> counter{ 0 };
        fs::path temp_dir = fs::temp_directory_path();
        std::string filename = fs::path(libname).filename().string();
        uint64_t count = counter.fetch_add(1);
#if defined(_WIN32)
        DWORD pid = GetCurrentProcessId();
#else
        pid_t pid = getpid();
#endif
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string temp_filename = std::to_string(pid) + "_" + std::to_string(timestamp) + "_" +
                                    std::to_string(count) + "_" + filename;
        return temp_dir / temp_filename;
    }

    qemu::LibraryLoaderIface::LibraryIfacePtr duplicate_and_load_library(const std::string& lib_name)
    {
        fs::path original_path(get_loaded_library_path(lib_name));
        if (original_path.empty()) {
            return nullptr;
        }

#if defined(_WIN32)
        // Windows: use counter-based naming, spawn cleanup process to delete after parent exits
        fs::path temp_path = create_temp_path(lib_name);

        try {
            fs::copy_file(original_path, temp_path, fs::copy_options::overwrite_existing);
        } catch (const fs::filesystem_error& e) {
            m_last_error = "Failed to create a copy of the library: " + std::string(e.what());
            return nullptr;
        }

        void* handle = load_library_internal(temp_path.string());
        if (handle == nullptr) {
            m_last_error = get_last_error();
            fs::remove(temp_path);
            return nullptr;
        }

        // Spawn a cleanup process that will delete the DLL after this process exits
        if (!spawn_cleanup_process(temp_path)) {
            SCP_FATAL(()) << "Failed to spawn cleanup process for: " << temp_path.string();
        }

        return std::make_shared<Library>(handle);
#else
        // Linux/macOS: use mkstemp for unique temp file, delete after loading
        char temp_template[] = "/tmp/qbox_lib.XXXXXX";
        int fd = mkstemp(temp_template);
        if (fd < 0) {
            m_last_error = "Unable to create temp file: " + std::string(strerror(errno));
            return nullptr;
        }
        close(fd);

        try {
            fs::copy_file(original_path, temp_template, fs::copy_options::overwrite_existing);
        } catch (const fs::filesystem_error& e) {
            m_last_error = "Failed to create a copy of the library: " + std::string(e.what());
            fs::remove(temp_template);
            return nullptr;
        }

        void* handle = dlopen(temp_template, RTLD_LOCAL | RTLD_NOW);
        if (handle == nullptr) {
            m_last_error = dlerror();
            fs::remove(temp_template);
            return nullptr;
        }

        // Delete temp file - library stays mapped in memory until dlclose()
        fs::remove(temp_template);

        return std::make_shared<Library>(handle);
#endif
    }

public:
    qemu::LibraryLoaderIface::LibraryIfacePtr load_library(const std::string& lib_name, bool load_as_separate_instance)
    {
        // If a separate instance of the shared library is requested and the library has been already loaded
        // the library must be copied into a new location and loaded from there.
        if (load_as_separate_instance && is_library_loaded(lib_name)) {
            std::cout << "Loading separate instance of library: " << lib_name << std::endl;
            return duplicate_and_load_library(lib_name);
        }

        // Load the library
        void* handle = load_library_internal(lib_name);
        if (handle == nullptr) {
            m_last_error = get_last_error();
            return nullptr;
        }

        return std::make_shared<Library>(handle);
    }

    const char* get_lib_ext()
    {
#if defined(_WIN32)
        return "dll";
#elif defined(__APPLE__)
        return "dylib";
#else
        return "so";
#endif
    }

    const char* get_last_error() { return m_last_error.c_str(); }
};

qemu::LibraryLoaderIface& qemu::get_default_lib_loader()
{
    static DefaultLibraryLoader loader;
    return loader;
}
