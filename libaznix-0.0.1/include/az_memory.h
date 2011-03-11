#ifndef _OS_AZ_MEMORY_H_
#define _OS_AZ_MEMORY_H_ 1

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <aznix/az_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * =============================
 * New aznix Memory system calls
 * =============================
 */

#define AZMM_NR_MEMORY_ACCOUNTS   8
#define AZMM_NR_MEMORY_FUNDS      8

typedef struct {
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
} account_stats_t;

typedef struct {
    uint64_t        ac_count;
    uint64_t        proc_mrlimit;
    account_stats_t ac_array[AZMM_NR_MEMORY_ACCOUNTS];
} account_info_t;

typedef struct {
    size_t fnd_balance;
    size_t fnd_maximum;
} fund_stats_t;

typedef struct {
    uint64_t     fi_count;
    fund_stats_t fi_array[AZMM_NR_MEMORY_FUNDS];
} funds_info_t;

#define AZMM_SMALL_PAGE_SHIFT  12
#define AZMM_LARGE_PAGE_SHIFT  21

#define AZMM_SMALL_PAGE_SIZE   (1UL<<AZMM_SMALL_PAGE_SHIFT) //4 KB
#define AZMM_LARGE_PAGE_SIZE   (1UL<<AZMM_LARGE_PAGE_SHIFT)     //2 MB

#define AZMM_SMALL_PAGE_OFFSET (AZMM_SMALL_PAGE_SIZE - 1)
#define AZMM_SMALL_PAGE_MASK   (~AZMM_SMALL_PAGE_OFFSET)

#define AZMM_LARGE_PAGE_OFFSET (AZMM_LARGE_PAGE_SIZE - 1)
#define AZMM_LARGE_PAGE_MASK   (~AZMM_LARGE_PAGE_OFFSET)

#define AZMM_NO_TLB_INVALIDATE           0x0100000
#define AZMM_BLIND_UNMAP                 0x0200000
#define AZMM_BLIND_PROTECT               0x0200000
#define AZMM_LARGE_PAGE_MAPPINGS         0x0400000
#define AZMM_MAY_SHATTER_LARGE_MAPPINGS  0x0800000
#define AZMM_BATCHABLE                   0x1000000
#define AZMM_ALIASABLE                   0x2000000
#define AZMM_RETAIN_SRC_MAPPINGS	 0x04000000
#define AZMM_MPROBE_SHADOW               0x00000010
#define AZMM_MPROBE_REF_COUNT		 0x08000000

#define AZMM_ALLOCATE_HEAP              0x0200
#define AZMM_ALLOCATE_NO_OVERDRAFT      0x1000
#define AZMM_ALLOCATE_REQUEST_NOZERO    0x2000
#define AZMM_MAY_RECYCLE_BEFORE_INVALIDATE 0x00000004 /* m[un]map, munshatter */

#define AZMM_FLUSH_HEAP                 0x0001
#define AZMM_FLUSH_OVERDRAFT_ONLY       0x0002

#define AZMM_DEFAULT_ACCOUNT            0   /* C Heap, stacks, etc */
#define AZMM_JHEAP_ACCOUNT              2
#define AZMM_GCPP_ACCOUNT               3   /* GC pause prevention */

#define AZMM_COMMITTED_FUND             0
#define AZMM_OVERDRAFT_FUND             1
#define AZMM_GCPP_FUND                  3   /* GC pause prevention */

static inline const char* azmm_get_fund_name(int id)
{
    switch(id) {
        case AZMM_COMMITTED_FUND:
            return "Committed";
            break;
        case AZMM_OVERDRAFT_FUND:
            return "Overdraft";
            break;
        case AZMM_GCPP_FUND:
            return "PausePrevention";
            break;
        default:
            return "Unknown";
            break;
    }
}

static inline const char* azmm_get_account_name(int id)
{
    switch(id) {
        case AZMM_DEFAULT_ACCOUNT:
            return "Default";
            break;
        case AZMM_JHEAP_ACCOUNT:
            return "JavaHeap";
            break;
        case AZMM_GCPP_ACCOUNT:
            return "PausePrevention";
            break;
        default:
            return "Unknown";
            break;
    }
}

extern int az_mreserve(void *addr, size_t len, int flags);
extern int az_mreserve_alias(void *addr, void *existing_addr);
extern int az_munreserve(void *addr, size_t len);
extern int az_mmap(void *addr, size_t len, int prot, int acct, int flags);
extern int az_munmap(void *addr, size_t len, int acct, int flags);
extern int az_mremap(void *old_addr, void *new_addr, size_t len, int flags);
extern int az_mprotect(void *addr, size_t len, int prot, int flags);
extern int az_mcopy(void *dst_addr, void *src_addr, size_t len);
extern int az_mbatch_start(void);
extern int az_mbatch_remap(void *old_addr, void *new_addr,
        size_t len, int flags);
extern int az_mbatch_protect(void *addr, size_t len,
        int prot, int flags);
extern int az_mbatch_commit(void);
extern int az_mbatch_abort(void);
extern int az_tlb_invalidate(void);
extern int az_munshatter(void *addr, void *force_addr, void *resource_addr,
            int prot, int acct, int flags);
extern int64_t az_mflush(int acct, int flags, size_t *allocated);


/* Aznix Physical Memory Accounting system calls */
#if (AZNIX_API_VERSION >= 200)
/* .ko ioctl interface */
extern int az_pmem_set_maximum(pid_t pid, size_t size);
extern int az_pmem_set_account_maximum(pid_t pid,
        unsigned long acct, size_t size);
extern int az_pmem_fund_account(pid_t pid,
        unsigned long acct, size_t size);
extern int az_pmem_account_transfer(pid_t pid,
        unsigned long dst, unsigned long src, size_t size);
extern int az_pmem_get_account_stats(pid_t pid,
        void * statsp, size_t * sizep);
extern int az_pmem_get_fund_stats(void * statsp,
        size_t * sizep);
extern int az_pmem_set_account_funds(pid_t pid,
        unsigned long acct, unsigned long commit, unsigned long overdraft);
extern int az_pmem_set_accounts_to_credit(pid_t pid,
        unsigned long acct, unsigned long accts_to_credit);
extern int az_pmem_fund_transfer(unsigned long dst,
        unsigned long src, size_t size);
extern int az_pmem_reserve_pages(unsigned long nr_pages);
extern int az_pmem_unreserve_pages(unsigned long nr_pages);
extern int az_pmem_reset_account_watermarks(unsigned long acct);
extern int az_pmem_subsys_available(void);
extern int az_pmem_subsys_initialized(void);
extern long az_mprobe(pid_t pid, void *addr, int flags, int *refcount);
#elif (AZNIX_API_VERSION >= 100)
/* syscalls interface */
extern int az_pmem_set_maximum(az_allocid_t allocid, size_t size);
extern int az_pmem_set_account_maximum(az_allocid_t allocid,
        unsigned long acct, size_t size);
extern int az_pmem_fund_account(az_allocid_t allocid,
        unsigned long acct, size_t size);
extern int az_pmem_account_transfer(az_allocid_t allocid,
        unsigned long dst, unsigned long src, size_t size);
extern int az_pmem_get_account_stats(az_allocid_t allocid,
        void *statsp, size_t *sizep);
extern int az_pmem_get_fund_stats(void *statsp, size_t *sizep);
extern int az_pmem_set_account_funds(az_allocid_t label,
        unsigned long acct, unsigned long commit, unsigned long overdraft);
extern int az_pmem_set_accounts_to_credit(az_allocid_t label,
        unsigned long acct, az_accts_list_t accts_to_credit);
extern int az_pmem_fund_transfer(unsigned long dst, unsigned long src,
        size_t size);
extern int az_pmem_reset_account_watermarks(unsigned long acct);
static inline int az_pmem_subsys_available(void) { return 0; }
static inline int az_pmem_subsys_initialized(void) { return 0; }
extern long az_mprobe(pid_t pid, void *addr, int flags, int *refcount);
#else
#error AZNIX_API_VERSION must be defined
#endif

/* Page table entry probe functions */
#include <sys/mman.h>
#define _PAGE_BIT_PRESENT   0   /* is present */
#define _PAGE_BIT_RW        1   /* writeable */
#define _PAGE_BIT_USER      2   /* userspace addressable */
#define _PAGE_BIT_PSE       7   /* 2MB page */
#define _PAGE_BIT_NX        63  /* No execute: only valid after cpuid check */

#define _PAGE_PRESENT   (1ULL << _PAGE_BIT_PRESENT)
#define _PAGE_RW        (1ULL << _PAGE_BIT_RW)
#define _PAGE_USER      (1ULL << _PAGE_BIT_USER)
#define _PAGE_PSE       (1ULL << _PAGE_BIT_PSE)
#define _PAGE_NX        (1ULL << _PAGE_BIT_NX)

#define PROT_MAPPED     0x10000
#define PROT_LARGE      0x20000
#define PROT_P2P        0x40000
#define PROT_INVALID    0x80000
#define pmd_prot(a)     (az_pte_prot(a) | PROT_LARGE)

static inline int az_pte_prot(unsigned long pte)
{
    int prot = 0;
    if (pte & _PAGE_PRESENT)
        prot |= PROT_MAPPED;
    if (pte && !(pte & _PAGE_PRESENT))
        prot |= PROT_P2P;
    if (pte & _PAGE_USER)
        prot |= PROT_READ;
    if (pte & (_PAGE_USER | _PAGE_RW))
        prot |= PROT_WRITE;
    if ((pte & _PAGE_USER) && !(pte & _PAGE_NX))
        prot |= PROT_EXEC;
    return prot;
}

static inline int pmd_large_virtual(unsigned long pmd)
{
    return (pmd & _PAGE_PSE);
}

static inline int az_page_prot(pid_t pid, void *addr, int flags)
{
    unsigned long map;

    map = az_mprobe(pid, addr, flags, NULL);
    if ((long)map == (long)-1  && errno == EINVAL)
        return PROT_INVALID;
    if ((long)map == (long)-1)
        return 0;
    if (pmd_large_virtual(map))
        return pmd_prot(map);
    return az_pte_prot(map);
}

#define az_large_page(addr, flags) \
    (((az_page_prot(getpid(), (addr), (flags))) & PROT_LARGE))

#define az_page_writable(addr, flags) \
    (((az_page_prot(getpid(), (addr), (flags))) & PROT_WRITE))

#define az_page_present(addr, flags) \
    (((az_page_prot(getpid(), (addr), (flags))) & PROT_MAPPED))

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
  
#endif // _OS_AZ_MEMORY_H_
