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
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

//
// GCTask
//

const char* GCTask::Kind::to_string(kind value) {
  const char* result = "unknown GCTask kind";
  switch (value) {
  default:
    result = "unknown GCTask kind";
    break;
  case unknown_task:
    result = "unknown task";
    break;
  case ordinary_task:
    result = "ordinary task";
    break;
  case barrier_task:
    result = "barrier task";
    break;
  case noop_task:
    result = "noop task";
    break;
  }
  return result;
};

GCTask::GCTask() :
  _kind(Kind::ordinary_task),
  _affinity(GCTaskManager::sentinel_worker()){
  initialize();
}

GCTask::GCTask(Kind::kind kind) :
  _kind(kind),
  _affinity(GCTaskManager::sentinel_worker()) {
  initialize();
}

GCTask::GCTask(uint affinity) :
  _kind(Kind::ordinary_task),
  _affinity(affinity) {
  initialize();
}

GCTask::GCTask(Kind::kind kind, uint affinity) :
  _kind(kind),
  _affinity(affinity) {
  initialize();
}

void GCTask::initialize() {
  _older = NULL;
  _newer = NULL;
}

void GCTask::destruct() {
  assert(older() == NULL, "shouldn't have an older task");
  assert(newer() == NULL, "shouldn't have a newer task");
  // Nothing to do.
}

void GCTask::worker_hit_barrier(){
  // This should only be called on BarrierGCTask and its subclasses.
  ShouldNotReachHere();
}

NOT_PRODUCT(
void GCTask::print(const char* message) const {
  tty->print(PTR_FORMAT " <- " PTR_FORMAT "(%u) -> " PTR_FORMAT,
             newer(), this, affinity(), older());
}
)

// 
// GCTaskQueue
// 

GCTaskQueue* GCTaskQueue::create() {
  GCTaskQueue* result = new GCTaskQueue(false);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create()"
" returns "PTR_FORMAT,result);
  }
  return result;
}

GCTaskQueue* GCTaskQueue::create_on_c_heap() {
  GCTaskQueue* result = new(ResourceObj::C_HEAP) GCTaskQueue(true);
  if (TraceGCTaskQueue) {
    tty->print_cr("GCTaskQueue::create_on_c_heap()"
" returns "PTR_FORMAT,
                  result);
  }
  return result;
}

GCTaskQueue::GCTaskQueue(bool on_c_heap) :
  _is_c_heap_obj(on_c_heap) {
  initialize();
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::GCTaskQueue() constructor",
                  this);
  }
}

void GCTaskQueue::destruct() {
  // Nothing to do.
}

void GCTaskQueue::destroy(GCTaskQueue* that) {
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::destroy()"
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

void GCTaskQueue::initialize() {
  set_insert_end(NULL);
  set_remove_end(NULL);
  set_length(0);
}

// Enqueue one task.
void GCTaskQueue::enqueue(GCTask* task) {
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::enqueue(task: "
                  PTR_FORMAT ")",
                  this, task);
    print("before:");
  }
  assert(task != NULL, "shouldn't have null task");
  assert(task->older() == NULL, "shouldn't be on queue");
  assert(task->newer() == NULL, "shouldn't be on queue");
  task->set_newer(NULL);
  task->set_older(insert_end());
  if (is_empty()) {
    set_remove_end(task);
  } else {
    insert_end()->set_newer(task);
  }
  set_insert_end(task);
  increment_length();
  if (TraceGCTaskQueue) {
    print("after:");
  }
}

// Enqueue a whole list of tasks.  Empties the argument list.
void GCTaskQueue::enqueue(GCTaskQueue* list) {
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::enqueue(list: "
                  PTR_FORMAT ")",
                  this, list);
    print("before:");
    list->print("list:");
  }
  if (list->is_empty()) {
    // Enqueuing the empty list: nothing to do.
    return;
  }
  uint list_length = list->length();
  if (is_empty()) {
    // Enqueuing to empty list: just acquire elements.
    set_insert_end(list->insert_end());
    set_remove_end(list->remove_end());
    set_length(list_length);
  } else {
    // Prepend argument list to our queue.
    list->remove_end()->set_older(insert_end());
    insert_end()->set_newer(list->remove_end());
    set_insert_end(list->insert_end());
    // was wrongly being set outside the else thereby setting the length to twice the actual size
    set_length(length() + list_length); 
    // empty the argument list.
  }
  list->initialize();
  if (TraceGCTaskQueue) {
    print("after:");
    list->print("list:");
  }
}

// Dequeue one task.
GCTask* GCTaskQueue::dequeue() {
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::dequeue()", this);
    print("before:");
  }
  assert(!is_empty(), "shouldn't dequeue from empty list");
  GCTask* result = remove();
  assert(result != NULL, "shouldn't have NULL task");
  if (TraceGCTaskQueue) {
tty->print_cr("    return: "PTR_FORMAT,result);
    print("after:");
  }
  return result;
}

// Dequeue one task, preferring one with affinity.
GCTask* GCTaskQueue::dequeue(uint affinity) {
  if (TraceGCTaskQueue) {
tty->print_cr("["PTR_FORMAT"]"
                  " GCTaskQueue::dequeue(%u)", this, affinity);
    print("before:");
  }
  assert(!is_empty(), "shouldn't dequeue from empty list");
  // Look down to the next barrier for a task with this affinity.
  GCTask* result = NULL;
  for (GCTask* element = remove_end();
       element != NULL;
       element = element->newer()) {
    if (element->is_barrier_task()) {
      // Don't consider barrier tasks, nor past them.
      result = NULL;
      break;
    }
    if (element->affinity() == affinity) {
      result = remove(element);
      break;
    }
  }
  // If we didn't find anything with affinity, just take the next task.
  if (result == NULL) {
    result = remove();
  }
  if (TraceGCTaskQueue) {
tty->print_cr("    return: "PTR_FORMAT,result);
    print("after:");
  }
  return result;
}

GCTask* GCTaskQueue::remove() {
  // Dequeue from remove end.
  GCTask* result = remove_end();
  assert(result != NULL, "shouldn't have null task");
  assert(result->older() == NULL, "not the remove_end");
  set_remove_end(result->newer());
  if (remove_end() == NULL) {
    assert(insert_end() == result, "not a singleton");
    set_insert_end(NULL);
  } else {
    remove_end()->set_older(NULL);
  }
  result->set_newer(NULL);
  decrement_length();
  assert(result->newer() == NULL, "shouldn't be on queue");
  assert(result->older() == NULL, "shouldn't be on queue");
  return result;
}

GCTask* GCTaskQueue::remove(GCTask* task) {
  // This is slightly more work, and has slightly fewer asserts
  // than removing from the remove end.
  assert(task != NULL, "shouldn't have null task");
  GCTask* result = task;
  if (result->newer() != NULL) {
    result->newer()->set_older(result->older());
  } else {
    assert(insert_end() == result, "not youngest");
    set_insert_end(result->older());
  }
  if (result->older() != NULL) {
    result->older()->set_newer(result->newer());
  } else {
    assert(remove_end() == result, "not oldest");
    set_remove_end(result->newer());
  }
  result->set_newer(NULL);
  result->set_older(NULL);
  decrement_length();
  return result;
}

NOT_PRODUCT(
void GCTaskQueue::print(const char* message) const {
tty->print_cr("["PTR_FORMAT"] GCTaskQueue:"
"  insert_end: "PTR_FORMAT
"  remove_end: "PTR_FORMAT
                "  %s",
                this, insert_end(), remove_end(), message);
  for (GCTask* element = insert_end();
       element != NULL;
       element = element->older()) {
    element->print("    ");
    tty->cr();
  }
}
)

// 
// SynchronizedGCTaskQueue
// 

SynchronizedGCTaskQueue::SynchronizedGCTaskQueue(GCTaskQueue* queue_arg,
AzLock*lock_arg):
  _unsynchronized_queue(queue_arg),
  _lock(lock_arg) {
  assert(unsynchronized_queue() != NULL, "null queue");
  assert(lock() != NULL, "null lock");
}

SynchronizedGCTaskQueue::~SynchronizedGCTaskQueue() {
  // Nothing to do.
}

// 
// GCTaskManager
// 
GCTaskManager::GCTaskManager(uint workers) :
  _workers(workers),
_ndc(NULL),
_gpgc_verify(false){
  initialize();
}
GCTaskManager::GCTaskManager(uint workers, bool gpgc_verify) :
  _workers(workers),
_ndc(NULL),
  _gpgc_verify(gpgc_verify) {
  initialize();
}

GCTaskManager::GCTaskManager(uint workers, NotifyDoneClosure* ndc) :
  _workers(workers),
_ndc(ndc),
_gpgc_verify(false){
  initialize();
}

void GCTaskManager::initialize() {
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::initialize: workers: %u", workers());
  }
  assert(workers() != 0, "no workers");
_active_threads=_workers;
  _monitor = new WaitLock(MonitorSupply_lock._rank,
"GCTaskManager monitor",false);
  _inactive_monitor = new WaitLock(MonitorSupply_lock._rank,
"GCTaskManager inactive thread monitor",false);
  // The queue for the GCTaskManager must be a CHeapObj.
  GCTaskQueue* unsynchronized_queue = GCTaskQueue::create_on_c_heap();
  _queue = SynchronizedGCTaskQueue::create(unsynchronized_queue, lock());
  _noop_task = NoopGCTask::create_on_c_heap();
  _inactive_thread_task = InactiveThreadGCTask::create_on_c_heap();
  _resource_flag = NEW_C_HEAP_ARRAY(bool, workers());
  {
    // Set up worker threads.
    _thread = NEW_C_HEAP_ARRAY(GCTaskThread*, workers());
    for (uint t = 0; t < workers(); t += 1) {
set_thread(t,GCTaskThread::create(this,t));
    }
  }
  reset_busy_workers();
  set_unblocked();
  for (uint w = 0; w < workers(); w += 1) {
    set_resource_flag(w, false);
  }
  reset_delivered_tasks();
  reset_completed_tasks();
  reset_noop_tasks();
  reset_inactive_thread_tasks();
  reset_barriers();
  reset_emptied_queue();
  for (uint s = 0; s < workers(); s += 1) {
    thread(s)->start();
  }
}


uint GCTaskManager::add_workers(uint new_workers) {
  uint new_total, orig_workers, actual_new_workers = 0; 
  GCTaskThread **new_thread_holder = NULL,**tmp_thread_holder = NULL;
  bool *tmp_resource_flag = NULL;
  
  //tty->print_cr("Adding %d more workers to pool of %d", new_workers, _workers); 
  new_total = _workers + new_workers;

  bool * new_resource_flag = NEW_C_HEAP_ARRAY(bool, new_total);
  // ran out of memory - cant add any more threads
  if (new_resource_flag == NULL) return _workers;
  
  // make a new array big enough for the existing threads plus new ones
  new_thread_holder = NEW_C_HEAP_ARRAY(GCTaskThread*, new_total);
assert(new_thread_holder!=NULL,"add_workers: new_thread_holder was NULL!");
  // ran out of memory - cant add any more threads 
  if (new_thread_holder == NULL) return _workers;
  
  // fill in the front of the array with the existing threads
  memcpy(new_thread_holder, _thread, sizeof(GCTaskThread*) * workers());
  // create the new threads
  for (uint t = workers(); t < new_total; t += 1) {
    GCTaskThread* new_worker = GCTaskThread::create(this, t);
    // this is pretty lame error checking during runtime
assert(new_worker!=NULL,"add_workers: could not add new GCTaskThread");
if(new_worker==NULL){
      // clean up and return with the original thread count
      for (uint k = workers(); k < t; k += 1) {
        GCTaskThread::destroy(new_thread_holder[k]);
      }
if(new_thread_holder!=NULL)
FREE_C_HEAP_ARRAY(GCTaskThread*,new_thread_holder);
if(new_resource_flag!=NULL)
FREE_C_HEAP_ARRAY(bool,new_resource_flag);
      return _workers;
    }
    new_thread_holder[t] = new_worker;
  }

tmp_thread_holder=_thread;
  tmp_resource_flag = _resource_flag;
orig_workers=_workers;
  // we are just starting a new gc cycle here so all the workers should be idle.
  // take the manager lock just in case and atomically reset all the fields we need
  {
    MutexLocker ml(*monitor());
    
_thread=new_thread_holder;
_resource_flag=new_resource_flag;
    _workers         = new_total;
    _active_threads  = new_total;
  }
  // free the older smaller arrays
FREE_C_HEAP_ARRAY(GCTaskThread*,tmp_thread_holder);
FREE_C_HEAP_ARRAY(bool,tmp_resource_flag);

  // now reset everything and start the new threads
  set_unblocked();
  for (uint s = orig_workers; s < new_total; s += 1) {
set_resource_flag(s,false);
    thread(s)->start();
  }
  return new_total;
}

GCTaskManager::~GCTaskManager() {
  assert(busy_workers() == 0, "still have busy workers");
  assert(queue()->is_empty(), "still have queued work");
  NoopGCTask::destroy(_noop_task);
  _noop_task = NULL;
  if (_thread != NULL) {
    for (uint i = 0; i < workers(); i += 1) {
      GCTaskThread::destroy(thread(i));
      set_thread(i, NULL);
    }
    FREE_C_HEAP_ARRAY(GCTaskThread*, _thread);
    _thread = NULL;
  }
  if (_resource_flag != NULL) {
    FREE_C_HEAP_ARRAY(bool, _resource_flag);
    _resource_flag = NULL;
  }
  if (queue() != NULL) {
    GCTaskQueue* unsynchronized_queue = queue()->unsynchronized_queue();
    GCTaskQueue::destroy(unsynchronized_queue);
    SynchronizedGCTaskQueue::destroy(queue());
    _queue = NULL;
  }
  if (monitor() != NULL) {
    delete monitor();
    _monitor = NULL;
  }
}

void GCTaskManager::print_task_time_stamps() {
for(uint i=0;i<_workers;i++){
    GCTaskThread* t = thread(i);
    t->print_task_time_stamps();
  }
}

void GCTaskManager::print_threads_on(outputStream* st) {
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    thread(i)->print_on(st);
    st->cr();
  }
} 

void GCTaskManager::threads_do(ThreadClosure* tc) {
  assert(tc != NULL, "Null ThreadClosure");
  uint num_thr = workers();
  for (uint i = 0; i < num_thr; i++) {
    tc->do_thread(thread(i));
  }
}

GCTaskThread* GCTaskManager::thread(uint which) {
  assert(which < workers(), "index out of bounds");
  assert(_thread[which] != NULL, "shouldn't have null thread");
  return _thread[which];
}

void GCTaskManager::set_thread(uint which, GCTaskThread* value) {
  assert(which < workers(), "index out of bounds");
  assert(value != NULL, "shouldn't have null thread");
  _thread[which] = value;
}

void GCTaskManager::set_active_threads(uint active){
  assert(active <= workers(), "Can't set more active GC task threads than exist");
  MutexLocker ml(*inactive_monitor());
  if ( active > _active_threads ) {
    // Wake up any currently inactive threads that might now be active.
(void)inactive_monitor()->notify_all();
  }
  _active_threads = active;
}

void GCTaskManager::add_task(GCTask* task) {
  assert(task != NULL, "shouldn't have null task");
MutexLocker ml(*monitor());
  if (TraceGCTaskManager) {
tty->print_cr("GCTaskManager::add_task("PTR_FORMAT" [%s])",
                  task, GCTask::Kind::to_string(task->kind()));
  }
  queue()->enqueue(task);
  // Notify with the lock held to avoid missed notifies.
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::add_task (%s)->notify_all",
                  monitor()->name());
  }
  (void) monitor()->notify_all();
  // Release monitor().
}

void GCTaskManager::add_list(GCTaskQueue* list) {
  assert(list != NULL, "shouldn't have null task");
MutexLocker ml(*monitor());
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::add_list(%u)", list->length());
  }
  queue()->enqueue(list);
  // Notify with the lock held to avoid missed notifies.
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::add_list (%s)->notify_all",
                  monitor()->name());
  }
  (void) monitor()->notify_all();
  // Release monitor().
}

GCTask* GCTaskManager::get_task(uint which) {
  GCTask* result = NULL;
  // Grab the queue lock.
  MutexLocker ml(*monitor(),Thread::current());
  // Wait while the queue is block or 
  // there is nothing to do, except maybe release resources.
  while (is_blocked() || 
         (queue()->is_empty() && !should_release_resources(which))) {
    if (TraceGCTaskManager) {
      tty->print_cr("GCTaskManager::get_task(%u)"
                    "  blocked: %s"
                    "  empty: %s"
                    "  release: %s",
                    which,
                    is_blocked() ? "true" : "false",
                    queue()->is_empty() ? "true" : "false",
                    should_release_resources(which) ? "true" : "false");
      tty->print_cr("    => (%s)->wait()",
                    monitor()->name());
    }
monitor()->wait();
  }
  // We've reacquired the queue lock here.
  // Figure out which condition caused us to exit the loop above.
  if (!queue()->is_empty()) {
    if ( which >= active_threads() ) {
      // We don't always use all of the GCTaskThreads.  Any thread past the active
      // count gets handled a dummy GCTask, which will cause it to block until the
      // the number of active threads is modified.
result=inactive_thread_task();
      increment_inactive_thread_tasks();
    } else {
      if (UseGCTaskAffinity) {
        result = queue()->dequeue(which);
      } else {
        result = queue()->dequeue();
      }
      if (result->is_barrier_task()) {
assert(which!=sentinel_worker(),"blocker shouldn't be bogus");
set_blocking_worker(which,result);
        result->worker_hit_barrier();
      }
    }
  } else {
    // The queue is empty, but we were woken up.
    // Just hand back a Noop task,
    // in case someone wanted us to release resources, or whatever.
    result = noop_task();
    increment_noop_tasks();
  }
  assert(result != NULL, "shouldn't have null task");
  if (TraceGCTaskManager) {
tty->print_cr("GCTaskManager::get_task(%u) => "PTR_FORMAT" [%s]",
                  which, result, GCTask::Kind::to_string(result->kind()));
    tty->print_cr("     %s", result->name());
  }
  if (result->kind() != GCTask::Kind::inactive_thread_task) {
    increment_busy_workers();
  }
  increment_delivered_tasks();
  return result;
  // Release monitor().
}

void GCTaskManager::note_completion(GCTask::Kind::kind task_kind, uint which) {
  MutexLocker ml(*monitor());
  if (TraceGCTaskManager) {
    tty->print_cr("GCTaskManager::note_completion(%u)", which);
  }
  // If we are blocked, check if the completing thread is the blocker.
  if (blocking_worker() == which) {
    assert(blocking_worker() != sentinel_worker(),
           "blocker shouldn't be bogus");
    increment_barriers();
    set_unblocked();
  }
  else if ( is_blocked() ) {
    GCTask* barrier_task = blocking_task();
    barrier_task->worker_hit_barrier();
  }
  increment_completed_tasks();
  if (task_kind != GCTask::Kind::inactive_thread_task) {
    uint active = decrement_busy_workers();
    if ((active == 0) && (queue()->is_empty())) {
      increment_emptied_queue();
      if (TraceGCTaskManager) {
        tty->print_cr("    GCTaskManager::note_completion(%u) done", which);
      }
      // Notify client that we are done.
      NotifyDoneClosure* ndc = notify_done_closure();
      if (ndc != NULL) {
        ndc->notify(this);
      }
    }
  }
  if (TraceGCTaskManager) {
    tty->print_cr("    GCTaskManager::note_completion(%u) (%s)->notify_all",
                  which, monitor()->name());
    tty->print_cr("  "
                  "  blocked: %s"
                  "  empty: %s"
                  "  release: %s",
                  is_blocked() ? "true" : "false",
                  queue()->is_empty() ? "true" : "false",
                  should_release_resources(which) ? "true" : "false");
    tty->print_cr("  "
                  "  delivered: %u"
                  "  completed: %u"
                  "  barriers: %u"
                  "  emptied: %u",
                  delivered_tasks(),
                  completed_tasks(),
                  barriers(),
                  emptied_queue());
  }
  // Tell everyone that a task has completed.
  (void) monitor()->notify_all();  
  // Release monitor().
}

uint GCTaskManager::increment_busy_workers() {
  assert(queue()->own_lock(), "don't own the lock");
  _busy_workers += 1;
  return _busy_workers;
}

uint GCTaskManager::decrement_busy_workers() {
  assert(queue()->own_lock(), "don't own the lock");
  _busy_workers -= 1;
  return _busy_workers;
}

void GCTaskManager::release_all_resources() {
  // If you want this to be done atomically, do it in a BarrierGCTask.
  for (uint i = 0; i < workers(); i += 1) {
    set_resource_flag(i, true);
  }
}

bool GCTaskManager::should_release_resources(uint which) {
  // This can be done without a lock because each thread reads one element.
  return resource_flag(which);
}

void GCTaskManager::note_release(uint which) {
  // This can be done without a lock because each thread writes one element.
  set_resource_flag(which, false);
}

void GCTaskManager::execute_and_wait(GCTaskQueue* list) {
  WaitForBarrierGCTask* fin = WaitForBarrierGCTask::create();
  list->enqueue(fin);
  add_list(list);
  fin->wait_for();
  // We have to release the barrier tasks!
  WaitForBarrierGCTask::destroy(fin);
}

bool GCTaskManager::resource_flag(uint which) {
  assert(which < workers(), "index out of bounds");
  return _resource_flag[which];
}

void GCTaskManager::set_resource_flag(uint which, bool value) {
  assert(which < workers(), "index out of bounds");
  _resource_flag[which] = value;
}

void GCTaskManager::request_new_gc_mode(bool gc_mode) {
for(uint i=0;i<workers();i++){
thread(i)->request_new_gc_mode(gc_mode);
  }
}

// 
// NoopGCTask
//

NoopGCTask* NoopGCTask::create() {
  NoopGCTask* result = new NoopGCTask(false);
  return result;
}

NoopGCTask* NoopGCTask::create_on_c_heap() {
  NoopGCTask* result = new(ResourceObj::C_HEAP) NoopGCTask(true);
  return result;
}

void NoopGCTask::destroy(NoopGCTask* that) {
  if (that != NULL) {
    that->destruct();
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void NoopGCTask::destruct() {
  // This has to know it's superclass structure, just like the constructor.
  this->GCTask::destruct();
  // Nothing else to do.
}

//
// InactiveThreadGCTask
//

InactiveThreadGCTask* InactiveThreadGCTask::create() {
  InactiveThreadGCTask* result = new InactiveThreadGCTask(false);
  return result;
}

InactiveThreadGCTask* InactiveThreadGCTask::create_on_c_heap() {
InactiveThreadGCTask*result=new(ResourceObj::C_HEAP)InactiveThreadGCTask(true);
  return result;
}

void InactiveThreadGCTask::destroy(InactiveThreadGCTask*that){
  if (that != NULL) {
    that->destruct();
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void InactiveThreadGCTask::destruct(){
  // This has to know it's superclass structure, just like the constructor.
  this->GCTask::destruct();
  // Nothing else to do.
}

void InactiveThreadGCTask::do_it(GCTaskManager*manager,uint which){
  MutexLocker ml(*manager->inactive_monitor());
  // If we're not one of the currently active threads, sleep until someone signals
  // that the inactive thread count has increased.
  if ( which >= manager->active_threads() ) {
    manager->inactive_monitor()->wait();
  }
}

//
// BarrierGCTask
//

void BarrierGCTask::do_it(GCTaskManager* manager, uint which) {
  // Wait for this to be the only busy worker.
  // ??? I thought of having a StackObj class 
  //     whose constructor would grab the lock and come to the barrier,
  //     and whose destructor would release the lock,
  //     but that seems like too much mechanism for two lines of code.
MutexLocker ml(*manager->lock());
  do_it_internal(manager, which);
  // Release manager->lock().
}

void BarrierGCTask::do_it_internal(GCTaskManager* manager, uint which) {
  // Wait for this to be the only busy worker.
  assert(manager->monitor()->owned_by_self(), "don't own the lock");
  assert(manager->is_blocked(), "manager isn't blocked");
  while (manager->busy_workers() > 1) {
    if (TraceGCTaskManager) {
      tty->print_cr("BarrierGCTask::do_it(%u) waiting on %u workers",
                    which, manager->busy_workers());
    }
manager->monitor()->wait();
  }
}

void BarrierGCTask::worker_hit_barrier(){
  long now = os::elapsed_counter();
  if ( _first_blocked_tick == 0 ) {
_first_blocked_tick=now;
  }
_last_blocked_tick=now;
  _total_ticks += now;
  _workers ++;
}

void BarrierGCTask::destruct() {
  this->GCTask::destruct();
  // Nothing else to do.
}

// 
// ReleasingBarrierGCTask
//

void ReleasingBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
MutexLocker ml(*manager->lock());
  do_it_internal(manager, which);
  manager->release_all_resources();
  // Release manager->lock().
}

void ReleasingBarrierGCTask::destruct() {
  this->BarrierGCTask::destruct();
  // Nothing else to do.
}

// 
// NotifyingBarrierGCTask
// 

void NotifyingBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
MutexLocker ml(*manager->lock());
  do_it_internal(manager, which);
  NotifyDoneClosure* ndc = notify_done_closure();
  if (ndc != NULL) {
    ndc->notify(manager);
  }
  // Release manager->lock().
}

void NotifyingBarrierGCTask::destruct() {
  this->BarrierGCTask::destruct();
  // Nothing else to do.
}

// 
// WaitForBarrierGCTask
// 
WaitForBarrierGCTask* WaitForBarrierGCTask::create() {
  WaitForBarrierGCTask* result = new WaitForBarrierGCTask(false);
  return result;
}

WaitForBarrierGCTask* WaitForBarrierGCTask::create_on_c_heap() {
  WaitForBarrierGCTask* result = new WaitForBarrierGCTask(true);
  return result;
}

WaitForBarrierGCTask::WaitForBarrierGCTask(bool on_c_heap) :
  _is_c_heap_obj(on_c_heap) {
  _monitor = MonitorSupply::reserve();
  set_should_wait(true);
  if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                  " WaitForBarrierGCTask::WaitForBarrierGCTask()"
"  monitor: "PTR_FORMAT,
                  this, monitor());
  }
}

void WaitForBarrierGCTask::destroy(WaitForBarrierGCTask* that) {
  if (that != NULL) {
    if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                    " WaitForBarrierGCTask::destroy()"
                    "  is_c_heap_obj: %s"
"  monitor: "PTR_FORMAT,
                    that, 
                    that->is_c_heap_obj() ? "true" : "false", 
                    that->monitor());
    }
    that->destruct();
    if (that->is_c_heap_obj()) {
      FreeHeap(that);
    }
  }
}

void WaitForBarrierGCTask::destruct() {
  assert(monitor() != NULL, "monitor should not be NULL");
  if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                  " WaitForBarrierGCTask::destruct()"
"  monitor: "PTR_FORMAT,
                  this, monitor());
  }
  this->BarrierGCTask::destruct();
  // Clean up that should be in the destructor, 
  // except that ResourceMarks don't call destructors.
   if (monitor() != NULL) {
     MonitorSupply::release(monitor());
  }
_monitor=(WaitLock*)0xDEAD000F;
}
  
void WaitForBarrierGCTask::do_it(GCTaskManager* manager, uint which) {
  if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                  " WaitForBarrierGCTask::do_it() waiting for idle"
"  monitor: "PTR_FORMAT,
                  this, monitor());
  }
  {
    // First, wait for the barrier to arrive.
MutexLocker ml(*manager->lock());
    do_it_internal(manager, which);
    // Release manager->lock().
  }
  {
    // Then notify the waiter.
MutexLocker ml(*monitor());
    set_should_wait(false);
    // Waiter doesn't miss the notify in the wait_for method
    // since it checks the flag after grabbing the monitor.
    if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                    " WaitForBarrierGCTask::do_it()"
"  ["PTR_FORMAT"] (%s)->notify_all()",
                    this, monitor(), monitor()->name());
    }
    monitor()->notify_all();
    // Release monitor().
  }
}

void WaitForBarrierGCTask::wait_for() {
  if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                  " WaitForBarrierGCTask::wait_for()"
      "  should_wait: %s", 
      this, should_wait() ? "true" : "false");
  }
  {
    // Grab the lock and check again.
MutexLocker ml(*monitor());
    while (should_wait()) {
      if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                      " WaitForBarrierGCTask::wait_for()"
"  ["PTR_FORMAT"] (%s)->wait()",
          this, monitor(), monitor()->name());
      }
monitor()->wait();
    }
    // Reset the flag in case someone reuses this task.
    set_should_wait(true);
    if (TraceGCTaskManager) {
tty->print_cr("["PTR_FORMAT"]"
                    " WaitForBarrierGCTask::wait_for() returns"
        "  should_wait: %s", 
        this, should_wait() ? "true" : "false");
    }
    // Release monitor().
  }
}

GrowableArray<WaitLock*>*MonitorSupply::_freelist=NULL;

WaitLock*MonitorSupply::reserve(){
MutexLocker ml(MonitorSupply_lock);
  // Lazy initialization.
if(!_freelist)
_freelist=new(ResourceObj::C_HEAP)GrowableArray<WaitLock*>(4,true);
WaitLock*result=_freelist->is_empty()
    ? new WaitLock (MonitorSupply_lock._rank, "MonitorSupply monitor", false)
    : _freelist->pop();
  guarantee(result != NULL, "shouldn't return NULL");
  assert(!result->is_locked(), "shouldn't be locked");
  return result;
}

void MonitorSupply::release(WaitLock*instance){
  assert(instance != NULL, "shouldn't release NULL");
  assert(!instance->is_locked(), "shouldn't be locked");
MutexLocker ml(MonitorSupply_lock);
  freelist()->push(instance);
}
