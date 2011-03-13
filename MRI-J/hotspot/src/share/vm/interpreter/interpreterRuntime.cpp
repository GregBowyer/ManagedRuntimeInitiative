/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "arguments.hpp"
#include "bytecode.hpp"
#include "codeCache.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "compilationPolicy.hpp"
#include "deoptimization.hpp"
#include "disassembler_pd.hpp"
#include "frame.hpp"
#include "gcLocker.hpp"
#include "icache_pd.hpp"
#include "interfaceSupport.hpp"
#include "interp_masm_pd.hpp"
#include "interpreterRuntime.hpp"
#include "interpreter_pd.hpp"
#include "jfieldIDWorkaround.hpp"
#include "jvmtiExport.hpp"
#include "linkResolver.hpp"
#include "methodCodeKlass.hpp"
#include "methodCodeOop.hpp"
#include "nativeLookup.hpp"
#include "oopFactory.hpp"
#include "resourceArea.hpp"
#include "refsHierarchy_pd.hpp"
#include "sharedRuntime.hpp"
#include "signature.hpp"
#include "stubRoutines.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "vmTags.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "frame.inline.hpp"
#include "hashtable.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "oop.inline2.hpp"

//------------------------------------------------------------------------------------------------------------------------
// Constants


IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, ldc, (JavaThread* thread, bool wide))
  // access constant pool
  constantPoolOop pool = method(thread)->constants();
  int index = wide ? two_byte_index(thread) : one_byte_index(thread);
  constantTag tag = pool->tag_at(index);
  if (tag.is_unresolved_klass() || tag.is_klass()) {
klassOop klass=pool->klass_at(index,CHECK_(nullRef));
return klass->klass_part()->java_mirror_ref();
  } else {
#ifdef ASSERT
    // If we entered this runtime routine, we believed the tag contained
    // an unresolved string, an unresolved class or a resolved class. 
    // However, another thread could have resolved the unresolved string
    // or class by the time we go there.
    assert(tag.is_unresolved_string()|| tag.is_string(), "expected string");
#endif
oop s_oop=pool->string_at(index,CHECK_(nullRef));
return s_oop;
  }
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Allocation

static Bytecodes::Code bytecode( JavaThread *thread ) {
  return (Bytecodes::Code)*thread->last_frame().interpreter_frame_bcp();
}

// --- InterpreterRuntime::_new ----------------------------------------------
IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, _new, (JavaThread* thread, constantPoolRef pool, int index, jint length))
  if (length < 0) THROW_(vmSymbols::java_lang_NegativeArraySizeException(),nullRef);
  klassOop k_oop = pool.as_constantPoolOop()->klass_at(index, CHECK_(nullRef));
  instanceKlassHandle klass (THREAD, k_oop);

  // Make sure we are not instantiating an abstract klass
klass->check_valid_for_instantiation(true,CHECK_(nullRef));

  // Make sure klass is initialized
  klass->initialize(CHECK_(nullRef));

  return SharedRuntime::_new(thread, Klass::cast(klass())->klassId(), klass->size_helper()<<LogHeapWordSize, length,
                             UseSBA && bytecode(thread) != Bytecodes::_new_heap && !klass->has_finalizer());
IRT_END

// --- InterpreterRuntime::anewarray ----------------------------------------------
IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, anewarray, (JavaThread* thread, constantPoolRef pool, int index, jint length))
  if (length < 0) THROW_(vmSymbols::java_lang_NegativeArraySizeException(),nullRef);
  klassOop eklass = pool.as_constantPoolOop()->klass_at(index, CHECK_(nullRef));

Klass*eK=eklass->klass_part();
  int dim = eK->oop_is_array() ? ((arrayKlass*)eK)->dimension() : 0;
  klassRef ak = Klass::array_klass(klassRef(eklass), dim+1, CHECK_(nullRef)); // GC point, crushes eklass, eK
int size_in_words=objArrayOopDesc::object_size(length);

  return SharedRuntime::_new(thread, Klass::cast(ak.as_klassOop())->klassId(), size_in_words<<LogHeapWordSize, length,
                             UseSBA && bytecode(thread) != Bytecodes::_anewarray_heap);
IRT_END



// --- InterpreterRuntime::multianewarray ------------------------------------
IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, multianewarray, (JavaThread* thread, jint* first_size_address))
  // We may want to pass in more arguments - could make this slightly faster
  constantPoolOop constants = method(thread)->constants();
  int          i = two_byte_index(thread);
klassOop klass=constants->klass_at(i,CHECK_(nullRef));
  int   nof_dims = number_of_dimensions(thread);

  assert(oop(klass)->is_klass(), "not a class");
  assert(nof_dims >= 1, "multianewarray rank must be nonzero");

  // The AVM uses a tagged interpreter stack.  If we modify the arguments in place,
  // it can cause the stack scanning code to believe some of the dimension arguments
  // are oops.  We need to build the array of jints in another location.

  jint array_backing_store[64]; // Unless the array's are size 1, this should cover it.
  jint* array_dimensions = array_backing_store;
  if (nof_dims > 64) {
    array_dimensions = NEW_C_HEAP_ARRAY(jint, nof_dims);
guarantee(array_dimensions!=NULL,"C_HEAP allocation failed");
  }

  { // Elements are actually 64-bit wide.
for(int index=0;index<nof_dims;++index){
      array_dimensions[index] = first_size_address[ index*2 + 0];      
    }
  }

  // Note that because we've copied the array, we no longer pass in the
  // JES direction, but simply 1.
  oop obj = arrayKlass::cast(klass)->multi_allocate(nof_dims, array_dimensions, true/*SBA*/, CHECK_(nullRef));

  if (array_dimensions != array_backing_store) {
    // We need to free the c heap allocated backing store
    FREE_C_HEAP_ARRAY(jint, array_dimensions);
  }
  return obj;
IRT_END


IRT_ENTRY(void, InterpreterRuntime, register_finalizer, (JavaThread* thread, objectRef obj))
  assert(obj.as_oop()->is_oop(), "must be a valid oop");
  assert(obj.as_oop()->klass()->klass_part()->has_finalizer(), "shouldn't be here otherwise");
  instanceKlass::register_finalizer(instanceOop(obj.as_oop()), THREAD);
IRT_END


// Quicken instance-of and check-cast bytecodes
IRT_ENTRY_NO_GC_ON_EXIT(objectRef,InterpreterRuntime,quicken_io_cc,(JavaThread*thread))
  // Force resolving; quicken the bytecode
  int which = two_byte_index(thread);
  constantPoolOop cpool = method(thread)->constants();
  // We'd expect to assert that we're only here to quicken bytecodes, but in a multithreaded
  // program we might have seen an unquick'd bytecode in the interpreter but have another
  // thread quicken the bytecode before we get here.
  // assert( cpool->tag_at(which).is_unresolved_klass(), "should only come here to quicken bytecodes" );
klassOop klass=cpool->klass_at(which,CHECK_(nullRef));
return klass;
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Exceptions

// Assume the compiler is (or will be) interested in this event.
void InterpreterRuntime::note_trap(JavaThread*thread,int reason){
  methodOop moop = method(thread); // naked oop, no GC
  StripedMutexLocker ml(OsrList_locks,moop);
  address bcpx = bcp(thread);
  // Don't clone the cp since we already acquired the OsrList_lock
  CodeProfile *cp = moop->codeprofile(false); // Get a place to hang failed Heroic Optimization bits
  // Make a place it there's not already one
  // Due to possible race conditions, set cp to the value returned by 
  // set_codeprofile
  if( !cp ) cp = moop->set_codeprofile( CodeProfile::make(moop), NULL );
int bci=moop->bci_from(bcpx);
  int cpdoff = moop->bci2cpd_map()->bci_to_cpdoff( bci );
  if( cpdoff == BCI2CPD_mapping::unprofiled_bytecode_offset ) return; // Bytecode not tracked
  CPData *cpd = cp->cpdoff_to_cpd(0,cpdoff);
  if (!cpd->is_Null()) return; // We're only interested in CPData_Null's
  CPData_Null *cpdn = cpd->as_Null(Bytecode_at(bcpx)->java_code());
  switch( reason ) {
case Deoptimization::Reason_div0_check:
  case Deoptimization::Reason_null_check:
    cpdn->_null = 1;
    break;
case Deoptimization::Reason_cast_check:
    cpdn->_fail = 1;
    break;
  case Deoptimization::Reason_range_check:
    cpdn->_rchk = 1;
    break;
  default:
    ShouldNotReachHere();
  }  

}

// Called from abstract interpreter, generate_exception_handler
IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, create_exception, (JavaThread* thread, char* name, char* message))
  // lookup exception klass
symbolHandle s=oopFactory::new_symbol_handle(name,CHECK_(nullRef));
  if (ProfileTraps) {
    if (s == vmSymbols::java_lang_ArithmeticException()) {
note_trap(thread,Deoptimization::Reason_div0_check);
    } else if (s == vmSymbols::java_lang_NullPointerException()) {
note_trap(thread,Deoptimization::Reason_null_check);
    }
  }
  // create exception 
  Handle exception = Exceptions::new_exception(thread, s(), message);  
return exception();
IRT_END


// Called from abstract interpreter, generate_klass_exception_handler
// for ArrayStoreException and ClassCastException so we get a better stack dump.
IRT_ENTRY_NO_GC_ON_EXIT(objectRef, InterpreterRuntime, create_klass_exception, (JavaThread* thread, char* name, objectRef obj))
  ResourceMark rm(thread);
const char*klass_name=Klass::cast(obj.as_oop()->klass())->external_name();
  // lookup exception klass
symbolHandle s=oopFactory::new_symbol_handle(name,CHECK_(nullRef));
  if (ProfileTraps) {
note_trap(thread,Deoptimization::Reason_cast_check);
  }
  // create exception, with klass name as detail message
  Handle exception = Exceptions::new_exception(thread, s(), klass_name);
return exception();
IRT_END

IRT_ENTRY(void, InterpreterRuntime, throw_ArrayIndexOutOfBoundsException, (JavaThread* thread, char* name, jint index))
  char message[jintAsStringSize];
  // lookup exception klass
  symbolHandle s = oopFactory::new_symbol_handle(name, CHECK);
  if (ProfileTraps) {
note_trap(thread,Deoptimization::Reason_range_check);
  }
  // create exception 
  sprintf(message, "%d", index);
  THROW_MSG(s(), message);
IRT_END

IRT_ENTRY(void, InterpreterRuntime, throw_ClassCastException, (JavaThread* thread, objectRef obj))
  ResourceMark rm(thread);
  char* message = SharedRuntime::generate_class_cast_message(
thread,Klass::cast(obj.as_oop()->klass())->external_name());

  if (ProfileTraps) {
note_trap(thread,Deoptimization::Reason_cast_check);
  }

  // create exception 
  THROW_MSG(vmSymbols::java_lang_ClassCastException(), message);
IRT_END

// Given the current Java thread, a just-prior interpreter frame and an
// exception in pending_exception, compute the new handler address.  This call
// can do class loading of exception classes, it can GC, and it can modify the
// class of the exception in pending_exception (if lookup itself throws).
XRT_ENTRY_EX(address, InterpreterRuntime, find_exception_handler, (JavaThread* thread))
assert(HAS_PENDING_EXCEPTION,"better be one!");
  // Get the interpreter frame
  frame fr = last_frame(thread);
  if( fr.is_runtime_frame() ) fr = fr.sender(); // skip runtime stub for forward_exception

  // Flush the interpreter's stack before any possible GC
  fr.interpreter_empty_expression_stack();

#ifdef ASSERT
  // We do class loading here and a GC is possible.  Force it.
  InterfaceSupport::check_gc_alot();
#endif

  // Get method from interpreter frame
  methodHandle meth(fr.interpreter_frame_methodRef());
 
  // Still in stack overflow region?
  const bool force_unwind = !thread->stack_is_good();
  int handler_bci = force_unwind  ? InvocationEntryBci : 
    // Else do lookup for handler; Can class-load, can GC
    SharedRuntime::find_exception_handler_method_bci(thread, meth, fr.interpreter_frame_bci());

  // notify JVMTI of an exception throw; JVMTI will detect if this is a first 
  // time throw or a stack unwinding throw and accordingly notify the debugger
Handle h_exception(thread->pending_exception_ref());
  CLEAR_PENDING_EXCEPTION;

  if (JvmtiExport::can_post_exceptions()) {
    JvmtiExport::post_exception_throw(thread, meth(),  fr.interpreter_frame_bcp(), h_exception());
  }

  address continuation = NULL;
address handler_bcp=NULL;
if(handler_bci<0){//Unwinding stack
    // Forward exception to callee (leaving bci/bcp untouched) because (a) no
    // handler in this method, or (b) after a stack overflow there is not yet
    // enough stack space available to reprotect the stack.
    handler_bcp = meth->code_base();
    continuation = force_unwind 
      ? Interpreter::remove_activation_force_unwind_entry()
      : Interpreter::remove_activation_entry();
    meth->incr_codeprofile_count(CodeProfile::_throwout);
  } else {
    // handler in this method => change bci to handler bci and continue there
if(!meth->is_native()){
      fr.interpreter_frame_set_bci(handler_bci);
    }
continuation=Interpreter::continue_activation_entry();
  }


  // notify debugger of an exception catch 
  // (this is good for exceptions caught in native methods as well)
  if (JvmtiExport::can_post_exceptions()) {
    JvmtiExport::notice_unwind_due_to_exception(thread, meth(), handler_bcp, h_exception(), handler_bci != InvocationEntryBci);
  }

  thread->update_pending_exception(h_exception.as_ref());

  return continuation;
IRT_END


IRT_ENTRY(void, InterpreterRuntime, throw_AbstractMethodError, (JavaThread* thread))          
  THROW(vmSymbols::java_lang_AbstractMethodError());
IRT_END


IRT_ENTRY(void, InterpreterRuntime, throw_IncompatibleClassChangeError, (JavaThread* thread))          
  THROW(vmSymbols::java_lang_IncompatibleClassChangeError());
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Fields
//

IRT_ENTRY(void, InterpreterRuntime, resolve_get_put, (JavaThread* thread, Bytecodes::Code bytecode))
  // resolve field
  FieldAccessInfo info;
  constantPoolHandle pool(thread, method(thread)->constants());
  bool is_static = (bytecode == Bytecodes::_getstatic || bytecode == Bytecodes::_putstatic);

  {
    JvmtiHideSingleStepping jhss(thread);
    LinkResolver::resolve_field(info, pool, two_byte_index(thread),
                                bytecode, false, CHECK);
  } // end JvmtiHideSingleStepping

  // check if link resolution caused cpCache to be updated
  if (already_resolved(thread)) return;

  // compute auxiliary field attributes

  // We need to delay resolving put instructions on final fields
  // until we actually invoke one. This is required so we throw
  // exceptions at the correct place. If we do not resolve completely
  // in the current pass, leaving the put_code set to zero will
  // cause the next put instruction to reresolve.
  bool is_put = (bytecode == Bytecodes::_putfield ||
                 bytecode == Bytecodes::_putstatic);
  Bytecodes::Code put_code = (Bytecodes::Code)0;

  // We also need to delay resolving getstatic instructions until the
  // class is intitialized.  This is required so that access to the static
  // field will call the initialization function every time until the class
  // is completely initialized ala. in 2.17.5 in JVM Specification.
  instanceKlass *klass = instanceKlass::cast(info.klass()->as_klassOop());
  bool uninitialized_static = ((bytecode == Bytecodes::_getstatic || bytecode == Bytecodes::_putstatic) &&
                               !klass->is_initialized());
  Bytecodes::Code get_code = (Bytecodes::Code)0;


  if (!uninitialized_static) {
    get_code = ((is_static) ? Bytecodes::_getstatic : Bytecodes::_getfield);
    if (is_put || !info.access_flags().is_final()) {
      put_code = ((is_static) ? Bytecodes::_putstatic : Bytecodes::_putfield);
    }
  }

  cache_entry(thread)->set_field(
    get_code,
    put_code,
    info.klass(),
    info.field_index(),
    info.field_offset(),
info.field_type(),
    info.access_flags().is_final(),
    info.access_flags().is_volatile()
  );
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Synchronization
//
// The interpreter's synchronization code is factored out so that it can
// be shared by method invocation and synchronized blocks.
//%note synchronization_3

static void trace_locking(Handle& h_locking_obj, bool is_locking) {
  Unimplemented();
  //ObjectSynchronizer::trace_locking(h_locking_obj, false, true, is_locking);
}


IRT_ENTRY(void, InterpreterRuntime, throw_IllegalMonitorStateException, (JavaThread* thread))  
  THROW(vmSymbols::java_lang_IllegalMonitorStateException());
IRT_END


XRT_ENTRY_EX(void, InterpreterRuntime, new_IllegalMonitorStateException, (JavaThread* thread))
  // Installs an illegal exception into the current thread.  Any current
  // installed exception will be overwritten.  This method will be called
  // during an exception unwind.
  if( !HAS_PENDING_EXCEPTION ||
!PENDING_EXCEPTION->is_a(SystemDictionary::threaddeath_klass())){
    thread->clear_pending_exception();
    thread->set_pending_exception(Exceptions::new_exception(thread, vmSymbols::java_lang_IllegalMonitorStateException(), NULL)(), __FILE__, __LINE__);
  }
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Invokes

IRT_ENTRY(Bytecodes::Code, InterpreterRuntime, get_original_bytecode_at, (JavaThread* thread, methodRef method, int bci))
  return method.as_methodOop()->orig_bytecode_at(bci);
IRT_END

IRT_ENTRY(void, InterpreterRuntime, set_original_bytecode_at, (JavaThread* thread, methodRef method, int bci, Bytecodes::Code new_code))
  method.as_methodOop()->set_orig_bytecode_at(bci, new_code);
IRT_END

IRT_ENTRY(void, InterpreterRuntime, _breakpoint, (JavaThread* thread, methodRef method, int bci))
  JvmtiExport::post_raw_breakpoint(thread, method.as_methodOop(), method.as_methodOop()->bcp_from(bci));
IRT_END

IRT_ENTRY(void, InterpreterRuntime, resolve_invoke, (JavaThread* thread, Bytecodes::Code bytecode))  
  // extract receiver from the outgoing argument list if necessary
  Handle receiver(thread, NULL);  
  if (bytecode == Bytecodes::_invokevirtual || bytecode == Bytecodes::_invokeinterface) {
    ResourceMark rm(thread);
    methodHandle m (thread, method(thread));
int bci=get_bci(thread);
    Bytecode_invoke* call = Bytecode_invoke_at(m, bci);    
    symbolHandle signature (thread, call->signature());
    
    objectRef receiver_ref = thread->last_frame().interpreter_callee_receiver_ref(signature);
    oop ro = receiver_ref.as_oop();
receiver=Handle(thread,ro);
    assert(receiver.is_null() ||
           Universe::heap()->is_in_reserved(receiver->klass()),
           "sanity check");
  }  

  // resolve method
  CallInfo info;
  constantPoolHandle pool(thread, method(thread)->constants());

  {
    JvmtiHideSingleStepping jhss(thread);
    LinkResolver::resolve_invoke(info, receiver, pool, 
			         two_byte_index(thread), bytecode, CHECK);
    if (JvmtiExport::can_hotswap_or_post_breakpoint()) {
      int retry_count = 0;
      while (info.resolved_method()->is_old()) {
        // It is very unlikely that method is redefined more than 100 times
        // in the middle of resolve. If it is looping here more than 100 times 
        // means then there could be a bug here.
        guarantee((retry_count++ < 100),
                  "Could not resolve to latest version of redefined method");
        // method is redefined in the middle of resolve so re-try.
        LinkResolver::resolve_invoke(info, receiver, pool, 
			             two_byte_index(thread), bytecode, CHECK);
      }
    }
  } // end JvmtiHideSingleStepping

  // check if link resolution caused cpCache to be updated
  if (already_resolved(thread)) return;

  if (bytecode == Bytecodes::_invokeinterface) {    

    if (TraceItables && Verbose) {
      ResourceMark rm(thread);
      tty->print_cr("Resolving: klass: %s to method: %s", info.resolved_klass()->name()->as_C_string(), info.resolved_method()->name()->as_C_string());
    }
    if (info.resolved_method()->method_holder() ==
                                            SystemDictionary::object_klass()) {
      // NOTE: THIS IS A FIX FOR A CORNER CASE in the JVM spec
      // (see also cpCacheOop.cpp for details)
      methodHandle rm = info.resolved_method();
      assert(rm->is_final() || info.has_vtable_index(),
             "should have been set already");
      cache_entry(thread)->set_method(bytecode, rm, info.vtable_index()); 
    } else {          
      // Setup itable entry      
      int index = klassItable::compute_itable_index(info.resolved_method()());
      cache_entry(thread)->set_interface_call(info.resolved_method(), index);
    }
  } else {    
    cache_entry(thread)->set_method(
      bytecode,
      info.resolved_method(),
      info.vtable_index());     
  }
IRT_END


//------------------------------------------------------------------------------------------------------------------------
// Miscellaneous


#ifndef PRODUCT
static void trace_frequency_counter_overflow(methodHandle m,int bci,bool is_osr){
  if (TraceInvocationCounterOverflow) {
    InvocationCounter* ic = m->invocation_counter();
    InvocationCounter* bc = m->backedge_counter();
    ResourceMark rm;
    const char* msg = !is_osr
      ? "comp-policy cntr ovfl @ %d in entry of "
      : "comp-policy cntr ovfl @ %d in loop of ";
    tty->print(msg, bci);
    m->print_value();
    tty->cr();
    Unimplemented();
    //ic->print();
    //bc->print();
  }
}

static void trace_osr_request(methodHandle method, methodCodeOop osr, int bci) {
  if (TraceOnStackReplacement) {
    ResourceMark rm;
    tty->print(osr != NULL ? "Reused OSR entry for " : "Requesting OSR entry for ");
    method->print_short_name(tty);
    tty->print_cr(" at bci %d", bci);
  }    
}
#endif // !PRODUCT

IRT_ENTRY(address, InterpreterRuntime, frequency_counter_overflow, (JavaThread* thread, bool is_osr))
  frame fr = thread->last_frame();
  assert(fr.is_interpreted_frame(), "must come from interpreter");
  methodHandle method(thread, fr.interpreter_frame_method());  
  const int loop_bci = fr.interpreter_frame_bci();
  NOT_PRODUCT(trace_frequency_counter_overflow(method, loop_bci, is_osr);)

  if (JvmtiExport::can_post_interpreter_events()) {
    if (thread->is_interp_only_mode()) {
      // If certain JVMTI events (e.g. frame pop event) are requested then the
      // thread is forced to remain in interpreted code. This is
      // implemented partly by a check in the run_compiled_code
      // section of the interpreter whether we should skip running
      // compiled code, and partly by skipping OSR compiles for
      // interpreted-only threads.
      if (is_osr) {
        method->backedge_counter()->reset();
        return NULL;
      }
    }
  }

  if( !is_osr ) {
    if (!method->has_compiled_code() && (UseC1 || UseC2) ) {
      int c12;
      if (UseC1 && UseC2) {
        // Interpreter kicks C1 (or C2 if C1 can't compile this method)
        c12 = method->is_c1_compilable() ? 1 : 2;
      } else {
        c12 = UseC1 ? 1 : 2;  // Interpreter kicks C1
      }
      CompilationPolicy::method_invocation_event(method, c12, NULL);
    } else {
      // Force counter overflow on method entry, even if no compilation
      // happened.  (The method_invocation_event call does this also.)
      // For tiered, if we end up here, there was a race between the interpreter and C1
      // to promote the same method.  Let C1 win.
method->invocation_counter()->reset();
    }
    // Returned code is only for OSR's
    return NULL;

  } else {
    // counter overflow in a loop => try to do on-stack-replacement
    methodCodeOop osr_mco = method->lookup_osr_for(loop_bci);
    NOT_PRODUCT(trace_osr_request(method, osr_mco, loop_bci));
    if ((osr_mco == NULL) && (UseC1 || UseC2)) {
      CompilationPolicy::method_back_branch_event(method, loop_bci);
      // While kicking a C2 OSR, ALSO kick a normal C1 - in case we
      // re-enter this hot method again.
      if (UseC1 && UseC2) CompilationPolicy::method_invocation_event(method, 2, NULL);
      osr_mco = method->lookup_osr_for(loop_bci);
    }
if(osr_mco==NULL){
      method->backedge_counter()->reset();
      return NULL;
    }
    Untested();

    // cont. with on-stack-replacement code
    return osr_mco->_blob->code_begins();
  }
IRT_END


XRT_ENTRY_EX(void, InterpreterRuntime, at_safepoint, (JavaThread* thread))
  // We used to need an explict preserve_arguments here for invoke bytecodes.
  // However, stack traversal automatically takes care of preserving arguments
  // for invoke, so this is no longer needed.

  thread->poll_at_safepoint(); // Poll for safepoint conditions

  if (JvmtiExport::should_post_single_step()) {
    // We are called during regular safepoints and when the VM is
    // single stepping. If any thread is marked for single stepping,
    // then we may have JVMTI work to do.
    JvmtiExport::at_single_stepping_point(thread, method(thread), bcp(thread));
  }
IRT_END

IRT_ENTRY(void, InterpreterRuntime, post_field_access, (JavaThread *thread, objectRef obj, ConstantPoolCacheEntry *cp_entry))

  // check the access_flags for the field in the klass
  instanceKlass* ik = instanceKlass::cast((klassOop)cp_entry->f1());
  typeArrayOop fields = ik->fields();
  int index = cp_entry->field_index();
  assert(index < fields->length(), "holders field index is out of range");
  // bail out if field accesses are not watched
  if ((fields->ushort_at(index) & JVM_ACC_FIELD_ACCESS_WATCHED) == 0) return;

  switch(cp_entry->flag_state()) {
case T_BYTE://fall thru
case T_BOOLEAN://fall thru
case T_CHAR://fall thru
case T_SHORT://fall thru
case T_INT://fall thru
case T_FLOAT://fall thru
case T_LONG://fall thru
case T_DOUBLE://fall thru
case T_ARRAY://fall thru
    case T_OBJECT: break;
    default: ShouldNotReachHere(); return;
  }
bool is_static=(obj.is_null());
  HandleMark hm(thread);

  Handle h_obj;
  if (!is_static) {
    // non-static field accessors have an object, but we need a handle
h_obj=Handle(thread,obj.as_oop());
  }
  instanceKlassHandle h_cp_entry_f1(thread, (klassOop)cp_entry->f1());
  jfieldID fid = jfieldIDWorkaround::to_jfieldID(h_cp_entry_f1, cp_entry->f2(), is_static);
  JvmtiExport::post_field_access(thread, method(thread), bcp(thread), h_cp_entry_f1, h_obj, fid);
IRT_END

// The value in post_field_modification will not have the fields set
// correctly if we cast it directly as a jvalue because it is a 64-bit quantity.
// For e.g., when *jvalue contains 0x0000002a
// jvalue->i will be 0 instead of 0x2a.
// Use this routine to convert it to the correct values.
static jvalue convert_to_jvalue(uint64_t* val_ptr, char sig_type) {
jvalue value;
  value.j = 0;
  switch (sig_type) {
    case 'Z': 
    case 'B': value.b = (jbyte) *val_ptr; break;
    case 'C':
    case 'S': value.s = (jshort) *val_ptr; break;
    case 'F':
    case 'I': value.i = (jint) *val_ptr; break;
    case 'D':
    case 'J': value.j = (jlong) *val_ptr; break;
    case 'L': value.l = (jobject) *val_ptr; break;
    default: ShouldNotReachHere();
   }
  return value;
}


IRT_ENTRY(void, InterpreterRuntime, post_field_modification, (JavaThread *thread, objectRef obj, ConstantPoolCacheEntry *cp_entry, uint64_t* value_ptr))
  klassOop k = (klassOop)cp_entry->f1();

  // check the access_flags for the field in the klass
  instanceKlass* ik = instanceKlass::cast(k);
  typeArrayOop fields = ik->fields();
  int index = cp_entry->field_index();
  assert(index < fields->length(), "holders field index is out of range");
  // bail out if field modifications are not watched
  if ((fields->ushort_at(index) & JVM_ACC_FIELD_MODIFICATION_WATCHED) == 0) return;

  char sig_type = '\0';

  switch(cp_entry->flag_state()) {
case T_BYTE://fall through
case T_BOOLEAN:sig_type='Z';break;
case T_CHAR:sig_type='C';break;
case T_SHORT:sig_type='S';break;
case T_INT:sig_type='I';break;
case T_FLOAT:sig_type='F';break;
case T_LONG:sig_type='J';break;
case T_DOUBLE:sig_type='D';break;
case T_ARRAY://fall through
case T_OBJECT:sig_type='L';break;
    default:  ShouldNotReachHere(); return;
  }
bool is_static=(obj.is_null());

  HandleMark hm(thread);
  instanceKlassHandle h_klass(thread, k);
  jfieldID fid = jfieldIDWorkaround::to_jfieldID(h_klass, cp_entry->f2(), is_static);
  jvalue fvalue = convert_to_jvalue(value_ptr, sig_type);

  Handle h_obj;
  if (!is_static) {
    // non-static field accessors have an object, but we need a handle
h_obj=Handle(thread,obj.as_oop());
  } 

  JvmtiExport::post_raw_field_modification(thread, method(thread), bcp(thread), h_klass,
                                           h_obj, fid, sig_type, &fvalue);
IRT_END

IRT_LEAF(int, InterpreterRuntime, interpreter_contains, (address pc))
{
  return (Interpreter::contains(pc) ? 1 : 0);
}
IRT_END

// ---------------------------------------------------------------------------
// Implementation of AdapterHandlerLibrary.  Some quick & dirty stats: 1600
// searchs + 43 unique sigs to run Queens; 2700 searchs & 80 unique sigs to
// run 213_javac, 8000 searchs & 90 unique sigs to run Volano.  Signatures
// have a fairly flat bell-shaped distribution centered around 5 args.  Also
// about 1/2 the methods have a max_stack of 0.  Based on this I'm removing
// the old linear scan in favor of a split based on parameter count.  Instead
// of trying to get a fingerprint (which fails for large parm counts), I'm
// using method->size_of_parameters and method->max_stack()==0 as a hash into
// a linked list of buckets.

// Fixed-size hash table, based on parameter size and max_stack.
AdapterHandlerLibrary::AdapterHandler *AdapterHandlerLibrary::_adapters[11/*0-9 parms, and 10+ parms*/*2/*max_stack==0*/];

AdapterHandlerLibrary::AdapterHandler *AdapterHandlerLibrary::get_create_adapter(methodOop method) {

  int total_args_passed = method->size_of_parameters(); // All args on stack
symbolOop meth_sig=method->signature();
  No_Safepoint_Verifier ngcv(!Universe::heap()->is_gc_active()); // Verify no Safepoint happens, but it is OK if we are in a GC (class unloading).
ResetNoHandleMark rnm;//called from LEAF ENTRY
  HandleMark hm();

  // Compute Hash.  Very cheap!
  int hashidx = total_args_passed > 10 ? 10 : total_args_passed;
  if( method->max_stack() > 0 ) hashidx += 11;

  // Flatten the method signature, 1 char per arg.
  char flatsig[257];
char*s=flatsig;
  if (!method->is_static()) { // Dynamic?
    *s++ = 'L';	              // Insert an extra oop arg up front
  }
for(SignatureStream ss(meth_sig);!ss.at_return_type();ss.next()){
    assert(InterpreterRuntime::_native_flat_sig[ss.type()] != 'X', "passing non Java parameter type");
    *s++ = InterpreterRuntime::_native_flat_sig[ss.type()];
  }
  *s++ = 0;                   // Make it a string
  off_t flatlen = s - flatsig;  // Length of flat signature
  assert( flatlen < (off_t)sizeof(flatsig), "max size of parms grew?" );
    
  // Scan linked list of buckets for a hit.  Scan is multi-thread safe because
  // the insertion code is clever.
  AdapterHandler *ah = _adapters[hashidx];
  // Atomic::read_barrier(); // No need for a read-barrier, because the 2nd read (ah->_flatsig) is data-dependent on the 1st read (ah=_adapters[hashidx])
while(ah!=NULL){
    if (!strcmp(ah->_flatsig, flatsig)) 
      return ah;
    ah = ah->_next;
  }

  // Missed!  Need to compute & add an AdapterHandler.
  // Create I2C & C2I handlers
  ResourceMark rm;

  // Fill in the signature array, for the calling-convention call. 

  BasicType* sig_bt = NEW_RESOURCE_ARRAY(BasicType  ,total_args_passed);
  VReg::VR* regs = NEW_RESOURCE_ARRAY(VReg::VR,total_args_passed);
  int i=0;
  if( !method->is_static() ) {	// Pass in receiver first
    sig_bt[i++] = T_OBJECT;
  }
for(SignatureStream ss(meth_sig);!ss.at_return_type();ss.next()){
    sig_bt[i++] = ss.type();	// Collect remaining bits of signature
    if (ss.type() == T_LONG || ss.type() == T_DOUBLE) {
      sig_bt[i++] = T_VOID;	// Longs & doubles take 2 Java slots
    }
  }
  assert0(i==total_args_passed);

  // Now get the re-packed compiled-Java layout.
SharedRuntime::java_calling_convention(sig_bt,regs,total_args_passed,false);

  // Scan for max-stack-offset used by the compiled convention.  Used to see
  // if, due to padding and alignment, we need to grow the interpreter's stack.
  VReg::VR max_arg = VReg::VR(REG_COUNT-1);
for(int i=0;i<total_args_passed;i++)
    if (regs[i] > max_arg) 
      max_arg = regs[i];
  max_arg = VReg::VR((int)max_arg+1); // One past the max arg

  // A macro assembler to make code.
  InterpreterMacroAssembler masm( CodeBlob::c2i_adapter, "i2c2i adapter" );
  int c2i_offset = InterpreterRuntime::generate_i2c2i_adapters(&masm,total_args_passed,max_arg,sig_bt,regs);
  masm.bake_into_CodeBlob(8/*framesize has just the return arg*/);   // finallize oopmaps, etc

  // Invalide I-cache
  address i2c_handler = masm.blob()->code_begins();
  address c2i_handler = (address)masm.blob() + c2i_offset;
  ICache::invalidate_range((address)masm.blob(), masm.blob()->code_size());

  // Create AdapterHandler entry
  int ahsize = sizeof( AdapterHandler ) + flatlen;
  ah = (AdapterHandler*) AllocateHeap(ahsize, "AdapterHandler in " __FILE__);
  ah->_i2c = i2c_handler;
  ah->_c2i = c2i_handler;
  ah->_codesize = masm.blob()->code_size();
  ah->_flatsig = (char*)&ah[1];
  memcpy( (char*)ah->_flatsig, flatsig, flatlen );
#ifndef PRODUCT
  // debugging suppport
  if (PrintAdapterHandlers) {
    tty->cr();
    tty->print_cr("i2c argument handler (c2i starts at %p) for %s:", ah->_c2i, ah->_flatsig );
    Disassembler::decode(ah->_i2c, ah->_i2c + ah->_codesize);
  }
#endif

  // Add handlers to hash without disrupting readers.  Simple linked-list
  // insertion.  There's never any deletion - adapters are "leaked", except
  // that they actually might be used in the future so it's not really a leak
  // (and they are small and few in number).  Since no deletion, I can use a
  // simple linked-list algorithm.  Also I might end up with duplicates, but
  // this is OK - all adapters (with same sig) look alike so some dups simply
  // inflate the linked-list search a tad.
  AdapterHandler *head = _adapters[hashidx]; // Old list head
  while( true ) {
    ah->_next = head;           // Setup next pointer
    Atomic::write_barrier();    // Force out above write before CAS.
    if( Atomic::cmpxchg_ptr( ah, &_adapters[hashidx], head ) == head )
      return ah;                // Return newly inserted adapter!
    // CAS failed - something got inserted ahead of us.  99% chance it's
    // another instance of the same adapter - happens when a thread pool of
    // threads all jump into some new code at the same time, and each thread
    // wants a copy of the same adapter.  Just scan 1st list element instead
    // of the whole list.
    head = _adapters[hashidx];  // New "Old list head"
    if( !strcmp(head->_flatsig, flatsig) )
      break;                    // Found a dup at list head!
  }
  // We get here only if we found a dup adapter racily inserted ahead of us.
  // Our adapter is no longer needed.
  CodeCache::free_CodeBlob(masm.blob());
FreeHeap(ah);
  return head;                  // We just tested above that 'head' matches our sig
}


// Create a native wrapper for this native method.  The wrapper converts the
// java compiled calling convention to the native convention, handlizes
// arguments, and frees the jvm_lock.  On return from the native we CAS to
// retake the jvm_lock and may have to block if a GC is in progress.
methodCodeRef AdapterHandlerLibrary::create_native_wrapper(methodHandle method,TRAPS){
  // See if we already got one!  They get made racily, so might happen at any moment.
  methodCodeRef mcref = method->codeRef();
  if( mcref.not_null() ) return mcref;
  ResourceMark rm;

  // ---
  // Fill in the signature array, for the calling-convention call. 
  int total_args_passed = method->size_of_parameters();

  BasicType* sig_bt = NEW_RESOURCE_ARRAY(BasicType  ,total_args_passed);
  VReg::VR* regs = NEW_RESOURCE_ARRAY(VReg::VR,total_args_passed);
  int i=0;
  if( !method->is_static() )	// Pass in receiver first
    sig_bt[i++] = T_OBJECT;
  SignatureStream ss(method->signature());
  for( ; !ss.at_return_type(); ss.next()) {
    sig_bt[i++] = ss.type();	// Collect remaining bits of signature
    if( ss.type() == T_LONG || ss.type() == T_DOUBLE )
      sig_bt[i++] = T_VOID;	// Longs & doubles take 2 Java slots
  }
  assert0( i==total_args_passed);
  BasicType ret_type = ss.type();

  // Now get the compiled-Java layout.
SharedRuntime::java_calling_convention(sig_bt,regs,total_args_passed,false);
  // Scan for max-stack-offset used by the compiled convention.  Used to see
  // if, due to padding and alignment, we need to grow the interpreter's stack.
  VReg::VR max_arg = VReg::VR(REG_COUNT-1);
for(int i=0;i<total_args_passed;i++)
    if( regs[i] > max_arg ) 
      max_arg = regs[i];
  max_arg = VReg::VR((int)max_arg+1); // One past the max arg

  // ---
  // A macro-assembler to build the assembly.
  InterpreterMacroAssembler masm(CodeBlob::native, strdup(method()->name_as_C_string()));
  // And now go build the assembly.
  int framesize_bytes = InterpreterRuntime::generate_native_wrapper( &masm, method, total_args_passed,max_arg,sig_bt,regs, ret_type);
  // No more uses of this masm or blob, so bake in OopMap goodness.
  methodCodeRef mcr = masm.bake_into_codeOop(method.as_ref(), framesize_bytes,NULL,false/*has_unsafe*/,CHECK_((uint64_t)0));

  // Install the generated code.  Unlike the generic insert_methodCode version, 
  // this one stops if there is code already - we only need one copy of the
  // native wrapper.
  if( !method()->set_methodCode(mcr) )
    // We lost the install race.  Allow the existing MCR to be GC'd.
return method()->codeRef_list_head();
  // Now make the code active.
  method()->set_codeRef(mcr);
#ifndef PRODUCT
  // debugging suppport
  if (PrintAdapterHandlers) {
    tty->cr();
tty->print_cr("native wrapper for: %s %s, %d bytes generated)",
		  (method->is_static() ? "static" : "receiver"), method->signature()->as_C_string(), masm.rel_pc() );
    masm.decode(tty);
  }
#endif
  return mcr;
}

// Create a native wrapper (I2C + C2N) for this native nmethod.  
IRT_ENTRY(void, InterpreterRuntime, create_native_wrapper, (JavaThread* thread, methodRef method))
  methodHandle m(thread, method.as_methodOop());
assert(m->is_native(),"tried to execute non-native method as native");
assert(!m->is_abstract(),"tried to execute abstract method as non-abstract");
  // lookup native function entry point if it doesn't exist.  Lookup does
  // string object allocation, can call Java code and can GC.  It can exit
  // with a pending exception if the lookup failed.
  bool in_base_library;
  NativeLookup::lookup(m, in_base_library, CHECK);

  // This is a particularly tricky entry point because the interpreter has not
  // set up a new frame and must do some tricks to get the environment setup
  // correct to make this call.  Make it even more stressful by triggering a
  // safepoint with stress options.
  assert( StubRoutines::safepoint_trap_handler(), "vm init order" );

//static int foo = 30; // get past init
//  if( foo-- <= 0 ) {
//    tty->print_cr("checking gc alot in create_native_wrapper");
//    InterfaceSupport::check_gc_alot();
//  }
//
#ifndef PRODUCT
  bool forceSafepoint = (SafepointALot || ScavengeALot);
  if ( forceSafepoint ) {
    VMThread::execute(new VM_ForceSafepoint());
  }
#endif // PRODUCT

  // Make the native wrapper now.  Wrapper generation can cause GC.
  AdapterHandlerLibrary::create_native_wrapper(m, CHECK);

IRT_END
