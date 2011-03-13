/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "collectedHeap.hpp"
#include "markSweep.hpp"
#include "ostream.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "universe.inline.hpp"

GrowableArray<oop>*     MarkSweep::_marking_stack       = NULL;
GrowableArray<Klass*>*  MarkSweep::_revisit_klass_stack = NULL;

GrowableArray<uintptr_t>*MarkSweep::_preserved_oop_stack=NULL;
GrowableArray<markWord*>*MarkSweep::_preserved_mark_stack=NULL;
size_t			MarkSweep::_preserved_count = 0;
size_t			MarkSweep::_preserved_count_max = 0;
PreservedMark*          MarkSweep::_preserved_marks = NULL;
ReferenceProcessor*     MarkSweep::_ref_processor   = NULL;

void MarkSweep::revisit_weak_klass_link(Klass* k) {
  _revisit_klass_stack->push(k);
}


void MarkSweep::follow_weak_klass_links() {
  // All klasses on the revisit stack are marked at this point.
  // Update and follow all subklass, sibling and implementor links.
  for (int i = 0; i < _revisit_klass_stack->length(); i++) {
    _revisit_klass_stack->at(i)->follow_weak_klass_links(&is_alive,&keep_alive);
  }
  follow_stack();
}


void MarkSweep::mark_and_follow(heapRef* p) {
  assert(Universe::heap()->is_in_reserved(p),
	 "we should only be traversing objects here");
heapRef r=UNPOISON_OBJECTREF(*p,p);
  oop m = r.as_oop();
  if (m != NULL && !m->mark()->is_marked()) {
mark_object(r);
    m->follow_contents();  // Follow contents of the marked object
  }
}

void MarkSweep::_mark_and_push(heapRef* p) {
  // Push marked object, contents will be followed later
heapRef r=UNPOISON_OBJECTREF(*p,p);
  oop     m = r.as_oop();
mark_object(r);
  _marking_stack->push(m);
}

MarkSweep::MarkAndPushClosure MarkSweep::mark_and_push_closure;

void MarkSweep::follow_root(heapRef* p) {
  assert(!Universe::heap()->is_in_reserved(p), 
         "roots shouldn't be things within the heap");
  heapRef r = UNPOISON_OBJECTREF(*p, p);
  oop     m = r.as_oop();
  if (m != NULL && !m->mark()->is_marked()) {
mark_object(r);
    m->follow_contents();  // Follow contents of the marked object
  }
  follow_stack();
}

MarkSweep::FollowRootClosure MarkSweep::follow_root_closure;

void MarkSweep::follow_stack() {
  while (!_marking_stack->is_empty()) {
    oop obj = _marking_stack->pop();
    assert (obj->is_gc_marked(), "p must be marked");
    obj->follow_contents();
  }
}

MarkSweep::FollowStackClosure MarkSweep::follow_stack_closure;


// We preserve the mark which should be replaced at the end and the location that it
// will go.  Note that the object that this markOop belongs to isn't currently at that
// address but it will be after phase4
void MarkSweep::preserve_mark(heapRef obj,markWord*mark){
  // we try to store preserved marks in the to space of the new generation since this
  // is storage which should be available.  Most of the time this should be sufficient
  // space for the marks we need to preserve but if it isn't we fall back in using
  // GrowableArrays to keep track of the overflow.
  if (_preserved_count < _preserved_count_max) {
    _preserved_marks[_preserved_count++].init(obj, mark);
  } else {
assert0(_preserved_mark_stack!=NULL);
assert0(_preserved_oop_stack!=NULL);

    _preserved_mark_stack->push(mark);
_preserved_oop_stack->push(obj.raw_value());
  }
}

MarkSweep::AdjustPointerClosure MarkSweep::adjust_pointer_closure_skipping_CodeCache(true);
MarkSweep::AdjustPointerClosure MarkSweep::adjust_pointer_closure(false);

void MarkSweep::adjust_marks() {
  assert(_preserved_oop_stack == NULL ||
	 _preserved_oop_stack->length() == _preserved_mark_stack->length(),
	 "inconsistent preserved oop stacks");

  // adjust the oops we saved earlier
  for (size_t i = 0; i < _preserved_count; i++) {
    _preserved_marks[i].adjust_pointer();
  }

  // deal with the overflow stack
  if (_preserved_oop_stack) {
    for (int i = 0; i < _preserved_oop_stack->length(); i++) {
heapRef*p=(heapRef*)(_preserved_oop_stack->adr_at(i));
      adjust_pointer(p);
    }
  }
}

void MarkSweep::restore_marks() {
  assert(_preserved_oop_stack == NULL ||
	 _preserved_oop_stack->length() == _preserved_mark_stack->length(),
	 "inconsistent preserved oop stacks");
  if (PrintGC && Verbose) {
gclog_or_tty->print_cr("Restoring %lu marks",_preserved_count+
		  (_preserved_oop_stack ? _preserved_oop_stack->length() : 0));
  }

  // restore the marks we saved earlier
  for (size_t i = 0; i < _preserved_count; i++) {
    _preserved_marks[i].restore();
  }

  // deal with the overflow
  if (_preserved_oop_stack) {
    for (int i = 0; i < _preserved_oop_stack->length(); i++) {
oop obj=(*(heapRef*)(_preserved_oop_stack->adr_at(i))).as_oop();
markWord*mark=_preserved_mark_stack->at(i);
      obj->set_mark(mark);      
    }
  }
}

MarkSweep::IsAliveClosure MarkSweep::is_alive;

void MarkSweep::KeepAliveClosure::do_oop(objectRef* p) {
  assert0(p->is_null() || UNPOISON_OBJECTREF(*p, p).is_heap());
heapRef*heap_ref=(heapRef*)p;

mark_and_push(heap_ref);
}

MarkSweep::KeepAliveClosure MarkSweep::keep_alive;

void marksweep_init() { /* empty */ }

#ifndef PRODUCT

void MarkSweep::trace(const char* msg) {
  if (TraceMarkSweep)
    gclog_or_tty->print("%s", msg);
}

void MarkSweep::check_kid(oop obj, unsigned int kid) {
  if (MarkSweepVerifyKIDs) {
    if (Universe::heap()->is_in(obj->klass())) {
      oop klass_klass_ptr = ((oop)(obj->klass())) + 1;
      if (klass_klass_ptr->is_oop()) {
        if (Universe::heap()->is_in(obj->klass()->blueprint())) {
          assert0(Klass::cast(obj->klass())->klassId() == kid);
        }
      }
    }
  }
}

#endif

