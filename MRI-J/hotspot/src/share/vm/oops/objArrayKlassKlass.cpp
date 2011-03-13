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


#include "artaObjects.hpp"
#include "collectedHeap.hpp"
#include "javaClasses.hpp"
#include "markSweep.hpp"
#include "mutexLocker.hpp"
#include "objArrayKlassKlass.hpp"
#include "objArrayOop.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "psParallelCompact.hpp"
#include "symbolOop.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "universe.inline.hpp"

#include "oop.inline2.hpp"

void objArrayKlassKlass::unused_initial_virtual() { }

klassOop objArrayKlassKlass::create_klass(TRAPS) {
  objArrayKlassKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());  
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),objArrayKlass_kid,CHECK_0);
assert(k()->size()==header_size(),"wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_0); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), objArrayKlass_kid);
  return k();
}

klassOop objArrayKlassKlass::allocate_system_objArray_klass(TRAPS) {
  // system_objArrays have no instance klass, so allocate with fake class, then reset to NULL
  KlassHandle kk(THREAD, Universe::intArrayKlassObj());
  klassOop k = allocate_objArray_klass(1, kk, CHECK_0);
  objArrayKlass* tk = (objArrayKlass*) k->klass_part();
  tk->set_element_klass(NULL);
  tk->set_bottom_klass(NULL);
  assert( k->klass_part()->klassId() == systemObjArrayKlass_kid, "expected kid mismatch" );
  return k;
}


klassOop objArrayKlassKlass::allocate_objArray_klass(int n, KlassHandle element_klass, TRAPS) {
  objArrayKlassKlassHandle this_oop(THREAD, as_klassOop());
  return allocate_objArray_klass_impl(this_oop, n, element_klass, THREAD);
}

klassOop objArrayKlassKlass::allocate_objArray_klass_impl(objArrayKlassKlassHandle this_oop, 
                                                          int n, KlassHandle element_klass, TRAPS) {

  // Eagerly allocate the direct array supertype.
  KlassHandle super_klass = KlassHandle();
  if (!Universe::is_bootstrapping()) {
    KlassHandle element_super (THREAD, element_klass->super());
    if (element_super.not_null()) {
      // The element type has a direct super.  E.g., String[] has direct super of Object[].
      super_klass = KlassHandle(THREAD, element_super->array_klass_or_null());
      bool supers_exist = super_klass.not_null();
      // Also, see if the element has secondary supertypes.
      // We need an array type for each.
      objArrayHandle element_supers = objArrayHandle(THREAD, 
                                            element_klass->secondary_supers());
      for( int i = element_supers->length()-1; i >= 0; i-- ) {
	klassOop elem_super = (klassOop) element_supers->obj_at(i);
	if (Klass::cast(elem_super)->array_klass_or_null() == NULL) {
	  supers_exist = false;
	  break;
	}
      }
      if (!supers_exist) {
	// Oops.  Not allocated yet.  Back out, allocate it, and retry.
        KlassHandle ek;
        {
MutexUnlocker_GC_on_Relock mu(MultiArray_lock);
MutexUnlocker_GC_on_Relock mc(Compile_lock);//for vtables
	  klassRef sk = Klass::array_klass(element_super.as_klassRef(),CHECK_0);
	  super_klass = KlassHandle(sk);
	  for( int i = element_supers->length()-1; i >= 0; i-- ) {
	    KlassHandle elem_super (THREAD, element_supers->obj_at(i));
            Klass::array_klass(elem_super.as_klassRef(),CHECK_0);
	  }
          // Now retry from the beginning
          klassRef klass_ref = Klass::array_klass(element_klass.as_klassRef(), n, CHECK_0);
          // Create a handle because the enclosing brace, when locking
          // can cause a gc.  Better to have this function return a Handle.
ek=KlassHandle(klass_ref);
        }  // re-lock; possible GC
        return ek();
      }
    } else {
      // The element type is already Object.  Object[] has direct super of Object.
      super_klass = KlassHandle(THREAD, SystemDictionary::object_klass());
    }
  }

  // Create type name for klass (except for symbol arrays, since symbolKlass
  // does not have a name).  This will potentially allocate an object, cause
  // GC, and all other kinds of things.  Hence, this must be done before we
  // get a handle to the new objArrayKlass we want to construct.  We cannot
  // block while holding a handling to a partly initialized object.
  symbolHandle name = symbolHandle();
  
  if (!element_klass->oop_is_symbol()) {
    ResourceMark rm(THREAD);
    char *name_str = element_klass->name()->as_C_string();
    int len = element_klass->name()->utf8_length();
    char *new_str = NEW_RESOURCE_ARRAY(char, len + 4);
    int idx = 0;
    new_str[idx++] = '[';
    if (element_klass->oop_is_instance()) { // it could be an array or simple type
      new_str[idx++] = 'L';
    } 
    memcpy(&new_str[idx], name_str, len * sizeof(char));
    idx += len;
    if (element_klass->oop_is_instance()) {
      new_str[idx++] = ';';
    }
    new_str[idx++] = '\0';
    name = oopFactory::new_symbol_handle(new_str, CHECK_0);    
  } 

  objArrayKlass o;  
  arrayKlassHandle k = arrayKlass::base_create_array_klass(o.vtbl_value(), 
                                                           objArrayKlass::header_size(), 
this_oop,0,
                                                           CHECK_0);

  // Initialize instance variables  
  objArrayKlass* oak = objArrayKlass::cast(k());
  oak->set_dimension(n);
  oak->set_element_klass(element_klass());
  oak->set_name(name());

  klassOop bk;
  if (element_klass->oop_is_objArray()) {
    bk = objArrayKlass::cast(element_klass())->bottom_klass();
  } else {
    bk = element_klass();
  }
  assert(bk != NULL && (Klass::cast(bk)->oop_is_instance() || Klass::cast(bk)->oop_is_typeArray()), "invalid bottom klass");
  oak->set_bottom_klass(bk);

  oak->set_layout_helper(array_layout_helper(T_OBJECT));
  assert(oak->oop_is_javaArray(), "sanity");
  assert(oak->oop_is_objArray(), "sanity");

  // Call complete_create_array_klass after all instance variables has been initialized.
  arrayKlass::complete_create_array_klass(k, super_klass, CHECK_0);

  return k();
}


int objArrayKlassKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_klass(), "must be klass");
  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");

  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  MarkSweep::adjust_pointer(oak->element_klass_addr());
  MarkSweep::adjust_pointer(oak->bottom_klass_addr());

  return arrayKlassKlass::oop_adjust_pointers(obj);
}



int objArrayKlassKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_klass(), "must be klass");
  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");

  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  blk->do_oop(oak->element_klass_addr());
  blk->do_oop(oak->bottom_klass_addr());

  return arrayKlassKlass::oop_oop_iterate(obj, blk);
}


int
objArrayKlassKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert(obj->is_klass(), "must be klass");
  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");

  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  objectRef* addr;
  addr = oak->element_klass_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  addr = oak->bottom_klass_addr();
  if (mr.contains(addr)) blk->do_oop(addr);

  return arrayKlassKlass::oop_oop_iterate(obj, blk);
}

void objArrayKlassKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->blueprint()->oop_is_objArrayKlass(),"must be an obj array klass");
}

void objArrayKlassKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->blueprint()->oop_is_objArrayKlass(),"must be an obj array klass");
}

int objArrayKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_klass(), "must be klass");
  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");

  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  PSParallelCompact::adjust_pointer(oak->element_klass_addr());
  PSParallelCompact::adjust_pointer(oak->bottom_klass_addr());

  return arrayKlassKlass::oop_update_pointers(cm, obj);
}

int objArrayKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
					    HeapWord* beg_addr,
					    HeapWord* end_addr) {
  assert(obj->is_klass(), "must be klass");
  assert(klassOop(obj)->klass_part()->oop_is_objArray_slow(), "must be obj array");

  objectRef* p;
  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  p = oak->element_klass_addr();
PSParallelCompact::adjust_pointer((heapRef*)p,beg_addr,end_addr);
  p = oak->bottom_klass_addr();
PSParallelCompact::adjust_pointer((heapRef*)p,beg_addr,end_addr);

  return arrayKlassKlass::oop_update_pointers(cm, obj, beg_addr, end_addr);
}

#ifndef PRODUCT

// Printing

void objArrayKlassKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  objArrayKlass* oak = (objArrayKlass*) klassOop(obj)->klass_part();
  klassKlass::oop_print_on(obj, st);
  st->print(" - instance klass: ");
  oak->element_klass()->print_value_on(st);
  st->cr();
}


void objArrayKlassKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  objArrayKlass* oak = (objArrayKlass*) klassOop(obj)->klass_part();

  oak->element_klass()->print_value_on(st);
  st->print("[]");
}

#endif

const char* objArrayKlassKlass::internal_name() const {
  return "{object array class}";
}


// Verification

void objArrayKlassKlass::oop_verify_on(oop obj, outputStream* st) {
  klassKlass::oop_verify_on(obj, st);
  objArrayKlass* oak = objArrayKlass::cast((klassOop)obj);
  guarantee(oak->element_klass()->is_perm(),  "should be in permspace");
  guarantee(oak->element_klass()->is_klass(), "should be klass");
  guarantee(oak->bottom_klass()->is_perm(),   "should be in permspace");
  guarantee(oak->bottom_klass()->is_klass(),  "should be klass");
  Klass* bk = Klass::cast(oak->bottom_klass());
  guarantee(bk->oop_is_instance() || bk->oop_is_typeArray(),  "invalid bottom klass");
}

void objArrayKlassKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  assert(obj->is_klass(), "must be klass");
objArrayKlass*oak=objArrayKlass::cast(klassOop(obj));
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
    xb->name_value_item("name", oak->external_name());
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}


void objArrayKlassKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  assert(obj->is_klass(), "must be klass");
objArrayKlass*oak=objArrayKlass::cast(klassOop(obj));
  xmlElement xe(xb, "klass");
  xb->name_value_item("name", oak->external_name());
  { xmlElement xe(xb, "super", xmlElement::delayed_LF);
  oak->super()->print_xml_on(xb, true);
  }
  xb->name_value_item("instance_size", oak->array_header_in_bytes());
  xb->name_value_item("klass_size", oak->object_size());
  { xmlElement xe(xb,"flags", xmlElement::delayed_LF);
oak->access_flags().print_on(xb);
  }
  for( Klass* sub = oak->subklass(); sub; sub = sub->next_sibling()) {
    xmlElement xe(xb, "subklass", xmlElement::delayed_LF);
    sub->as_klassOop()->print_xml_on(xb,true);
  }
}
