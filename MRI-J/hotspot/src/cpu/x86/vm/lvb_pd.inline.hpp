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

#ifndef LVB_PD_INLINE_HPP
#define LVB_PD_INLINE_HPP


#include "gpgc_lvb.hpp"


inline objectRef LVB::lvb_loaded(objectRef ref, const objectRef* addr) {
  assert0( (!VerifyJVMLockAtLVB) || verify_jvm_lock() );

  if ( ref.is_null() ) return nullRef;
  objectRef new_ref = ref;

#ifdef ASSERT
  if ( RefPoisoning ) {
    // If RefPoisoning is on, then we have to xor -1 a ref that is
    // not from the stack after we load it, and before it gets LVB'ed.
    if ( ref.not_null() && objectRef::needs_lvb(addr) ) { // needs an LVB ==> not stack allocated
      assert0(ref.is_poisoned());
      new_ref = ref._objref ^ -1;  // So remove poison
    }
  }
#endif // ASSERT

  if ( UseLVBs ) {
assert(UseGenPauselessGC,"We only have LVB support for GPGC");
    new_ref = GPGC_LVB::lvb_loaded_ref(new_ref, (objectRef*)addr);
  }

  return new_ref;
}


inline objectRef LVB::lvb(objectRef* addr) {
  assert0( (!VerifyJVMLockAtLVB) || verify_jvm_lock() );

  objectRef old_ref = *addr;
  if ( old_ref.is_null() ) return nullRef;
  objectRef new_ref = old_ref;

#ifdef ASSERT
  if ( RefPoisoning ) {
    // If RefPoisoning is on, then we have to xor -1 a ref that is
    // not from the stack after we load it, and before it gets LVB'ed.
    if ( old_ref.not_null() && objectRef::needs_lvb(addr) ) { // needs an LVB ==> not stack allocated
      assert0(old_ref.is_poisoned());
      new_ref = old_ref._objref ^ -1; // So remove poison
    }
  }
#endif // ASSERT

  if ( UseLVBs ) {
assert(UseGenPauselessGC,"We only have LVB support for GPGC");
    new_ref = GPGC_LVB::lvb_loaded_ref(new_ref, addr);
  }

  return new_ref;
}

#endif // LVB_PD_INLINE_HPP
