/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "init.hpp"
#include "jvmtiExport.hpp"
#include "lowMemoryDetector.hpp"
#include "markWord.hpp"
#include "oopTable.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "universe.hpp"

#include "atomic_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

#ifdef ASSERT
int CollectedHeap::_fire_out_of_memory_count = 0;
#endif

// Memory state functions.
CollectedHeap::CollectedHeap() :
  _reserved(), _barrier_set(NULL), _is_gc_active(false),
  _total_collections(0), _total_full_collections(0),
  _max_heap_capacity(0),
  _gc_cause(GCCause::_no_gc), _gc_lastcause(GCCause::_no_gc) {
  NOT_PRODUCT(_promotion_failure_alot_count = 0;)
  NOT_PRODUCT(_promotion_failure_alot_gc_number = 0;)

  if (UsePerfData) {
    EXCEPTION_MARK;

    // create the gc cause jvmstat counters
    _perf_gc_cause = PerfDataManager::create_string_variable(SUN_GC, "cause",
                             80, GCCause::to_string(_gc_cause), CHECK);

    _perf_gc_lastcause =
                PerfDataManager::create_string_variable(SUN_GC, "lastCause",
                             80, GCCause::to_string(_gc_lastcause), CHECK);
  }
}

#ifndef PRODUCT
void CollectedHeap::check_for_bad_heap_word_value(HeapWord* addr, size_t size) {
  if (CheckMemoryInitialization && ZapUnusedHeapArea) {
    for (size_t slot = 0; slot < size; slot += 1) {
      assert((*(intptr_t*) (addr + slot)) != ((intptr_t) badHeapWordVal),
             "Found badHeapWordValue in post-allocation check");
    }
  }
}

void CollectedHeap::check_for_non_bad_heap_word_value(HeapWord* addr, size_t size)
 {
  if (CheckMemoryInitialization && ZapUnusedHeapArea) {
    for (size_t slot = 0; slot < size; slot += 1) {
      assert((*(intptr_t*) (addr + slot)) == ((intptr_t) badHeapWordVal),
             "Found non badHeapWordValue in pre-allocation check");
    }
  }
}
#endif // PRODUCT

#ifdef ASSERT
void CollectedHeap::check_for_valid_allocation_state() {
  Thread *thread = Thread::current();
  // How to choose between a pending exception and a potential
  // OutOfMemoryError?  Don't allow pending exceptions.
  // This is a VM policy failure, so how do we exhaustively test it?
  assert(!thread->has_pending_exception(),
         "shouldn't be allocating with pending exception");
  if (StrictSafepointChecks) {
    assert(thread->allow_allocation(),
           "Allocation done by thread for which allocation is blocked "
           "by No_Allocation_Verifier!");
    // Allocation of an oop can always invoke a safepoint,
    // hence, the true argument
    thread->check_for_valid_safepoint_state(true);
  }
}
#endif

HeapWord* CollectedHeap::allocate_from_tlab_slow(Thread* thread, size_t size, bool zero_mem) {
  // Parking or unparking a TLAB while we're trying to do TLAB management will screw things up.
  NoTLABParkingMark parking_mark;

  assert(size<=MaxTLABObjectAllocationWords, "Larger object can't allocate through TLABs without hurting GPGC.");

  // Retain tlab and allocate object in shared space if
  // the amount free in the tlab is too large to discard.
//This is only used for resizeable TLABs with Serial and Parallel GC
if(!UseGenPauselessGC&&(thread->tlab().free()>thread->tlab().refill_waste_limit())){
    thread->tlab().record_slow_allocation(size);
    return NULL;
  }

  // SMA does not unwind CLZ's during an abort. Because allocating a new TLAB
  // may cause a thread to CLZ potentially shared memory, we must not allow SMA
  // speculation in this method.
  Atomic::sma_abort();

  // Discard tlab and allocate a new one.
  thread->tlab().clear_before_allocation();

  // Try to take the parked tlab on this cpu before allocating a new page
  if ( ParkTLAB ) {
    HeapWord* obj = thread->tlab().unpark_and_allocate(size, zero_mem);
    if ( obj != NULL ) {
      return obj;
    }
  } 
  // There was no parked tlab for us - Allocate a new TLAB...

  // Final tlab sizing is controlled by the specific collector.
  size_t new_tlab_size = Universe::heap()->compute_tlab_size(thread, thread->tlab().desired_size(), size, ThreadLocalAllocBuffer::alignment_reserve());
  if (new_tlab_size == 0) {
    return NULL;
  }

  // Double check the collector sizing work
  assert(size + ThreadLocalAllocBuffer::alignment_reserve() <= new_tlab_size, "Not enough space for object and alignment reserve");
  HeapWord* obj = Universe::heap()->allocate_new_tlab(new_tlab_size);
  if (obj == NULL) {
    return NULL;
  }

  obj = thread->tlab().new_tlab(obj, size, new_tlab_size, zero_mem);
  return obj;
}

size_t CollectedHeap::compute_tlab_size(Thread *thread, size_t requested_free_size, size_t obj_size, size_t alignment_reserve) {
  const size_t aligned_obj_size = align_object_size(obj_size);

  // Compute the size for the new TLAB.
  // The "last" tlab may be smaller to reduce fragmentation.
  // unsafe_max_tlab_alloc is just a hint.
  const size_t available_size = unsafe_max_tlab_alloc(thread) / HeapWordSize;
  
  // ecaspole 050519
  // **** quick hack to force a new tlab w/ parallel gc - this is to test parking with parallel gc
  //if ( requested_free_size == 0 ) {
  //  requested_free_size = ( size_t ) TLABSize - aligned_obj_size;
  //}

  size_t new_tlab_size = MIN2(available_size, requested_free_size + aligned_obj_size);

  // Make sure there's enough room for object and filler int[].
  const size_t obj_plus_filler_size = aligned_obj_size + alignment_reserve;
  if (new_tlab_size < obj_plus_filler_size) {
    // If there isn't enough room for the allocation, return failure.
    return 0;
  }
  return new_tlab_size;
}

HeapWord* CollectedHeap::allocate_new_tlab(size_t size) {
  guarantee(false, "thread-local allocation buffers not supported");
  return NULL;
}

void CollectedHeap::fill_all_tlabs(bool retire) {
  // See note in ensure_parsability() below.
  assert(SafepointSynchronize::is_at_safepoint() ||
         !is_init_completed(),
	 "should only fill tlabs at safepoint");
  // The main thread starts allocating via a TLAB even before it
  // has added itself to the threads list at vm boot-up.
  assert(Threads::first() != NULL,
         "Attempt to fill tlabs before main thread has been added"
         " to threads list is doomed to failure!");
  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) {
     thread->tlab().make_parsable(retire);
  }
  
  if (ParkTLAB) {
ThreadLocalAllocBuffer::reset_parking_area();
  }
}

// --- ensure_zeroing --------------------------------------------------------
// If we made TLABs parseable by inserting filler objects AND did not retire
// the TLAB, then after verification we need to go back and zero over those
// filler objects again - AVM allocation assumes some amount of pre-zeroing
// has already happened.
void CollectedHeap::ensure_prezero(){
  for( JavaThread* jt = Threads::first(); jt; jt = jt->next())
    jt->tlab().zero_filler();
}

void CollectedHeap::ensure_parsability(bool retire_tlabs) {
  // The second disjunct in the assertion below makes a concession
  // for the start-up verification done while the VM is being
  // created. Callers be careful that you know that mutators
  // aren't going to interfere -- for instance, this is permissible
  // if we are still single-threaded and have either not yet
  // started allocating (nothing much to verify) or we have
  // started allocating but are now a full-fledged JavaThread
  // (and have thus made our TLAB's) available for filling.
  assert(SafepointSynchronize::is_at_safepoint() ||
         !is_init_completed(), 
         "Should only be called at a safepoint or at start-up"
         " otherwise concurrent mutator activity may make heap "
         " unparsable again");
    fill_all_tlabs(retire_tlabs);
}

void CollectedHeap::accumulate_statistics_all_tlabs() {
  assert(SafepointSynchronize::is_at_safepoint() ||
         !is_init_completed(),
         "should only accumulate statistics on tlabs at safepoint");
  
  ThreadLocalAllocBuffer::accumulate_statistics_before_gc();
}

void CollectedHeap::resize_all_tlabs() {
  assert(SafepointSynchronize::is_at_safepoint() ||
         !is_init_completed(),
         "should only resize tlabs at safepoint");
  
  ThreadLocalAllocBuffer::resize_all_tlabs();
}

HeapWord*CollectedHeap::incremental_init_obj(HeapWord*obj,size_t size){
  // This is the default implementation of incremental_init_obj(), for those heaps which do not support
  // incremental object zeroing.
  assert(obj != NULL, "cannot initialize NULL object");
  const size_t hs = oopDesc::header_size();
  assert(size >= hs, "unexpected object size");

  JavaThread* jt         = JavaThread::current();
  long        start_zero = os::elapsed_counter();

  Copy::fill_to_aligned_words(obj + hs, size - hs);

  long obj_zero_ticks = os::elapsed_counter() - start_zero;
  if ( obj_zero_ticks > jt->get_obj_zero_max_ticks() ) {
    jt->set_obj_zero_max_ticks(obj_zero_ticks, size-hs);
  }

  return obj;
}
