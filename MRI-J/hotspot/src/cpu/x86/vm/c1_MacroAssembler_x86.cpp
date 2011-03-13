/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_LIRAssembler.hpp"
#include "c1_MacroAssembler.hpp"
#include "c1_Runtime1.hpp"
#include "c1_globals.hpp"
#include "disassembler_pd.hpp"
#include "gpgc_layout.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"

#include "c1_FrameMap.hpp"
#include "c1_LinearScan.hpp"
#include "oop.inline.hpp"

void C1_MacroAssembler::allocate(Register RDX_size_in_bytes, Register RCX_element_count_ekid, Register RSI_kid, Register RAX_oop, address alloc_stub, Label &slow_case) {
  // RDX: size in bytes
  // RCX: (EKID<<32 | element count)
  // RSI: kid
  assert0( RDX == RDX_size_in_bytes );
  assert0( RCX == RCX_element_count_ekid );
  assert0( RSI == RSI_kid );
  NativeAllocationTemplate::fill(this, alloc_stub);
  assert0( RAX == RAX_oop );
  // Z - failed, RAX - blown, RDX, RCX, RSI all preserved for the slow-path.
  // NZ- OK, R09- new oop, stripped; RAX - new oop w/mark, R10- preserved as ary length
  jeq  (slow_case);
  verify_not_null_oop(RAX_oop, MacroAssembler::OopVerify_NewOop);
  // Fall into the next code, with RAX holding correct value
}



void C1_MacroAssembler::inline_cache_check(Register receiver, Register iCache) {
  Unimplemented();
//  verify_oop(receiver);
//  // explicit NULL check not needed since load from [klass_offset] causes a trap  
//  // check against inline cache
//  assert(!MacroAssembler::needs_explicit_null_check(oopDesc::klass_offset_in_bytes()), "must add explicit null check");
//  int start_offset = offset();
//  cmpl(iCache, Address(receiver, oopDesc::klass_offset_in_bytes())); 
//  // if icache check fails, then jump to runtime routine
//  // Note: RECEIVER must still contain the receiver!
//  jump_cc(Assembler::notEqual,
//          RuntimeAddress(SharedRuntime::get_ic_miss_stub()));
//  assert(offset() - start_offset == 9, "check alignment in emit_method_entry");
}

void C1_MacroAssembler::build_frame(FrameMap* frame_map) {
  // offset from the expected fixed sp within the method
  int sp_offset = in_bytes(frame_map->framesize_in_bytes()) - 8; // call pushed the return IP
  if( frame_map->num_callee_saves() > 0 ) {
    int callee_save_num = frame_map->num_callee_saves()-1;
    int callee_saves    = frame_map->callee_saves(); // bitmap
    for (int i=LinearScan::nof_cpu_regs-1; i>=0; i--) {
      if ((callee_saves & 1<<i) != 0) {
        int wanted_sp_offset = frame_map->address_for_callee_save(callee_save_num)._disp;
        assert0( sp_offset-8 == wanted_sp_offset );
        push((Register)i);
        sp_offset -= 8;
        callee_save_num--;
        assert0( callee_save_num >= -1 );
      }
    }
#ifdef ASSERT
for(int i=0;i<LinearScan::nof_xmm_regs;i++){
      int reg = LinearScan::nof_cpu_regs+i;
      assert ((callee_saves & 1<<reg) == 0, "Unexpected callee save XMM register");
    }
#endif
  }
  if (sp_offset != 0) {
    // make sp equal expected sp for method
    if (sp_offset == 8) push  (RCX);            // push reg as smaller encoding than sub8i
    else                sub8i (RSP, sp_offset );
  }
  if( should_verify_oop(MacroAssembler::OopVerify_IncomingArgument) ) {
    int args_len = frame_map->incoming_arguments()->length();
    for(int i=0; i < args_len; i++) {
      LIR_Opr arg = frame_map->incoming_arguments()->at(i);
      if (arg->is_valid() && arg->is_oop()) {
        VOopReg::VR oop = frame_map->oopregname(arg);
        verify_oop(oop, MacroAssembler::OopVerify_IncomingArgument);
      }
    }
  }
}

void C1_MacroAssembler::method_exit(FrameMap* frame_map) {
  // offset from the expected fixed sp within the method
  int sp_offset = 0;
  // adjust SP over spills...
  sp_offset = in_bytes(frame_map->framesize_in_bytes()) - 8 - (frame_map->num_callee_saves()*8);
  if (sp_offset == 8) pop(RCX);                // pop and blow arbitrary caller save, smaller encoding than add8i
  else                add8i (RSP, sp_offset );
  if( frame_map->num_callee_saves() > 0 ) {
    int callee_save_num = 0;
    int callee_saves    = frame_map->callee_saves(); // bitmap
for(int i=0;i<LinearScan::nof_cpu_regs;i++){
      if ((callee_saves & 1<<i) != 0) {
        int wanted_sp_offset = frame_map->address_for_callee_save(callee_save_num)._disp;
        assert0( sp_offset == wanted_sp_offset );
        pop((Register)i);
        sp_offset += 8;
        callee_save_num++;
        assert0( callee_save_num <= frame_map->num_callee_saves() );
      }
    }
#ifdef ASSERT
for(int i=0;i<LinearScan::nof_xmm_regs;i++){
      int reg = LinearScan::nof_cpu_regs+i;
      assert ((callee_saves & 1<<reg) == 0, "Unexpected callee save XMM register");
    }
#endif
  }
  assert0 (sp_offset == (in_bytes(frame_map->framesize_in_bytes())-8) );
  ret   ();
}

void C1_MacroAssembler::restore_callee_saves_pop_frame_and_jmp(FrameMap* frame_map, address entry) {
  int callee_save_num=0;
  int callee_saves = frame_map->callee_saves();
for(int i=0;i<LinearScan::nof_cpu_regs;i++){
    if ((callee_saves & 1<<i) != 0) {
      int sp_offset = frame_map->address_for_callee_save(callee_save_num)._disp;
      ld8((Register)i, RSP, sp_offset);
      callee_save_num++;
    }
  }
#ifdef ASSERT
for(int i=0;i<LinearScan::nof_xmm_regs;i++){
    int reg = LinearScan::nof_cpu_regs+i;
    assert ((callee_saves & 1<<reg) == 0, "Unexpected callee save XMM register");
  }
#endif
  add8i (RSP, in_bytes(frame_map->framesize_in_bytes()) - 8);
  jmp(entry);
}

void C1_MacroAssembler::entry( CodeProfile *cp ) {
if(C1Breakpoint)os_breakpoint();

  if (UseC2 && ProfileMethodEntry) {
Label no_overflow;
    int invoff = CodeProfile::  invoke_count_offset_in_bytes() + in_bytes(InvocationCounter::counter_offset());
    int beoff  = CodeProfile::backedge_count_offset_in_bytes() + in_bytes(InvocationCounter::counter_offset());
    mov8i(R11,(intptr_t)cp);
    ldz4 (RAX, R11, invoff);    // increment the "# of calls" entry
    add4 (RAX, R11, beoff );
    inc4 (R11, invoff);         // increment "# of calls"
    cmp4i(RAX, C1PromotionThreshold);
    jbl  (no_overflow);
    call (Runtime1::entry_for(Runtime1::frequency_counter_overflow_wrapper_id));
    // No oop-map or debug info here, similar to resolve_and_patch_call the
    // caller NOT this code is responsible for GC'ing the arguments.
bind(no_overflow);
  }
}

#ifndef PRODUCT

void C1_MacroAssembler::invalidate_registers(bool inv_rax, bool inv_rbx, bool inv_rcx, bool inv_rdx, bool inv_rsi, bool inv_rdi) {
#ifdef ASSERT
  if (inv_rax) mov8i(RAX, 0xDEAD);
  if (inv_rbx) mov8i(RBX, 0xDEAD);
  if (inv_rcx) mov8i(RCX, 0xDEAD);
  if (inv_rdx) mov8i(RDX, 0xDEAD);
  if (inv_rsi) mov8i(RSI, 0xDEAD);
  if (inv_rdi) mov8i(RDI, 0xDEAD);

  if (inv_rax) mov8i(R08, 0xDEAD);
  if (inv_rbx) mov8i(R09, 0xDEAD);
  if (inv_rcx) mov8i(R10, 0xDEAD);
  if (inv_rdx) mov8i(R12, 0xDEAD);
  if (inv_rsi) mov8i(R13, 0xDEAD);
  if (inv_rdi) mov8i(R14, 0xDEAD);
  if (inv_rdi) mov8i(R15, 0xDEAD);
#endif
}
#endif // ifndef PRODUCT

// --- pre_write_barrier_compiled
void C1_MacroAssembler::pre_write_barrier_compiled(RInOuts, Register Rtmp0, Register Rtmp1,
                                                   RKeepIns, Register Rbase, int off,  Register Rindex, int scale, Register Rval,
                                                   CodeEmitInfo *info) {
  Label retry, safe_to_store;
  if (UseSBA) bind(retry); // Come here to retry barrier following an SBA escape
  // Perform regular pre write barrier
  pre_write_barrier(RInOuts::a, Rtmp0, Rtmp1, RKeepIns::a, Rbase, off, Rindex, scale, Rval, safe_to_store);
  if( UseSBA ) {
    // SBA escape will update Rbase. Rbase may already have been added to the
    // oop map for patching. Force Rbase into oop map.
    // NB. this is the last use of the oop map!
    info->oop_map()->add((VOopReg::VR)Rbase);
    // Full SBA escape - Rtmp0 holds FID of Rbase
    // Place args to sba_escape below return address in outgoing stack frame
    assert0 (-frame::runtime_stub_frame_size + frame::xPAD == -16)
    st8(RSP, -16, Rval);
    assert0 (-frame::runtime_stub_frame_size + frame::xRAX == -24)
    st8(RSP, -24, Rbase);
    call(StubRoutines::x86::sba_escape_handler());  // Do call
assert(info,"oop map expected");
    add_oopmap(rel_pc(), info->oop_map()); // Add oop map on return address of call
    add_dbg(rel_pc(), info->debug_scope());
    jmp (retry);
  }
bind(safe_to_store);
}

// --- ref_store_with_check
// Store oop taking care of SBA escape and barriers. Returns relpc of where NPE
// info should be added.
int C1_MacroAssembler::ref_store_with_check(RInOuts, Register Rbase, Register Rtmp0, Register Rtmp1,
                                            RKeepIns, Register Rindex, int off, int scale, Register Rval,
                                            CodeEmitInfo *info) {
Label strip;

#ifdef ASSERT
  verify_oop(Rval, MacroAssembler::OopVerify_Store);
  if (RefPoisoning) move8(Rtmp1,Rval); // Save Rval
#endif

  null_chk( Rval,strip  ); // NULLs are always safe to store

  pre_write_barrier_compiled(RInOuts::a, Rtmp0, Rtmp1,
                             RKeepIns::a, Rbase, off, Rindex, scale, Rval,
                             info);
#ifdef ASSERT
  if (RefPoisoning) {
    mov8  (Rtmp1,Rval);   // Save Rval again as it will have been squashed by the barrier
    if( Rbase == Rval ) { // if base can look like value then don't poison base
      assert0( MultiMapMetaData );
      Rval = Rtmp1;
      Rtmp1 = Rbase;
    }
    always_poison(Rval); // Poison ref
  }
#endif // ASSERT
bind(strip);
  verify_not_null_oop(Rbase, MacroAssembler::OopVerify_StoreBase);
  cvta2va(Rbase);
  int npe_relpc = rel_pc();
#ifdef ASSERT
  // check the value to be squashed is an oop, npe on this rather than the store
  if (should_verify_oop(MacroAssembler::OopVerify_OverWrittenField))
    npe_relpc = verify_oop (Rbase, off, Rindex, scale, MacroAssembler::OopVerify_OverWrittenField);
#endif
  if (Rindex == noreg) st8  (Rbase, off, Rval);
  else                 st8  (Rbase, off, Rindex, scale, Rval);
#ifdef ASSERT
  if (RefPoisoning) mov8(Rval,Rtmp1); // Restore unpoisoned Rval
#endif
  return npe_relpc;
}

// --- ref_cas_with_check
// Write barriered compare of Rcmp with memory, if equal set memory to Rval. Set
// flags dependent on success. Returns relpc of where NPE info should be added.
// NB on failure Rcmp contains the value from memory, this will be poisoned and
// not lvb-ed. ie. you shouldn't use this value.
int C1_MacroAssembler::ref_cas_with_check(RInOuts, Register Rbase, Register Rcmp, Register Rtmp0, Register Rtmp1,
                                          RKeepIns, Register Rindex, int off, int scale, Register Rval,
                                          CodeEmitInfo *info) {
  assert0( Rcmp == RAX );
  Label checked, strip;

#ifdef ASSERT
  verify_oop(Rval, MacroAssembler::OopVerify_Store);
  verify_oop(Rcmp, MacroAssembler::OopVerify_Move);
  if (RefPoisoning) move8(Rtmp1,Rval); // Save Rval
#endif

  null_chk( Rval,strip  ); // NULLs are always safe to store

  pre_write_barrier_compiled(RInOuts::a, Rtmp0, Rtmp1,
                             RKeepIns::a, Rbase, off, Rindex, scale, Rval,
                             info);

#ifdef ASSERT
  if (RefPoisoning) {
    mov8  (Rtmp1,Rval);  // Save Rval again as it will have been squashed by the barrier
    always_poison(Rval); // Must store poisoned ref
  }
#endif // ASSERT
bind(strip);
  verify_not_null_oop(Rbase, MacroAssembler::OopVerify_StoreBase);
  cvta2va(Rbase);
#ifdef ASSERT
  if (RefPoisoning) {
    poison(Rcmp);      // Compared register must also be posioned
  }
#endif // ASSERT
bind(checked);
  int npe_relpc = rel_pc();
#ifdef ASSERT
  // check the value to be cas-ed is an oop, npe on this rather than the store
  if (should_verify_oop(MacroAssembler::OopVerify_OverWrittenField))
    npe_relpc = verify_oop (Rbase, off, Rindex, scale, MacroAssembler::OopVerify_OverWrittenField);
#endif
  if (Rindex == noreg) locked()->cas8 (Rbase, off, Rval);
  else                 locked()->cas8 (Rbase, off, Rindex, scale, Rval);
#ifdef ASSERT
  pushf();
  if (RefPoisoning) {
    mov8    (Rval,Rtmp1); // Restore unpoisoned Rval
    unpoison(Rcmp);       // Compared register must also be unposioned
  }
  verify_oop(Rval, MacroAssembler::OopVerify_Move);
  verify_oop(Rcmp, MacroAssembler::OopVerify_Move);
  popf();
#endif
  return npe_relpc;
}

// lcmp bytecode
void C1_MacroAssembler::lcmp2int(Register Rdst, Register Rleft, Register Rright) {
  if (Rdst != Rleft && Rdst != Rright) {
    Label done;
    mov8u  (Rdst, -1, true);
    cmp8   (Rright, Rleft);
    jgt    (done);            // Rdst=-1 if greater
    setnz  (Rdst);            // Rdst=0 if equal, or 1 if less
    movzx81(Rdst, Rdst);
    bind   (done);
  } else {
Label less,done;
    cmp8   (Rright, Rleft);
    jgt    (less);           // Rdst=-1 if greater
    setnz  (Rdst);           // Rdst=0 if equal, or 1 if less
    movzx81(Rdst, Rdst);
jmp(done);
bind(less);
    mov8u  (Rdst, -1, true);
    bind   (done);
  }
}
