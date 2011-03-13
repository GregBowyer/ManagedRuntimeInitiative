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
#ifndef GPGC_MARKS_HPP
#define GPGC_MARKS_HPP


#include "atomic.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_pageInfo.hpp"
#include "prefetch.hpp"
#include "oop.hpp"

class GPGC_SmallSpaceObjectsStartWord;

// Closure for iterating over the live objects in a page.
class GPGC_MarksClosure VALUE_OBJ_CLASS_SPEC{
  public:
    virtual void do_mark(PageNum page, HeapWord* marked) = 0;
};


class objectRef;


// This class handles object marking for the pauseless collector.  Marks are
// kept separately from objects.  Normally, the beginning of each page has a
// number of words comprising a bitmap of marks.  Some pages are "chained"
// together to accomodate objects larger then a single page.  The first page
// in a chain will have a leading mark bitmap, but the following pages in the
// chain will not.
class GPGC_Marks:AllStatic{
  private:
    // Simple bit twiddling ops:
    static inline long      bit_in_word                   (long bit)  { return bit & (BitsPerWord-1); }
    static inline uintptr_t bit_mask                      (long bit)  { return 1L << bit_in_word(bit); }

  public:
    static inline long      word_index                    (long bit)  { return bit >> LogBitsPerWord; }

    static inline long      bit_index_for_mark            (intptr_t addr);
    static inline long      bit_index_for_marked_through  (intptr_t addr);
    static inline long      byte_index_for_markid         (intptr_t addr) PRODUCT_RETURN0;

    static inline bool      atomic_set_bit_in_bitmap      (HeapWord* bitmap, long bit_index);
    static inline bool      is_bit_set_in_bitmap          (HeapWord* bitmap, long bit_index);

    static        long      marks_byte_size               (PageNum page, long pages);
    static        long      card_mark_words               (long object_words);
    static        long      card_mark_summary_bytes       (long object_words);
    static        long      total_card_mark_overhead_words(long object_words);

    static        void      clear_mark_bitmap             (HeapWord* page_base, HeapWord* bitmap_base, long bitmap_byte_size);
    static        void      clear_mark_throughs           (HeapWord* page_base, long pages) PRODUCT_RETURN;
    static        void      clear_mark_ids                (HeapWord* page_base, long pages) PRODUCT_RETURN;

    static        void      clear_all_metadata            (PageNum page, long pages);

    static        void      clear_live_marks_bitmap       (PageNum page, long pages);
    static        void      clear_verify_marks_bitmap     (PageNum page, long pages) PRODUCT_RETURN;

    static        long      count_live_marks              (PageNum page, long pages);

    // Object Liveness Marking:
static inline bool atomic_mark_live_if_dead(oop obj);
static inline bool atomic_mark_final_live_if_dead(oop obj);

static inline bool is_any_marked_strong_live(oop obj);
static inline bool is_any_marked_final_live(oop obj);

    static inline bool      is_new_marked_strong_live     (objectRef ref);
    static inline bool      is_old_marked_strong_live     (objectRef ref);
static inline bool is_perm_marked_strong_live(oop obj);

    static inline bool      is_new_marked_final_live      (objectRef ref);
    static inline bool      is_old_marked_final_live      (objectRef ref);
static inline bool is_perm_marked_final_live(oop obj);

    static        void      verify_new_marked_live        (objectRef ref);
    static        void      verify_old_marked_live        (objectRef ref);
static void verify_perm_marked_live(oop obj);

    // Card Marking:
    static inline bool      atomic_attempt_card_mark      (objectRef* addr);
    static inline bool      is_card_marked                (objectRef* addr);

    // Mark Throughs:
    static inline void      set_marked_through            (oop obj)                          PRODUCT_RETURN;
    static inline bool      is_marked_through             (oop obj)                          PRODUCT_RETURN0;

    static inline void      set_marked_through_final      (oop obj)                          PRODUCT_RETURN;

    // Mark IDs:
    static inline void      set_markid                    (oop obj, long markid)             PRODUCT_RETURN;
    static inline void      set_nmt_update_markid         (objectRef* ref_addr, long markid) PRODUCT_RETURN;
    static inline long      get_markid                    (HeapWord* addr)                   PRODUCT_RETURN0;

    static inline void      set_markid_final              (oop obj, long markid)             PRODUCT_RETURN;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Core bitmap set/test operations:
//
inline bool GPGC_Marks::atomic_set_bit_in_bitmap(HeapWord* bitmap, long bit_index)
{
  long      word_index = GPGC_Marks::word_index(bit_index);
  intptr_t* word_addr  = (intptr_t*)(bitmap + word_index);
  intptr_t  nth        = bit_mask(bit_index);
  intptr_t  word;
  intptr_t  new_word;

  do {
    word = *word_addr;

    if ( word & nth ) return false; // bit is already set

    new_word = word | nth;
  } while ( word != Atomic::cmpxchg_ptr(new_word, word_addr, word));

  return true;
}


inline bool GPGC_Marks::is_bit_set_in_bitmap(HeapWord* bitmap, long bit_index)
{
  long      word_index = GPGC_Marks::word_index(bit_index);
  intptr_t* word_addr  = (intptr_t*)(bitmap + word_index);
  intptr_t  nth        = bit_mask(bit_index);
  intptr_t  word       = *word_addr;
  intptr_t  bit        = word & nth;

  return (bit != 0);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Bitmap base and bit index calculators for various metadata:
//

// Marking bitmap index.  Used for strong marking, final marking, and verify marking bitmaps.
inline long GPGC_Marks::bit_index_for_mark(intptr_t addr)
{
  long index;

  if ( GPGC_Layout::small_space_addr((void*)addr) ){
    index = addr >> (LogBytesPerWord + GPGC_SmallSpaceWordMarksIndexShift);
  } else {
    index = addr >> (LogBytesPerWord + GPGC_NonSmallSpaceWordMarksIndexShift);
  }

  return index;
}


// Marked through bitmap index. 
inline long GPGC_Marks::bit_index_for_marked_through(intptr_t addr)
{
  long index = addr >> (LogBytesPerWord + GPGC_WordMarkThroughsIndexShift);
  
  return index;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Strong and final live mark operations:
//

//  This method has an assembly version: MacroAssembler::GPGC_atomic_mark_live_if_dead().
//  Any substantive change to this algorithm also needs to be made in the assembly code.
inline bool GPGC_Marks::atomic_mark_live_if_dead(oop obj)
{
  long      bit_index   = bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::strong_mark_bitmap_base();

  return atomic_set_bit_in_bitmap(bitmap_base, bit_index);
}


inline bool GPGC_Marks::is_any_marked_strong_live(oop obj)
{
  long      bit_index   = bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::strong_mark_bitmap_base();

  return is_bit_set_in_bitmap(bitmap_base, bit_index);
}


inline bool GPGC_Marks::atomic_mark_final_live_if_dead(oop obj)
{
  long      bit_index   = bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::final_mark_bitmap_base();

  return atomic_set_bit_in_bitmap(bitmap_base, bit_index);
}

inline bool GPGC_Marks::is_any_marked_final_live(oop obj)
{
  long      bit_index   = bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::final_mark_bitmap_base();

  return is_bit_set_in_bitmap(bitmap_base, bit_index);
}


inline bool GPGC_Marks::is_new_marked_strong_live(objectRef ref)
{
  assert0(ref.is_new());
#ifdef ASSERT
  verify_new_marked_live(ref);
#endif // ASSERT
  return is_any_marked_strong_live(ref.as_oop());
}

inline bool GPGC_Marks::is_new_marked_final_live(objectRef ref)
{
  assert0(ref.is_new());
#ifdef ASSERT
  verify_new_marked_live(ref);
#endif // ASSERT
  return is_any_marked_final_live(ref.as_oop());
}


inline bool GPGC_Marks::is_old_marked_strong_live(objectRef ref)
{
  assert0(ref.is_old());
#ifdef ASSERT
  verify_old_marked_live(ref);
#endif // ASSERT
  return is_any_marked_strong_live(ref.as_oop());
}

inline bool GPGC_Marks::is_old_marked_final_live(objectRef ref)
{
  assert0(ref.is_old());
#ifdef ASSERT
  verify_old_marked_live(ref);
#endif // ASSERT
  return is_any_marked_final_live(ref.as_oop());
}


inline bool GPGC_Marks::is_perm_marked_strong_live(oop obj)
{
#ifdef ASSERT
verify_perm_marked_live(obj);
#endif // ASSERT
return is_any_marked_strong_live(obj);
}

inline bool GPGC_Marks::is_perm_marked_final_live(oop obj)
{
#ifdef ASSERT
verify_perm_marked_live(obj);
#endif // ASSERT
return is_any_marked_final_live(obj);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Card mark operations:
//

//  This method has an assembly version in the matrix trap handler.
//  Any substantive change to this algorithm also needs to be made in the assembly code.
inline bool GPGC_Marks::atomic_attempt_card_mark(objectRef* addr)
{
  HeapWord* cardmark_bitmap    = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bitmap);
  long      cardmark_bit_index = intptr_t(addr) >> LogBytesPerWord;
  bool      result             = atomic_set_bit_in_bitmap(cardmark_bitmap, cardmark_bit_index);

  if ( result ) {
    uint8_t* bytemap_base  = (uint8_t*) GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bytemap);
    long     bytemap_index = cardmark_bit_index >> LogWordsPerGPGCCardMarkSummaryByte;
    
    bytemap_base[bytemap_index] = 1;
  }

  return result;
}


inline bool GPGC_Marks::is_card_marked(objectRef* addr)
{
  HeapWord* cardmark_bitmap    = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bitmap);
  long      cardmark_bit_index = intptr_t(addr) >> LogBytesPerWord;
  bool      bitmap_result      = is_bit_set_in_bitmap(cardmark_bitmap, cardmark_bit_index);

  if ( bitmap_result ) {
    uint8_t* bytemap_base  = (uint8_t*) GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bytemap);
    long     bytemap_index = cardmark_bit_index >> LogWordsPerGPGCCardMarkSummaryByte;

    guarantee(bytemap_base[bytemap_index] == 1, "Card mark bit set, but card mark summary byte is not");
  }

  return bitmap_result;
}


#ifndef PRODUCT

/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Marked through bitmap operations:
//

//
//  MarkedThroughBits setting/testing:
// 
inline void GPGC_Marks::set_marked_through(oop obj)
{
  if ( ! GPGCAuditTrail ) { return; }

  long      bit_index   = bit_index_for_marked_through(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::mark_throughs_bitmap_base();

  atomic_set_bit_in_bitmap(bitmap_base, bit_index);
}

inline void GPGC_Marks::set_marked_through_final(oop obj)
{
  // We don't have a separate final-mark-throughs bitmap.  Instead, we just set the
  // bit in the normal marked through bitmap at bit_index + 1.
  
  // TODO: I'm not sure this avoids bit clashes, unless we only set marked throughs
  // on object heads.  Verify that's the case.

  set_marked_through(oop(((HeapWord*)obj)+1));
}

inline bool GPGC_Marks::is_marked_through(oop obj)
{
  assert0( GPGCAuditTrail );

  long      bit_index   = bit_index_for_marked_through(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::mark_throughs_bitmap_base();

  return is_bit_set_in_bitmap(bitmap_base, bit_index);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Mark ID bytemap operations:
//

// mark ID bytemap
inline long GPGC_Marks::byte_index_for_markid(intptr_t addr)
{
  long index = addr >> (LogBytesPerWord + GPGC_MarkIDsWordIndexShift);

  return index;
}

//
//  Mark ID setting/testing:
//
inline void GPGC_Marks::set_markid(oop obj, long markid)
{
  if ( ! GPGCAuditTrail ) { return; }

  long     byte_index  = byte_index_for_markid(intptr_t(obj));
  uint8_t* markid_base = GPGC_Layout::mark_ids_bytemap_base();

  markid_base[byte_index] = markid;
}

inline void GPGC_Marks::set_markid_final(oop obj, long markid)
{
  // We don't have a separate final-markID bytemap.  Instead we just set the
  // byte in the normal markID bytemap at byte_index + 1.

  // TODO: I'm not sure this avoids bit clashes, unless we only set markIDs on
  // object heads.  Verify that's the case.

  set_markid(oop(((HeapWord*)obj)+1), markid);
}

inline long GPGC_Marks::get_markid(HeapWord* addr)
{
  assert0( GPGCAuditTrail );

  long     byte_index  = byte_index_for_markid(intptr_t(addr));
  uint8_t* markid_base = GPGC_Layout::mark_ids_bytemap_base();

  return markid_base[byte_index];
}


inline void GPGC_Marks::set_nmt_update_markid(objectRef* ref_addr, long markid)
{
  if ( ! GPGCAuditTrail ) { return; }

  PageNum page = GPGC_Layout::addr_to_PageNum(ref_addr);

  if ( page>=GPGC_Layout::start_of_heap_range && page<GPGC_Layout::end_of_heap_range ) {
    // No markID for objectRefs stored outside the heap.

    long     byte_index  = byte_index_for_markid(intptr_t(ref_addr));
    uint8_t* markid_base = GPGC_Layout::mark_ids_bytemap_base();

    markid_base[byte_index] = markid;
  }
}

#endif // ! PRODUCT

#endif // GPGC_MARKS_HPP
