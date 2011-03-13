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


#include "gpgc_collector.hpp"
#include "lvb.hpp"
#include "lvbClosures.hpp"
#include "oop.hpp"
#include "oop.inline.hpp"
#include "thread.hpp"

#include "allocation.inline.hpp"
#include "lvb_pd.inline.hpp"

// When cloning an object, the internal objectRef's must be LVB'ed when they're copied.
void LVB::lvb_clone_objectRefs(oop from, oop to)
{
  LVB_CloneClosure cc(from, to);

  from->oop_iterate(&cc);
}


// This is for use by a few special cases where mutator threads need to get a
// current reference to an object, without marking the object live, but with an
// update of memory with the remapped location.
objectRef LVB::mutator_relocate_only(objectRef* addr)
{
DEBUG_ONLY(Thread*thr=Thread::current();)
  assert0( (!thr->is_GC_task_thread()) && (!thr->is_GenPauselessGC_thread()) );

  if ( UseGenPauselessGC ) {
    // get around some of the initialization stuff.
    // we need to come up with better checks..
    if ( Universe::is_fully_initialized() ) {
      return GPGC_Collector::mutator_relocate_only(addr);
    }
  }

  return UNPOISON_OBJECTREF(*addr, addr);
}


objectRef LVB::mutator_relocate(objectRef ref)
{
DEBUG_ONLY(Thread*thr=Thread::current();)
  assert0( (!thr->is_GC_task_thread()) && (!thr->is_GenPauselessGC_thread()) );

  if ( UseGenPauselessGC ) {
          // get around some of the initialization stuff.
          // we need to come up with better checks..
    if ( Universe::is_fully_initialized() ) {
      return GPGC_Collector::mutator_relocate(ref);
    }
  }

  return ref;
}


objectRef LVB::lvb_or_relocated(objectRef* addr) {
  // TODO: whole function needs cleanup for performance after LVB logic is checked-in.
  assert0( (!VerifyJVMLockAtLVB) || verify_jvm_lock() );

  objectRef ref = *addr;

  if ( UseGenPauselessGC ) {
assert(UseLVBs,"LVB's should be on GPGC");
    if ( GPGCNoGC ) {
      // should just return
      // GPGC is on for allocation, but not collection, so no LVB needed.  This is to enable early GPGC development.
      return PERMISSIVE_UNPOISON(*addr, addr);
    } else {
      Thread* thread = Thread::current();

      // this used to be a gc_mode check.. '
      // replaced it with Java_thread.. for now till we figure what we do with
      // gc_mode checks.
if(thread->is_gc_mode()){
        return LVB::lvb_ref(addr);
      }
      if ( UseGenPauselessGC ) {
assert(!thread->is_gc_mode(),"GC threads should be in non gc-mode to be here");
        return GPGC_Collector::mutator_relocate_only(addr);
        // get around some of the initialization stuff.
        // we need to come up with better checks..
        //if ( !Universe::is_fully_initialized() ) {
        //return PERMISSIVE_UNPOISON(*addr, addr);
        //}
      }
    }
  }

assert(!UseLVBs,"Dont expect to have LVB's turned ON for other GC's");
  return PERMISSIVE_UNPOISON(*addr, addr);
}


// JavaThreads shouldn't be touching objects, and thus shouldn't be LVB'ing, unless they
// own their own jvm lock.
bool LVB::verify_jvm_lock()
{
  Thread* thread = Thread::current();

  if ( thread->is_Java_thread() ) {
    return ((JavaThread*)thread)->jvm_locked_by_self();
  } else {
    return true;
  }
}


#ifdef ASSERT
// This ugly code is to handle refs on the stack that might or might not be poisoned.
// It's only used for GC'ing items, and so doesn't return a value.
void LVB::permissive_poison_lvb(objectRef* addr)
{
assert(RefPoisoning,"Only to make RefPoisoning work for uncertain refs on the stack");

  if ( ! addr->is_in_a_stack() ) {
    // Poisoned refs in the heap are handled normally by lvb trap code
    LVB::lvb(addr);
  } else {
    objectRef old_ref     = *addr;
    bool      is_poisoned = old_ref.is_poisoned();
    if ( ! is_poisoned ) {
      // Normally refs on stack are not poisoned
      LVB::lvb(addr);
    } else {
      // C2 may put poisoned refs on stack due to optimizations.  Updating the ref without
      // a CAS is horrible.  I have a black mark on my soul.
      *addr = old_ref.raw_value() ^ -1;
      LVB::lvb( addr );
      *addr = (*addr).raw_value() ^ -1;
    }
  }
}
#endif //ASSERT

