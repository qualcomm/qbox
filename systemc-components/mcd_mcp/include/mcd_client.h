/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QBOX_MCD_CLIENT_H
#define _QBOX_MCD_CLIENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wire-compatible mirrors of the mcd_server object model (mcd_server.h). */
typedef struct {
    uint32_t num_cores;
} mcd_server_st;

typedef struct {
    char system_name[256];
} mcd_system_st;

typedef struct {
    char device_name[256];
} mcd_device_st;

typedef struct {
    uint32_t core_id;
    uint32_t device_id;
} mcd_core_st;

typedef struct {
    uint32_t mem_space_id;
    char mem_space_name[64];
} mcd_mem_space_st;

/* Values match the mcd_server MCD_BP_* wire constants (and the GDB Z/z type
 * digit). */
typedef enum {
    MCD_BP_TYPE_SW_BREAK = 0,
    MCD_BP_TYPE_HW_BREAK = 1,
    MCD_BP_TYPE_WATCH_WRITE = 2,
    MCD_BP_TYPE_WATCH_READ = 3,
    MCD_BP_TYPE_WATCH_ACCESS = 4,
} mcd_bp_type_et;

typedef struct {
    uint32_t cpu;
    uint32_t type; /* mcd_bp_type_et */
    uint64_t addr;
    uint32_t kind; /* length in bytes */
} mcd_bp_st;

/* Values match the mcd_server MCD_STOP_* wire constants. */
typedef enum {
    MCD_STOP_REASON_RUNNING = 0,
    MCD_STOP_REASON_HALTED = 1,
    MCD_STOP_REASON_BREAK = 2,
    MCD_STOP_REASON_WATCH_WRITE = 3,
    MCD_STOP_REASON_WATCH_READ = 4,
    MCD_STOP_REASON_WATCH_ACCESS = 5,
    MCD_STOP_REASON_SIGNAL = 6,
} mcd_stop_reason_et;

typedef struct {
    uint8_t stopped;     /* 0 => still running (timeout), 1 => stopped */
    uint32_t reason;     /* mcd_stop_reason_et */
    uint64_t watch_addr; /* triggering address for watchpoint reasons, else 0 */
} mcd_stop_st;

typedef struct mcd_client_s mcd_client_t;

mcd_client_t* mcd_client_connect(const char* host, uint16_t port);
void mcd_client_disconnect(mcd_client_t* c);

/* *num is in/out: on entry the capacity of *out, on return the number of records
 * written, never more than that capacity (excess records are dropped silently,
 * so the count is not the server's total). Return 0 on success, -1 on error. */
int mcd_client_qry_servers(mcd_client_t* c, uint32_t* num, mcd_server_st* out);
int mcd_client_qry_systems(mcd_client_t* c, uint32_t* num, mcd_system_st* out);
int mcd_client_qry_devices(mcd_client_t* c, uint32_t* num, mcd_device_st* out);
int mcd_client_qry_cores(mcd_client_t* c, uint32_t* num, mcd_core_st* out);
int mcd_client_qry_mem_spaces(mcd_client_t* c, uint32_t* num, mcd_mem_space_st* out);

int mcd_client_run(mcd_client_t* c);
int mcd_client_stop(mcd_client_t* c);
int mcd_client_step(mcd_client_t* c, uint32_t cpu_idx);
int mcd_client_read_mem(mcd_client_t* c, uint64_t addr, uint32_t len, uint32_t space_id, uint8_t* buf);
int mcd_client_write_mem(mcd_client_t* c, uint64_t addr, uint32_t len, uint32_t space_id, const uint8_t* buf);
int mcd_client_read_reg(mcd_client_t* c, uint32_t cpu_idx, uint32_t regno, uint64_t* val);
int mcd_client_write_reg(mcd_client_t* c, uint32_t cpu_idx, uint32_t regno, uint64_t val);

/* kind is the length in bytes (0 => server default, one AArch64 instruction
 * word). Return 0 on success, -1 on error. */
int mcd_client_set_bp(mcd_client_t* c, uint32_t cpu_idx, uint32_t type, uint64_t addr, uint32_t kind);
int mcd_client_clr_bp(mcd_client_t* c, uint32_t cpu_idx, uint32_t type, uint64_t addr, uint32_t kind);
int mcd_client_list_bp(mcd_client_t* c, uint32_t* num, mcd_bp_st* out);

/* Wait up to timeout_ms for the core to stop. out->stopped is 0 if the timeout
 * elapsed with the target still running. Return 0 on success, -1 on error. */
int mcd_client_wait_stop(mcd_client_t* c, uint32_t cpu_idx, uint32_t timeout_ms, mcd_stop_st* out);

#ifdef __cplusplus
}
#endif

#endif /* _QBOX_MCD_CLIENT_H */
