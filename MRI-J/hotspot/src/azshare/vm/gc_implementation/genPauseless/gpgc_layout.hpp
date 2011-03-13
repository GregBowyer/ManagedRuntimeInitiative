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
#ifndef GPGC_LAYOUT_HPP
#define GPGC_LAYOUT_HPP


#include "allocation.hpp"
#include "globalDefinitions.hpp"

#include "os/azulmmap.h"


//+++ For increased performance, the variables that define the layout of a
//+++ Generational Pauseless GC managed heap page are declared as constants,
//+++ so the compiler can product optimized code.


const long LogPagesPerGPGCPage      = 0;  // = same as platform large page size, default is small-space
const long LogPagesPerMidSpaceBlock = 4;  // 16 x 2MB pages = 32MB block size in mid_space.

const long LogBytesPerGPGCPage      = LogPagesPerGPGCPage + LogBytesPerLargePage;// default is small-space
const long LogWordsPerGPGCPage      = LogBytesPerGPGCPage - LogBytesPerWord; // default is small-space

const long LogBytesPerMidSpaceBlock = LogPagesPerMidSpaceBlock + LogBytesPerLargePage;
const long LogWordsPerMidSpaceBlock = LogBytesPerMidSpaceBlock - LogBytesPerWord;

const long BytesPerGPGCPage         = 1 << LogBytesPerGPGCPage;
const long WordsPerGPGCPage         = 1 << LogWordsPerGPGCPage;

const long PagesPerGPGCPage         = 1 << LogPagesPerGPGCPage;
const long PagesPerMidSpaceBlock    = 1 << LogPagesPerMidSpaceBlock;
const long BytesPerMidSpaceBlock    = PagesPerMidSpaceBlock << LogBytesPerGPGCPage;
const long WordsPerMidSpaceBlock    = PagesPerMidSpaceBlock << LogWordsPerGPGCPage;


const long LogBytesPerGPGCDataPage  = LogBytesPerSmallPage;
const long LogWordsPerGPGCDataPage  = LogBytesPerGPGCDataPage - LogBytesPerWord;
const long LogDataPagesPerGPGCPage  = LogBytesPerGPGCPage - LogBytesPerGPGCDataPage;

const long BytesPerGPGCDataPage     = 1 << LogBytesPerGPGCDataPage;
const long WordsPerGPGCDataPage     = 1 << LogWordsPerGPGCDataPage;


const long LogBytesMidSpaceMinObjectSize = 18; // 256KB
const long LogWordsMidSpaceMinObjectSize = LogBytesMidSpaceMinObjectSize - LogBytesPerWord;

const long LogBytesLargeSpaceMinObjectSize    = 24; // 16MB
const long LogWordsLargeSpaceMinObjectSize    = LogBytesLargeSpaceMinObjectSize - LogBytesPerWord;
const long LogPagesLargeSpaceMinObjectSize    = LogBytesLargeSpaceMinObjectSize - LogBytesPerGPGCPage;

const long GPGC_LogBytesMidSpaceObjectSizeAlignment = 12; // 4KB
const long GPGC_LogWordsMidSpaceObjectSizeAlignment = GPGC_LogBytesMidSpaceObjectSizeAlignment - LogBytesPerWord;
const long GPGC_WordsMidSpaceObjectSizeAlignment    = 1 << GPGC_LogWordsMidSpaceObjectSizeAlignment;


// Metadata constants

// Mark bitmaps: strong marks, final marks, verify marks:
const long GPGC_SmallSpaceWordMarksIndexShift    = 0;  // 1 mark bit per 8B word in small space pages
const long GPGC_NonSmallSpaceWordMarksIndexShift = 9;  // 1 mark bit per 4KB in non small space pages

const long GPGC_SmallSpaceMarkWordsPerPage       = WordsPerGPGCPage >> (GPGC_SmallSpaceWordMarksIndexShift + LogBitsPerWord);
const long GPGC_NonSmallSpaceMarkWordsPerPage    = WordsPerGPGCPage >> (GPGC_NonSmallSpaceWordMarksIndexShift + LogBitsPerWord);

// Mark through bitmaps:
const long GPGC_WordMarkThroughsIndexShift       = 0;  // 1 mark bit per 8B word in all space
const long GPGC_MarkThroughsWordsPerPage         = WordsPerGPGCPage >> (GPGC_WordMarkThroughsIndexShift + LogBitsPerWord);

// MarkID bytemaps:
const long GPGC_MarkIDsWordIndexShift            = 0;  // 1 mark ID byte per 8B word in all spaces
const long GPGC_MarkIDsWordsPerPage              = WordsPerGPGCPage >> (GPGC_MarkIDsWordIndexShift + LogBytesPerWord);

// Card mark bitmaps:
const long LogWordsPerGPGCCardMarkSummaryByte = LogBytesPerCacheLine + LogBitsPerByte;
const long WordsPerGPGCCardMarkSummaryByte    = 1 << LogWordsPerGPGCCardMarkSummaryByte;


//+++
//+++ Constants for initializing the memory map layout:
//+++

// Areas of virtual memory are separated with a pad, to help detect overruns.  Pad size in large GPGC pages:
#define GPGC_LAYOUT_PAD             ((     1L * G) >> LogBytesPerGPGCPage)

// We don't include gpgc_pageInfo.hpp, to minimize dependencies.  This size gets sanity checked
// in GPGC_Layout::initialize():
#define SIZEOF_GPGC_PAGE_INFO       (64)

// Space needed for page audit trail, in large GPGC pages:
#define SIZEOF_GPGC_PAGE_AUDIT      (66 * BytesPerWord)



#if defined(AZ_X86)
//+++
//+++  X86 Heap Layout:
//+++

// Start address of virtual memory usable by objectRefs, in GPGC pages:  (16 GB)
#define GPGC_START_OF_HEAP_VM       (__GPGC_HEAP_START_ADDR__ >> LogBytesPerGPGCPage)
// End address of virtual memory usable by objectRefs, in GPGC pages:    (4 TB)
#define GPGC_END_OF_HEAP_VM         (__GPGC_HEAP_END_ADDR__   >> LogBytesPerGPGCPage)

// Start and end of heap mirror for relocation, in GPGC pages:  (4TB - 8TB)
#define GPGC_START_OF_HEAP_MIRROR   (__GPGC_HEAP_MIRROR_START_ADDR__ >> LogBytesPerGPGCPage)
#define GPGC_END_OF_HEAP_MIRROR     (__GPGC_HEAP_MIRROR_END_ADDR__   >> LogBytesPerGPGCPage)

#define GPGC_MEMORY_TIER_SIZE       ((1359L     * G) >> LogBytesPerGPGCPage)

// Extent of the tier of memory for medium objects:
#define GPGC_START_OF_MID_SPACE     (GPGC_START_OF_HEAP_VM)
#define GPGC_END_OF_MID_SPACE       (GPGC_START_OF_MID_SPACE   + GPGC_MEMORY_TIER_SIZE)

// Extent of the tier of memory for large objects:
#define GPGC_START_OF_LARGE_SPACE   (GPGC_END_OF_MID_SPACE     + GPGC_LAYOUT_PAD)
#define GPGC_END_OF_LARGE_SPACE     (GPGC_START_OF_LARGE_SPACE + GPGC_MEMORY_TIER_SIZE)

// Extent of the tier of memory for small objects:
#define GPGC_START_OF_SMALL_SPACE   (GPGC_END_OF_LARGE_SPACE   + GPGC_LAYOUT_PAD)
#define GPGC_END_OF_SMALL_SPACE     (GPGC_START_OF_SMALL_SPACE + GPGC_MEMORY_TIER_SIZE)

// Start of secondary heap structures, in large GPGC pages:
#define GPGC_START_OF_STRUCTURES    (__GPGC_HEAP_STRUCTURES_START_ADDR__ >> LogBytesPerGPGCPage)
#define GPGC_END_OF_STRUCTURES      (__GPGC_HEAP_STRUCTURES_END_ADDR__   >> LogBytesPerGPGCPage)

#else
#error Unknown Architecture
#endif


//+++
//+++  Calculate the size of various regions from the constants above:
//+++

// Total space for secondary heap structures, in large GPGC pages:
#define GPGC_STRUCTURE_SIZE            (GPGC_END_OF_STRUCTURES - GPGC_START_OF_STRUCTURES)

// Total space for strong mark bitmap, in large GPGC pages.  1 bit per word of heap.
#define GPGC_STRONG_MARK_BITMAP_SIZE   (GPGC_END_OF_HEAP_VM >> LogBitsPerWord)

// Total space for final mark bitmap, in large GPGC pages.  1 bit per word of heap.
#define GPGC_FINAL_MARK_BITMAP_SIZE    (GPGC_END_OF_HEAP_VM >> LogBitsPerWord)

// Total space for verify mark bitmap, in large GPGC pages.  1 bit per word of heap.
#define GPGC_VERIFY_MARK_BITMAP_SIZE   (GPGC_END_OF_HEAP_VM >> LogBitsPerWord)

// Total space for mark throughs bitmap, in large GPGC pages.  1 bit per word of heap.
#define GPGC_MARK_THROUGHS_BITMAP_SIZE (GPGC_END_OF_HEAP_VM >> LogBitsPerWord)

// Total space for mark IDs bytemap, in large GPGC Pages.  1 byte per word of heap.
#define GPGC_MARK_IDS_BYTEMAP_SIZE     (GPGC_END_OF_HEAP_VM >> LogBytesPerWord)

// Total space for card mark bitmap, in large GPGC pages.  1 bit per word of heap.
#define GPGC_CARDMARK_BITMAP_SIZE      (GPGC_END_OF_HEAP_VM >> LogBitsPerWord)

// Total space for card mark bytemap, in large GPGC pages.  1 per per cacheline of card mark bitmap.
#define GPGC_CARDMARK_BYTEMAP_SIZE     (GPGC_CARDMARK_BITMAP_SIZE >> LogBytesPerCacheLine)

// Space needed for page GPGC_PageInfos, in large GPGC pages:
#define GPGC_INFO_SPACE_SIZE           (((GPGC_END_OF_HEAP_VM * SIZEOF_GPGC_PAGE_INFO) + BytesPerGPGCPage - 1) >> LogBytesPerGPGCPage)

// Space needed for page audit trail, in large GPGC pages:
#define GPGC_PAGE_AUDIT_SIZE           (((GPGC_END_OF_HEAP_VM * SIZEOF_GPGC_PAGE_AUDIT) + BytesPerGPGCPage - 1) >> LogBytesPerGPGCPage)

#define GPGC_MAX_GC_THREAD_COUNT       (1024)

// Maximum object forwarding space is whatever's left over after other GC structures are allocated:
#define GPGC_FORWARDING_SIZE        (GPGC_STRUCTURE_SIZE               \
                                     - (11*GPGC_LAYOUT_PAD)            \
                                     - GPGC_STRONG_MARK_BITMAP_SIZE    \
                                     - GPGC_FINAL_MARK_BITMAP_SIZE     \
                                     - GPGC_VERIFY_MARK_BITMAP_SIZE    \
                                     - GPGC_MARK_THROUGHS_BITMAP_SIZE  \
                                     - GPGC_MARK_IDS_BYTEMAP_SIZE      \
                                     - GPGC_CARDMARK_BITMAP_SIZE       \
                                     - GPGC_CARDMARK_BYTEMAP_SIZE      \
                                     - GPGC_INFO_SPACE_SIZE            \
                                     - GPGC_PAGE_AUDIT_SIZE            \
                                     - GPGC_MAX_GC_THREAD_COUNT)


class GPGC_Layout:AllStatic{
  public:
    // The Generational Pauseless GC Memory Map is kept in this class.  GPGC_Layout::initialize() asserts
    // many things about the layout.

    // By convention, all address space layout is in done with Large GPGC page counts.

    static const PageNum  start_of_small_space          = GPGC_START_OF_SMALL_SPACE;
    static const PageNum  end_of_small_space            = GPGC_END_OF_SMALL_SPACE;

    static const PageNum  start_of_mid_space            = GPGC_START_OF_MID_SPACE;
    static const PageNum  end_of_mid_space              = GPGC_END_OF_MID_SPACE;

    static const PageNum  start_of_large_space          = GPGC_START_OF_LARGE_SPACE;
    static const PageNum  end_of_large_space            = GPGC_END_OF_LARGE_SPACE;

    static const PageNum  start_of_heap_range           = start_of_mid_space;
    static const PageNum  end_of_heap_range             = end_of_small_space;

    static const PageNum  start_of_strong_mark_bitmap   = GPGC_START_OF_STRUCTURES      + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_strong_mark_bitmap     = start_of_strong_mark_bitmap   + GPGC_STRONG_MARK_BITMAP_SIZE;

    static const PageNum  start_of_final_mark_bitmap    = end_of_strong_mark_bitmap     + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_final_mark_bitmap      = start_of_final_mark_bitmap    + GPGC_FINAL_MARK_BITMAP_SIZE;

    static const PageNum  start_of_verify_mark_bitmap   = end_of_final_mark_bitmap      + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_verify_mark_bitmap     = start_of_verify_mark_bitmap   + GPGC_VERIFY_MARK_BITMAP_SIZE;

    static const PageNum  start_of_mark_throughs_bitmap = end_of_verify_mark_bitmap     + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_mark_throughs_bitmap   = start_of_mark_throughs_bitmap + GPGC_MARK_THROUGHS_BITMAP_SIZE;

    static const PageNum  start_of_mark_ids_bytemap     = end_of_mark_throughs_bitmap   + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_mark_ids_bytemap       = start_of_mark_ids_bytemap     + GPGC_MARK_IDS_BYTEMAP_SIZE;

    static const PageNum  start_of_cardmark_bitmap      = end_of_mark_ids_bytemap       + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_cardmark_bitmap        = start_of_cardmark_bitmap      + GPGC_CARDMARK_BITMAP_SIZE;

    static const PageNum  start_of_cardmark_bytemap     = end_of_cardmark_bitmap        + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_cardmark_bytemap       = start_of_cardmark_bytemap     + GPGC_CARDMARK_BYTEMAP_SIZE;

    static const PageNum  start_of_page_info            = end_of_cardmark_bytemap       + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_page_info              = start_of_page_info            + GPGC_INFO_SPACE_SIZE;

    static const PageNum  start_of_page_audit           = end_of_page_info              + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_page_audit             = start_of_page_audit           + GPGC_PAGE_AUDIT_SIZE;

    static const PageNum  start_of_preallocated         = end_of_page_audit             + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_preallocated           = start_of_preallocated         + GPGC_MAX_GC_THREAD_COUNT;

    static const PageNum  start_of_object_forwarding    = end_of_preallocated           + GPGC_LAYOUT_PAD;
    static const PageNum  end_of_object_forwarding      = start_of_object_forwarding    + GPGC_FORWARDING_SIZE;

    static const PageNum  start_of_structures           = GPGC_START_OF_STRUCTURES;
    static const PageNum  end_of_structures             = GPGC_END_OF_STRUCTURES;

    static const PageNum  start_of_heap_mirror          = GPGC_START_OF_HEAP_MIRROR;
    static const PageNum  end_of_heap_mirror            = GPGC_END_OF_HEAP_MIRROR;

    static const PageNum  heap_mirror_offset            = start_of_heap_mirror - start_of_heap_range;


    static inline bool page_in_heap_range(PageNum page);
    static inline bool page_in_valid_range(PageNum page);
  

  public:
    static HeapWord* DataPageNum_to_addr          (PageNum     page)   { return (HeapWord*)(uintptr_t(page)<<LogBytesPerGPGCDataPage);  }
    static HeapWord* PageNum_to_addr              (PageNum     page)   { return (HeapWord*)(uintptr_t(page)<<LogBytesPerGPGCPage); }
    static PageNum   addr_to_DataPageNum          (const void* addr)   { return (PageNum)  (uintptr_t(addr)>>LogBytesPerGPGCDataPage);  }
    static PageNum   addr_to_PageNum              (const void* addr)   { return (PageNum)  (uintptr_t(addr)>>LogBytesPerGPGCPage); }

    static PageNum   addr_to_MidSpaceBasePageNum  (const void* addr)   { return (PageNum(addr)>>LogBytesPerMidSpaceBlock) << LogPagesPerMidSpaceBlock;}
    static PageNum   page_to_MidSpaceBasePageNum  (const PageNum page) { return (page >> LogPagesPerMidSpaceBlock) << LogPagesPerMidSpaceBlock;}

    static PageNum   addr_to_BasePageForSpace     (const void* addr)   { return mid_space_addr(addr)
                                                                                   ? addr_to_MidSpaceBasePageNum(addr)
                                                                                   : addr_to_PageNum(addr);
                                                                       }

  public:
    static HeapWord* strong_mark_bitmap_base   ()                { return PageNum_to_addr(start_of_strong_mark_bitmap); }
    static HeapWord* final_mark_bitmap_base    ()                { return PageNum_to_addr(start_of_final_mark_bitmap); }
    static HeapWord* verify_mark_bitmap_base   ()                { return PageNum_to_addr(start_of_verify_mark_bitmap); }
    static HeapWord* mark_throughs_bitmap_base ()                { return PageNum_to_addr(start_of_mark_throughs_bitmap); }
    static uint8_t*  mark_ids_bytemap_base     ()                { return (uint8_t*) PageNum_to_addr(start_of_mark_ids_bytemap); }

  public:
    static        void      initialize();

    static inline long      mid_space_object_size_alignment_word_size() { return GPGC_WordsMidSpaceObjectSizeAlignment; }
    static inline ulong     mid_space_min_object_words ()        { return 1 << LogWordsMidSpaceMinObjectSize; }
    static inline ulong     large_space_min_object_words ()      { return 1 << LogWordsLargeSpaceMinObjectSize; }

#ifdef DEBUG
    static inline bool      small_space_page  (PageNum page)     { return page >= start_of_small_space && page < end_of_small_space; }
    static inline bool      mid_space_page    (PageNum page)     { return page >= start_of_mid_space   && page < end_of_mid_space;   }
    static inline bool      large_space_page  (PageNum page)     { return page >= start_of_large_space && page < end_of_large_space; }
#else  // !DEBUG:
    static inline bool      small_space_page  (PageNum page)     { return page >= start_of_small_space; }
    static inline bool      mid_space_page    (PageNum page)     { return page <  end_of_mid_space; }
    static inline bool      large_space_page  (PageNum page)     { return page >= start_of_large_space && page < end_of_large_space; }
#endif // !DEBUG

    static inline bool      one_space_page    (PageNum page)     { return small_space_page(page) || mid_space_page(page); }

    static inline bool      small_space_addr  (const void* addr) { return small_space_page(addr_to_PageNum(addr)); }
    static inline bool      mid_space_addr    (const void* addr) { return mid_space_page  (addr_to_PageNum(addr)); }
    static inline bool      large_space_addr  (const void* addr) { return large_space_page(addr_to_PageNum(addr)); }

    static inline bool      is_shattered_address(intptr_t addr);
    static inline bool      is_object_forwarding(void* addr);
};


inline bool GPGC_Layout::page_in_heap_range(PageNum page) {
  return (page>=start_of_heap_range) && (page<end_of_heap_range);
}
inline bool GPGC_Layout::page_in_valid_range(PageNum page) {
  return (page>=start_of_heap_range) && (page<end_of_heap_mirror);
}


inline bool GPGC_Layout::is_object_forwarding(void* addr) {
  PageNum page = addr_to_PageNum(addr);
  return ( (page>=start_of_object_forwarding) && (page<end_of_object_forwarding) );
}
  
#endif // GPGC_LAYOUT_HPP
