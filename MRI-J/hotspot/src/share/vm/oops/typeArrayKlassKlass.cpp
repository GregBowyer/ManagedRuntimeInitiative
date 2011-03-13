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
#include "javaClasses.hpp"
#include "oopTable.hpp"
#include "typeArrayKlassKlass.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

void typeArrayKlassKlass::unused_initial_virtual() { }

klassOop typeArrayKlassKlass::create_klass(TRAPS) {
  typeArrayKlassKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());  
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),typeArrayKlass_kid,CHECK_NULL);
assert(k()->size()==header_size(),"wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), typeArrayKlass_kid);
  return k();
}


#ifndef PRODUCT

// Printing

void typeArrayKlassKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  oop_print_value_on(obj, st);
  Klass:: oop_print_on(obj, st); 
}


void typeArrayKlassKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  st->print("{type array ");
  st->print(type2name(typeArrayKlass::cast(klassOop(obj))->element_type()));
  st->print("}");
}

#endif

const char* typeArrayKlassKlass::internal_name() const {
  return "{type array class}";
}



void typeArrayKlassKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  assert(obj->is_klass(), "must be klass");
typeArrayKlass*tak=typeArrayKlass::cast(klassOop(obj));
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
    { xmlElement xen(xb,"name", xmlElement::delayed_LF);
xb->print("[");
    xb->print(type2name(tak->element_type()));
    }
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}


void typeArrayKlassKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  assert(obj->is_klass(), "must be klass");
typeArrayKlass*tak=typeArrayKlass::cast(klassOop(obj));
  xmlElement xe(xb, "klass");
  { xmlElement xen(xb,"name", xmlElement::delayed_LF);
xb->print("[");
  xb->print(type2name(tak->element_type()));
  }
  { xmlElement xe(xb, "super", xmlElement::delayed_LF);
  tak->super()->print_xml_on(xb, true);
  }
  xb->name_value_item("instance_size", tak->array_header_in_bytes());
  xb->name_value_item("klass_size", tak->object_size());
  { xmlElement xe(xb,"flags", xmlElement::delayed_LF);
tak->access_flags().print_on(xb);
  }
}
