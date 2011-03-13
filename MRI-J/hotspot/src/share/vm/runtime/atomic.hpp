/*
 * Copyright 1999-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef ATOMIC_HPP
#define ATOMIC_HPP
 
#include "allocation.hpp"

class Atomic : AllStatic {
 public:
  // Atomically store to a location
  static void store    (jbyte    store_value, jbyte*    dest);
  static void store    (jshort   store_value, jshort*   dest);
  static void store    (jint     store_value, jint*     dest);
  static void store    (jlong    store_value, jlong*    dest);
  static void store_ptr(intptr_t store_value, intptr_t* dest);
  static void store_ptr(void*    store_value, void*     dest);

  static void store    (jbyte    store_value, volatile jbyte*    dest);
  static void store    (jshort   store_value, volatile jshort*   dest);
  static void store    (jint     store_value, volatile jint*     dest);
  static void store    (jlong    store_value, volatile jlong*    dest);
  static void store_ptr(intptr_t store_value, volatile intptr_t* dest);
  static void store_ptr(void*    store_value, volatile void*     dest);

  // Performs both read barrier and write barrier.
  // Does NOT wait for clz or rstack!
  static void membar();

  // Read barrier
  static void read_barrier();

  // Write barrier
  static void write_barrier();

  // Abort SMA
  static void sma_abort();

  // clz barrier
  static void clz_barrier();

  // Flush out any backing register store to memory and reset any
  // register spill engine, so it picks up fresh data. Note that a
  // rstack barrier is NOT the same as flushing the rstack.
  static void flush_rstack();

  static jlong post_increment(jlong* dest);
  static jint post_increment(jint* dest);

  // Atomically add to a location, return updated value
  static intptr_t add_ptr(intptr_t add_value, volatile intptr_t* dest);
  static void*    add_ptr(intptr_t add_value, volatile void*     dest);

  // Atomically increment location
inline static void inc(volatile jint*dest);
inline static void inc_ptr(volatile intptr_t*dest);
inline static void inc_ptr(volatile void*dest);

  // Atomically decrement a location
inline static void dec(volatile jint*dest);
inline static void dec_ptr(volatile intptr_t*dest);
inline static void dec_ptr(volatile void*dest);

  // Performs atomic exchange of *dest with exchange_value.  Returns old prior value of *dest.
  static jint     xchg    (jint     exchange_value, volatile jint*     dest);
  static intptr_t xchg_ptr(intptr_t exchange_value, volatile intptr_t* dest);
  static void*    xchg_ptr(void*    exchange_value, volatile void*   dest);

  // Performs atomic compare of *dest and compare_value, and exchanges *dest with exchange_value
  // if the comparison succeeded.  Returns prior value of *dest.  Guarantees a two-way memory
  // barrier across the cmpxchg.  I.e., it's really a 'fence_cmpxchg_acquire'.
inline static jbyte cmpxchg(jbyte exchange_value,volatile jbyte*dest,jbyte compare_value);
inline static jint cmpxchg(jint exchange_value,volatile jint*dest,jint compare_value);
inline static jlong cmpxchg(jlong exchange_value,volatile jlong*dest,jlong compare_value);
inline static intptr_t cmpxchg_ptr(intptr_t exchange_value,volatile intptr_t*dest,intptr_t compare_value);
inline static void cmpxchg_ptr_without_result(intptr_t exchange_value,volatile intptr_t*dest,intptr_t compare_value);
inline static void*cmpxchg_ptr(void*exchange_value,volatile void*dest,void*compare_value);

  static void record_peak        (long new_value, long* peak_addr);
  static void add_and_record_peak(long delta, long* value_addr, long* peak_addr);
};
#endif // ATOMIC_HPP
