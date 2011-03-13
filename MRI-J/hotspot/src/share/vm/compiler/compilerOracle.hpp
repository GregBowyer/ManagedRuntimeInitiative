/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef COMPILERORACLE_HPP
#define COMPILERORACLE_HPP


// CompilerOracle is an interface for turning on and off compilation
// for some methods

#include "allocation.hpp"
#include "handles.hpp"

class symbolHandle;

class CompilerOracle : AllStatic {
 private:
  static bool _quiet;

 public:
  // Reads from file and adds to lists 
  static void parse_from_file();

  // Tells whether we to exclude compilation of method
  static bool should_exclude(methodHandle method, bool& quietly);

  // Tells whether we want to inline this method
  static bool should_inline(methodHandle method);

  // Tells whether we want to disallow inlining of this method
  static bool should_not_inline(methodHandle method);

  // Tells whether we should disable inlining inside this method
static bool should_disable_inlining(methodHandle method);

  // Tells whether to break at end of codegen when compiling method
static bool should_break_c1_after_codegen(methodHandle method);
static bool should_break_c2_after_codegen(methodHandle method);

  // Tells whether to disable loopopts when compiling method
static bool should_disable_loopopts(methodHandle method);

  // Tells whether we should print the IR for this method
static bool should_print_ir(methodHandle method);

  // Tells whether we should print the assembly for this method
  static bool should_print(methodHandle method);

  // Tells whether we should log the compilation data for this method
  static bool should_log(methodHandle method);

  // Tells whether to break when compiling method
  static bool should_break_at(methodHandle method);

  // Check to see if this method has option set for it
  static bool has_option_string(methodHandle method, const char * option);

  // Reads from string instead of file
  static void parse_from_string(const char* command_string, void (*parser)(char*));

  static void parse_from_line(char* line);
  static void parse_compile_only(char * line);

  // For updating the oracle file
  static void append_comment_to_file(const char* message);
  static void append_exclude_to_file(methodHandle method);
};

#endif // COMPILERORACLE_HPP
