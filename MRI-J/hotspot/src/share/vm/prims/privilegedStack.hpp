/*
 * Copyright 1997-2000 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PRIVILEGEDSTACK_HPP
#define PRIVILEGEDSTACK_HPP


#include "allocation.hpp"
#include "klass.hpp"
#include "instanceKlass.hpp"

class vframe;

class PrivilegedElement VALUE_OBJ_CLASS_SPEC {
 private:  
  klassRef  _klass;                // klass for method 
  objectRef _privileged_context;   // context for operation  
  intptr_t* _frame_id;             // location on stack
  PrivilegedElement* _next;        // Link to next one on stack
 public:
void initialize(const vframe*const vf,oop context,PrivilegedElement*next,TRAPS);
  void oops_do(OopClosure* f);    
  intptr_t* frame_id() const       { return _frame_id; }
oop privileged_context()const{return _privileged_context.as_oop();}
oop class_loader()const{return instanceKlass::cast(_klass.as_klassOop())->class_loader();}
oop protection_domain()const{return instanceKlass::cast(_klass.as_klassOop())->protection_domain();}
  PrivilegedElement *next() const  { return _next; }

  // debugging (used for find)
  void print_on(outputStream* st) const   PRODUCT_RETURN;
  bool contains(address addr)             PRODUCT_RETURN0;
};

#endif // PRIVILEGEDSTACK_HPP
