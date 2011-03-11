#define _POSIX_C_SOURCE 200112L

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#include <aznix/az_memory_ioctl.h>
#include <aznix/az_memory.h>

#define AZULDEV "/dev/azul"

#ifdef AZ_PROXIED
extern int __open(const char *pathname, int flags);
extern int __ioctl(int d, int request, ...);
#define MY_OPEN __open
#define MY_IOCTL __ioctl
#else
#define MY_OPEN open
#define MY_IOCTL ioctl
#endif

static int __azulfd = -1;
static int __azul_errno = 0;
static pthread_once_t __azul_init_ctrl = PTHREAD_ONCE_INIT;

static void __azul_openup(void)
{
	if ((__azulfd = MY_OPEN(AZULDEV, O_RDWR)) < 0)
		__azul_errno = errno;
}

static inline int __azulinit(void)
{
	(void)pthread_once(&__azul_init_ctrl, __azul_openup);
	return __azul_errno;
}

int az_mreserve(void* addr, size_t len, int flags)
{
	struct az_mreserve_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MRESERVE, &a);
}

int az_mreserve_alias(void* addr, void* existing_addr)
{
	struct az_mreserve_alias_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.existing_addr = (unsigned long)existing_addr;

	return MY_IOCTL(__azulfd, IOC_AZ_MRESERVE_ALIAS, &a);
}

int az_munreserve(void* addr, size_t len)
{
	struct az_munreserve_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;

	return MY_IOCTL(__azulfd, IOC_AZ_MUNRESERVE, &a);
}

int az_mmap(void* addr, size_t len, int prot, int acct, int flags)
{
	struct az_mmap_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;
	a.prot = prot;
	a.acct = acct;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MMAP, &a);
}

int az_munmap(void* addr, size_t len, int acct, int flags)
{
	struct az_munmap_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;
	a.acct = acct;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MUNMAP, &a);
}

int az_mremap(void* old_addr, void* new_addr, size_t len, int flags)
{
	struct az_mremap_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.old_addr = (unsigned long)old_addr;
	a.new_addr = (unsigned long)new_addr;
	a.len = len;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MREMAP, &a);
}

int az_mprotect(void* addr, size_t len, int prot, int flags)
{
	struct az_mprotect_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;
	a.prot = prot;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MPROTECT, &a);
}

int az_mcopy(void* dst_addr, void* src_addr, size_t len)
{
	struct az_mcopy_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.dst_addr = (unsigned long)dst_addr;
	a.src_addr = (unsigned long)src_addr;
	a.len = len;

	return MY_IOCTL(__azulfd, IOC_AZ_MCOPY, &a);
}

int az_mbatch_remap(void* old_addr, void* new_addr, size_t len, int flags)
{
	struct az_mbatch_remap_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.old_addr = (unsigned long)old_addr;
	a.new_addr = (unsigned long)new_addr;
	a.len = len;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MBATCH_REMAP, &a);
}

int az_mbatch_protect(void* addr, size_t len, int prot, int flags)
{
	struct az_mbatch_protect_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.len = len;
	a.prot = prot;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MBATCH_PROTECT, &a);
}

int az_munshatter(void* addr, void* force_addr, void* resource_addr,
	int prot, int acct, int flags)
{
	struct az_munshatter_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.addr = (unsigned long)addr;
	a.force_addr = (unsigned long)force_addr;
	a.resource_addr = (unsigned long)resource_addr;
	a.prot  = prot;
	a.acct  = acct;
	a.flags = flags;

	return MY_IOCTL(__azulfd, IOC_AZ_MUNSHATTER, &a);
}

long az_mprobe(pid_t pid, void* addr, int flags, int *refcount)
{
	struct az_mprobe_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.addr = (unsigned long)addr;
	a.flags = flags;
	a.refcntp = refcount;

	return MY_IOCTL(__azulfd, IOC_AZ_MPROBE, &a);
}

/*
 * Hack for compilaiton must be fixed.  ioctl only returns a 32-bit value, yet
 * the return value from this function is 64 bits.
 */
int64_t az_mflush(int acct, int flags, size_t * allocatedp)
{
	struct az_mflush_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return (int64_t)-1;
	}

	a.acct = acct;
	a.flags = flags;
	a.allocatedp = allocatedp;
	return (int64_t)MY_IOCTL(__azulfd, IOC_AZ_MFLUSH, &a);
}

int az_mbatch_commit(void)
{
	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	return MY_IOCTL(__azulfd, IOC_AZ_MBATCH_COMMIT, NULL);
}

int az_mbatch_abort(void)
{
	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	return MY_IOCTL(__azulfd, IOC_AZ_MBATCH_ABORT, NULL);
}

int az_tlb_invalidate(void)
{
	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	return MY_IOCTL(__azulfd, IOC_AZ_TLB_INVALIDATE, NULL);
}

int az_mbatch_start(void)
{
	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	return MY_IOCTL(__azulfd, IOC_AZ_MBATCH_START, NULL);
}


/* Physical memory calls */
int az_pmem_set_maximum(pid_t pid, size_t size)
{
	struct az_pmem_set_maximum_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_SET_MAXIMUM, &a);
}

int az_pmem_set_account_maximum(pid_t pid,
        unsigned long acct, size_t size)
{
	struct az_pmem_set_account_maximum_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.acct = acct;
	a.size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_SET_ACCOUNT_MAXIMUM, &a);
}

int az_pmem_fund_account(pid_t pid,
        unsigned long acct, size_t size)
{
	struct az_pmem_fund_account_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.acct = acct;
	a.size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_FUND_ACCOUNT, &a);
}

int az_pmem_account_transfer(pid_t pid,
        unsigned long dst, unsigned long src, size_t size)
{
	struct az_pmem_account_transfer_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.dst = dst;
	a.src = src;
	a.size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_ACCOUNT_TRANSFER, &a);
}

int az_pmem_get_account_stats(pid_t pid,
        void __user * statsp, size_t __user * sizep)
{
	struct az_pmem_get_account_stats_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.statsp = statsp;
	a.sizep = sizep;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_GET_ACCOUNT_STATS, &a);
}

int az_pmem_get_fund_stats(void __user * statsp,
        size_t __user * sizep)
{
	struct az_pmem_get_fund_stats_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.statsp = statsp;
	a.sizep = sizep;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_GET_FUND_STATS, &a);
}

int az_pmem_set_account_funds(pid_t pid,
        unsigned long acct, unsigned long commit, unsigned long overdraft)
{
	struct az_pmem_set_account_funds_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.acct = acct;
	a.commit = commit;
	a.overdraft = overdraft;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_SET_ACCOUNT_FUNDS, &a);
}

int az_pmem_set_accounts_to_credit(pid_t pid,
        unsigned long acct, unsigned long accts_to_credit)
{
	struct az_pmem_set_accounts_to_credit_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.pid = pid;
	a.acct = acct;
	a.accts_to_credit = accts_to_credit;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_SET_ACCOUNTS_TO_CREDIT, &a);
}

int az_pmem_fund_transfer(unsigned long dst,
        unsigned long src, size_t size)
{
	struct az_pmem_fund_transfer_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.dst = dst;
	a.src = src;
	a.size = size;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_FUND_TRANSFER, &a);
}

int az_pmem_reserve_pages(unsigned long nr_pages)
{
	struct az_pmem_reserve_pages_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.nr_pages = nr_pages;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_RESERVE_PAGES, &a);
}

int az_pmem_unreserve_pages(unsigned long nr_pages)
{
	struct az_pmem_unreserve_pages_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.nr_pages = nr_pages;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_UNRESERVE_PAGES, &a);
}

int az_pmem_reset_account_watermarks(unsigned long acct)
{
	struct az_pmem_reset_account_watermarks_args a;

	if (__azulinit()) {
		errno = __azul_errno;
		return -1;
	}

	a.acct = acct;

	return MY_IOCTL(__azulfd, IOC_AZ_PMEM_RESET_ACCOUNT_WATERMARKS, &a);
}

/*
 * Is the subsystem available (perhaps not initialized yet, but available)?  We
 * take this to mean that the Azul memory management module is loaded, which,
 * in the KO world, means taht /dev/azul is accessible.
 */
int az_pmem_subsys_available(void)
{
	int ret;

	ret = access(AZULDEV, R_OK | W_OK);
	return ret;
}

/*
 * Is the subsystem initialized?  Meaning, do one or more funds have one or
 * more bytes associated with them?
 */
int az_pmem_subsys_initialized(void)
{
    int ret;
    funds_info_t funds_info;
    size_t len;
    int i;

    len = sizeof(funds_info_t);
    ret = az_pmem_get_fund_stats(&funds_info, &len);

    if (ret) {
        return ret;
    }

    for (i = 0; i < funds_info.fi_count; i++) {
        if (funds_info.fi_array[i].fnd_maximum) {
            return 0;
        }
    }

    return -1;
}


