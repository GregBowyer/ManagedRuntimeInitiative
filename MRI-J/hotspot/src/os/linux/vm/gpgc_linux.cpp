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


#include "cycleCounts.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_space.hpp"
#include "thread.hpp"

#include "gpgc_layout.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "os_os.inline.hpp"

#include <signal.h>
#include <os/exceptions.h>


extern "C" address_t jvm_unexpected_exception_handler(int signum, intptr_t epc, intptr_t eva);


extern "C" address_t jvm_gpgc_heap_exception_handler(int signum, intptr_t epc, intptr_t fault_address, int si_code)
{
  // GPGC only expects to see SIGSEGV inside the heap range.
  if ( signum != SIGSEGV ) {
    return jvm_unexpected_exception_handler(signum, epc, fault_address);
  }

  // When getting a SEGV in the heap, GPGC expects there to be a protected page.
  if ( si_code != SEGV_ACCERR ) {
    return jvm_unexpected_exception_handler(signum, epc, fault_address);
  }

  Thread* thread = Thread::current();

  assert0(GPGC_Layout::is_shattered_address(fault_address));
  assert0(thread->is_Complete_Java_thread());

thread->increment_unshattered_page_trap_count();

  // We've received a protection access SEGV on a mid-space page.
  // The SEGV is resolved by healing the mid-space page.
  intptr_t stripped_addr = fault_address & objectRef::unknown_mask_in_place;
  PageNum  page          = GPGC_Layout::addr_to_PageNum((void*)stripped_addr);
  char*    force_addr    = (char*)( (stripped_addr >> LogBytesPerSmallPage) << LogBytesPerSmallPage );
  long     spin_count    = 0;

  while (true) {
    bool success = GPGC_Space::mutator_heal_mid_page(page, force_addr);

    if ( success ) {
      break;
    }

    spin_count ++;

assert(spin_count<10000,"Mutator spun on page healing 10000 times.");

    // If we couldn't heal, it means there was no memory available.  We sleep and give the
    // GC threads a chance to free up some memory.
struct timespec req;
    req.tv_sec  = 0;
    req.tv_nsec = 1000 * 1000; // sleep for 1 millisecond.

    int result = nanosleep(&req, NULL);

    if ( result < 0 && errno != EINTR ) {
fatal1("nanosleep(1ms) failed with errno %d",errno);
    }

    // Hopefully a short sleep allowed the collector time to free up some memory, so we try again.
  }

  return AZUL_SIGNAL_RESUME_EXECUTION;
}
