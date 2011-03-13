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


#include "collectedHeap.hpp"
#include "copy.hpp"
#include "parallelScavengeHeap.hpp"
#include "psPromotionLAB.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "oop.inline.hpp"

// This is the shared initialization code. It sets up the basic pointers,
// and allows enough extra space for a filler object. We call a virtual
// method, "lab_is_valid()" to handle the different asserts the old/young
// labs require. 
void PSPromotionLAB::initialize(MemRegion lab) {
  assert(lab_is_valid(lab), "Sanity");

  HeapWord* bottom = lab.start();
  HeapWord* end    = lab.end();

  set_bottom(bottom);
  set_end(end);
  set_top(bottom);

  // We can be initialized to a zero size!
  if (free() > 0) {
    if (ZapUnusedHeapArea) {
      debug_only(Copy::fill_to_words(top(), free()/HeapWordSize, badHeapWord));
    }
    
    // NOTE! We need to allow space for a filler object.
assert(lab.word_size()>=(size_t)oopDesc::header_size(),"lab is too small");
    end = end - oopDesc::header_size();
    set_end(end);

    _state = needs_flush;
  } else {
    _state = zero_size;
  }

  assert(this->top() <= this->end(), "pointers out of order");
}

// Fill all remaining lab space with an unreachable object.
// The goal is to leave a contiguous parseable span of objects.
void PSPromotionLAB::flush() {
  assert(_state != flushed, "Attempt to flush PLAB twice");
  assert(top() <= end(), "pointers out of order");
  
  // If we were initialized to a zero sized lab, there is
  // nothing to flush
  if (_state == zero_size)
    return;

  // PLAB's never allocate the last oopDesc::header_size so they can
  // always fill with an object
  MemRegion mr(top(), end() + oopDesc::header_size());
  mr.fill();
  
  set_bottom(NULL);
  set_end(NULL);
  set_top(NULL);

  _state = flushed;
}

bool PSPromotionLAB::unallocate_object(oop obj) {
  assert(Universe::heap()->is_in(obj), "Object outside heap");
  
  if (contains(obj)) {
    HeapWord* object_end = (HeapWord*)obj + obj->size();
    assert(object_end <= top(), "Object crosses promotion LAB boundary");

    if (object_end == top()) {
      set_top((HeapWord*)obj);
      return true;
    }
  }

  return false;
}

// Fill all remaining lab space with an unreachable object.
// The goal is to leave a contiguous parseable span of objects.
void PSOldPromotionLAB::flush() {
  assert(_state != flushed, "Attempt to flush PLAB twice");
  assert(top() <= end(), "pointers out of order");
  
  if (_state == zero_size)
    return;

  HeapWord* obj = top();

  PSPromotionLAB::flush();

  assert(_start_array != NULL, "Sanity");

  _start_array->allocate_block(obj);
}

#ifdef ASSERT

bool PSYoungPromotionLAB::lab_is_valid(MemRegion lab) {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  MutableSpace* to_space = heap->young_gen()->to_space();
  MemRegion used = to_space->used_region();
  if (used.contains(lab)) {
    return true;
  }

  return false;
}

bool PSOldPromotionLAB::lab_is_valid(MemRegion lab) {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  assert(_start_array->covered_region().contains(lab), "Sanity");    

  PSOldGen* old_gen = heap->old_gen();
  MemRegion used = old_gen->object_space()->used_region();
  
  if (used.contains(lab)) {
    return true;
  }

  return false;
}

#endif /* ASSERT */
