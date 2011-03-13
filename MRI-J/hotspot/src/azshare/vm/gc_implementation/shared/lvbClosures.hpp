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
#ifndef LVBCLOSURES_HPP
#define LVBCLOSURES_HPP


#include "iterator.hpp"
#include "lvb.hpp"

// This is a specialized oop closure with a macro generated oop_oop_iterate body
// to avoid virtual calls. This is necessary since OopClosure::do_header is
// non-virtual and we can't iterate over the header since that would attempt to
// modify the Klass in the KlassTable.
class LVB_CloneClosure:public OopClosure{
  private:
objectRef*_from;
objectRef*_to;
  public:
    LVB_CloneClosure(oop from, oop to) { _from=(objectRef*)from; _to=(objectRef*)to; }
    bool do_header()                   { return false; }

    void do_oop(objectRef* o) {
      // We've found an objectRef in the "from" oop.  Apply the LVB, and store the
      // result in the "to" oop:
      int offset = o - _from;

      _to[offset] = POISON_OBJECTREF(lvb_ref(o), o);
    }
void do_oop_nv(objectRef*o){
      do_oop(o);
    }
};
#endif // LVBCLOSURES_HPP
