/*
 * Copyright 2000-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GCLOCKER_INLINE_HPP
#define GCLOCKER_INLINE_HPP


inline bool GC_locker::is_active() {
return _lock_count>0||is_jni_active();
}

inline bool GC_locker::check_active_before_gc() {
  if (is_active()) {
    while (1) {
      jlong old_state = lock_state();
      jlong new_state = set_needs_gc(old_state);
      if ( old_state == Atomic::cmpxchg(new_state, state_addr(), old_state) ) { 
	// flag set successfully
	break;
      }
      // failed to set the flag, loop back around
    }
  }
  return is_active();
}

inline void GC_locker::lock() {
  // cast away volatile
guarantee(!UseGenPauselessGC,"Can't use GPGC or PGC with fast GC locks");
Atomic::inc_ptr(&_lock_count);
  assert(Universe::heap() == NULL ||
	 !Universe::heap()->is_gc_active(), "locking failed");
}

inline void GC_locker::unlock() {
guarantee(!UseGenPauselessGC,"Can't use GPGC or PGC with fast GC locks");
  // cast away volatile
Atomic::dec_ptr(&_lock_count);
}


inline void GC_locker::jni_lock_always(){
  while (1) {
    jlong old_state = lock_state();

    if ( block_for_gc(old_state) ) {
      // Can't do fast path locking      
      jni_lock_slow();
      break;
    }

    jlong new_state = increment_lock_count(old_state);
    if ( old_state == Atomic::cmpxchg(new_state, state_addr(), old_state) ) {
      // fast path lock successful
      break;
    }

    // fast path lock failed, loop around and try again.
  }
}

inline void GC_locker::lock_critical(JavaThread* thread) {
  if (!thread->in_critical()) {
    jni_lock_always();
  }
  thread->enter_critical();
}

inline void GC_locker::lock_universe(){
  jni_lock_always();
}


inline void GC_locker::jni_unlock_always(){
  while (1) {
    jlong old_state = lock_state();

    if ( needs_gc(old_state) ) {
      // Do slow path unlocking, since a GC is needed.  This path is never taken for
      // pauseless GC, because pauseless GC only sets doing_gc, not needs_gc.
      jni_unlock_slow();
      break;
    }

    jlong new_state = decrement_lock_count(old_state); 
    if ( old_state == Atomic::cmpxchg(new_state, state_addr(), old_state) ) {
      // fast path unlock successful
      break;
    }

    // fast path unlock failed, loop around and try again.
  }
}

inline void GC_locker::unlock_critical(JavaThread* thread) {
  thread->exit_critical();
  if (!thread->in_critical()) {
    jni_unlock_always();
  }
}

inline void GC_locker::unlock_universe(){
  jni_unlock_always();
}


inline void GC_locker::lock_concurrent_gc(){
  // This has only been reviewed for use with the GPGC and PGC algorithms:
  assert0(UseGenPauselessGC);

  // First tell other threads that a GC is going on:
  while (1) {
    // Make new threads entering lock_critical() block:
    jlong old_state = lock_state();

    // Pauseless GC wouldn't be calling this if it was already doing a GC:
    assert0( ! doing_gc(old_state) );

    jlong new_state = set_doing_gc(old_state);
    if ( old_state == Atomic::cmpxchg(new_state, state_addr(), old_state) ) {
      // successfully set the doing_gc indication.
      break;
    }
  }

  // Wait until there aren't any threads holding locks from lock_critical():
  while (is_jni_active()) {
    os::yield_all(1);
  }
  
assert(is_jni_active()==false,"GC_locker in wrong state");
  assert( doing_gc( lock_state() ) == true, "GC_locker in wrong state" );
  
}

#endif // GCLOCKER_INLINE_HPP

