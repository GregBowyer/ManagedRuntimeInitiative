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

// log.h - Syslog subsystem for Aztek OS Services

#ifndef _OS_LOG_H_
#define _OS_LOG_H_ 1

#include <os/types.h>
#include <stdarg.h>
#include <syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SYSLOG_MAX_MGSLEN	1024

typedef int log_priority_t;
typedef int log_flags_t;
typedef int log_facility_t;

#define LOG_NOTE             LOG_NOTICE
#define LOG_PRIORITY_MASK    LOG_PRIMASK

#define LOG_DEFAULT_FACILITY 0
#define LOG_AUTH_PRIV        LOG_AUTHPRIV
#define LOG_CONSOLE          LOG_USER
#define LOG_FILECACHE        LOG_USER
#define LOG_INIT             LOG_USER
#define LOG_SCHED            LOG_KERN
#define LOG_TRANSPORT        LOG_USER
#define LOG_PROXY            LOG_USER
#define LOG_JAVA             LOG_USER
#define LOG_UTIL             LOG_USER
#define LOG_FACILITY_MASK    LOG_FACMASK

#define LOG_FLAGS_NONE       0x00UL
#define LOG_FLAGS_PID        LOG_PID

// Associate a default identifier (app name by default) and flags with the system log mechanism
extern void log_set_defaults(const char *ident, uint64_t option, uint64_t facility);

// Log a message - format is printf() style. facility_priority indicate the source of
// message and relative priority. flags are optional.
// Note: facility_priority is formed by or'ing the log_facility_t code and the log_priority_t code.
// If no facility is supplied, the default facility specified by the log_set_defaults() call will
// be used, or LOG_USER otherwise.
extern void log_message(uint64_t facility_priority, log_flags_t flags, const char *format, ...);

// As above, but providing a va_args interface.
extern void log_messagev(uint64_t facility_priority, log_flags_t flags, const char *format, va_list ap);

// Log an error message - functions like log_message() but translates the error code to a human-readable
// string.
extern void log_error(uint64_t facility_priority, sys_return_t error_code, const char *format, ...);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_LOG_H_
