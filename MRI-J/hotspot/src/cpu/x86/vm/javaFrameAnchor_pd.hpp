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
#ifndef JAVAFRAMEANCHOR_PD_HPP
#define JAVAFRAMEANCHOR_PD_HPP


// _last_Java_sp is a ptr to a return address from the last X86 call.
// The X86 stack is always walkable.
public:
  enum {
    // Bits 0 & 1 are used by _jvm_lock already.
    is_walkable_bit = (2<<1),	// frame is walkable
  };

  static inline intptr_t  is_walkable(intptr_t sp) { return true; }
  static inline intptr_t set_walkable(intptr_t sp) { return sp; }
  static inline intptr_t clr_walkable(intptr_t sp) { return sp; }
  inline void record_walkable()                    { }

  frame pd_last_frame(intptr_t* last_Java_sp); // With SP from thread
  frame pd_last_frame();        // With SP from JFA

  intptr_t last_Java_sp() const { return _last_Java_sp; }
  address  last_Java_pc() const { return (address)_last_Java_pc; }
private:
  void pd_verify() const { }
  inline intptr_t pd_last_Java_sp() const { return _last_Java_sp; }

  inline void pd_zap() { }
#endif // JAVAFRAMEANCHOR_PD_HPP
