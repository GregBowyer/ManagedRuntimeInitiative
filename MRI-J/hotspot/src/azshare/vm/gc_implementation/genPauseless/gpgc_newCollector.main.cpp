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


#include "allocatedObjects.hpp"
#include "auditTrail.hpp"
#include "codeBlob.hpp"
#include "cycleCounts.hpp"
#include "debug.hpp"
#include "gpgc_closures.hpp"
#include "gpgc_gcManagerNewFinal.hpp"
#include "gpgc_gcManagerNewReloc.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_rendezvous.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_slt.hpp"
#include "gpgc_space.hpp"
#include "gpgc_stats.hpp"
#include "gpgc_tasks.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_threadCleaner.hpp"
#include "gpgc_traps.hpp"
#include "handles.hpp"
#include "jvmtiExport.hpp"
#include "liveObjects.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "pgcTaskManager.hpp"
#include "referencePolicy.hpp"
#include "resourceArea.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"
#include "universe.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "handles.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "thread_os.inline.hpp"


long                     GPGC_NewCollector::_collection_state               = GPGC_Collector::NotCollecting;
long                     GPGC_NewCollector::_mutator_ref_sanity_check       = GPGC_Collector::NoMutatorRefs;
long                     GPGC_NewCollector::_should_mark_new_objs           = false;
long                     GPGC_NewCollector::_current_new_cycle              = 0;
long                     GPGC_NewCollector::_current_total_cycle            = 0;
GPGC_JavaLangRefHandler* GPGC_NewCollector::_jlr_handler                    = NULL;
GPGC_CycleStats*         GPGC_NewCollector::_cycle_stats                    = NULL;
GPGC_Population*         GPGC_NewCollector::_page_pops                      = NULL;
GPGC_RelocationSpike     GPGC_NewCollector::_relocation_spike;

bool                     GPGC_NewCollector::_mark_old_space_roots           = false;
int64_t                  GPGC_NewCollector::_promotion_threshold_time       = 0;
jlong GPGC_NewCollector::_time_of_last_gc=0;

AuditTrail*              GPGC_NewCollector::_audit_trail                    = NULL;

bool                     GPGC_NewCollector::_start_global_safepoint         = false;
bool                     GPGC_NewCollector::_start_1st_mark_remap_safepoint = true;
bool                     GPGC_NewCollector::_end_1st_mark_remap_safepoint   = true;
bool                     GPGC_NewCollector::_start_2nd_mark_remap_safepoint = true;
bool                     GPGC_NewCollector::_end_2nd_mark_remap_safepoint   = true;
bool                     GPGC_NewCollector::_start_3rd_mark_remap_safepoint = true;
bool                     GPGC_NewCollector::_end_3rd_mark_remap_safepoint   = true;
bool                     GPGC_NewCollector::_start_mark_verify_safepoint    = true;
bool                     GPGC_NewCollector::_end_mark_verify_safepoint      = true;
bool                     GPGC_NewCollector::_end_mark_phase_safepoint       = false;
bool                     GPGC_NewCollector::_start_relocation_safepoint     = true;
bool                     GPGC_NewCollector::_end_relocation_safepoint       = true;
bool                     GPGC_NewCollector::_end_global_safepoint           = false;



void GPGC_NewCollector::initialize()
{
guarantee(_page_pops==NULL,"Shouldn't be initializing GPGC_NewCollector twice");

  _jlr_handler = new GPGC_JavaLangRefHandler(GPGC_PageInfo::NewGen, objectRef::new_space_id);
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


class GPGC_NewGC_VerifyMarkedLiveClosure:public OopClosure{
  public:
    bool check_derived_oops()      { return false; }
    void do_oop(objectRef* p)      { objectRef r = UNPOISON_OBJECTREF(*p, p);
                                     if (!r.is_new()) { return; }
                                     assert0(GPGC_NMT::has_desired_new_nmt(r) && !GPGC_ReadTrapArray::is_remap_trapped(r)); }
};


// Execute a NewGen GC cycle of the generational pauseless collector.  This will
// mark all live NewGen objects, and relocate the most sparse pages.
void GPGC_NewCollector::collect(const char* label, bool clear_all, GPGC_CycleStats* stats,
                                SafepointEndCallback safepoint_end_callback, void* user_data)
{
  if ( GPGCNoGC ) return;

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  if (_start_global_safepoint) {
Unimplemented();//TODO-LATER: Global safepoints not currently implemented.
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
  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("1"); )
  DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("1"); )
  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_mutator_list_is_empty("1"); )

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

  mark_remap_phase(label, clear_all, safepoint_end_callback, user_data);

  GPGC_PageBudget::return_grant_pages();

  cycle_stats()->start_relocate();

  // Phase 2: Relocate
  relocation_phase(label);

  // if (_end_global_safepoint) {
  //   GPGC_Safepoint::end(&tdt);
  // }

#ifdef ASSERT
  if (is_heap_iterate_collection) { guarantee(!_end_2nd_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT

  if (!_end_2nd_mark_remap_safepoint) {
    jlr_handler()->release_pending_list_lock();
  }

  assert0( !GPGC_Safepoint::is_at_safepoint() );

  GPGC_PageBudget::return_pause_pages();

  reset_millis_since_last_gc();

_cycle_stats=NULL;

  set_collection_state(NotCollecting);

  AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_END_NEW_GC_CYCLE);
}


// This method orchestrates the first phase of a NewGen GC: marking all live objects,
// and remapping references to objects that were relocated in the previous GC cycle.
void GPGC_NewCollector::mark_remap_phase(const char* label, bool clear_all,
                                         SafepointEndCallback safepoint_end_callback, void* user_data)
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  //***
  //***  Phase 1: Mark and Remap references
  //***

  marking_init();

  //*** Setup for the mark/remap phase.
  mark_remap_setup(heap, label, clear_all);

  //*** Initial safepoint of mark/remap phase.
  mark_remap_safepoint1(heap, label, safepoint_end_callback, user_data);

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

  //*** Object Mark Verification Safepoint
  mark_remap_verify_mark(heap, label);

  //*** Pre mark/remap cleanup
  pre_mark_remap_cleanup(heap, label);

  //*** Final mark/remap cleanup
  mark_remap_cleanup(heap, label);
}


// This method orchestrates the second phase of a NewGen GC: identifying empty pages
// to free and sparse pages to relocate, and then relocating the live objects out
// of the sparse pages.
void GPGC_NewCollector::relocation_phase(const char* label) {
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  //***
  //***  Phase 2: Relocate
  //***

  size_t total_free_words;

  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("2"); )
  DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("2"); )
  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_mutator_list_is_empty("2"); )

  if ( GPGCNoRelocation ) {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: GPGCNoRelocation",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    page_pops()->reset_populations();
    page_pops()->reset_cursor();
relocation_spike()->reset();
    GPGC_Space::new_gc_stop_marking();
    GPGC_Space::new_gc_clear_no_relocate();
    GPGC_Space::new_gc_clear_page_marks();
    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "N:");
    return;
  }

  //*** Concurrent relocation setup
  relocation_setup(heap, label);

  //*** Initial safepoint of relocation phase.
  relocation_safepoint1(heap, label);

  //*** Concurrent finish of relocation phase.
  relocation_concurrent_new(heap, label);

  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: Concurrent clear page marks",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    {
      GPGC_Interlock interlock(&tdt, GPGC_Interlock::NewGC_MarkClearing, "N:");

      {
        DetailTracer dt(&tdt, false, "N: Clear page marks");

PGCTaskQueue*q=PGCTaskQueue::create();

        long sections = (GenPauselessNewThreads<2) ? 1 : (GenPauselessNewThreads-1);

q->enqueue(new GPGC_NewGC_ClearMultiSpaceMarksTask());

        for ( long section=0; section<sections; section++ ) {
          q->enqueue( new GPGC_NewGC_ClearOneSpaceMarksTask(section, sections) );
        }

        // Wait for the tasks to be completed.
        GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
      }
    }

    {
      DetailTracer dt(&tdt, false, "N: Clear no-relocate flags");
      GPGC_Space::new_gc_clear_no_relocate();
    }

    if (_end_global_safepoint) {
      if ( mark_old_space_roots() ) { GPGC_Rendezvous::end_coordinated_safepoint_prepare(); }
      
      {
        DetailTracer dt(&tdt, false, "N: End relocation phase safepoint");
        GPGC_Safepoint::end(&tdt);
      }

      if ( mark_old_space_roots() ) { GPGC_Rendezvous::end_coordinated_safepoint(); }
    }
  }

  GPGC_PageBudget::new_gc_verify_preallocated_pages();

  // TODO: maw: what do we do with this call?
  Universe::update_heap_info_at_gc();

  long collection_delta = (page_pops()->total_released() +
                           page_pops()->source_pages_to_new_gen() +
                           page_pops()->source_pages_to_old_gen() -
                           page_pops()->target_pages_in_new_gen()) << LogWordsPerGPGCPage;

  long promoted = page_pops()->target_pages_in_old_gen() + page_pops()->large_space_to_old()->block_pages_total();

  cycle_stats()->heap_at_end_relocate(collection_delta,
                                      relocation_spike()->peak_value(),
                                      page_pops()->no_relocate_pages(),
                                      promoted,
                                      page_pops()->target_pages_in_new_gen());

  set_collection_state(CycleCleanup);
}


// Setup for the mark/remap phase.
void GPGC_NewCollector::mark_remap_setup(GPGC_Heap* heap, const char* label, bool clear_all)
{
  _current_total_cycle = heap->actual_collections() + 1;
  _current_new_cycle   = heap->actual_new_collections() + 1;

  AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_START_NEW_GC_CYCLE,
                       _current_total_cycle, _current_new_cycle);

  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent mark setup: %s soft refs",
                         label, _current_total_cycle, _current_new_cycle,
                         clear_all ? "clear all" : "normal");
m.enter();

    set_collection_state(MarkRemapSetup);

    if ( clear_all ) {
      jlr_handler()->pre_mark(new AlwaysClearPolicy());
    } else {
      jlr_handler()->pre_mark(new LRUMaxHeapPolicy());
    }

    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "N:");

    if ( mark_old_space_roots() ) {
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewToOld_RootsFlushed, "N:");
    }

    if ( mark_old_space_roots() ) {
      DetailTracer dt(&tdt, false, "N: Trigger OldGC");
      assert0(GPGC_OldCollector::collection_state() == NotCollecting);
      GPGC_OldCollector::request_collection_start();
      GPGC_Rendezvous::trigger_old_gc();
    }

    // Always take the BatchedMemoryOps interlock, so the two collectors don't fight over who
    // is updating the read barrier trap array.
    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "N:");

    // set the upcoming NMT for this cycle
    GPGC_NMT::set_upcoming_nmt(mark_old_space_roots());          

    SafepointTimes sptimes;

    {
      // Make sure that mutators see the upcoming NMT flag set above
      DetailTracer dt(&tdt, false, "N: Checkpoint to ensure upcoming NMT flag visibility.");
      NopThreadClosure ntc;
      GPGC_Safepoint::do_checkpoint(&ntc, &sptimes);
    }

    log_checkpoint_times(&tdt, &sptimes, "N: MRS", "NOOP");

    GPGC_Collector::setup_read_trap_array(&tdt, GPGC_Collector::NewCollector);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("New GC concurrent mark setup tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Initial safepoint of mark/remap phase.
void GPGC_NewCollector::mark_remap_safepoint1(GPGC_Heap* heap, const char* label, SafepointEndCallback safepoint_end_callback, void* user_data)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    // +1 is added to the count since we don't update it until we are in the safepoint 
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%da: initial mark pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_new_cycle,
                         heap->used()/M, heap->capacity()/M);
m.enter();

    if (_start_1st_mark_remap_safepoint) {
      // If we're marking roots for OldGC, then we start a safepoint
      // that covers both NewGC and OldGC:
      GPGC_Safepoint::begin(&tdt, false, safepoint_end_callback, user_data, mark_old_space_roots());
      cycle_stats()->start_pause1(tdt.pause_start_ticks());
    } else {
      cycle_stats()->start_pause1(0);
    }

    set_collection_state(InitialMarkSafepoint);

    {
      // increment the count inside the safepoint so we can use it to sync the vm op
      // allocation result with reality once it is given back to the caller thread - if the 
      // caller thread is switched out for a long time, another complete gc can pass him by,
      // so he needs to discard the vm op allocation, loop around and allocate again.
heap->increment_actual_collections();
heap->increment_actual_new_collections();

      {
        DetailTracer dt(&tdt, false, "N: Flush TLABs");

        // Fill in TLABs
        heap->ensure_parsability(true);
      }

      {
        DetailTracer dt(&tdt, false, "N: Prepare NewGen for marking");

        // There should be no objects marked live at this point.
        DEBUG_ONLY (
          if ( mark_old_space_roots() ) {
            GPGC_Space::new_and_old_gc_verify_no_live_marks();
          } else {
            GPGC_Space::new_gc_verify_no_live_marks();
          }
        )

        // Now mark all new objects as live
        set_mark_new_objects_live(true);

        // This needs to be inside the safepoint to ensure we don't allocate from a page without
        // counting page populations, but not marking the page as having invalid page pops.
        GPGC_Space::new_gc_clear_allocation_buffers();
        GPGC_Space::new_gc_set_marking_underway();
        if ( mark_old_space_roots() ) {
          GPGC_Space::old_gc_clear_allocation_buffers();
        }
      }

      {
        DetailTracer dt(&tdt, false, "N: Flush NewGen relocation: ");
        GPGC_Space::new_gc_flush_relocation(&dt);
      }

      // Have the mutators enable NMT traps and self-clean their stacks when this safepoint is over.
      _mutator_ref_sanity_check = GPGC_Collector::NoSanityCheck;

      enable_NMT_traps(&tdt);

      if ( mark_old_space_roots() ) {
        // Sync up with the OldCollector before starting to mark roots:
        DetailTracer dt(&tdt, false, "N: Synchronize with OldGC: start root marking"); 
        GPGC_Rendezvous::start_root_marking();
      }

      if ( ProfileAllocatedObjects ) {
        // Gather the per thread object allocation profiles and set them to null.
        // They will be accumulated concurrently and lazily reallocated.
        DetailTracer dt(&tdt, false, "N: Gather object allocation profiles");
        AllocatedObjects::gather(GPGC_Heap::new_gc_task_manager());
      }

      if ( GPGCVerifyCapacity ) {
        DetailTracer dt(&tdt, false, "N: Verify NewGen page capacity");
        GPGC_Space::verify_new_gen_capacity();
      }

      // Mark & Remap objects in roots.
      parallel_mark_roots(&tdt);

      if ( mark_old_space_roots() ) {
        // Sync up with the OldCollector before end the safepoint:
        DetailTracer dt(&tdt, false, "N: Synchronize with OldGC: end marking safepoint"); 
        GPGC_Rendezvous::end_marking_safepoint();
      }

      if ( PrintGCDetails ) {
        jlong  zero_max_ticks           = 0;
        size_t zero_max_tick_words      = 0;
        jlong  clone_max_ticks          = 0;
        size_t clone_max_tick_words     = 0;
        jlong  arraycopy_max_ticks      = 0;
        size_t arraycopy_max_tick_words = 0;

        {
          DetailTracer dt(&tdt, false, "N: collect obj init/clone/copy stats");

          for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
            jlong ticks = jt->get_obj_zero_max_ticks();
            if ( ticks > zero_max_ticks ) {
zero_max_ticks=ticks;
              zero_max_tick_words = jt->get_obj_zero_max_tick_words();
            }
            ticks = jt->get_obj_clone_max_ticks();
            if ( ticks > clone_max_ticks ) {
clone_max_ticks=ticks;
              clone_max_tick_words = jt->get_obj_clone_max_tick_words();
            }
            ticks = jt->get_arraycopy_max_ticks();
            if ( ticks > arraycopy_max_ticks ) {
arraycopy_max_ticks=ticks;
              arraycopy_max_tick_words = jt->get_arraycopy_max_tick_words();
            }

jt->reset_init_clone_copy_stats();
          }
        }

        double frequency               = double(os::elapsed_frequency());
        double zero_millis             = double(zero_max_ticks) * 1000.0 / frequency;
        double zero_words_per_sec      = double(zero_max_tick_words) / (zero_millis / 1000.0);
        double clone_millis            = double(clone_max_ticks) * 1000.0 / frequency;
        double clone_words_per_sec     = double(clone_max_tick_words) / (clone_millis / 1000.0);
        double arraycopy_millis        = double(arraycopy_max_ticks) * 1000.0 / frequency;
        double arraycopy_words_per_sec = double(arraycopy_max_tick_words) / (arraycopy_millis / 1000.0);

        GCLogMessage::log_a(tdt.details(), tdt.stream(), true,
"N: obj init %.7f ms for %d words %.3f words/sec",
                            zero_millis, zero_max_tick_words, zero_words_per_sec);
        GCLogMessage::log_a(tdt.details(), tdt.stream(), true,
"N: obj clone %.7f ms for %d words %.3f words/sec",
                            clone_millis, clone_max_tick_words, clone_words_per_sec);
        GCLogMessage::log_a(tdt.details(), tdt.stream(), true,
"N: arraycopy %.7f ms for %d words %.3f words/sec",
                            arraycopy_millis, arraycopy_max_tick_words, arraycopy_words_per_sec);
      }
    }

    // The collection state needs to be changed before we end the safepoint, to avoid a race.
    set_collection_state(ConcurrentMarking);

    if (_end_1st_mark_remap_safepoint ) {
      GPGC_Safepoint::end(&tdt);
      cycle_stats()->end_pause1(tdt.pause_ticks());
    } else {
      cycle_stats()->end_pause1(0);
    }

    // Since OldGC coordinates the first safepoint with the NewGC we
    // end the safepoint for OldGC here as well.
    // We should call OldGC end_pause1 only once for each old cycle.
    // So we record it only during the NewToOld cycle.
    if ( mark_old_space_roots() ) {
      GPGC_Heap::heap()->gc_stats()->old_gc_cycle_stats()->end_pause1(tdt.pause_ticks());
    }
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("New GC initial mark tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent section of mark/remap phase.
void GPGC_NewCollector::mark_remap_concurrent(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 4096);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent mark",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    // Concurrently mark/remap objects in all generations:
    parallel_concurrent_mark(&tdt);

    {
      DetailTracer dt(&tdt, false, "N: Acquiring NewGC_ConcurrentRefProcessing_lock");

      // Grab this lock to prevent JVMTI from looking up fields directly
      // without going through proper accessor functions during ConcurrentRefProcessing.
      NewGC_ConcurrentRefProcessing_lock.lock_can_block_gc(Thread::current());
      GET_RPC;
      NewGC_ConcurrentRefProcessing_lock._rpc = RPC;
    }

  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("New GC concurrent mark tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Phase change safepoint from concurrent marking to concurrent ref processing.
void GPGC_NewCollector::mark_remap_safepoint2(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%db: ref processing pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_new_cycle,
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
gclog_or_tty->print_cr("New GC weak ref safepoint tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent ref processing section of mark/remap phase.
void GPGC_NewCollector::mark_remap_concurrent_ref_process(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent ref processing",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    // Parallel concurrent handling of soft/weak/final/phantom/jni refs.
    parallel_concurrent_ref_process(&tdt);

NewGC_ConcurrentRefProcessing_lock.unlock();
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("New GC concurrent ref processing tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Final safepoint of mark/remap phase.
void GPGC_NewCollector::mark_remap_safepoint3(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%dc: final mark pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                         label, _current_total_cycle, _current_new_cycle,
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
      DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("3"); )
      DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("3"); )

      // There shouldn't be any NewGen mutator refs.
      long new_gen_refs, old_gen_refs;
      DEBUG_ONLY( GPGC_Collector::flush_mutator_ref_buffers(&tdt, "N: FMSP", new_gen_refs, old_gen_refs); )
      assert0(new_gen_refs == 0);

      // Final mark/remap mop up:
      parallel_final_mark(&tdt);

      // Disable NMT & TLB trapping
      _mutator_ref_sanity_check = GPGC_Collector::NoMutatorRefs;
      disable_NMT_traps(&tdt);

      DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("5"); )
      DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("5"); )
      DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_mutator_list_is_empty("5"); )
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
gclog_or_tty->print_cr("New GC final mark tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent mark/remap of weak roots
void GPGC_NewCollector::mark_remap_concurrent_weak_roots(GPGC_Heap* heap, const char* label)
{
  // No concurrent weak roots for NewGen GC
}


void GPGC_NewCollector::pre_mark_remap_cleanup(GPGC_Heap* heap, const char* label)
{
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
  TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: pre mark cleanup",
                       label, _current_total_cycle, _current_new_cycle);
m.enter();

  if ( mark_old_space_roots() ) {
    {
      DetailTracer dt(&tdt, false, "N: Flush OldGC roots");
      GPGC_GCManagerNewStrong::flush_nto_marking_stacks();
    }
    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewToOld_RootsFlushed, "N:");
  }

  GPGC_GCManagerNewStrong::ensure_all_nto_stacks_are_empty("6");

  if (GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection()) {
    guarantee(GPGCNoRelocation,          "GPGCNoRelocation must be true for a jvmti iterate collection");
    guarantee(_end_mark_phase_safepoint, "Heap iterate safepoint assumptions didn't hold");
  }
  if (GPGCNoRelocation) {
    // guarantee(_end_mark_phase_safepoint, "GPGCNoRelocation only supported with PGCSafepointMark");

    // I think if we're running concurrent mark + GPGCNoRelocation, it's OK to
    // disable new object live marking concurrently:
    set_mark_new_objects_live(false);
  }

  if (_end_mark_phase_safepoint) {
    // The prepare function will ensure that both collectors are ready to end the safepoint.
    // The new collector then ends the safepoint.
    // The old collector has to wait until the new collector has ended the safepoint
    if ( mark_old_space_roots() ) { GPGC_Rendezvous::end_coordinated_safepoint_prepare(); }

    {
      DetailTracer dt(&tdt, false, "N: End mark phase safepoint");

      GPGC_Safepoint::end(&tdt);

      cycle_stats()->end_pause1();
      GPGC_Heap::heap()->gc_stats()->old_gc_cycle_stats()->end_pause1();
    }

    if ( mark_old_space_roots() ) { GPGC_Rendezvous::end_coordinated_safepoint(); }
  }
}


// Final mark/remap cleanup
void GPGC_NewCollector::mark_remap_cleanup(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: final mark cleanup",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    set_collection_state(MarkingCleanup);

    marking_cleanup();

    {
      DetailTracer dt(&tdt, false, "N: ");
      long released_pages = GPGC_Space::new_gc_release_relocated_pages();
      dt.print("Release %lld relocated pages (%lld PagePops)", released_pages, page_pops()->total_pops_to_relocate());
      assert(released_pages == (long)page_pops()->total_pops_to_relocate(), "Released page count mismatch");
      GPGC_PageRelocation::new_gc_reset_relocation_space();
    }

    {
      DetailTracer dt(&tdt, false, "N: Release %d pages in %u relocated blocks",
                      page_pops()->large_space_to_old()->block_pages_total(), page_pops()->large_space_to_old()->max_cursor());

      GPGC_PopulationArray* large_space_to_old = page_pops()->large_space_to_old();
      for ( uint64_t i=0; i<large_space_to_old->max_cursor(); i++ ) {
        PageNum block = large_space_to_old->page(i);
        GPGC_Space::new_gc_release_relocated_block(block);
      }
    }

    {
      DetailTracer dt(&tdt, false, "N: Free ref buffers: " );
      long mutator_free = GPGC_GCManagerMark::count_free_mutator_ref_buffers();
      long strong_free  = GPGC_GCManagerNewStrong::count_free_ref_buffers();
      long final_free   = GPGC_GCManagerNewFinal::count_free_ref_buffers();

      dt.print("%lld mutator, %lld strong, %lld final", mutator_free, strong_free, final_free);
    }

    if ( ProfileLiveObjects ) {
      DetailTracer dt(&tdt, false, "N: Sum per thread object marking stats");

      LiveObjects *objs = LiveObjects::sum_new_gc_marked(GPGC_Heap::new_gc_task_manager());

#ifdef ASSERT
      uint64_t profile_count = 0, profile_words = 0;
      objs->total(&profile_count, &profile_words);

      uint64_t page_info_obj_count = 0, page_info_word_count = 0;
      GPGC_Space::new_gc_sum_raw_stats(&page_info_obj_count, &page_info_word_count);

      assert((profile_count == page_info_obj_count) && (profile_words == page_info_word_count),
"GPGC NewGC live object profile verification failed");
#endif
    }

    tdt.end_log(""   SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }
}


// Concurrent relocation setup
void GPGC_NewCollector::relocation_setup(GPGC_Heap* heap, const char* label)
{
  {
    cycle_stats()->heap_at_page_sort(heap->used_in_words());

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty, 2048);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: concurrent relocation setup",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    set_collection_state(RelocationSetup);

    // During relocation, we do not want to be taking GCLdValueTr traps.
Thread*current=Thread::current();
    current->set_gc_mode(true);
    // TODO: make it so getting a trap on a GC task thread is now an error.  We used to set the trap vector to the invalid_trap_table.
    GPGC_Heap::new_gc_task_manager()->request_new_gc_mode(true);

    if ( GPGCVerifyHeap ) {
      // There's a race when the heap verifier is running which can cause a crash when
      // new_gc_collect_sparse_populations() deallocates a completely empty page.  So keep
      // the verifier from running during page pop gathering.
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::VerifyingMarking, "N:");
    }

    // Collect page populations, sort pages by population stats, select pages to relocate
    {
      DetailTracer dt(&tdt, false, "N: Sparse pages: ");

      // NOTE: LVB traps must still be enabled here! Otherwise mutators might not be marking their
      // objects as live!
      // TODO: maw: Need to have a way to assert this!

      _promotion_threshold_time = cycle_stats()->cycle_start_tick() - GPGC_Heap::gpgc_time_stamp_promotion_threshold();

      page_pops()->reset_populations();
relocation_spike()->reset();
      GPGC_Space::new_gc_collect_sparse_populations(_promotion_threshold_time, page_pops());

      dt.print("Found garbage: "UINT64_FORMAT" MB empty, "UINT64_FORMAT" MB sparse, "UINT64_FORMAT" MB selected; "
               "Found fragged: "UINT64_FORMAT" MB, "UINT64_FORMAT" MB selected",
               (page_pops()->total_released()      << LogBytesPerGPGCPage)/M,
               (page_pops()->dead_words_found()    << LogBytesPerWord)/M,
               (page_pops()->dead_words_selected() << LogBytesPerWord)/M,
               (page_pops()->frag_words_found()    << LogBytesPerWord)/M,
               (page_pops()->frag_words_selected() << LogBytesPerWord)/M);
    }

    if ( GPGCVerifyHeap ) {
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::VerifyingMarking, "N:");
    }

    // Initialize sideband forwarding arrays.
    prepare_sideband_forwarding(&tdt);

    // Always take the BatchedMemoryOps interlock, so the two collectors don't fight over who
    // is updating the read barrier trap array.
    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "N:");

    update_page_relocation_states(&tdt);
    GPGC_Collector::setup_read_trap_array(&tdt, GPGC_Collector::NewCollector);

    if ( BatchedMemoryOps ) {
      // With azmem batched memory operations, we issue all the relocation virtual memory calls
      // in a batch prior to the safepoint, and then commit them inside the relocation safepoint.
      remap_memory(&tdt);
    }

    GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "N:");

    {
      DetailTracer dt(&tdt, false, "N: Start GC_locker concurrent_gc");
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
gclog_or_tty->print_cr("New GC concurrent relocation setup tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Initial safepoint of relocation phase.
void GPGC_NewCollector::relocation_safepoint1(GPGC_Heap* heap, const char* label)
{
  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%dd: relocation pause",
                         label, _current_total_cycle, _current_new_cycle);
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
        GPGC_Collector::commit_batched_memory_ops(&tdt, GPGC_Collector::NewCollector);
      } else {
        // If we don't have batched memory ops, then we must do relocation page remaps inside the safepoint:
        remap_memory(&tdt);

        // Swap in new read barrier trap array to enable new trap state:
        GPGC_ReadTrapArray::swap_readbarrier_arrays(&tdt, GPGC_Collector::NewCollector);
      }

      // Tell threads to self-clean their stacks
      GPGC_ThreadCleaner::enable_thread_self_cleaning(&tdt, JavaThread::gpgc_clean_new);
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
gclog_or_tty->print_cr("New GC initial relocate tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Concurrent finish of relocation phase.
void GPGC_NewCollector::relocation_concurrent_new(GPGC_Heap* heap, const char* label)
{
  {
    size_t prev_used = heap->used();

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(), "%s GC cycle %d-%d: concurrent relocation",
                         label, _current_total_cycle, _current_new_cycle);
m.enter();

    if ( BatchedMemoryOps ) {
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "N:");
    }

    {
      DetailTracer dt(&tdt, false, "N: End GC_locker concurrent_gc");
      // Release the GC_locker concurrent_gc lock, so JNI threads can get into critical sections again.
      GPGC_Rendezvous::end_gc_locker_concurrent();
    }

    GPGC_Space::new_gc_stop_marking();

    {
      DetailTracer dt(&tdt, false, "N: Complete %ld mid space source pages",
                                   page_pops()->mid_space_to_new()->max_cursor() + page_pops()->mid_space_to_old()->max_cursor());

      // Release the remaining memory at the source address of each relocated mid space page.
      GPGC_Space::remapped_pages(page_pops()->mid_space_to_new());
      GPGC_Space::remapped_pages(page_pops()->mid_space_to_old());
    }

    if ( PageHealing ) {
      if ( GPGCDiagHealDelayMillis>0 ) {
        DetailTracer dt(&tdt, false, "N: Diagnostic delay before mid space page healing");
os::sleep(Thread::current(),GPGCDiagHealDelayMillis,false);
      }

      {
        DetailTracer dt(&tdt, false, "N: Unshatter %ld mid space target pages",
                                     page_pops()->mid_space_targets()->current_length());

        // Form parallel tasks to go through each mid space page and force healing.
PGCTaskQueue*q=PGCTaskQueue::create();

        for ( int64_t i=0; i<int64_t(GenPauselessNewThreads); i++ ) {
q->enqueue(new GPGC_NewGC_HealMidPagesTask(i));
        }

        GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
      }
    }

    {
      DetailTracer dt(&tdt, false, "N: Relocate new ");

      assert0(page_pops()->small_space_to_new()->cursor() == 0);
      assert0(page_pops()->small_space_to_old()->cursor() == 0);
      assert0(page_pops()->mid_space_to_new()->cursor()   == 0);
      assert0(page_pops()->mid_space_to_old()->cursor()   == 0);

      long work_unit = (GenPauselessNewThreads / 64) + 1;

      GPGC_GCManagerNewReloc::set_generations();

PGCTaskQueue*q=PGCTaskQueue::create();

for(uint i=0;i<GenPauselessNewThreads;i++){
q->enqueue(new GPGC_NewGC_RelocateSmallPagesTask(work_unit));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);

      long target_new_pages = 0;
      long target_old_pages = 0;
      GPGC_GCManagerNewReloc::total_page_counts(&target_new_pages, &target_old_pages);

      page_pops()->atomic_increment_target_pages_in_new_gen(target_new_pages);
      page_pops()->atomic_increment_target_pages_in_old_gen(target_old_pages);

      long new_gen_mutator_pages = GPGC_Space::new_gen_relocation_page_count();
      long old_gen_mutator_pages = GPGC_Space::old_gen_promotion_page_count();

      page_pops()->atomic_increment_target_pages_in_new_gen(new_gen_mutator_pages);
      page_pops()->atomic_increment_target_pages_in_old_gen(old_gen_mutator_pages);

dt.print("pages: promotion: %ld -> %ld (%ld); survival %ld -> %ld (%ld)",
               page_pops()->source_pages_to_old_gen(), page_pops()->target_pages_in_old_gen(), old_gen_mutator_pages,
               page_pops()->source_pages_to_new_gen(), page_pops()->target_pages_in_new_gen(), new_gen_mutator_pages );
    }

    if ( ProfileLiveObjects ) {
      DetailTracer dt(&tdt, false, "N: Sum per thread object promotion stats");
      LiveObjects::sum_promoted(GPGC_Heap::new_gc_task_manager());
    }

    {
      DetailTracer dt(&tdt, false, "N: Card-mark promoted objects");
 
      long work_unit = (GenPauselessNewThreads / 64) + 1;
      long tasks = GenPauselessNewThreads;

      page_pops()->small_space_to_old()->reset_cursor();
      page_pops()->mid_space_to_old()->reset_cursor();

PGCTaskQueue*q=PGCTaskQueue::create();

      for ( uint64_t i=0; i<page_pops()->large_space_to_old()->max_cursor(); i++ ) {
        PageNum block = page_pops()->large_space_to_old()->page(i);
q->enqueue(new GPGC_NewGC_CardMarkBlockTask(block));
      }

      for ( long i=0; i<tasks; i++ ) {
q->enqueue(new GPGC_NewGC_CardMarkPagesTask(work_unit));
      }

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
      page_pops()->large_space_to_old()->reset_cursor();
    }

    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "N:");
    GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::OldGC_RelocatePerm, "N:");

if(PageHealing&&PrintGCDetails){
SafepointTimes times;

      {
        DetailTracer dt(&tdt, false, "N: Mutator unshattered page traps: ");

        GPGC_UnshatteredPageTrapCountClosure uptcc;
        GPGC_Safepoint::do_checkpoint(&uptcc, &times);

dt.print(" %ld threads, %ld total traps, %ld max single thread traps",
                 uptcc.get_threads(),
                 uptcc.get_total_traps(),
                 uptcc.get_max_single_thread_traps());
      }

      log_checkpoint_times(&tdt, &times, "N: CRN", "page_traps");
    }

    tdt.end_log(     SIZE_FORMAT "M"
"->"SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                prev_used/M, heap->used()/M, heap->capacity()/M);
  }

  if (PrintGCTaskTimeStamps) {
gclog_or_tty->print_cr("New GC concurrent relocation tasks:");
    GPGC_Heap::new_gc_task_manager()->print_task_time_stamps();
  }
}


// Parallel mark/remap of roots.
void GPGC_NewCollector::parallel_mark_roots(TimeDetailTracer* tdt)
{
  // TODO: maw: figure out ref processing

  DetailTracer dt(tdt, false, "N: Root marking");

  ResourceMark rm;
  HandleMark   hm;

  // TODO: assert work queues are empty

assert(GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");
  assert0(!jlr_handler()->is_capture_on());
  assert0(jlr_handler()->none_captured());
  jlr_handler()->set_jlr_capture(true);
  jlr_handler()->mark_roots(GPGC_GCManagerNewStrong::get_manager(0));
  GPGC_GCManagerNewStrong::reset_reference_lists();

PGCTaskQueue*q=PGCTaskQueue::create();

  // FIX ME! We should have a NoResourceMarkVerifier here!

  // We scan the vmThread roots.  The JavaThread roots are self cleaned.
q->enqueue(new GPGC_NewGC_VMThreadMarkTask());

  // These root tasks mark for both NewGC and OldGC during NewToOld cycles:
q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::universe));
q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::jni_handles));
  q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::object_synchronizer));
  q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::jvmti));
  q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::management));

  // These roots mark for OldGC:
q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::vm_symbols));

  // NOTE! ArtaObjects are strong roots for NewGen GC, even though they're weak roots for OldGen GC.
  q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::arta_objects));

  // Wait for the tasks to be completed.
  GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
}


// Parallel concurrent mark/remap of objects.
void GPGC_NewCollector::parallel_concurrent_mark(TimeDetailTracer* tdt)
{
  ResourceMark rm;
  HandleMark   hm;

  if ( BatchedMemoryOps ) {
    GPGC_Interlock::release_interlock(tdt, GPGC_Interlock::BatchedMemoryOps, "N:");
  }

  if ( ProfileAllocatedObjects ) {
    DetailTracer dt(tdt, false, "N: Sum per thread object allocation stats");
    AllocatedObjects::sum(GPGC_Heap::new_gc_task_manager());
  }

  // concurrent root mark
  {
    GPGC_Interlock interlock(tdt, GPGC_Interlock::NewGC_SystemDictionaryMarking, "N:");

    DetailTracer dt(tdt, false, "N: Concurrent system-dictionary marking");

PGCTaskQueue*q=PGCTaskQueue::create();
    // These roots mark just for NewGC:
    q->enqueue(new GPGC_NewGC_MarkRootsTask(GPGC_NewGC_MarkRootsTask::system_dictionary));
    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
  }

  {
    GPGC_Interlock interlock(tdt, GPGC_Interlock::NewGC_CardMarkScan, "N:");

    {
      DetailTracer dt(tdt, false, "N: Suspend OldGen relocation");
      GPGC_Rendezvous::request_suspend_relocation();
    }

    {
      DetailTracer dt(tdt, false, "N: Card mark scan");

PGCTaskQueue*q=PGCTaskQueue::create();

      GPGC_Space::make_cardmark_tasks(q);

      // Wait for the tasks to be completed.
      GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);

      GPGC_Rendezvous::verify_no_relocating_threads();
    }

    GPGC_Rendezvous::resume_relocation();
  }

  mark_all_discovered(tdt, NewCollector);

  // Make sure all thread stacks have been swept.
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(0);
  GPGC_ThreadCleaner::new_gen_checkpoint_and_clean_threads(tdt, gcm, "N: CM");
  mark_all_discovered(tdt, NewCollector);
  
  // Concurrent marking continues until JavaThread ref buffers are flushed with nothing found,
  // and the FinalLive marking stacks are empty.
  while (true) {
    long new_gen_refs, old_gen_refs;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "N: CM", new_gen_refs, old_gen_refs);
    if ( new_gen_refs != 0 ) {
      mark_all_discovered(tdt, NewCollector);
      continue;
    }

    if ( ! GPGC_GCManagerNewFinal::all_stacks_are_empty() ) {
      mark_all_final_discovered(tdt, NewCollector);
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
      DetailTracer dt(tdt, false, "N: acquire java.lang.ref pending list lock");
      jlr_handler()->acquire_pending_list_lock();
    }
  }
}


// Flush and mark any remaining refs, so the marking phase can be changed
void GPGC_NewCollector::parallel_flush_and_mark(TimeDetailTracer* tdt)
{
  {
    // Flush any buffered refs from GCLdValueNMTTr traps in JavaThreads
    long new_gen_refs, old_gen_refs;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "N: RPSP", new_gen_refs, old_gen_refs);
  }

  if ( GPGCVerifyThreadStacks ) {
    // The VMThread should have self scanned at the end of the initial mark safepoint.
    // C2 shouldn't schedule LVBs across safepoints.  Verify no un-NMTed refs in thread stacks.
    DetailTracer dt(tdt, false, "N: Verify clean thread stacks");
    GPGC_NewGC_VerifyMarkedLiveClosure vmlc;
    VMThread::vm_thread()->oops_do(&vmlc);
Threads::oops_do(&vmlc);
  }

  {
    // TODO: maw: Instead of completing marking in the safepoint, we might want to
    // bail out of the safepoint, mark concurrently, and then retry the safepoint.

    // We try and mark without checking to see if the prior flush turned up anything,
    // as there's a race with JavaThreads popping something onto the stack just before
    // the safepoint happens.  There may be a race with the other GC flushing as well.
    GPGC_Collector::mark_all_discovered(tdt, NewCollector);
    GPGC_Collector::mark_all_final_discovered(tdt, NewCollector);
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

  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("7"); )
  DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("7"); )
}


// Parallel concurrent handling of soft/weak/final/phantom/jni refs.
void GPGC_NewCollector::parallel_concurrent_ref_process(TimeDetailTracer* tdt)
{
  {
    DetailTracer dt(tdt, false, "N: JLR handling setup");
    // Can't assert mutator marking stacks are empty, because there's a race with mutators looking up JNI weak refs.
    // Can't assert NTO marking stacks are empty, because we haven't flushed them to OldGC yet.
    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("8"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("8"); )
    GPGC_GCManagerNewStrong::save_reference_lists();
    jlr_handler()->set_jlr_capture(false);
  }

  {
    DetailTracer dt(tdt, false, "N: Parallel soft refs");
    // Clear soft referents not marked strong live, or update referent objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_soft_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("9"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("9"); )
  }
  {
    DetailTracer dt(tdt, false, "N: Parallel weak refs");
    // Clear weak referants not marked strong live, or update referent objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_weak_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("10"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("10"); )
  }
  {
    DetailTracer dt(tdt, false, "N: Parallel final refs");
    // Enqueue final refs whose referents are not marked strong live, which will have not
    // been previously put on the pending list by the collector.  No marking through yet.
    jlr_handler()->do_final_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("11"); )
  }

  // Mark strong live through the final referents.  We may have to try multiple times, as we're racing with
  // mutators that may be trapping on refs looked up through JNI weak references.  When a mutator beats GC to
  // an objectRef, it updates the NMT, which halts the GC threads traversal.  Traversal is resumed when we
  // flush the mutator ref buffers and continue traversing from what is found there.
  long new_gen_refs_flushed;
  do {
    mark_all_discovered(tdt, NewCollector);

    long old_gen_refs_flushed;
    GPGC_Collector::flush_mutator_ref_buffers(tdt, "N: CRP", new_gen_refs_flushed, old_gen_refs_flushed);
  } while (new_gen_refs_flushed != 0);

  
  { 
    DetailTracer dt(tdt, false, "N: JNI weak refs");
    // Clear JNI weak refs not marked strong live, or update ref objectRef with valid
    // remapped and NMT objectRef.  No marking.
    jlr_handler()->do_jni_weak_refs();
    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("13"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("13"); )
  }
  
SafepointTimes times;

  {
    // Before we can safely change to NoMutatorRefs sanity check mode, we need to make
    // sure all JavaThreads have passed through any pending LVBs.  We also can't call
    // final_clear_mutator_ref_buffers without a checkpoint to ensure there's no race
    // with a mutator finishing an LVB.
    //
    // TODO: We could eliminate this checkpoint if the final_clear_mutator_ref_buffers()
    // was moved to after the next safepoint, the way GPGC_OldCollector does it.
    DetailTracer dt(tdt, false, "N: NOOP checkpoint");

    NopThreadClosure ntc;
    GPGC_Safepoint::do_checkpoint(&ntc, &times);

    // From now until the end of concurrent ref processing, we expect to find no more refs from the mutators.
    _mutator_ref_sanity_check = GPGC_Collector::NoMutatorRefs;

    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("12"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("12"); )
  }

  log_checkpoint_times(tdt, &times, "N: CRP", "NOOP");

  final_clear_mutator_ref_buffers(tdt, NewCollector, "N: CRP");

  {
    DetailTracer dt(tdt, false, "N: Parallel phantom refs");
    // Enqueue phantom refs not previously pending whose referents are not marked strong
    // live.  Push referent objectRef addresses to the marking stack for later metadata
    // update and strong live marking through.
    jlr_handler()->do_phantom_refs_parallel();
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("14"); )
  }
  {
    // Now update phantom referent objectRef metadata and do a strong live mark traversal.
    mark_all_discovered(tdt, NewCollector);
    DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("15"); )
    DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("15"); )
  }

  GPGC_GCManagerNewStrong::verify_empty_ref_lists();
  guarantee(!jlr_handler()->is_capture_on(), "NewGC JLR capture not off");
  guarantee(jlr_handler()->none_captured(), "NewGC JLR handler has captured refs");

#ifdef ASSERT
  bool is_heap_iterate_collection = GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection();
  if (is_heap_iterate_collection) { guarantee(!_end_2nd_mark_remap_safepoint, "heap iterate safepoint assumptions didn't hold"); }
#endif // ASSERT

  if (_end_2nd_mark_remap_safepoint) {
    jlr_handler()->release_pending_list_lock();
  }
}


// Parallel final mark/remap of things needing a safepoint.
void GPGC_NewCollector::parallel_final_mark(TimeDetailTracer* tdt)
{
assert(GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");

  ResourceMark rm;
  HandleMark   hm;

  // No more mutator ref sanity checking needed.
  _mutator_ref_sanity_check = GPGC_Collector::NoMutatorRefs;

  if ( GPGCVerifyThreadStacks ) {
    // This option verifies that we haven't missed an oop that hasn't yet been seen by the collector.
    DetailTracer dt(tdt, false, "N: SP3: Verify clean thread stacks");
    GPGC_NewGC_VerifyMarkedLiveClosure vmlc;
    VMThread::vm_thread()->oops_do(&vmlc);
Threads::oops_do(&vmlc);
  }

  DEBUG_ONLY( GPGC_GCManagerNewStrong::ensure_all_stacks_are_empty("16"); )
  DEBUG_ONLY( GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty("16"); )
  GPGC_GCManagerNewStrong::verify_empty_ref_lists();
  guarantee(!jlr_handler()->is_capture_on(), "NewGC JLR capture not off");
  guarantee(jlr_handler()->none_captured(), "NewGC JLR handler has captured refs");
 
  // JVMTI object tagging is based on JNI weak refs. If any of these
  // refs were cleared then JVMTI needs to update its maps and
  // maybe post ObjectFrees to agents.
JvmtiExport::gpgc_ref_processing_epilogue();
}


// Parallel final mark/remap of things needing a safepoint.
void GPGC_NewCollector::parallel_concurrent_weak_marks(TimeDetailTracer* tdt)
{
  // Nothing to do for NewGen
}


// When we relocate live objects out of the sparse pages, we keep a sideband
// data structure to map old object locations to new object locations.  Set
// that up now for all the pages we plan to relocate.
void GPGC_NewCollector::prepare_sideband_forwarding(TimeDetailTracer* tdt)
{
  {
    GPGC_Interlock interlock(tdt, GPGC_Interlock::SidebandAlloc, "N:");

    {
      DetailTracer dt(tdt, false, "N: Serial sideband allocation: ");

      bool                  safe_to_iterate     = true;
      GPGC_PopulationArray* all_populations     = page_pops()->all_populations();
      uint64_t              cursor              = 0;
      uint64_t              max_cursor          = all_populations->max_cursor();

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
        assert0(mark_count == live_objs);
        assert0(info->just_gen() == GPGC_PageInfo::NewGen);

        if ( ! GPGC_PageRelocation::new_gc_get_sideband_space(info, live_objs) ) {
          // We're out of space for sideband forwarding arrays.
          // Cap the limit of relocating pages where we ran out of space.
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

        bool promote_page = (all_populations->time(cursor) < _promotion_threshold_time);

        if ( in_small_space ) {
          if ( promote_page ) {
            page_pops()->small_space_to_old()->add_page(all_populations->page_pop(cursor));
          } else {
            page_pops()->small_space_to_new()->add_page(all_populations->page_pop(cursor));
          }
        } else {
          if ( promote_page ) {
            page_pops()->mid_space_to_old()->add_page(all_populations->page_pop(cursor));
          } else {
            page_pops()->mid_space_to_new()->add_page(all_populations->page_pop(cursor));
          }
        }
      }

      GPGC_Heap::set_safe_to_iterate(safe_to_iterate);

      page_pops()->small_space_to_new()->reset_max_cursor();
      page_pops()->small_space_to_old()->reset_max_cursor();
      page_pops()->mid_space_to_new()->reset_max_cursor();
      page_pops()->mid_space_to_old()->reset_max_cursor();

      page_pops()->small_space_skipped_pages()->reset_max_cursor();
      page_pops()->mid_space_skipped_pages()->reset_max_cursor();

      if ( ! GPGCCollectMidSpace ) {
        page_pops()->mid_space_to_new()->reset();
        page_pops()->mid_space_to_old()->reset();
      }

      if ( ! GPGCCollectLargeSpace ) {
        page_pops()->large_space_to_new()->reset();
        page_pops()->large_space_to_old()->reset();
      }
    }
  }

  {
    DetailTracer dt(tdt, false, "N: Relocation array setup: ");


    // TODO: these stats are bogus, should be counting number of allocated 2MB pages selected for relocation
    // in mid space, not number of mid space blocks.
    page_pops()->set_source_pages_to_new_gen( page_pops()->small_space_to_new()->max_cursor() + page_pops()->mid_space_to_new()->max_cursor() );
    page_pops()->set_source_pages_to_old_gen( page_pops()->small_space_to_old()->max_cursor() + page_pops()->mid_space_to_old()->max_cursor() );

dt.print(" %ld MB new, %ld MB old, %ld MB empty",
             GPGC_PageRelocation::new_sideband_bytes_in_use() / M,
             GPGC_PageRelocation::old_sideband_bytes_in_use() / M,
             GPGC_PageRelocation::sideband_bytes_free() / M);
  }

  {
    DetailTracer dt(tdt, false, "N: Sort survivor pages");

    // TODO: We sort the pages by time, but we also need to chunk up the relocation work so that
    //       pages of the same time are relocated together.  Relocation stripes should be divided
    //       by the number of live words.
    page_pops()->small_space_to_new()->sort_by_largest_time_first();
    page_pops()->mid_space_to_new()->sort_by_largest_time_first();
  }

  {
    DetailTracer dt(tdt, false, "N: Parallel forwarding hash init: %d source pages",
                    page_pops()->small_space_to_new()->max_cursor() +
                    page_pops()->small_space_to_old()->max_cursor() +
                    page_pops()->mid_space_to_new()->max_cursor() +
                    page_pops()->mid_space_to_old()->max_cursor());

    assert0(page_pops()->small_space_to_new()->cursor() == 0);
    assert0(page_pops()->small_space_to_old()->cursor() == 0);
    assert0(page_pops()->mid_space_to_new()->cursor()   == 0);
    assert0(page_pops()->mid_space_to_old()->cursor()   == 0);

    long work_unit = (GenPauselessNewThreads / 64) + 1;

PGCTaskQueue*q=PGCTaskQueue::create();

for(uint32_t i=0;i<GenPauselessNewThreads;i++){
q->enqueue(new GPGC_NewGC_SidebandForwardingInitTask(work_unit));
    }

    // Wait for the tasks to be completed.
    GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);

    page_pops()->small_space_to_new()->reset_cursor();
    page_pops()->small_space_to_old()->reset_cursor();
    page_pops()->mid_space_to_new()->reset_cursor();
    page_pops()->mid_space_to_old()->reset_cursor();
  }

  // The sideband forwarding structures are now ready for LdValueTr traps to occur.
}
