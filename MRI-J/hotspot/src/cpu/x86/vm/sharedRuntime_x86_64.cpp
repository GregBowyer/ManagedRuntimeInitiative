/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
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



#include "register_pd.hpp"
#include "sharedRuntime.hpp"

#include "frame.inline.hpp"
#include "oop.inline.hpp"
#include "register_pd.inline.hpp"

// ---------------------------------------------------------------------------
// Read the array of BasicTypes from a signature, and compute where the
// arguments should go.  Values less than REG_COUNT are registers, those above
// refer to stack byte offsets.  All stack slots are based off of the stack
// top.  Register values 0-15 (up to RegisterImpl::number_of_registers) are
// the 16 64-bit registers.

// Register passing mostly matches the C ABI and the first 6 non-FP args are
// passed in RDI, RSI, RDX, RCX, R08, R09.  FP values are passed in XMM0-XMM7.
// The regs array refer to the 64-bit registers or stack slots. 

// Arguments past these are passed above the caller's RSP and also skipping
// a 'sizeof(IFrame)' sized hole in case we are being called from the
// interpreter via a phat-frame.
//
// Example:  calling foo( int a0i0, double a12f01, int a3i1, int a4i2, int a5i3, int a6i4, int a7i5, int a8i6, double a910f23, int a11i7 );
//   RDI: a0i0, RSI: a3i1, RDX: a4i2, RCX: a5i3, R08:a6i4, R09: a7i5
//   F00: a12f01, F01: a910f23
// 
// high                   
//   |      a11i7            incoming args for VReg::stk2reg(1*8)
//   |      a8i6             incoming args for VReg::stk2reg(0*8)
//   +- - - - - - - - - - -
//   |      iframe.3         HOLE owned by caller
//   |      iframe.2         HOLE owned by caller
//   |      iframe.1         HOLE owned by caller
//   |      iframe.0         HOLE owned by caller
//   +=====================  CallER Frame
//   |      return adr       Callee Frame foo()
//   |      tmps 
//   |      outgoing args
//   |      hole for callee
//   +---------------------  <<== RSP
// low
//
// Note: sig_bt are in units of Java argument words, which are 64-bit.
//
// Note: Non-FP return results are in RAX, FP return results in XMM0.

// If RDI is an OOP, it is returned in RDI.  This is to capture the common
// notion of making a foo.xxx() call, followed by using foo again.

ByteSize SharedRuntime::java_calling_convention(const BasicType *sig_bt, VReg::VR *regs, int total_args_passed, bool is_outgoing ) {
  static const int int_reg_max = 6;
  static const int flt_reg_max = 8;
  static const  Register intarg[int_reg_max] = { RDI, RSI, RDX, RCX, R08, R09 };
  static const FRegister fltarg[flt_reg_max] = { F00, F01, F02, F03, F04, F05, F06, F07 };
  static const int skip_iframe = (sizeof(IFrame)/8);

  assert( gpr2reg(R15) < REG_COUNT, "overlapping stack/register numbers" );

  // Convention is to pack the first 6 integer args into the first 6 GPRs
  // (RDI-R09), and first 8 float/doubles in the first 8 XMM regs, extras
  // spill to the stack one 8-byte word per java arg.
  int int_arg_cnt = 0;
  int flt_arg_cnt = 0;
  int stk_arg_cnt = skip_iframe; // Skip room for an entire interpreter frame, if needed
  for( int i = 0; i < total_args_passed; i++) {
    switch( sig_bt[i] ) {
case T_LONG://Longs & doubles fit in 1 register
      assert0( sig_bt[i+1] == T_VOID );
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
    case T_INT:
    case T_SHORT:
    case T_ARRAY:
    case T_OBJECT:
case T_ADDRESS://Thread* passed into Runtime calls, or null
      regs[i] = (int_arg_cnt < int_reg_max) ? gpr2reg(intarg[int_arg_cnt++]) : VReg::stk2reg((stk_arg_cnt++)<<3);
      break;
    case T_DOUBLE:
      assert0( sig_bt[i+1] == T_VOID );
    case T_FLOAT:
      regs[i] = (flt_arg_cnt < flt_reg_max) ? fpr2reg(fltarg[flt_arg_cnt++]) : VReg::stk2reg((stk_arg_cnt++)<<3);
      break;
case T_VOID://Always follows a long or double
      regs[i] = VReg::Bad;
      break;
    default:
      ShouldNotReachHere();
    }
  }

  // number of outgoing stack argument bytes required
  return (stk_arg_cnt == skip_iframe) ? in_ByteSize(0) : in_ByteSize(stk_arg_cnt<<3);
}

ByteSize SharedRuntime::c_calling_convention(const BasicType *sig_bt, VReg::VR *regs, int total_args_passed, bool is_outgoing ) {
  static const int int_reg_max = 6;
  static const int flt_reg_max = 8;
  static const  Register intarg[int_reg_max] = { RDI, RSI, RDX, RCX, R08, R09 };
  static const FRegister fltarg[flt_reg_max] = { F00, F01, F02, F03, F04, F05, F06, F07 };

  assert( gpr2reg(R15) < REG_COUNT, "overlapping stack/register numbers" );

  // Convention is to pack the first 6 integer args into the first 6 GPRs
  // (RDI-R09), and first 8 float/doubles in the first 8 XMM regs, extras
  // spill to the stack one 8-byte word per java arg.
  int int_arg_cnt = 0;
  int flt_arg_cnt = 0;
  int stk_arg_cnt = 0;
  for( int i = 0; i < total_args_passed; i++) {
    switch( sig_bt[i] ) {
case T_LONG://Longs & doubles fit in 1 register
      assert0( sig_bt[i+1] == T_VOID );
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
    case T_INT:
    case T_SHORT:
    case T_ARRAY:
    case T_OBJECT:
      regs[i] = (int_arg_cnt < int_reg_max) ? gpr2reg(intarg[int_arg_cnt++]) : VReg::stk2reg((stk_arg_cnt++)<<3);
      break;
    case T_DOUBLE:
      assert0( sig_bt[i+1] == T_VOID );
    case T_FLOAT:
      regs[i] = (flt_arg_cnt < flt_reg_max) ? fpr2reg(fltarg[flt_arg_cnt++]) : VReg::stk2reg((stk_arg_cnt++)<<3);
      break;
case T_VOID://Always follows a long or double
      regs[i] = VReg::Bad;
      break;
    case T_ADDRESS: // Used, e.g., in slow-path locking for the lock's stack address
    default:
      ShouldNotReachHere();
    }
  }

  return in_ByteSize(stk_arg_cnt<<3); // number of outgoing argument bytes required
}
