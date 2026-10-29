/*
 * This file is part of libqbox
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <vnc.h>

vnc::vnc(const sc_core::sc_module_name& name, sc_core::sc_object* gpu)
    : sc_module(name)
    , p_params("params", "", "VNC configuration parameters")
    , p_gpu_output("gpu_output", -1, "GPU output index for this VNC connection (required)")
    , m_gpu(dynamic_cast<QemuDevice*>(gpu))
{
    if (m_gpu == nullptr) {
        SCP_FATAL(())("VNC module: provided GPU object is not a QemuDevice.");
    }

    m_gpu_device_id = m_gpu->get_id();
    initialize();
}

void vnc::build_qemu_options()
{
    std::stringstream user_options_stream(p_params.get_value());
    std::vector<std::string> option_tokens;

    /* Filter out spoofed arguments */
    for (std::string option; std::getline(user_options_stream, option, ',');) {
        if (!option.empty() && !spoofed_opt(option)) {
            option_tokens.push_back(option);
        }
    }

    /* Add display index */
    m_qemu_opts.append(get_display_opt(',')).append(get_head_opt(','));

    /* Append non-spoofed options */
    for (const auto& option : option_tokens) {
        m_qemu_opts.append(option + ',');
    }

    // `Display device id` is the last in the options list
    // and doesn't need to end with a comma
    m_qemu_opts.append(get_display_device_opt());
}

bool vnc::spoofed_opt(const std::string_view& option)
{
    for (const std::string& spoofed_opt : m_spoofed_options) {
        if (option.find(spoofed_opt) == 0) {
            SCP_WARN(())
            ("Option '{}' provided by the user will be ignored.", spoofed_opt);
            return true;
        }
    }
    return false;
}

std::string vnc::get_head_opt(char append_suffix)
{
    std::string head_opt = "head=" + std::to_string(p_gpu_output.get_value());
    if (append_suffix != '\0') {
        head_opt += append_suffix;
    }
    return head_opt;
}

std::string vnc::get_display_opt(char append_suffix)
{
    std::string display_opt = ":" + std::to_string(p_gpu_output.get_value());
    if (append_suffix != '\0') {
        display_opt += append_suffix;
    }
    return display_opt;
}

std::string vnc::get_display_device_opt(char append_suffix)
{
    std::string display_device = "display=" + m_gpu_device_id;
    if (append_suffix != '\0') {
        display_device += append_suffix;
    }
    return display_device;
}

void vnc::initialize()
{
    if (p_gpu_output.get_value() < 0) {
        SCP_FATAL(())
        ("VNC module '{}': display_index parameter must be explicitly set (currently: {}). "
         "Each VNC connection must specify which display/head index to use.",
         name(), p_gpu_output.get_value());
    }

    build_qemu_options();

    SCP_INFO(())
    ("VNC Configuration on display: {}, device id: {} and user param: {}", p_gpu_output.get_value(), m_gpu_device_id,
     p_params.get_value());

    m_gpu->get_qemu_inst().set_vnc_args(m_qemu_opts);
}

void module_register() { GSC_MODULE_REGISTER_C(vnc, sc_core::sc_object*); }
