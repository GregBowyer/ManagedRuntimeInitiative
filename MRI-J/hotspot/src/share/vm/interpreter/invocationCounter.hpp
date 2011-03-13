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
#ifndef INVOCATIONCOUNTER_HPP
#define INVOCATIONCOUNTER_HPP


#include "allocation.hpp"
#include "sizes.hpp"

// InvocationCounters are used to trigger actions when a limit (threshold) is reached.
// The _newcounter field is used to trigger overflow events and _oldcounter is used
// to retain information about previous counts.  Incrementing an InvocationCounter 
// should only touch the _newcounter field, and _oldcounter should only be changed on a
// reset().

class InvocationCounter VALUE_OBJ_CLASS_SPEC {
 private:
  int _newcounter;
  int _oldcounter;

 public:
  int    count() const             { return _newcounter+_oldcounter; }
  void   increment()               { _newcounter += 1; }
  void   reset()                   { _oldcounter += _newcounter; _newcounter = 0 ; }
  void   reset_to(unsigned int x)  { int delta = x-_newcounter; _oldcounter -= delta; _newcounter += delta;  }
static ByteSize counter_offset(){return byte_offset_of(InvocationCounter,_newcounter);}

  // Printing
  void print(outputStream* out) const;
  void print_short(outputStream* out) const;

  // Miscellaneous
void init();


  // For debugging, and ONLY debugging
  void set(unsigned int newcount, unsigned int oldcount) {
    _newcounter = newcount;
    _oldcounter = oldcount;
  }
};
#endif // INVOCATIONCOUNTER_HPP
