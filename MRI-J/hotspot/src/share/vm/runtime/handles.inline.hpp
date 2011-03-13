/*
 * Copyright 1998-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef HANDLES_INLINE_HPP
#define HANDLES_INLINE_HPP

#include "thread.hpp"

// these inline functions are in a separate file to break an include cycle
// between Thread and Handle

inline Handle::Handle(oop obj) { 
  if (obj == NULL) {
    _handle = NULL;
  } else {
    _handle = Thread::current()->handle_area()->allocate_handle(obj);     
  }
}


#ifndef ASSERT
inline Handle::Handle(Thread*thread,oop obj,bool dummy){
  assert(thread == Thread::current(), "sanity check");
  if (obj == NULL) {
    _handle = NULL;
  } else {
    _handle = thread->handle_area()->allocate_handle(obj);    
  }
}


inline Handle::Handle(Thread*thread,objectRef r){
  assert(thread == Thread::current(), "sanity check");
if(r.is_null()){
    _handle = NULL;
  } else {
_handle=thread->handle_area()->allocate_handle(r);
  }
}
#endif // ASSERT


inline Handle::Handle(objectRef r){
if(r.is_null()){
    _handle = NULL;
  } else {
_handle=Thread::current()->handle_area()->allocate_handle(r);
  }
}


inline HandleMark::HandleMark() {
  initialize(Thread::current());
}


inline void HandleMark::push() {
  // This is intentionally a NOP. pop_and_restore will reset
  // values to the HandleMark further down the stack, typically
  // in JavaCalls::call_helper.
  debug_only(_area->_handle_mark_nesting++);
}

inline void HandleMark::pop_and_restore() {
  HandleArea* area = _area;   // help compilers with poor alias analysis
  // Delete later chunks
  if( _chunk->next() ) {
    _chunk->next_chop();
  }
  // Roll back arena to saved top markers
  area->_chunk = _chunk;
  area->_hwm = _hwm;
  area->_max = _max;
  NOT_PRODUCT(area->set_size_in_bytes(_size_in_bytes);)
  debug_only(area->_handle_mark_nesting--);
}
#endif // HANDLES_INLINE_HPP
