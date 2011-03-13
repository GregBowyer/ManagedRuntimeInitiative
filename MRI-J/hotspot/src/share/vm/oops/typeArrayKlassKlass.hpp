/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef TYPEARRAYKLASSKLASS_HPP
#define TYPEARRAYKLASSKLASS_HPP


#include "arrayKlassKlass.hpp"
#include "typeArrayKlass.hpp"

// A typeArrayKlassKlass is the klass of a typeArrayKlass

class typeArrayKlassKlass : public arrayKlassKlass {
  virtual void unused_initial_virtual();
 public:
  // Testing
  bool oop_is_typeArrayKlass() const { return true; }

  // Dispatched operation
  int oop_size(oop obj) const { return typeArrayKlass::cast(klassOop(obj))->object_size(); }
  int GC_oop_size(oop obj) const { return oop_size(obj); }
  int klass_oop_size() const  { return object_size(); }

  // Allocation
  DEFINE_ALLOCATE_PERMANENT(typeArrayKlassKlass);
  static klassOop create_klass(TRAPS);
 
  // Casting from klassOop
  static typeArrayKlassKlass* cast(klassOop k) {
    assert(k->klass_part()->oop_is_klass(), "cast to typeArrayKlassKlass");
    return (typeArrayKlassKlass*) k->klass_part(); 
  }

  // Sizing
  static int header_size() { return oopDesc::header_size() + sizeof(typeArrayKlassKlass)/HeapWordSize; }
int object_size()const{return header_size();}

#ifndef PRODUCT
 public:
  // Printing
  void oop_print_on(oop obj, outputStream* st);
  void oop_print_value_on(oop obj, outputStream* st);
#endif
 public:
  const char* internal_name() const;

  void oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref);
  void oop_print_xml_on_as_object(oop obj, xmlBuffer *xb);
};

#endif // TYPEARRAYKLASSKLASS_HPP

