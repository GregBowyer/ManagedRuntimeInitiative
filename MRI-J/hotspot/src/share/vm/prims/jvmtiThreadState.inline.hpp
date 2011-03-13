/*
 * Copyright 2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifdef USE_PRAGMA_IDENT_HDR

#endif


#ifndef JVMTITHREADSTATE_INLINE_HPP
#define JVMTITHREADSTATE_INLINE_HPP

#include "jvmtiEnvThreadState.hpp"
#include "jvmtiThreadState.hpp"

// JvmtiEnvThreadStateIterator implementation

inline JvmtiEnvThreadStateIterator::JvmtiEnvThreadStateIterator(JvmtiThreadState* thread_state) {
  state = thread_state;
  Thread::current()->entering_jvmti_env_iteration(); 
}

inline JvmtiEnvThreadStateIterator::~JvmtiEnvThreadStateIterator() {
  Thread::current()->leaving_jvmti_env_iteration();
}

inline JvmtiEnvThreadState* JvmtiEnvThreadStateIterator::first() {
  return state->head_env_thread_state();
}

inline JvmtiEnvThreadState* JvmtiEnvThreadStateIterator::next(JvmtiEnvThreadState* ets) {
  return ets->next();
}
  
// JvmtiThreadState implementation

JvmtiEnvThreadState* JvmtiThreadState::env_thread_state(JvmtiEnvBase *env) {
  JvmtiEnvThreadStateIterator it(this); 
  for (JvmtiEnvThreadState* ets = it.first(); ets != NULL; ets = it.next(ets)) {
    if ((JvmtiEnvBase*)(ets->get_env()) == env) {
      return ets; 
    }
  }
  return NULL;
}

JvmtiEnvThreadState* JvmtiThreadState::head_env_thread_state() { 
  return _head_env_thread_state; 
}

void JvmtiThreadState::set_head_env_thread_state(JvmtiEnvThreadState* ets) {
  _head_env_thread_state = ets; 
}

#endif  // JVMTITHREADSTATE_INLINE_HPP
