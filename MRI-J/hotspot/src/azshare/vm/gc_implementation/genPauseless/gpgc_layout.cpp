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


#include "gpgc_layout.hpp"
#include "gpgc_pageInfo.hpp"

#include "math.h"


const PageNum GPGC_Layout::start_of_small_space;
const PageNum GPGC_Layout::end_of_small_space;
const PageNum GPGC_Layout::start_of_mid_space;
const PageNum GPGC_Layout::end_of_mid_space;
const PageNum GPGC_Layout::start_of_large_space;
const PageNum GPGC_Layout::end_of_large_space;
const PageNum GPGC_Layout::start_of_heap_range;
const PageNum GPGC_Layout::end_of_heap_range;
const PageNum GPGC_Layout::start_of_cardmark_bitmap;
const PageNum GPGC_Layout::end_of_cardmark_bitmap;
const PageNum GPGC_Layout::start_of_cardmark_bytemap;
const PageNum GPGC_Layout::end_of_cardmark_bytemap;
const PageNum GPGC_Layout::start_of_page_info;
const PageNum GPGC_Layout::end_of_page_info;
const PageNum GPGC_Layout::start_of_page_audit;
const PageNum GPGC_Layout::end_of_page_audit;
const PageNum GPGC_Layout::start_of_preallocated;
const PageNum GPGC_Layout::end_of_preallocated;
const PageNum GPGC_Layout::start_of_object_forwarding;
const PageNum GPGC_Layout::end_of_object_forwarding;
const PageNum GPGC_Layout::start_of_structures;
const PageNum GPGC_Layout::end_of_structures;
const PageNum GPGC_Layout::start_of_heap_mirror;
const PageNum GPGC_Layout::end_of_heap_mirror;
const PageNum GPGC_Layout::heap_mirror_offset;


void GPGC_Layout::initialize()
{
  guarantee(SIZEOF_GPGC_PAGE_INFO == sizeof(GPGC_PageInfo), "GPGC_PageInfo size changed");

  guarantee((PageNum)GPGC_END_OF_HEAP_VM >= end_of_small_space, "small space past end of usable virtual address range");
  guarantee((PageNum)GPGC_END_OF_HEAP_VM >= end_of_mid_space,   "mid space past end of usable virtual address range");
  guarantee((PageNum)GPGC_END_OF_HEAP_VM >= end_of_large_space, "large space past end of usable virtual address range");

  guarantee(start_of_heap_range  < end_of_heap_range,  "virtual layout confused");

  guarantee(start_of_small_space < end_of_small_space, "small space inverted");
  guarantee(start_of_mid_space   < end_of_mid_space,   "mid space inverted");
  guarantee(start_of_large_space < end_of_large_space, "large space inverted");

  // Space ordering is important!
  //
  // We put mid space at the bottom, so 32MB PageNums from mid space can't overlap with valid
  // 2MB PageNums from small or large space.  We put small space at the top, because most objects
  // are in small, and we want the test for being in small to be fast: page >= start_of_small_space.
  //
  // If you change this ordering, you may break assumptions elsewhere in the code.

  guarantee(start_of_heap_range == start_of_mid_space, "virtual layout confused");
  guarantee(end_of_mid_space   < start_of_small_space, "mid space and small space out of order");
  guarantee(end_of_mid_space   < start_of_large_space, "mid space and large space out of order");
  guarantee(end_of_large_space < start_of_small_space, "large space and small space out of order");
  guarantee(end_of_heap_range == end_of_small_space,   "virtual layout confused");

  guarantee(start_of_structures > end_of_heap_range, "structures expected after heap");
  guarantee(start_of_structures < end_of_structures, "structure layout confused");

  guarantee(start_of_strong_mark_bitmap   >= start_of_structures, "strong mark bitmap out of order");
  guarantee(start_of_final_mark_bitmap    >= start_of_structures, "final mark bitmap out of order");
  guarantee(start_of_verify_mark_bitmap   >= start_of_structures, "verify mark bitmap out of order");
  guarantee(start_of_mark_throughs_bitmap >= start_of_structures, "mark throughs bitmap out of order");
  guarantee(start_of_mark_ids_bytemap     >= start_of_structures, "mark ids bytemap out of order");
  guarantee(start_of_cardmark_bitmap      >= start_of_structures, "cardmark bitmap out of order");
  guarantee(start_of_cardmark_bytemap     >= start_of_structures, "cardmark bytemap out of order");
  guarantee(start_of_page_info            >= start_of_structures, "page info out of order");
  guarantee(start_of_page_audit           >= start_of_structures, "page audit out of order");
  guarantee(start_of_preallocated         >= start_of_structures, "preallocated pages out of order");
  guarantee(start_of_object_forwarding    >= start_of_structures, "object forwarding out of order");

  guarantee(end_of_strong_mark_bitmap   <= end_of_structures, "strong mark bitmap out of order");
  guarantee(end_of_final_mark_bitmap    <= end_of_structures, "final mark bitmap out of order");
  guarantee(end_of_verify_mark_bitmap   <= end_of_structures, "verify mark bitmap out of order");
  guarantee(end_of_mark_throughs_bitmap <= end_of_structures, "mark throughs bitmap out of order");
  guarantee(end_of_mark_ids_bytemap     <= end_of_structures, "mark ids bytemap out of order");
  guarantee(end_of_cardmark_bitmap      <= end_of_structures, "cardmark bitmap out of order");
  guarantee(end_of_cardmark_bytemap     <= end_of_structures, "cardmark bytemap out of order");
  guarantee(end_of_page_info            <= end_of_structures, "page info out of order");
  guarantee(end_of_page_audit           <= end_of_structures, "page audit out of order");
  guarantee(end_of_preallocated         <= end_of_structures, "preallocated pages out of order");
  guarantee(end_of_object_forwarding    <= end_of_structures, "object forwarding out of order");

  guarantee(sizeof(HeapWord) == sizeof(uintptr_t), "HeapWord isn't uintptr_t size!");

  // I've never tested large page support.
guarantee(LogPagesPerGPGCPage==0,"Untested: Multi-megabyte GPGC pages not yet supported");

  // Do some page layout sanity checking:
  guarantee(GPGC_LogWordsMidSpaceObjectSizeAlignment >= LogWordsPerSmallPage, "bad mid space object size alignment");
  guarantee(GPGC_NonSmallSpaceWordMarksIndexShift == GPGC_LogWordsMidSpaceObjectSizeAlignment, "bad mark resolution versus mid obj alignement");

  // TODO: guarantee mid space bitmap sizes against mid space object words

/*
  guarantee((GPGC_ObjectsWordSize-1)>>LogBitsPerWord < GPGC_CardMarksWordSize, "Need 1 card mark bit per object word");
  guarantee((GPGC_ObjectsWordSize-1)>>(LogWordsPerGPGCCardMarkSummaryByte+LogBytesPerWord) < GPGC_CardMarkSummaryWordSize,
                                                                               "CardMarkSummary word size mismatch");
  guarantee((GPGC_ObjectsWordSize-1)>>(LogWordsPerGPGCCardMarkSummaryByte) < GPGC_CardMarkSummaryByteSize,
                                                                               "CardMarkSummary byte size mismatch");

  guarantee((GPGC_CardMarksStartWord & (WordsPerCacheLine-1))==0, "CardMark bitmap should be cache line aligned");
  guarantee((GPGC_CardMarkSummaryStartWord & (WordsPerCacheLine-1))==0, "CardMarkSummary bytemap should be cache line aligned");
*/

/*
#ifndef PRODUCT
  if ( GPGCVerifyHeap ) {
    guarantee((GPGC_SmallSpaceObjectsWordSize-1)>>LogBitsPerWord < GPGC_SmallSpaceVerifyMarksWordSize, "Need 1 verify mark bit per object word");
  }
  if ( GPGCAuditTrail ) {
    guarantee((GPGC_SmallSpaceObjectsWordSize-1)>>LogBitsPerWord < GPGC_SmallSpaceMarkThroughsWordSize, "Need 1 through bit per object word");
    guarantee((GPGC_SmallSpaceObjectsWordSize-1)>>(LogBytesPerWord+1) < GPGC_SmallSpaceMarkIdsWordSize, "Need 1 mark ID byte per 2 object words");
  }
#endif // ! PRODUCT
*/
}
