/* az_vmem.h - Azul Virtual Memory Management 
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

#ifndef _LINUX_AZ_VMEM_H
#define _LINUX_AZ_VMEM_H

#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/elf.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

#include "az_mm.h"
#include "az_pmem.h"

#ifdef __KERNEL__

#define AZMM_NO_TLB_INVALIDATE			0x00100000
#define AZMM_BLIND_UNMAP			0x00200000
#define AZMM_BLIND_PROTECT			0x00200000
#define AZMM_LARGE_PAGE_MAPPINGS		0x00400000
#define AZMM_MAY_SHATTER_LARGE_MAPPINGS		0x00800000
#define AZMM_BATCHABLE				0x01000000
#define AZMM_ALIASABLE				0x02000000
#define AZMM_RETAIN_SRC_MAPPINGS		0x04000000

#define AZMM_MAY_RECYCLE_BEFORE_INVALIDATE	0x00000004
#define AZMM_MPROBE_SHADOW              	0x00000010
#define AZMM_MPROBE_REF_COUNT			0x08000000

#define AZMM_ALLOCATE_HEAP			0x00000200
#define AZMM_ALLOCATE_NO_OVERDRAFT		0x00001000
#define AZMM_ALLOCATE_REQUEST_NOZERO		0x00002000
#define AZMM_FLUSH_HEAP				0x00000001
#define AZMM_FLUSH_OVERDRAFT_ONLY		0x00000002

#define AZMM_FLUSH_SUPPORTED_FLAGS \
	(AZMM_FLUSH_HEAP | AZMM_FLUSH_OVERDRAFT_ONLY | AZMM_NO_TLB_INVALIDATE)

#define AZMM_MRESERVE_SUPPORTED_FLAGS (AZMM_BATCHABLE | AZMM_ALIASABLE)

#define AZMM_MMAP_SUPPORTED_FLAGS \
	(AZMM_LARGE_PAGE_MAPPINGS | AZMM_ALLOCATE_HEAP | \
	 AZMM_ALLOCATE_NO_OVERDRAFT | AZMM_ALLOCATE_REQUEST_NOZERO | \
	 AZMM_MAY_RECYCLE_BEFORE_INVALIDATE)

#define AZMM_MREMAP_SUPPORTED_FLAGS \
	(AZMM_NO_TLB_INVALIDATE | AZMM_MAY_SHATTER_LARGE_MAPPINGS | \
	 AZMM_RETAIN_SRC_MAPPINGS)

#define AZMM_MUNMAP_SUPPORTED_FLAGS \
	(AZMM_NO_TLB_INVALIDATE | AZMM_MAY_SHATTER_LARGE_MAPPINGS | \
	 AZMM_BLIND_UNMAP | AZMM_MAY_RECYCLE_BEFORE_INVALIDATE)

#define AZMM_MPROTECT_SUPPORTED_FLAGS \
	(AZMM_NO_TLB_INVALIDATE | AZMM_MAY_SHATTER_LARGE_MAPPINGS | \
	 AZMM_BLIND_PROTECT)

#define AZMM_MUNSHATTER_SUPPORTED_FLAGS \
	(AZMM_NO_TLB_INVALIDATE | AZMM_ALLOCATE_HEAP | \
	 AZMM_ALLOCATE_NO_OVERDRAFT | AZMM_ALLOCATE_REQUEST_NOZERO | \
	 AZMM_MAY_RECYCLE_BEFORE_INVALIDATE)

#define AZMM_MPROBE_SUPPORTED_FLAGS \
	(AZMM_MPROBE_SHADOW | AZMM_MPROBE_REF_COUNT)

#define LARGE_PHYSICAL_PAGE_ORDER (PMD_SHIFT - PAGE_SHIFT)

#define AZMM_VM_FLAGS (VM_DONTCOPY | VM_READ | VM_WRITE | VM_EXEC)

#define az_mm_vma(vma) \
	((vma)->mm_module_ops == &az_mm_module_ops)

#define az_mm_batchable_vma(vma) \
	(az_mm_vma(vma) && (az_vma_vmstate(vma)->az_mm_flags & AZMM_BATCHABLE))

#define az_mm_aliasable_vma(vma) \
	(az_mm_vma(vma) && (az_vma_vmstate(vma)->az_mm_flags & AZMM_ALIASABLE))

#define az_mm_active_shadow(vma) \
	(az_mm_batchable_vma(vma) && az_vma_mmstate(vma)->shadow_pgd)

/*
 * az_spinlock_t created to store spinlocks in cache_line_aligned spacing
 * Assumes spinlock can fit in single L1_CACHE_BYTES unit, and asserts
 * this with BUG_ON in az_init_az_mm_spinlocks();
 */

#define AZ_MM_PTL_STRIPE_SHIFT PMD_SHIFT
#define AZ_MM_NPTLS_PER_MM (PAGE_SIZE / sizeof(struct az_mm_spinlock))

#define az_mm_ptl_index(addr) \
	(((addr) >> AZ_MM_PTL_STRIPE_SHIFT) & (AZ_MM_NPTLS_PER_MM - 1))

#define az_mm_pmd_lockptr(mms, address) \
	(&((mms)->az_mm_ptls[az_mm_ptl_index(address)].spl))

#define az_same_pte_page(pte1, pte2) \
	(pte_page(*(pte1)) == pte_page(*(pte2)))

#define az_same_pmd_page(pmd1, pmd2) \
	(az_same_pte_page((pte_t *)(pmd1), ((pte_t *)(pmd2))))

/* Define our own _none and _present tests. The kernel's pte_present()
 * annoyingly returns true for PROT_NONE pages, so we'll use our own variants
 * everywhere for consistency.
 */
#define az_pte_none(x)		(!pte_val((x)))
#define az_pte_present(x)	(pte_val((x)) & _PAGE_PRESENT)
#define az_pmd_none(x)		(!pmd_val((x)))
#define az_pmd_present(x)	(pmd_val((x)) & _PAGE_PRESENT)

/* Define our own pte_offset() function.
 * az_mm requires x86-64, which does not have a use for pte_map()/pte_unmap()
 * and nested pairs, as it always has all page tables mapped.
 * Since we may often need to hold 3 or more ptes at the same time (e.g.
 * remaps with active shadows), 2-level nesting (which is what e.g. x86-32
 * with CONFIG_HIGHTABLE offers) wouldn't be enough anyway, and we would
 * have needed 3 or 4 level nesting to deal with page table architectures
 * that do not have all levels mapped at all times.
 * Bottom line: Always use az_pte_offset() [instead of pte_offset_map()], and
 * never use pte_unmap().
 */
#define __az_pte_offset(dir, address) pte_offset_kernel((dir), (address))

static inline void az_mm_pmd_lockptr_pair(az_mmstate *mms,
		unsigned long addr1, unsigned long addr2,
		spinlock_t **pmdl_low, spinlock_t **pmdl_high)
{
	spinlock_t *pmd1, *pmd2;
	pmd1 = az_mm_pmd_lockptr(mms, addr1);
	pmd2 = az_mm_pmd_lockptr(mms, addr2);
	if (pmd1 < pmd2) {
		*pmdl_low = pmd1;
		*pmdl_high = pmd2;
	} else {
		*pmdl_low = pmd2;
		*pmdl_high = pmd1;
	}
}

static inline void az_mm_spin_lock_pair(spinlock_t *spl1, spinlock_t *spl2)
{
	if (unlikely(spl1 == spl2)) {
		spin_lock(spl1);
	} else {
		spin_lock(spl1);
		spin_lock_nested(spl2, SINGLE_DEPTH_NESTING);
	}
}

static inline void az_mm_spin_unlock_pair(spinlock_t *spl1, spinlock_t *spl2)
{
	if (unlikely(spl1 == spl2)) {
		spin_unlock(spl1);
	} else {
		spin_unlock(spl2);
		spin_unlock(spl1);
	}
}

/*
 * az_shadow_pgd_offset returns pgd_t in the shadow for matching address
 * if there is a batch open (valid shadow_pgd), or NULL if there isn't
 */
#define __az_pgd_offset(pgd_root, address)    \
	((pgd_root) ? (pgd_root) + pgd_index((address)) : NULL)

#define az_pte_alloc_map(mm, pmd, address)     \
	((unlikely(!az_pmd_present(*(pmd))) && __pte_alloc(mm, pmd, address))? \
	 NULL: pte_offset_map(pmd, address))

static inline int az_mm_is_large_page(struct page *page)
{
	BUG_ON(!az_pmem_managed_page(page));
	return az_pmem_is_large_page(page);
}

static inline struct page *az_mm_large_page_head(struct page *page)
{
	BUG_ON(!az_pmem_managed_page(page));
	return az_pmem_large_page_head(page);
}

static inline void az_mm_get_page_count(struct page *page, int count)
{
	BUG_ON(!az_pmem_managed_page(page));
	page = az_mm_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	atomic_add(count, &page->_count);
}

static inline void az_mm_get_page(struct page *page)
{
	BUG_ON(!az_pmem_managed_page(page));
	page = az_mm_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	atomic_inc(&page->_count);
}

static inline int az_mm_put_page_count(struct page *page, int count)
{
	int c;
	BUG_ON(!az_pmem_managed_page(page));
	page = az_mm_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) <= count);
	c = atomic_sub_and_test(count, &page->_count);
	VM_BUG_ON(c);
	return c;
}

static inline int pmd_large_virtual(pmd_t pte)
{
	return (pmd_val(pte) & (_PAGE_PSE | _PAGE_PRESENT)) ==
		(_PAGE_PSE | _PAGE_PRESENT);
}

static inline int az_pte_read_or_exec(pte_t pte)
{
	return pte_flags(pte) & _PAGE_USER;
}

static inline pte_t pte_mklarge_virtual(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_PSE);
}

static inline pte_t pte_clrlarge_virtual(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_PSE);
}

static inline pte_t az_pte_mkpresent(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_PRESENT);
}

static inline pte_t az_pte_clrpresent(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_PRESENT);
}

static inline int az_pmd_bad(pmd_t pmd)
{
	return (pmd_val(pmd) & ~(PTE_PFN_MASK | _PAGE_USER)) != _KERNPG_TABLE;
}

static inline int az_pmd_none_or_clear_bad(pmd_t *pmd)
{
	if (az_pmd_none(*pmd))
		return 1;       
	if (unlikely(az_pmd_bad(*pmd))) {
		pmd_clear_bad(pmd);
		return 1;
	}
	return 0;
}

static inline int az_pud_bad(pud_t pud)
{
	return (pud_flags(pud) & ~(_KERNPG_TABLE | _PAGE_USER)) != 0;
}

static inline int az_pud_none_or_clear_bad(pud_t *pud)
{
	if (pud_none(*pud))
		return 1;
	if (unlikely(az_pud_bad(*pud))) {
		pud_clear_bad(pud);
		return 1;
	}
	return 0;
}

static inline int az_pgd_bad(pgd_t pgd)
{
	return (pgd_flags(pgd) & ~_PAGE_USER) != _KERNPG_TABLE;
}

static inline int az_pgd_none_or_clear_bad(pgd_t *pgd)
{
	if (pgd_none(*pgd))
		return 1;
	if (unlikely(az_pgd_bad(*pgd))) {
		pgd_clear_bad(pgd);
		return 1;
	}
	return 0;
}

#define az_pte_ERROR(e)					\
	printk("%s:%d: bad pte %p(%016lx).\n",		\
	       __FILE__, __LINE__, &(e), pte_val(e))
#define az_pte_pfn(pte)	((pte_val((pte)) & ~_PAGE_NX) >> PAGE_SHIFT)
#define az_pmd_pfn(pmd)	((pmd_val((pmd)) & ~_PAGE_NX) >> PAGE_SHIFT)
#define az_pud_pfn(pud)	(pud_val((pud)) >> PAGE_SHIFT)
#define az_pgd_pfn(pgd)	(pgd_val((pgd)) >> PAGE_SHIFT)

static inline pte_t* az_pte_offset(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;

	AZMM_DETAILBUG_IF(pmd_none(*pmd) || !pmd_present(*pmd) ||
			pmd_large_virtual(*pmd) ||
			az_pmd_bad(*pmd) || !pfn_valid(pmd_pfn(*pmd))) {
		pmd_clear_bad(pmd);
		printk(KERN_DEBUG "AZMM: az_pte_offset(0x%016lx, 0x%016lx)\n",
				(unsigned long)pmd, addr);
		BUG();
	}
	pte = __az_pte_offset(pmd, addr);
	AZMM_DETAILBUG_IF(!pfn_valid(az_pte_pfn(*pte))) {
		az_pte_ERROR(*pte);
		BUG();
	}
	return pte;
}

static inline pmd_t* az_pmd_offset(pud_t *pud, unsigned long addr)
{
	pmd_t *pmd;

	AZMM_DETAILBUG_IF(pud_none(*pud) || !pud_present(*pud) ||
			pud_bad(*pud) || !pfn_valid(az_pud_pfn(*pud))) {
		pud_clear_bad(pud);
		printk(KERN_DEBUG "AZMM: az_pmd_offset(0x%016lx, 0x%016lx)\n",
				(unsigned long)pud, addr);
		BUG();
	}
	pmd = pmd_offset(pud, addr);
	AZMM_DETAILBUG_IF((!pmd_none(*pmd) && pmd_present(*pmd)) && 
			((!pmd_large_virtual(*pmd) && az_pmd_bad(*pmd)) ||
			!pfn_valid(az_pmd_pfn(*pmd)))) {
		pmd_clear_bad(pmd);
		BUG();
	}
	return pmd;
}

static inline pud_t* az_pud_offset(pgd_t *pgd, unsigned long addr)
{
	pud_t *pud;

	AZMM_DETAILBUG_IF(pgd_none(*pgd) || !pgd_present(*pgd) ||
			pgd_bad(*pgd) || !pfn_valid(az_pgd_pfn(*pgd))) {
		pgd_clear_bad(pgd);
		printk(KERN_DEBUG "AZMM: az_pud_offset(0x%016lx, 0x%016lx)\n",
				(unsigned long)pgd, addr);
		BUG();
	}
	pud = pud_offset(pgd, addr);
	AZMM_DETAILBUG_IF((!pud_none(*pud) && pud_present(*pud)) &&
			(pud_bad(*pud) || !pfn_valid(az_pud_pfn(*pud)))) {
		pud_clear_bad(pud);
		BUG();
	}
	return pud;
}

static inline pgd_t* az_pgd_offset(pgd_t *pgd_root, unsigned long addr)
{
	pgd_t *pgd;

	pgd = __az_pgd_offset(pgd_root, addr);
	if (!pgd)
		return pgd;
	AZMM_DETAILBUG_IF((!pgd_none(*pgd) && pgd_present(*pgd)) &&
			(pgd_bad(*pgd) || !pfn_valid(az_pgd_pfn(*pgd)))) {
		pgd_clear_bad(pgd);
		printk(KERN_DEBUG "AZMM: az_pgd_offset(0x%016lx, 0x%016lx)\n",
				(unsigned long)pgd_root, addr);
		BUG();
	}
	return pgd;
}

static inline pte_t az_mk_large_pte(struct page *page, pgprot_t pgprot,
                                    int writable)
{
	pte_t entry;

	entry = mk_pte(page, pgprot);
	/* For AZMM we force PRESENT even for PROT_NONE. */
	entry = az_pte_mkpresent(entry);
	if (writable) {
		entry = pte_mkwrite(pte_mkdirty(entry));
	}
	entry = pte_mklarge_virtual(entry);

	return entry;
}

#define _AZ_PAGE_CHG_MASK (_PAGE_CHG_MASK | _PAGE_PSE)

static inline pte_t az_pte_modify_large(pte_t pte, pgprot_t newprot)
{
	pteval_t val = pte_val(pte);
	val &= _AZ_PAGE_CHG_MASK;
	val |= pgprot_val(newprot) & (~_AZ_PAGE_CHG_MASK) &
		__supported_pte_mask;
	/* For AZMM we force PRESENT even for PROT_NONE. */
	val |= _PAGE_PRESENT;
	return __pte(val);
}

static inline int az_pmd_needs_shattering(unsigned long addr, unsigned long len,
        pmd_t *pmd, pmd_t *shadow_pmd)
{
	/* shatter if both main and shadow exist but don't match in size. */
	if (pmd && shadow_pmd &&
	    (pmd_large_virtual(*pmd) != pmd_large_virtual(*shadow_pmd)))
		return true;
	/* shatter if not aligned and either main or shadow is large: */
	return (((addr & ~PMD_MASK) || (len < PMD_SIZE)) &&
	        ((pmd && pmd_large_virtual(*pmd)) ||
		 (shadow_pmd && pmd_large_virtual(*shadow_pmd))));
}

#define AZ_MM_PTE_PROT_MASK (_PAGE_USER | _PAGE_RW | _PAGE_NX)
#define AZ_MM_PTE_PROTSCAN_MASK \
	(_PAGE_USER | _PAGE_RW | _PAGE_NX | _PAGE_PSE | _PAGE_PRESENT)

static inline unsigned long az_mm_pteval_to_vm_flags(pteval_t pteval)
{
	unsigned long vm_flags;
	vm_flags = pteval & _PAGE_USER ? VM_READ : 0;
	vm_flags |= (pteval & _PAGE_RW) ?  VM_WRITE : 0;
	vm_flags |= (pteval & _PAGE_NX) ?  0 : VM_EXEC;
	return vm_flags;
}

extern int az_vmem_init_mm(az_mmstate *mms);
extern void az_vmem_exit_mm(az_mmstate *mms);

/* MM Module interface functions */

extern int az_mm_handle_mm_fault(struct mm_struct *mm,
		struct vm_area_struct *vma, unsigned long addr,
		unsigned int flags);
extern int az_mm_change_protection(struct vm_area_struct *vma,
		unsigned long start, unsigned long end, unsigned long newflags);
extern int az_mm_copy_page_range(struct mm_struct *dst_mm,
		struct mm_struct *src_mm, struct vm_area_struct *vma);
extern int az_mm_follow_page(struct mm_struct *mm, struct vm_area_struct *vma,
		struct page **pages, struct vm_area_struct **vmas,
		unsigned long *position, int *length, int i, int write);
extern int az_mm_probe_mapped(struct vm_area_struct *vma, unsigned long start,
		unsigned long *end_range, unsigned long *range_vm_flags);
extern unsigned long az_mm_unmap_page_range(struct mmu_gather *tlb,
		struct vm_area_struct *vma, unsigned long addr,
		unsigned long end, long *zap_work, struct zap_details *details);
extern void az_mm_free_pgd_range(struct mmu_gather *tlb, unsigned long addr,
		unsigned long end, unsigned long floor, unsigned long ceiling);

#endif /* __KERNEL__ */
#endif /* _LINUX_AZ_VMEM_H */
