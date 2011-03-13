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

#ifndef GPGC_SPARSEMAPPEDSPACE
#define GPGC_SPARSEMAPPEDSPACE

#include "allocation.hpp"
#include "memRegion.hpp"
#include "mutex.hpp"


/*
 * GPGC uses a number of sparse arrays of data where memory is conserved by not mapping in
 * the unused regions of the array.  This is a utility class to manage the sparse mapping.
 */
class GPGC_SparseMappedSpace:public CHeapObj{
  private:
    int            _module;
    MemRegion      _reserved_region;
AzLock*_lock;
    long           _log_page_size;
    bool*          _real_array_pages_mapped;
    volatile bool* _array_pages_mapped;

   bool locked_expand(long first_page_index, long last_page_index);

  public:
    GPGC_SparseMappedSpace(int module, PageNum start_layout, PageNum end_layout, long log_page_size, AzLock* lock);

    // Ensure that the range [start, end] is mapped into the array, mapping new pages
    // if necessary.  Return true if the method returns with the range mapped, false
    // if unmapped pages couldn't be mapped in from the kernel.
    bool expand(void* start, void* end);
};

#endif // GPGC_SPARSEMAPPEDSPACE
