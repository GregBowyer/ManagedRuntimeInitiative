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


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

#include "atomic.hpp"
#include "collectedHeap.hpp"
#include "methodOop.hpp"
#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "oop.hpp"
#include "ostream.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"
#include "threadCounters.hpp"
#include "vframe.hpp"
#include "vmTags.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

#ifdef AZ_PROFILER
#include <azprof/azprof_demangle.hpp>
#endif // AZ_PROFILER

void AzLock::lock_common_contended( Thread *self, bool allow_gc ) {
  // Take the lock 'the hard way'.  We expect to block in the OS here.
  // Record the lock contention.  The contention count on the _lock is
  // already incremented; we must call semaphore_wait eventually.

  // Is this really a Java ObjectMonitor?
  // Then find the stack trace to the lock and account for it.
  ContendedStackEntry *tick_addr = NULL;
  if( _rank == JVM_RAW_rank+1 ) {
    assert0( self->is_Java_thread() );
    int stack_trace[2*ProfileLockContentionDepth];
    int hash = 0, d = 0;
    vframe vf((JavaThread*)self);
    while( !vf.done() && d < ProfileLockContentionDepth ) {
      int method_id = stack_trace[2*d+0] = vf.method()->method_id(); 
      int bci       = stack_trace[2*d+1] = vf.bci();
      hash = (hash*13) + method_id + (bci<<4);
      vf.next(); d++;
    }
    for( ; d < ProfileLockContentionDepth; d++)
      stack_trace[2*d] = stack_trace[2*d+1] = 0;
    tick_addr = ((ObjectMonitor*)this)->object()->klass()->klass_part()->add_contention_stack(hash, stack_trace);
  }

  elapsedTimer timer;
  Atomic::add_ptr(1,&_contended_lockers);
  timer.start();
  TickProfiler::meta_tick(vmlock_blocked_tick, (intptr_t)this);
  assert0( !allow_gc || self->is_Java_thread() );

  if( allow_gc && ((JavaThread*)self)->jvm_locked_by_self() ) {
    // We own the jvm_lock, but got blocked trying to get the AzLock.
    // Release the jvm_lock and reacquire after the AzLock.
    // Does not return until it has acquired BOTH locks.
    wait_for_lock_and_jvmlock((JavaThread*)self);
  } else {
    if( _rank == JVM_RAW_rank+1 ) {
      assert0( self->is_Java_thread() );
JavaThread*jt=(JavaThread*)self;
      jt->hint_blocked(locking_name());
      jt->set_current_pending_monitor((ObjectMonitor*)this);
    }
    do { sem_wait_for_lock_impl();
    // Attempt to acquire the AzLock.  Raises the contention count if it
    // fails, requiring another call to semaphore_wait.
    } while( !try_lock(self) );
    if( _rank == JVM_RAW_rank+1 ) {
      assert0( self->is_Java_thread() );
JavaThread*jt=(JavaThread*)self;
jt->hint_unblocked();
jt->set_current_pending_monitor(NULL);
    }
  }

  // Now that we own the lock, we can record more stats without races.
  _contended_lock_attempts++;
  _contended_lockers_total += _contended_lockers;
  // Alas, _contended_lockers must be set before waiting on the lock, so
  // many threads race to set it, so it must be inc'd & dec'd with Atomic ops.
  Atomic::add_ptr(-1,&_contended_lockers);
  timer.stop();
  int64_t ticks = timer.ticks();
  // Since we own the lock, we can increment these counters without atomic ops.
  _contended_total_ticks += ticks;
  _contended_max_ticks = MAX2(ticks,_contended_max_ticks);
if(tick_addr!=NULL){
    tick_addr->add_blocked_ticks(ticks);
  }
}

// --- wait_for_lock_and_jvmlock ---------------------------------------------
// Spin until we can acquire BOTH the AzLock and the jvm_lock; we hold only
// the jvm_lock right now.  We up'd the contention count, so we MUST call
// semaphore_wait.
void AzLock::wait_for_lock_and_jvmlock(JavaThread *self) {
  debug_only(int cnt=0);
  while( true ) {
    // Release JVM lock before blocking on OS lock
    self->jvm_unlock_self();
    // We now own neither the AzLock nor the jvm_lock.
    // Convenience for profiler: signal we are blocked
    self->hint_blocked(locking_name());
    self->set_current_pending_monitor((ObjectMonitor*)this);

    // Grab the underlying OS semaphore, or block in the attempt.
    // This is our required call to semaphore_wait.
    do { sem_wait_for_lock_impl();
    // Attempt to acquire the AzLock.  Raises the contention count if it
    // fails, requiring another call to semaphore_wait.
    } while( !try_lock(self) );
    self->hint_unblocked();
self->set_current_pending_monitor(NULL);
    // Got the AzLock, but not the jvm_lock

    // were we externally suspended while we were waiting?
    if (self->jvm_lock_self_attempt())
      return;			// Got both locks!

    // We have been granted the contended AzLock, but while we were waiting
    // another thread suspended us.  We don't want to enter the AzLock while
    // suspended because that would surprise the thread that suspended us.
    // For all intents and purposes we are polling before we take the lock.
    if( try_unlock() )          // unlock
      unlock_implementation();  // Was contended; wake up somebody else
    // We now own neither the AzLock nor the jvm_lock.

    // Block until we can acquire the jvm_lock.
    self->jvm_lock_self_or_suspend();
    // We now own the jvm_lock, but not the AzLock
    if( try_lock(self) )
      return;                   // got both locks
    // We hold only the jvm_lock, and we have up'd the contention count.

debug_only(cnt++);
assert(cnt<100000,"I've been spinning a long time waiting for the _jvm_lock to become free or the VM to request me to self-suspend");
  }
}

// --- chk_lock --------------------------------------------------------------
#ifdef ASSERT
bool Thread::chk_lock(AzLock *m, bool allow_gc) {
  if( !_owned_locks ) return true;
  if( _owned_locks == m ) return true; // Generally, nested locking is OK
  int rank = _owned_locks->_rank; // Get lowest ranked lock
  // The _jvm_lock is ranked but not represented with a standard mutex.
  // Explicitly check for ownership and compute lowest rank.  If "allow_gc"
  // then jvmlock is allowed out of order; it will be released & reacquired
  // to preserve lock order.
  if( !allow_gc && rank > AzLock::JVMLOCK_rank && is_Java_thread() && ((JavaThread*)this)->jvm_locked_by_self() ) 
    rank = AzLock::JVMLOCK_rank;
  return m->_rank < rank;       // True if ranks are well ordered
}

void Thread::push_lock(AzLock *m) {
  assert0( m->owner() == this );
  if( m->_rank > AzLock::JVM_RAW_rank ) return; // Do not pop ObjectMonitors
  m->_next = _owned_locks;      // Standard linked-list push
_owned_locks=m;
}
void Thread::pop_lock(AzLock *m) {
  assert0( m->owner() == this );
  assert0( _owned_locks == m || m->_rank > AzLock::JVM_RAW_rank );
  if( m->_rank > AzLock::JVM_RAW_rank ) return; // Do not pop ObjectMonitors
  _owned_locks = m->_next;
  m->_next = (AzLock*)0xdeadbeef;
}
#endif

// Ensure each locksite key is unique via a hashtable of RPC's
struct locksite {
  AzLock *_mutex;                // Lock 
  int64_t _ticks, _takes;       // Count of ticks & takes at this RPC
  int64_t _max;                 // Max hold ticks
  jint _rpc;                    // RPC denoting lock site

  const char *name( char *buf ) {
    int offset;
    size_t size;
    char mangled[256];
    if( !os::dll_address_to_function_name((address)_rpc, mangled, sizeof(mangled), &offset, &size) ) 
return"unknown address";
#ifdef AZ_PROFILER
    char demangled[1024];
sprintf(buf,"%s+%d",
            azprof::Demangler::demangle(mangled, demangled, sizeof(demangled)) ? mangled : demangled,
            offset);
#else // !AZ_PROFILER
sprintf(buf,"%s+%d",mangled,offset);
#endif // !AZ_PROFILER
    return buf;
  }
};
static struct locksite locksites[512]; // power-of-2 hash table
static const int locksites_len = sizeof(locksites)/sizeof(locksites[0]);

// Called while holding the lock
void AzLock::record_hold_time( ) {
  assert0( _rpc );              // Failed to set _rpc on some locking path?

  int idx = _rpc & (locksites_len-1);
  int del_idx = -1;
  while( locksites[idx]._rpc != _rpc &&  // Hash, looking for a hit
         locksites[idx]._mutex != this ) {
    int oldrpc = locksites[idx]._rpc;
    if( oldrpc == _rpc && locksites[idx]._mutex == this ) 
      break;                    // Found matching rpc+mutex
    if( oldrpc == 1 && del_idx == -1 )
      del_idx = idx;            // Capture first deleted sentinel
    if( oldrpc == 0 ) {         // Found empty slot, so we missed in the table
      if( del_idx != -1 ) {     // Prior empty slot in table?
        idx = del_idx;          // Retry over any seen deleted sentinel
        del_idx = -1;           // Clear the sentinel, in case we must retry yet again
        oldrpc = 1;             // Expecting to see a deleted-sentinel
      } 
      if( Atomic::cmpxchg(_rpc,&locksites[idx]._rpc,oldrpc) == oldrpc ) {
        locksites[idx]._mutex = this; // Got it!
        break;
      }
    }
    // Reprobe with any odd (non-power-of-2) value
    idx = (idx+(_rpc|1))&(locksites_len-1); 
  }
  locksites[idx]._takes++;
  int64_t cur_tick = os::elapsed_counter();
  int64_t holdticks = cur_tick - _acquire_tick;
  locksites[idx]._ticks += holdticks;
  if( holdticks > locksites[idx]._max )
    locksites[idx]._max = holdticks;
  debug_only(_last_owner_unlock_tick = cur_tick);
}

// Called from AzLock::~AzLock destructor
void AzLock::delist_from_locksites() const {
for(int i=0;i<locksites_len;i++)
    if( locksites[i]._mutex == this ) {
      locksites[i]._takes = locksites[i]._ticks = locksites[i]._max = 0;
      Atomic::write_barrier();
locksites[i]._mutex=NULL;
      Atomic::write_barrier();
      locksites[i]._rpc = 1;    // Deleted sentinel
    }
}

extern "C" {
  int sort_helper2(const void *a, const void *b) {
    int A = *(int *)a;
    int B = *(int *)b;
    int64_t x = 0;
    if( locksites[A]._max != locksites[B]._max ) {
      x = locksites[B]._max - locksites[A]._max;
    } else {
      if( locksites[A]._takes == 0 ) return locksites[B]._takes != 0;
      if( locksites[B]._takes == 0 ) return -1;
      // Simple math optimization.
      // return   locksites[B]._ticks/locksites[B]._takes - locksites[A]._ticks/locksites[A]._takes;
      //int64_t x=locksites[B]._ticks*locksites[A]._takes - locksites[A]._ticks*locksites[B]._takes;
      x=locksites[B]._ticks - locksites[A]._ticks;
    }
    if( x==0 ) return 0;
    return x<0 ? -1 : 1;
  }
}

//----- print tty
static void print_lock_hold_times_impl(int *data, int cnt, outputStream* st) {
  if( cnt==0 ) return;
  qsort(data, cnt, sizeof(data[0]), sort_helper2); 
  if (!st) st = tty;
st->print_cr("=== VM Lock Hold Times ===");
  extern long First_Tick;
  long vm_uptime_ticks = os::elapsed_counter() - First_Tick;
  long vm_uptime_millis = os::ticks_to_millis(vm_uptime_ticks);
  long t = 0;
  long m = 0;
  long other_ticks = 0;
  long other_takes = 0;
  long other_max   = 0;
  bool singleton_lock = true;
  for( int i=0; i<cnt; i++ ) {
    int j = data[i];
    if( locksites[j]._takes == 0 ) break;
    t += locksites[j]._ticks;   // Compute ticks across all lock sites
    if( m < locksites[j]._max ) m = locksites[j]._max;
    long total_millis = os::ticks_to_millis(locksites[j]._ticks);
    long max_millis = os::ticks_to_millis(locksites[j]._max);
    if( total_millis < 10 ) { // Cutoff locks with 10ms or less total hold time
      other_ticks += total_millis;
      other_takes += locksites[j]._takes;
      other_max = MAX2(other_max,max_millis);
      break;
    }
    char buf[1024];
    locksites[j].name(buf);
    st->print_cr("%30.30s hold=%5ldms (%2ld%%)  takes=%6ld  maxhold=%4ldms at %s",locksites[j]._mutex->name(),total_millis,
                    total_millis*100/vm_uptime_millis,locksites[j]._takes,
                    max_millis,buf);
    // For hold times on a single lock, we can also print the time the lock was free
    if( locksites[data[0]]._mutex != locksites[j]._mutex ) 
      singleton_lock = false;
  }
  if( other_ticks ) {
      st->print_cr("%30.30s hold=%5ldms (%2ld%%)  takes=%6ld  maxhold=%4ldms at %s","other",other_ticks,
                    other_ticks*100/vm_uptime_millis,other_takes,other_max,"various");
  }

  if( singleton_lock ) {
    long total_millis = os::ticks_to_millis(vm_uptime_ticks - t);
    long max_millis = os::ticks_to_millis(m);
    st->print_cr("%22.22s hold=%5ldms (%ld%%)  takes=%5d  maxhold=%5ldms at %s","",total_millis,
                    total_millis*100/vm_uptime_millis,0,max_millis,"UNLOCKED");
  }
}

// Print for all locks
void AzLock::print_lock_hold_times(outputStream*st){
  int data[locksites_len];
for(int i=0;i<locksites_len;i++)
data[i]=i;
  print_lock_hold_times_impl(data,locksites_len,st);
}

// Print for just this one lock
void AzLock::print_lock_hold_times(const char *vm_lock_name, outputStream *st) {
  int data[locksites_len];
  int cnt = 0;
for(int i=0;i<locksites_len;i++)
    if( locksites[i]._mutex && !strcmp(locksites[i]._mutex->_name,vm_lock_name) )
      data[cnt++] = i;
  print_lock_hold_times_impl(data,cnt,st);
}


void AzLock::print_on_error(outputStream*st)const{
  st->print("[" PTR_FORMAT, this);
st->print("/"PTR_FORMAT,_os_lock);
  st->print("] %s", _name);
st->print(" - owner thread: "PTR_FORMAT,owner());
}

#ifdef ASSERT
void AzLock::print_deadlock(Thread *self) const {
ResourceMark rm(self);
tty->print("--- Potential deadlock: %s - rank: %d acquired out of order. ",_name,_rank);
self->print_owned_locks();
  tty->flush();
tty->print("--- Potential deadlock in thread %s",self->name());
}
#endif // ASSERT

// NOTE: It's enabled in product builds because lock contention is hard to
// debug in product builds.
int AzLock::gather_lock_contention(struct lock_contention_data *data, int cnt) {
  if( !this ) return cnt;	// Some locks are optional
  if( (_contended_lock_attempts | _contended_lockers_total | _wait_count) == 0 )
    return cnt;                 // No contention on this lock
  data[cnt]._blocks           = _contended_lock_attempts;
  data[cnt]._cum_blocks       = _contended_lockers_total;
  data[cnt]._max_ticks        = _contended_max_ticks;
  data[cnt]._total_ticks      = _contended_total_ticks;
  data[cnt]._wait_count       = _wait_count;
  data[cnt]._wait_max_ticks   = _wait_max_ticks;
  data[cnt]._wait_total_ticks = _wait_total_ticks;
  data[cnt]._kid               = 0;
  data[cnt]._name             = _name;
  return cnt+1;
}


//----- xml output
static void print_to_xml_lock_hold_times_impl(int *data, int cnt, xmlBuffer *xb) {
  if( cnt==0 ) return;
  qsort(data, cnt, sizeof(data[0]), sort_helper2); 
assert(xb,"print_to_xml_lock_hold_times_impl() called with NULL xmlBuffer pointer.");
  extern long First_Tick;
  long vm_uptime_ticks = os::elapsed_counter() - First_Tick;
  long vm_uptime_millis = os::ticks_to_millis(vm_uptime_ticks);
  long t = 0;
  long m = 0;
  long other_ticks = 0;
  long other_takes = 0;
  long other_max   = 0;
  bool singleton_lock = true;
  for( int i=0; i<cnt; i++ ) {
    int j = data[i];
    if( locksites[j]._takes == 0 ) break;
    t += locksites[j]._ticks;   // Compute ticks across all lock sites
    if( m < locksites[j]._max ) m = locksites[j]._max;
    long total_millis = os::ticks_to_millis(locksites[j]._ticks);
    long max_millis = os::ticks_to_millis(locksites[j]._max);
    if( total_millis < 10 ) { // Cutoff locks with 10ms or less total hold time
      other_ticks += total_millis;
      other_takes += locksites[j]._takes;
      other_max = MAX2(other_max,max_millis);
      break;
    }
    char buf[1024];
    locksites[j].name(buf);
    {
      xmlElement xl(xb,"vm_lock_hold");
      xb->name_value_item("millis", total_millis);
      xb->name_value_item("uptime", vm_uptime_millis);
      xb->name_value_item("takes", locksites[j]._takes);
      xb->name_value_item("max", max_millis);
      xb->name_value_item("function",  buf);
    }
    // For hold times on a single lock, we can also print the time the lock was free
    if( locksites[data[0]]._mutex != locksites[j]._mutex ) 
      singleton_lock = false;
  }
  if( other_ticks ) {
    xmlElement xl(xb,"vm_lock_hold");
    xb->name_value_item("millis", other_ticks);
    xb->name_value_item("uptime", vm_uptime_millis);
    xb->name_value_item("takes", other_takes);
    xb->name_value_item("max", other_max);
    xb->name_value_item("function",  "other");
  }

  if( singleton_lock ) {
    long total_millis = os::ticks_to_millis(vm_uptime_ticks - t);
    long max_millis = os::ticks_to_millis(m);
    {
      xmlElement xl(xb,"vm_lock_hold");
      xb->name_value_item("millis", total_millis);
      xb->name_value_item("uptime", vm_uptime_millis);
      xb->name_value_item("max", max_millis);
      xb->name_value_item("function",  "UNLOCKED");
    }
  }
}


// Output to xmlBuffer for all locks
void AzLock::print_to_xml_lock_hold_times(xmlBuffer *xb) {
  int data[locksites_len];
for(int i=0;i<locksites_len;i++)
data[i]=i;
  print_to_xml_lock_hold_times_impl(data,locksites_len,xb);
}

// Output to xmlBuffer for just this one lock
void AzLock::print_to_xml_lock_hold_times(const char *vm_lock_name, xmlBuffer *xb) {
  int data[locksites_len];
  int cnt = 0;
for(int i=0;i<locksites_len;i++)
    if( locksites[i]._mutex && !strcmp(locksites[i]._mutex->_name,vm_lock_name) )
      data[cnt++] = i;
  print_to_xml_lock_hold_times_impl(data,cnt,xb);
}


// ----------------------------------------------------------------------------------

StripedMutex::StripedMutex( int rank, const char *name, int stripe_factor ) : 
  _stripe_factor(stripe_factor), 
  _azlocks(NEW_C_HEAP_ARRAY(AzLock*,stripe_factor)) 
{
  assert0( stripe_factor > 0 && stripe_factor <= 1024 && is_power_of_2(stripe_factor) );
for(int i=0;i<stripe_factor;i++){
    char buf[1024];
sprintf(buf,"%s%d_lock",name,i);
    _azlocks[i] = new AzLock(rank, strdup(buf), false);  // buf will be free'd in ~StripedMutex() 
  }

}

StripedMutex::~StripedMutex() {
for(int i=0;i<_stripe_factor;i++){
    const char *name = _azlocks[i]->_name;
    delete _azlocks[i];
    debug_only(_azlocks[i] = (AzLock*)0xdead10c5);
    free((void*)name);
  }
  FREE_C_HEAP_ARRAY(AzLock*,_azlocks);
  debug_only(*((AzLock***)&_azlocks) = (AzLock**)0xdead10c3);
}

// Any locks in the collection currently locked?
bool StripedMutex::any_locked() const {
for(int i=0;i<_stripe_factor;i++)
    if( _azlocks[i]->is_locked() )
      return true;
  return false;
}

// Return the striped lock for a methodOop.  Broken out here to break an
// include-header cycle.  Do not read anything from the constMethodOop, as
// it's not readable during portions of the GC cycle.
AzLock &MethodStripedMutex::get_lock( const methodOopDesc *moop ) { 
  return *_azlocks[(moop->method_size()+moop->max_locals()+moop->max_stack()) & (_stripe_factor-1)]; 
}

//=============================================================================
// --- CAS_WN ----------------------------------------------------------------
// Atomically CAS the waiter/notify pair
int64 WaitLock::CAS_WNE( int64 WNEold, int64 WNEnew ) { 
  assert0( waiters(WNEold) >= notifys(WNEold) && notifys(WNEold) >= 0 );
  assert0( waiters(WNEnew) >= notifys(WNEnew) && notifys(WNEnew) >= 0  );
  return Atomic::cmpxchg(WNEnew,(volatile jlong*)&_WNE,WNEold); 
}

// --- wait_micros -----------------------------------------------------------
// See largish comment in mutex.hpp.
// Timeline for wait_micros(): 
//    W++; // Note that we must 'wait' even if N>0 (even if there is a
//         // pending notify) because that notify predates this call to
//         // wait and is intended to wakeup earlier threads.
//    unlock; 
//    wait_micros_impl(); 
//    if( N==0 ) retry;
//    W--/N--  // atomically 'consume' a notify with a waiting thread.
//    relock;
bool WaitLock::wait_micros(long timeout_micros, bool honor_interrupts) {
  Thread* thread = Thread::current();
  assert0(owner() == thread );
  if( honor_interrupts && os::is_interrupted(thread) ) return true;

  // Java Object wait/notify puts the stat gathering in a per-Klass place.
  int kid = (_rank > JVM_RAW_rank) ? ((ObjectMonitor*)this)->kid() : 0;
  if( kid ) Klass::cast(KlassTable::getKlassByKlassId(kid).as_klassOop())->_wait_count++;
  else                                                                     _wait_count++;

  // We are now counted as a waiter.  Atomically reads the Epoch as well.
  int64 WNEold, WNEnew;
  do { 
    WNEold = *(volatile int64*)&_WNE; // Re-read on CAS contention
    WNEnew = WNE(waiters(WNEold)+1,notifys(WNEold),epoch(WNEold));
  } while( CAS_WNE(WNEold,WNEnew) != WNEold );
  const int wait_epoch = epoch(WNEold);

  // Record the time we start the wait process
jlong starttick=os::elapsed_counter();
  elapsedTimer timer;
  timer.start(starttick);

  unlock();

  // If we are holding the jvm_lock, we should release it before
  // blocking lest we block a GC.
  JavaThread *jt = thread->is_Java_thread() ? (JavaThread*)thread : NULL;
  if( jt && !jt->jvm_locked_by_self() ) jt = NULL; // Was not holding jvm_lock, so do not screw with it
  if( jt ) {
    // Not polite to 'wait' on a low-rank lock while holding the jvmlock &
    // preventing GC.  High-rank locks will automatically release the jvmlock.
    assert0( _rank > JVMLOCK_rank );
    // If we own other locks while waiting, we might trigger a Safepoint
    // attempt while re-acquiring the jvm_lock - which automatically takes
    // the Threads_lock.
    assert0( !jt->owned_locks() || jt->owned_locks()->_rank > Threads_lock._rank );
    // Release the JVM lock while blocked
    jt->jvm_unlock_self();    // Fast & easy!
    // Make stack walkable since we expect to block anyways
    jt->hint_blocked(waiting_name());
    // Used by os::interrupt and deadlock detection help
    if( kid ) jt->set_current_waiting_monitor((ObjectMonitor*)this);
  }

  TickProfiler::meta_tick(kid ? objectmonitor_wait_block_tick : vmlock_wait_tick, (intptr_t)this, kid);

  // ---
  // Spin in case of spurious wakeups
  bool exit_status;
  while( true ) {
    // Check for timeout.  Computes remaining time to wait.
    long remaining_micros = 0;
    if( timeout_micros>0 ) {
      timer.stop(); timer.start(timer.start_ticks());
      remaining_micros = timeout_micros - timer.microseconds();
      if( remaining_micros <= 0 )  
        remaining_micros = -1;  // Timed out; no more wait_micros_impl please
    }

    // Attempt to consume a notify.  Spin until either we consume a notify or
    // we figure out one is not available.  If we consume a notify we
    // atomically lower the waiter count also.
    bool exit_outer_loop = false;
    do { 
      WNEold = *(volatile int64*)&_WNE; // Read once per inner loop
      if( epoch(WNEold) != wait_epoch && notifys(WNEold) > 0 ) { // Epoch moved forward?
        // Epoch moved forward, so we can 'eat' a notify.
        // Lower both waiters and notifys count.
        WNEnew = WNE(waiters(WNEold)-1,notifys(WNEold)-1,epoch(WNEold));
        exit_status = false;
        exit_outer_loop = true;

        // If interrupted, quit spin/waiting
        // If timed out, also quit spin/waiting
      } else if( (honor_interrupts && os::is_interrupted(thread)) || remaining_micros < 0 ) {
        // We only get here if we cannot 'eat' a notify, either because one
        // hasn't appeared or the one that is there is in the wrong epoch.
        assert0( waiters(WNEold) > notifys(WNEold) );
        // Lower just the waiters count.
        WNEnew = WNE(waiters(WNEold)-1,notifys(WNEold),epoch(WNEold));
        exit_status = true;
        exit_outer_loop = true;

      } else {
        // Spurious wakeup.  Skip out of the CAS loop and retry the spurious loop.
        exit_outer_loop = false;
        break;
      }

      // Attempt to CAS!
    } while( CAS_WNE(WNEold,WNEnew) != WNEold );

    // On a successful CAS we can break out of the spurious/spin loop
    if( exit_outer_loop ) break;

    // Do the wait (but only if not timed out).
    if( remaining_micros >= 0 ) {
      int err = wait_micros_impl(remaining_micros,WNEold);
    }

  } // ---
    
  // No longer waiting, start reacquiring locks.
  if( kid ) jt->set_current_waiting_monitor(NULL); // No longer waiting
  // Retake the JVM lock if we held it previously.  We do not own the OS
  // lock, so it is OK to block here while we retake the JVM lock.
  if( jt ) jt->jvm_lock_self();

  if( kid )                   // More counters?  For counting wait-times
    jt->thread_counters()->add_object_wait_ticks(timer.ticks());
  timer.stop();           // Done spending time in 'wait', now it's 'lock' instead

  // Acquire the OS lock
  TickProfiler::meta_tick(kid ? objectmonitor_wait_wakeup_tick : vmlock_wakeup_tick, (intptr_t)this);
  lock_common_fastpath(thread,jt!=NULL);
  GET_RPC;                  // Change locksite, so when we finally reclaim 
  _rpc = RPC;               // the lock we record lock hold times correctly.
  if( jt ) jt->hint_unblocked(); // Profile hint

  // Bump stats under lock
  int64_t ticks = timer.ticks(); // Bump counters under lock
  if( kid ) {                    // Object.wait?
    Klass *k = Klass::cast(KlassTable::getKlassByKlassId(kid).as_klassOop());
    k->_wait_total_ticks += ticks;
    k->_wait_max_ticks = MAX2(ticks,k->_wait_max_ticks);
  } else {
    _wait_total_ticks += ticks;
    _wait_max_ticks = MAX2(ticks,   _wait_max_ticks);
  }

  return exit_status;
}

// --- notify ----------------------------------------------------------------
void WaitLock::notify() {
assert(owned_by_self(),"invariant - notify on unknown thread");
  int64 WNEold;
  while( true ) {               // Spin on CAS contention
    WNEold = *(volatile int64*)&_WNE;          // Read status once
    if( waiters(WNEold) == notifys(WNEold) ) { // All waiters already notified
      TickProfiler::meta_tick(vmlock_notify_nobody_home_tick, (intptr_t)this);
      return;
    }
    if( notifys(WNEold)+1>=256 )
Unimplemented();//the 1-byte count overflows; must do some yields
    if( CAS_WNE( WNEold, WNE(waiters(WNEold),notifys(WNEold)+1,epoch(WNEold)+1) )==WNEold ) 
      break;
  }
  TickProfiler::meta_tick(vmlock_notify_tick, (intptr_t)this);
  notify_impl(1);               // notify one
}

// --- notify_all ------------------------------------------------------------
void WaitLock::notify_all() {
assert(owned_by_self(),"invariant - notify_all on unknown thread");
  int64 WNEold;
  while( true ) {               // Spin on CAS contention
    WNEold = *(volatile int64*)&_WNE;          // Read status once
    if( waiters(WNEold) == notifys(WNEold) ) { // All waiters already notified
      TickProfiler::meta_tick(vmlock_notify_nobody_home_tick, (intptr_t)this);
      return;
    }
    if( waiters(WNEold)>=256 )
      // will need to do no more than W-N notifies, in chunks (no more than
      // 255 outstanding at once) with yields (to let pending notifies get
      // processed by waiters), and handle racing other notifies doing some
      // wakeups for us while we yield.
Unimplemented();//the 1-byte count overflows; must do some yields
    if( CAS_WNE( WNEold, WNE(waiters(WNEold),waiters(WNEold),epoch(WNEold)+1) )==WNEold )
      break;
  }
  TickProfiler::meta_tick(vmlock_notify_tick, (intptr_t)this);
  notify_impl(waiters(WNEold)-notifys(WNEold)); // notify as many as needed
}
