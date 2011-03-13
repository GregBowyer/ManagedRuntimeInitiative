/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciCallProfile.hpp"
#include "ciEnv.hpp"
#include "ciExceptionHandler.hpp"
#include "ciMethod.hpp"
#include "ciMethodKlass.hpp"
#include "ciTypeFlow.hpp"
#include "codeProfile.hpp"
#include "compilerOracle.hpp"
#include "dependencies.hpp"
#include "generateOopMap.hpp"
#include "interfaceSupport.hpp"
#include "interpreter_pd.hpp"
#include "jvmtiExport.hpp"
#include "linkResolver.hpp"
#include "methodLiveness.hpp"
#include "methodOop.hpp"
#include "mutexLocker.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

// ciMethod
//
// This class represents a methodOop in the HotSpot virtual
// machine.


// ------------------------------------------------------------------
// ciMethod::ciMethod
//
// Loaded method.
ciMethod::ciMethod(methodHandle h_m) : ciObject(h_m) {
  assert(h_m() != NULL, "no null method");

  // These fields are always filled in in loaded methods.
  _flags = ciFlags(h_m()->access_flags());

  // Easy to compute, so fill them in now.
  _max_stack          = h_m()->max_stack();
  _max_locals         = h_m()->max_locals();  
  _code_size          = h_m()->code_size();
  _intrinsic_id       = h_m()->intrinsic_id();
  _handler_count      = h_m()->exception_table()->length() / 4;
  _invocation_count   = h_m()->invocation_count();
_has_monitors=h_m()->access_flags().has_monitor_bytecodes();
_balanced_monitors=!_has_monitors||h_m()->access_flags().is_monitor_matching();
  _compilable         = h_m()->_compilable;

  _has_c2_code        = (h_m()->lookup_c2() != NULL);

  _has_unloaded_classes_in_sig_initialized = false;
  _is_empty_method_initialized             = false;
  _is_vanilla_constructor_initialized      = false;
  _has_linenumber_table_initialized        = false;
  _compressed_linenumber_table_initialized = false;
  _number_of_breakpoints  = -1;
  _instructions_size      = -1;

  _has_loops              = h_m()->has_loops();
  _has_jsrs               = h_m()->has_jsrs();
  _is_accessor            = h_m()->is_accessor();
  _is_initializer         = h_m()->is_initializer();
  _objectId               = h_m()->objectId();
  _method_blocks          = NULL;

  // Lazy fields, filled in on demand.  Require allocation.
  _code               = NULL;
  _exception_handlers = NULL;
  _liveness           = NULL;
  _flow               = NULL;
_bci2cpd_mapping=NULL;

CompilerThread*C=CompilerThread::current();
  bool is_c1 = C->is_C1Compiler_thread();
if(JvmtiExport::can_hotswap_or_post_breakpoint()&&
      (is_c1 ? is_c1_compilable() : is_c2_compilable())) {
    // 6328518 check hotswap conditions under the right lock.  
MutexLockerAllowGC locker(Compile_lock,C);
    // Did somebody do a JVMTI RedefineClasses while our backs were turned?
    // Or is there a now a breakpoint?
    // (Assumes compiled code cannot handle bkpts; change if UseFastBreakpoints.)
    methodOop h_m_oop = h_m();
    if (h_m_oop->is_old() || h_m_oop->number_of_breakpoints() > 0) {
      set_not_compilable(is_c1 ? 1 : 2);
    }
  }

  if (instanceKlass::cast(h_m()->method_holder())->is_linked()) {
    _can_be_statically_bound = h_m()->can_be_statically_bound();
  } else {
    // Have to use a conservative value in this case.
    _can_be_statically_bound = false;
  }

  // Adjust the definition of this condition to be more useful:
  // %%% take these conditions into account in vtable generation
  if (!_can_be_statically_bound && h_m()->is_private())
    _can_be_statically_bound = true;
  if (_can_be_statically_bound && h_m()->is_abstract())
    _can_be_statically_bound = false;

  ciEnv *env = CURRENT_ENV;
  _monomorphic_lookup     = new (env->arena()) Dict(cmptriple, hashtriple, env->arena());
  _resolve_invoke_lookup  = new (env->arena()) Dict(cmptriple, hashtriple, env->arena());
  _resolve_vtable_lookup  = new (env->arena()) Dict(cmptriple, hashtriple, env->arena());
  // generating _signature may allow GC and therefore move m.
  // These fields are always filled in.
  _name = env->get_object(h_m()->name())->as_symbol();
  _holder = env->get_object(h_m()->method_holder())->as_instance_klass();
  ciSymbol* sig_symbol = env->get_object(h_m()->signature())->as_symbol();
  _signature = new (env->arena()) ciSignature(_holder, sig_symbol);

  // Clone a compile-lock non-changing CodeProfile, for the compilation.
  { StripedMutexLocker ml(OsrList_locks, h_m());
    CodeProfile *cp = h_m()->codeprofile(false);
    _codeprofile = cp ? cp->clone_into_arena(env->arena()) : NULL;
  }
}


ciMethod::ciMethod(ciInstanceKlass* holder,
                   ciSymbol* name,
                   ciSymbol* signature) : ciObject(ciMethodKlass::make()) {
  // These fields are always filled in.
  _name = name;
  _holder = holder;
  _signature = new (CURRENT_ENV->arena()) ciSignature(_holder, signature);
  _intrinsic_id = vmIntrinsics::_none;
  _liveness = NULL;
  _can_be_statically_bound = false;
  _flow = NULL;
_bci2cpd_mapping=NULL;

  _code = NULL;
  _exception_handlers = NULL;

  _monomorphic_lookup    = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());
  _resolve_invoke_lookup = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());
  _resolve_vtable_lookup = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());

  _has_unloaded_classes_in_sig_initialized = false;

_codeprofile=NULL;
}


// ------------------------------------------------------------------
// ciMethod::ciMethod
//
// Unloaded method.
ciMethod::ciMethod(FAMPtr old_cim):ciObject(old_cim){
  FAM->mapNewToOldPtr(this, old_cim);

  AccessFlags af;
  af.set_flags_raw(FAM->getInt("((struct ciMethod*)%p)->_flags->_flags", old_cim));
  ciFlags flags(af);
  _flags = flags;

  // Easy to compute, so fill them in now.
  _max_stack          = FAM->getInt("((struct ciMethod*)%p)->_max_stack", old_cim);
  _max_locals         = FAM->getInt("((struct ciMethod*)%p)->_max_locals", old_cim);
  _intrinsic_id       = (vmIntrinsics::ID)FAM->getInt("((struct ciMethod*)%p)->_intrinsic_id", old_cim);
  _invocation_count   = FAM->getInt("((struct ciMethod*)%p)->_invocation_count", old_cim);
  _has_monitors       = FAM->getInt("((struct ciMethod*)%p)->_has_monitors", old_cim);
  _balanced_monitors  = FAM->getInt("((struct ciMethod*)%p)->_balanced_monitors", old_cim);
  _compilable         = FAM->getInt("((struct ciMethod*)%p)->_compilable", old_cim);

  _codeprofile        = CodeProfile::make(FAM->getOldPtr("((struct ciMethod*)%p)->_codeprofile", old_cim));

  _number_of_breakpoints  = FAM->getInt("((struct ciMethod*)%p)->_number_of_breakpoints", old_cim);
  _instructions_size      = FAM->getInt("((struct ciMethod*)%p)->_instructions_size", old_cim);

  _can_be_statically_bound= FAM->getInt("((struct ciMethod*)%p)->_can_be_statically_bound", old_cim);

  _has_c2_code            = FAM->getInt("((struct ciMethod*)%p)->_has_c2_code", old_cim);

  _has_loops              = FAM->getInt("((struct ciMethod*)%p)->_has_loops", old_cim);
  _has_jsrs               = FAM->getInt("((struct ciMethod*)%p)->_has_jsrs", old_cim);
  _is_accessor            = FAM->getInt("((struct ciMethod*)%p)->_is_accessor", old_cim);
  _is_initializer         = FAM->getInt("((struct ciMethod*)%p)->_is_initializer", old_cim);
  _objectId               = FAM->getInt("((struct ciMethod*)%p)->_objectId", old_cim);

  _has_unloaded_classes_in_sig_initialized = FAM->getInt("((struct ciMethod*)%p)->_has_unloaded_classes_in_sig_initialized", old_cim);
  _has_unloaded_classes_in_sig             = FAM->getInt("((struct ciMethod*)%p)->_has_unloaded_classes_in_sig", old_cim);

  int code_size           = FAM->getInt("((struct ciMethod*)%p)->_code_size", old_cim);
_code_size=code_size;
  FAMPtr old_code         = FAM->getOldPtr("((struct ciMethod*)%p)->_code", old_cim);
  if (old_code != 0 && old_code != 0xabababababababab && old_code != 0x5151515151515151) {
    // Lazy fields, filled in on demand.  Require allocation.
    _code = (address)CURRENT_ENV->arena()->Amalloc(_code_size);
for(int i=0;i<code_size;i++){
      _code[i] = (unsigned char)FAM->getInt("((struct ciMethod*)%p)->_code[%d]", old_cim, i);
    }
  } else {
    _code = NULL;
  }

  _handler_count          = FAM->getInt("((struct ciMethod*)%p)->_handler_count", old_cim);
  FAMPtr old_eh           = FAM->getOldPtr("((struct ciMethod*)%p)->_exception_handlers", old_cim);
  if (old_eh != 0 && old_eh != 0xabababababababab && old_eh != 0x5151515151515151) {
    _exception_handlers = (ciExceptionHandler**)CURRENT_ENV->arena()->Amalloc(sizeof(ciExceptionHandler*) * (_handler_count + 1));
for(int i=0;i<_handler_count+1;i++){
      _exception_handlers[i] = (ciExceptionHandler*)FAM->getOldPtr("((struct ciMethod*)%p)->_exception_handlers[%d]", old_cim, i);
    }
  } else {
    _exception_handlers = NULL;
  }

  // These fields are always filled in.
  _name         = (ciSymbol*)FAM->getOldPtr("((struct ciMethod*)%p)->_name", old_cim);
  _holder       = (ciInstanceKlass*)FAM->getOldPtr("((struct ciMethod*)%p)->_holder", old_cim);
  _signature    = (ciSignature*)FAM->getOldPtr("((struct ciMethod*)%p)->_signature", old_cim);
  _liveness = NULL;
_flow=NULL;
  FAMPtr old_bci2cpd_mapping = FAM->getOldPtr("((struct ciMethod*)%p)->_bci2cpd_mapping", old_cim);
  if (old_bci2cpd_mapping) {
    _bci2cpd_mapping = NEW_C_HEAP_OBJ(BCI2CPD_mapping);
    _bci2cpd_mapping->init(old_bci2cpd_mapping);
  } else {
_bci2cpd_mapping=NULL;
  }

  _monomorphic_lookup    = (Dict*)FAM->getOldPtr("((struct ciMethod*)%p)->_monomorphic_lookup", old_cim);
  _resolve_invoke_lookup = (Dict*)FAM->getOldPtr("((struct ciMethod*)%p)->_resolve_invoke_lookup", old_cim);
  _resolve_vtable_lookup = (Dict*)FAM->getOldPtr("((struct ciMethod*)%p)->_resolve_vtable_lookup", old_cim);
}

void ciMethod::fixupFAMPointers(){
  ciObject::fixupFAMPointers();
  FAMPtr old_cim = FAM->getOldFromNewPtr(this);

  FAMPtr old_name = (FAMPtr)_name;
  _name = (ciSymbol*)FAM->getNewFromOldPtr(old_name);
  FAM->mapNewToOldPtr(_name, old_name);

  FAMPtr old_holder = (FAMPtr)_holder;
  _holder = (ciInstanceKlass*)FAM->getNewFromOldPtr(old_holder);
  FAM->mapNewToOldPtr(_holder, old_holder);

  FAMPtr old_sig = (FAMPtr)_signature;
  _signature = new (CURRENT_ENV->arena()) ciSignature(old_sig);
  FAM->mapNewToOldPtr(_signature, old_sig);

if(_exception_handlers!=NULL){
for(int i=0;i<_handler_count+1;i++){
      FAMPtr old_eh = (FAMPtr)_exception_handlers[i];
      _exception_handlers[i] = new ciExceptionHandler(old_eh);
      FAM->mapNewToOldPtr(_exception_handlers[i], old_eh);
    }
  }

  // Go through dictionary and recover entries
  FAMPtr old_ml = (FAMPtr)_monomorphic_lookup;
  _monomorphic_lookup = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_ml); i.test(); ++i ) { 
    Triple* t = new (CURRENT_ENV->arena()) Triple();
    t->_a = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_a", i._key));
    t->_b = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_b", i._key));
    t->_c = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_c", i._key));
    _monomorphic_lookup->Insert(t, FAM->getNewFromOldPtr(i._value));
  }

  FAMPtr old_ril = (FAMPtr)_resolve_invoke_lookup;
  _resolve_invoke_lookup = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_ril); i.test(); ++i ) { 
    Triple* t = new (CURRENT_ENV->arena()) Triple();
    t->_a = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_a", i._key));
    t->_b = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_b", i._key));
    t->_c = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_c", i._key));
    _resolve_invoke_lookup->Insert(t, (void*)FAM->getNewFromOldPtr(i._value));
  }

  FAMPtr old_rvl = (FAMPtr)_resolve_vtable_lookup;
  _resolve_vtable_lookup = new (CURRENT_ENV->arena()) Dict(cmptriple, hashtriple, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_rvl); i.test(); ++i ) { 
    Triple* t = new (CURRENT_ENV->arena()) Triple();
    t->_a = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_a", i._key));
    t->_b = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_b", i._key));
    t->_c = FAM->getNewFromOldPtr(FAM->getOldPtr("((struct Triple*)%p)->_c", i._key));
    _resolve_vtable_lookup->Insert(t, (void*)FAM->getNewFromOldPtr(i._value));
  }
}

ciMethodExtras::ciMethodExtras() {
  Arena* arena = CURRENT_ENV->arena();
  _FAM_is_klass_loaded = new (arena) GrowableArray<bool>(arena, 4, 0, 0);
  _FAM_check_call      = new (arena) GrowableArray<bool>(arena, 4, 0, 0);
}

ciMethodExtras::ciMethodExtras(FAMPtr old_extras) {
  FAM->mapNewToOldPtr(this, old_extras);

  int len              = FAM->getInt("((struct ciMethodExtras*)%p)->_FAM_is_klass_loaded->_len", old_extras);
  _FAM_is_klass_loaded = new (CURRENT_ENV->arena()) GrowableArray<bool>(CURRENT_ENV->arena(), len, 0, 0);
for(int i=0;i<len;i++){
    bool loaded = FAM->getInt("((struct ciMethodExtras*)%p)->_FAM_is_klass_loaded->_data[%d]", old_extras, i);
    _FAM_is_klass_loaded->append(loaded);
  }

  int cc_len         = FAM->getInt("((struct ciMethodExtras*)%p)->_FAM_check_call->_len", old_extras);
  _FAM_check_call = new (CURRENT_ENV->arena()) GrowableArray<bool>(CURRENT_ENV->arena(), cc_len, 0, 0);
for(int i=0;i<cc_len;i++){
    bool res = FAM->getInt("((struct ciMethodExtras*)%p)->_FAM_check_call->_data[%d]", old_extras, i);
    _FAM_check_call->append(res);
  }
}

void ciMethod::check_is_loaded() const {
  if (FAM) return;

assert(is_loaded(),"not loaded");
}

// ------------------------------------------------------------------
// ciMethod::load_code
//
// Load the bytecodes and exception handler table for this method.
void ciMethod::load_code() {
  VM_ENTRY_MARK;
  assert(is_loaded(), "only loaded methods have code");

  methodOop me = get_methodOop();
  Arena* arena = CURRENT_THREAD_ENV->arena();

  // Load the bytecodes.
  _code = (address)arena->Amalloc(code_size());
  memcpy(_code, me->code_base(), code_size());

  // Revert any breakpoint bytecodes in ci's copy
if(me->number_of_breakpoints()>0){
    BreakpointInfo* bp = instanceKlass::cast(me->method_holder())->breakpoints();
    for (; bp != NULL; bp = bp->next()) {
      if (bp->match(me)) {
        code_at_put(bp->bci(), bp->orig_bytecode());
      }
    }
  }

  // And load the exception table.
  typeArrayOop exc_table = me->exception_table();

  // Allocate one extra spot in our list of exceptions.  This
  // last entry will be used to represent the possibility that
  // an exception escapes the method.  See ciExceptionHandlerStream
  // for details.
  _exception_handlers =
    (ciExceptionHandler**)arena->Amalloc(sizeof(ciExceptionHandler*)
                                         * (_handler_count + 1));
  if (_handler_count > 0) {
    for (int i=0; i<_handler_count; i++) {
      int base = i*4;
      _exception_handlers[i] = new (arena) ciExceptionHandler(
                                holder(),
            /* start    */      exc_table->int_at(base),
            /* limit    */      exc_table->int_at(base+1),
            /* goto pc  */      exc_table->int_at(base+2),
            /* cp index */      exc_table->int_at(base+3));
    }
  }

  // Put an entry at the end of our list to represent the possibility
  // of exceptional exit.
  _exception_handlers[_handler_count] =
new(arena)ciExceptionHandler(holder(),0,code_size(),InvocationEntryBci,0);

  if (CIPrintMethodCodes) {
print_codes(tty);
  }
}


// ------------------------------------------------------------------
// ciMethod::line_number_from_bci
int ciMethod::line_number_from_bci(int bci) const {
  check_is_loaded();
  VM_ENTRY_MARK;
  return get_methodOop()->line_number_from_bci(bci);
}


// ------------------------------------------------------------------
// ciMethod::vtable_index
//
// Get the position of this method's entry in the vtable, if any.
int ciMethod::vtable_index() {
  check_is_loaded();
  assert(holder()->is_linked(), "must be linked");
  VM_ENTRY_MARK;
  return get_methodOop()->vtable_index();
}


// ------------------------------------------------------------------
// ciMethod::native_entry
//
// Get the address of this method's native code, if any.
address ciMethod::native_entry() {
  check_is_loaded();
  assert(flags().is_native(), "must be native method");
  VM_ENTRY_MARK;
  methodOop method = get_methodOop();
  address entry = method->native_function();
  assert(entry != NULL, "must be valid entry point");
  return entry;
}


// ------------------------------------------------------------------
// ciMethod::interpreter_entry
//
// Get the entry point for running this method in the interpreter.
address ciMethod::interpreter_entry() {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
  return Interpreter::entry_for_method(mh);
}


// ------------------------------------------------------------------
// ciMethod::uses_balanced_monitors
//
// Does this method use monitors in a strict stack-disciplined manner?
bool ciMethod::has_balanced_monitors() {
  check_is_loaded();
  if (FAM) {
    return FAM->getInt("((struct ciMethod*)%p)->_balanced_monitors", FAM->getOldFromNewPtr(this));
  }
  if (_balanced_monitors) return true;

  // Analyze the method to see if monitors are used properly.
  VM_ENTRY_MARK;
  methodHandle method(THREAD, get_methodOop());
  assert(method->has_monitor_bytecodes(), "should have checked this");

  // Check to see if a previous compilation computed the
  // monitor-matching analysis.
  if (method->guaranteed_monitor_matching()) {
    _balanced_monitors = true;
    return true;
  }

  {
    EXCEPTION_MARK;
    ResourceMark rm(THREAD);
    GeneratePairingInfo gpi(method);
    gpi.compute_map(CATCH);
    if (!gpi.monitor_safe()) {
      return false;
    }
    method->set_guaranteed_monitor_matching();
    _balanced_monitors = true;
  }
  return true;
}

// ------------------------------------------------------------------
// ciMethod::get_flow_analysis
ciTypeFlow* ciMethod::get_flow_analysis() {
  if (_flow == NULL) {
    ciEnv* env = CURRENT_ENV;
    _flow = new (env->arena()) ciTypeFlow(env, this);
    _flow->do_flow();
  }
  return _flow;
}


// ------------------------------------------------------------------
// ciMethod::get_osr_flow_analysis
ciTypeFlow* ciMethod::get_osr_flow_analysis(int osr_bci) {
  // OSR entry points are always place after a call bytecode of some sort
  assert(osr_bci >= 0, "must supply valid OSR entry point");
  ciEnv* env = CURRENT_ENV;
  ciTypeFlow* flow = new (env->arena()) ciTypeFlow(env, this, osr_bci);
  flow->do_flow();
  return flow;
}

// ------------------------------------------------------------------
// ciMethod::liveness_at_bci
//
// Which local variables are live at a specific bci?
const MethodLivenessResult ciMethod::liveness_at_bci(int bci){
  check_is_loaded();
  if (_liveness == NULL) {
    // Create the liveness analyzer.
    Arena* arena = CURRENT_ENV->arena();
    _liveness = new (arena) MethodLiveness(arena, this);
    _liveness->compute_liveness();
  }
  MethodLivenessResult result = _liveness->get_liveness_at(bci);
if(JvmtiExport::can_access_local_variables()||CompileTheWorld){
    // Keep all locals live for the user's edification and amusement.
    result.at_put_range(0, result.size(), true);
  }
  return result;
}

// ------------------------------------------------------------------
// Add new receiver and sort data by receiver's profile count.
void ciCallProfile::add_receiver(ciKlass* receiver, int receiver_count) {
  // Add new receiver and sort data by receiver's counts when we have space
  // for it otherwise replace the less called receiver (less called receiver 
  // is placed to the last array element which is not used).
  // First array's element contains most called receiver.
  int i = _limit;
  for (; i > 0 && receiver_count > _receiver_count[i-1]; i--) {
    _receiver[i] = _receiver[i-1];
    _receiver_count[i] = _receiver_count[i-1];
  }
  _receiver[i] = receiver;
  _receiver_count[i] = receiver_count;
  if (_limit < MorphismLimit) _limit++;
}

// ------------------------------------------------------------------
// ciMethod::find_monomorphic_target
//
// Given a certain calling environment, find the monomorphic target
// for the call.  Return NULL if the call is not monomorphic in
// its calling environment, or if there are only abstract methods.
// The returned method is never abstract.
// Note: If caller uses a non-null result, it must inform dependencies
// via assert_unique_concrete_method or assert_leaf_type.
ciMethod* ciMethod::find_monomorphic_target(ciInstanceKlass* caller,
                                            ciInstanceKlass* callee_holder,
                                            ciInstanceKlass* actual_recv) {
  check_is_loaded();

  if (actual_recv->is_interface()) {
    // %%% We cannot trust interface types, yet.  See bug 6312651.
    return NULL;
  }

  ciMethod* root_m = resolve_invoke(caller, actual_recv);
  if (root_m == NULL) {
    // Something went wrong looking up the actual receiver method.
    return NULL;
  }
  assert(!root_m->is_abstract(), "resolve_invoke promise");

  // Make certain quick checks even if UseCHA is false.

  // Is it private or final?
  if (root_m->can_be_statically_bound()) {
    //NeedsRevisiting();
    // return root_m; // This causes a crash in the exception table construction.  Will need some work..
    return NULL;
  }

  // Array methods (clone, hashCode, etc.) are always statically bound.
  // If we were to see an array type here, we'd return root_m.
  // However, this method processes only ciInstanceKlasses.  (See 4962591.)
  // The inline_native_clone intrinsic narrows Object to T[] properly,
  // so there is no need to do the same job here.

  if (!UseCHA)  return NULL;

  VM_ENTRY_MARK;

if(actual_recv->is_leaf_type_query()&&actual_recv==root_m->holder()){
    // Easy case.  There is no other place to put a method, so don't bother
    // to go through the VM_ENTRY_MARK and all the rest.
    return root_m;
  }

  methodHandle target;
  {
MutexLockerAllowGC locker(Compile_lock,THREAD);
    klassOop context = actual_recv->get_klassOop();
    target = Dependencies::find_unique_concrete_method(context,
                                                       root_m->get_methodOop());
    // %%% Should upgrade this ciMethod API to look for 1 or 2 concrete methods.
  }

  if (target() == NULL) {
    return NULL;
  }
  if (target() == root_m->get_methodOop()) {
    return root_m;
  }
  if (!root_m->is_public() &&
      !root_m->is_protected()) {
    // If we are going to reason about inheritance, it's easiest
    // if the method in question is public, protected, or private.
    // If the answer is not root_m, it is conservatively correct
    // to return NULL, even if the CHA encountered irrelevant
    // methods in other packages.
    // %%% TO DO: Work out logic for package-private methods
    // with the same name but different vtable indexes.
    return NULL;
  }
  return CURRENT_THREAD_ENV->get_object(target())->as_method();
}

// ------------------------------------------------------------------
// ciMethod::resolve_invoke
//
// Given a known receiver klass, find the target for the call.  
// Return NULL if the call has no target or the target is abstract.
ciMethod* ciMethod::resolve_invoke(ciKlass* caller, ciKlass* exact_receiver) {
  Triple* key = new (CURRENT_ENV->arena()) Triple(caller, exact_receiver, NULL);
  ciMethod* lookup = (ciMethod*)(*_resolve_invoke_lookup)[key];
  if (lookup || FAM) return lookup;

  check_is_loaded();
  VM_ENTRY_MARK;

  KlassHandle caller_klass (THREAD, caller->get_klassOop());
  KlassHandle h_recv       (THREAD, exact_receiver->get_klassOop());
  KlassHandle h_resolved   (THREAD, holder()->get_klassOop());
  symbolHandle h_name      (THREAD, name()->get_symbolOop());
  symbolHandle h_signature (THREAD, signature()->get_symbolOop());

  methodHandle m;
  // Only do exact lookup if receiver klass has been linked.  Otherwise,
  // the vtable has not been setup, and the LinkResolver will fail.
if(h_recv->oop_is_javaArray()||
(instanceKlass::cast(h_recv())->is_linked()&&!exact_receiver->is_interface())){
    if (holder()->is_interface()) {
      m = LinkResolver::resolve_interface_call_or_null(h_recv, h_resolved, h_name, h_signature, caller_klass);
    } else {
      m = LinkResolver::resolve_virtual_call_or_null(h_recv, h_resolved, h_name, h_signature, caller_klass);
    }
  }

  if (m.is_null()) {
    // Return NULL only if there was a problem with lookup (uninitialized class, etc.)
    _resolve_invoke_lookup->Insert(key, NULL);
    return NULL;
  }

  ciMethod* result = this;
  if (m() != get_methodOop()) {
    result = CURRENT_THREAD_ENV->get_object(m())->as_method();
  }

  // Don't return abstract methods because they aren't
  // optimizable or interesting.
  if (result->is_abstract()) {
    _resolve_invoke_lookup->Insert(key, NULL);
    return NULL;
  } else {
    _resolve_invoke_lookup->Insert(key, result);
    return result;
  }
}

// ------------------------------------------------------------------
// ciMethod::resolve_vtable_index
//
// Given a known receiver klass, find the vtable index for the call.  
// Return methodOopDesc::invalid_vtable_index if the vtable_index is unknown.
int ciMethod::resolve_vtable_index(ciKlass* caller, ciKlass* receiver) {
  Triple* key = new (CURRENT_ENV->arena()) Triple(caller, receiver, NULL);
  int lookup = (int)(intptr_t)(*_resolve_vtable_lookup)[key];
  if (lookup || FAM) return lookup;

  check_is_loaded();

   int vtable_index = methodOopDesc::invalid_vtable_index;
   // Only do lookup if receiver klass has been linked.  Otherwise,
   // the vtable has not been setup, and the LinkResolver will fail.
   if (!receiver->is_interface()
       && (!receiver->is_instance_klass() ||
           receiver->as_instance_klass()->is_linked())) {
     VM_ENTRY_MARK;

     KlassHandle caller_klass (THREAD, caller->get_klassOop());
     KlassHandle h_recv       (THREAD, receiver->get_klassOop());
     symbolHandle h_name      (THREAD, name()->get_symbolOop());
     symbolHandle h_signature (THREAD, signature()->get_symbolOop());

     vtable_index = LinkResolver::resolve_virtual_vtable_index(h_recv, h_recv, h_name, h_signature, caller_klass);
     if (vtable_index == methodOopDesc::nonvirtual_vtable_index) {
       // A statically bound method.  Return "no such index".
       vtable_index = methodOopDesc::invalid_vtable_index;
     }
   }

  _resolve_vtable_lookup->Insert(key, (void*)vtable_index);
  return vtable_index;
}

// ------------------------------------------------------------------
// ciMethod::will_link
//
// Will this method link in a specific calling context?
bool ciMethod::will_link(ciKlass* accessing_klass,
                         ciKlass* declared_method_holder,
                         Bytecodes::Code bc) {
  if (!is_loaded()) {
    // Method lookup failed.
    return false;
  }

  // The link checks have been front-loaded into the get_method
  // call.  This method (ciMethod::will_link()) will be removed
  // in the future.

  return true;
}

// ------------------------------------------------------------------
// ciMethod::should_exclude
//
// Should this method be excluded from compilation?
bool ciMethod::should_exclude() {
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  {
    EXCEPTION_MARK;
    methodHandle mh(THREAD, get_methodOop());
    bool ignore;
    return CompilerOracle::should_exclude(mh, ignore);
  }
}

// ------------------------------------------------------------------
// ciMethod::should_inline
//
// Should this method be inlined during compilation?
bool ciMethod::should_inline() {
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
  return CompilerOracle::should_inline(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_disable_inlining
//
// Should the compiler disable inlining inside this method?
bool ciMethod::should_disable_inlining(){
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
return CompilerOracle::should_disable_inlining(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_print_assembly
//
// Should the compiler print the generated code for this method?
bool ciMethod::should_print_assembly() {
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
  return CompilerOracle::should_print(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_print_ir
//
// Should the compiler print the IR for this method?
bool ciMethod::should_print_ir(){
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
return CompilerOracle::should_print_ir(mh);
}

// ------------------------------------------------------------------
// ciMethod::break_at_execute
//
// Should the compiler insert a breakpoint into the generated code
// method?
bool ciMethod::break_at_execute() {
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
  return CompilerOracle::should_break_at(mh);
}

// ------------------------------------------------------------------
// ciMethod::has_option
//
bool ciMethod::has_option(const char* option) {
  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
  return CompilerOracle::has_option_string(mh, option);
}

// ------------------------------------------------------------------
// ciMethod::break_after_codegen_execute
//
// Should the compiler break after codgen?
bool ciMethod::break_c1_after_codegen(){
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
return CompilerOracle::should_break_c1_after_codegen(mh);
}

bool ciMethod::break_c2_after_codegen(){
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
return CompilerOracle::should_break_c2_after_codegen(mh);
}

// ------------------------------------------------------------------
// ciMethod::should_disable_loopopts
//
// Should the compiler disable loopopts for this method?
bool ciMethod::should_disable_loopopts(){
  if(FAM) return 0;

  check_is_loaded();
  VM_ENTRY_MARK;
  methodHandle mh(THREAD, get_methodOop());
return CompilerOracle::should_disable_loopopts(mh);
}

// ------------------------------------------------------------------
// ciMethod::can_be_compiled
//
// Have previous compilations of this method succeeded?
bool ciMethod::is_c1_compilable() const { return (_compilable&1)==0; }
bool ciMethod::is_c2_compilable() const { return (_compilable&2)==0; }
void ciMethod::set_not_compilable(int c12) {
  _compilable |= c12;
  VM_ENTRY_MARK;
  get_methodOop()->set_not_compilable(c12);
}

// ------------------------------------------------------------------
// ciMethod::has_c1_code
bool ciMethod::has_c2_code()const{
  return _has_c2_code;
}

BCI2CPD_mapping *ciMethod::bci2cpd_impl() {
  if (FAM) {
    return _bci2cpd_mapping;
  }
  VM_ENTRY_MARK;
  _bci2cpd_mapping = get_methodOop()->bci2cpd_map();
  return _bci2cpd_mapping;
}

CodeProfile *ciMethod::codeprofile(bool clone) {
  if( _codeprofile ) return clone ? _codeprofile->clone() : _codeprofile;
if(FAM)return NULL;
  VM_ENTRY_MARK;
  return (_codeprofile=get_methodOop()->codeprofile(clone));
}

int ciMethod::get_codeprofile_count(int kind) {
  if (!_codeprofile) return 0;
  switch (kind) {
  case CodeProfile::_throwout:
    return _codeprofile->throwout_count();
  case CodeProfile::_invoke:
    return _codeprofile->invoke_count();
  default:
    Unimplemented();
    return 0;
  }
}

CodeProfile *ciMethod::set_codeprofile( CodeProfile *cp ) {
  if (FAM) return _codeprofile;
  VM_ENTRY_MARK;
  _codeprofile = get_methodOop()->set_codeprofile(cp, NULL);
  return _codeprofile;
}

int ciMethod::number_of_breakpoints(){
  if (_number_of_breakpoints == -1) {
    VM_ENTRY_MARK;
    _number_of_breakpoints = get_methodOop()->number_of_breakpoints();
  }
  return _number_of_breakpoints;
}

// ------------------------------------------------------------------
// ciMethod::instructions_size
int ciMethod::instructions_size() {
  if( FAM || _instructions_size != -1 ) return _instructions_size;

  GUARDED_VM_ENTRY(
    methodCodeOop mco = get_methodOop()->codeRef().as_methodCodeOop();
    return mco ? mco->_blob->code_size() : 0;
  )
}

// ------------------------------------------------------------------
// ciMethod::is_not_reached
bool ciMethod::is_not_reached(int bci) {
  check_is_loaded();
  VM_ENTRY_MARK;
  return Interpreter::is_not_reached(
               methodHandle(THREAD, get_methodOop()), bci);
}

// ------------------------------------------------------------------
// ciMethod::was_never_executed
bool ciMethod::was_executed_more_than(int times) {
  VM_ENTRY_MARK;
  return get_methodOop()->was_executed_more_than(times);
}

// ------------------------------------------------------------------
// ciMethod::has_unloaded_classes_in_signature
bool ciMethod::has_unloaded_classes_in_signature() {
  if( _has_unloaded_classes_in_sig_initialized )
    return _has_unloaded_classes_in_sig;
  assert0(!FAM);
  VM_ENTRY_MARK;
  {
    EXCEPTION_MARK;
    methodHandle m(THREAD, get_methodOop());
_has_unloaded_classes_in_sig=methodOopDesc::has_unloaded_classes_in_signature(m,(JavaThread*)THREAD);
    if( HAS_PENDING_EXCEPTION ) {
      CLEAR_PENDING_EXCEPTION;
      _has_unloaded_classes_in_sig = true;
    }
    _has_unloaded_classes_in_sig_initialized = true;
  }
  return _has_unloaded_classes_in_sig;
}

// ------------------------------------------------------------------
// ciMethod::is_klass_loaded
bool ciMethod::is_klass_loaded(int refinfo_index)const{
  if (FAM) {
    return CURRENT_ENV->get_cim_extras(this)->_FAM_is_klass_loaded->at(refinfo_index);
  }
  VM_ENTRY_MARK;
bool loaded=get_methodOop()->is_klass_loaded(refinfo_index,true);
  CURRENT_ENV->get_cim_extras(this)->_FAM_is_klass_loaded->at_put_grow(refinfo_index, loaded);
  return loaded;
}

// ------------------------------------------------------------------
// ciMethod::check_call
bool ciMethod::check_call(int refinfo_index, bool is_static) const {
  if (FAM) {
    return CURRENT_ENV->get_cim_extras(this)->_FAM_check_call->at(refinfo_index);
  }
  VM_ENTRY_MARK;
  {
    EXCEPTION_MARK;
    HandleMark hm(THREAD);
    constantPoolHandle pool (THREAD, get_methodOop()->constants());
    methodHandle spec_method;
    KlassHandle  spec_klass;
    LinkResolver::resolve_method(spec_method, spec_klass, pool, refinfo_index, THREAD);
    bool ret;
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
      ret = false;
    } else {
ret=(spec_method->is_static()==is_static);
    }
    CURRENT_ENV->get_cim_extras(this)->_FAM_check_call->at_put_grow(refinfo_index, ret);
    return ret;
  }
  ShouldNotReachHere();
  return false;
}

#define FETCH_FLAG_FROM_VM(flag_accessor, type) { \
  check_is_loaded(); \
  if (FAM) { \
    return FAM->get##type("((struct ciMethod*)%p)->_##flag_accessor##", FAM->getOldFromNewPtr(this)); \
  } \
  if (_##flag_accessor) { \
    return _##flag_accessor; \
  } else { \
    VM_ENTRY_MARK; \
    _##flag_accessor##_initialized = 1; \
    _##flag_accessor = get_methodOop()->flag_accessor(); \
    return _##flag_accessor; \
  } \
}

bool ciMethod::is_empty_method()                { FETCH_FLAG_FROM_VM(is_empty_method, Int);                    }
bool ciMethod::is_vanilla_constructor()         { FETCH_FLAG_FROM_VM(is_vanilla_constructor, Int);             }
bool ciMethod::has_linenumber_table()           { FETCH_FLAG_FROM_VM(has_linenumber_table, Int);               }
u_char* ciMethod::compressed_linenumber_table() { FETCH_FLAG_FROM_VM(compressed_linenumber_table, UCharArray); }

ciMethodBlocks  *ciMethod::get_method_blocks() {
  Arena *arena = CURRENT_ENV->arena();
  if (_method_blocks == NULL) {
    _method_blocks = new (arena) ciMethodBlocks(arena, this);
  }
  return _method_blocks;
}

 
// ------------------------------------------------------------------
// ciMethod::print_codes
//
// Print the bytecodes for this method.
void ciMethod::print_codes(outputStream *out, CodeProfile *cp) {
  check_is_loaded();
  GUARDED_VM_ENTRY(get_methodOop()->print_codes(out, cp);)
}


// ------------------------------------------------------------------
// ciMethod::print_codes
//
// Print a range of the bytecodes for this method.
void ciMethod::print_codes(int from,int to,outputStream*out){
  check_is_loaded();
GUARDED_VM_ENTRY(get_methodOop()->print_codes(from,to,out);)
}

// ------------------------------------------------------------------
// ciMethod::print_name
//
// Print the name of this method, including signature and some flags.
void ciMethod::print_name(outputStream*st)const{
  if (FAM) return;
  check_is_loaded();
  GUARDED_VM_ENTRY(get_methodOop()->print_name(st);)
}

// ------------------------------------------------------------------
// ciMethod::print_short_name
//
// Print the name of this method, without signature.
void ciMethod::print_short_name(outputStream*st)const{
  if (FAM) return;
  check_is_loaded();
  GUARDED_VM_ENTRY(get_methodOop()->print_short_name(st);)
}

// ------------------------------------------------------------------
// ciMethod::print_impl
//
// Implementation of the print method.
void ciMethod::print_impl(outputStream*out)const{
ciObject::print_impl(out);
out->print(" name=");
name()->print_symbol(out);
out->print(" holder=");
holder()->print_name_on(out);
out->print(" signature=");
signature()->print_signature(out);
  if (is_loaded()) {
out->print(" loaded=true flags=");
flags().print_member_flags(out);
  } else {
out->print(" loaded=false");
  }
}

// ------------------------------------------------------------------
// ciMethod::bci_block_start
//
// Marks all bcis where a new basic block starts
const BitMap ciMethod::bci_block_start() {
  check_is_loaded();
  if (_liveness == NULL) {
    // Create the liveness analyzer.
    Arena* arena = CURRENT_ENV->arena();
    _liveness = new (arena) MethodLiveness(arena, this);
    _liveness->compute_liveness();
  }

  return _liveness->get_bci_block_start();
}
