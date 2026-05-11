// Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TEST_H
#define TEST_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <systemc>

#include "argparser.h"
#include "cciutils.h"
#include "cci/utils/broker.h"
#include "gs_memory.h"
#include "scp/report.h"

bool run_systemc()
{
    bool test_failed = true;

    try {
        sc_core::sc_start();
        if (sc_core::SC_STOPPED != sc_core::sc_get_status()) {
            sc_core::sc_stop();
        }

        test_failed = false;
    } catch (std::exception& e) {
        std::cerr << "Test failure: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Test failure: Unknown exception\n";
    }

    return test_failed;
}

/*
 * Load a pre-assembled firmware .bin (built by llvm-mc + ld.lld +
 * llvm-objcopy at CMake-configure time). Each entry in patch_words overwrites
 * a 32-bit word at the tail of the binary — the Nth entry lands at
 * (size - (N+1)*4). The firmware .S file declares matching .word placeholders
 * at the end of its .data section.
 */
inline void load_firmware(gs::gs_memory<>& destination, const char* bin_path, uint64_t addr,
                          std::initializer_list<uint32_t> patch_words = {})
{
    std::ifstream file(bin_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        SCP_FATAL() << "Failed to open firmware file: " << bin_path;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        SCP_FATAL() << "Failed to read firmware file: " << bin_path;
    }

    const size_t needed = patch_words.size() * sizeof(uint32_t);
    if (data.size() < needed) {
        SCP_FATAL() << "Firmware " << bin_path << " size " << data.size() << " < " << needed
                    << " bytes needed for patches";
    }
    size_t idx = 0;
    for (uint32_t v : patch_words) {
        size_t offset = data.size() - (patch_words.size() - idx) * sizeof(uint32_t);
        std::memcpy(data.data() + offset, &v, sizeof(uint32_t));
        ++idx;
    }

    destination.load.ptr_load(data.data(), addr, data.size());
}

template <typename T>
bool run_test(int argc, char* argv[])
{
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::INFO)
                                        .msgTypeFieldWidth(30));

    gs::ConfigurableBroker broker{};
    cci::cci_originator originator{ "test" };

    auto broker_handle = broker.create_broker_handle(originator);
    ArgParser arg_parser{ broker_handle, argc, argv };

    T test{ "test" };

    bool result = run_systemc();
    return result;
}

#define RUN_TEST(test) \
    int sc_main(int argc, char* argv[]) { return run_test<test>(argc, argv); }

class TestFailureException : public std::runtime_error
{
protected:
    std::string* m_what;

    const char* make_what(const char* what, const char* func, const char* file, int line)
    {
        std::stringstream ss;
        ss << what << " (in " << func << ", at " << file << ":" << line << ")";

        m_what = new std::string(ss.str());

        return m_what->c_str();
    }

public:
    TestFailureException(const char* what, const char* func, const char* file, int line)
        : std::runtime_error(make_what(what, func, file, line))
    {
    }

    virtual ~TestFailureException() noexcept { delete m_what; }
};

#define TEST_FAIL(what)                                                 \
    do {                                                                \
        throw TestFailureException(what, __func__, __FILE__, __LINE__); \
    } while (0)

#define TEST_ASSERT(assertion)                              \
    do {                                                    \
        if (!(assertion)) {                                 \
            TEST_FAIL("assertion `" #assertion "' failed"); \
        }                                                   \
    } while (0)

#endif
