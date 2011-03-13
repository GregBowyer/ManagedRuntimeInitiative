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

#ifndef GPGC_ONEPAGESPACE_HPP
#define GPGC_ONEPAGESPACE_HPP

#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"

#include "allocation.hpp"
#include "atomic_os_pd.inline.hpp"
#include "iterator.hpp"

class PGCTaskQueue;


//  This class manages a region of virtual memory.  It provides allocation and
//  deallocation of single pages.  It also manages virtual address reuse, supporting
//  the need to hold a deallocated virtual address until a TLB resync has occurred.
class GPGC_OnePageSpace:public CHeapObj
{

  private: 
    bool              _small_space;
    long              _block_size;

    // The range of pages managed by this class: 
    PageNum           _start_page;
    PageNum           _end_page;
    volatile PageNum  _top_page;

    // A singly linked list of virtual addresses ready for allocation.  The
    // list is linked through the ll_next field of the GPGC_PageInfo array.  Updated
    // via CAS.
    volatile PageNum  _available_pages_list;

    // A singly linked list of virtual addresses waiting for TLB resync.  The
    // list is linked through the ll_next field of the GPGC_PageInfo array.  Updated
    // via CAS
    volatile PageNum  _pages_on_hold;

    // Held pages being transferred to available, pending TLB_resync_occurred()
    PageNum  _pending_available_pages_head;
    PageNum  _pending_available_pages_tail;
    long     _pending_available_pages_count;

    // Page usage statistics:
    long     _allocated_pages_count;
    long     _available_pages_count;
    long     _held_pages_count;

    enum {  
      PageNumBits = 32,
      TagBits = 32
    };

    enum {
      PageNumShift = 0,
      TagShift = PageNumBits
    };

    enum {
      PageNumMask        = (address_word)right_n_bits(PageNumBits),
      PageNumMaskInPlace = (address_word)PageNumMask << PageNumShift,
      TagMask            = (address_word)right_n_bits(TagBits),
      TagMaskInPlace     = (address_word)TagMask << TagShift
    };


  public:


    GPGC_OnePageSpace();

    void    initialize (PageNum space_start, PageNum space_end, bool small_space); 
                                

    PageNum  decode_pagenum(PageNum  available_pages_list)  { return (PageNum(available_pages_list & PageNumMask));}
    uint64_t     decode_tag(PageNum  available_pages_list) { return (uint64_t(available_pages_list & TagMaskInPlace) >> TagShift); }

    // init traps
    void     create_read_trap_array_tasks    (PGCTaskQueue* q, uint64_t tasks);

    // Lock free alloc/dealloc of pages:
    PageNum select_page                      ();
    void    return_available_page            (PageNum page);
    void    hold_page_for_TLB_resync         (PageNum page);

    // TLB resync notification:
    void    prepare_for_TLB_resync           ();
    void    TLB_resync_occurred              ();

    // Page pinning
    void    pin_page                         (PageNum page);
    void    unpin_page                       (PageNum page);

    // pages and fragmentation tracking
    void    page_and_frag_words_count        (long generation_mask, long* page_count_result, long* frag_words_result);

    // Sparse page tracking
    void    new_gc_collect_sparse_populations(int64_t promotion_threshold_time,
                                              GPGC_PopulationArray* population, GPGC_PopulationArray* skipped_population);
    void    old_gc_collect_sparse_populations(uint64_t fragment_mask, uint64_t fragment_stripe,
                                              GPGC_PopulationArray* population, GPGC_PopulationArray* skipped_population);

    // Page marks
    void    clear_page_marks                 (long generation_mask, long section, long sections);
    void    clear_verify_marks               ();
    void    verify_no_live_marks             (long generation_mask);

    // Object iteration
    void    object_iterate                   (GPGC_PageInfo::Gens gen, ObjectClosure* cl);

    // Page population stat support
    void    clear_no_relocate                (long generation_mask);

    // Debugging
    void    sum_raw_stats                    (GPGC_PageInfo::Gens generation_mask, uint64_t* obj_count_result, uint64_t* word_count_result);
    long    verify_capacity                  (GPGC_PageInfo::Gens generation);
    long    verify_page_budgets              ();

    // JVMTI heap iteration support
    void    make_heap_iteration_tasks        (long sections, ObjectClosure* closure, PGCTaskQueue* q);

    // accessors
    bool    small_space                         ()  { return _small_space; }
    PageNum start_page                          ()  { return _start_page;  }  
    PageNum end_page                            ()  { return _end_page;    }
    long    block_size                          ()  { return _block_size;  }

    volatile  PageNum  top_page                 () { return _top_page; }
    volatile  PageNum* top_page_addr            () { return &_top_page; }

    volatile  PageNum  available_pages_list     () { return _available_pages_list; }  
    volatile  PageNum* available_pages_list_addr() { return &_available_pages_list; }  

    volatile  PageNum  pages_on_hold            () { return _pages_on_hold; }  
    volatile  PageNum* pages_on_hold_addr       () { return &_pages_on_hold; }  

    PageNum  pending_available_pages_head       () { return _pending_available_pages_head; }  
    PageNum* pending_available_pages_head_addr  () { return &_pending_available_pages_head; }  
    void set_pending_available_pages_head       (PageNum head) { _pending_available_pages_head = head; }  

    PageNum  pending_available_pages_tail       () { return _pending_available_pages_tail; }  
    PageNum* pending_available_pages_tail_addr  () { return &_pending_available_pages_tail; }  
    void set_pending_available_pages_tail       (PageNum tail ) { _pending_available_pages_tail = tail; }  

    long  pending_available_pages_count         () { return _pending_available_pages_count; }  
    long* pending_available_pages_count_addr    () { return &_pending_available_pages_count; }  
    void  set_pending_available_pages_count     (long count) { _pending_available_pages_count = count; }  

    void  inc_available_pages_count()  {   Atomic::inc_ptr((intptr_t*)&_available_pages_count);  }
    void  dec_available_pages_count()  {   Atomic::dec_ptr((intptr_t*)&_available_pages_count);  }
    void  inc_allocated_pages_count()  {   Atomic::inc_ptr((intptr_t*)&_allocated_pages_count);  }
    void  dec_allocated_pages_count()  {   Atomic::dec_ptr((intptr_t*)&_allocated_pages_count);  }
    void  inc_held_pages_count()       {   Atomic::inc_ptr((intptr_t*)&_held_pages_count);  }
    void  dec_held_pages_count()       {   Atomic::dec_ptr((intptr_t*)&_held_pages_count);  }

    long  held_pages_count     ()  { return  _held_pages_count; } 
    long* held_pages_count_addr()  { return &_held_pages_count; } 

  private:
    PageNum get_available_page ();
    PageNum get_expansion_page ();
};

/*
we will have these classes later if need be
some functions in the parent class will then become virtual and implementations will
be in the classes below
*/

// class GPGC_OnePageSpace_Small: public GPGC_OnePageSpace


// class GPGC_OnePageSpace_Mid : public GPGC_OnePageSpace

#endif //GPGC_ONEPAGESPACE_HPP 
