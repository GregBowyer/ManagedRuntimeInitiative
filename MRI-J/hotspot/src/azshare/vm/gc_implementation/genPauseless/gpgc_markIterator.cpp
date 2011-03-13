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
#include "gpgc_layout.hpp"
#include "gpgc_markIterator.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_relocation.hpp"
#include "log.hpp"
#include "ostream.hpp"

#include "gpgc_pageInfo.inline.hpp"


/*
 * Iterate through live objects and initialize sideband forwarding hash tables:
 */
class GPGC_InitObjectHashClosure VALUE_OBJ_CLASS_SPEC
{
  private:
GPGC_PageInfo*_info;

  public:
    GPGC_InitObjectHashClosure(GPGC_PageInfo* info) : _info(info) {}

    inline void do_marked_obj(oop obj, long offset_words)
    {
      GPGC_PageRelocation::init_object_hash(_info, obj, offset_words);
    }
};


void GPGC_MarkIterator::init_live_obj_relocation(PageNum page)
{
  GPGC_PageInfo*             info = GPGC_PageInfo::page_info(page);
  GPGC_InitObjectHashClosure iohc(info);

  iterate_live_obj_marks(page, GPGC_Layout::small_space_page(page), &iohc);
}


/*
 * Iterate through live mid space objects and remap to new locations:
 */
class GPGC_RemapObjectClosure VALUE_OBJ_CLASS_SPEC
{
  private:
    PageNum                _old_page;
GPGC_PageInfo*_info;
    GPGC_RemapBuffer*      _remap_buffer;
    bool                   _mark_copy;
    int64_t                _stripe;

oop _last_obj;
    long                   _last_offset_words;

  public:
    GPGC_RemapObjectClosure(PageNum old_page, GPGC_PageInfo* info, GPGC_RemapBuffer* remap_buffer, bool mark_copy, int64_t stripe)
                           : _old_page(old_page), _info(info), _remap_buffer(remap_buffer), _mark_copy(mark_copy), _stripe(stripe), _last_obj(NULL)
    {
      assert0(GPGC_Layout::mid_space_page(_old_page));
    }

    inline void do_marked_obj(oop obj, long offset_words) {
if(_last_obj==NULL){
        // Account for any leading empty source pages. 
        PageNum first_obj_page = GPGC_Layout::addr_to_PageNum(obj);
        if ( first_obj_page > _old_page ) {
          long unmap_count = first_obj_page - _old_page;
          _info->increment_free_on_unmap(unmap_count);

          BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceRemap, false, gclog_or_tty);
          GCLogMessage::log_b(m.enabled(), m.stream(), "GPGCTraceRemap unmap +%ld free pages from block start 0x%lX to first live in block 0x%lX",
                                                       unmap_count, _old_page, first_obj_page);
        }
      } else {
        // Remap the previously discovered object.
        GPGC_ObjectRelocation* relocation    = GPGC_PageRelocation::find_object_generic(_info, _last_offset_words);
        PageNum                next_obj_page = GPGC_Layout::addr_to_PageNum(obj);
        relocation->gc_relocate_mid_object(_old_page, _remap_buffer, _mark_copy, _stripe, next_obj_page);
      }
_last_obj=obj;
      _last_offset_words = offset_words;
    }

    void remap_last_obj() {
      // There should always be a _last_obj: fully empty pages are filtered out during the page population scan.
guarantee(_last_obj!=NULL,"Empty page snuck through initial page population scan");
      GPGC_ObjectRelocation* relocation = GPGC_PageRelocation::find_object_generic(_info, _last_offset_words);
      relocation->gc_relocate_mid_object(_old_page, _remap_buffer, _mark_copy, _stripe, NoPage);
    }
};


void GPGC_MarkIterator::remap_live_objs(PageNum page, GPGC_RemapBuffer* remap_buffer, bool mark_copy, int64_t stripe)
{
  assert0(GPGC_Layout::mid_space_page(page));

  GPGC_PageInfo*          info = GPGC_PageInfo::page_info(page);
  GPGC_RemapObjectClosure roc(page, info, remap_buffer, mark_copy, stripe);

  iterate_live_obj_marks(page, false, &roc);

  roc.remap_last_obj();
}


//
// Iterate through the strong live marking bits for a OneSpace page, and call out
// to the mark closure for each object found marked_live.
//
template <class T>
void GPGC_MarkIterator::iterate_live_obj_marks(PageNum page, bool small_space, T* mark_closure)
{
  // Find all bits set in the mark bitmap, and call the relocation init function on each one.
  assert(GPGC_Layout::one_space_page(page), "relocation records are only for one-space-pages")

  // This is a single page with multiple objects in it.  These have a static size.
  HeapWord* page_base  = GPGC_Layout::PageNum_to_addr(page);
  HeapWord* mark_base  = GPGC_Layout::strong_mark_bitmap_base();
  long      base_bit   = GPGC_Marks::bit_index_for_mark(intptr_t(page_base));
  long      base_word  = GPGC_Marks::word_index(base_bit);
  
  assert0((base_word<<LogBitsPerWord) == base_bit);

  mark_base += base_word;

assert((uintptr_t(mark_base)&(BytesPerCacheLine-1))==0,"mark_base is supposed to be cache line aligned");

  long marks_word_size;
  long bit_scale;

  if ( small_space ) {
    marks_word_size = WordsPerGPGCPage >> (LogBitsPerWord + GPGC_SmallSpaceWordMarksIndexShift);
    bit_scale       = GPGC_SmallSpaceWordMarksIndexShift;
  } else {
    marks_word_size = WordsPerMidSpaceBlock >> (LogBitsPerWord + GPGC_NonSmallSpaceWordMarksIndexShift);
    bit_scale       = GPGC_NonSmallSpaceWordMarksIndexShift;
  }

  HeapWord* objs_base = page_base;
  HeapWord* marks_end = mark_base + marks_word_size;

  for ( long word_index=0; word_index<marks_word_size ; word_index++ ) {
    uintptr_t word = ((uintptr_t*)mark_base)[word_index];

    if ( word == 0 ) continue;

    // BitOps::first_bit() finds the most significant bit.  The order we use
    // has lower addressed objects appearing in the LSB, so we're not going to
    // find live objects in address order.

    HeapWord* word_base = objs_base + (word_index<<(LogBitsPerWord+bit_scale));

    do {
      long      first        = BitOps::first_bit(word);

      HeapWord* obj          = word_base + (first<<bit_scale);
      long      offset_words = obj - page_base;

      assert(((intptr_t*)obj)[0] != 0, "Found live object with NULL mark word");

      mark_closure->do_marked_obj((oop)obj, offset_words);

      word ^= 1UL << first;
    } while (word);

  }
}
