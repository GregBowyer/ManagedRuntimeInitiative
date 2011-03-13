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


#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "nativeInst_pd.hpp"
#include "mutexLocker.hpp"
#include "objectMonitor.hpp"
#include "synchronizer.hpp"
#include "tickProfiler.hpp"
#include "vframe.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

ObjectMonitor *ObjectMonitor::_free_mon_list = NULL;
jlong ObjectMonitor::_freeMonCount = 0;

// --- ObjectMonitor ---------------------------------------------------------
ObjectMonitor::ObjectMonitor(heapRef o, markWord* mrk, int rpc) : 
  WaitLock(JVM_RAW_rank+1, "objectMonitor", false), 
  _object(poison_heapRef(o)), _recursion(0), _this_in_use(0),
  _hashCode(mrk->hash()), // hash, if already computed
  _advise_speculation(0), _sma_success_indicator(0) {
  if( mrk->is_biased() ) {       // Mark-word is pre-biased?
    try_lock(mrk->lock_owner()); // Then ObjectMonitor is pre-locked
    _rpc = rpc;
    _acquire_tick = os::elapsed_counter(); // Lock hold times
  }
}

// --- make_monitor ----------------------------------------------------------
ObjectMonitor* ObjectMonitor::make_monitor( heapRef oop, markWord* mrk) {
ObjectMonitor*mon;
  do { 
    mon = _free_mon_list;
    if( !mon ) { 
      mon = make_monitor_bulk_slow(); 
      GET_RPC;
      return new (mon) ObjectMonitor(oop,mrk, RPC);
    }
  } while( Atomic::cmpxchg_ptr(mon->_next,&_free_mon_list,mon) != mon );
  Atomic::dec_ptr(&_freeMonCount);
  GET_RPC;
  return new (mon) ObjectMonitor(oop,mrk, RPC);
}

// --- make_monitor_bulk_slow ------------------------------------------------
// Make a huge bunch of monitors and dump them on the free list.  All addresses
// need to stay in low memory so the monitor's address can be CAS'd into any
// object's markWord.
ObjectMonitor* ObjectMonitor::make_monitor_bulk_slow() {
  CodeBlob *cb = CodeCache::malloc_CodeBlob(CodeBlob::monitors,32*sizeof(ObjectMonitor));

  // Init space
address adr=cb->code_begins();
address max=cb->end();
  bzero(adr, max-adr);
  ObjectMonitor *om = (ObjectMonitor*)adr; // Save the 1st one for returning.
  adr += sizeof(ObjectMonitor);

  // Chop space into ObjectMonitor sized thingies and dump onto free list.
  for( ; adr+sizeof(ObjectMonitor) < max; adr += sizeof(ObjectMonitor) ) 
    ((ObjectMonitor*)adr)->free_monitor();

  return om;
}

// --- free_monitor ----------------------------------------------------------
void ObjectMonitor::free_monitor(){
  assert0( !_next );            // Caller unlinked this from all lists
  assert0( !owner() );          // Unowned
  assert0( !waiters() );        // No pending waiters
  assert0( !_next_unbias );     // No pending biases
  assert0( !_jni_count );       // No pending JNI unlocks
  debug_only(*(intptr_t*)&_object = 0xdeadbeef);
  AzLock *n;
  do {  n = _next = _free_mon_list;
  } while( Atomic::cmpxchg_ptr(this,&_free_mon_list,n) != n );
  // At this point the 'this' object can be immediately in use by any
  // other thread which pulls it off the free list.  We cannot touch
  // it now!
Atomic::inc_ptr(&_freeMonCount);
}


// --- try_lock_with_revoke --------------------------------------------------
// try-lock-w/revoke.  Try to acquire the lock.  If that fails, try to raise
// the contention-count (which prevents the lock from deflating again).  Do
// timed waits, and attempt to revoke any bias until the lock revokes.
// Returns:
// true - lock acquired (hence revoke happened)
// false- contention count raised AND lock revoked, and lock is locked (but not by us!)
// Always returns with the lock revoked.
// GC can happen.
bool ObjectMonitor::try_lock_with_revoke( ) {
  // Just take care of this eagerly so we dont have to sweat it in the loop.
  // Means there's a path where we uselessly made this - except that the lock
  // is obviously contended now, so almost surely we'll need a semaphore here.
  make_os_lock();
  // Bool to track whether or not we got the contention count raised.
  bool upped = false;
  JavaThread *const self = JavaThread::current();
  intptr_t tid = self->reversible_tid();
  // 
  while( true ) {
    // Attempt to take the lock (jam self into _lock word).  Has to adjust the
    // contention-count down if it's been bumped to preserve the invariants.  
    const intptr_t lk = _lock;
    // Expect: same contention count, but 0 TID
    const intptr_t ccbit = 1L<<Thread::reversible_tid_bits;
    const intptr_t ccmask = ~(ccbit-1);
    const intptr_t expect  =  (lk & ccmask);
    if( upped && (lk&ccmask)==0 ) {
      // our CC++ got eaten by somebody else, which means they will do a post
      this->sem_wait_for_lock_impl();
      upped = false;
      continue;
    }
    // Desired: same contention count (-1 if upped), but with our TID
    const intptr_t desired = ((lk & ccmask)-(upped?ccbit:0)) | tid;
    if( Atomic::cmpxchg(desired,&_lock,expect) == expect ) {
      assert0( _recursion >= 0 ); // Bias got revoked
      return true;              // Got it!
    }
    if( (lk & ~ccmask) == 0 )   // Unlocked, but we failed to grab it?
      continue;                 // Just try again

    // Oops, probably owned by somebody else.  Try instead keep the
    // existing TID but raise the contention count.
    const intptr_t desired2 = lk + ccbit;
    if( !upped && Atomic::cmpxchg(desired2,&_lock,lk) == lk )
      upped = true;
    if( !upped ) continue;

    // Go ahead and try to revoke any bias ourselves - caller has already
    // tried to tell the remote thread to revoke the bias himself (but perhaps
    // the owner is asleep and isn't paying us any attention).
    int recur = _recursion; 
    if( recur < 0 ) {           // Not yet revoked
      Atomic::inc_ptr(&_this_in_use); // Prevent deflation while acquire lock
      // Grab threads lock, to prevent owner from disappearing out from under us
MutexLockerAllowGC mx(Threads_lock,self);
      assert0( object()->is_oop() );  // assert 'this' did not deflate
      Atomic::dec_ptr(&_this_in_use); // Prevent deflation while acquire lock
      recur = _recursion;             // re-read under lock
      if( recur > 0 ) return false;   // Now we can do a normal lock acquire
      assert0( recur == -1 );         // If recur==0, then somehow we got rebiased
      // Since lock has not yet revoked, owner is probably not aware of our
      // request.  Fetch owner.
      Thread *t = owner();
      if( !t ||
#if defined(AZ_X86)
          !t->is_Complete_Java_thread() // dead owner
#else
#error Are thread stacks type-stable memory?
#endif 
          ) {
        // Owner does not exist or as exited, leaving a biased lock behind.
        // Revoke the lock on behalf of the owner.  No one else is also
        // trying to revert the bias here, because we hold the Threads_lock.
        // However, other threads can be trying to take the lock the normal
        // way - they will fail (because the lock is held by a dead thread),
        // but I have to CAS into the monitor.
        _recursion = 1;         // When un-bias-locking, unlock to the no-bias state
debug_only(_last_owner=t);
        TickProfiler::meta_tick(objectmonitor_revoke_bias_dead_tick,(intptr_t)this);
        bool res = try_unlock(); // Unlock; if true then contention & count was lowered
        assert0( res );          // Must have been contention - this routine raised it!!!
        unlock_implementation(); // Oops, need to wake up some waiter (self!)
        return false;            // Return false: caller will do a 'wait' eventually
      }
JavaThread*owner=(JavaThread*)t;

      // If the owner is blocked himself (on I/O or another lock) we should
      // just do the job ourselves.  Check *after* setting the unbias_suspend
      // request so we know the owner is aware of our needs.  Otherwise
      // there's a race where we check the jvm_lock and fail (owner holds it),
      // then we set the suspend request but we are too late - owner is
      // already asleep, and never wakes up to revoke the lock.
      if( owner->jvm_lock_VM_attempt() ) { 
        // Find the monitor on the owners' list.  If we can't find it, it
        // means somebody else pulled it off or perhaps the thread died while
        // we were trying to unbias - but it's alive now (tested above) so
        // it's alive as a new thread recycled with the same stack.
        for( ObjectMonitor **pmon = &owner->_unbias; *pmon; pmon = &(*pmon)->_next_unbias ) {
          if( *pmon == this ) {
            *pmon = _next_unbias;
_next_unbias=NULL;
            break;
          }
        }
        // Go ahead and unbias him.
        TickProfiler::meta_tick(objectmonitor_revoke_bias_remote_tick,(intptr_t)this);
        owner->unbias_one(this, "remote_unlock"); // Crawl his stack remotely; count locks; revoke bias.
        // 'recur' remains unchanged at -1 when we fall out of here,
        // which will cause us to spin the whole big loop again.

        owner->jvm_unlock_VM(); // Now free him remotely.
      } else if( owner->jvm_locked_by_VM() ) {
Unimplemented();//Somehow need the VM thread to do the job here
      }
    }
    // Bias revoked (and contention count raised?)?
    if( recur > 0 ) return false; // Now we can do a normal lock acquire
    assert0( recur == -1 );       // If recur==0, then somehow we got rebiased

    // The locking race is ON: we informed the owner via the _unbias list but
    // the owner is "awake" (holds his JVM lock) so (1) we lost the race to
    // take the owner's _jvmlock to unbias the lock ourselves and (2) we
    // assume he will shortly unbias it for us.  Wait a little bit here for
    // that to happen.  Before waiting, be polite - but not TOO polite; we
    // expect this wait time to be very short.
    assert0( object()->is_oop() );
    Atomic::inc_ptr(&_this_in_use); // Prevent deflation while safepointing
    self->poll_at_safepoint();  // GC point
    self->jvm_unlock_self();    // GC point
    if( wait_for_lock_implementation2( 1000 ) ) {
      // post happened?  then CC is lowered
      upped = false;
      recur = _recursion;       // re-read after wait/post for assert
      assert0( recur > 0 );     // Bias got revoked
    } else {
      // Timeout happened, but CC is still raised.  
      // Just spin and retry the jvm_lock revoke path again.
    }
    self->jvm_lock_self_or_suspend();
    assert0( object()->is_oop() );
    Atomic::dec_ptr(&_this_in_use); // Prevent deflation while safepointing
  }
}


// --- insert_on_unbias_list -------------------------------------------------
// Insert self on owning thread's _unbias list.  Return false if we should
// re-attempt the lock because something changed.
bool ObjectMonitor::insert_on_unbias_list() {
  // Complicated handoff to owning thread - because owning thread might
  // unlock/unbias on his own (because of a request from another thread) or
  // might just exit and disappear.

  // Attempt to flip the recursion count from 0 to -1.
  if( Atomic::cmpxchg(-1, (volatile jint*)&_recursion, 0) != 0 )
    return false;               // Failed!  Lock was no longer biased

  // If we can find the owner, we must protect against the owner disappearing
  Atomic::inc_ptr(&_this_in_use); // Prevent deflation while safepointing
  MutexLockerAllowGC mx(Threads_lock, JavaThread::current());
  Atomic::dec_ptr(&_this_in_use); // Prevent deflation while safepointing
  assert0( _recursion != 0 );   // not plain-biased

  // Fetch owner.  Owner might be dead!
  Thread *t = owner();
  if( !t ) return false;        // owner unlocked already?
JavaThread*owner=(JavaThread*)t;

  if( !owner->is_Complete_Java_thread() ) return false; // dead owner

  // Inform owner... but we hold the threads lock so owner can't inspect the
  // jt->_unbias field yet, which is good because we haven't set anything
  // there yet.
  owner->set_suspend_request( JavaThread::unbias_suspend );

  // Simple linked-list insert under lock
  _next_unbias = owner->_unbias;
  owner->_unbias = this;

  return true;
}

// --- unbias_one ------------------------------------------------------------
// Unbias THIS monitor.
// WARNING: Sometimes called from a remote thread!
void JavaThread::unbias_one(ObjectMonitor *mon, const char *msg) {
  assert0( JavaThread::current() == this || jvm_locked_by_VM() );
  // What's the lock-hold time for a biased-held lock?  Is it from the
  // moment the thread gets the bias, even if no other thread requests?  I
  // choose to make it start from the moment some thread requests this lock,
  // i.e. - from Right Now.
  GET_RPC;
  mon->_rpc = RPC;            // Lock-hold starts from right now
  if( mon->_recursion != -1 ) return; // already unbiased?
  
  // Count recursive lock acquires
  int cnt = count_locks(mon->object());

  // Now set the _recursion count.  Note that I can use a plain store,
  // because setting the _recursion count to -1 effectively "locked"
  // changing the _recursion field until the owner (me) gets around
  // to computing the proper result.
  assert0( mon->_recursion == -1 );
  if( cnt == 0 ) {            // unlocked?
    mon->_recursion = 1;      // Set to indicate that the lock shall not be biased
    mon->unlock();            // and unlock it!
  } else {
    mon->_recursion = cnt;    // set to proper recursion count
  }
}

// --- unbias_all ------------------------------------------------------------
// Unbias all ObjectMonitors on the _unbias list
void JavaThread::unbias_all(){

  // Take Threads lock strictly so we can unlink without bothering with
  // CAS'ing.  If this becomes a problem I can CAS monitors both on and off
  // the list.
assert_lock_strong(Threads_lock);

  // While have locks to revoke...
  while( _unbias ) {
    ObjectMonitor *mon = _unbias; // Pop a lock from the list to revoke
    _unbias = mon->_next_unbias;
    mon->_next_unbias = 0;
    assert0( mon->_recursion != 0 ); // WaitLock did not re-bias!  (maybe too strong assert if a GC cycle snuck in there)
    if( mon->_recursion == -1 ) { // Set to -1 to indicate that a revoke is needed
      assert0( mon->owner() == this );  // Got the proper owner as well
      TickProfiler::meta_tick(objectmonitor_revoke_bias_self_tick,(intptr_t)mon);
      unbias_one(mon,"unbias_all");
    }
  }
}

// --- wait_java -------------------------------------------------------------
// Implement a standard Java Object.wait call.
void ObjectMonitor::wait_java(jlong millis,bool interruptible,TRAPS){
  JavaThread *jt = (JavaThread *)THREAD;
  assert0( jt == JavaThread::current() );
  assert0( owned_by_self() );

  if (millis < 0) {
    THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "timeout value is negative");
  }
  if( HAS_PENDING_EXCEPTION ) return;
  if( interruptible && os::is_interrupted_and_clear(jt) ) {
THROW(vmSymbols::java_lang_InterruptedException());
  }

  // Our wait is in micros, but Java's wait is in millis.  Converting millis to
  // micros might overflow, but if it does the wait is essentially "forever".
  jlong micros = millis*1000;
  if( micros < 0 )
    micros = 0x7fffffffffffffffLL; // a big number that does not overflow as nanos

  if( _jni_count ) Unimplemented(); // need to save/restore this also

  // The lock might be held recursively.  We are going to completely
  // unlock, then restore all recursive counts.
  int saved_recursions = _recursion;
  // Leave the lock not-biased, so a future unlocker actually does a true
  // unlock - since that future unlocker probably also does the notify that
  // wakes us up and this thread wants the lock.
  if( saved_recursions <= 0 ) {
    if( saved_recursions == 0 ) { // It's either 0 or -1
      saved_recursions = Atomic::cmpxchg(1, (volatile jint*)&_recursion, 0);
      // Either the old value was a 0 or a -1.  CAS failed if old was -1.
      assert0( saved_recursions == 0 || saved_recursions == -1 );
    }
    if( saved_recursions == -1 ) // CAS failed if old was -1
      _recursion = 1;       // But we no longer need to CAS to revoke the bias
    saved_recursions = jt->count_locks(object());
    assert0( saved_recursions > 0 );  // had to take the lock to do a 'wait'!

  } else {
    // However many times we held the lock recursively, reset the count to 1
    // before unlocking - signalling that new lock acquirers hold the lock
    // not-biased.
    _recursion = 1;             // Reset recursion count to 1 before unlocking
  }

  // Do the wait using the existing mechanism.  Prevent the monitor from
  // deflating - after the 'wait' call eats a 'notify' we might block
  // acquiring the normal monitor lock and take a GC without having any
  // waiters pending which might deflate this lock.
  Atomic::inc_ptr(&_this_in_use); // Prevent deflation while safepointing
  bool res = wait_micros(micros, interruptible);
  Atomic::dec_ptr(&_this_in_use); // Prevent deflation while safepointing

  assert0( _recursion == 1 ); // reacquiring lock after "wait" leaves lock not-biased

  // Unwind
  assert0( saved_recursions > 0 ); // No re-installing a biased lock
  _recursion = saved_recursions;

  // If we exited prematurely (timeout or interrupt), check again for
  // interrupt and throw as needed.  Normal exits (e.g. notify) take
  // precedence and won't throw if both a notify AND interrupt happens.
  if( HAS_PENDING_EXCEPTION ) return;
  if( res && interruptible && os::is_interrupted_and_clear(jt)) {
    THROW(vmSymbols::java_lang_InterruptedException());
  }
}

// --- unbias_locked -------------------------------------------------------------
// Unbias this lock, i.e. already locked by current thread.  Also assumes the
// lock-acquire-er does not leave a 'cookie' that count_locks can find and so
// the lock will be under-counted unless we force the lock inflated now and
// increase the count now.  Examples of lock 'cookies' are: ObjectLocker
// structures (stack allocated and threaded on a linked list on Thread*),
// compiled-frame debug info, and the interpreters lock-stack.
void ObjectMonitor::unbias_locked(){
  assert0( owned_by_self() );

  int rec = _recursion;
  if( _recursion > 0 ) // already unbiased?  
    return; // Then the normal lock acquire already bumped the recursion count
  // _recursion must be zero (biased) or -1 (mid-revoke)
  assert0( rec == 0/*biased*/ || rec == -1/*is now revoking*/ );

  // Count recursive lock acquires.  Misses one because the caller just locked
  // without leaving a 'cookie' to be counted.
  const int cnt = JavaThread::current()->count_locks(object());

  // Expected trip count is not more than one.
  // As an owner of this lock, the only two possibilities of the following CAS are:
  // either this thread wins and will set the _recursion field to +ve value
  // or a racing thread trying to invoke revokation of bias will set the field to -1.
  const int reread_recur = Atomic::cmpxchg(cnt+1, (volatile jint*)&_recursion, rec);
  if( reread_recur != rec ) {
    assert0( rec == 0/*was zero*/ && reread_recur == -1/*and is now revoking*/ );
    const int res2 = Atomic::cmpxchg(cnt+1, (volatile jint*)&_recursion, -1);
    assert0( res2 == -1 );      // CAS must work
  }
}
