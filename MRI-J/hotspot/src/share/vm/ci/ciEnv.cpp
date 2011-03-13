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


#include "assembler_pd.hpp"
#include "c1_globals.hpp"
#include "ciArray.hpp"
#include "ciConstantPoolCache.hpp"
#include "ciEnv.hpp"
#include "ciField.hpp"
#include "ciInstance.hpp"
#include "ciObjArrayKlass.hpp"
#include "codeProfile.hpp"
#include "compile.hpp"
#include "compileBroker.hpp"
#include "dict.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "linkResolver.hpp"
#include "objArrayKlass.hpp"
#include "oopFactory.hpp"
#include "reflection.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

// ciEnv
//
// This class is the top level broker for requests from the compiler
// to the VM.

ciObject*              ciEnv::_null_object_instance;
ciMethodKlass*         ciEnv::_method_klass_instance;
ciSymbolKlass*         ciEnv::_symbol_klass_instance;
ciKlassKlass*          ciEnv::_klass_klass_instance;
ciInstanceKlassKlass*  ciEnv::_instance_klass_klass_instance;
ciTypeArrayKlassKlass* ciEnv::_type_array_klass_klass_instance;
ciObjArrayKlassKlass*  ciEnv::_obj_array_klass_klass_instance;
 
ciInstanceKlass* ciEnv::_ArrayStoreException;
ciInstanceKlass* ciEnv::_Class;
ciInstanceKlass* ciEnv::_ClassCastException;
ciInstanceKlass*ciEnv::_Cloneable;
ciInstanceKlass* ciEnv::_Object;
ciInstanceKlass*ciEnv::_Serializable;
ciInstanceKlass* ciEnv::_Throwable;
ciInstanceKlass* ciEnv::_Thread;
ciInstanceKlass* ciEnv::_OutOfMemoryError;
ciInstanceKlass* ciEnv::_String;

ciSymbol*        ciEnv::_unloaded_cisymbol = NULL;
ciInstanceKlass* ciEnv::_unloaded_ciinstance_klass = NULL;
ciObjArrayKlass* ciEnv::_unloaded_ciobjarrayklass = NULL;
ciInstanceKlass**ciEnv::_array_ifaces = NULL;
ciObject*ciEnv::_test_string=NULL;

jobject ciEnv::_ArrayIndexOutOfBoundsException_handle = NULL;
jobject ciEnv::_ArrayStoreException_handle = NULL;
jobject ciEnv::_ClassCastException_handle = NULL;

bool ciEnv::_dont_install_code = false;

#ifndef PRODUCT
static bool firstEnv = true;
#endif /* PRODUCT */

// ------------------------------------------------------------------
// ciEnv::ciEnv
ciEnv::ciEnv(FreezeAndMelt* fam, FAMPtr old_cim) {
  _fam = fam;

  CompilerThread* current_thread = (CompilerThread*)Thread::current();
  current_thread->_env = this;

  _arena   = &_ciEnv_arena;

  FAMPtr old_ci_env = FAM->getOldPtr("((struct CompilerThread*)%p)->_env", FAM->thread());
  FAM->mapNewToOldPtr(this, old_ci_env);

  // Reconstruct the ciInstanceKlass
  FAMPtr old_ciik = FAM->getOldPtr("((struct ciMethod*)%p)->_holder", old_cim);
  ciInstanceKlass* ciik = new ciInstanceKlass(old_ciik);

  // Reconstruct the ciMethod
  ciMethod* cim = new ciMethod(old_cim);

  // FAM TODO: _debug_info may have to be reconstituted
  Unimplemented();
  // _oop_recorder = NULL;
  // _debug_info = NULL;

  //_failure_reason = FAM->getString("((struct ciEnv*)%p)->_failure_reason", old_ci_env);
  // Force _failure_reason to null
  _failure_reason = NULL;
  _break_at_compile = FAM->getInt("((struct ciEnv*)%p)->_break_at_compile", old_ci_env);

  _system_dictionary_modification_counter =  FAM->getInt("((struct ciEnv*)%p)->_system_dictionary_modification_counter", old_ci_env);
  _num_inlined_bytecodes =  FAM->getInt("((struct ciEnv*)%p)->_num_inlined_bytecodes", old_ci_env);
  
  // Temporary buffer for creating symbols and such.
  _name_buffer = FAM->getString("((struct ciEnv*)%p)->_name_buffer", old_ci_env);
  _name_buffer_len = FAM->getInt("((struct ciEnv*)%p)->_name_buffer_len", old_ci_env);

  FAMPtr old_ciof      = FAM->getOldPtr("((struct ciEnv*)%p)->_factory", old_ci_env);
  _factory = new (_arena) ciObjectFactory(_arena, old_ciof);
  cim->fixupFAMPointers();
  ciik->fixupFAMPointers();

  // At this point, we should have a mapping of all new->old pointers.

  _FAM_new_nmoop    = new (_arena) Dict(cmpkey,hashkey);
  // Go through dictionary and recover entries
  FAMPtr old_cinm_map = FAM->getOldPtr("((struct ciEnv*)%p)->_FAM_new_nmoop", old_ci_env);
  for( DictFAMI i(FAM, old_cinm_map); i.test(); ++i ) { 
    _FAM_new_nmoop->Insert(FAM->getNewFromOldPtr(i._key), FAM->getNewFromOldPtr(i._value));
  }

  _FAM_ciik_to_extras = new (_arena) Dict(cmpkey,hashkey);
  // Go through dictionary and recover entries
  FAMPtr old_ciik_map = FAM->getOldPtr("((struct ciEnv*)%p)->_FAM_ciik_to_extras", old_ci_env);
  for( DictFAMI i(FAM, old_ciik_map); i.test(); ++i ) { 
    _FAM_ciik_to_extras->Insert(FAM->getNewFromOldPtr(i._key), new (_arena) ciInstanceKlassExtras(i._value));
  }

  _FAM_cim_to_extras = new (_arena) Dict(cmpkey,hashkey);
  FAMPtr old_cim_map = FAM->getOldPtr("((struct ciEnv*)%p)->_FAM_cim_to_extras", old_ci_env);
  for( DictFAMI i(FAM, old_cim_map); i.test(); ++i ) { 
    _FAM_cim_to_extras->Insert(FAM->getNewFromOldPtr(i._key), new (_arena) ciMethodExtras(i._value));
  }


  // Now that the ciObjectFactory has been created, all ciObject new<->old mappings are available

  _NullPointerException_instance           = new ciInstance(FAM->getOldPtr("((struct ciEnv*)%p)->_NullPointerException_instance", old_ci_env));
  _NullPointerException_instance->fixupFAMPointers();

  _ArrayIndexOutOfBoundsException_instance = new ciInstance(FAM->getOldPtr("((struct ciEnv*)%p)->_ArrayIndexOutOfBoundsException_instance", old_ci_env));
  _ArrayIndexOutOfBoundsException_instance->fixupFAMPointers();

  _ArithmeticException_instance            = new ciInstance(FAM->getOldPtr("((struct ciEnv*)%p)->_ArithmeticException_instance", old_ci_env));
  _ArithmeticException_instance->fixupFAMPointers();

  _ArrayStoreException_instance            = new ciInstance(FAM->getOldPtr("((struct ciEnv*)%p)->_ArrayStoreException_instance", old_ci_env));
  _ArrayStoreException_instance->fixupFAMPointers();

  _ClassCastException_instance             = new ciInstance(FAM->getOldPtr("((struct ciEnv*)%p)->_ClassCastException_instance", old_ci_env));
  _ClassCastException_instance->fixupFAMPointers();
}

ciEnv::ciEnv(CompileTask* task, int system_dictionary_modification_counter, FreezeAndMelt* fam) {
  VM_ENTRY_MARK;

  // Set up ciEnv::current immediately, for the sake of ciObjectFactory, etc.
  thread->set_env(this);
  assert(ciEnv::current() == this, "sanity");

  _failure_reason = NULL;
#ifndef PRODUCT
  assert(!firstEnv, "not initialized properly");
#endif /* !PRODUCT */

  _system_dictionary_modification_counter = system_dictionary_modification_counter;
  _num_inlined_bytecodes = 0;
  assert(task == NULL || thread->task() == task, "sanity");
  _task = task;
  
  // Temporary buffer for creating symbols and such.
  _name_buffer = NULL;
  _name_buffer_len = 0;

  _arena   = &_ciEnv_arena;

  _FAM_new_nmoop    = new (_arena) Dict(cmpkey, hashkey, _arena);

  _FAM_ciik_to_extras = new (_arena) Dict(cmpkey, hashkey, _arena);
  _FAM_cim_to_extras  = new (_arena) Dict(cmpkey, hashkey, _arena);

  _factory = new (_arena) ciObjectFactory(_arena, 128);

  // Preload commonly referenced system ciObjects.

  // During VM initialization, these instances have not yet been created.
  // Assertions ensure that these instances are not accessed before
  // their initialization.

  oop o;
o=Universe::null_ptr_exception_instance();
  _NullPointerException_instance           = o ? get_object(o)->as_instance() : NULL;
o=Universe::aioob_exception_instance();
  _ArrayIndexOutOfBoundsException_instance = o ? get_object(o)->as_instance() : NULL;
  o = Universe::arithmetic_exception_instance();
_ArithmeticException_instance=o?get_object(o)->as_instance():NULL;
  _ArrayStoreException_instance = NULL;
  _ClassCastException_instance = NULL;
}

ciEnv::ciEnv(Arena* arena) {
  ASSERT_IN_VM;

  // Set up ciEnv::current immediately, for the sake of ciObjectFactory, etc.
  CompilerThread* current_thread = CompilerThread::current();
  assert(current_thread->env() == NULL, "must be");
  current_thread->set_env(this);
  assert(ciEnv::current() == this, "sanity");

  _failure_reason = NULL;
  _break_at_compile = false;
#ifndef PRODUCT
  assert(firstEnv, "must be first");
  firstEnv = false;
#endif /* !PRODUCT */

  _system_dictionary_modification_counter = 0;
  _num_inlined_bytecodes = 0;
  _task = NULL;
  
  // Temporary buffer for creating symbols and such.
  _name_buffer = NULL;
  _name_buffer_len = 0;

  _arena   = arena;
  _factory = new (_arena) ciObjectFactory(_arena, 128);

  // Preload commonly referenced system ciObjects.

  // During VM initialization, these instances have not yet been created.
  // Assertions ensure that these instances are not accessed before
  // their initialization.

  assert(Universe::is_fully_initialized(), "must be");

  oop o = Universe::null_ptr_exception_instance();
  assert(o != NULL, "should have been initialized");
  _NullPointerException_instance = get_object(o)->as_instance();
  o = Universe::arithmetic_exception_instance();
  assert(o != NULL, "should have been initialized");
  _ArithmeticException_instance = get_object(o)->as_instance();

  _ArrayIndexOutOfBoundsException_instance = NULL;
  _ArrayStoreException_instance = NULL;
  _ClassCastException_instance = NULL;
}


ciEnv::~ciEnv() {
}

ciInstanceKlassExtras* ciEnv::get_ciik_extras(ciInstanceKlass* ciik) {
  ciInstanceKlassExtras *extras = (ciInstanceKlassExtras*)(*_FAM_ciik_to_extras)[ciik];
if(extras==NULL){
    if (FAM) {
      ShouldNotReachHere();
    } else {
      extras = new (_arena) ciInstanceKlassExtras();
    }
    _FAM_ciik_to_extras->Insert(ciik, extras);
  }
  return extras;
}

ciMethodExtras* ciEnv::get_cim_extras(const ciMethod* cim) {
  ciMethodExtras *extras = (ciMethodExtras*)(*_FAM_cim_to_extras)[cim];
if(extras==NULL){
    if (FAM) {
      ShouldNotReachHere();
    } else {
      extras = new (_arena) ciMethodExtras();
    }
    _FAM_cim_to_extras->Insert(const_cast<ciMethod*>(cim), extras);
  }
  return extras;
}

// ------------------------------------------------------------------
// helper for lazy exception creation
ciInstance* ciEnv::get_or_create_exception(jobject& handle, symbolHandle name) {
  VM_ENTRY_MARK;
  if (handle == NULL) {
    // Cf. universe.cpp, creation of Universe::_null_ptr_exception_instance.
    klassOop k = SystemDictionary::find(name, Handle(), Handle(), THREAD);
    jobject objh = NULL;
    if (!HAS_PENDING_EXCEPTION && k != NULL) {
      oop obj = instanceKlass::cast(k)->allocate_permanent_instance(THREAD);
      if (!HAS_PENDING_EXCEPTION)
        objh = JNIHandles::make_global(obj);
    }
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
    } else {
      handle = objh;
    }
  }
  oop obj = JNIHandles::resolve(handle);
  return obj == NULL? NULL: get_object(obj)->as_instance();
}

// ------------------------------------------------------------------
// ciEnv::ArrayStoreException_instance, etc.

ciInstance* ciEnv::ArrayStoreException_instance() {
  if (_ArrayStoreException_instance == NULL) {
    _ArrayStoreException_instance
          = get_or_create_exception(_ArrayStoreException_handle,
          vmSymbolHandles::java_lang_ArrayStoreException());
  }
  return _ArrayStoreException_instance;
}
ciInstance* ciEnv::ClassCastException_instance() {
  if (_ClassCastException_instance == NULL) {
    _ClassCastException_instance
          = get_or_create_exception(_ClassCastException_handle,
          vmSymbolHandles::java_lang_ClassCastException());
  }
  return _ClassCastException_instance;
}

// ------------------------------------------------------------------
// ciEnv::get_method_from_handle.  This is used for top-level method compiles
ciMethod* ciEnv::get_method_from_handle(jobject method) {
  VM_ENTRY_MARK;
ciMethod*cim=get_object(JNIHandles::resolve(method))->as_method();
  cim->objectId();              // Force allocation & caching of objectId while already in the VM
  return cim;
}

// ------------------------------------------------------------------
// ciEnv::array_element_offset_in_bytes
int ciEnv::array_element_offset_in_bytes(ciArray* a_h, ciObject* o_h) {
  VM_ENTRY_MARK;
  objArrayOop a = (objArrayOop)a_h->get_oop();
  assert(a->is_objArray(), "");
  int length = a->length();
  oop o = o_h->get_oop();
  for (int i = 0; i < length; i++) {
    if (a->obj_at(i) == o)  return i;
  }
  return -1;
}


// ------------------------------------------------------------------
// ciEnv::check_klass_accessiblity
//
// Note: the logic of this method should mirror the logic of
// constantPoolOopDesc::verify_constant_pool_resolve.
bool ciEnv::check_klass_accessibility(ciKlass* accessing_klass,
				      klassOop resolved_klass) {
  if (accessing_klass == NULL || !accessing_klass->is_loaded()) {
    return true;
  }
  if (accessing_klass->is_obj_array()) {
    accessing_klass = accessing_klass->as_obj_array_klass()->base_element_klass();
  }
  if (!accessing_klass->is_instance_klass()) {
    return true;
  }

  if (resolved_klass->klass_part()->oop_is_objArray()) {
    // Find the element klass, if this is an array.
    resolved_klass = objArrayKlass::cast(resolved_klass)->bottom_klass();
  }
  if (resolved_klass->klass_part()->oop_is_instance()) {
    return Reflection::verify_class_access(accessing_klass->get_klassOop(),
					   resolved_klass,
					   true);
  }
  return true;
}

// ------------------------------------------------------------------
// ciEnv::get_klass_by_name_impl
// Checks for a prior unloaded klass as soon as it has the name, and
// will return that before doing any real lookups.
ciKlass* ciEnv::get_klass_by_name_impl(ciKlass* accessing_klass,
                                       ciSymbol* name,
                                       bool require_local) {
  ASSERT_IN_VM;
  EXCEPTION_CONTEXT;

  // Now we need to check the SystemDictionary
  symbolHandle sym(THREAD, name->get_symbolOop());
  if (sym->byte_at(0) == 'L' &&
    sym->byte_at(sym->utf8_length()-1) == ';') {
    // This is a name from a signature.  Strip off the trimmings.
    sym = oopFactory::new_symbol_handle(sym->as_utf8()+1,
                                        sym->utf8_length()-2,
                                        KILL_COMPILE_ON_FATAL_(_unloaded_ciinstance_klass));
    name = get_object(sym())->as_symbol();
  }

  // Check for prior unloaded klass.  The SystemDictionary's answers
  // can vary over time but the compiler needs consistency.
  ciKlass* unloaded_klass = check_get_unloaded_klass(accessing_klass, name);
  if (unloaded_klass != NULL) {
return require_local?NULL:unloaded_klass;
  }

  Handle loader(THREAD, (oop)NULL);
  Handle domain(THREAD, (oop)NULL);
  if (accessing_klass != NULL) {
    loader = Handle(THREAD, accessing_klass->loader());
    domain = Handle(THREAD, accessing_klass->protection_domain());
  }

  klassOop found_klass = require_local
    ? SystemDictionary::            find_instance_or_array_klass(sym, loader, domain, THREAD)
    : SystemDictionary::find_constrained_instance_or_array_klass(sym, loader,         THREAD);
  (KILL_COMPILE_ON_FATAL_(_unloaded_ciinstance_klass));

  if (found_klass)              // Found it.  Build a CI handle.
    return get_object(found_klass)->as_klass();

  // If we fail to find an array klass, look again for its element type.
  // The element type may be available either locally or via constraints.
  // In either case, if we can find the element type in the system dictionary,
  // we must build an array type around it.  The CI requires array klasses
  // to be loaded if their element klasses are loaded, except when memory
  // is exhausted.
  if (sym->byte_at(0) == '[' &&
      (sym->byte_at(1) == '[' || sym->byte_at(1) == 'L')) {
    // We have an unloaded array.
    // Build it on the fly if the element class exists.
symbolOop elem_sym=oopFactory::new_symbol(sym->as_utf8()+1,sym->utf8_length()-1,
KILL_COMPILE_ON_FATAL_(_unloaded_ciinstance_klass));
    // Get element ciKlass recursively.
ciKlass*elem_klass=get_klass_by_name_impl(accessing_klass,get_object(elem_sym)->as_symbol(),require_local);
if(elem_klass&&elem_klass->is_loaded())//Now make an array for it
      return ciObjArrayKlass::make_impl(elem_klass);
  }

  return require_local ? NULL
    // Not yet loaded into the VM, or not governed by loader constraints.
    // Make a CI representative for it.
:get_unloaded_klass(accessing_klass,name);
}

// ------------------------------------------------------------------
// ciEnv::get_klass_by_name
ciKlass* ciEnv::get_klass_by_name(ciKlass* accessing_klass,
                                  ciSymbol* klass_name,
                                  bool require_local) {
  GUARDED_VM_ENTRY(return get_klass_by_name_impl(accessing_klass,
                                                 klass_name,
                                                 require_local);)
}

// ------------------------------------------------------------------
// ciEnv::get_klass_by_kid
ciKlass* ciEnv::get_klass_by_kid(int kid) const {
  return _factory->get_klass_by_kid(kid);
}

// ------------------------------------------------------------------
// ciEnv::get_klass_by_index_impl
//
// Implementation of get_klass_by_index.
ciKlass* ciEnv::get_klass_by_index_impl(ciInstanceKlass* accessor,
                                        int index,
                                        bool& is_accessible) {
  assert(accessor->get_instanceKlass()->is_linked(), "must be linked before accessing constant pool");
  EXCEPTION_CONTEXT;
  constantPoolHandle cpool(THREAD, accessor->get_instanceKlass()->constants());
  KlassHandle klass (THREAD, constantPoolOopDesc::klass_at_if_loaded(cpool, index));
  symbolHandle klass_name;
  if (klass.is_null()) {
    // The klass has not been inserted into the constant pool.
    // Try to look it up by name.
    {
      // We have to lock the cpool to keep the oop from being resolved
      // while we are accessing it.
ObjectLocker ol(cpool());

      constantTag tag = cpool->tag_at(index);
      if (tag.is_klass()) {
        // The klass has been inserted into the constant pool
        // very recently.
        klass = KlassHandle(THREAD, cpool->resolved_klass_at(index));
      } else if (tag.is_symbol()) {
        klass_name = symbolHandle(THREAD, cpool->symbol_at(index));
      } else {
        assert(cpool->tag_at(index).is_unresolved_klass(), "wrong tag");
        klass_name = symbolHandle(THREAD, cpool->unresolved_klass_at(index));
      }
    }
  }
  // We have either the klass now, or at least the klass_name

  if (klass.is_null()) {
    // Not found in constant pool.  Use the name to do the lookup.
    // get_klass_by_name_impl will return a prior unloaded ciKlass
    // if one exists.
    ciKlass* k = get_klass_by_name_impl(accessor,
                                        get_object(klass_name())->as_symbol(),
                                        false);
    // Calculate accessibility the hard way.
    if (!k->is_loaded()) {
      is_accessible = false;
    } else if (k->loader() != accessor->loader() &&
               get_klass_by_name_impl(accessor, k->name(), true) == NULL) {
      // Loaded only remotely.  Not linked yet.
      is_accessible = false;
    } else {
      // Linked locally, and we must also check public/private, etc.
      is_accessible = check_klass_accessibility(accessor, k->get_klassOop());
    }
    accessor->add_klass_by_cpool_index(index, k, is_accessible);
    // Return the found ciKlass, which will be an unloaded ciKlass if
    // get_klass_by_name found an unloaded one with this name.
    return k;
  }

  // Check for prior unloaded klass.  The SystemDictionary's answers
  // can vary over time but the compiler needs consistency.
  ciSymbol* name = get_object(klass()->klass_part()->name())->as_symbol();
  ciKlass* unloaded_klass = check_get_unloaded_klass(accessor, name);
  if (unloaded_klass != NULL) {
    is_accessible = false;
    accessor->add_klass_by_cpool_index(index, unloaded_klass, is_accessible);
    return unloaded_klass;
  }

  // It is known to be accessible, since it was found in the constant pool.
  is_accessible = true;
ciKlass*cik=get_object(klass())->as_klass();
  accessor->add_klass_by_cpool_index(index, cik, is_accessible);
  return cik;
}

// ------------------------------------------------------------------
// ciEnv::get_klass_by_index
//
// Get a klass from the constant pool.
ciKlass* ciEnv::get_klass_by_index(ciInstanceKlass* accessor,
                                   int index,
                                   bool& is_accessible) {
  if (FAM) {
    return accessor->get_klass_by_cpool_index(index, is_accessible);
  }
  GUARDED_VM_ENTRY(return get_klass_by_index_impl(accessor, index, is_accessible);)
}

// ------------------------------------------------------------------
// ciEnv::get_constant_by_index_impl
//
// Implementation of get_constant_by_index().
ciConstant ciEnv::get_constant_by_index_impl(ciInstanceKlass* accessor,
					     int index) {
  EXCEPTION_CONTEXT;
  instanceKlass* ik_accessor = accessor->get_instanceKlass();
  assert(ik_accessor->is_linked(), "must be linked before accessing constant pool");
  constantPoolOop cpool = ik_accessor->constants();
  constantTag tag = cpool->tag_at(index);
  if (tag.is_int()) {
    ciConstant* con = new (arena()) ciConstant(T_INT, (jint)cpool->int_at(index));
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else if (tag.is_long()) {
    ciConstant* con = new (arena()) ciConstant((jlong)cpool->long_at(index));
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else if (tag.is_float()) {
    ciConstant* con = new (arena()) ciConstant((jfloat)cpool->float_at(index));
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else if (tag.is_double()) {
    ciConstant* con = new (arena()) ciConstant((jdouble)cpool->double_at(index));
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else if (tag.is_string() || tag.is_unresolved_string()) {
    oop string = cpool->string_at(index, THREAD);
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
      record_out_of_memory_failure();
      ciConstant* con = new (arena()) ciConstant();
      accessor->add_constant_by_cpool_index(index, con);
      return *con;
    }
    ciObject* constant = get_object(string);
    assert (constant->is_instance(), "must be an instance, or not? ");
    ciConstant* con = new (arena()) ciConstant(T_OBJECT, constant);
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else if (tag.is_klass() || tag.is_unresolved_klass()) {
    // 4881222: allow ldc to take a class type
    bool ignore;
    ciKlass* klass = get_klass_by_index_impl(accessor, index, ignore);
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
      record_out_of_memory_failure();
      ciConstant* con = new (arena()) ciConstant();
      accessor->add_constant_by_cpool_index(index, con);
      return *con;
    }
    assert (klass->is_instance_klass() || klass->is_array_klass(), 
            "must be an instance or array klass ");
    ciConstant* con = new (arena()) ciConstant(T_OBJECT, klass);
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  } else {
    ShouldNotReachHere();
    ciConstant* con = new (arena()) ciConstant();
    accessor->add_constant_by_cpool_index(index, con);
    return *con;
  }
}

// ------------------------------------------------------------------
// ciEnv::is_unresolved_string_impl
//
// Implementation of is_unresolved_string().
bool ciEnv::is_unresolved_string_impl(instanceKlass* accessor, int index) const {
  EXCEPTION_CONTEXT;
  assert(accessor->is_linked(), "must be linked before accessing constant pool");
  constantPoolOop cpool = accessor->constants();
  constantTag tag = cpool->tag_at(index);
  return tag.is_unresolved_string();
}

// ------------------------------------------------------------------
// ciEnv::is_unresolved_klass_impl
//
// Implementation of is_unresolved_klass().
bool ciEnv::is_unresolved_klass_impl(instanceKlass* accessor, int index) const {
  EXCEPTION_CONTEXT;
  assert(accessor->is_linked(), "must be linked before accessing constant pool");
  constantPoolOop cpool = accessor->constants();
  constantTag tag = cpool->tag_at(index);
  return tag.is_unresolved_klass();
}

// ------------------------------------------------------------------
// ciEnv::get_constant_by_index
//
// Pull a constant out of the constant pool.  How appropriate.
//
// Implementation note: this query is currently in no way cached.
ciConstant ciEnv::get_constant_by_index(ciInstanceKlass* accessor,
					int index) {
  if (FAM) {
    return *accessor->get_constant_by_cpool_index(index);
  }
  GUARDED_VM_ENTRY(return get_constant_by_index_impl(accessor, index); )
}

// ------------------------------------------------------------------
// ciEnv::is_unresolved_string
//
// Check constant pool
//
// Implementation note: this query is currently in no way cached.
bool ciEnv::is_unresolved_string(ciInstanceKlass* accessor,
					int index) const {
  if (FAM) {
    return accessor->get_is_unresolved_string(index);
  }
  GUARDED_VM_ENTRY(
    bool ret = is_unresolved_string_impl(accessor->get_instanceKlass(), index); 
    accessor->add_is_unresolved_string(index, ret);
    return ret;
  )
}

// ------------------------------------------------------------------
// ciEnv::is_unresolved_klass
//
// Check constant pool
//
// Implementation note: this query is currently in no way cached.
bool ciEnv::is_unresolved_klass(ciInstanceKlass* accessor,
					int index) const {
  if (FAM) {
    return accessor->get_is_unresolved_klass(index);
  }

  GUARDED_VM_ENTRY(
    bool ret = is_unresolved_klass_impl(accessor->get_instanceKlass(), index); 
    accessor->add_is_unresolved_klass(index, ret);
    return ret;
  )
}

// ------------------------------------------------------------------
// ciEnv::get_field_by_index_impl
//
// Implementation of get_field_by_index.
//
// Implementation note: the results of field lookups are cached
// in the accessor klass.
ciField* ciEnv::get_field_by_index_impl(ciInstanceKlass* accessor,
					int index) {
  ciConstantPoolCache* cache = accessor->field_cache();
  if (cache == NULL) {
    ciField* field = new (arena()) ciField(accessor, index);
accessor->add_field_by_cpool_index(index,field);
    return field;
  } else {
    ciField* field = (ciField*)cache->get(index);
    if (field == NULL) {
      field = new (arena()) ciField(accessor, index);
      cache->insert(index, field);
    }
accessor->add_field_by_cpool_index(index,field);
    return field;
  }
}

// ------------------------------------------------------------------
// ciEnv::get_field_by_index
//
// Get a field by index from a klass's constant pool.
ciField* ciEnv::get_field_by_index(ciInstanceKlass* accessor,
				   int index) {
  if (FAM) {
    return accessor->get_field_by_cpool_index(index);
  }
  GUARDED_VM_ENTRY(return get_field_by_index_impl(accessor, index);)
}

// ------------------------------------------------------------------
// ciEnv::lookup_method
//
// Perform an appropriate method lookup based on accessor, holder,
// name, signature, and bytecode.
methodOop ciEnv::lookup_method(instanceKlass*  accessor,
			       instanceKlass*  holder,
			       symbolOop       name,
			       symbolOop       sig,
			       Bytecodes::Code bc) {
  EXCEPTION_CONTEXT;
  KlassHandle h_accessor(THREAD, accessor);
  KlassHandle h_holder(THREAD, holder);
  symbolHandle h_name(THREAD, name);
  symbolHandle h_sig(THREAD, sig);
  LinkResolver::check_klass_accessability(h_accessor, h_holder, KILL_COMPILE_ON_FATAL_(NULL));
  methodHandle dest_method;
  switch (bc) {
  case Bytecodes::_invokestatic:
    dest_method = 
      LinkResolver::resolve_static_call_or_null(h_holder, h_name, h_sig, h_accessor); 
    break;
  case Bytecodes::_invokespecial:
    dest_method = 
      LinkResolver::resolve_special_call_or_null(h_holder, h_name, h_sig, h_accessor); 
    break;
  case Bytecodes::_invokeinterface: 
    dest_method =
      LinkResolver::linktime_resolve_interface_method_or_null(h_holder, h_name, h_sig,
							      h_accessor, true);
    break;
  case Bytecodes::_invokevirtual:
    dest_method = 
      LinkResolver::linktime_resolve_virtual_method_or_null(h_holder, h_name, h_sig,
							    h_accessor, true);
    break;
  default: ShouldNotReachHere();
  }

  return dest_method();
}


// ------------------------------------------------------------------
// ciEnv::get_method_by_index_impl
ciMethod* ciEnv::get_method_by_index_impl(ciInstanceKlass* accessor,
                                     int index, Bytecodes::Code bc) {
  // Get the method's declared holder.
                       
  assert(accessor->get_instanceKlass()->is_linked(), "must be linked before accessing constant pool");
  constantPoolHandle cpool = accessor->get_instanceKlass()->constants();
  int holder_index = cpool->klass_ref_index_at(index);
  bool holder_is_accessible;
  ciKlass* holder = get_klass_by_index_impl(accessor, holder_index, holder_is_accessible);
ciInstanceKlass*declared_holder=NULL;
if(holder->is_array_klass()){
    // methods on arrays are equivalent to methods on java.lang.Object
    declared_holder = Object_klass();
  } else {
    declared_holder = holder->as_instance_klass();
  }

  // Get the method's name and signature.
  int nt_index = cpool->name_and_type_ref_index_at(index);
  int sig_index = cpool->signature_ref_index_at(nt_index);
  symbolOop name_sym = cpool->name_ref_at(index);
  symbolOop sig_sym = cpool->symbol_at(sig_index);

  if (holder_is_accessible) { // Our declared holder is loaded.
    instanceKlass* lookup = declared_holder->get_instanceKlass();
    methodOop m = lookup_method(accessor->get_instanceKlass(), lookup, name_sym, sig_sym, bc);
    if (m != NULL) {
      // We found the method.
      ciMethod* cim = get_object(m)->as_method();
      accessor->add_method_by_cpool_index(index, cim);
      return cim;
    }
  }

  // Either the declared holder was not loaded, or the method could
  // not be found.  Create a dummy ciMethod to represent the failed
  // lookup.

  ciMethod* cim = get_unloaded_method(declared_holder,
                             get_object(name_sym)->as_symbol(),
                             get_object(sig_sym)->as_symbol());
  accessor->add_method_by_cpool_index(index, cim);
  return cim;
}

// ------------------------------------------------------------------
// ciEnv::get_instance_klass_for_declared_method_holder
ciInstanceKlass* ciEnv::get_instance_klass_for_declared_method_holder(ciKlass* method_holder) {
  // For the case of <array>.clone(), the method holder can be a ciArrayKlass
  // instead of a ciInstanceKlass.  For that case simply pretend that the
  // declared holder is Object.clone since that's where the call will bottom out.
  // A more correct fix would trickle out through many interfaces in CI,
  // requiring ciInstanceKlass* to become ciKlass* and many more places would
  // require checks to make sure the expected type was found.  Given that this
  // only occurs for clone() the more extensive fix seems like overkill so
  // instead we simply smear the array type into Object.
  if (method_holder->is_instance_klass()) {
    return method_holder->as_instance_klass();
  } else if (method_holder->is_array_klass()) {
    return current()->Object_klass();
  } else {
    ShouldNotReachHere();
  }
  return NULL;
}

// ------------------------------------------------------------------
// ciEnv::get_method_by_index
ciMethod* ciEnv::get_method_by_index(ciInstanceKlass* accessor,
                                     int index, Bytecodes::Code bc) {
  if (FAM) {
    return accessor->get_method_by_cpool_index(index);
  }
  GUARDED_VM_ENTRY(return get_method_by_index_impl(accessor, index, bc);)
}

// ------------------------------------------------------------------
// ciEnv::name_buffer
char *ciEnv::name_buffer(int req_len) {
  if (_name_buffer_len < req_len) {
    if (_name_buffer == NULL) {
      _name_buffer = (char*)arena()->Amalloc(sizeof(char)*req_len);
      _name_buffer_len = req_len;
    } else {
      _name_buffer =
	(char*)arena()->Arealloc(_name_buffer, _name_buffer_len, req_len);
      _name_buffer_len = req_len;
    }
  }
  return _name_buffer;
}

// ------------------------------------------------------------------
bool ciEnv::system_dictionary_modification_counter_changed() {
  return _system_dictionary_modification_counter != SystemDictionary::number_of_modifications();
}

// --- check_for_system_dictionary_modification
// ciEnv::check_for_system_dictionary_modification
// Check for changes to the system dictionary during compilation
// class loads, evolution, breakpoints
bool ciEnv::check_for_system_dictionary_modification(MacroAssembler*masm){
if(failing())return true;//no need for further checks

  // Dependencies must be checked when the system dictionary changes.
  // If logging is enabled all violated dependences will be recorded in
  // the log.  In debug mode check dependencies even if the system
  // dictionary hasn't changed to verify that no invalid dependencies
  // were inserted.  Any violated dependences in this case are dumped to
  // the tty.

  bool counter_changed = system_dictionary_modification_counter_changed();
  bool test_deps = counter_changed;
  DEBUG_ONLY(test_deps = true);
if(test_deps){
    // Required to access SystemDictionary::number_of_adds().
    assert_lock_strong(Compile_lock);

    // See if we pass the dependencies, also under lock so nothing can change.
    if( !masm->check_dependencies() ) {
      // Failed: something is wrong
      assert0( counter_changed );
      record_failure("concurrent class loading", true, false);
      return true;
    }
  }
  int success = masm->insert_dependencies();
  if( success == 1 ) {
      // Really really do not expect to fail here
      Untested("");
      assert0( counter_changed );
      record_failure("out of memory during code install", true, false);
      return true;
  } else if (success == 2) {
    // In this case, we've run out of space in the dependencies array -> need to grow
    return false;
  }

  // All is well!
  return true;
}

// ------------------------------------------------------------------
// ciEnv::register_method
void ciEnv::register_method(ciMethod*       target,
                            int             entry_bci,
                            MacroAssembler* masm,
                            CodeProfile*    cp,
ByteSize frame_bytes,
Label&generic_exception_handler,
                            bool            has_unsafe_access) {
  VM_ENTRY_MARK;

  // No more uses of this masm or blob, so bake in OopMap goodness.
  methodCodeHandle mch = masm->bake_into_codeOop(target->get_methodOop(),in_bytes(frame_bytes),cp,has_unsafe_access,CHECK);
  bool is_c1 = CompilerThread::current()->is_C1Compiler_thread();
if(mch.is_null()){
    // Don't recompile (a better solution would be a feedback mechanism to soften inlining or unrolling or whatever)
    target->get_methodOop()->set_not_compilable(is_c1 ? 1 : 2);
    return; // We failed to get the code oop, possibly because it was too large.  Bail..
  }
  mch()->_generic_exception_handler_rel_pc = is_c1 
    ? generic_exception_handler.rel_pc(masm) 
    : -1/*c2 does not use generic handlers*/;
  
  { // To prevent compile queue updates.
    while(true) {
      // Allocate memory for dependent mco arrays for each instance klass outside of the Compile_lock.
      // If we race with somebody else installing dependent mco's and we run out of room in the array,
      // we must grow the array, and try check_for_system_dictionary_modification again
      objArrayHandle dkh = mch()->dep_klasses().as_objArrayOop();
      if (dkh()) {
        bool failed_cas = false; 
for(int i=0;i<dkh()->length();i++){
          klassOop klass = (klassOop)dkh()->obj_at(i);
          instanceKlass *ik = instanceKlass::cast(klass);
instanceKlassHandle ikh(THREAD,klass);
          if (!ik) continue;
          objArrayHandle mcos = ik->dependent_mcos();
          
          // Optimistic allocation - must recheck after locking
          objArrayHandle newMCOs = mcos;
          if (!mcos()) { // No array at all?
            newMCOs = oopFactory::new_objectArray(1, false, CHECK);
          } else {
int len=mcos()->length();
            if (*(intptr_t*) mcos()->obj_at_addr(len - 1)) {
              // Last word is full, so must grow the array.
              // Optimistic allocation - must recheck after locking
              const int newlen = len + (len >> 1) + 1;
              newMCOs = oopFactory::new_objectArray(newlen, false, CHECK);
              Klass::cast(mcos()->klass())->copy_array(mcos(), 0, newMCOs(), 0, len, CHECK);
            }
          }
          
          // this could be CAS or a lock
          // leaving the lock in here
MutexLockerAllowGC locker(Compile_lock,THREAD);

          ik = instanceKlass::cast(ikh()); // ik could have been toasted by GC
          if (ik->dependent_mcos() != mcos()) {
            failed_cas = true;
            break;
          }
          ik->set_dependent_mcos(newMCOs());
        }

        // Racing with somebody.  Retry allocation of dependency space.
        if (failed_cas) continue;
      }

      // under lock check to make sure the state of the world is 
      // still the same
      // remember to give up the lock on failure paths
      Compile_lock.lock_allowing_gc((JavaThread*)Thread::current());
      GET_RPC;
      Compile_lock._rpc = RPC;
      while (dont_install_code()) {
        assert_lock_strong(Compile_lock);
        Compile_lock.wait();
      }


      // Check for {class loads, evolution, breakpoints} during compilation
      bool success = check_for_system_dictionary_modification(masm);

      if (!success) {
        // Unlock Compile_lock
Compile_lock.unlock();
        continue; // Need to grow instanceKlass dependent mco lists
      }

      if (failing()) {            // Failed; bail
        // Unlock Compile_lock
Compile_lock.unlock();
        // Bailing out.  The 'mch' goes dead here, and it's deletion
        // will recover the CodeBlob storage.
        return;
      }

      if( !is_c1 && C2CompilerThread::current()->_compile->print_assembly() ) {
        mch()->_blob->decode();
      }

      // Install the active code
      target->get_methodOop()->insert_methodCode(mch.as_ref());
      if (entry_bci == InvocationEntryBci) 
        target->get_methodOop()->set_codeRef(mch.as_ref());

      // Unlock Compile_lock
Compile_lock.unlock();

      // If we have a C1 method, arrange so his CodeProfile trips the C2
      // overflow count right away, so the next call to the C1 method instead
      // goes to C2.
      if( !is_c1 ) {
        methodCodeOop mc1 = target->get_methodOop()->lookup_c1();
        if( mc1 ) {
          mc1->get_codeprofile()->reset_invoke_count_to(C1PromotionThreshold);
        }
      }

      // Break out of while loop
      break;
    }
  }

  if (PrintMethodCodes) {
    masm->decode(tty);
  }

  // JVMTI -- compiled method notification (must be done outside lock)
  mch()->post_compiled_method_load_event();
}


// ------------------------------------------------------------------
// ciEnv::find_system_klass
ciKlass* ciEnv::find_system_klass(ciSymbol* klass_name) {
  if (FAM) {
    ciKlass* klass = _factory->get_ciKlass(klass_name);
    guarantee(klass, "klass not found");  
    return klass;
  }
  VM_ENTRY_MARK;
  return get_klass_by_name_impl(NULL, klass_name, false);
}

// ------------------------------------------------------------------
// ciEnv::compile_id
uint ciEnv::compile_id() {
  if (task() == NULL)  return 0;
  return task()->compile_id();
}

// ------------------------------------------------------------------
// ciEnv::notice_inlined_method()
void ciEnv::notice_inlined_method(ciMethod* method) {
  _num_inlined_bytecodes += method->code_size();
}

// ------------------------------------------------------------------
// ciEnv::num_inlined_bytecodes()
int ciEnv::num_inlined_bytecodes() const {
  return _num_inlined_bytecodes;
}

// ------------------------------------------------------------------
// ciEnv::record_failure()
void ciEnv::record_failure(const char*reason,bool retry_compile,bool retry_compile_immediately){
if(_failure_reason==NULL){//Record the first failure reason.
    _failure_reason = reason;
    _failure_retry_compile = retry_compile;
    _failure_retry_compile_immediately = retry_compile_immediately;
  }
}

// ------------------------------------------------------------------
// Azul: oop table interface

int ciEnv::get_OopTable_index(jobject h){
  if (FAM) {
    return 0; // Hack for the moment.  Real value should be cached
  }
  VM_ENTRY_MARK;
  int idx = CodeCacheOopTable::putOop(h); // May GC!
  return idx;
}
