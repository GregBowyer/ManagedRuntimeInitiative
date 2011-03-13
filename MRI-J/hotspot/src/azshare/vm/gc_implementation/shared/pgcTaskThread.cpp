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

#include "handles.inline.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "pauselessTraps.hpp"
#include "pgcTaskManager.hpp"
#include "pgcTaskThread.hpp"
#include "resourceArea.hpp"
#include "tickProfiler.hpp"
#include "vmTags.hpp"

#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

PGCTaskThread::PGCTaskThread(PGCTaskManager* manager,
                             uint64_t which) : 
                             _wakeupCount(0), _pgcManager(manager), _pgcWhich(which),
                             _time1(0), _time2(0),
                             _ticks1(), _ticks2(0), _ticks3(0),
                             GCTaskThread(NULL, uint(which))  {
}

void PGCTaskThread::run(){
  // Set up the thread for stack overflow support
  this->initialize_thread_local_storage();
  this->set_gcthread_lvb_trap_vector();
  this->set_gc_mode(true);
  this->set_vm_tag(VM_GCTask_tag);

  // Part of thread setup.
  // ??? Are these set up once here to make subsequent ones fast?
  HandleMark   hm_outer;
  ResourceMark rm_outer; 

  TimeStamp timer;

  while (true) {
    // These are so we can flush the resources allocated in the inner loop.
    HandleMark   hm_inner;
    ResourceMark rm_inner;

    {
      MutexLocker ml(*manager()->start_monitor(), this);
      while (wakeup_count() == manager()->wakeup_count()) {
        manager()->start_monitor()->wait();
      }
    }

    set_wakeup_count(manager()->wakeup_count());
    assert0(manager()->lists_added() != 0);
    assert0(manager()->active_threads() != 0);

    if ( new_gc_mode_requested() ) {
      set_new_gc_mode_requested(false);
      this->set_gc_mode(new_gc_mode());
    }

    if (TraceGCTaskManager) {
tty->print_cr("trying to drain task list (%lu)",which());
    }

PGCTask*task;
    NOT_PRODUCT (   long last_tick = _time1 = os::elapsed_counter(); )
    NOT_PRODUCT (   long now       = 0; )
    while ( NULL != (task = manager()->get_task(which()))) {
      // In case the update is costly
      if (PrintGCTaskTimeStamps) {
        timer.update();
      }

      jlong entry_time = timer.ticks();

     NOT_PRODUCT ( now           = os::elapsed_counter(); )
     NOT_PRODUCT ( _ticks1 += now - last_tick; )         // Ticks spend managing global lists.
     NOT_PRODUCT ( last_tick     = now; )                   

     task->do_it(which());

     NOT_PRODUCT ( now           = os::elapsed_counter(); )
     NOT_PRODUCT ( _ticks2 += now - last_tick; )         // Ticks spend doing task.
     NOT_PRODUCT ( last_tick     = now; )                   

     if (PrintGCTaskTimeStamps) {
const char*name=task->name();
       assert(_time_stamps != NULL, "Sanity (PrintGCTaskTimeStamps set late?)");

       timer.update();

       GCTaskTimeStamp* time_stamp = time_stamp_at(_time_stamp_index++);

       time_stamp->set_name(name);
       time_stamp->set_entry_time(entry_time);
       time_stamp->set_exit_time(timer.ticks());
     } 
      DEBUG_ONLY ( manager()->increment_completed_tasks(); )
    }
    manager()->note_completion(which());
    NOT_PRODUCT  ( now           = os::elapsed_counter(); )
    NOT_PRODUCT  ( _ticks3 += now - last_tick; )                   // ticks spent in notification 
    NOT_PRODUCT  ( _time2   = now; )                   
  }
}
