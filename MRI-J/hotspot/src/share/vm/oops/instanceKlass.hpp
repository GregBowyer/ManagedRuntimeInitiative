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
#ifndef INSTANCEKLASS_HPP
#define INSTANCEKLASS_HPP


#include "constMethodOop.hpp"
#include "instanceOop.hpp"
#include "handles.hpp"
#include "klass.hpp"
#include "growableArray.hpp"
#include "objArrayOop.hpp"
#include "typeArrayOop.hpp"
#include "klassVtable.hpp"

#include "lvb_pd.inline.hpp"

// forward declaration for class -- see below for definition
class BitMap;
class BreakpointInfo;
class FieldClosure;
class JNIid;
class JvmtiCachedClassFieldMap;
class OopMapBlock;
class PreviousVersionNode;
class SuperTypeClosure;
class fieldDescriptor;
class jfieldIDCache;
class jniIdMapBase;
class klassItable;
class CodeBlob;

// An instanceKlass is the VM level representation of a Java class. 
// It contains all information needed for at class at execution runtime.

//  instanceKlass layout:
//    [header                     ] klassOop
//    [klass pointer              ] klassOop
//    [C++ vtbl pointer           ] Klass
//    [subtype cache              ] Klass
//    [instance size              ] Klass
//    [java mirror                ] Klass
//    [super                      ] Klass
//    [access_flags               ] Klass 
//    [name                       ] Klass
//    [first subklass             ] Klass
//    [next sibling               ] Klass
//    [array klasses              ]
//    [methods                    ]
//    [local interfaces           ]
//    [transitive interfaces      ]
//    [implementors               ] klassOop[2]
//    [fields                     ]
//    [constants                  ]
//    [class loader               ]
//    [protection domain          ]
//    [signers                    ]
//    [source file name           ]
//    [inner classes              ]
//    [static field size          ]
//    [nonstatic field size       ]
//    [static oop fields size     ]
//    [nonstatic oop maps size    ]
//    [has finalize method        ]
//    [initialization state       ]
//    [initializing thread        ]
//    [Java vtable length         ]
//    [oop map cache (stack maps) ]
//    [EMBEDDED Java vtable             ] size in words = vtable_len
//    [EMBEDDED static oop fields       ] size in words = static_oop_fields_size
//    [         static non-oop fields   ] size in words = static_field_size - static_oop_fields_size
//    [EMBEDDED nonstatic oop-map blocks] size in words = nonstatic_oop_map_size
//
//    The embedded nonstatic oop-map blocks are short pairs (offset, length) indicating
//    where oops are located in instances of this klass.


class instanceKlass: public Klass {
 public:
  // See "The Java Virtual Machine Specification" section 2.16.2-5 for a detailed description 
  // of the class loading & initialization procedure, and the use of the states.
  enum ClassState {
    unparsable_by_gc = 0,               // object is not yet parsable by gc. Value of _init_state at object allocation.
    allocated,                          // allocated (but not yet linked)
    loaded,                             // loaded and inserted in class hierarchy (but not linked yet)
    linked,                             // successfully linked/verified (but not initialized yet)
    being_initialized,                  // currently running class initializer
    fully_initialized,                  // initialized (successfull final state)
    initialization_error                // error happened during initialization
  };

 public:
  heapRef* oop_block_beg() const { return adr_array_klasses(); }
heapRef*oop_block_end()const{return adr_dependent_mco()+1;}

  enum {
    implementors_limit = 2              // how many implems can we track?
  };

 protected:
  // 
  // The oop block.  See comment in klass.hpp before making changes.
  // 

  // Array classes holding elements of this class.
  klassRef        _array_klasses;
  // Method array.
  objArrayRef     _methods;
  // Int array containing the original order of method in the class file (for
  // JVMTI).
  typeArrayRef    _method_ordering;
  // Interface (klassOops) this class declares locally to implement.
  objArrayRef     _local_interfaces;
  // Interface (klassOops) this class implements transitively.
  objArrayRef     _transitive_interfaces;
  // Instance and static variable information, 5-tuples of shorts [access, name
  // index, sig index, initval index, offset].
  typeArrayRef    _fields;
  // Constant pool for this class.
  constantPoolRef _constants;
  // Class loader used to load this class, NULL if VM loader used.
  objectRef       _class_loader;
  // Protection domain.
  objectRef       _protection_domain;
  // Class signers.
  objArrayRef     _signers;
  // Name of source file containing this klass, NULL if not specified.
  symbolRef       _source_file_name;
  // the source debug extension for this klass, NULL if not specified.
  symbolRef       _source_debug_extension;
  // inner_classes attribute.
  typeArrayRef    _inner_classes;
  // Implementor of this interface (not valid if it overflows)
klassRef _implementor;
  // Generic signature, or null if none.
  symbolRef       _generic_signature;
  // Annotations for this class, or null if none.
  typeArrayRef    _class_annotations;
  // Annotation objects (byte arrays) for fields, or null if no annotations.
  // Indices correspond to entries (not indices) in fields array.
  objArrayRef     _fields_annotations;
  // Annotation objects (byte arrays) for methods, or null if no annotations.
  // Index is the idnum, which is initially the same as the methods array index.
  objArrayRef     _methods_annotations;
  // Annotation objects (byte arrays) for methods' parameters, or null if no
  // such annotations. 
  // Index is the idnum, which is initially the same as the methods array index.
  objArrayRef     _methods_parameter_annotations;
  // Annotation objects (byte arrays) for methods' default values, or null if no
  // such annotations.  
  // Index is the idnum, which is initially the same as the methods array index.
  objArrayRef     _methods_default_annotations;
  // List of methodCodeOops dependent on this klass not being overridden somehow
objArrayRef _dependent_mcoRef;

  //
  // End of the oop block.
  //

  int             _nonstatic_field_size; // number of non-static fields in this klass (including inherited fields)
  int             _static_field_size;    // number of static fields (oop and non-oop) in this klass
  int             _static_oop_field_size;// number of static oop fields in this klass
  int             _nonstatic_oop_map_size;// number of nonstatic oop-map blocks allocated at end of this klass
  bool            _rewritten;            // methods rewritten.
  u2              _minor_version;        // minor version number of class file
  u2              _major_version;        // major version number of class file
  ClassState      _init_state;           // state of class
  Thread*         _init_thread;          // Pointer to current thread doing initialization (to handle recusive initialization)
  int             _vtable_len;           // length of Java vtable (in words)
  int             _itable_len;           // length of Java itable (in words)
  ReferenceType   _reference_type;       // reference type
  jfieldIDCache*  _jfieldid_cache;       // List of jfields accessed from the proxy
  JNIid*          _jni_ids;              // First JNI identifier for static fields in this class
  jmethodID*      _methods_jmethod_ids;  // jmethodIDs corresponding to method_idnum, or NULL if none
  int*            _methods_cached_itable_indices;  // itable_index cache for JNI invoke corresponding to methods idnum, or NULL
  BreakpointInfo* _breakpoints;          // bpt lists, managed by methodOop
  // Array of interesting part(s) of the previous version(s) of this
  // instanceKlass. See PreviousVersionWalker below.
  GrowableArray<PreviousVersionNode *>* _previous_versions;
  u2              _enclosing_method_class_index;  // Constant pool index for class of enclosing method, or 0 if none
  u2              _enclosing_method_method_index; // Constant pool index for name and type of enclosing method, or 0 if none
  // JVMTI fields can be moved to their own structure - see 6315920
  unsigned char * _cached_class_file_bytes;       // JVMTI: cached class file, before retransformable agent modified it in CFLH
  jint            _cached_class_file_len;         // JVMTI: length of above
  JvmtiCachedClassFieldMap* _jvmti_cached_class_field_map;  // JVMTI: used during heap iteration
  volatile u2     _idnum_allocated_count;         // JNI/JVMTI: increments with the addition of methods, old ids don't change
private:

  // embedded Java vtable follows here
  // embedded Java itables follows here
  // embedded static fields follows here
  // embedded nonstatic oop-map blocks follows here

  friend class instanceKlassKlass;
  friend class SystemDictionary;
  friend class ClassLoadingService;
  friend class Dictionary;

 public:
  // field sizes
  int nonstatic_field_size() const         { return _nonstatic_field_size; }
  void set_nonstatic_field_size(int size)  { _nonstatic_field_size = size; }
  
  int static_field_size() const            { return _static_field_size; }
  void set_static_field_size(int size)     { _static_field_size = size; }
  
  int static_oop_field_size() const        { return _static_oop_field_size; }
  void set_static_oop_field_size(int size) { _static_oop_field_size = size; }
  
  // Java vtable
  int  vtable_length() const               { return _vtable_len; }
  void set_vtable_length(int len)          { _vtable_len = len; }

  // Java itable
  int  itable_length() const               { return _itable_len; }
  void set_itable_length(int len)          { _itable_len = len; }
  
  // array klasses
klassOop array_klasses()const{return lvb_klassRef(&_array_klasses).as_klassOop();}
klassRef array_klasses_ref()const{return lvb_klassRef(&_array_klasses);}
  void set_array_klasses(klassOop k)       { ref_store_without_check(&_array_klasses, klassRef(k)); }

  // methods
objArrayOop methods()const{return lvb_objArrayRef(&_methods).as_objArrayOop();}
void set_methods(objArrayOop a){ref_store_without_check(&_methods,objArrayRef(a));}
  methodOop method_with_idnum(int idnum);

  // method ordering
typeArrayOop method_ordering()const{return lvb_typeArrayRef(&_method_ordering).as_typeArrayOop();}
void set_method_ordering(typeArrayOop m){ref_store_without_check(&_method_ordering,typeArrayRef(m));}

  // interfaces
objArrayOop local_interfaces()const{return lvb_objArrayRef(&_local_interfaces).as_objArrayOop();}
void set_local_interfaces(objArrayOop a){ref_store_without_check(&_local_interfaces,objArrayRef(a));}
objArrayOop transitive_interfaces()const{return lvb_objArrayRef(&_transitive_interfaces).as_objArrayOop();}
void set_transitive_interfaces(objArrayOop a){ref_store_without_check(&_transitive_interfaces,objArrayRef(a));}

  // fields
  // Field info extracted from the class file and stored
  // as an array of 7 shorts 
  enum FieldOffset {
    access_flags_offset    = 0,
    name_index_offset      = 1,
    signature_index_offset = 2,
    initval_index_offset   = 3,
    low_offset             = 4,
    high_offset            = 5,
    generic_signature_offset = 6,
    next_offset            = 7
  };

  typeArrayOop fields() const              { return lvb_typeArrayRef(&_fields).as_typeArrayOop(); }
  int offset_from_fields( int index ) const {
    return build_int_from_shorts( fields()->ushort_at(index + low_offset),
                                  fields()->ushort_at(index + high_offset) );
  }
 
  void set_fields(typeArrayOop f)          { ref_store_without_check(&_fields, typeArrayRef(f)); }

  // inner classes
typeArrayOop inner_classes()const{return lvb_typeArrayRef(&_inner_classes).as_typeArrayOop();}
void set_inner_classes(typeArrayOop f){ref_store_without_check(&_inner_classes,typeArrayRef(f));}

  enum InnerClassAttributeOffset {
    // From http://mirror.eng/products/jdk/1.1/docs/guide/innerclasses/spec/innerclasses.doc10.html#18814
    inner_class_inner_class_info_offset = 0,
    inner_class_outer_class_info_offset = 1,
    inner_class_inner_name_offset = 2,
    inner_class_access_flags_offset = 3,
    inner_class_next_offset = 4
  };

  // package
  bool is_same_class_package(klassOop class2);
  bool is_same_class_package(oop classloader2, symbolOop classname2);
  static bool is_same_class_package(oop class_loader1, symbolOop class_name1, oop class_loader2, symbolOop class_name2);
  
  // initialization state  
  bool is_loaded() const                   { return _init_state >= loaded; }
  bool is_linked() const                   { return _init_state >= linked; }
  bool is_initialized() const              { return _init_state == fully_initialized; }
  bool is_not_initialized() const          { return _init_state <  being_initialized; }
  bool is_being_initialized() const        { return _init_state == being_initialized; }
  bool is_in_error_state() const           { return _init_state == initialization_error; }
  bool is_reentrant_initialization(Thread *thread)  { return thread == _init_thread; }
  int  get_init_state()                    { return _init_state; } // Useful for debugging
  bool is_rewritten() const                { return _rewritten; }

  // initialization (virtuals from Klass)
  bool should_be_initialized() const;  // means that initialize should be called
  void initialize(TRAPS);
  void link_class(TRAPS);
  bool link_class_or_fail(TRAPS); // returns false on failure
  void unlink_class();
  void rewrite_class(TRAPS);
  methodOop class_initializer();
  
  // set the class to initialized if no static initializer is present
  void eager_initialize(Thread *thread);

  // reference type
  ReferenceType reference_type() const     { return _reference_type; }
  void set_reference_type(ReferenceType t) { _reference_type = t; }

  // find local field, returns true if found
  bool find_local_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const;
  // find field in direct superinterfaces, returns the interface in which the field is defined
  klassOop find_interface_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const;
  // find field according to JVM spec 5.4.3.2, returns the klass in which the field is defined
  klassOop find_field(symbolOop name, symbolOop sig, fieldDescriptor* fd) const;
  // find instance or static fields according to JVM spec 5.4.3.2, returns the klass in which the field is defined
  klassOop find_field(symbolOop name, symbolOop sig, bool is_static, fieldDescriptor* fd) const;
  
  // find a non-static or static field given its offset within the class.
  bool contains_field_offset(int offset) { 
      return ((offset/wordSize) >= instanceOopDesc::header_size() && 
             (offset/wordSize)-instanceOopDesc::header_size() < nonstatic_field_size()); 
  }

  bool find_local_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const;
  bool find_field_from_offset(int offset, bool is_static, fieldDescriptor* fd) const;
  
  // find a local method (returns NULL if not found)
  methodOop find_method(symbolOop name, symbolOop signature) const;
  static methodOop find_method(objArrayOop methods, symbolOop name, symbolOop signature);

  // lookup operation (returns NULL if not found)
  methodOop uncached_lookup_method(symbolOop name, symbolOop signature) const;

  // lookup a method in all the interfaces that this class implements
  // (returns NULL if not found)
  methodOop lookup_method_in_all_interfaces(symbolOop name, symbolOop signature) const;
  
  // constant pool
constantPoolOop constants()const{return lvb_constantPoolRef(&_constants).as_constantPoolOop();}
constantPoolRef constantsRef()const{return lvb_constantPoolRef(&_constants);}
  void set_constants(constantPoolOop c)    { ref_store_without_check(&_constants, constantPoolRef(c)); }
 
  // class loader
oop class_loader()const{return lvb_ref(&_class_loader).as_oop();}
oop GC_class_loader()const{return _class_loader.as_oop();}
  void set_class_loader(oop l)             { ref_store(objectRef(as_klassOop()), &_class_loader, objectRef(l)); }
  objectRef class_loader_ref() const       { return lvb_ref(&_class_loader); }
 
  // protection domain
oop protection_domain(){return lvb_ref(&_protection_domain).as_oop();}
void set_protection_domain(oop pd){ref_store(objectRef(as_klassOop()),&_protection_domain,objectRef(pd));}

  // signers
  objArrayOop signers() const              { return lvb_objArrayRef(&_signers).as_objArrayOop(); }
  void set_signers(objArrayOop s)          { ref_store(objectRef(as_klassOop()), &_signers, objArrayRef(s)); }

  // source file name
symbolOop source_file_name()const{return lvb_symbolRef(&_source_file_name).as_symbolOop();}
void set_source_file_name(symbolOop n){ref_store_without_check(&_source_file_name,symbolRef(n));}

  // minor and major version numbers of class file
  u2 minor_version() const                 { return _minor_version; }
  void set_minor_version(u2 minor_version) { _minor_version = minor_version; }
  u2 major_version() const                 { return _major_version; }
  void set_major_version(u2 major_version) { _major_version = major_version; }

  // source debug extension
symbolOop source_debug_extension()const{return lvb_symbolRef(&_source_debug_extension).as_symbolOop();}
void set_source_debug_extension(symbolOop n){ref_store_without_check(&_source_debug_extension,symbolRef(n));}

  // nonstatic oop-map blocks
  int nonstatic_oop_map_size() const        { return _nonstatic_oop_map_size; }
  void set_nonstatic_oop_map_size(int size) { _nonstatic_oop_map_size = size; }

  // RedefineClasses() support for previous versions:
  void add_previous_version(instanceKlassHandle ikh, BitMap *emcp_methods,
         int emcp_method_count);
  bool has_previous_version() const;
  void init_previous_versions() {
    _previous_versions = NULL;
  }
  GrowableArray<PreviousVersionNode *>* previous_versions() const {
    return _previous_versions;
  }

  // JVMTI: Support for caching a class file before it is modified by an agent that can do retransformation
  void set_cached_class_file(unsigned char *class_file_bytes,
                             jint class_file_len)     { _cached_class_file_len = class_file_len;
                                                        _cached_class_file_bytes = class_file_bytes; }
  jint get_cached_class_file_len()                    { return _cached_class_file_len; }
  unsigned char * get_cached_class_file_bytes()       { return _cached_class_file_bytes; }

  // JVMTI: Support for caching of field indices, types, and offsets 
  void set_jvmti_cached_class_field_map(JvmtiCachedClassFieldMap* descriptor) { 
    _jvmti_cached_class_field_map = descriptor;
  }
  JvmtiCachedClassFieldMap* jvmti_cached_class_field_map() const { 
    return _jvmti_cached_class_field_map; 
  }

  // for adding methods, constMethodOopDesc::UNSET_IDNUM means no more ids available
  inline u2 next_method_idnum();
  void set_initial_method_idnum(u2 value)             { _idnum_allocated_count = value; }

  // generics support
symbolOop generic_signature()const{return lvb_symbolRef(&_generic_signature).as_symbolOop();}
void set_generic_signature(symbolOop sig){ref_store_without_check(&_generic_signature,(symbolRef)sig);}
  u2 enclosing_method_class_index() const             { return _enclosing_method_class_index; }
  u2 enclosing_method_method_index() const            { return _enclosing_method_method_index; }
  void set_enclosing_method_indices(u2 class_index,
                                    u2 method_index)  { _enclosing_method_class_index  = class_index;
                                                        _enclosing_method_method_index = method_index; }

  // jmethodID support
  static jmethodID jmethod_id_for_impl(instanceKlassHandle ik_h, methodHandle method_h);      
  jmethodID jmethod_id_or_null(methodOop method);      

  // cached itable index support
  void set_cached_itable_index(size_t idnum, int index);
  int cached_itable_index(size_t idnum);

  // annotations support
typeArrayOop class_annotations()const{return lvb_typeArrayRef(&_class_annotations).as_typeArrayOop();}
objArrayOop fields_annotations()const{return lvb_objArrayRef(&_fields_annotations).as_objArrayOop();}
objArrayOop methods_annotations()const{return lvb_objArrayRef(&_methods_annotations).as_objArrayOop();}
objArrayOop methods_parameter_annotations()const{return lvb_objArrayRef(&_methods_parameter_annotations).as_objArrayOop();}
objArrayOop methods_default_annotations()const{return lvb_objArrayRef(&_methods_default_annotations).as_objArrayOop();}
void set_class_annotations(typeArrayOop md){ref_store_without_check(&_class_annotations,(typeArrayRef)md);}
  void set_fields_annotations(objArrayOop md)            { set_annotations(md, &_fields_annotations); }
  void set_methods_annotations(objArrayOop md)           { set_annotations(md, &_methods_annotations); }
  void set_methods_parameter_annotations(objArrayOop md) { set_annotations(md, &_methods_parameter_annotations); }
  void set_methods_default_annotations(objArrayOop md)   { set_annotations(md, &_methods_default_annotations); }
  typeArrayOop get_method_annotations_of(int idnum)    
{return get_method_annotations_from(idnum,&_methods_annotations);}
  typeArrayOop get_method_parameter_annotations_of(int idnum)
{return get_method_annotations_from(idnum,&_methods_parameter_annotations);}
  typeArrayOop get_method_default_annotations_of(int idnum)
{return get_method_annotations_from(idnum,&_methods_default_annotations);}
  void set_method_annotations_of(int idnum, typeArrayOop anno)          
                                                { set_methods_annotations_of(idnum, anno, &_methods_annotations); }
  void set_method_parameter_annotations_of(int idnum, typeArrayOop anno)
{set_methods_annotations_of(idnum,anno,&_methods_parameter_annotations);}
  void set_method_default_annotations_of(int idnum, typeArrayOop anno)  
{set_methods_annotations_of(idnum,anno,&_methods_default_annotations);}
  
  // Dependent methodCodeOop support.
  objArrayOop dependent_mcos() const            { return lvb_objArrayRef(&_dependent_mcoRef).as_objArrayOop(); }
  objArrayRef dependent_mcos_ref() const        { return lvb_objArrayRef(&_dependent_mcoRef); }
  void set_dependent_mcos(objArrayOop doop)     { ref_store(objectRef(as_klassOop()),&_dependent_mcoRef, objArrayRef(doop)); }
heapRef*adr_dependent_mco()const{return(heapRef*)&this->_dependent_mcoRef;}

  // allocation
  DEFINE_ALLOCATE_PERMANENT(instanceKlass);
instanceOop allocate_instance(intptr_t sba_hint,TRAPS);
  instanceOop allocate_permanent_instance(TRAPS);

  // additional member function to return a handle
instanceHandle allocate_instance_handle(intptr_t sba_hint,TRAPS){return instanceHandle(THREAD,allocate_instance(sba_hint,THREAD));}

objArrayOop allocate_objArray(int n,int length,intptr_t sba_hint,TRAPS);
  // Helper function
  static instanceOop register_finalizer(instanceOop i, TRAPS);

  // Check whether reflection/jni/jvm code is allowed to instantiate this class;
  // if not, throw either an Error or an Exception.
  virtual void check_valid_for_instantiation(bool throwError, TRAPS);

  // initialization
  void call_class_initializer(TRAPS);
  void set_initialization_state_and_notify(ClassState state, TRAPS);

  // jfieldID cache support
  jfieldIDCache *jfieldid_cache()                { return _jfieldid_cache; }
  jfieldIDCache *get_or_create_jfieldid_cache();

  // JNI identifier support (for static fields - for jni performance)
  JNIid* jni_ids()                               { return _jni_ids; }
  void set_jni_ids(JNIid* ids)                   { _jni_ids = ids; }
  JNIid* jni_id_for(int offset);

  // maintenance of deoptimization dependencies
  void print_dependencies() const;
  bool add_dependent_codeblob(CodeBlob* cb, TRAPS);
  void deoptimize_evol_dependent_on( ) const;
  void deoptimize_code_dependent_on( ) const;
  void deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl(const instanceKlass *new_subclass) const;
  void deoptimize_codeblobrefs_dependent_on_class_hierarchy(const instanceKlass *new_subclass) const {
    if (_dependent_mcoRef.not_null())
      deoptimize_codeblobrefs_dependent_on_class_hierarchy_impl(new_subclass);
  }
  void deoptimize_codeblobrefs_dependent_on_method(methodOop moop);
  static void verify_dependencies(Klass *, int gc_mode, BoolObjectClosure* is_alive) PRODUCT_RETURN;
  static void gpgc_verify_klass_chain(Klass *);

  // Breakpoint support (see methods on methodOop for details)
  BreakpointInfo* breakpoints() const       { return _breakpoints; };
  void set_breakpoints(BreakpointInfo* bps) { _breakpoints = bps; };

  // support for stub routines
  static int init_state_offset_in_bytes()    { return offset_of(instanceKlass, _init_state); }
  static int init_thread_offset_in_bytes()   { return offset_of(instanceKlass, _init_thread); }

  // subclass/subinterface checks
  bool implements_interface(klassOop k) const;

  // Public accessible read of _implementor in case we're racey on reading the number of implementors
  intptr_t raw_implementor() const { return *(intptr_t*)&_implementor; }

  // If there is exactly one concrete (not abstract) implementor of this
  // interface then return it or NULL otherwise.  Does not distinguish between
  // NO implementors and MANY implementors.
  klassRef implementor(intptr_t raw) const { 
    return (raw == -1) ? klassRef() : lvb_klassRef_loaded(raw, &_implementor);
  }
klassRef implementor()const{
    return implementor(raw_implementor());
  }
  bool implementor_not_null(intptr_t raw) const {
    return (raw!=-1) && (raw!=0);
  }
  bool implementor_not_null() const {
    return implementor_not_null(raw_implementor()); 
  }

  // Return number of concrete (not abstract) implementors of this interface.
  // Encoded in the _implementor field: NULL means 0, -1 means MANY (2+) and
  // everthing else means 1.
  static int nof_implementors(intptr_t raw) {
    return (raw == -1) ? 2 // -1 is MANY
      : ((raw == 0) ? 0 : 1); // 0 is null, other is 1
  }
  int nof_implementors() const {
    return nof_implementors(raw_implementor());
  }

  // Track number of concrete implementors of interfaces.
  void process_interface_and_abstract();
void dec_implementor(BoolObjectClosure*is_alive);
void dec_implementor_impl(BoolObjectClosure*is_alive);
  void PGC_dec_implementor();
  void PGC_dec_implementor_impl();
  void GPGC_dec_implementor();
  void GPGC_dec_implementor_impl();

  // virtual operations from Klass
  bool is_leaf_class() const               { return (*adr_subklass()).is_null(); }
  objArrayOop compute_secondary_supers(int num_extra_slots, TRAPS);
  bool compute_is_subtype_of(klassOop k);
  bool can_be_primary_super_slow() const;
  klassOop java_super() const              { return super(); }
  int oop_size(oop obj)  const             { return size_helper(); }
int GC_oop_size(oop obj)const{return size_helper();}
  int klass_oop_size() const               { return object_size(); }
  bool oop_is_instance_slow() const        { return true; }

  // Iterators
void do_local_static_fields(FieldClosure*f,oop obj);
  void do_local_static_fields(void f(fieldDescriptor*, TRAPS), TRAPS);
  void do_nonstatic_fields(FieldClosure *f, oop obj); // including inherited fields
  void methods_do(void f(methodOop method));
  void array_klasses_do(void f(klassOop k));
  void with_array_klasses_do(void f(klassOop k));
  bool super_types_do(SuperTypeClosure* blk);

  // Casting from klassOop
  static instanceKlass* cast(klassOop k) {
    Klass* kp = k->klass_part();
    assert(kp->null_vtbl() || kp->oop_is_instance_slow(), "cast to instanceKlass");
    return (instanceKlass*) kp;
  }

  // Sizing (in words)
static int header_size(){return oopDesc::header_size()+sizeof(instanceKlass)/HeapWordSize;}
int object_size()const{return object_size(vtable_length()+itable_length()+static_field_size()+nonstatic_oop_map_size());}
  static int vtable_start_offset()    { return header_size(); }
  static int vtable_length_offset()   { return oopDesc::header_size() + offset_of(instanceKlass, _vtable_len) / HeapWordSize; }
static int object_size(int extra){return header_size()+extra;}

  intptr_t* start_of_vtable() const        { return ((intptr_t*)as_klassOop()) + vtable_start_offset(); }
intptr_t*start_of_itable()const{return start_of_vtable()+vtable_length();}
  int  itable_offset_in_words() const { return start_of_itable() - (intptr_t*)as_klassOop(); }

  heapRef* start_of_static_fields() const  { return (heapRef*)(start_of_itable() + itable_length()); }
  intptr_t* end_of_itable() const          { return start_of_itable() + itable_length(); }
  heapRef* end_of_static_fields() const    { return start_of_static_fields() + static_field_size(); }
  int offset_of_static_fields() const      { return (intptr_t)start_of_static_fields() - (intptr_t)as_klassOop(); }

  OopMapBlock* start_of_nonstatic_oop_maps() const { return (OopMapBlock*) (start_of_static_fields() + static_field_size()); }

  // Allocation profiling support
  juint alloc_size() const            { return _alloc_count * size_helper(); }
  void set_alloc_size(juint n)        {}

  // Use this to return the size of an instance in heap words:
  int size_helper() const {
    return layout_helper_to_size_helper(layout_helper());
  }

  // This bit is initialized in classFileParser.cpp.
  // It is false under any of the following conditions:
  //  - the class is abstract (including any interface)
  //  - the class has a finalizer (if !RegisterFinalizersAtInit)
  //  - the class size is larger than FastAllocateSizeLimit
  //  - the class is java/lang/Class, which cannot be allocated directly
  bool can_be_fastpath_allocated() const {
    return !layout_helper_needs_slow_path(layout_helper());
  }

  // Java vtable/itable
  klassVtable* vtable() const;        // return new klassVtable wrapper
  inline methodOop method_at_vtable(int index);
  klassItable* itable() const;        // return new klassItable wrapper
  methodOop method_at_itable(klassOop holder, int index, TRAPS);

  // Garbage collection
  void oop_follow_contents(oop obj);
  void follow_static_fields();
  void adjust_static_fields();
  int  oop_adjust_pointers(oop obj);
  bool object_is_parsable() const { return _init_state != unparsable_by_gc; }
       // Value of _init_state must be zero (unparsable_by_gc) when klass field is set.

  void follow_weak_klass_links(
    BoolObjectClosure* is_alive, OopClosure* keep_alive);
   void PGC_follow_weak_klass_links();
   void GPGC_follow_weak_klass_links();
   void release_C_heap_structures();

  // Parallel Scavenge and Parallel Old
  PARALLEL_GC_DECLS

  // Parallel Scavenge
  void copy_static_fields(PSPromotionManager* pm);
  void push_static_fields(PSPromotionManager* pm);

  // Parallel Old
  void follow_static_fields(ParCompactionManager* cm);
  void copy_static_fields(ParCompactionManager* cm);
  void update_static_fields();
  void update_static_fields(HeapWord* beg_addr, HeapWord* end_addr);

  // GenPauselessGC
  void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm, oop obj);
  void GPGC_follow_static_fields(GPGC_GCManagerOldStrong* gcm);
  void GPGC_follow_static_fields(GPGC_GCManagerOldFinal* gcm);
void GPGC_newgc_oop_update_cardmark(oop obj);
void GPGC_oldgc_oop_update_cardmark(oop obj);
void GPGC_mutator_oop_update_cardmark(oop obj);
  void GPGC_static_fields_update_cardmark();
  
  void GPGC_sweep_weak_codeblobrefs(GPGC_GCManagerOldStrong* gcm);
  void GPGC_deoptimize_codeblobrefs_dependent_on_method(methodOop moop) const;
  
  // Naming
  char* signature_name() const;

  // Iterators
  int oop_oop_iterate(oop obj, OopClosure* blk) {
    return oop_oop_iterate_v(obj, blk);
  }

  int oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
    return oop_oop_iterate_v_m(obj, blk, mr);
  }

#define InstanceKlass_OOP_OOP_ITERATE_DECL(OopClosureType, nv_suffix)   \
  int  oop_oop_iterate##nv_suffix(oop obj, OopClosureType* blk);        \
  int  oop_oop_iterate##nv_suffix##_m(oop obj, OopClosureType* blk,     \
                                      MemRegion mr);

  ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceKlass_OOP_OOP_ITERATE_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_3(InstanceKlass_OOP_OOP_ITERATE_DECL)

  void iterate_static_fields(OopClosure* closure);
  void iterate_static_fields(OopClosure* closure, MemRegion mr);

  // initialization state  
#ifdef ASSERT
  void set_init_state(ClassState state);  
#else
  void set_init_state(ClassState state) { _init_state = state; }
#endif
  void set_rewritten()                  { _rewritten = true; }
  void set_init_thread(Thread *thread)  { _init_thread = thread; }

private:
  u2 idnum_allocated_count() const      { return _idnum_allocated_count; }
  jmethodID* methods_jmethod_ids_acquire() const 
         { return (jmethodID*)OrderAccess::load_ptr_acquire(&_methods_jmethod_ids); }
  void release_set_methods_jmethod_ids(jmethodID* jmeths)
         { OrderAccess::release_store_ptr(&_methods_jmethod_ids, jmeths); }

  int* methods_cached_itable_indices_acquire() const 
         { return (int*)OrderAccess::load_ptr_acquire(&_methods_cached_itable_indices); }
  void release_set_methods_cached_itable_indices(int* indices)
         { OrderAccess::release_store_ptr(&_methods_cached_itable_indices, indices); }

  inline typeArrayOop get_method_annotations_from(int idnum, objArrayRef* annos_refp);
  void set_annotations(objArrayRef md, objArrayRef* md_p)  { ref_store_without_check(md_p, md); }
  void set_methods_annotations_of(int idnum, typeArrayRef anno, objArrayRef* md_p);

  // Offsets for memory management
  heapRef* adr_array_klasses() const     { return (heapRef*)&this->_array_klasses;}
  heapRef* adr_methods() const           { return (heapRef*)&this->_methods;}
  heapRef* adr_method_ordering() const   { return (heapRef*)&this->_method_ordering;}
  heapRef* adr_local_interfaces() const  { return (heapRef*)&this->_local_interfaces;}
  heapRef* adr_transitive_interfaces() const  { return (heapRef*)&this->_transitive_interfaces;}
  heapRef* adr_fields() const            { return (heapRef*)&this->_fields;}
  heapRef* adr_constants() const         { return (heapRef*)&this->_constants;}
  heapRef* adr_class_loader() const      { return (heapRef*)&this->_class_loader;}
  heapRef* adr_protection_domain() const { return (heapRef*)&this->_protection_domain;}
  heapRef* adr_signers() const           { return (heapRef*)&this->_signers;}
  heapRef* adr_source_file_name() const  { return (heapRef*)&this->_source_file_name;}
  heapRef* adr_source_debug_extension() const { return (heapRef*)&this->_source_debug_extension;}
  heapRef* adr_inner_classes() const     { return (heapRef*)&this->_inner_classes;}
heapRef*adr_implementor()const{return(heapRef*)&this->_implementor;}
  heapRef* adr_generic_signature() const { return (heapRef*)&this->_generic_signature;}
  heapRef* adr_methods_jmethod_ids() const             { return (heapRef*)&this->_methods_jmethod_ids;}
  heapRef* adr_methods_cached_itable_indices() const   { return (heapRef*)&this->_methods_cached_itable_indices;}
  heapRef* adr_class_annotations() const   { return (heapRef*)&this->_class_annotations;}
  heapRef* adr_fields_annotations() const  { return (heapRef*)&this->_fields_annotations;}
  heapRef* adr_methods_annotations() const { return (heapRef*)&this->_methods_annotations;}
  heapRef* adr_methods_parameter_annotations() const { return (heapRef*)&this->_methods_parameter_annotations;}
  heapRef* adr_methods_default_annotations() const { return (heapRef*)&this->_methods_default_annotations;}

  // Static methods that are used to implement member methods where an exposed this pointer
  // is needed due to possible GCs
  static bool link_class_impl                           (instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS);
  static bool verify_code                               (instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS);
  static void initialize_impl                           (instanceKlassHandle this_oop, TRAPS);
  static void eager_initialize_impl                     (instanceKlassHandle this_oop);
  static void set_initialization_state_and_notify_impl  (instanceKlassHandle this_oop, ClassState state, TRAPS);
  static void call_class_initializer_impl               (instanceKlassHandle this_oop, TRAPS);
  static klassRef array_klass_impl                      (instanceKlassHandle this_oop, bool or_null, int n, TRAPS);
  static void do_local_static_fields_impl               (instanceKlassHandle this_oop, void f(fieldDescriptor* fd, TRAPS), TRAPS);              
  /* jni_id_for_impl for jfieldID only */
  static JNIid* jni_id_for_impl                         (instanceKlassHandle this_oop, int offset);      

  // Returns the array class for the n'th dimension
klassRef array_klass_impl(klassRef thsi,bool or_null,int n,TRAPS);

  // Returns the array class with this class as element type
klassRef array_klass_impl(klassRef thsi,bool or_null,TRAPS);

public:
  // sharing support
  virtual void remove_unshareable_info();
  void field_names_and_sigs_iterate(OopClosure* closure);

  // jvm support
  jint compute_modifier_flags(TRAPS) const;

public:
  // JVMTI support
  jint jvmti_class_status() const;

#ifndef PRODUCT
 public:
  // Printing
  void oop_print_on      (oop obj, outputStream* st);
  void oop_print_value_on(oop obj, outputStream* st);
#endif

 public:
  // Verification
  const char* internal_name() const;
  void oop_verify_on(oop obj, outputStream* st);
  
#ifndef PRODUCT
  static void verify_class_klass_nonstatic_oop_maps(klassOop k) PRODUCT_RETURN;
#endif

  void oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref);
  void oop_print_xml_on_as_object(oop obj, xmlBuffer *xb);
};

inline methodOop instanceKlass::method_at_vtable(int index)  {
#ifndef PRODUCT
  assert(index >= 0, "valid vtable index");
#endif
  vtableEntry* ve = (vtableEntry*)start_of_vtable();
  return ve[index].method();
}

inline typeArrayOop instanceKlass::get_method_annotations_from(int idnum, objArrayRef *annos_refp) {
  objArrayOop annos = lvb_objArrayRef(annos_refp).as_objArrayOop();
  if (annos == NULL || annos->length() <= idnum) {
    return NULL;
  }
  return typeArrayOop(annos->obj_at(idnum));
}

// for adding methods
// UNSET_IDNUM return means no more ids available
inline u2 instanceKlass::next_method_idnum() { 
  if (_idnum_allocated_count == constMethodOopDesc::MAX_IDNUM) {
    return constMethodOopDesc::UNSET_IDNUM; // no more ids available
  } else {
    return _idnum_allocated_count++; 
  }
}


// ValueObjs embedded in klass. Describes where oops are located in instances of this klass.

class OopMapBlock VALUE_OBJ_CLASS_SPEC {
 private:
  jushort _offset;    // Offset of first oop in oop-map block
  jushort _length;    // Length of oop-map block
 public:
  // Accessors  
  jushort offset() const          { return _offset; }
  void set_offset(jushort offset) { _offset = offset; }

  jushort length() const          { return _length; }
  void set_length(jushort length) { _length = length; }
};

/* JNIid class for jfieldIDs only */
class JNIid: public CHeapObj { 
 private:
  klassRef           _holder;
  JNIid*             _next;
  int                _offset;
#ifdef ASSERT 
  bool               _is_static_field_id; 
#endif 

 public: 
  // Accessors 
klassOop holder()const{return lvb_klassRef(&_holder).as_klassOop();}
  int offset() const              { return _offset; } 
  JNIid* next()                   { return _next; } 
  // Constructor 
  JNIid(klassOop holder, int offset, JNIid* next); 
  // Identifier lookup 
  JNIid* find(int offset); 
 
  // Garbage collection support 
  objectRef* holder_addr() { return (objectRef*)&_holder; }
  void oops_do(OopClosure* f); 
  static void deallocate(JNIid* id); 
  // Debugging 
#ifdef ASSERT 
  bool is_static_field_id() const { return _is_static_field_id; } 
  void set_is_static_field_id()   { _is_static_field_id = true; } 
#endif 
  void verify(klassOop holder); 
}; 
 

// If breakpoints are more numerous than just JVMTI breakpoints,
// consider compressing this data structure.
// It is currently a simple linked list defined in methodOop.hpp.

class BreakpointInfo;


// A collection point for interesting information about the previous
// version(s) of an instanceKlass. This class uses weak references to
// the information so that the information may be collected as needed
// by the system. If the information is shared, then a regular
// reference must be used because a weak reference would be seen as
// collectible. A GrowableArray of PreviousVersionNodes is attached
// to the instanceKlass as needed. See PreviousVersionWalker below.
class PreviousVersionNode : public CHeapObj {
 private:
  // A shared ConstantPool is never collected so we'll always have
  // a reference to it so we can update items in the cache. We'll
  // have a weak reference to a non-shared ConstantPool until all
  // of the methods (EMCP or obsolete) have been collected; the
  // non-shared ConstantPool becomes collectible at that point.
  jobject _prev_constant_pool;  // regular or weak reference
  bool    _prev_cp_is_weak;     // true if not a shared ConstantPool

  // If the previous version of the instanceKlass doesn't have any
  // EMCP methods, then _prev_EMCP_methods will be NULL. If all the
  // EMCP methods have been collected, then _prev_EMCP_methods can
  // have a length of zero.
  GrowableArray<jweak>* _prev_EMCP_methods;

public:
  PreviousVersionNode(jobject prev_constant_pool, bool prev_cp_is_weak,
    GrowableArray<jweak>* prev_EMCP_methods);
  ~PreviousVersionNode();
  jobject prev_constant_pool() const {
    return _prev_constant_pool;
  }
  GrowableArray<jweak>* prev_EMCP_methods() const {
    return _prev_EMCP_methods;
  }
};


// A Handle-ized version of PreviousVersionNode.
class PreviousVersionInfo : public ResourceObj {
 private:
  constantPoolHandle   _prev_constant_pool_handle;
  // If the previous version of the instanceKlass doesn't have any
  // EMCP methods, then _prev_EMCP_methods will be NULL. Since the
  // methods cannot be collected while we hold a handle,
  // _prev_EMCP_methods should never have a length of zero.
  GrowableArray<methodHandle>* _prev_EMCP_method_handles;

public:
  PreviousVersionInfo(PreviousVersionNode *pv_node);
  ~PreviousVersionInfo();
  constantPoolHandle prev_constant_pool_handle() const {
    return _prev_constant_pool_handle;
  }
  GrowableArray<methodHandle>* prev_EMCP_method_handles() const {
    return _prev_EMCP_method_handles;
  }
};


// Helper object for walking previous versions. This helper cleans up
// the Handles that it allocates when the helper object is destroyed.
// The PreviousVersionInfo object returned by next_previous_version()
// is only valid until a subsequent call to next_previous_version() or
// the helper object is destroyed.
class PreviousVersionWalker : public StackObj {
 private:
  GrowableArray<PreviousVersionNode *>* _previous_versions;
  int                                   _current_index;
  // Fields for cleaning up when we are done walking the previous versions:
  // A HandleMark for the PreviousVersionInfo handles:
  HandleMark                            _hm;

  // It would be nice to have a ResourceMark field in this helper also,
  // but the ResourceMark code says to be careful to delete handles held
  // in GrowableArrays _before_ deleting the GrowableArray. Since we
  // can't guarantee the order in which the fields are destroyed, we
  // have to let the creator of the PreviousVersionWalker object do
  // the right thing. Also, adding a ResourceMark here causes an
  // include loop.

  // A pointer to the current info object so we can handle the deletes.
  PreviousVersionInfo *                 _current_p;

 public:
  PreviousVersionWalker(instanceKlass *ik);
  ~PreviousVersionWalker();

  // Return the interesting information for the next previous version
  // of the klass. Returns NULL if there are no more previous versions.
  PreviousVersionInfo* next_previous_version();
};

#endif // INSTANCEKLASS_HPP
