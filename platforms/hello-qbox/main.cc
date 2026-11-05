/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <chrono>
#include <string>

#include <cci_configuration>

#include <cciutils.h>
#include <argparser.h>
#include <module_factory_container.h>

#if SC_VERSION_MAJOR < 3
#warning PLEASE UPDATE TO SYSTEMC 3.0, OLDER VERSIONS ARE DEPRECATED AND MAY NOT WORK
#endif

class GreenSocsPlatform : public gs::ModuleFactory::Container
{
protected:
    cci::cci_param<int> m_quantum_ns;
    cci::cci_param<int> m_gdb_port;

public:
    GreenSocsPlatform(const sc_core::sc_module_name& n)
        : gs::ModuleFactory::Container(n)
        , m_quantum_ns("quantum_ns", 1000000, "TLM-2.0 global quantum in ns")
        , m_gdb_port("gdb_port", 0, "GDB port")
    {
        using tlm_utils::tlm_quantumkeeper;

        sc_core::sc_time global_quantum(m_quantum_ns, sc_core::SC_NS);
        tlm_quantumkeeper::set_global_quantum(global_quantum);
    };
};

int sc_main(int argc, char* argv[])
{
    if (sc_core::sc_version_major < 3) {
        SCP_WARN()
        ("\n********************************\nWARNING, USING A DEPRECATED VERSION OF SYSTEMC, PLEASE "
         "UPGRADE\n********************************");
    }

    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::DBGTRACE)
                                        .msgTypeFieldWidth(30));

    gs::ConfigurableBroker m_broker{};
    cci::cci_originator orig("sc_main");
    cci::cci_param<int> p_log_level{ "log_level", 0, "Default log level", cci::CCI_ABSOLUTE_NAME, orig };
    auto broker_h = m_broker.create_broker_handle(orig);
    ArgParser ap{ broker_h, argc, argv };

    GreenSocsPlatform platform("platform");

    auto start = std::chrono::system_clock::now();
    try {
        SCP_INFO() << "SC_START";
        sc_core::sc_start();
    } catch (std::runtime_error const& e) {
        std::cerr << argv[0] << "Error: '" << e.what() << std::endl;
        exit(1);
    } catch (const std::exception& exc) {
        std::cerr << argv[0] << " Error: '" << exc.what() << std::endl;
        exit(2);
    } catch (...) {
        SCP_ERR() << "Unknown error (main.cc)!";
        exit(3);
    }

    auto end = std::chrono::system_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::cout << "Simulation Time: " << sc_core::sc_time_stamp().to_seconds() << "SC_SEC" << std::endl;
    std::cout << "Simulation Duration: " << elapsed.count() << "s (Wall Clock)" << std::endl;

    return 0;
}
