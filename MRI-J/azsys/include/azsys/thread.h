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

// thread.h - Shared Linux & (Linux + azul DLKM) thread implementation
//
#ifndef _OS_THREAD_H_
#define _OS_THREAD_H_ 1

#include <os/errors.h>
#include <os/types.h>
#include <os/pagesize.h>
#include <os/azulmmap.h>
#include <os/utilities.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Defined for FDC
#define thread_key_create(key, func) (sys_return_t)pthread_key_create(key, func)
#define thread_key_destroy(key)      (sys_return_t)pthread_key_delete(key)
#define thread_set_specific(key, value)     pthread_setspecific(key, value)
#define thread_get_specific(key)      pthread_getspecific(key)
#define THR_SELF     pthread_self()
#define NULL_THREAD  0L

// Base of type-stable libos managed Thread memory
struct _ThreadBase {
  int64_t _user_vtable;     // User Memory.  Must be at StackBase+0
  int64_t _user_pending_ex; // User Memory.  Must be at StackBase+8
  int64_t _libos_eva, _libos_epc; // Tell user-mode SEGV crash dumps where we crashed
  int32_t _thread_create_millis;
  // These next counts are the number of mapped VM_SMALL_PAGEs.
  // For the JEX stack, pages are mapped in from thread_map_jex_stack up to 
  // the _jex_stk_ext_cnt; the _jex_stk_ext_cnt pagenum is the 1st page NOT 
  // mapped in.
  int16_t _jex_stk_ext_cnt; // Number of stack extensions for the interpreter expression stack
  // For the USR stack, pages are mapped out from the above JEX stk count
  // through to the usr_stk_ext_cnt, which is the 1st page mapped in.
  int16_t _usr_stk_ext_cnt; // Number of stack extensions for the user/C/C++ stack
  // Type-stable HotSpot bits.
  int8_t _is_Complete_Java_thread:1, // If the rest of the JavaThread* is initialized
    _is_Complete_Thread:1;     // If the rest of the Thread* is initialized
  // Record SIGSEGV access type for nio user-stack, delayed signal handling...
  // ( expected values are: 0 - instruction fetch, 1 - store,  2- load )
  int8_t _libos_nio_sigsegv_access_type;   
  // Reserved for libos use.
  int8_t _libos01, _libos02, _libos03,_libos04,_libos05,_libos06;
  // Further memory is reserved for user programs.
};
typedef struct _ThreadBase ThreadBase;

// Thread Stacks!
// Stacks memory map is defined in azulmmap.h
// Stacks start at  __THREAD_STACK_REGION_START_ADDR__
// Stacks end   at  __THREAD_STACK_REGION_END_ADDR__
//    __THREAD_STACK_REGION_START_ADDR__ is equal to (1ULL<<AZUL_THREAD_STACKS_BASE_SHIFT)

// Each individual thread stack is 2Meg for now.  Each stack needs to
// be a power-of-2 aligned and sized.  Fast masking off the RSP
// register gives the base of each stack area.  We can have this many
// simultaneous thread stacks: (start_addr - end_addr)/2M or about
// 8million with the current numbers.
enum {
  thread_stack_shift = 21,      // 2Meg stacks.
  thread_stack_size = (1ULL<<thread_stack_shift)
};

// Each individual stack is broken these regions, and each region is
// restricted to being a multiple of small OS pages to allow for
// hardware protection.
enum {
  // User local data.  Holds the HotSpot JavaThread* structure for HotSpot threads.
  thread_size_local_data    = 1*VM_SMALL_PAGE_SIZE, // Holds for HotSpot the JavaThread* structure
  // Thread-local-data overflow detection.
  // Alt-signal-stack overflow detection.
  // Either is a fatal error.
  thread_size_deadzone1     = 4*VM_SMALL_PAGE_SIZE, 
  // Alternate Signal Stack: the stack used when processing SEGVs.  Since we
  // do our own stack overflow management, we take SEGVs any time a stack
  // pointer overflows its current space, and we extend the stack (map in some
  // more storage) or kill the whole process (fatal red zone touch).  However
  // the SEGV handling requires stack space precisely when we have none, hence
  // the need for an alternate signal stack.
  thread_size_alt_sig_stack = 4*VM_SMALL_PAGE_SIZE, // Alternate stack for SEGV handling
  // Thread-local buffer for collecting hi-res profiling ticks
  thread_size_arta_buf      = 2*VM_SMALL_PAGE_SIZE, // ARTA buffer

  // As the stacks grow, we keep mapping in space.  If they get too close we
  // declare a stack-overflow condition and trigger a user call-back.  If they
  // get within 2 OS pages of each other, we declare a fatal red-zone overflow.
  // The yellow zone has to be large enough for the user program to recognize
  // the callback and start doing corrective action.
  thread_page_yellow_zone   = 10,
  thread_size_yellow_zone   = thread_page_yellow_zone*VM_SMALL_PAGE_SIZE
};

// Stack Layout, replicated for every stack at 2Meg offsets
enum {
  thread_map_local_data    = 0,
  thread_map_deadzone1     = thread_map_local_data   +thread_size_local_data,
  thread_map_alt_sig_stack = thread_map_deadzone1    +thread_size_deadzone1,
  thread_map_arta_buf      = thread_map_alt_sig_stack+thread_size_alt_sig_stack,
  thread_map_jex_stack     = thread_map_arta_buf     +thread_size_arta_buf
};

// To make the code simpler and more readable, we have created libos layer
// wrappers that standardize the prototypes between az_mmap/az_mreserve and
// as_munmap/munmap.  Be careful though, as the differences between when to
// use the mmap versus mreserve aren't always obvious.
void* libos_mmap_wrapper(void *start, size_t size, int prot , int flags, int fd, off_t offset);
void* libos_mreserve_wrapper(void *start, size_t size, int prot , int flags, int fd, off_t offset);
int   libos_munmap_wrapper(void *start, size_t size);

// The start function prototype passed to the pThreads library when we
// create a new thread.
typedef void    *(*Thread_Start_Function)(void *);

extern pid_t        thread_gettid();

// The thread stack callback function type.
typedef void (*threadStackCallbackFuncPtr_t) ( intptr_t );
// Set a call-back function, called after any new thread is created and after
// its stack has moved from the initial pThreads stack to the normal large
// libos managed stack.
extern threadStackCallbackFuncPtr_t _thread_start_callback;
// Set a call-back function, called when any thread is about to have its
// normal large libos managed stack be reclaimed.
extern threadStackCallbackFuncPtr_t _thread_death_callback;
static inline void thread_start_callback_register(threadStackCallbackFuncPtr_t f) { _thread_start_callback=f; }
static inline void thread_death_callback_register(threadStackCallbackFuncPtr_t f) { _thread_death_callback=f; }

//
//  These functions manage thread stacks that conform to Hotspot's particular alignment and layout requirements.
//
extern void  init_all_thread_stacks ();
extern void  alternate_stack_create (intptr_t,  Thread_Start_Function, void*);

extern void* thread_stack_create    ();
extern void  thread_stack_delete    (void* dead_stack);

extern pthread_t thread_init( intptr_t thr, Thread_Start_Function start_function, void *start_param);

// Create a thread that doesn't need to comply with Hotspot's alignment and layout requirements.
// Returns (pthread_t)0 on failure.
extern pthread_t thread_create_and_init(size_t stack_size, Thread_Start_Function start_function, void *start_param);

// Check to see if the current stack pointer(s) are in the yellow zone
inline static int stack_available(const ThreadBase *thr) {
  return (thr->_usr_stk_ext_cnt - thr->_jex_stk_ext_cnt - thread_page_yellow_zone)<<VM_SMALL_PAGE_SHIFT;
}
inline static int is_in_yellow_zone(const ThreadBase *thr) {
  return stack_available(thr)<=0;
}
extern int thread_stack_yellow_zone_reguard2(intptr_t rsp, intptr_t jexstk);
inline static int thread_stack_yellow_zone_reguard(intptr_t jexstk) {
  int dummy;
  return thread_stack_yellow_zone_reguard2((intptr_t)&dummy, jexstk);
}

// TODO: Check with the scheduler folks to see if this makes sense
enum {
    THREAD_PRIORITY_MIN = 0,
    THREAD_PRIORITY_MID = 16,
    THREAD_PRIORITY_MAX = 31
};

inline static sys_return_t thread_self(thread_t *t) {
    *t = pthread_self();
    return SYSERR_NONE;
}

inline static sys_return_t thread_terminate(thread_t thr __attribute__ ((unused))) {
    // TODO, need to figure out if pthread_cancel can be used
    return SYSERR_UNIMPLEMENTED;
}

inline static sys_return_t thread_set_priority(thread_t thr __attribute__ ((unused)),
                                               uint64_t pri __attribute__ ((unused))) {
    // TODO: Need to figure how thread priorities can be set (use pthread_setschedprio? )
    return 0;
}

inline static uint16_t thread_txu_id() {
    uint64_t id = 0;
    // TODO: Need to figure out how to do this
    return (uint16_t) id;
}

inline static uint64_t thread_get_identifier_self() {
    return (uint64_t) pthread_self();
}

extern sys_return_t thread_sleep(uint64_t sleep_nanos);

// TODO: For now define these, will need to revisit
typedef struct thread_perf2_context {
  ureg_t          perfcntctl0;
  ureg_t          perfcntctl1;
  int32_t         perfcnt0;
  reg_t           perfcnt1;
  int32_t         tlbperfcnt0;
} thread_perf2_context_t;


// --- Current thread's ThreadBase address
inline static void* thread_current() {
  char x;
  return (void*)(((intptr_t)&x) & ~(thread_stack_size-1));
}

// --- Thread stack base  
//     Returns address of first slot in user stack.
//     Required for JNIHandle compression in remote JNI calls in proxied JVM.
inline static void* thread_stack_base() 
{
    return (void*)(((char*)thread_current()) + thread_stack_size - 8);
}

#ifndef AZ_PROXIED
extern int thread_whack_main_stack(void* JavaMain, void* args);
#endif // !AZ_PROXIED

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_THREAD_H_
