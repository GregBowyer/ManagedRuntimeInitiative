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


#include "atomic.hpp"
#include "gpgc_closures.hpp"

#include "atomic_os_pd.inline.hpp"


void GPGC_UnshatteredPageTrapCountClosure::do_java_thread(JavaThread* jt)
{
  intptr_t count = jt->get_unshattered_page_trap_count();

jt->reset_unshattered_page_trap_count();

Atomic::inc_ptr(&_threads);
Atomic::add_ptr(count,&_traps);
  
  intptr_t old_max;
  
  do {
    old_max = _max_thread_traps;

    if ( old_max >= count ) break;
  } while ( old_max == Atomic::cmpxchg_ptr(count, &_max_thread_traps, old_max) );
}
