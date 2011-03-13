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


#include "arrayKlassKlass.hpp"
#include "classLoader.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "collectorPolicy.hpp"
#include "compactingPermGenGen.hpp"
#include "constMethodKlass.hpp"
#include "constantPoolKlass.hpp"
#include "cpCacheKlass.hpp"
#include "exceptions.hpp"
#include "filemap.hpp"
#include "gcLocker.hpp"
#include "genCollectedHeap.hpp"
#include "gpgc_heap.hpp"
#include "instanceKlassKlass.hpp"
#include "instanceRefKlass.hpp"
#include "interpreter_pd.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jniHandles.hpp"
#include "klass.hpp"
#include "klassKlass.hpp"
#include "markWord.hpp"
#include "methodCodeKlass.hpp"
#include "methodKlass.hpp"
#include "methodOop.hpp"
#include "mutexLocker.hpp"
#include "objArrayKlass.hpp"
#include "objArrayKlassKlass.hpp"
#include "objectRef_pd.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "parallelScavengeHeap.hpp"
#include "preserveException.hpp"
#include "refsHierarchy_pd.hpp"
#include "resourceArea.hpp"
#include "safepoint.hpp"
#include "symbolKlass.hpp"
#include "symbolTable.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "typeArrayKlass.hpp"
#include "typeArrayKlassKlass.hpp"
#include "universe.hpp"
#include "vmError.hpp"
#include "vmSymbols.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

// Known objects
klassRef Universe::_boolArrayKlassObj;
klassRef Universe::_byteArrayKlassObj;
klassRef Universe::_charArrayKlassObj;
klassRef Universe::_intArrayKlassObj;
klassRef Universe::_shortArrayKlassObj;
klassRef Universe::_longArrayKlassObj;
klassRef Universe::_singleArrayKlassObj;
klassRef Universe::_doubleArrayKlassObj;
klassRef Universe::_typeArrayKlassObjs[T_VOID+1];
klassRef Universe::_objectArrayKlassObj;
klassRef Universe::_symbolKlassObj;
klassRef Universe::_methodKlassObj;
klassRef Universe::_methodCodeKlassObj;
klassRef Universe::_constMethodKlassObj;
klassRef Universe::_klassKlassObj;
klassRef Universe::_arrayKlassKlassObj;
klassRef Universe::_objArrayKlassKlassObj;
klassRef Universe::_typeArrayKlassKlassObj;
klassRef Universe::_instanceKlassKlassObj;
klassRef Universe::_constantPoolKlassObj;
klassRef Universe::_constantPoolCacheKlassObj;
klassRef Universe::_systemObjArrayKlassObj;

objectRef Universe::_int_mirror;
objectRef Universe::_float_mirror;
objectRef Universe::_double_mirror;
objectRef Universe::_byte_mirror;
objectRef Universe::_bool_mirror;
objectRef Universe::_char_mirror;
objectRef Universe::_long_mirror;
objectRef Universe::_short_mirror;
objectRef Universe::_void_mirror;
objectRef Universe::_mirrors[T_VOID+1];


objectRef Universe::_main_thread_group;
objectRef Universe::_system_thread_group;
jobject Universe::_system_thread_group_handle;
typeArrayRef Universe::_the_empty_byte_array;
typeArrayRef Universe::_the_empty_short_array;
typeArrayRef Universe::_the_empty_int_array;
objArrayRef Universe::_the_empty_system_obj_array;
objArrayRef Universe::_the_empty_class_klass_array;
objArrayRef Universe::_the_array_interfaces_array;
LatestMethodOopCache* Universe::_finalizer_register_cache = NULL;
LatestMethodOopCache* Universe::_loader_addClass_cache    = NULL;
ActiveMethodOopsCache* Universe::_reflect_invoke_cache    = NULL;
objectRef Universe::_out_of_memory_error_java_heap;
objectRef Universe::_out_of_memory_error_perm_gen;
objectRef Universe::_out_of_memory_error_array_size;
objectRef Universe::_out_of_memory_error_gc_overhead_limit;
objArrayRef Universe::_preallocated_out_of_memory_error_array;
volatile jlong Universe::_preallocated_out_of_memory_error_avail_count=0;
bool Universe::_verify_in_progress                    = false;
objectRef Universe::_null_ptr_exception_instance;
objectRef Universe::_aioob_exception_instance;
objectRef Universe::_arithmetic_exception_instance;
objectRef Universe::_the_keep_methodCode_klass_alive_instance;
objectRef Universe::_virtual_machine_error_instance;
objectRef Universe::_vm_exception;
objectRef Universe::_emptySymbol;

// These variables are guarded by FullGCALot_lock.
debug_only(objArrayRef Universe::_fullgc_alot_dummy_array;)
debug_only(int Universe::_fullgc_alot_dummy_next      = 0;)


// Heap  
int             Universe::_verify_count = 0;

int             Universe::_base_vtable_size = 0;
bool            Universe::_bootstrapping = false;
bool            Universe::_fully_initialized = false;

size_t          Universe::_heap_capacity_at_last_gc;
size_t          Universe::_heap_used_at_last_gc;

CollectedHeap*  Universe::_collectedHeap = NULL;


void Universe::basic_type_classes_do(void f(klassOop)) {
  f(boolArrayKlassObj());
  f(byteArrayKlassObj());
  f(charArrayKlassObj());
  f(intArrayKlassObj());
  f(shortArrayKlassObj());
  f(longArrayKlassObj());
  f(singleArrayKlassObj());
  f(doubleArrayKlassObj());
}


void Universe::system_classes_do(void f(klassOop)) {
  f(symbolKlassObj());
  f(methodKlassObj());
  f(constMethodKlassObj());
  f(klassKlassObj());
  f(arrayKlassKlassObj());
  f(objArrayKlassKlassObj());
  f(typeArrayKlassKlassObj());
  f(instanceKlassKlassObj());
  f(constantPoolKlassObj());
  f(systemObjArrayKlassObj());
}

void Universe::oops_do(OopClosure* f, bool do_all) {
  // These *_mirror objects need to be processed early on 
  // when class data sharing is enabled.
  f->do_oop((objectRef*) &_int_mirror);
  f->do_oop((objectRef*) &_float_mirror);
  f->do_oop((objectRef*) &_double_mirror);
  f->do_oop((objectRef*) &_byte_mirror);
  f->do_oop((objectRef*) &_bool_mirror);
  f->do_oop((objectRef*) &_char_mirror);
  f->do_oop((objectRef*) &_long_mirror);
  f->do_oop((objectRef*) &_short_mirror);
  f->do_oop((objectRef*) &_void_mirror);

  // It's important to iterate over these guys even if they are null,
  // since that's how shared heaps are restored.
  for (int i = T_BOOLEAN; i < T_VOID+1; i++) {
    f->do_oop((objectRef*) &_mirrors[i]);
  }
assert(_mirrors[0].is_null()&&_mirrors[T_BOOLEAN-1].is_null(),"checking");

  // %%% Consider moving those "shared oops" over here with the others.
  f->do_oop((objectRef*)&_boolArrayKlassObj);
  f->do_oop((objectRef*)&_byteArrayKlassObj);
  f->do_oop((objectRef*)&_charArrayKlassObj);
  f->do_oop((objectRef*)&_intArrayKlassObj);
  f->do_oop((objectRef*)&_shortArrayKlassObj);
  f->do_oop((objectRef*)&_longArrayKlassObj);  
  f->do_oop((objectRef*)&_singleArrayKlassObj);
  f->do_oop((objectRef*)&_doubleArrayKlassObj);
  f->do_oop((objectRef*)&_objectArrayKlassObj);
  {
    for (int i = 0; i < T_VOID+1; i++) {
if(_typeArrayKlassObjs[i].not_null()){
        assert(i >= T_BOOLEAN, "checking");
f->do_oop((objectRef*)(&_typeArrayKlassObjs[i]));
      } else if (do_all) {
        f->do_oop((objectRef*)&_typeArrayKlassObjs[i]);
      }
    }
  }
  f->do_oop((objectRef*)&_symbolKlassObj);
  f->do_oop((objectRef*)&_methodKlassObj);
f->do_oop((objectRef*)&_methodCodeKlassObj);
  f->do_oop((objectRef*)&_constMethodKlassObj);
  f->do_oop((objectRef*)&_klassKlassObj);
  f->do_oop((objectRef*)&_arrayKlassKlassObj);
  f->do_oop((objectRef*)&_objArrayKlassKlassObj);
  f->do_oop((objectRef*)&_typeArrayKlassKlassObj);
  f->do_oop((objectRef*)&_instanceKlassKlassObj);
  f->do_oop((objectRef*)&_constantPoolKlassObj);
  f->do_oop((objectRef*)&_constantPoolCacheKlassObj);
  f->do_oop((objectRef*)&_systemObjArrayKlassObj);
  f->do_oop((objectRef*)&_the_empty_byte_array);
  f->do_oop((objectRef*)&_the_empty_short_array);
  f->do_oop((objectRef*)&_the_empty_int_array);
  f->do_oop((objectRef*)&_the_empty_system_obj_array);    
  f->do_oop((objectRef*)&_the_empty_class_klass_array);    
  f->do_oop((objectRef*)&_the_array_interfaces_array);    
  _finalizer_register_cache->oops_do(f);
  _loader_addClass_cache->oops_do(f);
  _reflect_invoke_cache->oops_do(f);
  f->do_oop((objectRef*)&_out_of_memory_error_java_heap);
  f->do_oop((objectRef*)&_out_of_memory_error_perm_gen);
  f->do_oop((objectRef*)&_out_of_memory_error_array_size);
  f->do_oop((objectRef*)&_out_of_memory_error_gc_overhead_limit);
if(_preallocated_out_of_memory_error_array.not_null()){//NULL when DumpSharedSpaces
    f->do_oop((objectRef*)&_preallocated_out_of_memory_error_array);
  }
  f->do_oop((objectRef*)&_null_ptr_exception_instance);
f->do_oop((objectRef*)&_aioob_exception_instance);
  f->do_oop((objectRef*)&_arithmetic_exception_instance);
  f->do_oop((objectRef*)&_virtual_machine_error_instance);
f->do_oop((objectRef*)&_the_keep_methodCode_klass_alive_instance);
  f->do_oop((objectRef*)&_main_thread_group);
  f->do_oop((objectRef*)&_system_thread_group);
  f->do_oop((objectRef*)&_vm_exception);
  f->do_oop((objectRef*)&_emptySymbol);
  debug_only(f->do_oop((objectRef*)&_fullgc_alot_dummy_array);)
}


void Universe::check_alignment(uintx size, uintx alignment, const char* name) {
  if (size < alignment || size % alignment != 0) {
    ResourceMark rm;
    stringStream st;
    st.print("Size of %s (%ld bytes) must be aligned to %ld bytes", name, size, alignment);
    char* error = st.as_string();
    vm_exit_during_initialization(error);
  }
}


void Universe::genesis(TRAPS) {
  ResourceMark rm;
  HandleMark hm;
  { FlagSetting fs(_bootstrapping, true);
      
    { MutexLockerAllowGC mc(Compile_lock, JavaThread::current());

      // determine base vtable size; without that we cannot create the array klasses
      compute_base_vtable_size();

      {
        klassOop k              = klassKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_klassKlassObj, klassRef(k));
k=arrayKlassKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_arrayKlassKlassObj, klassRef(k));

k=objArrayKlassKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_objArrayKlassKlassObj, klassRef(k));
k=instanceKlassKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_instanceKlassKlassObj, klassRef(k));
k=typeArrayKlassKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_typeArrayKlassKlassObj, klassRef(k));

k=symbolKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_symbolKlassObj, klassRef(k));

        symbolOop s             = oopFactory::new_symbol("", CHECK);
        POISON_AND_STORE_REF(&_emptySymbol, symbolRef(s));

k=typeArrayKlass::create_klass(T_BOOLEAN,sizeof(jboolean),CHECK);
        POISON_AND_STORE_REF(&_boolArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_CHAR,sizeof(jchar),CHECK);
        POISON_AND_STORE_REF(&_charArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_FLOAT,sizeof(jfloat),CHECK);
        POISON_AND_STORE_REF(&_singleArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_DOUBLE,sizeof(jdouble),CHECK);
        POISON_AND_STORE_REF(&_doubleArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_BYTE,sizeof(jbyte),CHECK);
        POISON_AND_STORE_REF(&_byteArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_SHORT,sizeof(jshort),CHECK);
        POISON_AND_STORE_REF(&_shortArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_INT,sizeof(jint),CHECK);
        POISON_AND_STORE_REF(&_intArrayKlassObj, klassRef(k));
k=typeArrayKlass::create_klass(T_LONG,sizeof(jlong),CHECK);
        POISON_AND_STORE_REF(&_longArrayKlassObj, klassRef(k));

        _typeArrayKlassObjs[T_BOOLEAN] = _boolArrayKlassObj;
        _typeArrayKlassObjs[T_CHAR]    = _charArrayKlassObj;
        _typeArrayKlassObjs[T_FLOAT]   = _singleArrayKlassObj;
        _typeArrayKlassObjs[T_DOUBLE]  = _doubleArrayKlassObj;
        _typeArrayKlassObjs[T_BYTE]    = _byteArrayKlassObj;
        _typeArrayKlassObjs[T_SHORT]   = _shortArrayKlassObj;
        _typeArrayKlassObjs[T_INT]     = _intArrayKlassObj;
        _typeArrayKlassObjs[T_LONG]    = _longArrayKlassObj;

k=methodKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_methodKlassObj, klassRef(k));
k=methodCodeKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_methodCodeKlassObj, klassRef(k));
        methodCodeOop nmo = oopFactory::new_methodCode(NULL,NULL,NULL,NULL,NULL,NULL,0,0,false,NULL,CHECK);
        POISON_AND_STORE_REF(&_the_keep_methodCode_klass_alive_instance, objectRef(nmo));
k=constMethodKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_constMethodKlassObj, klassRef(k));
k=constantPoolKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_constantPoolKlassObj, klassRef(k));
k=constantPoolCacheKlass::create_klass(CHECK);
        POISON_AND_STORE_REF(&_constantPoolCacheKlassObj, klassRef(k));

k=objArrayKlassKlass::cast(objArrayKlassKlassObj())->allocate_system_objArray_klass(CHECK);
        POISON_AND_STORE_REF(&_systemObjArrayKlassObj, klassRef(k));

        typeArrayOop ta             = oopFactory::new_permanent_byteArray(0, CHECK);
        POISON_AND_STORE_REF(&_the_empty_byte_array, typeArrayRef(ta));
ta=oopFactory::new_permanent_shortArray(0,CHECK);
        POISON_AND_STORE_REF(&_the_empty_short_array, typeArrayRef(ta));
ta=oopFactory::new_permanent_intArray(0,CHECK);
        POISON_AND_STORE_REF(&_the_empty_int_array, typeArrayRef(ta));
        objArrayOop oa              = oopFactory::new_system_objArray(0, CHECK);
        POISON_AND_STORE_REF(&_the_empty_system_obj_array, objArrayRef(oa));

oa=oopFactory::new_system_objArray(2,CHECK);
        POISON_AND_STORE_REF(&_the_array_interfaces_array, objArrayRef(oa));
s=oopFactory::new_symbol("vm exception holder",CHECK);
        POISON_AND_STORE_REF(&_vm_exception, symbolRef(s));
      }
    }

    vmSymbols::initialize(CHECK);

    SystemDictionary::initialize(CHECK);

    klassOop ok = SystemDictionary::object_klass();

    {
      // Set up shared interfaces array.  (Do this before supers are set up.)
the_array_interfaces_array()->obj_at_put(0,SystemDictionary::cloneable_klass());
the_array_interfaces_array()->obj_at_put(1,SystemDictionary::serializable_klass());

      // Set element klass for system obj array klass
objArrayKlass::cast(systemObjArrayKlassObj())->set_element_klass(ok);
objArrayKlass::cast(systemObjArrayKlassObj())->set_bottom_klass(ok);

      // Set super class for the classes created above
      Klass::cast(boolArrayKlassObj()     )->initialize_supers(ok, CHECK);
      Klass::cast(charArrayKlassObj()     )->initialize_supers(ok, CHECK);
      Klass::cast(singleArrayKlassObj()   )->initialize_supers(ok, CHECK);
      Klass::cast(doubleArrayKlassObj()   )->initialize_supers(ok, CHECK);
      Klass::cast(byteArrayKlassObj()     )->initialize_supers(ok, CHECK);
      Klass::cast(shortArrayKlassObj()    )->initialize_supers(ok, CHECK);
      Klass::cast(intArrayKlassObj()      )->initialize_supers(ok, CHECK);
      Klass::cast(longArrayKlassObj()     )->initialize_supers(ok, CHECK);
      Klass::cast(constantPoolKlassObj()  )->initialize_supers(ok, CHECK);
      Klass::cast(systemObjArrayKlassObj())->initialize_supers(ok, CHECK);
      Klass::cast(boolArrayKlassObj()     )->set_super(ok);
      Klass::cast(charArrayKlassObj()     )->set_super(ok);
      Klass::cast(singleArrayKlassObj()   )->set_super(ok);
      Klass::cast(doubleArrayKlassObj()   )->set_super(ok);
      Klass::cast(byteArrayKlassObj()     )->set_super(ok);
      Klass::cast(shortArrayKlassObj()    )->set_super(ok);
      Klass::cast(intArrayKlassObj()      )->set_super(ok);
      Klass::cast(longArrayKlassObj()     )->set_super(ok);
      Klass::cast(constantPoolKlassObj()  )->set_super(ok);
      Klass::cast(systemObjArrayKlassObj())->set_super(ok);
    }

    Klass::cast(boolArrayKlassObj()     )->append_to_sibling_list();
    Klass::cast(charArrayKlassObj()     )->append_to_sibling_list();
    Klass::cast(singleArrayKlassObj()   )->append_to_sibling_list();
    Klass::cast(doubleArrayKlassObj()   )->append_to_sibling_list();
    Klass::cast(byteArrayKlassObj()     )->append_to_sibling_list();
    Klass::cast(shortArrayKlassObj()    )->append_to_sibling_list();
    Klass::cast(intArrayKlassObj()      )->append_to_sibling_list();
    Klass::cast(longArrayKlassObj()     )->append_to_sibling_list();
    Klass::cast(constantPoolKlassObj()  )->append_to_sibling_list();
    Klass::cast(systemObjArrayKlassObj())->append_to_sibling_list();
  } // end of core bootstrapping

  // Initialize _objectArrayKlass after core bootstraping to make
  // sure the super class is set up properly for _objectArrayKlass.
  klassRef oak = Klass::array_klass(SystemDictionary::object_klassRef(), 1, CHECK);
  POISON_AND_STORE_REF(&_objectArrayKlassObj, oak);
  // Add the class to the class hierarchy manually to make sure that
  // its vtable is initialized after core bootstrapping is completed.
Klass::cast(objectArrayKlassObj())->append_to_sibling_list();

  // Compute is_jdk version flags. 
  if (JDK_Version::is_pre_jdk16_version()) {
    JDK_Version::set_jdk15x_version();
  }

  #ifdef ASSERT
  if (FullGCALot) {
    // Allocate an array of dummy objects.
    // We'd like these to be at the bottom of the old generation,
    // so that when we free one and then collect,
    // (almost) the whole heap moves
    // and we find out if we actually update all the oops correctly.
    // But we can't allocate directly in the old generation,
    // so we allocate wherever, and hope that the first collection
    // moves these objects to the bottom of the old generation.
    // We can allocate directly in the permanent generation, so we do.
    int size;
    if (UseGenPauselessGC) {
warning("Using +FullGCALot with Pauseless gc "
              "will not force all objects to relocate");
      size = FullGCALotDummies;
    } else {
      size = FullGCALotDummies * 2;
    }
    objArrayOop    naked_array = oopFactory::new_system_objArray(size, CHECK);
    objArrayHandle dummy_array(THREAD, naked_array);
    int i = 0;
    while (i < size) {
      if ( !UseGenPauselessGC ) {
        // Allocate dummy in old generation
oop dummy=instanceKlass::cast(SystemDictionary::object_klass())->allocate_instance(false/*No SBA*/,CHECK);
        dummy_array->obj_at_put(i++, dummy);
      }
      // Allocate dummy in permanent generation
      oop dummy = instanceKlass::cast(SystemDictionary::object_klass())->allocate_permanent_instance(CHECK);
      dummy_array->obj_at_put(i++, dummy);
    }
    {
      // Only modify the global variable inside the mutex.
      // If we had a race to here, the other dummy_array instances
      // and their elements just get dropped on the floor, which is fine.
      MutexLocker ml(FullGCALot_lock);
if(_fullgc_alot_dummy_array.is_null()){
POISON_AND_STORE_REF(&_fullgc_alot_dummy_array,objArrayRef(dummy_array()));
      }
    }
  }
  #endif
}    

void Universe::set_system_thread_group(oop group) {
  POISON_AND_STORE_REF(&_system_thread_group, objectRef(group));
_system_thread_group_handle=JNIHandles::make_global(group);
}

static inline void add_vtable(void** list, int* n, Klass* o, int count) {
  list[(*n)++] = *(void**)&o->vtbl_value();
  guarantee((*n) <= count, "vtable list too small.");
}


void Universe::init_self_patching_vtbl_list(void** list, int count) {
  int n = 0;
  { klassKlass o;             add_vtable(list, &n, &o, count); }
  { arrayKlassKlass o;        add_vtable(list, &n, &o, count); }
  { objArrayKlassKlass o;     add_vtable(list, &n, &o, count); }
  { instanceKlassKlass o;     add_vtable(list, &n, &o, count); }
  { instanceKlass o;          add_vtable(list, &n, &o, count); }
  { instanceRefKlass o;       add_vtable(list, &n, &o, count); }
  { typeArrayKlassKlass o;    add_vtable(list, &n, &o, count); }
  { symbolKlass o;            add_vtable(list, &n, &o, count); }
  { typeArrayKlass o;         add_vtable(list, &n, &o, count); }
  { methodKlass o;            add_vtable(list, &n, &o, count); }
  { constMethodKlass o;       add_vtable(list, &n, &o, count); }
  { constantPoolKlass o;      add_vtable(list, &n, &o, count); }
  { constantPoolCacheKlass o; add_vtable(list, &n, &o, count); }
  { objArrayKlass o;          add_vtable(list, &n, &o, count); }
}


class FixupMirrorClosure: public ObjectClosure {
 public:
  void do_object(oop obj) {
    if (obj->is_klass()) {
      EXCEPTION_MARK;
      KlassHandle k(THREAD, klassOop(obj));
      // We will never reach the CATCH below since Exceptions::_throw will cause
      // the VM to exit if an exception is thrown during initialization
      java_lang_Class::create_mirror(k, CATCH);
      // This call unconditionally creates a new mirror for k,
      // and links in k's component_mirror field if k is an array.
      // If k is an objArray, k's element type must already have
      // a mirror.  In other words, this closure must process
      // the component type of an objArray k before it processes k.
      // This works because the permgen iterator presents arrays
      // and their component types in order of creation.
    }
  }
};

void Universe::initialize_basic_type_mirrors(TRAPS) { 
assert(_int_mirror.is_null(),"basic type mirrors already initialized");
oop k=java_lang_Class::create_basic_type_mirror("int",T_INT,CHECK);
POISON_AND_STORE_REF(&_int_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("float",T_FLOAT,CHECK);
POISON_AND_STORE_REF(&_float_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("double",T_DOUBLE,CHECK);
POISON_AND_STORE_REF(&_double_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("byte",T_BYTE,CHECK);
POISON_AND_STORE_REF(&_byte_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("boolean",T_BOOLEAN,CHECK);
POISON_AND_STORE_REF(&_bool_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("char",T_CHAR,CHECK);
POISON_AND_STORE_REF(&_char_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("long",T_LONG,CHECK);
POISON_AND_STORE_REF(&_long_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("short",T_SHORT,CHECK);
POISON_AND_STORE_REF(&_short_mirror,objectRef(k));
k=java_lang_Class::create_basic_type_mirror("void",T_VOID,CHECK);
POISON_AND_STORE_REF(&_void_mirror,objectRef(k));

  _mirrors[T_INT]     = _int_mirror;
  _mirrors[T_FLOAT]   = _float_mirror;
  _mirrors[T_DOUBLE]  = _double_mirror;
  _mirrors[T_BYTE]    = _byte_mirror;
  _mirrors[T_BOOLEAN] = _bool_mirror;
  _mirrors[T_CHAR]    = _char_mirror;
  _mirrors[T_LONG]    = _long_mirror;
  _mirrors[T_SHORT]   = _short_mirror;
  _mirrors[T_VOID]    = _void_mirror;
  //_mirrors[T_OBJECT]  = instanceKlass::cast(_object_klass)->java_mirror();
  //_mirrors[T_ARRAY]   = instanceKlass::cast(_object_klass)->java_mirror();
}



void Universe::fixup_mirrors(TRAPS) {
  // Bootstrap problem: all classes gets a mirror (java.lang.Class instance) assigned eagerly,
  // but we cannot do that for classes created before java.lang.Class is loaded. Here we simply
  // walk over permanent objects created so far (mostly classes) and fixup their mirrors. Note
  // that the number of objects allocated at this point is very small.
  assert(SystemDictionary::class_klass_loaded(), "java.lang.Class should be loaded");
  FixupMirrorClosure blk;
  Universe::heap()->permanent_object_iterate(&blk);
}


static bool has_run_finalizers_on_exit = false;

void Universe::run_finalizers_on_exit() {
  if (has_run_finalizers_on_exit) return;
  has_run_finalizers_on_exit = true;

  // Called on VM exit. This ought to be run in a separate thread.
if(TraceReferenceGC)gclog_or_tty->print_cr("Callback to run finalizers on exit");
  { 
    PRESERVE_EXCEPTION_MARK;
    KlassHandle finalizer_klass(THREAD, SystemDictionary::finalizer_klass());
    JavaValue result(T_VOID);
    JavaCalls::call_static(
      &result, 
      finalizer_klass, 
      vmSymbolHandles::run_finalizers_on_exit_name(), 
      vmSymbolHandles::void_method_signature(),
      THREAD
    );
    // Ignore any pending exceptions
    CLEAR_PENDING_EXCEPTION;
  }
}


// initialize_vtable could cause gc if
// 1) we specified true to initialize_vtable and
// 2) this ran after gc was enabled
// In case those ever change we use handles for oops
void Universe::reinitialize_vtable_of(KlassHandle k_h, TRAPS) {  
  // init vtable of k and all subclasses
  Klass* ko = k_h()->klass_part();
  klassVtable* vt = ko->vtable();
  if (vt) vt->initialize_vtable(false, CHECK);
  if (ko->oop_is_instance()) {
    instanceKlass* ik = (instanceKlass*)ko;
    for (KlassHandle s_h(THREAD, ik->subklass()); s_h() != NULL; s_h = (THREAD, s_h()->klass_part()->next_sibling())) {
      reinitialize_vtable_of(s_h, CHECK);
    }
  }
}


void initialize_itable_for_klass(klassOop k, TRAPS) {
  instanceKlass::cast(k)->itable()->initialize_itable(false, CHECK);        
}


void Universe::reinitialize_itables(TRAPS) {
  SystemDictionary::classes_do(initialize_itable_for_klass, CHECK);

}


bool Universe::on_page_boundary(void* addr) {
  return ((uintptr_t) addr) % os::vm_page_size() == 0;
}


bool Universe::should_fill_in_stack_trace(Handle throwable) {
  // never attempt to fill in the stack trace of preallocated errors that do not have
  // backtrace. These errors are kept alive forever and may be "re-used" when all
  // preallocated errors with backtrace have been consumed. Also need to avoid
  // a potential loop which could happen if an out of memory occurs when attempting
  // to allocate the backtrace.
return((throwable()!=lvb_ref(&Universe::_out_of_memory_error_java_heap).as_oop())&&
(throwable()!=lvb_ref(&Universe::_out_of_memory_error_perm_gen).as_oop())&&
(throwable()!=lvb_ref(&Universe::_out_of_memory_error_array_size).as_oop())&&
(throwable()!=lvb_ref(&Universe::_out_of_memory_error_gc_overhead_limit).as_oop()));
}

oop Universe::gen_out_of_memory_error(objectRef *default_err) {
  // generate an out of memory error:
  // - if there is a preallocated error with backtrace available then return it wth
  //   a filled in stack trace.
  // - if there are no preallocated errors with backtrace available then return
  //   an error without backtrace.
  int next;
  if (_preallocated_out_of_memory_error_avail_count > 0) {
next=(int)(intptr_t)Atomic::add_ptr(-1,&_preallocated_out_of_memory_error_avail_count);
    assert(next < (int)PreallocatedOutOfMemoryErrorCount, "avail count is corrupt");
  } else {
    next = -1;
  }
  objectRef default_err_ref = lvb_ref(default_err);
  if (next < 0) {
    // all preallocated errors have been used.
    // return default
    return default_err_ref.as_oop();
  } else {
    // get the error object at the slot and set set it to NULL so that the
    // array isn't keeping it alive anymore.
    oop exc = preallocated_out_of_memory_errors()->obj_at(next);
    assert(exc != NULL, "slot has been used already");
    preallocated_out_of_memory_errors()->obj_at_put(next, NULL);

    // use the message from the default error
objectRef msg=java_lang_Throwable::message(default_err_ref);
assert(msg.not_null(),"no message");
    java_lang_Throwable::set_message(exc, msg);

    // populate the stack trace and return it.
    java_lang_Throwable::fill_in_stack_trace_of_preallocated_backtrace(exc);
    return exc;
  }
}

void universe_static_init() {
  Universe::_boolArrayKlassObj                      = nullRef;
  Universe::_byteArrayKlassObj                      = nullRef;
  Universe::_charArrayKlassObj                      = nullRef;
  Universe::_intArrayKlassObj                       = nullRef;
  Universe::_shortArrayKlassObj                     = nullRef;
  Universe::_longArrayKlassObj                      = nullRef;
  Universe::_singleArrayKlassObj                    = nullRef;
  Universe::_doubleArrayKlassObj                    = nullRef;
  {
    for (int i = 0; i < T_VOID+1; i++) {
      Universe::_typeArrayKlassObjs[i]              = nullRef;
    }
  }
  Universe::_objectArrayKlassObj                    = nullRef;
  Universe::_symbolKlassObj                         = nullRef;
  Universe::_methodKlassObj                         = nullRef;
  Universe::_methodCodeKlassObj                     = nullRef;
  Universe::_constMethodKlassObj                    = nullRef;
  Universe::_klassKlassObj                          = nullRef;
  Universe::_arrayKlassKlassObj                     = nullRef;
  Universe::_objArrayKlassKlassObj                  = nullRef;
  Universe::_typeArrayKlassKlassObj                 = nullRef;
  Universe::_instanceKlassKlassObj                  = nullRef;
  Universe::_constantPoolKlassObj                   = nullRef;
  Universe::_constantPoolCacheKlassObj              = nullRef;
  Universe::_systemObjArrayKlassObj                 = nullRef;
  Universe::_int_mirror                             =  nullRef;
  Universe::_float_mirror                           =  nullRef;
  Universe::_double_mirror                          =  nullRef;
  Universe::_byte_mirror                            =  nullRef;
  Universe::_bool_mirror                            =  nullRef;
  Universe::_char_mirror                            =  nullRef;
  Universe::_long_mirror                            =  nullRef;
  Universe::_short_mirror                           =  nullRef;
  Universe::_void_mirror                            =  nullRef;
  {
    for (int i = 0; i < T_VOID+1; i++) {
      Universe::_mirrors[i] = nullRef;
    }
  }
  Universe::_main_thread_group                      = nullRef;
  Universe::_system_thread_group                    = nullRef;
  Universe::_system_thread_group_handle             = NULL;
  Universe::_the_empty_short_array                  = nullRef;
  Universe::_the_empty_int_array                    = nullRef;
  Universe::_the_empty_system_obj_array             = nullRef;
  Universe::_the_empty_class_klass_array            = nullRef;
  Universe::_the_array_interfaces_array             = nullRef;
  Universe::_out_of_memory_error_java_heap          = nullRef;
  Universe::_out_of_memory_error_perm_gen           = nullRef;
  Universe::_out_of_memory_error_array_size         = nullRef;
  Universe::_preallocated_out_of_memory_error_array = nullRef;
  Universe::_null_ptr_exception_instance            = nullRef;
  Universe::_aioob_exception_instance               = nullRef;
  Universe::_arithmetic_exception_instance          = nullRef;
  Universe::_vm_exception                           = nullRef;
  Universe::_the_keep_methodCode_klass_alive_instance= nullRef;
  Universe::_emptySymbol                            = nullRef;
  debug_only(Universe::_fullgc_alot_dummy_array = nullRef;)
}

jint universe_init() {
  assert(!Universe::_fully_initialized, "called after initialize_vtables");
  guarantee(1 << LogHeapWordSize == sizeof(HeapWord),
	 "LogHeapWordSize is incorrect.");
  guarantee(sizeof(oop) >= sizeof(HeapWord), "HeapWord larger than oop?");
  guarantee(sizeof(oop) % sizeof(HeapWord) == 0,
	 "oop size is not not a multiple of HeapWord size");
  TraceTime timer("Genesis", TraceStartupTime);
GC_locker::lock_universe();//do not allow gc during bootstrapping
  JavaClasses::compute_hard_coded_offsets();

  FileMapInfo* mapinfo = NULL;

  jint status = Universe::initialize_heap();
  if (status != JNI_OK) {
    return status;
  }

  // We have a heap so create the methodOop caches.
  Universe::_finalizer_register_cache = new LatestMethodOopCache();
  Universe::_loader_addClass_cache    = new LatestMethodOopCache();
  Universe::_reflect_invoke_cache     = new ActiveMethodOopsCache();

  {
    SymbolTable::create_table();
    StringTable::create_table();

    ClassLoader::create_package_info_table();
  }

  return JNI_OK;
}

jint Universe::initialize_heap() {

  if (UseParallelGC) {
    Universe::_collectedHeap = new ParallelScavengeHeap();
  } else if (UseGenPauselessGC) {
Universe::_collectedHeap=new GPGC_Heap();
  } else if (UseSerialGC) {
    GenCollectorPolicy *gc_policy = new MarkSweepPolicy();
    Universe::_collectedHeap = new GenCollectedHeap(gc_policy);
  } else {
guarantee(false,"No GC type selected");
  }

  jint status = Universe::heap()->initialize();
  if (status != JNI_OK) {
    return status;
  }

  // We will never reach the CATCH below since Exceptions::_throw will cause
  // the VM to exit if an exception is thrown during initialization

  assert(Universe::heap()->supports_tlab_allocation(),
         "Should support thread-local allocation buffers");
  ThreadLocalAllocBuffer::startup_initialization();

  return JNI_OK;
}

// It's the caller's repsonsibility to ensure glitch-freedom
// (if required).
void Universe::update_heap_info_at_gc() {
  // Finished with GC: Also return unused CHeap pages to azmem
  size_t flushed;
  size_t allocated;
  os::flush_memory(os::CHEAP_COMMITTED_MEMORY_ACCOUNT, &flushed, &allocated);

  _heap_capacity_at_last_gc = heap()->capacity();
  _heap_used_at_last_gc     = heap()->used();
}



void universe2_init() {
  EXCEPTION_MARK;
  Universe::genesis(CATCH);
}


// This function is defined in JVM.cpp
extern void initialize_converter_functions();

bool universe_post_init() {
  HandleMark hm;
  Universe::_fully_initialized = true;

  EXCEPTION_MARK;
  { ResourceMark rm;
    KlassHandle ok_h(THREAD, SystemDictionary::object_klass());
    Universe::reinitialize_vtable_of(ok_h, CHECK_false);
    Universe::reinitialize_itables(CHECK_false);
  }

  klassOop k;
  instanceKlassHandle k_h;
  {
    // Setup preallocated empty java.lang.Class array
objArrayOop oa=oopFactory::new_objArray(SystemDictionary::class_klass(),0,false/*No SBA*/,CHECK_false);
    POISON_AND_STORE_REF(&Universe::_the_empty_class_klass_array, objArrayRef(oa));
    k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_OutOfMemoryError(), true, CHECK_false);
    k_h = instanceKlassHandle(THREAD, k);
oop o;
o=k_h->allocate_permanent_instance(CHECK_false);
POISON_AND_STORE_REF(&Universe::_out_of_memory_error_java_heap,objectRef(o));
o=k_h->allocate_permanent_instance(CHECK_false);
POISON_AND_STORE_REF(&Universe::_out_of_memory_error_perm_gen,objectRef(o));
o=k_h->allocate_permanent_instance(CHECK_false);
POISON_AND_STORE_REF(&Universe::_out_of_memory_error_array_size,objectRef(o));
o=k_h->allocate_permanent_instance(CHECK_false);
POISON_AND_STORE_REF(&Universe::_out_of_memory_error_gc_overhead_limit,objectRef(o));

    // Setup preallocated NullPointerException
    // (this is currently used for a cheap & dirty solution in compiler exception handling)
k=SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_NullPointerException(),NULL,NULL,true,CHECK_false);
o=instanceKlass::cast(k)->allocate_permanent_instance(CHECK_false);
    POISON_AND_STORE_REF(&Universe::_null_ptr_exception_instance, objectRef(o));
    // Setup preallocated ArrayIndexOutOfBoundsException
    // (this is currently used for a cheap & dirty solution in compiler exception handling)
    k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_ArrayIndexOutOfBoundsException(), NULL, NULL, true, CHECK_false);
o=instanceKlass::cast(k)->allocate_permanent_instance(CHECK_false);
    POISON_AND_STORE_REF(&Universe::_aioob_exception_instance, objectRef(o));
    // Setup preallocated ArithmeticException
    // (this is currently used for a cheap & dirty solution in compiler exception handling)
    k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_ArithmeticException(), true, CHECK_false);
o=instanceKlass::cast(k)->allocate_permanent_instance(CHECK_false);
    POISON_AND_STORE_REF(&Universe::_arithmetic_exception_instance, objectRef(o));

    // Virtual Machine Error for when we get into a situation we can't resolve
    k = SystemDictionary::resolve_or_fail(
      vmSymbolHandles::java_lang_VirtualMachineError(), true, CHECK_false);
    bool linked = instanceKlass::cast(k)->link_class_or_fail(CHECK_false);
    if (!linked) {
      tty->print_cr("Unable to link/verify VirtualMachineError class");
      return false; // initialization failed
    }
o=instanceKlass::cast(k)->allocate_permanent_instance(CHECK_false);
    POISON_AND_STORE_REF(&Universe::_virtual_machine_error_instance, objectRef(o));
  }

  {
    // These are the only Java fields that are currently set during shared space dumping.        
    // We prefer to not handle this generally, so we always reinitialize these detail messages.
Handle msg=java_lang_String::create_from_str("Java heap space",false/*No SBA*/,CHECK_false);
java_lang_Throwable::set_message(Universe::out_of_memory_error_java_heap(),msg());

msg=java_lang_String::create_from_str("PermGen space",false/*No SBA*/,CHECK_false);
java_lang_Throwable::set_message(Universe::out_of_memory_error_perm_gen(),msg());

msg=java_lang_String::create_from_str("Requested array size exceeds VM limit",false/*No SBA*/,CHECK_false);
java_lang_Throwable::set_message(Universe::out_of_memory_error_array_size(),msg());

msg=java_lang_String::create_from_str("GC overhead limit exceeded",false/*No SBA*/,CHECK_false);
java_lang_Throwable::set_message(Universe::out_of_memory_error_gc_overhead_limit(),msg());

msg=java_lang_String::create_from_str("/ by zero",false/*No SBA*/,CHECK_false);
java_lang_Throwable::set_message(Universe::arithmetic_exception_instance(),msg());

    // Setup the array of errors that have preallocated backtrace
k=lvb_ref(&Universe::_out_of_memory_error_java_heap).as_oop()->klass();
    assert(k->klass_part()->name() == vmSymbols::java_lang_OutOfMemoryError(), "should be out of memory error");
    k_h = instanceKlassHandle(THREAD, k);

    int len = (StackTraceInThrowable) ? (int)PreallocatedOutOfMemoryErrorCount : 0;
objArrayOop oa=oopFactory::new_objArray(k_h(),len,false/*No SBA*/,CHECK_false);
    POISON_AND_STORE_REF(&Universe::_preallocated_out_of_memory_error_array, objArrayRef(oa));
    for (int i=0; i<len; i++) {
      oop err = k_h->allocate_permanent_instance(CHECK_false);
      Handle err_h = Handle(THREAD, err);
      java_lang_Throwable::allocate_backtrace(err_h, CHECK_false);
      Universe::preallocated_out_of_memory_errors()->obj_at_put(i, err_h());
    }
Universe::_preallocated_out_of_memory_error_avail_count=(jlong)len;
  }

  
  // Setup static method for registering finalizers
  // The finalizer klass must be linked before looking up the method, in
  // case it needs to get rewritten.
  instanceKlass::cast(SystemDictionary::finalizer_klass())->link_class(CHECK_false);
  methodOop m = instanceKlass::cast(SystemDictionary::finalizer_klass())->find_method(
                                  vmSymbols::register_method_name(), 
                                  vmSymbols::register_method_signature());
  if (m == NULL || !m->is_static()) {
    THROW_MSG_(vmSymbols::java_lang_NoSuchMethodException(), 
      "java.lang.ref.Finalizer.register", false);
  }
  Universe::_finalizer_register_cache->init(
    SystemDictionary::finalizer_klass(), m, CHECK_false);

  // Resolve on first use and initialize class. 
  // Note: No race-condition here, since a resolve will always return the same result

  // Setup method for security checks 
  k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_reflect_Method(), true, CHECK_false);  
  k_h = instanceKlassHandle(THREAD, k);
  k_h->link_class(CHECK_false);
  m = k_h->find_method(vmSymbols::invoke_name(), vmSymbols::object_array_object_object_signature());
  if (m == NULL || m->is_static()) {
    THROW_MSG_(vmSymbols::java_lang_NoSuchMethodException(), 
      "java.lang.reflect.Method.invoke", false);
  }
  Universe::_reflect_invoke_cache->init(k_h(), m, CHECK_false);

  // Setup method for registering loaded classes in class loader vector 
  instanceKlass::cast(SystemDictionary::classloader_klass())->link_class(CHECK_false);
  m = instanceKlass::cast(SystemDictionary::classloader_klass())->find_method(vmSymbols::addClass_name(), vmSymbols::class_void_signature());
  if (m == NULL || m->is_static()) {
    THROW_MSG_(vmSymbols::java_lang_NoSuchMethodException(), 
      "java.lang.ClassLoader.addClass", false);
  }
  Universe::_loader_addClass_cache->init(
    SystemDictionary::classloader_klass(), m, CHECK_false);

  // The folowing is initializing converter functions for serialization in
  // JVM.cpp. If we clean up the StrictMath code above we may want to find
  // a better solution for this as well.
  initialize_converter_functions();

  // This needs to be done before the first scavenge/gc, since
  // it's an input to soft ref clearing policy.
  Universe::update_heap_info_at_gc();

  // ("weak") refs processing infrastructure initialization
  Universe::heap()->post_initialize();

GC_locker::unlock_universe();//allow gc after bootstrapping

  return true;
}


void Universe::compute_base_vtable_size() {
  _base_vtable_size = ClassLoader::compute_Object_vtable();
}

void Universe::print() { print_on(gclog_or_tty); }

void Universe::print_on(outputStream* st) {
  st->print_cr("Heap");
  heap()->print_on(st);
}

void Universe::print_heap_at_SIGBREAK() {
  if (PrintHeapAtSIGBREAK) {
    MutexLockerAllowGC hl(Heap_lock, JavaThread::current());
    print_on(tty);
    tty->cr();
    tty->flush();
  }
}

void Universe::print_heap_before_gc(outputStream* st) {  
  st->print_cr("{Heap before GC invocations=%u (full %u):",
	       heap()->total_collections(),
	       heap()->total_full_collections());
  heap()->print_on(st);
}

void Universe::print_heap_after_gc(outputStream* st) {
  st->print_cr("Heap after GC invocations=%u (full %u):",
	       heap()->total_collections(),
	       heap()->total_full_collections());
  heap()->print_on(st);
  st->print_cr("}");
}

void Universe::verify(bool allow_dirty, bool silent) {
  // The use of _verify_in_progress is a temporary work around for
  // 6320749.  Don't bother with a creating a class to set and clear
  // it since it is only used in this method and the control flow is
  // straight forward.
  _verify_in_progress = true;
  assert(!DerivedPointerTable::is_active(),
"DPT should not be active during verification (of thread stacks below)");

  ResourceMark rm;
  HandleMark hm;  // Handles created during verification can be zapped
  _verify_count++;

  if ( UseGenPauselessGC ) {
    // With gen pauseless GC, only a java thread or a GC thread can safely verify the heap:
    Thread* thread = Thread::current();
    if ( (!thread->is_Java_thread()) && (!thread->is_GenPauselessGC_thread()) )
      return;
    // TODO: maw: make the verification pass do any necessary remapping, so other threads,
    // including the VMThread, can do verification.
  }

  if (!silent) gclog_or_tty->print("[Verifying ");
  if (!silent) gclog_or_tty->print("threads ");     
  Threads::verify();
  heap()->verify(allow_dirty, silent);

  if (!silent) gclog_or_tty->print("syms ");        
  SymbolTable::verify();
  if (!silent) gclog_or_tty->print("strs ");        
  StringTable::verify();
  {
MutexLocker mu(CodeCache_lock);
    assert0((Thread::current()->is_Java_thread() && ((JavaThread*)Thread::current())->jvm_locked_by_self()) ||
            SafepointSynchronize::is_at_safepoint());
    if (!silent) gclog_or_tty->print("zone ");      
    CodeCache::verify();
  }
  if (!silent) gclog_or_tty->print("dict ");        
  SystemDictionary::verify();
  if (!silent) gclog_or_tty->print("hand ");        
  JNIHandles::verify();
  if (!silent) gclog_or_tty->print("C-heap ");      
  os::check_heap();
  if (!silent) gclog_or_tty->print_cr("]");

  _verify_in_progress = false;
}

// Oop verification (see MacroAssembler::verify_oop)

static uintptr_t _verify_oop_data[2]   = {0, (uintptr_t)-1};
static uintptr_t _verify_klass_data[2] = {0, (uintptr_t)-1};


static void calculate_verify_data(uintptr_t verify_data[2],
				  HeapWord* low_boundary,
				  HeapWord* high_boundary) {
  assert(low_boundary < high_boundary, "bad interval");

  // decide which low-order bits we require to be clear:
size_t alignSize=wordSize;
  size_t min_object_size = oopDesc::header_size();  

  // make an inclusive limit:
  uintptr_t max = (uintptr_t)high_boundary - min_object_size*wordSize;
  uintptr_t min = (uintptr_t)low_boundary;
  assert(min < max, "bad interval");
  uintptr_t diff = max ^ min;

  // throw away enough low-order bits to make the diff vanish
  uintptr_t mask = (uintptr_t)(-1);
  while ((mask & diff) != 0)
    mask <<= 1;
  uintptr_t bits = (min & mask);
  assert(bits == (max & mask), "correct mask");
  // check an intermediate value between min and max, just to make sure:
  assert(bits == ((min + (max-min)/2) & mask), "correct mask");

  // require address alignment, too:
  mask |= (alignSize - 1);

  if (!(verify_data[0] == 0 && verify_data[1] == (uintptr_t)-1)) {
    assert(verify_data[0] == mask && verify_data[1] == bits, "mask stability");
  }
  verify_data[0] = mask;
  verify_data[1] = bits;
}


// Oop verification (see MacroAssembler::verify_oop)
#ifndef PRODUCT

uintptr_t Universe::verify_oop_mask() {
  if (UseGenPauselessGC) {
    ShouldNotReachHere(); 
  }
  MemRegion m = heap()->reserved_region();
  calculate_verify_data(_verify_oop_data,
			m.start(),
			m.end());
  return _verify_oop_data[0];
}



uintptr_t Universe::verify_oop_bits() {
  verify_oop_mask();
  return _verify_oop_data[1];
}


uintptr_t Universe::verify_klass_mask() {
  /* $$$
  // A klass can never live in the new space.  Since the new and old
  // spaces can change size, we must settle for bounds-checking against
  // the bottom of the world, plus the smallest possible new and old
  // space sizes that may arise during execution.
  size_t min_new_size = Universe::new_size();   // in bytes
  size_t min_old_size = Universe::old_size();   // in bytes
  calculate_verify_data(_verify_klass_data,
	  (HeapWord*)((uintptr_t)_new_gen->low_boundary + min_new_size + min_old_size),
	  _perm_gen->high_boundary);
			*/
  // Why doesn't the above just say that klass's always live in the perm
  // gen?  I'll see if that seems to work...
  MemRegion permanent_reserved;
  switch (Universe::heap()->kind()) {
  default:
    // ???: What if a CollectedHeap doesn't have a permanent generation?
    ShouldNotReachHere();
    break;
  case CollectedHeap::GenCollectedHeap: {
    GenCollectedHeap* gch = (GenCollectedHeap*) Universe::heap();
    permanent_reserved = gch->perm_gen()->reserved();
    break;
  }
  case CollectedHeap::ParallelScavengeHeap: {
    ParallelScavengeHeap* psh = (ParallelScavengeHeap*) Universe::heap();
    permanent_reserved = psh->perm_gen()->reserved();
    break;
  }
  }
  calculate_verify_data(_verify_klass_data,
                        permanent_reserved.start(), 
                        permanent_reserved.end());
  
  return _verify_klass_data[0];
}



uintptr_t Universe::verify_klass_bits() {
  verify_klass_mask();
  return _verify_klass_data[1];
}


uintptr_t Universe::verify_mark_mask() {
  return markWord::kid_mask_in_place | markWord::lock_mask_in_place;
}



uintptr_t Universe::verify_mark_bits() {
  intptr_t mask = verify_mark_mask();
  intptr_t bits = (intptr_t)markWord::prototype_without_kid();
  assert((bits & ~mask) == 0, "no stray header bits");
  return bits;
}
#endif // PRODUCT


void Universe::compute_verify_oop_data() {
  verify_oop_mask();
  verify_oop_bits();
  verify_mark_mask();
  verify_mark_bits();
  verify_klass_mask();
  verify_klass_bits();
}


void CommonMethodOopCache::init(klassOop k, methodOop m, TRAPS) {
  POISON_AND_STORE_REF(&_klass, klassRef(k));

  _method_idnum = m->method_idnum();
  assert(_method_idnum >= 0, "sanity check");
}


ActiveMethodOopsCache::~ActiveMethodOopsCache() {
  if (_prev_methods != NULL) {
    for (int i = _prev_methods->length() - 1; i >= 0; i--) {
      jweak method_ref = _prev_methods->at(i);
      if (method_ref != NULL) {
        JNIHandles::destroy_weak_global(method_ref);
      }
    }
    delete _prev_methods;
    _prev_methods = NULL;
  }
}


void ActiveMethodOopsCache::add_previous_version(const methodOop method) {
  assert(Thread::current()->is_VM_thread(),
    "only VMThread can add previous versions");

  if (_prev_methods == NULL) {
    // This is the first previous version so make some space.
    // Start with 2 elements under the assumption that the class
    // won't be redefined much.
    _prev_methods = new (ResourceObj::C_HEAP) GrowableArray<jweak>(2, true);
  }

  methodHandle method_h(method);
  jweak method_ref = JNIHandles::make_weak_global(method_h);
  _prev_methods->append(method_ref);

  // Using weak references allows previous versions of the cached
  // method to be GC'ed when they are no longer needed. Since the
  // caller is the VMThread and we are at a safepoint, this is a good
  // time to clear out unused weak references.

  for (int i = _prev_methods->length() - 1; i >= 0; i--) {
    jweak method_ref = _prev_methods->at(i);
    assert(method_ref != NULL, "weak method ref was unexpectedly cleared");
    if (method_ref == NULL) {
      _prev_methods->remove_at(i);
      // Since we are traversing the array backwards, we don't have to
      // do anything special with the index.
      continue;  // robustness
    }
      
    methodOop m = (methodOop)JNIHandles::resolve(method_ref);
    if (m == NULL) {
      // this method entry has been GC'ed so remove it
      JNIHandles::destroy_weak_global(method_ref);
      _prev_methods->remove_at(i);
    } else {
    }
  }
} // end add_previous_version()


bool ActiveMethodOopsCache::is_same_method(const methodOop method) const {
  instanceKlass* ik = instanceKlass::cast(klass());
  methodOop check_method = (methodOop)ik->methods()->obj_at(method_idnum());
  assert(check_method != NULL, "sanity check");
  if (check_method == method) {
    // done with the easy case
    return true;
  }

  if (_prev_methods != NULL) {
    // The cached method has been redefined at least once so search
    // the previous versions for a match.
    for (int i = 0; i < _prev_methods->length(); i++) {
      jweak method_ref = _prev_methods->at(i);
      assert(method_ref != NULL, "weak method ref was unexpectedly cleared");
      if (method_ref == NULL) {
        continue;  // robustness
      }

      check_method = (methodOop)JNIHandles::resolve(method_ref);
      if (check_method == method) {
        // a previous version matches
        return true;
      }
    }
  }

  // either no previous versions or no previous version matched
  return false;
}


methodOop LatestMethodOopCache::get_methodOop() {
  instanceKlass* ik = instanceKlass::cast(klass());
  methodOop m = (methodOop)ik->methods()->obj_at(method_idnum());
  assert(m != NULL, "sanity check");
  return m;
}


#ifdef ASSERT
// Release dummy object(s) at bottom of heap
bool Universe::release_fullgc_alot_dummy() {
  MutexLocker ml(FullGCALot_lock);
  objArrayRef oaf(ALWAYS_UNPOISON_OBJECTREF(_fullgc_alot_dummy_array).raw_value());
if(oaf.not_null()){
    if (_fullgc_alot_dummy_next >= oaf.as_objArrayOop()->length()) {
      // No more dummies to release, release entire array instead
_fullgc_alot_dummy_array=nullRef;
      return false;
    }
    if ( !UseGenPauselessGC ) {
      // Release dummy at bottom of old generation
oaf.as_objArrayOop()->obj_at_put(_fullgc_alot_dummy_next++,NULL);
    }
    // Release dummy at bottom of permanent generation
oaf.as_objArrayOop()->obj_at_put(_fullgc_alot_dummy_next++,NULL);
  }
  return true;
}

#endif // ASSERT


klassOop Universe::forwarded_klassKlassObj() {
  // Why is this an unsafe transformation? Why not as_oop()?
  return (klassOop) _klassKlassObj.as_address();
}


// SBA

bool Universe::is_in_allocation_area(void* x) {
  return (heap()->is_in(x) || 
          KlassTable::contains(x) ||
          Threads::sba_find_owner((address)x));
}

bool Universe::is_in_allocation_area_or_null(void* x) {
  return x == NULL || is_in_allocation_area(x);
}
