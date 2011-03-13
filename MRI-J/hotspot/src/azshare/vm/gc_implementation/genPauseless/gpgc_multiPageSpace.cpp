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


#include "gpgc_cardTable.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_metadata.hpp"
#include "gpgc_multiPageSpace.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"
#include "java.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "tickProfiler.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "gpgc_population.inline.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"

// The real initialization is in the initialize() method.
GPGC_MultiPageSpace::GPGC_MultiPageSpace()
{
  _start_page = _top_page = _end_page = NoPage;

_available_blocks=NULL;

  _blocks_on_hold = _pending_available_blocks_head = NoPage;

  _allocated_pages = _available_pages = _held_pages = 0;
}


void GPGC_MultiPageSpace::initialize(PageNum start, PageNum end)
{
  if ( start >= end ) {
vm_exit_during_initialization("Invalid page range in GPGC_MultiPageSpace");
  }
_start_page=start;
  _top_page   = _start_page;
_end_page=end;

_available_blocks=NULL;
  _blocks_on_hold   = NoPage;

  _pending_available_blocks_head = NoPage;

  _allocated_pages = 0;
  _available_pages = 0;
  _held_pages      = 0;
}


//  Get a block of pages from the available pages.  Return NoPage if the
//  available pages can't provide a block of the requested size.
PageNum GPGC_MultiPageSpace::get_available_block(long pages)
{
assert_lock_strong(GPGC_MultiPageSpace_lock);

  GPGC_PageGroup* group = _available_blocks;

  // Find the smallest page group that can contain the request:
  while ( group && group->block_size()<pages ) {
    group = group->grp_next();
  }
  if ( group == NULL ) return NoPage;

  // Pull the first block off the block list:
  PageNum        block = group->first_block();
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);

  group->set_first_block( info->ll_next() );

  assert0(info->block_size()==group->block_size());

  info->set_block_size(pages);

  // Track the pages in the block that we're not returning to the caller:
  PageNum unneeded_block = block + pages;
  long    unneeded_pages = group->block_size() - pages;

  if ( group->first_block() == NoPage ) {
    // Delete empty page group:
    delete_page_group(group);
  } else {
    // Patch up the linked block list:
    info->set_ll_next( NoPage );
  }

  // Any unneeded pages from the block go back into a new block list:
  if ( unneeded_pages > 0 ) {
    info = GPGC_PageInfo::page_info(unneeded_block);
    info->set_gen_and_state(GPGC_PageInfo::InvalidGen, GPGC_PageInfo::Unmapped);
    info->set_block_size(unneeded_pages);
    add_block_to_page_group(unneeded_block, unneeded_pages);
  }

  if (GPGCKeepPageStats) {
    _available_pages -= pages;
  }

  return block;
}


//  Expand the range of pages used by the space, and return a block of pages for
//  use.  Return NoPage if the address range of the space is too full to get the
//  requested block.
PageNum GPGC_MultiPageSpace::get_expansion_block(long pages)
{
assert_lock_strong(GPGC_MultiPageSpace_lock);

  PageNum block   = _top_page;
  PageNum new_top = block + pages;

  if ( new_top > _end_page ) return NoPage;

  for ( long i=0; i<pages; i++ ) {
    if ( ! GPGC_PageInfo::expand_pages_in_use(block+i) ) {
      // Can't get a GPGC_PageInfo for a new page, so can't try to allocate that page.
      return NoPage;
    }
  }
  if ( ! GPGC_Metadata::expand_metadata_for_heap(block, pages) ) {
    // Can't grow metadata structures for a new page, so can't try to allocate that page.
    return NoPage;
  }

  if ( ! GPGC_CardTable::allocate_cardmark_for_heap(block, new_top) ) {
      return NoPage;
  }

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

  for ( long i=0; i<pages; i++ ) {
    info[i].initialize();
  }

  _top_page = new_top;

  info->set_block_size(pages);

  return block;
}


//  Get a block of pages for allocation.  First try the available pages, then try
//  expanding the space's address range.
PageNum GPGC_MultiPageSpace::select_block(long pages, GPGC_PageInfo::Gens generation)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);
  GPGC_Generation* gen = GPGC_Space::generation(generation);

  PageNum block = get_available_block(pages);

  if ( block == NoPage ) {
    block = get_expansion_block(pages);
  }

  if ( block != NoPage ) {
    if ( GPGCKeepPageStats ) {
      _allocated_pages += pages;
    }

    // the page info needs to be initialized under a lock
    // else the block can be put back on the available list
    // when we rebuild the available group list.

    uint8_t flags = gen->marking_underway() ? GPGC_PageInfo::NoRelocateFlag : 0;
    GPGC_PageInfo* pg_info = GPGC_PageInfo::page_info(block);
    for ( long i=1; i<pages; i++ ) {
      pg_info[i].set_block_size(-1 * i);
    }

    // change the block state to allocated under the lock
    pg_info->set_gen_and_state(generation, GPGC_PageInfo::Allocated);
    // set the NoRelocate flag so iterators through this space
    // will ignore this block
    pg_info->set_flags_non_atomic(flags);
    // clear entries in trap array..
    bool is_new_gen_page = (generation == GPGC_PageInfo::NewGen);
    bool in_large_space  = true;
    GPGC_ReadTrapArray::init_trap(block, pg_info->block_size(), is_new_gen_page, in_large_space);

  }

  return block;
}


void GPGC_MultiPageSpace::return_available_block(PageNum block)
{
  assert0(block != NoPage);

MutexLocker ml(GPGC_MultiPageSpace_lock);

  // Rather then trying to reinsert the available block into the data structures,
  // we wait for the next TLB resync:
hold_block_for_TLB_resync(block);
}


void GPGC_MultiPageSpace::hold_block_for_TLB_resync(PageNum block)
{
assert_lock_strong(GPGC_MultiPageSpace_lock);
  assert0(block != NoPage);

  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);

  long pages = info->block_size();  // Ok to read here, only written under lock
  assert0(pages > 1);

  // Link the block in on the front of the list:
  info->set_ll_next(_blocks_on_hold);
_blocks_on_hold=block;

  if (GPGCKeepPageStats) {
    _allocated_pages -= pages;
    _held_pages      += pages;
  }
}


void GPGC_MultiPageSpace::prepare_for_TLB_resync()
{
  assert0(_pending_available_blocks_head == NoPage);

  {
MutexLocker ml(GPGC_MultiPageSpace_lock);

    _pending_available_blocks_head = _blocks_on_hold;
    _blocks_on_hold                = NoPage;

    if (GPGCKeepPageStats) {
      _held_pages = 0;
    }
  }
}


void GPGC_MultiPageSpace::TLB_resync_occurred()
{
  if ( _pending_available_blocks_head == NoPage ) return;

  // We could walk through the list of blocks, aggregating each one with
  // adjacent available page blocks, and adding them to the group list.
  // But that would probably be really really slow.
  //
  // Instead, we're going to walk the block list and set the GPGC_PageInfo for
  // for each page to the Available state.  Then we're going to dealloc
  // all the page group structures.  Then we're going to walk the GPGC_PageInfo's
  // of all the pages in the space, and rebuild the available group list.
  //
  // We're hoping that the app isn't alloc heavy, so this wont be too slow.

  PageNum        block;
GPGC_PageInfo*info;

  _pending_available_blocks_head = NoPage;

  // Now we need the lock, as we're about to start changing the MT data structures.
MutexLocker ml(GPGC_MultiPageSpace_lock);

  // First dealloc the present group list.
  GPGC_PageGroup* group  = _available_blocks;
while(group!=NULL){
    GPGC_PageGroup* next = group->grp_next();
FreeHeap(group);
group=next;
  }
_available_blocks=NULL;

  if (GPGCKeepPageStats) {
    _available_pages = 0;
  }

  // Now walk the space's pages building new blocks.
block=_start_page;
  while ( block < _top_page ) {
    GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
    long           pages = info->block_size();  // Ok to read here, only written under lock

    assert0(pages > 0);

    if ( info->just_state() != GPGC_PageInfo::Unmapped) {
      block += pages;
      continue;
    }
 
    info = GPGC_PageInfo::page_info(block+pages);
    while ( info->just_state() == GPGC_PageInfo::Unmapped) {
      pages += info->block_size();
      info->set_block_size(0);
      info = GPGC_PageInfo::page_info(block+pages);
    }

    info = GPGC_PageInfo::page_info(block);
    info->set_block_size(pages);

    add_block_to_page_group(block, pages);

    if (GPGCKeepPageStats) {
      _available_pages += pages;
    }

    block += pages;
  }
}


void GPGC_MultiPageSpace::pin_block(PageNum block)
{
  assert0(GPGC_Layout::large_space_page(block));

MutexLocker ml(GPGC_MultiPageSpace_lock);

  GPGC_PageInfo*        info  = GPGC_PageInfo::page_info(block);
  GPGC_PageInfo::States state = info->just_state();
  uint64_t              flags = info->flags();
  long                  pages = info->block_size();

  // We can only pin pages that are in an Allocated state, and which are already not
  // relocatable.  Otherwise, there's a race where the garbage collector has already decided
  // to relocate a page before we apply the PinnedFlag to it.
  assert0( pages>0 );
  assert0( state==GPGC_PageInfo::Allocated );
  assert0( JavaThread::current()->jvm_locked_by_self() );
  assert ( (flags&GPGC_PageInfo::PinnedFlag)==0, "Trying to pin already pinned block" );

  if ( (flags&GPGC_PageInfo::NoRelocateFlag) == 0 ) {
    GPGC_PageInfo::Gens gen = info->just_gen();
    long                gc_state;

    if ( gen == GPGC_PageInfo::NewGen ) {
      gc_state = GPGC_NewCollector::collection_state();
    } else {
      assert0( gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen );
      gc_state = GPGC_OldCollector::collection_state();
    }

    // TODO: I have a guarantee here because I'm not yet certain that there isn't a window where this could
    //       be wrong, and so I want the sanity checking on in all build flavors.
    guarantee( gc_state<GPGC_Collector::InitialMarkSafepoint || gc_state>GPGC_Collector::RelocationSafepoint,
"Attempting to ping block that could be relocated by the collector.");
  }

  info->atomic_add_pinned();
}


void GPGC_MultiPageSpace::unpin_block(PageNum block)
{
  assert0(GPGC_Layout::large_space_page(block));

MutexLocker ml(GPGC_MultiPageSpace_lock);

  GPGC_PageInfo*        info  = GPGC_PageInfo::page_info(block);
  long                  pages = info->block_size();
  GPGC_PageInfo::States state = info->just_state();

  assert0( pages>0 );
  assert0( state==GPGC_PageInfo::Allocated );

  info->atomic_subtract_pinned();
}


void GPGC_MultiPageSpace::page_and_frag_words_count(long generation_mask, long* page_count_result, long* frag_words_result)
{
  long pages      = 0;
  long frag_words = 0;
  long block_size;

  PageNum block;

MutexLocker ml(GPGC_MultiPageSpace_lock);

  for ( PageNum block=_start_page; block<_top_page; block+=block_size) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    // We need to make sure that we see valid data in the other fields of GPGC_PageInfo.
    Atomic::read_barrier();

    block_size = info->block_size();

    if ( (gen&generation_mask) && (state==GPGC_PageInfo::Allocated) ) {
      // Consider anything from info->top() to the end of the page to be fragmented words that
      // are unusable.  It's possible that relocating the page will reduce the fragmentation,
      // but there's not easy way to tell without trying it.
      // we should ignore NoRelocate blocks inorder to not pollute this stat

      if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) 
         continue;
HeapWord*top=info->top();
      HeapWord* next_block  = GPGC_Layout::PageNum_to_addr(block + block_size);

pages+=block_size;
      frag_words += next_block - top;
    }
  }

  *page_count_result = pages;
  *frag_words_result = frag_words;
}


void GPGC_MultiPageSpace::new_gc_collect_blocks(int64_t promotion_threshold_time,
                                                GPGC_PopulationArray* surviving_blocks,
                                                GPGC_PopulationArray* promoting_blocks)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);
    long                  frag_words  = 0;

    pages = info->block_size();  // Okay to read without fence, only set under lock

assert(pages>0,"multi-space blocks can't be zero length");
    assert(state != GPGC_PageInfo::Allocating, "multi-space blocks are always fully allocated");

    if ( gen==GPGC_PageInfo::NewGen && state==GPGC_PageInfo::Allocated ) {
      Atomic::read_barrier();  // Don't read the rest of the GPGC_PageInfo until after a LDLD fence.

      // Consider anything from info->top() to the end of the page to be fragmented words that
      // are unusable.  It's possible that relocating the page will reduce the fragmentation,
      // but there's not easy way to tell without trying it.
HeapWord*top=info->top();
      HeapWord* next_block = GPGC_Layout::PageNum_to_addr(block + pages);
      frag_words           = next_block - top;

      if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) {
        surviving_blocks->add_no_relocate_pages(pages);
        continue;
      }

      if ( info->flags() & GPGC_PageInfo::PinnedFlag ) {
        surviving_blocks->add_no_relocate_pages(pages);
        continue;
      }

      long raw_stats  = info->raw_stats();
      long live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);

      if ( live_words == 0 ) {
        // The block found is empty, and should be released.
        surviving_blocks->add_empty_block(block, pages, GPGC_NewCollector::relocation_spike());
      } else {
        int64_t page_time = info->time();

        if ( page_time < promotion_threshold_time ) {
          surviving_blocks->add_block(block, info->time(), pages, live_words, frag_words);
        } else {
          promoting_blocks->add_block(block, info->time(), pages, live_words, frag_words);
        }
      }
    }
  }
}


void GPGC_MultiPageSpace::old_gc_collect_blocks(GPGC_PopulationArray* old_blocks, GPGC_PopulationArray* perm_blocks)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);
    long                  frag_words  = 0;

    pages = info->block_size();  // Okay to read without fence, only set under lock

assert(pages>0,"multi-space blocks can't be zero length");
    assert(state != GPGC_PageInfo::Allocating, "multi-space blocks are always fully allocated");

    GPGC_PopulationArray* current_array;

    if      ( gen == GPGC_PageInfo::OldGen )  { current_array = old_blocks; }
    else if ( gen == GPGC_PageInfo::PermGen ) { current_array = perm_blocks; }
    else { continue; }

    if ( state != GPGC_PageInfo::Allocated ) {
      continue;
    }

    Atomic::read_barrier();  // Don't read the rest of the GPGC_PageInfo until after a LDLD fence.

    // Consider anything from info->top() to the end of the page to be fragmented words that
    // are unusable.  It's possible that relocating the page will reduce the fragmentation,
    // but there's not easy way to tell without trying it.
HeapWord*top=info->top();
    HeapWord* next_block = GPGC_Layout::PageNum_to_addr(block + pages);
    frag_words           = next_block - top;

    if ( GPGCNoPermRelocation && gen==GPGC_PageInfo::PermGen ) {
      continue;
    }

    if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) {
      // PermGen blocks are never relocated, so we don't expect the NoRelocate mode
      ShouldNotReachHere();
    }

    long raw_stats  = info->raw_stats();
    long live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);
    long live_objs  = GPGC_PageInfo::live_objs_from_raw(raw_stats);

    if ( live_objs == 0 ) {
      // The block found is empty, and should be released.
      current_array->add_empty_block(block, pages, GPGC_OldCollector::relocation_spike());
    } else {
      current_array->add_block(block, info->time(), pages, live_words, frag_words);
    }
  }
}


void GPGC_MultiPageSpace::clear_no_relocate(long generation_mask)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages ) {
    GPGC_PageInfo*      info = GPGC_PageInfo::page_info(block);
    GPGC_PageInfo::Gens gen  = info->just_gen();

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    if ( gen & generation_mask ) {
      Atomic::read_barrier();  // Don't read the rest of the GPGC_PageInfo until after a LDLD fence.

      uint64_t flags = info->flags();

      if ( flags & GPGC_PageInfo::NoRelocateFlag ) {
        info->atomic_clear_flag( GPGC_PageInfo::NoRelocateFlag );
      }
    }
  }
}


void GPGC_MultiPageSpace::clear_page_marks(long generation_mask)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  guarantee(_start_page==GPGC_Layout::start_of_large_space, "Large space start failed sanity check");

  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    if ( gen & generation_mask ) {
      assert(state != GPGC_PageInfo::Allocating, "multi-space blocks are always fully allocated");

      // TODO: maw: is there a race here?  What if there is a deallocated page that matches the gen we're looking
      // for.  It could get allocated and processed by the other collector while we're zeroing the raw stats.  Not
      // likely timing wise, just do the the length of a GC cycle.  But we probably don't need to clear the raw
      // stats of anything other than allocated pages.
      info->zero_raw_stats();

      if ( state == GPGC_PageInfo::Allocated ) {
        // Clear just the liveness marks, and preserve the card marks.
        GPGC_Marks::clear_live_marks_bitmap(block, pages);
      }
    }
  }
}


void GPGC_MultiPageSpace::clear_verify_marks()
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    assert(state != GPGC_PageInfo::Allocating, "multi-space blocks are always fully allocated");

    if ( state == GPGC_PageInfo::Allocated ) {
      GPGC_Marks::clear_verify_marks_bitmap(block, pages);
    }
  }
}


void GPGC_MultiPageSpace::verify_no_live_marks(long generation_mask)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    if ( gen & generation_mask ) {
      assert(state != GPGC_PageInfo::Allocating, "multi-space blocks are always fully allocated");

      Atomic::read_barrier();   // Don't look at the rest of the GPGC_PageInfo until after a LDLD fence.

      // we should ignore NoRelocate blocks 
      if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) 
        continue;

      long raw_stats = info->raw_stats();
      long live_objs = GPGC_PageInfo::live_objs_from_raw(raw_stats);
      if ( live_objs != 0 ) {
        long live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);
        gclog_or_tty->print_cr("Page 0x%lX has %ld live objs, %ld live words", block, live_objs, live_words);
guarantee(false,"stop");
      }

      if ( state == GPGC_PageInfo::Allocated ) {
        long live_marks_count = GPGC_Marks::count_live_marks(block, pages);
        if ( live_marks_count != 0 ) {
          gclog_or_tty->print_cr("Page 0x%lX has live_objs stats %ld, and %ld live marks", block, live_objs, live_marks_count);
guarantee(false,"stop");
        }
      }
    }
  }
}


// Make a cardmark scanning task for each block in the space.  This does not happen
// at a safepoint, so it needs to deal with PermGen pages being allocated as the space
// is scanned.
long GPGC_MultiPageSpace::make_cardmark_tasks(PGCTaskQueue* q)
{
  long count = 0;

MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;

  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    if ( state!=GPGC_PageInfo::Allocating && state!=GPGC_PageInfo::Allocated ) {
      continue;
    }
    if ( gen == GPGC_PageInfo::NewGen ) {
      continue;
    }

    if ( info->flags() & GPGC_PageInfo::NoRelocateFlag )  {
      // TODO: verify this
      continue;
    }

HeapWord*top=info->top();

    // For each Allocated or Allocating block, make cardmark scanning tasks:
    if ( pages > 40 ) {
      long threads_chunks = GenPauselessNewThreads * 4;
      long pages_chunks   = pages / 10;
      
      long chunks = MIN2(threads_chunks, pages_chunks);

      for ( long chunk=0; chunk<chunks; chunk++ ) {
        q->enqueue(new GPGC_MultiPageScanCardMarksTask(block, chunk, chunks));
      }
    } else {
      q->enqueue(new GPGC_MultiPageScanCardMarksTask(block, 0, 1));
    }
    count ++;
  }

  return count;
}


void GPGC_MultiPageSpace::object_iterate(GPGC_PageInfo::Gens generation, ObjectClosure* cl)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    pages;
  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, only set under lock
    assert0(pages>0);

    if ( gen != generation ) continue;

    assert0(state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated);

    HeapWord* p = GPGC_Layout::PageNum_to_addr(block);

    Atomic::read_barrier();  // Don't look at the rest of the GPGC_PageInfo until after a LDLD fence.

    if ( p < info->top() ) {
      cl->do_object(oop(p));
      p += oop(p)->size();
    }

    assert0(p == info->top());
  }
}


// JVMTI Heap Iteration support
void GPGC_MultiPageSpace::make_heap_iteration_tasks(ObjectClosure* closure, PGCTaskQueue* q)
{
  // TODO: Why aren't we grabbing the GPGC_MultiPageSpace_lock?  We may be at a safepoint, but can
  // we be sure we're not going to clash with a garbage collector?  It'd be better to be safe and
  // grab the lock.
assert(SafepointSynchronize::is_at_safepoint(),"JvmtiHeapIterate only at a safepoint");

  PageNum block;
  long    pages;

  for (block = _start_page; block < _top_page; block += pages)
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size();  // Okay to read without fence, at safepoint
    assert0(pages>0);

    if (state != GPGC_PageInfo::Allocating && state != GPGC_PageInfo::Allocated) continue;

    // For each Allocated or Allocating block, make an iterate task: 
    q->enqueue(new GPGC_MultiPageHeapIterateTask(block, closure));
  }
}


void GPGC_MultiPageSpace::sum_raw_stats(GPGC_PageInfo::Gens generation_mask, uint64_t* obj_count_result, uint64_t* word_count_result)
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  uint64_t objects = 0;
  uint64_t words   = 0;

  PageNum  block;
  long     pages;

  for ( block=_start_page; block<_top_page; block+=pages )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    pages = info->block_size(); 

    if ( (gen&generation_mask)!=0 && state==GPGC_PageInfo::Allocated ) {
      uint64_t raw = info->raw_stats();

      objects += GPGC_PageInfo::live_objs_from_raw (raw);
      words   += GPGC_PageInfo::live_words_from_raw(raw);
    }
  }

  *obj_count_result  = objects;
  *word_count_result = words;
}


// Count up the number of pages with physical memory backing them.
long GPGC_MultiPageSpace::verify_page_budgets()
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  long    pages = 0;

  PageNum block;
  long    size;
  for ( block=_start_page; block<_top_page; block+=size )
  {
    GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

    size = info->block_size();  // Okay to read without fence, only set under lock
    assert0(size>0);

    // we should ignore NoRelocate blocks inorder to not pollute this stat
    if ( info->flags() & GPGC_PageInfo::NoRelocateFlag )  {
      continue;
    }

    if ( info->just_state()==GPGC_PageInfo::Allocated ) {
pages+=size;
    }
  }

  return pages;
}


void GPGC_MultiPageSpace::delete_page_group(GPGC_PageGroup* group)
{
  if ( group->grp_next() != NULL ) {
    group->grp_next()->set_grp_prev( group->grp_prev() );
  }
  if ( group->grp_prev() != NULL ) {
    group->grp_prev()->set_grp_next( group->grp_next() );
  } else {
    _available_blocks = group->grp_next();
  }

FreeHeap(group);
}


void GPGC_MultiPageSpace::add_block_to_page_group(PageNum block, long pages)
{
  // See if there's already a page group with the right block size.
GPGC_PageGroup*prev=NULL;
  GPGC_PageGroup* cursor = _available_blocks;

  while ( cursor && cursor->block_size()<pages ) {
prev=cursor;
    cursor = cursor->grp_next();
  }

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(block);

  if ( cursor && cursor->block_size()==pages ) {
    // Insert new block into existing group
    assert0( cursor->first_block() != NoPage );
    info->set_ll_next( cursor->first_block() );
    cursor->set_first_block( block );
  }
  else {
    // Create a new page group, and shove it into the group list.
    GPGC_PageGroup* group = new GPGC_PageGroup(pages, block);
    group->set_grp_prev( prev );
    group->set_grp_next( cursor );
    if ( prev == NULL ) {
_available_blocks=group;
    } else {
      prev->set_grp_next( group );
    }
    if ( cursor ) {
      cursor->set_grp_prev( group );
    }

    info->set_ll_next( NoPage );
  }

  info->set_block_size(pages);
}


void GPGC_MultiPageSpace::setup_read_trap_array()
{
MutexLocker ml(GPGC_MultiPageSpace_lock);

  PageNum block;
  long    size;

  for ( block=_start_page; block<_top_page; block+=size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(block);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    size = info->block_size();  // Okay to read without fence, only set under lock
    assert0(size>0);

    bool in_large_space = true;
    bool is_new_gen     = (gen == GPGC_PageInfo::NewGen);
    long upcoming_nmt   = is_new_gen ? GPGC_NMT::upcoming_new_nmt_flag() : GPGC_NMT::upcoming_old_nmt_flag();

    if (state > GPGC_PageInfo::Unmapped) {
      GPGC_ReadTrapArray::set_trap_state(block, size, is_new_gen, state, in_large_space, upcoming_nmt);
    }
  }
}
