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

#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_tlb.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "pgcTaskManager.hpp"
#include "thread.hpp"
#include "timer.hpp"

#include "gpgc_pageInfo.inline.hpp"

void GPGC_OldCollector::update_page_relocation_states(TimeDetailTracer* tdt, GPGC_PageInfo::Gens gen)
{
  // the interlock is acquired by the remap_old/perm call that happens before 
  assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));

  const char* gen_tag;
  uint64_t    ss_pages, ms_pages, ls_pages; 

  if ( gen == GPGC_PageInfo::OldGen )  {
    gen_tag = "old";
    ss_pages = page_pops()->small_space_in_old()->max_cursor();
    ms_pages = page_pops()->mid_space_in_old()->max_cursor();
    ls_pages = page_pops()->large_space_in_old()->max_cursor(); // check this??
  } else {
    gen_tag = "perm";
    ss_pages = page_pops()->small_space_in_perm()->max_cursor();
    ms_pages = page_pops()->mid_space_in_perm()->max_cursor();
    ls_pages = page_pops()->large_space_in_perm()->max_cursor();
  }

  DetailTracer dt(tdt, false, "O: Set relocation state on %lu %s gen small pages, %lu mid pages, %lu large blocks",
                  ss_pages, gen_tag, ms_pages, ls_pages);
               
  uint64_t      sections = GenPauselessOldThreads;
PGCTaskQueue*q=PGCTaskQueue::create();

  for ( uint64_t i=0; i<sections; i++ ) {
    q->enqueue( new GPGC_OldGC_UpdateRelocateStatesTask(i, sections, gen) );
  }

  GPGC_Collector::run_task_queue(GPGC_Heap::old_gc_task_manager(), q);
}


// Tell all threads to cease taking NMT traps
// happens outside a safepoint now
void GPGC_OldCollector::disable_NMT_traps(TimeDetailTracer* tdt)
{
  DetailTracer dt(tdt, false, "O: Disable NMT traps");

  // Disable the NMT trap and flush the NMT buffer on each thread
  GPGC_NMT::disable_old_trap();
#ifdef ASSERT
  {
    GCMutexLocker ml( Threads_lock );
    assert( !GPGC_Thread::full_gc_in_progress()->is_heap_iterate_collection() || 
GPGC_Safepoint::is_at_safepoint(),"must be at safepoint");

    for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
      assert0( jt->get_old_gen_ref_buffer() && jt->get_old_gen_ref_buffer()->is_empty() );
    }

    GPGC_GCManagerOldStrong::ensure_all_stacks_are_empty("nmt traps disabled");
    GPGC_GCManagerOldFinal::ensure_all_stacks_are_empty("nmt traps disabled");
  }
#endif
}
