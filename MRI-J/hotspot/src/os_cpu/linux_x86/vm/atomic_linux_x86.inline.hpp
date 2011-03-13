/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef ATOMIC_OS_PD_INLINE_HPP
#define ATOMIC_OS_PD_INLINE_HPP


// For lack of a better place, here are some excerpts from Rick Hudson's talk
// on the official X86 memory model.  I got this information from this tech
// talk: http://www.youtube.com/watch?v=WUfvvFD5tAA

// This is specific to the standard write-back memory (not write-combining memory).
// 1.  Loads  are not reordered with other loads
// 2.  Stores are not reordered with other stores
// 3.  Stores are not reordered with older loads
// 4.  Stores ARE     reordered with YOUNGER loads to different locations (load may be "eager")
// 5.  Memory ordering obeys causality (if A before B and B before C, then A before C)
// 6.  Stores to the SAME LOCATION have a global total order
// 7.  Locked instructions have a global total order
// 8.  Loads & Stores are not reordered with locked instructions
// LFENCE & SFENCE are no-ops in this model.

// Implementation of class Atomic

// Atomic operations.
// Inlined, since calling them would cost 3 instructions minimum:
// pushframe, js, return, plus argument marshalling.

#include "atomic.hpp"

inline jint Atomic::xchg(jint exchange_value, volatile jint* dest) {
  return __sync_lock_test_and_set(dest, exchange_value);
}

inline intptr_t Atomic::xchg_ptr(intptr_t exchange_value, volatile intptr_t* dest) {
  return __sync_lock_test_and_set(dest,exchange_value);
}

inline void* Atomic::xchg_ptr(void* exchange_value, volatile void* dest) {
  return (void*)__sync_lock_test_and_set((intptr_t*)dest,(intptr_t)exchange_value);
}

inline jint Atomic::cmpxchg(jint exchange_value, volatile jint* dest, jint compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

// Return old *dest (== compare_value on success).
inline jlong Atomic::cmpxchg(jlong exchange_value, volatile jlong* dest, jlong compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

// Return old *dest (== compare_value on success).
inline intptr_t Atomic::cmpxchg_ptr(intptr_t exchange_value, volatile intptr_t* dest, intptr_t compare_value) {
  return __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline void Atomic::cmpxchg_ptr_without_result(intptr_t exchange_value, volatile intptr_t* dest, intptr_t compare_value) {
  __sync_val_compare_and_swap(dest, compare_value, exchange_value);
}

inline void* Atomic::cmpxchg_ptr(void* exchange_value, volatile void* dest, void* compare_value) {
  return (void*) cmpxchg_ptr((intptr_t) exchange_value, (volatile intptr_t*) dest, (intptr_t) compare_value);
}

inline intptr_t Atomic::add_ptr(intptr_t add_value, volatile intptr_t* dest) {
return __sync_add_and_fetch(dest,add_value);
}

inline void* Atomic::add_ptr(intptr_t add_value, volatile void* dest) {
  return (void*) add_ptr(add_value, (volatile intptr_t*) dest);
}

inline void Atomic::store    (jbyte    store_value, jbyte*    dest) { *dest = store_value; }
inline void Atomic::store    (jshort   store_value, jshort*   dest) { *dest = store_value; }
inline void Atomic::store    (jint     store_value, jint*     dest) { *dest = store_value; }
inline void Atomic::store    (jlong    store_value, jlong*    dest) { *dest = store_value; }
inline void Atomic::store_ptr(intptr_t store_value, intptr_t* dest) { *dest = store_value; }
inline void Atomic::store_ptr(void*    store_value, void*     dest) { *(void**) dest = store_value; }

inline void Atomic::store    (jbyte    store_value, volatile jbyte*    dest) { *dest = store_value; }
inline void Atomic::store    (jshort   store_value, volatile jshort*   dest) { *dest = store_value; }
inline void Atomic::store    (jint     store_value, volatile jint*     dest) { *dest = store_value; }
inline void Atomic::store    (jlong    store_value, volatile jlong*    dest) { *dest = store_value; }
inline void Atomic::store_ptr(intptr_t store_value, volatile intptr_t* dest) { *dest = store_value; }
inline void Atomic::store_ptr(void*    store_value, volatile void*     dest) { *(void* volatile *) dest = store_value; }

inline void Atomic::inc(volatile jint*dest){(void)add_ptr(1LL,dest);}
inline void Atomic::inc_ptr(volatile intptr_t*dest){(void)add_ptr(1LL,dest);}
inline void Atomic::inc_ptr(volatile void*dest){(void)add_ptr(1LL,dest);}

inline void Atomic::dec(volatile jint*dest){(void)add_ptr(-1LL,dest);}
inline void Atomic::dec_ptr(volatile intptr_t*dest){(void)add_ptr(-1LL,dest);}
inline void Atomic::dec_ptr(volatile void*dest){(void)add_ptr(-1LL,dest);}

// Full memory barrier.
inline void Atomic::membar(void) {
  // Lying Dog of a GCC!!!!  This instruction emits as a NO-OP!!!
  //__sync_synchronize();         // gcc built-in full memory barrier
  // FORCE the freak'n full barrier.
  // Need to emit a store/load barrier.
  __asm__ __volatile__("mfence");
}

inline void Atomic::flush_rstack(void) {
  // empty function on X86
}

// Read barrier. Use on monitorenter and spin lock.
inline void Atomic::read_barrier(void) {
  // Void on X86; loads are strongly ordered already
}

// Write barrier. Use on monitoreexit and spin unlock. 
inline void Atomic::write_barrier(void) {
  // Void on X86; writes are strongly ordered already
}

// Write barrier. Use on monitoreexit and spin unlock. Currently conservative and fences clzs.
inline void Atomic::clz_barrier(void) {
  // No CLZ on X86
}

// Force SMA abort.
inline void Atomic::sma_abort(void) { 
  // No SMA on X86
}

inline void Atomic::record_peak(long new_value, long* peak_addr) {
  long peak_value = *peak_addr;
  while ( peak_value < new_value ) {
    if ( peak_value == Atomic::cmpxchg(new_value, (jlong*)peak_addr, peak_value) ) {
      // peak value successfully updated
      break;
    }
    peak_value = *peak_addr;
  }
}

inline void Atomic::add_and_record_peak(long delta, long* value_addr, long* peak_addr) {
  long old_value;
  long new_value;

  // First update the value.
  do {
    old_value = *value_addr;
    new_value = old_value + delta;
  } while ( old_value != Atomic::cmpxchg(new_value, (jlong*)value_addr, old_value) );

  // Next update the peak value, if we've exceeded it.
  Atomic::record_peak(new_value, peak_addr);
}


// 16b CAS op on X86 only
inline bool cmpxchg16b( int64_t newlo, int64_t newhi, intptr_t* dest, int64_t oldlo, int64_t oldhi ) {
  char result;
__asm__ __volatile__("lock; cmpxchg16b %0; setz %1"
                       : "=m"(*dest), "=q"(result)
                       : "m" (*dest),
                         "d" (oldhi),
                         "a" (oldlo),
                         "c" (newhi),
                         "b" (newlo)  : "memory");
  return result;
}


#endif // ATOMIC_OS_PD_INLINE_HPP
