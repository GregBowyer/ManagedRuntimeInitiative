// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef METHODCODEKLASS_HPP
#define METHODCODEKLASS_HPP

#include "klass.hpp"
#include "orderAccess.hpp"

class CodeBlob;
class RegMap;
class DebugMap;

class methodCodeKlass:public Klass{
 
  juint    _alloc_size;        // allocation profiling support

 public:  
  bool oop_is_methodCode() const { return true; }

  // Allocation
DEFINE_ALLOCATE_PERMANENT(methodCodeKlass);
  methodCodeOop allocate(const CodeBlob *blob, const DebugMap* dbginfo, const PC2PCMap *pc2pcinfo, const CodeProfile *profile, objArrayHandle dep_klasses, objArrayHandle dep_methods, int compile_id, int entry_bci, bool has_unsafe, objArrayHandle srefs, TRAPS);
  static klassOop create_klass(TRAPS);

  // Sizing
  int oop_size(oop obj) const;
int GC_oop_size(oop obj)const;
  int klass_oop_size() const     { return object_size(); }

  // Casting from klassOop
static methodCodeKlass*cast(klassOop k){
assert(k->klass_part()->oop_is_methodCode(),"cast to methodCodeKlass");
return(methodCodeKlass*)k->klass_part();
  }

  // Sizing
static int header_size(){return oopDesc::header_size()+sizeof(methodCodeKlass)/HeapWordSize;}
int object_size()const{return header_size();}

  // Garbage collection
  void oop_follow_contents(oop obj);
  int  oop_adjust_pointers(oop obj);
  bool oop_is_parsable(oop obj) const;

  // Parallel Scavenge
  void oop_copy_contents(PSPromotionManager* pm, oop obj);
  void oop_push_contents(PSPromotionManager* pm, oop obj);

  // Parallel Old
  virtual void oop_follow_contents(ParCompactionManager* cm, oop obj);
  virtual int  oop_update_pointers(ParCompactionManager* cm, oop obj);
  virtual int  oop_update_pointers(ParCompactionManager* cm, oop obj,
				   HeapWord* beg_addr, HeapWord* end_addr);

  // GenPauselessGC
  void GPGC_oop_follow_contents(GPGC_GCManagerNewStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerNewFinal* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldStrong* gcm, oop obj);
  void GPGC_oop_follow_contents(GPGC_GCManagerOldFinal* gcm, oop obj);
void GPGC_newgc_oop_update_cardmark(oop obj);
void GPGC_oldgc_oop_update_cardmark(oop obj);
void GPGC_mutator_oop_update_cardmark(oop obj);
				   
  // Allocation profiling support
  juint alloc_size() const              { return _alloc_size; }
  void set_alloc_size(juint n)          { _alloc_size = n; }

  // Iterators
  int oop_oop_iterate(oop obj, OopClosure* blk);
  int oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr);

  void oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref);
  void oop_print_xml_on_as_object(oop obj, xmlBuffer *xb);

  const char* internal_name() const;
#ifndef PRODUCT
  // Printing
void oop_print_on(oop obj,outputStream*st);
void oop_print_value_on(oop obj,outputStream*st);

  // Verify operations
void oop_verify_on(oop obj,outputStream*st);

#endif
};

#endif // METHODCODEKLASS_HPP

