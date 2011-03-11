#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <assert.h>

#include <aznix/az_memory.h>
#include <aznix/az_allocid.h>

#define AZMM_ACCT_SHIFT         8
#define AZMM_ACCT_MASK          ((1 << AZMM_ACCT_SHIFT) - 1)
#define AZMM_INIT_ACCTS_WORD    ~0UL
#define AZMM_PUSH_ACCT_ON_LIST(list, a) \
    (((list) << AZMM_ACCT_SHIFT) | ((a) & AZMM_ACCT_MASK))

//#define WITH_AC_LIST_VERIFY 1
#ifdef WITH_AC_LIST_VERIFY
#define az_pmem_for_each_acct(i, a, accts) \
    for ((i) = 0, (a) = (accts) & AZMM_ACCT_MASK; \
            ((a) != AZMM_ACCT_MASK) && (i) < AZMM_NR_MEMORY_ACCOUNTS; \
            (i)++, (a) = ((accts) >> ((i) * AZMM_ACCT_SHIFT)) & AZMM_ACCT_MASK)
static int az_pmem_acct_list_sane(unsigned long accts_to_credit)
{
    unsigned long a;
    int i;
    az_pmem_for_each_acct(i, a, accts_to_credit)
        ;
    return (i != 0);
}
#endif

void show_cmd_list()
{
    printf("USAGE: Enter the cmd option and the args\n");
    printf("Example args for cmd#1: 1 0x200000000 0x400000000\n");
    printf("Command list: \n"
            "[1] AZ_MRESERVE: 1 <addr> < len> <batchable[0,1]>"
	    " aliasable[0,1]>\n"
            "[2] AZ_MUNRESERVE: 2 <addr> <len>\n"
            "[3] AZ_MMAP: 3 <addr> <len> <prot[0x0|0x1|0x2|0x3]>"
                " <acct[0,2,3]> <use_large_pages[0,1]> <no_zero[0,1]>"
                " <recycle[0,1]>\n"
            "[4] AZ_MUNMAP: 4 <addr> <len> <acct[0,2,3]> <tlb_invalidate[0,1]>"
                " <blind[0/1]> <recycle[0,1]>\n"
            "[5] AZ_TLB_INVALIDATE: 5\n"
            "[6] PROBE ADDRESS: 6 <addr> <len> <shadow[0,1]>\n"
            "[7] AZ_PROC_ADD_TO_GROUP: 13 <id> <gid>\n"
            "[8] AZ_PMEM_SET_MAX: 8 <size K>\n"
            "[9] AZ_PMEM_SET_AC_MAX: 9 <acct[0,2,3]> <size K>\n"
            "[10] AZ_PMEM_FUND_AC: 10 <acct[0,2,3]> <size K>\n"
            "[11] AZ_PMEM_AC_TRANSFER: 11 <dst[0,2]> <src[0,2] <size K>\n"
            "[12] AZ_PMEM_GET_AC_STATS: 12 \n"
            "[13] AZ_PMEM_GET_FUND_STATS: 13 \n"
            "[14] AZ_MFLUSH: 14 <acct[0,2,3]> <od_only[0,1]> <flush_heap[0,1]>"
                " <tlb_invalidate[0,1]>\n"
            "[15] AZ_PMEM_SET_ACCT_FUNDS: 15 <acct[0,2,3]> <commit[0,1,3]>"
                " <overdraft[0,1,3]>\n"
            "[16] AZ_PMEM_SET_ACCTS_TO_CREDIT: 16 <acct[0,2,3]>"
                " <credit[0,2,3]...>\n"
            "[17] AZ_PMEM_FUND_TRANSFER: 17 <dst[0,1,3]> <src[0,1,3] <size K>\n"
            "[18] AZ_MBATCH_START: 18 \n"
            "[19] AZ_MBATCH_REMAP: 20 <src_addr> <dst_addr> <len>"
            " <tlb_invalidate[0,1]\n"
            "[20] AZ_MBATCH_PROTECT: 19 <addr> <len> <prot[0x0|0x1|0x2|0x3]>"
            " <tlb_invalidate[0,1] <blind[0,1]>\n"
            "[21] AZ_MBATCH_COMMIT: 21 \n"
            "[22] AZ_MBATCH_ABORT: 22 \n"
            "[23] AZ_MUNSHATTER: 23 <addr> <force_addr> <res_addr>"
            " prot[0x0|0x1|0x2|0x3]> <acct> <tlb_invalidate>\n"
            "[24] AZ_MRESERVE_ALIAS: 24 <addr> <existing_addr>\n"
            "[25] AZ_PMEM_RESERVE: 25 <nr_2m_pages>\n"
            "[26] AZ_PMEM_UNRESERVE: 26 <nr_2m_pages>\n"
            "\n"
            "[100] complete test sequence 100\n"
          );
}

#define PAGE_SHIFT  12
#define PAGE_SIZE   (1ULL << PAGE_SHIFT)

#define PMD_SHIFT   21
#define PMD_SIZE    (1ULL << PMD_SHIFT)
#define PMD_MASK    (~(PMD_SIZE-1))

int report_prot(char label[], unsigned long start, unsigned long end, int prot)
{
    char range_string[80];
    if (start == end)
        sprintf(range_string, "[%016lx]%s", start,
         (prot & PROT_MAPPED) ? ((prot & PROT_LARGE) ? "[L]" : "[S]") :
         ((prot & PROT_P2P) ? "[P]" : "[X]"));
    else
        sprintf(range_string, "[%016lx-%016lx]%s", start, end,
         (prot & PROT_MAPPED) ? ((prot & PROT_LARGE) ? "[L]" : "[S]") :
         ((prot & PROT_P2P) ? "[P]" : "[X]"));

    if ((prot & PROT_MAPPED) || (prot & PROT_P2P)) {
        printf("%s%s: %c%c%c\n", label, range_string,
                prot & PROT_READ ? 'r':'-',
                prot & PROT_WRITE ? 'w':'-',
                prot & PROT_EXEC ? 'x':'-');
    } else if (prot & PROT_INVALID) {
        printf("%s%s: NOT VALID\n", label, range_string);
    } else {
        printf("%s%s: NOT MAPPED\n", label, range_string);
    }
    return prot;
}

void report_probe_ranges(pid_t pid, char label[], unsigned long start,
                        unsigned long length, int flags)
{
    unsigned long end = start + length;
    unsigned long range_start = start;
    int range_prot;

    range_prot = az_page_prot(pid, (void *) start, flags);
    do {
        int probe_prot;
        do {
            /* Skip to the next large page if we can */
            start += ((range_prot & PROT_LARGE) && !(start & ~PMD_MASK)) ?
                PMD_SIZE : PAGE_SIZE;
            probe_prot = az_page_prot(pid, (void *) start, flags);
        } while ((probe_prot == range_prot) && (start < end));
        report_prot(label, range_start, start, range_prot);
        range_prot = probe_prot;
        range_start = start;
    } while (start < end);
}

void test100();

int
main(int argc, char *argv[])
{
    long rc;
    int cmd;
    unsigned long addr, len, acct1, acct2, acct3, acct4, flags, prot;
    int tlb_invalidate, use_large_pages, blind, od_only, flush_heap, shadow;
    int recycle, no_zero;
    int batchable, aliasable;
    char buf[1024];
    char *tmp;
    account_info_t  ac_stats;
    funds_info_t fnd_stats;
    unsigned long fund1, fund2;
    unsigned long accts_to_credit;
    unsigned long addr1;
    pid_t pid;

    if (argc > 1) 
        pid = strtoul(argv[1], NULL, 0);
    else
        pid = getpid();

    show_cmd_list();
    while (1) {
        cmd = addr = len = acct1 = acct2 = acct3 = acct4 = flags = addr1 = 0;
        fund1 = fund2 = AZMM_NR_MEMORY_FUNDS;
        tlb_invalidate = use_large_pages = blind = batchable = aliasable = 0;
        recycle = no_zero = shadow = 0;
        prot = PROT_NONE;

        memset(buf, 0, sizeof(buf));
        if (!fgets(buf, sizeof(buf), stdin))
            continue;
        tmp = strchr(buf, ' ');
        if (tmp) {
            *tmp = '\0';
            tmp++;
        }
        cmd = atoi(buf);
        switch (cmd) {
            case 1:
                sscanf(tmp, "%lx %lx %d %d", &addr, &len, &batchable,
				&aliasable);
                printf("Reserving %s %s area from 0x%016lx to 0x%016lx\n",
                        batchable ? "batchable" : "",
			 aliasable ? "aliasable" : "",
			  addr, addr + len);
                if (batchable)
                    flags = AZMM_BATCHABLE;
                if (aliasable)
                    flags |= AZMM_ALIASABLE;
                rc = az_mreserve( (void*)addr, len, flags);
                if (rc)
                    perror("az_mreserve failed");
                break;
            case 2:
                sscanf(tmp, "%lx %lx", &addr, &len);
                printf("Unreserving area from 0x%016lx to 0x%016lx:\n",
                        addr, addr + len);
                rc = az_munreserve((void*)addr, len);
                if (rc)
                    perror("az_munreserve failed");
                break;
            case 3:
                sscanf(tmp, "%lx %lx %lx %lu %d %d %d", 
                        &addr, &len, &prot, &acct1, &use_large_pages,
                        &no_zero, &recycle);
                if (use_large_pages)
                    flags |= AZMM_LARGE_PAGE_MAPPINGS;
                if (recycle)
                    flags |= AZMM_MAY_RECYCLE_BEFORE_INVALIDATE;
                if (no_zero)
                    flags |= AZMM_ALLOCATE_REQUEST_NOZERO;
                printf("[%lu]Mapping area from 0x%016lx to 0x%016lx: 0x%lx\n",
                        acct1, addr, addr + len, prot);
                rc = az_mmap((void*)addr, len, prot, acct1, flags);
                if (rc)
                    perror("az_mmap failed");
                break;
            case 4:
                sscanf(tmp, "%lx %lx %lu %d %d %d", &addr, &len, &acct1,
                        &tlb_invalidate, &blind, &recycle);
                if (!tlb_invalidate)
                    flags |= AZMM_NO_TLB_INVALIDATE;
                if (blind)
                    flags |= AZMM_BLIND_UNMAP;
                if (recycle)
                    flags |= AZMM_MAY_RECYCLE_BEFORE_INVALIDATE;
                printf("[%lu]Unmapping area from 0x%016lx to 0x%016lx:\n",
                        acct1, addr, addr + len);
                rc = az_munmap((void*)addr, len, acct1, flags);
                if (rc)
                    perror("az_munmap failed");
                break;
            case 5:
                rc = az_tlb_invalidate();
                if (rc)
                    perror("az_tlb_invalidate failed");
                break;
            case 6:
                sscanf(tmp, "%lx %lx %d", &addr, &len, &shadow);
                printf("Probing %s table from 0x%016lx to 0x%016lx:\n",
                        (shadow) ? "Shadow" : "Main", addr, addr + len);
                flags = (shadow) ? AZMM_MPROBE_SHADOW : 0;
                report_probe_ranges(pid, (shadow) ? "S" : "M",
                        addr, len, flags);
                break;
            case 7:
                rc = -1;
		errno = ENOSYS;
                if (rc)
                    perror("az_process_add failed");
                break;
            case 8:
                sscanf(tmp, "%lu", &len);
                printf("Setting process level max for pid %d as %luK:\n",
                        pid, len);
                rc = az_pmem_set_maximum( pid, len << 10);
                if (rc)
                    perror("az_pmem_set_maximum failed");
                break;
            case 9:
                sscanf(tmp, "%lu %lu", &acct1, &len);
                printf("Setting acct level max for pid %d, ac %lu as %luK:\n",
                        pid, acct1, len);
                rc = az_pmem_set_account_maximum( pid, acct1, len << 10);
                if (rc)
                    perror("az_pmem_set_account_maximum failed");
                break;
            case 10:
                sscanf(tmp, "%lu %lu", &acct1, &len);
                printf("Funding acct for pid %d, ac %lu with %luK:\n",
                        pid, acct1, len);
                rc = az_pmem_fund_account( pid, acct1, len << 10);
                if (rc)
                    perror("az_pmem_fund_account failed");
                break;
            case 11:
                sscanf(tmp, "%lu %lu %lu", &acct1, &acct2, &len);
                printf("Acct transfer pid %d, from ac# %lu to ac# %lu: %luK:\n",
                        pid, acct2, acct1, len);
                rc = az_pmem_account_transfer( pid, acct1, 
                        acct2, len << 10);
                if (rc)
                    perror("az_pmem_account_transfer failed");
                break;
            case 12:
                printf("Account stats for pid %d:\n", pid);
                len = sizeof(ac_stats);
                memset(&ac_stats, 0, len);
                rc = az_pmem_get_account_stats( pid, &ac_stats, &len);
                if (rc)
                    perror("az_pmem_get_account_stats failed");
                else {
                    uint64_t i;
                    printf("Process level mem_rlimit: %lu\n",
                            ac_stats.proc_mrlimit);
		    printf ("AC# %32s  %32s  %14s  %18s  %3s  %5s\n",
				    "BALANCE(now/min/max KB)",
				    "ALLOCATED(now/min/max KB)",
				    "MAXIMUM(KB)", "ACS_TO_CREDIT",
				    "FND", "ODFND");
                    for (i = 0; i < ac_stats.ac_count; i++)
                        if (ac_stats.ac_array[i].ac_fund >= 0 || 
                                ac_stats.ac_array[i].ac_overdraft_fund >= 0)
				printf ("[%lu]: %10ld/%10ld/%10ld  "
				    "%10lu/%10lu/%10lu  "
				    "%14lu  0x%16lx  %3ld  %5ld\n",
				    i,
                                    ac_stats.ac_array[i].ac_balance >> 10,
                                    ac_stats.ac_array[i].ac_balance_min >> 10,
                                    ac_stats.ac_array[i].ac_balance_max >> 10,
                                    ac_stats.ac_array[i].ac_allocated >> 10,
                                    ac_stats.ac_array[i].ac_allocated_min >> 10,
                                    ac_stats.ac_array[i].ac_allocated_max >> 10,
                                    ac_stats.ac_array[i].ac_maximum >> 10,
                                    ac_stats.ac_array[i].ac_accts_to_credit,
                                    ac_stats.ac_array[i].ac_fund,
                                    ac_stats.ac_array[i].ac_overdraft_fund);
                }
                break;
            case 13:
                printf("System Fund stats :\n");
                len = sizeof(fnd_stats);
                memset(&fnd_stats, 0, len);
                rc = az_pmem_get_fund_stats( &fnd_stats, &len);
                if (rc)
                    perror("az_pmem_get_fund_stats failed");
                else {
                    uint64_t i;
                    for (i = 0; i < fnd_stats.fi_count; i++)
                        if (fnd_stats.fi_array[i].fnd_balance > 0 || 
                                fnd_stats.fi_array[i].fnd_maximum > 0)
                            printf("fund[%lu]: balance:%luK, max:%luK\n", i,
                                    fnd_stats.fi_array[i].fnd_balance >> 10,
                                    fnd_stats.fi_array[i].fnd_maximum >> 10);
                }
                break;
            case 14:
                sscanf(tmp, "%lu %d %d %d",
                        &acct1, &od_only, &flush_heap, &tlb_invalidate);
                if (flush_heap)
                    flags |= AZMM_FLUSH_HEAP;
                if (od_only)
                    flags |= AZMM_FLUSH_OVERDRAFT_ONLY;
                if (!tlb_invalidate)
                    flags |= AZMM_NO_TLB_INVALIDATE;
                printf("Flushing acct %lu with flags 0x%lx:\n",
                        acct1, flags);
                rc = az_mflush( acct1, flags, &len);
                if (rc < 0)
                    perror("az_pmem_flush_account failed");
                else
                    printf("Flushed acct %lu, allocated %luK, flushed %ldK\n",
                            acct1, len >> 10, rc >> 10);
                break;
            case 15:
                sscanf(tmp, "%lu %ld %ld",
                        &acct1, &fund1, &fund2);
                printf("Setting acct %lu with commit %ld and overdraft %ld\n",
                        acct1, fund1, fund2);
                rc = az_pmem_set_account_funds( pid, acct1,
                        fund1, fund2);
                if (rc < 0)
                    perror("az_pmem_set_acct_funds failed");
                break;
            case 16:
                sscanf(tmp, "%lu %lu %lu %lu",
                        &acct1, &acct2, &acct3, &acct4);
                accts_to_credit = ~0UL;
                accts_to_credit = AZMM_PUSH_ACCT_ON_LIST(accts_to_credit,
                        acct4);
                accts_to_credit = AZMM_PUSH_ACCT_ON_LIST(accts_to_credit,
                        acct3);
                accts_to_credit = AZMM_PUSH_ACCT_ON_LIST(accts_to_credit,
                        acct2);
#ifdef WITH_AC_LIST_VERIFY
                if (az_pmem_acct_list_sane(accts_to_credit)) {
                    unsigned long a;
                    int i = 0;
                    az_pmem_for_each_acct(i, a, accts_to_credit)
                        printf("Acct[%d]: %lu\n", i, a);
                }
#endif
                printf("Setting acct %lu with release accts 0x%lx:\n",
                        acct1, accts_to_credit);
                rc = az_pmem_set_accounts_to_credit( pid, acct1,
                        accts_to_credit);
                if (rc < 0)
                    perror("az_pmem_set_accts_to_credit failed");
                break;
            case 17:
                sscanf(tmp, "%lu %lu %lu", &fund1, &fund2, &len);
                printf("Fund transfer from fund# %lu to fund# %lu: %luK:\n",
                        fund2, fund1, len);
                rc = az_pmem_fund_transfer( fund1, fund2, len << 10);
                if (rc)
                    perror("az_pmem_fund_transfer failed");
                break;
            case 18:
                printf("Starting batch operations\n");
                rc = az_mbatch_start();
                if (rc)
                    perror("az_mbatch_start failed");
                break;
            case 19:
                sscanf(tmp, "%lx %lx %lx %d", &addr, &addr1, &len,
                        &tlb_invalidate);
                flags = AZMM_MAY_SHATTER_LARGE_MAPPINGS;
                if (!tlb_invalidate)
                    flags |= AZMM_NO_TLB_INVALIDATE;
                printf("Batch remapping 0x%016lx from 0x%016lx to 0x%016lx\n",
                        len, addr, addr1);
                rc = az_mbatch_remap((void*)addr, (void*)addr1, len, flags);
                if (rc)
                    perror("az_mbatch_remap failed");
                break;
            case 20:
                sscanf(tmp, "%lx %lx %lx %d %d", &addr, &len, &prot,
                        &tlb_invalidate, &blind);
                flags = AZMM_MAY_SHATTER_LARGE_MAPPINGS;
                if (!tlb_invalidate)
                    flags |= AZMM_NO_TLB_INVALIDATE;
                if (blind)
                    flags |= AZMM_BLIND_PROTECT;
                printf("Batch protecting 0x%016lx from 0x%016lx with 0x%lx\n",
                        len, addr, prot);
                rc = az_mbatch_protect( (void*)addr, len, prot, flags);
                if (rc)
                    perror("az_mbatch_protect failed");
                break;
            case 21:
                printf("Committing batch operations\n");
                rc = az_mbatch_commit();
                if (rc)
                    perror("az_mbatch_commit failed");
                break;
            case 22:
                printf("Aborting batch operations\n");
                rc = az_mbatch_abort();
                if (rc)
                    perror("az_mbatch_abort failed");
                break;
            case 23:
                sscanf(tmp, "%lx %lx %lx %lx %lu %d", &addr, &len, &addr1,
                        &prot, &acct1, &tlb_invalidate);
                flags = (tlb_invalidate) ? 0 : AZMM_NO_TLB_INVALIDATE;
                printf("%s Unshattering 0x%016lx at 0x%016lx:0x%lx\n",
                        (len) ? "Async" : "Sync", addr, (len) ? len : addr,
                        prot);
                rc = az_munshatter( (void*)addr, (void*)len, (void*)addr1,
				prot, acct1, flags);
                if (rc < 0)
                    perror("az_munshatter failed");
                if (!rc && !len && addr1)
                    printf("Source page from 0x%016lx is mapped at 0x%016lx\n",
                            addr, addr1);
                break;
            case 24:
                sscanf(tmp, "%lx %lx", &addr, &addr1);
                printf("Reserving area at 0x%016lx aliased from  0x%016lx\n",
			  addr, addr1);
                rc = az_mreserve_alias((void*)addr, (void*)addr1);
                if (rc)
                    perror("az_mreserve_alias failed");
                break;
            case 25:
                sscanf(tmp, "%lu", &len);
                printf("Reserving %lu 2M pages\n", len);
                rc = az_pmem_reserve_pages(len);
                if (rc)
                    perror("az_pmem_reserve_pages failed");
                break;
            case 26:
                sscanf(tmp, "%lu", &len);
                printf("Unreserving %lu 2M pages\n", len);
                rc = az_pmem_unreserve_pages(len);
                if (rc)
                    perror("az_pmem_unreserve_pages failed");
                break;
            case 100:
                test100();
                break;
            default:
                printf("cmd# %d is not valid, try again\n", cmd);
                break;
        }
    }
    exit(0);
}

void
test100()
{
    allocid_to_pid_t *pm = 0;
    uint64_t cnt;
    int rc;

    rc = az_allocid_purge();
    assert (!rc);

    rc = az_allocid_get_list (pm, &cnt);
    assert (!rc);
    assert (cnt == 0);

    rc = az_allocid_add (1000000 + getpid(), getpid());
    assert (!rc);

    printf ("PASSED\n");
}
