/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QBOX_MCD_DEBUG_H
#define _QBOX_MCD_DEBUG_H

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>

#include <mcd_client.h>

namespace mcd {

struct MemSpace {
    uint32_t id;
    std::string name;
    uint32_t mem_type; // MCD_MEM_SPACE_*, e.g. MCD_MEM_SPACE_IS_REGISTERS
};

struct CoreInfo {
    uint32_t core_id;
    uint32_t device_id;
};

struct CoreState {
    uint32_t core_id;
    bool running; // false => halted
};

struct RegInfo {
    uint32_t regnum;
    uint32_t group_id;
    uint32_t bitsize;
    uint32_t reg_type;     // mcd_reg_type_et: 0 simple, 1 compound, 2 partial
    uint32_t hw_thread_id; // hw thread the register belongs to, 0 if unassigned
    // The register's mcd_addr_st: its number in the register memory space, valid
    // in the hw thread named by addr_space_id.
    uint64_t address;
    uint32_t mem_space_id;
    uint32_t addr_space_id;
    uint32_t addr_space_type; // MCD_NOTUSED_ID or MCD_HW_THREAD_ID
    std::string name;
};

struct RegGroup {
    uint32_t group_id;
    std::string name;
    uint32_t n_registers;
};

// The target's register description: the complete group table, plus the
// registers selected by the group filter.
struct RegMap {
    std::vector<RegGroup> groups;
    std::vector<RegInfo> regs;
};

// Mirrors mcd_bp_type_et on the wire.
enum class BpType : uint32_t {
    SwBreak = 0,
    HwBreak = 1,
    WatchWrite = 2,
    WatchRead = 3,
    WatchAccess = 4,
};

struct BpInfo {
    uint32_t cpu;
    BpType type;
    uint64_t addr;
    uint32_t kind;
};

struct StopEvent {
    bool stopped;    // false => timed out with the target still running
    uint32_t reason; // mcd_stop_reason_et
    uint64_t watch_addr;
};

// RAII connection to one mcd_server instance. All operations throw
// std::runtime_error on failure.
class Connection
{
public:
    explicit Connection(const std::string& host, uint16_t port = 1235);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    const std::string& host() const { return m_host; }
    uint16_t port() const { return m_port; }

    // Cached; re-query with refresh().
    const std::vector<std::string>& systems() const { return m_systems; }
    const std::vector<std::string>& devices() const { return m_devices; }
    const std::vector<CoreInfo>& cores() const { return m_cores; }
    const std::vector<MemSpace>& mem_spaces() const { return m_mem_spaces; }
    void refresh();

    // Live queries, not cached.
    std::vector<BpInfo> list_breakpoints();
    std::vector<CoreState> core_states();

    // Reset the platform, then re-query the (re-enumerated) topology.
    void reset();

    mcd_client_t* handle() const { return m_client; }

private:
    mcd_client_t* m_client;
    std::string m_host;
    uint16_t m_port;
    std::vector<std::string> m_systems;
    std::vector<std::string> m_devices;
    std::vector<CoreInfo> m_cores;
    std::vector<MemSpace> m_mem_spaces;
    void populate();
};

// Handle to one logical debug core: a value holding a Connection reference and
// the core index, so it must not outlive the Connection.
class Core
{
public:
    Core(Connection& conn, uint32_t idx): m_conn(conn), m_idx(idx) {}

    uint32_t index() const { return m_idx; }
    const CoreInfo& info() const;

    void run();
    // Resume only this core, leaving the rest of its instance halted.
    void run_only();
    void stop();
    void step();

    // regno is the GDB register number.
    uint64_t read_reg(uint32_t regno);
    void write_reg(uint32_t regno, uint64_t val);

    // The target's description of its registers, in GDB register-number order.
    // group_id 0 returns every group's registers, non-zero only that group's.
    RegMap registers(uint32_t group_id = 0);

    // kind is the length in bytes (0 => server default).
    void set_breakpoint(uint64_t addr, BpType type = BpType::SwBreak, uint32_t kind = 0);
    void clear_breakpoint(uint64_t addr, BpType type = BpType::SwBreak, uint32_t kind = 0);

    StopEvent wait_stop(uint32_t timeout_ms);

    // space_id 0 = physical. addr_space_id 0 = "not used"; in the register space
    // (MCD_REG_SPACE_ID) it is the core's hw thread id and addr a register number.
    std::vector<uint8_t> read_mem(uint64_t addr, uint32_t len, uint32_t space_id = 0, uint32_t addr_space_id = 0);
    void write_mem(uint64_t addr, const std::vector<uint8_t>& data, uint32_t space_id = 0, uint32_t addr_space_id = 0);

    // Single T at addr, little-endian.
    template <typename T>
    T read_pod(uint64_t addr, uint32_t space_id = 0)
    {
        auto buf = read_mem(addr, sizeof(T), space_id);
        T v{};
        std::memcpy(&v, buf.data(), sizeof(T));
        return v;
    }

    template <typename T>
    void write_pod(uint64_t addr, T val, uint32_t space_id = 0)
    {
        std::vector<uint8_t> buf(sizeof(T));
        std::memcpy(buf.data(), &val, sizeof(T));
        write_mem(addr, buf, space_id);
    }

private:
    Connection& m_conn;
    uint32_t m_idx;
};

} // namespace mcd

#endif /* _QBOX_MCD_DEBUG_H */
