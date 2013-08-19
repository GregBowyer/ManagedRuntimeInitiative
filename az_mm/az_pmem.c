/* az_pmem.c - Azul Physical Memory Management 
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
 */

#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/bitmap.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include "az_pmem.h"
#include "az_mm.h"
#include "az_vmem.h"

//#define AZ_PMEM_DEBUG 1

static DEFINE_SPINLOCK(az_pmem_global_lock);
static LIST_HEAD(az_zfree_pages);
static unsigned long az_nr_zfree_pages;
static LIST_HEAD(az_nzfree_pages);
static unsigned long az_nr_nzfree_pages;
static LIST_HEAD(az_xprocess_pools);
static unsigned long az_nr_xprocess_pools;
static atomic_long_t az_nr_pages_requested;
static atomic_long_t az_nr_pages_allocated;

const unsigned long az_pmem_zero = 0UL;
const unsigned long az_pmem_infinity = ~0UL;
static void az_pmem_put_pool(struct az_pmem_pool *pool);

static struct proc_dir_entry *azmm_pfs_root;
static struct proc_dir_entry *azmm_pfs_pmem;


/* 
 * Fund #   Fund name
 * ------   ---------
 * 0        Committed
 * 1        Grant/Overdraft
 * 2        Loan            - Not funded
 * 3        GC Pause prevention
 * 4        Emergency GC    - Not funded
 * 5        Spare
 * 6,7      Unknown         - Not funded
 */
struct az_pmem_fund az_pmem_funds[AZMFT_NR_FUND_TYPES]; /* XXX Global?? */

/* 
 * Dump all the memory in to committed fund and let the sysinit job move it
 * around.
 */
static size_t az_pmem_configured_funds[AZMFT_NR_FUND_TYPES] = 
{
	100, 0, 0, 0, 0, 0, 0, 0
};

#ifdef CONFIG_SPARSEMEM_EXTREME
static struct az_mem_section *az_mem_section[AZ_NR_SECTION_ROOTS];
static DEFINE_SPINLOCK(index_init_lock);

static int __meminit az_sparse_index_init(unsigned long section_nr, int nid)
{
	unsigned long root = AZ_SECTION_NR_TO_ROOT(section_nr);
	struct az_mem_section *section;
	unsigned long array_size = AZ_SECTIONS_PER_ROOT *
		sizeof(struct az_mem_section);

	if (az_mem_section[root])
		return 0;

	/* Assumes that the slab_is_available */
	section = kzalloc_node(array_size, GFP_KERNEL, nid);
	if (!section)
		return -ENOMEM;
	/*
	 * This lock keeps two different sections from
	 * reallocating for the same index
	 */
	spin_lock(&index_init_lock);

	if (az_mem_section[root])
		/* TODO We lost the race, Release the page we allocated */
		goto out;

	az_mem_section[root] = section;
out:
	spin_unlock(&index_init_lock);
	printk(KERN_INFO "AZMM: Inited section %lu root[%lu]: %lx\n",
			section_nr, root, (long)section);
	return 0;
}
#endif

static inline void az_validate_pfn_limits(unsigned long *start,
		unsigned long *end)
{
	unsigned long max_sparsemem_pfn = 1UL << (MAX_PHYSMEM_BITS-PAGE_SHIFT);

	if (*start > max_sparsemem_pfn) {
		*start = max_sparsemem_pfn;
		*end = max_sparsemem_pfn;
	}

	if (*end > max_sparsemem_pfn)
		*end = max_sparsemem_pfn;
}

/* Record a memory area against a node. */
static int az_memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;
	int ret = 0;

	start &= PAGE_SECTION_MASK;
	az_validate_pfn_limits(&start, &end);
	printk(KERN_DEBUG "AZMM: Initing sections for pfn range %lu-%lu\n",
			start, end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		if (az_sparse_index_init(pfn_to_section_nr(pfn), nid))
			ret = -ENOMEM;
	}
	return ret;
}

#if 0
#define for_each_active_range_index_in_nid(i, nid) \
	for (i = first_active_region_index_in_nid(nid); i != -1; \
			i = next_active_region_index_in_nid(i, nid))

/* If an architecture guarantees that all ranges registered with
 * add_active_ranges() contain no holes and may be freed, this
 * function may be used instead of calling memory_present() manually.
 */
static void az_sparse_memory_present_with_active_regions(int nid)
{   
	int i;

	for_each_active_range_index_in_nid(i, nid)
		az_memory_present(early_node_map[i].nid,
				early_node_map[i].start_pfn,
				early_node_map[i].end_pfn);
}   

static void az_init_mem_sections(void)
{
	az_sparse_memory_present_with_active_regions(MAX_NUMNODES);
}
#else
static int az_init_mem_sections(void)
{
	return az_memory_present(0, 0, max_pfn);
}
#endif

static inline struct az_mem_section *__az_nr_to_section(unsigned long nr)
{
	if (!az_mem_section[AZ_SECTION_NR_TO_ROOT(nr)])
		return NULL;
	return &az_mem_section[AZ_SECTION_NR_TO_ROOT(nr)][nr&AZ_SECTION_ROOT_MASK];
}

static void az_free_mem_section(unsigned long section_nr)
{
	unsigned long root = AZ_SECTION_NR_TO_ROOT(section_nr);

	if (!az_mem_section[root])
		return;
	kfree(az_mem_section[root]);
	printk(KERN_INFO "AZMM: Freed section %lu root[%lu]: %lx\n",
			section_nr, root, (long)az_mem_section[root]);
	az_mem_section[root] = NULL;
}

static unsigned long az_zap_vmemmap_pte(pmd_t *pmd, unsigned long addr,
		unsigned long end)
{
	spinlock_t *ptl;
	struct mm_struct *mm = &init_mm;

	pte_t *pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
	do {
		if (pte_none(*pte))
			continue;
		if (pte_present(*pte)) {
			printk(KERN_DEBUG "AZMM: Freed vmemmap 4k page @%lx\n",
					addr);
			page_cache_release(pte_page(*pte));
		}
		pte_clear(mm, addr, pte);
	} while (pte++, addr += PAGE_SIZE, (addr != end));
	pte_unmap_unlock(pte - 1, ptl);

	return addr;
}

static inline unsigned long az_zap_vmemmap_pmd(pud_t *pud,
		unsigned long addr, unsigned long end)
{
	unsigned long next;
	struct mm_struct *mm = &init_mm;

	pmd_t *pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (az_pmd_none_or_clear_bad(pmd))
			continue;
		if (pmd_large_virtual(*pmd)) {
			printk(KERN_DEBUG "AZMM: Freed vmemmap 2M page @%lx\n",
					addr);
			__free_pages(pte_page(*(pte_t *)pmd), AZ_PAGE_ORDER);
			pte_clear(mm, addr, (pte_t *)pmd);
			continue;
		}
		next = az_zap_vmemmap_pte(pmd, addr, next);
	} while (pmd++, addr = next, (addr != end));

	return addr;
}

static inline unsigned long az_zap_vmemmap_pud(pgd_t *pgd, unsigned long addr,
		unsigned long end)
{
	unsigned long next;

	pud_t *pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (az_pud_none_or_clear_bad(pud))
			continue;
		next = az_zap_vmemmap_pmd(pud, addr, next);
	} while (pud++, addr = next, (addr != end));

	return addr;
}

static void az_zap_vmemmap(void)
{
	unsigned long next;
	unsigned long addr = (unsigned long)az_vmemmap;
	unsigned long end = addr + az_npfn_to_az_pfn(max_pfn);

	pgd_t *pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		if (az_pgd_none_or_clear_bad(pgd))
			continue;
		next = az_zap_vmemmap_pud(pgd, addr, next);
	} while (pgd++, addr = next, (addr != end));
}

static void az_exit_mem_sections(void)
{
	unsigned long pfn;
	unsigned long end = max_pfn;
	unsigned long start = 0;

	start &= PAGE_SECTION_MASK;
	az_validate_pfn_limits(&start, &end);
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION)
		az_free_mem_section(pfn_to_section_nr(pfn));
}

static void az_free_vmemmap_pagetables(void)
{
	/* TODO: Free pagetables for the vmemmap */
}

static void az_exit_vmemmap(void)
{
	az_zap_vmemmap();
	az_free_vmemmap_pagetables();
}

static void * az_vmemmap_alloc_block(unsigned long size, int node)
{
	struct page *page = alloc_pages_node(node,
			GFP_KERNEL | __GFP_ZERO, get_order(size));
	if (!page)
		return NULL;
	printk(KERN_DEBUG "AZMM: Allocated page size %lx for vmemmap\n", size);
	return page_address(page);
}

static pte_t * az_vmemmap_pte_populate(pmd_t *pmd, unsigned long addr, int node)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte)) {
		pte_t entry;
		void *p = az_vmemmap_alloc_block(PAGE_SIZE, node);
		if (!p)
			return NULL;
		entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
		set_pte_at(&init_mm, addr, pte, entry);
	}
	return pte;
}

static pmd_t * az_vmemmap_pmd_populate(pud_t *pud, unsigned long addr, int node)
{
	pmd_t *pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		void *p = az_vmemmap_alloc_block(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pmd_populate_kernel(&init_mm, pmd, p);
	}
	return pmd;
}

static pud_t * az_vmemmap_pud_populate(pgd_t *pgd, unsigned long addr, int node)
{
	pud_t *pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		void *p = az_vmemmap_alloc_block(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pud_populate(&init_mm, pud, p);
	}
	return pud;
}

static pgd_t * az_vmemmap_pgd_populate(unsigned long addr, int node)
{
	pgd_t *pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd)) {
		void *p = az_vmemmap_alloc_block(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pgd_populate(&init_mm, pgd, p);
	}
	return pgd;
}

/*
 * Initialise the sparsemem vmemmap using huge-pages at the PMD level.
 */
static long addr_start, addr_end;
static void *p_start, *p_end;
static int node_start;
static int az_populate_vmemmap(unsigned long addr, unsigned long end, int node)
{
	unsigned long next;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	for (; addr < end; addr = next) {
		void *p = NULL;

		pgd = az_vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = az_vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		if (!cpu_has_pse) {
			next = (addr + PAGE_SIZE) & PAGE_MASK;
			pmd = az_vmemmap_pmd_populate(pud, addr, node);

			if (!pmd)
				return -ENOMEM;

			p = az_vmemmap_pte_populate(pmd, addr, node);

			if (!p)
				return -ENOMEM;

			addr_end = addr + PAGE_SIZE;
			p_end = p + PAGE_SIZE;
		} else {
			next = pmd_addr_end(addr, end);

			pmd = pmd_offset(pud, addr);
			if (pmd_none(*pmd)) {
				pte_t entry;

				p = az_vmemmap_alloc_block(PMD_SIZE, node);
				if (!p)
					return -ENOMEM;

				entry = pfn_pte(__pa(p) >> PAGE_SHIFT,
						PAGE_KERNEL_LARGE);
				set_pmd(pmd, __pmd(pte_val(entry)));

				/* check to see if we have contiguous blocks */
				if (p_end != p || node_start != node) {
					addr_start = addr;
					node_start = node;
					p_start = p;
				}

				addr_end = addr + PMD_SIZE;
				p_end = p + PMD_SIZE;
			}
		}
	}
	return 0;
}

int __meminit az_sparse_mem_map_populate(unsigned long pnum, int nid)
{
	unsigned long start =
		(unsigned long)az_pfn_to_az_page(pnum * AZ_PAGES_PER_SECTION);
	unsigned long end = start +
		(AZ_PAGES_PER_SECTION * sizeof(struct az_page));
	struct az_mem_section *ams = __az_nr_to_section(pnum);
	unsigned long root = AZ_SECTION_NR_TO_ROOT(pnum);

	if (!az_mem_section[root] && az_sparse_index_init(pnum, nid))
		return -ENOMEM;

	if (!ams) {
		printk(KERN_ERR "AZMM: section is not inited %lu, root %lu\n",
				pnum, root);
		return -EFAULT;
	}

	if (ams->section_mem_map)
		return 0;

	if (az_populate_vmemmap(start, end, nid)) {
		printk(KERN_ERR "AZPM: vmemmap backing failed "
				"some memory will not be available.\n");
		ams->section_mem_map = 0;
		return -ENOMEM;
	}
	printk(KERN_DEBUG "AZMM: Inited vmemmap section[%lu] range %lx - %lx\n",
			pnum, start, end);
	ams->section_mem_map = start;
	return 0;
}

/*
 * Allocate and initialize the section_mem_map: As we use the native
 * section_mem_map flags we will use section_mem_map only if the native
 * one is available.
 */
static int az_init_vmemmap(void)
{
	unsigned long pnum;
	int ret = 0;
	struct mem_section *ms;
	int nid;

	/* Assert struct az_page size */
	BUG_ON(sizeof(struct az_page) != (1 << AZ_PAGE_STRUCT_SHIFT));
	for (pnum = 0; pnum < NR_MEM_SECTIONS; pnum++) {
		ms = __nr_to_section(pnum);
		if (!ms)
			continue;
		nid = ms->section_mem_map >> SECTION_NID_SHIFT;
		if (!present_section(ms))
			continue;

		if (az_sparse_mem_map_populate(pnum, nid))
			ret = -ENOMEM;
	}
	return ret;
}

static struct task_struct *az_get_task_struct(pid_t pid)
{
	struct task_struct *tsk;

	read_lock(&tasklist_lock);
	tsk = pid_task(pid, PIDTYPE_PID);
	if (!tsk || (tsk->flags & PF_EXITING))
		goto err_unlock;
	get_task_struct(tsk);   /* Hold refcount for this task */
	read_unlock(&tasklist_lock);
	return tsk;

err_unlock:
	read_unlock(&tasklist_lock);
	return ERR_PTR(-ESRCH);
}

/* Lock in the order based on the addresses */
static void az_nested_spin_lock(spinlock_t *lock1, spinlock_t *lock2)
{
	if (lock1 == lock2) {
		spin_lock(lock1);
	} else {
		if (lock1 < lock2) {
			spin_lock(lock1);
			spin_lock_nested(lock2, SINGLE_DEPTH_NESTING);
		} else {
			spin_lock(lock2);
			spin_lock_nested(lock1, SINGLE_DEPTH_NESTING);
		}
	}
}

static void az_nested_spin_unlock(spinlock_t *lock1, spinlock_t *lock2)
{
	spin_unlock(lock1);
	if (lock1 != lock2)
		spin_unlock(lock2);
}

/*
 * Drop a ref, return true if the refcount fell to zero (the pool has no users)
 */
static inline int az_pmem_put_pool_testzero(struct az_pmem_pool *pool)
{
	VM_BUG_ON(atomic_read(&pool->azp_ref_count) == 0);
	return atomic_dec_and_test(&pool->azp_ref_count);
}

static inline void az_pmem_get_pool(struct az_pmem_pool *pool)
{
	atomic_inc(&pool->azp_ref_count);
}

/* XXX Never call the az_*queue_pool functions with azp_lock held as the
 * az_pmem_get_xprocess_page first holds global and then azp_lock 
 * and will result in dead lock
 */
static void az_enqueue_pool(struct list_head *pools, struct az_pmem_pool *pool,
		unsigned long *count)
{
	spin_lock(&az_pmem_global_lock);
	/* Don't do anything if the pool is on the list already */
	if (list_empty(&pool->azp_list)) {
		list_add_tail(&pool->azp_list, pools);
		(*count)++;
	}
	spin_unlock(&az_pmem_global_lock);
}

static void az_dequeue_pool(struct az_pmem_pool *pool, unsigned long *count)
{
	spin_lock(&az_pmem_global_lock);
	/* Don't do anything if the pool is not on the list */
	if (!list_empty(&pool->azp_list)) {
		BUG_ON(!(*count));
		list_del_init(&pool->azp_list);
		(*count)--;
	}
	spin_unlock(&az_pmem_global_lock);
}


/* XXX The caller should hold the list lock before calling these functions */
static void az_enqueue_page(struct list_head *pl,
		struct az_page *az_page, unsigned long *count)
{
	list_add_tail(&az_page->azp_list, pl);
	(*count)++;
}

static struct az_page* az_dequeue_large_page(struct list_head *pl,
		unsigned long *count)
{
	struct az_page *az_page = NULL;

	if (!list_empty(pl)) {
		BUG_ON(!(*count));
		az_page = list_first_entry(pl, struct az_page, azp_list);
		list_del_init(&az_page->azp_list);
		(*count)--;
	}
	return az_page;
}

static void az_page_init(struct az_page *az_page, unsigned long flags)
{

	INIT_LIST_HEAD(&az_page->azp_list);
	bitmap_fill(az_page->azp_bmap, AZ_NR_SUBPAGES_PER_PAGE);
	az_page->azp_flags = flags;
	az_page->azp_shift = AZ_PAGE_SHIFT;
	az_page->azp_pool = NULL;
	az_page->azp_acct = AZMAT_HEAP;
	az_page->azp_user_refcnt = 0;
}

/* Copied from mm/page_alloc.c as it was static there */
static void az_bad_page(struct page *page)
{
	printk(KERN_EMERG "Bad page state in process '%s'\n" KERN_EMERG
			"page:%p flags:0x%0*lx mapping:%p nr_maps:%d cnt:%d\n",
			current->comm, page, (int)(2*sizeof(unsigned long)),
			(unsigned long)page->flags, page->mapping,
			page_mapcount(page), page_count(page));

	printk(KERN_EMERG "Trying to fix it up, but a reboot is needed\n"
			KERN_EMERG "Backtrace:\n");
	dump_stack();
	/* Leave bad fields for debug, except PageBuddy could make trouble */
	__ClearPageBuddy(page);
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

static void az_destroy_compound_page(struct page *page, unsigned long order)
{
	int i;
	int nr_pages = 1 << order;

	if (unlikely(compound_order(page) != order))
		az_bad_page(page);

	if (unlikely(!PageHead(page)))
		az_bad_page(page);
	__ClearPageHead(page);
	SetPagePmemModule(page);

	/* Set mapping field to keep gup futex loopers happy: */
	page->mapping = (struct address_space *) AZ_PAGE_MAPPING_POISON;
	
	for (i = 1; i < nr_pages; i++) {
		struct page *p = page + i;

		if (unlikely(!PageTail(p) |
					(p->first_page != page)))
			az_bad_page(page);
		__ClearPageTail(p);
		SetPagePmemModule(p);
		/* Set mapping field to keep gup futex loopers happy: */
		p->mapping = (struct address_space *) AZ_PAGE_MAPPING_POISON;
	}
}

static void az_release_large_page(struct page *page)
{
	int i;
	int nr_pages = 1 << AZ_PAGE_ORDER;

	for (i = 0; i < nr_pages; i++) {
		page[i].flags &= ~(1 << PG_locked | 1 << PG_error |
				1 << PG_referenced | 1 << PG_dirty |
				1 << PG_active | 1 << PG_reserved |
				1 << PG_private | 1<< PG_writeback |
				1 << PG_mlocked | 1 << PG_swapbacked);
		ClearPagePmemModule(page + i);
		if (i > 0) {
			page[i].first_page = page;
			__SetPageTail(page + i);
		}
		/* clear mapping (was poisoned to keep futex loopers happy): */
		page[i].mapping = NULL;
	}
	set_compound_page_dtor(page, NULL);
	az_set_page_refcounted(page);
	__free_pages(page, AZ_PAGE_ORDER);
}

static struct page* az_grab_large_page(struct list_head *pl,
		unsigned long *count)
{
	unsigned long flags = (GFP_HIGHUSER | __GFP_COMP | __GFP_REPEAT);
	struct page *page; 
	struct az_page *az_page;

	page = alloc_pages(flags, AZ_PAGE_ORDER);
	if (!page)
		return NULL;
	put_page_testzero(page); /* Drop the buddy allocators ref */
	VM_BUG_ON(page_count(page));
	az_destroy_compound_page(page, AZ_PAGE_ORDER); /* Reset compound flags*/
	az_page = az_npage_to_az_page(page);
	az_page_init(az_page, AZPGF_MANAGED);
	az_enqueue_page(pl, az_page, count);
	return page;
}

static void az_pmem_init_pool(struct az_pmem_pool *pool)
{
	int i;

	spin_lock_init(&pool->azp_lock);
	pool->azp_nr_subpages_in_lists = 0;
	pool->azp_nr_pages_in_lists = 0;
	pool->azp_invalidated_count = 0;
	for (i = 0; i < AZPLT_NR_PAGE_LIST_TYPES; i++) {
		INIT_LIST_HEAD(&pool->azp_lists[i]);
	}
	az_pmem_get_pool(pool);    /* mm's ref */
	INIT_LIST_HEAD(&pool->azp_list);
	INIT_LIST_HEAD(&pool->azp_uref_pages);
	pool->azp_nr_uref_pages = 0;
}

static void az_pmem_payback_loan(struct az_pmem_account *ac, size_t loan)
{
	struct az_pmem_fund *od_fund = ac->azma_overdraft_fund;

	BUG_ON(!od_fund);
	spin_lock(&od_fund->azmf_lock); /* Nested lock */
	od_fund->azmf_balance += loan;
	spin_unlock(&od_fund->azmf_lock); /* Nested unlock */
}

/*
 * XXX The spin lock nesting for account and fund are always in the order
 * of account lock first.
 */
static size_t az_pmem_account_deposit(struct az_pmem_account *ac, size_t size)
{
	size_t surplus = 0;
	size_t loan;

	spin_lock(&ac->azma_lock);
	if (size > ac->azma_allocated) {
		surplus = size - ac->azma_allocated;
		size = ac->azma_allocated;
	}
	if (ac->azma_balance < 0) {
		loan = ((-ac->azma_balance) > size) ?
			size : (-ac->azma_balance);
		az_pmem_payback_loan(ac, loan);
	}
	ac->azma_balance += size;
	ac->azma_allocated -= size;
	if (ac->azma_balance_max < ac->azma_balance)
		ac->azma_balance_max = ac->azma_balance;
	if (ac->azma_allocated_min > ac->azma_allocated)
		ac->azma_allocated_min = ac->azma_allocated;
	spin_unlock(&ac->azma_lock);

	return surplus;
}

static void az_pmem_account_deposit_all(struct az_pmem_accounts *acs,
		size_t size)
{
	int i;
	size_t surplus = size;

	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES && surplus; i++)
		surplus = az_pmem_account_deposit(&acs->azmas[i], surplus);
	BUG_ON(surplus);
}   

static void az_pmem_credit_accounts(struct az_pmem_accounts *acs,
		unsigned long accts_to_credit, size_t size)
{
	size_t surplus = size; 
	unsigned long a;
	int i;

	az_pmem_for_each_acct(i, a, accts_to_credit) {
		surplus = az_pmem_account_deposit(&acs->azmas[a], surplus);
		if (!surplus)
			break;
	}
	if (surplus) {
		printk(KERN_ERR "Failed to credit %lu bytes to list %lx\n",
				surplus, accts_to_credit);
		az_pmem_account_deposit_all(acs, surplus);
	}
}   

static long az_pmem_get_loan(struct az_pmem_account *ac, size_t loan)
{
	struct az_pmem_fund *od_fund = ac->azma_overdraft_fund;

	if (!od_fund)
		return -EACCES;

	spin_lock(&od_fund->azmf_lock); /* Nested lock */
	if ((int64_t)loan > od_fund->azmf_balance) {
		spin_unlock(&od_fund->azmf_lock); /* Nested unlock */
		return -EACCES;
	}
	od_fund->azmf_balance -= loan;
	spin_unlock(&od_fund->azmf_lock); /* Nested unlock */
	return 0;
}

static long az_pmem_account_withdraw(struct az_pmem_accounts *acs,
		unsigned long accno, int overdraft, size_t size)
{
	size_t loan = 0;
	long rc = 0;
	struct az_pmem_account *ac = &acs->azmas[accno];

	spin_lock(&ac->azma_lock);
	if ((size + ac->azma_allocated > ac->azma_maximum) ||
			(size + ac->azma_allocated >
			 atomic_long_read(&acs->azm_rlimit))) {
		rc = -EACCES;   /* FIXME find approp error code for this */
		goto out;
	}
	if ((int64_t)size > ac->azma_balance) {
		if (!overdraft) {
			rc = -EACCES;
			goto out;
		}

		loan = (ac->azma_balance > 0) ?  size - ac->azma_balance : size;
		if (az_pmem_get_loan(ac, loan)) {
			rc = -EACCES;
			goto out;
		}
	}

	ac->azma_balance -= size;
	ac->azma_allocated += size;
	if (ac->azma_balance_min > ac->azma_balance)
		ac->azma_balance_min = ac->azma_balance;
	if (!ac->azma_allocated_max || ac->azma_allocated_max < ac->azma_allocated)
		ac->azma_allocated_max = ac->azma_allocated;
out:
	spin_unlock(&ac->azma_lock);
	return rc;
}

static void az_pmem_dump_funds(void)
{
	int i;

	for (i = 0; i < AZMFT_NR_FUND_TYPES; i++)
		printk(KERN_DEBUG "AZPM: fund %d balance 0x%010lx\n",
				i, (unsigned long)az_pmem_funds[i].azmf_balance);
}

static long __az_pmem_fund_transfer(struct az_pmem_fund *dst,
		struct az_pmem_fund *src, size_t size)
{
	long rc = -EINVAL;

	az_nested_spin_lock(&dst->azmf_lock, &src->azmf_lock);
	if (src->azmf_balance < size || src->azmf_maximum < size)
		goto done;
	src->azmf_balance -= size;
	src->azmf_maximum -= size;
	dst->azmf_balance += size;
	dst->azmf_maximum += size;
	rc = 0;
done:
	az_nested_spin_unlock(&dst->azmf_lock, &src->azmf_lock);
	return rc;
}   

static long __az_pmem_account_transfer(struct az_pmem_account *dst,
		struct az_pmem_account *src, size_t size)
{
	long rc = -EINVAL;

	/* TODO Verify the logic */
	az_nested_spin_lock(&dst->azma_lock, &src->azma_lock);
	if (dst->azma_fund != src->azma_fund || src->azma_balance < size)
		goto done;
	rc = 0;
	src->azma_balance -= size;
	if (src->azma_balance_min > src->azma_balance)
		src->azma_balance_min = src->azma_balance;
	dst->azma_balance += size;
	if (dst->azma_balance_max < dst->azma_balance)
		dst->azma_balance_max = dst->azma_balance;
done:
	az_nested_spin_unlock(&dst->azma_lock, &src->azma_lock);
	return rc;
}   

typedef struct {
	unsigned long nr_accts;
	unsigned long process_limit;
	struct {
		int64_t         ac_balance;
		int64_t         ac_balance_min;
		int64_t         ac_balance_max;
		size_t          ac_allocated;
		size_t          ac_allocated_min;
		size_t          ac_allocated_max;
		size_t          ac_maximum;
		unsigned long   ac_accts_to_credit;
		int64_t         ac_fund;
		int64_t         ac_overdraft_fund;
	} ac[AZMAT_NR_ACCOUNT_TYPES];
} azm_account_stats_s;

static long __az_pmem_get_account_stats(struct az_pmem_accounts *acs, 
		azm_account_stats_s *stats)
{
	int i;
	struct az_pmem_account *ac;

	stats->process_limit = atomic_long_read(&acs->azm_rlimit);
	stats->nr_accts = AZMAT_NR_ACCOUNT_TYPES;
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &acs->azmas[i];
		spin_lock(&ac->azma_lock);
		stats->ac[i].ac_balance = ac->azma_balance;
		stats->ac[i].ac_balance_min = ac->azma_balance_min;
		stats->ac[i].ac_balance_max = ac->azma_balance_max;
		stats->ac[i].ac_allocated = ac->azma_allocated;
		stats->ac[i].ac_allocated_min = ac->azma_allocated_min;
		stats->ac[i].ac_allocated_max = ac->azma_allocated_max;
		stats->ac[i].ac_maximum = ac->azma_maximum;
		stats->ac[i].ac_accts_to_credit = ac->azma_accts_to_credit;
		stats->ac[i].ac_fund = (ac->azma_fund ?
				(ac->azma_fund - &az_pmem_funds[0]) : -1);
		stats->ac[i].ac_overdraft_fund = (ac->azma_overdraft_fund ?
				(ac->azma_overdraft_fund - &az_pmem_funds[0]) :
				-1);
		spin_unlock(&ac->azma_lock);
	}

	return 0;
}

typedef struct {
	unsigned long nr_funds;
	struct {
		size_t fnd_balance;
		size_t fnd_maximum;
	} fund[AZMFT_NR_FUND_TYPES];
} azm_fund_stats_s;

long __az_pmem_get_fund_stats(azm_fund_stats_s *stats)
{
	int i;

	stats->nr_funds = AZMFT_NR_FUND_TYPES;
	for (i = 0; i < AZMFT_NR_FUND_TYPES; i++) {
		spin_lock(&az_pmem_funds[i].azmf_lock);
		stats->fund[i].fnd_balance = (az_pmem_funds[i].azmf_balance > 0)
			? az_pmem_funds[i].azmf_balance : 0;
		stats->fund[i].fnd_maximum = az_pmem_funds[i].azmf_maximum;
		spin_unlock(&az_pmem_funds[i].azmf_lock);
	}

	return 0;
}

static void az_pmem_dump_accounts(struct az_pmem_accounts *acs)
{
	int i;

	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++)
		printk(KERN_DEBUG "AZPM: account %d "
				"balance %10ld allocated %10ld maximum %10ld\n",
				i,
				(long int)acs->azmas[i].azma_balance,
				acs->azmas[i].azma_allocated,
				acs->azmas[i].azma_maximum);
}

void az_pmem_dump_accounting_stats(struct az_pmem_accounts *acs)
{
	az_pmem_dump_funds();
	az_pmem_dump_accounts(acs);
}

static long __az_pmem_adjust_account_fund(struct az_pmem_account *ac,
		int64_t size)
{
	struct az_pmem_fund *fund;
	long rc = 0;

	BUG_ON(!ac);
	fund = ac->azma_fund;
	if (!fund)
		return -EINVAL;

	az_nested_spin_lock(&ac->azma_lock, &fund->azmf_lock);
	if (size < 0) {
		/* Account balance can't be lowered below zero */
		if (ac->azma_balance + size < 0) {
			printk(KERN_WARNING "AZPM: account balance"
					"%lld < size %lld\n",
					ac->azma_balance, -size);
			rc = -EINVAL;
			goto done;
		}
		/* Tranfer funds back from account */
		ac->azma_balance += size;
		fund->azmf_balance -= size;
	} else {
		/* Account can't be funded more than the linked fund balance */
		if (size > fund->azmf_balance) {
			printk(KERN_WARNING
					"AZPM: fund balance %lld < size %lld\n",
					fund->azmf_balance, size);
			rc = -ENOMEM;
			goto done;
		}
		/* Transfer funds to account */
		fund->azmf_balance -= size;
		/* Pay back if we had taken a loan from overdraft earlier */
		if (ac->azma_balance < 0) {
			size_t loan = 0;
			loan = ((-ac->azma_balance) > size) ?
				size : (-ac->azma_balance);
			az_pmem_payback_loan(ac, loan); /* takes od_fund lock */
		}
		ac->azma_balance += size;
	}
	if (ac->azma_balance_max < ac->azma_balance)
		ac->azma_balance_max = ac->azma_balance;
	if (!ac->azma_balance_min || ac->azma_balance_min > ac->azma_balance)
		ac->azma_balance_min = ac->azma_balance;
done:
	az_nested_spin_unlock(&ac->azma_lock, &fund->azmf_lock);

	return rc;
}

static void az_pmem_close_accounts(struct az_pmem_accounts *acs)
{
	int i;
	struct az_pmem_account *ac;

	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &acs->azmas[i];
		az_pmem_credit_accounts(acs, ac->azma_accts_to_credit,
				ac->azma_allocated);
		BUG_ON(ac->azma_balance < 0);
		__az_pmem_adjust_account_fund(ac, -(ac->azma_balance));
	}
}

/*
 * The maximum could be less than (balance + allocated) but not be less than
 * allocated. The withdrawl checks first the maxmum to see if the
 * allocated + size is in limits.
 */
static long __az_pmem_set_maximum(struct mm_struct *mm, size_t size)
{
	int i;
	struct az_pmem_account *ac;
	az_mmstate *mms = az_mm_mmstate_auto(mm);

	if (!mms)
		return -ENOMEM;

	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &mms->azm_pool->azp_accounts.azmas[i];
		spin_lock(&ac->azma_lock);
		if (size < ac->azma_allocated) {
			printk(KERN_WARNING "AZPM: %s: can't set max, "
					"allocated %lu > max %lu\n",
					__func__, ac->azma_allocated, size);
			spin_unlock(&ac->azma_lock);
			return -EINVAL;
		}
		spin_unlock(&ac->azma_lock);
	}
	atomic_long_set(&mms->azm_pool->azp_accounts.azm_rlimit, size);
	return 0; 
}    

/* Similar to az_pmem_set_maximum but at account level */
static long __az_pmem_set_account_maximum(struct az_pmem_account *ac,
		size_t size)
{
	spin_lock(&ac->azma_lock);
	if (size < ac->azma_allocated) {
		printk(KERN_WARNING "AZPM: can't set allocated %lu > max %lu\n",
				ac->azma_allocated, size);
		spin_unlock(&ac->azma_lock);
		return -EINVAL;
	}
	ac->azma_maximum = size;
	spin_unlock(&ac->azma_lock);
	return 0; 
}    

static inline int az_pmem_acct_list_sane(unsigned long accts)
{
	unsigned long a;
	int i;
	az_pmem_for_each_acct(i, a, accts)
		;
	return (i != 0);
}

static long __az_pmem_set_accounts_to_credit(struct az_pmem_account *ac,
		unsigned long accts)
{
	spin_lock(&ac->azma_lock);
	ac->azma_accts_to_credit = accts;
	spin_unlock(&ac->azma_lock);
	return 0;
}

static long __az_pmem_set_account_funds(struct az_pmem_account *ac,
		unsigned long commit, unsigned long overdraft)
{
	spin_lock(&ac->azma_lock);
	ac->azma_fund = &az_pmem_funds[commit];
	ac->azma_overdraft_fund = &az_pmem_funds[overdraft];
	spin_unlock(&ac->azma_lock);
	return 0;
}

long az_pmem_flush_pool_account(struct az_pmem_pool *pool,
		unsigned long acct, size_t *allocated, int od_only)
{
	struct az_page *az_page;
	struct list_head *pl = &pool->azp_lists[AZPLT_FLUSHED_PAGES];
	struct list_head tmp_pl;
	size_t loan;
	unsigned long flush_limit;
	long i;
	struct az_pmem_account *ac;

	ac = &pool->azp_accounts.azmas[acct];
	spin_lock(&ac->azma_lock);
	*allocated = ac->azma_allocated;
	loan = (ac->azma_balance < 0 ? (-ac->azma_balance) : 0);
	spin_unlock(&ac->azma_lock);

	flush_limit = (od_only ? loan : *allocated);
	if (!flush_limit)
		return 0;

	flush_limit >>= AZ_PAGE_SHIFT;
	INIT_LIST_HEAD(&tmp_pl);

	for (i = 0; i < flush_limit; i++) {
		spin_lock(&pool->azp_lock);
		az_page = az_dequeue_large_page(pl,
				&pool->azp_nr_pages_in_lists);
		spin_unlock(&pool->azp_lock);
		if (!az_page)
			break;
		list_add_tail(&az_page->azp_list, &tmp_pl);
	}

	/* The page list pl could become empty by the time we dequeue from it */
	if (!i)
		return 0;

	spin_lock(&az_pmem_global_lock);
	list_splice_init(&tmp_pl, &az_nzfree_pages);
	az_nr_nzfree_pages += i;
	spin_unlock(&az_pmem_global_lock);

	return (i << AZ_PAGE_SHIFT);
}

static void az_pmem_open_accounts(struct az_pmem_accounts *acs)
{
	int i, j;
	struct az_pmem_account *ac;

	atomic_long_set(&acs->azm_rlimit, 0);
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &acs->azmas[i];
		spin_lock_init(&ac->azma_lock);
		for (j = 0; j < AZPLT_NR_PAGE_LIST_TYPES; j++)
			INIT_LIST_HEAD(&ac->subpage_lists[j]);
		ac->azma_balance = 0;
		ac->azma_balance_min = ac->azma_balance_max = ac->azma_balance;
		ac->azma_allocated = 0;
		ac->azma_allocated_min = ac->azma_allocated_max =
			ac->azma_allocated;
		ac->azma_maximum = 0;
		ac->azma_fund = ac->azma_overdraft_fund = NULL;
		/* Set itself as the account to credit the pages to. */
		ac->azma_accts_to_credit = ~0UL;
		ac->azma_accts_to_credit = az_pmem_push_acct_on_list(
				ac->azma_accts_to_credit, i);
	}
}

static inline unsigned long az_pmem_allocated(void)
{
	return (atomic_long_read(&az_nr_pages_allocated) << AZ_PAGE_SHIFT);
}

/*
 * Return number of pages available for the given fund based on the configured
 * funds. Change this logic when we have the actual funds configured from
 * the userland.
 */
static inline size_t az_pmem_get_configured_fund(unsigned long nr)
{
	if (nr < AZMFT_NR_FUND_TYPES)
		return (az_pmem_allocated() *
				az_pmem_configured_funds[nr] / 100);
	else
		return 0;
}

static void az_pmem_adjust_fund(unsigned long fund_nr, long size)
{
	struct az_pmem_fund *fund = &az_pmem_funds[fund_nr];

	spin_lock(&fund->azmf_lock);
	fund->azmf_balance += size;
	fund->azmf_maximum += size;
	spin_unlock(&fund->azmf_lock);
}

static void __az_pmem_unreserve_pages(struct list_head *pl)
{
	struct az_page *az_page;
	unsigned long count = 0;

	while (!list_empty(pl)) {
		az_page = list_first_entry(pl, struct az_page, azp_list);
		list_del_init(&az_page->azp_list);
		az_release_large_page(az_page_to_npage(az_page));
		count++;
	}
	if (count)
		printk(KERN_DEBUG "AZMM: Unreserved %lu pages\n", count);
}

static int az_pmem_unreserve_pages(struct list_head *pl, unsigned long nr_pages,
		spinlock_t *spl)
{
	struct list_head tmp_pl;
	unsigned long count = 0;
	struct az_page *az_page;

	if (list_empty(pl))
		return -1;

	/* TODO enclose in to a loop to split the work and resched in between */
	INIT_LIST_HEAD(&tmp_pl);
	spin_lock(spl);
	if (!nr_pages) {
		list_splice_init(pl, &tmp_pl);
	} else {
		list_for_each_entry(az_page, pl, azp_list) {
			count++;
			if (count == nr_pages)
				break;
		}
		if (count == nr_pages)
			list_cut_position(&tmp_pl, pl, &az_page->azp_list);
	}
	spin_unlock(spl);
	if (count != nr_pages) {
		printk(KERN_WARNING "AZMM: Can't free, in_list:"
				" %lu, to_free: %lu\n", count, nr_pages);
		return -1;
	}

	if (!nr_pages)
		list_for_each_entry(az_page, &tmp_pl, azp_list)
			count++;
	else
		count = nr_pages;

	/* Adjust the counters so that we stop allocating from it */
	spin_lock(spl);
	atomic_long_sub(count, &az_nr_pages_allocated);
	atomic_long_sub(count, &az_nr_pages_requested);
	az_nr_nzfree_pages -= count;
	spin_unlock(spl);
	__az_pmem_unreserve_pages(&tmp_pl);
	az_pmem_adjust_fund(AZMFT_COMMITTED, -(count << AZ_PAGE_SHIFT));
	return 0;

}

static int az_unreserve_global_pages(void)
{
	if (atomic_long_read(&az_nr_pages_allocated) != az_nr_nzfree_pages) {
		printk(KERN_WARNING "AZMM: Can't free, reserved:"
				" %lu, free: %lu\n",
				atomic_long_read(&az_nr_pages_allocated),
				az_nr_nzfree_pages);
		return -EBUSY;
	}
	if (!az_nr_nzfree_pages)
		return 0;
	if (az_pmem_unreserve_pages(&az_nzfree_pages, 0, &az_pmem_global_lock))
		return -EBUSY;
	return 0;
}

static int az_unreserve_pages(void)
{
	int ret;

	ret = az_unreserve_global_pages();
	if (ret)
		return ret;
	return 0;
}

static int __az_pmem_reserve_pages(struct list_head *pl, unsigned long nr_pages)
{
	unsigned long i;
	unsigned long nr_allocated = 0;

	for (i = 0; i < nr_pages; i++) {
		if (!az_grab_large_page(pl, &nr_allocated))
			break;
	}
	printk(KERN_DEBUG "AZMM: Reserved %lu, requested %lu pages\n",
			nr_allocated, nr_pages);

	if (nr_allocated != nr_pages) {
		__az_pmem_unreserve_pages(pl);
		return -ENOMEM;
	}
	return 0;
}

static int az_reserve_pages(struct list_head *pl, unsigned long nr_pages)
{
	unsigned long az_max_pfn = az_npfn_to_az_pfn(max_pfn);
	struct az_page *az_page;
	struct list_head tmp_pl;
	unsigned long nr = 0;
	unsigned long count = 0;

	if (nr_pages <= 0)
		return 0;

	INIT_LIST_HEAD(&tmp_pl);
	if (__az_pmem_reserve_pages(&tmp_pl, nr_pages))
		return -ENOMEM;

	spin_lock(&az_pmem_global_lock);
	list_splice_init(&tmp_pl, pl);
	atomic_long_add(nr_pages, &az_nr_pages_requested);
	atomic_long_add(nr_pages, &az_nr_pages_allocated);
	az_nr_nzfree_pages += nr_pages;
	spin_unlock(&az_pmem_global_lock);

	az_pmem_adjust_fund(AZMFT_COMMITTED, (nr_pages << AZ_PAGE_SHIFT));
	list_for_each_entry(az_page, pl, azp_list)
		count++;

	/* Walk thru the az_vmemmap and count az_page objects in it */
	for (az_page = az_vmemmap;
			az_page < (az_vmemmap + az_max_pfn);
			az_page++) {
		if (az_page && AZ_PAGE_MANAGED(az_page))
			nr++;
	}
	printk(KERN_DEBUG "AZMM: Reserved @ 0x%p, max_pfn=%lu, in_list= %lu "
			"nr_vmem=%lu, nr_requested=%lu, nr_allocated=%lu\n",
			az_vmemmap, max_pfn, count, nr, nr_pages, nr_pages);
	return 0;
}

static void az_pmem_init_funds(void)
{
	int i;
	struct az_pmem_fund *fund;

	for (i = 0; i < AZMFT_NR_FUND_TYPES; i++) {
		fund = &az_pmem_funds[i];
		spin_lock_init(&fund->azmf_lock);
	}
}

int az_ioc_pmem_set_maximum(pid_t pid, size_t size)
{
	long rc;
	struct task_struct *tsk;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	rc = __az_pmem_set_maximum(tsk->mm, size);
	put_task_struct(tsk);
	return rc;
}

int az_ioc_pmem_set_account_maximum(pid_t pid,
		unsigned long acct, size_t size)
{
	long rc;
	struct az_pmem_account *ac;
	struct task_struct *tsk;
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);
	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	ac = &pool->azp_accounts.azmas[acct];
	rc = __az_pmem_set_account_maximum(ac, size);
	put_task_struct(tsk);
	return rc;
}

int az_ioc_pmem_fund_account(pid_t pid,
		unsigned long acct, size_t size)
{
	long rc;
	struct az_pmem_account *ac;
	struct task_struct *tsk; 
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);
	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	ac = &pool->azp_accounts.azmas[acct];
	rc = __az_pmem_adjust_account_fund(ac, size);
	if (!rc)
		az_enqueue_pool(&az_xprocess_pools, pool,
				&az_nr_xprocess_pools);
	put_task_struct(tsk);
	return rc;
}

int az_ioc_pmem_account_transfer(pid_t pid,
		unsigned long dst, unsigned long src, size_t size)
{
	long rc;
	struct task_struct *tsk; 
	struct az_pmem_accounts *acs;
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(src) || !az_pmem_valid_acct(dst))
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);
	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	acs = &pool->azp_accounts;
	rc = __az_pmem_account_transfer(&acs->azmas[dst], &acs->azmas[src],
			size);
	put_task_struct(tsk);
	return rc;
}

/*
 * FIXME The stats are coherent at account level but not at process level as the
 * lock is at account level.
 */
int az_ioc_pmem_get_account_stats(pid_t pid,
		void __user * statsp, size_t __user * sizep)
{
	long rc;
	struct task_struct *tsk;
	size_t size;
	azm_account_stats_s *stats;
	struct az_pmem_pool *pool;

	size = sizeof(azm_account_stats_s);
	if (!statsp) {
		if (put_user(size, sizep))
			return -EFAULT;
		return 0;
	}

	if (get_user(size, sizep))
		return -EFAULT;

	if (size < sizeof(azm_account_stats_s))
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	rc = -ENOMEM;
	size = sizeof(azm_account_stats_s);
	stats = (azm_account_stats_s*) kzalloc(size, GFP_KERNEL);
	if (!stats)
		goto done;

	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	__az_pmem_get_account_stats(&pool->azp_accounts, stats);

	rc = -EFAULT;
	if (put_user(size, sizep))
		goto done;
	if (copy_to_user(statsp, stats, size))
		goto done;
	rc = 0;
done:
	if (stats)
		kfree(stats);
	put_task_struct(tsk);
	return rc;
}

/*
 * FIXME The stats are coherent at fund level but not at system level as the
 * lock is at fund level.
 */
int az_ioc_pmem_get_fund_stats(void __user * statsp, size_t __user * sizep)
{
	size_t size;
	azm_fund_stats_s *stats;
	long rc;

	size = sizeof(azm_fund_stats_s);
	if (!statsp) {
		if (put_user(size, sizep))
			return -EFAULT;
		return 0;
	}

	if (get_user(size, sizep))
		return -EFAULT;

	if (size < sizeof(azm_fund_stats_s))
		return -EINVAL;

	size = sizeof(azm_fund_stats_s);
	stats = (azm_fund_stats_s*) kzalloc(size, GFP_KERNEL);
	if (!stats)
		return -ENOMEM;

	__az_pmem_get_fund_stats(stats);

	if (put_user(size, sizep))
		goto err_fault;
	if (copy_to_user(statsp, stats, size))
		goto err_fault;
	rc = 0;
out:
	kfree(stats);
	return rc;
err_fault:
	rc = -EFAULT;
	goto out;
}

int az_ioc_pmem_set_account_funds(pid_t pid, unsigned long acct,
		unsigned long commit, unsigned long overdraft)
{
	long rc;
	struct az_pmem_account *ac;
	struct task_struct *tsk;
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(acct) ||
			commit >= AZMFT_NR_FUND_TYPES ||
			overdraft >= AZMFT_NR_FUND_TYPES)
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);
	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	ac = &pool->azp_accounts.azmas[acct];
	rc = __az_pmem_set_account_funds(ac, commit, overdraft);
	put_task_struct(tsk);
	return rc;
}

int az_ioc_pmem_set_accounts_to_credit(pid_t pid,
		unsigned long acct, unsigned long accts_to_credit)
{
	long rc;
	struct az_pmem_account *ac;
	struct task_struct *tsk;
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(acct) ||
			!az_pmem_acct_list_sane(accts_to_credit))
		return -EINVAL;

	tsk = az_get_task_struct(pid);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);
	pool = az_mm_azm_pool_auto(tsk->mm);
	if (!pool)
		return -ENOMEM;
	ac = &pool->azp_accounts.azmas[acct];
	rc = __az_pmem_set_accounts_to_credit(ac, accts_to_credit);
	put_task_struct(tsk);
	return rc;
}

int az_ioc_pmem_fund_transfer(unsigned long dst, unsigned long src, size_t size)
{
	long rc;

	if (src >= AZMFT_NR_FUND_TYPES || dst >= AZMFT_NR_FUND_TYPES ||
			(src == dst))
		return -EINVAL;

	rc = __az_pmem_fund_transfer(&az_pmem_funds[dst], &az_pmem_funds[src],
			size);
	return rc;
}

int az_ioc_pmem_reserve_pages(unsigned long nr_pages)
{
	return az_reserve_pages(&az_nzfree_pages, nr_pages);
}

int az_ioc_pmem_unreserve_pages(unsigned long nr_pages)
{
	return az_pmem_unreserve_pages(&az_nzfree_pages, nr_pages,
			&az_pmem_global_lock);
}

int az_ioc_pmem_reset_account_watermarks(unsigned long acct)
{
	struct az_pmem_account *ac;
	struct az_pmem_pool *pool;

	if (!az_pmem_valid_acct(acct))
		return -EINVAL;

	pool = az_mm_azm_pool(current->mm);
	if (!pool)
		return -ESRCH;

	ac = &pool->azp_accounts.azmas[acct];
	ac->azma_balance_min = ac->azma_balance_max = ac->azma_balance;
	ac->azma_allocated_min = ac->azma_allocated_max = ac->azma_allocated;

	return 0;
}

static void az_clear_large_page(struct az_page *az_page, unsigned long sz)
{
	int i;
	struct page *page = az_page_to_npage(az_page);

	might_sleep();
	for (i = 0; i < sz/PAGE_SIZE; i++) {
		cond_resched();
		clear_highpage(page + i);
	}
}

static struct az_page* az_pmem_get_xprocess_page(void)
{
	struct az_pmem_pool *pool;
	struct az_page *az_page = NULL;

	spin_lock(&az_pmem_global_lock);
	list_for_each_entry(pool, &az_xprocess_pools, azp_list) {
		spin_lock(&pool->azp_lock);
		az_page = az_dequeue_large_page(
				&pool->azp_lists[AZPLT_FLUSHED_PAGES],
				&pool->azp_nr_pages_in_lists);
		spin_unlock(&pool->azp_lock);
		if (az_page)
			break;
	}
	spin_unlock(&az_pmem_global_lock);
	return az_page;
}

/* XXX Call to __az_pmem_*_*page should be made with pool level lock released */
void __az_pmem_free_page(struct az_pmem_pool *pool, struct page *page,
		unsigned long flags)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	struct list_head *pl;
	struct az_pmem_account *ac;

	VM_BUG_ON(page_count(page)); /* Make sure the page is not referenced */
	VM_BUG_ON(page->mapping !=
			(struct address_space *) AZ_PAGE_MAPPING_POISON);
	/*
	 * If this page was shattered then the subpage healing logic should
	 * set these fields properly before freeing it.
	 */
	spin_lock(&pool->azp_lock);
	BUG_ON(!bitmap_full(az_page->azp_bmap, AZ_NR_SUBPAGES_PER_PAGE));
	BUG_ON(az_page->azp_shift != AZ_PAGE_SHIFT);

	if (flags & AZ_PMEM_TLB_INVALIDATED)
		pl = &pool->azp_lists[AZPLT_FLUSHED_PAGES];
	else if (flags & AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE)
		pl = &pool->azp_lists[AZPLT_RELEASED_PAGES];
	else
		pl = &pool->azp_lists[AZPLT_RELEASED_STRICT_PAGES];
	az_enqueue_page(pl, az_page, &pool->azp_nr_pages_in_lists);
	spin_unlock(&pool->azp_lock);

	/* Unaccount the page */
	ac = &pool->azp_accounts.azmas[az_page->azp_acct];
	az_pmem_credit_accounts(&pool->azp_accounts, ac->azma_accts_to_credit,
			AZ_PAGE_SIZE);

	az_pmem_put_pool(pool);
}

void az_pmem_free_page(struct az_pmem_pool *pool, unsigned long addr,
		unsigned long flags)
{
	struct page *page = virt_to_page(addr);
	return __az_pmem_free_page(pool, page, flags);
}

struct page *__az_pmem_alloc_page(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags)
{
	struct az_page *az_page = NULL;
	struct page *page = NULL;
	struct list_head *pl = &pool->azp_lists[AZPLT_FLUSHED_PAGES];
	struct list_head *rpl = &pool->azp_lists[AZPLT_RELEASED_PAGES];
	struct list_head *rspl = &pool->azp_lists[AZPLT_RELEASED_STRICT_PAGES];
	int zero_page = !(flags & AZ_PMEM_ALLOCATE_REQUEST_NOZERO);
	int alloc_released = flags & AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE;
	long rc = 0;
	int overdraft = !(flags & AZ_PMEM_ALLOCATE_NO_OVERDRAFT);

	/* See if we can account the page */
	rc = az_pmem_account_withdraw(&pool->azp_accounts, accno,
			overdraft, AZ_PAGE_SIZE);
	if (rc)
		return NULL;

	spin_lock(&pool->azp_lock);
	if (alloc_released)
		az_page = az_dequeue_large_page(rpl,
				&pool->azp_nr_pages_in_lists);

	if (!az_page) {
		az_page = az_dequeue_large_page(pl,
				&pool->azp_nr_pages_in_lists);
		if (az_page)
			az_page->azp_flags |= AZPGF_ALLOCED_FROM_FLUSHED;
	}

	/*
	 * TODO Fix the race here between the tlb flush and the others trying
	 * allocate pages at the same time and potentially hitting the NOMEM
	 * because of their memory accounting limits.
	 */
	/* Try to get a page from the released list by flushing */
	if (!az_page && (!list_empty(rspl) || !list_empty(rpl))) {
		spin_unlock(&pool->azp_lock);
		az_pmem_flush_tlb_and_released_pages(
				az_mm_mmstate(current->mm));
		spin_lock(&pool->azp_lock);
		az_page = az_dequeue_large_page(pl,
				&pool->azp_nr_pages_in_lists);
		if (az_page)
			az_page->azp_flags |= AZPGF_ALLOCED_FROM_FLUSHED;
	}

	if (!az_page) {
		/* Try getting a page from the global lists */
		spin_unlock(&pool->azp_lock);
		/*
		 * TODO May be we should check again at the free list as the
		 * extra pages allocated were back in the list while were not
		 * holding the lock.
		 */
		spin_lock(&az_pmem_global_lock);
		az_page = az_dequeue_large_page(&az_zfree_pages,
				&az_nr_zfree_pages);
		if (!az_page) {
			az_page = az_dequeue_large_page(&az_nzfree_pages,
					&az_nr_nzfree_pages);
			zero_page = 1;
		}
		spin_unlock(&az_pmem_global_lock);
		if (!az_page) {
			/* Try getting the page from xprocess pool list */
			az_page = az_pmem_get_xprocess_page();
			zero_page = 1;
		}
		if (!az_page)
			goto out_err;

		az_page->azp_rl_until_invalidated_count = 0;
		az_page->azp_rls_until_invalidated_count = 0;
		az_page->azp_flags |= AZPGF_ALLOCED_FROM_FLUSHED;
		spin_lock(&pool->azp_lock);
	}
	az_pmem_get_pool(pool);
	az_page->azp_pool = pool;
	az_page->azp_acct = accno;
	spin_unlock(&pool->azp_lock);

	if (zero_page)
		az_clear_large_page(az_page, AZ_PAGE_SIZE);
	page = az_page_to_npage(az_page);
	az_set_page_refcounted(page);
	return page;

out_err:
	/* Deposit the withdrew amount back */
	az_pmem_credit_accounts(&pool->azp_accounts,
			pool->azp_accounts.azmas[accno].azma_accts_to_credit,
			AZ_PAGE_SIZE);
	return NULL;
}

unsigned long az_pmem_alloc_page(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags)
{
	struct page *page;

	page = __az_pmem_alloc_page(pool, accno, flags);
	if (!page)
		return 0UL;

	return (unsigned long)page_address(page);
}

void __az_pmem_free_subpage(struct az_pmem_pool *pool, struct page *page,
		unsigned long flags)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	unsigned long n;
	struct list_head *pl;
	struct az_pmem_account *ac;

	VM_BUG_ON(page_count(page)); /* Make sure the page is not referenced */
	VM_BUG_ON(page->mapping !=
			(struct address_space *) AZ_PAGE_MAPPING_POISON);
	ac = &pool->azp_accounts.azmas[az_page->azp_acct];

	spin_lock(&pool->azp_lock);
	BUG_ON(az_page->azp_shift != PAGE_SHIFT);
	if (!(flags & AZ_PMEM_TLB_INVALIDATED)) {
		if (flags & AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE)
			az_page->azp_rl_until_invalidated_count = 
				pool->azp_invalidated_count + 1;
		else 
			az_page->azp_rls_until_invalidated_count = 
				pool->azp_invalidated_count + 1;
	}

	n = page_to_pfn(page) & (~AZ_SUBPAGES_MASK);
	__set_bit(n, az_page->azp_bmap);
	if (!list_empty(&az_page->azp_list)) {
		BUG_ON(!pool->azp_nr_subpages_in_lists);
		/* We can't use the dequeue here as the page is not the head */
		list_del_init(&az_page->azp_list);
		pool->azp_nr_subpages_in_lists--;
	}

	/*
	 * Set the flags based on the invalidate counts for the
	 * __az_pmem_free_page to put it in the right list if we endup freeing
	 * back to large page list.
	 */
	if (az_page->azp_rls_until_invalidated_count > 
			pool->azp_invalidated_count) {
		pl = &ac->subpage_lists[AZPLT_RELEASED_STRICT_PAGES];
		flags = 0;
	} else if (az_page->azp_rl_until_invalidated_count > 
			pool->azp_invalidated_count) {
		pl = &ac->subpage_lists[AZPLT_RELEASED_PAGES];
		flags = AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE;
	} else {
		BUG_ON(!(flags & AZ_PMEM_TLB_INVALIDATED));
		pl = &ac->subpage_lists[AZPLT_FLUSHED_PAGES];
		flags = AZ_PMEM_TLB_INVALIDATED;
	}

	/* Put back in the subpage list as all the subpages are not freed yet */
	if (!bitmap_full(az_page->azp_bmap, AZ_NR_SUBPAGES_PER_PAGE)) {
		az_enqueue_page(pl, az_page, &pool->azp_nr_subpages_in_lists);
		spin_unlock(&pool->azp_lock);
		return;
	}

	/* All the subpages in this az_page are freed, so free the az_page */
	az_page->azp_shift = AZ_PAGE_SHIFT;
	spin_unlock(&pool->azp_lock);
	__az_pmem_free_page(pool, az_page_to_npage(az_page), flags);

}

void az_pmem_free_subpage(struct az_pmem_pool *pool, unsigned long addr,
		unsigned long flags)
{
	/* FIXME check for the addr alignment */
	__az_pmem_free_subpage(pool, virt_to_page(addr), flags);
}

struct page* __az_pmem_alloc_subpage(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags)
{
	struct az_page *az_page = NULL;
	struct page *extra_pg = NULL;
	struct page *page;
	int64_t n;
	struct az_pmem_account *ac = &pool->azp_accounts.azmas[accno];
	struct list_head *fpl = &ac->subpage_lists[AZPLT_FLUSHED_PAGES];
	struct list_head *rpl = &ac->subpage_lists[AZPLT_RELEASED_PAGES];
	struct list_head *rspl =&ac->subpage_lists[AZPLT_RELEASED_STRICT_PAGES];
	int alloc_released = flags & AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE;
	int zero_page = !(flags & AZ_PMEM_ALLOCATE_REQUEST_NOZERO);

	spin_lock(&pool->azp_lock);

	if (alloc_released && !list_empty(rpl))
		az_page =  list_first_entry(rpl, struct az_page, azp_list);

	if ((!az_page) && !list_empty(fpl))
		az_page =  list_first_entry(fpl, struct az_page, azp_list);

	if (!az_page && (!list_empty(rspl) || !list_empty(rpl))) {
		spin_unlock(&pool->azp_lock);
		az_pmem_flush_tlb_and_released_pages(
				az_mm_mmstate(current->mm));
		spin_lock(&pool->azp_lock);
		if (!list_empty(fpl))
			az_page =  list_first_entry(fpl,
					struct az_page, azp_list);
	}

	if (!az_page) {
		spin_unlock(&pool->azp_lock);
		/* Set the flag so that alloc_page doesn't zero the 2M page */
		flags |= AZ_PMEM_ALLOCATE_REQUEST_NOZERO;
		/* Race to allocate a large page */
		page = __az_pmem_alloc_page(pool, accno, flags);
		/*
		 * TODO Check for the in-flight allocations and wait for them to
		 * get a large page and share it when we fail to get one for us.
		 */
		if (!page)
			return NULL;

		/* Check again if the list still empty  after the race */
		spin_lock(&pool->azp_lock);
		put_page_testzero(page); /* Drop the large page allocator ref */
		if (list_empty(fpl) && (list_empty(rpl) || !alloc_released)) {
			struct list_head *pl;
			az_page = az_npage_to_az_page(page);
			BUG_ON(!bitmap_full(az_page->azp_bmap,
						AZ_NR_SUBPAGES_PER_PAGE));
			BUG_ON(az_page->azp_shift != AZ_PAGE_SHIFT);
			az_page->azp_shift = PAGE_SHIFT;
			pl = AZ_PAGE_ALLOCED_FROM_FLUSHED(az_page) ? fpl : rpl;
			az_enqueue_page(pl, az_page,
					&pool->azp_nr_subpages_in_lists);
		} else { /* We lost in the race, free the allocated page */
			extra_pg = page;
			/* use az_page from whatever list got us here : */
			if (!list_empty(fpl))
				az_page =  list_first_entry(fpl, struct az_page,
						azp_list);
			else {
				BUG_ON(list_empty(rpl));
				az_page =  list_first_entry(rpl, struct az_page,
						azp_list);
			}
		}
	}
	BUG_ON(bitmap_empty(az_page->azp_bmap, AZ_NR_SUBPAGES_PER_PAGE));
	BUG_ON(az_page->azp_shift != PAGE_SHIFT);
	n = az_find_first_bit(az_page->azp_bmap);
	__clear_bit(n, az_page->azp_bmap);
	/*
	 * Remove it from the list if it is fully allocated and add later when
	 * we have the first page freed.
	 */
	if (bitmap_empty(az_page->azp_bmap, AZ_NR_SUBPAGES_PER_PAGE)) {
		list_del_init(&az_page->azp_list);
		pool->azp_nr_subpages_in_lists--;
	}
	spin_unlock(&pool->azp_lock);
	page =  pfn_to_page(az_page_to_npfn(az_page) + n);
	az_set_page_refcounted(page);
	if (zero_page)
		clear_highpage(page);

	if (extra_pg) {
		unsigned long free_flags = (AZ_PAGE_ALLOCED_FROM_FLUSHED(
					az_npage_to_az_page(extra_pg)) ? 
				AZ_PMEM_TLB_INVALIDATED :
				AZ_PMEM_MAY_RECYCLE_BEFORE_INVALIDATE);
		__az_pmem_free_page(pool, extra_pg, free_flags);
	}

	return page;
}

unsigned long az_pmem_alloc_subpage(struct az_pmem_pool *pool,
		unsigned long accno, unsigned long flags)
{
	struct page *page;

	page = __az_pmem_alloc_subpage(pool, accno, flags);

	if (!page)
		return 0UL;
	return (unsigned long)page_address(page);
}

#define AZ_PMEM_DEBUG_POOL     1
void az_pmem_pool_stats(struct az_pmem_pool *pool)
{
	unsigned long nr_used, nr_pages, nr_subpages, nr_uref_pages;
#ifdef AZ_PMEM_DEBUG_POOL
	unsigned long nr_released_pages, nr_released_subpages;
	unsigned long nr_flushed_pages, nr_flushed_subpages;
	unsigned long nr_released_strict_pages, nr_released_strict_subpages;
	struct list_head *iter;
	struct az_pmem_account *ac;
	int i;

	nr_released_subpages = nr_released_pages = 0;
	nr_flushed_subpages = nr_flushed_pages = 0;
	nr_released_strict_subpages = nr_released_strict_pages = 0;
#endif /* AZ_PMEM_DEBUG_POOL */

	spin_lock(&pool->azp_lock);
	nr_used = atomic_read(&pool->azp_ref_count) - 1; /* exclude mm's ref */
	nr_pages = pool->azp_nr_pages_in_lists;
	nr_subpages = pool->azp_nr_subpages_in_lists;
	nr_uref_pages = pool->azp_nr_uref_pages;
#ifdef AZ_PMEM_DEBUG_POOL
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &pool->azp_accounts.azmas[i];
		list_for_each(iter, 
				&ac->subpage_lists[AZPLT_RELEASED_STRICT_PAGES])
			nr_released_strict_subpages++;
		list_for_each(iter, &ac->subpage_lists[AZPLT_RELEASED_PAGES])
			nr_released_subpages++;
		list_for_each(iter, &ac->subpage_lists[AZPLT_FLUSHED_PAGES])
			nr_flushed_subpages++;
	}
	list_for_each(iter, &pool->azp_lists[AZPLT_RELEASED_STRICT_PAGES])
		nr_released_strict_pages++;
	list_for_each(iter, &pool->azp_lists[AZPLT_RELEASED_PAGES])
		nr_released_pages++;
	list_for_each(iter, &pool->azp_lists[AZPLT_FLUSHED_PAGES])
		nr_flushed_pages++;
#endif /* AZ_PMEM_DEBUG_POOL */
	spin_unlock(&pool->azp_lock);

	printk(KERN_DEBUG "AZPM: nr_used %ld, nr_pages %ld, nr_subpages %ld "
			"nr_uref_pages %ld\n",
			nr_used, nr_pages, nr_subpages, nr_uref_pages);
#ifdef AZ_PMEM_DEBUG_POOL
	printk(KERN_DEBUG "    nr_flushed_pages %ld, nr_flushed_subpages %ld"
			" nr_released_pages %ld, nr_released_subpages %ld,"
			" nr_released_strict_pages %ld, "
			"nr_released_strict_subpages %ld\n",
			nr_flushed_pages, nr_flushed_subpages,
			nr_released_pages, nr_released_subpages,
			nr_released_strict_pages, nr_released_strict_subpages);
#endif /* AZ_PMEM_DEBUG_POOL */
}

static void az_pmem_tlb_add_to_flushed_lists(struct az_pmem_pool *pool,
		struct list_head *rl_lists, struct list_head *rls_lists)
{
	int i;
	struct az_pmem_account *ac = &pool->azp_accounts.azmas[0];

	spin_lock(&pool->azp_lock);
	/* Add the subpages lists for each account */
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		list_splice_init(&rl_lists[i],
				&ac->subpage_lists[AZPLT_FLUSHED_PAGES]);
		list_splice_init(&rls_lists[i],
				&ac->subpage_lists[AZPLT_FLUSHED_PAGES]);
		ac++;
	}
	/* Add the pages list */
	list_splice_init(&rl_lists[i], &pool->azp_lists[AZPLT_FLUSHED_PAGES]);
	list_splice_init(&rls_lists[i], &pool->azp_lists[AZPLT_FLUSHED_PAGES]);
	pool->azp_invalidated_count++;
	spin_unlock(&pool->azp_lock);
}

static void az_pmem_tlb_extract_released_lists(struct az_pmem_pool *pool,
		struct list_head *rl_lists, struct list_head *rls_lists)
{
	int i;
	struct az_pmem_account *ac = &pool->azp_accounts.azmas[0];

	spin_lock(&pool->azp_lock);
	/* Extract the subpages lists for each account */
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		INIT_LIST_HEAD(&rl_lists[i]);
		INIT_LIST_HEAD(&rls_lists[i]);
		list_splice_init(&ac->subpage_lists[AZPLT_RELEASED_PAGES],
				&rl_lists[i]);
		list_splice_init(&ac->subpage_lists[AZPLT_RELEASED_STRICT_PAGES],
				&rls_lists[i]);
		ac++;
	}
	/* Extract the pages list */
	INIT_LIST_HEAD(&rl_lists[i]);
	INIT_LIST_HEAD(&rls_lists[i]);
	list_splice_init(&pool->azp_lists[AZPLT_RELEASED_PAGES], &rl_lists[i]);
	list_splice_init(&pool->azp_lists[AZPLT_RELEASED_STRICT_PAGES],
			&rls_lists[i]);
	spin_unlock(&pool->azp_lock);
}

void az_pmem_flush_tlb_and_released_pages(az_mmstate *mms)
{
	struct az_pmem_pool *pool = mms->azm_pool;
	struct list_head rl_lists[AZMAT_NR_ACCOUNT_TYPES + 1];
	struct list_head rls_lists[AZMAT_NR_ACCOUNT_TYPES + 1];
	BUG_ON(!pool);

	az_pmem_tlb_extract_released_lists(pool, rl_lists, rls_lists);
	flush_tlb_mm(mms->mm);
	az_pmem_tlb_add_to_flushed_lists(pool, rl_lists, rls_lists);
}

/*
 * XXX This function assumes that the free subpages and the released
 * pages/subpages lists are empty by the time we get here.
 * Flushes out the free pages list to the non-zeroed global free pages list
 * with local copy.
 */
static void az_pmem_empty_pool(struct az_pmem_pool *pool)
{
	struct list_head *pl = &pool->azp_lists[AZPLT_FLUSHED_PAGES];
	uint64_t nr_pages = pool->azp_nr_pages_in_lists;
	struct list_head tmp_pl;
#ifdef AZ_PMEM_DEBUG_POOL
	unsigned long nr_pages_leaked = 0;
	int i, j;
	struct list_head *iter;
	struct az_pmem_accounts *acs = &pool->azp_accounts;
#endif /* AZ_PMEM_DEBUG_POOL */

	/* Extract the list to local copy */
	INIT_LIST_HEAD(&tmp_pl);
	spin_lock(&pool->azp_lock);
	list_splice_init(pl, &tmp_pl); /* Extract flushed pages first */
#ifdef AZ_PMEM_DEBUG_POOL
	for (i = AZPLT_RELEASED_PAGES; i < AZPLT_NR_PAGE_LIST_TYPES; i++) {
		pl = &pool->azp_lists[i];
		list_for_each(iter, pl)
			nr_pages_leaked++;
	}
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++)
		for (j = 0; j < AZPLT_NR_PAGE_LIST_TYPES; j++) {
			pl = &acs->azmas[i].subpage_lists[j];
			list_for_each(iter, pl)
				nr_pages_leaked++;
		}
	nr_pages = pool->azp_nr_pages_in_lists - nr_pages_leaked;
#endif /* AZ_PMEM_DEBUG_POOL */
	pool->azp_nr_pages_in_lists = 0;
	/* We can release the lock as we reset the pool->azp_nr_pages_in_lists*/
	spin_unlock(&pool->azp_lock);
	BUG_ON(nr_pages < 0);
#ifdef AZ_PMEM_DEBUG_POOL
	if (nr_pages_leaked) {
		printk(KERN_ALERT "*** AZPM: Proc[%d] leaked %ld pages ***\n",
				current->pid, nr_pages_leaked);
		az_pmem_pool_stats(pool);
	}
#endif /* AZ_PMEM_DEBUG_POOL */
	if (!nr_pages)
		return;

	spin_lock(&az_pmem_global_lock);
	list_splice_init(&tmp_pl, &az_nzfree_pages);
	az_nr_nzfree_pages += nr_pages;
	spin_unlock(&az_pmem_global_lock);
}

static void az_pmem_exit_pool(struct az_pmem_pool *pool)
{
	az_dequeue_pool(pool, &az_nr_xprocess_pools);
	az_pmem_empty_pool(pool);
}

static inline long __az_pmem_add_user_ref(struct page *page, int cnt)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	unsigned long new_refcnt;
	struct az_pmem_pool *pool = az_page->azp_pool;

	BUG_ON(!atomic_read(&page->_count));
	BUG_ON(!AZ_PAGE_MANAGED(az_page));
	BUG_ON(az_page->azp_shift != AZ_PAGE_SHIFT);
	spin_lock(&pool->azp_lock);
	az_get_npages(page, cnt);
	new_refcnt = az_page->azp_user_refcnt;
	if (new_refcnt == cnt) {
		az_enqueue_page(&pool->azp_uref_pages, az_page,
				&pool->azp_nr_uref_pages);
	}
	spin_unlock(&pool->azp_lock);
	return new_refcnt;
}

long az_pmem_add_user_ref(struct page *page, int cnt)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	if (cnt <= 0)
		return -EINVAL;
	if (az_mm_azm_pool(current->mm) != az_page->azp_pool)
		return -EACCES;
	return __az_pmem_add_user_ref(page, cnt);
}

static inline long __az_pmem_sub_user_ref(struct page *page, unsigned int cnt)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	unsigned long new_refcnt;
	struct az_pmem_pool *pool = az_page->azp_pool;

	BUG_ON(!atomic_read(&page->_count));
	BUG_ON(!AZ_PAGE_MANAGED(az_page));
	BUG_ON(az_page->azp_shift != AZ_PAGE_SHIFT);
	spin_lock(&pool->azp_lock);
	/* see if we can subtract the user refs */
	if (az_page->azp_user_refcnt < cnt)
		goto err;
	az_page->azp_user_refcnt -= cnt;
	new_refcnt = az_page->azp_user_refcnt;
	if (!new_refcnt) {
		/* Remove from the user_ref list and put back to free pool */
		BUG_ON(!pool->azp_nr_uref_pages);
		list_del_init(&az_page->azp_list);
		pool->azp_nr_uref_pages--;
	}
	if (cnt > 1)
		az_put_npages(page, cnt - 1);
	az_pmem_put_page(page, 0);
out:
	spin_unlock(&pool->azp_lock);
	return new_refcnt;
err:
	new_refcnt = -EINVAL;
	goto out;
}

long az_pmem_sub_user_ref(struct page *page, int cnt)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	if (cnt <= 0)
		return -EINVAL;
	if (az_mm_azm_pool(current->mm) != az_page->azp_pool)
		return -EACCES;
	return __az_pmem_sub_user_ref(page, cnt);
}

static inline long __az_pmem_read_user_ref(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);
	unsigned long urefcnt;

	BUG_ON(!AZ_PAGE_MANAGED(az_page));
	BUG_ON(az_page->azp_shift != AZ_PAGE_SHIFT);
	spin_lock(&az_page->azp_pool->azp_lock);
	urefcnt = az_page->azp_user_refcnt;
	spin_unlock(&az_page->azp_pool->azp_lock);
	return urefcnt;
}

long az_pmem_read_user_ref(struct page *page)
{
	struct az_page *az_page = az_npage_to_az_page(page);

	if (az_mm_azm_pool(current->mm) != az_page->azp_pool)
		return -EACCES;
	return __az_pmem_read_user_ref(page);
}

static void az_pmem_put_user_ref_pages(struct list_head *pl)
{
	struct az_page *pos, *next;

	list_for_each_entry_safe(pos, next, pl, azp_list) {
		if (az_pmem_sub_user_ref(az_page_to_npage(pos),
					pos->azp_user_refcnt) < 0)
			printk(KERN_ERR "AZMM: Failed to release user ref page "
					"pfn %lu, refcnt %lu\n",
					az_page_to_npfn(pos),
					pos->azp_user_refcnt);
	}
}

static void az_pmem_put_pool(struct az_pmem_pool *pool)
{
	if (az_pmem_put_pool_testzero(pool)) {
		az_pmem_put_user_ref_pages(&pool->azp_uref_pages);
		az_pmem_exit_pool(pool);
		az_pmem_close_accounts(&pool->azp_accounts);
		kfree(pool);
	}
}

void az_pmem_exit_mm(az_mmstate *mms)
{
	struct az_pmem_pool *pool = mms->azm_pool;
	if (!pool)
		return;

	/* 
	 * Flush any residual released pages.
	 * [note: caller must not have a tlb open]
	 */
	az_pmem_flush_tlb_and_released_pages(mms);
	mms->azm_pool = NULL;
#ifdef AZ_PMEM_DEBUG_POOL
	if (atomic_read(&pool->azp_ref_count) > 1)
		az_pmem_pool_stats(pool);
#endif /* AZ_PMEM_DEBUG_POOL */
	az_pmem_put_pool(pool); /* Drop mm's ref */
}

long az_pmem_init_mm(az_mmstate *mms)
{
	struct az_pmem_pool *pool;

	pool = (struct az_pmem_pool *)kzalloc(sizeof(struct az_pmem_pool),
			GFP_KERNEL);
	if (!pool) {
		printk(KERN_INFO "AZPM: %s failed: -ENOMEM\n", __func__);
		return -ENOMEM;
	}

	az_pmem_open_accounts(&pool->azp_accounts);
	az_pmem_init_pool(pool);
	mms->azm_pool = pool;
	return 0;
}

#if 0
/*
 *  - Used for /proc/<pid>/az_pmem.
 */
static int az_pmem_show_proc_azpmem(struct seq_file *m, void *unused)
{
	struct pid *pid;
	struct task_struct *tsk;
	int i, on_list = 0;
	struct az_pmem_pool *pool;
	unsigned long nr_used, nr_pages, nr_subpages;
	azm_account_stats_s stats;
#ifdef AZ_PMEM_DEBUG_POOL
	unsigned long nr_released_pages, nr_released_subpages;
	unsigned long nr_flushed_pages, nr_flushed_subpages;
	unsigned long nr_released_strict_pages, nr_released_strict_subpages;
	struct list_head *iter;
	struct az_pmem_account *ac;

	nr_released_subpages = nr_released_pages = 0;
	nr_flushed_subpages = nr_flushed_pages = 0;
	nr_released_strict_subpages = nr_released_strict_pages = 0;
#endif /* AZ_PMEM_DEBUG_POOL */

	pid = m->private;
	tsk = get_pid_task(pid, PIDTYPE_PID);
	if (!tsk)
		return -ESRCH;
	if (!tsk->mm)
		goto out;
	pool = az_mm_azm_pool(tsk->mm);
	if (!pool)
		goto out;

	__az_pmem_get_account_stats(&pool->azp_accounts, &stats);
	seq_printf(m, "Process level mem_rlimit: %lu kB\n",
			stats.process_limit >> 10);
	seq_printf(m, "%3s  %26s  %26s  %14s  %18s  %3s  %5s\n",
		   "AC#", "BALANCE(now/min/max kB)", "USED(now/min/max kB)",
		   "MAXIMUM(kB)", "ACS_TO_CREDIT", "FND", "ODFND");
	for (i = 0; i < stats.nr_accts; i++)
		if (stats.ac[i].ac_fund >= 0 ||
				stats.ac[i].ac_overdraft_fund >= 0)
			seq_printf(m, "[%d] %8lld/%8lld/%8lld  %8lu/%8lu/%8lu  "
				   "%14lu  0x%16lx %3lld  %5lld\n",
				   i,
				   stats.ac[i].ac_balance >> 10,
				   stats.ac[i].ac_balance_min >> 10,
				   stats.ac[i].ac_balance_max >> 10,
				   stats.ac[i].ac_allocated >> 10,
				   stats.ac[i].ac_allocated_min >> 10,
				   stats.ac[i].ac_allocated_max >> 10,
				   stats.ac[i].ac_maximum >> 10,
				   stats.ac[i].ac_accts_to_credit,
				   stats.ac[i].ac_fund,
				   stats.ac[i].ac_overdraft_fund);
	seq_printf(m, "\n");

	spin_lock(&pool->azp_lock);
	nr_used = atomic_read(&pool->azp_ref_count) - 1;  /* exclude mm's ref */
	nr_pages = pool->azp_nr_pages_in_lists;
	nr_subpages = pool->azp_nr_subpages_in_lists;
	on_list = list_empty(&pool->azp_list) ? 0 : 1;
#ifdef AZ_PMEM_DEBUG_POOL
	for (i = 0; i < AZMAT_NR_ACCOUNT_TYPES; i++) {
		ac = &pool->azp_accounts.azmas[i];
		list_for_each(iter,
				&ac->subpage_lists[AZPLT_RELEASED_STRICT_PAGES])
			nr_released_strict_subpages++;
		list_for_each(iter, &ac->subpage_lists[AZPLT_RELEASED_PAGES])
			nr_released_subpages++;
		list_for_each(iter, &ac->subpage_lists[AZPLT_FLUSHED_PAGES])
			nr_flushed_subpages++;
	}
	list_for_each(iter, &pool->azp_lists[AZPLT_RELEASED_STRICT_PAGES])
		nr_released_strict_pages++;
	list_for_each(iter, &pool->azp_lists[AZPLT_RELEASED_PAGES])
		nr_released_pages++;
	list_for_each(iter, &pool->azp_lists[AZPLT_FLUSHED_PAGES])
		nr_flushed_pages++;
#endif /* AZ_PMEM_DEBUG_POOL */
	spin_unlock(&pool->azp_lock);
#ifdef AZ_PMEM_DEBUG_POOL
	seq_printf(m, "%10s %10s %7s %8s %9s %9s %10s %11s\n",
		   "XPROC_LIST", "USED_PAGES", "FLUSHED", "RELEASED",
		   "SRELEASED", "FLUSHED_S", "RELEASED_S", "SRELEASED_S");
	seq_printf(m, "%10s %10ld %7ld %8ld %9ld %9ld %10ld %11ld\n",
		   (on_list ? "ON" : "OFF"), nr_used,
		   nr_flushed_pages, nr_released_pages,
		   nr_released_strict_pages,
		   nr_flushed_subpages, nr_released_subpages,
		   nr_released_strict_subpages);
#endif /* AZ_PMEM_DEBUG_POOL */
out:
	put_task_struct(tsk);
	return 0;
}

static int az_pmem_open(struct inode *inode, struct file *file)
{
	struct pid *pid = PROC_I(inode)->pid;
	return single_open(file, az_pmem_show_proc_azpmem, pid);
}

/* XXX Do not change the name, it needs to be like proc_*_operations */
const struct file_operations proc_az_pmem_operations = {
	.open		= az_pmem_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#ifdef CONFIG_SYSCTL
int az_pmem_nr_pages_sysctl(struct ctl_table *table, int write,
		struct file *file, void __user *buffer,
		size_t *length, loff_t *ppos)
{
	unsigned long tmp;

	if (!write)
		tmp = atomic_long_read(&az_nr_pages_requested);

	table->data = &tmp;
	table->maxlen = sizeof(unsigned long);
	proc_doulongvec_minmax(table, write, file, buffer, length, ppos);

	if (write)
		az_reserve_pages(tmp);

	return 0;
}
#endif /* CONFIG_SYSCTL */
#endif

static int azmm_pfs_pmem_show(struct seq_file *m, void *v)
{
	unsigned long nr_zfree, nr_nzfree, nr_pools;
	int i;
	int64_t fnd_balance;
	unsigned long fnd_max;
	struct az_pmem_pool *pool;
	unsigned long nr_prefs = 0;

	spin_lock(&az_pmem_global_lock);
	nr_nzfree = az_nr_nzfree_pages;
	nr_zfree = az_nr_zfree_pages;
	nr_pools = az_nr_xprocess_pools;
	if (nr_pools) {
		list_for_each_entry(pool, &az_xprocess_pools, azp_list) {
			spin_lock(&pool->azp_lock);
			nr_prefs += atomic_read(&pool->azp_ref_count);
			spin_unlock(&pool->azp_lock);
		}
	}
	spin_unlock(&az_pmem_global_lock);
	seq_printf(m,
			"AZMM_PAGE_SIZE:         %lu kB\n"
			"AZMM_REQUESTED_PAGES:   %lu\n"
			"AZMM_ALLOCATED_PAGES:   %lu\n"
			"AZMM_NONZEROED_PAGES:   %lu\n"
			"AZMM_ZEROED_PAGES:      %lu\n",
			(1UL << (AZ_PAGE_SHIFT - 10)),
			atomic_long_read(&az_nr_pages_requested),
			atomic_long_read(&az_nr_pages_allocated),
			nr_nzfree, nr_zfree);
	if (nr_pools)
		seq_printf(m, "AZMM_XPROCESS_POOLS:    %lu/%lu\n", nr_pools,
				nr_prefs);
	seq_printf(m, "AZMM_FUNDS (Bal/Max): ");
	for (i = 0; i < AZMFT_NR_FUND_TYPES; i++) {
		spin_lock(&az_pmem_funds[i].azmf_lock);
		fnd_balance = az_pmem_funds[i].azmf_balance;
		fnd_max = az_pmem_funds[i].azmf_maximum;
		spin_unlock(&az_pmem_funds[i].azmf_lock);
		if (fnd_max)
			seq_printf(m, "[%d]%lld/%lu kB  ", i,
					(fnd_balance >> 10), (fnd_max >> 10));
	}
	seq_printf(m, "\n");
	return 0;
}

static int azmm_pfs_pmem_open(struct inode *inode, struct file *file)
{
    return single_open(file, azmm_pfs_pmem_show, NULL);
}

static const struct file_operations azmm_pfs_proc_fops = {
    .open    = azmm_pfs_pmem_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = seq_release,
};

static void __exit az_exit_pmem_procfs(void)
{
	remove_proc_entry("pmem", azmm_pfs_root);
	remove_proc_entry((THIS_MODULE)->name, NULL);
}

static int __init az_init_pmem_procfs(void)
{
	azmm_pfs_root = proc_mkdir((THIS_MODULE)->name, NULL);
	if (!azmm_pfs_root)
		return -ENOMEM;
	//azmm_pfs_root->owner = THIS_MODULE;
    azmm_pfs_pmem = proc_create_data("pmem", 0, azmm_pfs_root,
            &azmm_pfs_proc_fops, NULL);
	if (!azmm_pfs_pmem) {
		remove_proc_entry((THIS_MODULE)->name, NULL);
		return -ENOMEM;
	}
	//azmm_pfs_pmem->owner = THIS_MODULE;
	return 0;
}

/************************************************************************/
/* PMEM Module interface functions 					*/
/************************************************************************/

int az_pmem_put_page_if_owned(struct page *page)
{
	if (az_pmem_managed_page(page)) {
		az_pmem_put_page(page, AZ_PMEM_TLB_INVALIDATED);
		return 1;
	}
	return 0;
}

int az_pmem_get_page_if_owned(struct page *page)
{
	if (az_pmem_managed_page(page)) {
		az_pmem_get_page(page);
		return 1;
	}
	return 0;
}

int __meminit az_pmem_sparse_mem_map_populate(unsigned long pnum, int nid)
{
	if (az_sparse_mem_map_populate(pnum, nid))
		return -ENOMEM;
	return 0;
}

int __meminit az_pmem_init(void)
{
	int ret;

	printk(KERN_DEBUG "AZMM Init\n");
	atomic_long_set(&az_nr_pages_requested, 0);
	atomic_long_set(&az_nr_pages_allocated, 0);
	az_nr_nzfree_pages = 0;
	az_nr_zfree_pages = 0;
	az_nr_xprocess_pools = 0;
	az_pmem_init_funds();

	ret = az_init_mem_sections();
	if (ret)
		goto err_memsections;

	ret = az_init_vmemmap();
	if (ret)
		goto err_vmemmap;

	ret = az_init_pmem_procfs();
	if (!ret)
		return 0;

err_vmemmap:
	az_exit_vmemmap();
err_memsections:
	az_exit_mem_sections();
	printk(KERN_WARNING "AZMM: Failed to initialize, cleaning up: %d\n",
			ret);
	return ret;
}

void az_pmem_exit(void)
{
	if (az_unreserve_pages())
		return;
	az_exit_pmem_procfs();
	az_exit_vmemmap();
	az_exit_mem_sections();
	printk(KERN_DEBUG "AZMM Exit\n");
}
/************************************************************************/
