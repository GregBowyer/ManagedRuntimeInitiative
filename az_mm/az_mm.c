/* az_mm.c - Azul Memory Management 
 *
 * Author Gil Tene <gil@azulsystems.com>
 * Author Madhu Chalemcherla <madhu@azulsystems.com>
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
 */

#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/rcupdate.h>
#include <linux/kallsyms.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <linux/swapops.h>
#include <linux/elf.h>

#include <linux/module.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

#include "az_mm.h"
#include "az_vmem.h"
#include "az_pmem.h"
#include "az_mm_ioctl.h"
#include "az_mm_debug.h"

#define AZUL_VERSION "1.0"
#define DISABLE_STATIC_INLINE 1

#if DISABLE_STATIC_INLINE == 1
#define static_inline
#else
#define static_inline static_inline
#endif

#ifdef CONFIG_PMEM_MODULE_PARAMS
static unsigned long az_initial_nr_pages = 0;
module_param(az_initial_nr_pages, ulong, 0);
MODULE_PARM_DESC(az_initial_nr_pages, "Number of large pages to reserve");
#endif /* CONFIG_PMEM_MODULE_PARAMS */

static struct kmem_cache *az_mm_mmstate_cachep;
static struct kmem_cache *az_mm_vmstate_cachep;
static struct kmem_cache *az_mm_mm_module_cachep;

#define az_mm_allocate_mmstate() \
	(kmem_cache_alloc(az_mm_mmstate_cachep, GFP_KERNEL))
#define az_mm_free_mmstate(mmstate) \
	(kmem_cache_free(az_mm_mmstate_cachep, (mmstate)))

#define az_mm_allocate_vmstate() \
	(kmem_cache_alloc(az_mm_vmstate_cachep, GFP_KERNEL))
#define az_mm_free_vmstate(vmstate) \
	(kmem_cache_free(az_mm_vmstate_cachep, (vmstate)))

#define az_mm_allocate_mm_module() \
	(kmem_cache_alloc(az_mm_mm_module_cachep, GFP_KERNEL))
#define az_mm_free_mm_module(module) \
	(kmem_cache_free(az_mm_mm_module_cachep, (module)))

/*
 * *****************************************************************
 * AZ_MM MM Module interface for mm_modules:
 * *****************************************************************
 */

void az_mm_exit_module_vma(struct vm_area_struct *vma);
int az_mm_exit_module_mm(struct mm_struct *mm,
		struct mm_module_struct *module);

struct mm_module_operations_struct az_mm_module_ops = {
	az_mm_handle_mm_fault,
	az_mm_change_protection,
	az_mm_copy_page_range,
	az_mm_follow_page,
	az_mm_probe_mapped,
	az_mm_unmap_page_range,
	az_mm_free_pgd_range,
	az_mm_init_module_vma,
	az_mm_exit_module_vma,
	az_mm_init_module_mm,
	az_mm_exit_module_mm
};

/*
 * *****************************************************************
 * AZ_MM PMEM Module interface for pmem_modules:
 * *****************************************************************
 */

struct pmem_module_operations_struct az_pmem_module_ops = {
	az_pmem_put_page_if_owned,
	az_pmem_get_page_if_owned,
	az_pmem_sparse_mem_map_populate
};

struct pmem_module_struct az_pmem_module = {
	&az_pmem_module_ops,
	NULL
};

/*********************************************************************/

static void az_mm_exit(void)
{
	// Destroy debugging first incase we hold live references
	az_mm_debug_destroy();
	az_pmem_exit();
}

static int __init az_mm_init(void)
{
	int ret;
	az_mm_mmstate_cachep = kmem_cache_create("az_mm_module_mmstate_struct",
			sizeof(struct az_mm_module_mmstate_struct),
			ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	az_mm_vmstate_cachep = kmem_cache_create("az_mm_module_vmstate_struct",
			sizeof(struct az_mm_module_vmstate_struct),
			ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);

	az_mm_mm_module_cachep = kmem_cache_create("mm_module_struct",
			sizeof(struct mm_module_struct),
			ARCH_MIN_MMSTRUCT_ALIGN,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
	ret = az_pmem_init();
	if (ret)
		return ret;

	/* Register pmem_module: */
	az_pmem_module.next = pmem_modules;
	pmem_modules = &az_pmem_module;
	return 0;
}

/*********************************************************************/

int az_mm_init_module_vma(struct vm_area_struct *vma,
		struct vm_area_struct *old_vma)
{
	az_vmstate *vmstate = NULL;
	az_mmstate *mms = az_mm_mmstate(vma->vm_mm);

	vmstate = az_mm_allocate_vmstate();
	if (!vmstate)
		return -ENOMEM;

	vmstate->module_mm_state = mms;
	vmstate->mm = mms->mm;
	vmstate->aliased_vma_head = NULL;
	vmstate->next_aliased_vma = NULL;
	/* Fill in the vma fields: */
	vma->mm_module_ops = &az_mm_module_ops;
	vma->mm_module_vma_state = vmstate;
	return 0;
}

void az_mm_exit_module_vma(struct vm_area_struct *vma)
{
	BUG_ON(!vma->mm_module_vma_state);
	az_mm_free_vmstate(vma->mm_module_vma_state);
}

int az_mm_init_module_mm(struct mm_struct *mm,
		struct mm_module_struct *old_module)
{
	struct mm_module_struct *module = NULL;
	az_mmstate *mms = NULL;
	int ret = 0;

	module = az_mm_allocate_mm_module();
	if (!module)
		goto err_nomem;
	mms = az_mm_allocate_mmstate();
	if (!mms)
		goto err_nomem;
	mms->mm = mm;
	if ((ret = az_pmem_init_mm(mms)))
		goto err;
	if ((ret = az_vmem_init_mm(mms)))
		goto err;
	module->mm_module_ops = &az_mm_module_ops;
	module->mm_module_mm_state = (void *)mms;
	/* Add module to mm: */
	module->next = mm->mm_modules;
	mm->mm_modules = module;
	return 0;
err_nomem:
	ret = -ENOMEM;
err:
	if (mms)
		az_mm_free_mmstate(mms);
	if (module)
		az_mm_free_mmstate(module);
	return ret;
}

int az_mm_exit_module_mm(struct mm_struct *mm,
		struct mm_module_struct *module)
{
	az_mmstate *mms = (az_mmstate *)(module->mm_module_mm_state);
	/* take module off the mm's module list: */
	if (mm->mm_modules == module) {
		mm->mm_modules = module->next;
	} else {
		struct mm_module_struct *prev;
		for (prev = mm->mm_modules;
				prev && !(prev->next == module);
				prev = prev->next);
		BUG_ON(!prev);
		prev->next = module->next;
	}
	az_vmem_exit_mm(mms);
	az_pmem_exit_mm(mms);
	az_mm_free_mmstate(mms);
	az_mm_free_mm_module(module);
	return 0;
}

int __init az_mm_module_init(void)
{
	int rv;

	printk(KERN_INFO "Azul memory management driver v%s\n",
			AZUL_VERSION);

	rv = misc_register(&azul_dev);
	if (rv)
		return rv;

	rv = az_mm_init();
	if (rv)
		misc_deregister( &azul_dev );

	az_mm_debug_init();
	return rv;
}

void __exit az_mm_module_exit(void)
{
	az_mm_exit();
	misc_deregister( &azul_dev );
}

module_init(az_mm_module_init);
module_exit(az_mm_module_exit);
#define AUTHOR "Madhu Chalemcherla, Gil Tene, Bill O. Gallmeister"
#define DESCRIPTION "Azul memory management enhancements"


MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_LICENSE("GPL");
