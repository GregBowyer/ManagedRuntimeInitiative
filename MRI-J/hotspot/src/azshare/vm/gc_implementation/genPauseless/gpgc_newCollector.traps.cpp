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
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_space.hpp"
#include "gpgc_population.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_tasks.hpp"
#include "gpgc_tlb.hpp"
#include "timer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "os_os.inline.hpp"

// Flip the desired NMT flag value, and tell all the threads to begin
// taking NMT traps.
void GPGC_NewCollector::enable_NMT_traps(TimeDetailTracer* tdt)
{
assert(GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");

  if ( BatchedMemoryOps ) {
    // Commit batched swap in of new read barrier trap array inside a safepoint.
    GPGC_Collector::commit_batched_memory_ops(tdt, GPGC_Collector::NewCollector);
  } else {
    // Swap in new read barrier trap array to enable new trap state:
    GPGC_ReadTrapArray::swap_readbarrier_arrays(tdt, GPGC_Collector::NewCollector);
  }

  {
    DetailTracer dt(tdt, false, "N: NMT enable: ");

    //  Setup the NMT trap for the threads
    GPGC_NMT::toggle_new_nmt_flag();
    GPGC_NMT::enable_new_trap();

    if ( mark_old_space_roots() ) {
      GPGC_NMT::toggle_old_nmt_flag();
      GPGC_NMT::enable_old_trap();
    }

    // Make sure the new desired NMT state is visible to all the threads before we start
    // telling them to update their NMT state.
    if (os::is_MP()) Atomic::membar();

    // For each JavaThread, tell them to change their NMT state, and ask them to
    // self-clean their stacks when they resume.
    long thread_count          = 0;
    long uncleaned_new_threads = 0;
    long uncleaned_old_threads = 0;

    for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
      thread_count ++;

      assert0( jt->get_new_gen_ref_buffer() && jt->get_new_gen_ref_buffer()->is_empty() );

      if ( jt->please_self_suspend() & JavaThread::gpgc_clean_new ) {
        uncleaned_new_threads++;
      } else {
        jt->set_suspend_request_no_polling( JavaThread::gpgc_clean_new );
      }

      AuditTrail::log_time(jt, AuditTrail::GPGC_TOGGLE_NMT, intptr_t(objectRef::new_space_id));

      if ( mark_old_space_roots() ) {
        assert0( jt->get_old_gen_ref_buffer() && jt->get_old_gen_ref_buffer()->is_empty() );

        if ( jt->please_self_suspend() & JavaThread::gpgc_clean_old ) {
          uncleaned_old_threads++;
        } else {
          jt->set_suspend_request_no_polling( JavaThread::gpgc_clean_old );
        }
      }
    }

dt.print("found %d unclean-new threads (%d unclean-old) out of %d",
             uncleaned_new_threads, uncleaned_old_threads, thread_count);
  }
}


void GPGC_NewCollector::update_page_relocation_states(TimeDetailTracer* tdt)
{
  DetailTracer dt(tdt, false, "N: Set relocation state on %d new gen small pages, %d mid blocks, %d large blocks",
                  page_pops()->small_space_to_new()->max_cursor() + page_pops()->small_space_to_old()->max_cursor(),
                  page_pops()->mid_space_to_new()->max_cursor()   + page_pops()->mid_space_to_old()->max_cursor(),
                  page_pops()->large_space_to_new()->max_cursor() + page_pops()->large_space_to_old()->max_cursor());

  uint64_t      sections = GenPauselessNewThreads;
PGCTaskQueue*q=PGCTaskQueue::create();

  for ( uint64_t i=0; i<sections; i++ ) {
    q->enqueue( new GPGC_NewGC_UpdateRelocateStatesTask(i, sections) );
  }

  GPGC_Collector::run_task_queue(&dt, GPGC_Heap::new_gc_task_manager(), q);
}


// Tell all threads to cease taking NMT traps
void GPGC_NewCollector::disable_NMT_traps(TimeDetailTracer* tdt)
{
  DetailTracer dt(tdt, false, "N: Disable NMT traps");

assert(GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");

  // Disable the NMT trap.
  GPGC_NMT::disable_new_trap();

  // Make sure the new desired NMT state is visible to all the threads before we start
  // telling them to update their NMT state.
  if ( os::is_MP() ) Atomic::membar();

  for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
    assert0( jt->get_new_gen_ref_buffer() && jt->get_new_gen_ref_buffer()->is_empty() );
  }
  // TODO: figure out what this function is really doing, now that we're not setting JavaThread::nmt_suspend anymore.
}
