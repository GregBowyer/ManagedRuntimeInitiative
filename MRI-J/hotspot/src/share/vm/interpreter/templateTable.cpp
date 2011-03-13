/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "interp_masm_pd.hpp"
#include "templateTable.hpp"
#include "timer.hpp"

#include "allocation.inline.hpp"

//----------------------------------------------------------------------------------------------------
// Implementation of Template


void Template::initialize(int flags, TosState tos_in, TosState tos_out, generator gen, int arg) {
  _flags   = flags;
  _tos_in  = tos_in;
  _tos_out = tos_out;
  _gen     = gen;
  _arg     = arg;
}


Bytecodes::Code Template::bytecode() const {
  int i = this - TemplateTable::_template_table;
  if (i < 0 || i >= Bytecodes::number_of_codes) i = this - TemplateTable::_template_table_wide;
  return Bytecodes::cast(i);
}


void Template::generate(InterpreterMacroAssembler* masm) {
  // parameter passing
  TemplateTable::_desc = this;
  TemplateTable::_masm = masm;
  // code generation
  _gen(_arg);
}

//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Platform-independent bytecodes

void TemplateTable::float_cmp(int unordered_result) {
  transition(ftos, itos);
  float_cmp(true, unordered_result);
}


void TemplateTable::double_cmp(int unordered_result) {
  transition(dtos, itos);
  float_cmp(false, unordered_result);
}


void TemplateTable::_goto() {
  transition(vtos, vtos);
  branch(false, false);
}


void TemplateTable::goto_w() {
  transition(vtos, vtos);
  branch(false, true);
}


void TemplateTable::jsr_w() {
  transition(vtos, vtos);       // result is not an oop, so do not transition to atos
  branch(true, true);
}


void TemplateTable::jsr() {
  transition(vtos, vtos);       // result is not an oop, so do not transition to atos
  branch(true, false);
}



//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Debugging

void TemplateTable::transition(TosState tos_in, TosState tos_out) {
  assert(_desc->tos_in()  == tos_in , "inconsistent tos_in  information");
  assert(_desc->tos_out() == tos_out, "inconsistent tos_out information");
}


//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Initialization

bool                       TemplateTable::_is_initialized = false;
Template                   TemplateTable::_template_table     [Bytecodes::number_of_codes];
Template                   TemplateTable::_template_table_wide[Bytecodes::number_of_codes];

Template*                  TemplateTable::_desc;
InterpreterMacroAssembler* TemplateTable::_masm;


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(), char filler) {
  assert(filler == ' ', "just checkin'");
  def(code, flags, in, out, (Template::generator)gen, 0);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(int arg), int arg) {
  // should factor out these constants
  const int disp = 1 << Template::does_dispatch_bit;
  const int iswd = 1 << Template::wide_bit;
  // determine which table to use
  bool is_wide = (flags & iswd) != 0;
  // make sure that wide instructions have a vtos entry point
  // (since they are executed extremely rarely, it doesn't pay out to have an
  // extra set of 5 dispatch tables for the wide instructions - for simplicity
  // they all go with one table)
  assert(in == vtos || !is_wide, "wide instructions have vtos entry point only");
  Template* t = is_wide ? template_for_wide(code) : template_for(code);
  // setup entry
  t->initialize(flags, in, out, gen, arg);
  assert(t->bytecode() == code, "just checkin'");
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Operation op), Operation op) {
  def(code, flags, in, out, (Template::generator)gen, (int)op);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(bool arg    ), bool arg) {
  def(code, flags, in, out, (Template::generator)gen, (int)arg);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(TosState tos), TosState tos) {
  def(code, flags, in, out, (Template::generator)gen, (int)tos);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Condition cc), Condition cc) {
  def(code, flags, in, out, (Template::generator)gen, (int)cc);
}

#if defined(TEMPLATE_TABLE_BUG)
//
// It appears that gcc (version 2.91) generates bad code for the template
// table init if this macro is not defined.  My symptom was an assertion
// assert(Universe::heap()->is_in(obj), "sanity check") in handles.cpp line 24.
// when called from interpreterRuntime.resolve_invoke().
//
  #define iload  TemplateTable::iload
  #define lload  TemplateTable::lload
  #define fload  TemplateTable::fload
  #define dload  TemplateTable::dload
  #define aload  TemplateTable::aload
  #define istore TemplateTable::istore
  #define lstore TemplateTable::lstore
  #define fstore TemplateTable::fstore
  #define dstore TemplateTable::dstore
  #define astore TemplateTable::astore
#endif // TEMPLATE_TABLE_BUG

void TemplateTable::initialize() {
  if (_is_initialized) return;

  // Initialize table
  TraceTime timer("TemplateTable initialization", TraceStartupTime);

  // For better readability
  const char _    = ' ';
  const int  ____ = 0;
  const int  disp = 1 << Template::does_dispatch_bit;
  const int  iswd = 1 << Template::wide_bit;
  //                                    interpr. templates                                                  
//Java spec bytecodes                disp|iswd  in    out   generator             argument
def(Bytecodes::_nop,____|____,vtos,vtos,nop,_);
def(Bytecodes::_aconst_null,____|____,vtos,atos,aconst_null,_);
def(Bytecodes::_iconst_m1,____|____,vtos,itos,iconst,-1);
def(Bytecodes::_iconst_0,____|____,vtos,itos,iconst,0);
def(Bytecodes::_iconst_1,____|____,vtos,itos,iconst,1);
def(Bytecodes::_iconst_2,____|____,vtos,itos,iconst,2);
def(Bytecodes::_iconst_3,____|____,vtos,itos,iconst,3);
def(Bytecodes::_iconst_4,____|____,vtos,itos,iconst,4);
def(Bytecodes::_iconst_5,____|____,vtos,itos,iconst,5);
def(Bytecodes::_lconst_0,____|____,vtos,ltos,lconst,0);
def(Bytecodes::_lconst_1,____|____,vtos,ltos,lconst,1);
def(Bytecodes::_fconst_0,____|____,vtos,ftos,fconst,0);
def(Bytecodes::_fconst_1,____|____,vtos,ftos,fconst,1);
def(Bytecodes::_fconst_2,____|____,vtos,ftos,fconst,2);
def(Bytecodes::_dconst_0,____|____,vtos,dtos,dconst,0);
def(Bytecodes::_dconst_1,____|____,vtos,dtos,dconst,1);
def(Bytecodes::_bipush,____|____,vtos,itos,bipush,_);
def(Bytecodes::_sipush,____|____,vtos,itos,sipush,_);
def(Bytecodes::_ldc,____|____,vtos,vtos,ldc,false);
def(Bytecodes::_ldc_w,____|____,vtos,vtos,ldc,true);
def(Bytecodes::_ldc2_w,____|____,vtos,vtos,ldc2_w,_);
def(Bytecodes::_iload,____|____,vtos,itos,iload,_);
def(Bytecodes::_lload,____|____,vtos,ltos,lload,_);
def(Bytecodes::_fload,____|____,vtos,ftos,fload,_);
def(Bytecodes::_dload,____|____,vtos,dtos,dload,_);
def(Bytecodes::_aload,____|____,vtos,atos,aload,_);
def(Bytecodes::_iload_0,____|____,vtos,itos,iload,0);
def(Bytecodes::_iload_1,____|____,vtos,itos,iload,1);
def(Bytecodes::_iload_2,____|____,vtos,itos,iload,2);
def(Bytecodes::_iload_3,____|____,vtos,itos,iload,3);
def(Bytecodes::_lload_0,____|____,vtos,ltos,lload,0);
def(Bytecodes::_lload_1,____|____,vtos,ltos,lload,1);
def(Bytecodes::_lload_2,____|____,vtos,ltos,lload,2);
def(Bytecodes::_lload_3,____|____,vtos,ltos,lload,3);
def(Bytecodes::_fload_0,____|____,vtos,ftos,fload,0);
def(Bytecodes::_fload_1,____|____,vtos,ftos,fload,1);
def(Bytecodes::_fload_2,____|____,vtos,ftos,fload,2);
def(Bytecodes::_fload_3,____|____,vtos,ftos,fload,3);
def(Bytecodes::_dload_0,____|____,vtos,dtos,dload,0);
def(Bytecodes::_dload_1,____|____,vtos,dtos,dload,1);
def(Bytecodes::_dload_2,____|____,vtos,dtos,dload,2);
def(Bytecodes::_dload_3,____|____,vtos,dtos,dload,3);
def(Bytecodes::_aload_0,____|____,vtos,atos,aload_0,_);
def(Bytecodes::_aload_1,____|____,vtos,atos,aload,1);
def(Bytecodes::_aload_2,____|____,vtos,atos,aload,2);
def(Bytecodes::_aload_3,____|____,vtos,atos,aload,3);
def(Bytecodes::_iaload,____|____,itos,itos,iaload,_);
def(Bytecodes::_laload,____|____,itos,ltos,laload,_);
def(Bytecodes::_faload,____|____,itos,ftos,faload,_);
def(Bytecodes::_daload,____|____,itos,dtos,daload,_);
def(Bytecodes::_aaload,____|____,itos,atos,aaload,_);
def(Bytecodes::_baload,____|____,itos,itos,baload,_);
def(Bytecodes::_caload,____|____,itos,itos,caload,_);
def(Bytecodes::_saload,____|____,itos,itos,saload,_);
def(Bytecodes::_istore,____|____,itos,vtos,istore,_);
def(Bytecodes::_lstore,____|____,ltos,vtos,lstore,_);
def(Bytecodes::_fstore,____|____,ftos,vtos,fstore,_);
def(Bytecodes::_dstore,____|____,dtos,vtos,dstore,_);
def(Bytecodes::_astore,____|____,atos,vtos,astore,_);
def(Bytecodes::_istore_0,____|____,itos,vtos,istore,0);
def(Bytecodes::_istore_1,____|____,itos,vtos,istore,1);
def(Bytecodes::_istore_2,____|____,itos,vtos,istore,2);
def(Bytecodes::_istore_3,____|____,itos,vtos,istore,3);
def(Bytecodes::_lstore_0,____|____,ltos,vtos,lstore,0);
def(Bytecodes::_lstore_1,____|____,ltos,vtos,lstore,1);
def(Bytecodes::_lstore_2,____|____,ltos,vtos,lstore,2);
def(Bytecodes::_lstore_3,____|____,ltos,vtos,lstore,3);
def(Bytecodes::_fstore_0,____|____,ftos,vtos,fstore,0);
def(Bytecodes::_fstore_1,____|____,ftos,vtos,fstore,1);
def(Bytecodes::_fstore_2,____|____,ftos,vtos,fstore,2);
def(Bytecodes::_fstore_3,____|____,ftos,vtos,fstore,3);
def(Bytecodes::_dstore_0,____|____,dtos,vtos,dstore,0);
def(Bytecodes::_dstore_1,____|____,dtos,vtos,dstore,1);
def(Bytecodes::_dstore_2,____|____,dtos,vtos,dstore,2);
def(Bytecodes::_dstore_3,____|____,dtos,vtos,dstore,3);
def(Bytecodes::_astore_0,____|____,vtos,vtos,astore,0);
def(Bytecodes::_astore_1,____|____,vtos,vtos,astore,1);
def(Bytecodes::_astore_2,____|____,vtos,vtos,astore,2);
def(Bytecodes::_astore_3,____|____,vtos,vtos,astore,3);
def(Bytecodes::_iastore,____|____,itos,vtos,iastore,_);
def(Bytecodes::_lastore,____|____,ltos,vtos,lastore,_);
def(Bytecodes::_fastore,____|____,ftos,vtos,fastore,_);
def(Bytecodes::_dastore,____|____,dtos,vtos,dastore,_);
def(Bytecodes::_aastore,____|____,vtos,vtos,aastore,_);
def(Bytecodes::_bastore,____|____,itos,vtos,bastore,_);
def(Bytecodes::_castore,____|____,itos,vtos,castore,_);
def(Bytecodes::_sastore,____|____,itos,vtos,sastore,_);
def(Bytecodes::_pop,____|____,vtos,vtos,pop,_);
def(Bytecodes::_pop2,____|____,vtos,vtos,pop2,_);
def(Bytecodes::_dup,____|____,vtos,vtos,dup,_);
def(Bytecodes::_dup_x1,____|____,vtos,vtos,dup_x1,_);
def(Bytecodes::_dup_x2,____|____,vtos,vtos,dup_x2,_);
def(Bytecodes::_dup2,____|____,vtos,vtos,dup2,_);
def(Bytecodes::_dup2_x1,____|____,vtos,vtos,dup2_x1,_);
def(Bytecodes::_dup2_x2,____|____,vtos,vtos,dup2_x2,_);
def(Bytecodes::_swap,____|____,vtos,vtos,swap,_);
def(Bytecodes::_iadd,____|____,itos,itos,iop2,add);
def(Bytecodes::_ladd,____|____,ltos,ltos,lop2,add);
def(Bytecodes::_fadd,____|____,ftos,ftos,fop2,add);
def(Bytecodes::_dadd,____|____,dtos,dtos,dop2,add);
def(Bytecodes::_isub,____|____,itos,itos,iop2,sub);
def(Bytecodes::_lsub,____|____,ltos,ltos,lop2,sub);
def(Bytecodes::_fsub,____|____,ftos,ftos,fop2,sub);
def(Bytecodes::_dsub,____|____,dtos,dtos,dop2,sub);
def(Bytecodes::_imul,____|____,itos,itos,iop2,mul);
def(Bytecodes::_lmul,____|____,ltos,ltos,lmul,_);
def(Bytecodes::_fmul,____|____,ftos,ftos,fop2,mul);
def(Bytecodes::_dmul,____|____,dtos,dtos,dop2,mul);
def(Bytecodes::_idiv,____|____,itos,itos,idiv,_);
def(Bytecodes::_ldiv,____|____,ltos,ltos,ldiv,_);
def(Bytecodes::_fdiv,____|____,ftos,ftos,fop2,div);
def(Bytecodes::_ddiv,____|____,dtos,dtos,dop2,div);
def(Bytecodes::_irem,____|____,itos,itos,irem,_);
def(Bytecodes::_lrem,____|____,ltos,ltos,lrem,_);
def(Bytecodes::_frem,____|____,ftos,ftos,fop2,rem);
def(Bytecodes::_drem,____|____,dtos,dtos,dop2,rem);
def(Bytecodes::_ineg,____|____,itos,itos,ineg,_);
def(Bytecodes::_lneg,____|____,ltos,ltos,lneg,_);
def(Bytecodes::_fneg,____|____,ftos,ftos,fneg,_);
def(Bytecodes::_dneg,____|____,dtos,dtos,dneg,_);
def(Bytecodes::_ishl,____|____,itos,itos,iop2,shl);
def(Bytecodes::_lshl,____|____,itos,ltos,lshl,_);
def(Bytecodes::_ishr,____|____,itos,itos,iop2,shr);
def(Bytecodes::_lshr,____|____,itos,ltos,lshr,_);
def(Bytecodes::_iushr,____|____,itos,itos,iop2,ushr);
def(Bytecodes::_lushr,____|____,itos,ltos,lushr,_);
def(Bytecodes::_iand,____|____,itos,itos,iop2,_and);
def(Bytecodes::_land,____|____,ltos,ltos,lop2,_and);
def(Bytecodes::_ior,____|____,itos,itos,iop2,_or);
def(Bytecodes::_lor,____|____,ltos,ltos,lop2,_or);
def(Bytecodes::_ixor,____|____,itos,itos,iop2,_xor);
def(Bytecodes::_lxor,____|____,ltos,ltos,lop2,_xor);
def(Bytecodes::_iinc,____|____,vtos,vtos,iinc,_);
def(Bytecodes::_i2l,____|____,itos,ltos,convert,_);
def(Bytecodes::_i2f,____|____,itos,ftos,convert,_);
def(Bytecodes::_i2d,____|____,itos,dtos,convert,_);
def(Bytecodes::_l2i,____|____,ltos,itos,convert,_);
def(Bytecodes::_l2f,____|____,ltos,ftos,convert,_);
def(Bytecodes::_l2d,____|____,ltos,dtos,convert,_);
def(Bytecodes::_f2i,____|____,ftos,itos,convert,_);
def(Bytecodes::_f2l,____|____,ftos,ltos,convert,_);
def(Bytecodes::_f2d,____|____,ftos,dtos,convert,_);
def(Bytecodes::_d2i,____|____,dtos,itos,convert,_);
def(Bytecodes::_d2l,____|____,dtos,ltos,convert,_);
def(Bytecodes::_d2f,____|____,dtos,ftos,convert,_);
def(Bytecodes::_i2b,____|____,itos,itos,convert,_);
def(Bytecodes::_i2c,____|____,itos,itos,convert,_);
def(Bytecodes::_i2s,____|____,itos,itos,convert,_);
def(Bytecodes::_lcmp,____|____,ltos,itos,lcmp,_);
def(Bytecodes::_fcmpl,____|____,ftos,itos,float_cmp,-1);
def(Bytecodes::_fcmpg,____|____,ftos,itos,float_cmp,1);
def(Bytecodes::_dcmpl,____|____,dtos,itos,double_cmp,-1);
def(Bytecodes::_dcmpg,____|____,dtos,itos,double_cmp,1);
def(Bytecodes::_ifeq,____|____,itos,vtos,if_0cmp,equal);
def(Bytecodes::_ifne,____|____,itos,vtos,if_0cmp,not_equal);
def(Bytecodes::_iflt,____|____,itos,vtos,if_0cmp,less);
def(Bytecodes::_ifge,____|____,itos,vtos,if_0cmp,greater_equal);
def(Bytecodes::_ifgt,____|____,itos,vtos,if_0cmp,greater);
def(Bytecodes::_ifle,____|____,itos,vtos,if_0cmp,less_equal);
def(Bytecodes::_if_icmpeq,____|____,itos,vtos,if_icmp,equal);
def(Bytecodes::_if_icmpne,____|____,itos,vtos,if_icmp,not_equal);
def(Bytecodes::_if_icmplt,____|____,itos,vtos,if_icmp,less);
def(Bytecodes::_if_icmpge,____|____,itos,vtos,if_icmp,greater_equal);
def(Bytecodes::_if_icmpgt,____|____,itos,vtos,if_icmp,greater);
def(Bytecodes::_if_icmple,____|____,itos,vtos,if_icmp,less_equal);
def(Bytecodes::_if_acmpeq,____|____,atos,vtos,if_acmp,equal);
def(Bytecodes::_if_acmpne,____|____,atos,vtos,if_acmp,not_equal);
def(Bytecodes::_goto,disp|____,vtos,vtos,_goto,_);
def(Bytecodes::_jsr,disp|____,vtos,vtos,jsr,_);//result is not an oop, so do not transition to atos
def(Bytecodes::_ret,disp|____,vtos,vtos,ret,_);
def(Bytecodes::_tableswitch,disp|____,itos,vtos,tableswitch,_);
def(Bytecodes::_lookupswitch,disp|____,itos,itos,lookupswitch,_);
def(Bytecodes::_ireturn,disp|____,itos,itos,_return,itos);
def(Bytecodes::_lreturn,disp|____,ltos,ltos,_return,ltos);
def(Bytecodes::_freturn,disp|____,ftos,ftos,_freturn,ftos);
def(Bytecodes::_dreturn,disp|____,dtos,dtos,_freturn,dtos);
def(Bytecodes::_areturn,disp|____,atos,atos,areturn,_);
def(Bytecodes::_return,disp|____,vtos,vtos,_return,vtos);
def(Bytecodes::_getstatic,disp|____,vtos,vtos,getstatic,1);
def(Bytecodes::_putstatic,disp|____,vtos,vtos,putstatic,2);
def(Bytecodes::_getfield,disp|____,vtos,vtos,getfield,1);
def(Bytecodes::_putfield,disp|____,vtos,vtos,putfield,2);
def(Bytecodes::_invokevirtual,disp|____,vtos,vtos,invokevirtual,2);
def(Bytecodes::_invokespecial,disp|____,vtos,vtos,invokespecial,1);
def(Bytecodes::_invokestatic,disp|____,vtos,vtos,invokestatic,1);
def(Bytecodes::_invokeinterface,disp|____,vtos,vtos,invokeinterface,1);
def(Bytecodes::_new,____|____,vtos,atos,_new,_);
def(Bytecodes::_newarray,____|____,itos,atos,newarray,_);
def(Bytecodes::_anewarray,____|____,itos,atos,anewarray,_);
def(Bytecodes::_arraylength,____|____,atos,itos,arraylength,_);
def(Bytecodes::_athrow,disp|____,atos,vtos,athrow,_);
def(Bytecodes::_checkcast,____|____,atos,atos,checkcast,_);
def(Bytecodes::_instanceof,____|____,atos,itos,instanceof,_);
def(Bytecodes::_monitorenter,____|____,atos,vtos,monitorenter,_);
def(Bytecodes::_monitorexit,____|____,atos,vtos,monitorexit,_);
def(Bytecodes::_wide,disp|____,vtos,vtos,wide,_);
def(Bytecodes::_multianewarray,____|____,vtos,atos,multianewarray,_);
def(Bytecodes::_ifnull,____|____,atos,vtos,if_nullcmp,equal);
def(Bytecodes::_ifnonnull,____|____,atos,vtos,if_nullcmp,not_equal);
def(Bytecodes::_goto_w,disp|____,vtos,vtos,goto_w,_);
def(Bytecodes::_jsr_w,disp|____,vtos,vtos,jsr_w,_);

  // wide Java spec bytecodes
def(Bytecodes::_iload,____|iswd,vtos,itos,wide_iload,_);
def(Bytecodes::_lload,____|iswd,vtos,ltos,wide_lload,_);
def(Bytecodes::_fload,____|iswd,vtos,ftos,wide_fload,_);
def(Bytecodes::_dload,____|iswd,vtos,dtos,wide_dload,_);
def(Bytecodes::_aload,____|iswd,vtos,atos,wide_aload,_);
def(Bytecodes::_istore,____|iswd,vtos,vtos,wide_istore,_);
def(Bytecodes::_lstore,____|iswd,vtos,vtos,wide_lstore,_);
def(Bytecodes::_fstore,____|iswd,vtos,vtos,wide_fstore,_);
def(Bytecodes::_dstore,____|iswd,vtos,vtos,wide_dstore,_);
def(Bytecodes::_astore,____|iswd,vtos,vtos,wide_astore,_);
def(Bytecodes::_iinc,____|iswd,vtos,vtos,wide_iinc,_);
def(Bytecodes::_ret,disp|iswd,vtos,vtos,wide_ret,_);
def(Bytecodes::_breakpoint,disp|____,vtos,vtos,_breakpoint,_);

  // JVM bytecodes
def(Bytecodes::_fast_agetfield,____|____,atos,atos,fast_accessfield,atos);
def(Bytecodes::_fast_bgetfield,____|____,atos,itos,fast_accessfield,itos);
def(Bytecodes::_fast_cgetfield,____|____,atos,itos,fast_accessfield,itos);
def(Bytecodes::_fast_dgetfield,____|____,atos,dtos,fast_accessfield,dtos);
def(Bytecodes::_fast_fgetfield,____|____,atos,ftos,fast_accessfield,ftos);
def(Bytecodes::_fast_igetfield,____|____,atos,itos,fast_accessfield,itos);
def(Bytecodes::_fast_lgetfield,____|____,atos,ltos,fast_accessfield,ltos);
def(Bytecodes::_fast_sgetfield,____|____,atos,itos,fast_accessfield,itos);

def(Bytecodes::_fast_aputfield,____|____,atos,vtos,fast_storefield,atos);
def(Bytecodes::_fast_bputfield,____|____,itos,vtos,fast_storefield,itos);
def(Bytecodes::_fast_cputfield,____|____,itos,vtos,fast_storefield,itos);
def(Bytecodes::_fast_dputfield,____|____,dtos,vtos,fast_storefield,dtos);
def(Bytecodes::_fast_fputfield,____|____,ftos,vtos,fast_storefield,ftos);
def(Bytecodes::_fast_iputfield,____|____,itos,vtos,fast_storefield,itos);
def(Bytecodes::_fast_lputfield,____|____,ltos,vtos,fast_storefield,ltos);
def(Bytecodes::_fast_sputfield,____|____,itos,vtos,fast_storefield,itos);

def(Bytecodes::_fast_aload_0,____|____,vtos,atos,aload,0);
def(Bytecodes::_fast_iaccess_0,____|____,vtos,itos,fast_xaccess,itos);
def(Bytecodes::_fast_aaccess_0,____|____,vtos,atos,fast_xaccess,atos);
def(Bytecodes::_fast_faccess_0,____|____,vtos,ftos,fast_xaccess,ftos);

def(Bytecodes::_fast_iload,____|____,vtos,itos,fast_iload,_);
def(Bytecodes::_fast_iload2,____|____,vtos,itos,fast_iload2,_);
def(Bytecodes::_fast_icaload,____|____,vtos,itos,fast_icaload,_);

def(Bytecodes::_fast_invokevfinal,disp|____,vtos,vtos,fast_invokevfinal,2);


def(Bytecodes::_fast_linearswitch,disp|____,itos,vtos,fast_linearswitch,_);
def(Bytecodes::_fast_binaryswitch,disp|____,itos,vtos,fast_binaryswitch,_);

def(Bytecodes::_return_register_finalizer,disp|____,vtos,vtos,_return,vtos);

def(Bytecodes::_shouldnotreachhere,____|____,vtos,vtos,shouldnotreachhere,_);
  // platform specific bytecodes
  pd_initialize();
  // Some Azul-specific bytecodes
  def(Bytecodes::_new_heap            , ____|____, vtos, atos, new_heap            ,  _           );
  def(Bytecodes::_newarray_heap       , ____|____, itos, atos, newarray_heap       ,  _           );
  def(Bytecodes::_anewarray_heap      , ____|____, itos, atos, anewarray_heap      ,  _           );

  _is_initialized = true;
}

#if defined(TEMPLATE_TABLE_BUG)
  #undef iload
  #undef lload
  #undef fload
  #undef dload
  #undef aload
  #undef istore
  #undef lstore
  #undef fstore
  #undef dstore
  #undef astore
#endif // TEMPLATE_TABLE_BUG


void templateTable_init() {
  TemplateTable::initialize();
}


void TemplateTable::unimplemented_bc() {  
  _masm->unimplemented( Bytecodes::name(_desc->bytecode()));
}
