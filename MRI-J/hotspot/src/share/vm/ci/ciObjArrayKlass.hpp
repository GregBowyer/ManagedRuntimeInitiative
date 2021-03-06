/*
 * Copyright 1999-2001 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CIOBJARRAYKLASS_HPP
#define CIOBJARRAYKLASS_HPP


#include "ciArrayKlass.hpp"

#include "ciSymbol.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "handles.hpp"

// ciObjArrayKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part is an objArrayKlass.
class ciObjArrayKlass : public ciArrayKlass {
  CI_PACKAGE_ACCESS
  friend class ciEnv;

private:
  ciKlass* _element_klass;
  ciKlass* _base_element_klass;

protected:
  ciObjArrayKlass(KlassHandle h_k);
  ciObjArrayKlass(ciSymbol* array_name,
                  ciKlass* base_element_klass,
                  int dimension);
  ciObjArrayKlass(FAMPtr old_cioak);

  objArrayKlass* get_objArrayKlass() {
    return (objArrayKlass*)get_Klass();
  }

  static ciObjArrayKlass* make_impl(ciKlass* element_klass);
  static ciSymbol* construct_array_name(ciSymbol* element_name,
                                        int       dimension);

  const char* type_string() { return "ciObjArrayKlass"; }

  oop     loader()        { return _base_element_klass->loader(); }
  jobject loader_handle() { return _base_element_klass->loader_handle(); }

  oop     protection_domain()        { return _base_element_klass->protection_domain(); }
  jobject protection_domain_handle() { return _base_element_klass->protection_domain_handle(); }


public:
  virtual void fixupFAMPointers();

  // The one-level type of the array elements.
  ciKlass* element_klass();

  // The innermost type of the array elements.
  ciKlass* base_element_klass() { return _base_element_klass; }

  // What kind of ciObject is this?
  bool is_obj_array_klass() { return true; }

  static ciObjArrayKlass* make(ciKlass* element_klass);
};

#endif // CIOBJARRAYKLASS_HPP
