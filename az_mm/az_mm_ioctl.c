/* az_mm_ioctl.c - Azul Memory Management Device
 *
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
 * Instantiates an azul device, to which we can perform ioctls
 * (pantomime syscalls)
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

#include "az_mm_ioctl.h"

#define IOC_CASE0(L,C) \
	case L:\
		res = az_ioc_##C(); \
break;

#define IOC_CASE(L,C,...) \
	case L:\
{ \
	struct az_##C##_args a; \
	if (copy_from_user(&a, argp, sizeof(a)) != 0)  { \
		res = -EFAULT; \
	} else { \
		res = az_ioc_##C(__VA_ARGS__); \
	}  \
} \
break;

#if defined(HAVE_UNLOCKED_IOCTL)
static int azul_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
#else
static int azul_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
#endif
{
	void __user *argp = (void __user *)arg;
	int res = -EINVAL;

	printk(KERN_INFO "Azul ioctl %08x\n", cmd);

	switch(cmd) {
		IOC_CASE(IOC_AZ_MRESERVE, mreserve, a.addr, a.len, a.flags)
		IOC_CASE(IOC_AZ_MRESERVE_ALIAS, mreserve_alias, a.addr,
				a.existing_addr)
		IOC_CASE(IOC_AZ_MUNRESERVE, munreserve, a.addr, a.len)
		IOC_CASE(IOC_AZ_MMAP, mmap, a.addr, a.len, a.prot, a.acct,
				a.flags)
		IOC_CASE(IOC_AZ_MUNMAP, munmap, a.addr, a.len, a.acct, a.flags)
		IOC_CASE(IOC_AZ_MREMAP, mremap, a.old_addr, a.new_addr, a.len,
				a.flags)
		IOC_CASE(IOC_AZ_MPROTECT, mprotect, a.addr, a.len, a.prot,
				a.flags)
		IOC_CASE(IOC_AZ_MCOPY, mcopy, a.dst_addr, a.src_addr, a.len)
		IOC_CASE(IOC_AZ_MBATCH_REMAP, mbatch_remap, a.old_addr,
				a.new_addr, a.len, a.flags)
		IOC_CASE(IOC_AZ_MBATCH_PROTECT, mbatch_protect, a.addr, a.len,
				a.prot, a.flags)
		IOC_CASE(IOC_AZ_MUNSHATTER, munshatter, a.addr, a.force_addr,
				a.resource_addr, a.prot, a.acct,a.flags)
		IOC_CASE(IOC_AZ_MPROBE, mprobe, a.pid, a.addr, a.flags, a.refcntp)
		IOC_CASE(IOC_AZ_MFLUSH, mflush, a.acct, a.flags, a.allocatedp)
		IOC_CASE0(IOC_AZ_MBATCH_START, mbatch_start)
		IOC_CASE0(IOC_AZ_MBATCH_COMMIT, mbatch_commit)
		IOC_CASE0(IOC_AZ_MBATCH_ABORT, mbatch_abort)
		IOC_CASE0(IOC_AZ_TLB_INVALIDATE, tlb_invalidate)
		IOC_CASE(IOC_AZ_PMEM_SET_MAXIMUM, pmem_set_maximum, a.pid,
				a.size)
		IOC_CASE(IOC_AZ_PMEM_SET_ACCOUNT_MAXIMUM,
				pmem_set_account_maximum, a.pid, a.acct, a.size)
		IOC_CASE(IOC_AZ_PMEM_FUND_ACCOUNT, pmem_fund_account, a.pid,
				a.acct, a.size)
		IOC_CASE(IOC_AZ_PMEM_ACCOUNT_TRANSFER, pmem_account_transfer,
				a.pid, a.dst, a.src, a.size)
		IOC_CASE(IOC_AZ_PMEM_GET_ACCOUNT_STATS, pmem_get_account_stats,
				a.pid, a.statsp, a.sizep)
		IOC_CASE(IOC_AZ_PMEM_GET_FUND_STATS, pmem_get_fund_stats,
				a.statsp, a.sizep)
		IOC_CASE(IOC_AZ_PMEM_SET_ACCOUNT_FUNDS, pmem_set_account_funds,
				a.pid, a.acct, a.commit, a.overdraft)
		IOC_CASE(IOC_AZ_PMEM_FUND_TRANSFER, pmem_fund_transfer, a.dst,
				a.src, a.size)
		IOC_CASE(IOC_AZ_PMEM_RESERVE_PAGES, pmem_reserve_pages,
				a.nr_pages)
		IOC_CASE(IOC_AZ_PMEM_UNRESERVE_PAGES, pmem_unreserve_pages,
				a.nr_pages)
		IOC_CASE(IOC_AZ_PMEM_RESET_ACCOUNT_WATERMARKS,
				pmem_reset_account_watermarks, a.acct)
		default:
		return -EINVAL;
	}

	return res;
}

const struct file_operations azul_fops = {
	.owner		= THIS_MODULE,
#if defined(HAVE_UNLOCKED_IOCTL)
	.unlocked_ioctl = azul_ioctl,
#else
	.ioctl		= azul_ioctl,
#endif
};

struct miscdevice azul_dev = {
	MISC_DYNAMIC_MINOR,
	"azul",
	&azul_fops
};

