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
#ifndef C1_LIRASSEMBLER_HPP
#define C1_LIRASSEMBLER_HPP


#include "c1_CodeStubs.hpp"
#include "c1_Compilation.hpp"
#include "c1_Instruction.hpp"
#include "c1_MacroAssembler.hpp"

class Compilation;
class ScopeValue;

class LIR_Assembler: public CompilationResourceObj {
 private:
  C1_MacroAssembler* _masm;
  CodeStubList*      _slow_case_stubs;

  Compilation*       _compilation;
  FrameMap*          _frame_map;
  BlockBegin*        _current_block;

  FrameMap* frame_map() const { return _frame_map; }

  void set_current_block(BlockBegin* b) { _current_block = b; }
  BlockBegin* current_block() const { return _current_block; }

  // unified bailout support
  void bailout(const char* msg) const            { compilation()->bailout(msg); }
  bool bailed_out() const                        { return compilation()->bailed_out(); }

  // code emission patterns and accessors
  void check_codespace();
  bool needs_icache(ciMethod* method) const;

  void jobject2reg(jobject o, Register reg);
void jobject2reg_with_patching(Register reg,Register tmp1,Register tmp2,CodeEmitInfo*info);

  void emit_stubs(CodeStubList* stub_list);

  // addresses
  static Address as_Address(LIR_Address* addr);
  static Address as_Address_lo(LIR_Address* addr);
  static Address as_Address_hi(LIR_Address* addr);

  // debug information
public:
  void add_call_info(int pc_offset, CodeEmitInfo* cinfo, bool inline_cache);
  void add_debug_info_for_branch(CodeEmitInfo* info);
  void add_debug_info_for_div0(int pc_offset, CodeEmitInfo* cinfo);
  void add_debug_info_for_div0_here(CodeEmitInfo* info);
  void add_debug_info_for_null_check(int pc_offset, CodeEmitInfo* cinfo);
  void add_debug_info_for_null_check_here(CodeEmitInfo* info);
private:

  void breakpoint();
void profile_invoke(CodeEmitInfo*info);
  void push(LIR_Opr opr);
  void pop(LIR_Opr opr);

  // patching
  void append_patching_stub(PatchingStub* stub);
void patching_epilog(PatchingStub*patch);

  void comp_op(LIR_Condition condition, LIR_Opr src, LIR_Opr result, LIR_Op2* op);

 public:
  LIR_Assembler(Compilation* c);
  ~LIR_Assembler();
  C1_MacroAssembler* masm() const                { return _masm; }
  Compilation* compilation() const               { return _compilation; }
  ciMethod* method() const                       { return compilation()->method(); }

  int code_offset() const;
  address pc() const;

  int  initial_frame_size_in_bytes();

  // test for constants which can be encoded directly in instructions
  static bool is_small_constant(LIR_Opr opr);

  static LIR_Opr receiverOpr();
  static LIR_Opr incomingReceiverOpr();
  static LIR_Opr osrBufferPointer();

  // stubs
  void emit_slow_case_stubs();
  void emit_code_stub(CodeStub* op);
void add_call_info_here(CodeEmitInfo*info){add_call_info(code_offset(),info,false);}

  // code patterns
  void emit_exception_handler();
  void emit_exception_entries(ExceptionInfoList* info_list);
  void emit_deopt_handler();

  void emit_code(BlockList* hir);
  void emit_block(BlockBegin* block);
  void emit_lir_list(LIR_List* list);

  // any last minute peephole optimizations are performed here.  In
  // particular sparc uses this for delay slot filling.
  void peephole(LIR_List* list);

  void emit_string_compare(LIR_Opr left, LIR_Opr right, LIR_Opr dst, CodeEmitInfo* info);

  void return_op(LIR_Opr result);

  void safepoint_poll(LIR_Opr thread_reg, SafepointStub* slowpath);

  void const2reg  (LIR_Opr src, LIR_Opr dest, LIR_PatchCode patch_code, CodeEmitInfo* info, LIR_Opr tmp1, LIR_Opr tmp2);
  void const2stack(LIR_Opr src, LIR_Opr dest, LIR_Opr tmp1, LIR_Opr tmp2);
  void const2mem  (LIR_Opr src, LIR_Opr dest, LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, BasicType type, 
                   CodeEmitInfo* info);
void reg2stack(LIR_Opr src,LIR_Opr dest,BasicType type);
  void reg2reg    (LIR_Opr src, LIR_Opr dest);
void reg2mem(LIR_Opr src,LIR_Opr dest,LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr tmp3,BasicType type,
LIR_PatchCode patch_code,CodeEmitInfo*info,
		   bool /* unaligned */ );
  void stack2reg  (LIR_Opr src, LIR_Opr dest, BasicType type);
void stack2stack(LIR_Opr src,LIR_Opr dest,LIR_Opr tmp,BasicType type);
void mem2reg(LIR_Opr src,LIR_Opr dest,LIR_Opr tmp,BasicType type,
                   LIR_PatchCode patch_code = lir_patch_none,
                   CodeEmitInfo* info = NULL, bool unaligned = false);

  void null_check(LIR_Opr obj, CodeEmitInfo *info);

  void prefetchr  (LIR_Opr src);
  void prefetchw  (LIR_Opr src);

  void shift_op(LIR_Code code, LIR_Opr left, LIR_Opr count, LIR_Opr dest, LIR_Opr tmp);
  void shift_op(LIR_Code code, LIR_Opr left, jint  count, LIR_Opr dest);

  void move_regs(Register from_reg, Register to_reg);
  void swap_reg(Register a, Register b);

  void emit_op0(LIR_Op0* op);
  void emit_op1(LIR_Op1* op);
  void emit_op2(LIR_Op2* op);
  void emit_op3(LIR_Op3* op);
  void emit_opBranch(LIR_OpBranch* op);
  void emit_opLabel(LIR_OpLabel* op);
  void emit_arraycopy(LIR_OpArrayCopy* op);
  void emit_stringequals(LIR_OpStringEquals* op);
  void emit_opConvert(LIR_OpConvert* op);
void emit_alloc_obj(LIR_OpAlloc*op);
void emit_alloc_array(LIR_OpAlloc*op);
  void emit_opTypeCheck(LIR_OpTypeCheck* op);
  void emit_compare_and_swap(LIR_OpCompareAndSwap* op);
  void emit_lock(LIR_OpLock* op);
  void emit_call(LIR_OpJavaCall* op);
  void emit_rtcall(LIR_OpRTCall* op);
  void emit_profile_call(LIR_OpProfileCall* op);

void arith_op(LIR_Code code,LIR_Opr left,LIR_Opr right,LIR_Opr dest,CodeEmitInfo*info);
  void intrinsic_op(LIR_Code code, LIR_Opr value, LIR_Opr unused, LIR_Opr dest, LIR_Op* op);

  void logic_op(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr dest);

  void move_op(LIR_Opr src, LIR_Opr result, LIR_Opr tmp1, LIR_Opr tmp2, LIR_Opr tmp3, BasicType type,
               LIR_PatchCode patch_code, CodeEmitInfo* info, bool unaligned);
  void volatile_move_op(LIR_Opr src, LIR_Opr result, BasicType type, CodeEmitInfo* info);
  void comp_mem_op(LIR_Opr src, LIR_Opr result, BasicType type, CodeEmitInfo* info);  // info set for null exceptions
  void comp_fl2i(LIR_Code code, LIR_Opr left, LIR_Opr right, LIR_Opr result, LIR_Op2* op);
void cmove(LIR_Condition code,LIR_Opr left,LIR_Opr right,LIR_Opr tmp,LIR_Opr result);

  void ic_call(address destination, CodeEmitInfo* info);
  void vtable_call(int vtable_offset, CodeEmitInfo* info);
void call(address entry,CodeEmitInfo*info);

  void osr_entry();

  void build_frame();

void throw_op(LIR_Opr exceptionOop,LIR_Opr tmp,CodeEmitInfo*info,bool unwind);
  void monitor_address(int monitor_ix, LIR_Opr dst);

  void align_backward_branch_target();

  void negate(LIR_Opr left, LIR_Opr dest);
  void leal(LIR_Opr left, LIR_Opr dest);
void bit_test(LIR_Opr left,LIR_Opr bitnum);

  void rt_call(LIR_Opr result, address dest, const LIR_OprList* args, CodeEmitInfo* info);

  void membar();
  void membar_acquire();
  void membar_release();
  void get_thread(LIR_Opr result);

  // Azul
  void klassTable_oop_load(LIR_Opr ref, LIR_Opr klass, LIR_Opr tmp);

  #include "c1_LIRAssembler_pd.hpp"
};

#endif // C1_LIRASSEMBLER_HPP
