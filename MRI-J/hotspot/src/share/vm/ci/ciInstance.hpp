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
#ifndef CIINSTANCE_HPP
#define CIINSTANCE_HPP


#include "ciConstant.hpp"
#include "ciObject.hpp"
#include "handles.hpp"

// ciInstance
//
// This class represents an instanceOop in the HotSpot virtual
// machine.  This is an oop which corresponds to a non-array
// instance of java.lang.Object.
class ciInstance : public ciObject {
  CI_PACKAGE_ACCESS
  friend class ciEnv;

protected:
ciInstance(instanceHandle h_i);
ciInstance(ciKlass*klass);
ciInstance(FAMPtr old_cii);

  instanceOop get_instanceOop() { return (instanceOop)get_oop(); }

  const char* type_string() { return "ciInstance"; }

  void print_impl(outputStream *out) const;

public:
  virtual void fixupFAMPointers();

  // If this object is a java mirror, return the corresponding type.
  // Otherwise, return NULL.
  // (Remember that a java mirror is an instance of java.lang.Class.)
  ciType* java_mirror_type();

  // What kind of ciObject is this?
  bool is_instance()     { return true; }
  bool is_java_object()  { return true; }

  // Constant value of a field.
  ciConstant field_value(ciField* field);

  // Constant value of a field at the specified offset.
  ciConstant field_value_by_offset(int field_offset);

private:
ciType*_java_mirror_type;
  GrowableArray<ciConstant*>* _field_offset_to_constant; // offset[int] -> constant[ciConstant*]
};

#endif // CIINSTANCE_HPP
