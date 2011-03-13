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
#ifndef ABSTRACTCOMPILER_HPP
#define ABSTRACTCOMPILER_HPP



#include "allocation.hpp"

typedef void (*initializer)(void);

class ciEnv;
class ciMethod;
class AbstractCompiler : public CHeapObj {
 private:
bool _is_initialized;

 protected:
  // Used for tracking global state of compiler runtime initialization
  enum { uninitialized, initializing, initialized };

  // This method will call the initialization method "f" once (per compiler class/subclass)
  // and do so without holding any locks
  void initialize_runtimes(initializer f, volatile int* state);

 public:
  AbstractCompiler() : _is_initialized(false)    {}

  // Name of this compiler
virtual const char*name()const=0;

  // Missing feature tests
  virtual bool supports_osr   ()                 { return true; } 

  // Customization
  void mark_initialized()                     { _is_initialized = true; }
bool is_initialized()const{return _is_initialized;}

  virtual void initialize()                 = 0;

  virtual bool is_c1_compiler() const         { return false; }
  virtual bool is_c2_compiler() const         { return false; }

  // Compilation entry point for methods
  virtual void compile_method(ciEnv* env,
			      ciMethod* target,
int entry_bci,
                              bool retrying_compile) = 0;
};

#endif // ABSTRACTCOMPILER_HPP
