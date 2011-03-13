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


#include "auditTrail.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_thread.hpp"
#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "os_os.inline.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

         VM_VMThreadSafepoint* GPGC_Safepoint::_vm_thread_safepoint_op  = NULL;
volatile intptr_t              GPGC_Safepoint::_state                   = GPGC_Safepoint::NotAtSafepoint;
volatile bool                  GPGC_Safepoint::_new_safepoint           = false;
volatile bool                  GPGC_Safepoint::_old_safepoint           = false;
volatile long                  GPGC_Safepoint::_last_safepoint_end_time = 0;
volatile bool                  GPGC_Safepoint::_callbacks_completed     = false;


bool GPGC_Safepoint::is_at_new_safepoint()
{
  return _new_safepoint;
}


bool GPGC_Safepoint::is_at_safepoint()
{
  Thread* thread = Thread::current();

  if ( GPGC_Collector::is_new_collector_thread(thread) ) {
    return _new_safepoint;
  }
  else if ( GPGC_Collector::is_old_collector_thread(thread) ) {
    return _old_safepoint;
  }
  else {
    return (_state==AtSafepoint);
  }
}


void GPGC_Safepoint::begin(TimeDetailTracer* tdt, bool clean_vm_thread, SafepointEndCallback end_callback, void* user_data, bool safepoint_other_collector)
{
assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();
  char*        tag;

if(thread->is_new_collector()){
    assert0( _new_safepoint == false );
    tag = "N:";
  } else {
    assert0( _old_safepoint == false );
    tag = "O:";
  }

  GPGC_Interlock::acquire_interlock(tdt, GPGC_Interlock::GPGC_Safepoint, tag);

  long now               = os::elapsed_counter();
  long millis_since_last = (now - _last_safepoint_end_time) / (os::elapsed_frequency() / 1000);

  // We space safepoints at least GPGCSafepointSpacing ms apart.
  if ( millis_since_last < (long)GPGCSafepointSpacing ) {
    DetailTracer dt(tdt, false, "%s Pause %d millis for safepoint spacing", tag, (GPGCSafepointSpacing-millis_since_last));

    do {
thread->set_blocked(true);
      os::sleep(thread, (GPGCSafepointSpacing-millis_since_last), false);
thread->set_blocked(false);
      if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
      }

now=os::elapsed_counter();
      millis_since_last = (now - _last_safepoint_end_time) / (os::elapsed_frequency() / 1000);
    } while ( millis_since_last < (long)GPGCSafepointSpacing );
  }

  // Try and move the state to AtSafepoint
  assert0( _vm_thread_safepoint_op == NULL );

  // Set the blocked state to coordinate with possible VM shutdown
thread->set_blocked(true);

  _callbacks_completed    = false;

SafepointTimes times;

  _vm_thread_safepoint_op = new VM_VMThreadSafepoint(clean_vm_thread, &times, end_callback, user_data);
  _vm_thread_safepoint_op->safepoint_vm_thread();

  if ( tdt->details() ) {
    double freq            = double(os::elapsed_frequency()) / 1000.0;
    double threads_lock_ms = double(times.threads_lock_acquired  - times.acquire_threads_lock) / freq;
    double notify_ms       = double(times.all_threads_notified   - times.begin_threads_notify) / freq;
    double wait_ms         = double(times.safepoint_reached      - times.all_threads_notified) / freq;
    double cleanup_ms      = double(times.cleanup_tasks_complete - times.safepoint_reached   ) / freq;

    GCLogMessage::log_a(tdt->details(), tdt->stream(), true,
"%s Safepoint time: threads lock %3.7f, notify %3.7f, wait %3.7f, cleanup %3.7f millis",
                      tag, threads_lock_ms, notify_ms, wait_ms, cleanup_ms);
  }

  long safepoint_ticks = os::elapsed_counter() - times.begin_threads_notify;

  tdt->start_pause        (times.begin_threads_notify);
  tdt->add_safepoint_ticks(safepoint_ticks);

  // If we are racing through with a pending vm exit while trying to 
  // safepoint the vm thread, we will have returned to here without
  // having stopped the vm thread and should_terminate() should be set
  // Set the blocked state to coordinate with possible VM shutdown
thread->set_blocked(false);
  if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
  }

_state=AtSafepoint;

  // TODO: maw: we ought to be setting _is_gc_active during pauseless GC safepoint

  // DEBUG_ONLY( PageSpace::verify_page_budgets(); )

if(thread->is_new_collector()){
    _new_safepoint = true;
    AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_START_SAFEPOINT);

    // NewGC sometimes aquires a safepoint on behalf of both collectors.
    if ( safepoint_other_collector ) {
      _old_safepoint = true;
      AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_START_SAFEPOINT);
    }
  }

if(thread->is_old_collector()){
assert(safepoint_other_collector==false,"OldGC safepointing for NewGC isn't supported.");
    _old_safepoint = true;
    AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_START_SAFEPOINT);
  }
}


void GPGC_Safepoint::end(TimeDetailTracer* tdt) {
assert0(Thread::current()->is_GenPauselessGC_thread());

GPGC_Thread*thread=(GPGC_Thread*)Thread::current();
  char*        tag;

if(thread->is_new_collector()){
    assert0( _new_safepoint == true );
    tag = "N:";
  } else {
    assert0( _old_safepoint == true );
    tag = "O:";
  }

  // DEBUG_ONLY( PageSpace::verify_page_budgets(); )

  // This results in a bogus total collections count.  We get total GC safepoints
  // instead.  But, for debug builds, it causes the No_GC_Verifier to choke if
  // some thread allows a GC safepoint inappropriately.
Universe::heap()->increment_total_collections();

  if ( tdt ) {
    tdt->start_safepointing();
  }

  bool wait_for_callback_end = (_vm_thread_safepoint_op->end_callback() != NULL);

  _vm_thread_safepoint_op->restart_vm_thread();
  // The VMThread is responsible for deleting the VM_Operation once it wakes up.
_vm_thread_safepoint_op=NULL;

  // Note that this check cannot be moved to restart_vm_thread because there is a
  // race between the VMThread deleting the vm operation and this thread checking for
  // _end_callback of the vm operation
  if (wait_for_callback_end) {
    // Wait for the callback to finish before proceeding
    MutexLocker mlc(VMThreadCallbackEnd_lock);

    while (!_callbacks_completed) {
VMThreadCallbackEnd_lock.wait();
    }
  }

  if ( tdt) {
    tdt->end_safepointing();
    tdt->end_pause();
  }

_last_safepoint_end_time=os::elapsed_counter();
_state=NotAtSafepoint;

  // One last shutdown check before proceeding to the next safepoint 
  if ( thread->should_terminate() || VM_Exit::vm_exited() ) {
thread->block_if_vm_exited();
  }

  GPGC_Interlock::release_interlock(tdt, GPGC_Interlock::GPGC_Safepoint, tag);

if(thread->is_new_collector()){
    _new_safepoint = false;
    AuditTrail::log_time(GPGC_NewCollector::audit_trail(), AuditTrail::GPGC_END_SAFEPOINT);

    if ( _old_safepoint == true ) {
      // NewGC sometimes starts a safepoint that covers OldGC also.  If that's happened,
      // release the OldGC safepoint flag also.
      _old_safepoint = false;
      AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_END_SAFEPOINT);
    }
  }

if(thread->is_old_collector()){
    _old_safepoint = false;
    AuditTrail::log_time(GPGC_OldCollector::audit_trail(), AuditTrail::GPGC_END_SAFEPOINT);
  }
}

void GPGC_Safepoint::do_checkpoint(JavaThreadClosure* jtc, SafepointTimes* times) {
if(GPGC_Safepoint::is_at_safepoint()){
    // In various debugging modes, we might not be marking concurrently.
    for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
      jtc->do_java_thread(jt);
    }

    memset(times, 0, sizeof(*times));
  } else {
    SafepointSynchronize::do_checkpoint(jtc, times);
  }
}
