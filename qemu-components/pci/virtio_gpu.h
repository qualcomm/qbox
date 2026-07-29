/*
 * This file is part of libqbox
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_VIRTIO_GPU_H
#define _LIBQBOX_COMPONENTS_VIRTIO_GPU_H

#include <cstdint>
#include <string>

#include <cci_configuration>

#include <libgssync.h>

#include <qemu_gpex/include/qemu_gpex.h>

class QemuVirtioGpu : public qemu_gpex::Device
{
public:
    cci::cci_param<uint8_t> p_outputs;
    cci::cci_param<uint64_t> p_hostmem_mb;
    cci::cci_param<bool> p_enable_host_visible;

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();
        get_qemu_dev().set_prop_uint("max_outputs", p_outputs);
    }

    void gpex_realize(qemu::Bus& bus) override { qemu_gpex::Device::gpex_realize(bus); }

    uint8_t outputs() { return p_outputs.get_value(); }

protected:
    QemuVirtioGpu(const sc_core::sc_module_name& name, QemuInstance& inst, const char* gpu_type, qemu_gpex* gpex)
        : QemuVirtioGpu(name, inst, gpu_type, gpex, (const char*)NULL)
    {
    }

    QemuVirtioGpu(const sc_core::sc_module_name& name, QemuInstance& inst, const char* gpu_type, qemu_gpex* gpex,
                  const char* device_id)
        : qemu_gpex::Device(name, inst, gpu_type, device_id)
        , p_outputs("outputs", 1, "Number of outputs for this gpu")
        , p_hostmem_mb("hostmem_mb", 2048, "MB to allocate for the host-visible shared-memory region")
        , p_enable_host_visible("enable_host_visible", false,
                                "Enable virtio-gpu blob resources and the host-visible shared-memory region "
                                "(virglrenderer >= 1.0 / QEMU >= 11). Defaults to false to keep the pre-1.0 "
                                "PCI layout (no SHARED_MEMORY_CFG capability / large BAR), which all guests "
                                "support. Set true for guests that support and benefit from host-visible blob "
                                "resources (e.g. modern Linux/Mesa).")
    {
        gpex->add_device(*this);
    }

    /*
     * Host-visible / blob-resource setup shared by the 3D GPU variants
     * (virtio-gpu-gl/cl/qnn). The availability of the blob API is a per-renderer
     * compile-time decision, so callers must guard these with their own
     * HAVE_*_RESOURCE_BLOB macro; at runtime the setup is gated by the
     * enable_host_visible parameter.
     */

    /* Call from the derived constructor: adds the memfd host-visible backend. */
    void setup_host_visible_backend()
    {
        if (!p_enable_host_visible) {
            return;
        }
        m_inst.add_arg("-object");
        std::string memory_object = "memory-backend-memfd,id=mem1,size=" + std::to_string(p_hostmem_mb.get_value()) +
                                    "M";
        m_inst.add_arg(memory_object.c_str());
        m_inst.add_arg("-machine");
        m_inst.add_arg("memory-backend=mem1");
    }

    /* Call from the derived before_end_of_elaboration(): sets the device props. */
    void apply_host_visible_props()
    {
        if (!p_enable_host_visible) {
            return;
        }
        get_qemu_dev().set_prop_bool("blob", true);
        /*
         * QEMU's "hostmem" is a DEFINE_PROP_SIZE property expressed in BYTES, but
         * p_hostmem_mb is in MB. Convert (64-bit: 2048 * 1MB overflows int).
         */
        get_qemu_dev().set_prop_int("hostmem", (int64_t)p_hostmem_mb.get_value() * 1024 * 1024);
    }
};
#endif
