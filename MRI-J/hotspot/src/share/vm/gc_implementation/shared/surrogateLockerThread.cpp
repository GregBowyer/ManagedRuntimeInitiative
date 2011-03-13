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


// CopyrightVersion 1.2

#include "gpgc_thread.hpp"
#include "instanceRefKlass.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "mutexLocker.hpp"
#include "safepoint.hpp"
#include "surrogateLockerThread.hpp"
#include "systemDictionary.hpp"
#include "vm_operations.hpp"


SurrogateLockerThread* SurrogateLockerThread::_slt = NULL;


static void _sltLoop(JavaThread* thread, TRAPS) {
  SurrogateLockerThread* slt = (SurrogateLockerThread*)thread;
  slt->loop();
}

SurrogateLockerThread::SurrogateLockerThread() :
  JavaThread(&_sltLoop),
  _buffer(empty)
{}

void SurrogateLockerThread::makeSurrogateLockerThread(TRAPS){
assert(Thread::current()->is_Java_thread(),"Only a JavaThread can create the SurrogateLockerThread");
  assert(_slt == NULL, "SLT already created");
  _slt = SurrogateLockerThread::make(THREAD);
}

SurrogateLockerThread* SurrogateLockerThread::make(TRAPS) {
  klassOop k =
    SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_Thread(),
                                      true, CHECK_NULL);
  instanceKlassHandle klass (THREAD, k);
  instanceHandle thread_oop = klass->allocate_instance_handle(false/*No SBA*/, CHECK_NULL);

const char thread_name[]="Surrogate Locker Thread";
  Handle string = java_lang_String::create_from_str(thread_name, false/*No SBA*/, CHECK_NULL);

  // Initialize thread_oop to put it into the system threadGroup
  Handle thread_group (THREAD, Universe::system_thread_group());
  JavaValue result(T_VOID);
  JavaCalls::call_special(&result, thread_oop,
			  klass,
			  vmSymbolHandles::object_initializer_name(),
			  vmSymbolHandles::threadgroup_string_void_signature(),
			  thread_group,
			  string,
			  CHECK_NULL);

  SurrogateLockerThread* res;
  {
    MutexLockerAllowGC mu(Threads_lock, JavaThread::current());
    res = new (ttype::java_thread) SurrogateLockerThread();

    // At this point it may be possible that no osthread was created for the
    // JavaThread due to lack of memory. We would have to throw an exception
    // in that case. However, since this must work and we do not allow
    // exceptions anyway, check and abort if this fails.
    if (res == NULL || res->osthread() == NULL) {
      vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                    "unable to create new native thread");
    }
    java_lang_Thread::set_thread(thread_oop(), res);
    java_lang_Thread::set_daemon(thread_oop());

    res->set_threadObj(thread_oop());
    Threads::add(res, false);

    res->set_os_priority(SurrogateLockerPriority);

    Thread::start(res);
  }
  os::yield(); // This seems to help with initial start-up of SLT
  return res;
}

void SurrogateLockerThread::manipulatePLL(SLT_msg_type msg) {
assert0(Thread::current()->is_GenPauselessGC_thread());
  // TODO: Implement a generic shutdown mechanism for non-java threads, so we don't have to have per-collector handling here.

  ((GPGC_Thread *)Thread::current())->set_blocked(true);

  {
MutexLocker x(SLT_lock,Thread::current());
    assert(_buffer == empty, "Should be empty");
    assert(msg != empty, "empty message");
    _buffer = msg;
    while (_buffer != empty) {
SLT_lock.notify();
      while (true) {
        bool timedout = SLT_lock.wait_micros(100*1000L, false);
        if (timedout) { 
          // If we timed out, maybe the SLT noticed VM exit before we did.
GPGC_Thread*thread=(GPGC_Thread*)Thread::current();
          if ( GPGC_Thread::should_terminate() || VM_Exit::vm_exited()) {
            SLT_lock.unlock(); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
          }
        } else {
          // We were notified by SLT. If a shutdown is in progress the next 
          // Safepoint will see it and stop there
          break;
        }
      }
    }
  }

  ((GPGC_Thread *)Thread::current())->set_blocked(false);
}

// ======= Surrogate Locker Thread =============

void SurrogateLockerThread::loop() {
  SLT_msg_type msg;
  debug_only(unsigned int owned = 0;)

  while (/* !isTerminated() */ 1) {
    {
      MutexLockerAllowGC x(SLT_lock, this);
      // Since we are a JavaThread, we can't be here at a safepoint.
      assert(!SafepointSynchronize::is_at_safepoint(),
             "SLT is a JavaThread");
      // wait for msg buffer to become non-empty
      while (_buffer == empty) {
SLT_lock.notify();
SLT_lock.wait();
      }
      msg = _buffer;
    }
    switch(msg) {
      case acquirePLL: {
        instanceRefKlass::acquire_pending_list_lock();
        debug_only(owned++;)
        break;
      }
      case releaseAndNotifyPLL: {
        assert(owned > 0, "Don't have PLL");
        instanceRefKlass::release_and_notify_pending_list_lock();
        debug_only(owned--;)
        break;
      }
      case empty:
      default: {
        guarantee(false,"Unexpected message in _buffer");
        break;
      }
    }
    {
      MutexLockerAllowGC x(SLT_lock, this);
      // Since we are a JavaThread, we can't be here at a safepoint.
      assert(!SafepointSynchronize::is_at_safepoint(),
             "SLT is a JavaThread");
      _buffer = empty;
SLT_lock.notify();
    }
  }
assert(!SLT_lock.owned_by_self(),"Should unlock before exit.");
}
