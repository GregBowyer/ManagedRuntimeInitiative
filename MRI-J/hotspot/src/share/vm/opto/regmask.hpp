/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef REGMASK_HPP
#define REGMASK_HPP


#include "allocation.hpp"
#include "adGlobals_os_pd.hpp"
#include "port.hpp"
#include "vreg.hpp"

// Some fun naming (textual) substitutions:
//
// RegMask::get_low_elem() ==> RegMask::find_first_elem()
// RegMask::Special        ==> RegMask::Empty
// RegMask::_flags         ==> RegMask::is_AllStack()
// RegMask::operator<<=()  ==> RegMask::Insert()
// RegMask::opreerator>>=()  ==> RegMask::Remove()
// RegMask::Union()        ==> RegMask::OR
// RegMask::Inter()        ==> RegMask::AND
//
// OptoRegister::RegName   ==> OptoReg::Name
//
// OptoReg::stack0()       ==> _last_Mach_Reg  or ZERO in core version
//
// numregs in chaitin      ==> proper degree in chaitin

//-------------Non-zero bit search methods used by RegMask---------------------
// Find lowest 1, or return 32 if empty
int find_lowest_bit( uint32 mask );
// Find highest 1, or return 32 if empty
int find_hihghest_bit( uint32 mask );

//------------------------------OptoReg----------------------------------------
// We eventually need Registers for the Real World.  Registers are essentially
// non-SSA names.  A Register is represented as a number.  Non-regular values
// (e.g., Control, Memory, I/O) use the Special register.  The actual machine
// registers (as described in the ADL file for a machine) start at zero.
// Stack-slots (spill locations) start at the nest Chunk past the last machine
// register.
//
// Note that stack spill-slots are treated as a very large register set.
// They have all the correct properties for a Register: not aliased (unique
// named).  There is some simple mapping from a stack-slot register number
// to the actual location on the stack; this mapping depends on the calling
// conventions and is described in the ADL.
//
// Note that Name is not enum. C++ standard defines that the range of enum
// is the range of smallest bit-field that can represent all enumerators
// declared in the enum. The result of assigning a value to enum is undefined
// if the value is outside the enumeration's valid range. OptoReg::Name is
// typedef'ed as int, because it needs to be able to represent spill-slots.
//
class OptoReg VALUE_OBJ_CLASS_SPEC { 
 public:
  typedef int Name;
  enum {
    // Chunk 0
    Physical = 0/*AdlcVMDeps::Physical*/, // Start of physical regs 
    // A few oddballs at the edge of the world
    Special = -2,		// All special (not allocated) values
    Bad = -1,			// Not a register
    SPILL_REG=29999             // Register number of a spilled LRG
  };

  // Stack pointer register
  static Name c_frame_pointer;

  // Increment a register number.  As in:
  //    "for ( OptoReg::Name i; i=Control; i = add(i,1) ) ..."
  static Name add( Name x, int y ) { return Name(x+y); }

  // (We would like to have an operator+ for RegName, but it is not
  // a class, so this would be illegal in C++.)

  static inline bool is_valid(OptoReg::Name r) { return r != Bad; }
  static inline bool is_reg  (OptoReg::Name r) { return is_valid(r) && r<REG_COUNT; }
  static inline bool is_stack(OptoReg::Name r) { return is_valid(r) && !is_reg(r); }
  // Convert register numbers to stack slots (NOT offsets) and vice
  // versa.  Note that stack-offsets do not make sense in the "warped"
  // world, or at least a simple scaling of stack-slot by 8 will not
  // yield the final stack offset - because of the warp factor.
  static inline unsigned reg2stack( OptoReg::Name r ) {assert0(r >= REG_COUNT); return r - REG_COUNT; }
  static inline OptoReg::Name stack2reg( int idx ) { return OptoReg::Name(REG_COUNT+idx); }

  static void dump( int );

  // Unwarp - given post-register-allocation frame info
  static inline VReg::VR as_VReg(OptoReg::Name reg, int frame_slots, int arg_count) {
    if( reg < REG_COUNT ) return as_VReg(reg);
    return VReg::VR((reg < stack2reg(arg_count)) ? reg+frame_slots : reg-arg_count);
  }
  static inline VReg::VR as_VReg(OptoReg::Name reg) { // Register only
    assert0(is_reg(reg));
    return VReg::VR(reg);
  }
};


//------------------------------RegMask----------------------------------------
// The ADL file describes how to print the machine-specific registers, as well
// as any notion of register classes.  We provide a register mask, which is
// just a collection of Register numbers.  

// The ADLC defines 2 macros, RM_SIZE and FORALL_BODY.
// RM_SIZE is the size of a register mask in words.
// FORALL_BODY replicates a BODY macro once per word in the register mask.
// The usage is somewhat clumsy and limited to the regmask.[h,c]pp files.
// However, it means the ADLC can redefine the unroll macro and all loops
// over register masks will be unrolled by the correct amount.

class RegMask VALUE_OBJ_CLASS_SPEC {
  union {
    double _dummy_force_double_alignment[RM_SIZE>>1];
    // Array of Register Mask bits.  This array is large enough to cover 
    // all the machine registers and all parameters that need to be passed
    // on the stack (stack registers) up to some interesting limit.  Methods
    // that need more parameters will NOT be compiled.  On Intel, the limit
    // is something like 90+ parameters.
    int _A[RM_SIZE];
  };

  enum {
    _WordBits    = BitsPerInt,
    _LogWordBits = LogBitsPerInt,
    _RM_SIZE     = RM_SIZE   // local constant, imported, then hidden by #undef
  };

public:
  enum { CHUNK_SIZE = RM_SIZE*_WordBits };

  // A constructor only used by the ADLC output.  All mask fields are filled
  // in directly.  Calls to this look something like RM(1,2,3,4);
  RegMask( 
#   define BODY(I) int a##I,
    FORALL_BODY   
#   undef BODY
    int dummy = 0 ) {
#   define BODY(I) _A[I] = a##I;
    FORALL_BODY
#   undef BODY
  }

  // Handy copying constructor
  RegMask( RegMask *rm ) {
#   define BODY(I) _A[I] = rm->_A[I];
    FORALL_BODY
#   undef BODY
  }

  // Construct an empty mask
  RegMask( ) { Clear(); }

  // Construct a mask with a single bit
  RegMask( OptoReg::Name reg ) { Clear(); Insert(reg); }

  // Check for register being in mask
  int Member( OptoReg::Name reg ) const {
assert((int)reg<CHUNK_SIZE,"");
    return _A[reg>>_LogWordBits] & (1<<(reg&(_WordBits-1)));
  }

  // The last bit in the register mask indicates that the mask should repeat
  // indefinitely with ONE bits.  Returns TRUE if mask is infinite or 
  // unbounded in size.  Returns FALSE if mask is finite size.
  int is_AllStack() const { return _A[RM_SIZE-1] >> (_WordBits-1); }

  // Work around an -xO3 optimization problme in WS6U1. The old way:
  //   void set_AllStack() { _A[RM_SIZE-1] |= (1<<(_WordBits-1)); }
  // will cause _A[RM_SIZE-1] to be clobbered, not updated when set_AllStack()
  // follows an Insert() loop, like the one found in init_spill_mask(). Using
  // Insert() instead works because the index into _A in computed instead of
  // constant.  See bug 4665841.
  void set_AllStack() { Insert(OptoReg::Name(CHUNK_SIZE-1)); }

  // Test for being a not-empty mask.
  int is_NotEmpty( ) const {
    int tmp = 0;
#   define BODY(I) tmp |= _A[I];
    FORALL_BODY
#   undef BODY
    return tmp;
  }

  // Find lowest-numbered register from mask, or BAD if mask is empty.
  OptoReg::Name find_first_elem() const { 
    int base, bits;
#   define BODY(I) if( (bits = _A[I]) != 0 ) base = I<<_LogWordBits; else
    FORALL_BODY
#   undef BODY
      { base = OptoReg::Bad; bits = 1<<0; }
    return OptoReg::Name(base + find_lowest_bit(bits)); 
  }
  // Get highest-numbered register from mask, or BAD if mask is empty.
  OptoReg::Name find_last_elem() const { 
    int base, bits;
#   define BODY(I) if( (bits = _A[RM_SIZE-1-I]) != 0 ) base = (RM_SIZE-1-I)<<_LogWordBits; else
    FORALL_BODY
#   undef BODY
      { base = OptoReg::Bad; bits = 1<<0; }
    return OptoReg::Name(base + find_hihghest_bit(bits)); 
  }

  // Test for single register
  int is_bound1() const;

  // Fast overlap test.  Non-zero if any registers in common.
  int overlap( const RegMask &rm ) const {
    return 
#   define BODY(I) (_A[I] & rm._A[I]) |
    FORALL_BODY
#   undef BODY
    0 ;
  }

  // Special test for register pressure based splitting
  // UP means register only, Register plus stack, or stack only is DOWN
  bool is_UP() const;

  // Clear a register mask
  void Clear( ) {
#   define BODY(I) _A[I] = 0;
    FORALL_BODY
#   undef BODY
  }

  // Fill a register mask with 1's
  void Set_All( ) {
#   define BODY(I) _A[I] = -1;
    FORALL_BODY
#   undef BODY
  }

  // Insert register into mask
  void Insert( OptoReg::Name reg ) {
assert((int)reg<CHUNK_SIZE,"");
    _A[reg>>_LogWordBits] |= (1<<(reg&(_WordBits-1)));
  }

  // Remove register from mask
  void Remove( OptoReg::Name reg ) {
assert((int)reg<CHUNK_SIZE,"");
    _A[reg>>_LogWordBits] &= ~(1<<(reg&(_WordBits-1)));
  }

  // OR 'rm' into 'this'
  void OR( const RegMask &rm ) {
#   define BODY(I) this->_A[I] |= rm._A[I];
    FORALL_BODY
#   undef BODY
  }

  // AND 'rm' into 'this'
  void AND( const RegMask &rm ) {
#   define BODY(I) this->_A[I] &= rm._A[I];
    FORALL_BODY
#   undef BODY
  }

  // Subtract 'rm' from 'this'
  void SUBTRACT( const RegMask &rm ) {
#   define BODY(I) _A[I] &= ~rm._A[I];
    FORALL_BODY
#   undef BODY
  }

  // Compute size of register mask: number of bits
  uint Size() const;

  // Ask if the given mask is a subset of the 'this' mask
int is_subset(const RegMask&rm)const{
#   define BODY(I) if((_A[I] | rm._A[I]) != _A[I]) return false;
    FORALL_BODY
#   undef BODY
    return true;
  }

#ifndef PRODUCT
  void print() const { dump(); }
  void dump() const;            // Print a mask
#endif

  static const RegMask Empty;   // Common empty mask

  static bool can_represent(OptoReg::Name reg) {
    // NOTE: -1 in computation reflects the usage of the last
    //       bit of the regmask as an infinite stack flag.
    return (int)reg < (int)(CHUNK_SIZE-1);
  }
};

// Do not use this constant directly in client code!
#undef RM_SIZE
#endif // REGMASK_HPP

