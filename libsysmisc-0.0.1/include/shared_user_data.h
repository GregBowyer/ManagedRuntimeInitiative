/* Copyright 2006 Azul Systems, Inc. All rights reserved.
 * AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
 *
 * sud.h - Shared User Data subsystem for Aztek OS Services
 */

#ifndef _OS_SHARED_USER_DATA_H_
#define _OS_SHARED_USER_DATA_H_

#include <os/types.h>
#include <os/memory.h>

#ifdef __cplusplus
extern "C" {
#endif


/*-------------------------------------------------------------------------
 * Housekeeping
 *-----------------------------------------------------------------------*/

/* Called by launchd when starting a new process. 
 * Returns:  0 if successful.  Nonzero otherwise.
 */
extern int shared_user_data_create (az_allocid_t allocid);

/* Called by launchd when starting a new process. */
extern void shared_user_data_purge();


/*-------------------------------------------------------------------------
 * JVM configuration
 *-----------------------------------------------------------------------*/

#define SUD_JVM_CONF_REVISION 1

typedef struct {
    uint64_t revision;
    char name[64];
    char vendor[64];
    char info[64];
    char release[64];
    char internal_info[256];
    char flags[1024];
    char args[1024];
    char specification_version[8];
    char specification_name[64];
    char specification_vendor[64];
    char ext_dirs[256];
    char endorsed_dirs[256];
    char library_path[2048];
    char java_home[256];
    char classpath[4096];
    char boot_library_path[2048];
    char boot_classpath[2048];
    char command[2048];
} sud_jvm_conf_rev1_t;

extern sys_return_t
shared_user_data_get_jvm_conf_rev1 (az_allocid_t allocid, 
                                    sud_jvm_conf_rev1_t *buf);

extern sys_return_t
shared_user_data_set_jvm_conf_rev1 (az_allocid_t allocid,
                                    const sud_jvm_conf_rev1_t *buf);


/*-------------------------------------------------------------------------
 * Heap information
 *-----------------------------------------------------------------------*/

#define SUD_JVM_HEAP_REVISION 1

#define SUD_JVM_HEAP_FLAG_INLINE_CONTIG_ALLOC   0x1
#define SUD_JVM_HEAP_FLAG_TLAB_ALLOCATION       0x2

/* This is a clone of account_stats_t with the number of accounts
   hardwired to 8 (which is the current number). */
#define SUD_MAX_ACCOUNTS 8
typedef struct {
    uint64_t        ac_count;
    account_stats_t ac_array[SUD_MAX_ACCOUNTS];
} sud_account_info_t;

typedef struct {
    uint64_t revision;
    char name[16];
    uint64_t flags;
    uint64_t timestamp_ms;
    uint64_t used_bytes;
    uint64_t capacity_bytes;
    uint64_t max_capacity_bytes;
    uint64_t permanent_used_bytes;
    uint64_t permanent_capacity_bytes;
    uint64_t total_collections;
    uint64_t live_bytes;

    /* Full memory_accounting info. */
    sud_account_info_t account_info;

    /* Peak JVM heap values. */
    uint64_t peak_live_bytes;
    uint64_t peak_live_timestamp_ms;
    uint64_t peak_used_bytes;
    uint64_t peak_used_timestamp_ms;
    uint64_t peak_capacity_bytes;
    uint64_t peak_capacity_timestamp_ms;
    uint64_t peak_max_capacity_bytes;
    uint64_t peak_max_capacity_timestamp_ms;
    uint64_t peak_permanent_used_bytes;
    uint64_t peak_permanent_used_timestamp_ms;
    uint64_t peak_permanent_capacity_bytes;
    uint64_t peak_permanent_capacity_timestamp_ms;

    /* Peak memory_accounting values. */
    uint64_t peak_allocated_bytes;
    uint64_t peak_allocated_timestamp_ms;
    uint64_t peak_funded_bytes;
    uint64_t peak_funded_timestamp_ms;
    uint64_t peak_overdraft_bytes;
    uint64_t peak_overdraft_timestamp_ms;
    uint64_t peak_footprint_bytes;
    uint64_t peak_footprint_timestamp_ms;

    uint64_t peak_committed_bytes;
    uint64_t peak_committed_timestamp_ms;
    uint64_t peak_grant_bytes;
    uint64_t peak_grant_timestamp_ms;
    uint64_t peak_allocated_from_committed_bytes;
    uint64_t peak_allocated_from_committed_timestamp_ms;

    uint64_t peak_default_allocated_bytes;
    uint64_t peak_default_allocated_timestamp_ms;
    uint64_t peak_default_committed_bytes;
    uint64_t peak_default_committed_timestamp_ms;
    uint64_t peak_default_footprint_bytes;
    uint64_t peak_default_footprint_timestamp_ms;
    uint64_t peak_default_grant_bytes;
    uint64_t peak_default_grant_timestamp_ms;

    uint64_t peak_heap_allocated_bytes;
    uint64_t peak_heap_allocated_timestamp_ms;
    uint64_t peak_heap_committed_bytes;
    uint64_t peak_heap_committed_timestamp_ms;
    uint64_t peak_heap_footprint_bytes;
    uint64_t peak_heap_footprint_timestamp_ms;
    uint64_t peak_heap_grant_bytes;
    uint64_t peak_heap_grant_timestamp_ms;
} sud_jvm_heap_rev1_t;

extern sys_return_t
shared_user_data_get_jvm_heap_rev1 (az_allocid_t allocid,
                                    sud_jvm_heap_rev1_t *buf);

extern sys_return_t
shared_user_data_set_jvm_heap_rev1 (az_allocid_t allocid,
                                    const sud_jvm_heap_rev1_t *buf);


/*-------------------------------------------------------------------------
 * ARTA information
 *-----------------------------------------------------------------------*/

#define SUD_AZPROF_HTTP_REVISION 1

typedef struct {
    uint64_t revision;
    int64_t port;
    char message[256];
    char protocol[8];
    char interface[32];
    char authentication[256];
    char authorization[256];
} sud_arta_rev1_t;

extern sys_return_t
shared_user_data_get_arta_rev1 (az_allocid_t allocid,
                                sud_arta_rev1_t *buf);

extern sys_return_t
shared_user_data_set_arta_rev1 (az_allocid_t allocid,
                                const sud_arta_rev1_t *buf);


/*-------------------------------------------------------------------------
 * IO information
 *-----------------------------------------------------------------------*/

#define SUD_IO_REVISION 1

typedef struct {
    uint64_t revision;
    uint64_t rx_bytes;             // Proxy to appliance direction.
    uint64_t rx_packets;
    uint64_t tx_bytes;             // Appliance to proxy direction.
    uint64_t tx_packets;
} sud_io_rev1_t;

extern sys_return_t
shared_user_data_get_io_rev1 (az_allocid_t allocid,
                                sud_io_rev1_t *buf);

extern sys_return_t
shared_user_data_set_io_rev1 (az_allocid_t allocid,
                                const sud_io_rev1_t *buf);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _OS_SHARED_USER_DATA_H_ */
