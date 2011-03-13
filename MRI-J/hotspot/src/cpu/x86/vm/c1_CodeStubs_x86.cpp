/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_CodeStubs.hpp"
#include "c1_IR.hpp"
#include "c1_LIRAssembler.hpp"
#include "deoptimization.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"

#define __ ce->masm()->

float ConversionStub::float_zero=0.0F;
double ConversionStub::double_zero = 0.0;

void ConversionStub::emit_code ( LIR_Assembler* ce ) {
Unimplemented();//done inline on Azul x86
}

RangeCheckStub::RangeCheckStub(CodeEmitInfo* info, LIR_Opr index,
                               bool throw_index_out_of_bounds_exception)
  : _throw_index_out_of_bounds_exception(throw_index_out_of_bounds_exception)
  , _index(index)
{ 
  _info = info == NULL ? NULL : new CodeEmitInfo(info);
}


void RangeCheckStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  // pass the array index on stack because all registers must be preserved
  // Place args below return address in outgoing stack frame
  assert0 (-frame::runtime_stub_frame_size + frame::xPAD == -16);
  assert0 (_index->is_valid());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  if (_index->is_cpu_register()) {
    __ st4 (RSP, -16, _index->as_register());
  } else {
    __ st4i(RSP, -16, _index->as_jint());
  }
  Runtime1::StubID stub_id;
  if (_throw_index_out_of_bounds_exception) {
    stub_id = Runtime1::throw_index_exception_id;
  } else {
    stub_id = Runtime1::throw_range_check_failed_id;
  }
__ call(Runtime1::entry_for(stub_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
debug_only(__ os_breakpoint());
}


void DivByZeroStub::emit_code(LIR_Assembler* ce) {
  if (_offset != -1) {
    // Someday we might ponder catching X86 div-by-0 exception and avoiding the explicit test
  }
  __ bind(_entry);
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  __ call(Runtime1::entry_for(Runtime1::throw_div0_exception_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
 debug_only(__ os_breakpoint());              // call never returns
}


// Implementation of NewInstanceStub

NewInstanceStub::NewInstanceStub(LIR_Opr result, ciInstanceKlass* klass, CodeEmitInfo* info) {
  _result = result; 
  _klass = klass;
  _info = new CodeEmitInfo(info);
}


void NewInstanceStub::emit_code(LIR_Assembler* ce) {
  __ bind (_entry);
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PreNew);
  __ call (Runtime1::entry_for(Runtime1::new_instance_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(),_info->oop_map());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PostNew);
  __ jmp  (_continuation);
}


// Implementation of NewTypeArrayStub

NewTypeArrayStub::NewTypeArrayStub(LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {
  _length = length;
  _result = result;
  _info = new CodeEmitInfo(info);
}


void NewTypeArrayStub::emit_code(LIR_Assembler* ce) {
  __ bind (_entry);
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PreNew);
  __ call (Runtime1::entry_for(Runtime1::new_array_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(),_info->oop_map());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PostNew);
  __ jmp  (_continuation);
}


// Implementation of NewObjectArrayStub

NewObjectArrayStub::NewObjectArrayStub(LIR_Opr length, LIR_Opr result, CodeEmitInfo* info) {  
  _result = result;
  _length = length;
  _info = new CodeEmitInfo(info);
}


void NewObjectArrayStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PreNew);
  __ call (Runtime1::entry_for(Runtime1::new_array_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(),_info->oop_map());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PostNew);
  __ jmp  (_continuation);
}


// Implementation of MonitorAccessStubs

MonitorEnterStub::MonitorEnterStub(LIR_Opr obj_reg, CodeEmitInfo* info) : MonitorAccessStub(obj_reg) {
  _info = new CodeEmitInfo(info);
}

void MonitorEnterStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PreCall);
  assert0 (-frame::runtime_stub_frame_size + frame::xPAD == -16);
  __ st8 (RSP, -16, _obj_reg->as_register());
  __ call (StubRoutines::x86::blocking_lock_entry());
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PostCall);
  __ jmp(_continuation);
}

MonitorExitStub::MonitorExitStub(LIR_Opr obj_reg, LIR_Opr mark_reg) : MonitorAccessStub(obj_reg) {
  _mark_reg = mark_reg;
}

void MonitorExitStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);

  // obj_reg is always RSI
  assert0( R10 == _obj_reg->as_register() && RCX == _mark_reg->as_register());
  // note: non-blocking leaf routine => no call info needed
  // RCX = markWord, R10 = oop (not ref!) to unlock
  // Crushes RAX, RCX, R09, R10.
  __ call (StubRoutines::unlock_entry());

  __ jmp(_continuation);
}


// Implementation of patching: 

void PatchingStub::align_patch_site(MacroAssembler* masm) {
//We emit a long jump which needs to be atomically patched into a nop.
masm->align_with_nops(round_to(NativeGeneralJump::instruction_size,wordSize));
}

// LoadKlass Patching
LK_PatchingStub::LK_PatchingStub(MacroAssembler* masm, Register obj, Register tmp1, Register tmp2, CodeEmitInfo* info):
    PatchingStub(masm, info), _obj(obj), _tmp1(tmp1), _tmp2(tmp2) {
  assert0(_obj != noreg && _tmp1 != noreg && _tmp2 != noreg );
  align_patch_site(masm);
  masm->jmp2(_patch_code);    // jump to the patch stub
  masm->bind(_start);         // label for the code to-be-patched
  int adr = (int)(intptr_t)KlassTable::getKlassTableBase();
  masm->ld8(_obj, noreg, adr);  // load from ds:[ktb+0]
  _patch_offset1 = masm->rel_pc()-4; //
  _patch_offset2 = masm->lvb(RInOuts::a, _obj, _tmp1, RKeepIns::a, noreg, adr, false);
  masm->jmp2(_thread_test);   // jump to the thread test
masm->bind(_post_patch);
}

void LK_PatchingStub::emit_code(LIR_Assembler*ce){
  // static field accesses have special semantics while the class
  // initializer is being run so we emit a test which can be used to
  // check that this code is being executed by the initializing
  // thread.
  Register Rthr = _tmp1;
  Register Roop = _tmp2;
  __ block_comment(" being_initialized check");
__ bind(_thread_test);
  __ mov8   (Roop, _obj);
  __ getthr (Rthr);
  __ cvta2va(Roop);
  __ cmp8   (Rthr, Roop, instanceKlass::init_thread_offset_in_bytes() + sizeof(klassOopDesc));
  __ jeq    (_post_patch);   // passed the thread check

  // Now emit the patch record telling the runtime how to find the
  // pieces of the patch.  
__ block_comment("patch data encoded as mov4i");
__ bind(_patch_code);
  int off0 = _start.rel_pc(ce->masm());
  uint p1 = _patch_offset1 - off0;
  uint p2 = _patch_offset2 ? _patch_offset2 - off0 : 0;
  uint p3 = _post_patch.rel_pc(ce->masm()) - NativeGeneralJump::instruction_size - off0;
  assert0( p1<=127 && p2<=127 && p3<=127 );
  __ a_byte(0x3D);  __ emit4((p3<<16)|(p2<< 8)|(p1<< 0)); // cmp4i rax,#imm
  __ call (Runtime1::entry_for(Runtime1::load_klass_patching_id));
  _info->debug_scope()->set_should_reexecute();
  ce->add_call_info_here(_info);
  __ add_oopmap(__ offset(), _info->oop_map());
__ jmp(_start);
}

// AccessField Patching
AF_PatchingStub::AF_PatchingStub(MacroAssembler* masm, CodeEmitInfo* info): PatchingStub(masm, info) {
  align_patch_site(masm);
  masm->jmp2(_patch_code);    // jump to the patch stub
  masm->bind(_start);         // label for the code to-be-patched
}

void AF_PatchingStub::emit_code(LIR_Assembler*ce){
__ bind(_patch_code);
  int off0 = _start.rel_pc(ce->masm());
  uint p1 = _patch_offset1 - off0;
  uint p2 = _patch_offset2 ? _patch_offset2 - off0 : 0;
  assert0( p1<=127 && p2<=127 );
  __ a_byte(0x3D);  __ emit4((p2<< 8)|(p1<< 0)); // cmp4i rax,#imm
  __ call (Runtime1::entry_for(Runtime1::access_field_patching_id));
  _info->debug_scope()->set_should_reexecute();
  ce->add_call_info_here(_info);
  __ add_oopmap(__ offset(), _info->oop_map());
__ jmp(_start);
}


void ImplicitNullCheckStub::emit_code(LIR_Assembler* ce) {
  __ add_implicit_exception( _offset, __ offset() );
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  // CALL: pushes a return-address to hang the debug info off of
  __ call(Runtime1::entry_for(Runtime1::throw_null_pointer_exception_id));
  ce->add_call_info(__ rel_pc(),_info,false);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
debug_only(__ os_breakpoint());
}


void SimpleExceptionStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);

  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
if(_obj->is_valid()){
    // Place args below return address in outgoing stack frame
    assert0 (-frame::runtime_stub_frame_size + frame::xPAD == -16);
    assert0 (_obj->is_cpu_register());
    __ verify_not_null_oop(_obj->as_register(), MacroAssembler::OopVerify_OutgoingArgument);
    __ st8 (RSP, -16, _obj->as_register());
  }
__ call(Runtime1::entry_for(_stub));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
  debug_only(__ os_breakpoint()); // call never returns
}

void UncommonTrapStub::emit_code(LIR_Assembler*ce){
  __ bind(_entry);

  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);

  __ mov8i(RDI, _deopt_flavor);
  __ call(StubRoutines::uncommon_trap_entry());
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());

  debug_only(__ os_breakpoint()); // call never returns
}


ArrayStoreExceptionStub::ArrayStoreExceptionStub(CodeEmitInfo* info):
  _info(info) {
}


void ArrayStoreExceptionStub::emit_code(LIR_Assembler* ce) {
  __ bind(_entry);

  // full subtype check
  __ call (StubRoutines::x86::full_subtype_check());
  __ test4(RAX, RAX);
  __ jze  (_continuation); // zero => success

  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_Exception);
  __ call(Runtime1::entry_for(Runtime1::throw_array_store_exception_id));
  ce->add_call_info_here(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
  debug_only(__ os_breakpoint()); // call never returns
}


SafepointStub::SafepointStub(CodeEmitInfo*info):
  _info(info) {
}


void SafepointStub::emit_code(LIR_Assembler*ce){
  __ bind(_entry);
  __ call (StubRoutines::safepoint_trap_handler());  
ce->add_debug_info_for_branch(_info);
  __ add_oopmap(__ rel_pc(), _info->oop_map());
  __ verify_oopmap(_info->oop_map(), MacroAssembler::OopVerify_OopMap_PostSafepoint);
  __ jmp(_continuation);
}

#undef __
