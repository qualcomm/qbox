/*
 *  This file is part of libqbox
 *  Copyright (c) 2023 Qualcomm Innovation Center, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <virtio_gpu_gl_pci.h>

#include <libqemu/config-host.h>

virtio_gpu_gl_pci::virtio_gpu_gl_pci(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
    : virtio_gpu_gl_pci(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_gpex*>(t)))
{
}
virtio_gpu_gl_pci::virtio_gpu_gl_pci(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
    : QemuVirtioGpu(name, inst, _device_type, gpex, std::string(name).append(".").append(_device_type).c_str())
{
#ifndef __APPLE__
#ifdef HAVE_VIRGL_RESOURCE_BLOB
    // Host-visible / blob setup (shared by QemuVirtioGpu, gated by
    // enable_host_visible). Not available on MacOS (no memfd support).
    setup_host_visible_backend();
#endif // HAVE_VIRGL_RESOURCE_BLOB
#endif // NOT __APPLE__
}

void virtio_gpu_gl_pci::before_end_of_elaboration()
{
    QemuVirtioGpu::before_end_of_elaboration();

#ifndef __APPLE__
#ifdef HAVE_VIRGL_RESOURCE_BLOB
    apply_host_visible_props();
#endif // HAVE_VIRGL_RESOURCE_BLOB
    get_qemu_dev().set_prop_bool("context_init", true);
#endif // NOT __APPLE__
}

void module_register() {
    GSC_MODULE_REGISTER_C(virtio_gpu_gl_pci, sc_core::sc_object*, sc_core::sc_object*);
}
