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


#include "collectedHeap.hpp"
#include "methodOop.hpp"
#include "ostream.hpp"
#include "privilegedStack.hpp"
#include "vframe.hpp"

#include "frame.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"

void PrivilegedElement::initialize(const vframe *const vf, oop context, PrivilegedElement* next, TRAPS) {
methodOop method=vf->method();
  _klass                = klassRef(method->method_holder());
  _privileged_context   = objectRef(context);
#ifdef CHECK_UNHANDLED_OOPS
  THREAD->allow_unhandled_oop(&_klass);
  THREAD->allow_unhandled_oop(&_privileged_context);
#endif // CHECK_UNHANDLED_OOPS
  _frame_id             = vf->get_frame().id();
  _next                 = next;
assert(_privileged_context.is_null()||_privileged_context.as_oop()->is_oop(),"must be an oop");
  assert(protection_domain() == NULL || protection_domain()->is_oop(), "must be an oop");
}

void PrivilegedElement::oops_do(OopClosure* f) {
  PrivilegedElement *cur = this;
  do {    
    f->do_oop((objectRef*) &cur->_klass);
    f->do_oop((objectRef*) &cur->_privileged_context);
    cur = cur->_next;
  } while(cur != NULL);    
}

//-------------------------------------------------------------------------------
#ifndef PRODUCT

void PrivilegedElement::print_on(outputStream* st) const {
st->print("   "PTR_FORMAT,_frame_id);
_klass.as_klassOop()->print_value_on(st);
  if (protection_domain() != NULL) {
    st->print("   ");
    protection_domain()->print_value_on(st);
  }
  st->cr();
}

bool PrivilegedElement::contains(address addr) {
  PrivilegedElement *e = (PrivilegedElement *)addr;
  if (e >= this && e < this+1) return true;

  if (_next != NULL) {
    return _next->contains(addr);
  } else {
    return false;
  }
}

#endif

