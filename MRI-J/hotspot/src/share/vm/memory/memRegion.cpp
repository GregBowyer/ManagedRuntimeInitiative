/*
 * Copyright 2000-2004 Sun Microsystems, Inc.  All Rights Reserved.
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


// A very simple data structure representing a contigous word-aligned
// region of address space.

#include "memRegion.hpp"
#include "oop.hpp"
#include "typeArrayOop.hpp"

#include "oop.inline.hpp"

MemRegion MemRegion::intersection(const MemRegion mr2) const {
  MemRegion res;
  HeapWord* res_start = MAX2(start(), mr2.start());
  HeapWord* res_end   = MIN2(end(),   mr2.end());
  if (res_start < res_end) {
    res.set_start(res_start);
    res.set_end(res_end);
  }
  return res;
}

MemRegion MemRegion::_union(const MemRegion mr2) const {
  // If one region is empty, return the other
  if (is_empty()) return mr2;
  if (mr2.is_empty()) return MemRegion(start(), end());

  // Otherwise, regions must overlap or be adjacent
  assert(((start() <= mr2.start()) && (end() >= mr2.start())) ||
         ((mr2.start() <= start()) && (mr2.end() >= start())), 
             "non-adjacent or overlapping regions");
  MemRegion res;
  HeapWord* res_start = MIN2(start(), mr2.start());
  HeapWord* res_end   = MAX2(end(),   mr2.end());
  res.set_start(res_start);
  res.set_end(res_end);
  return res;
}

MemRegion MemRegion::minus(const MemRegion mr2) const {
  // There seem to be 6 cases:
  //                  |this MemRegion|
  // |strictly below|
  //   |overlap beginning|
  //                    |interior|
  //                        |overlap ending|
  //                                   |strictly above|
  //              |completely overlapping|
  // We can't deal with an interior case because it would 
  // produce two disjoint regions as a result.
  // We aren't trying to be optimal in the number of tests below, 
  // but the order is important to distinguish the strictly cases
  // from the overlapping cases.
  if (mr2.end() <= start()) {
    // strictly below
    return MemRegion(start(), end());
  }
  if (mr2.start() <= start() && mr2.end() <= end()) {
    // overlap beginning
    return MemRegion(mr2.end(), end());
  }
  if (mr2.start() >= end()) {
    // strictly above
    return MemRegion(start(), end());
  }
  if (mr2.start() >= start() && mr2.end() >= end()) {
    // overlap ending
    return MemRegion(start(), mr2.start());
  }
  if (mr2.start() <= start() && mr2.end() >= end()) {
    // completely overlapping
    return MemRegion();
  }
  if (mr2.start() > start() && mr2.end() < end()) {
    // interior
    guarantee(false, "MemRegion::minus, but interior");
    return MemRegion();
  }
  ShouldNotReachHere();
  return MemRegion();
}


void MemRegion::fill() {
  if( byte_size() == 0 ) return;

  // This constructs a dummy object filling the specified memory region.
  if( word_size() == (size_t)oopDesc::header_size() ) {
oop obj=(oop)start();
    obj->set_mark(markWord::prototype_with_kid(java_lang_Object_kid));
  } else {
    // Need enuf space for at least a minimal array
    int int_ary_hdr_size = typeArrayOopDesc::header_size_T_INT();
    assert0( byte_size() >= (size_t)int_ary_hdr_size );
    const size_t array_length_bytes = byte_size() - int_ary_hdr_size;
    const size_t array_length_ints  = array_length_bytes >> LogBytesPerInt;
    assert0( array_length_ints > 0 && array_length_ints < (size_t)max_jint ); // Need a lot of large arrays!!!
    arrayOop obj = (arrayOop)start();
    obj->set_mark(markWord::prototype_with_kid(intArrayKlass_kid));
    obj->set_length((int)array_length_ints);
    assert0(((oop)obj)->is_typeArray());
  }
}

