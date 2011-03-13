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

#ifndef OBJECTMONITOR_HPP
#define OBJECTMONITOR_HPP

#include "atomic.hpp"
#include "heapRef_pd.hpp"
#include "iterator.hpp"
#include "lvb.hpp"
#include "mutex.hpp"

// The ObjectMonitor class is used to implement JavaMonitors which have
// transformed from the lightweight structure to a heavy weight lock due to
// contention

enum MonitorInflateCallerId { 
  //INF_NA = 0
  INF_ENTER_ASM = 1,
  //INF_EXIT_ASM,
  INF_ENTER = 3,
  // INF_EXIT,
  INF_JNI_ENTER = 5,
  INF_JNI_EXIT = 6,
  INF_WAIT = 7,
  /*
  INF_NOTIFY,
  INF_NOTIFYALL,
  */
  INF_HASHCODE = 10,
  /*
  INF_OSR,
  INF_DEOPT,
  INF_COMPLETE_EXIT,
  */
  INF_REENTER,
  INF_BAD
};

class ObjectSynchronizer;
class markWord;

class ObjectMonitor : public WaitLock {
  friend class JvmtiRawMonitor; // Allow JVMTI to directly make these

  // Creating a monitor pulls the head monitor from the free-list (with a CAS-unlink).  
  //
  // - This means a free-monitor can migrate to "in-use" at any time an Object
  // lock can be acquired - i.e. anytime we're not at a safepoint.  
  //
  // - This means that NOBODY is allowed to look at the guts of a supposedly
  // "free" monitor except to read the _next field in an attempt to CAS it
  // into the free-list head.  Anybody who might look might be surprised when
  // the monitor suddenly becomes in-use and the _next field picks up a new
  // meaning.
  //
  // - Notice the tricky invariants when re-inserting monitors into the free
  // list other than at a safepoint: we read the list head, write the list
  // head into our _next field, then attempt to CAS the free monitor into the
  // list head.  Anytime after we read the list head that monitor might be
  // picked up and become active.  This means our CAS will fail but in the
  // meantime we have a pointer to a was-free-now-active monitor that we need
  // to NOT "peek through" - and even though a ptr to it was grabbed from the
  // free list it has to be treated as "not free" until our CAS succeeds.
static ObjectMonitor*_free_mon_list;
  static jlong _freeMonCount;   // Count of free monitors on list
  // Placement-new operator to allow Monitors to live on a free-list
  inline void* operator new( size_t x, void *ptr ) { return ptr; }
  // Private constructor: use the factory to get a new one
  ObjectMonitor(heapRef o, markWord *mrk, int rpc);
  // Make lots of monitors for the free list, all with addresses that can be
  // CAS'd into markWords (so in the low 32bit address space).
  static ObjectMonitor* make_monitor_bulk_slow( );
public:
  // Static factory for making new ObjectMonitors.
  static ObjectMonitor *make_monitor( heapRef o, markWord* mrk);
  void free_monitor();          // Returning them to the free list
  static jlong freeMonCount() { return _freeMonCount; }

  // Unbias this lock, i.e. already locked by current thread.
  void unbias_locked();

  // Returns true of the lock is pre-biased to the current Thread.
  inline bool is_self_spec_locked() const { return owned_by_self() && _recursion<=0; }

  // Implement a Object.wait call over the standard WaitLock structure.
  void wait_java( jlong millis, bool interruptible, Thread* traps );

  // Timed-wait
  // true - awoke via post
  // false- awoke via timeout or interrupt
  bool wait_for_lock_implementation2( intptr_t timeout_micros );

  // try-lock-w/revoke.  Try to acquire the lock.  If that fails, try to raise
  // the contention-count (which prevents the lock from deflating again).  Do
  // timed waits, and attempt to revoke any bias until the lock revokes.
  // Returns:
  // true - lock acquired (hence revoke happened)
  // false- contention count raised AND lock revoked, and lock is locked (but not by us!)
  // Always returns with the lock revoked.
  bool try_lock_with_revoke( );

  // Used only by ThreadService:
  // Number of threads contending on this monitor at this moment in time.  Not
  // precise, as another thread can appear at any moment.
  inline intptr_t contentions() const { return contention_count(); }

  // Used by SMA
  bool     advise_speculation() const    { return _advise_speculation; }
  void set_advise_speculation(bool x)    { _advise_speculation=x; }
  int      sma_success_indicator() const { return _sma_success_indicator; }
void set_sma_success_indicator(int x){_sma_success_indicator=x;}
  volatile intptr_t _advise_speculation;     // should we SMA?  used as a boolean
  volatile intptr_t _sma_success_indicator;  // records SMA success info for reevaluation

  // Meaning of fields:

  // - _object: the inflated object.  
  //   During inflation of an objectRef, many ObjectMonitors might be racily
  //   constructed.  After installation into the object, only 1 will win.  The
  //   _object field will never change again except for GC.  
private:
  const heapRef _object; // backward object pointer
public:
  inline oop object() const { return lvb_ref(&_object).as_oop(); }
  inline int kid() const { return lvb_ref(&_object).klass_id(); }
  static inline int object_offset_in_bytes() { return offset_of(ObjectMonitor, _object); }
  // Allow GC only to tweak this field via oops_do
  inline void oops_do(OopClosure *f) { f->do_oop((heapRef*)&_object); }

  // - _recursion: count of recursive acquires.
  //
  //   A Zero count means the lock is biased and we are not counting.
  //   -- Changing from a zero requires a CAS and can be done by many threads.
  //      Threads desiring to revoke the bias will CAS and the one 'winner'
  //      needs to know he 'won' because he has extra work to do.  The owner
  //      cannot also change from a zero without a CAS lest he confuse a
  //      'winning' revoking thread.  Revoking requestors CAS the recursion
  //      count from 0 to -1 and this is the signal that a revoke is in
  //      progress.  The current owner will make the next change from -1 to a
  //      proper recursion count (and unlock as needed).
  //
  //   A -1 count means the lock is biased and is being revoked.  
  //   -- This value is only changed by the holder of the owner's _jvmlock and
  //      only into a proper recursion count as part of the bias-revoke logic.
  //      (Can also be changed for a dead owner by somebody holding the
  //      Threads_lock).
  //
  //   A positive count means we are counting acquires and is the number of
  //   times the lock is acquired.  It is at least one for an owned lock.
  //   -- The owner adjusts this field up and down without a CAS, but never to
  //      zero or a negative number.  Instead of Unlocking lowering the count
  //      to zero, the inner LLock is unlocked instead (and the count left at
  //      1).  At a Safepoint, GC can revert this count to a zero effectively
  //      allowing biasing again.  Note that at a Safepoint no thread can peek
  //      at the _object field nor actively work on the ObjectMoniter fields
  //      (the inner WaitLock & LLock fields ARE fair game).
  //
  //   When the lock is unowned the recursion count is either 0 or 1, where
  //   zero means a new owner will be biased and not counting, and 1 means a
  //   new owner will be counting (and 1 is the correct count after acquiring).
  jint _recursion;
  static inline int recursion_offset_in_bytes() { return offset_of(ObjectMonitor, _recursion); }

  // - _hashCode: The object's hashCode
  //   Objects which are both locked and hashed have to keep their hash in a
  //   ObjectMonitor because there's no room in the markWord for both a hash
  //   and the monitor pointer. 
  jint _hashCode;               // Java hashs are 32 bit ints
  static inline int hashCode_offset_in_bytes() { return offset_of(ObjectMonitor, _hashCode); }

  // - _next_unbias: A linked list of monitors that need their biases revoked.
  //   It's inserted onto the list by threads that want to take the lock, but
  //   the lock is currently biased to another thread.  Multiple threads can
  //   attempt to insert the same ObjectMonitor into the list, but only the
  //   current thread removes things.  The multiple-insert race is closed by
  //   CAS'ing the _next_unbias field from -1 to Thread::_unbias as part of a
  //   standard linked-list insertion.
ObjectMonitor*_next_unbias;
  bool insert_on_unbias_list(); // Insert self on owning thread's _unbias list

  // Atomic counter of threads actively using 'this' monitor.  Set to
  // prevent deflation when blocking on locks allowing GC.  Bumped up
  // or down with atomic increments.
  intptr_t _this_in_use;
  
  // - _jni_count: Count of times JNI has explicit locked or unlocked this
  //   object.  The counts are replicated in the _recursion field (if the lock
  //   is counting and not biased), and are only used by jni_MonitorExit to
  //   barf on unbalanced JNI unlocking.
  int _jni_count;

};

#endif // OBJECTMONITOR_HPP
