/*
 * Copyright 1999 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciNullObject.hpp"

#include "ciEnv.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"
#include "ostream.hpp"

// ciNullObject
//
// This class represents a null reference.  It can be used
// as a class loader or as the null constant.

ciNullObject::ciNullObject(FAMPtr old_cino) : ciObject(old_cino) {
  FAM->mapNewToOldPtr(this, old_cino);
}

void ciNullObject::fixupFAMPointers() {
  ciObject::fixupFAMPointers();
}

// ------------------------------------------------------------------
// ciNullObject::print_impl
//
// Implementation of the print method.
void ciNullObject::print_impl(outputStream*out)const{
ciObject::print_impl(out);
out->print(" unique");
}

// ------------------------------------------------------------------
// ciNullObject::make
//
// Get the distinguished instance of this class.
ciNullObject* ciNullObject::make() {
  return CURRENT_ENV->_null_object_instance->as_null_object();
}

