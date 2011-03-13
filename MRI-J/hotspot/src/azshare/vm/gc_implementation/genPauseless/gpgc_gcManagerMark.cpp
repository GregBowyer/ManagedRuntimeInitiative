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


#include "allocation.hpp"
#include "gcTaskThread.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerMark.hpp"
#include "gpgc_gcManagerNewFinal.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_marks.hpp"
#include "heapRefBuffer.hpp"
#include "klassIds.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"


intptr_t            GPGC_GCManagerMark::_java_thread_ref_buffers_alloced = 0;
intptr_t            GPGC_GCManagerMark::_gc_thread_ref_buffers_alloced   = 0;
HeapRefBufferList** GPGC_GCManagerMark::_free_mutator_ref_buffer_list    = NULL;


//*****
//***** Static Methods
//*****

void GPGC_GCManagerMark::initialize()
{
  _free_mutator_ref_buffer_list = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);

  for ( long i=0; i<HeapRefBufferListStripes; i++ ) {
    _free_mutator_ref_buffer_list[i] = new HeapRefBufferList("mutator free lists");
  }
}


HeapRefBuffer* GPGC_GCManagerMark::pop_heap_ref_buffer(HeapRefBufferList** striped_list)
{
  assert0(is_power_of_2(HeapRefBufferListStripes));

HeapRefBuffer*result;

  long mask = HeapRefBufferListStripes - 1;
  long i    = Thread::current()->next_random() & mask;

  for ( long j=0; j<HeapRefBufferListStripes; j++ ) {
    if ( striped_list[i]->grab(&result) ) {
      assert0(result->next()==0);
      return result;
    }
    i = (i+1) & mask;
  }

  return NULL;
}


void GPGC_GCManagerMark::push_heap_ref_buffer(HeapRefBuffer* ref_buffer, HeapRefBufferList** striped_list)
{
  long mask = HeapRefBufferListStripes - 1;
  long i    = Thread::current()->next_random() & mask;

  striped_list[i]->push(ref_buffer);
}


HeapRefBuffer* GPGC_GCManagerMark::alloc_stack(HeapRefBufferList** free_striped_list)
{
  HeapRefBuffer* result = pop_heap_ref_buffer(free_striped_list);

  if ( result ) {
    assert0(result->is_empty());
    return result;
  }

  // out of empty stacks, allocate a bunch of them to get us over the hump
  HeapRefBuffer* stacks = NEW_C_HEAP_ARRAY(HeapRefBuffer, (GPGCEmptyStacks+1));
  long           mask   = HeapRefBufferListStripes - 1;
  long           i      = Thread::current()->next_random() & mask;

  memset(stacks, 0, (GPGCEmptyStacks+1)*sizeof(HeapRefBuffer));

  result = &stacks[0];

for(uint j=1;j<=GPGCEmptyStacks;j++){
    free_striped_list[i]->push(&stacks[j]);
    i = (i+1) & mask;
  }

  NOT_PRODUCT(
    if ( Thread::current()->is_Java_thread() ) {
Atomic::inc_ptr(&_java_thread_ref_buffers_alloced);
    } else {
Atomic::inc_ptr(&_gc_thread_ref_buffers_alloced);
    }
  )

  return result;
}


//***** 
//***** Instance Methods
//*****

GPGC_GCManagerMark::GPGC_GCManagerMark(long manager_number, HeapRefBufferList** free_list, HeapRefBufferList** full_list)
  : GPGC_GCManager(manager_number),
    _free_heap_ref_buffers(free_list),
    _full_heap_ref_buffers(full_list)
{
  set_current_stack(alloc_stack());
}


HeapRefBuffer* GPGC_GCManagerMark::get_full_heap_ref_buffer()
{
  HeapRefBuffer* result = pop_heap_ref_buffer(_full_heap_ref_buffers);
  assert0( result==NULL || !result->is_empty() );
  return result;
}


void GPGC_GCManagerMark::free_heap_ref_buffer(HeapRefBuffer* ref_buffer)
{
  assert0(ref_buffer->is_empty());

#ifdef ASSERT
  HeapRefBuffer::Entry* entries = ref_buffer->get_entries();
  for ( long i=0; i<HeapRefBuffer::end_index(); i++ ) {
    assert(entries[i].ref().is_null(), "freeing HeapRefBuffer that's not all zeros");
  }
#endif

  push_heap_ref_buffer(ref_buffer, _free_heap_ref_buffers);
}


bool GPGC_GCManagerMark::get_full_current_stack()
{
#ifdef ASSERT
assert0(Thread::current()->is_GC_task_thread());
GCTaskThread*thread=(GCTaskThread*)Thread::current();
    long          which  = thread->which();
    long          type   = this->manager_type();
    if ( (type&TypeGenMask) == TypeNew ) {
      assert0(thread->thread_type() == GPGC_Collector::NewCollector);
if(type==TypeNewStrong){
        assert0(this == GPGC_GCManagerNewStrong::get_manager(which));
      } else {
        assert0(this == GPGC_GCManagerNewFinal::get_manager(which));
      }
    } else {
      assert0(thread->thread_type() == GPGC_Collector::OldCollector);
if(type==TypeOldStrong){
        assert0(this == GPGC_GCManagerOldStrong::get_manager(which));
      } else {
        assert0(this == GPGC_GCManagerOldFinal::get_manager(which));
      }
    }
#endif // ASSERT

    assert0(current_stack()->is_empty());

    HeapRefBuffer* stack = get_full_heap_ref_buffer();
if(stack==NULL){
      return false;
    }

    free_heap_ref_buffer(current_stack());
    set_current_stack(stack);

    return true;
}


void GPGC_GCManagerMark::process_mutator_stack(HeapRefBuffer* stack)
{
  long size = stack->get_top();
  assert0(size != 0);

  HeapRefBuffer::Entry* entries = stack->get_entries();

  for ( long i=0; i<size; i++ ) {
    // If the object can be marked live, push it onto the marking stack for later traversal.
    objectRef ref = entries[i].ref();
oop obj=ref.as_oop();

    if ( GPGC_Marks::atomic_mark_live_if_dead(obj) ) {
      GPGC_Marks::set_markid(obj, 0x10);
      push_ref_to_stack(ref, KlassIds::mutator_stack_ref);  // Refs from the mutator never have a referring KID
    }
  }

  stack->set_top(0);
  push_heap_ref_buffer(stack, _free_mutator_ref_buffer_list);
}


bool GPGC_GCManagerMark::steal_from_remote_thread(objectRef& result, int& referrer_kid) {
  long count = manager_count();
  if ( count == 1 ) {
result=nullRef;
    return false;  // No remote thread to steal from
  }

  long manager;
  do {
    manager = uint32_t(Thread::current()->next_random()) % uint32_t(count);
  } while ( manager == manager_number() );
  assert0(manager < count);
  HeapRefBuffer* c_stack = lookup_manager(manager)->current_stack();

  Atomic::read_barrier();

  uint32_t r_top = c_stack->get_top();
  if ( r_top > 0 ) {
    uint32_t indx = uint32_t(Thread::current()->next_random()) % r_top;
    intptr_t ref;
    assert0(indx < r_top);
    if ( c_stack->swap_remote(ref, referrer_kid, indx) ) {
      result = objectRef(ref);
      return true;
    }
  }
result=nullRef;
  return false;
}


bool GPGC_GCManagerMark::striped_list_is_empty(HeapRefBufferList** striped_list)
{
  for ( long i=0; i<HeapRefBufferListStripes; i++ ) {
    if ( striped_list[i]->head() != 0 ) {
      return false;
    }
  }

  return true;
}


//***** 
//***** Debugging Methods
//*****


void GPGC_GCManagerMark::verify_empty_heap_ref_buffers(HeapRefBufferList** striped_list, const char* list, const char* tag)
{
  for ( long i=0; i<HeapRefBufferListStripes; i++ ) {
    if ( striped_list[i]->head() != 0) {
      fatal3("Finished GC phase with entries in %s stripe %d (%s)", list, i, tag);
    }
  }
}


long GPGC_GCManagerMark::count_heap_ref_buffers(HeapRefBufferList** striped_list)
{
  long size = 0;

  for ( long i=0; i<HeapRefBufferListStripes; i++ ) {
    size += striped_list[i]->list_length();
  }

  return size;
}
