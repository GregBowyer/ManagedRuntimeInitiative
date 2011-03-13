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
#ifndef GCLOCKER_HPP
#define GCLOCKER_HPP

#include "allocation.hpp"
#include "thread.hpp"

// The direct lock/unlock calls do not force a collection if an unlock
// decrements the count to zero. Avoid calling these if at all possible.

class GC_locker: public AllStatic {
 private:
static volatile intptr_t _lock_count;//number of other active instances

//_jni_state combines 3 pieces of state into one jlong, so they can be
  // updated atomically with CAS:
  //
  //   - bool _needs_gc       : A GC cycle didn't happen because of GC_locker.
  //   - bool _doing_gc       : unlock_critical() or pauseless GC is doing a GC
  //   - uint _jni_lock_count : number of active jni critical sections
  //
  // _jni_lock_count is bits 0 to 31
  // _needs_gc is bit 32
  // _doing_gc is bit 33
  static jlong _jni_state;

  // Accessors
  static jlong  lock_state() { return _jni_state; }
  static jlong* state_addr() { return &_jni_state; }
  
  static bool   needs_gc    (jlong state)         { return 0LL != ( state & 0x100000000LL ); }
  static bool   doing_gc    (jlong state)         { return 0LL != ( state & 0x200000000LL ); }
  static bool   block_for_gc(jlong state)         { return 0LL != ( state & 0x300000000LL ); }

  static jlong  set_needs_gc(jlong state)         { return state | 0x100000000LL; }
  static jlong  set_doing_gc(jlong state)         { return state | 0x200000000LL; }

  static jlong  clear_needs_gc(jlong state)       { return state & ~0x100000000LL; }
  static jlong  clear_doing_gc(jlong state)       { return state & ~0x200000000LL; }

  static jlong  increment_lock_count(jlong state) { return state + 1; }
  static jlong  decrement_lock_count(jlong state) { return state - 1; }

  static jlong  jni_lock_count(jlong state)       { return state & 0x0FFFFFFFFLL; }

  static bool   is_jni_active(jlong state)        { return jni_lock_count(     state  ) > 0; }
  static bool   is_jni_active()                   { return jni_lock_count(lock_state()) > 0; }

  static void jni_lock_slow();
  static void jni_unlock_slow();
    
  static void jni_lock_always();
  static void jni_unlock_always();

 public:
  // Accessors
static inline bool is_active();

  // Calls set_needs_gc() if is_active() is true. Returns is_active().
static inline bool check_active_before_gc();

  // Shorthand
  static inline bool is_active_and_needs_gc() { return is_active() && needs_gc(lock_state());}
  
  // Stalls the caller (who should not be in a jni critical section)
  // until needs_gc() clears. Note however that needs_gc() may be
  // set at a subsequent safepoint and/or cleared under the
  // JNICritical_lock, so the caller may not safely assert upon
  // return from this method that "!needs_gc()" since that is
  // not a stable predicate.
  static void stall_until_clear();

  // Non-structured GC locking: currently needed for JNI. Use with care!
static inline void lock();
static inline void unlock();

  // The following two methods are used for JNI critical regions.
  // If we find that we failed to perform a GC because the GC_locker
  // was active, arrange for one as soon as possible by allowing
  // all threads in critical regions to complete, but not allowing
  // other critical regions to be entered. The reasons for that are: 
  // 1) a GC request won't be starved by overlapping JNI critical 
  //    region activities, which can cause unnecessary OutOfMemory errors. 
  // 2) even if allocation requests can still be satisfied before GC locker 
  //    becomes inactive, for example, in tenured generation possibly with 
  //    heap expansion, those allocations can trigger lots of safepointing 
  //    attempts (ineffective GC attempts) and require Heap_lock which 
  //    slow down allocations tremendously. 
  // Note that critical regions can be nested in a single thread, so
  // we must allow threads already in critical regions to continue.
  //
  // JNI critical regions are the only participants in this scheme
  // because they are, by spec, well bounded while in a critical region.
  //
  // Each of the following two method is split into a fast path and a slow
  // path. JNICritical_lock is only grabbed in the slow path.
  // _needs_gc is initially false and every java thread will go
  // through the fast path (which does the same thing as the slow path
  // when _needs_gc is false). When GC happens at a safepoint,
  // GC_locker::is_active() is checked. Since there is no safepoint in the
  // fast path of lock_critical() and unlock_critical(), there is no race
  // condition between the fast path and GC. After _needs_gc is set at a
  // safepoint, every thread will go through the slow path after the safepoint.
  // Since after a safepoint, each of the following two methods is either
  // entered from the method entry and falls into the slow path, or is
  // resumed from the safepoints in the method, which only exist in the slow
  // path. So when _needs_gc is set, the slow path is always taken, till
  // _needs_gc is cleared.
static inline void lock_critical(JavaThread*thread);
static inline void unlock_critical(JavaThread*thread);

  // Locking for use by Universe during initialization.
  static inline void lock_universe();
  static inline void unlock_universe();

  // The following two methods are used to secure the GC_locker during
  // the run of a concurrent garbage collector.  The lock() and unlock()
  // methods in this class are not safe in conjunction with a concurrent
  // collector, as they don't inspect the _concurrent_gc flag.
  static inline void lock_concurrent_gc();
  static        void unlock_concurrent_gc();
};


// A No_GC_Verifier object can be placed in methods where one assumes that
// no garbage collection will occur. The destructor will verify this property
// unless the constructor is called with argument false (not verifygc).
//
// The check will only be done in debug mode and if verifygc true.

class No_GC_Verifier: public StackObj {
 friend class Pause_No_GC_Verifier;

 protected:
  bool _verifygc;  
  unsigned int _old_invocations;

 public:
#ifdef ASSERT
  No_GC_Verifier(bool verifygc = true);
  ~No_GC_Verifier();   
#else
  No_GC_Verifier(bool verifygc = true) {}
  ~No_GC_Verifier() {}
#endif
};

// A Pause_No_GC_Verifier is used to temporarily pause the behavior
// of a No_GC_Verifier object. If we are not in debug mode or if the
// No_GC_Verifier object has a _verifygc value of false, then there
// is nothing to do.

class Pause_No_GC_Verifier: public StackObj {
 private:
  No_GC_Verifier * _ngcv;

 public:
#ifdef ASSERT
  Pause_No_GC_Verifier(No_GC_Verifier * ngcv);
  ~Pause_No_GC_Verifier();   
#else
  Pause_No_GC_Verifier(No_GC_Verifier * ngcv) {}
  ~Pause_No_GC_Verifier() {}
#endif
};


// A No_Safepoint_Verifier object will throw an assertion failure if
// the current thread passes a possible safepoint while this object is
// instantiated. A safepoint, will either be: an oop allocation, blocking
// on a Mutex or JavaLock, or executing a VM operation.
//
// If StrictSafepointChecks is turned off, it degrades into a No_GC_Verifier
//
class No_Safepoint_Verifier : public No_GC_Verifier {
 friend class Pause_No_Safepoint_Verifier;

 private:  
  bool _activated;
  Thread *_thread;
 public:
#ifdef ASSERT
No_Safepoint_Verifier(bool activated=true):No_GC_Verifier(activated){
    _thread = Thread::current();
    if (_activated) {
      _thread->_allow_allocation_count++;
      _thread->_allow_safepoint_count++;
    }
  }

  ~No_Safepoint_Verifier() {
    if (_activated) {
      _thread->_allow_allocation_count--;
      _thread->_allow_safepoint_count--;
    }
  }
#else
No_Safepoint_Verifier(bool activated=true):No_GC_Verifier(activated){}
  ~No_Safepoint_Verifier() {}
#endif
};

// A Pause_No_Safepoint_Verifier is used to temporarily pause the
// behavior of a No_Safepoint_Verifier object. If we are not in debug
// mode then there is nothing to do. If the No_Safepoint_Verifier
// object has an _activated value of false, then there is nothing to
// do for safepoint and allocation checking, but there may still be
// something to do for the underlying No_GC_Verifier object.

class Pause_No_Safepoint_Verifier : public Pause_No_GC_Verifier {
 private:  
  No_Safepoint_Verifier * _nsv;

 public:
#ifdef ASSERT
  Pause_No_Safepoint_Verifier(No_Safepoint_Verifier * nsv)
    : Pause_No_GC_Verifier(nsv) {

    _nsv = nsv;
    if (_nsv->_activated) {
      _nsv->_thread->_allow_allocation_count--;
      _nsv->_thread->_allow_safepoint_count--;
    }
  }

  ~Pause_No_Safepoint_Verifier() {
    if (_nsv->_activated) {
      _nsv->_thread->_allow_allocation_count++;
      _nsv->_thread->_allow_safepoint_count++;
    }
  }
#else
  Pause_No_Safepoint_Verifier(No_Safepoint_Verifier * nsv)
    : Pause_No_GC_Verifier(nsv) {}
  ~Pause_No_Safepoint_Verifier() {}
#endif
};

// JRT_LEAF currently can be called from either _thread_in_Java or
// _thread_in_native mode. In _thread_in_native, it is ok
// for another thread to trigger GC. The rest of the JRT_LEAF
// rules apply.
class JRT_Leaf_Verifier : public No_Safepoint_Verifier {
  static bool should_verify_GC();
 public:
#ifdef ASSERT
  JRT_Leaf_Verifier();
  ~JRT_Leaf_Verifier();
#else
  JRT_Leaf_Verifier() {}
  ~JRT_Leaf_Verifier() {}
#endif
};

// A No_Alloc_Verifier object can be placed in methods where one assumes that
// no allocation will occur. The destructor will verify this property
// unless the constructor is called with argument false (not activated).
//
// The check will only be done in debug mode and if activated.
// Note: this only makes sense at safepoints (otherwise, other threads may
// allocate concurrently.)

class No_Alloc_Verifier : public StackObj {
 private:
  bool  _activated;

 public:
#ifdef ASSERT
  No_Alloc_Verifier(bool activated = true) { 
    _activated = activated;
    if (_activated) Thread::current()->_allow_allocation_count++;
  }

  ~No_Alloc_Verifier() {
    if (_activated) Thread::current()->_allow_allocation_count--;
  }
#else
  No_Alloc_Verifier(bool activated = true) {}
  ~No_Alloc_Verifier() {}
#endif
};

// A ConcurrentGCLockerMark object can be placed in methods which need to be
// contained by a lock_concurrent_gc()/unlock_concurrent_gc().

class GCLockerMark:public StackObj{
 public:
  GCLockerMark();
  ~GCLockerMark();
};


class GCModeSetter:public StackObj{
 private:
   Thread* _thread;
 public:
  GCModeSetter(Thread* thread)
          : _thread(thread)
          {
            assert0(!_thread->is_gc_mode());
_thread->set_gc_mode(true);
          }
  ~GCModeSetter() { _thread->set_gc_mode(false); }
};

#endif // GCLOCKER_HPP
