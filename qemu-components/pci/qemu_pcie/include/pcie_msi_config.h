#pragma once

#include <cstdint>
#include <systemc>

namespace gs {

struct PCIeMsiConfig {
    bool enabled = false;
    uint64_t addr = 0;
    uint32_t data = 0;

    bool operator ==(const PCIeMsiConfig& other) const
    {
        return addr == other.addr && data == other.data && enabled == other.enabled;
    }

    bool operator!=(const PCIeMsiConfig& other) const
    {
        return !(*this == other);
    }
};

struct PCIeMsiConfigIf: sc_core::sc_interface {
    virtual void update_msi_config(const PCIeMsiConfig& config) = 0;
};

using PCIeMsiConfigExport = sc_core::sc_export<PCIeMsiConfigIf>;

}