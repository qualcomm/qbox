/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mcd_server.h"
#include <module_factory_registery.h>
#include <router_if.h>
#include <cciutils.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
#include <chrono>
#include <algorithm>

/* Socket headers (and the socket_t/CLOSE_SOCKET portability block) come from
 * mcd_server.h, which must be included before anything pulls in windows.h. */

namespace qbox {

/* winsock reports through WSAGetLastError(), not errno. */
#ifdef _WIN32
#define SOCK_ERR_IS_INTR() (WSAGetLastError() == WSAEINTR)
#else
#define SOCK_ERR_IS_INTR() (errno == EINTR)
#endif

/* winsock ignores select()'s nfds argument. */
#ifdef _WIN32
#define SOCK_NFDS(fd) 0
#else
#define SOCK_NFDS(fd) (static_cast<int>(fd) + 1)
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif

static std::string sock_err()
{
#ifdef _WIN32
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

/* SO_RCVTIMEO takes a DWORD of milliseconds on winsock, a struct timeval on POSIX. */
static void set_recv_timeout(socket_t fd, uint32_t ms)
{
#ifdef _WIN32
    DWORD tv = ms;
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
#endif
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
}

/* Frame and single-access size cap. The protocol is length-prefixed, so an
 * unchecked length lets an unauthenticated peer allocate that much inside the
 * simulator; 1 MiB exceeds any real debugger operation. */
static const uint32_t MCD_MAX_FRAME = 1u * 1024u * 1024u;

/* Cap on the client-supplied WAIT_STOP timeout: the wait holds m_gdb_mutex, so
 * an unbounded value blocks every other debug operation. */
static const uint32_t MCD_MAX_WAIT_MS = 60u * 1000u;

/* SIGPIPE would kill the simulation when a debugger goes away mid-reply. Linux
 * has the per-call flag, macOS/BSD only the per-socket option below. */
#ifdef MSG_NOSIGNAL
#define MCD_SEND_FLAGS MSG_NOSIGNAL
#else
#define MCD_SEND_FLAGS 0
#endif

static void set_nosigpipe(socket_t fd)
{
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    /* Windows has neither the option nor SIGPIPE. */
    (void)fd;
#endif
}

static void put_u32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xff));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xff));
}

static uint32_t get_u32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint64_t get_u64(const uint8_t* p)
{
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) {
        x |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return x;
}

/* winsock's recv/send take a char* and an int length, so the buffer and count are
 * cast; every transfer here is bounded by MCD_MAX_FRAME. */
static bool recv_all(socket_t fd, void* buf, size_t n)
{
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < n) {
        int r = static_cast<int>(::recv(fd, reinterpret_cast<char*>(p + got), static_cast<int>(n - got), 0));
        if (r == 0) return false; /* peer closed */
        if (r < 0) {
            if (SOCK_ERR_IS_INTR()) continue;
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

static bool send_all(socket_t fd, const void* buf, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t sent = 0;
    while (sent < n) {
        int r = static_cast<int>(
            ::send(fd, reinterpret_cast<const char*>(p + sent), static_cast<int>(n - sent), MCD_SEND_FLAGS));
        if (r < 0) {
            if (SOCK_ERR_IS_INTR()) continue;
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

/* GDB remote-serial-protocol (RSP) client helpers: CPU control and register
 * access are forwarded to QEMU's gdbstub as framed packets. */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* RSP checksum: sum of all data bytes, modulo 256. */
static uint8_t gdb_checksum(const std::string& data)
{
    unsigned sum = 0;
    for (unsigned char c : data) sum += c;
    return static_cast<uint8_t>(sum & 0xff);
}

/* Split "host:port" (host may be empty, an IPv4 literal, or a name). */
static bool gdb_parse_hostport(const std::string& hostport, std::string& host, std::string& port)
{
    std::string::size_type colon = hostport.rfind(':');
    if (colon == std::string::npos) return false;
    host = hostport.substr(0, colon);
    port = hostport.substr(colon + 1);
    if (host.empty()) host = "127.0.0.1";
    return !port.empty();
}

/* Open a TCP connection to the gdbstub, with a receive timeout so a background
 * worker never blocks forever. Returns the fd or INVALID_SOCK. */
static socket_t gdb_open_once(const std::string& hostport)
{
    std::string host, port;
    if (!gdb_parse_hostport(hostport, host, port)) return INVALID_SOCK;

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || res == nullptr) {
        return INVALID_SOCK;
    }

    socket_t fd = INVALID_SOCK;
    for (struct addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == INVALID_SOCK) continue;
        if (::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
        CLOSE_SOCKET(fd);
        fd = INVALID_SOCK;
    }
    ::freeaddrinfo(res);
    if (fd == INVALID_SOCK) return INVALID_SOCK;

    set_recv_timeout(fd, 5000);
    set_nosigpipe(fd);
    return fd;
}

static socket_t gdb_open(const std::string& hostport)
{
    /* Retry ~1 s: the stub starts in start_of_simulation, after this module
     * publishes its port, and callback order between modules is unspecified.
     * Short enough that an absent stub is an error, not a hang. */
    for (int attempt = 0; attempt < 20; ++attempt) {
        socket_t fd = gdb_open_once(hostport);
        if (fd != INVALID_SOCK) return fd;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return INVALID_SOCK;
}

/* Frame @p data as "$<data>#<cksum>", send it, read the '+' ack. An async
 * stop-reply can land between send and ack; skip it, or its '$' is taken for the
 * ack and the session desynchronises. @p stray gets the last such packet. */
static bool gdb_write_packet(socket_t fd, const std::string& data, std::string* stray = nullptr)
{
    char csbuf[3];
    std::snprintf(csbuf, sizeof(csbuf), "%02x", gdb_checksum(data));

    std::string pkt;
    pkt.reserve(data.size() + 4);
    pkt.push_back('$');
    pkt += data;
    pkt.push_back('#');
    pkt += csbuf;

    if (!send_all(fd, pkt.data(), pkt.size())) return false;

    /* Bound the loop so a chatty or broken peer cannot hold us here forever. */
    for (int skipped = 0; skipped < 16; ++skipped) {
        char ack = 0;
        if (!recv_all(fd, &ack, 1)) return false;

        if (ack == '+') return true;
        if (ack == '-') return false; /* retransmit requested; we do not retransmit */

        if (ack == '$') {
            /* Unsolicited packet: consume body, '#' and 2 checksum digits, ack
             * it, keep waiting for ours. */
            std::string body;
            char c = 0;
            bool ok = true;
            while (ok) {
                if (!recv_all(fd, &c, 1)) return false;
                if (c == '#') break;
                body.push_back(c);
            }
            char cs[2];
            if (!recv_all(fd, cs, 2)) return false;
            const char plus = '+';
            (void)send_all(fd, &plus, 1);
            if (stray) *stray = body;
            continue;
        }
        /* Anything else is noise (e.g. a stray interrupt byte); ignore it. */
    }
    return false;
}

/* Read a "$<data>#<cksum>" packet, validate the checksum, strip framing into
 * @p out, and ack. Replies used here are printable ASCII, so RSP binary
 * escaping never occurs and is not decoded. */
static bool gdb_read_packet(socket_t fd, std::string& out)
{
    out.clear();

    char c = 0;
    do {
        if (!recv_all(fd, &c, 1)) return false;
    } while (c != '$');

    uint8_t sum = 0;
    while (true) {
        if (!recv_all(fd, &c, 1)) return false;
        if (c == '#') break;
        out.push_back(c);
        sum += static_cast<uint8_t>(c);
    }

    char cs[2];
    if (!recv_all(fd, cs, 2)) return false;
    int hi = hex_nibble(cs[0]);
    int lo = hex_nibble(cs[1]);
    if (hi < 0 || lo < 0) return false;

    char ack = '+';
    (void)send_all(fd, &ack, 1);

    return static_cast<uint8_t>((hi << 4) | lo) == sum;
}

/* Send @p cmd on an open session and, when @p wait_reply, read the reply.
 * Continue ('c') passes wait_reply = false: its reply only arrives on stop. */
static bool gdb_txn(socket_t fd, const std::string& cmd, std::string& reply, bool wait_reply = true,
                    std::string* stray = nullptr)
{
    reply.clear();
    if (fd == INVALID_SOCK) return false;
    bool ok = gdb_write_packet(fd, cmd, stray);
    if (ok && wait_reply) {
        ok = gdb_read_packet(fd, reply);
    }
    return ok;
}

/* Map an MCD_BP_* type onto the GDB Z/z packet type digit. */
static bool bp_type_to_gdb(uint32_t type, char& digit)
{
    switch (type) {
    case MCD_BP_SW_BREAK:
        digit = '0';
        return true;
    case MCD_BP_HW_BREAK:
        digit = '1';
        return true;
    case MCD_BP_WATCH_WRITE:
        digit = '2';
        return true;
    case MCD_BP_WATCH_READ:
        digit = '3';
        return true;
    case MCD_BP_WATCH_ACCESS:
        digit = '4';
        return true;
    default:
        return false;
    }
}

/* Extract the hex value following @p key ("watch:", "rwatch:", "awatch:") in a
 * 'T' stop-reply packet. Returns true and sets @p out when present. */
static bool gdb_stop_field(const std::string& pkt, const char* key, uint64_t& out)
{
    std::string::size_type pos = pkt.find(key);
    if (pos == std::string::npos) return false;
    pos += std::strlen(key);
    std::string hexv;
    while (pos < pkt.size() && pkt[pos] != ';') hexv.push_back(pkt[pos++]);
    if (hexv.empty()) return false;
    out = std::strtoull(hexv.c_str(), nullptr, 16);
    return true;
}

/* Classify a GDB stop-reply packet ('T aa ...' or 'S aa'). Returns an
 * MCD_STOP_* reason and, for watchpoints, the triggering address. */
static uint32_t gdb_classify_stop(const std::string& pkt, uint64_t& watch_addr)
{
    watch_addr = 0;
    if (pkt.empty()) return MCD_STOP_HALTED;

    /* Signal number is the two hex digits after the leading 'T'/'S'. */
    int sig = 0;
    if (pkt.size() >= 3) {
        int hi = hex_nibble(pkt[1]);
        int lo = hex_nibble(pkt[2]);
        if (hi >= 0 && lo >= 0) sig = (hi << 4) | lo;
    }

    if (pkt[0] == 'T') {
        /* "watch:" is a substring of "awatch:"/"rwatch:", so test it last. */
        if (gdb_stop_field(pkt, "awatch:", watch_addr)) return MCD_STOP_WATCH_ACCESS;
        if (gdb_stop_field(pkt, "rwatch:", watch_addr)) return MCD_STOP_WATCH_READ;
        if (gdb_stop_field(pkt, "watch:", watch_addr)) return MCD_STOP_WATCH_WRITE;
    }

    return (sig == 5) ? MCD_STOP_BREAK : MCD_STOP_SIGNAL; /* 5 = SIGTRAP */
}

mcd_server::mcd_server(const sc_core::sc_module_name& name)
    : sc_core::sc_module(name)
    , p_mcd_port("mcd_port", 1235, "MCD server TCP port")
    , m_listen_fd(INVALID_SOCK)
    , m_running(false)
    , m_client_fd(INVALID_SOCK)
    , m_thread_done(false)
    , m_debug_hold(false) /* start detached: no debugger, no behaviour change */
    , m_debug_hold_active(false)
{
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        SCP_FATAL(()) << "mcd_server: WSAStartup failed";
    }
#endif
    SCP_DEBUG(()) << "mcd_server constructor";
}

mcd_server::~mcd_server()
{
    m_running = false;
    if (m_listen_fd != INVALID_SOCK) {
        ::shutdown(m_listen_fd, SHUT_RDWR);
        CLOSE_SOCKET(m_listen_fd);
        m_listen_fd = INVALID_SOCK;
    }

    /* A worker servicing a client is blocked in recv() on the accepted socket, so
     * join() alone would hang: shut that socket down too. The retry loop covers
     * the window between accept() returning and the fd being published. */
    if (m_thread.joinable()) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            socket_t client = m_client_fd;
            if (client != INVALID_SOCK) ::shutdown(client, SHUT_RDWR);
            if (m_thread_done) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        m_thread.join();
    }
    gdb_close_all();
#ifdef _WIN32
    WSACleanup();
#endif
}

void mcd_server::bind_instance(sc_core::sc_object* inst, const std::string& gdb_hostport)
{
    m_insts.push_back(inst);
    m_gdb_ports.push_back(gdb_hostport);
    m_gdb_fds.push_back(INVALID_SOCK);
    m_gdb_running.push_back(false);
    m_threads_known.push_back(false);
    m_last_stop.push_back(last_stop_t{});
    SCP_INFO(()) << "bind_instance[" << (m_insts.size() - 1) << "]: " << (inst ? inst->name() : "<null>")
                 << " gdb-rsp=" << gdb_hostport;
}

/* Lazily open and cache a persistent RSP session. The qSupported exchange leaves
 * the stub connected, as op_stop's 0x03 interrupt requires. "multiprocess+" is
 * not advertised, so thread ids stay plain hex, not p<pid>.<tid>. Needs
 * m_gdb_mutex. */
socket_t mcd_server::gdb_session(uint32_t idx)
{
    if (idx >= m_gdb_ports.size()) return INVALID_SOCK;
    if (m_gdb_fds[idx] != INVALID_SOCK) return m_gdb_fds[idx];

    /* Connecting pauses the whole instance, so this call is itself a halt: the
     * hold must be taken first, since the attach is deferred to the SystemC
     * thread and the kernel could starve in the interim. */
    debug_hold(true);

    socket_t fd = gdb_open(m_gdb_ports[idx]);
    if (fd == INVALID_SOCK) {
        SCP_WARN(()) << "mcd_server: could not open GDB-RSP session to " << m_gdb_ports[idx] << ": " << sock_err();
        return INVALID_SOCK;
    }

    std::string reply;
    if (!gdb_txn(fd, "qSupported", reply, /*wait_reply=*/true)) {
        SCP_WARN(()) << "mcd_server: GDB-RSP handshake failed on " << m_gdb_ports[idx];
        CLOSE_SOCKET(fd);
        return INVALID_SOCK;
    }

    m_gdb_fds[idx] = fd;
    m_gdb_running[idx] = false;
    SCP_INFO(()) << "mcd_server: opened GDB-RSP session to " << m_gdb_ports[idx];

    gdb_enumerate_cores(idx);
    return fd;
}

/* Discover the cores of instance @p idx: qfThreadInfo/qsThreadInfo list the gdb
 * thread ids (QEMU uses cpu_index + 1), qThreadExtraInfo names each one
 * hex-encoded ("CPU#0 [running]"). Caller must hold m_gdb_mutex. */
void mcd_server::gdb_enumerate_cores(uint32_t idx)
{
    if (idx >= m_threads_known.size() || m_threads_known[idx]) return;

    socket_t fd = m_gdb_fds[idx];
    if (fd == INVALID_SOCK) return;

    std::vector<uint32_t> tids;
    std::string reply;
    std::string cmd = "qfThreadInfo";
    /* The list arrives as one or more "m<id>[,<id>...]" replies terminated by
     * "l". The loop is bounded: a stub that never says "l" must not wedge us. */
    for (int round = 0; round < 256; ++round) {
        if (!gdb_txn(fd, cmd, reply, /*wait_reply=*/true)) break;
        if (reply.empty() || reply[0] == 'l') break;
        if (reply[0] != 'm') break;

        /* Comma-separated hex ids follow the leading 'm'. */
        std::string::size_type pos = 1;
        while (pos < reply.size()) {
            std::string::size_type comma = reply.find(',', pos);
            std::string tok = reply.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!tok.empty()) {
                tids.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        cmd = "qsThreadInfo";
    }

    if (tids.empty()) {
        /* No thread list: fall back to the single implicit thread, which is what
         * a bare 'p'/'c' addresses. */
        SCP_WARN(()) << "mcd_server: " << m_gdb_ports[idx] << " returned no thread list; assuming a single core";
        tids.push_back(1);
    }

    for (uint32_t tid : tids) {
        std::string name;
        char q[32];
        std::snprintf(q, sizeof(q), "qThreadExtraInfo,%x", tid);
        if (gdb_txn(fd, q, reply, /*wait_reply=*/true) && !reply.empty() && reply.size() % 2 == 0) {
            for (std::string::size_type i = 0; i + 1 < reply.size(); i += 2) {
                int hi = hex_nibble(reply[i]);
                int lo = hex_nibble(reply[i + 1]);
                if (hi < 0 || lo < 0) {
                    name.clear();
                    break;
                }
                name.push_back(static_cast<char>((hi << 4) | lo));
            }
        }
        if (name.empty()) name = "core";

        m_cores.push_back(core_t{ idx, tid, name });
        SCP_INFO(()) << "mcd_server: core " << (m_cores.size() - 1) << " = instance " << idx << " tid " << tid << " '"
                     << name << "'";
    }

    m_threads_known[idx] = true;
}

/* Open a session to every instance lacking one. Doing so pauses that instance,
 * and an object-model query must not change run state, so resume unless the
 * debugger asked for the halt. Caller must hold m_gdb_mutex. */
void mcd_server::ensure_cores_known()
{
    for (uint32_t idx = 0; idx < m_gdb_ports.size(); ++idx) {
        if (m_threads_known[idx]) continue;

        bool was_open = (m_gdb_fds[idx] != INVALID_SOCK);
        if (gdb_session(idx) == INVALID_SOCK) continue;
        if (was_open) continue; /* already the debugger's session; leave its state alone */

        std::string reply;
        if (gdb_cmd(idx, "vCont;c", reply, /*wait_reply=*/false)) {
            m_gdb_running[idx] = true;
            SCP_DEBUG(()) << "mcd_server: resumed instance " << idx << " after core enumeration";
        }
    }

    /* Deliberately NOT update_debug_hold(): 'vCont;c' is acked long before QEMU
     * restarts the vCPUs, so releasing the hold here can starve the kernel. The
     * hold stays until a request with an observed outcome re-evaluates it. */
}

/* Point register access at @p core with "Hg<tid>". QEMU tracks this as
 * gdbserver_state.g_cpu, so it must be re-sent whenever the target core changes.
 * Caller must hold m_gdb_mutex. */
bool mcd_server::gdb_select_core(uint32_t core)
{
    if (core >= m_cores.size()) return false;

    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "Hg%x", m_cores[core].tid);
    std::string reply;
    if (!gdb_cmd(m_cores[core].inst, cmd, reply)) return false;
    if (reply != "OK") {
        SCP_WARN(()) << "mcd_server: '" << cmd << "' rejected: '" << reply << "'";
        return false;
    }
    return true;
}

void mcd_server::gdb_close_all()
{
    std::lock_guard<std::mutex> lock(m_gdb_mutex);

    /* Disarm everything: a leftover breakpoint would halt the target with no
     * debugger listening, and the hold is released below, so that halt would
     * starve the kernel. */
    for (const breakpoint_t& bp : m_breakpoints) {
        char digit;
        if (bp.core >= m_cores.size() || !bp_type_to_gdb(bp.type, digit)) continue;
        char cmd[48];
        std::snprintf(cmd, sizeof(cmd), "z%c,%llx,%x", digit, static_cast<unsigned long long>(bp.addr), bp.kind);
        std::string reply;
        if (!gdb_cmd(m_cores[bp.core].inst, cmd, reply)) {
            SCP_WARN(()) << "mcd_server: could not remove '" << cmd << "' on detach";
        }
    }
    if (!m_breakpoints.empty()) {
        SCP_DEBUG(()) << "mcd_server: removed " << m_breakpoints.size() << " breakpoint(s) on detach";
        m_breakpoints.clear();
    }

    /* Resume anything left halted before dropping the sessions: closing the socket
     * does not restart the instance, and the hold is released below. Detaching
     * hands the platform back as if no debugger had attached. */
    bool resumed_any = false;
    for (uint32_t idx = 0; idx < m_gdb_fds.size(); ++idx) {
        if (m_gdb_fds[idx] == INVALID_SOCK) continue;
        if (m_gdb_running[idx]) continue;

        std::string reply;
        if (gdb_cmd(idx, "vCont;c", reply, /*wait_reply=*/false)) {
            m_gdb_running[idx] = true;
            resumed_any = true;
            SCP_DEBUG(()) << "mcd_server: resumed instance " << idx << " on debugger detach";
        } else {
            SCP_WARN(()) << "mcd_server: could not resume instance " << idx << " on detach; it stays halted";
        }
    }

    /* Wait for that resume to take effect before the hold is dropped below: the
     * ack precedes QEMU restarting the vCPUs, and releasing in that window
     * starves the kernel - the very exit detaching must not cause. */
    if (resumed_any && !resume_observed()) {
        SCP_WARN(()) << "mcd_server: target not observed running after the detach resume";
    }

    for (socket_t& fd : m_gdb_fds) {
        if (fd != INVALID_SOCK) {
            CLOSE_SOCKET(fd);
            fd = INVALID_SOCK;
        }
    }
    /* Forget the discovered cores so a later client re-enumerates. Only a full
     * close resets this; a session dropped mid-use keeps its entry. */
    m_cores.clear();
    std::fill(m_threads_known.begin(), m_threads_known.end(), false);
    std::fill(m_last_stop.begin(), m_last_stop.end(), last_stop_t{});

    /* No sessions left: release the hold so the simulation can end normally. */
    debug_hold(false);
}

/* Run one RSP command. Any transport failure closes and invalidates the session
 * so the next call reconnects clean; otherwise one wedged command breaks every
 * later query on that connection. Caller must hold m_gdb_mutex. */
bool mcd_server::gdb_cmd(uint32_t idx, const std::string& cmd, std::string& reply, bool wait_reply)
{
    socket_t fd = gdb_session(idx);
    if (fd == INVALID_SOCK) return false;

    /* A stop-reply may arrive mid-command; gdb_write_packet skips it to keep the
     * stream framed, so record here that the instance is no longer running. */
    std::string stray;
    bool ok = gdb_txn(fd, cmd, reply, wait_reply, &stray);
    if (!stray.empty()) {
        m_gdb_running[idx] = false;
        SCP_INFO(()) << "mcd_server: instance " << idx << " reported a stop while '" << cmd << "' was in flight: '"
                     << stray << "'";
    }
    if (ok) return true;

    CLOSE_SOCKET(fd);
    m_gdb_fds[idx] = INVALID_SOCK;
    m_gdb_running[idx] = false;
    return false;
}

void mcd_server::note_stop(uint32_t idx, uint32_t reason, uint64_t watch_addr, uint32_t tid)
{
    if (idx >= m_last_stop.size()) return;
    m_last_stop[idx] = last_stop_t{ true, reason, watch_addr, tid };
}

/* Non-blocking check for an unsolicited stop-reply: a breakpoint hit becomes a
 * global vm_stop() with no request from us, so without this m_gdb_running is only
 * what we last told the instance. Caller must hold m_gdb_mutex. */
void mcd_server::drain_pending_stop(uint32_t idx)
{
    if (idx >= m_gdb_fds.size()) return;
    if (!m_gdb_running[idx]) return; /* we already believe it is stopped */

    socket_t fd = m_gdb_fds[idx];
    if (fd == INVALID_SOCK) return;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = { 0, 0 }; /* poll: do not wait */

    if (::select(SOCK_NFDS(fd), &rfds, nullptr, nullptr, &tv) <= 0) return;

    std::string reply;
    if (!gdb_read_packet(fd, reply)) {
        CLOSE_SOCKET(fd);
        m_gdb_fds[idx] = INVALID_SOCK;
        m_gdb_running[idx] = false;
        return;
    }

    m_gdb_running[idx] = false;

    /* Cache it: the core it belongs to must still be able to learn why it
     * stopped, even though the packet is consumed here. */
    uint64_t watch_addr = 0, tid = 0;
    uint32_t reason = gdb_classify_stop(reply, watch_addr);
    gdb_stop_field(reply, "thread:", tid);
    note_stop(idx, reason, watch_addr, static_cast<uint32_t>(tid));

    SCP_INFO(()) << "mcd_server: instance " << idx << " had already stopped on its own: '" << reply << "'";
}

/* Wait up to @p timeout_ms for the stop-reply owed after a 'c' sent with
 * wait_reply=false. Returns an MCD_STOP_* reason. Caller holds m_gdb_mutex. */
uint32_t mcd_server::gdb_wait_stop(uint32_t idx, uint32_t timeout_ms, uint64_t& watch_addr, uint32_t& stopped_tid)
{
    watch_addr = 0;
    stopped_tid = 0;
    if (idx >= m_gdb_fds.size()) return MCD_STOP_HALTED;

    /* Not running -> already halted; nothing will arrive on the socket. */
    if (!m_gdb_running[idx]) return MCD_STOP_HALTED;

    socket_t fd = m_gdb_fds[idx];
    if (fd == INVALID_SOCK) return MCD_STOP_HALTED;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int sel = ::select(SOCK_NFDS(fd), &rfds, nullptr, nullptr, &tv);
    if (sel <= 0) {
        /* 0 = timeout (still running); <0 = error (leave the session as is). */
        return MCD_STOP_RUNNING;
    }

    std::string reply;
    if (!gdb_read_packet(fd, reply)) {
        CLOSE_SOCKET(fd);
        m_gdb_fds[idx] = INVALID_SOCK;
        m_gdb_running[idx] = false;
        return MCD_STOP_HALTED;
    }

    m_gdb_running[idx] = false;
    uint32_t reason = gdb_classify_stop(reply, watch_addr);
    /* 'T' replies carry "thread:<tid>;": the instance halts as a whole, but this
     * names the core that hit the breakpoint. */
    uint64_t tid = 0;
    if (gdb_stop_field(reply, "thread:", tid)) stopped_tid = static_cast<uint32_t>(tid);
    note_stop(idx, reason, watch_addr, stopped_tid);
    SCP_INFO(()) << "mcd_server: WAIT_STOP inst=" << idx << " reply='" << reply << "' reason=" << reason
                 << " tid=" << stopped_tid;
    return reason;
}

void mcd_server::add_mem_space(uint32_t space_id, const std::string& name,
                               std::function<unsigned int(tlm::tlm_generic_payload&)> fn)
{
    mcd_mem_space_st space;
    space.mem_space_id = space_id;
    std::memset(space.mem_space_name, 0, sizeof(space.mem_space_name));
    std::strncpy(space.mem_space_name, name.c_str(), sizeof(space.mem_space_name) - 1);
    m_mem_spaces.push_back(space);
    m_transactors[space_id] = std::move(fn);
}

void mcd_server::bind_target(tlm::tlm_initiator_socket<>* socket, uint32_t space_id, const std::string& name)
{
    add_mem_space(space_id, name,
                  [socket](tlm::tlm_generic_payload& txn) -> unsigned int { return (*socket)->transport_dbg(txn); });
    SCP_INFO(()) << "bind_target: initiator socket registered for space id=" << space_id << " name='" << name << "'";
}

/* Auto-discover routers and QEMU instances, and wire up the memory spaces. */
void mcd_server::before_end_of_elaboration()
{
    auto routers = gs::find_sc_objects<gs::router_if<>>();
    for (auto* ri : routers) {
        auto* sc_obj = dynamic_cast<sc_core::sc_object*>(ri);
        if (!sc_obj) continue;

        /* The router's target socket is the child that casts to
         * tlm_base_target_socket_b; its initiator socket is a different base
         * type and will not match. */
        tlm::tlm_base_target_socket_b<>* ts = nullptr;
        for (auto* child : sc_obj->get_child_objects()) {
            ts = dynamic_cast<tlm::tlm_base_target_socket_b<>*>(child);
            if (ts) break;
        }
        if (!ts) continue;

        std::string space_name = sc_obj->name();
        uint32_t space_id = static_cast<uint32_t>(m_mem_spaces.size());

        /* Issue transport_dbg directly on the target socket's export: no extra
         * initiator socket, and no elaboration-time bind. */
        add_mem_space(space_id, space_name, [ts](tlm::tlm_generic_payload& txn) -> unsigned int {
            return ts->get_base_export()->transport_dbg(txn);
        });

        SCP_INFO(()) << "mcd_server: auto-discovered router '" << space_name << "' as mem_space id=" << space_id;
    }

    /* One gdbstub port per QEMU instance, not per CPU. mcd_server links only
     * `router`, so an instance is recognised by owning both a "tcg_mode" and a
     * "gdb_port" CCI param; a CPU has only a deprecated "gdb_port". */
    {
        /* Inside the hierarchy an originator must not be explicitly named, so use
         * the current-object broker. */
        cci::cci_broker_handle broker = sc_core::sc_get_current_object()
                                            ? cci::cci_get_broker()
                                            : cci::cci_get_global_broker(cci::cci_originator("mcd_server"));
        cci::cci_param_predicate all_params([](const cci::cci_param_untyped_handle&) { return true; });
        auto handles = broker.get_param_handles(all_params);

        /* Modules owning a "tcg_mode" param — i.e. QEMU instances. */
        std::vector<std::string> inst_modules;
        for (auto& ph : handles) {
            const std::string pname = ph.name();
            auto dot = pname.rfind('.');
            if (dot == std::string::npos) continue;
            if (pname.substr(dot + 1) != "tcg_mode") continue;
            inst_modules.push_back(pname.substr(0, dot));
        }

        for (const std::string& modname : inst_modules) {
            const std::string pname = modname + ".gdb_port";
            cci::cci_param_typed_handle<unsigned int> typed(broker.get_param_handle(pname));
            if (!typed.is_valid()) {
                SCP_WARN(()) << "mcd_server: QEMU instance '" << modname
                             << "' has no gdb_port parameter; run-control unavailable for its cores";
                continue;
            }
            unsigned port = typed.get_value();

            /* QEMU serves one gdb session per instance, so a non-zero gdb_port
             * is left for the user's gdb: taking it would leave their session
             * connected but unanswered. An explicit 0 is not a request for gdb. */
            if (port != 0) {
                auto prev = sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR,
                                                                    sc_core::SC_LOG | sc_core::SC_DISPLAY);
                SC_REPORT_ERROR("mcd_server", (pname + " is set explicitly (" + std::to_string(port) +
                                               "), so it is left for an external gdb: Leave it unset, or set it to 0 "
                                               "for MCD to debug this qemu instance.")
                                                  .c_str());
                sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, prev);
                continue;
            }

            /* Take a private port: bind to 0 on loopback, read it back, close. */
            socket_t tmp = ::socket(AF_INET, SOCK_STREAM, 0);
            if (tmp != INVALID_SOCK) {
                struct sockaddr_in sa = {};
                sa.sin_family = AF_INET;
                sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                sa.sin_port = 0;
                if (::bind(tmp, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) == 0) {
                    socklen_t len = sizeof(sa);
                    if (::getsockname(tmp, reinterpret_cast<struct sockaddr*>(&sa), &len) == 0) {
                        port = ntohs(sa.sin_port);
                        typed.set_value(port);
                        SCP_INFO(()) << "mcd_server: auto-assigned gdb_port=" << port << " for '" << pname << "'";
                    }
                }
                CLOSE_SOCKET(tmp);
            }
            if (port == 0) {
                SCP_WARN(()) << "mcd_server: could not find a free port for '" << pname
                             << "'; run-control unavailable for this instance's cores";
                continue;
            }

            sc_core::sc_object* sc_obj = sc_core::sc_find_object(modname.c_str());
            if (!sc_obj) {
                SCP_WARN(()) << "mcd_server: found gdb_port for '" << modname << "' but no sc_object with that name";
                continue;
            }
            bind_instance(sc_obj, "127.0.0.1:" + std::to_string(port));
        }
    }
}

void mcd_server::end_of_elaboration()
{
    if (p_mcd_port.get_value()) {
        SCP_INFO(()) << "Starting MCD server on TCP port " << p_mcd_port.get_value();
        start_server();
    }
}

void mcd_server::start_server()
{
    m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listen_fd == INVALID_SOCK) {
        SCP_ERR(()) << "mcd_server: socket() failed: " << sock_err();
        return;
    }

    int one = 1;
    ::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));

    set_nosigpipe(m_listen_fd);

    /* Loopback only, never INADDR_ANY: this port grants unauthenticated read/write
     * access to all simulated memory and registers. Remote debuggers tunnel in. */
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(p_mcd_port.get_value()));

    if (::bind(m_listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SCP_ERR(()) << "mcd_server: bind() failed on port " << p_mcd_port.get_value() << ": " << sock_err();
        CLOSE_SOCKET(m_listen_fd);
        m_listen_fd = INVALID_SOCK;
        return;
    }

    if (::listen(m_listen_fd, 1) < 0) {
        SCP_ERR(()) << "mcd_server: listen() failed: " << sock_err();
        CLOSE_SOCKET(m_listen_fd);
        m_listen_fd = INVALID_SOCK;
        return;
    }

    /* No suspending channel here: the debug server does not change when the
     * simulation ends. Platforms needing to stay open across debugger pauses
     * instantiate the `keep_alive` component. */

    m_running = true;
    m_thread = std::thread(&mcd_server::server_thread, this);
}

/* Accept loop: a POSIX thread, NOT a SystemC thread. */
void mcd_server::server_thread()
{
    while (m_running) {
        /* Snapshot the listening fd: the destructor stores INVALID_SOCK to break us
         * out, and FD_SET on an invalid fd is undefined behaviour. */
        socket_t listen_fd = m_listen_fd;
        if (listen_fd == INVALID_SOCK) break;

        /* select() with a timeout, so m_running is re-checked periodically. */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = ::select(SOCK_NFDS(listen_fd), &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (SOCK_ERR_IS_INTR()) continue;
            break;
        }
        if (sel == 0) continue; /* timeout: re-check m_running */

        socket_t client = ::accept(listen_fd, nullptr, nullptr);
        if (client == INVALID_SOCK) {
            if (SOCK_ERR_IS_INTR()) continue;
            if (!m_running) break;
            continue;
        }
        set_nosigpipe(client);
        if (!m_running) {
            CLOSE_SOCKET(client);
            break;
        }

        SCP_INFO(()) << "mcd_server: client connected";
        /* Publish the fd so the destructor can shut it down: handle_client blocks
         * in recv() on it, which closing the listening socket does not interrupt. */
        m_client_fd = client;
        handle_client(client);
        m_client_fd = INVALID_SOCK;
        CLOSE_SOCKET(client);
        /* Debugger gone: drop the gdb sessions, releasing any simulation hold. */
        gdb_close_all();
        SCP_INFO(()) << "mcd_server: client disconnected";
    }
    m_thread_done = true;
}

void mcd_server::handle_client(socket_t fd)
{
    while (m_running) {
        /* Frame in: 4-byte LE length, then <length> bytes = 1 opcode + payload. */
        uint8_t lenbuf[4];
        if (!recv_all(fd, lenbuf, sizeof(lenbuf))) return;
        uint32_t len = get_u32(lenbuf);
        if (len == 0) {
            SCP_WARN(()) << "mcd_server: zero-length frame, dropping client";
            return;
        }
        /* Refuse before allocating: the length is attacker-controlled, and a
         * bad_alloc thrown out of this worker thread terminates the process. */
        if (len > MCD_MAX_FRAME) {
            SCP_WARN(()) << "mcd_server: frame length " << len << " exceeds the " << MCD_MAX_FRAME
                         << "-byte limit, dropping client";
            return;
        }

        std::vector<uint8_t> frame(len);
        if (!recv_all(fd, frame.data(), len)) return;

        uint8_t opcode = frame[0];
        std::vector<uint8_t> req(frame.begin() + 1, frame.end());
        std::vector<uint8_t> resp;

        mcd_return_et rc = dispatch(opcode, req, resp);

        /* Frame out: 4-byte LE length (return code byte + payload), return
         * code, payload. */
        std::vector<uint8_t> out;
        uint32_t outlen = static_cast<uint32_t>(1 + resp.size());
        put_u32(out, outlen);
        out.push_back(static_cast<uint8_t>(rc));
        out.insert(out.end(), resp.begin(), resp.end());

        if (!send_all(fd, out.data(), out.size())) return;
    }
}

mcd_return_et mcd_server::dispatch(uint8_t opcode, const std::vector<uint8_t>& req, std::vector<uint8_t>& resp)
{
    switch (opcode) {
    case MCD_OP_QRY_SERVERS:
        return op_qry_servers(req, resp);
    case MCD_OP_QRY_SYSTEMS:
        return op_qry_systems(req, resp);
    case MCD_OP_QRY_DEVICES:
        return op_qry_devices(req, resp);
    case MCD_OP_QRY_CORES:
        return op_qry_cores(req, resp);
    case MCD_OP_QRY_MEM_SPACES:
        return op_qry_mem_spaces(req, resp);
    case MCD_OP_READ_MEM:
        return op_read_mem(req, resp);
    case MCD_OP_WRITE_MEM:
        return op_write_mem(req, resp);
    case MCD_OP_RUN:
        return op_run(req, resp);
    case MCD_OP_STOP:
        return op_stop(req, resp);
    case MCD_OP_STEP:
        return op_step(req, resp);
    case MCD_OP_READ_REG:
        return op_read_reg(req, resp);
    case MCD_OP_WRITE_REG:
        return op_write_reg(req, resp);
    case MCD_OP_SET_BP:
        return op_set_bp(req, resp);
    case MCD_OP_CLR_BP:
        return op_clr_bp(req, resp);
    case MCD_OP_LIST_BP:
        return op_list_bp(req, resp);
    case MCD_OP_WAIT_STOP:
        return op_wait_stop(req, resp);
    default:
        SCP_WARN(()) << "mcd_server: unknown opcode 0x" << std::hex << static_cast<int>(opcode);
        return MCD_RET_ERR_GENERAL;
    }
}

mcd_return_et mcd_server::mcd_qry_servers(uint32_t* num_servers, mcd_server_st* servers)
{
    if (!num_servers) return MCD_RET_ERR_GENERAL;
    /* A single QBox process presents exactly one MCD server. */
    if (servers) {
        std::lock_guard<std::mutex> lock(m_gdb_mutex);
        ensure_cores_known();
        servers[0].num_cores = static_cast<uint32_t>(m_cores.size());
    }
    *num_servers = 1;
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::mcd_qry_systems(uint32_t* num_systems, mcd_system_st* systems)
{
    if (!num_systems) return MCD_RET_ERR_GENERAL;
    if (systems) {
        std::memset(systems[0].system_name, 0, sizeof(systems[0].system_name));
        std::strncpy(systems[0].system_name, "qbox", sizeof(systems[0].system_name) - 1);
    }
    *num_systems = 1;
    return MCD_RET_ACT_NONE;
}

/* One device per QEMU instance. @p cap is the capacity of @p devices. */
mcd_return_et mcd_server::mcd_qry_devices(uint32_t* num_devices, mcd_device_st* devices, uint32_t cap)
{
    if (!num_devices) return MCD_RET_ERR_GENERAL;

    if (m_insts.empty()) {
        /* Present one device anyway, so a client has something to hang the
         * memory spaces off. */
        if (devices && cap) {
            std::memset(devices[0].device_name, 0, sizeof(devices[0].device_name));
            std::strncpy(devices[0].device_name, "qbox_device", sizeof(devices[0].device_name) - 1);
            *num_devices = 1;
        } else {
            *num_devices = 0;
        }
        return MCD_RET_ACT_NONE;
    }

    uint32_t n = 0;
    for (uint32_t i = 0; devices && i < m_insts.size() && i < cap; ++i) {
        std::memset(devices[i].device_name, 0, sizeof(devices[i].device_name));
        const char* dev = (m_insts[i] && m_insts[i]->name()) ? m_insts[i]->name() : "qbox_device";
        std::strncpy(devices[i].device_name, dev, sizeof(devices[i].device_name) - 1);
        n = i + 1;
    }
    *num_devices = devices ? n : static_cast<uint32_t>(m_insts.size());
    return MCD_RET_ACT_NONE;
}

/* Every core, in MCD core-id order; device_id is the owning instance's index. */
mcd_return_et mcd_server::mcd_qry_cores(uint32_t* num_cores, mcd_core_st* cores, uint32_t cap)
{
    if (!num_cores) return MCD_RET_ERR_GENERAL;

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    uint32_t n = 0;
    for (uint32_t i = 0; cores && i < m_cores.size() && i < cap; ++i) {
        cores[i].core_id = i;
        cores[i].device_id = m_cores[i].inst;
        n = i + 1;
    }
    *num_cores = cores ? n : static_cast<uint32_t>(m_cores.size());
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_qry_servers(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    uint32_t n = 0;
    mcd_server_st srv[1];
    mcd_return_et rc = mcd_qry_servers(&n, srv);
    if (rc != MCD_RET_ACT_NONE) return rc;

    put_u32(resp, n);
    for (uint32_t i = 0; i < n && i < 1; ++i) {
        put_u32(resp, srv[i].num_cores);
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_qry_systems(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    uint32_t n = 0;
    mcd_system_st sys[1];
    mcd_return_et rc = mcd_qry_systems(&n, sys);
    if (rc != MCD_RET_ACT_NONE) return rc;

    put_u32(resp, n);
    for (uint32_t i = 0; i < n && i < 1; ++i) {
        resp.insert(resp.end(), sys[i].system_name, sys[i].system_name + sizeof(sys[i].system_name));
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_qry_devices(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    uint32_t n = 0;
    std::vector<mcd_device_st> dev(m_insts.empty() ? 1 : m_insts.size());
    mcd_return_et rc = mcd_qry_devices(&n, dev.data(), static_cast<uint32_t>(dev.size()));
    if (rc != MCD_RET_ACT_NONE) return rc;

    put_u32(resp, n);
    for (uint32_t i = 0; i < n; ++i) {
        resp.insert(resp.end(), dev[i].device_name, dev[i].device_name + sizeof(dev[i].device_name));
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_qry_cores(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    /* Count first (null array), then size the buffer: the core table is only
     * populated once the gdbstub has been asked. */
    uint32_t count = 0;
    mcd_return_et rc = mcd_qry_cores(&count, nullptr, 0);
    if (rc != MCD_RET_ACT_NONE) return rc;

    uint32_t n = 0;
    std::vector<mcd_core_st> cores(count);
    if (count) {
        rc = mcd_qry_cores(&n, cores.data(), count);
        if (rc != MCD_RET_ACT_NONE) return rc;
    }

    put_u32(resp, n);
    for (uint32_t i = 0; i < n; ++i) {
        put_u32(resp, cores[i].core_id);
        put_u32(resp, cores[i].device_id);
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_qry_mem_spaces(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    /* Response payload: 4-byte LE count, then for each space a 4-byte LE
     * mem_space_id followed by the fixed 64-byte mem_space_name. */
    put_u32(resp, static_cast<uint32_t>(m_mem_spaces.size()));
    for (const mcd_mem_space_st& space : m_mem_spaces) {
        put_u32(resp, space.mem_space_id);
        resp.insert(resp.end(), space.mem_space_name, space.mem_space_name + sizeof(space.mem_space_name));
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_read_mem(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp)
{
    /* Request: addr u64, length u32, optional space_id u32. A 12-byte payload
     * (no space_id) means space 0, "physical". */
    if (req.size() < 12) return MCD_RET_ERR_GENERAL;

    uint64_t addr = get_u64(&req[0]);
    uint32_t length = get_u32(&req[8]);
    uint32_t space_id = (req.size() >= 16) ? get_u32(&req[12]) : 0u;

    if (length > MCD_MAX_FRAME) {
        SCP_WARN(()) << "mcd_server: read_mem length " << length << " exceeds the " << MCD_MAX_FRAME << "-byte limit";
        return MCD_RET_ERR_GENERAL;
    }

    auto it = m_transactors.find(space_id);
    if (it == m_transactors.end() || !it->second) {
        SCP_WARN(()) << "mcd_server: read_mem with no bound target socket for space id=" << space_id;
        return MCD_RET_ERR_GENERAL;
    }

    if (length == 0) return MCD_RET_ACT_NONE;

    std::vector<uint8_t> data(length, 0);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(addr);
    txn.set_data_ptr(data.data());
    txn.set_data_length(length);
    txn.set_streaming_width(length);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    /* transport_dbg must run on the SystemC kernel thread: TLM is not thread safe.
     * run_on_sysc() blocks until the job has run, keeping `txn`/`data` valid, and
     * returns false if the simulation already ended (transaction never ran). */
    unsigned int done = 0;
    if (!m_sc.run_on_sysc([&] { done = it->second(txn); })) {
        SCP_WARN(()) << "mcd_server: read_mem abandoned, simulation has ended";
        return MCD_RET_ERR_GENERAL;
    }

    if (done != length) {
        SCP_WARN(()) << "mcd_server: read_mem short read " << done << "/" << length << " @0x" << std::hex << addr;
        return MCD_RET_ERR_GENERAL;
    }

    resp.insert(resp.end(), data.begin(), data.end());
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_write_mem(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp)
{
    /* Request: addr u64, length u32, optional space_id u32, then <length> data
     * bytes. Header size (16 or 12 bytes, the latter meaning space 0) is inferred
     * from the total payload. */
    (void)resp;
    if (req.size() < 12) return MCD_RET_ERR_GENERAL;

    uint64_t addr = get_u64(&req[0]);
    uint32_t length = get_u32(&req[8]);

    if (length > MCD_MAX_FRAME) {
        SCP_WARN(()) << "mcd_server: write_mem length " << length << " exceeds the " << MCD_MAX_FRAME << "-byte limit";
        return MCD_RET_ERR_GENERAL;
    }

    /* 64-bit arithmetic is required: `16u + length` wraps in 32 bits and a wrapped
     * sum compares as satisfied, selecting a data offset past the buffer end. */
    const uint64_t len64 = length;
    uint32_t space_id;
    std::vector<uint8_t>::size_type data_off;
    if (static_cast<uint64_t>(req.size()) >= 16u + len64) {
        space_id = get_u32(&req[12]);
        data_off = 16;
    } else if (static_cast<uint64_t>(req.size()) >= 12u + len64) {
        space_id = 0;
        data_off = 12;
    } else {
        return MCD_RET_ERR_GENERAL;
    }

    auto it = m_transactors.find(space_id);
    if (it == m_transactors.end() || !it->second) {
        SCP_WARN(()) << "mcd_server: write_mem with no bound target socket for space id=" << space_id;
        return MCD_RET_ERR_GENERAL;
    }

    if (length == 0) return MCD_RET_ACT_NONE;

    std::vector<uint8_t> data(req.begin() + data_off, req.begin() + data_off + length);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(addr);
    txn.set_data_ptr(data.data());
    txn.set_data_length(length);
    txn.set_streaming_width(length);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    /* See op_read_mem: executed by the SystemC kernel thread. */
    unsigned int done = 0;
    if (!m_sc.run_on_sysc([&] { done = it->second(txn); })) {
        SCP_WARN(()) << "mcd_server: write_mem abandoned, simulation has ended";
        return MCD_RET_ERR_GENERAL;
    }

    if (done != length) {
        SCP_WARN(()) << "mcd_server: write_mem short write " << done << "/" << length << " @0x" << std::hex << addr;
        return MCD_RET_ERR_GENERAL;
    }
    return MCD_RET_ACT_NONE;
}

/* Hold the simulation open only while the debugger is the reason the CPUs are
 * stopped; see m_debug_hold in the header. */
void mcd_server::debug_hold(bool hold)
{
    if (hold == m_debug_hold_active) return; /* attach/detach are not counted */
    m_debug_hold_active = hold;
    if (hold) {
        SCP_DEBUG(()) << "mcd_server: holding simulation open (debugger owns a halted CPU)";
        m_debug_hold.async_attach_suspending();
    } else {
        SCP_DEBUG(()) << "mcd_server: releasing simulation hold (all CPUs resumed)";
        m_debug_hold.async_detach_suspending();
    }
}

void mcd_server::update_debug_hold()
{
    bool hold = false;
    for (size_t idx = 0; idx < m_gdb_fds.size(); ++idx) {
        if (m_gdb_fds[idx] == INVALID_SOCK) continue; /* debugger does not own this instance */

        if (!m_gdb_running[idx]) {
            /* Explicitly halted by us. */
            hold = true;
            break;
        }
        /* Running, but an armed breakpoint can halt it at any moment and stop the
         * quantum keeper, so hold until the client resumes with nothing armed. */
        if (!m_breakpoints.empty()) {
            hold = true;
            break;
        }
    }
    debug_hold(hold);
}

/* Wait, bounded, for simulated time to advance. Marshalled onto the SystemC
 * thread, which also fails once the simulation has ended. */
bool mcd_server::resume_observed()
{
    sc_core::sc_time t0;
    if (!m_sc.run_on_sysc([&] { t0 = sc_core::sc_time_stamp(); })) return false;

    for (int attempt = 0; attempt < 200; ++attempt) { /* ~2 s */
        sc_core::sc_time now;
        if (!m_sc.run_on_sysc([&] { now = sc_core::sc_time_stamp(); })) return false;
        if (now > t0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

mcd_return_et mcd_server::op_run(const std::vector<uint8_t>&, std::vector<uint8_t>&)
{
    if (m_gdb_ports.empty()) {
        SCP_WARN(()) << "mcd_server: RUN with no GDB-RSP endpoint";
        return MCD_RET_ERR_GENERAL;
    }

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    bool all_ok = true;
    bool resumed_any = false;
    /* Run is per instance: continue is a global vm_start(), so one 'c' resumes
     * every core of the instance. */
    for (uint32_t idx = 0; idx < m_gdb_ports.size(); ++idx) {
        /* m_gdb_running is only what we last told the instance, so drain any
         * pending stop-reply before deciding it is already running. */
        drain_pending_stop(idx);

        if (m_gdb_running[idx]) continue; /* already running */

        /* Read only the '+' ack: the stop-reply arrives only on the next stop. */
        std::string reply;
        if (!gdb_cmd(idx, "c", reply, /*wait_reply=*/false)) {
            SCP_WARN(()) << "mcd_server: RUN forward to " << m_gdb_ports[idx] << " failed";
            all_ok = false;
            continue;
        }
        m_gdb_running[idx] = true;
        resumed_any = true;
        SCP_INFO(()) << "mcd_server: RUN -> gdb 'c' @" << m_gdb_ports[idx];
    }

    /* Deciding whether to drop the hold needs the resume to have taken effect:
     * 'c' is acked long before QEMU restarts the vCPUs, and detaching inside
     * that window leaves the kernel with no suspending channel, so the
     * simulation exits by starvation and the *next* request fails on a dead
     * socket. Simulated time advancing is the proof, since only a running vCPU
     * advances it. Skipped when a breakpoint is armed, as the hold then stays
     * regardless; an unconfirmed resume also just keeps it, and is never an
     * error - the 'c' itself was accepted. */
    if (resumed_any && m_breakpoints.empty()) resume_observed();
    update_debug_hold();
    return all_ok ? MCD_RET_ACT_NONE : MCD_RET_ERR_GENERAL;
}

mcd_return_et mcd_server::op_stop(const std::vector<uint8_t>&, std::vector<uint8_t>&)
{
    if (m_gdb_ports.empty()) {
        SCP_WARN(()) << "mcd_server: STOP with no GDB-RSP endpoint";
        return MCD_RET_ERR_GENERAL;
    }

    std::lock_guard<std::mutex> lock(m_gdb_mutex);

    /* Take the hold before any interrupt: the attach is deferred to the SystemC
     * thread, so it must precede the halt that stops the quantum keeper. */
    debug_hold(true);

    bool all_ok = true;
    /* Per instance: a 0x03 interrupt makes QEMU vm_stop() every core of it. */
    for (uint32_t idx = 0; idx < m_gdb_ports.size(); ++idx) {
        socket_t fd = gdb_session(idx);
        if (fd == INVALID_SOCK) {
            all_ok = false;
            continue;
        }
        drain_pending_stop(idx);

        /* Connecting the session already halted it: nothing to interrupt. */
        if (!m_gdb_running[idx]) continue;

        /* The 0x03 break byte is sent raw, not as a framed packet; QEMU answers
         * with a stop-reply, which is consumed here. */
        const uint8_t brk = 0x03;
        bool ok = send_all(fd, &brk, 1);
        std::string reply;
        if (ok) ok = gdb_read_packet(fd, reply);
        if (!ok) {
            SCP_WARN(()) << "mcd_server: STOP forward to " << m_gdb_ports[idx] << " failed";
            all_ok = false;
            continue;
        }
        m_gdb_running[idx] = false;
        SCP_INFO(()) << "mcd_server: STOP -> gdb 0x03 @" << m_gdb_ports[idx] << " reply='" << reply << "'";
    }
    return all_ok ? MCD_RET_ACT_NONE : MCD_RET_ERR_GENERAL;
}

mcd_return_et mcd_server::op_step(const std::vector<uint8_t>& req, std::vector<uint8_t>&)
{
    if (m_gdb_ports.empty()) {
        SCP_WARN(()) << "mcd_server: STEP with no GDB-RSP endpoint";
        return MCD_RET_ERR_GENERAL;
    }

    /* Request: optional core id u32; empty payload means core 0. */
    uint32_t core = (req.size() >= 4) ? get_u32(&req[0]) : 0u;

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    if (core >= m_cores.size()) {
        SCP_WARN(()) << "mcd_server: STEP core id " << core << " out of range";
        return MCD_RET_ERR_GENERAL;
    }
    uint32_t inst = m_cores[core].inst;

    /* "vCont;s:<tid>" steps exactly one core, leaving the others halted:
     * gdb_continue_partial resumes only the CPUs named. A bare 's' would step
     * whichever core the stub has selected. */
    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "vCont;s:%x", m_cores[core].tid);

    std::string reply;
    if (!gdb_cmd(inst, cmd, reply)) {
        SCP_WARN(()) << "mcd_server: STEP forward to " << m_gdb_ports[inst] << " failed";
        return MCD_RET_ERR_GENERAL;
    }
    if (reply.empty()) {
        /* An empty packet means the stub does not support vCont at all. */
        SCP_WARN(()) << "mcd_server: STEP '" << cmd << "' unsupported by " << m_gdb_ports[inst];
        return MCD_RET_ERR_GENERAL;
    }
    /* Stepping briefly resumes the instance (vm_prepare_start); it is halted
     * again once the step completes. */
    m_gdb_running[inst] = false;
    update_debug_hold();
    SCP_INFO(()) << "mcd_server: STEP core=" << core << " -> '" << cmd << "' @" << m_gdb_ports[inst] << " reply='"
                 << reply << "'";
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_read_reg(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp)
{
    /* Request: core id u32, register number u32. */
    if (req.size() < 8) return MCD_RET_ERR_GENERAL;
    uint32_t core = get_u32(&req[0]);
    uint32_t regno = get_u32(&req[4]);

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    if (core >= m_cores.size()) {
        SCP_WARN(()) << "mcd_server: READ_REG core id " << core << " out of range";
        return MCD_RET_ERR_GENERAL;
    }
    uint32_t inst = m_cores[core].inst;

    /* 'p' reads from the currently selected core (gdbserver_state.g_cpu), so
     * without this every core of an instance aliases the same registers. */
    if (!gdb_select_core(core)) {
        SCP_WARN(()) << "mcd_server: READ_REG could not select core " << core;
        return MCD_RET_ERR_GENERAL;
    }

    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "p%x", regno);

    std::string reply;
    if (!gdb_cmd(inst, cmd, reply)) {
        SCP_WARN(()) << "mcd_server: READ_REG forward to " << m_gdb_ports[inst] << " failed";
        return MCD_RET_ERR_GENERAL;
    }
    /* Empty reply => 'p' unsupported; "E xx" => error. */
    if (reply.empty() || reply[0] == 'E') {
        SCP_WARN(()) << "mcd_server: READ_REG regno=" << regno << " gdb error '" << reply << "'";
        return MCD_RET_ERR_GENERAL;
    }
    if (reply.size() % 2 != 0) {
        SCP_WARN(()) << "mcd_server: READ_REG odd-length hex reply '" << reply << "'";
        return MCD_RET_ERR_GENERAL;
    }

    /* Reply is the value hex-encoded in target byte order; decode preserving
     * that order. */
    for (std::string::size_type i = 0; i + 1 < reply.size(); i += 2) {
        int hi = hex_nibble(reply[i]);
        int lo = hex_nibble(reply[i + 1]);
        if (hi < 0 || lo < 0) {
            SCP_WARN(()) << "mcd_server: READ_REG bad hex reply '" << reply << "'";
            return MCD_RET_ERR_GENERAL;
        }
        resp.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_write_reg(const std::vector<uint8_t>& req, std::vector<uint8_t>&)
{
    /* Request: core id u32, register number u32, raw value in target byte order. */
    if (req.size() < 8) return MCD_RET_ERR_GENERAL;
    uint32_t core = get_u32(&req[0]);
    uint32_t regno = get_u32(&req[4]);

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    if (core >= m_cores.size()) {
        SCP_WARN(()) << "mcd_server: WRITE_REG core id " << core << " out of range";
        return MCD_RET_ERR_GENERAL;
    }
    uint32_t inst = m_cores[core].inst;

    /* 'P' writes to the currently selected core, so select it first. */
    if (!gdb_select_core(core)) {
        SCP_WARN(()) << "mcd_server: WRITE_REG could not select core " << core;
        return MCD_RET_ERR_GENERAL;
    }

    /* "P<regno>=<hex-value>", hex-encoded in the given byte order. */
    std::string cmd = "P";
    {
        char rn[16];
        std::snprintf(rn, sizeof(rn), "%x", regno);
        cmd += rn;
    }
    cmd += '=';
    for (std::vector<uint8_t>::size_type i = 8; i < req.size(); ++i) {
        char b[3];
        std::snprintf(b, sizeof(b), "%02x", req[i]);
        cmd += b;
    }

    std::string reply;
    if (!gdb_cmd(inst, cmd, reply)) {
        SCP_WARN(()) << "mcd_server: WRITE_REG forward to " << m_gdb_ports[inst] << " failed";
        return MCD_RET_ERR_GENERAL;
    }
    if (reply == "OK") {
        SCP_INFO(()) << "mcd_server: WRITE_REG regno=" << regno << " OK";
        return MCD_RET_ACT_NONE;
    }
    SCP_WARN(()) << "mcd_server: WRITE_REG regno=" << regno << " gdb reply '" << reply << "'";
    return MCD_RET_ERR_GENERAL;
}

/* Breakpoints use RSP Z/z packets: "Z<type>,<addr>,<kind>" sets, "z..." removes;
 * QEMU replies "OK", "E xx", or empty if unsupported. SET_BP/CLR_BP request is
 * [core u32][type u32][addr u64][kind u32] (20 bytes). */
static bool decode_bp_req(const std::vector<uint8_t>& req, uint32_t& core, uint32_t& type, uint64_t& addr,
                          uint32_t& kind)
{
    if (req.size() < 20) return false;
    core = get_u32(&req[0]);
    type = get_u32(&req[4]);
    addr = get_u64(&req[8]);
    kind = get_u32(&req[16]);
    return true;
}

mcd_return_et mcd_server::op_set_bp(const std::vector<uint8_t>& req, std::vector<uint8_t>&)
{
    uint32_t core, type, kind;
    uint64_t addr;
    if (!decode_bp_req(req, core, type, addr, kind)) return MCD_RET_ERR_GENERAL;
    char digit;
    if (!bp_type_to_gdb(type, digit)) {
        SCP_WARN(()) << "mcd_server: SET_BP unknown type " << type;
        return MCD_RET_ERR_GENERAL;
    }
    if (kind == 0) kind = 4; /* sensible default: one AArch64 instruction word */

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    if (core >= m_cores.size()) {
        SCP_WARN(()) << "mcd_server: SET_BP core id " << core << " out of range";
        return MCD_RET_ERR_GENERAL;
    }
    uint32_t inst = m_cores[core].inst;

    /* QEMU installs Z/z breakpoints per address space, not per vCPU: this arms
     * every core of the instance. `core` is recorded only for LIST_BP. */
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "Z%c,%llx,%x", digit, static_cast<unsigned long long>(addr), kind);

    std::string reply;
    if (!gdb_cmd(inst, cmd, reply)) {
        SCP_WARN(()) << "mcd_server: SET_BP forward to " << m_gdb_ports[inst] << " failed";
        return MCD_RET_ERR_GENERAL;
    }
    if (reply != "OK") {
        SCP_WARN(()) << "mcd_server: SET_BP '" << cmd << "' gdb reply '" << reply << "'";
        return MCD_RET_ERR_GENERAL;
    }
    m_breakpoints.push_back(breakpoint_t{ core, type, addr, kind });
    /* An armed breakpoint can halt a running target at any moment, stopping the
     * quantum keeper before the client's next WAIT_STOP. */
    update_debug_hold();
    SCP_INFO(()) << "mcd_server: SET_BP core=" << core << " type=" << type << " @0x" << std::hex << addr;
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_clr_bp(const std::vector<uint8_t>& req, std::vector<uint8_t>&)
{
    uint32_t core, type, kind;
    uint64_t addr;
    if (!decode_bp_req(req, core, type, addr, kind)) return MCD_RET_ERR_GENERAL;
    char digit;
    if (!bp_type_to_gdb(type, digit)) {
        SCP_WARN(()) << "mcd_server: CLR_BP unknown type " << type;
        return MCD_RET_ERR_GENERAL;
    }
    if (kind == 0) kind = 4;

    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    ensure_cores_known();

    if (core >= m_cores.size()) {
        SCP_WARN(()) << "mcd_server: CLR_BP core id " << core << " out of range";
        return MCD_RET_ERR_GENERAL;
    }
    uint32_t inst = m_cores[core].inst;

    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "z%c,%llx,%x", digit, static_cast<unsigned long long>(addr), kind);

    std::string reply;
    if (!gdb_cmd(inst, cmd, reply)) {
        SCP_WARN(()) << "mcd_server: CLR_BP forward to " << m_gdb_ports[inst] << " failed";
        return MCD_RET_ERR_GENERAL;
    }
    if (reply != "OK") {
        SCP_WARN(()) << "mcd_server: CLR_BP '" << cmd << "' gdb reply '" << reply << "'";
        return MCD_RET_ERR_GENERAL;
    }
    /* Drop the first matching record (core+type+addr). */
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end(); ++it) {
        if (it->core == core && it->type == type && it->addr == addr) {
            m_breakpoints.erase(it);
            break;
        }
    }
    /* Disarming the last breakpoint on a running target releases the hold. */
    update_debug_hold();
    SCP_INFO(()) << "mcd_server: CLR_BP core=" << core << " type=" << type << " @0x" << std::hex << addr;
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_list_bp(const std::vector<uint8_t>&, std::vector<uint8_t>& resp)
{
    /* Response: 4-byte LE count, then each breakpoint as
     * [core u32][type u32][addr u64][kind u32] (20 bytes). */
    std::lock_guard<std::mutex> lock(m_gdb_mutex);
    put_u32(resp, static_cast<uint32_t>(m_breakpoints.size()));
    for (const breakpoint_t& bp : m_breakpoints) {
        put_u32(resp, bp.core);
        put_u32(resp, bp.type);
        put_u32(resp, static_cast<uint32_t>(bp.addr & 0xffffffffu));
        put_u32(resp, static_cast<uint32_t>((bp.addr >> 32) & 0xffffffffu));
        put_u32(resp, bp.kind);
    }
    return MCD_RET_ACT_NONE;
}

mcd_return_et mcd_server::op_wait_stop(const std::vector<uint8_t>& req, std::vector<uint8_t>& resp)
{
    /* Request: [core u32][timeout_ms u32]; a short payload means core 0, 1 s. */
    uint32_t core = (req.size() >= 4) ? get_u32(&req[0]) : 0u;
    uint32_t timeout_ms = (req.size() >= 8) ? get_u32(&req[4]) : 1000u;
    /* The wait holds m_gdb_mutex, so an uncapped timeout locks out every other
     * debug operation. Clamp rather than reject; the client can wait again. */
    if (timeout_ms > MCD_MAX_WAIT_MS) {
        SCP_DEBUG(()) << "mcd_server: WAIT_STOP timeout " << timeout_ms << "ms clamped to " << MCD_MAX_WAIT_MS << "ms";
        timeout_ms = MCD_MAX_WAIT_MS;
    }
    uint64_t watch_addr = 0;
    uint32_t reason;
    {
        std::lock_guard<std::mutex> lock(m_gdb_mutex);
        ensure_cores_known();

        if (core >= m_cores.size()) {
            SCP_WARN(()) << "mcd_server: WAIT_STOP core id " << core << " out of range";
            return MCD_RET_ERR_GENERAL;
        }

        uint32_t inst = m_cores[core].inst;
        uint32_t tid = m_cores[core].tid;

        /* A stop already recorded for this core is delivered now. */
        if (m_last_stop[inst].valid && (m_last_stop[inst].tid == 0 || m_last_stop[inst].tid == tid)) {
            reason = m_last_stop[inst].reason;
            watch_addr = m_last_stop[inst].watch_addr;
            m_last_stop[inst] = last_stop_t{}; /* delivered once */
            SCP_INFO(()) << "mcd_server: WAIT_STOP core=" << core << " delivering recorded stop reason=" << reason;
        } else {
            /* The instance halts as a whole and reports on its own session, so
             * wait there and then see which core it was attributed to. */
            uint32_t stopped_tid = 0;
            reason = gdb_wait_stop(inst, timeout_ms, watch_addr, stopped_tid);

            if (reason != MCD_STOP_RUNNING && reason != MCD_STOP_HALTED && stopped_tid != 0 && stopped_tid != tid) {
                /* Another core stopped: report this one as still running. The
                 * stop stays recorded so its own core can collect it. */
                SCP_INFO(()) << "mcd_server: WAIT_STOP core=" << core << " (tid " << tid
                             << "): instance stopped on tid " << stopped_tid << " instead; held for that core";
                reason = MCD_STOP_RUNNING;
                watch_addr = 0;
            } else {
                m_last_stop[inst] = last_stop_t{}; /* consumed by this wait */
            }
        }

        /* The target may now be halted, or the session dropped; re-evaluate. */
        update_debug_hold();
    }

    /* Response: [stopped u8][reason u32][watch_addr u64] (13 bytes); stopped is 0
     * only for MCD_STOP_RUNNING. */
    resp.push_back(reason == MCD_STOP_RUNNING ? 0u : 1u);
    put_u32(resp, reason);
    put_u32(resp, static_cast<uint32_t>(watch_addr & 0xffffffffu));
    put_u32(resp, static_cast<uint32_t>((watch_addr >> 32) & 0xffffffffu));
    return MCD_RET_ACT_NONE;
}

} // namespace qbox

using qbox::mcd_server;

/* The loader looks up "module_register" by unmangled symbol name: C linkage. */
extern "C" void module_register() { GSC_MODULE_REGISTER_C(mcd_server); }
