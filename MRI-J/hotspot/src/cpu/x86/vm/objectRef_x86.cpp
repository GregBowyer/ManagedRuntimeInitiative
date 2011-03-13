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


#include "barrierSet.hpp"
#include "collectedHeap.hpp"
#include "handles.hpp"
#include "nmt.hpp"
#include "objectRef_pd.hpp"
#include "oop.hpp"
#include "thread.hpp"
#include "universe.hpp"

#include "atomic_os_pd.inline.hpp"
#include "allocation.inline.hpp"
#include "barrierSet.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "universe.inline.hpp"

void update_barrier_set(objectRef* p, objectRef v) {
  assert(oopDesc::bs() != NULL, "Uninitialized bs in oop!");
  oopDesc::bs()->write_ref_field(p, v);
}


objectRef ref_check_without_store(objectRef base, objectRef* p, objectRef r) {
  assert0(!UseGenPauselessGC || r.is_null() || NMT::has_desired_nmt(r));
  // imitate the behavior of an STA
  bool p_is_stack = base.is_stack();
  if (p_is_stack) {
    if (r.is_stack()) { // [stack] <- stack
      int p_fid = stackRef(base).preheader()->fid();
      int r_fid = stackRef(r   ).preheader()->fid();
      assert (is_valid_fid(r_fid) && is_valid_fid(p_fid), "fids must be in range");
      if (r_fid > p_fid) {      // A stack escape
        Handle h(Thread::current(),base); // The escape may move the base
        intptr_t offset = (address)p - (address)base.as_oop();
        r = StackBasedAllocation::do_escape(JavaThread::current(), r, p_fid, base.klass_id(), "vm store");
        p = (objectRef*)(((address)h())+offset);
      }
    } else {                       // [stack]   <- heap
    }
  } else {
    if (r.is_stack()) // [heap] <- stack
      r = StackBasedAllocation::do_escape(JavaThread::current(), r, HEAP_FID, base.klass_id(), "vm store");
    // [heap] <- heap
update_barrier_set(p,r);
  }
  // Return the new r after potential stack escape
  return r;
}

void ref_store(objectRef base, objectRef* p, objectRef r) {
  r = ref_check_without_store(base,p,r);
  POISON_AND_STORE_REF(p, r);
}

objectRef::objectRef(oop o){
  if( o == NULL ) {
    _objref = 0;
  } else if (Universe::heap()->is_in_reserved((address)o) ) {
    _objref = heapRef(o)._objref;
  } else {
    assert0( JavaThread::sba_is_in_current((address)o) );
    _objref = stackRef(o)._objref;
  }
}

objectRef::objectRef(oop base, oop offset) {
  assert0( base != NULL );
  if (Universe::heap()->is_in_reserved((address)base) ) {
    assert0( Universe::heap()->is_in_reserved((address)offset) );
heapRef hr;
    uint64_t sid = heapRef::discover_space_id(base);
    hr.set_value_base(offset, sid, Klass::cast(base->klass())->klassId(), objectRef::discover_nmt(sid, base));
    _objref = hr._objref;
  } else {
    assert0( JavaThread::sba_is_in_current((address)base  ) );
    assert0( JavaThread::sba_is_in_current((address)offset) );
    stackRef sr;
    sr.set_value_base(offset, Klass::cast(base->klass())->klassId(), objectRef::discover_nmt(objectRef::stack_space_id,base)); // old nmt for stackref's ?
    _objref = sr._objref;
  }
}
