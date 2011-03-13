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


#include "arrayKlass.hpp"
#include "arrayKlassKlass.hpp"
#include "handles.hpp"
#include "javaClasses.hpp"
#include "klassVtable.hpp"
#include "markSweep.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "psParallelCompact.hpp"
#include "universe.hpp"

#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "universe.inline.hpp"

void arrayKlassKlass::unused_initial_virtual() { }

klassOop arrayKlassKlass::create_klass(TRAPS) {
  arrayKlassKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),arrayKlass_kid,CHECK_NULL);
  // Make sure size calculation is right
assert(k()->size()==header_size(),"wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror, make links
  KlassTable::bindReservedKlassId(k(), arrayKlass_kid);
  return k();
}

bool arrayKlassKlass::oop_is_parsable(oop obj) const {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  return (!ak->null_vtbl()) && ak->object_is_parsable();
}


int arrayKlassKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  MarkSweep::adjust_pointer(ak->adr_component_mirror());
  MarkSweep::adjust_pointer(ak->adr_lower_dimension());
  MarkSweep::adjust_pointer(ak->adr_higher_dimension());
  {
    HandleMark hm;
    ak->vtable()->oop_adjust_pointers();
  }
  return klassKlass::oop_adjust_pointers(obj);
}


int arrayKlassKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  blk->do_oop(ak->adr_component_mirror());
  blk->do_oop(ak->adr_lower_dimension());
  blk->do_oop(ak->adr_higher_dimension());
  ak->vtable()->oop_oop_iterate(blk);
  return klassKlass::oop_oop_iterate(obj, blk);
}


int arrayKlassKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  blk->do_oop(ak->adr_component_mirror());
  blk->do_oop(ak->adr_lower_dimension());
  blk->do_oop(ak->adr_higher_dimension());
  ak->vtable()->oop_oop_iterate_m(blk, mr);
  return klassKlass::oop_oop_iterate_m(obj, blk, mr);
}

void arrayKlassKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->blueprint()->oop_is_arrayKlass(),"must be an array klass");
}

void arrayKlassKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->blueprint()->oop_is_arrayKlass(),"must be an array klass");
}

int arrayKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  PSParallelCompact::adjust_pointer(ak->adr_component_mirror());
  PSParallelCompact::adjust_pointer(ak->adr_lower_dimension());
  PSParallelCompact::adjust_pointer(ak->adr_higher_dimension());
  {
    HandleMark hm;
    ak->vtable()->oop_update_pointers(cm);
  }
  return klassKlass::oop_update_pointers(cm, obj);
}

int
arrayKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				     HeapWord* beg_addr, HeapWord* end_addr) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));

  heapRef* p;
  p = ak->adr_component_mirror();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
  p = ak->adr_lower_dimension();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
  p = ak->adr_higher_dimension();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);

  {
    HandleMark hm;
    ak->vtable()->oop_update_pointers(cm, beg_addr, end_addr);
  }
  return klassKlass::oop_update_pointers(cm, obj, beg_addr, end_addr);
}

#ifndef PRODUCT

// Printing

void arrayKlassKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  klassKlass::oop_print_on(obj, st);
}


void arrayKlassKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  for(int index = 0; index < ak->dimension(); index++) {
    st->print("[]");
  }
}
#endif


const char* arrayKlassKlass::internal_name() const {
  return "{array class}";
}

void arrayKlassKlass::oop_verify_on(oop obj, outputStream* st) {
  klassKlass::oop_verify_on(obj, st);

  arrayKlass* ak = arrayKlass::cast(klassOop(obj));
  if (!obj->partially_loaded()) {
    if (ak->component_mirror() != NULL)
      guarantee(ak->component_mirror()->klass(), "should have a class");
    if (ak->lower_dimension() != NULL)
      guarantee(ak->lower_dimension()->klass(), "should have a class");
    if (ak->higher_dimension() != NULL)
      guarantee(ak->higher_dimension()->klass(), "should have a class");
  }
}
