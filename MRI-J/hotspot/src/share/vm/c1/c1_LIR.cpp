/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "c1_FrameMap.hpp"
#include "c1_IR.hpp"
#include "c1_Instruction.hpp"
#include "c1_LIR.hpp"
#include "c1_LIRAssembler.hpp"
#include "ciTypeArrayKlass.hpp"

#include "oop.inline.hpp"

Register LIR_OprDesc::as_register() const { 
  assert0(this != LIR_OprFact::illegalOpr);
  return FrameMap::cpu_rnr2reg(cpu_regnr()); 
}

Register LIR_OprDesc::as_register_lo() const { 
  assert0(this != LIR_OprFact::illegalOpr);
  return FrameMap::cpu_rnr2reg(cpu_regnrLo()); 
}

Register LIR_OprDesc::as_register_hi() const { 
  assert0(this != LIR_OprFact::illegalOpr);
  return FrameMap::cpu_rnr2reg(cpu_regnrHi()); 
}

#ifdef AZ_X86
FRegister LIR_OprDesc::as_xmm_float_reg()const{
  assert0(this != LIR_OprFact::illegalOpr);
  return FrameMap::nr2xmmreg(xmm_regnr());
}

FRegister LIR_OprDesc::as_xmm_double_reg()const{
  assert0(this != LIR_OprFact::illegalOpr);
  return FrameMap::nr2xmmreg(xmm_regnr());
}
#endif

LIR_Opr LIR_OprFact::illegalOpr = LIR_OprFact::illegal();

LIR_Opr LIR_OprFact::value_type(ValueType* type) {
  ValueTag tag = type->tag();
  switch (tag) {
  case objectTag : {
    ClassConstant* c = type->as_ClassConstant();
    if (c != NULL && !c->value()->is_loaded()) {
      return LIR_OprFact::oopConst(NULL);
    } else {
      return LIR_OprFact::oopConst(type->as_ObjectType()->encoding());
    }
  }
  case addressTag: return LIR_OprFact::intConst(type->as_AddressConstant()->value());
  case intTag    : return LIR_OprFact::intConst(type->as_IntConstant()->value());
  case floatTag  : return LIR_OprFact::floatConst(type->as_FloatConstant()->value());
  case longTag   : return LIR_OprFact::longConst(type->as_LongConstant()->value());
  case doubleTag : return LIR_OprFact::doubleConst(type->as_DoubleConstant()->value());
default:ShouldNotReachHere();return illegalOpr;
  }
}


LIR_Opr LIR_OprFact::dummy_value_type(ValueType* type) {
  switch (type->tag()) {
    case objectTag: return LIR_OprFact::oopConst(NULL);
    case addressTag:
    case intTag:    return LIR_OprFact::intConst(0);
    case floatTag:  return LIR_OprFact::floatConst(0.0);
    case longTag:   return LIR_OprFact::longConst(0);
    case doubleTag: return LIR_OprFact::doubleConst(0.0);
    default:        ShouldNotReachHere();
  }
  return illegalOpr;
}



//---------------------------------------------------


LIR_Address::Scale LIR_Address::scale(BasicType type) {
  int elem_size = type2aelembytes[type];
  switch (elem_size) {
  case 1: return LIR_Address::times_1;
  case 2: return LIR_Address::times_2;
  case 4: return LIR_Address::times_4;
  case 8: return LIR_Address::times_8;
  }
  ShouldNotReachHere();
  return LIR_Address::times_1;
}


#ifndef PRODUCT
void LIR_Address::verify() const {
  assert(base()->is_cpu_register(), "wrong base operand");
assert(index()->is_illegal()||index()->is_cpu_register(),"wrong index operand");
  assert(base()->type() == T_OBJECT || 
         base() == FrameMap::stack_pointer() || // x86 only
         base()->type() == T_LONG,
         "wrong type for addresses");
}
#endif


//---------------------------------------------------

char LIR_OprDesc::type_char(BasicType t) {
  switch (t) {
    case T_ARRAY:
      t = T_OBJECT;
    case T_BOOLEAN:
    case T_CHAR:
    case T_FLOAT:
    case T_DOUBLE:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
    case T_LONG:
    case T_OBJECT:
    case T_ADDRESS:
    case T_VOID:
      return ::type2char(t);

    case T_ILLEGAL:
      return '?';

    default:
      ShouldNotReachHere();
      return 0;
  }
}

#ifndef PRODUCT
void LIR_OprDesc::validate_type() const { 

#ifdef ASSERT
  if (!is_pointer() && !is_illegal()) {
    switch (as_BasicType(type_field())) {
    case T_LONG:
      assert((kind_field() == cpu_register || kind_field() == stack_value) && size_field() == double_size, "must match");
      break;
    case T_FLOAT:
      assert((kind_field() == fpu_register || kind_field() == stack_value) && size_field() == single_size, "must match");
      break;
    case T_DOUBLE:
      assert((kind_field() == fpu_register || kind_field() == stack_value) && size_field() == double_size, "must match");
      break;
    case T_BOOLEAN:
    case T_CHAR:
    case T_BYTE:
    case T_SHORT:
    case T_INT:
    case T_OBJECT:
    case T_ARRAY:
      assert((kind_field() == cpu_register || kind_field() == stack_value) && size_field() == single_size, "must match");
      break;

    case T_ILLEGAL:
      // XXX TKR also means unknown right now
      // assert(is_illegal(), "must match");
      break;

    default:
      ShouldNotReachHere();
    }
  }
#endif

}
#endif // PRODUCT


bool LIR_OprDesc::is_oop() const {
  if (is_pointer()) {
    return pointer()->is_oop_pointer();
  } else {
    OprType t= type_field();
    assert(t != unknown_type, "not set");
    return t == object_type;
  }
}



void LIR_Op2::verify() const {
#ifdef ASSERT
  switch (code()) {
    case lir_cmove:
      break;

    default:
      assert(!result_opr()->is_register() || !result_opr()->is_oop_register(),
             "can't produce oops from arith");
  }

  if (TwoOperandLIRForm) {
    switch (code()) {
    case lir_add:
    case lir_sub:
    case lir_mul:
    case lir_mul_strictfp:
    case lir_div:
    case lir_div_strictfp:
    case lir_rem:
    case lir_logic_and:
    case lir_logic_or:
    case lir_logic_xor:
    case lir_shl:
    case lir_shr:
      assert(in_opr1() == result_opr(), "opr1 and result must match");
      assert(in_opr1()->is_valid() && in_opr2()->is_valid(), "must be valid");
      break;

    // special handling for lir_ushr because of write barriers
    case lir_ushr:
      assert(in_opr1() == result_opr() || in_opr2()->is_constant(), "opr1 and result must match or shift count is constant");
      assert(in_opr1()->is_valid() && in_opr2()->is_valid(), "must be valid");
      break;

    }
  }
#endif
}


#ifdef AZ_X86
// compare-into-flags / branch-into-flags style
LIR_OpBranch::LIR_OpBranch(LIR_Condition cond, BasicType type, BlockBegin* block)
  : LIR_Op(lir_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*)NULL)
,_condition(cond)
  , _type(type)
  , _label(block->label())
  , _block(block)
  , _ublock(NULL)
  , _stub(NULL) {
}

LIR_OpBranch::LIR_OpBranch(LIR_Condition cond, BasicType type, CodeStub* stub) :
  LIR_Op(lir_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*)NULL)
,_condition(cond)
  , _type(type)
  , _label(stub->entry())
  , _block(NULL)
  , _ublock(NULL)
  , _stub(stub) {
}

LIR_OpBranch::LIR_OpBranch(BlockBegin* block) 
  : LIR_Op(lir_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*) NULL)
,_condition(lir_cond_always)
,_type(T_ILLEGAL)
  , _label(block->label()), _block(block), _ublock(NULL), _stub(NULL) { 
}

LIR_OpBranch::LIR_OpBranch(CodeStub *stub) 
  : LIR_Op(lir_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*) NULL)
,_condition(lir_cond_always)
,_type(T_ILLEGAL)
  , _label(stub->entry()), _block(NULL), _ublock(NULL), _stub(stub) { 
}

LIR_OpBranch::LIR_OpBranch(LIR_Condition cond, BasicType type, BlockBegin* block, BlockBegin* ublock)
  : LIR_Op(lir_cond_float_branch, LIR_OprFact::illegalOpr, (CodeEmitInfo*)NULL)
,_condition(cond)
  , _type(type)
  , _label(block->label())
  , _block(block)
  , _ublock(ublock)
  , _stub(NULL)
{
}

#endif // AZ_X86

void LIR_OpBranch::change_block(BlockBegin* b) {
  assert(_block != NULL, "must have old block"); 
  assert(_block->label() == label(), "must be equal");
  
  _block = b; 
  _label = b->label();
}

void LIR_OpBranch::change_ublock(BlockBegin* b) {
  assert(_ublock != NULL, "must have old block"); 
  _ublock = b; 
}

void LIR_OpBranch::negate_cond() {
switch(_condition){
case lir_cond_equal:_condition=lir_cond_notEqual;break;
case lir_cond_notEqual:_condition=lir_cond_equal;break;
case lir_cond_less:_condition=lir_cond_greaterEqual;break;
case lir_cond_lessEqual:_condition=lir_cond_greater;break;
case lir_cond_greaterEqual:_condition=lir_cond_less;break;
case lir_cond_greater:_condition=lir_cond_lessEqual;break;
    default: ShouldNotReachHere();
  }
}


LIR_OpTypeCheck::LIR_OpTypeCheck(LIR_Code code, LIR_Opr result, LIR_Opr object, ciKlass* klass,
                                 LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5,
                                 bool fast_check, CodeEmitInfo* info_for_exception, CodeEmitInfo* info_for_patch,
                                 CodeStub* stub,
                                 ciMethod* profiled_method,
                                 int profiled_bci, LIR_Opr cp_reg, int cpdoff)
  : LIR_Op(code, result, NULL)
  , _object(object)
  , _array(LIR_OprFact::illegalOpr)
  , _klass(klass)
  , _tmp1(tmp1)
  , _tmp2(tmp2)
  , _tmp3(tmp3)
,_tmp4(tmp4)
  , _tmp5(tmp5)
  , _cp_reg(cp_reg)
  , _cpdoff(cpdoff)
  , _fast_check(fast_check)
  , _stub(stub)
  , _info_for_patch(info_for_patch)
  , _info_for_exception(info_for_exception)
  , _profiled_method(profiled_method)
  , _profiled_bci(profiled_bci) {
  if (code == lir_checkcast) {
    assert(info_for_exception != NULL, "checkcast throws exceptions");
  } else if (code == lir_instanceof) {
    assert(info_for_exception == NULL, "instanceof throws no exceptions");
  } else {
    ShouldNotReachHere();
  }
}



LIR_OpTypeCheck::LIR_OpTypeCheck(LIR_Code code, LIR_Opr object, LIR_Opr array, LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5, CodeEmitInfo* info_for_exception, ciMethod* profiled_method, int profiled_bci, LIR_Opr cp_reg, int cpdoff)
  : LIR_Op(code, LIR_OprFact::illegalOpr, NULL)
  , _object(object)
  , _array(array)
  , _klass(NULL)
  , _tmp1(tmp1)
  , _tmp2(tmp2)
  , _tmp3(tmp3)
,_tmp4(tmp4)
  , _tmp5(tmp5)
  , _cp_reg(cp_reg)
  , _cpdoff(cpdoff)
  , _fast_check(false)
  , _stub(NULL)
  , _info_for_patch(NULL)
  , _info_for_exception(info_for_exception)
  , _profiled_method(profiled_method)
  , _profiled_bci(profiled_bci) {
  if (code == lir_store_check) {
    _stub = new ArrayStoreExceptionStub(info_for_exception);
    assert(info_for_exception != NULL, "store_check throws exceptions");
  } else {
    ShouldNotReachHere();
  }
}


LIR_OpAlloc::LIR_OpAlloc(LIR_Opr bytesize, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, BasicType type, CodeStub* stub)
  : LIR_Op(lir_alloc_array, result, NULL), 
    _bytesize(bytesize), 
    _len(len),
    _kid(kid),
    _tmp1(tmp1),
    _klass_reg(klass_reg),
_type(type),
    _init_test(false) ,
    _always_slow_path(false),
    _klass(ciTypeArrayKlass::make(type)),
    _stub(stub) {
}

LIR_OpAlloc::LIR_OpAlloc(LIR_Opr bytesize, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, ciType *elem_type, bool always_slow_path, CodeStub* stub)
  : LIR_Op(lir_alloc_array, result, NULL), 
    _bytesize(bytesize), 
    _len(len),
    _kid(kid),
    _tmp1(tmp1),
    _klass_reg(klass_reg),
_type(T_OBJECT),
    _init_test(false) ,
    _always_slow_path(always_slow_path),
    _klass(ciArrayKlass::make(elem_type)),
    _stub(stub) {
}


LIR_OpArrayCopy::LIR_OpArrayCopy(LIR_Opr src, LIR_Opr src_pos, LIR_Opr dst, LIR_Opr dst_pos, LIR_Opr length,
                                 LIR_Opr tmp, ciArrayKlass* expected_type, int flags, CodeEmitInfo* info)
  : LIR_Op(lir_arraycopy, LIR_OprFact::illegalOpr, info)
  , _tmp(tmp)
  , _src(src)
  , _src_pos(src_pos)
  , _dst(dst)
  , _dst_pos(dst_pos)
  , _flags(flags)
  , _expected_type(expected_type)
  , _length(length) {
}

LIR_OpStringEquals::LIR_OpStringEquals(LIR_Opr this_string, LIR_Opr other_string, LIR_Opr res,
                                       LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3,
                                       LIR_Opr tmp4, LIR_Opr tmp5, LIR_Opr tmp6, CodeEmitInfo* info)
:LIR_Op(lir_stringequals,LIR_OprFact::illegalOpr,info)
  , _this_string(this_string)
  , _other_string(other_string)
  , _res(res)
  , _tmp1(tmp1)
  , _tmp2(tmp2)
  , _tmp3(tmp3)
,_tmp4(tmp4)
  , _tmp5(tmp5)
  , _tmp6(tmp6) {
}

//-------------------verify--------------------------

void LIR_Op1::verify() const {
  switch(code()) {
  case lir_move:
    assert(in_opr()->is_valid() && result_opr()->is_valid(), "must be");
    break;
  case lir_null_check:
    assert(in_opr()->is_register(), "must be");
    break;
  case lir_return:
    assert(in_opr()->is_register() || in_opr()->is_illegal(), "must be");
    break;
  }
}

void LIR_OpRTCall::verify() const {
  assert(strcmp(Runtime1::name_for_address(addr()), "<unknown function>") != 0, "unknown function");
}

//-------------------visits--------------------------

// complete rework of LIR instruction visitor.
// The virtual calls for each instruction type is replaced by a big
// switch that adds the operands for each instruction

void LIR_OpVisitState::visit(LIR_Op* op) {
  // copy information from the LIR_Op
  reset();
  set_op(op);

  switch (op->code()) {

// LIR_Op0
    case lir_word_align:               // result and info always invalid
    case lir_backwardbranch_target:    // result and info always invalid
    case lir_build_frame:              // result and info always invalid
    case lir_breakpoint:               // result and info always invalid
    case lir_membar:                   // result and info always invalid
    case lir_membar_acquire:           // result and info always invalid
    case lir_membar_release:           // result and info always invalid
    { 
      assert(op->as_Op0() != NULL, "must be");
      assert(op->_info == NULL, "info not used by this instruction");
      assert(op->_result->is_illegal(), "not used");
      assert(!op->as_Op0()->_tmp1->is_valid(), "operation doesn't require temps")
      break;
    }

    case lir_nop:                      // may have info, result always invalid
    case lir_std_entry:                // may have result, info always invalid
    case lir_osr_entry:                // may have result, info always invalid 
    case lir_get_thread:               // may have result, info always invalid
    {
      assert(op->as_Op0() != NULL, "must be");
      assert(!op->as_Op0()->_tmp1->is_valid(), "operation doesn't require temps")
      if (op->_info != NULL)           do_info(op->_info);
      if (op->_result->is_valid())     do_output(op->_result);
      break;
    }

// LIR_OpLabel
    case lir_label:                    // result and info always invalid
    { 
      assert(op->as_OpLabel() != NULL, "must be");
      assert(op->_info == NULL, "info not used by this instruction");
      assert(op->_result->is_illegal(), "not used");
      break;
    }


// LIR_Op1
    case lir_move:           // input and result always valid, may have info
    {
      assert(op->as_Op1() != NULL, "must be");
      LIR_Op1* op1 = (LIR_Op1*)op;

      if (op1->_info)                  do_info(op1->_info);
      if (op1->_opr->is_valid()) {
do_input(op1->_opr);
        if (MultiMapMetaData && op1->_opr->is_pointer()) {
          LIR_Address* address = op1->_opr->as_address_ptr();
if(address!=NULL){
            // ensure base isn't shared with a temp
            if (address->_base->is_valid()) do_temp(address->_base);
          }
	}
      }
      if (op1->_result->is_valid()) {
do_output(op1->_result);
        if (MultiMapMetaData && op1->_result->is_pointer()) {
          LIR_Address* address = op1->_result->as_address_ptr();
if(address!=NULL){
            // ensure base isn't shared with a temp
            if (address->_base->is_valid()) do_temp(address->_base);
          }
	}
      }
      if (op1->_tmp1->is_valid()) {
        do_temp(op1->_tmp1);
        if (op1->_tmp2->is_valid()) {
          do_temp(op1->_tmp2);
          if (op1->_tmp3->is_valid()) {
            do_temp(op1->_tmp3);
          }
        }
      }
      break;
    }
    case lir_klassTable_oop_load: {
      assert(op->as_Op1() != NULL, "must be");
      LIR_Op1* op1 = (LIR_Op1*)op;

      if (op1->_info)                          do_info(op1->_info);
      assert(op1->_opr   ->is_valid(), "used") do_input(op1->_opr);
      assert(op1->_result->is_valid(), "used") do_output(op1->_result);
      assert(op1->_tmp1  ->is_valid(), "used") do_temp(op1->_tmp1);
      assert(!op1->_tmp2->is_valid() && !op1->_tmp3->is_valid(), "operation doesn't require temps");
      break;
    }

    case lir_push:           // input always valid, result and info always invalid
    case lir_pop:            // input always valid, result and info always invalid
    case lir_return:         // input always valid, result and info always invalid
    case lir_leal:           // input and result always valid, info always invalid
    case lir_neg:            // input and result always valid, info always invalid
    case lir_bit_test:
    case lir_monaddr:        // input and result always valid, info always invalid
    case lir_null_check:     // input and info always valid, result always invalid
    case lir_prefetchr:      // input always valid, result and info always invalid
    case lir_prefetchw:      // input always valid, result and info always invalid
    {
      assert(op->as_Op1() != NULL, "must be");
      LIR_Op1* op1 = (LIR_Op1*)op;

      if (op1->_info)                  do_info(op1->_info);
      if (op1->_opr->is_valid())       do_input(op1->_opr);
      if (op1->_result->is_valid())    do_output(op1->_result);
      assert(!op1->_tmp1->is_valid() && !op1->_tmp2->is_valid() && !op1->_tmp3->is_valid(), "operation doesn't require temps")
      break;
    }

    case lir_safepoint:
    {
assert(op->as_OpSafepoint()!=NULL,"must be");
      LIR_OpSafepoint* ops = (LIR_OpSafepoint*)op;

      assert(ops->_opr->is_valid(), "needed"); do_input(ops->_opr); // safepoint uses thread virtual register
do_stub(ops->_stub);
      break;
    }

// LIR_OpConvert;
    case lir_convert:        // input and result always valid, info always invalid
    {
      assert(op->as_OpConvert() != NULL, "must be");
      LIR_OpConvert* opConvert = (LIR_OpConvert*)op;

      assert(opConvert->_info == NULL, "must be");
      if (opConvert->_opr->is_valid())       do_input(opConvert->_opr);
      if (opConvert->_result->is_valid())    do_output(opConvert->_result);
      do_stub(opConvert->_stub);
      if (opConvert->_tmp1->is_valid()) {
        do_temp(opConvert->_tmp1);
      }
      assert(!opConvert->_tmp2->is_valid() && !opConvert->_tmp3->is_valid(), "operation doesn't require temps");
      break;
    }

// LIR_OpBranch;
    case lir_branch:                   // may have info, input and result register always invalid
    case lir_cond_float_branch:        // may have info, input and result register always invalid
    { 
      assert(op->as_OpBranch() != NULL, "must be");
      LIR_OpBranch* opBranch = (LIR_OpBranch*)op;

      if (opBranch->_info != NULL)     do_info(opBranch->_info);
      assert(opBranch->_result->is_illegal(), "not used");
if(opBranch->stub()!=NULL)opBranch->stub()->visit(this);

      break;
    }


// LIR_OpAlloc
    case lir_alloc_array:
    case lir_alloc_object: 
    {
assert(op->as_OpAlloc()!=NULL,"must be");
      LIR_OpAlloc* opAlloc = (LIR_OpAlloc*)op;

if(opAlloc->_info)do_info(opAlloc->_info);
      if (opAlloc->_klass_reg->is_valid())    do_input (opAlloc->_klass_reg);
      if (opAlloc->_bytesize->is_valid())     do_temp  (opAlloc->_bytesize);
      if (opAlloc->_len ->is_valid())         do_temp  (opAlloc->_len );
      if (opAlloc->_kid ->is_valid())         do_temp  (opAlloc->_kid );
if(opAlloc->_tmp1->is_valid())do_temp(opAlloc->_tmp1);
if(opAlloc->_result->is_valid())do_output(opAlloc->_result);
do_stub(opAlloc->_stub);
      break;
    }

// LIR_Op2
    case lir_cmp:
    case lir_cmp_l2i:
    case lir_ucmp_fd2i:
    case lir_cmp_fd2i:
    case lir_add:
    case lir_sub:
    case lir_mul:
    case lir_div:
    case lir_rem:
    case lir_sqrt:
    case lir_abs:
    case lir_log:
    case lir_log10:
    case lir_logic_and:
    case lir_logic_or:
    case lir_logic_xor:
    case lir_shl:
    case lir_shr:
    case lir_ushr:
    {
      assert(op->as_Op2() != NULL, "must be");
      LIR_Op2* op2 = (LIR_Op2*)op;

      if (op2->_info)                     do_info(op2->_info);
      if (op2->_opr1->is_valid())         do_input(op2->_opr1);
      if (op2->_opr2->is_valid())         do_input(op2->_opr2);
      if (op2->_tmp->is_valid())          do_temp(op2->_tmp);
      if (op2->_result->is_valid())       do_output(op2->_result);

      break;
    }

    // special handling for cmove: right input operand must not be equal 
    // to the result operand, otherwise the backend fails
    case lir_cmove:
    {
      assert(op->as_Op2() != NULL, "must be");
      LIR_Op2* op2 = (LIR_Op2*)op;

      assert(op2->_info == NULL, "not used");
      assert(op2->_opr1->is_valid() && op2->_opr2->is_valid() && op2->_result->is_valid(), "used");

      do_input(op2->_opr1);
      do_input(op2->_opr2);
      do_temp(op2->_opr2);
      do_output(op2->_result);
      if(op2->_tmp->is_valid()) do_temp(op2->_tmp);
      break;
    }

    // vspecial handling for strict operations: register input operands
    // as temp to guarantee that they do not overlap with other
    // registers
    case lir_mul_strictfp:
    case lir_div_strictfp:
    {
      assert(op->as_Op2() != NULL, "must be");
      LIR_Op2* op2 = (LIR_Op2*)op;

      assert(op2->_info == NULL, "not used");
      assert(op2->_opr1->is_valid(), "used");
      assert(op2->_opr2->is_valid(), "used");
      assert(op2->_result->is_valid(), "used");

      do_input(op2->_opr1); do_temp(op2->_opr1);
      do_input(op2->_opr2); do_temp(op2->_opr2);
      if (op2->_tmp->is_valid()) do_temp(op2->_tmp);
      do_output(op2->_result);

      break;
    }

    case lir_throw:
    case lir_unwind: {
      assert(op->as_Op2() != NULL, "must be");
      LIR_Op2* op2 = (LIR_Op2*)op;

      if (op2->_info)                         do_info(op2->_info);
assert(op2->_opr1->is_valid(),"used");do_input(op2->_opr1);//thread register
assert(op2->_opr2->is_valid(),"used");do_input(op2->_opr2);//exception object is input parameter
      if( op2->_tmp->is_valid() )             do_temp(op2->_tmp);   
      assert(op2->_result->is_illegal(), "no result");

      break;
    }


    case lir_tan:
    case lir_sin:
    case lir_cos: {
      assert(op->as_Op2() != NULL, "must be");
      LIR_Op2* op2 = (LIR_Op2*)op;

      // sin and cos need two temporary fpu stack slots, so register
      // two temp operands.  Register input operand as temp to
      // guarantee that they do not overlap
      assert(op2->_info == NULL, "not used");
      assert(op2->_opr1->is_valid(), "used");
      do_input(op2->_opr1); do_temp(op2->_opr1);

      if (op2->_opr2->is_valid())         do_temp(op2->_opr2);
      if (op2->_tmp->is_valid())          do_temp(op2->_tmp);
      if (op2->_result->is_valid())       do_output(op2->_result);

      break;
    }


// LIR_Op3
    case lir_ldiv:
    case lir_lrem:
    case lir_idiv:
    case lir_irem: {
      assert(op->as_Op3() != NULL, "must be");
      LIR_Op3* op3= (LIR_Op3*)op;

      if (op3->_info)                     do_info(op3->_info);
      if (op3->_opr1->is_valid())         do_input(op3->_opr1);

      // second operand is input and temp, so ensure that second operand
      // and third operand get not the same register
      if (op3->_opr2->is_valid())         do_input(op3->_opr2);
      if (op3->_opr2->is_valid())         do_temp(op3->_opr2);
      if (op3->_opr3->is_valid())         do_temp(op3->_opr3);

      if (op3->_result->is_valid())       do_output(op3->_result);

      break;
    }


// LIR_OpJavaCall
    case lir_static_call:
    case lir_optvirtual_call:
    case lir_icvirtual_call:
    case lir_virtual_call: {
      assert(op->as_OpJavaCall() != NULL, "must be");
      LIR_OpJavaCall* opJavaCall = (LIR_OpJavaCall*)op;

      int n = opJavaCall->_arguments->length();
      if (opJavaCall->_receiver->is_valid())     assert(n > 0 && opJavaCall->_receiver == opJavaCall->_arguments->at(0), "receiver should be the same as the first argument");

      // only visit register parameters
      for (int i = 0; i < n; i++) {
        if (!opJavaCall->_arguments->at(i)->is_pointer()) {
          do_input(*opJavaCall->_arguments->adr_at(i));
        }
      }

      if (opJavaCall->_info)                     do_info(opJavaCall->_info);
      do_call();
      if (opJavaCall->_result->is_valid())       do_output(opJavaCall->_result);

      break;
    }


// LIR_OpRTCall
    case lir_rtcall: {
      assert(op->as_OpRTCall() != NULL, "must be");
      LIR_OpRTCall* opRTCall = (LIR_OpRTCall*)op;

      // only visit register parameters
      int n = opRTCall->_arguments->length();
      for (int i = 0; i < n; i++) {
        if (!opRTCall->_arguments->at(i)->is_pointer()) {
          do_input(*opRTCall->_arguments->adr_at(i));
        }
      }
      if (opRTCall->_info)                     do_info(opRTCall->_info);
      do_call();
      if (opRTCall->_result->is_valid())       do_output(opRTCall->_result);

      break;
    }


// LIR_OpArrayCopy
    case lir_arraycopy: {
      assert(op->as_OpArrayCopy() != NULL, "must be");
      LIR_OpArrayCopy* opArrayCopy = (LIR_OpArrayCopy*)op;

      assert(opArrayCopy->_result ->is_illegal(), "unused");
assert(opArrayCopy->_src->is_valid(),"used");do_input(opArrayCopy->_src);
assert(opArrayCopy->_src_pos->is_valid(),"used");do_input(opArrayCopy->_src_pos);
assert(opArrayCopy->_dst->is_valid(),"used");do_input(opArrayCopy->_dst);
assert(opArrayCopy->_dst_pos->is_valid(),"used");do_input(opArrayCopy->_dst_pos);
assert(opArrayCopy->_length->is_valid(),"used");do_input(opArrayCopy->_length);
      if (opArrayCopy->_info)                            do_info(opArrayCopy->_info    );
      // the implementation of arraycopy always has a call into the runtime
                                                         do_call();
      break;
    }

// LIR_OpStringEquals
    case lir_stringequals: {
assert(op->as_OpStringEquals()!=NULL,"must be");
      LIR_OpStringEquals* opStringEquals = (LIR_OpStringEquals*)op;

      assert(opStringEquals->_this_string ->is_valid(), "used"); { do_input (opStringEquals->_this_string ); do_temp  (opStringEquals->_this_string);  }
      assert(opStringEquals->_other_string->is_valid(), "used"); { do_input (opStringEquals->_other_string); do_temp  (opStringEquals->_other_string); }
assert(opStringEquals->_tmp1->is_valid(),"used");do_temp(opStringEquals->_tmp1);
      assert(opStringEquals->_tmp2        ->is_valid(), "used");   do_temp  (opStringEquals->_tmp2        );
      assert(opStringEquals->_tmp3        ->is_valid(), "used");   do_temp  (opStringEquals->_tmp3        );
      assert(opStringEquals->_tmp4        ->is_valid(), "used");   do_temp  (opStringEquals->_tmp4        );
      assert(opStringEquals->_tmp5        ->is_valid(), "used");   do_temp  (opStringEquals->_tmp5        );
      assert(opStringEquals->_tmp6        ->is_valid(), "used");   do_temp  (opStringEquals->_tmp6        );
      assert(opStringEquals->_res         ->is_valid(), "used");   do_output(opStringEquals->_res         );
      assert(opStringEquals->_info                    , "used");   do_info  (opStringEquals->_info        );
      break;
    }

// LIR_OpLock
    case lir_lock:
    case lir_unlock: {
      assert(op->as_OpLock() != NULL, "must be");
      LIR_OpLock* opLock = (LIR_OpLock*)op;

      if (opLock->_info)                          do_info(opLock->_info);

      assert(opLock->_obj->is_valid(),  "used");  do_input(opLock->_obj); do_temp(opLock->_obj);
if(opLock->_tid->is_valid())do_temp(opLock->_tid);
assert(opLock->_mark->is_valid(),"used");do_temp(opLock->_mark);
assert(opLock->_tmp->is_valid(),"used");do_temp(opLock->_tmp);
      assert(opLock->_result->is_illegal(), "unused");

      do_stub(opLock->_stub);

      break;
    }

// LIR_OpTypeCheck
    case lir_instanceof:
    case lir_checkcast:
    case lir_store_check: {
      assert(op->as_OpTypeCheck() != NULL, "must be");
      LIR_OpTypeCheck* opTypeCheck = (LIR_OpTypeCheck*)op;

      if (opTypeCheck->_info_for_exception)       do_info(opTypeCheck->_info_for_exception);
      if (opTypeCheck->_info_for_patch)           do_info(opTypeCheck->_info_for_patch);
if(opTypeCheck->_object->is_valid()){do_input(opTypeCheck->_object);do_temp(opTypeCheck->_object);}
if(opTypeCheck->_array->is_valid()){do_input(opTypeCheck->_array);do_temp(opTypeCheck->_array);}
if(opTypeCheck->_cp_reg->is_valid())do_input(opTypeCheck->_cp_reg);
      if (opTypeCheck->_tmp1->is_valid())         do_temp(opTypeCheck->_tmp1);
      if (opTypeCheck->_tmp2->is_valid())         do_temp(opTypeCheck->_tmp2);
      if (opTypeCheck->_tmp3->is_valid())         do_temp(opTypeCheck->_tmp3);
if(opTypeCheck->_tmp4->is_valid())do_temp(opTypeCheck->_tmp4);
if(opTypeCheck->_tmp5->is_valid())do_temp(opTypeCheck->_tmp5);
      if (opTypeCheck->_result->is_valid())       do_output(opTypeCheck->_result);
                                                  do_stub(opTypeCheck->_stub);
      break;
    }

// LIR_OpCompareAndSwap
    case lir_cas_long:
    case lir_cas_obj:
    case lir_cas_int: {
      assert(op->as_OpCompareAndSwap() != NULL, "must be");
      LIR_OpCompareAndSwap* opCompareAndSwap = (LIR_OpCompareAndSwap*)op;

      if(opCompareAndSwap->_info)                  do_info(opCompareAndSwap->_info);
if(opCompareAndSwap->_obj->is_valid()){do_input(opCompareAndSwap->_obj);do_temp(opCompareAndSwap->_obj);}
      if(opCompareAndSwap->_offset->is_valid())    {do_input(opCompareAndSwap->_offset   );do_temp(opCompareAndSwap->_offset   );}
      if(opCompareAndSwap->_cmp_value->is_valid()) {do_input(opCompareAndSwap->_cmp_value);do_temp(opCompareAndSwap->_cmp_value);}
      if(opCompareAndSwap->_new_value->is_valid()) {do_input(opCompareAndSwap->_new_value);do_temp(opCompareAndSwap->_new_value);}
      if(opCompareAndSwap->_tmp1->is_valid())      do_temp(opCompareAndSwap->_tmp1);
      if(opCompareAndSwap->_tmp2->is_valid())      do_temp(opCompareAndSwap->_tmp2);
      if(opCompareAndSwap->_result->is_valid())    do_output(opCompareAndSwap->_result);

      break;
    }


// LIR_OpProfileCall:
    case lir_profile_call: {
      assert(op->as_OpProfileCall() != NULL, "must be");
      LIR_OpProfileCall* opProfileCall = (LIR_OpProfileCall*)op;

      if (opProfileCall->_recv->is_valid())              do_temp(opProfileCall->_recv);
      assert(opProfileCall->_mdo->is_valid(), "used");   do_temp(opProfileCall->_mdo);
      assert(opProfileCall->_tmp1->is_valid(), "used");  do_temp(opProfileCall->_tmp1);
      break;
    }

  default:
    ShouldNotReachHere();
  }
}


void LIR_OpVisitState::do_stub(CodeStub* stub) {
  if (stub != NULL) {
    stub->visit(this);
  }
}

XHandlers* LIR_OpVisitState::all_xhandler() {
  XHandlers* result = NULL;

  int i;
  for (i = 0; i < info_count(); i++) {
    if (info_at(i)->exception_handlers() != NULL) {
      result = info_at(i)->exception_handlers();
      break;
    }
  }

#ifdef ASSERT
  for (i = 0; i < info_count(); i++) {
    assert(info_at(i)->exception_handlers() == NULL ||
           info_at(i)->exception_handlers() == result,
           "only one xhandler list allowed per LIR-operation");
  }
#endif

  if (result != NULL) {
    return result;
  } else {
    return new XHandlers();
  }

  return result;
}


#ifdef ASSERT
bool LIR_OpVisitState::no_operands(LIR_Op* op) {
  visit(op);

  return opr_count(inputMode) == 0 && 
         opr_count(outputMode) == 0 && 
         opr_count(tempMode) == 0 && 
         info_count() == 0 && 
         !has_call() && 
         !has_slow_case();
}
#endif

//---------------------------------------------------


void LIR_OpJavaCall::emit_code(LIR_Assembler* masm) {
  masm->emit_call(this);
}

void LIR_OpRTCall::emit_code(LIR_Assembler* masm) {
  masm->emit_rtcall(this);
}

void LIR_OpLabel::emit_code(LIR_Assembler* masm) {
  masm->emit_opLabel(this); 
}

void LIR_OpArrayCopy::emit_code(LIR_Assembler* masm) {
  masm->emit_arraycopy(this);
}

void LIR_OpStringEquals::emit_code(LIR_Assembler*masm){
masm->emit_stringequals(this);
}

void LIR_Op0::emit_code(LIR_Assembler* masm) {
  masm->emit_op0(this); 
}

void LIR_Op1::emit_code(LIR_Assembler* masm) {
  masm->emit_op1(this); 
}

void LIR_OpAlloc::emit_code(LIR_Assembler*masm){
  if( code() == lir_alloc_object ) masm->emit_alloc_obj  (this); 
  else                             masm->emit_alloc_array(this); 
  masm->emit_code_stub(stub());
}

void LIR_OpBranch::emit_code(LIR_Assembler* masm) {
  masm->emit_opBranch(this); 
  if (stub()) {
    masm->emit_code_stub(stub());
  }
}

void LIR_OpConvert::emit_code(LIR_Assembler* masm) {
  masm->emit_opConvert(this); 
  if (stub() != NULL) {
    masm->emit_code_stub(stub());
  }
}

void LIR_OpSafepoint::emit_code(LIR_Assembler*masm){
  masm->safepoint_poll(in_opr(), stub()); 
  masm->emit_code_stub(stub());
}

void LIR_Op2::emit_code(LIR_Assembler* masm) {
  masm->emit_op2(this); 
}

void LIR_OpTypeCheck::emit_code(LIR_Assembler* masm) {
  masm->emit_opTypeCheck(this); 
  if (stub()) {
    masm->emit_code_stub(stub());
  }
}

void LIR_OpCompareAndSwap::emit_code(LIR_Assembler* masm) {
  masm->emit_compare_and_swap(this);
}

void LIR_Op3::emit_code(LIR_Assembler* masm) {
  masm->emit_op3(this); 
}

void LIR_OpLock::emit_code(LIR_Assembler* masm) {
  masm->emit_lock(this);
  if (stub()) {
    masm->emit_code_stub(stub());
  }
}


void LIR_OpProfileCall::emit_code(LIR_Assembler* masm) {
  masm->emit_profile_call(this);
}


// LIR_List
LIR_List::LIR_List(Compilation* compilation, BlockBegin* block) 
  : _operations(8)
  , _compilation(compilation)
#ifndef PRODUCT
  , _block(block)
#endif
#ifdef ASSERT
  , _file(NULL)
  , _line(0)
#endif
{ }


#ifdef ASSERT
void LIR_List::set_file_and_line(const char * file, int line) {
  const char * f = strrchr(file, '/');
  if (f == NULL) f = strrchr(file, '\\');
  if (f == NULL) {
    f = file;
  } else {
    f++;
  }
  _file = f;
  _line = line;
}
#endif


void LIR_List::append(LIR_InsertionBuffer* buffer) {
  assert(this == buffer->lir_list(), "wrong lir list");
  const int n = _operations.length();
  
  if (buffer->number_of_ops() > 0) {
    // increase size of instructions list
    _operations.at_grow(n + buffer->number_of_ops() - 1, NULL);
    // insert ops from buffer into instructions list
    int op_index = buffer->number_of_ops() - 1;
    int ip_index = buffer->number_of_insertion_points() - 1;
    int from_index = n - 1;
    int to_index = _operations.length() - 1;
    for (; ip_index >= 0; ip_index --) {
      int index = buffer->index_at(ip_index);
      // make room after insertion point
      while (index < from_index) {
        _operations.at_put(to_index --, _operations.at(from_index --));
      }
      // insert ops from buffer
      for (int i = buffer->count_at(ip_index); i > 0; i --) {
        _operations.at_put(to_index --, buffer->op_at(op_index --));
      }
    }
  }

  buffer->finish();
}


void LIR_List::oop2reg_patch(jobject o, LIR_Opr reg, CodeEmitInfo* info) {
  append(new LIR_Op1(lir_move, LIR_OprFact::oopConst(o),  reg, T_OBJECT, lir_patch_normal, info));
}


void LIR_List::load(LIR_Address* addr, LIR_Opr src, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move, 
            LIR_OprFact::address(addr),
            src,
            addr->type(),
            patch_code, 
            info));
}


void LIR_List::volatile_load_mem_reg(LIR_Address* address, LIR_Opr dst, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move, 
            LIR_OprFact::address(address),
            dst,
            address->type(),
            patch_code, 
            info, lir_move_volatile));
}

void LIR_List::volatile_load_unsafe_reg(LIR_Opr base, LIR_Opr offset, LIR_Opr dst, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move, 
            LIR_OprFact::address(new LIR_Address(base, offset, type)), 
            dst,
            type,
            patch_code, 
            info, lir_move_volatile));
}


void LIR_List::prefetch(LIR_Address* addr, bool is_store) {
  append(new LIR_Op1(
            is_store ? lir_prefetchw : lir_prefetchr,
            LIR_OprFact::address(addr)));
}


void LIR_List::store_mem_int(jint v, LIR_Opr base, int offset_in_bytes, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code) {
Unimplemented();//Need to pass temps down
  append(new LIR_Op1(
            lir_move, 
            LIR_OprFact::intConst(v),
            LIR_OprFact::address(new LIR_Address(base, offset_in_bytes, type)),
            type,
            patch_code, 
            info));
}


void LIR_List::store_mem_oop(jobject o, LIR_Opr base, int offset_in_bytes, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code) {
Unimplemented();//Need to pass temps down
  append(new LIR_Op1(
            lir_move, 
            LIR_OprFact::oopConst(o),
            LIR_OprFact::address(new LIR_Address(base, offset_in_bytes, type)),
            type,
            patch_code, 
            info));
}


void LIR_List::store(LIR_Opr src, LIR_Address* addr, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move, 
            src,
            LIR_OprFact::address(addr),
            addr->type(),
            patch_code, 
            info));
}


void LIR_List::volatile_store_mem_reg(LIR_Opr src, LIR_Address* addr, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move,
            src,
            LIR_OprFact::address(addr),
            addr->type(),
            patch_code,
            info,
            lir_move_volatile));
}

void LIR_List::volatile_store_unsafe_reg(LIR_Opr src, LIR_Opr base, LIR_Opr offset, BasicType type, CodeEmitInfo* info, LIR_PatchCode patch_code) {
  append(new LIR_Op1(
            lir_move,
            src,
            LIR_OprFact::address(new LIR_Address(base, offset, type)),
            type,
            patch_code,
            info, lir_move_volatile));
}


void LIR_List::idiv(LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info) {
  append(new LIR_Op3(
                    lir_idiv,
                    left,
                    right,
                    tmp,
                    res,
                    info));
}


void LIR_List::idiv(LIR_Opr left, int right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info) {
  append(new LIR_Op3(
                    lir_idiv,
                    left,
                    LIR_OprFact::intConst(right),
                    tmp,
                    res,
                    info));
}


void LIR_List::ldiv(LIR_Opr left,LIR_Opr right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info){
  append(new LIR_Op3(
                    lir_ldiv,
                    left,
                    right,
                    tmp,
                    res,
                    info));
}


void LIR_List::ldiv(LIR_Opr left,int right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info){
  append(new LIR_Op3(
                    lir_ldiv,
                    left,
                    LIR_OprFact::intConst(right),
                    tmp,
                    res,
                    info));
}

void LIR_List::irem(LIR_Opr left, LIR_Opr right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info) {
  append(new LIR_Op3(
                    lir_irem,
                    left,
                    right,
                    tmp,
                    res,
                    info));
}


void LIR_List::irem(LIR_Opr left, int right, LIR_Opr res, LIR_Opr tmp, CodeEmitInfo* info) {
  append(new LIR_Op3(
                    lir_irem,
                    left,
                    LIR_OprFact::intConst(right),
                    tmp,
                    res,
                    info));
}


void LIR_List::lrem(LIR_Opr left,LIR_Opr right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info){
  append(new LIR_Op3(
                    lir_lrem,
                    left,
                    right,
                    tmp,
                    res,
                    info));
}


void LIR_List::lrem(LIR_Opr left,int right,LIR_Opr res,LIR_Opr tmp,CodeEmitInfo*info){
  append(new LIR_Op3(
                    lir_lrem,
                    left,
                    LIR_OprFact::intConst(right),
                    tmp,
                    res,
                    info));
}


void LIR_List::cmp_mem_int(LIR_Condition condition, LIR_Opr base, int disp, int c, CodeEmitInfo* info) { 
  append(new LIR_Op2(
                    lir_cmp,
                    condition,
                    LIR_OprFact::address(new LIR_Address(base, disp, T_INT)), 
                    LIR_OprFact::intConst(c), 
                    info)); 
}


void LIR_List::cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Address* addr, CodeEmitInfo* info) { 
  append(new LIR_Op2(
                    lir_cmp, 
                    condition,
                    reg,
                    LIR_OprFact::address(addr),
                    info));
}


void LIR_List::allocate_object(ciInstanceKlass *ik, LIR_Opr size, LIR_Opr len_is_zero, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, bool init_test, bool always_slow_path, CodeStub* stub) {
  append(new LIR_OpAlloc(ik,size,len_is_zero,kid,tmp1,result,klass_reg,init_test,always_slow_path,stub));
}

void LIR_List::allocate_prim_array(LIR_Opr size, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, BasicType type, CodeStub* stub) {
  append(new LIR_OpAlloc(size,len,kid,tmp1,result,klass_reg,type,stub));
}

void LIR_List::allocate_obj_array(LIR_Opr size, LIR_Opr len, LIR_Opr kid, LIR_Opr tmp1, LIR_Opr result, LIR_Opr klass_reg, ciType *elem_type, bool always_slow_path, CodeStub* stub) {
  append(new LIR_OpAlloc(size,len,kid,tmp1,result,klass_reg,elem_type,always_slow_path,stub));
}

void LIR_List::shift_left(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp) {
 append(new LIR_Op2(
                    lir_shl, 
                    value, 
                    count, 
                    dst,
                    tmp));
}

void LIR_List::shift_right(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp) {
 append(new LIR_Op2(
                    lir_shr, 
                    value, 
                    count, 
                    dst,
                    tmp));
}


void LIR_List::unsigned_shift_right(LIR_Opr value, LIR_Opr count, LIR_Opr dst, LIR_Opr tmp) {
 append(new LIR_Op2(
                    lir_ushr, 
                    value, 
                    count, 
                    dst,
                    tmp));
}

void LIR_List::bit_test(LIR_Opr value, LIR_Opr bitnum) {
 append(new LIR_Op1(lir_bit_test, value, bitnum));
}

void LIR_List::fcmp2int(LIR_Opr left, LIR_Opr right, LIR_Opr dst, bool is_unordered_less) {
  append(new LIR_Op2(is_unordered_less ? lir_ucmp_fd2i : lir_cmp_fd2i,  
                     left,
                     right,
                     dst));
}

void LIR_List::lock_object(LIR_Opr obj, LIR_Opr tid, LIR_Opr mark, LIR_Opr tmp, int mon_num, CodeStub* stub, CodeEmitInfo* info) {
  append(new LIR_OpLock(
                    lir_lock, 
                    obj,
                    tid,
		    mark,
		    tmp,
                    mon_num,
                    stub,
                    info));
}

void LIR_List::unlock_object(LIR_Opr obj, LIR_Opr mark, LIR_Opr tmp, int mon_num, CodeStub* stub) {
  append(new LIR_OpLock(
                    lir_unlock, 
                    obj,
                    LIR_OprFact::illegalOpr,
                    mark,
		    tmp,
                    mon_num,
                    stub,
                    NULL));
}


void check_LIR() {
  // cannot do the proper checking as PRODUCT and other modes return different results
  // guarantee(sizeof(LIR_OprDesc) == wordSize, "may not have a v-table");
}



void LIR_List::checkcast (LIR_Opr result, LIR_Opr object, ciKlass* klass,
                          LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5, bool fast_check,
                          CodeEmitInfo* info_for_exception, CodeEmitInfo* info_for_patch, CodeStub* stub,
                          ciMethod* profiled_method, int profiled_bci, LIR_Opr cp_reg, int cpdoff) {
  append(new LIR_OpTypeCheck(lir_checkcast, result, object, klass,
tmp1,tmp2,tmp3,tmp4,tmp5,fast_check,info_for_exception,info_for_patch,stub,
profiled_method,profiled_bci,cp_reg,cpdoff));
}


void LIR_List::instanceof(LIR_Opr result,LIR_Opr object,ciKlass*klass,
                          LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5, bool fast_check,
                          CodeEmitInfo* info_for_patch, LIR_Opr cp_reg, int cpdoff) {
append(new LIR_OpTypeCheck(lir_instanceof,result,object,klass,
                             tmp1, tmp2, tmp3, tmp4, tmp5, fast_check, NULL, info_for_patch, NULL,
                             NULL, 0, cp_reg, cpdoff));
}


void LIR_List::store_check(LIR_Opr object, LIR_Opr array,
                           LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, LIR_Opr tmp4, LIR_Opr tmp5,
                           CodeEmitInfo* info_for_exception, LIR_Opr cp_reg) {
  append(new LIR_OpTypeCheck(lir_store_check, object, array,
                             tmp1, tmp2, tmp3, tmp4, tmp5, info_for_exception,
                             NULL, 0, cp_reg, -1));
}


void LIR_List::cas_long(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2) {
  // Compare and swap produces condition code "zero" if contents_of(addr) == cmp_value,
  // implying successful swap of new_value into addr
append(new LIR_OpCompareAndSwap(lir_cas_long,obj,offset,cmp_value,new_value,t1,t2));
}

void LIR_List::cas_obj(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2) {
  // Compare and swap produces condition code "zero" if contents_of(addr) == cmp_value,
  // implying successful swap of new_value into addr
append(new LIR_OpCompareAndSwap(lir_cas_obj,obj,offset,cmp_value,new_value,t1,t2));
}

void LIR_List::cas_int(LIR_Opr obj, LIR_Opr offset, LIR_Opr cmp_value, LIR_Opr new_value, LIR_Opr t1, LIR_Opr t2) {
  // Compare and swap produces condition code "zero" if contents_of(addr) == cmp_value,
  // implying successful swap of new_value into addr
append(new LIR_OpCompareAndSwap(lir_cas_int,obj,offset,cmp_value,new_value,t1,t2));
}


// GDB convenience method
void print_LIR_Opr(void *opr) {
  ((LIR_Opr)opr)->print();
}

#ifdef PRODUCT

void print_LIR(BlockList* blocks) {
}

#else
// LIR_OprDesc
void LIR_OprDesc::print() const {
print(C1OUT);
}

void LIR_OprDesc::print(outputStream* out) const {
  if (is_illegal()) {
    return;
  }

  out->print("[");
  if (is_pointer()) {
    pointer()->print_value_on(out);
  } else if (is_single_stack()) {
    out->print("stack:%d", single_stack_ix());
  } else if (is_double_stack()) {
    out->print("dbl_stack:%d",double_stack_ix());
  } else if (is_virtual()) {
    out->print("R%d", vreg_number());
  } else if (is_single_cpu()) {
out->print(raw_reg_name(as_register()));
  } else if (is_double_cpu()) {
out->print(raw_reg_name(as_register_hi()));
out->print(raw_reg_name(as_register_lo()));
#ifdef AZ_X86
  } else if (is_single_xmm()) {
out->print(raw_reg_name(as_xmm_float_reg()));
  } else if (is_double_xmm()) {
    out->print(raw_reg_name(as_xmm_double_reg()));
#endif

  } else if (is_illegal()) {
    out->print("-");
  } else {
    out->print("Unknown Operand");
  }
  if (!is_illegal()) {
    out->print("|%c", type_char());
  }
if(is_register()&&is_destroyed()){
out->print("(destroyed)");
  }
  if (is_register() && is_last_use()) {
    out->print("(last_use)");
  }
  out->print("]");
}

// LIR_Address
void LIR_Const::print_value_on(outputStream* out) const {
  switch (type()) {
    case T_INT:    out->print("int:%d",   as_jint());           break;
    case T_LONG:   out->print("lng:%lld", as_jlong());          break;
    case T_FLOAT:  out->print("flt:%f",   as_jfloat());         break;
    case T_DOUBLE: out->print("dbl:%f",   as_jdouble());        break;
case T_OBJECT:out->print("obj:"PTR_FORMAT,as_jobject());break;
default:out->print("%3d:%llx",type(),as_jlong());break;
  }
}

// LIR_Address
void LIR_Address::print_value_on(outputStream* out) const {
  out->print("Base:"); _base->print(out);
  if (!_index->is_illegal()) {
    out->print(" Index:"); _index->print(out);
    switch (scale()) {
    case times_1: break;
    case times_2: out->print(" * 2"); break;
    case times_4: out->print(" * 4"); break;
    case times_8: out->print(" * 8"); break;
    }
  }
out->print(" Disp: %ld",_disp);
}

// debug output of block header without InstructionPrinter
//       (because phi functions are not necessary for LIR)
static void print_block(BlockBegin* x) {
  // print block id
  BlockEnd* end = x->end();
C1OUT->print("B%d ",x->block_id());

  // print flags
if(x->is_set(BlockBegin::std_entry_flag))C1OUT->print("std ");
if(x->is_set(BlockBegin::osr_entry_flag))C1OUT->print("osr ");
if(x->is_set(BlockBegin::exception_entry_flag))C1OUT->print("ex ");
if(x->is_set(BlockBegin::subroutine_entry_flag))C1OUT->print("jsr ");
if(x->is_set(BlockBegin::backward_branch_target_flag))C1OUT->print("bb ");
if(x->is_set(BlockBegin::linear_scan_loop_header_flag))C1OUT->print("lh ");
if(x->is_set(BlockBegin::linear_scan_loop_end_flag))C1OUT->print("le ");

  // print block bci range
C1OUT->print("[%d, %d] ",x->bci(),(end==NULL?InvocationEntryBci:end->bci()));

  // print predecessors and successors
  if (x->number_of_preds() > 0) {
C1OUT->print("preds: ");
    for (int i = 0; i < x->number_of_preds(); i ++) {
C1OUT->print("B%d ",x->pred_at(i)->block_id());
    }
  }

  if (x->number_of_sux() > 0) {
C1OUT->print("sux: ");
    for (int i = 0; i < x->number_of_sux(); i ++) {
C1OUT->print("B%d ",x->sux_at(i)->block_id());
    }
  }

  // print exception handlers
  if (x->number_of_exception_handlers() > 0) {
C1OUT->print("xhandler: ");
    for (int i = 0; i < x->number_of_exception_handlers();  i++) {
C1OUT->print("B%d ",x->exception_handler_at(i)->block_id());
    }
  }

C1OUT->cr();
}

void print_LIR(BlockList* blocks) {
C1OUT->print_cr("LIR:");
  int i;
  for (i = 0; i < blocks->length(); i++) {
    BlockBegin* bb = blocks->at(i);
    print_block(bb);
C1OUT->print("__id_Instruction___________________________________________");C1OUT->cr();
    bb->lir()->print_instructions();
  }
}

void LIR_List::print_instructions() {
  for (int i = 0; i < _operations.length(); i++) {
_operations.at(i)->print(C1OUT);C1OUT->cr();
  }
C1OUT->cr();
}

// LIR_Ops printing routines
// LIR_Op
void LIR_Op::print_on(outputStream* out) const {
  if (id() != -1 || PrintCFGToFile) {
    out->print("%4d ", id());
  } else {
    out->print("     ");
  }
  out->print(name()); out->print(" ");
  print_instr(out);
  if (info() != NULL) out->print(" [bci:%d]", info()->bci());
#ifdef ASSERT
  if (Verbose && _file != NULL) {
    out->print(" (%s:%d)", _file, _line);
  }
#endif
}

const char * LIR_Op::name() const {
  const char* s = NULL;
  switch(code()) {
     // LIR_Op0
     case lir_membar:                s = "membar";        break;
     case lir_membar_acquire:        s = "membar_acquire"; break;
     case lir_membar_release:        s = "membar_release"; break;
     case lir_word_align:            s = "word_align";    break;
     case lir_label:                 s = "label";         break;
     case lir_nop:                   s = "nop";           break;
     case lir_backwardbranch_target: s = "backbranch";    break;
     case lir_std_entry:             s = "std_entry";     break;
     case lir_osr_entry:             s = "osr_entry";     break;
     case lir_build_frame:           s = "build_frm";     break;
     case lir_breakpoint:            s = "breakpoint";    break;
     case lir_get_thread:            s = "get_thread";    break;
     // LIR_Op1
     case lir_push:                  s = "push";          break;
     case lir_pop:                   s = "pop";           break;
     case lir_null_check:            s = "null_check";    break;
     case lir_return:                s = "return";        break;
     case lir_safepoint:             s = "safepoint";     break;
     case lir_neg:                   s = "neg";           break;
case lir_bit_test:s="bit_test";break;
     case lir_leal:                  s = "leal";          break;
     case lir_branch:                s = "branch";        break;
     case lir_cond_float_branch:     s = "flt_cond_br";   break;
     case lir_move:                  s = "move";          break;
     case lir_rtcall:                s = "rtcall";        break;
     case lir_throw:                 s = "throw";         break;
     case lir_unwind:                s = "unwind";        break;
case lir_klassTable_oop_load:s="klassTable_oop_load";break;
     case lir_convert:               s = "convert";       break;
     case lir_alloc_object:          s = "alloc_obj";     break;
     case lir_monaddr:               s = "mon_addr";      break;
     // LIR_Op2
     case lir_cmp:                   s = "cmp";           break;
     case lir_cmp_l2i:               s = "cmp_l2i";       break;
     case lir_ucmp_fd2i:             s = "ucomp_fd2i";    break;
     case lir_cmp_fd2i:              s = "comp_fd2i";     break;
     case lir_cmove:                 s = "cmove";         break;
     case lir_add:                   s = "add";           break;
     case lir_sub:                   s = "sub";           break;
     case lir_mul:                   s = "mul";           break;
     case lir_mul_strictfp:          s = "mul_strictfp";  break;
     case lir_div:                   s = "div";           break;
     case lir_div_strictfp:          s = "div_strictfp";  break;
     case lir_rem:                   s = "rem";           break;
     case lir_abs:                   s = "abs";           break;
     case lir_sqrt:                  s = "sqrt";          break;
     case lir_sin:                   s = "sin";           break;
     case lir_cos:                   s = "cos";           break;
     case lir_tan:                   s = "tan";           break;
     case lir_log:                   s = "log";           break;
     case lir_log10:                 s = "log10";         break;
     case lir_logic_and:             s = "logic_and";     break;
     case lir_logic_or:              s = "logic_or";      break;
     case lir_logic_xor:             s = "logic_xor";     break;
     case lir_shl:                   s = "shift_left";    break;
     case lir_shr:                   s = "shift_right";   break;
     case lir_ushr:                  s = "ushift_right";  break;
     case lir_alloc_array:           s = "alloc_array";   break;
     // LIR_Op3
     case lir_idiv:                  s = "idiv";          break;
     case lir_irem:                  s = "irem";          break;
case lir_ldiv:s="ldiv";break;
case lir_lrem:s="lrem";break;
     // LIR_OpJavaCall
     case lir_static_call:           s = "static";        break;
     case lir_optvirtual_call:       s = "optvirtual";    break;
     case lir_icvirtual_call:        s = "icvirtual";     break;
     case lir_virtual_call:          s = "virtual";       break;
     // LIR_OpArrayCopy
     case lir_arraycopy:             s = "arraycopy";     break;
     // LIR_OpStringEquals
case lir_stringequals:s="stringequals";break;
     // LIR_OpLock
     case lir_lock:                  s = "lock";          break;
     case lir_unlock:                s = "unlock";        break;
     // LIR_OpTypeCheck
     case lir_instanceof:            s = "instanceof";    break;
     case lir_checkcast:             s = "checkcast";     break;
     case lir_store_check:           s = "store_check";   break;
     // LIR_OpCompareAndSwap
     case lir_cas_long:              s = "cas_long";      break;
     case lir_cas_obj:               s = "cas_obj";      break;
     case lir_cas_int:               s = "cas_int";      break;
     // LIR_OpProfileCall
     case lir_profile_call:          s = "profile_call";  break;
case lir_profile_invoke:s="profile_invoke";break;

     case lir_none:                  ShouldNotReachHere();break;
    default:                         s = "illegal_op";    break;
  }
  return s;
}

// LIR_OpJavaCall
void LIR_OpJavaCall::print_instr(outputStream* out) const {
  out->print("call: ");
  out->print("[addr: " PTR_FORMAT "]", address());
  if (receiver()->is_valid()) {
    out->print(" [recv: ");   receiver()->print(out);   out->print("]");
  }
  if (result_opr()->is_valid()) {
    out->print(" [result: "); result_opr()->print(out); out->print("]");
  }
}

// LIR_OpLabel
void LIR_OpLabel::print_instr(outputStream* out) const {
  out->print("[label:" PTR_FORMAT "]", _label);
}

// LIR_OpArrayCopy
void LIR_OpArrayCopy::print_instr(outputStream* out) const {
  src()->print(out);     out->print(" ");
  src_pos()->print(out); out->print(" ");
  dst()->print(out);     out->print(" ");
  dst_pos()->print(out); out->print(" ");
  length()->print(out);  out->print(" ");
}

// LIR_OpStringEquals
void LIR_OpStringEquals::print_instr(outputStream*out)const{
this_string()->print(out);out->print(" ");
other_string()->print(out);out->print(" ");
res()->print(out);out->print(" ");
out->print(" (");tmp1()->print(out);out->print(")");
out->print(" (");tmp2()->print(out);out->print(")");
out->print(" (");tmp3()->print(out);out->print(")");
out->print(" (");tmp4()->print(out);out->print(")");
out->print(" (");tmp5()->print(out);out->print(")");
out->print(" (");tmp6()->print(out);out->print(")");
}

// LIR_OpCompareAndSwap
void LIR_OpCompareAndSwap::print_instr(outputStream* out) const {
  obj()->print(out);       out->print(" ");
offset()->print(out);out->print(" ");
  cmp_value()->print(out); out->print(" ");
  new_value()->print(out); out->print(" ");
  tmp1()->print(out);      out->print(" ");
  tmp2()->print(out);      out->print(" ");

}

// LIR_Op0
void LIR_Op0::print_instr(outputStream* out) const {
  result_opr()->print(out);
if(_tmp1->is_valid()){
out->print(" (");tmp1_opr()->print(out);out->print(")");
  }
}

// LIR_Op1
const char * LIR_Op1::name() const {
  if (code() == lir_move) {
    switch (move_kind()) {
    case lir_move_normal:
      return "move";
    case lir_move_unaligned:
      return "unaligned move";
    case lir_move_volatile:
      return "volatile_move";
    default:
      ShouldNotReachHere();
    return "illegal_op";
    }
  } else {
    return LIR_Op::name();
  }
}


void LIR_Op1::print_instr(outputStream* out) const {
  _opr->print(out);         out->print(" ");
  result_opr()->print(out); out->print(" ");
  print_patch_code(out, patch_code());
if(_tmp1->is_valid()){
out->print(" (");tmp1_opr()->print(out);out->print(")");
  }
if(_tmp2->is_valid()){
out->print(" (");tmp2_opr()->print(out);out->print(")");
  }
if(_tmp3->is_valid()){
out->print(" (");tmp3_opr()->print(out);out->print(")");
  }
}


// LIR_Op1
void LIR_OpRTCall::print_instr(outputStream* out) const {
  out->print(Runtime1::name_for_address(addr()));
  out->print(" ");
}

void LIR_Op1::print_patch_code(outputStream* out, LIR_PatchCode code) {
  switch(code) {
    case lir_patch_none:                                 break;
    case lir_patch_low:    out->print("[patch_low]");    break;
    case lir_patch_high:   out->print("[patch_high]");   break;
    case lir_patch_normal: out->print("[patch_normal]"); break;
    default: ShouldNotReachHere();
  }
}

// LIR_OpBranch
void LIR_OpBranch::print_instr(outputStream* out) const {
  print_condition(out, cond());             out->print(" ");
  if (block() != NULL) {
    out->print("[B%d] ", block()->block_id());
  } else if (stub() != NULL) {
    out->print("[");
    stub()->print_name(out);
    out->print(": " PTR_FORMAT "]", stub());
    if (stub()->info() != NULL) out->print(" [bci:%d]", stub()->info()->bci());
  } else {
out->print("[label:%p] ",label());
  }
  if (ublock() != NULL) {
    out->print("unordered: [B%d] ", ublock()->block_id());
  }
}

void LIR_Op::print_condition(outputStream* out, LIR_Condition cond) {
  switch(cond) {
    case lir_cond_equal:           out->print("[EQ]");      break; 
    case lir_cond_notEqual:        out->print("[NE]");      break;
    case lir_cond_less:            out->print("[LT]");      break; 
    case lir_cond_lessEqual:       out->print("[LE]");      break; 
    case lir_cond_greaterEqual:    out->print("[GE]");      break; 
    case lir_cond_greater:         out->print("[GT]");      break; 
    case lir_cond_belowEqual:      out->print("[BE]");      break; 
    case lir_cond_aboveEqual:      out->print("[AE]");      break; 
    case lir_cond_always:          out->print("[AL]");      break;
    default:                       out->print("[%d]",cond); break;
  }
}

// LIR_OpConvert
void LIR_OpConvert::print_instr(outputStream* out) const {
  print_bytecode(out, bytecode());
  in_opr()->print(out);                  out->print(" ");
  result_opr()->print(out);              out->print(" ");
if(_tmp1->is_valid()){
out->print(" (");tmp1_opr()->print(out);out->print(")");
  }
}

void LIR_OpConvert::print_bytecode(outputStream* out, Bytecodes::Code code) {
  switch(code) {
    case Bytecodes::_d2f: out->print("[d2f] "); break; 
    case Bytecodes::_d2i: out->print("[d2i] "); break; 
    case Bytecodes::_d2l: out->print("[d2l] "); break;
    case Bytecodes::_f2d: out->print("[f2d] "); break; 
    case Bytecodes::_f2i: out->print("[f2i] "); break; 
    case Bytecodes::_f2l: out->print("[f2l] "); break;
    case Bytecodes::_i2b: out->print("[i2b] "); break;
    case Bytecodes::_i2c: out->print("[i2c] "); break;
    case Bytecodes::_i2d: out->print("[i2d] "); break;
    case Bytecodes::_i2f: out->print("[i2f] "); break; 
    case Bytecodes::_i2l: out->print("[i2l] "); break; 
    case Bytecodes::_i2s: out->print("[i2s] "); break; 
    case Bytecodes::_l2i: out->print("[l2i] "); break; 
    case Bytecodes::_l2f: out->print("[l2f] "); break; 
    case Bytecodes::_l2d: out->print("[l2d] "); break; 
    default:
      out->print("[?%d]",code);
    break;
  }
}

void LIR_OpSafepoint::print_instr(outputStream*out)const{
in_opr()->print(out);
out->print(" [lbl:%p]",stub()->entry());
}

void LIR_OpAlloc::print_instr(outputStream*out)const{
bytesize()->print(out);out->print(" ");
  len ()->print(out);                       out->print(" ");
kid()->print(out);out->print(" ");
  tmp1()->print(out);                       out->print(" ");
klass_reg()->print(out);out->print(" ");
  result_opr()->print(out);                 out->print(" ");
out->print("[lbl:%p]",stub()->entry());
}

// LIR_Op2
void LIR_Op2::print_instr(outputStream* out) const {
  if (code() == lir_cmove) {
    print_condition(out, condition());         out->print(" ");
  }
  in_opr1()->print(out);    out->print(" ");
  in_opr2()->print(out);    out->print(" ");
  if (tmp_opr()->is_valid()) { tmp_opr()->print(out);    out->print(" "); }
  result_opr()->print(out);
  //if (_info) _info->exception_handlers()->print(out);
}

void LIR_OpTypeCheck::print_instr(outputStream* out) const {
  object()->print(out);                  out->print(" ");
  if (code() == lir_store_check) {
    array()->print(out);                 out->print(" ");
  }
  if (code() != lir_store_check) {
    klass()->print_name_on(out);         out->print(" ");
    if (fast_check())                 out->print("fast_check ");
  }
  tmp1()->print(out);                    out->print(" ");
  tmp2()->print(out);                    out->print(" ");
  tmp3()->print(out);                    out->print(" ");
tmp4()->print(out);out->print(" ");
tmp5()->print(out);out->print(" ");
  result_opr()->print(out);              out->print(" ");
  if (info_for_exception() != NULL) out->print(" [bci:%d]", info_for_exception()->bci());
}


// LIR_Op3
void LIR_Op3::print_instr(outputStream* out) const {
  in_opr1()->print(out);    out->print(" ");
  in_opr2()->print(out);    out->print(" ");
  in_opr3()->print(out);    out->print(" ");
  result_opr()->print(out);
}


void LIR_OpLock::print_instr(outputStream* out) const {
  obj_opr()->print(out);   out->print(" ");
if(_tid->is_valid()){
_tid->print(out);out->print(" ");
  }
_mark->print(out);out->print(" ");
_tmp->print(out);out->print(" ");
out->print("mon_no = %d",_mon_num);
}


// LIR_OpProfileCall
void LIR_OpProfileCall::print_instr(outputStream* out) const {
  profiled_method()->name()->print_symbol_on(out);
  out->print(".");
  profiled_method()->holder()->name()->print_symbol_on(out);
  out->print(" @ %d ", profiled_bci());
  mdo()->print(out);           out->print(" ");
  recv()->print(out);          out->print(" ");
  tmp1()->print(out);          out->print(" ");
}


#endif // PRODUCT

// Implementation of LIR_InsertionBuffer

void LIR_InsertionBuffer::append(int index, LIR_Op* op) {
  assert(_index_and_count.length() % 2 == 0, "must have a count for each index");
  
  int i = number_of_insertion_points() - 1;
  if (i < 0 || index_at(i) < index) {
    append_new(index, 1);
  } else {
    assert(index_at(i) == index, "can append LIR_Ops in ascending order only");
    assert(count_at(i) > 0, "check");
    set_count_at(i, count_at(i) + 1);
  }
  _ops.push(op);

  DEBUG_ONLY(verify());
}

#ifdef ASSERT
void LIR_InsertionBuffer::verify() {
  int sum = 0;
  int prev_idx = -1;

  for (int i = 0; i < number_of_insertion_points(); i++) {
    assert(prev_idx < index_at(i), "index must be ordered ascending");
    sum += count_at(i);
  }
  assert(sum == number_of_ops(), "wrong total sum");
}
#endif
