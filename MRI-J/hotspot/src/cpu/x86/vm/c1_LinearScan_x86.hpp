/*
 * Copyright 2005-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_LINEARSCAN_PD_HPP
#define C1_LINEARSCAN_PD_HPP


// Is the given register valid for use by linear scan
inline bool LinearScan::is_processed_reg_num(int reg_num) {
assert(FrameMap::rsp_opr->cpu_regnr()==RSP,"wrong assumption below");
  assert(reg_num >= 0, "invalid reg_num");
  if (reg_num >= nof_regs) return true;
  return reg_num != RSP;
}

inline int LinearScan::num_physical_regs(BasicType type) { return 1; }

inline bool LinearScan::requires_adjacent_regs(BasicType type) { return false; }

inline bool LinearScan::is_caller_save(int assigned_reg) {
assert(assigned_reg>=0&&assigned_reg<33,"should call this only for registers");
//RBX, RBP, RSP, R12, R13, R14, R15 are callee save.  Of course, RSP is
  // "free" to save.  RBP is "free" to save only for C1 which will
  // save/restore it as part of the C1 prolog & epilog.
  // NO OOPS in callee-save registers please.
  // RCX, RDX, RSI, RDI, R08, R09 are used to pass args.
  // RAX, R10, R11 are temps and killed.
  // XMM0-7 are used to pass args.
  // XMM8-15 are temps and killed (but might as well be used to pass args).

  //      RRRRRRRRRRRRRRRRXXXXXXXXXXXXXXXXf
  //      ACDBSBSD00111111MMMMMMMMMMMMMMMMl
  //      XXXXPPII890123450123456789ABCDEFg
  return "111000111111000011111111111111111"[assigned_reg] == '1';
}

inline void LinearScan::pd_add_temps(LIR_Op* op) {
  int op_id = op->id();
  switch (op->code()) {
    case lir_rtcall: {
      // RAX is clobbered and often a return value
      //add_temp(reg_num(FrameMap::rax_opr), op_id, noUse, T_ILLEGAL);
      break;
    }
    case lir_alloc_object:
    case lir_alloc_array: {
      add_temp(reg_num(FrameMap::xmm15_double_opr), op_id, mustHaveRegister, T_ILLEGAL);
      break;
    }
    case lir_convert: {
LIR_OpConvert*conv_op=(LIR_OpConvert*)op;
switch(conv_op->bytecode()){
case Bytecodes::_f2i:
      case Bytecodes::_d2i:
case Bytecodes::_f2l:
case Bytecodes::_d2l:{
BasicType type=T_LONG;
        LIR_Opr tmp = _gen->get_temp(0, type);
        conv_op->set_tmp1_opr(tmp);
add_temp(reg_num(tmp),op_id,mustHaveRegister,type);
        break;
      }
      default:
        break;
      }
      break;
    }
    case lir_move:   {
      int tempsNeeded = 0;
LIR_Op1*move_op=(LIR_Op1*)op;
LIR_Opr src=move_op->in_opr();
LIR_Opr dest=move_op->result_opr();
      bool     isOopMove = (move_op->type() == T_ARRAY) || (move_op->type() == T_OBJECT);
if(src->is_address()){
LIR_Address*addr=src->as_address_ptr();
        BasicType base_type = addr->base()->type();
        if ( isOopMove && (base_type == T_OBJECT || base_type == T_ARRAY)) {
          // Load Oop!!!
          tempsNeeded = 1;
          // Ensure base (and possibly index) are live whilst result is computed
          add_temp(addr->base(), op_id, mustHaveRegister);
          if (addr->index() != LIR_OprFact::illegalOpr) {
            add_temp(addr->index(), op_id, mustHaveRegister);
          }
        }
      } else if ( src->is_constant() && dest->is_address() && isOopMove && src->as_jobject() != NULL ) {
        tempsNeeded = 3;
      } else if ( src->is_constant() && dest->is_address() && ((move_op->type() == T_DOUBLE) || (move_op->type() == T_LONG)) ) {
        tempsNeeded = 1;
      } else if ( src->is_constant() && dest->is_stack() && isOopMove && src->as_jobject() != NULL ) {
        tempsNeeded = 1;
      } else if ( src->is_stack()    && dest->is_stack() && !src->is_equal(dest) ) {
        tempsNeeded = 1;
}else if(src->is_constant()&&dest->is_register()){
LIR_Const*c=src->as_constant_ptr();
if(c->type()==T_OBJECT){
          if ( move_op->patch_code() != lir_patch_none ) {
            // patching requires an lvb and 2 temp registers
            tempsNeeded = 2;
          } else if( c->as_jobject() != NULL ) {
            tempsNeeded = 1;
          }
        } else if ( (c->type() == T_FLOAT  && c->as_jint_bits() != 0) ||
                    (c->type() == T_DOUBLE && c->as_jlong_bits() != 0) ) {
          // temp required for mov8i
          tempsNeeded = 1;
        }
}else if(src->is_register()&&dest->is_address()){
        if ( isOopMove ) {
          // Store Oop!!!
          LIR_Address* addr   = dest->as_address_ptr();
          // Out going argument stores don't need temps
          tempsNeeded = addr->base() == FrameMap::rsp_opr ? 0 : 2;
          if (move_op->patch_code() != lir_patch_none)
            tempsNeeded ++;
          // Ensure the src of a store oop doesn't die midway through the operation
add_temp(src,op_id,mustHaveRegister);
          // Ensure different registers for base, index and temps
          BasicType base_type = addr->base()->type();
if(base_type==T_OBJECT||base_type==T_ARRAY){
            add_temp(addr->base(), op_id, mustHaveRegister);
            if (addr->index() != LIR_OprFact::illegalOpr) {
              add_temp(addr->index(), op_id, mustHaveRegister);
            }
          }
        }
      } else if ( src->is_constant() && dest->is_stack() && isOopMove) {
        tempsNeeded = 1;
      }

      if (tempsNeeded > 0) {
        BasicType type = T_LONG; /* move_op->type() */
        LIR_Opr tmp = _gen->get_temp(0, type);
        move_op->set_tmp1_opr(tmp);
add_temp(reg_num(tmp),op_id,mustHaveRegister,type);
        if (tempsNeeded > 1) {
          tmp = _gen->get_temp(1, type);
          move_op->set_tmp2_opr(tmp);
add_temp(reg_num(tmp),op_id,mustHaveRegister,type);
          if (tempsNeeded > 2) {
            tmp = _gen->get_temp(2, type);
            move_op->set_tmp3_opr(tmp);
add_temp(reg_num(tmp),op_id,mustHaveRegister,type);
          }
        }
      }
      break;
    }
  case lir_unlock:
    // Unlock crushes R09 - RCX, RAX and R10 are explicit temp registers
    add_temp(reg_num(FrameMap::r09_opr), op_id, mustHaveRegister, T_ILLEGAL);
    break;
  default: break;
  }
}


// Implementation of LinearScanWalker

inline bool LinearScanWalker::pd_init_regs_for_alloc(Interval* cur) {
  // We can allocate all registers for all instructions, no special set up
  // required (NB. JavaSoft handle byte registers specially here, but AZUL
  // avoid the notion of byte registers by applying a REX prefix to make
  // register addressing uniform).
  return false;
}


#endif // C1_LINEARSCAN_PD_HPP
