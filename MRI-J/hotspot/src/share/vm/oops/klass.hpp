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
#ifndef KLASS_HPP
#define KLASS_HPP


#include "accessFlags.hpp"
#include "atomic.hpp"
#include "exceptions.hpp"
#include "genOopClosures.hpp"
#include "heapRef_pd.hpp"
#include "klassOop.hpp"
#include "klassPS.hpp"
#include "lvbClosures.hpp"
#include "memRegion.hpp"
#include "oopTable.hpp"
#include "refsHierarchy_pd.hpp"
#include "specialized_oop_closures.hpp"

class BoolObjectClosure;
class GPGC_GCManagerNewFinal;
class GPGC_GCManagerNewStrong;
class GPGC_GCManagerOldFinal;
class GPGC_GCManagerOldStrong;
class OopClosure;
class PSPromotionManager;
class ParCompactionManager;


// A Klass is the part of the klassOop that provides:
//  1: language level class object (method dictionary etc.)
//  2: provide vm dispatch behavior for the object
// Both functions are combined into one C++ class. The toplevel class "Klass"
// implements purpose 1 whereas all subclasses provide extra virtual functions 
// for purpose 2.

// One reason for the oop/klass dichotomy in the implementation is
// that we don't want a C++ vtbl pointer in every object.  Thus,
// normal oops don't have any virtual functions.  Instead, they
// forward all "virtual" functions to their klass, which does have
// a vtbl and does the C++ dispatch depending on the object's
// actual type.  (See oop.inline.hpp for some of the forwarding code.)
// ALL FUNCTIONS IMPLEMENTING THIS DISPATCH ARE PREFIXED WITH "oop_"!

//  Klass layout:
//    [header        ] klassOop
//    [C++ vtbl ptr  ] (contained in Klass_vtbl)
//    [layout_helper ]
//    [super_check_offset   ] for fast subtype checks
//    [secondary_super_kid_cache] for fast subtype checks
//    [secondary_supers     ] array of 2ndary supertypes
//    [primary_supers 0]
//    [primary_supers 1]
//    [primary_supers 2]
//    ...
//    [primary_supers 7]
//    [java_mirror   ]
//    [super         ]
//    [name          ]
//    [first subklass]
//    [next_sibling  ] link to chain additional subklasses
//    [modifier_flags]
//    [access_flags  ]
//    [verify_count  ] - not in product
//    [alloc_count   ]
//    [last_biased_lock_bulk_revocation_time] (64 bits)
//    [prototype_header]
//    [biased_lock_revocation_count]


// Forward declarations.
class klassVtable;
class KlassHandle;
class OrderAccess;

// Holder (or cage) for the C++ vtable of each kind of Klass.
// We want to tightly constrain the location of the C++ vtable in the overall layout.
class Klass_vtbl {
 protected:
  // The following virtual exists only to force creation of a C++ vtable,
  // so that this class truly is the location of the vtable of all Klasses.
  // Must not be inlined.
  virtual void unused_initial_virtual();

 public:
  // The following virtual makes Klass_vtbl play a second role as a
  // factory protocol for subclasses of Klass ("sub-Klasses").
  // Here's how it works....
  //
  // This VM uses metaobjects as factories for their instances.
  //
  // In order to initialize the C++ vtable of a new instance, its
  // metaobject is forced to use the C++ placed new operator to
  // allocate the instance.  In a typical C++-based system, each
  // sub-class would have its own factory routine which
  // directly uses the placed new operator on the desired class,
  // and then calls the appropriate chain of C++ constructors.
  //
  // However, this system uses shared code to performs the first
  // allocation and initialization steps for all sub-Klasses.
  // (See base_create_klass() and base_create_array_klass().)
  // This does not factor neatly into a hierarchy of C++ constructors.
  // Each caller of these shared "base_create" routines knows
  // exactly which sub-Klass it is creating, but the shared routine
  // does not, even though it must perform the actual allocation.
  //
  // Therefore, the caller of the shared "base_create" must wrap
  // the specific placed new call in a virtual function which
  // performs the actual allocation and vtable set-up.  That
  // virtual function is here, Klass_vtbl::allocate_permanent.
  //
  // The arguments to Universe::allocate_permanent() are passed
  // straight through the placed new operator, which in turn
  // obtains them directly from this virtual call.
  //
  // This virtual is called on a temporary "example instance" of the
  // sub-Klass being instantiated, a C++ auto variable.  The "real"
  // instance created by this virtual is on the VM heap, where it is
  // equipped with a klassOopDesc header.
  //
  // It is merely an accident of implementation that we use "example
  // instances", but that is why the virtual function which implements
  // each sub-Klass factory happens to be defined by the same sub-Klass
  // for which it creates instances.
  //
  // The vtbl_value() call (see below) is used to strip away the
  // accidental Klass-ness from an "example instance" and present it as
  // a factory.  Think of each factory object as a mere container of the
  // C++ vtable for the desired sub-Klass.  Since C++ does not allow
  // direct references to vtables, the factory must also be delegated
  // the task of allocating the instance, but the essential point is
  // that the factory knows how to initialize the C++ vtable with the
  // right pointer value.  All other common initializations are handled
  // by the shared "base_create" subroutines.
  //
  virtual void* allocate_permanent(KlassHandle& klass, int size, TRAPS) const = 0;
  void post_new_init_klass(KlassHandle& klass, klassOop obj, int size) const;

  Klass* klass_allocate(KlassHandle& klass, int size, TRAPS) const;

  // Every subclass on which vtbl_value is called must include this macro.
  // Delay the installation of the klassKlass pointer until after the
  // the vtable for a new klass has been installed (after the call to new()).
#define DEFINE_ALLOCATE_PERMANENT(thisKlass) \
virtual void*allocate_permanent(KlassHandle&klass_klass,int size,TRAPS)const{\
    Klass* new_klass = klass_allocate(klass_klass, size, CHECK_NULL);       \
    Klass* result = new (new_klass) thisKlass();                \
    klassOop new_klass_oop = result->as_klassOop();                 \
    OrderAccess::storestore();	\
post_new_init_klass(klass_klass,new_klass_oop,size);\
    return (void*)result;	\
  }

  bool null_vtbl() { return *(intptr_t*)this == 0; }

};

class ContendedStackEntry:public CHeapObj{
  friend class Klass;

 private:
  ContendedStackEntry* _next;      // chain of stacks entries
  int64_t              _count;     // how many times this stack contributed to contention
  int64_t              _ticks;     // how many ticks were spent blocked at this stack trace
  int64_t              _max_ticks; // longest time a thread was blocked at this stack trace
int _hash;//hash of the stack
  int*                 _stack;     // stack of methodIds
  
 public:
  void add_blocked_ticks(int64_t val);
  void print_xml_on(xmlBuffer *xb);
};


class Klass : public Klass_vtbl {
  friend class VMStructs;
  virtual void unused_initial_virtual();
protected:
  // note: put frequently-used fields together at start of klass structure
  // for better cache behavior (may not make much of a difference but sure won't hurt)
  enum { _primary_super_limit = 8 };
  
  // The "layout helper" is a combined descriptor of object layout.
  // For klasses which are neither instance nor array, the value is zero.
  //
  // For instances, layout helper is a positive number, the instance size.
  // This size is already passed through align_object_size and scaled to bytes.
  // The low order bit is set if instances of this class cannot be
  // allocated using the fastpath.
  //
  // For arrays, layout helper is a negative number, containing four
  // distinct bytes, as follows:
  //    MSB:[tag, hsz, ebt, log2(esz)]:LSB
  // where:
  //    tag is 0x80 if the elements are oops, 0xC0 if non-oops
  //    hsz is array header size in bytes (i.e., offset of first element)
  //    ebt is the BasicType of the elements
  //    esz is the element size in bytes
  // This packed word is arranged so as to be quickly unpacked by the
  // various fast paths that use the various subfields.
  //
  // The esz bits can be used directly by a SLL instruction, without masking.
  //
  // Note that the array-kind tag looks like 0x00 for instance klasses,
  // since their length in bytes is always less than 24Mb.
  //
  // Final note:  This comes first, immediately after Klass_vtbl,
  // because it is frequently queried.
  jint        _layout_helper;

  // The fields _super_check_offset, _secondary_super_kid_cache, _secondary_supers
  // and _primary_supers all help make fast subtype checks.  See big discussion
  // in doc/server_compiler/checktype.txt
  //
  // Where to look to observe a supertype (it is &_secondary_super_kid_cache for
  // secondary supers, else is &_primary_supers[depth()].
juint _secondary_super_kid_cache;
  juint       _super_check_offset;
  // Cache of last observed secondary supertype
juint _cache_update_throttle;
  // Ordered list of all primary supertypes
  juint       _primary_supers_kids[_primary_super_limit];

 public:
heapRef*oop_block_beg()const{return adr_secondary_supers();}
  heapRef* oop_block_end() const { return adr_next_sibling() + 1; }
 protected:
  //
  // The oop block.  All oop fields must be declared here and only oop fields
  // may be declared here.  In addition, the first and last fields in this block
  // must remain first and last, unless oop_block_beg() and/or oop_block_end()
  // are updated.  Grouping the oop fields in a single block simplifies oop
  // iteration.
  // 

  // Array of all secondary supertypes
  objArrayRef _secondary_supers;
  // java/lang/Class instance mirroring this class
  objectRef   _java_mirror;
  // Superclass
  klassRef  _super;
  // Class name.  Instance classes: java/lang/String, etc.  Array classes: [I,
  // [Ljava/lang/String;, etc.  Set to zero for all other kinds of classes.
  symbolRef _name;
  // First subclass (NULL if none); _subklass->next_sibling() is next one
  klassRef _subklass;
  // Sibling link (or NULL); links all subklasses of a klass
  klassRef _next_sibling;

  //
  // End of the oop block.
  // 

  jint        _modifier_flags;  // Processed access flags, for use by Class.getModifiers.
  AccessFlags _access_flags;    // Access flags. The class/interface distinction is stored here.

#ifndef PRODUCT
  int           _verify_count;  // to avoid redundant verifies
#endif

  juint    _alloc_count;        // allocation profiling support - update klass_size_in_bytes() if moved/deleted
  intptr_t _new_gen_live_count; // shared between PGC and new-gen GPGC
  intptr_t _new_gen_live_size; // shared between PGC and new-gen GPGC
  intptr_t _new_gen_live_count_reported;  // shared between PGC and new-gen GPGC
  intptr_t _new_gen_live_size_reported; // shared between PGC and new-gen GPGC
  intptr_t _old_gen_live_count;
  intptr_t _old_gen_live_size;
  intptr_t _old_gen_live_count_reported;
  intptr_t _old_gen_live_size_reported;

  unsigned int _klassId;        // Support for kid bits in objrefs (Azul)

  uint64_t _klass_sma_successes;      // SMA statistics for this class
  uint64_t _klass_sma_failures;               
  
 private:
  ContendedStackEntry *_contentions;
  
 public:
  ContendedStackEntry* add_contention_stack(int hash, int stack[]); // stack of methodIds and bcis

 public:

  int64_t _wait_count;               // Count of calls to wait()
  int64_t _wait_max_ticks;           // Longest wait() time
  int64_t _wait_total_ticks;         // Cumulative wait() times
  static int gather_lock_contention(Klass *K, struct lock_contention_data *data, int cnt);

  // returns the enclosing klassOop
  klassOop as_klassOop() const {
    // see klassOop.hpp for layout.
    return (klassOop) (((char*) this) - sizeof(klassOopDesc));
  }

 public:
  // Allocation
  const Klass_vtbl& vtbl_value() const { return *this; }  // used only on "example instances"
static KlassHandle base_create_klass(KlassHandle&klass,int size,const Klass_vtbl&vtbl,juint klassId,TRAPS);
static klassOop base_create_klass_oop(KlassHandle&klass,int size,const Klass_vtbl&vtbl,juint klassId,TRAPS);

  // super
klassOop super()const{return lvb_klassRef(&_super).as_klassOop();}
klassRef super_ref()const{return lvb_klassRef(&_super);}
  void set_super(klassOop k)           { ref_store_without_check(&_super, klassRef(k)); }

  // initializes _super link, _primary_supers & _secondary_supers arrays
  void initialize_supers(klassOop k, TRAPS);
  void initialize_supers_impl1(klassOop k);
  void initialize_supers_impl2(klassOop k);

  // klass-specific helper for initializing _secondary_supers
  virtual objArrayOop compute_secondary_supers(int num_extra_slots, TRAPS);

  // java_super is the Java-level super type as specified by Class.getSuperClass.
  virtual klassOop java_super() const  { return NULL; }

  juint    super_check_offset() const  { return _super_check_offset; }
  void set_super_check_offset(juint o) { _super_check_offset = o; }

  juint secondary_super_kid_cache() const   { return _secondary_super_kid_cache; }
  void set_secondary_super_cache(juint kid) { _secondary_super_kid_cache =  kid; }

  objArrayOop secondary_supers() const { return lvb_objArrayRef(&_secondary_supers).as_objArrayOop(); }
  void set_secondary_supers(objArrayOop k) { ref_store_without_check(&_secondary_supers, objArrayRef(k)); }

  // Return the KlassID of the _super chain of the given depth.
juint primary_super_of_depth(juint i)const{
    assert(i < primary_super_limit(), "oob");
return _primary_supers_kids[i];
  }

  // Can this klass be a primary super?  False for interfaces and arrays of
  // interfaces.  False also for arrays or classes with long super chains.
  bool can_be_primary_super() const {
const juint secondary_offset=secondary_super_kid_cache_offset_in_bytes()+sizeof(oopDesc);
    return super_check_offset() != secondary_offset;
  }
  virtual bool can_be_primary_super_slow() const;

  // Returns number of primary supers; may be a number in the inclusive range [0, primary_super_limit].
  juint super_depth() const {
    if (!can_be_primary_super()) {
      return primary_super_limit();
    } else {
juint d=(super_check_offset()-(primary_supers_kids_offset_in_bytes()+sizeof(oopDesc)))/sizeof(juint);
      assert(d < primary_super_limit(), "oob");
      assert(KlassTable::getKlassByKlassId(_primary_supers_kids[d]).as_klassOop() == as_klassOop(), "proper init");
      return d;
    }
  }

  // java mirror
oop java_mirror()const{return lvb_ref(&_java_mirror).as_oop();}
objectRef java_mirror_ref()const{return lvb_ref(&_java_mirror);}
  void set_java_mirror(oop m)          { ref_store_without_check(&_java_mirror, objectRef(m)); }

  // modifier flags
  jint modifier_flags() const          { return _modifier_flags; }
  void set_modifier_flags(jint flags)  { _modifier_flags = flags; }

  // size helper
  int layout_helper() const            { return _layout_helper; }
  void set_layout_helper(int lh)       { _layout_helper = lh; }

  // Note: for instances layout_helper() may include padding.
  // Use instanceKlass::contains_field_offset to classify field offsets.

  // sub/superklass links
  instanceKlass* superklass() const;
  Klass* subklass() const;
  Klass* next_sibling() const;
  static bool is_valid_klassref(klassRef klass_ref);
  void append_to_sibling_list();           // add newly created receiver to superklass' subklass list
 protected:                                
  klassRef subklass_ref() const;
  klassOop subklass_oop() const { return subklass_ref().as_klassOop(); }
  klassRef next_sibling_ref() const;
  klassOop next_sibling_oop() const { return next_sibling_ref().as_klassOop(); }
  void     set_subklass(klassOop s); 
  void     set_next_sibling(klassOop s);
  void     init_subklass(); 
  void     init_next_sibling();

  heapRef* adr_super()               const { return (heapRef*)&_super;             }
heapRef*adr_secondary_super_kid_cache()const{return(heapRef*)&_secondary_super_kid_cache;}
  heapRef* adr_secondary_supers()    const { return (heapRef*)&_secondary_supers;  }
  heapRef* adr_java_mirror()         const { return (heapRef*)&_java_mirror;       }
  heapRef* adr_name()                const { return (heapRef*)&_name;              }

  heapRef* adr_subklass()            const;
  heapRef* adr_next_sibling()        const;

 public:
  // Allocation profiling support
  juint alloc_count() const          { return _alloc_count; }
  void set_alloc_count(juint n)      { _alloc_count = n; }
  virtual juint alloc_size() const = 0;
  intptr_t new_gen_live_count() const	     {return _new_gen_live_count;}
  void set_new_gen_live_count(intptr_t n)	     {_new_gen_live_count = n;}
  intptr_t new_gen_live_count_reported() const {return _new_gen_live_count_reported;}
  void set_new_gen_live_count_reported(intptr_t n)	{Atomic::store_ptr(n,&_new_gen_live_count_reported);}

  intptr_t new_gen_live_size() {return _new_gen_live_size; }
  void set_new_gen_live_size(intptr_t n) 		{_new_gen_live_size = n;}
  intptr_t new_gen_live_size_reported() const {return _new_gen_live_size_reported;}
  void set_new_gen_live_size_reported(intptr_t n) 	{Atomic::store_ptr(n, &_new_gen_live_size_reported);}
  intptr_t atomic_add_to_new_gen_live_size(int obj_size){return Atomic::add_ptr(obj_size,&_new_gen_live_size);}
  intptr_t atomic_add_to_new_gen_live_count(int obj_count){return Atomic::add_ptr(obj_count,&_new_gen_live_count);}
  intptr_t atomic_increment_new_gen_live_count(){Atomic::inc_ptr(&_new_gen_live_count); return new_gen_live_count();}
  virtual void set_alloc_size(juint n) = 0;

  intptr_t old_gen_live_count() const	     {return _old_gen_live_count;}
  void set_old_gen_live_count(intptr_t n)	     {_old_gen_live_count = n;}
  intptr_t old_gen_live_count_reported() const {return _old_gen_live_count_reported;}
  void set_old_gen_live_count_reported(intptr_t n)	{Atomic::store_ptr(n,&_old_gen_live_count_reported);}
  intptr_t old_gen_live_size_reported() const {return _old_gen_live_size_reported;}
  void set_old_gen_live_size_reported(intptr_t n) 	{Atomic::store_ptr(n, &_old_gen_live_size_reported);}

  intptr_t old_gen_live_size() {return _old_gen_live_size; }
  void set_old_gen_live_size(intptr_t n) 		{_old_gen_live_size = n;}
  intptr_t atomic_add_to_old_gen_live_size(int obj_size){return Atomic::add_ptr(obj_size,&_old_gen_live_size);}
  intptr_t atomic_add_to_old_gen_live_count(int obj_count){return Atomic::add_ptr(obj_count,&_old_gen_live_count);}
  intptr_t atomic_increment_old_gen_live_count(){Atomic::inc_ptr(&_old_gen_live_count); return old_gen_live_count();}


  // Compiler support
  static int super_offset_in_bytes()         { return offset_of(Klass, _super); }
  static int super_check_offset_offset_in_bytes() { return offset_of(Klass, _super_check_offset); }
static int primary_supers_kids_offset_in_bytes(){return offset_of(Klass,_primary_supers_kids);}
static int secondary_super_kid_cache_offset_in_bytes(){return offset_of(Klass,_secondary_super_kid_cache);}
  static int secondary_supers_offset_in_bytes() { return offset_of(Klass, _secondary_supers); }
static int cache_update_throttle_offset_in_bytes(){return offset_of(Klass,_cache_update_throttle);}
  static int java_mirror_offset_in_bytes()   { return offset_of(Klass, _java_mirror); }
  static int modifier_flags_offset_in_bytes(){ return offset_of(Klass, _modifier_flags); }
  static int layout_helper_offset_in_bytes() { return offset_of(Klass, _layout_helper); }
  static int access_flags_offset_in_bytes()  { return offset_of(Klass, _access_flags); }

  // Unpacking layout_helper:
  enum {
    _lh_neutral_value           = 0,  // neutral non-array non-instance value
    _lh_instance_slow_path_bit  = 0x01,
    _lh_log2_element_size_shift = BitsPerByte*0,
    _lh_log2_element_size_mask  = BitsPerLong-1,
    _lh_element_type_shift      = BitsPerByte*1,
    _lh_element_type_mask       = right_n_bits(BitsPerByte),  // shifted mask
    _lh_header_size_shift       = BitsPerByte*2,
    _lh_header_size_mask        = right_n_bits(BitsPerByte),  // shifted mask
    _lh_array_tag_bits          = 2,
    _lh_array_tag_shift         = BitsPerInt - _lh_array_tag_bits,
    _lh_array_tag_type_value    = ~0x00,  // 0xC0000000 >> 30
    _lh_array_tag_obj_value     = ~0x01   // 0x80000000 >> 30
  };

  static int layout_helper_size_in_bytes(jint lh) {
    assert(lh > (jint)_lh_neutral_value, "must be instance");
    return (int) lh & ~_lh_instance_slow_path_bit;
  }
  static bool layout_helper_needs_slow_path(jint lh) {
    assert(lh > (jint)_lh_neutral_value, "must be instance");
    return (lh & _lh_instance_slow_path_bit) != 0;
  }
  static bool layout_helper_is_instance(jint lh) {
    return (jint)lh > (jint)_lh_neutral_value;
  }
  static bool layout_helper_is_javaArray(jint lh) {
    return (jint)lh < (jint)_lh_neutral_value;
  }
  static bool layout_helper_is_typeArray(jint lh) {
    // _lh_array_tag_type_value == (lh >> _lh_array_tag_shift);
    return (juint)lh >= (juint)(_lh_array_tag_type_value << _lh_array_tag_shift);
  }
  static bool layout_helper_is_objArray(jint lh) {
    // _lh_array_tag_obj_value == (lh >> _lh_array_tag_shift);
    return (jint)lh < (jint)(_lh_array_tag_type_value << _lh_array_tag_shift);
  }
  static int layout_helper_header_size(jint lh) {
    assert(lh < (jint)_lh_neutral_value, "must be array");
    int hsize = (lh >> _lh_header_size_shift) & _lh_header_size_mask;
    assert(hsize > 0 && hsize < (int)sizeof(oopDesc)*3, "sanity");
    return hsize;
  }
  static BasicType layout_helper_element_type(jint lh) {
    assert(lh < (jint)_lh_neutral_value, "must be array");
    int btvalue = (lh >> _lh_element_type_shift) & _lh_element_type_mask;
    assert(btvalue >= T_BOOLEAN && btvalue <= T_OBJECT, "sanity");
    return (BasicType) btvalue;
  }
  static int layout_helper_log2_element_size(jint lh) {
    assert(lh < (jint)_lh_neutral_value, "must be array");
    int l2esz = (lh >> _lh_log2_element_size_shift) & _lh_log2_element_size_mask;
    assert(l2esz <= LogBitsPerLong, "sanity");
    return l2esz;
  }
  static jint array_layout_helper(jint tag, int hsize, BasicType etype, int log2_esize) {
    return (tag        << _lh_array_tag_shift)
      |    (hsize      << _lh_header_size_shift)
      |    ((int)etype << _lh_element_type_shift)
      |    (log2_esize << _lh_log2_element_size_shift);
  }
  static jint instance_layout_helper(jint size, bool slow_path_flag) {
    return (size << LogHeapWordSize)
      |    (slow_path_flag ? _lh_instance_slow_path_bit : 0);
  }
  static int layout_helper_to_size_helper(jint lh) {
    assert(lh > (jint)_lh_neutral_value, "must be instance");
    // Note that the following expression discards _lh_instance_slow_path_bit.
    return lh >> LogHeapWordSize;
  }
  // Out-of-line version computes everything based on the etype:
  static jint array_layout_helper(BasicType etype);
static int klassId_offset_in_bytes(){return offset_of(Klass,_klassId);}
  
static int wait_count_offset_in_bytes(){return offset_of(Klass,_wait_count);}
static int wait_max_ticks_offset_in_bytes(){return offset_of(Klass,_wait_max_ticks);}
static int wait_total_ticks_offset_in_bytes(){return offset_of(Klass,_wait_total_ticks);}

  // What is the maximum number of primary superclasses any klass can have?
#ifdef PRODUCT
  static juint primary_super_limit()         { return _primary_super_limit; }
#else
  static juint primary_super_limit() {
    assert(FastSuperclassLimit <= _primary_super_limit, "parameter oob");
    return FastSuperclassLimit;
  }
#endif

  // vtables
  virtual klassVtable* vtable() const        { return NULL; }

  static int klass_size_in_bytes()           { return offset_of(Klass, _alloc_count) + sizeof(juint); }  // all "visible" fields

  // subclass check
  bool is_subclass_of(klassOop k) const;
  // subtype check: true if is_subclass_of, or if k is interface and receiver implements it
  bool is_subtype_of(klassOop k) const { 
    juint    off = k->klass_part()->super_check_offset();
juint sup_kid=*(juint*)((address)as_klassOop()+off);
const juint secondary_offset=secondary_super_kid_cache_offset_in_bytes()+sizeof(oopDesc);
    if (sup_kid == k->klass_part()->klassId()) {
      return true;
    } else if (off != secondary_offset) {
      return false;
    } else {
      return search_secondary_supers(k);
    }
  }
  bool search_secondary_supers(klassOop k) const;

  // Find LCA in class heirarchy
  Klass *LCA( Klass *k );

  // Find any finalizable subclass of this class, or NULL if none.  Used by C2
  // to allow compiled allocation of newInstance idioms where the exact class
  // isn't known until runtime.
  const Klass* find_finalizable_subclass() const;

  // Check whether reflection/jni/jvm code is allowed to instantiate this class;
  // if not, throw either an Error or an Exception.
  virtual void check_valid_for_instantiation(bool throwError, TRAPS);

  // Casting
  static Klass* cast(klassOop k) {
    assert(k->is_klass(), "cast to Klass");
    return k->klass_part(); 
  }

  // array copying
  virtual void  copy_array(arrayOop s, int src_pos, arrayOop d, int dst_pos, int length, TRAPS);

  // tells if the class should be initialized
  virtual bool should_be_initialized() const    { return false; }
  // initializes the klass
  virtual void initialize(TRAPS);
  virtual methodOop uncached_lookup_method(symbolOop name, symbolOop signature) const;
 public:  
  methodOop lookup_method(symbolOop name, symbolOop signature) const {
    return uncached_lookup_method(name, signature);
  }

  // array class with specific rank
  static klassRef array_klass(klassRef thsi, int rank, TRAPS) { return thsi.as_klassOop()->klass_part()->array_klass_impl(thsi,false, rank, THREAD); }

  // array class with this klass as element type
  static klassRef array_klass(klassRef thsi, TRAPS)  { return thsi.as_klassOop()->klass_part()->array_klass_impl(thsi, false, THREAD); }

  // These will return NULL instead of allocating on the heap:
  // NB: these can block for a mutex, like other functions with TRAPS arg.
  klassOop array_klass_or_null(int rank);
  klassOop array_klass_or_null();

  virtual oop protection_domain()       { return NULL; }
  virtual oop class_loader()  const     { return NULL; }

 protected:
virtual klassRef array_klass_impl(klassRef thsi,bool or_null,int rank,TRAPS);
virtual klassRef array_klass_impl(klassRef thsi,bool or_null,TRAPS);

 public:
  virtual void remove_unshareable_info();

 protected:
  // computes the subtype relationship
  virtual bool compute_is_subtype_of(klassOop k);
 public:
  // subclass accessor (here for convenience; undefined for non-klass objects)
  virtual bool is_leaf_class() const { fatal("not a class"); return false; }
 public:
  // ALL FUNCTIONS BELOW THIS POINT ARE DISPATCHED FROM AN OOP
  // These functions describe behavior for the oop not the KLASS.

  // actual oop size of obj in memory
  virtual int oop_size(oop obj) const = 0;
virtual int GC_oop_size(oop obj)const=0;

  // actual oop size of this klass in memory
  virtual int klass_oop_size() const = 0;

  // Returns the Java name for a class (Resource allocated)
  // For arrays, this returns the name of the element with a leading '['.
  // For classes, this returns the name with the package separators 
  //     turned into '.'s.
  const char* external_name() const;

  // Returns the Java name for a class (Resource allocated) as the class would
  // appear in Java source code.
  // For arrays, this returns the name of the element with a trailing '[]'.
  // For classes, this returns the name with the package separators
  //     turned into '.'s.
  const char* pretty_name() const;

  // Returns the name for a class (Resource allocated) as the class 
  // would appear in a signature.
  // For arrays, this returns the name of the element with a leading '['.
  // For classes, this returns the name with a leading 'L' and a trailing ';'
  //     and the package separators as '/'.
  virtual char* signature_name() const;

  // garbage collection support
  virtual void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm, oop obj) = 0;
  virtual void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm, oop obj) = 0;
  virtual void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm, oop obj) = 0;
  virtual void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm, oop obj) = 0;
virtual void GPGC_newgc_oop_update_cardmark(oop obj)=0;
virtual void GPGC_oldgc_oop_update_cardmark(oop obj)=0;
virtual void GPGC_mutator_oop_update_cardmark(oop obj)=0;
  virtual void GPGC_sweep_weak_methodCodes(GPGC_GCManagerOldStrong* gcm);

  virtual void oop_follow_contents(oop obj) = 0;
  virtual int  oop_adjust_pointers(oop obj) = 0;

  // Parallel Scavenge and Parallel Old
  PARALLEL_GC_DECLS_PV

 public:
  // type testing operations
  virtual bool oop_is_instance_slow()       const { return false; }
  virtual bool oop_is_instanceRef()         const { return false; }
  virtual bool oop_is_array()               const { return false; }
  virtual bool oop_is_code()                const { return false; }
  virtual bool oop_is_objArray_slow()       const { return false; }
  virtual bool oop_is_symbol()              const { return false; }
  virtual bool oop_is_klass()               const { return false; }
  virtual bool oop_is_thread()              const { return false; }
  virtual bool oop_is_method()              const { return false; }
  virtual bool oop_is_methodCode()          const { return false; }
  virtual bool oop_is_dependency()          const { return false; }
  virtual bool oop_is_constMethod()         const { return false; }
  virtual bool oop_is_constantPool()        const { return false; }
  virtual bool oop_is_constantPoolCache()   const { return false; }
  virtual bool oop_is_typeArray_slow()      const { return false; }
  virtual bool oop_is_arrayKlass()          const { return false; }
  virtual bool oop_is_objArrayKlass()       const { return false; }
  virtual bool oop_is_typeArrayKlass()      const { return false; }
  virtual bool oop_is_instanceKlass()       const { return false; }

  bool oop_is_javaArray_slow() const {
    return oop_is_objArray_slow() || oop_is_typeArray_slow();
  }

  // Fast non-virtual versions, used by oop.inline.hpp and elsewhere:
  #ifndef ASSERT
  #define assert_same_query(xval, xcheck) xval
  #else
 private:
  static bool assert_same_query(bool xval, bool xslow) {
    assert(xval == xslow, "slow and fast queries agree");
    return xval;
  }
 public:
  #endif
  inline  bool oop_is_instance()            const { return assert_same_query(
                                                    layout_helper_is_instance(layout_helper()),
                                                    oop_is_instance_slow()); }
  inline  bool oop_is_javaArray()           const { return assert_same_query(
                                                    layout_helper_is_javaArray(layout_helper()),
                                                    oop_is_javaArray_slow()); }
  inline  bool oop_is_objArray()            const { return assert_same_query(
                                                    layout_helper_is_objArray(layout_helper()),
                                                    oop_is_objArray_slow()); }
  inline  bool oop_is_typeArray()           const { return assert_same_query(
                                                    layout_helper_is_typeArray(layout_helper()),
                                                    oop_is_typeArray_slow()); }
  #undef assert_same_query
          
  // Unless overridden, oop is parsable if it has a klass pointer.
  virtual bool oop_is_parsable(oop obj) const { return true; }

  // Access flags
  AccessFlags access_flags() const         { return _access_flags;  }
  void set_access_flags(AccessFlags flags) { _access_flags = flags; }

  bool is_public() const                { return _access_flags.is_public(); }
  bool is_final() const                 { return _access_flags.is_final(); }
  bool is_interface() const             { return _access_flags.is_interface(); }
  bool is_abstract() const              { return _access_flags.is_abstract(); }
  bool is_super() const                 { return _access_flags.is_super(); }
  bool is_synthetic() const             { return _access_flags.is_synthetic(); }
  void set_is_synthetic()               { _access_flags.set_is_synthetic(); }
  bool has_finalizer() const            { return _access_flags.has_finalizer(); }
  bool has_final_method() const         { return _access_flags.has_final_method(); }
  void set_has_finalizer()              { _access_flags.set_has_finalizer(); }
  void set_has_final_method()           { _access_flags.set_has_final_method(); }
  bool is_cloneable() const             { return _access_flags.is_cloneable(); }
  void set_is_cloneable()               { _access_flags.set_is_cloneable(); }
  bool has_vanilla_constructor() const  { return _access_flags.has_vanilla_constructor(); }
  void set_has_vanilla_constructor()    { _access_flags.set_has_vanilla_constructor(); }
  bool has_miranda_methods () const     { return access_flags().has_miranda_methods(); }
  void set_has_miranda_methods()        { _access_flags.set_has_miranda_methods(); }

  // garbage collection support
  virtual void follow_weak_klass_links(
    BoolObjectClosure* is_alive, OopClosure* keep_alive);
  virtual void GPGC_follow_weak_klass_links();

  // Prefetch within oop iterators.  This is a macro because we
  // can't guarantee that the compiler will inline it.  In 64-bit
  // it generally doesn't.  Signature is
  //
  // static void prefetch_beyond(objectRef* const start,
  //                             objectRef* const end,
  //                             const intx foffset,
  //                             const Prefetch::style pstyle);
#define prefetch_beyond(start, end, foffset, pstyle) {   \
    const intx foffset_ = (foffset);                     \
    const Prefetch::style pstyle_ = (pstyle);            \
    assert(foffset_ > 0, "prefetch beyond, not behind"); \
    if (pstyle_ != Prefetch::do_none) {                  \
      objectRef* ref = (start);                          \
      if (ref < (end)) {                                 \
        switch (pstyle_) {                               \
        case Prefetch::do_read:                          \
Prefetch::read(UNPOISON_OBJECTREF(*ref,ref).as_oop(),foffset_);\
          break;                                         \
        case Prefetch::do_write:                         \
Prefetch::write(UNPOISON_OBJECTREF(*ref,ref).as_oop(),foffset_);\
          break;                                         \
        default:                                         \
          ShouldNotReachHere();                          \
          break;                                         \
        }                                                \
      }                                                  \
    }                                                    \
  }

  // iterators
  virtual int oop_oop_iterate(oop obj, OopClosure* blk) = 0;
  virtual int oop_oop_iterate_v(oop obj, OopClosure* blk) {
    return oop_oop_iterate(obj, blk);
  }

  // Iterates "blk" over all the oops in "obj" (of type "this") within "mr".
  // (I don't see why the _m should be required, but without it the Solaris
  // C++ gives warning messages about overridings of the "oop_oop_iterate"
  // defined above "hiding" this virtual function.  (DLD, 6/20/00)) */
  virtual int oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) = 0;
  virtual int oop_oop_iterate_v_m(oop obj, OopClosure* blk, MemRegion mr) {
    return oop_oop_iterate_m(obj, blk, mr);
  }

  // Versions of the above iterators specialized to particular subtypes
  // of OopClosure, to avoid closure virtual calls.
#define Klass_OOP_OOP_ITERATE_DECL(OopClosureType, nv_suffix)                \
  virtual int oop_oop_iterate##nv_suffix(oop obj, OopClosureType* blk) {     \
    /* Default implementation reverts to general version. */                 \
    return oop_oop_iterate(obj, blk);                                        \
  }                                                                          \
                                                                             \
  /* Iterates "blk" over all the oops in "obj" (of type "this") within "mr". \
     (I don't see why the _m should be required, but without it the Solaris  \
     C++ gives warning messages about overridings of the "oop_oop_iterate"   \
     defined above "hiding" this virtual function.  (DLD, 6/20/00)) */       \
  virtual int oop_oop_iterate##nv_suffix##_m(oop obj,                        \
                                             OopClosureType* blk,            \
                                             MemRegion mr) {                 \
    return oop_oop_iterate_m(obj, blk, mr);                                  \
  }

  SPECIALIZED_OOP_OOP_ITERATE_CLOSURES_1(Klass_OOP_OOP_ITERATE_DECL)
  SPECIALIZED_OOP_OOP_ITERATE_CLOSURES_3(Klass_OOP_OOP_ITERATE_DECL)

  virtual void array_klasses_do(void f(klassOop k)) {}
  virtual void with_array_klasses_do(void f(klassOop k));

  // klass name
symbolOop name()const{return lvb_symbolRef(&_name).as_symbolOop();}
void set_name(symbolOop n){ref_store_without_check(&_name,symbolRef(n));}

  friend class klassKlass;

 public:
  // jvm support
  virtual jint compute_modifier_flags(TRAPS) const;

 public:
  // JVMTI support
  virtual jint jvmti_class_status() const;

  // Azul kid bits support
  unsigned int klassId() const {
assert(_klassId!=0,"klassId was never set");
    return _klassId;
  }
  
  void set_klassId(unsigned int kid) {
    _klassId = kid;
  }

  void reset_sma_failures()  { _klass_sma_failures = 0; }
  void reset_sma_successes() { _klass_sma_successes = 0; }
  
  uint64_t klass_sma_failures()  { return _klass_sma_failures;  }
  uint64_t klass_sma_successes() { return _klass_sma_successes; }

  void increment_klass_sma_failures()  { _klass_sma_failures++;  }
  void increment_klass_sma_successes() { _klass_sma_successes++; }

  virtual void oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref);
  virtual void oop_print_xml_on_as_object(oop obj, xmlBuffer *xb);
  
  void print_lock_xml_on(xmlBuffer *xb); 

#ifndef PRODUCT
 public:
  // Printing
  virtual void oop_print_on      (oop obj, outputStream* st);
  virtual void oop_print_value_on(oop obj, outputStream* st);
#endif

 public:
  // Verification
  virtual const char* internal_name() const = 0;
  virtual void oop_verify_on(oop obj, outputStream* st);
  virtual void oop_verify_old_oop(oop obj, objectRef* p, bool allow_dirty);
  // tells whether obj is partially constructed (gc during class loading)
  virtual bool oop_partially_loaded(oop obj) const { return false; }
  virtual void oop_set_partially_loaded(oop obj) {};

#ifndef PRODUCT
  void verify_vtable_index(int index);
#endif
};

#endif // KLASS_HPP
