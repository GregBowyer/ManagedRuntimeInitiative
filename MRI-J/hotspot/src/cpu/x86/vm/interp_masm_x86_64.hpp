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
#ifndef INTERP_MASM_PD_HPP
#define INTERP_MASM_PD_HPP

#include "assembler_pd.hpp"
#include "allocation.hpp"
#include "bytecodes.hpp"
class InterpreterCodelet;

class InterpreterMacroAssembler: public MacroAssembler {
  friend class TemplateTable;   // for access to _dispatch_next_step
  // Tracks which steps of dispatch_next have been scheduled for assertions and
  // implicit completion.
  enum {
    DISPATCH_NEXT_RESET,
    DISPATCH_NEXT_0,
    DISPATCH_NEXT_1,
    DISPATCH_NEXT_2
  };
  int _dispatch_next_state;
  int _dispatch_next_step;

 public:
  InterpreterMacroAssembler(CodeBlob::BlobType type, const char *const name) :
    MacroAssembler(Thread::current()->resource_area(),type,0,name), _dispatch_next_state(DISPATCH_NEXT_RESET) {}

  InterpreterCodelet *make_codelet(const char* desc, Bytecodes::Code bc);
CodeBlob*blob()const{return _blob;}

  // dispatch routines
  void dispatch_prolog(TosState state, int step);
void dispatch_epilog(TosState state);

  void dispatch_next(int step);

  // dispatch_next steps split up for explicit scheduling
  void dispatch_next_0(RInOuts, Register Rbc);
  void dispatch_next_1(RInOuts, Register Rbc);
  void dispatch_next_2(RInOuts, Register Rbc);

  virtual void check_and_handle_popframe();

  // tagged stack store checks
  void store_sentinel(RKeepIns, Register Rslot, int off);
  void single_slot_locals_store_check(RInOuts, Register Rslot, Register Rsentinel, Register Rtmp, int slotnum);
  void double_slot_locals_store_check(RInOuts, Register Rslot, Register Rsentinel, Register Rtmp);
void push_tos_adj_sp(Register reg);
  void adj_sp_pop_tos   (); // load tos when top was 64 bit wide

  // expression stack
  void push_tos() { st8 (RDI_JEX, 0,RCX_TOS); add8i(RDI_JEX,8); }
  void pop_tos () { ld8 (RCX_TOS,RDI_JEX,-8); sub8i(RDI_JEX,8); }
  void empty_java_exec_stack( RInOuts, Register Rtmp );

  // Subtype checking
  void check_subtype(RInOuts, Register Rsubkid, Register RAX_superklass, Register R09_tmp, Register Rtmp2, Register Rtmp3, Label& is_subtype);

  // tear down an interpreted frame
  void remove_activation(RInOuts, Register Rsi, Register Rcx, Register Rtmp0, bool exception_exit, bool throw_if_locked);

  // misc
  void get_cache_index_at_bcp(RInOuts, Register index, RKeepIns, Register Rbcp, int offset);  // load index from bytecode stream and scale

  // support for jvmti
  void notify_method_entry();
  void notify_method_exit(bool is_native_method);

  void verify_FPU(int stack_depth, TosState state = vtos) { }

  void getmoop( Register rbx ); // Loads method OOP into method

  // Packs RDX_LOC & RSI_BCP into the frame word
  void pack_interpreter_regs(RInOuts, Register Rtmp0, Register Rtmp1, Register Rdx_loc, RKeepIns, Register Rdi_jex, Register Rsi_bcp );
  void unpack_interpreter_regs(RInOuts, Register Rmoop, Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc );

  // Store val_ref into base_ref+offset, doing a store-check first.  The
  // Store-check can trigger generational card-marking, and also SBA escapes.
  // SBA escapes can trigger GC.  Hence this macro can GC and is an interpreter
  // GC point.  If a safepoint happens it must be taken BEFORE the store
  // bytecode, because the store cannot have happened yet (and the store cannot
  // happen until GC makes space for the SBA escape).
  void ref_store_with_check(RInOuts, Register Rbase, int off, Register Rindex, int scale, Register Rval, Register Rtmp0, Register Rtmp1,
Label&retry);

  // VM calls

  // RDX, RSI, RDI, R08 are saved/restored.
  // All caller save registers are killed.
  void call_VM(address entry_point, Register Rresult,
               Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
               Register Rtmp0, Register Rarg1, Register Rarg2, Register Rarg3);

  void call_VM_leaf_interp(address adr);
  void call_VM_leaf_interp(address adr, Register arg0)                   { call_VM_leaf_interp(adr, arg0, noreg, noreg); }
  void call_VM_leaf_interp(address adr, Register arg0, Register arg1 )   { call_VM_leaf_interp(adr, arg0, arg1,  noreg); }
  void call_VM_leaf_interp(address adr, Register arg0, Register arg1, Register arg2 );

  // not enough free registers for a call_VM with 4 args, so special case of call_VM for SharedRuntime::_new
  void call_runtime_new(Bytecodes::Code bcode,
                        Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
                        Register R11_kid, Register R09_bytes, Register R10_len, bool allow_sba);

  Register get_free_reg(Bytecodes::Code bcode,
                        Register inuse1,       Register inuse2=noreg, Register inuse3=noreg,
                        Register inuse4=noreg, Register inuse5=noreg, Register inuse6=noreg,
                        Register inuse7=noreg, Register inuse8=noreg, Register inuse9=noreg) {
    static const int interpreter_regs_bitmask =
      1<<RAX | 1<<RCX | 1<<RDX | 1<<RSI | 1<<RDI |
      1<<R08 | 1<<R09 | 1<<R10 | 1<<R11;
    int inuse_regs_bitmask = 1<<inuse1;
    inuse_regs_bitmask |= (inuse2 != noreg) ? 1<<inuse2 : 0;
    inuse_regs_bitmask |= (inuse3 != noreg) ? 1<<inuse3 : 0;
    inuse_regs_bitmask |= (inuse4 != noreg) ? 1<<inuse4 : 0;
    inuse_regs_bitmask |= (inuse5 != noreg) ? 1<<inuse5 : 0;
    inuse_regs_bitmask |= (inuse6 != noreg) ? 1<<inuse6 : 0;
    inuse_regs_bitmask |= (inuse7 != noreg) ? 1<<inuse7 : 0;
    inuse_regs_bitmask |= (inuse8 != noreg) ? 1<<inuse8 : 0;
    inuse_regs_bitmask |= (inuse9 != noreg) ? 1<<inuse9 : 0;
    int allocatable_register_bitmask = interpreter_regs_bitmask & ~inuse_regs_bitmask;
assert(allocatable_register_bitmask!=0,"not possible - all registers are allocated");
Register result=noreg;
for(int i=0;i<R12;i++){
#ifdef ASSERT
if((1<<i)&allocatable_register_bitmask){
        mov8u( (Register)i, 0xEBAD0000 | bcode );
      }
#endif
      if( (result == noreg) && ((1<<i) & allocatable_register_bitmask) ) {
        result = (Register)i;
      }
    }
    return result;
  }
};

#endif // INTERP_MASM_PD_HPP

