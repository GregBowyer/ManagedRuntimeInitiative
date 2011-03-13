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



#include "heapIterator.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_space.hpp"
#include "jniHandles.hpp"
#include "jvmtiEnvBase.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

// Machine generated file; must come last:
#include "jvmtiEnv.hpp"

// Azul - Make the heap iteration multi-threaded
//

PGCTaskManager* HeapIterator::_heap_iterator_task_manager  = NULL;
bool            HeapIterator::_should_initialize           = false;

HeapIteratorThread::HeapIteratorThread(uint id) : _id(id) {
  bool created = os::create_thread(this, ttype::jvmti_thread);
  guarantee(created, "failed to create HeapIteratorThread");

set_name("\"Heap Iterator Thread#%d\" ",id);

  os::start_thread(this);
}

void HeapIteratorThread::run(){
assert(UseGenPauselessGC,"HeapIteratorThreads can be used only with [G]PGC");

  this->initialize_thread_local_storage();

  this->set_gc_mode(true);

#ifdef AZ_PROXIED
  proxy_invoke_remote_jvmtiHeapCallbacksUpdater(id());
#else
  //Unimplemented();  // TODO: No idea what I'm supposed to do here when not proxied.
#endif // AZ_PROXIED

  delete this;
}

void HeapIteratorThread::print()const{
tty->print("\"%s\" ",name());
Thread::print();
  tty->cr();
}

void HeapIteratorThread::print_xml_on(xmlBuffer *xb, bool ref) {
  xmlElement te(xb, ref ? "thread_ref" : "thread");
  state_print_xml_on(xb, "Heap Iterator Thread");
}


void HeapIterator::initialize()
{
#ifndef PRODUCT
  // Make sure that there is atleast one heap iterator thread
  if (HeapIteratorTaskThreads < 1 || (!UseGenPauselessGC)) {
    HeapIteratorTaskThreads = 1;
  }
#endif // !PRODUCT

  bool jvmti_env_might_exist = JvmtiEnv::environments_might_exist();

  if (!HeapIterator::should_initialize() && !jvmti_env_might_exist) return;

  // Tell the proxy how many threads can make callbacks
  // The VMThread can also make callbacks. Account for that as well.
  // We need to have number_of_heap_callbacks_completed on the proxy side
  // even for the synchronous calls because the jvmtiObjectReferenceCallback
  // is synchronous even though the other heap iteration callbacks can be
  // asynchronous
  // If there are no jvmti environments, we don't have to initialize the callbacks
#ifdef AZ_PROXIED
  if (jvmti_env_might_exist) {
    proxy_invoke_remote_jvmtiHeapCallbacksInit(HeapIteratorTaskThreads + 1);
  }
#else
  //Unimplemented();  // TODO: No idea what I'm supposed to do here when not proxied.
#endif // AZ_PROXIED

  if (UseGenPauselessGC) {
    _heap_iterator_task_manager = PGCTaskManager::create(HeapIteratorTaskThreads);

    // If there are no jvmti environments, we don't have to create these threads
    if (jvmti_env_might_exist) {
      // For each task thread there is a corresponding thread that processes the
      // updates from the callbacks.
      for (int i = 0; i < (HeapIteratorTaskThreads + 1); i++) {
        HeapIteratorThread* jt = new (ttype::jvmti_thread) HeapIteratorThread(i);
      }
    }
  }
}

void heapIterator_init() {
HeapIterator::initialize();

  if (!UseGenPauselessGC) {
assert(HeapIteratorTaskThreads==1,"HeapIteratorTaskThreads can be set only with [G]PGC");
  }
}

void HeapIterator::heap_iterate(ObjectClosure* closure) {
TraceTime t("Heap Iteration",TraceJVMTIObjectTagging);

  Thread* thread = Thread::current();

guarantee(SafepointSynchronize::is_at_safepoint(),"heap_iterate only at a safepoint");
guarantee(thread->is_VM_thread(),"heap_iterate can be called only by the VMThread");

PGCTaskQueue*q=PGCTaskQueue::create();

  if (UseGenPauselessGC) {
    GPGC_Space::make_heap_iteration_tasks(HeapIteratorTaskThreads, closure, q);

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(_heap_iterator_task_manager, q);
  } else {
    ShouldNotReachHere();
    // TODO: parallelize UseParallelGC and UseSerialGC
  }
}


