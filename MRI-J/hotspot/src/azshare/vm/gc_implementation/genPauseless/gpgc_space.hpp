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

#ifndef GPGC_SPACE_HPP
#define GPGC_SPACE_HPP

#include "allocation.hpp"
#include "gpgc_generation.hpp"
#include "gpgc_multiPageSpace.hpp"
#include "gpgc_population.hpp"
#include "gpgc_relocationSpike.hpp"

//  Data structures for tracking pages in the heap.
//
//  The heap is divided into two regions:
//
//    - New (aka Eden)
//    - Old & Perm
//
//  The Old/Perm region is covered by a card marking table, while the new region is
//  not.
//
//  A region is then divided into one or more stripes.  Each stripe consists of a
//  separate address reservation with Azul memory module.  Azul memory allocation is serial
//  within a reservation.  By spreading allocation load across multiple stripes,
//  page allocation can be parallel.
//
//  As the largest allocation load is in the new region, one would normally expect
//  to have more new region stripes then old/perm region stripes.
//
//  Azul uses 2MB pages on linux.  A pauseless GC page consists of 2^N pages,
//  with N determined at compile time.
//
//  Pages are numbered.  The page number is simply the base address of the page,
//  divided by the page size.  As a page is a least 2^20 bytes, and the memory
//  map requires object heap pages to be below 2^41, a signed 4 byte integer
//  integer can store a page number.
//
//  Within a stripe, pages are tracked as follows:
//
//    - A "page block" is some number of contiguous pages in the same state.
//    - A "page group" is a linked list of page blocks of the same size.
//    - A "group list" is a linked list of page groups, sorted smallest group first.
//
//  Some objects are larger then can be stored in an individual page.  In this case,
//  multiple adjacent pages are "chained" together, and they must then be dealt with
//  as a unit.
// 
//  Each page has two components of data attached to it:
//
//    - Data that only exists if the page is mapped in.  This data sits in the page
//      in front of the objects in the page.
//    - Data that exists for all pages.  This data is stored in an array, and that
//      array grows larger each time a page is mapped in at a higher address then
//      any previous mapped page.  This array never shrinks, so unmapping pages
//      won't cause it to use less space.  The array sits in a reserved memory
//      region that's big enough to contain the theoretical max size heap.
//
//
//  Things we want to keep track of:
//
//   - In page data:
//      - marking bitmap
//      - obj-start-array (only used for old/perm region)
//
//   - Page data array:
//      - uint8_t  page_state:      unmapped/allocating/allocated/relocating
//      - uint8_t  page_generation: new/old/perm
//      - uint8_t  page_flags:      no-pop-stats
//      - linked list prev/next pointers
//      - page population count
//      - allocation pointers
//      - pointer to page relocation data
// 
//   - Separate data structures:
//      - card marking table
//
//
//  The normal life cycle of a page is:
//
//     +--> Unmapped ->   -> Allocating -> Allocated -> Relocating -> Relocated -> Remapped --+
//                      ^                                                                     |
//                      |                                                                     |
//                      +---------------------------------------------------------------------+


class GPGC_Population;
class PGCTaskQueue;


class GPGC_Space: public AllStatic
{
  private:
    static GPGC_MultiPageSpace _multi_space;
    static GPGC_OnePageSpace   _one_space_small;
    static GPGC_OnePageSpace   _one_space_mid;

    static GPGC_Generation     _new_gen;
    static GPGC_Generation     _old_gen;
    static GPGC_Generation     _perm_gen;

    static GPGC_Generation*    _gens[33];

    // Last Gasp Allocation
    static long                _reserved_waiting;

    // Diagnostic/Debug
    static void print_stats();

  public:
    // Construct a new GPGC_Space.  The virtual address space described in new_gen_region and
    // old_gen_region should already have been reserved with the kernel.
    static void initialize(int max_committed_pages);

    // Generation lookup:
    static GPGC_Generation*     generation          (GPGC_PageInfo::Gens gen)  { return _gens[gen]; }

    static GPGC_MultiPageSpace* multi_space         ()                         { return &_multi_space; }
    static GPGC_OnePageSpace*   one_space_small     ()                         { return &_one_space_small; }
    static GPGC_OnePageSpace*   one_space_mid       ()                         { return &_one_space_mid; }

    // Allocation
    static HeapWord* new_gen_allocate               (size_t word_size, bool is_tlab);
    static HeapWord* old_gen_allocate               (size_t word_size);
    static HeapWord* perm_gen_allocate              (size_t word_size);

    static void      pin_page                       (PageNum page);
    static void      unpin_page                     (PageNum page);

    // Relocation pages:
    static PageNum   alloc_small_relocation_page    (GPGC_PageInfo::Gens generation, GPGC_PageInfo::Gens source_gen);
    static PageNum   alloc_mid_remapping_page       (GPGC_PageInfo::Gens generation, long source_space_id);

    static bool      mutator_heal_mid_page          (PageNum page, char* force_addr);
    static void      heal_mid_remapping_page        (PageNum page, GPGC_PageInfo::Gens source_generation, PageNum resource_page,
                                                     GPGC_RelocationSpike* relocation_spike);

    static HeapWord* new_gen_allocate_for_relocate  (GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time);
    static HeapWord* old_gen_allocate_for_relocate  (GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time);
    static HeapWord* perm_gen_allocate_for_relocate (GPGC_PageInfo::Gens source_generation, size_t word_size, long page_time);

    static long      new_gen_relocation_page_count  ();
    static long      old_gen_relocation_page_count  ();
    static long      old_gen_promotion_page_count   ();
    static long      perm_gen_relocation_page_count ();
   
    static bool      new_gen_promote_block          (PageNum block);

    // Mirror pages for bug protection:
    static void      remap_to_mirror                (PageNum page);

    // Track pages and fragmentation
    static void newgc_page_and_frag_words_count  (long* small_pages     , long* mid_pages     , long* large_pages,
                                                  long* small_frag_words, long* mid_frag_words, long* large_frag_words);
    static void oldgc_page_and_frag_words_count  (long* small_pages     , long* mid_pages     , long* large_pages,
                                                  long* small_frag_words, long* mid_frag_words, long* large_frag_words);

    // Page pops
    static void new_gc_collect_sparse_populations(int64_t promotion_threshold_time, GPGC_Population* page_pops);
    static void old_gc_collect_sparse_populations(uint64_t fragment_mask, uint64_t fragment_stripe, GPGC_Population* page_pops);

    // Iteration
    static void object_iterate                   (ObjectClosure* cl);
    static void old_gen_object_iterate           (ObjectClosure* cl);
    static void perm_gen_object_iterate          (ObjectClosure* cl);

    // Relocating/Relocated/Remapped/Release
    static void new_gc_relocating_page           (PageNum page);
    static void old_gc_relocating_page           (PageNum page);
    static void new_gc_relocated_block           (PageNum block);
    static void old_gc_relocated_block           (PageNum block);
    static void relocated_page                   (PageNum page, GPGC_RelocationSpike* relocation_spike);
    static void remapped_page                    (PageNum page);
    static void remapped_pages                   (GPGC_PopulationArray* array);
    static void release_empty_page               (PageNum page,  GPGC_RelocationSpike* relocation_spike);
    static void release_empty_block              (PageNum block, GPGC_RelocationSpike* relocation_spike);

    // trap support
    static void setup_read_trap_array            (PGCTaskManager* manager, uint64_t tasks);

    // New GC support
    static void new_gc_clear_allocation_buffers  ();
    static void new_gc_set_marking_underway      ();
    static void new_gc_flush_relocation          (DetailTracer* dt);
    static long make_cardmark_tasks              (PGCTaskQueue* q);
    static void new_gc_stop_marking              ();
    static void new_gc_clear_no_relocate         ();
    static long new_gc_release_relocated_pages   ();
    static void new_gc_release_relocated_block   (PageNum block);
    static void new_gc_clear_page_marks          ();
    static void new_gc_clear_one_space_marks     (long section, long sections);
    static void new_gc_clear_multi_space_marks   ();

    // Old GC support
    static void old_gc_clear_allocation_buffers();
    static void old_gc_set_marking_underway();
    static void old_gc_flush_relocation          (DetailTracer* dt);
    static void old_gc_stop_marking              ();
    static void old_gc_clear_no_relocate         ();
    static long old_gc_release_relocated_pages   ();
    static void old_gc_clear_page_marks          ();
    static void old_gc_clear_one_space_marks     (long section, long sections);
    static void old_gc_clear_multi_space_marks   ();

    static void add_live_object                  (oop obj, long words) {
                                                                         PageNum page = GPGC_Layout::addr_to_BasePageForSpace(obj);
                                                                         assert0( (!GPGC_Layout::small_space_page(page))
                                                                                  || (words<(long)GPGC_Layout::mid_space_min_object_words()) );
                                                                         GPGC_PageInfo::page_info(page)->add_live_object(words);
                                                                       }

    // TLB management
    static void prepare_for_TLB_resync           ();
    static void TLB_resync_occurred              ();

    // Stats:
    static size_t total_words_capacity    () { return new_gen_words_capacity() + old_gen_words_capacity() + perm_gen_words_capacity(); }
    static size_t total_words_used        () { return new_gen_words_used() + old_gen_words_used() + perm_gen_words_used(); }

    static size_t new_gen_words_capacity  () { return _new_gen.words_capacity();  }
    static size_t new_gen_words_used      () { return _new_gen.words_capacity();  }
    static size_t old_gen_words_capacity  () { return _old_gen.words_capacity();  }
    static size_t old_gen_words_used      () { return _old_gen.words_capacity();  }
    static size_t perm_gen_words_capacity () { return _perm_gen.words_capacity(); }
    static size_t perm_gen_words_used     () { return _perm_gen.words_capacity(); }

    static size_t new_gen_pages_allocated () { return _new_gen.pages_allocated(); }
    static size_t old_gen_pages_allocated () { return _old_gen.pages_allocated(); }
    static size_t perm_gen_pages_allocated() { return _perm_gen.pages_allocated(); }

    static PageNum mapped_pages_start     () { return GPGC_Layout::start_of_heap_range; }
    static PageNum mapped_pages_end       () { return GPGC_Layout::end_of_heap_range;   }

    // Utility:
    static bool is_valid_virtual_addr(const void* p)           {
                                                 PageNum page = GPGC_Layout::addr_to_PageNum((address)p);
                                                 return GPGC_Layout::page_in_valid_range(page);
                                               }
    static bool is_in(const void* p)           {
                                                 PageNum page = GPGC_Layout::addr_to_PageNum((address)p);
                                                 return GPGC_Layout::page_in_heap_range(page);
                                               }
 
    static bool is_in_new(const void* p)       {
                                                 PageNum        page = GPGC_Layout::addr_to_BasePageForSpace((address)p);
                                                 GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
                                                 return info->just_gen()==GPGC_PageInfo::NewGen;
                                               }
    static bool is_in_old_or_perm(const void* p){
                                                 PageNum page = GPGC_Layout::addr_to_BasePageForSpace((address)p);
                                                 if ( ! GPGC_Layout::page_in_heap_range(page) ) return false;
                                                 GPGC_PageInfo*      info = GPGC_PageInfo::page_info(page);
                                                 GPGC_PageInfo::Gens gen  = info->just_gen();
                                                 return gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen;
                                               }
    static bool is_in_permanent(const void* p) { 
                                                 PageNum page = GPGC_Layout::addr_to_BasePageForSpace((address)p);
                                                 if ( ! GPGC_Layout::page_in_heap_range(page) ) return false;
                                                 GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
                                                 return info->just_gen()==GPGC_PageInfo::PermGen;
                                               }

    // Debugging:
    static void      new_gc_verify_no_live_marks();
    static void      new_and_old_gc_verify_no_live_marks();

    static void      clear_verify_marks();
    static void      verify_page_budgets();
    static void      verify_page_object_starts();

    static void      verify_new_gen_capacity()  { _new_gen.verify_capacity();  }
    static void      verify_old_gen_capacity()  { _old_gen.verify_capacity();  }
    static void      verify_perm_gen_capacity() { _perm_gen.verify_capacity(); }

    static HeapWord* block_start(const void* addr);
    static bool      block_is_obj(const HeapWord* addr);

    static void      new_gc_sum_raw_stats(uint64_t* obj_count_result, uint64_t* word_count_result);
    static void      old_gc_sum_raw_stats(uint64_t* obj_count_result, uint64_t* word_count_result);

    // support for JVMTI heap iteration
    static void      make_heap_iteration_tasks(long sections, ObjectClosure* closure, PGCTaskQueue* q);
};

#endif // GPGC_SPACE_HPP
