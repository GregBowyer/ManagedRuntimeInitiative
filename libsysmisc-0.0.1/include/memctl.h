// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// memctl.h - Cooperative memory management

#ifndef _OS_MEMCTL_H_
#define _OS_MEMCTL_H_ 1

#include <os/types.h>

typedef struct system_memctl_limit {
	uint64_t	committed;
	uint64_t	overdraft;
	uint64_t	grant;
} system_memctl_limit_t;

typedef struct system_memctl_counter {
	uint64_t	committed;
	uint64_t	loan;
	uint64_t	grant;
} system_memctl_counter_t;

typedef struct system_memctl {
	system_memctl_limit_t	limit;
	system_memctl_counter_t	counter;
} system_memctl_t;

typedef struct process_memctl_counter {
	uint64_t	total;
	uint64_t	freeable;
} process_memctl_counter_t;

typedef struct process_memctl_limit {
	uint64_t  minimum;
	uint64_t  committed;
	uint64_t  live;
	uint64_t  maximum;
} process_memctl_limit_t;

typedef struct process_memctl {
	process_memctl_limit_t		limit;
	process_memctl_counter_t	counter;
} process_memctl_t;

/* User Memory Stats */
struct hw_usermem {
	size_t	max_kernel;	/* memory kernel can use */
	size_t	max_user;	/* memory user programs can use */
	size_t	limit_committed;
	size_t	limit_overdraft;
	size_t	limit_grant;
	size_t	used_committed;
	size_t	used_overdraft;
	size_t	used_grant;
};
typedef struct hw_usermem hw_usermem_t;

/* Kernel Memory Stats */
struct hw_kernelmem {
	size_t max_kernel;
	size_t used;
	size_t peak;
};
typedef struct hw_kernelmem hw_kernelmem_t;

/* Kernel Stack Usage Stats */
struct hw_kstack_usage {
	size_t  max_rused;
	size_t  max_aused;
	size_t  kstack_size;
	size_t  kastack_size;
};
typedef struct hw_kstack_usage hw_kstack_usage_t;
#endif // _OS_MEMCTL_H_
