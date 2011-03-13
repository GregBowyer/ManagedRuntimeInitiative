/*
 * Copyright 2002-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PSSCAVENGE_INLINE_HPP
#define PSSCAVENGE_INLINE_HPP


#include "cardTableExtension.hpp"
#include "objectRef_pd.hpp"

inline void PSScavenge::save_to_space_top_before_gc() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  _to_space_top_before_gc = heap->young_gen()->to_space()->top();
}

inline bool PSScavenge::should_scavenge(heapRef obj){
  return obj.is_new();
}

inline bool PSScavenge::should_scavenge(heapRef p, MutableSpace* to_space) {
  if (should_scavenge(p)) {
    // Skip objects copied to to_space since the scavenge started.
HeapWord*const addr=(HeapWord*)p.as_oop();
    return addr < to_space_top_before_gc() || addr >= to_space->end();
  }
  return false;
}

inline bool PSScavenge::should_scavenge(heapRef p, bool check_to_space) {
  if (check_to_space) {
    ParallelScavengeHeap* heap = (ParallelScavengeHeap*) Universe::heap();
    return should_scavenge(p, heap->young_gen()->to_space());
  }
  return should_scavenge(p);
}

// Attempt to "claim" oop at p via CAS, push the new obj if successful
// This version tests the oop* to make sure it is within the heap before
// attempting marking.
inline void PSScavenge::copy_and_push_safe_barrier(PSPromotionManager* pm,
                                                   heapRef* p) {
  assert(should_scavenge(UNPOISON_OBJECTREF(*p,p), true), "revisiting object");

  objectRef pref = UNPOISON_OBJECTREF(*p,p);
  oop o = pref.as_oop();
  if (o->is_forwarded()) {
POISON_AND_STORE_REF(p,o->forwarded_ref());
  } else {
POISON_AND_STORE_REF(p,pm->copy_to_survivor_space(o,pm->depth_first(),p));
  }
  
  // We cannot mark without test, as some code passes us pointers 
  // that are outside the heap.
  if ((!PSScavenge::is_obj_in_young((HeapWord*) p)) &&
      Universe::heap()->is_in_reserved(p)) {
    objectRef pref = UNPOISON_OBJECTREF(*p,p);
    oop o = pref.as_oop();
    if (PSScavenge::is_obj_in_young((HeapWord*) o)) {
      card_table()->inline_write_ref_field_gc(p, o);
    }
  }
}

#endif // PSSCAVENGE_INLINE_HPP
