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
#include "bitMap.hpp"
#include "gcLocker.hpp"
#include "gpgc_cardTable.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_marks.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "prefetch.hpp"
#include "tickProfiler.hpp"

#include "gpgc_newCollector.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

#include "oop.inline2.hpp"

GPGC_CardTable*  GPGC_CardTable::_card_table                = NULL;
MemRegion        GPGC_CardTable::_bitmap_reserved_region;
MemRegion        GPGC_CardTable::_bytemap_reserved_region;
volatile int8_t* GPGC_CardTable::_bitmap_pages_mapped       = NULL;
volatile int8_t* GPGC_CardTable::_bytemap_pages_mapped      = NULL;
uint16_t*        GPGC_CardTable::_real_page_set_card_counts = NULL;
uint16_t*        GPGC_CardTable::_page_set_card_counts      = NULL;


GPGC_CardTable::GPGC_CardTable()
  : BarrierSet(1)
{
  _card_table = this;

  // Initialize virtual memory management for the cardmark bitmap and summary bytemap.
  char* bitmap_reservation_start  = (char*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_cardmark_bitmap );
  long  bitmap_reservation_pages  = GPGC_Layout::end_of_cardmark_bitmap - GPGC_Layout::start_of_cardmark_bitmap;
  long  bitmap_reservation_words  = bitmap_reservation_pages << LogWordsPerGPGCPage;

  char* bytemap_reservation_start = (char*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_cardmark_bytemap );
  long  bytemap_reservation_pages = GPGC_Layout::end_of_cardmark_bytemap - GPGC_Layout::start_of_cardmark_bytemap;
  long  bytemap_reservation_words = bytemap_reservation_pages << LogWordsPerGPGCPage;

  _bitmap_reserved_region         = MemRegion((HeapWord*)bitmap_reservation_start,  bitmap_reservation_words);
  _bitmap_pages_mapped            = NEW_C_HEAP_ARRAY(int8_t, bitmap_reservation_pages);

  _bytemap_reserved_region        = MemRegion((HeapWord*)bytemap_reservation_start, bytemap_reservation_words);
  _bytemap_pages_mapped           = NEW_C_HEAP_ARRAY(int8_t, bytemap_reservation_pages);

  memset((void*)_bitmap_pages_mapped,  0, sizeof(int8_t)*bitmap_reservation_pages);
  memset((void*)_bytemap_pages_mapped, 0, sizeof(int8_t)*bytemap_reservation_pages);

  // TODO: guarantee that the carmark address calculations for all heap pages fit within the regions reserves.

  // TODO: maw: make card-mark verification be an option, and only allocate if it's set:
  long page_count            = GPGC_Layout::end_of_heap_range - GPGC_Layout::start_of_heap_range;
  _real_page_set_card_counts = NEW_C_HEAP_ARRAY(uint16_t, page_count);
  _page_set_card_counts      = _real_page_set_card_counts - GPGC_Layout::start_of_heap_range;
}


// Verify that the sections of the cardmark bitmap and bytemap that cover this heap
// page is mapped into memory.  If not, allocate it.
bool GPGC_CardTable::allocate_cardmark_for_heap(PageNum page)
{
  long    word_number  = page<<LogWordsPerGPGCPage;
  PageNum bitmap_page  = word_number >> (LogBitsPerWord + LogWordsPerGPGCPage);
  PageNum bytemap_page = word_number >> (LogWordsPerGPGCCardMarkSummaryByte + LogBytesPerGPGCPage);

  if ( !_bitmap_pages_mapped[bitmap_page] ) {
    // Locking protects against multiple threads racing to allocate the same cardmark page
MutexLocker ml(GPGC_CardTable_lock);

    if ( !_bitmap_pages_mapped[bitmap_page] ) {
      PageNum page = GPGC_Layout::start_of_cardmark_bitmap + bitmap_page;
      if ( ! os::commit_memory((char *)GPGC_Layout::PageNum_to_addr(page), BytesPerGPGCPage, Modules::GPGC_CardTable) ) {
        return false;
        // TODO Make sure GC won't fail to make forward progress if the
        // system is too low on memory to allocate another cardmark page.
      }

      if ( !_bytemap_pages_mapped[bytemap_page] ) {
        PageNum page = GPGC_Layout::start_of_cardmark_bytemap + bytemap_page;
        if ( ! os::commit_memory((char *)GPGC_Layout::PageNum_to_addr(page), BytesPerGPGCPage, Modules::GPGC_CardTable) ) {
          return false;
          // TODO Make sure GC won't fail to make forward progress if the
          // system is too low on memory to allocate another cardmark page.
        }

        _bytemap_pages_mapped[bytemap_page] = true;
      }

      _bitmap_pages_mapped[bitmap_page] = true;
    }
  }

  return true;
}


bool GPGC_CardTable::allocate_cardmark_for_heap(PageNum start_page, PageNum end_page)
{
for(PageNum i=start_page;i<end_page;i++){
    if ( ! allocate_cardmark_for_heap(i) ) {
      return false;
    }
  }

  return true;
}


void GPGC_CardTable::clear_card_marks(PageNum page)
{
  // Make sure one of following rules are followed by the callers:
  // 1. its a small space page.
  // 2. its a mid-space page & is the base page of the Mid-space page block
  // 3. its a large space page & is the first page (a.k.a: block page) of the block.
  assert0(GPGC_Layout::small_space_page(page)
          || (GPGC_Layout::mid_space_page(page) && GPGC_Layout::page_to_MidSpaceBasePageNum(page) == page )
          || (GPGC_Layout::large_space_page(page)
              && GPGC_PageInfo::page_info(page) != NULL));

  HeapWord*  page_base = GPGC_Layout::PageNum_to_addr(page);
  
  long       bytemap_base_index   = intptr_t(page_base) >> (LogBytesPerWord + LogWordsPerGPGCCardMarkSummaryByte);
  long       bitmap_base_index    = intptr_t(page_base) >> (LogBytesPerWord + LogBitsPerByte);

  uint8_t*   card_bitmap          = ((uint8_t*)bitmap_base()) + bitmap_base_index;
  uint8_t*   card_summary_bytemap = ((uint8_t*) bytemap_base()) + bytemap_base_index;

  // Based on the page space, determine the number of words in this small-space page
  // or mid/large space block.
  size_t     words_per_page = GPGC_Layout::small_space_page(page) ? WordsPerGPGCPage :
                              GPGC_Layout::mid_space_page(page) ? WordsPerMidSpaceBlock :
                              (WordsPerGPGCPage * GPGC_PageInfo::page_info(page)->block_size());

  size_t     bitmap_bytes   = words_per_page >> LogBitsPerByte;
  size_t     bytemap_bytes  = words_per_page >> LogWordsPerGPGCCardMarkSummaryByte;

  memset(card_bitmap,          0, bitmap_bytes);
  memset(card_summary_bytemap, 0, bytemap_bytes);
}


void GPGC_CardTable::set_mark_range_within_word(BitMap::idx_t start_bit, BitMap::idx_t end_bit, HeapWord* bitmap_base)
{
  assert(start_bit==end_bit || BitMap::word_index(start_bit)==BitMap::word_index(end_bit-1), "must be a single word range");
assert(end_bit!=0,"does not work when end_bit == 0");

  if ( start_bit != end_bit ) {
    jlong* word_addr = (jlong*)(bitmap_base + BitMap::word_index(start_bit));
    jlong  mask      = (jlong) (~ BitMap::inverted_bit_mask_for_range(start_bit, end_bit));
    jlong  old_word;
    jlong  new_word;

    do {
      old_word = *word_addr;

      if ( (old_word & mask) == mask ) return;

      new_word = old_word | mask;
    } while ( old_word != Atomic::cmpxchg(new_word, word_addr, old_word) );
  }
}


void GPGC_CardTable::card_mark_across_region(HeapWord* start, HeapWord* end)
{
  assert0( start <= end );

  // Set bitmap first.  start and end both get marked, but the bitmap algorithm
  // marks [start, end), so we add one to end when calculating the bitmap_end_index:
  BitMap::idx_t bitmap_start_index     = intptr_t(start) >> (LogBytesPerWord);
  BitMap::idx_t bitmap_end_index       = intptr_t(end+1) >> (LogBytesPerWord);

  BitMap::idx_t bitmap_start_full_word = BitMap::word_index(bitmap_start_index+BitsPerWord-1);
  BitMap::idx_t bitmap_end_full_word   = BitMap::word_index(bitmap_end_index);

  if ( bitmap_start_full_word < bitmap_end_full_word ) {
    set_mark_range_within_word(bitmap_start_index, BitMap::bit_index(bitmap_start_full_word), bitmap_base());
    memset(bitmap_base()+bitmap_start_full_word, 0xFF, (bitmap_end_full_word-bitmap_start_full_word)<<LogBytesPerWord);
    set_mark_range_within_word(BitMap::bit_index(bitmap_end_full_word), bitmap_end_index, bitmap_base());
  } else {
    long boundary = MIN2(BitMap::bit_index(bitmap_start_full_word), bitmap_end_index);
    set_mark_range_within_word(bitmap_start_index, boundary,         bitmap_base());
    set_mark_range_within_word(boundary,           bitmap_end_index, bitmap_base());
  }

  // Then set bytemap.  This algorith marks [start, end].
  BitMap::idx_t bytemap_start_index  = intptr_t(start) >> (LogBytesPerWord + LogWordsPerGPGCCardMarkSummaryByte);
  BitMap::idx_t bytemap_end_index    = intptr_t(end)   >> (LogBytesPerWord + LogWordsPerGPGCCardMarkSummaryByte);
  uint8_t*      card_summary_bytemap = ((uint8_t*) bytemap_base()) + bytemap_start_index;
  size_t        bytemap_bytes        = bytemap_end_index - bytemap_start_index + 1;

  memset(card_summary_bytemap, 1, bytemap_bytes);

#ifdef ASSERT
  for ( HeapWord* ptr=start; ptr<end; ptr++ ) {
    assert0(GPGC_Marks::is_card_marked((objectRef*)ptr));
  }
#endif // ASSERT
}


void GPGC_CardTable::scan_cardmarks_for_page(GPGC_GCManagerNewStrong* gcm, PageNum page, GPGC_PageInfo::States page_state)
{
  // This code scans the cardmark bitmap looking for set bits.  When it finds them,
  // it sends the associated object word address to the GC manager for marking.
  //
  // Two approaches are used to improve the performance of the scan:
  //
  // 1. Each cache-line of cardmark bitmap has an associated cardmark summary byte.  We only
  //    read the portion of the bitmap covered by non-zero cardmark summary bytes.
  //
  // 2. We maintain a prefetch pipeline for each part of memory:
  //      - The cardmark summary bytemap
  //      - The dardmark bitmap
  //      - The object words in the heap.
  //
  // TODO: Implement prefetch pipeline once the base scan code is tested and working.

  // Calculate the bitmap base that matches the summary bytemap we're scanning.
  HeapWord*  page_base            = GPGC_Layout::PageNum_to_addr(page);

  long       bytemap_base_index   = intptr_t(page_base) >> (LogBytesPerWord + LogWordsPerGPGCCardMarkSummaryByte);
  long       bitmap_base_index    = bytemap_base_index  << (LogWordsPerGPGCCardMarkSummaryByte - LogBitsPerWord);
  long       objects_base_index   = bytemap_base_index  << LogWordsPerGPGCCardMarkSummaryByte;

  uint8_t*   card_summary_bytemap = ((uint8_t*) bytemap_base()) + bytemap_base_index;
  uintptr_t* card_bitmap          = ((uintptr_t*)bitmap_base()) + bitmap_base_index;
  heapRef*   objects_base         = ((heapRef*)NULL)            + objects_base_index;

  long       card_summary_bytes   = WordsPerGPGCPage >> LogWordsPerGPGCCardMarkSummaryByte;
  long       bytemap_cachelines   = (card_summary_bytes + BytesPerCacheLine - 1) >> LogBytesPerCacheLine;

  long       byte_cursor          = 0;

  if ( page_state == GPGC_PageInfo::Relocating ) {
    // If we're scanning a small space page that's in the Relocating state, then the objects
    // will be in the mirror page.
    objects_base += GPGC_Layout::heap_mirror_offset << LogWordsPerGPGCPage;
  }

  // TODO code asserts to ensure we're only scanning the bitmap for the page desired.

  for ( long i=0; i<bytemap_cachelines; i++ ) {
    for ( long j=0; j<BytesPerCacheLine; j++,byte_cursor++ ) {
      uint8_t byte = card_summary_bytemap[byte_cursor];

      if ( byte != 0 ) {
        long bitmap_cursor = byte_cursor << (LogWordsPerGPGCCardMarkSummaryByte-LogBitsPerWord);
        long end_bitmap    = bitmap_cursor + (1 << (LogWordsPerGPGCCardMarkSummaryByte-LogBitsPerWord));

        for ( ; bitmap_cursor<end_bitmap; bitmap_cursor++ ) {
          uintptr_t bitmap_word = card_bitmap[bitmap_cursor];

          if ( bitmap_word == 0 ) continue;

          heapRef* word_base = objects_base + (bitmap_cursor << LogBitsPerWord);

          do {
            long     first    = BitOps::first_bit(bitmap_word);
            heapRef* ref_addr = word_base + first;

            bitmap_word ^= 1UL << first;

            GPGC_NewCollector::mark_and_push_from_cardmark(gcm, ref_addr);
          } while (bitmap_word != 0);
        }
      }
    }
  }
}


void GPGC_CardTable::scan_page_multi_space(GPGC_GCManagerNewStrong* gcm, PageNum block, long chunk, long chunks)
{
GCModeSetter mode_setter(Thread::current());

  GPGC_PageInfo* info  = GPGC_PageInfo::page_info(block);
  long           pages = info->block_size();

#ifdef ASSERT
  {
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    assert0( pages > 1 );
    assert0( state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated );
    assert0( gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen );
  }
#endif // ASSERT

  long chunk_size  = pages / chunks;
  long chunk_start = chunk * chunk_size;
  long chunk_end   = chunk_start + chunk_size;

  if ( chunk+1 == chunks ) chunk_end = pages;

  assert0( chunk_size > 0 );

  for ( long i=chunk_start; i<chunk_end; i++ ) {
    scan_cardmarks_for_page(gcm, block+i, GPGC_PageInfo::Allocated);
  }
}


long GPGC_CardTable::verify_cardmark_bitmap(uintptr_t* bitmap, long size, heapRef* objs_base, bool all_zero)
{
  long card_count = 0;

  for ( long i=0; i<size; i++ ) {
uintptr_t word=bitmap[i];

    if ( word == 0 ) continue;

    heapRef* word_base = objs_base + (i<<LogBitsPerWord);

    do {
      long     first = BitOps::first_bit(word);
      heapRef* addr  = word_base + first;
      heapRef  ref   = *addr;
 
      card_count ++;

      guarantee(all_zero == false, "Found cardmark when none expected");

if(ref.not_null()){
        assert0( ref.is_new() || ref.is_old() );
        PageNum               ref_page = GPGC_Layout::addr_to_PageNum(ref.as_oop());
        GPGC_PageInfo*        pg_info  = GPGC_PageInfo::page_info(ref_page);
        GPGC_PageInfo::States state    = pg_info->just_state();

        assert0( state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Allocated ||
                 state==GPGC_PageInfo::Relocating || state==GPGC_PageInfo::Relocated );
      }

      word ^= 1UL << first;
    } while (word);
  }

  return card_count;
}


void GPGC_CardTable::verify_marks_for_page(PageNum page, bool can_change)
{
Unimplemented();//TODO-LATER: this verification method isn't presently called.
  /*
  HeapWord*      page_base      = GPGC_Layout::PageNum_to_addr(page);
  heapRef*       objs_base      = (heapRef*) (page_base + GPGC_SmallSpaceObjectsStartWord);
  GPGC_PageInfo* pg_info        = GPGC_PageInfo::page_info(page);
  long           card_count     = 0;
  
  if ( GPGC_Layout::one_space_page(page) ) {
    HeapWord* cards_base = page_base + GPGC_CardMarksStartWord;
    card_count = verify_cardmark_bitmap((uintptr_t*)cards_base, GPGC_MarksWordSize, objs_base, false);
  } else {
    long      card_mark_offset = pg_info->card_mark_offset();
    HeapWord* cards_base       = page_base + card_mark_offset;
    long      card_words       = (card_mark_offset - GPGC_SmallSpaceObjectsStartWord) >> 6; // One word per 64 object-words.

    assert(pg_info->block_size() > 1, "Found bogus block size in multi-page space");

    card_count = verify_cardmark_bitmap((uintptr_t*)cards_base, card_words, objs_base, false);
  }

  if ( can_change ) {
    _page_set_card_counts[page] = card_count;
  } else {
    long old_count = _page_set_card_counts[page];
    guarantee(old_count == card_count, "cardmark count changed");
  }
  */
}


void GPGC_CardTable::verify_no_marks_for_page(PageNum page)
{
Unimplemented();//TODO-LATER: this verification method isn't presently called.
  /*
  HeapWord*      page_base      = GPGC_Layout::PageNum_to_addr(page);
  heapRef*       objs_base      = (heapRef*) (page_base + GPGC_SmallSpaceObjectsStartWord);
  GPGC_PageInfo* pg_info        = GPGC_PageInfo::page_info(page);
  long           card_count     = 0;

  if ( GPGC_Layout::one_space_page(page) ) {
    HeapWord* cards_base = page_base + GPGC_CardMarksStartWord;
    card_count = verify_cardmark_bitmap((uintptr_t*)cards_base, GPGC_MarksWordSize, objs_base, true);
  } else {
    long      card_mark_offset = pg_info->card_mark_offset();
    HeapWord* cards_base       = page_base + card_mark_offset;
    long      card_words       = (card_mark_offset - GPGC_SmallSpaceObjectsStartWord) >> 6; // One word per 64 object-words.

    assert(pg_info->block_size() > 1, "Found bogus block size in multi-page space");

    card_count = verify_cardmark_bitmap((uintptr_t*)cards_base, card_words, objs_base, true);
  }

  guarantee(card_count == 0, "Page found with cardmarks set");

  _page_set_card_counts[page] = card_count;
  */
}
