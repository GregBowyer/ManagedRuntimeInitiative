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


#include "collectedHeap.hpp"
#include "interfaceSupport.hpp"
#include "gcLocker.hpp"
#include "mutexLocker.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "gcLocker.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

volatile intptr_t GC_locker::_lock_count = 0;
         jlong GC_locker::_jni_state  = 0;

void GC_locker::stall_until_clear() {
  assert(!JavaThread::current()->in_critical(), "Would deadlock");
  MutexLocker   ml(JNICritical_lock);
  // Wait for _needs_gc  to be cleared
  jlong current_state = lock_state();
  while (GC_locker::needs_gc(current_state)) {
    JNICritical_lock.wait();
  }
}

void GC_locker::jni_lock_slow() {
  ThreadBlockInVM tbinvm(JavaThread::current(),"JNICritical_lock");
  MutexLocker mu(JNICritical_lock);
  // Block entering threads if we know at least one thread is in a
  // JNI critical region and we need a GC.
  // We check that at least one thread is in a critical region before
  // blocking because blocked threads are woken up by a thread exiting
  // a JNI critical region.
  while (1) {
    jlong old_state = lock_state(); 
  
    if ( (is_jni_active(old_state) && needs_gc(old_state)) || doing_gc(old_state) ) {
      JNICritical_lock.wait();
    } else {
      jlong new_state = increment_lock_count(old_state);
      if ( old_state == Atomic::cmpxchg(new_state, state_addr(), old_state) ) {
        // lock successful
        break;
      }
 
      // lock failed, loop around and try again.
    }
  }
}

void GC_locker::jni_unlock_slow() {
  // There isn't a slow path jni_unlock with GPGC or PGC.
  assert0((!UseGenPauselessGC));

  MutexLocker mu(JNICritical_lock);

  jlong old_state;
  jlong new_state;

  bool  do_a_gc;
  bool  do_a_notify;

  while (1) {
    do_a_gc     = false;
    do_a_notify = false;

    old_state = lock_state();
    new_state = decrement_lock_count(old_state);

    if ( needs_gc(new_state) && !is_jni_active(new_state) ) {
      do_a_notify = true;
      // GC will also check is_active, so this check is not
      // strictly needed. It's added here to make it clear that
      // the GC will NOT be performed if any other caller
      // of GC_locker::lock() still needs GC locked.
      if ( (!doing_gc(new_state)) && (!is_active()) ) {
        do_a_gc = true;
        new_state = set_doing_gc(new_state);
      } else {
        new_state = clear_needs_gc(new_state);
      }
    }

    if ((old_state = Atomic::cmpxchg(new_state, state_addr(), old_state))) {
      // unlocked successful
      break;
    }

    // unlock failed, loop around and try again.
  } 

  if ( do_a_gc ) {
    {
      // Must give up the lock while at a safepoint
      MutexUnlocker munlock(JNICritical_lock);
      Universe::heap()->collect(GCCause::_gc_locker);
    }

    // Now that the lock is reaquired, unset _doing_gc and _needs_gc:
    while (1) {
      old_state = lock_state();
      new_state = clear_needs_gc(clear_doing_gc(old_state));

      if ((old_state = Atomic::cmpxchg(new_state, state_addr(), old_state))) {
        // clear successful
        break;
      }

      // clear failed, loop around and try again.
    }
  }

  if ( do_a_notify ) {
JNICritical_lock.notify_all();
  }
}

void GC_locker::unlock_concurrent_gc(){
  MutexLocker mu(JNICritical_lock);

  while (1) {
    jlong old_state = lock_state();
    jlong new_state = clear_doing_gc(old_state);

    if ((old_state = Atomic::cmpxchg(new_state, state_addr(), old_state))) {
      // clear successful
      break;
    }

    // clear failed, loop around and try again.
  } 

JNICritical_lock.notify_all();
}

// Implementation of No_GC_Verifier

#ifdef ASSERT

No_GC_Verifier::No_GC_Verifier(bool verifygc) {
  _verifygc = verifygc;
  if (_verifygc) {
    CollectedHeap* h = Universe::heap();
    assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
    _old_invocations = h->total_collections();
  }
}


No_GC_Verifier::~No_GC_Verifier() {
  if (_verifygc) {
    CollectedHeap* h = Universe::heap();
    assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
    if (_old_invocations != h->total_collections()) {
      fatal("collection in a No_GC_Verifier secured function");
    }
  }
}

Pause_No_GC_Verifier::Pause_No_GC_Verifier(No_GC_Verifier * ngcv) {
  _ngcv = ngcv;
  if (_ngcv->_verifygc) {
    // if we were verifying, then make sure that nothing is
    // wrong before we "pause" verification
    CollectedHeap* h = Universe::heap();
    assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
    if (_ngcv->_old_invocations != h->total_collections()) {
      fatal("collection in a No_GC_Verifier secured function");
    }
  }
}


Pause_No_GC_Verifier::~Pause_No_GC_Verifier() {
  if (_ngcv->_verifygc) {
    // if we were verifying before, then reenable verification
    CollectedHeap* h = Universe::heap();
    assert(!h->is_gc_active(), "GC active during No_GC_Verifier");
    _ngcv->_old_invocations = h->total_collections();
  }
}


// JRT_LEAF rules: 
// A JRT_LEAF method may not interfere with safepointing by
//   1) acquiring or blocking on a Mutex or JavaLock - checked
//   2) allocating heap memory - checked
//   3) executing a VM operation - checked
//   4) executing a system call (including malloc) that could block or grab a lock
//   5) invoking GC
//   6) reaching a safepoint
//   7) running too long
// Nor may any method it calls.
JRT_Leaf_Verifier::JRT_Leaf_Verifier()
:No_Safepoint_Verifier(JRT_Leaf_Verifier::should_verify_GC())
{
}

JRT_Leaf_Verifier::~JRT_Leaf_Verifier()
{
}

bool JRT_Leaf_Verifier::should_verify_GC() {
return JavaThread::current()->jvm_locked_by_self();
}
#endif


GCLockerMark::GCLockerMark() {
GC_locker::lock_concurrent_gc();
}

GCLockerMark::~GCLockerMark() {
GC_locker::unlock_concurrent_gc();
}
