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
#ifndef HEAPREF_PD_HPP
#define HEAPREF_PD_HPP

#include "objectRef_pd.hpp"

// A heapRef is a specific type of objectRef. It can only be a new_space_id
// or old_space_id space type. 

// heapRefs have the following layout:
//
// Bit 63: nmt
// Bit 62: space id (must be either 10 or 11)
// Bit 61: space id (must be either 10 or 11)
// Bit 60: reserved
// Bit 45: 
// Bit 43: 
// Bit 42: NMT bit
// Bit 41: virtual address
// ...
// Bit  0: virtual address 

class heapRef : public objectRef {
 public:
  enum {
    va_bits             = 42,
    va_shift            = 0,
    va_mask             = (address_word)right_n_bits(va_bits),
    va_mask_in_place    = (address_word)va_mask << va_shift
  };

  //
  // Static methods
  //
  
  // Note that this is not an accessor method! It derives the value.
  inline static uint64_t discover_space_id(const void* addr);

  //
  // Constructors
  //

  heapRef() {
    _objref = 0; // 0 is always mapped to NULL
  }

  heapRef(const objectRef& ref) {
    assert (ref.is_null() || ref.is_heap(), "Must be a heapRef");
    _objref = ref._objref;
  }

  heapRef(const oop o) {
set_value(o);
  }

  // Danger Will Robinson! This constructor does not do any error or assert checks.
  heapRef(const uint64_t raw_value) {
    _objref = raw_value;
  }

  //
  // Overloaded operators (BLEH!)
  //

  // I would have preferred to disallow all assignments of
  // heapRef = objectRef, but the assumption that this could
  // be done is already heavily embedded in the code. Second
  // best choice is to hope we catch bad assignments with the
  // asserts.
  void operator = (const objectRef& right) {
assert(right.is_null()||right.is_heap(),"Must be heapRef");
    _objref = right._objref;
  }

  void operator = (const objectRef& right) volatile {
assert(right.is_null()||right.is_heap(),"Must be heapRef");
    _objref = right._objref;
  }

  //
  // Accessor methods (in const and volatile const forms)
  //

  inline void set_value_base(oop o, uint64_t sid, uint64_t kid, uint64_t nmt);
  inline void set_value(oop o);
  inline void set_value(oop o, uint64_t sid, uint64_t kid);

  // Note that this simply masks out the va bits. It does no checks
  // or asserts. Derived pointers use this to extract misaligned values.
  uint64_t va() {
    return (uint64_t)(_objref & va_mask);
  }

#ifdef ASSERT
  friend heapRef poison_heapRef(heapRef r);
#else
#define poison_heapRef(r) (r)
#endif
};

#ifdef ASSERT
inline heapRef poison_heapRef(heapRef r) {
  if ( RefPoisoning && r.not_null() ) {
    return heapRef(r.raw_value() ^ -1);
  }
  return r;
}
#endif

#endif // HEAPREF_PD_HPP
