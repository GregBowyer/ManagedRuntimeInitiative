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

// time.c - Time subsystem - Shared Linux & (Linux + azul DLKM) implementation
//

#include <os/time.h>
#include <os/log.h>		// For logging
#include <os/utilities.h>	// For debug macros
#include <os/config.h>		// For system_configuration

// We do not know the frequency of the CPU or time  stamp counter until
// we start the process.
//
// Conversions between cycles/ticks and time and vice versa is done using 
// 64 bit integer conversion functions. 
// For the following units:  
//    units-type1 = ut1
//    units-type2 = ut2
// Calculate the conversion factor, ut1_to_ut2, to be used for converting
// from ut2 to ut1 such that:   
//    input_value (ut1) * ut1_to_ut2 (ut2/ut1) = value (ut2)
//
// Use 64-bit integers as the scaling factors for greater accuracy, 
// particularly for the nanosecond conversions. Please see the explanation
// that follows in the _init_time function for more details.
//
static uint64_t _millis_to_cycles_int_shift32 = 0; 
static uint64_t _micros_to_cycles_int_shift42 = 0;
static uint64_t _nanos_to_cycles_int_shift52 = 0;
static uint64_t _cycles_to_nanos_int_shift54 = 0;
static uint64_t _cycles_to_micros_int_shift64 = 0;
static uint64_t _cycles_to_millis_int_shift64 = 0;

static double _cycles_to_seconds_fp = 0.0;

static struct sysconf_frequency system_cpu_frequency_hz = {0.0, 0.0};

// Initialize clock rate
void _init_time() {
    // Let's not do the initialization twice.  However, we are not so concerned
    // about not initializing twice that we're going to worry about locking.
    // Time initialization is one of the first tasks to do when initializing.
    // During the early initialization, we're single threaded, hopefully.
    if (system_cpu_frequency_hz.sysc_numerator != 0.0 && 
        system_cpu_frequency_hz.sysc_denominator != 0.0) {
        return;
    }

    // Find the CPU's frequency from the /proc/cpuinfo file
    struct sysconf_frequency cpu_hz;
    sys_return_t rc = system_configuration(SYSTEM_CONFIGURATION_CPU_FREQUENCY, &cpu_hz, sizeof(cpu_hz));
    if (rc != SYSERR_NONE) {
        os_guarantee (rc == SYSERR_NONE, "_init_time: Unable to obtain the CPU frequency\n");
    }

    // Measure the Time Stamp Counter (TSC) register's frequency
    // Determining the TSC frequency should only be done for the VMware
    // environment where the TSC that we are reading is a virtual TSC.
    struct sysconf_frequency tsc_hz;
    rc = system_configuration(SYSTEM_CONFIGURATION_TSC_FREQUENCY, &tsc_hz, sizeof(tsc_hz));
    if (rc != SYSERR_NONE) {
        os_guarantee (rc == SYSERR_NONE, "_init_time: Unable to obtain the system time-stamp counter frequency\n");
    }

    // The denominator is always set to one (check to be certain in the debug version)
    os_assert (((tsc_hz.sysc_denominator == 1) && (cpu_hz.sysc_denominator == 1)), 
        "_init_time: The sysconf_frequency denominators for cpu and time-stamp counter frequency are not equal to one\n");
    
    // Check to be certain that the TSC frequency is within 1 percent of the 
    // CPU frequency.
    // In most cases, we've observed that the difference is within 0.01 precent 
    // or less, so we are being conservative.
    // If running in a VMware environment, VMotion could move the process to 
    // another physical box. But we will still be reading the "virtual" TSC on
    // the new physical box. VMware guarantees that the "virtual" TSC will run
    // at the same frequency on the new physical box as it did on the box where
    // the process started.
    float error_allowed = cpu_hz.sysc_numerator * 0.01; 
    if ((tsc_hz.sysc_numerator > (cpu_hz.sysc_numerator + error_allowed)) ||
        (tsc_hz.sysc_numerator < (cpu_hz.sysc_numerator - error_allowed))) {
        // We could also recheck the TSC frequency at this point to see if we
        // get a different value. If they don't converge then we will need to
        // use the TSC frequency. For now, let's exit.
        os_guarantee (rc == SYSERR_NONE, "_init_time: CPU and time-stamp counter frequencies deviate by greater than 1%\n");
    } 

    // Use the CPU frequency for our ticks <-> time conversion
    system_cpu_frequency_hz.sysc_numerator = cpu_hz.sysc_numerator;
    system_cpu_frequency_hz.sysc_denominator = cpu_hz.sysc_denominator;

    double tick_frequency = (double)cpu_hz.sysc_numerator / (double)cpu_hz.sysc_denominator; // for 2000 MHz == 2,000,000,000

    double _millis_to_cycles_fp  = 0; // cycles per millisecond 
    double _micros_to_cycles_fp  = 0; // cycles per microsecond
    double _nanos_to_cycles_fp   = 0; // cycles per nanosecond
    double _cycles_to_nanos_fp   = 0; // nanoseconds per cycle - most important for accuracy
    double _cycles_to_micros_fp  = 0; // micros per cycle
    double _cycles_to_millis_fp  = 0; // millis per cycle

    _millis_to_cycles_fp  = tick_frequency / 1E3;  // ~ (2E9 cycles/sec)/(1E3 milliseconds/sec) = 2E6 cycles/millisecond 
    _micros_to_cycles_fp  = tick_frequency / 1E6;  // ~ (2E9 cycles/sec)/(1E6 microseconds/sec) = 2E3 cycles/microsecond 
    _nanos_to_cycles_fp   = tick_frequency / 1E9;  // ~ (2E9 cycles/sec)/(1E9 nanoseconds/sec)  = 2 cycles/nanosecond 
    _cycles_to_nanos_fp   = 1E9 / tick_frequency;  // ~ (1E9 nanoseconds/sec)/(2E9 cycles/sec)  = 0.5 nanosecond/cycle
    _cycles_to_micros_fp  = 1E6 / tick_frequency;  // ~ (1E6 microseconds/sec)/(2E9 cycles/sec) = 0.5E-3 microseconds/cycle
    _cycles_to_millis_fp  = 1E3 / tick_frequency;  // ~ (1E3 milliseconds/sec)/(2E9 cycles/sec) = 0.5E-6 milliseconds/cycle

    _cycles_to_seconds_fp = 1   / tick_frequency;  // ~ (1)/(2E9 cycles/sec) = 0.5E-9 seconds/cycle

#if VERBOSE
    log_message (LOG_INFO, LOG_FLAGS_PID, "Azul JVM libos timing info: clock frequency %llu Hz [cycles to nanos = %4.8e, cycles to micros = %4.8e, cycles to millis = %4.8e]", 
        (unsigned long long)((double)cpu_hz.sysc_numerator/(double)cpu_hz.sysc_denominator),
         _cycles_to_nanos_fp, _cycles_to_micros_fp, _cycles_to_millis_fp);
#endif // VERBOSE

    // Calculate the conversion factor closest to some large power of two and 
    // then do the math to do a multiply and shift.
    // Max unsigned 64 bit integer = 18,446,744,073,709,551,615 = 1.8E19
    // We want to be close to this value for our calculated conversion factors
    // but not exceed it. The approximate values are shown for each for a
    // 2000 MHz system.

    _millis_to_cycles_int_shift32 = (uint64_t)(_millis_to_cycles_fp * (1L<<32) + 0.5);          // ~ 2E6    * (4E9)  = 8E15 
    _micros_to_cycles_int_shift42 = (uint64_t)(_micros_to_cycles_fp * (1L<<42) + 0.5);          // ~ 2E3    * (4E12) = 8E15
    _nanos_to_cycles_int_shift52  = (uint64_t)(_nanos_to_cycles_fp * (1L<<52) + 0.5);           // ~ 2      * (4E15) = 8E15
    _cycles_to_nanos_int_shift54 = (uint64_t)(_cycles_to_nanos_fp * (1L<<54) + 0.5);            // ~ 0.5    * (16E15)= 8E15
    _cycles_to_micros_int_shift64 = (uint64_t)(_cycles_to_micros_fp * (1L<<32)*(1L<<32) + 0.5); // ~ 0.5E-3 * (16E18)= 8E15
    _cycles_to_millis_int_shift64 = (uint64_t)(_cycles_to_millis_fp * (1L<<32)*(1L<<32) + 0.5); // ~ 0.5E-6 * (16E18)= 8E12
}

// 128 bit multiply and only use the top 64 bits
//
// No overflow checks by design - fast internal use only
// Simple multiply using %%rax * %%rdx with the 128 bit result stored in 
// both %%rax (lowbits) and %%rdx (highbits); return the highbits.
//
static __inline__ uint64_t multiply_in_128bits_shift_64bits(uint64_t x, 
    uint64_t conversion_value_64_bit_shifted) 
{
    uint64_t lowbits, highbits = 0; 
    asm ("mul %%rdx"
        : "=a" (lowbits), "=d" (highbits)
        : "a" (x), "d" (conversion_value_64_bit_shifted)
        );
    return highbits;
}

// 128 bit multiply and shift the 128 bit result right by xbits_to_shift
//
// No overflow checks by design - fast internal use only
// Simple multiply using %%rax * %%rdx and the 128 bit result is stored in
// %%rax (lowbits) and %%rdx (highbits).
// Fancy SHRD (double precision shift right) shifts the 128 bit value in the 
// %%rax and %%rdx registers xbits_to_shift (shift value is stored in the %%cl
// register) to the right.  Return the lowbits, the value in %%rax.
// If you modify this function, please take care to check the generated 
// instructions for both the unoptimized and optimized versions of the 
// resulting compiled code.  You should see something similar to what is 
// generated by the original version of the source code.  Here is the inlined
// version of the assembly in ticks_to_nanos() in the product version:
// 0x0000000000f70920 <ticks_to_nanos+0>:  mov    0xa24ce9(%rip),%rdx   # 0x1995610 <_cycles_to_nanos_int_shift54>
// 0x0000000000f70927 <ticks_to_nanos+7>:  mov    $0x36,%ecx
// 0x0000000000f7092c <ticks_to_nanos+12>: mov    %rdi,%rax
// 0x0000000000f7092f <ticks_to_nanos+15>: mul    %rdx
// 0x0000000000f70932 <ticks_to_nanos+18>: shrd   %cl,%rdx,%rax
// 0x0000000000f70936 <ticks_to_nanos+22>: retq
//
static __inline__ uint64_t multiply_in_128bits_shift_xbits(uint64_t x, 
    uint64_t conversion_value_xbits_shifted, uint32_t xbits_to_shift) 
{
    uint64_t lowbits = 0; 
    asm ("mul %%rdx\n\t"
         "shrd %%cl, %%rdx, %%rax"
        : "=a" (lowbits)
        : "a" (x), "d" (conversion_value_xbits_shifted), "c" (xbits_to_shift)
        );
    return lowbits;
}

// Functions that use integer multiplication for conversion

// ticks_to_ time functions are valid for all input values of 
// unit64_t [0..18,446,744,073,709,551,615 = 1.8E19] for 2000 MHz systems
//
uint64_t ticks_to_nanos(uint64_t ticks) {
    return multiply_in_128bits_shift_xbits(ticks, _cycles_to_nanos_int_shift54, 54);
}
uint64_t ticks_to_micros(uint64_t ticks) {
    return multiply_in_128bits_shift_64bits(ticks, _cycles_to_micros_int_shift64);
}
uint64_t ticks_to_millis(uint64_t ticks) {
    return multiply_in_128bits_shift_64bits(ticks, _cycles_to_millis_int_shift64);
}

// time _to_ticks for a 2000 MHz system are valid for uint64_t typed input values:
// nanos:  [0..2E18]
// micros: [0..2E15]
// millis: [0..2E12]
// These are equivalent to:
//   2E9 seconds
//   3E7 minutes
//   5E5 hours
//   23148 days
//   63 years
//
uint64_t nanos_to_ticks(uint64_t nanos) {
    return multiply_in_128bits_shift_xbits(nanos, _nanos_to_cycles_int_shift52, 52);
}
uint64_t micros_to_ticks(uint64_t micros) {
    return multiply_in_128bits_shift_xbits(micros, _micros_to_cycles_int_shift42, 42);
}
uint64_t millis_to_ticks(uint64_t millis) {
    return multiply_in_128bits_shift_xbits(millis, _millis_to_cycles_int_shift32, 32);
}

#if USETHESEFUNCTIONSFORFASTPORTING
// Functions that use floating point for conversion

uint64_t ticks_to_millis_use_fp( uint64_t ticks ) {
  return (uint64_t)(ticks * _cycles_to_millis_fp);  // ~ 2E6 ticks/millsec for 2000 MHz system
}
uint64_t ticks_to_micros_use_fp( uint64_t ticks ) {
  return (uint64_t)(ticks * _cycles_to_micros_fp);
}
uint64_t ticks_to_nanos_use_fp( uint64_t ticks ) {
  return (uint64_t)(ticks * _cycles_to_nanos_fp);
}

uint64_t millis_to_ticks_use_fp( uint64_t millis) {
  return (uint64_t)(millis * _millis_to_cycles_fp);
}
uint64_t micros_to_ticks_use_fp( uint64_t micros) {
  return (uint64_t)(micros * _micros_to_cycles_fp);
}
uint64_t nanos_to_ticks_use_fp( uint64_t nanos) {
  return (uint64_t)(nanos * _nanos_to_cycles_fp);
}
#endif // USETHESEFUNCTIONSFORFASTPORTING

sys_return_t system_get_seconds_per_tick(double *seconds_per_tick) {
    *seconds_per_tick = _cycles_to_seconds_fp;  // ~ 0.5E-9 seconds/cycle for 2000 MHz system
    return SYSERR_NONE;
}
