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


#include "abstractCompiler.hpp"
#include "c2compiler.hpp"
#include "ciEnv.hpp"
#include "compile.hpp"
#include "interfaceSupport.hpp"
#include "node.hpp"
#include "vmTags.hpp"

volatile int C2Compiler::_runtimes = uninitialized;

const char* C2Compiler::retry_no_subsuming_loads() {
  return "retry without subsuming loads";
}
void C2Compiler::initialize_runtime() {

  // Check assumptions used while running ADLC
  Compile::adlc_verification();

  DEBUG_ONLY( Node::init_NodeProperty(); )

  Compile::pd_compiler2_init();

}


void C2Compiler::initialize() {

  // This method can only be called once per C2Compiler object
  // The first compiler thread that gets here will initialize the
  // small amount of global state (and runtime stubs) that c2 needs.

  // There is a race possible once at startup and then we're fine

  // Note that this is being called from a compiler thread not the
  // main startup thread.

  if (_runtimes != initialized) {
    initialize_runtimes( initialize_runtime, &_runtimes);
  }

  mark_initialized();
}

void C2Compiler::compile_method(ciEnv* env,
                                ciMethod* target,
int entry_bci,
                                bool retrying_compile) {
  VMTagMark vmt(Thread::current(), VM_C2Compiler_tag);
  GrowableArray<const ciInstanceKlass*> *ciks = NULL;
  GrowableArray<const ciMethod       *> *cms  = NULL;

  // Attempt to compile while subsuming loads into machine instructions.
Compile C(env,this,target,entry_bci,!retrying_compile/*subsume_loads*/,NULL,NULL);

if(!C.failure_reason())return;//worked

  env->record_failure(C.failure_reason(), C.failure_retry_compile(),C.failure_reason_is(retry_no_subsuming_loads()));
}


void C2Compiler::print_timers() const {
  // do nothing
}
