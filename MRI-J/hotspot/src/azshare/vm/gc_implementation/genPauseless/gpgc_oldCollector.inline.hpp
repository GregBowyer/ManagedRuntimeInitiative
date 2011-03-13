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
#ifndef GPGC_OLDCOLLECTOR_INLINE_HPP
#define GPGC_OLDCOLLECTOR_INLINE_HPP



#include "gpgc_marks.hpp"
#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_readTrapArray.hpp"
#include "oop.gpgc.inline.hpp"


// Attempt to set the NMT bit on a objectRef to 'marked-through', and rewrite the
// original source of the reference.  The location of the objectRef may be remapped
// as part of this effort.  Return the (possibly) new oop for the objectRef if
// successful.  Otherwise return NULL, indicating someone else has updated this
// ref.
inline bool GPGC_OldCollector::objectRef_needed_nmt(objectRef* ref_addr, objectRef& new_ref)
{
  objectRef old_ref = *ref_addr;
  new_ref = old_ref;
  if ( new_ref.is_null() ) { return false; }

  DEBUG_ONLY( bool is_poisoned = new_ref.is_poisoned(); )
  DEBUG_ONLY( new_ref = PERMISSIVE_UNPOISON(new_ref, ref_addr); )
  assert((new_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  // We ignore new_space refs, they're handled by the NewGen GC.
  if ( new_ref.is_new() ) { return false; }

  assert(new_ref.is_old(), "OldGen GenPauselessGC sees non-heap objectRef");

  if ( GPGC_NMT::has_desired_old_nmt(new_ref) ) {
    assert(!GPGC_ReadTrapArray::is_remap_trapped(new_ref), "Marked through old_space objectRefs must have already been relocated");
    return false;
  }

  // Unmakred-through old_space objectRefs may need to be remapped.
  if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(new_ref));
    // By the time the Old GC collector starts scanning thread stacks, there should only
    // be relocated pages, and no relocating pages, so the object must have already been
    // copied, and we're just remapping here.
    new_ref = GPGC_Collector::get_forwarded_object(new_ref);

    // Remappped old_space refs must always still be old_space refs.
    assert0(new_ref.is_old());
  } else {
    // Set the NMT bit to the marked-through value.
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }

  // try and update the source of the objectRef
  intptr_t old_value = old_ref.raw_value();
  intptr_t new_value = new_ref.raw_value();
  DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

  if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
    // CAS updated the ref in memory.  Return true, to indicate the objectRef must be marked through.
    GPGC_Marks::set_nmt_update_markid(ref_addr, 0x60);
    return true;
  }

  // Updating the objectRef failed.  Either:
  //   1. A mutator hit an LVB, and updated the objectRef before we could, which is OK.
  //   2. A mutator overwrote the objectRef with a different value, so the objectRef seen
  //      in this function isn't there anymore, which is OK.
  //   3. Some thread did a remap without NMT update, in which case this function still
  //      nneds to rewrite the objectRef.
  //
  // Below, we test for and then handle case 3.

  objectRef changed_ref = PERMISSIVE_UNPOISON(*ref_addr, ref_addr);
  if ( changed_ref.is_null() ) { return false; }
  if ( changed_ref.is_new() )  { return false; }

  assert(changed_ref.is_old(), "OldGen GenPauselessGC sees non-heap objectRef");

  // If the other guy remapped and set the NMT, no action is necessary
  if ( GPGC_NMT::has_desired_old_nmt(changed_ref) ) {
    assert0(!GPGC_ReadTrapArray::is_remap_trapped(changed_ref));
    return false;
  }

  // The only way we should get to here is if another thread did a remap-only LVB and moved
  // old_ref to new_ref in an old_space->old_space remapping, and didn't set the the NMT flag.
  assert0(changed_ref.as_oop() == new_ref.as_oop());

  // Update the ref with the correct NMT set
  old_value = ALWAYS_POISON_OBJECTREF(changed_ref).raw_value();

  if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
    // CAS updated the ref in memory.  Return true, to indicate the old_space ref must be marked through now.
    GPGC_Marks::set_nmt_update_markid(ref_addr, 0x61);
    return true;
  }

  // Someone else updated the objectRef again!  This time, it must be set to a valid value.
#ifdef ASSERT
  changed_ref = PERMISSIVE_UNPOISON(*ref_addr, ref_addr);
  assert0(changed_ref.is_new() || (changed_ref.is_old() && GPGC_NMT::has_desired_old_nmt(changed_ref)));
#endif // ASSERT

  return false;
}


inline bool GPGC_OldCollector::objectRef_needs_final_mark(objectRef* ref_addr, objectRef& new_ref)
{
  objectRef ref = *ref_addr;

  DEBUG_ONLY( ref = PERMISSIVE_UNPOISON(ref, ref_addr); )
  assert((ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while final live marking");

  // We ignore null refs and NewSpace refs.
  if ( ! ref.is_old() ) { return false; }

  // We ignore refs with up-to-date NMT values.
  if ( GPGC_NMT::has_desired_old_nmt(ref) ) { return false; }

  // If needed, remap the ref.
  if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
    ref = GPGC_Collector::get_forwarded_object(ref);
    assert0(ref.is_old());
  }

  // Objects already marked strong live are ignored.
  if ( GPGC_Marks::is_old_marked_strong_live(ref) ) { return false; }

new_ref=ref;

  return true;
}


inline void GPGC_OldCollector::unlink_weak_root(objectRef* ref_addr, objectRef old_ref)
{
  assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting an OldGC thread");
  assert(!GPGC_Space::is_in(ref_addr), "Weak root unlink shouldn't be looking at addresses inside the heap");

  objectRef new_ref = old_ref;

  if ( new_ref.is_null() || new_ref.is_new() ) { return; }

  assert0(new_ref.is_old());

  if ( GPGC_NMT::has_desired_old_nmt(new_ref) ) {
    assert0(!GPGC_ReadTrapArray::is_remap_trapped(new_ref));
    assert0(GPGC_Marks::is_old_marked_strong_live(new_ref));
    return;
  }

  if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(new_ref));
    // By the time the Old GC collector starts scanning thread stacks, there should only
    // be relocated pages, and no relocating pages, so the object must have already been
    // copied, and we're just remapping here.
    new_ref = GPGC_Collector::get_forwarded_object(new_ref);

    // Remappped old_space refs must always still be old_space refs.
    assert0(new_ref.is_old());
  } else {
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }

  // NewRef is now a remapped and NMT'ed old-space ref.

  if ( GPGC_Marks::is_old_marked_strong_live(new_ref) ) {
    // Update the ref if the object is live.  
    intptr_t old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value();
    intptr_t new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value();
    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
      // Successfully CAS updated the ref in memory. 
      GPGC_Marks::set_nmt_update_markid(ref_addr, 0x62);
    }
  } else {
    *ref_addr = nullRef;
  }
}


// This call remaps a objectRef, and updates its NMT to the current desired value.
// It doesn't mark through.  Use cautiously!  If you don't guarantee the referent
// is going to be marked through some other means, you can corrupt the heap!
inline objectRef GPGC_OldCollector::remap_and_nmt_only(objectRef* ref_addr)
{
  objectRef old_ref = *ref_addr;
  if ( old_ref.is_null() ) { return old_ref; }

  DEBUG_ONLY( bool is_poisoned = old_ref.is_poisoned(); )
  DEBUG_ONLY( old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  // We ignore new_space refs, they're handled by the NewGen GC.
  if ( old_ref.is_new() ) { return old_ref; }

  assert(old_ref.is_old(), "OldGen GenPauselessGC sees non-heap objectRef");

objectRef new_ref;

  if ( GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref));
    // The ref is trapped for relocation.
    Thread* thread = Thread::current();
if(thread->is_GenPauselessGC_thread()||thread->is_GC_task_thread()){
      new_ref = GPGC_Collector::get_forwarded_object(old_ref);
    } else {
      new_ref = GPGC_Collector::mutator_relocate_object(old_ref);
    }

    // Remappped old_space refs must always still be old_space refs.
    assert0(new_ref.is_old());
    assert0(GPGC_NMT::has_desired_old_nmt(new_ref));
  }
  else if ( GPGC_NMT::has_desired_old_nmt(old_ref) ) {
    // The ref isn't trapped for relocation, and has a valid NMT, so just return it.
    return old_ref;
  }
  else {
    // Set the NMT bit to the marked-through value.
    new_ref = old_ref;
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }

  // try and update the source of the objectRef
  intptr_t old_value = old_ref.raw_value();
  intptr_t new_value = new_ref.raw_value();

  DEBUG_ONLY( if (is_poisoned) { old_value = ALWAYS_POISON_OBJECTREF(old_ref).raw_value(); } )
  DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

  if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
    // CAS updated the ref in memory.
    GPGC_Marks::set_nmt_update_markid(ref_addr, 0x63);
  }

  return new_ref;
}


inline void GPGC_OldCollector::mark_and_push(GPGC_GCManagerOldStrong* gcm, objectRef* ref_addr, int referrer_klass_id)
{
  assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting an OldGC thread");

objectRef new_ref;

  if ( objectRef_needed_nmt(ref_addr, new_ref) ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid(new_oop, 0x64);
      gcm->push_ref_to_stack(new_ref, referrer_klass_id);
    }
  }
}


inline void GPGC_OldCollector::mark_and_push(GPGC_GCManagerOldFinal* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting an OldGC thread");

objectRef new_ref;

  if ( objectRef_needs_final_mark(ref_addr, new_ref) ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_final_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid_final(new_oop, 0x65);
      gcm->push_ref_to_stack(new_ref, 0);
    }
  }
}


inline void GPGC_OldCollector::mark_and_follow(GPGC_GCManagerOldStrong* gcm, objectRef* ref_addr, int referrer_klass_id)
{
  assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting an OldGC thread");

objectRef new_ref;

  if ( objectRef_needed_nmt(ref_addr, new_ref) ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid(new_oop, 0x66);
      GPGC_Marks::set_marked_through(new_oop);

      new_oop->GPGC_follow_contents(gcm);

int size=new_oop->size();
      GPGC_Space::add_live_object(new_oop, size);
      if (ProfileLiveObjects) {
        Thread::current()->live_objects()->add(new_ref.klass_id(), referrer_klass_id, 1, size);
      }
    }
  }
}


inline void GPGC_OldCollector::mark_and_follow(GPGC_GCManagerOldFinal* gcm, objectRef* ref_addr)
{
  assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting an OldGC thread");

objectRef new_ref;

  if ( objectRef_needs_final_mark(ref_addr, new_ref) ) {
    oop new_oop = new_ref.as_oop();
    if ( GPGC_Marks::atomic_mark_final_live_if_dead(new_oop) ) {
      GPGC_Marks::set_markid_final(new_oop, 0x67);
      GPGC_Marks::set_marked_through_final(new_oop);

      new_oop->GPGC_follow_contents(gcm);
    }
  }
}


inline void GPGC_OldCollector::update_card_mark(objectRef* ref_addr)
{
  objectRef old_ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);

  if ( old_ref.is_new() ) {
    GPGC_Marks::atomic_attempt_card_mark(ref_addr);
  }
}


// remaps the ref and sets the correct NMT bit but doesn't write it back
// doesn't deal with mutators separately.
// doesn't mark-through, you have to guarantee that the ref is processed by GC
inline objectRef GPGC_OldCollector::remap_and_nmt_without_cas (objectRef* ref_addr)
{
  objectRef old_ref = *ref_addr;
  if ( old_ref.is_null() ) { return old_ref; }

  DEBUG_ONLY( old_ref = PERMISSIVE_UNPOISON(old_ref, ref_addr); )
  assert((old_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  // We ignore new_space refs, they're handled by the NewGen GC.
  if ( old_ref.is_new() ) { return old_ref; }

  assert(old_ref.is_old(), "OldGen GenPauselessGC sees non-heap objectRef");

objectRef new_ref;

  if ( GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref));
    // The ref is trapped for relocation.
    new_ref = GPGC_Collector::get_forwarded_object(old_ref);

    // Remappped old_space refs must always still be old_space refs.
    assert0(new_ref.is_old());
    assert0(GPGC_NMT::has_desired_old_nmt(new_ref));
  }
  else if ( GPGC_NMT::has_desired_old_nmt(old_ref) ) {
    // The ref isn't trapped for relocation, and has a valid NMT, so just return it.
    return old_ref;
  }
  else {
    // Set the NMT bit to the marked-through value.
    new_ref = old_ref;
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }
  return new_ref;
}


// Determine if an object from instanceRefKlass:GPGC_oop_follow_contents() is
// claimed by the JLR handler or not.  If the JLR handler claims it, then false
// is returned to tell the caller that the referent field should not be marked
// through.
inline bool GPGC_OldCollector::mark_through_non_strong_ref(GPGC_GCManagerOldStrong* gcm,
objectRef referent,
                                                           oop obj,
                                                           ReferenceType ref_type)
{
  if ( referent.is_old() ) {
    referent = GPGC_Collector::remap(referent);

    assert0(referent.is_old());

    if ( ! GPGC_Marks::is_old_marked_strong_live(referent) &&
         jlr_handler()->capture_java_lang_ref(gcm->reference_lists(), obj, ref_type) )
    {
      if ( ref_type == REF_FINAL ) {
        // The JLR handler has captured this reference.  FinalRefs need to be concurrently
        // marked through for the GPGC concurrent final JLR handling algorithm.
        oop referent_oop = referent.as_oop();
        if ( GPGC_Marks::atomic_mark_final_live_if_dead(referent_oop) ) {
          GPGC_GCManagerOldFinal* final_gcm = GPGC_GCManagerOldFinal::get_manager(gcm->manager_number());
          GPGC_Marks::set_markid_final(referent_oop, 0x68);
          final_gcm->push_ref_to_stack(referent, 0);
        }
      }

      return false;
    }
  }

  return true;
}

#endif // GPGC_OLDCOLLECTOR_INLINE_HPP
