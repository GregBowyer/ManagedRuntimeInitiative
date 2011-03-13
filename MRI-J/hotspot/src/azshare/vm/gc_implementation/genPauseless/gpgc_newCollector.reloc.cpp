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
#include "gpgc_collector.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "oop.gpgc.inline.hpp"
#include "os_os.inline.hpp"


void GPGC_NewCollector::sideband_forwarding_init(long work_unit)
{
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->small_space_to_new(), objectRef::new_space_id);
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->small_space_to_old(), objectRef::old_space_id);

  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->mid_space_to_new(), objectRef::new_space_id);
  GPGC_PageRelocation::sideband_forwarding_init(work_unit, page_pops()->mid_space_to_old(), objectRef::old_space_id);
}


void GPGC_NewCollector::remap_to_mirror_for_relocate(GPGC_PopulationArray* relocation_array)
{
  uint64_t max_cursor = relocation_array->max_cursor();

  for ( uint64_t cursor=0; cursor<max_cursor; cursor++ ) {
    PageNum page = relocation_array->page(cursor);
    GPGC_Space::remap_to_mirror(page);
  }
}


void GPGC_NewCollector::relocate_mid_pages(GPGC_RemapBuffer* remap_buffer, long work_unit, GPGC_PopulationArray* array, bool mark_copy, int64_t stripe)
{
  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )

    // Process the pages just claimed:
    for ( uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum page = array->page(cursor);

      GPGC_PageRelocation::gc_relocate_mid_page(page, remap_buffer, mark_copy, stripe);

      DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )
    }
  }
}


void GPGC_NewCollector::relocate_mid_pages(GPGC_GCManagerNewReloc* gcm, long work_unit, int64_t stripe)
{
  bool mark_in_old = GPGC_OldCollector::should_mark_new_objects_live();

  relocate_mid_pages(gcm->new_remap_buffer(), work_unit, page_pops()->mid_space_to_new(), false,       stripe);
  relocate_mid_pages(gcm->old_remap_buffer(), work_unit, page_pops()->mid_space_to_old(), mark_in_old, stripe);
}


//  This is the top-level function for moving pages in memory.
//
//  On stock linux, it should be called from within the relocation safepoint. 
//
//  With azmem, it should be called during relocation setup, and then during
//  the relocation safepoint commit_batched_memory_ops() should be called to
//  put the changes into effect.
void GPGC_NewCollector::remap_memory(TimeDetailTracer* tdt)
{
  if ( BatchedMemoryOps ) {
    // Need to hold the interlock if we're doing batched memory ops.
    assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));
  }

  {
    DetailTracer dt(tdt, false, "N: remap to mirror for %lu small pages", 
                    page_pops()->small_space_to_new()->max_cursor() + page_pops()->small_space_to_old()->max_cursor());

    // Move small space pages we're relocating to a mirror address, so invalid accesses will cause a SEGV.
    // TODO: Parallelize this
    remap_to_mirror_for_relocate(page_pops()->small_space_to_new());
    remap_to_mirror_for_relocate(page_pops()->small_space_to_old());
  }

  {
    DetailTracer dt(tdt, false, "N: remap live objs from %lu mid pages", 
                    page_pops()->mid_space_to_new()->max_cursor() + page_pops()->mid_space_to_old()->max_cursor());

    // Remap live objects in mid space pages we're relocating.
    GPGC_GCManagerNewReloc::reset_remap_buffers();

    long          work_unit = 1;
PGCTaskQueue*q=PGCTaskQueue::create();

    for ( int64_t i=0; i<int64_t(GenPauselessNewThreads); i++ ) {
q->enqueue(new GPGC_NewGC_RelocateMidPagesTask(work_unit,i));
    }

    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);

    page_pops()->mid_space_to_new()->reset_cursor();
    page_pops()->mid_space_to_old()->reset_cursor();
  }

  // Remap large space blocks we're relocating.
  {
    DetailTracer dt(tdt, false, "N: remap live objs from %lu large pages: ", 
                    page_pops()->large_space_to_new()->max_cursor() + page_pops()->large_space_to_old()->max_cursor());
    
    // We currently don't remap large space blocks withing NewGen for degramentation, but we might someday.


    long skipped_blocks = 0;
    long skipped_pages  = 0;

    for ( uint64_t cursor=0; cursor<page_pops()->large_space_to_old()->max_cursor(); cursor++ ) {
      PageNum block  = page_pops()->large_space_to_old()->page(cursor);
      bool    cloned = GPGC_Space::new_gen_promote_block(block);

      if ( ! cloned ) {
        // reset the page state to Allocated
        GPGC_PageInfo* orig_block_info =  GPGC_PageInfo::page_info(block);
        // reset the trap in the read barrier
        orig_block_info->restore_block_state(block);

        // we shouldn't need this
        // page_pops()->skip_block(cursor, block_size);
        long block_size = orig_block_info->block_size();
        skipped_blocks ++;
skipped_pages+=block_size;

        // Don't advance cursor in the for loop when a block is skipped.
        cursor --;
      }
      else if ( GPGC_OldCollector::should_mark_new_objects_live() ) {
        GPGC_PageInfo* info    = GPGC_PageInfo::page_info(block);
        PageNum        clone   = info->ll_next();
        oop            new_obj = (oop) GPGC_Layout::PageNum_to_addr(clone);
        bool           marked  = GPGC_Marks::atomic_mark_live_if_dead(new_obj);
assert(marked,"no one else should have been able to mark a cloned block");
        GPGC_Marks::set_markid(new_obj, 0x4A);
        GPGC_Marks::set_marked_through(new_obj);
      }
    }

dt.print("promoting %d blocks, %d pages; skipped %d blocks, %d pages",
             page_pops()->large_space_to_old()->max_cursor(),
             page_pops()->large_space_to_old()->block_pages_total(),
             skipped_blocks,
             skipped_pages);
  }
}


void GPGC_NewCollector::heal_mid_pages(GPGC_GCManagerNewReloc* gcm, int64_t stripe)
{
  GPGC_RemapTargetArray* mid_space_targets = page_pops()->mid_space_targets();
  uint64_t               max_cursor        = mid_space_targets->current_length();
  PageNum                resource_page     = GPGC_PageBudget::preallocated_page_for_thread();
  long                   target_new_pages  = 0;
  long                   target_old_pages  = 0;

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

      if ( info->just_gen() == GPGC_PageInfo::NewGen ) {
        target_new_pages += pages;
      } else {
        target_old_pages += pages;
      }
    }
  }

  page_pops()->atomic_increment_target_pages_in_new_gen(target_new_pages);
  page_pops()->atomic_increment_target_pages_in_old_gen(target_old_pages);
}


void GPGC_NewCollector::relocate_small_pages(GPGC_RelocBuffer* relocation_buffer, long work_unit, GPGC_PopulationArray* array, bool mark_copy)
{
  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )

    if ( relocation_buffer->_prime.empty() ) {
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(array->page(start_cursor));
      relocation_buffer->get_relocation_page(&relocation_buffer->_prime, info->time());
      relocation_buffer->increment_page_count();
    }

    // Process the pages just claimed:
    for ( uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum page = array->page(cursor);

      GPGC_PageRelocation::gc_relocate_small_page(page, relocation_buffer, mark_copy);
      GPGC_Space::relocated_page(page, relocation_spike());

      DEBUG_ONLY( GPGC_PageBudget::verify_this_thread(); )
    }
  }
}


void GPGC_NewCollector::relocate_small_pages(GPGC_GCManagerNewReloc* gcm, long work_unit)
{
  bool mark_in_old = GPGC_OldCollector::should_mark_new_objects_live();

  relocate_small_pages(gcm->new_relocation_buffer(), work_unit, page_pops()->small_space_to_new(), false);
  relocate_small_pages(gcm->old_relocation_buffer(), work_unit, page_pops()->small_space_to_old(), mark_in_old);
}


void GPGC_NewCollector::card_mark_pages(GPGC_PopulationArray* array, long work_unit)
{
  uint64_t start_cursor;
  uint64_t end_cursor;

  while ( array->atomic_claim_array_chunk(start_cursor, end_cursor, work_unit) ) {
    // Process the pages just claimed:
    for ( uint64_t cursor=start_cursor; cursor<end_cursor; cursor++ ) {
      PageNum        page = array->page(cursor);
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

      assert0(info->just_gen()       == GPGC_PageInfo::NewGen);
      assert0(info->relocate_space() == objectRef::old_space_id);

      GPGC_ObjectRelocation* relocations = info->relocations();
      long                   reloc_len   = info->reloc_len();

      for ( long i=0; i<reloc_len; i++ ) {
        uint64_t record = relocations[i].get_record();
        if ( record != 0 ) {
          oop new_obj = GPGC_ObjectRelocation::decode_new_oop(record);
new_obj->GPGC_newgc_update_cardmark();

          // Object marking now done when the object is copied:
          // if ( GPGC_OldCollector::should_mark_new_objects_live() ) {
          //   if ( GPGC_Marks::atomic_mark_live_if_dead(new_obj) ) {
          //     GPGC_Marks::set_markid(new_obj, 0x4A);
          //     GPGC_Marks::set_marked_through(new_obj);
          //   }
          // }
        }
      }
    }
  }
}


void GPGC_NewCollector::card_mark_pages(long work_unit)
{
  card_mark_pages(page_pops()->small_space_to_old(), work_unit);
  card_mark_pages(page_pops()->mid_space_to_old(), work_unit);
}


// Derived oop handling for a NewGC thread cleaning a JavaThread's stack.
void GPGC_NewCollector::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr)
{
  assert0(GPGC_Collector::is_new_collector_thread(Thread::current()));

  objectRef old_base = *base_ptr;
objectRef new_base;

assert(old_base.not_null(),"Found null base ref for derived pointer");

  if ( ! old_base.is_new() ) {
    // The NewGC threads only handle refs to NewGen when cleaning a thread stack.
    return;
  }

  if ( GPGC_ReadTrapArray::is_remap_trapped(old_base) ) {
    assert0( GPGC_ReadTrapArray::is_new_gc_remap_trapped(old_base) );
    new_base = GPGC_Collector::get_forwarded_object(old_base);
  }
  else {
    return;
  }

  GPGC_Collector::fixup_derived_oop(derived_ptr, old_base, new_base);
}


// Derived oop handling for a NewToOldGC thread cleaning a JavaThread's stack.
void GPGC_NewCollector::NewToOld_do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr)
{
  assert0(GPGC_Collector::is_new_collector_thread(Thread::current()));

  objectRef old_base = *base_ptr;
objectRef new_base;

  assert0(old_base.is_heap());

  if ( GPGC_ReadTrapArray::is_remap_trapped(old_base) ) {
    new_base = GPGC_Collector::get_forwarded_object(old_base);
  }
  else {
    return;
  }

  GPGC_Collector::fixup_derived_oop(derived_ptr, old_base, new_base);
}


// This is a debugging method, to make sure we're not relocating a base oop without also
// updating an associated derived oop.
void GPGC_NewCollector::ensure_base_not_relocating(objectRef* base_ptr)
{
  objectRef base = *base_ptr;

  if ( ! base.is_new() ) {
    return;
  }

  guarantee( ! GPGC_ReadTrapArray::is_remap_trapped(base), "base oop is moving with an ignored derived oop");
}


// This is a debugging method, to make sure we're not relocating a base oop without also
// updating an associated derived oop.
void GPGC_NewCollector::NewToOld_ensure_base_not_relocating(objectRef* base_ptr)
{
  objectRef base = *base_ptr;

  assert0(base.is_heap());

  guarantee( ! GPGC_ReadTrapArray::is_remap_trapped(base), "base oop is moving with an ignored derived oop");
}
