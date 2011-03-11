// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// process_info.h - Process subsystem for Aztek OS Services

#ifndef _OS_PROCESS_INFO_H_
#define _OS_PROCESS_INFO_H_ 1

#include <os/types.h>
#include <os/memctl.h>
#include <aznix/az_pgroup.h>

#ifdef __cplusplus
extern "C" {
#endif


/* XXX - DEPRECATED API below */
typedef enum {
        PROCESS_INERT                = 0,
        PROCESS_ACTIVE               = 1,
        PROCESS_TERMINATING          = 2,
        PROCESS_TERMINATED           = 3
} process_state_t;

typedef struct process_info {
	id_t				pi_pid;
	process_id_t			pi_label;
	id_t				pi_gid;
        process_state_t			pi_state;
        cnt_t				pi_refcnt;
        cnt_t				pi_thread_count;
        cnt_t				pi_pthread_count;
        cnt_t				pi_suspend_count;
	cnt_t				pi_txu_count;
	process_resource_limit_t	pi_proc_txu_rlimit;
	process_resource_limit_t	pi_group_txu_rlimit;
	process_memctl_t        	pi_memctl;
	uint64_t			pi_flags;
} process_info_t;


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PROCESS_INFO_H_
