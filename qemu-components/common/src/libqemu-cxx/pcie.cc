/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu/libqemu.h>
#include <internals.h>
#include <libqemu-cxx/libqemu-cxx.h>

namespace qemu {

struct PCIeDevice::CallbackOwner::State {
    PCIeDevice dev;
    Callbacks cbs;

    State(const PCIeDevice& dev, Callbacks cbs): dev(dev), cbs(std::move(cbs)) {}

    ~State() { dev.m_int->exports().pcie_clear_callbacks(reinterpret_cast<QemuPCIDevice*>(dev.get_qemu_obj()), this); }

    template <typename Cb, typename... Args>
    static void dispatch_cb(void* opaque, Cb Callbacks::* member, Args&&... args)
    {
        auto& state = *static_cast<State*>(opaque);
        Object dev(state.dev);

        auto cb = state.cbs.*member;
        if (cb) {
            cb(std::forward<Args>(args)...);
        }
    }

    static void on_bme_updated(PCIDevice* dev, void* opaque, bool enabled)
    {
        dispatch_cb(opaque, &Callbacks::on_bme_changed, enabled);
    }

    static void on_vf_enable_updated(PCIDevice* dev, void* opaque, bool enabled)
    {
        dispatch_cb(opaque, &Callbacks::on_vf_enable_changed, enabled);
    }

    static void on_msi_config_changed(PCIDevice* dev, void* opaque, const LibQemuPCIeMsiConfig* msi_config)
    {
        dispatch_cb(opaque, &Callbacks::on_msi_config_changed, *msi_config);
    }

    static void on_flr(PCIDevice* dev, void* opaque) { dispatch_cb(opaque, &Callbacks::on_flr); }

    static void on_reset(PCIDevice* dev, void* opaque) { dispatch_cb(opaque, &Callbacks::on_reset); }
};

PCIeDevice::CallbackOwner::CallbackOwner(std::unique_ptr<State> state): m_state(std::move(state)) {}

PCIeDevice::CallbackOwner::CallbackOwner() = default;
PCIeDevice::CallbackOwner::~CallbackOwner() = default;
PCIeDevice::CallbackOwner::CallbackOwner(CallbackOwner&&) noexcept = default;
PCIeDevice::CallbackOwner& PCIeDevice::CallbackOwner::operator=(CallbackOwner&&) noexcept = default;
void PCIeDevice::CallbackOwner::reset() { m_state.reset(); }

bool PCIeDevice::msi_notify(unsigned vector) { return m_int->exports().pcie_msi_notify(pdev(), vector); }

bool PCIeDevice::msix_notify(unsigned vector) { return m_int->exports().pcie_msix_notify(pdev(), vector); }

bool PCIeDevice::get_cb_data(LibQemuPCIeCbData& cb_data)
{
    return m_int->exports().pcie_callbacks_data(pdev(), &cb_data);
}

PCIeDevice::CallbackOwner PCIeDevice::set_callbacks(Callbacks cbs)
{
    if (cbs.empty() || !valid()) {
        return {};
    }

    using State = CallbackOwner::State;

    auto state = std::make_unique<State>(*this, std::move(cbs));
    LibQemuPCIeCallbacks c_cbs{};
    auto& callbacks = state->cbs;

    if (callbacks.on_bme_changed) c_cbs.on_bme_updated = State::on_bme_updated;
    if (callbacks.on_vf_enable_changed) c_cbs.on_vf_enabled_updated = State::on_vf_enable_updated;
    if (callbacks.on_msi_config_changed) c_cbs.on_msi_config_changed = State::on_msi_config_changed;
    if (callbacks.on_flr) c_cbs.on_flr = State::on_flr;
    if (callbacks.on_reset) c_cbs.on_reset = State::on_reset;

    if (!m_int->exports().pcie_register_callbacks(pdev(), state.get(), &c_cbs)) {
        return {};
    }

    return CallbackOwner(std::move(state));
}

QemuPCIDevice* PCIeDevice::pdev() const noexcept { return reinterpret_cast<QemuPCIDevice*>(m_obj); }

} // namespace qemu