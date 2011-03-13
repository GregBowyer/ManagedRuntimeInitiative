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
#ifndef C1_LIRGENERATOR_HPP
#define C1_LIRGENERATOR_HPP


// The classes responsible for code emission and register allocation

#include "c1_Compilation.hpp"
#include "c1_FrameMap.hpp"
#include "c1_Instruction.hpp"
#include "ciEnv.hpp"

class LIRGenerator;
class LIREmitter;
class Invoke;
class SwitchRange;
class LIRItem;

define_array(LIRItemArray, LIRItem*)
define_stack(LIRItemList, LIRItemArray)

class SwitchRange: public CompilationResourceObj {
 private:
  int _low_key;
  int _high_key;
  BlockBegin* _sux;
 public:
  SwitchRange(int start_key, BlockBegin* sux): _low_key(start_key), _high_key(start_key), _sux(sux) {}
  void set_high_key(int key) { _high_key = key; }

  int high_key() const { return _high_key; }
  int low_key() const { return _low_key; }
  BlockBegin* sux() const { return _sux; }
};

define_array(SwitchRangeArray, SwitchRange*)
define_stack(SwitchRangeList, SwitchRangeArray)


class ResolveNode;

define_array(NodeArray, ResolveNode*);
define_stack(NodeList, NodeArray);


// Node objects form a directed graph of LIR_Opr
// Edges between Nodes represent moves from one Node to its destinations
class ResolveNode: public CompilationResourceObj {
 private:
  LIR_Opr    _operand;       // the source or destinaton
  NodeList   _destinations;  // for the operand
  bool       _assigned;      // Value assigned to this Node?
  bool       _visited;       // Node already visited?
  bool       _start_node;    // Start node already visited?
  
 public:
  ResolveNode(LIR_Opr operand) 
    : _operand(operand)
    , _assigned(false)
    , _visited(false)
    , _start_node(false) {};

  // accessors
  LIR_Opr operand() const           { return _operand; }
  int no_of_destinations() const    { return _destinations.length(); }
  ResolveNode* destination_at(int i)     { return _destinations[i]; } 
  bool assigned() const             { return _assigned; }
  bool visited() const              { return _visited; }
  bool start_node() const           { return _start_node; }

  // modifiers
  void append(ResolveNode* dest)         { _destinations.append(dest); }
  void set_assigned()               { _assigned = true; }
  void set_visited()                { _visited = true; }
  void set_start_node()             { _start_node = true; }
};


// This is shared state to be used by the PhiResolver so the operand
// arrays don't have to be reallocated for reach resolution.
class PhiResolverState: public CompilationResourceObj {
  friend class PhiResolver;

 private:
  NodeList _virtual_operands; // Nodes where the operand is a virtual register
  NodeList _other_operands;   // Nodes where the operand is not a virtual register
  NodeList _vreg_table;       // Mapping from virtual register to Node

 public:
  PhiResolverState() {}

  void reset(int max_vregs);
};


// class used to move value of phi operand to phi function
class PhiResolver: public CompilationResourceObj {
 private:
  LIRGenerator*     _gen;
  PhiResolverState& _state; // temporary state cached by LIRGenerator

  ResolveNode*   _loop;
  LIR_Opr _temp;

  // access to shared state arrays
  NodeList& virtual_operands() { return _state._virtual_operands; }
  NodeList& other_operands()   { return _state._other_operands;   }
  NodeList& vreg_table()       { return _state._vreg_table;       }

  ResolveNode* create_node(LIR_Opr opr, bool source);
  ResolveNode* source_node(LIR_Opr opr)      { return create_node(opr, true); }
  ResolveNode* destination_node(LIR_Opr opr) { return create_node(opr, false); }

  void emit_move(LIR_Opr src, LIR_Opr dest);
  void move_to_temp(LIR_Opr src);
  void move_temp_to(LIR_Opr dest);
  void move(ResolveNode* src, ResolveNode* dest);

  LIRGenerator* gen() {
    return _gen;
  }

 public:
  PhiResolver(LIRGenerator* _lir_gen, int max_vregs);
  ~PhiResolver();

  void move(LIR_Opr src, LIR_Opr dest);
};


// only the classes below belong in the same file
class LIRGenerator:public InstructionVisitor{
 private:
  Compilation*  _compilation;
  ciMethod*     _method;    // method that we are compiling
  PhiResolverState  _resolver_state;
  BlockBegin*   _block;
  int           _virtual_register_number;
  Values        _instruction_for_operand;
  BitMap2D      _vreg_flags; // flags which can be set on a per-vreg basis
  LIR_List*     _lir;
  LIR_Opr       _temp_vregs[FrameMap::pd_max_temp_vregs]; // vregs for PD temps
  LIR_Opr       _cp_reg;        // CodeProfile reserved register
  LIR_Opr       _thread_reg;    // Register holding thread pointer

  LIRGenerator* gen() {
    return this;
  }

#ifdef ASSERT
  LIR_List* lir(const char * file, int line) const {
    _lir->set_file_and_line(file, line);
    return _lir;
  }
#endif
  LIR_List* lir() const {
    return _lir;
  }  

  // a simple cache of constants used within a block
  GrowableArray<LIR_Const*>       _constants;
  LIR_OprList                     _reg_for_constants;
  Values                          _unpinned_constants;

  LIR_Const*                      _card_table_base;

  friend class PhiResolver;

  // unified bailout support
  void bailout(const char* msg) const            { compilation()->bailout(msg); }
  bool bailed_out() const                        { return compilation()->bailed_out(); }

  void block_do_prolog(BlockBegin* block);
  void block_do_epilog(BlockBegin* block);

  // register allocation
  LIR_Opr rlock(Value instr);                      // lock a free register
  LIR_Opr rlock_result(Value instr);
  LIR_Opr rlock_result(Value instr, BasicType type);
  LIR_Opr rlock_byte(BasicType type);
  LIR_Opr rlock_callee_saved(BasicType type);

  // get a constant into a register and get track of what register was used
  LIR_Opr load_constant(Constant* x);
  LIR_Opr load_constant(LIR_Const* constant);

  LIR_Const* card_table_base() const { 
    if (UseGenPauselessGC) ShouldNotReachHere(); 
    return _card_table_base; 
  }

  void  set_result(Value x, LIR_Opr opr)           {
    assert(opr->is_valid(), "must set to valid value");
    assert(x->operand()->is_illegal(), "operand should never change");
    assert(!opr->is_register() || opr->is_virtual(), "should never set result to a physical register");
    x->set_operand(opr);
    assert(opr == x->operand(), "must be");
    if (opr->is_virtual()) {
      _instruction_for_operand.at_put_grow(opr->vreg_number(), x, NULL);
    }
  }
  void  set_no_result(Value x)                     { assert(!x->has_uses(), "can't have use"); x->clear_operand(); }

  friend class LIRItem;

  LIR_Opr force_to_spill(LIR_Opr value, BasicType t);

  void  profile_branch(If* if_instr, If::Condition cond);

  PhiResolverState& resolver_state() { return _resolver_state; }

  void  move_to_phi(PhiResolver* resolver, Value cur_val, Value sux_val);
  void  move_to_phi(ValueStack* cur_state);

  // code emission
  void do_ArithmeticOp_CPU_div (ArithmeticOp* x, bool isLong);
  void do_ArithmeticOp_CPU_mul (ArithmeticOp* x, bool isLong);
  void do_ArithmeticOp_CPU_addsub (ArithmeticOp* x, bool isLong);
  void do_ArithmeticOp_FPU (ArithmeticOp* x);

  // platform dependent
  LIR_Opr getThreadPointer();

  void do_RegisterFinalizer(Intrinsic* x);
  void do_getClass(Intrinsic* x);
  void do_currentThread(Intrinsic* x);
  void do_MathIntrinsic(Intrinsic* x);
  void do_ArrayCopy(Intrinsic* x);
void do_StringEquals(Intrinsic*x);
  void do_CompareAndSwap(Intrinsic* x, ValueType* type);
  void do_AttemptUpdate(Intrinsic* x);
  void do_NIOCheckIndex(Intrinsic* x);
  void do_FPIntrinsics(Intrinsic* x);

  void do_UnsafePrefetch(UnsafePrefetch* x, bool is_store);

  LIR_Opr call_runtime(BasicTypeArray* signature, LIRItemList* args, address entry, ValueType* result_type, CodeEmitInfo* info);
  LIR_Opr call_runtime(BasicTypeArray* signature, LIR_OprList* args, address entry, ValueType* result_type, CodeEmitInfo* info);

  // convenience functions
  LIR_Opr call_runtime(Value arg1, address entry, ValueType* result_type, CodeEmitInfo* info);
  LIR_Opr call_runtime(Value arg1, Value arg2, address entry, ValueType* result_type, CodeEmitInfo* info);

  void cas_helper(Intrinsic* x, ValueType* type, LIRItem* obj, LIR_Opr offset, LIRItem* cmp, LIRItem* val);

  // GC Barriers

  // generic interface

  void post_barrier(LIR_OprDesc* addr, LIR_OprDesc* new_val);

  // specific implementations

  // post barriers

  void CardTableModRef_post_barrier(LIR_OprDesc* addr, LIR_OprDesc* new_val);


  static LIR_Opr result_register_for(ValueType* type, bool callee = false);

  ciObject* get_jobject_constant(Value value);

  LIRItemList* invoke_visit_arguments(Invoke* x);
  void invoke_load_arguments(Invoke* x, LIRItemList* args, const LIR_OprList* arg_list);

  void trace_block_entry(BlockBegin* block);

  // volatile field operations are never patchable because a klass
  // must be loaded to know it's volatile which means that the offset
  // it always known as well.
  void volatile_field_store(LIR_Opr value, LIR_Address* address, CodeEmitInfo* info);
  void volatile_field_load(LIR_Address* address, LIR_Opr result, CodeEmitInfo* info);

  void put_Object_unsafe(LIR_Opr src, LIR_Opr offset, LIR_Opr data, BasicType type, bool is_volatile);
  void get_Object_unsafe(LIR_Opr dest, LIR_Opr src, LIR_Opr offset, BasicType type, bool is_volatile);

  void arithmetic_call_op (Bytecodes::Code code, LIR_Opr result, LIR_OprList* args);

  void increment_counter(address counter, int step = 1);
  void increment_counter(LIR_Address* addr, int step = 1);

  // increment a counter returning the incremented value
  LIR_Opr increment_and_return_counter(LIR_Opr base, int offset, int increment);

  // is_strictfp is only needed for mul and div (and only generates different code on i486)
  void arithmetic_op(Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, bool is_strictfp, LIR_Opr tmp, CodeEmitInfo* info = NULL);
  // machine dependent.  returns true if it emitted code for the multiply
  bool strength_reduce_multiply(LIR_Opr left, int constant, LIR_Opr result, LIR_Opr tmp);

  void store_stack_parameter (LIR_Opr opr, ByteSize offset_from_sp_in_bytes);

  void jobject2reg_with_patching(LIR_Opr r, ciObject* obj, CodeEmitInfo* info);

  // this loads the length and compares against the index
  void array_range_check          (LIR_Opr array, LIR_Opr index, CodeEmitInfo* null_check_info, CodeEmitInfo* range_check_info);
  // For java.nio.Buffer.checkIndex
  void nio_range_check            (LIR_Opr buffer, LIR_Opr index, LIR_Opr result, CodeEmitInfo* info);

  void arithmetic_op_cpu (Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, LIR_Opr tmp, CodeEmitInfo* info = NULL);
  void arithmetic_op_fpu  (Bytecodes::Code code, LIR_Opr result, LIR_Opr left, LIR_Opr right, bool is_strictfp, LIR_Opr tmp = LIR_OprFact::illegalOpr);

  void shift_op   (Bytecodes::Code code, LIR_Opr dst_reg, LIR_Opr value, LIR_Opr count, LIR_Opr tmp);

  void logic_op   (Bytecodes::Code code, LIR_Opr dst_reg, LIR_Opr left, LIR_Opr right);

void monitor_enter(LIR_Opr object,LIR_Opr tmp1,LIR_Opr tmp2,LIR_Opr mark,int mon_num,CodeEmitInfo*info_for_exception,CodeEmitInfo*info);
void monitor_exit(LIR_Opr object,LIR_Opr tmp1,LIR_Opr tmp2,int mon_num);

  void new_instance(LIR_Opr dst, ciInstanceKlass* klass, LIR_Opr size_in_bytes, LIR_Opr length_ekid, LIR_Opr akid, LIR_Opr tmp1, CodeEmitInfo* info);

  // machine dependent
  void cmp_mem_int(LIR_Condition condition, LIR_Opr base, int disp, int c, CodeEmitInfo* info);
  void cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Opr base, int disp, BasicType type, CodeEmitInfo* info);
  void cmp_reg_mem(LIR_Condition condition, LIR_Opr reg, LIR_Opr base, LIR_Opr disp, BasicType type, CodeEmitInfo* info);

  void arraycopy_helper(Intrinsic* x, int* flags, ciArrayKlass** expected_type);

  // returns a LIR_Address to address an array location.  May also
  // emit some code as part of address calculation.  If
  // needs_card_mark is true then compute the full address for use by
  // both the store and the card mark.
  LIR_Address* generate_address(LIR_Opr base,
                                LIR_Opr index, int shift,
                                int disp,
                                BasicType type);
  LIR_Address* generate_address(LIR_Opr base, int disp, BasicType type) {
    return generate_address(base, LIR_OprFact::illegalOpr, 0, disp, type);
  }
  LIR_Address* emit_array_address(LIR_Opr array_opr, LIR_Opr index_opr, BasicType type, bool needs_card_mark);

  // machine preferences and characteristics
  bool can_inline_as_constant(Value i) const;
  bool can_inline_as_constant(LIR_Const* c) const;
  bool can_store_as_constant(Value i, BasicType type) const;

  LIR_Opr safepoint_poll_register();
  void increment_invocation_counter(CodeEmitInfo* info, bool backedge = false);
  void increment_backedge_counter(CodeEmitInfo* info) {
    increment_invocation_counter(info, true);
  }

  CodeEmitInfo* state_for(Instruction* x, ValueStack* state, bool ignore_xhandler = false);
  CodeEmitInfo* state_for(Instruction* x);

  // allocates a virtual register for this instruction if
  // one isn't already allocated.  Only for Phi and Local.
  LIR_Opr operand_for_instruction(Instruction *x);

  void set_block(BlockBegin* block)              { _block = block; }

  void block_prolog(BlockBegin* block);
  void block_epilog(BlockBegin* block);

  void do_root (Instruction* instr);
  // This is called for each node in tree; the walk stops if a root is reached
  LIR_Opr walk    (Instruction* instr) {
    if (ciEnv::current()->failing()) return LIR_OprFact::illegalOpr;

    InstructionMark im(compilation(), instr);
    //stop walk when encounter a root
    if ((instr->is_pinned() && instr->as_Phi() == NULL)|| instr->operand()->is_valid()) {
      assert(instr->operand() != LIR_OprFact::illegalOpr || instr->as_Constant() != NULL, "this root has not yet been visited");
    } else {
      assert(instr->subst() == instr, "shouldn't have missed substitution");
      instr->visit(this);
      // assert(instr->use_count() > 0 || instr->as_Phi() != NULL, "leaf instruction must have a use");
    }
return instr->operand();
  }

  void bind_block_entry(BlockBegin* block);
  void start_block(BlockBegin* block);

  LIR_Opr new_register(BasicType type);
  LIR_Opr new_register(Value value)              { return new_register(as_BasicType(value->type())); }
  LIR_Opr new_register(ValueType* type)          { return new_register(as_BasicType(type)); }

  friend class LinearScan;

  // return a cached temporary operand
  LIR_Opr get_temp(int i, BasicType type) {
    if (i < FrameMap::pd_max_temp_vregs) {
      assert0(_temp_vregs[i]->type_register() == type);
return _temp_vregs[i];
    } else {
return new_register(type);
    }
  }

  // returns a register suitable for doing pointer math
  LIR_Opr new_pointer_register() {
    return new_register(T_LONG);
  }

  static LIR_Condition lir_cond(If::Condition cond) {
    LIR_Condition l;
    switch (cond) {
    case If::eql: l = lir_cond_equal;        break;
    case If::neq: l = lir_cond_notEqual;     break;
    case If::lss: l = lir_cond_less;         break;
    case If::leq: l = lir_cond_lessEqual;    break;
    case If::geq: l = lir_cond_greaterEqual; break;
    case If::gtr: l = lir_cond_greater;      break;
    };
    return l;
  }

  void init();

  SwitchRangeArray* create_lookup_ranges(TableSwitch* x);
  SwitchRangeArray* create_lookup_ranges(LookupSwitch* x);
  void do_SwitchRanges(SwitchRangeArray* x, LIR_Opr value, BlockBegin* default_sux);

 public:
  Compilation*  compilation() const              { return _compilation; }
  FrameMap*     frame_map() const                { return _compilation->frame_map(); }
  ciMethod*     method() const                   { return _method; }
  BlockBegin*   block() const                    { return _block; }
  IRScope*      scope() const                    { return block()->scope(); }

  struct BC : public BlockClosure {
    LIRGenerator *const _thsi;
    BC(LIRGenerator *thsi) : _thsi(thsi) { }
    void block_do(BlockBegin* block) { _thsi->block_do(block); }
  } _bc;

  int max_virtual_register_number() const        { return _virtual_register_number; }

  void block_do(BlockBegin* block);

  // Flags that can be set on vregs
  enum VregFlag {
      must_start_in_memory = 0  // needs to be assigned a memory location at beginning, but may then be loaded in a register
 #if 0 // AZUL: the callee_saved flag is only used for SPARC. For our x86-64 we
       // don't need to special case byte registers as we use a REX prefix to
       // make the registers uniformly accessed
    , callee_saved     = 1    // must be in a callee saved register
    , byte_reg         = 2    // must be in a byte register
 #endif
    , num_vreg_flags

  };

  LIRGenerator(Compilation* compilation, ciMethod* method)
    : _compilation(compilation)
    , _method(method)
    , _virtual_register_number(LIR_OprDesc::vreg_base)
,_vreg_flags(NULL,0,num_vreg_flags),_bc(this){
    init();
  }

  // for virtual registers, maps them back to Phi's or Local's
  Instruction* instruction_for_opr(LIR_Opr opr);
  Instruction* instruction_for_vreg(int reg_num);

  void set_vreg_flag   (int vreg_num, VregFlag f);
  bool is_vreg_flag_set(int vreg_num, VregFlag f);
  void set_vreg_flag   (LIR_Opr opr,  VregFlag f) { set_vreg_flag(opr->vreg_number(), f); }
  bool is_vreg_flag_set(LIR_Opr opr,  VregFlag f) { return is_vreg_flag_set(opr->vreg_number(), f); }

  // statics
  static LIR_Opr exceptionOopOpr();
  static LIR_Opr exceptionPcOpr();
  static LIR_Opr divInOpr();
  static LIR_Opr divOutOpr();
  static LIR_Opr remOutOpr();
static LIR_Opr divInOprLong();
static LIR_Opr divOutOprLong();
static LIR_Opr remOutOprLong();
  static LIR_Opr shiftCountOpr();
  LIR_Opr syncTempOpr();

  // visitor functionality
  virtual void do_Phi            (Phi*             x);
  virtual void do_Local          (Local*           x);
  virtual void do_Constant       (Constant*        x);
  virtual void do_LoadField      (LoadField*       x);
  virtual void do_StoreField     (StoreField*      x);
  virtual void do_ArrayLength    (ArrayLength*     x);
  virtual void do_LoadIndexed    (LoadIndexed*     x);
  virtual void do_StoreIndexed   (StoreIndexed*    x);
  virtual void do_NegateOp       (NegateOp*        x);
  virtual void do_ArithmeticOp   (ArithmeticOp*    x);
  virtual void do_ShiftOp        (ShiftOp*         x);
  virtual void do_LogicOp        (LogicOp*         x);
  virtual void do_CompareOp      (CompareOp*       x);
  virtual void do_IfOp           (IfOp*            x);
  virtual void do_Convert        (Convert*         x);
  virtual void do_NullCheck      (NullCheck*       x);
  virtual void do_Invoke         (Invoke*          x);
  virtual void do_NewInstance    (NewInstance*     x);
  virtual void do_NewTypeArray   (NewTypeArray*    x);
  virtual void do_NewObjectArray (NewObjectArray*  x);
  virtual void do_NewMultiArray  (NewMultiArray*   x);
  virtual void do_CheckCast      (CheckCast*       x);
  virtual void do_InstanceOf     (InstanceOf*      x);
  virtual void do_MonitorEnter   (MonitorEnter*    x);
  virtual void do_MonitorExit    (MonitorExit*     x);
  virtual void do_Intrinsic      (Intrinsic*       x);
  virtual void do_BlockBegin     (BlockBegin*      x);
  virtual void do_Goto           (Goto*            x);
  virtual void do_If             (If*              x);
  virtual void do_IfInstanceOf   (IfInstanceOf*    x);
  virtual void do_TableSwitch    (TableSwitch*     x);
  virtual void do_LookupSwitch   (LookupSwitch*    x);
  virtual void do_Return         (Return*          x);
  virtual void do_Throw          (Throw*           x);
  virtual void do_Base           (Base*            x);
  virtual void do_OsrEntry       (OsrEntry*        x);
  virtual void do_ExceptionObject(ExceptionObject* x);
  virtual void do_UnsafeGetRaw   (UnsafeGetRaw*    x);
  virtual void do_UnsafePutRaw   (UnsafePutRaw*    x);
  virtual void do_UnsafeGetObject(UnsafeGetObject* x);
  virtual void do_UnsafePutObject(UnsafePutObject* x);
  virtual void do_UnsafePrefetchRead (UnsafePrefetchRead*  x);
  virtual void do_UnsafePrefetchWrite(UnsafePrefetchWrite* x);
  virtual void do_ProfileCall    (ProfileCall*     x);
  virtual void do_ProfileCounter (ProfileCounter*  x);
  virtual void do_IncrementCount (IncrementCount*  x);
};


class LIRItem: public CompilationResourceObj {
 private:
  Value         _value;
  LIRGenerator* _gen;
  LIR_Opr       _result;
  enum DestroyedState {
    not_destroyed, awaiting_copy, destroyed
  };
  DestroyedState _destroys_register;

  LIRGenerator* gen() const { return _gen; }

 public:
  LIRItem(Value value, LIRGenerator* gen) {
    _destroys_register = not_destroyed;
    _gen = gen;
    set_instruction(value);
  }

  LIRItem(LIRGenerator* gen) {
    _destroys_register = not_destroyed;
    _gen = gen;
    _result = LIR_OprFact::illegalOpr;
    set_instruction(NULL);
  }

  void set_instruction(Value value) {
    _value = value;
    _result = LIR_OprFact::illegalOpr;
    if (_value != NULL) {
      _gen->walk(_value);
      _result = _value->operand();
    }
  }

  Value value() const          { return _value;          }
  ValueType* type() const      { return value()->type(); }
  LIR_Opr result()             {
assert(_destroys_register==not_destroyed||(!_result->is_register()||_result->is_virtual()),
           "shouldn't use set_destroys_register with physical regsiters");
if(_destroys_register==awaiting_copy&&_result->is_register()){
LIR_Opr new_result=_gen->new_register(type())->set_destroyed();
      gen()->lir()->move(_result, new_result);
      _destroys_register = destroyed;
_result=new_result;
    }
    return _result;
  }

  void set_result(LIR_Opr opr);

  void load_item();
  void load_byte_item();
  void load_nonconstant();
  // load any values which can't be expressed as part of a single store instruction
  void load_for_store(BasicType store_type);
  void load_item_force(LIR_Opr reg);

  void dont_load_item() {
    // do nothing
  }

  void set_destroys_register() {
    assert( _destroys_register == not_destroyed, "register already destroyed" );
    _destroys_register = awaiting_copy;
  }

  bool is_constant() const { return value()->as_Constant() != NULL; }
  bool is_stack()          { return result()->is_stack(); }
  bool is_register()       { return result()->is_register(); }

  ciObject* get_jobject_constant() const;
  jint      get_jint_constant() const;
  jlong     get_jlong_constant() const;
  jfloat    get_jfloat_constant() const;
  jdouble   get_jdouble_constant() const;
  jint      get_address_constant() const;
};

#endif // C1_LIRGENERATOR_HPP
