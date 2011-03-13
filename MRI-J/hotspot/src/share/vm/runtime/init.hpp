/*
 * Copyright 1997-2004 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef INIT_HPP
#define INIT_HPP

#include "jni.h"

// init_globals replaces C++ global objects so we can use the standard linker
// to link Delta (which is at least twice as fast as using the GNU C++ linker). 
// Also, init.c gives explicit control over the sequence of initialization.

// Programming convention: instead of using a global object (e,g, "Foo foo;"), 
// use "Foo* foo;", create a function init_foo() in foo.c, and add a call
// to init_foo in init.cpp.

void static_init();      // call "static" constructors too complex for static constructors
jint init_globals();     // call constructors at startup (main Java thread)
void vm_init_globals();  // call constructors at startup (VM thread)
void exit_globals();     // call destructors before exit

bool is_init_completed();     // returns true when bootstrapping has completed
void set_init_completed();    // set basic init to completed

#endif // INIT_HPP
