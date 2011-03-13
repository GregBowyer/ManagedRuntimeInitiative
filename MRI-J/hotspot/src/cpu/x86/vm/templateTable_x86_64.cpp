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


#include "c2_globals.hpp"
#include "frame.hpp"
#include "interp_masm_pd.hpp"
#include "interpreter.hpp"
#include "interpreterRuntime.hpp"
#include "interpreter_pd.hpp"
#include "jvmtiExport.hpp"
#include "methodOop.hpp"
#include "objArrayKlass.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "templateTable.hpp"
#include "typeArrayOop.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "thread_os.inline.hpp"

#define __ _masm->


//----------------------------------------------------------------------------------------------------
// Platform-dependent initialization

void TemplateTable::pd_initialize() {
}


//----------------------------------------------------------------------------------------------------
// Individual instructions

void TemplateTable::nop() {
  // nothing to do
}

void TemplateTable::shouldnotreachhere() {
  __ stop("shouldnotreachhere bytecode");
}

void TemplateTable::aconst_null() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ mov8i(RCX_TOS,0);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::iconst(int value) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ mov8i(RCX_TOS,value);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::lconst(int value) {
  assert(value >= 0, "check this code");
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ push_tos_adj_sp(RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ mov8i(RCX_TOS,value);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fconst(int value) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  if (value == 0) {
    __ mov8i(RCX_TOS, 0);
}else if(value==1){
    __ mov8u(RCX_TOS, (uint32_t)0x3f800000LL);
}else if(value==2){
    __ mov8u(RCX_TOS, (uint32_t)0x40000000LL);
  } else {
    ShouldNotReachHere();
  }
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::dconst(int value) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ push_tos_adj_sp(RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  if (value == 0) {
    __ mov8i(RCX_TOS, 0);
}else if(value==1){
    __ mov8i(RCX_TOS, (int64_t)0x3ff0000000000000LL);
  } else {
    ShouldNotReachHere();
  }
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::bipush() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ lds1(RCX_TOS, RSI_BCP, 1); // load before bumping RSI_BCP
  __ dispatch_next_1(RInOuts::a,RAX_TMP); // bump RSI_BCP
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::sipush() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ lds2(RCX_TOS, RSI_BCP, 1); // happy misaligned load
  __ dispatch_next_1(RInOuts::a,RAX_TMP); // Bump RSI_BCP
  __ bswap2(RCX);
  __ movsx82(RCX,RCX);          // TaggedInterpreterStack needs proper upper bits
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::ldc(bool wide) {
  Label call_ldc, not_string, not_class, done;
  __ push_tos();                // Free RCX!
  __ getmoop(R10);              // Load method OOP
  __ ldref_lvb(RInOuts::a, R09, RAX_TMP, RKeepIns::a, R10, in_bytes(methodOopDesc::constants_offset()),false);
  if (wide) {
    __ ldz2  (RCX, RSI_BCP, 1); // unaligned load, works great on X86
    __ bswap2(RCX);
  } else {
    __ ldz1  (RCX, RSI_BCP, 1);
  }

  // Get tag array and tag
  // R09: constantPoolOop ref
  // RCX: index
  __ cvta2va  (R09);            // R09=constant pool oop
  __ ldref_lvb(RInOuts::a, RAX, R10, RKeepIns::a, R09, constantPoolOopDesc::tags_offset_in_bytes(),false);
  __ cvta2va  (RAX);            // RAX=tags array base
  // Load byte at RAX (tags array base) plus array_header plus (RCX<<0)
  __ ldz1     (RAX,RAX,typeArrayOopDesc::header_size_T_BYTE(),RCX,0);


  // check for unresolved string
  // R09: constantPoolOop
  // RCX: index
  // RAX: tags
__ cmp1i(RAX,JVM_CONSTANT_UnresolvedString);
__ jeq(call_ldc);
__ cmp1i(RAX,JVM_CONSTANT_UnresolvedClass);
__ jeq(call_ldc);
  __ cmp1i(RAX, JVM_CONSTANT_Class); // need to call vm to get java mirror of the class
  __ jne  (not_class);

__ bind(call_ldc);//Slow path
__ mov1i(RAX,wide);
  { Register Rtmp0 = __ get_free_reg(bytecode(), RAX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::ldc), RAX,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RAX, noreg, noreg);
  }
  __ move8(RCX_TOS,RAX);
__ jmp(done);

__ bind(not_class);
  // get entry
  // R09: constantPoolOop
  // RCX: index
  // RAX: tag
__ cmp1i(RAX,JVM_CONSTANT_String);
  __ jne  (not_string);
  __ ldref_lvb(RInOuts::a, RAX, R10, RKeepIns::a, R09, constantPoolOopDesc::header_size() * wordSize, RCX, 3, false);
  __ verify_oop (RAX, MacroAssembler::OopVerify_ConstantPoolLoad);
  __ move8(RCX_TOS,RAX);
__ jmp(done);

__ bind(not_string);
  // Must be an int or float
  __ lds4 (RCX_TOS, R09, constantPoolOopDesc::header_size() * wordSize, RCX, 3);

  __ bind (done);
}


void TemplateTable::ldc2_w() {
  __ getmoop(R10);              // Reload method OOP
  __ push_tos_adj_sp(RCX);
  __ ldref_lvb(RInOuts::a, R09, RAX_TMP, RKeepIns::a, R10, in_bytes(methodOopDesc::constants_offset()),false);
  __ ldz2  (RCX, RSI_BCP, 1);   // unaligned load, works great on X86
  __ dispatch_next_0(RInOuts::a, RAX_TMP);
  __ bswap2(RCX);
  __ cvta2va(R09);              // Strip metadata from method Constants
  __ dispatch_next_1(RInOuts::a, RAX_TMP);
  __ ld8   (RCX_TOS, R09, constantPoolOopDesc::header_size() * wordSize, RCX, 3);
  __ dispatch_next_2(RInOuts::a, RAX_TMP);
}


void TemplateTable::locals_index0(Register Rslot,int offset){
  __ ldz1(Rslot,RSI_BCP,offset);
}
void TemplateTable::locals_index0_wide(Register Rslot){
  __ ldz2(Rslot,RSI_BCP,2);
  __ bswap2(Rslot);
}

void TemplateTable::locals_index1(Register Rslot){
  __ lea (Rslot,RDX_LOC,0,Rslot,3);
}

void TemplateTable::locals_index(Register Rslot,int offset){
  locals_index0(Rslot,offset);
  locals_index1(Rslot);
}
void TemplateTable::locals_index_wide(Register Rslot){
  locals_index0_wide(Rslot);
  locals_index1     (Rslot);
}


void TemplateTable::iload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP); // load next bytecode
locals_index(R10,1);
__ push_tos();

  // Rewrite iload, iload  => fast_iload2
  //         iload, caload => fast_icaload
  if (RewriteBytecodes && RewriteFrequentPairs) {
Label caload_check,rewrite_fast_iload,done;

    // Don't rewrite an iload pair as an iload2 the first time we execute
    // them.  We need to determine if the second iload is going to become part
    // of another pair for correctness.  So if the *next* bytecode (now loaded
    // in RAX_TMP) is still an "iload" we don't know what it will become.
    // Wait until it turns into a "fast_iload".
__ cmp1i(RAX_TMP,Bytecodes::_iload);
__ jeq(done);

    // If the next bytecode has been rewritten as a fast_iload then it's safe to
    // use it as part of an iload2 since it wasn't used for another pair the
    // first time it was executed.
__ cmp4i(RAX_TMP,Bytecodes::_fast_iload);
    __ jne  (caload_check);
    __ st1i (RSI_BCP, 0, Bytecodes::_fast_iload2);
    __ jmp  (done);

    // If the next bytecode is a caload rewrite the pair as a fast_icaload.
__ bind(caload_check);
__ cmp1i(RAX_TMP,Bytecodes::_caload);
    __ jne  (rewrite_fast_iload);
    __ st1i (RSI_BCP, 0, Bytecodes::_fast_icaload);
    __ jmp  (done);

    // Rewrite the current iload as a fast_iload to avoid future checks.
__ bind(rewrite_fast_iload);
    __ st1i (RSI_BCP, 0, Bytecodes::_fast_iload);

    // Delay the rewriting decision until the next execution.
    __ bind (done);
  }

  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, R10, 0);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fast_iload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP); // load next bytecode
__ push_tos();
  locals_index0(R10, 1);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDX_LOC,0,R10,3);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fast_iload2() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  locals_index0(R10, 1);
__ push_tos();
  __ ld8  (RCX_TOS, RDX_LOC, 0, R10, 3);
  locals_index0(R10, 3);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ push_tos();
  __ ld8  (RCX_TOS, RDX_LOC, 0, R10, 3);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP); // load next bytecode
  locals_index0(R09, 1);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, RDX_LOC, 0, R09, 3); // optimized locals_index1 with ld8
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}

void TemplateTable::aload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP); // load next bytecode
  locals_index0(R09, 1);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, RDX_LOC, 0, R09, 3); // optimized locals_index1 with ld8
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::lload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  locals_index0(R09, 1);
  __ push_tos_adj_sp(RCX);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, RDX_LOC, 8, R09, 3); // optimized locals_index1_wide with ld8
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::dload() { lload(); }


void TemplateTable::wide_iload() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
locals_index_wide(R10);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, R10, 0);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}

void TemplateTable::wide_fload() { wide_iload(); }


void TemplateTable::wide_aload() {
  __ dispatch_next_0(RInOuts::a,R09);
locals_index_wide(RAX);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,R09);
  __ ld8(RCX_TOS, RAX, 0);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ dispatch_next_2(RInOuts::a,R09);
}

void TemplateTable::wide_lload() {
  __ dispatch_next_0(RInOuts::a,R09);
locals_index_wide(RAX);
  __ push_tos_adj_sp(RCX_TOS);
  __ dispatch_next_1(RInOuts::a,R09);
  __ ld8(RCX_TOS, RAX, 8);
  __ dispatch_next_2(RInOuts::a,R09);
}

void TemplateTable::wide_dload() { wide_lload(); }


void TemplateTable::laload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ ld8(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_LONG),RCX_TOS,3);
  __ store_sentinel(RKeepIns::a,RDI_JEX,-8);
  __ dispatch_next_2(RInOuts::a,R09);
}
void TemplateTable::daload(){laload();}


void TemplateTable::iaload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ sub8i(RDI_JEX, 8);         // pop stack
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ lds4(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_INT),RCX_TOS,2);
  __ dispatch_next_2(RInOuts::a,R09);
}
void TemplateTable::faload() { iaload(); }


void TemplateTable::aaload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ sub8i(RDI_JEX, 8);         // pop stack
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ ldref_lvb(RInOuts::a, R10, R11, RKeepIns::a, RAX, arrayOopDesc::base_offset_in_bytes(T_OBJECT),RCX_TOS,3,true);
  __ verify_oop(R10, MacroAssembler::OopVerify_Load);
  __ move8(RCX_TOS,R10);
  __ dispatch_next_2(RInOuts::a,R09);
}


void TemplateTable::baload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ sub8i(RDI_JEX, 8);         // pop stack
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ lds1(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_BYTE),RCX_TOS,0);
  __ dispatch_next_2(RInOuts::a,R09);
}


void TemplateTable::caload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ sub8i(RDI_JEX, 8);         // pop stack
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ ldz2(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_CHAR),RCX_TOS,1);
  __ dispatch_next_2(RInOuts::a,R09);
}


void TemplateTable::fast_icaload() {
  locals_index0(R10,1);         // iload, #, caload
  __ dispatch_next_0(RInOuts::a,R09);
  __ move8(RAX,RCX);            // array to RAX
  __ cvta2va(RAX);              // Strip metadata
  __ ldz4(RCX_TOS, RDX_LOC, 0, R10, 3); // place index in TOS for exception handler
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ ldz2(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_CHAR),RCX_TOS,1);
  __ dispatch_next_2(RInOuts::a,R09);
}


void TemplateTable::saload() {
  __ dispatch_next_0(RInOuts::a,R09);
  __ ld8  (RAX,RDI_JEX,-8);     // array
  __ sub8i(RDI_JEX, 8);         // pop stack
  __ cvta2va(RAX);              // Strip metadata
  __ cmp4(RCX_TOS,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ dispatch_next_1(RInOuts::a,R09);
  __ lds2(RCX_TOS, RAX, arrayOopDesc::base_offset_in_bytes(T_SHORT),RCX_TOS,1);
  __ dispatch_next_2(RInOuts::a,R09);
}


void TemplateTable::lload(int n) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ push_tos_adj_sp(RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8(RCX_TOS, RDX_LOC, (n+1) * reg_size);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::dload(int n){lload(n);}


void TemplateTable::iload(int n) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ lds4(RCX_TOS, RDX_LOC, n * reg_size);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::fload(int n) {  iload(n);  }

void TemplateTable::aload(int n) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS, RDX_LOC, n*reg_size);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::aload_0() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);         // load next bytecode
__ push_tos();

  // Rewrite aload_0, agetfield => fast_aaccess_0 and
  //         aload_0, igetfield => fast_iaccess_0 and
  //         aload_0, fgetfield => fast_faccess_0
  if (RewriteFrequentPairs) {
Label rewrite_fast_aload_0,rewrite,done;

    // Don't rewrite an aload_0, getfield pair the first time we execute them.
    // We need to determine what type the getfield operates on.
__ cmp8i(RAX_TMP,Bytecodes::_getfield);
__ jeq(done);

    // aload_0, agetfield => fast_aaccess_0
__ mov8i(R09,Bytecodes::_fast_aaccess_0);
__ cmp8i(RAX_TMP,Bytecodes::_fast_agetfield);
__ jeq(rewrite);

    // aload_0, igetfield => fast_iaccess_0
__ mov8i(R09,Bytecodes::_fast_iaccess_0);
__ cmp8i(RAX_TMP,Bytecodes::_fast_igetfield);
__ jeq(rewrite);

    // aload_0, fgetfield => fast_faccess_0
__ mov8i(R09,Bytecodes::_fast_faccess_0);
__ cmp8i(RAX_TMP,Bytecodes::_fast_fgetfield);
__ jeq(rewrite);

    // Rewrite the current aload_0 as a fast_aload_0 to avoid future checks.
__ bind(rewrite_fast_aload_0);
__ mov8i(R09,Bytecodes::_fast_aload_0);

    // Store to rewrite the bytecode.
    __ bind (rewrite);
    if (RewriteBytecodes) {
      __ st1(RSI_BCP, 0, R09);
    }
    
    // Delay the rewritting decision until the next execution.
    __ bind (done);
  }

  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS, RDX_LOC, 0);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::istore() {
  locals_index0(R10, 1);
  __ single_slot_locals_store_check(RInOuts::a, R10, R09, RAX_TMP, -1);

  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8(R10, 0, RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::astore() {
  if (VerifyOopLevel > 0) {
Label possible_jsr_index;
    __ cmp8i(RCX_TOS, 65536);
    __ jbl  (possible_jsr_index);
    __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
__ bind(possible_jsr_index);
  }
  istore();
}
void TemplateTable::fstore() { istore(); }

void TemplateTable::lstore() {
locals_index(R10,1);
  __ double_slot_locals_store_check(RInOuts::a, R10, R09, RAX );

  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8   (R10, 8, RCX_TOS);
  // Stamp bad slot sentinel value into the empty slot
  __ store_sentinel(RKeepIns::a,R10, 0);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ adj_sp_pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::dstore() { lstore(); }

void TemplateTable::wide_istore() {
  // Loads the 2-byte index value in R10
  locals_index0_wide(R10);
  __ single_slot_locals_store_check(RInOuts::a, R10, R09, RAX_TMP, -1);

  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8(R10, 0, RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}

void TemplateTable::wide_fstore() { wide_istore(); }
void TemplateTable::wide_astore() { wide_istore(); }

void TemplateTable::wide_lstore() {
  // Loads the 2-byte index value in R10
locals_index_wide(R10);
  __ double_slot_locals_store_check(RInOuts::a, R10, R09, RAX );

  __ dispatch_next_0(RInOuts::a,RAX);
  __ st8   (R10, reg_size, RCX_TOS);
  // Stamp bad slot sentinel value into the empty slot
  __ store_sentinel(RKeepIns::a,R10, 0);
  __ dispatch_next_1(RInOuts::a,RAX);
__ adj_sp_pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX);
}

void TemplateTable::wide_dstore() { wide_lstore(); }

void TemplateTable::iastore() {
  __ ld8  (RAX,RDI_JEX,-16);    // array ref
  __ move8(R09,RCX);            // Value to R09
  __ ldz4 (RCX,RDI_JEX, -8);    // index to RCX
  __ cvta2va(RAX);              // Strip metadata from array
  __ cmp4 (RCX,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ st4  (RAX,arrayOopDesc::base_offset_in_bytes(T_INT),RCX,2, R09);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDI_JEX,-24);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ sub8i(RDI_JEX,24);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::fastore() { iastore(); }


void TemplateTable::lastore() {
  __ ld8  (RAX,RDI_JEX,-24);    // array ref
  __ move8(R09,RCX);            // Value to R09
  __ ldz4 (RCX,RDI_JEX,-16);    // index to RCX
  __ cvta2va(RAX);              // Strip metadata from array
  __ cmp4 (RCX,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ st8  (RAX,arrayOopDesc::base_offset_in_bytes(T_LONG),RCX,3, R09);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDI_JEX,-32);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ sub8i(RDI_JEX,32);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::dastore() { lastore(); }


void TemplateTable::aastore() {
  Label throw_aioob, store_ok, do_resolve;
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Store);
  Register R10_aryref = R10;
  Register R09_idx    = R09;
  Register RAX_ary    = RAX;
  __ ld8  (R10_aryref,RDI_JEX,-16); // R10 = array ref
  __ cvta2va(RAX_ary, R10_aryref);  // RAX = array oop
  __ ldz4 (R09_idx,   RDI_JEX, -8); // R09 = index
  __ sub8i(RDI_JEX,16);             // pop both words
  __ cmp4(R09_idx,RAX_ary,arrayOopDesc::length_offset_in_bytes());
  __ jae (throw_aioob);
  // RCX_TOS: value
  // R09_idx: index
  // R10_aryref: array ref
  // RAX_ary: array oop

  // R11_kid = value.kid
  Register R11_kid = R11;
  if( KIDInRef ) {
    __ ref2kid(R11_kid, RCX_TOS); // r11 hold value.kid
    __ jze  (store_ok);  // store of null always ok
  } else {
    __ null_chk(RCX_TOS, store_ok); // store of null always ok
    __ ref2kid(R11_kid, RCX_TOS); // r11 hold value.kid
  }
  // RAX = ref2klass(R10).element_klass().klassId()
  Register RAX_ekid = RAX;
  __ ldz4 (RAX_ekid, RAX_ary, objArrayOopDesc::ekid_offset_in_bytes());

  // does ekid match value.kid?
  __ cmp4(R11_kid, RAX_ekid);
  __ jeq (store_ok);

  // store to Object[]?
  __ cmp4i  (RAX_ekid, java_lang_Object_kid);
  __ jeq    (store_ok);

  // Need full type check
  __ call (StubRoutines::x86::full_subtype_check());
  __ test4(RAX, RAX);
  // on failure throw the array store check exception
  // needs value still in RCX_TOS, so the exception detail message includes value's klass name
__ jne(Interpreter::_throw_ArrayStoreException_entry);

__ bind(store_ok);

  // Potential stack escape may cause a GC here in +UseSBA mode.  This GC
  // point is not really at any particular BCI - some (but not all) aastore
  // args are popped, but the store has not happened.  Tagged interpreter
  // stack does not depend on BCI.
  // RCX_TOS: value
  // R09_IDX: index
  // R10_ARYREF: array
  Label reload_and_retry, retry;
  if (UseSBA) {
    __ st8   (RDI_JEX, 16, RCX_TOS); // push value (array and index already on stack)
    __ add8i (RDI_JEX, 24);          // incase they need reloading following an SBA escape
    __ bind  (retry);
  }
  __ ref_store_with_check(RInOuts::a,R10_aryref/*base*/,arrayOopDesc::base_offset_in_bytes(T_OBJECT)/*offset*/,
                          R09_idx/*index*/,3/*scale*/,RCX_TOS/*value*/,
                          RAX_TMP/*tmp1*/,R11/*tmp2*/,
                          reload_and_retry);
  if (UseSBA) {
    // Success - Pop stack and begin dispatch
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ ld8 (RCX_TOS,RDI_JEX,-32);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ sub8i(RDI_JEX,32);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    // SBA escape occured - out of line code
__ bind(reload_and_retry);
    __ ld8 (RCX_TOS,    RDI_JEX,-8 ); // Reload value
    __ ldz4(R09_idx,    RDI_JEX,-16); // Reload index
    __ ld8 (R10_aryref, RDI_JEX,-24); // Reload array ref
__ jmp(retry);
  } else {
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ pop_tos();
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
  }

  // Come here to throw array index out of bounds exception
__ bind(throw_aioob);
  __ move8(RCX_TOS,R09_idx);    // index into TOS
__ jmp(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
}


void TemplateTable::bastore() {
  __ ld8  (RAX,RDI_JEX,-16);    // array ref
  __ move8(R09,RCX);            // Value to R09
  __ ld8  (RCX,RDI_JEX, -8);    // index to RCX
  __ cvta2va(RAX);              // Strip metadata from array
  __ cmp4 (RCX,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ st1  (RAX,arrayOopDesc::base_offset_in_bytes(T_BYTE),RCX,0, R09);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDI_JEX,-24);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ sub8i(RDI_JEX,24);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::castore() {
  __ ld8  (RAX,RDI_JEX,-16);    // array ref
  __ move8(R09,RCX);            // Value to R09
  __ ld8  (RCX,RDI_JEX, -8);    // index to RCX
  __ cvta2va(RAX);              // Strip metadata from array
  __ cmp4 (RCX,RAX,arrayOopDesc::length_offset_in_bytes());
__ jae(Interpreter::_throw_ArrayIndexOutOfBoundsException_entry);
  __ st2  (RAX,arrayOopDesc::base_offset_in_bytes(T_CHAR),RCX,1, R09);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDI_JEX,-24);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ sub8i(RDI_JEX,24);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::sastore() { castore(); }


void TemplateTable::istore(int n) {
  __ single_slot_locals_store_check(RInOuts::a, R10, R09, RAX_TMP, n);

  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8(R10, 0, RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::fstore(int n) { istore(n); }
void TemplateTable::astore(int n) { istore(n); }

void TemplateTable::lstore(int n) {
  // equivalent to locals_index()
  __ lea(R10, RDX_LOC, n * reg_size);
  __ double_slot_locals_store_check(RInOuts::a, R10, R09, RAX );

  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8(R10, 8, RCX_TOS);

  // Stamp bad slot sentinel value into the empty slot
  __ store_sentinel(RKeepIns::a,R10,0);

  __ dispatch_next_1(RInOuts::a,RAX_TMP);
__ adj_sp_pop_tos();
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}
void TemplateTable::dstore(int n) { lstore(n); }


void TemplateTable::pop() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::pop2() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (RCX_TOS,RDI_JEX,-16);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ add8i(        RDI_JEX,-16);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::dup() {
  // stack: ..., a
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
  // stack: ..., a, a
}


void TemplateTable::dup_x1() {
  // stack: ..., a, b
  __ dispatch_next_0(RInOuts::a,RAX_TMP);  // [RDI-8]:a   RCX:b
  __ ld8  (R09,RDI_JEX,-8);    // [RDI-8]:a   RCX:b   R09:a 
  __ st8  (RDI_JEX,-8,RCX_TOS);// [RDI-8]:b   RCX:b   R09:a 
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ st8  (RDI_JEX,0,R09);     // [RDI-8]:b  [RDI+0]:a  RCX:b   
  __ add8i(RDI_JEX,8);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
  // stack: ..., b, a, b
}


void TemplateTable::dup_x2() {
  // stack: ..., a, b, c        // [RDI-16]:a  [RDI-8]:b  RCX:c
  __ ld8  (R09,RDI_JEX,-8);     // [RDI-16]:a   R09   :b  RCX:c
  __ ld8  (RAX,RDI_JEX,-16);    //  RAX    :a   R09   :b  RCX:c
  __ st8  (RDI_JEX,-16,RCX);    // [RDI-16]:c   R09   :b  RCX:c
  __ st8  (RDI_JEX,-8,RAX);     // [RDI-16]:c  [RDI-8]:a  RCX:c
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ st8  (RDI_JEX, 0,R09);     // [RDI-16]:c  [RDI-8]:a [RDI+0]:b  RCX:c
  __ add8i(RDI_JEX,8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
  // stack: ..., c, a, b, c
}


void TemplateTable::dup2() {
  // stack: ..., a, b
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (R09,RDI_JEX,-8);
  __ st8  (RDI_JEX,0,RCX);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ st8  (RDI_JEX,8,R09);
  __ add8i(RDI_JEX,16);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
  // stack: ..., a, b, a, b
}


void TemplateTable::dup2_x1() {
  // stack: ..., a, b, [cx: c]
  __ ld8  (R09,RDI_JEX,-8 );    // stack: ...,       a, [09: b], [cx: c]
  __ ld8  (RAX,RDI_JEX,-16);    // stack: ..., [ax: a], [09: b], [cx: c]
  __ st8  (RDI_JEX,-16,R09);    // stack: ..., b, [ax: a], [09: b], [cx: c]
  __ st8  (RDI_JEX, -8,RCX);    // stack: ..., b, c, [ax: a], [09: b], [cx: c]
  __ st8  (RDI_JEX,  0,RAX);    // stack: ..., b, c, a, [09: b], [cx: c]
  __ st8  (RDI_JEX,  8,R09);    // stack: ..., b, c, a, b, [cx: c]
  __ add8i(RDI_JEX,16);
  // stack: ..., b, c, a, b, c
}


void TemplateTable::dup2_x2() { 
 // stack: ..., a, b, c, [rcx:d]
  __ ld8 (R09,RDI_JEX,-8 );  // stack: ..., a, b, [r09:c], [rcx:d]
  __ ld8 (RAX,RDI_JEX,-16);  // stack: ..., a, [rax:b], [r09:c], [rcx:d]
  __ ld8 (R10,RDI_JEX,-24);  // stack: ..., [r10:a], [rax:b], [r09:c], [rcx:d]
  __ st8 (RDI_JEX,-24,R09);  // stack: ..., c, [r10:a], [rax:b], [r09:c], [rcx:d]
  __ st8 (RDI_JEX,-16,RCX);  // stack: ..., c, d, [r10:a], [rax:b], [r09:c], [rcx:d]
  __ st8 (RDI_JEX, -8,R10);  // stack: ..., c, d, a, [rax:b], [r09:c], [rcx:d]
  __ st8 (RDI_JEX,  0,RAX);  // stack: ..., c, d, a, b, [r09:c], [rcx:d]
  __ st8 (RDI_JEX,  8,R09);  // stack: ..., c, d, a, b, c, [rcx:d]
  __ add8i(RDI_JEX,16);
  // stack: ..., c, d, a, b, c, d
}


void TemplateTable::swap() {
  // stack: ..., a, b
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ xchg(RCX_TOS,RDI_JEX,-8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
  // stack: ..., b, a
}


void TemplateTable::iop2(Operation op) {
if(op==mul){
    __ sub8i(RDI_JEX,8);        // pop stack
    __ ldz4 (RAX,RDI_JEX,0);    // load arg
    __ mov8 (R09,RDX);          // Save RDX
    __ mul4 (RCX);              // RCX:RAX ==>  RDX:RAX
    __ mov8 (RDX,R09);          // Restore RDX
    __ movsx84(RCX,RAX); 

  } else {
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ sub8i(RDI_JEX,8);        // pop stack
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    switch (op) {
    case  add: __ add4(RCX_TOS,RDI_JEX,0);                   __ movsx84(RCX_TOS,RCX_TOS); break;
    case  sub: __ sub4(RCX_TOS,RDI_JEX,0); __ neg4(RCX_TOS); __ movsx84(RCX_TOS,RCX_TOS); break;
    case _and: __ and8(RCX_TOS,RDI_JEX,0); break;
    case  _or: __ or_8(RCX_TOS,RDI_JEX,0); break;
    case _xor: __ xor8(RCX_TOS,RDI_JEX,0); break;
    case  shl: __ ldz4(R09,RDI_JEX,0); __ shl4(R09,RCX); __ movsx84(RCX_TOS,R09); break;
    case  shr: __ ldz4(R09,RDI_JEX,0); __ sar4(R09,RCX); __ movsx84(RCX_TOS,R09); break;
    case ushr: __ ldz4(R09,RDI_JEX,0); __ shr4(R09,RCX); __ movsx84(RCX_TOS,R09); break;
    default: ShouldNotReachHere();
    }
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
  }
}


void TemplateTable::lop2(Operation op) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (R10, RDI_JEX, -16);
  __ add8i(RDI_JEX, -16);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  switch (op) {
  case  add: __ add8(RCX_TOS,R10); break;
  case  sub: __ sub8(RCX_TOS,R10); __ neg8(RCX_TOS); break;
  case _and: __ and8(RCX_TOS,R10); break;
  case  _or: __ or_8(RCX_TOS,R10); break;
  case _xor: __ xor8(RCX_TOS,R10); break;
  default: ShouldNotReachHere();
  }
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::idiv() {
  Label normal_case, special_case, divby0;
  __ jrcxz1(divby0);
  __ ld8  (RAX,RDI_JEX,-8);     // Dividend
  __ sub8i(RDI_JEX,8);
  __ move8(R09,RDX);            // Save DX before clobber
  // check for special case: min_int/-1
  __ cmp4i(RAX, 0xFFFFFFFF80000000L); // 0x8000,0000 sign extended to pass the sanity checks
  __ jne  (normal_case);
  __ cmp4i(RCX, -1);
  __ jeq  (special_case);

__ bind(normal_case);
  __ cdq4 ();                   // RDX = sign-extend of RAX
  __ div4 (RCX);                // Signed divide of RDX:RAX/RCX

  // normal and special case exit
__ bind(special_case);
  __ movsx84(RCX_TOS,RAX);      // Quotient back into TOS
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ move8(RDX,R09);            // Reload DX
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

__ bind(divby0);
__ jmp(Interpreter::_throw_ArithmeticException_entry);
}


void TemplateTable::irem() {
  Label normal_case, special_case, divby0;
  __ jrcxz1(divby0);
  __ ld8  (RAX,RDI_JEX,-8);     // Dividend
  __ sub8i(RDI_JEX,8);
  __ move8(R09,RDX);            // Save DX before clobber
  // check for special case: min_int/-1
  __ cmp4i(RAX, 0xFFFFFFFF80000000L); // 0x8000,0000 sign extended to pass the sanity checks
  __ jne  (normal_case);
  __ mov8i(RDX, 0); // prepare rcx for possible special case (where remainder = 0)
  __ cmp4i(RCX, -1);
  __ jeq  (special_case);

__ bind(normal_case);
  __ cdq4 ();                   // RDX = sign-extend of RAX
  __ div4 (RCX);                // Signed divide of RDX:RAX/RCX

  // normal and special case exit
__ bind(special_case);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ movsx84(RCX_TOS,RDX);      // Remainder back into TOS
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ move8(RDX,R09);            // Reload DX
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

__ bind(divby0);
__ jmp(Interpreter::_throw_ArithmeticException_entry);
}


void TemplateTable::lmul() {
  __ ld8  (RAX, RDI_JEX, -16);
  __ add8i(     RDI_JEX, -16);
  __ move8(R09,RDX);            // Save RDX
  __ mul8 (RCX);                // Signed multiply RAX * RCX into RDX:RAX
  __ move8(RDX,R09);            // Restore RDX
  __ move8(RCX,RAX);            // low bits into RCX
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::ldiv() {
  Label normal_case, special_case, divby0;
  address min_jlong_addr = __ long_constant(min_jlong);
assert(min_jlong_addr!=NULL,"min jlong address required");
  __ jrcxz1(divby0);
  __ ld8  (RAX, RDI_JEX, -16);  // Dividend
  __ sub8i(RDI_JEX, 16);        // Pop long arg
  __ move8(R09,RDX);            // Save DX before clobber
  // check for special case: min_jlong/-1
  __ cmp8 (RAX, min_jlong_addr);
  __ jne  (normal_case);
  __ cmp8i(RCX, -1);
  __ jeq  (special_case);

__ bind(normal_case);
  __ cdq8 ();                   // RDX = sign-extend of RAX
  __ div8 (RCX);                // Signed divide of RDX:RAX/RCX

  // normal and special case exit
__ bind(special_case);
  __ move8(RCX_TOS,RAX);        // Quotient back into TOS
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ move8(RDX,R09);            // Reload DX
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

__ bind(divby0);
__ jmp(Interpreter::_throw_ArithmeticException_entry);
}


void TemplateTable::lrem() {
  Label normal_case, special_case, divby0;
  address min_jlong_addr = __ long_constant(min_jlong);
assert(min_jlong_addr!=NULL,"min jlong address required");
  __ jrcxz1(divby0);
  __ ld8  (RAX, RDI_JEX, -16);  // Dividend
  __ sub8i(RDI_JEX, 16);        // Pop long arg
  __ move8(R09,RDX);            // Save DX before clobber
  // check for special case: min_jlong/-1
  __ cmp8 (RAX, min_jlong_addr);
  __ jne  (normal_case);
  __ mov8i(RDX,0);              // In case we need the special-case
  __ cmp8i(RCX, -1);
  __ jeq  (special_case);

__ bind(normal_case);
  __ cdq8 ();                   // RDX = sign-extend of RAX
  __ div8 (RCX);                // Signed divide of RDX:RAX/RCX

  // normal and special case exit
__ bind(special_case);
  __ move8(RCX_TOS,RDX);        // Remainder back into TOS
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ move8(RDX,R09);            // Reload DX
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

__ bind(divby0);
__ jmp(Interpreter::_throw_ArithmeticException_entry);
}


void TemplateTable::lshl() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (R10,RDI_JEX,-8);     // load the long to shift
  __ add8i(RDI_JEX,-8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ shl8 (R10,RCX);            // RCX holds count
  __ move8(RCX,R10);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::lshr() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (R10,RDI_JEX,-8);     // load the long to shift
  __ add8i(RDI_JEX,-8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ sar8 (R10,RCX);            // RCX holds count
  __ move8(RCX,R10);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::lushr() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ ld8  (R10,RDI_JEX,-8);     // load the long to shift
  __ add8i(RDI_JEX,-8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ shr8 (R10,RCX);            // RCX holds count
  __ move8(RCX,R10);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fop2(Operation op) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ mov4(F00,RCX_TOS);
  __ sub8i(RDI_JEX,8);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  switch (op) {
  case  add:                    __ addf(F00,RDI_JEX,0); __ mov4 (RCX_TOS,F00);  break;   
  case  mul:                    __ mulf(F00,RDI_JEX,0); __ mov4 (RCX_TOS,F00);  break;   
  case  sub: __ ld4(F01,RDI_JEX,0); __ subf(F01,F00  ); __ mov4 (RCX_TOS,F01);  break;   
  case  div: __ ld4(F01,RDI_JEX,0); __ divf(F01,F00  ); __ mov4 (RCX_TOS,F01);  break;   
  case  rem: __ ld4(F01,RDI_JEX,0); __ remf(F01,F00  ); __ mov4 (RCX_TOS,F01);  break;
  default: ShouldNotReachHere();
  }
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::dop2(Operation op) {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ mov8 (F00, RCX_TOS);
  __ sub8i(RDI_JEX,16);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  switch (op) {
  case  add:                    __ addd(F00,RDI_JEX,0); __ mov8(RCX_TOS,F00);  break;   
  case  mul:                    __ muld(F00,RDI_JEX,0); __ mov8(RCX_TOS,F00);  break;   
  case  sub: __ ld8(F01,RDI_JEX,0); __ subd(F01,F00  ); __ mov8(RCX_TOS,F01);  break;   
  case  div: __ ld8(F01,RDI_JEX,0); __ divd(F01,F00  ); __ mov8(RCX_TOS,F01);  break;
  case  rem: __ ld8(F01,RDI_JEX,0); __ remd(F01,F00  ); __ mov8(RCX_TOS,F01);  break;
  default: ShouldNotReachHere();
  }
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::ineg() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ neg8(RCX_TOS);             // 8-byte negate to keep high bits properly sign-ex
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::lneg() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ neg8(RCX_TOS);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fneg() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ btc8i(RCX_TOS,31);         // Flip sign bit
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::dneg() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ btc8i(RCX_TOS,63);         // Flip sign bit
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::iinc() {
  // This normally simple bytecode must maintain a number of invariants in our
  // implementation:
  // 1) if the JEX stack is empty RCX_TOS hold the last local variable
  // 2) correct sign extension is maintained to avoid junk in the msbs
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
locals_index(R09,1);
  __ lds1   (RCX,RSI_BCP,2); // RCX = increment amount
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ add4   (RCX,R09,0); // RCX += [R09,0]
  __ movsx84(RCX,RCX);   // Correct sign extension
  __ st8    (R09,0,RCX);
  __ pop_tos();          // Reload TOS or last local variable, that this bytecode may have changed
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::wide_iinc() {
  // See comments for iinc
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ push_tos();
locals_index_wide(R09);
  __ ldz2   (RCX, RSI_BCP, 4); // RCX = increment amount
  __ bswap2 (RCX);
  __ movsx82(RCX,RCX);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ add4   (RCX,R09,0); // RCX += [R09,0]
  __ movsx84(RCX,RCX);   // Correct sign extension
  __ st8    (R09,0,RCX);
  __ pop_tos();          // Reload TOS or last local variable, that this bytecode may have changed
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::convert() {
  switch (bytecode()) {
  case Bytecodes::_i2l:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ add8i(RDI_JEX, 8); // TOS is already 64bit wide, only adjust the stack pointer
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    // Stamp bad slot sentinel value into the empty slot
    __ store_sentinel (RKeepIns::a,RDI_JEX,-8);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_i2f:
    __ cvt_i2f(F00, RCX_TOS);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov4 (RCX_TOS,F00);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movsx84(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_i2d:
    __ cvt_i2d(F00, RCX_TOS);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov8 (RCX_TOS,F00);
    __ add8i(RDI_JEX, 8); // TOS is already 64bit wide, only adjust the stack pointer
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    // Stamp bad slot sentinel value into the empty slot
    __ store_sentinel (RKeepIns::a,RDI_JEX,-8);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_i2b:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movsx81(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_i2c:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movzx82(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
   
  case Bytecodes::_i2s:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movsx82(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;

  case Bytecodes::_l2i:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ movsx84(RCX_TOS,RCX_TOS);
    __ sub8i(RDI_JEX,8);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_l2f:
    __ cvt_l2f(F00, RCX_TOS);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov8 (RCX_TOS,F00);
    __ sub8i(RDI_JEX,8);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
     
  case Bytecodes::_l2d:
    __ cvt_l2d(F00, RCX_TOS);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov8(RCX_TOS,F00);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_f2i:
    __ mov4 (F00, RCX_TOS);
    __ corrected_f2i(RCX_TOS, F00, RAX_TMP);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movsx84(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
     
  case Bytecodes::_f2l:
    __ mov4 (F00, RCX_TOS);
    __ corrected_f2l(RCX_TOS, F00, RAX_TMP);
    __ add8i(RDI_JEX, 8); // TOS is already 64bit wide, only adjust the stack pointer
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    // Stamp bad slot sentinel value into the empty slot
    __ store_sentinel (RKeepIns::a,RDI_JEX,-8);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;

  case Bytecodes::_f2d:
    __ mov4 (F00,RCX_TOS);
    __ cvt_f2d(F00, F00);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov8 (RCX_TOS,F00);
    __ add8i(RDI_JEX, 8); // TOS is already 64bit wide, only adjust the stack pointer
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    // Stamp bad slot sentinel value into the empty slot
    __ store_sentinel (RKeepIns::a,RDI_JEX,-8);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_d2i:
    __ mov8 (F00,RCX_TOS);
    __ corrected_d2i(RCX, F00, RAX_TMP);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ sub8i(RDI_JEX,8);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ movsx84(RCX_TOS,RCX_TOS);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_d2l:
    __ mov8 (F00,RCX_TOS);      // double to F00
    __ corrected_d2l(RCX_TOS, F00, RAX_TMP);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  case Bytecodes::_d2f:
    __ dispatch_next_0(RInOuts::a,RAX_TMP);
    __ mov8 (F00,RCX_TOS);
    __ sub8i(RDI_JEX,8);
    __ cvt_d2f(F00, F00);
    __ dispatch_next_1(RInOuts::a,RAX_TMP);
    __ mov4(RCX_TOS,F00);
    __ dispatch_next_2(RInOuts::a,RAX_TMP);
    break;
    
  default: ShouldNotReachHere();
  }
}


void TemplateTable::lcmp() {
  Label done;
  // ..., stack2/tag[-24], stack1[-16], stack0/tag[-8], TOS => ..., TOS
  // Goal: RCX < DI[-16] ==> 1,  RCX > DI[-16] ==> -1, RCX==DI[-16] ==> 0
  __ mov8i(RAX,-1);
  __ dispatch_next_0(RInOuts::a,R09);
  __ cmp8 (RCX_TOS, RDI_JEX, -16);
  __ jgt  (done);               // RAX=-1 if greater
  __ setnz(RAX);                // AL=0 if equal, or 1 if less
  __ bind (done);
  __ dispatch_next_1(RInOuts::a,R09);
  __ add8i(RDI_JEX, -24);       // ...
  __ movsx81(RCX_TOS,RAX);      // 
  __ dispatch_next_2(RInOuts::a,R09);
}

// Compare two floating point values ..., value1, value2
// creating a result of -1, 0 or 1 depending on whether the values are less,
// equal or greater than each other.
void TemplateTable::float_cmp(bool is_float, int unordered_result) {
  // Intel floating point compares set flags as if the comparison were unsigned,
  // use some hacker's delight style magic to compute the result of the
  // comparison by "(value1 > value2 ? 1 : 0) - (value1 < value2 ? 1 : 0)".
  // If the floating point values are unordered then all flags are set, giving
  // the correct result if the unordered result is -1, otherwise we need to
  // ensure the result is 1 with an explicit branch.
  __ dispatch_next_0(RInOuts::a,RAX_TMP);         // Loads RAX
  if (is_float) {
    __ mov4 (F00,RCX);          // place value2 in F00
    __ ld4  (F01,RDI_JEX, -8);  // place value1 in F01
  } else {
    __ mov8 (F00,RCX);          // place value2 in F00
    __ ld8  (F01,RDI_JEX,-16);  // place value1 in F01
  }

  if (is_float) {
    __ fcmp(RCX, F01, F00, unordered_result);
  } else {
    __ dcmp(RCX, F01, F00, unordered_result);
  }

  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  if( is_float ) __ add8i(RDI_JEX, -1*reg_size);
  else           __ add8i(RDI_JEX, -3*reg_size);  
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::branch(bool is_jsr, bool is_wide) {
if(is_wide){
    __ ldz4  (RAX, RSI_BCP, 1); // unaligned load, works great on X86
    __ bswap4(RAX);
    __ movsx84(RAX,RAX);
  } else {
    __ ldz2  (RAX, RSI_BCP, 1); // unaligned load, works great on X86
    __ bswap2(RAX);
    __ movsx82(RAX,RAX);
  }
  // Handle all the JSR stuff here, then exit.  It's much shorter and cleaner
  // than intermingling with the non-JSR normal-branch stuff occuring below.
  // TMP0: branch offset
  if (is_jsr) {
__ push_tos();
    __ getmoop(RCX);        // Reload method OOP
    __ ldref_lvb(RInOuts::a, R10, R11, RKeepIns::a, RCX, in_bytes(methodOopDesc::const_offset()),false);
    __ cvta2va(R10);
    __ lea (RCX_TOS,RSI_BCP,(is_wide ? 5 : 3) - in_bytes(constMethodOopDesc::codes_offset()));
    __ sub8(RCX_TOS,R10);
    __ add8(RSI_BCP,RAX);
    // TOS contains a Java pointer type, but not an oop
__ dispatch_next(0);
    return;
  }

  // Normal (non-jsr) branch handling
  // RAX: branch offset
  __ add8(RSI_BCP, RAX);

Label forward_branch;
  __ test4(RAX,RAX);
  __ jge  (forward_branch);

  __ getmoop(R11);        // Reload method OOP
  __ ldz4 (RAX, R11, in_bytes(methodOopDesc::invocation_counter_offset() + InvocationCounter::counter_offset()));
  __ add4i(RAX, 1);
  __ st4  (R11, in_bytes(methodOopDesc::invocation_counter_offset() + InvocationCounter::counter_offset()), RAX);
  
  if( UseOnStackReplacement ) {
    Label no_overflow, no_osr_code;
    __ add4 (RAX, R11, in_bytes(methodOopDesc::  backedge_counter_offset() + InvocationCounter::counter_offset()));
    // Now RAX holds the summed counters together.  See if they overflow.
    __ cmp4i(RAX, C2CompileThreshold*C2OnStackReplacePercentage/100);
    __ jlt  (no_overflow);
    // Overflow, time to OSR
    __ push_tos();              // Un-cache TOS; GC may happen here
    __ mov8i(RCX,1);            // Is an OSR
    { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, RCX);
      __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::frequency_counter_overflow), RAX,
                 RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX, noreg, noreg);
    }
    // Compilation might not be allowed or might not have finished yet
__ untested("return from osr request");
    __ test4(RAX,RAX);
    __ jeq  (no_osr_code);
    // Jump to the osr code.
    __ jmp8 (RAX);              // Convert I-frame to C-frame and go
__ bind(no_osr_code);
    __ pop_tos();               // Re-cache TOS
__ bind(no_overflow);
  }

  // Backedges need to poll
Label Lpoll;
  __ getthr(RAX);
  __ cmp8i(RAX, in_bytes(JavaThread::please_self_suspend_offset()), 0 );
  __ jne  (Lpoll);

  Label Lcontinue(_masm);

__ bind(forward_branch);

  // continue at branch destination - but enclosing Template will also
  // need to dispatch_epilog and so needs the _step value.
  int stepsize = _masm->_dispatch_next_step;
__ dispatch_next(0);
  _masm->_dispatch_next_step = stepsize;

  // Please self suspend was set, go into safepoint (out of line)
__ bind(Lpoll);
__ push_tos();
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), noreg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, noreg, noreg, noreg);
  }
__ pop_tos();
__ jmp(Lcontinue);
}


// Note Condition in argument is TemplateTable::Condition
// arg scope is within class scope

void TemplateTable::if_0cmp(Condition cc) {
  // no pointers, integer only!
  Label not_taken;
  __ move8(RAX,RCX);
  __ pop_tos();                 // Blows flags
  __ test4(RAX,RAX);
  switch( cc ) {               
  case equal:         __ jne(not_taken); break;
  case not_equal:     __ jeq(not_taken); break;
  case less:          __ jge(not_taken); break;
  case less_equal:    __ jgt(not_taken); break;
  case greater:       __ jle(not_taken); break;
  case greater_equal: __ jlt(not_taken); break;
  }
  branch(false, false);
  __ bind(not_taken);
}


void TemplateTable::if_icmp(Condition cc) {
  // no pointers, integer only!
  Label not_taken;
  __ sub8i(RDI_JEX,16);         // pre-pop stack, sets flags
  __ cmp4 (RCX_TOS,RDI_JEX, 8); // load from beyond RDI_JEX
  __ ld8  (RCX_TOS,RDI_JEX, 0); // load from beyond RDI_JEX
  switch( cc ) {               
  case equal:         __ jne(not_taken); break;
  case not_equal:     __ jeq(not_taken); break;
  case less:          __ jle(not_taken); break;
  case less_equal:    __ jlt(not_taken); break;
  case greater:       __ jge(not_taken); break;
  case greater_equal: __ jgt(not_taken); break;
  }
  branch(false, false);
  __ bind  (not_taken);
}


void TemplateTable::if_nullcmp(Condition cc) {
  Label not_taken;
  __ move8(RAX,RCX);
  __ pop_tos();                 // Blows flags
  __ test8(RAX,RAX);
  switch (cc) {
  case equal:     __ jne (not_taken); break;
  case not_equal: __ jeq (not_taken); break;
  default: ShouldNotReachHere();      break;
  }
  branch(false, false);
  __ bind  (not_taken);
}


void TemplateTable::if_acmp(Condition cc) {
  Label not_taken;
  __ sub8i(RDI_JEX,16);         // pre-pop stack, sets flags
  __ cmp8 (RCX_TOS,RDI_JEX, 8); // load from beyond RDI_JEX
  __ ld8  (RCX_TOS,RDI_JEX, 0); // load from beyond RDI_JEX
  switch (cc) {
  case equal:     __ jne (not_taken); break;
  case not_equal: __ jeq (not_taken); break;
  default:        ShouldNotReachHere(); break;
  }
  branch(false, false);
  __ bind  (not_taken);
}


void TemplateTable::ret() {
  __ getmoop(R10);
  __ ldref_lvb(RInOuts::a, RAX, R11, RKeepIns::a, R10, in_bytes(methodOopDesc::const_offset()),false);
  __ cvta2va(RAX);
locals_index(R10,1);
  __ ld8 (R10, R10, 0);         // the bci index
  __ lea (RSI_BCP, RAX, in_bytes(constMethodOopDesc::codes_offset()), R10, 0);
__ dispatch_next(0);
}


void TemplateTable::wide_ret() {
  __ getmoop(R10);
  __ ldref_lvb(RInOuts::a, RAX, R11, RKeepIns::a, R10, in_bytes(methodOopDesc::const_offset()),false);
  __ cvta2va(RAX);
locals_index_wide(R11);
  __ ld8  (R10, R11, 0);        // the bci index
  __ lea  (RSI_BCP, RAX, in_bytes(constMethodOopDesc::codes_offset()), R10, 0);
__ dispatch_next(0);
}


void TemplateTable::tableswitch() {
  Label def_case, go;
  // The table holds aligned ints; we need to bump align BCP.
  __ lea  (R09,RSI_BCP,BytesPerInt);
__ and8i(R09,-BytesPerInt);
  // Table looks like:
  // 0xAA / alignment-pad
  // bci_offset_to_default (4 bytes)
  // low (4 bytes), high (4 bytes) - inclusive
  // [bci_offset (4 bytes each)]*

  __ ldz4(RAX, R09, 2*BytesPerInt); // load high
  __ bswap4(RAX);
  __ cmp4(RCX,RAX);
  __ jgt (def_case);            // index too large
  __ ldz4(RAX, R09, 1*BytesPerInt); // load low
  __ bswap4(RAX);
  __ sub4(RCX,RAX);             // bias into the table
  __ jlt (def_case);            // too low
  __ ldz4(RAX, R09, 3*BytesPerInt,RCX,2);

__ bind(go);
__ pop_tos();
  __ bswap4(RAX);               // Must bswap offset also
  __ movsx84(RAX,RAX);
  __ add8(RSI_BCP,RAX);         // Adjust BCP
Label forward_branch;
  __ test4(RAX,RAX);
  __ jge  (forward_branch);
  __ getmoop(R09);             // Reload method OOP
  __ add4i(R09, in_bytes(methodOopDesc::backedge_counter_offset() + InvocationCounter::counter_offset()), 1);
__ bind(forward_branch);
  
  // continue at branch destination - but enclosing Template will also
  // need to dispatch_epilog and so needs the _step value.
__ dispatch_next(0);
  
__ bind(def_case);
  __ ldz4(RAX, R09, 0);
__ jmp(go);
}


void TemplateTable::lookupswitch() {
  __ stop("lookupswitch bytecode should have been rewritten");
}  


void TemplateTable::fast_linearswitch() {
Label def_case,loop,found;
  // RCX holds the 4 byte index which we will compare for an exact match
  // against the table entries.  Since the table entries are the wrong
  // endianess we need to swap them on loading... instead we bswap RCX.
  __ move4 (RAX,RCX_TOS);       // Free up RCX
  __ bswap4(RAX);               // Reverse bytes to allow an exact match
  // The table holds aligned ints; we need to bump align BCP.
  __ lea  (R09,RSI_BCP,BytesPerInt);
__ and8i(R09,-BytesPerInt);
  // Table looks like:
  // 0xE3 / alignment-pad
  // bci_offset_to_default (4 bytes)
  // number_entries (4 bytes)
  // [match , bci_offset (4 bytes each)]*
  __ ldz4 (RCX, R09, 1*BytesPerInt);
  __ bswap4(RCX);               // Load & bswap count
  __ jrcxz1(def_case);          // Zero counts?  Hit default case

__ bind(loop);//table search
  __ cmp4 (RAX, R09, 0, RCX, 3/*yes: cmp 4 but scale by 8*/);
  __ jloopne1(loop);            // Dec RCX, jump if RCX!=0 and NZ
__ jeq(found);

__ bind(def_case);
  __ add8i(R09,-3*BytesPerInt);
  __ bind (found);
  __ lds4 (RAX, R09, 3 * BytesPerInt, RCX, 3);
  __ bswap4(RAX);               // Must bswap offset as well
  __ movsx84(RAX,RAX);
  __ add8 (RSI_BCP,RAX);
__ pop_tos();
Label forward_branch;
  __ test4(RAX,RAX);
  __ jge  (forward_branch);
  __ getmoop(R09);             // Reload method OOP
  __ add4i(R09, in_bytes(methodOopDesc::backedge_counter_offset() + InvocationCounter::counter_offset()), 1);
__ bind(forward_branch);
  
  // continue at branch destination - but enclosing Template will also
  // need to dispatch_epilog and so needs the _step value.
__ dispatch_next(0);
}


void TemplateTable::fast_binaryswitch() {
  // Implementation using the following core algorithm:
  //
  // int binary_search(int key, LookupswitchPair* array, int n) {
  //   // Binary search according to "Methodik des Programmierens" by
  //   // Edsger W. Dijkstra and W.H.J. Feijen, Addison Wesley Germany 1985.
  //   int i = 0;
  //   int j = n;
  //   while (i+1 < j) {
  //     // invariant P: 0 <= i < j <= n and (a[i] <= key < a[j] or Q)
  //     // with      Q: for all i: 0 <= i < n: key < a[i]
  //     // where a stands for the array and assuming that the (inexisting)
  //     // element a[n] is infinitely big.
  //     int h = (i + j) >> 1;
  //     // i < h < j
  //     if (key < array[h].fast_match()) {
  //       j = h;
  //     } else {
  //       i = h;
  //     }
  //   }
  //   // R: a[i] <= key < a[i+1] or Q
  //   // (i.e., if key is within array, i is the correct index)
  //   return i;
  // }

  // Register allocation
  const Register RAX_h   = RAX;
  const Register RCX_key = RCX_TOS; // already set
  const Register R08_i   = R08; // constant pool cache - restored at end
  const Register R09_ary = R09;
  const Register R10_tmp = R10;
  const Register R11_j   = R11;

  // Find array start
  // btw: should be able to get rid of this instruction (change offsets below)
  __ lea  (R09_ary, RSI_BCP, 3 * BytesPerInt); 
__ and8i(R09_ary,-BytesPerInt);

  // Initialize i & j
  __ mov8i(R08_i, 0);                 // i = 0;
  __ ldz4 (R11_j, R09_ary, -BytesPerInt); // j = length(array);

  // Convert j into native byteordering
  __ bswap4(R11_j);

  // And start
  Label entry;
  __ jmp  (entry);

  // binary search loop
  {
    Label loop;
    __ bind(loop);
    // int h = (i + j) >> 1;
    __ lea  (RAX_h, R08_i, 0, R11_j, 0); // h =  i + j;
__ shr4i(RAX_h,1);//h = (i + j) >> 1;
    // if (key < array[h].fast_match()) {
    //   j = h;
    // } else {
    //   i = h;
    // }
    // Convert array[h].match to native byte-ordering before compare
    __ ldz4 (R10_tmp, R09_ary, 0, RAX_h, 3);
    __ bswap4(R10_tmp);
    __ cmp4 (RCX_key, R10_tmp);
    // j = h if (key <  array[h].fast_match())
    __ cmov4lt(R11_j, RAX_h);
    // i = h if (key >= array[h].fast_match())
    __ cmov4ge(R08_i, RAX_h);
    // while (i+1 < j)
    __ bind (entry);
    __ lea  (RAX_h, R08_i, 1);  // i+1
    __ cmp4 (RAX_h, R11_j);     // i+1 < j
__ jlt(loop);
  }

  // end of binary search, result index is i (must check again!)
Label default_case,dispatch;
  // Convert array[i].match to native byte-ordering before compare
  __ ldz4 (RAX_TMP, R09_ary, 0, R08_i, 3);
  __ bswap4(RAX_TMP);
  __ cmp4 (RCX_key, RAX_TMP);
__ jne(default_case);

  // entry found -> RAX = offset
  __ ldz4 (RAX_TMP , R09_ary, BytesPerInt, R08_i, 3);
__ jmp(dispatch);

  // default case -> RAX = default offset
  __ bind (default_case);
  __ ldz4 (RAX_TMP, R09_ary, -2 * BytesPerInt);

  __ bind (dispatch);
  __ bswap4(RAX_TMP);          // Must bswap offset as well
  __ movsx84(RAX_TMP,RAX_TMP);
  __ add8 (RSI_BCP,RAX_TMP);
Label forward_branch;
  __ test4(RAX_TMP,RAX_TMP);
  __ jge  (forward_branch);
  __ getmoop(RAX_TMP);        // Reload method OOP
  __ add4i(RAX_TMP, in_bytes(methodOopDesc::backedge_counter_offset() + InvocationCounter::counter_offset()), 1);
__ bind(forward_branch);

  // continue at branch destination - but enclosing Template will also
  // need to dispatch_epilog and so needs the _step value.
  __ ld8  (R08_CPC,RSP,offset_of(IFrame,_cpc) ); // restore R08
  __ verify_not_null_oop(R08_CPC, MacroAssembler::OopVerify_Sanity);
  __ cvta2va(R08_CPC);
__ pop_tos();
__ dispatch_next(0);
}


void TemplateTable::_return(TosState state) {
assert(state==tos,"no TosStates supported");
  __ push_tos(); // spill the TOS before potentially calling VM code
  if( _desc->bytecode() == Bytecodes::_return_register_finalizer) {
    __ ld8  (R09, RDX_LOC, 0); // Load local 0 'this' for Object::<init>
    __ ref2klass(RInOuts::a, RAX_TMP, RCX /*temp*/, R09); // klassRef into RAX_TMP, from objectRef in R09
    __ cvta2va(RAX_TMP);        // Strip to Klass ptr
    Label skip_register_finalizer;
    __ test4i(RAX_TMP, Klass::access_flags_offset_in_bytes() + sizeof(oopDesc), noreg, 0, JVM_ACC_HAS_FINALIZER);
    __ jze  (skip_register_finalizer); // Jump if zero
    Register R11_this = R11;
    __ ld8  (R11_this, RDX_LOC, 0); // Load local 0 'this' for Object::<init>
    Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11_this);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::register_finalizer), noreg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R11_this, noreg, noreg);
    __ bind (skip_register_finalizer);
  }
  { Register Rtmp0 = __ get_free_reg(bytecode(), RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    // unlock if synchronized method and check that all monitors are unlocked
    __ remove_activation(RInOuts::a, RSI, RCX, Rtmp0, false,false);
  }
  // save result (push state before jvmti call and pop it afterwards) and notify jvmti
__ notify_method_exit(false);

  // CALLING CONVENTION:
  // This code acts as the classic "epilog" code for interpreter calls.
  // "post-call" code is in generate_return_entry.
  __ getthr(RCX);
  __ st8  (RCX,in_bytes(JavaThread::jexstk_top_offset()),RDX_LOC); // Update JT->_jexstk_top
  __ ld8  (RAX,RDI_JEX,-8);        // Load return value
  __ ld8  (RDI,RDX_LOC,0);         // Bonus extra return argument, if any
  __ add8i(RSP,sizeof(IFrame)-8); // pop stack 
__ ret();//return!
}


void TemplateTable::_freturn(TosState state){
assert(state==tos,"no TosStates supported");
  __ push_tos(); // spill the TOS before potentially calling VM code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    // unlock if synchronized method and check that all monitors are unlocked
    __ remove_activation(RInOuts::a, RSI, RCX, Rtmp0, false,false);
  }
__ notify_method_exit(false);

  // CALLING CONVENTION:
  // This code acts as the classic "epilog" code for interpreter calls.
  // "post-call" code is in generate_return_entry.
  __ getthr(RCX);
  __ st8  (RCX,in_bytes(JavaThread::jexstk_top_offset()),RDX_LOC); // Update JT->_jexstk_top
  __ ld8  (F00,RDI_JEX,-8);     // Load return value
  __ ld8  (RDI,RDX_LOC,0);      // Bonus extra return argument, if any
  __ add8i(RSP,sizeof(IFrame)-8); // pop stack 
__ ret();//return!
}


void TemplateTable::areturn(){
  //Label done;
  //__ jrcxz(done);               // null return?  No SBA escape
  //__ unimplemented("areturn - SBA check is missing");
  //__ bind (done);
  __ push_tos(); // spill the TOS before potentially calling VM code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    // unlock if synchronized method and check that all monitors are unlocked
    __ remove_activation(RInOuts::a, RSI, RCX, Rtmp0, false,false);
  }
__ notify_method_exit(false);

  // CALLING CONVENTION:
  // This code acts as the classic "epilog" code for interpreter calls.
  // "post-call" code is in generate_return_entry.
  __ getthr(RCX);
  __ st8  (RCX,in_bytes(JavaThread::jexstk_top_offset()),RDX_LOC); // Update JT->_jexstk_top
  __ ld8  (RAX,RDI_JEX,-8);     // Load return value
  __ ld8  (RDI,RDX_LOC,0);      // Bonus extra return argument, if any
  __ add8i(RSP,sizeof(IFrame)-8); // pop stack 
__ ret();//return!
}


// ----------------------------------------------------------------------------
// Volatile variables demand their effects be made known to all CPU's in
// order.  Store buffers on most chips allow reads & writes to reorder; the
// JMM's ReadAfterWrite.java test fails in -Xint mode without some kind of
// memory barrier (i.e., it's not sufficient that the interpreter does not
// reorder volatile references, the hardware also must not reorder them).
// 
// According to the new Java Memory Model (JMM):
// (1) All volatiles are serialized wrt to each other.  
// ALSO reads & writes act as aquire & release, so:
// (2) A read cannot let unrelated NON-volatile memory refs that happen after
// the read float up to before the read.  It's OK for non-volatile memory refs
// that happen before the volatile read to float down below it.
// (3) Similar a volatile write cannot let unrelated NON-volatile memory refs
// that happen BEFORE the write float down to after the write.  It's OK for
// non-volatile memory refs that happen after the volatile write to float up
// before it.
//
// We only put in barriers around volatile refs (they are expensive), not
// _between_ memory refs (that would require us to track the flavor of the
// previous memory refs).  Requirements (2) and (3) require some barriers
// before volatile stores and after volatile loads.  These nearly cover
// requirement (1) but miss the volatile-store-volatile-load case.  This final
// case is placed after volatile-stores although it could just as well go
// before volatile-loads.
void TemplateTable::volatile_barrier() {
__ unimplemented("TemplateTable::volatile_barrier()");
}

// ----------------------------------------------------------------------------
// Set temp, so that (CPC+Rcache+cPCOop::base_offset) is a ConstantPool Cache entry
void TemplateTable::resolve_cache_and_index0(RInOuts, Register Ridx, RKeepIns, Register R08_cpc, Register Rbcp,
                                             int byte_no, Label &do_resolve, Label &resolved) {
  assert0( R08_cpc == R08_CPC );
  assert0(byte_no == 1 || byte_no == 2);

  __ get_cache_index_at_bcp(RInOuts::a, Ridx, RKeepIns::a, Rbcp, 1);
  // See if byte at CPC + Ridx_cache*1 + offset is non-zero
  __ cmp1i (R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset()) + (1+byte_no), Ridx, 0, 0);
  __ jeq   (do_resolve);
  __ bind  (resolved);
}

// --- resolve_cache_and_index1
void TemplateTable::resolve_cache_and_index1(RInOuts, Register Rtmp0, Register Ridx,
                                             RKeepIns, Register Rdx_loc, Register Rsi_bcp, Register Rdi_jex, Register R08_cpc,
                                             int byte_no, Label &do_resolve, Label &resolved) {
  assert0( Rdx_loc == RDX_LOC && Rsi_bcp == RSI_BCP && Rdi_jex == RDI_JEX && R08_cpc == R08_CPC );
__ bind(do_resolve);
  // Set the _f1 & _f2 fields of cpCacheOop structure.
  switch (bytecode()) {
case Bytecodes::_getstatic://fall through
case Bytecodes::_putstatic://fall through
case Bytecodes::_getfield://fall through
  case Bytecodes::_putfield       : 
    __ push_tos(); // For get/put only, save/restore TOS around VM call
    __ mov8i(Ridx, (int)bytecode()); // Thread in RDI, bytecode in Ridx
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::resolve_get_put), noreg,
               Rdx_loc, Rsi_bcp, Rdi_jex, R08_cpc, Rtmp0, Ridx, noreg, noreg);
    __ pop_tos(); // For get/put only, save/restore TOS around VM call
    break;

case Bytecodes::_invokevirtual://fall through
case Bytecodes::_fast_invokevfinal://fall through
case Bytecodes::_invokespecial://fall through
case Bytecodes::_invokestatic://fall through
  case Bytecodes::_invokeinterface  : // TOS was pushed by the _invokeXXX before we got here
    // We have already packed the interpreter args in preperation for the
    // invoke.  So this is bascially a call_VM inlined with the pack step
    // removed.  This call loads classes & can trigger GC.  
    __ mov8i(RSI, (int)bytecode()); // Thread in RDI, bytecode in RSI
    __ call_VM_plain((address)InterpreterRuntime::resolve_invoke, RSI, noreg, noreg, noreg);
__ check_and_handle_popframe();
    // Check for exceptions
{Label no_exception;
      __ getthr(Rtmp0);
__ null_chk(Rtmp0,in_bytes(JavaThread::pending_exception_offset()),no_exception);
      // Forward_exception_entry *will* pop a frame, so push one now in case we
      // are going to catch this exception in this method.  We care not coming back
      // from the exception and all registers are free and the JES will be popped.
      __ call(StubRoutines::forward_exception_entry());
__ bind(no_exception);
    }
    // Reload RSI_BCP and R08_CPC and RDI_JEX
    __ unpack_interpreter_regs(RInOuts::a, Rtmp0, Rdx_loc, Rsi_bcp, Rdi_jex, R08_cpc);
    break;
  default: ShouldNotReachHere();  break;
  }
  // Reload the resolved index.
  // No fencing needed to force coherent read of _f1 & _f2 on X86.
  // Loads are not allowed to bypass loads.
  __ get_cache_index_at_bcp(RInOuts::a, Ridx, RKeepIns::a, Rsi_bcp, 1);
__ jmp(resolved);
}


// --- getfield_or_static
void TemplateTable::getfield_or_static(int byte_no, bool is_static) {
  // Leave TOS in RCX, do not push it.  Only if we call into the VM will
  // we want to push RCX_TOS
  Label do_resolve, resolved;
  Register RAX_scp = RAX; // RAX scaled cache pointer
  resolve_cache_and_index0(RInOuts::a, RAX_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  if (JvmtiExport::can_post_field_access()) {
__ unimplemented("getfield_or_static jvmti");
  }

  if (is_static) {
__ push_tos();
    __ ldref_lvb (RInOuts::a, RCX_TOS, R10, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f1_offset()), RAX_scp, 0, false);
    __ cvta2va(RCX);            // Strip metadata in a common place
  } else {
    __ cvta2va(RCX);            // Strip metadata in a common place
__ null_check(RCX_TOS);//Hardware NPE if base is null
  }
  // RCX holds the base address
  // RAX: scaled cache pointer
  __ ldz4(R10, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset   ()), RAX_scp,0);
  Register RAX_flags = RAX;
  __ ldz4(RAX_flags, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), RAX_scp,0);
  // RCX_TOS: base object
  // RAX: flags; low byte has the getfield type
  // R10: offset
__ shr4i(RAX_flags,ConstantPoolCacheEntry::tosBits);

Label post;
  Label ld_int, ld_object, ld_long, ld_short, ld_char, ld_double, ld_float, ld_byte;

  __ cmp1i(RAX_flags,T_OBJECT);
  __ jeq  (ld_object);
  __ cmp1i(RAX_flags,T_ARRAY);
  __ jeq  (ld_object);
  __ cmp1i(RAX_flags,T_INT);
  __ jeq  (ld_int);
  __ cmp1i(RAX_flags,T_BOOLEAN);
  __ jeq  (ld_byte);
  __ cmp1i(RAX_flags,T_BYTE);
  __ jeq  (ld_byte);
  __ cmp1i(RAX_flags,T_LONG);
  __ jeq  (ld_long);
  __ cmp1i(RAX_flags,T_CHAR);
  __ jeq  (ld_char);
  __ cmp1i(RAX_flags,T_SHORT);
  __ jeq  (ld_short);
  __ cmp1i(RAX_flags,T_DOUBLE);
  __ jeq  (ld_double);
  __ cmp1i(RAX_flags,T_FLOAT);
  __ jeq  (ld_float);
  __ os_breakpoint(); // shorter version of should-not-reach-here to keep branches short

  Register RAX_fbc = RAX; // possible fast bytecode substitute
  // Bytes & Booleans
__ bind(ld_byte);
  if( !is_static ) __ mov1i(RAX_fbc, Bytecodes::_fast_bgetfield);
  __ lds1 (RCX_TOS, RCX_TOS,0,R10,0);
__ jmp(post);

  // Shorts & Chars
__ bind(ld_short);
  if( !is_static ) __ mov1i(RAX_fbc, Bytecodes::_fast_sgetfield);
  __ lds2 (RCX_TOS, RCX_TOS,0,R10,0);
__ jmp(post);

__ bind(ld_char);
  if( !is_static ) __ mov1i(RAX_fbc, Bytecodes::_fast_cgetfield);
  __ ldz2 (RCX_TOS, RCX_TOS,0,R10,0);
__ jmp(post);

  // Ints & Floats
  if( is_static ) {
__ bind(ld_float);
__ bind(ld_int);
  } else {
Label L;
__ bind(ld_float);
__ mov1i(RAX_fbc,Bytecodes::_fast_fgetfield);
__ jmp(L);
__ bind(ld_int);
__ mov1i(RAX_fbc,Bytecodes::_fast_igetfield);
    __ bind (L);
  }
  __ lds4 (RCX_TOS, RCX_TOS,0,R10,0);
__ jmp(post);

  // Longs & Doubles
  if( is_static ) {
__ bind(ld_double);
__ bind(ld_long);
  } else {
Label L;
__ bind(ld_double);
    __ mov1i(RAX_fbc, Bytecodes::_fast_dgetfield);
__ jmp(L);
__ bind(ld_long);
    __ mov1i(RAX_fbc, Bytecodes::_fast_lgetfield);
    __ bind (L);
  }
  __ add8i(RDI_JEX, 8);         // push the tag slot
  // Stamp bad slot sentinel value into the empty slot
  __ store_sentinel(RKeepIns::a,RDI_JEX,-8);
  __ ld8  (RCX_TOS, RCX_TOS,0,R10,0);
__ jmp(post);

  // Objects & Arrays
__ bind(ld_object);
  __ ldref_lvb(RInOuts::a, R11, RAX_TMP, RKeepIns::a, RCX_TOS,0,R10,0,true);
  __ verify_oop(R11, MacroAssembler::OopVerify_Load);
  if( !is_static ) __ mov1i(RAX_fbc, Bytecodes::_fast_agetfield);
  __ move8(RCX_TOS,R11);
  //__ jmp  (post); ...fall into ...

__ bind(post);

  // On the fabulous X86 strong memory model, we do not need to fence
  // after reads from volatiles

  // Rewrite for faster future access
  if (RewriteBytecodes && !JvmtiExport::can_post_field_access() && !is_static) {
    __ st1(RSI_BCP, 0, RAX_fbc);
  }

  __ dispatch_next(Bytecodes::length_for(bytecode()));

  // out-of-line resolve cache
  { Register Rtmp0 = __ get_free_reg(bytecode(), RAX_scp, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, RAX_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


void TemplateTable::getfield(int byte_no) {
  getfield_or_static(byte_no, false);
}


void TemplateTable::getstatic(int byte_no) {
  getfield_or_static(byte_no, true);
}


void TemplateTable::putfield_or_static(int byte_no, bool is_static) {
  // Leave TOS in RCX, do not push it.  Only if we call into the VM will
  // we want to push RCX_TOS
  Label do_resolve, resolved, reload_and_retry, retry;
  if (UseSBA) __ bind(retry); // retry bytecode if SBA escape occurs
  Register RAX_scp = RAX; // RAX scaled cache pointer
  resolve_cache_and_index0(RInOuts::a, RAX_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  if (JvmtiExport::can_post_field_modification()) {
    Unimplemented();
  }
  // Load base store address into R11
  // RAX: scaled cache offset
  if (is_static) {
    __ ldref_lvb(RInOuts::a, R11, R10, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f1_offset()), RAX_scp, 0, false);
  } else {
    __ ld8  (R11,RDI_JEX,-8);   // Pop base address off stack; value is in RCX
    __ sub8i(RDI_JEX,8);
  }
  
  // RCX_TOS: value
  // R11: base store ref
  // RAX: scaled cache pointer
  __ ldz4(R10, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset   ()), RAX_scp, 0);
  Register RAX_flags = RAX;
  __ ldz4(RAX_flags, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), RAX_scp, 0);

  // On the fabulous X86 strong memory model, we do not need to fence
  // before writes to volatiles or final fields.

  // store the value according to type
  // RCX_TOS: value to store
  // R11: base store ref
  // R10: offset
  // RAX: flags
  
  // Get type-to-store out of flags.  
  __ move4(R09,RAX_flags);            // save flags for volatile check in a moment
__ shr4i(RAX_flags,ConstantPoolCacheEntry::tosBits);

  Label st_int, st_object, st_long, st_short, st_char, st_double, st_float, st_byte;
Label post,rewrite,done;
  __ cmp1i(RAX_flags,T_OBJECT);
  __ jeq  (st_object);
  __ cmp1i(RAX_flags,T_ARRAY);
  __ jeq  (st_object);
  __ cvta2va(R11);              // Strip metadata in a common place
  __ cmp1i(RAX_flags,T_INT);
  __ jeq  (st_int);
  __ cmp1i(RAX_flags,T_BOOLEAN);
  __ jeq  (st_byte);
  __ cmp1i(RAX_flags,T_BYTE);
  __ jeq  (st_byte);
  __ cmp1i(RAX_flags,T_LONG);
  __ jeq  (st_long);
  __ cmp1i(RAX_flags,T_CHAR);
  __ jeq  (st_char);
  __ cmp1i(RAX_flags,T_SHORT);
  __ jeq  (st_short);
  __ cmp1i(RAX_flags,T_DOUBLE);
  __ jeq  (st_double);
  __ cmp1i(RAX_flags,T_FLOAT);
  __ jeq  (st_float);
  __ os_breakpoint();              // shorter version of should-not-reach-here to keep branches short

  Register RAX_fbc = RAX; // possible fast bytecode substitute
  // Bytes & Booleans
__ bind(st_byte);
  if( !is_static ) { __ null_check(R11); __ mov1i(RAX_fbc, Bytecodes::_fast_bputfield); }
  __ st1  (R11,0, R10,0, RCX_TOS);
__ jmp(post);

  // Chars & Shorts
__ bind(st_short);
  if( is_static ) {
__ bind(st_char);
  } else {
    Label L;
    __ mov1i(RAX_fbc, Bytecodes::_fast_sputfield);
__ jmp(L);
__ bind(st_char);
    __ mov1i(RAX_fbc, Bytecodes::_fast_cputfield);
    __ bind (L);
__ null_check(R11);
  }
  __ st2  (R11,0, R10,0, RCX_TOS);
__ jmp(post);

  // Ints & Floats
__ bind(st_float);
  if( is_static ) {
__ bind(st_int);
  } else {
    Label L;
    __ mov1i(RAX_fbc, Bytecodes::_fast_fputfield);
__ jmp(L);
__ bind(st_int);
    __ mov1i(RAX_fbc, Bytecodes::_fast_iputfield);
    __ bind (L);
__ null_check(R11);
  }
  __ st4  (R11,0, R10,0, RCX_TOS);
__ jmp(post);

  // Longs & Doubles
__ bind(st_double);
  if( is_static ) {
__ bind(st_long);
    __ sub8i(RDI_JEX,8);        // drop dummy value slot
  } else {
    Label L;
    __ mov1i(RAX_fbc, Bytecodes::_fast_dputfield);
__ jmp(L);
__ bind(st_long);
    __ mov1i(RAX_fbc, Bytecodes::_fast_lputfield);
__ bind(L);

    __ ld8  (R11,RDI_JEX,-8);   // dummy slot was popped earlier on
    __ sub8i(RDI_JEX,8);
    __ cvta2va(R11);            // Re-strip but with correct address
__ null_check(R11);//Now check the address
  }
  __ st8  (R11,0, R10,0, RCX_TOS);
  //__ jmp (post); // fall into the volatile check

  // Volatile fencing & Bytecode Rewriting
__ bind(post);
  __ testi (R09,(1<<ConstantPoolCacheEntry::volatileField) | (1<<ConstantPoolCacheEntry::finalField));
  __ jeq   (rewrite); // Volatile OR setting final, need post-fence
  __ mfence(); // Finals need a StoreStore, Volatiles need a StoreLoad
  if(RewriteBytecodes && !JvmtiExport::can_post_field_modification() && !is_static ) {
__ jmp(done);//fast_accessfield doesn't support volatiles
    __ bind(rewrite);
    __ st1 (RSI_BCP, 0, RAX_fbc);    // Rewrite bytecode
  } else {
    __ bind (rewrite);
  }
  
__ jmp(done);//go dispatch next bytecode

  // Arrays & Objects
__ bind(st_object);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Store);

  // Potential stack escape may cause a GC here in +UseSBA mode.  This GC
  // point is not really at any particular BCI - some (but not all) putfield
  // args are popped, but the store has not happened.  Tagged interpreter
  // stack does not depend on BCI.
  // R11: base store REF, may be null
  // R10: offset
  // R09: flags, needed after call for volatile/final check
  // RCX: TOS/ref

  if (UseSBA) { // Preserve values for potential SBA escape
    if (is_static) {
      __ push_tos(); // save value
    } else {
      __ st8 (RDI_JEX, 8, RCX_TOS); // save value, base is already on the stack
      __ add8i(RDI_JEX,16);
    }
  }
  // Test fencing prior to store as ref_store_with_check will crush R09
Label needs_fence;
  __ testi (R09,(1<<ConstantPoolCacheEntry::volatileField) | (1<<ConstantPoolCacheEntry::finalField));
  __ jnz (needs_fence);
  // Unfenced ref store with possible reload
  __ ref_store_with_check(RInOuts::a,R11/*base*/,0/*offset*/,R10/*index*/,0/*scale*/,RCX/*value*/,
                          RAX_TMP/*tmp1*/,R09/*tmp2*/,
                          reload_and_retry);
  if (UseSBA) __ add8i (RDI_JEX,is_static ? -8 : -16); // Pop preserved values from stack
  if( is_static ) {
__ jmp(done);
  } else {
    __ mov1i (RAX_fbc, Bytecodes::_fast_aputfield);
__ jmp(rewrite);
  }
  // Volatile OR setting final, need post-fence
__ bind(needs_fence);
  __ ref_store_with_check(RInOuts::a,R11/*base*/,0/*offset*/,R10/*index*/,0/*scale*/,RCX/*value*/,
                          RAX_TMP/*tmp1*/,R09/*tmp2*/,
                          reload_and_retry);
  __ mfence(); // Finals need a StoreStore, Volatiles need a StoreLoad
  if (UseSBA) __ add8i (RDI_JEX,is_static ? -8 : -16); // Pop preserved values from stack

  __ bind (done);
  // get next TOS, putfield emptied the TOS
__ pop_tos();
  // fall into dispatch logic
  __ dispatch_next(Bytecodes::length_for(bytecode()));

  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RAX_scp, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, RAX_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
  // Out of line reload and retry following SBA escape
  if (UseSBA) {
__ bind(reload_and_retry);
__ pop_tos();
__ jmp(retry);
  }
}


void TemplateTable::putfield(int byte_no) {
  putfield_or_static(byte_no, false);
}


void TemplateTable::putstatic(int byte_no) {
  putfield_or_static(byte_no, true);
}


void TemplateTable::fast_accessfield(TosState state) {
assert(state==tos,"no TosStates supported");
  __ ldz2 (R10, RSI_BCP, 1); // unaligned load, works great on X86
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ shl8i(R10, 2 + log_ptr_size);  // scale by ConstantPoolCacheEntry size
  __ ldz4 (R10, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R10,0);
  __ cvta2va(RCX_TOS);

  switch (bytecode()) {
  case Bytecodes::_fast_bgetfield:
    __ lds1(RCX_TOS, RCX_TOS, 0, R10, 0);
    break;
  case Bytecodes::_fast_cgetfield:
    __ ldz2(RCX_TOS, RCX_TOS, 0, R10, 0);
    break;
  case Bytecodes::_fast_sgetfield:
    __ lds2(RCX_TOS, RCX_TOS, 0, R10, 0);
    break;
  case Bytecodes::_fast_fgetfield:
  case Bytecodes::_fast_igetfield:
    __ lds4(RCX_TOS, RCX_TOS, 0, R10, 0);
    break;
  case Bytecodes::_fast_lgetfield:
  case Bytecodes::_fast_dgetfield:
    __ add8i(RDI_JEX, 8);       // push the tag slot
    // Stamp bad slot sentinel value into the empty slot
    __ store_sentinel(RKeepIns::a,RDI_JEX,-8);
    __ ld8 (RCX_TOS, RCX_TOS, 0, R10, 0);
    break;
  case Bytecodes::_fast_agetfield:
    __ ldref_lvb(RInOuts::a, R09, R11, RKeepIns::a, RCX_TOS, 0, R10, 0, true);
    __ verify_oop(R09, MacroAssembler::OopVerify_Load);
    __ move8(RCX,R09);
    break;
  default:
    ShouldNotReachHere();
  }
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


// short form for: aload0, xgetfield.
// It's really 2 bytecodes jammed together:
//   fast_xaccess, xgetfield, index0, index1
void TemplateTable::fast_xaccess(TosState state) {
assert(state==tos,"no TosStates supported");

__ push_tos();
  __ get_cache_index_at_bcp(RInOuts::a, R10, RKeepIns::a, RSI_BCP, 2);
  __ dispatch_next_0(RInOuts::a,RAX_TMP); // next bytecode to RAX
  __ ld8  (R09, RDX_LOC, 0);    // "aload0"
  __ verify_oop(R09, MacroAssembler::OopVerify_Move);
  __ ldz4(R10, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset   ()), R10,0);
  // R10 holds offset
  __ cvta2va(R09);
  __ dispatch_next_1(RInOuts::a,RAX_TMP);

  switch (bytecode()) {
case Bytecodes::_fast_aaccess_0:
    __ ldref_lvb(RInOuts::a, RCX_TOS, R11, RKeepIns::a, R09,0,R10,0,true);
    __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Load);
    break;
case Bytecodes::_fast_iaccess_0:
case Bytecodes::_fast_faccess_0:
    __ lds4(RCX_TOS,R09,0,R10,0);
    break;
  default:
    ShouldNotReachHere();
  }
  
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


void TemplateTable::fast_storefield(TosState state) {
assert(state==tos,"no TosStates supported");
  Label reload_and_retry, retry; // Labels necessary for SBA retries
  int baseoff = (bytecode() == Bytecodes::_fast_lputfield || bytecode() == Bytecodes::_fast_dputfield) ? -16 : -8;

  if( bytecode() != Bytecodes::_fast_aputfield) {
    // Get offset into R09
    __ get_cache_index_at_bcp(RInOuts::a, R09, RKeepIns::a, RSI_BCP, 1);
    __ ldz4 (R09, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09, 0);
    __ dispatch_next_0(RInOuts::a,RAX_TMP); // next bytecode to RAX
    __ ld8  (R10, RDI_JEX,baseoff);         // Base ref
    __ cvta2va(R10);                        // Strip metadata
    switch (bytecode()) {
    case Bytecodes::_fast_bputfield:  __ st1  (R10, 0, R09, 0, RCX_TOS);   break;
case Bytecodes::_fast_sputfield://fall-through
    case Bytecodes::_fast_cputfield:  __ st2  (R10, 0, R09, 0, RCX_TOS);   break;
    case Bytecodes::_fast_iputfield:  // fall-through
    case Bytecodes::_fast_fputfield:  __ st4  (R10, 0, R09, 0, RCX_TOS);   break;
    case Bytecodes::_fast_lputfield:  // fall-through
    case Bytecodes::_fast_dputfield:  __ st8  (R10, 0, R09, 0, RCX_TOS);   break;
    default:        ShouldNotReachHere();
    }
    __ dispatch_next_1(RInOuts::a,RAX_TMP); // next bytecode to RAX

  } else {
    // fast_aputfield
    // Potential stack escape may cause a GC here in +UseSBA mode.  This GC
    // point is not really at any particular BCI - in SBA mode args to aputfield
    // are pushed on the stack in case they need reloading.  Tagged interpreter
    // stack does not depend on BCI.
    __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Store);
    if (UseSBA) {
      __ push_tos();   // Save value in case SBA escape requires reload
      baseoff -= 8;    // Base value is now further away
__ bind(retry);//Come here when we need to retry store following an SBA escape
    }
    // Get offset into R09
    __ get_cache_index_at_bcp(RInOuts::a, R09, RKeepIns::a, RSI_BCP, 1);
    __ ldz4 (R09, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09, 0);
    __ ld8  (R10, RDI_JEX,baseoff);   // Base ref
    __ ref_store_with_check(RInOuts::a,R10/*base*/,0/*offset*/,R09/*index*/,0/*scale*/,RCX/*value*/,
                            RAX_TMP/*tmp1*/,R11/*tmp2*/,
                            reload_and_retry);
    __ dispatch_next_0(RInOuts::a,RAX_TMP);       // next bytecode to RAX
    __ dispatch_next_1(RInOuts::a,RAX_TMP);       // next bytecode to RAX
  }

  __ ld8  (RCX_TOS, RDI_JEX, baseoff-8);
  __ add8i(RDI_JEX, baseoff-8);  // get next TOS, putfield emptied the TOS
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

  if (UseSBA && bytecode() == Bytecodes::_fast_aputfield) {
__ bind(reload_and_retry);//SBA escape occured - out of line code
    __ ld8 (RCX_TOS, RDI_JEX,-8); // Reload crushed TOS
__ jmp(retry);
  }
}

//--- invokevirtual ----------------------------------------------------------
void TemplateTable::invokevirtual(int byte_no) {
  Label null_rcv, do_resolve, resolved, call, lookup_index;
__ push_tos();
  // RAX - free, RBX - callee-save, RCX - free, RDX_LOC - loc
  // RSI - bcp , RDI - jex stk    , RSP - used, RBP - callee-save
  // R08 - CPC , R09 - free       , R10 - free, R11 - free
  // Pack interpreter registers.  Frees lots of them for general use.
  // Also must be packed for the resolve call.
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    Register Rtmp1 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0);
    __ pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  // RAX - free, RBX - callee-save, RCX - free, RDX - free
  // RSI - bcp,  RDI - jex stk    , RSP - used, RBP - callee-save
  // R08 - CPC , R09 - free       , R10 - free, R11 - free

  // RAX holds flags
  // RCX is a temp, then holds the receiver, then holds the method oop
  // RDX holds the start of the outgoing args
  // R09 holds offset into R08_CPC (scaled cache pointer)
  Register R09_scp   = R09;
  Register RAX_flags = RAX;
  Register RCX_rcvr  = RCX; Register RCX_tmp = RCX; Register RCX_moop = RCX;

  resolve_cache_and_index0(RInOuts::a, R09_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  // load the flags from [R08_CPC + R09*1 + base_offset+flags_offset]
  __ ldz4 (RCX_tmp, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), R09_scp, 0);
  __ move4(RAX_flags, RCX_tmp);

  // receiver null check & return type
  __ and4i(RCX_tmp,0xff);                         // mask to the parm count
  __ neg8 (RCX_tmp);
  __ lea  (RDX_LOC, RDI_JEX, 0, RCX_tmp, 3 );     // put start of args in RDX_LOC
  __ st4  (RSP, offset_of(IFrame,_stk), RDX_LOC); // pop args off saved stack-depth

  __ ld8  (RCX_rcvr, RDX_LOC, 0);    // Load receiver
  __ null_chk(RCX_rcvr, null_rcv);   // Receiver null check

  // check for vfinal method call
  __ btx4i(RAX_flags,ConstantPoolCacheEntry::vfinalMethod);
  __ jnb  (lookup_index);
  // vfinal method call
  if (RewriteBytecodes && !JvmtiExport::can_post_interpreter_events()) {
    __ st1i(RSI_BCP, 0, Bytecodes::_fast_invokevfinal);
  }
  // load the target methodRef
  { Register Rtmp1 = __ get_free_reg(bytecode(), RAX_flags, RCX_moop, RDX_LOC, R08_CPC, R09_scp);
    __ ldref_lvb(RInOuts::a, RCX_moop, Rtmp1, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09_scp, 0, false);
  }
__ jmp(call);//RCX_rcvr has the right methodOop, branch to the call

  // lookup destination method in the vtable
  // RCX: receiver
  // RDX: LOC: start of outgoing args
  // R08: CPC
  // R09: scaled cache pointer
  { __ bind(lookup_index);
    // f2 contains an index, which we now load:
    Register Rtmp        = __ get_free_reg(bytecode(), RCX_moop, RDX_LOC, R08_CPC, R09_scp);
    Register Rvtable_idx = __ get_free_reg(bytecode(), RCX_moop, RDX_LOC, R08_CPC, R09_scp, Rtmp);
    Register Rrcvr_klass = __ get_free_reg(bytecode(), RCX_moop, RDX_LOC, R08_CPC, R09_scp, Rtmp, Rvtable_idx);
    __ ldz4(Rvtable_idx, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09_scp, 0);
    __ ref2klass (RInOuts::a, Rrcvr_klass, Rtmp, RCX_rcvr);  // KlassRef into Rcvr_klass, from objectRef in RCX
    assert0(vtableEntry::size() * wordSize == ref_size);
    // Load target method from: RSI+RDI*8+vtable_start+method_offset
    __ cvta2va(Rrcvr_klass);
    __ ldref_lvb(RInOuts::a, RCX_moop, Rtmp, RKeepIns::a, Rrcvr_klass, instanceKlass::vtable_start_offset() * wordSize + vtableEntry::method_offset_in_bytes(), Rvtable_idx, log_ref_size, false);
  }

  // CALLING CONVENTION:
  // This is "pre-call" code ("prolog" code is in interpreter_x86.cpp).
  // RCX: methodRef
  // RDX_LOC: start of locals for the *next* call
__ bind(call);
  __ verify_not_null_oop(RCX_moop, MacroAssembler::OopVerify_VtableLoad);
  __ cvta2va(RAX_TMP, RCX_moop);         // strip to C ptr to get the target address
  __ ldz4 (RAX_TMP, RAX_TMP, in_bytes(methodOopDesc::  from_interpreted_offset()) );
if(JvmtiExport::can_post_interpreter_events()){
    // if we are single stepping, we must jump to the interpreted entry regardless.
    Unimplemented();
  }
  Label do_call(_masm);
__ call(RAX_TMP);//Normal X86 call, so we get a proper call/ret hardware prefetch
  // Now jump to common return-from-call recovery code
  __ jmp  (Interpreter::return_entry(tos, 3));

  // Out of line - branch here with rcx null, restore interpreter registers & throw NPE
__ bind(null_rcv);//Only come here to die
  __ unpack_interpreter_regs(RInOuts::a, RAX_TMP, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ getthr(RAX_TMP);
  __ st8i (RAX_TMP,in_bytes(JavaThread::epc_offset()), do_call);
  __ jmp  (StubRoutines::x86::handler_for_null_ptr_exception_entry());
  
  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_scp);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, R09_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


//--- fast_invokevfinal ------------------------------------------------------
void TemplateTable::fast_invokevfinal(int byte_no) {
  Label null_rcv, do_resolve, resolved;
__ push_tos();
  // Pack interpreter registers.  Frees lots of them for general use.
  // Also must be packed for the resolve call.
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    Register Rtmp1 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0);
    __ pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  // R09 holds offset into R08_CPC (scaled cache pointer)
  Register R09_scp = R09;
  resolve_cache_and_index0(RInOuts::a, R09_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  // load the target methodRef into RCX
  Register RCX_moop = RCX;
  __ ldref_lvb(RInOuts::a, RCX_moop, RAX_TMP, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09_scp, 0, false);
  __ verify_not_null_oop(RCX_moop, MacroAssembler::OopVerify_VtableLoad);

  // load the flags from [R08_CPC + RDX*1 + base_offset+flags_offset]
  __ ldz1 (RAX, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), R09_scp, 0);

  // receiver null check & return type
  // RCX: methodRef
  // RAX: parameter size
  __ neg8 (RAX);
  __ lea  (RDX_LOC, RDI_JEX, 0, RAX, 3 ); // put start of args in RDX_LOC
  __ st4  (RSP, offset_of(IFrame,_stk), RDX_LOC); // pop args off saved stack-depth

  __ cmp8i(RDX_LOC,0,0);
  __ jeq  (null_rcv);           // Receiver null check

  // CALLING CONVENTION:
  // This is "pre-call" code ("prolog" code is in interpreter_x86.cpp).
  // RCX: methodRef
  // RDX_LOC: start of locals for the *next* call
  __ cvta2va(RAX, RCX_moop);   // strip to C ptr to get the target address
  __ ldz4 (RAX_TMP, RAX_TMP, in_bytes(methodOopDesc::  from_interpreted_offset()) );

if(JvmtiExport::can_post_interpreter_events()){
    // if we are single stepping, we must jump to the interpreted entry regardless.
    Unimplemented();
  } 
  Label do_call(_masm);
__ call(RAX_TMP);//Normal X86 call, so we get a proper call/ret hardware prefetch
  // Now jump to common return-from-call recovery code
  __ jmp  (Interpreter::return_entry(tos, 3));

  // Out of line - branch here with rcx null, restore interpreter registers & throw NPE
__ bind(null_rcv);//Only come here to die
  __ unpack_interpreter_regs(RInOuts::a, RAX_TMP, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ getthr(RAX_TMP);
  __ st8i(RAX_TMP,in_bytes(JavaThread::epc_offset()), do_call);
  __ jmp(StubRoutines::x86::handler_for_null_ptr_exception_entry());
  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_scp);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, R09_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


//--- invokespecial ----------------------------------------------------------
void TemplateTable::invokespecial(int byte_no) {
  Label null_rcv, do_resolve, resolved;
__ push_tos();
  // Pack interpreter registers.  Frees lots of them for general use.
  // Also must be packed for the resolve call.
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    Register Rtmp1 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0);
    __ pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  // R09 holds offset into R08_CPC (scaled cache pointer)
  Register R09_scp = R09;
  resolve_cache_and_index0(RInOuts::a, R09_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  // load the target methodRef into RCX
  Register RCX_moop = RCX;
  __ ldref_lvb(RInOuts::a, RCX_moop, RAX_TMP, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f1_offset()), R09_scp, 0, false);
  __ verify_not_null_oop(RCX_moop, MacroAssembler::OopVerify_VtableLoad);

  // load the flags from [R08_CPC + RDX*1 + base_offset+flags_offset]
  __ ldz1 (RAX, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), R09_scp, 0);

  // receiver null check & return type
  // RCX: methodRef
  // RAX: parameter size
  __ neg8 (RAX);
  __ lea  (RDX_LOC, RDI_JEX, 0, RAX, 3 ); // put start of args in RDX_LOC
  __ st4  (RSP, offset_of(IFrame,_stk), RDX_LOC); // pop args off saved stack-depth
  __ cmp8i(RDX_LOC,0,0);
  __ jeq  (null_rcv);            // Receiver null check

  // CALLING CONVENTION:
  // This is "pre-call" code ("prolog" code is in interpreter_x86.cpp).
  // RCX: methodRef
  // RDX_LOC: start of locals for the *next* call
  __ cvta2va(RAX_TMP, RCX_moop); // strip to C ptr to get the target address
  __ ldz4 (RAX_TMP, RAX_TMP, in_bytes(methodOopDesc::  from_interpreted_offset()) );

if(JvmtiExport::can_post_interpreter_events()){
    // if we are single stepping, we must jump to the interpreted entry regardless.
    Unimplemented();
  } 
  Label do_call(_masm);
__ call(RAX_TMP);//Normal X86 call, so we get a proper call/ret hardware prefetch
  // Now jump to common return-from-call recovery code
  __ jmp  (Interpreter::return_entry(tos, 3));

  // Out of line - branch here with rcx null, restore interpreter registers & throw NPE
__ bind(null_rcv);//Only come here to die
  __ unpack_interpreter_regs(RInOuts::a, RAX_TMP, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ getthr(RAX_TMP);
  __ st8i (RAX_TMP,in_bytes(JavaThread::epc_offset()), do_call);
  __ jmp  (StubRoutines::x86::handler_for_null_ptr_exception_entry());

  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_scp);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, R09_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


//--- invokestatic -----------------------------------------------------------
void TemplateTable::invokestatic(int byte_no) {
  Label do_resolve, resolved;
__ push_tos();
  // Pack interpreter registers.  Frees lots of them for general use.
  // Also must be packed for the resolve call.
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    Register Rtmp1 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0);
    __ pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  // R09 holds offset into R08_CPC (scaled cache pointer)
  Register R09_scp = R09;
  resolve_cache_and_index0(RInOuts::a, R09_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  // load the target methodRef into RCX
  Register RCX_moop = RCX;
  __ ldref_lvb(RInOuts::a, RCX_moop, RAX_TMP, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f1_offset()), R09_scp, 0, false);
  __ verify_not_null_oop(RCX_moop, MacroAssembler::OopVerify_VtableLoad);

  // load the flags     from [R08_CPC + RDX*1 + base_offset+flags_offset]
  __ ldz1 (RAX, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), R09_scp, 0);

  // receiver null check & return type
  // RCX: methodRef
  // RAX: parameter size
  __ neg8 (RAX);
  __ lea  (RDX_LOC, RDI_JEX, 0, RAX, 3 ); // put start of args in RDX_LOC
  __ st4  (RSP, offset_of(IFrame,_stk), RDX_LOC); // pop args off saved stack-depth

  // CALLING CONVENTION:
  // This is "pre-call" code ("prolog" code is in interpreter_x86.cpp).
  // RCX: methodRef
  // RDX_LOC: start of locals for the *next* call
  __ cvta2va(RAX_TMP, RCX_moop);              // strip to C ptr to get the target address
  __ ldz4 (RAX_TMP, RAX_TMP, in_bytes(methodOopDesc::  from_interpreted_offset()) );

if(JvmtiExport::can_post_interpreter_events()){
    // if we are single stepping, we must jump to the interpreted entry regardless.
    Unimplemented();
  } 
__ call(RAX_TMP);//Normal X86 call, so we get a proper call/ret hardware prefetch
  // Now jump to common return-from-call recovery code
  __ jmp  (Interpreter::return_entry(tos, 3));

  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_scp);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, R09_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


//--- invokeinterface --------------------------------------------------------
void TemplateTable::invokeinterface(int byte_no) {
  Label null_rcv, do_resolve, resolved, call, lookup_index, not_method;
__ push_tos();
  // RAX - free, RBX - callee-save, RCX - free, RDX_LOC - loc
  // RSI - bci , RDI - jex stk    , RSP - used, RBP - callee-save
  // R08 - CPC , R09 - free       , R10 - free, R11 - free
  // Pack interpreter registers.  Frees lots of them for general use.
  // Also must be packed for the resolve call.
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    Register Rtmp1 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0);
    __ pack_interpreter_regs(RInOuts::a, Rtmp0, Rtmp1, RDX_LOC, RKeepIns::a, RDI_JEX, RSI_BCP);
  }
  // RAX - free, RBX - callee-save, RCX - free, RDX - free
  // RSI - bcp,  RDI - jex stk    , RSP - used, RBP - callee-save
  // R08 - CPC , R09 - free       , R10 - free, R11 - free
  // R09 holds offset into R08_CPC (scaled cache pointer)
  Register R09_scp = R09;
  resolve_cache_and_index0(RInOuts::a, R09_scp, RKeepIns::a, R08_CPC, RSI_BCP, byte_no, do_resolve, resolved);

  // load the flags from [R08_CPC + R09*1 + base_offset+flags_offset]
  Register RAX_flags = RAX; Register RCX_flags = RCX;
  __ ldz4 (RAX_flags, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::flags_offset()), R09_scp, 0); 
  __ move4(RCX_flags,RAX_flags);            // Save flags for a bit

  // receiver null check & return type
  // RAX: flags
  // RSI: BCP
  // RDI: JEX
  // R08: CPC
  // R09: scaled cache pointer
  __ and4i(RAX_flags,0xff);                // Mask to parm count
  __ neg8 (RAX);
  __ lea  (RDX_LOC, RDI_JEX, 0, RAX, 3 ); // put start of args in RDX_LOC
  __ st4  (RSP, offset_of(IFrame,_stk), RDX_LOC); // pop args off saved stack-depth

  Register RAX_rcvr = RAX;
  __ ld8  (RAX_rcvr, RDX_LOC, 0);    // Load receiver
  __ verify_oop(RAX_rcvr, MacroAssembler::OopVerify_OutgoingArgument);
  __ null_chk(RAX_rcvr, null_rcv);   // Receiver null check

  // Special case of invokeinterface called for virtual method of java.lang.Object.  
  // See cpCacheOop.cpp for details.  This code isn't produced by javac, but
  // could be produced by another compliant java compiler.
  __ btx4i(RCX_flags,ConstantPoolCacheEntry::methodInterface);
  __ jnb  (not_method);

  // Not a real interface call here.
  // check for vfinal method call
  __ btx4i(RCX_flags,ConstantPoolCacheEntry::vfinalMethod);
  __ jnb  (lookup_index);
  // load the target methodRef
  __ ldref_lvb(RInOuts::a, RCX, R11, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09, 0, false);
  __ verify_not_null_oop(RCX, MacroAssembler::OopVerify_VtableLoad);
  if (RewriteBytecodes && !JvmtiExport::can_post_interpreter_events()) {
    __ st1i(RSI_BCP, 0, Bytecodes::_fast_invokevfinal);
  }
__ jmp(call);//RCX has the right methodOop, branch to the call

  // lookup destination method in the vtable
  // RAX: receiver
  // RDX: LOC: start of outgoing args
  // R08: CPC
  // R09: scaled cache pointer
__ bind(lookup_index);
  // f2 contains an index, which we now load:
  __ ldz4(RDI, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09, 0); 
  __ verify_oop(RAX, MacroAssembler::OopVerify_OutgoingArgument);
  __ ref2klass (RInOuts::a, R11, RCX/*temp*/, RAX);  // KlassRef into R11, from objectRef in RAX
  assert0(vtableEntry::size() * wordSize == ref_size);
  // R11: Klass of receiver
  // RDI: vtable index
  // Load target method from: RSI+RDI*8+vtable_start+method_offset
  __ cvta2va(R11);
  __ ldref_lvb(RInOuts::a, RCX, RAX, RKeepIns::a, R11, instanceKlass::vtable_start_offset() * wordSize + vtableEntry::method_offset_in_bytes(), RDI, log_ref_size, false);
__ jmp(call);


  // A Real Interface Call
  // RAX: receiver
  // RDX: LOC: start of outgoing args
  // RSI: BCP
  // R08: CPC
  // R09: scaled cache pointer
  // R10: flags
__ bind(not_method);
  __ ldref_lvb(RInOuts::a, R11, RCX, RKeepIns::a, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f1_offset()), R09, 0, false);
  // R11: interface klass
  // RDI: index
  // RAX: receiver
  __ ldz4 (RDI, R08_CPC, in_bytes(constantPoolCacheOopDesc::base_offset() + ConstantPoolCacheEntry::f2_offset()), R09, 0); 
  __ ref2klass(RInOuts::a, R09, RCX/*temp*/, RAX); // KlassRef into R09, from objectRef in RAX
  __ move8(RCX, R09);
  __ verify_oop(RCX, MacroAssembler::OopVerify_VtableLoad);
  __ cvta2va  (RCX);

  // compute start of first itableOffsetEntry (which is at end of vtable)
  // RDI: index
  // RCX: receiver klassOop
  // RSI: BCP
  // R11: interface klass
  __ ldz4 (RAX, RCX, instanceKlass::vtable_length_offset() * wordSize);
  __ lea  (RAX, RCX, instanceKlass::vtable_start_offset () * wordSize, RAX, 3);

  // search the itableOffsetEntries for the interfaceKlass
  // RAX: current itableOffsetEntry
  // RDI: index
  // RCX: receiver klassOop
  // RSI: BCP
  // R11: interface klass
Label entry,search;
  __ jmp  (entry);
  __ bind (search);
__ add8i(RAX,itableOffsetEntry::size()*wordSize);
  __ bind (entry);

  __ ldref_lvb (RInOuts::a, R09, R10, RKeepIns::a, RAX, itableOffsetEntry::interface_offset_in_bytes(),true);
  // Check that entry is non-null.  Null entries are probably a bytecode
  // problem or a class library versioning problem.  If the interface isn't
  // implemented by the reciever class, the VM should throw IncompatibleClassChangeError.
  // linkResolver checks this too but that's only if the entry isn't already
  // resolved, so we need to check again.
  // RAX: current itableOffsetEntry
  // RDI: index
  // RCX: receiver klassOop
  // RSI: BCP
  // R09: current entry value (klass)
  // R11: interface klass
  { Label L;
    __ test8(R09,R09);
__ jne(L);
    Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_IncompatibleClassChangeError), noreg,
	       RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R11, noreg, noreg);
    __ bind (L);
  }
  __ cmp8 (R11,R09);
__ jne(search);

  // found entry, get the methodOop from the vtable based on the offset
  // RAX: current itableOffsetEntry
  // RDI: index
  // RCX: receiver klassOop
  // RSI: BCP
  __ lds4 (RAX, RAX, itableOffsetEntry::offset_offset_in_bytes());
  __ lea  (RAX, RAX, 0, RDI, 3);
  __ ldref_lvb(RInOuts::a, RDI, R10, RKeepIns::a, RCX, 0, RAX, 0, true);
  __ move8(RCX,RDI);

  // Check for abstract method error.
  // RCX: methodRef
  { Label L;
    __ test8(RCX,RCX);
__ jne(L);
    Register Rtmp0 = __ get_free_reg(bytecode(), RCX, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_AbstractMethodError), noreg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX, noreg, noreg);
    __ bind (L);
  }

  // CALLING CONVENTION:
  // This is "pre-call" code ("prolog" code is in interpreter_x86.cpp).
  // RCX: methodRef
  // RDX_LOC: start of locals for the *next* call
__ bind(call);
  __ move8(RAX,RCX);
  __ cvta2va(RAX);              // strip to C ptr to get the target address
  __ ldz4 (RAX, RAX, in_bytes(methodOopDesc::  from_interpreted_offset()) );

if(JvmtiExport::can_post_interpreter_events()){
    // if we are single stepping, we must jump to the interpreted entry regardless.
    Unimplemented();
  } 
  Label do_call(_masm);
__ call(RAX);//Normal X86 call, so we get a proper call/ret hardware prefetch
  // Now jump to common return-from-call recovery code
  __ jmp  (Interpreter::return_entry(tos, 5));

  // Out of line - branch here with RAX_rcvr null, restore interpreter registers & throw NPE
__ bind(null_rcv);//Only come here to die
  __ unpack_interpreter_regs(RInOuts::a, RAX_TMP, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ getthr(RAX_TMP);
  __ st8i (RAX_TMP,in_bytes(JavaThread::epc_offset()), do_call);
  __ jmp  (StubRoutines::x86::handler_for_null_ptr_exception_entry());

  // Out of line resolve code
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_scp);
    resolve_cache_and_index1(RInOuts::a, Rtmp0, R09_scp,
                             RKeepIns::a, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC,
                             byte_no, do_resolve, resolved);
  }
}


//----------------------------------------------------------------------------------------------------
// Allocation

void TemplateTable::new_impl( bool allow_sba ) {
  Label slow_case, slow_path, done, done2;

  Register RCX_cpidx = RCX; // constant pool index
  Register R09_cpref = R09; // reference to constant pool
  Register R08_moop  = R08; // method oop - R08_CPC restore at end
  // Get constant pool, index of klassRef
  __ push_tos();                // Both clear out TOS and free up RCX
  __ getmoop(R08_moop);         // Reload method OOP
  __ ldref_lvb(RInOuts::a, R09_cpref, RAX, RKeepIns::a, R08_moop, in_bytes(methodOopDesc::constants_offset()),false);
  __ ldz2  (RCX_cpidx, RSI_BCP, 1);   // unaligned load, works great on X86
  __ bswap2(RCX_cpidx);

  // Get tag array and tag
  Register R11_cpoop = R11; // oop reference to constant pool
  Register R10_tags  = R10; // oop reference to tags array
  __ cvta2va(R11_cpoop, R09_cpref);
  __ ldref_lvb(RInOuts::a, R10_tags, RAX_TMP, RKeepIns::a, R11_cpoop, constantPoolOopDesc::tags_offset_in_bytes(),false);
  __ cvta2va(R10_tags);

  // A little tricky to parse this next instruction, so here's the breakdown:
  // cmp1i [R10 + RCX*1 + typeArrayOopDesc::header_size_T_BYTE] vs JVM_CONSTANT_Class
  // ie if (tags[cpidx] == JVM_CONSTANT_Class) goto resolved
  __ cmp1i(R10_tags,typeArrayOopDesc::header_size_T_BYTE(),RCX_cpidx,0, JVM_CONSTANT_Class);
__ jne(slow_case);
  
  // Constant pool entry was resolved, check we have given the method Oop an oop
  // table index (allocated lazily). If not go to slow case.
  __ cmp4i(R08_moop,in_bytes(methodOopDesc::oid_offset()),0);
__ jeq(slow_case);

  // Load the klassref - klass = cpool[cpidx]
  Register R08_klass = R08;
  __ ldref_lvb(RInOuts::a, R08_klass, RAX_TMP, RKeepIns::a, R11_cpoop, constantPoolOopDesc::header_size() * wordSize, RCX_cpidx, 3, false);
  __ cvta2va(R08_klass);

  // make sure klass is fully initialized
__ cmp4i(R08_klass,instanceKlass::init_state_offset_in_bytes()+sizeof(oopDesc),instanceKlass::fully_initialized);
__ jne(slow_case);

//make sure klass does not have has_finalizer, or is abstract, or interface or java/lang/Class
  __ btx4i(R08_klass, Klass::layout_helper_offset_in_bytes() + sizeof(oopDesc), Klass::_lh_instance_slow_path_bit-1); // move flag into Carry
  __ jbl  (slow_case);          // Go slow if carry set (if flag==1, if cannot fastpath_allocate)

  __ move8(R11,RSI);            // Save RSI_BCP in R11
  __ move8(R10,RDX);            // Save RDX_LOC in R10
  
  // get instance_size from instanceKlass (already aligned if ALIGN_ALL_OBJECTS is set)
  Register RDX_size = RDX;
  __ lds4 (RDX_size, R08_klass, Klass::layout_helper_offset_in_bytes() + sizeof(oopDesc));
  
  // Get KID before possible GC blows klass in R08 as well
  Register RSI_kid = RSI;
  __ ldz4 (RSI_kid, R08_klass, klassOopDesc::klass_part_offset_in_bytes() + Klass::klassId_offset_in_bytes());

  if( allow_sba ) {
    // -----------------------
    // Attempt SBA-style allocation inline
    // RSI_kid: KID
    // RDX_size: instance size (in bytes)
    // R10: Saved RDX_LOC
    // R11: Saved RSI_BCP
    // Do not bother with a too-large check here; such objects will fail the
    // basic SBA-area-full checks and the VM call will Do The Right Thing.
    __ call(StubRoutines::new_sba() ); // plain call, not VM call, not GC point!
    // RCX holds the length of zero
    // RAX: the new object, pre-zero'd with markWord set and SBA preheader word allocated
    //       or Z/NULL if SBA can not do it (needs GC or finalizer, etc)
    // RSI_kid: KID
    // RDX_size: instance size (in bytes)
    // R09: the new object, stripped of metadata
    // R10: Saved RDX_LOC
    // R11: Saved RSI_BCP
    __ jeq  (slow_path); // force stack GC, etc on slow-path

    Register RCX_moid = RCX;
    Register R09_oop = R09;
    // Set interpreter pre-header bits, stomping over the compiler pre-header bits
    // FID, 0, MTH, BCI
    __ getmoop(R08_moop);
    __ ldz4 (RCX_moid,R08_moop,in_bytes(methodOopDesc::oid_offset()));
    __ st1i (R09_oop, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_ispc),   0);
    __ st4  (R09_oop, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_moid), RCX_moid);

    Register RCX_cmthd = RCX; // constMethod
    { Register Rtmp = __ get_free_reg(bytecode(), R09_oop, RCX_TOS, R10, R11, RDI_JEX, R08_moop, RCX_cmthd);
      __ ldref_lvb(RInOuts::a, RCX_cmthd, Rtmp, RKeepIns::a, R08_moop, in_bytes(methodOopDesc::const_offset()),false);
    }
    __ cvta2va(RCX_cmthd);       // convert constMethod into a real pointer
    __ sub8 (RCX_cmthd,R11);     // BCI in R11, plus sizeof(constMethod)
    __ neg4 (RCX_cmthd);
    __ add4i(RCX_cmthd,-in_bytes(constMethodOopDesc::codes_offset()));
    __ st2  (R09_oop, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_bci), RCX_cmthd);
    __ jmp  (done);
  }

  // -----------------------
  // Attempt TLAB allocation
  // RSI_kid: KID
  // RDX_size: instance size (in bytes)
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ call (StubRoutines::new_fast());
  // RCX holds the length of zero
  __ jne  (done); // try to allocate in the heap if stack ran out of memory

__ bind(slow_path);
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ xchg8(RSI_BCP,R11);      // Restore RSI_BCP from R11; KID to R11
  __ xchg8(RDX_LOC,R10);      // Restore RDX_LOC from R10; SIZ to R10
  // R11: KID
  // R10: instance size (in bytes)
  // RCX: array length+ekid (set to zero above)
  __ call_runtime_new(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11, R10, RCX, allow_sba);

#if defined(ASSERT)
Label ok;
  __ test8(RCX_TOS,RCX_TOS);
__ jne(ok);
__ should_not_reach_here("SharedRuntime::_new returns null, should throw instead");
__ bind(ok);
#endif // ASSERT
__ jmp(done2);

  // continue
  __ bind (done);
  // RAX: oop to return
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ move8(RSI_BCP,R11);        // Restore RSI_BCP from R11
  __ move8(RDX_LOC,R10);        // Restore RDX_LOC from R10
__ bind(done2);
  __ move8(RCX_TOS,RAX);        // Return ref
  __ dispatch_next_0(RInOuts::a, RAX_TMP);
  __ ld8  (R08_CPC,RSP,offset_of(IFrame,_cpc));
  __ verify_oop(R08_CPC, MacroAssembler::OopVerify_Sanity);
  __ dispatch_next_1(RInOuts::a, RAX_TMP);
  __ cvta2va(R08_CPC);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_NewOop);
  __ dispatch_next_2(RInOuts::a, RAX_TMP);

  // -----------------------
  // SLOW CASE
  __ bind (slow_case);
  { Register RAX_result_arg = RAX;
    Register Rtmp0 = __ get_free_reg(bytecode(), RAX_result_arg, RCX_cpidx, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09);
    __ mov8i(RAX_result_arg,0);       // zero length for objects
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::_new), RAX_result_arg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R09_cpref, RCX_cpidx, RAX_result_arg);
  }
__ jmp(done2);
}


void TemplateTable::newarray_impl( bool allow_sba ) {
  Label alloc, slow_path, done, done2;

  // Move the klassID, which is the same as the element type, and the length into
  // position for the call to StubRoutines::new_fast_array.
  __ ldz1 (RAX, RSI_BCP, 1);    // Load BasicType, which is also the KID
  __ move8(R11,RSI_BCP);        // Save RSI_BCP in R11
  __ move8(R10,RDX_LOC);        // Save RDX_LOC in R10

  // Determine the size in bytes of the array object.  Adding the arrayOop
  // header size and any necessary tail padding for heap word size alignment
  // (64-bits).
  // RCX: length/ekid=0
  // RAX: klassID
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP

  Label is_8_bits, not_8_bits, is_16_bits, not_16_bits, is_32_bits, not_32_bits, is_64_bits;
  __ cmp1i(RAX, T_BYTE);
  __ jeq  (is_8_bits);
  __ cmp1i(RAX, T_BOOLEAN);
  __ jne  (not_8_bits);
__ bind(is_8_bits);
  __ lea  (RDX,   RCX, arrayOopDesc::header_size_in_bytes(T_BYTE ) + 7);
  __ and8i(RDX, -8);
__ jmp(alloc);

__ bind(not_8_bits);
  __ cmp1i(RAX, T_CHAR);
  __ jeq  (is_16_bits);
  __ cmp1i(RAX, T_SHORT);
  __ jne  (not_16_bits);
__ bind(is_16_bits);
  __ lea  (RDX, noreg, arrayOopDesc::header_size_in_bytes(T_SHORT) + 7, RCX, 1);
  __ and8i(RDX, -8);
__ jmp(alloc);

__ bind(not_16_bits);
  __ cmp1i(RAX, T_INT);
  __ jeq  (is_32_bits);
  __ cmp1i(RAX, T_FLOAT);
  __ jne  (not_32_bits);
__ bind(is_32_bits);
  __ lea  (RDX, noreg, arrayOopDesc::header_size_in_bytes(T_INT  ) + 7, RCX, 2);
  __ and8i(RDX, -8);
__ jmp(alloc);

__ bind(not_32_bits);
  __ lea  (RDX, noreg, arrayOopDesc::header_size_in_bytes(T_LONG )    , RCX, 3);

__ bind(alloc);
  __ move4(RSI,RAX);
  // RSI: KID
  // RCX: length/ekid=0
  // RDX: size in bytes
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  
  if( allow_sba ) {
    // -----------------------
    // Attempt SBA-style allocation inline
    __ getmoop(RAX); // Must have (lazy) OID on methodOop to sba allocate
    __ cmp4i(RAX,in_bytes(methodOopDesc::oid_offset()),0);
    __ jeq  (slow_path); // Force slow-path to get an OID

    // RSI: KID
    // RCX: length/ekid=0
    // RDX: size in bytes
    // R10: Saved RDX_LOC
    // R11: Saved RSI_BCP
    __ call (StubRoutines::x86::new_sba_array() );
    // RCX: holds length/ekid=0
    // RAX: the new object, pre-zero'd with markWord set and SBA preheader word allocated
    //       or Z/NULL if SBA can not do it (needs GC, etc)
    // R09: the new object, stripped of metadata
    // R10: Saved RDX_LOC
    // R11: Saved RSI_BCP
    __ jeq  (slow_path); // force stack GC, etc on slow-path

    // Set interpreter pre-header bits, stomping over the compiler pre-header bits
    // FID, 0, MTH, BCI
    Register RCX_moid = RCX;
    Register RDX_moop = RDX;
    Register R09_oop = R09;
    __ getmoop(RDX_moop);
    __ ldz4 (RCX_moid,RDX_moop,in_bytes(methodOopDesc::oid_offset()));
    __ st1i (RAX, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_ispc),   0);
    __ st4  (RAX, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_moid), RCX_moid);

    Register RCX_cmthd = RCX; // constMethod
    { Register Rtmp = __ get_free_reg(bytecode(), R09_oop, RCX_TOS, R10, R11, RDI_JEX, R08_CPC, RCX_cmthd);
      __ ldref_lvb(RInOuts::a, RCX_cmthd, Rtmp, RKeepIns::a, RDX_moop, in_bytes(methodOopDesc::const_offset()),false);
    }
    __ cvta2va(RCX_cmthd);       // convert constMethod into a real pointer
    __ sub8 (RCX_cmthd,R11);     // BCI in R11, plus sizeof(constMethod)
    __ neg4 (RCX_cmthd);
    __ add4i(RCX_cmthd,-in_bytes(constMethodOopDesc::codes_offset()));
    __ st2  (R09_oop, -sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_bci), RCX_cmthd);
    __ jmp  (done);
  }

  // -----------------------
  // Attempt TLAB allocation
  // RSI_kid: KID
  // RCX: length/ekid=0
  // RDX_size: instance size (in bytes)
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ call (StubRoutines::new_fast_array() );
  __ jne  (done); // try to allocate in the heap if stack ran out of memory

  // RDX: instance size
  // RCX: array length/ekid=0
  // RSI: klassID
__ bind(slow_path);
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ xchg8(RSI_BCP,R11);      // Restore RSI_BCP from R11; KID to R11
  __ xchg8(RDX_LOC,R10);      // Restore RDX_LOC from R10; SIZ to R10
  __ call_runtime_new(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R11, R10, RCX, allow_sba);

#if defined(ASSERT)
Label ok;
  __ test8(RCX,RCX);
__ jne(ok);
__ should_not_reach_here("SharedRuntime::_newarray returns null, should throw instead");
__ bind(ok);
#endif // ASSERT
__ jmp(done2);

  // continue
  __ bind(done);
  // RAX: oop to return
  // R10: Saved RDX_LOC
  // R11: Saved RSI_BCP
  __ move8(RSI_BCP,R11);        // Restore RSI_BCP from R11
  __ move8(RDX_LOC,R10);        // Restore RDX_LOC from R10
__ bind(done2);
  __ move8(RCX,RAX);            // Return ref
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_NewOop);
}


void TemplateTable::anewarray_impl( bool allow_sba ) {
  __ push_tos();       // we must uncache TOS/length before calling into the vm
  __ getmoop(R10);              // Reload method OOP
  __ ldref_lvb(RInOuts::a, R09, R11, RKeepIns::a, R10, in_bytes(methodOopDesc::constants_offset()),false);
  __ ldz2  (RAX, RSI_BCP, 1);   // misaligned load, works great on X86
  __ bswap2(RAX);               // but wrong byte order
  // arg0=Thread
  // arg1=constantPoolRef
  // arg2=index
  // arg3=length in TOS
  { Register Rtmp0 = __ get_free_reg(bytecode(), RAX, RCX_TOS, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::anewarray), RAX,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, R09, RAX, RCX_TOS);
  }
  __ move8 (RCX_TOS,RAX);       // Array returned in RAX; move to TOS
  __ add8i (RDI_JEX,-8);        // Discard value cached before vm call
  __ verify_not_null_oop(RCX_TOS, MacroAssembler::OopVerify_NewOop);
}


void TemplateTable::arraylength() {
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_LoadBase);
  __ cvta2va(RCX_TOS);
  __ ldz4(RCX_TOS, RCX_TOS, arrayOopDesc::length_offset_in_bytes());
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


// --- instanceof_checkcast
// Common implementation of checkcast & instanceof
void TemplateTable::instanceof_checkcast( bool is_ccast ) {
  Label done, do_resolve, do_check, success;

  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ test8(RCX,RCX);            // NULL is easy
  __ jze  (done);               // 0 for instanceof, NULL and no exception for checkcast

  Register R09_cp    = R09;     // R09 constant pool oop
  Register R10_cpidx = R10;     // R10 constant pool index
  { Register R11_moop = R11;
    __ getmoop(R11_moop);         // Reload method OOP
    __ ldref_lvb(RInOuts::a, R09_cp, RAX_TMP, RKeepIns::a, R11_moop, in_bytes(methodOopDesc::constants_offset()),false);
  }
  __ ldz2  (R10_cpidx, RSI_BCP, 1); // unaligned load, works great on X86
  __ bswap2(R10_cpidx);

  Register RAX_super = RAX;     // Klass we're testing TOS against
  // check if constant pool entry was resolved
  // RCX_TOS:   object
  // R09_CP:    constantPoolOop
  // R10_CPIDX: index
  Register R11_tags = R11;
  __ cvta2va(R09_cp);
  __ ldref_lvb(RInOuts::a, R11_tags, RAX_TMP, RKeepIns::a, R09_cp, constantPoolOopDesc::tags_offset_in_bytes(),false);
  __ cvta2va(R11_tags);
  // A little tricky to parse this next instruction, so here's the breakdown:
  // cmp1i [R11 + R10_CPIDX*1 + typeArrayOopDesc::header_size_T_BYTE] vs JVM_CONSTANT_Class
  __ cmp1i(R11_tags,typeArrayOopDesc::header_size_T_BYTE(),R10_cpidx,0, JVM_CONSTANT_Class);
  __ jne  (do_resolve);

  // constant pool entry was resolved
  // RCX_TOS:   object
  // R09_CP:    constantPoolOop
  // R10_CPIDX: index
  { Register Rtmp = __ get_free_reg(bytecode(), RAX_super, RCX_TOS, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_cp, R10_cpidx);
    __ ldref_lvb(RInOuts::a, RAX_super, Rtmp, RKeepIns::a, R09_cp, sizeof(constantPoolOopDesc), R10_cpidx, 3, false);
  }
  // check for subclass assuming success
  // RAX_SUPER: super klass
__ bind(do_check);
  __ verify_not_null_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  if( is_ccast ) __ push_tos(); // preserve TOS for checkcast
  Register RCX_subkid = RCX;
  __ ref2kid(RCX_TOS); // klassRef of objectRef in RCX into kid

  // RAX_super:  super klass
  // RCX_subkid: sub klass id
  { Register R09_tmp = R09;
    Register Rtmp1 = __ get_free_reg(bytecode(), RAX_super, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_tmp, RCX_subkid);
    Register Rtmp2 = __ get_free_reg(bytecode(), RAX_super, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, R09_tmp, RCX_subkid, Rtmp1);
    __ check_subtype(RInOuts::a,RCX_subkid,RAX_super,R09_tmp,Rtmp1,Rtmp2,success);
  }
  if( is_ccast ) {
__ pop_tos();
__ jmp(Interpreter::_throw_ClassCastException_entry);
  } else {
    __ mov8i(RCX_TOS, 0);       // not instanceof
    __ jmp  (done);
  }

  // Resolve class code
__ bind(do_resolve);
  __ push_tos(); // save RCX_TOS
  { Register Rtmp0 = __ get_free_reg(bytecode(), RAX_super, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::quicken_io_cc), RAX_super,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, noreg, noreg, noreg);
  }
  // RAX_SUPER contains resolved class
  __ pop_tos(); // restore RCX_TOS
__ jmp(do_check);

__ bind(success);
  if( is_ccast ) {
__ pop_tos();
  } else {
    __ mov8i(RCX_TOS, 1);       // is instanceof
  }
  __ bind (done);
}

void TemplateTable::instanceof() { instanceof_checkcast(false); }
void TemplateTable::checkcast () { instanceof_checkcast(true ); }


// --- breakpoint
void TemplateTable::_breakpoint() {
__ unimplemented("breakpoint");
//
//  // Note: We get here even if we are single stepping..
//  // jbug inists on setting breakpoints at every bytecode 
//  // even if we are in single step mode.  
//
//  // get the unpatched byte code @ BCP
//  __ push_tos();
//  __ move(IA1, MTH);
//  __ move(IA2, BCP);
//  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::get_original_bytecode_at), IA0, IA1, IA2);
//  __ move(IS0, IA0);
//  __ move(IA1, MTH);
//  __ move(IA2, BCP);
//  __ call_VM(noreg, CAST_FROM_FN_PTR(address, InterpreterRuntime::_breakpoint), IA0, IA1, IA2);
//  __ pop_tos(TOS);
//  __ dispatch_bytecode_in_reg_via(IS0, Interpreter::normal_table(vtos));
//   
}


//----------------------------------------------------------------------------------------------------
// Exceptions

void TemplateTable::athrow() {
Label nul_obj;
  __ null_chk(RCX_TOS,nul_obj);
  __ verify_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  __ jmp (Interpreter::throw_exception_entry());
  // jump to NPE entry
__ bind(nul_obj);
  __ getthr(RAX_TMP);
  __ st8i(RAX_TMP,in_bytes(JavaThread::epc_offset()), nul_obj);
  __ jmp(StubRoutines::x86::handler_for_null_ptr_exception_entry());
}


//----------------------------------------------------------------------------------------------------
// Synchronization


// Record the object being locked in the 'monitor stack' of the interpreter
// frame.  Note that we have to record the object even if the object is
// biased-locked to this thread - as we must be able to count lock actions in
// case we need to inflate the lock, or take an exception exit and need to
// unlock only those locks taken in this frame.  monitorenter bytecodes do not
// need to balance or nest in any reasonable way.  We "count" monitorenter
// bytecodes by saving the locked object in a linear list.
void TemplateTable::monitorenter() {
Label fast_locked;

  // Locks RCX, without blowing RCX.
  // Blows RAX, R09, R10.
  // May fall out into the slow-path, or branch to the fast_path label
  __ fast_path_lock(RInOuts::a,R10,R09,RAX,RKeepIns::a,RCX,fast_locked);
  // RAX: markWord from lock attempt
  // R09: shifted thread ID
  // RCX: objectRef to lock
  __ call (StubRoutines::lock_entry());
  __ jeq  (fast_locked);

  // Slow-path locking
  { Register RAX_res = RAX; // value of RCX may change if GC occurs, new value returned in RAX
    Register Rtmp0 = __ get_free_reg(bytecode(), RAX_res, RCX_TOS, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, SharedRuntime::monitorenter), RAX_res,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RCX_TOS, noreg, noreg);
    __ mov8 (RCX_TOS, RAX_res);
    __ verify_not_null_oop(RCX_TOS, MacroAssembler::OopVerify_Move);
  }
  // RCX: objectRef that was locked

__ bind(fast_locked);

  // ---
  // Now that we hold the lock, record it
Label ok,retry;
  __ bind (retry);
  __ getthr(RAX);
  __ ld8  (R09,RAX,in_bytes(JavaThread::lckstk_top_offset()));
  __ cmp8 (R09,RAX,in_bytes(JavaThread::lckstk_max_offset()));
__ jlt(ok);
__ push(RCX_TOS);
__ push(RDX_LOC);
__ push(RSI_BCP);
__ push(RDI_JEX);
__ push(R08_CPC);
  __ call_VM_leaf((address)JavaThread::extend_lckstk,RAX);
__ pop(R08_CPC);
__ pop(RDI_JEX);
__ pop(RSI_BCP);
__ pop(RDX_LOC);
__ pop(RCX_TOS);
__ jmp(retry);
__ bind(ok);
  if( RefPoisoning ) __ always_poison(RCX);
  __ st8  (R09,0,RCX);          // Store lock in lockstack
  __ add8i(R09,8);              // Bump lockstack top
  __ st8  (RAX,in_bytes(JavaThread::lckstk_top_offset()),R09);
  __ add2i(RSP,offset_of(IFrame,_numlck),1);
  
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);
}


// --- monitorexit
void TemplateTable::monitorexit() {
  Label slow_unlock, done, npe, throwup;
  // RCX_TOS: object to unlock
  // Scan the lockstack for the entry
  { Register RAX_cnt = RAX; // holds count of locks in current frame
    Register R08_tmp = R08; // scratch register - cpc restored at end
    Register R09_cmp = R09; // value we're comparing against in the lock stack
    Register R10_lstlck = R10; // top of the lock stack, used to stomp and carelessly shrink the stack
    Register R11_lckstk = R11; // lock stack, scanned backwards

    // count of locks
    __ ldz2 (RAX_cnt,RSP,offset_of(IFrame,_numlck));
    __ test4(RAX_cnt,RAX_cnt);
    __ jze  (throwup);            // Zero length; skip scan

     // load top of lock stack (NB lock stack is not an oop)
    __ getthr(R09);
    __ ld8   (R11_lckstk,R09,in_bytes(JavaThread::lckstk_top_offset()));
    __ sub8i (R11_lckstk, 8);        // Scan starts at 1st word
    // Load last lock taken
    __ ldref_lvb(RInOuts::a, R10_lstlck, R09, RKeepIns::a, R11_lckstk, 0, false);
    __ move8(R09_cmp, R10_lstlck);   // 1st value to compare is last lock

    Label again(_masm), found;
    __ cmp8 (RCX_TOS, R09_cmp);
__ jeq(found);
    __ sub8i(R11_lckstk, 8);
    __ ldref_lvb(RInOuts::a, R09_cmp, R08_tmp, RKeepIns::a, R11_lckstk, 0, false);
    __ dec4 (RAX_cnt);
    __ jnz  (again);
    // not found
    __ ld8  (R08_CPC,RSP,offset_of(IFrame,_cpc) ); // restore R08_CPC
    __ cvta2va(R08_CPC);
__ jmp(throwup);

    __ bind(found);
    // Clear/compress the entry from lockstack
    // R10 holds top element of stack
    // R11 holds address of last entry compared
    __ st8  (R11_lckstk,0,R10_lstlck);              // Stomp last locked thing over stack entry
    __ getthr(RAX_TMP);
    __ add8i(RAX_TMP,in_bytes(JavaThread::lckstk_top_offset()),-8); // pop last word off lock stack
    __ add2i(RSP,offset_of(IFrame,_numlck),-1); // reduce lock count in frame

    __ ld8  (R08_CPC,RSP,offset_of(IFrame,_cpc) ); // restore R08_CPC
    __ cvta2va(R08_CPC);
  }
  // unlock the object
  Register R10_mark = R10; // Will load R10 with markWord
  __ fast_path_unlock(RInOuts::a,RCX_TOS,R10_mark,RAX_TMP,slow_unlock);

  __ bind(done);
  __ dispatch_next_0(RInOuts::a,RAX_TMP);
__ pop_tos();
  __ dispatch_next_1(RInOuts::a,RAX_TMP);
  __ dispatch_next_2(RInOuts::a,RAX_TMP);

__ bind(slow_unlock);
  // R10 = markWord, RCX = oop
  __ xchg8(R10_mark,RCX_TOS);   // for stub RCX=mark, R10=oop
  __ call (StubRoutines::unlock_entry());
  __ jmp  (done);

  // no monitor entry found => throw
  // Do we need to push_tos() here?  I think we might.
__ bind(throwup);
  __ null_chk(RCX_TOS, npe);   // throw NPE instead of IMSE
__ push_tos();
  { Register Rtmp0 = __ get_free_reg(bytecode(), RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
    __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::throw_IllegalMonitorStateException), noreg,
               RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, noreg, noreg, noreg);
  }

  // Branch here with rcx null & create a NPE
__ bind(npe);
  __ getthr(RAX_TMP);
  __ st8i(RAX_TMP,in_bytes(JavaThread::epc_offset()), npe);
  __ jmp(StubRoutines::x86::handler_for_null_ptr_exception_entry());
}


//----------------------------------------------------------------------------------------------------
// Wide instructions

void TemplateTable::wide() {
  __ ldz1 (RAX, RSI_BCP, 1);
  assert0( Interpreter::_wentry_point == ((address*)((int)(intptr_t)Interpreter::_wentry_point)));
  __ jmp8 (noreg, (intptr_t)Interpreter::_wentry_point, RAX, 3);
}


//----------------------------------------------------------------------------------------------------
// Multi arrays

void TemplateTable::multianewarray() {
__ push_tos();
  Register RAX_dims = RAX;
  __ ldz1(RAX_dims, RSI_BCP, 3);         // number of dimensions
  __ neg8(RAX_dims);
  __ lea (RAX_dims, RDI_JEX, 0, RAX_dims, 3); // RAX = (intptr_t*)dimensions
  Register Rtmp0 = __ get_free_reg(bytecode(), RAX_dims, RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC);
  __ call_VM(CAST_FROM_FN_PTR(address, InterpreterRuntime::multianewarray), RAX,
             RDX_LOC, RSI_BCP, RDI_JEX, R08_CPC, Rtmp0, RAX_dims, noreg, noreg);
  __ verify_not_null_oop(RAX, MacroAssembler::OopVerify_NewOop);
  __ mov8(RCX_TOS,RAX);
  __ ldz1(RAX_dims, RSI_BCP, 3); // number of dimensions
  __ neg8(RAX_dims);
  __ lea (RDI_JEX, RDI_JEX, 0, RAX, 3);// tear down dimensions left on the stack
}
