/*
 * Copyright 2002-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "gcTaskManager.hpp"
#include "gcTaskThread.hpp"
#include "gpgc_verifyClosure.hpp"
#include "ostream.hpp"
#include "pauselessTraps.hpp"
#include "resourceArea.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "os_os.inline.hpp"

GCTaskThread::GCTaskThread(GCTaskManager* manager,
uint which):
  _manager(manager),
  _new_gc_mode_requested(false),
  _new_gc_mode(true),
  _time_stamps(NULL),
_time_stamp_index(0),
  _preallocated_page(NoPage)
{
if(!os::create_thread(this,ttype::gc_thread))
    vm_exit_out_of_memory(0, "Cannot create GC thread. Out of system resources.");

  if (PrintGCTaskTimeStamps) {
    _time_stamps = NEW_C_HEAP_ARRAY(GCTaskTimeStamp, GCTaskTimeStampEntries );

    guarantee(_time_stamps != NULL, "Sanity");
  }

  set_id(which);

  if (UseGenPauselessGC) {
set_name("GC task thread#%d (GenPauselessGC)",which);
  } else {
    set_name("GC task thread#%d (ParallelGC)", which);
  }
}

GCTaskThread::~GCTaskThread() {
  if (_time_stamps != NULL) {
    FREE_C_HEAP_ARRAY(GCTaskTimeStamp, _time_stamps);
  }
}

void GCTaskThread::start() {
  os::start_thread(this);
}

GCTaskTimeStamp* GCTaskThread::time_stamp_at(uint index) {
  guarantee(index < GCTaskTimeStampEntries, "increase GCTaskTimeStampEntries");

  return &(_time_stamps[index]);
}

void GCTaskThread::print_task_time_stamps() {
  assert(PrintGCTaskTimeStamps, "Sanity");
  assert(_time_stamps != NULL, "Sanity (Probably set PrintGCTaskTimeStamps late)");

gclog_or_tty->print_cr("GC-Thread %u entries: %d",id(),_time_stamp_index);
  for(uint i=0; i<_time_stamp_index; i++) {
    GCTaskTimeStamp* time_stamp = time_stamp_at(i);
    if ( 1 ) {
gclog_or_tty->print_cr("\t[ %s %lld %lld %lld usecs ]",
                    time_stamp->name(),
                    time_stamp->entry_time(),
time_stamp->exit_time(),
                    ((time_stamp->exit_time()-time_stamp->entry_time())*1000000)/os::elapsed_frequency());
    } else {
gclog_or_tty->print_cr("\t[ %s %lld %lld ]",
                    time_stamp->name(),
                    time_stamp->entry_time(),
                    time_stamp->exit_time());
    }
  }

  // Reset after dumping the data
  _time_stamp_index = 0;
}

void GCTaskThread::request_new_gc_mode(bool gc_mode) {
  _new_gc_mode          = gc_mode;
  Atomic::write_barrier();
  _new_gc_mode_requested = true;
}

void GCTaskThread::print_on(outputStream* st) const {
  st->print("\"%s\" ", name());
  Thread::print_on(st);
  st->cr();
}

void GCTaskThread::run() {
  // Set up the thread for stack overflow support
  this->initialize_thread_local_storage();
  this->set_gcthread_lvb_trap_vector();
  this->set_gc_mode(true);
  this->set_vm_tag(VM_GCTask_tag);

  // Part of thread setup.
  // ??? Are these set up once here to make subsequent ones fast?
  HandleMark   hm_outer;
  ResourceMark rm_outer; 
 
  if ( manager()->gpgc_verify() ) {
    GPGC_VerifyClosure::task_thread_loop(this, which());
    return;
  }

  TimeStamp timer;

  for (;/* ever */;) {
    // These are so we can flush the resources allocated in the inner loop.
    HandleMark   hm_inner;
    ResourceMark rm_inner;
    for (; /* break */; ) {
      // This will block until there is a task to be gotten.
      GCTask* task = manager()->get_task(which());

      if ( _new_gc_mode_requested ) {
        _new_gc_mode_requested = false;
        this->set_gc_mode(_new_gc_mode);
      }
      // In case the update is costly
      if (PrintGCTaskTimeStamps) {
        timer.update();
      }

      jlong entry_time = timer.ticks();
const char*name=task->name();

      GCTask::Kind::kind task_kind = task->kind();
      task->do_it(manager(), which());
manager()->note_completion(task_kind,which());

      if (PrintGCTaskTimeStamps) {
        assert(_time_stamps != NULL, "Sanity (PrintGCTaskTimeStamps set late?)");

        timer.update();

        GCTaskTimeStamp* time_stamp = time_stamp_at(_time_stamp_index++);

        time_stamp->set_name(name);
        time_stamp->set_entry_time(entry_time);
        time_stamp->set_exit_time(timer.ticks());
      }

      // Check if we should release our inner resources.
      if (manager()->should_release_resources(which())) {
        manager()->note_release(which());
        break;
      }
    }
  }
}

