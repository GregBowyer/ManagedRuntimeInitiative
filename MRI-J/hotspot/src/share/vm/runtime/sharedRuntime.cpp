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


#include "bytecode.hpp"
#include "bytecodeTracer.hpp"
#include "c1_Runtime1.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "compiledIC.hpp"
#include "deoptimization.hpp"
#include "gcLocker.hpp"
#include "handles.hpp"
#include "instructionTraceRecording.hpp"
#include "interfaceSupport.hpp"
#include "interpreterRuntime.hpp"
#include "interpreter_pd.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "linkResolver.hpp"
#include "methodOop.hpp"
#include "nativeInst_pd.hpp"
#include "objArrayKlass.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "sharedRuntime.hpp"
#include "signature.hpp"
#include "stubRoutines.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "threadService.hpp"
#include "vframe.hpp"
#include "vmTags.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "barrierSet.inline.hpp"
#include "bitMap.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "gcLocker.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "handles.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

#include "oop.inline2.hpp"

#include <math.h>

// Implementation of SharedRuntime

// For statistics
int SharedRuntime::_resolve_and_patch_call_ctr=0;
int SharedRuntime::_arraycopy_stats[100]; // Counts of arraycopy sizes

int SharedRuntime::_multi1_ctr=0;
int SharedRuntime::_multi2_ctr=0;
int SharedRuntime::_multi3_ctr=0;
int SharedRuntime::_multi4_ctr=0;
int SharedRuntime::_multi5_ctr=0;
int SharedRuntime::_az_mon_enter_stub_ctr=0;
int SharedRuntime::_az_mon_exit_stub_ctr=0;
int SharedRuntime::_az_mon_enter_ctr=0;
int SharedRuntime::_az_mon_exit_ctr=0;
int SharedRuntime::_jbyte_array_copy_ctr=0;
int SharedRuntime::_jshort_array_copy_ctr=0;
int SharedRuntime::_jint_array_copy_ctr=0;
int SharedRuntime::_jlong_array_copy_ctr=0;
int SharedRuntime::_oop_array_copy_ctr=0;
int SharedRuntime::_find_handler_ctr=0;          
int SharedRuntime::_rethrow_ctr=0;

JRT_LEAF(jlong, SharedRuntime, lmul, (jlong y, jlong x))
  return x * y;
JRT_END

static jlong mul_64_64_hi_check( jlong ab, jlong cd ) {
  jlong flip = ab^cd;
  uint64_t ab_pos = ab < 0 ? -ab : ab;
  uint64_t cd_pos = cd < 0 ? -cd : cd;
  uint64_t a = ab_pos >>32;
  uint64_t b = ab_pos & 0xffffffffL;
  uint64_t c = cd_pos >>32;
  uint64_t d = cd_pos & 0xffffffffL;
  
  uint64_t ac = a*c;
  uint64_t ad = a*d;
  uint64_t bc = b*c;
  uint64_t bd = b*d;
  
  uint64_t sum0 = bc+(bd  >>32);
  uint64_t sum1 = ad+ sum0;
  jlong res  = ac+(sum1>>32);
  if( flip < 0 ) {
    res = -res;
    if( (bd | (ad<<32) | (bc<<32)) != 0 )
      res--;
  }
  return res;
}

JRT_LEAF(jlong, SharedRuntime, ldiv, (jlong y, jlong x))
#ifdef AZ_X86
  if (x == min_jlong && y == CONST64(-1)) {
    return x;
  } else {
    return x / y;
  }
#else
  // The magic constants used in this routine were discovered with
  // Cliff Click's ~cliffc/src/bugs/ldiv.java routine, based on
  //   "Division by Invariant Integers using Multiplication"
  //     by Granlund and Montgomery
  // in the 1994 PLDI proceedings.
  if (0) { 
  } else if( y == 86400000 ) {  // The EXTREMELY common millis->days conversion
    jlong tmp1 = 0x636ba875fd33dc87L;
    jlong tmp2 = CAST_TO_FN_PTR(mul_64_64_hi_t,StubRoutines::mul_64_64_hi())(tmp1,x);
    //    assert0( tmp2 == mul_64_64_hi_check(tmp1,x));
    jlong tmp3 = tmp2>>25;
    jlong sign = x>>63;
    return tmp3 - sign;
   
  } else if( y == 100 ) {
    jlong tmp1 = 0xa3d70a3d70a3d70bL;
    jlong tmp2 = CAST_TO_FN_PTR(mul_64_64_hi_t,StubRoutines::mul_64_64_hi())(tmp1,x);
    //    assert0( tmp2 == mul_64_64_hi_check(tmp1,x));
    jlong tmp3 = tmp2+x;
    jlong tmp4 = tmp3>>6;
    jlong sign = x>>63;
    return tmp4 - sign;

  } else if( y == 10 ) {
    jlong tmp1 = 0x6666666666666667L;
    jlong tmp2 = CAST_TO_FN_PTR(mul_64_64_hi_t,StubRoutines::mul_64_64_hi())(tmp1,x);
    //    assert0( tmp2 == mul_64_64_hi_check(tmp1,x));
    jlong tmp3 = tmp2>>2;
    jlong sign = x>>63;
    return tmp3 - sign;
   
  } else if( y == 1000 ) {
    jlong tmp1 = 0x20c49ba5e353f7cfL;
    jlong tmp2 = CAST_TO_FN_PTR(mul_64_64_hi_t,StubRoutines::mul_64_64_hi())(tmp1,x);
    //    assert0( tmp2 == mul_64_64_hi_check(tmp1,x));
    jlong tmp3 = tmp2>>7;
    jlong sign = x>>63;
    return tmp3 - sign;
   
  } else if( y == 1 ) {
    return x;
  } else if( x == min_jlong && y == CONST64(-1) ) {
    return x;
  } else {
    return x / y;
  }
#endif
JRT_END


JRT_LEAF(jlong, SharedRuntime, lrem, (jlong y, jlong x))
#ifdef AZ_X86
  if (x == min_jlong && y == CONST64(-1)) {
    return 0;
  } else {
    return x % y;
  }
#else
  if (x == min_jlong && y == CONST64(-1)) {
    return 0;
  } else {
    return x - SharedRuntime::ldiv(y,x)*y;
  }
#endif
JRT_END


JRT_LEAF(jfloat, SharedRuntime, frem, (jfloat  x, jfloat  y))
  return ((jfloat)fmod((double)x,(double)y));
JRT_END


JRT_LEAF(jdouble, SharedRuntime, drem, (jdouble x, jdouble y))
  return ((jdouble)fmod((double)x,(double)y));
JRT_END


JRT_LEAF(jint, SharedRuntime, f2i, (jfloat  x))
  if (g_isnan(x)) {return 0;}
  jlong lltmp = (jlong)x;
  jint ltmp   = (jint)lltmp;
  if (ltmp == lltmp) {
    return ltmp;
  } else {
    if (x < 0) {
      return min_jint;
    } else {
      return max_jint;
    }
  }
JRT_END


JRT_LEAF(jlong, SharedRuntime, f2l, (jfloat  x))  
  if (g_isnan(x)) {return 0;}
  jlong lltmp = (jlong)x;
  if (lltmp != min_jlong) {
    return lltmp;
  } else {
    if (x < 0) {
      return min_jlong;
    } else {
      return max_jlong;
    }
  }
JRT_END


JRT_LEAF(jint, SharedRuntime, d2i, (jdouble x))
  if (g_isnan(x)) {return 0;}
  jlong lltmp = (jlong)x;
  jint ltmp   = (jint)lltmp;
  if (ltmp == lltmp) {
    return ltmp;
  } else {
    if (x < 0) {
      return min_jint;
    } else {
      return max_jint;
    }
  }
JRT_END


JRT_LEAF(jlong, SharedRuntime, d2l, (jdouble x))
  if (g_isnan(x)) {return 0;}
  jlong lltmp = (jlong)x;
  if (lltmp != min_jlong) {
    return lltmp;
  } else {
    if (x < 0) {
      return min_jlong;
    } else {
      return max_jlong;
    }
  }
JRT_END


JRT_LEAF(jfloat, SharedRuntime, d2f, (jdouble x))
  return (jfloat)x;
JRT_END


JRT_LEAF(jfloat, SharedRuntime, l2f, (jlong x))
  return (jfloat)x;
JRT_END


JRT_LEAF(jdouble, SharedRuntime, l2d, (jlong x))
  return (jdouble)x;
JRT_END

// --------------------------------------------------------------------------
// Find a handling BCI for the exception passed in thread->pending_exception()
// or return -1 if no handler in this method.  If there is an exception thrown
// during the lookup, the exception in pending_exception() can change.  Lookup
// can load classes and cause GC.
int SharedRuntime::find_exception_handler_method_bci(JavaThread *thread, methodHandle m, int bci) {
  while( true ) {		// Repeat if needed
HandleMark hm;//Reset handles with every iteration
    // Move the exception from pending_exception to a handle because the
    // following code will be returning exceptions in pending_exception.
Handle ex(thread->pending_exception_ref());
guarantee(ex.not_null(),"NULL exceptions should be handled by athrow");
    thread->clear_pending_exception();
    // Klass for lookup
    KlassHandle ek(ex()->klass_ref());

#ifdef ASSERT
    // Check that exception is a subclass of Throwable, 
    // otherwise we have a VerifyError
    if( !ex()->is_a(SystemDictionary::throwable_klass()) ) {
      if (ExitVMOnVerifyError) vm_exit(-1);
      ShouldNotReachHere();
    }
#endif
    // tracing
    if (TraceExceptions) {
      ResourceMark rm(thread);
      tty->print_cr("Exception <%s> (" PTR_FORMAT ")", ex()->print_value_string(), ex());
tty->print_cr(" thrown in method <%s>",m->print_value_string());
tty->print_cr(" at bci %d for thread "PTR_FORMAT,bci,thread);
    }

    // This next call can load exception classes and class loading can cause GC.
    int handler_bci = m->fast_exception_handler_bci_for(ek, bci, thread);
    assert( handler_bci == InvocationEntryBci || (handler_bci >= 0 && handler_bci < m->code_size()), 
"InvocationEntryBci is the only special flag value otherwise must be a valid bci");
    if( !thread->has_pending_exception() ) { // No problem with the lookup
      thread->update_pending_exception(ex.as_ref()); // Move exception back into pending
      return handler_bci;
    }
    // We threw an exception while trying to find the exception handler.
    // Transfer the new exception to the exception handle which will
    // be set into thread local storage, and do another lookup for an
    // exception handler for this exception, this time starting at the
    // BCI of the exception handler which caused the exception to be
    // thrown (bugs 4307310 and 4546590).  Continue looping to find the 
    // right handler.

    if (handler_bci == InvocationEntryBci) 	// If not catching it locally
      return handler_bci;	// then pending_exception is all set up
    bci = handler_bci;		// Else rethrow NEW exception at the handler bci
  } 
  ShouldNotReachHere();
  return -1;
}

// --------------------------------------------------------------------------
// Helper function for find_exception_handler_in_methodCode_pc.  Converts the
// methodCode & pc into a method & bci, and uses the CORE lookup.  After finding
// a handling bci, reverses that into a pc_offset in the methodCode.  This call
// can GC and change the thrown exception, if lookup itself causes an
// exception.  The exception is passed in and out via pending_exception.
static address compute_compiled_exc_handler(JavaThread *thread, methodCodeOop mcoop, address ret_pc) {
  // Convert the methodCode & pc into a method & bci
  int rel_pc = ret_pc - (address)mcoop->_blob;
  // Follow the tower of inlined scopes looking for a handler
  const DebugScope *ds0 = mcoop->_debuginfo->get(rel_pc);
  for( const DebugScope *ds = ds0; ds; ds = ds->caller() ) {
HandleMark hm;//Reset handles with every iteration
    // The following call can do exception class loading and GC
    int handler_bci = SharedRuntime::find_exception_handler_method_bci(thread, ds->method(), ds->bci());
    // Reverse the handler_bci into a pc_offset in the methodCode.
    int pco = ds->find_handler(handler_bci);
    if( pco != NO_MAPPING )     // Found a handler?
      return (address)(mcoop->_blob) + pco;
  }
  // Specific handler-lookup missed.  Now look for a generic handler.
  int pco = mcoop->_blob->is_c1_method() 
    ? mcoop->_generic_exception_handler_rel_pc
    : ds0->find_handler(InvocationEntryBci);
  if( pco == NO_MAPPING ) {
    // Still no handler?.  So deopt and allow the
    // interpreter to figure it out.
if(TraceDeoptimization)tty->print_cr("DEOPT no handler so deopting");
    Unimplemented();
    //    nm->deoptimize_now(Deoptimization::Deopt_install_async); // can GC
    //    return ret_pc;              // pretend handler is just the return
  }
  return (address)(mcoop->_blob) + pco;
}


//------------------------------find_exception_handler-------------------------
// Find the exception handler for compiled code, given the methodCode and a PC
// which points into it.  Pass the exception in and out via pending_exception.
// This method can GC.
XRT_ENTRY_EX(address, SharedRuntime, find_exception_handler_in_methodCode_pc, ( JavaThread *thread, methodCodeOop mcoop, address ret_pc ))
assert(HAS_PENDING_EXCEPTION,"better be one!");

address handler=NULL;
  // Check the stack guard pages.  If enabled, look for handler in this frame;
  // otherwise, forcibly unwind the frame.
bool force_unwind=!thread->stack_is_good();
  // If we are not forcibly unwinding the stack, we can first check methodCode's
  // exception cache.  If this succeeds, great.  If it fails, we'll do the
  // lookup the slow way and install what we find.
  if (force_unwind) {
    // Need to deopt the method so that the interpreter can properly unwind
    // this frame including its monitors.
    if (!mcoop->_blob->is_native_method()) {
      mcoop->deoptimize_now(Deoptimization::Reason_install_async); // can GC
    }
    // We know that we will want to continue at the return pc for proper deopt handling.
handler=ret_pc;
  } else {
    // Do slow lookup.
    handler = compute_compiled_exc_handler(thread, mcoop, ret_pc); // can GC
  }
  return handler;
JRT_END

// Exception handling accross interpreter/compiler boundaries
//
// exception_handler_for_return_address(...) returns the continuation address.
// The continuation address is the entry point of the exception handler of the
// previous frame depending on the return address.

address SharedRuntime::find_exception_handler_for_return_address(JavaThread *thread, address return_address) {
  assert(frame::verify_return_pc(return_address), "must be a return pc");
assert(thread->has_pending_exception(),"better have one!");
  
  thread->poll_at_safepoint();  // install stackoverflow if needed

#ifdef ASSERT
  // Code which pops a frame must first handle any escapes before coming here
  if( UseSBA && thread->pending_exception_ref().is_stack() )
    assert0( stackRef(thread->pending_exception_ref()).preheader()->fid() <= (int)thread->curr_sbafid() );
  if( (FullGCALot || ScavengeALot) && // We do class loading here and a GC is possible.  Force it.
      // However, we can throw a false-positive here as the outgoing
      // args for the failing call-stub are property of the routine
      // that got called and may have been used for scratch space.
      //!StubRoutines::returns_to_call_stub(return_address) &&
      // Must flush the interpreter's stack before GC'ing
      //!Interpreter::contains(return_address)
1
      ) {
    InterfaceSupport::check_gc_alot();
  }
#endif

  // Compiled code: hottest case first
  CodeBlob* blob = CodeCache::find_blob(return_address);
  assert0(blob != NULL);
assert(!blob->is_native_method(),"native methods handle their own pending exceptions");
if(blob->is_methodCode()){
    // Lookup the local handler in the method.  There *will* be one, even if
    // it's a generic "handle-all" type handler.  C2 handlers at least must
    // unlock locks and reload callee-save registers.  This function can do
    // class-loading of the exception types in the standard Java method
    // handler table.  Class loading can trigger a safepoint, so the methodCode
    // might deoptimize in the meantime.
    address handler = find_exception_handler_in_methodCode_pc( thread, blob->owner().as_methodCodeOop(), return_address ); // can GC

    // If the methodCode is deopt'd already, just normal return.  The deopt
    // path will unpack this frame into interpreter frames and start
    // throwing the exception there.  Otherwise, return to the proper local
    // exception handler.
    return blob->owner().as_methodCodeOop()->_patched_for_deopt ? return_address : handler;
  }
  guarantee(!blob->is_vtable_stub(), "NULL exceptions in vtables should have been handled already!");

  // Interpreted code
  if (Interpreter::contains(return_address)) {
    return InterpreterRuntime::find_exception_handler(thread);
  }
  // Entry code
  if (StubRoutines::returns_to_call_stub(return_address)) {
    // The pending_exception is already in the right place.  Simply return as
    // normal, and the JavaCalls code will check for the pending exception
    // after returning into the VM.
    return return_address;
  }
  if (blob->is_runtime_stub()) {
    Untested();
    return StubRoutines::forward_exception_entry(); // tear down the stub frame and try again
  }
#ifndef PRODUCT
  { ResourceMark rm;
tty->print_cr("No exception handler found for exception at "PTR_FORMAT" - potential problems:",return_address);
tty->print_cr("a) should have called exception_handler_for_address instead");
tty->print_cr("b) exception happened in (new?) code stubs/buffers that is not handled here");
tty->print_cr("c) some other problem");
  }
#endif // PRODUCT
  ShouldNotReachHere();
  return NULL;
}


oop SharedRuntime::retrieve_receiver( symbolHandle sig, frame caller ) {
  assert(caller.is_interpreted_frame(), "");
  int args_size = ArgumentSizeComputer(sig).size() + 1;
  oop result = (oop) *caller.interpreter_frame_tos_at(args_size - 1);
  assert(Universe::heap()->is_in(result) && result->is_oop(), "receiver must be an oop");
  return result;
}

void SharedRuntime::record_memory_traces(){
assert(UseITR,"Should not be here");

Thread*thread=(Thread*)Thread::current();

  uint64_t count = os::elapsed_counter(); 
  if ( count<ITRCollectionTimeStart                         || 
       count>=ITRCollectionTimeStart+ITRCollectionTimeLimit || 
       InstructionTraceThreads::doNotTrace->contains(thread)||
       (InstructionTraceThreads::doTrace && !InstructionTraceThreads::doTrace->contains(thread)) ) {
    // Outside recording range: 
    //  * Get a buffer if we dont already have one 
    //  * Wipe the array instead of getting new empty arrays
    while(thread->getInstructionTraceArray() == NULL) {
      thread->setInstructionTraceArray(InstructionTraceManager::getEmptyTrace());
    } 
thread->getInstructionTraceArray()->clear();
    thread->getInstructionTraceArray()->setAssociatedJavaThreadID(thread->unique_id());
    // const char* threadName = thread->get_thread_name_without_resource();
    // thread->_instructionTraceArray->setAssociatedJavaName(threadName);
    // delete[] threadName;
    thread->setCurrentTracePosition(thread->getInstructionTraceArray()->getTraces());
  } else {
    // Get new empty slate for recording
    InstructionTraceArray* ata = InstructionTraceManager::getEmptyTrace();
if(ata==NULL){
      if (thread->getInstructionTraceArray() == NULL) {
        // If a thread is created during tracing, and there are no empty slots, we're fried.
        // The trace will have to wait until an array becomes available.
        do {
          ata = InstructionTraceManager::getEmptyTrace();
          // Should probably take the ITR_lock and just wait here if ata is still NULL..
}while(ata==NULL);
        thread->setInstructionTraceArray(ata);
      } else {
        // Couldn't get a trace, but we already had one.  Keep ours
thread->getInstructionTraceArray()->clear();
      }
      thread->setCurrentTracePosition(thread->getInstructionTraceArray()->getTraces());
      return;
    }

    // If we were recording previously, save the results
if(thread->getInstructionTraceArray()!=NULL){
      InstructionTraceManager::addFullTrace(thread->getInstructionTraceArray());
    }

    ata->setAssociatedJavaThreadID(thread->unique_id());
    // const char* threadName = thread->get_thread_name_without_resource();
    // ata->setAssociatedJavaName(threadName);
    // delete[] threadName;

    thread->setInstructionTraceArray(ata);
    thread->setCurrentTracePosition(ata->getTraces());
  }
}

// Pass in and return thread, so the caller does not have to save/restore it.
JRT_ENTRY(JavaThread*, SharedRuntime, throw_NPE, (JavaThread *thread))
  // Allocate NPE, fill in stack crawl and set pending_exception
THROW_(vmSymbols::java_lang_NullPointerException(),thread);
JRT_END

// This LEAF call will compute a continuation address from the PC for
// a failing range-check instruction.  It does NOT allocate the
// exception object.  It does NOT compute the failing index.
int SharedRuntime::_throw_null_ctr=0;
int SharedRuntime::_throw_range_ctr=0;
JRT_LEAF(address, SharedRuntime, handle_array_index_check, (JavaThread* thread, address faulting_pc))
  _throw_range_ctr++;		// implicit range-check throw

  // Handle the interpreter first
if(Interpreter::contains(faulting_pc)){
return Interpreter::throw_ArrayIndexOutOfBoundsException_entry();
  }

  Unimplemented();
  return NULL;
JRT_END


// Special handling for stack overflow: since we don't have any (java) stack
// space left we use the pre-allocated & pre-initialized StackOverflowError
// klass to create an stack overflow error instance.  We do not call its
// constructor for the same reason (it is empty, anyway).
//
// SBA may eventually need a very similar function, build_FrameIdOverflowError().
//
JRT_ENTRY(void, SharedRuntime, build_StackOverflowError, (JavaThread* thread))
  // get klass
instanceKlass*soe_klass=instanceKlass::cast(SystemDictionary::StackOverflowError_klass());
  assert(soe_klass->is_initialized(), "VM initialization should set up StackOverflowError klass.");
  // alloc an instance - avoid constructor since execution stack is exhausted
  Handle ex = Handle(thread, soe_klass->allocate_instance(true/*SBA*/, thread));
  // if no other exception is pending, fill in trace for stack overflow...
  if( !thread->has_pending_exception() ) {
java_lang_Throwable::fill_in_stack_trace(ex);
    thread->set_pending_exception(ex(), __FILE__, __LINE__ );
  }
JRT_END


JNI_ENTRY(void, throw_unsatisfied_link_error, (JNIEnv* env, ...))
  THROW(vmSymbols::java_lang_UnsatisfiedLinkError());
JNI_END


address SharedRuntime::native_method_throw_unsatisfied_link_error_entry() {
  return CAST_FROM_FN_PTR(address, &throw_unsatisfied_link_error);
}

void SharedRuntime::throw_and_post_jvmti_exception(JavaThread *thread, Handle h_exception) {
  if (JvmtiExport::can_post_exceptions()) {
    methodOop m = NULL;
address bcp=NULL;
    {
      vframe vf(thread);
      m = vf.method();
      bcp = m->bcp_from(vf.bci());
    }

    JvmtiExport::post_exception_throw(thread, m, bcp, h_exception());
  }
  Exceptions::_throw(thread, __FILE__, __LINE__, h_exception);
}

void SharedRuntime::throw_and_post_jvmti_exception(JavaThread *thread, symbolOop name, const char *message) {
  Handle h_exception = Exceptions::new_exception(thread, name, message);
  throw_and_post_jvmti_exception(thread, h_exception);
}


#ifndef PRODUCT
JRT_ENTRY_NO_GC_ON_EXIT(intptr_t, SharedRuntime, trace_bytecode, (JavaThread* thread, intptr_t preserve_this_value, intptr_t tos, intptr_t tos2))
  const frame f = thread->last_frame();
  assert(f.is_interpreted_frame(), "must be an interpreted frame");
  BytecodeTracer::trace(f.interpreter_frame_method(), 
                        f.interpreter_frame_bcp(), tos, tos2);
  return preserve_this_value;
JRT_END
#endif // !PRODUCT

JRT_ENTRY(void, SharedRuntime, yield_all, (JavaThread* thread, int attempts))
  os::yield_all(attempts);
JRT_END

int SharedRuntime::_mon_wait_ctr=0;
JRT_ENTRY_NO_GC_ON_EXIT(void, SharedRuntime, wait_for_monitor, (JavaThread* thread, ObjectMonitor* monitor))
  _mon_wait_ctr++;              // monitor wait slow
  {
    // update JavaThread state -- blocked on monitor enter.
    JavaThreadBlockedOnMonitorEnterState jtbmes(thread, monitor);
Unimplemented();//almost surely this should be Synchronizer::wait()
monitor->wait();
  }
  // Any pending exception we find here must be asynchronous (the slow call
  // above cannot throw errors on its own).  Throwing a pending exception on
  // return from here is problematic: if we blocked trying to lock the 'this'
  // pointer and now want to throw, are we covered by handlers in this method
  // or not?  Do we need to unlock or not?  Instead, we "push back" a pending
  // asynchronous and poll for it later at a more convenient time.
  if( HAS_PENDING_EXCEPTION ) {
    // If we find an async exception pending already, it means some other
    // thread is gunning us down with async exceptions as fast as it can.
    // Keep the lastest async exception we see and blow this pending exception
    // off.  Otherwise convert this pending_exception into a pending async
    // exception and check it at the next convenient polling point.
    Handle e(PENDING_EXCEPTION);
    // async exception must be heap allocated
    StackBasedAllocation::ensure_in_heap(thread, e, "exception during wait");
    thread->install_async_exception(e.as_ref(),false/*overwrite*/, false/*self thread*/);
    CLEAR_PENDING_EXCEPTION;
  }
JRT_END

// Handles the uncommon case in locking, i.e., contention or an inflated lock.
JRT_ENTRY_NO_GC_ON_EXIT(objectRef, SharedRuntime, monitorenter, (JavaThread* thread, objectRef obj))
_az_mon_enter_ctr++;//monitor enter slow
  assert(Universe::is_in_allocation_area_or_null(obj.as_oop()), "must be NULL or an object");
  assert0( obj.is_heap() );     // No stack-refs on slow-path please.
  Handle h(thread,obj);         // Handlize across GC point
  obj.as_oop()->lock(INF_ENTER_ASM, thread);
  // Any pending exception we find here must be asynchronous (the slow call
  // above cannot throw errors on its own).  Throwing a pending exception on
  // return from here is problematic: if we blocked trying to lock the 'this'
  // pointer and now want to throw, are we covered by handlers in this method
  // or not?  Do we need to unlock or not?  Instead, we "push back" a pending
  // asynchronous and poll for it later at a more convenient time.
  if( HAS_PENDING_EXCEPTION ) {
    // If we find an async exception pending already, it means some other
    // thread is gunning us down with async exceptions as fast as it can.
    // Keep the lastest async exception we see and blow this pending exception
    // off.  Otherwise convert this pending_exception into a pending async
    // exception and check it at the next convenient polling point.
    Handle e(PENDING_EXCEPTION);
    // async exception must be heap allocated
    StackBasedAllocation::ensure_in_heap(thread, e, "exception during monitorenter");
    thread->install_async_exception(e.as_ref(),false/*overwrite*/, false/*self thread*/);
    CLEAR_PENDING_EXCEPTION;
  }
  return h();
JRT_END

// Handles the uncommon cases of monitor unlocking in compiled code
XRT_LEAF_EX(void, SharedRuntime, monitorexit, (oop obj))
_az_mon_exit_ctr++;//monitor exit slow
  { // Non-blocking so will not install any pending_exception, but can have pending exceptions coming in.
    No_Safepoint_Verifier nsv(true);
    // This call does not block, nor GC, nor throw exceptions.  We better be
    // balanced-locks here!  (asserted for!)  Notice that I call "oop->unlock"
    // and NOT "monitor->unlock" - because "monitor->unlock" is a direct call
    // to mutex::unlock and thus does not grok recursive locking.  oop->unlock
    // forwards to Synchronizer::unlock which DOES grok recursive locking.
obj->unlock();
  }
JRT_END

// If the caller to this routine was compiled then deoptimize it
static void deopt_compiled_caller(JavaThread*thread){
frame caller_frame=thread->last_frame();
  // unwind further if called by a stub
  if(Runtime1::contains(caller_frame.pc()) != -1) {
    caller_frame = caller_frame.sender();
  }
if(caller_frame.is_compiled_frame()){
CodeBlob*codeBlob=CodeCache::find_blob(caller_frame.pc());
assert(codeBlob!=NULL,"CodeBlob expected.");
    methodCodeOop mcOop = codeBlob->owner().as_methodCodeOop();
    if( mcOop ) {
      mcOop->deoptimize_now(Deoptimization::Reason_install_async); // can GC
    }
  }
}

static inline BasicType get_array_klass_type_of(klassRef klass) {
  Klass* kl = klass.as_klassOop()->klass_part();
  if (kl->oop_is_objArray())  return T_OBJECT;           
  if (kl->oop_is_typeArray()) return ((typeArrayKlass*)kl)->element_type();
  if (kl->oop_is_symbol())    return T_CHAR;
  return T_VOID;  // cannot handle this array?
}

// same as JVM_Arraycopy, but called directly from compiled code
// if an exception is thrown we deopt a compiled caller
JRT_ENTRY(void, SharedRuntime, slow_arraycopy_C, (JavaThread *thread, arrayRef srcref, int src_pos, arrayRef dstref, int dst_pos, int length))
  if( ProfileArrayCopy ) {
    BasicType type = get_array_klass_type_of(srcref.klass());
    SharedRuntime::collect_arraycopy_stats( length * type2aelembytes[type] );
  }
  // Check if we have null pointers
if(srcref.is_null()||dstref.is_null()){
    Exceptions::_throw_msg(THREAD_AND_LOCATION, vmSymbols::java_lang_NullPointerException(), NULL);
deopt_compiled_caller(thread);
  } else {
    // Do the copy.  The casts to arrayOop are necessary to the copy_array API,
    // even though the copy_array API also performs dynamic checks to ensure
    // that src and dest are truly arrays (and are conformable).
    // The copy_array mechanism is awkward and could be removed, but
    // the compilers don't call this function except as a last resort,
    // so it probably doesn't matter.
    Klass::cast(srcref.klass().as_klassOop())->copy_array(srcref.as_arrayOop(),  src_pos,
                                             dstref.as_arrayOop(), dst_pos,
                                             length, thread);
if(thread->has_pending_exception()){
deopt_compiled_caller(thread);
    }
  }
JRT_END

// ---------------------------------------------------------------------------------------------------------
// Non-product code
#ifndef PRODUCT

void SharedRuntime::verify_caller_frame(frame caller_frame, methodHandle callee_method) {
  ResourceMark rm;  
  assert (caller_frame.is_interpreted_frame(), "sanity check");
  assert (callee_method->has_compiled_code(), "callee must be compiled");  
methodHandle caller_method(caller_frame.interpreter_frame_methodRef());
  jint bci = caller_frame.interpreter_frame_bci();
  methodHandle method = find_callee_method_inside_interpreter(caller_frame, caller_method, bci);
  assert (callee_method == method, "incorrect method");
}

methodHandle SharedRuntime::find_callee_method_inside_interpreter(frame caller_frame, methodHandle caller_method, int bci) {
  EXCEPTION_MARK;
  Bytecode_invoke* bytecode = Bytecode_invoke_at(caller_method, bci);
  methodHandle staticCallee = bytecode->static_target(CATCH); // Non-product code
  
  bytecode = Bytecode_invoke_at(caller_method, bci);
  int bytecode_index = bytecode->index();
  Bytecodes::Code bc = bytecode->adjusted_invoke_code();      

  Handle receiver;
  if (bc == Bytecodes::_invokeinterface ||
      bc == Bytecodes::_invokevirtual ||
      bc == Bytecodes::_invokespecial) {
    symbolHandle signature (THREAD, staticCallee->signature());
    receiver = Handle(THREAD, retrieve_receiver(signature, caller_frame));
  } else {
    receiver = Handle();
  }
  CallInfo result;
  constantPoolHandle constants (THREAD, caller_method->constants());
  LinkResolver::resolve_invoke(result, receiver, constants, bytecode_index, bc, CATCH); // Non-product code
  methodHandle calleeMethod = result.selected_method();
  return calleeMethod;
}

#endif  // PRODUCT

JRT_ENTRY_NO_GC_ON_EXIT(objectRef, SharedRuntime, register_finalizer, (JavaThread* thread, objectRef obj))
  assert(obj.as_oop()->is_oop(), "must be a valid oop");
  assert(obj.as_oop()->klass()->klass_part()->has_finalizer(), "shouldn't be here otherwise");
  HandleMark hm;
  Handle hobj(obj);
  instanceKlass::register_finalizer(instanceOop(obj.as_oop()), THREAD);

  // The caller of this code does not check for pending exceptions, because C2
  // would rather deoptimize than deal with the really rare exception returns.
  // "new" throws OutOfMemory or asynchronous exceptions only.  Hence we need
  // to deopt the caller if an exception is pending.
if(thread->has_pending_exception()){
    Unimplemented();
  }
  return hobj();                   // Return obj for convenience.
JRT_END


// JVMTI wrappers 

JRT_ENTRY(void, SharedRuntime, jvmti_contended_monitor_enter, (JavaThread* thread, ObjectMonitor* mon))
{
    JvmtiExport::post_monitor_contended_enter(thread, mon);
}
JRT_END

JRT_ENTRY(void, SharedRuntime, jvmti_contended_monitor_entered, (JavaThread* thread, ObjectMonitor* mon))
{
    JvmtiExport::post_monitor_contended_entered(thread, mon);
}
JRT_END



char* SharedRuntime::generate_class_cast_message(
    JavaThread* thread, const char* objName) {

  // Get target class name from the checkcast instruction
  vframe vf(thread);
  Bytecode_checkcast* cc = Bytecode_checkcast_at(
vf.method()->bcp_from(vf.bci()));
Klass*targetKlass=Klass::cast(vf.method()->constants()->klass_at(
    cc->index(), thread));
  return generate_class_cast_message(objName, targetKlass->external_name());
}

char* SharedRuntime::generate_class_cast_message(
    const char* objName, const char* targetKlassName) {
  const char* desc = " cannot be cast to ";
  size_t msglen = strlen(objName) + strlen(desc) + strlen(targetKlassName) + 1;

char*message=NEW_C_HEAP_ARRAY(char,msglen);
  if (NULL == message) {
    // Shouldn't happen, but don't cause even more problems if it does
    message = const_cast<char*>(objName); 
  } else {
    jio_snprintf(message, msglen, "%s%s%s", objName, desc, targetKlassName);
  }
  return message;
}

// OSR Migration Code
//
// This code is used convert interpreter frames into compiled frames.  It is
// called from very start of a compiled OSR methodCode.  A temp array is
// allocated to hold the interesting bits of the interpreter frame.  All
// active locks are inflated to allow them to move.  The displaced headers and
// active interpeter locals are copied into the temp buffer.  Then we return
// back to the compiled code.  The compiled code then pops the current
// interpreter frame off the stack and pushes a new compiled frame.  Then it
// copies the interpreter locals and displaced headers where it wants.
// Finally it calls back to free the temp buffer.
//
// All of this is done NOT at any Safepoint, nor is any safepoint or GC allowed.

JRT_LEAF(intptr_t*, SharedRuntime, OSR_migration_begin, ( JavaThread *thread, int active_monitor_count ) ) {
  Unimplemented();
  return NULL;
//  ResourceMark rm;
//
//  frame lastfr = thread->last_frame();
//  // Force the appearance of being an interpreter frame, despite the fact that
//  // our PC is pointing back into the start of an OSR method.  Our incoming
//  // args are still laid out in interpreter form and we're doing the I2C stuff
//  // right now.
//  frame fr = lastfr;		// Copy
//  fr.set_pc(Interpreter::entry_for_kind(Interpreter::zerolocals));
//
//  assert( fr.is_interpreted_frame(), "" );
//  assert( fr.interpreter_frame_expression_stack_size()==0, "only handle empty stacks" );
//
//  methodOop moop = fr.interpreter_frame_method();
//  int max_locals = moop->max_locals();
//  // Allocate temp buffer, 1 word per local & active monitor
//  int buf_size_words = max_locals + active_monitor_count*2 + 2;
//  intptr_t *buf = NEW_C_HEAP_ARRAY(intptr_t,buf_size_words);
//
//  // Copy the locals.  Order is preserved so that loading of longs works.
//  // Since there's no GC I can copy the oops blindly.
//  assert( sizeof(HeapWord)==sizeof(intptr_t), "fix this code");
//  Copy::disjoint_words((HeapWord*)fr.interpreter_frame_local_at(max_locals-1),(HeapWord*)&buf[0],max_locals);
//
//  // Inflate locks.  Copy the displaced headers.  Be careful, there can be holes.
//  int i = max_locals;
//  for( BasicObjectLock *kptr = fr.interpreter_frame_monitor_end();
//       kptr < fr.interpreter_frame_monitor_begin();
//       kptr = fr.next_monitor_in_interpreter_frame(kptr) ) {
//    if( kptr->obj() ) {		// Avoid 'holes' in the monitor array
//      BasicLock *lock = kptr->lock();
//      if( !objectRef(kptr->obj()).is_stack() ) { // Stack oops do not need inflation
//        // Inflate so the displaced header becomes position-independent
//        ObjectSynchronizer::inflate(kptr->obj(), INF_OSR);
//      } else {
//        assert0( !kptr->obj()->mark()->has_monitor() );
//      }
//      // Now the displaced header is free to move
//      buf[i++] = (intptr_t)lock->displaced_header();
//      buf[i++] = objectRef(kptr->obj()).raw_value();
//    }
//  }
//  assert( i - max_locals == active_monitor_count*2, "found the expected number of monitors" );
//  // Compute the caller's original SP.  When we pop off this interpreted
//  // frame, we'll restore the caller's SP (which the interpreter adjusted to
//  // make space for his locals).  The compiled OSR frame we're about to push
//  // won't need to adjust the caller's SP, nor will it correct for an adjusted
//  // caller's SP on exit, so we need to correct caller's SP on entry to the
//  // OSR code.  The correction is actually done in the ASM that follows this
//  // call.  We pass this info along to help.
//  buf[i++] = (intptr_t)fr.sender_sp();
//  // Pass along the framesize as well
//  buf[i++] = CodeCache::find_blob(lastfr.pc())->frame_size_in_words();
//  return buf;
} 
JRT_END

JRT_LEAF(void, SharedRuntime, OSR_migration_end, (intptr_t* buf, int max_locals, int active_monitor_count, int total_monitor_count, intptr_t *fp ))
  Unimplemented();
//  // Copy the displaced headers down to the compact area just below the FP.
//  // This is C2 specific code, ugh.  Where does C1 keep the displaced headers?
//  int max = max_locals + active_monitor_count*2;
//  fp = (intptr_t*)((intptr_t)fp + STACK_BIAS);
//  fp += buf[max+1]; // Convert OSR SP into an OSR FP by adding framesize
//  fp -= (total_monitor_count - active_monitor_count); // skip inactive monitor slots (nothing to migrate)
//  for( int i=max_locals; i<max; i += 2 ) {
//    objectRef ref(buf[i+1]);
//    if( ref.is_stack() ) { // For stack-ref's only, we can move the displaced header
//      oop obj = ref.as_oop();
//      assert0( !obj->mark()->has_monitor() );
//      *((intptr_t**)obj) = fp;  // Hammer in a new ptr to displaced header
//    }
//    *(--fp) = buf[i];           // Lay down the displaced header
//  }
//  FREE_C_HEAP_ARRAY(intptr_t,buf);
JRT_END


// Resolve Calls
// Generalized resolve method for static, virtual and optimized virtual calls.
// The call-site has not been patched (nor likely resolved) or the targeted
// methodCode is not_entrant.  In either case we cannot use it.  There may 
// be a newer methodCode or we may be stuck going to the interpreter.  We 
// do not know if the caller was a static call or an inline cache call or a
// vtable call.  We could even be coming here from the interpreter, if the
// methodCode hit an uncommon trap in another thread between when the interpreter
// loaded the address and when it jumped.
JRT_ENTRY(address, SharedRuntime, resolve_and_patch_call, (JavaThread* thread, objectRef recv_or_firstarg))
  int patch_ctr = _resolve_and_patch_call_ctr++; // uninitialized call site
#ifndef PRODUCT
  if (TraceCallFixup)
tty->print("%d resolve_and_patch_call",_resolve_and_patch_call_ctr);
#endif
ResourceMark rm(thread);

  // Get caller.
  frame caller_frame = thread->last_frame().sender();

  // See if we got called from an entry frame.
  if( caller_frame.is_entry_frame() ) {
    // We got here directly from a JavaCall.
    // caller_frame is now pointing to the entry frame.    
    methodOop moop = caller_frame.entry_frame_call_wrapper()->callee_method();
    assert(caller_frame.entry_frame_call_wrapper()->receiver() == NULL || !moop->is_static(), 
"non-null receiver for static call??");
    // Since we got here from a JavaCall, there is no code to patch.
    // However the JavaCall properly resolved the target.
    // Return address to jump to.
#ifndef PRODUCT
    if (TraceCallFixup) {
      tty->print(", resolved from entry frame to " /* , patch_ctr */);
      moop->print_short_name(tty);
      tty->cr();
    }
#endif
    return moop->from_compiled_entry();
  }

  // VVVVVVVVVVVVVVVVVV  No Safepoint  VVVVVVVVVVVVVVVVVVVV
  // I need a non-block-structured No_Safepoint_Verifier
  CollectedHeap* h = Universe::heap();
  assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
  uint old_invocations = h->total_collections();
#ifdef DEBUG
  thread->_allow_allocation_count++;
  thread->_allow_safepoint_count++;
#endif

  // Resolve the call-site (without holding the CompiledIC_lock).
  // Finds receiver, CallInfo (i.e. receiver method), and calling bytecode
  // for a call currently in progress.  Arguments have been pushed onto stack 
  // but callee has not yet been invoked.  
  // Note that caller frame must be compiled.
  methodCodeHandle mco;
methodHandle caller_method;
  int bci;
address caller_pc;
  const DebugScope *dbg=0;  

  // Mostly I get here from compiled-callers, but there is a narrow race
  // condition between when I stamp down the methodCode-not-entrant jump/trap and
  // when I update the method->_from_interpreted pointers.  It is possible to
  // get here from an interpreted caller.
if(caller_frame.is_interpreted_frame()){
    caller_method = methodHandle( caller_frame.interpreter_frame_methodRef() );
    bci = caller_frame.interpreter_frame_bci();
    caller_pc = (address)0xdeadbeef; // Interpreter was caller, nothing to patch!
  } else {
    caller_pc = caller_frame.pc();
CodeBlob*cb=CodeCache::find_blob(caller_pc);
assert(cb!=NULL,"can't find blob");
    mco = cb->owner();
assert(mco!=NULL,"must be called from methodCode");

    // Find caller and bci from debug info.  Note that the caller is the
    // inlined caller and likely not the top-level methodCode->method().
    // methodCode's have a lot of inlining and here we need to resolve
    // against the original java bytecodes.
    dbg = mco->_debuginfo->get(caller_pc-(address)cb);
    assert((intptr_t)dbg != -1, "Missing debug info");
caller_method=methodHandle(dbg->method());
bci=dbg->bci();
  }
  
#ifndef PRODUCT
  if (TraceCallFixup) {
    tty->print(" from %s frame, ",caller_frame.is_interpreted_frame() ? "interpreted" : "compiled");
caller_method()->print_short_name(tty);
tty->print_cr(" @ bci %d",bci);
  }
#endif

  // Find bytecode
  Bytecode_invoke* bytecode = Bytecode_invoke_at(caller_method, bci);
  Bytecodes::Code bc = bytecode->adjusted_invoke_code();
  int bytecode_index = bytecode->index();

  // Verify receiver
#ifndef AZ_X86
assert(bc==Bytecodes::_invokestatic||
          // For non-static calls, receiver is null-checked before
          // we ever get here.
          (recv_or_firstarg.not_null() && recv_or_firstarg.as_oop()->is_oop()), "Broken receiver" );
#else
#ifdef ASSERT
  if ( bc != Bytecodes::_invokestatic &&
          // For AZUL x86-64 we can receive a dispatch on a null receiver
          !recv_or_firstarg.is_null() && !recv_or_firstarg.as_oop()->is_oop() ) {
    fatal1("Broken receiver - " INTPTR_FORMAT, recv_or_firstarg.raw_value());
  }
#endif
#endif
  // Preserve receiver across LinkResolver call, now that I can tell
  // it is receiver and not some random junk arg for a static call.
  // This is the reason I did not pass in a receiver handle in the
  // first place; for handle_wrong_methodCode I do not even know if I
  // have a static call until I get the bc.
  Handle receiver(thread, bc == Bytecodes::_invokestatic ? NULL : recv_or_firstarg.as_oop());

  // Now verify no-gc, then do the link-resolver (which may GC)
  assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
  if (old_invocations != h->total_collections())
    fatal("collection in a No_GC_Verifier secured function");
#ifdef DEBUG
  thread->_allow_allocation_count--;
  thread->_allow_safepoint_count--;
#endif
  // ^^^^^^^^^^^^^^^^^^  End No Safepoint  ^^^^^^^^^^^^^^^^^^^^

#ifdef AZ_X86
  // AZUL x86-64 inline cache does a kid comparison and on failure calls here
  // The kid comparison fails if the receiver is null, in which case we
  // generate an NPE.
  if ( (bc != Bytecodes::_invokestatic) && recv_or_firstarg.is_null() ) {
THROW_0(vmSymbols::java_lang_NullPointerException());
  }
#endif

  // Check for NoSuchMethod.  Is this needed?
  methodHandle mh = bytecode->static_target(CHECK_0);
if(mh.is_null()){
    THROW_0(vmSymbols::java_lang_NoSuchMethodException());
  }

  debug_only(InterfaceSupport::check_gc_alot());

  // Resolve method.
  // May block, GC, deopt, and/or load classes.
  constantPoolHandle constants (THREAD, caller_method->constants());
  CallInfo callinfo;
  LinkResolver::resolve_invoke(callinfo, receiver, constants, bytecode_index, bc, CHECK_0);

#ifndef PRODUCT
    if (TraceCallFixup) {
      tty->print("%d resolved to ",patch_ctr+1);
callinfo.selected_method()->print_short_name(tty);
      tty->cr();
    }
#endif

  // Make sure natives have their wrappers ready.
  callinfo.selected_method()->make_native_wrapper_if_needed(CHECK_0);
      
#ifdef ASSERT
  // Check that the receiver klass is of the right subtype and that it is initialized for virtual calls
  if (bc != Bytecodes::_invokestatic) {
    assert(receiver.not_null(), "should have thrown exception");
    KlassHandle receiver_klass (THREAD, receiver->klass());
klassOop rk=constants->klass_ref_at(bytecode_index,CHECK_0);
                            // klass is already loaded
    KlassHandle static_receiver_klass (THREAD, rk);
    assert(receiver_klass->is_subtype_of(static_receiver_klass()), "actual receiver must be subclass of static receiver klass");
    if (receiver_klass->oop_is_instance()) {
      if (instanceKlass::cast(receiver_klass())->is_not_initialized()) {
        tty->print_cr("ERROR: Klass not yet initialized!!");
        receiver_klass.print();
      }
      assert (!instanceKlass::cast(receiver_klass())->is_not_initialized(), "receiver_klass must be initialized");
    }
  }
#endif

  // The callee might also have de-opt'd.  In any case, "from_compiled_code"
  // will contain the proper target address.
  address code = callinfo.selected_method()->from_compiled_entry();
assert(code,"must have code");

  // If the caller is interpreted, there is no code to patch.
  if( caller_frame.is_interpreted_frame() ) return code;
  assert0( caller_frame.is_compiled_frame() );

  // If the caller de-opt'd while we where resolving, then there's nothing to patch.
  if( mco->_patched_for_deopt ) return code;

  No_Safepoint_Verifier nsv();	// Verify no safepoint across lock

  // In order to patch the caller, we need to know if we are patching an
  // inline-cache or just a plain call site.  Unfortunately, code-inspection
  // cannot reliably tell them apart, we need to use the debug info.  Also
  // if multiple threads start patching we might have a site get 1/2 patched
  // from clean to e.g. caching-flavor-1, and also to caching-flavor-2.
  // Lock around the computation of patching flavor.
  StripedMutexLocker ml_patch(CompiledIC_locks,caller_pc);
  if( dbg->is_inline_cache() ) {// Patch an inline-cache
    // Find the inline-cache implementing the virtual or interface Java call.  
    CompiledIC *ic = CompiledIC::make_before(caller_pc);
    // Patch it
    ic->patch_as_needed(&callinfo,receiver()->klass(),bc);
  } else {			// Patch a native call
    // Find the native call implementing the static or opt-virtual Java call.
    // This will fail to recognize inline-caches.  
NativeCall*ncall=nativeCall_before(caller_pc);
    ncall->set_destination_mt_safe(code);
    mco->patched_a_call_to(caller_pc,code);
  }
  // Return address to continue execution at
  return code;

JRT_END


XRT_LEAF_EX(bool, SharedRuntime, verify_oop, (objectRef obj))
#if !defined(PRODUCT)
  // Leaf cannot lock, gc, or throw exceptions
  // Inline ref_as_oop() so we return false instead of asserting for
  // junk (i.e. 0xdeadbeef) objects.
  intptr_t bits = obj.raw_value();
  if( (bits & 7) != 0 ) return false;
  // Null is ok
  if( bits == 0 ) return true;

  // Inline ref_as_oop() for speed, since we already done must of
  // it.  Note that null is not possible since we already checked
  // for that in asm code.
#define va_mask    0x1ffffffffffL
#define gpgc_va_mask    0x3ffffffffffL //42 bits of VA for GPGC 

  oop o = UseGenPauselessGC ? (oop)(bits & gpgc_va_mask) : (oop)(bits & va_mask);
if(o==NULL)return false;
  if ( o != Universe::non_oop_word()) {
    if( UseGenPauselessGC ) {
      if( !GPGC_NMT::has_desired_nmt(obj) ) return false;
      if( GPGC_ReadTrapArray::is_any_trapped(obj, NULL) ) return false;
    }
    unsigned int kid = obj.klass_id();
    if( kid == 0 ) return false; // Never a valid kid
    if (kid > max_reserved_kid) {
      if( !KlassTable::is_valid_klassId(kid) ) return false;
      if( o->mark()->kid() != (int)kid )       return false;
      if( VerifyOopLevel > 4 ) {
      klassOop k1, k2;
      k1 = KlassTable::getKlassByKlassId(kid).as_klassOop();
k2=o->klass();
        if (k1 != k2)  return false;
      } 
    }
  }
  if( VerifyOopLevel > 5 ) ((oop)obj.as_address())->verify();
  return VerifyOopLevel <= 4 ? true : ((oop)obj.as_address())->is_oop();
#else // !defined(PRODUCT)
NEEDS_CLEANUP;//should at least do minimal validity check (eg. alignment & in heap)
  return true;
#endif // !defined(PRODUCT)
JRT_END


// --- SharedRuntime::verify_ref_klass
// Verify that an object has a KID with a klassRef that has the expected space and NMT.
XRT_LEAF_EX(void, SharedRuntime, verify_ref_klass, (objectRef obj, int verify_id))
  uint32_t  kid      = obj.klass_id();
  klassRef* addr     = KlassTable::getKlassAddrByKlassId(kid);
  klassRef  klass    = UNPOISON_KLASSREF(*addr);
  uint32_t  nmt      = klass.nmt();
  uint32_t  space_id = klass.space_id();

  guarantee(space_id == objectRef::old_space_id,          "verify_ref_klass(): found klassRef not in old space");
  guarantee(nmt      == GPGC_NMT::desired_old_nmt_flag(), "verify_ref_klass(): found klassRef with invalid NMT");

  return;
JRT_END


// --- lazy_c2i
// Entry point for compiled vtable-stub calls that need a C2I adapter.  We get
// here from a vtable-stub that is calling a method that has never been called
// from compiled code before.  All argument registers are full, RDI has the
// receiver, and R11 has the methodOop (not methodRef because the vtable stub
// has striped the metadata bits already).
JRT_LEAF(address, SharedRuntime, lazy_c2i, (JavaThread *thread, methodOop moop))
  return moop->from_compiled_entry(); // Make c2i adapters if needed
JRT_END


// -------------------------------- SBA ------------------------------
// value - value escaping the stack
// base  - reference to the object the value is being stored into
XRT_ENTRY_EX(void, SharedRuntime, sba_escape, (JavaThread* thread, stackRef value, objectRef base))
{
  assert0( value.is_stack() ); // No escaping if value is already escaped!

  int escape_to_fid = base.is_stack()
    ? stackRef(base).preheader()->fid()
    : HEAP_FID;

  assert0( escape_to_fid == HEAP_FID || // Escape-to-heap
           (is_valid_fid(escape_to_fid) && // or stack-to-stack escape
            escape_to_fid < value.preheader()->fid()) ); // but to an older frame

  // Do the escape. NB base and value will be junk after this call.
  StackBasedAllocation::do_escape(thread, value, escape_to_fid, base.klass_id(), "STA/SVB trap");
}
JRT_END

// --- SharedRuntime::_new ---------------------------------------------------
// Allocation of a given object; either it has a finalizer or it didn't fit in
// the current thread's tlab (so we likely need a tlab expansion as well).
// Stuff the klass and mark words down and zero the rest.  size_in_bytes is
// pre-rounded.
JRT_ENTRY_NO_GC_ON_EXIT( objectRef, SharedRuntime, _new, (JavaThread *thread, int KID, intptr_t size_in_bytes, jint length, intptr_t sba_hint))
  KlassHandle h_k(KlassTable::getKlassByKlassId(KID));
  Klass *K = NULL;
  if( !h_k->oop_is_array() ) { 
    // Non-arrays need to check for JVMTI interference
    h_k->check_valid_for_instantiation(true, thread);
if(thread->has_pending_exception()){
deopt_compiled_caller(thread);
      return nullRef;
    }
    // make sure klass is initialized
    h_k->initialize(thread);
if(thread->has_pending_exception()){
deopt_compiled_caller(thread);
      return nullRef;
    }
  }
  K = Klass::cast(KlassTable::getKlassByKlassId(KID).as_klassOop());

  assert0( !K->is_abstract() );
  assert0( K->oop_is_array() || length == 0 );

  // Attempt SBA
  if( UseSBA && sba_hint ) {
thread->mmu_start_pause();
    stackRef obj = thread->sba_area()->allocate(K->as_klassOop(), size_in_bytes>>LogHeapWordSize, length, sba_hint);
thread->mmu_end_pause();
    if( obj.not_null() ) return obj;
  }

  // SBA failed so attempt heap allocation
thread->mmu_start_pause();
  oop o;
if(K->oop_is_array()){
    o = K->oop_is_objArray() 
      ? (oop)((( objArrayKlass*)K)->allocate(length,false/*No SBA*/,THREAD))
      : (oop)(((typeArrayKlass*)K)->allocate(length,false/*No SBA*/,THREAD));
  } else {
    o = CollectedHeap::obj_allocate(h_k, size_in_bytes>>LogHeapWordSize, THREAD);
  }
thread->mmu_end_pause();

  // The caller of this code does not check for pending exceptions, because C2
  // would rather deoptimize than deal with the really rare exception returns.
  // "new" throws OutOfMemory or asynchronous exceptions only.  Hence we need
  // to deopt the caller if an exception is pending.
if(thread->has_pending_exception()){
deopt_compiled_caller(thread);
    return nullRef;
  } else {
    StackBasedAllocation::collect_heap_allocation_statistics(size_in_bytes);
    assert0( (o->size()<<LogHeapWordSize) == size_in_bytes ); // Double-check size
return heapRef(o);
  }
JRT_END


// --- SharedRuntime::post_method_entry --------------------------------------
JRT_ENTRY(void, SharedRuntime, post_method_entry, (JavaThread *thread))
  // sender could be an interpreter frame or a native method stub 

  methodOop method = NULL;
  frame fr = frame(0, 0);
  {
    vframe vf = thread->last_java_vframe();
    method = vf.method();
    fr = vf.get_frame();
  }

JvmtiExport::post_method_entry(thread,method,fr);
JRT_END


// --- SharedRuntime::post_method_exit ---------------------------------------
JRT_ENTRY(void, SharedRuntime, post_method_exit, (JavaThread *thread, uint64_t return_value))
  // sender could be an interpreter frame or a native method stub 

  methodOop method = NULL;
  frame fr = frame(0, 0);
  {
    vframe vf = thread->last_java_vframe();
    method = vf.method();
    fr = vf.get_frame();
  }

  JvmtiExport::post_method_exit(thread, method, fr, return_value);
JRT_END

void SharedRuntime::print_statistics(xmlBuffer *xb) {
  xmlElement xe(xb, "runtime_calls");
  xmlElement xf(xb, "name_value_table");
  xb->name_value_item( "monitor_enter_slow",  _az_mon_enter_ctr  );
  xb->name_value_item( "monitor_exit_slow",   _az_mon_exit_ctr   );
  xb->name_value_item( "monitor_wait_slow",   _mon_wait_ctr   );
  xb->name_value_item( "inflated_monitors",   ObjectSynchronizer::_monCount );
  xb->name_value_item( "free_monitors",       ObjectMonitor::freeMonCount() );
  xb->name_value_item( "implicit_null_throw", _throw_null_ctr );
  xb->name_value_item( "resolve_and_patch_call", _resolve_and_patch_call_ctr );
  xb->name_value_item( "implicit_rangecheck_throw", _throw_range_ctr );
  print_arraycopy_stats(xb);
}

static int idx2bytes( int i ) { return ((2+(i&1))<<(i>>1))>>1; }
JRT_LEAF(void, SharedRuntime, collect_arraycopy_stats, ( int len )) 
  // Convert len to a histogram element.  We're using powers of 2 and 1/2's,
  // we have buckets for length 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, ...
  // and bucket index is        0, 1, 2, 3, 4, 5, 6,  7,  8,  9, 10, 11, 12, ... 
if(len<4){
    _arraycopy_stats[len]++;
    return;
  }
  int lg2 = log2_intptr(len);
  int delta = len - (1<<lg2);
  int idx = (lg2<<1) + (( delta+delta < (1<<lg2) ) ? 0 : 1);
  assert( idx2bytes(idx) <= len && len < idx2bytes(idx+1), "sanity" );
  _arraycopy_stats[idx]++;      // Non-atomic shared update; may drop some stats
JRT_END

void SharedRuntime::print_arraycopy_stats(){
assert(idx2bytes(9)==24,"sanity");
for(int i=1;i<4;i++)
if(_arraycopy_stats[i])
tty->print_cr("arraycopy[%d bytes] happened %d times",i,_arraycopy_stats[i]);
  for( uint i=4; i<sizeof(_arraycopy_stats)/sizeof(_arraycopy_stats[0]); i++ )
if(_arraycopy_stats[i])
tty->print_cr("arraycopy[%d-%d bytes] happened %d times",
                    idx2bytes(i), idx2bytes(i+1)-1, _arraycopy_stats[i] );
}

void SharedRuntime::print_arraycopy_stats(xmlBuffer *xb) {
  if( !ProfileArrayCopy ) {
    xb->name_value_item("None", "Since +ProfileArrayCopy not used, no arraycopy stats collected");
    return;
  }
assert(idx2bytes(9)==24,"sanity");
for(int i=1;i<4;i++){
if(_arraycopy_stats[i]){
      char namebuf[100];
sprintf(namebuf,"arraycopy_%d_bytes",i);
      xb->name_value_item(namebuf,_arraycopy_stats[i]);
    }
  }
  for( uint i=4; i<sizeof(_arraycopy_stats)/sizeof(_arraycopy_stats[0]); i++ ) {
if(_arraycopy_stats[i]){
      char namebuf[100];
      sprintf(namebuf,"arraycopy_%d-%d_bytes",idx2bytes(i), idx2bytes(i+1)-1);
      xb->name_value_item(namebuf,_arraycopy_stats[i]);
    }
  }
}


#ifndef PRODUCT

void SharedRuntime::print_statistics() {
  ttyLocker ttyl;

if(_mon_wait_ctr)tty->print_cr("%5d monitor wait slow",_mon_wait_ctr);
  if (_throw_null_ctr) tty->print_cr("%5d implicit null throw", _throw_null_ctr);
if(_resolve_and_patch_call_ctr)tty->print_cr("%5d resolve_and_patch_call",_resolve_and_patch_call_ctr);
if(_throw_range_ctr)tty->print_cr("%5d implicit rangecheck throw",_throw_range_ctr);
  print_arraycopy_stats();

  // Dump the JRT_ENTRY counters
  if( _multi1_ctr ) tty->print_cr("%5d multianewarray 1 dim", _multi1_ctr);
  if( _multi2_ctr ) tty->print_cr("%5d multianewarray 2 dim", _multi2_ctr);
  if( _multi3_ctr ) tty->print_cr("%5d multianewarray 3 dim", _multi3_ctr);
  if( _multi4_ctr ) tty->print_cr("%5d multianewarray 4 dim", _multi4_ctr);
  if( _multi5_ctr ) tty->print_cr("%5d multianewarray 5 dim", _multi5_ctr);

if(_az_mon_enter_stub_ctr)tty->print_cr("%5d monitor enter stub",_az_mon_enter_stub_ctr);
if(_az_mon_exit_stub_ctr)tty->print_cr("%5d monitor exit stub",_az_mon_exit_stub_ctr);
if(_az_mon_enter_ctr)tty->print_cr("%5d monitor enter slow",_az_mon_enter_ctr);
if(_az_mon_exit_ctr)tty->print_cr("%5d monitor exit slow",_az_mon_exit_ctr);
  if( _jbyte_array_copy_ctr ) tty->print_cr("%5d byte array copies", _jbyte_array_copy_ctr );
  if( _jshort_array_copy_ctr ) tty->print_cr("%5d short array copies", _jshort_array_copy_ctr );
  if( _jint_array_copy_ctr ) tty->print_cr("%5d int array copies", _jint_array_copy_ctr );
  if( _jlong_array_copy_ctr ) tty->print_cr("%5d long array copies", _jlong_array_copy_ctr );
  if( _oop_array_copy_ctr ) tty->print_cr("%5d oop array copies", _oop_array_copy_ctr );
  if( _find_handler_ctr ) tty->print_cr("%5d find exception handler", _find_handler_ctr );
  if( _rethrow_ctr ) tty->print_cr("%5d rethrow handler", _rethrow_ctr );
}

#endif // !PRODUCT

