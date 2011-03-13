/*
 * Copyright 1997-2002 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef KLASSOOP_HPP
#define KLASSOOP_HPP


#include "oop.hpp"

// A klassOop is the C++ equivalent of a Java class.
// Part of a klassOopDesc is a Klass which handle the 
// dispatching for the C++ method calls.

//  klassOop object layout:
//    [header     ]
//    [KLASS      ]

class klassOopDesc : public oopDesc {
 public:
  // size operation
  static int header_size()                       { return sizeof(klassOopDesc)/HeapWordSize; }

  // support for code generation
  static int klass_part_offset_in_bytes()        { return sizeof(klassOopDesc); }

  // returns the Klass part containing dispatching behavior
  Klass* klass_part()                            { return (Klass*)((address)this + klass_part_offset_in_bytes()); }
};

#endif // KLASSOOP_HPP
