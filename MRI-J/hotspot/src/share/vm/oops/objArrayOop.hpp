/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OBJARRAYOOP_HPP
#define OBJARRAYOOP_HPP

#include "arrayOop.hpp"
#include "lvb.hpp"
#include "refsHierarchy_pd.hpp"

// An objArrayOop is an array containing oops.
// Evaluating "String arg[10]" will create an objArrayOop.

class objArrayOopDesc : public arrayOopDesc {
 public:
  // Accessing
oop obj_at(int index)const{
objectRef*ref_addr=obj_at_addr(index);
                                          return lvb_ref(ref_addr).as_oop();
                                        }
  objectRef ref_at(int index) const     { return lvb_ref(obj_at_addr(index)); }
  void obj_at_put(int index, oop value) { ref_store(objectRef(this), obj_at_addr(index), objectRef(value)); }
  void ref_at_put(int index, objectRef ref) { ref_store(objectRef(this), obj_at_addr(index), ref); }
  static void ref_at_put(objArrayRef thsi,int index, objectRef ref) { ref_store(thsi, thsi.as_objArrayOop()->obj_at_addr(index), ref); }
  // Returns the address of the first element.
  objectRef* base() const               { return (objectRef*) arrayOopDesc::base(T_OBJECT); }

  // Interpreter/Compiler offsets
  static int ekid_offset_in_bytes()     { return pad_offset_in_bytes(); }

  // Accessores for instance variable
  unsigned int ekid() const                     { return get_pad(); }
  void set_ekid(int ekid)                       { set_pad(ekid);    }

  // Sizing
static int header_size(){return arrayOopDesc::header_size_T_OBJECT()>>LogHeapWordSize;}
static size_t object_size(int length){return header_size()+length;}
size_t object_size(){return object_size(length());}

  // Returns the address of the index'th element
  objectRef* obj_at_addr(int index) const {
    assert(is_within_bounds(index), "index out of bounds");
    return &base()[index];
  }
};

#endif // OBJARRAYOOP_HPP
