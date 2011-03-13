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
#ifndef OBJARRAYKLASS_HPP
#define OBJARRAYKLASS_HPP

#include "arrayKlass.hpp"
#include "orderAccess.hpp"
#include "refsHierarchy_pd.hpp"

// objArrayKlass is the klass for objArrays

class objArrayKlass : public arrayKlass {
 private:
  klassRef _element_klass;            // The klass of the elements of this array type
  klassRef _bottom_klass;             // The one-dimensional type (instanceKlass or typeArrayKlass)
 public:
  // Instance variables
klassOop element_klass()const{return lvb_klassRef(&_element_klass).as_klassOop();}
void set_element_klass(klassOop k){ref_store_without_check(&_element_klass,klassRef(k));}
  heapRef* element_klass_addr()       { return (heapRef*)&_element_klass; }

  klassOop bottom_klass() const       { return lvb_klassRef(&_bottom_klass).as_klassOop(); }
  void set_bottom_klass(klassOop k)   { ref_store_without_check(&_bottom_klass, klassRef(k)); }
  heapRef* bottom_klass_addr()        { return (heapRef*)&_bottom_klass; }

  // Compiler/Interpreter offset
  static int element_klass_offset_in_bytes() { return offset_of(objArrayKlass, _element_klass); }

  // Dispatched operation
  bool can_be_primary_super_slow() const;
  objArrayOop compute_secondary_supers(int num_extra_slots, TRAPS);
  bool compute_is_subtype_of(klassOop k);
  bool oop_is_objArray_slow()  const  { return true; }
  int oop_size(oop obj) const;
int GC_oop_size(oop obj)const;
  int klass_oop_size() const          { return object_size(); }

  // Allocation
  DEFINE_ALLOCATE_PERMANENT(objArrayKlass);
objArrayOop allocate(int length,intptr_t sba_hint,TRAPS);
oop multi_allocate(int rank,jint*sizes,intptr_t sba_hint,TRAPS);

  // Copying
  void  copy_array(arrayOop s, int src_pos, arrayOop d, int dst_pos, int length, TRAPS);

  // Compute protection domain
  oop protection_domain() { return Klass::cast(bottom_klass())->protection_domain(); }
  // Compute class loader
  oop class_loader() const { return Klass::cast(bottom_klass())->class_loader(); }

 protected:
  // Returns the objArrayKlass for n'th dimension.
virtual klassRef array_klass_impl(klassRef thsi,bool or_null,int n,TRAPS);

  // Returns the array class with this class as element type.
virtual klassRef array_klass_impl(klassRef thsi,bool or_null,TRAPS);

 public:
  // Casting from klassOop
  static objArrayKlass* cast(klassOop k) {
    assert(k->klass_part()->oop_is_objArray_slow(), "cast to objArrayKlass");
    return (objArrayKlass*) k->klass_part(); 
  }

  // Sizing
  static int header_size()                { return oopDesc::header_size() + sizeof(objArrayKlass)/HeapWordSize; }
  int object_size() const                 { return arrayKlass::object_size(header_size()); }

  // Initialization (virtual from Klass)
  void initialize(TRAPS);

  // Garbage collection
  void oop_follow_contents(oop obj);
  int  oop_adjust_pointers(oop obj);

  // Parallel Scavenge and Parallel Old
  PARALLEL_GC_DECLS

  // GenPauselessGC
  void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm, oop obj);
void GPGC_newgc_oop_update_cardmark(oop obj);
void GPGC_oldgc_oop_update_cardmark(oop obj);
void GPGC_mutator_oop_update_cardmark(oop obj);
				   
  // Iterators
  int oop_oop_iterate(oop obj, OopClosure* blk) {
    return oop_oop_iterate_v(obj, blk);
  }
  int oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
    return oop_oop_iterate_v_m(obj, blk, mr);
  }
#define ObjArrayKlass_OOP_OOP_ITERATE_DECL(OopClosureType, nv_suffix)   \
  int oop_oop_iterate##nv_suffix(oop obj, OopClosureType* blk);         \
  int oop_oop_iterate##nv_suffix##_m(oop obj, OopClosureType* blk,      \
                                     MemRegion mr);

  ALL_OOP_OOP_ITERATE_CLOSURES_1(ObjArrayKlass_OOP_OOP_ITERATE_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_3(ObjArrayKlass_OOP_OOP_ITERATE_DECL)

  // JVM support
  jint compute_modifier_flags(TRAPS) const;

 private:
   static klassRef array_klass_impl   (objArrayKlassHandle this_oop, bool or_null, int n, TRAPS);

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
  void oop_verify_old_oop(oop obj, objectRef* p, bool allow_dirty);

  void oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref);
  void oop_print_xml_on_as_object(oop obj, xmlBuffer *xb);
};

#endif // OBJARRAYKLASS_HPP
