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


#include "gpgc_generation.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_multiPageSpace.hpp"
#include "gpgc_onePageSpace.hpp"
#include "gpgc_pageAudit.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"

#include "gpgc_pageInfo.inline.hpp"
#include "oop.inline.hpp"

GPGC_MultiPageSpace GPGC_Space::_multi_space;
GPGC_OnePageSpace   GPGC_Space::_one_space_small;
GPGC_OnePageSpace   GPGC_Space::_one_space_mid;
GPGC_Generation     GPGC_Space::_new_gen (&GPGC_NewGen_relocation_lock,  &GPGC_NewGen_small_allocation_lock,  &GPGC_NewGen_mid_allocation_lock);
GPGC_Generation     GPGC_Space::_old_gen (&GPGC_OldGen_relocation_lock,  &GPGC_OldGen_small_allocation_lock,  &GPGC_NewGen_mid_allocation_lock);
GPGC_Generation     GPGC_Space::_perm_gen(&GPGC_PermGen_relocation_lock, &GPGC_PermGen_small_allocation_lock, &GPGC_PermGen_mid_allocation_lock);

GPGC_Generation*    GPGC_Space::_gens[33];
long                GPGC_Space::_reserved_waiting;


// Construct a new GPGC_Space.  The virtual address space described in new_gen_region and
// old_gen_region should already have been reserved with the kernel.
void GPGC_Space::initialize(int max_committed_pages)
{
  _multi_space.initialize    (GPGC_Layout::start_of_large_space, GPGC_Layout::end_of_large_space);
  _one_space_small.initialize(GPGC_Layout::start_of_small_space, GPGC_Layout::end_of_small_space, true);
  _one_space_mid.initialize  (GPGC_Layout::start_of_mid_space,   GPGC_Layout::end_of_mid_space,   false);
                          
  _new_gen.initialize (&_one_space_small, &_one_space_mid, &_multi_space, GPGC_PageInfo::NewGen);
  _old_gen.initialize (&_one_space_small, &_one_space_mid, &_multi_space, GPGC_PageInfo::OldGen);
  _perm_gen.initialize(&_one_space_small, &_one_space_mid, &_multi_space, GPGC_PageInfo::PermGen);

  assert0( (sizeof(_gens)/sizeof(GPGC_Generation*)) >= GPGC_PageInfo::InvalidGen );
  assert0( (sizeof(_gens)/sizeof(GPGC_Generation*)) >= GPGC_PageInfo::NewGen     );
  assert0( (sizeof(_gens)/sizeof(GPGC_Generation*)) >= GPGC_PageInfo::OldGen     );
  assert0( (sizeof(_gens)/sizeof(GPGC_Generation*)) >= GPGC_PageInfo::PermGen    );

  memset(_gens, 0, sizeof(_gens));

  _gens[GPGC_PageInfo::InvalidGen] = NULL;
  _gens[GPGC_PageInfo::NewGen    ] = &_new_gen;
  _gens[GPGC_PageInfo::OldGen    ] = &_old_gen;
  _gens[GPGC_PageInfo::PermGen   ] = &_perm_gen;

  _reserved_waiting = 0;

  GPGC_PageBudget::initialize(max_committed_pages);

  GPGC_PageInfo::initialize_info_array();
  GPGC_PageAudit::initialize_page_audit();
}


HeapWord* GPGC_Space::new_gen_allocate(size_t word_size, bool is_tlab)
{
  return _new_gen.allocate(word_size, is_tlab);
}
HeapWord* GPGC_Space::old_gen_allocate(size_t word_size)
{
  return _old_gen.allocate(word_size, false);
}
HeapWord* GPGC_Space::perm_gen_allocate(size_t word_size)
{
  return _perm_gen.allocate(word_size, false);
}


PageNum GPGC_Space::alloc_small_relocation_page(GPGC_PageInfo::Gens generation, GPGC_PageInfo::Gens source_gen)
{
  assert0(generation==GPGC_PageInfo::NewGen || generation==GPGC_PageInfo::OldGen || generation==GPGC_PageInfo::PermGen);
  assert0(source_gen==GPGC_PageInfo::NewGen || source_gen==GPGC_PageInfo::OldGen || source_gen==GPGC_PageInfo::PermGen);

  return _gens[generation]->allocate_small_relocation_page(_gens[source_gen], GPGC_Generation::UsePreAllocated, PageAuditWhy(0x3000|generation));
}
PageNum GPGC_Space::alloc_mid_remapping_page(GPGC_PageInfo::Gens generation, long source_space_id)
{
  assert0(generation==GPGC_PageInfo::NewGen || generation==GPGC_PageInfo::OldGen || generation==GPGC_PageInfo::PermGen);

  return _gens[generation]->allocate_mid_remapping_page(source_space_id);
}


void GPGC_Space::pin_page(PageNum page)
{
  if ( GPGC_Layout::large_space_page(page) ) {
    _multi_space.pin_block(page);
  }
  else if ( GPGC_Layout::mid_space_page(page) ) {
    _one_space_mid.pin_page(page);
  }
  else {
fatal("Pinning small space pages not supported.");
  }
}
void GPGC_Space::unpin_page(PageNum page)
{
  if ( GPGC_Layout::large_space_page(page) ) {
    _multi_space.unpin_block(page);
  }
  else if ( GPGC_Layout::mid_space_page(page) ) {
    _one_space_mid.unpin_page(page);
  }
  else {
fatal("Pinning small space pages not supported.");
  }
}


bool GPGC_Space::mutator_heal_mid_page(PageNum page, char* force_addr)
{
  assert0( GPGC_Layout::mid_space_page(page) );

  PageNum             base_page         = GPGC_Layout::page_to_MidSpaceBasePageNum(page);
  GPGC_PageInfo*      base_info         = GPGC_PageInfo::page_info_unchecked(base_page);
  GPGC_PageInfo::Gens target_generation = base_info->just_gen();

  bool result = _gens[target_generation]->mutator_heal_mid_page(page, force_addr);

  return result;
}

void GPGC_Space::heal_mid_remapping_page(PageNum page, GPGC_PageInfo::Gens source_generation, PageNum resource_page,
                                         GPGC_RelocationSpike* relocation_spike)
{
  assert0(source_generation==GPGC_PageInfo::NewGen || source_generation==GPGC_PageInfo::OldGen || source_generation==GPGC_PageInfo::PermGen);

  _gens[source_generation]->heal_mid_remapping_page(page, resource_page, relocation_spike);
}


HeapWord* GPGC_Space::new_gen_allocate_for_relocate(GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time)
{
  assert0(source_generation == GPGC_PageInfo::NewGen);
  return _new_gen.allocate_for_relocate(&_new_gen, word_size, page_time);
}
HeapWord* GPGC_Space::old_gen_allocate_for_relocate(GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time)
{
  assert0(source_generation==GPGC_PageInfo::NewGen || source_generation==GPGC_PageInfo::OldGen);
  return _old_gen.allocate_for_relocate(_gens[source_generation], word_size, page_time);
}
HeapWord* GPGC_Space::perm_gen_allocate_for_relocate(GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time)
{
  assert0(source_generation == GPGC_PageInfo::PermGen);
  return _perm_gen.allocate_for_relocate(&_perm_gen, word_size, page_time);
}


long GPGC_Space::new_gen_relocation_page_count()
{
  return _new_gen.relocation_page_count();
}
long GPGC_Space::old_gen_relocation_page_count()
{
  return _old_gen.relocation_page_count();
}
long GPGC_Space::old_gen_promotion_page_count()
{
  return _old_gen.promotion_page_count();
}
long GPGC_Space::perm_gen_relocation_page_count()
{
  return _perm_gen.relocation_page_count();
}


bool GPGC_Space::new_gen_promote_block(PageNum block)
{
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();
  PageNum        clone = _old_gen.clone_block(block, pages);

  // Note: clone might be NoPage, which means there's too much fragmentation to clone the block right now.
  info->set_ll_next( clone );

  return clone != NoPage;
}


void GPGC_Space::remap_to_mirror(PageNum page)
{
  assert0(GPGC_Layout::small_space_page(page));

  // Move the physical memory of the page to it's mirror location.

  PageNum mirror_page = page + GPGC_Layout::heap_mirror_offset;

  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::relocate_memory);
    os::batched_relocate_memory((char*)GPGC_Layout::PageNum_to_addr(page),
                                (char*)GPGC_Layout::PageNum_to_addr(mirror_page),
                                BytesPerGPGCPage);
  }

  GPGC_PageInfo*      page_info = GPGC_PageInfo::page_info(page);
  GPGC_PageInfo::Gens gen       = page_info->just_gen();

  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0x3200|gen), GPGC_PageAudit::RemapToMirror);

  if (GPGCTracePageSpace) {
    gclog_or_tty->print_cr("Moving page 0x%lX to mirror page 0x%lX in gen %d", page, mirror_page, gen);
  }
}


void GPGC_Space::object_iterate(ObjectClosure* cl)
{
  _new_gen.object_iterate(cl);
  _old_gen.object_iterate(cl);
  _perm_gen.object_iterate(cl);
}

void GPGC_Space::perm_gen_object_iterate(ObjectClosure* cl)
{
  _perm_gen.object_iterate(cl);
}
void GPGC_Space::old_gen_object_iterate(ObjectClosure* cl)
{
  _old_gen.object_iterate(cl);
}


void GPGC_Space::setup_read_trap_array(PGCTaskManager* manager, uint64_t tasks)
{
PGCTaskQueue*q=PGCTaskQueue::create();

  _one_space_small.create_read_trap_array_tasks(q, tasks);
  _one_space_mid.create_read_trap_array_tasks(q, tasks);

  GPGC_Collector::run_task_queue(manager, q);

  // TODO: this step can be skipped during old gen relocation
  _multi_space.setup_read_trap_array();
}


// Tell the GPGC_Space that GC object marking is beginning.  This
// causes the GPGC_Space to cease allocating from any current partially
// allocated pages, and to track free pages that are allocated from
// during the GC marking process.
void GPGC_Space::new_gc_clear_allocation_buffers()
{
  _new_gen.clear_allocation_buffers();
}

void GPGC_Space::new_gc_set_marking_underway()
{
  _new_gen.set_marking(true);
}

void GPGC_Space::old_gc_clear_allocation_buffers()
{
  _old_gen.clear_allocation_buffers();
  _perm_gen.clear_allocation_buffers();
}

void GPGC_Space::old_gc_set_marking_underway()
{
  _old_gen.set_marking (true);
  _perm_gen.set_marking(true);
}

void GPGC_Space::new_gc_flush_relocation(DetailTracer* dt)
{
  _new_gen.flush_relocation_pages(dt);
  _old_gen.flush_promotion_pages(dt);

  // TODO: maw: might need to sweep through and clear NoRelocateFlag.
}

void GPGC_Space::old_gc_flush_relocation(DetailTracer* dt)
{
  _old_gen.flush_relocation_pages(dt);
  _perm_gen.flush_relocation_pages(dt);
}

// Notifies the GPGC_Space that the GC's marking process has completed.
void GPGC_Space::new_gc_stop_marking()
{
  // Record that marking is no longer in progress.
  _new_gen.set_marking(false);
}

// Notifies the GPGC_Space that the GC's marking process has completed.
void GPGC_Space::old_gc_stop_marking()
{
  // Record that marking is no longer in progress.
  _old_gen.set_marking(false);
  _perm_gen.set_marking(false);
}


// Sweep the pages in the space and unset the NoRelocateFlag.
void GPGC_Space::new_gc_clear_no_relocate()
{
  _one_space_small.clear_no_relocate(GPGC_PageInfo::NewGenMask);
  _one_space_mid.clear_no_relocate  (GPGC_PageInfo::NewGenMask);
  _multi_space.clear_no_relocate    (GPGC_PageInfo::NewGenMask);
}


// Sweep the pages in the space and unset the NoRelocateFlag.
void GPGC_Space::old_gc_clear_no_relocate()
{
  _one_space_small.clear_no_relocate(GPGC_PageInfo::OldAndPermMask);
  _one_space_mid.clear_no_relocate  (GPGC_PageInfo::OldAndPermMask);
  _multi_space.clear_no_relocate    (GPGC_PageInfo::OldAndPermMask);
}


void GPGC_Space::new_gc_sum_raw_stats(uint64_t* obj_count_result, uint64_t* word_count_result)
{
  *obj_count_result  = 0;
  *word_count_result = 0;

  uint64_t object_count;
  uint64_t word_count;

  _one_space_small.sum_raw_stats(GPGC_PageInfo::NewGenMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;

  _one_space_mid.sum_raw_stats  (GPGC_PageInfo::NewGenMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;

  _multi_space.sum_raw_stats    (GPGC_PageInfo::NewGenMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;
}

void GPGC_Space::old_gc_sum_raw_stats(uint64_t* obj_count_result, uint64_t* word_count_result)
{
  *obj_count_result  = 0;
  *word_count_result = 0;

  uint64_t object_count;
  uint64_t word_count;

  _one_space_small.sum_raw_stats(GPGC_PageInfo::OldAndPermMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;

  _one_space_mid.sum_raw_stats  (GPGC_PageInfo::OldAndPermMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;

  _multi_space.sum_raw_stats    (GPGC_PageInfo::OldAndPermMask, &object_count, &word_count);
  *obj_count_result  += object_count;
  *word_count_result += word_count;
}


// Create a PGCTask for scanning the cardmarks on each OldGen and PermGen page.
// Return the number of tasks (and thus pages) that must be scanned.
long GPGC_Space::make_cardmark_tasks(PGCTaskQueue* q)
{
  long count;

  // Make the multi-space tasks first.
  count = _multi_space.make_cardmark_tasks(q);

  // enqueue stipped tasks for one-page-space
for(uint i=0;i<GenPauselessNewThreads;i++){
    q->enqueue( new GPGC_OnePageScanCardMarksTask(i,GenPauselessNewThreads) );
  }

  return count;
}


// JVMTI Heap Iteration support
void GPGC_Space::make_heap_iteration_tasks(long sections, ObjectClosure* closure, PGCTaskQueue* q)
{
  // Make the multi-space tasks first, since they might take a very long time.
  // TODO: make multiple tasks for multipage arrays.
  _multi_space.make_heap_iteration_tasks(closure, q);

  // enqueue striped tasks for one-page-space
  _one_space_small.make_heap_iteration_tasks(sections, closure, q);
  _one_space_mid.make_heap_iteration_tasks(sections, closure, q);
}


// Prepare for a call to os::tlb_resync().
void GPGC_Space::prepare_for_TLB_resync()
{
  _one_space_small.prepare_for_TLB_resync();
  _one_space_mid.prepare_for_TLB_resync();
  _multi_space.prepare_for_TLB_resync();
}


// After a call to os::tlb_resync().
void GPGC_Space::TLB_resync_occurred()
{
  _one_space_small.TLB_resync_occurred();
  _one_space_mid.TLB_resync_occurred();
  _multi_space.TLB_resync_occurred();
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::new_gc_clear_page_marks()
{
  _one_space_small.clear_page_marks  (GPGC_PageInfo::NewGenMask, 0, 1);
  _one_space_mid.clear_page_marks  (GPGC_PageInfo::NewGenMask, 0, 1);
  _multi_space.clear_page_marks(GPGC_PageInfo::NewGenMask);
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::new_gc_clear_one_space_marks(long section, long sections)
{
  _one_space_small.clear_page_marks  (GPGC_PageInfo::NewGenMask, section, sections);
  _one_space_mid.clear_page_marks  (GPGC_PageInfo::NewGenMask, section, sections);
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::new_gc_clear_multi_space_marks()
{
  _multi_space.clear_page_marks(GPGC_PageInfo::NewGenMask);
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::old_gc_clear_page_marks()
{
  _one_space_small.clear_page_marks  (GPGC_PageInfo::OldAndPermMask, 0, 1);
  _one_space_mid.clear_page_marks  (GPGC_PageInfo::OldAndPermMask, 0, 1);
  _multi_space.clear_page_marks(GPGC_PageInfo::OldAndPermMask);
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::old_gc_clear_one_space_marks(long section, long sections)
{
  _one_space_small.clear_page_marks  (GPGC_PageInfo::OldAndPermMask, section, sections);
  _one_space_mid.clear_page_marks  (GPGC_PageInfo::OldAndPermMask, section, sections);
}


// Sweep the heap, and clear the mark bits for each allocated page.
void GPGC_Space::old_gc_clear_multi_space_marks()
{
  _multi_space.clear_page_marks(GPGC_PageInfo::OldAndPermMask);
}


void GPGC_Space::clear_verify_marks()
{
  _one_space_small.clear_verify_marks();
  _one_space_mid.clear_verify_marks();
  _multi_space.clear_verify_marks();
}


// Sweep the heap, verify that there are no objects marked live.
void GPGC_Space::new_gc_verify_no_live_marks()
{
  _one_space_small.verify_no_live_marks(GPGC_PageInfo::NewGenMask);
  _one_space_mid.verify_no_live_marks  (GPGC_PageInfo::NewGenMask);
  _multi_space.verify_no_live_marks    (GPGC_PageInfo::NewGenMask);
}


// Sweep the heap, verify that there are no objects marked live.
void GPGC_Space::new_and_old_gc_verify_no_live_marks()
{
  _one_space_small.verify_no_live_marks(GPGC_PageInfo::NewGenMask|GPGC_PageInfo::OldAndPermMask);
  _one_space_mid.verify_no_live_marks  (GPGC_PageInfo::NewGenMask|GPGC_PageInfo::OldAndPermMask);
  _multi_space.verify_no_live_marks    (GPGC_PageInfo::NewGenMask|GPGC_PageInfo::OldAndPermMask);
}


// This function won't work reliably unless all TLBs are filled before it's called.
// It also isn't very MT-safe.  Because of that, it's only supported for debug builds.
HeapWord* GPGC_Space::block_start(const void* addr)
{
  if ( ! is_in(addr) ) {
    // Can't call block_start() on something outside the object heap.
    ShouldNotReachHere();
  }

  PageNum page = GPGC_Layout::addr_to_PageNum(addr);

  if ( ! GPGC_PageInfo::info_exists(page) ) {
    // Shouldn't be calling block_start() on something that could never have been
    // a valid allocated page.
return NULL;//ShouldNotReachHere();
  }

  GPGC_PageInfo*      info   = GPGC_PageInfo::page_info(page);
  GPGC_PageInfo::Gens gen    = info->just_gen();

  if ( _gens[gen] == NULL ) {
    // Shouldn't be calling block_start() on something that's not a current
    // page in a generation.
return NULL;//ShouldNotReachHere
  }

return _gens[gen]->block_start(addr);
}


void GPGC_Space::newgc_page_and_frag_words_count(long* small_pages     , long* mid_pages     , long* large_pages,
                                                 long* small_frag_words, long* mid_frag_words, long* large_frag_words)
{
  _one_space_small.page_and_frag_words_count(GPGC_PageInfo::NewGenMask, small_pages, small_frag_words);
  _one_space_mid  .page_and_frag_words_count(GPGC_PageInfo::NewGenMask, mid_pages,   mid_frag_words);
  _multi_space    .page_and_frag_words_count(GPGC_PageInfo::NewGenMask, large_pages, large_frag_words);
}


void GPGC_Space::oldgc_page_and_frag_words_count(long* small_pages     , long* mid_pages     , long* large_pages,
                                                 long* small_frag_words, long* mid_frag_words, long* large_frag_words)
{
  _one_space_small.page_and_frag_words_count(GPGC_PageInfo::OldAndPermMask, small_pages, small_frag_words);
  _one_space_mid  .page_and_frag_words_count(GPGC_PageInfo::OldAndPermMask, mid_pages,   mid_frag_words);
  _multi_space    .page_and_frag_words_count(GPGC_PageInfo::OldAndPermMask, large_pages, large_frag_words);
}


void GPGC_Space::new_gc_collect_sparse_populations(int64_t promotion_threshold_time, GPGC_Population* page_pops)
{
  assert0(page_pops->all_populations()->is_empty());
  assert0(page_pops->small_space_to_new()->is_empty());
  assert0(page_pops->small_space_to_old()->is_empty());
  assert0(page_pops->mid_space_to_new()->is_empty());
  assert0(page_pops->mid_space_to_old()->is_empty());
  assert0(page_pops->large_space_to_new()->is_empty());
  assert0(page_pops->large_space_to_old()->is_empty());

  // Collect the OneSpace sparse and promotable pages.
  _one_space_small.new_gc_collect_sparse_populations(promotion_threshold_time, page_pops->all_populations(), page_pops->small_space_skipped_pages());
  _one_space_mid.new_gc_collect_sparse_populations  (promotion_threshold_time, page_pops->all_populations(), page_pops->mid_space_skipped_pages());

  page_pops->all_populations()->sort_by_new_gc_relocation_priority();

  // Collect dead and promotable multi-page objects:
  _multi_space.new_gc_collect_blocks(promotion_threshold_time, page_pops->large_space_to_new(), page_pops->large_space_to_old());

  page_pops->reset_max_cursor();

#ifdef ASSERT
  // We should only being seeing pages in the Allocated state:
  page_pops->all_populations()->assert_only_allocated_pages(); 
#endif //ASSERT
}


void GPGC_Space::old_gc_collect_sparse_populations(uint64_t fragment_mask, uint64_t fragment_stripe, GPGC_Population* page_pops)
{
  assert0(page_pops->all_populations()->is_empty());
  assert0(page_pops->small_space_in_old()->is_empty());
  assert0(page_pops->small_space_in_perm()->is_empty());
  assert0(page_pops->mid_space_in_old()->is_empty());
  assert0(page_pops->mid_space_in_perm()->is_empty());
  assert0(page_pops->large_space_in_old()->is_empty());
  assert0(page_pops->large_space_in_perm()->is_empty());

  // Collect the sparse pages.
  _one_space_small.old_gc_collect_sparse_populations(fragment_mask, fragment_stripe, page_pops->all_populations(), page_pops->small_space_skipped_pages());
  _one_space_mid.old_gc_collect_sparse_populations  (fragment_mask, fragment_stripe, page_pops->all_populations(), page_pops->mid_space_skipped_pages());

  page_pops->all_populations()->sort_by_old_gc_relocation_priority();

  // Collect dead multi-page objects:
  _multi_space.old_gc_collect_blocks(page_pops->large_space_in_old(), page_pops->large_space_in_perm());

  page_pops->reset_max_cursor();

#ifdef ASSERT
  // We should only being seeing pages in the Allocated state:
  page_pops->all_populations()->assert_only_allocated_pages();
#endif //ASSERT
}


void GPGC_Space::new_gc_relocating_page(PageNum page)
{
  GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
  uint64_t              gen_n_state = info->gen_and_state();
  GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
  GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

  assert0(state == GPGC_PageInfo::Allocated);
  assert0(gen   == GPGC_PageInfo::NewGen);

  info->set_just_state(GPGC_PageInfo::Relocating);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocating page 0x%lX in gen %d", page, info->just_gen());
}


void GPGC_Space::old_gc_relocating_page(PageNum page)
{
  GPGC_PageInfo*        info = GPGC_PageInfo::page_info(page);
  uint64_t              gen_n_state = info->gen_and_state();
  GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
  GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);
  
  assert0(state == GPGC_PageInfo::Allocated);
  assert0(gen   != GPGC_PageInfo::NewGen);

  info->set_just_state(GPGC_PageInfo::Relocating);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocating page 0x%lX in gen %d", page, info->just_gen());
}


void GPGC_Space::new_gc_relocated_block(PageNum block)
{
  GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(info->just_gen()   == GPGC_PageInfo::NewGen);
  assert0(info->ll_next()    != NoPage);

  _new_gen.new_gc_relocated_block(block);
}


void GPGC_Space::old_gc_relocated_block(PageNum block)
{
  GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(info->ll_next() != NoPage);

  if ( info->just_gen() == GPGC_PageInfo::OldGen ) {
    _old_gen.old_gc_relocated_block(block);
  } else {
    assert0(info->just_gen() == GPGC_PageInfo::PermGen);
    _perm_gen.old_gc_relocated_block(block);
  }
}


void GPGC_Space::relocated_page(PageNum page, GPGC_RelocationSpike* relocation_spike)
{
  GPGC_PageInfo*   info = GPGC_PageInfo::page_info(page);
  GPGC_Generation* gen  = _gens[info->just_gen()];

  gen->relocated_page(page, relocation_spike);
}


void GPGC_Space::remapped_page(PageNum page)
{
  GPGC_PageInfo*   info = GPGC_PageInfo::page_info(page);
  GPGC_Generation* gen  = _gens[info->just_gen()];

  gen->remapped_page(page);
}


void GPGC_Space::remapped_pages(GPGC_PopulationArray* array)
{
  uint64_t max_cursor = array->max_cursor();

  for ( uint64_t cursor=0; cursor<max_cursor; cursor++ ) {
    GPGC_Space::remapped_page(array->page(cursor));
  }
}


long GPGC_Space::new_gc_release_relocated_pages()
{
  return _new_gen.release_relocated_pages();
}


void GPGC_Space::new_gc_release_relocated_block(PageNum block)
{
  _new_gen.release_relocated_block(block);
}


long GPGC_Space::old_gc_release_relocated_pages()
{
  return _old_gen.release_relocated_pages() + _perm_gen.release_relocated_pages();
}


void GPGC_Space::release_empty_page(PageNum page, GPGC_RelocationSpike* relocation_spike)
{
  GPGC_PageInfo*   info = GPGC_PageInfo::page_info(page);
  GPGC_Generation* gen  = _gens[info->just_gen()];

  gen->release_empty_page(page, relocation_spike);
}


void GPGC_Space::release_empty_block(PageNum block, GPGC_RelocationSpike* relocation_spike)
{
  GPGC_PageInfo*   info = GPGC_PageInfo::page_info(block);
  GPGC_Generation* gen  = _gens[info->just_gen()];

  gen->release_empty_block(block, relocation_spike);
}
