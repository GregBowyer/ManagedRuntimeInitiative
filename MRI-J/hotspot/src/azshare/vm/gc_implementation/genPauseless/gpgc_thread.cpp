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


#include "exceptions.hpp"
#include "gcCause.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_heuristic.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_operation.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_rendezvous.hpp"
#include "gpgc_stats.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_traps.hpp"
#include "init.hpp"
#include "java.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "log.hpp"
#include "memoryService.hpp"
#include "mutexLocker.hpp"
#include "orderAccess.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "surrogateLockerThread.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"
#include "universe.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gctrap_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

GPGC_Thread*         GPGC_Thread::_new_gc_thread                = NULL;
GPGC_Thread*         GPGC_Thread::_old_gc_thread                = NULL;
bool GPGC_Thread::_should_terminate=false;
bool                 GPGC_Thread::_could_be_running_constant_gc = false;
GPGC_Cycle*          GPGC_Thread::_full_gc_in_progress          = NULL;
GPGC_Cycle*          GPGC_Thread::_full_gc_pending              = NULL;


GPGC_Cycle::GPGC_Cycle()
{
  _cycle_op_queue = new GPGC_OperationQueue();
  _alloc_op_queue = new GPGC_OperationQueue();

  reset();
}


void GPGC_Cycle::reset()
{
  _gc_cycle                 = false;
  _clear_all                = false;
  _cause                    = GCCause::_no_gc;
_safepoint_end_callback=NULL;
_user_data=NULL;
  _heap_iterate_collection = false;
}


void GPGC_Cycle::request_full_gc(GCCause::Cause cause, bool clear_all)
{
assert_lock_strong(PauselessGC_lock);

  if ( clear_all ) { _clear_all = true; }

  _gc_cycle = true;
  _cause    = cause;
}


void GPGC_Cycle::cycle_complete()
{
assert_lock_strong(PauselessGC_lock);
  assert0(Thread::current()==GPGC_Thread::old_gc_thread());
  assert0(_gc_cycle==true);

  reset();

  assert0(cycle_op_queue()->is_empty());
  assert0(alloc_op_queue()->is_empty());
}


void GPGC_Cycle::set_alloc_op_queue(GPGC_OperationQueue* q)
{
assert_lock_strong(PauselessGC_lock);

_alloc_op_queue=q;
}


bool GPGC_Thread::should_run_full_gc()
{
assert_lock_strong(PauselessGC_lock);
  assert0(Thread::current()==GPGC_Thread::new_gc_thread());

  if ( full_gc_in_progress()->gc_cycle() || !full_gc_pending()->gc_cycle() ) {
    return false;
  }

  assert0(full_gc_in_progress()->cycle_op_queue()->is_empty());
  assert0(full_gc_in_progress()->alloc_op_queue()->is_empty());

  GPGC_Cycle* cycle_swap = _full_gc_in_progress;
  _full_gc_in_progress   = _full_gc_pending;
  _full_gc_pending       = cycle_swap;

full_gc_pending()->reset();

  return true;
}


void GPGC_Thread::request_full_gc(GCCause::Cause cause, bool clear_all)
{
  full_gc_pending()->request_full_gc(cause, clear_all);
}


void GPGC_Thread::request_full_gc(GPGC_AllocOperation* alloc_op)
{
  full_gc_pending()->request_full_gc(GCCause::_allocation_failure, true);
  full_gc_pending()->alloc_op_queue()->add(alloc_op);
}


void GPGC_Thread::request_full_gc(GCCause::Cause cause, bool clear_all, GPGC_CycleOperation* cycle_op)
{
  full_gc_pending()->request_full_gc(cause, clear_all);
  full_gc_pending()->cycle_op_queue()->add(cycle_op);
  full_gc_pending()->set_safepoint_end_callback(cycle_op->safepoint_end_callback());
  full_gc_pending()->set_user_data(cycle_op->user_data());
}


void GPGC_Thread::start(long collector)
{
  if ( ! should_terminate() ) {
    GPGC_Thread* th = new (ttype::pgc_thread) GPGC_Thread(collector);
  }
}


void GPGC_Thread::stop_all()
{
  _should_terminate = true;

OrderAccess::release();

  {
    MutexLockerAllowGC mu(&Terminator_lock, 1);
  
    // Since stop() is called by the VM thread during a VM_Exit safepoint
    // the VM thread holds the Threads_lock at this point.
    // If the GPGC_Thread is starting a checkpoint during this time 
    // it will block on the Threads_lock so we use is_blocked() to 
    // coordinate with shutdown.

    GPGC_Thread* new_thread = NULL;
    GPGC_Thread* old_thread = NULL;

    // We loop with a timedwait to periodically check whether the gc threads
    // have blocked.
    while ( ((new_thread = new_gc_thread()) != NULL) && (!new_thread->is_blocked()) ) {
      Terminator_lock.wait_micros(20000L, false);
    }
    while ( ((old_thread = old_gc_thread()) != NULL) && (!old_thread->is_blocked()) ) {
      Terminator_lock.wait_micros(20000L, false);
    }
  }
}


void GPGC_Thread::block_if_vm_exited(){
  if ( is_new_collector() ) {
set_new_gc_thread(NULL);
  } else if ( is_old_collector() ) {
set_old_gc_thread(NULL);
  } else {
    ShouldNotReachHere();
  }

OrderAccess::fence();//Make sure gc thread is set to NULL and
                        // we see VMExit::_vm_exited

  // Currently we come in here only when should_terminate is true.
  // If _should_terminate is set, _vm_exited should have been set to true
  guarantee(VM_Exit::vm_exited() || !should_terminate(),
"GPGC_Thread cannot block without _vm_exited");

  // Wait at this point so we don't touch any memory until vm exit
  VM_Exit::block_if_vm_exited();

  if (should_terminate()) {
    ShouldNotReachHere();
  }
}


void GPGC_Thread::run_alloc_operation(GPGC_AllocOperation* op) {
  JavaThread* thread = JavaThread::current();

  thread->check_for_valid_safepoint_state(true);
  op->set_calling_thread(thread);

  long ticket = thread->vm_operation_ticket();

  {
MutexLockerAllowGC ml(PauselessGC_lock,thread);
request_full_gc(op);
PauselessGC_lock.notify();
  }

  GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Thread 0x%lx enqueued GPGC sync allocation request", thread);

  {     
MutexLockerAllowGC mu(VMOperationRequest_lock,thread);
while(thread->vm_operation_completed_count()<ticket){
VMOperationRequest_lock.wait();
    }
  }
}


void GPGC_Thread::run_sync_gc_cycle_operation(GCCause::Cause cause, bool clear_all, GPGC_CycleOperation* op) {
  JavaThread* thread = JavaThread::current();

  thread->check_for_valid_safepoint_state(true);
  op->set_calling_thread(thread);

  long ticket = thread->vm_operation_ticket();

  {
MutexLockerAllowGC ml(PauselessGC_lock,thread);
    request_full_gc(cause, clear_all, op);
PauselessGC_lock.notify();
  }

  GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Thread 0x%lx enqueued GPGC sync FullGC request", thread);

  {     
MutexLockerAllowGC mu(VMOperationRequest_lock,thread);
while(thread->vm_operation_completed_count()<ticket){
VMOperationRequest_lock.wait();
    }
  }
}


void GPGC_Thread::run_async_gc_cycle(GCCause::Cause cause, bool clear_all) {
  {
MutexLockerAllowGC ml(PauselessGC_lock,JavaThread::current());

    request_full_gc(cause, clear_all);

PauselessGC_lock.notify();
  }

  GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Thread 0x%lx requested GPGC async FullGC cycle", Thread::current());
}


void GPGC_Thread::heuristic_demands_full_gc() {
  // Request a FullGC regardless of what's currently running.
  request_full_gc(GCCause::_gc_heuristic, true);
}


void GPGC_Thread::heuristic_wants_full_gc(bool clear_all) {
  if ( ! full_gc_in_progress()->gc_cycle() ) {
    // Only listen to the heuristic's request for a FullGC if there isn't already one in progress.
    request_full_gc(GCCause::_gc_heuristic, clear_all);
  }
}


void GPGC_Thread::static_initialize() {
  ResourceMark rm;
  // Initialize gc timestamps, and the gc_stats last_cycle data, so we calculate
  // a reasonable intracycle allocation rate for the first GC cycle that occurs.
  TimeStamp* stamp = gclog_or_tty->get_time_stamp();
  if ( ! stamp->is_updated() ) {
    stamp->update();
  }

  GPGC_Heap::heap()->gc_stats()->init_stats();

  GPGC_Heuristic::start_allocation_model();

  _full_gc_in_progress = new GPGC_Cycle();
  _full_gc_pending     = new GPGC_Cycle();
}


GPGC_Thread::GPGC_Thread(long collector)
  : Thread()
{
  _collector = collector;

  assert0( UseGenPauselessGC );
  assert0( is_new_collector() || is_old_collector() );

  if ( is_new_collector() ) {
    set_new_gc_thread(this);
    _thread_number = GenPauselessNewThreads + GenPauselessOldThreads;
  } else {
    set_old_gc_thread(this);
    _thread_number = GenPauselessNewThreads + GenPauselessOldThreads + 1;
  }

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty, "creating %s GPGC_Thread...",
           is_new_collector()?"NewGC":"OldGC");

if(os::create_thread(this,ttype::gc_thread)){
os::set_native_priority(this,GCThreadPriority);
    if (!DisableStartThread) {
      os::start_thread(this);
    }
  } else {
vm_exit_during_initialization("Unable to create generational pauseless GC thread");
  }

  _preallocated_page = NoPage;
  GPGC_PageBudget::preallocate_page(this);
}


void GPGC_Thread::run(){
  this->initialize_thread_local_storage();
  this->set_gcthread_lvb_trap_vector();
  this->set_gc_mode(true);
  if ( is_new_collector() ) {
    this->set_vm_tag(VM_NewGPGC_tag);
  } else {
    this->set_vm_tag(VM_OldGPGC_tag);
  }
  this->set_active_handles(JNIHandleBlock::allocate_block());

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty, "%s GPGC_Thread started",
           is_new_collector()?"NewGC":"OldGC");

  // ExternalProfiler::record_thread_start(this);

assert(Thread::current()==this,"Being paranoid");

  // Wait for the VM boot up enough to start running garbage collection:
  {
    MutexLocker ml(PauselessGC_lock, this);

    // Wait until the Universe is fully initialized.
    while (!is_init_completed() && !Universe::is_fully_initialized() && !_should_terminate) {
      PauselessGC_lock.wait_micros(200000L, false);
    }

    if ( !_should_terminate ) {
      Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty, "%s GPGC_Thread waiting for surrogate",
               is_new_collector()?"NewGC":"OldGC");
    }
    
    // Wait until the SurrogateLockerThread is created.  Can't create it ourselves,
    // since one must be a JavaThread to kick it off.
    while (SurrogateLockerThread::slt() == NULL && !_should_terminate) {
      PauselessGC_lock.wait_micros(200000L, false);
    }
  }

  // Outside of holding PauselessGC_lock, make sure the VM didn't 
  // start to shut down while we were waiting to get going
  if ( GPGC_Thread::should_terminate() || VM_Exit::vm_exited() ) {
    GPGC_Thread::block_if_vm_exited();
  }

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty, "%s GPGC_Thread beginning loop",
           is_new_collector()?"NewGC":"OldGC");

  if ( is_new_collector() ) {
    // Loop doing garbage collections until we're told to shutdown:
    new_collector_loop();
  } else {
    old_collector_loop();
  }

  // Signal that it is terminated
  {
    MutexLocker mu(Terminator_lock, this);
    if ( is_new_collector() ) {
set_new_gc_thread(NULL);
    } else {
set_old_gc_thread(NULL);
    }
Terminator_lock.notify();
  }

  // ThreadLocalStorage::set_thread(NULL);.. no ThreadLocalStorage in x86

  // "delete this" will not have much impact.
  // These threads are not really recycled, as they do away only after JVM shutdown.
  delete this;
}


void GPGC_Thread::old_collector_loop() {
  Thread* thread = Thread::current();
  assert0(thread->is_GenPauselessGC_thread());
  GPGC_Thread* old_collector_thread = (GPGC_Thread*) thread;

  while ( ! should_terminate() ) {
    ResourceMark rm;

    // Wait to be signaled:
    GPGC_Rendezvous::wait_for_old_gc();

    // Did VM shutdown start while we were asleep?
    if ( should_terminate() ) return;

    // Do an OldGen GC if requested.
    if ( GPGC_OldCollector::collection_state() == GPGC_Collector::CollectionStarting ) {
      JvmtiGCFullMarker jgcm(full_gc_in_progress()->is_heap_iterate_collection());
      TraceMemoryManagerStats tms(true /* Full GC */);

      bool should_coordinate_gc_cycle_end = GPGC_Rendezvous::should_coordinate_gc_cycle_end();

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
      assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

      Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: Begin OldGC iteration");

      GPGC_CycleStats* cycle_stats = heap->gc_stats()->old_gc_cycle_stats();

cycle_stats->reset();
      cycle_stats->start_cycle();

      GPGC_OldCollector::collect("Old", full_gc_in_progress()->clear_all(), cycle_stats);

      cycle_stats->end_cycle();

      Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: end OldGC iteration");
      Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
               "Heap size: capacity " INT64_FORMAT "KB, used " INT64_FORMAT "KB",
               heap->capacity()/K,
               heap->used()/K);

      GPGC_OperationQueue* in_progress_alloc_queue    = NULL;
      GPGC_OperationQueue* pending_alloc_queue        = NULL;
      GPGC_OperationQueue* failed_pending_alloc_queue = NULL;
      {
MutexLocker ml(PauselessGC_lock,thread);

        in_progress_alloc_queue = full_gc_in_progress()->alloc_op_queue();
        pending_alloc_queue     = full_gc_pending()->alloc_op_queue();

        full_gc_in_progress()->set_alloc_op_queue(new GPGC_OperationQueue());
        full_gc_pending()->set_alloc_op_queue(new GPGC_OperationQueue());
      }

      // Set the blocked state to coordinate with possible VM shutdown
      old_collector_thread->set_blocked( true );

      {
        // Grabbing the Threads_lock while the OldGC thread allocates ensures that we're
        // not allocating in a safepoint.
MutexLocker ml(Threads_lock,thread);

        // After each OldGC cycle completes, attempt to satisfy allocation operations.

        must_allocate(in_progress_alloc_queue);
        delete in_progress_alloc_queue;
        failed_pending_alloc_queue = try_allocate(pending_alloc_queue);
        delete pending_alloc_queue;
      }

      old_collector_thread->set_blocked( false );
      if ( old_collector_thread->should_terminate() || VM_Exit::vm_exited() ) {
old_collector_thread->block_if_vm_exited();
      }

      {
MutexLocker ml(PauselessGC_lock,thread);

        add_alloc_op_queue(full_gc_pending(), failed_pending_alloc_queue);

        // Complete GC Cycle operations, if there were any.
        GPGC_OperationQueue* cycle_op_queue  = full_gc_in_progress()->cycle_op_queue();
        GPGC_CycleOperation* cycle_op;
        while ((cycle_op = (GPGC_CycleOperation*) cycle_op_queue->remove_next())) {
          GPGC_Operation::complete_operation(cycle_op);
        }

        full_gc_in_progress()->cycle_complete();

        _could_be_running_constant_gc = true;
        GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H end OldGC: could_be_running_constant_gc to true");
      }

      if (should_coordinate_gc_cycle_end) {
        GPGC_Rendezvous::end_gc_cycle();
      }         

      // Notify waiting java threads of their allocations or cycles.
      {
MutexLocker mu(VMOperationRequest_lock,thread);
VMOperationRequest_lock.notify_all();
      }

      cycle_stats->verbose_gc_log("Old", "  - ");
      GPGC_Heuristic::update_old_gc_cycle_model(cycle_stats);
    }
  }
}


void GPGC_Thread::new_collector_loop() {
  long new_gc_counter = 0;
  Thread* thread = Thread::current();
  assert0(thread->is_GenPauselessGC_thread());
  GPGC_Thread* new_collector_thread = (GPGC_Thread*) thread;

  // Loop doing garbage collections until we're told to shutdown:
  while ( ! should_terminate() ) {

    ResourceMark rm;

    bool run_full_gc = false;

    // Wait on the VM_Operation lock for ops or next scheduled GC cycle.
    {
MutexLocker ml(PauselessGC_lock,thread);
  
      if ( (!full_gc_pending()->gc_cycle()) && GPGC_Heuristic::need_immediate_old_gc() ) {
        request_full_gc(GCCause::_gc_heuristic, false);
      }

      while ( (!full_gc_pending()->gc_cycle()) && (!should_terminate()) ) {
        ResourceMark rm;

        long sleep_millis = GPGCNewGCIntervalMS>0 ? GPGCNewGCIntervalMS : GPGC_Heuristic::time_to_sleep();

        if ( sleep_millis <= 0 ) {
          break;
        }

        if ( _could_be_running_constant_gc ) {
          _could_be_running_constant_gc = false;
          GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H NewGC sleeping: could_be_running_constant_gc to false");
          if ( GPGC_PageBudget::leak_into_grant_enabled() ) {
            // Disable leak-into-grant as soon as we realize we're not running back-to-back cycles.
            GPGC_PageBudget::disable_leak_into_grant();
            GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H no back-to-back GC 1, disabling leak-into-grant.");
          }
        }

        // Wait for the next scheduled GC cycle, or for another thread to request a VM_Operation.
        bool timedout = PauselessGC_lock.wait_micros(sleep_millis * 1000, false);
 
        // Did VM shutdown start while we were asleep?
        if ( should_terminate() ) {
          return;
        }
 
        // Update the allocation model for the period we slept.
        GPGC_Heuristic::update_new_gen_allocation_model();
        GPGC_Heuristic::update_perm_gen_allocation_model();

        if ( GPGCNewGCIntervalMS>0 && timedout ) {
          break;
        }
      }

      if ( GPGCNewGCIntervalMS>0 && !full_gc_in_progress()->gc_cycle() ) {
        // If we're currently doing a FullGC, see if it's time for a forced FullGC:
        new_gc_counter ++;
        if ( new_gc_counter > GPGCOldGCInterval ) {
          request_full_gc(GCCause::_gc_heuristic, false);
        }
      }

      run_full_gc = should_run_full_gc();

      if ( GPGC_PageBudget::leak_into_grant_enabled() ) {
        if ( (!_could_be_running_constant_gc) || (!full_gc_in_progress()->gc_cycle()) ) {
          // If we're not running back-to-back GC, then heuristic failures might cause us
          // to run out of memory, and we should be using the pause-prevention fund.
          GPGC_PageBudget::disable_leak_into_grant();

          GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H no back-to-back GC 2, disabling leak-into-grant.");
        }
      } else {
        if ( _could_be_running_constant_gc ) {
          if ( full_gc_in_progress()->gc_cycle() ) {
            // We are running back-to-back GC, so enable leak-into-grant.  A heuristic failure
            // couldn't account for running out of memory, so any allocation failure should use
            // grant funds, not pause-prevention funds.
            GPGC_PageBudget::enable_leak_into_grant();

            GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H back-to-back GC, enabling leak-into-grant.");
          } else {
            // If we could be running back-to-back GC, but we're not, then cancel the flag so it
            // doesn't still register as true on a later cycle.
            _could_be_running_constant_gc = false;
            GCLogMessage::log_b(GPGCTraceHeuristic, gclog_or_tty, "GPGC-H setting could_be_running_constant_gc to false");
          }
        }
      }

    } // Release the PauselessGC_lock

    if ( should_terminate() ) return;

    SafepointEndCallback sfpt_end_callback = full_gc_in_progress()->safepoint_end_callback();
    void* user_data               = full_gc_in_progress()->user_data();
    bool  reset_concurrency       = false;
    bool  reset_old_concurrency   = false;
    bool  saved_GPGCSafepointMark = false;
    bool  saved_GPGCNoRelocation  = false;
    GCCause::Cause gc_cause = full_gc_in_progress()->cause();
    if (gc_cause == GCCause::_jvmti_iterate_over_heap ||
        gc_cause == GCCause::_jvmti_iterate_over_reachable_objects ||
        gc_cause == GCCause::_heap_dump) {
      reset_concurrency       = true;
      saved_GPGCSafepointMark = GPGCSafepointMark;
      GPGCSafepointMark       = true;
      // Set GPGCNoRelocation to true
      // Since JvmtiTagMap::rehash will have to be called before we can
      // iterate over the heap, end the collection with the mark remap phase 
      saved_GPGCNoRelocation  = GPGCNoRelocation;
      GPGCNoRelocation        = true;
      reset_old_concurrency   = true;
      full_gc_in_progress()->set_heap_iterate_collection();
      GPGC_NewCollector::reset_concurrency();
      GPGC_OldCollector::reset_concurrency();
    } else if (gc_cause == GCCause::_java_lang_system_gc ||
               gc_cause == GCCause::_system_resourcelimit_hit) {
      // resume allowing System.gc()'s to be posted 
      GPGC_Heap::reset_system_gc_is_pending();
    }

char*cycle_label=NULL;

    {
      JvmtiGCForAllocationMarker jgcm(full_gc_in_progress()->is_heap_iterate_collection());
TraceMemoryManagerStats tms(false/*Full GC*/);

      if ( run_full_gc ) {
        if ( gc_cause == GCCause::_gc_heuristic ) {
          cycle_label = "Max   ";
        } else if ( gc_cause == GCCause::_system_resourcelimit_hit ) {
          cycle_label = "Res   ";
        } else if ( gc_cause == GCCause::_memory_funds ) {
          cycle_label = "Mem   ";
        } else if ( gc_cause == GCCause::_java_lang_system_gc ) {
          cycle_label = "Sys   ";
        } else if ( gc_cause == GCCause::_jvmti_force_gc ) {
          cycle_label = "TI    ";
          GPGC_Rendezvous::set_should_coordinate_gc_cycle_end(true);
        } else if ( gc_cause == GCCause::_jvmti_iterate_over_heap ) {
          cycle_label = "TIIOH ";
          GPGC_Rendezvous::set_should_coordinate_gc_cycle_end(true);
        } else if ( gc_cause == GCCause::_jvmti_iterate_over_reachable_objects ) {
          cycle_label = "TIIOR ";
          GPGC_Rendezvous::set_should_coordinate_gc_cycle_end(true);
        } else if ( gc_cause == GCCause::_heap_dump ) {
          cycle_label = "HD    ";
          GPGC_Rendezvous::set_should_coordinate_gc_cycle_end(true);
        } else if ( gc_cause == GCCause::_allocation_failure ) {
          cycle_label = "Alo   ";        
        } else {
NOT_PRODUCT(ShouldNotReachHere();)
          cycle_label = "Max   ";
        }
        run_full_gc_cycle(sfpt_end_callback, user_data);
      } else {
        cycle_label = "New   ";
        run_new_gc_cycle(sfpt_end_callback, user_data);
      }
   
      // Track memory usage and detect low memory
      MemoryService::track_memory_usage();
    }

    // Initialize the collection of timing stats for any waiting mutator threads.
    reset_waiter_stats();

    // Again check VM shutdown before doing more allocation
    if ( should_terminate() ) return;

    // After each NewGC cycle completes, attempt to satisfy allocation operations.
    // We don't give up on an allocation operation until the end of an OldGC cycle.
    GPGC_OperationQueue* in_progress_alloc_queue        = NULL;
    GPGC_OperationQueue* failed_in_progress_alloc_queue = NULL;
    GPGC_OperationQueue* pending_alloc_queue            = NULL;
    GPGC_OperationQueue* failed_pending_alloc_queue     = NULL;
    {
MutexLocker ml(PauselessGC_lock,thread);

      in_progress_alloc_queue = full_gc_in_progress()->alloc_op_queue();
      pending_alloc_queue     = full_gc_pending()->alloc_op_queue();

      full_gc_in_progress()->set_alloc_op_queue(new GPGC_OperationQueue());
      full_gc_pending()->set_alloc_op_queue(new GPGC_OperationQueue());
    }
 
    // Set the blocked state to coordinate with possible VM shutdown
    new_collector_thread->set_blocked( true );

    {
      // Grabbing the Threads_lock while the OldGC thread allocates ensures that we're
      // not allocating in a safepoint.
MutexLocker ml(Threads_lock,thread);

      failed_in_progress_alloc_queue = try_allocate(in_progress_alloc_queue);
      delete in_progress_alloc_queue;
      failed_pending_alloc_queue     = try_allocate(pending_alloc_queue);
      delete pending_alloc_queue;
    }

    new_collector_thread->set_blocked( false );
    if ( new_collector_thread->should_terminate() || VM_Exit::vm_exited() ) {
new_collector_thread->block_if_vm_exited();
    }

    {
MutexLocker ml(PauselessGC_lock,thread);

      add_alloc_op_queue(full_gc_in_progress(), failed_in_progress_alloc_queue);
      add_alloc_op_queue(full_gc_pending(), failed_pending_alloc_queue);
    }

    log_waiter_stats(cycle_label);

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
    assert0(heap->kind() == CollectedHeap::GenPauselessHeap);
    GPGC_CycleStats* cycle_stats = heap->gc_stats()->new_gc_cycle_stats();

    if ( run_full_gc ) {
      cycle_stats->verbose_gc_log("NTO", cycle_label);
    } else {
      cycle_stats->verbose_gc_log("New", "  - ");
    }

    if (GPGC_Rendezvous::should_coordinate_gc_cycle_end()) {
      GPGC_Rendezvous::end_gc_cycle();
    }
    GPGC_Rendezvous::set_should_coordinate_gc_cycle_end(false);

    if (reset_concurrency) {
      GPGCSafepointMark = saved_GPGCSafepointMark;
      GPGCNoRelocation  = saved_GPGCNoRelocation;
      GPGC_NewCollector::reset_concurrency();
    }

    if (reset_old_concurrency) {
      GPGC_OldCollector::reset_concurrency();
    }
    
    // Notify waiting java threads of their allocations or cycles.
    {
MutexLocker mu(VMOperationRequest_lock,thread);
VMOperationRequest_lock.notify_all();
    }

    GPGC_Heuristic::update_new_gen_allocation_model(cycle_stats->intracycle_alloc_rate(),
                                                    double(cycle_stats->cycle_ticks())/double(os::elapsed_frequency()));
    GPGC_Heuristic::update_old_gen_allocation_model(cycle_stats->pages_promoted_to_old());
    GPGC_Heuristic::update_perm_gen_allocation_model();
    GPGC_Heuristic::update_new_gc_cycle_model(cycle_stats);
  }
}


void GPGC_Thread::run_new_gc_cycle(SafepointEndCallback safepoint_end_callback, void* user_data) {
assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: Begin NewGC iteration");

  GPGC_CycleStats* cycle_stats = heap->gc_stats()->new_gc_cycle_stats();

cycle_stats->reset();
  cycle_stats->start_cycle();

  bool clear_all = full_gc_in_progress()->gc_cycle() && full_gc_in_progress()->clear_all();

  GPGC_NewCollector::collect("New", clear_all, cycle_stats, safepoint_end_callback, user_data);

  cycle_stats->end_cycle();

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: end NewGC iteration");
  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
           "Heap size: capacity " INT64_FORMAT "KB, used " INT64_FORMAT "KB",
           heap->capacity()/K,
           heap->used()/K);
}


void GPGC_Thread::run_full_gc_cycle(SafepointEndCallback safepoint_end_callback, void* user_data) {
  guarantee(GPGC_OldCollector::collection_state() == GPGC_Collector::NotCollecting, "Can't run FullGC with OldGC already going");

assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: Begin NewToOld GC iteration");

  GPGC_CycleStats* cycle_stats = heap->gc_stats()->new_gc_cycle_stats();

cycle_stats->reset();
  cycle_stats->start_cycle();

  GPGC_NewCollector::set_mark_old_space_roots(true);
  GPGC_NewCollector::collect("NewToOld", full_gc_in_progress()->clear_all(), cycle_stats, safepoint_end_callback, user_data);
  GPGC_NewCollector::set_mark_old_space_roots(false);

  cycle_stats->end_cycle();

  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
"GenPauselessGC: end NewToOld GC iteration");
  Log::log6(NOTAG, Log::M_GC, PrintGCDetails, true, gclog_or_tty,
           "Heap size: capacity " INT64_FORMAT "KB, used " INT64_FORMAT "KB",
           heap->capacity()/K,
           heap->used()/K);
}


void GPGC_Thread::reset_waiter_stats()
{
_waiters_base_time=os::elapsed_counter();
  _waiters_earliest_start_time = 0;
  _waiters_total_start_offset  = 0;
  _waiters_thread_count        = 0;
}


void GPGC_Thread::count_op_time(GPGC_Operation* op)
{
  _waiters_thread_count ++;
  _waiters_total_start_offset += _waiters_base_time - op->start_time();

  if ( _waiters_earliest_start_time==0 || _waiters_earliest_start_time>op->start_time() ) {
    _waiters_earliest_start_time = op->start_time();
  }
}


void GPGC_Thread::log_waiter_stats(char* cycle_label)
{

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  assert0(heap->kind() == CollectedHeap::GenPauselessHeap);

  double average = 0;
  double max     = 0;

  if ( _waiters_thread_count != 0 ) {
    long   now          = os::elapsed_counter();
    long   average_wait = (_waiters_total_start_offset / _waiters_thread_count) + (now - _waiters_base_time);
    long   max_wait     = now - _waiters_earliest_start_time;

double frequency=(double)os::elapsed_frequency();
           average      = double(average_wait) / frequency;
           max          = double(max_wait)     / frequency;

    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
    GCLogMessage::log_b(m.enabled(), m.stream(),
"%sGC cycle %d: allocation failure: %d threads paused for GC, average of %3.7f secs, max %3.7f secs",
                      cycle_label, heap->actual_collections(), _waiters_thread_count, average, max);

    GPGC_CycleStats* cycle_stats = heap->gc_stats()->new_gc_cycle_stats();
    cycle_stats->threads_delayed(_waiters_thread_count, average, max);
  }
}


void GPGC_Thread::must_allocate(GPGC_AllocOperation* op)
{
  assert0(op->is_alloc_operation());

op->allocate();

count_op_time(op);

  GPGC_Operation::complete_operation(op);
}


bool GPGC_Thread::try_allocate(GPGC_AllocOperation* op)
{
  assert0(op->is_alloc_operation());

HeapWord*result=op->allocate();

if(result==NULL)return false;

count_op_time(op);

  GPGC_Operation::complete_operation(op);

  return true;
}


void GPGC_Thread::must_allocate(GPGC_OperationQueue* alloc_op_queue) {
  GPGC_AllocOperation* alloc_op = NULL;

  while ((alloc_op = (GPGC_AllocOperation*) alloc_op_queue->remove_next())) {
    must_allocate(alloc_op);
  }
}


GPGC_OperationQueue* GPGC_Thread::try_allocate(GPGC_OperationQueue* alloc_op_queue) {
  GPGC_AllocOperation* alloc_op        = NULL;
  GPGC_OperationQueue* failed_op_queue = new GPGC_OperationQueue();

  while ((alloc_op = (GPGC_AllocOperation*) alloc_op_queue->remove_next())) {
    if ( ! try_allocate(alloc_op) ) {
      failed_op_queue->add(alloc_op);
    }
  }

  return failed_op_queue;
}


void GPGC_Thread::add_alloc_op_queue(GPGC_Cycle* cycle, GPGC_OperationQueue* failed_op_queue) {
  GPGC_AllocOperation* alloc_op        = NULL;
  GPGC_OperationQueue* alloc_op_queue  = cycle->alloc_op_queue();

  while ((alloc_op = (GPGC_AllocOperation*) alloc_op_queue->remove_next())) {
    failed_op_queue->add(alloc_op);
  }

  delete alloc_op_queue;
  cycle->set_alloc_op_queue(failed_op_queue);
}
