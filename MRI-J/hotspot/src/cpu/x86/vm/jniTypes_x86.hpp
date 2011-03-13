/*
 * Copyright 1998-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef JNITYPES_PD_HPP
#define JNITYPES_PD_HPP

#include "allocation.hpp"
#include "frame.hpp"

// This file holds platform-dependent routines used to write primitive jni 
// types to the array of arguments passed into JavaCalls::call

class JNITypes : AllStatic {
  // These functions write a java primitive type (in native format)
  // to a java stack slot array to be passed as an argument to JavaCalls:calls.
  // I.e., they are functionally 'push' operations if they have a 'pos'
  // formal parameter.  Note that jlong's and jdouble's are written
  // _in reverse_ of the order in which they appear in the interpreter
  // stack.  This is because call stubs (see stubGenerator_sparc.cpp)
  // reverse the argument list constructed by JavaCallArguments (see
  // javaCalls.hpp).

#ifndef LONGS_IN_ONE_ENTRY
private:
  // Helper routines.
  static inline void    put_int2 (jint *from, jint *to)	          { *(jint *)(to++) = from[0];
*(jint*)(to)=from[1];}
  static inline void    put_int2 (jint *from, jint *to, int& pos) { put_int2 (from, (jint *)((intptr_t *)to + pos)); pos += 2; }
  static inline void    put_int2r(jint *from, jint *to)	          { *(jint *)(to++) = from[1];
                                                                    *(jint *)(to  ) = from[0]; }
static inline void put_int2r(jint*from,jint*to,int&pos){put_int2r(from,(jint*)((intptr_t*)to+pos));pos+=2;}
#endif

public:
  // Ints are stored in native format in one JavaCallArgument slot at *to.
//Ints are stored sign-extended to 64-bits in the
//full JavaCallArgument slot.
static inline void put_int(jint from,intptr_t*to,int&pos){*(jlong*)(to+pos++)=(jlong)from;}

//Longs are stored in native format in one JavaCallArgument slot at *(to+1).
  // We also need to tag longs with double_slot_primitive_type_empty_slot_id
  static inline void    put_long(jlong  from, intptr_t *to, int& pos)	{
    // The stack tag is actually only on the upper 32 bits of the stack slot. We zero the lower bits as well.
    jint* halfs = (jint *)(to + pos++);
    halfs[0] = 0;
    halfs[1] = (jint)frame::double_slot_primitive_type_empty_slot_id;

    *(jlong *)(to + pos++) =  from;
  }

  // Oops are stored in native format in one JavaCallArgument slot at *to.
  static inline void    put_obj(oop  from, intptr_t *to, int& pos)	{ *(oop *)(to + pos++) =  from; }

  static inline void    put_float(jfloat  from, intptr_t *to, int& pos) { *(jlong *)(to + pos++) =  (jlong)(*(unsigned int*)&from); }

  // Doubles are stored in native word format in one JavaCallArgument slot at *(to+1).
  static inline void    put_double(jdouble  from, intptr_t *to, int& pos) {
    // The stack tag is actually only on the upper 32 bits of the stack slot. We zero the lower bits as well.
    jint* halfs = (jint *)(to + pos++);
    halfs[0] = 0;
    halfs[1] = (jint)frame::double_slot_primitive_type_empty_slot_id;

    *(jdouble *)(to + pos++) =  from;
  }

  // The get_xxx routines, on the other hand, actually _do_ fetch
  // java primitive types from the interpreter stack.
  static inline jint    get_int(intptr_t *from)		{ return *(jint *)from; }
static inline jlong get_long(intptr_t*from){return*(jlong*)from;}
  static inline oop     get_obj(intptr_t *from)		{ return *(oop *)from; }
  static inline jfloat  get_float(intptr_t *from)       { return *(jfloat *)from; }
static inline jdouble get_double(intptr_t*from){return*(jdouble*)from;}
};

#endif // JNITYPES_PD_HPP
