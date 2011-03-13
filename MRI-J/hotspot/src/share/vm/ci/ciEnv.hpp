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
#ifndef CIENV_HPP
#define CIENV_HPP


#include "allocation.hpp"
#include "bytecodes.hpp"
#include "ciClassList.hpp"
#include "ciInstanceKlass.hpp"
#include "ciMethod.hpp"
#include "dict.hpp"
#include "handles.hpp"
#include "freezeAndMelt.hpp"
#include "thread.hpp"

class CodeProfile;
class CompileTask;
class Label;
class MacroAssembler;
class ciInstance;

// ciEnv
//
// This class is the top level broker for requests from the compiler
// to the VM.

class AbstractCompiler;
class FreezeAndMelt;

class ciEnv : StackObj {
  CI_PACKAGE_ACCESS_TO

  friend class CompileBroker;

private:
  Arena*           _arena;       // Alias for _ciEnv_arena except in init_shared_objects()
  Arena            _ciEnv_arena;
  int              _system_dictionary_modification_counter;
  ciObjectFactory* _factory;
  const char*      _failure_reason;
  bool             _failure_retry_compile;
  bool             _failure_retry_compile_immediately;
  bool             _break_at_compile;
  int              _num_inlined_bytecodes;
  CompileTask*     _task;           // faster access to CompilerThread::task

  char* _name_buffer;
  int   _name_buffer_len;

  Dict* _FAM_new_nmoop;

  Dict* _FAM_ciik_to_extras;
  Dict* _FAM_cim_to_extras;

  // Distinguished instances of certain ciObjects..
  static ciObject*              _null_object_instance;
  static ciMethodKlass*         _method_klass_instance;
  static ciSymbolKlass*         _symbol_klass_instance;
  static ciKlassKlass*          _klass_klass_instance;
  static ciInstanceKlassKlass*  _instance_klass_klass_instance;
  static ciTypeArrayKlassKlass* _type_array_klass_klass_instance;
  static ciObjArrayKlassKlass*  _obj_array_klass_klass_instance;

  static ciInstanceKlass* _ArrayStoreException;
  static ciInstanceKlass* _Class;
  static ciInstanceKlass* _ClassCastException;
static ciInstanceKlass*_Cloneable;
  static ciInstanceKlass* _Object;
static ciInstanceKlass*_Serializable;
  static ciInstanceKlass* _Throwable;
  static ciInstanceKlass* _Thread;
  static ciInstanceKlass* _OutOfMemoryError;
  static ciInstanceKlass* _String;

  static ciSymbol*        _unloaded_cisymbol;
  static ciInstanceKlass* _unloaded_ciinstance_klass;
  static ciObjArrayKlass* _unloaded_ciobjarrayklass;
  static ciInstanceKlass**_array_ifaces;
static ciObject*_test_string;

  static jobject _ArrayIndexOutOfBoundsException_handle;
  static jobject _ArrayStoreException_handle;
  static jobject _ClassCastException_handle;

  static bool _dont_install_code;

  ciInstance* _NullPointerException_instance;
  ciInstance* _ArithmeticException_instance;
  ciInstance* _ArrayIndexOutOfBoundsException_instance;
  ciInstance* _ArrayStoreException_instance;
  ciInstance* _ClassCastException_instance;

  FreezeAndMelt* _fam;

  // Look up a klass by name from a particular class loader (the accessor's).
  // If require_local, result must be defined in that class loader, or NULL.
  // If !require_local, a result from remote class loader may be reported,
  // if sufficient class loader constraints exist such that initiating
  // a class loading request from the given loader is bound to return
  // the class defined in the remote loader (or throw an error).
  //
  // Return an unloaded klass if !require_local and no class at all is found.
  //
  // The CI treats a klass as loaded if it is consistently defined in
  // another loader, even if it hasn't yet been loaded in all loaders
  // that could potentially see it via delegation.
  ciKlass* get_klass_by_name(ciKlass* accessing_klass,
                             ciSymbol* klass_name,
                             bool require_local);
  ciKlass*   get_klass_by_kid( int kid ) const;

  // Constant pool access.
  ciKlass*   get_klass_by_index(ciInstanceKlass* loading_klass,
                                int klass_index,
                                bool& is_accessible);
  ciConstant get_constant_by_index(ciInstanceKlass* loading_klass,
                                   int constant_index);
  bool       is_unresolved_string(ciInstanceKlass* loading_klass,
                                   int constant_index) const;
  bool       is_unresolved_klass(ciInstanceKlass* loading_klass,
                                   int constant_index) const;
  ciField*   get_field_by_index(ciInstanceKlass* loading_klass,
                                int field_index);
  ciMethod*  get_method_by_index(ciInstanceKlass* loading_klass,
                                 int method_index, Bytecodes::Code bc);

  // Implementation methods for loading and constant pool access.
  ciKlass* get_klass_by_name_impl(ciKlass* accessing_klass,
                                  ciSymbol* klass_name,
                                  bool require_local);
  ciKlass*   get_klass_by_index_impl(ciInstanceKlass* loading_klass,
                                     int klass_index,
                                     bool& is_accessible);
  ciConstant get_constant_by_index_impl(ciInstanceKlass* loading_klass,
					int constant_index);
  bool       is_unresolved_string_impl (instanceKlass* loading_klass,
					int constant_index) const;
  bool       is_unresolved_klass_impl (instanceKlass* loading_klass,
					int constant_index) const;
  ciField*   get_field_by_index_impl(ciInstanceKlass* loading_klass,
				     int field_index);
  ciMethod*  get_method_by_index_impl(ciInstanceKlass* loading_klass,
				      int method_index, Bytecodes::Code bc);

  bool       check_klass_accessibility(ciKlass* accessing_klass,
				      klassOop resolved_klassOop);
  methodOop  lookup_method(instanceKlass*  accessor,
			   instanceKlass*  holder,
			   symbolOop       name,
			   symbolOop       sig,
			   Bytecodes::Code bc);

  // Get a ciObject from the object factory.  Ensures uniqueness
  // of ciObjects.
  ciObject* get_object(oop o) {
    if (o == NULL) {
      return _null_object_instance;
    } else {
      return _factory->get(o);
    }
  }

  ciSymbol* get_symbol_from_string(const char* str) {
if(str==NULL){
      return (ciSymbol*)_null_object_instance;
    } else {
      return _factory->get_symbol_from_string(str);
    }
  }

  ciMethod* get_method_from_handle(jobject method);

  ciInstance* get_or_create_exception(jobject& handle, symbolHandle name);

  // Get a ciMethod representing either an unfound method or
  // a method with an unloaded holder.  Ensures uniqueness of
  // the result.
  ciMethod* get_unloaded_method(ciInstanceKlass* holder,
                                ciSymbol*        name,
                                ciSymbol*        signature) {
    return _factory->get_unloaded_method(holder, name, signature);
  }

  // Get a ciKlass representing an unloaded klass.
  // Ensures uniqueness of the result.
  ciKlass* get_unloaded_klass(ciKlass* accessing_klass,
                              ciSymbol* name) {
    return _factory->get_unloaded_klass(accessing_klass, name, true);
  }

  // See if we already have an unloaded klass for the given name
  // or return NULL if not.
  ciKlass *check_get_unloaded_klass(ciKlass* accessing_klass, ciSymbol* name) {
    return _factory->get_unloaded_klass(accessing_klass, name, false);
  }

  // Get a ciReturnAddress corresponding to the given bci.
  // Ensures uniqueness of the result.
  ciReturnAddress* get_return_address(int bci) {
    return _factory->get_return_address(bci);
  }

  // General utility : get a buffer of some required length.
  // Used in symbol creation.
  char* name_buffer(int req_len);

  // Helper routine for determining the validity of a compilation
  // with respect to concurrent class loading.
bool check_for_system_dictionary_modification(MacroAssembler*masm);

public:
  ciEnv(FreezeAndMelt* fam, FAMPtr old_cim);
  ciEnv(CompileTask* task, int system_dictionary_modification_counter, FreezeAndMelt* fam);
  // Used only during initialization of the ci
  ciEnv(Arena* arena);
  ~ciEnv();

  ciInstanceKlassExtras* get_ciik_extras(ciInstanceKlass* ciik);
  ciMethodExtras*        get_cim_extras (const ciMethod* cim);

  // This is true if the compilation is not going to produce code.
  // (It is reasonable to retry failed compilations.)
  bool failing() { return _failure_reason != NULL; }

  // Reason this compilation is failing, such as "too many basic blocks".
  const char* failure_reason() { return _failure_reason; }

  bool failure_retry_compile() const { return _failure_retry_compile; }
  bool failure_retry_compile_immediately() const { return _failure_retry_compile_immediately; }

  bool break_at_compile() { return _break_at_compile; }
  void set_break_at_compile(bool z) { _break_at_compile = z; }

  // The compiler task which has created this env.
  // May be useful to find out compile_id, comp_level, etc.
  CompileTask* task() { return _task; }
  uint compile_id();  // task()->compile_id()

  // Register the result of a compilation.
  void register_method(ciMethod*       target,
                       int             entry_bci,
                       MacroAssembler* masm,
                       CodeProfile*    cp,
ByteSize frame_bytes,
Label&generic_exception_handler_rel_pc,
                       bool            has_unsafe_access);
  

  // Access to certain well known ciObjects.
  ciInstanceKlass* ArrayStoreException_klass() {
    return _ArrayStoreException;
  }
  ciInstanceKlass* Class_klass() {
    return _Class;
  }
  ciInstanceKlass* ClassCastException_klass() {
    return _ClassCastException;
  }
ciInstanceKlass*Cloneable_klass(){
    return _Cloneable;
  }
  ciInstanceKlass* Object_klass() {
    return _Object;
  }
ciInstanceKlass*Serializable_klass(){
    return _Serializable;
  }
  ciInstanceKlass* Throwable_klass() {
    return _Throwable;
  }
  ciInstanceKlass* Thread_klass() {
    return _Thread;
  }
  ciInstanceKlass* OutOfMemoryError_klass() {
    return _OutOfMemoryError;
  }
  ciInstanceKlass* String_klass() {
    return _String;
  }
  ciInstance* NullPointerException_instance() {
    assert(_NullPointerException_instance != NULL, "initialization problem");
    return _NullPointerException_instance;
  }
ciInstance*ArrayIndexOutOfBoundsException_instance(){
assert(_ArrayIndexOutOfBoundsException_instance!=NULL,"initialization problem");
    return _ArrayIndexOutOfBoundsException_instance;
  }
  ciInstance* ArithmeticException_instance() {
    assert(_ArithmeticException_instance != NULL, "initialization problem");
    return _ArithmeticException_instance;
  }

  // Lazy constructors:
  ciInstance* ArrayStoreException_instance();
  ciInstance* ClassCastException_instance();

  static ciSymbol* unloaded_cisymbol() {
    return _unloaded_cisymbol;
  }
  static ciObjArrayKlass* unloaded_ciobjarrayklass() {
    return _unloaded_ciobjarrayklass;
  }
  static ciInstanceKlass* unloaded_ciinstance_klass() {
    return _unloaded_ciinstance_klass;
  }
  static ciInstanceKlass** array_ifaces() {
    return _array_ifaces;
  }
  static ciObject* test_string() {
    return _test_string;
  }

  FreezeAndMelt* freeze_and_melt() {
    return _fam;
  }

  bool dont_install_code() {
    return  _dont_install_code;
  }

  static void set_dont_install_code(bool dont_install_code) {
    _dont_install_code = dont_install_code;
  }

  ciKlass*  find_system_klass(ciSymbol* klass_name);
  // Note:  To find a class from its name string, use ciSymbol::make,
  // but consider adding to vmSymbols.hpp instead.

  // converts the ciKlass* representing the holder of a method into a
  // ciInstanceKlass*.  This is needed since the holder of a method in
  // the bytecodes could be an array type.  Basically this converts
  // array types into java/lang/Object and other types stay as they are.
  static ciInstanceKlass* get_instance_klass_for_declared_method_holder(ciKlass* klass);

  // Return the machine-level offset of o, which must be an element of a.
  // This may be used to form constant-loading expressions in lieu of simpler encodings.
  int       array_element_offset_in_bytes(ciArray* a, ciObject* o);

  // Access to the compile-lifetime allocation arena.
  Arena*    arena() { return _arena; }

  // What is the current compilation environment?
static ciEnv*current(){return CompilerThread::current()->_env;}

  // Overload with current thread argument
  static ciEnv* current(Thread *thread) { assert0(thread->is_Compiler_thread()); return ((CompilerThread*)thread)->_env; }

  // Notice that a method has been inlined in the current compile;
  // used only for statistics.
  void notice_inlined_method(ciMethod* method);

  // Total number of bytecodes in inlined methods in this compile
  int num_inlined_bytecodes() const;

  // Check for changes to the system dictionary during compilation
  bool system_dictionary_modification_counter_changed();

  void record_failure(const char* reason, bool retry_compile, bool retry_compile_immediately);
  void record_method_not_compilable(const char* reason) { record_failure(reason,true,false); }
  void record_out_of_memory_failure() { record_failure("out of memory", false, false); }

  // Azul: oop table interface for compilers
  static int get_OopTable_index(jobject h);

  ciObject* get_new_dependencyOop(unsigned count);
};

#endif // CIENV_HPP
