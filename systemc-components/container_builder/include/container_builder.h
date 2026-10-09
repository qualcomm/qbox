/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef _GREENSOCS_BASE_COMPONENTS_CONTAINER_BUILDER_H
#define _GREENSOCS_BASE_COMPONENTS_CONTAINER_BUILDER_H

#include <systemc>
#include <tlm>
#include <tlm_utils/peq_with_get.h>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <module_factory_registery.h>
#include <ports/target-signal-socket.h>
#include <ports/initiator-signal-socket.h>
#include <module_factory_container.h>
#include <string>
#include <map>
#include <vector>
#include <memory>

namespace gs {
class container_builder : public gs::ModuleFactory::ContainerDeferModulesConstruct
{
public:
    container_builder(sc_core::sc_module_name _name);
    ~container_builder() = default;

protected:
    void dispatch(const std::string& _name);
    void redirect_socket_params(const std::string& _name);
    static std::string get_socket_redirect_cycle(const std::map<std::string, std::string>& socket_redirects);
    std::string replace_all(std::string str, const std::string& from, const std::string& to);

    template <class T>
    void cci_set(std::string n, T value)
    {
        auto handle = m_broker.get_param_handle(n);
        if (handle.is_valid())
            handle.set_cci_value(cci::cci_value(value));
        else if (!m_broker.has_preset_value(n)) {
            m_broker.set_preset_cci_value(n, cci::cci_value(value));
        } else {
            SCP_FATAL(())("Trying to re-set a value? {}", n);
        }
    }

    // Socket redirection map: alias_name -> internal_path
    std::map<std::string, std::string> m_socket_redirects;
};
} // namespace gs

extern "C" void module_register();

#endif
