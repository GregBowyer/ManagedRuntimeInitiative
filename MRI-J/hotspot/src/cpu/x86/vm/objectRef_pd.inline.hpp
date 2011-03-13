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
#ifndef OBJECTREF_PD_INLINE_HPP
#define OBJECTREF_PD_INLINE_HPP


#include "nmt.hpp"
#include "oopTable.hpp"
#include "universe.inline.hpp"

inline void ref_store_without_check(objectRef*p,objectRef r){
  assert0(r.is_null() || NMT::has_desired_nmt(r) );
  *p = POISON_OBJECTREF(r, p);
}
inline void ref_store_without_check(objectRef volatile * p, objectRef r) {
  assert0(r.is_null() || NMT::has_desired_nmt(r) );
  *p = POISON_OBJECTREF(r, (objectRef*)p);
}

inline uint64_t objectRef::discover_klass_id(oop o) {
  if ((oop)Universe::non_oop_word() == o) {
    return unresolved_kid;
  }
  uint64_t kid = o->mark()->kid();
  assert(!Universe::is_fully_initialized() || KlassTable::is_valid_klassId(kid), "assigning invalid klassId");
  assert(!Universe::is_fully_initialized() || ((uint64_t)Klass::cast(o->klass())->klassId() == kid), "klassId doesn't match klass");
  return kid;
}

inline uint64_t objectRef::discover_nmt(uint64_t space_id, oop o) {
  if ((oop)Universe::non_oop_word() == o) return 0;
  if ( !UseGenPauselessGC ) return 0; // ignored for e.g. old parallel collectors
    // should be a range check
  assert0(space_id == stack_space_id || space_id == Universe::heap()->space_id_for_address(o));
  return (space_id == new_space_id) 
         ? GPGC_NMT::desired_new_nmt_flag()
         : GPGC_NMT::desired_old_nmt_flag();
Unimplemented();//unknown hardware?
}

// --- klass
inline klassRef objectRef::klass() const {
  return KlassTable::getKlassByKlassId(klass_id());
}

#endif // OBJECTREF_PD_INLINE_HPP

