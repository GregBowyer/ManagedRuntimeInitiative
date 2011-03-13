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


#include "bitOps.hpp"
#include "collectedHeap.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_readTrapArray.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

#include "oop.inline2.hpp"

void GPGC_Marks::verify_new_marked_live(objectRef ref)
{
  Thread* thread = Thread::current();
  guarantee(GPGC_Collector::is_new_collector_thread(thread), "only NewGC threads should check NewGen object liveness");
  guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "can't check liveness on unremapped refs");
}


void GPGC_Marks::verify_old_marked_live(objectRef ref)
{
  Thread* thread = Thread::current();
  guarantee(GPGC_Collector::is_old_collector_thread(thread), "only OldGC threads should check OldGen object liveness");
  guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "can't check liveness on unremapped refs");
}


void GPGC_Marks::verify_perm_marked_live(oop obj)
{
guarantee(obj->is_perm(),"non-perm object checked for liveness");
  Thread*   thread = Thread::current();
objectRef ref=objectRef(obj);
  guarantee(GPGC_Collector::is_old_collector_thread(thread), "only OldGC threads should check OldGen object liveness");
  guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "can't check liveness on unremapped refs");
}


// Calculate how many words of card-mark-table is required for a multi-page object.
long GPGC_Marks::card_mark_words(long object_words)
{
  // Do a fast version of ceil(object_words / BitsPerWord):
  long card_mark_words = (object_words + BitsPerWord - 1) >> LogBitsPerWord; // One card mark word per 64 object-words.
  assert((card_mark_words&(WordsPerCacheLine-1))==0, "CardMark bitmap must be a multiple of WordsPerCacheLine");
  return card_mark_words;
}
long GPGC_Marks::card_mark_summary_bytes(long object_words)
{
  // Do a fast version of ceil(object_words / WordsPerGPGCCardMarkSummaryByte):
  long summary_bytes = (object_words + WordsPerGPGCCardMarkSummaryByte - 1) >> LogWordsPerGPGCCardMarkSummaryByte;
  return summary_bytes;
}
long GPGC_Marks::total_card_mark_overhead_words(long object_words)
{
  // Get the size of the card mark bitmap.
  long card_words = card_mark_words(object_words);

  // Get the size of the card mark summary bitmap:
  long summary_words = (object_words + (WordsPerGPGCCardMarkSummaryByte << LogBytesPerWord) - 1)
                       >> (LogWordsPerGPGCCardMarkSummaryByte + LogBytesPerWord);
  
  // Return the total:
  return (card_words + summary_words);
}


long GPGC_Marks::marks_byte_size(PageNum page, long pages)
{
  long byte_size;

  if ( GPGC_Layout::small_space_page(page) ) {
    byte_size = GPGC_SmallSpaceMarkWordsPerPage << LogBytesPerWord;
  } else {
    byte_size = GPGC_NonSmallSpaceMarkWordsPerPage << LogBytesPerWord;
  }

  byte_size *= pages;

  return byte_size;
}


void GPGC_Marks::clear_mark_bitmap(HeapWord* page_base, HeapWord* bitmap_base, long bitmap_byte_size)
{
  long      base_bit  = bit_index_for_mark(intptr_t(page_base));
  long      base_word = word_index(base_bit);

  bitmap_base += base_word;

  memset( bitmap_base, 0, bitmap_byte_size );
}


#ifndef PRODUCT

void GPGC_Marks::clear_mark_throughs(HeapWord* page_base, long pages)
{
  HeapWord* bitmap_base = GPGC_Layout::mark_throughs_bitmap_base();
  long      base_bit    = bit_index_for_marked_through(intptr_t(page_base));
  long      base_word   = word_index(base_bit);
  long      byte_size   = GPGC_MarkThroughsWordsPerPage << LogBytesPerWord;

  bitmap_base += base_word;
  byte_size   *= pages;

  memset( bitmap_base, 0, byte_size );
}

void GPGC_Marks::clear_mark_ids(HeapWord* page_base, long pages)
{
  uint8_t* bytemap_base = GPGC_Layout::mark_ids_bytemap_base();
  long     base_byte    = byte_index_for_markid(intptr_t(page_base));
  long     byte_size    = GPGC_MarkIDsWordsPerPage << LogBytesPerWord;

  bytemap_base += base_byte;
  byte_size    *= pages;

  memset( bytemap_base, 0, byte_size );
}

#endif // ! PRODUCT


void GPGC_Marks::clear_all_metadata(PageNum page, long pages)
{
  HeapWord* page_base         = GPGC_Layout::PageNum_to_addr(page);
  long      bitmap_byte_size  = marks_byte_size(page, pages);

  clear_mark_bitmap(page_base, GPGC_Layout::strong_mark_bitmap_base(), bitmap_byte_size);
  clear_mark_bitmap(page_base, GPGC_Layout::final_mark_bitmap_base(),  bitmap_byte_size);

  if ( GPGCVerifyHeap ) {
    clear_mark_bitmap(page_base, GPGC_Layout::verify_mark_bitmap_base(), bitmap_byte_size);
  }

  if ( GPGCAuditTrail ) {
    clear_mark_throughs(page_base, pages);
    clear_mark_ids     (page_base, pages);
  }
}


void GPGC_Marks::clear_live_marks_bitmap(PageNum page, long pages)
{
  HeapWord* page_base        = GPGC_Layout::PageNum_to_addr(page);
  long      bitmap_byte_size = marks_byte_size(page, pages);

  clear_mark_bitmap(page_base, GPGC_Layout::strong_mark_bitmap_base(), bitmap_byte_size);
  clear_mark_bitmap(page_base, GPGC_Layout::final_mark_bitmap_base(),  bitmap_byte_size);

  if ( GPGCAuditTrail ) {
    clear_mark_throughs(page_base, pages);
    clear_mark_ids     (page_base, pages);
  }
}


#ifndef PRODUCT

void GPGC_Marks::clear_verify_marks_bitmap(PageNum page, long pages)
{
  if ( GPGCVerifyHeap ) {
    HeapWord* page_base        = GPGC_Layout::PageNum_to_addr(page);
    long      bitmap_byte_size = marks_byte_size(page, pages);

    clear_mark_bitmap(page_base, GPGC_Layout::verify_mark_bitmap_base(), bitmap_byte_size);
  }
}

#endif // ! PRODUCT


// Return the number of "live" bits in the strong marking bitmap for a range of pages.
long GPGC_Marks::count_live_marks(PageNum page, long pages)
{
  HeapWord*  page_base        = GPGC_Layout::PageNum_to_addr(page);
  long       bitmap_word_size = marks_byte_size(page, pages) >> LogBytesPerWord;
  uintptr_t* bitmap_base      = (uintptr_t*)GPGC_Layout::strong_mark_bitmap_base();
  long       base_bit         = bit_index_for_mark(intptr_t(page_base));
  long       base_word        = word_index(base_bit);

  assert((base_word<<LogBitsPerWord) == base_bit, "mark bitmap for page not word aligned");

  bitmap_base += base_word;

  long count = 0;

  for ( long i=0; i<bitmap_word_size; i++ ) {
    count += BitOps::bit_count(bitmap_base[i]);
  }

  return count;
}
