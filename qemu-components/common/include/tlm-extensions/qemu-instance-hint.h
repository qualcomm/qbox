/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_TLM_EXTENSIONS_QEMU_INSTANCE_HINT_H
#define _LIBQBOX_TLM_EXTENSIONS_QEMU_INSTANCE_HINT_H

#include <tlm>

class QemuInstance;

struct QemuInstanceHintTlmExtension : tlm::tlm_extension<QemuInstanceHintTlmExtension> {
    QemuInstance* instance;
    explicit QemuInstanceHintTlmExtension(QemuInstance& inst): instance(&inst) {}
    tlm_extension_base* clone() const override { return new QemuInstanceHintTlmExtension(*instance); }
    void copy_from(const tlm_extension_base& ext) override
    {
        instance = static_cast<const QemuInstanceHintTlmExtension&>(ext).instance;
    }
};

#endif
