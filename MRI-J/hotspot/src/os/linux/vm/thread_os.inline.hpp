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
#ifndef THREAD_OS_INLINE_HPP
#define THREAD_OS_INLINE_HPP

class ThreadLocalProfileBuffer;

inline int64_t Thread::unique_id() {
  return reversible_tid();      // probably should be OS thread id?
}

// A quickly reversible fcn to produce a unique thread-id.  Thread
// stacks are allocated in a large contigous array.  We can strip the
// low bits and subtract from the base array.

inline intptr_t Thread::reversible_tid() {
  intptr_t tid = ((uintptr_t)this) >> LogBytesPerThreadStack;
  assert0( tid < (1L<<reversible_tid_bits) ); // do not understand thread VA layout?
  assert0( tid != 0 );                        // do not hand out the zero TID
  return tid;
}

// The reverse fcn.  
inline Thread *Thread::reverse_tid(intptr_t tid) {
  assert0( tid != 0 );
  return (Thread*)(tid << LogBytesPerThreadStack);
}

#endif // THREAD_OS_INLINE_HPP
