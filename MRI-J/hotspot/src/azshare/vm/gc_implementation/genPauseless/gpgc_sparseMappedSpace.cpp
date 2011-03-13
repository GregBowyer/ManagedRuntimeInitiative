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


#include "gpgc_sparseMappedSpace.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"

#include "os_os.inline.hpp"


GPGC_SparseMappedSpace::GPGC_SparseMappedSpace(int module, PageNum start_layout, PageNum end_layout, long log_page_size, AzLock* lock)
{
  _module        = module;
  _lock          = lock;
  _log_page_size = log_page_size;

  assert(_log_page_size==LogBytesPerSmallPage || _log_page_size==LogBytesPerLargePage, "invalid page size");

  intptr_t start_addr = (intptr_t) GPGC_Layout::PageNum_to_addr(start_layout);
  intptr_t end_addr   = (intptr_t) GPGC_Layout::PageNum_to_addr(end_layout);

  assert((start_addr>>_log_page_size)<<_log_page_size==start_addr, "start address not page aligned");
  assert((end_addr  >>_log_page_size)<<_log_page_size==end_addr,   "end address not page aligned");

  long start_page = start_addr >> _log_page_size;
  long byte_size  = end_addr - start_addr;
  long word_size  = byte_size >> LogBytesPerWord;
  long page_size  = byte_size >> _log_page_size;

  _reserved_region         = MemRegion((HeapWord*)start_addr, word_size);
  _real_array_pages_mapped = NEW_C_HEAP_ARRAY(bool, page_size);
  _array_pages_mapped      = _real_array_pages_mapped - start_page;

  for ( long i=0; i<page_size; i++ ) {
_real_array_pages_mapped[i]=false;
  }
}


// Ensure that the range [start, end] is mapped into the array, mapping new pages
// if necessary.  Return true if the method returns with the range mapped, false
// if unmapped pages couldn't be mapped in from the kernel.
bool GPGC_SparseMappedSpace::expand(void* start, void* end)
{
  assert0(_reserved_region.contains(start));
  assert0(_reserved_region.contains(end));

  long first_page_index = intptr_t(start) >> _log_page_size;
  long last_page_index  = intptr_t(end)   >> _log_page_size;

  for ( long cursor=first_page_index; cursor<=last_page_index; cursor++ ) {
    if ( ! _array_pages_mapped[cursor] ) {
      bool success = locked_expand(cursor, last_page_index);
      return success;
    }
  }

  return true;
}


bool GPGC_SparseMappedSpace::locked_expand(long first_page_index, long last_page_index)
{
  // TODO: Move memory allocation to be inside the java heap account, and through GPGC_PageBudget
  MutexLocker ml(*_lock);

  long page_size_bytes = 1 << _log_page_size;

  for ( long cursor=first_page_index; cursor<=last_page_index; cursor++ ) {
    if ( ! _array_pages_mapped[cursor] ) {
      char* addr = (char*)(cursor << _log_page_size);

      if ( ! os::commit_memory(addr, page_size_bytes, _module) ) {
        return false;
      }

      _array_pages_mapped[cursor] = true;
    }
  }

  return true;
}
