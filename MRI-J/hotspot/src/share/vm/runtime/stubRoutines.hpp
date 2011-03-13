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
#ifndef STUBROUTINES_HPP
#define STUBROUTINES_HPP

#include "codeBlob.hpp"
#include "methodOop.hpp"

// StubRoutines provides entry points to assembly routines used by
// compiled code and the run-time system. Platform-specific entry
// points are defined in the platform-specific inner class.
//
// Class scheme:
//
//    platform-independent               platform-dependent
//
//    stubRoutines.hpp  <-- included --  stubRoutines_<arch>.hpp
//           ^                                  ^
//           |                                  |
//       implements                         implements
//           |                                  |
//           |                                  |
//    stubRoutines.cpp                   stubRoutines_<arch>.cpp
//    stubRoutines_<os_family>.cpp       stubGenerator_<arch>.cpp
//    stubRoutines_<os_arch>.cpp
//
// Note 1: The important thing is a clean decoupling between stub
//         entry points (interfacing to the whole vm; i.e., 1-to-n
//         relationship) and stub generators (interfacing only to
//         the entry points implementation; i.e., 1-to-1 relationship).
//         This significantly simplifies changes in the generator
//         structure since the rest of the vm is not affected.
//
// Note 2: stubGenerator_<arch>.cpp contains a minimal portion of
//         machine-independent code; namely the generator calls of
//         the generator functions that are used platform-independently.
//         However, it comes with the advantage of having a 1-file
//         implementation of the generator. It should be fairly easy
//         to change, should it become a problem later.
//
// Scheme for adding a new entry point:
//
// 1. determine if it's a platform-dependent or independent entry point
//    a) if platform independent: make subsequent changes in the independent files
//    b) if platform   dependent: make subsequent changes in the   dependent files
// 2. add a private instance variable holding the entry point address
// 3. add a public accessor function to the instance variable
// 4. implement the corresponding generator function in the platform-dependent
//    stubGenerator_<arch>.cpp file and call the function in generate_all() of that file


class StubRoutines: AllStatic {

 public:
  enum platform_independent_constants {
    max_size_of_parameters = 256                           // max. parameter size supported by megamorphic lookups
  };

  // Dependencies
  friend class StubGenerator;
  #include "stubRoutines_pd.hpp"               // machine-specific parts

  static address _verify_oop_subroutine_entry;
static address _verify_ref_klass_subroutine_entry;

  static address _call_stub_return_address;                // the return PC, when returning to a call stub
  static address _call_stub_entry;  
static address _resolve_and_patch_call_entry;
static address _lazy_c2i_entry;
  static address _forward_exception_entry;
static address _forward_exception_fat16_entry;
static address _forward_exception_fat256_entry;
static address _c2_internal_rethrows;
static address _illegal_jump_target;
  static address _throw_AbstractMethodError_entry;
  static address _throw_IncompatibleClassChangeError_entry;
  static address _throw_ArithmeticException_entry;
  static address _throw_NullPointerException_entry;
  static address _throw_NullPointerException_at_call_entry;
  static address _throw_StackOverflowError_entry;
  static address _handler_for_unsafe_access_entry;
static address _new_fast;
static address _new_fast_array;
static address _new_sba;
static address _new_sba_array;
static address _register_finalizer_entry;
static address _safepoint_trap_handler;
static address _uncommon_trap_entry;
static address _deopt_asm_entry;
static address _promotion_patch_entry;

static address _memcpy;

static address _recordLD8;
static address _recordST8;
static address _recordCAS8;
static address _recordPrefOther;
static address _recordPrefCLZ;

  static address _lock_entry;  // try-lock: Various almost-fast attempts.  No blocking.
static address _unlock_entry;
static address _unlock_c2_entry;

  static address _d2i_wrapper;
  static address _d2l_wrapper;
  static address _mul_64_64_hi; // Signed 64x64 multiply, returning the HIGH 64 bits

  static jint    _fpu_cntrl_wrd_std;
  static jint    _fpu_cntrl_wrd_24;
  static jint    _fpu_cntrl_wrd_64;
  static jint    _fpu_cntrl_wrd_trunc;
  static jint    _mxcsr_std;
  static jint    _fpu_subnormal_bias1[3];
  static jint    _fpu_subnormal_bias2[3];

static CodeBlob*_code1;//code buffer for initial routines
static CodeBlob*_code2;//code buffer for all other routines
  static bool _should_install_instrumented_stubs;          // stubs for JVMTI/JVMPI monitor entry/exit

  // Leaf routines which implement arraycopy and their addresses
  // arraycopy operands aligned on element type boundary
  static address _jbyte_arraycopy;
  static address _jshort_arraycopy;
  static address _jint_arraycopy;
  static address _jlong_arraycopy;
static address _objectRef_arraycopy;
  static address _jbyte_disjoint_arraycopy;
  static address _jshort_disjoint_arraycopy;
  static address _jint_disjoint_arraycopy;
  static address _jlong_disjoint_arraycopy;
static address _objectRef_disjoint_arraycopy;

  // arraycopy operands aligned on zero'th element boundary
  // These are identical to the ones aligned aligned on an
  // element type boundary, except that they assume that both
  // source and destination are HeapWord aligned.
  static address _arrayof_jbyte_arraycopy;
  static address _arrayof_jshort_arraycopy;
  static address _arrayof_jint_arraycopy;
  static address _arrayof_jlong_arraycopy;
static address _arrayof_objectRef_arraycopy;
  static address _arrayof_jbyte_disjoint_arraycopy;
  static address _arrayof_jshort_disjoint_arraycopy;
  static address _arrayof_jint_disjoint_arraycopy;
  static address _arrayof_jlong_disjoint_arraycopy;
static address _arrayof_objectRef_disjoint_arraycopy;

  // these are recommended but optional:
static address _object_arraycopy;
  static address _checkcast_arraycopy;
  static address _unsafe_arraycopy;
  static address _generic_arraycopy;

 public:
  // Initialization/Testing
  static void    initialize1();                            // must happen before universe::genesis
  static void    initialize2();                            // must happen after  universe::genesis
 
  // JVMTI/JVMPI 
  static void    set_should_install_instrumented_stubs(bool on) {_should_install_instrumented_stubs = on; }
  static bool    should_install_instrumented_stubs() { return _should_install_instrumented_stubs; }

  static bool contains(address addr) {
    return
(_code1!=NULL&&_code1->contains(addr))||
(_code2!=NULL&&_code2->contains(addr));
  }

  // a subroutine for debugging the GC
  static address verify_oop_subroutine_entry_address()     { return (address)&_verify_oop_subroutine_entry; }
  static address verify_oop_subroutine_entry()             { return _verify_oop_subroutine_entry; }
  static address verify_ref_klass_subroutine_entry()       { return _verify_ref_klass_subroutine_entry; }

  // Calls to Java
  typedef void (*CallStub)(
    address   link,
    intptr_t* result,
    BasicType result_type,
    methodRef method,
    address   entry_point,
    intptr_t* parameters,
    int       size_of_parameters,
    TRAPS
  );

  static CallStub call_stub()                              { return CAST_TO_FN_PTR(CallStub, _call_stub_entry); }

  // Platform-specific stub that saves argument registers and has a GC
  // map and calls the above SharedRuntime::resolve_and_patch_call routine.
  static address resolve_and_patch_call_entry()            { return _resolve_and_patch_call_entry; }
  static address lazy_c2i_entry()                          { return _lazy_c2i_entry; }

  // Exceptions
  static address forward_exception_entry()                 { return _forward_exception_entry; }
  static void set_forward_exception_fat16_entry(int64_t x) { *(int32_t *)_forward_exception_fat16_entry = (int32_t)x; }
  static void set_forward_exception_fat256_entry(int64_t x){ *(int32_t *)_forward_exception_fat256_entry = (int32_t)x; }
  static address forward_exception_entry2()                { return _c2_internal_rethrows; }
  static address illegal_jump_target()                     { return _illegal_jump_target; }
  // Implicit exceptions
  static address throw_AbstractMethodError_entry()         { return _throw_AbstractMethodError_entry; }
  static address throw_IncompatibleClassChangeError_entry(){ return _throw_IncompatibleClassChangeError_entry; }
  static address throw_ArithmeticException_entry()         { return _throw_ArithmeticException_entry; }
  static address throw_NullPointerException_entry()        { return _throw_NullPointerException_entry; }
  static address throw_NullPointerException_at_call_entry(){ return _throw_NullPointerException_at_call_entry; }
  static address throw_StackOverflowError_entry()          { return _throw_StackOverflowError_entry; }
  

  // New-allocation routines.  Slow-path is for finalizers.
  static address new_fast()                                { return _new_fast; }
  static address new_fast_array()                          { return _new_fast_array; }
  static address new_sba()                                 { return _new_sba; }
  static address new_sba_array()                           { return _new_sba_array; }
  static address register_finalizer_entry()                { return _register_finalizer_entry; }

  static address safepoint_trap_handler()                  { return _safepoint_trap_handler; } 
  static address uncommon_trap_entry()                     { return _uncommon_trap_entry; }
  static address deopt_asm_entry()                         { return _deopt_asm_entry; }
  static address promotion_patch_entry()                   { return _promotion_patch_entry; }

  static address memcpy()                                  { return _memcpy; }

  // Exceptions during unsafe access - should throw Java exception rather
  // than crash.
  static address handler_for_unsafe_access()               { return _handler_for_unsafe_access_entry; }
  typedef void (*RecordLD8)(int64_t link);
  typedef void (*RecordST8)(int64_t link);
  typedef void (*RecordCAS8)(int64_t link);
  typedef void (*RecordPrefOther)(int64_t link);
  typedef void (*RecordPrefCLZ)(int64_t link);
  static RecordLD8 recordLD8()                             { return CAST_TO_FN_PTR(RecordLD8,_recordLD8); }
  static RecordST8 recordST8()                             { return CAST_TO_FN_PTR(RecordST8,_recordST8); }
  static RecordCAS8 recordCAS8()                           { return CAST_TO_FN_PTR(RecordCAS8,_recordCAS8); }
  static RecordPrefOther recordPrefOther()                 { return CAST_TO_FN_PTR(RecordPrefOther,_recordPrefOther); }
  static RecordPrefCLZ recordPrefCLZ()                     { return CAST_TO_FN_PTR(RecordPrefCLZ,_recordPrefCLZ); }

  static address lock_entry()                              { return _lock_entry; }
  static address unlock_entry()                            { return _unlock_entry; }
  static address unlock_c2_entry()                         { return _unlock_c2_entry; }

  //static address monitorenter_instrumented_entry()         { return _monitorenter_instrumented_entry; }
  //static address monitorexit_instrumented_entry()          { return _monitorexit_instrumented_entry; }

  static address d2i_wrapper()                             { return _d2i_wrapper; }
  static address d2l_wrapper()                             { return _d2l_wrapper; }
  static address mul_64_64_hi()                            { return _mul_64_64_hi; }
  static address addr_fpu_cntrl_wrd_std()                  { return (address)&_fpu_cntrl_wrd_std;   }
  static address addr_fpu_cntrl_wrd_24()                   { return (address)&_fpu_cntrl_wrd_24;   }
  static address addr_fpu_cntrl_wrd_64()                   { return (address)&_fpu_cntrl_wrd_64;   }
  static address addr_fpu_cntrl_wrd_trunc()                { return (address)&_fpu_cntrl_wrd_trunc; }
  static address addr_mxcsr_std()                          { return (address)&_mxcsr_std; }
  static address addr_fpu_subnormal_bias1()                { return (address)&_fpu_subnormal_bias1; }
  static address addr_fpu_subnormal_bias2()                { return (address)&_fpu_subnormal_bias2; }


  static address jbyte_arraycopy()  { return _jbyte_arraycopy; }
  static address jshort_arraycopy() { return _jshort_arraycopy; }
  static address jint_arraycopy()   { return _jint_arraycopy; }
  static address jlong_arraycopy()  { return _jlong_arraycopy; }
  static address objectRef_arraycopy()    { return _objectRef_arraycopy; }
  static address jbyte_disjoint_arraycopy()  { return _jbyte_disjoint_arraycopy; }
  static address jshort_disjoint_arraycopy() { return _jshort_disjoint_arraycopy; }
  static address jint_disjoint_arraycopy()   { return _jint_disjoint_arraycopy; }
  static address jlong_disjoint_arraycopy()  { return _jlong_disjoint_arraycopy; }
  static address objectRef_disjoint_arraycopy()    { return _objectRef_disjoint_arraycopy; }

  static address arrayof_jbyte_arraycopy()  { return _arrayof_jbyte_arraycopy; }
  static address arrayof_jshort_arraycopy() { return _arrayof_jshort_arraycopy; }
  static address arrayof_jint_arraycopy()   { return _arrayof_jint_arraycopy; }
  static address arrayof_jlong_arraycopy()  { return _arrayof_jlong_arraycopy; }
  static address arrayof_objectRef_arraycopy()    { return _arrayof_objectRef_arraycopy; }

  static address arrayof_jbyte_disjoint_arraycopy()  { return _arrayof_jbyte_disjoint_arraycopy; }
  static address arrayof_jshort_disjoint_arraycopy() { return _arrayof_jshort_disjoint_arraycopy; }
  static address arrayof_jint_disjoint_arraycopy()   { return _arrayof_jint_disjoint_arraycopy; }
  static address arrayof_jlong_disjoint_arraycopy()  { return _arrayof_jlong_disjoint_arraycopy; }
  static address arrayof_objectRef_disjoint_arraycopy()    { return _arrayof_objectRef_disjoint_arraycopy; }

  static address object_arraycopy()        { return _object_arraycopy; }
  static address checkcast_arraycopy()     { return _checkcast_arraycopy; }
  static address unsafe_arraycopy()        { return _unsafe_arraycopy; }
  static address generic_arraycopy()       { return _generic_arraycopy; }

  //
  // Default versions of the above arraycopy functions for platforms which do
  // not have specialized versions
  //
static void jbyte_copy(jbyte*dest,jbyte*src,size_t count);
static void jshort_copy(jshort*dest,jshort*src,size_t count);
static void jint_copy(jint*dest,jint*src,size_t count);
static void jlong_copy(jlong*dest,jlong*src,size_t count);
static void objectRef_copy(objectRef*dest,objectRef*src,size_t count);

static void arrayof_jbyte_copy(HeapWord*dest,HeapWord*src,size_t count);
static void arrayof_jshort_copy(HeapWord*dest,HeapWord*src,size_t count);
static void arrayof_jint_copy(HeapWord*dest,HeapWord*src,size_t count);
static void arrayof_jlong_copy(HeapWord*dest,HeapWord*src,size_t count);
static void arrayof_objectRef_copy(HeapWord*dest,HeapWord*src,size_t count);

  // Compute corrected issuing pc from an exception pc.
  static address find_NPE_continuation_address(JavaThread *thread, address faulting_pc);
};
#endif // STUBROUTINES_HPP
