/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <libqemu/libqemu.h>

#include <internals.h>
#include <libqemu-cxx/libqemu-cxx.h>

namespace qemu {

bool PciDevice::msi_notify(unsigned vector)
{
    auto* pci_dev = reinterpret_cast<QemuPCIDevice*>(m_obj);

    return m_int->exports().pci_msi_notify(pci_dev, vector);
}

bool PciDevice::msix_notify(unsigned vector)
{
    auto* pci_dev = reinterpret_cast<QemuPCIDevice*>(m_obj);

    return m_int->exports().pci_msix_notify(pci_dev, vector);
}

} // namespace qemu