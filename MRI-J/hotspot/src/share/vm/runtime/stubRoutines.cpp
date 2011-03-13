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


#include "copy.hpp"
#include "collectedHeap.hpp"
#include "barrierSet.hpp"
#include "interfaceSupport.hpp"
#include "sharedRuntime.hpp"
#include "stubRoutines.hpp"
#include "resourceArea.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"
#include "barrierSet.inline.hpp"

// Implementation of StubRoutines - for a description
// of how to extend it, see the header file.

// Class Variables

CodeBlob*StubRoutines::_code1=NULL;
CodeBlob*StubRoutines::_code2=NULL;

address StubRoutines::_call_stub_return_address                 = NULL;
address StubRoutines::_call_stub_entry                          = NULL;

address StubRoutines::_resolve_and_patch_call_entry=NULL;
address StubRoutines::_lazy_c2i_entry=NULL;

address StubRoutines::_forward_exception_entry                  = NULL;
address StubRoutines::_forward_exception_fat16_entry=NULL;
address StubRoutines::_forward_exception_fat256_entry=NULL;
address StubRoutines::_c2_internal_rethrows=NULL;
address StubRoutines::_illegal_jump_target=NULL;
address StubRoutines::_throw_AbstractMethodError_entry          = NULL;
address StubRoutines::_throw_IncompatibleClassChangeError_entry = NULL;
address StubRoutines::_throw_ArithmeticException_entry          = NULL;
address StubRoutines::_throw_NullPointerException_entry         = NULL;
address StubRoutines::_throw_NullPointerException_at_call_entry = NULL;
address StubRoutines::_throw_StackOverflowError_entry           = NULL;
address StubRoutines::_handler_for_unsafe_access_entry          = NULL;
address StubRoutines::_new_fast=NULL;
address StubRoutines::_new_fast_array=NULL;
address StubRoutines::_new_sba=NULL;
address StubRoutines::_new_sba_array=NULL;
address StubRoutines::_register_finalizer_entry=NULL;
address StubRoutines::_verify_oop_subroutine_entry              = NULL;
address StubRoutines::_verify_ref_klass_subroutine_entry=NULL;
address StubRoutines::_safepoint_trap_handler=NULL;
address StubRoutines::_uncommon_trap_entry=NULL;
address StubRoutines::_deopt_asm_entry=NULL;
address StubRoutines::_promotion_patch_entry=NULL;

address StubRoutines::_memcpy=NULL;

address StubRoutines::_recordLD8=NULL;
address StubRoutines::_recordST8=NULL;
address StubRoutines::_recordCAS8=NULL;
address StubRoutines::_recordPrefCLZ=NULL;
address StubRoutines::_recordPrefOther=NULL;

address StubRoutines::_lock_entry=NULL;
address StubRoutines::_unlock_entry=NULL;
address StubRoutines::_unlock_c2_entry=NULL;

address StubRoutines::_d2i_wrapper                              = NULL;
address StubRoutines::_d2l_wrapper                              = NULL;
address StubRoutines::_mul_64_64_hi=NULL;

jint    StubRoutines::_fpu_cntrl_wrd_std                        = 0;
jint    StubRoutines::_fpu_cntrl_wrd_24                         = 0;
jint    StubRoutines::_fpu_cntrl_wrd_64                         = 0;
jint    StubRoutines::_fpu_cntrl_wrd_trunc                      = 0;
jint    StubRoutines::_mxcsr_std                                = 0;
jint    StubRoutines::_fpu_subnormal_bias1[3]                   = { 0, 0, 0 };
jint    StubRoutines::_fpu_subnormal_bias2[3]                   = { 0, 0, 0 };

bool    StubRoutines::_should_install_instrumented_stubs        = false;

// Compiled code entry points default values
// The dafault functions don't have separate disjoint versions.
address StubRoutines::_jbyte_arraycopy          = CAST_FROM_FN_PTR(address, StubRoutines::jbyte_copy);
address StubRoutines::_jshort_arraycopy         = CAST_FROM_FN_PTR(address, StubRoutines::jshort_copy);
address StubRoutines::_jint_arraycopy           = CAST_FROM_FN_PTR(address, StubRoutines::jint_copy);
address StubRoutines::_jlong_arraycopy          = CAST_FROM_FN_PTR(address, StubRoutines::jlong_copy);
address StubRoutines::_objectRef_arraycopy=CAST_FROM_FN_PTR(address,StubRoutines::objectRef_copy);
address StubRoutines::_jbyte_disjoint_arraycopy          = CAST_FROM_FN_PTR(address, StubRoutines::jbyte_copy);
address StubRoutines::_jshort_disjoint_arraycopy         = CAST_FROM_FN_PTR(address, StubRoutines::jshort_copy);
address StubRoutines::_jint_disjoint_arraycopy           = CAST_FROM_FN_PTR(address, StubRoutines::jint_copy);
address StubRoutines::_jlong_disjoint_arraycopy          = CAST_FROM_FN_PTR(address, StubRoutines::jlong_copy);
address StubRoutines::_objectRef_disjoint_arraycopy=CAST_FROM_FN_PTR(address,StubRoutines::objectRef_copy);

address StubRoutines::_arrayof_jbyte_arraycopy  = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jbyte_copy);
address StubRoutines::_arrayof_jshort_arraycopy = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jshort_copy);
address StubRoutines::_arrayof_jint_arraycopy   = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jint_copy);
address StubRoutines::_arrayof_jlong_arraycopy  = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jlong_copy);
address StubRoutines::_arrayof_objectRef_arraycopy=CAST_FROM_FN_PTR(address,StubRoutines::arrayof_objectRef_copy);
address StubRoutines::_arrayof_jbyte_disjoint_arraycopy  = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jbyte_copy);
address StubRoutines::_arrayof_jshort_disjoint_arraycopy = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jshort_copy);
address StubRoutines::_arrayof_jint_disjoint_arraycopy   = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jint_copy);
address StubRoutines::_arrayof_jlong_disjoint_arraycopy  = CAST_FROM_FN_PTR(address, StubRoutines::arrayof_jlong_copy);
address StubRoutines::_arrayof_objectRef_disjoint_arraycopy=CAST_FROM_FN_PTR(address,StubRoutines::arrayof_objectRef_copy);

address StubRoutines::_object_arraycopy=NULL;
address StubRoutines::_checkcast_arraycopy               = NULL;
address StubRoutines::_unsafe_arraycopy                  = NULL;
address StubRoutines::_generic_arraycopy                 = NULL;

// Initialization
//
// Note: to break cycle with universe initialization, stubs are generated in two phases.
// The first one generates stubs needed during universe init (e.g., _handle_must_compile_first_entry).
// The second phase includes all other stubs (which may depend on universe being initialized.)

extern CodeBlob *StubGenerator_generate(bool all); // only interface to generators
extern void StubGenerator_install_instrumented_stubs();

void StubRoutines::initialize1() {
  if (_code1 == NULL) {
    ResourceMark rm;
    TraceTime timer("StubRoutines generation 1", TraceStartupTime);
_code1=StubGenerator_generate(false);
  }
}


#ifdef ASSERT
typedef void (*arraycopy_fn)(address src, address dst, int count);

// simple tests of generated arraycopy functions
static void test_arraycopy_func(address func, int alignment) {
  int v = 0xcc;
  int v2 = 0x11;
  jlong lbuffer[2];
  jlong lbuffer2[2];
  address buffer  = (address) lbuffer;
  address buffer2 = (address) lbuffer2;
  unsigned int i;
  for (i = 0; i < sizeof(lbuffer); i++) {
    buffer[i] = v; buffer2[i] = v2;
  }
  // do an aligned copy
  ((arraycopy_fn)func)(buffer, buffer2, 0);
  for (i = 0; i < sizeof(lbuffer); i++) {
    assert(buffer[i] == v && buffer2[i] == v2, "shouldn't have copied anything");
  }
  // adjust destination alignment
  ((arraycopy_fn)func)(buffer, buffer2 + alignment, 0);
  for (i = 0; i < sizeof(lbuffer); i++) {
    assert(buffer[i] == v && buffer2[i] == v2, "shouldn't have copied anything");
  }
  // adjust source alignment
  ((arraycopy_fn)func)(buffer + alignment, buffer2, 0);
  for (i = 0; i < sizeof(lbuffer); i++) {
    assert(buffer[i] == v && buffer2[i] == v2, "shouldn't have copied anything");
  }
}
#endif


void StubRoutines::initialize2() {
  if (_code2 == NULL) {
    ResourceMark rm;
    TraceTime timer("StubRoutines generation 2", TraceStartupTime);
_code2=StubGenerator_generate(true);
  }

#ifdef ASSERT

  // Confusingly, all the arraycopy stubs assert against zero... so
  // these tests can never run.
//#define TEST_ARRAYCOPY(type)                                                    \
//  test_arraycopy_func(          type##_arraycopy(),          sizeof(type));     \
//  test_arraycopy_func(          type##_disjoint_arraycopy(), sizeof(type));     \
//  test_arraycopy_func(arrayof_##type##_arraycopy(),          sizeof(HeapWord)); \
//  test_arraycopy_func(arrayof_##type##_disjoint_arraycopy(), sizeof(HeapWord))
//
//  // Make sure all the arraycopy stubs properly handle zeros
//  TEST_ARRAYCOPY(jbyte);
//  TEST_ARRAYCOPY(jshort);
//  TEST_ARRAYCOPY(jint);
//  TEST_ARRAYCOPY(jlong);
//
//#undef TEST_ARRAYCOPY

#endif

  if (should_install_instrumented_stubs()) {
    StubGenerator_install_instrumented_stubs();
  }
}


void stubRoutines_init1() { StubRoutines::initialize1(); }
void stubRoutines_init2() { StubRoutines::initialize2(); }

//
// Default versions of arraycopy functions
//

static void gen_arraycopy_barrier(objectRef *dest, size_t count) {
  assert(count != 0, "count should be non-zero");
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->has_write_ref_array_opt(), "Barrier set must have ref array opt");
  bs->write_ref_array(MemRegion((HeapWord*)dest, (HeapWord*)(dest + count)));
}

JRT_LEAF(void, StubRoutines, jbyte_copy, (jbyte* dest, jbyte* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jbyte_array_copy_ctr++;      // Slow-path byte array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::conjoint_bytes_atomic(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, jshort_copy, (jshort* dest, jshort* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jshort_array_copy_ctr++;     // Slow-path short/char array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::conjoint_jshorts_atomic(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, jint_copy, (jint* dest, jint* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jint_array_copy_ctr++;       // Slow-path int/float array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::conjoint_jints_atomic(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, jlong_copy, (jlong* dest, jlong* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jlong_array_copy_ctr++;      // Slow-path long/double array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::conjoint_jlongs_atomic(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, objectRef_copy, (objectRef *dest, objectRef *src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_oop_array_copy_ctr++;        // Slow-path oop array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
Copy::conjoint_objectRefs_atomic(src,dest,count);
  gen_arraycopy_barrier(dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, arrayof_jbyte_copy, (HeapWord* dest, HeapWord* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jbyte_array_copy_ctr++;      // Slow-path byte array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::arrayof_conjoint_bytes(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, arrayof_jshort_copy, (HeapWord* dest, HeapWord* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jshort_array_copy_ctr++;     // Slow-path short/char array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::arrayof_conjoint_jshorts(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, arrayof_jint_copy, (HeapWord* dest, HeapWord* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jint_array_copy_ctr++;       // Slow-path int/float array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::arrayof_conjoint_jints(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, arrayof_jlong_copy, (HeapWord* dest, HeapWord* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_jlong_array_copy_ctr++;       // Slow-path int/float array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
  Copy::arrayof_conjoint_jlongs(src, dest, count);
JRT_END

JRT_LEAF(void, StubRoutines, arrayof_objectRef_copy, (HeapWord* dest, HeapWord* src, size_t count))
#ifndef PRODUCT
  SharedRuntime::_oop_array_copy_ctr++;        // Slow-path oop array copy
#endif // !PRODUCT
  assert(count != 0, "count should be non-zero");
Copy::conjoint_objectRefs_atomic((objectRef*)src,(objectRef*)dest,count);
  gen_arraycopy_barrier((objectRef*) dest, count);
JRT_END
