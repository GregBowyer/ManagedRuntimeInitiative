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
#include "gpgc_cardTable.hpp"
#include "gpgc_generation.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_multiPageSpace.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_onePageSpace.hpp"
#include "gpgc_pageAudit.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_safepoint.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"


// A note about relocation spike: Although GPGC can relocate pages with no net new pages using
// its hand-over-hand relocation algorithm, in practice the collector does consume extra pages
// during relocation.  This is because mutator access to unrelocated objects triggers allocation
// for the relocated object, without being able immediately deallocate the original page.  Once
// relocation is complete, all the source pages will have been deallocated, but for a short time
// relocation causes memory in use to spike upwards.  We want to track the size of that spike,
// and do so with the following approach:
//
// We maintain a counter with atomic ops.
// 
// On page allocation:
//  - When a mutator causes a relocation page to be allocated, the counter is incremented.
//  - Collector's don't increment when allocating relocation pages the preallocated memory:
//    we're only tracking net memory increase.
//
// On page deallocation:
//  - When a page is deallocated to the kernel, the counter is decremented.
//  - When a page is deallocated to preallocated memory, the counter is not decremented.
//
// After each increment to the counter, we compare the counter to a separate peak counter
// variable, and CAS in a new peak if we've seen a new high value.


// The real initialization is done in the initialize() method.
GPGC_Generation::GPGC_Generation( AzLock* rl, AzLock* small_al, AzLock* mid_al )
  : _relocation_lock(rl), _small_shared_allocation_lock(small_al), _mid_shared_allocation_lock(mid_al)
{
  _generation                    = GPGC_PageInfo::InvalidGen;
_one_page_small=NULL;
_one_page_mid=NULL;
_multi_page=NULL;
  _marking_underway              = false;
  _pages_allocated               = 0;
  _small_space_page_capacity     = 0;
  _mid_space_page_capacity       = 0;
  _large_space_page_capacity     = 0;
  _relocated_pages               = NoPage;
  _small_space_allocation_page   = NoPage;
  _mid_space_allocation_page     = NoPage;
  _full_small_space_alloc_pages  = NoPage;
  _full_mid_space_alloc_pages    = NoPage;
}


void GPGC_RelocationPages::initialize(int lock_rank)
{
  _top_slot   = 1;
  _pages[0]   = NoPage;
  _locks[0]   = new AzLock(lock_rank, "GPGC_RelocationPages_slot_lock", false);
  _page_count = 0;
}


void GPGC_RelocationPages::expand(AzLock* expansion_lock, long old_top_slot)
{
  if ( old_top_slot==_top_slot && old_top_slot<_max_pages ) {
    MutexLocker ml(*expansion_lock);

    if ( old_top_slot==_top_slot ) {
      _pages[old_top_slot] = NoPage; 
      _locks[old_top_slot] = new AzLock(expansion_lock->_rank, "GPGC_RelocationPages_slot_lock", false);

      Atomic::write_barrier();

      _top_slot = old_top_slot + 1;
    }
  }
}


void GPGC_Generation::initialize(GPGC_OnePageSpace* one_page_small, GPGC_OnePageSpace* one_page_mid, GPGC_MultiPageSpace* multi_page, GPGC_PageInfo::Gens generation)
{
  _generation = generation;

  _one_page_small = one_page_small;
  _one_page_mid   = one_page_mid;
  _multi_page     = multi_page;

  _marking_underway = false;

  _pages_allocated               = 0;
  _small_space_page_capacity     = 0;
  _mid_space_page_capacity       = 0;
  // gclog_or_tty->print_cr("gen %d: init _mid_space_page_capacity = 0\n", _generation);
  _large_space_page_capacity     = 0;

  _relocated_pages               = NoPage;
  _small_space_allocation_page   = NoPage;
  _mid_space_allocation_page     = NoPage;

  _relocation_pages.initialize(_relocation_lock->_rank);
  _promotion_pages.initialize(_relocation_lock->_rank);
}


inline void GPGC_Generation::atomic_stat_increment(intptr_t* statistic)
{
  assert((*statistic) >= 0, "Statistic increment underflow.");

  Atomic::inc_ptr( statistic );

  assert((*statistic) >= 0, "Statistic increment wrap around.");
}

inline void GPGC_Generation::atomic_stat_decrement(intptr_t* statistic)
{
  assert((*statistic) >= 0, "Statistic decrement already underflowed.");

  Atomic::dec_ptr( statistic );

  assert((*statistic) >= 0, "Statistic decrment underflow.");
}

inline void GPGC_Generation::atomic_stat_add(intptr_t amount, intptr_t* statistic)
{
assert(amount>0,"Only add positive values to accounting statistics");
  assert((*statistic) >= 0, "Statistic add already underflowed.");

  Atomic::add_ptr( amount, statistic );

  assert((*statistic) >= 0, "Statistic add wrap around.");
}

inline void GPGC_Generation::atomic_stat_subtract(intptr_t amount, intptr_t* statistic)
{
assert(amount>0,"Only subtract positive values from accounting statistics");
  assert((*statistic) >= 0, "Statistic subtract already underflowed.");

  Atomic::add_ptr( -1*amount, statistic );

  assert((*statistic) >= 0, "Statistic subtract underflow.");
}


HeapWord* GPGC_Generation::allocate(size_t words, bool is_tlab)
{
  if ( words == (size_t)WordsPerGPGCPage ) {
    return allocate_full_page_small(is_tlab);
  }

assert(!is_tlab,"TLAB's must be exactly one page in size");
    
  // words < 256KB, words < 2MB 
  if ( words < (size_t)GPGC_Layout::mid_space_min_object_words() ) {
    // allocate in small-space
    return allocate_sub_page_small(words);
  }
  else if ( words < (size_t)GPGC_Layout::large_space_min_object_words() ) {
    // allocate in mid-space
    return allocate_sub_page_mid(words);
  }
  else {
    return allocate_multi_page(words);
  }
}


bool GPGC_Generation::allocate_mid_space_sub_pages(HeapWord* current_top, HeapWord* new_top)
{
  assert_lock_strong(*_mid_shared_allocation_lock);

  // Do any incremental 2MB page allocation needed to extend a mid space page block to cover a new allocation.
  PageNum first_page = GPGC_Layout::addr_to_PageNum(current_top-1);
  PageNum last_page  = GPGC_Layout::addr_to_PageNum(new_top-1);

  if ( first_page != last_page ) {
    long pages_alloced = last_page - first_page;

    if ( ! GPGC_PageBudget::allocate_pages(first_page+1, pages_alloced) ) {
      for ( PageNum alloc_page=first_page+1; alloc_page<=last_page; alloc_page++ ) {
        GPGC_PageAudit::audit_entry(alloc_page, PageAuditWhy(0x100|_generation), GPGC_PageAudit::FailedAlloc);
      }
      return false;
    }

    for ( PageNum alloc_page=first_page+1; alloc_page<=last_page; alloc_page++ ) {
      GPGC_PageAudit::audit_entry(alloc_page, PageAuditWhy(0x200|_generation), GPGC_PageAudit::AllocPage);
    }

    atomic_stat_add(pages_alloced, &_pages_allocated);
    atomic_stat_add(pages_alloced, &_mid_space_page_capacity);
    // gclog_or_tty->print_cr("gen %d: alloc mid space sub pages at 0x%lX +%ld to _mid_space_page_capacity\n", _generation, first_page+1, pages_alloced);
  }

  return true;
}


HeapWord* GPGC_Generation::allocate_in_new_small_page(size_t words)
{
  assert0(words>0);

  PageNum new_page = select_and_commit_small_page(CountForAllocRate, DontUsePreAllocated, PageAuditWhy(0x300|_generation));
  if ( NoPage == new_page ) { return NULL; }

  // When we get here, possible failure points are behind us.

  uint64_t       flags      = _marking_underway ? GPGC_PageInfo::NoRelocateFlag : 0;
  HeapWord*      alloc      = GPGC_Layout::PageNum_to_addr(new_page);
  HeapWord*      top        = alloc + words;
  GPGC_PageInfo* info       = GPGC_PageInfo::page_info(new_page);

  info->zero_raw_stats      ();
  info->set_flags_non_atomic(flags);  // No other thread will see this page until set_gen_and_state() below.
  info->set_time            (0);
  info->set_top             (top);

  // clear entry in read-trap-array
  bool is_new_gen_page = (generation() == GPGC_PageInfo::NewGen);
  bool in_large_space  = false;
  GPGC_ReadTrapArray::init_trap(new_page, 1, is_new_gen_page, in_large_space);

  Atomic::write_barrier();

  info->set_gen_and_state(_generation, GPGC_PageInfo::Allocating);

  // Shut down old shared allocation page.
  PageNum old_page = small_space_allocation_page();
  if ( old_page != NoPage ) {
    // If there's an old allocation page, there may be other threads racing to allocate
    // into it as we're changing to a new allocation page here.  And those allocations
    // might succeed, if they're allocating a smaller object than the one this thread
    // tried to allocate.  So we timestamp the page, but don't otherwise alter it.
    GPGC_PageInfo* old_info = GPGC_PageInfo::page_info(old_page);
    old_info->set_time(os::elapsed_counter());

    old_info->set_ll_next(_full_small_space_alloc_pages);
    _full_small_space_alloc_pages = old_page;
  }

  // Set the new page as the current allocation buffer.
  set_small_space_allocation_page(new_page);

  return alloc;
}


HeapWord* GPGC_Generation::allocate_in_new_mid_page(size_t words)
{
  assert0( intptr_t(words) == align_size_up(words, GPGC_Layout::mid_space_object_size_alignment_word_size()) );

  PageNum new_page = select_and_commit_mid_page(words, PageAuditWhy(0x400|_generation));
  if ( NoPage == new_page ) { return NULL; }

  // When we get here, possible failure points are behind us.

  uint64_t       flags      = _marking_underway ? GPGC_PageInfo::NoRelocateFlag : 0;
  HeapWord*      alloc      = GPGC_Layout::PageNum_to_addr(new_page);
  HeapWord*      top        = alloc + words;
  GPGC_PageInfo* info       = GPGC_PageInfo::page_info(new_page);

  info->zero_raw_stats      ();
  info->set_flags_non_atomic(flags);  // No other thread will see this page until set_gen_and_state() below.
  info->set_time            (0);
  info->set_top             (top);

  // clear entry in read-trap-array
  bool is_new_gen_page = (generation() == GPGC_PageInfo::NewGen);
  bool in_large_space  = false;
  GPGC_ReadTrapArray::init_trap(new_page, PagesPerMidSpaceBlock, is_new_gen_page, in_large_space);

  Atomic::write_barrier();

  info->set_gen_and_state(_generation, GPGC_PageInfo::Allocating);

  // Shut down old page.
  PageNum old_page = mid_space_allocation_page();
  if ( old_page != NoPage ) {
    // If there's an old allocation page, there may be other threads racing to allocate
    // into it as we're changing to a new allocation page here.  And those allocations
    // might succeed, if they're allocating a smaller object than the one this thread
    // tried to allocate.  So we timestamp the page, but don't otherwise alter it.
    GPGC_PageInfo* old_info = GPGC_PageInfo::page_info(old_page);
    old_info->set_time(os::elapsed_counter());

    old_info->set_ll_next(_full_mid_space_alloc_pages);
    _full_mid_space_alloc_pages = old_page;
  }

  // Set the new page as the current allocation buffer.
  set_mid_space_allocation_page(new_page);

  return alloc;
}


HeapWord* GPGC_Generation::allocate_sub_page_small(size_t words)
{
  PageNum page = small_space_allocation_page();

  if ( NoPage == page ) {
    // No current allocation buffer for the generation in the space needed.
    MutexLocker ml(*_small_shared_allocation_lock);
    page = small_space_allocation_page();
    if ( NoPage == page ) {
      return allocate_in_new_small_page(words);
    }
  }

  HeapWord*      page_end = GPGC_Layout::PageNum_to_addr(page+1);
  GPGC_PageInfo* info     = GPGC_PageInfo::page_info(page);
HeapWord*result=NULL;
HeapWord*new_top=NULL;

  while (true) {
    result  = info->top();
    new_top = result + words;

    if ( new_top <= page_end ) {
      if ( (intptr_t)result == Atomic::cmpxchg_ptr((intptr_t)new_top, (intptr_t*)info->top_addr(), (intptr_t)result) ) {
        return result;
      }
    } else {
      {
        // Current page is full.  Close it out and get a new one.
        MutexLocker ml(*_small_shared_allocation_lock);
        PageNum old_page = page;
        page = small_space_allocation_page();
        if ( page == old_page ) {
          result = allocate_in_new_small_page(words);
          return result;
        }
      }

      info     = GPGC_PageInfo::page_info(page);
      page_end = GPGC_Layout::PageNum_to_addr(page+1);
    }
  }

  ShouldNotReachHere();
  return NULL;
}


HeapWord* GPGC_Generation::allocate_sub_page_mid(size_t words)
{
  MutexLocker ml(*_mid_shared_allocation_lock);

  long      alloc_words = align_size_up(words, GPGC_Layout::mid_space_object_size_alignment_word_size());
  PageNum   page        = mid_space_allocation_page();
  HeapWord* page_end    = GPGC_Layout::PageNum_to_addr(page+PagesPerMidSpaceBlock);

  if ( NoPage == page ) {
    // No current allocation buffer for the generation in the space needed.
HeapWord*result=allocate_in_new_mid_page(alloc_words);
    return result;
  }

  GPGC_PageInfo* info        = GPGC_PageInfo::page_info(page);
HeapWord*current_top=info->top();
HeapWord*new_top=current_top+alloc_words;

  if ( new_top > page_end ) {
HeapWord*result=allocate_in_new_mid_page(alloc_words);
    return result;
  }

  if ( ! allocate_mid_space_sub_pages(current_top, new_top) ) {
    return NULL;
  }

info->set_top(new_top);

  return current_top;
}


PageNum GPGC_Generation::select_and_commit_small_page(bool count_for_alloc_rate, bool use_preallocated, PageAuditWhy why)
{
  // First select a virtual memory location to allocate a page into.
  PageNum page = _one_page_small->select_page();
  if ( page == NoPage ) {
    if (PrintGCDetails) {
julong balance=os::available_memory();
gclog_or_tty->print_cr("Can't allocate page.  Virtual address space is full?  C-Heap balance = %llu",balance);
    }
    return NoPage;
  }

  // Next, try and allocate a page
  if ( use_preallocated ) {
    GPGC_PageBudget::get_preallocated_page(page, true);
    GPGC_PageAudit::audit_entry(page, why, GPGC_PageAudit::GetPreAlloc);
  } else {
    if ( ! GPGC_PageBudget::allocate_page(page) ) {
      // Unable to actually allocate a page from the OS.  We need to give back the virtual
      // address to the pgcOnePageSpace, and then return NoPage.
      _one_page_small->return_available_page(page);
      GPGC_PageAudit::audit_entry(page, why, GPGC_PageAudit::FailedAlloc);
      return NoPage;
    } 

    GPGC_PageAudit::audit_entry(page, why, GPGC_PageAudit::AllocPage);
  }

  if ( count_for_alloc_rate ) {
    // _pages_allocated is used to calculate a per-generation allocation rate.  Not all allocations should contribute to that number.
    atomic_stat_increment(&_pages_allocated);
    
  }
  atomic_stat_increment(&_small_space_page_capacity);

  // When we get here, possible failure points are behind us.
  GPGC_Marks::clear_all_metadata(page, 1);
  GPGC_CardTable::clear_card_marks(page);

  return page;
}


PageNum GPGC_Generation::select_and_commit_mid_page(size_t words, PageAuditWhy why)
{
  // First select a virtual memory location to allocate a page into.
  PageNum page = _one_page_mid->select_page();
  if ( page == NoPage ) {
    if (PrintGCDetails) gclog_or_tty->print_cr("Can't allocate page, virtual address space is full");
    return NoPage;
  }

  // Next, try and allocate a page
  HeapWord *current_top = GPGC_Layout::PageNum_to_addr(page);
HeapWord*new_top=current_top+words;

  if ( ! allocate_mid_space_sub_pages(current_top, new_top) ) {
    // Unable to actually allocate the pages from the OS.  We need to give back the virtual
    // address to the pgcOnePageSpace, and then return NoPage.
    _one_page_mid->return_available_page(page);
    GPGC_PageAudit::audit_entry(page, why, GPGC_PageAudit::FailedAlloc);
    return NoPage;
  } 

  GPGC_PageAudit::audit_entry(page, why, GPGC_PageAudit::AllocPage);

  // When we get here, possible failure points are behind us.
  GPGC_Marks::clear_all_metadata(page, PagesPerMidSpaceBlock);
  GPGC_CardTable::clear_card_marks(page);

  return page;
}


HeapWord* GPGC_Generation::allocate_full_page_small(bool is_tlab)
{
  PageNum page = select_and_commit_small_page(CountForAllocRate, DontUsePreAllocated, PageAuditWhy(0x500|_generation));
  if ( page == NoPage ) return NULL;

  // When we get here, possible failure points are behind us.
  uint64_t flags = _marking_underway ? GPGC_PageInfo::NoRelocateFlag : 0;
  if ( is_tlab ) flags |= GPGC_PageInfo::TLABFlag;

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  info->zero_raw_stats      ();
  info->set_flags_non_atomic(flags);  // No other thread will see this page until set_gen_and_state() below.
  info->set_time            (is_tlab ? 0 : os::elapsed_counter());
  info->set_top             (GPGC_Layout::PageNum_to_addr(page+1));

  // clear entries in trap array..
  bool is_new_gen_page = (generation() == GPGC_PageInfo::NewGen);
  bool in_large_space  = false;
  GPGC_ReadTrapArray::init_trap(page, 1, is_new_gen_page, in_large_space);

  Atomic::write_barrier(); // Make sure the GPGC_PageInfo is set before changing state to Allocated.
  info->set_gen_and_state(_generation, GPGC_PageInfo::Allocated);

  if (GPGCTracePageSpace) {
    gclog_or_tty->print_cr("Allocating full page 0x%lX to gen %d%s", page, _generation, is_tlab ? " for tlab" : "");
  }

  return GPGC_Layout::PageNum_to_addr(page);
}


HeapWord* GPGC_Generation::allocate_multi_page(size_t words)
{
  long pages = (words + WordsPerGPGCPage - 1) >> LogWordsPerGPGCPage;

  // First, get a block of virtual address space to allocate pages into.
  PageNum block = _multi_page->select_block(pages, _generation);
  if ( block == NoPage ) {
    if (PrintGCDetails) gclog_or_tty->print_cr("Can't allocate "INTX_FORMAT" pages, virtual address space is full", pages);
    return NULL;
  }

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

  // Next, try and allocate pages
  if ( ! GPGC_PageBudget::allocate_pages(block, pages) ) {
    // Unable to actually allocate the pages.  We need to give back the virtual
    // address to the GPGC_MultiPageSpace, and then return NULL.
    _multi_page->return_available_block(block);
    // reset: state, Gen, flags, trap state.
    info->clear_block_state(block, pages, _generation);
    return NULL;
  }

  atomic_stat_add(pages, &_pages_allocated);
  atomic_stat_add(pages, &_large_space_page_capacity);

  GPGC_PageAudit::audit_entry(block, PageAuditWhy(0x700|_generation), GPGC_PageAudit::AllocPage);

  // When we get here, possible failure points are behind us.
  GPGC_Marks::clear_all_metadata(block, pages);
  GPGC_CardTable::clear_card_marks(block);

  info->zero_raw_stats      ();
  info->set_time            (os::elapsed_counter());
  info->set_top             (GPGC_Layout::PageNum_to_addr(block) + words);
  info->set_block_size      (pages);

  for ( long i=1; i<pages; i++ ) {
    info[i].set_gen_and_state(_generation, GPGC_PageInfo::Unmapped);
  }

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Allocating block of %ld pages at 0x%lX to gen %d", pages, block, _generation);

  return GPGC_Layout::PageNum_to_addr(block);
}


PageNum GPGC_Generation::clone_block(PageNum orig_block, long pages)
{
  assert0(GPGC_PageInfo::page_info(orig_block)->just_gen() == GPGC_PageInfo::NewGen);
  assert0(_generation == GPGC_PageInfo::OldGen);

  // First, get the virtual address space for the clone.
  PageNum clone = _multi_page->select_block(pages, _generation);
  if ( clone == NoPage ) {
    if (PrintGCDetails) gclog_or_tty->print_cr("Can't clone block of "INTX_FORMAT" pages, virtual address space is full", pages);
    return NoPage;
  }

  // batch remap 
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::relocate_memory);
    os::batched_relocate_memory((char*)GPGC_Layout::PageNum_to_addr(orig_block),
                                (char*)GPGC_Layout::PageNum_to_addr(clone),
                                pages << LogBytesPerGPGCPage);
  }

  // When we get here, possible failure points are behind us.
  
  // TODO: check if this is needed?
  GPGC_Marks::clear_all_metadata(clone, pages);
  // should we be clearing card-marks as well?
  // GPGC_CardTable::clear_card_marks(clone);

  Atomic::add_ptr(pages, (intptr_t*)&_large_space_page_capacity);

  GPGC_PageInfo* clone_info       = GPGC_PageInfo::page_info(clone);
  GPGC_PageInfo* orig_info        = GPGC_PageInfo::page_info(orig_block);
  long           top_offset       = orig_info->top() - GPGC_Layout::PageNum_to_addr(orig_block);
  long           orig_raw_stats   = orig_info->raw_stats();

  orig_info->zero_raw_stats();
  clone_info->set_raw_stats(orig_raw_stats);
  clone_info->set_top             (GPGC_Layout::PageNum_to_addr(clone) + top_offset);
  clone_info->set_time            (os::elapsed_counter());

  for ( long i=1; i<pages; i++ ) {
    clone_info[i].set_gen_and_state(_generation, GPGC_PageInfo::Unmapped);
  }

  if (GPGCTracePageSpace)
gclog_or_tty->print_cr("Cloning block of %ld pages at 0x%lX to 0x%lX in gen %d",
                           pages, orig_block, clone, _generation);

  return clone;
}


PageNum GPGC_Generation::allocate_mid_remapping_page(long source_space_id)
{
  // First select a virtual memory location for remapping mid space object into.
  PageNum page = _one_page_mid->select_page();

  if ( page == NoPage ) {
    if (PrintGCDetails) gclog_or_tty->print_cr("Can't allocate page, virtual address space is full");
fatal("Unexpectedly out of virtual address space for mid space remapping");
  }

  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0x800|_generation), GPGC_PageAudit::GetForRemap);

  // When we get here, possible failure points are behind us.

  // Clear metadata and card marks
  GPGC_Marks::clear_all_metadata(page, PagesPerMidSpaceBlock);
  GPGC_CardTable::clear_card_marks(page);

  // We need to set the NoRelocateFlag if we're relocating into a generation whose collector is marking:
  uint64_t flags = _marking_underway ? GPGC_PageInfo::NoRelocateFlag : 0;

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  info->zero_raw_stats      ();
  info->set_flags_non_atomic(flags);  // No other thread will see this page until set_gen_and_state() below.
  info->set_time            (0);
  info->set_top             (GPGC_Layout::PageNum_to_addr(page));

  // Initialize the count of pages that will be freed with each unshatter:
  for ( long i=0; i<PagesPerMidSpaceBlock; i++ ) {
    info[i].reset_unshatter_free_stats();
    info[i].set_relocate_space(source_space_id);
  }

  // clear entry in read-trap-arraytrue
  bool is_new_gen_page = (generation() == GPGC_PageInfo::NewGen);
  bool in_large_space  = false;
  GPGC_ReadTrapArray::init_trap(page, PagesPerMidSpaceBlock, is_new_gen_page, in_large_space);
  
  Atomic::write_barrier(); // Make sure the GPGC_PageInfo is set before changing state to Allocated.
  info->set_gen_and_state(_generation, GPGC_PageInfo::Allocated);

  if (GPGCTracePageSpace) {
gclog_or_tty->print_cr("Getting page 0x%lX to gen %d for remapping",page,_generation);
  }

  return page;
}


// This method attempts to heal the portion of a shattered mid-space page that a mutator
// has trapped upon.  It tries once.  The heal mail fail if no memory is available to allocate
// a 2MB page to unshatter into.  If that occurs, this method returns false, and it's up to
// the caller to retry.  True is returne on successful unshatter, false if a retry is needed.
bool GPGC_Generation::mutator_heal_mid_page(PageNum page, char* force_addr)
{
  bool result;

  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::partial_unshatter);
    result = os::partial_unshatter_memory((char*)GPGC_Layout::PageNum_to_addr(page), force_addr);
  }

  if ( ! result ) {
    // If the process is out of java heap memory, then a mutator will not be able to get the 2MB
    // page needed to unshatter a set of small pages.  If that occurs, the mutator has to wait
    // for a garbage collector thread with a preallocated page to unshatter the page.  The caller
    // is responsible for the wait and retry.
    return false;
  }

  GPGC_PageInfo* info = GPGC_PageInfo::page_info_unchecked(page);

  if ( info->set_first_unshatter() ) {
    // If it appears that this function is responsible for allocating a resource page for the unshatter,
    // then update the memory allocation accounting stats appropriately.
    uint64_t              relocate_space   = info->relocate_space();
    GPGC_RelocationSpike* relocation_spike = (relocate_space==objectRef::new_space_id)
                                             ? GPGC_NewCollector::relocation_spike()
                                             : GPGC_OldCollector::relocation_spike();

    relocation_spike->add_and_record_peak(1);

    // TODO: See note about _pages_allocated stat in heal_mid_remapping_page() below.  The mutator
    // doesn't know the source generation, and so can't test for promotion here.
    //
    // if ( source_generation != this ) {
    //   // _pages_allocated is used to calculate an allocation rate for timing GC.
    //   // Intra-generation relocation shouldn't count, but promotion from NewGen to OldGen
    //   // is the basis of OldGen's allocation rate.  So we only update _pages_allocated here
    //   // when it's a cross generation relocation that we're healing.
    //   atomic_stat_increment(&target_gen->_pages_allocated);
    // }

    atomic_stat_increment(&_mid_space_page_capacity);
    // gclog_or_tty->print_cr("gen %d: mutator heal mid page 0x%lX +1 to _mid_space_page_capacity: 1st unshatter\n", _generation, page);
  }

  // No accounting is done for deallocation, because we won't be sure we've unshattered an
  // entire page and forced deallocation until the GC thread comes along and does the
  // os::unshatter_all_memory() call.

  return true;
}


void GPGC_Generation::heal_mid_remapping_page(PageNum page, PageNum resource_page, GPGC_RelocationSpike* relocation_spike)
{
  assert0( GPGC_Layout::mid_space_page(page) );
  assert0( page == GPGC_Layout::page_to_MidSpaceBasePageNum(page) );

  GPGC_PageInfo*      info       = GPGC_PageInfo::page_info(page);
HeapWord*top=info->top();
  PageNum             last_page  = GPGC_Layout::addr_to_PageNum(top-1);
  GPGC_Generation*    target_gen = GPGC_Space::generation(info->just_gen());

  assert0( last_page >= page );

  for ( ; page<=last_page; page++,info++ )
  {
    long used_preallocated = 0;
    if ( this != target_gen ) {
      // _pages_allocated is used to calculate an allocation rate for timing GC.
      // Intra-generation relocation shouldn't count, but promotion from NewGen to OldGen
      // is the basis of OldGen's allocation rate.  So we only update _pages_allocated here
      // when it's a cross generation relocation that we're healing.
      //
      // We always account for pages allocated here, even if a mutator set first unshatter and
      // did the allocation.  The mutator doesn't have the data necessary to decide if an unshatter
      // is a promotion or not.
      //
      // TODO: Improve mutator so it can detect promoting unshatters, add update to this stat
      // in the mutator path and move this update into the if set_first_unshatter block below.
      atomic_stat_increment(&target_gen->_pages_allocated);
    }
    if ( info->set_first_unshatter() ) {
      atomic_stat_increment(&target_gen->_mid_space_page_capacity);
      // gclog_or_tty->print_cr("gen %d: heal mid page 0x%lX +1 to _mid_space_page_capacity: 1st unshatter\n", target_gen->_generation, page);

      // NOTE: We don't update the relocation_page_count, as we're allocating with a preallocated
      // page, and preallocate page ops are considered NOPs from a relocation spike perspective.
      // We also don't worry about GPGC_PageBudget's accounts, since actual # of pages allocated isn't
      // affected. 
      used_preallocated = 1;
    }

    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::unshatter_all);
      os::unshatter_all_memory((char*)GPGC_Layout::PageNum_to_addr(page), (char*)GPGC_Layout::PageNum_to_addr(resource_page));
    }

    // NOTE: If the resource pages was used for the unshatter, then to when accounting for pages
    // freed we decement the relocation page count by N-1, because one page is returned to
    // preallocated instead of the kernel.  If a mutator allocated for unshatter, then we
    // decrement the relocation page count by N, because all of the pages are deallocated to the
    // kernel.
    long pages_freed_on_unshatter = info->unshatter_free_count();
    // we will atleast use one physical page for unshatter
    long physical_pages_freed     = pages_freed_on_unshatter - 1;

    assert0(info == GPGC_PageInfo::page_info_unchecked(page));
// TODO: reinstate assert when bug 25958 is fixed
// assert0(pages_freed_on_unshatter > 0);

    GPGC_PageBudget::account_for_deallocate(physical_pages_freed, "unshatter", page);

    relocation_spike->subtract(physical_pages_freed);
// TODO: remove check on atomic_stat_subtract when bug 25958 is fixed
if (pages_freed_on_unshatter>0)
    atomic_stat_subtract(pages_freed_on_unshatter, &_mid_space_page_capacity);
    // gclog_or_tty->print_cr("gen %d: heal mid remapping page 0x%lX -%ld to _mid_space_page_capacity: freed unshatter\n",
    //                        _generation, page, pages_freed_on_unshatter);
  }
}


PageNum GPGC_Generation::allocate_small_relocation_page(GPGC_Generation* source_gen, bool use_preallocated, PageAuditWhy why)
{
  // We should only be updating _pages_allocated when we're allocating for a mutator, or
  // when we're doing an inter-generation relocation.  Intra-generation relocation shouldn't
  // count when we're calculating the allocation rate for each generation.
  bool    count_for_alloc_rate = (source_gen != this);
  PageNum page                 = select_and_commit_small_page(count_for_alloc_rate, use_preallocated, why);

  if ( page == NoPage ) return NoPage;

  // When we get here, possible failure points are behind us.

  // We need to set the NoRelocateFlag if we're relocating into a generation whose collector is marking:
  uint64_t flags = _marking_underway ? GPGC_PageInfo::NoRelocateFlag : 0;

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  info->zero_raw_stats      ();
  info->set_flags_non_atomic(flags);  // No other thread will see this page until set_gen_and_state() below.
  info->set_time            (0);
  info->set_top             (GPGC_Layout::PageNum_to_addr(page+1));

  // clear entry in read-trap-array
  bool is_new_gen_page = (generation() == GPGC_PageInfo::NewGen);
  bool in_large_space  = false;
  GPGC_ReadTrapArray::init_trap(page, 1, is_new_gen_page, in_large_space);

  Atomic::write_barrier(); // Make sure the GPGC_PageInfo is set before changing state to Allocated.
  info->set_gen_and_state(_generation, GPGC_PageInfo::Allocated);

  if (GPGCTracePageSpace) {
gclog_or_tty->print_cr("Allocating full page 0x%lX to gen %d for relocation",page,_generation);
  }

  // NOTE: When allocating small relocation pages without using a preallocated page, we want to increment
  // the relocation spike counter.  Rather than adding another parameter to this function, we delegate
  // the record keeping to the caller.  If the caller didn't set use_preallocated = true, then we expect
  // it to do this:
  //
  // relocation_spike->add_and_record_peak(1);

  return page;
}


void GPGC_RelocationPages::new_page(long slot, PageNum page)
{
  // TODO: We're holding a slot lock, but we're atomic updating a global counter.  We should either
  //       have slot local counters that get aggregated later, or else decide a global lock is fine
  //       for replacing pages in the slots.
  _pages[slot] = page;
  Atomic::inc_ptr((intptr_t*)&_page_count);
}


HeapWord* GPGC_Generation::new_page_in_relocation_slot(GPGC_RelocationPages* relocation_pages, GPGC_Generation* source_gen,
                                                       long slot, long word_size, long page_time)
{
  assert0(word_size>0);

  PageNum page = allocate_small_relocation_page(source_gen, DontUsePreAllocated, PageAuditWhy(0x900|_generation));
  if ( page == NoPage ) { return NULL; }

  // NOTE: As per the comment in allocate_small_relocation_page, update the relocation_spike data:
  if ( source_gen->generation() == GPGC_PageInfo::NewGen ) {
    GPGC_NewCollector::relocation_spike()->add_and_record_peak(1);
  } else {
    GPGC_OldCollector::relocation_spike()->add_and_record_peak(1);
  }

  GPGC_PageInfo* info    = GPGC_PageInfo::page_info(page);
  HeapWord*      top     = GPGC_Layout::PageNum_to_addr(page);
HeapWord*new_top=top+word_size;

  info->set_time(page_time);
info->set_top(new_top);
       
  Atomic::write_barrier();

  relocation_pages->new_page(slot, page);

  return top;
}


// This is the allocation function used by mutators who are trying to relocate an object.
//
// The design is tricky, because we want enough parallelism in allocation to not throttle
// the mutators during concurrent-relocation phases of GC.  But, we don't want to allocate
// too many pages that don't get filled with objects.
//
// Perhaps we can dynamically scale up the number of parallel relocation pages allocated?
// Have each thread keep a spin-count on trying to CAS forwards a top pointer, and expand
// the set of relocation pages if the spin-count was too high?
//
// When a page is full, how do we replace it?  AzLock?  That'd be easiest.
//
// Initial design:
//
// - Each generation has a preallocated array of relocation pages.
// - There is a #-of-pages field in the generation that controls how many slots are in use.
// - Use the CPU tick counter as a random number, mod to the slot number of a page.
// - CAS-spin trying to bump forwards the top pointer.
// - If spin count is too high, try to increase the #-of-pages by one, under a AzLock.
// - If a page is full, take a mutex for the slot.  Allocate a new page, release the mutex.
//
HeapWord* GPGC_Generation::allocate_for_relocate(GPGC_Generation* source_gen, long word_size, long page_time)
{
  // We use separate pages for mutator relocation within a generation versus mutator promotion, so we can
  // more clearly account for memory usage and collector behavior.
  GPGC_RelocationPages* relocation_pages;
  if ( source_gen == this ) {
    relocation_pages = &_relocation_pages;
  } else {
    assert0(source_gen->_generation==GPGC_PageInfo::NewGen && this->_generation==GPGC_PageInfo::OldGen);
    relocation_pages = &_promotion_pages;
  }

  long    top_slot = relocation_pages->top_slot();
  long    slot     = long(Thread::current()) % top_slot;
  PageNum page     = relocation_pages->page(slot);

  // Allocate a new page for allocation if the slot is empty.
  if ( page == NoPage ) {
    MutexLocker ml(*relocation_pages->lock(slot));
    page = relocation_pages->page(slot);
    if ( NoPage == page ) {
      return new_page_in_relocation_slot(relocation_pages, source_gen, slot, word_size, page_time);
    }
  }

  long           spin_counter = 0;

  GPGC_PageInfo* info         = GPGC_PageInfo::page_info(page);
  HeapWord*      end          = GPGC_Layout::PageNum_to_addr(page+1);

  HeapWord*      top;
HeapWord*new_top;

  while (true) {
    top     = info->top();
    new_top = top + word_size;

    if ( new_top <= end )
    {
      if ( intptr_t(top) == Atomic::cmpxchg_ptr(intptr_t(new_top), (intptr_t*)info->top_addr(), intptr_t(top)) ) {
        break; // Successfully bumped up the top pointer!
      }
    } else {
      {
        MutexLocker ml(*relocation_pages->lock(slot));
        PageNum old_page = page;
        page = relocation_pages->page(slot);
        if ( page == old_page ) {
          return new_page_in_relocation_slot(relocation_pages, source_gen, slot, word_size, page_time);
        }
      }

      info = GPGC_PageInfo::page_info(page);
      end  = GPGC_Layout::PageNum_to_addr(page+1);
    }

    spin_counter ++;
  }


  // If the spin count is too high, bring another slot into use.
  if ( spin_counter>5 ) {
    relocation_pages->expand(_relocation_lock, top_slot);
  }

  return top;
}


long GPGC_RelocationPages::flush()
{
  assert0( GPGC_Safepoint::is_at_new_safepoint() );

  long result = 0;

  for ( long i=0; i<_top_slot; i++ ) {
    PageNum page = _pages[i];
    if ( page != NoPage ) {
      _pages[i] = NoPage;
    }
  }

result=_page_count;
  _page_count = 0;

  return result;
}


void GPGC_Generation::flush_relocation_pages(DetailTracer* dt)
{
  long pages = _relocation_pages.flush();

  dt->print("gen 0x%x: %d relocation slots, %ld pages ", _generation, _relocation_pages.top_slot(), pages);
}


void GPGC_Generation::flush_promotion_pages(DetailTracer* dt)
{
  long pages = _promotion_pages.flush();

  dt->print("gen 0x%x: %d promotion slots, %ld pages ", _generation, _promotion_pages.top_slot(), pages);
}


// transition the state to Relocated
void GPGC_Generation::new_gc_relocated_block(PageNum block)
{
  assert0(GPGC_Layout::large_space_page(block));

  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();  // Ok to read here, only written under lock

  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(pages > 0);
  assert0(info->just_gen() == GPGC_PageInfo::NewGen);

  info->set_just_state(GPGC_PageInfo::Relocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocated block of %ld pages at 0x%lX in gen %d", pages, block, info->just_gen());

}


void GPGC_Generation::old_gc_relocated_block(PageNum block)
{
  ShouldNotReachHere();

  assert0(GPGC_Layout::large_space_page(block));

  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();  // Ok to read here, only written under lock

  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(pages > 0);
  assert0(info->just_gen() == GPGC_PageInfo::OldGen);

  info->set_just_state(GPGC_PageInfo::Relocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocated block of %ld pages at 0x%lX in gen %d", pages, block, info->just_gen());
}


// Mark a page as having been Relocated, and release the memory from its mirror page.
void GPGC_Generation::relocated_page(PageNum page, GPGC_RelocationSpike* relocation_spike)
{
  assert0(GPGC_Layout::small_space_page(page));

  PageNum        mirror_page = page + GPGC_Layout::heap_mirror_offset;
  GPGC_PageInfo* info        = GPGC_PageInfo::page_info(page);

  assert0(info->just_state() == GPGC_PageInfo::Relocating);
  assert0(info->just_gen()   == _generation);

  info->set_gen_and_state(_generation, GPGC_PageInfo::Relocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocated page 0x%lX in gen %d", page, _generation);

  bool deallocated = GPGC_PageBudget::deallocate_mapped_page(mirror_page);
  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0xB00|_generation), GPGC_PageAudit::FreeMirror);

  atomic_stat_decrement(&_small_space_page_capacity);
  if ( deallocated ) {
    relocation_spike->subtract(1);
  }

  while (true) {
    PageNum old_head = _relocated_pages;
    info->set_ll_next(old_head);
    Atomic::write_barrier(); // Make sure the ll_next is set before CASing the list head.
    if ( old_head == Atomic::cmpxchg(page, (jlong*)&_relocated_pages, old_head) ) {
      // Successfully CAS'ed a new list head.
      break;
    }
  }

}


// Mark a page as having been Relocated, and release the unremapped memory for it.
void GPGC_Generation::remapped_page(PageNum page)
{
  assert0(GPGC_Layout::mid_space_page(page));

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  assert0(info->just_state() == GPGC_PageInfo::Relocating);
  assert0(info->just_gen()   == _generation);

  info->set_gen_and_state(_generation, GPGC_PageInfo::Relocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Relocated page 0x%lx in gen %d", page, _generation);

  // Fragments of remaining memory after remapping out live objects don't provide contiguous 2MB
  // pages for GPGC_PageBudget to reserve for future use.  So we just do a bulk deallocate of the
  // address range.
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::uncommit_memory);
    os::uncommit_heap_memory((char*)GPGC_Layout::PageNum_to_addr(page), BytesPerMidSpaceBlock, false/*no-tlb-sync*/, true/*blind*/);
  }

  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0xC00|_generation), GPGC_PageAudit::FreePage);

  long pages_freed_on_unmap = info->unmap_free_count();

  assert0(pages_freed_on_unmap >= 0);

  GPGC_PageBudget::account_for_deallocate(pages_freed_on_unmap, "mid_space unmap", page);

  if ( pages_freed_on_unmap > 0 ) {
    // TODO-NOW: get a relocation_spike into this function so we can track unmap impact.
    // relocation_spike->subtract(pages_freed_on_unmap);

    atomic_stat_subtract(pages_freed_on_unmap, &_mid_space_page_capacity);
    // gclog_or_tty->print_cr("gen %d: remapped page 0x%lX -%ld to _mid_space_page_capacity\n", _generation, page, pages_freed_on_unmap);
  }

  // TODO: peak relocation count?
  // TODO: see relocated_page() above: where should I track the pages_freed_on_unmap stats?  Any other stats I should adjust here?

  while (true) {
    PageNum old_head = _relocated_pages;
    info->set_ll_next(old_head);
    Atomic::write_barrier(); // Make sure the ll_next is set before CASin the list head.
    if ( old_head == Atomic::cmpxchg(page, (jlong*)&_relocated_pages, old_head) ) {
      // Successfully CAS'ed a new list head.
      break;
    }
  }
}


// Release a fully remapped relocated page for future use.  The memory should have already been unmapped.
void GPGC_Generation::release_relocated_page(PageNum page)
{
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(page);
  long           pages = GPGC_Layout::mid_space_page(page) ? PagesPerMidSpaceBlock : 1;

  assert0(info->just_state()  == GPGC_PageInfo::Relocated);
  assert0(info->just_gen()    == _generation);
  assert0(info->relocations() != NULL);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Releasing relocated page 0x%lX", page);

  GPGC_ReadTrapArray::clear_trap_on_page(page, pages);
  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0xD00|_generation), GPGC_PageAudit::Released);

info->set_relocations(NULL);
  info->set_reloc_len  (0);
  info->set_just_state (GPGC_PageInfo::Unmapped);

  if (GPGCReuseFreePages) {
    // The virtual page is immediately available for new allocation, because the
    // actual unmap of the memory was done in a batch during the relocation
    // safepoint, and had a TLB invalidate.
    if ( GPGC_Layout::mid_space_page(page) ) {
      _one_page_mid->return_available_page(page);
    } else {
      assert0(GPGC_Layout::small_space_page(page));
      _one_page_small->return_available_page(page);
    }
  }
}


void GPGC_Generation::release_relocated_block(PageNum block)
{
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();  // Ok to read here, only written under lock

  assert0(info->just_state() == GPGC_PageInfo::Relocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Releasing relocated block of %ld pages at 0x%lX", pages, block);
  
  bool new_gen_block = (_generation == GPGC_PageInfo::NewGen) ? true : false;
  // GPGC_ReadTrapArray::clear_trap_on_block() also deallocates the virtual address space:
  GPGC_ReadTrapArray::clear_trap_on_block(block, pages, new_gen_block);

  GPGC_PageAudit::audit_entry(block, PageAuditWhy(0xE00|_generation), GPGC_PageAudit::FreePage);

  info->set_just_state(GPGC_PageInfo::Unmapped);

  if (GPGCReuseFreePages) {
    _multi_page->return_available_block(block);
  }
}


long GPGC_Generation::release_relocated_pages()
{
  long released_pages_count = 0;

  PageNum page = _relocated_pages;

  while (page!=NoPage) {
    GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
    PageNum        next = info->ll_next();

    info->set_ll_next(NoPage);

    release_relocated_page(page);

page=next;

    released_pages_count++;
  }

  _relocated_pages = NoPage;

  return released_pages_count;
}


// Clean up both the physical and virtual memory of a page.  No TLB traps need clearing,
// since empty pages don't get them set.
void GPGC_Generation::release_empty_page(PageNum page, GPGC_RelocationSpike* relocation_spike)
{
  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(info->just_gen()   == _generation);

  info->set_just_state(GPGC_PageInfo::Unmapped);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Releasing empty page 0x%lX in gen %d", page, _generation);

  GPGC_OnePageSpace* one_page;
  intptr_t*          capacity_addr;
  long               pages;
  long               pages_freed;

  if ( GPGC_Layout::mid_space_page(page) ) {
    assert( info->top() != GPGC_Layout::PageNum_to_addr(page), "Don't expect to see mid space pages with no allocated pages" );

    PageNum end_page = GPGC_Layout::addr_to_PageNum(info->top() + WordsPerGPGCPage - 1);

    one_page      = _one_page_mid;
    capacity_addr = &_mid_space_page_capacity;
    pages         = end_page - page;
    pages_freed   = GPGC_PageBudget::deallocate_mapped_block(page, pages);
  } else {
    one_page      = _one_page_small;
    capacity_addr = &_small_space_page_capacity;
    pages         = 1;
    pages_freed   = GPGC_PageBudget::deallocate_mapped_page(page) ? 1 : 0;
  }

  if ( pages_freed > 0 ) {
    relocation_spike->subtract(pages_freed);
  }
  atomic_stat_subtract(pages, capacity_addr);

  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0xF00|_generation), GPGC_PageAudit::FreePage);

  if (GPGCReuseFreePages) {
    // TODO: figure out the right page hold behavior
    one_page->hold_page_for_TLB_resync(page);
  }
}


// Clean up both the physical and virtual memory of a block of pages.  No TLB traps need clearing,
// since page blocks don't get them set.
void GPGC_Generation::release_empty_block(PageNum block, GPGC_RelocationSpike* relocation_spike)
{
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();  // Ok to read here, only written under lock

  assert0(GPGC_Layout::large_space_page(block));
  assert0(info->just_state() == GPGC_PageInfo::Allocated);
  assert0(info->just_gen()   == _generation);
  assert((pages<<LogWordsPerGPGCPage) >= (long)GPGC_Layout::large_space_min_object_words(), "Large space blocks are expected to be at least 16 MB");
  assert(pages < (((1L<<31)+1+WordsPerGPGCPage-1)>>LogWordsPerGPGCPage), "Max expected large space block size of 2^31 array entries exceeded");

  // TODO: maw: do we need a barrier when updating page state here?
  info->set_just_state(GPGC_PageInfo::Unmapped);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Releasing empty block of %ld pages at page 0x%lX", pages, block);

  long pages_freed = GPGC_PageBudget::deallocate_mapped_block(block, pages);

assert(pages_freed>0,"Shouldn't ever see all pages in a block going back to the preallocated pages, just 1");

  relocation_spike->subtract(pages_freed);
  atomic_stat_subtract(pages, &_large_space_page_capacity);

  GPGC_PageAudit::audit_entry(block, PageAuditWhy(0x1000|_generation), GPGC_PageAudit::FreePage);

  if (GPGCReuseFreePages) {
    _multi_page->hold_block_for_TLB_resync(block);
  }
}


void GPGC_Generation::object_iterate(ObjectClosure* cl)
{
  // TODO: maw: we should probably make sure this isn't being called during a GC cycle.
  _one_page_small->object_iterate(_generation, cl);
  _one_page_mid->object_iterate(_generation, cl);
  _multi_page->object_iterate(_generation, cl);
}


void GPGC_Generation::clear_allocation_buffers()
{
  assert0( GPGC_Safepoint::is_at_new_safepoint() );

  // Probably don't need locks, since we're in a safepoint, but I'm acquiring
  // them because I'm paranoid.

  {
    MutexLocker ml(*_small_shared_allocation_lock);
  
    PageNum page = small_space_allocation_page();
    if ( page != NoPage ) {
      set_small_space_allocation_page(NoPage);

      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
      info->set_ll_next(_full_small_space_alloc_pages);
      _full_small_space_alloc_pages = page;
    }

    page = _full_small_space_alloc_pages;
    _full_small_space_alloc_pages = NoPage;
    while ( page != NoPage ) {
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

      assert0(GPGC_Layout::small_space_page(page));
      assert0(info->just_state() == GPGC_PageInfo::Allocating);
  
      info->set_time(os::elapsed_counter());
      info->set_just_state(GPGC_PageInfo::Allocated);

      GPGC_PageAudit::audit_entry(page, PageAuditWhy(0x1100|_generation), GPGC_PageAudit::CloseShared);
  
      page = info->ll_next();
    }
  }

  {
    MutexLocker ml(*_mid_shared_allocation_lock);
  
    PageNum page = mid_space_allocation_page();
    if ( page != NoPage ) {
      set_mid_space_allocation_page(NoPage);

      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
      info->set_ll_next(_full_mid_space_alloc_pages);
      _full_mid_space_alloc_pages = page;
    }
  
    page = _full_mid_space_alloc_pages;
    _full_mid_space_alloc_pages = NoPage;
    while ( page != NoPage ) {
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

      assert0(GPGC_Layout::mid_space_page(page));
      assert0(info->just_state() == GPGC_PageInfo::Allocating);
  
      info->set_time(os::elapsed_counter());
      info->set_just_state(GPGC_PageInfo::Allocated);
  
      GPGC_PageAudit::audit_entry(page, PageAuditWhy(0x1200|_generation), GPGC_PageAudit::CloseShared);

      page = info->ll_next();
    }
  }
}


void GPGC_Generation::set_marking(bool state)
{
_marking_underway=state;
  Atomic::write_barrier();
}


HeapWord* GPGC_Generation::block_start(const void* addr)
{
  // Block start isn't supported by GPGC.
  return NULL;
}


void GPGC_Generation::verify_capacity()
{
  long small_space_capacity = _one_page_small->verify_capacity(_generation);
  long mid_space_capacity   = _one_page_mid->verify_capacity(_generation);

  if ( mid_space_capacity != _mid_space_page_capacity ) {
gclog_or_tty->print_cr("_mid_space_page_capacity failed verify");
gclog_or_tty->flush();
  }

  if ( small_space_capacity != _small_space_page_capacity) {
gclog_or_tty->print_cr("_small_space_page_capacity failed verify");
gclog_or_tty->flush();
  }

  guarantee( small_space_capacity == _small_space_page_capacity, "small space capacity failed verification" );
  guarantee( mid_space_capacity   == _mid_space_page_capacity,   "mid space capacity failed verification" );
}
