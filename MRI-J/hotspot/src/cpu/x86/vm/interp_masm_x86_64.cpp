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


#include "gpgc_layout.hpp"
#include "interp_masm_pd.hpp"
#include "interpreter.hpp"
#include "interpreterRuntime.hpp"
#include "jvmtiExport.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
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

// Implementation of InterpreterMacroAssembler

// Make/setup a codelet
InterpreterCodelet *InterpreterMacroAssembler::make_codelet(const char* desc, Bytecodes::Code bc) {
  // Make space before placed-new to allocate
  grow(sizeof(InterpreterCodelet));
  // Allocate codelet in the CodeBlob (placed operator new)
  InterpreterCodelet *ic = new ( pc() ) InterpreterCodelet(desc,bc);
  // Bump pc by that size
  _pc += sizeof(InterpreterCodelet);
  align(CodeEntryAlignment);    // Now align up for code
  return ic;
}

// --- dispatch_prolog
// Setup for bytecode dispatch done in parts.  Mostly just record the total
// bytecode size, so the dispatch parts can find the next bytecode.
void InterpreterMacroAssembler::dispatch_prolog(TosState state, int step) {
assert(state==tos,"no TosStates supported");
  assert0(_dispatch_next_state == DISPATCH_NEXT_RESET);
  _dispatch_next_step = step;   // record bytecode size, so each template does not need to know
}


// --- dispatch_epilog
// Finish up the dispatch.
void InterpreterMacroAssembler::dispatch_epilog(TosState state){
assert(state==tos,"no TosStates supported");
  dispatch_next(_dispatch_next_step);
  _dispatch_next_step = -1;
}

// --- dispatch_next
// Do an entire dispatch right now
void InterpreterMacroAssembler::dispatch_next(int step) {
  // As a convenience, if the template did no dispatch at all, we'll do it here.
  // But assert against partial dispatch.
  if(_dispatch_next_state == DISPATCH_NEXT_RESET ) {
    _dispatch_next_step = step;
    dispatch_next_0(RInOuts::a,RAX_TMP);
    dispatch_next_1(RInOuts::a,RAX_TMP);
    dispatch_next_2(RInOuts::a,RAX_TMP);
  } else {
    assert(_dispatch_next_state == DISPATCH_NEXT_2, "scheduling of dispatch_next incomplete");
  }
  _dispatch_next_state = DISPATCH_NEXT_RESET;
}

void InterpreterMacroAssembler::dispatch_next_0(RInOuts, Register Rbc) {
  assert (_dispatch_next_state == DISPATCH_NEXT_RESET, "out-of-order scheduling of dispatch_next");
  assert0(_dispatch_next_step != -1 );
  ldz1(Rbc,RSI_BCP,_dispatch_next_step); // load bytecode from bytecode stream
  _dispatch_next_state = DISPATCH_NEXT_0;
  // Rbc is alive from here to dispatch_next_2
}

void InterpreterMacroAssembler::dispatch_next_1(RInOuts, Register Rbc) {
  assert(_dispatch_next_state == DISPATCH_NEXT_0, "out-of-order scheduling of dispatch_next");
  assert0(_dispatch_next_step != -1 );
  ld8(Rbc,noreg,(intptr_t)Interpreter::active_table(),Rbc,3); // Preload target address of next bytecode
  if( _dispatch_next_step != 0 )
    add8i(RSI_BCP,_dispatch_next_step);
  _dispatch_next_state = DISPATCH_NEXT_1;
  // Rbc is alive from here to dispatch_next_2
}

void InterpreterMacroAssembler::dispatch_next_2(RInOuts, Register Rbc) {
  assert(_dispatch_next_state == DISPATCH_NEXT_1, "out-of-order scheduling of dispatch_next");
  // Rbc is alive on entry here with the bytecode address
  jmp8(Rbc);
  _dispatch_next_state = DISPATCH_NEXT_2;
  _dispatch_next_step = -1;
}


void InterpreterMacroAssembler::check_and_handle_popframe(){
  if (JvmtiExport::can_pop_frame()) {
    Label L;

    // Check the "pending popframe condition" flag in the current thread
//    ld4 (tmp, THR, in_bytes(JavaThread::popframe_condition_offset()));
//
//    // Initiate popframe handling only if it is not already being processed.  If the flag
//    // has the popframe_processing bit set, it means that this code is called *during* popframe
//    // handling - we don't want to reenter.
//    bnei (tmp, JavaThread::popframe_pending_bit, L);
//    beqi (tmp, JavaThread::popframe_processing_bit, L);
//
//    // Jump to Interpreter::_remove_activation_preserving_args_entry
//    call_VM_ex(CAST_FROM_FN_PTR(address, Interpreter::remove_activation_preserving_args_entry), IA0);
//    jr   (IA0);
//
//    bind(L);
  }
}


void InterpreterMacroAssembler::store_sentinel(RKeepIns, Register Rslot, int off) {
  st4i  (Rslot, 4+off, frame::double_slot_primitive_type_empty_slot_id);
#ifdef DEBUG
  st4i  (Rslot, 0+off, 0x1452EBAD); // 0x1452EBAD^-1 == 0xEBAD1452
#endif // DEBUG
}


void InterpreterMacroAssembler::single_slot_locals_store_check(RInOuts, Register Rslot, Register Rsentinel, Register Rtmp, int slotnum) {
  assert_different_registers(Rslot,Rsentinel,Rtmp);

Label test_2_complete;

  // Every local store must perform two tests
  // 
  // #1 Is the slot being stored to tagged as a long/double?
  // #2 Is the slot being stored to the *tag* for a long/double?
  //
  // #2 is only run if test #1 fails, that is, indicates the target
  // slot is not a tagged long/double.
  //
  // If test #1 is true, we need to overwrite the previous slot with
  // a zero. This will prevent the current store from being treated like
  // a long/double.
  //
  // If test #1 fails, but test #2 is true, we need to overwrite the
  // following slot with a zero. This will prevent an out of scope long/double
  // from being unmasked, and treated like an oop.

  if( slotnum >= 0 ) {
    // equivalent to locals_index(Rslot)
    lea(Rslot, RDX_LOC, slotnum * reg_size);
  }

  if( slotnum != 0 ) {
    // Test #1
    Label sentinel_counting_loop, test_1_complete, test_1_counting_complete, test_1_complete_with_sentinel;

    // If we are storing to locals 0, there can be no previous sentinel value
    if( slotnum < 0 ) {         // Unknown slot?
      test4(Rslot,Rslot);       // Test Rslot for 0
      lea (Rslot, RDX_LOC, 0, Rslot,3);
      jeq (test_1_complete);    // Test Rslot for 0
    }

    // Peel the hot iteration#1 of this loop.
    cmp4i(Rslot,4-reg_size,frame::double_slot_primitive_type_empty_slot_id);
    jne  (test_1_complete);
    lea  (Rtmp, Rslot, -reg_size);
    cmp8 (Rtmp, RDX_LOC);
    jeq  (test_1_complete_with_sentinel);

    // Count the number of prior sentinel values.
    // Found one already from the above peeled loop iteration
    mov8i(Rsentinel, 1);

bind(sentinel_counting_loop);
    add8i(Rtmp, -reg_size);
    cmp4i(Rtmp,4,frame::double_slot_primitive_type_empty_slot_id);
    jne  (test_1_counting_complete);
    inc4 (Rsentinel);
    cmp8 (Rtmp, RDX_LOC);
    jne  (sentinel_counting_loop);

bind(test_1_counting_complete);
    and1i(Rsentinel,1);
    jeq  (test_1_complete);
    
    // At this point, test #1 has passed. Zap the previous slot!
bind(test_1_complete_with_sentinel);
    st8i (Rslot, -reg_size, 0);
jmp(test_2_complete);
    
bind(test_1_complete);
  }

  // Test #2
  
  cmp4i(Rslot,4,frame::double_slot_primitive_type_empty_slot_id);
  jne  (test_2_complete);

  // At this point, test #2 has passed, Zap the following slot!
  st8i (Rslot, reg_size, 0);

bind(test_2_complete);
}

// The double slot test is similar to doing the single slot test on both slots (which does work).
// However, the double slot test can be optimized a bit. For any four slots, assuming we are storing
// to the middle pair of slots, there are only 4 conditions that we care about:
//
// S = sentinel, V = value
//
// Slot 1    S    V    S    S   (offset is -reg_size)
// Slot 2    V    V    V    V   (offset is zero)
// Slot 3    V    S    S    V   (offset is reg_size)
// Slot 4    V    V    V    S   (offset is reg_size * 2)

// NOTE! This method assumes that the TARGET_SLOT pointed to is the lower numbered of the two
// slots. It will fail if you pass in the higher numbered slot.
void InterpreterMacroAssembler::double_slot_locals_store_check(RInOuts, Register Rslot, Register Rsentinel, Register Rtmp) {
  assert_different_registers(Rslot,Rsentinel,Rtmp);

Label sentinel_counting_loop;
Label test_1_counting_complete;
Label slot_1_not_sentinel;
Label stomp_slot_4;
Label all_tests_complete;

  // Test for slot 1 being a sentinel

  // If we are storing to locals 0, there can be no previous sentinel value
  cmp8 (Rslot, RDX_LOC);
  jeq  (slot_1_not_sentinel);

  // Count the number of prior sentinel values
  mov8i(Rsentinel, 0);
  move8(Rtmp, Rslot);

bind(sentinel_counting_loop);
  add8i(Rtmp, -reg_size);
  cmp4i(Rtmp,4,frame::double_slot_primitive_type_empty_slot_id);
  jne  (test_1_counting_complete);
  inc4 (Rsentinel);
  cmp8 (Rtmp, RDX_LOC);
  jne  (sentinel_counting_loop);

bind(test_1_counting_complete);
  and1i(Rsentinel,1);
  jeq  (slot_1_not_sentinel);

  // At this point, test #1 has passed. Zap the previous slot!
  st8i (Rslot, -reg_size, 0);

  // We know that slot 1 was a sentinel at this point. There is no need
  // to check slot 2, as we know it is a value. Check slot 3.
  cmp4i(Rslot, reg_size+4, frame::double_slot_primitive_type_empty_slot_id);
  jeq  (stomp_slot_4);
jmp(all_tests_complete);

bind(slot_1_not_sentinel);
  // We need to check slot 2 for sentinel status. If it is, we're done.
  cmp4i(Rslot, 4, frame::double_slot_primitive_type_empty_slot_id);
  jeq  (all_tests_complete);
  // Okay, slot 2 was not a sentinel. If slot 3 is, we need to stomp slot 4
  cmp4i(Rslot, reg_size+4, frame::double_slot_primitive_type_empty_slot_id);
  jne  (all_tests_complete);

bind(stomp_slot_4);
  st8i  (Rslot, reg_size * 2, 0);

bind(all_tests_complete);
}


// --- empty_java_exec_stack
// Stack is left empty and not "over-popped".
void InterpreterMacroAssembler::empty_java_exec_stack(RInOuts, Register Rtmp) {
  getmoop(Rtmp);
  ldz2 (Rtmp, Rtmp, in_bytes(methodOopDesc::size_of_locals_offset()));
  lea  (RDI_JEX,RDX_LOC,0,Rtmp,3);
}

void InterpreterMacroAssembler::push_tos_adj_sp(Register reg){
  st8  (RDI_JEX,  0, reg);
  add8i(RDI_JEX, 16);
  // Stamp bad slot sentinel value into the empty slot
  store_sentinel(RKeepIns::a,RDI_JEX, -8);
}

void InterpreterMacroAssembler::adj_sp_pop_tos(){
  ld8  (RCX_TOS, RDI_JEX, -16);
  sub8i(RDI_JEX, 16);
}

// --- check_subtype ---------------------------------------------------------
// Kills the named registers.
void InterpreterMacroAssembler::check_subtype(RInOuts, Register Rsubkid, Register RAX_superklass,
Register R09_tmp,Register Rtmp2,Register Rtmp3,
Label&is_subtype){
  assert0( R09_tmp        == R09 ); // necessary for argument set up to stub routine call
  assert0( RAX_superklass == RAX ); // necessary for argument set up to stub routine call
Label not_subtype;

  Register R09_subklass = R09_tmp;
  Register Rsuperkid    = Rtmp2;

  cvta2va(RAX_superklass);
  ldz4 (Rsuperkid, RAX_superklass, sizeof(oopDesc) + Klass::klassId_offset_in_bytes());

  // 1) Trivial equality test
  cmp4(Rsubkid,Rsuperkid);
  jeq (is_subtype);

  // 2) Is super's KID in subklass's cache?
  kid2klass(RInOuts::a, R09_subklass, Rtmp3, RKeepIns::a, Rsubkid);
  cvta2va(R09_subklass);
  Register Rsuper_check_off=Rtmp3;
  ldz4 (Rsuper_check_off, RAX_superklass, sizeof(oopDesc) + Klass::super_check_offset_offset_in_bytes());
  cmp4 (Rsuperkid, R09_subklass, 0, Rsuper_check_off, 0);
  jeq  (is_subtype); // Then it's a hit: we are a subtype

  // 3) Did we check in the direct subtype list or in the secondary list's cache?
  cmp4i(Rsuper_check_off, (sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes()));
  jne  (not_subtype); // checked in direct list => failure

  // 4) Slow path, loop over secondary supers array returning NZ for hit or Z for failure
  // Inputs: R09_subklass
  // Inputs: RAX_superklass
  call (StubRoutines::x86::partial_subtype_check());
  test4 (RAX,RAX);
  jeq   (is_subtype);           // Z => we are a subtype
  bind  (not_subtype);
}

// --- remove_activation -----------------------------------------------------
// Remove an interpreter activation.  Unlock the receiver if the method is
// synchronized.  Unlock any other locks as well.  If we're doing a "normal"
// exit, nothing should be locked so throw IllegalMonitorState if we see
// anything locked.  If we're doing an exception exit, again the local handler
// should have unlocked all (but the receiver of sync'd methods).  If we're
// doing a forced unwind because of stack overflow, just unlock.
//
// Some interpreter state is free, e.g. RSI, RAX, RCX, but most exits want
// RDI_JEX and RDX_LOC so these cannot be crushed.
void InterpreterMacroAssembler::remove_activation(RInOuts, Register Rsi, Register Rcx, Register Rtmp0, bool exception_exit, bool throw_if_locked) {
  assert_different_registers(Rsi,Rcx,Rtmp0);
  Label loop, slow_unlock, all_unlocked, unlocked, do_throw1, do_throw2;

  // Check for sync method being locked still
  if( !exception_exit || throw_if_locked ) {
Label not_sync;
    assert0( is_power_of_2(JVM_ACC_SYNCHRONIZED) );
    getmoop(RAX);               // Reload method OOP
    btx4i(RAX, in_bytes(methodOopDesc::access_flags_offset()), log2_intptr(JVM_ACC_SYNCHRONIZED) );
    jae  (not_sync);            // jump if CF==0
    ldz2 (RCX, RSP, offset_of(IFrame,_numlck));
    cmp4i(RCX,1);               // Less than 1 lock?
    // Silly JCK tests make me pick between 2 subtle flavors of throwing.
    jbl  (!exception_exit ? do_throw1 : do_throw2);
bind(not_sync);
  }

  // ---
  // Check that all monitors are unlocked
  bind (loop);
  ldz2 (RCX, RSP, offset_of(IFrame,_numlck)); // number of locks
  test4(RCX,RCX);
  jeq  (all_unlocked);          // No locks present
  
  if( !exception_exit ) {       // Normal sync-method exit: if this is 1st loop, then all is good
Label do_unlock;
    cmp4i(RCX,1);               // Down to the last lock which is the 'this' of a sync method?
    jgt  (do_throw1);           // too many locks, throw up
    getmoop(RAX);               // Reload method OOP
    btx4i(RAX, in_bytes(methodOopDesc::access_flags_offset()), log2_intptr(JVM_ACC_SYNCHRONIZED) );
    jbl  (do_unlock);           // jump if CF==1, sync Then just unlock, no complaints
bind(do_throw1);

    call_VM((address)InterpreterRuntime::throw_IllegalMonitorStateException, noreg,
            RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, noreg, noreg, noreg);
    // We do not return from this call!  An exception IS set and we forward to it
bind(do_unlock);
  }

  dec4 (RCX);
  st2  (RSP, offset_of(IFrame,_numlck),RCX); // number of locks
  getthr(R10);
  ld8  (RAX,R10,in_bytes(JavaThread::lckstk_top_offset())); // top of lock stack
  sub8i(RAX,8);
  st8  (R10,in_bytes(JavaThread::lckstk_top_offset()),RAX);
  ld8  (R10,RAX,0);             // Load a lock
  if( RefPoisoning ) always_unpoison(R10);
  
  // Now unlock the synchronized method
  // R10: locked object
  fast_path_unlock(RInOuts::a, R10, RCX, Rtmp0, slow_unlock);
  bind (unlocked);

  if( throw_if_locked ) {
Label do_unlock;
    ldz2 (RCX, RSP, offset_of(IFrame,_numlck)); // number of locks
    cmp4i(RCX,1);               // Down to the last lock which is the 'this' of a sync method?
    jgt  (do_throw2);           // too many locks, throw up
    getmoop(RAX);               // Reload method OOP
    btx4i(RAX, in_bytes(methodOopDesc::access_flags_offset()), log2_intptr(JVM_ACC_SYNCHRONIZED) );
    jbl  (do_unlock);           // jump if CF==1, sync Then just unlock, no complaints
bind(do_throw2);
    call_VM_plain((address)InterpreterRuntime::new_IllegalMonitorStateException, noreg, noreg, noreg, noreg);
    // We do not return from this call!  An exception IS set and we forward to it
bind(do_unlock);
  }
jmp(loop);

  // R10 holds the locked 'this' pointer, and it is not biased locked
  // RCX holds the loaded markWord
  // Crushes RAX, RCX, R09, R10, R11.
bind(slow_unlock);
  assert0( Rcx == RCX );        // Incoming arg
  call(StubRoutines::unlock_entry());
jmp(unlocked);

  bind (all_unlocked);          // Checked them all
}


void InterpreterMacroAssembler::get_cache_index_at_bcp(RInOuts, Register index, RKeepIns, Register Rbcp, int offset) {
  ldz2 (index, Rbcp, offset);   // unaligned load, works great on X86
  assert0(sizeof(ConstantPoolCacheEntry) == 4 * ptr_size);
  shl4i(index, 2 + log_ptr_size); // scale by ConstantPoolCacheEntry size
}


// Inline assembly for:
//
// if (thread is in interp_only_mode) {
//   InterpreterRuntime::post_method_entry();
// }

void InterpreterMacroAssembler::notify_method_entry() {
  // Whenever JVMTI puts a thread in interp_only_mode, method
  // entry/exit events are sent for that thread to track stack
  // depth.  If it is possible to enter interp_only_mode we add
  // the code to check if the event should be sent.
  if (JvmtiExport::can_post_interpreter_events()) {
    Label L;
    getthr(R09);
    cmp4i (R09,in_bytes(JavaThread::interp_only_mode_offset()),0);
jeq(L);
//    call_VM(noreg, CAST_FROM_FN_PTR(address, SharedRuntime::post_method_entry), IA0 );
    bind(L);
  }
}


// Inline assembly for:
//
// if (thread is in interp_only_mode) {
//   // save result
//   InterpreterRuntime::post_method_exit();
//   // restore result
// }
void InterpreterMacroAssembler::notify_method_exit(bool is_native_method) {
  // Whenever JVMTI puts a thread in interp_only_mode, method
  // entry/exit events are sent for that thread to track stack
  // depth.  If it is possible to enter interp_only_mode we add
  // the code to check if the event should be sent.
  if (JvmtiExport::can_post_interpreter_events()) {
    Label L;
    getthr(R09);
    cmp4i (R09,in_bytes(JavaThread::interp_only_mode_offset()),0);
jeq(L);
    unimplemented("");
//    call_VM(noreg, CAST_FROM_FN_PTR(address, SharedRuntime::post_method_entry), IA0 );
    bind(L);
  }
}

// -- getmoop
// Loads method OOP into RBX_MTH
void InterpreterMacroAssembler::getmoop(Register reg){
  ld8    (reg,RSP,offset_of(IFrame,_mref)); // Reload moop
  cvta2va(reg);                 // Strip metadata
}

// --- pack_interpreter_regs
// Pack some interpreter regs onto the stack in preperation for a call (the
// remaining regs do not need to be saved and can be rebuilt on return).  Used
// by both call_VM and the various invoke_XXXX calls.
// RDX is crushed.  RSI_BCP & RDI_JEX are preserved (but saved on stack).
void InterpreterMacroAssembler::pack_interpreter_regs(RInOuts, Register Rtmp0, Register Rtmp1, Register Rdx_loc, RKeepIns, Register Rdi_jex, Register Rsi_bcp ) {
  assert0( Rsi_bcp == RSI_BCP && Rdi_jex == RDI_JEX && Rdx_loc == RDX_LOC );
  assert0( Rtmp0 != noreg && Rtmp1 != noreg );
  assert_different_registers(Rtmp0,Rtmp1,RSI_BCP,RDX_LOC,RDI_JEX, true);
  // Update the Java Execution Stack top as a service to VM calls
  getthr(Rtmp0);
  st8  (Rtmp0,in_bytes(JavaThread::jexstk_top_offset()),RDI_JEX);
  // Save _loc and _stk
  st4  (RSP,offset_of(IFrame,_loc),RDX_LOC);
  st4  (RSP,offset_of(IFrame,_stk),RDI_JEX);
  // Pack RSI into a BCI
  getmoop(Rtmp0);   // Reload method OOP, do it prior to pushes that will change RSP.
  ldref_lvb(RInOuts::a, Rtmp1, RDX, RKeepIns::a, Rtmp0, in_bytes(methodOopDesc::const_offset()), false);
  cvta2va(Rtmp1);                // convert constMethod into a real pointer
  sub4 (RSI_BCP,Rtmp1);          // BCI in RSI, plus sizeof(constMethod)
  sub4i(RSI_BCP,in_bytes(constMethodOopDesc::codes_offset()));
  st2  (RSP,offset_of(IFrame,_bci), RSI_BCP); // Save in stack for GC/VM to find.
  // Recover RSI
  lea  (RSI_BCP,RSI_BCP, in_bytes(constMethodOopDesc::codes_offset()), Rtmp1, 0);
}

// --- unpack_interpreter_regs
// Recover RSI_BCP & R08_CPC & RDI_JEX & RDX_LOC.
// Rmoop holds oop of method, Rtmp holds thread at end.
// Stack is left not-over-popped.
void InterpreterMacroAssembler::unpack_interpreter_regs(RInOuts, Register Rmoop, Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc) {
  assert0( Rdx_loc == RDX_LOC && Rsi_bcp == RSI_BCP && Rdi_jex == RDI_JEX && R08_cpc == R08_CPC );
  assert_different_registers(Rmoop,RDX_LOC,RSI_BCP,RDI_JEX,R08_CPC, true);
  // Rebuild interpreter state after VM call crushed everything
  ld8    (Rmoop  ,RSP,offset_of(IFrame,_mref)); // methodRef  was saved on stack
  ldz2   (Rsi_bcp,RSP,offset_of(IFrame,_bci));  // Load BCI
  verify_not_null_oop(Rmoop, MacroAssembler::OopVerify_Sanity);
  cvta2va(Rmoop);                               // strip methodRef to give method oop

  // load _constMethod from method oop, use RDX as a temp
  ldref_lvb(RInOuts::a, RDI, RDX, RKeepIns::a, Rmoop, in_bytes(methodOopDesc::const_offset()),false);
  cvta2va(RDI);                                 // strip _constMethod
  ldz4   (Rdx_loc,RSP,offset_of(IFrame,_loc));  // Get count of java locals
  lea    (Rsi_bcp,RDI,in_bytes(constMethodOopDesc::codes_offset()),Rsi_bcp,0);  // Rebuild BCP from BCI
  ldz4   (Rdi_jex,RSP,offset_of(IFrame,_stk));  // Get count of java stack
  getthr (R08);
  or_8   (Rdi_jex,R08);         // rebuild RDI_JEX
  or_8   (Rdx_loc,R08);         // rebuild RDX_LOC

  ld8    (R08_cpc,RSP,offset_of(IFrame,_cpc) ); // cpCacheRef was saved on stack
  verify_not_null_oop(R08_cpc, MacroAssembler::OopVerify_Sanity);
  cvta2va(R08_cpc);                             // strip cpCacheRef
}


// --- ref_store_with_check
// Store val_ref into base_ref+offset, doing a store-check first.  The
// Store-check can trigger generational card-marking, and also SBA escapes.
// SBA escapes can trigger GC.  Hence this macro can GC and is an interpreter
// GC point.  If a safepoint happens it must be taken BEFORE the store
// bytecode, because the store cannot have happened yet (and the store cannot
// happen until GC makes space for the SBA escape).
//
// Rtmp0, Rtmp1 - Temps that are cruhsed
// Rbase  - Base REF address being stored into, converted to oop
// Rval   - Value REF being stored, possibly poisoned
// Rindex - Register holding index for array stores or noreg
// off    - Displacement from base ref
// scale  - Scale to apply to Rindex
// retry  - If an SBA escape occurs, we must reload Rbase and Rval and retry
//
// RCX_TOS- TOS is pushed already; this register is free (and may hold an incoming arg)
void InterpreterMacroAssembler::ref_store_with_check(RInOuts, Register Rbase, int off, Register Rindex, int scale, Register Rval, Register Rtmp0, Register Rtmp1,
Label&retry){
  Label poison_and_store, strip, checked, done;
  // Cheap cases where no stack escape or barrier are necessary
  null_chk( Rval,strip  ); // NULLs are always safe to store
  null_chk(Rbase,checked); // NULL addr will explode on the store

  pre_write_barrier(RInOuts::a, Rtmp0, Rtmp1, RKeepIns::a, Rbase, off, Rindex, scale, Rval, poison_and_store);

  if( UseSBA ) {
    // Full SBA escape
    pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
    // Now call into the VM; all interpreter registers are saved/re-computable
    call_VM_plain(CAST_FROM_FN_PTR(address, SharedRuntime::sba_escape),Rval,Rbase,noreg,noreg);
    // Rebuild interpreter state after VM call crushed everything
    unpack_interpreter_regs(RInOuts::a, Rtmp0, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
jmp(retry);
  }

bind(poison_and_store);
#ifdef ASSERT
  if (RefPoisoning) always_poison(Rval); // Must store poisoned ref
#endif // ASSERT
bind(strip);
  cvta2va(Rbase);               // Get the virtual address
bind(checked);
  if (Rindex == noreg) st8 (Rbase, off, Rval);
  else                 st8 (Rbase, off, Rindex, scale, Rval);
  bind (done);
}


// --- call_VM - from interpreter into VM with upto 3 args
// Making a call to native blows most of the interpreter registers.  I can't
// place any oops in callee-save registers, because GC can't track them in
// native code.  But actually, VM calls are rare and slow and most interpreter
// values are easy to regenerate.  Only the MTH really needs to be GC tracked.
//
// All caller save registers are blown by this call, on return RAX holds the result
// and R09 the method oop
//
// Rresult   - register to hold result
// RDX_LOC   - save/restored as offset in frame; eventually arg2
// RSI_BCP   - converted into a offset from constMethod, and saved in stack; also arg1
// RDI_JEX   - save/restored as offset in frame; eventually arg0
// R08_CPC   - crushed; reloaded from stack
// Rtmp[12]  - non-arg registers to use for temps
// Rarg[123] - registers holding arguments
void InterpreterMacroAssembler::call_VM(address entry_point, Register Rresult,
                                        Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
Register Rtmp0,
Register Rarg1,Register Rarg2,Register Rarg3){

  Register Rtmp1 = R08_cpc; // already on stack and reloaded during unpack
  pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, Rdx_loc, RKeepIns::a, Rdi_jex, Rsi_bcp);

  // Now call into the VM; all interpreter registers are saved/re-computable
  call_VM_plain(entry_point, Rarg1, Rarg2, Rarg3, noreg);
  // RAX holds return value (except for F00 for some float calls)
  // Other interpreter registers are NOT rebuilt

  check_and_handle_popframe();

  // Check for exceptions
{Label no_exception;
    getthr (Rtmp0);
    null_chk(Rtmp0, in_bytes(JavaThread::pending_exception_offset()), no_exception);
    // Forward_exception_entry *will* pop a frame, so push one now in case we
    // are going to catch this exception in this method.  We care not coming back
    // from the exception and all registers are free and the JES will be popped.
    call(StubRoutines::forward_exception_entry());
bind(no_exception);
  }

  // Rebuild interpreter state after VM call crushed everything.
  // RAX holds the return value
  unpack_interpreter_regs(RInOuts::a, Rtmp0, Rdx_loc, Rsi_bcp, Rdi_jex, R08_cpc);
}


// --- call_VM_leaf_interp.
// No stack crawls; just standard C code.  RSI & R08 are derived from OOPs and
// are not GCd.  Crushes RCX_TOS.  No args passed, including thread.
void InterpreterMacroAssembler::call_VM_leaf_interp(address adr){
push(RSI_BCP);
push(RDI_JEX);
push(RDX_LOC);
push(R08_CPC);
call(adr);
pop(R08_CPC);
pop(RDX_LOC);
pop(RDI_JEX);
pop(RSI_BCP);
}

void InterpreterMacroAssembler::call_VM_leaf_interp(address adr,Register arg0,Register arg1,Register arg2){
push(RSI_BCP);
push(RDI_JEX);
push(RDX_LOC);
push(R08_CPC);
  if( arg0 != noreg && arg0 != RDI ) move8(RDI,arg0);
  if( arg1 != noreg && arg1 != RSI ) { assert0(RDI!=arg1); move8(RSI,arg1); }
  if( arg2 != noreg && arg2 != RDX ) { assert0(RDI!=arg2 && RSI!=arg2); move8(RDX,arg2); }
call(adr);
pop(R08_CPC);
pop(RDX_LOC);
pop(RDI_JEX);
pop(RSI_BCP);
}

// not enough free registers for a call_VM with 4 args, so special case of call_VM for SharedRuntime::_new
void InterpreterMacroAssembler::call_runtime_new(Bytecodes::Code bcode,
                                                 Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
                                                 Register R11_kid, Register R10_bytes, Register RCX_len, bool allow_sba) {
  { Register Rtmp0 = get_free_reg(bcode, RDX_LOC, RSI_BCP, RDI_JEX, R10_bytes, RCX_len, R11_kid);
    Register Rtmp1 = get_free_reg(bcode, RDX_LOC, RSI_BCP, RDI_JEX, R10_bytes, RCX_len, R11_kid, Rtmp0);
    pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  mov8i(RAX,allow_sba);      // sba hint
  call_VM_plain(CAST_FROM_FN_PTR(address, SharedRuntime::_new), R11_kid, R10_bytes, RCX_len, RAX);
  // RAX holds return value
  Register RCX_tmp = RCX;
  check_and_handle_popframe();
  // Check for exceptions
{Label no_exception;
    getthr (RCX_tmp);
    null_chk(RCX_tmp, in_bytes(JavaThread::pending_exception_offset()), no_exception);
    // Forward_exception_entry *will* pop a frame, so push one now in case we
    // are going to catch this exception in this method.  We care not coming back
    // from the exception and all registers are free and the JES will be popped.
    call(StubRoutines::forward_exception_entry());
bind(no_exception);
  }
  // Rebuild interpreter state after VM call crushed everything.
  // RAX holds the return value
  unpack_interpreter_regs(RInOuts::a, RCX_tmp, Rdx_loc, Rsi_bcp, Rdi_jex, R08_cpc);
}
