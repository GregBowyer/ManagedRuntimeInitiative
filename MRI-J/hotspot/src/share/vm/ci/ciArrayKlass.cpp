/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciArrayKlass.hpp"

#include "arrayKlass.hpp"
#include "arrayOop.hpp"
#include "ciEnv.hpp"
#include "ciTypeArrayKlass.hpp"
#include "ciObjArrayKlass.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "handles.hpp"

// ciArrayKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part in an arrayKlass.

// ------------------------------------------------------------------
// ciArrayKlass::ciArrayKlass
//
// Loaded array klass.
ciArrayKlass::ciArrayKlass(KlassHandle h_k) : ciKlass(h_k) {
  assert(get_Klass()->oop_is_array(), "wrong type");
  _dimension = get_arrayKlass()->dimension();
}

ciArrayKlass::ciArrayKlass(FAMPtr old_ciak):ciKlass(old_ciak){
  FAM->mapNewToOldPtr(this, old_ciak);

  _dimension = FAM->getInt("((struct ciArrayKlass*)%p)->_dimension", old_ciak);
}

// ------------------------------------------------------------------
// ciArrayKlass::ciArrayKlass
//
// Unloaded array klass.
ciArrayKlass::ciArrayKlass(ciSymbol* name, int dimension, ciKlass* klass)
  : ciKlass(name, klass) {
  _dimension = dimension;
}

// ------------------------------------------------------------------
// ciArrayKlass::element_type
//
// What type is obtained when this array is indexed once?
ciType* ciArrayKlass::element_type() {
  if (is_type_array_klass()) {
    return ciType::make(as_type_array_klass()->element_type());
  } else {
    return as_obj_array_klass()->element_klass()->as_klass();
  }
}


// ------------------------------------------------------------------
// ciArrayKlass::base_element_type
//
// What type is obtained when this array is indexed as many times as possible?
ciType* ciArrayKlass::base_element_type() {
  if (is_type_array_klass()) {
    return ciType::make(as_type_array_klass()->element_type());
  } else {
    ciKlass* ek = as_obj_array_klass()->base_element_klass();
    if (ek->is_type_array_klass()) {
      return ciType::make(ek->as_type_array_klass()->element_type());
    }
    return ek;
  }
}


// ------------------------------------------------------------------
// ciArrayKlass::is_leaf_type
bool ciArrayKlass::is_leaf_type() {
  if (is_type_array_klass()) {
    return true;
  } else {
    return as_obj_array_klass()->base_element_klass()->is_leaf_type();
  }
}


// ------------------------------------------------------------------
// ciArrayKlass::base_element_type
//
// What type is obtained when this array is indexed as many times as possible?
ciArrayKlass* ciArrayKlass::make(ciType* element_type) {
  if (element_type->is_primitive_type()) {
    return ciTypeArrayKlass::make(element_type->basic_type());
  } else {
    return ciObjArrayKlass::make(element_type->as_klass());
  }
}
