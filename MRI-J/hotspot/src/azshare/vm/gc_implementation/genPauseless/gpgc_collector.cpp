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

#include "cycleCounts.hpp"
#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_tasks.hpp"
#include "gpgc_tlb.hpp"
#include "gpgc_thread.hpp"
#include "interfaceSupport.hpp"
#include "javaClasses.hpp"
#include "klass.hpp"
#include "os.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "timer.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

void GPGC_Collector::fixup_derived_oop(objectRef* derived_ptr, objectRef old_base, objectRef new_base)
{
Unimplemented();//TODO-NOW: Need to implement derived-oop handling for C2.  Awais is on the case.
 /*
 // Need to figure out derived oop handling for x86.

  objectRef old_derived = *derived_ptr;
  
 intptr_t offset = intptr_t(old_derived.as_address()) - intptr_t(old_base.as_address());
  
  // // TODO: maw: this assert only works if the pointer part of derived can't intrude into the the
  // // meta-data part of derived.  The max offset added to the base pointer by C2 is 2^31 words, or
  // // 2^34 bytes.  So we need to make sure that we don't use pages within 16GB (2^34) of the object
  // // address space limit.
  assert0(old_derived.klass_id() == old_base.klass_id());
  assert0(old_base.is_new() || old_base.is_old());
  assert0(old_derived.space_id() == old_base.space_id());
  
  objectRef new_derived;
  
  new_derived.set_raw_value(uint64_t(intptr_t(new_base.raw_value()) + offset));
  
  *derived_ptr = new_derived;
  */
}


void GPGC_Collector::log_checkpoint_times(TimeDetailTracer* tdt, SafepointTimes* times, const char* tag, const char* label)
{
  if ( tdt->details() ) {
      double freq            = double(os::elapsed_frequency()) / 1000.0;
      double threads_lock_ms = double(times->threads_lock_acquired  - times->acquire_threads_lock) / freq;
      double notify_ms       = double(times->all_threads_notified   - times->begin_threads_notify) / freq;
      double wait_ms         = double(times->safepoint_reached      - times->all_threads_notified) / freq;
      double boosted_wait_ms = 0;

      if ( times->priority_boosted != 0 ) {
        boosted_wait_ms      = double(times->safepoint_reached - times->priority_boosted) / freq;
      }

      double closure_ms      = double(times->thread_closure) / freq;

      GCLogMessage::log_a(tdt->details(), tdt->stream(), true,
"%s %s Checkpoint: "
"lock %3.3f, notify %3.3f, wait %3.3f, boosted wait %3.3f, closure %3.3f millis: "
"threads %lld, boosted %lld, self chkp %lld",
                        tag, label,
                        threads_lock_ms, notify_ms, wait_ms - closure_ms, boosted_wait_ms, closure_ms,
                        times->total_threads, times->priority_boosts, times->self_checkpoints);
  }
}


class GPGC_NMTFlushClosure: public JavaThreadClosure {
  private:
    intptr_t _flushed_new_refs;
    intptr_t _flushed_old_refs;
  public:
    GPGC_NMTFlushClosure() : _flushed_new_refs(0), _flushed_old_refs(0) {}
    long flushed_new_refs() { return _flushed_new_refs; }
    long flushed_old_refs() { return _flushed_old_refs; }
void flush_ref_buffers(Thread*thread){
      assert0(thread->is_Java_thread() || thread->is_VM_thread());

      intptr_t refs = thread->get_new_gen_ref_buffer()->get_top();
      if ( refs > 0 ) {
        GPGC_GCManagerNewStrong::push_mutator_stack_to_full(thread->get_new_gen_ref_buffer_addr());
Atomic::add_ptr(refs,&_flushed_new_refs);
      }

      refs = thread->get_old_gen_ref_buffer()->get_top();
      if ( refs > 0 ) {
        GPGC_GCManagerOldStrong::push_mutator_stack_to_full(thread->get_old_gen_ref_buffer_addr());
Atomic::add_ptr(refs,&_flushed_old_refs);
      }
    }
    void do_java_thread(JavaThread* jt) {
      flush_ref_buffers(jt);
    }
};


// Flush any enqueued NMT heapRefs from the JavaThreads, and enqueue them
// for processing by the garbage collector threads.
void GPGC_Collector::flush_mutator_ref_buffers(TimeDetailTracer* tdt, const char* tag, long& new_gen_refs, long& old_gen_refs)
{
SafepointTimes times;

  {
    DetailTracer dt(tdt, false, "%s: NMT flushing: ", tag);

    new_gen_refs = 0;
    old_gen_refs = 0;

    GPGC_NMTFlushClosure nmtfc;
    GPGC_Safepoint::do_checkpoint(&nmtfc, &times);
if(GPGC_Safepoint::is_at_safepoint()){
      // In various debug modes, we might not be marking concurrently, and need to flush the VMThread's ref buffers here.
      // TODO: Why is this needed?  How does the VMThread have it's buffers cleared when we're doing concurrent marking?
      nmtfc.flush_ref_buffers(VMThread::vm_thread());
    }

    new_gen_refs = nmtfc.flushed_new_refs();
    old_gen_refs = nmtfc.flushed_old_refs();

    dt.print("%d new refs, %d old refs", new_gen_refs, old_gen_refs);
  }

if(!GPGC_Safepoint::is_at_safepoint()){
    log_checkpoint_times(tdt, &times, tag, "flush_refs");
  }
}


void GPGC_Collector::mark_all_discovered(TimeDetailTracer* tdt, CollectorAge age)
{
  {
    DetailTracer dt(tdt, false, "%s: Cleanup marking", ((age==NewCollector)?"N":"O"));

    // Do another round of parallel marking to finish up any flushed refs.
PGCTaskQueue*q=PGCTaskQueue::create();

    long            threads;
    PGCTaskManager* manager;

    if ( age == NewCollector ) {
threads=GenPauselessNewThreads;
      manager = GPGC_Heap::new_gc_task_manager();
    } else {
threads=GenPauselessOldThreads;
      manager = GPGC_Heap::old_gc_task_manager();
    }

    if ( age == NewCollector ) {
      for (long j=0; j<threads; j++) { q->enqueue( new GPGC_NewGC_StrongMarkTask() ); }
    } else {
      for (long j=0; j<threads; j++) { q->enqueue( new GPGC_OldGC_StrongMarkTask() ); }
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, manager, q);
  }

#ifdef ASSERT
  if ( age == NewCollector ) {
    assert0(GPGC_GCManagerNew::working_count() == 0);
  } else {
    assert0(GPGC_GCManagerOld::working_count() == 0);
  }
#endif // ASSERT
}


void GPGC_Collector::mark_all_final_discovered(TimeDetailTracer* tdt, CollectorAge age)
{
  {
    DetailTracer dt(tdt, false, "%s: Final-Live cleanup marking", ((age==NewCollector)?"N":"O"));

    // Do another round of parallel marking to finish up any flushed refs.
PGCTaskQueue*q=PGCTaskQueue::create();

    long            threads;
    PGCTaskManager* manager;

    if ( age == NewCollector ) {
threads=GenPauselessNewThreads;
      manager = GPGC_Heap::new_gc_task_manager();
    } else {
threads=GenPauselessOldThreads;
      manager = GPGC_Heap::old_gc_task_manager();
    }

    if ( age == NewCollector ) {
      for (long j=0; j<threads; j++) { q->enqueue( new GPGC_NewGC_FinalMarkTask() ); }
    } else {
      for (long j=0; j<threads; j++) { q->enqueue( new GPGC_OldGC_FinalMarkTask() ); }
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, manager, q);
  }

#ifdef ASSERT
  if ( age == NewCollector ) {
    assert0(GPGC_GCManagerNew::working_count() == 0);
  } else {
    assert0(GPGC_GCManagerOld::working_count() == 0);
  }
#endif // ASSERT
}


objectRef GPGC_Collector::old_remap_nmt_and_rewrite(objectRef* addr)
{
  objectRef old_ref = UNPOISON_OBJECTREF(*addr, addr);

  assert0(old_ref.is_null() || old_ref.is_old());

  if ( old_ref.is_null() ) { return old_ref; }

objectRef new_ref;

  if ( GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref));
    new_ref = GPGC_Collector::get_forwarded_object(old_ref);
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }
  else if ( ! GPGC_NMT::has_desired_old_nmt(old_ref) ) {
    new_ref = old_ref;
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }
  else {
    // No need to remap or NMT.
    return old_ref;
  }

  intptr_t old_value = POISON_OBJECTREF(old_ref, addr).raw_value();
  intptr_t new_value = POISON_OBJECTREF(new_ref, addr).raw_value();

  Atomic::cmpxchg_ptr(new_value, (intptr_t*)addr, old_value);

  return new_ref;
}


objectRef GPGC_Collector::mutator_relocate_only(objectRef* addr)
{
  objectRef old_ref = UNPOISON_OBJECTREF(*addr, addr);

  assert0(old_ref.is_null() || old_ref.is_heap());

  if ( old_ref.is_null() || !GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    return old_ref;
  }

  Thread* thread = Thread::current();
  assert0( !thread->is_GenPauselessGC_thread() && !thread->is_GC_task_thread() );

  GCModeSetter gcmode(thread);

  objectRef new_ref = GPGC_Collector::mutator_relocate_object(heapRef(old_ref));

  assert0( new_ref.not_null() );

  return new_ref;
}


objectRef GPGC_Collector::mutator_relocate(objectRef ref)
{
  assert0(ref.is_null() || ref.is_heap());

  if ( ref.is_null() || !GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
    return ref;
  }

  Thread* thread = Thread::current();
  assert0( !thread->is_GenPauselessGC_thread() && !thread->is_GC_task_thread() );

  GCModeSetter gcmode(thread);

  objectRef new_ref = GPGC_Collector::mutator_relocate_object(heapRef(ref));

  assert0( new_ref.not_null() );

  return new_ref;
}


objectRef GPGC_Collector::remap_only(objectRef const* addr)
{
  objectRef old_ref = UNPOISON_OBJECTREF(*(objectRef*)addr, (objectRef*)addr);

  assert0(old_ref.is_null() || old_ref.is_heap());

  if ( old_ref.is_null() || !GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    return old_ref;
  }

  objectRef new_ref = GPGC_Collector::get_forwarded_object(old_ref);

  assert0( new_ref.not_null() );

  return new_ref;
}


// Return an objectRef read from an address.  If the objectRef is in old-space, it is returned
// remapped to it's current location.
objectRef GPGC_Collector::old_gc_remapped(objectRef* addr)
{
  objectRef old_ref = ALWAYS_UNPOISON_OBJECTREF(*addr);
    
  if ( old_ref.is_null() || old_ref.is_new() ) {
    return old_ref;
  }

  assert(old_ref.is_old(), "found a non-heapRef" );

  if ( ! GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
    return old_ref;
  }

  assert0( GPGC_ReadTrapArray::is_old_gc_remap_trapped(old_ref) );
  
  objectRef new_ref = GPGC_Collector::get_forwarded_object(old_ref);

  return new_ref;
}


// Return a heapRef read from an address, remapped to a new location if needed.  Assert that
// the object is actually in perm.
objectRef GPGC_Collector::perm_remapped_only(objectRef* addr)
{
  objectRef ref = UNPOISON_OBJECTREF(*addr, addr);
    
if(ref.not_null()){
    assert( ref.is_old(), "Object not in OldGen" );
    DEBUG_ONLY( GPGC_PageInfo* info = GPGC_PageInfo::page_info( GPGC_Layout::addr_to_PageNum(ref.as_oop()) ); )
    assert( info->just_gen() == GPGC_PageInfo::PermGen, "Object not in perm" );

    if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
      assert0( GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref) );
  
      ref = GPGC_Collector::get_forwarded_object(ref);
    }
  }
  
  return ref;
}


// Return the new location of a relocated object.  This method is only for use after
// it's certain that the object has already been relocated, so the forwarded pointer
// is guaranteed to be immediately present.
objectRef GPGC_Collector::get_forwarded_object(objectRef old_ref)
{
  assert0( old_ref.is_heap() );
  assert0( GPGC_ReadTrapArray::is_remap_trapped(old_ref) );

  oop            old_obj = old_ref.as_oop();
  PageNum        page    = GPGC_Layout::addr_to_BasePageForSpace(old_obj);
  GPGC_PageInfo* info    = GPGC_PageInfo::page_info(page);
heapRef new_ref;

  if ( GPGC_Layout::large_space_page(page) ) {
    new_ref = GPGC_PageRelocation::relocated_multi_ref(old_ref, info->ll_next());
  } else {
    GPGC_ObjectRelocation* forward = GPGC_PageRelocation::find_object(page, info, old_obj);

    assert0( forward && forward->is_relocated() );

    new_ref = forward->relocated_ref(info->relocate_space(), old_ref);
  }

  assert0( new_ref.not_null() );
  assert0( GPGC_NMT::has_desired_nmt(new_ref) );

  return new_ref;
}


objectRef GPGC_Collector::remap(objectRef old_ref)
{
  // TODO: should this just be a is_remap_trapped() test and a call to get_forwarded_object()?
if(old_ref.not_null()){
    assert0( old_ref.is_heap() );

    if ( GPGC_ReadTrapArray::is_remap_trapped(old_ref) ) {
      oop            old_obj = old_ref.as_oop();
      PageNum        page    = GPGC_Layout::addr_to_BasePageForSpace(old_obj);
      GPGC_PageInfo* info    = GPGC_PageInfo::page_info(page);

      if ( GPGC_Layout::large_space_page(page) ) {
        old_ref = GPGC_PageRelocation::relocated_multi_ref(old_ref, info->ll_next());
      } else {
        GPGC_ObjectRelocation* forward = GPGC_PageRelocation::find_object(page, info, old_obj);

        assert0( forward && forward->is_relocated() );

        old_ref = forward->relocated_ref(info->relocate_space(), old_ref);
      }

      assert0( old_ref.not_null() );
    }
  }

  return old_ref;
}


// Relocate an object, or get the new location if the object is already relocated.
// Return an updated heapRef that shows the new location of the object.
heapRef GPGC_Collector::mutator_relocate_object(heapRef old_ref)
{
  assert0( old_ref.is_heap() );
  assert0( GPGC_ReadTrapArray::is_remap_trapped(old_ref) );
  assert0( !Thread::current()->is_GC_task_thread() );

  oop            old_obj  = old_ref.as_oop();
  PageNum        old_page = GPGC_Layout::addr_to_BasePageForSpace(old_obj);
  GPGC_PageInfo* old_info = GPGC_PageInfo::page_info(old_page);
heapRef new_ref;

  if ( GPGC_Layout::large_space_page(old_page) ) {
    new_ref = GPGC_PageRelocation::relocated_multi_ref(old_ref, old_info->ll_next());
  } else {
    GPGC_ObjectRelocation* forward = GPGC_PageRelocation::find_object(old_page, old_info, old_obj);

    new_ref = forward->mutator_relocate_object(old_info->relocate_space(), old_info->time(), old_info->just_gen(), old_ref);
  }

  assert0( new_ref.not_null() );
  assert0( GPGC_NMT::has_desired_nmt(new_ref) );

  return new_ref;
}


// This is a debugging function that is used to verify an address doesn't hold a ref to NewGen.
bool GPGC_Collector::no_card_mark(objectRef* addr) {
  objectRef ref = UNPOISON_OBJECTREF(*addr, addr);
  return ref.is_null_or_old();
}


bool GPGC_Collector::is_new_collector_thread(Thread* thread) {
if(thread->is_GC_task_thread()){
    return ((GCTaskThread*)thread)->thread_type() == GPGC_Collector::NewCollector;
  }
if(thread->is_GenPauselessGC_thread()){
    return GPGC_Thread::new_gc_thread() == thread;
  }
  return false;
}


bool GPGC_Collector::is_old_collector_thread(Thread* thread) {
if(thread->is_GC_task_thread()){
    return ((GCTaskThread*)thread)->thread_type() == GPGC_Collector::OldCollector;
  }
if(thread->is_GenPauselessGC_thread()){
    return GPGC_Thread::old_gc_thread() == thread;
  }
  return false;
}


bool GPGC_Collector::is_verify_collector_thread(Thread* thread) {
if(thread->is_GC_task_thread()){
    return ((GCTaskThread*)thread)->thread_type() == GPGC_Collector::Verifier;
  }
  return false;
}


void GPGC_Collector::run_task_queue(DetailTracer* dt, PGCTaskManager* manager, PGCTaskQueue* q) {
  manager->add_list(q);
  NOT_PRODUCT (
      if (TraceGCTaskManager) {
manager->log_perf_counters();
manager->reset_perf_counters();
      }
  )
}


void GPGC_Collector::run_task_queue(PGCTaskManager* manager, PGCTaskQueue* q) {
  manager->add_list(q);
  NOT_PRODUCT (
      if (TraceGCTaskManager) {
manager->log_perf_counters();
manager->reset_perf_counters();
      }
  )
}


// Resolve a JNI weak global handle.  This needs to work for threads that aren't
// in GC mode.
objectRef GPGC_Collector::JNI_weak_reference_resolve(objectRef* referent_addr) {
  objectRef referent = mutator_relocate_only(referent_addr);

if(referent.is_null()){
    return nullRef;
  }
  else if ( referent.is_new() ) {
    if ( GPGC_NewCollector::collection_state() != ConcurrentRefProcessing ) {
      return lvb_ref(referent_addr);
    }
  }
  else {
    assert0(referent.is_old());

    if ( GPGC_OldCollector::collection_state() != ConcurrentRefProcessing ) {
      return lvb_ref(referent_addr);
    }
  }
     
  // If the referent is StrongLive or FinalLive, the JNI resolve will return it.  Otherwise, nullRef.
  oop referent_oop = referent.as_oop();
  if ( GPGC_Marks::is_any_marked_strong_live(referent_oop) ||
       GPGC_Marks::is_any_marked_final_live(referent_oop) ) {
    return lvb_ref(referent_addr);
  }

  return nullRef;
}


// Resolve a java.lang.ref.SoftReference or java.lang.ref.WeakReference referent.  This
// needs to work for threads that aren't in GC mode.
objectRef GPGC_Collector::java_lang_ref_Reference_get(objectRef reference) {
  DEBUG_ONLY( Klass* ref_klass = reference.as_oop()->blueprint(); )
  DEBUG_ONLY( ReferenceType rt = ((instanceKlass*)ref_klass)->reference_type(); )
  assert0( ref_klass->oop_is_instanceRef() );
  assert0( rt==REF_SOFT || rt==REF_WEAK );
 
  objectRef* referent_addr = java_lang_ref_Reference::referent_addr(reference.as_oop());
objectRef referent;

  if ( reference.is_new() ) {
    if ( GPGC_NewCollector::collection_state() != ConcurrentRefProcessing ) {
      return lvb_ref(referent_addr);
    }
    referent = mutator_relocate_only(referent_addr);
    if ( ! referent.is_new() ) {
      return lvb_ref(referent_addr);
    }
  } else {
    assert0(reference.is_old());

    if ( GPGC_OldCollector::collection_state() != ConcurrentRefProcessing ) {
      return lvb_ref(referent_addr);
    }
    referent = mutator_relocate_only(referent_addr);
    if ( ! referent.is_old() ) {
      return lvb_ref(referent_addr);
    }
  }

  if ( GPGC_Marks::is_any_marked_strong_live(referent.as_oop()) ) {
    // If marked live, then it won't be cleared, so LVB it and return.            
    return lvb_ref(referent_addr);
  }

  // The referent isn't marked live, so it will be cleared by the
  // collector, and so NullRef should be returned to the application.
  return nullRef;
}


JRT_LEAF(objectRef, GPGC_Collector, java_lang_ref_Referent_get_slow_path, (objectRef reference))
{
  assert0( UseGenPauselessGC );
assert(Thread::current()->is_Java_thread(),"shouldn't call intrinsic functions from non-Java threads");

  DEBUG_ONLY( Klass* ref_klass = reference.as_oop()->blueprint(); )
  DEBUG_ONLY( ReferenceType rt = ((instanceKlass*)ref_klass)->reference_type(); )
  assert0( ref_klass->oop_is_instanceRef() );
  assert0( rt==REF_SOFT || rt==REF_WEAK );

  objectRef* referent_addr = java_lang_ref_Reference::referent_addr(reference.as_oop());
  objectRef  referent      = mutator_relocate_only(referent_addr);

  if ( reference.is_new() ) {
    assert( GPGC_NewCollector::collection_state() == ConcurrentRefProcessing, "should be in fast path code" );

    if ( ! referent.is_new() ) {
      return lvb_ref(referent_addr);
    }
  } else {
    assert0(reference.is_old());
    assert( GPGC_OldCollector::collection_state() == ConcurrentRefProcessing, "should be in fast path code" );

    if ( ! referent.is_old() ) {
      return lvb_ref(referent_addr);
    }
  }

  if ( GPGC_Marks::is_any_marked_strong_live(referent.as_oop()) ) {
    // If marked live in OldSpace, then it won't be cleared, so LVB it and return.            
    return lvb_ref(referent_addr);
  }

  // The referent isn't marked live in OldSpace, so it will be cleared by the
  // collector, and so NullRef should be returned to the application.
  return nullRef;
}
JRT_END


class GPGC_NMTClearClosure: public JavaThreadClosure {
  private:
    intptr_t                     _flushed_refs;
    GPGC_Collector::CollectorAge _age;
  public:
    GPGC_NMTClearClosure(GPGC_Collector::CollectorAge age) : _flushed_refs(0), _age(age) {}
    long flushed_refs() { return _flushed_refs; }
void clear_ref_buffer(Thread*thread){
      assert0(thread->is_Java_thread() || thread->is_VM_thread());
      bool           new_gen_ref = false;
      HeapRefBuffer* rb          = NULL;
      if (_age == GPGC_Collector::NewCollector) {
        new_gen_ref = true;
        rb          = thread->get_new_gen_ref_buffer(); 
      } else {
        new_gen_ref = false;
        rb          = thread->get_old_gen_ref_buffer();
      }
      intptr_t       refs = rb->get_top();
      if ( refs > 0 ) {
        // TODO: Make the sanity check portion only in non-product builds.
        //#ifdef ASSERT
        // Make sure all the refs we find point to already StrongLive objects.
        HeapRefBuffer::Entry* entries = rb->get_entries();
        for ( long i=0; i<refs; i++ ) {
          objectRef ref = entries[i].ref();
          if (new_gen_ref) {
            guarantee(ref.is_new(), "NewGen NMT ref buffer contains old objectRef.");
            guarantee(GPGC_Marks::is_any_marked_strong_live(ref.as_oop()), "NewGen objectRef at final clear not strong live.");
          } else  {
            guarantee(ref.is_old(), "OldGen NMT ref buffer contains non-old objectRef.");
            guarantee(GPGC_Marks::is_any_marked_strong_live(ref.as_oop()), "OldGen objectRef at final clear not strong live.");
          }
        }
        //#endif //ASSERT

        rb->set_top(0);
Atomic::add_ptr(refs,&_flushed_refs);
      }
    }
    void do_java_thread(JavaThread* jt) {
      clear_ref_buffer(jt);
    }
};


// Empty the mutator OldGen NMT ref buffers, and ensure any refs found point to
// objects already marked StrongLive.
void GPGC_Collector::final_clear_mutator_ref_buffers(TimeDetailTracer* tdt, CollectorAge age, const char* tag)
{
  bool                new_gen_ref             = false;
  HeapRefBufferList** mutator_ref_buffer_list = NULL;   
SafepointTimes times;

  if ( age == GPGC_Collector::NewCollector ) {
    new_gen_ref             = true;
    mutator_ref_buffer_list = GPGC_GCManagerNewStrong::full_mutator_ref_buffer_list();
  } else {
    new_gen_ref              = false;
    mutator_ref_buffer_list  = GPGC_GCManagerOldStrong::full_mutator_ref_buffer_list();
  }

  {
    DetailTracer dt(tdt, false, "%s final NMT clearing: ", tag);

    GPGC_NMTClearClosure nmtcc(age);
    GPGC_Safepoint::do_checkpoint(&nmtcc, &times);
if(GPGC_Safepoint::is_at_safepoint()){
      // In various debug modes, we might not be marking concurrently, and need to clear the VMThread's ref buffers here.
      // TODO: Why is this needed?  How does the VMThread have it's buffers cleared when we're doing concurrent marking?
      nmtcc.clear_ref_buffer(VMThread::vm_thread());
    }
    long refs = nmtcc.flushed_refs();

    HeapRefBuffer* mutator_stack;
    while ((mutator_stack =  GPGC_GCManagerMark::pop_heap_ref_buffer(mutator_ref_buffer_list))) {
      long size = mutator_stack->get_top();

      // Make sure all the refs we find point to already StrongLive objects.
      HeapRefBuffer::Entry* entries = mutator_stack->get_entries();
      for ( long i=0; i<size; i++ ) {
        objectRef ref = entries[i].ref();
        if (new_gen_ref) {
          guarantee(ref.is_new(), "NewGen NMT ref buffer contains old objectRef.");
        } else  {
          guarantee(ref.is_old(), "OldGen NMT ref buffer contains non-old objectRef.");
        }
        guarantee(GPGC_Marks::is_any_marked_strong_live(ref.as_oop()), "OldGen objectRef at final clear isn't strong live.");
      }

refs+=size;
      mutator_stack->set_top(0);
      GPGC_GCManagerMark::free_mutator_stack(mutator_stack);
    }

dt.print("%d %s refs",
             refs,
             age==GPGC_Collector::NewCollector ? "new" : "old");

    DEBUG_ONLY( 
        if (new_gen_ref) { 
           GPGC_GCManagerNewStrong::ensure_mutator_list_is_empty("40");
        } else  {
           GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("40");
        }
      )
  }

  log_checkpoint_times(tdt, &times, tag, "final_clear_refs");
}


void GPGC_Collector::setup_read_trap_array(TimeDetailTracer* tdt, CollectorAge age)
{
  assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));

  const char*     tag;
  PGCTaskManager* manager;
  uint64_t        tasks;
  
  if ( age == NewCollector ) {
    tag     = "N";
    manager = GPGC_Heap::new_gc_task_manager();
    tasks   = GenPauselessNewThreads;
  } else {
    tag     = "O";
    manager = GPGC_Heap::old_gc_task_manager();
    tasks   = GenPauselessOldThreads;
  }

  {
    // Set the trap flags in the non-current ReadBarrierTrap array, so we can flip it into place during the safepoint.
    DetailTracer dt(tdt, false, "%s: Setup duplicate read barrier trap array.", tag);
    GPGC_Space::setup_read_trap_array(manager, tasks);
  }

  if ( BatchedMemoryOps ) {
    {
      DetailTracer dt(tdt, false, "%s: start batched memory operation", tag);
      {
        CycleCounter cc(ProfileMemoryTime, &CycleCounts::mbatch_start);
os::start_memory_batch();
      }
    }

    // Do the batched remap operations for swapping the read barrier arrays:
    GPGC_ReadTrapArray::batched_array_swap(tdt, age);
  }

  {
    GPGC_TLB::tlb_resync(tdt, tag);
  }
}


void GPGC_Collector::commit_batched_memory_ops(TimeDetailTracer* tdt, CollectorAge age)
{
  assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));

  const char* tag = (age==NewCollector) ? "N:" : "O:";

  {
    DetailTracer dt(tdt, false, "%s Commit batched memory ops", tag);
    assert0(BatchedMemoryOps);
    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::mbatch_commit);
os::commit_memory_batch();
    }
  }
}
