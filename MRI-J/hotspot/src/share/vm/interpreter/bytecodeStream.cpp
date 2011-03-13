/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "bytecodeStream.hpp"

Bytecodes::Code RawBytecodeStream::raw_next_special(Bytecodes::Code code) {
  assert(!is_last_bytecode(), "should have been checked");
  // set next bytecode position
  address bcp  = RawBytecodeStream::bcp();
  int l = Bytecodes::raw_special_length_at(bcp);
  if (l <= 0 || (_bci + l) > _end_bci) {
    code = Bytecodes::_illegal;
  } else {
    _next_bci += l;
    assert(_bci < _next_bci, "length must be > 0");
    // set attributes
    _is_wide = false;
    // check for special (uncommon) cases
    if (code == Bytecodes::_wide) {
      code = (Bytecodes::Code)bcp[1];
      _is_wide = true;
    }
  }
  _code = code;
  return code;
}
