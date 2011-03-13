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


#include "ciEnv.hpp"
#include "ciTypeArrayKlass.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "handles.hpp"
#include "interfaceSupport.hpp"
#include "typeArrayKlass.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"

// ciTypeArrayKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part in a TypeArrayKlass.

// ------------------------------------------------------------------
// ciTypeArrayKlass::ciTypeArrayKlass
ciTypeArrayKlass::ciTypeArrayKlass(KlassHandle h_k) : ciArrayKlass(h_k) {
  assert(get_Klass()->oop_is_typeArray(), "wrong type");
_cached_element_type=get_typeArrayKlass()->element_type();
}

ciTypeArrayKlass::ciTypeArrayKlass(FAMPtr old_citak):ciArrayKlass(old_citak){
  FAM->mapNewToOldPtr(this, old_citak);
  _cached_element_type = (BasicType)FAM->getInt("((struct ciTypeArrayKlass*)%p)->_cached_element_type", old_citak);
}

// ------------------------------------------------------------------
// ciTypeArrayKlass::make_impl
//
// Implementation of make.
ciTypeArrayKlass* ciTypeArrayKlass::make_impl(BasicType t) {
  klassOop k = Universe::typeArrayKlassObj(t);
  return CURRENT_ENV->get_object(k)->as_type_array_klass();
}

// ------------------------------------------------------------------
// ciTypeArrayKlass::make
//
// Make an array klass corresponding to the specified primitive type.
ciTypeArrayKlass* ciTypeArrayKlass::make(BasicType t) {
  if (FAM) {
    ciTypeArrayKlass* citak = ciEnv::current()->_factory->get_ciTypeArrayKlass(t);  
    guarantee(citak, "Could not find ciTypeArrayKlass");
    return citak;
  }
  GUARDED_VM_ENTRY(return make_impl(t);)
}

