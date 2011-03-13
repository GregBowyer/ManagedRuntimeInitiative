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

// private.h - Private implementation stuff for Aztek OS Services

#ifndef _OS_PRIVATE_H_
#define _OS_PRIVATE_H_ 1

#include <os/types.h>
#include <os/utilities.h> // Backwards compatibility.

#ifdef __cplusplus
extern "C" {
#endif

// For internal/init use only
#define INIT_PROCESS_LABEL      (process_id_t)1

// Final call for all user threads. Cleans up stack and calls thread_terminate().
extern void _thread_eventhorizon(thread_t  thread,
                                 size_t    stack_rsize,
                                 address_t stack_rbase,
                                 size_t    rstack_size,
                                 address_t rstack_base,
                                 uint64_t  flags,
                                 size_t    stack_asize,
                                 address_t stack_abase);

// Thread-specific data cleanup - called directly from _exit().
extern void _thread_specific_cleanup(user_thread_t thread);

// Process initialization
extern void _init_process(process_t this_process);

// Exceptions initialization
extern void _init_exceptions(process_t this_process);

// Memory (heap and stack) initialization
extern void _init_memory(process_t this_process);

// Thread (stack) initialization
extern void _init_thread(process_t this_process);

// Time initialization
extern void _init_time();

// Features initialization
extern void _init_features(process_t this_process);

// ELF Symbol initialization
extern void _init_elf(process_t this_process);

// Proxy initialization
extern void _init_proxy(process_t this_process, intptr_t comms);

// Weak references to "hook" functions that are called if linked in must be of this type:
typedef void (*proxy_hook_t)(process_t, intptr_t);

// IO layer initialization
// Weak references to "hook" functions that are called if linked in must be of this type:
typedef void (*io_hook_t)(process_t, intptr_t);

// Synchronization
inline static void SYNCP(void)
{
	__asm__ __volatile__("syncp" : /* outputs */ : /* inputs */ : "memory");
}

inline static void SYNCM(void)
{
	__asm__ __volatile__("syncm" : /* outputs */ : /* inputs */ : "memory");
}

inline static void FENCE(void)
{
	__asm__ __volatile__("fence stld,ldst,stst,ldld" : /* outputs */ : /* inputs */ : "memory");
}


// Cache-line aligned bzero
extern void _bzero32(void *_address, size_t _n_bytes);

// Experimental bzero
extern void txu_bzero(void *_address, size_t _n_bytes);

// Experimental memmove
extern void txu_memmove(void *_dst, const void *_src, size_t _n_bytes);

// The actual "bare" system call
extern int __uprintf(const char *fmt, unsigned long long arg, size_t fmtlen);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_PRIVATE_H_
