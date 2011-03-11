/* az_mm_ioctl.h - Azul "I/O" definitions and access functions
 *
 * Copyright Azul Systems, 2010
 * Author Bill Gallmeister <bog@azulsystems.com>
 *
 * Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * This code is free software; you can redistribute it and/or modify it under 
 * the terms of the GNU General Public License version 2 only, as published by 
 * the Free Software Foundation. 
 * 
 * This code is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
 * details (a copy is included in the LICENSE file that accompanied this code).
 * 
 * You should have received a copy of the GNU General Public License version 2 
 * along with this work; if not, write to the Free Software Foundation,Inc., 
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
 * CA 94043 USA, or visit www.azulsystems.com if you need additional information 
 * or have any questions.
 *
 * Definitions herein are for the Azul "syscalls masquerading as
 * IOCTLs" part of the Azul MM kernel module.
 */

#ifndef _AZ_MM_IOCTL_H
#define _AZ_MM_IOCTL_H

#include <linux/errno.h>

#ifdef __KERNEL__
struct miscdevice;
extern struct miscdevice azul_dev;

/* Implementations of our "system calls" */

extern int az_ioc_mreserve(unsigned long addr, size_t len, int flags);
extern int az_ioc_mreserve_alias(unsigned long addr,
		unsigned long existing_addr);
extern int az_ioc_munreserve(unsigned long addr, size_t len);
extern int az_ioc_mmap(unsigned long addr, size_t len,
	int prot, int acct, int flags);
extern int az_ioc_munmap(unsigned long addr, size_t len,
	int acct, int flags);
extern int az_ioc_mremap(unsigned long old_addr,
	unsigned long new_addr, size_t len, int flags);
extern int az_ioc_mprotect(unsigned long addr, size_t len,
	int prot, int flags);
extern int az_ioc_mcopy(unsigned long dst_addr,
	unsigned long src_addr, size_t len);
extern int az_ioc_mbatch_start(void);
extern int az_ioc_mbatch_remap(unsigned long old_addr,
	unsigned long new_addr, size_t len, int flags);
extern int az_ioc_mbatch_protect(unsigned long addr, size_t len,
	int prot, int flags);
extern int az_ioc_mbatch_commit(void);
extern int az_ioc_mbatch_abort(void);
extern int az_ioc_tlb_invalidate(void);
extern int az_ioc_munshatter(unsigned long addr,
	unsigned long force_addr, unsigned long resource_addr,
	int prot, int acct, int flags);
extern long az_ioc_mprobe(pid_t pid, unsigned long addr, int flags, 
		int __user* refcntp);
extern int az_ioc_mflush(int acct, int flags, size_t __user* allocatedp);

/* Physical memory submodule initializer */
extern int az_ioc_pmem_set_maximum(pid_t pid, size_t size);
extern int az_ioc_pmem_set_account_maximum(pid_t pid,
        unsigned long acct, size_t size);
extern int az_ioc_pmem_fund_account(pid_t pid,
        unsigned long acct, size_t size);
extern int az_ioc_pmem_account_transfer(pid_t pid,
        unsigned long dst, unsigned long src, size_t size);
extern int az_ioc_pmem_get_account_stats(pid_t pid,
        void __user * statsp, size_t __user * sizep);
extern int az_ioc_pmem_get_fund_stats(void __user * statsp,
        size_t __user * sizep);
extern int az_ioc_pmem_set_account_funds(pid_t pid,
        unsigned long acct, unsigned long commit, unsigned long overdraft);
extern int az_ioc_pmem_set_accounts_to_credit(pid_t pid,
        unsigned long acct, unsigned long accts_to_credit);
extern int az_ioc_pmem_fund_transfer(unsigned long dst,
        unsigned long src, size_t size);
extern int az_ioc_pmem_reserve_pages(unsigned long nr_pages);
extern int az_ioc_pmem_unreserve_pages(unsigned long nr_pages);
extern int az_ioc_pmem_reset_account_watermarks(unsigned long acct);

#endif /* __KERNEL__ */

/*
 * /dev/azul ioctls and arguments for same
 */

struct az_mreserve_args {
	unsigned long addr;
	size_t len;
	int flags;
};
#define IOC_AZ_MRESERVE	_IOR('z', 0x86, struct az_mreserve_args)

struct az_munreserve_args {
	unsigned long addr;
	size_t len;
};
#define IOC_AZ_MUNRESERVE _IOR('z', 0x87, struct az_munreserve_args)

struct az_mmap_args {
	unsigned long addr;
	size_t len;
	int prot;
	int acct;
	int flags;
};
#define IOC_AZ_MMAP _IOR('z', 0x88, struct az_mmap_args)

struct az_munmap_args {
	unsigned long addr;
	size_t len;
	int acct;
	int flags;
};
#define IOC_AZ_MUNMAP _IOR('z', 0x89, struct az_munmap_args)

struct az_mremap_args {
	unsigned long old_addr;
	unsigned long new_addr;
	size_t len;
	int flags;
};
#define IOC_AZ_MREMAP _IOR('z', 0x8b, struct az_mremap_args)

struct az_mprotect_args {
	unsigned long addr;
	size_t len;
	int prot;
	int flags;
};
#define IOC_AZ_MPROTECT _IOR('z', 0x8c, struct az_mprotect_args)

struct az_mcopy_args {
	unsigned long dst_addr;
	unsigned long src_addr;
	size_t len;
};
#define IOC_AZ_MCOPY _IOR('z', 0x8d, struct az_mcopy_args)

struct az_mbatch_remap_args {
	unsigned long old_addr;
	unsigned long new_addr;
	size_t len;
	int flags;
};
#define IOC_AZ_MBATCH_REMAP _IOR('z', 0x8e, struct az_mbatch_remap_args)

struct az_mbatch_protect_args {
	unsigned long addr;
	size_t len;
	int prot;
	int flags;
};
#define IOC_AZ_MBATCH_PROTECT _IOR('z', 0x8f, struct az_mbatch_protect_args)

struct az_munshatter_args {
	unsigned long addr;
	unsigned long force_addr;
	unsigned long resource_addr;
	int prot;
	int acct;
	int flags;
};
#define IOC_AZ_MUNSHATTER _IOR('z', 0x90, struct az_munshatter_args)

struct az_mprobe_args {
	pid_t pid;
	unsigned long addr;
	int flags;
	int __user* refcntp;
};
#define IOC_AZ_MPROBE _IOR('z', 0x91, struct az_mprobe_args)

#ifndef KERNEL
#define __user
#endif

struct az_mflush_args {
	int acct;
	int flags;
        size_t __user* allocatedp;
};

#define IOC_AZ_MFLUSH _IOR('z', 0x92, struct az_mflush_args)

#define IOC_AZ_MBATCH_START		_IO('z', 0x94)
#define IOC_AZ_MBATCH_COMMIT		_IO('z', 0x95)
#define IOC_AZ_MBATCH_ABORT		_IO('z', 0x96)
#define IOC_AZ_TLB_INVALIDATE		_IO('z', 0x97)


struct az_pmem_set_maximum_args {
    pid_t pid;
    size_t size;
};
#define IOC_AZ_PMEM_SET_MAXIMUM		_IO('z', 0x98)

struct az_pmem_set_account_maximum_args {
    pid_t pid;
    unsigned long acct;
    size_t size;
};
#define IOC_AZ_PMEM_SET_ACCOUNT_MAXIMUM		_IO('z', 0x99)

struct az_pmem_fund_account_args {
    pid_t pid;
    unsigned long acct;
    size_t size;
};
#define IOC_AZ_PMEM_FUND_ACCOUNT		_IO('z', 0x9a)

struct az_pmem_account_transfer_args {
    pid_t pid;
    unsigned long dst;
    unsigned long src;
    size_t size;
};
#define IOC_AZ_PMEM_ACCOUNT_TRANSFER		_IO('z', 0x9b)

struct az_pmem_get_account_stats_args {
    pid_t pid;
    void __user * statsp;
    size_t __user * sizep;
};
#define IOC_AZ_PMEM_GET_ACCOUNT_STATS		_IO('z', 0x9c)

struct az_pmem_get_fund_stats_args {
    void __user * statsp;
    size_t __user * sizep;
};
#define IOC_AZ_PMEM_GET_FUND_STATS		_IO('z', 0x9d)

struct az_pmem_set_account_funds_args {
    pid_t pid;
    unsigned long acct;
    unsigned long commit;
    unsigned long overdraft;
};
#define IOC_AZ_PMEM_SET_ACCOUNT_FUNDS		_IO('z', 0x9e)

struct az_pmem_set_accounts_to_credit_args {
    pid_t pid;
    unsigned long acct;
    unsigned long accts_to_credit;
};
#define IOC_AZ_PMEM_SET_ACCOUNTS_TO_CREDIT		_IO('z', 0x9f)

struct az_pmem_fund_transfer_args {
    unsigned long dst;
    unsigned long src;
    size_t size;
};
#define IOC_AZ_PMEM_FUND_TRANSFER		_IO('z', 0xa0)

struct az_pmem_reserve_pages_args {
    unsigned long nr_pages;
};
#define IOC_AZ_PMEM_RESERVE_PAGES		_IO('z', 0xa1)

struct az_pmem_unreserve_pages_args {
    unsigned long nr_pages;
};
#define IOC_AZ_PMEM_UNRESERVE_PAGES		_IO('z', 0xa2)

struct az_mreserve_alias_args {
	unsigned long addr;
	unsigned long existing_addr;
};
#define IOC_AZ_MRESERVE_ALIAS	_IOR('z', 0xa3, struct az_mreserve_alias_args)

struct az_pmem_reset_account_watermarks_args {
    unsigned long acct;
};
#define IOC_AZ_PMEM_RESET_ACCOUNT_WATERMARKS	_IO('z', 0xa4)

#endif /* _AZ_MM_IOCTL_H */
