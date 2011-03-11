/* az_pgroup_ioctl.h - Azul "I/O" definitions and access functions for pgroups.
 *
 * Copyright Azul Systems, 2010
 * Author Dan Silver <dsilver@azulsystems.com>
 *
 * Definitions herein are for the Azul "syscalls masquerading as
 * IOCTLs" part of the Azul scheduler kernel module.
 */

#ifndef _AZ_PGROUP_IOCTL_H
#define _AZ_PGROUP_IOCTL_H

#include <linux/errno.h>
#include "az_types.h"

enum process_resources {
	PROCESS_RESOURCE_CPU_COUNT       = 1
};
typedef enum process_resources process_resource_t;

#ifndef KERNEL
#define __user
#endif

struct az_rlimit {
	uint64_t rl_minimum;
	uint64_t rl_committed;
	uint64_t rl_maximum;
	uint64_t rl_cpu_weight;
};
typedef struct az_rlimit az_rlimit_t;

struct az_pgroup_member {
	az_groupid_t pm_az_gid;
	az_allocid_t pm_allocid;
};
typedef struct az_pgroup_member az_pgroup_member_t;

struct az_pgroup_create_args {
	unsigned long gid;
        void __user *data;
	size_t data_size;
};
#define IOC_AZ_PGROUP_CREATE      _IOR('z', 0x86, struct az_pgroup_create_args)

struct az_pgroup_destroy_args {
	unsigned long gid;
};
#define IOC_AZ_PGROUP_DESTROY     _IOR('z', 0x87, struct az_pgroup_destroy_args)

struct az_pgroup_get_list_args {
	unsigned long __user *gidp;
	unsigned long __user *countp;
};
#define IOC_AZ_PGROUP_GET_LIST    _IOR('z', 0x88, struct az_pgroup_get_list_args)

struct az_pgroup_set_data_args {
	unsigned long gid;
	void __user *data;
	size_t data_size;
};
#define IOC_AZ_PGROUP_SET_DATA    _IOR('z', 0x89, struct az_pgroup_set_data_args)

struct az_pgroup_get_data_args {
	unsigned long gid;
	void __user *data;
	size_t __user *data_size;
};
#define IOC_AZ_PGROUP_GET_DATA    _IOR('z', 0x8A, struct az_pgroup_get_data_args)

struct az_pgroup_set_rlimit_args {
	unsigned long gid;
	int resource;
        az_rlimit_t __user *rlimit;
};
#define IOC_AZ_PGROUP_SET_RLIMIT  _IOR('z', 0x8B, struct az_pgroup_set_rlimit_args)

struct az_pgroup_get_rlimit_args {
	unsigned long gid;
	int resource;
        az_rlimit_t __user *rlimit;
};
#define IOC_AZ_PGROUP_GET_RLIMIT  _IOR('z', 0x8C, struct az_pgroup_get_rlimit_args)

struct az_pgroup_uncommit_all_args {
	unsigned long gid;
};
#define IOC_AZ_PGROUP_UNCOMMIT_ALL _IOR('z', 0x8D, struct az_pgroup_uncommit_all_args)

struct az_process_add_args {
	unsigned long label;
	pid_t pid;
	unsigned long gid;
};
#define IOC_AZ_PROCESS_ADD        _IOR('z', 0x8E, struct az_process_add_args)

struct az_process_move_args {
	unsigned long label;
	unsigned long sgid;
	unsigned long dgid;
};
#define IOC_AZ_PROCESS_MOVE       _IOR('z', 0x8F, struct az_process_move_args)

struct az_process_get_gid_args {
	unsigned long label;
};
#define IOC_AZ_PROCESS_GET_GID    _IOR('z', 0x90, struct az_process_get_gid_args)

struct az_process_get_list_args {
        az_pgroup_member_t __user *memberp;
	unsigned long __user *countp;
};
#define IOC_AZ_PROCESS_GET_LIST   _IOR('z', 0x91, struct az_process_get_list_args)

struct az_process_kill_args {
	unsigned long label;
	int signo;
};
#define IOC_AZ_PROCESS_KILL       _IOR('z', 0x92, struct az_process_kill_args)

#endif /* _AZ_SCHED_IOCTL_H */
