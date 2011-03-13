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

#include "gpgc_collector.hpp"
#include "gpgc_markingQueue.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "log.hpp"

#include "atomic_os_pd.inline.hpp"
#include "os_os.inline.hpp"

         intptr_t GPGC_MarkingQueue::_blocks_requested                               = 0;
         intptr_t GPGC_MarkingQueue::_blocks_allocated                               = 0;
volatile intptr_t GPGC_MarkingQueue::_free_lists[GPGC_MarkingQueue::_max_free_lists] = { 0, 0, 0, 0, 0, 0, 0, 0 };


GPGC_MarkingQueue::RefBlock* GPGC_MarkingQueue::pop_internal(volatile intptr_t* stack_top)
{
  intptr_t  old_top;
  intptr_t  new_top;
uintptr_t tag;
  RefBlock* block;
  RefBlock* next_block;

  DEBUG_ONLY( long loop_counter = 0; )

  do {
DEBUG_ONLY(loop_counter++;)

    old_top    = *stack_top;
    tag        = extract_tag  (old_top) + TagIncrement;
    block      = extract_block(old_top);

    if ( block == NULL ) { return NULL; }

    next_block = block->_next;
    new_top    = intptr_t(next_block) | tag;
  } while ( old_top != Atomic::cmpxchg_ptr(new_top, stack_top, old_top) );

  DEBUG_ONLY (
    Log::log6(NOTAG, Log::M_GC, PrintGCDetails && loop_counter>5, true, gclog_or_tty,
             "thread 0x%llx spun %d times dequeuing RefBlock", Thread::current(), loop_counter);
  )

block->_next=NULL;

  return block;
}


void GPGC_MarkingQueue::push_internal(volatile intptr_t* stack_top, RefBlock* block)
{
  intptr_t   old_top;
  intptr_t   new_top;
uintptr_t tag;
RefBlock*top;

  DEBUG_ONLY( long loop_counter = 0; )

  assert0(extract_block(intptr_t(block)) == block);

  do {
DEBUG_ONLY(loop_counter++;)

    old_top      = *stack_top;
    tag          = extract_tag  (old_top) + TagIncrement;
    top          = extract_block(old_top);
    new_top      = intptr_t(block) | tag;
block->_next=top;
    Atomic::write_barrier();
  } while ( old_top != Atomic::cmpxchg_ptr(new_top, stack_top, old_top) );

  DEBUG_ONLY (
    Log::log6(NOTAG, Log::M_GC, PrintGCDetails && loop_counter>5, true, gclog_or_tty,
             "thread 0x%llx spun %d times enqueuing RefBlock", Thread::current(), loop_counter);
  )
}


GPGC_MarkingQueue::RefBlock* GPGC_MarkingQueue::new_block()
{
  // Pick a starting point to dequeue 
  long mask   = _max_free_lists - 1;
  long cursor = os::elapsed_counter()   & mask;

RefBlock*result;

DEBUG_ONLY(Atomic::inc_ptr(&_blocks_requested);)

  for ( long i=0; i<_max_free_lists; i++ ) {
    result = pop_internal(_free_lists+cursor);
    if ( result ) {
      return result;
    }
    cursor = (cursor + 1) & mask;
  }

  result = new RefBlock();

Atomic::inc_ptr(&_blocks_allocated);

  return result;
}


void GPGC_MarkingQueue::delete_block(RefBlock* block)
{
  long mask = _max_free_lists - 1;
  long list = os::elapsed_counter() & mask;

  push_internal(_free_lists+list, block);
}


long GPGC_MarkingQueue::count_free_blocks()
{
  long count = 0;

  for ( long i=0; i<_max_free_lists; i++ ) {
    RefBlock* cursor = extract_block(_free_lists[i]);
while(cursor!=NULL){
      count ++;
      cursor = cursor->_next;
    }
  }

  return count;
}


GPGC_MarkingQueue::GPGC_MarkingQueue()
{
  _stack_top = 0;
}


// Grab the lock and throw a RefBlock onto the end of the queue.
//
void GPGC_MarkingQueue::enqueue_block(RefBlock* block)
{
  assert0(GPGC_NMT::is_new_trap_enabled() || GPGC_NMT::is_old_trap_enabled() ||
          GPGC_NewCollector::collection_state()==GPGC_Collector::MarkingVerification ||
          GPGC_OldCollector::collection_state()==GPGC_Collector::MarkingVerification);

  push_internal(&_stack_top, block);
}


// Take an array of objectRefs, copy them into RefBlock's and put the
// blocks onto the end of the queue.
//
void GPGC_MarkingQueue::enqueue_array(heapRef* array, uint length)
{
  assert0(length>0);

  RefBlock* block = GPGC_MarkingQueue::new_block();
uint next=0;
uint i=0;
 
  while ( i<length ) {
    assert0( GPGC_NMT::has_desired_new_nmt(array[i]) ||
             GPGC_NMT::has_desired_old_nmt(array[i]) );
    // This is_oop() causes problems for debugging some LVB-type problems since it may cause an LVB itself
    //assert0(array[i].as_oop()->is_oop());

    DEBUG_ONLY (
      Log::log6(NOTAG, Log::M_GC, PrintGCNMTDetails, true, gclog_or_tty,
               "enqueue ref %2d: 0x%llX", i, array[i].raw_value());
    )

    block->_refs[next++] = array[i++];
    
if(next==RefBlockLength){
      // Throw the current RefBlock onto the end of the queue
enqueue_block(block);

      DEBUG_ONLY (
        Log::log6(NOTAG, Log::M_GC, PrintGCNMTDetails, true, gclog_or_tty,
"enqueue block");
      )

      if ( i == length ) return;

      block = GPGC_MarkingQueue::new_block();
      next = 0;
    }
  }

  // Fill partial RefBlock with NULL objectRefs, and then enqueue.
  DEBUG_ONLY (
    Log::log6(NOTAG, Log::M_GC, PrintGCNMTDetails && next<RefBlockLength, true, gclog_or_tty,
             "fill partial block starting at %d", next);
  )

  for ( ; next<RefBlockLength; next++ ) {
    block->_refs[next] = nullRef;
  }

enqueue_block(block);

  DEBUG_ONLY (
    Log::log6(NOTAG, Log::M_GC, PrintGCNMTDetails, true, gclog_or_tty,
"enqueue block");
  )
}


// Pull of the first RefBlock from the queue and return it.
GPGC_MarkingQueue::RefBlock* GPGC_MarkingQueue::dequeue_block()
{
  return pop_internal(&_stack_top);
}
