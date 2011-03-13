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
#ifndef STACKREF_PD_INLINE_HPP
#define STACKREF_PD_INLINE_HPP


inline uint64_t stackRef::discover_space_id(const void* addr) {
  if ((const void*)Universe::non_oop_word() == addr) return 0;
  assert0(JavaThread::sba_is_in_current((address)addr));
  return (uint64_t)objectRef::stack_space_id;
}

inline uint64_t stackRef::discover_frame_id(oop o) {
  assert0(JavaThread::sba_is_in_current((address)o));
  return preheader(o)->fid();
}

inline void stackRef::set_value_base(const oop o, uint64_t kid, uint64_t nmt) {
  uint64_t val = ((uint64_t)o) & va_mask;
  val |= (stack_space_id << space_shift);
  if( KIDInRef )
    val |= (kid << klass_id_shift);

_objref=val;
}

// Note that we want to use discover_space_id(), instead of just passing stack_space_id.
// This allows for asserts and error checking code to run.
inline void stackRef::set_value(oop o){
  if (o == NULL) {
    _objref = 0; // If the value is null, do NOT set a space_id, klass_id, and nmt!
  } else {
    assert0( discover_space_id(o) == stack_space_id );
    set_value_base(o, discover_klass_id(o), discover_nmt(stack_space_id, o));
  }
}

inline stackRef::stackRef( const oop o, uint64_t kid, uint64_t nmt, uint64_t fid) {
  set_value_base( o, kid, nmt );
}

inline address stackRef::as_address(const JavaThread*const jt) const {
  // No ABase on x86
  return (address)(_objref & va_mask);
}


inline void stackRef::set_va(HeapWord *va) {
  _objref = (_objref & ~va_mask) | ((uint64_t)va & va_mask);
}

#endif // STACKREF_PD_INLINE_HPP
