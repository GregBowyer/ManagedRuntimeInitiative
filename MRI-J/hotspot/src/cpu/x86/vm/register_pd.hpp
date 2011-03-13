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
#ifndef REGISTER_PD_HPP
#define REGISTER_PD_HPP


#include "allocation.hpp"

enum Register {
  noreg = -1,
  retadr_arg = -2,              // rare funny "register" denoting the return address at [SP+0]

  // raw names
  RAX =  0, RCX =  1, RDX =  2, RBX =  3,
  RSP =  4, RBP =  5, RSI =  6, RDI =  7,
  R08 =  8, R09 =  9, R10 = 10, R11 = 11,
  R12 = 12, R13 = 13, R14 = 14, R15 = 15,

  nof_registers,

  // Interpreter registers:
  RAX_TMP = RAX,   // Temp inside the interpreter
  // RBX    : Callee-save
  RCX_TOS = RCX,   // top-of-stack cache, even for floats
  RDX_LOC = RDX,   // locals in increasing order: JL0 is at [RDX+0], JL1 is at [RDX+8], etc.
  // RSP    : Normal C/C++ stack.  16b aligned.
  // RBP    : Callee-save
  RSI_BCP = RSI,   // bytecode pointer, essentially a derived ptr from MTH
  RDI_JEX = RDI,   // Top of Java Execution Stack.  
  R08_CPC = R08,   // Constant Pool Cache OOP (not ref)
  // R09    : temp
  // R10    : temp
  // R11    : temp
  // R12-R15: Callee-save
};

inline bool is_valid_reg(Register reg) {
  return (0 <= reg) && (reg <= nof_registers);
}

// Is reg one of the first 4 integer regs? Useful when selecting short r8
// instruction forms where encoding 4 to 7 encode ah to dh.
inline bool is_abcdx(Register reg) {
  return (reg >= 0 /* RAX */) && (reg <= 3 /* RBX */);
}

enum FRegister {
  // raw names; numbering is unique with GPRs
  F00 =  0+16, F01 =  1+16, F02 =  2+16, F03 =  3+16,
  F04 =  4+16, F05 =  5+16, F06 =  6+16, F07 =  7+16,
  F08 =  8+16, F09 =  9+16, F10 = 10+16, F11 = 11+16,
  F12 = 12+16, F13 = 13+16, F14 = 14+16, F15 = 15+16,

  nof_float_registers=16
};
static inline Register freg2reg(FRegister freg) { return (Register)(freg-16); }

const char* raw_reg_name(Register reg);
const char* raw_reg_name(FRegister reg);
const char* x86_reg_name(Register reg);
const char* interpreter_reg_name(Register reg);
const char* full_reg_name(Register reg, address pc);

#define reg_encoding(reg) ((int)(reg))

// Need to know the total number of registers of all sorts for SharedInfo.
// Define a class that exports it.
// A big enough number for C2: all the registers plus flags
#define REG_COUNT ((int)(ConcreteRegisterImpl::number_of_registers))
// Count of registers that might hold oops in an OopMap.
// All 16 GPRs (except RSP), and not the XMM registers.
#define REG_OOP_COUNT 16

#define REG_CALLEE_SAVE_COUNT 6

class ConcreteRegisterImpl {
 public:
  enum {
    number_of_registers = nof_registers + nof_float_registers + 1/*flags*/
  };
};

class RegisterSaveArea:public CHeapObj{
 private:
  reg_t _saved_registers[nof_registers + nof_float_registers];
  reg_t _rsp;
  reg_t _rip;
 public:
  RegisterSaveArea();
  
  reg_t      saved_register(Register reg)            const { assert0(is_valid_reg(reg)); return _saved_registers[reg]; }
  reg_t*     saved_register_addr(Register reg)             { return &_saved_registers[reg]; }
  static int saved_register_offset_in_bytes(Register reg)  { return offset_of(RegisterSaveArea,_saved_registers) + reg*sizeof(reg_t); }
  static int rsp_offset_in_bytes()                         { return offset_of(RegisterSaveArea,_rsp); }
  static int rip_offset_in_bytes()                         { return offset_of(RegisterSaveArea,_rip); }
};

inline void assert_different_registers(Register a, Register b) {
  assert(a != b, "registers must be different");
}

inline void assert_different_registers(Register a, Register b, Register c, bool ignore_noreg = false) {
  if ( ignore_noreg ) {
    if (a == noreg) assert_different_registers(b, c);
    if (b == noreg) assert_different_registers(a, c);
    if (c == noreg) assert_different_registers(a, b);
  } else {
    assert(a != b && a != c && b != c, "registers must be different");
  }
}

inline void assert_different_registers(Register a, Register b, Register c, Register d, bool ignore_noreg = false) {
  if ( ignore_noreg ) {
    if (a != noreg) assert(a != b && a != c && a != d, "registers must be different");
    if (b != noreg) assert(b != a && b != c && b != d, "registers must be different");
    if (c != noreg) assert(c != a && c != b && c != d, "registers must be different");
    if (d != noreg) assert(d != a && d != b && d != c, "registers must be different");
  } else {
    assert(a != b && a != c && a != d && b != c && b != d && c != d, "registers must be different");
  }
}

inline void assert_different_registers(Register a, Register b, Register c, Register d, Register e, bool ignore_noreg = false) {
  if ( ignore_noreg ) {
    if (a != noreg) assert(a != b && a != c && a != d && a !=e, "registers must be different");
    if (b != noreg) assert(b != a && b != c && b != d && b !=e, "registers must be different");
    if (c != noreg) assert(c != a && c != b && c != d && c !=e, "registers must be different");
    if (d != noreg) assert(d != a && d != b && d != c && d !=e, "registers must be different");
    if (e != noreg) assert(e != a && e != b && e != c && e !=d, "registers must be different");
  } else {
    assert(a != b && a != c && a != d && a != e && 
                     b != c && b != d && b != e && 
                               c != d && c != e && 
                                         d != e    , "registers must be different");
  }
}

#endif // REGISTER_PD_HPP
