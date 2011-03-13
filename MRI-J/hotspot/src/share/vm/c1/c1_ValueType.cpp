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


#include "c1_ValueType.hpp"
#include "ciNullObject.hpp"
#include "ciArray.hpp"
#include "ciInstance.hpp"
#include "ciInstanceKlass.hpp"

void ValueType::initialize() {
  // Note: Must initialize all types for each compilation
  //       as they are allocated within a ResourceMark!

  // types
ThreadLocals->_voidType=new VoidType();
ThreadLocals->_intType=new IntType();
ThreadLocals->_longType=new LongType();
ThreadLocals->_floatType=new FloatType();
ThreadLocals->_doubleType=new DoubleType();
ThreadLocals->_objectType=new ObjectType();
ThreadLocals->_arrayType=new ArrayType();
ThreadLocals->_instanceType=new InstanceType();
ThreadLocals->_classType=new ClassType();
ThreadLocals->_addressType=new AddressType();
ThreadLocals->_illegalType=new IllegalType();

  // constants
ThreadLocals->_intZero=new IntConstant(0);
ThreadLocals->_intOne=new IntConstant(1);
ThreadLocals->_objectNull=new ObjectConstant(ciNullObject::make());
};


ValueType* ValueType::meet(ValueType* y) const {
  // incomplete & conservative solution for now - fix this!
  assert(tag() == y->tag(), "types must match");
  return base();
}


ValueType* ValueType::join(ValueType* y) const {
  Unimplemented();
  return NULL;
}



jobject ObjectType::encoding() const {
  assert(is_constant(), "must be");
  return constant_value()->encoding();
}

bool ObjectType::is_loaded() const {
  assert(is_constant(), "must be");
  return constant_value()->is_loaded();
}

ciObject* ObjectConstant::constant_value() const                   { return _value; }
ciObject* ArrayConstant::constant_value() const                    { return _value; }
ciObject* InstanceConstant::constant_value() const                 { return _value; }
ciObject* ClassConstant::constant_value() const                    { return _value; }


ValueType* as_ValueType(BasicType type) {
  switch (type) {
case T_VOID:return ThreadLocals->_voidType;
    case T_BYTE   : // fall through
    case T_CHAR   : // fall through
    case T_SHORT  : // fall through
    case T_BOOLEAN: // fall through
case T_INT:return ThreadLocals->_intType;
case T_LONG:return ThreadLocals->_longType;
case T_FLOAT:return ThreadLocals->_floatType;
case T_DOUBLE:return ThreadLocals->_doubleType;
case T_ARRAY:return ThreadLocals->_arrayType;
case T_OBJECT:return ThreadLocals->_objectType;
case T_ADDRESS:return ThreadLocals->_addressType;
case T_ILLEGAL:return ThreadLocals->_illegalType;
  }
  ShouldNotReachHere();
  return ThreadLocals->_illegalType;
}


ValueType* as_ValueType(ciConstant value) {
  switch (value.basic_type()) {
    case T_BYTE   : // fall through
    case T_CHAR   : // fall through
    case T_SHORT  : // fall through
    case T_BOOLEAN: // fall through
    case T_INT    : return new IntConstant   (value.as_int   ());
    case T_LONG   : return new LongConstant  (value.as_long  ());
    case T_FLOAT  : return new FloatConstant (value.as_float ());
    case T_DOUBLE : return new DoubleConstant(value.as_double());
    case T_ARRAY  : // fall through (ciConstant doesn't have an array accessor)
    case T_OBJECT : return new ObjectConstant(value.as_object());
  }
  ShouldNotReachHere();
  return ThreadLocals->_illegalType;
}


BasicType as_BasicType(ValueType* type) {
  switch (type->tag()) {
    case voidTag:    return T_VOID;
    case intTag:     return T_INT;
    case longTag:    return T_LONG;
    case floatTag:   return T_FLOAT;
    case doubleTag:  return T_DOUBLE;
    case objectTag:  return T_OBJECT;
    case addressTag: return T_ADDRESS;
    case illegalTag: return T_ILLEGAL;
  }
  ShouldNotReachHere();
  return T_ILLEGAL;
}
