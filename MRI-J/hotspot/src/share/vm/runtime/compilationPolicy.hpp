/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef COMPILATIONPOLICY_HPP
#define COMPILATIONPOLICY_HPP


#include "allocation.hpp"
#include "timer.hpp"
#include "rframe.hpp"

// The CompilationPolicy selects which method (if any) should be compiled.
// It also decides which methods must always be compiled (i.e., are never
// interpreted).

class CompilationPolicy : public CHeapObj {
private:
  static elapsedTimer       _accumulated_time;

  static RFrame* findTopInlinableFrame(RFrame *first, int c12, BufferedLoggerMark *blm );

  static const char* shouldInline   (methodHandle callee, int c12, int caller_cnt, int site_cnt, char *buf, BufferedLoggerMark *blm );
  // positive filter: should send be inlined?  returns NULL (--> yes) or rejection msg 
  static const char* shouldNotInline(methodHandle callee, int c12, char *buf, BufferedLoggerMark *blm);
  // negative filter: should send NOT be inlined?  returns NULL (--> inline) or rejection msg 
  
 public:
  static void method_invocation_event(methodHandle m, int c12, address continue_in_c1);
  static void method_back_branch_event(methodHandle m, int loop_top_bci);

  static bool mustBeCompiled(methodHandle m, int c12);      // m must be compiled before executing it
  static bool canBeCompiled(methodHandle m, int c12);     // m is allowed to be compiled   

static void print_time()PRODUCT_RETURN;
};

#endif // COMPILATIONPOLICY_HPP
