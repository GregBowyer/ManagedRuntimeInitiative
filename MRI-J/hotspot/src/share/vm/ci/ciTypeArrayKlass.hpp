/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CITYPEARRAYKLASS_HPP
#define CITYPEARRAYKLASS_HPP


#include "ciArrayKlass.hpp"

// ciTypeArrayKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part in a TypeArrayKlass.
class ciTypeArrayKlass : public ciArrayKlass {
  CI_PACKAGE_ACCESS

private:
BasicType _cached_element_type;

protected:
  ciTypeArrayKlass(KlassHandle h_k);
  ciTypeArrayKlass(FAMPtr old_citak);

  typeArrayKlass* get_typeArrayKlass() {
    return (typeArrayKlass*)get_Klass();
  }

  const char* type_string() { return "ciTypeArrayKlass"; }

  // Helper method for make.
  static ciTypeArrayKlass* make_impl(BasicType type);

public:
  virtual void fixupFAMPointers() {
    ciArrayKlass::fixupFAMPointers();
  }

  // The type of the array elements.
  BasicType element_type() { return _cached_element_type; }

  // What kind of ciObject is this?
  bool is_type_array_klass() { return true; }

  // Make an array klass corresponding to the specified primitive type.
  static ciTypeArrayKlass* make(BasicType type);
};

#endif // CITYPEARRAYKLASS_HPP
