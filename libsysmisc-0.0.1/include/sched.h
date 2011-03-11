// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// sched.h - Scheduler control interface to Azul DLKM

#ifndef _OS_SCHED_H_
#define _OS_SCHED_H_ 1

#include <stdint.h>
#include <stdbool.h>
#include <os/types.h>
#include <os/process_info.h>

#ifdef __cplusplus
extern "C" {
#endif

// High and lower-level scheduler control.                                                                             
// Thread scheduling
typedef uint64_t utclass_t;

typedef struct utclass_info {
    id_t     uci_class;
    uint64_t uci_fraction;
} utclass_info_t;

#define UT_CLASS_0         0ULL
#define UT_CLASS_1         1ULL
#define UT_CLASS_2         2ULL
#define UT_CLASS_3         3ULL
#define UT_CLASS_4         4ULL
#define UT_CLASS_NUM       5ULL

// User-friendly aliases
#define UT_CLASS_MUTATOR   UT_CLASS_0
#define UT_CLASS_COLLECTOR UT_CLASS_1

typedef struct tsched_config {
	int64_t		tsc_bound_score;
	int64_t		tsc_runnable_score;
	cnt_t		tsc_idle_bonus;
	cnt_t		tsc_same_cluster_bonus;	
	cnt_t		tsc_class_match_bonus;
	cnt_t		tsc_filled_cluster_bonus;
        time_interval_t tsc_runq_balancer_sleep_interval;
        time_interval_t tsc_runq_balancer_sleep_multiplier;
} tsched_config_t;

typedef struct psched_score {
	int64_t		psw_alloc_same_proc;
	int64_t		psw_alloc_diff_proc;
	int64_t		psw_remove_same_proc;
	int64_t		psw_remove_diff_proc;
	int64_t		psw_remove_avail_txu;
	int64_t		psw_remove_io_txu;
} psched_score_t;

typedef struct psched_config {
	time_interval_t	psc_sleep_interval;
	time_interval_t	psc_timeout_interval;
	uint64_t	psc_defrag_cycles;
	psched_score_t	psc_score[UT_CLASS_NUM];
	int64_t		psc_evacuate_non_allocated_score;
	int64_t		psc_evacuate_extra_score;
	uint64_t	psc_runnable_dampen_level;
	uint64_t        psc_proc_stat_active;
} psched_config_t;

typedef struct sched_config {
	psched_config_t	sc_psched;
	tsched_config_t	sc_tsched;
} sched_config_t;

typedef enum {
	RQS_INSTANT	= 0,
	RQS_DELAYED	= 1,
	RQS_HISTORIC	= 2,
	RQS_CLASS_COUNT	= 3
} runq_stat_class_t;

typedef struct runq_stat {
        cnt_t           rqs_txucnt;
        utclass_t       rqs_utclass;
        cnt_t           rqs_bound_count;
        cnt_t           rqs_runnable_count;
        int64_t         rqs_bound_delta;
        int64_t         rqs_runnable_delta;
        cnt_t           rqs_migration_from_count;
        cnt_t           rqs_migration_to_count;
        cnt_t           rqs_delta_ccount;
        cnt_t           rqs_delta_idle_ccount;
} runq_stat_t;

typedef struct sched_proc_info {
	runq_stat_class_t	stat_class;
	cnt_t			runq_stat_cnt;
	cnt_t			utclass_cnt;
	process_info_t		*pinfo;
	runq_stat_t		*runq_stat;
	utclass_info_t		*utclass;
} sched_proc_info_t;

#define	SC_PROC_RUN_THR_NUM		20
#define	SC_PROC_TXINFO_NUM		1024

typedef struct psched_utclass_txinfo {
	bool		su_inited;
	uint8_t		su_runnable_dampen_level;
	uint16_t	su_run_thr_idx;
	uint32_t	su_avg_txu_num;
	uint32_t	su_avg_run_num;
	uint64_t	su_var_txu_num;
	uint64_t	su_var_run_num;
	uint32_t	su_run_thr_num[SC_PROC_RUN_THR_NUM];
} psched_utclass_txinfo_t;

typedef struct psched_proc_txinfo {
	id_t			spt_pid;
	process_id_t		spt_label;
	psched_utclass_txinfo_t	spt_utclass[UT_CLASS_NUM];
} psched_proc_txinfo_t;

#define SC_STAT_TOT     0
#define SC_STAT_TCOLL   1
#define SC_STAT_PCOLL   2
#define SC_STAT_ALLOC   3
#define SC_STAT_REMOVE  4
#define SC_STAT_ADD     5
#define SC_STAT_DEFRAG  6
#define SC_STAT_COMM    7
#define SC_STAT_MEM     8
#define SC_STAT_ROUND   9
#define SC_STAT_NUM     10

typedef struct sys_psched_stat {
	// Counters
	uint64_t	sc_count[SC_STAT_NUM];
	// Cycles start
	uint64_t	sc_start_cycles[SC_STAT_NUM];
	// Cycles tot
	uint64_t	sc_tot_cycles[SC_STAT_NUM];
} sys_psched_stat_t;

typedef struct sched_ctl_data {
	uint64_t	arg0;
	uint64_t	arg1;
	uint64_t	arg2;
	uint64_t	arg3;
} sched_ctl_data_t;

typedef enum {
	TXU_INVALID,
	TXU_GEN,
	TXU_IO
} txu_type_t;

typedef struct txu_assign_desc {
	id_t		tad_txuid;		
	txu_type_t	tad_type;
	bool		tad_active;
	bool		tad_reserved;
	bool		tad_allocated;
	bool		tad_failed;
	process_id_t	tad_plabel;
	id_t		tad_pid;
	utclass_t	tad_utclass;
	bool		tad_suspended;
} txu_assign_desc_t;

typedef struct txu_usage_desc {
	id_t		tud_txuid;		
	txu_type_t	tud_type;
	bool		tud_active;
	uint64_t	tud_timestamp;
	uint64_t	tud_total;
	uint64_t	tud_system;
	uint64_t	tud_idle;
	uint64_t	tud_pcs;
	uint64_t	tud_tcs;
	uint64_t	tud_intrs;
	uint64_t	tud_intrs_notification;
	uint64_t	tud_intrs_migration;
	uint64_t	tud_intrs_clock;
	uint64_t	tud_intrs_resched;
	uint64_t	tud_timers;
	uint64_t	tud_syscalls;
	uint64_t	tud_exceptions;
} txu_usage_desc_t;

typedef struct txu_usage_desc_2 {
	id_t		tud_txuno;		
	id_t		tud_txuid;		
	txu_type_t	tud_type;
	bool		tud_active;
	uint64_t	tud_total;
	uint64_t	tud_system;
	uint64_t	tud_idle;
	uint64_t	tud_pcs;
	uint64_t	tud_tcs;
	uint64_t	tud_intrs;
	uint64_t	tud_intrs_notification;
	uint64_t	tud_intrs_migration;
	uint64_t	tud_intrs_clock;
	uint64_t	tud_intrs_resched;
	uint64_t	tud_timers;
	uint64_t	tud_syscalls;
	uint64_t	tud_exceptions;
} txu_usage_desc_2_t;

typedef struct {
	uint8_t ntxus_per_cluster;
	uint8_t nclusters_per_chip;
	uint8_t nchips;
	uint8_t pad0;
	uint32_t pad1;
	uint64_t count;
	txu_usage_desc_2_t tud[0];
} txu_usage_info_t;

typedef struct txu_assign_desc_2 {
	id_t		tad_txuno;		
	id_t		tad_txuid;		
	txu_type_t	tad_type;
	bool		tad_active;
	bool		tad_reserved;
	bool		tad_allocated;
	bool		tad_failed;
	process_id_t	tad_plabel;
	id_t		tad_pid;
	utclass_t	tad_utclass;
	bool		tad_suspended;
} txu_assign_desc_2_t;

typedef struct {
	uint64_t reserved0;
	uint64_t count;
	uint64_t avail_count;
	txu_assign_desc_2_t tad[0];
} txu_assign_info_t;

#define SCHED_CTL_GET_TXU_COUNT         0

#define SCHED_CTL_ADD_TXU               10
#define SCHED_CTL_REMOVE_TXU            11
#define SCHED_CTL_ADD_TXU_CLUSTER       12
#define SCHED_CTL_REMOVE_TXU_CLUSTER    13
#define SCHED_CTL_ADD_TXUS              14
#define SCHED_CTL_REMOVE_TXUS           15
#define SCHED_CTL_SET_THREAD_CLASS_INFO 16
#define SCHED_CTL_GET_THREAD_CLASS_INFO 17
#define SCHED_CTL_SET_THREAD_CLASS      18
#define SCHED_CTL_OPEN_ALL_PROC         19
#define SCHED_CTL_GET_PROC_INFO         20
#define SCHED_CTL_GET_CONFIG            21
#define SCHED_CTL_SET_CONFIG            22
#define SCHED_CTL_PSCHED_WAIT           23

#define SCHED_CTL_DEBUG_LEVEL           100

#define SCHED_CTL_ENABLE                1000
#define SCHED_CTL_DISABLE               1001

#define SCHED_CTL_MEM_TEST              2000

// Schedule control
extern sys_return_t kern_sched_ctl(int       _cmd,
                                   address_t _a0,
                                   address_t _a1,
                                   address_t _a2,
                                   address_t _a3,
                                   address_t _a4);

// Thread scheduler class
extern sys_return_t sched_set_thread_class(thread_t thread, utclass_t scheduler_class);

extern sys_return_t sched_set_class_info(process_t process, uint64_t utinfo_count, utclass_info_t *utinfo);

// Removal forthcoming ...

/* Azul scheduler interface */

#define AZ_SCHED_AZUL_POLICY    100
#define AZ_SCHED_AZUL_PRIORITY  50

extern int az_sched_setscheduler(pid_t pid, int priority);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_SCHED_H_
