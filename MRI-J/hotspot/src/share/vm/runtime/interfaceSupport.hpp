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
#ifndef INTERFACESUPPORT_HPP
#define INTERFACESUPPORT_HPP

#include "auditTrail.hpp"
#include "gcLocker.hpp"
#include "preserveException.hpp"

// Wrapper for all entry points to the virtual machine.
// The HandleMarkCleaner is a faster version of HandleMark.
// It relies on the fact that there is a HandleMark further 
// down the stack (in JavaCalls::call_helper), and just resets
// to the saved values in that HandleMark.

class HandleMarkCleaner: public StackObj {
 private:
  Thread* _thread;
 public:
  HandleMarkCleaner(Thread* thread) {
    _thread = thread;
    _thread->last_handle_mark()->push();
  }
  ~HandleMarkCleaner() {
    _thread->last_handle_mark()->pop_and_restore();
  }

 private:
  inline void* operator new(size_t size, void* ptr) {
    return ptr;
  }
};


class VMTagMark:public StackObj{
 private:
  Thread* _thread;
  int     _tag;
 public:
  VMTagMark(Thread* thread, int tag) {
    _tag = 0;
    _thread = thread;
    if (thread != NULL) {
      _tag = thread->vm_tag(); // capture the orginal tag
      thread->set_vm_tag(tag); // setup tag for current operation
    }
  }
  ~VMTagMark() {
    if (_thread != NULL) {
      _thread->set_vm_tag(_tag); // reset the tag back to the original
    }
  }
};

// InterfaceSupport provides functionality used by the __LEAF and __ENTRY
// macros. These macros are used to guard entry points into the VM and
// perform checks upon leave of the VM.


class InterfaceSupport: AllStatic {
# ifdef ASSERT
 public:
  static long _scavenge_alot_counter;
  static long _fullgc_alot_counter;
  static long _number_of_calls;
  static long _fullgc_alot_invocation;

  // tracing
static void trace(const char*result_type,const char*func,const char*header);

  // Helper methods used to implement +ScavengeALot and +FullGCALot
  static void check_gc_alot() { if (ScavengeALot || FullGCALot) gc_alot(); }
  static void gc_alot();

  static void walk_stack();

  static void stress_derived_pointers();
  static void verify_stack();
  static void verify_last_frame();
# endif
};


// Caller must record last_Java_sp so the VM can later release the JVM lock if needed.
class ThreadInVMfromJava:public StackObj{
  JavaThread* const _thread; 
 public:
ThreadInVMfromJava(JavaThread*thread):_thread(thread){
    assert0( _thread->_allow_safepoint_count == 0 );
    assert(_thread != NULL && _thread->is_Java_thread(), "must be Java thread"); 
assert(_thread->jvm_locked_by_self(),"how did we lose the jvm lock?");
_thread->verify_anchor();
  }
  ~ThreadInVMfromJava() {
assert(_thread->jvm_locked_by_self(),"how did we lose the jvm lock?");
    // No polling.  Both the interpreter and compiled code will poll soon enough.
    // This means the wrapper does not block, GC, or blow oops.  Hence we can
    // return naked oops from C code, except for the debug-only VMEntryWrapper.
    // No async exception checking: handled by polling.
    // HOWEVER, if the memory backing the hardware register-stack got
    // modified, force the hardware to recognize the memory changes.
    if( _thread->please_self_suspend() & JavaThread::rstak_suspend ) {
      Atomic::flush_rstack();
      _thread->clr_suspend_request(JavaThread::rstak_suspend);
      AuditTrail::log_time(_thread, AuditTrail::MAKE_SELF_WALKABLE, 4);
    }
    assert0( _thread->_allow_safepoint_count == 0 );
    CHECK_UNHANDLED_OOPS_ONLY(thread->clear_unhandled_oops();)
  }
};


// Take the JVM lock (so the VM can touch naked oops) and release it on exit.
// Renable yellow zone stack guards if needed.
class ThreadInVMfromNative:public StackObj{
  JavaThread* const _thread; 
 public:
ThreadInVMfromNative(JavaThread*thread):_thread(thread){
    assert0( _thread->_allow_safepoint_count == 0 );
    assert(_thread != NULL && _thread->is_Java_thread(), "must be Java thread"); 
    // Take the JVM lock (so the VM can handle naked oops) or block in the attempt
_thread->jvm_lock_self();
  }
  ~ThreadInVMfromNative() {     
    // It's possible the stack overflowed while in native code, so reenable
    // the yellow zone if possible, otherwise the yellow zone won't get
    // reenabled until the next exception dispatch.
    if( ::is_in_yellow_zone((ThreadBase*)_thread) )
      ::thread_stack_yellow_zone_reguard((intptr_t)(_thread->_jexstk_top));
    _thread->poll_at_safepoint(); // Last-chance poll to keep safepointing fast
    // Release the JVM lock when back in the native code
    _thread->jvm_unlock_self(); // Fast & easy!
    // VM code can set a pending exception.  Native code will ignore it, but
    // when the native code transitions back to Java code, the Java code needs
    // to handle it.
    if( _thread->has_pending_exception() ) // If there was a pending exception
      _thread->set_jvm_lock_poll_advisory(); // notice it before retaking the jvm lock
    assert0( _thread->_allow_safepoint_count == 0 );
  }
};

// Release the JVM lock in native code and retake it on exit.
class ThreadToNativeFromVM:public StackObj{
protected:
  JavaThread* const _thread; 
public:
ThreadToNativeFromVM(JavaThread*thread):_thread(thread){
    assert0( _thread->_allow_safepoint_count == 0 );
    assert(_thread != NULL && _thread->is_Java_thread(), "must be Java thread"); 
_thread->verify_anchor();
    // We are leaving the VM at this point and going directly to native code. 
    assert(!_thread->owns_locks(), "must release all locks when leaving VM");
    
    // Try to park right before giving up jvm lock
    if (ParkTLAB) {
_thread->tlab().park();
    }
    // Release the JVM lock when back in the native code
    _thread->jvm_unlock_self(); // Fast & easy!
    // VM code can set a pending exception.  Native code will ignore it, but
    // when the native code transitions back to Java code, the Java code needs
    // to handle it.
    if( _thread->has_pending_exception() ) // If there was a pending exception
      _thread->set_jvm_lock_poll_advisory(); // notice it before retaking the jvm lock
    // No safepoint polling required, its optional.

    // No async exception checking: these are already installed in
    // _pending_async_exception and will appear at the next poll.

    // This means we can enter native code with _pending_async_exception
    // already set.
  }
  ~ThreadToNativeFromVM() { 
    // Take the JVM lock (so the VM can handle naked oops) or block in the attempt
    if( _thread->please_self_suspend() ) // Inline 1 check
      _thread->jvm_lock_self_or_suspend(); // Poll & handle funny conditions, then relock
    else
_thread->jvm_lock_self();
    assert0( _thread->_allow_safepoint_count == 0 );
  }
};

// Release the JVM lock while blocked and retake it on exit.
class ThreadBlockInVM:public ThreadToNativeFromVM{
 public:
  ThreadBlockInVM(JavaThread *thread, const char *hint) : ThreadToNativeFromVM(thread) {
    _thread->hint_blocked(hint);
  }
  ~ThreadBlockInVM() { 
_thread->hint_unblocked();
  }
};


// JVMTI support
class ThreadInVMfromUnknown:public StackObj{
  JavaThread* _thread;
 public:
  ThreadInVMfromUnknown() : _thread(NULL) {
    Thread* t = Thread::current();
if(t->is_Complete_Java_thread()){
      JavaThread* t2 = (JavaThread*) t;
      if (!t2->jvm_locked_by_self()) {
        _thread = t2;
        // Take the JVM lock (so the VM can handle naked oops) or block in the attempt
_thread->jvm_lock_self();
        // Used to have a HandleMarkCleaner but that is dangerous as
        // it could free a handle in our (indirect, nested) caller.
        // We expect any handles will be short lived and figure we
        // don't need an actual HandleMark.
      }
    }
  }
  ~ThreadInVMfromUnknown()  {
    if (_thread) {
      // Release the JVM lock when back in the native code
      _thread->jvm_unlock_self(); // Fast & easy!
      if (_thread->has_pending_exception()) { // If there was a pending exception
        _thread->set_jvm_lock_poll_advisory(); // notice it before retaking the jvm lock
      }
    }
  }
};

// called from print_on_error (frame/thread)
// this is a non-blocking call
class ThreadInVMfromError:public StackObj{
  JavaThread* _thread;
  bool        _safe_to_print;
 public:
  ThreadInVMfromError() : _thread(NULL), _safe_to_print(true) {
    Thread* t = Thread::current();
if(t->is_Complete_Java_thread()){
      JavaThread* t2 = (JavaThread*) t;
      if (!t2->jvm_locked_by_self()) {
        _thread = t2;
if(!_thread->jvm_lock_self_attempt()){
          _safe_to_print = false;
        }
      }
    }
  }

  bool safe_to_print()   { return _safe_to_print; }

  ~ThreadInVMfromError()  {
    if (_thread && _safe_to_print) {
      // Release the JVM lock when back in the native code      
      _thread->jvm_unlock_self(); // Fast & easy!
    }
  }
};

// Debug class instantiated in JRT_ENTRY and IRT_ENTRY macro.
// Can be used to verify properties on enter/exit of the VM.

#ifdef ASSERT
class VMEntryWrapper {
 public:
  VMEntryWrapper() {
if(VerifyLastFrame)InterfaceSupport::verify_last_frame();
  }
  ~VMEntryWrapper() {
    JavaThread *thread = JavaThread::current();
    InterfaceSupport::check_gc_alot();
if(WalkStackALot)InterfaceSupport::walk_stack();
if(StressDerivedPointers)InterfaceSupport::stress_derived_pointers();
//do verification AFTER potential deoptimization
if(VerifyStack)InterfaceSupport::verify_stack();
  }
};

class VMEntryWrapper_No_GC {
 public:
  VMEntryWrapper_No_GC() {
    if (VerifyLastFrame) InterfaceSupport::verify_last_frame();
  }
  ~VMEntryWrapper_No_GC() {
    JavaThread *thread = JavaThread::current();
    // No GC: we are returning a naked oop from the VM. // InterfaceSupport::check_gc_alot();
    if (WalkStackALot)         InterfaceSupport::walk_stack();
    // No OopMap for hardware NPE lookups
    //if (StressDerivedPointers) InterfaceSupport::stress_derived_pointers();
    // do verification AFTER potential deoptimization
if(VerifyStack)InterfaceSupport::verify_stack();
  }
};

class VMNativeEntryWrapper {
 public:
  VMNativeEntryWrapper() {
    if (GCALotAtAllSafepoints) InterfaceSupport::check_gc_alot();
  }

  ~VMNativeEntryWrapper() {
    if (GCALotAtAllSafepoints) InterfaceSupport::check_gc_alot();
  }
};

#endif


// VM-internal runtime interface support

#ifdef ASSERT
#define TRACE_CALL(result_type,func,sig)\
    InterfaceSupport::_number_of_calls++;                            \
if(TraceRuntimeCalls)InterfaceSupport::trace(#result_type,#func,#sig);
#else
#define TRACE_CALL(result_type,func,sig)\
    /* do nothing */
#endif


// LEAF routines do not lock, GC or throw exceptions

#define __LEAF(result_type, func, sig)                               \
  TRACE_CALL(result_type, func, sig)                                 \
  debug_only(NoHandleMark __hm;)                                     \
  /* begin of body */


// ENTRY routines may lock, GC and throw exceptions

#define __ENTRY(result_type, func, sig, thread)                      \
  TRACE_CALL(result_type, func, sig)                                 \
  HandleMarkCleaner __hm(thread);                                    \
  Thread* THREAD = thread;                                           \
  /* begin of body */


// QUICK_ENTRY routines behave like ENTRY but without a handle mark

#define __QUICK_ENTRY(result_type, func, sig, thread)                \
  TRACE_CALL(result_type, func, sig)                                 \
  debug_only(NoHandleMark __hm;)                                     \
  Thread* THREAD = thread;                                           \
  /* begin of body */


// Definitions for GCT (Garbage Collection Trap).  These are special purpose
// entry point wrappers that don't assume the thread is a JavaThread.  They
// also allow a pending_exception on entry, because they are used during GC
// traps, which might be possible while handling a pending exception.  Entry
// points must have a parameter 'Thread * thread' in their signatures.

#define GCT_LEAF(result_type, cls, name, sig)                        \
  result_type cls :: name sig {                                      \
    assert0(thread == Thread::current());                            \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __LEAF(result_type, cls :: name, sig)                            \
    debug_only(No_Safepoint_Verifier __nspv(true);)

#define GCT_END }


// Definitions for GCThread_LEAF.  GC threads sometimes take GC traps, but
// cannot use the GCT_LEAF wrapper, as it contains a No_Safepoint_Verifier.

#define GCThread_LEAF(result_type, cls, name, sig)                   \
  result_type cls :: name sig {                                      \
    assert0(thread == Thread::current());                            \
    assert0(thread->is_GenPauselessGC_thread() ||                    \
            thread->is_GC_task_thread() );                           \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __LEAF(result_type, cls :: name, sig)                            

// Definitions for IRT (Interpreter Runtime)
// (thread is an argument passed in to all these routines)

#define IRT_ENTRY(result_type, cls, name, sig)                       \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
      __ENTRY(result_type, cls :: name, sig, thread)                 \
    assert(thread->has_pending_exception() ? thread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
    debug_only(VMEntryWrapper __vew;)


#define IRT_ENTRY_NO_GC_ON_EXIT(result_type, cls, name, sig)         \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
      __ENTRY(result_type, cls :: name, sig, thread)                 \
    assert(thread->has_pending_exception() ? thread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
debug_only(VMEntryWrapper_No_GC __vew;)


#define IRT_LEAF(result_type, cls, name, sig)                        \
  result_type cls :: name sig {                                      \
    JavaThread *thread = JavaThread::current(); /* used in code body */  \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __LEAF(result_type, cls :: name, sig)                            \
    assert(thread->has_pending_exception() ? thread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
    debug_only(No_Safepoint_Verifier __nspv(true);)


#define IRT_END }


// Definitions for JRT (Java (Compiler/Shared) Runtime)

#define JRT_ENTRY(result_type, cls, name, sig)                       \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __ENTRY(result_type, cls :: name, header, thread)                \
    assert(thread->has_pending_exception() ? thread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
    debug_only(VMEntryWrapper __vew;)

#define JRT_ENTRY_NO_GC_ON_EXIT(result_type, cls, name, sig)         \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __ENTRY(result_type, cls :: name, sig, thread)                   \
    assert(thread->has_pending_exception() ? thread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
debug_only(VMEntryWrapper_No_GC __vew;)

#define JRT_LEAF(result_type, cls, name, sig)                        \
  result_type cls :: name sig {                                      \
JavaThread*jthread=JavaThread::current();\
    VMTagMark __vt(jthread, cls##__##name##_tag);                    \
    __LEAF(result_type, cls :: name, sig)                            \
    assert(jthread->has_pending_exception() ? jthread->unhandled_pending_exception() : true, "unhandled pending-exception in java code" ); \
    debug_only(JRT_Leaf_Verifier __jlv;)


#define JRT_END }

// These calls allow a pending_exception on entry, because they are used
// during stack unwinding operations (like unlock or jvmpi notification)
#define XRT_ENTRY_EX(result_type, cls, name, sig)                    \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __ENTRY(result_type, cls :: name, sig, thread)                   \
    debug_only(VMEntryWrapper __vew;)

#define XRT_ENTRY_EX_NO_GC_ON_EXIT(result_type, cls, name, sig)      \
  result_type cls :: name sig {                                      \
    ThreadInVMfromJava __tiv(thread);                                \
    VMTagMark __vt(thread, cls##__##name##_tag);                     \
    __ENTRY(result_type, cls :: name, sig, thread)                   \
debug_only(VMEntryWrapper_No_GC __vew;)

#define XRT_LEAF_EX(result_type, cls, name, sig)                     \
  result_type cls :: name sig {                                      \
JavaThread*jthread=JavaThread::current();\
    VMTagMark __vt(jthread, cls##__##name##_tag);                    \
    __LEAF(result_type, cls :: name, sig)                            \
    debug_only(JRT_Leaf_Verifier __jlv;)


// Definitions for JNI

#define JNI_ENTRY(result_type, func, sig)                            \
    JNI_ENTRY_NO_PRESERVE(result_type, func, sig)                    \
    WeakPreserveExceptionMark __wem(thread);

#define JNI_ENTRY_NO_PRESERVE(result_type, func, sig)                \
extern "C" {                                                         \
static result_type JNICALL func sig{\
    JavaThread* thread=JavaThread::thread_from_jni_environment(env); \
    assert( !VerifyJNIEnvThread || (thread == Thread::current()), "JNIEnv is only valid in same thread"); \
    ThreadInVMfromNative __tiv(thread);                              \
    debug_only(VMNativeEntryWrapper __vew;)                          \
    VMTagMark __vt(thread, func##_tag);                              \
    __ENTRY(result_type, func, sig, thread)
    

// Ensure that the VMNativeEntryWrapper constructor, which can cause
// a GC, is called outside the NoHandleMark (set via __QUICK_ENTRY).
#define JNI_QUICK_ENTRY(result_type,func,sig)\
extern "C" {                                                         \
static result_type JNICALL func sig{\
    JavaThread* thread=JavaThread::thread_from_jni_environment(env); \
    assert( !VerifyJNIEnvThread || (thread == Thread::current()), "JNIEnv is only valid in same thread"); \
    ThreadInVMfromNative __tiv(thread);                              \
    debug_only(VMNativeEntryWrapper __vew;)                          \
    VMTagMark __vt(thread, func##_tag);                              \
    __QUICK_ENTRY(result_type, func, sig, thread)


// We can't add a No_GC_Verifier here, it will SEGV at startup
#define JNI_LEAF(result_type, func, sig)                             \
extern "C" {                                                         \
static result_type JNICALL func sig{\
    JavaThread* thread=JavaThread::thread_from_jni_environment(env); \
    assert( !VerifyJNIEnvThread || (thread == Thread::current()), "JNIEnv is only valid in same thread"); \
    VMTagMark __vt(thread, func##_tag);                              \
    __LEAF(result_type, func, sig)


// Close the routine and the extern "C"
#define JNI_END } }



// Definitions for JVM

// Note that I tried to assert that native code never calls with VM with 
// a pending exception set, but this fails a JCK test.
#define JVM_ENTRY(result_type, func, sig)                            \
extern "C" {                                                         \
  JNIEXPORT result_type JNICALL func sig {                           \
    JavaThread* thread=JavaThread::thread_from_jni_environment(env); \
    ThreadInVMfromNative __tiv(thread);                              \
    debug_only(VMNativeEntryWrapper __vew;)                          \
    VMTagMark __vt(thread, func##_tag);                              \
    __ENTRY(result_type, func, sig, thread)

#define JVM_ENTRY_NO_ENV(result_type, func, sig)                     \
extern "C" {                                                         \
  JNIEXPORT result_type JNICALL func sig {                           \
    JavaThread* thread = JavaThread::current(); /* used in code body */  \
    ThreadInVMfromNative __tiv(thread);                              \
    debug_only(VMNativeEntryWrapper __vew;)                          \
    VMTagMark __vt(thread, func##_tag);                              \
    __ENTRY(result_type, func, sig, thread)


#define JVM_QUICK_ENTRY(result_type, func, sig)                      \
extern "C" {                                                         \
  JNIEXPORT result_type JNICALL func sig {                           \
    JavaThread* thread=JavaThread::thread_from_jni_environment(env); \
    ThreadInVMfromNative __tiv(thread);                              \
    debug_only(VMNativeEntryWrapper __vew;)                          \
    VMTagMark __vt(thread, func##_tag);                              \
    __QUICK_ENTRY(result_type, func, sig, thread)


#define JVM_LEAF(result_type, func, sig)                             \
extern "C" {                                                         \
  JNIEXPORT result_type JNICALL func sig {                           \
    VM_Exit::block_if_vm_exited();                                   \
    JavaThread* thread = JavaThread::current(); /* used in code body */  \
    VMTagMark __vt(thread, func##_tag);                              \
    __LEAF(result_type, func, sig)

// In proxy callbacks, do not enter the JVM if it is shutting down, since the 
// proxy code needs to free locks that the VM thread needs take to shut down.
// Proxy callbacks have void return type
#define JVM_PROXY_SAFE_LEAF(result_type, func, sig)                  \
extern "C" {                                                         \
  JNIEXPORT void JNICALL func sig {                                  \
    if ( VM_Exit::vm_exited() ) return;                              \
    JavaThread* thread = JavaThread::current(); /* used in code body */  \
    VMTagMark __vt(thread, func##_tag);                              \
    __LEAF(void, func, sig)

#define JVM_END } }


// Definitions for JVMTI
// This can be called by non-HotSpot threads.  So no HandleMarks.
#define JVMTI_ENTRY(result_type, func, sig)                          \
  result_type func sig {                                             \
    Thread* THREAD = Thread::current(); /* used in code body */      \
    ThreadInVMfromUnknown __tivU;                                    \
    TRACE_CALL(result_type, func, sig)                               \
    /* code body... comes next */

#define JVMTI_END  }
#endif // INTERFACESUPPORT_HPP
