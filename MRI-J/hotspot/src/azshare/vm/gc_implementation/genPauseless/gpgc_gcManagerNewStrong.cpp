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
#include "gpgc_gcManagerMark.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_newCollector.inline.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_threadRefLists.hpp"
#include "klassIds.hpp"

#include "prefetch_os_pd.inline.hpp"


GPGC_GCManagerNewStrong**GPGC_GCManagerNewStrong::_manager_array=NULL;
long                      GPGC_GCManagerNewStrong::_manager_count                = 0;
HeapRefBufferList**       GPGC_GCManagerNewStrong::_free_ref_buffer_list         = NULL;
HeapRefBufferList**       GPGC_GCManagerNewStrong::_full_ref_buffer_list         = NULL;
HeapRefBufferList**       GPGC_GCManagerNewStrong::_full_mutator_ref_buffer_list = NULL;


//*****
//*****  Closure Methods
//*****

void GPGC_NewGC_MarkPushClosure::do_oop(objectRef*p){
  DEBUG_ONLY( objectRef ref = PERMISSIVE_UNPOISON(*p, p); )
  assert0( ref.is_null() || ref.is_heap() );
  GPGC_NewCollector::mark_and_push(_gcm, (heapRef*)p, KlassIds::new_system_root);
}


void GPGC_NewGC_MarkPushClosure::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
  if ( check_derived_oops() ) {
    GPGC_NewCollector::do_derived_oop(base_ptr, derived_ptr);
  } else {
    DEBUG_ONLY( GPGC_NewCollector::ensure_base_not_relocating(base_ptr); )
  }
}


void GPGC_NewGC_RootMarkPushClosure::do_oop(objectRef*p){
  DEBUG_ONLY( objectRef ref = PERMISSIVE_UNPOISON(*p, p); )
  assert0( ref.is_null() || ref.is_heap() );
  GPGC_NewCollector::mark_and_push_from_New_root(_gcm, (heapRef*)p);
}


void GPGC_NewToOldGC_RootMarkPushClosure::do_oop(objectRef*p){
  DEBUG_ONLY( objectRef ref = PERMISSIVE_UNPOISON(*p, p); )
  assert0( ref.is_null() || ref.is_heap() );
  GPGC_NewCollector::mark_and_push_from_NewToOld_root(_gcm, (heapRef*)p);
}


void GPGC_NewGC_RootMarkPushClosure::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
  if ( check_derived_oops() ) {
    GPGC_NewCollector::do_derived_oop(base_ptr, derived_ptr);
  } else {
    DEBUG_ONLY( GPGC_NewCollector::ensure_base_not_relocating(base_ptr); )
  }
}


void GPGC_NewToOldGC_RootMarkPushClosure::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
  if ( check_derived_oops() ) {
    GPGC_NewCollector::NewToOld_do_derived_oop(base_ptr, derived_ptr);
  } else {
    DEBUG_ONLY( GPGC_NewCollector::NewToOld_ensure_base_not_relocating(base_ptr); )
  }
}


//*****
//*****  Static Methods
//*****

void GPGC_GCManagerNewStrong::initialize()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"Sanity");

assert(_manager_array==NULL,"Attempted to initialize twice.");

  _manager_count                = GenPauselessNewThreads;
  _manager_array                = NEW_C_HEAP_ARRAY(GPGC_GCManagerNewStrong*, _manager_count);
  _free_ref_buffer_list         = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _full_ref_buffer_list         = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _full_mutator_ref_buffer_list = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);

  for ( long i=0; i<HeapRefBufferListStripes; i++ ) {
    _free_ref_buffer_list[i]         = new HeapRefBufferList("new strong free lists");
    _full_ref_buffer_list[i]         = new HeapRefBufferList("new strong full lists");
    _full_mutator_ref_buffer_list[i] = new HeapRefBufferList("new mutator full lists");
  }

  for ( long i=0; i<_manager_count; i++ ) {
    _manager_array[i] = new GPGC_GCManagerNewStrong(i);
  }
}


void GPGC_GCManagerNewStrong::verify_mutator_ref_buffer(HeapRefBuffer* ref_buffer)
{
  long size = ref_buffer->get_top();
  assert0(size != 0);

  long collection_state = GPGC_NewCollector::collection_state();
  long sanity_check     = GPGC_NewCollector::mutator_ref_sanity_check();

  // Try and verify that the mutator enqueued objectRefs meet the expectations of the algorithm.
  //
  // For NewGC, it's fairly straight forward:
  //
  //  -  During normal concurrent marking, we expect to see objectRefs enqueued by the mutator.
  //  -  After we transition to concurrent ref processing, we only expect to see objectRefs enqueued that are
  //     already marked FinalLive.
  //  -  Once we complete FinalRef processing, we expect to see no refs enqueued by the mutator.

  guarantee((collection_state>=GPGC_Collector::ConcurrentMarking) && (collection_state<=GPGC_Collector::ConcurrentRefProcessing),
"Invalid NewGC collector state for mutator ref buffers");
  guarantee(sanity_check != GPGC_Collector::NoMutatorRefs, "Mutators have NewGC buffered refs at invalid collector state");

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
      assert0(ref.not_null());
      assert0(ref.is_new());
      assert0(!GPGC_ReadTrapArray::is_remap_trapped(ref));

      bool strong_live = GPGC_Marks::is_any_marked_strong_live(obj);
      bool final_live  = GPGC_Marks::is_any_marked_final_live(obj);

      switch (sanity_check) {
        case GPGC_Collector::NoMutatorRefs:
          fatal2("NewGC mutator found ref outside marking, strong live %d, final live %d", strong_live, final_live);
          break;
        case GPGC_Collector::MustBeFinalLive:
          // Normal marking is done, but ref processing is still in progress.  Only StrongLive or FinalLive objects expected.
          if ( ! (strong_live || final_live) ) {
            fatal2("NewGC non live mutator ref, strong live %d, final live %d", strong_live, final_live);
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


void GPGC_GCManagerNewStrong::push_mutator_stack_to_full(HeapRefBuffer** buffer)
{
  verify_mutator_ref_buffer(*buffer);

  push_heap_ref_buffer(*buffer, _full_mutator_ref_buffer_list);

  *buffer = GPGC_GCManagerMark::alloc_mutator_stack();
}


void GPGC_GCManagerNewStrong::flush_nto_marking_stacks()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm = get_manager(i);
    if ( ! gcm->_new_to_old_marking_stack->is_empty() ) {
      GPGC_GCManagerMark::push_heap_ref_buffer(gcm->_new_to_old_marking_stack, GPGC_GCManagerOldStrong::full_ref_buffer_list());
      gcm->_new_to_old_marking_stack = GPGC_GCManagerOldStrong::alloc_heap_ref_buffer();
    }
  }
}


void GPGC_GCManagerNewStrong::reset_reference_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

ref_lists->reset();
  }
}


void GPGC_GCManagerNewStrong::save_reference_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

    GPGC_NewCollector::jlr_handler()->save_ref_lists(ref_lists);
  }
}


void GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty(const char* tag)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm = get_manager(i);
    if ( ! gcm->current_stack()->is_empty() ) {
      fatal2("NewStrong current stack isn't empty for manager %d (%s)", i, tag);
    }
  }

  GPGC_GCManagerMark::verify_empty_heap_ref_buffers(_full_ref_buffer_list, "NewStrong", tag);
}


void GPGC_GCManagerNewStrong::ensure_all_nto_stacks_are_empty(const char* tag)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm = get_manager(i);
    if ( ! gcm->_new_to_old_marking_stack->is_empty() ) {
      fatal2("NewStrong NTO stack isn't empty for manager %d (%s)", i, tag);
    }
  }
}


void GPGC_GCManagerNewStrong::ensure_mutator_list_is_empty(const char* tag)
{
  GPGC_GCManagerMark::verify_empty_heap_ref_buffers(_full_mutator_ref_buffer_list, "NewStrong mutator", tag);
}


void GPGC_GCManagerNewStrong::verify_empty_ref_lists()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

    ref_lists->verify_empty_ref_lists();
  }
}


void GPGC_GCManagerNewStrong::oops_do_ref_lists(OopClosure* f) 
{ 
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewStrong* gcm       = get_manager(i);
    GPGC_ThreadRefLists*     ref_lists = gcm->reference_lists();

ref_lists->oops_do(f);
  }
}


//*****
//*****  Instance Methods
//*****

GPGC_GCManagerNewStrong::GPGC_GCManagerNewStrong(long manager_number)
  : GPGC_GCManagerNew(manager_number, _free_ref_buffer_list, _full_ref_buffer_list),
    _reference_lists(),
    _mark_push_closure(this),
    _new_root_mark_push_closure(this),
    _nto_root_mark_push_closure(this)
{
  _new_to_old_marking_stack = alloc_stack();
}


void GPGC_GCManagerNewStrong::update_live_referent(objectRef* referent_addr, int referrer_kid)
{
  GPGC_NewCollector::mark_and_push(this, referent_addr, referrer_kid);
}


void GPGC_GCManagerNewStrong::mark_and_push_referent(objectRef* referent_addr, int referrer_kid)
{
  GPGC_NewCollector::mark_and_push(this, referent_addr, referrer_kid);
}


void GPGC_GCManagerNewStrong::push_referent_without_mark(objectRef* referent_addr, int referrer_kid)
{
  push_array_chunk_to_stack(referent_addr, 1, referrer_kid);
}
