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


#include "cardTableRS.hpp"
#include "collectedHeap.hpp"
#include "defNewGeneration.hpp"
#include "generation.hpp"
#include "genRemSet.hpp"
#include "nativeInst_pd.hpp"
#include "markWord.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "sharedHeap.hpp"
#include "space.hpp"
#include "synchronizer.hpp"
#include "task.hpp"
#include "threadService.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "genOopClosures.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// --- lock ------------------------------------------------------------------
// Lock the object.  Probably will be slow-path and blocking, as the
// fast-path attempts should already have happened and failed.  
// May block.  May GC.  May throw exceptions.
void ObjectSynchronizer::lock( heapRef ref, MonitorInflateCallerId caller) {
  // All the fast-path tests are repeated here - just in case somebody ends up
  // calling here on a hot-path without doing the fast-path tests first.
  // 1- See if the lock is pre-biased to this thread
  // 2- See if the lock is unlocked and can be CAS'd
  // 3- Install a heavy monitor and also check:
  // 4- - the monitor is biased to this thread
  // 5- - the monitor is unlocked and can be CAS'd
  // 6- - the monitor is locked by this thread (bump recursion count)
  // 7- - take the OS lock associated with the monitor.

  // 1- See of the lock is pre-biased to this thread
oop obj=ref.as_oop();
  markWord *mark = obj->mark();
  if( mark->is_self_spec_locked() ) return;

  // 1.5- See if lock is biased to a dead thread, and strip the bias.
  // TODO/FIXME:  Come back and revisit this code; when jt is dead but
  //              holds a lock, it's not clear we're doing this right.
  //              Also, remember we're going to move the jvm lock from 
  //              the JavaThread into the Thread class. (Cliff's comments
  //              added by Bean)
  while( mark->is_biased() ) {
JavaThread*jt=mark->lock_owner();
    if( !jt ) break;
#if defined(AZ_X86)
    if( jt->is_Complete_Java_thread() ) break;
#else
#error Are thread stacks type-stable memory?
#endif 
    // Owned by 'jt' but 'jt' is dead.
    assert0( mark->has_no_hash() ); // no hash on biased markWords
    obj->cas_set_mark(mark->as_fresh(), mark);
    mark = obj->mark();         // Reload
  }

  // 2- See if the lock is unlocked and can be CAS'd
  intptr_t tid = Thread::current()->reversible_tid();
  if( mark->is_fresh() && obj->cas_set_mark( mark->as_biaslocked(tid), mark ) == mark )
    return;                     // Got it!

  // 3- Install a heavy monitor and also check:
  ObjectMonitor *mon = mark->has_monitor() ? mark->monitor() : inflate(ref,caller);
  // Now I cannot allow a GC lest the monitor deflate out from under me

  // 4- - the monitor is biased to this thread.  Always true for fresh monitors
  if( mon->is_self_spec_locked() ) 
    return;                     // Got it

  // 5- - the monitor is unlocked and can be CAS'd
  if( !mon->is_locked() && mon->try_lock(Thread::current()) ) {
    // Got the lock!!!
  } else {

    // 6- - the monitor is locked by this thread (bump recursion count)
    if( mon->owned_by_self() ) {
      assert0( mon->_recursion > 0 ); // lock is counted and not biased
      mon->_recursion++;
      return;
    }

    // Is the monitor apparently biased to the wrong thread?
    // If so, tell the current owner and wait for him release the lock.
    if( mon->_recursion == 0 ) {
      Handle h(obj);              // Handlize across possible GC point
      if( !mon->insert_on_unbias_list() ) {
        lock( h(), caller );      // Retry from the git-go
        return;
      } 
    }

    // 7- - take the OS lock associated with the monitor.
    // Very similar to lock_common_fastpath, except specialized for ObjectMonitors
    // This call can GC, block, etc.
    if( !mon->try_lock_with_revoke() ) {
      Atomic::inc_ptr(&mon->_this_in_use); // Prevent deflation while safepointing
      assert0( mon->object()->is_oop() ); // assert 'mon' did not deflate
      JavaThreadBlockedOnMonitorEnterState jtbmes(JavaThread::current(), mon);
      mon->lock_common_contended(JavaThread::current(),true/*allow gc*/);
      assert0( mon->object()->is_oop() ); // assert 'mon' did not deflate
      Atomic::dec_ptr(&mon->_this_in_use); // Prevent deflation while safepointing
    }
  }

  // Record lock hold time
  GET_RPC;
  mon->_rpc = RPC;              // Record locksite info
  mon->_acquire_tick = os::elapsed_counter(); // Lock hold times
  meta_tick_vmlock_acquire((intptr_t)mon);
  Atomic::read_barrier();       // locking read barrier
}

// --- unlock ----------------------------------------------------------------
// Unlock the object.  Probably will be slow-path as the fast-path
// attempts should already have happened and failed.
// Cannot block, GC, nor throw exceptions.
void ObjectSynchronizer::unlock(oop obj){
  
  // See if the lock is pre-biased to this Thread
  markWord *mark = obj->mark();
  if( mark->is_self_spec_locked() ) return;

  // There MUST be a heavy monitor
ObjectMonitor*mon=mark->monitor();
  assert0( mon->owned_by_self() );

  // See if the lock is inflated and pre-biased to this Thread
  if( mon->is_self_spec_locked() ) return;

  // Ahhh... counting recursive locks
  if( mon->_recursion > 1 ) {   // Still nestedly owned?
    mon->_recursion--;          // Lower recursion and be done
    return;
  }
  
  // Leave recursion count at 1 and unlock
  assert0( mon->_recursion==1 );
mon->unlock();
}


#define LOCKING_NAME_PREFIX "acquiring VM mutex '"
#define LOCKING_NAME_SUFFIX "'"
#define WAITING_NAME_PREFIX "waiting on VM monitor '"
#define WAITING_NAME_SUFFIX "'"

// --- inflate ---------------------------------------------------------------
// Inflation!  "Inflate" the lock: make an objectMonitor for this Java object
// and make the object's header refer to it.  Inflated locks have a bunch of
// useful features:
// - the object can be both locked AND have a hashCode
// - the object supports lock contention, wait & notify
// - SMA works only on inflated locks (so we have a place for SMA stats)
ObjectMonitor* ObjectSynchronizer::inflate(heapRef ref, MonitorInflateCallerId caller) {

  // Make a new clean ObjectMonitor.  It has the object's old hashCode, if the
  // object had one, or a fresh hashCode otherwise.  It starts out unlocked
  // and prepared to be bias-locked.
  const oop o = ref.as_oop();
  markWord *mrk = o->mark();
  if( mrk->has_monitor() ) return mrk->monitor();
  ObjectMonitor *mon = ObjectMonitor::make_monitor(ref,mrk);

  // Now attempt to install the fresh monitor.  Very racey, we might lose.
  // If we lose, we should free our useless fresh monitor and reread the new one.
  // If we win, then we also have named the hashCode for this object.
  // This means that several threads might make ObjectMonitors each with
  // unique hashCodes - and only the winner's hashCode will "stick".
  markWord *mrk_with_mon = mrk->as_heavylocked(mon);
  markWord *res = o->cas_set_mark(mrk_with_mon,mrk);
  if( res == mrk ) {            // We win?
    // Link on to list of all monitors.  We use this list for profiling & deflation.
ObjectMonitor*nmon;
    do { mon->_next = nmon = _monitors;
    } while( Atomic::cmpxchg_ptr(mon,&_monitors,nmon) != nmon );
    Atomic::post_increment(&_monCount);
    return mon;                 // We Win!
  }
    
  // We lost the inflate race: free the freshly made useless monitor and
  // return the existing monitor.  Actually we can lose for a bunch of
  // reasons, including the clean markWord getting a 1-shot hashCode installed
  // by another thread or another thread inflating a monitor - then getting
  // deflated before we can try again.
mon->_next=NULL;//not on any list right now
  mon->_lock =    0;          // not indicating any ownership either
  mon->free_monitor();
  // Return existing monitor (if any)
  if( res->has_monitor() ) 
return res->monitor();

  // We lost the race due to e.g. racing hashCode insertion by another
  // thread?  Then we need to rebuild a new ObjectMonitor (with the
  // hashCode in it!)  and try again.
  return inflate(ref,caller);   // recursive-tail-call is really a retry loop
}


// --- current_thread_holds_lock ---------------------------------------------
// Expensively determine if a thead really holds a biased lock, without
// revoking the bias or relying on the bias.
bool ObjectSynchronizer::current_thread_holds_lock(JavaThread* thread, oop o ) {
  assert0( thread == JavaThread::current() );
  if( !o->is_self_locked() ) return false; // Not owned by the current thread.
  // Owned by the current thread, so we can move on to the next tests.
  markWord *mark = o->mark();
  if( mark->has_monitor() && mark->monitor()->_recursion > 0 ) 
    return true; // Not biased, so we know the answer immediately.
  // Biased to self, so now we have to do the expensive stack crawl
  return thread->count_locks(o) != 0;
}


// --- get_next_hash ---------------------------------------------------------
intptr_t ObjectSynchronizer::get_next_hash() {
Thread*Self=Thread::current();
  intptr_t value = 0 ; 
  // Marsaglia's xor-shift scheme with thread-specific state
  // This is probably the best overall implementation -- we'll
  // likely make this the default in future releases.
  unsigned t = Self->_hashStateX ; 
  t ^= (t << 11) ; 
  Self->_hashStateX = Self->_hashStateY ; 
  Self->_hashStateY = Self->_hashStateZ ; 
  Self->_hashStateZ = Self->_hashStateW ; 
  unsigned v = Self->_hashStateW ; 
  v = (v ^ (v >> 19)) ^ (t ^ (t >> 8)) ; 
  Self->_hashStateW = v ; 
  value = v ; 
  
value&=markWord::hash_mask;
  if (value == 0) value = 0xBAD ; 
  return value;
}

// --- FastHashCode ----------------------------------------------------------
// Returns the identity hash value for an oop.
// NOTE: It may cause monitor inflation, but never a GC.
intptr_t ObjectSynchronizer::FastHashCode( objectRef ref ) {
oop obj=ref.as_oop();
  while( true ) {
    // Read it once (racey with inflation!)
    markWord *mark = obj->mark();

    // First see if we got a hash already, in-place
    if( !mark->has_no_hash() ) return mark->hash();

    // See if we spec-own it.  
if(mark->is_self_spec_locked()){
      // Self-spec-owned.
      int nested_locks = JavaThread::current()->count_locks(obj);
      if( nested_locks == 0 ) {
        // We can remove the spec-ownership with a plain store.
        assert0( mark->has_no_hash() ); // no hash on biased markWords
        markWord *newMark = mark->as_fresh();
        markWord *oldMark = obj->cas_set_mark(newMark,mark); // No longer biased
        if( oldMark != mark ) { // cas failed, possibly due to racey hashCode insertion
          continue;             // just retry from scratch
        }
      } 
    } 

    // Could be remote-biased or self-biased but with counts.  Now we must
    // install a monitor, even if all we need to do is revoke the remote bias.
    // And if we install a monitor, we can store the hashCode there and skip
    // the remote-bias-revoke step.  The next GC cycle will remove a useless
    // biased monitors.
if(mark->is_biased()){
      // Self-owned but with real counts, or remote biased.  Must install a
      // monitor for objects that are both locked and hashed.
      inflate(obj,INF_HASHCODE);
      mark = obj->mark();       // Re-read, with monitor
    }

    // See if we feel inflated (because the hash will be there!)
    if( mark->has_monitor() ) {
ObjectMonitor*mon=mark->monitor();
      if( mon->_hashCode ) return mon->_hashCode;
      intptr_t hash = get_next_hash();
      // Dont care if I win or lose this race, as long as there's no spurious failure.
      // SOMEBODY gets to install the hashCode, and always I return the installed one.
      Atomic::cmpxchg(hash, &mon->_hashCode, 0);
      return mon->_hashCode;
    }
    
    // Ok, not inflated & no hash & unlocked - compute the hash and
    // attempt to install it.
    intptr_t hash = get_next_hash();
    markWord *mark_with_hash = mark->copy_set_hash(hash);
    if( obj->cas_set_mark(mark_with_hash, mark) == mark )
      return hash;
    // CAS failed; need to retry!
  }
}

// ===========================================================================
// --- ObjectMonitorDeflater -------------------------------------------------
// ObjectMonitorDeflater implementation based on ChunkPoolCleaner
ObjectMonitor *ObjectSynchronizer::_monitors = NULL; // Global list of all monitors
jlong ObjectSynchronizer::_last_deflation_round = 0;
jlong ObjectSynchronizer::_monCount = 0;
static bool pending_deflate = false;

class ObjectMonitorDeflater:public PeriodicTask{
  public:
    ObjectMonitorDeflater() : PeriodicTask(ObjectMonitorDeflationInterval) {}
    void task() {
      if( !pending_deflate &&
          ObjectSynchronizer::_monCount >= (int64_t)ObjectMonitorDeflationThreshold && 
          (ObjectMonitor::freeMonCount()<<3) < ObjectSynchronizer::_monCount ) {
        VMThread::async_execute(new VM_ForceSafepoint());
        pending_deflate = true;
      }
    }
};

// --- start_monitor_deflate_task --------------------------------------------
// Start the deflater task called from Threads::create_vm()
void ObjectSynchronizer::start_monitor_deflate_task(){
  if (EnableAggressiveDeflations) {
#ifdef ASSERT
    static bool task_created = false;
assert(!task_created,"should not start montior deflater twice");
    task_created = true;
#endif
    ObjectMonitorDeflater* cleaner = new ObjectMonitorDeflater();
cleaner->enroll();
  }
}

// --- deflate_idle_monitors -------------------------------------------------
// Deflate_idle_monitors() is called at all safepoints, immediately after all
// mutators are stopped, but before any objects have moved.  It traverses the
// list of known monitors, deflating where possible.  The scavenged monitor
// are returned to the monitor free list.
//
// Beware that we scavenge at *every* stop-the-world point.  Having a large
// number of monitors in-circulation negatively impacts the performance of
// some applications (e.g., PointBase).  Broadly, we want to minimize the # of
// monitors in circulation.  Alternately, we could partition the active
// monitors into sub-lists of those that need scanning and those that do not.
// Specifically, we would add a new sub-list of objectmonitors that are
// in-circulation and potentially active.  deflate_idle_monitors() would scan
// only that list.  Other monitors could reside on a quiescent list.  Such
// sequestered monitors wouldn't need to be scanned by
// deflate_idle_monitors().  omAlloc() would first check the global free list,
// then the quiescent list, and, failing those, would allocate a new block.
// Deflate_idle_monitors() would scavenge and move monitors to the quiescent
// list.
//
// Perversely, the heap size -- and thus the STW safepoint rate -- typically
// drives the scavenge rate.  Large heaps can mean infrequent GC, which in
// turn can mean large(r) numbers of objectmonitors in circulation.  This is
// an unfortunate aspect of this design.
//
// Another refinement would be to refrain from calling deflate_idle_monitors()
// except at stop-the-world points associated with garbage collections.
//
// An even better solution would be to deflate on-the-fly, aggressively, at
// monitorexit-time as is done in EVM's metalock or Relaxed Locks.
void ObjectSynchronizer::deflate_idle_monitors() {

  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");

#ifdef ASSERT
  bool trouble = false;
for(JavaThread*t=Threads::first();t;t=t->next()){
    if( t->_allow_safepoint_count ) {
      if( !trouble ) tty->flush();
tty->print_cr("Safepoint - Thread %ld in trouble",t->unique_id());
      trouble = true;
    }
  }
  if (trouble)
fatal("Possible safepoint reached by thread(s) that do not allow it");
#endif // ASSERT

  bool do_deflations = (os::elapsed_counter() - _last_deflation_round) > (jlong)os::millis_to_ticks(ObjectMonitorDeflationInterval);
  if( !do_deflations ) 
    return;
  if( !EnableDeflations ) return;

  ObjectMonitor **pmon = &_monitors;
  ObjectMonitor *mon = *pmon;
  while( mon ) {
    JavaThread *owner = (JavaThread*)mon->owner();
    if( owner ) {
      if( !owner->is_Complete_Java_thread() ) owner=NULL; // dead owner
    }

    // Lock has contended lockers.  It must remain inflated.
    if( mon->contention_count() ||
        // Actively in use by another thread's C code
        mon->_this_in_use ||
        // Lock has threads awaiting a notify.  It must remain inflated.
        mon->waiters() ||
        // Do not deflate speculating monitors: we like speculation.
        mon->advise_speculation() ||
        // Must not have any JNI-locking; the JNI lock counts are only in the
        // monitor and are used to enforce balanced JNI lock/unlock.
        mon->_jni_count ||
        // Is currently owned "the hard way": has owner and has non-zero recursion
        (owner && mon->_recursion) ||
        false ) {
      // Keep this monitor and move the prev-monitor pointer forward
      pmon = (ObjectMonitor**)(&mon->_next);
      mon = *pmon;
      continue;
    }
    assert0( !mon->_next_unbias ); // No pending biases

    // Remove the bias of bias-locked objects.  Some objects will re-bias
    // immediately, but some will remain biased-locked with no monitor.
    if( owner && !mon->_recursion ) { 
      if( owner->count_locks(mon->object()) != 0 ) { // expensively check the bias count
        // Keep this monitor and move the prev-monitor pointer forward
        pmon = (ObjectMonitor**)(&mon->_next);
        mon = *pmon;
        continue;
      }
      // The monitor is bias-locked, hashed & has zero bias-count.  Deflate,
      // pushing the hash back into the header.  
      owner = 0;
    }

oop o=mon->object();
    assert0( mon == o->mark()->monitor() );
    assert0( !owner || !mon->_hashCode ); // Can't be both locked & hashed to deflate
    // Simply put the bias-lock (or unlocked) into the object.
    markWord *mark = o->mark();
    markWord *newmark = mark->as_fresh();
    // If owned then inject ownership bias into markWord
    if( owner ) newmark = newmark->as_biaslocked(owner->reversible_tid());
    // if hashed (and hence not owned) then inject hashCode into markWord
    if( mon->_hashCode ) newmark = newmark->copy_set_hash(mon->_hashCode);
    // We can use set_mark instead of cas_set_mark because we're at a safepoint.
o->set_mark(newmark);
    // Unlink from list.  Again, requires a Safepoint because a concurrent
    // monitor insert will break the linked-list manipulation.
    ObjectMonitor *nmon = (ObjectMonitor*)mon->next();
    *pmon = nmon;
    mon->_next = 0;           // monitor is not on list anymore
    _monCount--;
    // Since we are at a safepoint, nobody has a pointer to the object and
    // can be trying to grab the lock.  So we are free to plain-store the
    // _lock to indicate it is free.
    mon->_lock = 0;           // Not owned by anybody
    // Now we have a free monitor.
    mon->free_monitor();
    // Link on to the next monitor
    mon = *pmon;
    assert0( o->mark() == newmark ); // mark-setting was not racey; it "sticks"
  }
_last_deflation_round=os::elapsed_counter();
}


// --- oops_do
// Oops_do for all oops in monitors
void ObjectSynchronizer::oops_do(OopClosure* f) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");
  for( ObjectMonitor *m = _monitors; m; m=(ObjectMonitor*)m->next() ) 
m->oops_do(f);
}

// --- release_monitors_owned_by_thread
// 6282335 JNI DetachCurrentThread spec states that all Java monitors held by
// this thread must be released.  A detach operation must only get here if
// there are no Java frames on the stack.  Therefore, any owned monitors at
// this point MUST be JNI-acquired monitors which are pre-inflated and in the
// monitor cache.
//
// ensure_join() ignores IllegalThreadStateExceptions, and so does this.
void ObjectSynchronizer::release_monitors_owned_by_thread(JavaThread*thread){
  // A possible impl would be to search all the monitors for ones owned by
  // this thread, and assert they are either biased or unowned - except for
  // JNI counts.  Unlock and JNI locks and revert any biases.
  for( ObjectMonitor *m = _monitors; m; m=(ObjectMonitor*)m->next() ) {
    if( m->owned_by_self() ) {  // No lock is owned by self, except for biased or JNI
      while( m->_jni_count ) {  // Unwind the JNI locks
        Untested();
        m->_jni_count--;
m->unlock();
      }
      // Still self-locked?
      if( m->owned_by_self() ) { // No lock is owned by self, except for biased
        assert0( m->is_self_spec_locked() );
        // For an unlocked biased lock, the recursion count is either 0 or -1.
        // We CAS it to +1 - indicating we are holding the lock once only.
        int orec = m->_recursion;
        assert0( orec == 0 || orec == -1 );
        while( Atomic::cmpxchg(1, (volatile jint*)&m->_recursion, orec) != orec ) 
          orec = m->_recursion;
        m->unlock();            // Now unlock it, while not biased
      }
    }
  }
}


// --- check_notify
static ObjectMonitor *check_notify(objectRef obj, TRAPS) {
  markWord *mark = obj.as_oop()->mark();
  // Having waiters always inflates a lock, so if this object is biased-locked
  // then it has no waiters and so there is no one to notify.
  if( mark->is_self_spec_locked() ) return NULL;
  
  // Stack objects never bias, never inflate and can never have waiters
  if( obj.is_stack() && mark->is_fresh() ) {
    // Alas, we must count locks for correctness here.  But always
    // there is no one to notify on stack objects.
    if( JavaThread::current()->count_locks(obj.as_oop()) > 0 ) return NULL;
    // However, notify on an *unlocked* stack object is an error.
    return (ObjectMonitor*)-1;
  }
  // Better be inflated with a monitor (catches the case of a hashCode or incorrect bias)
  if( !mark->has_monitor() ) return (ObjectMonitor*)-1;

  // Better be a self-owned monitor.
ObjectMonitor*mon=mark->monitor();
  if( !mon->owned_by_self() )
    return (ObjectMonitor*)-1;
  return mon;
}


// --- notify
void ObjectSynchronizer::notify(objectRef obj,TRAPS){
  ObjectMonitor *mon = check_notify(obj,THREAD);
  if( (ObjectMonitor*)-1 == mon ) // Oops!
    THROW(vmSymbols::java_lang_IllegalMonitorStateException());
  if( !mon ) {                  // Stack object; known no-waiters
    TickProfiler::meta_tick(vmlock_notify_nobody_home_tick, 0);
    inflate(obj,INF_WAIT);      // inflate it, expecting a waiter and a monitor shortly, but too late for this guy
    return;
  }
mon->notify();
}


// --- notifyall
void ObjectSynchronizer::notifyall(objectRef obj,TRAPS){
  ObjectMonitor *mon = check_notify(obj,THREAD);
  if( (ObjectMonitor*)-1 == mon ) // Oops!
    THROW(vmSymbols::java_lang_IllegalMonitorStateException());
  if( !mon ) {                  // Stack object; known no-waiters
    TickProfiler::meta_tick(vmlock_notify_nobody_home_tick, 0);
    inflate(obj,INF_WAIT);      // inflate it, expecting a waiter and a monitor shortly, but too late for this guy
    return;
  }
mon->notify_all();
}


// --- wait_impl
void ObjectSynchronizer::wait_impl(objectRef ref, jlong millis, bool interruptible, TRAPS) {
  assert0( !ref.is_stack() );
  if( ref.is_null() ) Unimplemented(); // throws npe?
  markWord *mark = ref.as_oop()->mark();
  if( !mark->is_self_locked() )
    THROW(vmSymbols::java_lang_IllegalMonitorStateException());
  ObjectMonitor *mon = mark->has_monitor() ? mark->monitor() : inflate(ref,INF_WAIT);
  mon->wait_java(millis, interruptible, THREAD);
}

// --- notifyall
// Notify (wakeup) all waiting threads
void ObjectLocker::notify_all( TRAPS ) {
  // No correctness checks, because ObjectLockers are VM things and are used properly.  ;-)
  // Stack objects can never have waiters.
  if( _h.as_ref().is_stack() ) return;

  // Since this ObjectLocker locked the object, there's no chance of an
  // IllegalMonitorException.  
  markWord *mark = _h()->mark();
  // Having waiters always inflates a lock, so if this object is biased-locked
  // then it has no waiters and so there's no one to notify.
  if( mark->is_self_spec_locked() ) return;

  // Better be inflated with a monitor:
ObjectMonitor*mon=mark->monitor();
mon->notify_all();
}


// --- summarize_sma_status_xml ----------------------------------------------
void ObjectSynchronizer::summarize_sma_status_xml(xmlBuffer *xb) {

}

void ObjectSynchronizer::lock_recursively( Handle lockObj ) {

  lock(lockObj(), INF_REENTER);    
  ObjectMonitor *new_mon = lockObj()->mark()->monitor();
  // In biased locking, only when the lock is contended we have a monitor and 
  // only then need to know the recursion count. If there is no monitor, return.
  // Also, if the monitor is pre-biased to current thread then dont need to count
  // recursion.
  if ( new_mon && !new_mon->is_self_spec_locked() ) {
    JavaThread* thread = JavaThread::current();
    int recursions = thread->count_locks(lockObj());
    new_mon->_recursion = recursions;
  }
  return;
}

void ObjectSynchronizer::unlock_recursively(oop obj){

  // See if the lock is pre-biased to this Thread
  markWord *mark = obj->mark();
  if( mark->is_self_spec_locked() ) return;

  // There MUST be a heavy monitor
ObjectMonitor*mon=mark->monitor();
  assert0( mon->owned_by_self() );

  // See if the lock is inflated and pre-biased to this Thread
  if( mon->is_self_spec_locked() ) return;

  // Leave recursion count at 1 and unlock
  mon->_recursion = 1;
mon->unlock();

  return;
}
