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

// process.c - Process subsystem for Aztek OS Services
//
// Implementation for most process_* functions is in syscall.s.
// This file contains those functions that extend the available syscall functionality and/or
// provide user-space facilities.


#ifdef AZ_PROXIED
#include <os/posix_redefines.h>
#endif // AZ_PROXIED

#include <stdlib.h>
#include <unistd.h>

#include <os/process.h>
#include <os/memory.h>
#include <os/utilities.h>

static az_allocid_t process_allocationid = 0;

process_id_t
process_id(process_t target)
{
    // We use the linux PID for both process handle and process ID
    return getpid();
}

process_t
process_self_cached()
{
    // We use the linux PID for both process handle and process ID
    return getpid();
}

sys_return_t
process_self(process_t *self)
{
    // We use the linux PID for both process handle and process ID
    *self = getpid();
    return SYSERR_NONE;
}

sys_return_t
process_get_identifier(process_t target, process_id_t *label)
{
    // We use the linux PID for both process handle and process ID
    *label = target;
    return SYSERR_NONE;
}

void
process_set_allocationid(az_allocid_t allocid)
{
    process_allocationid = allocid;
}

az_allocid_t
process_get_allocationid()
{
    return process_allocationid;
}

sys_return_t
process_get_account_info(process_t handle, account_info_t *account_info, size_t *bufsize)
{
    os_unimplemented();
    *bufsize = 0;
    return SYSERR_UNIMPLEMENTED;
}

const char *
process_get_account_name(process_account_num_t account_num)
{
    switch (account_num) {
    case PROCESS_ACCOUNT_DEFAULT:           return "default"; break;
    case PROCESS_ACCOUNT_EMERGENCY_GC:      return "emergency_gc"; break;
    case PROCESS_ACCOUNT_HEAP:              return "heap"; break;
    case PROCESS_ACCOUNT_PAUSE_PREVENTION:  return "pause_prevention"; break;
    default: break;
    }

    return "unknown";
}

sys_return_t
process_get_threads(process_t target,
                    uint64_t *thread_count,
                    thread_t *thread_handle_array)
{
    os_unimplemented();
    *thread_count = 0;
    return SYSERR_UNIMPLEMENTED;
}

sys_return_t
process_get_cpu_statistics(process_t             target,
                           uint64_t              *total_ticks)
{
    os_unimplemented();
    *total_ticks = 0;
    return SYSERR_UNIMPLEMENTED;
}

sys_return_t
process_get_memory_statistics(process_t             target,
                              uint64_t              *allocated_bytes,
                              uint64_t              *committed_bytes,
                              uint64_t              *default_allocated_bytes,
                              uint64_t              *default_committed_bytes,
                              uint64_t              *heap_allocated_bytes,
                              uint64_t              *heap_committed_bytes,
                              int                   *using_grant)
{
    os_unimplemented();
    *allocated_bytes = 0;
    *committed_bytes = 0;
    *default_allocated_bytes = 0;
    *default_committed_bytes = 0;
    *heap_allocated_bytes = 0;
    *heap_committed_bytes = 0;
    *using_grant = 0;
    return SYSERR_UNIMPLEMENTED;
}


