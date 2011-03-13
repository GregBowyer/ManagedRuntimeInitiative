/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CPCACHEOOP_HPP
#define CPCACHEOOP_HPP

#include "allocation.hpp"
#include "array.hpp"
#include "arrayOop.hpp"
#include "bytecodes.hpp"
#include "handles.hpp"

// A ConstantPoolCacheEntry describes an individual entry of the constant
// pool cache. There's 2 principal kinds of entries: field entries for in-
// stance & static field access, and method entries for invokes. Some of
// the entry layout is shared and looks as follows:
//
// bit number |31                0|
// bit length |-8--|-8--|---16----|
// --------------------------------
// _indices   [ b2 | b1 |  index  ]
// _f1        [  entry specific   ]
// _f2        [  entry specific   ]
// _flags     [t|f|vf|v|m|h|unused|field_index] (for field entries)
// bit length |4|1|1 |1|1|0|---7--|----16-----]
// _flags     [t|f|vf|v|m|h|unused|eidx|psze] (for method entries)
// bit length |4|1|1 |1|1|1|---7--|-8--|-8--]

// --------------------------------
//
// with:
// index  = original constant pool index
// b1     = bytecode 1
// b2     = bytecode 2
// psze   = parameters size (method entries only)
// eidx   = interpreter entry index (method entries only)
// field_index = index into field information in holder instanceKlass
//          The index max is 0xffff (max number of fields in constant pool)
//          and is multiplied by (instanceKlass::next_offset) when accessing.
// t      = TosState (see below)
// f      = field is marked final (see below)
// vf     = virtual, final (method entries only : is_vfinal())
// v      = field is volatile (see below)
// m      = invokeinterface used for method in class Object (see below)
// h      = RedefineClasses/Hotswap bit (see below)
//
// The flags after TosState have the following interpretation:
// bit 27: f flag  true if field is marked final
// bit 26: vf flag true if virtual final method
// bit 25: v flag true if field is volatile (only for fields)
// bit 24: m flag true if invokeinterface used for method in class Object
// bit 23: 0 for fields, 1 for methods
//
// The flags 31, 30, 29, 28 together build a 4 bit number 0 to 8 with the
// following mapping to the TosState states:
//
// btos: 0
// ctos: 1
// stos: 2
// itos: 3
// ltos: 4
// ftos: 5
// dtos: 6
// atos: 7
// vtos: 8
//
// Entry specific: field entries:
// _indices = get (b1 section) and put (b2 section) bytecodes, original constant pool index
// _f1      = field holder
// _f2      = field offset in words
// _flags   = field type information, original field index in field holder
//            (field_index section)
//
// Entry specific: method entries:
// _indices = invoke code for f1 (b1 section), invoke code for f2 (b2 section),
//            original constant pool index
// _f1      = method for all but virtual calls, unused by virtual calls
//            (note: for interface calls, which are essentially virtual,
//             contains klassOop for the corresponding interface.
// _f2      = method/vtable index for virtual calls only, unused by all other
//            calls.  The vf flag indicates this is a method pointer not an
//            index.
// _flags   = field type info (f section),
//            virtual final entry (vf),
//            interpreter entry index (eidx section),
//            parameter size (psze section)
//
// Note: invokevirtual & invokespecial bytecodes can share the same constant
//       pool entry and thus the same constant pool cache entry. All invoke
//       bytecodes but invokevirtual use only _f1 and the corresponding b1
//       bytecode, while invokevirtual uses only _f2 and the corresponding
//       b2 bytecode.  The value of _flags is shared for both types of entries.
//
// The fields are volatile so that they are stored in the order written in the
// source code.  The _indices field with the bytecode must be written last.

class ConstantPoolCacheEntry VALUE_OBJ_CLASS_SPEC {
 private:
  volatile intx     _indices;  // constant pool index & rewrite bytecodes
  volatile heapRef  _f1;       // entry specific oop field
  volatile intx     _f2;       // entry specific int/oop field
  volatile intx     _flags;    // flags


#ifdef ASSERT
  bool same_methodOop(oop cur_f1, oop f1);
#endif

  void set_bytecode_1(Bytecodes::Code code);
  void set_bytecode_2(Bytecodes::Code code);
  void set_f1(oop f1)                            {
objectRef existing_f1=lvb_ref(&_f1);//read once
assert(existing_f1.is_null()||existing_f1.as_oop()==f1,"illegal field change");
    assert(!objectRef(f1).is_stack(), "stack obj not allowed here");
    // There is no point in keeping the volatile qualifier for the ref_store,
    // it never reads the value!
    ref_store_without_check((heapRef*)&_f1, heapRef(f1));
  }

  // Poisoning f2 when setting an objectRef must be done by the caller:
  void set_f2(intx newf2)                        { 
    DEBUG_ONLY (
    intx oldf2=f2();
    intptr_t newf2_unpoisoned = objectRef(newf2).is_poisoned() ? ALWAYS_UNPOISON_OBJECTREF(objectRef(newf2)).raw_value() : newf2;
    intptr_t oldf2_unpoisoned = objectRef(oldf2).is_poisoned() ? ALWAYS_UNPOISON_OBJECTREF(objectRef(oldf2)).raw_value() : oldf2;
    assert(oldf2_unpoisoned == 0 || oldf2_unpoisoned == newf2_unpoisoned, "illegal field change");
    )
    _f2 = newf2; 
  }

int as_flags(BasicType state,bool is_final,bool is_vfinal,bool is_volatile,
               bool is_method_interface, bool is_method);
  void set_flags(intx flags)                     { _flags = flags; }

 public:
  // specific bit values in flag field
  // Note: the interpreter knows this layout!
  enum FlagBitValues {
    hotSwapBit    = 23,
    methodInterface = 24,
    volatileField = 25,
    vfinalMethod  = 26,
    finalField    = 27
  }; 

  enum { field_index_mask = 0xFFFF };

  // start of type bits in flags
  // Note: the interpreter knows this layout!
  enum FlagValues {
    tosBits      = 28 
  };

  // Initialization
  void set_initial_state(int index);             // sets entry to initial state

  void set_field(                                // sets entry to resolved field state
    Bytecodes::Code get_code,                    // the bytecode used for reading the field
    Bytecodes::Code put_code,                    // the bytecode used for writing the field
    KlassHandle     field_holder,                // the object/klass holding the field
    int             orig_field_index,            // the original field index in the field holder
    int             field_offset,                // the field offset in words in the field holder
BasicType field_type,//the (machine) field type
    bool            is_final,                     // the field is final 
    bool            is_volatile                  // the field is volatile 
  );

  void set_method(                               // sets entry to resolved method entry
    Bytecodes::Code invoke_code,                 // the bytecode used for invoking the method
    methodHandle    method,                      // the method/prototype if any (NULL, otherwise)
    int             vtable_index                 // the vtable index if any, else negative
  );

  void set_interface_call(
    methodHandle method,                         // Resolved method    
    int index                                    // Method index into interface
  );               

  void set_parameter_size(int value) {
    assert(parameter_size() == 0 || parameter_size() == value, 
           "size must not change");
    // Setting the parameter size by itself is only safe if the
    // current value of _flags is 0, otherwise another thread may have
    // updated it and we don't want to overwrite that value.  Don't
    // bother trying to update it once it's nonzero but always make
    // sure that the final parameter size agrees with what was passed.
    if (_flags == 0) {
      Atomic::cmpxchg_ptr((value & 0xFF), &_flags, 0);
    }
    guarantee(parameter_size() == value, "size must not change");
  }

  // Which bytecode number (1 or 2) in the index field is valid for this bytecode?
  // Returns -1 if neither is valid.
  static int bytecode_number(Bytecodes::Code code) {
    switch (code) {
      case Bytecodes::_getstatic       :    // fall through
      case Bytecodes::_getfield        :    // fall through
      case Bytecodes::_invokespecial   :    // fall through
      case Bytecodes::_invokestatic    :    // fall through
      case Bytecodes::_invokeinterface : return 1;
      case Bytecodes::_putstatic       :    // fall through
      case Bytecodes::_putfield        :    // fall through
      case Bytecodes::_invokevirtual   : return 2;
      default                          : break;
    }
    return -1;
  }

  // Has this bytecode been resolved? Only valid for invokes and get/put field/static.
  bool is_resolved(Bytecodes::Code code) const {
    switch (bytecode_number(code)) {
      case 1:  return (bytecode_1() == code);
      case 2:  return (bytecode_2() == code);
    }
    return false;      // default: not resolved
  }

  // Accessors
  int constant_pool_index() const                { return _indices & 0xFFFF; }
  Bytecodes::Code bytecode_1() const             { return Bytecodes::cast((_indices >> 16) & 0xFF); }
  Bytecodes::Code bytecode_2() const             { return Bytecodes::cast((_indices >> 24) & 0xFF); }
volatile oop f1()const{return lvb_ref(&_f1).as_oop();}
intx f2()const{
                                                   if (is_vfinal()) {
                                                     return lvb_ref((objectRef*)&_f2).raw_value();
                                                   } else {
                                                     return _f2;
                                                   }
                                                 }
  int  field_index() const;
  int  parameter_size() const                    { return _flags & 0xFF; }
  bool is_vfinal() const                         { return ((_flags & (1 << vfinalMethod)) == (1 << vfinalMethod)); }
  bool is_volatile() const                       { return ((_flags & (1 << volatileField)) == (1 << volatileField)); }
  bool is_methodInterface() const                { return ((_flags & (1 << methodInterface)) == (1 << methodInterface)); }
BasicType flag_state()const{assert(((_flags>>tosBits)&0x0F)<16,"Invalid state in as_flags");
return(BasicType)((_flags>>tosBits)&0x0F);}

  // Code generation support
  static WordSize size()                         { return in_WordSize(sizeof(ConstantPoolCacheEntry) / HeapWordSize); }
  static ByteSize indices_offset()               { return byte_offset_of(ConstantPoolCacheEntry, _indices); }
  static ByteSize f1_offset()                    { return byte_offset_of(ConstantPoolCacheEntry, _f1); }
  static ByteSize f2_offset()                    { return byte_offset_of(ConstantPoolCacheEntry, _f2); }
  static ByteSize flags_offset()                 { return byte_offset_of(ConstantPoolCacheEntry, _flags); }

  // GC Support
  void oops_do(void f(oop*));
  void oop_iterate(OopClosure* blk);
  void oop_iterate_m(OopClosure* blk, MemRegion mr);
  void follow_contents();
  void adjust_pointers();

  void PGC_follow_contents(PGC_FullGCManager* fgcm, int klassId);
  void GPGC_follow_contents(GPGC_GCManagerOldStrong* gcm, int klassId);
  void GPGC_follow_contents(GPGC_GCManagerOldFinal* gcm);
  void GPGC_verify_no_cardmark();
  
  // Parallel Old
  void follow_contents(ParCompactionManager* cm);
  void update_pointers();
  void update_pointers(HeapWord* beg_addr, HeapWord* end_addr);

  // RedefineClasses() API support:
  // If this constantPoolCacheEntry refers to old_method then update it
  // to refer to new_method.
  // trace_name_printed is set to true if the current call has
  // printed the klass name so that other routines in the adjust_*
  // group don't print the klass name.
  bool adjust_method_entry(methodOop old_method, methodOop new_method,
         bool * trace_name_printed);
  bool is_interesting_method_entry(klassOop k);
  bool is_field_entry() const                    { return (_flags & (1 << hotSwapBit)) == 0; }
  bool is_method_entry() const                   { return (_flags & (1 << hotSwapBit)) != 0; }

  // Debugging & Printing
  void print (outputStream* st, int index) const;
  void verify(outputStream* st) const;

  static void verify_tosBits() {
    assert(tosBits == 28, "interpreter now assumes tosBits is 28");
  }
};


// A constant pool cache is a runtime data structure set aside to a constant pool. The cache
// holds interpreter runtime information for all field access and invoke bytecodes. The cache
// is created and initialized before a class is actively used (i.e., initialized), the indivi-
// dual cache entries are filled at resolution (i.e., "link") time (see also: rewriter.*).

class constantPoolCacheOopDesc: public arrayOopDesc {
 private:
  constantPoolRef _constant_pool;                // the corresponding constant pool

  // Sizing
  static int header_size()                       { return sizeof(constantPoolCacheOopDesc) / HeapWordSize; }
static int object_size(int length){return header_size()+length*in_words(ConstantPoolCacheEntry::size());}
  int object_size()                              { return object_size(length()); }

  // Helpers
  constantPoolRef*        constant_pool_addr()   { return &_constant_pool; }
  ConstantPoolCacheEntry* base() const           { return (ConstantPoolCacheEntry*)((address)this + in_bytes(base_offset())); }

  friend class constantPoolCacheKlass;

 public:
  // Initialization
  void initialize(intArray& inverse_index_map);

  // Accessors
void set_constant_pool(constantPoolOop pool){ref_store_without_check(&_constant_pool,constantPoolRef(pool));}
constantPoolOop constant_pool()const{return _constant_pool.as_constantPoolOop();}
  ConstantPoolCacheEntry* entry_at(int i) const  { assert(0 <= i && i < length(), "index out of bounds"); return base() + i; }

  // Code generation
  static ByteSize base_offset()                  { return in_ByteSize(sizeof(constantPoolCacheOopDesc)); }

  // RedefineClasses() API support:
  // If any entry of this constantPoolCache points to any of
  // old_methods, replace it with the corresponding new_method.
  // trace_name_printed is set to true if the current call has
  // printed the klass name so that other routines in the adjust_*
  // group don't print the klass name.
  void adjust_method_entries(methodRef* old_methods, methodRef* new_methods,
                             int methods_length, bool * trace_name_printed);
};

#endif // CPCACHEOOP_HPP
