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
#ifndef GPGC_MULTIPAGESPACE_HPP
#define GPGC_MULTIPAGESPACE_HPP


#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"

#include "iterator.hpp"
#include "pgcTaskManager.hpp"

class GPGC_PageGroup;
class GPGC_Population;
class PGCTaskQueue;

//  This class manages a region of virtual memory.  It provides allocation and
//  deallocation of multi-page blocks.  It also manages virtual address reuse,
//  supporting the need to hold a deallocated virtual address until a TLB resync
//  has occurred.
class GPGC_MultiPageSpace:public CHeapObj
{
  private:
    // The range of pages managed by this class: 
    PageNum           _start_page;
    volatile PageNum  _top_page;
    PageNum           _end_page;

    // Tracking of available pages:
    GPGC_PageGroup*   _available_blocks;

    // Tracking of pages held for TLB resync:
    PageNum           _blocks_on_hold;

    // Held pages being transferred to available, pending TLB_resync_occurred()
    PageNum           _pending_available_blocks_head;

    // Page usage statistics:
    long              _allocated_pages;
    long              _available_pages;
    long              _held_pages;

  public:
    GPGC_MultiPageSpace();

    void initialize(PageNum start, PageNum end);

    PageNum           start_page     () { return _start_page; }
    volatile PageNum  top_page       () { return _top_page; }
    PageNum           end_page       () { return _end_page; }

    // alloc/dealloc of page blocks:
    PageNum select_block             (long pages, GPGC_PageInfo::Gens  generation);
    void    return_available_block   (PageNum block);
    void    hold_block_for_TLB_resync(PageNum block);

    // pinning of page blocks, to make GC ignore them:
    void    pin_block                (PageNum block);
    void    unpin_block              (PageNum block);

    // setup read barray array trap state
    void    setup_read_trap_array    ();

    // TLB resync notification:
    void    prepare_for_TLB_resync   ();
    void    TLB_resync_occurred      ();

    // pages and fragmentation tracking
    void    page_and_frag_words_count(long generation_mask, long* page_count_result, long* frag_words_result);

    // Empty page tracking
    void    new_gc_collect_blocks    (int64_t promotion_threshold_time, GPGC_PopulationArray* surviving_blocks, GPGC_PopulationArray* promoting_blocks);
    void    old_gc_collect_blocks    (GPGC_PopulationArray* old_blocks, GPGC_PopulationArray* perm_blocks);

    // Page marks
    void    clear_page_marks         (long generation_mask);
    void    clear_verify_marks       ();
    void    verify_no_live_marks     (long generation_mask);

    // Cardmark scanning tasks 
    long    make_cardmark_tasks      (PGCTaskQueue* q);

    // Object iteration
    void    object_iterate           (GPGC_PageInfo::Gens gen, ObjectClosure* cl);

    // Page population stat support
    void    clear_no_relocate        (long generation_mask);

    // Debugging
    void    sum_raw_stats            (GPGC_PageInfo::Gens generation_mask, uint64_t* obj_count_result, uint64_t* word_count_result);
    long    verify_page_budgets      ();

    // support for JVMTI heap iteration
    void    make_heap_iteration_tasks(ObjectClosure* closure, PGCTaskQueue* q);

  private:
    PageNum get_available_block      (long pages);
    PageNum get_expansion_block      (long pages);

    // Page group management:
    void    delete_page_group        (GPGC_PageGroup* group);
    void    add_block_to_page_group  (PageNum block, long pages);
};


class GPGC_PageGroup:public CHeapObj{
  private:
    int             _block_size;
    PageNum         _first_block;
    
    GPGC_PageGroup* _grp_next;
    GPGC_PageGroup* _grp_prev;

  public:
    GPGC_PageGroup(int size, PageNum block)  { _block_size = size; _first_block = block, _grp_next = _grp_prev = NULL; }
    
    int             block_size()  const      { return _block_size;  }
    PageNum         first_block() const      { return _first_block; }
    GPGC_PageGroup* grp_prev()    const      { return _grp_prev; }
    GPGC_PageGroup* grp_next()    const      { return _grp_next; }
  
    void set_first_block(PageNum block)      { _first_block = block; }
    void set_grp_next(GPGC_PageGroup* group) { _grp_next = group; }
    void set_grp_prev(GPGC_PageGroup* group) { _grp_prev = group; }
};  

#endif // GPGC_MULTIPAGESPACE_HPP
