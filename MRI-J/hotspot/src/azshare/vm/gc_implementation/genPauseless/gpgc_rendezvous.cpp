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


#include "collectedHeap.hpp"
#include "gcLocker.hpp"
#include "gpgc_rendezvous.hpp"
#include "gpgc_thread.hpp"
#include "mutexLocker.hpp"
#include "tickProfiler.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gcLocker.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

         long GPGC_Rendezvous::_gc_locker_concurrent                  = 0;
         long GPGC_Rendezvous::_old_gc_pending                        = 0;
         long GPGC_Rendezvous::_ready_for_root_marking                = 0;
         long GPGC_Rendezvous::_ready_for_end_marking_safepoint       = 0;
         long GPGC_Rendezvous::_ready_for_end_coordinated_safepoint   = 0;
         long GPGC_Rendezvous::_wait_for_end_coordinated_safepoint    = 0;
         bool GPGC_Rendezvous::_should_coordinate_gc_cycle_end        = false;
         long GPGC_Rendezvous::_ready_for_gc_cycle_end                = 0;
volatile bool GPGC_Rendezvous::_suspend_relocating                    = false;
         long GPGC_Rendezvous::_active_relocating_threads             = 0;


void GPGC_Rendezvous::start_gc_locker_concurrent()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  assert0(_gc_locker_concurrent < 2);

  _gc_locker_concurrent++;

  if ( _gc_locker_concurrent == 1 ) {
GC_locker::lock_concurrent_gc();
  }
}


void GPGC_Rendezvous::end_gc_locker_concurrent()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  assert0(_gc_locker_concurrent > 0);

  _gc_locker_concurrent--;

  if ( _gc_locker_concurrent == 0 ) {
GC_locker::unlock_concurrent_gc();
  }
}


void GPGC_Rendezvous::trigger_old_gc()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  assert0(_old_gc_pending == 0);

  _old_gc_pending++;

GPGC_Rendezvous_lock.notify_all();
}


void GPGC_Rendezvous::wait_for_old_gc()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

  while ( _old_gc_pending < 1 ) {
    assert0(_old_gc_pending == 0);

thread->set_blocked(true);

    GPGC_Rendezvous_lock.wait();

    if ( GPGC_Thread::should_terminate() ) {
      MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
    }
thread->set_blocked(false);
  }

  assert0(_old_gc_pending == 1);

  // Reset the rendezvous counter
  _old_gc_pending = 0;
}


void GPGC_Rendezvous::start_root_marking()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  _ready_for_root_marking++;

  if ( _ready_for_root_marking == 2 ) {
    // We're the second thread in.  Notify the other guy.
GPGC_Rendezvous_lock.notify_all();
  } else {
    // We're the first thread in.  Wait until the other thread comes.
    assert0(_ready_for_root_marking == 1);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

    while ( _ready_for_root_marking < 2 ) {
thread->set_blocked(true);

      GPGC_Rendezvous_lock.wait();

      if ( GPGC_Thread::should_terminate() ) {
        MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
      }
thread->set_blocked(false);
    }

    assert0(_ready_for_root_marking == 2);

    // The other thread has come and gone, reset the counter.
    _ready_for_root_marking = 0;
  }
}


void GPGC_Rendezvous::end_marking_safepoint()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  _ready_for_end_marking_safepoint++;

  if ( _ready_for_end_marking_safepoint == 2 ) {
    // We're the second thread in.  Notify the other guy.
GPGC_Rendezvous_lock.notify_all();
  } else {
    // We're the first thread in.  Wait until the other thread comes.
    assert0(_ready_for_end_marking_safepoint == 1);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

    while ( _ready_for_end_marking_safepoint < 2 ) {
thread->set_blocked(true);

      GPGC_Rendezvous_lock.wait();

      if ( GPGC_Thread::should_terminate() ) {
        MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
      }
thread->set_blocked(false);
    }

    assert0(_ready_for_end_marking_safepoint == 2);

    // The other thread has come and gone, reset the counter.
    _ready_for_end_marking_safepoint = 0;
  }
}


void GPGC_Rendezvous::end_coordinated_safepoint_prepare()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  _ready_for_end_coordinated_safepoint++;

  if ( _ready_for_end_coordinated_safepoint == 2 ) {
    // We're the second thread in.  Notify the other guy.
GPGC_Rendezvous_lock.notify_all();
  } else {
    // We're the first thread in.  Wait until the other thread comes.
    assert0(_ready_for_end_coordinated_safepoint == 1);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

    while ( _ready_for_end_coordinated_safepoint < 2 ) {
thread->set_blocked(true);

      GPGC_Rendezvous_lock.wait();

      if ( GPGC_Thread::should_terminate() ) {
        MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
      }
thread->set_blocked(false);
    }

    assert0(_ready_for_end_coordinated_safepoint == 2);

    // The other thread has come and gone, reset the counter.
    _ready_for_end_coordinated_safepoint = 0;
  }
}


void GPGC_Rendezvous::end_coordinated_safepoint()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  _wait_for_end_coordinated_safepoint++;

  if ( _wait_for_end_coordinated_safepoint == 2 ) {
    // We're the second thread in.  Notify the other guy.
GPGC_Rendezvous_lock.notify_all();
  } else {
    // We're the first thread in.  Wait until the other thread comes.
    assert0(_wait_for_end_coordinated_safepoint == 1);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

    while ( _wait_for_end_coordinated_safepoint < 2 ) {
thread->set_blocked(true);

      GPGC_Rendezvous_lock.wait();

      if ( GPGC_Thread::should_terminate() ) {
        MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
      }
thread->set_blocked(false);
    }

    assert0(_wait_for_end_coordinated_safepoint == 2);

    // The other thread has come and gone, reset the counter.
    _wait_for_end_coordinated_safepoint = 0;
  }
}


void GPGC_Rendezvous::end_gc_cycle()
{
assert0(Thread::current()->is_GenPauselessGC_thread());

MutexLocker ml(GPGC_Rendezvous_lock);

  _ready_for_gc_cycle_end++;

  if ( _ready_for_gc_cycle_end == 2 ) {
    // We're the second thread in.  Notify the other guy.
GPGC_Rendezvous_lock.notify_all();
  } else {
    // We're the first thread in.  Wait until the other thread comes.
    assert0(_ready_for_gc_cycle_end == 1);

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

    while ( _ready_for_gc_cycle_end < 2 ) {
thread->set_blocked(true);

      GPGC_Rendezvous_lock.wait();

      if ( GPGC_Thread::should_terminate() ) {
        MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
      }
thread->set_blocked(false);
    }

    assert0(_ready_for_gc_cycle_end == 2);

    // The other thread has come and gone, reset the counter.
    _ready_for_gc_cycle_end = 0;
  }
}


void GPGC_Rendezvous::start_relocating_threads(long threads)
{
MutexLocker ml(GPGC_Rendezvous_lock);

  assert0(_active_relocating_threads==0);
  assert0(GPGC_Collector::is_old_collector_thread(Thread::current()));

  while ( _suspend_relocating ) {
GPGC_Rendezvous_lock.wait();
  }

_active_relocating_threads=threads;
}


void GPGC_Rendezvous::check_suspend_relocating()
{
  assert0(GPGC_Collector::is_old_collector_thread(Thread::current()));
  assert0(_active_relocating_threads>0);

  if ( _suspend_relocating ) {
MutexLocker ml(GPGC_Rendezvous_lock);
    if ( _suspend_relocating ) {
      assert0(_active_relocating_threads>0);
      _active_relocating_threads --;
      if ( _active_relocating_threads == 0 ) {
GPGC_Rendezvous_lock.notify_all();
      }
      while ( _suspend_relocating ) {
GPGC_Rendezvous_lock.wait();
      }
      _active_relocating_threads ++;
    }
  }
}


void GPGC_Rendezvous::relocating_thread_done()
{
  assert0(GPGC_Collector::is_old_collector_thread(Thread::current()));
  assert0(_active_relocating_threads>0);

MutexLocker ml(GPGC_Rendezvous_lock);

  _active_relocating_threads --;

  if ( _suspend_relocating && _active_relocating_threads==0 ) {
GPGC_Rendezvous_lock.notify_all();
  }
}


void GPGC_Rendezvous::verify_no_relocating_threads()
{
  assert0(_active_relocating_threads==0);
}

void GPGC_Rendezvous::verify_relocating_threads()
{
  assert0(_active_relocating_threads>0);
}


void GPGC_Rendezvous::request_suspend_relocation()
{
  assert0(GPGC_Collector::is_new_collector_thread(Thread::current()));
  assert0(!_suspend_relocating);
GPGC_Thread*thread=(GPGC_Thread*)Thread::current();

MutexLocker ml(GPGC_Rendezvous_lock);

  _suspend_relocating = true;

  while ( _active_relocating_threads > 0 ) {
    GPGC_Rendezvous_lock.wait_micros(20000L, false);
    if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
      MutexUnlocker mul(GPGC_Rendezvous_lock); // unlock to avoid rank-ordering assert
thread->block_if_vm_exited();
    }
  }
}


void GPGC_Rendezvous::resume_relocation()
{
  assert0(GPGC_Collector::is_new_collector_thread(Thread::current()));
  assert0(_suspend_relocating);
  assert0(_active_relocating_threads==0);

MutexLocker ml(GPGC_Rendezvous_lock);

  _suspend_relocating = false;

GPGC_Rendezvous_lock.notify_all();
}
