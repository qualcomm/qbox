/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

namespace qemu {

enum Target {
    AARCH64,
    ARM,
    RISCV64,
    RISCV32,
    MICROBLAZE,
    MICROBLAZEEL,
    HEXAGON,
};

const char* get_target_name(Target t);
const char* get_target_lib(Target t);

} // namespace qemu
