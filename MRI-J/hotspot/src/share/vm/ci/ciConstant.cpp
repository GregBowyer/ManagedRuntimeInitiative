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


#include "ciConstant.hpp"

#include "ostream.hpp"

// ciConstant
//
// This class represents a constant value.

// ------------------------------------------------------------------
// ciConstant::print
void ciConstant::print() {
  tty->print("<ciConstant type=%s value=",
	     basictype_to_str(basic_type()));
  switch (basic_type()) {
  case T_BOOLEAN:
    tty->print("%s", bool_to_str(_value._int == 0));
    break;
  case T_CHAR:
  case T_BYTE:
  case T_SHORT:
  case T_INT:
    tty->print("%d", _value._int);
    break;
  case T_LONG:
tty->print("%lld",_value._long);
    break;
  case T_FLOAT:
    tty->print("%f", _value._float);
    break;
  case T_DOUBLE:
    tty->print("%lf", _value._double);
    break;
  case T_OBJECT:
  case T_ARRAY:
_value._object->print(tty);
    break;
  default:
    tty->print("ILLEGAL");
    break;
  }
  tty->print(">");
}
