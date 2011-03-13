/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "tickProfiler.hpp"
#include "thread.hpp"
#include "mutexLocker.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"

#include "sys/mman.h"


void Thread::self_pc_and_rpc(int depth, uint64_t *result_pc, uint64_t *result_rpc) {
  // These values are not available in optimized X86 builds, because
  // they require an extra register (RBP) for a frame pointer, and
  // registers are rare on X86.
  *result_pc  = 0;
  *result_rpc = 0;
}


// Called once per thread start.
void JavaThread::pd_initialize(){
guarantee(Thread::current()==this,"must pd-init self thread");
  _proxy_jni_environment = 0LL; // We will create it lazily if we need it
}

void JavaThread::pd_destroy(){
}
