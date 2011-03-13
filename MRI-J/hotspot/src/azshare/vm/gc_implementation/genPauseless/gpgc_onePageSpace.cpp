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

//
#include "gpgc_cardTable.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_metadata.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_onePageSpace.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_tasks.hpp"
#include "java.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "gpgc_population.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"

// The real initialization is in the initialize() method.
GPGC_OnePageSpace::GPGC_OnePageSpace()
{
  _small_space           = true;
  _block_size            = 0;
  _start_page            = _end_page = _top_page = NoPage;
  _available_pages_list  = 0;
  _available_pages_list &= ~PageNumMaskInPlace; 
  _available_pages_list |= (NoPage & PageNumMaskInPlace);
  _pages_on_hold         = NoPage;

  _pending_available_pages_head = _pending_available_pages_tail = NoPage;
  _pending_available_pages_count = 0;

  _allocated_pages_count = _available_pages_count = _held_pages_count = 0;
}


void GPGC_OnePageSpace::initialize(PageNum start, PageNum end, bool small_space)
{
  if ( start >= end ) {
vm_exit_during_initialization("Invalid page range in GPGC_OnePageSpace");
  }

  _small_space  = small_space;
_start_page=start;
_end_page=end;
_top_page=start;

  if ( small_space ) {
    _block_size = 1;
  } else {
    _block_size = PagesPerMidSpaceBlock;

    PageNum aligned_start = (_start_page >> LogPagesPerMidSpaceBlock) << LogPagesPerMidSpaceBlock;
    PageNum aligned_end   = (_end_page   >> LogPagesPerMidSpaceBlock) << LogPagesPerMidSpaceBlock;

    if ( _start_page != aligned_start ) {
vm_exit_during_initialization("Mid space start page not aligned to mid space block size");
    }
    if ( _end_page != aligned_end ) {
vm_exit_during_initialization("Mid space end page not aligned to mid space block size");
    }
  }

  _available_pages_list  = 0;
  _available_pages_list &= ~PageNumMaskInPlace; 
  _available_pages_list |= (NoPage & PageNumMaskInPlace);

  _pages_on_hold         = NoPage;

  _pending_available_pages_head  = NoPage;
  _pending_available_pages_tail  = NoPage;
  _pending_available_pages_count = 0;

  _allocated_pages_count = 0;
  _available_pages_count = 0;
  _held_pages_count      = 0;
}


//  Get a page from the list of available pages.  Return NoPage if the list is empty.
PageNum GPGC_OnePageSpace::get_available_page()
{
  long spin_counter            = 0;

  // Try to CAS the head off the _available_pages_list.
  PageNum available_pages_list = _available_pages_list;
  PageNum head                 = decode_pagenum(available_pages_list);

  while ( head != NoPage ) {
    GPGC_PageInfo* info        = GPGC_PageInfo::page_info(head);
    PageNum        new_head    = info->ll_next();
    uint64_t       old_tag     = decode_tag(available_pages_list);
    uint64_t       new_tag     = (old_tag + 1) & TagMask;
    new_head                   = new_head | PageNum(new_tag << TagShift); 

    if ( available_pages_list == Atomic::cmpxchg(new_head, (jlong*)&_available_pages_list, available_pages_list) ) {
      // Successfully CAS'ed a PageNum off the head of the list.
      info->set_ll_next(NoPage);

      if (GPGCKeepPageStats) {
        Atomic::dec_ptr((intptr_t*)&_available_pages_count);
      }

      if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times selecting available GPGC_OnePageSpace page",spin_counter);
      }

      assert0(info->just_state() == GPGC_PageInfo::Unmapped);
      
      return head;
    }

    spin_counter ++;

    available_pages_list = _available_pages_list;
    head                 = decode_pagenum(available_pages_list);
  }

  return NoPage;
}


//  Expand the range of pages used by the space, and return a PageNum for use.  Return
//  NoPage if the address range of the space is full.
PageNum GPGC_OnePageSpace::get_expansion_page()
{
  long spin_counter = 0;

  // Try to expand the range of pages in use with CAS.
  PageNum top = _top_page;
  while ( top < _end_page) 
  {
    PageNum new_top = top + _block_size;

    // Make sure we can get a GPGC_PageInfo for the new page.
    for ( long i=0; i<_block_size; i++ ) {
      if ( ! GPGC_PageInfo::expand_pages_in_use(top+i) ) {
        return NoPage;
      }
    }
    if ( ! GPGC_Metadata::expand_metadata_for_heap(top, _block_size) ) {
      // Couldn't allocate metadata for the new page(s).
      return NoPage;
    }

    // Exapand the card marking structures if necessary. 
    if ( ! GPGC_CardTable::allocate_cardmark_for_heap(top, new_top) ) {
      return NoPage;
    }

    if ( top == Atomic::cmpxchg(new_top, (jlong*)&_top_page, top) ) {
      // Successfully CAS'ed forward the _top_page index.
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(top);
      for ( long i=0; i<_block_size; i++ ) {
        info[i].initialize();
      }

      info = GPGC_PageInfo::page_info(top);
      info->set_just_state(GPGC_PageInfo::Unmapped);

      if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times expanding GPGC_OnePageSpace",spin_counter);
      }

      return top;
    }

    spin_counter ++;
top=_top_page;
  }

  return NoPage;
}


//  Get a page for allocation.  First try the available_pages list, then try expanding
//  the space's address range.
PageNum GPGC_OnePageSpace::select_page()
{
  PageNum page = get_available_page();

  if ( page == NoPage ) {
    page = get_expansion_page();
  }

  if ( GPGCKeepPageStats && page!=NoPage ) {
    Atomic::inc_ptr((intptr_t*)&_allocated_pages_count);
  }

  return page;
}
  

//  This method adds a page to the list of pages that are immediately available
//  for allocation.
void GPGC_OnePageSpace::return_available_page(PageNum page)
{
  assert0(page != NoPage);

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  long    spin_counter = 0;
  PageNum old_head = NoPage;

  while (true)
  {
    PageNum available_pages_list = _available_pages_list;
    PageNum old_head = decode_pagenum(available_pages_list);
    info->set_ll_next(old_head);
    PageNum new_head = page | (available_pages_list & TagMaskInPlace); // preserve tag bits
    // Make sure the ll_next is set before we write the head:
    Atomic::write_barrier();

    if ( available_pages_list == Atomic::cmpxchg(new_head, (jlong*)&_available_pages_list, available_pages_list) ) {
      // Successfully CAS'ed a new head page to the _available_pages_list.
      if (GPGCKeepPageStats) {
        Atomic::dec_ptr((intptr_t*)&_allocated_pages_count);
        Atomic::inc_ptr((intptr_t*)&_available_pages_count);
      }

      if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times adding available GPGC_OnePageSpace page",spin_counter);
      }

      return;
    }

    spin_counter ++;
  }
}


//  This method adds a page to the list of pages that are unused, but which cannot be
//  allocated until after the next TLB resync.
void GPGC_OnePageSpace::hold_page_for_TLB_resync(PageNum page)
{
  assert0(page != NoPage);

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

  long    spin_counter = -1;
  PageNum old_head;

  do {
    spin_counter ++;
    old_head = _pages_on_hold;
    info->set_ll_next(old_head);
    Atomic::write_barrier();     // Make sure ll_next is set before CAS'ing the head.
  } while ( old_head != Atomic::cmpxchg(page, (jlong*)&_pages_on_hold, old_head) );

  // Successfully CAS'ed a new head page to the _pages_on_hold list.
  if (GPGCKeepPageStats) {
    Atomic::dec_ptr((intptr_t*)&_allocated_pages_count);
    Atomic::inc_ptr((intptr_t*)&_held_pages_count);
  }

  if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times holding a GPGC_OnePageSpace page for TLB resync",spin_counter);
  }
}


//  This method is sets aside the current list of pages being held for a TLB resync.  The
//  saved list of pages will be added to the list of pages available for allocation when
//  the TLB_resync_occurred() call is made.
//
//  This call is MT-safe with the call hold_page_for_TLB_resync(), but not with itself
//  or TLB_resync_occurred().
void GPGC_OnePageSpace::prepare_for_TLB_resync()
{
  // TODO: maw: verify that NewGC and OldGC won't race between hold_page_for_TLB_resync,
  //            prepare_for_TLB_resync, and TLB_resync_occurred.
  assert0(_pending_available_pages_head == NoPage);

  if ( _pages_on_hold == NoPage ) {
    return;
  }

  long    spin_counter = 0;
  PageNum head;

  do {
    spin_counter ++;
head=_pages_on_hold;
  } while ( head != Atomic::cmpxchg(NoPage, (jlong*)&_pages_on_hold, head) );

  // make sure we are reading the right _next fields in the page_info structure.
  Atomic::read_barrier();

  if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times preparing GPGC_OnePageSpace for TLB resync",spin_counter);
  }

  PageNum        tail  = head;
  long           pages = 1;
  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(tail);

  while ( info->ll_next() != NoPage )
  {
    tail  = info->ll_next();
    pages ++;
    info  = GPGC_PageInfo::page_info(tail);
  }

  if (GPGCKeepPageStats) {
    Atomic::add_ptr(-1 * pages, (intptr_t*)&_held_pages_count);
  }

_pending_available_pages_head=head;
  _pending_available_pages_tail  = tail;
  _pending_available_pages_count = pages;

  // Make sure the class fields are visible to everyone:
  Atomic::write_barrier();
}


//  This method is called after the program has done a TLB resync.  It transfers the list
//  of pages set aside by prepare_for_TLB_resync() into the list of pages available for
//  allocation.
//
//  This call is MT-safe with the call select_page(), but not with itself or
//  prepare_for_TLB_resync().
void GPGC_OnePageSpace::TLB_resync_occurred()
{
  if ( _pending_available_pages_head == NoPage ) return;
 
  GPGC_PageInfo* info         = GPGC_PageInfo::page_info(_pending_available_pages_tail);
  long           spin_counter = -1;
  PageNum        old_head = NoPage;
  PageNum        available_pages_list = NoPage;
  PageNum        new_head = NoPage;

  do {
    spin_counter ++;
    available_pages_list = _available_pages_list;
    old_head = decode_pagenum(available_pages_list);
    info->set_ll_next(old_head);
    new_head = _pending_available_pages_head | (available_pages_list & TagMaskInPlace); // preserve tag
    Atomic::write_barrier();         // Make sure the ll_next is set before CAS'ing the head.
  } while (available_pages_list != Atomic::cmpxchg(new_head, (jlong*)&_available_pages_list, available_pages_list));

  // Successfully CAS'ed the pending pages on to the front of the _available_pages_list.
  if ( spin_counter>10 && PrintGCDetails ) {
gclog_or_tty->print_cr("spun "INTX_FORMAT" times moving pending GPGC_OnePageSpace pages to available",spin_counter);
  }
  
  if (GPGCKeepPageStats) {
    Atomic::add_ptr(_pending_available_pages_count, (intptr_t*)&_available_pages_count);
  }

  _pending_available_pages_head  = NoPage;
  _pending_available_pages_tail  = NoPage;
  _pending_available_pages_count = 0;

  // Make sure the class fields are visible to everyone:
  Atomic::write_barrier();
}


void GPGC_OnePageSpace::pin_page(PageNum page)
{
  assert0( GPGC_Layout::mid_space_page(page) );
  assert0( page == GPGC_Layout::page_to_MidSpaceBasePageNum(page) );

  GPGC_PageInfo*        info  = GPGC_PageInfo::page_info(page);
  GPGC_PageInfo::States state = info->just_state();
  uint64_t              flags = info->flags();

  // We can only pin pages that are in an Allocating/Allocated state, and which are already not
  // relocatable.  Otherwise, there's a race where the garbage collector has already decided to
  // relocate a page before we apply the PinnedFlag to it.
  assert0( state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating );
  assert0( JavaThread::current()->jvm_locked_by_self() );

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
"Attempting to pin page that could be relocated by the collector.");
  }

  info->atomic_add_pinned();
}


void GPGC_OnePageSpace::unpin_page(PageNum page)
{
  assert0( GPGC_Layout::mid_space_page(page) );
  assert0( page == GPGC_Layout::page_to_MidSpaceBasePageNum(page) );

  GPGC_PageInfo*        info  = GPGC_PageInfo::page_info(page);
  GPGC_PageInfo::States state = info->just_state();

  assert0( state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating );

  info->atomic_subtract_pinned();
}


void GPGC_OnePageSpace::page_and_frag_words_count(long generation_mask, long* page_count_result, long* frag_words_result)
{
  long pages      = 0;
  long frag_words = 0;

  for ( PageNum page=_start_page; page<_top_page; page+=_block_size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( (gen&generation_mask) && (state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating) ) {
      // We need to make sure that we see valid data in the other fields of GPGC_PageInfo.
      Atomic::read_barrier();

      if ( small_space() ) {
        pages ++;
        if ( state == GPGC_PageInfo::Allocated ) {
          // We only collect fragmentation info for Allocated state pages.
          frag_words += GPGC_Layout::PageNum_to_addr(page+1) - info->top();
        }
      } else {
        // We're in mid space.
HeapWord*page_top=info->top();
        PageNum   top_allocated_page = GPGC_Layout::addr_to_PageNum( page_top - 1 );
        long      allocated_pages    = 1 + top_allocated_page - page;

        pages += allocated_pages;

        if ( state == GPGC_PageInfo::Allocated ) {
          // We only collect fragmentation info for Allocated state pages.

          // In mid space, there's two kinds of fragmentation: 
          //   - Per object fragmentation from the end of an object to the next 4KB page.
          //   - Per block fragmentation from the end of top() to the next 2MB page.
          // This function only reports the 2nd type of fragmentation.
          HeapWord* top_alloced = GPGC_Layout::PageNum_to_addr(top_allocated_page+1);

          // Per block fragmentation:
          frag_words += top_alloced - page_top;

          // // Per object fragmentation:
          // HeapWord* obj_addr = GPGC_Layout::PageNum_to_addr(page);
          //
          // while ( obj_addr < page_top ) {
          //   intptr_t obj_end  = intptr_t(obj_addr + oop(obj_addr)->size());
          //   intptr_t page_end = align_size_up(obj_end, GPGC_WordsMidSpaceObjectSizeAlignment);
          // 
          //   frag_words += (page_end - obj_end) >> LogBytesPerWord;
          //   obj_addr    = (HeapWord*) obj_end;
          // }
        }
      }
    }
  }

  *page_count_result = pages;
  *frag_words_result = frag_words;
}


//  Scan the pages in the space for empty or sparse allocated pages.  Empty pages are
//  released.  Sparse pages are sent to the GPGC_Population object.
void GPGC_OnePageSpace::new_gc_collect_sparse_populations(int64_t promotion_threshold_time, GPGC_PopulationArray* population, GPGC_PopulationArray* skipped_population)
{
  PageNum page;

  for ( page=_start_page; page<_top_page; page+=_block_size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( gen==GPGC_PageInfo::NewGen && (state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating) ) {
      // We need to make sure that we see valid data in the other fields of GPGC_PageInfo.
      Atomic::read_barrier();

      uint64_t raw_stats  = info->raw_stats();
      uint32_t live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);

      PageNum  top_allocated_page;
uint32_t allocated_pages;
uint32_t occupancy;

      if ( small_space() ) {
        assert0(_block_size == 1);

        top_allocated_page = page;
        allocated_pages    = 1;
        occupancy          = live_words;
      } else {
        assert0(_block_size > 1);

        top_allocated_page = GPGC_Layout::addr_to_PageNum( info->top() - 1 );
        allocated_pages    = 1 + top_allocated_page - page;
        occupancy          = live_words / allocated_pages;
      }

      uint32_t word_size = allocated_pages << LogWordsPerGPGCPage;

      if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) {
        // Some pages are marked unsafe for relocation in the GC cycle.
        if (GPGCTraceSparsePages) gclog_or_tty->print_cr("Page 0x%lX ignored, NoRelocateFlag", page);
        population->add_no_relocate_pages(allocated_pages);
        skipped_population->add_skipped_page(page, allocated_pages, live_words, 0, 0);

        continue;
      }

      if ( info->flags() & GPGC_PageInfo::PinnedFlag ) {
        // Don't relocate pages that have been pinned in place.
        if (GPGCTraceSparsePages) gclog_or_tty->print_cr("Page 0x%lX ignored, PinnedFlag", page);
        population->add_no_relocate_pages(allocated_pages);
        skipped_population->add_skipped_page(page, allocated_pages, live_words, 0, 0);

        continue;
      }

#ifdef ASSERT
      long mark_count = GPGC_Marks::count_live_marks(page, _block_size);
      long live_objs  = GPGC_PageInfo::live_objs_from_raw(raw_stats);
      assert0(mark_count==live_objs);
      assert0(live_words>0 || live_objs==0);
#endif // ASSERT

      if ( live_words == 0 ) {
        // The page found is empty, and should be released.
        population->add_empty_page(allocated_pages);
        GPGC_Space::release_empty_page(page, GPGC_NewCollector::relocation_spike());
      } else {
        // Space in the page is divided into three groups:
        //
        //     Live: Meta-data and live objects.
        //     Dead: Previously allocated objects that are now dead.
        //     Frag: Space at the end allocated to an object.
        //
        // It's possible that relocating the page will reduce the fragmentation,
        // but there's not easy way to tell without trying it.  Unless you solve the
        // knapsack problem.
HeapWord*top=info->top();
        HeapWord* end_word   = GPGC_Layout::PageNum_to_addr(top_allocated_page+1);
        uint32_t  frag_words = end_word - top;
        uint32_t  dead_words = word_size - (frag_words + live_words);
        int64_t   page_time  = info->time();

        assert0(end_word  >= top);
        assert0(word_size >= (frag_words + live_words));

        if (GPGCTraceSparsePages) {
gclog_or_tty->print_cr("Page 0x%lX has live: %u objs, %u words, %u fragmented words, %u garbage words",
                                 page, GPGC_PageInfo::live_objs_from_raw(info->raw_stats()),
                                 live_words, frag_words, dead_words);
        }

        if ( dead_words>0 || page_time<promotion_threshold_time ) {
          population->add_page(page, info->time(), occupancy, live_words, frag_words, dead_words);
        } else {
          skipped_population->add_skipped_page(page, allocated_pages, live_words, frag_words, dead_words);
        }
      }
    }

    //don't expect NewGen pages in the relocated/ing state 
    assert(!( (gen==GPGC_PageInfo::NewGen) && (state==GPGC_PageInfo::Relocated || state==GPGC_PageInfo::Relocating)),
"don't expect NewGen pages in the relocated/ing state");

    if ( (gen & GPGC_PageInfo::OldAndPermMask) && 
        ((state >= GPGC_PageInfo::Allocating) && (state <= GPGC_PageInfo::Relocated)) ) {
      // assert against other states
      // dont care to get the stats for the other gen
      skipped_population->add_skipped_page(page, 1, 0, 0, 0 );
    }
  }
}


//  Scan the pages in the space for empty or sparse allocated pages.  Empty pages are
//  released.  Sparse pages are sent to the GPGC_Population object.  The method returns
//  the number of empty pages released.
void GPGC_OnePageSpace::old_gc_collect_sparse_populations(uint64_t fragment_mask, uint64_t fragment_stripe, GPGC_PopulationArray* population, GPGC_PopulationArray* skipped_population)
{
  PageNum page;

  for ( page=_start_page; page<_top_page; page+=_block_size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( gen!=GPGC_PageInfo::NewGen && (state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating) ) {
      assert0( gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen );

      // We need to make sure that we see valid data in the other fields of GPGC_PageInfo.
      Atomic::read_barrier();

      if ( gen==GPGC_PageInfo::PermGen && (GPGCNoPermRelocation || page==GPGC_Layout::start_of_small_space) ) {
          // We avoid the overhead of relocating the first perm gen page that contains all the
          // initial bootstrap perm gen objects.
          // add to the skipped pages, dont worry about the stats for this page
          skipped_population->add_skipped_page(page, 0, 0, 0, 0);
          continue;
      }

      uint64_t raw_stats  = info->raw_stats();
      uint32_t live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);

      PageNum  top_allocated_page;
uint32_t allocated_pages;
uint32_t occupancy;

      if ( small_space() ) {
        assert0(_block_size == 1);

        top_allocated_page = page;
        allocated_pages    = 1;
        occupancy          = live_words;
      } else {
        assert0(_block_size > 1);

        top_allocated_page = GPGC_Layout::addr_to_PageNum( info->top() - 1 );
        allocated_pages    = 1 + top_allocated_page - page;
        occupancy          = live_words / allocated_pages;
      }

      uint32_t word_size = allocated_pages << LogWordsPerGPGCPage;

      if ( info->flags() & GPGC_PageInfo::NoRelocateFlag ) {
        // Some pages are marked unsafe for relocation in the GC cycle.
        if (GPGCTraceSparsePages) gclog_or_tty->print_cr("Page 0x%lX ignored, NoRelocateFlag", page);
        population->add_no_relocate_pages(allocated_pages);
        skipped_population->add_skipped_page(page, allocated_pages, live_words, 0, 0);
        continue;
      }

#ifdef ASSERT
      long mark_count = GPGC_Marks::count_live_marks(page, _block_size);
      long live_objs  = GPGC_PageInfo::live_objs_from_raw(raw_stats);
      assert0(mark_count==live_objs);
      assert0(live_words>0 || live_objs==0);
#endif // ASSERT

      if ( live_words == 0 ) {
        // The page found is empty, and should be released.
        population->add_empty_page(allocated_pages);
        GPGC_Space::release_empty_page(page, GPGC_OldCollector::relocation_spike());
      } else {
        // Space in the page is divided into three groups:
        //
        //     Live: Meta-data and live objects.
        //     Dead: Previously allocated objects that are now dead.
        //     Frag: Space at the end allocated to an object.
        //
        // Consider anything from info->top() to the end of the page to be fragmented words that
        // are unusable.  It's possible that relocating the page will reduce the fragmentation,
        // but there's no easy way to tell without trying it.
HeapWord*top=info->top();
        HeapWord* end_word   = GPGC_Layout::PageNum_to_addr(top_allocated_page+1);
        uint32_t  frag_words = end_word - top;
        uint32_t  dead_words = word_size - (frag_words + live_words);

        assert0(end_word  >= top);
        assert0(word_size >= (frag_words + live_words));

        if (GPGCTraceSparsePages) {
gclog_or_tty->print_cr("Page 0x%lX has live: %u objs, %u words, %u fragmented words, %u garbage words",
                                 page, GPGC_PageInfo::live_objs_from_raw(info->raw_stats()),
                                 live_words, frag_words, dead_words);
        }

        if ( dead_words > 0 ) {
          population->add_page(page, info->time(), occupancy, live_words, frag_words, dead_words);
        }
        // TODO: Better worst case fragmentation behavior may obviate need for selecting some pages to defrag.
        else if (    (frag_words > ((allocated_pages<<LogWordsPerGPGCPage)/16))
                  && ((page&fragment_mask) == fragment_stripe) ) {
          population->add_page(page, info->time(), occupancy, live_words, frag_words, dead_words);
          population->increment_defrag_pages_selected();
        } else {
          skipped_population->add_skipped_page(page, allocated_pages, live_words, frag_words, dead_words);
        }
      }
    }

    //don't expect OldGen pages in the relocated/ing state 
    assert(!( (gen==GPGC_PageInfo::OldGen) && (state==GPGC_PageInfo::Relocated || state==GPGC_PageInfo::Relocating)),
"don't expect OldGen pages in the relocated/ing state");

    if ( (gen & GPGC_PageInfo::NewGen) && 
        ((state >= GPGC_PageInfo::Allocating) && (state <= GPGC_PageInfo::Relocated)) ) {
      // dont care to get the stats for the other gen
      skipped_population->add_skipped_page(page, 1, 0, 0, 0);
    }
  }
}


//  This method can only be run when pages aren't getting transitioned
//  from allocated/allocating to unmapped!  It's not MT-safe with that.
void GPGC_OnePageSpace::clear_page_marks(long generation_mask, long section, long sections)
{
  PageNum start_page = _start_page + (section * _block_size);
  PageNum increment  = sections * _block_size;

  for ( PageNum page=start_page; page<_top_page; page+=increment )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( (gen&generation_mask) && (state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated) ) {
      info->zero_raw_stats();

      GPGC_Marks::clear_live_marks_bitmap(page, _block_size);
    }
  }
}


void GPGC_OnePageSpace::clear_verify_marks()
{
  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated ) {
      GPGC_Marks::clear_verify_marks_bitmap(page, _block_size);
    }
  }
}


void GPGC_OnePageSpace::verify_no_live_marks(long generation_mask)
{
  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( (gen&generation_mask) && (state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated) ) {
      Atomic::read_barrier();  // Don't look at the rest of the GPGC_PageInfo until after a LDLD fence.

      long raw_stats = info->raw_stats();
      long live_objs = GPGC_PageInfo::live_objs_from_raw(raw_stats);
      if ( live_objs != 0 ) {
        long live_words = GPGC_PageInfo::live_words_from_raw(raw_stats);
        gclog_or_tty->print_cr("Page 0x%lX has %ld live objs, %ld live words", page, live_objs, live_words);
guarantee(false,"stop");
      }

      long live_marks_count = GPGC_Marks::count_live_marks(page, _block_size);
      if ( live_marks_count != 0 ) {
        gclog_or_tty->print_cr("Page 0x%lX has live_objs stats %ld, and %ld live marks", page, live_objs, live_marks_count);
guarantee(false,"stop");
      }
    }
  }
}


// This is really really unsafe!  For a lot of reasons.  Here's an example: after the
// first GC cycle, you may run across pages that are being relocated.  This object_iterate
// function skips those pages.  So objects in the page that haven't already been copied to
// a new location will not be seen.
void GPGC_OnePageSpace::object_iterate(GPGC_PageInfo::Gens generation, ObjectClosure* cl)
{
  // TODO: Put in a guarantee that we're running this at a safe time.  i.e. Before the first GPGC cycle.  Does JVMTI use this method?
  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( gen != generation ) continue;

    assert0(state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated);

    HeapWord* p = GPGC_Layout::PageNum_to_addr(page);

    Atomic::read_barrier();  // Don't read the rest of the GPGC_PageInfo until after a LDLD fence.

    while ( p < info->top() )
    {
      cl->do_object(oop(p));
      p += oop(p)->size();
    }
  }
}


void GPGC_OnePageSpace::make_heap_iteration_tasks(long sections, ObjectClosure* closure, PGCTaskQueue* q)
{
  for (long i = 0; i < sections; i++) {
    q->enqueue(new GPGC_OnePageHeapIterateTask(this, i, sections, closure));
  }
}


void GPGC_OnePageSpace::clear_no_relocate(long generation_mask)
{
  // this code needs to be reworked.. for now getting it to compile
  // this code needs to be reworked.. 
  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( (gen&generation_mask) && (state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated) ) {
      Atomic::read_barrier(); // Don't read the rest of the GPGC_PageInfo until after a LDLD fence.

      uint64_t flags = info->flags();

      if ( flags & GPGC_PageInfo::NoRelocateFlag ) {
        info->atomic_clear_flag( GPGC_PageInfo::NoRelocateFlag );
      }
    }
  }
}


// This diagnostic function verifies the capacity stat for a generation in a space.  It's
// intended for use during the initial marking safepoint, so that mutators allocation won't
// invalidate the result, and so that there are no partially relocated pages or unshattered
// pages to invalidate the result.
long GPGC_OnePageSpace::verify_capacity(GPGC_PageInfo::Gens generation)
{
  guarantee( GPGC_Safepoint::is_at_safepoint(), "Capacity verification isn't reliable outside of a safepoint." );

  long capacity = 0;

  for ( PageNum page=_start_page; page<_top_page; page+=_block_size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( gen == generation ) {
      if ( state==GPGC_PageInfo::Unmapped || state==GPGC_PageInfo::Relocated ) { continue; }

      guarantee(state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated, "Bad page state during capacity verification");

      long allocated_pages;

      if ( _block_size == 1 ) {
        // Small space somtimes has allocated pages empty pages with a top set to the page base, which
        // would result in 0 allocated pages if calculated as is done with mid space pages.
        allocated_pages = 1;
      } else {
HeapWord*page_top=info->top();
        PageNum   top_allocated_page = GPGC_Layout::addr_to_PageNum( page_top - 1 );

        allocated_pages    = 1 + top_allocated_page - page;
      }

      capacity += allocated_pages;

      // Diagnostic output we don't normally use:
      //
      // if ( generation==GPGC_PageInfo::NewGen && _block_size>1 ) {
      //   gclog_or_tty->print_cr("verify NewGen mid: page 0x%lX in use with state %d (allocated %ld)", page, state, allocated_pages);
      // }
    }
  }

  return capacity;
}


// Count and return pages with physical memory behind them.
long GPGC_OnePageSpace::verify_page_budgets()
{
  long pages = 0;

  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);

    if ( state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Relocating ) {
pages+=_block_size;
    }
  }

  return pages;
}


void GPGC_OnePageSpace::create_read_trap_array_tasks(PGCTaskQueue* q, uint64_t tasks)
{
  // We're doing this concurrently, so _top_page might extend mid-function.  Any extended
  // pages will be handled by the extender, so we don't need to worry about them here.
  PageNum top_page        = _top_page;
  long    nof_blocks      = (top_page - _start_page) / _block_size;
  long    blocks_per_task = nof_blocks / tasks;

  if ( blocks_per_task > 0 ) { blocks_per_task = 1; }

  PageNum task_start_page = _start_page;
  long    pages_per_task  = blocks_per_task * _block_size;

  for ( uint64_t i=1; i<=tasks; i++ ) {
    PageNum task_end_page = task_start_page + pages_per_task;

    if ( task_start_page >= top_page ) { break; }
    if ( i == tasks )                  { task_end_page = top_page; }

    q->enqueue(new GPGC_InitReadTrapArrayTask(task_start_page, task_end_page, _block_size, GPGC_ReadTrapArray::NotInLargeSpace));

    task_start_page += pages_per_task;
  }
}


void GPGC_OnePageSpace::sum_raw_stats(GPGC_PageInfo::Gens generation_mask, uint64_t* obj_count_result, uint64_t* word_count_result)
{
  uint64_t objects = 0;
  uint64_t words   = 0;

  for ( PageNum page=_start_page; page<_top_page; page+=_block_size )
  {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if ( (gen&generation_mask)!=0 && state==GPGC_PageInfo::Allocated ) {
      uint64_t raw = info->raw_stats();

      objects += GPGC_PageInfo::live_objs_from_raw (raw);
      words   += GPGC_PageInfo::live_words_from_raw(raw);
    }
  }

  *obj_count_result  = objects;
  *word_count_result = words;
}


// This is a utility function intended to be called from gdb.  It iterates
// over the objects in the specified page, and prints out the addresses of
// the objects it finds.  It is intended to help find the start address of
// an object when you are debugging a crash and only have an interior
// pointer.
void gpgc_page_find_objs(PageNum page)
{
  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
HeapWord*top=info->top();
  HeapWord*      obj  = GPGC_Layout::PageNum_to_addr(page);

  if ( GPGC_Layout::small_space_page(page) ) {
    tty->print_cr("Traversing small page 0x%lX from %p to %p", page, obj, top);
  }
  else if ( GPGC_Layout::mid_space_page(page) ) {
    tty->print_cr("Traversing mid page 0x%lX from %p to %p", page, obj, top);
  }
  else {
tty->print_cr("Page 0x%lX not in small or mid space",page);
tty->flush();
    return;
  }

tty->flush();

while(obj<top){
tty->print_cr("\tFound oop at %p",obj);
tty->flush();
obj+=oop(obj)->size();
  }
}
