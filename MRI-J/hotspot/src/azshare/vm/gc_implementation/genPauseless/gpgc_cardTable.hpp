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
#ifndef GPGC_CARDTABLE_HPP
#define GPGC_CARDTABLE_HPP


#include "barrierSet.hpp"
#include "bitMap.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_pageInfo.hpp"


class GPGC_GCManagerNewStrong;


//
// GPGC maintains a cardmark bitmap and a cardmark summary bytemap to track references from Old to New.
//
// The cardmark bitmap contains 1 bit per word in the heap.  This is LogWordsPerGPGCPage
// bits for each heap page.
//
// The cardmark summary bytemap contains 1 byte per cache-line of cardmark bitmap.
//
// The bitmap and the bytemap are both kept as a sparsely allocated array in their own region of virtual
// memory.

const long LogBytesPerCardMarkPage     = LogWordsPerGPGCPage - LogBitsPerByte;
const long LogWordsPerCardMarkPage     = LogBytesPerCardMarkPage - LogBytesPerWord;

const long BytesPerCardMarkPage        = 1 << LogBytesPerCardMarkPage;
const long WordsPerCardMarkPage        = 1 << LogWordsPerCardMarkPage;

const long LogBytesPerCardSummaryPage  = LogBytesPerCardMarkPage - LogBytesPerCacheLine;
const long LogWordsPerCardSummaryPage  = LogBytesPerCardSummaryPage - LogBytesPerWord;

const long BytesPerCardSummaryPage     = 1 << LogBytesPerCardSummaryPage;
const long WordsPerCardSummaryPage     = 1 << LogWordsPerCardSummaryPage;


class GPGC_CardTable:public BarrierSet{
  // Static field and methods:
  private:
    static GPGC_CardTable*  _card_table;

static MemRegion _bitmap_reserved_region;
static MemRegion _bytemap_reserved_region;
    static volatile int8_t* _bitmap_pages_mapped;
    static volatile int8_t* _bytemap_pages_mapped;

    static void set_mark_range_within_word(BitMap::idx_t start_bit, BitMap::idx_t end_bit, HeapWord* bitmap_base);
    static long verify_cardmark_bitmap(uintptr_t* bitmap, long size, heapRef* objs_base, bool all_zero);

    // For cardmarking verification:
    static uint16_t* _real_page_set_card_counts;
    static uint16_t* _page_set_card_counts;

  public:
    static GPGC_CardTable* card_table()   { return _card_table; }

    static bool allocate_cardmark_for_heap(PageNum page);
    static bool allocate_cardmark_for_heap(PageNum start_page, PageNum end_page);

    static void clear_card_marks          (PageNum page);

    static void card_mark_across_region   (HeapWord* start, HeapWord* end);

    static void scan_cardmarks_for_page   (GPGC_GCManagerNewStrong* gcm, PageNum page, GPGC_PageInfo::States page_state);
    static void scan_page_multi_space     (GPGC_GCManagerNewStrong* gcm, PageNum page, long chunk, long chunks);

    static void verify_marks_for_page     (PageNum page, bool can_change);
    static void verify_no_marks_for_page  (PageNum page);

    static HeapWord* bitmap_base          ()  { return GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bitmap);  }
    static HeapWord* bytemap_base         ()  { return GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bytemap); }


  // Non-static methods, needed to implement BarrierSet:
  public:
    GPGC_CardTable();

  //
  // Implement the required BarrierSet methods:
  //
  public:
BarrierSet::Name kind(){return BarrierSet::GenPauselessBarrier;}
    bool             is_a        (BarrierSet::Name bsn)                { return bsn == kind(); }

    bool has_read_ref_barrier    ()                                    { return false; }
    bool has_read_prim_barrier   ()                                    { return false; }
    bool has_write_ref_barrier   ()                                    { return true;  }
    bool has_write_prim_barrier  ()                                    { return false; }

    bool read_ref_needs_barrier  (objectRef*)                          { return false; }
    bool read_prim_needs_barrier (HeapWord*, size_t)                   { return false; }
    bool write_ref_needs_barrier (objectRef* field, oop new_val)       { return new_val!=NULL && !new_val->is_perm(); }
    bool write_prim_needs_barrier(HeapWord*, size_t, juint, juint)     { return false; }

    void read_ref_field          (objectRef* field)                    {}
    void read_prim_field         (HeapWord* field, size_t bytes)       {}
    void write_prim_field        (HeapWord*, size_t, juint, juint)     {}

    void inline_write_ref_field  (objectRef* field, objectRef new_val) { GPGC_Marks::atomic_attempt_card_mark(field); }
    void inline_write_ref_array  (MemRegion mr)                        { card_mark_across_region(mr.start(), mr.last()); }
    void inline_write_region     (MemRegion mr)                        { card_mark_across_region(mr.start(), mr.last()); }

    void write_ref_field_work    (objectRef* field, objectRef new_val) { inline_write_ref_field(field, new_val); }
    void write_ref_array_work    (MemRegion mr)                        { inline_write_ref_array(mr); }
    void write_region_work       (MemRegion mr)                        { inline_write_region(mr); }

    bool has_read_ref_array_opt  ()                                    { return false; }
    bool has_read_prim_array_opt ()                                    { return false; }
    bool has_write_ref_array_opt ()                                    { return true;  }
    bool has_write_prim_array_opt()                                    { return false; }

    bool has_read_region_opt     ()                                    { return false; }
    bool has_write_region_opt    ()                                    { return true;  }

    void read_ref_array          (MemRegion mr)                        { ShouldNotReachHere(); }
    void read_prim_array         (MemRegion mr)                        { ShouldNotReachHere(); }
    void write_prim_array        (MemRegion mr)                        { ShouldNotReachHere(); }
    void read_region             (MemRegion mr)                        { ShouldNotReachHere(); }

    void resize_covered_region   (MemRegion mr)                        { ShouldNotReachHere(); }

    bool is_aligned              (HeapWord* addr)                      { return true; }
};

#endif // GPGC_CARDTABLE_HPP
