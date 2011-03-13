/*
 * Copyright 1999 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciFlags.hpp"

#include "ostream.hpp"

// ciFlags
//
// This class represents klass or method flags

// ------------------------------------------------------------------
// ciFlags::print_klass_flags
void ciFlags::print_klass_flags(outputStream*out)const{
  if (is_public()) {
out->print("public");
  } else {
out->print("DEFAULT_ACCESS");
  }

  if (is_final()) {
out->print(",final");
  }
  if (is_super()) {
out->print(",super");
  }
  if (is_interface()) {
out->print(",interface");
  }
  if (is_abstract()) {
out->print(",abstract");
  }
}

// ------------------------------------------------------------------
// ciFlags::print_member_flags
void ciFlags::print_member_flags(outputStream*out){
  if (is_public()) {
out->print("public");
  } else if (is_private()) {
out->print("private");
  } else if (is_protected()) {
out->print("protected");
  } else {
out->print("DEFAULT_ACCESS");
  }

  if (is_static()) {
out->print(",static");
  }
  if (is_final()) {
out->print(",final");
  }
  if (is_synchronized()) {
out->print(",synchronized");
  }
  if (is_volatile()) {
out->print(",volatile");
  }
  if (is_transient()) {
out->print(",transient");
  }
  if (is_native()) {
out->print(",native");
  }
  if (is_abstract()) {
out->print(",abstract");
  }
  if (is_strict()) {
out->print(",strict");
  }
    
}

// ------------------------------------------------------------------
// ciFlags::print
void ciFlags::print(outputStream*out){
out->print(" flags=%x",_flags);
}
