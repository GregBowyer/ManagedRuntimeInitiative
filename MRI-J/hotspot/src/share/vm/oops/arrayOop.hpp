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
#ifndef ARRAYOOP_HPP
#define ARRAYOOP_HPP


#include "oop.hpp"

// arrayOopDesc is the abstract baseclass for all arrays.

class arrayOopDesc : public oopDesc {
 private:
jint _length;//number of elements in the array
  jint _pad;    // if sizeof(element) < 64bits holds array data,
                // for non-oop arrays holds padding,
                // for oop arrays it holds the klass ID of elements (see objArrayOop.hpp)
 protected:
  // Protected access to pad information for sub-class use
static int pad_offset_in_bytes(){return offset_of(arrayOopDesc,_pad);}
  void set_pad(jint x)             { _pad = x; }
 public:
  static bool has_ekid   (BasicType type) { return type==T_OBJECT||type==T_ARRAY; }
  static bool has_padding(BasicType type) { return type==T_DOUBLE||type==T_LONG||has_ekid(type); }

  // Interpreter/Compiler offsets
  static int length_offset_in_bytes()             { return offset_of(arrayOopDesc, _length); }
static int base_offset_in_bytes(BasicType type){return header_size_in_bytes(type);}

  // Returns the address of the first element.
  void* base(BasicType type) const              { return (void*) (((intptr_t) this) + base_offset_in_bytes(type)); }

  // Tells whether index is within bounds.
  bool is_within_bounds(int index) const	{ return 0 <= index && index < length(); }

  // Accessors for instance variable
  int length() const				{ return _length;   }
  void set_length(int length)			{ _length = length; }
  jint get_pad() const                          { return _pad;      }

  // Header size computation. 
  // Should only be called with constants as argument (will not constant fold otherwise)
static int header_size_in_bytes(BasicType type){
    return sizeof(oopDesc)+sizeof(jint)+(has_padding(type)?sizeof(jint):0);
  }
  // Popular common sizes that want to be optimized
  static int header_size_T_INT()   { return sizeof(oopDesc)+sizeof(jint); }
  static int header_size_T_BYTE()  { return sizeof(oopDesc)+sizeof(jint); }
  static int header_size_T_OBJECT(){ return sizeof(oopDesc)+sizeof(jint)+sizeof(jint); }

  // Return the maximum length of an array of BasicType.  The length can passed
  // to typeArrayOop::object_size(scale, length, header_size) without causing an
  // overflow. We substract an extra 2*wordSize to guard against double word
  // alignments. It gets the scale from the type2aelembytes array.
  static int32_t max_array_length(BasicType type) { 
    assert(type >= 0 && type < T_CONFLICT, "wrong type");
    assert(type2aelembytes[type] != 0, "wrong type");
const int bytes_per_element=type2aelembytes[type];
    return (((unsigned)1 << (BitsPerInt-1)) - header_size_in_bytes(type) - 2*HeapWordSize)
      / bytes_per_element;
  }

};
#endif // ARRAYOOP_HPP
