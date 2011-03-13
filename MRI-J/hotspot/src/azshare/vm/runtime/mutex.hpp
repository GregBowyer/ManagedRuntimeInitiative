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
#ifndef MUTEX_HPP
#define MUTEX_HPP


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

#include "allocation.hpp"
class JavaThread;
class methodOopDesc;

class AzLock:public CHeapObj{
  friend class MutexLockerAllowGC;        // allowed to call the lock_common_fastpath version directly to skip asserts in rare cases
  friend class MutexUnlocker_GC_on_Relock;// allowed to call the lock_common_fastpath version directly to skip asserts in rare cases
protected:
  // The OS structure used to sleep/block the current thread until it can acquire the lock.
  void* _os_lock;               // OS backing support: used to block the thread
  // see os-specific implementations...
  void sem_wait_for_lock_impl();
  void unlock_implementation();
  void make_os_lock();

  // The actual lock owner field, plus a count of threads contending for the
  // lock.  This one field is atomically set to determine who owns the lock.
  // The low bits are a reversible unique thread id; the high order bits are a
  // count of contending threads (that have called semaphore_wait), and thus
  // is a count times somebody needs to call semaphore_post.
  jlong volatile _lock;
  inline intptr_t reversible_tid  () const;
public:
  inline static int lock_offset() { return offset_of(AzLock,_lock); }
  inline intptr_t contention_count() const;
  inline void *os_lock_just_during_thread_start() const { return _os_lock; }
protected:

  // Acquire attempt on the lock.  If it succeeds, the current thread's TID is
  // jammed into the _lock field, preserving any existing contention counts.
  // If it fails, the contention count is raised and the lock is not acquired.
  // Caller is expected to do a semaphore_wait.
  friend class ObjectSynchronizer;
  inline bool try_lock( Thread *self );

  // Unlock the lock.  Returns false if the lock was not contended.  Returns
  // true if the lock was contended, AND the lock contention count is lowered.
  // Caller is expected to do a semaphore_post.
  inline bool try_unlock( );

  // Spin until this thread can acquire both this AzLock and the jvm_lock
void wait_for_lock_and_jvmlock(JavaThread*thread);

 public:
  enum {              // This is defined here to break a cycle with thread.hpp
    JVMLOCK_rank = 8,
    JVM_RAW_rank = 25
  };
  const int _rank;              // rank (to avoid/detect potential deadlocks)
  int _rpc;   // Return PC of lock-acquire site, for profiling lock hold times

  // Preserve useful information for crashes/debugging
#ifndef PRODUCT
  Thread* _last_owner;          // the last thread to own the lock, nice to have on a crash
  long _last_owner_unlock_tick; // Handy when we crash
#endif

  // The _next field is used to chain free Java ObjectMonitors together, and
  // to chain in-use Java ObjectMonitors (for deflation).  In debug mode only
  // it also holds a linked list of owned VM locks, so the lock-ranking
  // asserts can be checked.  Locks are never both VM locks and Java
  // ObjectMonitors so these two uses do not collide.  In-use AzLocks &
  // ObjectMonitors are crawled for GC (oops_do) and ARTA (hot lock
  // profiling).  Free ObjectMonitors are chained on a free list.  See
  // the longish discussion of _next field invariants in objectMonitor.hpp.
  AzLock* _next;   // Only valid in debug mode (VM locks) or for ObjectMonitors
AzLock*next()const{return _next;}

const char*_name;//Name of mutex

  const char* _locking_name;         // Locking_Name of mutex  
  const char* _waiting_name;	     // Waiting_Name of mutex
  
  int64_t _contended_lock_attempts;  // Count of times lock is taken after failing a lock
  int64_t _contended_lockers;        // Count of threads waiting on a lock 'the slow way' 
  int64_t _contended_lockers_total;  // Sum of _contended_lockers over time
  int64_t _contended_max_ticks;      // Max acquire-lock time
  int64_t _contended_total_ticks;    // Cumulative acquire-lock time
  int64_t _wait_count;               // Count of calls to wait()
  int64_t _wait_max_ticks;           // Longest wait() time
  int64_t _wait_total_ticks;         // Cumulative wait() times
  int64_t _acquire_tick;             // Tick at last acquire, used to compute hold times

  bool    _static_global_lock;

  // additional os specific details here...
  AzLock(const AzLock&);          // not defined; linker error to use these
  AzLock &operator=(const AzLock &rhs);
public:
  AzLock(int rank, const char *name, bool static_global);
  ~AzLock();
protected:
  // Common locking support
  void inline lock_common_fastpath ( Thread *self, bool allow_gc );
  void        lock_common_contended( Thread *self, bool allow_gc );
public:
  // Lock without safepoint check (e.g. without release the jvm_lock).  Only
  // allowed for locks of rank < jvm_lock or for non-Java threads.  Should
  // ONLY be used by safepoint code and other code that is guaranteed not to
  // block while running inside the VM.
  inline void lock_can_block_gc(Thread *self);

  // Lock acquire, allowing a gc.  This call acts "as if" it releases the
  // jvm_lock and re-acquires after it acquires this lock.  It really acquires
  // both locks nearly simultaneously (by a lock-first-try-lock-second approach;
  // if the 2nd lock is not available it reverses the locks and tries again).
  // Only allowed for JavaThreads taking locks of rank > jvm_lock.
  inline void lock_allowing_gc(JavaThread *jself);

  // Unlock the lock!
  inline void unlock();
  // Check for self ownership
  inline bool owned_by_self() const;

  // The locked state comes & goes, so this is a best-effort only value
  bool is_locked() const { return reversible_tid()!=0; }

  // Current owner - NOT MT-safe!  Owners come and go.  Can only be used to
  // guarantee that the current running thread owns the lock.  Must strip
  // off the low order bit, which indicates contention on the lock.
  inline Thread* owner() const;
  bool is_contended() const { return contention_count()==0; }

  // Record lock hold time
  void record_hold_time( );
  void delist_from_locksites() const;
  void register_vm_lock() const;
  void deregister_vm_lock() const;

  // Support for JVM_RawMonitorEnter & JVM_RawMonitorExit. These can be called by
  // non-Java thread. (We should really have a RawMonitor abstraction)
  inline void jvm_raw_lock();
  inline void jvm_raw_unlock();
  const char *name() const                  { return _name; }
  const char *locking_name() const          { return _locking_name; }
  const char *waiting_name() const          { return _waiting_name; }

  void print_on_error(outputStream* st) const;
#ifdef ASSERT
  void print_deadlock(Thread *self) const;
#endif

#ifndef PRODUCT
  void print_on(outputStream *st) const;
#endif
  
  // Gather lock contention info for this VM lock
  int gather_lock_contention(struct lock_contention_data *, int cnt);

  // TTY Output
static void print_lock_hold_times(outputStream*st);
  static void print_lock_hold_times(const char *name, outputStream *st);

  bool is_static_global_lock()              { return _static_global_lock; }

  // XML Output
  static void print_to_xml_lock_hold_times(xmlBuffer *xb);
  static void print_to_xml_lock_hold_times(const char *name, xmlBuffer *xb);
};

// ---------------------------------------------------------------------------
// A WaitLock is an AzLock with a built in condition variable. It allows a thread
// to temporarily to give up the lock and wait on the lock until it is notified.
class WaitLock: public AzLock {
protected:

  // A set of counters, all updated atomically together.  
  // -- A count of pending waiters; threads calling wait() increment this
  // before releasing the lock.  Thus the counter is bumped before a thread
  // wishing to notify() can get the lock and call notify.
  // -- A count of pending notifies; threads calling notify() increment this
  // before releasing the lock.  Thus the counter is bumped before another
  // thread can notify() again, preventing endless notifies from stacking up.
  // -- An 'epoch' counter, to help avoid self-notification
  // -- A thread coming out of wait decrements both W & N counters atomically.

  // Invariants: 0 <= W <= N
  // Timeline for wait_micros(): 
  //    C/W++; // Note that we must 'wait' even if N>0 (even if there is a
  //           // pending notify) because that notify predates this call to
  //           // wait and is intended to wakeup earlier threads.
  //    unlock; 
  //    wait_micros_impl(); 
  //    if( N==0 || C' == C ) retry;
  //    W--/N--  // atomically 'consume' a notify with a waiting thread.
  //    relock;

  // Timeline for notify():
  //    if( W>0 ) { C++/N++; wakeup; }
  int64 _WNE;                   // wait/notify/epoch counters

  // Take apart or build up *a* WN word, one that has been pre-read only once.
  static inline int waiters( int64 WNE ) { return (WNE>> 0)&0xFFFF; }
  static inline int notifys( int64 WNE ) { return (WNE>>16)&0x00FF; }
  static inline int epoch  ( int64 WNE ) { return (WNE>>24)&0x00FF; }
  static inline int64 WNE( int W, int N, int E ) { return ((int64)E<<24) | ((int64)N<<16) | (int64)W; }
  int64 CAS_WNE( int64 WNEold, int64 WNEnew );

  // Implemented in OS-specific code.
  friend class os;
  int wait_micros_impl(long timeout_micros, int64 expected_WN);
  void notify_impl( int all );
  
public:
  WaitLock(int rank, const char *name, bool static_global);
  ~WaitLock();

  inline int waiters() const { return waiters(_WNE); }

  // 'wait' & 'notify' are not allowed for JavaThreads with locks of rank less
  // than jvm_lock because that means a JavaThread would be 'waiting' while
  // holding the jvm_lock - blocking GC.

  // Default time is forever (i.e, zero). Returns true if it times-out, otherwise false. 
  // Must be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  inline bool wait() { return wait_micros(0, false); }
  // Set honor_interrupts to true to pay attention to the java-level is_interrupted 
  //    state, which if set will cause wait() calls to exit early.
  // Set honor_interrupts to false to ignore the is_interrupted state and always 
  //    end up wait()'ing.
  // Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  // Returns false if notify'd, true if timed out or interrupted
  bool wait_micros(long timeout_micros, bool honor_interrupts);
  void notify();
  void notify_all();
};

// ---------------------------------------------------------------------------
// A Striped Mutex - replicated locks conceptually mimic'ing a single lock
// covering a collection of unrelated items, but striped to lower lock
// contention.  The creator must define the hash function to map from the
// items to the lock.  The hash function must be stable over time - hence
// using the address of an oop won't work, because a GC cycle can move the
// oop.  Specifically: when striping across methodOop's you can't use the raw
// methodOop value.
class StripedMutex:public CHeapObj{
protected:
  const int _stripe_factor;
  AzLock** const _azlocks;       // An array of lock pointers
  StripedMutex( int rank, const char *name, int stripe_factor );
  ~StripedMutex();
public:
  bool any_locked() const;
};

// address-striping, for CompiledIC's
class AddressStripedMutex  : public StripedMutex {
public: 
  AzLock &get_lock( const address pc ) { return *_azlocks[(((intptr_t)pc)>>3) & (_stripe_factor-1)]; }
  AddressStripedMutex( int rank, const char *name, int stripe_factor ) : 
    StripedMutex(rank,name,stripe_factor) { }
};

// method-striping, for OSR-list lock (really _nm_list lock)
class MethodStripedMutex  : public StripedMutex {
public: 
  AzLock &get_lock( const methodOopDesc *moop ); // defined in cpp to break an include-header cycle
  MethodStripedMutex( int rank, const char *name, int stripe_factor ) : 
    StripedMutex(rank,name,stripe_factor) { }
};


// ---------------------------------------------------------------------------
/*
 * Per-thread blocking support for JSR166. See the Java-level
 * Documentation for rationale. Basically, park acts like wait, unpark
 * like notify.
 * 
 * 6271289 --
 * To avoid errors where an os thread expires but the JavaThread still
 * exists, Parkers are immortal (type-stable) and are recycled across 
 * new threads.  This parallels the ParkEvent implementation.
 * Because park-unpark allow spurious wakeups it is harmless if an
 * unpark call unparks a new thread using the old Parker reference.  
 *
 * In the future we'll want to think about eliminating Parker and using
 * ParkEvent instead.  There's considerable duplication between the two
 * services.  
 *
 */

extern WaitLock Safepoint_lock;

class Parker : public WaitLock {
private:
  volatile int _counter ; 
  Parker * FreeNext ;
  JavaThread * AssociatedWith ; // Current association
  
public:
  Parker() : WaitLock(Safepoint_lock._rank, "Park Semaphore", false) {
    _counter = 0 ;
    FreeNext       = NULL ;
    AssociatedWith = NULL ;
  }

protected:
  ~Parker() {
  } 

public:
  // For simplicity of interface with Java, all forms of park (indefinite,
  // relative, and absolute) are multiplexed into one call.
  void park(bool isAbsolute, jlong time);
  void unpark();

  // Lifecycle operators
  static Parker * Allocate (JavaThread * t) ;
  static void Release (Parker * e) ;
private:
  static Parker * volatile FreeList ;
  static volatile int ListLock ;
};

#endif // MUTEX_HPP
