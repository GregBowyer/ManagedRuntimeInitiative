// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

#include "gpgc_heap.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_traps.hpp"
#include "thread.hpp"

#include "gpgc_pageInfo.inline.hpp"

// Initialize GPGC_NewCollector marking before each GC cycle.
void GPGC_NewCollector::marking_init() {
  // During marking, we want to enable GCLdValueTr traps, which will catch unremapped
  // heapRefs to objects that have been relocated.
Thread*current=Thread::current();
  assert0(current->is_GenPauselessGC_thread());

  current->set_gc_mode(false);
current->set_gcthread_lvb_trap_vector();
  GPGC_Heap::new_gc_task_manager()->request_new_gc_mode(false);
}


// Cleanup marking at the end of each mark-remap phase.
void GPGC_NewCollector::marking_cleanup() {
}
