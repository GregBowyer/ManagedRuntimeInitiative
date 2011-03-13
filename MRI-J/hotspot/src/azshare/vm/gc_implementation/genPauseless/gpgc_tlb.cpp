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


#include "cycleCounts.hpp"
#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tlb.hpp"
#include "gpgc_readTrapArray.hpp"
#include "heapRefBuffer.hpp"
#include "interfaceSupport.hpp"
#include "klassIds.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"




void GPGC_TLB::tlb_resync(TimeDetailTracer* tdt, const char* gc_tag)
{
  GPGC_Interlock interlock(tdt, GPGC_Interlock::TLB_Resync, gc_tag);

  {
    DetailTracer dt(tdt, false, "%s TLB resync", gc_tag);

    GPGC_Space::prepare_for_TLB_resync();
    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::tlb_resync);
os::tlb_resync();
    }
    GPGC_Space::TLB_resync_occurred();
  }
}


// This is the GCLdValueTr trap handler for JavaThreads and the VMThread.
//
// The trap is triggered when a heapRef is loaded from a GC protected page, which means it
// needs to be forwarded to an object's new location.  The trap can trigger on an object
// which hasn't yet been copied.
heapRef GPGC_TLB::lvb_trap_from_c(Thread* thread, heapRef old_ref, heapRef* va)
{
  assert0(thread->is_Java_thread() || thread->is_VM_thread());

  if ( thread->is_Java_thread() ) {
    ((JavaThread*)thread)->mmu_start_pause();
  }

  assert0( UseLVBs );
  assert0( old_ref.not_null() );
  assert0( old_ref.is_heap() );

  bool    nmt;
heapRef new_ref;

  if ( old_ref.is_new() ) {
    assert0( GPGC_ReadTrapArray::is_new_gc_remap_trapped(old_ref) );

    nmt = GPGC_NMT::has_desired_new_nmt(old_ref);
    assert0( nmt || GPGC_NMT::is_new_trap_enabled() );

    {
assert(!thread->is_gc_mode(),"hit GCLdValueNTr when GC mode was set");
      // TODO: we could get better remap performance if we didn't change GC mode unless it was necessary to copy the object.
      GCModeSetter gcmode(thread);

      // Now we're sure we're seeing a new_space relocation trap.
      new_ref = GPGC_Collector::mutator_relocate_object(old_ref);
    }

    if ( new_ref.is_new() ) {
      if ( ! nmt ) {
        // If the NMT wasn't right, then we need to enqueue the ref for the GC threads.
        DEBUG_ONLY( GPGC_NMT::sanity_check_trapped_ref(new_ref); )

        if ( ! thread->get_new_gen_ref_buffer()->record_ref(intptr_t(new_ref.raw_value()), KlassIds::jvm_internal_lvb) ) {
          assert0(thread->get_new_gen_ref_buffer()->is_full());
          GPGC_GCManagerNewStrong::push_mutator_stack_to_full(thread->get_new_gen_ref_buffer_addr()); 
        }
      }
    } else {
      assert0( new_ref.is_old() );
      // When we remap a NewGen ref to an OldGen ref, we consider that an un-NMT'ed ref that
      // must be marked if the OldGen collector NMT traps are enabled.  Unlike the other places
      // where the mutator pushes a ref onto the marking stack, when we cross from NewGen to OldGen,
      // we only push if not already marked live.
      if ( GPGC_NMT::is_old_trap_enabled() ) {
        if ( ! GPGC_Marks::is_any_marked_strong_live(new_ref.as_oop()) ) {
          DEBUG_ONLY( GPGC_NMT::sanity_check_trapped_ref(new_ref); )

          if ( ! thread->get_old_gen_ref_buffer()->record_ref(intptr_t(new_ref.raw_value()), KlassIds::jvm_internal_lvb) ) {
            assert0(thread->get_old_gen_ref_buffer()->is_full());
            GPGC_GCManagerOldStrong::push_mutator_stack_to_full(thread->get_old_gen_ref_buffer_addr()); 
          }
        }
      }
    }
  } else {
    assert0( old_ref.is_old() );
    assert0( GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref) );

    nmt = GPGC_NMT::has_desired_old_nmt(old_ref);
    assert0( nmt || GPGC_NMT::is_old_trap_enabled() );

    {
assert(!thread->is_gc_mode(),"hit GCLdValueNTr when GC mode was set");
      // TODO: we could get better remap performance if we didn't change GC mode unless it was necessary to copy the object.
      GCModeSetter gcmode(thread);

      // Now we're sure we're seeing an old_space relocation trap.
      new_ref = GPGC_Collector::mutator_relocate_object(old_ref);
    }

    assert0( new_ref.is_old() );

    if ( ! nmt ) {
      // If the NMT wasn't right, then we need to enqueue the ref for the GC threads.
      DEBUG_ONLY( GPGC_NMT::sanity_check_trapped_ref(new_ref); )

      if ( ! thread->get_old_gen_ref_buffer()->record_ref(new_ref.raw_value(), KlassIds::jvm_internal_lvb) ) {
        assert0(thread->get_old_gen_ref_buffer()->is_full());
        GPGC_GCManagerOldStrong::push_mutator_stack_to_full(thread->get_old_gen_ref_buffer_addr()); 
      }
    }
  }


#ifdef ASSERT
  if ( RefPoisoning && SHOULD_BE_POISONED(va) ) {
    VERIFY_POISONING(*va, va);
  }
#endif // ASSERT

  intptr_t old_value = POISON_OBJECTREF(old_ref, va).raw_value();
  intptr_t new_value = POISON_OBJECTREF(new_ref, va).raw_value();

  if (old_value == *(intptr_t*)va) {
#ifdef ASSERT
    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)va, old_value) ) {
      GPGC_Marks::set_nmt_update_markid(va, 0xB0);
    }
#else // !ASSERT
    // A hack to get around the stalls with mfcr after a cas8
    // The result of the cmpxchg is ignored
    Atomic::cmpxchg_ptr_without_result(new_value, (intptr_t*)va, old_value);
#endif // !ASSERT
  }

#ifndef PRODUCT
  if ( thread->is_Java_thread() ) {
    ((JavaThread*)thread)->mmu_end_pause();
  }
#endif // !PRODUCT

  assert0( new_ref.not_null() );
  return new_ref;
}


GCT_LEAF(heapRef, GPGC_TLB, lvb_trap_from_asm, (Thread* thread, heapRef old_ref, heapRef* va))
{
  return lvb_trap_from_c(thread, old_ref, va);
}
GCT_END


// This trap entry point is used by GPGC GC threads that need objectRefs to be remapped,
// but which are not doing NMT updates.  It's expected that we don't hit this trap entry
// point until after all object relocation is complete.
heapRef GPGC_TLB::gc_thread_lvb_trap_from_c(Thread* thread, heapRef old_ref, heapRef* va)
{
  assert0( ! thread->is_gc_mode() );

  assert0( thread->is_GC_task_thread() || thread->is_GenPauselessGC_thread() );
  assert0( UseLVBs );
  assert0( old_ref.not_null() );

heapRef new_ref;

#ifndef PRODUCT
  if ( GPGC_Collector::is_verify_collector_thread(thread) ) {
    // verifier threads should always just return the remapped reference.
    guarantee(GPGC_ReadTrapArray::is_remap_trapped(old_ref), "GCLdValueTr in a page that's not marked for TLB traps");
    new_ref = GPGC_Collector::get_forwarded_object(old_ref);
guarantee(new_ref.not_null(),"Verifier thread remapped ref to NULL");
    return new_ref;
  }
#endif // ! PRODUCT

  if ( old_ref.is_new() ) {
    if ( GPGC_Collector::is_old_collector_thread(thread) ) {
      assert(GPGC_OldCollector::collection_state()==GPGC_Collector::MarkingVerification, "only verifying OldGC threads here")
      assert0(GPGC_ReadTrapArray::is_remap_trapped(old_ref));

      new_ref = GPGC_Collector::get_forwarded_object(old_ref);
    } else {
      // We support GCLdValueTr LVB traps for GPGC threads only to ease handling of relocated PermGen
      // objects.  So we don't expect to trigger on any new-space objectRef.
      ShouldNotReachHere();
    }
  } else {
    DEBUG_ONLY( GPGC_PageInfo* pg_info = GPGC_PageInfo::page_info(GPGC_Layout::addr_to_BasePageForSpace(old_ref.as_address())); )
    assert0( old_ref.is_old() );
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref) );
    assert0( pg_info->just_gen()==GPGC_PageInfo::PermGen ||
             (GPGC_Collector::is_new_collector_thread(thread) &&
              GPGC_NewCollector::collection_state()==GPGC_Collector::MarkingVerification) );

    new_ref = GPGC_Collector::get_forwarded_object(old_ref);

    assert0( new_ref.not_null() );
  }

  return new_ref;
}
