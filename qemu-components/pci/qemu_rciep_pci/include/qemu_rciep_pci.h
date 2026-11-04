/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * qemu_rciep_pci: QBox wrapper for the QEMU "rciep" PCIe device.
 *
 * Derives from qemu_gpex::Device, registers with the gpex bus, and
 * forwards CCI parameters to the underlying QEMU properties:
 *   devfn          PCI devfn (dev<<3 | fn) → "addr" property
 *   vendor_id      uint16 → "vendor-id"
 *   device_id      uint16 → "device-id"
 *   class_id       uint16 → "class-id"
 *   revision       uint8  → "revision"
 *   num_vfs        uint16 → "num-vfs"
 *   num_msi_vectors uint16 → "num-msi-vectors"
 *   private_base   uint64 → "private-base"   (reserved — not consumed yet)
 *   pf_shadow_base uint64 → "pf-shadow-base" (reserved — not consumed yet)
 *   for BAR i (6 BARs are usable, from 0 to 5 included):
 *      - bari_initiator_socket -> backing memory for BAR i
 *      - bari_size -> the size of the BAR (can be smaller that the backing memory size)
 *      - bari_is_64bits -> true if the BAR should be addressable on 64 bits (it will then occupy BAR i and BAR i + 1)
 *      - bari_prefetchable -> true if the BAR is prefetchable
 */

#ifndef _LIBQBOX_COMPONENTS_PCI_RCIEP_H
#define _LIBQBOX_COMPONENTS_PCI_RCIEP_H

#include <sstream>
#include <iomanip>
#include <array>
#include <string>

#include <cci_configuration>
#include "libqemu-cxx/libqemu-cxx.h"
#include "ports/initiator.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"

#include <libgssync.h>
#include <qemu-instance.h>
#include <module_factory_registery.h>

#include <tlm_utils/multi_passthrough_target_socket.h>

#include <qemu_gpex.h>

class qemu_rciep_pci : public qemu_gpex::Device
{
    static constexpr unsigned num_bars = 6;

    struct Bar {
        unsigned id;

        cci::cci_param<uint64_t> size;
        cci::cci_param<bool> is_64bits;
        cci::cci_param<bool> prefetchable;

        QemuInitiatorSocket<> initiator_socket;

        static std::string cci_bar_name(unsigned id, const char* suffix)
        {
            return "bar" + std::to_string(id) + "_" + suffix;
        }

        static std::string qemu_bar_name(unsigned id, const char* suffix)
        {
            return "bar" + std::to_string(id) + "-" + suffix;
        }

        static std::string cci_socket_name(unsigned id) { return "bar" + std::to_string(id) + "_initiator_socket"; }

        Bar(unsigned id, qemu_gpex& gpex, QemuInstance& inst)
            : id(id)
            , size(cci_bar_name(id, "size").c_str(), 0, "PCI BAR size (in bytes)")
            , is_64bits(cci_bar_name(id, "is_64bits").c_str(), false, "PCI BAR uses two PCI bar slots")
            , prefetchable(cci_bar_name(id, "prefetchable").c_str(), false, "PCI BAR is prefetchable")
            , initiator_socket(cci_socket_name(id).c_str(), gpex, inst)
        {
        }

        Bar(const Bar&) = delete;
        Bar& operator=(const Bar&) = delete;

        void configure(qemu::Device& dev)
        {
            dev.set_prop_uint(qemu_bar_name(id, "size").c_str(), size.get_value());
            dev.set_prop_bool(qemu_bar_name(id, "is-64bits").c_str(), is_64bits.get_value());
            dev.set_prop_bool(qemu_bar_name(id, "prefetchable").c_str(), prefetchable.get_value());

            if (size.get_value() != 0) {
                initiator_socket.init(dev, qemu_bar_name(id, "target").c_str());
            }
        }
    };

public:
    cci::cci_param<uint32_t> p_devfn;
    cci::cci_param<uint32_t> p_vendor_id;
    cci::cci_param<uint32_t> p_device_id;
    cci::cci_param<uint32_t> p_class_id;
    cci::cci_param<uint32_t> p_revision;
    cci::cci_param<uint32_t> p_num_vfs;
    cci::cci_param<uint64_t> p_private_base;
    cci::cci_param<uint64_t> p_pf_shadow_base;

    cci::cci_param<uint32_t> p_num_msi_vectors;

    cci::cci_param<uint16_t> p_num_msix_vectors;
    cci::cci_param<uint8_t> p_msix_table_bar;
    cci::cci_param<uint32_t> p_msix_table_offset;
    cci::cci_param<uint8_t> p_msix_pba_bar;
    cci::cci_param<uint32_t> p_msix_pba_offset;

    tlm_utils::multi_passthrough_target_socket_optional<qemu_rciep_pci> p_msi_target_socket;
    tlm_utils::multi_passthrough_target_socket_optional<qemu_rciep_pci> p_msix_target_socket;

    qemu_rciep_pci(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_rciep_pci(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_gpex*>(t)))
    {
    }

    qemu_rciep_pci(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
        : qemu_gpex::Device(name, inst, "rciep")
        , p_devfn("devfn", 0, "PCI devfn (dev<<3 | fn)")
        , p_vendor_id("vendor_id", 0x17CB, "PCI Vendor ID")
        , p_device_id("device_id", 0x0D0A, "PCI Device ID")
        , p_class_id("class_id", 0x1200, "PCI class code")
        , p_revision("revision", 0x00, "PCI revision ID")
        , p_num_vfs("num_vfs", 0, "Number of SR-IOV VFs")
        , p_private_base("private_base", 0, "Reserved — not consumed yet (future CPU-facing PRIVATE window)")
        , p_pf_shadow_base("pf_shadow_base", 0, "Reserved — not consumed yet (future config-monitor mirror)")
        , p_msi_target_socket("msi_target_socket")
        , p_num_msi_vectors(
              "num_msi_vectors", 4,
              "MSI vectors advertised per PF (keep small vs platform IRQ pool, e.g. GICv2m 64 SPIs) - 0 to disable")
        , p_msix_target_socket("msix_target_socket")
        , p_num_msix_vectors("num_msix_vectors", 0, "Number of MSI-X vectors (0 to disable)")
        , p_msix_table_bar("msix_table_bar", 0, "MSI-X table bar")
        , p_msix_table_offset("msix_table_offset", 0, "MSI-X table offset")
        , p_msix_pba_bar("msix_pba_bar", 0, "MSI-X pba bar")
        , p_msix_pba_offset("msix_pba_offset", 0, "MSI-X pba offset")
        , bars{ {
              { 0, *gpex, inst },
              { 1, *gpex, inst },
              { 2, *gpex, inst },
              { 3, *gpex, inst },
              { 4, *gpex, inst },
              { 5, *gpex, inst },
          } }
    {
        p_msi_target_socket.register_b_transport(this, &qemu_rciep_pci::msi_b_transport);
        p_msix_target_socket.register_b_transport(this, &qemu_rciep_pci::msix_b_transport);

        gpex->add_device(*this);
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();

        /* Convert devfn to QEMU "addr" property string: "DD.F" */
        uint32_t devfn = p_devfn.get_value();
        uint32_t dev = (devfn >> 3) & 0x1f;
        uint32_t fn = devfn & 0x7;
        std::ostringstream addr_str;
        addr_str << std::hex << std::setfill('0') << std::setw(2) << dev << "." << fn;
        m_dev.set_prop_str("addr", addr_str.str().c_str());

        m_dev.set_prop_uint("vendor-id", p_vendor_id.get_value());
        m_dev.set_prop_uint("device-id", p_device_id.get_value());
        m_dev.set_prop_uint("class-id", p_class_id.get_value());
        m_dev.set_prop_uint("revision", p_revision.get_value());
        m_dev.set_prop_uint("num-vfs", p_num_vfs.get_value());
        m_dev.set_prop_uint("private-base", p_private_base.get_value());
        m_dev.set_prop_uint("pf-shadow-base", p_pf_shadow_base.get_value());

        m_dev.set_prop_uint("num-msi-vectors", p_num_msi_vectors.get_value());

        m_dev.set_prop_uint("num-msix-vectors", p_num_msix_vectors.get_value());
        m_dev.set_prop_uint("msix-table-bar", p_msix_table_bar.get_value());
        m_dev.set_prop_uint("msix-table-offset", p_msix_table_offset.get_value());
        m_dev.set_prop_uint("msix-pba-bar", p_msix_pba_bar.get_value());
        m_dev.set_prop_uint("msix-pba-offset", p_msix_pba_offset.get_value());

        for (auto& bar : bars) {
            bar.configure(m_dev);
        }
    }

private:
    std::array<Bar, num_bars> bars;

    void msi_b_transport(int, tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        txn.set_dmi_allowed(false);

        if (txn.get_command() != tlm::TLM_WRITE_COMMAND || txn.get_address() != 0 ||
            txn.get_data_length() != sizeof(uint32_t) || txn.get_data_ptr() == nullptr ||
            txn.get_byte_enable_ptr() != nullptr) {
            txn.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        uint32_t vector;
        std::memcpy(&vector, txn.get_data_ptr(), sizeof(vector));

        if (qemu::PciDevice(m_dev).msi_notify(vector)) {
            txn.set_response_status(tlm::TLM_OK_RESPONSE);
        } else {
            txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        }
    }

    void msix_b_transport(int, tlm::tlm_generic_payload& txn, sc_core::sc_time&)
    {
        txn.set_dmi_allowed(false);

        if (txn.get_command() != tlm::TLM_WRITE_COMMAND || txn.get_address() != 0 ||
            txn.get_data_length() != sizeof(uint32_t) || txn.get_data_ptr() == nullptr ||
            txn.get_byte_enable_ptr() != nullptr) {
            txn.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            return;
        }

        uint32_t vector;
        std::memcpy(&vector, txn.get_data_ptr(), sizeof(vector));

        if (qemu::PciDevice(m_dev).msix_notify(vector)) {
            txn.set_response_status(tlm::TLM_OK_RESPONSE);
        } else {
            txn.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        }
    }
};

extern "C" void module_register();

#endif /* _LIBQBOX_COMPONENTS_PCI_RCIEP_H */
