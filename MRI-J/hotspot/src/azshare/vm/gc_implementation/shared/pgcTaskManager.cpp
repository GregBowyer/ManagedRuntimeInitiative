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

#include "allocation.inline.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_thread.hpp"
#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "pgcTaskManager.hpp"
#include "pgcTaskThread.hpp"
#include "tickProfiler.hpp"
#include "vm_operations.hpp"

#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

//
// PGCTask
//

PGCTask::PGCTask() {
  initialize();
}

void PGCTask::initialize(){
_next=NULL;
}

void PGCTask::destruct(){
assert(next()==NULL,"shouldn't have a newer task");
  // Nothing to do.
}

NOT_PRODUCT(
void PGCTask::print(const char*message)const{
  tty->print(PTR_FORMAT " <- " PTR_FORMAT "(%s) ",
             next(), this, message);
}
)

// 
// PGCTaskQueue
// 

PGCTaskQueue* PGCTaskQueue::create() {
  PGCTaskQueue* result = new PGCTaskQueue(false);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create()"
" returns "PTR_FORMAT,result);
  }
  return result;
}

PGCTaskQueue* PGCTaskQueue::create_on_c_heap() {
PGCTaskQueue*result=new(ResourceObj::C_HEAP)PGCTaskQueue(true);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create_on_c_heap()"
" returns "PTR_FORMAT,
                  result);
  }
  return result;
}

PGCTaskQueue::PGCTaskQueue(bool on_c_heap) :
  _is_c_heap_obj(on_c_heap) {
  initialize();
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::GCTaskQueue() constructor",
                  this);
  }
}

void PGCTaskQueue::destruct(){
  // Nothing to do.
}

void PGCTaskQueue::destroy(PGCTaskQueue*that){
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
" PGCTaskQueue::destroy()"
                  "  is_c_heap_obj:  %s",
                  that, 
                  that->is_c_heap_obj() ? "true" : "false");
  }
  // That instance may have been allocated as a CHeapObj, 
  // in which case we have to free it explicitly.
  if (that != NULL) {
    that->destruct();
    assert(that->is_empty(), "should be empty");
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void PGCTaskQueue::initialize(){
set_head(NULL);
}

// Enqueue one task.
void PGCTaskQueue::enqueue(PGCTask*task){
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
" PGCTaskQueue::enqueue(task: "
                  PTR_FORMAT ")",
                  this, task);
    print("before:");
  }
  assert(task != NULL, "shouldn't have null task");
assert(task->next()==NULL,"shouldn't be on queue");
  task->set_next(head());
set_head(task);
  if (TraceGCTaskQueue) {
    print("after:");
  }
}

// Enqueue a whole list of tasks.  Empties the argument list.
void PGCTaskQueue::enqueue(PGCTaskQueue*list){
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
" PGCTaskQueue::enqueue(list: "
        PTR_FORMAT ")",
        this, list);
    print("before:");
    list->print("list:");
  }
  if (list->is_empty()) {
    // Enqueuing the empty list: nothing to do.
    return;
  }
  assert0(is_empty());
set_head(list->head());
  list->initialize();
  if (TraceGCTaskQueue) {
    print("after:");
    list->print("list:");
  }
}

NOT_PRODUCT(
void PGCTaskQueue::print(const char*message)const{
tty->print_cr("["PTR_FORMAT"] PGCTaskQueue:"
"  head: "PTR_FORMAT
"  %s",
                this, head(), message);
  for (PGCTask* element = head();
       element != NULL;
element=element->next()){
    element->print("    ");
    tty->cr();
  }
}
)

PGCTask* PGCTaskQueue::grab() {
  PGCTask* new_addr;
  PGCTask* old_addr;
  long spin_counter=0;
  do {
old_addr=_head;
if(old_addr==NULL){
      break;
    }
    new_addr = old_addr->next();
    spin_counter ++;
  } while (old_addr != (PGCTask *)Atomic::cmpxchg_ptr(intptr_t(new_addr),(intptr_t*)&_head, intptr_t(old_addr))); 
  if (old_addr) {
old_addr->set_next(NULL);
  }
  if ( (spin_counter>3) && TraceGCTaskQueue) {
    const char* thread_name = (Thread::current()->is_Java_thread()) ? "Java Thread" : (Thread::current()->is_VM_thread()) ? "VM Thread" : "GC Thread" ;
    gclog_or_tty->print_cr("%s spun "INTX_FORMAT" times trying to get tasks", thread_name, spin_counter );
  }
  return old_addr;
}

//
// TaskManager
//
PGCTaskManager::PGCTaskManager(uint workers):
  _workers(workers) {
  initialize();
}

void PGCTaskManager::initialize(){
  if (TraceGCTaskManager) {
tty->print_cr("PGCTaskManager::initialize: workers: %lu",workers());
  }
  assert(workers() != 0, "no workers");
  _active_threads = 0;
  DEBUG_ONLY (
      _lists_added = 0;
      _delivered_tasks = 0;
      _completed_tasks = 0;
      _emptied_queue = 0;
  )
  _wakeup_count = 0;
  _start_monitor = new WaitLock(GPGC_TaskManagerNotify_lock._rank,             // rank
                                "PGCTaskManager start monitor", false);        // name
  _notify_monitor = new WaitLock(GPGC_TaskManagerNotify_lock._rank,            // rank
                                 "PGCTaskManager notify monitor", false);      // name
  // The queue for the GCTaskManager must be a CHeapObj.
  _queue = PGCTaskQueue::create_on_c_heap();
  {
    // Set up worker threads.
_thread=NEW_C_HEAP_ARRAY(PGCTaskThread*,workers());
    for (uint64_t t = 0; t < (uint64_t)workers(); t += 1) {
      set_thread(t, PGCTaskThread::create(this, t));
    }
  }
  for (uint s = 0; s < workers(); s += 1) {
    thread(s)->start();
  }
}

PGCTaskManager::~PGCTaskManager() {
  assert(queue()->is_empty(), "still have queued work");
  if (_thread != NULL) {
    for (uint64_t i = 0; i < (uint64_t)workers(); i += 1) {
PGCTaskThread::destroy(thread(i));
      set_thread(i, NULL);
    }
FREE_C_HEAP_ARRAY(PGCTaskThread*,_thread);
    _thread = NULL;
  }
  if (queue() != NULL) {
    PGCTaskQueue* unsynchronized_queue = queue();
PGCTaskQueue::destroy(unsynchronized_queue);
    _queue = NULL;
  }
if(start_monitor()!=NULL){
    delete start_monitor();
_start_monitor=NULL;
  }
if(notify_monitor()!=NULL){
    delete notify_monitor();
_notify_monitor=NULL;
  }
}

void PGCTaskManager::print_task_time_stamps(){
for(uint i=0;i<_workers;i++){
PGCTaskThread*t=thread(i);
    t->print_task_time_stamps();
  }
}

void PGCTaskManager::print_threads_on(outputStream*st){
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    thread(i)->print_on(st);
    st->cr();
  }
} 

void PGCTaskManager::threads_do(ThreadClosure*tc){
  assert(tc != NULL, "Null ThreadClosure");
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    tc->do_thread(thread(i));
  }
}

PGCTaskThread* PGCTaskManager::thread(uint64_t which) {
  assert(which < (uint64_t)workers(), "index out of bounds");
  assert(_thread[which] != NULL, "shouldn't have null thread");
  return _thread[which];
}

void PGCTaskManager::set_thread(uint64_t which, PGCTaskThread* value) {
  assert(which < (uint64_t)workers(), "index out of bounds");
  assert(value != NULL, "shouldn't have null thread");
  _thread[which] = value;
}

void PGCTaskManager::reset_perf_counters() {
  for (long i=0; i<workers(); i++) {
PGCTaskThread*thr=thread(i);

    thr->_time1  = thr->_time2 = 0;
    thr->_ticks1 = thr->_ticks2 = thr->_ticks3= 0;
  }
}

void PGCTaskManager::log_perf_counters() {
  for (long i=0; i<workers(); i++) {
PGCTaskThread*thr=thread(i);
    double tick_frequency     = os::elapsed_frequency();
    long   start_of_time_tick = gclog_or_tty->get_time_stamp()->ticks();

    double time1  = (thr->_time1 - start_of_time_tick) / tick_frequency;
    double time2  = (thr->_time2 - start_of_time_tick) / tick_frequency;

    double ticks1 = thr->_ticks1                       / tick_frequency;
    double ticks2 = thr->_ticks2                       / tick_frequency;
    double ticks3 = thr->_ticks3                       / tick_frequency;
    char* name = NULL;
      if ( this == GPGC_Heap::new_gc_task_manager()) { name = "NewGC Task Thread"; }
      else { name = "OldGC Task Thread"; } 

      GCLogMessage::log_b(true, gclog_or_tty,
"%s: perf counters #%d: %.3f %.3f : %.3f %.3f %.3f",
name,i,
          time1, time2,
          ticks1, ticks2, ticks3);
  }
}

void PGCTaskManager::add_list(PGCTaskQueue*list){
  assert(list != NULL, "shouldn't have null task");
  assert0(active_threads() == 0);
  assert0(queue()->is_empty());
  assert0(lists_added() == 0);

  Thread* thread = Thread::current();
if(thread->is_GenPauselessGC_thread()){
    if (((GPGC_Thread*)thread)->should_terminate() || VM_Exit::vm_exited()) {
      ((GPGC_Thread*)thread)->block_if_vm_exited();
    }
    ((GPGC_Thread*)thread)->set_blocked(true);
  } else {
    // GPGC is the only collector supported here.
    ShouldNotReachHere();
  }

  {
    MutexLocker ml(*start_monitor(),thread);
    NOT_PRODUCT (
        if ((list->head()) && TraceGCTaskManager) {
          GCLogMessage::log_b(true, gclog_or_tty,
"Task: %s",
          list->head()->name());
        }
    )
    queue()->enqueue(list);
DEBUG_ONLY(increment_lists_added();)
    increment_wakeup_count();
    set_active_threads(workers());  // for now we have everybody working.. we could throttle this
    // Notify with the lock held to avoid missed notifies.
    if (TraceGCTaskManager) {
tty->print_cr("    PGCTaskManager::add_list (%s)->notify_all",
start_monitor()->name());
    }
    (void) start_monitor()->notify_all(); //release
  }
  {
    MutexLocker ml(*notify_monitor(), thread);
    assert0(!(start_monitor()->owned_by_self()));
    while (active_threads() != 0) {
      notify_monitor()->wait();
    }

if(thread->is_GenPauselessGC_thread()){
      ((GPGC_Thread*)thread)->set_blocked(false);
      if (((GPGC_Thread*)thread)->should_terminate() || VM_Exit::vm_exited()) {
        MutexUnlocker uml(*notify_monitor()); // avoid deadlock check on the Threads_lock
        ((GPGC_Thread*)thread)->block_if_vm_exited();
      }
    } else {
      // GPGC is the only collector supported here.
      ShouldNotReachHere();
    }

    // we are woken up; the queue should be empty and there should be no active threads
    assert0(active_threads() == 0);
    assert0(queue()->is_empty());
    assert0(delivered_tasks() == completed_tasks());
    DEBUG_ONLY (
        reset_completed_tasks();
        reset_delivered_tasks();
        reset_emptied_queue();
        reset_lists_added(); 
    )
  }
}


void PGCTaskManager::set_active_threads(uint active) {
  assert(active <= workers(), "Can't set more active GC task threads than exist");
  _active_threads = active;
}

PGCTask* PGCTaskManager::get_task(uint64_t which) {
PGCTask*result=NULL;
  // we are woken up; check if we can get work
  if ((result = queue()->grab())) {
DEBUG_ONLY(increment_delivered_tasks();)
  } 
  /* throttle if needed
     if (which > active_threads) { 
     result = queue()->pop_task(); 
     increment_inactive_thread_tasks();
     }
   */
  if (TraceGCTaskManager) {
    if (result) {
tty->print_cr("GCTaskManager::get_task(%lu) => "PTR_FORMAT,
which,result);
tty->print_cr("     %s",result->name());
    }
  }
  return result;
  // Release monitor().
}

void PGCTaskManager::note_completion(uint64_t which) {
  {
    MutexLocker ml(*notify_monitor(),Thread::current());
    decrement_active_threads();
    if (active_threads() == 0) {

DEBUG_ONLY(increment_emptied_queue();)

      if (TraceGCTaskManager) {
tty->print_cr("    PGCTaskThread::note_completion(%lu) (%s)->notify",
which,notify_monitor()->name());
        DEBUG_ONLY (
            tty->print_cr("  "
"  delivered: %lu"
"  completed: %lu"
"  emptied: %lu",
               delivered_tasks(),
               completed_tasks(),
               emptied_queue());
        )
      }

      (void) notify_monitor()->notify();  
    }
    if (TraceGCTaskManager) {
tty->print_cr("    PGCTaskManager::note_completion(%lu) done",which);
    }
  }
}

void PGCTaskManager::request_new_gc_mode(bool gc_mode) {
for(uint i=0;i<workers();i++){
    thread(i)->request_new_gc_mode(gc_mode);
  }
}


DEBUG_ONLY (
    void PGCTaskManager::increment_completed_tasks() {
Atomic::inc_ptr(&_completed_tasks);
    }

    void PGCTaskManager::increment_delivered_tasks() {
Atomic::inc_ptr(&_delivered_tasks);
    }
    void PGCTaskManager::increment_emptied_queue() {
Atomic::inc_ptr(&_emptied_queue);
    }
)
