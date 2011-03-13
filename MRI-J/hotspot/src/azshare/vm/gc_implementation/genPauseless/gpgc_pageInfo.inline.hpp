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

#ifndef GPGC_PAGEINFO_INLINE_HPP
#define GPGC_PAGEINFO_INLINE_HPP

#include "atomic.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_pageAudit.hpp"


inline GPGC_PageInfo* GPGC_PageInfo::page_info_unchecked(PageNum page)
{
  GPGC_PageInfo* result = &(_page_info[page]);

  assert0(_reserved_region.contains(result));
  assert0(_array_pages_mapped[GPGC_Layout::addr_to_DataPageNum(result)]);

  return result;
}


inline GPGC_PageInfo* GPGC_PageInfo::page_info(PageNum page)
{
  if ( GPGC_Layout::mid_space_page(page) ) {
    PageNum base_page = GPGC_Layout::page_to_MidSpaceBasePageNum(page);
    assert(base_page == page, "Fetching non base page of mid space block");
  }

  return page_info_unchecked(page);
}


inline bool GPGC_PageInfo::info_exists(PageNum page)
{
  GPGC_PageInfo* info            = &(_page_info[page]);
  PageNum        info_large_page = GPGC_Layout::addr_to_PageNum(info);
  PageNum        info_data_page  = GPGC_Layout::addr_to_DataPageNum(info);

  if ( info_large_page<GPGC_Layout::start_of_page_info || info_large_page>=GPGC_Layout::end_of_page_info ) {
    return false;
  }

  return _array_pages_mapped[info_data_page];
}


//  This method updates the _raw_stats field to reflect a new raw object.  It
//  increments the object count part of the stats, and adds the words size of
//  the object to the word count part of the stats.
inline void GPGC_PageInfo::add_live_object(long word_size)
{
  DEBUG_ONLY( States state = just_state() );
  assert0( state == Allocating || state == Allocated );

  long old_raw_stats;
  long new_raw_stats;
  long increment = (1L << 32) | word_size;

  do {
    old_raw_stats = _raw_stats;
    new_raw_stats = old_raw_stats + increment;
  } while ( old_raw_stats != Atomic::cmpxchg(new_raw_stats, (jlong*)&_raw_stats, old_raw_stats) );
}

inline void GPGC_PageInfo::set_relocate_space(long space)
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;

    new_flags = old_flags & ~(RelocateSpaceMask<<RelocateSpaceShift);
    new_flags = new_flags | (uint64_t(space)<<RelocateSpaceShift);
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );

  assert0(relocate_space()==uint64_t(space));
}

inline void GPGC_PageInfo::reset_unmap_free_stats()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert0( (old_flags&PinnedFlag)         == 0 );
    assert0( (old_flags&UnmapFreeStatsFlag) == 0 );
    assert0( (old_flags>>CountShift)==0 || (old_flags&UnshatterFreeStatsFlag)!=0 );
    new_flags = old_flags & AllButCountMask;
    new_flags = new_flags & ~AllFreeStatFlags;
new_flags=new_flags|UnmapFreeStatsFlag;
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}

inline void GPGC_PageInfo::reset_unshatter_free_stats()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert0( (old_flags&PinnedFlag) == 0 );
    new_flags = old_flags & AllButCountMask;
    new_flags = new_flags & ~AllFreeStatFlags;
new_flags=new_flags|UnshatterFreeStatsFlag;
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}

inline void GPGC_PageInfo::atomic_set_flag(Flags flag, Flags assert_not_flag)
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert0( (old_flags&assert_not_flag) == 0 );
new_flags=old_flags|flag;
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}

inline void GPGC_PageInfo::atomic_clear_flag(Flags flag)
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert0( (old_flags&flag) != 0 );
new_flags=old_flags&~flag;
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}

inline void GPGC_PageInfo::atomic_increment_flags_count(uint64_t delta)
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert(   delta                         <= uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count delta too large" );
    assert(  (old_flags>>CountShift)        <= uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count overflow" );
    assert( ((old_flags>>CountShift)+delta) <= uint64_t(WordsPerGPGCPage), "Increment delta causing flags count overflow" );
    new_flags = old_flags + (delta << CountShift);
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}

inline uint64_t GPGC_PageInfo::atomic_decrement_flags_count()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;
    assert( (old_flags>>CountShift) != 0,  "GPGC_PageInfo flags count underflow" );
    new_flags = old_flags - (1UL << CountShift);
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );

  return (new_flags >> CountShift);
}

inline GPGC_PageInfo* GPGC_PageInfo::page_info(oopDesc *obj)
{
  PageNum page = GPGC_Layout::addr_to_PageNum(obj);

  if ( GPGC_Layout::mid_space_page(page) ) {
    page = GPGC_Layout::page_to_MidSpaceBasePageNum(page);
  }

  return page_info(page);
}
inline void GPGC_PageInfo::clear_block_state(PageNum page, long pages, Gens gen) {
  set_gen_and_state(GPGC_PageInfo::InvalidGen, GPGC_PageInfo::Unmapped);
  set_flags_non_atomic(0);
  bool new_gen_page = (gen == GPGC_PageInfo::NewGen) ? true : false;
  GPGC_ReadTrapArray::clear_trap_on_block(page, pages, new_gen_page, true);
  GPGC_PageAudit::audit_entry(page, PageAuditWhy(0x600|gen), GPGC_PageAudit::FailedAlloc);
}

inline void GPGC_PageInfo::restore_block_state(PageNum page) {
  assert(just_state() == GPGC_PageInfo::Relocated, "should only restore state on failed relocations")
    assert(just_gen() == GPGC_PageInfo::NewGen, "should only restore state on NewGen blocks ")
    set_just_state(GPGC_PageInfo::Allocated);
  // reset the trap in the read barrier
  GPGC_ReadTrapArray::set_trap_state(page, block_size(), true, 
      GPGC_PageInfo::Allocated, true, GPGC_NMT::upcoming_new_nmt_flag());

}
#endif // GPGC_PAGEINFO_INLINE_HPP
