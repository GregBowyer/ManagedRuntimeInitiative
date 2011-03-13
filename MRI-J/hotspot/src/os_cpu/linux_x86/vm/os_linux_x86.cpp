/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "atomic.hpp"
#include "debug.hpp"
#include "os.hpp"
#include "os_os_pd.hpp"
#include "thread.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"

// put OS-includes here
# include <sys/types.h>


#define MAX_PATH (2 * K)


extern "C" void findpc(int x);
static intptr_t* _get_current_sp() __attribute__((noinline));

static intptr_t* _get_current_sp() {
  intptr_t *fp = (intptr_t*)__builtin_frame_address (0);
  return fp+2;
}

void os::make_self_walkable() { /* nothing on X86 */}


void os::make_remote_walkable(Thread*thread){
  Atomic::write_barrier();      // Complete stack writes before flushing remote thread
  // nothing more on X86
}

// Used solely for error reporting
frame os::fetch_frame_from_context(address pc) {
intptr_t*sp=_get_current_sp();
  frame myframe(sp, CAST_FROM_FN_PTR(address, os::fetch_frame_from_context));
  do {
    if (os::is_first_C_frame(&myframe)) {
      // Just give up if we reached the first frame without finding a matching RPC
      return frame(NULL, NULL);
    }
    myframe = myframe.sender_robust();
  } while (myframe.pc() != pc && myframe.pc() != NULL);
  return myframe;
}

// Used solely for error reporting
frame os::get_sender_for_C_frame(frame* fr) {
  return fr->sender_robust();
}

// Used solely for error reporting
frame os::current_frame() {
intptr_t*sp=_get_current_sp();
frame myframe(sp,CAST_FROM_FN_PTR(address,os::current_frame));
  if (os::is_first_C_frame(&myframe)) {
    // stack is not walkable
return frame(NULL,NULL);
  } else {
    return os::get_sender_for_C_frame(&myframe);
  }
}

void os::print_context(outputStream *st, void *context) {
  Unimplemented();
}
