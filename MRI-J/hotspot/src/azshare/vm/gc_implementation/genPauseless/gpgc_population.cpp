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


#include "allocation.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"
#include "gpgc_space.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"


void GPGC_RemapTargetArray::initialize(uint32_t max_length)
{
  _allocated_length = max_length;
  _array            = NEW_C_HEAP_ARRAY(RemapTarget, _allocated_length);

  reset();
}


void GPGC_RemapTargetArray::reset()
{
  _current_length         = 0;
}


void GPGC_RemapTargetArray::atomic_add_page(PageNum page, int64_t stripe, GPGC_PageInfo::Gens source_gen)
{
  uint64_t old_length;
  uint64_t new_length;

  do {
    old_length = _current_length;
    new_length = old_length + 1;

    assert0(old_length < _allocated_length);
  } while ( jlong(old_length) != Atomic::cmpxchg(jlong(new_length), (jlong*)&_current_length, jlong(old_length)) );

  _array[old_length].page       = page;
  _array[old_length].stripe     = stripe;
  _array[old_length].source_gen = source_gen;
}


void GPGC_PopulationArray::initialize(uint32_t max_length)
{
  _allocated_length = max_length;
  _array            = NEW_C_HEAP_ARRAY(PagePop, _allocated_length);

  reset();
}


void GPGC_PopulationArray::reset()
{
  _current_length         = 0;

  _cursor                 = 0;
  _max_cursor             = 0;

  _live_words_total       = 0;
  _dead_words_total       = 0;
  _frag_words_total       = 0;
  _block_pages_total      = 0;

  _live_words_selected    = 0;
  _dead_words_selected    = 0;
  _frag_words_selected    = 0;

  _sideband_limited_words = 0;
  _skipped_pages          = 0;
  _released_empty_pages   = 0;
  _norelocate_pages       = 0;
  _defrag_pages_selected  = 0;
}


// When we don't have enough sideband relocation array space to relocate every page
// originally selected for relocation, this method is called.  We reset the the
// relocation cutoff, calculate the amount of garbage not reclaimed by the change,
// and update the size of garbage selected for reclaimation.
void GPGC_PopulationArray::sideband_limit_reclaim_cutoff(uint32_t max_cursor) {
  assert0(_max_cursor >= 0);
  assert0(max_cursor  >= 0);
  assert0(max_cursor < _max_cursor);
  assert0(_sideband_limited_words == 0);

  for ( uint64_t cursor=max_cursor; cursor<_max_cursor; cursor++ ) {
    _sideband_limited_words += _array[cursor].dead_words;
  }

  _max_cursor = max_cursor;
}


// qsort() comparator that sorts pages with the lowest occupancy ratio to the front.
extern "C" int gpgc_qsort_compare_lowest_occupancy_first(const void* a, const void* b)
{
  return   ((GPGC_PopulationArray::PagePop*)a)->occupancy
         - ((GPGC_PopulationArray::PagePop*)b)->occupancy;
}
void GPGC_PopulationArray::sort_by_new_gc_relocation_priority()
{
  qsort(_array, _current_length, sizeof(_array[0]), gpgc_qsort_compare_lowest_occupancy_first);
}


// qsort() comparator that sorts pages with the largest dead words to the front, followed by largest fragment words.
extern "C" int gpgc_qsort_compare_lowest_occupancy_then_largest_frag_first(const void* a, const void* b)
{
  int result =   ((GPGC_PopulationArray::PagePop*)a)->occupancy
               - ((GPGC_PopulationArray::PagePop*)b)->occupancy;

  if ( result == 0 ) {
    result =   ((GPGC_PopulationArray::PagePop*)b)->frag_words
             - ((GPGC_PopulationArray::PagePop*)a)->frag_words;
  }

  return result;
}
void GPGC_PopulationArray::sort_by_old_gc_relocation_priority()
{
  // Sort pages with largest number of dead words to the front, followed by largest fragment words:
  qsort(_array, _current_length, sizeof(_array[0]), gpgc_qsort_compare_lowest_occupancy_then_largest_frag_first);
}


// qsort() comparator that sorts pages with the largest timestamp to the front.
extern "C" int gpgc_qsort_compare_largest_time_first(const void* a, const void* b)
{
  uint64_t a_time = ((GPGC_PopulationArray::PagePop*)a)->time_stripe;
  uint64_t b_time = ((GPGC_PopulationArray::PagePop*)b)->time_stripe;

  int      result = (b_time>a_time) ? 1 : (b_time==a_time) ? 0 : -1;

  return result;
}
void GPGC_PopulationArray::sort_by_largest_time_first()
{
  // TODO: make sure the _current_length is truncated down to the limit of the array we're sorting on.
  qsort(_array, _current_length, sizeof(_array[0]), gpgc_qsort_compare_largest_time_first);
}


void GPGC_PopulationArray::assert_only_allocated_pages()
{
  for ( uint64_t cursor=0; cursor<max_cursor(); cursor++ ) {
    PageNum        page = this->page(cursor);
    GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);

    assert0(info->just_state() == GPGC_PageInfo::Allocated );
  }
}


// Atomically advance the cursor, claiming a number of indicies for the local thread to act upon.  Returns
// false if all indicies have been claimed, or true if some new indicies have been claimed.
bool GPGC_PopulationArray::atomic_claim_array_chunk(uint64_t& start_cursor, uint64_t& end_cursor, long work_unit)
{
  start_cursor = 0;
  end_cursor   = 0;

  uint64_t max_cursor;
  uint64_t cursor;
  uint64_t new_cursor;

  do {
    max_cursor = this->max_cursor();
    cursor     = this->cursor();
    if ( cursor >= max_cursor ) {
      return false;
    }
    new_cursor = cursor + work_unit;
    if ( new_cursor > max_cursor ) {
      new_cursor = max_cursor;
    }
  } while ( jlong(cursor) != Atomic::cmpxchg(jlong(new_cursor), (jlong*)this->cursor_addr(), jlong(cursor)) );

  start_cursor = cursor;
  end_cursor   = new_cursor;

  return true;
}


// initialize should be called with the maximum number of pages that the page population
// class needs to handle at once.  On a 128GB systems, 131072 would clearly be an upper
// bound.
void GPGC_Population::initialize(uint32_t max_small_space_pages, uint32_t max_mid_space_pages, uint32_t max_large_space_blocks)
{
  _all_populations.initialize(max_small_space_pages);

  _small_space_1.initialize(max_small_space_pages);
  _small_space_2.initialize(max_small_space_pages);
  _small_space_skipped_pages.initialize(max_small_space_pages);
  _mid_space_1.initialize  (max_mid_space_pages);
  _mid_space_2.initialize  (max_mid_space_pages);
  _mid_space_skipped_pages.initialize(max_mid_space_pages);
  _large_space_1.initialize(max_large_space_blocks);
  _large_space_2.initialize(max_large_space_blocks);
  _large_space_skipped_pages.initialize(max_large_space_blocks);

  _mid_space_targets.initialize(max_mid_space_pages);
}


void GPGC_Population::reset_populations()
{
_all_populations.reset();

_small_space_1.reset();
_small_space_2.reset();
_small_space_skipped_pages.reset();
_mid_space_1.reset();
_mid_space_2.reset();
_mid_space_skipped_pages.reset();
_large_space_1.reset();
_large_space_2.reset();
_large_space_skipped_pages.reset();
  
_mid_space_targets.reset();

  _source_pages_to_new_gen    = 0;
  _target_pages_in_new_gen    = 0;
  _source_pages_to_old_gen    = 0;
  _target_pages_in_old_gen    = 0;
}


void GPGC_Population::reset_max_cursor()
{
  _all_populations.reset_max_cursor();

  _small_space_1.reset_max_cursor();
  _small_space_2.reset_max_cursor();
  _mid_space_1.reset_max_cursor();
  _mid_space_2.reset_max_cursor();
  _large_space_1.reset_max_cursor();
  _large_space_2.reset_max_cursor();
}


void GPGC_Population::reset_cursor()
{
  _all_populations.reset_cursor();

  _small_space_1.reset_cursor();
  _small_space_2.reset_cursor();
  _small_space_skipped_pages.reset_cursor();
  _mid_space_1.reset_cursor();
  _mid_space_2.reset_cursor();
  _mid_space_skipped_pages.reset_cursor();
  _large_space_1.reset_cursor();
  _large_space_2.reset_cursor();
  _large_space_skipped_pages.reset_cursor();
}


void GPGC_Population::atomic_increment_source_pages_to_new_gen(long pages)
{
  Atomic::add_ptr(pages, (intptr_t*) &_source_pages_to_new_gen);
}


void GPGC_Population::atomic_increment_target_pages_in_new_gen(long pages)
{
  Atomic::add_ptr(pages, (intptr_t*) &_target_pages_in_new_gen);
}


void GPGC_Population::atomic_increment_source_pages_to_old_gen(long pages)
{
  Atomic::add_ptr(pages, (intptr_t*) &_source_pages_to_old_gen);
}


void GPGC_Population::atomic_increment_target_pages_in_old_gen(long pages)
{
  Atomic::add_ptr(pages, (intptr_t*) &_target_pages_in_old_gen);
}


uint64_t GPGC_Population::total_released()
{
  uint64_t released = _small_space_1.released_pages()
                    + _small_space_2.released_pages()
                    + _mid_space_1.released_pages()
                    + _mid_space_2.released_pages()
                    + _large_space_1.released_pages()
                    + _large_space_2.released_pages();

  return released;
}


uint64_t GPGC_Population::no_relocate_pages()
{
  uint64_t no_relocate = _small_space_1.no_relocate_pages()
                       + _small_space_2.no_relocate_pages()
                       + _mid_space_1.no_relocate_pages()
                       + _mid_space_2.no_relocate_pages()
                       + _large_space_1.no_relocate_pages()
                       + _large_space_2.no_relocate_pages();

  return no_relocate;
}


uint64_t GPGC_Population::total_pops_to_relocate()
{
  uint64_t total_to_relocate = _small_space_1.max_cursor()
                             + _small_space_2.max_cursor()
                             + _mid_space_1.max_cursor()
                             + _mid_space_2.max_cursor()
                             + _large_space_1.max_cursor()
                             + _large_space_2.max_cursor();

  return total_to_relocate;
}


uint64_t GPGC_Population::live_words_found()
{
  uint64_t words   = _small_space_1.live_words_found()
                   + _small_space_2.live_words_found()
                   + _mid_space_1.live_words_found()
                   + _mid_space_2.live_words_found();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.live_words_found();

  return words;
}


uint64_t GPGC_Population::dead_words_found()
{
  uint64_t words   = _small_space_1.dead_words_found()
                   + _small_space_2.dead_words_found()
                   + _mid_space_1.dead_words_found()
                   + _mid_space_2.dead_words_found();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.dead_words_found();

  return words;
}


uint64_t GPGC_Population::dead_words_selected()
{
  uint64_t words   = _small_space_1.dead_words_selected()
                   + _small_space_2.dead_words_selected()
                   + _mid_space_1.dead_words_selected()
                   + _mid_space_2.dead_words_selected();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.dead_words_selected();

  return words;
}


uint64_t GPGC_Population::frag_words_found()
{
  uint64_t words   = _small_space_1.frag_words_found()
                   + _small_space_2.frag_words_found()
                   + _mid_space_1.frag_words_found()
                   + _mid_space_2.frag_words_found();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.frag_words_found();

  return words;
}


uint64_t GPGC_Population::frag_words_selected()
{
  uint64_t words   = _small_space_1.frag_words_selected()
                   + _small_space_2.frag_words_selected()
                   + _mid_space_1.frag_words_selected()
                   + _mid_space_2.frag_words_selected();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.frag_words_selected();

  return words;
}


uint64_t GPGC_Population::sideband_limited_words()
{
  uint64_t words = all_populations()->sideband_limited_words();
  // TODO: Do we need to add in large_space?
  //             + large_space_1.sideband_limited_words();

  return words;
}


