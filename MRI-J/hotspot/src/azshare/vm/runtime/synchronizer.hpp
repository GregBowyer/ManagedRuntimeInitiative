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
#ifndef SYNCHRONIZER_HPP
#define SYNCHRONIZER_HPP

#include "allocation.hpp"
#include "objectMonitor.hpp"
#include "thread.hpp"

// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.
class MonitorClosure;

class ObjectSynchronizer:AllStatic{
public:
  // Returns the identity hash value for an oop.
  // NOTE: It may cause monitor inflation, but never a GC.
  static intptr_t FastHashCode(objectRef ref) ;
  static intptr_t get_next_hash();

  // Handle all interpreter, compiler and jni cases
static void notify(objectRef ref,TRAPS);
static void notifyall(objectRef ref,TRAPS);
  static void wait     (objectRef ref, jlong millis, TRAPS) { wait_impl(ref,millis,true,THREAD); }

  static void wait_impl(objectRef ref, jlong millis, bool interruptible, TRAPS);

  // Visit all referenced oops
  static void oops_do(OopClosure* f);

  // Lock the object.  Probably will be slow-path and blocking, as the
  // fast-path attempts should already have happened and failed.
  // May block.  May GC.  May throw exceptions.
  static void lock( heapRef o, MonitorInflateCallerId caller);

  // Similar to lock but set recursion count when done.
  static void lock_recursively( Handle lockObj );

  // Unlock the object.  Probably will be slow-path as the fast-path
  // attempts should already have happened and failed.
  // Cannot block, GC, nor throw exceptions.
  static void unlock( oop o );

  // Similar to unlock but do recursively (slap a recursion count of 1 first).
  static void unlock_recursively( oop o );

  // Inflation!  "Inflate" the lock: make an objectMonitor for this Java
  // object and make the object's header refer to it.  Inflated locks have a
  // bunch of useful features:
  // - the object can be both locked AND have a hashCode
  // - the object supports lock contention, wait & notify
  // - SMA works only on inflated locks (so we have a place for SMA stats)
  static ObjectMonitor* inflate( heapRef o, MonitorInflateCallerId caller);

  // Expensively determine if a thead really holds a biased lock,
  // without revoking the bias or relying on the bias.
static bool current_thread_holds_lock(JavaThread*thread,oop o);

  // 6282335 JNI DetachCurrentThread spec states that all Java monitors
  // held by this thread must be released.  A detach operation must only
  // get here if there are no Java frames on the stack.  Therefore, any
  // owned monitors at this point MUST be JNI-acquired monitors which are
  // pre-inflated and in the monitor cache.
  //
  // ensure_join() ignores IllegalThreadStateExceptions, and so does this.
static void release_monitors_owned_by_thread(JavaThread*thread);

  // GC: we current use aggressive monitor deflation policy
  // Basically we deflate all monitors that are not busy.
  // An adaptive profile-based deflation policy could be used if needed
  static ObjectMonitor *_monitors; // List of all monitors for easy deflation
  static jlong _last_deflation_round; // prevent spamming deflations
  static jlong _monCount; // Racey count of monitors; used to guide deflation
  static void deflate_idle_monitors();
  static void start_monitor_deflate_task();

  // Printing
  static void summarize_sma_status();
  static void summarize_sma_status_xml(xmlBuffer* xb);

  // JVMTI
  static void monitors_iterate(MonitorClosure* m);
};

// ObjectLocker enforces balanced locking.  May block.  May GC. While it can
// never thrown an IllegalMonitorStateException, pending and async exceptions
// may have to pass through.  When revoking the bias on a lock, these lock
// acquires have to be counted.  We maintain a linked list of them per JavaThread.
class ObjectLocker:public StackObj{
public:
  const Handle _h;
  const ObjectLocker *const _ol; // nested list of ObjectLockers on this thread.
  ObjectLocker( objectRef o ) : _h(JavaThread::current(),o), _ol(JavaThread::current()->_objectLockerList) { 
    JavaThread *self = JavaThread::current(); // shortcut
if(o.not_null()){
      o.as_oop()->lock(INF_ENTER,self);
      self->_objectLockerList = this; // linked-list insertion
    }
  }
  ~ObjectLocker( ) { if( _h.not_null() ) { _h()->unlock(); JavaThread::current()->_objectLockerList = _ol; } }
void notify_all(TRAPS);
  void waitUninterruptibly(TRAPS) { ObjectSynchronizer::wait_impl(_h.as_ref(),0,false,THREAD); }
};

#endif // SYNCHRONIZER_HPP
