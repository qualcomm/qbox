/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "sysc/utils/sc_vector.h"

#include <libqemu-cxx/target/aarch64.h>

#include <module_factory_registery.h>
#include <arm.h>
#include <ports/qemu-target-signal-socket.h>

class cpu_arm_cortexM3 : public QemuCpuArm
{
private:
    qemu::Clock m_clk;

public:
    cci::cci_param<bool> p_start_powered_off;
    cci::cci_param<uint64_t> p_pmsav7_dregion;
    cci::cci_param<uint64_t> p_clock_hz;
    cci::cci_param<uint64_t> p_num_irq;
    cci::cci_param<uint64_t> p_init_nsvtor;

    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;

    cpu_arm_cortexM3(const sc_core::sc_module_name& name, sc_core::sc_object* inst);
    cpu_arm_cortexM3(sc_core::sc_module_name name, QemuInstance& inst);

    void before_end_of_elaboration() override;
    void end_of_elaboration() override;
};

extern "C" void module_register();
