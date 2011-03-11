/* az_pmem.h - Azul Physical Memomry Management specific headers
 *
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
 * 
 * NOTE: Only AZ_PAGE_* related items are re-defined here and the common
 * will be used from their native header files.
 */

#ifndef _AZ_PMEM_H
#define _AZ_PMEM_H 1

/* XXX x86 arch specific stuff, should go to arch/x86/include/asm */
#include <asm/page.h>
#include <linux/mm.h>
#include <linux/lockdep.h>

#define AZ_PAGE_SHIFT       21  /* XXX move to arch and set as PMD_SHIFT */
#define AZ_PAGE_SIZE        (_AC(1,UL) << AZ_PAGE_SHIFT)
#define AZ_PAGE_MASK        (~(AZ_PAGE_SIZE - 1))
#define AZ_PAGE_ORDER       (AZ_PAGE_SHIFT - PAGE_SHIFT)
#define AZ_NR_SUBPAGES_PER_PAGE (_AC(1,UL) << AZ_PAGE_ORDER) /* 512 */
#define AZ_SUBPAGES_MASK    (~(AZ_NR_SUBPAGES_PER_PAGE - 1))    

#define AZ_PAGE_MAPPING_POISON  1

/* AZ_PAGE flags used to manage the page allocation and free logic */
enum az_page_flag {	
	AZPGF_MANAGED = (1UL << 0),
	AZPGF_ALLOCED_FROM_FLUSHED = (1UL << 1),
	AZPGF_LAST_BIT = (1UL << 2),
};
#define AZPF_MASK (~(AZPGF_LAST_BIT -1))
#define AZ_PAGE_MANAGED(az_pg) ((az_pg)->azp_flags & AZPGF_MANAGED)
#define AZ_PAGE_ALLOCED_FROM_FLUSHED(az_pg) \
	((az_pg)->azp_flags & AZPGF_ALLOCED_FROM_FLUSHED)

enum az_page_list_type {
	AZPLT_FLUSHED_PAGES = 0,          /* TLB flushed, ready to allocate */
	AZPLT_RELEASED_PAGES = 1,         /* Pending TLB, ok to allocate */
	AZPLT_RELEASED_STRICT_PAGES = 2,  /* Pending TLB, flush before allocate */
	AZPLT_NR_PAGE_LIST_TYPES = 3,
};

/* 
 * XXX Good candidate to go to a different file like az_mem_accounting.h
 * Memory accounting and the associated funds
 * Each account is associated with some funds (committed/overdraft). There
 * could be more than one account associated with a fund.
 */

/* The missing types are not being used for now */
enum az_pmem_fund_type {
	AZMFT_COMMITTED = 0,
	AZMFT_OVERDRAFT = 1,
	AZMFT_GC_PAUSE_PREVENTION = 3,
	AZMFT_EMERGENCY_GC = 4,
	AZMFT_NR_FUND_TYPES = 8,
};

extern const unsigned long az_pmem_zero;
extern const unsigned long az_pmem_infinity;

struct az_pmem_fund {
	spinlock_t  azmf_lock;
	int64_t     azmf_balance;
	size_t      azmf_maximum;
};

#define AZ_PMEM_ACCT_SHIFT 8
#define AZ_PMEM_ACCT_MASK  ((1 << AZ_PMEM_ACCT_SHIFT) - 1)
#define az_pmem_for_each_acct(i, a, accts) \
	for ((i) = 0, (a) = (accts) & AZ_PMEM_ACCT_MASK; \
			(((a) != AZ_PMEM_ACCT_MASK) && (i) < AZMAT_NR_ACCOUNT_TYPES); \
			(i)++, \
			(a) = ((accts) >> ((i) * AZ_PMEM_ACCT_SHIFT))& AZ_PMEM_ACCT_MASK)
#define az_pmem_push_acct_on_list(accts, a) \
	(((accts) << AZ_PMEM_ACCT_SHIFT) | ((a) & AZ_PMEM_ACCT_MASK))

enum az_pmem_account_type {
	AZMAT_HEAP = 0,
	AZMAT_JAVA_HEAP = 2,
	AZMAT_JAVA_GC =3,
	AZMAT_NR_ACCOUNT_TYPES = 8,
};

#define az_pmem_valid_acct(a) (a >= 0 && a < AZMAT_NR_ACCOUNT_TYPES)

struct az_pmem_account {
	spinlock_t          azma_lock;
	struct list_head    subpage_lists[AZPLT_NR_PAGE_LIST_TYPES];
	int64_t             azma_balance;
	int64_t             azma_balance_min;
	int64_t             azma_balance_max;
	size_t              azma_allocated;
	size_t              azma_allocated_min;
	size_t              azma_allocated_max;
	size_t              azma_maximum;
	unsigned long       azma_accts_to_credit;
	struct az_pmem_fund *azma_fund;
	struct az_pmem_fund *azma_overdraft_fund;
};

struct az_pmem_accounts {
	atomic_long_t           azm_rlimit;    /* Process level limit */
	struct az_pmem_account  azmas[AZMAT_NR_ACCOUNT_TYPES];
};

struct az_pmem_pool {
	struct list_head        azp_list;
	spinlock_t              azp_lock;
	unsigned long           azp_nr_subpages_in_lists;
	unsigned long           azp_nr_pages_in_lists;
	struct list_head        azp_lists[AZPLT_NR_PAGE_LIST_TYPES];
	struct az_pmem_accounts azp_accounts;
	atomic_t                azp_ref_count;
	unsigned long           azp_invalidated_count;
	struct list_head	azp_uref_pages;
	unsigned long		azp_nr_uref_pages;
};

/* XXX Make sure the az_page size is 2^n aligned */
struct az_page {
	struct list_head    azp_list;
	unsigned long       azp_flags;
	struct az_pmem_pool *azp_pool;  /* FIXME can become stale */
	unsigned int        azp_shift;
	unsigned int        azp_acct;
	unsigned long       azp_rl_until_invalidated_count;
	unsigned long       azp_rls_until_invalidated_count;
	DECLARE_BITMAP(azp_bmap, AZ_NR_SUBPAGES_PER_PAGE); /* 512 bit 4k map */
	unsigned long	    azp_user_refcnt;
};
#define AZ_PAGE_STRUCT_SHIFT   7 /* XXX Based on the current size (2^7 bytes) */

#define AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE  0x00000004UL
#define AZ_PMEM_TLB_INVALIDATED         0x00000008UL

#define AZ_PMEM_ALLOCATE_NO_OVERDRAFT   0x00001000UL
#define AZ_PMEM_ALLOCATE_REQUEST_NOZERO   0x00002000UL

#if 0
#define az_pfn_to_section_nr(az_pfn) ((az_pfn) >> AZ_PFN_SECTION_SHIFT)
#define az_section_nr_to_az_pfn(sec) ((sec) << AZ_PFN_SECTION_SHIFT)
#endif

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#include <linux/mmzone.h>

struct az_mem_section {
	unsigned long section_mem_map;
};

#define AZ_PFN_SECTION_SHIFT    (SECTION_SIZE_BITS - AZ_PAGE_SHIFT) /*27-21=6*/
#define AZ_PAGES_PER_SECTION    (1UL << AZ_PFN_SECTION_SHIFT)
#define AZ_PAGE_SECTION_MASK    (~(AZ_PAGES_PER_SECTION-1))

#define AZ_SECTIONS_PER_ROOT    (PAGE_SIZE / sizeof(struct az_mem_section))
#define AZ_SECTION_NR_TO_ROOT(sec) ((sec) / AZ_SECTIONS_PER_ROOT)
#define AZ_NR_SECTION_ROOTS     (NR_MEM_SECTIONS / AZ_SECTIONS_PER_ROOT)
#define AZ_SECTION_ROOT_MASK    (AZ_SECTIONS_PER_ROOT - 1)

#if 0
/* Make sure that the highest order page size should fit in a memory section */
#define AZ_MAX_LARGE_PAGE_ORDER    1   /* NR orders */
#if (AZ_MAX_LARGE_PAGE_ORDER - 1 + AZ_PAGE_SHIFT) > SECTION_SIZE_BITS
#error Allocator MAX_ORDER exceeds SECTION_SIZE
#endif


static inline struct az_page * __az_section_mem_map_addr(
		struct mem_section *section)
{
	unsigned long map = section->az_section_mem_map;
	BUG_ON(map & (~SECTION_MAP_MASK));
	return (struct az_page *)map;
}

/* 
 * We use section_mem_map flags to see if the section is present/valid for now.
 */
static inline struct mem_section *__az_pfn_to_section(unsigned long az_pfn)
{
	return __nr_to_section(az_pfn_to_section_nr(az_pfn));
}

static inline int az_pfn_valid(unsigned long az_pfn)
{
	if (az_pfn_to_section_nr(az_pfn) >= NR_MEM_SECTIONS)
		return 0;
	return valid_section(__nr_to_section(az_pfn_to_section_nr(az_pfn)));
}

static inline int az_pfn_present(unsigned long az_pfn)
{
	if (az_pfn_to_section_nr(az_pfn) >= NR_MEM_SECTIONS)
		return 0;
	return present_section(__nr_to_section(az_pfn_to_section_nr(az_pfn)));
}
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * az_vmemmap is virtually contigious and uses the 40bits of the current
 * kernel virtual address space hole from AZ_VMEMMAP_START AZ_VMEMMAP_END.
 */
#define AZ_VMEMMAP_START        _AC(0xffffe30000000000, UL)
#define AZ_VMEMMAP_END            _AC(0xffffe3ffffffffff, UL)
#define az_vmemmap  ((struct az_page *)AZ_VMEMMAP_START)

/* az_pfn <-> az_page conversion macros */
#define az_pfn_to_az_page(az_pfn)    (az_vmemmap + (az_pfn))
#define az_page_to_az_pfn(az_page)    ((az_page) - az_vmemmap)

/* pfn <-> az_pfn conversion macros */
#define az_npfn_to_az_pfn(pfn) ((pfn) >> AZ_PAGE_ORDER)
#define az_pfn_to_npfn(az_pfn) ((az_pfn) << AZ_PAGE_ORDER)

/* pfn <-> az_page conversion macros */
#define az_npfn_to_az_page(pfn) (az_pfn_to_az_page(az_npfn_to_az_pfn(pfn)))
#define az_page_to_npfn(az_page) (az_pfn_to_npfn(az_page_to_az_pfn(az_page)))

/* page <-> az_page conversion macros */
#define az_npage_to_az_page(page) az_npfn_to_az_page(page_to_pfn((page)))
#define az_page_to_npage(az_page) pfn_to_page(az_page_to_npfn((az_page)))

//#define az_early_pfn_valid(az_pfn)    az_pfn_valid(az_pfn)

/* XXX Check later: az_page <-> virtual address */
static inline unsigned long az_page_address(struct az_page *az_page)
{
	struct page *page = az_page_to_npage(az_page);
	return (unsigned long)page_address(page);
}

static inline struct az_page *az_virt_to_az_page(unsigned long vaddr)
{
	return az_npage_to_az_page(virt_to_page(vaddr));
}

#define az_page_aligned(page)  (!(page_to_pfn((page)) & ~AZ_SUBPAGES_MASK))
static inline int az_pmem_managed_page(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	return AZ_PAGE_MANAGED(az_page);
}

static inline int az_pmem_is_large_page(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	BUG_ON(!(AZ_PAGE_MANAGED(az_page)));
	return (az_page->azp_shift == AZ_PAGE_SHIFT);
}

static inline struct page *az_pmem_large_page_head(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	BUG_ON(!(AZ_PAGE_MANAGED(az_page)));
	if (unlikely(az_page->azp_shift == PAGE_SHIFT))
		return page;

	return az_page_to_npage(az_page);
}

static inline void az_get_npages(struct page *page, int n)
{
	page = az_pmem_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	atomic_add(n, &page->_count);
}

static inline int az_put_npages(struct page *page, int n)
{
	int c;
	page = az_pmem_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) <= n);
	c = atomic_sub_and_test(n, &page->_count);
	VM_BUG_ON(c <= 0);
	return c;
}

static inline void az_set_page_count(struct page *page, int v)
{
	atomic_set(&page->_count, v);
}

static inline void az_set_page_refcounted(struct page *page)
{
	VM_BUG_ON(PageTail(page));
	VM_BUG_ON(atomic_read(&page->_count));
	az_set_page_count(page, 1);
}

#endif  /* CONFIG_SPARSEMEM_VMEMMAP */
#endif /* CONFIG_SPARSEMEM */

/* FIXME: Move to the arch specific header file */
static inline int az_find_first_bit(const unsigned long *b)
{
#if BITS_PER_LONG == 64
	if (b[0])
		return __ffs(b[0]);
	if (b[1])
		return __ffs(b[1]) + 64;
	if (b[2])
		return __ffs(b[2]) + 128;
	if (b[3])
		return __ffs(b[3]) + 192;
	if (b[4])
		return __ffs(b[4]) + 256;
	if (b[5])
		return __ffs(b[5]) + 320;
	if (b[6])
		return __ffs(b[6]) + 384;
	return __ffs(b[7]) + 448;
#else
#error BITS_PER_LONG not 64 or not defined
#endif
}

/* Initilize/Finalize process specific free page pools */
struct az_mm_module_mmstate_struct;
extern int az_pmem_init(void);
extern void az_pmem_exit(void);
extern long az_pmem_init_mm(struct az_mm_module_mmstate_struct *mms);
extern void az_pmem_exit_mm(struct az_mm_module_mmstate_struct *mms);

/* Alloc/free from/to process specific pools */
extern unsigned long az_pmem_alloc_page(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags);
extern void az_pmem_free_page(struct az_pmem_pool *pool,
		unsigned long addr, unsigned long flags);
extern struct page* __az_pmem_alloc_page(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags);
extern void __az_pmem_free_page(struct az_pmem_pool *pool,
		struct page *page, unsigned long flags);

extern unsigned long az_pmem_alloc_subpage(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags);
extern void az_pmem_free_subpage(struct az_pmem_pool *pool,
		unsigned long addr, unsigned long flags);
extern struct page* __az_pmem_alloc_subpage(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags);
extern void __az_pmem_free_subpage(struct az_pmem_pool *pool,
		struct page *page, unsigned long flags);
extern void az_pmem_flush_tlb_and_released_pages(
		struct az_mm_module_mmstate_struct *mm);
extern void az_pmem_pool_stats(struct az_pmem_pool *pool);
extern long az_pmem_flush_pool_account(struct az_pmem_pool *pool,
		unsigned long acct, size_t *allocated, int od_only);
extern int az_pmem_nr_pages_sysctl(struct ctl_table *, int, struct file *,
		void __user *, size_t *, loff_t *);
extern long az_pmem_add_user_ref(struct page *page, int cnt);
extern long az_pmem_sub_user_ref(struct page *page, int cnt);
extern long az_pmem_read_user_ref(struct page *page);

static inline void az_pmem_get_page(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	BUG_ON(!az_pmem_managed_page(page));
	BUG_ON(!az_page->azp_pool);
	if (az_pmem_is_large_page(page))
		page = az_pmem_large_page_head(page);
	VM_BUG_ON(atomic_read(&page->_count) == 0);
	atomic_inc(&page->_count);
}

static inline void az_pmem_put_page(struct page *page, int flags)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	BUG_ON(!az_pmem_managed_page(page));
	BUG_ON(!az_page->azp_pool);
	if (az_pmem_is_large_page(page)) {
		page = az_pmem_large_page_head(page);
		if (put_page_testzero(page))
			__az_pmem_free_page(az_page->azp_pool, page, flags);
	} else {
		if (put_page_testzero(page))
			__az_pmem_free_subpage(az_page->azp_pool, page, flags);
	}
}

/* PMEM Module interface function : */

extern int az_pmem_put_page_if_owned(struct page *page);
extern int az_pmem_get_page_if_owned(struct page *page);
extern int az_pmem_sparse_mem_map_populate(unsigned long pnum, int nid);

#endif /* _AZ_PMEM_H */
