// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

// os.c - Azul OS (Aztek) system services. Leftover cruft.

#include <string.h>	// strlen() prototype
#include <stdio.h>	// snprintf() prototype
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <aznix/az_memory.h>
#include <aznix/az_pgroup.h>

#include <os/log.h>
#include <os/process.h>
#include <os/sched.h>
#include <os/thread.h>
#include <os/time.h>
#include <os/utilities.h>

static int _abort_on_out_of_memory = 0;

// External reference to force init function to be linked in
extern uint64_t __init_force_ref(void);

extern int native_stat(const char *path, struct stat *buf);

// By default we always use the azul virtual memory calls, unless the user turns it off.
// (with the -PX:-UseAzMem flag)
static int _os_use_azmem = 1;

// By default we always use the azul scheduler, unless the user turns it off.
// (with the -PX:-UseAzSched flag)
static int _os_use_azsched = 1;

static int _os_start_suspended = 0;  
static int64_t _os_memcommit = 0;  
static int64_t _os_memmax = 0;  

#ifdef AZ_PROXIED
#define MY_STAT native_stat
#else // !AZ_PROXIED
#define MY_STAT stat
#endif // !AZ_PROXIED

// Library initializer
void init_azsys(void)
{
#ifndef AZ_PROXIED // FIXME - /dev/az* should be checked for azproxied also when running on fc12
    struct stat st;

    _os_use_azmem = (MY_STAT("/dev/azul", &st) < 0) ? 0 : 1;
    _os_use_azsched = (MY_STAT("/dev/azul_sched", &st) < 0) ? 0 : 1;

    // Initialize time related constants
    _init_time();

#endif // !AZ_PROXIED
}

// Trap process (and thread) exit here.

void set_abort_on_out_of_memory(int b)
{
    _abort_on_out_of_memory = b;
}

void exit_out_of_memory(void)
{
    os_unimplemented();
}

void exit_abort(uint64_t abort_code)
{
    // Sayonara
    abort();
}

// Debugging/assertions/guarantees/should not reach here/sys call checks, etc.
void _os_abort(const char *_type,
               const char *_function,
	       const char *_file,
	       int         _line,
	       const char *_msg,
	       const char *_expression)
{
    // where are we and why are we dying?
    // TODO:  I'm pretty sure I shouldn't be using printf, but I don't know what should be used.
    printf("_os_abort %s: %s %s in %s at %s:%d\n"
            ,_type, _msg, _expression, _function, _file, _line);

    // provide a backtrace
    os_backtrace();

    // breakpoint.  If I understand this correctly, this should either enter the debugger
    // or terminate the problem.
    _os_breakpoint();

    // if we return from the breakpoint, then call libc::abort()
    abort();
}

void _os_syscallok(sys_return_t rc, const char *function, const char *file, int line, const char *msg)
{
    os_unimplemented();
}

void os_debug(void)
{
    os_unimplemented();
}

static void (*backtrace_callback) (void) = 0;

void set_backtrace_callback(void (*f) (void))
{
    backtrace_callback = f;
}

void os_backtrace(void)
{
    if (backtrace_callback) {
        (*backtrace_callback)();
        return;
    }

    /* 
     * Default implementation to walk C stack frames where RBP is
     * well-defined.
     * 
     * Note that for the java (i.e. hotspot) normal case, a
     * backtrace_callback MUST be set that knows how to unwind java
     * frames properly.
     *
     * This default implementation is only useful for toy test
     * programs.
     */

    uint64_t rip;
    uint64_t rip_addr;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbp_addr;
    int frameno;
    // TODO: if we start using labelbuf, we could increase its size.  When we do, thread stack handling
    // needs to give the error path more stack space when a thread hits the yellow zone.
    char labelbuf[8];

    /* Calculate first frame */
    {
        __asm__ __volatile__  (
            "lea 0(%%rip), %0\n\t"
            : "=r" (rip)
            );

        rip_addr = -1;

        __asm__ __volatile__  (
            "movq %%rsp, %0"
            : "=r" (rsp)
            );

        __asm__ __volatile__  (
            "movq %%rbp, %0"
            : "=r" (rbp)
            );

        rbp_addr = -1;
        frameno = 0;
    }

    while (1) {
        uint64_t caller_rip;
        uint64_t caller_rip_addr;
        uint64_t caller_rsp;
        uint64_t caller_rbp;
        uint64_t caller_rbp_addr;

        /* Print current frame */
        {
            /* TODO:  Need to lookup a C symbol here! */
            labelbuf[0] = '\0';

            if (frameno != 0) {
                printf ("\n");
            }
            printf ("frame %d\n", frameno);
            printf ("rip 0x%016lx [0x%016lx] %s\n", rip, rip_addr, labelbuf);
            printf ("rsp 0x%016lx\n", rsp);
            printf ("rbp 0x%016lx [0x%016lx]\n", rbp, rbp_addr);
        }

        /* Calculate next frame */
        {
            {
                /* In a gcc frame */
                caller_rip_addr = rbp + 8;
                caller_rip = * (uint64_t *) caller_rip_addr;
                caller_rsp = rbp + 16;
                caller_rbp_addr = rbp;
                caller_rbp = * (uint64_t *) caller_rbp_addr;
                printf ("unwind type dont-care (prev) calls gcc (curr)\n");
            }

            /* Check for loop termination cases */
            if (caller_rsp <= rsp) {
                break;
            }

            if (caller_rbp <= rbp) {
                if (caller_rbp_addr) {
                    break;
                }
            }

            if (frameno > 10000) {
                printf ("Too many frames!\n");
                break;
            }

            rip = caller_rip;
            rip_addr = caller_rip_addr;
            rsp = caller_rsp;
            rbp = caller_rbp;
            rbp_addr = caller_rbp_addr;

            frameno++;
        }
    }
}

void os_disable_azmem() {
  _os_use_azmem = 0;
}

int os_should_use_azmem() {
  return _os_use_azmem;
}

void os_disable_azsched() {
  _os_use_azsched = 0;
}

int os_should_use_azsched() {
  return _os_use_azsched;
}

void os_set_start_suspended(int ss) {
  _os_start_suspended = ss;
}

int os_should_start_suspended() {
  return _os_start_suspended;
}

void os_set_memcommit(int64_t memcommit) {
  _os_memcommit = memcommit;
}

int64_t os_memcommit() {
  return _os_memcommit;
}

void os_set_memmax(int64_t memmax) {
  _os_memmax = memmax;
}

int64_t os_memmax() {
  return _os_memmax;
}

#ifndef AZ_PROXIED // os_setup_avm_launch_environment work is done in launchd for AZ_PROXIED
// Create pgroup, allocationid and setup azmem/azsched, etc.
void os_setup_avm_launch_environment() {
#define AZULHOST_GROUPID 2
  int ret = 0;
  char failure_msg[1024];

  unsigned long avm_process_id = (unsigned long) getpid();
  unsigned long avm_group_id   = AZULHOST_GROUPID;
  unsigned long avm_mem_max    = (unsigned long) _os_memmax;
  unsigned long avm_mem_commit = (unsigned long) _os_memcommit;

  process_set_allocationid(avm_process_id);

  int use_azmem = os_should_use_azmem();
  int use_azsched = os_should_use_azsched();

  if (0 == use_azmem && 0 == use_azsched) {
    // Nothing more to do if running on stock linux
    return;
  }

  if (1 == use_azsched) {
    // Create default groups for AZUL_HOST/system process launch
    ret = az_pgroup_create(avm_group_id, NULL, 0);
    if (ret && errno != EEXIST) {
      snprintf(failure_msg, sizeof(failure_msg),
               "Failed to create launch group: %s", strerror(errno));
      os_guarantee(0, failure_msg);
    }

    ret = az_process_add(0 /*dummy allocationid*/, avm_process_id, avm_group_id);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to add AVM pid %lu to gid %lu: %s\n",
               avm_process_id, avm_process_id, avm_group_id,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }

    ret = az_sched_setscheduler(avm_process_id, AZ_SCHED_AZUL_PRIORITY);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set sched policy for AVM pid %lu, group id %lu: %s\n",
               avm_process_id, avm_process_id, avm_group_id,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }
    syslog(LOG_INFO, "[%lu] Set shed policy for AVM pid %lu, gid %lu\n",
           avm_process_id, avm_process_id, avm_group_id);
  }

  if (1 == use_azmem) {
    ret = az_pmem_set_account_funds(avm_process_id, AZMM_DEFAULT_ACCOUNT,
                                    AZMM_COMMITTED_FUND, AZMM_OVERDRAFT_FUND);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set AC# %d with funds %d/%d: %s\n",
               avm_process_id,
               AZMM_DEFAULT_ACCOUNT, AZMM_COMMITTED_FUND, AZMM_OVERDRAFT_FUND,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }

    ret = az_pmem_set_account_funds(avm_process_id, AZMM_JHEAP_ACCOUNT,
                                    AZMM_COMMITTED_FUND, AZMM_OVERDRAFT_FUND);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set AC# %d with funds %d/%d: %s\n",
               avm_process_id,
               AZMM_JHEAP_ACCOUNT, AZMM_COMMITTED_FUND, AZMM_OVERDRAFT_FUND,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }

    ret = az_pmem_set_account_funds(avm_process_id, AZMM_GCPP_ACCOUNT,
                                    AZMM_GCPP_FUND, AZMM_GCPP_FUND);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set AC# %d with funds %d/%d: %s\n",
               avm_process_id,
               AZMM_JHEAP_ACCOUNT, AZMM_GCPP_FUND, AZMM_GCPP_FUND,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }
    syslog(LOG_INFO, "[%lu] Associated accts with funds\n", avm_process_id);

    ret = az_pmem_fund_account(avm_process_id, AZMM_DEFAULT_ACCOUNT,
        avm_mem_commit);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to fund AC# %d with %lu: %s\n",
               avm_process_id, AZMM_DEFAULT_ACCOUNT, avm_mem_commit,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }
    syslog(LOG_INFO, "[%lu] Funded AC# %d with %lu\n",
           avm_process_id, AZMM_DEFAULT_ACCOUNT, avm_mem_commit);

    ret = az_pmem_set_maximum(avm_process_id, avm_mem_max);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set mem_rlimit with %lu: %s\n",
               avm_process_id, avm_mem_max,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }
    syslog(LOG_INFO, "[%lu] Set mem_rlimit with %lu\n", avm_process_id, avm_mem_max);

    ret = az_pmem_set_account_maximum(avm_process_id, AZMM_DEFAULT_ACCOUNT, avm_mem_max);
    if (ret) {
      snprintf(failure_msg, sizeof(failure_msg),
               "[%lu] Failed to set AC# 0 mem_rlimit with %lu: %s\n",
               avm_process_id, avm_mem_max,
               strerror(errno));
      os_guarantee(0, failure_msg);
    }
    syslog(LOG_INFO, "[%lu] Set AC# 0 mem_rlimit with %lu\n",
           avm_process_id, avm_mem_max);

    // Reserve low memory upfront for VM structures
    int flags = AZMM_BATCHABLE;
    size_t len = __VM_STRUCTURES_END_ADDR__ - __VM_STRUCTURES_START_ADDR__; 
    ret = az_mreserve((address_t)__VM_STRUCTURES_START_ADDR__, len, flags);
    if (ret < 0) {
      snprintf(failure_msg, sizeof(failure_msg),
               "az_mreserve(VMSTRUCTS) failed: %s", strerror(errno));
      os_guarantee(0, failure_msg);
    }
  }

#if 0 // No SUD for azlinux yet
  shared_user_data_purge();   // Garbage collect old ones.

  ret = shared_user_data_create(avm_process_id);
  if (ret) {
    snprintf(failure_msg, sizeof(failure_msg),
             "[%lu] Failed to create shared_user_data\n",
             avm_process_id);
    os_guarantee(0, failure_msg);
  }
#endif // 0

  return;
}
#endif // !AZ_PROXIED
