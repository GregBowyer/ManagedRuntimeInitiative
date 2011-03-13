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

#ifndef THREADLS_OS_PD_HPP
#define THREADLS_OS_PD_HPP

// This header is included in thread.hpp, inside the Thread class.


  static inline Thread* stack_ptr_to_Thread(intptr_t* stackptr) {
    return (Thread*)((intptr_t)stackptr & ~(BytesPerThreadStack-1));
  }

  static inline JavaThread *stack_ptr_to_JavaThread(intptr_t* stackptr) {
    Thread *t = stack_ptr_to_Thread(stackptr);
    assert0( t->is_Java_thread() );
return(JavaThread*)t;
  }




// Inline implementation of Thread::current().  Thread::current is
// "hot" it's called > 128K times in the 1st 500 msecs of startup.
// No such thing as a "slow version" of this call.
static inline Thread*current(){
    // Thread* is kept in the low aligned bits of the stack space.
    // We get it by masking off the RSP.
    return (Thread*) thread_current();
  }

  
#endif
