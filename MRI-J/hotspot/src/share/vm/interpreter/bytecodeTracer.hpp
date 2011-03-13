/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef BYTECODETRACER_HPP
#define BYTECODETRACER_HPP


// The BytecodeTracer is a helper class used by the interpreter for run-time
// bytecode tracing. If bytecode tracing is turned on, trace() will be called
// for each bytecode.
//
// By specialising the BytecodeClosure, all kinds of bytecode traces can
// be done.

#include "allocation.hpp"
#include "handles.hpp"
// class BytecodeTracer is only used by TraceBytecodes option

class BytecodeClosure;
class BytecodeTracer: AllStatic {
 private:
  static BytecodeClosure* _closure;

 public:
  static BytecodeClosure* std_closure();                        // a printing closure
  static BytecodeClosure* closure()                                                   { return _closure; }
  static void             set_closure(BytecodeClosure* closure) { _closure = closure; }

  static void             trace(methodHandle method, address bcp, uintptr_t tos, uintptr_t tos2, outputStream* st = tty);
  static void             trace(methodHandle method, address bcp, outputStream* st = tty);

  static void             print_one_bytecode(const methodOopDesc *const moop, int bci, outputStream *out);
};

// For each bytecode, a BytecodeClosure's trace() routine will be called.

class BytecodeClosure {
 public:
  virtual void trace(methodHandle method, address bcp, uintptr_t tos, uintptr_t tos2, outputStream* st) = 0;
  virtual void trace(methodHandle method, address bcp, outputStream* st) = 0;
};
#endif // BYTECODETRACER_HPP
