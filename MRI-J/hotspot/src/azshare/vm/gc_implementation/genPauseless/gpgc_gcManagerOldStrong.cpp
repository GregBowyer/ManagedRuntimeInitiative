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
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerMark.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "growableArray.hpp"
#include "heapRefBuffer.hpp"
#include "klass.hpp"
#include "klassIds.hpp"
#include "taskqueue.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

GPGC_GCManagerOldStrong**GPGC_GCManagerOldStrong::_manager_array=NULL;
long                      GPGC_GCManagerOldStrong::_manager_count                = 0;
HeapRefBufferList**       GPGC_GCManagerOldStrong::_free_ref_buffer_list         = NULL;
HeapRefBufferList**       GPGC_GCManagerOldStrong::_full_ref_buffer_list         = NULL;
HeapRefBufferList**       GPGC_GCManagerOldStrong::_full_mutator_ref_buffer_list = NULL;
GenericTaskQueueSet<Klass*>*     GPGC_GCManagerOldStrong::_revisit_array                = NULL;


//*****
//*****  GPGC_OldGC_MarkPushClosure Methods
//*****

void GPGC_OldGC_MarkPushClosure::do_oop(objectRef*p){
  DEBUG_ONLY( objectRef ref = PERMISSIVE_UNPOISON(*p, p); )
  assert0( ref.is_null() || ref.is_heap() );
  GPGC_OldCollector::mark_and_push(_gcm, (heapRef*)p, KlassIds::old_system_root);
}


void GPGC_OldGC_MarkPushClosure::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
  if ( check_derived_oops() ) {
    GPGC_OldCollector::do_derived_oop(base_ptr, derived_ptr);
  } else {
    DEBUG_ONLY( GPGC_OldCollector::ensure_base_not_relocating(base_ptr); )
  }
}


//*****
//*****  Static Methods
//*****

void GPGC_GCManagerOldStrong::initialize()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"Sanity");

assert(_manager_array==NULL,"Attempted to initialize twice.");

  _manager_count                = GenPauselessOldThreads;
  _manager_array                = NEW_C_HEAP_ARRAY(GPGC_GCManagerOldStrong*, _manager_count);
  _free_ref_buffer_list         = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _full_ref_buffer_list         = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _full_mutator_ref_buffer_list = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _revisit_array                = new GenericTaskQueueSet<Klass*>(_manager_count);

for(int i=0;i<HeapRefBufferListStripes;i++){
    _free_ref_buffer_list[i]         = new HeapRefBufferList("old strong free lists");
    _full_ref_buffer_list[i]         = new HeapRefBufferList("old strong full lists");
    _full_mutator_ref_buffer_list[i] = new HeapRefBufferList("old mutator full lists");
  }

for(int i=0;i<_manager_count;i++){
    _manager_array[i] = new GPGC_GCManagerOldStrong(i);
    _revisit_array->register_queue(i, _manager_array[i]->revisit_klass_stack());
  }
}


void GPGC_GCManagerOldStrong::verify_mutator_ref_buffer(HeapRefBuffer* ref_buffer)
{
  long size = ref_buffer->get_top();
  assert0(size != 0);

  long collection_state = GPGC_OldCollector::collection_state();
  long sanity_check     = GPGC_OldCollector::mutator_ref_sanity_check();

  // Try and verify that the mutator enqueued objectRefs meet the expectations of the algorithm.
  //
  // For NewGC, it's fairly straight forward:
  //
  //  -  During normal concurrent marking, we expect to see objectRefs enqueued by the mutator.
  //  -  After we transition to concurrent ref processing, we only expect to see objectRefs enqueued that are
  //     already marked FinalLive.
  //  -  Once we complete FinalRef processing, we expect to see no refs enqueued by the mutator.
  //
  // For OldGC, it's more complicated.  There are a variety of weak roots into OldGen/PermGen that
  // are cleaned up after the final marking safepoint.  Once concurrent marking is done, any weak
  // root accessed by a mutator is expected to be already marked StrongLive.  So the NewGC rules
  // are followed, but with the exception that we'll see enqueued objectRefs to StrongLive objects
  // right through the end of concurrent weak root processing.

  guarantee((collection_state>=GPGC_Collector::ConcurrentMarking) && (collection_state<=GPGC_Collector::ConcurrentWeakMarking),
"Invalid OldGC collector state for mutator ref buffers");

#ifdef ASSERT
  Thread* current_thread = Thread::current();
  bool is_gc_mode = current_thread->is_gc_mode();
  if ( ! is_gc_mode ) {
    current_thread->set_gc_mode(true);
  }

  HeapRefBuffer::Entry* entries = ref_buffer->get_entries();

  if ( sanity_check != GPGC_Collector::NoSanityCheck ) {
    for ( long i=0; i<size; i++ ) {
      objectRef ref = entries[i].ref();
oop obj=ref.as_oop();
      assert0(ref.is_old());
      assert0(!GPGC_ReadTrapArray::is_remap_trapped(ref));

      bool strong_live = GPGC_Marks::is_any_marked_strong_live(obj);
      bool final_live  = GPGC_Marks::is_any_marked_final_live(obj);

      switch (sanity_check) {
        case GPGC_Collector::NoMutatorRefs:
          fatal2("OldGC mutator found ref outside marking, strong live %d, final live %d", strong_live, final_live);
          break;
        case GPGC_Collector::MustBeFinalLive:
          // Normal marking is done, but ref processing is still in progress.  Only StrongLive or FinalLive objects expected.
          if ( ! (strong_live || final_live) ) {
            fatal2("OldGC non live mutator ref, strong live %d, final live %d", strong_live, final_live);
          }
          break;
        case GPGC_Collector::MustBeStrongLive:
          // Marking is done, but weak root cleanup is not, only StrongLive objects expected.
          if ( ! strong_live ) {
fatal1("OldGC non StrongLive mutator ref, final live %d",final_live);
          }
          break;
        default:
ShouldNotReachHere();//Unknown sanity check state.
      }
    }
  }

  if ( ! is_gc_mode ) {
    current_thread->set_gc_mode(false);
  }
#endif // ASSERT
}


void GPGC_GCManagerOldStrong::push_mutator_stack_to_full(HeapRefBuffer** buffer)
{
  verify_mutator_ref_buffer(*buffer);

  push_heap_ref_buffer(*buffer, _full_mutator_ref_buffer_list);

  *buffer = GPGC_GCManagerMark::alloc_mutator_stack();
}


void GPGC_GCManagerOldStrong::pre_mark()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm = get_manager(i);
 
    gcm->revisit_overflow_stack()->clear();
    gcm->dec_implementors_stack()->clear();
  }
}


void GPGC_GCManagerOldStrong::post_relocate()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm = get_manager(i);
 
    gcm->revisit_overflow_stack()->clear();
    gcm->dec_implementors_stack()->clear();
  }
}


bool GPGC_GCManagerOldStrong::steal_revisit_klass(int queue_num, int* seed, Klass*& t)
{
return revisit_array()->steal(queue_num,seed,t);
}


void GPGC_GCManagerOldStrong::reset_reference_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

ref_lists->reset();
  }
}


void GPGC_GCManagerOldStrong::save_reference_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

    GPGC_OldCollector::jlr_handler()->save_ref_lists(ref_lists);
  }
}


void GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty(const char* tag)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm = get_manager(i);
    if ( ! gcm->current_stack()->is_empty() ) {
      fatal2("OldStrong current stack isn't empty for manager %d (%s)", i, tag);
    }
  }

  GPGC_GCManagerMark::verify_empty_heap_ref_buffers(_full_ref_buffer_list, "OldStrong", tag);
}


void GPGC_GCManagerOldStrong::ensure_revisit_stacks_are_empty()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm = get_manager(i);
    if ( gcm->revisit_klass_stack()->size() != 0 ) {
fatal1("klass revisit stack isn't empty for manager %d",i);
    }
  }
}


void GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty(const char* tag)
{
  GPGC_GCManagerMark::verify_empty_heap_ref_buffers(_full_mutator_ref_buffer_list, "OldStrong mutator", tag);
}


void GPGC_GCManagerOldStrong::verify_empty_ref_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

    ref_lists->verify_empty_ref_lists();
  }
}


void GPGC_GCManagerOldStrong::oops_do_ref_lists(OopClosure* f)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

ref_lists->oops_do(f);
  }
}


//*****
//*****  Instance Methods
//*****


GPGC_GCManagerOldStrong::GPGC_GCManagerOldStrong(long manager_number)
  : GPGC_GCManagerOld(manager_number, _free_ref_buffer_list, _full_ref_buffer_list),
    _reference_lists(),
    _mark_push_closure(this)
{
_revisit_overflow_stack=new(ResourceObj::C_HEAP)GrowableArray<Klass*>(500,true);
revisit_klass_stack()->initialize();

_dec_implementors_stack=new(ResourceObj::C_HEAP)GrowableArray<instanceKlass*>(20,true);
}


void GPGC_GCManagerOldStrong::drain_revisit_stack()
{
  for ( long i=0; i<_revisit_overflow_stack->length(); i++ ) {
_revisit_overflow_stack->at(i)->GPGC_follow_weak_klass_links();
  }

  do {
Klass*k;
    while (revisit_klass_stack()->pop_local(k)) {
k->GPGC_follow_weak_klass_links();
    }
  } while (revisit_klass_stack()->size() != 0);
}


void GPGC_GCManagerOldStrong::update_live_referent(objectRef* referent_addr, int referrer_kid)
{
  GPGC_OldCollector::mark_and_push(this, referent_addr, referrer_kid);
}


void GPGC_GCManagerOldStrong::mark_and_push_referent(objectRef* referent_addr, int referrer_kid)
{
  GPGC_OldCollector::mark_and_push(this, referent_addr, referrer_kid);
}


void GPGC_GCManagerOldStrong::push_referent_without_mark(objectRef* referent_addr, int referrer_kid)
{
  push_array_chunk_to_stack(referent_addr, 1, referrer_kid);
}
