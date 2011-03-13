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

// system_config.h - Azul OS system services.

#ifndef _OS_CONFIG_H_
#define _OS_CONFIG_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <os/types.h>

extern sys_return_t system_configuration(uint64_t  _what,
					 address_t _buffer,
					 size_t    _buffer_size);

extern long slow_thread_cpu_time(pid_t tid,
                                 int clock_tics_per_sec,
                                 int user_sys_cpu_time);

// Get the system configuration information.

struct sysconf_frequency {
	uint64_t sysc_numerator;
	uint64_t sysc_denominator;
};

#define	SYSTEM_CONFIGURATION_CPU_FREQUENCY	0x1

// If SYSTEM_CONFIGURATION_CPU_FREQUENCY is passed in as "what", the
// frequency of the CPU is returned in "buffer". In this case 
// "buffer_size" should be sizeof(struct sysconf_frequency).
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_ARGUMENT: Invalid "what" or "buffer_size" value.
// SYSERR_INVALID_ADDRESS: "buffer" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "buffer" address is on a write
//                         protected page.
// SYSERR_INVALID_STATE: On linux systems, reading /proc/cpuinfo 
//                       or parsing MHz failed

// system_timestamp_counter_frequency_estimate
// Determining the TSC frequency should only be done for the VMware
// environment where the TSC that we are reading is a virtual TSC.

#define	SYSTEM_CONFIGURATION_TSC_FREQUENCY	0x2

// If SYSTEM_CONFIGURATION_TSC_FREQUENCY is passed in as "what", the
// frequency at which the count register is incremented is returned
// in "buffer". In this case "buffer_size" should be sizeof(struct
// sysconf_frequency). This enables caller to do nanoseconds to
// "time units" conversion.
// 
// Error codes returned:
// SYSERR_NONE: Success.
// SYSERR_INVALID_ARGUMENT: Invalid "what" or "buffer_size" value.
// SYSERR_INVALID_ADDRESS: "buffer" pointer is bad.
// SYSERR_PROTECTION_FAILURE: "buffer" address is on a write
//                         protected page.
// SYSERR_INVALID_STATE: On linux systems, values are inconsistent
//                       or calculating MHz failed

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_CONFIG_H_
