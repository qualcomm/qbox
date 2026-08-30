/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QBOX_MCD_SERVER_H
#define _QBOX_MCD_SERVER_H

/* Winsock must be included before anything can pull in windows.h, which would
 * otherwise bring the incompatible winsock.h in first. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
/* runonsysc.h uses gs::async_event without including it: pull in the
 * aggregate header, not runonsysc.h directly. */
#include <libgssync.h>

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace qbox {

#ifdef _WIN32
typedef SOCKET socket_t;
static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#define CLOSE_SOCKET closesocket
#else
typedef int socket_t;
static constexpr socket_t INVALID_SOCK = -1;
#define CLOSE_SOCKET close
#endif

/* MCD object model and opcodes, mirroring the public SPRINT MCD API
 * (mcd_api.h, BSD-3-Clause), so no MCD SDK dependency is needed. */
typedef struct mcd_server_st {
    uint32_t num_cores;
} mcd_server_st;

typedef struct mcd_system_st {
    char system_name[256];
} mcd_system_st;

typedef struct mcd_device_st {
    char device_name[256];
} mcd_device_st;

typedef struct mcd_core_st {
    uint32_t core_id;
    uint32_t device_id;
} mcd_core_st;

struct mcd_mem_space_st {
    uint32_t mem_space_id;
    char mem_space_name[64];
    uint32_t mem_type; /* MCD_MEM_SPACE_* */
};

typedef enum {
    MCD_RET_ACT_NONE = 0,
    MCD_RET_ERR_GENERAL = 1,
} mcd_return_et;

#define MCD_OP_QRY_SERVERS    0x01
#define MCD_OP_QRY_SYSTEMS    0x02
#define MCD_OP_QRY_DEVICES    0x03
#define MCD_OP_QRY_CORES      0x04
#define MCD_OP_QRY_MEM_SPACES 0x05
#define MCD_OP_RUN            0x10
#define MCD_OP_STOP           0x11
#define MCD_OP_STEP           0x12
#define MCD_OP_QRY_STATE      0x13
#define MCD_OP_RESET          0x14
#define MCD_OP_READ_MEM       0x20
#define MCD_OP_WRITE_MEM      0x21
#define MCD_OP_READ_REG       0x30
#define MCD_OP_WRITE_REG      0x31
#define MCD_OP_QRY_REGS       0x32
#define MCD_OP_SET_BP         0x40
#define MCD_OP_CLR_BP         0x41
#define MCD_OP_LIST_BP        0x42
#define MCD_OP_WAIT_STOP      0x43

/* Breakpoint/watchpoint types; values equal the GDB Z/z packet type digit. */
#define MCD_BP_SW_BREAK     0 /* software breakpoint (Z0) */
#define MCD_BP_HW_BREAK     1 /* hardware breakpoint (Z1) */
#define MCD_BP_WATCH_WRITE  2 /* write watchpoint (Z2) */
#define MCD_BP_WATCH_READ   3 /* read watchpoint (Z3) */
#define MCD_BP_WATCH_ACCESS 4 /* access watchpoint (Z4) */

/* Memory space kinds reported by MCD_OP_QRY_MEM_SPACES; values match
 * mcd_mem_type_et. */
#define MCD_MEM_SPACE_DEFAULT      0x00000000 /* none of the types below */
#define MCD_MEM_SPACE_IS_REGISTERS 0x00000001 /* the space contains only registers */

/* Interpretation of mcd_addr_st.addr_space_id; values match
 * mcd_addr_space_type_et, of which only these two are used. */
#define MCD_NOTUSED_ID   0 /* no address space id */
#define MCD_HW_THREAD_ID 4 /* the hw thread the address is valid in */

/* Id of the register memory space. MCD models a register that is not memory
 * mapped as memory in a space of type MCD_MEM_SPACE_IS_REGISTERS, addressed by
 * register number. Router spaces are numbered up from 0 as they are bound, so a
 * high sentinel cannot collide with one. */
#define MCD_REG_SPACE_ID 0xFFFF0000u

/* Register kinds reported by MCD_OP_QRY_REGS; values match mcd_reg_type_et. */
#define MCD_REG_TYPE_SIMPLE   0 /* a plain register */
#define MCD_REG_TYPE_COMPOUND 1 /* built from other types (gdb vector/union/struct) */
#define MCD_REG_TYPE_PARTIAL  2 /* a sub-field of another register */

/* Stop reasons reported by MCD_OP_WAIT_STOP. */
#define MCD_STOP_RUNNING      0 /* timed out; target still running */
#define MCD_STOP_HALTED       1 /* already halted (nothing was running) */
#define MCD_STOP_BREAK        2 /* SIGTRAP: breakpoint or single step */
#define MCD_STOP_WATCH_WRITE  3
#define MCD_STOP_WATCH_READ   4
#define MCD_STOP_WATCH_ACCESS 5
#define MCD_STOP_SIGNAL       6 /* some other signal */

/**
 * @brief Multi-Core Debug (MCD) TCP server component.
 *
 * Length-prefixed binary protocol over TCP; the accept loop is a std::thread,
 * not a SystemC thread. Memory access uses transport_dbg on a bound TLM socket;
 * CPU control goes to QEMU's GDB-RSP stub.
 */
class mcd_server : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    cci::cci_param<unsigned int> p_mcd_port;
    /* Interface to listen on: "127.0.0.1" (default), an address, or "*" for all.
     * This port is unauthenticated access to all memory and registers. */
    cci::cci_param<std::string> p_mcd_host;
    /* Block in start_of_simulation until a client connects, like QEMU's -S. */
    cci::cci_param<bool> p_wait_for_client;

    mcd_server(const sc_core::sc_module_name& name);
    ~mcd_server();

    /**
     * @brief Register a QEMU instance and the GDB-RSP endpoint reaching it.
     * @param gdb_hostport  GDB-RSP "host:port"; one endpoint serves every core
     */
    void bind_instance(sc_core::sc_object* inst, const std::string& gdb_hostport);

    /**
     * @brief Register a TLM initiator socket for debug memory access.
     * @param space_id  identifier the debugger uses to select this space
     */
    void bind_target(tlm::tlm_initiator_socket<>* socket, uint32_t space_id = 0, const std::string& name = "physical");

    void before_end_of_elaboration() override;
    void end_of_elaboration() override;
    void start_of_simulation() override;

private:
    mcd_return_et mcd_qry_servers(uint32_t* num_servers, mcd_server_st* servers);
    mcd_return_et mcd_qry_systems(uint32_t* num_systems, mcd_system_st* systems);
    /* @p cap is the output array capacity; *num is the number written. A null
     * array queries the count only. */
    mcd_return_et mcd_qry_devices(uint32_t* num_devices, mcd_device_st* devices, uint32_t cap);
    mcd_return_et mcd_qry_cores(uint32_t* num_cores, mcd_core_st* cores, uint32_t cap);

    void start_server();
    void server_thread();
    void handle_client(socket_t fd);

    mcd_return_et dispatch(uint8_t opcode, const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);

    mcd_return_et op_qry_servers(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_systems(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_devices(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_cores(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_mem_spaces(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_read_mem(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_write_mem(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_run(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_stop(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_step(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_state(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_reset(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_read_reg(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_write_reg(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_qry_regs(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_set_bp(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_clr_bp(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_list_bp(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);
    mcd_return_et op_wait_stop(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp);

    socket_t m_listen_fd;
    std::thread m_thread;
    std::atomic<bool> m_running;

    /* Connection being serviced, or INVALID_SOCK. Published so the destructor can
     * shutdown() it: the worker blocks in recv() here, which closing the listener
     * does not interrupt. */
    std::atomic<socket_t> m_client_fd;
    std::atomic<bool> m_thread_done;

    /* TLM (transport_dbg included) is not thread safe: every call into the model
     * runs on the SystemC kernel thread, never on the socket worker. */
    gs::runonsysc m_sc;

    /* Suspending channel attached only while the debugger is the reason the CPUs
     * are halted: a gdbstub halt removes the quantum keeper's suspending channel
     * (QemuCpu::wait_for_work -> m_qk->stop()), starving the kernel. The attach is
     * applied on the SystemC thread in update(), so it must precede the halt. */
    gs::async_event m_debug_hold;
    bool m_debug_hold_active; /* mirrors m_debug_hold; guarded by m_gdb_mutex */

    /* Both idempotent; caller must hold m_gdb_mutex. */
    void debug_hold(bool hold);
    void update_debug_hold();

    /* True once simulated time has advanced, i.e. a resume really took effect. */
    bool resume_observed();

    /* One debug endpoint per QEMU instance: run/stop are global
     * vm_stop()/vm_start(); only register access and stepping are per core. */
    std::vector<sc_core::sc_object*> m_insts;
    std::vector<std::string> m_gdb_ports; /* GDB-RSP host:port per instance */

    /* Persistent RSP session per instance, opened lazily; INVALID_SOCK = not
     * connected. The 0x03 interrupt and c/s run-control only work within one
     * long-lived connection. m_gdb_running = last told to continue. Guarded by
     * m_gdb_mutex. */
    std::vector<socket_t> m_gdb_fds;
    std::vector<bool> m_gdb_running;
    std::vector<bool> m_threads_known; /* cores enumerated for this session yet */
    std::mutex m_gdb_mutex;

    /* One entry per core, in MCD core-id order, from the gdbstub itself
     * (qfThreadInfo / qThreadExtraInfo). Guarded by m_gdb_mutex. */
    struct core_t {
        uint32_t inst; /* index into m_insts / m_gdb_fds */
        uint32_t tid;  /* gdb thread id (= QEMU cpu_index + 1) */
        std::string name;
    };
    std::vector<core_t> m_cores;

    /* Per-core run state, parallel to m_cores: QEMU's run control is instance
     * wide except for a vCont naming individual threads, so this is the only
     * record of a single core having been resumed. Guarded by m_gdb_mutex. */
    std::vector<bool> m_core_running;

    /* Both require m_gdb_mutex. */
    void set_inst_cores_running(uint32_t inst, bool running);
    bool inst_all_cores_running(uint32_t inst);

    /* Register description of one core, from the gdbstub's target XML. Cached per
     * instance, since every core of an instance has the same layout. Groups are
     * objects with ids, as mcd_register_group_st/mcd_register_info_st require:
     * a register carries its group's id, not a name. Id 0 is reserved by MCD, so
     * ids run from 1. hw_thread_id is not cached here: it is per core, this is per
     * instance. Guarded by m_gdb_mutex. */
    struct reg_t {
        uint32_t regnum;
        uint32_t group_id;
        uint32_t bitsize;
        uint32_t reg_type; /* MCD_REG_TYPE_* */
        std::string name;
    };
    struct reg_group_t {
        uint32_t group_id;
        std::string name;
        uint32_t n_registers;
    };
    struct inst_regs_t {
        std::vector<reg_group_t> groups;
        std::vector<reg_t> regs;
    };
    std::vector<inst_regs_t> m_inst_regs;

    /* Description of register @p regnum of instance @p inst, or null. Caller must
     * hold m_gdb_mutex and have called ensure_regs_known(). */
    const reg_t* find_reg(uint32_t inst, uint32_t regnum) const;

    /* One register of @p core over the RSP session. The value is the register in
     * target byte order, i.e. the bytes the stub hex-encodes. Caller must hold
     * m_gdb_mutex and keep the instance halted (scoped_halt): the stub parses no
     * packet while the VM runs. */
    bool gdb_read_reg(uint32_t core, uint32_t regno, std::vector<uint8_t>& value);
    bool gdb_write_reg(uint32_t core, uint32_t regno, const uint8_t* value, uint32_t len);

    /* One access to the register memory space (MCD_REG_SPACE_ID): @p address is a
     * register number, @p addr_space_id the gdb thread id of the core it is valid
     * in (0 => core 0), and @p length must cover whole registers. @p data holds the
     * values to write, or receives the values read, in the byte order READ_REG
     * reports. Takes m_gdb_mutex itself. */
    mcd_return_et access_reg_space(bool write, uint64_t address, uint32_t length, uint32_t addr_space_id,
                                   std::vector<uint8_t>& data);
    /* Core index for gdb thread id @p addr_space_id, 0 meaning core 0. */
    bool reg_space_core(uint32_t addr_space_id, uint32_t& core);
    /* Split @p length bytes from register number @p regno into whole registers,
     * using the widths of the target description. Fails unless they match exactly. */
    struct reg_span_t {
        uint32_t regno;
        uint32_t bytes;
    };
    bool reg_space_split(uint32_t inst, uint32_t regno, uint32_t length, std::vector<reg_span_t>& out);

    /* QEMU's gdbstub parses no packet while the VM runs: any byte other than the
     * 0x03 interrupt makes gdb_read_byte() vm_stop() and answer nothing at all. A
     * request that has to reach a running instance therefore halts it first.
     * gdb_halt() returns true if this call did the halt, i.e. gdb_resume() is owed.
     * Both need m_gdb_mutex. */
    bool gdb_halt(uint32_t idx);
    void gdb_resume(uint32_t idx, const std::vector<bool>& was_running);

    /* RAII form of the above, for requests with several exit paths: halts on entry
     * if needed, restores the running set on exit. MCD does not restrict register
     * or memory access to halted cores (mcd_execute_txlist_f has no such
     * precondition), so the halt is transparent to the client. exclude() drops a
     * core from the set to restore, for STEP, which leaves its core halted. */
    class scoped_halt
    {
    public:
        scoped_halt(mcd_server& server, uint32_t idx);
        ~scoped_halt();
        void exclude(uint32_t core);

    private:
        mcd_server& m_server;
        uint32_t m_idx;
        bool m_halted;
        std::vector<bool> m_was_running;
    };

    /* Fetch and parse the target XML for instance @p idx, once. Caller must hold
     * m_gdb_mutex, as for gdb_qxfer() and gdb_monitor(). */
    void ensure_regs_known(uint32_t idx);
    /* Read an "qXfer:features:read:<annex>" object in 0x400-byte chunks. Returns
     * the concatenated payload, or "" if the stub refuses or the transport fails. */
    std::string gdb_qxfer(uint32_t idx, const std::string& annex);
    /* Run @p command through the stub's HMP monitor as "qRcmd,<hex>". */
    bool gdb_monitor(uint32_t idx, const std::string& command);

    /* Last stop reported by an instance, cached until the core it belongs to asks:
     * a stop arrives once per session, but WAIT_STOP is per core and the stopping
     * core need not be the one waited on. Guarded by m_gdb_mutex. */
    struct last_stop_t {
        bool valid = false;
        uint32_t reason = 0;
        uint64_t watch_addr = 0;
        uint32_t tid = 0; /* gdb thread id that stopped, 0 if unreported */
    };
    std::vector<last_stop_t> m_last_stop;

    void note_stop(uint32_t idx, uint32_t reason, uint64_t watch_addr, uint32_t tid);

    /* note_stop and these three require m_gdb_mutex. ensure_cores_known() opens a
     * session per instance; gdb_select_core() sends "Hg" for register access. */
    void gdb_enumerate_cores(uint32_t idx);
    void ensure_cores_known();
    bool gdb_select_core(uint32_t core);

    /* Breakpoints installed via MCD_OP_SET_BP. Guarded by m_gdb_mutex. `core` is
     * only what the debugger asked for: QEMU installs Z/z breakpoints per address
     * space, so a breakpoint fires on any core of that instance. */
    struct breakpoint_t {
        uint32_t core; /* MCD core id the request came in on */
        uint32_t type; /* MCD_BP_* */
        uint64_t addr;
        uint32_t kind; /* length in bytes (Z-packet "kind") */
    };
    std::vector<breakpoint_t> m_breakpoints;

    /* Wait up to @p timeout_ms for a stop-reply. Returns an MCD_STOP_* reason,
     * the watchpoint address, and the reply's gdb thread id (0 if absent).
     * Caller holds m_gdb_mutex, as for drain_pending_stop() and gdb_cmd(). */
    uint32_t gdb_wait_stop(uint32_t idx, uint32_t timeout_ms, uint64_t& watch_addr, uint32_t& stopped_tid);

    /* Non-blocking consume of an unsolicited stop-reply, so m_gdb_running tracks
     * what the instance is doing. */
    void drain_pending_stop(uint32_t idx);

    /* Connected, handshaken RSP session fd for instance @p idx, or INVALID_SOCK. */
    socket_t gdb_session(uint32_t idx);
    void gdb_close_all();
    /* Invalidates the session on transport failure. */
    bool gdb_cmd(uint32_t idx, const std::string& cmd, std::string& reply, bool wait_reply = true);

    /* m_mem_spaces keeps registration order for query; m_transactors maps space_id
     * to a callable issuing a transport_dbg and returning the bytes handled. The
     * register space has no transactor: it is served over the RSP session. */
    std::vector<mcd_mem_space_st> m_mem_spaces;
    std::map<uint32_t, std::function<unsigned int(tlm::tlm_generic_payload&)>> m_transactors;

    /* A null @p fn registers a space with no transactor. */
    void add_mem_space(uint32_t space_id, const std::string& name, uint32_t mem_type,
                       std::function<unsigned int(tlm::tlm_generic_payload&)> fn);
};

} // namespace qbox

#endif // _QBOX_MCD_SERVER_H
