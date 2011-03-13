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

// utilities.h - Remaining bits and pieces of OS Abstraction Layer for Aztek OS Services

#ifndef _OS_UTILITIES_H_
#define _OS_UTILITIES_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <os/types.h>

extern void init_azsys(void);

// Process exit related interface
extern void exit_out_of_memory(void);
extern void exit_abort(uint64_t code);
extern void set_abort_on_out_of_memory(int flag/* boolean value */);
extern void set_backtrace_callback(void (*f) (void));


// How to make as-yet-unspecified syscalls
extern sys_return_t systemcall(int64_t syscall_num,
                               int64_t arg1,
                               int64_t arg2,
                               int64_t arg3,
                               int64_t arg4,
                               int64_t arg5);

// Miscellaneous convenience functions

// Formatting into a string buffer
#define os_snprintf  snprintf
#define os_vsnprintf vsnprintf

// Safe (syscall) print-to-console function
extern int uprintf(const char *fmt, unsigned long long arg);

// Assertions, etc.
inline static void _os_breakpoint(void)
{
	__asm__ __volatile__("int	$0x3");
}

// Always-on assertion
#define os_guarantee(e, s)   ((e) ? (void)0 : _os_abort("guarantee failed", __FUNCTION__, __FILE__, __LINE__, (s), #e))
// Check system call
#define os_syscallok(x, s)   (((x) == SYSERR_NONE) ? (void)0 : _os_syscallok(x, __FUNCTION__, __FILE__, __LINE__, (s)))

extern void _os_syscallok(sys_return_t rc,
                          const char *_function,
                          const char *_file,
                          int         _line,
                          const char *_msg);

// Debug-only assertions
#ifdef DEBUG
#define os_assert(e, s)      ((e) ? (void)0 : _os_abort("assertion failed",     __FUNCTION__, __FILE__, __LINE__, (s), #e))
// Precondition
#define os_require(e, s)     ((e) ? (void)0 : _os_abort("precondition failed",  __FUNCTION__, __FILE__, __LINE__, (s), #e))
// Postcondition
#define os_ensure(e, s)      ((e) ? (void)0 : _os_abort("postcondition failed", __FUNCTION__, __FILE__, __LINE__, (s), #e))
#else
#define os_assert(e, s)      ((void)0)
#define os_require(e, s)     ((void)0)
#define os_ensure(e, s)      ((void)0)
#endif

// Used internally for noting implementation state/progress
#define os_unreachable()     os_guarantee(0, "should not reach here")
#define os_unimplemented()   os_guarantee(0, "unimplemented")
#define os_untested()        os_guarantee(0, "untested")

// Helper function
extern void _os_abort(const char *_type,
                      const char *_function,
		      const char *_file,
		      int         _line,
		      const char *_msg,
		      const char *_expression);

// For explicit debugging
extern void os_debug(void);

// Print a backtrace
extern void os_backtrace(void);

// TODO: Macro to retrieve the value of a control register into a variable
// src: name of control register (in quotes, e.g., "perfcnt1")
// dst: int64_t variable that wil receive the value
#define  os_get_control_register(src,dst) dst=0

// TODO: Macro to set the value of a control register.
// src: int64_t variable with the value to set
// dst: name of control register (in quotes, e.g., "perfcnt1")
#define os_set_control_register(src,dst)

extern void       os_disable_azmem();
extern int        os_should_use_azmem();
extern void       os_disable_azsched();
extern int        os_should_use_azsched();
extern void       os_set_start_suspended(int ss);
extern int        os_should_start_suspended();
extern void       os_set_memcommit(int64_t memcommit);
extern int64_t    os_memcommit();
extern void       os_set_memmax(int64_t memcommit);
extern int64_t    os_memmax();

#ifndef AZ_PROXIED
extern void       os_setup_avm_launch_environment();
#endif // !AZ_PROXIED

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_UTILITIES_H_
