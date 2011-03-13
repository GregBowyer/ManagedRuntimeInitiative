// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

#ifndef C1_THREADLOCAL_HPP
#define C1_THREADLOCAL_HPP

#include "allocation.hpp"
#include "bytecodes.hpp"

class AddressType;
class ArrayType;
class BitMap;
class ClassType;
class DoubleType;
class FloatType;
class IllegalType;
class InstanceType;
class IntConstant;
class IntType;
class Interval;
class LongType;
class NullCheckEliminator;
class ObjectConstant;
class ObjectType;
class RegAlloc;
class VoidType;

class C1ThreadLocals:public CHeapObj{
 public:
  C1ThreadLocals();
  ~C1ThreadLocals();

  RegAlloc* _regAlloc;
NullCheckEliminator*_static_nce;
  int _next_instruction_id;
Interval*_interval_end;

  bool _GB_can_trap[Bytecodes::number_of_java_codes];
  bool _GB_is_async[Bytecodes::number_of_java_codes];
  int  _BB_next_block_id;

  // predefined types
  VoidType*       _voidType;
  IntType*        _intType;
  LongType*       _longType;
  FloatType*      _floatType;
  DoubleType*     _doubleType;
  ObjectType*     _objectType;
  ArrayType*      _arrayType;
  InstanceType*   _instanceType;
  ClassType*      _classType;
  AddressType*    _addressType;
  IllegalType*    _illegalType;
  
  
  // predefined constants
  IntConstant*    _intZero;
  IntConstant*    _intOne;
  ObjectConstant* _objectNull;

  BitMap* _BLI_all_blocks;  // bitmap where all bits for all possible block id's are set

};

#endif // C1_THREADLOCAL_HPP
