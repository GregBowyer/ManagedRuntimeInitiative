/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


// Portions of code courtesy of Clifford Click

// Optimization - Graph Style

#include "callnode.hpp"
#include "connode.hpp"
#include "machnode.hpp"
#include "matcher.hpp"
#include "phaseX.hpp"
#include "regalloc.hpp"
#include "rootnode.hpp"
#include "runtime.hpp"
#include "stubRoutines.hpp"
#include "sharedRuntime.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

//=============================================================================
uint StartNode::size_of() const { return sizeof(*this); }
uint StartNode::cmp( const Node &n ) const
{ return _domain == ((StartNode&)n)._domain; }
const Type *StartNode::bottom_type() const { return _domain; }
const Type *StartNode::Value(PhaseTransform *phase) const { return _domain; }
void StartNode::dump_spec(outputStream *st) const { st->print(" #"); _domain->dump_on(st);}

//------------------------------Ideal------------------------------------------
Node *StartNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//------------------------------calling_convention-----------------------------
void StartNode::calling_convention(BasicType*sig_bt,VReg::VR*parm_regs,uint argcnt)const{
  Matcher::calling_convention( sig_bt, parm_regs, argcnt, false );
}

//------------------------------Registers--------------------------------------
const RegMask &StartNode::in_RegMask(uint) const { 
  return RegMask::Empty;
}

//------------------------------match------------------------------------------
// Construct projections for incoming parameters, and their RegMask info
Node *StartNode::match( const ProjNode *proj, const Matcher *match ) {
  switch (proj->_con) {
  case TypeFunc::Control: 
  case TypeFunc::I_O:
  case TypeFunc::Memory:
    return new (match->C, 1) MachProjNode(this,proj->_con,RegMask::Empty,MachProjNode::unmatched_proj);
  case TypeFunc::FramePtr:
    return new (match->C, 1) MachProjNode(this,proj->_con,Matcher::c_frame_ptr_mask, Op_RegP);
  case TypeFunc::ReturnAdr:
    return new (match->C, 1) MachProjNode(this,proj->_con,match->_return_addr_mask,Op_RegP);
  case TypeFunc::Parms:
  default: {
      uint parm_num = proj->_con - TypeFunc::Parms;
      const Type *t = _domain->field_at(proj->_con);
      if (t->base() == Type::Half)  // 2nd half of Longs and Doubles
        return new (match->C, 1) ConNode(Type::TOP);
      uint ideal_reg = Matcher::base2reg[t->base()];
      RegMask &rm = match->_calling_convention_mask[parm_num];
      return new (match->C, 1) MachProjNode(this,proj->_con,rm,ideal_reg);
    }
  } 
  return NULL;
}

//------------------------------StartOSRNode----------------------------------
// The method start node for an on stack replacement adapter

//------------------------------osr_domain-----------------------------
const TypeTuple *StartOSRNode::osr_domain() {
  const Type **fields = TypeTuple::fields(2);
  fields[TypeFunc::Parms+0] = TypeRawPtr::BOTTOM;  // address of osr buffer

  return TypeTuple::make(TypeFunc::Parms+1, fields);
}

//=============================================================================
const char * const ParmNode::names[TypeFunc::Parms+1] = {
  "Control", "I_O", "Memory", "FramePtr", "ReturnAdr", "Parms"
};

void ParmNode::dump_spec(outputStream *st) const {
  if( _con < TypeFunc::Parms ) {
    st->print(names[_con]);
  } else {
    st->print("Parm%d: ",_con-TypeFunc::Parms);
bottom_type()->dump_on(st);
  }
}

uint ParmNode::ideal_reg() const {
  switch( _con ) {
  case TypeFunc::Control  : // fall through
  case TypeFunc::I_O      : // fall through
  case TypeFunc::Memory   : return 0;
  case TypeFunc::FramePtr : // fall through
  case TypeFunc::ReturnAdr: return Op_RegP;      
  default                 : assert( _con > TypeFunc::Parms, "" ); 
    // fall through
  case TypeFunc::Parms    : {
    // Type of argument being passed
    const Type *t = in(0)->as_Start()->_domain->field_at(_con);
    return Matcher::base2reg[t->base()];
  }
  }
  ShouldNotReachHere();
  return 0;
}

//=============================================================================
ReturnNode::ReturnNode(uint edges, Node *cntrl, Node *i_o, Node *memory, Node *frameptr, Node *retadr ) : Node(edges) { 
  init_req(TypeFunc::Control,cntrl); 
  init_req(TypeFunc::I_O,i_o); 
  init_req(TypeFunc::Memory,memory); 
  init_req(TypeFunc::FramePtr,frameptr);
  init_req(TypeFunc::ReturnAdr,retadr); 
}

static Node *tail_clone_helper( Node *sfpt, Node *phi, int idx ) {
  return (phi->is_Phi() && phi->in(0)==sfpt) ? phi->in(idx) : phi;
}
Node *ReturnNode::Ideal(PhaseGVN *phase, bool can_reshape){
  if (remove_dead_region(phase, can_reshape))  return this;

  // See if we should tail-clone this return.  Useful if there's an immediate
  // merge just before-hand, and we're merging some different constants.
Node*sfpt=in(TypeFunc::Control);
if(!sfpt)return NULL;
  if( can_reshape && sfpt->is_Region() && sfpt->req() > 2 ) {
PhaseIterGVN*igvn=phase->is_IterGVN();
    uint i,phicnt=0;            // Must have at least 1 phi
for(i=1;i<req();i++){
Node*n=in(i);
      if( n->Opcode() == Op_Parm ) continue;
      if( !n->is_Phi() || n->in(0) != sfpt )
        break;
      phicnt++;
    }
    if( i == req() && phicnt > 0 ) {
Node*R=NULL;
for(i=1;i<sfpt->req();i++){
        if( !sfpt->in(i) ) continue;
        R = new (igvn->C, req()) ReturnNode(req(), sfpt->in(i), 
                                            tail_clone_helper(sfpt,in(TypeFunc::I_O),i),
                                            tail_clone_helper(sfpt,in(TypeFunc::Memory),i),
                                            tail_clone_helper(sfpt,in(TypeFunc::FramePtr),i),
                                            tail_clone_helper(sfpt,in(TypeFunc::ReturnAdr),i));
        if( req() > TypeFunc::Parms ) 
          R->init_req(TypeFunc::Parms, tail_clone_helper(sfpt,in(TypeFunc::Parms),i));
igvn->register_new_node_with_optimizer(R);
        if( this->outcnt() > 0 )  igvn->subsume_node_keep_old(this,R);
        else                      igvn->C->root()->add_req(R);
      }
      
igvn->remove_globally_dead_node(this);
      return R;
    }
  }

  // Skip over a prior safepoint; we don't need one just before we exit.
  // (unless it's the only 1 in the method).
  if( sfpt->is_SafePoint() &&
      !(sfpt->in(0)->is_Proj() && sfpt->in(0)->in(0)->is_Start())) {
    set_req(TypeFunc::Control,sfpt->in(TypeFunc::Control));
    sfpt->set_req(TypeFunc::FramePtr,0); // Flag safept as dead
    if( can_reshape ) phase->is_IterGVN()->_worklist.push(sfpt);
    else              phase->C->record_for_igvn(sfpt);
    return this;
  }
  return NULL;
}

const Type *ReturnNode::Value( PhaseTransform *phase ) const { 
  return ( phase->type(in(TypeFunc::Control)) == Type::TOP)
    ? Type::TOP
    : Type::BOTTOM;
}

// Do we Match on this edge index or not?  No edges on return nodes
uint ReturnNode::match_edge(uint idx) const {
  return 0;
}


#ifndef PRODUCT
void ReturnNode::dump_req() const {
  outputStream* out = Compile::current()->out();
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
if(i==TypeFunc::Parms)C2OUT->print("returns");
if(in(i))C2OUT->print("%c%d ",Compile::current()->node_arena()->contains(in(i))?' ':'o',in(i)->_idx);
else C2OUT->print("_ ");
  }
}
#endif

//=============================================================================
RethrowNode::RethrowNode(
  Node* cntrl,
  Node* i_o,
  Node* memory,
  Node* frameptr,
Node*ret_adr
) : Node(TypeFunc::Parms) { 
  init_req(TypeFunc::Control  , cntrl    ); 
  init_req(TypeFunc::I_O      , i_o      ); 
  init_req(TypeFunc::Memory   , memory   ); 
  init_req(TypeFunc::FramePtr , frameptr );
  init_req(TypeFunc::ReturnAdr, ret_adr);
}

Node *RethrowNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL; 
}

const Type *RethrowNode::Value( PhaseTransform *phase ) const { 
  return (phase->type(in(TypeFunc::Control)) == Type::TOP)
    ? Type::TOP
    : Type::BOTTOM;
}

uint RethrowNode::match_edge(uint idx) const {
  return 0;
}

#ifndef PRODUCT
void RethrowNode::dump_req() const {
  outputStream* out = Compile::current()->out();
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
if(i==TypeFunc::Parms)C2OUT->print("exception");
if(in(i))C2OUT->print("%c%d ",Compile::current()->node_arena()->contains(in(i))?' ':'o',in(i)->_idx);
else C2OUT->print("_ ");
  }
}
#endif

//=============================================================================
JVMState::JVMState(ciMethod* method, JVMState* caller) {
  assert(method != NULL, "must be valid call site");
  _method = method;
  debug_only(_bci = -99);  // random garbage value
  debug_only(_map = (SafePointNode*)-1);
  _caller = caller;
  _depth  = 1 + (caller == NULL ? 0 : caller->depth());
  _locoff = TypeFunc::Parms;
  _stkoff = _locoff + _method->max_locals();
  _monoff = _stkoff + _method->max_stack();
  _endoff = _monoff;
  _sp = 0;
}
JVMState::JVMState(int stack_size) {
  _method = NULL;
  _bci = InvocationEntryBci;
  debug_only(_map = (SafePointNode*)-1);
  _caller = NULL;
  _depth  = 1;
  _locoff = TypeFunc::Parms;
  _stkoff = _locoff;
  _monoff = _stkoff + stack_size;
  _endoff = _monoff;
  _sp = 0;
}

//--------------------------------of_depth-------------------------------------
JVMState* JVMState::of_depth(int d) const {
  const JVMState* jvmp = this;
  assert(0 < d && (uint)d <= depth(), "oob");
  for (int skip = depth() - d; skip > 0; skip--) {
    jvmp = jvmp->caller();
  }
  assert(jvmp->depth() == (uint)d, "found the right one");
  return (JVMState*)jvmp;
}

//-----------------------------same_calls_as-----------------------------------
bool JVMState::same_calls_as(const JVMState* that) const {
  if (this == that)                    return true;
  if (this->depth() != that->depth())  return false;
  const JVMState* p = this;
  const JVMState* q = that;
  for (;;) {
    if (p->_method != q->_method)    return false;
    if (p->_method == NULL)          return true;   // bci is irrelevant
    if (p->_bci    != q->_bci)       return false;
    p = p->caller();
    q = q->caller();
    if (p == q)                      return true;
    assert(p != NULL && q != NULL, "depth check ensures we don't run off end");
  }
}

//------------------------------debug_start------------------------------------
uint JVMState::debug_start()  const {
  debug_only(JVMState* jvmroot = of_depth(1));
  assert(jvmroot->locoff() <= this->locoff(), "youngest JVMState must be last");
  return of_depth(1)->locoff();
}

//-------------------------------debug_end-------------------------------------
uint JVMState::debug_end() const {
  debug_only(JVMState* jvmroot = of_depth(1));
  assert(jvmroot->endoff() <= this->endoff(), "youngest JVMState must be last");
  return endoff();
}

//------------------------------debug_depth------------------------------------
uint JVMState::debug_depth() const {
  uint total = 0;
  for (const JVMState* jvmp = this; jvmp != NULL; jvmp = jvmp->caller()) {
    total += jvmp->debug_size();
  }
  return total;
}

//------------------------------format_helper----------------------------------
// Given an allocation (a Chaitin object) and a Node decide if the Node carries
// any defined value or not.  If it does, print out the register or constant.
#ifndef PRODUCT
static void format_helper( PhaseRegAlloc *regalloc, outputStream* st, Node *n, const char *msg, uint i ) {
  if (n == NULL) { st->print(" NULL"); return; }
if(OptoReg::is_valid(regalloc->get_reg(n))){//Check for undefined
    char buf[50];
    regalloc->dump_register(n,buf);
    st->print(" %s%d]=%s",msg,i,buf);
  } else {                      // No register, but might be constant  
    const Type *t = n->bottom_type();
    switch (t->base()) {
    case Type::Int:  
      st->print(" %s%d]=#"INT32_FORMAT,msg,i,t->is_int()->get_con()); 
      break;
    case Type::AnyPtr: 
      assert( t == TypePtr::NULL_PTR, "" );
      st->print(" %s%d]=#NULL",msg,i);
      break;
    case Type::AryPtr: 
    case Type::KlassPtr:
    case Type::InstPtr: 
st->print(" %s%d]=#Ptr%p",msg,i,t->isa_oopptr()->const_oop());
      break;
    case Type::RawPtr: 
st->print(" %s%d]=#Raw%p",msg,i,t->is_rawptr());
      break;
    case Type::DoubleCon:
      st->print(" %s%d]=#%fD",msg,i,t->is_double_constant()->_d);
      break;
    case Type::FloatCon:
      st->print(" %s%d]=#%fF",msg,i,t->is_float_constant()->_f);
      break;
    case Type::Long:
st->print(" %s%d]=#%lld",msg,i,t->is_long()->get_con());
      break;
    case Type::Half:
    case Type::Top:  
      st->print(" %s%d]=_",msg,i);
      break;
    default: ShouldNotReachHere();
    }
  }
}
#endif

void JVMState::dump_spec(outputStream *st) const { 
  if( _method ) {
    _method->print_short_name(st);
    st->print(" @ bci:%d ",_bci);
  } else {
st->print(" runtime stub ");
  }
  if (caller() != NULL)  caller()->dump_spec(st);
}

void JVMState::dump_on(outputStream* st) const {
  if (_map && !((uintptr_t)_map & 1)) {
    if (_map->len() > _map->req()) {  // _map->has_exceptions()
      Node* ex = _map->in(_map->req());  // _map->next_exception()
      // skip the first one; it's already being printed
      while (ex != NULL && ex->len() > ex->req()) {
        ex = ex->in(ex->req());  // ex->next_exception()
        ex->dump(1);
      }
    }
    _map->dump(2);
  }
  st->print("JVMS depth=%d loc=%d stk=%d mon=%d end=%d mondepth=%d sp=%d bci=%d method=",
             depth(), locoff(), stkoff(), monoff(), endoff(), monitor_depth(), sp(), bci());
  if (_method == NULL) {
    st->print_cr("(none)");
  } else {
    _method->print_name(st);
    st->cr();
    if (bci() >= 0 && bci() < _method->code_size()) {
      st->print("    bc: ");
_method->print_codes(bci(),bci()+1,st);
    }
  }
  if (caller() != NULL) {
    caller()->dump_on(st);
  }
}

// Extra way to dump a jvms from the debugger,
// to avoid a bug with C++ member function calls.
void dump_jvms(JVMState* jvms) {
  jvms->dump();
}

//--------------------------clone_shallow--------------------------------------
JVMState* JVMState::clone_shallow(Compile* C) const {
  JVMState* n = has_method() ? new (C) JVMState(_method, _caller) : new (C) JVMState(0);
  n->set_bci(_bci);
  n->set_locoff(_locoff);
  n->set_stkoff(_stkoff);
  n->set_monoff(_monoff);
  n->set_endoff(_endoff);
  n->set_sp(_sp);
  n->set_map(_map);
  return n;
}

//---------------------------clone_deep----------------------------------------
JVMState* JVMState::clone_deep(Compile* C) const {
  JVMState* n = clone_shallow(C);
  for (JVMState* p = n; p->_caller != NULL; p = p->_caller) {
    p->_caller = p->_caller->clone_shallow(C);
  }
  assert(n->depth() == depth(), "sanity");
  assert(n->debug_depth() == debug_depth(), "sanity");
  return n;
}

//=============================================================================
uint CallNode::cmp( const Node &n ) const
{ return _tf == ((CallNode&)n)._tf && _jvms == ((CallNode&)n)._jvms; }
void CallNode::dump_req() const { 
  outputStream* out = Compile::current()->out();
  // Dump the required inputs, enclosed in '(' and ')'
  uint i;                       // Exit value of loop
  for( i=0; i<req(); i++ ) {    // For all required inputs
if(i==TypeFunc::Parms)C2OUT->print("(");
if(in(i))C2OUT->print("%c%d ",Compile::current()->node_arena()->contains(in(i))?' ':'o',in(i)->_idx);
else C2OUT->print("_ ");
  }
C2OUT->print(")");
}

void CallNode::dump_spec(outputStream *st) const { 
  st->print(" "); 
  tf()->dump_on(st);
  if (jvms() != NULL)  jvms()->dump_spec(st);
st->print(", ");
  if( _cloned_in_citypeflow )
st->print("ciTypeFlow_cloned, ");
SafePointNode::dump_spec(st);
}

const Type *CallNode::bottom_type() const { return tf()->range(); }
const Type *CallNode::Value(PhaseTransform *phase) const { 
  if (phase->type(in(0)) == Type::TOP)  return Type::TOP;
  return tf()->range(); 
}

//------------------------------calling_convention-----------------------------
void CallNode::calling_convention(BasicType*sig_bt,VReg::VR*parm_regs,uint argcnt)const{
  // Use the standard compiler calling convention 
  Matcher::calling_convention( sig_bt, parm_regs, argcnt, true );
}


//------------------------------match------------------------------------------
// Construct projections for control, I/O, memory-fields, ..., and
// return result(s) along with their RegMask info
Node *CallNode::match( const ProjNode *proj, const Matcher *match ) {
  switch (proj->_con) {
  case TypeFunc::Control: 
  case TypeFunc::I_O:
  case TypeFunc::Memory:
    return new (match->C, 1) MachProjNode(this,proj->_con,RegMask::Empty,MachProjNode::unmatched_proj);

  case TypeFunc::Parms+1:       // For LONG & DOUBLE returns
    assert(tf()->_range->field_at(TypeFunc::Parms+1) == Type::HALF, "");
    // 2nd half of doubles and longs
    return new (match->C, 1) MachProjNode(this,proj->_con, RegMask::Empty, (uint)OptoReg::Bad);

  case TypeFunc::Parms: {       // Normal returns
    uint ideal_reg = Matcher::base2reg[tf()->range()->field_at(TypeFunc::Parms)->base()];
VReg::VR rreg=match->return_value(ideal_reg,true);//Calls into compiled Java code
RegMask rm=RegMask(OptoReg::Name(rreg));
    return new (match->C, 1) MachProjNode(this,proj->_con,rm,ideal_reg);
  }

  case TypeFunc::ReturnAdr:
  case TypeFunc::FramePtr:
  default:
    ShouldNotReachHere();
  }
  return NULL;
}

// Do we Match on this edge index or not?  Match no edges
uint CallNode::match_edge(uint idx) const {
  return 0;
}

//=============================================================================
uint CallJavaNode::size_of() const { return sizeof(*this); }
uint CallJavaNode::cmp( const Node &n ) const { 
  CallJavaNode &call = (CallJavaNode&)n;
  return CallNode::cmp(call) && _method == call._method; 
}
void CallJavaNode::dump_spec(outputStream *st) const { 
  if( _method ) _method->print_short_name(st);
  CallNode::dump_spec(st);
}

//=============================================================================
uint CallStaticJavaNode::size_of() const { return sizeof(*this); }
uint CallStaticJavaNode::cmp( const Node &n ) const { 
  CallStaticJavaNode &call = (CallStaticJavaNode&)n;
  return CallJavaNode::cmp(call); 
}

//----------------------------uncommon_trap_request----------------------------
// If this is an uncommon trap, return the request code, else zero.
int CallStaticJavaNode::uncommon_trap_request() const {
  if (_name != NULL && !strcmp(_name, "uncommon_trap")) {
    return extract_uncommon_trap_request(this);
  }
  return 0;
}
int CallStaticJavaNode::extract_uncommon_trap_request(const Node* call) {
#ifndef PRODUCT
  if (!(call->req() > TypeFunc::Parms &&
        call->in(TypeFunc::Parms) != NULL &&
        call->in(TypeFunc::Parms)->is_Con())) {
    assert(_in_dump_cnt != 0, "OK if dumping");
    tty->print("[bad uncommon trap]");
    return 0;
  }
#endif
  return call->in(TypeFunc::Parms)->bottom_type()->is_int()->get_con();
}

void CallStaticJavaNode::dump_spec(outputStream *st) const { 
  st->print("# Static ");
if(_name){
st->print(_name);
    st->print(" ");
  }
  CallJavaNode::dump_spec(st);
}

//=============================================================================
uint CallDynamicJavaNode::size_of() const { return sizeof(*this); }
uint CallDynamicJavaNode::cmp( const Node &n ) const { 
  CallDynamicJavaNode &call = (CallDynamicJavaNode&)n;
  return CallJavaNode::cmp(call); 
}
void CallDynamicJavaNode::dump_spec(outputStream *st) const { 
  st->print("# Dynamic ");
  CallJavaNode::dump_spec(st);
}

//=============================================================================
uint CallRuntimeNode::size_of() const { return sizeof(*this); }
uint CallRuntimeNode::cmp( const Node &n ) const { 
  CallRuntimeNode &call = (CallRuntimeNode&)n;
  return CallNode::cmp(call) && !strcmp(_name,call._name);
}
void CallRuntimeNode::dump_spec(outputStream *st) const { 
  st->print("# "); 
  st->print(_name);
  CallNode::dump_spec(st);
}

//------------------------------calling_convention-----------------------------
void CallRuntimeNode::calling_convention(BasicType*sig_bt,VReg::VR*parm_regs,uint argcnt)const{
Matcher::calling_convention(sig_bt,parm_regs,argcnt,true);
}

//=============================================================================
//------------------------------calling_convention-----------------------------


//=============================================================================
void CallLeafNode::dump_spec(outputStream *st) const {
  st->print("# ");
  st->print(_name);
  CallNode::dump_spec(st);
}

//=============================================================================

void SafePointNode::set_local(JVMState* jvms, uint idx, Node *c) {
  assert(verify_jvms(jvms), "jvms must match");
  int loc = jvms->locoff() + idx;
  if (in(loc)->is_top() && idx > 0 && !c->is_top() ) {
    // If current local idx is top then local idx - 1 could
    // be a long/double that needs to be killed since top could
    // represent the 2nd half ofthe long/double.
    uint ideal = in(loc -1)->ideal_reg();
    if (ideal == Op_RegD || ideal == Op_RegL) {
      // set other (low index) half to top
      set_req(loc - 1, in(loc));
    }
  }
  set_req(loc, c);
}

uint SafePointNode::size_of() const { return sizeof(*this); }
uint SafePointNode::cmp( const Node &n ) const { 
  return (&n == this);          // Always fail except on self
}

//-------------------------set_next_exception----------------------------------
void SafePointNode::set_next_exception(SafePointNode* n) {
  assert(n == NULL || n->Opcode() == Op_SafePoint, "correct value for next_exception");
  if (len() == req()) {
    if (n != NULL)  add_prec(n);
  } else {
    set_prec(req(), n);
  }
}


//----------------------------next_exception-----------------------------------
SafePointNode* SafePointNode::next_exception() const {
  if (len() == req()) {
    return NULL;
  } else {
    Node* n = in(req());
    assert(n == NULL || n->Opcode() == Op_SafePoint, "no other uses of prec edges");
    return (SafePointNode*) n;
  }
}


//------------------------------Ideal------------------------------------------
// Skip over any collapsed Regions
Node *SafePointNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  if (remove_dead_region(phase, can_reshape))  return this;

  return NULL; 
}

//------------------------------Identity---------------------------------------
// Remove obviously duplicate safepoints
Node *SafePointNode::Identity( PhaseTransform *phase ) {

  // Also remove "dead" safepoints - those with no control users but still
  // with lingering memory users.
  if( !in(TypeFunc::FramePtr) ) 
    return in(TypeFunc::Control);

  // If you have back to back safepoints, remove one
  if( in(TypeFunc::Control)->is_SafePoint() )
    return in(TypeFunc::Control);

  // Same for back-to-back Safepoint after a Call.
  if( in(0)->is_Proj() ) {
    Node *n0 = in(0)->in(0);
    // Check if he is a call projection (except Leaf Call)
    if( n0->is_Catch() ) {
      n0 = n0->in(0)->in(0);
      assert( n0->is_Call(), "expect a call here" );
    }
    if( n0->is_Call() && n0->as_Call()->guaranteed_safepoint() ) {
      // Useless Safepoint, so remove it
      return in(TypeFunc::Control);
    }
  }

  // Also remove Safepoints that consume the state of a CompareAndSwap.
  // These safepoints keep alive the manifested integer result of a CAS as
  // valid Java state - meaning the success/fail CAS-flag result needs to be
  // converted into a 0/1 int.  This is expensive!  And typically such
  // Safepoints are for the retry loop of some tight CAS spin loop and will
  // almost never be taken.  I.e., I'm willing to lose the safepoint in a CAS
  // retry loop.
  Node *region,*iffalse,*iftrue,*iff,*bol,*sc;
  if( (region =  this->in(0)) && region ->is_Region () && region->req() > 2 &&
      (iffalse=region->in(1)) && iffalse->is_IfFalse() &&
      (iftrue =region->in(2)) && iftrue ->is_IfTrue () &&
      (iff    =iftrue->in(0)) && iff    ->is_If     () &&
      (bol    =iff   ->in(1)) && bol    ->is_Bool   () &&
      (sc     =bol   ->in(1)) && sc     ->is_LoadStore() ) {
    return in(TypeFunc::Control);
  }

  return this;
}

//------------------------------Value------------------------------------------
const Type *SafePointNode::Value( PhaseTransform *phase ) const {
  if( phase->type(in(0)) == Type::TOP ) return Type::TOP;
  if( phase->eqv( in(0), this ) ) return Type::TOP; // Dead infinite loop
  return Type::CONTROL;
}

void SafePointNode::dump_spec(outputStream *st) const { 
  if( _jvms && _cpd && _jvms->bci() >= 0 ) _cpd->print_line(bc(),st);
else st->print("no CPData_Invoke???");
}

const RegMask &SafePointNode::in_RegMask(uint idx) const { 
  if( idx < TypeFunc::Parms ) return RegMask::Empty;
  // Values outside the domain represent debug info
  return *(Compile::current()->matcher()->idealreg2debugmask[in(idx)->ideal_reg()]);
}
const RegMask &SafePointNode::out_RegMask() const {
  return RegMask::Empty;
}


void SafePointNode::grow_stack(JVMState* jvms, uint grow_by) {
  assert((int)grow_by > 0, "sanity");
  int monoff = jvms->monoff();
  int endoff = jvms->endoff();
  assert(endoff == (int)req(), "no other states or debug info after me");
  Node* top = Compile::current()->top();
  for (uint i = 0; i < grow_by; i++) {
    ins_req(monoff, top);
  }
  jvms->set_monoff(monoff + grow_by);
  jvms->set_endoff(endoff + grow_by);
}

void SafePointNode::push_monitor(Node *obj) {
  // Add a LockNode, which points to Object being locked.
  assert(req() == jvms()->endoff(), "correct sizing");
add_req(obj);
  jvms()->set_endoff(req());
}

void SafePointNode::pop_monitor() {
  // Delete last monitor from debug info
  debug_only(int num_before_pop = jvms()->nof_monitors());
  int endoff = jvms()->endoff();
int new_endoff=endoff-1;
  jvms()->set_endoff(new_endoff);
  while (endoff > new_endoff)  del_req(--endoff);
  assert(jvms()->nof_monitors() == num_before_pop-1, "");
}

Node *SafePointNode::peek_monitor_obj() const {
  int mon = jvms()->nof_monitors() - 1;
  assert(mon >= 0, "most have a monitor");
  return monitor_obj(jvms(), mon);
}

// Do we Match on this edge index or not?  Match no edges
uint SafePointNode::match_edge(uint idx) const {
  return 0;
}

//=============================================================================
// Create a new fixed-size object 
AllocateNode::AllocateNode( Node *thr, Node *kid_node, Node *size_in_bytes, Node *extra_slow, const TypeOopPtr *oop_type, const TypeFunc *tf ) 
  : CallRuntimeNode(tf,
                    choose_starting_allocation_routine(oop_type),
                    choose_starting_allocation_name   (oop_type),
                    TypeRawPtr::BOTTOM,
                    0/* no profile info */, false) 
{
  init_req(Thr      ,thr);
  init_req(KID      ,kid_node);
init_req(AllocSize,size_in_bytes);
  init_req(sizeHalf ,Compile::current()->top()); // half of Long size
  init_req(ExtraSlow,extra_slow); // extra slow-path test
  init_class_id(Class_Allocate);
  assert0( kid_node->bottom_type()->is_klassptr()->_is_kid );
}

//-------------------------choose_starting_allocation_routine------------------
address AllocateNode::choose_starting_allocation_routine(const TypeOopPtr *oop_type) {
  if( oop_type->klass()->has_finalizer() )
    return (address)SharedRuntime::_new;
  return UseSBA ? StubRoutines::new_sba() : StubRoutines::new_fast();
}

//-------------------------choose_starting_allocation_name---------------------
const char *AllocateNode::choose_starting_allocation_name(const TypeOopPtr *oop_type) {
  if( oop_type->klass()->has_finalizer() )
return"new_slow";
  return UseSBA ? "new_sba" : "new_fast";
}

//=============================================================================
// Create a new array 
AllocateArrayNode::AllocateArrayNode( Node *thr, Node *kid, Node *size_in_bytes, Node *extra_slow, const TypeOopPtr *oop_type, Node *length, Node *ekid ) : AllocateNode( thr, kid, size_in_bytes, extra_slow, oop_type, oop_type->make_new_alloc_sig(true) ) {
init_req(ALength,length);
  init_req(EKID   ,ekid);
init_class_id(Class_AllocateArray);
  _name = UseSBA ? "new_sba_array" : "new_fast_array";
  _entry_point = UseSBA ? StubRoutines::new_sba_array() : StubRoutines::new_fast_array();
}

//------------------------------Ideal------------------------------------------
// Skip over any collapsed Regions
Node*AllocateNode::Ideal(PhaseGVN*phase,bool can_reshape){
  if (remove_dead_region(phase, can_reshape)) return this;
  if (!can_reshape) return NULL;
  if (outcnt() == 3) return NULL;
Node*ctl=proj_out(TypeFunc::Control);
  Node *mem = proj_out(TypeFunc::Memory );
  if (!ctl || !mem) return NULL; // this will get deleted shortly.

  // AllocateArray has to do a negative array-length check, and throw the
  // correct exception if the length is bad, even if the array is dead.
PhaseIterGVN*igvn=phase->is_IterGVN();
  if (is_AllocateArray()) {
    // Check if we are called after expand_allocation.
    if (in(AllocateNode::EKID)->is_top()) return NULL;
    // Check if length is known positive.
    if (!igvn->type(in(AllocateNode::ALength))->higher_equal(TypeInt::POS)) return NULL;
  }

  // The only case is when ctl and mem exist but there is no use for oop.
igvn->hash_delete(mem);
igvn->hash_delete(ctl);
  igvn->subsume_node( mem, igvn->C->top() );
  igvn->subsume_node( ctl, in(0) );

  return igvn->C->top(); 
}

//=============================================================================
LockNode::LockNode( Compile *C, const TypeFunc *tf ) : 
  CallStaticJavaNode(tf,
                     (address)SharedRuntime::monitorenter,
                     "monitorenter", 0,
TypeOopPtr::BOTTOM,
                     NULL, false) {
init_class_id(Class_Lock);
}

//-------------------------lock_type------------------------------------------
const TypeFunc *LockNode::lock_type() {
  // create input type (domain)
  const Type **fields = TypeTuple::fields(1);
fields[TypeFunc::Parms+0]=TypeInstPtr::NOTNULL;//Object to be Locked
  const TypeTuple *domain = TypeTuple::make(TypeFunc::Parms+1,fields);

  // create result type (range)
  fields = TypeTuple::fields(0);

  const TypeTuple *range = TypeTuple::make(TypeFunc::Parms+0,fields);

  return TypeFunc::make(domain,range);
}

//-------------------------Value----------------------------------------------
const Type*LockNode::Value(PhaseTransform*phase)const{
  //Node *lobj = in(TypeFunc::Parms+0);
  //Node *lmem = in(TypeFunc::Memory);
  //if( lmem->is_Proj() &&
  //    lobj->is_Proj() &&
  //    lmem->in(0) == obj->in(0) &&
  //    lmem->in(0)->is_Allocate() ) {
  //  C2OUT->print_cr("locking a new object");
  //}
  
return CallStaticJavaNode::Value(phase);
}

//-------------------------Ideal----------------------------------------------
// Remove back-to-back unlock/re-lock pairs
Node*LockNode::Ideal(PhaseGVN*phase,bool can_reshape){
  if (remove_dead_region(phase, can_reshape))  return this;
if(!can_reshape)return NULL;
if(!CoarsenLocks)return NULL;
  if( outcnt() != 2 ) return NULL; // Dead or dying

Node*p=in(0);
  if( !p->is_Proj() ) return NULL;
Node*unlock=p->in(0);
  if( !unlock->is_Unlock() ) return NULL;
  const UnlockNode *un = unlock->as_Unlock();
  assert0( un->entry_point() == StubRoutines::unlock_c2_entry() );
  if( un->in(TypeFunc::Parms+0) != this->in(TypeFunc::Parms+0) )
    return NULL;
  // Found back-to-back unlock/relock of same thing.  Coarsen the lock.
if(PrintOpto)C2OUT->print_cr("Lock coarsening");
  ProjNode *ctrl = this->proj_out(TypeFunc::Control);
  ProjNode *meml = this->proj_out(TypeFunc::Memory );
  ProjNode *ctru =   un->proj_out(TypeFunc::Control);
ProjNode*memu=un->proj_out(TypeFunc::Memory);

  PhaseIterGVN *iter = (PhaseIterGVN*)phase;
  iter->add_users_to_worklist(ctrl);
  iter->add_users_to_worklist(ctru);
  iter->add_users_to_worklist(meml);
  iter->add_users_to_worklist(memu);
  iter->hash_delete(ctrl);
  iter->hash_delete(ctru);
  iter->hash_delete(meml);
  iter->hash_delete(memu);
  iter->subsume_node(ctrl,this->in(TypeFunc::Control));
  iter->subsume_node(ctru,  un->in(TypeFunc::Control));
  iter->subsume_node(meml,this->in(TypeFunc::Memory ));
  iter->subsume_node(memu,  un->in(TypeFunc::Memory ));
return new(phase->C,1)ConNode(Type::TOP);
}

//=============================================================================
UnlockNode::UnlockNode( Compile *C, const TypeFunc *tf ) :
  CallLeafNode(tf,
StubRoutines::unlock_c2_entry(),
"monitorexit",
TypeOopPtr::BOTTOM,
               NULL/*no call-count info*/, false) {
init_class_id(Class_Unlock);
}

