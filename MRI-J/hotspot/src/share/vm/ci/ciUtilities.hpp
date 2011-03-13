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
#ifndef CIUTILITIES_HPP
#define CIUTILITIES_HPP


#include "globalDefinitions.hpp"

// The following routines and definitions are used internally in the 
// compiler interface.


#define FAM ((UseFreezeAndMelt && ciEnv::current()) ? ((FreezeAndMelt*)ciEnv::current()->freeze_and_melt()) : ((FreezeAndMelt*)NULL))

// Add a ci native entry wrapper?

// Bring the compilation thread into the VM state.
#define VM_ENTRY_MARK_WITH_THREAD           \
  VM_ENTRY_MARK_WITH_THREAD_FAMSAFE         \
  if(FAM) { guarantee(false, "VM_ENTRY_MARK with FAM"); }

#define VM_ENTRY_MARK_WITH_THREAD_FAMSAFE   \
  ThreadInVMfromNative __tiv(thread);       \
  ResetNoHandleMark rnhm;                   \
  HandleMarkCleaner __hm(thread);           \
JavaThread*THREAD=thread;\
  debug_only(VMNativeEntryWrapper __vew;)

#define VM_ENTRY_MARK                       \
  CompilerThread* thread=CompilerThread::current(); \
  VM_ENTRY_MARK_WITH_THREAD

#define VM_ENTRY_MARK_FAMSAFE               \
  CompilerThread* thread=CompilerThread::current(); \
  VM_ENTRY_MARK_WITH_THREAD_FAMSAFE


// Bring the compilation thread into the VM state.  No handle mark.
#define VM_QUICK_ENTRY_MARK                 \
  CompilerThread* thread=CompilerThread::current(); \
  ThreadInVMfromNative __tiv(thread);       \
/*                                          \
 * [TODO] The NoHandleMark line does nothing but declare a function prototype \
 * The NoHandkeMark constructor is NOT executed. If the ()'s are   \
 * removed, causes the NoHandleMark assert to trigger. \
 * debug_only(NoHandleMark __hm();)         \
 */                                         \
JavaThread*THREAD=thread;\
  debug_only(VMNativeEntryWrapper __vew;)


#define EXCEPTION_CONTEXT \
JavaThread*thread=CompilerThread::current();\
JavaThread*THREAD=thread;


#define CURRENT_ENV                         \
  ciEnv::current()

// where current thread is THREAD
#define CURRENT_THREAD_ENV                  \
  ciEnv::current(thread)

#define ASSERT_IN_VM                        \
assert(JavaThread::current()->jvm_locked_by_self(),"must own JVM lock");

#define ASSERT_IN_VM_FAMSAFE                \
  assert(FAM || JavaThread::current()->jvm_locked_by_self(), "must own JVM lock");

#define GUARDED_VM_ENTRY(action)            \
  { JavaThread* thread=JavaThread::current(); \
    if (thread->jvm_locked_by_self()) { action } else { VM_ENTRY_MARK_WITH_THREAD; { action }}}

#define GUARDED_VM_ENTRY_FAMSAFE(action)\
  { JavaThread* thread=JavaThread::current(); \
    if (thread->jvm_locked_by_self()) { action } else { VM_ENTRY_MARK_WITH_THREAD_FAMSAFE; { action }}}

// Redefine this later.
#define KILL_COMPILE_ON_FATAL_(result)           \
  THREAD);                                       \
  if (HAS_PENDING_EXCEPTION) {                   \
    if (PENDING_EXCEPTION->klass() ==            \
        SystemDictionary::threaddeath_klass()) { \
      /* Kill the compilation. */                \
      fatal("unhandled ci exception");           \
      return (result);                           \
    }                                            \
    CLEAR_PENDING_EXCEPTION;                     \
    return (result);                             \
  }                                              \
  (0

#define KILL_COMPILE_ON_ANY                      \
  THREAD);                                       \
  if (HAS_PENDING_EXCEPTION) {                   \
    fatal("unhandled ci exception");             \
    CLEAR_PENDING_EXCEPTION;                     \
  }                                              \
(0


inline const char* bool_to_str(bool b) {
  return ((b) ? "true" : "false");
}

const char* basictype_to_str(BasicType t);
const char  basictype_to_char(BasicType t);

#endif // CIUTILITIES_HPP
