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
#ifndef VREG_HPP
#define VREG_HPP

#include "allocation.hpp"
#include "register_pd.hpp"
class outputStream;
class VOopReg;

//------------------------------VOopReg---------------------------------------
// Same thing as a VReg, but excludes registers that will not hold oops such
// as the FP registers.  This means that a bit-vector of VOopRegs will have
// many fewer forced-zero bits over a bit-vector of VRegs, and will more
// often fit into a smaller OopMap structure.
// Example: an oop is in R0 and SP+24.  
//   For both VReg and VOopReg, R0 encodes as '0' and the bitvector is '1<<0'.
//   For VReg   , SP+24 encodes as (24>>3)+REG_COUNT     (33 on X86) so the bit is 1<<(3+33).
//   For VOopReg, SP+24 encodes as (24>>3)+REG_OOP_COUNT (16 on X86) so the bit is 1<<(3+16).
// The final bitvector is either:
//   0x1000000001  or
//        0x80001.
//   The VOopReg value fits in 32 bits and the VReg version never will.
//
// Conversely, VOopRegs cannot name e.g. FP registers and cannot be used to
// describe calling conventions or debug info.  Due to funny casting games,
// VOopReg needs to be a prefix of VReg.  i.e., on X86, the RSP will never
// hold an oop - but it's in the middle of the register file.  We cannot set
// REG_OOP_COUNT to 15 (instead of 16) and exclude this middle register.
class VOopReg VALUE_OBJ_CLASS_SPEC{
public:
  enum VR {
    Bad = -1			// Not a register
  };
  // Convert register numbers to stack offsets and vice versa.
  // For now, force 8-byte aligned offsets.
  static inline VOopReg::VR stk2reg(int stkoff) { 
    assert0((stkoff&1)==0); 
    return VOopReg::VR((stkoff>>3)+REG_OOP_COUNT); 
  }
  static inline int reg2stk(VOopReg::VR reg) { 
    assert0(reg>=REG_OOP_COUNT); 
    return ((int)reg-REG_OOP_COUNT)<<3; 
  }
  static inline bool is_valid(VOopReg::VR r) { return r != Bad; }
  static inline bool is_reg  (VOopReg::VR r) { return is_valid(r) && r<REG_OOP_COUNT; }
  static inline bool is_stack(VOopReg::VR r) { return is_valid(r) && !is_reg(r); }
};

//------------------------------VReg------------------------------------------
// Shared naming convention for live VM values.
// 0 .. REG_COUNT: Encoding a physical register
//    > REG_COUNT: Encoding a stack location >> 3
//   == -1       : Bad
//    < -1       : In debug info, constant encoding (constants held in different tables)
// VReg values fit in 16bits.
class VReg VALUE_OBJ_CLASS_SPEC{
public:
  enum VR {
    Bad = -1			// Not a register
  };
  // Convert register numbers to stack offsets and vice versa.
  // For now, force 8-byte aligned offsets.
  static inline VReg::VR stk2reg(int stkoff) { assert0((stkoff&7)==0); return VReg::VR((stkoff>>3)+REG_COUNT); }
  static inline int reg2stk(VReg::VR reg) { assert0(reg>=REG_COUNT); return ((int)reg-REG_COUNT)<<3; }

  static inline bool is_valid(VReg::VR r) { return r != Bad; }
  static inline bool is_reg  (VReg::VR r) { return is_valid(r) && r<REG_COUNT; }
  static inline bool is_stack(VReg::VR r) { return is_valid(r) && !is_reg(r); }

  static void print_on(VReg::VR r, outputStream *st );

  // Convert to a VOopReg, but only if this is on the stack or already in a
  // oop-capable register.
  static inline VOopReg::VR as_VOopReg( VReg::VR r ) {
    if( r == Bad ) return VOopReg::Bad;
    if( r>= REG_COUNT ) return VOopReg::stk2reg(reg2stk(r));
    assert0( r<REG_OOP_COUNT ); // broken to convert e.g. XMM13 to a VOopReg
    return (VOopReg::VR)r;
  }

};

#endif // VREG_HPP
