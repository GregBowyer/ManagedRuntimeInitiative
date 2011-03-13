/*
 * Copyright 1999-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CICONSTANT_HPP
#define CICONSTANT_HPP


#include "ciEnv.hpp"
#include "ciUtilities.hpp"
#include "freezeAndMelt.hpp"

// ciConstant
//
// This class represents a constant value.
class ciConstant:public ResourceObj{
private:
  friend class ciEnv;
  friend class ciField;

  BasicType _type;
  union {
    jint      _int;
    jlong     _long;
    jint      _long_half[2];
    jfloat    _float;
    jdouble   _double;
    ciObject* _object;
  } _value;

  // Implementation of the print method.
void print_impl(outputStream*out)const;

public:

  ciConstant() {
    _type = T_ILLEGAL; _value._long = -1;
  }
  ciConstant(BasicType type, jint value) {
    assert(type != T_LONG && type != T_DOUBLE && type != T_FLOAT,
           "using the wrong ciConstant constructor");
    _type = type; _value._int = value;
  }
  ciConstant(jlong value) { 
    _type = T_LONG; _value._long = value;
  }
  ciConstant(jfloat value) { 
    _type = T_FLOAT; _value._float = value;
  }
  ciConstant(jdouble value) { 
    _type = T_DOUBLE; _value._double = value;
  }
  ciConstant(BasicType type, ciObject* p) { 
    _type = type; _value._object = p;
  }
  ciConstant(FAMPtr old_cic) { 
    FAM->mapNewToOldPtr(this, old_cic);
    _type = (BasicType)FAM->getInt("((struct ciConstant*)%p)->_type", old_cic);

    switch(_type) {
      case T_BOOLEAN:
      case T_CHAR:
      case T_INT:    _value._int    =         FAM->getInt ("((struct ciConstant*)%p)->_value._int", old_cic);  break;
      case T_LONG:   _value._long   =         FAM->getLong("((struct ciConstant*)%p)->_value._long", old_cic); break;
      case T_FLOAT:  _value._float  =  (float)FAM->getLong("(long)(((struct ciConstant*)%p)->_value._float)", old_cic); break;
      case T_DOUBLE: _value._double = (double)FAM->getLong("(long)(((struct ciConstant*)%p)->_value._double)", old_cic); break;
      case T_OBJECT:
      case T_ARRAY: {
        FAMPtr old_obj = FAM->getOldPtr("((struct ciConstant*)%p)->_value._object", old_cic);
        _value._object = (ciObject*)FAM->getNewFromOldPtr(old_obj);
        break;
      }
      case T_ILLEGAL:
        _value._long = -1;
        break;
      default:
        ShouldNotReachHere();
    }
  }

  BasicType basic_type() const { return _type; }

  jboolean  as_boolean() {
    assert(basic_type() == T_BOOLEAN, "wrong type");
    return (jboolean)_value._int;
  }
  jchar     as_char() {
    assert(basic_type() == T_CHAR, "wrong type");
    return (jchar)_value._int;
  }
  jbyte     as_byte() {
    assert(basic_type() == T_BYTE, "wrong type");
    return (jbyte)_value._int;
  }
  jshort    as_short() {
    assert(basic_type() == T_SHORT, "wrong type");
    return (jshort)_value._int;
  }
  jint      as_int() {
    assert(basic_type() == T_BOOLEAN || basic_type() == T_CHAR  ||
           basic_type() == T_BYTE    || basic_type() == T_SHORT ||
           basic_type() == T_INT, "wrong type");
    return _value._int;
  }
  jlong     as_long() {
    assert(basic_type() == T_LONG, "wrong type");
    return _value._long;
  }
  jfloat    as_float() {
    assert(basic_type() == T_FLOAT, "wrong type");
    return _value._float;
  }
  jdouble   as_double() {
    assert(basic_type() == T_DOUBLE, "wrong type");
    return _value._double;
  }
  ciObject* as_object() const {
    assert(basic_type() == T_OBJECT || basic_type() == T_ARRAY, "wrong type");
    return _value._object;
  }

  // Debugging output
  void print();
};

#endif // CICONSTANT_HPP
