/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

namespace gs {

class security_attributes_extension : public tlm::tlm_extension<security_attributes_extension>
{
public:
    bool secure = false;
    bool debug = false;

    tlm_extension_base* clone() const override { return new security_attributes_extension(*this); }

    void copy_from(const tlm_extension_base& other) override
    {
        *this = static_cast<const security_attributes_extension&>(other);
    }
};

} // namespace gs
