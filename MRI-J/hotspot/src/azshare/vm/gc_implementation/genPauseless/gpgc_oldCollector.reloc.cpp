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


#include "atomic.hpp"
#include "cycleCounts.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerOldReloc.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_rendezvous.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "oop.gpgc.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"


void GPGC_OldCollector::sideband_forwarding_init(long work_unit)
{
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->small_space_in_perm(), objectRef::old_space_id);
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->small_space_in_old(),  objectRef::old_space_id);

  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->mid_space_in_perm(), objectRef::old_space_id);
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->mid_space_in_old(),  objectRef::old_space_id);
}


void GPGC_OldCollector::remap_to_mirror_for_relocate(GPGC_PopulationArray* relocation_array)
{
  // TODO: refactor, this is the same as GPGC_NewCollector::remap_to_mirror_for_relocate.
  uint64_t max_cursor = relocation_array->max_cursor();

  for ( uint64_t cursor=0; cursor<max_cursor; cursor++ ) {
    PageNum page = relocation_array->page(cursor);
    GPGC_Space::remap_to_mirror(page);
  }
}


void GPGC_OldCollector::relocate_mid_pages(GPGC_GCManagerOldReloc* gcm, long work_unit, GPGC_PopulationArray* array, int64_t stripe)
{
  GPGC_RemapBuffer* remap_buffer = gcm->remap_buffer();

  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )

    // Process the pages just claimed:
    for ( uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum page = array->page(cursor);

      GPGC_PageRelocation::gc_relocate_mid_page(page, remap_buffer, false, stripe);

      DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )
    }
  }
}


//  This is the top-level function for moving PermGen pages in memory.
//
//  On stock linux, it should be called from within the perm relocation safepoint. 
//
//  With azmem, it should be called during relocation setup, and then during
//  the perm relocation safepoint commit_batched_memory_ops() should be called to
//  put the changes into effect.
void GPGC_OldCollector::remap_perm_gen_memory(TimeDetailTracer* tdt)
{
  if ( BatchedMemoryOps ) {
    // Need to hold the interlock if we're doing batched memory ops.
    assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));
  }

  {
    DetailTracer dt(tdt, false, "O: remap perm to mirror for %lu small pages", page_pops()->small_space_in_perm()->max_cursor());

    // Move small space pages we're relocating to a mirror address, so invalid accesses will cause a SEGV.
    // TODO: Parallelize this

    remap_to_mirror_for_relocate(page_pops()->small_space_in_perm());
  }

  {
    DetailTracer dt(tdt, false, "O: remap perm live objs from %lu mid pages", page_pops()->mid_space_in_perm()->max_cursor());

    // Remap live objects in mid space pages we're relocating.
    GPGC_GCManagerOldReloc::reset_remap_buffer(GPGC_PageInfo::PermGen);

    long          work_unit = 1;
PGCTaskQueue*q=PGCTaskQueue::create();

    for ( int64_t i=0; i<int64_t(GenPauselessOldThreads); i++ ) {
      q->enqueue( new GPGC_OldGC_RelocateMidPagesTask(work_unit, page_pops()->mid_space_in_perm(), i) );
    }

    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

    page_pops()->mid_space_in_perm()->reset_cursor();
  }

  // We currently don't remap large space blocks within OldGen or PermGen for deframentation, but we might someday.
}


//  This is the top-level function for moving PermGen pages in memory.
//
//  On stock linux, it should be called from within the perm relocation safepoint. 
//
//  With azmem, it should be called during relocation setup, and then during
//  the perm relocation safepoint commit_batched_memory_ops() should be called to
//  put the changes into effect.
void GPGC_OldCollector::remap_old_gen_memory(TimeDetailTracer* tdt)
{
  if ( BatchedMemoryOps ) {
    // Need to hold the interlock if we're doing batched memory ops.
    assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));
  }

  {
    DetailTracer dt(tdt, false, "O: remap old to mirror for %lu small pages", page_pops()->small_space_in_old()->max_cursor());

    // Move small space pages we're relocating to a mirror address, so invalid accesses will cause a SEGV.
    // TODO: Parallelize this

    remap_to_mirror_for_relocate(page_pops()->small_space_in_old());
  }

  {
    DetailTracer dt(tdt, false, "O: remap old live objs from %lu mid pages", page_pops()->mid_space_in_old()->max_cursor());

    // Zero out the list of mid space target pages.
    page_pops()->mid_space_targets()->reset();

    // Remap live objects in mid space pages we're relocating.
    GPGC_GCManagerOldReloc::reset_remap_buffer(GPGC_PageInfo::OldGen);

    long          work_unit = 1;
PGCTaskQueue*q=PGCTaskQueue::create();

    for ( int64_t i=0; i<int64_t(GenPauselessOldThreads); i++ ) {
      q->enqueue( new GPGC_OldGC_RelocateMidPagesTask(work_unit, page_pops()->mid_space_in_old(), i) );
    }

    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

    page_pops()->mid_space_in_old()->reset_cursor();
  }

  // We currently don't remap large space blocks within OldGen or PermGen for degramentation, but we might someday.
}


void GPGC_OldCollector::heal_mid_pages(GPGC_GCManagerOldReloc* gcm, int64_t stripe)
{
  GPGC_RemapTargetArray* mid_space_targets = page_pops()->mid_space_targets();
  uint64_t               max_cursor        = mid_space_targets->current_length();
  PageNum                resource_page     = GPGC_PageBudget::preallocated_page_for_thread();
  long                   target_pages      = 0;

  DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )

  for ( uint64_t cursor=0; cursor<max_cursor; cursor++ ) {
    // Process only the pages in the specified stripe:
    if ( mid_space_targets->stripe(cursor) == stripe ) {
      PageNum             page       = mid_space_targets->page(cursor);
      GPGC_PageInfo::Gens source_gen = mid_space_targets->source_gen(cursor);

      GPGC_Space::heal_mid_remapping_page(page, source_gen, resource_page, relocation_spike());

      // Account for the number of pages:
      GPGC_PageInfo* info      = GPGC_PageInfo::page_info(page);
      PageNum        last_page = GPGC_Layout::addr_to_PageNum( info->top() - 1 );
      long           pages     = last_page - page + 1;

      target_pages += pages;
    }
  }

  page_pops()->atomic_increment_target_pages_in_old_gen(target_pages);
}


void GPGC_OldCollector::relocate_small_pages(GPGC_GCManagerOldReloc* gcm, long work_unit, GPGC_PopulationArray* array)
{
  GPGC_RelocBuffer* relocation_buffer = gcm->relocation_buffer();

  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )

    if ( relocation_buffer->_prime.empty() ) {
      relocation_buffer->get_relocation_page(&relocation_buffer->_prime, 0);
      relocation_buffer->increment_page_count();
    }

    // Process the pages just claimed:
    for ( uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum page = array->page(cursor);

      GPGC_PageRelocation::gc_relocate_small_page(page, relocation_buffer, false);
      GPGC_Space::relocated_page(page, relocation_spike());

      if ( relocation_buffer->_generation == GPGC_PageInfo::OldGen ) {
        GPGC_Rendezvous::check_suspend_relocating();
      }
    }
  }

  if ( relocation_buffer->_generation == GPGC_PageInfo::OldGen ) {
    GPGC_Rendezvous::relocating_thread_done();
  }
}


// Get the remapped klassRef for an oop's klass, if it's been relocated.  When relocating objects,
// the collection algorithm guarantees that the klass is either not being relocated or has already been
// copied.  This method is callable by mutators, OldGC, or NewGC threads.
klassRef GPGC_OldCollector::relocate_obj_klass(oop mirror_obj)
{
  klassRef* klass_addr = mirror_obj->klass_addr();
  klassRef  klass_ref  = UNPOISON_KLASSREF(*klass_addr);

  assert0( klass_ref.is_old() );

  // TODO: verify this test isn't needed anymore: not relocating the initial PermGen page should
  //       cause the is_remap_trapped() test below to return in this case.
  // if ( klass_ref.as_oop() == obj ) return klass_ref;

  if ( !GPGC_ReadTrapArray::is_remap_trapped(klass_ref) ) return klass_ref;

  // TODO What's the max possible size for a klassRef?  Will one ever be in a mid space block?
  //      What's the right handling for that?

  assert0( GPGC_ReadTrapArray::is_old_gc_remap_trapped(klass_ref) );
  assert0( GPGC_Layout::one_space_page(GPGC_Layout::addr_to_PageNum(klass_ref.as_oop())) );

  oop                    old_klass = klass_ref.as_oop();
  PageNum                page      = GPGC_Layout::addr_to_BasePageForSpace(old_klass);
  GPGC_PageInfo*         info      = GPGC_PageInfo::page_info(page);
  GPGC_ObjectRelocation* forward   = GPGC_PageRelocation::find_object(page, info, old_klass);

  assert0( objectRef::old_space_id == info->relocate_space() );
  assert0( forward && forward->is_relocated() );

  // Dunno why the operator overloading of '=' won't let me assign a heapRef to a klassRef.
  klassRef new_klass( forward->relocated_ref(objectRef::old_space_id, klass_ref).raw_value() );

  assert0( GPGC_NMT::has_desired_old_nmt(klass_ref) == GPGC_NMT::has_desired_old_nmt(new_klass) );
  assert0( GPGC_NMT::has_desired_old_nmt(klass_ref) );

  // Update the klass table with the relocated klass.  No need to CAS.  We might race with another
  // thread, but it'll be writing the same value.
  DEBUG_ONLY( intptr_t raw_value = PERMISSIVE_UNPOISON(*klass_addr,klass_addr).raw_value(); )
  assert0(raw_value==klass_ref.raw_value() || raw_value==new_klass.raw_value());

  *klass_addr = POISON_KLASSREF(new_klass);

  return new_klass;
}


void GPGC_OldCollector::card_mark_pages(GPGC_PopulationArray* array, long work_unit)
{
  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    // Process the pages just claimed:
    for (uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum        page = array->page(cursor);
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

      assert0(info->just_gen()       != GPGC_PageInfo::NewGen);
      assert0(info->relocate_space() == objectRef::old_space_id);

      GPGC_ObjectRelocation* relocations = info->relocations();
      long                   reloc_len   = info->reloc_len();

      for ( long i=0; i<reloc_len; i++ ) {
        uint64_t record = relocations[i].get_record();
        if ( record != 0 ) {
          oop new_obj = GPGC_ObjectRelocation::decode_new_oop(record);
new_obj->GPGC_oldgc_update_cardmark();

          if ( GPGC_OldCollector::should_mark_new_objects_live() ) {
            if ( GPGC_Marks::atomic_mark_live_if_dead(new_obj) ) {
              GPGC_Marks::set_markid(new_obj, 0x4A);
              GPGC_Marks::set_marked_through(new_obj);
            }
          }
        }
      }
    }
  }
}


// Derived oop handling for a OldGC thread cleaning a JavaThread's stack.
void GPGC_OldCollector::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr)
{
  objectRef old_base = *base_ptr;

  if ( ! old_base.is_old() ) {
    // The OldGC threads only handle refs to OldGen when cleaning a thread stack.
    return;
  }

  assert0( Thread::current()->is_GenPauselessGC_thread() || Thread::current()->is_GC_task_thread() );

  if ( ! GPGC_ReadTrapArray::is_remap_trapped(old_base) ) {
    return;
  }

  assert0( GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_base) );

  objectRef new_base = GPGC_Collector::get_forwarded_object(old_base);

  GPGC_Collector::fixup_derived_oop(derived_ptr, old_base, new_base);
}


// This is a debugging method, to make sure we're not relocating a base oop without also
// update an associated derived oop.
void GPGC_OldCollector::ensure_base_not_relocating(objectRef* base_ptr)
{
  objectRef base = *base_ptr;

  if ( ! base.is_old() ) {
    return;
  }

  guarantee( !GPGC_ReadTrapArray::is_remap_trapped(base), "base oop is moving with an ignored derived oop");
}
