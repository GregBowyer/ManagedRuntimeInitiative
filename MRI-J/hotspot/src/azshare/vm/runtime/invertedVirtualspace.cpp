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


#include "invertedVirtualspace.hpp"
#include "modules.hpp"

#include "os_os.inline.hpp"

bool InvertedVirtualSpace::initialize(ReservedSpace rs,size_t committed_size){
  assert(rs.is_reserved(), "ReservedSpace not initialized");
  assert(_low_boundary == NULL, "VirtualSpace already initialized");
  _low_boundary  = rs.base();
  _high_boundary = low_boundary() + rs.size();
_low=high_boundary();
  _high = low();
  
  // we don't do MPSS large pages
  
  // commit to initial size
  if (committed_size > 0) {
    if (!expand_by(committed_size, false)) {
      return false;
    }
  }
  return true;
}


bool InvertedVirtualSpace::expand_by(size_t bytes,bool pre_touch){
  if( pre_touch ) Unimplemented();
  if (uncommitted_size() < bytes) return false;
  
  char* unaligned_new_low = low() - bytes;
  assert(unaligned_new_low >= low_boundary(), 
"cannot expand by more than lower boundary");
  
  if (!os::commit_memory(low()-bytes, bytes, Modules::IVSpace)) {
    debug_only(warning("os::commit_memory failed"));
    return false;
  }

_low-=bytes;
  return true;
}


void InvertedVirtualSpace::shrink_by(size_t size){
  // we never call this
  ShouldNotReachHere();
}
