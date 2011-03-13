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
#ifndef SHAREDRUNTIME_HPP
#define SHAREDRUNTIME_HPP

#include "allocation.hpp"
#include "frame.hpp"
#include "handles.hpp"
#include "objectMonitor.hpp"
#include "objectRef_pd.hpp"
#include "refsHierarchy_pd.hpp"
#include "stackRef_pd.hpp"
class markWord;

// Runtime is the base class for various runtime interfaces
// (InterpreterRuntime, CompilerRuntime, etc.). It provides
// shared functionality such as exception forwarding (C++ to
// Java exceptions), locking/unlocking mechanisms, statistical
// information, etc.

class SharedRuntime: AllStatic {
 private:
  static int     _resolve_and_patch_call_ctr;    // called uninitialized or not_entrant
 public: // for compiler
  static int _implicit_null_throws;
  static int _implicit_div0_throws;
  static int _jbyte_array_copy_ctr;        // Slow-path byte array copy
  static int _jshort_array_copy_ctr;       // Slow-path short array copy
  static int _jint_array_copy_ctr;         // Slow-path int array copy
  static int _jlong_array_copy_ctr;        // Slow-path long array copy
  static int _oop_array_copy_ctr;          // Slow-path oop array copy

  static int _multi1_ctr, _multi2_ctr, _multi3_ctr, _multi4_ctr, _multi5_ctr; 
  static int _find_handler_ctr;            // find exception handler
  static int _rethrow_ctr;                 // rethrow exception
static int _az_mon_enter_stub_ctr;//monitor enter stub
static int _az_mon_exit_stub_ctr;//monitor exit stub

public:
  // Resolving of calls
  static address resolve_and_patch_call(JavaThread *thread, objectRef recv_or_firstarg);
  // Called by compiled v-calls when we need a c2i adapter.
  static address lazy_c2i( JavaThread *thread, methodOop moop );

  // The following arithmetic routines are used on platforms that do
  // not have machine instructions to implement their functionality.
  // Do not remove these.

  // long arithmetics
  static jlong   lmul(jlong y, jlong x);
  static jlong   ldiv(jlong y, jlong x);
  static jlong   lrem(jlong y, jlong x);
  typedef jlong (*mul_64_64_hi_t)(jlong, jlong);

  // float and double remainder
  static jfloat  frem(jfloat  x, jfloat  y);
  static jdouble drem(jdouble x, jdouble y);

  // float conversion (needs to set appropriate rounding mode)
  static jint    f2i (jfloat  x);
  static jlong   f2l (jfloat  x);
  static jint    d2i (jdouble x);
  static jlong   d2l (jdouble x);
  static jfloat  d2f (jdouble x);
  static jfloat  l2f (jlong   x);
  static jdouble l2d (jlong   x);

  // double trigonometrics and transcendentals
  static jdouble dsin(jdouble x);
  static jdouble dcos(jdouble x);
  static jdouble dtan(jdouble x);
  static jdouble dlog(jdouble x);
  static jdouble dlog10(jdouble x);
  static jdouble dexp(jdouble x);
  static jdouble dpow(jdouble x, jdouble y);

  // Find a handling BCI for the exception passed in thread->pending_exception
  // or return -1 if no handler in this method, or the stack still needs
  // unwinding (because of stack overflow).  If there is an exception thrown
  // during the lookup, the exception in pending_exception() can change.
  // Lookup can load classes and cause GC.
  static int find_exception_handler_method_bci(JavaThread *thread, methodHandle m, int bci);

  // Find the exception handler for compiled code, given the methodCode and a PC
  // which points into it.  Uses the above lookup if the built-in exception
  // cache fails, after converting the methodCode & pc into a method & bci.
  // Passes the exception in and out via pending_exception.
  static address find_exception_handler_in_methodCode_pc( JavaThread *thread, methodCodeOop nm, address ret_pc );

  // First uses the return_address to do a frame-type breakdown, then calls
  // one of the above methods for finding the handler.  Also handles various
  // special frames (deopt, JavaCall entry frames).
  static address find_exception_handler_for_return_address(JavaThread *thread, address return_address);

  // exception handling
static void build_StackOverflowError(JavaThread*thread);

  static address handle_array_index_check(JavaThread* thread, address failing_pc);

  static void record_memory_traces();

  // Allocate (GC), fill-in and throw an NPE.  Pass in and return
  // thread, so the caller does not have to save/restore it.
static JavaThread*throw_NPE(JavaThread*thread);

  // To be used as the entry point for unresolved native methods.
  static address native_method_throw_unsatisfied_link_error_entry();

  // bytecode tracing -- used only by TraceBytecodes
  static intptr_t trace_bytecode(JavaThread* thread, intptr_t preserve_this_value, intptr_t tos, intptr_t tos2) PRODUCT_RETURN0;

  
  // Used to back off a spin lock that is under heavy contention
  static void yield_all(JavaThread* thread, int attempts = 0);

  static oop retrieve_receiver( symbolHandle sig, frame caller );  

  // arraycopy, the non-leaf version.  (See StubRoutines for all the leaf calls.)
  static void slow_arraycopy_C(JavaThread *thread, arrayRef src, int src_pos, arrayRef dst, int dst_pos, int length);

  static void verify_caller_frame(frame caller_frame, methodHandle callee_method) PRODUCT_RETURN;
  static methodHandle find_callee_method_inside_interpreter(frame caller_frame, methodHandle caller_method, int bci) PRODUCT_RETURN_(return methodHandle(););

static objectRef register_finalizer(JavaThread*thread,objectRef obj);

/*
  static address get_resolve_opt_virtual_call_stub(){
    assert(_resolve_opt_virtual_call_blob != NULL, "oops");
    return _resolve_opt_virtual_call_blob->instructions_begin();
  }
  static address get_resolve_virtual_call_stub() {
    assert(_resolve_virtual_call_blob != NULL, "oops");
    return _resolve_virtual_call_blob->instructions_begin();
  }
  static address get_resolve_static_call_stub() {
    assert(_resolve_static_call_blob != NULL, "oops");
    return _resolve_static_call_blob->instructions_begin();
  }
*/

  // Helper routine for full-speed JVMTI exception throwing support
  static void throw_and_post_jvmti_exception(JavaThread *thread, Handle h_exception);
  static void throw_and_post_jvmti_exception(JavaThread *thread, symbolOop name, const char *message = NULL);

  static void jvmti_contended_monitor_enter(JavaThread* thread, ObjectMonitor* mon);
  static void jvmti_contended_monitor_entered(JavaThread* thread, ObjectMonitor* mon);

  /**
   * Fill in the "X cannot be cast to a Y" message for ClassCastException
   * 
   * @param thr the current thread
   * @param name the name of the class of the object attempted to be cast
   * @return the dynamically allocated exception message (must be freed
   * by the caller using a resource mark)  
   *
   * BCP must refer to the current 'checkcast' opcode for the frame 
   * on top of the stack.  
   * The caller (or one of it's callers) must use a ResourceMark 
   * in order to correctly free the result.
   */
  static char* generate_class_cast_message(JavaThread* thr, const char* name);

  /**
   * Fill in the "X cannot be cast to a Y" message for ClassCastException
   * 
   * @param name the name of the class of the object attempted to be cast
   * @param klass the name of the target klass attempt
   * @return the dynamically allocated exception message (must be freed
   * by the caller using a resource mark)  
   *
   * This version does not require access the frame, so it can be called
   * from interpreted code
   * The caller (or one of it's callers) must use a ResourceMark 
   * in order to correctly free the result.
   */
  static char* generate_class_cast_message(const char* name, const char* klass);

  // OSR support
static intptr_t*OSR_migration_begin(JavaThread*thread,int active_monitor_count);
  static void      OSR_migration_end  ( intptr_t* buf, int max_locals, int active_monitor_count, int total_monitor_count, intptr_t *fp );

  // Slow-path Locking and Unlocking
  static void wait_for_monitor(JavaThread* thread, ObjectMonitor* mon);
static objectRef monitorenter(JavaThread*thread,objectRef obj);
static void monitorexit(oop obj);
static int _az_mon_enter_ctr;//monitor enter slow
static int _az_mon_exit_ctr;//monitor exit slow
  static int _mon_wait_ctr;	// monitor wait slow
  static int _throw_null_ctr;	// throwing a null-pointer exception
  static int _throw_range_ctr;	// throwing a range-check  exception
  static void print_statistics();
  static void print_statistics(xmlBuffer* xb);
  static int  _arraycopy_stats[100]; // Counts of arraycopy sizes
  static void collect_arraycopy_stats( int len );
  static void print_arraycopy_stats();
  static void print_arraycopy_stats(xmlBuffer *xb);
 
  // very involved method in verifying that an oop is really an oop
  static bool verify_oop(objectRef oop);

  // Verify that an objectRef has a KID with a valid klassRef.
  static void verify_ref_klass(objectRef obj, int verify_id);

  // jvmti method entry/exit
  static void post_method_entry(JavaThread* thr);
  static void post_method_exit(JavaThread* thr, uint64_t return_value);

  // SBA: An STA just did an SBA trap.  Escape the given value to the
  // given location's frame (or heap).
  static void sba_escape(JavaThread* thread, stackRef value, objectRef base);

  // Bare-naked heap allocation of a given size.  It doesn't fit in the
  // current thread's tlab, so we likely need a tlab expansion as well.  
  // Stuff the klass and mark words down only, plus zero the rest.  No
  // length-setting if this is an array.  size_in_bytes is pre-rounded.
  static objectRef _new(JavaThread *thread, int KID, intptr_t size_in_bytes, jint len, intptr_t sba_hint);


  // TODO: this comment needs revisiting as we're now 8 bytes in size by default
  // Read the array of BasicTypes from a Java signature, and compute where
  // compiled Java code would like to put the results.  Values in reg_lo and
  // reg_hi refer to 4-byte quantities.  Values less than SharedInfo::stack0 are
  // registers, those above refer to 4-byte stack slots.  All stack slots are
  // based off of the window top.  SharedInfo::stack0 refers to the first slot
  // past the 16-word window, and SharedInfo::stack0+1 refers to the memory word
  // 4-bytes higher.
  static ByteSize java_calling_convention(const BasicType *sig_bt, VReg::VR *regs, int total_args_passed, bool is_outgoing);
  static ByteSize c_calling_convention(const BasicType *sig_bt, VReg::VR *regs, int total_args_passed, bool is_outgoing);
};

#endif // SHAREDRUNTIME_HPP
