/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu-cxx/libqemu-cxx.h>

namespace qemu {

const char* get_target_name(Target t)
{
    switch (t) {
    case AARCH64:
        return "AArch64";

    case ARM:
        return "ARM (32-bit)";

    case RISCV64:
        return "RISC-V (64 bits)";

    case RISCV32:
        return "RISC-V (32 bits)";

    case MICROBLAZE:
        return "Microblaze";

    case MICROBLAZEEL:
        return "Microblaze (little-endian)";

    case HEXAGON:
        return "Hexagon";

    default:
        return "";
    }
}

#ifndef LIBQEMU_TARGET_aarch64_LIBRARY
#define LIBQEMU_TARGET_aarch64_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_arm_LIBRARY
#define LIBQEMU_TARGET_arm_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_riscv64_LIBRARY
#define LIBQEMU_TARGET_riscv64_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_riscv32_LIBRARY
#define LIBQEMU_TARGET_riscv32_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_microblaze_LIBRARY
#define LIBQEMU_TARGET_microblaze_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_microblazeel_LIBRARY
#define LIBQEMU_TARGET_microblazeel_LIBRARY nullptr
#endif

#ifndef LIBQEMU_TARGET_hexagon_LIBRARY
#define LIBQEMU_TARGET_hexagon_LIBRARY nullptr
#endif

const char* get_target_lib(Target t)
{
    switch (t) {
    case AARCH64:
        return LIBQEMU_TARGET_aarch64_LIBRARY;

    case ARM:
        return LIBQEMU_TARGET_arm_LIBRARY;

    case RISCV64:
        return LIBQEMU_TARGET_riscv64_LIBRARY;

    case RISCV32:
        return LIBQEMU_TARGET_riscv32_LIBRARY;

    case MICROBLAZE:
        return LIBQEMU_TARGET_microblaze_LIBRARY;

    case MICROBLAZEEL:
        return LIBQEMU_TARGET_microblazeel_LIBRARY;

    case HEXAGON:
        return LIBQEMU_TARGET_hexagon_LIBRARY;

    default:
        return "";
    }
}

} // namespace qemu
