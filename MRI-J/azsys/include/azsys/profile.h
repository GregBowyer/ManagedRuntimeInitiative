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

// profile.h 

#ifndef _OS_PROFILE_H_
#define _OS_PROFILE_H_ 1

#include <os/types.h>
#include <os/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*profile_event_handler_t)(uint64_t _estate, uint64_t _epc, uint64_t _eva);

typedef struct profile_event_handlers {
    profile_event_handler_t perfcnt0;
    profile_event_handler_t perfcnt1;
    profile_event_handler_t perfcnt4;
    profile_event_handler_t perfcnt5;
    profile_event_handler_t tlbperfcnt0;
    profile_event_handler_t tlbperfcnt1;
} profile_event_handlers_t;

// ?.??. profile_enable()
sys_return_t profile_enable(const thread_perf2_context_t*, const profile_event_handlers_t*);

// This will cause all existing threads to have their perfcount registers set to the values
// in "register_values". Newly created threads will also have their perfcount registers set.
// At each profile event, the appropriate perf0/perf1 handler will be called. The profile event
// handlers do not need to reset the perfcount registers.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_TRY_AGAIN: The attempt to set all threads perfcount registers failed


// ?.??. profile_disable()
sys_return_t profile_disable();

// This will immediately stop all profiling events from being delivered.
// 
// Error codes returned:
// SYSERR_NONE: Success.


// ?.??. profile_reconfigure_performance_counters()
sys_return_t profile_reconfigure_performance_counters(const thread_perf2_context_t*);

// This will change the values of the perfcnt registers used to reset the perfcnt registers
// after an overflow exception.
//
// Error codes returned:
// SYSERR_NONE: Success.


// ?.??. initialize_thread_performance_counters()
sys_return_t initialize_thread_performance_counters(thread_t target);
sys_return_t initialize_thread_performance_counters_self();

// This will set the threads perfcntctl and perfcnt registers to the current values
//
// Error codes returned:
// SYSERR_NONE: Success.

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PROFILE_H_
