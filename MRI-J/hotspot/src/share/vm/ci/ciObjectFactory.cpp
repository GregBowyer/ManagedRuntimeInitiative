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


#include "ciEnv.hpp"
#include "ciInstance.hpp"
#include "ciInstanceKlassKlass.hpp"
#include "ciMethodKlass.hpp"
#include "ciNullObject.hpp"
#include "ciObjArray.hpp"
#include "ciObjArrayKlass.hpp"
#include "ciObjArrayKlassKlass.hpp"
#include "ciObjectFactory.hpp"
#include "ciSymbolKlass.hpp"
#include "ciTypeArray.hpp"
#include "ciTypeArrayKlass.hpp"
#include "ciTypeArrayKlassKlass.hpp"
#include "fieldType.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"

#include "oop.inline2.hpp"

// ciObjectFactory
//
// This class handles requests for the creation of new instances
// of ciObject and its subclasses.  It contains a caching mechanism
// which ensures that for each oop, at most one ciObject is created.
// This invariant allows more efficient implementation of ciObject.
//
// Implementation note: the oop->ciObject mapping is represented as
// a table stored in an array.  Even though objects are moved
// by the garbage collector, the compactor preserves their relative
// order; address comparison of oops (in perm space) is safe so long
// as we prohibit GC during our comparisons.  We currently use binary
// search to find the oop in the table, and inserting a new oop
// into the table may be costly.  If this cost ends up being
// problematic the underlying data structure can be switched to some
// sort of balanced binary tree.

ciObject**                ciObjectFactory::_shared_hashtable = NULL; // Common shared hashtable for all compiles
int ciObjectFactory::_shared_hashmax=0;
ciSymbol*                 ciObjectFactory::_shared_ci_symbols[vmSymbols::SID_LIMIT];
int                       ciObjectFactory::_shared_ident_limit = 0;
volatile bool             ciObjectFactory::_initialized = false;


// ------------------------------------------------------------------
// ciObjectFactory::ciObjectFactory
ciObjectFactory::ciObjectFactory(Arena* arena,
                                 int expected_size) {

  for (int i = 0; i < NON_PERM_BUCKETS; i++) {
    _non_perm_bucket[i] = NULL;
  }
  _non_perm_count = 0;

  _next_ident = _shared_ident_limit;
  _arena = arena;

  _unloaded_methods = new (arena) GrowableArray<ciMethod*>(arena, 4, 0, NULL);
  _unloaded_klasses = new (arena) GrowableArray<ciKlass*>(arena, 8, 0, NULL);
  _return_addresses =
    new (arena) GrowableArray<ciReturnAddress*>(arena, 8, 0, NULL);

  _hashcnt = 0;                 // Empty private hash table
  _hashmax = 256;               // Next larger power of 2 above average size of 174
  _hashtable = NEW_ARENA_ARRAY(arena,ciObject*,_hashmax);
  bzero(_hashtable,sizeof(ciObject*)*_hashmax);
}

// ------------------------------------------------------------------
// ciObjectFactory::ciObjectFactory - FAM version
ciObjectFactory::ciObjectFactory(Arena* arena, FAMPtr old_ciof) {
  Unimplemented();
/*
  FAM->mapNewToOldPtr(this, old_ciof);

  _arena = arena;

  _FAM_fixup_list = new (arena) GrowableArray<ciObject*>(arena, 4, 0, NULL);

  init_shared_objects_FAM();

  _unloaded_klasses = new (arena) GrowableArray<ciKlass*>(arena, 4, 0, NULL);
  _unloaded_methods = new (arena) GrowableArray<ciMethod*>(arena, 4, 0, NULL);
  _return_addresses = new (arena) GrowableArray<ciReturnAddress*>(arena, 4, 0, NULL);

  // TODO: Fixup _return_addresses

  // Load _unloaded_klasses
  int old_uk_size   = FAM->getInt("((struct ciObjectFactory*)%p)->_unloaded_klasses->_len", old_ciof);
  _unloaded_klasses = new (arena) GrowableArray<ciKlass*>(arena, old_uk_size, 0, NULL);
  for(int i=0; i<old_uk_size; i++) {
    FAMPtr old_uk = FAM->getOldPtr("((struct ciObjectFactory*)%p)->_unloaded_klasses->_data[%d]", old_ciof, i);
    if (old_uk) {
      ciKlass* new_cik = create_from_FAMPtr(old_uk)->as_klass();
      _unloaded_klasses->at_put_grow(i, new_cik);
    }
  }

  // Load _unloaded_methods
  int old_um_size   = FAM->getInt("((struct ciObjectFactory*)%p)->_unloaded_methods->_len", old_ciof);
  _unloaded_methods = new (arena) GrowableArray<ciMethod*>(arena, old_um_size, 0, NULL);
  for(int i=0; i<old_um_size; i++) {
    FAMPtr old_um = FAM->getOldPtr("((struct ciObjectFactory*)%p)->_unloaded_methods->_data[%d]", old_ciof, i);
    if (old_um) {
      ciMethod* new_cim = create_from_FAMPtr(old_um)->as_method();
      _unloaded_methods->at_put_grow(i, new_cim);
    }
  }

  int old_table_size   = FAM->getInt("((struct ciObjectFactory*)%p)->_hashmax", old_ciof);
  FAMPtr old_table_ptr = FAM->getOldPtr("((struct ciObjectFactory*)%p)->_hashtable", old_ciof);

  _hashcnt = 0;
  _hashmax = old_table_size;
  _hashtable = NEW_ARENA_ARRAY(arena,ciObject*,_hashmax+1);
  bzero(_hashtable,sizeof(ciObject*)*(_hashmax+1));

  // Pass 1: Suck up ci structures from core
  _hashcnt = populate_table_from_FAM(_hashtable, old_table_ptr, old_table_size);

  // Phase 2: Fix up internal pointers
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] != NULL) {
      tty->print_cr("%d (%p): Fixing up shared pointers", i, _shared_hashtable[i]);
      _shared_hashtable[i]->fixupFAMPointers();
    }
  }
  for(int i=0; i<old_table_size; i++) {
    if (_hashtable[i] != NULL) {
      tty->print_cr("%d (%p): Fixing up local pointers", i, _hashtable[i]);
      _hashtable[i]->fixupFAMPointers();
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) != NULL) {
      tty->print_cr("%d (%p): Fixing up FAM list pointers", i, _FAM_fixup_list->at(i));
      _FAM_fixup_list->at(i)->fixupFAMPointers();
    }
  }
  for(int i=0; i<_unloaded_klasses->length(); i++) {
    if (_unloaded_klasses->at(i) != NULL) {
      tty->print_cr("%d (%p): Fixing up unloaded klasses", i, _unloaded_klasses->at(i));
      _unloaded_klasses->at(i)->fixupFAMPointers();
    }
  }
  for(int i=0; i<_unloaded_methods->length(); i++) {
    if (_unloaded_methods->at(i) != NULL) {
      tty->print_cr("%d (%p): Fixing up unloaded methods", i, _unloaded_methods->at(i));
      _unloaded_methods->at(i)->fixupFAMPointers();
    }
  }
*/
}

ciObject* ciObjectFactory::create_from_FAMPtr(FAMPtr fp) {
if(fp==0)return NULL;

  ciObject* newci=0;
  char* buf = FAM->getStringFromGDBCmd("x/i *(intptr_t*)%p", fp);
if(strstr(buf,"ciInstanceKlassKlass")!=NULL){
tty->print_cr("Found: ciInstanceKlassKlass");
    newci = new ciInstanceKlassKlass(fp);
  }
  else if (strstr(buf, "ciKlassKlass") != NULL) {
tty->print_cr("Found: ciKlassKlass");
    newci = new ciKlassKlass(fp);
  }
  else if (strstr(buf, "ciInstanceKlass") != NULL) {
tty->print_cr("Found: ciInstanceKlass");
    newci = new ciInstanceKlass(fp);
  }
  else if (strstr(buf, "ciInstance") != NULL) {
tty->print_cr("Found: ciInstance");
    newci = new ciInstance(fp);
  }
  else if (strstr(buf, "ciSymbolKlass") != NULL) {
tty->print_cr("Found: ciSymbolKlass");
    newci = new ciSymbolKlass(fp);
  }
  else if (strstr(buf, "ciSymbol") != NULL) {
    newci = new ciSymbol(fp);
    tty->print_cr("Found: ciSymbol (%s)", ((ciSymbol*)newci)->as_utf8());
  }
  else if (strstr(buf, "ciTypeArrayKlass") != NULL) {
tty->print_cr("Found: ciTypeArrayKlass");
    newci = new ciTypeArrayKlass(fp);
  }
  else if (strstr(buf, "ciTypeArray") != NULL) {
tty->print_cr("Found: ciTypeArray");
    newci = new ciTypeArray(fp);
  }
  else if (strstr(buf, "ciType") != NULL) {
tty->print_cr("Found: ciType");
    newci = new ciType(fp);
  }
  else if (strstr(buf, "ciKlass") != NULL) {
tty->print_cr("Found: ciKlass");
    newci = new ciKlass(fp);
  }
  else if (strstr(buf, "ciMethodKlass") != NULL) {
tty->print_cr("Found: ciMethodKlass");
    newci = new ciMethodKlass(fp);
  }
  else if (strstr(buf, "ciMethod") != NULL) {
tty->print_cr("Found: ciMethod");
    newci = new ciMethod(fp);
  }
  else if (strstr(buf, "ciFieldLayout") != NULL) {
ShouldNotReachHere();//ciFieldLayout is not a ciObject
  }
  else if (strstr(buf, "ciField") != NULL) {
    ShouldNotReachHere();
  }
  else if (strstr(buf, "ciObjArrayKlassKlass") != NULL) {
tty->print_cr("Found: ciObjArrayKlass");
    newci = new ciObjArrayKlassKlass(fp);
  }
  else if (strstr(buf, "ciArrayKlassKlass") != NULL) {
tty->print_cr("Found: ciObjArrayKlass");
    newci = new ciArrayKlassKlass(fp);
  }
  else if (strstr(buf, "ciObjArrayKlass") != NULL) {
tty->print_cr("Found: ciObjArrayKlass");
    newci = new ciObjArrayKlass(fp);
  }
  else if (strstr(buf, "ciObjArray") != NULL) {
tty->print_cr("Found: ciObjArray");
    newci = new ciObjArray(fp);
  }
  else {
    ShouldNotReachHere();
  }

  return newci;
}

// Returns number of elements actually inserted into table
int ciObjectFactory::populate_table_from_FAM(ciObject** table, FAMPtr old_table_ptr, int old_table_size) {
  int hashcnt=0;
for(int i=0;i<old_table_size;i++){
    FAMPtr entry_ptr = FAM->getOldPtr("((struct ciObject**)%p)[%d]", old_table_ptr, i);
    if (entry_ptr == 0) continue;
    tty->print("%d (old: " INTPTR_FORMAT "): ", i, entry_ptr);
    ciObject* newci = create_from_FAMPtr(entry_ptr);

    if (newci) {
tty->print_cr("Constructed with new ptr %p",newci);
      FAM->mapNewToOldPtr(newci, entry_ptr);
table[i]=newci;
      hashcnt++;
    }
  }
  Untested();
  return 0;
}

// ciObjectFactory::ciObjectFactory
void ciObjectFactory::initialize() {
  ASSERT_IN_VM;
  JavaThread* thread = JavaThread::current();
  HandleMark  handle_mark(thread);

  // This Arena is long lived and exists in the resource mark of the
  // compiler thread that initializes the initial ciObjectFactory which
  // creates the shared ciObjects that all later ciObjectFactories use.
  Arena* arena = new Arena();
  ciEnv initial(arena);
  ciEnv* env = ciEnv::current();
  env->_factory->init_shared_objects();

  _initialized = true;

}


// ------------------------------------------------------------------
// ciObjectFactory::ciObjectFactory
void ciObjectFactory::init_shared_objects_FAM(){
  Unimplemented();
/*
  MutexLockerAllowGC mu(Compile_lock, JavaThread::current());

  _shared_hashmax = 1;

  ciEnv::current()->_factory = this;

  // Blow away current shared structure (if one exists)
  FAM->issueGDBCmdNoRPC("p 'ciObjectFactory::_shared_hashtable'");
  int test = FAM->getInt("1234");

  int old_shared_table_size   = FAM->getInt("((struct ciObjectFactory*)%p)->_shared_hashmax", FAM->getOldFromNewPtr(this));
  FAMPtr old_shared_table_ptr = FAM->getOldPtr("((struct ciObjectFactory*)%p)->_shared_hashtable", FAM->getOldFromNewPtr(this));
  assert0(old_shared_table_ptr);
  _shared_hashmax = old_shared_table_size;
  _shared_hashtable = NEW_ARENA_ARRAY(_arena,ciObject*,old_shared_table_size);
  bzero(_shared_hashtable,sizeof(ciObject*)*old_shared_table_size);
  populate_table_from_FAM(_shared_hashtable, old_shared_table_ptr, old_shared_table_size);

  // Create the shared symbols, both in the shared hashtable
  // and in the shared_ci_symbols lookup array.
  const int sym_count = vmSymbolHandles::symbol_handle_count();
  _shared_ci_symbols = NEW_ARENA_ARRAY(_arena,ciObject*,sym_count);
  for (int i = vmSymbols::FIRST_SID; i < sym_count; i++) {
    _shared_ci_symbols[i] = new ciSymbol(FAM->getOldPtr("((struct ciObjectFactory*)%p)->_shared_ci_symbols[%d]", FAM->getOldFromNewPtr(this), i));
    tty->print_cr("%d Loading vmsymbol %s", i, ((ciSymbol*)_shared_ci_symbols[i])->as_utf8());
  }

  // Create the basic types
  for (int i = T_BOOLEAN; i <= T_CONFLICT; i++) {
    BasicType t = (BasicType)i;
    if (type2name(t) != NULL && t != T_OBJECT && t != T_ARRAY) {
      char buf[128];
      snprintf(buf, 128, "'ciType::_basic_types'[%d]", i);
      ciType::_basic_types[i] = new ciType(FAM->getOldPtr(buf)); _FAM_fixup_list->append(ciType::_basic_types[i]);
    }
  }

#define FAM_INIT(citype, ciikname) { ciikname = new citype(FAM->getOldPtr("'"#ciikname"'")); _FAM_fixup_list->append(ciikname); }
#define FAM_INIT_INDEX(citype, ciikname, index) { FAMPtr fp = FAM->getOldPtr("'"#ciikname"'["#index"]"); ciikname[index] = fp ? new citype(fp) : NULL; if (fp) _FAM_fixup_list->append(ciikname[index]); }

  FAM_INIT(ciNullObject,          ciEnv::_null_object_instance);
  FAM_INIT(ciMethodKlass,         ciEnv::_method_klass_instance);
  FAM_INIT(ciSymbolKlass,         ciEnv::_symbol_klass_instance);
  FAM_INIT(ciKlassKlass,          ciEnv::_klass_klass_instance);
  FAM_INIT(ciInstanceKlassKlass,  ciEnv::_instance_klass_klass_instance);
  FAM_INIT(ciTypeArrayKlassKlass, ciEnv::_type_array_klass_klass_instance);
  FAM_INIT(ciObjArrayKlassKlass,  ciEnv::_obj_array_klass_klass_instance);
  FAM_INIT(ciInstanceKlass,       ciEnv::_ArrayStoreException);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Class);
  FAM_INIT(ciInstanceKlass,       ciEnv::_ClassCastException);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Cloneable);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Object);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Serializable);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Throwable);
  FAM_INIT(ciInstanceKlass,       ciEnv::_Thread);
  FAM_INIT(ciInstanceKlass,       ciEnv::_OutOfMemoryError);
  FAM_INIT(ciInstanceKlass,       ciEnv::_String);
  FAM_INIT(ciSymbol,              ciEnv::_unloaded_cisymbol);
  
  FAM_INIT(ciInstanceKlass,       ciEnv::_unloaded_ciinstance_klass);
  FAM_INIT(ciObjArrayKlass,       ciEnv::_unloaded_ciobjarrayklass);
  assert(ciEnv::_unloaded_ciobjarrayklass->is_obj_array_klass(), "just checking");
  ciEnv::_array_ifaces = (ciInstanceKlass**)_arena->Amalloc(3*sizeof(ciInstanceKlass*));
  FAM_INIT_INDEX(ciInstanceKlass, ciEnv::_array_ifaces, 0);
  FAM_INIT_INDEX(ciInstanceKlass, ciEnv::_array_ifaces, 1);
  FAM_INIT_INDEX(ciInstanceKlass, ciEnv::_array_ifaces, 2);

#undef FAM_INIT
#undef FAM_INIT_INDEX

//
//  get(Universe::boolArrayKlassObj()  );
//  get(Universe::charArrayKlassObj()  );
//  get(Universe::singleArrayKlassObj());
//  get(Universe::doubleArrayKlassObj());
//  get(Universe::byteArrayKlassObj()  );
//  get(Universe::shortArrayKlassObj() );
//  get(Universe::intArrayKlassObj()   );
//  get(Universe::longArrayKlassObj()  );
//
//  // Pre-load some transitive interfaces, so they allocate memory in the
//  // shared arena and not the per-compile arena.
//  bool more = true;
//  while( more ) {               // I have to iterate to a fixed point
//    more = false;
//    for( int i=0; i<_hashmax; i++ ) {
//      if( _hashtable[i] && _hashtable[i]->is_instance_klass() ) {
//        ciInstanceKlass* ik = _hashtable[i]->as_instance_klass();
//        if( !ik->_transitive_interfaces ) {
//          ik->transitive_interfaces();
//          more = true;
//        }
//      }
//    }
//  }
//

  Atomic::write_barrier(); // Must fence out contents before publishing completion
  _shared_hashmax = old_shared_table_size;
*/
}

void ciObjectFactory::init_shared_objects() {

  _next_ident = 1;  // start numbering CI objects at 1
  
ciObject*dummy=NULL;//Empty shared hashtable
  _shared_hashtable = &dummy;
  _shared_hashmax = 1;
  // Make a private hashtable for initialization
  _hashcnt = 0;                 // Empty private hash table
  _hashmax = 1024;              // Big enough for all shared objects
  _hashtable = NEW_ARENA_ARRAY(_arena,ciObject*,_hashmax);
  bzero(_hashtable,sizeof(ciObject*)*_hashmax);

  {
    // Create the shared symbols, but not in _shared_ci_objects.
    int i;
    for (i = vmSymbols::FIRST_SID; i < vmSymbols::SID_LIMIT; i++) {
      symbolHandle sym_handle = vmSymbolHandles::symbol_handle_at((vmSymbols::SID) i);
      assert(vmSymbols::find_sid(sym_handle()) == i, "1-1 mapping");
      _shared_ci_symbols[i] = get( sym_handle() )->as_symbol();
    }
#ifdef ASSERT
    for (i = vmSymbols::FIRST_SID; i < vmSymbols::SID_LIMIT; i++) {
      symbolHandle sym_handle = vmSymbolHandles::symbol_handle_at((vmSymbols::SID) i);
      ciSymbol* sym = vm_symbol_at((vmSymbols::SID) i);
      assert(sym->get_oop() == sym_handle(), "oop must match");
    }
    assert(ciSymbol::void_class_signature()->get_oop() == vmSymbols::void_class_signature(), "spot check");
#endif
  }

  for (int i = T_BOOLEAN; i <= T_CONFLICT; i++) {
    BasicType t = (BasicType)i;
    if (type2name(t) != NULL && t != T_OBJECT && t != T_ARRAY) {
      ciType::_basic_types[t] = new (_arena) ciType(t);
      init_ident_of(ciType::_basic_types[t]);
    }
  }

  ciEnv::_null_object_instance = new (_arena) ciNullObject();
  init_ident_of(ciEnv::_null_object_instance);
ciEnv::_method_klass_instance=get(Universe::methodKlassObj())->as_method_klass();
ciEnv::_symbol_klass_instance=get(Universe::symbolKlassObj())->as_symbol_klass();
ciEnv::_klass_klass_instance=get(Universe::klassKlassObj())->as_klass_klass();
ciEnv::_instance_klass_klass_instance=get(Universe::instanceKlassKlassObj())->as_instance_klass_klass();
ciEnv::_type_array_klass_klass_instance=get(Universe::typeArrayKlassKlassObj())->as_type_array_klass_klass();
ciEnv::_obj_array_klass_klass_instance=get(Universe::objArrayKlassKlassObj())->as_obj_array_klass_klass();
ciEnv::_ArrayStoreException=get(SystemDictionary::ArrayStoreException_klass())->as_instance_klass();
ciEnv::_Class=get(SystemDictionary::class_klass())->as_instance_klass();
ciEnv::_ClassCastException=get(SystemDictionary::ClassCastException_klass())->as_instance_klass();
ciEnv::_Cloneable=get(SystemDictionary::cloneable_klass())->as_instance_klass();
ciEnv::_Object=get(SystemDictionary::object_klass())->as_instance_klass();
ciEnv::_Serializable=get(SystemDictionary::serializable_klass())->as_instance_klass();
ciEnv::_Throwable=get(SystemDictionary::throwable_klass())->as_instance_klass();
ciEnv::_Thread=get(SystemDictionary::thread_klass())->as_instance_klass();
ciEnv::_OutOfMemoryError=get(SystemDictionary::OutOfMemoryError_klass())->as_instance_klass();
ciEnv::_String=get(SystemDictionary::string_klass())->as_instance_klass();

for(int len=-1;len!=_hashmax;){
len=_hashmax;
    for (int i2 = 0; i2 < len; i2++) {
ciObject*obj=_hashtable[i2];
if(obj&&obj->is_loaded()&&obj->is_instance_klass()){
        obj->as_instance_klass()->compute_nonstatic_fields();
      }
    }
  }

  ciEnv::_unloaded_cisymbol = (ciSymbol*) ciObjectFactory::get(vmSymbols::dummy_symbol_oop());
  // Create dummy instanceKlass and objArrayKlass object and assign them idents
  ciEnv::_unloaded_ciinstance_klass = new (_arena) ciInstanceKlass(ciEnv::_unloaded_cisymbol, NULL, NULL);
  init_ident_of(ciEnv::_unloaded_ciinstance_klass);
  ciEnv::_unloaded_ciobjarrayklass = new (_arena) ciObjArrayKlass(ciEnv::_unloaded_cisymbol, ciEnv::_unloaded_ciinstance_klass, 1);
  init_ident_of(ciEnv::_unloaded_ciobjarrayklass);
  assert(ciEnv::_unloaded_ciobjarrayklass->is_obj_array_klass(), "just checking");
  ciEnv::_array_ifaces = (ciInstanceKlass**)_arena->Amalloc(3*sizeof(ciInstanceKlass*));
  ciEnv::_array_ifaces[0] = ciEnv::_Cloneable;
  ciEnv::_array_ifaces[1] = ciEnv::_Serializable;
  ciEnv::_array_ifaces[2] = NULL;
#if 0
  // This part is only necessary for extra-strong asserts in C2 which have been disabled in general builds.
  // We cannot leave this here because it will allocate and potentially load classes with the Compile_lock held,
  // leading to a deadlock situation.
  EXCEPTION_MARK;
  Handle string = StringTable::intern("CI test string", CATCH);
  ciEnv::_test_string = get(string());
  ciObjArrayKlass::make(ciEnv::_String); // Also string-array for testing
#endif // 0

  get(Universe::boolArrayKlassObj());
  get(Universe::charArrayKlassObj());
  get(Universe::singleArrayKlassObj());
  get(Universe::doubleArrayKlassObj());
  get(Universe::byteArrayKlassObj());
  get(Universe::shortArrayKlassObj());
  get(Universe::intArrayKlassObj());
  get(Universe::longArrayKlassObj());



  assert(_non_perm_count == 0, "no shared non-perm objects");

  // Pre-load some transitive interfaces, so they allocate memory in the
  // shared arena and not the per-compile arena.
  bool more = true;
  while( more ) {               // I have to iterate to a fixed point
    more = false;
for(int i=0;i<_hashmax;i++){
      if( _hashtable[i] && _hashtable[i]->is_instance_klass() ) {
        ciInstanceKlass* ik = _hashtable[i]->as_instance_klass();
        if( !ik->_transitive_interfaces ) {
ik->transitive_interfaces();
          more = true;
        }
      }
    }
  }

  // The shared_ident_limit is the first ident number that will
  // be used for non-shared objects.  That is, numbers less than
  // this limit are permanently assigned to shared CI objects,
  // while the higher numbers are recycled afresh by each new ciEnv.

  _shared_ident_limit = _next_ident;

  // Move the private hashtable into the public area
  _shared_hashtable = _hashtable;

  _shared_ident_limit = _next_ident;
  // The shared_ident_limit is the first ident number that will
  // be used for non-shared objects.  That is, numbers less than
  // this limit are permanently assigned to shared CI objects,
  // while the higher numbers are recycled afresh by each new ciEnv.

  Atomic::write_barrier(); // Must fence out contents before publishing completion
  _shared_hashmax = _hashmax; // Publish it
}

// ------------------------------------------------------------------
// ciObjectFactory::get
//
// Get the ciObject corresponding to some oop.  If the ciObject has
// already been created, it is returned.  Otherwise, a new ciObject
// is created.
ciObject* ciObjectFactory::get(oop key) {
  if (FAM) {
    ShouldNotReachHere();
  }

  intptr_t h = key->identity_hash();
ciObject*p;
  // try in compile-local table
  int oldmax = _hashmax;
  int idx = h & (oldmax-1);
  while( (p=_hashtable[idx]) != NULL ) { 
    if( p->get_oop() == key ) return p;
    idx = (idx+1) & (oldmax-1);
  }

  // shared hashtable find
  if (_shared_hashmax > 0) {
    int sidx = h & (_shared_hashmax-1);
    while( (p=_shared_hashtable[sidx]) != NULL ) { 
      if( p->get_oop() == key ) return p; 
      sidx = (sidx+1) & (_shared_hashmax-1);
    }
  }

  // Missed twice, need to create object & insert it in private table
  ciObject *new_object = create_new_object(key);
  init_ident_of(new_object);

#ifdef ASSERT
  key = new_object->get_oop();  // reload after GC
  if (_shared_hashmax > 0) {
for(int i=0;i<_shared_hashmax;i++){
      assert0( !_shared_hashtable[i] || _shared_hashtable[i]->get_oop() != key );
    }
  }
for(int i=0;i<oldmax;i++){
    assert0( !_hashtable[i] || _hashtable[i]->get_oop() != key );
  }
#endif

  // "is_perm" is nearly the same as "has_encoding" which *must* hold (for C2)
  // for Class mirrors and some other special oop types.  On Azul it turns
  // into "can go in oop table" which is pretty much true for all oops.
  if (JNIHandles::resolve_non_null(new_object->handle())->is_perm()) {
    new_object->set_perm();
  }  

  // Recursive create may have re-grown table or inserted new objects;
  // must recompute idx
  oldmax = _hashmax;
  idx = h & (oldmax-1);
  while( (p=_hashtable[idx]) != NULL ) 
    idx = (idx+1) & (oldmax-1);
  _hashtable[idx] = new_object; // Insert into table
  _hashcnt++;                   // Table is now fuller

  // See if private table needs to grow
  if( (_hashcnt+_hashcnt) < oldmax ) // target a 50% table-full ratio
    return new_object;

  // Must grow table

  // Save off old table, make new table 2x bigger
  ciObject **old_t =_hashtable; // Save off old table
  int newmax = oldmax<<1;       // Double table size
  _hashmax = newmax;            // Install new empty table 2x size
  _hashtable = NEW_ARENA_ARRAY(_arena,ciObject*,newmax);
  bzero(_hashtable,sizeof(ciObject*)*newmax);
  // Copy old table into new table
for(int i=0;i<oldmax;i++){
    ciObject *p = old_t[i];
    if( p ) {
      int idx = p->get_oop()->identity_hash() & (newmax-1);
      while( _hashtable[idx] ) 
        idx = (idx+1) & (newmax-1);
      _hashtable[idx] = p;
    }
  }
  // Free old table
  FREE_ARENA_ARRAY(_arena,ciObject*,old_t,oldmax);

  return new_object;
}

// ------------------------------------------------------------------
// ciObjectFactory::create_new_object
//
// Create a new ciObject from an oop; may cause GC
//
// Implementation note: this functionality could be virtual behavior
// of the oop itself.  For now, we explicitly marshal the object.
ciObject* ciObjectFactory::create_new_object(oop o) {
  EXCEPTION_CONTEXT;

  if (o->is_symbol()) {
    symbolHandle h_o(THREAD, (symbolOop)o);
    return new (arena()) ciSymbol(h_o);
  } else if (o->is_klass()) {
    KlassHandle h_k(THREAD, (klassOop)o);
    Klass* k = ((klassOop)o)->klass_part();
    if (k->oop_is_instance()) {
      return new (arena()) ciInstanceKlass(h_k);
    } else if (k->oop_is_objArray()) {
      return new (arena()) ciObjArrayKlass(h_k);
    } else if (k->oop_is_typeArray()) {
      return new (arena()) ciTypeArrayKlass(h_k);
    } else if (k->oop_is_method()) {
      return new (arena()) ciMethodKlass(h_k);
    } else if (k->oop_is_symbol()) {
      return new (arena()) ciSymbolKlass(h_k);
    } else if (k->oop_is_klass()) {
      if (k->oop_is_objArrayKlass()) {
        return new (arena()) ciObjArrayKlassKlass(h_k);
      } else if (k->oop_is_typeArrayKlass()) {
        return new (arena()) ciTypeArrayKlassKlass(h_k);
      } else if (k->oop_is_instanceKlass()) {
        return new (arena()) ciInstanceKlassKlass(h_k);
      } else {
        assert(o == Universe::klassKlassObj(), "bad klassKlass");
        return new (arena()) ciKlassKlass(h_k);
      }
    }
  } else if (o->is_method()) {
    methodHandle h_m(THREAD, (methodOop)o);
    return new (arena()) ciMethod(h_m);
  } else if (o->is_instance()) {
    instanceHandle h_i(THREAD, (instanceOop)o);
    return new (arena()) ciInstance(h_i);
  } else if (o->is_objArray()) {
    objArrayHandle h_oa(THREAD, (objArrayOop)o);
    return new (arena()) ciObjArray(h_oa);
  } else if (o->is_typeArray()) {
    typeArrayHandle h_ta(THREAD, (typeArrayOop)o);
    return new (arena()) ciTypeArray(h_ta);
  }

  // The oop is of some type not supported by the compiler interface.
  ShouldNotReachHere();
  return NULL;
}

//------------------------------------------------------------------
// ciObjectFactory::get_unloaded_method
//
// Get the ciMethod representing an unloaded/unfound method.
//
// Implementation note: unloaded methods are currently stored in
// an unordered array, requiring a linear-time lookup for each
// unloaded method.  This may need to change.
ciMethod* ciObjectFactory::get_unloaded_method(ciInstanceKlass* holder,
                                               ciSymbol*        name,
                                               ciSymbol*        signature) {
  for (int i=0; i<_unloaded_methods->length(); i++) {
    ciMethod* entry = _unloaded_methods->at(i);
    if (entry->holder()->equals(holder) &&
        entry->name()->equals(name) &&
        entry->signature()->as_symbol()->equals(signature)) {
      // We've found a match.
      return entry;
    }
  }

  // This is a new unloaded method.  Create it and stick it in
  // the cache.
  ciMethod* new_method = new (arena()) ciMethod(holder, name, signature);

  init_ident_of(new_method);
  _unloaded_methods->append(new_method);

  return new_method;
}

//------------------------------------------------------------------
// ciObjectFactory::get_unloaded_klass
//
// Get a ciKlass representing an unloaded klass.
//
// Implementation note: unloaded klasses are currently stored in
// an unordered array, requiring a linear-time lookup for each
// unloaded klass.  This may need to change.
ciKlass* ciObjectFactory::get_unloaded_klass(ciKlass* accessing_klass,
                                             ciSymbol* name,
                                             bool create_if_not_found) {
  EXCEPTION_CONTEXT;
  oop loader = NULL;
  oop domain = NULL;
  if (accessing_klass != NULL) {
    loader = accessing_klass->loader();
    domain = accessing_klass->protection_domain();
  }
  for (int i=0; i<_unloaded_klasses->length(); i++) {
    ciKlass* entry = _unloaded_klasses->at(i);
    if (entry->name()->equals(name) &&
        entry->loader() == loader &&
        entry->protection_domain() == domain) {
      // We've found a match.
      return entry;
    }
  }

  if (!create_if_not_found)
    return NULL;

  // This is a new unloaded klass.  Create it and stick it in
  // the cache.
  ciKlass* new_klass = NULL;

  // Two cases: this is an unloaded objArrayKlass or an
  // unloaded instanceKlass.  Deal with both.
  if (name->byte_at(0) == '[') {
    // Decompose the name.'
    jint dimension = 0;
    symbolOop element_name = NULL;
    BasicType element_type= FieldType::get_array_info(name->get_symbolOop(),
                                                      &dimension,
                                                      &element_name,
                                                      THREAD);
    if (HAS_PENDING_EXCEPTION) {
      CLEAR_PENDING_EXCEPTION;
      CURRENT_THREAD_ENV->record_out_of_memory_failure();
      return ciEnv::_unloaded_ciobjarrayklass;
    }
    assert(element_type != T_ARRAY, "unsuccessful decomposition");
    ciKlass* element_klass = NULL;
    if (element_type == T_OBJECT) {
      ciEnv *env = CURRENT_THREAD_ENV;
      ciSymbol* ci_name = env->get_object(element_name)->as_symbol();
      element_klass =
        env->get_klass_by_name(accessing_klass, ci_name, false)->as_instance_klass();
    } else {
      assert(dimension > 1, "one dimensional type arrays are always loaded.");

      // The type array itself takes care of one of the dimensions.
      dimension--;

      // The element klass is a typeArrayKlass.
      element_klass = ciTypeArrayKlass::make(element_type);
    }
    new_klass = new (arena()) ciObjArrayKlass(name, element_klass, dimension);
  } else {
    jobject loader_handle = NULL;
    jobject domain_handle = NULL;
    if (accessing_klass != NULL) {
      loader_handle = accessing_klass->loader_handle();
      domain_handle = accessing_klass->protection_domain_handle();
    }
    new_klass = new (arena()) ciInstanceKlass(name, loader_handle, domain_handle);
  }
  init_ident_of(new_klass);
  _unloaded_klasses->append(new_klass);

  return new_klass;
}

//------------------------------------------------------------------
// ciObjectFactory::get_return_address
//
// Get a ciReturnAddress for a specified bci.
ciReturnAddress* ciObjectFactory::get_return_address(int bci) {
  for (int i=0; i<_return_addresses->length(); i++) {
    ciReturnAddress* entry = _return_addresses->at(i);
    if (entry->bci() == bci) {
      // We've found a match.
      return entry;
    }
  }
  
  ciReturnAddress* new_ret_addr = new (arena()) ciReturnAddress(bci);
  init_ident_of(new_ret_addr);
  _return_addresses->append(new_ret_addr);
  return new_ret_addr;
}

// ------------------------------------------------------------------
// ciObjectFactory::init_ident_of
void ciObjectFactory::init_ident_of(ciObject* obj) {
  obj->set_ident(_next_ident++);
}


// ------------------------------------------------------------------
// ciObjectFactory::is_found_at
//
// Verify that the binary seach found the given key.
bool ciObjectFactory::is_found_at(int index, oop key, GrowableArray<ciObject*>* objects) {
  return (index < objects->length() &&
	  objects->at(index)->get_oop() == key);
}


// ------------------------------------------------------------------
// ciObjectFactory::insert
//
// Insert a ciObject into the table at some index.
void ciObjectFactory::insert(int index, ciObject* obj, GrowableArray<ciObject*>* objects) {
  int len = objects->length();
  if (len == index) {
    objects->append(obj);
  } else {
    objects->append(objects->at(len-1));
    int pos;
    for (pos = len-2; pos >= index; pos--) {
      objects->at_put(pos+1,objects->at(pos));
    }
    objects->at_put(index, obj);
  }
#ifdef ASSERT
  oop last = NULL;
  for (int j = 0; j< objects->length(); j++) {
    oop o = objects->at(j)->get_oop();
    assert(last < o, "out of order");
    last = o;
  }
#endif // ASSERT
}

static ciObjectFactory::NonPermObject* emptyBucket = NULL;

// ------------------------------------------------------------------
// ciObjectFactory::find_non_perm
//
// Use a small hash table, hashed on the klass of the key.
// If there is no entry in the cache corresponding to this oop, return
// the null tail of the bucket into which the oop should be inserted.
ciObjectFactory::NonPermObject* &ciObjectFactory::find_non_perm(oop key) {
  // Be careful:  is_perm might change from false to true.
  // Thus, there might be a matching perm object in the table.
  // If there is, this probe must find it.
  if (key->is_perm() && _non_perm_count == 0) {
    return emptyBucket;
  } else if (key->is_instance()) {
    if (key->klass() == SystemDictionary::class_klass()) {
      // class mirror instances are always perm
      return emptyBucket;
    }
    // fall through to probe
  } else if (key->is_array()) {
    // fall through to probe
  } else {
    // not an array or instance
    return emptyBucket;
  }

  ciObject* klass = get(key->klass());
  NonPermObject* *bp = &_non_perm_bucket[(unsigned) klass->hash() % NON_PERM_BUCKETS];
  for (NonPermObject* p; (p = (*bp)) != NULL; bp = &p->next()) {
    if (is_equal(p, key))  break;
  }
  return (*bp);
}



// ------------------------------------------------------------------
// Code for for NonPermObject
//
inline ciObjectFactory::NonPermObject::NonPermObject(ciObjectFactory::NonPermObject* &bucket, oop key, ciObject* object) {
  assert(ciObjectFactory::is_initialized(), "");
  _object = object;
  _next = bucket;
  bucket = this;
}



// ------------------------------------------------------------------
// ciObjectFactory::insert_non_perm
//
// Insert a ciObject into the non-perm table.
void ciObjectFactory::insert_non_perm(ciObjectFactory::NonPermObject* &where, oop key, ciObject* obj) {
  assert(&where != &emptyBucket, "must not try to fill empty bucket");
  NonPermObject* p = new (arena()) NonPermObject(where, key, obj);
  assert(where == p && is_equal(p, key) && p->object() == obj, "entry must match");
  assert(find_non_perm(key) == p, "must find the same spot");
  ++_non_perm_count;
}

// ------------------------------------------------------------------
// ciObjectFactory::vm_symbol_at
// Get the ciSymbol corresponding to some index in vmSymbols.
ciSymbol* ciObjectFactory::vm_symbol_at(int index) {
  assert(index >= vmSymbols::FIRST_SID && index < vmSymbols::SID_LIMIT, "oob");
  return _shared_ci_symbols[index];
}
static ciKlass *scan_for_kid( ciObject** ptr, int max, int kid ) {
for(int i=0;i<max;i++)
    if( ptr[i] &&
        ptr[i]->is_klass() &&
        ((ciKlass*)ptr[i])->klassId() == (uint)kid )
      return (ciKlass*)ptr[i];
  return NULL;
}


ciKlass *ciObjectFactory::get_klass_by_kid (int kid) const {
  ciKlass *cik0 = scan_for_kid( _shared_hashtable, _shared_hashmax, kid );
  if( cik0 ) return cik0;
  return          scan_for_kid(        _hashtable,        _hashmax, kid );
}

// ------------------------------------------------------------------
// ciObjectFactory::print
//
// Print debugging information about the object factory
void ciObjectFactory::print() {
  Unimplemented();
/*
  tty->print("<ciObjectFactory oops=%d unloaded_methods=%d unloaded_klasses=%d>",
             _ci_objects->length(), _unloaded_methods->length(),
             _unloaded_klasses->length());
*/
}

ciObjArrayKlass*ciObjectFactory::get_ciObjArrayKlass_from_element(ciKlass*element_klass){
assert(FAM,"For FAM use only");
  Unimplemented();
/*
  for(int i=0; i<_hashmax; i++) {
    if (_hashtable[i] && _hashtable[i]->is_obj_array_klass()) {
      ciObjArrayKlass* toak = (ciObjArrayKlass*)_hashtable[i];
      if (toak->element_klass() == element_klass) {
        return (ciObjArrayKlass*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] && _shared_hashtable[i]->is_obj_array_klass()) {
      ciObjArrayKlass* toak = (ciObjArrayKlass*)_shared_hashtable[i];
      if (toak->element_klass() == element_klass) {
        return (ciObjArrayKlass*)_shared_hashtable[i];
      }
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) && _FAM_fixup_list->at(i)->is_obj_array_klass()) {
      ciObjArrayKlass* toak = (ciObjArrayKlass*)_FAM_fixup_list->at(i);
      if (toak->element_klass() == element_klass) {
        return (ciObjArrayKlass*)_FAM_fixup_list->at(i);
      }
    }
  }
*/
  return NULL;
}

ciMethod*ciObjectFactory::get_ciMethod(ciSymbol*name,ciSymbol*signature){
assert(FAM,"For FAM use only");
  Unimplemented();
  /*
  const char* namestr = name->as_utf8();
  const char* sigstr = signature->as_utf8();
  for(int i=0; i<_hashmax; i++) {
    if (_hashtable[i] && _hashtable[i]->is_method()) {
      ciSymbol* tname = ((ciMethod*)_hashtable[i])->name();
      ciSymbol* tsig  = ((ciMethod*)_hashtable[i])->signature()->as_symbol();
      if (tname && strcmp(namestr, tname->as_utf8())==0 &&
          tsig  && strcmp(sigstr,  tsig->as_utf8()) ==0) {
        return (ciMethod*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] && _shared_hashtable[i]->is_method()) {
      ciSymbol* tname = ((ciMethod*)_shared_hashtable[i])->name();
      ciSymbol* tsig  = ((ciMethod*)_shared_hashtable[i])->signature()->as_symbol();
      if (tname && strcmp(namestr, tname->as_utf8())==0 &&
          tsig  && strcmp(sigstr,  tsig->as_utf8()) ==0) {
        return (ciMethod*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) && _FAM_fixup_list->at(i)->is_method()) {
      ciSymbol* tname = ((ciMethod*)_FAM_fixup_list->at(i))->name();
      ciSymbol* tsig  = ((ciMethod*)_FAM_fixup_list->at(i))->signature()->as_symbol();
      if (tname && strcmp(namestr, tname->as_utf8())==0 &&
          tsig  && strcmp(sigstr,  tsig->as_utf8()) ==0) {
        return (ciMethod*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_unloaded_methods->length(); i++) {
    if (_unloaded_methods->at(i) && _unloaded_methods->at(i)->is_method()) {
      ciSymbol* tname = ((ciMethod*)_unloaded_methods->at(i))->name();
      ciSymbol* tsig  = ((ciMethod*)_unloaded_methods->at(i))->signature()->as_symbol();
      if (tname && strcmp(namestr, tname->as_utf8())==0 &&
          tsig  && strcmp(sigstr,  tsig->as_utf8()) ==0) {
        return (ciMethod*)_hashtable[i];
      }
    }
  }
  */
  return NULL;
}

ciKlass *ciObjectFactory::get_ciKlass(ciSymbol* klassname) {
assert(FAM,"For FAM use only");
  Unimplemented();
/*
  const char* str = klassname->as_utf8();
  for(int i=0; i<_hashmax; i++) {
    if (_hashtable[i] && _hashtable[i]->is_klass()) {
      ciSymbol* sym = ((ciKlass*)_hashtable[i])->name();
      if (sym && strcmp(str, sym->as_utf8())==0) {
        return (ciKlass*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] && _shared_hashtable[i]->is_klass()) {
      ciSymbol* sym = ((ciKlass*)_shared_hashtable[i])->name();
      if (sym && strcmp(str, sym->as_utf8())==0) {
        return (ciKlass*)_shared_hashtable[i];
      }
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) && _FAM_fixup_list->at(i)->is_klass()) {
      ciSymbol* sym = ((ciKlass*)_FAM_fixup_list->at(i))->name();
      if (sym && strcmp(str, sym->as_utf8())==0) {
        return (ciKlass*)_FAM_fixup_list->at(i);
      }
    }
  }
  for(int i=0; i<_unloaded_klasses->length(); i++) {
    if (_unloaded_klasses->at(i) && _unloaded_klasses->at(i)->is_klass()) {
      ciSymbol* sym = ((ciKlass*)_unloaded_klasses->at(i))->name();
      if (sym && strcmp(str, sym->as_utf8())==0) {
        return (ciKlass*)_unloaded_klasses->at(i);
      }
    }
  }
*/
  return NULL;
}

ciSymbol *ciObjectFactory::get_symbol_from_string(const char* str) {
assert(FAM,"For FAM use only");
  Unimplemented();
/*
  for(int i=0; i<_hashmax; i++) {
    if (_hashtable[i] && _hashtable[i]->is_symbol()) {
      if (strcmp(str, ((ciSymbol*)_hashtable[i])->as_utf8())==0) {
        return (ciSymbol*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] && _shared_hashtable[i]->is_symbol()) {
      if (strcmp(str, ((ciSymbol*)_shared_hashtable[i])->as_utf8())==0) {
        return (ciSymbol*)_shared_hashtable[i];
      }
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) && _FAM_fixup_list->at(i)->is_symbol()) {
      if (strcmp(str, ((ciSymbol*)_FAM_fixup_list->at(i))->as_utf8())==0) {
        return (ciSymbol*)_FAM_fixup_list->at(i);
      }
    }
  }
*/
  return NULL;
}

ciTypeArrayKlass *ciObjectFactory::get_ciTypeArrayKlass(BasicType bt) {
assert(FAM,"For FAM use only");
  Unimplemented();
/*
  for(int i=0; i<_hashmax; i++) {
    if (_hashtable[i] && _hashtable[i]->is_type_array_klass()) {
      if (bt == ((ciTypeArrayKlass*)_hashtable[i])->element_type()) {
        return (ciTypeArrayKlass*)_hashtable[i];
      }
    }
  }
  for(int i=0; i<_shared_hashmax; i++) {
    if (_shared_hashtable[i] && _shared_hashtable[i]->is_type_array_klass()) {
      if (bt == ((ciTypeArrayKlass*)_shared_hashtable[i])->element_type()) {
        return (ciTypeArrayKlass*)_shared_hashtable[i];
      }
    }
  }
  for(int i=0; i<_FAM_fixup_list->length(); i++) {
    if (_FAM_fixup_list->at(i) && _FAM_fixup_list->at(i)->is_type_array_klass()) {
      if (bt == ((ciTypeArrayKlass*)_FAM_fixup_list->at(i))->element_type()) {
        return (ciTypeArrayKlass*)_FAM_fixup_list->at(i);
      }
    }
  }
*/
  return NULL;
}
