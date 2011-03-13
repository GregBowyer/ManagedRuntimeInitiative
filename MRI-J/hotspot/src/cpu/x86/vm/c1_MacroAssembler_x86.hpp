/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_MACROASSEMBLER_PD_HPP
#define C1_MACROASSEMBLER_PD_HPP


// C1_MacroAssembler contains high-level macros for C1

 private:
  // initialization
void pd_init(){/*Nothing to initialize*/}

 public:
  // See stubGenerator_x86.cpp, new_handler
  // Inputs:
  //   RCX - array length (0 for objects, and the entry point for objects will zap RCX)
  //   RDX - size in bytes
  //   RSI - Klass ID
  // Outputs:
  //   Z/NZ- failed or allocated; failed requires a VM call which will GC
  //   RCX - array length, also set into the object
  //   RAX - new objectRef, pre-zero'd with markWord & array length
  void allocate(Register RDX_size_in_bytes, Register RCX_element_count_ekid, Register RSI_kid, Register RAX_oop, address call_stub, Label &slow_case);

  void invalidate_registers(bool inv_rax, bool inv_rbx, bool inv_rcx, bool inv_rdx, bool inv_rsi, bool inv_rdi) PRODUCT_RETURN;

protected:
  // perform the store-check
  void pre_write_barrier_compiled(RInOuts, Register Rtmp0, Register Rtmp1,
                                  RKeepIns, Register Rbase, int off,  Register Rindex, int scale, Register Rval,
                                  CodeEmitInfo *info);
public:

  // Store val_ref into base_ref+offset, doing a store-check first.
  int ref_store_with_check(RInOuts, Register Rbase, Register Rtmp0, Register Rtmp1,
                           RKeepIns, Register Rindex, int off, int scale, Register Rval,
                           CodeEmitInfo *info);
  // Store val_ref into base_ref+offset, doing a store-check first and compare-and-swapping only if Rcmp is equal to the location
  int ref_cas_with_check(RInOuts, Register Rbase, Register Rcmp, Register Rtmp0, Register Rtmp1,
                         RKeepIns, Register Rindex, int off, int scale, Register Rval,
                         CodeEmitInfo *info);

  // Restore callee save registers and jump
  void restore_callee_saves_pop_frame_and_jmp(FrameMap* frame_map, address entry);

  // lcmp bytecode
  void lcmp2int(Register Rdst, Register Rleft, Register Rright);

  // Figure out the cheapest register to use when we have a selection
  Register cheapest_encodable_register(Register r1, Register r2) {
    // NB ignores RSP as that has a specific meaning
    // Return the lowest value register that isn't RBP. We assume that RBP always
    // costs an SIB byte whereas a REX prefix may be hidden.
    Register min_reg = r1 < r2 ? r1 : r2;
    if (min_reg != RBP) return min_reg;
    return (r1 != RBP) ? r1 : r2;
  }
  Register cheapest_encodable_register(Register r1, Register r2, Register r3) {
    return cheapest_encodable_register(cheapest_encodable_register(r1, r2), r3);
  }
  Register cheapest_encodable_register(Register r1, Register r2, Register r3, Register r4) {
    return cheapest_encodable_register(cheapest_encodable_register(r1, r2, r3), r4);
  }
  Register cheapest_encodable_register(Register r1, Register r2, Register r3, Register r4, Register r5) {
    return cheapest_encodable_register(cheapest_encodable_register(r1, r2, r3, r4), r5);
  }
#endif
