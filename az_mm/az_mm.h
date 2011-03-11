/* az_mm.h - Azul Virtual Memory Management 
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

#ifndef _LINUX_AZ_MM_H
#define _LINUX_AZ_MM_H

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

#include "az_pmem.h"

#ifdef __KERNEL__

#ifdef CONFIG_AZMM_DEBUG_DETAIL
#define AZMM_DETAILBUG_IF(cond)  if ((cond)) 
#else
#define AZMM_DETAILBUG_IF(cond) if (0)
#endif

/* AZ_MM module interface structures: */

/*
 * az_spinlock_t created to store spinlocks in cache_line_aligned spacing
 * Assumes spinlock can fit in single L1_CACHE_BYTES unit, and asserts
 * this with BUG_ON in az_init_az_mm_spinlocks();
 */

struct az_mm_spinlock {
	spinlock_t spl;
	char __cacheline_padding[L1_CACHE_BYTES - sizeof(spinlock_t)];
};

typedef
struct az_mm_module_mmstate_struct {
	pgd_t *shadow_pgd;
	int shadow_pgd_populated;
	pgd_t *prev_main_pgd;
	atomic_long_t az_mm_nr_ptes;
	atomic_long_t nr_pgrefs_taken;
	atomic_long_t nr_pgrefs_released;
	atomic_long_t nr_p2p_taken;
	atomic_long_t nr_p2p_released;
	struct mm_struct *mm;
	struct az_pmem_pool *azm_pool;
	struct az_mm_spinlock *az_mm_ptls;
} az_mmstate;

typedef
struct az_mm_module_vmstate_struct {
	struct az_mm_module_mmstate_struct *module_mm_state; /* shortcut */
	struct mm_struct *mm;
	struct vm_area_struct *aliased_vma_head;
	struct vm_area_struct *next_aliased_vma;
	unsigned long az_mm_flags;
} az_vmstate; 

extern struct mm_module_operations_struct az_mm_module_ops;

extern int az_mm_init_module_vma(struct vm_area_struct *vma,
		struct vm_area_struct *old_vma);
extern int az_mm_init_module_mm(struct mm_struct *mm,
		struct mm_module_struct *old_module);

#define az_vma_vmstate(vma) ((az_vmstate *)((vma)->mm_module_vma_state))
#define az_vma_mmstate(vma)	\
	(((az_vmstate *)((vma)->mm_module_vma_state))->module_mm_state)

static inline az_mmstate *az_mm_mmstate(struct mm_struct *mm)
{
	struct mm_module_struct *_mod;
	for (_mod = mm->mm_modules;
	     _mod && (_mod->mm_module_ops != &az_mm_module_ops);
	     _mod = _mod->next);
	return _mod ? (az_mmstate *)(_mod->mm_module_mm_state) : NULL;
}

static inline az_mmstate *az_mm_mmstate_auto(struct mm_struct *mm)
{
	az_mmstate *mms = az_mm_mmstate(mm);
	if (!mms && az_mm_init_module_mm(mm, NULL))
			return NULL;
	mms = az_mm_mmstate(mm);
	BUG_ON(!mms);
	return mms;
}

static inline struct az_pmem_pool *az_mm_azm_pool(struct mm_struct *mm)
{
	az_mmstate *mms = az_mm_mmstate(mm);
	BUG_ON(mms && !mms->azm_pool);
	return mms ? mms->azm_pool : NULL;
}

static inline struct az_pmem_pool *az_mm_azm_pool_auto(struct mm_struct *mm)
{
	az_mmstate *mms = az_mm_mmstate_auto(mm);
	BUG_ON(mms && !mms->azm_pool);
	return mms ? mms->azm_pool : NULL;
}

#endif /* __KERNEL__ */
#endif /* _LINUX_AZ_MM_H */
