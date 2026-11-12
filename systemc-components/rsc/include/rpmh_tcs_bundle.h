/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef QUIC_RPMH_TCS_BUNDLE_H
#define QUIC_RPMH_TCS_BUNDLE_H

#include <tlm>
#include <vector>
#include <cstdint>

namespace quic {

enum class rpmh_state : uint8_t {
    ACTIVE_ONLY = 0,
    WAKE_ONLY = 1,
    SLEEP = 2,
};

struct rpmh_tcs_cmd {
    uint32_t msgid;
    uint32_t addr;
    uint32_t data;
    bool wait_for_cmpl;
};

struct rpmh_tcs_cmd_result {
    uint32_t read_resp;
    uint32_t cmd_status;
};

class rpmh_tcs_bundle_ext : public tlm::tlm_extension<rpmh_tcs_bundle_ext>
{
public:
    uint32_t drv_id = 0;
    uint32_t tcs_id = 0;
    uint32_t cmd_enable = 0;
    rpmh_state state = rpmh_state::ACTIVE_ONLY;
    std::vector<rpmh_tcs_cmd> cmds;
    std::vector<rpmh_tcs_cmd_result> results;
    uint32_t tcs_status_or = 0;
    bool success = true;

    tlm::tlm_extension_base* clone() const override { return new rpmh_tcs_bundle_ext(*this); }
    void copy_from(const tlm::tlm_extension_base& ext) override
    {
        *this = static_cast<const rpmh_tcs_bundle_ext&>(ext);
    }
};

} // namespace quic

#endif
