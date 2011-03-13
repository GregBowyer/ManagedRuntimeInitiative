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
#ifndef GPGC_POPULATION_HPP
#define GPGC_POPULATION_HPP


#include "allocation.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_relocationSpike.hpp"


//
//  A GPGC_RemapTargetArray object tracks the pages mid space space objects are relocated into.
//

class GPGC_RemapTargetArray VALUE_OBJ_CLASS_SPEC{
  public:
    typedef struct {
      PageNum             page;
      int64_t             stripe;
      GPGC_PageInfo::Gens source_gen;
    } RemapTarget;

  private:
    // Array of target pages:
RemapTarget*_array;
uint32_t _allocated_length;
    uint64_t     _current_length;
 
  public:
    void  initialize       (uint32_t max_length);
    void  reset            ();
    void  atomic_add_page  (PageNum page, int64_t stripe, GPGC_PageInfo::Gens source_gen);

    uint64_t            current_length ()              { return _current_length; }

    PageNum             page           (uint64_t indx) { assert0(indx<_current_length); return _array[indx].page; }
    int64_t             stripe         (uint64_t indx) { assert0(indx<_current_length); return _array[indx].stripe; }
    GPGC_PageInfo::Gens source_gen     (uint64_t indx) { assert0(indx<_current_length); return _array[indx].source_gen; }
};


//
//  A GPGC_Population object tracks the page populations statistics
//  for the pauseless collector.
//

class GPGC_PopulationArray VALUE_OBJ_CLASS_SPEC{
  public:
    // Captures basic stats on pages to be relocated.  Maximum word size of a "page" in this
    // structure is WordsPerMidSpaceBlock, currently 2^22.
    typedef struct {
        PageNum  page;
uint32_t live_words;
uint32_t dead_words;
uint32_t frag_words;
        uint32_t occupancy;   // live_words/allocated_pages: For Small space this equals live_words.
	int64_t  time_stripe; // time for some arrays, stripe for others.
    } PagePop;

  private:
    // Array of pages:
PagePop*_array;
uint32_t _allocated_length;
uint32_t _current_length;

    uint64_t  _cursor;
uint32_t _max_cursor;

    // Total memory found in array of pages:
    uint64_t  _live_words_total;
    uint64_t  _dead_words_total;
    uint64_t  _frag_words_total;
    uint64_t  _block_pages_total;

    // Total memory found in pages selected for relocation:
    uint64_t  _live_words_selected;
    uint64_t  _dead_words_selected;
    uint64_t  _frag_words_selected;
    
    uint64_t  _sideband_limited_words;
    uint64_t  _skipped_pages;
    uint64_t  _released_empty_pages;
    uint64_t  _norelocate_pages;
    uint64_t  _defrag_pages_selected;


  public:
           void      initialize (uint32_t max_length);
           void      reset      ();

    // Insert functions:
    inline void      add_no_relocate_pages(uint32_t pages)                   { assert0(pages>0); _norelocate_pages     += pages; }
    inline void      add_empty_page       (uint32_t pages)                   { assert0(pages>0); _released_empty_pages += pages; }
    inline void      add_empty_block      (PageNum block, long pages, GPGC_RelocationSpike* relocation_spike);
 
    inline void      add_skipped_page (PageNum page, uint32_t pages, uint32_t live_words, uint32_t frag_words, uint32_t dead_words);

    inline void      add_page        (PageNum page,  int64_t page_time, uint32_t occupancy, uint32_t live_words, uint32_t frag_words, uint32_t dead_words);
    inline void      add_block       (PageNum block, int64_t block_time, uint64_t pages, uint32_t live_words, uint32_t frag_words);


    inline void      add_page        (PagePop* page_pop);

    inline void      increment_defrag_pages_selected()                   { _defrag_pages_selected ++; }
    
    // Array sorting functions:
           void      sort_by_new_gc_relocation_priority();
           void      sort_by_old_gc_relocation_priority();
           void      sort_by_largest_time_first();
     
    // Cursor management:
    inline void      reset_cursor()              { _cursor = 0; }
    inline void      reset_max_cursor()          { reset_cursor(); _max_cursor = _current_length; }
    inline uint64_t  max_cursor  ()              { return _max_cursor; }
    inline bool      cursor_at_end()             { return _cursor == _max_cursor; }
    inline uint64_t  cursor()                    { return _cursor; }
    inline uint64_t* cursor_addr()               { return &_cursor; }

           bool      atomic_claim_array_chunk(uint64_t& start_cursor, uint64_t& end_cursor, long work_unit);

    // Array accessors:
    inline PageNum   page        ()              { assert0(_cursor<_max_cursor);  return _array[_cursor].page; }
    inline uint32_t  occupancy   ()              { assert0(_cursor<_max_cursor);  return _array[_cursor].occupancy; }

    inline PagePop*  page_pop    (uint64_t indx) { assert0(indx<_current_length); return &_array[indx];}

    inline PageNum   page        (uint64_t indx) { assert0(indx<_current_length); return _array[indx].page; }
    inline uint32_t  occupancy   (uint64_t indx) { assert0(indx<_current_length); return _array[indx].occupancy; }
    inline int64_t   time        (uint64_t indx) { assert0(indx<_current_length); return _array[indx].time_stripe; }
    inline int64_t   stripe      (uint64_t indx) { assert0(indx<_current_length); return _array[indx].time_stripe; }

           void      sideband_limit_reclaim_cutoff(uint32_t max_cursor);

    // Accessors:
    inline bool      is_empty              ()     { return _current_length==0 && _skipped_pages==0; }

    inline uint64_t  sideband_limited_words()     { return _sideband_limited_words; }
    inline uint64_t  released_pages        ()     { return _released_empty_pages; }
    inline uint64_t  no_relocate_pages     ()     { return _norelocate_pages; }

    inline uint64_t  live_words_found      ()     { return _live_words_total;    }
    inline uint64_t  live_words_selected   ()     { return _live_words_selected; }
    inline uint64_t  dead_words_found      ()     { return _dead_words_total;    }
    inline uint64_t  dead_words_selected   ()     { return _dead_words_selected; }
    inline uint64_t  frag_words_found      ()     { return _frag_words_total;    }
    inline uint64_t  frag_words_selected   ()     { return _frag_words_selected; }
    inline uint64_t  block_pages_total     ()     { return _block_pages_total;   }

    // Sanity checking:
           void      assert_only_allocated_pages();
};


inline void GPGC_PopulationArray::add_page(PageNum  page,        int64_t page_time,  uint32_t occupancy,
                                           uint32_t live_words, uint32_t frag_words, uint32_t dead_words)
{
  assert0(_current_length < _allocated_length);

  _array[_current_length].page           = page;
  _array[_current_length].occupancy      = occupancy;
  _array[_current_length].live_words     = live_words;
  _array[_current_length].dead_words     = dead_words;
  _array[_current_length].frag_words     = frag_words;
  _array[_current_length].time_stripe    = page_time;
  _current_length ++;

  _live_words_total    += live_words;
  _frag_words_total    += frag_words;
  _dead_words_total    += dead_words;

  _live_words_selected += live_words;
  _frag_words_selected += frag_words;
  _dead_words_selected += dead_words;
}


inline void GPGC_PopulationArray::add_skipped_page(PageNum page, uint32_t pages, uint32_t live_words, uint32_t frag_words, uint32_t dead_words)
{
  _array[_current_length++].page           = page;
  _skipped_pages    += pages;
  _live_words_total += live_words;
  _frag_words_total += frag_words;
  _dead_words_total += dead_words;
}

inline void GPGC_PopulationArray::add_block(PageNum block, int64_t block_time, uint64_t pages, uint32_t live_words, uint32_t frag_words)
{
  assert0(_current_length < _allocated_length);

  _array[_current_length].page        = block;
  _array[_current_length].occupancy   = live_words;
  _array[_current_length].dead_words  = 0;
  _array[_current_length].frag_words  = frag_words;
  _array[_current_length].time_stripe = block_time;
  _current_length ++;

  _live_words_total    += live_words;
  _frag_words_total    += frag_words;
  _block_pages_total   += pages;

  _live_words_selected += live_words;
  _frag_words_selected += frag_words;
}

inline void GPGC_PopulationArray::add_page(PagePop* page_pop)
{
  add_page(page_pop->page, page_pop->time_stripe, page_pop->occupancy, page_pop->live_words, page_pop->frag_words, page_pop->dead_words);
}


class GPGC_Population:public CHeapObj{
  private:
    GPGC_PopulationArray _all_populations;
    GPGC_PopulationArray _small_space_1;
    GPGC_PopulationArray _small_space_2;
    GPGC_PopulationArray _mid_space_1;
    GPGC_PopulationArray _mid_space_2;
    GPGC_PopulationArray _large_space_1;
    GPGC_PopulationArray _large_space_2;
    GPGC_PopulationArray _small_space_skipped_pages;
    GPGC_PopulationArray _mid_space_skipped_pages;
    GPGC_PopulationArray _large_space_skipped_pages;

    GPGC_RemapTargetArray _mid_space_targets;

  public:
    GPGC_PopulationArray* all_populations   () { return &_all_populations; }

    // Accessors for NewGC:
    GPGC_PopulationArray* small_space_to_new()        { return &_small_space_1; }
    GPGC_PopulationArray* small_space_to_old()        { return &_small_space_2; }
    GPGC_PopulationArray* small_space_skipped_pages() { return &_small_space_skipped_pages; }
    GPGC_PopulationArray* mid_space_to_new  ()        { return &_mid_space_1;   }
    GPGC_PopulationArray* mid_space_to_old  ()        { return &_mid_space_2;   }
    GPGC_PopulationArray* mid_space_skipped_pages()   { return &_mid_space_skipped_pages; }
    GPGC_PopulationArray* large_space_to_new()        { return &_large_space_1; }
    GPGC_PopulationArray* large_space_to_old()        { return &_large_space_2; }
    GPGC_PopulationArray* large_space_skipped_pages() { return &_large_space_skipped_pages; }

    // Accessors for OldGC:
    GPGC_PopulationArray* small_space_in_old () { return &_small_space_1; }
    GPGC_PopulationArray* small_space_in_perm() { return &_small_space_2; }
    GPGC_PopulationArray* mid_space_in_old   () { return &_mid_space_1;   }
    GPGC_PopulationArray* mid_space_in_perm  () { return &_mid_space_2;   }
    GPGC_PopulationArray* large_space_in_old () { return &_large_space_1; }
    GPGC_PopulationArray* large_space_in_perm() { return &_large_space_2; }

    // Relocation target pages:
    GPGC_RemapTargetArray* mid_space_targets () { return &_mid_space_targets; }

  private:
    long      _source_pages_to_new_gen;
    long      _target_pages_in_new_gen;
    long      _source_pages_to_old_gen;
    long      _target_pages_in_old_gen;

  public:
    void     initialize                (uint32_t max_small_space_pages, uint32_t max_mid_space_pages, uint32_t max_large_space_blocks);
    void     reset_populations         ();
    void     reset_max_cursor          ();
    void     reset_cursor              ();

    uint64_t total_released            ();
    uint64_t no_relocate_pages         ();
    uint64_t total_pops_to_relocate    ();

    uint64_t live_words_found          ();
    uint64_t dead_words_found          ();
    uint64_t dead_words_selected       ();
    uint64_t frag_words_found          ();
    uint64_t frag_words_selected       ();

    uint64_t total_garbage_words       ()    { return (total_released() << LogWordsPerGPGCPage) + dead_words_found();    }
    uint64_t garbage_words_to_collect  ()    { return (total_released() << LogWordsPerGPGCPage) + dead_words_selected(); }

    uint64_t sideband_limited_words    ();

    void     set_source_pages_to_new_gen(long pages)  { _source_pages_to_new_gen = pages; }
    void     set_source_pages_to_old_gen(long pages)  { _source_pages_to_old_gen = pages; }

    long     source_pages_to_new_gen   ()    { return _source_pages_to_new_gen; }
    long     target_pages_in_new_gen   ()    { return _target_pages_in_new_gen; }
    long     source_pages_to_old_gen   ()    { return _source_pages_to_old_gen; }
    long     target_pages_in_old_gen   ()    { return _target_pages_in_old_gen; }

    void     atomic_increment_source_pages_to_new_gen(long pages);
    void     atomic_increment_target_pages_in_new_gen(long pages);
    void     atomic_increment_source_pages_to_old_gen(long pages);
    void     atomic_increment_target_pages_in_old_gen(long pages);
};

#endif // GPGC_POPULATION_HPP

