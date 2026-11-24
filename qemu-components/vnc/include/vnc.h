/*
 *  This file is part of libqbox
 *  Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_VNC_H
#define _LIBQBOX_COMPONENTS_VNC_H

#include <systemc>
#include <scp/report.h>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>

#include <cci_configuration>
#include <module_factory_registery.h>

#include <qemu-instance.h>
#include <device.h>

/**
 * @class vnc
 *
 * @brief VNC module as a SystemC module
 *
 * @details This class manages a VNC connection as an independent SystemC module.
 *
 * It performs the following tasks:
 *    * Configures the display number/head index
 *    * Passes optional VNC configuration items to libqemu
 *    * Initializes the VNC server with the GPU device
 */
class vnc : public sc_core::sc_module
{
    SCP_LOGGER(());

public:
    cci::cci_param<std::string> p_params;
    cci::cci_param<bool> p_egl_headless;
    cci::cci_param<int> p_gpu_output;

protected:
    std::string m_qemu_opts;
    std::string m_gpu_device_id;
    std::vector<std::string> m_spoofed_options = { ":", "head=", "display=" };
    QemuDevice* m_gpu;

public:
    /**
     * @brief Construct a new VNC module
     *
     * @param[in] name SystemC module name
     * @param[in] gpu GPU device associated with this VNC connection
     */
    vnc(const sc_core::sc_module_name& name, sc_core::sc_object* gpu);

private:
    /**
     * @brief Initialize VNC connection with QEMU instance
     */
    void initialize();

    /**
     * @brief Build QEMU VNC options from `params` cci parameter
     */
    void build_qemu_options();

    /**
     * @brief Check if an option is spoofed (managed internally)
     *
     * @param[in] option The option to check
     * @return true if the option is spoofed, false otherwise
     */
    bool spoofed_opt(const std::string_view& option);

    /**
     * @brief Get the head option string
     *
     * @param[in] append_suffix Character to append to the option
     * @return The head option string
     */
    std::string get_head_opt(char append_suffix = '\0');

    /**
     * @brief Get the display option string
     *
     * @param[in] append_suffix Character to append to the option
     * @return The display option string
     */
    std::string get_display_opt(char append_suffix = '\0');

    /**
     * @brief Get the display device option string
     *
     * @param[in] append_suffix Character to append to the option
     * @return The display device option string
     */
    std::string get_display_device_opt(char append_suffix = '\0');
};

extern "C" void module_register();

#endif // _LIBQBOX_COMPONENTS_VNC_H
