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


#include "allocation.inline.hpp"
#include "gpgc_pageAudit.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "java.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"

#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

MemRegion      GPGC_PageInfo::_reserved_region;
volatile bool* GPGC_PageInfo::_real_array_pages_mapped = NULL;
volatile bool* GPGC_PageInfo::_array_pages_mapped      = NULL;
GPGC_PageInfo* GPGC_PageInfo::_page_info               = NULL;


void GPGC_PageInfo::initialize_info_array()
{
  // The reservation of memory actually happens in GPGC_Heap::initialize(), as part of the overall
  // reservation of GPGC's address space.

  char*  reservation_start       = (char*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_page_info );
  long   reservation_large_pages = GPGC_Layout::end_of_page_info - GPGC_Layout::start_of_page_info;
  long   reservation_data_pages  = reservation_large_pages << LogDataPagesPerGPGCPage;
  size_t reservation_words       = reservation_data_pages  << LogWordsPerGPGCDataPage;

  _reserved_region = MemRegion((HeapWord*)reservation_start, reservation_words);

  // Create array to track which pages in the GPGC_PageInfo virtual memory range are mapped in.
  _real_array_pages_mapped = NEW_C_HEAP_ARRAY(bool, reservation_data_pages);
  for ( long i=0; i<reservation_data_pages; i++ ) {
_real_array_pages_mapped[i]=false;
  }
  _array_pages_mapped = _real_array_pages_mapped - (GPGC_Layout::start_of_page_info << LogDataPagesPerGPGCPage);
  _page_info          = (GPGC_PageInfo*) _reserved_region.start();

  // Make sure all GPGC_PageInfo's fit in the reserved address space.
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::start_of_small_space), "Bad GPGC_PageInfo memory range: small space start");
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::end_of_small_space-1), "Bad GPGC_PageInfo memory range: small space end");
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::start_of_mid_space),   "Bad GPGC_PageInfo memory range: mid space start");
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::end_of_mid_space-1),   "Bad GPGC_PageInfo memory range: mid space end");
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::start_of_large_space), "Bad GPGC_PageInfo memory range: large space start");
  guarantee( _reserved_region.contains(_page_info+GPGC_Layout::end_of_large_space-1), "Bad GPGC_PageInfo memory range: large space end");

  // Ensure that GPGC_PageInfo's don't straddle pages.
  guarantee( (long(BytesPerGPGCDataPage/page_info_size())*page_info_size()) == BytesPerGPGCDataPage, "GPGC_PageInfo straddles data pages");
}


bool GPGC_PageInfo::expand_pages_in_use(PageNum page)
{
  assert0(page!=0);
  assert0(page!=NoPage);

  GPGC_PageInfo* info            = &(_page_info[page]);
  PageNum        info_data_page  = GPGC_Layout::addr_to_DataPageNum(info);

  assert0(_reserved_region.contains(info));

  void* info_tail_byte = (void*)( intptr_t(info) + page_info_size() - 1 );
  assert( GPGC_Layout::addr_to_DataPageNum(info_tail_byte) == info_data_page, "GPGC_PageInfo straddles data page boundry");
  
  if ( ! _array_pages_mapped[info_data_page] ) {
    // Locking protects against multiple threads racing to expand the same page of PageInfos.
MutexLocker ml(GPGC_PageInfo_lock);

    if ( ! _array_pages_mapped[info_data_page] ) {
      if ( GPGCPageAuditTrail ) {
        // Expand the GPGC_PageAudit to cover the pages covered by the new Data page's GPGC_PageInfo elements.
        PageNum start_page = ((GPGC_PageInfo*)GPGC_Layout::DataPageNum_to_addr(info_data_page))   - (&_page_info[0]);
        PageNum end_page   = ((GPGC_PageInfo*)GPGC_Layout::DataPageNum_to_addr(info_data_page+1)) - (&_page_info[0]);
        if ( ! GPGC_PageAudit::expand_pages_in_use(start_page, end_page) ) {
          return false;
        }
      }

      // Allocate another page of memory for GPGC_PageInfo structures.
      if ( ! os::commit_memory((char*)GPGC_Layout::DataPageNum_to_addr(info_data_page), BytesPerGPGCDataPage, Modules::GPGC_PageInfo) ) {
        if ( GPGCTracePageSpace) gclog_or_tty->print_cr("Unable to expand page_info array");
        return false;
        // TODO: maw: make sure GC won't fail to make forward progress if the system is too low on
        // memory to allocate another page for PageInfos.
      }

      _array_pages_mapped[info_data_page] = true;
    }
  }

  return true;
}


void GPGC_PageInfo::initialize()
{
  set_gen_and_state(InvalidGen, InvalidState);
  _size        = 0;
  _raw_stats   = 0;
  _ll_next     = NoPage;
  _object_data = 0;
_time=0;
  _flags       = 0;
_top=NULL;
  _unused      = 0;
}


bool GPGC_PageInfo::set_first_unshatter()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;

    assert0( (old_flags&UnshatterFreeStatsFlag) != 0 );

    if ( (old_flags & FirstUnshatterFlag) != 0 ) {
      // Someone else set the FirstUnshatterFlag
      return false;
    }

new_flags=old_flags|FirstUnshatterFlag;
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );

  // Successfully set the FirstUnshatterFlag
  return true;
}


void GPGC_PageInfo::atomic_add_pinned()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;

    assert ( (old_flags>>CountShift)<=uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count overflow" );
    assert ( (old_flags&PinnedFlag)!=0 || (old_flags>>CountShift)==0, "Setting first page pin on non-zero count" );

    assert0( (old_flags&UnmapFreeStatsFlag)     == 0 );
    assert0( (old_flags&UnshatterFreeStatsFlag) == 0 );

    new_flags = (old_flags | PinnedFlag) + (1UL << CountShift);
    assert ( (new_flags>>CountShift)<=uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count overflow" );
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}


void GPGC_PageInfo::atomic_subtract_pinned()
{
  uint64_t old_flags;
  uint64_t new_flags;

  do {
    old_flags = _flags;

    assert0( (old_flags&PinnedFlag)  != 0 );
    assert ( (old_flags>>CountShift) != 0, "GPGC_PageInfo flags count underflow" );
    assert ( (old_flags>>CountShift) <= uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count overflow" );

    new_flags = old_flags - (1UL << CountShift);

    if ( (new_flags>>CountShift) == 0 ) {
      new_flags = new_flags & ~PinnedFlag;
    }
    assert ( (new_flags>>CountShift)<=uint64_t(WordsPerGPGCPage), "GPGC_PageInfo flags count overflow" );
  } while ( jlong(old_flags) != Atomic::cmpxchg(jlong(new_flags), (jlong*)&_flags, jlong(old_flags)) );
}
