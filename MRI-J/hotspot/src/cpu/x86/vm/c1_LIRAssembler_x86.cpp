/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_FrameMap.hpp"
#include "c1_IR.hpp"
#include "c1_LinearScan.hpp"
#include "c1_LIRAssembler.hpp"
#include "c2_globals.hpp"
#include "ciArrayKlass.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "objArrayKlass.hpp"
#include "register_pd.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "systemDictionary.hpp"
#include "universe.hpp"

#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

NEEDS_CLEANUP // remove this definitions ?
const Register IC_Klass=RAX;//where the IC klass is cached
const Register SHIFT_count=RCX;//where count for shift operations must be

#define __ _masm->

static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2) {
  if (tmp1 == preserve) {
    assert_different_registers(tmp1, tmp2, extra);
    tmp1 = extra;
  } else if (tmp2 == preserve) {
    assert_different_registers(tmp1, tmp2, extra);
    tmp2 = extra;
  }
  assert_different_registers(preserve, tmp1, tmp2);
}



static void select_different_registers(Register preserve,
                                       Register extra,
                                       Register &tmp1,
                                       Register &tmp2,
                                       Register &tmp3) {
  if (tmp1 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp1 = extra;
  } else if (tmp2 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp2 = extra;
  } else if (tmp3 == preserve) {
    assert_different_registers(tmp1, tmp2, tmp3, extra);
    tmp3 = extra;
  }
  assert_different_registers(preserve, tmp1, tmp2, tmp3);
}



bool LIR_Assembler::is_small_constant(LIR_Opr opr) {
  if (opr->is_constant()) {
    LIR_Const* constant = opr->as_constant_ptr();
    switch (constant->type()) {
case T_INT:return true;
    case T_FLOAT: return true;
    case T_LONG:  return is_int32(constant->as_jlong());
    case T_DOUBLE:return is_int32(constant->as_jlong_bits());
    default:      return false;
    }
  }
  return false;
}


LIR_Opr LIR_Assembler::receiverOpr() {
return FrameMap::rdi_oop_opr;
}

LIR_Opr LIR_Assembler::incomingReceiverOpr() {
  return receiverOpr();
}

LIR_Opr LIR_Assembler::osrBufferPointer() {
  return FrameMap::rcx_opr;
}

void LIR_Assembler::breakpoint() {
__ os_breakpoint();
}

void LIR_Assembler::null_check(LIR_Opr obj,CodeEmitInfo*info){
  if( !MultiMapMetaData ) {
ShouldNotReachHere();//null checks are cmp branches to avoid strip
  } else {
    add_debug_info_for_null_check_here(info);
__ null_check(obj->as_register());
  }
}


void LIR_Assembler::push(LIR_Opr opr) {
  ShouldNotReachHere();
}

void LIR_Assembler::pop(LIR_Opr opr) {
  ShouldNotReachHere();
}

//-------------------------------------------
Address LIR_Assembler::as_Address(LIR_Address* addr) {
  if (addr->base()->is_illegal()) {
    assert(addr->index()->is_illegal(), "must be illegal too");
    //return Address(addr->disp(), relocInfo::none);
    // hack for now since this should really return an AddressLiteral
    // which will have to await 64bit c1 changes.
    return Address(noreg, addr->disp());
  }

  Register base = addr->base()->as_register();

  if (addr->index()->is_illegal()) {
    return Address( base, addr->disp());
  } else if (addr->index()->is_single_cpu()) {
    Register index = addr->index()->as_register();
    return Address(base, index, (Address::ScaleFactor) addr->scale(), addr->disp());
  } else if (addr->index()->is_constant()) {
    int addr_offset = (addr->index()->as_constant_ptr()->as_jint() << addr->scale()) + addr->disp();

    return Address(base, addr_offset);
  } else {
    Unimplemented();
    return Address();
  }
}


Address LIR_Assembler::as_Address_hi(LIR_Address* addr) {
  Address base = as_Address(addr);
  return Address(base._base, base._index, base._scale, base._disp + BytesPerWord);
}


Address LIR_Assembler::as_Address_lo(LIR_Address* addr) {
  return as_Address(addr);
}


void LIR_Assembler::osr_entry() {
  Unimplemented();
//  offsets()->set_value(CodeOffsets::OSR_Entry, code_offset());
//  BlockBegin* osr_entry = compilation()->hir()->osr_entry();
//  ValueStack* entry_state = osr_entry->state();
//  int number_of_locks = entry_state->locks_size();
//
//  // we jump here if osr happens with the interpreter
//  // state set up to continue at the beginning of the
//  // loop that triggered osr - in particular, we have
//  // the following registers setup:
//  //
//  // rcx: osr buffer
//  //
//
//  // build frame
//  ciMethod* m = compilation()->method();
//  __ build_frame(initial_frame_size_in_bytes());
//
//  // OSR buffer is
//  //
//  // locals[nlocals-1..0]
//  // monitors[0..number_of_locks]
//  //
//  // locals is a direct copy of the interpreter frame so in the osr buffer
//  // so first slot in the local array is the last local from the interpreter
//  // and last slot is local[0] (receiver) from the interpreter
//  //
//  // Similarly with locks. The first lock slot in the osr buffer is the nth lock
//  // from the interpreter frame, the nth lock slot in the osr buffer is 0th lock
//  // in the interpreter frame (the method lock if a sync method)
//
//  // Initialize monitors in the compiled activation.
//  //   rcx: pointer to osr buffer
//  //
//  // All other registers are dead at this point and the locals will be
//  // copied into place by code emitted in the IR.
//
//  Register OSR_buf = osrBufferPointer()->as_register();
//  { assert(frame::interpreter_frame_monitor_size() == BasicObjectLock::size(), "adjust code below");
//    int monitor_offset = BytesPerWord * method()->max_locals() +
//      (BasicObjectLock::size() * BytesPerWord) * (number_of_locks - 1);
//    for (int i = 0; i < number_of_locks; i++) {
//      int slot_offset = monitor_offset - ((i * BasicObjectLock::size()) * BytesPerWord);
//#ifdef ASSERT
//      // verify the interpreter's monitor has a non-null object
//      {
//        Label L;
//        __ cmpl(Address(OSR_buf, slot_offset + BasicObjectLock::obj_offset_in_bytes()), NULL_WORD);
//        __ jcc(Assembler::notZero, L);
//        __ stop("locked object is NULL");
//        __ bind(L);
//      }
//#endif
//      __ movl(rbx, Address(OSR_buf, slot_offset + BasicObjectLock::lock_offset_in_bytes()));
//      __ movl(frame_map()->address_for_monitor_lock(i), rbx);
//      __ movl(rbx, Address(OSR_buf, slot_offset + BasicObjectLock::obj_offset_in_bytes()));
//      __ movl(frame_map()->address_for_monitor_object(i), rbx);
//    }
//  }
}


void LIR_Assembler::jobject2reg_with_patching(Register reg, Register tmp1, Register tmp2, CodeEmitInfo* info) {
  PatchingStub* patch = new LK_PatchingStub(_masm, reg, tmp1, tmp2, info);
patching_epilog(patch);
  __ verify_not_null_oop(reg, MacroAssembler::OopVerify_OopTableLoad);
}


void LIR_Assembler::monitorexit(LIR_Opr obj_opr, LIR_Opr lock_opr, Register new_hdr, int monitor_no, Register exception) {
  Unimplemented();
//  if (is_valid_reg(exception)) {
//    // preserve exception
//    // note: the monitor_exit runtime call is a leaf routine
//    //       and cannot block => no GC can happen
//    // The slow case (MonitorAccessStub) uses the first two stack slots
//    // ([esp+0] and [esp+4]), therefore we store the exception at [esp+8]
//    __ movl (Address(rsp, 2*wordSize), exception);
//  }
//
//  Register obj_reg  = obj_opr->as_register();
//  Register lock_reg = lock_opr->as_register();
//
//  // setup registers (lock_reg must be rax, for lock_object)
//  assert(obj_reg != SYNC_header && lock_reg != SYNC_header, "rax, must be available here");
//  Register hdr = lock_reg;
//  assert(new_hdr == SYNC_header, "wrong register");
//  lock_reg = new_hdr;
//  // compute pointer to BasicLock
//  Address lock_addr = frame_map()->address_for_monitor_lock(monitor_no);
//  __ leal(lock_reg, lock_addr);
//  // unlock object
//  MonitorAccessStub* slow_case = new MonitorExitStub(lock_opr, true, monitor_no);
//  // _slow_case_stubs->append(slow_case);
//  // temporary fix: must be created after exceptionhandler, therefore as call stub
//  _slow_case_stubs->append(slow_case);
//  if (UseFastLocking) {
//    // try inlined fast unlocking first, revert to slow locking if it fails
//    // note: lock_reg points to the displaced header since the displaced header offset is 0!
//    assert(BasicLock::displaced_header_offset_in_bytes() == 0, "lock_reg must point to the displaced header");
//    __ unlock_object(hdr, obj_reg, lock_reg, *slow_case->entry());
//  } else {
//    // always do slow unlocking
//    // note: the slow unlocking code could be inlined here, however if we use
//    //       slow unlocking, speed doesn't matter anyway and this solution is
//    //       simpler and requires less duplicated code - additionally, the
//    //       slow unlocking code is the same in either case which simplifies
//    //       debugging
//    __ jmp(*slow_case->entry());
//  }
//  // done
//  __ bind(*slow_case->continuation());
//
//  if (is_valid_reg(exception)) {
//    // restore exception
//    __ movl (exception, Address(rsp, 2 * wordSize));
//  }
}

// This specifies the rsp decrement needed to build the frame
int LIR_Assembler::initial_frame_size_in_bytes() {
  // if rounding, must let FrameMap know!
  return in_bytes(frame_map()->framesize_in_bytes());
}


void LIR_Assembler::emit_exception_handler() {
  __ bind(compilation()->_generic_exception_handler);

  // the exception oop is in thr->pending_exception
  // All other registers are scratch.

  // unwind activation and forward exception to caller
__ restore_callee_saves_pop_frame_and_jmp(frame_map(),Runtime1::entry_for(Runtime1::unwind_exception_id));
}

//void LIR_Assembler::emit_deopt_handler() {
//  // if the last instruction is a call (typically to do a throw which
//  // is coming at the end after block reordering) the return address
//  // must still point into the code area in order to avoid assertion
//  // failures when searching for the corresponding bci => add a nop
//  // (was bug 5/14/1999 - gri)
//
//  __ nop();
//
//  // generate code for exception handler
//  address handler_base = __ start_a_stub(deopt_handler_size);
//  if (handler_base == NULL) {
//    // not enough space left for the handler
//    bailout("deopt handler overflow");
//    return;
//  }
//#ifdef ASSERT
//  int offset = code_offset();
//#endif // ASSERT
//
//  compilation()->offsets()->set_value(CodeOffsets::Deopt, code_offset());
//
//  InternalAddress here(__ pc());
//  __ pushptr(here.addr());
//
//  __ jump(RuntimeAddress(SharedRuntime::deopt_blob()->unpack()));
//
//  assert(code_offset() - offset <= deopt_handler_size, "overflow");
//
//  __ end_a_stub();
//
//}
//
//
//// This is the fast version of java.lang.String.compare; it has not
//// OSR-entry and therefore, we generate a slow version for OSR's
//void LIR_Assembler::emit_string_compare(LIR_Opr arg0, LIR_Opr arg1, LIR_Opr dst, CodeEmitInfo* info) {
//  __ movl (rbx, rcx); // receiver is in rcx
//  __ movl (rax, arg1->as_register());
//
//  // Get addresses of first characters from both Strings
//  __ movl (rsi, Address(rax, java_lang_String::value_offset_in_bytes()));
//  __ movl (rcx, Address(rax, java_lang_String::offset_offset_in_bytes()));
//  __ leal (rsi, Address(rsi, rcx, Address::times_2, arrayOopDesc::base_offset_in_bytes(T_CHAR)));
//
//
//  // rbx, may be NULL
//  add_debug_info_for_null_check_here(info);
//  __ movl (rdi, Address(rbx, java_lang_String::value_offset_in_bytes()));
//  __ movl (rcx, Address(rbx, java_lang_String::offset_offset_in_bytes()));
//  __ leal (rdi, Address(rdi, rcx, Address::times_2, arrayOopDesc::base_offset_in_bytes(T_CHAR)));
//
//  // compute minimum length (in rax) and difference of lengths (on top of stack)
//  if (VM_Version::supports_cmov()) {
//    __ movl (rbx, Address(rbx, java_lang_String::count_offset_in_bytes()));
//    __ movl (rax, Address(rax, java_lang_String::count_offset_in_bytes()));
//    __ movl (rcx, rbx);
//    __ subl (rbx, rax); // subtract lengths
//    __ pushl(rbx);      // result
//    __ cmovl(Assembler::lessEqual, rax, rcx);
//  } else {
//    Label L;
//    __ movl (rbx, Address(rbx, java_lang_String::count_offset_in_bytes()));
//    __ movl (rcx, Address(rax, java_lang_String::count_offset_in_bytes()));
//    __ movl (rax, rbx);
//    __ subl (rbx, rcx);
//    __ pushl(rbx);
//    __ jcc  (Assembler::lessEqual, L);
//    __ movl (rax, rcx);
//    __ bind (L);
//  }
//  // is minimum length 0?
//  Label noLoop, haveResult;
//  __ testl (rax, rax);
//  __ jcc (Assembler::zero, noLoop);
//
//  // compare first characters
//  __ load_unsigned_word(rcx, Address(rdi, 0));
//  __ load_unsigned_word(rbx, Address(rsi, 0));
//  __ subl(rcx, rbx);
//  __ jcc(Assembler::notZero, haveResult);
//  // starting loop
//  __ decrement(rax); // we already tested index: skip one
//  __ jcc(Assembler::zero, noLoop);
//
//  // set rsi.edi to the end of the arrays (arrays have same length)
//  // negate the index
//
//  __ leal(rsi, Address(rsi, rax, Address::times_2, type2aelembytes[T_CHAR]));
//  __ leal(rdi, Address(rdi, rax, Address::times_2, type2aelembytes[T_CHAR]));
//  __ negl(rax);
//
//  // compare the strings in a loop
//
//  Label loop;
//  __ align(wordSize);
//  __ bind(loop);
//  __ load_unsigned_word(rcx, Address(rdi, rax, Address::times_2, 0));
//  __ load_unsigned_word(rbx, Address(rsi, rax, Address::times_2, 0));
//  __ subl(rcx, rbx);
//  __ jcc(Assembler::notZero, haveResult);
//  __ increment(rax);
//  __ jcc(Assembler::notZero, loop);
//
//  // strings are equal up to min length
//
//  __ bind(noLoop);
//  __ popl(rax);
//  return_op(LIR_OprFact::illegalOpr);
//
//  __ bind(haveResult);
//  // leave instruction is going to discard the TOS value
//  __ movl (rax, rcx); // result of call is in rax,
//}


void LIR_Assembler::return_op(LIR_Opr result) {
assert(result->is_illegal()||!result->is_single_cpu()||result->as_register()==RAX,"word returns are in rax,");
  // Azul expects to poll-on-entry not poll-on-exit.  So no poll here.
  if( result->is_valid() && result->is_oop_register() ) {
    __ verify_oop(result->as_register(), MacroAssembler::OopVerify_ReturnValue);
  }
  __ method_exit(frame_map());
}


void LIR_Assembler::safepoint_poll(LIR_Opr thread_reg, SafepointStub* stub) {
  assert0( stub );              // Better be something here!
  assert0( thread_reg->is_register() );
Register Rthread=thread_reg->as_register();
  __ verify_oopmap(stub->info()->oop_map(), MacroAssembler::OopVerify_OopMap_PreSafepoint);
  __ cmp4i(Rthread,in_bytes(JavaThread::please_self_suspend_offset()),0);
__ jne(*stub->entry());
  __ bind (*stub->continuation());
}

void LIR_Assembler::move_regs(Register from_reg, Register to_reg) {
__ move8(to_reg,from_reg);
}

void LIR_Assembler::const2reg(LIR_Opr src,LIR_Opr dest,LIR_PatchCode patch_code,
                              CodeEmitInfo* info, LIR_Opr tmp1, LIR_Opr tmp2) {
  // by default don't allow flags to be destroyed as moves may be in the moddle
  // of a compare and branch
  const2reg(src, dest, patch_code, info, tmp1, tmp2, false);
}

void LIR_Assembler::const2reg(LIR_Opr src,LIR_Opr dest,LIR_PatchCode patch_code,
                              CodeEmitInfo* info, LIR_Opr tmp1, LIR_Opr tmp2, bool destroy_flags) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();
  int temps_needed=0;
  switch (c->type()) {
case T_INT:__ mov8u(dest->as_register(),c->as_jint(),destroy_flags);break;
case T_LONG:__ mov8i(dest->as_register_lo(),(int64_t)c->as_jlong(),destroy_flags);break;
case T_OBJECT:
      if (patch_code != lir_patch_none) {
        temps_needed=2;
        jobject2reg_with_patching(dest->as_register(), tmp1->as_register(), tmp2->as_register(), info);
      } else {
        temps_needed=1; // don't differentiate for null due to complicated cmove cases (although we shouldn't define tmp in common null cases)
jobject obj=c->as_jobject();
if(obj==NULL){
__ mov8i(dest->as_register(),0);
        } else {
          int oop_index = ciEnv::get_OopTable_index(obj);
          // we can get here through register allocation and then won't have been given a temp register
Register Rtmp;
if(tmp1->is_valid()){
            Rtmp = tmp1->as_register();
          } else {
            Rtmp = (dest->as_register() != RAX) ? RAX : RDX;
__ push(Rtmp);
          }
          __ oop_from_OopTable(RInOuts::a, dest->as_register(), Rtmp, oop_index); // includes verify
if(!tmp1->is_valid()){
__ pop(Rtmp);
          }
        }
      }
      break;
    case T_FLOAT:
    case T_DOUBLE: {
      assert0( dest->is_xmm_register() );
Register Rtmp=noreg;//possible temp register
      bool restore_rax = false; // in the worst case we grab RAX with a push, do we need to restore it?
if(tmp1->is_valid()){
        temps_needed=1;
        Rtmp = tmp1->as_register();
      } else if( ((c->type() == T_FLOAT)  && (c->as_jint_bits()  == 0)) ||
                 ((c->type() == T_DOUBLE) && (c->as_jlong_bits() == 0)) ){
        // no temps necessary
      } else {
        if( ((c->type() == T_FLOAT)  && (__ float_constant(c->as_jfloat()) == NULL)) ||
            ((c->type() == T_DOUBLE) && (__ double_constant(c->as_jdouble()) == NULL)) ){
__ push(RAX);//Worst case, we probably got here through phi resolution of exception entry :-(
          Rtmp = RAX;
          restore_rax = true;
        }
      }
if(c->type()==T_FLOAT){
        __ float_constant(dest->as_xmm_float_reg(), c->as_jfloat(), Rtmp);
      } else {
        __ double_constant(dest->as_xmm_double_reg(), c->as_jdouble(), Rtmp);
      }
      if (restore_rax)  __ pop(RAX);
      break;
    }
    default: ShouldNotReachHere();
  }
  if(temps_needed < 2) assert0( !tmp2->is_valid() );
  if(temps_needed < 1) assert0( !tmp1->is_valid() );
}

void LIR_Assembler::const2stack(LIR_Opr src, LIR_Opr dest, LIR_Opr tmp1, LIR_Opr tmp2) {
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_stack(), "should not call otherwise");
  LIR_Const* c = src->as_constant_ptr();
  int temps_needed = 0;

  switch (c->type()) {
  case T_INT:  // fall through
  case T_FLOAT: {
Address addr=frame_map()->address_for_slot(dest->single_stack_ix());
    __ st4i(addr._base, addr._disp, c->as_jint_bits());
    break;
  }
  case T_OBJECT: {
Address addr=frame_map()->address_for_slot(dest->single_stack_ix());
jobject obj=c->as_jobject();
if(obj==NULL){
      __ st8i(addr._base, addr._disp, 0);
    } else {
      temps_needed = 2;
      Register Rtmp1, Rtmp2;
      // If const2mem is caused by a spill then we don't get a temp, create one
      // using a push. This is safe as no GC can occur although oop_from_OopTable
      // contains an ldref_lvb.
      if (!tmp1->is_valid()) { Rtmp1 = RAX; __ push(Rtmp1); addr._disp+=8; }
      else                     Rtmp1 = tmp1->as_register();
      if (!tmp2->is_valid()) { Rtmp2 = RDX; __ push(Rtmp2); addr._disp+=8; }
      else                     Rtmp2 = tmp2->as_register();
      int oop_index = ciEnv::get_OopTable_index(obj);
      __ oop_from_OopTable(RInOuts::a, Rtmp1, Rtmp2, oop_index); // includes verify
      __ verify_not_null_oop (Rtmp1, MacroAssembler::OopVerify_OopTableLoad);
      __ st8(addr._base, addr._disp, Rtmp1);
      if (!tmp2->is_valid()) { __ pop(Rtmp2); addr._disp-=8; }
      if (!tmp1->is_valid()) { __ pop(Rtmp1); addr._disp-=8; }
    }
    break;
  }
  case T_LONG:  // fall through
  case T_DOUBLE: {
    Untested();
Address addr=frame_map()->address_for_slot(dest->double_stack_ix());
    if (is_small_constant(src)) {
      __ st8i(addr._base, addr._disp, c->as_jlong_bits());
    } else {
      __ st4i(addr._base, addr._disp,   c->as_jint_lo_bits());
      __ st4i(addr._base, addr._disp+4, c->as_jint_hi_bits());
    }
  }
  default: ShouldNotReachHere();
  }
  if(temps_needed < 2) assert0( !tmp2->is_valid() );
  if(temps_needed < 1) assert0( !tmp1->is_valid() );
}

void LIR_Assembler::const2mem ( LIR_Opr src, LIR_Opr dest, LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3,
				BasicType type, CodeEmitInfo* info ) 
{
  assert(src->is_constant(), "should not call otherwise");
  assert(dest->is_address(), "should not call otherwise");

  LIR_Const* c      = src->as_constant_ptr();
  LIR_Address* addr = dest->as_address_ptr();
  LIR_Opr base      = addr->base();
Register Rbase=base->as_register();
  int disp_value    = addr->disp();
int offset;//offset where implicit NPE info will be added

  if ( (base->type() == T_OBJECT || base->type() == T_ARRAY) &&
       (type != T_OBJECT && type != T_ARRAY) ) {
    assert0( MultiMapMetaData || base->is_destroyed() );
    __ verify_oop(Rbase, MacroAssembler::OopVerify_StoreBase);
    __ cvta2va(Rbase);
  }
  if ( type == T_OBJECT || type == T_ARRAY ) {
jobject obj=c->as_jobject();
    offset = -1; // null checks are explicit or not possible
if(obj==NULL){
if(base->type()==T_OBJECT||base->type()==T_ARRAY){
        __ verify_not_null_oop(Rbase, MacroAssembler::OopVerify_StoreBase);
        __ cvta2va(Rbase);
      }
      assert(!tmp1->is_valid() && !tmp2->is_valid() && !tmp3->is_valid(), "check");
      if(addr->index() != LIR_OprFact::illegalOpr) __ st8i(Rbase, disp_value, addr->index()->as_register(), addr->scale(), 0);
      else                                         __ st8i(Rbase, disp_value, 0);
    } else {
Register Rtmp1=tmp1->as_register();
Register Rtmp2=tmp2->as_register();
Register Rtmp3=tmp3->as_register();
      int oop_index = ciEnv::get_OopTable_index(obj);
      __ oop_from_OopTable(RInOuts::a, Rtmp1, Rtmp2, oop_index);  // includes verify
      __ verify_not_null_oop(Rtmp1, MacroAssembler::OopVerify_OopTableLoad);
      if( Rbase == RSP ) {
        __ st8(RSP, disp_value, Rtmp1);
      } else {
        Register Rindex = (addr->index() == LIR_OprFact::illegalOpr) ? noreg : addr->index()->as_register();
        int scale       = (addr->index() == LIR_OprFact::illegalOpr) ? 0     : addr->scale();
        __ ref_store_with_check(RInOuts::a, Rbase, Rtmp2, Rtmp3, RKeepIns::a, Rindex, disp_value, scale, Rtmp1, info);
      }
    }
}else if(type==T_LONG||type==T_DOUBLE){
    assert(tmp1->is_valid() && !tmp2->is_valid() && !tmp3->is_valid(), "check");
    if (is_small_constant(src)) {
      offset = __ rel_pc(); // offset where NPE will occur
      if(addr->index() == LIR_OprFact::illegalOpr) __ st8i(Rbase, disp_value, c->as_jlong_bits());
      else                                         __ st8i(Rbase, disp_value, addr->index()->as_register(), addr->scale(), c->as_jlong_bits());
    } else {
Register Rtmp=tmp1->as_register();
      __ mov8i(Rtmp, (int64_t)c->as_jlong_bits());
      offset = __ rel_pc(); // offset where NPE will occur
      if(addr->index() == LIR_OprFact::illegalOpr) __ st8(Rbase, disp_value, Rtmp);
      else                                         __ st8(Rbase, disp_value, addr->index()->as_register(), addr->scale(), Rtmp);
    }
  } else {
    assert(!tmp1->is_valid() && !tmp2->is_valid() && !tmp3->is_valid(), "check");
    jint val = c->as_jint_bits();
    offset = __ rel_pc(); // offset where NPE will occur
    if ( Rbase == RSP ) type = T_INT; // must always store to stack in 4 or 8byte sizes for deopt
if(addr->index()==LIR_OprFact::illegalOpr){
      switch (type) {
      case T_INT:     // fall through
      case T_FLOAT:                  __ st4i(Rbase, disp_value, val); break;
      case T_CHAR:    // fall through
      case T_SHORT:   val &= 0xFFFF; __ st2i(Rbase, disp_value, val); break;
      case T_BOOLEAN: // fall through
      case T_BYTE:    val &= 0xFF;   __ st1i(Rbase, disp_value, val); break;
      }
    } else {
Register Rindex=addr->index()->as_register();
int scale=addr->scale();
      switch (type) {
      case T_INT:     // fall through
      case T_FLOAT:                  __ st4i(Rbase, disp_value, Rindex, scale, val); break;
      case T_CHAR:    // fall through
      case T_SHORT:   val &= 0xFFFF; __ st2i(Rbase, disp_value, Rindex, scale, val); break;
      case T_BOOLEAN: // fall through
      case T_BYTE:    val &= 0xFF;   __ st1i(Rbase, disp_value, Rindex, scale, val); break;
      }
    }
  }
  if (info != NULL && offset != -1) add_debug_info_for_null_check(offset, info);
}

void LIR_Assembler::reg2reg(LIR_Opr src, LIR_Opr dest) {
  assert(src->is_register(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");

   // move between cpu-registers
  if (dest->is_single_cpu()) {
if(src->type()==T_LONG){
      // Can do LONG -> OBJECT
__ move8(dest->as_register(),src->as_register_lo());
}else if(src->is_single_xmm()){
      // floatToIntBits
__ mov4(dest->as_register(),src->as_xmm_float_reg());
    } else {
      assert(src->is_single_cpu(), "must match");
if(src->type()==T_OBJECT||src->type()==T_ARRAY){
        if( src->as_register() != dest->as_register() ) {
          __ verify_oop(src->as_register(), MacroAssembler::OopVerify_Move);
        }
__ move8(dest->as_register(),src->as_register());
      } else {
__ move4(dest->as_register(),src->as_register());
      }
    }
  } else if (dest->is_double_cpu()) {
if(src->type()==T_OBJECT||src->type()==T_ARRAY){
      // Surprising to me but we can see move of a long to t_object
__ move8(dest->as_register_lo(),src->as_register());
      if( src->as_register() != dest->as_register() ) {
        __ verify_oop(dest->as_register(), MacroAssembler::OopVerify_Move);
      }
    } else if (src->is_double_xmm()) {
      // doubleToLongBits
      Register t_lo = dest->as_register_lo();
#ifdef ASSERT
      Register t_hi = dest->as_register_hi();
      assert(t_hi == t_lo, "must be same");
#endif
__ mov8(t_lo,src->as_xmm_double_reg());
    } else {
      assert( src->is_double_cpu(), "must match");
      Register f_lo = src->as_register_lo();
      Register t_lo = dest->as_register_lo();
#ifdef ASSERT
      Register f_hi = src->as_register_hi();
      Register t_hi = dest->as_register_hi();
assert(f_hi==f_lo,"must be same");
      assert(t_hi == t_lo, "must be same");
#endif
      __ move8(t_lo, f_lo);
    }
    // move between xmm-registers
  } else if (dest->is_single_xmm()) {
    if (src->is_single_cpu()) {
      // intBits2float
__ mov4(dest->as_xmm_float_reg(),src->as_register());
    } else {
      assert(src->is_single_xmm(), "must match");
__ move4(dest->as_xmm_float_reg(),src->as_xmm_float_reg());
    }
  } else if (dest->is_double_xmm()) {
if(src->is_double_cpu()){
      // longBits2double
Register lo=src->as_register_lo();
#ifdef ASSERT
Register hi=src->as_register_hi();
      assert(hi == lo, "must be same");
#endif
__ mov8(dest->as_xmm_double_reg(),lo);
    } else {
      assert(src->is_double_xmm(), "must match");
__ move8(dest->as_xmm_double_reg(),src->as_xmm_double_reg());
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::reg2stack(LIR_Opr src,LIR_Opr dest,BasicType type){
  assert(src->is_register(), "should not call otherwise");
  assert(dest->is_stack(), "should not call otherwise");

  if (src->is_single_cpu()) {
    Address dst = frame_map()->address_for_slot(dest->single_stack_ix());
    if (type == T_OBJECT || type == T_ARRAY) {
__ verify_oop(src->as_register(),MacroAssembler::OopVerify_Spill);
      __ st8 (dst._base, dst._disp, src->as_register());
    } else {
      __ st4 (dst._base, dst._disp, src->as_register());
    }
  } else if (src->is_double_cpu()) {
Address dst=frame_map()->address_for_slot(dest->double_stack_ix());
    __ st8 (dst._base, dst._disp, src->as_register());
  } else if (src->is_single_xmm()) {
    Address dst = frame_map()->address_for_slot(dest->single_stack_ix());
    __ st4(dst._base, dst._disp, src->as_xmm_float_reg());
  } else if (src->is_double_xmm()) {
Address dst=frame_map()->address_for_slot(dest->double_stack_ix());
    __ st8(dst._base, dst._disp, src->as_xmm_double_reg());
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::mem2reg(LIR_Opr src, LIR_Opr dest, LIR_Opr tmp, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool /* unaligned */) {
  assert(src->is_address(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");
  assert(!tmp->is_valid() || type == T_OBJECT || type == T_ARRAY, "unexpected temp operand");

  LIR_Address* addr = src->as_address_ptr();
  LIR_Opr base      = addr->base();
Register Rbase=base->as_register();
int disp=addr->disp();
  Register Rtmp     = tmp->is_valid() ? tmp->as_register() : noreg;

  // If we have an oop map, check it is sane
  if (info != NULL) __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);

  // Create patching stub if necessary, patching needs alignment and GC
  bool oopmap_valid_after = true;
AF_PatchingStub*patch=NULL;
  if (patch_code != lir_patch_none) {
if(base->type()==T_OBJECT||base->type()==T_ARRAY){
      // patch may cause GC so Rbase needs to be in oop map, all uses of the oop
      // map must occur before Rbase is destroyed or if Rbase == NULL
      info->oop_map()->add((VOopReg::VR)Rbase);
      oopmap_valid_after = false;
    }
    assert(disp == max_jint, "must be");
    patch = new AF_PatchingStub(_masm, info); // insert jump to patch stub
  }

  // Start load, strip Rbase if necessary
if(base->type()==T_OBJECT||base->type()==T_ARRAY){
    assert0( MultiMapMetaData || base->is_destroyed() );
    __ verify_oop(Rbase, MacroAssembler::OopVerify_LoadBase);
    __ cvta2va(Rbase);
  } else if ( type == T_OBJECT ) {
    // This is an objectRef being loaded from a non-heap address - and it does NOT
    // need an LVB.  Commonly used to pull out _pending_exception from JavaThread*.
type=T_ADDRESS;
  }
  // Generate the actual load, as we add the NPE info before we generate the instruction it must immediately follow
  if (info != NULL)  add_debug_info_for_null_check_here(info);
  if (addr->index() == LIR_OprFact::illegalOpr) { // Plain load
    switch(type) {
    case T_BOOLEAN: // fall through
    case T_BYTE  : __ lds1(dest->as_register(), Rbase, disp); break;
    case T_CHAR  : __ ldz2(dest->as_register(), Rbase, disp); break;
    case T_SHORT : __ lds2(dest->as_register(), Rbase, disp); break;
    case T_INT   : __ ldz4(dest->as_register(), Rbase, disp); break;
    case T_LONG:   // fall through
    case T_ADDRESS: __ ld8(dest->as_register(), Rbase, disp); break;
    case T_FLOAT : __ ld4(dest->as_xmm_float_reg(), Rbase, disp); break;
    case T_DOUBLE: __ ld8(dest->as_xmm_double_reg(), Rbase, disp); break;
    case T_ARRAY : // fall through
    case T_OBJECT: __ ld8(dest->as_register(),Rbase, disp); break;
    default      : ShouldNotReachHere();
    }
    int patch_offset1 = __ rel_pc()-4; // offset needing patching
    // Read barrier for objectRef loads
    int patch_offset2 = 0;
    if( type == T_ARRAY || type == T_OBJECT ) 
      patch_offset2 = __ lvb(RInOuts::a, dest->as_register(), Rtmp, RKeepIns::a, Rbase, disp, true);

    // Store information gather during code generation into patch stub
    if (patch) {
      patch->_patch_offset1 = patch_offset1;
      patch->_patch_offset2 = patch_offset2;
patching_epilog(patch);
    }

  } else {                                        // Array load
    assert0(patch_code == lir_patch_none);        // no patching array loads
Register Rindex=addr->index()->as_register();
int scale=addr->scale();
    switch(type) {
      case T_BOOLEAN: // fall through
      case T_BYTE  : __ lds1(dest->as_register(), Rbase, disp, Rindex, scale); break;
      case T_CHAR  : __ ldz2(dest->as_register(), Rbase, disp, Rindex, scale); break;
      case T_SHORT : __ lds2(dest->as_register(), Rbase, disp, Rindex, scale); break;
      case T_INT   : __ ldz4(dest->as_register(), Rbase, disp, Rindex, scale); break;
      case T_LONG:   // fall through
      case T_ADDRESS: __ ld8(dest->as_register(), Rbase, disp, Rindex, scale); break;
      case T_FLOAT : __ ld4(dest->as_xmm_float_reg(), Rbase, disp, Rindex, scale); break;
      case T_DOUBLE: __ ld8(dest->as_xmm_double_reg(), Rbase, disp, Rindex, scale); break;
      case T_ARRAY : // fall through
      case T_OBJECT: __ ldref_lvb(RInOuts::a, dest->as_register(), Rtmp, RKeepIns::a, Rbase, disp, Rindex, scale, true); break;
      default      : ShouldNotReachHere();
    }
  }

  // Oop map double check
  if (info != NULL && oopmap_valid_after)
    __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  // Check we loaded an object ref
  if (type == T_ARRAY || type == T_OBJECT) __ verify_oop(dest->as_register(), MacroAssembler::OopVerify_Load);
}

void LIR_Assembler::reg2mem(LIR_Opr src,LIR_Opr dest,
                              LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3,
                              BasicType type, LIR_PatchCode patch_code,
                              CodeEmitInfo* info, bool /* unaligned */ )
{
assert(dest->is_address(),"should not call otherwise");
assert(src->is_register(),"should not call otherwise");

LIR_Address*addr=dest->as_address_ptr();
  LIR_Opr base      = addr->base();
Register Rbase=base->as_register();
int disp=addr->disp();

  // Check stored value is sane
  if (type == T_OBJECT || type == T_ARRAY) __ verify_oop (src->as_register(), MacroAssembler::OopVerify_Store);
  // If we have an oop map, check it is sane
  if (info != NULL ) __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);

  // Create patching stub if necessary, patching needs alignment and GC
  bool oopmap_valid_after = true;
AF_PatchingStub*patch=NULL;
  if (patch_code != lir_patch_none) {
if(base->type()==T_OBJECT||base->type()==T_ARRAY){
      // patch may cause GC so Rbase needs to be in oop map, all uses of the oop
      // map must occur before Rbase is destroyed or if Rbase == NULL
      oopmap_valid_after = false;
      __ verify_oop (Rbase, MacroAssembler::OopVerify_StoreBase);
      info->oop_map()->add((VOopReg::VR)Rbase);
    }
    patch = new AF_PatchingStub(_masm, info); // insert jump to patch stub
    assert(disp == max_jint, "must be");
  }

  // Start store, strip Rbase if necessary
  if( (type != T_OBJECT && type != T_ARRAY) &&
      (base->type() == T_OBJECT || base->type() == T_ARRAY) ) {
    // NB oop stores perform a meta-data check and strip in the macro-assembler
    assert0( MultiMapMetaData || base->is_destroyed() );
    __ verify_oop(Rbase, MacroAssembler::OopVerify_StoreBase);
    __ cvta2va(Rbase);
  }
  // Generate the actual store
  int temps_needed = 0;                   // sanity check temp usage
  if (info != NULL)  add_debug_info_for_null_check_here(info);
  if( Rbase == RSP && type2aelembytes[type] < 4) {
    type = T_INT; // Widen small out going stack args
  }
  if (addr->index() == LIR_OprFact::illegalOpr) { // Plain store
    switch (type) {
    case T_BOOLEAN: // fall through
    case T_BYTE:   __ st1(Rbase, disp, src->as_register());       break;
case T_SHORT://fall through
    case T_CHAR:   __ st2(Rbase, disp, src->as_register());       break;
    case T_INT:    __ st4(Rbase, disp, src->as_register());       break;
    case T_LONG:   __ st8(Rbase, disp, src->as_register());       break;
    case T_FLOAT:  __ st4(Rbase, disp, src->as_xmm_float_reg());  break;
    case T_DOUBLE: __ st8(Rbase, disp, src->as_xmm_double_reg()); break;
    case T_OBJECT:  // fall through
    case T_ARRAY:
      if( Rbase == RSP ) { // Out going stack arg
        __ st8(Rbase, disp, src->as_register());
      } else {
        temps_needed=2;
        if (patch_code != lir_patch_none) { // Generate patchable offset into temp register
          temps_needed++;
Register Roffset=tmp3->as_register();
          __ mov8i(Roffset, disp); // generate patchable offset
          patch->_patch_offset1 = __ rel_pc()-4;
patching_epilog(patch);
          patch = NULL; // don't reinstall patch
          __ ref_store_with_check(RInOuts::a, Rbase, tmp1->as_register(), tmp2->as_register(),
                                  RKeepIns::a, Roffset, 0, 0, src->as_register(), info);
        } else {
          __ ref_store_with_check(RInOuts::a, Rbase, tmp1->as_register(), tmp2->as_register(),
                                  RKeepIns::a, noreg, disp, 0, src->as_register(), info);
        }
        oopmap_valid_after = oopmap_valid_after && !UseSBA;
      }
      break;
    default      : ShouldNotReachHere();
    }

    // Store information gather during code generation into patch stub
    if( patch ) {
      patch->_patch_offset1 = __ rel_pc()-4;
patching_epilog(patch);
    }

  } else {                                         // Array store
    assert0( patch_code == lir_patch_none );       // no patching needed for array stores
Register Rindex=addr->index()->as_register();
int scale=addr->scale();
    switch (type) {
    case T_BOOLEAN: // fall through
    case T_BYTE:   __ st1(Rbase, disp, Rindex, scale, src->as_register());       break;
case T_SHORT://fall through
    case T_CHAR:   __ st2(Rbase, disp, Rindex, scale, src->as_register());       break;
    case T_INT:    __ st4(Rbase, disp, Rindex, scale, src->as_register());       break;
    case T_LONG:   __ st8(Rbase, disp, Rindex, scale, src->as_register());       break;
    case T_FLOAT:  __ st4(Rbase, disp, Rindex, scale, src->as_xmm_float_reg());  break;
    case T_DOUBLE: __ st8(Rbase, disp, Rindex, scale, src->as_xmm_double_reg()); break;
    case T_OBJECT:  // fall through
    case T_ARRAY:
      temps_needed=2;
      __ ref_store_with_check(RInOuts::a, Rbase, tmp1->as_register(), tmp2->as_register(),
                              RKeepIns::a, Rindex, disp, scale, src->as_register(), info);
      oopmap_valid_after = oopmap_valid_after && !UseSBA;
      break;
    default      : ShouldNotReachHere();
    }
  }
  // Check temp usage
  assert0( !tmp1->is_valid() || (temps_needed > 0) );
  assert0( !tmp2->is_valid() || (temps_needed > 1) );
  assert0( !tmp3->is_valid() || (temps_needed > 2) );
  // Oop map double check
  if (info != NULL && oopmap_valid_after)
    __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
}

void LIR_Assembler::stack2reg(LIR_Opr src, LIR_Opr dest, BasicType type) {
  assert(src->is_stack(), "should not call otherwise");
  assert(dest->is_register(), "should not call otherwise");

Address saddr;
if(src->is_double_stack()){
saddr=frame_map()->address_for_slot(src->double_stack_ix());
  } else {
saddr=frame_map()->address_for_slot(src->single_stack_ix());
  }
  if (dest->is_single_cpu()) {
    if (type == T_ARRAY || type == T_OBJECT) {
      __ ld8(dest->as_register(), saddr._base, saddr._disp);
      __ verify_oop(dest->as_register(), MacroAssembler::OopVerify_Fill);
    } else {
      __ ldz4(dest->as_register(), saddr._base, saddr._disp);
    }
  } else if (dest->is_double_cpu()) {
    __ ld8(dest->as_register(), saddr._base, saddr._disp);
  } else if (dest->is_single_xmm()) {
    __ ld4(dest->as_xmm_float_reg(), saddr._base, saddr._disp);
  } else if (dest->is_double_xmm()) {
    __ ld8(dest->as_xmm_double_reg(), saddr._base, saddr._disp);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::stack2stack(LIR_Opr src, LIR_Opr dst, LIR_Opr tmp, BasicType type) {
assert(src->is_stack(),"should not call otherwise");
assert(dst->is_stack(),"should not call otherwise");
  if (src == dst) {
    return;
  }

Address saddr;
Address daddr;
if(src->is_single_stack()){
saddr=frame_map()->address_for_slot(src->single_stack_ix());
daddr=frame_map()->address_for_slot(dst->single_stack_ix());
  } else {
    assert0 (src->is_double_stack());
saddr=frame_map()->address_for_slot(src->double_stack_ix());
daddr=frame_map()->address_for_slot(dst->double_stack_ix());
  }
if(!tmp->is_valid()){
    assert0( saddr._base == RSP && daddr._base == RSP );
    __ push(saddr._base, saddr._disp); // temp = RSP[_dis]; RSP--; RSP[0] = temp
    __ pop (daddr._base, daddr._disp); // RSP++; temp = RSP[0]; RSP[_dis] = temp
  } else {
    __ ld8 (tmp->as_register(), saddr._base, saddr._disp);
    __ st8 (daddr._base, daddr._disp, tmp->as_register());
  }
}

void LIR_Assembler::prefetchr(LIR_Opr src) {
  Untested();
  int ReadPrefetchInstr = 0; // JavaSoft default (we don't support the command line flag)
  LIR_Address* addr = src->as_address_ptr();
  Address from_addr = as_Address(addr);
  if (from_addr._index != noreg) Unimplemented();
  switch (ReadPrefetchInstr) {
case 0:__ prefetchnta(from_addr._base,from_addr._disp);break;
  case 1:   __ prefetch0  (from_addr._base, from_addr._disp); break;
  case 2:   __ prefetch2  (from_addr._base, from_addr._disp); break;
  default:  ShouldNotReachHere();
  }
}

void LIR_Assembler::prefetchw(LIR_Opr src) {
  Untested();
  int AllocatePrefetchInstr = 0; // JavaSoft default (we don't support the command line flag)
  LIR_Address* addr = src->as_address_ptr();
  Address from_addr = as_Address(addr);
  if (from_addr._index != noreg) Unimplemented();
  switch (AllocatePrefetchInstr) {
case 0:__ prefetchnta(from_addr._base,from_addr._disp);break;
  case 1:   __ prefetch0  (from_addr._base, from_addr._disp); break;
  case 2:   __ prefetch2  (from_addr._base, from_addr._disp); break;
  case 3:   // a prefetchw 3D Now instruction in JavaSoft
  default:  ShouldNotReachHere();
  }
}

//NEEDS_CLEANUP; // This could be static?
Address::ScaleFactor LIR_Assembler::array_element_size(BasicType type) const {
  int elem_size = type2aelembytes[type];
  switch (elem_size) {
    case 1: return Address::times_1;
    case 2: return Address::times_2;
    case 4: return Address::times_4;
    case 8: return Address::times_8;
  }
  ShouldNotReachHere();
  return Address::no_scale;
}


void LIR_Assembler::emit_op3(LIR_Op3* op) {
  __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  switch (op->code()) {
    case lir_idiv:
    case lir_irem:
arithmetic_div(op->code(),
                     op->in_opr1(),
                     op->in_opr2(),
                     op->in_opr3(),
                     op->result_opr(),
op->info(),
                     false);
      break;
    case lir_ldiv:
    case lir_lrem:
arithmetic_div(op->code(),
                     op->in_opr1(),
                     op->in_opr2(),
                     op->in_opr3(),
                     op->result_opr(),
op->info(),
                     true);
      break;
    default:      ShouldNotReachHere(); break;
  }
}

void LIR_Assembler::emit_opBranch(LIR_OpBranch* op) {
#ifdef ASSERT
  assert(op->block() == NULL || op->block()->label() == op->label(), "wrong label");
#endif

  if (op->cond() == lir_cond_always) {
    if (op->info() != NULL) add_debug_info_for_branch(op->info());
    __ jmp (*(op->label()));
  } else {
    if (op->code() == lir_cond_float_branch) {
      assert(op->ublock() != NULL, "must have unordered successor");
      Label* usucc = op->ublock()->label();
      Label* succ = op->label();
      if (usucc == succ) {
        switch(op->cond()) {
          case lir_cond_equal:                        __ jeq(*succ); break;
          case lir_cond_notEqual:     __ jpe(*succ);  __ jne(*succ); break;
          case lir_cond_less:                         __ jbl(*succ); break;
          case lir_cond_lessEqual:                    __ jbe(*succ); break;
          case lir_cond_greaterEqual: __ jpe(*succ);  __ jae(*succ); break;
          case lir_cond_greater:      __ jpe(*succ);  __ jab(*succ); break;
          default:                    ShouldNotReachHere();
        }
      } else {
        switch(op->cond()) {
          case lir_cond_equal:        __ jpe(*usucc); __ jeq(*succ); break;
          case lir_cond_notEqual:     __ jpe(*usucc); __ jne(*succ); break;
          case lir_cond_less:         __ jpe(*usucc); __ jbl(*succ); break;
          case lir_cond_lessEqual:    __ jpe(*usucc); __ jbe(*succ); break;
          case lir_cond_greaterEqual: __ jpe(*usucc); __ jae(*succ); break;
          case lir_cond_greater:      __ jpe(*usucc); __ jab(*succ); break;
          default:                    ShouldNotReachHere();
        }
      }
    } else {
      switch (op->cond()) {
      case lir_cond_equal:        __ jeq(*(op->label())); break;
      case lir_cond_notEqual:     __ jne(*(op->label())); break;
      case lir_cond_less:         __ jlt(*(op->label())); break;
      case lir_cond_lessEqual:    __ jle(*(op->label())); break;
      case lir_cond_greaterEqual: __ jge(*(op->label())); break;
      case lir_cond_greater:      __ jgt(*(op->label())); break;
      case lir_cond_belowEqual:   __ jbe(*(op->label())); break;
      case lir_cond_aboveEqual:   __ jae(*(op->label())); break;
      case lir_cond_carry:        __ jbl(*(op->label())); break;
      case lir_cond_notCarry:     __ jnb(*(op->label())); break;
      default:                    ShouldNotReachHere();
      }
    }
  }
}

void LIR_Assembler::emit_opConvert(LIR_OpConvert* op) {
  LIR_Opr src  = op->in_opr();
  LIR_Opr dest = op->result_opr();
LIR_Opr tmp1=op->tmp1_opr();
  assert (!op->tmp2_opr()->is_valid() && !op->tmp2_opr()->is_valid(), "wasting temp regs");

  switch (op->bytecode()) {
    case Bytecodes::_i2f:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_i2f(dest->as_xmm_float_reg(),src->as_register());
      break;

case Bytecodes::_i2d:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_i2d(dest->as_xmm_double_reg(),src->as_register());
      break;

    case Bytecodes::_i2l:
assert(!tmp1->is_valid(),"wasting temp reg");
__ movsx84(dest->as_register_lo(),src->as_register());
      break;

    case Bytecodes::_i2s:
assert(!tmp1->is_valid(),"wasting temp reg");
__ movsx82(dest->as_register(),src->as_register());
      break;

    case Bytecodes::_i2b:
assert(!tmp1->is_valid(),"wasting temp reg");
__ movsx81(dest->as_register(),src->as_register());
      break;

    case Bytecodes::_i2c:
assert(!tmp1->is_valid(),"wasting temp reg");
__ movzx82(dest->as_register(),src->as_register());
      break;

    case Bytecodes::_l2f:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_l2f(dest->as_xmm_float_reg(),src->as_register_lo());
      break;

    case Bytecodes::_l2d:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_l2d(dest->as_xmm_double_reg(),src->as_register_lo());
      break;

    case Bytecodes::_l2i:
assert(!tmp1->is_valid(),"wasting temp reg");
__ mov4(dest->as_register(),src->as_register_lo());
      break;

    case Bytecodes::_f2d:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_f2d(dest->as_xmm_double_reg(),src->as_xmm_float_reg());
      break;

    case Bytecodes::_d2f:
assert(!tmp1->is_valid(),"wasting temp reg");
__ cvt_d2f(dest->as_xmm_float_reg(),src->as_xmm_double_reg());
      break;

    case Bytecodes::_f2i:
assert(tmp1->is_valid()&&tmp1->is_register(),"must be");
      __ corrected_f2i(dest->as_register(), src->as_xmm_float_reg(), tmp1->as_register());
      break;

    case Bytecodes::_d2i:
assert(tmp1->is_valid()&&tmp1->is_register(),"must be");
      __ corrected_d2i(dest->as_register(), src->as_xmm_double_reg(), tmp1->as_register());
      break;

    case Bytecodes::_f2l:
assert(tmp1->is_valid()&&tmp1->is_register(),"must be");
      __ corrected_f2l(dest->as_register_lo(), src->as_xmm_float_reg(), tmp1->as_register());
      break;

    case Bytecodes::_d2l:
assert(tmp1->is_valid()&&tmp1->is_register(),"must be");
      __ corrected_d2l(dest->as_register_lo(), src->as_xmm_double_reg(), tmp1->as_register());
      break;

    default: ShouldNotReachHere();
  }
}


void LIR_Assembler::emit_alloc_obj(LIR_OpAlloc*op){
  assert0( RDX == op->bytesize()->as_register() );
  assert0( RCX == op->len ()->as_register() );
  assert0( RSI == op->kid ()->as_register() );
  assert0( RAX == op->obj ()->as_register() );
  assert0( R09 == op->tmp1()->as_register() );

if(op->always_slow_path()){
    __ move8(RAX, op->klass_reg()->as_register());
    __ cvta2va(RAX);
    __ ldz4 (RDX,RAX, Klass::layout_helper_offset_in_bytes() + sizeof(oopDesc));
    __ ldz4 (RSI,RAX, klassOopDesc::klass_part_offset_in_bytes() + Klass::klassId_offset_in_bytes());
    __ jmp  (*op->stub()->entry());
  } else {
assert(!op->klass_reg()->is_valid(),"unnecessary definition of klass_reg as kid is known");
    __ mov8i(RDX, op->klass()->as_instance_klass()->size_helper()<< LogBytesPerWord);
if(op->init_test()){
      add_debug_info_for_null_check_here(op->stub()->info());
      __ kid2klass(RInOuts::a, RAX, R09 /*temp*/, op->klass()->klassId());
      __ verify_not_null_oop(RAX, MacroAssembler::OopVerify_OopTableLoad);
      __ mov8i(RSI, (int32_t)op->klass()->klassId());
      __ cvta2va(RAX);
      __ cmp4i(RAX, instanceKlass::init_state_offset_in_bytes() + sizeof(oopDesc), instanceKlass::fully_initialized);
__ jne(*op->stub()->entry());
    } else {
      __ mov8i(RSI, (int32_t)op->klass()->klassId());
    }
    __ allocate(RDX,RCX,RSI,RAX, UseSBA ? StubRoutines::new_sba() : StubRoutines::new_fast(), *op->stub()->entry());
  }
  __ bind (*op->stub()->continuation());
  // NB RCX always holds the result to be consistent with the interpreter, if we
  // call a stub then RAX also holds the result in case of a deopt
  __ verify_not_null_oop(RAX, MacroAssembler::OopVerify_NewOop);
}


void LIR_Assembler::emit_alloc_array(LIR_OpAlloc*op){
  assert0( RDX == op->bytesize()->as_register() );
  assert0( RCX == op->len ()->as_register() );
  assert0( RSI == op->kid ()->as_register() );
  assert0( RAX == op->obj ()->as_register() );
  assert0( R09 == op->tmp1()->as_register() );

  // Set up registers:
  Register RDX_size = RDX; // size of object
  Register RCX_len  = RCX; // [element klass ID] [array length]
  Register RSI_akid = RSI; // klass ID of array
if(op->always_slow_path()){
    // CNC: I dont believe we need to slow-path arrays just because
    // their elements are abstract or not loaded.
    assert( op->type() == T_OBJECT || op->type() == T_ARRAY, "Primitive arrays shouldn't be on slow path" );    
    // Temps:
    Register RAX_aklass = RAX; // array's klass
    Register RDX_eklass = RDX; // array element's klass
    Register RAX_ekid   = RAX; // array element's klass ID
    Register RSI_tmp    = RSI; // temp for lvb computation
    __ cvta2va(RAX_aklass, op->klass_reg()->as_register());
    __ ldref_lvb(RInOuts::a, RDX_eklass, RSI_tmp, RKeepIns::a, RAX_aklass, sizeof(oopDesc) + objArrayKlass::element_klass_offset_in_bytes() , false);
    __ verify_not_null_oop(RDX_eklass, MacroAssembler::OopVerify_Load);
    __ ldz4 (RSI_akid,RAX_aklass, klassOopDesc::klass_part_offset_in_bytes() + Klass::klassId_offset_in_bytes());
    __ cvta2va (RDX_eklass);
    __ ldz4 (RAX_ekid, RDX_eklass, sizeof(oopDesc) + Klass::klassId_offset_in_bytes());
    __ shl8i(RAX_ekid, 32);
    __ lea  (RDX_size, noreg, arrayOopDesc::header_size_in_bytes(T_OBJECT), RCX_len, 3);
    __ or_8 (RCX_len, RAX_ekid); // combine length and ekid for header, use of fixed register => destruction ok
    __ jmp  (*op->stub()->entry());
  } else {
    // Temps:
    Register RAX_ekid   = RAX; // array element's klass ID
    __ lea  (RDX_size, noreg, arrayOopDesc::header_size_in_bytes(op->type())+7, RCX, type2logaelembytes[op->type()]);
    __ and8i(RDX_size, -8); // 8byte align size
    __ mov8i(RSI_akid, (int32_t)op->klass()->klassId());
if(op->type()==T_OBJECT||op->type()==T_ARRAY){
      __ mov8i(RAX_ekid, (int64_t)op->klass()->element_klassId() << 32);
      __ or_8 (RCX_len, RAX_ekid); // combine length and ekid for header, use of fixed register => destruction ok
    }
    // -ve and > max_array_allocation_length allocations will fail in fast stub with a return of null
    // inline allocation
    __ allocate(RDX,RCX,RSI,RAX, UseSBA ? StubRoutines::new_sba_array() : StubRoutines::new_fast_array(), *op->stub()->entry());
  }
  __ bind (*op->stub()->continuation());
  __ verify_not_null_oop(RAX, MacroAssembler::OopVerify_NewOop); // NB if we called Runtime1 then RAX also holds result in case of deopt stub
}


void LIR_Assembler::emit_opTypeCheck(LIR_OpTypeCheck* op) {
  LIR_Code code = op->code();
SimpleExceptionStub*stub=(SimpleExceptionStub*)op->stub();

  if (code == lir_store_check) {
    // Perform array store check. Notes:
    // - a store of constant null doesn't generate this check, however,
    //   the register containing value may still be null
    // - the stub routine will throw the exception
    assert (op->array()->is_register() && op->object()->is_register(), "must be");
assert(!op->result_opr()->is_valid(),"result operand is unused");
    assert (!op->tmp1()->is_valid() && !op->tmp2()->is_valid() &&
            !op->tmp3()->is_valid() && !op->tmp4()->is_valid() &&
            !op->tmp5()->is_valid(), "expected temp unused");
assert(op->array()->is_destroyed(),"array is destroyed during computation");
assert(op->object()->is_destroyed(),"value is destroyed during computation");

Register Rvalue=op->object()->as_register();//input value to be stored
Register Rarray=op->array()->as_register();//input array to be stored into
    Label done;

    // 1) if element is null then nothing to check
    __ verify_oop (Rvalue, MacroAssembler::OopVerify_Move);
    Register Rvalue_kid = Rvalue;
    assert( Rvalue_kid  == R11, "must be for stub" );
    if( KIDInRef ) {
      __ ref2kid(Rvalue_kid, Rvalue);
      __ jze  (done);            // store of null always ok
    } else {
      __ null_chk(Rvalue, done); // store of null always ok
      __ ref2kid(Rvalue_kid, Rvalue);
    }

    // Rarray = Rarray.ekid
    Register Rarray_ekid = Rarray;
    assert( Rarray_ekid == RAX, "must be for stub" );
    __ verify_oop (Rarray, MacroAssembler::OopVerify_Move);
    __ cvta2va(Rarray);
    __ ldz4   (Rarray_ekid, Rarray, objArrayOopDesc::ekid_offset_in_bytes());

    // 2) matching kid values then done
    __ cmp4   (Rarray_ekid, Rvalue_kid);
__ jeq(done);

    // 3) store to Object[] then done
    __ cmp4i  (Rarray_ekid, java_lang_Object_kid);
    __ jne    (*stub->entry()); // go to code stub for full type check and raise exception

    __ bind   (*stub->continuation());
    __ bind(done);
  } else {
    assert0 (code == lir_checkcast || code == lir_instanceof);
    // CheckCast:  if opr op->object is not a subclass of op->klass(), then
    //             branch to the stub which will throw a class cast exception.
    // InstanceOf: if opr op->object is not a subclass of op->klass(), then
    //             the result is 0 else the result is 1
    assert (code == lir_checkcast || stub == NULL, "no stub expected for instanceof");
    assert (!op->object()->is_destroyed(), "object is preserved");
Register Robj=op->object()->as_register();//register holding object to cast
    assert (lir_instanceof || Robj != stub->obj()->as_register(), "must not pass destoyed register to exception stub");
    ciKlass* k = op->klass();                        // klass being cast to
Register Rdst=op->result_opr()->as_register();//1 or 0 for instanceof, obj for checkcast

    // Initialization and handle case when Robj is NULL
if(code==lir_checkcast){
      __ mov8 (Rdst, Robj);    
    } else {
      __ xor4 (Rdst, Rdst);
    }
    Label done, not_null;
    __ test8(Robj,Robj);
    if( UseC2 ) {
      __ jne  (not_null);
Register cp_reg=op->cp_reg()->as_register();
      __ bts4i(cp_reg, op->cpdoff(), 0); // matched CPData_Null::_null
      __ jmp  (done);
__ bind(not_null);
    } else {
__ jeq(done);
    }
    if (op->fast_check()) {
      // Case 1: class loaded && fast_check (known class with no subclasses)
      //         if (ref2kid(obj) != T.kid) throw ...
      assert (!op->tmp2()->is_valid() && !op->tmp3()->is_valid() &&
              !op->tmp4()->is_valid() && !op->tmp5()->is_valid(), "unexpected definition of temps");
Register Rtmp1_kid=op->tmp1()->as_register();
      // 1) Trivial equality test suffices
      __ ref2kid (Rtmp1_kid,Robj);
      __ cmp4i   (Rtmp1_kid, k->klassId());
      if (code == lir_checkcast) __ jne  (*stub->entry());
      else                       __ setz (Rdst);
    } else if (k->is_loaded() && k->super_check_offset() != sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes()) {
      // Case 2: class loaded && T.super_check_offset != secondary_super_cache_offset
      //         if (obj.kid == T.kid) goto done
      //         if (S[T.super_check_offset] != T.kid) throw ...
      //         done:
      //   (known class for which we look in primary super class array)
      assert (!op->tmp4()->is_valid() && !op->tmp5()->is_valid(), "unexpected definition of temps");
Register Rtmp1_kid=op->tmp1()->as_register();
Register Rtmp2_obj_klass=op->tmp2()->as_register();
Register Rtmp3=op->tmp3()->as_register();
Label success;

      // 1) Trivial equality test
      __ ref2kid   (Rtmp1_kid, Robj);
      __ cmp4i     (Rtmp1_kid, k->klassId());
      __ jeq       (success);

      // 2) Is super's KID in subklass's cache?
      __ kid2klass (RInOuts::a, Rtmp2_obj_klass, Rtmp3, RKeepIns::a, Rtmp1_kid);
      __ cvta2va   (Rtmp2_obj_klass);
      __ cmp4i     (Rtmp2_obj_klass, k->super_check_offset(), k->klassId());
      if (code == lir_checkcast)  __ jne  (*stub->entry());
      else                        __ jne  (done);
__ bind(success);
      if (code == lir_instanceof) __ mov8i(Rdst, 1);

    } else {
      // Case 3: T not loaded || T.super_check_offset == secondary_super_cache_offset
      //         if (obj.kid == T.kid) goto done
      //         if (S[T.super_check_offset] == T.kid) goto done
      //         if (T.super_check_offset != secondary_super_cache_offset) throw ..
      //         search secondary super cache for T
      //         done:
      //   (full slow path required - known or unknown class for which we may need
      //    full search of secondary super class array)

Register Rtmp1=op->tmp1()->as_register();
Register Rtmp2=op->tmp2()->as_register();
Register Rtmp3=op->tmp3()->as_register();
      Register Rtmp4, Rtmp5;
Register Rtmp1_subkid=Rtmp1;
      Register Rtmp2_subklass   = Rtmp2;
      Register Rtmp3_superklass = Rtmp3;
      Label failure, success;

      __ ref2kid (Rtmp1_subkid, Robj);
      if (k->is_loaded()) {
        assert (!op->tmp4()->is_valid() && !op->tmp5()->is_valid(), "unexpected definition of temps");
        Rtmp4 = Rtmp5 = noreg;

        // 1) Trivial equality test
        __ cmp4i   (Rtmp1_subkid, k->klassId());
        __ jeq     (success);
        // 2) Is super's KID in subklass's cache?
        __ kid2klass (RInOuts::a, Rtmp2_subklass, Rtmp3, RKeepIns::a, Rtmp1_subkid);
        __ cvta2va   (Rtmp2_subklass);
        __ cmp4i     (Rtmp2_subklass, k->super_check_offset(), k->klassId());
        __ jeq       (success); // Then it's a hit: we are a subtype

        // No (3) as we just checked in the secondary list's cache, set up Rtmp3_superklass
        __ kid2klass (RInOuts::a, Rtmp3_superklass, Rtmp1, k->klassId());
        __ cvta2va(Rtmp3_superklass);
      } else {
Rtmp4=op->tmp4()->as_register();
Rtmp5=op->tmp5()->as_register();
        Register Rtmp4_superkid        = Rtmp4;
        Register Rtmp5_super_check_off = Rtmp5;
        // NB may trigger GC and blow non-live oops!
        jobject2reg_with_patching(Rtmp3_superklass, __ cheapest_encodable_register(Rtmp2,Rtmp4), Rtmp5, op->info_for_patch());
        if (code == lir_checkcast) { // recopy result in case GC blew it
          __ verify_oop (Robj, MacroAssembler::OopVerify_Move);
          __ mov8 (Rdst, Robj);
        }

        __ cvta2va(Rtmp3_superklass);
        __ ldz4   (Rtmp4_superkid, Rtmp3_superklass, sizeof(oopDesc) + Klass::klassId_offset_in_bytes());
        // 1) Trivial equality test
        __ cmp4   (Rtmp1_subkid, Rtmp4_superkid);
        __ jeq    (success);
        // 2) Is super's KID in subklass's cache?
        __ kid2klass (RInOuts::a, Rtmp2_subklass, Rtmp5, RKeepIns::a, Rtmp1_subkid);
        __ ldz4   (Rtmp5_super_check_off, Rtmp3_superklass, sizeof(oopDesc) + Klass::super_check_offset_offset_in_bytes());
        __ cvta2va(Rtmp2_subklass);
        __ cmp4   (Rtmp4_superkid, Rtmp2_subklass, 0, Rtmp5_super_check_off, 0);
        __ jeq    (success); // Then it's a hit: we are a subtype
        // 3) Did we check in the direct subtype list or in the secondary list's cache?
        __ cmp4i(Rtmp5_super_check_off, (sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes()));
        if (code == lir_checkcast) __ jne (*stub->entry()); // checked in direct list => failure
        else                       __ jne (failure);
      }

      // 4) Slow path, loop over secondary supers array returning 1 for hit or 0 for failure
      assert( Rtmp2_subklass   == R09, "must be for stub" );
      assert( Rtmp3_superklass == RAX, "must be for stub" );
      // Do subtype check
      __ call (StubRoutines::x86::partial_subtype_check());
      __ test4(RAX, RAX);
if(code==lir_checkcast){
        __ jnz (*stub->entry()); // branch to throw class cast stub on non-zero
      } else {
        __ setz (Rdst);          // is_subtype: Rdst==0 ==> 1;  is_not_subtype: Rdst!=0 ==> 0
        __ movzx81(Rdst,Rdst);   //
        __ jmp (done);
      }
__ bind(success);
      if (code == lir_instanceof) __ mov8i (Rdst, 1);
__ bind(failure);
    }
    __ bind (done);
  }
}


void LIR_Assembler::emit_compare_and_swap(LIR_OpCompareAndSwap* op) {
Register Robj=op->obj()->as_register();
LIR_Opr offset=op->offset();
Register Rcmpval=op->cmp_value()->as_register();
Register Rnewval=op->new_value()->as_register();

  assert0( MultiMapMetaData || op->obj()->is_destroyed() );
  __ verify_oop(Robj, MacroAssembler::OopVerify_StoreBase);

  assert(Rcmpval == RAX, "wrong register");
if(op->code()==lir_cas_obj){
    // NB strip performed in ref_cas_with_check
Register Rtmp1=op->tmp1()->as_register();
Register Rtmp2=op->tmp2()->as_register();
if(offset->is_constant()){
      __ ref_cas_with_check(RInOuts::a, Robj, RAX, Rtmp1, Rtmp2, RKeepIns::a, noreg, offset->as_jlong(), 0, Rnewval, NULL);
    } else {
      __ ref_cas_with_check(RInOuts::a, Robj, RAX, Rtmp1, Rtmp2, RKeepIns::a, offset->as_register(), 0, 0, Rnewval, NULL);
    }
}else if(op->code()==lir_cas_long){
    __ cvta2va(Robj);
if(offset->is_constant()){
      intptr_t off = offset->type() == T_INT ? offset->as_jint() : offset->as_jlong();
      __ locked()->cas8(Robj, off, Rnewval);
    } else {
      __ locked()->cas8(Robj, 0, offset->as_register(), 0, Rnewval);
    }
  } else {
    assert0 (op->code() == lir_cas_int);
    __ cvta2va(Robj);
if(offset->is_constant()){
      intptr_t off = offset->type() == T_INT ? offset->as_jint() : offset->as_jlong();
      __ locked()->cas4(Robj, off, Rnewval);
    } else {
      __ locked()->cas4(Robj, 0, offset->as_register(), 0, Rnewval);
    }
  }
}


void LIR_Assembler::cmove(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Opr tmp, LIR_Opr result) {
  bool reg_2const_move = false;
  if (opr1->is_cpu_register()) {
    reg2reg(opr1, result);
  } else if (opr1->is_stack()) {
    stack2reg(opr1, result, result->type());
  } else if (opr1->is_constant()) {
BasicType type=opr1->type();
    if (!opr2->is_constant() || type == T_OBJECT || type2size[type] != 1) {
      // one temp for oop table load, preserve flags
      const2reg(opr1, result, lir_patch_none, NULL, tmp, LIR_OprFact::illegalOpr, false);
    } else {
      reg_2const_move = true;
    }
  } else {
    ShouldNotReachHere();
  }
  

  if (reg_2const_move) {
    // optimized const-const version that does not require a branch
    // TODO: possibly handle more sizes of constant conditional move
Register Rres=result->as_register();
jint x=opr1->as_constant_ptr()->as_jint();
jint y=opr2->as_constant_ptr()->as_jint();
    switch (condition) {
    case lir_cond_equal:        __ cmov4eqi(Rres,x,y); break;
    case lir_cond_notEqual:     __ cmov4nei(Rres,x,y); break;
    case lir_cond_less:         __ cmov4lti(Rres,x,y); break;
    case lir_cond_lessEqual:    __ cmov4lei(Rres,x,y); break;
    case lir_cond_greaterEqual: __ cmov4gei(Rres,x,y); break;
    case lir_cond_greater:      __ cmov4gti(Rres,x,y); break;
    case lir_cond_belowEqual:   __ cmov4bei(Rres,x,y); break;
    case lir_cond_aboveEqual:   __ cmov4aei(Rres,x,y); break;
    default: ShouldNotReachHere();
    }
  } else if (!opr2->is_constant()) {
    // optimized reg-reg version that does not require a branch
    if (opr2->is_single_cpu() && opr2->type() != T_OBJECT && opr2->type() != T_ARRAY) {
      assert(opr2->cpu_regnr() != result->cpu_regnr(), "opr2 already overwritten by previous move");
Register Rres=result->as_register();
Register Rop2=opr2->as_register();
      switch (condition) {
      case lir_cond_equal:        __ cmov4ne(Rres,Rop2); break;
      case lir_cond_notEqual:     __ cmov4eq(Rres,Rop2); break;
      case lir_cond_less:         __ cmov4ge(Rres,Rop2); break;
      case lir_cond_lessEqual:    __ cmov4gt(Rres,Rop2); break;
      case lir_cond_greaterEqual: __ cmov4lt(Rres,Rop2); break;
      case lir_cond_greater:      __ cmov4le(Rres,Rop2); break;
      case lir_cond_belowEqual:   __ cmov4ab(Rres,Rop2); break;
      case lir_cond_aboveEqual:   __ cmov4bl(Rres,Rop2); break;
      default: ShouldNotReachHere();
      }

    } else if (opr2->is_double_cpu() || opr2->type() == T_ARRAY || opr2->type() == T_OBJECT) {
      assert(opr2->cpu_regnr() != result->cpu_regnr(), "opr2 already overwritten by previous move");
Register Rres=result->as_register();
Register Rop2=opr2->as_register();
      switch (condition) {
      case lir_cond_equal:        __ cmov8ne(Rres,Rop2); break;
      case lir_cond_notEqual:     __ cmov8eq(Rres,Rop2); break;
      case lir_cond_less:         __ cmov8ge(Rres,Rop2); break;
      case lir_cond_lessEqual:    __ cmov8gt(Rres,Rop2); break;
      case lir_cond_greaterEqual: __ cmov8lt(Rres,Rop2); break;
      case lir_cond_greater:      __ cmov8le(Rres,Rop2); break;
      case lir_cond_belowEqual:   __ cmov8ab(Rres,Rop2); break;
      case lir_cond_aboveEqual:   __ cmov8bl(Rres,Rop2); break;
      default: ShouldNotReachHere();
      }

    } else if (opr2->is_single_stack()) {
      Unimplemented();
      //Address *addr = frame_map()->address_for_slot(opr2->single_stack_ix());
      //__ cmov4(ncond, result->as_register(), addr);
    } else if (opr2->is_double_stack()) {
      Unimplemented();
      //__ cmov8(ncond, result->as_register(), frame_map()->address_for_slot(opr2->double_stack_ix(), lo_word_offset_in_bytes));
    } else {
      ShouldNotReachHere();
    }

  } else {
    Label skip;
    switch (condition) {
    case lir_cond_equal:        __ jeq(skip); break;
    case lir_cond_notEqual:     __ jne(skip); break;
    case lir_cond_less:         __ jlt(skip); break;
    case lir_cond_lessEqual:    __ jle(skip); break;
    case lir_cond_greaterEqual: __ jge(skip); break;
    case lir_cond_greater:      __ jgt(skip); break;
    case lir_cond_belowEqual:   __ jbe(skip); break;
    case lir_cond_aboveEqual:   __ jae(skip); break;
    default: ShouldNotReachHere();
    }
    if (opr2->is_cpu_register()) {
      reg2reg(opr2, result);
    } else if (opr2->is_stack()) {
      stack2reg(opr2, result, result->type());
    } else if (opr2->is_constant()) {
      const2reg(opr2, result, lir_patch_none, NULL, tmp, LIR_OprFact::illegalOpr, true);  // one temp for oop table load
    } else {
      ShouldNotReachHere();
    }
    __ bind(skip);
  }
}

static void float_arith_reg_reg(C1_MacroAssembler* _masm, LIR_Code code, LIR_Opr dest, LIR_Opr right) {
FRegister rreg=right->as_xmm_float_reg();
  FRegister dreg = dest->as_xmm_float_reg();
  switch (code) {
case lir_add:__ addf(dreg,rreg);break;
case lir_sub:__ subf(dreg,rreg);break;
case lir_mul://fall through
    case lir_mul_strictfp: __ mulf(dreg, rreg); break;
case lir_div://fall through
    case lir_div_strictfp: __ divf(dreg, rreg); break;
    case lir_rem: __ remf(dreg, rreg); break;
    default: ShouldNotReachHere();
  } 
}

static void double_arith_reg_reg(C1_MacroAssembler* _masm, LIR_Code code, LIR_Opr dest, LIR_Opr right) {
FRegister rreg=right->as_xmm_double_reg();
  FRegister dreg = dest->as_xmm_double_reg();
  switch (code) {
case lir_add:__ addd(dreg,rreg);break;
case lir_sub:__ subd(dreg,rreg);break;
case lir_mul://fall through
    case lir_mul_strictfp: __ muld(dreg, rreg); break;
case lir_div://fall through
    case lir_div_strictfp: __ divd(dreg, rreg); break;
    case lir_rem: __ remd(dreg, rreg); break;
    default: ShouldNotReachHere();
  }
}

static void long_arith_reg_reg(C1_MacroAssembler* _masm, LIR_Code code, LIR_Opr dest, LIR_Opr right) {
Register rreg=right->as_register_lo();
Register dreg=dest->as_register_lo();
  switch (code) {
case lir_add:__ add8(dreg,rreg);break;
case lir_sub:__ sub8(dreg,rreg);break;
case lir_mul:__ mul8(dreg,rreg);break;
    default: ShouldNotReachHere();
  }
}

static void int_arith_reg_reg(C1_MacroAssembler* _masm, LIR_Code code, LIR_Opr dest, LIR_Opr right) {
Register rreg=right->as_register();
Register dreg=dest->as_register();
  switch (code) {
case lir_add:__ add4(dreg,rreg);break;
case lir_sub:__ sub4(dreg,rreg);break;
case lir_mul:__ mul4(dreg,rreg);break;
    default: ShouldNotReachHere();
  }
}

void LIR_Assembler::arith_op(LIR_Code code,LIR_Opr left,LIR_Opr right,LIR_Opr dest,CodeEmitInfo*info){
assert(info==NULL,"unused on this code path");
  assert(left->type() != T_OBJECT && left->type() != T_ARRAY, "math on oops?");
  assert(right->type() != T_OBJECT && right->type() != T_ARRAY, "math on oops?");
  // NB JavaSoft handle x87 fpu operations that mean they can't assert this for all cases
  assert(dest == left, "left and dest must be equal");

if(dest->is_constant()){
    intptr_t base = dest->as_constant_ptr()->as_jlong();
    assert0( right->is_register() || right->is_constant() );
if(right->type()==T_INT){
      if( right->is_register() ) { // reg-to-memory case
        switch( code ) {
          //case lir_add: __ add4(noreg, base, right->as_register());  break;
        default: ShouldNotReachHere();
        }
      } else {                  // const-to-memory case
        switch( code ) {
        case lir_add: 
          if( is_int32(base) ) __ add4i(noreg, base, right->as_constant_ptr()->as_jint());  
          else {
            Unimplemented();
          }
          break;
        default: ShouldNotReachHere();
        }
      }
    } else {
      // long case
      Unimplemented();
    }
    return;
  } else if( dest->is_address() ) {
LIR_Address*adr=dest->as_address_ptr();
    assert0( left == dest );
    assert0( right->is_constant() );
    assert0( right->type() == T_INT );
    switch( code ) {
    case lir_add: 
      __ add4i(adr->base()->as_register(), adr->disp(), right->as_constant_ptr()->as_jint());  
      break;
    default: ShouldNotReachHere();
    }
    return;
  }

assert(left->is_register(),"wrong items state");
assert(dest->is_register(),"wrong items state");
if(right->is_register()){
    if (dest->is_single_cpu()) {
      assert0 (right->is_single_cpu());
      int_arith_reg_reg(_masm, code, dest, right);
    } else if (dest->is_double_cpu()) {
      assert0 (right->is_double_cpu());
      long_arith_reg_reg(_masm, code, dest, right);
    } else if (dest->is_single_xmm()) {
      assert0 (right->is_single_xmm());
      float_arith_reg_reg(_masm, code, dest, right);
    } else {
      assert0 (right->is_double_xmm() && dest->is_double_xmm());
      double_arith_reg_reg(_masm, code, dest, right);
    }
  } else if (right->is_stack()) {
if(dest->is_single_cpu()){
Address raddr=frame_map()->address_for_slot(right->single_stack_ix());
      assert0(raddr._index == noreg);
Register to=left->as_register();
      switch (code) {
      case lir_add: __ add4(to, raddr._base, raddr._disp); break;
      case lir_sub: __ sub4(to, raddr._base, raddr._disp); break;
      default:      ShouldNotReachHere();
      }
    } else if (dest->is_double_cpu()) {
      Unimplemented();
    } else if (dest->is_single_xmm()) {
Address raddr=frame_map()->address_for_slot(right->single_stack_ix());
      assert0(raddr._index == noreg);
FRegister to=left->as_xmm_float_reg();
      switch (code) {
      case lir_add: __ addf(to, raddr._base, raddr._disp); break;
      case lir_sub: __ subf(to, raddr._base, raddr._disp); break;
      case lir_mul: __ mulf(to, raddr._base, raddr._disp); break;
      case lir_div: __ divf(to, raddr._base, raddr._disp); break;
      default:      ShouldNotReachHere();
      }
    } else {
      assert0 (dest->is_double_xmm());
Address raddr=frame_map()->address_for_slot(right->double_stack_ix());
      assert0(raddr._index == noreg);
FRegister to=left->as_xmm_double_reg();
      switch (code) {
      case lir_add: __ addd(to, raddr._base, raddr._disp); break;
      case lir_sub: __ subd(to, raddr._base, raddr._disp); break;
      case lir_mul: __ muld(to, raddr._base, raddr._disp); break;
      case lir_div: __ divd(to, raddr._base, raddr._disp); break;
      default:      ShouldNotReachHere();
      }
    }
  } else {
assert(right->is_constant(),"must be constant");
if(dest->is_single_cpu()){
Register to=left->as_register();
      jint      c = right->as_constant_ptr()->as_jint();
      switch (code) {
        case lir_add:  __ add4i(to, c); break;
        case lir_sub:  __ sub4i(to, c); break;
        case lir_mul:  __ mul4i(to, to, c); break;
        default: ShouldNotReachHere();
      }
    } else if( dest->is_double_cpu() ) {
      jlong     c = right->as_constant_ptr()->as_jlong();
assert(is_int32(c),"LIRGenerator should force non 32bit constants into registers");
Register to=dest->as_register_lo();
      switch (code) {
        case lir_add:  __ add8i(to, c); break;
        case lir_sub:  __ sub8i(to, c); break;
        case lir_mul:  __ mul8i(to, to, c); break;
        default: ShouldNotReachHere();
      }
    } else {
ShouldNotReachHere();//XMM constants should be forced into registers by the LIRGenerator
    }
  }
}

void LIR_Assembler::intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op) {
assert(value->is_double_xmm(),"must be");
assert(dest->is_double_xmm(),"must be");
  FRegister Rdest = dest->as_xmm_double_reg();
  FRegister Rval  = value->as_xmm_double_reg();
  switch(code) {
  case lir_cos:   Unimplemented(); // handled as a RT call
  case lir_sin:   Unimplemented(); // handled as a RT call
  case lir_tan:   Unimplemented(); // handled as a RT call
  case lir_abs:   __ move8 (Rdest, Rval); __ absd(Rdest); break;
  case lir_sqrt:  __ sqrtd (Rdest, Rval); break;
  case lir_log:   __ flog  (Rdest, Rval); break;
  case lir_log10: __ flog10(Rdest, Rval); break;
  default:        ShouldNotReachHere();
  }
}

void LIR_Assembler::logic_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst) {
  //assert(left->destroys_register(), "check");
  assert(left->type() != T_OBJECT && left->type() != T_ARRAY, "logic on oops?");
  assert(right->type() != T_OBJECT && right->type() != T_ARRAY, "logic on oops?");

  if (left->is_single_cpu()) {
    Register reg = left->as_register();
assert(dst->as_register()==reg,"must be");
    if (right->is_constant()) {
      int val = right->as_constant_ptr()->as_jint();
      switch (code) {
case lir_logic_and:__ and4i(reg,val);break;
case lir_logic_or:__ or_4i(reg,val);break;
case lir_logic_xor:__ xor4i(reg,val);break;
        default: ShouldNotReachHere();
      }
    } else if (right->is_stack()) {
      Address raddr = frame_map()->address_for_slot(right->single_stack_ix());
      switch (code) {
case lir_logic_and:__ and4(reg,raddr._base,raddr._disp);break;
        case lir_logic_or:  __ or_4 (reg, raddr._base, raddr._disp); break;
        case lir_logic_xor: __ xor4 (reg, raddr._base, raddr._disp); break;
        default: ShouldNotReachHere();
      }
    } else {
      Register rright = right->as_register();
      switch (code) {
case lir_logic_and:__ and4(reg,rright);break;
case lir_logic_or:__ or_4(reg,rright);break;
case lir_logic_xor:__ xor4(reg,rright);break;
        default: ShouldNotReachHere();
      }
    }
  } else {
    Register l_lo = left->as_register_lo();
#ifdef ASSERT
    Register dst_lo = dst->as_register_lo();
    assert (dst_lo == l_lo, "must be");
    Register l_hi = left->as_register_hi();
    assert (l_lo   == l_hi, "must be");
#endif
    if (right->is_constant()) {
      int r_lo = right->as_constant_ptr()->as_jint_lo();
      int r_hi = right->as_constant_ptr()->as_jint_hi();
      switch (code) {
        case lir_logic_and:
if(r_hi==0){
__ and4i(l_lo,r_lo);
          } else {
assert(r_hi==-1,"instruction won't clear msbs");
__ and8i(l_lo,r_lo);
          }
          break;
        case lir_logic_or:
__ or_8i(l_lo,r_lo);
          assert ((r_lo < 0 && r_hi == -1) || (r_hi == 0), "incorrect sign extension");
          break;
        case lir_logic_xor:
__ xor8i(l_lo,r_lo);
          assert ((r_lo < 0 && r_hi == -1) || (r_hi == 0), "incorrect sign extension");
          break;
        default: ShouldNotReachHere();
      }
    } else {
      Register r_lo = right->as_register_lo();
      switch (code) {
case lir_logic_and:__ and8(l_lo,r_lo);break;
case lir_logic_or:__ or_8(l_lo,r_lo);break;
case lir_logic_xor:__ xor8(l_lo,r_lo);break;
        default: ShouldNotReachHere();
      }
    }
  }
}

// we assume that rax, and rdx can be overwritten
void LIR_Assembler::arithmetic_div(LIR_Code code,LIR_Opr left,LIR_Opr right,LIR_Opr temp,LIR_Opr result,CodeEmitInfo*info,bool is_long){

assert(left->is_cpu_register(),"left must be register");
assert(right->is_cpu_register()||right->is_constant(),"right must be register or constant");
assert(result->is_cpu_register(),"result must be register");
  assert(left->type() != T_OBJECT && left->type() != T_ARRAY, "math on oops?");
  assert(right->type() != T_OBJECT && right->type() != T_ARRAY, "math on oops?");

  Register lreg = left->as_register();
  Register dreg = result->as_register();
Register Rtmp=temp->as_register();
assert(lreg==RAX,"left register must be rax,");
  assert(Rtmp == RDX, "tmp register must be rdx");

  // lreg and dreg may be the same
  if (right->is_constant()) {
    intptr_t divisor = is_long ? right->as_constant_ptr()->as_jlong() : right->as_constant_ptr()->as_jint();
    assert(divisor > 0 && is_power_of_2(divisor), "must be");
if(code==lir_idiv||code==lir_ldiv){
      if (is_long) {
__ cdq8();//sign extend into rdx:rax
        if (divisor == 2) {
__ sub8(lreg,Rtmp);
        } else {
          assert(is_int32(divisor-1), "shouldn't be here otherwise");
__ and8i(Rtmp,divisor-1);
__ add8(lreg,Rtmp);
        }
__ sar8i(lreg,log2_intptr(divisor));
__ move8(dreg,lreg);
      } else {
__ cdq4();//sign extend into rdx:rax
        if (divisor == 2) {
__ sub4(lreg,Rtmp);
        } else {
__ and4i(Rtmp,divisor-1);
__ add4(lreg,Rtmp);
        }
__ sar4i(lreg,log2_intptr(divisor));
__ move4(dreg,lreg);
      }
    } else {
      Label done;
      if (is_long) {
        assert(is_int32(~(divisor-1)), "shouldn't be here otherwise");
        __ mov8i (dreg,  (int64_t)(0x8000000000000000 | (divisor - 1)));
__ and8(dreg,lreg);
__ jns(done);
        __ add8i (dreg, -1);
__ or_8i(dreg,~(divisor-1));
        __ add8i (dreg, 1);
      } else {
__ mov8u(dreg,0x80000000|(divisor-1));
__ and4(dreg,lreg);
__ jns(done);
        __ add4i (dreg, -1);
__ or_4i(dreg,~(divisor-1));
        __ add4i (dreg, 1);
      }
      __ bind(done);
    }
  } else {
    Register rreg = right->as_register();
assert(rreg!=RDX,"right register must not be rdx");

    if (is_long) __ move8 (lreg, RAX);
    else         __ move4 (lreg, RAX);

    int idivl_offset = is_long ? __ corrected_idiv8(rreg) : __ corrected_idiv4(rreg);
    add_debug_info_for_div0(idivl_offset, info);
    Register res = (code == lir_irem || code == lir_lrem) ? RDX : RAX;

    if (is_long) __ move8 (dreg, res);
    else         __ move4 (dreg, res);
  }
}


void LIR_Assembler::comp_op(LIR_Condition condition, LIR_Opr opr1, LIR_Opr opr2, LIR_Op2* op) {
  if (opr1->is_single_cpu()) {
    Register reg1 = opr1->as_register();
if(opr2->is_single_cpu()){//cpu register - cpu register
if(opr1->type()==T_OBJECT||opr1->type()==T_ARRAY)__ cmp8(reg1,opr2->as_register());
else __ cmp4(reg1,opr2->as_register());
}else if(opr2->is_constant()){//cpu register - constant
      LIR_Const* c = opr2->as_constant_ptr();
      if (c->type() == T_INT) {
        if (c->as_jint() == 0 && (condition == lir_cond_equal || condition == lir_cond_notEqual))
__ test4(reg1,reg1);
        else
__ cmp4i(reg1,c->as_jint());
      } else {
        assert0 (c->type() == T_OBJECT);
jobject obj=c->as_jobject();
        assert0 (obj == NULL && (condition == lir_cond_equal || condition == lir_cond_notEqual));
__ test8(reg1,reg1);
      }
    } else if (opr2->is_stack()) {    // cpu register - stack
Address addr=frame_map()->address_for_slot(opr2->single_stack_ix());
      if (opr1->type() == T_OBJECT || opr1->type() == T_ARRAY) __ cmp8(reg1, addr._base, addr._disp);
      else                                                     __ cmp4(reg1, addr._base, addr._disp);
    } else if (opr2->is_address()) {  // cpu register - address
      if( op->info() != NULL ) __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
LIR_Address*addr=opr2->as_address_ptr();
Register Rbase=addr->base()->as_register();
      assert0( MultiMapMetaData || addr->base()->is_destroyed() );
      __ verify_oop(Rbase, MacroAssembler::OopVerify_LoadBase);
      __ cvta2va(Rbase);
if(op->info()!=NULL)add_debug_info_for_null_check_here(op->info());
      if (!addr->index()->is_illegal()) __ cmp4(reg1, Rbase, addr->disp(), addr->index()->as_register(), addr->scale());
      else                              __ cmp4(reg1, Rbase, addr->disp());
    } else { ShouldNotReachHere(); }

  } else if(opr1->is_double_cpu()) {
    Register reg1 = opr1->as_register();
    if (opr2->is_double_cpu()) { // cpu register - cpu register
__ cmp8(reg1,opr2->as_register());
    } else if (opr2->is_constant()) {
      LIR_Const* c = opr2->as_constant_ptr();
      if (c->type() == T_INT) {
	if (c->as_jint() == 0) __ test8(reg1, reg1);
	else                   __ cmp8i(reg1, c->as_jint());
      } else {                   // cpu register - constant
        assert0(c->type() == T_LONG);
	assert(is_int32(opr2->as_jlong()), "only handles int32s");
        if (c->as_jlong() == 0) __ test8(reg1, reg1);
        else                    __ cmp8i(reg1, (long)c->as_jlong());
      }
    } else if (opr2->is_stack()) {   // cpu register - stack
Address addr=frame_map()->address_for_slot(opr2->double_stack_ix());
      __ cmp8(reg1,addr._base,addr._disp);
    } else if (opr2->is_address()) { // cpu register - address
LIR_Address*addr=opr2->as_address_ptr();
      Untested();
      if( op->info() != NULL ) __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
Register Rbase=addr->base()->as_register();
      assert0( MultiMapMetaData || addr->base()->is_destroyed() );
      __ verify_oop(Rbase, MacroAssembler::OopVerify_LoadBase);
      __ cvta2va(Rbase);
if(op->info()!=NULL)add_debug_info_for_null_check_here(op->info());
      if (!addr->index()->is_illegal()) __ cmp8(reg1, Rbase, addr->disp(), addr->index()->as_register(), addr->scale());
      else                              __ cmp8(reg1, Rbase, addr->disp());
    } else { ShouldNotReachHere(); }

  } else if (opr1->is_single_xmm()) {
FRegister reg1=opr1->as_xmm_float_reg();
    if (opr2->is_single_xmm()) {     // xmm register - xmm register
__ cmp4(reg1,opr2->as_xmm_float_reg());
    } else if (opr2->is_stack()) {   // xmm register - stack
Address addr=frame_map()->address_for_slot(opr2->single_stack_ix());
      __ cmp4(reg1,addr._base,addr._disp);
    } else if (opr2->is_address()) { // xmm register - address
LIR_Address*addr=opr2->as_address_ptr();
      Untested();
Register Rbase=addr->base()->as_register();
      assert0( MultiMapMetaData || addr->base()->is_destroyed() );
      __ verify_oop(Rbase, MacroAssembler::OopVerify_LoadBase);
      __ cvta2va(Rbase);
if(op->info()!=NULL)add_debug_info_for_null_check_here(op->info());
      if (!addr->index()->is_illegal()) __ cmp4(reg1, Rbase, addr->disp(), addr->index()->as_register(), addr->scale());
      else                              __ cmp4(reg1, Rbase, addr->disp());
    } else if (opr2->is_constant()) {
ShouldNotReachHere();//xmm - constant, constant should be forced into a register
    } else { ShouldNotReachHere(); }

  } else if (opr1->is_double_xmm()) {
FRegister reg1=opr1->as_xmm_double_reg();
    if (opr2->is_double_xmm()) {   // xmm register - xmm register
__ cmp8(reg1,opr2->as_xmm_double_reg());
    } else if (opr2->is_stack()) { // xmm register - stack
Address addr=frame_map()->address_for_slot(opr2->double_stack_ix());
      __ cmp8(reg1,addr._base,addr._disp);
    } else if (opr2->is_address()) { // xmm register - address
      Untested();
      if( op->info() != NULL ) __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
LIR_Address*addr=opr2->as_address_ptr();
Register Rbase=addr->base()->as_register();
      assert0( MultiMapMetaData || addr->base()->is_destroyed() );
      __ verify_oop(Rbase, MacroAssembler::OopVerify_LoadBase);
      __ cvta2va(Rbase);
if(op->info()!=NULL)add_debug_info_for_null_check_here(op->info());
      if (!addr->index()->is_illegal()) __ cmp8(reg1, Rbase, addr->disp(), addr->index()->as_register(), addr->scale());
      else                              __ cmp8(reg1, Rbase, addr->disp());
    } else if (opr2->is_constant()) {
ShouldNotReachHere();//xmm - constant, constant should be forced into a register
    } else { ShouldNotReachHere(); }
  } else if (opr1->is_address() && opr2->is_constant()) {
    // special case: address - constant
    if( op->info() != NULL ) __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
    LIR_Address* addr = opr1->as_address_ptr();
    LIR_Const* c = opr2->as_constant_ptr();
    assert0( MultiMapMetaData || addr->base()->is_destroyed() );
Register Rbase=addr->base()->as_register();
    __ cvta2va(Rbase);
    if (c->type() == T_INT) {
if(op->info()!=NULL)add_debug_info_for_null_check_here(op->info());
      if (addr->index() != LIR_OprFact::illegalOpr) __ cmp4i(Rbase, addr->disp(), addr->index()->as_register(), addr->scale(), (long)c->as_jint());
      else                                          __ cmp4i(Rbase, addr->disp(), (long)c->as_jint());
    } else if (c->type() == T_OBJECT) {
jobject obj=c->as_jobject();
      assert0 (obj == NULL);
      add_debug_info_for_null_check_here(op->info());
      if (addr->index() != LIR_OprFact::illegalOpr) __ cmp8i(Rbase, addr->disp(), addr->index()->as_register(), addr->scale(), 0);
      else                                          __ cmp8i(Rbase, addr->disp(), 0);
    } else { ShouldNotReachHere(); }

  } else { ShouldNotReachHere(); }
}

void LIR_Assembler::comp_fl2i(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dst, LIR_Op2* op) {
  if (code == lir_cmp_fd2i || code == lir_ucmp_fd2i) {
    if (left->is_single_xmm()) {
      assert(right->is_single_xmm(), "must match");
      __ fcmp(dst->as_register(), left->as_xmm_float_reg(), right->as_xmm_float_reg(), code == lir_ucmp_fd2i ? -1 : 1);
    } else {
      assert0( left->is_double_xmm());
      assert(right->is_double_xmm(), "must match");
      __ dcmp(dst->as_register(), left->as_xmm_float_reg(), right->as_xmm_float_reg(), code == lir_ucmp_fd2i ? -1 : 1);
    }
  } else {
    assert(code == lir_cmp_l2i, "check");
    __ lcmp2int(dst->as_register(), left->as_register(), right->as_register());
  }
}


void LIR_Assembler::call(address entry,CodeEmitInfo*info){
  __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PreCall);
__ aligned_patchable_call(entry);
  add_call_info(__ rel_pc(), info, false);
  __ add_oopmap(__ rel_pc(), info->oop_map());
  __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PostCall);
}


void LIR_Assembler::ic_call(address entry, CodeEmitInfo* info) {
  if (UseC1 && UseC2 && UseTypeProfile) {
assert(StubRoutines::x86::c1_profile_callee(),"Not loaded yet!");

__ push(R11);
__ push(RAX);
    __ mov8i(R11, (int64_t)((intptr_t)compilation()->codeprofile() + (intptr_t)info->cpdoff()));
    __ call((address)StubRoutines::x86::c1_profile_callee());
__ pop(RAX);
__ pop(R11);
  }
  __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PreCall);
  // Get KID of ref (held in RDI - 1st outgoing arg) in RAX
  __ ref2kid_no_npe(RAX, RDI); // any NPE is hidden
  // must align calls sites, otherwise they can't be updated atomically on MP hardware
  NativeInlineCache::fill(_masm);
  add_call_info(__ rel_pc(), info, true);
  __ add_oopmap(__ rel_pc(), info->oop_map());
  __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PostCall);
}


/* Currently, vtable-dispatch is only enabled for sparc platforms */
void LIR_Assembler::vtable_call(int vtable_offset, CodeEmitInfo* info) {
  ShouldNotReachHere();
}

void LIR_Assembler::throw_op(LIR_Opr exceptionOop,LIR_Opr thread_reg,CodeEmitInfo*info,bool unwind){
  // Save the exception oop where it belongs
Register Rthread=thread_reg->as_register();
  assert0( Rthread != exceptionOop->as_register() );
  __ verify_oop(exceptionOop->as_register(), MacroAssembler::OopVerify_Move);
  __ st8(Rthread,in_bytes(Thread::pending_exception_offset()),exceptionOop->as_register());
  if (!unwind) {
    // search an exception handler (THR->_pending_exception: exception oop, on_stack_after_call: throwing pc)
    __ call(Runtime1::entry_for(Runtime1::handle_exception_id));
  } else {
    __ restore_callee_saves_pop_frame_and_jmp(frame_map(), Runtime1::entry_for(Runtime1::unwind_exception_id));
  }
  add_call_info_here(info); // for exception handler
  __ add_oopmap(__ rel_pc(), info->oop_map());
  debug_only (__ os_breakpoint());  // never returns from call
}


void LIR_Assembler::shift_op ( LIR_Code code, LIR_Opr left, LIR_Opr count, LIR_Opr dest, LIR_Opr tmp) {
  // optimized version for linear scan:
  // * count must be already in RCX (guaranteed by LinearScan)
  // * left and dest must be equal
  // * tmp must be unused
  assert(count->as_register() == SHIFT_count, "count must be in ECX");
  assert(left == dest, "left and dest must be equal");
  assert(tmp->is_illegal(), "wasting a register if tmp is allocated");
  assert(left->type() != T_OBJECT && left->type() != T_ARRAY, "math on oops?");

  if (left->is_single_cpu()) {
    Register value = left->as_register();
    assert(value != SHIFT_count, "left cannot be ECX");

    switch (code) {
    case lir_shl:  __ shl4(value, count->as_register()); break;
    case lir_shr:  __ sar4(value, count->as_register()); break;
    case lir_ushr: __ shr4(value, count->as_register()); break;
    default: ShouldNotReachHere();
    }
  } else if (left->is_double_cpu()) {
Register value=left->as_register_lo();
    assert(value != SHIFT_count, "left cannot be ECX");

    switch (code) {
    case lir_shl:  __ shl8(value, count->as_register()); break;
    case lir_shr:  __ sar8(value, count->as_register()); break;
    case lir_ushr: __ shr8(value, count->as_register()); break;
    default: ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::shift_op(LIR_Code code, LIR_Opr left, jint count, LIR_Opr dest) {
  assert(left->type() != T_OBJECT && left->type() != T_ARRAY, "math on oops?");
  if (dest->is_single_cpu()) {
    // first move left into dest so that left is not destroyed by the shift
Register Rdest=dest->as_register();
    count = count & 0x1F; // Java spec

move_regs(left->as_register(),Rdest);
    if (count != 0) {
      switch (code) {
case lir_shl:__ shl4i(Rdest,count);break;
case lir_shr:__ sar4i(Rdest,count);break;
case lir_ushr:__ shr4i(Rdest,count);break;
      default: ShouldNotReachHere();
      }
    }
  } else if (dest->is_double_cpu()) {
    // first move left into dest so that left is not destroyed by the shift
Register Rdest=dest->as_register_lo();
    count = count & 0x1F; // Java spec

    move_regs(left->is_single_cpu() ? left->as_register() : left->as_register_lo(), Rdest);
    if (count != 0) {
      switch (code) {
case lir_shl:__ shl8i(Rdest,count);break;
case lir_shr:__ sar8i(Rdest,count);break;
case lir_ushr:__ shr8i(Rdest,count);break;
      default: ShouldNotReachHere();
      }
    }
  } else {
    ShouldNotReachHere();
  }
}

// This code replaces a call to arraycopy; no exception may
// be thrown in this code, they must be thrown in the System.arraycopy
// activation frame; we could save some checks if this would not be the case
void LIR_Assembler::emit_arraycopy(LIR_OpArrayCopy* op) {
  ciArrayKlass* default_type = op->expected_type();
  __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  if ( default_type == NULL ) {
    // Call original Java method
    assert (op->src()->as_register()     == RDI, "src must be in RDI");
    assert (op->src_pos()->as_register() == RSI, "src_pos must be in RSI");
    assert (op->dst()->as_register()     == RDX, "dst must be in RDX");
    assert (op->dst_pos()->as_register() == RCX, "dst_pos must be in RCX");
    assert (op->length()->as_register()  == R08, "length must be in R08");
    // Check sanity of args
    __ verify_oop(RDI, MacroAssembler::OopVerify_OutgoingArgument);
    __ verify_oop(RDX, MacroAssembler::OopVerify_OutgoingArgument);
    call     ((address)StubRoutines::resolve_and_patch_call_entry(), op->info());
  } else {
    const Register Rsrc     = op->src()   ->as_register(); // RDI for all copies
    const Register Rdst     = op->dst()   ->as_register(); // RSI for all copies
    const Register Rlength  = op->length()->as_register(); // RDX for all copies
    assert(   Rdst == RDI, "check argument is in correct register");
    assert(   Rsrc == RSI, "check argument is in correct register");
    assert(Rlength == RDX, "check argument is in correct register");
Register Rsrc_pos=op->src_pos()->as_register();//unknown register from allocator
Register Rdst_pos=op->dst_pos()->as_register();//unknown register from allocator
    int flags = op->flags(); // what checks are necessary
    BasicType basic_type = default_type->element_type()->basic_type(); // Type of elements copied
    if (basic_type == T_ARRAY) basic_type = T_OBJECT;
    assert(default_type->is_array_klass() && default_type->is_loaded(), "must be true at this point");
    // Find temporary caller save registers that will have been spilled prior to
    // this LIR instruction. Give temps small registers to avoid REX prefixes.
    Register Rorig_src = noreg, Rorig_dst = noreg; // unstripped src and dst registers
    Register Rtmp0 = noreg, Rtmp1 = noreg;         // scratch registers
    for (int i=0; i < 16; i++) {
      if (LinearScan::is_caller_save(i) &&
          ((Register)i != Rsrc    ) && ((Register)i != Rdst    ) &&
          ((Register)i != Rsrc_pos) && ((Register)i != Rdst_pos) &&
          ((Register)i != Rlength ) && ((Register)i != RSP     )) {
if(Rtmp0==noreg){
          Rtmp0 = (Register)i;
        } else if( UseSBA && basic_type == T_OBJECT && Rtmp1 == noreg ) {
          Rtmp1 = (Register)i;
}else if(Rorig_src==noreg){
          Rorig_src = (Register)i;
        } else {
          Rorig_dst = (Register)i;
          break;
        }
      }
    }
assert(Rorig_dst!=noreg,"must have found necessary temp registers");
    // Slow case labels, either restoring or not original src and dst registers
    Label slowcase_restore_origs, slowcase_no_restore_origs;

    // Check sanity of args
    __ verify_oop(Rsrc, MacroAssembler::OopVerify_OutgoingArgument);
    __ verify_oop(Rdst, MacroAssembler::OopVerify_OutgoingArgument);

    // test for NULL
    if (flags & LIR_OpArrayCopy::src_null_check) __ null_chk(Rsrc, slowcase_no_restore_origs);
    if (flags & LIR_OpArrayCopy::dst_null_check) __ null_chk(Rdst, slowcase_no_restore_origs);

    // do array types match test (won't NPE in !KIDInRef mode following null check)
    if (flags & LIR_OpArrayCopy::type_check) {
      __ compare_klasses (RInOuts::a, Rtmp0, RKeepIns::a, Rsrc, Rdst);
      __ jne             (slowcase_no_restore_origs);
    }

    // check if negative
    if (flags & LIR_OpArrayCopy::src_pos_positive_check) {
      __ test4(Rsrc_pos, Rsrc_pos);
      __ jsg  (slowcase_no_restore_origs);
    }
    if (flags & LIR_OpArrayCopy::dst_pos_positive_check) {
      __ test4(Rdst_pos, Rdst_pos);
      __ jsg  (slowcase_no_restore_origs);
    }
    if (flags & LIR_OpArrayCopy::length_positive_check) {
      __ test4(Rlength, Rlength);
      __ jsg  (slowcase_no_restore_origs);
    }

#ifdef ASSERT
    if (basic_type != T_OBJECT || !(flags & LIR_OpArrayCopy::type_check)) {
      // Sanity check the known type with the incoming class.  For the
      // primitive case the types must match exactly with src.klass and
      // dst.klass each exactly matching the default type.  For the
      // object array case, if no type check is needed then either the
      // dst type is exactly the expected type and the src type is a
      // subtype which we can't check or src is the same array as dst
      // but not necessarily exactly of type default_type.
      Label known_ok, halt;
      __ ref2kid (Rtmp0, Rdst);
      __ cmp4i   (Rtmp0, default_type->klassId());
      if (basic_type != T_OBJECT) {
__ jne(halt);
        __ ref2kid (Rtmp0, Rsrc);
        __ cmp4i   (Rtmp0, default_type->klassId());
      }
__ jeq(known_ok);
      __ bind    (halt);
      __ stop    ("incorrect type information in arraycopy");
      __ bind    (known_ok);
    }
#endif

    // Do we need to save originals?
    bool save_originals = ((flags & LIR_OpArrayCopy::src_range_check) != 0) ||
                          ((flags & LIR_OpArrayCopy::dst_range_check) != 0) ||
                          (UseSBA && basic_type == T_OBJECT);
    // Save original values, strip src and dst
    assert0( MultiMapMetaData || (op->src()->is_destroyed() && op->dst()->is_destroyed()) );
    if( save_originals ) __ mov8 (Rorig_src, Rsrc);
    __ cvta2va (Rsrc);
    if( save_originals ) __ mov8 (Rorig_dst, Rdst);
    __ cvta2va (Rdst);

    if (flags & LIR_OpArrayCopy::src_range_check) {
      __ lea4 (Rtmp0, Rsrc_pos, 0, Rlength, 0); // Rtmp0 = src_pos + length
      __ cmp4 (Rtmp0, Rsrc, arrayOopDesc::length_offset_in_bytes());
      __ jab  (slowcase_restore_origs);
    }
    if (flags & LIR_OpArrayCopy::dst_range_check) {
      __ lea4 (Rtmp0, Rdst_pos, 0, Rlength, 0); // Rtmp0 = dst_pos + length
      __ cmp4 (Rtmp0, Rdst, arrayOopDesc::length_offset_in_bytes());
      __ jab  (slowcase_restore_origs);
    }
    // Perform copies
    Label done;
    // set Rsrc and Rdst to beginning of memory to read/write from/to
    const int log_size = type2logaelembytes[basic_type];
    __ lea (Rsrc, Rsrc, arrayOopDesc::base_offset_in_bytes(basic_type), Rsrc_pos, log_size);
    __ lea (Rdst, Rdst, arrayOopDesc::base_offset_in_bytes(basic_type), Rdst_pos, log_size);

    if (basic_type != T_OBJECT) {
      // Primitive copies using appropriate stub routine
      bool possible_overlap = (flags & LIR_OpArrayCopy::overlap_check) != 0;
      address entry = StubRoutines::prim_arraycopy(1 << log_size, possible_overlap);
      __ call_VM_leaf (entry, Rdst, Rsrc, Rlength, noreg, noreg, noreg);
    } else {
      // Heap to heap oop copies using Runtime1::oop_arraycopy
      address entry_heap = (address)StubRoutines::_object_arraycopy;
      if( MultiMapMetaData ) {
        // Strip metadata always before this leaf call: the call computes cardmark
        // math which is screwed up unless the metadata is first removed.
        __ shl8i(Rsrc,64-objectRef::unknown_bits);
        __ shr8i(Rsrc,64-objectRef::unknown_bits);
        __ shl8i(Rdst,64-objectRef::unknown_bits);
        __ shr8i(Rdst,64-objectRef::unknown_bits);
      }
      if( UseSBA ) {
        // SBA to SBA copies use primitive copy
	bool possible_overlap = (flags & LIR_OpArrayCopy::overlap_check) != 0;
        address entry_sba = StubRoutines::prim_arraycopy(1 << log_size, possible_overlap);
        Label not_same_space, sba_copy, sba_copy_with_lvb, heap_copy;
        __ move8(Rtmp0,Rorig_dst);     // Rtmp0 = spaceid(dst)
        __ move8(Rtmp1,Rorig_src);     // Rtmp1 = spaceid(src)
        __ shr8i(Rtmp0,objectRef::space_shift);
        __ shr8i(Rtmp1,objectRef::space_shift);
        __ and1i(Rtmp0,objectRef::space_mask );
        __ and1i(Rtmp1,objectRef::space_mask );
        __ cmp1 (Rtmp0,Rtmp1);         // spaceid(dst) cmp spaceid(src) ?
        __ jne  (not_same_space);      // if (spaceid(dst) != spaceid(src)) goto not_same_space
        // dst and src are both in the same space, is it heap or stack?
        __ cmp1i(Rtmp0,objectRef::stack_space_id);
        __ jne  (heap_copy); // spaceid(dst) == spaceid(src) == heap
        // Check stack references are correctly ordered
        __ cmp8 (Rdst,Rsrc);           // See if dst_adr > src_adr
        __ jae  (sba_copy);            // Addresses ordered properly
        __ ldz1 (Rtmp0,Rdst,-sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_fid)); // Load base FID into Rtmp0
        __ cmp1 (Rtmp0,Rsrc,-sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_fid)); // Compare against value fid
        __ jbl  (slowcase_restore_origs); // FIDs aren't ordered properly go to slow case
        // Fast SBA case
__ bind(sba_copy);
__ bind(sba_copy_with_lvb);
assert(!UseLVBs,"TODO: LVB arraycopy of heap oops to stack");
        __ call_VM_leaf (entry_sba, Rdst, Rsrc, Rlength, noreg, noreg, noreg);
        __ jmp  (done);
        // Space IDs vary
__ bind(not_same_space);
        __ cmp1i(Rtmp0,objectRef::stack_space_id);
        __ jeq  (sba_copy_with_lvb);      // spaceid(dst) == stack, spaceid(src) == heap, use fast SBA copy but reads need LVB
        __ cmp1i(Rtmp1,objectRef::stack_space_id);
        __ jeq  (slowcase_restore_origs); // spaceid(src) == stack, spaceid(dst) == heap, slow case for SBA escapes
        // Fast heap to heap case
__ bind(heap_copy);
      }
      __ call_VM_leaf (entry_heap, Rdst, Rsrc, Rlength, noreg, noreg, noreg);
    }
    __ jmp          (done);

    // Slow cases
    if( save_originals ) {
__ bind(slowcase_restore_origs);
      __ mov8 (Rsrc, Rorig_src);
      __ mov8 (Rdst, Rorig_dst);
    }
__ bind(slowcase_no_restore_origs);
    // Shuffle to setup parameters for call:
    // What?        Before(oop/prim) After
    // src          RSI              RDI
    // src_pos      ???              RSI
    // dst          RDI              RDX
    // dst_pos      ???              RCX
    // length       RDX              R08
__ push(Rsrc_pos);//save args in unknown regs
__ push(Rdst_pos);
    __ mov4  (R08, Rlength /*RDX*/); // set up regs known not to interfere
    __ mov8  (RDX, Rdst /*RDI*/);
    __ mov8  (RDI, Rsrc /*RSI*/);
__ pop(RCX);//set up other args from stack
__ pop(RSI);
    __ verify_oop(RDI, MacroAssembler::OopVerify_OutgoingArgument);
    __ verify_oop(RDX, MacroAssembler::OopVerify_OutgoingArgument);
    call     ((address)StubRoutines::resolve_and_patch_call_entry(), op->info());

    __ bind(done);
  }
}

// This code replaces a call to String.equals, it doesn't throw exceptions
// and must check the other argument is a string
void LIR_Assembler::emit_stringequals(LIR_OpStringEquals*op){
Label fail,done;

Register Rthis=op->this_string()->as_register();
Register Rother=op->other_string()->as_register();
Register Rlength=op->tmp1()->as_register();
Register Roffset=op->tmp2()->as_register();
Register Rthis_vl=op->tmp3()->as_register();
Register Rother_vl=op->tmp4()->as_register();
  FRegister Rxmm1    = op->tmp5()->as_xmm_double_reg();
  FRegister Rxmm2    = op->tmp6()->as_xmm_double_reg();
Register Rres=op->res()->as_register();

  __ cvta2va (Rthis); assert0( MultiMapMetaData || op->this_string()->is_destroyed() );
  int npe_off = __ rel_pc(); // if we had dispatched on this method it would throw an NPE, ensure we catch the NPE in the same place
  __ ldref_lvb( RInOuts::a, Rthis_vl, __ cheapest_encodable_register(Rlength,Roffset,Rother_vl,Rres) /*temp*/,
                RKeepIns::a, Rthis, java_lang_String::value_offset_in_bytes(), false );
  { Register Rkid =  __ cheapest_encodable_register(Rlength,Roffset,Rother_vl,Rres);
    __ ref2kid_no_npe(Rkid, Rother);
    __ cmp4i  (Rkid, SystemDictionary::string_klass()->klass_part()->klassId());
    __ jne(fail); // branch if not string or null
  }
  __ cvta2va (Rthis_vl);
  __ cvta2va (Rother); assert0( MultiMapMetaData || op->other_string()->is_destroyed() );
  __ ldref_lvb( RInOuts::a, Rother_vl, __ cheapest_encodable_register(Rlength,Roffset,Rres) /*temp*/,
                RKeepIns::a, Rother, java_lang_String::value_offset_in_bytes(), false );
  __ cvta2va (Rother_vl);

  __ prim_arrays_equal(2, RInOuts::a, Rthis_vl, Rother_vl, Rlength, Roffset, Rxmm1, Rxmm2, fail);
  // Success, set result to true/1
  __ mov8i(Rres, 1);
  __ jmp  (done);
__ bind(fail);//hot fail path, set result to false/0
  __ mov8i(Rres, 0);
  __ bind(done);

  // Add NPE info for if String was null
add_debug_info_for_null_check(npe_off,op->info());
}

void LIR_Assembler::emit_lock(LIR_OpLock*op){
if(op->code()==lir_lock){
Register Robj=op->obj_opr()->as_register();
Register RAX_mw=op->mark()->as_register();
Register R09_tid=op->tid()->as_register();
Register Rtmp=op->tmp()->as_register();
    assert0( RAX_mw == RAX && R09_tid == R09 );

    // Store object to be locked on stack before lock attempts.  The slow-path
    // locking will need to find the object in case of biased locking.
    int monitor_index = op->mon_num();
    assert0( monitor_index >= 0 );
    Address addr = frame_map()->address_for_monitor_object(monitor_index);
    __ st8(addr._base, (int)addr._disp, Robj);

Label fast_locked;
    if( op->info() != NULL ) __ verify_oopmap(op->info()->oop_map(), MacroAssembler::OopVerify_OopMap_PreCall);
    int null_check_offset = __ fast_path_lock(RInOuts::a, Rtmp, R09_tid, RAX_mw, RKeepIns::a, Robj, fast_locked );
    if (op->info() != NULL) add_debug_info_for_null_check(null_check_offset, op->info());

    // fast lock failed, slow lock
    // RAX == loaded markWord, R09 == the shifted thread-id
    __ call (StubRoutines::lock_entry());
    __ jne  (*op->stub()->entry());       // lock failed branch to slow path

    __ bind(*op->stub()->continuation());
__ bind(fast_locked);
  } else {
    assert0( op->code() == lir_unlock );
    assert0( MultiMapMetaData || op->obj_opr()->is_destroyed() );

Register R10_obj=op->obj_opr()->as_register();//may hold a ref or an oop
Register RCX_mark=op->mark()->as_register();
Register RAX_tmp=op->tmp()->as_register();
    assert0 (R10_obj == R10 && RCX_mark == RCX && RAX_tmp == RAX);

    __ fast_path_unlock(RInOuts::a, R10_obj, RCX_mark, RAX_tmp,*op->stub()->entry());
    __ bind(*op->stub()->continuation());
  }
}


//void LIR_Assembler::emit_profile_call(LIR_OpProfileCall* op) {
//  ciMethod* method = op->profiled_method();
//  int bci          = op->profiled_bci();
//
//  // Update counter for all call types
//  ciMethodData* md = method->method_data();
//  if (md == NULL) {
//    bailout("out of memory building methodDataOop");
//    return;
//  }
//  ciProfileData* data = md->bci_to_data(bci);
//  assert(data->is_CounterData(), "need CounterData for calls");
//  assert(op->mdo()->is_single_cpu(),  "mdo must be allocated");
//  Register mdo  = op->mdo()->as_register();
//  __ movoop(mdo, md->encoding());
//  Address counter_addr(mdo, md->byte_offset_of_slot(data, CounterData::count_offset()));
//  __ addl(counter_addr, DataLayout::counter_increment);
//  Bytecodes::Code bc = method->java_code_at_bci(bci);
//  // Perform additional virtual call profiling for invokevirtual and
//  // invokeinterface bytecodes
//  if ((bc == Bytecodes::_invokevirtual || bc == Bytecodes::_invokeinterface) &&
//      Tier1ProfileVirtualCalls) {
//    assert(op->recv()->is_single_cpu(), "recv must be allocated");
//    Register recv = op->recv()->as_register();
//    assert_different_registers(mdo, recv);
//    assert(data->is_VirtualCallData(), "need VirtualCallData for virtual calls");
//    ciKlass* known_klass = op->known_holder();
//    if (Tier1OptimizeVirtualCallProfiling && known_klass != NULL) {
//      // We know the type that will be seen at this call site; we can
//      // statically update the methodDataOop rather than needing to do
//      // dynamic tests on the receiver type
//
//      // NOTE: we should probably put a lock around this search to
//      // avoid collisions by concurrent compilations
//      ciVirtualCallData* vc_data = (ciVirtualCallData*) data;
//      uint i;
//      for (i = 0; i < VirtualCallData::row_limit(); i++) {
//        ciKlass* receiver = vc_data->receiver(i);
//        if (known_klass->equals(receiver)) {
//          Address data_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
//          __ addl(data_addr, DataLayout::counter_increment);
//          return;
//        }
//      }
//
//      // Receiver type not found in profile data; select an empty slot
//
//      // Note that this is less efficient than it should be because it
//      // always does a write to the receiver part of the
//      // VirtualCallData rather than just the first time
//      for (i = 0; i < VirtualCallData::row_limit(); i++) {
//        ciKlass* receiver = vc_data->receiver(i);
//        if (receiver == NULL) {
//          Address recv_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_offset(i)));
//          __ movoop(recv_addr, known_klass->encoding());
//          Address data_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
//          __ addl(data_addr, DataLayout::counter_increment);
//          return;
//        }
//      }
//    } else {
//      __ movl(recv, Address(recv, oopDesc::klass_offset_in_bytes()));
//      Label update_done;
//      uint i;
//      for (i = 0; i < VirtualCallData::row_limit(); i++) {
//        Label next_test;
//        // See if the receiver is receiver[n].
//        __ cmpl(recv, Address(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_offset(i))));
//        __ jne( next_test);
//        Address data_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i)));
//        __ addl(data_addr, DataLayout::counter_increment);
//        __ jmp(update_done);
//        __ bind(next_test);
//      }
//
//      // Didn't find receiver; find next empty slot and fill it in
//      for (i = 0; i < VirtualCallData::row_limit(); i++) {
//        Label next_test;
//        Address recv_addr(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_offset(i)));
//        __ cmpl(recv_addr, NULL_WORD);
//        __ jne( next_test);
//        __ movl(recv_addr, recv);
//        __ movl(Address(mdo, md->byte_offset_of_slot(data, VirtualCallData::receiver_count_offset(i))), DataLayout::counter_increment);
//        if (i < (VirtualCallData::row_limit() - 1)) {
//          __ jmp(update_done);
//        }
//        __ bind(next_test);
//      }
//
//      __ bind(update_done);
//    }
//  }
//}
//
//
//void LIR_Assembler::monitor_address(int monitor_no, LIR_Opr dst) {
//  __ leal(dst->as_register(), frame_map()->address_for_monitor_lock(monitor_no));
//}


void LIR_Assembler::align_backward_branch_target() {
__ align_with_nops(BytesPerWord);
}


void LIR_Assembler::negate(LIR_Opr left, LIR_Opr dest) {
  // Negation, for int/long operations negation is the same as subtraction from 0.
  // For floats and doubles the specification is that the sign bit must be inverted.
  if (left->is_single_cpu()) {
Register dest_reg=dest->as_register();
Register left_reg=left->as_register();
    if (dest_reg != left_reg) {
__ mov8i(dest_reg,0);
__ sub4(dest_reg,left_reg);
    } else {
      __ neg4  (dest_reg);
    }
  } else if (left->is_double_cpu()) {
Register dest_reg=dest->as_register_lo();
Register left_reg=left->as_register_lo();
    if (dest_reg != left_reg) {
__ mov8i(dest_reg,0);
__ sub8(dest_reg,left_reg);
    } else {
      __ neg8  (dest_reg);
    }
  } else if (dest->is_single_xmm()) {
FRegister dest_reg=dest->as_xmm_float_reg();
FRegister left_reg=left->as_xmm_float_reg();
__ move4(dest_reg,left_reg);
    __ negf (dest_reg);
  } else if (dest->is_double_xmm()) {
FRegister dest_reg=dest->as_xmm_double_reg();
FRegister left_reg=left->as_xmm_double_reg();
__ move8(dest_reg,left_reg);
    __ negd (dest_reg);
  } else {
    ShouldNotReachHere();
  }
}

void LIR_Assembler::bit_test(LIR_Opr left,LIR_Opr bitnum){
  assert0( bitnum->is_constant() );
int bit=bitnum->as_constant_ptr()->as_jint();
if(left->is_constant()){
    __ btx4i((address)(left->as_constant_ptr()->as_jlong()),bit);
  } else {
    __ btx4i(left->as_register(),bit);
  }
}

void LIR_Assembler::leal(LIR_Opr address,LIR_Opr dest){
assert(address->is_address()&&dest->is_register(),"check");
LIR_Address*addr=address->as_address_ptr();
  Register reg = dest->as_register();
  assert(!addr->base()->is_illegal() && !addr->index()->is_illegal(),
"Handle this case");
  __ lea(reg, addr->base()->as_register(), (int)addr->disp(), 
         addr->index()->as_register(), addr->scale());
}

void LIR_Assembler::rt_call(LIR_Opr result, address dest, const LIR_OprList* args, CodeEmitInfo* info) {
  // Leaf calls pass thread in RDI
  bool is_stub = Runtime1::contains(dest) != -1;
  bool is_leaf = (info == NULL);
#ifdef ASSERT

  assert(!result->is_valid() ||
         (result->is_cpu_register() && result->as_register() == RAX) ||
         (result->is_xmm_register() && result->as_xmm_double_reg() == F00), "result must be set up");

  // Stub routines have their own parameter passing conventions, results are consistent in case of deopt.
  if (!is_stub) {
    static const int int_reg_max = 6;
    static const int flt_reg_max = 8;
    static const  Register intarg[int_reg_max] = { RDI, RSI, RDX, RCX, R08, R09 };
    static const FRegister fltarg[flt_reg_max] = { F00, F01, F02, F03, F04, F05, F06, F07 };
    for (int i=0, xmm=0, gpr= is_leaf ? 0 : 1; i < args->length(); i++) {
      if(args->at(i)->is_cpu_register()) {
        assert0(gpr < int_reg_max);
        assert(args->at(i)->as_register() == intarg[gpr], "outgoing arg mismatch");
        gpr++;
      } else {
        assert0(args->at(i)->is_xmm_register());
        assert0(xmm < flt_reg_max);
        assert(args->at(i)->as_xmm_double_reg() == fltarg[xmm], "outgoing arg mismatch");
        xmm++;
      }
    }
  }
#endif
  if (is_leaf) {
__ call_VM_leaf(dest,noreg);
  } else {
    __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PreCall);
    if (is_stub) __ call(dest);
    else         __ call_VM_plain(dest, noreg, noreg, noreg, noreg, noreg);
    add_call_info_here(info);
    __ add_oopmap(__ rel_pc(), info->oop_map());
    if (!is_stub) {
Label no_exception;
      // check for exception to forward - assume [RSP-8] contains address of call above
      __ getthr(RDI); // safe to crush as all caller registers are crushed by a call
      __ cmp8i (RDI, in_bytes(JavaThread::pending_exception_offset()),0);
      __ likely()->jeq (no_exception);
      // fake entry to forward exception as if we'd gone there from the call above
      __ sub8i (RSP, 8);
      __ jmp(StubRoutines::forward_exception_entry());
__ bind(no_exception);
    }
    __ verify_oopmap(info->oop_map(), MacroAssembler::OopVerify_OopMap_PostCall);
  }
}


void LIR_Assembler::volatile_move_op(LIR_Opr src, LIR_Opr dest, BasicType type, CodeEmitInfo* info) {
ShouldNotReachHere();//only appears reachable in JavaSoft for SPARC
}

void LIR_Assembler::membar() {
__ mfence();
}

void LIR_Assembler::membar_acquire() {
  // No x86 machines currently require load fences
  // __ load_fence();
}

void LIR_Assembler::membar_release() {
  // No x86 machines currently require store fences
  // __ store_fence();
}

void LIR_Assembler::get_thread(LIR_Opr result_reg) {
  assert(result_reg->is_register(), "check");
__ getthr(result_reg->as_register());
}


void LIR_Assembler::peephole(LIR_List*) {
  // do nothing for now
}

void LIR_Assembler::klassTable_oop_load(LIR_Opr ref, LIR_Opr klass, LIR_Opr tmp) {
Register Rklass=klass->as_register();
Register Rref=ref->as_register();
Register Rtmp=tmp->as_register();
  // NB if ref is null then the returned klass will be null, no implicit exceptions
  // are possible and should be caught on the uses of the klass (for example
  // loading the mirror in Object.getClass())
  assert( ref->is_destroyed(), "ref2klass clobbers ref");
  __ ref2klass_no_npe(RInOuts::a,Rklass,Rtmp,Rref);
}

#undef __
