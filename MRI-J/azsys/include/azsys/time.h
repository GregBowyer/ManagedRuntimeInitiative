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

// time.h - Time primitives in OS Services

#ifndef _OS_TIME_H_
#define _OS_TIME_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

#include <os/types.h>

// _init_time()
// Needs to be called as the very first function at system start-up,
// preferably when the application is still single threaded. Otherwise, you 
// risk calling the conversion routines when the values of the conversion 
// factors are zero.
//
extern void _init_time();

// Raw time stamp counter in arbitrary units
//
inline static uint64_t system_tick_count(void) {
    unsigned hi, lo;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

// Time to ticks and ticks to time conversion functions
//
// Preferred functions to use: Implemented using 128 bit integer multiplication
//
// Convert ticks to time
// ticks_to_ time functions are valid for all input values of 
// unit64_t [0..18,446,744,073,709,551,615 = 1.8E19] for 2000 MHz systems
//
uint64_t ticks_to_nanos(uint64_t ticks);
uint64_t ticks_to_micros(uint64_t ticks);
uint64_t ticks_to_millis(uint64_t ticks);

// Convert time to ticks
// time _to_ticks functions for a 2000 MHz system are valid for uint64_t typed input values:
//   nanos:  [0..~2E18]
//   micros: [0..~2E15]
//   millis: [0..~2E12]
// These are equivalent to:
//   2E9 seconds == 3E7 minutes == 5E5 hours == 23148 days == 63 years
//
uint64_t nanos_to_ticks(uint64_t nanos);
uint64_t micros_to_ticks(uint64_t micros);
uint64_t millis_to_ticks(uint64_t millis);

// 64-bit time since boot based on the time stamp counter. To get GMT time a 
// constant must be added.
inline static uint64_t system_time_nanos (void) { return ticks_to_nanos(system_tick_count()); }
inline static uint64_t system_time_micros(void) { return ticks_to_micros(system_tick_count()); }
inline static uint64_t system_time_millis(void) { return ticks_to_millis(system_tick_count()); }

// Conversion factor for ticks<->seconds
extern sys_return_t system_get_seconds_per_tick(double *seconds_per_tick);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_TIME_H_
