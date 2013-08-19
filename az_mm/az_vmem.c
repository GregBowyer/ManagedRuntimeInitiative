/* az_vmem.c - Azul Virtual Memory Management 
 *
 * Author Gil Tene <gil@azulsystems.com>
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
#include <linux/sched.h>

#include <linux/err.h>
#include <linux/syscalls.h>

#include <asm/pgalloc.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/percpu.h>

#include <linux/swapops.h>
#include <linux/elf.h>

#include "az_mm.h"
#include "az_vmem.h"
#include "az_pmem.h"
#include "az_mm_debug.h"

#define AZMM_DISABLE_STATIC_INLINE 1

#if AZMM_DISABLE_STATIC_INLINE == 1
#define static_inline
#else
#define static_inline static inline
#endif

/* 
 * VM_AZMM Locking and manipulation of page table hierarchy levels.
 *
 * The mainstream linux code has the luxury of assuming page table
 * hierarchy levels will not be removed or modified without holding
 * the mm->mmap_sem sempahore for write. As a result, the main concurrent
 * change it needs to protect against is the addition and allocation
 * of missing levels of the hierarchy when they are enocuntered. This is
 * easily done by the likes of pud_alloc(), pmd_alloc(), pte_alloc(),
 * and code manipulating the lowest level ptes code can safely assume that
 * the page or pages it is working within will not be removed from the
 * hierarchy as it is being worked on. USE_SPLIT_PTLOCKS further uses this
 * assumption to only lock pte work locally using a lock resident within
 * the page_t that describes the page of ptes (a lock that would not exist
 * if the pte page were to be concurrently released).
 *
 * VM_AZMM area code must, in contrast, handle concurrent hierarchy
 * transitions for two main and common cases:
 *
 * 1. Transitions of pmd entries back and forth between containing a
 * single large page mapping to referring to a page of pte mapping.
 *
 * 2. Concurrent unmapping (or the unmapping part of remap) within a
 * single vm_area's address range, including the removal of page table
 * hieratchy state via free_pte_range() [this cannot occur concurrently
 * in the mainstream linux code].
 *
 * Both of these cases present changes to the hierarchy below a pmd, and
 * must be correctly locked against.
 *
 * AZ_MM uses a striped locking mode. pmd locks are determined by the mapping
 * address, and the correct spinlock to use for a given pmd is derived from
 * az_mm_pmd_lockptr(mms, addr).
 * 
 * As a result, VM_AZMM code follows these locking rules:
 *
 * 1. Mapping data for hierarchy points above the pmd level (i.e. pud, pgd)
 * are never released without holding the mm->mmap_sem semaphore for write.
 * As a result, only pmd entries can be concurrently cleared, or change in
 * state (from large page ot small page and back). We do not need to deal
 * with pud or pgd entires being concurrently cleared, or with pmd entry
 * stoarge (pointed to by a pud entry) being concurrently removed within
 * adhereing code paths.
 *
 * 2. Releasing mapping data under a pmd is only done with the hierarchy
 * below the pmd locked via az_mm_pmd_lockptr(mm, addr).
 *
 * 3. All work done under a pmd is done with the hierarchy below the pmd
 * locked via az_mm_pmd_lockptr(mm, addr). pmd state is checked while holding
 * the lock (for e.g. az_pmd_present(), pmd_clear(), pmd_large_virtual(), etc.)
 * before any work is performed within.
 *
 * 4. No spinlocks are held during any allocations, and a single spinlock
 * is not entered recursively by VM_AZMM code.
 *
 * 5. When operating under [potentially] two separate pmds (e.g. mremap),
 * which would imply [potentially] two seprate locks, the pmd hierarchy
 * locks are taken and released in consistent order to avoid deadlocks.
 * The functions az_mm_pmd_lockptr_pair(), az_mm_spin_lock_pair(),
 * and az_mm_spin_unlock_pair() are used for consistency.
 *
 */

/*
 * Aliased reservations and Page Table aliasing:
 *
 * [Complete] Reservation address ranges may be aliased to other reservation
 * address ranges. Aliases have a specific semantic meaning, and they are *NOT*
 * the equivalent of multi-mapping a physical page at multiple addresses.
 *
 * When a set of reservations are aliased to each other, all mapping
 * operations that occur on an address range within any one of the set
 * of aliased reservations will identically affect and appear in the
 * equivalent address ranges of all the reservations in the set. This includes
 * mapping, unamapping, protection changes, remapping, shattering, and
 * unshattering, and implicitly includes all batch operations. Unreserving
 * any of a set of reservations that are aliased to each other will unreserve
 * only the specific reservation's address range, but will also have the
 * effect of unmapping (but not unreserving) the entire contents of all
 * reservations in the set.
 *
 * Aliasable reservations must be PUD_SIZE (1GB on x86-64) aligned.
 *
 * Reservation aliasing is achieved by aliasing the pud entries in address
 * ranges of the respective reservation vmas. With aliasing, the page table
 * becomes a directed graph with potentially converging nodes at the pud
 * entry level, rather than a simple hierarchical tree. pmd entry pages may
 * therefore be shared among multiple paths, and pmd_entry pages are therefor
 * ref counted accordingly when aliased pud entries are populated.
 *
 * A new aliased reservation created with do_az_mreserve_alias() will have
 * all it's pud entries populated to point to the current pmd entry pages of
 * the reservation it is aliasing. All pud entry additions within a set of
 * aliased reservations are  performed on all resevations in the set.
 *
 * Shadow page table: The shadow page table (see below) properly creates and
 * maintains a replica of the directed graph that corresponds the main page
 * table addresses in batchable reservations. As new aliaseable and batachable
 * reservations are created (which may happen during an active batch), aliased
 * pud entries are populated in both the main and shadow tables. All concurrent
 * operations that may occur during a batch_start (and thereby overlap with a
 * not yet fully populated shadow table), will properly populate aliased
 * pud entries through the use the common az_alloc_pmd() mechanism.
 *
 * Locking: AZ_MM's stripped locking is designed such that locks alias across
 * all PUD_SIZE address ranges. As a result, all equivalent address ranges in
 * aliased reservations are implicitly synchronized under the same lock, and
 * no special lock handling is needed to handle page table aliasing.
 *
 * Zapping and freeing of page table contents: Zapping (unmapping contents)
 * of page table contents in aliased resevation address ranges needs no
 * special handling. Freeing of page table contents will similarly work
 * correctly since aliased pud entries of the aliases address spaces point to
 * common pmd entry physical pages that are ref-counted, will be ref counted
 * down once for each reservation's page table free operation, until the last
 * one releases it.
 *
 * Aliased reservations track a simple aliasing relationship between all
 * reservations in a common alias group, tracking a common "root" reservation
 * for the group. The "root" reservation is useful for easily identifying the
 * pmd entry pages that pud entries in all members should alias to when
 * allocating new pmds (can happen at reservation creation, during batch
 * starts, or on concurrent population of shadow pmds by other activity
 * that may happen during batch start operations).
 * All "non-root" aliases in an alias group point their
 * vmstate->aliased_vma_head field to a common "root" reservation (the "root"
 * has it set to NULL). The resevations in a common alias group are chained
 * together in a singly-linked list using the vmstate->next_aliased_vma field.
 * The root reservation is initially the first aliasable reservation created
 * in the group (with all the rest created by aliasing it or another in the
 * group). However, the root reservation can be any member of the group,
 * and can be safely changed if needed [with mm_sema held for write] (e.g.
 * when the current root of a group is unreserved).
 */

/*
 * Shadow page table behavior, life cycle, and consistency:
 *
 * The Shadow page table is replicated from the page table on every
 * batch start, and discarded on a batch commit or abort. During a batch,
 * the shadow table maintains a consistent replica of every entry and point
 * in the page table hierarchy that corresponds to address ranges in VM_AZMM
 * vm_areas that have the AZMM_BATCHABLE flags set in vmstate->az_mm_flags.
 * The shadow table is guaranteed to NOT contain any entries that represent
 * addresses outside such "batchable" VM_AZMM vm areas.
 *
 * As VM_AZMM areas that have the AZMM_BATCHABLE flag set are required to be
 * aligned to PUD_SIZE (1GB on x86-64), all populated pud entries in the
 * shadow page table are known to be both solely representative of batchable
 * VM_AZMM adress ranges, and to hold a complete represnetation of the
 * current, up-to-date state of those addresses, modified (from the main
 * page table's state) only by az_mbatch_mremap and az_mbatch_protect
 * operations that happened within the currently open batch.
 * Since az_mbatch_mremap() and az_mbatch_protect() can only move page
 * mappings around or change their protection, but will never add or
 * remove references to physical pages, all pages currently mapped in
 * batchable VM_AZMM areas in the actual page tables have a representation
 * in the shadow table (their virtual address may be different, but they are
 * there). In addition, it is known that all physical pages tracked in the
 * shadow have a current representation in the actual page table.
 *
 * As such, an az_mbatch_commit operation simply needs to copy all batchable
 * VM_AZMM related pud entries (populated or not) from the shadow table to
 * the main table, and discard the overwritten entries (and their underlying
 * hierarchies). No unmapping of pages mapped by the discarded puds is
 * needed on a batch commit, and no unmapping of the pages mapped by the
 * shadow table is needed on a batch abort. This is true because reference
 * counts are not incremented when adding shadow tracking, and at the commit
 * point each mapped page is known to be mapped with the same ref count in
 * both the shadow and the main table.
 *
 * In order to maintain consistency between the shadow and main page tables,
 * all batchable VM_AZMM vm_area page table modification operations (called
 * from az_mmap(), az_munamp(), az_mremap(), az_mprotect(), az_munshatter(),
 * etc.) will apply changes to both the main and shadow tables if a current
 * shadow exists (if mm->shadow_pgd is not NULL). This happens even as the
 * batch is being initially populated. All creation of aliasable resevations
 * will also populate all puds associated with the aliasable resevation in
 * both the main and shadow tables.
 *
 * Shadow table population occurs without blocking regular (non batch)
 * operations from occuring, and the state changes performed by those
 * operations is correctly represnted in the shadow once it is fully
 * populated. If a concurrent operation encounters a not-yet-populated
 * portion of the shadow table, it will cooperatively populate the shadow
 * for the affected range. Whenever operating on an active shadow, consistent
 * use of shadow az_mm_lookup_shadow_pmd() makes sure than any shadow pmd
 * that may be operated on will already be up to date with the main table.
 * 
 * az_mbatch_remap() and az_mbatch_protect() operations naturally apply
 * only to the shadow table.
 *
 * Conflicts can arise if, while a batch exists which contains pending,
 * not yet committed batch_protect and batch_remap changes, operations in the
 * main page table are performed in overlapping address ranges. Similarly,
 * if an az_mbatch operation is proceeding concurrently with normal non-batch
 * manipulations of the page table, their operation may conflict. The conflict
 * testing semantics can be summerized as: "batch operations only check for 
 * conflict against the shadow table", and "main operations that occur while
 * a batch is open will test for conflict against the shadow table".
 *
 * The following conflict scenarios are tested for:
 * - az_mmap attempting to populate pages already populated in the shadow
 * - az_munmap attempting to unmap pages not mapped in the shadow
 * - az_munmap attempting to unmap pages mapped in the shadow, but are not
 *      to the same physical page as the main.
 * - az_mremap attempting to populate pages already populated in the shadow
 * - az_mremap attempting to relocate pages not mapped in the shadow
 * - az_mremap attempting to relocate pages mapped in the shadow, but are
 *      not mapped to the same physical page as the main.
 * - az_mprotect attempting to apply to pages not mapped in the shadow
 * - az_mprotect attempting to apply to pages mapped in the shadow, but are
 *      not mapped to the same physical page as the main.
 * - az_munshatter attempting to apply to pages not mapped in the shadow
 * - az_munshatter attempting to apply to pages mapped in the sahdow, but are
 *      not mapped to the same physical page as the main.
 * - az_munshatter attempting to use a resource address that is not mapped
 *      in the shadow.
 * - az_munshatter attempting to use a resource address that is mapped in
 *      the shadow but is not mapped to the same physical page as the main.
 *
 * Any operation detecting a conflict will return with an error, and it
 * is the user's reponsibility to deal with such errors (and more importantly
 * to avoid them in the first place).
 *
 * Note: An esoteric side effect of keeping shadow and main consistent is
 * that "spontaneously silent" shattering of one (of [main, shadow]) may
 * occur if an operation is performed on an address range in which the other
 * is already shattered.
 *
 * In addition, while an az_mbatch_start is in progress, other az_batch
 * operations (az_mbatch_unmap(), az_mbatch_remap(), az_mbatch_commit(),
 * az_mbatch_abort()) should not be attempted until the az_mbatch_start()
 * operation complete. If such operations are attepted, they may exit with
 * an error, or may block until the bmatch_start() completes.
 */

/*
 * Large page behavior:
 *
 * AZ_MM supports the use of large page mappings for PMD_SIZE contiguous
 * virtual and physical addresses. Large virtual mappings are represented
 * by pmd_large_virtual() pmd entries in the hierarchy, and are created
 * by mapping requests that specify AZMM_LARGE_PAGE_MAPPINGS flags. Such
 * require PMD_SIZE aligned addresses and lengths.
 *
 * Operations that modify existing mappings (such as az_mprotect(),
 * az_munamp(), az_mremap(), and az_mbatch_remap()) and attempt to
 * operate on large page mappings must either use PMD_SIZE aligned
 * addresses and lengths, or, if they wish to operate on smaller than
 * aligned PMD_SIZED (i.e. aligned only to PAGE_SIZE), they must specify
 * AZMM_MAY_SHATTER_LARGE_MAPPINGS. Using AZMM_MAY_SHATTER_LARGE_MAPPINGS
 * may cause the large mappings to be virtually shattered into a set of
 * small mappings.
 *
 * Note that shattering a large mapping into small ones does not shatter
 * the physical page asscoiated, and as long as one small mapping exists
 * to a portion of a large physical page, the page has not been released.
 * This should be taken into account by unmap and remap operations that
 * expect to release certain memory resources.
 *
 * Shattering large mappings and Unshattering small ones into large:
 *
 * Large virtual mappings can be virtually shattered into a set of small
 * virtual mappings that map to small physical page portions of a common
 * large physical page. Such virtually shattered large mappings have
 * pmd entries that refer to regular page of pte entries. Inidividual pte
 * mappings under a virtually shattered large mapping will refer to inidvidual 
 * small pages that are part of a common compound large physical page.
 *
 * To maintain consistent reference counting behavior, each large virtual
 * mappings is counted as PTRS_PER_PMD refs to the large physical page.
 * As a large virtual mapping is shattered, the inidividual ptes within
 * it count as one ref each, which means that the actual ref count on
 * the physical page does not change during a shatter operation.
 *
 * Individual ptes within a shattered large virtual range can be safely
 * remapped without changing ref counts. 
 *
 * Finally, a contiguous virtual range of PMD_SIZE addresses that is spread
 * across discontiguous physical pages can be "unshattered" into a single
 * large virtual mapping by physically relocating the contents of it's
 * individual mappings into appropriate offsets within a contiguous large
 * physical page.
 * This is done using two explicit phase operations:
 * 1. An az_unshatter_pmd_start() call will establish a new large physical
 * page for the associated mapping, setting up backing-store mappings for
 * each of it's small page portions: (struct page *)(page->mapping) of each
 * small page within the large physical page will be set to point to the
 * current physical pages associated with each small page mapping within
 * the PMD. The small page mappings will then be changed to refer to the
 * approriate portion of the large physical page, with each small mapping
 * set to fault on any attempted access (by clearing the present bit),
 * and their protection will be set to that of the eventual large page
 * mapping that is intended.
 * - After an az_unshatter_pmd_start() call, faults on "unshatterring" small
 * mappings will use az_unshatter_copy_into_pte() to on-demand copy the
 * physical contents from the original physical page into the mapped target
 * small page portion, remove the trapping cause from the associated small
 * mapping, and remove the reference to the original physical page.
 * 2. An az_unshatter_pmd_finish() call will complete the unshattering
 * by completeing the copying process of any not-yet-copied small mapping
 * within the range. Once the unshattering is complete, the small
 * mappings will be replaced with large virtual mappings.
 */ 

/*
 * A note about pte_present(), PROT_NONE, and variants for pmd_ etc.
 *
 * The Linux kernel's normal behavior for PROT_NONE mappings is "strange"
 * (in the counterintuitive sense). The pte_present() test will return
 * true for PROT_NONE mappings even if the pte in question does not have
 * the _PRESENT bit set. Similarly, pte entries for mappings with protections
 * other than PROT_NONE are generated with the _PRESENT bit set, while
 * PROT_NONE pte entries are generated with it cleared.
 * As a result, the pte_present() test is not a valid way of determining
 * whether a pte entry actually is present or not. Since AZ_MM certainly
 * does have cases of PROT_NONE mappings which may or may  not be present
 * (p2p mappings for PROT_NONE contents being a good example), we cannot
 * use that test conveniently, and using it accidentally can lead to
 * unintutive and hard to find bugs.
 *
 * To Avoid this issue, and to allow for a consistent discipline and intuitive
 * code without a need to understand the "special" nature of pte_present(),
 * ALL AZ_MM code uses ONLY az_ variants for determining the _none or _present
 * status of pmd and pte entries. These include az_pte_present(),
 * az_pte_none(), az_pmd_present(), and az_pmd_none().  No direct use of
 * the kernel's normal variants should exist in the code, and this fact
 * should be periodically verfied during code reviews.
 *
 * In addition, we make sure to enforce our convention of _PRESENT being
 * set for any mapping that is not _none() and not a p2p mapping (including
 * pages mapped with PROT_NONE). To do this, we make sure that at any place
 * set_pte_at() is used on a pte (not a casted pmd), and the entry being
 * deposited was derived from a protection value (not just copied from another
 * pte), we use something along the line of "entry = az_pte_mkpresent(entry);"
 * to force the _PRESENT bit on.
 */
/***********************************************************************/

static void az_mm_terminate_mbatch(struct mmu_gather *tlb);
int do_az_mprotect(unsigned long addr, size_t len, int prot,
		int flags, int shadow_only);
int az_populate_shadow_pmd(az_mmstate *mms,
		pmd_t *main_pmd, pmd_t *shadow_pmd,
		unsigned long addr, spinlock_t *ptl, pgtable_t *new_page);
pteval_t az_mm_probe_addr(struct mm_struct *mm, unsigned long addr, 
		int *refcnt);

/***********************************************************************/

int az_mm_change_protection(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, unsigned long newflags)
{
	int prot = 0;
	prot |= (newflags | VM_READ) ? PROT_READ : 0;
	prot |= (newflags | VM_WRITE) ? PROT_WRITE : 0;
	prot |= (newflags | VM_EXEC) ?  PROT_EXEC : 0;
	return do_az_mprotect(start, end - start, prot,
			(AZMM_MAY_SHATTER_LARGE_MAPPINGS | AZMM_BLIND_PROTECT),
			false /* shadow_only */);
}

int az_mm_copy_page_range(struct mm_struct *dst_mm,
		struct mm_struct *src_mm, struct vm_area_struct *vma)
{
	return 0;
}

int az_mm_probe_mapped(struct vm_area_struct *vma, unsigned long start,
		unsigned long *end_range, unsigned long *range_vm_flags)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long az_start;
	pteval_t range_prot, probe_prot;
	int refcnt;

	range_prot = az_mm_probe_addr(mm, start, &refcnt) & 
		AZ_MM_PTE_PROTSCAN_MASK;
	range_prot |= (range_prot & ~(_PAGE_PSE)) ? _PAGE_PRESENT: 0;
	az_start = start;
	do {
		az_start += ((range_prot & _PAGE_PSE) &&
			     !(az_start & ~PMD_MASK)) ?  PMD_SIZE : PAGE_SIZE;
		probe_prot = az_mm_probe_addr(mm, az_start, &refcnt) &
			AZ_MM_PTE_PROTSCAN_MASK;
		probe_prot |= (probe_prot & ~(_PAGE_PSE)) ? _PAGE_PRESENT: 0;
		if (need_resched())
			cond_resched();
	} while ((probe_prot == range_prot) && (az_start < vma->vm_end));

	if (range_vm_flags)
		*range_vm_flags = az_mm_pteval_to_vm_flags(range_prot);
	if (end_range)
		*end_range = (az_start <= vma->vm_end) ? az_start : vma->vm_end;
	return (range_prot & _PAGE_PRESENT);
}

#define AZ_MM_PAGE_TYPES_BASE 0
enum az_mm_page_types {
	AZ_MM_SMALL_PAGE = AZ_MM_PAGE_TYPES_BASE,
	AZ_MM_LARGE_PAGE,
	AZ_MM_PTE_PAGE,
	AZ_MM_PMD_PAGE,
	AZ_MM_PUD_PAGE,
	AZ_MM_PGD_PAGE,
	AZ_MM_NR_PAGE_TYPES
};

/*
 * *****************************************************************
 * az_mm init and exit
 * *****************************************************************
 */

int az_mm_init_spinlocks(az_mmstate *mms);

int az_vmem_init_mm(az_mmstate *mms)
{
	mms->shadow_pgd = NULL;
	mms->shadow_pgd_populated = false;
	mms->prev_main_pgd = NULL;
	mms->az_mm_ptls = NULL;
	atomic_long_set(&mms->az_mm_nr_ptes, 0);
	atomic_long_set(&mms->nr_pgrefs_taken, 0);
	atomic_long_set(&mms->nr_pgrefs_released, 0);
	atomic_long_set(&mms->nr_p2p_taken, 0);
	atomic_long_set(&mms->nr_p2p_released, 0);
	return 0;
}

void az_vmem_exit_mm(az_mmstate *mms)
{ 
	long nr_ptes = atomic_long_read(&mms->az_mm_nr_ptes);
	long nr_pgrefs_taken = atomic_long_read(&mms->nr_pgrefs_taken);
	long nr_pgrefs_released = atomic_long_read(&mms->nr_pgrefs_released);
	long nr_p2p_taken = atomic_long_read(&mms->nr_p2p_taken);
	long nr_p2p_released = atomic_long_read(&mms->nr_p2p_released);
	long nr_pgrefs = nr_pgrefs_taken - nr_pgrefs_released;
	long nr_p2p = nr_p2p_taken - nr_p2p_released;
	if (mms->shadow_pgd || mms->prev_main_pgd) {
		struct mmu_gather tlb;
		tlb_gather_mmu(&tlb, current->mm, true);
		az_mm_terminate_mbatch(&tlb);
		tlb_finish_mmu(&tlb, 0, 0);
	}

	if (nr_ptes || nr_pgrefs || nr_p2p) {
		printk(KERN_CRIT
		"*** AZMM leak; nr_ptes: %ld nr_pgrefs: %ld nr_p2p: %ld ***\n",
				nr_ptes, nr_pgrefs, nr_p2p);
		printk(KERN_CRIT
		"         nr_pgrefs_taken: %ld nr_pgref_released: %ld\n",
				nr_pgrefs_taken, nr_pgrefs_released);
		printk(KERN_CRIT
		"         nr_p2p_taken: %ld nr_p2p_released: %ld\n",
				nr_p2p_taken, nr_p2p_released);
	}
	BUG_ON(nr_ptes);
	if (mms->az_mm_ptls)
		az_pmem_put_page(virt_to_page(mms->az_mm_ptls), 0);
}

/* az_mm_init_spinlocks() shoudl be called to on-demand init az_mm_ptls.
 * It cannot be called during az_vmem_init_mm(), because funding for the
 * physical pages is not yet available at that time.
 */
int az_mm_init_spinlocks(az_mmstate *mms)
{
	long ptl_idx;
	spinlock_t *pmdl;
	unsigned long addr;
	struct page *page;

	if (mms->az_mm_ptls)
		return 0;

	/* macros all assume spinlock_t can fit in one cache line */
	BUG_ON(sizeof(spinlock_t) > L1_CACHE_BYTES);
	BUG_ON(sizeof(struct az_mm_spinlock) != L1_CACHE_BYTES);

	/* Allocate locks page from azm_pool memory: */
	page = __az_pmem_alloc_subpage(mms->azm_pool, 0, 0);
	if (!page)
		return -ENOMEM;
	mms->az_mm_ptls = (struct az_mm_spinlock *) page_address(page);


	for (ptl_idx = 0; ptl_idx < AZ_MM_NPTLS_PER_MM; ptl_idx++) {
		addr = ptl_idx << AZ_MM_PTL_STRIPE_SHIFT; 
		pmdl = az_mm_pmd_lockptr(mms, addr);
		spin_lock_init(pmdl);
	}
	return 0;
}

/*
 * *****************************************************************
 * az_mm page allocation and free
 * *****************************************************************
 */

static_inline
struct page * az_alloc_page(az_mmstate *mms, enum az_mm_page_types t,
                int acct, int pflags)
{
	struct page *page = NULL;

	BUG_ON(!mms->azm_pool);

	if (t == AZ_MM_LARGE_PAGE) {
		page = __az_pmem_alloc_page(mms->azm_pool, acct, pflags);
		if (page)
			atomic_long_inc(&mms->nr_pgrefs_taken);
		goto out;
	} 

	if (t == AZ_MM_SMALL_PAGE) {
		page = __az_pmem_alloc_subpage(mms->azm_pool, acct, pflags);
		if (page)
			atomic_long_inc(&mms->nr_pgrefs_taken);
		goto out;
	}

	BUG_ON((t != AZ_MM_PTE_PAGE) &&
	       (t != AZ_MM_PMD_PAGE) &&
	       (t != AZ_MM_PUD_PAGE) &&
	       (t != AZ_MM_PGD_PAGE));

	/* __az_pmem_alloc_subpage zeros the page by default */
	page = __az_pmem_alloc_subpage(mms->azm_pool, acct, pflags);
	if (page && (t == AZ_MM_PTE_PAGE)) {
		pgtable_page_ctor(page);
		atomic_long_inc(&mms->az_mm_nr_ptes);
		atomic_long_inc(&mms->nr_pgrefs_taken);
	}
	/* NOTE: PMD, PUD, and PGD pages do not track with nrpgrefs_taken,
	 * because they are released via generic pmd_free_tlb()
	 * pud_free_tlb(), and free_page, all of which go through the
	 * pmem layer, but will not be seen by the vmem layer.
	 */
out:
	BUG_ON(page && (!pfn_valid(page_to_pfn(page)) || !page_to_pfn(page)));
	BUG_ON(page && page->mapping !=
	       (struct address_space *) AZ_PAGE_MAPPING_POISON);
	return page;
}

static_inline
void az_finish_mmu(az_mmstate *mms, int tlb_invalidate)
{
	if (tlb_invalidate)
		az_pmem_flush_tlb_and_released_pages(mms);
}

static_inline
void az_remove_page(az_mmstate *mms, struct page *page,
        int flags)
{
	BUG_ON(!az_pmem_managed_page(page));
	atomic_long_inc(&mms->nr_pgrefs_released);
	az_pmem_put_page(page, flags);
}

/*
 * *****************************************************************
 * az_mm pte allocation/free
 * *****************************************************************
 */

static_inline
void az_pte_free(az_mmstate *mms, struct page *pte, int flags)
{
	BUG_ON(!atomic_long_read(&mms->az_mm_nr_ptes));

	atomic_long_dec(&mms->az_mm_nr_ptes);
	pgtable_page_dtor(pte);
	paravirt_release_pte(page_to_pfn(pte));
	pte->mapping = (struct address_space *) AZ_PAGE_MAPPING_POISON;
	az_remove_page(mms, pte, flags);
}

static inline
void az_pte_free_never_used(az_mmstate *mms, struct page *pte)
{
	az_pte_free(mms, pte, AZ_PMEM_TLB_INVALIDATED);
}

/* Just because the pmd is present doesn't mean anything is
 * there. If this pmd is full of az_pte_none() entries, clear it.  */
static_inline
int az_pmd_none_or_clear_empty(struct vm_area_struct *vma, unsigned long addr,
        pmd_t *pmd, int tlb_invalidate)
{
	int pte_mapping_exists = false; 
	unsigned long eaddr = addr;
	az_mmstate *mms = az_vma_mmstate(vma);
	struct mm_struct *mm = vma->vm_mm;
	pgtable_t pte_page;

	if (!az_pmd_present(*pmd))
		return 1;
	if (pmd_large_virtual(*pmd))
		return 0;
	do {
		pte_t *pte = az_pte_offset(pmd, eaddr);
		pte_mapping_exists |= !az_pte_none(*pte);
	} while (eaddr += PAGE_SIZE,
			(!pte_mapping_exists && eaddr < addr + PMD_SIZE));

	if (pte_mapping_exists)
		return 0;

	/* clear the pmd, free the pte page */
	pte_page = pmd_pgtable(*pmd);
	if (tlb_invalidate)
		ptep_clear_flush(vma, addr, (pte_t *)pmd);
	else
		pte_clear(mm, addr, (pte_t *)pmd);

	az_pte_free(mms, pte_page, 0);
	return 1;
}

/* 
 * az_pte_alloc():
 * populate pmd and [potentiallly] shadow pmd at the same time.
 */
static_inline
int az_pte_alloc(az_mmstate *mms, pmd_t *pmd,
                pmd_t *shadow_pmd, unsigned long address)
{
	struct mm_struct *mm = mms->mm;
	spinlock_t *ptl;
	pgtable_t new;
	pgtable_t new_shadow = NULL;
	int ret = -EFAULT;

	if (!shadow_pmd && az_pmd_present(*pmd))
		return 0;
	if (az_pmd_present(*pmd) && shadow_pmd && az_pmd_present(*shadow_pmd))
		return 0;

	new = az_alloc_page(mms, AZ_MM_PTE_PAGE, 0, 0);
	if (!new)
		goto err_nomem;
	if (shadow_pmd) {
		new_shadow = az_alloc_page(mms, AZ_MM_PTE_PAGE, 0, 0);
		if (!new_shadow)
			goto err_nomem;
	}
	smp_wmb();

	ptl = az_mm_pmd_lockptr(mms, address);
	spin_lock(ptl);

	/* Check for stupid user tricks: */
	if (pmd_large_virtual(*pmd) ||
			(shadow_pmd && pmd_large_virtual(*shadow_pmd)))
		goto err_fault_unlock;
	if (!az_pmd_present(*pmd) &&
			shadow_pmd && az_pmd_present(*shadow_pmd))
		goto err_fault_unlock;
	if (az_pmd_present(*pmd) &&
			shadow_pmd && !az_pmd_present(*shadow_pmd))
		goto err_fault_unlock;

	if (!az_pmd_present(*pmd)) {
		pmd_populate(mm, pmd, new);
		new = NULL;
	}
	if (shadow_pmd && !az_pmd_present(*shadow_pmd)) {
		pmd_populate(mm, shadow_pmd, new_shadow);
		new_shadow = NULL;
	}
	ret = 0;

	spin_unlock(ptl);
out:
	if (new)
		az_pte_free_never_used(mms, new);
	if (new_shadow)
		az_pte_free_never_used(mms, new_shadow);
	return ret;
err_nomem:
	ret = -ENOMEM;
	goto out;
err_fault_unlock:
	ret = -EFAULT;
	spin_unlock(ptl);
	goto out;
}

/*
 * *****************************************************************
 * az_mm pmd allocation, lookup and pud populate support
 * *****************************************************************
 */

static_inline
int az_populate_aliased_pud(struct vm_area_struct *vma, pud_t *pud,
		pgd_t *pgd_root, unsigned long addr)
{
	pgd_t *aliased_pgd;
	pud_t *aliased_pud;
	pmd_t *aliased_pmd;
	struct page *aliased_pmd_page;
	unsigned long aliased_addr;
	struct mm_struct *mm = vma->vm_mm;
	az_vmstate *vmstate = az_vma_vmstate(vma);

	if (pud_present(*pud))	/* Already populated */
		return 0;

	BUG_ON(!vmstate);
	BUG_ON(!vmstate->aliased_vma_head);
	BUG_ON(!az_mm_aliasable_vma(vmstate->aliased_vma_head));

	aliased_addr = (addr  - vma->vm_start) +
		vmstate->aliased_vma_head->vm_start;

	aliased_pgd = az_pgd_offset(pgd_root, aliased_addr);

	aliased_pud = pud_alloc(mm, aliased_pgd, aliased_addr);
	if (unlikely(!aliased_pud))
		return -ENOMEM;

	aliased_pmd = pmd_alloc(mm, aliased_pud, aliased_addr);
	if (unlikely(!aliased_pmd))
		return -ENOMEM;

	aliased_pmd_page = virt_to_page(aliased_pmd);

	/* aliased_pmd_page now points into an allocated and populated page
	 * of pmd entries, referenced by aliased_pud. We can now safely
	 * populate the pud with the aliased page.
	 * Since this can be done in concurrent paths (e.g. shadow being
	 * populated concurrently with a map or remap that affects it),
	 * verify (under lock) that pud is not yet populated before
	 * populating and incrementing the page's ref count. */
	spin_lock(&mm->page_table_lock);
	if (!pud_present(*pud)) {
		get_page(aliased_pmd_page);
		pud_populate(mm, pud, (pmd_t *)page_address(aliased_pmd_page));
	}
	spin_unlock(&mm->page_table_lock);

	return 0;
}

static_inline
void az_pmd_free(az_mmstate *mms, pmd_t *pmd, int flags)
{
	BUG_ON((unsigned long)pmd & (PAGE_SIZE-1));
	BUG_ON(!az_pmem_managed_page(virt_to_page(pmd)));
	az_pmem_put_page(virt_to_page(pmd), flags);
}

static_inline
void az_pud_free(az_mmstate *mms, pud_t *pud, int flags)
{
	BUG_ON((unsigned long)pud & (PAGE_SIZE-1));
	BUG_ON(!az_pmem_managed_page(virt_to_page(pud)));
	az_pmem_put_page(virt_to_page(pud), flags);
}

int __az_pud_alloc(struct vm_area_struct *vma, pgd_t *pgd1,
                pgd_t *pgd2, unsigned long addr)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	struct mm_struct *mm = vma->vm_mm;
	pud_t *new1 = NULL;
	pud_t *new2 = NULL;
	unsigned long new1_pa = 0, new2_pa = 0;
	int ret = 0;
	struct page *page;

	if (pgd_none(*pgd1)) {
		page = az_alloc_page(mms, AZ_MM_PUD_PAGE, 0, 0);
		if (!page)
			goto err;
		new1 = page_address(page);
	}
	if (pgd2 && pgd_none(*pgd2)) {
		page = az_alloc_page(mms, AZ_MM_PUD_PAGE, 0, 0);
		if (!page)
			goto err;
		new2 = page_address(page);
	}
	smp_wmb();

	spin_lock(&mm->page_table_lock);
	if (!pgd_present(*pgd1)) {
		pgd_populate(mm, pgd1, new1);
		new1_pa = __pa(new1);
		new1 = NULL;
	}
	if (pgd2 && !pgd_present(*pgd2)) {
		pgd_populate(mm, pgd2, new2);
		new2_pa = __pa(new2);
		new2 = NULL;
	}
	spin_unlock(&mm->page_table_lock);
out:
	if (new1)
		az_pud_free(mms, new1, 0);
	if (new2)
		az_pud_free(mms, new2, 0);
	AZMM_DETAILBUG_IF(!ret && (!pgd_present(*pgd1) ||
				!pfn_valid(az_pgd_pfn(*pgd1)))) {
		pgd_clear_bad(pgd1);
		printk(KERN_DEBUG "AZMM: __az_pud_alloc(): new1_pa: %016lx\n",
				new1_pa);
		BUG();
	}
	return ret;
err:
	ret = -ENOMEM;
	goto out;
}

static inline pmd_t *az_alloc_pmd(struct vm_area_struct *vma,
		pgd_t *pgd_root1, pgd_t *pgd_root2, unsigned long addr);

int __az_pmd_alloc(struct vm_area_struct *vma, pud_t *pud1,
                pud_t *pud2, pgd_t *pgd_root1, pgd_t *pgd_root2,
		unsigned long addr)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	struct mm_struct *mm = vma->vm_mm;
	az_vmstate *vmstate = az_vma_vmstate(vma);
	pmd_t *new1 = NULL;
	pmd_t *new2 = NULL;
	unsigned long new1_pa = 0, new2_pa = 0;
	int ret = 0;
	struct page *page;

	if (vmstate->aliased_vma_head) {
		/* vma is aliased to another. populate puds to
		 * point to the pmd entry page of the vma that it
		 * is aliased to. */
		if (az_populate_aliased_pud(vma, pud1, pgd_root1, addr))
			return -ENOMEM;
		if (pgd_root2 &&
		    az_populate_aliased_pud(vma, pud2, pgd_root2, addr))
			return -ENOMEM;
		goto out;
	}

	if (pud_none(*pud1)) {
		page = az_alloc_page(mms, AZ_MM_PMD_PAGE, 0, 0);
		if (!page)
			goto err;
		new1 = (pmd_t *) page_address(page);
	}
	if (pud2 && pud_none(*pud2)) {
		page = az_alloc_page(mms, AZ_MM_PMD_PAGE, 0, 0);
		if (!page)
			goto err;
		new2 = (pmd_t *) page_address(page);
	}
	smp_wmb();

	spin_lock(&mm->page_table_lock);
	if (!pud_present(*pud1)) {
		pud_populate(mm, pud1, new1);
		new1_pa = __pa(new1);
		new1 = NULL;
	}
	if (pud2 && !pud_present(*pud2)) {
		pud_populate(mm, pud2, new2);
		new2_pa = __pa(new2);
		new2 = NULL;
	}
	spin_unlock(&mm->page_table_lock);

	if (vmstate->next_aliased_vma) {
		/* This is the root vma of an aliased vma grou (with at
		 * least one more member). Go through all members of the
		 * alias group and make sure their coresponding pud entries
		 * are correctly populated. They will all end up aliasing
		 * to this vma's pud entry. */
		struct vm_area_struct *alias_vma = vmstate->next_aliased_vma;
		while (alias_vma) {
			pmd_t *alias_pmd = az_alloc_pmd(alias_vma,
					pgd_root1, pgd_root2,
					addr  - vma->vm_start +
					alias_vma->vm_start);
			if (unlikely(!alias_pmd))
				goto err;
			alias_vma = az_vma_vmstate(alias_vma)->next_aliased_vma;
		}
	}
out:
	if (new1)
		az_pmd_free(mms, new1, 0);
	if (new2)
		az_pmd_free(mms, new2, 0);
	AZMM_DETAILBUG_IF (!ret && (!pud_present(*pud1) ||
				!pfn_valid(az_pud_pfn(*pud1)))) {
		pud_clear_bad(pud1);
		printk(KERN_DEBUG "AZMM: __az_pmd_alloc(): new1_pa: %016lx\n",
				new1_pa);
		BUG();
	}
	return ret;
err:
	ret = -ENOMEM;
	goto out;
}

static inline pud_t *az_pud_alloc(struct vm_area_struct *vma, pgd_t *pgd1,
                pgd_t *pgd2, unsigned long addr)
{
	return (unlikely(pgd_none(*pgd1) || (pgd2 && pgd_none(*pgd2))) &&
			__az_pud_alloc(vma, pgd1, pgd2, addr)) ?
		NULL : az_pud_offset(pgd1, addr);
}

static inline pmd_t *az_pmd_alloc(struct vm_area_struct *vma, pud_t *pud1,
                pud_t *pud2, pgd_t *pgd_root1, pgd_t *pgd_root2,
                unsigned long addr)
{
	return (unlikely(pud_none(*pud1) || (pud2 && pud_none(*pud2))) &&
			__az_pmd_alloc(vma, pud1, pud2, pgd_root1,
				pgd_root2, addr)) ?
		NULL : az_pmd_offset(pud1, addr);
}

static inline pmd_t *az_alloc_pmd(struct vm_area_struct *vma,
        pgd_t *pgd_root1, pgd_t *pgd_root2, unsigned long addr)
{
	pgd_t *pgd1, *pgd2;
	pud_t *pud1, *pud2;

	/* pgds: */
	pgd1 = az_pgd_offset(pgd_root1, addr);
	pgd2 = pgd_root2 ? az_pgd_offset(pgd_root2, addr) : NULL;

	pud1 = az_pud_alloc(vma, pgd1, pgd2, addr);
	if (!pud1)
		return NULL;
	pud2 =  pgd_root2 ? az_pud_offset(pgd2, addr) : NULL;

	return az_pmd_alloc(vma, pud1, pud2, pgd_root1, pgd_root2, addr);
}

/* az_mm_lookup_pmd() MAY be called with a pmd lock held. */
static inline pmd_t *az_mm_lookup_pmd(struct mm_struct *mm,
		unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = az_pgd_offset(mm->pgd, addr);
	if (unlikely(!pgd_present(*pgd)))
		return NULL;

	pud = az_pud_offset(pgd, addr);
	if (unlikely(!pud_present(*pud)))
		return NULL;

	return az_pmd_offset(pud, addr);
}

/* az_mm_lookup_shadow_pmd must NOT be called with the pmd lock held. */
int az_mm_lookup_shadow_pmd(struct vm_area_struct *vma,
		unsigned long addr, pmd_t **shadow_pmdp)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	pgd_t *shadow_pgd;
	pud_t *shadow_pud;
	pmd_t *shadow_pmd = NULL;
	struct mm_struct *mm = mms->mm;
	int ret = 0;

	BUG_ON(!shadow_pmdp);
	if (unlikely(!mms->shadow_pgd))
		goto err_fault;

	if (unlikely(!mms->shadow_pgd_populated)) {
		shadow_pmd = az_alloc_pmd(vma, mms->shadow_pgd, mm->pgd, addr);
		if (unlikely(!shadow_pmd))
			goto err_nomem;
		if (unlikely(!az_pmd_present(*shadow_pmd))) {
			spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
			pmd_t *pmd = az_mm_lookup_pmd(mm, addr);
			BUG_ON(!pmd); /* we just allocated it */
			/* Populate under this pmd if not already populated */
			spin_lock(ptl);
			if (!az_pmd_present(*shadow_pmd) &&
			    az_pmd_present(*pmd))
				ret = az_populate_shadow_pmd(mms, pmd,
						shadow_pmd, (addr & PMD_MASK),
						ptl, NULL);
			spin_unlock(ptl);
		} 
		goto out;
	}

	*shadow_pmdp = NULL;

	shadow_pgd = az_pgd_offset(mms->shadow_pgd, addr);
	if (unlikely(!pgd_present(*shadow_pgd)))
		goto err_fault;

	shadow_pud = az_pud_offset(shadow_pgd, addr);
	if (unlikely(!pud_present(*shadow_pud)))
		goto err_fault;

	shadow_pmd = az_pmd_offset(shadow_pud, addr);
out:
	*shadow_pmdp = shadow_pmd;
	return ret;
err_fault:
	ret = -EFAULT;
	goto out;
err_nomem:
	ret = -ENOMEM;
	goto out;
}

int az_populate_aliased_vma_puds(struct vm_area_struct *vma)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	az_vmstate *vmstate = az_vma_vmstate(vma);
	struct mm_struct *mm = mms->mm;
	unsigned long addr = vma->vm_start;
	unsigned long end = vma->vm_end;
	unsigned long aliased_addr, aliased_vma_start, next;
	int active_shadow = az_mm_active_shadow(vma);

	BUG_ON(!vmstate->aliased_vma_head);
	aliased_vma_start = vmstate->aliased_vma_head->vm_start;

	for (; addr < end; addr = next) {
		pgd_t *main_pgd;
		pud_t *main_pud;
		pmd_t *pmd;

		aliased_addr = (addr  - vma->vm_start) + aliased_vma_start;

		/* Skip unpopulated address ranges of the aliased vma */
		main_pgd = az_pgd_offset(mm->pgd, aliased_addr);
		if (!pgd_present(*main_pgd)) {
			next = pgd_addr_end(addr, end);
			continue;
		}

		main_pud = az_pud_offset(main_pgd, aliased_addr);
		if (!pud_present(*main_pud)) {
			next = pud_addr_end(addr, end);
			continue;
		}

		/* The aliased vma has a populated pud entry. Populate ours. */
		pmd = az_alloc_pmd(vma, mm->pgd,
				active_shadow ? mms->shadow_pgd : NULL, addr);
		if (!pmd)
			return -ENOMEM;

		next = addr + PMD_SIZE;
	}
	return 0;
}

void az_prep_vma_for_unmap(struct vm_area_struct *this_vma)
{
	az_vmstate *this_vmstate = az_vma_vmstate(this_vma);
	struct vm_area_struct *vma;
	az_vmstate *vmstate;
	struct vm_area_struct *new_aliased_vma_head = NULL;

	/* we want to make sure a vma is not unmapped with other vmas
	 * referring to it, as would be the case if it is the root of
	 * an alias group. */

	if (!az_mm_aliasable_vma(this_vma))
		return;

	if (this_vmstate->aliased_vma_head) {
		/* Not the "root" of an alias group, take us off the list */
		vma = this_vmstate->aliased_vma_head;
		vmstate = az_vma_vmstate(vma);
		while (vmstate->next_aliased_vma != this_vma) {
			vma = vmstate->next_aliased_vma;
			BUG_ON(!vma);
			vmstate = az_vma_vmstate(vma);
		}
		/* az_vma_vmstate(vma)->next_aliased_vma is this_vma. Unlink. */
		vmstate->next_aliased_vma = this_vmstate->next_aliased_vma;
		this_vmstate->next_aliased_vma = NULL;
		this_vmstate->aliased_vma_head = NULL;

		/* NOTE: this_vma's mapping must be destroyed before
		 * mm_sema is released */
		return;
	}

	/* This vma is the "root" vma of an alias group. All other vmas
	 * in the group (if any exist) will have their aliased_vma field
	 * pointing to this one, and will be linked under it's next_aliased_vma
	 * list. To remove the root, we'll choose another root and point
	 * everyone's aliased_vma field to it */

	/* Unlink this vma from the head: */
	new_aliased_vma_head = this_vmstate->next_aliased_vma;
	this_vmstate->next_aliased_vma = NULL;

	if (!new_aliased_vma_head)
		return; /* Empty list, no aliases */

	/* Designate new root: */
	vmstate = az_vma_vmstate(new_aliased_vma_head);
	vmstate->aliased_vma_head =  NULL;

	/* have all non-roots point to new root: */
	vma = vmstate->next_aliased_vma;
	while (vma) {
		vmstate = az_vma_vmstate(vma);
		vmstate->aliased_vma_head = new_aliased_vma_head;
		vma = vmstate->next_aliased_vma;
	}

	/* this_vma's mapping must be destrotyed before mm_sema is released */
}

/*
 * *****************************************************************
 * Probe functions
 * *****************************************************************
 */

static inline pteval_t az_mm_probe_pmd_addr(pmd_t *pmd, unsigned long addr,
		int *refcnt)
{
	pte_t *pte;

	if (!pmd || !az_pmd_present(*pmd))
		return (pteval_t) _PAGE_PSE;
	if (pmd_large_virtual(*pmd)) {
		*refcnt = atomic_read(&(pte_page(*(pte_t *) pmd)->_count));
		return ((pte_t *)pmd)->pte;
	}
	pte = az_pte_offset(pmd, addr);
	/* Mask off PSE bit so ptes never show it set (even for PROT_NONE) */
	if(az_pte_none(*pte))
		return (pteval_t) 0;
	*refcnt = atomic_read(&(pte_page(*pte)->_count));
	return (pte->pte) & ~_PAGE_PSE;
}


pteval_t az_mm_probe_addr(struct mm_struct *mm,
		unsigned long addr, int *refcnt)
{
	pmd_t *pmd;
	pmd = az_mm_lookup_pmd(mm, addr);
	return az_mm_probe_pmd_addr(pmd, addr, refcnt);
}


pteval_t az_mm_probe_shadow_addr(struct vm_area_struct *vma,
		unsigned long addr, int *refcnt)
{
	pmd_t *pmd;
	if (az_mm_lookup_shadow_pmd(vma, addr, &pmd))
		return (pteval_t)0;
	return az_mm_probe_pmd_addr(pmd, addr, refcnt);
}


/*
 * *****************************************************************
 * Page table hierarchy functions to free mapping resources.
 * *****************************************************************
 */

/*
 * Replicas of memory.c's free_PPP_range [PPP = pgd, pud, pmd], 
 * somewhat modified, and we have to have them anyway because the
 * memory.c ones are static_inline there...
 *
 * az_free_pte_range() should be called holding a spinlock for the
 * associated pmd (determined via az_mm_pmd_lockptr()). Calls at higher
 * hierarchy levels should NOT be holding mm->page_table_lock. Instead,
 * the mm->mmap_sem should be held for write.
 */

static_inline
void az_free_pte_range(az_mmstate *mms, pmd_t *pmd)
{
	pgtable_t token = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	az_pte_free(mms, token, 0);
}

static_inline
void az_free_pmd_range(struct mmu_gather *tlb, pud_t *pud,
		unsigned long addr, unsigned long end,
		unsigned long floor, unsigned long ceiling)
{
	pmd_t *pmd;
	unsigned long next;
	unsigned long start;
	struct mm_struct *mm = tlb->mm;
	az_mmstate *mms = az_mm_mmstate(mm);
	BUG_ON(!mms);

	start = addr;
	pmd = az_pmd_offset(pud, addr);
	do {
		spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
		next = pmd_addr_end(addr, end);
		spin_lock(ptl);
		if (!az_pmd_none(*pmd)) {
			if (pmd_large_virtual(*pmd))
				pte_clear(mm, addr, (pte_t *)pmd);
			else if (az_pmd_present(*pmd))
				az_free_pte_range(mms, pmd);
			else
				pmd_clear_bad(pmd);
		}
		spin_unlock(ptl);
	} while (pmd++, addr = next, addr != end);

	start &= PUD_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PUD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pmd = az_pmd_offset(pud, start);
	pud_clear(pud);
	pmd_free_tlb(tlb, pmd, start);
}

static_inline void az_free_pud_range(struct mmu_gather *tlb, pgd_t *pgd,
		unsigned long addr, unsigned long end,
		unsigned long floor, unsigned long ceiling)
{
	pud_t *pud;
	unsigned long next;
	unsigned long start;

	start = addr;
	pud = az_pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		az_free_pmd_range(tlb, pud, addr, next, floor, ceiling);
	} while (pud++, addr = next, addr != end);

	start &= PGDIR_MASK;
	if (start < floor)
		return;
	if (ceiling) {
		ceiling &= PGDIR_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		return;

	pud = az_pud_offset(pgd, start);
	pgd_clear(pgd);
	pud_free_tlb(tlb, pud, start);
}

void az_free_root_pgd_range(struct mmu_gather *tlb, pgd_t *pgd_root,
		unsigned long addr, unsigned long end,
		unsigned long floor, unsigned long ceiling)
{
	pgd_t *pgd;
	unsigned long next;

	addr &= PMD_MASK;
	if (addr < floor) {
		addr += PMD_SIZE;
		if (!addr)
			return;
	}
	if (ceiling) {
		ceiling &= PMD_MASK;
		if (!ceiling)
			return;
	}
	if (end - 1 > ceiling - 1)
		end -= PMD_SIZE;
	if (addr > end - 1)
		return;

	pgd = az_pgd_offset(pgd_root, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		az_free_pud_range(tlb, pgd, addr, next, floor, ceiling);
	} while (pgd++, addr = next, addr != end);
}

void az_mm_free_pgd_range(struct mmu_gather *tlb,
		unsigned long addr, unsigned long end,
		unsigned long floor, unsigned long ceiling)
{
	struct mm_struct *mm = tlb->mm;
	az_mmstate *mms = az_mm_mmstate(mm);
	BUG_ON(!mms);
	/* Common path applys to shadow if one exists */
	az_free_root_pgd_range(tlb, mm->pgd, addr, end, floor, ceiling);
	if (mms->shadow_pgd)
		az_free_root_pgd_range(tlb, mms->shadow_pgd, addr, end,
				floor, ceiling);
}

/*
 * *****************************************************************
 * Shadow handling functions, to populate, commit, and free a shadow
 * *****************************************************************
 */

/* 
 * az_populate_shadow_pmd(): populate under given shadow pmd.
 *
 * Expects to be called with ptl held. may release ptl for allocation
 * and but will always return with ptl held.
 *
 * Expects a non-present shadow pmd, and with PMD-aligned addr.
 *
 * Will use page in (*new_page) if given and needed, and will set it to
 * null if it is consumed. May set (*new_page) to newly allocated page
 * even if not given one, and it is the caller's responsibility to
 * later discard of the page if it makes no use of it.
 *
 */
int az_populate_shadow_pmd(az_mmstate *mms,
		pmd_t *main_pmd, pmd_t *shadow_pmd,
		unsigned long addr, spinlock_t *ptl, pgtable_t *new_page)
{
	struct mm_struct *mm = mms->mm;
	pgtable_t new_shadow_pte_page = new_page ? *new_page : NULL;

	BUG_ON(az_pmd_present(*shadow_pmd));
	BUG_ON(addr & ~PMD_MASK);

	if (!az_pmd_present(*main_pmd))
		goto out;

	if (pmd_large_virtual(*main_pmd)) {
		set_pte_at(mm, addr, (pte_t *)shadow_pmd, *(pte_t *)main_pmd);
		goto out;
	}

	/* Need to populate pmd with new pte page. */
	if (!new_shadow_pte_page) {
		/* Allocate outside of lock */
		spin_unlock(ptl);
		new_shadow_pte_page = az_alloc_page(mms, AZ_MM_PTE_PAGE, 0, 0);
		smp_wmb();
		spin_lock(ptl);
		if (!new_shadow_pte_page)
			return -ENOMEM;
	}

	/* We have a pte page. But may have released lock. Re-test pmds */

	/* shadow_pmd may have been concurrently populated when we let go: */
	if (az_pmd_present(*shadow_pmd))
		goto out;

	/* main_pmd may have been concurrently set to non-present */
	if (!az_pmd_present(*main_pmd))
		goto out;

	if (pmd_large_virtual(*main_pmd)) {
		set_pte_at(mm, addr, (pte_t *)shadow_pmd, *(pte_t *)main_pmd);
	} else {
		pte_t *main_pte, *shadow_pte;
		unsigned long pte_addr;
		pmd_populate(mm, shadow_pmd, new_shadow_pte_page);
		new_shadow_pte_page = NULL;
		/* Copy contents to the shadow. */
		main_pte = az_pte_offset(main_pmd, addr);
		shadow_pte = az_pte_offset(shadow_pmd, addr);
		for (pte_addr = addr;
		     pte_addr < addr + PMD_SIZE; 
		     main_pte++, shadow_pte++, pte_addr += PAGE_SIZE) {
			set_pte_at(mm, pte_addr, shadow_pte, *main_pte);
		}
	}

out:
	if (new_page)
		*new_page = new_shadow_pte_page;
	else if (new_shadow_pte_page) 
		az_pte_free_never_used(mms, new_shadow_pte_page);
	return 0;
}

static_inline
static inline int az_populate_shadow_pud(az_mmstate *mms,
		struct vm_area_struct *vma,
		pud_t *main_pud, pud_t *shadow_pud,
		unsigned long addr, unsigned long end,
		pgtable_t *new_pte_page)
{
	pmd_t *main_pmd, *shadow_pmd;
	spinlock_t *ptl;
	int ret;

	BUG_ON(end > ((addr + PUD_SIZE) & PUD_MASK));

	for (; addr < end; addr += PMD_SIZE) {
		shadow_pmd = az_pmd_alloc(vma, shadow_pud, NULL,
				mms->shadow_pgd, NULL, addr);
		if (unlikely(!shadow_pmd))
			return -ENOMEM;
		main_pmd = az_pmd_offset(main_pud, addr);
		if (!az_pmd_present(*main_pmd))
			continue;
		/* 
		 * So we suspect there is a populated pmd entry in
		 * the main table. This pmd can still change with
		 * someone else unmapping it, changing it to a large
		 * page, or even mapping again after unmapping.
		 */

		BUG_ON(!new_pte_page);
		if (!(*new_pte_page)) {
			/* make sure we have a pte page ready */
			*new_pte_page = az_alloc_page(mms,
					AZ_MM_PTE_PAGE, 0, 0);
			if (!(*new_pte_page))
				return -ENOMEM;
			smp_wmb();
		}

		ptl = az_mm_pmd_lockptr(mms, addr);
		spin_lock(ptl);
		if (!az_pmd_present(*shadow_pmd))
			ret = az_populate_shadow_pmd(mms,
					main_pmd, shadow_pmd,
					addr, ptl,
					new_pte_page);
		else
			ret = 0;
		spin_unlock(ptl); /* Unlock pmd */

		if (ret)
			return ret;

		if (need_resched())
			cond_resched();
	}
	return 0;
}

static inline int az_populate_shadow_pgd(az_mmstate *mms,
		struct vm_area_struct *vma,
		pgd_t *main_pgd, pgd_t *shadow_pgd,
		unsigned long addr, unsigned long end,
		pgtable_t *new_pte_page)
{
	unsigned long next;
	pud_t *main_pud, *shadow_pud;
	int ret;

	for (; addr < end; addr = next) {
		next = pud_addr_end(addr, end);
		shadow_pud = az_pud_alloc(vma, shadow_pgd, NULL, addr);
		if (unlikely(!shadow_pud))
			return -ENOMEM;
		main_pud = az_pud_offset(main_pgd, addr);
		if (!pud_present(*main_pud))
			continue;
		ret = az_populate_shadow_pud(mms, vma, main_pud,
				shadow_pud, addr, next, new_pte_page);
		if (ret)
			return ret;
	}
	return 0;
}

static int az_populate_shadow_table(az_mmstate *mms)
{
	struct mm_struct *mm = mms->mm;
	struct vm_area_struct *vma;
	pgtable_t new_pte_page = NULL;
	int ret;
	unsigned long nr_batchable_vmas = 0;

	if (mms->shadow_pgd == NULL)
		return -EINVAL;

	lru_add_drain();

	vma = mm->mmap;
	if (!vma)
		return -EFAULT;

	do {
		unsigned long addr, end, next;

		if (!az_mm_batchable_vma(vma))
			continue;

		nr_batchable_vmas++;
		addr = vma->vm_start;
		end = vma->vm_end;

		/* Batchable AZ_VMM areas expected to be PUD_SIZE aligned */
		BUG_ON(addr & ~PUD_MASK);
		BUG_ON(end & ~PUD_MASK);

		flush_cache_range(vma, addr, end);

		for (; addr < end; addr = next) {
			pgd_t *main_pgd, *shadow_pgd;
			next = pgd_addr_end(addr, end);

			shadow_pgd = az_pgd_offset(mms->shadow_pgd, addr);
			main_pgd = az_pgd_offset(mm->pgd, addr);
			if (!pgd_present(*main_pgd))
				continue;
			ret = az_populate_shadow_pgd(mms, vma, main_pgd,
					shadow_pgd, addr, next, &new_pte_page);
			if (ret)
				goto err;
		}
	} while ((vma = vma->vm_next));

	ret = nr_batchable_vmas ? 0 : -EFAULT;
out:
	/* Free leftover pre-allocated page if one exists: */
	if (new_pte_page)
		az_pte_free_never_used(mms, new_pte_page);
	return ret;
err:
	goto out;
}

void az_commit_pud_range(pgd_t *pgd, pgd_t *shadow_pgd,
				unsigned long addr, unsigned long end)
{
	pud_t *pud, *shadow_pud, tmp_pud_val;
	unsigned long next;

	pud = az_pud_offset(pgd, addr);
	shadow_pud = az_pud_offset(shadow_pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		/* Flip puds between main and shadow. */
		tmp_pud_val = *pud;
		set_pud(pud, *shadow_pud);
		set_pud(shadow_pud, tmp_pud_val);
	} while (pud++, shadow_pud++, addr = next, addr != end);
}

void az_commit_pgd_range(struct mm_struct *mm, pgd_t *shadow_pgd_root,
                         unsigned long addr, unsigned long end)
{
	pgd_t *pgd, *shadow_pgd, tmp_pgd;
	unsigned long next;

	pgd = az_pgd_offset(mm->pgd, addr);
	shadow_pgd = az_pgd_offset(shadow_pgd_root, addr);
	do {
		next = pgd_addr_end(addr, end);

		if (!pgd_present(*pgd) && !pgd_present(*shadow_pgd))
			continue;
		if (!pgd_present(*pgd) && pgd_present(*shadow_pgd)) {
			int flipped_pgds = false;
			/* XXX: is this even possible?? Maybe should BUG_ON */
			/*
			 * Shadow has entries under this pgd and the main
			 * does not. Flip the pgd entries, locking against
			 * non-VM_AZMM changes.
			 */
			spin_lock(&mm->page_table_lock);
			if (!pgd_present(*pgd)) {
				tmp_pgd = *pgd;
				*pgd = *shadow_pgd;
				*shadow_pgd = tmp_pgd;
				flipped_pgds = true;
			}
			spin_unlock(&mm->page_table_lock);
			if (flipped_pgds)
				continue;
		}
		/*
		 * We should never encounter a case where the main has entries
		 * under this pgd but shadow does not, since hierarchy entries
		 * above pmd are not removed from either side during normal ops.
		 */
		BUG_ON(pgd_present(*pgd) && !pgd_present(*shadow_pgd));

		az_commit_pud_range(pgd, shadow_pgd, addr, next);

	} while (pgd++, shadow_pgd++, addr = next, addr != end);
}

/*
 * az_commit_shadow_pgd() : Commit shadow pgd into the main page table.
 * We do this by flipping all batchable VM_AZMM related pud entries from
 * the shadow into the main table, and placing the [old] main pud entry
 * in the shadow for later freeing.
 *
 * Expects to be called with the mm->mmap_sem held for write, to make sure
 * no in flight changes to the maps run concurrently with the commit.
 */ 
void az_commit_shadow_pgd(struct mm_struct *mm, pgd_t *shadow_pgd_root)
{
	struct vm_area_struct *vma;

	vma = mm->mmap;
	while (vma) {
		if (az_mm_batchable_vma(vma))
			az_commit_pgd_range(mm, shadow_pgd_root,
					vma->vm_start, vma->vm_end);
		vma = vma->vm_next;
	}
}

/*
 * az_free_shadow_pgd()
 * Free the hierarchical page table contents of the shadow pgd. 
 * Since batch ops only remap and change protections, no new pages
 * were created during the batch, so no page actually need to be unmapped
 * even if it's non-empty when it is destroyed.
 *
 * Expects the shadow pgd to already be disconnected from the mm. 
 */

void az_free_shadow_pgd(struct mmu_gather *tlb, pgd_t *shadow_pgd)
{
	struct mm_struct *mm = tlb->mm;
	struct vm_area_struct *vma = mm->mmap;
	unsigned long end = 0;
	az_mmstate *mms = az_mm_mmstate(mm);

	BUG_ON(!mms);
	BUG_ON(mms->shadow_pgd_populated != false);
	BUG_ON(mms->shadow_pgd != NULL);
	BUG_ON(mms->prev_main_pgd != NULL);

	lru_add_drain();
	/* Establish end address for all vmas: */
	while (vma) {
		if (vma->vm_end > end)
			end = vma->vm_end;
		vma = vma->vm_next;
	}
	/* free shadow pgd hierarchy for entire range: */
	az_free_root_pgd_range(tlb, shadow_pgd,
			FIRST_USER_ADDRESS, end,
			FIRST_USER_ADDRESS, 0);

	put_page(virt_to_page(shadow_pgd));
}

/*
 * *****************************************************************
 * Shatter/Unshatter functions to manipulate large physical pages
 * *****************************************************************
 */

/*
 * az_shatter_pmd(): take a large pmd entry and break it into pte entries
 * that all point to small pages at appropriate offsets within the same
 * large physical page.
 * The large physical page remains unbroken (compund), and ref counting
 * for ptes within a large physical page is done on the large (compound)
 * physical page. Since large_pmd() entries each count as PTRS_PER_PMD
 * refs, shattering does not increase ref counts.
 *
 * The pmd entry will change to to become a "normal" hierachy point, but
 * will have PAGE_LARGE_PHYSICAL set, so that future ptes manipulations
 * will be able to tell they are part of a large physical page.
 *
 * az_shatter_pmd() can be called on the main table only (when no batch is
 * open), on the shadow table only (e.g. during shadow only remaps), or
 * or on both (when an active batch is open and changes are done to the
 * main table). Shadow-only shatters (called with the a null main pmd)
 * must shatter the associated main pmd if one exists, since there is
 * an overall expectation that the hierarchy structures match for main
 * and shadow for any address ranges where both have contents.
 *
 */

static_inline
int az_shatter_pmd(az_mmstate *mms, pmd_t *pmd, pmd_t *shadow_pmd,
		unsigned long addr)
{
	struct mm_struct *mm = mms->mm;
	pgtable_t new_pte_page = NULL;
	pgtable_t new_shadow_pte_page = NULL;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
	unsigned long large_pfn, pfn;
	pgprot_t pgprot, shadow_pgprot;
	pte_t *pte, *shadow_pte;
	int ret;

	BUG_ON(addr & ~PMD_MASK);
	BUG_ON(!pmd && !shadow_pmd);

	/* get pages for pte entries. */
	if (pmd) {
		new_pte_page = az_alloc_page(mms, AZ_MM_PTE_PAGE, 0, 0);
		if (!new_pte_page)
			goto err_nomem;
		smp_wmb();
	}
	if (shadow_pmd) {
		new_shadow_pte_page = az_alloc_page(mms, AZ_MM_PTE_PAGE, 0, 0);
		if (!new_shadow_pte_page)
			goto err_nomem;
		smp_wmb();
	}

	spin_lock(ptl);

	/* We shouldn't be asked to shatter non-present pmds : */
	if ((pmd && !az_pmd_present(*pmd)) ||
			(shadow_pmd && !az_pmd_present(*shadow_pmd)))
		goto err_fault_unlock;

	/* If already not large, act like we got a NULL: */
	if (pmd && !pmd_large_virtual(*pmd))
		pmd = NULL;
	if (shadow_pmd && !pmd_large_virtual(*shadow_pmd))
		shadow_pmd = NULL;
	if (!pmd && !shadow_pmd)
		goto err_fault_unlock;

	if ((pmd && !pmd_large_virtual(*pmd)) ||
			(shadow_pmd && !pmd_large_virtual(*shadow_pmd)))
		goto err_fault_unlock;

	if ((pmd && shadow_pmd) &&
			!az_same_pmd_page(pmd, shadow_pmd))
		goto err_fault_unlock;

	large_pfn = pmd ? pte_pfn(*((pte_t *)pmd)) :
		pte_pfn(*((pte_t *)shadow_pmd));

	pte = pmd ?
		(pte_t *)pfn_to_kaddr(page_to_pfn(new_pte_page)) :
		NULL;
	pgprot = pmd ?
		pte_pgprot(pte_clrlarge_virtual((*(pte_t *)pmd))) :
		__pgprot(0);

	shadow_pte = shadow_pmd ?
		(pte_t *)pfn_to_kaddr(page_to_pfn(new_shadow_pte_page)) :
		NULL;
	shadow_pgprot = shadow_pmd ?
		pte_pgprot(pte_clrlarge_virtual((*(pte_t*)shadow_pmd))) :
		__pgprot(0);

	/* fill in pte values before making them visible */
	for (pfn = large_pfn; pfn < large_pfn + PTRS_PER_PMD;
	     (pte ? pte++ : 0), (shadow_pte ? shadow_pte++ : 0),
	     pfn++, addr += PAGE_SIZE) {
		if (pte) {
			pte_t entry = pfn_pte(pfn, pgprot);
			entry = az_pte_mkpresent(entry);
			set_pte_at(mm, addr, pte, entry);
		}
		if (shadow_pte) {
			pte_t shadow_entry = pfn_pte(pfn, shadow_pgprot);
			shadow_entry = az_pte_mkpresent(shadow_entry);
			set_pte_at(mm, addr, shadow_pte, shadow_entry);
		}
	}
	smp_wmb();

	if (pmd) {
		pmd_populate(mm, pmd, new_pte_page);
		new_pte_page = NULL;
	}
	if (shadow_pmd) {
		pmd_populate(mm, shadow_pmd, new_shadow_pte_page);
		new_shadow_pte_page = NULL;
	}

	/*
	 * At this point, TLB caches may still include the large_pmd. This
	 * is ok since it is still consistent with the pte entries. Any
	 * future mods to ptes that would send an invalidate would also
	 * invalidate any cached overlapping pmd entry.
	 */
	ret = 0;

out_unlock:
	spin_unlock(ptl);
out:
	if (new_pte_page)
		az_pte_free_never_used(mms, new_pte_page);
	if (new_shadow_pte_page)
		az_pte_free_never_used(mms, new_shadow_pte_page);
	return ret;
err_fault_unlock:
	ret = -EFAULT;
	goto out_unlock;
err_nomem:
	ret = -ENOMEM;
	goto out;
}

/*
 * az_unshatter_pmd_start(): take a pmd entry that includes ptes pointing to 
 * individual pages (which can be small pages or parts of large physical
 * pages), and cause all ptes within it to point to their associated 
 * counterparts in a common physical page, and to hold a common protection.
 * The notion is that (barring some sort of concurrent interference) a later
 * az_unshatter_pmd_finish() call will then be able to convert the pmd to a
 * single large mapping to a single large physical page.
 *
 * The caller provides dst_large_page, which must point to a large physical
 * page, and will be used, if needed, as the physical backing of the eventual
 * large mapping. If the function succesfully completes without needing to
 * consume dst_large_page, and src_large_page in non-NULL, *src_large_page
 * will be set to dst_large_page to allow it to be reused by the caller.
 *
 * az_unshatter_pmd_start() may be called on a pmd that is already unshattered,
 * is in the process of being unshattered, or whose ptes may not require to be
 * remapped in order to facilitate unshatttering. In such cases, the
 * az_unshatter_pmd_start() will succeed without consuming *dst_large_page.
 *
 * If unshattering succesfully gets set up by the call, the dst_large_page
 * is consumed, and the first pte being unshattered (that actually contains
 * a mapping) is mapped to a portion of a large physical page [this is
 * expected to be the common mode], az_unshatter_pmd_start() will proviode
 * the caller with a refernce to that large physical source page in
 * *src_large_page (if it was given a non-NULL *src_large_page).
 * This reference will prevent the inidicated source physical page from
 * being released even after all ptes have been succesfully transitioned
 * to the destination physical page.
 *
 * It is the caller's responsibility to either release it's reference to
 * (*src_large_page) or use it appropriately as a resource in some other
 * mapping.
 *
 * Rationale: In hand-over-hand compacting-unshattering ops, an available
 * physical page is provided to guarantee forward progress even when no new
 * pages can be allocated. If it used, another physical page needs to be
 * reclaimed during the compacting unshatter op, so it can be used for next.
 * Compacting unshatters would normally be built such that at least one large
 * source physical page would be released as a result of an unshatter of one
 * large physical page, but they need to prevent this released page from being
 * recirculated by the system for some other purpose.
 *
 * az_unshatter_pmd_start() cannot be called while a batch operation is open.
 */

static_inline
int az_unshatter_pmd_start(az_mmstate *mms, pmd_t *pmd, pmd_t *shadow_pmd,
		unsigned long addr, int prot, struct page *dst_large_page,
		struct page **src_large_page, unsigned long src_addr, int flags)
{
	struct mm_struct *mm = mms->mm;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
	pgprot_t pgprot;
	int write_access = prot & PROT_WRITE;
	unsigned long dst_large_pfn, src_pfn, dst_pfn;
	unsigned long eaddr;
	pte_t *pte = NULL;
	pte_t *shadow_pte = NULL;
	struct page *first_head = NULL;
	int needs_unshatter = false;
	int needs_prot_pass = false;
	int already_being_unshattered = false;
	int ret = 0;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);
	pte_t protcheck_entry;
	pgprotval_t protcheck_pgprotval;

	BUG_ON(addr & ~PMD_MASK);
	BUG_ON(!pmd);
	BUG_ON(!dst_large_page);
	BUG_ON(!az_mm_is_large_page(dst_large_page));

	/* Derive pgprot. NOTE: This implicitly sets present, and doesn't
	 * include the RW bit. */
	pgprot = vm_get_page_prot(calc_vm_prot_bits(prot));
	protcheck_entry = pfn_pte(0, pgprot);
	if (write_access)
		protcheck_entry = pte_mkwrite(pte_mkdirty(protcheck_entry));
	protcheck_pgprotval =
		pgprot_val(pte_pgprot(protcheck_entry)) & AZ_MM_PTE_PROT_MASK;

	spin_lock(ptl);

	/* We shouldn't be asked to unshatter non-present pmds : */
	if (!az_pmd_present(*pmd) ||
	    (shadow_pmd && !az_pmd_present(*shadow_pmd)))
		goto err_fault_unlock;
	/* If it's already a large page: */
	if (pmd_large_virtual(*pmd)) {
		/* check for shadow mismatch */
		if (shadow_pmd &&
				(!pmd_large_virtual(*shadow_pmd) ||
				 !az_same_pmd_page(pmd, shadow_pmd)))
			goto err_fault_unlock;
		/* already unshattered, leave without error: */
		goto out_unlock;
	}

	/* First pass establishes what needs to be done: */
	shadow_pte = shadow_pmd ? az_pte_offset(shadow_pmd, addr) : NULL;
	for (pte = az_pte_offset(pmd, addr), eaddr = addr;
	     eaddr < addr + PMD_SIZE;
	     pte++, (shadow_pmd ? shadow_pte++ : 0), eaddr += PAGE_SIZE) {
		if (az_pte_none(*pte)) {
			if (shadow_pte && !az_pte_none(*shadow_pte))
				goto err_fault_unlock;
			needs_unshatter = true;
			continue;
		}
		if (shadow_pte && !az_same_pte_page(pte, shadow_pte))
			goto err_fault_unlock;
		if (!first_head && az_mm_is_large_page(pte_page(*pte)))
			first_head = az_mm_large_page_head(pte_page(*pte));
		needs_prot_pass |= 
			((pgprot_val(pte_pgprot(*pte)) & AZ_MM_PTE_PROT_MASK)
				 != protcheck_pgprotval);
		if (shadow_pte)
			needs_prot_pass |= 
				((pgprot_val(pte_pgprot(*shadow_pte)) &
				  AZ_MM_PTE_PROT_MASK) != protcheck_pgprotval);
		if (az_pte_present(*pte)) {
			/* Unshatter is needed if...: */
			/* If mapped physical page is not large: */
			needs_unshatter |= !az_mm_is_large_page(pte_page(*pte));
			/* If ptes don't map to same large physical page: */
			needs_unshatter |=
				(az_mm_large_page_head(pte_page(*pte)) !=
				 first_head);
			/* If ptes don't map to associated offset in page: */
			needs_unshatter |=
				((pte_pfn(*pte) &
				  ((~PMD_MASK) >> PAGE_SHIFT)) !=
				 ((eaddr & (~PMD_MASK)) >> PAGE_SHIFT));
			continue;
		}
		/* If we get here, the pte maps to a p-p mapping */
		already_being_unshattered = true;
		/*
		 * Check for interferance:
		 * If p-p mappings are not in same large physical page, or
		 * an earlier az_pte_none() or az_pte_present() detected a need
		 * to unshatter, or  protection values are not what we are
		 * trying to set things to, there was some sort of concurrent
		 * modification (e.g. a remap of an already unshattered
		 * subpage), and we cannot proceed with the  unshatter attempt.
		 */
		if ((az_mm_large_page_head(pte_page(*pte)) != first_head) ||
				needs_unshatter || needs_prot_pass)
			goto err_fault_unlock;
	}

	if (already_being_unshattered)
		goto out_unlock;

	if (!needs_unshatter) {
		if (!needs_prot_pass)
			goto out_unlock;
		shadow_pte = shadow_pmd ?
			az_pte_offset(shadow_pmd, addr) : NULL;
		for (pte = az_pte_offset(pmd, addr), eaddr = addr;
		     eaddr < addr + PMD_SIZE;
		     pte++, (shadow_pmd ? shadow_pte++ : 0),
				eaddr += PAGE_SIZE) {
			pte_t entry = pte_modify(*pte, pgprot);
			entry = az_pte_mkpresent(entry);
			set_pte_at(mm, eaddr, pte, entry);
			if (shadow_pte)
				set_pte_at(mm, eaddr, shadow_pte, entry);
		}
		smp_wmb();
		goto out_unlock;
	}

	/* If we get here, we are doing a an unshatter with p-p mappings. */
	dst_large_pfn = page_to_pfn(dst_large_page);

	/* Indicate that we are consuming dst_large_page */
	dst_large_page = NULL;

	/* Give caller a reference to the source page if valid */
	if (src_large_page && src_addr) {
		pte_t *src_pte = az_pte_offset(pmd, src_addr);
		if (az_pte_present(*src_pte) &&
				az_mm_is_large_page(pte_page(*src_pte))) {
			*src_large_page =
				az_mm_large_page_head(pte_page(*src_pte));
			az_mm_get_page_count(*src_large_page, PTRS_PER_PMD);
			atomic_long_add(PTRS_PER_PMD, &mms->nr_pgrefs_taken);
		}
	}

	shadow_pte = shadow_pmd ? az_pte_offset(shadow_pmd, addr) : NULL;
	for (dst_pfn = dst_large_pfn, pte = az_pte_offset(pmd, addr),
			eaddr = addr;
	     dst_pfn < dst_large_pfn + PTRS_PER_PMD;
	     dst_pfn++, pte++, (shadow_pmd ? shadow_pte++: 0),
			eaddr += PAGE_SIZE) {
		pte_t entry = pfn_pte(dst_pfn, pgprot);
		entry = az_pte_mkpresent(entry);
		if (!az_pte_present(*pte) && !az_pte_none(*pte))
			continue; /* already being unshattered. */
		if (write_access)
			entry = pte_mkwrite(pte_mkdirty(entry));
		if (az_pte_present(*pte)) {
			pgtable_t dst_page = pfn_to_page(dst_pfn);
			src_pfn = pte_pfn(*pte);
			BUG_ON(dst_page->mapping !=
			       (struct address_space *) AZ_PAGE_MAPPING_POISON);
			dst_page->mapping =
				(struct address_space *)pfn_to_page(src_pfn);
			entry = az_pte_clrpresent(entry); /* p-p mapping */
			atomic_long_inc(&mms->nr_p2p_taken);
		}
		set_pte_at(mm, eaddr, pte, entry);
		if (shadow_pte)
			set_pte_at(mm, eaddr, shadow_pte, entry);
	}
	smp_wmb();

	/*
	 * At this point, mappings are set up, and will unshatter on-demand.
	 * However, pte entries mapping to the source pages may still be 
	 * cached, so if tlb_invalidate is requested, we need to flush here.
	 */
	if (tlb_invalidate)
		az_pmem_flush_tlb_and_released_pages(mms);


out_unlock:
	if (dst_large_page && src_large_page)
		*src_large_page = dst_large_page;
	spin_unlock(ptl);
	return ret;
err_fault_unlock:
	ret = -EFAULT;
	goto out_unlock;
}

static_inline
void az_unshatter_copy_into_pte(az_mmstate *mms, pte_t *pte,
		pte_t *shadow_pte, unsigned long addr)
{
	struct mm_struct *mm = mms->mm;
	pgtable_t dst_page, src_page;
	void *src_kaddr, *dst_kaddr;

	BUG_ON(az_pte_present(*pte) || az_pte_none(*pte));
	BUG_ON(shadow_pte &&
	       (az_pte_present(*shadow_pte) || az_pte_none(*shadow_pte)));
	BUG_ON(shadow_pte && !az_same_pte_page(pte, shadow_pte));

	dst_page= pte_page(*pte);
	BUG_ON(!dst_page);
	src_page = (struct page *)dst_page->mapping;
	BUG_ON((!src_page) ||
	       (src_page == (struct page *) AZ_PAGE_MAPPING_POISON));

	/* copy contents */
	src_kaddr = (void *)pfn_to_kaddr(page_to_pfn(src_page));
	dst_kaddr = (void *)pfn_to_kaddr(page_to_pfn(dst_page));
	memcpy((void *)dst_kaddr, (void *)src_kaddr, PAGE_SIZE);
	smp_wmb();

	/* set pte entry */ 
	set_pte_at(mm, addr, pte, az_pte_mkpresent(*pte));
	if (shadow_pte)
		set_pte_at(mm, addr, shadow_pte, az_pte_mkpresent(*shadow_pte));

	/* release source page */
	dst_page->mapping = (struct address_space *) AZ_PAGE_MAPPING_POISON;
	dec_mm_counter(mm, MM_ANONPAGES);
	atomic_long_inc(&mms->nr_p2p_released);
	az_remove_page(mms, src_page, AZ_MM_SMALL_PAGE);
}

static_inline
int az_unshatter_pmd_finish(az_mmstate *mms,
		pmd_t *pmd, pmd_t *shadow_pmd,
		unsigned long addr, int prot, int flags)
{
	struct mm_struct *mm = mms->mm;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
	pgprot_t pgprot = vm_get_page_prot(calc_vm_prot_bits(prot));
	int write_access = prot & PROT_WRITE;
	pgtable_t pte_page = NULL;
	pgtable_t shadow_pte_page = NULL;
	struct page *dst_large_page = NULL;
	unsigned long eaddr;
	pte_t *pte = NULL;
	pte_t *shadow_pte = NULL;
	pte_t entry;
	int refc;
	int ret = 0;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);

	BUG_ON(addr & ~PMD_MASK);
	BUG_ON(!pmd);

	/* copy individual pte-sized pages into unshattered dst page */

	spin_lock(ptl);
	/* We shouldn't be asked to unshatter non-present pmds : */
	if (!az_pmd_present(*pmd) ||
	    (shadow_pmd && !az_pmd_present(*shadow_pmd)))
		goto err_fault_unlock;
	/* If already unshattered, leave without error: */
	if (pmd_large_virtual(*pmd)) {
		/* check for shadow mismatch */
		if (shadow_pmd &&
			(!pmd_large_virtual(*shadow_pmd) ||
			 !az_same_pmd_page(pmd, shadow_pmd)))
			goto err_fault_unlock;
		entry = az_mk_large_pte(pte_page(*(pte_t *)pmd), pgprot,
				write_access);
		set_pte_at(mm, addr, (pte_t *)pmd, entry);
		if (shadow_pmd)
			set_pte_at(mm, addr, (pte_t *)shadow_pmd, entry);
		goto out_unlock;
	}

	shadow_pte = shadow_pmd ?  az_pte_offset(shadow_pmd, addr) : NULL;
	for (pte = az_pte_offset(pmd, addr), eaddr = addr;
	     eaddr < addr + PMD_SIZE;
	     pte++, (shadow_pmd ? shadow_pte++ : 0), eaddr += PAGE_SIZE) {
		pgtable_t dst_page;
		/* Verify that each pte is mapped to the same large page: */
		if (az_pte_none(*pte) ||
		    (shadow_pte && az_pte_none(*shadow_pte)))
			goto err_fault_unlock;
		if (shadow_pte && !az_same_pte_page(pte, shadow_pte))
			goto err_fault_unlock;
		dst_page = pte_page(*pte);
		if (!az_mm_is_large_page(dst_page))
			goto err_fault_unlock;
		if (!dst_large_page)
			dst_large_page = az_mm_large_page_head(dst_page);
		if (dst_large_page != az_mm_large_page_head(dst_page))
			goto err_fault_unlock;

		if (az_pte_present(*pte)) {
			if (shadow_pte && !az_pte_present(*shadow_pte))
				goto err_fault_unlock;
			continue; /* already unshattered. */
		}

		az_unshatter_copy_into_pte(mms, pte, shadow_pte, eaddr);

		spin_unlock(ptl);

		if (need_resched())
			cond_resched();

		spin_lock(ptl);

		/* re-verify pmd, since lock was released and regained */
		if (!az_pmd_present(*pmd) ||
		    (shadow_pmd && !az_pmd_present(*shadow_pmd)))
			goto err_fault_unlock;
		/* If it was somehow unshattered, leave without error: */
		if (pmd_large_virtual(*pmd)) {
			/* check for shadow mismatch */
			if (shadow_pmd &&
				(!pmd_large_virtual(*shadow_pmd) ||
				 !az_same_pmd_page(pmd, shadow_pmd)))
				goto err_fault_unlock;
			goto out_unlock;
		}
	}

	/* 
	 * Verify that exactly all expected ptes are mapped. Concurrent
	 * modifications (such as remaps and unmaps) could have changed that,
	 * and then we shouldn't create the large mapping (and with multiple
	 * of them ref counts alone are not enough for proper verfication).
	 * Concurrent prot changes don't bother us, as we are just as right
	 * to stomp them in place as they were to mess with our unshatter op.
	 */

	shadow_pte = shadow_pmd ?  az_pte_offset(shadow_pmd, addr) : NULL;
	for (pte = az_pte_offset(pmd, addr), eaddr = addr;
	     eaddr < addr + PMD_SIZE;
	     pte++, (shadow_pmd ? shadow_pte++ : 0), eaddr += PAGE_SIZE) {
		pgtable_t dst_page;
		if (!az_pte_present(*pte) ||
			(shadow_pte && !az_pte_present(*shadow_pte)))
			goto err_fault_unlock;
		if (shadow_pte && !az_same_pte_page(pte, shadow_pte))
			goto err_fault_unlock;
		dst_page = pte_page(*pte);
		if (!az_mm_is_large_page(dst_page) ||
			(dst_large_page != az_mm_large_page_head(dst_page)))
			goto err_fault_unlock;
	}
	refc = atomic_read(&dst_large_page->_count);
	BUG_ON(refc != PTRS_PER_PMD);

	/* Replace ptes with one large pmd. ref count stays the same: */
	entry = az_mk_large_pte(dst_large_page, pgprot, write_access);

	pte_page = pmd_pgtable(*pmd);
	set_pte_at(mm, addr, (pte_t *)pmd, entry);
	az_pte_free(mms, pte_page, 0);
	if (shadow_pmd) {
		shadow_pte_page = pmd_pgtable(*shadow_pmd);
		set_pte_at(mm, addr, (pte_t *)shadow_pmd, entry);
		az_pte_free(mms, shadow_pte_page, 0);
	}

	/*
	 * At this point, TLB caches may still include the pte mapping. This
	 * is temporarily ok since they are still consistent with the pmd, but
	 * any future mods to the pmd that would only invalidate the pmd
	 * address would fail to invalidate cached overlapping pte entries.
	 * This means that a tlb flush is needed here if tlb_invalidate is on.
	 */
	if (tlb_invalidate)
		az_pmem_flush_tlb_and_released_pages(mms);

	ret = 0;

out_unlock:
	spin_unlock(ptl);
	return ret;
err_fault_unlock:
	ret = -EFAULT;
	goto out_unlock;
}

int get_resource_page(struct vm_area_struct *vma, unsigned long resource_addr,
		pmd_t **resource_pmdp, pmd_t **resource_shadow_pmdp,
		struct page **dst_large_page, pgprot_t *resource_pgprot,
		int active_shadow, int acct, int flags)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	struct mm_struct *mm = vma->vm_mm;
	pmd_t *resource_pmd = NULL;
	pmd_t *resource_shadow_pmd = NULL;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);
	spinlock_t *resource_ptl = az_mm_pmd_lockptr(mms, resource_addr);
	int ret = 0;

	BUG_ON(!dst_large_page);

	if (resource_addr) {
		int refc;
		pte_t resource_pte_v;
		/* Get dst physical page and unmap it from resource_addr */

		/* get pmd and shadow pmd for resource address: */
		resource_pmd = az_mm_lookup_pmd(mm, resource_addr);
		if (!resource_pmd)
			goto err_fault;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(vma, 
					resource_addr, &resource_shadow_pmd);
			if (ret)
				goto err;
		}

		spin_lock(resource_ptl);
		if (!az_pmd_present(*resource_pmd) ||
			!pmd_large_virtual(*resource_pmd))
			goto err_fault_unlock;
		if (resource_shadow_pmd &&
			(!az_pmd_present(*resource_shadow_pmd) ||
			 !pmd_large_virtual(*resource_shadow_pmd) ||
			 !az_same_pmd_page(resource_pmd, resource_shadow_pmd)))
			goto err_fault_unlock;

		*dst_large_page = pte_page(*(pte_t *)resource_pmd);

		refc = atomic_read(&(*dst_large_page)->_count);
		BUG_ON(refc != PTRS_PER_PMD);

		if (resource_shadow_pmd)
			ptep_get_and_clear(mm, resource_addr,
					(pte_t *)resource_shadow_pmd);

		if (tlb_invalidate)
			resource_pte_v = ptep_clear_flush(vma, resource_addr,
					(pte_t *)resource_pmd);
		else
			resource_pte_v = ptep_get_and_clear(mm, resource_addr,
					(pte_t *)resource_pmd);
		spin_unlock(resource_ptl);
		*resource_pgprot = pte_pgprot(resource_pte_v);
	} else {
		/* Note: could improve async speed by pushing this to _start */
		/* Allocate a physical dst page */
		*dst_large_page = az_alloc_page(mms, AZ_MM_LARGE_PAGE, acct,
				flags);
		if (!(*dst_large_page))
			goto err_nomem;
		/* Large pages are ref'ed PTRS_PER_PMD time in AZ_MM: */
		az_mm_get_page_count(*dst_large_page, PTRS_PER_PMD - 1);
		atomic_long_add(PTRS_PER_PMD - 1, &mms->nr_pgrefs_taken);
	}
out:
	*resource_pmdp = resource_pmd;
	*resource_shadow_pmdp = resource_shadow_pmd;
	return ret;
err:
	goto out;
err_nomem:
	ret = -ENOMEM;
	goto out;
err_fault_unlock:
	spin_unlock(resource_ptl);
err_fault:
	ret = -EFAULT;
	goto out;
}

int az_unshatter_force_addr(az_mmstate *mms, pmd_t *pmd,
		pmd_t *shadow_pmd, unsigned long addr, int prot)
{
	struct mm_struct *mm = mms->mm;
	pte_t *pte = NULL;
	pte_t *shadow_pte = NULL;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, addr);
	int ret = 0;

	spin_lock(ptl);
	if (!az_pmd_present(*pmd))
		goto err_fault_unlock;
	if (pmd_large_virtual(*pmd)) {
		int write_access = prot & PROT_WRITE;
		pgprot_t pgprot = vm_get_page_prot(calc_vm_prot_bits(prot));
		pte_t entry = az_mk_large_pte(pte_page(*(pte_t *)pmd),
				pgprot, write_access);
		set_pte_at(mm, addr, (pte_t *)pmd, entry);
		if (shadow_pmd) {
			BUG_ON(!pmd_large_virtual(*shadow_pmd));
			set_pte_at(mm, addr, (pte_t *)shadow_pmd, entry);
		}
		goto out_unlock;
	}
	pte = az_pte_offset(pmd, addr);
	if (shadow_pmd)
		shadow_pte = az_pte_offset(shadow_pmd, addr);
	if (az_pte_none(*pte))
		goto err_fault_unlock;
	if (az_pte_present(*pte))
		goto out_unlock;
	/* Ok. if we get here, pte is mapped to a p-p mapping. */
	az_unshatter_copy_into_pte(mms, pte, shadow_pte, addr);
out_unlock:
	spin_unlock(ptl);
	return ret;
err_fault_unlock: 
	ret = -EFAULT;
	goto out_unlock;
}

int az_unshatter(az_mmstate *mms, struct vm_area_struct *vma,
		unsigned long addr, unsigned long force_addr,
		unsigned long resource_addr, unsigned long src_addr,
		int active_shadow, int prot, int acct, int flags)
{
	struct mm_struct *mm = mms->mm;
	struct page *dst_large_page;
	struct page *src_large_page = NULL;
	pmd_t *pmd, *shadow_pmd;
	pmd_t *resource_pmd = NULL;
	pmd_t *resource_shadow_pmd = NULL;
	pgprot_t resource_pgprot = __pgprot(0);
	pte_t resource_pte_v;
	spinlock_t *resource_ptl = az_mm_pmd_lockptr(mms, resource_addr);
	int ret = 0;
	int refc;

	/* Set up and verify pmd and shadow_pmd: */
	pmd = az_mm_lookup_pmd(mm, addr);
	if (!pmd || !az_pmd_present(*pmd))
		goto err_fault;

	shadow_pmd = NULL;
	if (active_shadow) {
		ret = az_mm_lookup_shadow_pmd(vma, addr, &shadow_pmd);
		if (ret)
			goto err;
	}

	ret = get_resource_page(vma, resource_addr,
			&resource_pmd, &resource_shadow_pmd,
			&dst_large_page, &resource_pgprot,
			active_shadow, acct, flags);

	if (ret)
		goto out;

	ret = az_unshatter_pmd_start(mms, pmd, shadow_pmd, addr, prot,
			dst_large_page, &src_large_page, src_addr, flags); 
	if (ret)
		goto out;

	if (force_addr) {
		/* Only force the specific address, and not the whole page. */
		ret = az_unshatter_force_addr(mms, pmd, shadow_pmd,
				force_addr, prot);
		goto out;
	}

	ret = az_unshatter_pmd_finish(mms, pmd, shadow_pmd, addr, prot, flags);
	if (ret)
		goto out;

	if (!resource_addr) {
		ret = 0;
		goto out;
	}
	if (!src_large_page) {
		ret = 1;
		goto out;
	}
	/* There was a source page identified by az_unshatter_pmd_start() */
	refc = atomic_read(&src_large_page->_count);
	BUG_ON(refc < PTRS_PER_PMD);
	if (refc > PTRS_PER_PMD) {
		/* Indicate the user how many refs the page has got still */
		ret = refc - PTRS_PER_PMD;
		goto out;
	}
	/* Source page was released by unshatter. map it at resource_addr. */
	spin_lock(resource_ptl);
	if (!az_pmd_none(*resource_pmd) ||
		(resource_shadow_pmd && !az_pmd_none(*resource_shadow_pmd))) {
		ret = 1;
		goto out_unlock;
	}
	resource_pte_v = mk_pte(src_large_page, resource_pgprot);
	set_pte_at(mm, resource_addr, (pte_t *)resource_pmd, resource_pte_v);
	if (resource_shadow_pmd)
		set_pte_at(mm, resource_addr,
				(pte_t *)resource_shadow_pmd, resource_pte_v);
	src_large_page = NULL;
	spin_unlock(resource_ptl);
	ret = 0;

out:
	if (src_large_page) { /* remove our reference to src_large_page. */
		az_mm_put_page_count(src_large_page, PTRS_PER_PMD - 1);
		atomic_long_add(PTRS_PER_PMD - 1, &mms->nr_pgrefs_released);
		az_remove_page(mms, src_large_page, flags);
	}
	return ret;
out_unlock:
	spin_unlock(resource_ptl);
	goto out;
err_fault:
	ret = -EFAULT;
	goto out;
err:
	goto out;
}

/*
 * *****************************************************************
 * Fault handling
 * *****************************************************************
 */

int az_mm_handle_mm_fault(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, unsigned int flags)
{
	spinlock_t *ptl;
	az_mmstate *mms = az_vma_mmstate(vma);
	pmd_t *pmd;
	pte_t *pte = NULL;
	pmd_t *shadow_pmd = NULL;
	pte_t *shadow_pte = NULL;
	int active_shadow = az_mm_active_shadow(vma);
	int ret = 0;
	int write_access = flags & FAULT_FLAG_WRITE;

	BUG_ON(!az_mm_vma(vma));

	pmd = az_mm_lookup_pmd(mm, addr);
	if (!pmd)
		goto err_maperr;

	if (active_shadow) {
		ret = az_mm_lookup_shadow_pmd(vma, addr, &shadow_pmd);
		if (ret)
			goto err_oom; /* main exists, shadow failed on oom. */
	}

	ptl = az_mm_pmd_lockptr(mms, addr);
	spin_lock(ptl);

	/* If not mapped, indicate SEGV_MAPERR */
	if (!az_pmd_present(*pmd))
		goto err_maperr_unlock;

	if (pmd_large_virtual(*pmd)) {
		/* mapped large page. check port, indicate ACCERR if needed */
		if (write_access & !pte_write(*(pte_t *)pmd))
			goto err_accerr_unlock;
		if (!az_pte_read_or_exec(*(pte_t *)pmd))
			goto err_accerr_unlock;
		goto out_unlock;
	}

	/* Small pages: */
	pte = az_pte_offset(pmd, addr);

	/* If pte not mapped, indicate SEGV_MAPERR: */
	if (az_pte_none(*pte))
		goto err_maperr_unlock;

	if (az_pte_present(*pte)) {
		/* mapped small page. check port, indicate ACCERR if needed */
		if (write_access & !pte_write(*pte))
			goto err_accerr_unlock;
		if (!az_pte_read_or_exec(*pte))
			goto err_accerr_unlock;
		goto out_unlock;
	}

	/* p2p mapping. Copy it in: */
	if (shadow_pmd) {
		if (!az_pmd_present(*shadow_pmd) ||
				pmd_large_virtual(*shadow_pmd))
			goto err_sigbus_unlock; /* must match main */
		shadow_pte = az_pte_offset(shadow_pmd, addr);
		if (az_pte_present(*shadow_pte) || az_pte_none(*shadow_pte))
			goto err_sigbus_unlock; /* must match main */
		if (!az_same_pte_page(pte, shadow_pte))
			goto err_sigbus_unlock; /* must match main */
	}
	az_unshatter_copy_into_pte(mms, pte, shadow_pte, addr);

out_unlock:
	spin_unlock(ptl);
out:
	return ret;
err_oom:
	ret = VM_FAULT_OOM;
	goto out;
err_maperr_unlock:
	spin_unlock(ptl);
err_maperr:
	ret = VM_FAULT_SEGV_MAPERR;
	goto out;
err_accerr_unlock:
	spin_unlock(ptl);
	ret = VM_FAULT_SEGV_ACCERR;
	goto out;
err_sigbus_unlock:
	spin_unlock(ptl);
	ret = VM_FAULT_SIGBUS;
	goto out;
}


/*******************************************************************/ 

/*
 * *****************************************************************
 * Unmapping functions to unmap an release mapped resources.
 * *****************************************************************
 */

/*
 * az_ versions of unmap_vmas, unmap_page_range, zap_pud_range. zap_pmd_range,
 * and zap_pte_range.
 *
 * These constitute the unmap path used from az_munmap and az_mremap. We
 * need our own versions both to facilitate PMD size page management and to
 * make sure the shadow page table is correctly updated.
 *
 * VM_AZMM uses different locking at the lowest level than other areas. We
 * hold the hierarchy lock (mm->page_table_lock) even when manipulating 
 * the lowest pte levels, since we need to protect against concurrent hierarchy
 * changes (e.g. concurrent unmaps or changes form small to large pages at
 * the pmd level).
 * 
 */

/* az_zap_pte_range() expects caller to be holding mm->page_table_lock */

static_inline
int az_zap_pte_range(struct vm_area_struct *vma,
		pmd_t *pmd, pmd_t *shadow_pmd,
		unsigned long addr, unsigned long end,
		unsigned long *next_addr,long *zap_work, 
		int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	pte_t *pte = NULL;
	pte_t *shadow_pte = NULL;
	int anon_rss = 0;
	int blind_unmap = flags & AZMM_BLIND_UNMAP;
	int ret;

	BUG_ON(!pmd);
	pte = az_pte_offset(pmd, addr);
	if (shadow_pmd)
		shadow_pte = az_pte_offset(shadow_pmd, addr);
	arch_enter_lazy_mmu_mode();
	do {
		if (az_pte_none(*pte) ||
		    (shadow_pte && az_pte_none(*shadow_pte))) {
			/* only filter error when main and shadow agree */
			if (blind_unmap && 
				(!shadow_pte ||
				 (az_pte_none(*pte) &&
				  az_pte_none(*shadow_pte))))
				continue;
			goto err_fault;
		}

		if (shadow_pte && !az_same_pte_page(pte, shadow_pte))
			goto err_fault;

		if (shadow_pte && !az_pte_none(*shadow_pte))
			pte_clear(mm, addr, shadow_pte);

		if (!az_pte_none(*pte)) {
			struct page *page = vm_normal_page(vma, addr, *pte);
			BUG_ON(!page);
			if (!az_pte_present(*pte)) {
				/* p2p mapping */
				struct page *src_page =
					(struct page *)page->mapping;
				BUG_ON((!src_page) ||
				       (src_page ==
					(struct page *)AZ_PAGE_MAPPING_POISON));
				page->mapping =	(struct address_space *)
					AZ_PAGE_MAPPING_POISON;
				anon_rss--;
				atomic_long_inc(&mms->nr_p2p_released);
				az_remove_page(mms, src_page, flags);
			}
			pte_clear(mm, addr, pte);
			anon_rss--;
			az_remove_page(mms, page, flags);
		}
	} while (pte++, (shadow_pte ? shadow_pte++ : 0),
			(*zap_work) -= PAGE_SIZE, addr += PAGE_SIZE,
			(addr != end && *zap_work > 0));

	ret = 0;
out:
	if (anon_rss)
		add_mm_counter(mm, MM_ANONPAGES, anon_rss);
	arch_leave_lazy_mmu_mode();
	*next_addr = addr;
	return ret;
err_fault:
	(*zap_work)--;
	ret = -EFAULT;
	goto out;
}

/* az_zap_large_pmd() expects caller to be holding mm->page_table_lock */

static_inline
int az_zap_large_pmd(struct vm_area_struct *vma,
		pmd_t *pmd, pmd_t *shadow_pmd,
		unsigned long addr, unsigned long *next_addr,
		long *zap_work, int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	int anon_rss = 0;
	int blind_unmap = flags & AZMM_BLIND_UNMAP;

	BUG_ON(!pmd);
	arch_enter_lazy_mmu_mode();

	if (az_pmd_none(*pmd) || (shadow_pmd && az_pmd_none(*shadow_pmd))) {
		/* only filter error when main and shadow agree */
		if (blind_unmap && 
			(!shadow_pmd ||
			 (az_pmd_none(*pmd) && az_pmd_none(*shadow_pmd))))
			goto out;
		goto err_fault;
	}
	/* If main and shadow are not the same, bad stuff WILL happen. */
	if (shadow_pmd &&
		(!pmd_large_virtual(*shadow_pmd) ||
	 !az_same_pmd_page(pmd, shadow_pmd)))
		goto err_fault;

	(*zap_work) -= PMD_PAGE_SIZE;

	if (shadow_pmd && pmd_large_virtual(*shadow_pmd))
		pte_clear(mm, addr, (pte_t *)shadow_pmd);

	if (pmd && pmd_large_virtual(*pmd)) {
		struct page *page = vm_normal_page(vma, addr, *(pte_t *)pmd);
		/* A present entry in an VM_AZMM area always has a page */
		BUG_ON(!page);
		pte_clear(mm, addr, (pte_t *)pmd);
		anon_rss -= PTRS_PER_PMD;
		/* Large pages are ref'ed PTRS_PER_PMD time in AZ_MM: */
		az_mm_put_page_count(page, PTRS_PER_PMD - 1);
		atomic_long_add(PTRS_PER_PMD - 1, &mms->nr_pgrefs_released);
		az_remove_page(mms, page, flags);
	}

	BUG_ON(shadow_pmd && !az_pmd_none(*shadow_pmd));
	BUG_ON(pmd && !az_pmd_none(*pmd));

	if (anon_rss)
		add_mm_counter(mm, MM_ANONPAGES, anon_rss);
	arch_leave_lazy_mmu_mode();

	*next_addr = addr + PMD_PAGE_SIZE;
out:
	return 0;
err_fault:
	(*zap_work)--;
	*next_addr = addr;
	return -EFAULT;
}

static_inline
int az_zap_pmd_range(struct vm_area_struct *vma,
		pud_t *pud, pud_t *shadow_pud,
		unsigned long addr, unsigned long end,
		unsigned long *next_addr, long *zap_work,
		int flags)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	pmd_t *pmd;
	pmd_t *shadow_pmd = NULL;
	unsigned long next;
	spinlock_t *ptl;
	pgtable_t new_shadow_pte_page = NULL;
	int ret;
	int blind_unmap = flags & AZMM_BLIND_UNMAP;
	int ok_to_shatter = flags & AZMM_MAY_SHATTER_LARGE_MAPPINGS;

	BUG_ON(!pud);
	pmd = az_pmd_offset(pud, addr);
	if (shadow_pud) {
		ret = az_mm_lookup_shadow_pmd(vma, addr, &shadow_pmd);
		if (ret)
			goto err;
	}
	do {
		next = pmd_addr_end(addr, end);
		/*
		 * We know hierarchy is stable up to this point, because
		 * freeing hierarchy levels above pmds will only be done with
		 * the mmap_sem held for write. To continue safely into a
		 * specific pmd entry, we need to lock the hierarchy so no
		 * changes would be made while we work on it.
		 */
		ptl = az_mm_pmd_lockptr(mms, addr);
		spin_lock(ptl);

		/* Shatter the pmd if required and is allowed to shatter */
		if (az_pmd_needs_shattering(addr, next - addr, pmd,
					shadow_pmd)) {
			if (!ok_to_shatter)
				goto err_fault_unlock;
			spin_unlock(ptl);
			ret = az_shatter_pmd(mms, pmd, shadow_pmd,
					addr & PMD_MASK);
			if (ret)
				goto err;
			spin_lock(ptl);
			/* Verify under lock. If conc. changed, err out. */
			if (az_pmd_needs_shattering(addr, next - addr, pmd,
						shadow_pmd))
				goto err_fault_unlock;
		}

		/* At this point main and shadow should match */
		if (!az_pmd_present(*pmd) ||
		    (shadow_pmd && !az_pmd_present(*shadow_pmd))) {
			/* trying to unmap in a range missing valid pmds. */
			if (blind_unmap && 
			    (!shadow_pmd ||
			     (!az_pmd_present(*pmd) &&
			      !az_pmd_present(*shadow_pmd)))) {
				/* only filter when main and shadow agree */
				(*zap_work)--;
				spin_unlock(ptl);
				continue;
			}
			goto err_fault_unlock;
		}

		/* Zap within this pmd: */
		if (pmd_large_virtual(*pmd) ||
		    (shadow_pmd && pmd_large_virtual(*shadow_pmd))) {
			ret = az_zap_large_pmd(vma, pmd, shadow_pmd, addr,
					&next, zap_work, flags);
		} else {
			ret = az_zap_pte_range(vma, pmd, shadow_pmd, addr,
					next, &next, zap_work, flags);
			/* Free pte mapping if we zapped a whole PMD: */
			if (!ret && !(addr & ~PMD_MASK) &&
			    !(next & ~PMD_MASK)) {
				az_free_pte_range(mms, pmd);
				if (shadow_pmd)
					az_free_pte_range(mms, shadow_pmd);
			}
		}

		if (ret) {
			addr = next;
			goto err_unlock;
		}
		/* Unlock hierarchy */
		spin_unlock(ptl);
	} while (pmd++, (shadow_pmd ? shadow_pmd++ : 0),
			addr = next, (addr != end && *zap_work > 0));

	ret = 0;
out:
	*next_addr = addr;
	if (new_shadow_pte_page)
		az_pte_free_never_used(mms, new_shadow_pte_page);
	return ret;
err_unlock:
	spin_unlock(ptl);
err:
	(*zap_work)--;
	goto out;
err_fault_unlock:
	ret = -EFAULT;
	goto err_unlock;
}

static_inline
int az_zap_pud_range(struct vm_area_struct *vma,
		pgd_t *pgd, pgd_t *shadow_pgd,
		unsigned long addr, unsigned long end,
		unsigned long *next_addr, long *zap_work,
		int flags)
{
	pud_t *pud;
	pud_t *shadow_pud = NULL;
	unsigned long next;
	int ret;
	int blind_unmap = flags & AZMM_BLIND_UNMAP;

	*next_addr = addr;
	BUG_ON(!pgd);
	pud = az_pud_offset(pgd, addr);
	if (shadow_pgd) {
		shadow_pud = az_pud_alloc(vma, shadow_pgd, NULL, addr);
		if (unlikely(!shadow_pud))
			goto err_nomem;
	}
	do {
		next = pud_addr_end(addr, end);
		/* Deal with concurrent shadow population: */
		if (shadow_pud && !pud_present(*shadow_pud) &&
				pud_present(*pud)) {
			az_mmstate *mms = az_vma_mmstate(vma);
			/* Populate matching shadow hierarchy level */
			pmd_t *shadow_pmd = az_pmd_alloc(vma, shadow_pud,
					NULL, mms->shadow_pgd, NULL, addr);
			if (unlikely(!shadow_pmd))
				goto err_nomem;
		}
		/* By this point main and shadow match for this pud. */
		if (pud_none_or_clear_bad(pud) ||
		    (shadow_pud && pud_none_or_clear_bad(shadow_pud))) {
			if (blind_unmap && 
			    (!shadow_pud ||
			     (pud_none(*pud) && pud_none(*shadow_pud)))) {
				(*zap_work)--;
				continue;
			}
			goto err_fault;
		}
		ret = az_zap_pmd_range(vma, pud, shadow_pud, addr, next,
				&next, zap_work, flags);
		if (ret != 0) {
			*next_addr = next;
			return ret;
		}
	} while (pud++, (shadow_pud ? shadow_pud++ : 0),
			addr = next, (addr != end && *zap_work > 0));

	*next_addr = addr;
	return 0;
err_nomem:
	(*zap_work)--;
	return -ENOMEM;
err_fault:
	(*zap_work)--;
	return -EFAULT;
}

int az_unmap_page_range(struct vm_area_struct *vma,
		unsigned long addr, unsigned long end,
		unsigned long *next_addr, long *zap_work,
		int flags, int active_shadow)
{
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	pgd_t *pgd;
	pgd_t *shadow_pgd = NULL;
	unsigned long next;
	int ret;
	int blind_unmap = flags & AZMM_BLIND_UNMAP;

	*next_addr = addr;
	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	if (active_shadow)
		shadow_pgd = az_pgd_offset(mms->shadow_pgd, addr);
	do {
		next = pgd_addr_end(addr, end);
		/* Deal with concurrent shadow population: */
		if (shadow_pgd && !pgd_present(*shadow_pgd) &&
				pgd_present(*pgd)) {
			/* Populate matching shadow hierarchy level */
			pud_t *shadow_pud = az_pud_alloc(vma, shadow_pgd,
						NULL, addr);
			if (unlikely(!shadow_pud))
				goto err_nomem;
		}
		/* By this point main and shadow match for this pgd. */
		if (pgd_none_or_clear_bad(pgd) ||
		    (shadow_pgd && pgd_none_or_clear_bad(shadow_pgd))) {
			if (blind_unmap && 
			    (!shadow_pgd ||
			     (pgd_none(*pgd) && pgd_none(*shadow_pgd)))) {
				(*zap_work)--;
				continue;
			}
			goto err_fault;
		}
		ret = az_zap_pud_range(vma, pgd, shadow_pgd, addr, next,
				&next, zap_work, flags);
		if (ret != 0) {
			*next_addr = next;
			return ret;
		}
	} while (pgd++, (shadow_pgd ? shadow_pgd++ : 0),
			addr = next, (addr != end && *zap_work > 0));

	*next_addr = addr;
	return 0;
err_nomem:
	(*zap_work)--;
	return -ENOMEM;
err_fault:
	(*zap_work)--;
	return -EFAULT;
}

unsigned long az_mm_unmap_page_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma,
		unsigned long addr, unsigned long end,
		long *zap_work, struct zap_details *details)
{
	int ret;
	unsigned long start = addr;
	if (az_mm_batchable_vma(vma))
		az_mm_terminate_mbatch(tlb);
	az_prep_vma_for_unmap(vma);
	ret = az_unmap_page_range(vma, start, end,
			&start, zap_work, AZMM_BLIND_UNMAP,
			az_mm_active_shadow(vma));
	BUG_ON(ret);
	return start;
}


#ifdef CONFIG_PREEMPT
# define ZAP_BLOCK_SIZE	(8 * PAGE_SIZE)
#else
/* No preempt: go for improved straight-line efficiency */
# define ZAP_BLOCK_SIZE	(1024 * PAGE_SIZE)
#endif

/**
 * az_unmap_vma - unmap a range of memory within a az_mm vma
 * @vma: the vma
 * @start_addr: virtual address at which to start unmapping
 * @end_addr: virtual address at which to end unmapping
 * @next_addr: virtual address at which unmapping ended
 * @flags: user flags for az_munmap
 * @active_shadow: Apply changes to shadow page table as well.
 *
 * This is a simplified clone of the work done in unmap_vmas(), with the
 * knowledge that we are unmapping a single az_mm vma.
 *
 * Can return error indication.
 *
 */
int az_unmap_vma(struct vm_area_struct *vma, unsigned long start_addr,
		unsigned long end_addr, int flags, int active_shadow)
{
	long zap_work = ZAP_BLOCK_SIZE;
	struct mm_struct *mm = vma->vm_mm;
	unsigned long start = start_addr;
	unsigned long end = end_addr;
	int ret;

	mmu_notifier_invalidate_range_start(mm, start_addr, end_addr);

	while (start != end) {
		ret = az_unmap_page_range(vma, start, end, &start,
				&zap_work, flags, active_shadow);
		if (ret != 0)
			goto out;

		if (zap_work > 0) {
			BUG_ON(start != end);
			break;
		}

		zap_work = ZAP_BLOCK_SIZE;
	}
	ret = 0;
out:
	mmu_notifier_invalidate_range_end(mm, start_addr, end_addr);
	return ret;
}

/*
 * az_mm_follow_page()
 */

int az_mm_follow_page(struct mm_struct *mm, struct vm_area_struct *vma,
		struct page **pages, struct vm_area_struct **vmas,
		unsigned long *position, int *length, int i,
		int write)
{
	az_mmstate *mms = az_vma_mmstate(vma);
	unsigned long addr = *position;
	int remainder = *length;
	spinlock_t *ptl;
	int active_shadow = az_mm_active_shadow(vma);
	int ret = 0;

	while (addr < vma->vm_end && remainder) {
		pmd_t *pmd;
		pmd_t *shadow_pmd = NULL;
		pte_t *pte;
		pte_t *shadow_pte = NULL;
		struct page *page;

		ptl = az_mm_pmd_lockptr(mms, addr);

		pmd = az_mm_lookup_pmd(mm, addr);
		if (!pmd)
			goto err_fault;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(vma, addr, &shadow_pmd);
			if (ret)
				goto err;
		}

		spin_lock(ptl);

		if (az_pmd_none(*pmd))
			goto err_fault_unlock;

		BUG_ON(!az_pmd_present(*pmd));

		if (pmd_large_virtual(*pmd)) {
			unsigned long large_page_pfn = pte_pfn(*(pte_t *)pmd);
			unsigned long pfn_offset;
			pfn_offset =(addr & ~PMD_MASK) >> PAGE_SHIFT;
			if (write & !pte_write(*(pte_t *)pmd))
				goto err_fault_unlock;
			page = pfn_to_page(large_page_pfn + pfn_offset);
			goto got_page;
		}

		pte = az_pte_offset(pmd, addr);
		if (shadow_pmd)
			shadow_pte = az_pte_offset(shadow_pmd, addr);

		if (az_pte_none(*pte))
			goto err_fault_unlock;

		if (write & !pte_write(*pte))
			goto err_fault_unlock;

		if (!az_pte_present(*pte)) {
			/* p-p mapping. copy it in before continuing. */
			az_unshatter_copy_into_pte(mms, pte, shadow_pte, addr);
			BUG_ON(!az_pte_present(*pte));
		}

		page = pte_page(*pte);
		BUG_ON(!page);

got_page:
		if (pages) {
			pages[i] = page;
			get_page(page);
		}
		if (vmas)
			vmas[i] = vma;

		addr += PAGE_SIZE;
		--remainder;
		++i;

		spin_unlock(ptl);

		cond_resched();
	}
out:
	*length = remainder;
	*position = addr;
	return i;
err:
	i = i ? i : ret;
	remainder = 0;
	if (need_resched())
		cond_resched();
	goto out;
err_fault_unlock:
	spin_unlock(ptl);
err_fault:
	ret = -EFAULT;
	goto err;
}

/*
 * *****************************************************************
 * Remapping functions to remap addresses within VM_AZMM.
 * *****************************************************************
 */

static_inline
int az_remap_ptes(struct vm_area_struct *vma,
		pmd_t *src_pmd, pmd_t *src_shadow_pmd,
		unsigned long src_addr, unsigned long src_end,
		pmd_t *dst_pmd, pmd_t *dst_shadow_pmd, unsigned long dst_addr,
		unsigned long *naddrs_done, int retain_src, int tlb_invalidate)
{
	pte_t *src_pte, *dst_pte, pte_v;
	pte_t *src_shadow_pte, *dst_shadow_pte;
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	int ret = 0;

	*naddrs_done = 0;

	src_pte = dst_pte = NULL;
	if (src_pmd) {
		BUG_ON(!dst_pmd);
		src_pte = az_pte_offset(src_pmd, src_addr);
		dst_pte = az_pte_offset(dst_pmd, dst_addr);
	}

	src_shadow_pte = dst_shadow_pte = NULL;
	if (src_shadow_pmd) {
		BUG_ON(!dst_shadow_pmd);
		src_shadow_pte = az_pte_offset(src_shadow_pmd, src_addr);
		dst_shadow_pte = az_pte_offset(dst_shadow_pmd, dst_addr);
	}

	arch_enter_lazy_mmu_mode();

	for (; src_addr < src_end;
	     (src_pte ? src_pte++ : 0), (src_shadow_pte ? src_shadow_pte++ : 0),
			src_addr += PAGE_SIZE,
			(dst_pte ? dst_pte++ : 0),
			(dst_shadow_pte ? dst_shadow_pte++ : 0),
			dst_addr += PAGE_SIZE) {
		/* validity checks: */
		if (src_pmd) {
			if (az_pte_none(*src_pte))
				goto err_fault;
			if (!az_pte_none(*dst_pte))
				goto err_fault;
		}
		if (src_shadow_pmd) {
			BUG_ON((!src_shadow_pte) || (!dst_shadow_pte));
			if (az_pte_none(*src_shadow_pte))
				goto err_fault;
			if (!az_pte_none(*dst_shadow_pte))
				goto err_fault;
		}
		if (src_pmd && src_shadow_pmd &&
				!az_same_pte_page(src_pte, src_shadow_pte))
			goto err_fault;

		if ((src_pmd && !az_pte_present(*src_pte)) ||
		    (src_shadow_pmd && !az_pte_present(*src_shadow_pte))) {
			/* We do not want to move p2p mappings, certainly
			 * not in shadow_only moves. Eliminate the p2p
			 * by pulling it through, and terminate az_remap_ptes
			 * early, without error, and  without moving this or
			 * the rest of the ptes.
			 * We don't want to move more than one of these
			 * under lock, and the caller will come back for all
			 * the uncompleted pages unless we return an error.
			 */
			pmd_t *main_pmd;
			pte_t *main_pte = src_pte;
			/* We may not have a src_pte (shadow only moves),
			 * and we need one to copy the p2p in, so find
			 * one and map/unmap it approriately if needed.
			 */
			if (!src_pmd) {
				main_pmd = az_mm_lookup_pmd(mm, src_addr);
				/* main pmd should always exist here. */
				BUG_ON(!main_pmd);
				main_pte = az_pte_offset(main_pmd, src_addr);
			}
			az_unshatter_copy_into_pte(az_vma_mmstate(vma),
					main_pte, src_shadow_pte, src_addr);
			goto out;
		}

		/* moves: */
		if (src_pmd) {
			if(retain_src) {
				pte_v = *src_pte;
				get_page(pte_page(pte_v));
				atomic_long_inc(&mms->nr_pgrefs_taken);
			} else if (tlb_invalidate) {
				pte_v = ptep_clear_flush(vma, src_addr,
						src_pte);
			} else {
				pte_v = ptep_get_and_clear(mm, src_addr,
						src_pte);
			}
			set_pte_at(mm, dst_addr, dst_pte, pte_v);
		}
		if (src_shadow_pmd) {
			pte_v = retain_src ? 
				*src_shadow_pte :
				ptep_get_and_clear(mm, src_addr,
					src_shadow_pte);
			set_pte_at(mm, dst_addr, dst_shadow_pte, pte_v);
		}
		(*naddrs_done) += PAGE_SIZE;
	}
out:
	arch_leave_lazy_mmu_mode();
	return ret;
err_fault:
	ret = -EFAULT;
	goto out;
}

static_inline
int az_remap_page_tables(struct vm_area_struct *src_vma,
		struct vm_area_struct *dst_vma,
		unsigned long src_addr,
		unsigned long dst_addr, size_t len,
		int move_shadow_only, unsigned long *naddrs_done,
		int active_shadow, int ok_to_shatter, int retain_src,
		int tlb_invalidate)
{
	unsigned long extent, next, src_end;
	struct mm_struct *mm = src_vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(src_vma);
	spinlock_t *ptl_a, *ptl_b;
	pmd_t *src_pmd, *dst_pmd;
	pmd_t *src_shadow_pmd, *dst_shadow_pmd;
	pgtable_t new_shadow_pte_page = NULL;
	pgtable_t new_pte_page = NULL;
	int ret;

	*naddrs_done = 0;
	src_end = src_addr + len;
	flush_cache_range(src_vma, src_addr, src_end);

	for (; src_addr < src_end; src_addr += extent, dst_addr += extent) {
		int large_pmd_move, prelock_large_pmd_move;

		/* calculate extent, stay within a single PMD on src and dst */
		next = (src_addr + PMD_SIZE) & PMD_MASK;
		if (next - 1 > src_end)
			next = src_end;
		extent = next - src_addr;
		next = (dst_addr + PMD_SIZE) & PMD_MASK;
		if (extent > next - dst_addr)
			extent = next - dst_addr;

		src_pmd = NULL;
		if (!move_shadow_only) {
			/* Establish hierarchy levels to src_pmd */
			src_pmd = az_mm_lookup_pmd(mm, src_addr);
			if (unlikely(!src_pmd))
				goto err_fault;
		}

		src_shadow_pmd = NULL;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(src_vma, src_addr,
					&src_shadow_pmd);
			if (ret)
				goto err;
		}

		prelock_large_pmd_move = (extent == PMD_SIZE) &&
			(src_pmd ? pmd_large_virtual(*src_pmd) :
			 pmd_large_virtual(*src_shadow_pmd));

		dst_pmd = NULL;
		if (!move_shadow_only) {
			/* Establish hierarchy levels to dst_pmd */
			dst_pmd = az_alloc_pmd(dst_vma, mm->pgd,
					active_shadow ? mms->shadow_pgd : NULL,
					dst_addr);
			if (unlikely(!dst_pmd))
				goto err_nomem;
			/* Get a pte page ready if it looks like we need it */
			if (!prelock_large_pmd_move &&
			    !az_pmd_present(*dst_pmd) && !new_pte_page) {
				new_pte_page = az_alloc_page(mms,
						AZ_MM_PTE_PAGE, 0, 0);
				if (!new_pte_page)
					goto err_nomem;
				smp_wmb();
			}
		}

		dst_shadow_pmd = NULL;
		if (active_shadow) {
			/* Ensure hierarchy levels to dst_shadow_pmd  */ 
			dst_shadow_pmd = az_alloc_pmd(dst_vma, mms->shadow_pgd,
					mm->pgd, dst_addr);
			if (unlikely(!dst_shadow_pmd))
				goto err_nomem;
			/* Re-Lookup shadow in order to make sure it is
			   properly populated */
			ret = az_mm_lookup_shadow_pmd(dst_vma, dst_addr,
					&dst_shadow_pmd);
			if (ret)
				goto err;
			/* Get a pte page ready if it looks like we need it */
			if (!prelock_large_pmd_move &&
			    !az_pmd_present(*dst_shadow_pmd) &&
			    !new_shadow_pte_page) {
				new_shadow_pte_page = az_alloc_page(mms,
						AZ_MM_PTE_PAGE, 0, 0);
				if (!new_shadow_pte_page)
					goto err_nomem;
				smp_wmb();
			}
		}

		BUG_ON((src_shadow_pmd && !dst_shadow_pmd) ||
				(!src_shadow_pmd && dst_shadow_pmd));

		if (az_pmd_needs_shattering(src_addr, extent, src_pmd,
					src_shadow_pmd)) {
			/* partial move from large pmd. Shatter source. */
			if (!ok_to_shatter)
				goto err_fault;
			ret = az_shatter_pmd(mms, src_pmd, src_shadow_pmd,
					src_addr & PMD_MASK);
			if (ret)
				goto err;
		}

		mmu_notifier_invalidate_range_start(mm, src_addr,
				src_addr + extent);

		/* Lock hierarchy so it doesn't change while we work on pmds */
		az_mm_pmd_lockptr_pair(mms, src_addr, dst_addr, &ptl_a, &ptl_b);
		az_mm_spin_lock_pair(ptl_a, ptl_b);

		/* verify (under lock) that no unshattering is needed */
		if (az_pmd_needs_shattering(src_addr, extent, src_pmd,
					src_shadow_pmd))
			goto err_fault_unlock;

		/* verify we are not overwriting a large page at dst_addr: */
		if ((dst_pmd && pmd_large_virtual(*dst_pmd)) ||
		    (dst_shadow_pmd && pmd_large_virtual(*dst_shadow_pmd)))
			goto err_fault_unlock;

		/* Verify that the source pmds are actually there */
		if ((src_pmd && !az_pmd_present(*src_pmd)) ||
		    (src_shadow_pmd && !az_pmd_present(*src_shadow_pmd)))
			goto err_fault_unlock;

		BUG_ON(src_pmd && src_shadow_pmd &&
		       (pmd_large_virtual(*src_pmd) !=
			pmd_large_virtual(*src_shadow_pmd)));

		/* Recompue large_pmd_move. May have changed before the lock */
		large_pmd_move = (extent == PMD_SIZE) &&
			(src_pmd ? pmd_large_virtual(*src_pmd) :
			 pmd_large_virtual(*src_shadow_pmd));

		/* A change to the large_pmd_move would be a concurrency
		 * conflict. Without checking for it, bad things could happen,
		 * like not having pre-allocated pte pages for targets (if
		 * large change to small) of not having split the source (if
		 * small changed to large).  Either way, it's user
		 * (concurrency) error, so flag it.
		 */ 
		if (large_pmd_move != prelock_large_pmd_move)
			goto err_fault_unlock;

		if (large_pmd_move) {
			pte_t pte_v;
			/* Old pmds are large, present pages, we are moving
			 * a whole pmd */
			if (dst_pmd &&
			    !az_pmd_none_or_clear_empty(dst_vma, dst_addr,
				    dst_pmd, tlb_invalidate))
				goto err_fault_unlock;
			if (dst_shadow_pmd &&
			    !az_pmd_none_or_clear_empty(dst_vma, dst_addr,
				    dst_shadow_pmd, tlb_invalidate))
				goto err_fault_unlock;

			/* verify main and shadow pages match */
			if (src_pmd && src_shadow_pmd &&
			    !az_same_pmd_page(src_pmd, src_shadow_pmd))
				goto err_fault_unlock;
			/* Move or multimap pmd value: */
			if (src_pmd) {
				if(retain_src) {
					pte_v = *(pte_t *)src_pmd;
					az_mm_get_page_count(pte_page(pte_v),
							PTRS_PER_PMD);
					atomic_long_add(PTRS_PER_PMD,
							&mms->nr_pgrefs_taken);
				} else if (tlb_invalidate) {
					pte_v = ptep_clear_flush(src_vma,
							src_addr,
							(pte_t *)src_pmd);
				} else {
					pte_v = ptep_get_and_clear(mm, src_addr,
							(pte_t *)src_pmd);
				}
				set_pte_at(mm, dst_addr,
						(pte_t *)dst_pmd, pte_v);
			}
			if (src_shadow_pmd) {
				pte_v = retain_src ?
					*(pte_t *)src_shadow_pmd :
					ptep_get_and_clear(mm, src_addr, 
						(pte_t *)src_shadow_pmd);
				set_pte_at(mm, dst_addr,
						(pte_t *)dst_shadow_pmd, pte_v);
			}
			(*naddrs_done) += PMD_SIZE;
		} else {
			/* Moving small pages. */
			BUG_ON((src_pmd && pmd_large_virtual(*src_pmd)) ||
			       (src_shadow_pmd &&
				pmd_large_virtual(*src_shadow_pmd)));

			/*  Populate target pte pages : */
			if (dst_pmd && !az_pmd_present(*dst_pmd)) {
				if (!new_pte_page) /* conflict */
					goto err_fault_unlock;
				pmd_populate(mm, dst_pmd, new_pte_page);
				new_pte_page = NULL;
			}
			if (dst_shadow_pmd && !az_pmd_present(*dst_shadow_pmd)) {
				if (!new_shadow_pte_page) /* conflict */
					goto err_fault_unlock;
				pmd_populate(mm, dst_shadow_pmd,
						new_shadow_pte_page);
				new_shadow_pte_page = NULL;
			}
			/* All pmds are good, move the ptes. */
			ret = az_remap_ptes(src_vma, src_pmd, src_shadow_pmd,
					src_addr, src_addr + extent,
					dst_pmd, dst_shadow_pmd, dst_addr,
					&extent, retain_src, tlb_invalidate);

			/* extent was updated to the number actually done */
			*naddrs_done += extent;
			/* If source covered PMD, free src mappings. */
			if (!retain_src && !(src_addr & ~PMD_MASK) && 
					(extent == PMD_SIZE)) {
				if (src_pmd)
					az_free_pte_range(mms, src_pmd);
				if (src_shadow_pmd)
					az_free_pte_range(mms, src_shadow_pmd);
			}
			if (ret)
				goto err_unlock;

		}

		az_mm_spin_unlock_pair(ptl_a, ptl_b);

		mmu_notifier_invalidate_range_end(mm, src_addr,
				src_addr + extent);

		if (need_resched()) {
			cond_resched();
		}
	}
	ret = 0;
out:
	if (new_pte_page)
		az_pte_free_never_used(mms, new_pte_page);
	if (new_shadow_pte_page)
		az_pte_free_never_used(mms, new_shadow_pte_page);
	return ret;
err_nomem:
	ret = -ENOMEM;
	goto err;
err_fault:
	ret = -EFAULT;
	goto err;
err_fault_unlock:
	ret = -EFAULT;
err_unlock:
	az_mm_spin_unlock_pair(ptl_a, ptl_b);
	mmu_notifier_invalidate_range_end(mm, src_addr, src_addr + extent);
err:
	goto out;
}

static_inline
int az_memcopy(struct vm_area_struct *vma,
		unsigned long dst_addr, unsigned long src_addr, size_t len)
{
	unsigned long extent, next, src_end;
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	spinlock_t *ptl_a, *ptl_b;
	pmd_t *src_pmd, *dst_pmd;
	int ret;

	src_end = src_addr + len;

	while (src_addr < src_end) {
		void *src_kaddr, *dst_kaddr;
		/* calculate extent. Stay within single page on src and dst */
		next = (src_addr + PAGE_SIZE) & PAGE_MASK;
		if (next - 1 > src_end)
			next = src_end;
		extent = next - src_addr;
		next = (dst_addr + PAGE_SIZE) & PAGE_MASK;
		if (extent > next - dst_addr)
			extent = next - dst_addr;

		src_pmd = az_mm_lookup_pmd(mm, src_addr);
		if (unlikely(!src_pmd))
			goto err_fault;

		dst_pmd = az_mm_lookup_pmd(mm, dst_addr);
		if (unlikely(!dst_pmd))
			goto err_fault;

		az_mm_pmd_lockptr_pair(mms, src_addr, dst_addr, &ptl_a, &ptl_b);
		az_mm_spin_lock_pair(ptl_a, ptl_b);

		if (!az_pmd_present(*src_pmd) || !az_pmd_present(*dst_pmd))
			goto err_fault_unlock;
		if (!pmd_large_virtual(*src_pmd)) {
			pte_t *src_pte = az_pte_offset(src_pmd, src_addr);
			if (!az_pte_present(*src_pte))
				goto err_fault_unlock;
			src_kaddr = (void *)pfn_to_kaddr(pte_pfn(*src_pte));
		} else {
			unsigned long src_pfn = pte_pfn(*(pte_t *)src_pmd);
			src_pfn += (src_addr & ~PMD_PAGE_MASK) >> PAGE_SHIFT;
			src_kaddr = (void *)pfn_to_kaddr(src_pfn);
		}
		if (!pmd_large_virtual(*dst_pmd)) {
			pte_t *dst_pte = az_pte_offset(dst_pmd, dst_addr);
			if (!az_pte_present(*dst_pte))
				goto err_fault_unlock;
			dst_kaddr = (void *)pfn_to_kaddr(pte_pfn(*dst_pte));
		} else {
			unsigned long dst_pfn = pte_pfn(*(pte_t *)dst_pmd);
			dst_pfn += (dst_addr & ~PMD_PAGE_MASK) >> PAGE_SHIFT;
			dst_kaddr = (void *)pfn_to_kaddr(dst_pfn);
		}

		/* If we made it this far, src_addr and dst_addr are mapped */
		memcpy((void *)dst_kaddr, (void *)src_kaddr, extent);

		az_mm_spin_unlock_pair(ptl_a, ptl_b);

		src_addr += extent;
		dst_addr += extent;
		if (src_addr < src_end)
			cond_resched();
	}
	ret = 0;
out:
	return ret;
err_fault:
	ret = -EFAULT;
	goto err;
err_fault_unlock:
	ret = -EFAULT;
	az_mm_spin_unlock_pair(ptl_a, ptl_b);
err:
	goto out;
}

static_inline
int az_protect_large_page(az_mmstate *mms, unsigned long addr,
		pmd_t *pmd, pmd_t *shadow_pmd,
		pgprot_t vm_page_prot, int write_access)
{
	struct mm_struct *mm = mms->mm;
	pte_t ptent;
	/* Large page protect: */
	if (pmd && shadow_pmd && !az_same_pmd_page(pmd, shadow_pmd))
		return -EFAULT;
	if (pmd) {
		ptent = az_pte_modify_large(*(pte_t *)pmd, vm_page_prot);
		if (write_access)
			ptent = pte_mkwrite(pte_mkdirty(ptent));
		set_pte_at(mm, addr, (pte_t *)pmd, ptent);
	}
	if (shadow_pmd) {
		ptent = az_pte_modify_large(*(pte_t *)shadow_pmd, vm_page_prot);
		if (write_access)
			ptent = pte_mkwrite(pte_mkdirty(ptent));
		set_pte_at(mm, addr, (pte_t *)shadow_pmd, ptent);
	}
	return 0;
}

static_inline
int az_protect_small_page(az_mmstate *mms, unsigned long addr,
		pmd_t *pmd, pmd_t *shadow_pmd,
		pgprot_t vm_page_prot, int write_access, int blind_protect)
{
	struct mm_struct *mm = mms->mm;
	pte_t *pte = NULL;
	pte_t *shadow_pte = NULL;
	pte_t ptent;
	int ret = 0;

	if (pmd)
		pte = az_pte_offset(pmd, addr);
	if (shadow_pmd)
		shadow_pte = az_pte_offset(shadow_pmd, addr);
	if (pmd && shadow_pmd && !az_same_pte_page(pte, shadow_pte))
		goto err_fault;
	if (!blind_protect &&
	    ((pmd && az_pte_none(*pte)) ||
	     (shadow_pmd && az_pte_none(*shadow_pte))))
		goto err_fault;
	if ((pmd && !az_pte_present(*pte)) ||
	    (shadow_pmd && !az_pte_present(*shadow_pte))) {
		/* p2p: copy in before making changes */
		pmd_t *main_pmd;
		pte_t *main_pte = pte;
		/* We may not have a main pte (shadow only protects),
		 * and we need one to copy the p2p in, so find
		 * one and map/unmap it approriately if needed.
		 */
		if (!pmd) {
			main_pmd = az_mm_lookup_pmd(mm, addr);
			/* main pmd should always exist here. */
			BUG_ON(!main_pmd);
			main_pte = az_pte_offset(main_pmd, addr);
		}
		az_unshatter_copy_into_pte(mms, main_pte, shadow_pte, addr);
	}

	if (pmd && !az_pte_none(*pte)) {
		ptent = pte_modify(*pte, vm_page_prot);
		if (write_access)
			ptent = pte_mkwrite( pte_mkdirty(ptent));
		ptent = az_pte_mkpresent(ptent);
		BUG_ON(!az_pte_present(*pte));
		set_pte_at(mm, addr, pte, ptent);
	}
	if (shadow_pmd && !az_pte_none(*shadow_pte)) {
		ptent = pte_modify(*shadow_pte, vm_page_prot);
		if (write_access)
			ptent = pte_mkwrite(pte_mkdirty(ptent));
		ptent = az_pte_mkpresent(ptent);
		BUG_ON(!az_pte_present(*shadow_pte));
		set_pte_at(mm, addr, shadow_pte, ptent);
	}
out:
	return ret;
err_fault:
	ret = -EFAULT;
	goto out;
}
			
static_inline
int az_protect_pages(struct vm_area_struct *vma, unsigned long start,
		size_t len, pgprot_t vm_page_prot, int write_access,
		int protect_shadow_only, int active_shadow, int ok_to_shatter,
		int blind_protect)
{
	spinlock_t *ptl;
	struct mm_struct *mm = vma->vm_mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	pmd_t *pmd, *shadow_pmd;
	int ret;

	do {
		ptl = az_mm_pmd_lockptr(mms, start);

		pmd = NULL;
		if (!protect_shadow_only) {
			pmd = az_mm_lookup_pmd(mm, start);
			if (!pmd) {
				if (blind_protect) {
					unsigned long next;
					next = pmd_addr_end(start, start + len);
					len -= (next - start);
					start = next;
					continue;
				}
				goto err_fault;
			}
		}

		shadow_pmd = NULL;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(vma, start, &shadow_pmd);
			if ((ret == -EFAULT) &&
			    protect_shadow_only && blind_protect) {
				unsigned long next = pmd_addr_end(start,
						start + len);
				len -= (next - start);
				start = next;
				continue;
			}
			if (ret)
				goto err;
		}

		/* See if we need to shatter target page: */
		if(az_pmd_needs_shattering(start, len, pmd, shadow_pmd)) {
			/* partial protect within large pmd. Shatter target. */
			if (!ok_to_shatter)
				goto err_fault;
			ret = az_shatter_pmd(mms, pmd, shadow_pmd,
					start & PMD_MASK);
			if (ret)
				goto err;
		}

		/* Note: we do not want to use the sequence of 
		 * ptep_modify_prot_start(), pte_modify(),
		 * ptep_modify_prot_commit() that change_pte_range() uses,
		 * because that sequence makes the pte temporarily not-present,
		 * in order to avoid losing hardware mods to the pte during
		 * the change. The reason we don't want that is that page
		 * faults may then occur within our AZ_VMM, which we will
		 * throw a SEGV on, or make us mistake it for a p2p fault.
		 * We don't care about dirty and accessed indication because
		 * we don't swap, so we simply do a non-atomic mod of the
		 * protection below. [If we ever decide to care about the
		 * hardware mods to ptes, we should change this]
		 */

		spin_lock(ptl);

		if(az_pmd_needs_shattering(start, len, pmd, shadow_pmd))
			goto err_fault_unlock;

		/* Verify that the pmds are actually there */
		if ((pmd && !az_pmd_present(*pmd)) ||
				(shadow_pmd && !az_pmd_present(*shadow_pmd))) {
			if (blind_protect) {
				unsigned long next =
					pmd_addr_end(start, start + len);
				len -= (next - start);
				start = next;
				spin_unlock(ptl);
				continue;
			}
			goto err_fault_unlock;
		}

		BUG_ON(pmd && shadow_pmd &&
		       (pmd_large_virtual(*pmd) !=
			pmd_large_virtual(*shadow_pmd)));

		if (pmd ?
		    pmd_large_virtual(*pmd) :
		    pmd_large_virtual(*shadow_pmd)) {
			/* Protect a large page */
			BUG_ON((start & ~PMD_MASK) || (len < PMD_SIZE));
			ret = az_protect_large_page(mms, start, pmd, shadow_pmd,
					vm_page_prot, write_access);
			if (ret)
				goto err_unlock;
			start += PMD_SIZE;
			len -= PMD_SIZE;
		} else {
			/* Protect a small page */
			ret = az_protect_small_page(mms, start, pmd, shadow_pmd,
					vm_page_prot, write_access,
					blind_protect);
			if (ret)
				goto err_unlock;
			start += PAGE_SIZE;
			len -= PAGE_SIZE;
		}

		spin_unlock(ptl);

		if (len)
			cond_resched();
	} while (len);
	ret = 0;

out:
	return ret;

err_fault:
	ret = -EFAULT;
	goto out;
err_fault_unlock:
	ret = -EFAULT;
err_unlock:
	spin_unlock(ptl);
err:
	goto out;
}

static_inline
int az_populate_large_page(az_mmstate *mms,
		struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd, pmd_t *shadow_pmd,
		pgprot_t vm_page_prot, int write_access,
		int acct, int pflags)
{
	struct page *page;
	struct mm_struct *mm = mms->mm;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, address);
	pte_t entry;

	page = az_alloc_page(mms, AZ_MM_LARGE_PAGE, acct, pflags);

	if (!page)
		goto oom;
	/* Large pages are ref'ed PTRS_PER_PMD time in AZ_MM: */
	az_mm_get_page_count(page, PTRS_PER_PMD - 1);
	atomic_long_add(PTRS_PER_PMD - 1, &mms->nr_pgrefs_taken);

	__SetPageUptodate(page);

	entry = az_mk_large_pte(page, vm_page_prot, write_access);

	/* Lock the hierarchy while we work within it */
	spin_lock(ptl);

	/* 
	 * We know the hierarchy is stable up to the pmd, but we need to check
	 * the pmd itself under lock.
	 */
	/* Verify that noone concurrently messed with our pmd or shadow_pmd */
	if (!az_pmd_none_or_clear_empty(vma, address, pmd, true))
		goto release_err;
	if (shadow_pmd && !az_pmd_none_or_clear_empty(vma, address,
				shadow_pmd, true))
		goto release_err;

	add_mm_counter(mm, MM_ANONPAGES, PTRS_PER_PMD);

	/*
	 * before we map this new page, mlock it: mimic work from
	 * mlock_vma_page, except that we don't need to remove it from any
	 * lru lists (since it hasn't been put on any) or lock it (since
	 * we had just picked it up).
	 */
	SetPageMlocked(page);
	/*
	 * Note: The code above creates an Mlocked page inside an VM_AZMM area.
	 * Our vm_area does not use VM_LOCKED since doing so will force pages
	 * in the area in during map and umap. This is accounted for correctly
	 * by ...->free_pages_check()->free_page_mlock(), where the Mlocked
	 * is cleared and NR_LOCK is accounted for in the ultimate freeing
	 * paths.
	 */

	SetPageSwapBacked(page);
	/*
	 * We do NOT create an rmap for this page. We can't keep a consistent
	 * mapping and index across remapping anyway, and we don't need it
	 * because we are Mlocked and will never be swapped out. 
	 */
	if (shadow_pmd) {
		set_pte_at(mm, address, (pte_t *)shadow_pmd, entry);
	}
	set_pte_at(mm, address, (pte_t *)pmd, entry);

	update_mmu_cache(vma, address, &entry);

	spin_unlock(ptl);
	return 0;

release_err:
	/* Large pages are ref'ed PTRS_PER_PMD time in AZ_MM: */
	az_mm_put_page_count(page, PTRS_PER_PMD - 1);
	atomic_long_add(PTRS_PER_PMD - 1, &mms->nr_pgrefs_released);
	az_remove_page(mms, page, pflags);
	spin_unlock(ptl);
	return -EFAULT;
oom:
	return -ENOMEM;
}

static_inline
int az_populate_large_pages(struct vm_area_struct *vma,
		unsigned long start, size_t len, pgprot_t vm_page_prot,
		int write_access, int active_shadow,
		int acct, int pflags)
{
	struct task_struct *tsk = current;
	az_mmstate *mms = az_vma_mmstate(vma);
	struct mm_struct *mm = current->mm;
	int ret;

	if (len <= 0)
		return 0;

	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	do {
		pmd_t *pmd, *shadow_pmd;

		/*
		 * If tsk is ooming, cut off its access to large memory
		 * allocations. It has a pending SIGKILL, but it can't
		 * be processed until returning to user space.
		 */
		if (unlikely(test_tsk_thread_flag(tsk, TIF_MEMDIE)))
			return -ENOMEM;

		/* Make sure hierarchy chain to pmd is populated */
		pmd = az_alloc_pmd(vma, mm->pgd,
				active_shadow ? mms->shadow_pgd : NULL, start);
		if (!pmd)
			return -ENOMEM;

		shadow_pmd = NULL;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(vma, start, &shadow_pmd);
			if (ret)
				return ret;
		}

		ret = az_populate_large_page(mms, vma, start, pmd, shadow_pmd,
				vm_page_prot, write_access, acct, pflags);
		if (ret)
			return ret;

		start += PMD_SIZE;
		len -= PTRS_PER_PMD;

		if (len)
			cond_resched();
	} while (len);
	return 0;
}

/* 
 * az_populate_small_page()
 *
 * Create a pte entry for the needed page, allocate a physical page
 * for it, and populate the page table accordingly.
 *
 * We assume that at least a non_exclusive mmap_sem is held, to
 * exclude vma changes.
 */
static_inline
int az_populate_small_page(az_mmstate *mms,
		struct vm_area_struct *vma, unsigned long address,
		pmd_t *pmd, pmd_t *shadow_pmd,
		pgprot_t vm_page_prot, int write_access,
		int acct, int pflags)
{
	struct page *page;
	pte_t *pte, *shadow_pte;
	struct mm_struct *mm = mms->mm;
	spinlock_t *ptl = az_mm_pmd_lockptr(mms, address);
	pte_t entry;

	page = az_alloc_page(mms, AZ_MM_SMALL_PAGE, acct, pflags);
	if (!page)
		goto oom;
	__SetPageUptodate(page);

	entry = mk_pte(page, vm_page_prot);
	/* For AZMM we force PRESENT even for PROT_NONE. */
	entry = az_pte_mkpresent(entry);
	if (write_access)
		entry = pte_mkwrite(pte_mkdirty(entry));

	/* Lock the hierarchy while we work within it */
	spin_lock(ptl);

	/* 
	 * We know the hierarchy is stable up to the pmd, but we need to check
	 * the pmd itself under lock.
	 */

	/* Verify noone concurrently messed with our pmd or shadow_pmd */
	if (!az_pmd_present(*pmd) || pmd_large_virtual(*pmd))
		goto release_err;
	if (shadow_pmd && 
	    (!az_pmd_present(*shadow_pmd) || pmd_large_virtual(*shadow_pmd)))
		goto release_err;

	/* 
	 * Now that we know we have a populated pmd that points to a page of
	 * ptes. Get a pte that (under lock) points to the right entry.
	 */
	pte = az_pte_offset(pmd, address);

	if (!az_pte_none(*pte))
		goto release_err;
	if (shadow_pmd) {
		shadow_pte = az_pte_offset(shadow_pmd, address);
		if (!az_pte_none(*shadow_pte))
			goto release_err;
	} else 
		shadow_pte = NULL;

	inc_mm_counter(mm, MM_ANONPAGES);

	/*
	 * before we map this new page, mlock it: mimic work from
	 * mlock_vma_page, except that we don't need to remove it from any
	 * lru lists (since it hasn't been put on any) or lock it (since we
	 * had just picked it up).
	 */
	SetPageMlocked(page);

	/* Don't add_page_to_unevictable_list(page), we keep lru for own use. */
	/*
	 * Note: The code above creates an Mlocked page inside an VM_AZMM area.
	 * Our vm_area does not use VM_LOCKED since doing so will force pages
	 * in the area in during map and umap. This is accounted for correctly
	 * by ...->free_pages_check()->free_page_mlock(), where the Mlocked
	 * is cleared and NR_LOCK is accounted for in the ultimate freeing
	 * paths.
	 */

	SetPageSwapBacked(page);
	/*
	 * We do NOT create an rmap for this page. We can't keep a consistent
	 * mapping and index across remapping anyway, and we don't need it
	 * because we are Mlocked and will never be swapped out. 
	 */
	if (shadow_pte != NULL) {
		set_pte_at(mm, address, shadow_pte, entry);
	}
	set_pte_at(mm, address, pte, entry);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, address, &entry);

	spin_unlock(ptl);
	return 0;

release_err:
	az_remove_page(mms, page, pflags);
	spin_unlock(ptl);
	return -EFAULT;
oom:
	return -ENOMEM;
}

static_inline
int az_populate_small_pages(struct vm_area_struct *vma,
		unsigned long start, size_t len, pgprot_t vm_page_prot,
		int write_access, int active_shadow,
		int acct, int pflags)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = current->mm;
	az_mmstate *mms = az_vma_mmstate(vma);
	pgtable_t new_shadow_pte_page = NULL;
	int ret;

	if (len <= 0)
		return 0;

	if (unlikely(anon_vma_prepare(vma)))
		goto err_nomem;

	do {
		pmd_t *pmd, *shadow_pmd;

		/*
		 * If tsk is ooming, cut off its access to large memory
		 * allocations. It has a pending SIGKILL, but it can't
		 * be processed until returning to user space.
		 */
		if (unlikely(test_tsk_thread_flag(tsk, TIF_MEMDIE)))
			goto err_nomem;

		/* Make sure hierarchy chain to pmd is populated */
		pmd = az_alloc_pmd(vma, mm->pgd, 
				active_shadow ? mms->shadow_pgd : NULL, start);
		if (!pmd)
			goto err_nomem;

		/* Get shadow pmd if there is an active shadow */
		shadow_pmd = NULL;
		if (active_shadow) {
			ret = az_mm_lookup_shadow_pmd(vma, start, &shadow_pmd);
			if (ret)
				goto err;
		}

		/* [potentially] Populate pte levels */
		ret = az_pte_alloc(mms, pmd, shadow_pmd, start);
		if (ret)
			goto err;

		ret = az_populate_small_page(mms, vma, start, pmd, shadow_pmd,
				vm_page_prot, write_access,
				acct, pflags);
		if (ret)
			goto err;

		start += PAGE_SIZE;
		len --;

		if (len)
			cond_resched();
	} while (len);

	ret = 0;
out:
	if (new_shadow_pte_page)
		az_pte_free_never_used(mms, new_shadow_pte_page);
	return ret;
err_nomem:
	ret = -ENOMEM;
err:
	goto out;

}

unsigned long do_az_mreserve(unsigned long addr, size_t len, int flags,
		struct vm_area_struct *aliased_vma)
{
	struct vm_area_struct * vma;
	az_mmstate *mms;

	int az_flags =  MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED;
	int az_prot = PROT_NONE;
	unsigned long ret;
	int batchable = flags & AZMM_BATCHABLE;
	int aliasable = flags & AZMM_ALIASABLE;
	unsigned long az_mm_flags = flags & (AZMM_BATCHABLE | AZMM_ALIASABLE);

	printk("Entered do_az_mreserve\n");

	if ((addr & ~PMD_MASK) || (len & ~PMD_MASK))
		return -EINVAL;
	if ((batchable || aliasable) &&
			((addr & ~PUD_MASK) || (len & ~PUD_MASK)))
		return -EINVAL;
	if (flags & ~AZMM_MRESERVE_SUPPORTED_FLAGS)
		return -EINVAL;
	if (aliased_vma) {
		if (!aliasable)
			return -EINVAL;
		/* aliasing a vma. verify compatibility. */
		if (!az_mm_aliasable_vma(aliased_vma))
			return -EINVAL;
		if ((batchable && !az_mm_batchable_vma(aliased_vma)) ||
		    (!batchable && az_mm_batchable_vma(aliased_vma)))
			return -EINVAL;
		if (len != (aliased_vma->vm_end - aliased_vma->vm_start))
			return -EINVAL;
	}

	/* Check if requested are overlaps with any existing vm_area: */
	vma = find_vma_intersection(current->mm, addr, addr + len);
	if (vma) {
#ifdef CONFIG_AZMM_DEBUG
		printk(KERN_WARNING "VMA intersection was detected \n");
		ret = az_mm_create_vmem_map_dump(vma);
		return (ret == 0) ? -EFAULT : ret;
#else
		return -EFAULT;
#endif
	}

	mms = az_mm_mmstate(current->mm);
	if (!mms || az_mm_init_spinlocks(mms))
		return -ENOMEM;

	ret = do_mmap_pgoff(NULL, addr, len, az_prot, az_flags, 0, 0);

	if ((long)ret > 0) {
		/* do_mmap_pgoff() returns >0 if successful. scrub the ret.*/
		vma = find_vma(current->mm, addr);
		if (!vma || (addr < vma->vm_start))
			goto err_fault;
		/* If our vma got merged with anything, slice it back down: */
		if (addr != vma->vm_start) {
			ret = split_vma(current->mm, vma, addr, 1);
			if (ret)
				goto err;
		}
		if ((addr + len) != vma->vm_end) {
			ret = split_vma(current->mm, vma, addr + len, 0);
			if (ret)
				goto err;
		}
		/* At this point, vma is supposed to exactly match our range: */
		if (vma && (addr == vma->vm_start) &&
				((addr + len) == vma->vm_end)) {
			ret = az_mm_init_module_vma(vma, NULL);
			if (ret)
				goto err;
			az_vma_vmstate(vma)->az_mm_flags = az_mm_flags;
			/* Only dump the original reservation */
			/* GB ! How to support VM_ALWAYSDUMP behaviour ?
			if (aliased_vma) {
				vma->vm_flags |= AZMM_VM_FLAGS;
				vma->vm_flags &= ~VM_ALWAYSDUMP;
			} else {
				vma->vm_flags |=
					(AZMM_VM_FLAGS | VM_ALWAYSDUMP);
			}
			*/

			// GB compile HACK
			vma->vm_flags |= AZMM_VM_FLAGS;
		} else {
			/* We somehow didn't get the vma we wanted: */
			goto err;
		}

		if (!(az_mm_flags & AZMM_ALIASABLE))
			goto out;

		/* Aliasable resevration handing: */
		if (aliased_vma) {
			az_vmstate *aliased_vmstate =
				az_vma_vmstate(aliased_vma);
			/* Drill down to the root vma of the alias group */
			if (aliased_vmstate->aliased_vma_head)
				aliased_vma = aliased_vmstate->aliased_vma_head;
			/* One drill down step should be all it takes: */
			BUG_ON(az_vma_vmstate(aliased_vma)->aliased_vma_head);

			az_vma_vmstate(vma)->aliased_vma_head = aliased_vma;

			/* push this vma at the head of the aliased vma list: */
			az_vma_vmstate(vma)->next_aliased_vma =
				az_vma_vmstate(aliased_vma)->next_aliased_vma;
			az_vma_vmstate(aliased_vma)->next_aliased_vma = vma;

			/* Populate aliased puds at creation time: */
			ret = az_populate_aliased_vma_puds(vma);
			if (ret)
				goto err;
		}
	}
out:
	return ret;
err_fault:
	ret = -EFAULT;
err:
	do_munmap(current->mm, addr, len);
	goto out;
}

unsigned long
do_az_mreserve_alias(unsigned long addr, unsigned long existing_addr)
{
	struct vm_area_struct *existing_vma;
	int flags = AZMM_ALIASABLE;
	size_t len;

	existing_vma = find_vma(current->mm, existing_addr);
	if (!existing_vma || (existing_addr != existing_vma->vm_start))
		return -EINVAL;
	if (!az_mm_aliasable_vma(existing_vma))
		return -EINVAL;

	len = existing_vma->vm_end - existing_vma->vm_start;

	if (az_mm_batchable_vma(existing_vma))
		flags |= AZMM_BATCHABLE;

	return do_az_mreserve(addr, len, flags, existing_vma);
}

//#define AZ_MM_DEBUG 1
unsigned long do_az_munreserve(unsigned long addr, size_t len)
{
	struct vm_area_struct * vma;
	int ret;

	vma = find_vma(current->mm, addr);

	if (!vma)
		return -EINVAL;
	if (!az_mm_vma(vma))
		return -EINVAL;
	/* Verify range exactly matches this VMA */
	if (addr != vma->vm_start)
		return -EINVAL;
	if (addr + len != vma->vm_end)
		return -EINVAL;
#ifdef AZ_MM_DEBUG
	if (current->mm->azm_acct_pool)
		az_pmem_pool_stats(&current->mm->azm_acct_pool->azm_pool);
#endif /* AZ_MM_DEBUG */
	ret = do_munmap(current->mm, addr, len);
#ifdef AZ_MM_DEBUG
	if (current->mm->azm_acct_pool)
		az_pmem_pool_stats(&current->mm->azm_acct_pool->azm_pool);
#endif /* AZ_MM_DEBUG */
	return ret;
}

int do_az_mmap(unsigned long addr, size_t len, int prot,
		int acct, int flags)
{
	int ret;
	struct vm_area_struct * vma;
	pgprot_t vm_page_prot;
	int active_shadow;
	int use_large_pages = flags & AZMM_LARGE_PAGE_MAPPINGS;

	if (flags & ~AZMM_MMAP_SUPPORTED_FLAGS)
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	vma = find_vma(current->mm, addr);
	if (!vma)
		return -EFAULT;
	if (!az_mm_vma(vma))
		return -EFAULT;
	active_shadow = az_mm_active_shadow(vma);
	/* Verify range is fully within this VMA */
	if (addr < vma->vm_start)
		return -EFAULT;
	if (addr + len > vma->vm_end)
		return -EFAULT;

	vm_page_prot = vm_get_page_prot(calc_vm_prot_bits(prot));

	if (use_large_pages) {
		if ((addr & ~PMD_MASK) || (len & ~PMD_MASK))
			return -EINVAL;
		ret = az_populate_large_pages(vma, addr, (len/PAGE_SIZE),
				vm_page_prot, (prot & PROT_WRITE),
				active_shadow, acct, flags);
	} else {
		if ((addr & ~PAGE_MASK) || (len & ~PAGE_MASK))
			return -EINVAL;
		ret = az_populate_small_pages(vma, addr, (len/PAGE_SIZE),
				vm_page_prot, (prot & PROT_WRITE),
				active_shadow, acct, flags);
	}
	return ret;
}

int do_az_mprotect(unsigned long addr, size_t len, int prot,
		int flags, int shadow_only)
{
	int ret;
	struct vm_area_struct * vma;
	pgprot_t vm_page_prot;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);
	int active_shadow;
	int ok_to_shatter = flags & AZMM_MAY_SHATTER_LARGE_MAPPINGS;
	int blind_protect = flags & AZMM_BLIND_PROTECT;
	az_mmstate *mms;

	if (!len || len > TASK_SIZE)
		return -EINVAL;

	if ((addr & ~PAGE_MASK) || (len & ~PAGE_MASK))
		return -EINVAL;
	if (flags & ~AZMM_MPROTECT_SUPPORTED_FLAGS)
		return -EINVAL;
	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;
	vma = find_vma(current->mm, addr);
	if (!vma)
		return -EFAULT;
	if (!az_mm_vma(vma))
		return -EFAULT;
	active_shadow = az_mm_active_shadow(vma);
	mms = az_vma_mmstate(vma);
	if (shadow_only && !mms->shadow_pgd_populated) 
		return -EFAULT;
	/* Verify range is fully within this VMA */
	if (addr < vma->vm_start)
		return -EFAULT;
	if (addr + len > vma->vm_end)
		return -EFAULT;
	vm_page_prot = vm_get_page_prot(calc_vm_prot_bits(prot));
	mmu_notifier_invalidate_range_start(current->mm, addr, addr + len);
	ret = az_protect_pages(vma, addr, len, vm_page_prot,
			(prot & PROT_WRITE), shadow_only, active_shadow,
			ok_to_shatter, blind_protect);
	mmu_notifier_invalidate_range_end(current->mm, addr, addr + len);
	az_finish_mmu(mms, tlb_invalidate);

	if (ret)
		return ret;
	return 0;
}

int do_az_munmap(unsigned long addr, size_t len, int flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int ret;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);
	int active_shadow;

	if (!len || len > TASK_SIZE)
		return -EINVAL;

	if ((addr & ~PAGE_MASK) || (len & ~PAGE_MASK))
		return -EINVAL;

	if (flags & ~AZMM_MUNMAP_SUPPORTED_FLAGS)
		return -EINVAL;

	vma = find_vma(mm, addr);
	if (!vma)
		return -EFAULT;
	if (!az_mm_vma(vma))
		return -EFAULT;
	active_shadow = az_mm_active_shadow(vma);
	/* Verify range is fully within this VMA */
	if (addr < vma->vm_start)
		return -EFAULT;
	if (addr + len > vma->vm_end)
		return -EFAULT;

	/* borrowed/modified logic from unmap_region() */
	lru_add_drain();
	update_hiwater_rss(mm);
	ret =  az_unmap_vma(vma, addr, addr + len, flags, active_shadow);
	az_finish_mmu(az_vma_mmstate(vma), tlb_invalidate);

	/* we suceeded only if we actually managed to unmap the whole set: */
	return ret;
}

int do_az_mremap(unsigned long old_addr, unsigned long new_addr,
		size_t len, int flags, int move_shadow_only)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *old_vma;
	struct vm_area_struct *new_vma;
	unsigned long moved_len;
	int tlb_invalidate = !(flags & AZMM_NO_TLB_INVALIDATE);
	int ret;
	int batchable_new, batchable_old;
	int active_shadow;
	int ok_to_shatter = flags & AZMM_MAY_SHATTER_LARGE_MAPPINGS;
	int retain_src = flags & AZMM_RETAIN_SRC_MAPPINGS;
	az_mmstate *mms;

	if (!len || len > TASK_SIZE)
		return -EINVAL;

	if ((old_addr & ~PAGE_MASK) ||
			(new_addr & ~PAGE_MASK) ||
			(len & ~PAGE_MASK))
		return -EINVAL;

	if (flags & ~AZMM_MREMAP_SUPPORTED_FLAGS)
		return -EINVAL;
	
	if(move_shadow_only && retain_src)
		return -EINVAL;

	/* Veify ranges do not overlap */
	if (old_addr > new_addr) {
		if (old_addr < new_addr + len)
			return -EINVAL;
	} else {
		if (new_addr < old_addr + len)
			return -EINVAL;
	}

	/* Verify VMAs */
	old_vma = find_vma(mm, old_addr);
	if (!old_vma)
		return -EFAULT;
	if (!az_mm_vma(old_vma))
		return -EFAULT;
	batchable_old = az_mm_batchable_vma(old_vma);

	new_vma = find_vma(mm, new_addr);
	if (!new_vma)
		return -EFAULT;
	if (!az_mm_vma(new_vma))
		return -EFAULT;
	batchable_new = az_mm_batchable_vma(new_vma);

	/* cannot remap across batchable and non-batchable vmas */
	if ((batchable_old && !batchable_new) ||
			(!batchable_old && batchable_new))
		return -EFAULT;

	/* shadow_only remaps must target batchable areas */
	if (move_shadow_only && (!batchable_old || !batchable_new))
		return -EFAULT;

	/* Verify range is fully within this VMAs */
	if (old_addr < old_vma->vm_start)
		return -EFAULT;
	if (old_addr + len > old_vma->vm_end)
		return -EFAULT;
	if (new_addr < new_vma->vm_start)
		return -EFAULT;
	if (new_addr + len > new_vma->vm_end)
		return -EFAULT;

	active_shadow = az_mm_active_shadow(old_vma);
	mms = az_vma_mmstate(old_vma);
	if (move_shadow_only && !mms->shadow_pgd_populated) 
		return -EFAULT;

	BUG_ON(old_vma->vm_mm != current->mm);
	ret = az_remap_page_tables(old_vma, new_vma, old_addr, new_addr,
			len, move_shadow_only, &moved_len, active_shadow,
			ok_to_shatter, retain_src, tlb_invalidate);
	BUG_ON(!ret && (moved_len < len));
	az_finish_mmu(mms, tlb_invalidate);
	return ret;
}

int do_az_munshatter(unsigned long addr, unsigned long force_addr,
		unsigned long resource_addr, unsigned long src_addr,
		int prot, int acct, int flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_area_struct *resource_vma;
	int ret;
	int active_shadow;

	if ((addr & ~PMD_MASK) ||
			(resource_addr & ~PMD_MASK))
		return -EINVAL;

	if (flags & ~AZMM_MUNSHATTER_SUPPORTED_FLAGS)
		return -EINVAL;

	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	force_addr &= PAGE_MASK;

	/* force_addr, if specified, must be within addr's large page: */
	if (force_addr && ((force_addr & PMD_MASK) != addr))
		return -EINVAL;

	/* src_addr, if specified, must be within addr's large page: */
	if (src_addr && ((src_addr & PMD_MASK) != addr))
		return -EINVAL;

	if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
		return -EINVAL;

	/* Verify VMAs */
	vma = find_vma(mm, addr);
	if (!vma)
		return -EFAULT;
	if (!az_mm_vma(vma))
		return -EFAULT;
	if (addr < vma->vm_start)
		return -EFAULT;

	if (resource_addr) {
		if (resource_addr == addr)
			return -EINVAL;
		resource_vma = find_vma(mm, resource_addr);
		if (!resource_vma)
			return -EFAULT;
		if (!az_mm_vma(resource_vma))
			return -EFAULT;
		if (resource_addr < resource_vma->vm_start)
			return -EFAULT;
	}

	active_shadow = az_mm_active_shadow(vma);

	ret = az_unshatter(az_vma_mmstate(vma), vma, addr, force_addr,
			resource_addr, src_addr, active_shadow,
			prot, acct, flags);
	return ret;
}

int do_az_mcopy(unsigned long dst_addr, unsigned long src_addr,
		size_t len)
{
	struct vm_area_struct *src_vma;
	struct vm_area_struct *dst_vma;
	int ret;

	if (!len || len > TASK_SIZE)
		return -EINVAL;

	if ((src_addr & ~PAGE_MASK) ||
			(dst_addr & ~PAGE_MASK) ||
			(len & ~PAGE_MASK))
		return -EINVAL;

	/* Veify ranges do not overlap */
	if (src_addr > dst_addr) {
		if (src_addr < dst_addr + len)
			return -EINVAL;
	} else {
		if (dst_addr < src_addr + len)
			return -EINVAL;
	}

	/* Verify VMAs */
	src_vma = find_vma(current->mm, src_addr);
	if (!src_vma)
		return -EFAULT;
	if (!az_mm_vma(src_vma))
		return -EFAULT;

	dst_vma = find_vma(current->mm, dst_addr);
	if (!dst_vma)
		return -EFAULT;
	if (!az_mm_vma(dst_vma))
		return -EFAULT;

	/* Verify range is fully within these VMAs */
	if (src_addr < src_vma->vm_start)
		return -EFAULT;
	if (src_addr + len > src_vma->vm_end)
		return -EFAULT;
	if (dst_addr < dst_vma->vm_start)
		return -EFAULT;
	if (dst_addr + len > dst_vma->vm_end)
		return -EFAULT;

	ret = az_memcopy(dst_vma, dst_addr, src_addr, len);
	return ret;
}

int do_az_mbatch_start(void)
{
	struct mm_struct *mm = current->mm;
	az_mmstate *mms = az_mm_mmstate(mm);
	int ret;
	struct page *page;

	down_read(&mm->mmap_sem);
	if (!mms) {
		up_read(&mm->mmap_sem);
		return -EINVAL;
	}

	if (mms->shadow_pgd) {
		up_read(&mm->mmap_sem);
		return -EFAULT;
	}

	/* If there is a pgd hierarchy left over from a previous batch,
	 * free it here, under a read lock.
	 */
	if (mms->prev_main_pgd) {
		pgd_t *shadow_pgd = mms->prev_main_pgd;
		struct mmu_gather tlb;
		tlb_gather_mmu(&tlb, current->mm, true);
		mms->prev_main_pgd = NULL;
		az_free_shadow_pgd(&tlb, shadow_pgd);
		tlb_finish_mmu(&tlb, 0, 0);
	}

	up_read(&mm->mmap_sem);

	/* Make sure no vma changes occur during shadow initialization: */
	down_write(&mm->mmap_sem);

	/* Validate that no current batch is active, and that no new batch
	 * was done while we let go of the lock (leaving pgd stuff for
	 * us to free).
	 * */
	if (mms->shadow_pgd || mms->prev_main_pgd) {
		up_write(&mm->mmap_sem);
		return -EFAULT;
	}
	BUG_ON(mms->shadow_pgd_populated);

	/*
	 * No need to call pgd_alloc() like behavior. At least for x86, all
	 * we need is a nice empty page for the pud entries.
	 */
	page = az_alloc_page(mms, AZ_MM_PGD_PAGE, 0, 0);
	if (!page) {
		up_write(&mm->mmap_sem);
		return -ENOMEM;
	}
	mms->shadow_pgd = (pgd_t *) page_address(page);

	/* perform the populate run on the shadow under read lock to allow
	 * concurrent map manipulation operations to proceed.
	 */
	downgrade_write(&mm->mmap_sem);
	ret = az_populate_shadow_table(mms);
	up_read(&mm->mmap_sem);


	down_write(&mm->mmap_sem);

	/* We let go of the mmap_sem, and all sorts of things could have
	 * happened.  Re-validate that there is no active shadow, that no
	 * other shadow work was done and left over for us to deal with,
	 * and that noone tore down the shadow_pgd while we were gone
	 * This could happen if, for example, while we were gone a full
	 * abort was done and then possibly followed by a new, sucessful start.
	 */
	if (mms->shadow_pgd_populated ||
	    mms->prev_main_pgd ||
	    !mms->shadow_pgd) {
		up_write(&mm->mmap_sem);
		return -EFAULT;
	}

	/* Deal with populate errors: */
	if (ret) {
		struct mmu_gather tlb;
		pgd_t *shadow_pgd = mms->shadow_pgd;
		mms->shadow_pgd = NULL;
		downgrade_write(&mm->mmap_sem);
		if (shadow_pgd) {
			tlb_gather_mmu(&tlb, current->mm, true);
			az_free_shadow_pgd(&tlb, shadow_pgd);
			tlb_finish_mmu(&tlb, 0, 0);
		}
		up_read(&mm->mmap_sem);
		return ret;
	}

	/* 
	 * At this point, there is a populated shadow, and VM_AZMM changes
	 * are being reflected in it. However, batch op changes have not yet
	 * been allowed. Indicate that the batch has actually succesfully
	 * completed by setting shadow_pgd_populated.
	 */
	mms->shadow_pgd_populated = true;
	up_write(&mm->mmap_sem);
	return  0; 
}

int do_az_mbatch_remap(unsigned long old_addr, unsigned long new_addr,
		size_t len, int flags)
{
	int ret;
	struct mm_struct *mm = current->mm;
	down_read(&mm->mmap_sem);
	/* Validate that a current, fully populated batch is active */
	ret = do_az_mremap(old_addr, new_addr, len,
			flags | AZMM_NO_TLB_INVALIDATE,
			true /* move_shadow_only */);
	up_read(&mm->mmap_sem);
	return ret;
}

int do_az_mbatch_protect(unsigned long addr, size_t len,
		int prot, int flags)
{
	int ret;
	struct mm_struct *mm = current->mm;
	down_read(&mm->mmap_sem);
	/* Validate that a current, fully populated batch is active */
	ret = do_az_mprotect(addr, len, prot,
			flags | AZMM_NO_TLB_INVALIDATE,
			true /* shadow_only */);
	up_read(&mm->mmap_sem);
	return ret;
}

int do_az_mbatch_commit(void)
{
	struct mm_struct *mm = current->mm;
	pgd_t *shadow_pgd;
	az_mmstate *mms;

	/* 
	 * Start off with a write hold on mmap_sem, as we do the
	 * visible removal of the shadow from the mm.
	 */
	down_write(&mm->mmap_sem);

	mms = az_mm_mmstate(current->mm);
	if (!mms) {
		up_write(&mm->mmap_sem);
		return -EINVAL;
	}

	/* Validate that a current, fully populated batch is active */
	if (!mms->shadow_pgd_populated) {
		up_write(&mm->mmap_sem);
		return -EINVAL;
	}

	/* Clear mm->shadow_pgd and keep copy so we can clean it up. */
	mms->shadow_pgd_populated = false;
	shadow_pgd = mms->shadow_pgd;
	mms->shadow_pgd = NULL;

	/* Perform commit flip with mmap_sem held for write */
	az_commit_shadow_pgd(mm, shadow_pgd);
	az_pmem_flush_tlb_and_released_pages(mms);

	/*
	 * az_commit_shadow_pgd() would have exchanged PUD entries between
	 * the shadow and the main, so the shadow now has the old, flushed
	 * main entries that need to be discarded. Keep track of the old
	 * main pgd so that the next batch start (or exit) can clear it.
	 */
	mms->prev_main_pgd = shadow_pgd;

	up_write(&mm->mmap_sem);
	return 0;
}

/*
 * az_mm_terminate_mbatch() unconditionally terminates an active batch if
 * ones exists. It should be called when memory system attempts to unmap
 * a batchable vm_area or otherwise free page tables within a batchable
 * vm_area. A common cause would be a call to az_unreserve on a batchable
 * area (which will use the do_munmap() path), or a direct munmap() of one.
 *
 * Caller must hold mmap_sem write or stronger equiv. (e.g. mm_exit
 * may unmap areas and free page tables without holding the mmap_sem).
 *
 * We know no concurrent calls will be in flight. To an ongoing batch
 * (populated), this will appear as if an interleaved do_az_mbatch_abort()
 * was performed, and further attempts to manipulate the batch will fail.
 */
static void az_mm_terminate_mbatch(struct mmu_gather *tlb)
{
	struct mm_struct *mm = tlb->mm;
	az_mmstate *mms = az_mm_mmstate(mm);
	pgd_t *shadow_pgd;
	/* No option of aborting. must be done unconditionally on exit paths. */
	/* Can we break down to smaller pieces of work? */
	if (!mms)
		return;

	if (mms->prev_main_pgd) {
		/* Free leftover pgd from latest committed batch: */
		shadow_pgd = mms->prev_main_pgd;
		mms->prev_main_pgd = NULL;
		az_free_shadow_pgd(tlb, shadow_pgd);
	}

	shadow_pgd = mms->shadow_pgd;
	if (!shadow_pgd)
		return;
	mms->shadow_pgd_populated = false;
	mms->shadow_pgd = NULL;
	az_free_shadow_pgd(tlb, shadow_pgd);
}

int do_az_mbatch_abort(void)
{
	struct mm_struct *mm = current->mm;
	pgd_t *shadow_pgd;
	struct mmu_gather tlb;
	az_mmstate *mms;

	/* 
	 * Start off with a write hold on mmap_sem, as we do the
	 * visible removal of the shadow from the mm.
	 */
	down_write(&mm->mmap_sem);

	mms = az_mm_mmstate(mm);
	if (!mms) {
		up_write(&mm->mmap_sem);
		return -EINVAL;
	}

	/* Validate that a current, fully populated batch is active */
	if (!mms->shadow_pgd_populated) {
		up_write(&mm->mmap_sem);
		return -EINVAL;
	}
	BUG_ON(!mms->shadow_pgd);
	BUG_ON(mms->prev_main_pgd);

	mms->shadow_pgd_populated = false;
	/* Clear mm->shadow_pgd and keep copy so we can clean it up. */
	shadow_pgd = mms->shadow_pgd;
	mms->shadow_pgd = NULL;

	/* 
	 * downgrade mmap_sem from write to read to let concurrent ops through
	 * as we do the bulk of cleanup work, so from this point on we need to
	 * release as read...
	 */
	downgrade_write(&mm->mmap_sem);

	tlb_gather_mmu(&tlb, current->mm, true);
	az_free_shadow_pgd(&tlb, shadow_pgd);
	tlb_finish_mmu(&tlb, 0, 0);

	up_read(&mm->mmap_sem);
	return 0;
}

int do_az_tlb_invalidate(void)
{
	az_mmstate *mms = az_mm_mmstate(current->mm);
	if (!mms)
		return -EINVAL;
	down_read(&current->mm->mmap_sem);
	az_pmem_flush_tlb_and_released_pages(mms);
	up_read(&current->mm->mmap_sem);
	return 0;
}

unsigned long do_az_mprobe(struct mm_struct *mm, unsigned long addr,
		int flags, int *refcnt)
{
	if(flags & ~AZMM_MPROBE_SUPPORTED_FLAGS)
		return -EINVAL;
	if (flags & AZMM_MPROBE_SHADOW) {
		struct vm_area_struct *vma = find_vma(current->mm, addr);
		if (!vma || (addr < vma->vm_start) || !az_mm_vma(vma))
			return 0; /* nothing mapped in shadow */
		return (unsigned long)az_mm_probe_shadow_addr(vma, addr, refcnt);
	}
	return (unsigned long)az_mm_probe_addr(mm, addr, refcnt);
}

long do_az_mflush(unsigned long acct, int flags, size_t *allocated)
{
	int od_only = false;
	az_mmstate *mms = az_mm_mmstate(current->mm);

	if (!mms)
		return -EINVAL;

	if (!mms->azm_pool)
		return -ENOENT;
	if (flags & ~AZMM_FLUSH_SUPPORTED_FLAGS)
		return -EINVAL;
	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	if (flags & AZMM_FLUSH_OVERDRAFT_ONLY)
		od_only = true;
	if (!(flags & AZMM_NO_TLB_INVALIDATE))
		az_pmem_flush_tlb_and_released_pages(mms);

	return az_pmem_flush_pool_account(mms->azm_pool, acct, allocated,
			od_only);
}

/* system call interface */

int az_ioc_mreserve(unsigned long addr, size_t len, int flags)
{
	unsigned long ret;

	printk("Entered az_ioc_mreserve\n");
	down_write(&current->mm->mmap_sem);
	ret = do_az_mreserve(addr, len, flags, NULL);
	up_write(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mreserve_alias(unsigned long addr, unsigned long existing_addr)
{
	unsigned long ret;
	down_write(&current->mm->mmap_sem);
	ret = do_az_mreserve_alias(addr, existing_addr);
	up_write(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_munreserve(unsigned long addr, size_t len)
{
	unsigned long ret;
	down_write(&current->mm->mmap_sem);
	ret = do_az_munreserve(addr, len);
	up_write(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mmap(unsigned long addr, size_t len, int prot, int acct, int flags)
{
	int ret;
	down_read(&current->mm->mmap_sem);
	ret = do_az_mmap(addr, len, prot, acct, flags);
	up_read(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_munmap(unsigned long addr, size_t len, int acct, int flags)
{
	int ret;
	down_read(&current->mm->mmap_sem);
	ret = do_az_munmap(addr, len, flags);
	up_read(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mremap(unsigned long old_addr, unsigned long new_addr, size_t len,
		int flags)
{
	int ret;
	down_read(&current->mm->mmap_sem);
	ret = do_az_mremap(old_addr, new_addr, len, flags,
			false /* !shadow_only */);
	up_read(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mprotect(unsigned long addr, size_t len, int prot, int flags)
{
	int ret;
	down_read(&current->mm->mmap_sem);
	ret = do_az_mprotect(addr, len, prot, flags, false /* !shadow_only */);
	up_read(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mcopy(unsigned long dst_addr, unsigned long src_addr, size_t len)
{
	int ret;
	down_read(&current->mm->mmap_sem);
	ret = do_az_mcopy(dst_addr, src_addr, len);
	up_read(&current->mm->mmap_sem);
	return ret;
}

int az_ioc_mbatch_start(void)
{
	return do_az_mbatch_start();
}

int az_ioc_mbatch_remap(unsigned long old_addr, unsigned long new_addr,
		size_t len, int flags)
{
	return do_az_mbatch_remap(old_addr, new_addr, len, flags);
}

int az_ioc_mbatch_protect(unsigned long addr, size_t len, int prot, int flags)
{
	return do_az_mbatch_protect(addr, len, prot, flags);
}


int az_ioc_mbatch_commit(void)
{
	return do_az_mbatch_commit();
}

int az_ioc_mbatch_abort(void)
{
	return do_az_mbatch_abort();
}

int az_ioc_tlb_invalidate(void)
{
	return do_az_tlb_invalidate();
}

int az_ioc_munshatter(unsigned long addr, unsigned long force_addr,
		unsigned long resource_addr, int prot, int acct, int flags)
{
	int ret;
	unsigned long src_addr = addr;
	down_read(&current->mm->mmap_sem);
	ret = do_az_munshatter(addr, force_addr, resource_addr,
			src_addr, prot, acct, flags);
	up_read(&current->mm->mmap_sem);
	return ret;
}

long az_ioc_mprobe(pid_t pid, unsigned long addr, int flags, int __user* refcnt_p)
{
	struct mm_struct *mm = NULL;
	struct task_struct *tsk = NULL;
	long ret = -ESRCH;
	int refcnt = 0;

	if ((pid != 0) && current->pid != pid) {
		if(!capable(CAP_SYS_ADMIN))
			return -EPERM;
		read_lock(&tasklist_lock);
		tsk = find_task_by_vpid(pid);
		if (!tsk || (tsk->flags & PF_EXITING)) {
			read_unlock(&tasklist_lock);
			return -ESRCH;
		}
		get_task_struct(tsk);   /* Hold refcount for this task */
		read_unlock(&tasklist_lock);
		mm = get_task_mm(tsk);
		if (!mm)
			goto out;
	} else {
		mm = current->mm;
	}

	down_read(&mm->mmap_sem);
	ret = do_az_mprobe(mm, addr, flags, &refcnt);
	up_read(&mm->mmap_sem);

	if (!ret) {
		ret = -EFAULT;
		goto out;
	}
	if ((flags & AZMM_MPROBE_REF_COUNT) &&
		((!refcnt_p) || put_user(refcnt, refcnt_p)))
		ret = -EFAULT;

out:
	if (tsk) {
		if (mm)
			mmput(mm);
		put_task_struct(tsk);
	}
	return ret;
}

int az_ioc_mflush(int acct, int flags, size_t __user* allocatedp)
{
	long flushed;
	size_t allocated;

	down_read(&current->mm->mmap_sem);
	flushed = do_az_mflush(acct, flags, &allocated);
	up_read(&current->mm->mmap_sem);

	if (flushed < 0)
		return flushed;
	if (put_user(allocated, allocatedp))
		return -EFAULT;
	return flushed;
}
