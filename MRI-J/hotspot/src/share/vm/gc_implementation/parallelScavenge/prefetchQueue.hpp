/*
 * Copyright 2002-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PREFETCHQUEUE_HPP
#define PREFETCHQUEUE_HPP

#include "allocation.hpp"
#include "heapRef_pd.hpp"
#include "oop.hpp"
#include "prefetch.hpp"

//
// PrefetchQueue is a FIFO queue of variable length (currently 8).
//
// We need to examine the performance penalty of variable lengths.
// We may also want to split this into cpu dependant bits.
//

const int PREFETCH_QUEUE_SIZE  = 8;

class PrefetchQueue : public CHeapObj {
 private:
  heapRef*                   _prefetch_queue[PREFETCH_QUEUE_SIZE];
  unsigned int               _prefetch_index;

 public:
  int length() { return PREFETCH_QUEUE_SIZE; }

  inline void clear() {
    for(int i=0; i<PREFETCH_QUEUE_SIZE; i++) {
      _prefetch_queue[i] = NULL;
    }
    _prefetch_index = 0;
  }

  inline heapRef* push_and_pop(heapRef* p) {
    address mark_addr = (address)p->as_oop()->mark_addr();
    Prefetch::write(mark_addr, 0);    
    // This prefetch is intended to make sure the size field of array
    // oops is in cache. It assumes the the object layout is
    // mark -> klass -> size, and that mark and klass are heapword
    // sized. If this should change, this prefetch will need updating!
Prefetch::write(mark_addr+BytesPerCacheLine,0);
    _prefetch_queue[_prefetch_index++] = p;
    _prefetch_index &= (PREFETCH_QUEUE_SIZE-1);
    return _prefetch_queue[_prefetch_index];
  }

  // Stores a NULL pointer in the pop'd location.
  inline heapRef* pop() {
    _prefetch_queue[_prefetch_index++] = NULL;
    _prefetch_index &= (PREFETCH_QUEUE_SIZE-1);
    return _prefetch_queue[_prefetch_index];
  }
};

#endif // PREFETCHQUEUE_HPP

