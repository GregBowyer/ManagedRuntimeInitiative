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

#include "gpgc_marker.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"

#include "atomic_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

void GPGC_Marker::new_gen_mark_new_obj(HeapWord* obj) {
  if ( ! GPGC_NewCollector::should_mark_new_objects_live() ) {
    // Don't mark newly allocated objects outside of a GC cycle.
    return;
  }

  if ( ! GPGC_Marks::atomic_mark_live_if_dead((oop)obj) ) {
    // We shouldn't ever fail to mark newly allocated objects.
    ShouldNotReachHere();
  }

  GPGC_Marks::set_markid((oop)obj, 0x20);
  GPGC_Marks::set_marked_through((oop)obj);

  // New allocated objects don't update the page population stats
}


void GPGC_Marker::perm_gen_mark_new_obj(HeapWord* obj) {
  if ( ! GPGC_OldCollector::should_mark_new_objects_live() ) {
    // Don't mark newly allocated objects outside of a GC cycle.
    return;
  }

  if ( ! GPGC_Marks::atomic_mark_live_if_dead((oop)obj) ) {
    // We shouldn't ever fail to mark newly allocated objects.
    ShouldNotReachHere();
  }

  GPGC_Marks::set_markid((oop)obj, 0x21);
  GPGC_Marks::set_marked_through((oop)obj);

  // New allocated objects don't update the page population stats
}
