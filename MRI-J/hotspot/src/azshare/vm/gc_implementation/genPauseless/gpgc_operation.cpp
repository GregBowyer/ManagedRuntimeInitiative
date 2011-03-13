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

#include "gpgc_operation.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_thread.hpp"
#include "os.hpp"

#include "os_os.inline.hpp"

GPGC_OperationQueue::GPGC_OperationQueue()
{
  // The queue is a circular double-linked list, which always contains one element.
  // i.e., one element means empty.
  _queue_length  = 0;
  _queue_counter = 0;
  _queue         = new GPGC_Operation();
  _queue->set_next(_queue);
  _queue->set_prev(_queue);
}


GPGC_OperationQueue::~GPGC_OperationQueue()
{
  // Deleting queue's which aren't empty is an error (except during VM shutdoown).
  if ( ! GPGC_Thread::should_terminate() ) {
    guarantee(this->is_empty(), "GPGC_OperationQueue must be empty before delete");
  }
  
  delete _queue;
_queue=NULL;
}


bool GPGC_OperationQueue::is_empty()
{
  // The queue is empty if there's exactly one element.
  bool empty = (_queue == _queue->next());
  assert( (_queue_length == 0 && empty) ||
          (_queue_length > 0  && !empty), "sanity check");
  return _queue_length == 0;
}
bool GPGC_OperationQueue::not_empty()
{
  return ! is_empty();
}


void GPGC_OperationQueue::add(GPGC_Operation* op)
{
  assert(_queue->prev()->next() == _queue && _queue->next()->prev() == _queue, "sanity check");

  // Insert the new GPGC_Operation at the back of the queue.
  op->set_prev(_queue->prev());
op->set_next(_queue);
  _queue->prev()->set_next(op);
  _queue->set_prev(op);

  _queue_length ++;
}


GPGC_Operation* GPGC_OperationQueue::remove_next()
{
if(_queue_length==0)return NULL;

  GPGC_Operation* result = _queue->next();
assert(result!=_queue,"cannot remove base element");

  assert(result->prev()->next()==result && result->next()->prev()==result, "sanity check");

_queue->set_next(result->next());
result->next()->set_prev(_queue);

  _queue_length --;

  return result;
}


GPGC_Operation::GPGC_Operation()
  :  _calling_thread(NULL),
     _next(NULL),
_prev(NULL)
{
_start_time=os::elapsed_counter();
}


// This should be called when an operation is done.  For synchronous operations, it signals
// the requesting thread.  For async operations, it deallocates the memory.
void GPGC_Operation::complete_operation(GPGC_Operation* op)
{
  // Last access of info in op!
  bool c_heap_allocated = op->is_cheap_allocated();

  // Mark as completed
  Thread* caller = op->calling_thread();
  if (caller != NULL) {
    // Not an async operation.
    caller->increment_vm_operation_completed_count();
  }

  // It is unsafe to access the VM_Operation after the 'increment_vm_operation_completed_count'
  // call, since the caller might have deallocated the operation.
  if (c_heap_allocated) {
    delete op;
  }
}


HeapWord* GPGC_NewAllocOperation::allocate()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"must be a GenPauselessHeap");

  set_result         (GPGC_Space::new_gen_allocate(word_size(), is_tlab()));
  set_result_gc_count(heap->actual_new_collections());

  // The GC count is set to actual_new_collections, because it's a NewGC that would invalidate
  // a NewGen allocated address.

  return result();
}


HeapWord* GPGC_PermAllocOperation::allocate()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"must be a GenPauselessHeap");

  set_result         (GPGC_Space::perm_gen_allocate(word_size()));
  set_result_gc_count(heap->actual_old_collections());

  // The GC count is set to actual_old_collections, because it's an OldGC that would invalidate
  // a PermGen allocated address.

  return result();
}
