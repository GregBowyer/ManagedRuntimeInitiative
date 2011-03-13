/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "addnode.hpp"
#include "ciInstance.hpp"
#include "ciArrayKlass.hpp"
#include "ciObjArrayKlass.hpp"
#include "collectedHeap.hpp"
#include "graphKit.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "matcher.hpp"
#include "objArrayKlass.hpp"
#include "parse.hpp"
#include "rootnode.hpp"
#include "runtime.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"

//----------------------------GraphKit-----------------------------------------
// Main utility constructor.
GraphKit::GraphKit(JVMState* jvms)
  : Phase(Phase::Parser),
    _env(C->env()),
    _gvn(*C->initial_gvn())
{
  _exceptions = jvms->map()->next_exception();
  if (_exceptions != NULL)  jvms->map()->set_next_exception(NULL);
  set_jvms(jvms);
}

// Private constructor for parser.
GraphKit::GraphKit()
  : Phase(Phase::Parser),
    _env(C->env()),
    _gvn(*C->initial_gvn())
{
  _exceptions = NULL;
  set_map(NULL);
  debug_only(_sp = -99);
  debug_only(set_bci(-99));
}



//---------------------------clean_stack---------------------------------------
// Clear away rubbish from the stack area of the JVM state.
// This destroys any arguments that may be waiting on the stack.
void GraphKit::clean_stack(int from_sp) {
  SafePointNode* map      = this->map();
  JVMState*      jvms     = this->jvms();
  int            stk_size = jvms->stk_size();
  int            stkoff   = jvms->stkoff();
  Node*          top      = this->top();
  for (int i = from_sp; i < stk_size; i++) {
    if (map->in(stkoff + i) != top) {
      map->set_req(stkoff + i, top);
    }
  }
}


//--------------------------------sync_jvms-----------------------------------
// Make sure our current jvms agrees with our parse state.
JVMState* GraphKit::sync_jvms() const {
  JVMState* jvms = this->jvms();
  jvms->set_bci(bci());       // Record the new bci in the JVMState
  jvms->set_sp(sp());         // Record the new sp in the JVMState
  assert(jvms_in_sync(), "jvms is now in sync");
  return jvms;
}

#ifdef ASSERT
bool GraphKit::jvms_in_sync() const {
  Parse* parse = is_Parse();
  if (parse == NULL) {
    if (bci() !=      jvms()->bci())          return false;
    if (sp()  != (int)jvms()->sp())           return false;
    return true;
  }
  if (jvms()->method() != parse->method())    return false;
  if (jvms()->bci()    != parse->bci())       return false;
  int jvms_sp = jvms()->sp();
  if (jvms_sp          != parse->sp())        return false;
  int jvms_depth = jvms()->depth();
  if (jvms_depth       != parse->depth())     return false;
  return true;
}

// Local helper checks for special internal merge points
// used to accumulate and merge exception states.
// They are marked by the region's in(0) edge being the map itself.
// Such merge points must never "escape" into the parser at large,
// until they have been handed to gvn.transform.
static bool is_hidden_merge(Node* reg) {
  if (reg == NULL)  return false;
  if (reg->is_Phi()) {
    reg = reg->in(0);
    if (reg == NULL)  return false;
  }
  return reg->is_Region() && reg->in(0) != NULL && reg->in(0)->is_Root();
}

void GraphKit::verify_map() const {
  if (map() == NULL)  return;  // null map is OK
  assert(map()->req() <= jvms()->endoff(), "no extra garbage on map");
  assert(!map()->has_exceptions(),    "call add_exception_states_from 1st");
  assert(!is_hidden_merge(control()), "call use_exception_state, not set_map");
}

void GraphKit::verify_exception_state(SafePointNode* ex_map) {
  assert(ex_map->next_exception() == NULL, "not already part of a chain");
  assert(has_saved_ex_oop(ex_map), "every exception state has an ex_oop");
}
#endif

//---------------------------stop_and_kill_map---------------------------------
// Set _map to NULL, signalling a stop to further bytecode execution.
// First smash the current map's control to a constant, to mark it dead.
void GraphKit::stop_and_kill_map() {
  SafePointNode* dead_map = stop();
  if (dead_map != NULL) {
    dead_map->disconnect_inputs(NULL); // Mark the map as killed.
    assert(dead_map->is_killed(), "must be so marked");
  }
}


//--------------------------------stopped--------------------------------------
// Tell if _map is NULL, or control is top.
bool GraphKit::stopped() {
  if (map() == NULL)           return true;
  else if (control() == top()) return true;
  else                         return false;
}


//-----------------------------has_ex_handler----------------------------------
// Tell if this method or any caller method has exception handlers.
bool GraphKit::has_ex_handler() {
  for (JVMState* jvmsp = jvms(); jvmsp != NULL; jvmsp = jvmsp->caller()) {
    if (jvmsp->has_method() && jvmsp->method()->has_exception_handlers()) {
      return true;
    }
  }
  return false;
}

//------------------------------save_ex_oop------------------------------------
// Save an exception without blowing stack contents or other JVM state.
void GraphKit::set_saved_ex_oop(SafePointNode* ex_map, Node* ex_oop) {
  assert(!has_saved_ex_oop(ex_map), "clear ex-oop before setting again");
  ex_map->add_req(ex_oop);
  debug_only(verify_exception_state(ex_map));
}

inline static Node* common_saved_ex_oop(SafePointNode* ex_map, bool clear_it) {
  assert(GraphKit::has_saved_ex_oop(ex_map), "ex_oop must be there");
  Node* ex_oop = ex_map->in(ex_map->req()-1);
  if (clear_it)  ex_map->del_req(ex_map->req()-1);
  return ex_oop;
}

//-----------------------------saved_ex_oop------------------------------------
// Recover a saved exception from its map.
Node* GraphKit::saved_ex_oop(SafePointNode* ex_map) {
  return common_saved_ex_oop(ex_map, false);
}

//--------------------------clear_saved_ex_oop---------------------------------
// Erase a previously saved exception from its map.
Node* GraphKit::clear_saved_ex_oop(SafePointNode* ex_map) {
  return common_saved_ex_oop(ex_map, true);
}

#ifdef ASSERT
//---------------------------has_saved_ex_oop----------------------------------
// Erase a previously saved exception from its map.
bool GraphKit::has_saved_ex_oop(SafePointNode* ex_map) {
  return ex_map->req() == ex_map->jvms()->endoff()+1;
}
#endif

//-------------------------make_exception_state--------------------------------
// Turn the current JVM state into an exception state, appending the ex_oop.
SafePointNode* GraphKit::make_exception_state(Node* ex_oop) {
  sync_jvms();
  SafePointNode* ex_map = stop();  // do not manipulate this map any more
  set_saved_ex_oop(ex_map, ex_oop);
  return ex_map;
}


//--------------------------add_exception_state--------------------------------
// Add an exception to my list of exceptions.
void GraphKit::add_exception_state(SafePointNode* ex_map) {
  if (ex_map == NULL || ex_map->control() == top()) {
    return;
  }
#ifdef ASSERT
  verify_exception_state(ex_map);
  if (has_exceptions()) {
    assert(ex_map->jvms()->same_calls_as(_exceptions->jvms()), "all collected exceptions must come from the same place");
  }
#endif

  // If there is already an exception of exactly this type, merge with it.
  // In particular, null-checks and other low-level exceptions common up here.
  Node*       ex_oop  = saved_ex_oop(ex_map);
  const Type* ex_type = _gvn.type(ex_oop);
  if (ex_oop == top()) {
    // No action needed.
    return;
  }
  assert(ex_type->isa_instptr(), "exception must be an instance");
  for (SafePointNode* e2 = _exceptions; e2 != NULL; e2 = e2->next_exception()) {
    const Type* ex_type2 = _gvn.type(saved_ex_oop(e2));
    // We check sp also because call bytecodes can generate exceptions
    // both before and after arguments are popped!
    if (ex_type2 == ex_type
        && e2->_jvms->sp() == ex_map->_jvms->sp()) {
      combine_exception_states(ex_map, e2);
      return;
    }
  }

  // No pre-existing exception of the same type.  Chain it on the list.
  push_exception_state(ex_map);
}

//-----------------------add_exception_states_from-----------------------------
void GraphKit::add_exception_states_from(JVMState* jvms) {
  SafePointNode* ex_map = jvms->map()->next_exception();
  if (ex_map != NULL) {
    jvms->map()->set_next_exception(NULL);
    for (SafePointNode* next_map; ex_map != NULL; ex_map = next_map) {
      next_map = ex_map->next_exception();
      ex_map->set_next_exception(NULL);
      add_exception_state(ex_map);
    }
  }
}

//-----------------------transfer_exceptions_into_jvms-------------------------
JVMState* GraphKit::transfer_exceptions_into_jvms() {
  if (map() == NULL) {
    // We need a JVMS to carry the exceptions, but the map has gone away.
    // Create a scratch JVMS, cloned from any of the exception states...
    if (has_exceptions()) {
      _map = _exceptions;
      _map = clone_map();
      _map->set_next_exception(NULL);
      clear_saved_ex_oop(_map);
      debug_only(verify_map());
    } else {
      // ...or created from scratch
      JVMState* jvms = new (C) JVMState(_method, NULL);
      jvms->set_bci(_bci);
      jvms->set_sp(_sp);
jvms->set_map(new(C,TypeFunc::Parms)SafePointNode(TypeFunc::Parms,jvms,NULL,NULL));
      set_jvms(jvms);
      for (uint i = 0; i < map()->req(); i++)  map()->init_req(i, top());
      set_all_memory(top());
      while (map()->req() < jvms->endoff())  map()->add_req(top());
    }
    // (This is a kludge, in case you didn't notice.)
    set_control(top());
  }
  JVMState* jvms = sync_jvms();
  assert(!jvms->map()->has_exceptions(), "no exceptions on this map yet");
  jvms->map()->set_next_exception(_exceptions);
  _exceptions = NULL;   // done with this set of exceptions
  return jvms;
}

static inline void add_n_reqs(Node* dstphi, Node* srcphi) {
  assert(is_hidden_merge(dstphi), "must be a special merge node");
  assert(is_hidden_merge(srcphi), "must be a special merge node");
  uint limit = srcphi->req();
  for (uint i = PhiNode::Input; i < limit; i++) {
    dstphi->add_req(srcphi->in(i));
  }
}
static inline void add_one_req(Node* dstphi, Node* src) {
  assert(is_hidden_merge(dstphi), "must be a special merge node");
  assert(!is_hidden_merge(src), "must not be a special merge node");
  dstphi->add_req(src);
}

//-----------------------combine_exception_states------------------------------
// This helper function combines exception states by building phis on a
// specially marked state-merging region.  These regions and phis are
// untransformed, and can build up gradually.  The region is marked by
// having a control input of its exception map, rather than NULL.  Such
// regions do not appear except in this function, and in use_exception_state.
void GraphKit::combine_exception_states(SafePointNode* ex_map, SafePointNode* phi_map) {
  if (failing())  return;  // dying anyway...
  JVMState* ex_jvms = ex_map->_jvms;
  assert(ex_jvms->same_calls_as(phi_map->_jvms), "consistent call chains");
  assert(ex_jvms->stkoff() == phi_map->_jvms->stkoff(), "matching locals");
  assert(ex_jvms->sp() == phi_map->_jvms->sp(), "matching stack sizes");
  assert(ex_jvms->monoff() == phi_map->_jvms->monoff(), "matching JVMS");
  assert(ex_map->req() == phi_map->req(), "matching maps");
  uint tos = ex_jvms->stkoff() + ex_jvms->sp();
  Node*         hidden_merge_mark = root();
  Node*         region  = phi_map->control();
  MergeMemNode* phi_mem = phi_map->merged_memory();
  MergeMemNode* ex_mem  = ex_map->merged_memory();
  if (region->in(0) != hidden_merge_mark) {
    // The control input is not (yet) a specially-marked region in phi_map.
    // Make it so, and build some phis.
    region = new (C, 2) RegionNode(2);
    _gvn.set_type(region, Type::CONTROL);
    region->set_req(0, hidden_merge_mark);  // marks an internal ex-state
    region->init_req(1, phi_map->control());
    phi_map->set_control(region);
    Node* io_phi = PhiNode::make(region, phi_map->i_o(), Type::ABIO);
    record_for_igvn(io_phi);
    _gvn.set_type(io_phi, Type::ABIO);
    phi_map->set_i_o(io_phi);
    for (MergeMemStream mms(phi_mem); mms.next_non_empty(); ) {
      Node* m = mms.memory();
      Node* m_phi = PhiNode::make(region, m, Type::MEMORY, mms.adr_type(C));
      record_for_igvn(m_phi);
      _gvn.set_type(m_phi, Type::MEMORY);
      mms.set_memory(m_phi);
    }
  }

  // Either or both of phi_map and ex_map might already be converted into phis.
  Node* ex_control = ex_map->control();
  // if there is special marking on ex_map also, we add multiple edges from src
  bool add_multiple = (ex_control->in(0) == hidden_merge_mark);
  // how wide was the destination phi_map, originally?
  uint orig_width = region->req();

  if (add_multiple) {
    add_n_reqs(region, ex_control);
    add_n_reqs(phi_map->i_o(), ex_map->i_o());
  } else {
    // ex_map has no merges, so we just add single edges everywhere
    add_one_req(region, ex_control);
    add_one_req(phi_map->i_o(), ex_map->i_o());
  }
  for (MergeMemStream mms(phi_mem, ex_mem); mms.next_non_empty2(); ) {
    if (mms.is_empty()) {
      // get a copy of the base memory, and patch some inputs into it
      const TypePtr* adr_type = mms.adr_type(C);
      Node* phi = mms.force_memory()->as_Phi()->slice_memory(adr_type);
      assert(phi->as_Phi()->region() == mms.base_memory()->in(0), "");
      mms.set_memory(phi);
      // Prepare to append interesting stuff onto the newly sliced phi:
      while (phi->req() > orig_width)  phi->del_req(phi->req()-1);
    }
    // Append stuff from ex_map:
    if (add_multiple) {
      add_n_reqs(mms.memory(), mms.memory2());
    } else {
      add_one_req(mms.memory(), mms.memory2());
    }
  }
  uint limit = ex_map->req();
  for (uint i = TypeFunc::Parms; i < limit; i++) {
    // Skip everything in the JVMS after tos.  (The ex_oop follows.)
    if (i == tos)  i = ex_jvms->monoff();
    Node* src = ex_map->in(i);
    Node* dst = phi_map->in(i);
    if (src != dst) {
      PhiNode* phi;
      if (dst->in(0) != region) {
        dst = phi = PhiNode::make(region, dst, _gvn.type(dst));
        record_for_igvn(phi);
        _gvn.set_type(phi, phi->type());
        phi_map->set_req(i, dst);
        // Prepare to append interesting stuff onto the new phi:
        while (dst->req() > orig_width)  dst->del_req(dst->req()-1);
      } else {
        assert(dst->is_Phi(), "nobody else uses a hidden region");
        phi = (PhiNode*)dst;
      }
      if (add_multiple && src->in(0) == ex_control) {
        // Both are phis.
        add_n_reqs(dst, src);
      } else {
        while (dst->req() < region->req())  add_one_req(dst, src);
      }
      const Type* srctype = _gvn.type(src);
      if (phi->type() != srctype) {
        const Type* dsttype = phi->type()->meet(srctype);
        if (phi->type() != dsttype) {
          phi->set_type(dsttype);
          _gvn.set_type(phi, dsttype);
        }
      }
    }
  }
}

//--------------------------use_exception_state--------------------------------
Node* GraphKit::use_exception_state(SafePointNode* phi_map) {
  if (failing()) { stop(); return top(); }
  Node* region = phi_map->control();
  Node* hidden_merge_mark = root();
  assert(phi_map->jvms()->map() == phi_map, "sanity: 1-1 relation");
  Node* ex_oop = clear_saved_ex_oop(phi_map);
  if (region->in(0) == hidden_merge_mark) {
    // Special marking for internal ex-states.  Process the phis now.
    region->set_req(0, region);  // now it's an ordinary region
    set_jvms(phi_map->jvms());   // ...so now we can use it as a map
    // Note: Setting the jvms also sets the bci and sp.
    set_control(_gvn.transform(region));
    uint tos = jvms()->stkoff() + sp();
    for (uint i = 1; i < tos; i++) {
      Node* x = phi_map->in(i);
      if (x->in(0) == region) {
        assert(x->is_Phi(), "expected a special phi");
        phi_map->set_req(i, _gvn.transform(x));
      }
    }
    for (MergeMemStream mms(merged_memory()); mms.next_non_empty(); ) {
      Node* x = mms.memory();
      if (x->in(0) == region) {
        assert(x->is_Phi(), "nobody else uses a hidden region");
        mms.set_memory(_gvn.transform(x));
      }
    }
    if (ex_oop->in(0) == region) {
      assert(ex_oop->is_Phi(), "expected a special phi");
      ex_oop = _gvn.transform(ex_oop);
    }
  } else {
    set_jvms(phi_map->jvms());
  }

  assert(!is_hidden_merge(phi_map->control()), "hidden ex. states cleared");
  assert(!is_hidden_merge(phi_map->i_o()), "hidden ex. states cleared");
  return ex_oop;
}

//---------------------------------java_bc-------------------------------------
Bytecodes::Code GraphKit::java_bc() const {
  ciMethod* method = this->method();
  int       bci    = this->bci();
  if (method != NULL && bci != InvocationEntryBci)
    return method->java_code_at_bci(bci);
  else
    return Bytecodes::_illegal;
}

//------------------------------builtin_throw----------------------------------
void GraphKit::builtin_throw(Deoptimization::DeoptReason reason,const char*msg,CPData_Null*cpdn,bool repeated_fail,bool must_throw){
  // JVMTI forces us into the interpreter with every throw.
  if (JvmtiExport::can_post_exceptions()) {
    // Do not try anything fancy if we're notifying the VM on every throw.
    // Cf. case Bytecodes::_athrow in parse2.cpp.
    uncommon_trap(Deoptimization::Reason_jvmti, NULL, "jvmti hooks", /*must_throw=*/false);
    return;
  }

  // If this particular condition has not yet happened at this
  // bytecode, then use the uncommon trap mechanism, and allow for
  // a future recompilation if several traps occur here.
  // If the throw is hot, try to use a more complicated inline mechanism
  // which keeps execution inside the compiled code.
  bool treat_throw_as_hot = false;

  if (ProfileTraps) {
    // This is a place where Azul and Sun's handling of exceptions differs.
    // Azul is tracking exceptions exactly per bytecode, but not directly
    // counting fails.  Sun appears to be 'smearing' failed fast-path
    // bytecodes together, but exactly counting fails per-method.  If we get
    // here with repeated_fail==true, then we failed at this bytecode at least
    // once before, so we assume we will fail here again in the future and go
    // ahead and JIT code to deal with a potential future exception throw.
    if( repeated_fail )
      treat_throw_as_hot = true;

    // (If there is no MDO at all, assume it is early in
    // execution, and that any deopts are part of the
    // startup transient, and don't need to be remembered.)

    // Also, if there is a local exception handler, treat all throws
    // as hot if there has been at least one in this method.
    if (repeated_fail
        && has_ex_handler()) {
        treat_throw_as_hot = true;
    }
  }

  // If this throw happens frequently, an uncommon trap might cause
  // a performance pothole.  If there is a local exception handler,
  // and if this particular bytecode appears to be deoptimizing often,
  // let us handle the throw inline, with a preconstructed instance.
  // Note:   If the deopt count has blown up, the uncommon trap
  // runtime is going to flush this nmethod, not matter what.
  if (treat_throw_as_hot
      && (!StackTraceInThrowable || OmitStackTraceInFastThrow)) {
    // If the throw is local, we use a pre-existing instance and
    // punt on the backtrace.  This would lead to a missing backtrace
    // (a repeat of 4292742) if the backtrace object is ever asked
    // for its backtrace.
    // Fixing this remaining case of 4292742 requires some flavor of
    // escape analysis.  Leave that for the future.
    ciInstance* ex_obj = NULL;
    switch (reason) {
    case Deoptimization::Reason_null_check:
      ex_obj = env()->NullPointerException_instance();
      break;
    case Deoptimization::Reason_div0_check:
      ex_obj = env()->ArithmeticException_instance();
      break;
    case Deoptimization::Reason_range_check:
      ex_obj = env()->ArrayIndexOutOfBoundsException_instance();
      break;
case Deoptimization::Reason_cast_check:
      if (java_bc() == Bytecodes::_aastore) {
        ex_obj = env()->ArrayStoreException_instance();
      } else {
        ex_obj = env()->ClassCastException_instance();
      }
      break;
    }
    if (failing()) { stop(); return; }  // exception allocation might fail
    if (ex_obj != NULL) {
      // Cheat with a preallocated exception object.
      const TypeInstPtr* ex_con  = TypeInstPtr::make(ex_obj);
      Node*              ex_node = _gvn.transform(new (C, 1) ConPNode(ex_con));

      // Clear the detail message of the preallocated exception object.
      // Weblogic sometimes mutates the detail message of exceptions 
      // using reflection.
      int offset = java_lang_Throwable::get_detailMessage_offset();
      const TypePtr* adr_typ = ex_con->add_offset(offset);
      
      Node *adr = basic_plus_adr(ex_node, ex_node, offset);
uint adr_idx=C->get_alias_index(adr_typ);
      Node *store = store_to_memory(control(), adr, null(), T_OBJECT, adr_idx);
        
      add_exception_state(make_exception_state(ex_node));
      return;
    }
  }

  // %%% Maybe add entry to OptoRuntime which directly throws the exc.?
  // It won't be much cheaper than bailing to the interp., since we'll
  // have to pass up all the debug-info, and the runtime will have to
  // create the stack trace.

  // Usual case:  Bail to interpreter.
  // Reserve the right to recompile if we haven't seen anything yet.

  //Deoptimization::DeoptAction action = Deoptimization::Action_maybe_recompile;
  //if (treat_throw_as_hot
  //    && (method()->method_data()->trap_recompiled_at(bci())
  //        || C->too_many_traps(reason))) {
  //  // We cannot afford to take more traps here.  Suffer in the interpreter.
  //  if (C->log() != NULL)
  //    C->log()->elem("hot_throw preallocated='0' reason='%s' mcount='%d'",
  //                   Deoptimization::trap_reason_name(reason),
  //                   C->trap_count(reason));
  //  action = Deoptimization::Action_none;
  //}

  // "must_throw" prunes the JVM state to include only the stack, if there
  // are no local exception handlers.  This should cut down on register
  // allocation time and code size, by drastically reducing the number
  // of in-edges on the call to the uncommon trap.

  uncommon_trap(reason, (ciKlass*)NULL, msg, must_throw);
}

//----------------------------PreserveJVMState---------------------------------
PreserveJVMState::PreserveJVMState(GraphKit* kit, bool clone_map) {
  debug_only(kit->verify_map());
  _kit    = kit;
  _map    = kit->map();   // preserve the map
  _sp     = kit->sp();
  kit->set_map(clone_map ? kit->clone_map() : NULL);
#ifdef ASSERT
  _bci    = kit->bci();
  Parse* parser = kit->is_Parse();
  int block = (parser == NULL || parser->block() == NULL) ? -1 : parser->block()->pre_order();
  _block  = block;
#endif
}
PreserveJVMState::~PreserveJVMState() {
  GraphKit* kit = _kit;
#ifdef ASSERT
  assert(kit->bci() == _bci, "bci must not shift");
  Parse* parser = kit->is_Parse();
  int block = (parser == NULL || parser->block() == NULL) ? -1 : parser->block()->pre_order();
  assert(block == _block,    "block must not shift");
#endif
  kit->set_map(_map);
  kit->set_sp(_sp);
}


//-----------------------------BuildCutout-------------------------------------
BuildCutout::BuildCutout(GraphKit* kit, Node* p, float prob, float cnt)
  : PreserveJVMState(kit)
{
  assert(p->is_Con() || p->is_Bool(), "test must be a bool");
  SafePointNode* outer_map = _map;   // preserved map is caller's
  SafePointNode* inner_map = kit->map();
  IfNode* iff = kit->create_and_map_if(outer_map->control(), p, prob, cnt);
  outer_map->set_control(kit->gvn().transform( new (kit->C, 1) IfTrueNode(iff) ));
  inner_map->set_control(kit->gvn().transform( new (kit->C, 1) IfFalseNode(iff) ));
}
BuildCutout::~BuildCutout() {
  GraphKit* kit = _kit;
  assert(kit->stopped(), "cutout code must stop, throw, return, etc.");
}


//------------------------------clone_map--------------------------------------
// Implementation of PreserveJVMState
//
// Only clone_map(...) here. If this function is only used in the
// PreserveJVMState class we may want to get rid of this extra
// function eventually and do it all there.

SafePointNode* GraphKit::clone_map() {
  if (map() == NULL)  return NULL;
  
  // Clone the memory edge first
  Node* mem = MergeMemNode::make(C, map()->memory());
  gvn().set_type_bottom(mem);

  SafePointNode *clonemap = (SafePointNode*)map()->clone();
  JVMState* jvms = this->jvms();
  JVMState* clonejvms = jvms->clone_shallow(C);
  clonemap->set_memory(mem);
clonemap->set_jvms(clonejvms,map()->_cpd);
  clonejvms->set_map(clonemap);
  record_for_igvn(clonemap);
  gvn().set_type_bottom(clonemap);
  return clonemap;
}


//-----------------------------set_map_clone-----------------------------------
void GraphKit::set_map_clone(SafePointNode* m) {
  _map = m;
  _map = clone_map();
  _map->set_next_exception(NULL);
  debug_only(verify_map());
}


//----------------------------kill_dead_locals---------------------------------
// Detect any locals which are known to be dead, and force them to top.
void GraphKit::kill_dead_locals() {
  // Consult the liveness information for the locals.  If any
  // of them are unused, then they can be replaced by top().  This
  // should help register allocation time and cut down on the size
  // of the deoptimization information.

  // This call is made from many of the bytecode handling
  // subroutines called from the Big Switch in do_one_bytecode.
  // Every bytecode which might include a slow path is responsible
  // for killing its dead locals.  The more consistent we
  // are about killing deads, the fewer useless phis will be
  // constructed for them at various merge points.

  // bci can be -1 (InvocationEntryBci).  We return the entry
  // liveness for the method.

  if (method() == NULL || method()->code_size() == 0) {
    // We are building a graph for a call to a native method.
    // All locals are live.
    ShouldNotReachHere();
    return;
  }

  ResourceMark rm;

  // Consult the liveness information for the locals.  If any
  // of them are unused, then they can be replaced by top().  This
  // should help register allocation time and cut down on the size
  // of the deoptimization information.
  MethodLivenessResult live_locals = method()->liveness_at_bci(bci());
  
  int len = (int)live_locals.size();
  assert(len <= jvms()->loc_size(), "too many live locals");
  for (int local = 0; local < len; local++) {
    if (!live_locals.at(local)) {
      set_local(local, top());
    }
  }
}

#ifdef ASSERT
//-------------------------dead_locals_are_killed------------------------------
// Return true if all dead locals are set to top in the map.
// Used to assert "clean" debug info at various points.
bool GraphKit::dead_locals_are_killed() {
  if (method() == NULL || method()->code_size() == 0) {
    // No locals need to be dead, so all is as it should be.
    return true;
  }

  // Make sure somebody called kill_dead_locals upstream.
  ResourceMark rm;
  for (JVMState* jvms = this->jvms(); jvms != NULL; jvms = jvms->caller()) {
    if (jvms->loc_size() == 0)  continue;  // no locals to consult
    SafePointNode* map = jvms->map();
    ciMethod* method = jvms->method();
    int       bci    = jvms->bci();
    if (jvms == this->jvms()) {
      bci = this->bci();  // it might not yet be synched
    }
    MethodLivenessResult live_locals = method->liveness_at_bci(bci);
    int len = (int)live_locals.size();
    if (!live_locals.is_valid() || len == 0)
      // This method is trivial, or is poisoned by a breakpoint.
      return true;
    assert(len == jvms->loc_size(), "live map consistent with locals map");
    for (int local = 0; local < len; local++) {
      if (!live_locals.at(local) && map->local(jvms, local) != top()) {
        return false;
      }
    }
  }
  return true;
}

#endif //ASSERT

// Helper function for adding JVMState and debug information to node
void GraphKit::add_safepoint_edges(SafePointNode*call,bool must_throw,CPData*cpd){
  // Add the safepoint edges to the call (or other safepoint).

  // Make sure dead locals are set to top.  This
  // should help register allocation time and cut down on the size
  // of the deoptimization information.
  assert(dead_locals_are_killed(), "garbage in debug info before safepoint");

  // Walk the inline list to fill in the correct set of JVMState's
  // Also fill in the associated edges for each JVMState.

  JVMState* youngest_jvms = sync_jvms();

  // Do we need debug info here?  If it is a SafePoint and this method
  // cannot de-opt, then we do NOT need any debug info.
  bool full_info = (C->deopt_happens() || call->Opcode() != Op_SafePoint);

  // If we are guaranteed to throw at this bytecode, we can prune everything but the
  // input to the current bytecode.  Happens all the time for
  // uncommon-trap bytecodes leading to improbable situations.
  bool can_prune_locals = false;
  uint stack_slots_not_pruned = 0;
  int inputs = 0, depth = 0;
  if (must_throw) {
    assert(method() == youngest_jvms->method(), "sanity");
    if (compute_stack_effects(inputs, depth)) {
      can_prune_locals = true;
      stack_slots_not_pruned = inputs;
    }
  }

  if (JvmtiExport::can_examine_or_deopt_anywhere()) {
    // At any safepoint, this method can get breakpointed, which would
    // then require an immediate deoptimization.
    full_info = true;
    can_prune_locals = false;  // do not prune locals
    stack_slots_not_pruned = 0;
  }

  // do not scribble on the input jvms
  JVMState* out_jvms = youngest_jvms->clone_deep(C);
call->set_jvms(out_jvms,cpd);//Start jvms list for call node

  // Presize the call:
  debug_only(uint non_debug_edges = call->req());
  call->add_req_batch(top(), youngest_jvms->debug_depth());
  assert(call->req() == non_debug_edges + youngest_jvms->debug_depth(), "");

  // Set up edges so that the call looks like this:
  //  Call [state:] ctl io mem fptr retadr
  //       [parms:] parm0 ... parmN
  //       [root:]  loc0 ... locN stk0 ... stkSP mon0 obj0 ... monN objN
  //    [...mid:]   loc0 ... locN stk0 ... stkSP mon0 obj0 ... monN objN [...]
  //       [young:] loc0 ... locN stk0 ... stkSP mon0 obj0 ... monN objN
  // Note that caller debug info precedes callee debug info.

  // Fill pointer walks backwards from "young:" to "root:" in the diagram above:
  uint debug_ptr = call->req();

  // Loop over the map input edges associated with jvms, add them
  // to the call node, & reset all offsets to match call node array.
  for (JVMState* in_jvms = youngest_jvms; in_jvms != NULL; ) {
    uint debug_end   = debug_ptr;
    uint debug_start = debug_ptr - in_jvms->debug_size();
    debug_ptr = debug_start;  // back up the ptr

    uint p = debug_start;  // walks forward in [debug_start, debug_end)
    uint j, k, l;
    SafePointNode* in_map = in_jvms->map();
    out_jvms->set_map(call);

    if (can_prune_locals) {
      assert(in_jvms->method() == out_jvms->method(), "sanity");
      // If the current throw can reach an exception handler in this JVMS,
      // then we must keep everything live that can reach that handler.
      // As a quick and dirty approximation, we look for any handlers at all.
      if (in_jvms->method()->has_exception_handlers()) {
        can_prune_locals = false;
      }
    }

    // Add the Locals
    k = in_jvms->locoff();
    l = in_jvms->loc_size();
    out_jvms->set_locoff(p);
    if (full_info && !can_prune_locals) {
      for (j = 0; j < l; j++)
        call->set_req(p++, in_map->in(k+j));
    } else {
      p += l;  // already set to top above by add_req_batch
    }

    // Add the Expression Stack
    k = in_jvms->stkoff();
    l = in_jvms->sp();
    out_jvms->set_stkoff(p);
    if (full_info && !can_prune_locals) {
      for (j = 0; j < l; j++)
        call->set_req(p++, in_map->in(k+j));
    } else if (can_prune_locals && stack_slots_not_pruned != 0) {
      // Divide stack into {S0,...,S1}, where S0 is set to top.
      uint s1 = stack_slots_not_pruned;
      stack_slots_not_pruned = 0;  // for next iteration
      if (s1 > l)  s1 = l;
      uint s0 = l - s1;
      p += s0;  // skip the tops preinstalled by add_req_batch
      for (j = s0; j < l; j++)
        call->set_req(p++, in_map->in(k+j));
    } else {
      p += l;  // already set to top above by add_req_batch
    }

    // Add the Monitors
    k = in_jvms->monoff();
    l = in_jvms->mon_size();
    out_jvms->set_monoff(p);
    for (j = 0; j < l; j++)
      call->set_req(p++, in_map->in(k+j));

    // Finish the new jvms.
    out_jvms->set_endoff(p);

    assert(out_jvms->endoff()     == debug_end,             "fill ptr must match");
    assert(out_jvms->depth()      == in_jvms->depth(),      "depth must match");
    assert(out_jvms->loc_size()   == in_jvms->loc_size(),   "size must match");
    assert(out_jvms->mon_size()   == in_jvms->mon_size(),   "size must match");
    assert(out_jvms->debug_size() == in_jvms->debug_size(), "size must match");

    // Update the two tail pointers in parallel.
    out_jvms = out_jvms->caller();
    in_jvms  = in_jvms->caller();
  }

  assert(debug_ptr == non_debug_edges, "debug info must fit exactly");

  // Test the correctness of JVMState::debug_xxx accessors:
  assert(call->jvms()->debug_start() == non_debug_edges, "");
  assert(call->jvms()->debug_end()   == call->req(), "");
  assert(call->jvms()->debug_depth() == call->req() - non_debug_edges, "");
}

bool GraphKit::compute_stack_effects(int& inputs, int& depth) {
  Bytecodes::Code code = java_bc();
  if (code == Bytecodes::_wide) {
    code = method()->java_code_at_bci(bci() + 1);
  }

  BasicType rtype = T_ILLEGAL;
  int       rsize = 0;

  if (code != Bytecodes::_illegal) {
    depth = Bytecodes::depth(code); // checkcast=0, athrow=-1
    rtype = Bytecodes::result_type(code); // checkcast=P, athrow=V
    if (rtype < T_CONFLICT)
      rsize = type2size[rtype];
  }

  switch (code) {
  case Bytecodes::_illegal:
    return false;

  case Bytecodes::_ldc:
  case Bytecodes::_ldc_w:
  case Bytecodes::_ldc2_w:
    inputs = 0;
    break;

  case Bytecodes::_dup:         inputs = 1;  break;
  case Bytecodes::_dup_x1:      inputs = 2;  break;
  case Bytecodes::_dup_x2:      inputs = 3;  break;
  case Bytecodes::_dup2:        inputs = 2;  break;
  case Bytecodes::_dup2_x1:     inputs = 3;  break;
  case Bytecodes::_dup2_x2:     inputs = 4;  break;
  case Bytecodes::_swap:        inputs = 2;  break;
  case Bytecodes::_arraylength: inputs = 1;  break;

  case Bytecodes::_getstatic:
  case Bytecodes::_putstatic:
  case Bytecodes::_getfield:
  case Bytecodes::_putfield:
    {
      bool is_get = (depth >= 0), is_static = (depth & 1);
      bool ignore;
      ciBytecodeStream iter(method());
      iter.reset_to_bci(bci());
      iter.next();
      ciField* field = iter.get_field(ignore);
      int      size  = field->type()->size();
      inputs  = (is_static ? 0 : 1);
      if (is_get) {
        depth = size - inputs;
      } else {
        inputs += size;        // putxxx pops the value from the stack
        depth = - inputs;
      }
    }
    break;

  case Bytecodes::_invokevirtual:
  case Bytecodes::_invokespecial:
  case Bytecodes::_invokestatic:
  case Bytecodes::_invokeinterface:
    {
      bool is_static = (depth == 0);
      bool ignore;
      ciBytecodeStream iter(method());
      iter.reset_to_bci(bci());
      iter.next();
      ciMethod* method = iter.get_method(ignore);
      inputs = method->arg_size_no_receiver();
      if (!is_static)  inputs += 1;
      int size = method->return_type()->size();
      depth = size - inputs;
    }
    break;

  case Bytecodes::_multianewarray:
    {
      ciBytecodeStream iter(method());
      iter.reset_to_bci(bci());
      iter.next();
      inputs = iter.get_dimensions();
      assert(rsize == 1, "");
      depth = rsize - inputs;
    }
    break;

  case Bytecodes::_ireturn:
  case Bytecodes::_lreturn:
  case Bytecodes::_freturn:
  case Bytecodes::_dreturn:
  case Bytecodes::_areturn:
    assert(rsize = -depth, "");
    inputs = rsize;
    break;

  case Bytecodes::_jsr:
  case Bytecodes::_jsr_w:
    inputs = 0;
    depth  = 1;                  // S.B. depth=1, not zero
    break;

  default:
    // bytecode produces a typed result
    inputs = rsize - depth;
    assert(inputs >= 0, "");
    break;
  }

#ifdef ASSERT
  // spot check
  int outputs = depth + inputs;
  assert(outputs >= 0, "sanity");
  switch (code) {
  case Bytecodes::_checkcast: assert(inputs == 1 && outputs == 1, ""); break;
  case Bytecodes::_athrow:    assert(inputs == 1 && outputs == 0, ""); break;
  case Bytecodes::_aload_0:   assert(inputs == 0 && outputs == 1, ""); break;
  case Bytecodes::_return:    assert(inputs == 0 && outputs == 0, ""); break;
  case Bytecodes::_drem:      assert(inputs == 4 && outputs == 2, ""); break;
  }
#endif //ASSERT

  return true;
}



//------------------------------basic_plus_adr---------------------------------
Node* GraphKit::basic_plus_adr(Node* base, Node* ptr, Node* offset) {
  // short-circuit a common case
  if (offset == intcon(0))  return ptr;
  return _gvn.transform( new (C, 4) AddPNode(base, ptr, offset) );
}

Node* GraphKit::ConvI2L(Node* offset) {
  // short-circuit a common case
  jint offset_con = find_int_con(offset, Type::OffsetBot);
  if (offset_con != Type::OffsetBot) {
    return longcon((long) offset_con);
  }
  return _gvn.transform( new (C, 2) ConvI2LNode(offset));
}
Node* GraphKit::ConvL2I(Node* offset) {
  // short-circuit a common case
  jlong offset_con = find_long_con(offset, (jlong)Type::OffsetBot);
  if (offset_con != (jlong)Type::OffsetBot) {
    return intcon((int) offset_con);
  }
  return _gvn.transform( new (C, 2) ConvL2INode(offset));
}

//-------------------------load_object_klass-----------------------------------
Node* GraphKit::load_object_klass(Node* obj) {
  // Special-case a fresh allocation to avoid building nodes:
  Node* akls = AllocateNode::Ideal_klass(obj, &_gvn);
  if (akls != NULL)  return akls;
Node*k_adr=obj;
  const TypeOopPtr *toop = obj->bottom_type()->is_oopptr();
  const TypeKlassPtr *tkid = TypeKlassPtr::make_kid(toop->klass(),toop->klass_is_exact());
Node*kid=_gvn.transform(new(C,2)GetKIDNode(0,k_adr,tkid));
  //Node* klass = _gvn.transform( new (C, 2) KID2KlassNode(kid) );
  return kid;
}

//-------------------------load_array_length-----------------------------------
Node* GraphKit::load_array_length(Node* array) {
  // Special-case a fresh allocation to avoid building nodes:
  Node* alen = AllocateArrayNode::Ideal_length(array, &_gvn);
  if (alen != NULL)  return alen;
  Node *r_adr = basic_plus_adr(array, arrayOopDesc::length_offset_in_bytes());
  // The ODJK uses "immutable_memory()" here, but this is buggy in the case
  // where the optimizer proves (long past parse-time) that the LoadRange
  // feeds from a AllocateArray.  In this case the correct memory is the edges
  // coming from the AllocateArray and not the Start - or at least from the
  // store that sets the length.  In some sense, "immutable_memory()" is "too soon".
  Node *mem = memory(TypeAryPtr::RANGE); // immutable_memory()
  return _gvn.transform( new (C, 3) LoadRangeNode(0, mem, r_adr, TypeInt::POS));
}

//------------------------------do_null_check----------------------------------
// Helper function to do a NULL pointer check.  Returned value is 
// the incoming address with NULL casted away.  You are allowed to use the
// not-null value only if you are control dependent on the test.
extern int explicit_null_checks_inserted, 
           explicit_null_checks_elided;
Node* GraphKit::null_check_common(Node* value, BasicType type,
                                  // optional arguments for variations:
                                  bool assert_null,
Node**null_control,
                                  Deoptimization::DeoptReason DI, const char *msg,
                                  CPData_Null *cpdn) {
  assert(!assert_null || null_control == NULL, "not both at once");
  if (stopped())  return top();
  if (!GenerateCompilerNullChecks && !assert_null && null_control == NULL) {
    // For some performance testing, we may wish to suppress null checking.
value=cast_not_null(value,true);//Make it appear to be non-null (4962416).
    return value;
  }
  explicit_null_checks_inserted++;

  // Construct NULL check
  Node *chk = NULL;
  switch(type) {
    case T_LONG   : chk = new (C, 3) CmpLNode(value, _gvn.zerocon(T_LONG)); break;
    case T_INT    : chk = new (C, 3) CmpINode( value, _gvn.intcon(0)); break;
    case T_ARRAY  : // fall through
      type = T_OBJECT;  // simplify further tests
    case T_OBJECT : {
      const Type *t = _gvn.type( value );

      const TypeInstPtr* tp = t->isa_instptr();
      if (tp != NULL && !tp->klass()->is_loaded()
          // Only for do_null_check, not any of its siblings:
          && !assert_null && null_control == NULL) {
        // Usually, any field access or invocation on an unloaded oop type
        // will simply fail to link, since the statically linked class is
        // likely also to be unloaded.  However, in -Xcomp mode, sometimes
        // the static class is loaded but the sharper oop type is not.
        // Rather than checking for this obscure case in lots of places,
        // we simply observe that a null check on an unloaded class
        // will always be followed by a nonsense operation, so we
        // can just issue the uncommon trap here.
        // Our access to the unloaded class will only be correct
        // after it has been loaded and initialized, which requires
        // a trip through the interpreter.
        uncommon_trap(Deoptimization::Reason_unloaded, 
tp->klass(),"!loaded",false);
        return top();
      }

      if (assert_null) {
        // See if the type is contained in NULL_PTR.
        // If so, then the value is already null.
        if (t->higher_equal(TypePtr::NULL_PTR)) {
          explicit_null_checks_elided++;  
          return value;           // Elided null assert quickly!
        }
      } else {
        // See if mixing in the NULL pointer changes type.
        // If so, then the NULL pointer was not allowed in the original
        // type.  In other words, "value" was not-null.
        if (t->meet(TypePtr::NULL_PTR) != t) {
          // same as: if (!TypePtr::NULL_PTR->higher_equal(t)) ...
          explicit_null_checks_elided++;  
          return value;           // Elided null check quickly!
        }
      }
      chk = new (C, 3) CmpPNode( value, null() );
      break;    
    }

    default      : ShouldNotReachHere();
  }
  assert(chk != NULL, "sanity check"); 
  chk = _gvn.transform(chk);

  BoolTest::mask btest = assert_null ? BoolTest::eq : BoolTest::ne;
  BoolNode *btst = new (C, 2) BoolNode( chk, btest);
  Node   *tst = _gvn.transform( btst );

  //-----------
  // if peephole optimizations occured, a prior test existed.
  // If a prior test existed, maybe it dominates as we can avoid this test.
  if (tst != btst && type == T_OBJECT) {
    // At this point we want to scan up the CFG to see if we can
    // find an identical test (and so avoid this test altogether).
    Node *cfg = control();
    int depth = 0;
    while( depth < 16 ) {       // Limit search depth for speed
      if( cfg->Opcode() == Op_IfTrue &&
          cfg->in(0)->in(1) == tst ) {
        // Found prior test.  Use "cast_not_null" to construct an identical
        // CastPP (and hence hash to) as already exists for the prior test.
        // Return that casted value.
        if (assert_null) {
          replace_in_map(value, null());
          return null();  // do not issue the redundant test
        }
        Node *oldcontrol = control();
        set_control(cfg);
Node*res=cast_not_null(value,true);
        set_control(oldcontrol);
        explicit_null_checks_elided++;  
        return res;
      }
      cfg = IfNode::up_one_dom(cfg, /*linear_only=*/ true);
      if (cfg == NULL)  break;  // Quit at region nodes
      depth++;
    }
  }

  //-----------
  // Branch to failure if null
  float ok_prob = PROB_MAX;  // a priori estimate:  nulls never happen
 
  // To cause an implicit null check, we set the not-null probability
  // to the maximum (PROB_MAX).  For an explicit check the probablity
  // is set to a smaller value.
  if (null_control != NULL ) {
    // probability is less likely
    ok_prob =  PROB_LIKELY_MAG(3);
  } else if (!assert_null &&
             cpdn->saw_null()) {
    ok_prob =  PROB_LIKELY_MAG(3);
  }

  if (null_control != NULL) {
    IfNode* iff = create_and_map_if(control(), tst, ok_prob, COUNT_UNKNOWN);
    Node* null_true = _gvn.transform( new (C, 1) IfFalseNode(iff));
    set_control(      _gvn.transform( new (C, 1) IfTrueNode(iff)));
    if (null_true == top())
      explicit_null_checks_elided++;
    (*null_control) = null_true;
  } else {
    BuildCutout unless(this, tst, ok_prob);
    // Check for optimizer eliding test at parse time
    if (stopped()) {
      // Failure not possible; do not bother making uncommon trap.
      explicit_null_checks_elided++;
    } else if (assert_null) {
      uncommon_trap(Deoptimization::Reason_unloaded, 
                    NULL, "unloaded class must always be null", false);
    } else {
      // Throwing all the time?
      builtin_throw( DI, msg, cpdn, cpdn->saw_null(), /*must_throw=*/true );
    }
  }

  // Must throw exception, fall-thru not possible?
  if (stopped()) {
    return top();               // No result
  }

  if (assert_null) {
    // Cast obj to null on this path.
    replace_in_map(value, zerocon(type));
    return zerocon(type);
  }

  // Cast obj to not-null on this path, if there is no null_control.
  // (If there is a null_control, a non-null value may come back to haunt us.)
  if (type == T_OBJECT) {
    Node* cast = cast_not_null(value, false);
    if (null_control == NULL || (*null_control) == top())
      replace_in_map(value, cast);
    value = cast;
  }
      
  return value;
}


//------------------------------cast_not_null----------------------------------
// Cast obj to not-null on this path
Node* GraphKit::cast_not_null(Node* obj, bool do_replace_in_map) {
  const Type *t = _gvn.type(obj);
  const Type *t_not_null = t->join(TypePtr::NOTNULL);
  // Object is already not-null?
  if( t == t_not_null ) return obj;

  Node *cast = new (C, 2) CastPPNode(obj,t_not_null);
  cast->init_req(0, control());
  cast = _gvn.transform( cast );

  // Scan for instances of 'obj' in the current JVM mapping.
  // These instances are known to be not-null after the test.
  if (do_replace_in_map)
    replace_in_map(obj, cast);

  return cast;                  // Return casted value
}


//--------------------------replace_in_map-------------------------------------
void GraphKit::replace_in_map(Node* old, Node* neww) {
  this->map()->replace_edge(old, neww);

  // Note: This operation potentially replaces any edge
  // on the map.  This includes locals, stack, and monitors
  // of the current (innermost) JVM state.

  // We can consider replacing in caller maps.
  // The idea would be that an inlined function's null checks
  // can be shared with the entire inlining tree.
  // The expense of doing this is that the PreserveJVMState class
  // would have to preserve caller states too, with a deep copy.
}



//=============================================================================
//--------------------------------memory---------------------------------------
Node* GraphKit::memory(uint alias_idx) {
  MergeMemNode* mem = merged_memory();
  Node* p = mem->memory_at(alias_idx);
  if( p != top() )
    _gvn.set_type(p, Type::MEMORY);  // must be mapped
  return p;
}

//-----------------------------reset_memory------------------------------------
Node* GraphKit::reset_memory() {
  Node* mem = map()->memory();
  // do not use this node for any more parsing!
  debug_only( map()->set_memory((Node*)NULL) );
  return _gvn.transform( mem ); 
}

//------------------------------set_all_memory---------------------------------
void GraphKit::set_all_memory(Node* newmem) {
  Node* mergemem = MergeMemNode::make(C, newmem);
  gvn().set_type_bottom(mergemem);
  map()->set_memory(mergemem);
}

//------------------------------set_all_memory_call----------------------------
void GraphKit::set_all_memory_call(Node* call) {
  Node* newmem = _gvn.transform( new (C, 1) ProjNode(call, TypeFunc::Memory) );
  set_all_memory(newmem);
}

//=============================================================================
//
// parser factory methods for MemNodes
//
// These are layered on top of the factory methods in LoadNode and StoreNode,
// and integrate with the parser's memory state and _gvn engine.
//

// factory methods in "int adr_idx"
Node* GraphKit::make_load(Node* ctl, Node* adr, const Type* t, BasicType bt,
                          int adr_idx,
                          bool DUMMY_require_atomic_access) {
  assert(adr_idx != Compile::AliasIdxTop, "use other make_load factory" );
  const TypePtr* adr_type = NULL; // debug-mode-only argument
  debug_only(adr_type = C->get_adr_type(adr_idx));
  Node* mem = memory(adr_idx);
  Node* ld;
  ld = LoadNode::make(C, ctl, mem, adr, adr_type, t, bt);
  return _gvn.transform(ld);
}

Node* GraphKit::store_to_memory(Node* ctl, Node* adr, Node *val, BasicType bt,
int adr_idx){
  assert(adr_idx != Compile::AliasIdxTop, "use other store_to_memory factory" );
  const TypePtr* adr_type = NULL;
  debug_only(adr_type = C->get_adr_type(adr_idx));
  Node *mem = memory(adr_idx);
  Node* st;
  st = StoreNode::make(C, ctl, mem, adr, adr_type, val, bt);
  st = _gvn.transform(st);
  set_memory(st, adr_idx);
  // Back-to-back stores can only remove intermediate store with DU info
  // so push on worklist for optimizer.
  if (mem->req() > MemNode::Address && adr == mem->in(MemNode::Address))  
    record_for_igvn(st);

  return st;
}

//-------------------------array_element_address-------------------------
Node* GraphKit::array_element_address(Node* ary, Node* idx, BasicType elembt,
                                      const TypeInt* sizetype) {
  uint shift  = exact_log2(type2aelembytes[elembt]);
  uint header = arrayOopDesc::base_offset_in_bytes(elembt);

  // short-circuit a common case (saves lots of confusing waste motion)
  jint idx_con = find_int_con(idx, -1);
  if (idx_con >= 0) {
    intptr_t offset = header + ((intptr_t)idx_con << shift);
    return basic_plus_adr(ary, offset);
  }

  // must be correct type for alignment purposes
  Node* base  = basic_plus_adr(ary, header); 
  // The scaled index operand to AddP must be a clean 64-bit value.
  // Java allows a 32-bit int to be incremented to a negative
  // value, which appears in a 64-bit register as a large
  // positive number.  Using that large positive number as an
  // operand in pointer arithmetic has bad consequences.
  // On the other hand, 32-bit overflow is rare, and the possibility
  // can often be excluded, if we annotate the ConvI2L node with
  // a type assertion that its value is known to be a small positive
  // number.  (The prior range check has ensured this.)
  // This assertion is used by ConvI2LNode::Ideal.
  int index_max = max_jint - 1;  // array size is max_jint, index is one less
  if (sizetype != NULL)  index_max = sizetype->_hi - 1;
  const TypeLong* lidxtype = TypeLong::make(CONST64(0), index_max, Type::WidenMax);
  idx = _gvn.transform( new (C, 2) ConvI2LNode(idx, lidxtype) );
Node*scale=_gvn.transform(new(C,3)LShiftLNode(idx,intcon(shift)));
  return basic_plus_adr(ary, base, scale);
}

//-------------------------load_array_element-------------------------
Node* GraphKit::load_array_element(Node* ctl, Node* ary, Node* idx, const TypeAryPtr* arytype) {
  const Type* elemtype = arytype->elem();
  BasicType elembt = elemtype->array_element_basic_type();
  Node* adr = array_element_address(ary, idx, elembt, arytype->size());
  Node* ld = make_load(ctl, adr, elemtype, elembt, arytype);
  return ld;
}

//-------------------------set_arguments_for_java_call-------------------------
// Arguments (pre-popped from the stack) are taken from the JVMS.
void GraphKit::set_arguments_for_java_call(CallJavaNode* call) {
  // Add the call arguments:
  uint nargs = call->method()->arg_size();
  for (uint i = 0; i < nargs; i++) {
    Node* arg = argument(i);
    call->init_req(i + TypeFunc::Parms, arg);
  }
}

//---------------------------set_edges_for_java_call---------------------------
// Connect a newly created call into the current JVMS.
// A return value node (if any) is returned from set_edges_for_java_call.
void GraphKit::set_edges_for_java_call(CallJavaNode*call,CPData*cpd,bool must_throw){

  // Add the predefined inputs:
  call->init_req( TypeFunc::Control, control() );
  call->init_req( TypeFunc::I_O    , i_o() );
  call->init_req( TypeFunc::Memory , reset_memory() );
  call->init_req( TypeFunc::FramePtr, frameptr() );
  call->init_req( TypeFunc::ReturnAdr, top() );

  add_safepoint_edges(call, must_throw, cpd);

  Node* xcall = _gvn.transform(call);

  if (xcall == top()) {
    set_control(top());
    return;
  }
  assert(xcall == call, "call identity is stable");

  // Re-use the current map to produce the result.

  set_control(_gvn.transform(new (C, 1) ProjNode(call, TypeFunc::Control)));
  set_i_o(    _gvn.transform(new (C, 1) ProjNode(call, TypeFunc::I_O    )));
  set_all_memory_call(xcall);

  //return xcall;   // no need, caller already has it
}

Node* GraphKit::set_results_for_java_call(CallJavaNode* call) {
  if (stopped())  return top();  // maybe the call folded up?

  // Capture the return value, if any.
  Node* ret;
  if (call->method() == NULL ||
      call->method()->return_type()->basic_type() == T_VOID)
        ret = top();
  else  ret = _gvn.transform(new (C, 1) ProjNode(call, TypeFunc::Parms));

  // Note:  Since any out-of-line call can produce an exception,
  // we always insert an I_O projection from the call into the result.

  make_slow_call_ex(call, env()->Throwable_klass(), false);

  return ret;
}

//--------------------set_predefined_input_for_runtime_call--------------------
// Reading and setting the memory state is way conservative here.
// The real problem is that I am not doing real Type analysis on memory,
// so I cannot distinguish card mark stores from other stores.  Across a GC
// point the Store Barrier and the card mark memory has to agree.  I cannot
// have a card mark store and its barrier split across the GC point from
// either above or below.  Here I get that to happen by reading ALL of memory.
// A better answer would be to separate out card marks from other memory.
// For now, return the input memory state, so that it can be reused
// after the call, if this call has restricted memory effects.
Node* GraphKit::set_predefined_input_for_runtime_call(SafePointNode* call) {
  // Set fixed predefined input arguments
  Node* memory = reset_memory();
  call->init_req( TypeFunc::Control,   control()  );
  call->init_req( TypeFunc::I_O,       top()      ); // does no i/o
  call->init_req( TypeFunc::Memory,    memory     ); // may gc ptrs
  call->init_req( TypeFunc::FramePtr,  frameptr() );
  call->init_req( TypeFunc::ReturnAdr, top()      );
  return memory;
}

//-------------------set_predefined_output_for_runtime_call--------------------
// Set control and memory (not i_o) from the call.
// If keep_mem is not NULL, use it for the output state,
// except for the RawPtr output of the call, if hook_mem is TypeRawPtr::BOTTOM.
// If hook_mem is NULL, this call produces no memory effects at all.
// If hook_mem is a Java-visible memory slice (such as arraycopy operands),
// then only that memory slice is taken from the call.
// In the last case, we must put an appropriate memory barrier before
// the call, so as to create the correct anti-dependencies on loads
// preceding the call.
void GraphKit::set_predefined_output_for_runtime_call(Node* call,
                                                      Node* keep_mem,
                                                      const TypePtr* hook_mem) {
  // no i/o
  set_control(_gvn.transform( new (C, 1) ProjNode(call,TypeFunc::Control) ));
  if (keep_mem) {
    // First clone the existing memory state
    set_all_memory(keep_mem);
    if (hook_mem != NULL) {
      // Make memory for the call
      Node* mem = _gvn.transform( new (C, 1) ProjNode(call, TypeFunc::Memory) );
      // Set the RawPtr memory state only.  This covers all the heap top/GC stuff
      // We also use hook_mem to extract specific effects from arraycopy stubs.
      set_memory(mem, hook_mem);
    }
    // ...else the call has NO memory effects.

    // Make sure the call advertises its memory effects precisely.
    // This lets us build accurate anti-dependences in gcm.cpp.
    assert(C->alias_type(call->adr_type()) == C->alias_type(hook_mem),
           "call node must be constructed correctly");
  } else {
    assert(hook_mem == NULL, "");
    // This is not a "slow path" call; all memory comes from the call.
    set_all_memory_call(call);
  }
} 

//------------------------------increment_counter------------------------------
// for statistics: increment a VM counter by 1

void GraphKit::increment_counter(address counter_addr) {
  Node* adr1 = makecon(TypeRawPtr::make(counter_addr));
  increment_counter(adr1);
}

void GraphKit::increment_counter(Node* counter_addr) {
  int adr_type = Compile::AliasIdxRaw;
  Node* cnt  = make_load(NULL, counter_addr, TypeInt::INT, T_INT, adr_type);
  Node* incr = _gvn.transform(new (C, 3) AddINode(cnt, _gvn.intcon(1)));
  store_to_memory( NULL, counter_addr, incr, T_INT, adr_type );
}


//------------------------------uncommon_trap----------------------------------
// Bail out to the interpreter in mid-method.  Implemented by calling the
// uncommon_trap blob.  This helper function inserts a runtime call with the
// right debug info.  
void GraphKit::uncommon_trap(Deoptimization::DeoptReason trap_request,
                             ciKlass* klass, const char* comment,
bool must_throw
){
  if (failing())  stop();
  if (stopped())  return; // trap reachable?

#ifdef ASSERT
  if (!must_throw) {
    // Make sure the stack has at least enough depth to execute
    // the current bytecode.
    int inputs, ignore;
    if (compute_stack_effects(inputs, ignore)) {
      assert(sp() >= inputs, "must have enough JVMS stack to execute");
      // It is a frequent error in library_call.cpp to issue an
      // uncommon trap with the _sp value already popped.
    }
  }
#endif

  if (TraceOptoParse) {
C2OUT->print_cr("Uncommon trap %s at bci:%d",
comment,bci());
  }

  // Make sure any guarding test views this path as very unlikely
  Node *i0 = control()->in(0);
  if (i0 != NULL && i0->is_If()) {        // Found a guarding if test?
    IfNode *iff = i0->as_If();
    float f = iff->_prob;   // Get prob
    if (control()->Opcode() == Op_IfTrue) {
      if (f > PROB_UNLIKELY_MAG(4))
        iff->_prob = PROB_MIN;
    } else {
      if (f < PROB_LIKELY_MAG(4))
        iff->_prob = PROB_MAX;
    }
  }

  // Clear out dead values from the debug info.
  kill_dead_locals();

  // Now insert the uncommon trap subroutine call
address call_addr=StubRoutines::uncommon_trap_entry();
  const TypePtr* no_memory_effects = NULL;
  // Pass the index of the class to be loaded
  Node* call = make_runtime_call(RC_NO_LEAF | RC_UNCOMMON |
                                 (must_throw ? RC_MUST_THROW : 0),
                                 false /* !must_callruntimenode */,
                                 OptoRuntime::uncommon_trap_Type(),
                                 call_addr, "uncommon_trap", no_memory_effects,
                                 intcon(trap_request));
  assert(call->as_CallStaticJava()->uncommon_trap_request() == trap_request,
         "must extract request correctly from the graph");
  assert(trap_request != 0, "zero value reserved by uncommon_trap_request");

  call->set_req(TypeFunc::ReturnAdr, returnadr());
  // The debug info is the only real input to this call.

  // Halt-and-catch fire here.  The above call should never return!
HaltNode*halt=new(C,TypeFunc::Parms)HaltNode(control(),frameptr(),"uncommon trap");
  _gvn.set_type_bottom(halt);
  root()->add_req(halt);

  stop_and_kill_map();
}


void GraphKit::round_double_arguments(ciMethod* dest_method) {
  // (Note:  TypeFunc::make has a cache that makes this fast.)
  const TypeFunc* tf    = TypeFunc::make(dest_method);
  int             nargs = tf->_domain->_cnt - TypeFunc::Parms;
  for (int j = 0; j < nargs; j++) {
    const Type *targ = tf->_domain->field_at(j + TypeFunc::Parms);
    if( targ->basic_type() == T_DOUBLE ) {
      // If any parameters are doubles, they must be rounded before
      // the call, dstore_rounding does gvn.transform
      Node *arg = argument(j);
      arg = dstore_rounding(arg);
      set_argument(j, arg);
    }
  }
}

void GraphKit::round_double_result(ciMethod* dest_method) {
  // A non-strict method may return a double value which has an extended 
  // exponent, but this must not be visible in a caller which is 'strict'
  // If a strict caller invokes a non-strict callee, round a double result

  BasicType result_type = dest_method->return_type()->basic_type();
  assert( method() != NULL, "must have caller context");
  if( result_type == T_DOUBLE && method()->is_strict() && !dest_method->is_strict() ) {
    // Destination method's return value is on top of stack
    // dstore_rounding() does gvn.transform
    Node *result = pop_pair();
    result = dstore_rounding(result);
    push_pair(result);
  }
}

// rounding for strict float precision conformance
Node* GraphKit::precision_rounding(Node* n) {
  // Azul assumes at least SSE2 support
  return n;
}

// rounding for strict double precision conformance
Node* GraphKit::dprecision_rounding(Node *n) {
  // Azul assumes at least SSE2 support
  return n;
}

// rounding for non-strict double stores
Node* GraphKit::dstore_rounding(Node* n) {
  // Azul assumes at least SSE2 support
  return n;
}

//=============================================================================
// Generate a fast path/slow path idiom.  Graph looks like:
// [foo] indicates that 'foo' is a parameter
//
//              [in]     NULL
//                 \    /
//                  CmpP
//                  Bool ne
//                   If
//                  /  \
//              True    False-<2>
//              / |        
//             /  cast_not_null
//           Load  |    |   ^
//        [fast_test]   |   |
// gvn to   opt_test    |   |
//          /    \      |  <1>
//      True     False  |
//        |         \\  |
//   [slow_call]     \[fast_result]
//    Ctl   Val       \      \
//     |               \      \ 
//    Catch       <1>   \      \
//   /    \        ^     \      \
//  Ex    No_Ex    |      \      \
//  |       \   \  |       \ <2>  \
//  ...      \  [slow_res] |  |    \   [null_result]
//            \         \--+--+---  |  |
//             \           | /    \ | /
//              --------Region     Phi
//
//=============================================================================
// Code is structured as a series of driver functions all called 'do_XXX' that 
// call a set of helper functions.  Helper functions first, then drivers.

//------------------------------null_check_oop---------------------------------
// Null check oop.  Set null-path control into Region in slot 3.
// Make a cast-not-nullness use the other not-null control.  Return cast.
Node* GraphKit::null_check_oop(Node* value, Node* *null_control,
bool never_see_null,
                               CPData_Null *cpdn) {
  assert0( never_see_null == !cpdn->saw_null());
  // Initial NULL check taken path
  (*null_control) = top();
Node*cast=null_check_common(value,T_OBJECT,false,null_control,
                                 Deoptimization::Reason_null_check, "expected throwing NPE", cpdn);

  // Generate uncommon_trap:
  if (never_see_null && (*null_control) != top()) {
    // If we see an unexpected null at a check-cast we record it and force a
    // recompile; the offending check-cast will be compiled to handle NULLs.
    // If we see more than one offending BCI, then all checkcasts in the
    // method will be compiled to handle NULLs.
    PreserveJVMState pjvms(this);
    set_control(*null_control);
uncommon_trap(Deoptimization::Reason_null_check,NULL,"unexpected NPE",false);
    (*null_control) = top();    // NULL path is dead
  }

  // Cast away null-ness on the result
  return cast;
}

//------------------------------opt_iff----------------------------------------
// Optimize the fast-check IfNode.  Set the fast-path region slot 2.
// Return slow-path control.
Node* GraphKit::opt_iff(Node* region, Node* iff) {
  IfNode *opt_iff = _gvn.transform(iff)->as_If();

  // Fast path taken; set region slot 2
  Node *fast_taken = _gvn.transform( new (C, 1) IfFalseNode(opt_iff) );
  region->init_req(2,fast_taken); // Capture fast-control 

  // Fast path not-taken, i.e. slow path
  Node *slow_taken = _gvn.transform( new (C, 1) IfTrueNode(opt_iff) );
  return slow_taken;
}

//-----------------------------make_runtime_call-------------------------------
Node* GraphKit::make_runtime_call(int flags,
                                  bool must_callruntimenode,
                                  const TypeFunc* call_type, address call_addr,
                                  const char* call_name,
                                  const TypePtr* adr_type,
                                  // The following parms are all optional.
                                  // The first NULL ends the list.
                                  Node* parm0, Node* parm1,
                                  Node* parm2, Node* parm3,
                                  Node* parm4, Node* parm5,
                                  Node* parm6, Node* parm7) {
Parse*P=is_Parse();
  CPData *cpd = P ? P->cpdata() : NULL;
  bool cloned = P ? P->is_private_copy() : false;

  // Slow-path call
  int size = call_type->domain()->cnt();
  bool is_leaf = !(flags & RC_NO_LEAF);
  bool has_io  = (!is_leaf && !(flags & RC_NO_IO));
  assert0( call_name != NULL );
  CallNode* call;
  if (must_callruntimenode) {
call=new(C,size)CallRuntimeNode(call_type,call_addr,call_name,
                                       adr_type, 0/* no call-site data*/, 
                                       cloned);
  } else if (!is_leaf) {
    call = new(C, size) CallStaticJavaNode(call_type, call_addr, call_name,
                                           bci(), adr_type, cpd, cloned);
  } else if (flags & RC_NO_FP) {
call=new(C,size)CallLeafNoFPNode(call_type,call_addr,call_name,adr_type,cpd,cloned);
  } else {
call=new(C,size)CallLeafNode(call_type,call_addr,call_name,adr_type,cpd,cloned);
  }

  // The following is similar to set_edges_for_java_call,
  // except that the memory effects of the call are restricted to AliasIdxRaw.

  // Slow path call has no side-effects, uses few values
  bool wide_in  = !(flags & RC_NARROW_MEM);
  bool wide_out = (C->get_alias_index(adr_type) == Compile::AliasIdxBot);

  Node* prev_mem = NULL;
  if (wide_in) {
    prev_mem = set_predefined_input_for_runtime_call(call);
  } else {
    assert(!wide_out, "narrow in => narrow out");
    Node* narrow_mem = memory(adr_type);
    prev_mem = reset_memory();
    map()->set_memory(narrow_mem);
    set_predefined_input_for_runtime_call(call);
  }

  // Hook each parm in order.  Stop looking at the first NULL.
  if (parm0 != NULL) { call->init_req(TypeFunc::Parms+0, parm0);
  if (parm1 != NULL) { call->init_req(TypeFunc::Parms+1, parm1);
  if (parm2 != NULL) { call->init_req(TypeFunc::Parms+2, parm2);
  if (parm3 != NULL) { call->init_req(TypeFunc::Parms+3, parm3);
  if (parm4 != NULL) { call->init_req(TypeFunc::Parms+4, parm4);
  if (parm5 != NULL) { call->init_req(TypeFunc::Parms+5, parm5);
  if (parm6 != NULL) { call->init_req(TypeFunc::Parms+6, parm6);
  if (parm7 != NULL) { call->init_req(TypeFunc::Parms+7, parm7);
    /* close each nested if ===> */  } } } } } } } }
  assert(call->in(call->req()-1) != NULL, "must initialize all parms");

  if (!is_leaf) {
    // Non-leaves can block and take safepoints:
add_safepoint_edges(call,((flags&RC_MUST_THROW)!=0),cpd);
  }
  // Non-leaves can throw exceptions:
  if (has_io) {
    call->set_req(TypeFunc::I_O, i_o());
  }

  if (flags & RC_UNCOMMON) {
    // Set the count to a tiny probability.  Cf. Estimate_Block_Frequency.
    // (An "if" probability corresponds roughly to an unconditional count.
    // Sort of.)
    // AZUL: We use our profile data
    //call->set_cnt(PROB_UNLIKELY_MAG(4));
  }

  Node* c = _gvn.transform(call);
  assert(c == call, "cannot disappear");

  if (wide_out) {
    // Slow path call has full side-effects.
    set_predefined_output_for_runtime_call(call);
  } else {
    // Slow path call has few side-effects, and/or sets few values.
    set_predefined_output_for_runtime_call(call, prev_mem, adr_type);
  }

  if (has_io) {
    set_i_o(_gvn.transform(new (C, 1) ProjNode(call, TypeFunc::I_O)));
  }
  return call;

}

//------------------------------merge_memory-----------------------------------
// Merge memory from one path into the current memory state.
void GraphKit::merge_memory(Node* new_mem, Node* region, int new_path) {
  for (MergeMemStream mms(merged_memory(), new_mem->as_MergeMem()); mms.next_non_empty2(); ) {
    Node* old_slice = mms.force_memory();
    Node* new_slice = mms.memory2();
    if (old_slice != new_slice) {
      PhiNode* phi;
      if (new_slice->is_Phi() && new_slice->as_Phi()->region() == region) {
        phi = new_slice->as_Phi();
        #ifdef ASSERT
        if (old_slice->is_Phi() && old_slice->as_Phi()->region() == region)
          old_slice = old_slice->in(new_path);
        // Caller is responsible for ensuring that any pre-existing
        // phis are already aware of old memory.
        int old_path = (new_path > 1) ? 1 : 2;  // choose old_path != new_path
        assert(phi->in(old_path) == old_slice, "pre-existing phis OK");
        #endif
        mms.set_memory(phi);
      } else {
        phi = PhiNode::make(region, old_slice, Type::MEMORY, mms.adr_type(C));
        _gvn.set_type(phi, Type::MEMORY);
        phi->set_req(new_path, new_slice);
        mms.set_memory(_gvn.transform(phi));  // assume it is complete
      }
    }
  }
}

//------------------------------make_slow_call_ex------------------------------
// Make the exception handler hookups for the slow call
void GraphKit::make_slow_call_ex(Node*call,ciInstanceKlass*ex_klass,bool dummy){
  if (stopped())  return;

  // Make a catch node with just two handlers:  fall-through and catch-all
Node*i_o=_gvn.transform(new(C,1)ProjNode(call,TypeFunc::I_O));
  Node* catc = _gvn.transform( new (C, 2) CatchNode(control(), i_o, 2) );
  Node* norm = _gvn.transform( new (C, 1) CatchProjNode(catc, CatchProjNode::fall_through_index, CatchProjNode::no_handler_bci) );
  Node* excp = _gvn.transform( new (C, 1) CatchProjNode(catc, CatchProjNode::catch_all_index,    CatchProjNode::no_handler_bci) );

  { PreserveJVMState pjvms(this);
    set_control(excp);
    set_i_o(i_o);

    if (excp != top()) {
      // Create an exception state also.
      // Use an exact type if the caller has specified a specific exception.
      const Type* ex_type = TypeOopPtr::make_from_klass_unique(ex_klass)->cast_to_ptr_type(TypePtr::NotNull);

      Node *thread = _gvn.transform( new (C, 1) ThreadLocalNode() );
Node*ex_adr=basic_plus_adr(top(),thread,in_bytes(JavaThread::pending_exception_offset()));
      int pending_ex_alias_idx = C->get_alias_index(ex_adr->bottom_type()->is_ptr());
      Node *ex_oop = make_load( NULL, ex_adr, ex_type, T_OBJECT, pending_ex_alias_idx );
      Node *ex_st  = store_to_memory( control(), ex_adr, null(), T_OBJECT, pending_ex_alias_idx );
record_for_igvn(ex_st);
add_exception_state(make_exception_state(ex_oop));
    }
  }

  // Get the no-exception control from the CatchNode. 
  set_control(norm);
}  


//-------------------------------gen_subtype_check-----------------------------
// Generate a subtyping check.  Takes as input the subtype and supertype.
// Returns 2 values: sets the default control() to the true path and returns
// the false path.  Only reads invariant memory; sets no (visible) memory.
// The PartialSubtypeCheckNode sets the hidden 1-word cache in the encoding
// but that's not exposed to the optimizer.  This call also doesn't take in an
// Object; if you wish to check an Object you need to load the Object's class
// prior to coming here.
Node* GraphKit::gen_subtype_check(Node* subkid, Node* superkid, const Type *subtype) {
  // Fast check for identical types, perhaps identical constants.
  // The types can even be identical non-constants, in cases
  // involving Array.newInstance, Object.clone, etc.
  if (subkid == superkid)
    return top();             // false path is dead; no test needed.

ciKlass*superk=NULL;
  const TypeOopPtr *toop = NULL;
  if( superkid ) {
    superk = _gvn.type(superkid)->is_klassptr()->klass();
    // This trick only works if we are comparing concrete klasses to concrete
    // klasses.  The superklass is sharpened into 'toop'.  The subklass, if it
    // comes from a real object, is also concrete - it cannot be an interface
    // or abstract.  However, if we came here from system.ArrayCopy or
    // isAssignableFrom then the subklass can be almost anything - including
    // an interface klass.  Subklasses are known to be concrete if they came
    // from an object (subtype != NULL) or if the type is not plain imprecise
    // Object.  Interfaces mimic plain Object with an interface list.
    if( subtype || subkid->bottom_type() != TypeKlassPtr::OBJECT ) 
      toop = TypeOopPtr::make_from_klass_unique(superk)->isa_oopptr();
    else
      toop = TypeKlassPtr::OBJECT; // Subklass might be an interface, so weaken the super to dodge the optimziation
  }

  // Some superklasses cannot have an oop: e.g. interface with no
  // implementors.  An oop of such a sharpened superklass will be typed as a
  // NULL, and the sharpening code will have already put in a dependence.
  // In this case, the superklass Node is NULL.
  if( !toop ) {
Node*fall_in=control();
set_control(top());//No success path, but no "stop" either
    return fall_in;       // Return fall-in as fail
  }
  
  // Can I have a subobj with the same type as the superklass?  If no then the
  // subtype check always fails, even if looks OK to the optimizer.  This
  // happens, e.g., when the type system says "no" because of conflicting
  // interface requirements even if the Java klass hierarchy is ok.
  if( subtype && superk->is_instance_klass() ) {
const TypePtr*suptype=TypeOopPtr::make_from_klass_raw(superk)->cast_to_ptr_type(TypePtr::NotNull);
    const Type *tj = subtype->join(suptype);
if(tj->empty()){
Node*fall_in=control();
set_control(top());//No success path, but no "stop" either
      return fall_in;       // Return fall-in as fail
    }
  }

  // Shortcut important common case: superklass has NO subtypes and we can
  // check with a simple compare.  Interfaces or abstract classes with 1
  // concrete implementation are already sharpened to that 1 class.
  if( toop->klass_is_exact() ) {  // If we can do the easy test
    // Just do a direct pointer compare and be done.
    Node *cmp = _gvn.transform( new (C, 3) CmpPNode( subkid, _gvn.makecon(TypeKlassPtr::make_kid(toop->klass(),true)) ) );
    Node *bol = _gvn.transform( new (C, 2) BoolNode( cmp, BoolTest::eq ) );
    IfNode *iff = create_and_xform_if( control(), bol, PROB_STATIC_FREQUENT, COUNT_UNKNOWN );
    set_control( _gvn.transform( new (C, 1) IfTrueNode ( iff ) ) );
    return       _gvn.transform( new (C, 1) IfFalseNode( iff ) );
  }

  // First load the super-klass's check-offset
Node*superklass=_gvn.transform(new(C,2)KID2KlassNode(superkid));
  Node *p1 = basic_plus_adr( superklass, superklass, sizeof(oopDesc) + Klass::super_check_offset_offset_in_bytes() );
  Node *chk_off = _gvn.transform( new (C, 3) LoadINode( NULL, memory(p1), p1, _gvn.type(p1)->is_ptr() ) );
int cacheoff_con=sizeof(oopDesc)+Klass::secondary_super_kid_cache_offset_in_bytes();
  bool might_be_cache = (find_int_con(chk_off, cacheoff_con) == cacheoff_con);

  // Load from the sub-klass's super-class display list, or a 1-word cache of
  // the secondary superclass list, or a failing value with a sentinel offset
  // if the super-klass is an interface or exceptionally deep in the Java
  // hierarchy and we have to scan the secondary superclass list the hard way.
  // Worst-case type is a little odd: NULL is allowed as a result (usually
  // klass loads can never produce a NULL).
Node*chk_off_X=ConvI2L(chk_off);
Node*subklass=_gvn.transform(new(C,2)KID2KlassNode(subkid));
  Node *p2 = _gvn.transform( new (C, 4) AddPNode(subklass,subklass,chk_off_X) );
  // For some types like interfaces the following loadKlass is from a 1-word
  // cache which is mutable so can't use immutable memory.  Other
  // types load from the super-class display table which is immutable.

  // CNC - I do not understand how this can ever work: immutable_memory() is
  // not typed as read-only, instead it is the initial StartNode memory which
  // is definitely writable.  The LoadKlass then ends up with a memory edge
  // from the program start and also it happily aliases with anybody who
  // writes all of memory (such as locks or calls).  Since it aliases it is
  // anti-dependent on such calls and must happen before all calls, but
  // obviously the load cannot happen until the address is available and the
  // address is typically the result of some call.
  //Node *kmem = might_be_cache ? memory(p2) : immutable_memory();
Node*kmem=memory(p2);
Node*nkid=_gvn.transform(new(C,3)LoadKlassNode(NULL,kmem,p2,_gvn.type(p2)->is_ptr(),TypeKlassPtr::KID_OR_NULL));

  // Compile speed common case: ARE a subtype and we canNOT fail
  if( superkid == nkid )
    return top();             // false path is dead; no test needed.

  // See if we get an immediate positive hit.  Happens roughly 83% of the
  // time.  Test to see if the value loaded just previously from the subklass
  // is exactly the superklass.
Node*cmp1=_gvn.transform(new(C,3)CmpPNode(superkid,nkid));
  Node *bol1 = _gvn.transform( new (C, 2) BoolNode( cmp1, BoolTest::eq ) );
  IfNode *iff1 = create_and_xform_if( control(), bol1, PROB_LIKELY(0.83f), COUNT_UNKNOWN );
  Node *iftrue1 = _gvn.transform( new (C, 1) IfTrueNode ( iff1 ) );
  set_control(    _gvn.transform( new (C, 1) IfFalseNode( iff1 ) ) );

  // Compile speed common case: Check for being deterministic right now.  If
  // chk_off is a constant and not equal to cacheoff then we are NOT a
  // subklass.  In this case we need exactly the 1 test above and we can
  // return those results immediately.
  if (!might_be_cache) {
    Node* not_subtype_ctrl = control();
    set_control(iftrue1); // We need exactly the 1 test above 
    return not_subtype_ctrl;
  }

  // Gather the various success & failures here
  RegionNode *r_ok_subtype = new (C, 4) RegionNode(4);
  record_for_igvn(r_ok_subtype);
  RegionNode *r_not_subtype = new (C, 3) RegionNode(3);
  record_for_igvn(r_not_subtype);

  r_ok_subtype->init_req(1, iftrue1);

  // Check for immediate negative hit.  Happens roughly 11% of the time (which
  // is roughly 63% of the remaining cases).  Test to see if the loaded
  // check-offset points into the subklass display list or the 1-element
  // cache.  If it points to the display (and NOT the cache) and the display
  // missed then it's not a subtype.
  Node *cacheoff = _gvn.intcon(cacheoff_con);
  Node *cmp2 = _gvn.transform( new (C, 3) CmpINode( chk_off, cacheoff ) );
  Node *bol2 = _gvn.transform( new (C, 2) BoolNode( cmp2, BoolTest::ne ) );
  IfNode *iff2 = create_and_xform_if( control(), bol2, PROB_LIKELY(0.63f), COUNT_UNKNOWN );
  r_not_subtype->init_req(1, _gvn.transform( new (C, 1) IfTrueNode (iff2) ) );
  set_control(                _gvn.transform( new (C, 1) IfFalseNode(iff2) ) );

  // Check for self.  Very rare to get here, but its taken 1/3 the time.
  // No performance impact (too rare) but allows sharing of secondary arrays
  // which has some footprint reduction.
Node*cmp3=_gvn.transform(new(C,3)CmpPNode(subkid,superkid));
  Node *bol3 = _gvn.transform( new (C, 2) BoolNode( cmp3, BoolTest::eq ) );
  IfNode *iff3 = create_and_xform_if( control(), bol3, PROB_LIKELY(0.36f), COUNT_UNKNOWN );
  r_ok_subtype->init_req(2, _gvn.transform( new (C, 1) IfTrueNode ( iff3 ) ) );
  set_control(               _gvn.transform( new (C, 1) IfFalseNode( iff3 ) ) );

  // Now do a linear scan of the secondary super-klass array.  Again, no real
  // performance impact (too rare) but it's gotta be done.
  // (The stub also contains the self-check of subklass == superklass.
  // Since the code is rarely used, there is no penalty for moving it
  // out of line, and it can only improve I-cache density.)
  Node* psc = _gvn.transform(
    new (C, 3) PartialSubtypeCheckNode(control(), subklass, superklass) );

Node*cmp4=_gvn.transform(new(C,3)CmpINode(psc,_gvn.intcon(0)));
  Node *bol4 = _gvn.transform( new (C, 2) BoolNode( cmp4, BoolTest::ne ) );
  IfNode *iff4 = create_and_xform_if( control(), bol4, PROB_FAIR, COUNT_UNKNOWN );
  r_not_subtype->init_req(2, _gvn.transform( new (C, 1) IfTrueNode (iff4) ) );
  r_ok_subtype ->init_req(3, _gvn.transform( new (C, 1) IfFalseNode(iff4) ) );

  // Return false path; set default control to true path.
  set_control( _gvn.transform(r_ok_subtype) );
  return _gvn.transform(r_not_subtype);
}

// --- type_check_receiver ---------------------------------------------------
// Profile-driven exact type check:
Node* GraphKit::type_check_receiver(Node* receiver, ciKlass* klass,
                                    float prob,
                                    Node* *casted_receiver) {
const TypeKlassPtr*tklass=TypeKlassPtr::make_kid(klass,true/*exact*/);
  Node* recv_klass = load_object_klass(receiver);
  Node* want_klass = makecon(tklass);
  Node* cmp = _gvn.transform( new(C, 3) CmpPNode(recv_klass, want_klass) );
  Node* bol = _gvn.transform( new(C, 2) BoolNode(cmp, BoolTest::eq) );
  IfNode* iff = create_and_xform_if(control(), bol, prob, COUNT_UNKNOWN);
  set_control( _gvn.transform( new(C, 1) IfTrueNode (iff) ));
  Node* fail = _gvn.transform( new(C, 1) IfFalseNode(iff) );

  const TypeOopPtr* recv_xtype = tklass->as_instance_type();
  assert(recv_xtype->klass_is_exact(), "");

  // Subsume downstream occurrences of receiver with a cast to
  // recv_xtype, since now we know what the type will be.
  Node* cast = new(C, 2) CheckCastPPNode(control(), receiver, recv_xtype);
  (*casted_receiver) = _gvn.transform(cast);
  // (User must make the replace_in_map call.)

  return fail;
}


//-------------------------------gen_instanceof--------------------------------
// Generate an instance-of idiom.  Used by both the instance-of bytecode
// and the reflective instance-of call.
Node*GraphKit::gen_instanceof(Node*subobj,Node*superklass,const char*msg,CPData_Null*cpdn){
  C->set_has_split_ifs(true); // Has chance for split-if optimization
  assert( !stopped(), "dead parse path should be checked in callers" );
assert(!superklass||//OK for NULL superklass meaning no-instances possible, only NULLs allowed
         _gvn.type(superklass)->is_klassptr()->_ptr == TypePtr::Constant ||
         _gvn.type(superklass)->is_klassptr()->_ptr == TypePtr::NotNull,
         "must check for not-null not-dead klass in callers");

  // Make the merge point
  enum { _obj_path = 1, _fail_path, _null_path, PATH_LIMIT };
  RegionNode* region = new(C, PATH_LIMIT) RegionNode(PATH_LIMIT);
  Node*       phi    = new(C, PATH_LIMIT) PhiNode(region, TypeInt::BOOL);
  C->set_has_split_ifs(true); // Has chance for split-if optimization

  // Null check; get casted pointer; set region slot 3
  Node* null_ctl = top();
Node*not_null_obj=null_check_oop(subobj,&null_ctl,!cpdn->saw_null(),cpdn);

  // If not_null_obj is dead, only null-path is taken
  if (stopped()) {              // Doing instance-of on a NULL?
    set_control(null_ctl);
    return intcon(0);
  }
  region->init_req(_null_path, null_ctl);
  phi   ->init_req(_null_path, intcon(0)); // Set null path value

  // Load the object's klass
  Node* obj_klass = load_object_klass(not_null_obj);

  // Generate the subtype check
Node*not_subtype_ctrl=gen_subtype_check(obj_klass,superklass,_gvn.type(subobj));

  // Plug in the success path to the general merge in slot 1.
  region->init_req(_obj_path, control());
  phi   ->init_req(_obj_path, intcon(1));

  // Plug in the failing path to the general merge in slot 2.
  region->init_req(_fail_path, not_subtype_ctrl);
  phi   ->init_req(_fail_path, intcon(0));

  // Return final merged results
  set_control( _gvn.transform(region) );
  record_for_igvn(region);
  return phi;                   // no 'transform' call, because library_call.cpp also hacks
}

//-------------------------------gen_checkcast---------------------------------
// Generate a checkcast idiom.  Used by both the checkcast bytecode and the
// array store bytecode.  Stack must be as-if BEFORE doing the bytecode so the
// uncommon-trap paths work.  Adjust stack after this call.
// If failure_control is supplied and not null, it is filled in with
// the control edge for the cast failure.  Otherwise, an appropriate
// uncommon trap or exception is thrown.
Node* GraphKit::gen_checkcast(Node *obj, Node* superklass,
                              Node* *failure_control, const char *msg, CPData_Null *cpdn) {
  kill_dead_locals();           // Benefit all the uncommon traps
  const TypePtr *toop = superklass
    ? TypeOopPtr::make_from_klass_unique(_gvn.type(superklass)->is_klassptr()->klass())
    : TypePtr::NULL_PTR;
  
  // See if the cast is trivially true: object has same klass (inexact) as the
  // desired klass (which is exact).  The superklass must be exact (which it
  // always is exact for arrays) lest we get a subklass of the super.  The
  // object does not need to be exact.
  const TypeOopPtr *tobj = obj->bottom_type()->isa_oopptr();
  if( tobj && tobj->klass() && // many different array types will make a tobj with no klass
      TypeKlassPtr::make(tobj->klass()) == toop && toop->klass_is_exact() )
    return obj;

  // Make the merge point
  enum { _obj_path = 1, _null_path, PATH_LIMIT };
  RegionNode* region = new (C, PATH_LIMIT) RegionNode(PATH_LIMIT);
  Node*       phi    = new (C, PATH_LIMIT) PhiNode(region, toop);
  C->set_has_split_ifs(true); // Has chance for split-if optimization

  // If we see an unexpected null at a check-cast we record it and force a
  // recompile; the offending check-cast will be compiled to handle NULLs.
if(obj==null()||//The stupid -Xcomp case?
      !superklass )             // Abstract-class w/no implementors?
    cpdn->_null = 1;            // Seeing a NULL here now

  // Null check; get casted pointer; set region slot 3
  Node* null_ctl = top();
Node*not_null_obj=null_check_oop(obj,&null_ctl,!cpdn->saw_null(),cpdn);

  // If not_null_obj is dead, only null-path is taken
  if (stopped()) {              // Doing instance-of on a NULL?
    set_control(null_ctl);
    return null();
  }
  region->init_req(_null_path, null_ctl);
  phi   ->init_req(_null_path, null());  // Set null path value

  Node* cast_obj = NULL;        // the casted version of the object

  if (cast_obj == NULL) {
    // Load the object's klass
    Node* obj_klass = load_object_klass(not_null_obj);

    // Generate the subtype check
Node*not_subtype_ctrl=gen_subtype_check(obj_klass,superklass,_gvn.type(obj));

    // Plug in success path into the merge
    cast_obj = _gvn.transform(new (C, 2) CheckCastPPNode(control(),
                                                         not_null_obj, toop));
    // Rarely, the type system can prove failure when the control-flow opts cannot.
    // e.g., casting SomeInterface[] to AnotherInterface[] as in the JCK test
    // javasoft.sqe.tests.lang.conv083.conv08304.conv08304
    if( cast_obj->bottom_type()->empty() ) { // impossible result, means subtype test must always fail
      PreserveJVMState pjvms(this);
      builtin_throw(Deoptimization::Reason_cast_check, "checkcast is throwing", cpdn, cpdn->did_fail(), /*must_throw=*/true);
    }

    // Failure path ends in uncommon trap (or may be dead - failure impossible)
    if (failure_control == NULL) {
      if (not_subtype_ctrl != top()) { // If failure is possible
        PreserveJVMState pjvms(this);
        set_control(not_subtype_ctrl);
        builtin_throw(Deoptimization::Reason_cast_check, "checkcast is throwing", cpdn, cpdn->did_fail(), /*must_throw=*/true);
      }
    } else {
      (*failure_control) = not_subtype_ctrl;
    }
  }

  region->init_req(_obj_path, control());
  phi   ->init_req(_obj_path, cast_obj);

  // A merge of NULL or Casted-NotNull obj
  Node* res = _gvn.transform(phi);

  // Note I do NOT always 'replace_in_map(obj,result)' here.
  //  if( tk->klass()->can_be_primary_super()  ) 
    // This means that if I successfully store an Object into an array-of-String
    // I 'forget' that the Object is really now known to be a String.  I have to
    // do this because we don't have true union types for interfaces - if I store
    // a Baz into an array-of-Interface and then tell the optimizer it's an
    // Interface, I forget that it's also a Baz and cannot do Baz-like field
    // references to it.  FIX THIS WHEN UNION TYPES APPEAR!
  // Azul has UNION TYPES, so always do replace_in_map
replace_in_map(obj,res);

  // Return final merged results
  set_control( _gvn.transform(region) );
  record_for_igvn(region);
  return res;
}

//------------------------------insert_mem_bar---------------------------------
// Memory barrier to avoid floating things around
// The membar serves as a pinch point between both control and all memory slices.
Node* GraphKit::insert_mem_bar(int opcode, Node* precedent) {
  MemBarNode* mb = MemBarNode::make(C, opcode, Compile::AliasIdxBot, precedent);
  mb->init_req(TypeFunc::Control, control());
  mb->init_req(TypeFunc::Memory,  reset_memory());
  Node* membar = _gvn.transform(mb);
  set_control(_gvn.transform(new (C, 1) ProjNode(membar,TypeFunc::Control) ));
  set_all_memory_call(membar);
  return membar;
}
  
//-------------------------insert_mem_bar_volatile----------------------------
// Memory barrier to avoid floating things around
// The membar serves as a pinch point between both control and memory(alias_idx).
// If you want to make a pinch point on all memory slices, do not use this
// function (even with AliasIdxBot); use insert_mem_bar() instead.
Node* GraphKit::insert_mem_bar_volatile(int opcode, int alias_idx, Node* precedent) {
  // When Parse::do_put_xxx updates a volatile field, it appends a series
  // of MemBarVolatile nodes, one for *each* volatile field alias category.
  // The first membar is on the same memory slice as the field store opcode.
  // This forces the membar to follow the store.  (Bug 6500685 broke this.)
  // All the other membars (for other volatile slices, including AliasIdxBot,
  // which stands for all unknown volatile slices) are control-dependent
  // on the first membar.  This prevents later volatile loads or stores
  // from sliding up past the just-emitted store.

  MemBarNode* mb = MemBarNode::make(C, opcode, alias_idx, precedent);
  mb->set_req(TypeFunc::Control,control());
  if (alias_idx == Compile::AliasIdxBot) {
    mb->set_req(TypeFunc::Memory, merged_memory()->base_memory());
  } else {
    mb->set_req(TypeFunc::Memory, memory(alias_idx));
  }
  Node* membar = _gvn.transform(mb);
  set_control(_gvn.transform(new (C, 1) ProjNode(membar, TypeFunc::Control)));
  if (alias_idx == Compile::AliasIdxBot) {
    merged_memory()->set_base_memory(_gvn.transform(new (C, 1) ProjNode(membar, TypeFunc::Memory)));
  } else {
    set_memory(_gvn.transform(new (C, 1) ProjNode(membar, TypeFunc::Memory)),alias_idx);
  }
  return membar;
}
  
//------------------------------shared_lock------------------------------------
// Emit locking code.
void GraphKit::shared_lock(Node*obj,CPData_Null*lock_cpdn){
  // bci is either a monitorenter bc or InvocationEntryBci
  if (stopped())                // Dead monitor?
    return ;

  assert(dead_locals_are_killed(), "should kill locals before sync. point");
C->set_has_split_ifs(true);//Has chance for lock coarsening

  MergeMemNode * mem = merged_memory();

  // Add monitor to debug info for the slow path.  If we block inside the
  // slow path and de-opt, we need the monitor hanging around
map()->push_monitor(obj);

  const TypeFunc *tf = LockNode::lock_type();
  LockNode *lock = new (C, tf->domain()->cnt()) LockNode(C, tf);

  lock->init_req( TypeFunc::Control, control() );
  lock->init_req( TypeFunc::Memory , mem );
  lock->init_req( TypeFunc::I_O    , top() )     ;   // does no i/o
  lock->init_req( TypeFunc::FramePtr, frameptr() );
  lock->init_req( TypeFunc::ReturnAdr, top() );

  lock->init_req(TypeFunc::Parms + 0, obj);
  add_safepoint_edges(lock, lock_cpdn,/*must_throw=*/false);

  lock = _gvn.transform( lock )->as_Lock();

  // Note that the unlock defines a new memory state
set_predefined_output_for_runtime_call(lock,NULL,NULL);

  // Add this to the worklist so that the lock can be eliminated
  record_for_igvn(lock);
}


//------------------------------shared_unlock----------------------------------
// Emit unlocking code.
void GraphKit::shared_unlock(Node*obj){
  // bci is either a monitorenter bc or InvocationEntryBci
  if (stopped()) {               // Dead monitor?
    map()->pop_monitor();        // Kill monitor from debug info
    return;
  }

  // Milli-code locking call
  const TypeFunc *tf = OptoRuntime::complete_monitor_exit_Type();
  UnlockNode *unlock = new (C, tf->domain()->cnt()) UnlockNode(C, tf);
Node*mem=set_predefined_input_for_runtime_call(unlock);

  unlock->init_req(TypeFunc::Parms + 0, obj);
  unlock = _gvn.transform(unlock)->as_Unlock();

  // Note that the unlock defines a new memory state
set_predefined_output_for_runtime_call(unlock,NULL,NULL);

record_for_igvn(unlock);

  // Kill monitor from debug info
  map()->pop_monitor( );
}

//-------------------------------get_layout_helper-----------------------------
// If the given klass is a constant or known to be an array,
// fetch the constant layout helper value into constant_value
// and return (Node*)NULL.  Otherwise, load the non-constant
// layout helper value, and return the node which represents it.
// This two-faced routine is useful because allocation sites
// almost always feature constant types.
Node*GraphKit::get_layout_helper(Node*kid_node,jint&constant_value,jint&ekid){
const TypeKlassPtr*inst_klass=_gvn.type(kid_node)->isa_klassptr();
  if (!StressReflectiveCode && inst_klass != NULL) {
    ciKlass* klass = inst_klass->klass();
    bool    xklass = inst_klass->klass_is_exact();
    if (xklass || klass->is_array_klass()) {
      jint lhelper = klass->layout_helper();
      if (lhelper != Klass::_lh_neutral_value) {
        constant_value = lhelper;
        if( xklass && klass->is_obj_array_klass() ) {
          ekid = klass->as_obj_array_klass()->element_klass()->klassId();
        } else {
          ekid = 0;
        }
        return (Node*) NULL;
      }
    }
  }
  constant_value = Klass::_lh_neutral_value;  // put in a known value
  ekid = 0;
  Node *klass_node = inst_klass->_is_kid ? _gvn.transform( new (C, 2) KID2KlassNode(kid_node) ) : kid_node;
  Node* lhp = basic_plus_adr(klass_node, klass_node, Klass::layout_helper_offset_in_bytes() + sizeof(oopDesc));
  return make_load(NULL, lhp, TypeInt::INT, T_INT);
}

//=============================================================================
// 
//                              A L L O C A T I O N 
//
// Allocation attempts to be fast in the case of frequent small objects.

//---------------------------new_instance-------------------------------------
// Basic graph construction of allocation.  Passed in the KID Node which is
// usually a simple integer constant (variable KIDs only appear in things like
// Object.clone, Class.newInstance, or Arrays.copyOf).  It will work equally
// well for either, and the graph will fold nicely if the optimizer later
// reduces the type to a constant.
Node* GraphKit::new_instance(Node *kid_node, Node *extra_slow_test, CPData *cpd) {
  // Compute size in doublewords
  // The size is always an integral number of doublewords, represented
  // as a positive bytewise size stored in the klass's layout_helper.
  // The layout_helper also encodes (in a low bit) the need for a slow path.
  jint  layout_con = Klass::_lh_neutral_value;
jint dummy;
Node*layout_val=get_layout_helper(kid_node,layout_con,dummy);
  int   layout_is_con = (layout_val == NULL);
  assert0( dummy == 0 );

  // Generate the initial go-slow test.  It's either ALWAYS (return a
  // Node for 1) or NEVER (return a NULL) or perhaps (in the reflective
  // case) a computed value derived from the layout_helper.
  Node* initial_slow_test = NULL;
  if (layout_is_con) {
    assert(!StressReflectiveCode, "stress mode does not use these paths");
    bool must_go_slow = Klass::layout_helper_needs_slow_path(layout_con);
    initial_slow_test = must_go_slow? intcon(1): extra_slow_test;

  } else {   // reflective case
    // This reflective path is used by Unsafe.allocateInstance.
    // (It may be stress-tested by specifying StressReflectiveCode.)
    // Basically, we want to get into the VM is there's an illegal argument.
    Node* bit = intcon(Klass::_lh_instance_slow_path_bit);
    initial_slow_test = _gvn.transform( new (C, 3) AndINode(layout_val, bit) );
    if (extra_slow_test != intcon(0)) {
      initial_slow_test = _gvn.transform( new (C, 3) OrINode(initial_slow_test, extra_slow_test) );
    }
    // (Macro-expander will further convert this to a Bool, if necessary.)
  }

  // Find the size in bytes.  This is easy; it's the layout_helper.
  // The size value must be valid even if the slow path is taken.
  Node* size = NULL;
  if (layout_is_con) {
size=_gvn.longcon(Klass::layout_helper_size_in_bytes(layout_con));
  } else {   // reflective case
    // This reflective path is used by clone and Unsafe.allocateInstance.
size=ConvI2L(layout_val);

    // Clear the low bits to extract layout_helper_size_in_bytes:
    assert((int)Klass::_lh_instance_slow_path_bit < BytesPerLong, "clear bit");
Node*mask=_gvn.longcon(~(intptr_t)right_n_bits(LogBytesPerLong));
size=_gvn.transform(new(C,3)AndLNode(size,mask));
  }

  // This is a precise notnull oop of the klass.
  // (Actually, it need not be precise if this is a reflective allocation.)
  // It's what we cast the result to.
const TypeKlassPtr*tklass=_gvn.type(kid_node)->isa_klassptr();
if(!tklass)tklass=TypeKlassPtr::KID;
  const TypeOopPtr* oop_type = tklass->as_instance_type();


  // Allocation Nodes.  Pass in the KID, so we can set the mark words in the
  // template.  Pass in object size in bytes.  Upon completion the new object
  // will be coherently zero'd.  Later in the compiler, the AllocateNode will
  // be macro-expanded into a fast/slow path.  This construction does not
  // throw any exceptions in the graph.  The slow path will be allowed to
  // throw unexpected exceptions like OutOfMemory via deoptimizing.
Node*thr=_gvn.transform(new(C,1)ThreadLocalNode());
  AllocateNode *alloc = new (C, TypeFunc::Parms+5) AllocateNode(thr,kid_node, size, initial_slow_test, oop_type, oop_type->make_new_alloc_sig(false) );

return new_object(alloc,oop_type,cpd);
}


//-------------------------------new_array-------------------------------------
// Used by both the 'newarray' bytecodes - for which the ciArrayKlass is
// prefectly known, and by Object.clone/Reflect.newArray - for which the
// ciArrayKlass is a conservative approximation.
Node*GraphKit::new_array(Node*kid_node,//array klass (maybe variable)
                          Node* length,         // number of array elements
                          CPData *cpd
                          ) {
  jint  layout_con = Klass::_lh_neutral_value;
  jint  ekid_con;
  Node* layout_val = get_layout_helper(kid_node, layout_con, ekid_con);
  int   layout_is_con = (layout_val == NULL);

  if (!layout_is_con && !StressReflectiveCode) {
    // This is a reflective array creation site.
    // Optimistically assume that it is a subtype of Object[],
    // so that we can fold up all the address arithmetic.
    layout_con = Klass::array_layout_helper(T_OBJECT);
    Node* cmp_lh = _gvn.transform( new(C, 3) CmpINode(layout_val, intcon(layout_con)) );
    Node* bol_lh = _gvn.transform( new(C, 2) BoolNode(cmp_lh, BoolTest::eq) );
    { BuildCutout unless(this, bol_lh, PROB_MAX);
      uncommon_trap(Deoptimization::Reason_cast_check, NULL, "heroic assumption of subtype of Object[] failed", false);
    }
    kid_node = _gvn.transform( new(C,2) CastPPNode(kid_node,TypeKlassPtr::make_kid(ciObjArrayKlass::make(ciEnv::current()->Object_klass()), false)));
    layout_val = NULL;
    layout_is_con = true;
  }

  // Generate the initial go-slow test.  Make sure we do not overflow
  // if length is huge (near 2Gig) or negative!  We do not need
  // exact double-words here, just a close approximation of needed
  // double-words.  We can't add any offset or rounding bits, lest we
  // take a size -1 of bytes and make it positive.  Use an unsigned
  // compare, so negative sizes look hugely positive.
  // AZUL: the normal millicode test handles this case fine.
Node*initial_slow_test=_gvn.intcon(0);

  // Sharpen result type: if we make an array-of-interface with just one
  // implementor, we both make an array-of-interface (kid_node is captured above)
  // AND we know the result can only hold the single concrete type.
const TypeKlassPtr*tklass=_gvn.type(kid_node)->isa_klassptr();
if(!tklass)tklass=TypeKlassPtr::KID;
  const TypeOopPtr *oop_type = TypeOopPtr::make_from_klass_unique(tklass->klass())->is_oopptr();

  // Cast-away nullness of the result.  Allocation does not return a null (but
  // it might throw an OOM).
oop_type=oop_type->cast_to_ptr_type(TypePtr::NotNull)->is_oopptr();

  // If the incoming KID is exact, then so is the result.  The KID is exact
  // for newarray bytecodes but approximate for Object.clone().
  if( tklass->klass_is_exact() )
    oop_type->cast_to_exactness(true);

  // --- Size Computation ---
  // array_size = round_to_heap(array_header + (length << elem_shift));
  // where round_to_heap(x) == round_to(x, MinObjAlignmentInBytes)
  // and round_to(x, y) == ((x + y-1) & ~(y-1))
  // The rounding mask is strength-reduced, if possible.
  int round_mask = MinObjAlignmentInBytes - 1;
  Node* header_size = NULL;
  int   header_size_min  = arrayOopDesc::base_offset_in_bytes(T_BYTE);
  // (T_BYTE has the weakest alignment and size restrictions...)
  if (layout_is_con) {
    int       hsize  = Klass::layout_helper_header_size(layout_con);
    int       eshift = Klass::layout_helper_log2_element_size(layout_con);
    BasicType etype  = Klass::layout_helper_element_type(layout_con);
    if ((round_mask & ~right_n_bits(eshift)) == 0)
      round_mask = 0;  // strength-reduce it if it goes away completely
    assert((hsize & right_n_bits(eshift)) == 0, "hsize is pre-rounded");
    assert(header_size_min <= hsize, "generic minimum is smallest");
    header_size_min = hsize;
    header_size = intcon(hsize + round_mask);
  } else {
    Node* hss   = intcon(Klass::_lh_header_size_shift);
    Node* hsm   = intcon(Klass::_lh_header_size_mask);
    Node* hsize = _gvn.transform( new(C, 3) URShiftINode(layout_val, hss) );
    hsize       = _gvn.transform( new(C, 3) AndINode(hsize, hsm) );
    Node* mask  = intcon(round_mask);
    header_size = _gvn.transform( new(C, 3) AddINode(hsize, mask) );
  }

  // Compute EKID.  If this is not an oop-ary, then we don't need one.
Node*ekid=NULL;
const TypeAryPtr*oop_type_ary=oop_type->isa_aryptr();
  // Statically known as not-an-oop-array?
  if( !tklass->klass()->is_obj_array_klass() ) { // Not an oop-array?
    ekid = _gvn.intcon(0);                // Then no KID required
  } else if( ekid_con ) {                 // Constant element-kid: statically known as a oop array
    // must be a compile-time-known oop array type
    const TypeKlassPtr *tek = TypeKlassPtr::make_kid(tklass->klass()->as_obj_array_klass()->element_klass(),true);
ekid=_gvn.makecon(tek);
  } else {                      // Unknown: might e.g. byte[] or String[]
    // Load element klass
Node*klass_node=_gvn.transform(new(C,2)KID2KlassNode(kid_node));
    Node* lekp = basic_plus_adr(klass_node, objArrayKlass::element_klass_offset_in_bytes() + sizeof(oopDesc));
const TypePtr*tek=_gvn.type(lekp)->is_ptr();
    // This eklass load cannot happen unless we pass the guard that we are
    // really an Object[] and e.g. a byte[]
    Node *eklass = _gvn.transform(new (C, 3) LoadKlassNode(control(), immutable_memory(), lekp, tek, TypeKlassPtr::OBJECT));
    // Load element KID
    Node *kid_adr = basic_plus_adr(eklass, Klass::klassId_offset_in_bytes() + (int)sizeof(oopDesc));
    ekid = _gvn.transform(new(C,3) LoadKlassNode(NULL,immutable_memory(),kid_adr,_gvn.type(kid_adr)->is_ptr(),TypeKlassPtr::KID));
  }

  Node* elem_shift = NULL;
  if (layout_is_con) {
    int eshift = Klass::layout_helper_log2_element_size(layout_con);
    if (eshift != 0)
      elem_shift = intcon(eshift);
  } else {
    // There is no need to mask or shift this value.
    // The semantics of LShiftINode include an implicit mask to 0x1F.
    assert(Klass::_lh_log2_element_size_shift == 0, "use shift in place");
    elem_shift = layout_val;
  }

  // Transition to native address size for all offset calculations:
Node*lengthx=ConvI2L(length);
Node*headerx=ConvI2L(header_size);
  // CNC - Removed this bogus constraint; a negative length can be passed in,
  // and we'll bail out in the stub and force a slow-path.
  //{ const TypeLong* tllen = _gvn.find_long_type(lengthx);
  //  if (tllen != NULL && tllen->_lo < 0) {
  //    // Add a manual constraint to a positive range.  Cf. array_element_address.
  //    jlong size_max = arrayOopDesc::max_array_length(T_BYTE);
  //    if (size_max > tllen->_hi)  size_max = tllen->_hi;
  //    const TypeLong* tlcon = TypeLong::make(CONST64(0), size_max, Type::WidenMin);
  //    lengthx = _gvn.transform( new (C, 2) ConvI2LNode(length, tlcon));
  //  }
  //}

  // Combine header size (plus rounding) and body size.  Then round down.
  // This computation cannot overflow, because it is used only in two
  // places, one where the length is sharply limited, and the other
  // after a successful allocation.
  Node* abody = lengthx;
  if (elem_shift != NULL)
abody=_gvn.transform(new(C,3)LShiftLNode(lengthx,elem_shift));
Node*size=_gvn.transform(new(C,3)AddLNode(headerx,abody));
  if (round_mask != 0) {
Node*mask=_gvn.longcon(~round_mask);
size=_gvn.transform(new(C,3)AndLNode(size,mask));
  }
  // else if round_mask == 0, the size computation is self-rounding

  // Allocation Nodes.  Pass in the KID, so we can set the mark words in the
  // template.  Pass in object size in bytes.  Upon completion the new object
  // will be coherently zero'd.  Later in the compiler, the AllocateNode will
  // be macro-expanded into a fast/slow path.  This construction does not
  // throw any exceptions in the graph.  The slow path will be allowed to
  // throw unexpected exceptions like OutOfMemory via deoptimizing.
Node*thr=_gvn.transform(new(C,1)ThreadLocalNode());
  AllocateArrayNode *alloc = new (C, TypeFunc::Parms+7) AllocateArrayNode(thr, kid_node, size, initial_slow_test, oop_type, length, ekid);

return new_object(alloc,oop_type,cpd);
}

//-------------------------------new_object-----------------------------------
// Common helper used by new_instance and new_array
Node* GraphKit::new_object( AllocateNode* alloc, const TypeOopPtr *oop_type, CPData *cpd ) {

  // Capture pre-call memory state
MergeMemNode*pre_mem=merged_memory();

  // Bring in the full Java state, in case we take a GC during allocation
  // or want to throw an OOM
set_predefined_input_for_runtime_call(alloc);
  // Length is known small constant at compile time, so do not pass it in.
  add_safepoint_edges(alloc,/*must_throw=*/false, cpd);

Node*call=_gvn.transform(alloc);
  // Control from the call
  set_control(_gvn.transform( new (C, 1) ProjNode(call,TypeFunc::Control) ));
  // Memory & IO from the call
  Node *newmem = _gvn.transform( new (C, 1) ProjNode(call, TypeFunc::Memory) );
  // The result
Node*new_obj_adr=_gvn.transform(new(C,1)ProjNode(call,TypeFunc::Parms));

  // The memory from the call is only valid for fields in the object just made
  // and Raw memory.  Set parser memory for all fields of the object.  Some
  // fields have never been referenced, but they need to have their memory set
  // just the same, possible creating some alias indices.  EscapeMem will join
  // the private freshly-made memory with the pre-allocation-mem.
set_all_memory(pre_mem);
  escape_new_memory( oop_type, oopDesc::mark_offset_in_bytes (), newmem );
  if( alloc->is_AllocateArray() ) { // Arrays?
    // Array field alias indices
    escape_new_memory( oop_type, arrayOopDesc::length_offset_in_bytes(),newmem);
    if( alloc->in(AllocateNode::EKID)->bottom_type() != TypeInt::ZERO )
      escape_new_memory( oop_type, objArrayOopDesc::ekid_offset_in_bytes(),newmem);
    // Also do the one alias index for the whole array body
    escape_new_memory( oop_type, arrayOopDesc::length_offset_in_bytes()+8,newmem);
  } else {
    ciInstanceKlass* ik = oop_type->klass()->as_instance_klass();
for(int i=0;i<ik->nof_nonstatic_fields();i++){
      int off = ik->nonstatic_field_at(i)->offset_in_bytes();
      escape_new_memory(oop_type, off, newmem);
    }  
  }
  return new_obj_adr;
}


// The following "Ideal_foo" functions are placed here because they recognize
// the graph shapes created by the functions immediately above.

//---------------------------Ideal_allocation----------------------------------
// Given an oop pointer or raw pointer, see if it feeds from an AllocateNode.
AllocateNode* AllocateNode::Ideal_allocation(Node* ptr, PhaseTransform* phase) {
  if (ptr == NULL) {     // reduce dumb test in callers
    return NULL;
  }
  // Azul's AllocateNode produces a strongly typed result directly.
  //if (ptr->is_CheckCastPP()) {  // strip a raw-to-oop cast
  //  ptr = ptr->in(1);
  //  if (ptr == NULL)  return NULL;
  //}
  if (ptr->is_Proj()) {
    Node* allo = ptr->in(0);
    if (allo != NULL && allo->is_Allocate()) {
      return allo->as_Allocate();
    }
  }
  // Report failure to match.
  return NULL;
}

// Fancy version which also strips off an offset (and reports it to caller).
AllocateNode* AllocateNode::Ideal_allocation(Node* ptr, PhaseTransform* phase,
                                             intptr_t& offset) {
  Node* base = AddPNode::Ideal_base_and_offset(ptr, phase, offset);
  if (base == NULL)  return NULL;
  return Ideal_allocation(base, phase);
}

//---------------------------escape_new_memory---------------------------------
void GraphKit::escape_new_memory( const TypeOopPtr *oop_type, int off, Node *newmem) {
  const TypePtr *tp = oop_type->add_offset(off);
int idx=C->get_alias_index(tp);
  EscapeMemoryNode *esc = new (C, 3) EscapeMemoryNode(newmem,memory(idx));
set_memory(_gvn.transform(esc),idx);
}

//---------------------------do_array------------------------------------------
void GraphKit::do_array(ciArrayKlass *ci_ary_klass, BasicType elem_type, CPData *cpd) {
  Node* count_val = peek();     // get length, but do not adjust stack
  // Need KID constant to jam in the header or make a VM call.
  // This is the final kind of object that will get made.
  // Note that the KID made is exact.
  Node *kid_node = makecon(TypeKlassPtr::make_kid(ci_ary_klass,true/*exact*/));
  Node *ary = new_array(kid_node,count_val, cpd);
pop();//remove length from stack
push(ary);
}
