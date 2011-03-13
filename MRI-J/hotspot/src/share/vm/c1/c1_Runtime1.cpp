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


#include "allocation.hpp"
#include "barrierSet.hpp"
#include "bytecode.hpp"
#include "c1_CodeStubs.hpp"
#include "c1_Compilation.hpp"
#include "c1_MacroAssembler.hpp"
#include "c1_Runtime1.hpp"
#include "c1_globals.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "compiledIC.hpp"
#include "compilationPolicy.hpp"
#include "copy.hpp"
#include "disassembler_pd.hpp"
#include "deoptimization.hpp"
#include "gcLocker.hpp"
#include "handles.hpp"
#include "instanceKlass.hpp"
#include "interfaceSupport.hpp"
#include "linkResolver.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "sharedRuntime.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "vframe.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "barrierSet.inline.hpp"
#include "frame.inline.hpp"
#include "hashtable.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "nativeInst_pd.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

// Implementation of Runtime1

bool      Runtime1::_is_initialized = false;
address   Runtime1::_entries[Runtime1::number_of_ids];
const char *Runtime1::_blob_names[] = {
  RUNTIME1_STUBS(STUB_NAME, LAST_STUB_NAME)
};

#ifndef PRODUCT
// statistics
int Runtime1::_new_multi_array_slowcase_cnt = 0;
int Runtime1::_patch_code_slowcase_cnt = 0;
int Runtime1::_throw_range_check_exception_count = 0;
int Runtime1::_throw_index_exception_count = 0;
int Runtime1::_throw_div0_exception_count = 0;
int Runtime1::_throw_null_pointer_exception_count = 0;
int Runtime1::_throw_class_cast_exception_count = 0;
int Runtime1::_throw_incompatible_class_change_error_count = 0;
int Runtime1::_throw_array_store_exception_count = 0;
int Runtime1::_throw_count = 0;
#endif

//BufferBlob* Runtime1::_buffer_blob  = NULL;

// Simple helper to see if the caller of a runtime stub which
// entered the VM has been deoptimized

static bool caller_is_deopted() {
  JavaThread* thread = JavaThread::current();
  frame runtime_frame = thread->last_frame();
frame caller_frame=runtime_frame.sender();
  assert(caller_frame.is_compiled_frame(), "must be compiled");
CodeBlob*blob=CodeCache::find_blob(caller_frame.pc());
  return blob->owner().as_methodCodeOop()->_patched_for_deopt;
}

void Runtime1::generate_blob_for(C1StubGenerator *csg, StubID id) {
  assert(0 <= id && id < number_of_ids, "illegal stub id");

  csg->assembler()->align(CodeEntryAlignment);
  _entries[id] = csg->assembler()->pc();

  // generate code for runtime stub
  generate_code_for(id, csg->c1assembler());
csg->assembler()->flush();
}


void Runtime1::initialize() {
  ResourceMark rm;
  // Warning: If we have more than one compilation running in parallel, we
  //          need a lock here with the current setup (lazy initialization).
  if (!is_initialized()) {
MutexLocker ml(Patching_lock);
    if (is_initialized()) return;
    _is_initialized = true;

    // platform-dependent initialization
    initialize_pd();

    // generate stubs
    C1StubGenerator csg;
    MacroAssembler *masm = csg.assembler();
    masm->grow(8192); // Force him to initialize

    for (int id = 0; id < number_of_ids; id++) generate_blob_for(&csg, (StubID)id);

    MutexLocker mltc(ThreadCritical_lock);
CodeBlob*cb=masm->blob();
    hotspot_to_gdb_symbol_t *hsgdb = CodeCache::get_new_hsgdb();
    hsgdb->startPC = (uint32_t)(intptr_t)cb->code_begins();
    hsgdb->frameSize = frame::runtime_stub_frame_size;
    hsgdb->nameAddress = "C1::Runtime1 stubs";
    hsgdb->nameLength = strlen(hsgdb->nameAddress);
    hsgdb->savedRBP = false;
    hsgdb->pad1 = hsgdb->pad2 = hsgdb->pad3 = 0;
    masm->bake_into_CodeBlob(frame::runtime_stub_frame_size); 
    hsgdb->codeBytes = cb->code_size();

    // printing
#ifndef PRODUCT
    if (PrintSimpleStubs) {
for(int id=0;id<number_of_ids-1;id++){
        tty->print_cr(name_for((StubID)id));
        Disassembler::decode(_entries[id],_entries[id+1]);
      }
      tty->print_cr(name_for((StubID)(number_of_ids-1)));
      Disassembler::decode(_entries[number_of_ids-1],cb->code_ends());
    }
#endif
  }
}


const char* Runtime1::name_for(StubID id) {
  assert(0 <= id && id < number_of_ids, "illegal stub id");
  return _blob_names[id];
}

const char* Runtime1::name_for_address(address entry) {
  for (int id = 0; id < number_of_ids; id++) {
    if (entry == entry_for((StubID)id)) return name_for((StubID)id);
  }

#define FUNCTION_CASE(a, f) \
  if ((intptr_t)a == CAST_FROM_FN_PTR(intptr_t, f))  return #f

  FUNCTION_CASE(entry, os::javaTimeMillis);
  FUNCTION_CASE(entry, os::javaTimeNanos);
  FUNCTION_CASE(entry, SharedRuntime::OSR_migration_end);
  FUNCTION_CASE(entry, SharedRuntime::d2f);
  FUNCTION_CASE(entry, SharedRuntime::d2i);
  FUNCTION_CASE(entry, SharedRuntime::d2l);
  FUNCTION_CASE(entry, SharedRuntime::dcos);
  FUNCTION_CASE(entry, SharedRuntime::dexp);
  FUNCTION_CASE(entry, SharedRuntime::dlog);
  FUNCTION_CASE(entry, SharedRuntime::dlog10);
  FUNCTION_CASE(entry, SharedRuntime::dpow);
  FUNCTION_CASE(entry, SharedRuntime::drem);
  FUNCTION_CASE(entry, SharedRuntime::dsin);
  FUNCTION_CASE(entry, SharedRuntime::dtan);
  FUNCTION_CASE(entry, SharedRuntime::f2i);
  FUNCTION_CASE(entry, SharedRuntime::f2l);
  FUNCTION_CASE(entry, SharedRuntime::frem);
  FUNCTION_CASE(entry, SharedRuntime::l2d);
  FUNCTION_CASE(entry, SharedRuntime::l2f);
  FUNCTION_CASE(entry, SharedRuntime::ldiv);
  FUNCTION_CASE(entry, SharedRuntime::lmul);
  FUNCTION_CASE(entry, SharedRuntime::lrem);
  FUNCTION_CASE(entry, SharedRuntime::lrem);
  FUNCTION_CASE(entry, trace_block_entry);
FUNCTION_CASE(entry,new_multi_array);

#undef FUNCTION_CASE    

  return NULL;
}


Runtime1::StubID Runtime1::contains(address entry) {
  if( entry < entry_for((StubID)0) ) return (StubID)-1;
  for( int id = 1; id < number_of_ids; id++)
    if( entry < _entries[id] ) 
      return (StubID)(id-1);
  return (StubID)-1;
}


JRT_ENTRY_NO_GC_ON_EXIT(objectRef, Runtime1, new_multi_array, (JavaThread* thread, klassRef klass, int rank, jint* dims))
  NOT_PRODUCT(_new_multi_array_slowcase_cnt++;)

  assert(klass.as_oop()->is_klass(), "not a class");
  assert(rank >= 1, "rank must be nonzero");
oop obj=arrayKlass::cast(klass.as_klassOop())->multi_allocate(rank,dims,UseSBA,CHECK_(nullRef));
return obj;
JRT_END


JRT_ENTRY(void, Runtime1, unimplemented_entry, (JavaThread* thread, StubID id))
C1OUT->print_cr("Runtime1::entry_for(%d) returned unimplemented entry point",id);
JRT_END


JRT_ENTRY(void, Runtime1, throw_array_store_exception, (JavaThread* thread))
  THROW(vmSymbolHandles::java_lang_ArrayStoreException());
JRT_END


JRT_ENTRY(void, Runtime1, post_jvmti_exception_throw, (JavaThread* thread))
  Unimplemented();
//  if (JvmtiExport::can_post_exceptions()) {
//    vframeStream vfst(thread, true);
//    address bcp = vfst.method()->bcp_from(vfst.bci());
//    JvmtiExport::post_exception_throw(thread, vfst.method(), bcp, thread->exception_oop());
//  }
JRT_END

extern void vm_exit(int code);

// Enter this method from compiled code handler below. This is where we transition
// to VM mode. This is done as a helper routine so that the method called directly
// from compiled code does not have to transition to VM. This allows the entry
// method to see if the nmethod that we have just looked up a handler for has
// been deoptimized while we were in the vm. This simplifies the assembly code
// cpu directories.
//
// We are entering here from exception stub (via the entry method below)
// If there is a compiled exception handler in this method, we will continue there;
// otherwise we will unwind the stack and continue at the caller of top frame method
// Note: we enter in Java using a special JRT wrapper. This wrapper allows us to
// control the area where we can allow a safepoint. After we exit the safepoint area we can
// check to see if the handler we are going to return is now in a nmethod that has
// been deoptimized. If that is the case we return the deopt blob
// unpack_with_exception entry instead. This makes life for the exception blob easier
// because making that same check and diverting is painful from assembly language.
//


//JRT_ENTRY_NO_ASYNC(static address, exception_handler_for_pc_helper(JavaThread* thread, oopDesc* ex, address pc, nmethod*& nm))
//
//  Handle exception(thread, ex);
//  nm = CodeCache::find_nmethod(pc);
//  assert(nm != NULL, "this is not an nmethod");
//  // Adjust the pc as needed/
//  if (nm->is_deopt_pc(pc)) {
//    RegisterMap map(thread, false);
//    frame exception_frame = thread->last_frame().sender(&map);
//    // if the frame isn't deopted then pc must not correspond to the caller of last_frame
//    assert(exception_frame.is_deoptimized_frame(), "must be deopted");
//    pc = exception_frame.pc();
//  }
//#ifdef ASSERT
//  assert(exception.not_null(), "NULL exceptions should be handled by throw_exception");
//  assert(exception->is_oop(), "just checking");
//  // Check that exception is a subclass of Throwable, otherwise we have a VerifyError
//  if (!(exception->is_a(SystemDictionary::throwable_klass()))) {
//    if (ExitVMOnVerifyError) vm_exit(-1);
//    ShouldNotReachHere();
//  }
//#endif
//
//  // Check the stack guard pages and reenable them if necessary and there is
//  // enough space on the stack to do so.  Use fast exceptions only if the guard
//  // pages are enabled.
//  bool guard_pages_enabled = thread->stack_yellow_zone_enabled();
//  if (!guard_pages_enabled) guard_pages_enabled = thread->reguard_stack( expressionStack, userStack );
//
//  if (JvmtiExport::can_post_exceptions()) {
//    // To ensure correct notification of exception catches and throws
//    // we have to deoptimize here.  If we attempted to notify the
//    // catches and throws during this exception lookup it's possible
//    // we could deoptimize on the way out of the VM and end back in
//    // the interpreter at the throw site.  This would result in double
//    // notifications since the interpreter would also notify about
//    // these same catches and throws as it unwound the frame.
//
//    RegisterMap reg_map(thread);
//    frame stub_frame = thread->last_frame();
//    frame caller_frame = stub_frame.sender(&reg_map); 
//
//    // We don't really want to deoptimize the nmethod itself since we
//    // can actually continue in the exception handler ourselves but I
//    // don't see an easy way to have the desired effect.
//    VM_DeoptimizeFrame deopt(thread, caller_frame.id());
//    VMThread::execute(&deopt);
//
//    return SharedRuntime::deopt_blob()->unpack_with_exception_in_tls();
//  }
//
//  // ExceptionCache is used only for exceptions at call and not for implicit exceptions
//  if (guard_pages_enabled) {
//    address fast_continuation = nm->handler_for_exception_and_pc(exception, pc);
//    if (fast_continuation != NULL) {
//      if (fast_continuation == ExceptionCache::unwind_handler()) fast_continuation = NULL;
//      return fast_continuation;
//    }
//  }
//
//  // If the stack guard pages are enabled, check whether there is a handler in
//  // the current method.  Otherwise (guard pages disabled), force an unwind and
//  // skip the exception cache update (i.e., just leave continuation==NULL).
//  address continuation = NULL;
//  if (guard_pages_enabled) {
//
//    // New exception handling mechanism can support inlined methods
//    // with exception handlers since the mappings are from PC to PC
//
//    // debugging support
//    // tracing
//    if (TraceExceptions) {
//      ttyLocker ttyl;
//      ResourceMark rm;
//      tty->print_cr("Exception <%s> (0x%x) thrown in compiled method <%s> at PC " PTR_FORMAT " for thread 0x%x",
//                    exception->print_value_string(), (address)exception(), nm->method()->print_value_string(), pc, thread);
//    }
//    // for AbortVMOnException flag
//    NOT_PRODUCT(Exceptions::debug_check_abort(exception));
//
//    // Clear out the exception oop and pc since looking up an
//    // exception handler can cause class loading, which might throw an
//    // exception and those fields are expected to be clear during
//    // normal bytecode execution.
//    thread->set_exception_oop(NULL);
//    thread->set_exception_pc(NULL);
//
//    continuation = SharedRuntime::compute_compiled_exc_handler(nm, pc, exception, false, false);
//    // If an exception was thrown during exception dispatch, the exception oop may have changed
//    thread->set_exception_oop(exception());
//    thread->set_exception_pc(pc);
//
//    // the exception cache is used only by non-implicit exceptions
//    if (continuation == NULL) {
//      nm->add_handler_for_exception_and_pc(exception, pc, ExceptionCache::unwind_handler());
//    } else {
//      nm->add_handler_for_exception_and_pc(exception, pc, continuation);
//    }
//  }
//
//  thread->set_vm_result(exception());
//
//  if (TraceExceptions) {
//    ttyLocker ttyl;
//    ResourceMark rm;
//    tty->print_cr("Thread " PTR_FORMAT " continuing at PC " PTR_FORMAT " for exception thrown at PC " PTR_FORMAT,
//                  thread, continuation, pc);
//  }
//
//  return continuation;
//JRT_END

// Enter this method from compiled code only if there is a Java exception handler
// in the method handling the exception
// We are entering here from exception stub. We don't do a normal VM transition here.
// We do it in a helper. This is so we can check to see if the nmethod we have just
// searched for an exception handler has been deoptimized in the meantime.
address  Runtime1::exception_handler_for_pc(JavaThread* thread) {
  Unimplemented();
  return 0;
//  oop exception = thread->exception_oop();
//  address pc = thread->exception_pc();
//  // Still in Java mode
//  debug_only(ResetNoHandleMark rnhm);
//  nmethod* nm = NULL;
//  address continuation = NULL;
//  {
//    // Enter VM mode by calling the helper
//
//    ResetNoHandleMark rnhm;
//    continuation = exception_handler_for_pc_helper(thread, exception, pc, nm);
//  }
//  // Back in JAVA, use no oops DON'T safepoint
//
//  // Now check to see if the nmethod we were called from is now deoptimized.
//  // If so we must return to the deopt blob and deoptimize the nmethod
//
//  if (nm != NULL && caller_is_deopted()) {
//    continuation = SharedRuntime::deopt_blob()->unpack_with_exception_in_tls();
//  }
//
//  return continuation;
}


JRT_ENTRY(void, Runtime1, throw_range_check_exception, (JavaThread* thread, int index))
  NOT_PRODUCT(_throw_range_check_exception_count++;)
  char message[jintAsStringSize];
  sprintf(message, "%d", index);
  SharedRuntime::throw_and_post_jvmti_exception(thread, vmSymbols::java_lang_ArrayIndexOutOfBoundsException(), message);
JRT_END


JRT_ENTRY(void, Runtime1, throw_index_exception, (JavaThread* thread, int index))
  NOT_PRODUCT(_throw_index_exception_count++;)
  //Events::log("throw_index");
  char message[16];
  sprintf(message, "%d", index);
  SharedRuntime::throw_and_post_jvmti_exception(thread, vmSymbols::java_lang_IndexOutOfBoundsException(), message);
JRT_END


JRT_ENTRY(void, Runtime1, throw_div0_exception, (JavaThread* thread))
  NOT_PRODUCT(_throw_div0_exception_count++;)
  SharedRuntime::throw_and_post_jvmti_exception(thread, vmSymbols::java_lang_ArithmeticException(), "/ by zero");
JRT_END


JRT_ENTRY(void, Runtime1, throw_null_pointer_exception, (JavaThread* thread))
  NOT_PRODUCT(_throw_null_pointer_exception_count++;)
  SharedRuntime::throw_and_post_jvmti_exception(thread, vmSymbols::java_lang_NullPointerException());
JRT_END


JRT_ENTRY(void, Runtime1, throw_class_cast_exception, (JavaThread* thread, objectRef object))
  NOT_PRODUCT(_throw_class_cast_exception_count++;)
  ResourceMark rm(thread);
  char* message = SharedRuntime::generate_class_cast_message(
thread,Klass::cast(object.as_oop()->klass())->external_name());
  SharedRuntime::throw_and_post_jvmti_exception(
    thread, vmSymbols::java_lang_ClassCastException(), message);
JRT_END


JRT_ENTRY(void, Runtime1, throw_incompatible_class_change_error, (JavaThread* thread))
  NOT_PRODUCT(_throw_incompatible_class_change_error_count++;)
  ResourceMark rm(thread);
  SharedRuntime::throw_and_post_jvmti_exception(thread, vmSymbols::java_lang_IncompatibleClassChangeError());
JRT_END


static klassOop resolve_field_return_klass(methodHandle caller, int bci, TRAPS) {
  Bytecode_field* field_access = Bytecode_field_at(caller(), caller->bcp_from(bci));
  // This can be static or non-static field access
  Bytecodes::Code code       = field_access->code();

  // We must load class, initialize class and resolvethe field
  FieldAccessInfo result; // initialize class if needed
  constantPoolHandle constants(THREAD, caller->constants());
  LinkResolver::resolve_field(result, constants, field_access->index(), Bytecodes::java_code(code), false, CHECK_NULL);
  return result.klass()();
}


//
// This routine patches sites where a class wasn't loaded or
// initialized at the time the code was generated.  It handles
// references to classes, fields and forcing of initialization.  Most
// of the cases are straightforward and involving simply forcing
// resolution of a class, rewriting the instruction stream with the
// needed constant and replacing the call in this function with the
// patched code.  The case for static field is more complicated since
// the thread which is in the process of initializing a class can
// access it's static fields but other threads can't so the code
// either has to deoptimize when this case is detected or execute a
// check that the current thread is the initializing thread.  The
// current
//
// Patches basically look like this:
//
//
// patch_site: jmp patch stub     ;; will be patched
// continue:   ...
//             ...
//             ...
//             ...
//
// They have a stub which looks like this:
//
//             ;; patch body
//             movl <const>, reg           (for class constants)
//        <or> movl [reg1 + <const>], reg  (for field offsets)
//        <or> movl reg, [reg1 + <const>]  (for field offsets)
//             <being_init offset> <bytes to copy> <bytes to skip>
// patch_stub: call Runtime1::patch_code (through a runtime stub)
//             jmp patch_site
//
// 
// A normal patch is done by rewriting the patch body, usually a move,
// and then copying it into place over top of the jmp instruction
// being careful to flush caches and doing it in an MP-safe way.  The
// constants following the patch body are used to find various pieces
// of the patch relative to the call site for Runtime1::patch_code.
// The case for getstatic and putstatic is more complicated because
// getstatic and putstatic have special semantics when executing while
// the class is being initialized.  getstatic/putstatic on a class
// which is being_initialized may be executed by the initializing
// thread but other threads have to block when they execute it.  This
// is accomplished in compiled code by executing a test of the current
// thread against the initializing thread of the class.  It's emitted
// as boilerplate in their stub which allows the patched code to be
// executed before it's copied back into the main body of the nmethod.
//
// being_init: get_thread(<tmp reg>
//             cmpl [reg1 + <init_thread_offset>], <tmp reg>
//             jne patch_stub
//             movl [reg1 + <const>], reg  (for field offsets)  <or>
//             movl reg, [reg1 + <const>]  (for field offsets)
//             jmp continue
//             <being_init offset> <bytes to copy> <bytes to skip>
// patch_stub: jmp Runtim1::patch_code (through a runtime stub)
//             jmp patch_site
//
// If the class is being initialized the patch body is rewritten and
// the patch site is rewritten to jump to being_init, instead of
// patch_stub.  Whenever this code is executed it checks the current
// thread against the intializing thread so other threads will enter
// the runtime and end up blocked waiting the class to finish
// initializing inside the calls to resolve_field below.  The
// initializing class will continue on it's way.  Once the class is
// fully_initialized, the intializing_thread of the class becomes
// NULL, so the next thread to execute this code will fail the test,
// call into patch_code and complete the patching process by copying
// the patch body back into the main part of the nmethod and resume
// executing.
//
//

JRT_ENTRY(void, Runtime1, patch_code, (JavaThread* thread, Runtime1::StubID stub_id ))
  NOT_PRODUCT(_patch_code_slowcase_cnt++;)

  ResourceMark rm(thread);
  frame runtime_frame = thread->last_frame();
frame caller_frame=runtime_frame.sender();

  // last java frame on stack
  vframe lastvf = thread->last_java_vframe();

methodHandle caller_method(THREAD,lastvf.method());
  // Note that caller_method->code() may not be same as caller_code because of OSR's
  // Note also that in the presence of inlining it is not guaranteed
  // that caller_method() == caller_code->method()


int bci=lastvf.bci();

  //Events::log("patch_code @ " INTPTR_FORMAT , caller_frame.pc());

  Bytecodes::Code code = Bytecode_at(caller_method->bcp_from(bci))->java_code();

#ifndef PRODUCT
  // this is used by assertions in the access_field_patching_id
  BasicType patch_field_type = T_ILLEGAL;
#endif // PRODUCT
  bool deoptimize_for_volatile = false;
  int patch_field_offset = -1;
  KlassHandle init_klass(THREAD, klassOop(NULL)); // klass needed by access_field_patching code
  Handle load_klass(THREAD, NULL);                // oop needed by load_klass_patching code
  int kid=0;
  if (stub_id == Runtime1::access_field_patching_id) {

    Bytecode_field* field_access = Bytecode_field_at(caller_method(), caller_method->bcp_from(bci));
    FieldAccessInfo result; // initialize class if needed
    Bytecodes::Code code = field_access->code();
    constantPoolHandle constants(THREAD, caller_method->constants());
    LinkResolver::resolve_field(result, constants, field_access->index(), Bytecodes::java_code(code), false, CHECK);
    patch_field_offset = result.field_offset();

    // If we're patching a field which is volatile then at compile it
    // must not have been know to be volatile, so the generated code
    // isn't correct for a volatile reference.  The nmethod has to be
    // deoptimized so that the code can be regenerated correctly.
    // This check is only needed for access_field_patching since this
    // is the path for patching field offsets.  load_klass is only
    // used for patching references to oops which don't need special
    // handling in the volatile case.
    deoptimize_for_volatile = result.access_flags().is_volatile();

#ifndef PRODUCT
    patch_field_type = result.field_type();
#endif
  } else if (stub_id == Runtime1::load_klass_patching_id) {
oop k=0;
    switch (code) {
      case Bytecodes::_putstatic:
      case Bytecodes::_getstatic:
        { klassOop klass = resolve_field_return_klass(caller_method, bci, CHECK);
          // Save a reference to the class that has to be checked for initialization
          init_klass = KlassHandle(THREAD, klass);
          k = klass;
kid=Klass::cast(klass)->klassId();
        }
        break;
      case Bytecodes::_new:
        { Bytecode_new* bnew = Bytecode_new_at(caller_method->bcp_from(bci));
klassOop klass=caller_method->constants()->klass_at(bnew->index(),CHECK);
          k = klass;
kid=klass->klass_part()->klassId();
        }
        break;
      case Bytecodes::_multianewarray:
        { Bytecode_multianewarray* mna = Bytecode_multianewarray_at(caller_method->bcp_from(bci));
klassOop klass=caller_method->constants()->klass_at(mna->index(),CHECK);
          k = klass;
kid=klass->klass_part()->klassId();
        }
        break;
      case Bytecodes::_instanceof:
        { Bytecode_instanceof* io = Bytecode_instanceof_at(caller_method->bcp_from(bci));
klassOop klass=caller_method->constants()->klass_at(io->index(),CHECK);
          k = klass;
kid=klass->klass_part()->klassId();
        }
        break;
      case Bytecodes::_checkcast:
        { Bytecode_checkcast* cc = Bytecode_checkcast_at(caller_method->bcp_from(bci));
klassOop klass=caller_method->constants()->klass_at(cc->index(),CHECK);
          k = klass;
kid=klass->klass_part()->klassId();
        }
        break;
      case Bytecodes::_anewarray:
        { Bytecode_anewarray* anew = Bytecode_anewarray_at(caller_method->bcp_from(bci));
          klassOop ek = caller_method->constants()->klass_at(anew->index(), CHECK);
          klassRef klass = Klass::array_klass(klassRef(ek),CHECK);
          k = klass.as_klassOop();
          kid = klass.as_klassOop()->klass_part()->klassId();
        }
        break;
      case Bytecodes::_ldc:
      case Bytecodes::_ldc_w:
        {
          Bytecode_loadconstant* cc = Bytecode_loadconstant_at(caller_method(),
                                                               caller_method->bcp_from(bci));
          klassOop resolved = caller_method->constants()->klass_at(cc->index(), CHECK);
          // ldc wants the java mirror.
          k = resolved->klass_part()->java_mirror();
          kid = CodeCacheOopTable::putOop(k);
        }
        break;
      default: Unimplemented();
    }
    // convert to handle
    load_klass = Handle(THREAD, k);

    // We now have a new oop referenced from the compiled code.
    // Update the CodeBlob to keep the oop alive.
CodeBlob*blob=CodeCache::find_blob(caller_frame.pc());
    methodCodeOop mcoop = blob->owner().as_methodCodeOop();
    mcoop->add_static_ref(kid,CHECK);

  } else {
    ShouldNotReachHere();
  }

  if (deoptimize_for_volatile) {
    // At compile time we assumed the field wasn't volatile but after
    // loading it turns out it was volatile so we have to throw the
    // compiled code out and let it be regenerated.
    if (TracePatching) {
      tty->print_cr("Deoptimizing for patching volatile field reference");
    }

CodeBlob*blob=CodeCache::find_blob(caller_frame.pc());
    methodCodeOop mcoop = blob->owner().as_methodCodeOop();
    if( mcoop ) {
      mcoop->deoptimize_now(Deoptimization::Reason_for_volatile); // can GC
    }

    // Return to the now deoptimized frame.
    return;
  }

  // Lock & patch code.
  {
MutexLocker ml_patch(Patching_lock,thread);
    //
    // Deoptimization may have happened while we waited for the lock.
    // In that case we don't bother to do any patching we just return
    // and let the deopt happen
if(caller_is_deopted())return;

    NativeGeneralJump* jump = nativeGeneralJump_at(caller_frame.pc());
    int patch_bits = *(int*)(caller_frame.pc() - NativeCall::instruction_size-4);
    address instr_pc = jump->jump_destination();
    NativeInstruction* ni = NativeInstruction::nativeInstruction_at(instr_pc-NativeGeneralJump::instruction_size);
    int* patch_addr1  = (int*)(instr_pc+((patch_bits>> 0)&0xff));
    int* patch_addr2  = (int*)(instr_pc+((patch_bits>> 8)&0xff));
    address jump_addr =       (instr_pc+((patch_bits>>16)&0xff));

    if( TracePatching && ni->is_jump() ) {
tty->print_cr(" Patching %s at bci %d at address %p  (%s)",Bytecodes::name(code),bci,
                    instr_pc, (stub_id == Runtime1::access_field_patching_id) ? "field" : "klass");
      Disassembler::decode((address)ni, jump_addr+NativeGeneralJump::instruction_size, tty);
      tty->cr();
      address patch = ((NativeGeneralJump*)ni)->jump_destination();
      Disassembler::decode(patch,patch+12,tty);
    }

    // Value to patch down
    int patch_val = (stub_id == Runtime1::access_field_patching_id) 
      ? patch_field_offset
      : (int)(intptr_t)((address)KlassTable::getKlassTableBase() + kid*8);

    // Patch the first offset.  In the special case of a klass which is not
    // yet fully initialized the code can already be patched (so the thread
    // running the <clinit> can make progress) but other threads will
    // execute the patched code, fail the thread-check and come through here
    // and attempt to re-patch the already patched code.  
    
    // The initial patch is thread-safe because the guard jump prevents
    // other threads from executing the unpatched code.  Later patch
    // attempts are not so safe... so we do a guarded store.

    if( *patch_addr1 != patch_val ) {
      *patch_addr1 = patch_val;
      ICache::invalidate_range((address)patch_addr1,4);
    }

    // Check for a 2nd patch required
    if( ((patch_bits>> 8)&0xff) != 0 )  {
      if( *patch_addr2 != patch_val ) {
        *patch_addr2 = patch_val;
        ICache::invalidate_range((address)patch_addr2,4);
      }
    }

    if( stub_id == Runtime1::load_klass_patching_id ) {
      // If a getstatic or putstatic is referencing a klass which isn't fully
      // initialized, the 2nd guard jump is not removed.  In this case the
      // patch site is setup so that any threads besides the initializing
      // thread are forced to come into the VM and block.
      if( (code != Bytecodes::_getstatic && code != Bytecodes::_putstatic) ||
          instanceKlass::cast(init_klass())->is_initialized() ) 
        NativeNop::create_at(jump_addr, NativeJump::instruction_size);
    }

    // Atomically wipe out the initial guard jump.  Instantly all ops in the
    // patch can execute.
    NativeNop::create_at((address)ni, NativeJump::instruction_size);

    if (TracePatching) {
tty->print_cr(" Post Patch %s at bci %d at address %p  (%s)",Bytecodes::name(code),bci,
                    instr_pc, (stub_id == Runtime1::access_field_patching_id) ? "field" : "klass");
      Disassembler::decode((address)ni, jump_addr+NativeGeneralJump::instruction_size, tty);
    }
      
  }
}


// Patch a load-klass instruction.  Unlike JavaSoft we don't care if we
// deoptimize during patching: the deopt code will Do The Right Thing and a
// plain return is fine.
JRT_ENTRY(int, Runtime1, move_klass_patching, (JavaThread* thread))
//
// NOTE: we are still in Java 
//
  debug_only(NoHandleMark nhm;)
  {
    // Enter VM mode

    ResetNoHandleMark rnhm;
    patch_code(thread, load_klass_patching_id);
  }
  // Back in JAVA, use no oops DON'T safepoint

  // Return true if calling code is deoptimized

  return caller_is_deopted();
JRT_END

// Patch a access-field instruction.  Unlike JavaSoft we don't care if we
// deoptimize during patching: the deopt code will Do The Right Thing and a
// plain return is fine.
JRT_ENTRY(void, Runtime1, access_field_patching, (JavaThread* thread))
  patch_code(thread, access_field_patching_id);
JRT_END


JRT_LEAF(void, Runtime1, trace_block_entry, (jint block_id))
  // for now we just print out the block id
C1OUT->print("%d ",block_id);
JRT_END

// Temporary receiver for C1->C2 promotion request
JRT_ENTRY_NO_GC_ON_EXIT(address, Runtime1, frequency_counter_overflow, (JavaThread* thread, address continue_in_c1)) {
  ResourceMark rm(thread);

  frame runtime_frame = thread->last_frame();
  frame caller_frame = runtime_frame.sender();

CodeBlob*blob=CodeCache::find_blob(continue_in_c1);
  assert0(blob && blob->is_c1_method());
  // We would like to assert that we have hit the promotion threshold but we cannot:
  // another racing thread might have promoted already.
  // CodeProfile *cp = cb->get_codeprofile();
  // assert( cp->invoke_count() + cp->backedge_count() >= C1PromotionThreshold, "Should have not promoted yet");
  methodOop moop = blob->method().as_methodOop();
  // If not compilable as a C2 we need to live on in the C1 code,
  // but would like the frequency overflows to get rare.
  if( !moop->is_c2_compilable() ) {
    blob->owner().as_methodCodeOop()->get_codeprofile()->reset_invoke_count();
    return continue_in_c1;
  }

  // Look for a pre-existing C2 compile.
  methodCodeOop mcoop = moop->lookup_c2();
  if( !mcoop ) {                // No prior C2?
    if (!CodeCache::is_full()) {
      CompilationPolicy::method_invocation_event(moop, 2, continue_in_c1); // GC point
    }
    // If no c2 method exists and we've been stuck in profiled life for a while,
    // just return to C1 code ASAP.  Later, we should whip out an unprofiled version of C1.
    blob->owner().as_methodCodeOop()->get_codeprofile()->reset_invoke_count();
    return continue_in_c1;
  }

  No_Safepoint_Verifier nsv();	// Verify no safepoint across lock

  // In order to patch the caller, we need to know if we are patching an
  // inline-cache or just a plain call site.  Unfortunately, code-inspection
  // cannot reliably tell them apart, we need to use the debug info.  Also
  // if multiple threads start patching we might have a site get 1/2 patched
  // from clean to e.g. caching-flavor-1, and also to caching-flavor-2.
  // Lock around the computation of patching flavor.
  address code = mcoop->_blob->code_begins();
address caller_pc=caller_frame.pc();
CodeBlob*caller_cb=CodeCache::find_blob(caller_pc);
  const DebugScope *dbg = caller_cb->owner().as_methodCodeOop()->_debuginfo->get(caller_pc-(address)caller_cb);
  StripedMutexLocker ml_patch(CompiledIC_locks,caller_pc);
  if( dbg->is_inline_cache() ) {// Patch an inline-cache
    // Find the inline-cache implementing the virtual or interface Java call.  
    CompiledIC *ic = CompiledIC::make_before(caller_pc);
    // Patch it
    ic->set_new_destination(code);
  } else {			// Patch a native call
    // Find the native call implementing the static or opt-virtual Java call.
    // This will fail to recognize inline-caches.  
NativeCall*ncall=nativeCall_before(caller_pc);
    ncall->set_destination_mt_safe(code);
    caller_cb->owner().as_methodCodeOop()->patched_a_call_to(caller_pc,code);
  }
  // Return address to continue execution at
  return code;
} JRT_END

#ifndef PRODUCT
void Runtime1::print_statistics() {
if(_new_multi_array_slowcase_cnt)C1OUT->print_cr(" _new_multi_array_slowcase_cnt:   %d",_new_multi_array_slowcase_cnt);
if(_patch_code_slowcase_cnt)C1OUT->print_cr(" _patch_code_slowcase_cnt:        %d",_patch_code_slowcase_cnt);

if(_throw_range_check_exception_count)C1OUT->print_cr(" _throw_range_check_exception_count:            %d:",_throw_range_check_exception_count);
if(_throw_index_exception_count)C1OUT->print_cr(" _throw_index_exception_count:                  %d:",_throw_index_exception_count);
if(_throw_div0_exception_count)C1OUT->print_cr(" _throw_div0_exception_count:                   %d:",_throw_div0_exception_count);
if(_throw_null_pointer_exception_count)C1OUT->print_cr(" _throw_null_pointer_exception_count:           %d:",_throw_null_pointer_exception_count);
if(_throw_class_cast_exception_count)C1OUT->print_cr(" _throw_class_cast_exception_count:             %d:",_throw_class_cast_exception_count);
if(_throw_incompatible_class_change_error_count)C1OUT->print_cr(" _throw_incompatible_class_change_error_count:  %d:",_throw_incompatible_class_change_error_count);
if(_throw_array_store_exception_count)C1OUT->print_cr(" _throw_array_store_exception_count:            %d:",_throw_array_store_exception_count);
if(_throw_count)C1OUT->print_cr(" _throw_count:                                  %d:",_throw_count);

C1OUT->cr();
}
#endif // PRODUCT

