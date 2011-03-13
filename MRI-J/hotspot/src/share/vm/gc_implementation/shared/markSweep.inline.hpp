/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef MARKSWEEP_INLINE_HPP
#define MARKSWEEP_INLINE_HPP

#include "markSweep.hpp"
#include "psParallelCompact.hpp"

inline void MarkSweep::_adjust_pointer(heapRef*p,bool skipping_codecache){
  assert0( !UNPOISON_OBJECTREF(*p, p).is_stack() ); // Should not be stackRef
  if( skipping_codecache && CodeCache::contains(p) ) return; // Do not adjust CodeBlob oops here
if(p->not_null()){
    heapRef r = UNPOISON_OBJECTREF(*p, p);
oop obj=r.as_oop();
    oop new_pointer = oop(obj->mark()->decode_pointer());
    assert(new_pointer != NULL ||       // is forwarding ptr?
obj->mark()->is_cleared()||//not gc marked?
           obj->is_shared(),            // never forwarded?
           "should contain a forwarding pointer");
    if (new_pointer != NULL) {
      unsigned int kid = r.klass_id(); 
NEEDS_CLEANUP;//the following should work, but doesn't:
      //Klass::cast(obj->klass())->klassId();
#ifdef ASSERT
      check_kid(obj, kid);
#endif
heapRef hr;
      uint64_t sid = heapRef::discover_space_id(new_pointer);
      hr.set_value_base(new_pointer, sid, kid, objectRef::discover_nmt(sid, new_pointer));
      POISON_AND_STORE_REF(p, hr);

      assert(Universe::heap()->is_in_reserved(new_pointer),
	     "should be in object space");
    }
  }
}

inline void MarkSweep::mark_object(heapRef r){
  oop o = r.as_oop();

  if (UseParallelOldGC && VerifyParallelOldWithMarkSweep) {
assert(PSParallelCompact::mark_bitmap()->is_marked(o),
      "Should be marked in the marking bitmap");
  }

  // some marks may contain information we need to preserve so we store them away
  // and overwrite the mark.  We'll restore it at the end of markSweep.
markWord*mark=o->mark();
  o->set_mark(mark->clear_and_save_kid()->set_mark_bit());

if(mark->must_be_preserved(o)){
preserve_mark(o,mark);
  }
}

inline bool MarkSweep::is_unmarked_and_discover_reference(objectRef referent, oop obj, ReferenceType type) {
  return ( !referent.as_oop()->is_gc_marked() && 
           MarkSweep::ref_processor()->discover_reference(obj, type) );
}

#endif // MARKSWEEP_INLINE_HPP
