/*
 * Copyright 1998-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef RUNTIME_HPP
#define RUNTIME_HPP

#include "allocation.hpp"
#include "objectRef_pd.hpp"
class TypeFunc;
class TypeAryPtr;

class OptoRuntime : public AllStatic {
 public:
  //
  // Implementation of runtime methods
  // =================================
  
  // Allocate storage for a multi-dimensional arrays
  // Note: needs to be fixed for arbitrary number of dimensions  
  static objectRef multianewarray1_Java(JavaThread *thread, klassRef klass, int len1);
  static objectRef multianewarray2_Java(JavaThread *thread, klassRef klass, int len1, int len2);
  static objectRef multianewarray3_Java(JavaThread *thread, klassRef klass, int len1, int len2, int len3);
  static objectRef multianewarray4_Java(JavaThread *thread, klassRef klass, int len1, int len2, int len3, int len4);
  static objectRef multianewarray5_Java(JavaThread *thread, klassRef klass, int len1, int len2, int len3, int len4, int len5);
  static const TypeFunc* multianewarray_Type(int ndim, const TypeAryPtr *arr);


  // Type functions
  // ======================================================

static const TypeFunc*forward_exception2_Type();
  static const TypeFunc* complete_monitor_exit_Type();
  static const TypeFunc* uncommon_trap_Type();
  static const TypeFunc* Math_D_D_Type();  // sin,cos & friends
  static const TypeFunc* Math_DD_D_Type(); // mod,pow & friends
  static const TypeFunc* modf_Type();
  static const TypeFunc* l2f_Type();
  static const TypeFunc* current_time_millis_Type();

  // arraycopy routine types
  static const TypeFunc* fast_arraycopy_Type(); // bit-blasters
  static const TypeFunc* checkcast_arraycopy_Type();
  static const TypeFunc* generic_arraycopy_Type();
  static const TypeFunc* slow_arraycopy_Type();   // the full routine

  // leaf on stack replacement interpreter accessor types
  static const TypeFunc* osr_end_Type();

  static const TypeFunc* register_finalizer_Type();
};

#endif // RUNTIME_HPP
