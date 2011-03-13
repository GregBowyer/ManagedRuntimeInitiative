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
#ifndef SAFEPOINT_HPP
#define SAFEPOINT_HPP

#include "safepointTimes.hpp"
#include "thread.hpp"

//
// Safepoint synchronization
//
// The VMThread or CMS_thread uses the SafepointSynchronize::begin/end methods
// to enter/exit a safepoint region.  The begin method will roll all
// JavaThreads forward to a safepoint.
//
// The Mutex/Condition variable and ObjectLocker classes calls the enter/exit
// safepoint methods, when a thread is blocked/restarted.  Hence, all mutex
// exter/exit points *must* be at a safepoint.



// This subclass of JavaThreadClosure does nothing.  It's only used when a checkpoint
// is needed to ensure that every JavaThread has gone through a safepoint.
class NopThreadClosure : public JavaThreadClosure {
 public:
  virtual void do_java_thread(JavaThread *jt) {}
};


// Polling-Only Self-Suspension Safepoints, 3/18/2003
//
// The HotSpot VM runs all the time with a giant distributed "inside-out"
// not-at-Safepoint JVM lock.  This lock is normally held by each running Java
// thread and prevents GC.  To do a GC or other Safepoint-only operation, the
// VM has to wrest control of this lock from each and every Java thread.  Java
// threads voluntarily give up the lock on entry to native methods (since
// natives can block on I/O for extended periods of time) and attempt to take
// it back before entering Java code again.  Java threads also poll a stop
// flag to see if they should voluntarily self-suspend.  If the stop flag is
// set, the Java thread runs to a Safepoint location and does a normal unlock
// action on the JVM lock.
// 
// The JVM lock is distributed, with a _jvm_lock field in every thread's TLS
// area.  The _jvm_lock field is in the TLS to allow fast access for running
// Java threads which need to rapidly free and re-take the lock on each native
// call.
// 
// For a thread to take the JVM lock it needs to LL/SC or CAS in the "locked"
// value.  Releasing the lock only requires a store of the "unlocked" value
// (no memory fence needed).  Stack crawlers need a valid SP for each
// suspended thread and this isn't readily available from the OS.  Hence Java
// threads entering natives must set their SP on the way in.  We use this set
// as the "unlocked" value.  To retake the lock, Java threads must LL/SC or
// CAS in a NULL value.  For the VM thread to take the lock, it will need to
// see that the lock is free (Java thread's _jvm_lock is set to some SP), and
// it will LL/SC or CAS down the same SP with the low order bit set.  Freeing the
// lock is a normal store of the original SP.
//

//
// Implements roll-forward to safepoint (safepoint synchronization)
// 
class SafepointSynchronize : AllStatic {
 public:
  enum SynchronizeState {
      _not_synchronized = 0,                   // Threads not synchronized at a safepoint
      _synchronizing    = 1,                   // Synchronizing in progress  
_synchronized=2,//All Java threads are stopped at a safepoint. Only VM thread is running,
      _checkpointing    = 3                    // Checkpoint in progress
  };

 private:
  static double         _max_time_to_safepoint; // Longest time to complete begin(), in millis
  static double         _max_safepoint;         // Longest total safepoint time, in millis
  static double         _max_checkpoint;        // Longest total checkpoint time, in millis

  static SafepointTimes _current_times;

  static void           print_safepoint_times();
  static void           print_checkpoint_times();

 private:
static volatile SynchronizeState _state;//Threads might read this flag directly, without acquiring the Threads_lock
  // _waiting_to_block is only modified when the Safepoint_lock is held.
  static int _waiting_to_block;	// No. of threads we are waiting for to block.
  // Flag indicating checkpointing threads may have had their priority artificially raised.
  static bool _priority_boosted;

  // The action to be done at the checkpoint
  static JavaThreadClosure* _checkpoint_closure;

public:
  // Main entry points

  // Roll all threads forward to safepoint. Must be called by the
  // VMThread or CMS_thread.
  static void begin(SafepointTimes* times = NULL);
  static void end();                    // Start all suspended threads again...

  // called by thread requesting the checkpoint
  static void do_checkpoint(JavaThreadClosure *jtc, SafepointTimes* times = NULL);
  // called by JavaThreads observing the checkpoint
static void block_checkpoint(JavaThread*thread);
static void reset_priority_if_needed(JavaThread*thread);

  // Query
  inline static bool is_at_safepoint()                    { return _state == _synchronized;  }
  inline static bool is_synchronizing()                   { return _state == _synchronizing;  }
  
  // Called when a thread voluntarily blocks
  static void   block(JavaThread *thread);
  static void   signal_thread_at_safepoint()              { _waiting_to_block--; }  

  static void do_cleanup_tasks();

  // debugging
  static void print_state()                                PRODUCT_RETURN;
  static void safepoint_msg(const char* format, ...)       PRODUCT_RETURN;

  static void print_stat_on_exit();

  static void set_is_at_safepoint()                        { _state = _synchronized; }
  static void set_is_not_at_safepoint()                    { _state = _not_synchronized; }

  // assembly support
  static address address_of_state()                        { return (address)&_state; }  
};

class EnforceSafepoint:public StackObj{
 public:
  EnforceSafepoint() {
    SafepointSynchronize::begin();
  }

  ~EnforceSafepoint() {
    SafepointSynchronize::end();
  }
};

#endif // SAFEPOINT_HPP
