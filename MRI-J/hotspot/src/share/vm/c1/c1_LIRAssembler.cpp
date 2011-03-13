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


#include "c1_FrameMap.hpp"
#include "c1_IR.hpp"
#include "c1_InstructionPrinter.hpp"
#include "c1_LIRAssembler.hpp"
#include "c1_ValueStack.hpp"
#include "disassembler_pd.hpp"

#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

void LIR_Assembler::patching_epilog(PatchingStub*patch){
  append_patching_stub(patch);

#ifdef ASSERT
Bytecodes::Code code=patch->_info->scope()->method()->java_code_at_bci(patch->_info->bci());
if(patch->name()[0]=='A'){
    switch (code) {
      case Bytecodes::_putstatic:
      case Bytecodes::_getstatic:
      case Bytecodes::_putfield:
      case Bytecodes::_getfield:
        break;
      default:
        ShouldNotReachHere();
    }
  } else if (patch->name()[0] == 'L') {
    switch (code) {
      case Bytecodes::_putstatic:
      case Bytecodes::_getstatic:
      case Bytecodes::_new:
      case Bytecodes::_anewarray:
      case Bytecodes::_multianewarray:
      case Bytecodes::_instanceof:
      case Bytecodes::_checkcast:
      case Bytecodes::_ldc:
      case Bytecodes::_ldc_w:
        break;
      default:
        ShouldNotReachHere();
    }
  } else {
    ShouldNotReachHere();
  }
#endif
}


//---------------------------------------------------------------


LIR_Assembler::LIR_Assembler(Compilation* c): 
   _compilation(c)
 , _masm(c->masm())
 , _frame_map(c->frame_map())
 , _current_block(NULL)
{
  _slow_case_stubs = new CodeStubList();
}


LIR_Assembler::~LIR_Assembler() {
}


void LIR_Assembler::append_patching_stub(PatchingStub* stub) {
  _slow_case_stubs->append(stub);
}


void LIR_Assembler::check_codespace() {
}


void LIR_Assembler::emit_code_stub(CodeStub* stub) {
  _slow_case_stubs->append(stub);
}

void LIR_Assembler::emit_stubs(CodeStubList* stub_list) {
  for (int m = 0; m < stub_list->length(); m++) {
    CodeStub* s = (*stub_list)[m];

    check_codespace();
    CHECK_BAILOUT();

#ifndef PRODUCT
    stringStream st;
    s->print_name(&st);
    st.print(" slow case");
    _masm->block_comment(st.as_string());
#endif
    s->emit_code(this);
  }
}


void LIR_Assembler::emit_slow_case_stubs() { 
  emit_stubs(_slow_case_stubs);
}


bool LIR_Assembler::needs_icache(ciMethod* method) const {
  return !method->is_static();
}


int LIR_Assembler::code_offset() const {
  return _masm->offset();
}


address LIR_Assembler::pc() const {
  return _masm->pc();
}


void LIR_Assembler::emit_exception_entries(ExceptionInfoList* info_list) {
  // tell these to the MacroAssembler 

  for (int i = 0; i < info_list->length(); i++) {
    XHandlers* handlers = info_list->at(i)->exception_handlers();

    for (int j = 0; j < handlers->length(); j++) {
      XHandler* handler = handlers->handler_at(j);
      assert(handler->lir_op_id() != -1, "handler not processed by LinearScan");
      assert(handler->entry_code() == NULL ||
handler->entry_code()->instructions_list()->last()->code()==lir_branch,"last operation must be branch");

      if (!handler->entry_lbl()) {
        // entry code not emitted yet
        if (handler->entry_code() != NULL && handler->entry_code()->instructions_list()->length() > 1) {
          handler->set_entry_lbl(new Label(_masm)); // bind label here
          _masm->block_comment("Exception adapter block");
          emit_lir_list(handler->entry_code());
        } else {
handler->set_entry_lbl(handler->entry_block()->label());
        }
      }
    }
  }
}


void LIR_Assembler::emit_code(BlockList* hir) {
  if (PrintLIR) {
    print_LIR(hir);
  }

  int n = hir->length();
  for (int i = 0; i < n; i++) {
    emit_block(hir->at(i));
    CHECK_BAILOUT();
  }
}


void LIR_Assembler::emit_block(BlockBegin* block) {
  if (block->is_set(BlockBegin::backward_branch_target_flag)) {
    align_backward_branch_target(); 
  }

#ifndef PRODUCT
  if (PrintLIRWithAssembly) {
    // don't print Phi's
    InstructionPrinter ip(false);
    block->print(ip);
  }
#endif /* PRODUCT */

  assert(block->lir() != NULL, "must have LIR");

#ifndef PRODUCT
  stringStream st;
  st.print_cr(" block B%d [%d, %d]", block->block_id(), block->bci(), block->end()->bci());
  _masm->block_comment(st.as_string());
#endif

  emit_lir_list(block->lir());

}


void LIR_Assembler::emit_lir_list(LIR_List* list) {
  peephole(list);

  int n = list->length();
  for (int i = 0; i < n; i++) {
    LIR_Op* op = list->at(i);

    check_codespace();
    CHECK_BAILOUT();

#ifndef PRODUCT
    // Don't record out every op since that's too verbose.  Print
    // branches since they include block and stub names.  Also print
    // patching moves since they generate funny looking code.
    if (op->code() == lir_branch ||
        (op->code() == lir_move && op->as_Op1()->patch_code() != lir_patch_none)) {
      stringStream st;
      op->print_on(&st);
      _masm->block_comment(st.as_string());
    }
    int start_relpc = _masm->rel_pc();
    if (PrintLIRWithAssembly) {
      // print out the LIR operation followed by the resulting assembly
list->at(i)->print(C1OUT);C1OUT->cr();
    }
#endif /* PRODUCT */

    op->emit_code(this);

#ifndef PRODUCT
    if (PrintLIRWithAssembly) {
      address code = (address)_masm->blob();
      Disassembler::decode(code+start_relpc, code+_masm->rel_pc(), C1OUT);
    }
#endif /* PRODUCT */
  }
#ifndef PRODUCT
  if (PrintLIRWithAssembly) {
C1OUT->flush();
  }
#endif
}

//----------------------------------debug info--------------------------------


void LIR_Assembler::add_debug_info_for_branch(CodeEmitInfo* info) {
  masm()->add_dbg( code_offset(), info->debug_scope() );
  if (info->exception_handlers() != NULL) {
compilation()->add_exception_handlers_for_pco(code_offset(),info->exception_handlers());
  }
}


void LIR_Assembler::add_call_info(int pc_offset, CodeEmitInfo* cinfo, bool inline_cache) {
  if( inline_cache ) cinfo->debug_scope()->set_inline_cache();
  masm()->add_dbg( pc_offset, cinfo->debug_scope() );
  if (cinfo->exception_handlers() != NULL) {
    compilation()->add_exception_handlers_for_pco(pc_offset, cinfo->exception_handlers());
  }
}

static ValueStack* debug_info(Instruction* ins) {
  StateSplit* ss = ins->as_StateSplit();
  if (ss != NULL) return ss->state();
  return ins->lock_stack();
}

// Index caller states in s, where 0 is the oldest, 1 its callee, etc.
// Return NULL if n is too large.
// Returns the caller_bci for the next-younger state, also.
static ValueStack* nth_oldest(ValueStack* s, int n, int& bci_result) {
  ValueStack* t = s;
  for (int i = 0; i < n; i++) {
    if (t == NULL)  break;
    t = t->caller_state();
  }
  if (t == NULL)  return NULL;
  for (;;) {
    ValueStack* tc = t->caller_state();
    if (tc == NULL)  return s;
    t = tc;
    bci_result = s->scope()->caller_bci();
    s = s->caller_state();
  }
}

void LIR_Assembler::add_debug_info_for_null_check_here(CodeEmitInfo* cinfo) {
  add_debug_info_for_null_check(code_offset(), cinfo);
}

void LIR_Assembler::add_debug_info_for_null_check(int pc_offset, CodeEmitInfo* cinfo) {
  ImplicitNullCheckStub* stub = new ImplicitNullCheckStub(pc_offset, cinfo);
  emit_code_stub(stub);
}

void LIR_Assembler::add_debug_info_for_div0_here(CodeEmitInfo* info) {
  add_debug_info_for_div0(code_offset(), info);
}

void LIR_Assembler::add_debug_info_for_div0(int pc_offset, CodeEmitInfo* cinfo) {
  DivByZeroStub* stub = new DivByZeroStub(pc_offset, cinfo);
  emit_code_stub(stub);
}

void LIR_Assembler::emit_rtcall(LIR_OpRTCall* op) {
rt_call(op->result_opr(),op->addr(),op->arguments(),op->info());
}


void LIR_Assembler::emit_call(LIR_OpJavaCall* op) {
  if( _masm->should_verify_oop(MacroAssembler::OopVerify_OutgoingArgument) ) {
    int len = op->arguments()->length();
for(int i=0;i<len;i++){
      LIR_Opr opr = op->arguments()->at(i);
      if( opr->is_oop() ) _masm->verify_oop(_frame_map->oopregname(opr), MacroAssembler::OopVerify_OutgoingArgument);
    }
  }
  switch (op->code()) {
  case lir_static_call:  
call(op->addr(),op->info());
    break;
  case lir_optvirtual_call: 
call(op->addr(),op->info());
    break;
  case lir_icvirtual_call:
    ic_call(op->addr(), op->info());
    break;
  case lir_virtual_call:
    vtable_call(op->vtable_offset(), op->info());
    break;
  default: ShouldNotReachHere();
  }
}


void LIR_Assembler::emit_opLabel(LIR_OpLabel* op) {
  _masm->bind (*(op->label()));
}


void LIR_Assembler::emit_op1(LIR_Op1* op) {
  switch (op->code()) {
    case lir_move:   
      if (op->move_kind() == lir_move_volatile) {
        assert(op->patch_code() == lir_patch_none, "can't patch volatiles");
        volatile_move_op(op->in_opr(), op->result_opr(), op->type(), op->info());
      } else {
move_op(op->in_opr(),op->result_opr(),op->tmp1_opr(),op->tmp2_opr(),op->tmp3_opr(),op->type(),
                op->patch_code(), op->info(), op->move_kind() == lir_move_unaligned);
      }
      break;

    case lir_prefetchr:
      prefetchr(op->in_opr());
      break;

    case lir_prefetchw:
      prefetchw(op->in_opr());
      break;

    case lir_return:
      return_op(op->in_opr()); 
      break;
    
    case lir_branch:
      break;

    case lir_push:
      push(op->in_opr());
      break;

    case lir_pop:
      pop(op->in_opr());
      break;

    case lir_neg:
      negate(op->in_opr(), op->result_opr());
      break;
    
    case lir_bit_test:
bit_test(op->in_opr(),op->result_opr());
      break;
    
    case lir_leal:
      leal(op->in_opr(), op->result_opr());
      break;
    
    case lir_null_check:
      if (GenerateCompilerNullChecks) {
null_check(op->in_opr(),op->info());
      }
      break;

    case lir_klassTable_oop_load:
      klassTable_oop_load(op->in_opr(), op->result_opr(), op->tmp1_opr());
      break;

    case lir_monaddr:
      monitor_address(op->in_opr()->as_constant_ptr()->as_jint(), op->result_opr());
      break;

    default:
      Unimplemented();
      break;
  }
}


void LIR_Assembler::emit_op0(LIR_Op0* op) {
  switch (op->code()) {
    case lir_word_align: {
      while (code_offset() % BytesPerWord != 0) {
        _masm->nop();
      }
      break;
    }

    case lir_nop:
      assert(op->info() == NULL, "not supported");
      _masm->nop();
      break;

    case lir_label:
      Unimplemented();
      break;

    case lir_build_frame:
      build_frame();
      break;

    case lir_std_entry:
      _masm->align(CodeEntryAlignment);
_masm->set_code_start();
      _masm->entry(_compilation->codeprofile());
      build_frame();
      break;

    case lir_osr_entry:
      Unimplemented();
      //osr_entry();
      break;

    case lir_breakpoint:
      breakpoint();
      break;

    case lir_membar:
      membar();
      break;

    case lir_membar_acquire:
      membar_acquire();
      break;

    case lir_membar_release:
      membar_release();
      break;

    case lir_get_thread:
      get_thread(op->result_opr());
      break;

    default: 
      ShouldNotReachHere();
      break;
  }
}


void LIR_Assembler::emit_op2(LIR_Op2* op) {
  switch (op->code()) {
    case lir_cmp:
      comp_op(op->condition(), op->in_opr1(), op->in_opr2(), op);
      break;
    
    case lir_cmp_l2i:
    case lir_cmp_fd2i:
    case lir_ucmp_fd2i:
      comp_fl2i(op->code(), op->in_opr1(), op->in_opr2(), op->result_opr(), op);
      break;

    case lir_cmove:
cmove(op->condition(),op->in_opr1(),op->in_opr2(),op->tmp_opr(),op->result_opr());
      break;

    case lir_shl:
    case lir_shr:
    case lir_ushr:
      if (op->in_opr2()->is_constant()) {
        shift_op(op->code(), op->in_opr1(), op->in_opr2()->as_constant_ptr()->as_jint(), op->result_opr());
      } else {
        shift_op(op->code(), op->in_opr1(), op->in_opr2(), op->result_opr(), op->tmp_opr());
      }
      break;

    case lir_add:
    case lir_sub:
    case lir_mul:
    case lir_mul_strictfp:
    case lir_div:
    case lir_div_strictfp:
    case lir_rem:
      arith_op(
        op->code(),
        op->in_opr1(),
        op->in_opr2(),
        op->result_opr(),
op->info());
      break;
    
    case lir_abs:
    case lir_sqrt:
    case lir_sin:
    case lir_tan:
    case lir_cos:
    case lir_log:
    case lir_log10:
      intrinsic_op(op->code(), op->in_opr1(), op->in_opr2(), op->result_opr(), op);
      break;

    case lir_logic_and:
    case lir_logic_or:
    case lir_logic_xor:
      logic_op(
        op->code(),
        op->in_opr1(),
        op->in_opr2(),
        op->result_opr());
      break;

    case lir_throw:
    case lir_unwind:
throw_op(op->in_opr2(),op->in_opr1(),op->info(),op->code()==lir_unwind);
      break;

    default:
      Unimplemented();
      break;
  }
}


void LIR_Assembler::build_frame() {
_masm->build_frame(frame_map());
}


void LIR_Assembler::move_op(LIR_Opr src, LIR_Opr dest, LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, BasicType type, LIR_PatchCode patch_code, CodeEmitInfo* info, bool unaligned) {
  if (src->is_register()) {
    if (dest->is_register()) {
      assert(patch_code == lir_patch_none && info == NULL, "no patching and info allowed here");
      assert(!tmp1->is_valid() && !tmp2->is_valid(), "unnecessary definition of temp operands");
      reg2reg(src,  dest);
    } else if (dest->is_stack()) {
      assert(patch_code == lir_patch_none && info == NULL, "no patching and info allowed here");
      assert(!tmp1->is_valid() && !tmp2->is_valid(), "unnecessary definition of temp operands");
reg2stack(src,dest,type);
    } else if (dest->is_address()) {
reg2mem(src,dest,tmp1,tmp2,tmp3,type,patch_code,info,unaligned);
    } else {
      ShouldNotReachHere();
    }

  } else if (src->is_stack()) {
    assert(patch_code == lir_patch_none && info == NULL, "no patching and info allowed here");
    if (dest->is_register()) {
      assert(!tmp1->is_valid() && !tmp2->is_valid(), "unnecessary definition of temp operands");
      stack2reg(src, dest, type);
    } else if (dest->is_stack()) {
assert(!tmp2->is_valid(),"unnecessary definition of temp operands");
      stack2stack(src, dest, tmp1, type);
    } else {
      ShouldNotReachHere();
    }

  } else if (src->is_constant()) {
    if (dest->is_register()) {
assert(!tmp3->is_valid(),"unnecessary definition of temp operands");
      const2reg(src, dest, patch_code, info, tmp1, tmp2); // patching is possible
    } else if (dest->is_stack()) {
      assert(patch_code == lir_patch_none && info == NULL, "no patching and info allowed here");
assert(!tmp3->is_valid(),"unnecessary definition of temp operands");
      const2stack(src, dest, tmp1, tmp2);
    } else if (dest->is_address()) {
      assert(patch_code == lir_patch_none, "no patching allowed here");
      const2mem(src, dest, tmp1, tmp2, tmp3, type, info);
    } else {
      ShouldNotReachHere();
    }

  } else if (src->is_address()) {
assert(!tmp2->is_valid(),"unnecessary definition of temp operand");
    mem2reg(src, dest, tmp1, type, patch_code, info, unaligned);

  } else {
    ShouldNotReachHere();
  }
}



