/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C2COMPILER_HPP
#define C2COMPILER_HPP


#include "abstractCompiler.hpp"

class C2Compiler : public AbstractCompiler {
private:

  static void initialize_runtime();

public:
  // Name
const char*name()const{return"HotSpot Server Compiler";}

  static volatile int _runtimes;

  // Customization
  
  void initialize();

  bool is_c2_compiler() const              { return true; }

  // Compilation entry point for methods
  void compile_method(ciEnv* env,
                      ciMethod* target,
int entry_bci,
                      bool retry_compile);
  
  // sentinel value used to trigger backtracking in compile_method().
  static const char* retry_no_subsuming_loads();

  // Print compilation timers and statistics
void print_timers()const;
};

#endif // C2COMPILER_HPP
