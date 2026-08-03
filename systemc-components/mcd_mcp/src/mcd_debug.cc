/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <mcd_debug.h>

#include <array>

namespace mcd {

namespace {
constexpr uint32_t k_max_records = 256;
/* A gdbstub target description runs to hundreds of registers, well past
 * k_max_records, and each record is too large for a stack array of that many. */
constexpr uint32_t k_max_regs = 4096;
} // namespace

Connection::Connection(const std::string& host, uint16_t port): m_client(nullptr), m_host(host), m_port(port)
{
    m_client = mcd_client_connect(host.c_str(), port);
    if (!m_client) {
        throw std::runtime_error("mcd: failed to connect to " + host + ":" + std::to_string(port));
    }
    populate();
}

Connection::~Connection()
{
    if (m_client) {
        mcd_client_disconnect(m_client);
    }
}

void Connection::refresh() { populate(); }

/* Each block passes a fixed-capacity array; mcd_client_qry_* returns a count
 * bounded by that capacity, and the `i < tmp.size()` guard keeps the bound
 * visible at the point of use. */
void Connection::populate()
{
    m_systems.clear();
    m_devices.clear();
    m_cores.clear();
    m_mem_spaces.clear();

    {
        std::array<mcd_system_st, k_max_records> tmp{};
        uint32_t num = tmp.size();
        if (mcd_client_qry_systems(m_client, &num, tmp.data()) != 0) {
            throw std::runtime_error("mcd: qry_systems failed");
        }
        for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
            m_systems.emplace_back(tmp[i].system_name);
        }
    }

    {
        std::array<mcd_device_st, k_max_records> tmp{};
        uint32_t num = tmp.size();
        if (mcd_client_qry_devices(m_client, &num, tmp.data()) != 0) {
            throw std::runtime_error("mcd: qry_devices failed");
        }
        for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
            m_devices.emplace_back(tmp[i].device_name);
        }
    }

    {
        std::array<mcd_core_st, k_max_records> tmp{};
        uint32_t num = tmp.size();
        if (mcd_client_qry_cores(m_client, &num, tmp.data()) != 0) {
            throw std::runtime_error("mcd: qry_cores failed");
        }
        for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
            m_cores.push_back(CoreInfo{ tmp[i].core_id, tmp[i].device_id });
        }
    }

    {
        std::array<mcd_mem_space_st, k_max_records> tmp{};
        uint32_t num = tmp.size();
        if (mcd_client_qry_mem_spaces(m_client, &num, tmp.data()) != 0) {
            throw std::runtime_error("mcd: qry_mem_spaces failed");
        }
        for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
            m_mem_spaces.push_back(MemSpace{ tmp[i].mem_space_id, tmp[i].mem_space_name, tmp[i].mem_type });
        }
    }
}

std::vector<BpInfo> Connection::list_breakpoints()
{
    std::array<mcd_bp_st, k_max_records> tmp{};
    uint32_t num = tmp.size();
    if (mcd_client_list_bp(m_client, &num, tmp.data()) != 0) {
        throw std::runtime_error("mcd: list_bp failed");
    }
    std::vector<BpInfo> out;
    for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
        out.push_back(BpInfo{ tmp[i].cpu, static_cast<BpType>(tmp[i].type), tmp[i].addr, tmp[i].kind });
    }
    return out;
}

std::vector<CoreState> Connection::core_states()
{
    std::array<mcd_core_state_st, k_max_records> tmp{};
    uint32_t num = tmp.size();
    if (mcd_client_qry_state(m_client, &num, tmp.data()) != 0) {
        throw std::runtime_error("mcd: qry_state failed");
    }
    std::vector<CoreState> out;
    for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
        out.push_back(CoreState{ tmp[i].core_id, tmp[i].running != 0 });
    }
    return out;
}

void Connection::reset()
{
    if (mcd_client_reset(m_client) != 0) {
        throw std::runtime_error("mcd: reset failed");
    }
    /* The server dropped its core list, so the cached topology is stale. */
    populate();
}

const CoreInfo& Core::info() const
{
    const auto& cores = m_conn.cores();
    if (m_idx >= cores.size()) {
        throw std::runtime_error("mcd: core index " + std::to_string(m_idx) + " out of range");
    }
    return cores[m_idx];
}

void Core::run()
{
    if (mcd_client_run(m_conn.handle()) != 0) {
        throw std::runtime_error("mcd: run failed");
    }
}

void Core::run_only()
{
    if (mcd_client_run_core(m_conn.handle(), m_idx) != 0) {
        throw std::runtime_error("mcd: run of core " + std::to_string(m_idx) + " failed");
    }
}

void Core::stop()
{
    if (mcd_client_stop(m_conn.handle()) != 0) {
        throw std::runtime_error("mcd: stop failed");
    }
}

void Core::step()
{
    if (mcd_client_step(m_conn.handle(), m_idx) != 0) {
        throw std::runtime_error("mcd: step failed");
    }
}

uint64_t Core::read_reg(uint32_t regno)
{
    uint64_t val = 0;
    if (mcd_client_read_reg(m_conn.handle(), m_idx, regno, &val) != 0) {
        throw std::runtime_error("mcd: read_reg " + std::to_string(regno) + " failed");
    }
    return val;
}

void Core::write_reg(uint32_t regno, uint64_t val)
{
    if (mcd_client_write_reg(m_conn.handle(), m_idx, regno, val) != 0) {
        throw std::runtime_error("mcd: write_reg " + std::to_string(regno) + " failed");
    }
}

RegMap Core::registers(uint32_t group_id)
{
    std::vector<mcd_reg_group_st> gtmp(k_max_records);
    std::vector<mcd_reg_info_st> tmp(k_max_regs);
    uint32_t n_groups = static_cast<uint32_t>(gtmp.size());
    uint32_t num = static_cast<uint32_t>(tmp.size());
    if (mcd_client_qry_regs(m_conn.handle(), m_idx, group_id, &n_groups, gtmp.data(), &num, tmp.data()) != 0) {
        throw std::runtime_error("mcd: qry_regs failed");
    }
    RegMap out;
    for (uint32_t i = 0; i < n_groups && i < gtmp.size(); ++i) {
        out.groups.push_back(RegGroup{ gtmp[i].group_id, gtmp[i].name, gtmp[i].n_registers });
    }
    for (uint32_t i = 0; i < num && i < tmp.size(); ++i) {
        out.regs.push_back(RegInfo{ tmp[i].regnum, tmp[i].group_id, tmp[i].bitsize, tmp[i].reg_type,
                                    tmp[i].hw_thread_id, tmp[i].address, tmp[i].mem_space_id, tmp[i].addr_space_id,
                                    tmp[i].addr_space_type, tmp[i].name });
    }
    return out;
}

void Core::set_breakpoint(uint64_t addr, BpType type, uint32_t kind)
{
    if (mcd_client_set_bp(m_conn.handle(), m_idx, static_cast<uint32_t>(type), addr, kind) != 0) {
        throw std::runtime_error("mcd: set_breakpoint failed");
    }
}

void Core::clear_breakpoint(uint64_t addr, BpType type, uint32_t kind)
{
    if (mcd_client_clr_bp(m_conn.handle(), m_idx, static_cast<uint32_t>(type), addr, kind) != 0) {
        throw std::runtime_error("mcd: clear_breakpoint failed");
    }
}

StopEvent Core::wait_stop(uint32_t timeout_ms)
{
    mcd_stop_st st{};
    if (mcd_client_wait_stop(m_conn.handle(), m_idx, timeout_ms, &st) != 0) {
        throw std::runtime_error("mcd: wait_stop failed");
    }
    return StopEvent{ st.stopped != 0, st.reason, st.watch_addr };
}

std::vector<uint8_t> Core::read_mem(uint64_t addr, uint32_t len, uint32_t space_id, uint32_t addr_space_id)
{
    std::vector<uint8_t> buf(len);
    if (len && mcd_client_read_mem(m_conn.handle(), addr, len, space_id, addr_space_id, buf.data()) != 0) {
        throw std::runtime_error("mcd: read_mem failed");
    }
    return buf;
}

void Core::write_mem(uint64_t addr, const std::vector<uint8_t>& data, uint32_t space_id, uint32_t addr_space_id)
{
    if (!data.empty() && mcd_client_write_mem(m_conn.handle(), addr, static_cast<uint32_t>(data.size()), space_id,
                                              addr_space_id, data.data()) != 0) {
        throw std::runtime_error("mcd: write_mem failed");
    }
}

} // namespace mcd
