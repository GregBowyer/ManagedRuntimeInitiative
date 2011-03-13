/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef UNIVERSE_INLINE_HPP
#define UNIVERSE_INLINE_HPP


inline void* Universe::non_oop_word() {
  // Neither the high bits nor the low bits of this value is allowed
  // to look like (respectively) the high or low bits of a real oop.
  //
  // High and low are CPU-specific notions, but low always includes
  // the low-order bit.  Since oops are always aligned at least mod 4,
  // setting the low-order bit will ensure that the low half of the
  // word will never look like that of a real oop.
  //
  // Using the OS-supplied non-memory-address word (usually 0 or -1)
  // will take care of the high bits, however many there are.
  assert0( os::non_memory_address_word() == 0 );
  // But make sure this is a compile-time constant - it is extremely hot in debug builds
  return (void*)/*os::non_memory_address_word() | */NON_OOP_MARK;
}

inline objectRef Universe::non_ref_word() {
  // Note that this value is not a legal objectRef!
return objectRef(NON_OOP_MARK);
}

// Check whether an element of a typeArrayOop with the given type must be
// aligned 0 mod 8.  The typeArrayOop itself must be aligned at least this
// strongly.

inline bool Universe::element_type_should_be_aligned(BasicType type) {
  return type == T_DOUBLE || type == T_LONG;
}

// Check whether an object field (static/non-static) of the given type must be aligned 0 mod 8.

inline bool Universe::field_type_should_be_aligned(BasicType type) {
  return type == T_DOUBLE || type == T_LONG;
}


#endif // UNIVERSE_INLINE_HPP

