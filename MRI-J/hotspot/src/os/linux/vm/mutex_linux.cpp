/*
 * Copyright (c) 2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"
#include "thread.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// put OS-includes here
#include <semaphore.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>

extern "C" long int syscall(long int x, ...);
extern "C" int __gettimeofday (struct timeval *__restrict __tv, __timezone_ptr_t __tz);

// Implementation of AzLock

// A simple AzLock for VM locking: it is not guaranteed to interoperate 
// with fast object locking, so:
//  * exclusively use AzLock locking,      or 
//  * exclusively use fast object locking.


// Azul Note: We might have a libos mutex at some point which could be used here.

#define LOCKING_NAME_PREFIX "acquiring VM mutex '"
#define LOCKING_NAME_SUFFIX "'"
#define WAITING_NAME_PREFIX "waiting on VM monitor '"
#define WAITING_NAME_SUFFIX "'"

AzLock::AzLock(int rank, const char *name, bool static_global) : _rank(rank), 
  _contended_lock_attempts(0), _contended_lockers(0), _contended_lockers_total(0), 
  _contended_total_ticks (0), _contended_max_ticks(0),
  _wait_count(0), _wait_max_ticks(0), _wait_total_ticks(0),
  _static_global_lock(static_global)
{
  assert0( _rank != AzLock::JVMLOCK_rank ); // All locks but the JVM lock
  _lock        = 0;             // Lock is not held
  _name        = name;
if(strlen(name)){
    char *lname  = (char*)os::malloc(strlen(LOCKING_NAME_PREFIX)+strlen(name)+strlen(LOCKING_NAME_SUFFIX)+1);
    char *wname  = (char*)os::malloc(strlen(WAITING_NAME_PREFIX)+strlen(name)+strlen(WAITING_NAME_SUFFIX)+1);
    strcpy(lname,LOCKING_NAME_PREFIX);
    strcpy(wname,WAITING_NAME_PREFIX);
strcat(lname,name);
strcat(wname,name);
    strcat(lname,LOCKING_NAME_SUFFIX);
    strcat(wname,WAITING_NAME_SUFFIX);
    _locking_name = lname;
    _waiting_name = wname;
  } else {                      // Else object lock
    _locking_name = "acquiring Object mutex";
    _waiting_name = "waiting on Object monitor";
  }
_next=NULL;
#ifndef PRODUCT
_last_owner=NULL;
  _last_owner_unlock_tick = 0;
#endif

  _os_lock = NULL;              // Lazy init
  // Make the underlying OS lock.  Made eagerly for VM locks, lazily for Java locks.
  if( rank <= JVM_RAW_rank ) {
    make_os_lock();
    register_vm_lock();           
  }
}

AzLock::~AzLock() {
  // Nuking a VM lock?  Only happens at shutdown.  Don't bother to reclaim
  // resources or else we have to deal with lots of races on exit.
  if (is_static_global_lock()) return;
assert(_lock==0,"Owned AzLock being deleted");
  delist_from_locksites();
  if( _os_lock ) {
    int res = sem_destroy((sem_t*)_os_lock);
guarantee(!res,"sem_destroy failed");
  }
  deregister_vm_lock();
if(strlen(_name)){
os::free((void*)_locking_name);
os::free((void*)_waiting_name);
  }
}


void AzLock::make_os_lock() {
  if( _os_lock ) return;
  sem_t *const sem = NEW_C_HEAP_OBJ(sem_t);
  int res = sem_init(sem, 0/*across-threads-not-across-processes*/,0/*init count*/);
guarantee(res==0,"sem_init failed");
  // One-shot insert of an os_lock.  This can fail if another thread is racing to insert,
  // in which case we just leak the losing os_lock.
  if( Atomic::cmpxchg_ptr(sem, &_os_lock, 0) != 0 ) {
    sem_destroy(sem);
    FREE_C_HEAP_ARRAY(sem_t,sem);
  }
}

void AzLock::unlock_implementation() {
  int res = sem_post((sem_t*)_os_lock);
  assert0(!res);
}


void AzLock::sem_wait_for_lock_impl() {
  make_os_lock();
assert(!owned_by_self(),"unowned lock - deadlock?");
  while(1) {
    int res = sem_wait((sem_t*)_os_lock);
    if( !res ) return;
    // The sem_wait reported an error.  If the error is for any reason OTHER
    // than a signal interrupt, we explode.  If the wait() got interrupted we
    // simply re-spin on the wait() - this is the no-interrupt version of
    // things often used for VM locks.  We cannot return until we hold the lock.
    int err = errno;
    assert0( err == EINTR );
  }
}


// Timed-wait
// true - awoke via post
// false- awoke via timeout or interrupt
bool ObjectMonitor::wait_for_lock_implementation2( intptr_t timeout_micros ) {
  struct timespec time = { 0, 0 };
#if 0 // FIXME - clock_gettime hangs on azlinux
  clock_gettime(CLOCK_REALTIME, &time);
#else // !1
timeval gt;
int status=__gettimeofday(&gt,NULL);
assert(status!=-1,"linux gettimeofday error");
  time.tv_nsec = gt.tv_usec*1000;
  time.tv_sec  = gt.tv_sec;
#endif // !1
  time.tv_nsec += timeout_micros*1000;
  long sec = time.tv_nsec/1000000000;
  time.tv_nsec -= sec*1000000000;
  time.tv_sec += sec;
  int res = sem_timedwait((sem_t*)_os_lock, &time);
  if (res == -1 && 
      (errno == ETIMEDOUT || errno == EINTR) ) return false;
  assert0(!res);
  return true;
}


#ifndef PRODUCT
void AzLock::print_on(outputStream*st)const{
  st->print_cr("AzLock: [" PTR_FORMAT "] %s - rank: %d semaphore: " PTR_FORMAT " lock_count: " INTPTR_FORMAT " owner: " PTR_FORMAT " %s",
               this, _name, _rank, _os_lock, contention_count(), owner(), owner()->name());
}
#endif


//
// WaitLock
//
WaitLock::WaitLock(int rank, const char *name, bool static_global) : 
  AzLock(rank, name, static_global), _WNE(0) {
}


WaitLock::~WaitLock() {
}

// --- wait_micros -----------------------------------------------------------
// wait() (sleep this thread) until -
// - notified.  Returns false.
// - timedout.  Returns true.
// - interrupted.  Returns true.
// I think no spurious wakeups are possible.
int WaitLock::wait_micros_impl(long timeout_micros, int64 expected_WNE) {
  // Giant Waits will overflow... but are as good as infinite waits
  if( (timeout_micros<<10) < 0 ) // overflow?
    timeout_micros = 0;          // switch to infinite wait

  int res;
  if (timeout_micros == 0) {
    res = syscall(SYS_futex,&_WNE,FUTEX_WAIT,(int)expected_WNE,0,0,0);
  } else {
    struct timespec time = { 0, 0 };
    time.tv_nsec  = timeout_micros*1000;
    time.tv_sec   = timeout_micros/1000000;
    time.tv_nsec -= time.   tv_sec*1000000000;
    res = syscall(SYS_futex,&_WNE,FUTEX_WAIT,(int)expected_WNE,&time,0,0);
  }
  int errx = res == -1 ? errno : 0;

  assert0( res == 0 ||          // Either normal wait/notify OR
           ((res == -1) &&      // errored out because...
            (errx == ETIMEDOUT || // timedout OR
             errx == EINTR ||     // interrupted OR
             errx == EAGAIN)) );  // underlying _WN word changed?
  return errx;
}


// Notify single condvar waiter
void WaitLock::notify_impl( int all ) {
  int res = syscall(SYS_futex,&_WNE,FUTEX_WAKE,all,0,0,0);
  assert0( all >= res && res >= 0 );
}

// JSR166
// -------------------------------------------------------

/*
 * Park
 * Return if the thread has been interrupted. Don't clear the interrupted flag.
 * Otherwise do a semaphore_[timed]wait.
 * Will have to consume the permit by setting _counter = 0 after coming out of
 * a wait.
 *
 * Unpark
 * Set _counter to 1 and post on the semaphore if _counter was 0 prior to
 * setting it.
 *
 * There are a few simplifications and specialization wrt WaitLock::wait though:
 * Only one thread ever waits on the condvar.
 * And spurious returns are fine, so there is no need to track notifications.
 *
 */

void Parker::park(bool isAbsolute, jlong time) {
  // First, demultiplex/decode time arguments
  if (time < 0) return ; // don't wait at all
  jlong timeout_micros=0;
  if (time > 0) {
    if (isAbsolute) { // Absolute time; 'time' is in milliseconds
time-=os::javaTimeMillis();//relative time in millis
      if (time <= 0) return; // already elapsed ('time' should be a SIGNED entity)
      timeout_micros = time * 1000; // time in microseconds
    } else {                // Relative time; 'time' is in nanoseconds
      // If a positive wait is requested (time > 0), wait for at least
      // one microsecond to prevent an infinite hang.  java.util.concurrent
      // api's provide calls, which round down to 0 microseconds, 
      // without adding 999...
      timeout_micros = (time + 999) / 1000; // time in microseconds
    }
  }

  JavaThread *jt = JavaThread::current();

  // Check interrupt before trying wait
  if (Thread::is_interrupted(jt))
    return;

  TickProfiler::meta_tick(vmlock_wait_tick, (intptr_t)this);

  // Enter safepoint region
  jt->verify_anchor();      // Should keep last_Java_sp around
  // Release the JVM lock while blocked
  jt->jvm_unlock_self();    // Fast & easy!
  // Make stack walkable since we expect to block anyways
  jt->hint_blocked(waiting_name());
  
  // Do the wait
  int res;
  if (timeout_micros == 0) {
    res = syscall(SYS_futex,&_counter,FUTEX_WAIT,/*expected a zero*/0,0,0,0);
  } else {
    struct timespec time = { 0, 0 };
    time.tv_nsec  = timeout_micros*1000;
    time.tv_sec   = timeout_micros/1000000;
    time.tv_nsec -= time.   tv_sec*1000000000;
    res = syscall(SYS_futex,&_counter,FUTEX_WAIT,/*expected a zero*/0,&time,0,0);
  }
  int errx = res == -1 ? errno : 0;

  assert0( res == 0 ||          // Either normal wait/notify OR
           ((res == -1) &&      // errored out because...
            (errx == ETIMEDOUT || // timedout OR
             errx == EINTR ||     // interrupted OR
             errx == EAGAIN)) );  // underlying _WN word changed?
  // Slam in a 0. We don't care about multiple unparks, because park is
  // supposed to consume the permit.
  _counter = 0;

  // We block here while we retake the JVM lock.
jt->jvm_lock_self();

  TickProfiler::meta_tick(vmlock_wakeup_tick, (intptr_t)this);

  // Profiler hint
jt->hint_unblocked();
}

void Parker::unpark() {
  // Try to move the latch from 0 to 1.
  int cas_status = Atomic::cmpxchg(1, &_counter, 0);
  if (cas_status == 0) {       // I moved the latch from 0 to 1
    // hence I should post
    int res = syscall(SYS_futex,&_counter,FUTEX_WAKE,1,0,0,0);
    assert0( 1 >= res && res >= 0 );
  }
}
