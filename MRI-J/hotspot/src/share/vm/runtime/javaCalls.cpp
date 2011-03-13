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


#include "auditTrail.hpp"
#include "collectedHeap.hpp"
#include "compileBroker.hpp"
#include "compilationPolicy.hpp"
#include "interfaceSupport.hpp"
#include "javaCalls.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "linkResolver.hpp"
#include "methodOop.hpp"
#include "safepoint.hpp"
#include "signature.hpp"
#include "stubRoutines.hpp"
#include "thread.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"

// -----------------------------------------------------
// Implementation of JavaCallWrapper

JavaCallWrapper::JavaCallWrapper(methodHandle callee_method, Handle receiver, JavaValue* result, TRAPS) : 
  _thread((JavaThread *)THREAD) {
  JavaThread* thread = _thread;
  guarantee(thread->is_Java_thread(), "crucial check - the VM thread cannot and must not escape to Java code");
  assert(!thread->owns_locks(), "must release all locks when leaving VM"); 
  guarantee(!thread->is_Compiler_thread(), "cannot make java calls from the compiler");

  _result   = result;

  // Allocate handle block for Java code.  This must be done before we change
  // thread_state to _thread_in_Java_or_stub, since it can potentially block.
  JNIHandleBlock* new_handles = JNIHandleBlock::allocate_block(thread);

  // Poll for any funny conditions before entering Java code.  This call can
  // install a pending async exception into _pending_exception.
thread->poll_at_safepoint();

  if( UseSBA ) {
    SBAArea *sba = thread->sba_area();
    if( !sba )                  // Allocate an initial SBA area
      thread->set_sba(sba=new SBAArea(thread));
    sba->push_frame();          // Push an SBA frame
  }

  // Make sure to set the oop's after taking the jvm lock - since we can block
  // there.  No one is GC'ing the JavaCallWrapper before the entry frame is on
  // the stack.
  _callee_method = methodRef(callee_method());
  _receiver = receiver.as_ref();

  _handles      = thread->active_handles(); // save previous handle block & Java frame linkage

  // Copy the current anchor into the JavaCallWrapper: we'll need it on a
  // stack crawl to find any Java code that called into the VM (and from the
  // VM called into here).  
  thread->copy_anchor_out(_anchor);
  _jexstk_top = thread->_jexstk_top;
  // Blast the anchor to clear out the 'root_Java_frame' notion.
thread->zap_anchor();


  debug_only(thread->inc_java_call_counter());
  thread->set_active_handles(new_handles); // install new handle block and reset Java frame linkage

  if( _anchor.root_Java_frame() ) // For an initial JavaCall, record the base of
    thread->record_base_of_stack_pointer(); // the stack pointer, found the hard way.
  assert0( thread->_allow_safepoint_count == 0 );
}


JavaCallWrapper::~JavaCallWrapper() {
  assert(_thread == JavaThread::current(), "must still be the same thread");
  assert0( _thread->_allow_safepoint_count == 0 );

  // restore previous handle block & Java frame linkage
  JNIHandleBlock *_old_handles = _thread->active_handles();   
  _thread->set_active_handles(_handles);

  debug_only(_thread->dec_java_call_counter());  

  if( _anchor.root_Java_frame() ) 
    _thread->set_base_of_stack_pointer(NULL);

  // State has been restored now make the anchor frame visible for the profiler.
_thread->copy_anchor_in(_anchor);
  _thread->_jexstk_top = _jexstk_top;

  if( UseSBA ) 
    _thread->sba_area()->pop_frame();

  // Old thread-local info. has been restored.  We are now back in the VM.
_thread->poll_at_safepoint();

  // Release handles after we are marked as being inside the VM again, since this
  // operation might block
  JNIHandleBlock::release_block(_old_handles, _thread);
}


void JavaCallWrapper::oops_do(OopClosure* f) {
  f->do_oop((objectRef*)&_callee_method);
f->do_oop(&_receiver);
  handles()->oops_do(f);  
}

// Called from inside of sender() methods, where we know the entire
// stack was made walkable.
JavaFrameAnchor* JavaCallWrapper::anchor_known_walkable(void) {
  if( !_anchor.is_walkable(_anchor.last_Java_sp_raw()) ) {
    Thread* current_thread = Thread::current();
    if ( current_thread == _thread ) {
      os::make_self_walkable();	// just flush windows
      AuditTrail::log_time(_thread, AuditTrail::MAKE_SELF_WALKABLE, 6);
    } else {
os::make_remote_walkable(_thread);
      AuditTrail::log_time(_thread, AuditTrail::MAKE_REMOTE_WALKABLE, intptr_t(current_thread), 8);
    }
    _anchor.record_walkable();  // record walkable in _anchor's last_java_sp
  }
  return &_anchor;
}

// Helper methods
static BasicType runtime_type_from(JavaValue* result) {
  switch (result->get_type()) {
    case T_BOOLEAN: // fall through
    case T_CHAR   : // fall through
    case T_SHORT  : // fall through
    case T_INT    : // fall through
    case T_BYTE   : // fall through
    case T_VOID   : return T_INT;
    case T_LONG   : return T_LONG;
    case T_FLOAT  : return T_FLOAT;
    case T_DOUBLE : return T_DOUBLE;
    case T_ARRAY  : // fall through
    case T_OBJECT:  return T_OBJECT;
  }
  ShouldNotReachHere();
  return T_ILLEGAL;
}

// ===== object constructor calls =====

void JavaCalls::call_default_constructor(JavaThread* thread, methodHandle method, Handle receiver, TRAPS) {
  assert(method->name() == vmSymbols::object_initializer_name(),    "Should only be called for default constructor");
  assert(method->signature() == vmSymbols::void_method_signature(), "Should only be called for default constructor");

  instanceKlass* ik = instanceKlass::cast(method->method_holder());
  if (ik->is_initialized() && ik->has_vanilla_constructor()) {
    // safe to skip constructor call
  } else {
    static JavaValue result(T_VOID);
    JavaCallArguments args(receiver);  
    call(&result, method, &args, CHECK);
  }
}

// ============ Virtual calls ============

void JavaCalls::call_virtual(JavaValue* result, KlassHandle spec_klass, symbolHandle name, symbolHandle signature, JavaCallArguments* args, TRAPS) {  
  CallInfo callinfo;
  Handle receiver = args->receiver();
  KlassHandle recvrKlass(THREAD, receiver.is_null() ? (klassOop)NULL : receiver->klass());
  LinkResolver::resolve_virtual_call(
          callinfo, receiver, recvrKlass, spec_klass, name, signature,
          KlassHandle(), false, true, CHECK);
  methodHandle method = callinfo.selected_method();
  assert(method.not_null(), "should have thrown exception"); 

  // Invoke the method
  JavaCalls::call(result, method, args, CHECK);
}


void JavaCalls::call_virtual(JavaValue* result, Handle receiver, KlassHandle spec_klass, symbolHandle name, symbolHandle signature, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  call_virtual(result, spec_klass, name, signature, &args, CHECK);
}


void JavaCalls::call_virtual(JavaValue* result, Handle receiver, KlassHandle spec_klass, symbolHandle name, symbolHandle signature, Handle arg1, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  args.push_oop(arg1);
  call_virtual(result, spec_klass, name, signature, &args, CHECK);
}



void JavaCalls::call_virtual(JavaValue* result, Handle receiver, KlassHandle spec_klass, symbolHandle name, symbolHandle signature, Handle arg1, Handle arg2, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  args.push_oop(arg1);
  args.push_oop(arg2);
  call_virtual(result, spec_klass, name, signature, &args, CHECK);
}


// ============ Special calls ============

void JavaCalls::call_special(JavaValue* result, KlassHandle klass, symbolHandle name, symbolHandle signature, JavaCallArguments* args, TRAPS) {
  CallInfo callinfo;
  LinkResolver::resolve_special_call(callinfo, klass, name, signature, KlassHandle(), false, CHECK);
  methodHandle method = callinfo.selected_method();
  assert(method.not_null(), "should have thrown exception");
    
  // Invoke the method
  JavaCalls::call(result, method, args, CHECK);
}


void JavaCalls::call_special(JavaValue* result, Handle receiver, KlassHandle klass, symbolHandle name, symbolHandle signature, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  call_special(result, klass, name, signature, &args, CHECK);
}


void JavaCalls::call_special(JavaValue* result, Handle receiver, KlassHandle klass, symbolHandle name, symbolHandle signature, Handle arg1, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  args.push_oop(arg1);  
  call_special(result, klass, name, signature, &args, CHECK);
}


void JavaCalls::call_special(JavaValue* result, Handle receiver, KlassHandle klass, symbolHandle name, symbolHandle signature, Handle arg1, Handle arg2, TRAPS) {
  JavaCallArguments args(receiver); // One oop argument
  args.push_oop(arg1);
  args.push_oop(arg2);
  call_special(result, klass, name, signature, &args, CHECK);
}


// ============ Static calls ============

void JavaCalls::call_static(JavaValue* result, KlassHandle klass, symbolHandle name, symbolHandle signature, JavaCallArguments* args, TRAPS) {
  CallInfo callinfo;
  LinkResolver::resolve_static_call(callinfo, klass, name, signature, KlassHandle(), false, true, CHECK);
  methodHandle method = callinfo.selected_method();
  assert(method.not_null(), "should have thrown exception");
    
  // Invoke the method
  JavaCalls::call(result, method, args, CHECK);
}


void JavaCalls::call_static(JavaValue* result, KlassHandle klass, symbolHandle name, symbolHandle signature, TRAPS) {
  JavaCallArguments args; // No argument
  call_static(result, klass, name, signature, &args, CHECK);
}


void JavaCalls::call_static(JavaValue* result, KlassHandle klass, symbolHandle name, symbolHandle signature, Handle arg1, TRAPS) {
  JavaCallArguments args(arg1); // One oop argument
  call_static(result, klass, name, signature, &args, CHECK);
}


void JavaCalls::call_static(JavaValue* result, KlassHandle klass, symbolHandle name, symbolHandle signature, Handle arg1, Handle arg2, TRAPS) {
  JavaCallArguments args; // One oop argument
  args.push_oop(arg1);
  args.push_oop(arg2);
  call_static(result, klass, name, signature, &args, CHECK);
}


// -------------------------------------------------
// Implementation of JavaCalls (low level)


void JavaCalls::call(JavaValue* result, methodHandle method, JavaCallArguments* args, TRAPS) {
  // Check if we need to wrap a potential OS exception handler around thread
  // This is used for e.g. Win32 structured exception handlers
  assert(THREAD->is_Java_thread(), "only JavaThreads can make JavaCalls");  
  // Need to wrap each and everytime, since there might be native code down the
  // stack that has installed its own exception handlers
  os::os_exception_wrapper(call_helper, result, &method, args, THREAD);
}

void JavaCalls::call_helper(JavaValue* result, methodHandle* m, JavaCallArguments* args, TRAPS) {
  methodHandle method = *m;
  JavaThread* thread = (JavaThread*)THREAD;
  assert(thread->is_Java_thread(), "must be called by a java thread"); 
  assert(method.not_null(), "must have a method to call");  
  assert(!SafepointSynchronize::is_at_safepoint(), "call to Java code during VM operation");
  assert(!thread->handle_area()->no_handle_mark_active(), "cannot call out to Java here");
  

  CHECK_UNHANDLED_OOPS_ONLY(thread->clear_unhandled_oops();)

  // Make sure that the arguments have the right type
  debug_only(args->verify(method, result->get_type(), thread));

  // Ignore call if method is empty
  if (method->is_empty_method()) {
    assert(result->get_type() == T_VOID, "an empty method must return a void value");
    return;
  }
    

#ifdef ASSERT
  { klassOop holder = method->method_holder();    
    // A klass might not be initialized since JavaCall's might be used during the executing of
    // the <clinit>. For example, a Thread.start might start executing on an object that is
    // not fully initialized! (bad Java programming style)
assert(instanceKlass::cast(holder)->is_linked(),"rewriting must have taken place");
  }
#endif


  if( UseC1 || UseC2 ) {
    if( UseC1 && CompilationPolicy::mustBeCompiled(method,1) )
     CompileBroker::_c1.producer_add_task(method, method, InvocationEntryBci);
  //if( UseC2 && !UseC1 && CompilationPolicy::mustBeCompiled(method,2) )
  //  CompileBroker::_c2.producer_add_task(method, method, InvocationEntryBci);
  }

  // Get the entry point as-if called from the interpreter.
  address entry_point = method->from_interpreted_entry();
  if (JvmtiExport::can_post_interpreter_events() && thread->is_interp_only_mode()) {
    entry_point = method->interpreter_entry();      
  }

  // Figure out if the result value is an oop or not (Note: This is a different value
  // than result_type. result_type will be T_INT of oops. (it is about size)    
  BasicType result_type = runtime_type_from(result);  

  // Find receiver
  Handle receiver = (!method->is_static()) ? args->receiver() : Handle();

  // Azul - stack overflow done at first safepoint poll, no need to do it here
  //// When we reenter Java, we need to reenable the yellow zone which
  //// might already be disabled when we are in VM.
  //if (thread->stack_yellow_zone_disabled()) {
  //  thread->reguard_stack();
  //}
  //
  ////// Check that there are shadow pages available before changing thread state
  //// to Java
  //if (!os::stack_shadow_pages_available(THREAD, method)) {
  //  // Throw stack overflow exception with preinitialized exception.
  //  Exceptions::throw_stack_overflow_exception(THREAD, __FILE__, __LINE__);
  //  return;
  //} else {
  //  // Touch pages checked if the OS needs them to be touched to be mapped.
  //  os::bang_stack_shadow_pages();
  //}

  // do call
  { JavaCallWrapper link(method, receiver, result, CHECK);  
    { HandleMark hm(thread);  // HandleMark used by HandleMarkCleaner
      VMTagMark vmt(thread, VM_JavaCode_tag);

      StubRoutines::call_stub()(
        (address)&link,        
        (intptr_t*)result->get_value_addr(),
        result_type,
        method.as_ref(),
        entry_point,
        args->parameters(),
        args->size_of_parameters(),
        CHECK
      );    
  
      // Preserve oop return value across possible gc points.  Also the pop of
      // JavaCallWrapper can trigger an SBA pop-frame and an SBA escape of the
      // returned value.  Since we just checked for pending exceptions (the
      // CHECK macro above) and didn't find any the thread->_pending_exception
      // field is empty.  We use it as the GC & SBA-safe object-return field.
      if( result_type == T_OBJECT )
        thread->set_pending_exception_ref(*(objectRef*)result->get_value_addr());
    }
  } // Exit JavaCallWrapper (can block - potential return oop must be preserved)

  // Restore possible oop return 
if(result_type==T_OBJECT){
    *(objectRef*)result->get_value_addr() = thread->pending_exception_ref();
    thread->clear_pending_exception();
  }
}


//--------------------------------------------------------------------------------------
// Implementation of JavaCallArguments

intptr_t* JavaCallArguments::parameters() {
  // First convert all handles to oops
  for(int i = 0; i < _size; i++) {
    if (_is_oop[i]) {
//Ref's stored into _value are not poisoned, as the assembly that installs the args
//into the stack frame doesn't know types, and thus can't unpoison at that time:
_value[i]=(Handle::raw_resolve((objectRef*)_value[i])).raw_value();
    }
  }
  // Return argument vector
  // No support for TaggedStackInterpreter, just return _value
  return _value;
}

//--------------------------------------------------------------------------------------
// Non-Product code
#ifndef PRODUCT

class SignatureChekker : public SignatureIterator {
 private:
   bool *_is_oop;
   int   _pos;
   BasicType _return_type;

 public:
  bool _is_return;

  SignatureChekker(symbolHandle signature, BasicType return_type, bool is_static, bool* is_oop) : SignatureIterator(signature) {
    _is_oop = is_oop;
    _is_return = false;
    _return_type = return_type;
    _pos = 0;    
    if (!is_static) {      
      check_value(true); // Receiver must be an oop
    }
  }

  void check_value(bool type) {    
    guarantee(_is_oop[_pos++] == type, "signature does not match pushed arguments");
  }

  void check_doing_return(bool state) { _is_return = state; }

  void check_return_type(BasicType t) {    
    guarantee(_is_return && t == _return_type, "return type does not match");
  }

  void check_int(BasicType t) {
    if (_is_return) {
      check_return_type(t);
      return;
    }      
    check_value(false);
  }

  void check_double(BasicType t) { check_long(t); }

  void check_long(BasicType t) {
    if (_is_return) {
      check_return_type(t);
      return;
    }
    
    check_value(false); 
    check_value(false);
  }

  void check_obj(BasicType t) {
    if (_is_return) {
      check_return_type(t);
      return;
    }    
    check_value(true);
  }

  void do_bool()                       { check_int(T_BOOLEAN);       }
  void do_char()                       { check_int(T_CHAR);          }
  void do_float()                      { check_int(T_FLOAT);         }
  void do_double()                     { check_double(T_DOUBLE);     }
  void do_byte()                       { check_int(T_BYTE);          }
  void do_short()                      { check_int(T_SHORT);         }
  void do_int()                        { check_int(T_INT);           }
  void do_long()                       { check_long(T_LONG);         }
  void do_void()                       { check_return_type(T_VOID);  }
  void do_object(int begin, int end)   { check_obj(T_OBJECT);        }
  void do_array(int begin, int end)    { check_obj(T_OBJECT);        }
};

void JavaCallArguments::verify(methodHandle method, BasicType return_type,
  Thread *thread) {
  guarantee(method->size_of_parameters() == size_of_parameters(), "wrong no. of arguments pushed");

  // Treat T_OBJECT and T_ARRAY as the same
  if (return_type == T_ARRAY) return_type = T_OBJECT;

  // Check that oop information is correct
  symbolHandle signature (thread,  method->signature());

  SignatureChekker sc(signature, return_type, method->is_static(),_is_oop);
  sc.iterate_parameters();
  sc.check_doing_return(true);
  sc.iterate_returntype();
}

#endif // PRODUCT
