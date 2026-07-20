/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <libqemu-cxx/libqemu-cxx.h>

#define DSP_REV(arch)     \
    {                     \
#arch, arch##_rev \
    }

namespace qemu {
class CpuHexagon : public Cpu
{
public:
    typedef enum {
        unknown_rev = 0x0,
        v66_rev = 0xa666,
        v68_rev = 0x8d68,
        v69_rev = 0x8c69,
        v73_rev = 0x8c73,
        v79_rev = 0x8c79,
        v81_rev = 0x8781,
        v85_rev = 0x8085,
        v89_rev = 0xa289,
        v91_rev = 0x8291,
    } Rev_t;

    static constexpr const char* const TYPE = "v67-hexagon-cpu";

    using MipUpdateCallbackFn = std::function<void(uint32_t)>;

    CpuHexagon() = default;
    CpuHexagon(const CpuHexagon&) = default;
    CpuHexagon(const Object& o): Cpu(o) {}
    void register_reset();
    static Rev_t parse_dsp_arch(const std::string arch_str)
    {
        static const std::unordered_map<std::string, Rev_t> DSP_REVS = { DSP_REV(v66), DSP_REV(v68), DSP_REV(v69),
                                                                         DSP_REV(v73), DSP_REV(v79), DSP_REV(v81),
                                                                         DSP_REV(v85), DSP_REV(v89), DSP_REV(v91) };
        auto rev = DSP_REVS.find(arch_str);
        Rev_t dsp_rev = unknown_rev;
        if (rev != DSP_REVS.end()) {
            dsp_rev = rev->second;
        }
        return dsp_rev;
    }
};
} // namespace qemu
