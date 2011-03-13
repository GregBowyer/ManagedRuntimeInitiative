/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef COPY_HPP
#define COPY_HPP


#include "globalDefinitions.hpp"
#include "oop.hpp"
#include "copy.inline.hpp"

// Assembly code for platforms that need it.
extern "C" {
  void _Copy_conjoint_words(HeapWord* from, HeapWord* to, size_t count);
  void _Copy_disjoint_words(HeapWord* from, HeapWord* to, size_t count);

  void _Copy_conjoint_words_atomic(HeapWord* from, HeapWord* to, size_t count);
  void _Copy_disjoint_words_atomic(HeapWord* from, HeapWord* to, size_t count);

  void _Copy_aligned_conjoint_words(HeapWord* from, HeapWord* to, size_t count);
  void _Copy_aligned_disjoint_words(HeapWord* from, HeapWord* to, size_t count);

  void _Copy_conjoint_bytes(void* from, void* to, size_t count);

  void _Copy_conjoint_bytes_atomic  (void*   from, void*   to, size_t count);
  void _Copy_conjoint_jshorts_atomic(jshort* from, jshort* to, size_t count);
  void _Copy_conjoint_jints_atomic  (jint*   from, jint*   to, size_t count);
  void _Copy_conjoint_jlongs_atomic (jlong*  from, jlong*  to, size_t count);

  void _Copy_arrayof_conjoint_bytes  (HeapWord* from, HeapWord* to, size_t count);
  void _Copy_arrayof_conjoint_jshorts(HeapWord* from, HeapWord* to, size_t count);
  void _Copy_arrayof_conjoint_jints  (HeapWord* from, HeapWord* to, size_t count);
  void _Copy_arrayof_conjoint_jlongs (HeapWord* from, HeapWord* to, size_t count);
}

class Copy : AllStatic {
 public:
  // Block copy methods have four attributes.  We don't define all possibilities.
  //   alignment: aligned according to minimum Java object alignment (MinObjAlignment)
  //   arrayof:   arraycopy operation with both operands aligned on the same
  //              boundary as the first element of an array of the copy unit.
  //              This is currently a HeapWord boundary on all platforms, except
  //              for long and double arrays, which are aligned on an 8-byte
  //              boundary on all platforms.
  //              arraycopy operations are implicitly atomic on each array element.
  //   overlap:   disjoint or conjoint.
  //   copy unit: bytes or words (i.e., HeapWords) or oops (i.e., pointers).
  //   atomicity: atomic or non-atomic on the copy unit.
  //
  // Names are constructed thusly:
  //
  //     [ 'aligned_' | 'arrayof_' ]
  //     ('conjoint_' | 'disjoint_')
  //     ('words' | 'bytes' | 'jshorts' | 'jints' | 'jlongs' | 'objectRefs')
  //     [ '_atomic' ]
  //
  // Except in the arrayof case, whatever the alignment is, we assume we can copy
  // whole alignment units.  E.g., if MinObjAlignment is 2x word alignment, an odd
  // count may copy an extra word.  In the arrayof case, we are allowed to copy
  // only the number of copy units specified.

  // HeapWords

  // objectRefs,                  conjoint, atomic on each objectRef
  static inline void conjoint_objectRefs_atomic(objectRef* from, objectRef* to, size_t count) {
#ifdef ASSERT
    assert0(HeapWordsPerOop == 1);
    assert_non_zero(count);
    assert_params_ok((HeapWord*) from, (HeapWord*) to, LogHeapWordSize);
#endif
    if ( (UseGenPauselessGC && GPGCNoGC) || (!UseGenPauselessGC) ) {
      // No special handling needed if we're not collecting.  For GPGC bringup only.
pd_conjoint_objectRefs_atomic(from,to,count);
    } else {
      gpgc_objarray_copy(to, from, count);
    }
  }

  // Word-aligned words,    conjoint, not atomic on each word
static inline void conjoint_words(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogHeapWordSize);
    pd_conjoint_words(from, to, count);
  }

  // Word-aligned words,    disjoint, not atomic on each word
static inline void disjoint_words(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogHeapWordSize);
    assert_disjoint(from, to, count);
    pd_disjoint_words(from, to, count);
  }

  // Word-aligned words,    disjoint, atomic on each word
static inline void disjoint_words_atomic(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogHeapWordSize);
    assert_disjoint(from, to, count);
    pd_disjoint_words_atomic(from, to, count);
  }

  // Object-aligned words,  conjoint, not atomic on each word
static inline void aligned_conjoint_words(HeapWord*from,HeapWord*to,size_t count){
    assert_params_aligned(from, to);
    assert_non_zero(count);
    pd_aligned_conjoint_words(from, to, count);
  }

  // Object-aligned words,  disjoint, not atomic on each word
static inline void aligned_disjoint_words(HeapWord*from,HeapWord*to,size_t count){
    assert_params_aligned(from, to);
    assert_disjoint(from, to, count);
    assert_non_zero(count);
    pd_aligned_disjoint_words(from, to, count);
  }

  // bytes, jshorts, jints, jlongs, objectRefs

  // bytes,                 conjoint, not atomic on each byte (not that it matters)
static inline void conjoint_bytes(void*from,void*to,size_t count){
    assert_non_zero(count);
    pd_conjoint_bytes(from, to, count);
  }

  // bytes,                 conjoint, atomic on each byte (not that it matters)
static inline void conjoint_bytes_atomic(void*from,void*to,size_t count){
    assert_non_zero(count);
    pd_conjoint_bytes(from, to, count);
  }

  // jshorts,               conjoint, atomic on each jshort
static inline void conjoint_jshorts_atomic(jshort*from,jshort*to,size_t count){
    assert_params_ok(from, to, LogBytesPerShort);
    assert_non_zero(count);
    pd_conjoint_jshorts_atomic(from, to, count);
  }

  // jints,                 conjoint, atomic on each jint
static inline void conjoint_jints_atomic(jint*from,jint*to,size_t count){
    assert_params_ok(from, to, LogBytesPerInt);
    assert_non_zero(count);
    pd_conjoint_jints_atomic(from, to, count);
  }

  // jlongs,                conjoint, atomic on each jlong
static inline void conjoint_jlongs_atomic(jlong*from,jlong*to,size_t count){
    assert_params_ok(from, to, LogBytesPerLong);
    assert_non_zero(count);
    pd_conjoint_jlongs_atomic(from, to, count);
  }

  // Copy a span of memory.  If the span is an integral number of aligned
  // longs, words, or ints, copy those units atomically.
  // The largest atomic transfer unit is 8 bytes, or the largest power
  // of two which divides all of from, to, and size, whichever is smaller.
  static void conjoint_memory_atomic(void* from, void* to, size_t size);

  // bytes,                 conjoint array, atomic on each byte (not that it matters)
static inline void arrayof_conjoint_bytes(HeapWord*from,HeapWord*to,size_t count){
    assert_non_zero(count);
    pd_arrayof_conjoint_bytes(from, to, count);
  }

  // jshorts,               conjoint array, atomic on each jshort
static inline void arrayof_conjoint_jshorts(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogBytesPerShort);
    assert_non_zero(count);
    pd_arrayof_conjoint_jshorts(from, to, count);
  }

  // jints,                 conjoint array, atomic on each jint
static inline void arrayof_conjoint_jints(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogBytesPerInt);
    assert_non_zero(count);
    pd_arrayof_conjoint_jints(from, to, count);
  }

  // jlongs,                conjoint array, atomic on each jlong
static inline void arrayof_conjoint_jlongs(HeapWord*from,HeapWord*to,size_t count){
    assert_params_ok(from, to, LogBytesPerLong);
    assert_non_zero(count);
    pd_arrayof_conjoint_jlongs(from, to, count);
  }

  // Known overlap methods

  // Copy word-aligned words from higher to lower addresses, not atomic on each word
static inline void conjoint_words_to_lower(HeapWord*from,HeapWord*to,size_t byte_count){
pd_arrayof_conjoint_bytes(from,to,byte_count);
  }

  // Copy word-aligned words from lower to higher addresses, not atomic on each word
static inline void conjoint_words_to_higher(HeapWord*from,HeapWord*to,size_t byte_count){
pd_arrayof_conjoint_bytes(from,to,byte_count);
  }

  // Fill methods

  // Fill word-aligned words, not atomic on each word
  // set_words
static inline void fill_to_words(HeapWord*to,size_t count,jlong value=0){
    assert_params_ok(to, LogHeapWordSize);
    pd_fill_to_words(to, count, value);
  }

  static inline void fill_to_aligned_words(HeapWord* to, size_t count, jlong value = 0) {
    assert_params_aligned(to);
    pd_fill_to_aligned_words(to, count, value);
  }

  // Fill bytes
static inline void fill_to_bytes(void*to,size_t count,jubyte value=0){
    pd_fill_to_bytes(to, count, value);
  }

  // Fill a span of memory.  If the span is an integral number of aligned
  // longs, words, or ints, store to those units atomically.
  // The largest atomic transfer unit is 8 bytes, or the largest power
  // of two which divides both to and size, whichever is smaller.
  static void fill_to_memory_atomic(void* to, size_t size, jubyte value = 0);

  // Zero-fill methods

  // Zero word-aligned words, not atomic on each word
static inline void zero_to_words(HeapWord*to,size_t count){
    assert_params_ok(to, LogHeapWordSize);
    pd_zero_to_words(to, count);
  }

  // Zero bytes
static inline void zero_to_bytes(void*to,size_t count){
    pd_zero_to_bytes(to, count);
  }

 private:
  static bool params_disjoint(HeapWord* from, HeapWord* to, size_t count) {
    if (from < to) {
      return pointer_delta(to, from) >= count;
    }
    return pointer_delta(from, to) >= count;
  }

  // These methods raise a fatal if they detect a problem.

  static void assert_disjoint(HeapWord* from, HeapWord* to, size_t count) {
#ifdef ASSERT
    if (!params_disjoint(from, to, count))
      basic_fatal("source and dest overlap");
#endif
  }

  static void assert_params_ok(void* from, void* to, intptr_t log_align) {
#ifdef ASSERT
    if (mask_bits((uintptr_t)from, right_n_bits(log_align)) != 0)
      basic_fatal("not aligned");
    if (mask_bits((uintptr_t)to, right_n_bits(log_align)) != 0)
      basic_fatal("not aligned");
#endif
  }

  static void assert_params_ok(HeapWord* to, intptr_t log_align) {
#ifdef ASSERT 
    if (mask_bits((uintptr_t)to, right_n_bits(log_align)) != 0)
      basic_fatal("not word aligned");
#endif
  }
  static void assert_params_aligned(HeapWord* from, HeapWord* to) {
#ifdef ASSERT
    if (mask_bits((uintptr_t)from, MinObjAlignmentInBytes-1) != 0)
      basic_fatal("not object aligned");
    if (mask_bits((uintptr_t)to, MinObjAlignmentInBytes-1) != 0)
      basic_fatal("not object aligned");
#endif
  }

  static void assert_params_aligned(HeapWord* to) {
#ifdef ASSERT
    if (mask_bits((uintptr_t)to, MinObjAlignmentInBytes-1) != 0)
      basic_fatal("not object aligned");
#endif
  }

  static void assert_non_zero(size_t count) {
#ifdef ASSERT
    if (count == 0) {
      basic_fatal("count must be non-zero");
    }
#endif
  }

  static void assert_byte_count_ok(size_t byte_count, size_t unit_size) {
#ifdef ASSERT
    if ((size_t)round_to(byte_count, unit_size) != byte_count) {
      basic_fatal("byte count must be aligned");
    }
#endif
  }

  // Platform dependent implementations of the above methods.
#include "copy_pd.hpp"
};

#endif // COPY_HPP
