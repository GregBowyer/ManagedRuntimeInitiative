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


#include "c1_CodeStubs.hpp"
#include "c1_FrameMap.hpp"
#include "c1_IR.hpp"
#include "c1_LIR.hpp"
#include "c1_LIRGenerator.hpp"
#include "c1_ValueStack.hpp"
#include "ciInstance.hpp"
#include "ciObjArrayKlass.hpp"
#include "ciTypeArrayKlass.hpp"
#include "deoptimization.hpp"
#include "javaClasses.hpp"
#include "sharedRuntime.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

#ifdef ASSERT
#define __ gen()->lir(__FILE__, __LINE__)->
#else
#define __ gen()->lir()->
#endif

// Item will be loaded into a byte register; Intel only
void LIRItem::load_byte_item() {
  load_item();
  LIR_Opr res = result();

if(!res->is_virtual()){
    // make sure that it is a byte register
    assert(!value()->type()->is_float() && !value()->type()->is_double(),
           "can't load floats in byte register");
    LIR_Opr reg = _gen->rlock_byte(T_BYTE);
    __ move(res, reg);

    _result = reg;
  }
}


void LIRItem::load_nonconstant() {
  LIR_Opr r = value()->operand();
  if (r->is_constant()) {
    _result = r;
  } else {
    load_item();
  }
}

//--------------------------------------------------------------
//               LIRGenerator
//--------------------------------------------------------------


LIR_Opr LIRGenerator::exceptionOopOpr() { return FrameMap::rax_oop_opr; }
LIR_Opr LIRGenerator::exceptionPcOpr()  { return FrameMap::rdx_opr; }
LIR_Opr LIRGenerator::divInOpr()        { return FrameMap::rax_opr; }
LIR_Opr LIRGenerator::divOutOpr()       { return FrameMap::rax_opr; }
LIR_Opr LIRGenerator::remOutOpr()       { return FrameMap::rdx_opr; }
LIR_Opr LIRGenerator::divInOprLong(){return FrameMap::rax_long_opr;}
LIR_Opr LIRGenerator::divOutOprLong(){return FrameMap::rax_long_opr;}
LIR_Opr LIRGenerator::remOutOprLong(){return FrameMap::rdx_long_opr;}
LIR_Opr LIRGenerator::shiftCountOpr()   { return FrameMap::rcx_opr; }
LIR_Opr LIRGenerator::syncTempOpr()     { return FrameMap::rax_opr; }


LIR_Opr LIRGenerator::result_register_for(ValueType* type, bool callee) {
  LIR_Opr opr;
  switch (type->tag()) {
    case intTag:     opr = FrameMap::rax_opr;          break;
    case objectTag:  opr = FrameMap::rax_oop_opr;      break;
case longTag:opr=FrameMap::rax_long_opr;break;
case floatTag:opr=FrameMap::xmm0_float_opr;break;
case doubleTag:opr=FrameMap::xmm0_double_opr;break;
    
    case addressTag:
    default: ShouldNotReachHere(); return LIR_OprFact::illegalOpr;
  }

  assert(opr->type_field() == as_OprType(as_BasicType(type)), "type mismatch");
  return opr;
}


LIR_Opr LIRGenerator::rlock_byte(BasicType type) {
//AZUL: all CPU regs can be byte regs on Azul x86-64
return new_register(T_INT);
}


//--------- loading items into registers --------------------------------


// i486 instructions can inline constants
bool LIRGenerator::can_store_as_constant(Value v, BasicType type) const {
  if (type == T_SHORT || type == T_CHAR) {
    // there is no immediate move of word values in asembler_i486.?pp
    return false;
  }
  Constant* c = v->as_Constant();
  if (c && c->state() == NULL) {
    // constants of any type can be stored directly, except for
    // unloaded object constants.
    return true;
  }
  return false;
}


bool LIRGenerator::can_inline_as_constant(Value v) const {
  return v->type()->tag() != objectTag ||
    (v->type()->is_constant() && v->type()->as_ObjectType()->constant_value()->is_null_object());
}


bool LIRGenerator::can_inline_as_constant(LIR_Const* c) const {
  return c->type() != T_OBJECT || c->as_jobject() == NULL;
}


LIR_Opr LIRGenerator::safepoint_poll_register() {
  return getThreadPointer();
}


LIR_Address* LIRGenerator::generate_address(LIR_Opr base, LIR_Opr index,
                                            int shift, int disp, BasicType type) {
  assert(base->is_register(), "must be");
assert(base->is_oop_register(),"must be");

  if (index->is_constant()) {
    return new LIR_Address(base,
                           (index->as_constant_ptr()->as_jint() << shift) + disp,
                           type);
  } else {
    return new LIR_Address(base, index, (LIR_Address::Scale)shift, disp, type);
  }
}


LIR_Address* LIRGenerator::emit_array_address(LIR_Opr array_opr, LIR_Opr index_opr,
                                              BasicType type, bool needs_card_mark) {
  int offset_in_bytes = arrayOopDesc::base_offset_in_bytes(type);

  LIR_Address* addr;
  if (index_opr->is_constant()) {
    int elem_size = type2aelembytes[type];
    addr = new LIR_Address(array_opr,
                           offset_in_bytes + index_opr->as_jint() * elem_size, type);
  } else {
    addr =  new LIR_Address(array_opr,
                            index_opr,
                            LIR_Address::scale(type),
                            offset_in_bytes, type);
  }
  return addr;
}


void LIRGenerator::increment_counter(address counter, int step) {
  Unimplemented();
//  LIR_Opr temp = new_register(T_INT);
//  LIR_Opr pointer = new_register(T_INT);
//  __ move(LIR_OprFact::intConst((int)counter), pointer);
//  LIR_Address* addr = new LIR_Address(pointer, 0, T_INT);
//  increment_counter(addr, step);
}


void LIRGenerator::increment_counter(LIR_Address* addr, int step) {
  __ add((LIR_Opr)addr, LIR_OprFact::intConst(step), (LIR_Opr)addr);
}


void LIRGenerator::do_IncrementCount(IncrementCount*x){
  CodeProfile *cp = _compilation->codeprofile();

#ifdef CPMAGIC
  if (VerifyCPDataAtRuntime) {
    const int cpdoff = x->cpd_offset_from_CP();
    if (cpdoff>=0) {
LIR_Opr magic_base=new_register(T_LONG);
LabelObj*magicok=new LabelObj();
      LIR_Opr magic_ptr = LIR_OprFact::intConst((intptr_t)cp+cpdoff + CPData::magic_offset());
__ move(magic_ptr,magic_base);
      LIR_Address *magic_adr = new LIR_Address(magic_base, 0, T_CHAR);
LIR_Opr magic_reg=new_register(T_INT);
__ move(magic_adr,magic_reg);
      __ unsigned_shift_right(magic_reg, 4, magic_reg);
      __ logical_and(magic_reg, LIR_OprFact::intConst(0x0F), magic_reg);
__ cmp(lir_cond_equal,magic_reg,LIR_OprFact::intConst(0x0F));
__ branch(lir_cond_equal,magicok->label());
      __ breakpoint(); // how do I inject a ShouldNotReachHere into C1 IR?
__ branch_destination(magicok->label());
    } 
  }
#endif

  if (x->deopt_threshold()>0 && x->state()!=NULL) {
    LIR_Opr result = increment_and_return_counter( _cp_reg, x->count_offset_from_CP(), 1 );
    CodeEmitInfo* info = state_for(x, x->state());
__ bit_test(result,C1_LogBailOutToC2);
    CodeStub *stub = new UncommonTrapStub(Deoptimization::Reason_stuck_in_loop, info);
__ branch(lir_cond_carry,T_INT,stub);
  } else {
    LIR_Address* counter = new LIR_Address(_cp_reg, x->count_offset_from_CP(), T_INT);
    LIR_Opr ctr = LIR_OprFact::address(counter);
    __ add( ctr, LIR_OprFact::intConst(1), ctr );
    //LIR_Opr result = new_register(T_INT);
    //__ load(counter, result);
    //__ add(result, LIR_OprFact::intConst(1), result);
    //__ store(result, counter);
  }

}

void LIRGenerator::cmp_mem_int(LIR_Condition condition, LIR_Opr base, int disp, int c, CodeEmitInfo* info) {
  __ cmp_mem_int(condition, base, disp, c, info);
}


void LIRGenerator::cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Opr base, int disp, BasicType type, CodeEmitInfo* info) {
  __ cmp_reg_mem(condition, reg, new LIR_Address(base, disp, type), info);
}


void LIRGenerator::cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Opr base, LIR_Opr disp, BasicType type, CodeEmitInfo* info) {
  __ cmp_reg_mem(condition, reg, new LIR_Address(base, disp, type), info);
}


bool LIRGenerator::strength_reduce_multiply(LIR_Opr left, int c, LIR_Opr result, LIR_Opr tmp) {
  if (tmp->is_valid()) {
    if (is_power_of_2(c + 1)) {
      __ move(left, tmp);
      __ shift_left(left, log2_intptr(c + 1), left);
      __ sub(left, tmp, result);
      return true;
    } else if (is_power_of_2(c - 1)) {
      __ move(left, tmp);
      __ shift_left(left, log2_intptr(c - 1), left);
      __ add(left, tmp, result);
      return true;
    }
  }
  return false;
}


void LIRGenerator::store_stack_parameter (LIR_Opr item, ByteSize offset_from_sp) {
  BasicType type = item->type();
  __ store(item, new LIR_Address(FrameMap::rsp_opr, in_bytes(offset_from_sp), type));
}

//----------------------------------------------------------------------
//             visitor functions
//----------------------------------------------------------------------


void LIRGenerator::do_StoreIndexed(StoreIndexed* x) {
  assert( x->is_root(),"" );
  assert( x->elt_type() != T_ARRAY, "stores of arrays have type T_OBJECT" );
  bool needs_range_check = true;
  bool use_length = x->length() != NULL;
bool obj_store=x->elt_type()==T_OBJECT;
  // need store check unless storing a null
  bool needs_store_check = obj_store && (x->value()->as_Constant() == NULL ||
                                         !get_jobject_constant(x->value())->is_null_object());
  // avoid store check if we're storing to a freshly created Object[]
  if( needs_store_check && x->array()->as_NewObjectArray() != NULL &&
      x->array()->as_NewObjectArray()->exact_type()->is_loaded() &&
      ((ciKlass*)(x->array()->as_NewObjectArray()->exact_type()))->element_klassId() == java_lang_Object_kid ) {
    needs_store_check = false;
  }
  LIRItem array(x->array(), this);
  LIRItem index(x->index(), this);
  LIRItem value(x->value(), this);
  LIRItem length(this);

  array.load_item();
  index.load_nonconstant();

  if (use_length) {
    needs_range_check = x->compute_needs_range_check();
    if (needs_range_check) {
      length.set_instruction(x->length());
      length.load_item();
    }
  }
  if (needs_store_check) {
    value.load_item();
  } else {
    value.load_for_store(x->elt_type());
  }

  set_no_result(x);

  // the CodeEmitInfo must be duplicated for each different
  // LIR-instruction because spilling can occur anywhere between two
  // instructions and so the debug information must be different
  CodeEmitInfo* range_check_info = state_for(x);
  CodeEmitInfo* null_check_info = NULL;
  if (x->needs_null_check()) {
    null_check_info = new CodeEmitInfo(range_check_info);
  }

  // this code is an ugly (possibly having 2 copies of array) work around to give the
  // array store, the array math and range check take a fresh and
  // destroyed register, perhaps we can add smarts to the destroyed register
  // code to detect multiple uses of a result and insert the necessary copies
  // (or blow up). See similar issue in the shared LIR generator for load
  // indexed.

  if (needs_range_check) {
    if (use_length) {
      __ cmp(lir_cond_belowEqual, length.result(), index.result());
      __ branch(lir_cond_belowEqual, T_INT, new RangeCheckStub(range_check_info, index.result()));
    } else {
LIR_Opr new_array2=new_register(T_ARRAY);
      if( !MultiMapMetaData ) new_array2 = new_array2->set_destroyed();
      lir()->move(array.result(), new_array2);
      array_range_check(new_array2, index.result(), null_check_info, range_check_info);
      // range_check also does the null check
      null_check_info = NULL;
    }
  }

  if (needs_store_check) {
    LIR_Opr new_array = FrameMap::rax_oop_opr->set_destroyed();
    lir()->move(array.result(), new_array);
    LIR_Opr new_value = FrameMap::r11_oop_opr->set_destroyed();
    lir()->move(value.result(), new_value);
    CodeEmitInfo* store_check_info = new CodeEmitInfo(range_check_info);
    __ store_check(new_value, new_array, LIR_OprFact::illegalOpr, LIR_OprFact::illegalOpr, LIR_OprFact::illegalOpr, LIR_OprFact::illegalOpr, LIR_OprFact::illegalOpr, store_check_info, _cp_reg);
  }

  // Scheduled later than JavaSoft to avoid carrying a destroyed register
LIR_Opr new_array1=new_register(T_ARRAY);
  if( !MultiMapMetaData ) new_array1 = new_array1->set_destroyed();
  lir()->move(array.result(), new_array1);
  LIR_Address* array_addr = emit_array_address(new_array1, index.result(), x->elt_type(), obj_store);

  if (obj_store) {
if(null_check_info==NULL){
      if(UseSBA) {
        // info always needed for SBA escape
        null_check_info = new CodeEmitInfo(range_check_info);
      }
    } else {
      // AZUL x86-64 do explicit null check to avoid handling in card mark
      if( !MultiMapMetaData ) {
        CodeStub *stub = new SimpleExceptionStub(Runtime1::throw_null_pointer_exception_id,
                                                 LIR_OprFact::illegalOpr, null_check_info);
        __ cmp(lir_cond_equal, array.result(), LIR_OprFact::oopConst(NULL));
__ branch(lir_cond_equal,T_OBJECT,stub);
      } else {
	__ null_check(array.result(), null_check_info);
      }
      // only need info if an SBA escape is possible
      null_check_info = UseSBA ? new CodeEmitInfo(null_check_info) : NULL;
    }
    __ move(value.result(), array_addr, null_check_info);
    // Seems to be a precise 
    post_barrier(LIR_OprFact::address(array_addr), value.result());
  } else {
    __ move(value.result(), array_addr, null_check_info);
  }
}


void LIRGenerator::do_Base(Base*x){
  __ std_entry(LIR_OprFact::illegalOpr);

  // Emit moves from physical registers / stack slots to virtual registers
  CallingConvention* args = compilation()->frame_map()->incoming_arguments();
  IRScope* irScope = compilation()->hir()->top_scope();
  int java_index = 0;
  for (int i = 0; i < args->length(); i++) {
    LIR_Opr src = args->at(i);
    assert(!src->is_illegal(), "check");
    BasicType t = src->type();

    // Types which are smaller than int are passed as int, so
    // correct the type which passed.
    switch (t) {
    case T_BYTE:
    case T_BOOLEAN:
    case T_SHORT:
    case T_CHAR:
      t = T_INT;
      break;
    }

    LIR_Opr dest = new_register(t);
    __ move(src, dest);

    // Assign new location to Local instruction for this local
    Local* local = x->state()->local_at(java_index)->as_Local();
    assert(local != NULL, "Locals for incoming arguments must have been created");
    assert(as_ValueType(t)->tag() == local->type()->tag(), "check");
    local->set_operand(dest);
    _instruction_for_operand.at_put_grow(dest->vreg_number(), local, NULL);
    java_index += type2size[t];
  }

  if (method()->is_synchronized() && GenerateSynchronizationCode) {
LIR_Opr obj;
    if (method()->is_static()) {
      obj = new_register(T_OBJECT);
      __ oop2reg(method()->holder()->java_mirror()->encoding(), obj);
    } else {
      Local* receiver = x->state()->local_at(0)->as_Local();
      assert(receiver != NULL, "must already exist");
      obj = receiver->operand();
    }
    assert(obj->is_valid(), "must be valid");

    LIR_Opr tmp  = get_temp(0, T_LONG);
    LIR_Opr mark = FrameMap::rax_opr; // used in compare-xchg
LIR_Opr tid=FrameMap::r09_opr;
CodeEmitInfo*info=new CodeEmitInfo(InvocationEntryBci,scope()->start()->state(),NULL);
    CodeStub* slow_case = new MonitorEnterStub(obj, info);
    __ lock_object(obj, tid, mark, tmp, 0, slow_case, info);
  }

  // Register holding current JavaThread*
  _thread_reg = new_register(T_ADDRESS);
__ get_thread(_thread_reg);
  // register that the start of this block defines this register (for inter basic block analysis)
  set_result(x->begin(), _thread_reg);

  // Poll for GC & stackoverflow also
CodeEmitInfo*info=new CodeEmitInfo(InvocationEntryBci,scope()->start()->state(),NULL);
  SafepointStub* slow_path = new SafepointStub(info);
__ safepoint(safepoint_poll_register(),slow_path);

  // Register for CodeProfile
  if( UseC2 ) {
    // TODO: even with UseC2 this register isn't always used, make definition conditional
    _cp_reg = new_register(T_LONG);
    CodeProfile *cp = compilation()->codeprofile();
    __ move(LIR_OprFact::longConst((intptr_t)cp),_cp_reg);
    // register that this instruction defines this register (for inter basic block analysis)
set_result(x,_cp_reg);
  }

  // all blocks with a successor must end with an unconditional jump
  // to the successor even if they are consecutive
  __ jump(x->default_sux());
}


void LIRGenerator::do_ExceptionObject(ExceptionObject*x){
  assert(block()->is_set(BlockBegin::exception_entry_flag), "ExceptionObject only allowed in exception handler block");
  assert(block()->next() == x, "ExceptionObject must be first instruction of block");

  // no moves are created for phi functions at the begin of exception
  // handlers, so assign operands manually here
  for_each_phi_fun(block(), phi,
                   operand_for_instruction(phi));

  // Reset registers holding literals (they may have been spilled, we need a consistent state)
__ get_thread(_thread_reg);
  if( UseC2 ) {
    CodeProfile *cp = compilation()->codeprofile();
    __ move(LIR_OprFact::longConst((intptr_t)cp),_cp_reg);
  }

  // Grab and clear exception object
__ move(new LIR_Address(_thread_reg,in_bytes(JavaThread::pending_exception_offset()),T_OBJECT),
          exceptionOopOpr());
  __ move(LIR_OprFact::oopConst(NULL),
new LIR_Address(_thread_reg,in_bytes(JavaThread::pending_exception_offset()),T_OBJECT));

LIR_Opr result=new_register(T_OBJECT);
  __ move(exceptionOopOpr(), result);
  set_result(x, result);
}

void LIRGenerator::do_MonitorEnter(MonitorEnter* x) {
  assert(x->is_root(),"");
  LIRItem obj(x->obj(), this);
  set_no_result(x);

  LIR_Opr tmp  = get_temp(0, T_LONG);
  LIR_Opr mark = FrameMap::rax_opr; // used in compare-xchg
LIR_Opr tid=FrameMap::r09_opr;

  CodeEmitInfo* info_for_exception = NULL;
  if (x->needs_null_check()) {
    info_for_exception = state_for(x, x->lock_stack_before());
  }
  // this CodeEmitInfo must not have the xhandlers because here the
  // object is already locked (xhandlers expect object to be unlocked)
  CodeEmitInfo* info = state_for(x, x->state(), true);

  obj.load_item();
monitor_enter(obj.result(),tid,mark,tmp,x->monitor_no(),info_for_exception,info);
}

void LIRGenerator::do_MonitorExit(MonitorExit* x) {
  assert(x->is_root(),"");
  LIRItem obj(x->obj(), this);
  set_no_result(x);

LIR_Opr mark=FrameMap::rcx_opr;
LIR_Opr tmp=FrameMap::rax_opr;
  if( MultiMapMetaData ) obj.load_item_force(FrameMap::r10_oop_opr);
  else                   obj.load_item_force(FrameMap::r10_oop_opr->set_destroyed());

  monitor_exit(obj.result(), mark, tmp, x->monitor_no());
}

// _ineg, _lneg, _fneg, _dneg
void LIRGenerator::do_NegateOp(NegateOp* x) {
  LIRItem value(x->x(), this);
  value.load_item();
  LIR_Opr reg = rlock(x);
  __ negate(value.result(), reg);
  
set_result(x,reg);
}


// for  _fadd, _fmul, _fsub, _fdiv, _frem
//      _dadd, _dmul, _dsub, _ddiv, _drem
void LIRGenerator::do_ArithmeticOp_FPU(ArithmeticOp* x) {
  LIRItem left(x->x(),  this);
  LIRItem right(x->y(), this);
  assert(!left.is_stack() || !right.is_stack(), "can't both be memory operands");
if(left.is_register()||x->x()->type()->is_constant()){
    left.load_item();
  } else {
    left.dont_load_item();
  }
  if (right.is_register() || x->y()->type()->is_constant()) {
    right.load_item();
  } else {
    right.dont_load_item();
  }
  LIR_Opr reg = rlock(x);
  LIR_Opr tmp = LIR_OprFact::illegalOpr;
  arithmetic_op_fpu(x->op(), reg, left.result(), right.result(), x->is_strictfp(), tmp);
set_result(x,reg);
}

// for  _ldiv, _lrem, _idiv, _irem
void LIRGenerator::do_ArithmeticOp_CPU_div(ArithmeticOp *x, bool is_long) {
  assert0( (!is_long && (x->op() == Bytecodes::_idiv || x->op() == Bytecodes::_irem))||
           ( is_long && (x->op() == Bytecodes::_ldiv || x->op() == Bytecodes::_lrem)));
  // The requirements for division and modulo
  // input : rax,: dividend                         min_int
  //         reg: divisor   (may not be rax,/rdx)   -1
  //
  // output: rax,: quotient  (= rax, idiv reg)      min_int
  //         rdx:  remainder (= rax, irem reg)      0

  // rax, and rdx will be destroyed

  // Note: does this invalidate the spec ???
  LIRItem right(x->y(), this);
  LIRItem left(x->x() , this);   // visit left second, so that the is_register test is valid

  // call state_for before load_item_force because state_for may
  // force the evaluation of other instructions that are needed for
  // correct debug info.  Otherwise the live range of the fix
  // register might be too long.
  CodeEmitInfo* info = state_for(x);

  left.load_item_force(is_long ? divInOprLong() : divInOpr());

  if (right.is_constant()) {
    intptr_t divisor = is_long ? right.get_jlong_constant() : right.get_jint_constant();
    if (powerof2(divisor) && (divisor > 0) &&
        (!is_long ||
         (is_int32(divisor-1)    && x->op() == Bytecodes::_ldiv) ||
         (is_int32(~(divisor-1)) && x->op() == Bytecodes::_lrem))) {
right.dont_load_item();
    } else {
      right.load_item();
    }
    if (divisor == 0) __ branch(lir_cond_always, is_long ? T_LONG : T_INT, new DivByZeroStub(new CodeEmitInfo(info)));
  } else {
right.load_item();
    __ cmp(lir_cond_equal, right.result(), is_long ? LIR_OprFact::longConst(0) : LIR_OprFact::intConst(0));
    __ branch(lir_cond_equal, is_long ? T_LONG : T_INT, new DivByZeroStub(new CodeEmitInfo(info)));
  }

  LIR_Opr result = rlock_result(x);
  LIR_Opr result_reg;
if(x->op()==Bytecodes::_idiv||x->op()==Bytecodes::_ldiv){
    result_reg = is_long ? divOutOprLong() : divOutOpr();
  } else {
    result_reg = is_long ? remOutOprLong() : remOutOpr();
  }

  LIR_Opr tmp = FrameMap::rdx_opr; // idiv and irem use rdx in their implementation
  if (x->op() == Bytecodes::_irem) {
    __ irem(left.result(), right.result(), result_reg, tmp, info);
  } else if (x->op() == Bytecodes::_idiv) {
    __ idiv(left.result(), right.result(), result_reg, tmp, info);
}else if(x->op()==Bytecodes::_lrem){
__ lrem(left.result(),right.result(),result_reg,tmp,info);
}else if(x->op()==Bytecodes::_ldiv){
__ ldiv(left.result(),right.result(),result_reg,tmp,info);
  } else {
    ShouldNotReachHere();
  }
  __ move(result_reg, result);
}

// for  _lmul, _imul
void LIRGenerator::do_ArithmeticOp_CPU_mul(ArithmeticOp *x, bool is_long) {
  assert0( (!is_long && x->op() == Bytecodes::_imul ) ||
           ( is_long && x->op() == Bytecodes::_lmul ) );
  // missing test if instr is commutative and if we should swap
  LIRItem left(x->x(),  this);
  LIRItem right(x->y(),  this);
  LIRItem *left_arg =  &left;
  LIRItem *right_arg = &right;
  if (x->is_commutative() && left.is_stack() && right.is_register()) {
    // swap them if left is real stack (or cached) and right is real register(not cached)
    left_arg  = &right;
    right_arg = &left;
  }
  left_arg->load_item();
  // do not need to load right, as we can handle stack and constants
  // check if we can use shift instead of multiply
  bool use_constant = false;
  bool use_tmp = false;
  if (right_arg->is_constant()) {
    intptr_t iconst = is_long ? right_arg->get_jlong_constant() : right_arg->get_jint_constant();
    if (iconst > 0) {
      if (is_power_of_2(iconst)) {
        use_constant = true;
      } else if (is_power_of_2(iconst - 1) || is_power_of_2(iconst + 1)) {
        use_constant = true;
        use_tmp = true;
      } else {
        use_constant = is_int32(iconst);
      }
    } else {
      use_constant = is_int32(iconst);
    }
  }
  if (use_constant) {
    right_arg->dont_load_item();
  } else {
    right_arg->load_item();
  }
  LIR_Opr tmp = LIR_OprFact::illegalOpr;
  if (use_tmp) {
    tmp = new_register(is_long ? T_LONG : T_INT);
  }
  rlock_result(x);
arithmetic_op_cpu(x->op(),x->operand(),left_arg->result(),right_arg->result(),tmp);
}

// for  _ladd, _lsub, _iadd, _isub
void LIRGenerator::do_ArithmeticOp_CPU_addsub(ArithmeticOp *x, bool is_long) {
  assert0( (!is_long && (x->op() == Bytecodes::_iadd || x->op() == Bytecodes::_isub))||
           ( is_long && (x->op() == Bytecodes::_ladd || x->op() == Bytecodes::_lsub)));
  // missing test if instr is commutative and if we should swap
  LIRItem left(x->x(),  this);
  LIRItem right(x->y(),  this);
  LIRItem *left_arg =  &left;
  LIRItem *right_arg = &right;

  if (ciEnv::current()->failing()) return;

  if (x->is_commutative() && left.is_stack() && right.is_register()) {
    // swap them if left is real stack (or cached) and right is real register(not cached)
    left_arg  = &right;
    right_arg = &left;
  }
  left_arg->load_item();
  // do not need to load right, as we can handle stack and constants
  LIR_Opr tmp = LIR_OprFact::illegalOpr;
  rlock_result(x);
  if (is_long && right_arg->is_constant()) {
    intptr_t iconst = right_arg->get_jlong_constant();
    if (!is_int32(iconst)) {
      // force non 32bit constants to be loaded, recycle result register if possible
      right_arg->load_item();
if(x->is_commutative()){
arithmetic_op_cpu(x->op(),x->operand(),right_arg->result(),left_arg->result(),tmp);
      } else {
arithmetic_op_cpu(x->op(),x->operand(),left_arg->result(),right_arg->result(),tmp);
      }
      return;
    }
  }
  right_arg->dont_load_item();
arithmetic_op_cpu(x->op(),x->operand(),left_arg->result(),right_arg->result(),tmp);
}


void LIRGenerator::do_ArithmeticOp(ArithmeticOp* x) {
  // when an operand with use count 1 is the left operand, then it is
  // likely that no move for 2-operand-LIR-form is necessary
  if (x->is_commutative() && x->y()->as_Constant() == NULL && x->x()->use_count() > x->y()->use_count()) {
    x->swap_operands();
  }

  ValueTag tag = x->type()->tag();
  assert(x->x()->type()->tag() == tag && x->y()->type()->tag() == tag, "wrong parameters");
  switch (tag) {
  case floatTag:
case doubleTag:
do_ArithmeticOp_FPU(x);break;
case longTag:
case intTag:
switch(x->op()){
    case Bytecodes::_lmul: do_ArithmeticOp_CPU_mul(x, true);  break;
    case Bytecodes::_imul: do_ArithmeticOp_CPU_mul(x, false); break;
case Bytecodes::_ldiv://fall through
    case Bytecodes::_lrem: do_ArithmeticOp_CPU_div(x, true);  break;
case Bytecodes::_idiv://fall through
    case Bytecodes::_irem: do_ArithmeticOp_CPU_div(x, false); break;
case Bytecodes::_lsub://fall through
    case Bytecodes::_ladd: do_ArithmeticOp_CPU_addsub(x, true); break;
case Bytecodes::_isub://fall through
    case Bytecodes::_iadd: do_ArithmeticOp_CPU_addsub(x, false); break;
    default:         ShouldNotReachHere();
    }
  }
}


// _ishl, _lshl, _ishr, _lshr, _iushr, _lushr
void LIRGenerator::do_ShiftOp(ShiftOp* x) {
  // count must always be in rcx
  LIRItem value(x->x(), this);
  LIRItem count(x->y(), this);

  ValueTag elemType = x->type()->tag();
  bool must_load_count = !count.is_constant() || elemType == longTag;
  if (must_load_count) {
    // count for long must be in register
    count.load_item_force(shiftCountOpr());
  } else {
    count.dont_load_item();
  }
  value.load_item();
  LIR_Opr reg = rlock_result(x);

  shift_op(x->op(), reg, value.result(), count.result(), LIR_OprFact::illegalOpr);
}


// _iand, _land, _ior, _lor, _ixor, _lxor
void LIRGenerator::do_LogicOp(LogicOp* x) {
Bytecodes::Code op=x->op();
  bool is_long = (op == Bytecodes::_land) || (op == Bytecodes::_lor) || (op == Bytecodes::_lxor);
  // when an operand with use count 1 is the left operand, then it is
  // likely that no move for 2-operand-LIR-form is necessary
  if (x->is_commutative() && x->y()->as_Constant() == NULL && x->x()->use_count() > x->y()->use_count()) {
    x->swap_operands();
  }

  LIRItem left(x->x(), this);
  LIRItem right(x->y(), this);

  left.load_item();
LIR_Opr res=rlock_result(x);

  if (is_long && right.is_constant()) {
    bool force_load = false;
    // force non-32bit constants to be loaded
    intptr_t lconst = right.get_jlong_constant();
    if (op == Bytecodes::_land) {
      // ensure for land we either want to zero the high bits or correctly sign extend
      force_load = ((lconst >> 32) != 0) && !is_int32(lconst);
    } else {
      force_load = !is_int32(lconst);
    }
    if (force_load) {
      assert0( x->is_commutative() );
      right.load_item();
logic_op(x->op(),res,right.result(),left.result());
      return;
    }
  }
  right.load_nonconstant();
logic_op(x->op(),res,left.result(),right.result());
}



// _lcmp, _fcmpl, _fcmpg, _dcmpl, _dcmpg
void LIRGenerator::do_CompareOp(CompareOp* x) {
  LIRItem left(x->x(), this);
  LIRItem right(x->y(), this);
  ValueTag tag = x->x()->type()->tag();
  left.load_item();
  right.load_item();
  LIR_Opr reg = rlock_result(x);

  if (x->x()->type()->is_float_kind()) {
    Bytecodes::Code code = x->op();
    __ fcmp2int(left.result(), right.result(), reg, (code == Bytecodes::_fcmpl || code == Bytecodes::_dcmpl));
  } else if (x->x()->type()->tag() == longTag) {
    __ lcmp2int(left.result(), right.result(), reg);
  } else {
    Unimplemented();
  }
}


void LIRGenerator::do_AttemptUpdate(Intrinsic* x) {
  assert(x->number_of_arguments() == 3, "wrong type");
  LIRItem obj (x->argument_at(0), this);  // AtomicLong object
LIRItem cmp(x->argument_at(1),this);//value to compare with field
LIRItem val(x->argument_at(2),this);//replace field with new_value if it matches cmp_value

  LIR_Opr offset = LIR_OprFact::intConst(sun_misc_AtomicLongCSImpl::value_offset());
  cas_helper(x, ThreadLocals->_longType, &obj, offset, &cmp, &val);
}

void LIRGenerator::do_CompareAndSwap(Intrinsic* x, ValueType* type) {
  assert(x->number_of_arguments() == 4, "wrong type");
  LIRItem obj   (x->argument_at(0), this);  // object
  LIRItem offset(x->argument_at(1), this);  // offset of field
  LIRItem cmp   (x->argument_at(2), this);  // value to compare with field
  LIRItem val   (x->argument_at(3), this);  // replace field with val if matches cmp
assert(offset.type()->tag()==longTag,"invalid type");
  offset.load_nonconstant();
  cas_helper(x, type, &obj, offset.result(), &cmp, &val);
}

void LIRGenerator::cas_helper(Intrinsic* x, ValueType* type, LIRItem* obj, LIR_Opr offset, LIRItem* cmp, LIRItem* val) {
  assert(obj->type()->tag() == objectTag,   "invalid type");
  assert(cmp->type()->tag() == type->tag(), "invalid type");
  assert(val->type()->tag() == type->tag(), "invalid type");
  LIR_Opr addr = get_temp(0, T_LONG);
  LIR_Opr ill = LIR_OprFact::illegalOpr;  // for convenience
  LIR_Opr tmp = ill;
obj->load_item();
  if( !MultiMapMetaData ) obj->set_destroys_register();
val->load_item();
  // compare value must be in rax; may be destroyed by cmpxchg instruction
  if (type == ThreadLocals->_intType) {
    cmp->load_item_force(FrameMap::rax_opr);
    __ cas_int (obj->result(), offset, cmp->result(), val->result(), ill, ill);
  } else if (type == ThreadLocals->_longType) {
    cmp->load_item_force(FrameMap::rax_long_opr);
    __ cas_long(obj->result(), offset, cmp->result(), val->result(), ill, ill);
  } else {
    assert0 (type == ThreadLocals->_objectType);
    cmp->load_item_force(FrameMap::rax_oop_opr);
    __ cas_obj (obj->result(), offset, cmp->result(), val->result(), get_temp(0, T_LONG), get_temp(1, T_LONG));
    tmp = get_temp(2, T_LONG);
  }
  // generate conditional move of boolean result
  LIR_Opr result = rlock_result(x);
__ cmove(lir_cond_equal,LIR_OprFact::intConst(1),LIR_OprFact::intConst(0),result,tmp);
if(type==ThreadLocals->_objectType){//Write-barrier needed for Object fields.
    // Seems to be precise
post_barrier(addr,val->result());
  }
}

void LIRGenerator::do_MathIntrinsic(Intrinsic* x) {
  assert(x->number_of_arguments() == 1, "wrong type");
  LIRItem value(x->argument_at(0), this);

address rtcall_target=NULL;

  switch(x->id()) {
case vmIntrinsics::_dabs:break;
case vmIntrinsics::_dsqrt:break;
case vmIntrinsics::_dlog:break;
case vmIntrinsics::_dlog10:break;
  case vmIntrinsics::_dcos:   rtcall_target = (address)SharedRuntime::dcos; break;
  case vmIntrinsics::_dsin:   rtcall_target = (address)SharedRuntime::dsin; break;
  case vmIntrinsics::_dtan:   rtcall_target = (address)SharedRuntime::dtan; break;
  default:                    ShouldNotReachHere();
  }

if(rtcall_target==NULL){
    value.load_item(); // any reg will do
  } else {
value.load_item_force(FrameMap::xmm0_double_opr);
  }
  LIR_Opr calc_input = value.result();
  LIR_Opr calc_result = rlock_result(x);

LIR_Opr il=LIR_OprFact::illegalOpr;
  switch(x->id()) {
case vmIntrinsics::_dabs:__ abs(calc_input,calc_result,il);break;
case vmIntrinsics::_dsqrt:__ sqrt(calc_input,calc_result,il);break;
case vmIntrinsics::_dlog:__ log(calc_input,calc_result,il);break;
case vmIntrinsics::_dlog10:__ log10(calc_input,calc_result,il);break;
case vmIntrinsics::_dcos:
case vmIntrinsics::_dsin:
case vmIntrinsics::_dtan:{
LIR_OprList*args=new LIR_OprList(1);
args->append(calc_input);
    __ call_runtime_leaf(rtcall_target, FrameMap::xmm0_double_opr, args);
__ move(FrameMap::xmm0_double_opr,calc_result);
    break;
  }
  default: ShouldNotReachHere();
  }
}


void LIRGenerator::do_ArrayCopy(Intrinsic* x) {
  assert(x->number_of_arguments() == 5, "wrong type");
  LIRItem src(x->argument_at(0), this);
  LIRItem src_pos(x->argument_at(1), this);
  LIRItem dst(x->argument_at(2), this);
  LIRItem dst_pos(x->argument_at(3), this);
  LIRItem length(x->argument_at(4), this);
  int flags;
  ciArrayKlass* expected_type;
  // Grab information about the arraycopy
  arraycopy_helper(x, &flags, &expected_type);
//We may want to have stack (deoptimization?) Call prior to forcing registers
//so that fixed registers don't get clobbered.
CodeEmitInfo*info=state_for(x,x->state());
//Do we know the type of array copy?
if(expected_type){
    // We know the type, force into known registers for array copy.
    if( !MultiMapMetaData ) {
      dst.load_item_force (FrameMap::rdi_oop_opr->set_destroyed());
      src.load_item_force (FrameMap::rsi_oop_opr->set_destroyed());
    } else {
dst.load_item_force(FrameMap::rdi_oop_opr);
src.load_item_force(FrameMap::rsi_oop_opr);
    }
length.load_item_force(FrameMap::rdx_opr);
src_pos.load_item();
dst_pos.load_item();
  } else {
    // We don't know the type of the array copy force args into registers for a
    // call through to the original Java method
src.load_item_force(FrameMap::rdi_oop_opr);
src_pos.load_item_force(FrameMap::rsi_opr);
dst.load_item_force(FrameMap::rdx_oop_opr);
dst_pos.load_item_force(FrameMap::rcx_opr);
length.load_item_force(FrameMap::r08_opr);
  }
  set_no_result(x);
  __ arraycopy(src.result(), src_pos.result(), dst.result(), dst_pos.result(), length.result(), expected_type, flags, info); // does add_safepoint
}

void LIRGenerator::do_StringEquals(Intrinsic*x){
assert(x->number_of_arguments()==2,"wrong type");
LIRItem this_string(x->argument_at(0),this);
LIRItem other_string(x->argument_at(1),this);

  // If this for call is null then we can generate an NPE, if other_string is null
  // then the result is false.
  CodeEmitInfo* info = state_for(x, x->state());

this_string.load_item();
other_string.load_item();
LIR_Opr res=rlock(x);

  if( !MultiMapMetaData ) {
this_string.set_destroys_register();
other_string.set_destroys_register();
  }
  LIR_Opr Rtmp1 = get_temp(0, T_LONG); // for RCX use FrameMap::rcx_opr;
  LIR_Opr Rtmp2 = get_temp(1, T_LONG);
  LIR_Opr Rtmp3 = get_temp(2, T_LONG);
  LIR_Opr Rtmp4 = get_temp(3, T_LONG);
LIR_Opr Rtmp5=new_register(T_DOUBLE);
LIR_Opr Rtmp6=new_register(T_DOUBLE);
  __ stringequals(this_string.result(), other_string.result(), res,
                  Rtmp1, Rtmp2, Rtmp3, Rtmp4, Rtmp5, Rtmp6, info);

set_result(x,res);
}

void LIRGenerator::do_Convert(Convert* x) {
  LIRItem value(x->value(), this);
  value.load_item();
  LIR_Opr input = value.result();
  LIR_Opr result = rlock(x);

__ convert(x->op(),input,result,NULL);

  assert(result->is_virtual(), "result must be virtual register");
  set_result(x, result);
}


void LIRGenerator::do_NewInstance(NewInstance* x) {
  if (PrintNotLoaded && !x->klass()->is_loaded()) {
    tty->print_cr("   ###class not loaded at new bci %d", x->bci());
  }
  CodeEmitInfo* info = state_for(x, x->state());

  new_instance(FrameMap::rax_oop_opr,
               x->klass(),
               FrameMap::rdx_opr, // size in bytes
               FrameMap::rcx_opr, // (EKID<<32) | LENGTH  (or 0 for instances)
               FrameMap::rsi_opr, // KID
               FrameMap::r09_opr, // tmp1
               info);
  LIR_Opr result = rlock_result(x);
__ move(FrameMap::rax_oop_opr,result);
}


void LIRGenerator::do_NewTypeArray(NewTypeArray* x) {
  // Get state prior to forcing fixed registers to avoid blowing them
  CodeEmitInfo* info = state_for(x, x->state());

  LIRItem length(x->length(), this);
length.load_item_force(FrameMap::rcx_opr);

LIR_Opr size=FrameMap::rdx_opr;//size in bytes
  LIR_Opr len  = length.result();  // LENGTH
  LIR_Opr kid  = FrameMap::rsi_opr;// KID
LIR_Opr klass_reg=LIR_OprFact::illegalOpr;
LIR_Opr tmp1=FrameMap::r09_opr;
LIR_Opr res=FrameMap::rax_oop_opr;
  BasicType elem_type = x->elt_type();
  
CodeStub*slow_path=new NewTypeArrayStub(len,res,info);
  __ allocate_prim_array(size, len, kid, tmp1, res, klass_reg, elem_type, slow_path);

  LIR_Opr result = rlock_result(x);
__ move(res,result);
}


void LIRGenerator::do_NewObjectArray(NewObjectArray* x) {
  LIRItem length(x->length(), this);
  // in case of patching (i.e., object class is not yet loaded), we need to reexecute the instruction
  // and therefore provide the state before the parameters have been consumed
  CodeEmitInfo* patching_info = NULL;
  if (!x->klass()->is_loaded() || PatchALot) {
    patching_info =  state_for(x, x->state_before());
  }

  CodeEmitInfo* info = state_for(x, x->state());
ciKlass*elem_klass=x->klass();

length.load_item_force(FrameMap::rcx_opr);
LIR_Opr size=FrameMap::rdx_opr;//size in bytes
  LIR_Opr len  = length.result();
  LIR_Opr kid  = FrameMap::rsi_opr; // KID
LIR_Opr tmp1=FrameMap::r09_opr;
LIR_Opr klass_reg;
LIR_Opr res=FrameMap::rax_oop_opr;
  
CodeStub*slow_path=new NewObjectArrayStub(len,res,info);

  bool always_slow_path = !UseFastNewInstance || 
                          !elem_klass->is_loaded() ||
                          ( !Klass::layout_helper_is_javaArray(elem_klass->layout_helper()) &&
                             Klass::layout_helper_needs_slow_path(elem_klass->layout_helper()) );
  if (always_slow_path) {
    ciObject* obj = ciObjArrayKlass::make(elem_klass);
    if (obj == ciEnv::unloaded_ciobjarrayklass()) {
      BAILOUT("encountered unloaded_ciobjarrayklass due to out of memory error");
    }
klass_reg=new_register(T_OBJECT);
    jobject2reg_with_patching(klass_reg, obj, patching_info);
  } else {
klass_reg=LIR_OprFact::illegalOpr;
  }
  __ allocate_obj_array(size, len, kid, tmp1, res, klass_reg, elem_klass, always_slow_path, slow_path);

  LIR_Opr result = rlock_result(x);
__ move(res,result);
}


void LIRGenerator::do_NewMultiArray(NewMultiArray* x) {
  // Build list of dimensions
  Values* dims = x->dims();
  int i = dims->length();
  LIRItemList* items = new LIRItemList(dims->length(), NULL);
  while (i-- > 0) {
    LIRItem* size = new LIRItem(dims->at(i), this);
    items->at_put(i, size);
  }

  // need to get the info before, as the items may become invalid through item_free
  CodeEmitInfo* patching_info = NULL;
  if (!x->klass()->is_loaded() || PatchALot) {
    patching_info = state_for(x, x->state_before());

    // cannot re-use same xhandlers for multiple CodeEmitInfos, so
    // clone all handlers.
    x->set_exception_handlers(new XHandlers(x->exception_handlers()));
  }

  CodeEmitInfo* info = state_for(x, x->state());

  // Ensure space in reserved area for out going args
  frame_map()->update_reserved_argument_area_size(in_ByteSize(dims->length() * sizeof(jint)));

  // Store dimensions onto the stack
  i = dims->length();
  while (i-- > 0) {
    LIRItem* size = items->at(i);
    size->load_nonconstant();
store_stack_parameter(size->result(),in_ByteSize(i*sizeof(jint)));
  }

  // Registers for arguments
LIR_Opr klass_reg=FrameMap::rsi_oop_opr;
LIR_Opr rank_reg=FrameMap::rdx_opr;
LIR_Opr dims_reg=FrameMap::rcx_opr;

jobject2reg_with_patching(klass_reg,x->klass(),patching_info);
__ move(LIR_OprFact::intConst(x->rank()),rank_reg);
__ move(FrameMap::rsp_long_opr,dims_reg);

  LIR_OprList* args = new LIR_OprList(3);
args->append(klass_reg);
args->append(rank_reg);
args->append(dims_reg);
LIR_Opr result=result_register_for(x->type());
  __ call_runtime((address)Runtime1::new_multi_array, result, args, info);

LIR_Opr result2=rlock_result(x);
__ move(result,result2);
}


void LIRGenerator::do_BlockBegin(BlockBegin* x) {
  // nothing to do for now
}


void LIRGenerator::do_CheckCast(CheckCast* x) {
ciKlass*k=x->klass();
  CodeEmitInfo* patching_info = NULL;
if(!k->is_loaded()||(PatchALot&&!x->is_incompatible_class_change_check())){
    // must do this before locking the destination register as an oop register,
    // and before the obj is loaded (the latter is for deoptimization)
    patching_info = state_for(x, x->state_before());
  }

  // info for exceptions
  CodeEmitInfo* info_for_exception = state_for(x, x->state()->copy_locks());

  // Get input and output
  LIRItem obj(x->obj(), this);
  obj.load_item();
  LIR_Opr reg = rlock_result(x);

  CodeStub* stub;
  if (x->is_incompatible_class_change_check()) {
    assert(patching_info == NULL, "can't patch this");
    stub = new SimpleExceptionStub(Runtime1::throw_incompatible_class_change_error_id, LIR_OprFact::illegalOpr, info_for_exception);
  } else {
    stub = new SimpleExceptionStub(Runtime1::throw_class_cast_exception_id, obj.result(), info_for_exception);
  }

  // compute number of temps and whether we need fixed regs
  bool fast_check = x->direct_compare();
  int temps_needed;
  if (fast_check) {
    temps_needed=1;
  } else if (k->is_loaded()) {
    temps_needed=3;
  } else {
    temps_needed=5;
  }

  bool calls_stub = !k->is_loaded() ||
    k->super_check_offset() == sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes();

  LIR_Opr tmp1, tmp2, tmp3, tmp4, tmp5;
  tmp1 = tmp2 = tmp3 = tmp4 = tmp5 = LIR_OprFact::illegalOpr;
BasicType type=T_LONG;
  if (temps_needed > 0) {
    tmp1 = get_temp(0, type);
    if (temps_needed > 1) {
      tmp2 = calls_stub ? FrameMap::r09_opr : get_temp(1, type);
      if (temps_needed > 2) {
        tmp3 = calls_stub ? FrameMap::rax_opr : get_temp(2, type);
        if (temps_needed > 3) {
          tmp4 = get_temp(3, type);
          if (temps_needed > 4) {
            tmp5 = get_temp(4, type);
          }
        }
      }
    }
  }

  __ checkcast(reg, obj.result(), k, tmp1, tmp2, tmp3, tmp4, tmp5,
               fast_check, info_for_exception, patching_info, stub,
               x->profiled_method(), x->profiled_bci(), _cp_reg, x->cpdoff());
}


void LIRGenerator::do_InstanceOf(InstanceOf* x) {
ciKlass*k=x->klass();
  LIRItem obj(x->obj(), this);

  // result and test object may not be in same register
  LIR_Opr reg = rlock_result(x);
  CodeEmitInfo* patching_info = NULL;
if((!k->is_loaded()||PatchALot)){
    // must do this before locking the destination register as an oop register
    patching_info = state_for(x, x->state_before());
  }
  obj.load_item();

  bool fast_check = x->direct_compare();
  int temps_needed;
  if (fast_check) {
    temps_needed=1;
  } else if (k->is_loaded()) {
    temps_needed=3;
  } else {
    temps_needed=5;
  }

  bool calls_stub = !k->is_loaded() ||
    k->super_check_offset() == sizeof(oopDesc) + Klass::secondary_super_kid_cache_offset_in_bytes();

  LIR_Opr tmp1, tmp2, tmp3, tmp4, tmp5;
  tmp1 = tmp2 = tmp3 = tmp4 = tmp5 = LIR_OprFact::illegalOpr;
BasicType type=T_LONG;
  if (temps_needed > 0) {
    tmp1 = get_temp(0, type);
    if (temps_needed > 1) {
      tmp2 = calls_stub ? FrameMap::r09_opr : get_temp(1, type);
      if (temps_needed > 2) {
        tmp3 = calls_stub ? FrameMap::rax_opr : get_temp(2, type);
        if (temps_needed > 3) {
          tmp4 = get_temp(3, type);
          if (temps_needed > 4) {
            tmp5 = get_temp(4, type);
          }
        }
      }
    }
  }
  // NB we don't use fixed registers, there use could avoid spills, as their use
  // could cause spills and we only need the fixed registers on the slowest path
  __ instanceof(reg, obj.result(), k, tmp1, tmp2, tmp3, tmp4, tmp5,
                fast_check, patching_info, _cp_reg, x->cpdoff());
}


void LIRGenerator::do_If(If* x) {
  assert(x->number_of_sux() == 2, "inconsistency");
  ValueTag tag = x->x()->type()->tag();
  bool is_safepoint = x->is_safepoint();

  If::Condition cond = x->cond();

  LIRItem xitem(x->x(), this);
  LIRItem yitem(x->y(), this);
  LIRItem* xin = &xitem;
  LIRItem* yin = &yitem;

  xin->load_item();
if(tag==longTag&&yin->is_constant()){
if(is_int32(yin->get_jlong_constant())){
//inline long compare with int32
      yin->dont_load_item();
    } else {
      // can't handle long compare with int64
      yin->load_item();
    }
  } else if (tag == floatTag || tag == doubleTag) {
    // floats & doubles cannot handle constants at right side
    yin->load_item();
  } else if (tag == objectTag && yin->is_constant()) {
    if (yin->get_jobject_constant()->is_null_object()) {
      yin->dont_load_item();
    } else {
yin->load_item();
    }
  } else {
    yin->dont_load_item();
  }

  // add safepoint before generating condition code so it can be recomputed
if(is_safepoint){
    // increment backedge counter if needed
    increment_backedge_counter(state_for(x, x->state_before()));

    SafepointStub* slow_path = new SafepointStub(state_for(x, x->state_before()));
__ safepoint(safepoint_poll_register(),slow_path);
  }
  set_no_result(x);

  LIR_Opr left  = xin->result();
  LIR_Opr right = yin->result();
  __ cmp(lir_cond(cond), left, right);
  profile_branch(x, cond);
  move_to_phi(x->state());
  if (x->x()->type()->is_float_kind()) {
    __ branch(lir_cond(cond), right->type(), x->tsux(), x->usux());
  } else {
    __ branch(lir_cond(cond), right->type(), x->tsux());
  }
  assert(x->default_sux() == x->fsux(), "wrong destination above");
  __ jump(x->default_sux());
}


LIR_Opr LIRGenerator::getThreadPointer() {
  assert0( _thread_reg->is_valid() );
  return _thread_reg;
}

void LIRGenerator::trace_block_entry(BlockBegin* block) {
  store_stack_parameter(LIR_OprFact::intConst(block->block_id()), in_ByteSize(0));
  LIR_OprList* args = new LIR_OprList();
  address func = CAST_FROM_FN_PTR(address, Runtime1::trace_block_entry);
__ call_runtime_leaf(func,LIR_OprFact::illegalOpr,args);
}


void LIRGenerator::volatile_field_store(LIR_Opr value, LIR_Address* address,
                                        CodeEmitInfo* info) {
//On X86_64 aligned 64-bit loads & stores are atomic
__ store(value,address,info,lir_patch_none);
}



void LIRGenerator::volatile_field_load(LIR_Address* address, LIR_Opr result,
                                       CodeEmitInfo* info) {
  // On X86_64 aligned 64-bit loads & stores are atomic
  __ load(address, result, info);
}

void LIRGenerator::get_Object_unsafe(LIR_Opr dst, LIR_Opr src, LIR_Opr offset,
                                     BasicType type, bool is_volatile) {
  if (is_volatile && type == T_LONG) {
    LIR_Address* addr = new LIR_Address(src, offset, T_DOUBLE);
    LIR_Opr tmp = new_register(T_DOUBLE);
    __ load(addr, tmp);
    LIR_Opr spill = new_register(T_LONG);
    set_vreg_flag(spill, must_start_in_memory);
    __ move(tmp, spill);
    __ move(spill, dst);
  } else {
    LIR_Address* addr = new LIR_Address(src, offset, type);
    __ load(addr, dst);
  }
}


void LIRGenerator::put_Object_unsafe(LIR_Opr src, LIR_Opr offset, LIR_Opr data,
                                     BasicType type, bool is_volatile) {
  if (is_volatile && type == T_LONG) {
    LIR_Address* addr = new LIR_Address(src, offset, T_DOUBLE);
    LIR_Opr tmp = new_register(T_DOUBLE);
    LIR_Opr spill = new_register(T_DOUBLE);
    set_vreg_flag(spill, must_start_in_memory);
    __ move(data, spill);
    __ move(spill, tmp);
    __ move(tmp, addr);
  } else {
    LIR_Address* addr = new LIR_Address(src, offset, type);
    bool is_obj = (type == T_ARRAY || type == T_OBJECT);
    if (is_obj) {
      __ move(data, addr);
      assert(src->is_register(), "must be register");
      // Seems to be a precise address
      post_barrier(LIR_OprFact::address(addr), data);
    } else {
      __ move(data, addr);
    }
  }
}
