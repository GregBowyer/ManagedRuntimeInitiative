/*
 * Copyright 2005-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "gcLocker.hpp"
#include "genCollectedHeap.hpp"
#include "heapDumper.hpp"
#include "heapInspection.hpp"
#include "instanceRefKlass.hpp"
#include "jvmtiExport.hpp"
#include "memoryService.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "parallelScavengeHeap.hpp"
#include "psMarkSweep.hpp"
#include "tickProfiler.hpp"
#include "vmGCOperations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "parallelScavengeHeap.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

void VM_GC_Operation::acquire_pending_list_lock() {
  // we may enter this with pending exception set
instanceRefKlass::acquire_pending_list_lock();
}


void VM_GC_Operation::release_and_notify_pending_list_lock() {
instanceRefKlass::release_and_notify_pending_list_lock();
}

// Allocations may fail in several threads at about the same time,
// resulting in multiple gc requests.  We only want to do one of them.
// In case a GC locker is active and the need for a GC is already signalled,
// we want to skip this GC attempt altogether, without doing a futile
// safepoint operation. 
bool VM_GC_Operation::skip_operation() const { 
  bool skip = (_gc_count_before != Universe::heap()->total_collections());
  if (_full && skip) {
    skip = (_full_gc_count_before != Universe::heap()->total_full_collections());
  }
  if (!skip && GC_locker::is_active_and_needs_gc()) {
    skip = Universe::heap()->is_maximal_no_gc();
    assert(!(skip && (_gc_cause == GCCause::_gc_locker)),
           "GC_locker cannot be active when initiating GC");
  }
  return skip;
}

bool VM_GC_Operation::doit_prologue() {
  assert(Thread::current()->is_Java_thread(), "just checking");

  acquire_pending_list_lock();
  // If the GC count has changed someone beat us to the collection
  // Get the Heap_lock after the pending_list_lock.
  Heap_lock.lock_allowing_gc(JavaThread::current());
  GET_RPC;
  Heap_lock._rpc = RPC;
  // Check invocations
  if (skip_operation()) {
    // skip collection
Heap_lock.unlock();
    release_and_notify_pending_list_lock();
    _prologue_succeeded = false;
  } else {
    _prologue_succeeded = true;
  }
  return _prologue_succeeded;
}


void VM_GC_Operation::doit_epilogue() {
  assert(Thread::current()->is_Java_thread(), "just checking");
  // Release the Heap_lock first.
Heap_lock.unlock();
  release_and_notify_pending_list_lock();
}


bool VM_GC_HeapInspection::doit_prologue() {
  if (Universe::heap()->supports_heap_inspection()) {
    return VM_GC_Operation::doit_prologue();
  } else {
    return false;
  }
}

bool VM_GC_HeapInspection::skip_operation() const {
  assert(Universe::heap()->supports_heap_inspection(), "huh?");
  return false;
}

void VM_GC_HeapInspection::doit() {
  HandleMark hm;
  CollectedHeap* ch = Universe::heap();
  if (_full_gc) {
    ch->collect_as_vm_thread(GCCause::_heap_inspection);
  } else {
    // make the heap parsable (no need to retire TLABs)
    ch->ensure_parsability(false);
  }
  HeapInspection::heap_inspection(_out);
}


void VM_GenCollectForAllocation::doit() {
  JvmtiGCForAllocationMarker jgcm;

  GenCollectedHeap* gch = GenCollectedHeap::heap();
  GCCauseSetter gccs(gch, _gc_cause);
  _res = gch->satisfy_failed_allocation(_size, _tlab);
  assert(gch->is_in_reserved_or_null(_res), "result not in heap");

  if (_res == NULL && GC_locker::is_active_and_needs_gc()) {
    set_gc_locked();
  }
} 			

void VM_GenCollectFull::doit() {
  JvmtiGCFullMarker jgcm;

  GenCollectedHeap* gch = GenCollectedHeap::heap();
  GCCauseSetter gccs(gch, _gc_cause);
  gch->do_full_collection(gch->must_clear_all_soft_refs(), _max_level);
} 			
