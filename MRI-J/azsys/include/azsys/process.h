// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

// process.h - Process subsystem for Aztek OS Services

#ifndef _OS_PROCESS_H_
#define _OS_PROCESS_H_ 1

#include <stdbool.h>
#include <os/types.h>
#include <os/memory.h>
#include <os/memctl.h>
#include <aznix/az_pgroup.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =========================
 * Process related interface
 * =========================
 */ 
extern process_id_t process_id(process_t target);
extern sys_return_t process_self(process_t * self);
extern process_t    process_self_cached();
extern sys_return_t process_get_identifier(process_t target, process_id_t *label);
extern void         process_set_allocationid(az_allocid_t allocid);
extern az_allocid_t process_get_allocationid();

// Get the process identifier for "target".
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ADDRESS: "label" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "label" address is on a write protected page.

extern sys_return_t
process_get_cpu_statistics(process_t             target,
                           uint64_t              *total_ticks);
// A shortcut for getting statistics returned through sysagent to CPM.
//
// Params:  total_ticks  -- Lifetime number of ticks executed by all threads
//                          of the process.
//
// Error codes returned:
// SYSERR_NONE: Success.
// (Anything else indicates failure.)

extern sys_return_t 
process_get_memory_statistics(process_t             target,
                              uint64_t              *allocated_bytes,
                              uint64_t              *committed_bytes,
                              uint64_t              *default_allocated_bytes,
                              uint64_t              *default_committed_bytes,
                              uint64_t              *heap_allocated_bytes,
                              uint64_t              *heap_committed_bytes,
                              int                   *using_grant);
// A shortcut for getting statistics returned through sysagent to CPM.
//
// Params:  allocated_bytes         -- Bytes in pages allocated by this process.
//          committed_bytes         -- Bytes of committed memory for this process.
//          default_allocated_bytes -- The "C" portion of the process.
//          default_committed_bytes -- 
//          heap_allocated_bytes    -- The heap portion of the process.
//          heap_committed_bytes    -- 
//          using_grant             -- By any portion of the process.  0 or 1.
//
// Error codes returned:
// SYSERR_NONE: Success.
// (Anything else indicates failure.)

extern sys_return_t
process_get_account_info(process_t handle, account_info_t *account_info, size_t *bufsize);
//
// Purpose:  Get full memory stats information.
//
// Params:   If account_info is NULL:
//               (Output)
//                   bufsize    -- Required size in bytes of account_info buffer.
//
//           If account_info is not NULL:
//               (Input)
//                   bufsize    -- Caller-allocated size of account_info buffer.
//               (Output)
//                   account_info -- Buffer written to by kernel.
//
// Returns:  SYSERR_NONE if successful.  Something else, otherwise.

//extern const char *
//process_get_account_name(process_account_num_t account_num);
//
// Purpose:  Provide a consistent way for getting the name of an account.
//
// Params:   account_num        -- Account number to get the name for.
//
// Returns:  A pointer to a string if the account is known.
//           If the account is not known, "unknown" is returned.
//           NULL is never returned.

// process_get_privilege_token()
extern sys_return_t process_get_privilege_token(process_t          _target,
						privilege_token_t *_token);

// Returns a referenced privilege token for "target". It is the
// caller's responsibility to release this reference using
// privilege_token_deallocate().
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_INVALID_ADDRESS: "token" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "token" address is on a write protected page.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges to extract
// privilege token from "target".


// process_set_privilege_token()
extern sys_return_t process_set_privilege_token(process_t         _target,
						privilege_token_t _token);

// Set privilege token for a process.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: Either "target" or "token" handle is invalid.
// SYSERR_STALE_HANDLE: Either "target" or "token" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges to set
// privilege token for "target".


// Process Callbacks
enum process_callback_code {
	PROCESS_CB_PROTECTION_FAULT   = 0,
	PROCESS_CB_SAFE_POINT         = 1,
	PROCESS_CB_SMA_FAIL           = 2,
	PROCESS_CB_ADDRESS_FAULT      = 3,
	PROCESS_CB_RSTACK_OVERFLOW    = 4,
	PROCESS_CB_PERF0_OVERFLOW     = 5,
	PROCESS_CB_PERF1_OVERFLOW     = 6,
	PROCESS_CB_BREAK              = 7,
	PROCESS_CB_FP_UNIMPLEMENTED   = 8,
	PROCESS_CB_ILLEGAL_ADDRESS    = 9,
	PROCESS_CB_WATCH_POINT        = 10,
	PROCESS_CB_PREEMPTION         = 11,
	PROCESS_CB_TLB0_PERF_OVERFLOW = 12,
	PROCESS_CB_DEMAND_ALLOCATION  = 13,
	PROCESS_CB_DEMAND_ALLOC_SYS   = 14,
	PROCESS_CB_TLB1_PERF_OVERFLOW = 15,
	PROCESS_CB_PERF4_OVERFLOW     = 16,
	PROCESS_CB_PERF5_OVERFLOW     = 17,
	// Andy hack workaround
	// Pure software callbacks are added here in an area that is not being
	// used by this version of libos. So even if Aztek adds new callbacks
	// we will never register them.
	PROCESS_CB_ASTACK_OVERFLOW,

	// Do not renumber.  Add new ones here (at the end of the list).
	PROCESS_CB_NCALLBACKS
};

typedef enum process_callback_code process_callback_code_t;
typedef void (*process_callback_t)(uint64_t _estate, uint64_t _epc, uint64_t _eva);

// process_register_callback()
extern sys_return_t process_register_callback(process_t               _target,
					      process_callback_code_t _cbtype,
					      process_callback_t      _callback);

extern sys_return_t
process_register_callback_flags(process_t               _target,
				process_callback_code_t _cbtype,
				process_callback_t      _callback,
				uint64_t		_flags);

// Enter callback function with async callbacks already disabled
#define PROCESS_CBF_ASYNC_CALLBACK_DISABLE	0x01

// Register a callback function to handle an interrupt or
// an exception. Registration of a handler for and event
// overrides any previously registered handler for that event.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_HANDLE: "target" handle is invalid.
// SYSERR_STALE_HANDLE: "target" handle is not usable.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ADDRESS: "callback_function" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "callback_function" address is on a
// read protected page.
// SYSERR_INVALID_ARGUMENT: "callback_event" is invalid.
// SYSERR_INVALID_ARGUMENT: "flags" is invalid.


// process_lookup()
sys_return_t process_lookup(process_id_t label,
			    process_t   *process);

// Search for process with identifier "label" and on success return
// a referenced handle to it.
//
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_NOT_FOUND: A process with identifier "label" does not exist.
// SYSERR_NO_PRIVILEGE: caller does not have sufficient privileges.
// SYSERR_INVALID_ADDRESS: "process" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "process" address is on a write protected page.



// Process monitoring and management interfaces

// A process is a selectable object.
// The following object-specific status flags are defined:

#define PROC_STATUS_TERMINATED  0x1	// Terminated, now a zombie
#define PROC_STATUS_DESTROYED   0x2	// Final destruction, clear-on-delivery
#define PROC_STATUS_NO_PATHS    0x4     // All i/o paths have been lost

extern sys_return_t process_open_all(process_t *, size_t *, id_t *);
extern sys_return_t process_set_thread_quantum(process_t, int64_t _cycles);
extern sys_return_t process_get_thread_quantum(process_t, int64_t *_cycles);
extern sys_return_t process_set_tlb_config(process_t, uint64_t _config);
extern sys_return_t process_get_tlb_config(process_t, uint64_t *_config);
extern sys_return_t process_get_threads(process_t _target,
					uint64_t *_thread_count,
					thread_t *_thread_handle_array);

// Process Statistics
enum process_statistics_type {
        PROCESS_STATISTICS_DATA        = 0,
        PROCESS_STATISTICS_EXIT_STATUS = 1,
        PROCESS_STATISTICS_IO_DATA   = 2
};
typedef enum process_statistics_type process_statistics_type_t;

struct process_statistics_data {
        time_interval_t ps_utime;               /* User time used */
        time_interval_t ps_stime;               /* System time used */
        uint32_t        ps_thread_count;        /* number of threads */
        uint32_t        ps_cur_cpu_count;       /* number of CPUs currently in use */
        uint32_t        ps_alloc_cpu_count;     /* number of CPUs allocated to this process */
};
typedef struct process_statistics_data process_statistics_data_t;

struct process_statistics_exit_status {
        uint64_t ps_exit_status;
};
typedef struct process_statistics_exit_status process_statistics_exit_status_t;

struct process_statistics_io_data {
        uint64_t        rx_bytes;               /* Bytes received */
        uint64_t        rx_chan_msgs;           /* Logical messages */
        uint64_t        rx_tq_dmas;             /* DMA operations on tequila */
        uint64_t        rx_wakes;               /* Thread wakeups */
        uint64_t        tx_bytes;               /* Bytes sent */
        uint64_t        tx_chan_msgs;           /* Logical messages */
        uint64_t        tx_tq_dmas;             /* DMA operations on tequila */
        uint64_t        tx_kicks;               /* Syscalls to start write */
};
typedef struct process_statistics_io_data process_statistics_io_data_t;
#define process_statistics_g1io_data_t process_statistics_io_data_t


union process_statistics {
        process_statistics_data_t        ps_data;  /* usage data */
        process_statistics_exit_status_t ps_exit;  /* exit status */
        process_statistics_io_data_t   ps_io_data; /* i/o */
};

typedef union process_statistics process_statistics_t;


// 2.8. process_get_statistics()
extern sys_return_t process_get_statistics(process_t                 _target,
                                           process_statistics_type_t _type,
                                           process_statistics_t     *_statistics);


/*
 * =======================
 * Process group interface
 * =======================
 */
#define AZPGROUP_MAX_DATA_SIZE (16 * 1024)
#define AZPGROUP_MAX_BLOB_SIZE (16 * 1024)
#define AZ_MAX_PROCESSES 2048
#define AZ_MAX_PGROUPS 2048


// To maintain compatibility with older code
#define process_resource_limit_t         az_rlimit_t
#define prl_minimum                      rl_minimum
#define prl_committed                    rl_committed
#define prl_livepeak                     rl_livepeak
#define prl_maximum                      rl_maximum
#define pgroup_member_t                  az_pgroup_member_t
#define process_group_create             az_pgroup_create
#define process_group_destroy            az_pgroup_destroy
#define process_group_set_blob           az_pgroup_set_data
#define process_group_get_blob           az_pgroup_get_data
#define process_group_get_resource_limit az_pgroup_set_rlimit
#define process_group_set_resource_limit az_pgroup_get_rlimit
#define process_group_uncommit_all       az_pgroup_uncommit_all
#define process_group_move               az_process_move
#define process_group_get_list           az_process_get_list
static inline sys_return_t process_get_group_identifier(process_t pid, process_group_id_t *gid)
{
    int64_t ret_gid;
    sys_return_t retval;

    ret_gid = az_process_get_gid(pid);
    if (ret_gid >= 0) {
        *gid = (process_group_id_t)ret_gid;
        retval = SYSERR_NONE;
    } else {
        retval = (sys_return_t)ret_gid;
    }
    return retval;
}
static inline sys_return_t process_group_get_weight(process_group_id_t gid, cnt_t *wt)
{
    sys_return_t retval;
    az_rlimit_t reslimits;

    retval = az_pgroup_get_rlimit(gid, PROCESS_RESOURCE_CPU_COUNT, &reslimits);
    if (retval == SYSERR_NONE) {
        *wt = (cnt_t)reslimits.rl_cpu_weight;
    }
    return retval;
}
static inline sys_return_t process_group_set_weight(process_group_id_t gid, cnt_t wt)
{
    sys_return_t retval;
    az_rlimit_t reslimits;

    retval = az_pgroup_get_rlimit(gid, PROCESS_RESOURCE_CPU_COUNT, &reslimits);
    if (retval == SYSERR_NONE) {
        reslimits.rl_cpu_weight = wt;
        retval = az_pgroup_set_rlimit(gid, PROCESS_RESOURCE_CPU_COUNT, (const az_rlimit_t *)&reslimits);
    }
    return retval;
}

static inline sys_return_t process_group_remove(process_group_id_t gid, process_t pid)
{
    if (pid == 0 || gid == 0) {
        return SYSERR_INVALID_HANDLE;
    }
    return SYSERR_UNIMPLEMENTED;
}

extern sys_return_t process_group_add(process_group_id_t gid, process_t *pid, size_t size);


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PROCESS_H_
