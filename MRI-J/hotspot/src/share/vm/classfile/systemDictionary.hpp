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
#ifndef SYSTEMDICTIONARY_HPP
#define SYSTEMDICTIONARY_HPP

#include "allocation.hpp"
#include "instanceKlass.hpp"
#include "java.hpp"
#include "mutexLocker.hpp"
#include "symbolOop.hpp"
class ClassFileStream;

// The system dictionary stores all loaded classes and maps:
//
//   [class name,class loader] -> class   i.e.  [symbolOop,oop] -> klassOop
//
// Classes are loaded lazily. The default VM class loader is
// represented as NULL.

// The underlying data structure is an open hash table with a fixed number
// of buckets. During loading the loader object is locked, (for the VM loader 
// a private lock object is used). Class loading can thus be done concurrently,
// but only by different loaders.
//
// During loading a placeholder (name, loader) is temporarily placed in
// a side data structure, and is used to detect ClassCircularityErrors
// and to perform verification during GC.  A GC can occur in the midst
// of class loading, as we call out to Java, have to take locks, etc.
//
// When class loading is finished, a new entry is added to the system
// dictionary and the place holder is removed. Note that the protection
// domain field of the system dictionary has not yet been filled in when
// the "real" system dictionary entry is created.
//
// Clients of this class who are interested in finding if a class has
// been completely loaded -- not classes in the process of being loaded --
// can read the SystemDictionary unlocked. This is safe because
//    - entries are only deleted at safepoints  
//    - readers cannot come to a safepoint while actively examining
//         an entry  (an entry cannot be deleted from under a reader) 
//    - entries must be fully formed before they are available to concurrent
//         readers (we must ensure write ordering)
//
// Note that placeholders are deleted at any time, as they are removed
// when a class is completely loaded. Therefore, readers as well as writers
// of placeholders must hold the SystemDictionary_lock.
// 

class Dictionary;
class PlaceholderTable;
class LoaderConstraintTable;
class HashtableBucket;
class ResolutionErrorTable;

class SystemDictionary : AllStatic {
  friend void systemdictionary_static_init();
  friend class VMStructs;
  friend class CompactingPermGenGen;
  NOT_PRODUCT(friend class instanceKlassKlass;)

 public:
  // Returns a class with a given class name and class loader.  Loads the
  // class if needed. If not found a NoClassDefFoundError or a
  // ClassNotFoundException is thrown, depending on the value on the
  // throw_error flag.  For most uses the throw_error argument should be set
  // to true.

  static klassOop resolve_or_fail(symbolHandle class_name, Handle class_loader, Handle protection_domain, bool throw_error, TRAPS);
  // Convenient call for null loader and protection domain.
  static klassOop resolve_or_fail(symbolHandle class_name, bool throw_error, TRAPS);
private:
  // handle error translation for resolve_or_null results
  static klassOop handle_resolution_exception(symbolHandle class_name, Handle class_loader, Handle protection_domain, bool throw_error, KlassHandle klass_h, TRAPS);

public:

  // Returns a class with a given class name and class loader.
  // Loads the class if needed. If not found NULL is returned.
  static klassOop resolve_or_null(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS);
  // Version with null loader and protection domain
  static klassOop resolve_or_null(symbolHandle class_name, TRAPS);

  // Resolve a superclass or superinterface. Called from ClassFileParser, 
  // parse_interfaces, resolve_instance_class_or_null, load_shared_class
  // "child_name" is the class whose super class or interface is being resolved.
  static klassOop resolve_super_or_fail(symbolHandle child_name,
                                        symbolHandle class_name,
                                        Handle class_loader,
                                        Handle protection_domain,
                                        bool is_superclass,
                                        TRAPS);
  static inline klassOop resolve_super_or_fail(symbolHandle child_name,
                                               symbolHandle class_name,
                                               Handle class_loader,
                                               Handle protection_domain,
                                               TRAPS)
  { return SystemDictionary::resolve_super_or_fail(child_name, class_name, class_loader, protection_domain, false, THREAD); }

  // Parse new stream. This won't update the system dictionary or
  // class hierarchy, simply parse the stream. Used by JVMTI RedefineClasses.
  static klassOop parse_stream(symbolHandle class_name,
                               Handle class_loader,
                               Handle protection_domain,
                               ClassFileStream* st,
                               TRAPS);
                               
  // Resolve from stream (called by jni_DefineClass and JVM_DefineClass)
  static klassOop resolve_from_stream(symbolHandle class_name, Handle class_loader, Handle protection_domain, ClassFileStream* st, TRAPS);
  
  // Lookup an already loaded class. If not found NULL is returned.
  static klassOop find(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS);

  // Lookup an already loaded instance or array class.
  // Do not make any queries to class loaders; consult only the cache.
  // If not found NULL is returned.
  static klassOop find_instance_or_array_klass(symbolHandle class_name,
					       Handle class_loader,
					       Handle protection_domain,
					       TRAPS);

  // Lookup an instance or array class that has already been loaded
  // either into the given class loader, or else into another class
  // loader that is constrained (via loader constraints) to produce
  // a consistent class.  Do not take protection domains into account.
  // Do not make any queries to class loaders; consult only the cache.
  // Return NULL if the class is not found.
  //
  // This function is a strict superset of find_instance_or_array_klass.
  // This function (the unchecked version) makes a conservative prediction
  // of the result of the checked version, assuming successful lookup.
  // If both functions return non-null, they must return the same value.
  // Also, the unchecked version may sometimes be non-null where the
  // checked version is null.  This can occur in several ways:
  //   1. No query has yet been made to the class loader.
  //   2. The class loader was queried, but chose not to delegate.
  //   3. ClassLoader.checkPackageAccess rejected a proposed protection domain.
  //   4. Loading was attempted, but there was a linkage error of some sort.
  // In all of these cases, the loader constraints on this type are
  // satisfied, and it is safe for classes in the given class loader
  // to manipulate strongly-typed values of the found class, subject
  // to local linkage and access checks.
  static klassOop find_constrained_instance_or_array_klass(symbolHandle class_name,
                                                           Handle class_loader,
                                                           TRAPS);
  
  // Iterate over all klasses in dictionary
  //   Just the classes from defining class loaders
  static void classes_do(void f(klassOop));
  // Added for initialize_itable_for_klass to handle exceptions
  static void classes_do(void f(klassOop, TRAPS), TRAPS);
  // clone of the above function for GC purposes: at this point it is used only by the allocation-profiler
  // it uses the non-lvb accessor for the loader
static void GC_classes_do(void f(klassOop));
  //   All classes, and their class loaders
  static void classes_do(void f(klassOop, oop));
  //   All classes, and their class loaders
  //   (added for helpers that use HandleMarks and ResourceMarks)
  static void classes_do(void f(klassOop, oop, TRAPS), TRAPS);
  // All entries in the placeholder table and their class loaders
  static void placeholders_do(void f(symbolOop, oop));

  // Iterate over all methods in all klasses in dictionary
  static void methods_do(void f(methodOop));

  // Garbage collection support

  // This method applies "blk->do_oop" to all the pointers to "system"
  // classes and loaders.
  static void always_strong_oops_do(OopClosure* blk);
static void GC_always_strong_oops_do(OopClosure*blk);
static void GPGC_always_strong_oops_do_concurrent(OopClosure*blk);
  static void always_strong_classes_do(OopClosure* blk);
static void GC_always_strong_classes_do(OopClosure*blk);
  // This method applies "blk->do_oop" to all the placeholders.
  static void placeholders_do(OopClosure* blk);

  // Unload (that is, break root links to) all unmarked classes and
  // loaders.  Returns "true" iff something was unloaded.
  static bool do_unloading(BoolObjectClosure* is_alive);

  static void verify_dependencies(int gc_mode);

  static void GPGC_unload_section(GPGC_GCManagerOldStrong* gcm, long section, long sections); 
  static void GPGC_unload_section_cleanup(long sections);
  static void GPGC_purge_loader_constraints_section(long section, long sections);

  // Applies "f->do_oop" to all root oops in the system dictionary.
  static void oops_do(OopClosure* f);

  // System loader lock
  static objectRef system_loader_lock() { return _system_loader_lock_obj; }

private:
  //    Traverses preloaded oops: various system classes.  These are
  //    guaranteed to be in the perm gen.
  static void preloaded_oops_do(OopClosure* f);
  static void lazily_loaded_oops_do(OopClosure* f);

public:
  // Sharing support.
  static void reorder_dictionary();
  static void copy_buckets(char** top, char* end);
  static void copy_table(char** top, char* end);
  static void reverse();
  static void set_shared_dictionary(HashtableBucket* t, int length,
                                    int number_of_entries);
  // Printing
  static void print()                   PRODUCT_RETURN;
  static void print_class_statistics()  PRODUCT_RETURN;
  static void print_method_statistics() PRODUCT_RETURN;
  
  static void print_xml_on(xmlBuffer *xb);

  // Number of contained klasses
  // This is both fully loaded classes and classes in the process
  // of being loaded
  static int number_of_classes();

  static void reset_unloaded_classes();
  static int number_of_unloaded_classes();

  // Monotonically increasing counter which grows as classes are
  // loaded or modifications such as hot-swapping or setting/removing
  // of breakpoints are performed
static inline intptr_t number_of_modifications(){assert_locked_or_safepoint(Compile_lock);return _number_of_modifications;}
  // Needed by evolution and breakpoint code
static inline void notice_modification(){assert_locked_or_safepoint(Compile_lock);Atomic::inc_ptr(&_number_of_modifications);}

  // Verification
  static void verify();

#ifdef ASSERT
  static bool is_internal_format(symbolHandle class_name);
#endif

  // Verify class is in dictionary
  static void verify_obj_klass_present(Handle obj,
                                       symbolHandle class_name,
                                       Handle class_loader);

  // Initialization
  static void initialize(TRAPS);

  // Fast access to commonly used classes (preloaded)
  static klassOop check_klass(klassRef k) {
assert(k.not_null(),"preloaded klass not initialized");
return k.as_klassOop();
  }

public:
static klassOop object_klass(){return check_klass(lvb_klassRef(&_object_klass));}
static klassRef object_klassRef(){return lvb_klassRef(&_object_klass);}
static klassOop string_klass(){return check_klass(lvb_klassRef(&_string_klass));}
static klassOop class_klass(){return check_klass(lvb_klassRef(&_class_klass));}
static klassOop cloneable_klass(){return check_klass(lvb_klassRef(&_cloneable_klass));}
static klassOop classloader_klass(){return check_klass(lvb_klassRef(&_classloader_klass));}
static klassOop serializable_klass(){return check_klass(lvb_klassRef(&_serializable_klass));}
static klassOop system_klass(){return check_klass(lvb_klassRef(&_system_klass));}

static klassOop throwable_klass(){return check_klass(lvb_klassRef(&_throwable_klass));}
static klassOop error_klass(){return check_klass(lvb_klassRef(&_error_klass));}
static klassOop threaddeath_klass(){return check_klass(lvb_klassRef(&_threaddeath_klass));}
static klassOop exception_klass(){return check_klass(lvb_klassRef(&_exception_klass));}
static klassOop runtime_exception_klass(){return check_klass(lvb_klassRef(&_runtime_exception_klass));}
static klassOop classNotFoundException_klass(){return check_klass(lvb_klassRef(&_classNotFoundException_klass));}
static klassOop noClassDefFoundError_klass(){return check_klass(lvb_klassRef(&_noClassDefFoundError_klass));}
static klassOop linkageError_klass(){return check_klass(lvb_klassRef(&_linkageError_klass));}
static klassOop NullPointerException_klass(){return check_klass(lvb_klassRef(&_nullPointerException_klass));}
static klassOop ArrayIndexOutOfBoundsException_klass(){return check_klass(lvb_klassRef(&_arrayIndexOutOfBoundsException_klass));}
static klassOop ClassCastException_klass(){return check_klass(lvb_klassRef(&_classCastException_klass));}
static klassOop ArrayStoreException_klass(){return check_klass(lvb_klassRef(&_arrayStoreException_klass));}
static klassOop virtualMachineError_klass(){return check_klass(lvb_klassRef(&_virtualMachineError_klass));}
static klassOop OutOfMemoryError_klass(){return check_klass(lvb_klassRef(&_outOfMemoryError_klass));}
static klassOop StackOverflowError_klass(){return check_klass(lvb_klassRef(&_StackOverflowError_klass));}
static klassOop IllegalMonitorStateException_klass(){return check_klass(lvb_klassRef(&_illegalMonitorStateException_klass));}
static klassOop protectionDomain_klass(){return check_klass(lvb_klassRef(&_protectionDomain_klass));}
static klassOop AccessControlContext_klass(){return check_klass(lvb_klassRef(&_AccessControlContext_klass));}
static klassOop reference_klass(){return check_klass(lvb_klassRef(&_reference_klass));}
static klassOop soft_reference_klass(){return check_klass(lvb_klassRef(&_soft_reference_klass));}
static klassOop weak_reference_klass(){return check_klass(lvb_klassRef(&_weak_reference_klass));}
static klassOop final_reference_klass(){return check_klass(lvb_klassRef(&_final_reference_klass));}
static klassOop phantom_reference_klass(){return check_klass(lvb_klassRef(&_phantom_reference_klass));}
static klassOop finalizer_klass(){return check_klass(lvb_klassRef(&_finalizer_klass));}

static klassOop thread_klass(){return check_klass(lvb_klassRef(&_thread_klass));}
static klassOop threadGroup_klass(){return check_klass(lvb_klassRef(&_threadGroup_klass));}
static klassOop properties_klass(){return check_klass(lvb_klassRef(&_properties_klass));}
static klassOop reflect_accessible_object_klass(){return check_klass(lvb_klassRef(&_reflect_accessible_object_klass));}
static klassOop reflect_field_klass(){return check_klass(lvb_klassRef(&_reflect_field_klass));}
static klassOop reflect_method_klass(){return check_klass(lvb_klassRef(&_reflect_method_klass));}
static klassOop reflect_constructor_klass(){return check_klass(lvb_klassRef(&_reflect_constructor_klass));}
  static klassOop reflect_method_accessor_klass() { 
return check_klass(lvb_klassRef(&_reflect_method_accessor_klass));
  }
  static klassOop reflect_constructor_accessor_klass() { 
return check_klass(lvb_klassRef(&_reflect_constructor_accessor_klass));
  }
  // NOTE: needed too early in bootstrapping process to have checks based on JDK version
static klassOop reflect_magic_klass(){return check_klass(lvb_klassRef(&_reflect_magic_klass));}
static klassOop reflect_delegating_classloader_klass(){return check_klass(lvb_klassRef(&_reflect_delegating_classloader_klass));}
  static klassOop reflect_constant_pool_klass() { 
    return check_klass(lvb_klassRef(&_reflect_constant_pool_klass)); 
  }
  static klassOop reflect_unsafe_static_field_accessor_impl_klass() {
    return check_klass(lvb_klassRef(&_reflect_unsafe_static_field_accessor_impl_klass));
  }

  static klassOop vector_klass()            { return check_klass(lvb_klassRef(&_vector_klass)); }
  static klassOop hashtable_klass()         { return check_klass(lvb_klassRef(&_hashtable_klass)); }
  static klassOop stringBuffer_klass()      { return check_klass(lvb_klassRef(&_stringBuffer_klass)); }
  static klassOop stackTraceElement_klass() { return check_klass(lvb_klassRef(&_stackTraceElement_klass)); }

  static klassOop java_nio_Buffer_klass()   { return check_klass(lvb_klassRef(&_java_nio_Buffer_klass)); }

  static klassOop sun_misc_AtomicLongCSImpl_klass() { return lvb_klassRef(&_sun_misc_AtomicLongCSImpl_klass).as_klassOop(); }
  static klassOop com_azulsystems_util_Prefetch_klass() { return lvb_klassRef(&_com_azulsystems_util_Prefetch_klass).as_klassOop(); }
  static klassOop com_azulsystems_misc_BlockingHint_klass() { return lvb_klassRef(&_com_azulsystems_misc_BlockingHint_klass).as_klassOop(); }

  static klassOop boolean_klass()           { return check_klass(lvb_klassRef(&_boolean_klass)); }
  static klassOop char_klass()              { return check_klass(lvb_klassRef(&_char_klass)); }
  static klassOop float_klass()             { return check_klass(lvb_klassRef(&_float_klass)); }
  static klassOop double_klass()            { return check_klass(lvb_klassRef(&_double_klass)); }
  static klassOop byte_klass()              { return check_klass(lvb_klassRef(&_byte_klass)); }
  static klassOop short_klass()             { return check_klass(lvb_klassRef(&_short_klass)); }
  static klassOop int_klass()               { return check_klass(lvb_klassRef(&_int_klass)); }
  static klassOop long_klass()              { return check_klass(lvb_klassRef(&_long_klass)); } 

  static klassOop box_klass(BasicType t) {
    assert((uint)t < T_VOID+1, "range check");
return check_klass(lvb_klassRef(&_box_klasses[t]));
  }
  static BasicType box_klass_type(klassOop k);  // inverse of box_klass

  // methods returning lazily loaded klasses
  // The corresponding method to load the class must be called before calling them.
static klassOop abstract_ownable_synchronizer_klass(){return check_klass(lvb_klassRef(&_abstract_ownable_synchronizer_klass));}
  static klassRef abstract_ownable_synchronizer_klassref() { return lvb_klassRef(&_abstract_ownable_synchronizer_klass); }

  static void load_abstract_ownable_synchronizer_klass(TRAPS);

private:
  // Tells whether ClassLoader.loadClassInternal is present
  static bool has_loadClassInternal()       { return _has_loadClassInternal; }

public:
  // Tells whether ClassLoader.checkPackageAccess is present
  static bool has_checkPackageAccess()      { return _has_checkPackageAccess; }

  static bool class_klass_loaded()          { return _class_klass.not_null(); }
  static bool cloneable_klass_loaded()      { return _cloneable_klass.not_null(); }
  
  // Returns default system loader
  static oop java_system_loader();

  // Compute the default system loader
  static void compute_java_system_loader(TRAPS);

public:
  // Check class loader constraints
static bool add_loader_constraint(symbolHandle name,Handle loader1,Handle loader2,Thread*);
  static char* check_signature_loaders(symbolHandle signature, Handle loader1,
				       Handle loader2, bool is_method, TRAPS);

  // Utility for printing loader "name" as part of tracing constraints
  static const char* loader_name(objectRef loader) {
return(loader.is_null()?"<bootloader>":
instanceKlass::cast(loader.as_oop()->klass())->name()->as_C_string());
  }

  // Record the error when the first attempt to resolve a reference from a constant
  // pool entry to a class fails.
  static void add_resolution_error(constantPoolHandle pool, int which, symbolHandle error);
  static symbolOop find_resolution_error(constantPoolHandle pool, int which);

 private:

  enum Constants {
    _loader_constraint_size = 107,                     // number of entries in constraint table
    _resolution_error_size  = 107,		       // number of entries in resolution error table
    _nof_buckets            = 1009                     // number of buckets in hash table
  };


  // Static variables

  // Hashtable holding loaded classes.
  static Dictionary*            _dictionary;

  // Hashtable holding placeholders for classes being loaded.
  static PlaceholderTable*       _placeholders;

  // Hashtable holding classes from the shared archive.
  static Dictionary*             _shared_dictionary;

  // Monotonically increasing counter which grows with
  // _number_of_classes as well as hot-swapping and breakpoint setting
  // and removal.
static intptr_t _number_of_modifications;

  // Lock object for system class loader
  static objectRef               _system_loader_lock_obj;

  // Constraints on class loaders
  static LoaderConstraintTable*  _loader_constraints;

  // Resolution errors
  static ResolutionErrorTable*	 _resolution_errors;

public:
  // for VM_CounterDecay iteration support
  friend class CounterDecay;
  static klassOop try_get_next_class();

private:
  static void validate_protection_domain(instanceKlassHandle klass,
                                         Handle class_loader,
                                         Handle protection_domain, TRAPS);
  friend class TraversePlaceholdersClosure;
  static Dictionary*         dictionary() { return _dictionary; }
  static Dictionary*         shared_dictionary() { return _shared_dictionary; }
  static PlaceholderTable*   placeholders() { return _placeholders; }
  static LoaderConstraintTable* constraints() { return _loader_constraints; }
  static ResolutionErrorTable* resolution_errors() { return _resolution_errors; }

  // Basic loading operations
  static klassOop resolve_instance_class_or_null(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS);
  static klassOop resolve_array_class_or_null(symbolHandle class_name, Handle class_loader, Handle protection_domain, TRAPS);
  static instanceKlassHandle handle_parallel_super_load(symbolHandle class_name, symbolHandle supername, Handle class_loader, Handle protection_domain, Handle lockObject, TRAPS);
  // Wait on SystemDictionary_lock; unlocks lockObject before 
  // waiting; relocks lockObject with correct recursion count
  // after waiting, but before reentering SystemDictionary_lock
  // to preserve lock order semantics.
  static void double_lock_wait(Handle lockObject, TRAPS);
  static void define_instance_class(instanceKlassHandle k, TRAPS);
  static instanceKlassHandle find_or_define_instance_class(symbolHandle class_name, 
                                                Handle class_loader, 
                                                instanceKlassHandle k, TRAPS);
  static instanceKlassHandle load_instance_class(symbolHandle class_name, Handle class_loader, TRAPS);
static objectRef compute_loader_lock_object(Handle class_loader,TRAPS);
  static void check_loader_lock_contention(objectRef loader_lock, Thread *self);

  static klassOop find_shared_class(symbolHandle class_name);

  // Setup link to hierarchy
static void add_to_hierarchy(instanceKlassHandle k);

private:
  // We pass in the hashtable index so we can calculate it outside of
  // the SystemDictionary_lock.   

  // Basic find on loaded classes 
  static klassOop find_class(int index, unsigned int hash,
                             symbolHandle name, Handle loader);

  // Basic find on classes in the midst of being loaded
  static symbolOop find_placeholder(int index, unsigned int hash,
                                    symbolHandle name, Handle loader);

  // Basic find operation of loaded classes and classes in the midst
  // of loading;  used for assertions and verification only.
  static oop find_class_or_placeholder(symbolHandle class_name,
                                       Handle class_loader);

  // Updating entry in dictionary
  // Add a completely loaded class 
  static void add_klass(int index, symbolHandle class_name,
                        Handle class_loader, KlassHandle obj);

  // Add a placeholder for a class being loaded
  static void add_placeholder(int index, 
                              symbolHandle class_name, 
                              Handle class_loader);
  static void remove_placeholder(int index,
                                 symbolHandle class_name, 
                                 Handle class_loader);

  // Performs cleanups after resolve_super_or_fail. This typically needs
  // to be called on failure.
  // Won't throw, but can block.
  static void resolution_cleanups(symbolHandle class_name,
                                  Handle class_loader,
                                  TRAPS);
  
  // Initialization
  static void initialize_preloaded_classes(TRAPS);
    
  // Class loader constraints
  static void check_constraints(int index, unsigned int hash,
                                instanceKlassHandle k, Handle loader, 
                                bool defining, TRAPS);
  static void update_dictionary(int d_index, unsigned int d_hash,
                                int p_index, unsigned int p_hash,
                                instanceKlassHandle k, Handle loader, TRAPS);

  // Variables holding commonly used klasses (preloaded)
  static klassRef _object_klass;
  static klassRef _string_klass;
  static klassRef _class_klass;
  static klassRef _cloneable_klass;
  static klassRef _classloader_klass;
  static klassRef _serializable_klass;
  static klassRef _system_klass;
  
  static klassRef _throwable_klass;
  static klassRef _error_klass;
  static klassRef _threaddeath_klass;
  static klassRef _exception_klass;
  static klassRef _runtime_exception_klass;
  static klassRef _classNotFoundException_klass;
  static klassRef _noClassDefFoundError_klass;
  static klassRef _linkageError_klass;
static klassRef _nullPointerException_klass;
static klassRef _arrayIndexOutOfBoundsException_klass;
  static klassRef _classCastException_klass;
  static klassRef _arrayStoreException_klass;
  static klassRef _virtualMachineError_klass;
  static klassRef _outOfMemoryError_klass;
  static klassRef _StackOverflowError_klass;
  static klassRef _illegalMonitorStateException_klass;
  static klassRef _protectionDomain_klass;
  static klassRef _AccessControlContext_klass;
  static klassRef _reference_klass;
  static klassRef _soft_reference_klass;
  static klassRef _weak_reference_klass;
  static klassRef _final_reference_klass;
  static klassRef _phantom_reference_klass;
  static klassRef _finalizer_klass;

  static klassRef _thread_klass;
  static klassRef _threadGroup_klass;
  static klassRef _properties_klass;      
  static klassRef _reflect_accessible_object_klass;
  static klassRef _reflect_field_klass;
  static klassRef _reflect_method_klass;
  static klassRef _reflect_constructor_klass;
  // 1.4 reflection implementation
  static klassRef _reflect_magic_klass;
  static klassRef _reflect_method_accessor_klass;
  static klassRef _reflect_constructor_accessor_klass;
  static klassRef _reflect_delegating_classloader_klass;
  // 1.5 annotations implementation
  static klassRef _reflect_constant_pool_klass;
  static klassRef _reflect_unsafe_static_field_accessor_impl_klass;

  static klassRef _stringBuffer_klass;
  static klassRef _vector_klass;
  static klassRef _hashtable_klass;

  static klassRef _stackTraceElement_klass;

  static klassRef _java_nio_Buffer_klass;

  static klassRef _sun_misc_AtomicLongCSImpl_klass;
static klassRef _com_azulsystems_util_Prefetch_klass;
static klassRef _com_azulsystems_misc_BlockingHint_klass;

  // Lazily loaded klasses
static klassRef _abstract_ownable_synchronizer_klass;

  // Box klasses
  static klassRef _boolean_klass;
  static klassRef _char_klass;
  static klassRef _float_klass;
  static klassRef _double_klass;
  static klassRef _byte_klass;
  static klassRef _short_klass;
  static klassRef _int_klass;
  static klassRef _long_klass;

  // table of same
  static klassRef _box_klasses[T_VOID+1];

  static objectRef _java_system_loader;

  static bool _has_loadClassInternal;
  static bool _has_checkPackageAccess;
};

#endif // SYSTEMDICTIONARY_HPP
