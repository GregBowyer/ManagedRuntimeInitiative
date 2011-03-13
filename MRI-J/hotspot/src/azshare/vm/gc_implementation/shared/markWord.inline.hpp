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
#ifndef MARKWORD_INLINE_HPP
#define MARKWORD_INLINE_HPP



// Checks for being speculatively locked by self thread
inline bool markWord::is_self_spec_locked() const { 
  intptr_t tid = Thread::current()->reversible_tid();
  tid <<= hash_shift;           // TID lives in the tid/hash/monitor bits
  assert0( (tid & lock_mask_in_place) == 0 );
  assert0( (tid & ~hash_mask_in_place) == 0 );
  return (((intptr_t)this) & (hash_mask_in_place | lock_mask)) == tid;
}

// Check for being self-locked.  A little more efficient than
// "mark->lock_owner()==JavaThread::current()"
inline bool markWord::is_self_locked( ) const { 
  if( is_self_spec_locked() ) return true;
  if( !has_monitor() ) return false;
return monitor()->owned_by_self();
}

// Best-effort lock owner.  The lock owner can change from thread-to-thread
// or owned to unowned moment by moment.  
inline JavaThread* markWord::lock_owner( ) const {
  Thread *t = has_monitor() 
    ? monitor()->owner() 
    : (is_biased() 
       ? Thread::reverse_tid((((intptr_t)this) & (hash_mask_in_place | lock_mask))>> hash_shift)
       : NULL);
return(JavaThread*)t;
}

// Best-effort is-unlocked.  The ownership can change moment-by-moment.
inline bool markWord::is_unlocked() const {
  intptr_t bits = (intptr_t)this;
  return (bits & (hash_mask_in_place | lock_mask)) == 0 || // totally zero OR
    (bits&1)==1;                // hashed?
}

// Best-effort is-unlocked.  The ownership can change moment-by-moment.
inline bool markWord::is_fresh() const {
  intptr_t bits = (intptr_t)this;
  return (bits & (hash_mask_in_place | lock_mask)) == 0; // totally zero
}

// Return a version of this markWord as it would be, if it was bias-locked.
// Handy for CAS'ing markWords during locking attempts.
inline markWord *markWord::as_biaslocked( int tid ) const {
  assert0( tid < (1L<<hash_bits) );
  return (markWord*)((((intptr_t)this) & ~(hash_mask_in_place | lock_mask)) | (tid << hash_shift));
}

// Return a version of this markWord as it would be, if it was fresh.
// Handy for CAS'ing markWords during striping of bias-locks owned by dead threads.
inline markWord *markWord::as_fresh() const {
  return (markWord*)(((intptr_t)this) & ~(hash_mask_in_place | lock_mask));
}


// Return a version of this markWord as it would be, if this ObjectMonitor
// is install.  Handy for CAS'ing markWords during locking attempts.
inline markWord *markWord::as_heavylocked( ObjectMonitor* mon ) const {
  assert0( (intptr_t)mon < (1LL<<(hash_bits+lock_bits)) ); // ptr better fit in allotted space
  return (markWord*)((((intptr_t)this) & ~(hash_mask_in_place | lock_mask)) | ((intptr_t)mon|2));
}

#endif // MARKWORD_INLINE_HPP

