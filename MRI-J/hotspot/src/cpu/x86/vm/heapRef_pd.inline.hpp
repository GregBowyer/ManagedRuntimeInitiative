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
#ifndef HEAPREF_PD_INLINE_HPP
#define HEAPREF_PD_INLINE_HPP


#include "collectedHeap.hpp"

inline uint64_t heapRef::discover_space_id(const void* addr) {
  if ((const void*)Universe::non_oop_word() == addr) {
    return 0;
  }
assert(Universe::heap()->is_in_reserved(addr),"Address must be in heap");
  return Universe::heap()->space_id_for_address(addr);
}

// NOTE! This method does no error checking. You probably shouldn't be using it.
inline void heapRef::set_value_base(oop o, uint64_t sid, uint64_t klass_id, uint64_t nmt) {
  uint64_t val;
  assert (sid == (uint64_t)objectRef::new_space_id || sid == (uint64_t)objectRef::old_space_id, "Illegal space id");
assert(o!=NULL,"Sanity");

  val = (uint64_t)o;
  val |= (nmt << nmt_shift);
  val |= (sid << space_shift);
  if( KIDInRef ) 
    val |= (klass_id << klass_id_shift);

_objref=val;
}


inline void heapRef::set_value(oop o){
  if (o == NULL) {
    _objref = 0; // If the value is null, do NOT set a space_id, klass_id, and nmt!
  } else {
    uint64_t sid = discover_space_id(o);
    set_value_base(o, sid, discover_klass_id(o), discover_nmt(sid, o));
  }
}


inline void heapRef::set_value(oop o, uint64_t sid, uint64_t klass_id) {
  if (o == NULL) {
    _objref = 0; // If the value is null, do NOT set a space_id, klass_id, and nmt!
  } else {
    assert0(!Universe::is_fully_initialized() || Universe::heap()->is_in_reserved(o));
    assert0(!Universe::is_fully_initialized() || (discover_space_id(o) == sid));
    assert0(sid != (uint64_t)objectRef::stack_space_id);
    assert0(!Universe::is_fully_initialized() || (discover_klass_id(o) == klass_id));
    set_value_base(o, sid, klass_id, discover_nmt(sid, o));
  }
}

#endif // HEAPREF_PD_INLINE_HPP

