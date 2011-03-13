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
#ifndef C1_RUNTIME1_HPP
#define C1_RUNTIME1_HPP


#include "codeBlob.hpp"
#include "klassOop.hpp"
#include "stubCodeGenerator.hpp"
#include "c1_MacroAssembler.hpp"

class JavaThread;
class C1StubGenerator;

// The Runtime1 holds all assembly stubs and VM
// runtime routines needed by code code generated
// by the Compiler1.

#define RUNTIME1_STUBS(stub, last_entry) \
  stub(unwind_exception)             \
  stub(forward_exception)            \
  stub(throw_range_check_failed)       /* throws ArrayIndexOutOfBoundsException */ \
  stub(throw_index_exception)          /* throws IndexOutOfBoundsException */ \
  stub(throw_div0_exception)         \
  stub(throw_null_pointer_exception) \
stub(frequency_counter_overflow_wrapper)\
stub(partial_subtype_check)\
  stub(register_finalizer)           \
  stub(new_instance)                 \
stub(new_array)\
  stub(new_multi_array)              \
  stub(handle_exception_nofpu)         /* optimized version that does not preserve fpu registers */ \
  stub(handle_exception)             \
  stub(throw_array_store_exception)  \
  stub(throw_class_cast_exception)   \
  stub(throw_incompatible_class_change_error)   \
  stub(slow_subtype_check)           \
  stub(monitorenter)                 \
  stub(monitorenter_nofpu)             /* optimized version that does not preserve fpu registers */ \
  stub(monitorexit)                  \
  stub(monitorexit_nofpu)              /* optimized version that does not preserve fpu registers */ \
  stub(access_field_patching)        \
  stub(load_klass_patching)          \
  stub(jvmti_exception_throw)        \
  stub(fpu2long_stub)                \
  last_entry(number_of_ids)

#define DECLARE_STUB_ID(x)       x ## _id ,
#define DECLARE_LAST_STUB_ID(x)  x
#define STUB_NAME(x)             #x " Runtime1 stub",
#define LAST_STUB_NAME(x)        #x " Runtime1 stub"

class Runtime1: public AllStatic {
  friend class LIRGenerator;
  friend class VMStructs;
 private:
  static int desired_max_code_buffer_size() {
    Unimplemented();
    return 0;
    
    // return (int) NMethodSizeLimit;  // default 256K or 512K
  }
  static int desired_max_constant_size() {
    Unimplemented();
    return 0;
    
    // return (int) NMethodSizeLimit / 10;  // about 25K
  }

  // Note: This buffers is allocated once at startup since allocation
  // for each compilation seems to be too expensive (at least on Intel
  // win32).
  //static BufferBlob* _buffer_blob;

 public:
  enum StubID {
    RUNTIME1_STUBS(DECLARE_STUB_ID, DECLARE_LAST_STUB_ID)
  };

  // statistics
#ifndef PRODUCT
  static int _new_multi_array_slowcase_cnt;
  static int _patch_code_slowcase_cnt;
  static int _throw_range_check_exception_count;
  static int _throw_index_exception_count;
  static int _throw_div0_exception_count;
  static int _throw_null_pointer_exception_count;
  static int _throw_class_cast_exception_count;
  static int _throw_incompatible_class_change_error_count;
  static int _throw_array_store_exception_count;
  static int _throw_count;
#endif

 private:
  static bool      _is_initialized;
  static address   _entries[number_of_ids];
  static const char* _blob_names[];

  // stub generation
static void generate_blob_for(C1StubGenerator*csg,StubID id);
static void generate_code_for(StubID id,C1_MacroAssembler*masm);
static void generate_exception_throw(C1_MacroAssembler*sasm,address target,bool has_argument);
static void generate_patching(C1_MacroAssembler*sasm,address target);

static void generate_stub_call(C1_MacroAssembler*sasm,Register result,address entry,
                                 Register arg1 = noreg, Register arg2 = noreg, Register arg3 = noreg);

  // runtime entry points
static objectRef new_instance(JavaThread*thread,klassRef klass);
static objectRef new_type_array(JavaThread*thread,klassRef klass,jint length);
static objectRef new_object_array(JavaThread*thread,klassRef klass,jint length);
static objectRef new_multi_array(JavaThread*thread,klassRef klass,int rank,jint*dims);

#ifdef TIERED
  static void counter_overflow(JavaThread* thread, int bci);
#endif // TIERED

  static void unimplemented_entry   (JavaThread* thread, StubID id);

  static address exception_handler_for_pc(JavaThread* thread);
  static void post_jvmti_exception_throw(JavaThread* thread);

  static void throw_range_check_exception(JavaThread* thread, int index);
  static void throw_index_exception(JavaThread* thread, int index);
  static void throw_div0_exception(JavaThread* thread);
  static void throw_null_pointer_exception(JavaThread* thread);
static void throw_class_cast_exception(JavaThread*thread,objectRef object);
  static void throw_incompatible_class_change_error(JavaThread* thread);
  static void throw_array_store_exception(JavaThread* thread);

  //static void monitorenter(JavaThread* thread, oopDesc* obj, BasicObjectLock* lock);
  //static void monitorexit (JavaThread* thread, BasicObjectLock* lock);

static void access_field_patching(JavaThread*thread);
  static int move_klass_patching(JavaThread* thread);

  static void patch_code(JavaThread* thread, StubID stub_id);

 public:
  // Promote c1 to c2
  static address frequency_counter_overflow(JavaThread* thread, address continue_in_c1);

  // initialization
  static bool is_initialized()                   { return _is_initialized; }
  static void initialize();
  static void initialize_pd();

  // stubs
static address entry_for(StubID id){
    assert(0 <= id && id < number_of_ids, "illegal stub id");
    if (!is_initialized()) initialize();
return _entries[id];
  }
  static const char* name_for (StubID id);
  static const char* name_for_address(address entry);
  static StubID contains(address); // -1 for a miss, or a StubID

  // method tracing
  static void trace_block_entry(jint block_id);

#ifndef PRODUCT
  static address throw_count_address()       { return (address)&_throw_count;       }
#endif

  static void print_statistics()                 PRODUCT_RETURN;
};

class C1StubGenerator:public StubCodeGenerator{
 public:
  C1StubGenerator() : StubCodeGenerator(new C1_MacroAssembler("C1 stubs", 0)) { }
  virtual C1_MacroAssembler* c1assembler() const { return (C1_MacroAssembler*)_masm; }
};

#endif // C1_RUNTIME1_HPP
