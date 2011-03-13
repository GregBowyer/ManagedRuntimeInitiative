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


#include "gpgc_collector.hpp"
#include "gpgc_slt.hpp"
#include "gpgc_thread.hpp"
#include "os.hpp"
#include "surrogateLockerThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "os_os.inline.hpp"

volatile intptr_t GPGC_SLT::_state      = GPGC_SLT::Unlocked;
volatile bool     GPGC_SLT::_new_locked = false;
volatile bool     GPGC_SLT::_old_locked = false;


bool GPGC_SLT::is_locked()
{
  Thread* thread = Thread::current();

  if ( GPGC_Collector::is_new_collector_thread(thread) ) {
    return _new_locked;
  } else {
    assert0(GPGC_Collector::is_old_collector_thread(thread));
    return _old_locked;
  }
}


void GPGC_SLT::acquire_lock()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

if(thread->is_new_collector()){
    assert0( _new_locked == false );
  } else {
    assert0( _old_locked == false );
  }

  while (true) {
    // SLT interactions seem like a centralized place to check on pending vm exits.
    if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
    }

    intptr_t old_state = _state;
    intptr_t new_state;

    if ( old_state == Unlocked ) {
      // Try and move the state to Locked
      new_state = Locking;
      if ( old_state != Atomic::cmpxchg_ptr(new_state, &_state, old_state) ) {
        // Failed CAS, loop around and try again
        continue;
      }

      // This thread has claimed the right to acquire the PLL.

      SurrogateLockerThread::slt()->manipulatePLL(SurrogateLockerThread::acquirePLL);

      assert( !(((GPGC_Thread *)Thread::current())->is_blocked()), "we should be unblocked" );

      if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
      }

_state=Locked;

      break;
    }

    if ( old_state == Locking ) {
      // The other collector is trying to acquire the PLL.  Give it a moment, and check again.
      os::yield_all();
      continue;
    }

    if ( old_state == Unlocking ) {
      // The other collector is trying to release the PLL.  Give it a moment, and check again.
      os::yield_all();
      continue;
    }

    if ( old_state == Locked ) {
      // The other collector has already acquired the PLL, so we just mark that both collectors
      // have the lock.
      new_state = TwoLocked;
      if ( old_state != Atomic::cmpxchg_ptr(new_state, &_state, old_state) ) {
        // Failed CAS, loop around and try again
        continue;
      }

      break;
    }

    guarantee( old_state != TwoLocked, "TwoLocked while one collector hasn't acquired the PLL!" );

    // If we get here, we're seeing some unknown SLT state.
    ShouldNotReachHere();
  }

if(thread->is_new_collector()){
    _new_locked = true;
  } else {
    _old_locked = true;
  }
}


void GPGC_SLT::release_lock() {
assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

if(thread->is_new_collector()){
    assert0( _new_locked == true );
  } else {
    assert0( _old_locked == true );
  }

  intptr_t old_state;
  intptr_t new_state;

  while (true) {
    // SLT interactions seem like a centralized place to check on pending vm exits.
    if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
    }

    // Now try and move the state to NotAtSafepoint
old_state=_state;

    if ( old_state == TwoLocked ) {
      // The other collector also holds the lock, so we move the state to one collector having the lock.
      new_state = Locked;
      if ( old_state != Atomic::cmpxchg_ptr(new_state, &_state, old_state) ) {
        // Failed CAS, loop around and try again
        continue;
      }

      break;
    }

    if ( old_state == Locked ) {
      new_state = Unlocking;
      if ( old_state != Atomic::cmpxchg_ptr(new_state, &_state, old_state) ) {
        // Failed CAS, loop around and try again
        continue;
      }

      SurrogateLockerThread::slt()->manipulatePLL(SurrogateLockerThread::releaseAndNotifyPLL);
      assert( !(((GPGC_Thread *)Thread::current())->is_blocked()), "we should be unblocked" );

_state=Unlocked;

      break;
    }

    guarantee( old_state != Unlocking, "Unlocking while this collector has the lock!" );
    guarantee( old_state != Locking,   "Locking while this collector has the lock!" );
    guarantee( old_state != Unlocked,  "Unlocked while this collector has the lock!" );

    // If we get here, we're seeing some unknown SLT state.
    ShouldNotReachHere();
  }

  if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
  }

if(thread->is_new_collector()){
    _new_locked = false;
  } else {
    _old_locked = false;
  }
}
