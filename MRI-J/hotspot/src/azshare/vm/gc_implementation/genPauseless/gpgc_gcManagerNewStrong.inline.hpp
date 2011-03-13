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
#ifndef GPGC_GCMANAGERNEWSTRONG_INLINE_HPP
#define GPGC_GCMANAGERNEWSTRONG_INLINE_HPP



#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_newCollector.inline.hpp"
#include "oop.gpgc.inline.hpp"


inline void GPGC_GCManagerNewStrong::mark_and_follow(objectRef* p, int referrer_kid)
{
  GPGC_NewCollector::mark_and_follow(this, p, referrer_kid);
}


inline void GPGC_GCManagerNewStrong::mark_through(objectRef ref, int referrer_kid)
{
oop obj=ref.as_oop();

  assert0(GPGC_Layout::page_in_heap_range(GPGC_Layout::addr_to_PageNum(obj)));
  assert0(!GPGC_ReadTrapArray::is_remap_trapped(ref));
  assert0(GPGC_Marks::is_new_marked_strong_live(ref));

  GPGC_Marks::set_marked_through(obj);

obj->GPGC_follow_contents(this);

int size=obj->size();
  GPGC_Space::add_live_object(obj, size);
  if (ProfileLiveObjects) {
    Thread::current()->live_objects()->add(ref.klass_id(), referrer_kid, 1, size);
  }
}

#endif // GPGC_GCMANAGERNEWSTRONG_INLINE_HPP
