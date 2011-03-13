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
#ifndef UNIVERSE_HPP
#define UNIVERSE_HPP

#include "exceptions.hpp"
#include "handles.hpp"
#include "iterator.hpp"
#include "growableArray.hpp"

// Universe is a name space holding known system classes and objects in the VM.
// 
// Loaded classes are accessible through the SystemDictionary.
// 
// The object heap is allocated and accessed through Universe, and various allocation
// support is provided. Allocation by the interpreter and compiled code is done inline
// and bails out to Scavenge::invoke_and_allocate.

class CollectedHeap;
class DeferredObjAllocEvent;
class outputStream;
extern outputStream* gclog_or_tty;  // stream for gc log if -Xloggc:<f>, or tty

// Common parts of a methodOop cache. This cache safely interacts with
// the RedefineClasses API.
//
class CommonMethodOopCache : public CHeapObj {
  // We save the klassOop and the idnum of methodOop in order to get
  // the current cached methodOop.
 private:
  klassRef              _klass;
  int                   _method_idnum;

 public:
CommonMethodOopCache(){_klass=nullRef;_method_idnum=-1;}
~CommonMethodOopCache(){_klass=nullRef;_method_idnum=-1;}

  void     init(klassOop k, methodOop m, TRAPS);
klassOop klass()const{return lvb_klassRef(&_klass).as_klassOop();}
  int      method_idnum() const  { return _method_idnum; }

  // GC support
  void     oops_do(OopClosure* f)  { f->do_oop((objectRef*)&_klass); }
};


// A helper class for caching a methodOop when the user of the cache
// cares about all versions of the methodOop.
//
class ActiveMethodOopsCache : public CommonMethodOopCache {
  // This subclass adds weak references to older versions of the
  // methodOop and a query method for a methodOop.

 private:
  // If the cached methodOop has not been redefined, then
  // _prev_methods will be NULL. If all of the previous
  // versions of the method have been collected, then
  // _prev_methods can have a length of zero.
  GrowableArray<jweak>* _prev_methods;

 public:
  ActiveMethodOopsCache()   { _prev_methods = NULL; }
  ~ActiveMethodOopsCache();

  void add_previous_version(const methodOop method);
  bool is_same_method(const methodOop method) const;
};


// A helper class for caching a methodOop when the user of the cache
// only cares about the latest version of the methodOop.
//
class LatestMethodOopCache : public CommonMethodOopCache {
  // This subclass adds a getter method for the latest methodOop.

 public:
  methodOop get_methodOop();
};


class Universe: AllStatic {
  friend class MarkSweep;
  friend class oopDesc;
  friend class ClassLoader;
  friend class Arguments;
  friend class SystemDictionary;
  friend void  universe_static_init();
  friend class CompactingPermGenGen;

  friend jint  universe_init();
  friend void  universe2_init();
  friend bool  universe_post_init();

 private:
  // Known classes in the VM
  static klassRef _boolArrayKlassObj;
  static klassRef _byteArrayKlassObj;
  static klassRef _charArrayKlassObj;
  static klassRef _intArrayKlassObj;
  static klassRef _shortArrayKlassObj;
  static klassRef _longArrayKlassObj;
  static klassRef _singleArrayKlassObj;
  static klassRef _doubleArrayKlassObj;
  static klassRef _typeArrayKlassObjs[T_VOID+1];
  
  static klassRef _objectArrayKlassObj;

  static klassRef _symbolKlassObj;
  static klassRef _methodKlassObj;
static klassRef _methodCodeKlassObj;
  static klassRef _constMethodKlassObj;
  static klassRef _klassKlassObj;
  static klassRef _arrayKlassKlassObj;
  static klassRef _objArrayKlassKlassObj;
  static klassRef _typeArrayKlassKlassObj;
  static klassRef _instanceKlassKlassObj;
  static klassRef _constantPoolKlassObj;
  static klassRef _constantPoolCacheKlassObj;
  static klassRef _systemObjArrayKlassObj;

  // Known objects in the VM

  // Primitive objects
  static objectRef    _int_mirror;
  static objectRef    _float_mirror;
  static objectRef    _double_mirror;
  static objectRef    _byte_mirror;
  static objectRef    _bool_mirror;
  static objectRef    _char_mirror;
  static objectRef    _long_mirror;
  static objectRef    _short_mirror;
  static objectRef    _void_mirror;

  static objectRef    _main_thread_group;             // Reference to the main thread group object
  static objectRef    _system_thread_group;           // Reference to the system thread group object
static jobject _system_thread_group_handle;

  static typeArrayRef _the_empty_byte_array;          // Canonicalized byte array  
  static typeArrayRef _the_empty_short_array;         // Canonicalized short array
  static typeArrayRef _the_empty_int_array;           // Canonicalized int array
  static objArrayRef  _the_empty_system_obj_array;    // Canonicalized system obj array
  static objArrayRef  _the_empty_class_klass_array;   // Canonicalized obj array of type java.lang.Class
  static objArrayRef  _the_array_interfaces_array;    // Canonicalized 2-array of cloneable & serializable klasses
  static LatestMethodOopCache* _finalizer_register_cache; // static method for registering finalizable objects
  static LatestMethodOopCache* _loader_addClass_cache;    // method for registering loaded classes in class loader vector
  static ActiveMethodOopsCache* _reflect_invoke_cache;    // method for security checks
  static objectRef    _out_of_memory_error_java_heap; // preallocated error object (no backtrace)
  static objectRef    _out_of_memory_error_perm_gen;  // preallocated error object (no backtrace)
  static objectRef    _out_of_memory_error_array_size;// preallocated error object (no backtrace)
  static objectRef    _out_of_memory_error_gc_overhead_limit; // preallocated error object (no backtrace)
  static objectRef    _null_ptr_exception_instance;   // preallocated exception object
static objectRef _aioob_exception_instance;//preallocated exception object
  static objectRef    _arithmetic_exception_instance; // preallocated exception object
  static objectRef    _virtual_machine_error_instance;// preallocated exception object
  static objectRef    _the_keep_methodCode_klass_alive_instance;  // Prop methodCodeKlass up until methods start to compile.

  // array of preallocated error objects with backtrace
  static objArrayRef  _preallocated_out_of_memory_error_array;

  // number of preallocated error objects available for use
  static volatile jlong _preallocated_out_of_memory_error_avail_count;

  // The object used as an exception dummy when exceptions are thrown for
  // the vm thread.
  static objectRef    _vm_exception;

  static objectRef    _emptySymbol;                   // Canonical empty string ("") symbol

  // The particular choice of collected heap.
  static CollectedHeap* _collectedHeap;

  // array of dummy objects used with +FullGCAlot
  debug_only(static objArrayRef _fullgc_alot_dummy_array;)
 // index of next entry to clear
  debug_only(static int         _fullgc_alot_dummy_next;) 

  // Compiler/dispatch support
  static int  _base_vtable_size;                      // Java vtbl size of klass Object (in words)

  // Initialization
  static bool _bootstrapping;                         // true during genesis
  static bool _fully_initialized;                     // true after universe_init and initialize_vtables called

  // the array of preallocated errors with backtraces
static objArrayOop preallocated_out_of_memory_errors(){return lvb_objArrayRef(&_preallocated_out_of_memory_error_array).as_objArrayOop();}

  // generate an out of memory error; if possible using an error with preallocated backtrace;
  // otherwise return the given default error.
static oop gen_out_of_memory_error(objectRef*default_err);


  // Historic gc information
  static size_t _heap_capacity_at_last_gc;
  static size_t _heap_used_at_last_gc;

  static jint initialize_heap();
  static void initialize_basic_type_mirrors(TRAPS);
  static void fixup_mirrors(TRAPS);

  static void reinitialize_vtable_of(KlassHandle h_k, TRAPS);
  static void reinitialize_itables(TRAPS);
  static void compute_base_vtable_size();             // compute vtable size of class Object

  static void genesis(TRAPS);                         // Create the initial world

  // Mirrors for primitive classes (created eagerly)
  static oop check_mirror(objectRef m) {
assert(m.not_null(),"mirror not initialized");
return m.as_oop();
  }

  // Debugging
  static int _verify_count;                           // number of verifies done
  // True during call to verify().  Should only be set/cleared in verify().
  static bool _verify_in_progress;		      

  static void compute_verify_oop_data();

 public:
  // Known classes in the VM
static klassOop boolArrayKlassObj(){return lvb_klassRef(&_boolArrayKlassObj).as_klassOop();}
static klassOop byteArrayKlassObj(){return lvb_klassRef(&_byteArrayKlassObj).as_klassOop();}
static klassOop charArrayKlassObj(){return lvb_klassRef(&_charArrayKlassObj).as_klassOop();}
static klassOop intArrayKlassObj(){return lvb_klassRef(&_intArrayKlassObj).as_klassOop();}
static klassOop shortArrayKlassObj(){return lvb_klassRef(&_shortArrayKlassObj).as_klassOop();}
static klassOop longArrayKlassObj(){return lvb_klassRef(&_longArrayKlassObj).as_klassOop();}
static klassOop singleArrayKlassObj(){return lvb_klassRef(&_singleArrayKlassObj).as_klassOop();}
static klassOop doubleArrayKlassObj(){return lvb_klassRef(&_doubleArrayKlassObj).as_klassOop();}
static klassOop objectArrayKlassObj(){return lvb_klassRef(&_objectArrayKlassObj).as_klassOop();}

  static klassOop typeArrayKlassObj(BasicType t) {
    assert((uint)t < T_VOID+1, "range check");
assert(_typeArrayKlassObjs[t].not_null(),"domain check");
return lvb_klassRef(&_typeArrayKlassObjs[t]).as_klassOop();
  }

  static klassOop symbolKlassObj()                    { return lvb_klassRef(&_symbolKlassObj).as_klassOop();            }
  static klassOop methodKlassObj()                    { return lvb_klassRef(&_methodKlassObj).as_klassOop();            }
  static klassOop methodCodeKlassObj()                { return lvb_klassRef(&_methodCodeKlassObj).as_klassOop();        }
  static klassOop constMethodKlassObj()               { return lvb_klassRef(&_constMethodKlassObj).as_klassOop();       }
static klassOop forwarded_klassKlassObj();
  static klassOop klassKlassObj()                     { return lvb_klassRef(&_klassKlassObj).as_klassOop();             }
  static klassOop arrayKlassKlassObj()                { return lvb_klassRef(&_arrayKlassKlassObj).as_klassOop();        }
  static klassOop objArrayKlassKlassObj()             { return lvb_klassRef(&_objArrayKlassKlassObj).as_klassOop();     }
  static klassOop typeArrayKlassKlassObj()            { return lvb_klassRef(&_typeArrayKlassKlassObj).as_klassOop();    }
  static klassOop instanceKlassKlassObj()             { return lvb_klassRef(&_instanceKlassKlassObj).as_klassOop();     }
  static klassOop constantPoolKlassObj()              { return lvb_klassRef(&_constantPoolKlassObj).as_klassOop();      }
  static klassOop constantPoolCacheKlassObj()         { return lvb_klassRef(&_constantPoolCacheKlassObj).as_klassOop(); }
  static klassOop systemObjArrayKlassObj()            { return lvb_klassRef(&_systemObjArrayKlassObj).as_klassOop();    }

  static klassRef* constantPoolKlassObj_addr()        { return &_constantPoolKlassObj; }

  // Known objects in tbe VM
static oop int_mirror(){return check_mirror(lvb_ref(&_int_mirror));}
static oop float_mirror(){return check_mirror(lvb_ref(&_float_mirror));}
static oop double_mirror(){return check_mirror(lvb_ref(&_double_mirror));}
static oop byte_mirror(){return check_mirror(lvb_ref(&_byte_mirror));}
static oop bool_mirror(){return check_mirror(lvb_ref(&_bool_mirror));}
static oop char_mirror(){return check_mirror(lvb_ref(&_char_mirror));}
static oop long_mirror(){return check_mirror(lvb_ref(&_long_mirror));}
static oop short_mirror(){return check_mirror(lvb_ref(&_short_mirror));}
static oop void_mirror(){return check_mirror(lvb_ref(&_void_mirror));}

  // table of same
  static objectRef _mirrors[T_VOID+1];

  static oop java_mirror(BasicType t) {
    assert((uint)t < T_VOID+1, "range check");
return check_mirror(lvb_ref(&_mirrors[t]));
  }

  static oop      main_thread_group()                 { return lvb_ref(&_main_thread_group).as_oop(); }
  static void set_main_thread_group(oop group)        { POISON_AND_STORE_REF(&_main_thread_group, objectRef(group)); }

  static oop      system_thread_group()               { return lvb_ref(&_system_thread_group).as_oop(); }
  static jobject  system_thread_group_handle()        { return _system_thread_group_handle; }
  static void set_system_thread_group(oop group);

  static typeArrayOop the_empty_byte_array()          { return lvb_typeArrayRef(&_the_empty_byte_array).as_typeArrayOop();      }
  static typeArrayOop the_empty_short_array()         { return lvb_typeArrayRef(&_the_empty_short_array).as_typeArrayOop();     }
  static typeArrayOop the_empty_int_array()           { return lvb_typeArrayRef(&_the_empty_int_array).as_typeArrayOop();       }
  static objArrayOop  the_empty_system_obj_array ()   { return lvb_objArrayRef(&_the_empty_system_obj_array).as_objArrayOop();  }
  static objArrayOop  the_empty_class_klass_array ()  { return lvb_objArrayRef(&_the_empty_class_klass_array).as_objArrayOop(); }
  static objArrayOop  the_array_interfaces_array()    { return lvb_objArrayRef(&_the_array_interfaces_array).as_objArrayOop();  }
  static methodOop    finalizer_register_method()     { return _finalizer_register_cache->get_methodOop(); }
  static methodOop    loader_addClass_method()        { return _loader_addClass_cache->get_methodOop(); }
  static ActiveMethodOopsCache* reflect_invoke_cache() { return _reflect_invoke_cache; }
static oop null_ptr_exception_instance(){return lvb_ref(&_null_ptr_exception_instance).as_oop();}
static oop aioob_exception_instance(){return lvb_ref(&_aioob_exception_instance).as_oop();}
static oop arithmetic_exception_instance(){return lvb_ref(&_arithmetic_exception_instance).as_oop();}
static oop virtual_machine_error_instance(){return lvb_ref(&_virtual_machine_error_instance).as_oop();}
static oop vm_exception(){return lvb_ref(&_vm_exception).as_oop();}
static oop emptySymbol(){return lvb_ref(&_emptySymbol).as_oop();}

  // OutOfMemoryError support. Returns an error with the required message. The returned error 
  // may or may not have a backtrace. If error has a backtrace then the stack trace is already
  // filled in.
static oop out_of_memory_error_java_heap(){return gen_out_of_memory_error(&_out_of_memory_error_java_heap);}
static oop out_of_memory_error_perm_gen(){return gen_out_of_memory_error(&_out_of_memory_error_perm_gen);}
static oop out_of_memory_error_array_size(){return gen_out_of_memory_error(&_out_of_memory_error_array_size);}
static oop out_of_memory_error_gc_overhead_limit(){return gen_out_of_memory_error(&_out_of_memory_error_gc_overhead_limit);}

  // Accessors needed for fast allocation
  static klassRef* boolArrayKlassObj_addr()           { return &_boolArrayKlassObj;   }
  static klassRef* byteArrayKlassObj_addr()           { return &_byteArrayKlassObj;   }
  static klassRef* charArrayKlassObj_addr()           { return &_charArrayKlassObj;   }
  static klassRef* intArrayKlassObj_addr()            { return &_intArrayKlassObj;    }
  static klassRef* shortArrayKlassObj_addr()          { return &_shortArrayKlassObj;  }
  static klassRef* longArrayKlassObj_addr()           { return &_longArrayKlassObj;   }
  static klassRef* singleArrayKlassObj_addr()         { return &_singleArrayKlassObj; }
  static klassRef* doubleArrayKlassObj_addr()         { return &_doubleArrayKlassObj; }

  // The particular choice of collected heap.
  static CollectedHeap* heap() { return _collectedHeap; }

  // SBA
  // "allocation area" means heap or stack
  static bool is_in_allocation_area(void* x);
  static bool is_in_allocation_area_or_null(void* x);

  // Historic gc information
  static size_t get_heap_capacity_at_last_gc()         { return _heap_capacity_at_last_gc; }
  static size_t get_heap_free_at_last_gc()             { return _heap_capacity_at_last_gc - _heap_used_at_last_gc; }
  static size_t get_heap_used_at_last_gc()             { return _heap_used_at_last_gc; }
  static void update_heap_info_at_gc();

  // Testers
  static bool is_bootstrapping()                      { return _bootstrapping; }
  static bool is_fully_initialized()                  { return _fully_initialized; }

  static inline bool element_type_should_be_aligned(BasicType type);
  static inline bool field_type_should_be_aligned(BasicType type);
  static bool        on_page_boundary(void* addr);
  static bool        should_fill_in_stack_trace(Handle throwable);
  static void check_alignment(uintx size, uintx alignment, const char* name);

  // Finalizer support.
  static void run_finalizers_on_exit();

  // Iteration

  // Apply "f" to the addresses of all the direct heap pointers maintained
  // as static fields of "Universe".
  static void oops_do(OopClosure* f, bool do_all = false);

  // Apply "f" to all klasses for basic types (classes not present in
  // SystemDictionary).
  static void basic_type_classes_do(void f(klassOop));
  
  // Apply "f" to all system klasses (classes not present in SystemDictionary).
  static void system_classes_do(void f(klassOop));

  // For sharing -- fill in a list of known vtable pointers.
  static void init_self_patching_vtbl_list(void** list, int count);

  // Debugging
  static bool verify_in_progress() { return _verify_in_progress; }
  static void verify(bool allow_dirty = true, bool silent = false);
  static int  verify_count()                  { return _verify_count; }
  static void print();
  static void print_on(outputStream* st);
  static void print_heap_at_SIGBREAK();
  static void print_heap_before_gc() { print_heap_before_gc(gclog_or_tty); }
  static void print_heap_after_gc()  { print_heap_after_gc(gclog_or_tty); }
  static void print_heap_before_gc(outputStream* st);
  static void print_heap_after_gc(outputStream* st);

  // Change the number of dummy objects kept reachable by the full gc dummy
  // array; this should trigger relocation in a sliding compaction collector.
  debug_only(static bool release_fullgc_alot_dummy();)
  // The non-oop pattern (see compiledIC.hpp, etc)
  static void*     non_oop_word();
  // The non-ref pattern (see compiledIC.hpp, etc)
static objectRef non_ref_word();

  // Oop verification (see MacroAssembler::verify_oop)
  static uintptr_t verify_oop_mask()          PRODUCT_RETURN0;
  static uintptr_t verify_oop_bits()          PRODUCT_RETURN0;
  static uintptr_t verify_mark_bits()         PRODUCT_RETURN0;
  static uintptr_t verify_mark_mask()         PRODUCT_RETURN0;
  static uintptr_t verify_klass_mask()        PRODUCT_RETURN0;
  static uintptr_t verify_klass_bits()        PRODUCT_RETURN0;

  // Compiler support
  static int base_vtable_size()               { return _base_vtable_size; }

};
#endif // UNIVERSE_HPP
