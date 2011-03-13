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
#ifndef MUTEX_INLINE_HPP
#define MUTEX_INLINE_HPP


#include "thread_os.inline.hpp"

// This file holds platform-independant bodies of inline functions for mutexes

inline intptr_t AzLock::reversible_tid  () const { return _lock & ((1L<<Thread::reversible_tid_bits)-1); }
inline intptr_t AzLock::contention_count() const { return _lock >> Thread::reversible_tid_bits; }
inline Thread*  AzLock::owner() const { intptr_t rtid = reversible_tid(); return rtid==0 ? NULL : Thread::reverse_tid(rtid); }

// Lock without safepoint check (e.g. without release the jvm_lock).  Only
// allowed for locks of rank < jvm_lock or for non-Java threads.  Should
// ONLY be used by safepoint code and other code that is guaranteed not to
// block while running inside the VM.
inline void AzLock::lock_can_block_gc(Thread *self) {
  assert0( _rank < JVMLOCK_rank || !self->is_Java_thread() );  // This call is only for low rank locks or non-java threads
  lock_common_fastpath(self,false);
}

// Lock acquire, allowing a gc.  This call acts "as if" it releases the
// jvm_lock and re-acquires after it acquires this lock.  However if the
// fast-path works, then the release/reacquire of the jvm_lock can be
// considered instantaneous (and nobody managed to take it during the
// release).  Conceptually, the lock ranking is not violated: it is "as if"
// the high rank lock is first acquired, then the lower rank jvm_lock.  
// Really this call acquires both locks nearly simultaneously (by a
// lock-first-try-lock-second approach; if the 2nd lock is not available it
// reverses the locks and tries again).
// Only allowed for JavaThreads taking locks of rank > JVMLOCK_rank.
inline void AzLock::lock_allowing_gc(JavaThread *jself) {
  assert0( _rank > JVMLOCK_rank );  // This call is only for high rank locks
  lock_common_fastpath(jself,true);
}

// Support for JVM_RawMonitorEnter & JVM_RawMonitorExit. These can be called by
// non-Java thread. (We should really have a RawMonitor abstraction)
inline void AzLock::jvm_raw_lock() {
  assert( _rank == JVM_RAW_rank, "must be called by non-VM locks");
  lock_common_fastpath(Thread::current(),true);
}

// defined in tickProfiler.hpp
extern void meta_tick_vmlock_acquire(intptr_t lock);
extern void meta_tick_vmlock_release(intptr_t lock);

// Fast-path VM Locking support.  Inlined.
inline void AzLock::lock_common_fastpath( Thread *self, bool allow_gc ) {
#ifdef ASSERT
  if( !self->chk_lock(this,allow_gc) ) { // Check general lock rank-ordering rules
print_deadlock(self);
    assert0( self->chk_lock(this,allow_gc) ); // Check general rank-ordering rules
  }
#endif // ASSERT
  if( !try_lock(self) )
    lock_common_contended(self, allow_gc);
  debug_only(self->push_lock(this)); // Track stack of locks owned by thread
  _acquire_tick = os::elapsed_counter(); // Lock hold times
#ifdef ASSERT
  meta_tick_vmlock_acquire((intptr_t)this);
#endif // ASSERT
  _rpc = 0;                     // Filled in by caller.  Used to profile lock holders
  Atomic::read_barrier();
}

// Acquire attempt on the lock.  If it succeeds, the current thread's TID is
// jammed into the _lock field, preserving any existing contention counts.
// If it fails, the contention count is raised.
inline bool AzLock::try_lock( Thread *self ) {
  intptr_t tid = self->reversible_tid();
  while( true ) {
    intptr_t lk = _lock;
    // Expect: same contention count, but 0 TID
    intptr_t expect  = (lk & ~((1L<<Thread::reversible_tid_bits)-1));
    // Desired: same contention count, but with our TID
    intptr_t desired = (lk & ~((1L<<Thread::reversible_tid_bits)-1)) | tid;
    if( Atomic::cmpxchg(desired,&_lock,expect) == expect )
      return true;              // Got it!
    if( (lk & ((1L<<Thread::reversible_tid_bits)-1)) == 0 )  // Unlocked, but we failed to grab it?
      // Must not try to raise the contention-count here - because the lock is
      // currently UNlocked.  If we succeed in raising the contention-count,
      // we'll then go to sleep on the lock awaiting the next un-locker to
      // wake us up.  Since the lock is UNowned, there may never BE a next
      // unlocker.
      continue;                 // Just try again, perhaps contention-count is changing
    // Oops, probably owned by somebody else.  Try instead keep the
    // existing TID but raise the contention count.
    if( !_os_lock ) make_os_lock(); // must have one of these BEFORE contention happens
    desired = lk + (1L<<Thread::reversible_tid_bits);
    if( Atomic::cmpxchg(desired,&_lock,lk) == lk )
      return false;
    // Else _lock field is changing out from under us, spin try again
  }
}

// Unlock the lock.  Returns false if the lock was not contended.  Returns
// true if the lock was contended, AND the lock contention count is lowered.
// Caller is expected to do a semaphore_post.  Can be called from remote
// threads if the lock is a biased Java lock and the current has already exited.
inline bool AzLock::try_unlock( ) {
  intptr_t tid = _lock & ((1L<<Thread::reversible_tid_bits)-1);
  while( true ) {
    // Expect: zero contention count and our tid
    intptr_t expect  = (0L<<Thread::reversible_tid_bits) | tid;
    // Desired: all zero 
    intptr_t desired = (0L<<Thread::reversible_tid_bits) |   0;
    if( Atomic::cmpxchg(desired,&_lock,expect) == expect )
      return false;             // Unlocked and no contention
    // Oops: contention on the unlock: lower contention count.
    intptr_t c = contention_count(); // Reads _lock word
    expect  = ((c  )<<Thread::reversible_tid_bits) | tid;
    desired = ((c-1)<<Thread::reversible_tid_bits) |   0;
    if( Atomic::cmpxchg(desired,&_lock,expect) == expect )
      return true;              // We lowered the contention_count: caller is expected to post()
    // Else _lock field is changing out from under us, spin try again
  }
}

inline void AzLock::unlock() {
  Thread *self = owner();
  assert(self == Thread::current() || 
         // Also allow remote unlocking of biased-but-not-locked locks.
         (self->is_Java_thread() && ((JavaThread*)self)->jvm_locked_by_VM())
,"AzLock not being unlocked by owner");
  // Track how long we held this VM lock.  Do it while holding the
  // lock so stat gathering doesn't need to be atomic.
  record_hold_time();           // Alas, too large to inline
  debug_only(self->pop_lock(this)); // Track stack of locks owned by thread
  debug_only(_last_owner = self); // Handy when we crash
  Atomic::write_barrier();
#ifdef ASSERT
  meta_tick_vmlock_release((intptr_t)this);
#endif // ASSERT
  if( try_unlock() )            // Unlock; if true then contention & count was lowered
    unlock_implementation();    // Oops, need to wake up some waiter
}

inline void AzLock::jvm_raw_unlock() {
  assert( _rank == JVM_RAW_rank, "must be called by non-VM locks");
  unlock();
}

// Just check for self ownership
inline bool AzLock::owned_by_self() const { 
  return owner() == Thread::current();
}

#endif // MUTEX_INLINE_HPP
