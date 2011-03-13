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
#ifndef GPGC_GENERATION_HPP
#define GPGC_GENERATION_HPP

#include "gpgc_layout.hpp"
#include "gpgc_onePageSpace.hpp"
#include "gpgc_pageAudit.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_relocationSpike.hpp"
#include "mutex.hpp"

class DetailTracer;
class GPGC_MultiPageSpace;
class GPGC_OnePageSpace;
class ObjectClosure;


class GPGC_RelocationPages VALUE_OBJ_CLASS_SPEC
{
  private:
    static const long _max_pages = 256;

    volatile long     _top_slot;
    volatile PageNum  _pages[_max_pages];
    AzLock*           _locks[_max_pages];
    volatile long     _page_count;

  public:
    void    initialize (int lock_rank);
    void    expand     (AzLock* expansion_lock, long old_top_slot);
    long    flush      ();

    long    top_slot   ()           { return _top_slot;    }
    PageNum page       (long slot)  { return _pages[slot]; }
    AzLock* lock       (long slot)  { return _locks[slot]; }
    long    page_count ()           { return _page_count;  }

    void    new_page   (long slot, PageNum page);
};


//  This class describes a generation in pauseless GC.
class GPGC_Generation VALUE_OBJ_CLASS_SPEC
{
  private:
    GPGC_PageInfo::Gens  _generation;

    GPGC_OnePageSpace*   _one_page_small;
    GPGC_OnePageSpace*   _one_page_mid;
    GPGC_MultiPageSpace* _multi_page;

    bool                 _marking_underway;

    static const long    _max_relocation_pages = 256;

    AzLock* const        _relocation_lock;

    GPGC_RelocationPages _relocation_pages;
    GPGC_RelocationPages _promotion_pages;

    intptr_t             _pages_allocated;               // Number of pages allocated fresh into this generation

    intptr_t             _small_space_page_capacity;     // Number of pages assigned to this generation & space
    intptr_t             _mid_space_page_capacity;       // Number of pages assigned to this generation & space
    intptr_t             _large_space_page_capacity;     // Number of pages assigned to this generation & space

    volatile PageNum     _relocated_pages;

    // Each gen gets one page of each tier for sub-page allocation.
    AzLock*              _small_shared_allocation_lock;
    AzLock*              _mid_shared_allocation_lock;
    volatile PageNum     _small_space_allocation_page;
    volatile PageNum     _mid_space_allocation_page;

    PageNum              _full_small_space_alloc_pages;
    PageNum              _full_mid_space_alloc_pages;


  public:
    enum BoolFlags {
      CountForAllocRate     = true,
      DontCountForAllocRate = false,
      UsePreAllocated       = true,
      DontUsePreAllocated   = false
    };


    GPGC_Generation( AzLock* rl, AzLock* small_al, AzLock* mid_al );

    void initialize(GPGC_OnePageSpace* one_page_small, GPGC_OnePageSpace* one_page_mid, GPGC_MultiPageSpace* multi_page, GPGC_PageInfo::Gens generation);

    // accessors
    GPGC_PageInfo::Gens  generation          ()              { return _generation; }
    bool                 marking_underway    ()              { return _marking_underway; }

    GPGC_OnePageSpace*   one_page_small      ()              { return _one_page_small; }
    GPGC_OnePageSpace*   one_page_mid        ()              { return _one_page_mid;   }
    GPGC_MultiPageSpace* multi_page          ()              { return _multi_page;     }

    // Shared allocation buffer pages:
    PageNum   small_space_allocation_page    ()              { return _small_space_allocation_page; }
    PageNum   mid_space_allocation_page      ()              { return _mid_space_allocation_page;   }

    void      set_small_space_allocation_page(PageNum page)  { _small_space_allocation_page = page; }
    void      set_mid_space_allocation_page  (PageNum page)  { _mid_space_allocation_page   = page; }

    // Allocation:
HeapWord*allocate(size_t words,bool is_tlab);

    // Remapping pages:
    PageNum   allocate_mid_remapping_page    (long source_space_id);
    bool      mutator_heal_mid_page          (PageNum page, char* force_addr);
    void      heal_mid_remapping_page        (PageNum page, PageNum resource_page, GPGC_RelocationSpike* relocation_spike);

    // Relocation pages:
    PageNum   allocate_small_relocation_page (GPGC_Generation* source_gen, bool use_preallocated, PageAuditWhy why);
    HeapWord* allocate_for_relocate          (GPGC_Generation* source_gen, long word_size, long page_time);
    void      flush_relocation_pages         (DetailTracer* dt);
    void      flush_promotion_pages          (DetailTracer* dt);
    long      relocation_page_count          ()     { return _relocation_pages.page_count(); }
    long      promotion_page_count           ()     { return _promotion_pages.page_count();  }

    // Page block promotion
    PageNum   clone_block                    (PageNum orig_block, long pages);
    void      new_gc_relocated_block         (PageNum block);
    void      old_gc_relocated_block         (PageNum block);

    // Relocating/Relocated/Remapped/Release:
    void      relocated_page                 (PageNum page, GPGC_RelocationSpike* relocation_spike);
    void      remapped_page                  (PageNum page);
    long      release_relocated_pages        ();
    void      release_empty_page             (PageNum page, GPGC_RelocationSpike* relocation_spike);
    void      release_relocated_block        (PageNum block);
    void      release_empty_block            (PageNum block, GPGC_RelocationSpike* relocation_spike);

    // Iteration:
    void      object_iterate                 (ObjectClosure* cl);

    // GC preparation:
    void      clear_allocation_buffers       ();
    void      set_marking                    (bool state);

    // Stats:
    size_t    small_space_words_capacity     ()  { return _small_space_page_capacity << LogWordsPerGPGCPage; }
    size_t    mid_space_words_capacity       ()  { return _mid_space_page_capacity << LogWordsPerGPGCPage;   }
    size_t    large_space_words_capacity     ()  { return _large_space_page_capacity << LogWordsPerGPGCPage; }

    size_t    small_space_capacity           ()  { return small_space_words_capacity() << LogBytesPerWord;  }
    size_t    mid_space_capacity             ()  { return mid_space_words_capacity() << LogBytesPerWord;    }
    size_t    large_space_capacity           ()  { return large_space_words_capacity() << LogBytesPerWord;  }

    size_t    words_capacity                 ()  { return small_space_words_capacity() + mid_space_words_capacity() + large_space_words_capacity(); }
    size_t    capacity                       ()  { return words_capacity() << LogBytesPerWord;  }

    size_t    pages_allocated                ()  { return _pages_allocated; }

    // Blocks:
    HeapWord* block_start                    (const void* addr);

    // Debugging:
    void      verify_capacity                ();

  private:
    inline void atomic_stat_increment        (intptr_t* statistic);
    inline void atomic_stat_decrement        (intptr_t* statistic);
    inline void atomic_stat_add              (intptr_t amount, intptr_t* statistic);
    inline void atomic_stat_subtract         (intptr_t amount, intptr_t* statistic);

    HeapWord* allocate_sub_page_small        (size_t words);
    HeapWord* allocate_sub_page_mid          (size_t words);
    HeapWord* allocate_full_page_small       (bool is_tlab);
    HeapWord* allocate_multi_page            (size_t words);

    PageNum   select_and_commit_small_page   (bool count_for_alloc_rate, bool use_preallocated, PageAuditWhy why);
    PageNum   select_and_commit_mid_page     (size_t words, PageAuditWhy why);

    HeapWord* allocate_in_reserved_page      (size_t words, bool small_space);
    bool      allocate_mid_space_sub_pages   (HeapWord* current_top, HeapWord* new_top);   
    HeapWord* allocate_in_new_small_page     (size_t words);
    HeapWord* allocate_in_new_mid_page       (size_t words);

    void      release_relocated_page         (PageNum page);

    HeapWord* new_page_in_relocation_slot    (GPGC_RelocationPages* relocation_pages, GPGC_Generation* source_gen,
                                              long slot, long word_size, long page_time);
};

#endif // GPGC_GENERATION_HPP
