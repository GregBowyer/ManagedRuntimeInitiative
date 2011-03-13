/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "init.hpp"
#include "safepoint.hpp"
#include "sharedRuntime.hpp"

// Early "static" initializations
void universe_static_init(); 
void systemdictionary_static_init();
void thread_static_init();
void jvmtiRedefineClasses_static_init();
void methodComparator_static_init();

// Initialization done by VM thread in vm_init_globals()
void check_ThreadShadow();
void check_basic_types();
void eventlog_init();
void mutex_init();
void chunkpool_init();
void perfMemory_init();
void oopTable_init();
void monitor_trace_init();

// Initialization done by Java thread in init_globals()
void statistics_init();
void management_init();
void proxy_rlimits_initialize();
void bytecodes_init();
void classLoader_init();
void codeCache_init();
void VM_Version_init();
void JDK_Version_init();
void GenPauselessHeap_init();
void stubRoutines_init1();
jint universe_init();  // dependent on codeCache_init and stubRoutines_init 
void interpreter_init();  // before any methods loaded
void marksweep_init();
void accessFlags_init();
void templateTable_init();
void InterfaceSupport_init();
void universe2_init();  // dependent on codeCache_init and stubRoutines_init
void referenceProcessor_init();
void jni_handles_init();
void sba_init();

void compilerOracle_init();



// Initialization after compiler initialization
bool universe_post_init();  // must happen after compiler_init
void javaClasses_init();  // must happen after vtable initialization
void stubRoutines_init2(); // note: StubRoutines need 2-phase init 
void genPauselessThreads_init(); // must happen after trap table initialized
                                 // in stubRoutines_init2
void heapIterator_init();
void memoryService_init(); // must happen after GC threads are initialized

// Do not disable thread-local-storage, as it is important for some
// JNI/JVM/JVMTI functions and signal handlers to work properly
// during VM shutdown
void oopTable_exit();
void perfMemory_exit();
void ostream_exit();

void yellowZoneGlobalParamSet();  // Sets yellow zone size to StackYellowPages * stack extension size.

void static_init() {
  universe_static_init();
  systemdictionary_static_init();
  thread_static_init();
  jvmtiRedefineClasses_static_init();
  methodComparator_static_init();
}


void vm_init_globals() {
  check_ThreadShadow();
  check_basic_types();
  mutex_init();
  chunkpool_init();
  perfMemory_init();
  oopTable_init();
}


jint init_globals() {  
  statistics_init();
  management_init();
  proxy_rlimits_initialize();
  bytecodes_init();
  classLoader_init();
  codeCache_init();
  VM_Version_init();
  JDK_Version_init();
  GenPauselessHeap_init();
  stubRoutines_init1();
jint status=universe_init();
  if (status != JNI_OK)
    return status;

  interpreter_init();  // before any methods loaded
  marksweep_init();
  accessFlags_init();
  templateTable_init();
  InterfaceSupport_init();
  universe2_init();  // dependent on codeCache_init and stubRoutines_init
  referenceProcessor_init();
  jni_handles_init();
  sba_init();

  compilerOracle_init();

  stubRoutines_init2(); // note: StubRoutines need 2-phase init 
  if (!universe_post_init()) { 
    return JNI_ERR;
  }
  javaClasses_init();  // must happen after vtable initialization
  genPauselessThreads_init(); // must happen after trap table initialized
                              // in stubRoutines_init2
  heapIterator_init();
  memoryService_init(); // must happen after GC threads are initialized

  return JNI_OK;
}


void exit_globals() {
  static bool destructorsCalled = false;
  if (!destructorsCalled) {
    destructorsCalled = true;
    oopTable_exit();
    perfMemory_exit();
    if (PrintSafepointStatistics) {
      // Print the collected safepoint statistics.
      SafepointSynchronize::print_stat_on_exit();
    }
    ostream_exit();
  }
}


static bool _init_completed = false;

bool is_init_completed() {
  return _init_completed;
}


void set_init_completed() {
  _init_completed = true;
}
