/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef RESOURCEAREA_HPP
#define RESOURCEAREA_HPP



#include "allocation.hpp"
#include "os.hpp"
#include "thread.hpp"

// The resource area holds temporary data structures in the VM.
// The actual allocation areas are thread local. Typical usage:
//
//   ...
//   {
//     ResourceMark rm;
//     int foo[] = NEW_RESOURCE_ARRAY(int, 64);
//     ...
//   }
//   ...

//------------------------------ResourceArea-----------------------------------
// A ResourceArea is an Arena that supports safe usage of ResourceMark.
class ResourceArea: public Arena {
  friend class ResourceMark;
  debug_only(int _nesting;)             // current # of nested ResourceMarks
  debug_only(static int _warned;)  	// to suppress multiple warnings
  	
public:
  ResourceArea() {
    debug_only(_nesting = 0;)    
  }

  ResourceArea(size_t init_size) : Arena(init_size) {
    debug_only(_nesting = 0;);    
  }

  char* allocate_bytes(size_t size) {
#ifdef ASSERT
    if (_nesting < 1 && !_warned++) 
      fatal("memory leak: allocating without ResourceMark");
    if (UseMallocOnly) {
      // use malloc, but save pointer in res. area for later freeing
      char** save = (char**)internal_malloc_4(sizeof(char*));
      return (*save = (char*)os::malloc(size));
    }
#endif
    return (char*)Amalloc(size);
  }

  debug_only(int nesting() const { return _nesting; });  
};


//------------------------------ResourceMark-----------------------------------
// A resource mark releases all resources allocated after it was constructed
// when the destructor is called.  Typically used as a local variable.
class ResourceMark: public StackObj {
protected:
  ResourceArea *_area;		// Resource area to stack allocate
  Chunk *_chunk;		// saved arena chunk
  char *_hwm, *_max;
  NOT_PRODUCT(size_t _size_in_bytes;)

  void initialize(Thread *thread) {
    _area = thread->resource_area();
    _chunk = _area->_chunk;
    _hwm = _area->_hwm;
    _max= _area->_max;
    NOT_PRODUCT(_size_in_bytes = _area->size_in_bytes();)
    debug_only(_area->_nesting++;)
    assert( _area->_nesting > 0, "must stack allocate RMs" ); 
  }

 public:

#ifndef ASSERT
  ResourceMark(Thread *thread) {
    assert(thread == Thread::current(), "not the current thread");
    initialize(thread);
  }
#else
  ResourceMark(Thread *thread);
#endif // ASSERT

  ResourceMark()               { initialize(Thread::current()); }

  ResourceMark( ResourceArea *r ) : 
    _area(r), _chunk(r->_chunk), _hwm(r->_hwm), _max(r->_max) {
    NOT_PRODUCT(_size_in_bytes = _area->size_in_bytes();)
    debug_only(_area->_nesting++;)
    assert( _area->_nesting > 0, "must stack allocate RMs" ); 
  }

  void reset_to_mark() { 
    if (UseMallocOnly) free_malloced_objects();

    if( _chunk->next() ) 	// Delete later chunks
      _chunk->next_chop();
    _area->_chunk = _chunk;	// Roll back arena to saved chunk
    _area->_hwm = _hwm;
    _area->_max = _max;

    // clear out this chunk (to detect allocation bugs)
    if (ZapResourceArea) memset(_hwm, badResourceValue, _max - _hwm);
    _area->set_size_in_bytes(size_in_bytes());
  }

  ~ResourceMark() { 
    assert( _area->_nesting > 0, "must stack allocate RMs" );
    debug_only(_area->_nesting--;)
    reset_to_mark(); 
  }

  
 private:
  void free_malloced_objects()                                         PRODUCT_RETURN;
  size_t size_in_bytes()       NOT_PRODUCT({ return _size_in_bytes; }) PRODUCT_RETURN0;
};

#endif // RESOURCEAREA_HPP
