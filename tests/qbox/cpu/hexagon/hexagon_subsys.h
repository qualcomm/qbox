/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <systemc>

#include "hexagon.h"
#include "hexagon_globalreg.h"
#include "hexagon_tlb.h"
#include "hexagon-l2vic.h"
#include "qemu_hexagon_qtimer.h"
#include "qemu-instance.h"

/*
 * The per-core devices that QEMU's Hexagon CPU needs in order to realize:
 * the TLB, the L2VIC and the global registers (which in turn need the L2VIC
 * and the QTimer).  One of these is shared by all hardware threads of a core,
 * i.e. by all the CPUs living in the same QemuInstance.
 */
class HexagonSubsys
{
public:
    hexagon_l2vic l2vic;
    qemu_hexagon_qtimer qtimer;
    hexagon_globalreg gregs;
    hexagon_tlb tlb;

    HexagonSubsys(const std::string& prefix, QemuInstance& inst)
        : l2vic((prefix + "_l2vic").c_str(), inst)
        , qtimer((prefix + "_qtimer").c_str(), inst)
        , gregs((prefix + "_gregs").c_str(), inst, &qtimer, &l2vic)
        , tlb((prefix + "_tlb").c_str(), inst)
    {
    }

    /* Link a CPU to this core. @htid is the hardware thread id within it. */
    void attach(qemu_cpu_hexagon& cpu, uint32_t htid)
    {
        cpu.p_htid = htid;
        cpu.set_hex_greg(&gregs);
        cpu.set_l2vic(&l2vic);
        cpu.set_hex_tlb(&tlb);
    }
};
