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


#include "assembler_pd.hpp"
#include "codeProfile.hpp"
#include "copy.hpp"
#include "deoptimization.hpp"
#include "disassembler_pd.hpp"
#include "interp_masm_pd.hpp"
#include "interpreter_pd.hpp"
#include "interpreterRuntime.hpp"
#include "klassIds.hpp"
#include "oopTable.hpp"
#include "resourceArea.hpp"
#include "sharedRuntime.hpp"
#include "stubCodeGenerator.hpp"
#include "stubRoutines.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "globals.hpp"
#include "handles.inline.hpp"
#include "heapRefBuffer.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "gpgc_tlb.hpp"

#include "os/thread.h"
#ifdef AZ_PROXIED
#include "proxy/nio.h"
#endif // AZ_PROXIED

// Declaration and definition of StubGenerator (no .hpp file).
// For a more detailed description of the stub routine structure
// see the comment in stubRoutines.hpp.

#define __ _masm->

static address handle_unsafe_access() {
  JavaThread* thread = JavaThread::current();
  
  // epc is the address for the failing unsafe access instruction.
  // rather than emulate, doing a no-op is fine:  return garbage from the load
  // Compute the pc for next instruction and hope for the best.
 
  // REVISIT? NativeMovRegMem should do the job.  Another option is to port
  // the javasoft code to compute next instruction address for x86...
  NativeMovRegMem *epc = (NativeMovRegMem*) thread->_epc;  
  address npc = epc->next_instruction_address();

  // request an async exception.  
  // This operation needs to walk the stack.
  thread->set_pending_unsafe_access_error();

  // return address of next instruction to execute
  return npc;
}

static address handle_nio_protection_fault(){
#if !defined(AZ_PROXIED)
   ShouldNotReachHere();   return NULL;
#else  // AZ_PROXIED
address continue_pc=NULL;
  ThreadBase *thr = (ThreadBase*)(((intptr_t)&continue_pc) & ~(thread_stack_size - 1));

  // Expects following fields to have been set up...
  // _libos_nio_sigsegv_access_type  -- access type (exec, store, or load)
  // _libos_eva  -- failing nio buffer access address
  // _libos_epc  -- pc of the failing access instruction
  continue_pc = (address) nio_protection_fault_handler( 
                                         thr->_libos_eva, 
                                         thr->_libos_nio_sigsegv_access_type);
 
  // Any return value except AZUL_SIGNAL_RESUME_EXECUTION is fatal.
  // If AZUL_SIGNAL_NOT_HANDLED is returned, then there is an internal
  // consistency or resource issue.  As long as we get a stack trace at this 
  // point, crashing should be fine.
  guarantee( continue_pc == AZUL_SIGNAL_RESUME_EXECUTION, 
"nio_protection_failure");
  
  // return address of original faulting instruction...
  return ((address) thr->_libos_epc);
#endif // AZ_PROXIED
}

// --- pd_reg_to_addr --------------------------------------------------------
// Fast map from register number to a runtime-stub frame layout
intptr_t *frame::pd_reg_to_addr(VReg::VR regname) const {
  assert(0 <= regname && regname < REG_COUNT, "sanity check");
  Register R = (Register)regname;
  static int offs[] = {
    xRAX, // 0- RAX
    xRCX, // 1- RCX
    xRDX, // 2- RDX
    xRBX, // 3- RBX
    xRSP, // 4- RSP
    xRBP, // 5- RBP (callee save and not a Frame Pointer)
    xRSI, // 6- RSI
    xRDI, // 7- RDI
    xR08, // 8- R08
    xR09, // 9- R09
    xR10, //10- R10
    xR11, //11- R11
    xR12, //12- R12
    xR13, //13- R13
    xR14, //14- R14
    xR15, //15- R15
    xF00, //16- F00
    xF01, //17- F01
    xF02, //18- F02
    xF03, //19- F03
    xF04, //20- F04
    xF05, //21- F05
    xF06, //22- F06
    xF07, //23- F07
    xF08, //24- F08
    xF09, //25- F09
    xF10, //26- F10
    xF11, //27- F11
    xF12, //28- F12
    xF13, //29- F13
    xF14, //30- F14
    xF15, //31- F15
  };
  assert0(R>=0 && R<32);
  assert0(offs[R] != -1);     // trying to fetch a register that was not saved?
  return (intptr_t*)((address)_sp-frame::runtime_stub_frame_size+offs[R]);
}

// ---------------------------------------------------------------------------
class StubGenerator : public StubCodeGenerator {

  // --- gen_resolve_and_patch_call
  // This stub is entered from compiled methods. Unlike the other stubs, this
  // one needs a Real Frame which allows stack crawls and GC - we get here from
  // calls with all kinds of signatures, so the exact argument oop layout is
  // only known to the VM.  The VM may need to do a GC - which means it might
  // need to discover the spilled location of all oop registers. Also, it might
  // throw an exception instead of making the call and the caller might end up
  // Deopt'ing.
address gen_resolve_and_patch_call(){
    StubCodeMark mark(this, "StubRoutines", "resolve_and_patch_handler", frame::runtime_stub_frame_size);
    Label start(_masm);

    // We are going to call into the VM and must be able to discover all
    // registers that the compiler might carry debug info or arguments in.
    // We also have to save & restore all arg registers that C++ might blow
    // including the FP arguments.
    uint32_t save_mask = 
      (1<<RDI) | (1<<RSI) | (1<<RDX) | (1<<RCX) | (1<<R08) | (1<<R09) | // arg registers
      (1<<F00) | (1<<F01) | (1<<F02) | (1<<F03) | (1<<F04) | (1<<F05) | (1<<F06) | (1<<F07) | // arg registers
      (1<<RBX) | (1<<RBP) | (1<<R12) | (1<<R13) | (1<<R14) | (1<<R15) | // callee-save regs holding debug info
      0;                        

    // Make a Frame.  Return address and Old Frame Pointer are already in place.
    __ call_VM_compiled((address)SharedRuntime::resolve_and_patch_call,
                        save_mask, RDI/*pass in receiver as arg1*/, noreg );

    // Check for pending exception from resolution
Label no_exception;
    __ getthr(R10);             // R10 already blown by having a call in the 1st place, so it's free here
    __ cmp8i(R10,in_bytes(JavaThread::pending_exception_offset()),0);
    __ jne  (StubRoutines::forward_exception_entry());
#ifdef ASSERT
Label ok;
    __ test8(RAX,RAX);
__ jne(ok);
__ should_not_reach_here("should return an address");
__ bind(ok);
#endif // ASSERT    
    __ jmp8 (RAX);              // Return result is where the call is supposed to go

__ patch_branches();
    // Rare point in the VM where the Caller, not the Callee, must GC the args.
    CodeCache::_caller_must_gc_args_0 = _masm->last_oopmap_adr();

    return start.abs_pc(_masm);
  }

  //---------------------------------------------------------------------------------
  void generate_initial() {
    // Generates initial stubs and initializes the entry points
StubRoutines::_verify_oop_subroutine_entry=gen_verify_oop();
    StubRoutines::_verify_ref_klass_subroutine_entry = gen_verify_ref_klass();

    StubRoutines::_forward_exception_entry = gen_forward_exception(StubRoutines::_c2_internal_rethrows);

    // The GCLdValueTr trap handler needs to be generated very early, as the LVB instruction may trigger them.
    StubRoutines::x86::_handler_for_GCLdValueTr_entry = gen_handler_for_GCLdValueTr();

    if (UseSBA || RefPoisoning) {
      StubRoutines::x86::_handler_for_GCStValueNMTTr_entry = gen_handler_for_GCStValueNMTTr();
    }
    if (UseGenPauselessGC) {    // GPGC done
      StubRoutines::x86::_handler_for_GCThread_GCLdValueTr_entry = gen_handler_for_GCThread_GCLdValueTr();
    }

    gen_handler_for_new(&StubRoutines::_new_fast, &StubRoutines::_new_fast_array, &StubRoutines::_new_sba, &StubRoutines::_new_sba_array );

StubRoutines::_call_stub_entry=gen_call_stub(StubRoutines::_call_stub_return_address);
    // The call_stub alone amongst all stubs saves RBP where GDB can find it.
    HotspotToGdbSymbolTable.symbolsAddress[HotspotToGdbSymbolTable.numEntries-1].savedRBP=true;

    StubRoutines::_lazy_c2i_entry  = gen_lazy_c2i();
    StubRoutines::_uncommon_trap_entry = gen_uncommon_trap();
    StubRoutines::_deopt_asm_entry = gen_deopt_asm();
    StubRoutines::_lock_entry  = gen_lock();
    StubRoutines::x86::_c2_lock_entry  = gen_c2_lock();
    StubRoutines::x86::_blocking_lock_entry  = gen_blocking_lock  ();
    StubRoutines::_unlock_entry= gen_unlock(false);
    StubRoutines::_unlock_c2_entry= gen_unlock(true);
    StubRoutines::_register_finalizer_entry = gen_finalizer();

    StubRoutines::x86::_handler_for_null_ptr_exception_entry = gen_handler_for_null_ptr_exception();
    StubRoutines::x86::_partial_subtype_check = gen_partial_subtype_check();
    StubRoutines::x86::_full_subtype_check = gen_full_subtype_check();
    StubRoutines::x86::_sba_escape_handler = gen_sba_escape_handler();
  }

  // --- 
  void generate_all() {
    // Generates all remaining stubs and initializes the entry points
    StubRoutines::_resolve_and_patch_call_entry = gen_resolve_and_patch_call();
    StubRoutines::_safepoint_trap_handler       = gen_safepoint_trap_handler();
    StubRoutines::x86::_c1_profile_callee            = generate_c1_profile_callee();
    StubRoutines::_handler_for_unsafe_access_entry           = gen_handler_for_unsafe_access();

    // entry points that are platform specific
    StubRoutines::x86::_handler_for_nio_protection_entry     = gen_handler_for_nio_protection();

    gen_prim_arraycopy(&StubRoutines::x86::_prim_arraycopy1, &StubRoutines::x86::_prim_arraycopy2,
                       &StubRoutines::x86::_prim_arraycopy4, &StubRoutines::x86::_prim_arraycopy8,
                       &StubRoutines::x86::_prim_arraycopy1_no_overlap, &StubRoutines::x86::_prim_arraycopy2_no_overlap,
                       &StubRoutines::x86::_prim_arraycopy4_no_overlap, &StubRoutines::x86::_prim_arraycopy8_no_overlap);
    StubRoutines::_object_arraycopy = gen_object_arraycopy();
    StubRoutines::_checkcast_arraycopy = gen_checkcast_arraycopy();
    StubRoutines::_generic_arraycopy   = generate_generic_copy("generic_arraycopy");
  }

public:
address gen_safepoint_trap_handler();
address generate_c1_profile_callee();
  StubGenerator(bool all) : StubCodeGenerator(CodeBlob::runtime_stubs, "Stubs") {
    if( all ) generate_all();
    else      generate_initial();
  }
  StubGenerator(const char *name) : StubCodeGenerator(CodeBlob::runtime_stubs, name) { }

  // --- verify RAX is a valid oop, preserves all registers including RAX
address gen_verify_oop(){
    StubCodeMark mark(this, "StubRoutines", "verify_oop", frame::runtime_stub_frame_size);
    Label start(_masm), invalid_oop, zero;
    // preserve everything except callee saves
    uint32_t save_mask = -1        & ~(1<<RBX) & ~(1<<RBP) & ~(1<<RSP) &
                         ~(1<<R12) & ~(1<<R13) & ~(1<<R14) & ~(1<<R15);

    __ pushf();                 // save flags
    __ test8(RAX,RAX);
    __ jeq  (zero);             // NULL is a perfectly valid oop
    __ add8i(RSP,-(frame::runtime_stub_frame_size-16));

    // Save all requested GPR registers
    assert0((RAX == 0) && (frame::xRAX == 31*8));
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        __ st8(RSP,(31-i)*8,( Register)i);
    // Save all requested FPR registers
for(int i=16;i<32;i++)
      if( (1<<i)&save_mask )
        __ st8(RSP,(31-i)*8,(FRegister)i);

    __ call_VM_leaf((address)SharedRuntime::verify_oop, RAX);
    __ test1 (RAX, RAX);
    __ jeq   (invalid_oop); // if (RAX == 0) goto invalid_oop

    // Restore all requested GPR registers
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        __ ld8(( Register)i, RSP,(31-i)*8);
    // Restore all requested FPR registers
for(int i=16;i<32;i++)
      if( (1<<i)&save_mask )
        __ ld8((FRegister)i, RSP,(31-i)*8);

    __ add8i(RSP, frame::runtime_stub_frame_size-16);
__ bind(zero);
__ popf();
__ ret();
__ bind(invalid_oop);

    // Restore all requested GPR registers
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        __ ld8(( Register)i, RSP,(31-i)*8);
    // Restore all requested FPR registers
for(int i=16;i<32;i++)
      if( (1<<i)&save_mask )
        __ ld8((FRegister)i, RSP,(31-i)*8);

    __ stop ("verify oop failed"); // verification has failed, oop is in RAX
    __ add8i(RSP, frame::runtime_stub_frame_size-16);
__ popf();
__ ret();
__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- Verify RAX is an objectRef with a KID that has a valid NMT.
  //
  // RAX should contain the objectRef, and R10 should contain an ID used
  // to make it easy to figure out where in the code this was called from.
address gen_verify_ref_klass(){
    StubCodeMark mark(this, "StubRoutines", "verify_oop", frame::runtime_stub_frame_size);
    Label start(_masm);
    
    // preserve everything except callee saves
    uint32_t save_mask = -1        & ~(1<<RBX) & ~(1<<RBP) & ~(1<<RSP) &
                         ~(1<<R12) & ~(1<<R13) & ~(1<<R14) & ~(1<<R15);

    __ pushf();                 // save flags
    __ add8i(RSP,-(frame::runtime_stub_frame_size-16));

    // Save all requested GPR registers
    assert0((RAX == 0) && (frame::xRAX == 31*8));
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        __ st8(RSP,(31-i)*8,( Register)i);
    // Save all requested FPR registers
for(int i=16;i<32;i++)
      if( (1<<i)&save_mask )
        __ st8(RSP,(31-i)*8,(FRegister)i);

    __ call_VM_leaf((address)SharedRuntime::verify_ref_klass, RAX);

    // Restore all requested GPR registers
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        __ ld8(( Register)i, RSP,(31-i)*8);
    // Restore all requested FPR registers
for(int i=16;i<32;i++)
      if( (1<<i)&save_mask )
        __ ld8((FRegister)i, RSP,(31-i)*8);

    __ add8i(RSP, frame::runtime_stub_frame_size-16);
__ popf();
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- Handler for Load-Value-Barrier traps (NMT/LVB)
address gen_handler_for_GCLdValueTr(){
    // RSP offsets are defined by the MacroAssembler::LVB_FrameLayout enum.

    StubCodeMark mark(this, "StubRoutines", "handler_for_GCLdValueNMTTr", MacroAssembler::LVB_Framesize);
    Label start(_masm);
    Label lvb_done, set_up_return_value_for_parallelgc, remap_trap, cas_ok;
    Label ok0, ok1;

    // skip over the args + tmp regs
    __ sub8i(RSP, MacroAssembler::LVB_Framesize-8);
    // Save temp registers so the callers do not need to worry about register usage.
    __ st8  (RSP, MacroAssembler::LVB_Saved_RAX, RAX);
    __ st8  (RSP, MacroAssembler::LVB_Saved_RCX, RCX);
    __ st8  (RSP, MacroAssembler::LVB_Saved_RDX, RDX);
    __ st8  (RSP, MacroAssembler::LVB_Saved_RSI, RSI);
    __ st8  (RSP, MacroAssembler::LVB_Saved_RDI, RDI);

    Register RSI_Val  = RSI;
    Register RDX_TAVA = RDX;
    Register RDI_tmp1 = RDI;
    Register RCX_tmp2 = RCX;

    // Register RBX_TAVA = RBX;
    // Register RCX_Val  = RCX;
    // Register RDX_tmp1 = RDX;
    // Register RBP_tmp2 = RBP;

    __ ld8  (RSI_Val, RSP, MacroAssembler::LVB_Value); // Value is not-poisoned and not-null

#ifdef ASSERT
    __ mov8  (RAX, RSI_Val); 
    __ shr8i (RAX, objectRef::space_shift);
    __ test1i(RAX, objectRef::space_mask); 
    __ jne   (ok0);
__ should_not_reach_here("should not find pointers");
__ bind(ok0);
#endif

    // get the trap flags now
    __ move8(RAX, RSI_Val); // Need some of the high 4 bytes so a 8-byte move
    __ shr8i(RAX, objectRef::offset_in_page);
    __ and4i(RAX, GPGC_ReadTrapArray::read_barrier_index_mask);
    // assuming the array will be allocated in the lower 4GB
    int rdbar_ary = (intptr_t)GPGC_ReadTrapArray::address_of_read_barrier_array();
    if( rdbar_ary == (intptr_t)GPGC_ReadTrapArray::address_of_read_barrier_array() ) {
      __ ldz1  (RAX, noreg, rdbar_ary, RAX, 0);
    } else {
      __ mov8i(RDX_TAVA,(intptr_t)GPGC_ReadTrapArray::address_of_read_barrier_array() );
      __ ldz1  (RAX, RDX_TAVA  ,         0, RAX, 0);
    }


    // Reg contents:
    //   RSI_Val = unpoisoned Value
    //   RAX     = trap flag

    // Test if this is an remap trap or an nmt trap:
    __ cmp1i   (RAX, GPGC_ReadTrapArray::NMTTrapped);
    __ jgt     (remap_trap);       // Not just an NMT trap.  Might be a remap, or a remap+NMT trap.
    // fall through for the nmt-trap
#ifndef AT_PRODUCT
#ifdef ASSERT
    if ( true ) {
#else
    if ( GPGCVerifyHeap ) {
#endif // ASSERT
      Label in_new_space, complete;
      __ mov8  (RCX_tmp2, RSI_Val); 
      __ shr8i (RCX_tmp2, objectRef::space_shift);
      __ and8i (RCX_tmp2, objectRef::space_mask); 
      __ cmp8i (RCX_tmp2, objectRef::new_space_id);
      __ jeq   (in_new_space);
      __ ld8   (RDI_tmp1 , GPGC_NMT::old_trap_enabled_addr());
__ jmp(complete);
__ bind(in_new_space);
      __ ld8   (RDI_tmp1 , GPGC_NMT::new_trap_enabled_addr());
      
Label ok;
__ bind(complete);
      __ cmp8i (RDI_tmp1, 0);
__ jne(ok);
      
      if (UseSBA) {
        __ cmp8i (RCX_tmp2, objectRef::stack_space_id);
__ jeq(ok);
      }

__ should_not_reach_here("GPGC: NMT trap (Ld) when disabled, and not a stackRef");
__ bind(ok);
    }
#endif // ! AT_PRODUCT


    // Prepare to CAS back the fixed heapRef in a few cycles
    __ ld8     (RDX_TAVA, RSP, MacroAssembler::LVB_TAVA);
    __ mov8    (RAX, RSI_Val);                               // Original value to RAX
    if( RefPoisoning ) __ always_poison(RAX);                // Poison for the CAS  // TODO: This should check if poisoning is needed based on TAVA!
    // Flip the heapRef's NMT bit state, so we've got the desired NMT bit value.
    __ mov8    (RCX_tmp2, RSI_Val);                          // copy original value into RCX_tmp2 as well
    __ btc8i   (RCX_tmp2, objectRef::nmt_shift);             // RCX_tmp2 = fixed heapRef value, non-poisoned
    __ st8     (RSP, MacroAssembler::LVB_Result, RCX_tmp2);  // Store the fixed heapRef in the Result slot on the stack.
    if( RefPoisoning ) __ always_poison(RCX_tmp2);           // Poison for the CAS
    
    // Reg contents:
    //   RDX_TAVA = address the heapRef was loaded from
    //   RSI_Val  = original heapRef value (no   poison)
    //   RAX      = original heapRef value (with poison)
    //   RCX_tmp2 = fixed heapRef value i.e. has the right NMT value (with poison)

    // Try and rewrite memory with the fixed reference.
    __ locked()-> cas8(RDX_TAVA, 0, RCX_tmp2);  // Try and write the fixed & poisoned heapRef back to memory
    // TODO: maw: Verify that there are no remap-only updates, which would cause heap corruption if we compete
    // TODO: maw: with a remap-only update, and lose the CAS.  We'd be left with a ref that's remapped, but
    // TODO: maw: doesn't have the NMT set!

    // Reg contents:
    //   RDX_TAVA = address the heapRef was loaded from
    //   RSI_Val  = original heapRef value (no   poison)
    //   RAX      = original heapRef value (with poison)
    //   RCX_tmp2 = fixed heapRef value i.e. has the right NMT value (with poison)

    // If this is a stack ref and not a heap ref, then don't mark/enqueue it
    if (UseSBA) {
__ unimplemented("do not mark/enqueue SBA?  Why are we here for SBA?");
    }

    // If we get here, then we have updated the heapRef's NMT, and we now must
    // enqueue it for the collector to mark through.

Label in_new_space;
Label complete;
    __ getthr  (RAX);
    __ mov8    (RDI_tmp1, RSI_Val);
    __ shr8i   (RDI_tmp1, objectRef::space_shift);
    __ and8i   (RDI_tmp1, objectRef::space_mask);
    __ cmp8i   (RDI_tmp1, objectRef::new_space_id);
    // TODO: maw: make ref buffer lookup be in an array of buffers indexed by space, so we can skip the test and branches.
    __ jeq     (in_new_space);
    __ ld8     (RAX, RAX, in_bytes(Thread::old_gen_ref_buffer_offset())); 
__ jmp(complete);
__ bind(in_new_space);
    __ ld8     (RAX, RAX, in_bytes(Thread::new_gen_ref_buffer_offset())); 
__ bind(complete);

    DEBUG_ONLY ({
Label assert_ok;
      __ cmp8i (RAX, 0);
      __ jnz   (assert_ok);
__ should_not_reach_here("No HeapRefBuffer attached to thread");
__ bind(assert_ok);
    })

    __ ld8     (RDI_tmp1, RAX, in_bytes(HeapRefBuffer::top_offset()));

    // Reg contents:
    //   RDX_TAVA = address the heapRef was loaded from
    //   RSI_Val  = original heapRef value (no poison)
    //   RDI_tmp1 = HeapRefBuffer buffer top index
    //   RAX      = HeapRefBuffer*
    //   RCX_tmp2 = fixed heapRef value i.e. has the right NMT value (with poison)

    DEBUG_ONLY ({
Label assert_ok;
      __ cmp8i (RDI_tmp1, HeapRefBuffer::end_index());
      __ jlt   (assert_ok);
__ should_not_reach_here("HeapRefBuffer overflow");
__ bind(assert_ok);
    })

    // Increment buffer_top and store it back into the HeapRefBuffer
    __ mov8    (RCX_tmp2, RDI_tmp1);
    __ inc8    (RCX_tmp2);
    __ st8     (RAX, in_bytes(HeapRefBuffer::top_offset()), RCX_tmp2);
    // Record the fixed heapRef in the thread's HeapRefBuffer.
    __ shl8i   (RDI_tmp1, LogBytesPerWord+1);  // get the index into _entry 
    __ add8    (RDI_tmp1, RAX); // addr of entry
    __ add8i   (RDI_tmp1, in_bytes(HeapRefBuffer::entries_offset()));
    if( KIDInRef ) {
Label has_kid;
      __ mov8    (RAX, RDX_TAVA);
      __ shr8i   (RAX, objectRef::klass_id_shift);
      __ and4i   (RAX, objectRef::klass_id_mask);
      __ jnz     (has_kid);
      __ mov8i   (RAX, KlassIds::lvb_asm); // TODO: We never get referring KID through the LVB trap at this time.
__ bind(has_kid);
    } else {
      // No referring KID because no KID in referrers REF
      __ mov8i   (RAX, KlassIds::lvb_asm); 
    }
    __ st4     (RDI_tmp1, in_bytes(HeapRefBuffer::Entry::referrer_klass_id_offset()), RAX);
    // get RDI_tmp1 back
    __ mov8    (RAX, RSI_Val);              // copy Value into RAX as well
    __ btc8i   (RAX, objectRef::nmt_shift); // RAX = fixed heapRef value with fixed NMT, non-poisoned
    __ st8     (RDI_tmp1, in_bytes(HeapRefBuffer::Entry::raw_value_offset()), RAX);

    // If the buffer is full, we have to call into the VM
Label restore_regs;
    __ cmp8i   (RCX_tmp2, HeapRefBuffer::end_index());
    __ jne     (restore_regs);


    // Reg contents:
    //   RDX_TAVA = address the heapRef was loaded from
    //   RSI_Val  = original heapRef value
    //   RDI_tmp1 = tmp
    //   RAX      = tmp
    //   RCX_tmp2 = HeapRefBuffer buffer top index

    { 
      // Prep to call into the VM: we need to save regs that will be stomped.
      // hammer approach.. not sure which register get blown out by the call to the VM.. save all 

      // Setup the regs for calling into the VM.
      Label in_new_space, complete;
      __ getthr(RDI);
      __ mov8  (RCX_tmp2, RSI_Val); // copy TAValue into RDI_tmp1 as well
      __ shr8i (RCX_tmp2, objectRef::space_shift);
      __ and8i (RCX_tmp2, objectRef::space_mask);
      __ cmp8i (RCX_tmp2, objectRef::new_space_id);

      // Save regs, call blows all regs in use by trap handler.
      __ st8   (RSP, MacroAssembler::LVB_Saved_R08, R08);
      __ st8   (RSP, MacroAssembler::LVB_Saved_R09, R09);
      __ st8   (RSP, MacroAssembler::LVB_Saved_R10, R10);
      __ st8   (RSP, MacroAssembler::LVB_Saved_R11, R11);

      __ jeq   (in_new_space);
      __ call  (CAST_FROM_FN_PTR(address, GPGC_NMT::old_space_nmt_buffer_full));  // RDI = Thread*
__ jmp(complete);
__ bind(in_new_space);
      __ call  (CAST_FROM_FN_PTR(address, GPGC_NMT::new_space_nmt_buffer_full));  // RDI = Thread*
__ bind(complete);

      // Restore saved regs
      __ ld8   (R08, RSP, MacroAssembler::LVB_Saved_R08);
      __ ld8   (R09, RSP, MacroAssembler::LVB_Saved_R09);
      __ ld8   (R10, RSP, MacroAssembler::LVB_Saved_R10);
      __ ld8   (R11, RSP, MacroAssembler::LVB_Saved_R11);
    }
    // Done with NMT trap handling
__ jmp(restore_regs);


    //**********************************************************
    //
    // Remap trap handling from here down:
    //
__ bind(remap_trap);

    // Reg contents:
    //   RSI_Val = unpoisoned Value

    __ getthr  (RDI);
    __ ld8     (RDX_TAVA, RSP, MacroAssembler::LVB_TAVA);

    // Save regs, call blows all regs in use by trap handler.
    __ st8     (RSP, MacroAssembler::LVB_Saved_R08, R08);
    __ st8     (RSP, MacroAssembler::LVB_Saved_R09, R09);
    __ st8     (RSP, MacroAssembler::LVB_Saved_R10, R10);
    __ st8     (RSP, MacroAssembler::LVB_Saved_R11, R11);

    __ call    (CAST_FROM_FN_PTR(address, GPGC_TLB::lvb_trap_from_asm)); // RDI = Thread*, RSI = heapRef, RDX = TAVA

    // Restore saved regs
    __ ld8     (R08, RSP, MacroAssembler::LVB_Saved_R08);
    __ ld8     (R09, RSP, MacroAssembler::LVB_Saved_R09);
    __ ld8     (R10, RSP, MacroAssembler::LVB_Saved_R10);
    __ ld8     (R11, RSP, MacroAssembler::LVB_Saved_R11);

    __ st8     (RSP, MacroAssembler::LVB_Result, RAX);

    
    //**********************************************************
    //
    // Stack frame cleanup and return:
    //
__ bind(restore_regs);

    DEBUG_ONLY(
    if( KIDInRef ) {
      // A quick sanity check of the Value returned from the trap:
      // Ensure that the KID in the Value matches the KID in the mark word of the oop.
Label kid_matched;
      __ ld8    (RAX, RSP, MacroAssembler::LVB_Result); // Load returned objectRef value off stack.
      __ mov8   (RDI, RAX);
      __ shr8i  (RAX, objectRef::klass_id_shift);       // Get KID from objectRef.
      __ cvta2va(RDI);                                  // Convert objectRef to oop.
      __ ld8    (RCX, RDI, 0);                          // Load mark word.
      __ shr8i  (RCX, markWord::kid_shift);             // Get KID from mark word.
      __ cmp8   (RCX, RAX);
      __ jeq    (kid_matched);

__ should_not_reach_here("KID mismatch in returned LVB Value");

__ bind(kid_matched);
    }
    )

    DEBUG_ONLY(
      if ( ! RefPoisoning && KIDInRef ) {
        // A quick sanity check of the TAVA for the trap:
        // Ensure that the KID of the objectRef in TAVA matches the KID in the mark word of the oop.
Label kid_matched;
        __ ld8    (RDX, RSP, MacroAssembler::LVB_TAVA);   // Load TAVA of trap off stack.
        __ ld8    (RAX, RDX, 0);                          // Load objectRef from TAVA
        __ mov8   (RDI, RAX);
        __ shr8i  (RAX, objectRef::klass_id_shift);       // Get KID from objectRef.
        __ cvta2va(RDI);                                  // Convert objectRef to oop.
        __ ld8    (RCX, RDI, 0);                          // Load mark word.
        __ shr8i  (RCX, markWord::kid_shift);             // Get KID from mark word.
        __ cmp8   (RCX, RAX);
        __ jeq    (kid_matched);

__ should_not_reach_here("KID mismatch in returned LVB Value");

__ bind(kid_matched);
    })

    __ ld8  (RAX, RSP, MacroAssembler::LVB_Saved_RAX);
    __ ld8  (RCX, RSP, MacroAssembler::LVB_Saved_RCX);
    __ ld8  (RDX, RSP, MacroAssembler::LVB_Saved_RDX);
    __ ld8  (RSI, RSP, MacroAssembler::LVB_Saved_RSI);
    __ ld8  (RDI, RSP, MacroAssembler::LVB_Saved_RDI);

    // do we need a leave() here to restore RBP and the RSP ???
__ bind(lvb_done);
    __ add8i(RSP, MacroAssembler::LVB_Framesize-8);
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

address gen_handler_for_GCStValueNMTTr(){
    StubCodeMark mark(this, "StubRoutines", "handler_for_GCStValueNMTTr",8);
    Label start(_masm);
__ unimplemented("handler_for_GCStValueNMTNMTTr");
__ patch_branches();
    return start.abs_pc(_masm);
  }

address gen_handler_for_GCThread_GCLdValueTr(){
    StubCodeMark mark(this, "StubRoutines", "handler_for_GCStValueNMTTr",8);
    Label start(_masm);
__ unimplemented("handler_for_GCStValueNMTNMTTr");
__ patch_branches();
    return start.abs_pc(_masm);
  }

  //----------------------------------------------------------------------------
  // Continuation point for runtime calls returning with a pending exception or
  // anybody who has popped a frame and doesn't know the new current frame type.
  // There is a pending exception in thread->_pending_exception.  Call into the
  // VM and determine the proper handler for the current frame.
address gen_forward_exception(address&c2_internal_rethrows){
    StubCodeMark mark(this, "StubRoutines", "forward_exception", frame::runtime_stub_frame_size);

    // Entry point for exception passed in a reg (used by C2) for internal rethrows
    Label Lexc(_masm);
__ push(RAX);//c2 allows for debug info in all registers here
    __ getthr(RAX);             // so save a temp register
    __ st8 (RAX,in_bytes(Thread::pending_exception_offset()),RDI);
__ pop(RAX);

    // Contract with calling code:
    //
    //   exceptionOop is in Thread::_pending_exception.  The return PC is
    //   under the SP and the frame pointer is under that.  All registers are
    //   preserved.  Stack is aligned.
    //
    //   If the caller wishes to exit his frame (eg. native wrappers), he pops
    //   his frame (leaving only his return PC) and jumps here.
    // 
    //   If the caller wishes return to a handler in his frame, he needs to
    //   call this code, with the return PC under the SP.  
    __ align(CodeEntryAlignment);
    Label start(_masm);

    Label Lfat16_entry, Lfat256_entry, Ldone_fat_popped;
    Label Lfat16_addr_ref, Lfat256_addr_ref;

__ bind(Lfat16_addr_ref);
    __ cmp4i(RSP, 0,(int32_t)0xBAADBAAD);
    __ jeq  (Lfat16_entry);
__ bind(Lfat256_addr_ref);
    __ cmp4i(RSP, 0,(int32_t)0xBAADBAAD);
    __ jeq  (Lfat256_entry);

__ bind(Ldone_fat_popped);

    // GC & deopt is possible here; finding exception handles can sometimes require class loading
    __ call_VM_compiled((address)SharedRuntime::find_exception_handler_for_return_address, 
                        -1,     // save/restore all registers
                        retadr_arg, noreg);

    __ st8(RSP, 0, RAX);        // Replace return address
    __ ld8(RAX, RSP, -(frame::runtime_stub_frame_size-8)+frame::xRAX); // Recover RAX, -8 to account for return address still in frame
__ ret();//Trampoline away!

__ bind(Lfat16_entry);
    __ ld8  (RCX,RSP, offset_of(IFrame,_cpc) +8/*ret adr*/);
    __ ld8  (RDX,RSP, offset_of(IFrame,_mref)+8/*ret adr*/);
    __ ld8  (RSI,RSP, offset_of(IFrame,_loc) +8/*ret adr*/);
    __ ld8  (R11,RSP, offset_of(IFrame,_bci) +8/*ret adr*/);
    __ add8i(RSP,16*8);
    __ st8  (    RSP, offset_of(IFrame,_cpc) +8/*ret adr*/, RCX );
    __ st8  (    RSP, offset_of(IFrame,_mref)+8/*ret adr*/, RDX );
    __ st8  (    RSP, offset_of(IFrame,_loc) +8/*ret adr*/, RSI );
    __ st8  (    RSP, offset_of(IFrame,_bci) +8/*ret adr*/, R11 );
__ jmp(Ldone_fat_popped);

__ bind(Lfat256_entry);
    __ ld8  (RCX,RSP, offset_of(IFrame,_cpc) +8/*ret adr*/);
    __ ld8  (RDX,RSP, offset_of(IFrame,_mref)+8/*ret adr*/);
    __ ld8  (RSI,RSP, offset_of(IFrame,_loc) +8/*ret adr*/);
    __ ld8  (R11,RSP, offset_of(IFrame,_bci) +8/*ret adr*/);
    __ add8i(RSP,256*8);
    __ st8  (    RSP, offset_of(IFrame,_cpc) +8/*ret adr*/, RCX );
    __ st8  (    RSP, offset_of(IFrame,_mref)+8/*ret adr*/, RDX );
    __ st8  (    RSP, offset_of(IFrame,_loc) +8/*ret adr*/, RSI );
    __ st8  (    RSP, offset_of(IFrame,_bci) +8/*ret adr*/, R11 );
__ jmp(Ldone_fat_popped);

__ patch_branches();

    StubRoutines::_forward_exception_fat16_entry  = (address)(Lfat16_addr_ref .abs_pc(_masm))+3;
    StubRoutines::_forward_exception_fat256_entry = (address)(Lfat256_addr_ref.abs_pc(_masm))+3;
    c2_internal_rethrows = Lexc.abs_pc(_masm);
    return start.abs_pc(_masm);
  }


  //---------------------------------------------------------------------------
  void gen_handler_for_new(address *new_fast,
address*new_array_fast,
address*new_sba,
                           address *new_sba_array) {
    StubCodeMark mark(this, "StubRoutines", "_new_stub", 24/*ret adr and 2 pushes*/);
    Label do_heap_allocation, shared_object_create, new_full;
    Register R09_OOP = R09; // killed/output
    Register RBX_CLZ = RBX; // preserved
    Register RBX_FID = RBX;
    Register RBX_THR = RBX;
    Register RAX_REF = RAX; // killed/output
    Register R11_NEW = R11; // preserved
    Register RDX_SIZ = RDX; // preserved
    Register RCX_LEN_EKID = RCX; // preserved - or set to zero
    Register RSI_KID = RSI; // preserved
    FRegister F15_ZAP = F15; // killed for zeroing
    guarantee( (TLABZeroRegion/BytesPerCacheLine)*BytesPerCacheLine == TLABZeroRegion, "TLABZeroRegion must be cache line sized" );

    // --------
    // New Fast Default 
    // NOT a GC point!  No VM calls here; just returns Z if failed (heap full; needs GC)

    // Incoming arguments
    // RDX_SIZ: size in bytes
    // RSI_KID: klassID
    // RCX_LEN_EKID: (EKID<<32 | length)  (0 for non-arrays; EKID is 0 for non-oop-arrays)
    //
    // Outgoing arguments
    // NZ : Allocated
    // R09_OOP: the new object, stripped of metadata
    // RAX_REF: the new object, pre-zero'd with markWord set
    // RCX_LEN_EKID: preserved
    // RSI_KID: preserved

    //   -- or --
    // Z  : Failed to allocated
    // R09_OOP: blown
    // RDX_SIZ: size in bytes
    // RCX_LEN_EKID: (EKID<<32 | length)
    // RSI_KID: klassID
    __ align(CodeEntryAlignment);
    Label Lheapfast(_masm);
    __ mov8i(RCX_LEN_EKID,0);     // length for arrays
__ jmp(do_heap_allocation);//Forced large size

__ bind(new_full);
    // Flags: Z - failed to allocate
    // RDX_SIZ - size in bytes
    // RCX_LEN_EKID: (EKID<<32 | length)
    // RSI_KID - KID
__ pop(RBX);
__ pop(R11_NEW);
    __ xor4 (RAX,RAX);          // Set Z
__ ret();

    // --------
    // New Array Fast Default
    // RDX_SIZ: size in bytes
    // RCX_LEN_EKID: (EKID<<32 | length)
    // RSI_KID: klassID
    // Outgoing arguments
    // R09_OOP: the new object, stripped of metadata
    // RAX_REF: the new object, pre-zero'd with markWord set
    // Flags: NZ means OK, Z means failed to allocate
    __ align(CodeEntryAlignment);
    // Shared heap allocation
    // RDX_SIZ: size in bytes
    // RCX_LEN_EKID: length (0 for objects)
    // RSI_KID: klassID
__ bind(do_heap_allocation);

__ push(R11_NEW);
__ push(RBX);

#ifdef ASSERT
    // Check that KID/Klass is still alive.  The interpreter gets KIDs from
    // Klasses, so clearly the Klass is still alive there.  Compiled code
    // bakes in KIDs and passes them in after the KID only exists in the code
    // for a long long time.  Any code containing KIDs though, should only
    // exist if the Klass for the KID still exists.  If the Klass unloaded,
    // the dependencies should kill the code... and we should not need to do a
    // LVB barrier check here.
{Label klass_alive;
    __ cmp8i(noreg,(intptr_t)KlassTable::getKlassTableBase(), RSI_KID, 3, 0);
    __ jne  (klass_alive);
__ stop("Klass was unloaded in _new_fast");
__ bind(klass_alive);
    }
    // Check the validity of an ekid (if given) for oop arrays
{Label done;
    Register RBX_klass = RBX;
    Register R11_tmp = R11;
    __ kid2klass(RInOuts::a, RBX_klass, R11_tmp, RKeepIns::a, RSI_KID);
    __ cvta2va(RBX_klass);            // Determine if this is an object array from the layout helper
    __ ldz4  (R11_tmp,RBX_klass, Klass::layout_helper_offset_in_bytes() + sizeof(oopDesc));
    jint objArray_lh = Klass::array_layout_helper(T_OBJECT);
__ cmp4i(R11_tmp,objArray_lh);
    __ jne   (done);
    __ mov8  (R11_tmp, RCX_LEN_EKID); // Place expected kid value into R11
    __ shr8i (R11_tmp, 32);
    __ verify_kid(R11_tmp);           // Verify
__ bind(done);
    }
#endif
    // Check for negative array sizes
    __ test4(RCX_LEN_EKID,RCX_LEN_EKID);
    __ jsg  (new_full);
    // Check that the allocation fits the tlab
    __ getthr(RBX_THR);
    __ ld8  (RAX_REF, RBX_THR, in_bytes(JavaThread::tlab_top_offset())); 
    __ lea  (R11_NEW,RAX_REF,0,RDX_SIZ,0);
    __ cmp8 (R11_NEW, RBX_THR, in_bytes(JavaThread::tlab_end_offset()));
    __ jae  (new_full);
    __ st8  (RBX_THR, in_bytes(JavaThread::tlab_top_offset()), R11_NEW);

    if( PrintSBAStatistics )    // Collect heap allocation statistics
      __ add8(noreg,(intptr_t)&StackBasedAllocation::_allocation_statistics[0]/* heap is in 0*/, RDX_SIZ);
    if( UseGenPauselessGC && ProfileAllocatedObjects ) 
      __ add_to_allocated_objects(RInOuts::a, R09_OOP, RKeepIns::a, RSI_KID, RDX_SIZ, RBX_THR);
    __ cvta2va(R09_OOP, RAX_REF);        // Strip space-id bits

    // --------
__ bind(shared_object_create);
    // R09_OOP - oop minus metadata
    // RAX_REF - ref (past SBA preheader)
    // RDX_SIZ - size in bytes
    // RCX_LEN_EKID - length (0 for objects)
    // RSI_KID - KID
    // RSI - new top (already saved)
    // RBX - tmp/trash
#ifdef ASSERT
{Label ok;
    // test current and next cache lines are zero
__ push(RDI);//used to index oop
__ push(RCX);//count of bytes to check
__ push(RAX);//value to check for
    __ mov8 (RDI, R09_OOP);             // RDI points to start of region to check
    __ mov8 (RAX, R09_OOP);             // Stripped oop start to RAX
    __ add8i(RAX, TLABZeroRegion + BytesPerCacheLine-1); // RAX holds end of zero region
    __ and8i(RAX, -BytesPerCacheLine);
    __ sub8 (RAX, R09_OOP);             // RAX holds byte count of total zero region size
    __ mov8 (RCX, RAX);                 // RCX = RAX count
    __ mov8i(RAX,0);                    // RAX = 0
    __ repeq()->scas1();                // while(RAX==[RSI++]) RCX-- 
    __ test4(RCX, RCX);                 // if region was zero then continue
__ pop(RAX);
__ pop(RCX);
__ pop(RDI);
__ jze(ok);
__ should_not_reach_here("memory not zero'd before allocation (new)");
__ bind(ok);
    }
#endif // ASSERT

    // --- zero lines
    // On entry the current cache-line holding 'obj' is zeroed, this is followed
    // by the zero region.  We zero any extra cache lines needed and ensure the
    // zero region is also zero.
    __ cvta2va(R11_NEW);        // Strip metadata from new top
    // Set RBX to end of zero region
    __ move8(RBX_CLZ,R09_OOP);
    __ add8i(RBX_CLZ,TLABZeroRegion+BytesPerCacheLine-1);
    __ and8i(RBX_CLZ,-BytesPerCacheLine);
    // Set R11_NEW to end of new zero region
    __ add8i(R11_NEW,TLABZeroRegion+BytesPerCacheLine-1);
    __ and8i(R11_NEW,-BytesPerCacheLine);
    { Label loop, done_zeroing;
      __ cmp8 (RBX_CLZ, R11_NEW);
      __ jeq  (done_zeroing);

      __ xorf (F15,F15);                    // Zero xmm15 register
__ bind(loop);
      assert0( BytesPerCacheLine % 16 == 0 );
for(int i=0;i<BytesPerCacheLine;i+=16){
        if( i % PrefetchStride == 0 ) __ prefetch0(RBX_CLZ, TLABPrefetchSize+i);
        __ st16 (RBX_CLZ,i,F15);            // aligned 16 byte store
      }
      __ add8i(RBX_CLZ, BytesPerCacheLine); // this line is zeroed, so move to next
      __ cmp8 (RBX_CLZ, R11_NEW);
__ jne(loop);

__ bind(done_zeroing);
    }

    // Initialize the mark word
    __ shl8i(RSI_KID,markWord::kid_shift);
    assert0 ( markWord::prototype_without_kid()==0 ); // fix me if this changes
    __ st8  (R09_OOP, oopDesc::mark_offset_in_bytes(), RSI_KID);
__ st8(R09_OOP,arrayOopDesc::length_offset_in_bytes(),RCX_LEN_EKID);

    // Inlined optimized va2ref.  SpaceID already in the tlab.
    // Need to add proper NMT bit.

    // Move the result to RAX_REF, adding the klassID and NMT bit as we go
    assert0( (int)markWord::kid_shift==(int)objectRef::klass_id_shift ); // fix me if this changes
    if( KIDInRef ) {
      __ or_8 (RAX_REF,RSI_KID); // Inject kid into final REF
    }
    __ shr8i(RSI_KID,markWord::kid_shift); // restore RSI_KID
    if ( UseGenPauselessGC ) {    // Inject NMT bit
      Register RBX_TMP = RBX;
      __ ld8   (RBX_TMP, GPGC_NMT::desired_new_nmt_flag_addr());
      __ shl8i (RBX_TMP, objectRef::nmt_shift);
      __ or_8  (RAX_REF, RBX_TMP);          // Inject NMT into the ref
    }
__ pop(RBX);
__ pop(R11_NEW);

    // RAX_REF holds return REF
    // R09_OOP holds return OOP
    // Flags: NZ means OK
__ ret();


    // --------
    // New SBA - Normal new SBA allocations
    // Incoming arguments
    // RDX_SIZ: size in bytes
    // RSI_KID: klassID
    // Outgoing arguments
    // R09_OOP: the new object, stripped of metadata
    // RAX_REF: the new object, pre-zero'd with markWord set
    //          SBA preheader word allocated
    __ align(CodeEntryAlignment);
    Label Lsbafast(_masm);
    Label do_sba_allocation, not_line_start;
    __ mov8i(RCX_LEN_EKID,0);            // No length field for objects
__ jmp(do_sba_allocation);

    // --------
    // New SBA Array Fast
    // Incoming arguments
    // RDX_SIZ: size in bytes
    // RCX_LEN_EKID: (EKID<<32 | length)
    // RSI_KID: klassID
    // Outgoing arguments
    // R09_OOP: the new object, stripped of metadata
    // RAX_REF: the new object, pre-zero'd with markWord set
    //          SBA preheader word allocated
    __ align(CodeEntryAlignment);
    // Shared SBA allocation
    // RCX_LEN_EKID: length (0 for objects)
    // RSI_KID: klassID
    // RDX_SIZ: size in bytes
__ bind(do_sba_allocation);
    
__ push(R11_NEW);
__ push(RBX);
    __ getthr(RBX_THR);
    __ ld8  (RAX_REF, RBX_THR, in_bytes(JavaThread::sba_top_offset())); 
    __ lea  (R11_NEW,RAX_REF,sizeof(SBAPreHeader),RDX_SIZ,0);
    __ cmp8 (R11_NEW, RBX_THR, in_bytes(JavaThread::sba_max_offset()));
    __ jae  (new_full);

    // Make the SBA pre-header for a PC allocation site.
    // The interpreter will stamp down his own pre-header
    __ st8  (RBX_THR, in_bytes(JavaThread::sba_top_offset()), R11_NEW);
    __ ldz4 (RBX_FID,RBX_THR, in_bytes(JavaThread::curr_fid_offset()));
__ push(RBP);//one more register
    __ ldz4 (RBP,RSP,3*8);      // Get return PC for allocation hint
    __ cvta2va(R09_OOP,RAX_REF); // Strip off space id bits
    __ st1  (R09_OOP, offset_of(SBAPreHeader,_fid ), RBX_FID);
    __ st1i (R09_OOP, offset_of(SBAPreHeader,_ispc),   1);
    __ st4  (R09_OOP, offset_of(SBAPreHeader,_moid), RBP);

    if (PrintSBAStatistics) {   // Collect heap allocation statistics
      // RBX_FID still holds FID
      __ lea  (RBP,RDX_SIZ,sizeof(SBAPreHeader)); // size to add
      __ add8(noreg,(intptr_t)&StackBasedAllocation::_allocation_statistics[1]/* heap is in 0, FID 0 is at +8*/,RBX_FID,3, RBP);
    }
__ pop(RBP);

    // R09_OOP - SBAPreHeader*
    // RAX_REF - ref version of R09_OOP
    // RDX_SIZ - size in bytes
    // RCX_LEN_EKID - length (0 for objects)
    // RSI_KID - KID
    // RSI - new top (already saved)
    // RBX - tmp/trash

    // The shared-object-allocation code requires the cache line and next of R09_OOP
    // be already clear, but we just plunked down the SBA pre-header. See if pre-header
    // pushed oop onto a new cache line
Label zero_region;
    __ testi(R09_OOP, BytesPerCacheLine-1); // Check for cache-line alignment
    __ jze  (zero_region);                  // jump if another cache line needs zeroing

    assert0( sizeof(SBAPreHeader) == 8 );
    __ add8i(RAX_REF,8);                    // Skip pre-header
    __ add8i(R09_OOP,8);
__ jmp(shared_object_create);//perform shared object creation

__ bind(zero_region);
    // RAX is cache line aligned start of zero-ed cache line, zero next cache line
    __ xorf (F15,F15);                      // Zero xmm15 register
for(int i=0;i<BytesPerCacheLine;i+=16){
      if( i % PrefetchStride == 0 ) __ prefetch0(RBX_CLZ, TLABPrefetchSize+i);
      __ st16 (R09_OOP,TLABZeroRegion+i,F15);           // aligned 16 byte store
    }
    __ add8i(RAX_REF,8);                    // Skip pre-header
    __ add8i(R09_OOP,8);
__ jmp(shared_object_create);//perform shared object creation

    // ---
__ patch_branches();
    *new_fast = Lheapfast.abs_pc(_masm);
    *new_sba  = Lsbafast .abs_pc(_masm);
    *new_array_fast = do_heap_allocation.abs_pc(_masm);
    *new_sba_array  =  do_sba_allocation.abs_pc(_masm);
  }


  //---------------------------------------------------------------------------
  // Call stubs (aka entry_frame) are used in the entry to the
  // interpreter from native C code. They copy arguments from a native
  // array onto the expression stack. They also save and restore
  // JT->_jexstk_top.
address gen_call_stub(address&return_address){
    StubCodeMark mark(this, "StubRoutines", "call_stub", call_stub_frame_size);
    Label start(_masm), ok;

    // Incoming args:
    //    RDI : JavaCallWrapper *link 
    //    RSI : address          result 
    //    RDX : BasicType        result type
    //    RCX : methodRef        method
    //    R08 : code address     (interpreter) entry point
    //    R09 : intptr_t *       parameters
    //  0(rsp): address          return address to C code
    //  8(rsp): int              parameter size (in words)
    // 16(rsp): Thread *         thread
__ push(RBP);//align frame to 16b; save RBP for GDB
    assert0( sizeof(IFrame) == 6*8 ); // adjust this code
    __ push (RSP,8);            // Repush the return address, to mimic an interpreter frame
    __ pushi(0);                // A zero interpreter pad1 word
    __ pushi(0);                // A zero interpreter bci/numlck word
    __ pushi(0);                // A zero interpreter loc/stk word
__ push(RCX);//Now the methodRef
    __ pushi(0);                // A NULL CPC
    __ getthr(R10);             // Get thread
    __ st4  (RSP,call_stub_saved_rty_offset,RDX); // Save result type over thread

#ifdef ASSERT
{Label L;//make sure we have no pending exceptions
      __ cmp8i(R10, in_bytes(Thread::pending_exception_offset()),0);
__ jeq(L);
      __ stop ("StubRoutines::call_stub: entered with pending exception");
      __ bind (L);
    }
#endif

    // Pass parameters on the JEXSTK.
    // Check for JEXSTK overflow.
    // RDI - JavaCallWrapper (needed for stack crawls)
    // RSI - result ptr
    // RCX - methodRef
    // R08 - code address
    // R09 - parms
    // R10 - JavaThread
    // RAX, RDX, R11 - free; RBP, RBX, R12-R15 - callee-save
    __ ldz4 (RAX,RSP,call_stub_param_count_offset);  // Get count of parameters from C
    __ ld8  (R11,R10,in_bytes(JavaThread::jexstk_top_offset()));
    __ lea  (RDX,R11,0,RAX,3);  // RDX = end of copy region
    __ st8  (RSP,call_stub_saved_jcw_offset,RDI);       // Save JavaCallWrapper for stack crawls
    __ move8(RBP,RSI);          // Save result ptr into RBP
    __ st8  (R10,in_bytes(JavaThread::jexstk_top_offset()),RDX);
    __ xchg8(RCX,RAX);          // RCX=count of args; RAX=methodRef
    __ move8(RSI,R09);          // Start of parameter area
    __ move8(RDI,R11);          // Copy start in RDI
    // assume direction flag is set to *forward* (as per x86 64 ABI)
    __ repeated() -> movs8();   // *rdi++ = *rsi++; rcx--;

    // Perform call: JEX stack holds arguments
    // - RCX holds methodRef to be executed.
    // - RDX holds start of argument area on the JEXSTK.
    // Callee-save registers to be preserved over call:
    // - RBP holds result ptr
    __ move8(RCX,RAX);          // Restore methodRef in RCX
    __ move8(RDX,R11);          // JEX stack param start area to RDX also

__ call(R08);//Call to method entry

    Label Lretadr(_masm);
    // RAX = return result

    // Break down the return value type, store back the result into
    // the C structure in an appropriate fashion.
    Label is_object, is_64bits, is_32bits, flt, dbl;
    __ move8(RDI,RAX);          // free up RAX; it has shorter encodings
    __ ldz4 (RAX, RSP, call_stub_saved_rty_offset); // RAX = result type
__ cmp1i(RAX,T_OBJECT);
    __ jeq  (is_object);
    __ cmp1i(RAX,T_INT);
    __ jeq  (is_32bits);
    __ cmp1i(RAX,T_FLOAT);      // this is an SSE reg return
    __ jeq  (flt);
    __ cmp1i(RAX,T_DOUBLE);     // this is an SSE reg return
    __ jeq  (dbl);
#ifdef ASSERT
__ cmp1i(RAX,T_LONG);
    __ jeq  (is_64bits);
__ os_breakpoint();
__ bind(is_64bits);
#endif // ASSERT
__ bind(is_object);//special place for ref-store into C structure

    __ st8  (RBP,0,RDI);
    __ add8i(RSP,sizeof(IFrame));
__ pop(RBP);//Restore RBP
__ ret();
    
__ bind(is_32bits);
    __ st4  (RBP,0,RDI);
    __ add8i(RSP,sizeof(IFrame));
__ pop(RBP);//Restore RBP
__ ret();

__ bind(flt);
    __ st4  (RBP,0,F00);
    __ add8i(RSP,sizeof(IFrame));
__ pop(RBP);//Restore RBP
__ ret();

__ bind(dbl);
    __ st8  (RBP,0,F00);
    __ add8i(RSP,sizeof(IFrame));
__ pop(RBP);//Restore RBP
__ ret();

__ patch_branches();
    return_address = Lretadr.abs_pc(_masm);
    return start.abs_pc(_masm);
  }


  //--- gen_lazy_c2i ---------------------------------------------------------
  // Entry point for compiled vtable-stub calls that need a C2I adapter.  We
  // get here from a vtable-stub that is calling a method that has never been
  // called from compiled code before.  All argument registers are full, RDI
  // has the receiver, and R11 has the methodOop (or methodRef if +MultiMapMetaData).
address gen_lazy_c2i(){
    StubCodeMark mark(this, "StubRoutines", "lazy_c2i", frame::runtime_stub_frame_size);
    Label start(_masm);
    if( MultiMapMetaData ) {
      // Clunkily, SharedRuntime::lazy_c2i expects an oop not a ref - but it
      // just as well could handle a ref except that the vtable stub in
      // non-MultiMap mode already stripped R11.
      __ shl8i(R11,64-objectRef::unknown_bits);
      __ shr8i(R11,64-objectRef::unknown_bits);
    }
    uint32_t save_mask = 
      (1<<RDI) | (1<<RSI) | (1<<RDX) | (1<<RCX) | (1<<R08) | (1<<R09) | // arg registers
      (1<<F00) | (1<<F01) | (1<<F02) | (1<<F03) | (1<<F04) | (1<<F05) | (1<<F06) | (1<<F07) | // arg registers
      (1<<RBX) | (1<<RBP) | (1<<R12) | (1<<R13) | (1<<R14) | (1<<R15) | // callee-save regs holding debug info
      0;                        
    __ call_VM_compiled((address)SharedRuntime::lazy_c2i, save_mask, R11, noreg);
    __ jmp8(RAX);
__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_uncommon_trap ----------------------------------------------------
address gen_uncommon_trap(){
    StubCodeMark mark(this, "StubRoutines", "uncommon_trap", frame::runtime_stub_frame_size);
    Label start(_masm);
    // Uncommon-trap entry point.  Return PC is already on the stack.
    // Callee-save registers hold live debug info.  RDI holds the
    // uncommon-trap cause.
    uint32_t save_mask = -1; // Save them all, so all can hold debug info
    __ call_VM_compiled((address)Deoptimization::uncommon_trap, save_mask, RDI, retadr_arg);

    // RAX points to a 4-byte size of JIT'd frame to pop, then 4 bytes of
    // interpreter frame to copy, then all the frames.

    // There is a return address on the stack (because we CALL'd to this stub).
    __ add8i(RSP, 8);           // Pop return address
    __ ldz4 (RSI, RAX, 0);      // size of JIT'd frame to pop
    __ add8 (RSP, RSI);         // Pop the JIT'd frame
    __ ldz4 (RCX, RAX, 4);      // Size of iframes
    __ sub8 (RSP, RCX);         // Make space on normal stack for IFrames
    __ lea  (RSI, RAX,16);      // Start of IFrame array
    __ ld8  (RAX, RAX, 8);      // Interpreter start address
    __ move8(RDI, RSP);         // Where to copy to...
    __ shr4i(RCX, 3);           // Word count
    __ repeated()->movs8();     // Copy words: jam all the IFrames onto the normal stack

    // Start interpreting!
    __ jmp8 (RAX);

__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_deopt_asm --------------------------------------------------------
address gen_deopt_asm(){
    StubCodeMark mark(this, "StubRoutines", "deoptimize", frame::runtime_stub_frame_size);
    Label start(_masm);
    // Deoptimization entry point.  Return PC is already on the stack.  ALL
    // registers could hold live debug info.  RAX or F00 holds the return
    // value from the call that deoptimized.
    uint32_t save_mask = -1;    // All registers
    __ call_VM_compiled((address)Deoptimization::deoptimize, save_mask, retadr_arg, noreg);

    // RAX points to a 4-byte size of JIT'd frame to pop, then 4 bytes of
    // interpreter frame to copy, then all the frames.

    // There is a return address on the stack (because we CALL'd to this stub).
    __ add8i(RSP, 8);           // Pop return address
    __ ldz4 (RSI, RAX, 0);      // size of JIT'd frame to pop
    __ add8 (RSP, RSI);         // Pop the JIT'd frame
    __ ldz4 (RCX, RAX, 4);      // Size of iframes
    __ sub8 (RSP, RCX);         // Make space on normal stack for IFrames
    __ lea  (RSI, RAX,16);      // Start of IFrame array
    __ ld8  (RAX, RAX, 8);      // Interpreter start address
    __ move8(RDI, RSP);         // Where to copy to...
    __ shr4i(RCX, 3);           // Word count
    __ repeated()->movs8();     // Copy words: jam all the IFrames onto the normal stack

    // Start interpreting!
    __ jmp8 (RAX);

__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_lock -------------------------------------------------------------
  // This code tries various non-blocking ways of acquiring the lock.  It can
  // report failure and is essentially a "try-lock".  No GC (nor any blocking)
  // happens here, so there's no oop-map.  This is just a subroutine to gather
  // up various slightly-less-than fastest-path lock attempts:
  // 1- See of the lock is pre-biased to this thread (already done by caller)
  // 2- See if the lock is unlocked and can be CAS'd (already done by caller)
  // 3- See if there's a heavy monitor and also:
  // 4- - the monitor is biased to this thread
  // 5- - the monitor is unlocked and can be CAS'd
  // 6- - the monitor is locked by this thread (bump recursion count)
  // Does not blow RBX, RCX.
address gen_lock(){
    StubCodeMark mark(this, "StubRoutines", "lock_stub", 8/*ret adr*/);
    Label start(_masm);
    Label hashed, not_monitor, not_owner, self_biased, revoke, got_lock;

    // RAX: markWord pre-loaded; killed
    // R09: shifted thread id
    
    // We assume the caller already checked for self-biased or unlocked.
    // Check for a heavy monitor.
    __ test1i( RAX, 1 );        // Check low bit: set means hash code
    __ jne ( hashed );          // Jump NZ - not locked AND must go slow

    // 3- See if there's a heavy monitor
    __ test1i( RAX, 2 );        // Check low bit2: set means monitor code
    __ jeq ( not_monitor );     // Not a monitor?  Then it must be the wrong owner

    // RAX has the monitor and bit 1 is set
    __ and4(RAX,RAX);           // Zap high bits; strips off KID bits from markWord; leaves bit 1 set
    // 4- - the monitor is biased to this thread
    __ shr4i(R09,markWord::hash_shift);
    __ cmp4(R09, RAX,-2+AzLock::lock_offset()); // Load owners thread-id
    __ jne (not_owner);                        // Wrong owner

    __ cmp4i(RAX,-2+ObjectMonitor::recursion_offset_in_bytes(),0);
    __ jeq (self_biased);       // Z - locked already
    __ jlt (revoke); // -1 - means we've been requested to revoke the bias

    // Since we own the lock, we can bump the recursion count without
    // an atomic operation.
    __ add4i(RAX,-2+ObjectMonitor::recursion_offset_in_bytes(),1);
    __ xor4 (RAX,RAX);          // Bump recur count & locked
__ bind(self_biased);//Arrive here w/Z set
__ ret();//Flags: Z - locked

    // Not zero, not self (checked by caller).  Not hash (checked above).
    // Could be we can CAS in the monitor's lock word.  We need a few more
    // registers here.
__ bind(not_owner);
__ push(RCX);
__ push(RBX);
    __ move4(RBX,RAX);          // Monitor to RBX

    // Some spinning to see if we can acquire the lock directly in the monitor
    // "shortly".  Try to CAS vs a ZERO lock-word, and if that fails spin a
    // little to see if the lock word changes shortly.
{Label outer_spin;
__ bind(outer_spin);
      __ xor4 (RAX,RAX);        // Expect RAX: 0; Desired R09: TID
      __ locked()->cas8(RBX,-2+ObjectMonitor::lock_offset(),R09);
      __ jeq  (got_lock);       // got the lock via CAS into the monitor

      // CAS failed; spin a little to see if lock gets unlocked.  Do "in
      // cache" spinning: only read.  I assume a 'mfence' does not force any
      // out-of-cache traffic as long as I am only reading.
      __ mov8i(RCX,ReadSpinIterations);
{Label spin_till_zero;
__ bind(spin_till_zero);
        __ mfence();            // be slow on purpose
        __ cmp8i(RBX,-2+ObjectMonitor::lock_offset(),0);
        __ jloopne1(spin_till_zero);// dec rcx; break out of loop if NZ or RCX==0; does not blow flags
      }
      __ jeq  (outer_spin);     // Broke out because _lock word is Z
    }
__ bind(got_lock);//Branch here with Z set or fall into here with Z clear.

__ pop(RBX);//Broke out because spin count ran out; go slow
__ pop(RCX);
__ ret();//Return NZ - not locked, Z for locked
    
    // Not zero, not self (checked by caller).  Not hash (checked above).
    // So must be wrong thread; fail out of here and make caller inflate.
__ bind(not_monitor);
    __ and4(RAX,RAX);           // Return NZ - not locked
__ bind(revoke);//NZ: we must revoke our bias
__ bind(hashed);//NZ: markWord holds hash and needs lock so must inflate
__ ret();//Return NZ - not locked

__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_c2_lock ----------------------------------------------------------
address gen_c2_lock(){
    StubCodeMark mark(this, "StubRoutines", "c2_lock_stub", 8/*ret adr*/);
    Label start(_masm);
    // 'Lock' is treated as a function call by C2, so this whole template
    // follows standard X86 calling conventions.  In particular, RDI is always
    // the Object to lock, and RAX is always clobbered.
    Label locked, slow_path;
    __ ldz4 (RAX,RDI,oopDesc::mark_offset_in_bytes()); // 4 bytes of markWord into RAX
    __ gettid(R09);
    __ shl4i(R09,markWord::lock_bits);
    __ or_4 (RAX,RAX);
    // RAX = 4 bytes of markWord
    // R09 = TID, pre-shifted
    __ jne  (slow_path);        // Locked by other?  Or monitor or hash?
    // Atomically compare RAX(0) vs [RDI+mark_offset], if equal set R09 into [RDI+mark_offset]
    __ locked()->cas4(RDI,oopDesc::mark_offset_in_bytes(),R09);
    __ jne  (slow_path);        // CAS failed; RAX is reloaded
    // CAS worked, return Z set
__ ret();

    // Now try the monitor-path, and also setup RDI/RSI for a SharedRuntime
    // locking call.
__ bind(slow_path);
    __ move8(RSI,RDI);
    __ getthr(RDI);
    __ jmp  (StubRoutines::lock_entry()); // CAS failed; RAX is reloaded

__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_blocking_lock ----------------------------------------------------
  // Called from native wrappers, the oop to be locked is passed on the stack
  // *BELOW* RSP.
address gen_blocking_lock(){
    StubCodeMark mark(this, "StubRoutines", "blocking_lock_stub", frame::runtime_stub_frame_size);
    Label start(_masm);
    // save all regs, TODO optimize
    int savemask = -1;
    __ call_VM_compiled(CAST_FROM_FN_PTR(address, SharedRuntime::monitorenter), savemask, noreg, noreg, noreg, noreg, 1);
__ ret();
__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_unlock -----------------------------------------------------------
  address gen_unlock(bool normal_call_conv) {
    StubCodeMark mark(this, "StubRoutines", "unlock_stub", frame::runtime_stub_frame_size);
    Label start(_masm);
    Label slow_path, biased, unlock;

    // R10: OOP to unlock
    Register Roop = normal_call_conv ? RDI : R10;
    // RCX: markWord from fast_path_unlock
    Register Rmw  = normal_call_conv ? RSI : RCX;
    // RBP, RBX, R12-R15 and RDX, RDI, RSI, R08, R11 are all preserved.
    // Crushes RAX, RCX, R09, R10.
    // This call cannot block nor GC (but it makes a leaf call into the VM to
    // wakeup other threads when it releases the lock.

    // Load low 4 bytes: it's not biased to self (blew that check already) but
    // it might be a monitor.
    __ test1i(Rmw,2);        // Not a monitor?  funky unlock w/hash???
    __ jeq  (slow_path);

    // Loaded a monitor!  Bare monitorexit bytecodes can do illegal
    // unlocking.  Check owner.
    Register RAX_owner_tid = RAX;
    __ ldz4 (RAX_owner_tid, Rmw,-2+AzLock::lock_offset()); // Load owners thread-id
    __ and4i(RAX_owner_tid, (1L<<Thread::reversible_tid_bits)-1); // strip off contention count
    Register R09_my_tid = R09;
    __ gettid(R09_my_tid);      // R09=self-tid
    __ cmp4 (R09_my_tid,RAX);
    __ jne  (slow_path);        // Wrong owner

    // We own it.  See if we are self-biased and can just trip outta here.
    __ cmp4i(Rmw,-2+ObjectMonitor::recursion_offset_in_bytes(),1);
    __ jlt  (biased);           // _recursion < 1?  Must be biased

    // See if we can just lower the recursion count
    __ jeq  (unlock);        // _recursion==1?  Must do offical unlock
    
    __ sub4i(Rmw,-2+ObjectMonitor::recursion_offset_in_bytes(),1);

    // RAX, RCX: blown
__ bind(biased);
__ ret();

    // Attempt to unlock.  If there's no contenders we can just CAS-unlock.
__ bind(unlock);
    __ mov8i(R09,0);            // unlocked (desired result)
    // RAX already holds the TID
    __ locked()->cas8(Rmw, -2+AzLock::lock_offset(), R09);
    __ jeq  (biased);           // Success!

    // Contenders and unlocking: will need to actually do a sem_post and wake
    // up a sleeper.  C calls will preserve RBX, RBP, R12-R15 (and hopefully
    // all the float regs as well).  We also preserve RDI, RSI, RDX, R08, R11.
    // Crushes RAX, RCX, R09, R10 that were args or crushed in this stub.
    // No GC can happen, and we DO pass live oops in callee-save registers.
__ bind(slow_path);
    __ add8i(RSP,-(frame::runtime_stub_frame_size-8));
    __ st8(RSP,frame::xRDX,RDX);
    __ st8(RSP,frame::xRDI,RDI);
    __ st8(RSP,frame::xRSI,RSI);
    __ st8(RSP,frame::xR08,R08);
    __ st8(RSP,frame::xR11,R11);
    // arg0 = object to unlock
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, SharedRuntime::monitorexit), Roop, noreg, noreg, noreg, noreg, noreg);
    __ ld8(RDX,RSP,frame::xRDX);
    __ ld8(RDI,RSP,frame::xRDI);
    __ ld8(RSI,RSP,frame::xRSI);
    __ ld8(R08,RSP,frame::xR08);
    __ ld8(R11,RSP,frame::xR11);
    __ add8i(RSP, (frame::runtime_stub_frame_size-8));
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  //--- gen_finalizer --------------------------------------------------------
  // Call slow-path register finalizer.  Passed in RSI.
address gen_finalizer(){
    StubCodeMark mark(this, "StubRoutines", "register_finalizer", frame::runtime_stub_frame_size);
    Label start(_masm);
    // save all regs, TODO optimize
    int savemask = -1;
    __ call_VM_compiled((address)SharedRuntime::register_finalizer, savemask, RDI, noreg );
__ ret();
__ patch_branches();
    return start.abs_pc(_masm);
  }

  //---------------------------------------------------------------------------
  // Handler for null-pointer exceptions forwarded by the OS
  //
address gen_handler_for_null_ptr_exception(){
    StubCodeMark mark(this, "StubRoutines", "handler_for_null_ptr_exception", frame::runtime_stub_frame_size+8);
    Label start(_masm);

    // We get here returning from the signal handler.  JavaThread->_epc holds
    // the faulting PC.  All registers are live.  Arrange for the faulting PC to be on
    // the stack just as-if the signal handler had pushed it (or equivalently
    // as-if the signal handler just jumped to us from the faulting op).
__ push(RAX);//Start a new frame - save a register, any register
    __ getthr(RAX);
    __ ld8  (RAX,RAX,in_bytes(JavaThread::epc_offset())); // load faulting PC into RAX
    __ xchg (RAX,RSP,0);        // Put faulting PC on stack (in return address slot) & restore RAX

    // GC can happen here, if we are at an inline-cache (we need to construct
    // an official j.l.NPE object).  However, if we are at an inline cache, no
    // oops are in callee-save registers and the caller's args are dead
    // (because the call isn't happening).  If we are not at an inline-cache
    // then all registers could be live but no GC happens - so we only need to
    // save & restore them.

    __ call_VM_compiled((address)StubRoutines::find_NPE_continuation_address, 
                        -1,     // save/restore all registers
                        retadr_arg, noreg);

    // Restore RAX without blowing continuation address.
    // The faulting address is still on the stack.
    __ st8  (RSP,0,RAX);        // Continuation becomes the return address
    __ ld8(RAX, RSP, -(frame::runtime_stub_frame_size-8)+frame::xRAX); // Recover RAX, -8 to account for return address still in frame
__ ret();//And off to the continuation
    
__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- gen_handler_for_unsafe_access ---------------------------------------
  // The following routine generates a subroutine to throw an
  // asynchronous UnknownError when an unsafe access gets a fault that
  // could not be reasonably prevented by the programmer.  (Example:
  // SIGBUS/OBJERR.)
address gen_handler_for_unsafe_access(){
    StubCodeMark mark(this, "StubRoutines", "handler_for_unsafe_access", frame::runtime_stub_frame_size+128);
    Label start(_masm);
    
    // We get here returning from the signal handler.  
    // All registers are live.  
    // JavaThread->_epc holds the faulting PC.  
    // The desired continuation address is the instruction which follows the faulting instruction.
    //
    // Since we MAY be returning to a C leaf routine which establishes no frame of its own,
    // we increase the normal frame size by 128.  On the return, the last slot in the "safe"
    // stack range [rsp..rsp-128), will contain the continuation address and be used for an
    // indirect jump.  
    //
    // While this sounds crummy, the only choices that I see at this point are 
    // *) stomp a stack slot slot which may or may not be in use by a leaf routine.
    // *) stomp a register.      This option is chancy.
    // *) use a stack in the range rsp-128 ... rsp-more.  But this option is racy.
    //

    // *****************************************************************************
    // WARNING:  It is very important we don't execute any instructions that can
    // change the flags register before we save it with the lahf() instruction.
    // Similarly, we can't have any instructions that can modify the flags register
    // after we restore it with the sahf() instruction.
    // *****************************************************************************

    __ lea(RSP,RSP,-(frame::runtime_stub_frame_size + 128));

    // Save all GPR registers
for(int i=0;i<16;i++)
      __ st8(RSP,(31-i)*8,( Register)i);
    // Save all FPR registers
for(int i=16;i<32;i++)
      __ st8(RSP,(31-i)*8,(FRegister)i);
 
    // save eflags into runtime_stub_frame
__ lahf();
    __ st8(RSP, frame::xPAD, RAX);

    // GC can occur here.  The stack must be walkable.  
    // For the interpreter case this should be simple.
    // For a compiled java method with inlined unsafe accesses the oopmap at those access instructions
    // will describe active registers.
    //
    // handle_unsafe_access() returns the address of the next instruction, in RAX,
    // and creates an exception object (walks the stack).  call_VM_compiled() is not used
    // because it resets JavaThread fields to identify this stub as the top java frame...
    // and that breaks the stack walker.
    __ call_VM_leaf(CAST_FROM_FN_PTR(address, handle_unsafe_access), RAX /* need an argument...any reg ok? */);
      
    // Save the npc just above the return PC slot.  
    // Once this frame is popped, The saved npc the last protected stack slot
    // within the intel "safe" stack region (rsp..rsp-127).
    __ st8  ( RSP, frame::xStkArgs, RAX);

    // restore eflags from orignal signal
    __ ld8  ( RAX, RSP, frame::xPAD);
__ sahf();

    // Restore all requested GPR registers
for(int i=0;i<16;i++)
      __ ld8(( Register)i, RSP,(31-i)*8);
    // Restore all requested FPR registers
for(int i=16;i<32;i++)
      __ ld8((FRegister)i, RSP,(31-i)*8);

    __ lea(RSP, RSP, (frame::runtime_stub_frame_size + 128));

    // indirect jump through stack location holding npc, the continuation address.
    __ jmp8 (RSP, -128);  

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- gen_handler_for_nio_protection --------------------------------------
  // The following routine generates a stub routine that addresses nio buffer 
  // protection sigsegv which may require proxy io to mmapped file.  Note that 
  // any "AZUL_SIGNAL_NOT_HANDLED" will crash and burn, as we are no longer within
  // a signal handler...no signal handler of last resort.
address gen_handler_for_nio_protection(){
    StubCodeMark mark(this, "StubRoutines", "handler_for_nio_protection", frame::runtime_stub_frame_size+128);
    Label start(_masm);

#if !defined(AZ_PROXIED)
__ should_not_reach_here("proxied nio handler invoked!?");
__ patch_branches();
#else  // AZ_PROXIED    
    // We get here returning from the signal handler.  
    // All registers are live.  
    // JavaThread holds all arguments for this delayed signal handling...
    // If the handler succeeds, execution resumes in at the original faulting
    // instruction.  Otherwise, the signal handler of last resort is called.
    //
    // Since we MAY be returning to a C leaf routine which establishes no frame of its own,
    // we increase the normal frame size by 128.  On the return, the last slot in the "safe"
    // stack range [rsp..rsp-128), will contain the continuation address and be used for an
    // indirect jump.  
    // While this sounds crummy, the only choices that I see at this point are 
    // *) stomp a stack slot slot which may or may not be in use by a leaf routine.
    // *) stomp a register.      This option is chancy.
    // *) use a stack in the range rsp-128 ... rsp-more.  But this option is racy.
    //

    // *****************************************************************************
    // WARNING:  It is very important we don't execute any instructions that can
    // change the flags register before we save it with the lahf() instruction.
    // Similarly, we can't have any instructions that can modify the flags register
    // after we restore it with the sahf() instruction.
    // *****************************************************************************

    __ lea(RSP,RSP,-(frame::runtime_stub_frame_size + 128));

    // Save all GPR registers
for(int i=0;i<16;i++)
      __ st8(RSP,(31-i)*8,( Register)i);
    // Save all FPR registers
for(int i=16;i<32;i++)
      __ st8(RSP,(31-i)*8,(FRegister)i);

    // save eflags into runtime_stub_frame
__ lahf();
    __ st8(RSP, frame::xPAD, RAX);
 
    // GC can occur here.  The stack must be walkable.  
    // For the interpreter case this should be simple.
    // For a compiled java method with inlined unsafe accesses the oopmap at those access instructions
    // will describe active registers.
    //
    // handle_nio_protection_fault() returns the continuation address, in RAX.
    // The call_VM_compiled() macro is not used because it resets JavaThread 
    // fields to identify this stub as the top java frame...  
    // and that breaks the stack walker.

    __ call_VM_leaf(CAST_FROM_FN_PTR(address, handle_nio_protection_fault), RAX);
      
    // Save the continuation pc just above the return PC slot.  
    // Once this frame is popped, The saved npc the last protected stack slot
    // within the intel "safe" stack region (rsp..rsp-127).
    __ st8  ( RSP, frame::xStkArgs, RAX);

    // restore eflags from orignal signal
    __ ld8  ( RAX, RSP, frame::xPAD);
__ sahf();

    // Restore all requested GPR registers
for(int i=0;i<16;i++)
      __ ld8(( Register)i, RSP,(31-i)*8);
    // Restore all requested FPR registers
for(int i=16;i<32;i++)
      __ ld8((FRegister)i, RSP,(31-i)*8);

    __ lea(RSP, RSP, (frame::runtime_stub_frame_size + 128));

    // indirect jump through stack location holding npc, the continuation address.
    __ jmp8 (RSP, -128);  

__ patch_branches();
    
    // register this stub w nio_fault_handler...
    set_hotspot_nio_handler_callback((address_t) start.abs_pc(_masm));  
#endif // AZ_PROXIED

    return start.abs_pc(_masm);
  }

  // --- gen_prim_arraycopy --------------------------------------------------
  void gen_prim_arraycopy(address *prim_arraycopy1,    address *prim_arraycopy2,    address *prim_arraycopy4,    address *prim_arraycopy8,
                          address *prim_arraycopy1_no, address *prim_arraycopy2_no, address *prim_arraycopy4_no, address *prim_arraycopy8_no) {
    StubCodeMark mark(this, "StubRoutines", "prim_arraycopy", 8/*ret adr*/);
    Label start(_masm);
    // RDI = dst, RSI = src, RDX = len
    // Clobbers the FP registers RAX, RDI, RSI, RDX and flags.
    // Does NOT clobber the other caller-save registers RCX, R08-R11.

    // A short run of JBB2000 shows that nearly all copies (like 99%)
    // are of 64-95 bytes long.  There are plenty even shorter (hence
    // the above tests) but most are in this range.  
    // These entry points are known to not overlap, so a forward-copy works.

    Label Lcopy1,  Lcopy2,  Lcopy4,  Lcopy8, Loop;
    Label          Lcopy2a, Lcopy4a, Lcopy8a,Llong;
    Label   done, Lshort2, Lshort4, Lshort8;
Label Lmemmove;

__ bind(Lcopy1);
    __ test1i(RDI,1);           // Test dest for alignment
    __ jeq  (Lcopy2a);          // 2 byte aligned but unknown size
    __ cmp8i(RDX,1);
    __ jlt  (done);             // short copy is done
    __ movs1();                 // move 1 byte to align dest better
    __ sub8i(RDX,1);
    __ jeq  (done);
__ jmp(Lcopy2a);

    __ align(CodeEntryAlignment);
__ bind(Lcopy4);
    __ shl8i(RDX,2);
__ jmp(Lcopy4a);

    __ align(CodeEntryAlignment);
__ bind(Lcopy8);
    __ shl8i(RDX,3);
__ jmp(Lcopy8a);

    __ align(CodeEntryAlignment);
__ bind(Lcopy2);//HOT String Entrypoint
    __ shl8i(RDX,1);

    // Known 2-byte aligned dest
__ bind(Lcopy2a);
    __ test1i(RDI,3);           // Test dest for alignment
    __ jeq  (Lcopy4a);          // 4 byte aligned but unknown size
    __ cmp8i(RDX,2);
    __ jlt  (Lshort2);          // short copy is done
    __ movs2();                 // move 2 byte to align dest better
    __ sub8i(RDX,2);
    __ jeq  (done);

    // Known 4-byte aligned dest
__ bind(Lcopy4a);
    __ test1i(RDI,7);           // Test dest for alignment
    __ jeq  (Lcopy8a);          // 8 byte aligned but unknown size
    __ cmp8i(RDX,4);
    __ jlt  (Lshort4);          // short copy is done
    __ ldz4 (RAX,RSI,0);        // TODO: play with turning this into a movs4
    __ st4  (    RDI,0,RAX);
    __ add8i(RDI,4);
    __ add8i(RSI,4);
    __ sub8i(RDX,4);
    __ jeq  (done);

    // Known 8-byte aligned dest
__ bind(Lcopy8a);
    __ cmp8i(RDX,8);
    __ jlt  (Lshort8);          // short copy is done
    __ cmp8i(RDX,96);
    __ jge  (Llong);            // Long copy
__ bind(Loop);
    __ ld8  (RAX,RSI,0);        // TODO: play with turning this into a movs8
    __ st8  (    RDI,0,RAX);
    __ add8i(RDI,8);
    __ add8i(RSI,8);
    __ sub8i(RDX,8);
    __ cmp8i(RDX,8);
    __ jge  (Loop);

    // Tail copy
__ bind(Lshort8);
    __ cmp8i(RDX,4);
    __ jlt  (Lshort4);
__ movs4();
    __ sub4i(RDX,4);

__ bind(Lshort4);
    __ cmp8i(RDX,2);
    __ jlt  (Lshort2);
__ movs2();
    __ sub4i(RDX,2);

__ bind(Lshort2);
    __ cmp8i(RDX,1);
    __ jlt  (done);
__ movs1();

__ bind(done);
    __ mov8i(RAX, 0);           // C2 stubs expect RAX to be cleared on successful copies
__ ret();
           

// This version gets the length-to-copy aligned, but copies to misaligned dest.
//    Label Lcopy1, Lcopy2, Lcopy4, Lcopy8, Lcopy16, Lcopy32, Lcopy64, Lcopy128, done;
//    Label Lcopy1a,Lcopy2a,Lcopy4a;
//    __ bind (Lcopy1);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy2);
//    __ movs1();
//
//    __ align(CodeEntryAlignment);
//    __ bind (Lcopy2);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy4);
//    __ movs2();
//
//    __ bind (Lcopy4);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy8);
//    __ movs4();
//
//    __ bind (Lcopy8);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy16);
//    __ movs8();
//
//    __ bind (Lcopy16);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy32);
//    __ movs8();
//    __ movs8();
//
//    __ bind (Lcopy32);
//    __ jeq  (done);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy64);
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//
//    __ bind (Lcopy64);
//    __ jeq  (done);
//    __ shr4i(RDX,1);
//    __ jnb  (Lcopy128);
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//    __ movs8();
//
//    __ bind (Lcopy128);
//    __ shl4i(RDX, 7); // RDX = RDX * 128 - count in bytes for copy
//    __ jeq  (done);   // most copies won't be longer than 127bytes

__ bind(Llong);
__ push(RCX);//Rare case long copy - save more registers.
__ push(R08);//But always clobbering the FP regs because
__ push(R09);//so few people want them saved across arraycopy.
__ push(R10);
__ push(R11);
    __ mov8i(R11, (intptr_t) memcpy);
__ call(R11);
__ pop(R11);
__ pop(R10);
__ pop(R09);
__ pop(R08);
__ pop(RCX);
    __ mov8i(RAX, 0);           // C2 stubs expect RAX to be cleared on successful copies
__ ret();

__ bind(Lmemmove);
__ push(RCX);//Rare case long copy - save more registers.
__ push(R08);//But always clobbering the FP regs because
__ push(R09);//so few people want them saved across arraycopy.
__ push(R10);
__ push(R11);
    __ mov8i(R11, (intptr_t) memmove);
__ call(R11);
__ pop(R11);
__ pop(R10);
__ pop(R09);
__ pop(R08);
__ pop(RCX);
    __ mov8i(RAX, 0);          // C2 stubs expect RAX to be cleared on successful copies
__ ret();

    // The possibly-overlapped entry points
    __ align(CodeEntryAlignment);
    Label Lmove1(_masm);
    __ lea  (RAX,RSI,0,RDX,0);
    __ cmp8 (RAX,RDI);
    __ jlt  (Lcopy1);
    __ lea  (RAX,RDI,0,RDX,0);
    __ cmp8 (RAX,RSI);
    __ jlt  (Lcopy1);
__ jmp(Lmemmove);

    __ align(CodeEntryAlignment);
    Label Lmove2(_masm);
    __ lea  (RAX,RSI,0,RDX,1);
    __ cmp8 (RAX,RDI);
    __ jlt  (Lcopy2);
    __ lea  (RAX,RDI,0,RDX,1);
    __ cmp8 (RAX,RSI);
    __ jlt  (Lcopy2);
    __ shl4i(RDX,1);
__ jmp(Lmemmove);

    __ align(CodeEntryAlignment);
    Label Lmove4(_masm);
    __ lea  (RAX,RSI,0,RDX,2);
    __ cmp8 (RAX,RDI);
    __ jlt  (Lcopy4);
    __ lea  (RAX,RDI,0,RDX,2);
    __ cmp8 (RAX,RSI);
    __ jlt  (Lcopy4);
    __ shl4i(RDX,2);
__ jmp(Lmemmove);

    __ align(CodeEntryAlignment);
    Label Lmove8(_masm);
    __ lea  (RAX,RSI,0,RDX,3);
    __ cmp8 (RAX,RDI);
    __ jlt  (Lcopy8);
    __ lea  (RAX,RDI,0,RDX,3);
    __ cmp8 (RAX,RSI);
    __ jlt  (Lcopy8);
    __ shl4i(RDX,3);
__ jmp(Lmemmove);

__ patch_branches();
    *prim_arraycopy1    = Lmove1.abs_pc(_masm);
    *prim_arraycopy2    = Lmove2.abs_pc(_masm);
    *prim_arraycopy4    = Lmove4.abs_pc(_masm);
    *prim_arraycopy8    = Lmove8.abs_pc(_masm);
    *prim_arraycopy1_no = Lcopy1.abs_pc(_masm);
    *prim_arraycopy2_no = Lcopy2.abs_pc(_masm);
    *prim_arraycopy4_no = Lcopy4.abs_pc(_masm);
    *prim_arraycopy8_no = Lcopy8.abs_pc(_masm);
  }

  // --- gen_object_arraycopy ------------------------------------------------
  // Fast-path oop copy still needs read/write barriers, but is known
  // compatible per-element.  All range checks already done.
  // RSI=src, RDI=dst, RDX=length - blows RAX and RCX
address gen_object_arraycopy(){
    StubCodeMark mark(this, "StubRoutines", "object_arraycopy", 16/*ret adr + saved RBX */);
    Label start(_masm), done, overlapping, forward_loop;
assert(StubRoutines::verify_oop_subroutine_entry()!=NULL,"must be generated before");

    Register RSI_src = RSI;     // source pointer (objectRef*)
    Register RDI_dst = RDI;     // dest pointer (objectRef*)
    Register RDX_cnt = RDX;     // element count
    Register RAX_in  = RAX;     // current read value
    Register RBX_tmp = RBX;
    Register RCX_tmp = RCX;

    __ test4(RDX_cnt,RDX_cnt);  // if count == 0 then we're done
    __ jze  (done);
    __ cmp8 (RSI_src,RDI_dst);  // check for different & not-overlapping arrays
    __ jeq  (done);
__ push(RBX_tmp);
    __ jgt  (forward_loop);     // source ahead of dst, so forwards copy

    __ lea  (RCX_tmp, RSI_src,0,RDX_cnt,3); // RCX_tmp=end of source area
    __ cmp8 (RCX_tmp, RDI_dst);
    __ jgt  (overlapping);      // end of source BEFORE start of dest, must not overlap

__ bind(forward_loop);
    { Label store_null, loop(_masm);
      __ lods8();                 // RAX_in=*(RSI_src++)
      __ null_chk(RAX_in,store_null); // Easy shortcut for copying null
      __ lvb  (RInOuts::a, RAX_in, RCX_tmp, RKeepIns::a, RSI_src, -8, false/*can't be null*/);
      __ verify_oop(RAX_in, MacroAssembler::OopVerify_Store);
      __ pre_write_barrier_HEAP(RInOuts::a, RCX_tmp, RBX_tmp, // temps
                              RKeepIns::a, RDI_dst, RAX_in);
      if( RefPoisoning ) __ always_poison(RAX_in);
__ bind(store_null);
      __ stos8();                 // *(RDI_dst++)=RAX_in;
      __ dec4 (RDX_cnt);
__ jgt(loop);
    }
__ pop(RBX_tmp);//restore RBX
__ bind(done);
    __ mov8i(RAX, 0);            // RAX = 0 for the benefit of sharing with C2 stubs
__ ret();

__ bind(overlapping);
    __ std  ();                 // backwards direction
    __ lea  (RSI_src, RSI_src, -8, RDX_cnt, 3); // go to end of arrays
    __ lea  (RDI_dst, RDI_dst, -8, RDX_cnt, 3);

    { Label store_null, loop(_masm);
      __ lods8();               // RAX_in=*(RSI_src--)
      __ null_chk(RAX_in,store_null); // Easy shortcut for copying null
      __ lvb  (RInOuts::a, RAX_in, RCX_tmp, RKeepIns::a, RSI_src, 8, false/*can't be null*/);
      __ verify_oop(RAX_in, MacroAssembler::OopVerify_Store);
      __ pre_write_barrier_HEAP(RInOuts::a, RCX_tmp, RBX_tmp, // temps
                              RKeepIns::a, RDI_dst, RAX_in);
      if( RefPoisoning ) __ always_poison(RAX_in);
__ bind(store_null);
      __ stos8();               // *(RDI_dst--)=RAX_in;
      __ dec4 (RDX_cnt);
__ jgt(loop);
     }
__ cld();
__ pop(RBX_tmp);//restore RBX
    __ mov8i(RAX, 0);            // RAX = 0 for the benefit of sharing with C2 stubs
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- gen_checkcast_arraycopy ---------------------------------------------
address gen_checkcast_arraycopy(){
    StubCodeMark mark(this, "StubRoutines", "checkcast_arraycopy", 24/*ret adr+saved RBX+saved incoming count*/);
assert(StubRoutines::verify_oop_subroutine_entry()!=NULL,"must be generated before");
    Label start(_masm), done, loop, fail, ok_to_store, store_null, do_fast_copy;
    // Do the copy, including a fast per-oop check that: *(oop+supercheck_offset)==superkid.
    // Return 0 that all is OK, or -1^(# of elements copied)
    // Standard C2/C calling conventions: dst_off/src/num_elements/supercheck_offset/superkid/dst_ary_base
    // Args:
    Register RDI_off = RDI;     // offset into dst_ary_base, scaled down to elements and including header (ie for a 2 word header, the 0th element is passed as 2)
    Register RSI_src = RSI;     // derived oop-ptr source
    Register RDX_cnt = RDX;     // element count
    Register RCX_sco = RCX;     // supercheck_offset, used to index into subklass
    Register R08_kid = R08;     // expected superklass KID
    Register R09_ary = R09;     // destination array base, used for spaceid checks
    // Temps:
    Register RAX_in  = RAX;     // incoming oop to store
    Register RBX_tmp = RBX;
    Register R10_tmp = R10;
    Register R11_in_klass = R11;// klass of incoming oop

    __ test4(RDX_cnt,RDX_cnt);  // Zero-trip-test
    __ jeq  (done);             // zero to copy
    __ cmp4i(R09_ary, objArrayOopDesc::ekid_offset_in_bytes(), java_lang_Object_kid);
    __ jeq  (do_fast_copy);
__ push(RBX_tmp);//Need another register
__ push(RDX_cnt);//Need another register; save the original count for exit math
    __ add4 (RDX_cnt,RDI_off);  // Set RDX to the loop limit

__ bind(loop);
    __ lods8();                 // RAX_in=*(RSI_src++)
    __ null_chk(RAX_in, store_null); // Easy shortcut for copying null
    __ lvb  (RInOuts::a, RAX_in, R10_tmp, RKeepIns::a, RSI_src, -8, false/*can be null*/);
    __ verify_oop(RAX_in, MacroAssembler::OopVerify_Store);
    __ ref2klass(RInOuts::a,R11_in_klass,R10_tmp,RBX_tmp,RKeepIns::a,RAX_in); // Load element klass into R11
    __ cmp4 (R08_kid,R11_in_klass,0,RCX_sco,0); // Compare kid in R08 vs *(Rsubklass+RCX_supercheck_offset)
    __ jne  (fail);             // oops, kid mis-match
    __ pre_write_barrier(RInOuts::a, RBX_tmp, R10_tmp, // temps
                         RKeepIns::a, R09_ary, 0, RDI_off, 3, RAX_in,
                         ok_to_store);
    if( UseSBA ) __ jmp(fail); // SBA escape, with array-copy only partially done

__ bind(ok_to_store);
    if( RefPoisoning ) __ always_poison(RAX_in);
__ bind(store_null);
    __ st8  (R09_ary,0,RDI_off,3,RAX_in);
    __ inc4 (RDI_off);          // Bump store index
    __ cmp4 (RDI_off,RDX_cnt);  // Check against loop limit
__ jlt(loop);
__ pop(RAX);//Toss saved value, everything copied fine
__ pop(RBX_tmp);

__ bind(done);
    __ mov8i(RAX,0);            // Everything copied fine
__ ret();

__ bind(do_fast_copy);//Doing kid checks is overly pessimistic, go to fast copy
    { // RSI == source (already computed), RDX == count (already computed)
      // RDI == dest => compute
      Register RDI_to = RDI;
      __ lea(RDI_to, R09_ary, 0, RDI_off, 3);  // dst_addr
      __ jmp(StubRoutines::_object_arraycopy); // go to fast object arraycopy
    }

__ bind(fail);//Failed quick KID check or SBA
__ pop(RAX);
__ pop(RBX_tmp);
    __ sub4 (RDX_cnt,RAX);      // Compute original RDI
    __ sub4 (RDI_off,RDX);      // Compute elements copied
    __ not4 (RDI);              // XOR elements copied
    __ move4(RAX,RDI);
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // Perform range checks on the proposed arraycopy.
  // Kills temp, but nothing else.
  // Also, clean the sign bits of src_pos and dst_pos.
void arraycopy_range_checks(Register Rsrc,//source array oop (RDI)
                              Register Rsrc_pos, // source position  (RSI)
                              Register Rdst,     // destination array oo (RDX)
                              Register Rdst_pos, // destination position (RCX)
Register Rlength,
Register Rtemp,
                              Label& L_failed) {
    //  if (src_pos + length > arrayOop(src)->length())  FAIL;
    __ lea4(Rtemp, Rlength, 0, Rsrc_pos, 0); // temp = src_pos + length
    __ cmp4(Rtemp, Rsrc, arrayOopDesc::length_offset_in_bytes());
__ jab(L_failed);

    //  if (dst_pos + length > arrayOop(dst)->length())  FAIL;
    __ lea4(Rtemp, Rlength, 0, Rdst_pos, 0); // temp = dst_pos + length
    __ cmp4(Rtemp, Rdst, arrayOopDesc::length_offset_in_bytes());
__ jab(L_failed);

#ifdef ASSERT
    Label L_good, L_fail;
    // Make sure high 32-bits of 'src_pos' and 'dst_pos' are clean
    __ mov8 (Rtemp, Rsrc_pos);
    __ shr8i(Rtemp, 32);
    __ test4(Rtemp, Rtemp);
    __ jze  (L_good);
__ bind(L_fail);
__ stop("expected high 32bits of register to be clear in arraycopy range check");
__ bind(L_good);
    __ mov8 (Rtemp, Rdst_pos);
    __ shr8i(Rtemp, 32);
    __ test4(Rtemp, Rtemp);
    __ jnz  (L_fail);
#endif
  }

  //
  //  Generate generic array copy stubs
  //
  //  Input:
  //    RDI    -  src oop
  //    RSI    -  src_pos (32-bits)
  //    RDX    -  dst oop
  //    RCX    -  dst_pos (32-bits)
  //    R08    -  element count (32-bits)
  //
  //  Output:
  //    RAX ==  0  -  success
  //    RAX == -1^K - failure, where K is partial transfer count
  //
  address generate_generic_copy(const char *name) {

Label L_failed,L_success;
    Label L_copy_bytes, L_copy_ints_or_longs, L_copy_longs;
    Label L_objectarray_copies, L_checkcast_arraycopy;

    // Input registers
const Register RDI_src=RDI;//source array oop
const Register RSI_src_pos=RSI;//source position
const Register RDX_dst=RDX;//destination array oop
const Register RCX_dst_pos=RCX;//destination position
    const Register R08_length     = R08;  // length
    const Register RAX_tmp        = RAX;

    { int modulus = CodeEntryAlignment;
      int target  = modulus - 5; // 5 = sizeof jmp(L_failed)
      int advance = target - (__ offset() % modulus);
      if (advance < 0)  advance += modulus;
      if (advance > 0)  __ nop(advance);
    }
StubCodeMark mark(this,"StubRoutines",name,8/*ret adr*/);

    //assert(__ offset() % CodeEntryAlignment == 0, "no further alignment needed");

    __ align(CodeEntryAlignment);
    Label start(_masm);

    //-----------------------------------------------------------------------
    // Assembler stub will be used for this call to arraycopy 
    // if the following conditions are met:
    // 
    // (1) src and dst must not be null.
    // (2) src_pos must not be negative.
    // (3) dst_pos must not be negative.
    // (4) length  must not be negative.
    // (5) src klass and dst klass should be the same and not NULL.
    // (6) src and dst should be arrays.
    // (7) src_pos + length must not exceed length of src.
    // (8) dst_pos + length must not exceed length of dst.
    // 

    //  if (src == NULL) return -1;
    __ null_chk(RDI_src, L_failed); // src oop

    //  if (dst == NULL) return -1;
    __ null_chk(RDX_dst, L_failed); // dst oop

    //  if ((src_pos < 0) || (dst_pos < 0) || (length < 0)) return -1;
    __ mov4(RAX_tmp, RSI_src_pos);
    __ or_4(RAX_tmp, RCX_dst_pos);
    __ or_4(RAX_tmp, R08_length);
__ jsg(L_failed);

    // if src_pos or dst_pos plus length would result in an AIOBE fail
    arraycopy_range_checks(RDI_src, RSI_src_pos, RDX_dst, RCX_dst_pos, R08_length,
RAX_tmp,L_failed);

    // Get destination klass and kid, klass for layout helper, kid for compatibility with src array for prim arrays
    const Register R09_dst_kid   = R09; // src array kid
    const Register R10_dst_klass = R10; // src array klass

    __ ref2kid(R09_dst_kid, RDX_dst);
    __ kid2klass(RInOuts::a, R10_dst_klass, RAX_tmp, RKeepIns::a, R09_dst_kid);
#ifdef ASSERT
    //  assert(src->klass() != NULL);
{Label L1;
      __ test8(R10_dst_klass, R10_dst_klass);
__ jnz(L1);
      __ stop ("broken null klass");
      __ bind (L1);
    }
#endif

    // Load layout helper (32-bits)
    //
    //  |array_tag|     | header_size | element_type |     |log2_element_size|
    // 32        30    24            16              8     2                 0
    //
    //   array_tag: typeArray = 0x3, objArray = 0x2, non-array = 0x0
    //
    int lh_offset = klassOopDesc::header_size() * HeapWordSize +
      Klass::layout_helper_offset_in_bytes();

const Register RAX_lh=RAX;//layout helper
    const Register R11_src_kid = R11; // src array kid
    __ ld4(RAX_lh, R10_dst_klass, lh_offset);
    __ ref2kid(R11_src_kid, RDI_src);

    // Handle objArrays completely differently...
    jint objArray_lh = Klass::array_layout_helper(T_OBJECT);
__ cmp4i(RAX_lh,objArray_lh);
    __ jeq(L_objectarray_copies);

    //  if (src->klass() != dst->klass()) return -1;
    __ cmp4(R09_dst_kid, R11_src_kid);
__ jne(L_failed);

    //  if (!src->is_Array()) return -1;
__ cmp4i(RAX_lh,Klass::_lh_neutral_value);
__ jge(L_failed);

    // At this point, it is known to be a typeArray (array_tag 0x3).
#ifdef ASSERT
    { Label L;
__ cmp4i(RAX_lh,(Klass::_lh_array_tag_type_value<<Klass::_lh_array_tag_shift));
__ jge(L);
      __ stop("must be a primitive array");
      __ bind(L);
    }
#endif

    // the copy will succeed. if length == 0 then go to a quick exit route
    __ test4(R08_length, R08_length);
__ jze(L_success);

    // typeArrayKlass
    //
    // src_addr = (src + array_header_in_bytes()) + (src_pos << log2elemsize);
    // dst_addr = (dst + array_header_in_bytes()) + (dst_pos << log2elemsize);
    //

    const Register RAX_elsize = RAX_lh; // array element size
    assert0( Klass::_lh_log2_element_size_shift == 0 );
__ and4i(RAX_elsize,Klass::_lh_log2_element_size_mask);//RAX_lh -> RAX_elsize

     // next registers should be set before the jump to corresponding stub
const Register RSI_from=RSI;//source array address
const Register RDI_to=RDI;//destination array address
const Register RDX_count=RDX;//elements count

    // 'from', 'to', 'count' registers should be set in such order
    // since they are the same as 'src', 'src_pos', 'dst'.

__ cmp4i(RAX_elsize,LogBytesPerShort);
__ jlt(L_copy_bytes);
    __ jgt (L_copy_ints_or_longs);

    // fall through => copy shorts (common case due to lots of use of chars)
    { int header_size = arrayOopDesc::base_offset_in_bytes(T_SHORT);
      __ lea(RSI_from, RDI_src, header_size, RSI_src_pos, 1); // src_addr
      __ lea(RDI_to,   RDX_dst, header_size, RCX_dst_pos, 1); // dst_addr
      __ mov4(RDX_count, R08_length);               // length
      __ jmp(StubRoutines::x86::_prim_arraycopy2);
    }

__ bind(L_copy_bytes);
#ifdef ASSERT
    { Label L;
      __ cmp4i(RAX_elsize, 0);
__ jeq(L);
__ stop("must be byte copy, but elsize is wrong");
      __ bind(L);
    }
#endif
    { int header_size = arrayOopDesc::base_offset_in_bytes(T_BYTE);
      __ lea(RSI_from, RDI_src, header_size, RSI_src_pos, 0); // src_addr
      __ lea(RDI_to,   RDX_dst, header_size, RCX_dst_pos, 0); // dst_addr
      __ mov4(RDX_count, R08_length);               // length
      __ jmp(StubRoutines::x86::_prim_arraycopy1);
    }

__ bind(L_copy_ints_or_longs);
__ cmp4i(RAX_elsize,LogBytesPerInt);
__ jne(L_copy_longs);
    { int header_size = arrayOopDesc::base_offset_in_bytes(T_INT);
      __ lea(RSI_from, RDI_src, header_size, RSI_src_pos, 2); // src_addr
      __ lea(RDI_to,   RDX_dst, header_size, RCX_dst_pos, 2); // dst_addr
      __ mov4(RDX_count, R08_length);               // length
      __ jmp(StubRoutines::x86::_prim_arraycopy4);
    }

__ bind(L_copy_longs);
#ifdef ASSERT
    { Label L;
__ cmp4i(RAX_elsize,LogBytesPerLong);
__ jeq(L);
      __ stop("must be long copy, but elsize is wrong");
      __ bind(L);
    }
#endif
    { int header_size = arrayOopDesc::base_offset_in_bytes(T_LONG);
      __ lea(RSI_from, RDI_src, header_size, RSI_src_pos, 3); // src_addr
      __ lea(RDI_to,   RDX_dst, header_size, RCX_dst_pos, 3); // dst_addr
      __ mov4(RDX_count, R08_length);               // length
      __ jmp(StubRoutines::x86::_prim_arraycopy8);
    }

__ bind(L_objectarray_copies);
    // RDI = src, RSI = src_pos, RDX = dst, RCX = dst_pos, R08 = length
    // R09 = dst kid, R10 = dst klass, R11 = src kid, RAX = dst klass layout helper
    // Are object arrays trivially compatible?
    __ cmp4(R09_dst_kid, R11_src_kid);
    __ jne(L_checkcast_arraycopy);

    Label L_fast_arraycopy(_masm);
    { int header_size = arrayOopDesc::base_offset_in_bytes(T_OBJECT);
      __ lea(RSI_from, RDI_src, header_size, RSI_src_pos, 3); // src_addr
      __ lea(RDI_to,   RDX_dst, header_size, RCX_dst_pos, 3); // dst_addr
      __ mov4(RDX_count, R08_length);                         // length
      __ jmp(StubRoutines::_object_arraycopy);    // go to fast object arraycopy
    }

    // We're here with a potentially incompatible array copy
__ bind(L_checkcast_arraycopy);
    // A copy into Object[] is always safe
    __ cmp4i(RDX_dst, objArrayOopDesc::ekid_offset_in_bytes(), java_lang_Object_kid);
    __ jeq  (L_fast_arraycopy);
    { // Set up for full checkcast array copy that checks for potential array store exception on each store
__ untested("checkcast arraycopy from generic");
      // check we have an object array as src - RAX == object array layout helper from dst array's klass
      Register R10_src_klass = R10;     // register holding source array's klass
      Register R09_tmp       = R09;     // temp
      __ kid2klass(RInOuts::a, R10_src_klass, R09_tmp, RKeepIns::a, R11_src_kid);
      __ cmp4(RAX_lh /*dst layout helper*/, R10_src_klass, lh_offset);
__ jne(L_failed);
      // load sco and kid, marshall arguments
      // Extra outgoing args to RSI_from, RSI_to and RDX_count
      const Register RCX_sco  = RCX;    // supercheck_offset, used to index into subklass
      const Register R08_ekid = R08;    // expected superklass KID
      const Register R09_ary  = R09;    // destination array base, used for spaceid checks
      // Temps:
      const Register RDX_dst_ekid   = RDX;
      const Register RAX_dst_eklass = RAX;
      const Register R10_tmp        = R10;
      
      __ mov8(R09_ary, RDX_dst);                                // destination array
int header_size=arrayOopDesc::base_offset_in_bytes(T_OBJECT);
      __ lea (RSI_from, RDI_src, header_size, RSI_src_pos, 3);  // src_addr
      __ lea (RDI_to, RCX_dst_pos, header_size >> 3, noreg, 0); // dst index scaled over header
      // compute ekid in RDX and then swap with count in R08_length below
      __ ldz4(RDX_dst_ekid, RDX_dst, objArrayOopDesc::ekid_offset_in_bytes());
      __ kid2klass(RInOuts::a, RAX_dst_eklass, R10_tmp, RKeepIns::a, RDX_dst_ekid);
      int sco_offset = Klass::super_check_offset_offset_in_bytes() + sizeof(oopDesc);
      __ ldz4(RCX_sco, RAX_dst_eklass, sco_offset);
      __ xchg4(RDX_dst_ekid, R08_length);         // R08 == ekid, RDX == count
      __ jmp(StubRoutines::_checkcast_arraycopy); // go to arraycopy with checkcasts
    }

__ bind(L_failed);
    __ mov8i(RAX, -1);
__ ret();

__ bind(L_success);
    __ mov8i(RAX, 0);
__ ret();
__ patch_branches();
    return start.abs_pc(_masm);
  }  

  // --- gen_partial_subtype_check -------------------------------------------
  // Return ZERO RAX if IS subtype, NZ RAX if NOT subtype
address gen_partial_subtype_check(){
    StubCodeMark mark(this, "StubRoutines", "partial_subtype_check", frame::runtime_stub_frame_size);
    Label start(_masm), restore_and_return;

    Register RAX_superklass = RAX; // incoming arg (stripped oop) and result
    Register R09_subklass   = R09; // incoming arg (stripped oop)
    Register RBX_tmp2       = RBX; // preserved temp
    Register RCX_count      = RCX; // preserved temp - must be RCX
    Register RDX_oop_array  = RDX; // preserved temp
    Register RSI_tmp        = RSI; // preserved temp

    __ add8i(RSP,-(frame::runtime_stub_frame_size-8));
    // save clobbered temps
    __ st8(RSP,frame::xRBX,RBX);
    __ st8(RSP,frame::xRCX,RCX);
    __ st8(RSP,frame::xRDX,RDX);
    __ st8(RSP,frame::xRSI,RSI);

    // Load oop array and oop array.length
    __ ldref_lvb(RInOuts::a, RDX_oop_array, RSI_tmp, RKeepIns::a, R09_subklass, sizeof (oopDesc) + Klass::secondary_supers_offset_in_bytes(), false);
    __ cvta2va (RDX_oop_array);
    __ ldz4    (RCX_count, RDX_oop_array, arrayOopDesc::length_offset_in_bytes()); // get the length from the array

    // Loop over array finding desired oop
    __ find_oop_in_oop_array(RKeepIns::a, RAX_superklass, RInOuts::a, RDX_oop_array, RCX_count, RBX_tmp2, RSI_tmp, restore_and_return);

    // found subtype, cache super class's kid in subklass
    Register RAX_superkid = RAX_superklass;
    __ ldz4 (RAX_superkid, RAX_superklass, sizeof (oopDesc) + Klass::klassId_offset_in_bytes());
    __ st4  (R09_subklass, sizeof (oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes(), RAX_superkid);
    __ mov8i(RAX, 0);           // success!

__ bind(restore_and_return);
    // restore registers
    __ ld8(RSI, RSP,frame::xRSI);
    __ ld8(RDX, RSP,frame::xRDX);
    __ ld8(RCX, RSP,frame::xRCX);
    __ ld8(RBX, RSP,frame::xRBX);
    // pop frame and return
    __ add8i(RSP, (frame::runtime_stub_frame_size-8)); 
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- gen_full_subtype_check -------------------------------------------
  // Return ZERO RAX if IS subtype, NZ RAX if NOT subtype
address gen_full_subtype_check(){
    StubCodeMark mark(this, "StubRoutines", "full_subtype_check", frame::runtime_stub_frame_size);
    Label start(_masm), sco_check, sco_fail, supers_fail;

    //  assumed performed in caller - trivial equality test => if (obj.kid == T.kid) goto done
    //  if (S[T.super_check_offset] == T.kid) goto done
    //  if (T.super_check_offset != secondary_super_cache_offset) goto fail
    //  partial subtype check - search secondary super cache for T
    //  done: return !0
    //  fail: return 0

    Register R11_sub_kid         = R11; // kid of thing we're checking
    Register RAX_super_kid       = RAX; // kid of class we we're checking sub against and result
    Register RBX_sub_klass       = RBX; // preserved reg holding sub klass
    Register RCX_super_check_off = RCX; // preserved reg holding super check offset
    Register RDX_super_klass     = RDX; // preserved reg holding super klass
    Register RDI_tmp             = RDI; // preserved temp

    __ add8i(RSP,-(frame::runtime_stub_frame_size-8));
    // save clobbered temps
    __ st8(RSP,frame::xRBX,RBX);
    __ st8(RSP,frame::xRCX,RCX);
    __ st8(RSP,frame::xRDX,RDX);
    __ st8(RSP,frame::xRDI,RDI);

    __ kid2klass(RInOuts::a, RDX_super_klass, RDI_tmp, RKeepIns::a, RAX_super_kid);
    __ cvta2va (RDX_super_klass);
    __ kid2klass(RInOuts::a, RBX_sub_klass, RDI_tmp, RKeepIns::a, R11_sub_kid);
    __ cvta2va (RBX_sub_klass);

    // 1) Is super's KID in subklass's cache?
    __ ldz4    (RCX_super_check_off, RDX_super_klass, sizeof(oopDesc) + Klass::super_check_offset_offset_in_bytes());
    __ cmp4    (RAX_super_kid, RBX_sub_klass, 0, RCX_super_check_off, 0);
    __ jne     (sco_check); // Not a hit then go to next test

    __ mov8i(RAX, 0);           // success!
    // restore registers
    __ ld8(RDI, RSP,frame::xRDI);
    __ ld8(RDX, RSP,frame::xRDX);
    __ ld8(RCX, RSP,frame::xRCX);
    __ ld8(RBX, RSP,frame::xRBX);
    // pop frame and return
    __ add8i(RSP, (frame::runtime_stub_frame_size-8));
__ ret();

    // 2) Did we check in the direct subtype list or in the secondary list's cache?
__ bind(sco_check);
    __ cmp4i  (RCX_super_check_off, (sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes()));
    __ jne    (sco_fail);
    // 3) Slow path, loop over secondary supers array to find hit
    // Load secondary supers oop array and oop array.length
    Register RCX_count            = RCX;  // live range doesn't overlap with Rsuper_check_off
    Register RSI_supers_oop_array = RSI;
    __ st8(RSP,frame::xRSI,RSI); // free RSI
    __ st4(RSP,frame::xRAX,RAX); // free RAX
    Register RAX_tmp = RAX;
    __ ldref_lvb(RInOuts::a, RSI_supers_oop_array, RAX_tmp, RKeepIns::a, RBX_sub_klass, sizeof (oopDesc) + Klass::secondary_supers_offset_in_bytes(), false);
    __ cvta2va (RSI_supers_oop_array);
    __ ldz4    (RCX_count, RSI_supers_oop_array, arrayOopDesc::length_offset_in_bytes()); // get the length from the array

    // Loop over array finding desired oop
    __ find_oop_in_oop_array(RKeepIns::a, RDX_super_klass, RInOuts::a, RSI_supers_oop_array, RCX_count, RAX_tmp, RDI_tmp, supers_fail);
    // found subtype, cache super class's kid in subklass
    __ ldz4 (RAX_super_kid, RSP,frame::xRAX); // restore RAX clobbered as a temp
    __ st4  (RBX_sub_klass, sizeof (oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes(), RAX_super_kid);
    __ mov8i(RAX, 0);           // success!
    // restore registers
__ bind(supers_fail);
    __ ld8(RSI, RSP,frame::xRSI);
__ bind(sco_fail);//here to avoid a predicted backward branch
    __ ld8(RDI, RSP,frame::xRDI);
    __ ld8(RDX, RSP,frame::xRDX);
    __ ld8(RCX, RSP,frame::xRCX);
    __ ld8(RBX, RSP,frame::xRBX);
    // pop frame and return
    __ add8i(RSP, (frame::runtime_stub_frame_size-8));
__ ret();

__ patch_branches();
    return start.abs_pc(_masm);
  }

  // --- gen_sba_escape_handler -------------------------------------------
address gen_sba_escape_handler(){
    StubCodeMark mark(this, "StubRoutines", "sba_escape_handler", frame::runtime_stub_frame_size);
    assert(StubRoutines::forward_exception_entry() != NULL, "must be generated before");
    Label start(_masm);
    __ call_VM_compiled(CAST_FROM_FN_PTR(address, SharedRuntime::sba_escape),-1,noreg,noreg,noreg,noreg,2);
__ ret();
__ patch_branches();
    return start.abs_pc(_masm);
  }
};

CodeBlob *StubGenerator_generate(bool all) {
  StubGenerator g(all);
  // Only resolve_and_patch_call needs a frame, so set the framesize for all
  // these stubs to be just the one for r&p.
  g.assembler()->bake_into_CodeBlob(frame::runtime_stub_frame_size);
  return g.assembler()->blob();
}

static bool instrumented_stub_installed = false;

void StubGenerator_install_instrumented_stubs() {
  if (instrumented_stub_installed) {
    return;
  }

  // no equivalent code in JavaSoft, we assume all necessary paths to JVMTI
  // exist in the VM
  //Unimplemented();

  instrumented_stub_installed = true;
}

// --- gen_safepoint_trap_handler --------------------------------------------
// X86 does not have a special safepoint-checking instruction, so this code is
// explicitly called.
address StubGenerator::gen_safepoint_trap_handler() {
  StubCodeMark mark(this, "StubRoutines", "safepoint_handler", frame::runtime_stub_frame_size);
  assert (StubRoutines::forward_exception_entry() != NULL, "must be generated before");  
  Label start(_masm);
  // Save all registers... AND GC them all!  The GC/stack crawl explicitly
  // understands this frame style and will GC oops in registers.  So NO
  // registers are blown here, and also OOPs are allowed.
  __ call_VM_compiled(CAST_FROM_FN_PTR(address, JavaThread::poll_at_safepoint_static), -1, noreg, noreg);
__ ret();
  
__ patch_branches();
  return start.abs_pc(_masm);
}


address StubGenerator::generate_c1_profile_callee() {
  StubCodeMark mark(this, "StubRoutines", "c1_profile_callee", frame::runtime_stub_frame_size);
  Label start(_masm), null_rcv;
  // Record information about the callee in the caller's code profile
  // NOTE!  This code is racy, but it's collecting statistics, so it's okay
  // Inputs:
  Register R11_cpd = R11;   // CPData_Invoke
  Register RAX_kid = RAX;   // killed temp
  Register RDI_rcv = RDI;   // Receiver

#ifdef CPMAGIC
Label okaymagic;
  // Count better be 0
  __ cmp1i(R11_cpd, (address)CPData_Invoke::magic_offset(), CPData::_magicbase | 0x3);
  __ jeq  (okaymagic);
__ stop("Baaaad magic");
__ bind(okaymagic);
#endif
  __ null_chk(RDI_rcv, null_rcv);
  __ ref2kid(RAX_kid, RDI_rcv);
  
  // Create histogram.  Take next available slot for a new klass id.  If slots
  // are all filled, a new klass id is dropped.  Unrolled in the MacroAssembler.
for(int i=0;i<CPData_Invoke::NumCalleeHistogramEntries;i++){
    Label iskid, notkid;
    // If oldkid == kid, add to the count
    __ cmp4 (RAX_kid, R11_cpd, CPData_Invoke::callee_histogram_klassids_offset()+(i<<2));
    __ jeq  (iskid);
    // If oldkid == 0, put down kid and put count to 1
    __ cmp4i(R11_cpd, CPData_Invoke::callee_histogram_klassids_offset()+(i<<2), 0);
    __ jne  (notkid);           // wrong KID, try again
    // Found empty slot; fill it
    __ st4  (R11_cpd, CPData_Invoke::callee_histogram_klassids_offset()+(i<<2), RAX_kid);
__ bind(iskid);//Hit on an existing kid; bump count and return
    __ inc4 (R11_cpd, CPData_Invoke::callee_histogram_count_offset   ()+(i<<2));
__ ret();
__ bind(notkid);
  }
  // If we're here, we did not find the klass id and there are no empty slots
  // left -> add to the overflow count
  __ inc4 (R11_cpd, CPData_Invoke::callee_histogram_num_overflowed_offset());
__ ret();

  // Got here on a null receiver
__ bind(null_rcv);
  __ or_4i(R11_cpd, CPData_Null::bitdata_offset(), 1);
__ ret();
  
__ patch_branches();
  return start.abs_pc(_masm);
}

