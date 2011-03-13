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


#include "artaObjects.hpp"
#include "auditTrail.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "cycleCounts.hpp"
#include "gpgc_closures.hpp"
#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldReloc.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_rendezvous.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_slt.hpp"
#include "gpgc_tasks.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_threadCleaner.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_traps.hpp"
#include "log.hpp"
#include "jvmtiExport.hpp"
#include "referencePolicy.hpp"
#include "resourceArea.hpp"
#include "symbolTable.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

long                     GPGC_OldCollector::_collection_state               = GPGC_Collector::NotCollecting;
long                     GPGC_OldCollector::_mutator_ref_sanity_check       = GPGC_Collector::NoMutatorRefs;
long                     GPGC_OldCollector::_should_mark_new_objs           = false;
long                     GPGC_OldCollector::_current_old_cycle              = 0;
long                     GPGC_OldCollector::_current_total_cycle            = 0;
GPGC_JavaLangRefHandler* GPGC_OldCollector::_jlr_handler                    = NULL;
GPGC_CycleStats*         GPGC_OldCollector::_cycle_stats                    = NULL;
GPGC_Population*         GPGC_OldCollector::_page_pops                      = NULL;
GPGC_RelocationSpike     GPGC_OldCollector::_relocation_spike;

long                     GPGC_OldCollector::_last_target_pages              = 0;

uint64_t                 GPGC_OldCollector::_fragment_page_stripe           = 0;
uint64_t                 GPGC_OldCollector::_fragment_page_stripe_mask      = 0xF;
jlong GPGC_OldCollector::_time_of_last_gc=0;

AuditTrail*              GPGC_OldCollector::_audit_trail                    = NULL;

bool                     GPGC_OldCollector::_start_global_safepoint         = false;
bool                     GPGC_OldCollector::_start_1st_mark_remap_safepoint = true;
bool                     GPGC_OldCollector::_end_1st_mark_remap_safepoint   = true;
bool                     GPGC_OldCollector::_start_2nd_mark_remap_safepoint = true;
bool                     GPGC_OldCollector::_end_2nd_mark_remap_safepoint   = true;
bool                     GPGC_OldCollector::_start_3rd_mark_remap_safepoint = true;
bool                     GPGC_OldCollector::_end_3rd_mark_remap_safepoint   = true;
bool                     GPGC_OldCollector::_start_mark_verify_safepoint    = true;
bool                     GPGC_OldCollector::_end_mark_verify_safepoint      = true;
bool                     GPGC_OldCollector::_end_mark_phase_safepoint       = false;
bool                     GPGC_OldCollector::_start_relocation_safepoint     = true;
bool                     GPGC_OldCollector::_end_relocation_safepoint       = true;
bool                     GPGC_OldCollector::_start_relocation_safepoint2    = true;
bool                     GPGC_OldCollector::_end_relocation_safepoint2      = true;
bool                     GPGC_OldCollector::_end_global_safepoint           = false;


void GPGC_OldCollector::initialize()
{
  guarantee(_page_pops==NULL, "Shouldn't be initializing GPGC_OldCollector twice.")

  _jlr_handler = new GPGC_JavaLangRefHandler(GPGC_PageInfo::OldGen, objectRef::old_space_id);
  _page_pops   = new GPGC_Population();

  // TODO: expand the page counts to include get_fund_stats() results for max possible from grant and pause_prevention
  uint32_t maximum_heap_pages    = GPGC_PageBudget::committed_budget();
  uint32_t small_space_max_pages = maximum_heap_pages;
  uint32_t mid_space_max_pages   = maximum_heap_pages >> (LogPagesPerMidSpaceBlock - 2);
  uint32_t large_space_max_pages = maximum_heap_pages >> (LogPagesLargeSpaceMinObjectSize - 1);

  page_pops()->initialize(small_space_max_pages, mid_space_max_pages, large_space_max_pages);

  reset_concurrency();

#ifndef AT_PRODUCT
  if ( GPGCAuditTrail ) {
    _audit_trail = new AuditTrail(GPGCAuditTrailSize);
  }
#endif
}


class GPGC_OldGC_VerifyMarkedLiveClosure:public OopClosure{
  public:
    bool check_derived_oops()      { return false; }
    void do_oop(objectRef* p)      { objectRef r = UNPOISON_OBJECTREF(*p, p); 
                                     if (!r.is_old()) { return; }
                                     assert0(GPGC_NMT::has_desired_old_nmt(r) && !GPGC_ReadTrapArray::is_remap_trapped(r)); }
};


// Execute a OldGen & PermGen GC cycle of the generational pauseless collector.  This will
// mark all live old space objects, and relocate the most sparse pages.
void GPGC_OldCollector::collect(const char* label, bool clear_all, GPGC_CycleStats* stats)
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  if (_start_global_safepoint) {
Unimplemented();//TODO-LATER: global safepoints not currently implemented.
    // jlr_handler()->acquire_pending_list_lock();
    // GPGC_Safepoint::begin();
    //
    // if (GC_locker::is_active()) {
    //   if (PrintGCDetails) gclog_or_tty->print_cr("aborting GC because GC_locker is active");
    //   GPGC_Safepoint::end();
    //   return;
    // }
  }

  _cycle_stats = stats;

  if (ProfileMemoryTime) {
    CycleCounts::print_all();
  }

  ResourceMark rm;
  HandleMark   hm;

  assert( !DerivedPointerTable::is_active(), "DerivedPointerTable shouldn't be active for Pauseless GC" );
  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("1"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("1"); )
  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("1"); )

  if ( clear_all ) {
    jlr_handler()->pre_mark(new AlwaysClearPolicy());
  } else {
    jlr_handler()->pre_mark(new LRUMaxHeapPolicy());
  }

  GPGC_GCManagerOldStrong::pre_mark();

  // Phase 1: Mark and Remap references

#ifdef ASSERT
  bool is_heap_iterate_collection = GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection();
  if (is_heap_iterate_collection) { guarantee(!_end_1st_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT

  if (!_end_1st_mark_remap_safepoint) {
    // The java.lang.ref pending list lock has be be acquired outside of a safepoint.  If we're
    // not going to do a concurrent mark/remap, then we need to grab the lock up front.
    jlr_handler()->acquire_pending_list_lock();
  }

  mark_remap_phase(label);

  cycle_stats()->start_relocate();

  // Phase 2: Relocate
  relocation_phase(label);

  GPGC_GCManagerOldStrong::post_relocate();

//  if (_end_global_safepoint) {
//    GPGC_Safepoint::end();
//  }

#ifdef ASSERT
  if (is_heap_iterate_collection) { guarantee(!_end_2nd_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT

  if (!_end_2nd_mark_remap_safepoint) {
    jlr_handler()->release_pending_list_lock();
  }

  assert0(!GPGC_Safepoint::is_at_safepoint());

  GPGC_PageBudget::return_pause_pages();

  reset_millis_since_last_gc();

_cycle_stats=NULL;

  set_collection_state(NotCollecting);

  AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_END_OLD_GC_CYCLE);
}


// This method orchestrates the first phase of an OldGen GC: marking all live objects,
// and remapping references to objects that were relocated in the previous GC cycle.
void GPGC_OldCollector::mark_remap_phase(const char* label)
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  //***
  //***  Phase 1: Mark and Remap references
  //***

  marking_init();

  //*** Initial safepoint of mark/remap phase.
  mark_remap_safepoint1(heap, label);

  //*** Concurrent section of mark/remap phase.
  mark_remap_concurrent(heap, label);

  //*** Phase change from concurrent marking to concurrent ref processing
  mark_remap_safepoint2(heap, label);

  //*** Concurrent ref processing
  mark_remap_concurrent_ref_process(heap, label);

  //*** Final safepoint of mark/remap phase.
  mark_remap_safepoint3(heap, label);

  //*** Concurrent mark/remap of weak roots
  mark_remap_concurrent_weak_roots(heap, label);

  //***  Object Mark Verification Safepoint
  mark_remap_verify_mark(heap, label);

  //*** pre mark/remap cleanup
  pre_mark_remap_cleanup(heap, label);

  //*** Final mark/remap cleanup
  mark_remap_cleanup(heap, label);
}


// This method orchestrates the second phase of an OldGen GC: identifying empty pages
// to free and sparse pages to relocate, and then relocating the live objects out
// of the sparse pages.
void GPGC_OldCollector::relocation_phase(const char* label)
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  //***
  //***  Phase 2: Relocate
  //***

  size_t total_free_words;

  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("2"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("2"); )
  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("2"); )

  if ( GPGCNoRelocation ) {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: GPGCNoRelocation",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    page_pops()->reset_populations();
    page_pops()->reset_cursor();
relocation_spike()->reset();
    GPGC_Space::old_gc_stop_marking();
    GPGC_Space::old_gc_clear_no_relocate();
    GPGC_Space::old_gc_clear_page_marks();
    return;
  }

  //*** Concurrent relocation setup
  relocation_setup(heap, label);

  //*** Initial safepoint of relocation phase for PermGen.
  relocation_safepoint_perm(heap, label);

  //*** Concurrent finish of PermGen relocation.
  relocation_concurrent_perm(heap, label);

  if ( (page_pops()->small_space_in_old()->max_cursor() + page_pops()->mid_space_in_old()->max_cursor()) > 0 )
  {
    //*** Concurrent setup for relocating OldGen.
    relocation_setup_old(heap, label);

    //*** Second safepoint of relocation phase for OldGen.
    relocation_safepoint_old(heap, label);

    //*** Concurrent finish of OldGen phase.
    relocation_concurrent_old(heap, label);
  }

  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: Concurrent clear page marks",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();
    
    {
      GPGC_Interlock interlock(&tdt, GPGC_Interlock::OldGC_MarkClearing, "O:");

      {
        DetailTracer dt(&tdt, false, "O: Clear page marks");

PGCTaskQueue*q=PGCTaskQueue::create();

        long sections = (GenPauselessOldThreads<2) ? 1 : (GenPauselessOldThreads-1);

q->enqueue(new GPGC_OldGC_ClearMultiSpaceMarksTask());

        for ( long section=0; section<sections; section++ ) {
          q->enqueue( new GPGC_OldGC_ClearOneSpaceMarksTask(section, sections) );
        }

        // Wait for the tasks to be completed.
        GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
      }
    }
   
    {
      DetailTracer dt(&tdt, false, "O: Clear no-relocate flags");
      GPGC_Space::old_gc_clear_no_relocate();
    }

    if ( _end_global_safepoint) {
      GPGC_Rendezvous::end_coordinated_safepoint_prepare();
      GPGC_Rendezvous::end_coordinated_safepoint();
    }
  }

  GPGC_PageBudget::old_gc_verify_preallocated_pages();

  Universe::update_heap_info_at_gc();

  long collection_delta = (page_pops()->total_released() +
                           page_pops()->source_pages_to_old_gen() -
                           page_pops()->target_pages_in_old_gen()) << LogWordsPerGPGCPage;

  cycle_stats()->heap_at_end_relocate(collection_delta,
                                      relocation_spike()->peak_value(),
                                      page_pops()->no_relocate_pages(),
                                      0, /* pages are not promoted by the OldCollector */
                                      page_pops()->target_pages_in_old_gen());

  set_collection_state(CycleCleanup);
}


// Initial safepoint of mark/remap phase.
void GPGC_OldCollector::mark_remap_safepoint1(GPGC_Heap* heap, const char* label)
{
  {
    _current_total_cycle = heap->actual_collections() + 1;
    _current_old_cycle   = heap->actual_old_collections() + 1;

    AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_START_OLD_GC_CYCLE,
                         _current_total_cycle, _current_old_cycle);

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    // +1 is added to the count since we don't update it until we are in the safepoint 
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%da:%s pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_old_cycle,
                         (PrintGCDetails?" initial mark":""),
                         heap->used()/M, heap->capacity()/M);
m.enter();

    {

      // Sync up with the NewCollector before starting to mark roots: 
      {
        DetailTracer dt(&tdt, false, "O: Synchronize with NewGC: start root marking");
        _mutator_ref_sanity_check = GPGC_Collector::NoSanityCheck;
        GPGC_Rendezvous::start_root_marking();
        // By this time, NewGC has toggled the OldSpace NMT state on behalf of the OldGC.
      }

      {
        // At this point, we know we're inside the NewGC's mark_remap_safepoint1.

        // Since OldGC coordinates the first safepoint with the NewGC we
        // record the safepoint start from the new GC's pause1.  We end the
        // safepoint in the new collector when the safepoint actually ends.
        // Note that we cannot record the old GC's pause1 in the new collector
        // without jumping a couple of hoops because the old collector will
        // reset when it actually starts the old collection.  But because the
        // start_root_marking is synchronized with the New GC, this value
        // could not have been overwritten by a subsequent new GC.
        long pause_start_ticks = GPGC_Heap::heap()->gc_stats()->new_gc_cycle_stats()->pause1_start_ticks();
        cycle_stats()->start_pause1(pause_start_ticks);

        set_collection_state(InitialMarkSafepoint);
      }

      {
        DetailTracer dt(&tdt, false, "O: Prepare OldGen for marking");

        // increment the count inside the safepoint so we can use it to sync the vm op
        // allocation result with reality once it is given back to the caller thread - if the 
        // caller thread is switched out for a long time, another complete gc can pass him by,
        // so he needs to discard the vm op allocation, loop around and allocate again.
heap->increment_actual_collections();
heap->increment_actual_old_collections();

        // There should be no objects marked live at this point.  In DEBUG, NewGC will verify that.

        // Now mark all new objects as live
        set_mark_new_objects_live(true); 

        // This needs to be inside the safepoint to ensure we don't allocate from a page without
        // counting page populations, but not marking the page as having invalid page pops.
        GPGC_Space::old_gc_set_marking_underway();
      }

      {
        DetailTracer dt(&tdt, false, "O: Flush OldGen relocation: ");
        GPGC_Space::old_gc_flush_relocation(&dt);
      }

      if ( GPGCVerifyCapacity ) {
        DetailTracer dt(&tdt, false, "O: Verify OldGen and PermGen page capacity");
        GPGC_Space::verify_old_gen_capacity();
        GPGC_Space::verify_perm_gen_capacity();
      }

      {
        DetailTracer dt(&tdt, false, "O: Enable java lang ref capture");
        // TODO: maw: figure out JLR handling: Can there be refs in roots that NewGC sees?
        assert0(!jlr_handler()->is_capture_on());
        assert0(jlr_handler()->none_captured());
        jlr_handler()->set_jlr_capture(true);
        jlr_handler()->mark_roots(GPGC_GCManagerOldStrong::get_manager(0));
        GPGC_GCManagerOldStrong::reset_reference_lists();
      }
    }

    // The collection state needs to be changed before we end the safepoint, to avoid a race.
    set_collection_state(ConcurrentMarking);

    // if (_end_1st_mark_remap_safepoint ) GPGC_Safepoint::end(&tdt);
    {
      DetailTracer dt(&tdt, false, "O: Synchronize with NewGC: end marking safepoint");
      GPGC_Rendezvous::end_marking_safepoint();
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC initial mark tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent section of mark/remap phase.
void GPGC_OldCollector::mark_remap_concurrent(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent mark",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    // Concurrently mark/remap objects in all generations:
    parallel_concurrent_mark(&tdt);

    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "O:");
    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewGC_CardMarkScan, "O:");

    {
      DetailTracer dt(&tdt, false, "O: Acquiring OldGC_ConcurrentRefProcessing_lock");

      // Grab this lock to prevent JVMTI from looking up fields directly
      // without going through proper accessor functions during ConcurrentRefProcessing.
      OldGC_ConcurrentRefProcessing_lock.lock_can_block_gc(Thread::current());
      GET_RPC;
      OldGC_ConcurrentRefProcessing_lock._rpc = RPC;
    }

  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC concurrent mark tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Phase change safepoint from concurrent marking to concurrent ref processing.
void GPGC_OldCollector::mark_remap_safepoint2(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%db: ref processing pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_old_cycle,
                         heap->used()/M, heap->capacity()/M);
m.enter();

    if (_start_2nd_mark_remap_safepoint) {
      GPGC_Safepoint::begin(&tdt);
      cycle_stats()->start_pause2(tdt.pause_start_ticks());
    } else {
      cycle_stats()->start_pause2(0);
    }

    set_collection_state(WeakRefSafepoint);

    parallel_flush_and_mark(&tdt);

    // Update the collection state before ending the safepoint, to avoid a race.
    set_collection_state(ConcurrentRefProcessing);

    if (_end_2nd_mark_remap_safepoint) {
      GPGC_Safepoint::end(&tdt);
      cycle_stats()->end_pause2(tdt.pause_ticks());
    } else {
      cycle_stats()->end_pause2(0);
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC weak ref safepoint tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent ref processing section of mark/remap phase.
void GPGC_OldCollector::mark_remap_concurrent_ref_process(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent ref processing",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    // Parallel concurrent handling of soft/weak/final/phantom/jni refs.
    parallel_concurrent_ref_process(&tdt);

OldGC_ConcurrentRefProcessing_lock.unlock();
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC concurrent ref processing tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Final safepoint of mark/remap phase.
void GPGC_OldCollector::mark_remap_safepoint3(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%db: final mark pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_old_cycle,
                         heap->used()/M, heap->capacity()/M);
m.enter();

    if (_start_3rd_mark_remap_safepoint) {
      GPGC_Safepoint::begin(&tdt);
      cycle_stats()->start_pause3(tdt.pause_start_ticks());
    } else {
      cycle_stats()->start_pause3(0);
    }

    set_collection_state(FinalMarkSafepoint);

    {
      DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("3"); )
      DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("3"); )

      // TODO: there shouldn't be, but there are, since NMT traps don't filter out marked refs anymore
      // // There shouldn't be any OldGen or PermGen mutator refs.
      // long new_gen_refs, old_gen_refs;
      // DEBUG_ONLY( GPGC_Collector::flush_mutator_ref_buffers(&tdt, "O: FMSP", new_gen_refs, old_gen_refs); )
      // assert0(old_gen_refs == 0);

      // Final mark/remap mop up:
      parallel_final_mark(&tdt);

      DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("4"); )
      DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("4"); )
    }

    // Update the collection state before ending the safepoint, to avoid a race.
    set_collection_state(ConcurrentWeakMarking);

    if (_end_3rd_mark_remap_safepoint) {
      GPGC_Safepoint::end(&tdt);
      cycle_stats()->end_pause3(tdt.pause_ticks());
    } else {
      cycle_stats()->end_pause3(0);
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC final mark tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent mark/remap of weak roots
void GPGC_OldCollector::mark_remap_concurrent_weak_roots(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: Concurrent mark weak roots",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    parallel_concurrent_weak_marks(&tdt);

    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewGC_CardMarkScan, "O:");
    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "O:");

    // After weak roots have been marked, we need a checkpoint, to ensure that there are
    // no JavaThreads in the midst of some kind of operation on weak roots that may retain
    // a reference to a page that's going away.  After the checkpoint, we can safely free
    // pages that may have contained weak roots, and C-Heap data structures associated with
    // deleted weak roots.

    // Run a checkpoint to clear out the JavaThread NMT buffers.  Because the NMT trap
    // enqueues refs with the wrong NMT that were already marked live by the collector,
    // it's easy to find buffers with refs.  But all of the refs should already be marked
    // StrongLive.
    final_clear_mutator_ref_buffers(&tdt, OldCollector, "O: CMWR");

    // No more mutator ref sanity checking needed.
    _mutator_ref_sanity_check = GPGC_Collector::NoMutatorRefs;

    // Disable NMT & TLB trapping
    disable_NMT_traps(&tdt);

    {
      DetailTracer dt(&tdt, false, "O: Release pending free table entries");

      // C-Heap SymbolTable & StringTable structures freed during weak root collection can now
      // be released for reuse.  By running a checkpoint, we've ensured that no JavaThreads
      // may be halfway through a SymbolTable and StringTable op and holding on to an old entry.
SymbolTable::GC_release_pending_free();
StringTable::GC_release_pending_free();
    }

    tdt.end_log(" "  SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC concurrent mark weak roots tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }


  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("5"); )
}


// Final mark/remap cleanup
void GPGC_OldCollector::pre_mark_remap_cleanup(GPGC_Heap* heap, const char* label)
{
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
  TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: pre mark cleanup",
                       label, _current_total_cycle, _current_old_cycle);
m.enter();

  if (GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection()) {
    guarantee(GPGCNoRelocation,          "jvmti heap iterate collections can't be concurrent");
    guarantee(_end_mark_phase_safepoint, "Heap iterate safepoint assumptions didn't hold");
  }
  if (GPGCNoRelocation) {
    // guarantee(_end_mark_phase_safepoint, "GPGCNoRelocation only supported with PGCSafepointMark");

    // I think if we're running concurrent mark + GPGCNoRelocation, it's OK to
    // isable new object live marking concurrently:
    set_mark_new_objects_live(false);
  }

  if (_end_mark_phase_safepoint) {
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("6"); )

    // The prepare function will ensure that both collectors are ready to end the safepoint.
    // The new collector then ends the safepoint.
    // The old collector has to wait until the new collector has ended the safepoint
    GPGC_Rendezvous::end_coordinated_safepoint_prepare();
    GPGC_Rendezvous::end_coordinated_safepoint();
  }
}


// Final mark/remap cleanup
void GPGC_OldCollector::mark_remap_cleanup(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: final mark cleanup",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    set_collection_state(MarkingCleanup);

    marking_cleanup();

    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_mutator_list_is_empty("7"); )

    {
      DetailTracer dt(&tdt, false, "O: ");
      long released_pages = GPGC_Space::old_gc_release_relocated_pages();
      dt.print("Release %lld relocated pages (%lld PagePops)", released_pages, page_pops()->total_pops_to_relocate());
      assert(released_pages == (long)page_pops()->total_pops_to_relocate(), "Released page count mismatch");
      GPGC_PageRelocation::old_gc_reset_relocation_space();
    }

    {
      DetailTracer dt(&tdt, false, "O: Free ref buffers: " );
      long mutator_free = GPGC_GCManagerMark::count_free_mutator_ref_buffers();
      long strong_free  = GPGC_GCManagerOldStrong::count_free_ref_buffers();
      long final_free   = GPGC_GCManagerOldFinal::count_free_ref_buffers();
      dt.print("%lld mutator, %lld strong, %lld final", mutator_free, strong_free, final_free);
    }

    if ( ProfileLiveObjects ) {
      DetailTracer dt(&tdt, false, "O: Sum per thread object marking stats");

      LiveObjects *objs = LiveObjects::sum_old_gc_marked(GPGC_Heap::old_gc_task_manager());

#ifdef ASSERT
      uint64_t profile_count = 0, profile_words = 0;
      objs->total(&profile_count, &profile_words);

      uint64_t page_info_obj_count = 0, page_info_word_count = 0;
      GPGC_Space::old_gc_sum_raw_stats(&page_info_obj_count, &page_info_word_count);

     // TODO enable this assert
     // assert((profile_count == page_info_count) && (profile_words == page_info_words),
     //        "GPGC OldGC live object profile verification failed");
#endif
    }

    tdt.end_log(""   SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }
}


// Concurrent relocation setup
void GPGC_OldCollector::relocation_setup(GPGC_Heap* heap, const char* label)
{
  {
    cycle_stats()->heap_at_page_sort(heap->used_in_words());

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent relocation setup",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    set_collection_state(RelocationSetup);

    // During relocation, we do not want to be taking GCLdValueTr traps.
Thread*current=Thread::current();
    current->set_gc_mode(true);
    // TODO make it so getting a trap is now an error, we used to set the vector to invalid_trap_table here.
    GPGC_Heap::old_gc_task_manager()->request_new_gc_mode(true);

    {
      // The population collector immediately deallocates empty pages, which causes a race
      // if NewGC is looking for pages to card-mark scan.  Which is why we need this interlock.
      GPGC_Interlock interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "O:");

      // Sort pages by population stats
      {
        DetailTracer dt(&tdt, false, "O: Sparse pages: ");

        // NOTE: LVB traps must still be enabled here! Otherwise mutators might not be marking their
        // objects as live!
        // TODO: maw: Need to have a way to assert this!

        page_pops()->reset_populations();
relocation_spike()->reset();
        GPGC_Space::old_gc_collect_sparse_populations(fragment_page_stripe_mask(), fragment_page_stripe(), page_pops());

        _fragment_page_stripe = (_fragment_page_stripe + 1) & _fragment_page_stripe_mask;

        dt.print("Found garbage "INT64_FORMAT" MB empty, "UINT64_FORMAT" MB sparse, "UINT64_FORMAT" MB selected; "
                 "Found fragged "UINT64_FORMAT" MB, "UINT64_FORMAT" MB selected",
                  (page_pops()->total_released()      << LogBytesPerGPGCPage)/M,
                  (page_pops()->dead_words_found()    << LogBytesPerWord)/M,
                  (page_pops()->dead_words_selected() << LogBytesPerWord)/M,
                  (page_pops()->frag_words_found()    << LogBytesPerWord)/M,
                  (page_pops()->frag_words_selected() << LogBytesPerWord)/M);
      }
    }

    // Select pages to relocate
    // Initialize sideband forwarding arrays.
    prepare_sideband_forwarding(&tdt);

    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "O:");
    
    // Always take the BatchedMemoryOps interlock, so the two collectors don't fight over who
    // is updating the read barrier trap array.
    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");

    update_page_relocation_states        (&tdt, GPGC_PageInfo::PermGen);
    GPGC_Collector::setup_read_trap_array(&tdt, GPGC_Collector::OldCollector);

    if ( BatchedMemoryOps ) {
      // With azmem batched memory operations, we issue all the relocation virtual memory calls
      // in a batch prior to the safepoint, and then commit them inside the relocation safepoint.
      remap_perm_gen_memory(&tdt);
    }

    { 
      DetailTracer dt(&tdt, false, "O: Start GC_locker concurrent_gc");
      // Make sure the GC_locker concurrent_gc lock is held while we relocate.  Do it outside a safepoint,
      // so the safepoint time doesn't include waiting for a JNI thread to complete a critical section.
      GPGC_Rendezvous::start_gc_locker_concurrent();
    }

    cycle_stats()->heap_after_reloc_setup(page_pops()->total_garbage_words(),
                                          page_pops()->garbage_words_to_collect(),
                                          page_pops()->sideband_limited_words());

    tdt.end_log(""   SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                cycle_stats()->get_page_sort_words_in_use() / (M>>LogBytesPerWord),
                heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC concurrent relocation setup tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Initial safepoint of relocation phase for PermGen setup.
void GPGC_OldCollector::relocation_safepoint_perm(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%dc: perm relocation pause",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    if (_start_relocation_safepoint) {
      GPGC_Safepoint::begin(&tdt, true);
      cycle_stats()->start_pause4(tdt.pause_start_ticks());
    } else {
      cycle_stats()->start_pause4(0);
    }

    set_collection_state(RelocationSafepoint);

    long prev_used_words = heap->used_in_words();

    {
      // Must not do this before now, newly allocated objects must be marked live.
      set_mark_new_objects_live(false);
  
      if ( BatchedMemoryOps ) {
        // Commit batched memory ops inside a safepoint.
        GPGC_Collector::commit_batched_memory_ops(&tdt, GPGC_Collector::OldCollector);
      } else {
        // If we don't have batched memory ops, then we do relocation page remaps inside the safepoint:
        remap_perm_gen_memory(&tdt);

        // Swap in the new read barrier trap array to enable new trap state:
        GPGC_ReadTrapArray::swap_readbarrier_arrays(&tdt, GPGC_Collector::OldCollector);
      }

      // Tell threads to self-clean their stacks
      GPGC_ThreadCleaner::enable_thread_self_cleaning(&tdt, JavaThread::gpgc_clean_old);
    }

    // Update the collection state before ending the safepoint, to avoid a race:
    set_collection_state(ConcurrentRelocation);

    if (_end_relocation_safepoint) {
      GPGC_Safepoint::end(&tdt);
      cycle_stats()->end_pause4(tdt.pause_ticks());
    } else {
      cycle_stats()->end_pause4(0);
    }

    long frag_words = page_pops()->frag_words_found();
    long live_words = page_pops()->live_words_found() + (page_pops()->no_relocate_pages() << LogWordsPerGPGCPage);
    long live_bytes = live_words << LogBytesPerWord;

    cycle_stats()->heap_at_pause4(prev_used_words, live_words, frag_words);

    if ( tdt.verbose() ) {
      tdt.end_log("%s"
                  "used " SIZE_FORMAT "M, garbage " SIZE_FORMAT "M, frag " SIZE_FORMAT "M,"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                  tdt.details()?"":" ",
                  heap->used()/M,
                  (page_pops()->dead_words_found()<<LogBytesPerWord)/M,
                  (page_pops()->frag_words_found()<<LogBytesPerWord)/M,
                  live_bytes/M, heap->capacity()/M);
    } else {
      tdt.end_log("%s" SIZE_FORMAT "M"
"("SIZE_FORMAT"M)",
                  tdt.details()?"":" ",
                  live_bytes/M, heap->capacity()/M);
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC initial perm relocate tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent relocation phase for PermGen.
void GPGC_OldCollector::relocation_concurrent_perm(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: perm concurrent relocation",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    if ( BatchedMemoryOps ) {
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");
    }

    {
      DetailTracer dt(&tdt, false, "O: End GC_locker concurrent_gc");
      // Release the GC_locker concurrent_gc lock, so JNI threads can get into critical sections again.
      GPGC_Rendezvous::end_gc_locker_concurrent();
    }

    GPGC_Space::old_gc_stop_marking();

    {
      DetailTracer dt(&tdt, false, "O: Complete %ld mid space pages", page_pops()->mid_space_in_perm()->max_cursor());

      // Release the remaining memory at the source address of each relocated mid space page.
      GPGC_Space::remapped_pages(page_pops()->mid_space_in_perm());
    }

    if ( PageHealing ) {
      if ( GPGCDiagHealDelayMillis>0 ) {
        DetailTracer dt(&tdt, false, "O: Diagnostic delay before mid space page healing");
os::sleep(Thread::current(),GPGCDiagHealDelayMillis,false);
      }

      {
        DetailTracer dt(&tdt, false, "O: Unshatter %ld mid space target pages", page_pops()->mid_space_targets()->current_length());

        // Form parallel tasks to go through each mid space page and force healing.
PGCTaskQueue*q=PGCTaskQueue::create();

        for ( int64_t i=0; i<int64_t(GenPauselessOldThreads); i++ ) {
q->enqueue(new GPGC_OldGC_HealMidPagesTask(i));
        }

        GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
      }
    }

    {
      DetailTracer dt(&tdt, false, "O: Card mark %lu relocated mid space pages in PermGen");

      long work_unit = 10;
      long tasks     = GenPauselessOldThreads;

      assert0(page_pops()->mid_space_in_perm()->cursor() == 0);

PGCTaskQueue*q=PGCTaskQueue::create();

      for ( long i=0; i<tasks; i++ ) {
        q->enqueue(new GPGC_OldGC_CardMarkPagesTask(page_pops()->mid_space_in_perm(), work_unit));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

      page_pops()->mid_space_in_perm()->reset_cursor();
    }

    {
      DetailTracer dt(&tdt, false, "O: Relocate perm ");

      assert0(page_pops()->small_space_in_perm()->cursor() == 0);
      assert0(page_pops()->mid_space_in_perm()->cursor() == 0);

      long work_unit = (GenPauselessOldThreads / 64) + 1;
      long tasks = GenPauselessOldThreads;

      GPGC_GCManagerOldReloc::set_generations(GPGC_PageInfo::PermGen);

      GPGC_PopulationArray* small_space_in_perm = page_pops()->small_space_in_perm();
      GPGC_PopulationArray* mid_space_in_perm   = page_pops()->mid_space_in_perm();

PGCTaskQueue*q=PGCTaskQueue::create();

      for ( long i=0; i<tasks; i++ ) {
        q->enqueue(new GPGC_OldGC_RelocateSmallPagesTask(work_unit, small_space_in_perm));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

      long target_perm_pages = GPGC_GCManagerOldReloc::total_page_count();
      page_pops()->atomic_increment_target_pages_in_old_gen(target_perm_pages);

      long perm_gen_mutator_pages = GPGC_Space::perm_gen_relocation_page_count();
      page_pops()->atomic_increment_target_pages_in_old_gen(perm_gen_mutator_pages);

      dt.print("pages: %ld -> %ld (%ld)", small_space_in_perm->max_cursor() + mid_space_in_perm->max_cursor(),
                                          target_perm_pages, perm_gen_mutator_pages);
    }

    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "O:");

if(PageHealing&&PrintGCDetails){
SafepointTimes times;

      {
        DetailTracer dt(&tdt, false, "O: Mutator unshattered page traps: ");

        GPGC_UnshatteredPageTrapCountClosure uptcc;
        GPGC_Safepoint::do_checkpoint(&uptcc, &times);

dt.print(" %ld threads, %ld total traps, %ld max single thread traps",
                 uptcc.get_threads(),
                 uptcc.get_total_traps(),
                 uptcc.get_max_single_thread_traps());
      }

      log_checkpoint_times(&tdt, &times, "O: CRP", "page_traps");
    }

    tdt.end_log(     SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC perm concurrent relocation tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent relocation setup phase for OldGen.
void GPGC_OldCollector::relocation_setup_old(GPGC_Heap* heap, const char* label)
{
  assert0( (page_pops()->small_space_in_old()->max_cursor() + page_pops()->mid_space_in_old()->max_cursor()) > 0 );

  {
    size_t prev_used = heap->used();

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: old concurrent relocation setup",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    set_collection_state(RelocationSetup2);

    {
      DetailTracer dt(&tdt, false, "O: Setup old relocation buffers");
      GPGC_GCManagerOldReloc::set_generations(GPGC_PageInfo::OldGen);
    }

    // Make sure NewGC has had a chance to run.
os::yield_all();

    // Always take the BatchedMemoryOps interlock, so the two collectors don't fight over who
    // is updating the read barrier trap array.
    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");

    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::OldGC_RelocateOld, "O:");

    {
      DetailTracer dt(&tdt, false, "O: Rendezvous start OldGen relocation");
      GPGC_Rendezvous::start_relocating_threads(GenPauselessOldThreads);
    }

    update_page_relocation_states        (&tdt, GPGC_PageInfo::OldGen);
    GPGC_Collector::setup_read_trap_array(&tdt, GPGC_Collector::OldCollector);

    if ( BatchedMemoryOps ) {
      // With azmem batched memory operations, we issue all the relocation virtual memory calls
      // in a batch prior to the safepoint, and then commit them inside the relocation safepoint.
      remap_old_gen_memory(&tdt);
    }

    { 
      DetailTracer dt(&tdt, false, "O: Start GC_locker concurrent_gc");
      // Make sure the GC_locker concurrent_gc lock is held while we relocate.  Do it outside a safepoint,
      // so the safepoint time doesn't include waiting for a JNI thread to complete a critical section.
      GPGC_Rendezvous::start_gc_locker_concurrent();
    }

    tdt.end_log(     SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC old concurrent relocation setup tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Second safepoint of relocation phase for OldGen setup.
void GPGC_OldCollector::relocation_safepoint_old(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%dc: old relocation pause",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    if (_start_relocation_safepoint2) {
      GPGC_Safepoint::begin(&tdt, true);
      cycle_stats()->start_pause4(tdt.pause_start_ticks());
    } else {
      cycle_stats()->start_pause4(0);
    }

    set_collection_state(RelocationSafepoint2);

    long prev_used_words = heap->used_in_words();

    {
      if ( BatchedMemoryOps ) {
        // Commit batched memory ops inside a safepoint.
        GPGC_Collector::commit_batched_memory_ops(&tdt, GPGC_Collector::OldCollector);
      } else {
        // If we don't have batched memory ops, then we must do relocation page remaps inside the safepoint:
        remap_old_gen_memory(&tdt);

        // Swap in new read barrier trap array to enable new trap state:
        GPGC_ReadTrapArray::swap_readbarrier_arrays(&tdt, GPGC_Collector::OldCollector);
      }
      
      GPGC_Rendezvous::verify_relocating_threads();

      // Tell threads to self-clean their stacks
      GPGC_ThreadCleaner::enable_thread_self_cleaning(&tdt, JavaThread::gpgc_clean_old);
    }

    // Update the collection state before ending the safepoint, to avoid a race:
    set_collection_state(ConcurrentRelocation2);

    if (_end_relocation_safepoint2) {
      GPGC_Safepoint::end(&tdt);
      cycle_stats()->end_pause4(tdt.pause_ticks());
    } else {
      cycle_stats()->end_pause4(0);
    }

    long frag_words = page_pops()->frag_words_found();
    long live_words = page_pops()->live_words_found() + (page_pops()->no_relocate_pages() << LogWordsPerGPGCPage);
    long live_bytes = live_words << LogBytesPerWord;

    // TODO: Calling heap_at_pause4() twice for OldGC.  Review this and decide what the right thing to do it.
    cycle_stats()->heap_at_pause4(prev_used_words, live_words, frag_words);

    if ( tdt.verbose() ) {
      tdt.end_log("%s"
                  "used " SIZE_FORMAT "M, garbage " SIZE_FORMAT "M, frag " SIZE_FORMAT "M,"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                  tdt.details()?"":" ",
                  heap->used()/M,
                  (page_pops()->dead_words_found()<<LogBytesPerWord)/M,
                  (page_pops()->frag_words_found()<<LogBytesPerWord)/M,
                  live_bytes/M, heap->capacity()/M);
    } else {
      tdt.end_log("%s" SIZE_FORMAT "M"
"("SIZE_FORMAT"M)",
                  tdt.details()?"":" ",
                  live_bytes/M, heap->capacity()/M);
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC initial old relocate tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent relocation phase for OldGen.
void GPGC_OldCollector::relocation_concurrent_old(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: old concurrent relocation",
                         label, _current_total_cycle, _current_old_cycle);
m.enter();

    if ( BatchedMemoryOps ) {
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");
    }

    {
      DetailTracer dt(&tdt, false, "O: End GC_locker concurrent_gc");
      // Release the GC_locker concurrent_gc lock, so JNI threads can get into critical sections again.
      GPGC_Rendezvous::end_gc_locker_concurrent();
    }

    {
      DetailTracer dt(&tdt, false, "O: Complete %ld mid space pages", page_pops()->mid_space_in_old()->max_cursor());

      // Release the remaining memory at the source address of each relocated mid space page.
      GPGC_Space::remapped_pages(page_pops()->mid_space_in_old());
    }

    if ( PageHealing ) {
      if ( GPGCDiagHealDelayMillis>0 ) {
        DetailTracer dt(&tdt, false, "O: Diagnostic delay before mid space page healing");
os::sleep(Thread::current(),GPGCDiagHealDelayMillis,false);
      }

      {
        DetailTracer dt(&tdt, false, "O: Unshatter %ld mid space target pages", page_pops()->mid_space_targets()->current_length());

        // Form parallel tasks to go through each mid space page and force healing.
PGCTaskQueue*q=PGCTaskQueue::create();

        for ( int64_t i=0; i<int64_t(GenPauselessOldThreads); i++ ) {
q->enqueue(new GPGC_OldGC_HealMidPagesTask(i));
        }

        GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
      }
    }

    {
      DetailTracer dt(&tdt, false, "O: Card mark %lu relocated mid space pages in OldGen");

      long work_unit = 10;
      long tasks     = GenPauselessOldThreads;

      assert0(page_pops()->mid_space_in_old()->cursor() == 0);

PGCTaskQueue*q=PGCTaskQueue::create();

      for ( long i=0; i<tasks; i++ ) {
        q->enqueue(new GPGC_OldGC_CardMarkPagesTask(page_pops()->mid_space_in_old(), work_unit));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

      page_pops()->mid_space_in_old()->reset_cursor();
    }


    {
      DetailTracer dt(&tdt, false, "O: Relocate old ");

      assert0(page_pops()->small_space_in_old()->cursor() == 0);
      assert0(page_pops()->mid_space_in_old()->cursor() == 0);

      GPGC_PopulationArray* small_space_in_old = page_pops()->small_space_in_old();
      GPGC_PopulationArray* mid_space_in_old   = page_pops()->mid_space_in_old();

      long work_unit = (GenPauselessOldThreads / 64) + 1;

      // TODO: is this unneeded because it's already been done?
      GPGC_GCManagerOldReloc::set_generations(GPGC_PageInfo::OldGen);

      long small_starting_index = small_space_in_old->cursor();
      long mid_starting_index   = mid_space_in_old->cursor();

PGCTaskQueue*q=PGCTaskQueue::create();
 
      for ( uint64_t i=0; i<GenPauselessOldThreads; i++ ) {
        q->enqueue(new GPGC_OldGC_RelocateSmallPagesTask(work_unit, small_space_in_old));
      }

      GPGC_Rendezvous::verify_relocating_threads();

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

      GPGC_Rendezvous::verify_no_relocating_threads();

      long target_pages     = GPGC_GCManagerOldReloc::total_page_count();
      long new_target_pages = target_pages - _last_target_pages;
      _last_target_pages = target_pages;

      page_pops()->atomic_increment_target_pages_in_old_gen(new_target_pages);

      // This won't be right if we do OldGen relocation in multiple phases: we'll need
      // to track the prior old gen relocation page count, and substract.
      long old_gen_mutator_pages = GPGC_Space::old_gen_relocation_page_count();
      page_pops()->atomic_increment_target_pages_in_old_gen(old_gen_mutator_pages);

      dt.print("pages: %ld -> %ld (%ld)", 0/*TODO: get real number: _old_pages_index - starting_index*/, new_target_pages, old_gen_mutator_pages);
    }

    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::OldGC_RelocateOld, "O:");

if(PageHealing&&PrintGCDetails){
SafepointTimes times;

      {
        DetailTracer dt(&tdt, false, "O: Mutator unshattered page traps: ");

        GPGC_UnshatteredPageTrapCountClosure uptcc;
        GPGC_Safepoint::do_checkpoint(&uptcc, &times);

dt.print(" %ld threads, %ld total traps, %ld max single thread traps",
                 uptcc.get_threads(),
                 uptcc.get_total_traps(),
                 uptcc.get_max_single_thread_traps());
      }

      log_checkpoint_times(&tdt, &times, "O: CRO", "page_traps");
    }

/*
 * TODO: mid_space_in_old()->cursor() is 0, thus causing this test to trigger.  Sanity check this to make sure
 * we are processing all the mid_space_in_old pages.
 *
    // If there's yet more OldGen pages to relocate:
    if ( (page_pops()->small_space_in_old()->cursor() < page_pops()->small_space_in_old()->max_cursor()) ||
         (page_pops()->mid_space_in_old()->cursor() < page_pops()->mid_space_in_old()->max_cursor()) ) {
      os::breakpoint();
      // Make sure NewGC has a chance to run.
      os::yield_all();
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::OldGC_RelocateOld, "O:");

      {
        DetailTracer dt(&tdt, false, "O: Rendezvous start OldGen relocation");
        GPGC_Rendezvous::start_relocating_threads(GenPauselessOldThreads);
      }

      { 
        DetailTracer dt(&tdt, false, "O: Start GC_locker concurrent_gc");
        // Make sure the GC_locker concurrent_gc lock is held while we relocate.  Do it outside a safepoint,
        // so the safepoint time doesn't include waiting for a JNI thread to complete a critical section.
        GPGC_Rendezvous::start_gc_locker_concurrent();
      }
    }
*/

    tdt.end_log(     SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("Old GC old concurrent relocation tasks:");
    GPGC_Heap::old_gc_task_manager()->print_task_time_stamps();
  }
}


// Parallel concurrent mark/remap of objects.
void GPGC_OldCollector::parallel_concurrent_mark(TimeDetailTracer* tdt)
{
  ResourceMark rm;
  HandleMark   hm;

  // concurrent root mark
  {
    DetailTracer dt(tdt, false, "O: System-Dictionary-Always-Strong-Concurrent");

PGCTaskQueue*q=PGCTaskQueue::create();

q->enqueue(new GPGC_OldGC_MarkRootsTaskConcurrent());

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
  }

  mark_all_discovered(tdt, OldCollector);

  // NewToOldGC now does a thread stack clean that covers OldGen and PermGen roots.

  {
    // Make sure the NewGC that kicked off this OldGC has delivered all the OldGen roots.
    GPGC_Interlock interlock(tdt, GPGC_Interlock::NewToOld_RootsFlushed, "O:");
  }

  mark_all_discovered(tdt, OldCollector);

  // Flush buffered refs and mark, until no more buffered refs are flushed.
  while (true)
  {
    long new_gen_refs, old_gen_refs;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "O: CM", new_gen_refs, old_gen_refs);
    if ( old_gen_refs != 0 ) {
      mark_all_discovered(tdt, OldCollector);
      continue;
    }

    if ( ! GPGC_GCManagerOldFinal::all_stacks_are_empty() ) {
      mark_all_final_discovered(tdt, OldCollector);
      continue;
    }

    break;
  }

  {
    // Acquiring the ref processing lock has to be done prior to the concurrent ref
    // processing phase-change safepoint.  This ensures:
    //   - To ensure that applications which call java.lang.Runtime.runFinalizersOnExit(true)
    //     aren't able to corrupt the determination of which objects are alive.  See Azul bug 21359.
    //   - To ensure that java.lang.ref.Reference.enqueue() doesn't clash with concurrent ref
    //     ref processing.  See Azul bug 21395.
    //   - To ensure that lookup of a FinalReference referent doesn't clash with concurrent
    //     ref processing.  See Azul bug 21359.
#ifdef ASSERT
    bool is_heap_iterate_collection = GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection();
    if (is_heap_iterate_collection) { guarantee(!_end_1st_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT 

    if (_end_1st_mark_remap_safepoint) {
      DetailTracer dt(tdt, false, "O: acquire java.lang.ref pending list lock");
      jlr_handler()->acquire_pending_list_lock();
    }
  }
}


// Flush and mark any remaining refs, so the marking phase can be changed
void GPGC_OldCollector::parallel_flush_and_mark(TimeDetailTracer* tdt)
{
  {
    // Flush any buffered refs from GCLdValueNMTTr traps in JavaThreads
    long new_gen_refs, old_gen_refs;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "O: RPSP", new_gen_refs, old_gen_refs);
  }

  if ( GPGCVerifyThreadStacks ) {
    // The VMThread should have self scanned at the end of the initial mark safepoint.
    // C2 shouldn't schedule LVBs across safepoints.  Verify no un-NMTed refs in thread stacks.
    DetailTracer dt(tdt, false, "O: Verify clean thread stacks");
    GPGC_OldGC_VerifyMarkedLiveClosure vmlc;
    VMThread::vm_thread()->oops_do(&vmlc);
Threads::oops_do(&vmlc);
  }

  {
    // TODO: maw: Instead of completing marking in the safepoint, we might want to
    // bail out of the safepoint, mark concurrently, and then retry the safepoint.

    // We try and mark without checking to see if the prior flush turned up anything,
    // as there's a race with JavaThreads popping something onto the stack just before
    // the safepoint happens.  There may be a race with the other GC flushing as well.
    GPGC_Collector::mark_all_discovered(tdt, OldCollector);
    GPGC_Collector::mark_all_final_discovered(tdt, OldCollector);
  }

  // During the next phase of concurrent ref processing, mutators may be trapping on
  // finally live objectRefs that were originally looked up via JNI weak refs.  The
  // LVB traps would have CAS'ed back refs with updated NMTs and enqueued the ref for
  // the GC threads, but without marking the objects strong live.  Any mutator refs
  // found prior to the end of strong live marking of final referents should be for
  // objects that are already marked finally live.  We set this state flag to instruct
  // the mutator ref buffer flushing to assert that all mutator refs discovered have
  // already been marked final live.
  _mutator_ref_sanity_check = GPGC_Collector::MustBeFinalLive;

  {
    // TODO: XXX: shouldn't be able to get an interlock inside a safepoint, it could draw out
    // the pause.
    GPGC_Interlock interlock(tdt, GPGC_Interlock::NewGC_SystemDictionaryMarking, "O:");

    {
      DetailTracer dt(tdt, false, "O: System dict unloading: ");
      ResourceMark rm;
      HandleMark   hm;

SystemDictionary::reset_unloaded_classes();

PGCTaskQueue*q=PGCTaskQueue::create();

      long sections = GenPauselessOldThreads;

      for ( long section=0; section<sections; section++ ) {
        q->enqueue(new GPGC_OldGC_UnloadDictionarySectionTask(section, sections));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

dt.print("unloaded %d classes, %d live",
SystemDictionary::number_of_unloaded_classes(),
          SystemDictionary::number_of_classes());
    }

    {
      DetailTracer dt(tdt, false, "O: Implementor adjusting");

      long sections = GenPauselessOldThreads;

      // Single threaded fixing of superklass _implementors. Each task thread
      // built a list of classes needing fixing, now go over each list and 
      // fix each superklass
      SystemDictionary::GPGC_unload_section_cleanup(sections);
    }

    {
      DetailTracer dt(tdt, false, "O: Loader constraints");
      ResourceMark rm;
      HandleMark   hm;

PGCTaskQueue*q=PGCTaskQueue::create();

      long sections = GenPauselessOldThreads;

      for ( long section=0; section<sections; section++ ) {
        q->enqueue(new GPGC_OldGC_UnloadLoaderConstraintSectionTask(section, sections));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

#ifdef ASSERT
      SystemDictionary::verify_dependencies(3);
#endif
    }
  }

  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("8"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("8"); )
}


// Parallel concurrent handling of soft/weak/final/phantom/jni refs.
void GPGC_OldCollector::parallel_concurrent_ref_process(TimeDetailTracer* tdt)
{
  {
    DetailTracer dt(tdt, false, "O: JLR handling setup");
    // Can't assert mutator marking stacks are empty, because there's a race with mutators looking up JNI weak refs.
    // Can't assert NTO marking stacks are empty, because we haven't flushed them to OldGC yet.
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("9"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("9"); )
    GPGC_GCManagerOldStrong::save_reference_lists();
    jlr_handler()->set_jlr_capture(false);
  }

  {
    DetailTracer dt(tdt, false, "O: Parallel soft refs");
    // Clear soft referents not marked strong live, or update referent objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_soft_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("10"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("10"); )
  }
  {
    DetailTracer dt(tdt, false, "O: Parallel weak refs");
    // Clear weak referants not marked strong live, or update referent objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_weak_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("11"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("11"); )
  }
  {
    DetailTracer dt(tdt, false, "O: Parallel final refs");
    // Enqueue final refs whose referents are not marked strong live, which will have not
    // been previously put on the pending list by the collector.  No marking through yet.
    jlr_handler()->do_final_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("12"); )
  }

  // Mark strong live through the final referents.  We may have to try multiple times, as we're racing with
  // mutators that may be trapping on refs looked up through JNI weak references.  When a mutator beats GC to
  // an objectRef, it updates the NMT, which halts the GC threads traversal.  Traversal is resumed when we
  // flush the mutator ref buffers and continue traversing from what is found there.
  long old_gen_refs_flushed;
  do {
    mark_all_discovered(tdt, OldCollector);

    long new_gen_refs_flushed;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "O: CRP", new_gen_refs_flushed, old_gen_refs_flushed);
  } while (old_gen_refs_flushed != 0);

  {
    // From now until the end of concurrent ref processing, we expect to find no more refs from the mutators.
    _mutator_ref_sanity_check = GPGC_Collector::MustBeStrongLive;

    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("13"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("13"); )
  }

  {
    DetailTracer dt(tdt, false, "O: JNI weak refs");
    // Clear JNI weak refs not marked strong live, or update ref objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_jni_weak_refs();
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("14"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("14"); )
  }
  {
    DetailTracer dt(tdt, false, "O: Parallel phantom refs");
    // Enqueue phantom refs not previously pending whose referents are not marked strong
    // live.  Push referent objectRef addresses to the marking stack for later metadata
    // update and strong live marking through.
    jlr_handler()->do_phantom_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("15"); )
  }
  {
    // Now update phantom referent objectRef metadata and do a strong live mark traversal.
    mark_all_discovered(tdt, OldCollector);
    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("16"); )
    DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("16"); )
  }

  GPGC_GCManagerOldStrong::verify_empty_ref_lists();
  guarantee(!jlr_handler()->is_capture_on(), "OldGC JLR capture not off");
  guarantee(jlr_handler()->none_captured(), "OldGC JLR handler has captured refs");

#ifdef ASSERT
  bool is_heap_iterate_collection = GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection();
  if (is_heap_iterate_collection) { guarantee(!_end_2nd_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT

  if (_end_2nd_mark_remap_safepoint) {
    jlr_handler()->release_pending_list_lock();
  }

  DEBUG_ONLY( final_clear_mutator_ref_buffers(tdt, OldCollector, "O: CRP"); )
}


// Parallel final mark/remap of things needing a safepoint.
void GPGC_OldCollector::parallel_final_mark(TimeDetailTracer* tdt)
{
assert(GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");

  ResourceMark rm;
  HandleMark   hm;

  if ( GPGCVerifyThreadStacks ) {
    // This option verifies that we haven't missed an oop that hasn't yet been seen by the collector.
    DetailTracer dt(tdt, false, "O: SP3: Verify clean thread stacks");
    GPGC_OldGC_VerifyMarkedLiveClosure vmlc;
    VMThread::vm_thread()->oops_do(&vmlc);
Threads::oops_do(&vmlc);
  }

  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("17"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("17"); )
  GPGC_GCManagerOldStrong::verify_empty_ref_lists();
  guarantee(!jlr_handler()->is_capture_on(), "OldGC JLR capture not off");
  guarantee(jlr_handler()->none_captured(), "OldGC JLR handler has captured refs");

  {
    DetailTracer dt(tdt, false, "O: ARTA table");
    ArtaObjects::GPGC_unlink();
  }

  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("18"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("18"); )
  GPGC_GCManagerOldStrong::verify_empty_ref_lists();
  guarantee(!jlr_handler()->is_capture_on(), "OldGC JLR capture not off");
  guarantee(jlr_handler()->none_captured(), "OldGC JLR handler has captured refs");

  // JVMTI object tagging is based on JNI weak refs. If any of these
  // refs were cleared then JVMTI needs to update its maps and
  // maybe post ObjectFrees to agents.
JvmtiExport::gpgc_ref_processing_epilogue();

  DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("19"); )
  DEBUG_ONLY( GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("19"); )
}


// Parallel concurrent mark/remap of remaining weak roots
void GPGC_OldCollector::parallel_concurrent_weak_marks(TimeDetailTracer* tdt)
{
  ResourceMark rm;
  HandleMark   hm;

  if ( GPGCVerifyHeap ) {
    // grab the symbol-string-table verify inter-lock in the verify mode
    GPGC_Interlock::acquire_interlock(tdt, GPGC_Interlock::SymbolStringTable_verify, "Unlink in verify mode:");
  }

  {
    DetailTracer dt(tdt, false, "O: symbol and string table unlink");

PGCTaskQueue*q=PGCTaskQueue::create();
    long          sections = GenPauselessOldThreads;

    for ( long section=0; section<sections; section++ ) {
      q->enqueue(new GPGC_OldGC_UnlinkWeakRootSectionTask(GPGC_OldGC_UnlinkWeakRootSectionTask::symbol_table, section, sections));
    }
    for ( long section=0; section<sections; section++ ) {
      q->enqueue(new GPGC_OldGC_UnlinkWeakRootSectionTask(GPGC_OldGC_UnlinkWeakRootSectionTask::string_table, section, sections));
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
  }

  if ( GPGCVerifyHeap ) {
    GPGC_Interlock::release_interlock(tdt, GPGC_Interlock::SymbolStringTable_verify, "Unlink in verify mode:");
  }

  {
    GPGC_Interlock interlock(tdt, GPGC_Interlock::OopTableUnlink, "O:");

    {
      DetailTracer dt(tdt, false, "O: Klass table unlinking");

      ResourceMark rm;
      HandleMark   hm;

PGCTaskQueue*q=PGCTaskQueue::create();

      long sections = GenPauselessOldThreads;
      guarantee(GPGC_Heap::old_gc_task_manager()->active_threads() == 0, "thread count messed up");

      for ( long section=0; section<sections; section++ ) {
        q->enqueue(new GPGC_OldGC_UnlinkKlassTableTask(section, sections));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(GPGC_Heap::old_gc_task_manager(), q);
    }

    {
      DetailTracer dt(tdt, false, "O: CodeCacheOop table unlinking");

      ResourceMark rm;
      HandleMark   hm;

PGCTaskQueue*q=PGCTaskQueue::create();

      long sections = GenPauselessOldThreads;

      for ( long section=0; section<sections; section++ ) {
        q->enqueue(new GPGC_OldGC_UnlinkCodeCacheOopTableTask(section, sections));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);
    }
  }

  {
    DetailTracer dt(tdt, false, "O: CodeCache unlinking");
CodeCache::GPGC_unlink();
  }

  {
    DetailTracer dt(tdt, false, "O: Class revisiting");

MutexLocker ml(Compile_lock,Thread::current());

    ResourceMark rm;
    HandleMark   hm;

PGCTaskQueue*q=PGCTaskQueue::create();

    long sections = GenPauselessOldThreads;
    guarantee(GPGC_Heap::old_gc_task_manager()->active_threads() == 0, "thread count messed up");

    for ( long section=0; section<sections; section++ ) {
q->enqueue(new GPGC_OldGC_StealRevisitKlassTask());
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

    DEBUG_ONLY( GPGC_GCManagerOldStrong::ensure_revisit_stacks_are_empty(); )
  }

  // TODO: verify strength of all symboltable entries remaining
}


// When we relocate live objects out of the sparse pages, we keep a sideband
// data structure to map old object locations to new object locations.  Set
// that up now for all the pages we plan to relocate.  Page relocation is done
// in two passes.  First the perm gen pages must be relocated, and then all
// other pages.  Within each pass, pages are separated in "stripes" of pages,
// where the pages within a stripe must be relocated in sequence.
//
void GPGC_OldCollector::prepare_sideband_forwarding(TimeDetailTracer* tdt)
{
  {
    GPGC_Interlock interlock(tdt, GPGC_Interlock::SidebandAlloc, "O:");

    {
      DetailTracer dt(tdt, false, "O: Serial sideband allocation: ");

      bool                  safe_to_iterate = true;
      GPGC_PopulationArray* all_populations = page_pops()->all_populations();
      uint64_t              cursor          = 0;
      uint64_t              max_cursor      = all_populations->max_cursor();

      // Claim space in the sideband forwarding region for each page we're going to relocate.
      for ( ; cursor<max_cursor; cursor++ ) {
        PageNum        page           = all_populations->page(cursor);
        GPGC_PageInfo* info           = GPGC_PageInfo::page_info(page);
        long           live_objs      = GPGC_PageInfo::live_objs_from_raw(info->raw_stats());
        bool           in_small_space = GPGC_Layout::small_space_page(page);

        assert0(!GPGC_Layout::large_space_page(page));

        // cross check live object count:
        DEBUG_ONLY( long pages      = in_small_space ? 1 : PagesPerMidSpaceBlock; )
        DEBUG_ONLY( long mark_count = GPGC_Marks::count_live_marks(page, pages); )
        assert0(mark_count==live_objs);
        assert0(info->just_gen()==GPGC_PageInfo::PermGen || info->just_gen()==GPGC_PageInfo::OldGen);

        if ( ! GPGC_PageRelocation::old_gc_get_sideband_space(info, live_objs) ) {
          // We're out of space for sideband forwarding arrays.
          // Cap the limit of reclaimed pages where we ran out of space.
          all_populations->sideband_limit_reclaim_cutoff(cursor);
          safe_to_iterate = false;

          if ( PrintGCDetails ) {
            uint64_t skipped_populations = max_cursor - cursor;
            uint64_t sideband_limited_MB = (all_populations->sideband_limited_words() << LogBytesPerWord) / M;

gclog_or_tty->print_cr("Limiting relocation: out of sideband: skipping %lu pages (%lu MB garbage)",
                                   skipped_populations, sideband_limited_MB);
          }

          break;
        }

        bool in_perm_gen = (info->just_gen() == GPGC_PageInfo::PermGen);

        if ( in_small_space ) {
          if ( in_perm_gen ) {
            page_pops()->small_space_in_perm()->add_page(all_populations->page_pop(cursor));
          } else {
            page_pops()->small_space_in_old()->add_page(all_populations->page_pop(cursor));
          }
        } else {
          if ( in_perm_gen ) {
            page_pops()->mid_space_in_perm()->add_page(all_populations->page_pop(cursor));
          } else {
            page_pops()->mid_space_in_old()->add_page(all_populations->page_pop(cursor));
          }
        }
      }
      
      GPGC_Heap::set_safe_to_iterate(safe_to_iterate);

      page_pops()->small_space_in_old()->reset_max_cursor();
      page_pops()->small_space_in_perm()->reset_max_cursor();
      page_pops()->mid_space_in_old()->reset_max_cursor();
      page_pops()->mid_space_in_perm()->reset_max_cursor();

      page_pops()->small_space_skipped_pages()->reset_max_cursor();
      page_pops()->mid_space_skipped_pages()->reset_max_cursor();

      if ( ! GPGCCollectMidSpace ) {
        page_pops()->mid_space_in_old()->reset();
        page_pops()->mid_space_in_perm()->reset();
      }

      if ( ! GPGCCollectLargeSpace ) {
        // OldGC doesn't do anything with large space anyway, this option only affect NewGC.
      }
    }
  }

  {
    DetailTracer dt(tdt, false, "O: Relocation array setup: ");

    // TODO: these stats are bogus, should be counting number of allocated 2MB pages selected for relocation
    //       in mid space, not number of mid space blocks.
    page_pops()->set_source_pages_to_old_gen(page_pops()->small_space_in_old()->max_cursor()  + page_pops()->mid_space_in_old()->max_cursor() +
                                             page_pops()->small_space_in_perm()->max_cursor() + page_pops()->mid_space_in_perm()->max_cursor());

dt.print(" %ld MB new, %ld MB old, %ld MB empty",
               GPGC_PageRelocation::new_sideband_bytes_in_use() / M,
               GPGC_PageRelocation::old_sideband_bytes_in_use() / M,
               GPGC_PageRelocation::sideband_bytes_free() / M);
  }

  {
    DetailTracer dt(tdt, false, "O: Parallel forwarding hash init: %d source pages",
                    page_pops()->small_space_in_old()->max_cursor()  + page_pops()->mid_space_in_old()->max_cursor() +
                    page_pops()->small_space_in_perm()->max_cursor() + page_pops()->mid_space_in_perm()->max_cursor());

    assert0(page_pops()->small_space_in_old()->cursor()  == 0);
    assert0(page_pops()->small_space_in_perm()->cursor() == 0);
    assert0(page_pops()->mid_space_in_old()->cursor()    == 0);
    assert0(page_pops()->mid_space_in_perm()->cursor()   == 0);

    long work_unit = (GenPauselessOldThreads / 64) + 1;

PGCTaskQueue*q=PGCTaskQueue::create();

for(uint32_t i=0;i<GenPauselessOldThreads;i++){
q->enqueue(new GPGC_OldGC_SidebandForwardingInitTask(work_unit));
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::old_gc_task_manager(), q);

    page_pops()->small_space_in_old()->reset_cursor();
    page_pops()->small_space_in_perm()->reset_cursor();
    page_pops()->mid_space_in_old()->reset_cursor();
    page_pops()->mid_space_in_perm()->reset_cursor();
  }

  // The sideband forwarding structures are now ready for LdValueTr traps to occur.
}


// Similar to objectRef_needed_nmt except this routine NULLs the reference if the
// oop is not marked.  Returns true iff reference is live and not null.
bool GPGC_OldCollector::remap_and_nmt_if_live(objectRef* ref_addr) {
  bool outcome = true;
  objectRef old_ref = *ref_addr;
  objectRef new_ref = old_ref;
  if ( new_ref.is_null() ) { return false; }

  DEBUG_ONLY( bool is_poisoned = new_ref.is_poisoned(); )
  DEBUG_ONLY( new_ref = PERMISSIVE_UNPOISON(new_ref, ref_addr); )
  assert((new_ref.raw_value() & 0x7)==0, "invalid ref (poisoned?) found while marking");

  // We ignore new_space refs, they're handled by the NewGen GC.
  if ( new_ref.is_new() ) { return false; }

  if ( GPGC_NMT::has_desired_old_nmt(new_ref) ) {
    assert(!GPGC_ReadTrapArray::is_remap_trapped(new_ref), "Marked through old_space objectRefs must have already been relocated");
    return outcome;
  }

  // Unmarked-through old_space objectRefs may need to be remapped.
  if ( GPGC_ReadTrapArray::is_remap_trapped(new_ref) ) {
    assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(new_ref));
    // By the time the Old GC collector starts scanning thread stacks, there should only
    // be relocated pages, and no relocating pages, so the object must have already been
    // copied, and we're just remapping here.
    new_ref = GPGC_Collector::get_forwarded_object(new_ref);

    // Remappped old_space refs must always still be old_space refs.
    assert0(new_ref.is_old());
  } else {
    // Set the NMT bit to the marked-through value.
    new_ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
  }
  
  // If it is not live null the field out.
  if (!GPGC_Marks::is_any_marked_strong_live(new_ref.as_oop())) {
    outcome = false;
    new_ref = nullRef;
  }  

  // try and update the source of the objectRef
  intptr_t old_value = old_ref.raw_value();
  intptr_t new_value = new_ref.raw_value();
  DEBUG_ONLY( if (is_poisoned) { new_value = ALWAYS_POISON_OBJECTREF(new_ref).raw_value(); } )

  if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
    // CAS updated the ref in memory.  Return true, to indicate the objectRef must be marked through.
    GPGC_Marks::set_nmt_update_markid(ref_addr, 0x69);
    return outcome;
  }

  // Updating the objectRef failed.  Either:
  //   1. A mutator hit an LVB, and updated the objectRef before we could, which is OK.
  //   2. A mutator overwrote the objectRef with a different value, so the objectRef seen
  //      in this function isn't there anymore, which is OK.
  //   3. Some thread did a remap without NMT update, in which case this function still
  //      nneds to rewrite the objectRef.
  //
  // Below, we test for and then handle case 3.

  objectRef changed_ref = PERMISSIVE_UNPOISON(*ref_addr, ref_addr);
  if ( changed_ref.is_null() ) { return false; }
  if ( changed_ref.is_new() )  { return false; }

  assert(changed_ref.is_old(), "OldGen GenPauselessGC sees non-heap objectRef");

  // If the other guy remapped and set the NMT, no action is necessary
  if ( GPGC_NMT::has_desired_old_nmt(changed_ref) ) {
    assert0(!GPGC_ReadTrapArray::is_remap_trapped(changed_ref));
    return outcome;
  }

  // The only way we should get to here is if another thread did a remap-only LVB and moved
  // old_ref to new_ref in an old_space->old_space remapping, and didn't set the the NMT flag.
  assert0(changed_ref.as_oop() == new_ref.as_oop());

  // Update the ref with the correct NMT set
  old_value = ALWAYS_POISON_OBJECTREF(changed_ref).raw_value();

  if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)ref_addr, old_value) ) {
    // CAS updated the ref in memory.  Return true, to indicate the old_space ref must be marked through now.
    GPGC_Marks::set_nmt_update_markid(ref_addr, 0x6A);
    return outcome;
  }

  // Someone else updated the objectRef again!  This time, it must be set to a valid value.
#ifdef ASSERT
  changed_ref = PERMISSIVE_UNPOISON(*ref_addr, ref_addr);
  assert0(changed_ref.is_new() || (changed_ref.is_old() && GPGC_NMT::has_desired_old_nmt(changed_ref)));
#endif // ASSERT

  return outcome;
}


objectRef GPGC_OldCollector::get_weak_ref(objectRef* ref_addr) {
  objectRef  ref = ALWAYS_UNPOISON_OBJECTREF(*ref_addr);
  long state = collection_state();
  
  if ( (state >= GPGC_Collector::ConcurrentRefProcessing) &&
       (state <= GPGC_Collector::ConcurrentWeakMarking) ) {
if(ref.not_null()){
      assert(ref.is_old(), "Must be an old ref");
      if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(ref);
      }
      
      if ( !GPGC_Marks::is_any_marked_strong_live(ref.as_oop()) &&
           (state!=GPGC_Collector::ConcurrentRefProcessing || !GPGC_Marks::is_any_marked_final_live(ref.as_oop())) ) {
ref=nullRef;
      } else if ( !GPGC_NMT::has_desired_old_nmt(ref) ) {
        ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
      }
    }
  } else {
    ref = lvb_ref(ref_addr);
  }
  
  return ref;
}


objectRef GPGC_OldCollector::get_weak_ref(objectRef ref) {
  long state = collection_state();
  
  if ( (state >= GPGC_Collector::ConcurrentRefProcessing) &&
       (state <= GPGC_Collector::ConcurrentWeakMarking) ) {
if(ref.not_null()){
      assert(ref.is_old(), "Must be an old ref");
      if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(ref);
      }
      
      if ( !GPGC_Marks::is_any_marked_strong_live(ref.as_oop()) &&
           (state!=GPGC_Collector::ConcurrentRefProcessing || !GPGC_Marks::is_any_marked_final_live(ref.as_oop())) ) {
ref=nullRef;
      } else if ( !GPGC_NMT::has_desired_old_nmt(ref) ) {
        ref.set_nmt(GPGC_NMT::desired_old_nmt_flag());
      }
    }
  } else {
    ref = lvb_ref(&ref);
  }
  
  return ref;
}
