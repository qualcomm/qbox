#pragma once

#include <sstream>
#include <string_view>

#include <cci_configuration>
#include "libqemu-cxx/libqemu-cxx.h"
#include "ports/initiator-signal-socket.h"
#include "ports/bindable.h"
#include "sysc/communication/sc_port.h"
#include "sysc/kernel/sc_stage_callback_if.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"
#include "tlm_utils/multi_passthrough_target_socket.h"
#include <qemu_gpex.h>
#include "pcie_msi_config.h"

class qemu_pcie : public qemu_gpex::Device, private sc_core::sc_stage_callback_if
{
    static constexpr const char* DEVICE_NAME = "libqemu-pcie";
    static constexpr unsigned BARS_NB = 6;
    static constexpr const uint8_t PCI_TYPE_ENDPOINT = 0x0;

    using MsiPort = gs::bindable_port<gs::PCIeMsiConfigIf>;

    struct BarBase {
        const std::string cci_prefix;
        const std::string qemu_prefix;

        std::size_t idx;

        cci::cci_param<uint64_t> p_size;
        cci::cci_param<uint64_t> p_target_offset;
        QemuInitiatorSocket<> p_initiator_socket;

        std::string cci_name(std::string_view suffix)
        {
            return cci_prefix + "_" + std::string(suffix);
        }

        std::string qemu_name(std::string_view suffix)
        {
            return qemu_prefix + "-" + std::string(suffix);
        }

        BarBase(size_t idx, std::string_view cci_kind, std::string_view qemu_kind, qemu_gpex& gpex, QemuInstance& inst)
            : cci_prefix(std::string(cci_kind) + std::to_string(idx))
            , qemu_prefix(std::string(qemu_kind) + std::to_string(idx))
            , p_size(cci_name("size"), 0, "BAR size, in bytes")
            , p_initiator_socket(cci_name("initiator_socket"), gpex, inst)
            , p_target_offset(cci_name("target_offset"), 0, "Offset in the backing memory region")
        {}

        void configure(qemu::Device& dev)
        {
            dev.set_prop_uint(qemu_name("size"), p_size.get_value());
            dev.set_prop_uint(qemu_name("mr-offset").c_str(), p_target_offset.get_value());

            if (p_size.get_value() != 0) {
                p_initiator_socket.init(dev, qemu_name("mr"));
            }
        }
    };

    struct Bar : public BarBase {
        cci::cci_param<bool> p_is_prefetchable;
        cci::cci_param<bool> p_is_64b;

        Bar(size_t idx, qemu_gpex& gpex, QemuInstance& inst)
            : BarBase(idx, "bar", "bar", gpex, inst)
            , p_is_prefetchable(cci_name("prefetchable"), false, "Prefetchable BAR")
            , p_is_64b(cci_name("is_64bits"), false, "64 bits BAR pair")
        {}

        void configure(qemu::Device& dev)
        {
            BarBase::configure(dev);

            dev.set_prop_bool(qemu_name("64b"), p_is_64b.get_value());
            dev.set_prop_bool(qemu_name("prefetchable"), p_is_prefetchable.get_value());
        }
    };

    struct VfBar : public BarBase {
        cci::cci_param<uint64_t> p_stride;

        VfBar(size_t idx, qemu_gpex& gpex, QemuInstance& inst)
            : BarBase(idx, "vf_bar", "vf-bar", gpex, inst)
            , p_stride(cci_name("stride"), 0, "VF BAR stride")
        {}

        void configure(qemu::Device& dev)
        {
            BarBase::configure(dev);

            dev.set_prop_uint(qemu_name("stride"), p_stride.get_value());
        }
    };

public:
    // PCI
    cci::cci_param<uint8_t> p_devfn;
    cci::cci_param<uint16_t> p_vendor_id;
    cci::cci_param<uint16_t> p_device_id;
    cci::cci_param<uint16_t> p_subsystem_vendor_id;
    cci::cci_param<uint16_t> p_subsystem_id;
    cci::cci_param<uint16_t> p_class_id;
    cci::cci_param<uint16_t> p_revision;

    // PCIe
    cci::cci_param<uint8_t> p_pcie_type;
    cci::cci_param<uint8_t> p_pcie_cap_offset;

    // SRIOV
    cci::cci_param<uint16_t> p_sriov_cap_offset;
    cci::cci_param<uint16_t> p_vf_nb;
    cci::cci_param<uint16_t> p_vf_device_id;
    cci::cci_param<uint16_t> p_vf_offset;
    cci::cci_param<uint16_t> p_vf_stride;

    // MSI
    cci::cci_param<uint8_t> p_msi_nb_vectors;
    cci::cci_param<uint8_t> p_msi_cap_offset;
    cci::cci_param<bool> p_msi_64b;

    // MSI-X
    cci::cci_param<uint16_t> p_msix_nb_vectors;
    cci::cci_param<uint8_t> p_msix_table_bar;
    cci::cci_param<uint32_t> p_msix_table_offset;
    cci::cci_param<uint8_t> p_msix_pba_bar;
    cci::cci_param<uint32_t> p_msix_pba_offset;

    // PM
    cci::cci_param<bool> p_pm_enabled;
    cci::cci_param<uint8_t> p_pm_cap_offset;
    cci::cci_param<uint16_t> p_pm_capabilities;
    cci::cci_param<uint16_t> p_pm_control_reset;

    // PASID
    cci::cci_param<uint8_t> p_pasid_width;
    cci::cci_param<uint16_t> p_pasid_cap_offset;
    cci::cci_param<bool> p_pasid_exec;
    cci::cci_param<bool> p_pasid_priv;

    // sockets
    tlm_utils::multi_passthrough_target_socket_optional<qemu_pcie> msi_target_socket;
    tlm_utils::multi_passthrough_target_socket_optional<qemu_pcie> msix_target_socket;
    MsiPort msi_config_port;

    sc_core::sc_port<sc_core::sc_signal_inout_if<bool>, 0, sc_core::SC_ZERO_OR_MORE_BOUND> pf_bme;
    InitiatorSignalSocket<bool> pf_flr;
    InitiatorSignalSocket<bool> pf_vf_enable;
    InitiatorSignalSocket<bool> config_event;

    qemu_pcie(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_pcie(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_gpex*>(t)))
    {
    }

    qemu_pcie(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
        : qemu_gpex::Device(name, inst, DEVICE_NAME)
        , p_devfn("devfn", 0xff, "PCI devfn")
        , p_vendor_id("vendor_id", 0xffff, "Vendor ID")
        , p_device_id("device_id", 0xffff, "Device ID")
        , p_subsystem_vendor_id("subsystem_vendor_id", 0, "Subsystem vendor ID")
        , p_subsystem_id("subsystem_id", 0, "Subsystem ID")
        , p_class_id("class_id", 0xff00, "Class ID")
        , p_revision("revision", 0, "Revision")
        , p_pcie_type("pcie_type", PCI_TYPE_ENDPOINT, "PCIe type (default: endpoint type)")
        , p_pcie_cap_offset("pcie_cap_offset", 0, "PCIe cap offset (0: default)")
        , p_sriov_cap_offset("sriov_cap_offset", 0, "SR-IOV cap offset (0: default)")
        , p_vf_nb("vf_nb", 0, "VF max count (0: disabled)")
        , p_vf_device_id("vf_device_id", 0xffff, "VF device ID")
        , p_vf_offset("vf_offset", 1, "First VF ID offset")
        , p_vf_stride("vf_stride", 1, "VF ID stride")
        , p_msi_nb_vectors("msi_nb_vectors", 0, "MSI vectors (0: disabled)")
        , p_msi_cap_offset("msi_cap_offset", 0, "MSI cap offset (0: default)")
        , p_msi_64b("msi_64b", false, "MSI 64 bits")
        , p_msix_nb_vectors("msix_nb_vectors", 0, "MSI-X vectors (0: disabled)")
        , p_msix_table_bar("msix_table_bar", 0, "MSI-X table BAR")
        , p_msix_table_offset("msix_table_offset", 0, "MSI-X table offset")
        , p_msix_pba_bar("msix_pba_bar", 0, "MSI-X pba BAR")
        , p_msix_pba_offset("msix_pba_offset", 0, "MSI-X pba offset")
        , p_pm_enabled("pm_enabled", false, "PM enabled")
        , p_pm_cap_offset("pm_cap_offset", 0, "PM cap offset (0: default)")
        , p_pm_capabilities("pm_capabilities", 3, "PM capabilities")
        , p_pm_control_reset("pm_control_reset", 0, "PM control reset")
        , p_pasid_width("pasid_width", 0, "PASID width (0: disabled)")
        , p_pasid_cap_offset("pasid_cap_offset", 0, "PASID cap offset (0: default)")
        , p_pasid_exec("pasid_exec", false, "PASID execute permission support")
        , p_pasid_priv("pasid_priv", false, "PASID privileged mod support")
        , msi_target_socket("msi_target_socket")
        , msix_target_socket("msix_target_socket")
        , msi_config_port("msi_config")
        , pf_bme("pf_bme")
        , pf_flr("pf_flr")
        , pf_vf_enable("pf_vf_enable")
        , config_event("config_event")
    {
        for (size_t bar = 0; bar < BARS_NB; ++bar) {
            m_bars[bar] = std::make_unique<Bar>(bar, *gpex, inst);
            m_vf_bars[bar] = std::make_unique<VfBar>(bar, *gpex, inst);
        }

        msi_target_socket.register_b_transport(this, &qemu_pcie::msi_b_transport);
        msix_target_socket.register_b_transport(this, &qemu_pcie::msix_b_transport);

        sc_core::sc_register_stage_callback(*this, sc_core::SC_POST_START_OF_SIMULATION);
        gpex->add_device(*this);
    }

    static std::string devfn_to_addr(uint8_t devfn)
    {
        std::ostringstream addr;
        addr << std::hex << std::setfill('0') << std::setw(2)
             << ((devfn >> 3) & 0x1f) << "." << (devfn & 0x7);
        return addr.str();
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();

        // PCI
        if (auto devfn = p_devfn.get_value(); devfn != 0xff)
            m_dev.set_prop_str("addr", devfn_to_addr(devfn));
        m_dev.set_prop_uint("vendor-id", p_vendor_id.get_value());
        m_dev.set_prop_uint("device-id", p_device_id.get_value());
        m_dev.set_prop_uint("subsystem-vendor-id", p_subsystem_vendor_id.get_value());
        m_dev.set_prop_uint("subsystem-id", p_subsystem_id.get_value());
        m_dev.set_prop_uint("class-id", p_class_id.get_value());
        m_dev.set_prop_uint("revision", p_revision.get_value());

        // PCIe
        m_dev.set_prop_uint("pcie-type", p_pcie_type.get_value());
        if (const auto cap_offset = p_pcie_cap_offset.get_value())
            m_dev.set_prop_uint("pcie-cap-offset", cap_offset);

        // SR-IOV
        if (const auto cap_offset = p_sriov_cap_offset.get_value())
            m_dev.set_prop_uint("sriov-cap-offset", cap_offset);
        m_dev.set_prop_uint("vf-nb", p_vf_nb.get_value());
        m_dev.set_prop_uint("vf-device-id", p_vf_device_id.get_value());
        m_dev.set_prop_uint("vf-offset", p_vf_offset.get_value());
        m_dev.set_prop_uint("vf-stride", p_vf_stride.get_value());

        // MSI
        m_dev.set_prop_uint("msi-nb-vectors", p_msi_nb_vectors.get_value());
        if (const auto cap_offset = p_msi_cap_offset.get_value())
            m_dev.set_prop_uint("msi-cap-offset", cap_offset);
        m_dev.set_prop_bool("msi-64b", p_msi_64b.get_value());

        // MSI-X
        if (const auto nvecs = p_msix_nb_vectors.get_value())
            m_dev.set_prop_uint("msix-nb-vectors", nvecs);
        m_dev.set_prop_uint("msix-table-bar", p_msix_table_bar.get_value());
        m_dev.set_prop_uint("msix-table-offset", p_msix_table_offset.get_value());
        m_dev.set_prop_uint("msix-pba-bar", p_msix_pba_bar.get_value());
        m_dev.set_prop_uint("msix-pba-offset", p_msix_pba_offset.get_value());

        // PM
        m_dev.set_prop_bool("pm-enabled", p_pm_enabled.get_value());
        if (const auto cap_offset = p_pm_cap_offset.get_value())
            m_dev.set_prop_uint("pm-cap-offset", cap_offset);
        m_dev.set_prop_uint("pm-capabilities", p_pm_capabilities.get_value());
        m_dev.set_prop_uint("pm-control-reset", p_pm_control_reset.get_value());

        // PASID
        m_dev.set_prop_uint("pasid-width", p_pasid_width.get_value());
        if (const auto cap_offset = p_pasid_cap_offset.get_value())
            m_dev.set_prop_uint("pasid-cap-offset", cap_offset);
        m_dev.set_prop_bool("pasid-exec", p_pasid_exec.get_value());
        m_dev.set_prop_bool("pasid-priv", p_pasid_priv.get_value());

        for (auto& bar: m_bars) {
            bar->configure(m_dev);
        }

        for (auto& bar: m_vf_bars) {
            bar->configure(m_dev);
        }
    }

protected: 
    void gpex_realize(qemu::Bus& pcie_bus) override
    {
        qemu_gpex::Device::gpex_realize(pcie_bus);
        qemu::PCIeDevice::Callbacks cbs;

        if (pf_bme.size() || config_event.get_interface()) {
            cbs.on_bme_changed = [this](bool enabled) { on_bme_changed(enabled); };
        }

        if (pf_vf_enable.get_interface() || config_event.get_interface()) {
            cbs.on_vf_enable_changed = [this](bool enabled) { on_vf_enable_changed(enabled); };
        }

        if (msi_config_port.size()) {
            cbs.on_msi_config_changed = [this](const LibQemuPCIeMsiConfig& config) {
                on_msi_config_changed({config.enabled, config.addr, config.data});
            };
        }

        cbs.on_flr = [this] { on_reset(true); };

        if (pf_bme.size() || pf_vf_enable.get_interface() || msi_config_port.size()) {
            cbs.on_reset = [this] { on_reset(false); };
        }

        m_cbs = qemu::PCIeDevice(m_dev).set_callbacks(std::move(cbs));
        if (!m_cbs.valid()) {
            SCP_FATAL(())("callbacks config failed");
        }
    }

private:
    std::array<std::unique_ptr<Bar>, BARS_NB> m_bars;
    std::array<std::unique_ptr<VfBar>, BARS_NB> m_vf_bars;
    gs::runonsysc m_on_sysc;
    gs::PCIeMsiConfig m_last_msi_config {};
    bool m_qemu_init_done = false;
    qemu::PCIeDevice::CallbackOwner m_cbs;

    void stage_callback(const sc_core::sc_stage&) override
    {
        m_qemu_init_done = true;
    }

    void clear_pcie_cbs()
    {
        m_qemu_init_done = false;
        m_cbs.reset();
    }

    void on_qemu_event(std::function<void()> evt)
    {
        m_inst.get().unlock_iothread();
        m_on_sysc.run_on_sysc(std::move(evt));
        m_inst.get().lock_iothread();
    }

    template <typename Socket>
    static void write_signal(Socket& socket, bool value)
    {
        for (int i = 0; i < socket.size(); ++i) {
            socket[i]->write(value);
        }
    }

    template <typename Socket>
    static void pulse_signal(Socket& socket)
    {
        for (int i = 0; i < socket.size(); ++i) {
            socket[i]->write(true);
            socket[i]->write(false);
        }
    }

    void commit_msi_config(const gs::PCIeMsiConfig& msi_config)
    {
        if (!msi_config_port.size() || msi_config == m_last_msi_config) {
            return;
        }

        m_last_msi_config = msi_config;

        msi_config_port->update_msi_config(msi_config);
    }

    void on_bme_changed(bool enabled)
    {
        on_qemu_event([this, enabled] {
            write_signal(pf_bme, enabled);
            pulse_signal(config_event);
        });
    }

    void on_vf_enable_changed(bool enabled)
    {
        on_qemu_event([this, enabled] {
            write_signal(pf_vf_enable, enabled);
            pulse_signal(config_event);
        });
    }

    void on_msi_config_changed(gs::PCIeMsiConfig msi_config)
    {
        on_qemu_event([this, msi_config] {
            commit_msi_config(msi_config);
        });
    }

    void on_reset(bool flr)
    {
        if (!m_qemu_init_done) {
            return;
        }

        LibQemuPCIeCbData cb_data{};

        if (!qemu::PCIeDevice(m_dev).get_cb_data(cb_data)) {
            SCP_FATAL(())("Cannot get PCIe CB data");
        }

        on_qemu_event([this, cb_data, flr] {
            write_signal(pf_bme, cb_data.bme);
            write_signal(pf_vf_enable, cb_data.vf_enabled);
            commit_msi_config({ cb_data.msi_config.enabled, cb_data.msi_config.addr, cb_data.msi_config.data });
            if (flr) {
                pulse_signal(pf_flr);
                pulse_signal(config_event);
            }
        });
    }

    static bool decode_vector(tlm::tlm_generic_payload& txn, uint32_t& vec)
    {
        txn.set_dmi_allowed(false);
        if (txn.get_command() != tlm::TLM_WRITE_COMMAND
         || txn.get_address() != 0
         || txn.get_data_length() != sizeof(vec)
         || txn.get_data_ptr() == nullptr) {
            return false;
        }

        std::memcpy(&vec, txn.get_data_ptr(), sizeof(vec));
        return true;
    }

    void msi_b_transport(int, tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        uint32_t vec;
        if (!decode_vector(txn, vec)) {
            txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        txn.set_response_status(qemu::PCIeDevice(m_dev).msi_notify(vec)
            ? tlm::TLM_OK_RESPONSE
            : tlm::TLM_GENERIC_ERROR_RESPONSE);
    }

    void msix_b_transport(int, tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        uint32_t vec;
        if (!decode_vector(txn, vec)) {
            txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        txn.set_response_status(qemu::PCIeDevice(m_dev).msix_notify(vec)
            ? tlm::TLM_OK_RESPONSE
            : tlm::TLM_GENERIC_ERROR_RESPONSE);
    }
};

extern "C" void module_register();
