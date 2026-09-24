/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mcd_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#endif

#ifdef _WIN32
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#define SOCK_EINTR   WSAEINTR
#define sock_errno() WSAGetLastError()
#else
typedef int socket_t;
#define INVALID_SOCK (-1)
#define CLOSE_SOCKET close
#define SOCK_EINTR   EINTR
#define sock_errno() errno
#endif

struct mcd_client_s {
    socket_t fd;
};

/* Upper bound on the length a peer may announce for a reply frame: the protocol
 * is length-prefixed, so a cap is needed to avoid reading an arbitrary frame. */
#define MCD_MAX_FRAME (1u * 1024u * 1024u)

/* Suppress SIGPIPE on write to a closed socket, which would otherwise kill the
 * process. Linux has per-call MSG_NOSIGNAL, macOS/BSD per-socket SO_NOSIGPIPE;
 * Windows has neither, and no SIGPIPE either. */
#ifdef MSG_NOSIGNAL
#define MCD_SEND_FLAGS MSG_NOSIGNAL
#else
#define MCD_SEND_FLAGS 0
#endif

/* SO_RCVTIMEO takes a DWORD of milliseconds on winsock, a struct timeval on
 * POSIX, so the option value is not portable and is wrapped here. */
#ifdef _WIN32
typedef DWORD sock_timeout_t;
typedef int sock_optlen_t;
#else
typedef struct timeval sock_timeout_t;
typedef socklen_t sock_optlen_t;
#endif

static void sock_timeout_from_ms(sock_timeout_t* t, uint32_t ms)
{
#ifdef _WIN32
    *t = (DWORD)ms;
#else
    t->tv_sec = (time_t)(ms / 1000);
    t->tv_usec = (long)(ms % 1000) * 1000;
#endif
}

static void sock_set_recv_timeout(socket_t fd, const sock_timeout_t* t)
{
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)t, (int)sizeof(*t));
}

static void put_u32_le(uint8_t* p, uint32_t x)
{
    p[0] = (uint8_t)(x & 0xff);
    p[1] = (uint8_t)((x >> 8) & 0xff);
    p[2] = (uint8_t)((x >> 16) & 0xff);
    p[3] = (uint8_t)((x >> 24) & 0xff);
}

static void put_u64_le(uint8_t* p, uint64_t x)
{
    int i;
    for (i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((x >> (8 * i)) & 0xff);
    }
}

static uint32_t get_u32_le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64_le(const uint8_t* p)
{
    uint64_t x = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        x |= (uint64_t)p[i] << (8 * i);
    }
    return x;
}

/* winsock's send/recv take a char* and an int length; every transfer here is
 * bounded by MCD_MAX_FRAME. */
static int send_all(socket_t fd, const uint8_t* buf, uint32_t len)
{
    uint32_t sent = 0;
    while (sent < len) {
        int r = (int)send(fd, (const char*)(buf + sent), (int)(len - sent), MCD_SEND_FLAGS);
        if (r < 0) {
            if (sock_errno() == SOCK_EINTR) continue;
            return -1;
        }
        sent += (uint32_t)r;
    }
    return 0;
}

static int recv_all(socket_t fd, uint8_t* buf, uint32_t len)
{
    uint32_t got = 0;
    while (got < len) {
        int r = (int)recv(fd, (char*)(buf + got), (int)(len - got), 0);
        if (r == 0) return -1; /* peer closed */
        if (r < 0) {
            if (sock_errno() == SOCK_EINTR) continue;
            return -1;
        }
        got += (uint32_t)r;
    }
    return 0;
}

/* Build [u32 LE (1+plen)][opcode][payload] and send it. */
static int send_frame(socket_t fd, uint8_t opcode, const uint8_t* payload, uint32_t plen)
{
    uint8_t hdr[5];
    put_u32_le(hdr, 1 + plen);
    hdr[4] = opcode;
    if (send_all(fd, hdr, sizeof(hdr)) != 0) return -1;
    if (plen && send_all(fd, payload, plen) != 0) return -1;
    return 0;
}

/* Discard @p len bytes so a reply we cannot store leaves the stream framed.
 * Returns 0 if all of it was drained. */
static int drain(socket_t fd, uint32_t len)
{
    uint8_t scratch[512];
    while (len) {
        uint32_t chunk = len < sizeof(scratch) ? len : (uint32_t)sizeof(scratch);
        if (recv_all(fd, scratch, chunk) != 0) return -1;
        len -= chunk;
    }
    return 0;
}

/* Read [u32 LE len][retcode][payload]; *blen gets the payload byte count. At
 * most @p cap payload bytes are stored in @p buf (NULL allowed when cap is 0);
 * a longer payload is drained and reported as an error, since callers pass
 * small fixed-size stack buffers. */
static int recv_frame(socket_t fd, uint8_t* retcode, uint8_t* buf, uint32_t cap, uint32_t* blen)
{
    uint8_t lenbuf[4];
    if (recv_all(fd, lenbuf, sizeof(lenbuf)) != 0) return -1;
    uint32_t len = get_u32_le(lenbuf);
    if (len < 1) return -1;             /* must contain at least the return code */
    if (len > MCD_MAX_FRAME) return -1; /* implausible: do not try to read it */

    if (recv_all(fd, retcode, 1) != 0) return -1;

    uint32_t plen = len - 1;
    if (plen > cap) {
        (void)drain(fd, plen);
        return -1;
    }
    if (plen && recv_all(fd, buf, plen) != 0) return -1;
    *blen = plen;
    return 0;
}

/* No library init hook, so winsock is started per connection and stopped on
 * disconnect; the calls are reference counted. */
mcd_client_t* mcd_client_connect(const char* host, uint16_t port)
{
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

#ifdef _WIN32
    {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return NULL;
    }
#endif

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || res == NULL) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

    socket_t fd = INVALID_SOCK;
    struct addrinfo* ai;
    for (ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == INVALID_SOCK) continue;
        if (connect(fd, ai->ai_addr, (int)ai->ai_addrlen) == 0) break;
        CLOSE_SOCKET(fd);
        fd = INVALID_SOCK;
    }
    freeaddrinfo(res);
    if (fd == INVALID_SOCK) {
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }

    {
        sock_timeout_t tv;
        sock_timeout_from_ms(&tv, 5000);
        sock_set_recv_timeout(fd, &tv);
    }

#ifdef SO_NOSIGPIPE
    {
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&on, (int)sizeof(on));
    }
#endif

    mcd_client_t* c = (mcd_client_t*)malloc(sizeof(*c));
    if (!c) {
        CLOSE_SOCKET(fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return NULL;
    }
    c->fd = fd;
    return c;
}

void mcd_client_disconnect(mcd_client_t* c)
{
    if (!c) return;
    if (c->fd != INVALID_SOCK) CLOSE_SOCKET(c->fd);
    free(c);
#ifdef _WIN32
    WSACleanup();
#endif
}

/* Close a connection whose framing can no longer be trusted: fd becomes
 * INVALID_SOCK and every later call fails fast, rather than parsing a
 * desynchronised stream into plausible-looking garbage replies. */
static void invalidate(mcd_client_t* c)
{
    if (c && c->fd != INVALID_SOCK) {
        CLOSE_SOCKET(c->fd);
        c->fd = INVALID_SOCK;
    }
}

/* Send a framed request and read the reply, storing at most @p cap payload bytes
 * in @p resp. Returns 0 only when the transport succeeds and the server return
 * code is MCD_RET_ACT_NONE (0). A transport or framing failure invalidates the
 * connection; a non-zero return code does not, that frame arrived intact. */
static int transact(mcd_client_t* c, uint8_t opcode, const uint8_t* payload, uint32_t plen, uint8_t* resp, uint32_t cap,
                    uint32_t* rlen)
{
    if (!c || c->fd == INVALID_SOCK) return -1;
    if (send_frame(c->fd, opcode, payload, plen) != 0) {
        invalidate(c);
        return -1;
    }

    uint8_t rc = 0;
    uint32_t blen = 0;
    if (recv_frame(c->fd, &rc, resp, cap, &blen) != 0) {
        invalidate(c);
        return -1;
    }
    if (rc != 0) return -1;
    if (rlen) *rlen = blen;
    return 0;
}

/* Scratch buffer for a query reply (4-byte count + records); the largest record
 * is 256 bytes. */
#define MCD_CLIENT_SCRATCH 65536

int mcd_client_qry_servers(mcd_client_t* c, uint32_t* num, mcd_server_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x01, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 4;
        if (off + 4 > blen) return -1;
        out[i].num_cores = get_u32_le(&resp[off]);
    }
    /* Report what was written, not what the server claimed. */
    if (num) *num = i;
    return 0;
}

int mcd_client_qry_systems(mcd_client_t* c, uint32_t* num, mcd_system_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x02, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 256;
        if (off + 256 > blen) return -1;
        memcpy(out[i].system_name, &resp[off], 256);
        /* Wire strings are fixed-width and not guaranteed terminated. */
        out[i].system_name[sizeof(out[i].system_name) - 1] = '\0';
    }
    if (num) *num = i;
    return 0;
}

int mcd_client_qry_devices(mcd_client_t* c, uint32_t* num, mcd_device_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x03, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 256;
        if (off + 256 > blen) return -1;
        memcpy(out[i].device_name, &resp[off], 256);
        out[i].device_name[sizeof(out[i].device_name) - 1] = '\0';
    }
    if (num) *num = i;
    return 0;
}

int mcd_client_qry_cores(mcd_client_t* c, uint32_t* num, mcd_core_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x04, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 8;
        if (off + 8 > blen) return -1;
        out[i].core_id = get_u32_le(&resp[off]);
        out[i].device_id = get_u32_le(&resp[off + 4]);
    }
    if (num) *num = i;
    return 0;
}

int mcd_client_qry_mem_spaces(mcd_client_t* c, uint32_t* num, mcd_mem_space_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x05, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    /* Per space: [mem_space_id u32][mem_type u32][64-byte fixed name]. */
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * (4 + 4 + 64);
        if (off + 4 + 4 + 64 > blen) return -1;
        out[i].mem_space_id = get_u32_le(&resp[off]);
        out[i].mem_type = get_u32_le(&resp[off + 4]);
        memcpy(out[i].mem_space_name, &resp[off + 8], 64);
        out[i].mem_space_name[sizeof(out[i].mem_space_name) - 1] = '\0';
    }
    if (num) *num = i;
    return 0;
}

int mcd_client_qry_state(mcd_client_t* c, uint32_t* num, mcd_core_state_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x13, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 5;
        if (off + 5 > blen) return -1;
        out[i].core_id = get_u32_le(&resp[off]);
        out[i].running = resp[off + 4];
    }
    if (num) *num = i;
    return 0;
}

/* The register description is variable length and far larger than the fixed
 * scratch buffers the other queries use, so the reply frame is read into a heap
 * buffer bounded by MCD_MAX_FRAME. That buffer is freed here; the caller owns
 * only @p groups and @p regs. */
int mcd_client_qry_regs(mcd_client_t* c, uint32_t cpu_idx, uint32_t group_id, uint32_t* num_groups,
                        mcd_reg_group_st* groups, uint32_t* num_regs, mcd_reg_info_st* regs)
{
    uint8_t req[8];
    put_u32_le(&req[0], cpu_idx);
    put_u32_le(&req[4], group_id);

    uint8_t* resp = (uint8_t*)malloc(MCD_MAX_FRAME);
    if (!resp) return -1;

    uint32_t blen = 0;
    if (transact(c, 0x32, req, sizeof(req), resp, MCD_MAX_FRAME, &blen) != 0 || blen < 4) {
        free(resp);
        return -1;
    }

    /* [n_groups u32] { [group_id u32][n_registers u32][name_len u16][name] }
     * [n_regs u32]   { [regnum u32][group_id u32][bitsize u32][reg_type u32]
     *                  [hw_thread_id u32][address u64][mem_space_id u32]
     *                  [addr_space_id u32][addr_space_type u32][name_len u16][name] }
     * The offset only advances by what has been validated against blen. */
    uint32_t gcount = get_u32_le(&resp[0]);
    uint32_t gcap = (num_groups && groups) ? *num_groups : 0;
    uint32_t off = 4;
    uint32_t g;
    for (g = 0; g < gcount; ++g) {
        uint32_t name_len;
        if (off + 10 > blen) break;
        name_len = (uint32_t)resp[off + 8] | ((uint32_t)resp[off + 9] << 8);
        if (off + 10 + name_len > blen) break;

        if (g < gcap) {
            uint32_t n = name_len < sizeof(groups[g].name) - 1 ? name_len : (uint32_t)sizeof(groups[g].name) - 1;
            groups[g].group_id = get_u32_le(&resp[off]);
            groups[g].n_registers = get_u32_le(&resp[off + 4]);
            memcpy(groups[g].name, &resp[off + 10], n);
            groups[g].name[n] = '\0';
        }
        off += 10 + name_len;
    }
    /* A short group table leaves the register count unreadable. */
    if (g < gcount || off + 4 > blen) {
        free(resp);
        return -1;
    }

    uint32_t rcount = get_u32_le(&resp[off]);
    uint32_t rcap = (num_regs && regs) ? *num_regs : 0;
    off += 4;
    uint32_t i;
    for (i = 0; i < rcount; ++i) {
        uint32_t name_len;
        if (off + 42 > blen) break;
        name_len = (uint32_t)resp[off + 40] | ((uint32_t)resp[off + 41] << 8);
        if (off + 42 + name_len > blen) break;

        if (i < rcap) {
            uint32_t n = name_len < sizeof(regs[i].name) - 1 ? name_len : (uint32_t)sizeof(regs[i].name) - 1;
            regs[i].regnum = get_u32_le(&resp[off]);
            regs[i].group_id = get_u32_le(&resp[off + 4]);
            regs[i].bitsize = get_u32_le(&resp[off + 8]);
            regs[i].reg_type = get_u32_le(&resp[off + 12]);
            regs[i].hw_thread_id = get_u32_le(&resp[off + 16]);
            regs[i].address = get_u64_le(&resp[off + 20]);
            regs[i].mem_space_id = get_u32_le(&resp[off + 28]);
            regs[i].addr_space_id = get_u32_le(&resp[off + 32]);
            regs[i].addr_space_type = get_u32_le(&resp[off + 36]);
            memcpy(regs[i].name, &resp[off + 42], n);
            regs[i].name[n] = '\0';
        }
        off += 42 + name_len;
    }
    free(resp);

    /* Report what was written, not what the server claimed. */
    if (num_groups) *num_groups = g < gcap ? g : gcap;
    if (num_regs) *num_regs = i < rcap ? i : rcap;
    return 0;
}

/* Run control returns no payload, so capacity 0: any payload the server does
 * send is a protocol error. */
int mcd_client_run(mcd_client_t* c) { return transact(c, 0x10, NULL, 0, NULL, 0, NULL); }

int mcd_client_run_core(mcd_client_t* c, uint32_t cpu_idx)
{
    uint8_t req[4];
    put_u32_le(req, cpu_idx);
    return transact(c, 0x10, req, sizeof(req), NULL, 0, NULL);
}

int mcd_client_stop(mcd_client_t* c) { return transact(c, 0x11, NULL, 0, NULL, 0, NULL); }

int mcd_client_step(mcd_client_t* c, uint32_t cpu_idx)
{
    uint8_t req[4];
    put_u32_le(req, cpu_idx);
    return transact(c, 0x12, req, sizeof(req), NULL, 0, NULL);
}

int mcd_client_reset(mcd_client_t* c) { return transact(c, 0x14, NULL, 0, NULL, 0, NULL); }

int mcd_client_read_mem(mcd_client_t* c, uint64_t addr, uint32_t len, uint32_t space_id, uint32_t addr_space_id,
                        uint8_t* buf)
{
    if (len > MCD_MAX_FRAME) return -1;
    if (len && !buf) return -1;

    uint8_t req[20];
    put_u64_le(&req[0], addr);
    put_u32_le(&req[8], len);
    put_u32_le(&req[12], space_id);
    put_u32_le(&req[16], addr_space_id);

    /* The reply payload is exactly @p len bytes, written into the caller's
     * buffer, which is therefore also the capacity bound. */
    uint32_t blen = 0;
    if (transact(c, 0x20, req, sizeof(req), buf, len, &blen) != 0) return -1;
    if (blen != len) return -1;
    return 0;
}

int mcd_client_write_mem(mcd_client_t* c, uint64_t addr, uint32_t len, uint32_t space_id, uint32_t addr_space_id,
                         const uint8_t* buf)
{
    /* Bound len so the 20 + len payload length cannot wrap. */
    if (len > MCD_MAX_FRAME) return -1;
    if (len && !buf) return -1;

    uint32_t plen = 20 + len;
    uint8_t* req = (uint8_t*)malloc(plen);
    if (!req) return -1;
    put_u64_le(&req[0], addr);
    put_u32_le(&req[8], len);
    put_u32_le(&req[12], space_id);
    put_u32_le(&req[16], addr_space_id);
    if (len) memcpy(&req[20], buf, len);

    int rc = transact(c, 0x21, req, plen, NULL, 0, NULL);
    free(req);
    return rc;
}

int mcd_client_read_reg(mcd_client_t* c, uint32_t cpu_idx, uint32_t regno, uint64_t* val)
{
    uint8_t req[8];
    put_u32_le(&req[0], cpu_idx);
    put_u32_le(&req[4], regno);

    uint8_t resp[8];
    uint32_t blen = 0;
    if (transact(c, 0x30, req, sizeof(req), resp, sizeof(resp), &blen) != 0) return -1;
    if (blen != 8) return -1;
    if (val) *val = get_u64_le(resp);
    return 0;
}

int mcd_client_write_reg(mcd_client_t* c, uint32_t cpu_idx, uint32_t regno, uint64_t val)
{
    uint8_t req[16];
    put_u32_le(&req[0], cpu_idx);
    put_u32_le(&req[4], regno);
    put_u64_le(&req[8], val);
    return transact(c, 0x31, req, sizeof(req), NULL, 0, NULL);
}

/* Encode the [cpu][type][addr][kind] request shared by SET_BP and CLR_BP. */
static void put_bp_req(uint8_t req[20], uint32_t cpu, uint32_t type, uint64_t addr, uint32_t kind)
{
    put_u32_le(&req[0], cpu);
    put_u32_le(&req[4], type);
    put_u64_le(&req[8], addr);
    put_u32_le(&req[16], kind);
}

int mcd_client_set_bp(mcd_client_t* c, uint32_t cpu_idx, uint32_t type, uint64_t addr, uint32_t kind)
{
    uint8_t req[20];
    put_bp_req(req, cpu_idx, type, addr, kind);
    return transact(c, 0x40, req, sizeof(req), NULL, 0, NULL);
}

int mcd_client_clr_bp(mcd_client_t* c, uint32_t cpu_idx, uint32_t type, uint64_t addr, uint32_t kind)
{
    uint8_t req[20];
    put_bp_req(req, cpu_idx, type, addr, kind);
    return transact(c, 0x41, req, sizeof(req), NULL, 0, NULL);
}

int mcd_client_list_bp(mcd_client_t* c, uint32_t* num, mcd_bp_st* out)
{
    uint8_t resp[MCD_CLIENT_SCRATCH];
    uint32_t blen = 0;
    if (transact(c, 0x42, NULL, 0, resp, sizeof(resp), &blen) != 0) return -1;
    if (blen < 4) return -1;

    uint32_t count = get_u32_le(&resp[0]);
    uint32_t cap = (num && out) ? *num : 0;
    uint32_t i;
    for (i = 0; i < count && i < cap; ++i) {
        uint32_t off = 4 + i * 20;
        if (off + 20 > blen) return -1;
        out[i].cpu = get_u32_le(&resp[off]);
        out[i].type = get_u32_le(&resp[off + 4]);
        out[i].addr = (uint64_t)get_u32_le(&resp[off + 8]) | ((uint64_t)get_u32_le(&resp[off + 12]) << 32);
        out[i].kind = get_u32_le(&resp[off + 16]);
    }
    if (num) *num = i;
    return 0;
}

int mcd_client_wait_stop(mcd_client_t* c, uint32_t cpu_idx, uint32_t timeout_ms, mcd_stop_st* out)
{
    if (!c || c->fd == INVALID_SOCK) return -1;

    uint8_t req[8];
    put_u32_le(&req[0], cpu_idx);
    put_u32_le(&req[4], timeout_ms);

    /* The server holds the reply for up to timeout_ms, so raise SO_RCVTIMEO
     * above it for this call and restore it afterwards. */
    sock_timeout_t saved;
    sock_timeout_t tv;
    sock_optlen_t slen = (sock_optlen_t)sizeof(saved);
    sock_timeout_from_ms(&saved, 5000);
    getsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&saved, &slen);
    /* Clamped so the +2 s margin cannot wrap. */
    sock_timeout_from_ms(&tv, (timeout_ms > 0xfffff000u) ? 0xffffffffu : timeout_ms + 2000u);
    sock_set_recv_timeout(c->fd, &tv);

    uint8_t resp[16];
    uint32_t blen = 0;
    int rc = transact(c, 0x43, req, sizeof(req), resp, sizeof(resp), &blen);

    /* transact() invalidates the fd on any transport failure. */
    if (c->fd != INVALID_SOCK) sock_set_recv_timeout(c->fd, &saved);

    if (rc != 0) return -1;
    if (blen < 13) return -1;
    if (out) {
        out->stopped = resp[0];
        out->reason = get_u32_le(&resp[1]);
        out->watch_addr = (uint64_t)get_u32_le(&resp[5]) | ((uint64_t)get_u32_le(&resp[9]) << 32);
    }
    return 0;
}
