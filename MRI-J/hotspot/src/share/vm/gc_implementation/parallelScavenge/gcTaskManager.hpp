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
#ifndef GCTASKMANAGER_HPP
#define GCTASKMANAGER_HPP

#include "allocation.hpp"
#include "growableArray.hpp"
#include "mutex.hpp"

//
// The GCTaskManager is a queue of GCTasks, and accessors 
// to allow the queue to be accessed from many threads.  
//

// Forward declarations of types defined in this file.
class BarrierGCTask;
class GCTask;
class GCTaskManager;
class GCTaskQueue;
class GCTaskThread;
class InactiveThreadGCTask;
class MonitorSupply;
class AzLock;
class NoopGCTask;
class NotifyDoneClosure;
class NotifyingBarrierGCTask;
class ReleasingBarrierGCTask;
class SynchronizedGCTaskQueue;
class ThreadClosure;
class WaitForBarrierGCTask;
class WaitLock;

// The abstract base GCTask.
class GCTask : public ResourceObj {
public:
  // Known kinds of GCTasks, for predicates.
  class Kind : AllStatic {
  public:
    enum kind {
      unknown_task,
      ordinary_task,
      barrier_task, 
noop_task,
      inactive_thread_task
    };
    static const char* to_string(kind value);
  };
private:
  // Instance state.
  const Kind::kind _kind;               // For runtime type checking.
  const uint       _affinity;           // Which worker should run task.
  GCTask*          _newer;              // Tasks are on doubly-linked ... 
  GCTask*          _older;              // ... lists.
public:
virtual const char*name()=0;

  // Abstract do_it method
  virtual void do_it(GCTaskManager* manager, uint which) = 0;
  // Accessors
  Kind::kind kind() const {
    return _kind;
  }
  uint affinity() const {
    return _affinity;
  }
  GCTask* newer() const {
    return _newer;
  }
  void set_newer(GCTask* n) {
    _newer = n;
  }
  GCTask* older() const {
    return _older;
  }
  void set_older(GCTask* p) {
    _older = p;
  }
  // Predicates.
  bool is_ordinary_task() const {
    return kind()==Kind::ordinary_task;
  }
  bool is_barrier_task() const {
    return kind()==Kind::barrier_task;
  }
  bool is_noop_task() const {
    return kind()==Kind::noop_task;
  }
  virtual void worker_hit_barrier();
  void print(const char* message) const PRODUCT_RETURN;
protected:
  // Constructors: Only create subclasses.
  //     An ordinary GCTask.
  GCTask();
  //     A GCTask of a particular kind, usually barrier or noop.
  GCTask(Kind::kind kind);
  //     An ordinary GCTask with an affinity.
  GCTask(uint affinity);
  //     A GCTask of a particular kind, with and affinity.
  GCTask(Kind::kind kind, uint affinity);
  // We want a virtual destructor because virtual methods, 
  // but since ResourceObj's don't have their destructors 
  // called, we don't have one at all.  Instead we have 
  // this method, which gets called by subclasses to clean up.
  virtual void destruct();
  // Methods.
  void initialize();
};

// A doubly-linked list of GCTasks.
// The list is not synchronized, because sometimes we want to 
// build up a list and then make it available to other threads.
// See also: SynchronizedGCTaskQueue.
class GCTaskQueue : public ResourceObj {
private:
  // Instance state.
  GCTask*    _insert_end;               // Tasks are enqueued at this end.
  GCTask*    _remove_end;               // Tasks are dequeued from this end.
  uint       _length;                   // The current length of the queue.
  const bool _is_c_heap_obj;            // Is this a CHeapObj?
public:
  // Factory create and destroy methods.
  //     Create as ResourceObj.
  static GCTaskQueue* create();
  //     Create as CHeapObj.
  static GCTaskQueue* create_on_c_heap();
  //     Destroyer.
  static void destroy(GCTaskQueue* that);
  // Accessors.
  //     These just examine the state of the queue.
  bool is_empty() const {
    assert(((insert_end() == NULL && remove_end() == NULL) ||
            (insert_end() != NULL && remove_end() != NULL)),
           "insert_end and remove_end don't match");
    return insert_end() == NULL;
  }
  uint length() const {
    return _length;
  }
  // Methods.
  //     Enqueue one task.
  void enqueue(GCTask* task);
  //     Enqueue a list of tasks.  Empties the argument list.
  void enqueue(GCTaskQueue* list);
  //     Dequeue one task.
  GCTask* dequeue();
  //     Dequeue one task, preferring one with affinity.
  GCTask* dequeue(uint affinity);
protected:
  // Constructor. Clients use factory, but there might be subclasses.
  GCTaskQueue(bool on_c_heap);
  // Destructor-like method. 
  // Because ResourceMark doesn't call destructors.
  // This method cleans up like one.
  virtual void destruct();
  // Accessors.
  GCTask* insert_end() const {
    return _insert_end;
  }
  void set_insert_end(GCTask* value) {
    _insert_end = value;
  }
  GCTask* remove_end() const {
    return _remove_end;
  }
  void set_remove_end(GCTask* value) {
    _remove_end = value;
  }
  void increment_length() {
    _length += 1;
  }
  void decrement_length() {
    _length -= 1;
  }
  void set_length(uint value) {
    _length = value;
  }
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
  // Methods.
  void initialize();
  GCTask* remove();                     // Remove from remove end.
  GCTask* remove(GCTask* task);         // Remove from the middle.
  void print(const char* message) const PRODUCT_RETURN;
};

// A GCTaskQueue that can be synchronized.
// This "has-a" GCTaskQueue and a mutex to do the exclusion.
class SynchronizedGCTaskQueue : public CHeapObj {
private:
  // Instance state.
  GCTaskQueue* _unsynchronized_queue;   // Has-a unsynchronized queue.
AzLock*_lock;//Lock to control access.
public:
  // Factory create and destroy methods.
static SynchronizedGCTaskQueue*create(GCTaskQueue*queue,AzLock*lock){
    return new SynchronizedGCTaskQueue(queue, lock);
  }
  static void destroy(SynchronizedGCTaskQueue* that) {
    if (that != NULL) {
      delete that;
    }
  }
  // Accessors
  GCTaskQueue* unsynchronized_queue() const {
    return _unsynchronized_queue;
  }
AzLock*lock()const{
    return _lock;
  }
  // GCTaskQueue wrapper methods.
  // These check that you hold the lock
  // and then call the method on the queue.
  bool is_empty() const {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->is_empty();
  }
  void enqueue(GCTask* task) {
    guarantee(own_lock(), "don't own the lock");
    unsynchronized_queue()->enqueue(task);
  }
  void enqueue(GCTaskQueue* list) {
    guarantee(own_lock(), "don't own the lock");
    unsynchronized_queue()->enqueue(list);
  }
  GCTask* dequeue() {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->dequeue();
  }
  GCTask* dequeue(uint affinity) {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->dequeue(affinity);
  }
  uint length() const {
    guarantee(own_lock(), "don't own the lock");
    return unsynchronized_queue()->length();
  }
  // For guarantees.
  bool own_lock() const {
    return lock()->owned_by_self();
  }
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
SynchronizedGCTaskQueue(GCTaskQueue*queue,AzLock*lock);
  // Destructor.  Not virtual because no virtuals.
  ~SynchronizedGCTaskQueue();
};

// This is an abstract base class for getting notifications 
// when a GCTaskManager is done.
class NotifyDoneClosure : public CHeapObj {
public:
  // The notification callback method.
  virtual void notify(GCTaskManager* manager) = 0;
protected:
  // Constructor.
  NotifyDoneClosure() {
    // Nothing to do.
  }
  // Virtual destructor because virtual methods.
  virtual ~NotifyDoneClosure() {
    // Nothing to do.
  }
};
  
class GCTaskManager : public CHeapObj {
 friend class ParCompactionManager;
 friend class PSParallelCompact;
 friend class PSScavenge;
 friend class PSRefProcTaskExecutor;
 friend class RefProcTaskExecutor;
private:
  // Instance state.
  bool                      _gpgc_verify;           // This is a GPGCVerifyHeap task manager.
  NotifyDoneClosure*        _ndc;                   // Notify on completion.
uint _workers;//Total number of workers.
  uint                      _active_threads;        // Number of workers to use for tasks.
WaitLock*_monitor;//Notification of changes.
  WaitLock*                 _inactive_monitor;      // Notification of inactive thread changes.
  SynchronizedGCTaskQueue*  _queue;                 // Queue of tasks.
  GCTaskThread**            _thread;                // Array of worker threads.
  uint                      _busy_workers;          // Number of busy workers.
  uint                      _blocking_worker;       // The worker that's blocking.
  GCTask*                   _blocking_task;         // The task of the worker that's blocking.
  bool*                     _resource_flag;         // Array of flag per threads.
  uint                      _delivered_tasks;       // Count of delivered tasks.
  uint                      _completed_tasks;       // Count of completed tasks.
  uint                      _barriers;              // Count of barrier tasks.
  uint                      _emptied_queue;         // Times we emptied the queue.
  NoopGCTask*               _noop_task;             // The NoopGCTask instance.
  uint                      _noop_tasks;            // Count of noop tasks.
  InactiveThreadGCTask*     _inactive_thread_task;  // The InactiveThreadGCTask instance.
uint _inactive_thread_tasks;//Count of noop tasks.
public:
  // Factory create and destroy methods.
  static GCTaskManager* create(uint workers) {
    return new GCTaskManager(workers);
  }
  static GCTaskManager* create(uint workers, bool gpgc_verify) {
return new GCTaskManager(workers,gpgc_verify);
  }
  static GCTaskManager* create(uint workers, NotifyDoneClosure* ndc) {
    return new GCTaskManager(workers, ndc);
  }
  static void destroy(GCTaskManager* that) {
    if (that != NULL) {
      delete that;
    }
  }
  // Accessors.
  bool gpgc_verify() const {
    return _gpgc_verify;
  }
uint active_threads()const{
    return _active_threads;
  }
  uint busy_workers() const {
    return _busy_workers;
  }
  //     Pun between WaitLock* and AzLock*
WaitLock*monitor()const{
    return _monitor;
  }
AzLock*lock()const{
    return _monitor;
  }
  WaitLock* inactive_monitor() const {
    return _inactive_monitor;
  }
  // Methods.
  //     Change the number of active worker threads.
  void set_active_threads(uint active);

  //     Create and activate more worker threads
uint add_workers(uint workers);
  
  //     Add the argument task to be run.
  void add_task(GCTask* task);
  //     Add a list of tasks.  Removes task from the argument list.
  void add_list(GCTaskQueue* list);
  //     Claim a task for argument worker.
  GCTask* get_task(uint which);
  //     Note the completion of a task by the argument worker.
  void note_completion(GCTask::Kind::kind task_kind, uint which);
  //     Is the queue blocked from handing out new tasks?
  bool is_blocked() const {
    return (blocking_worker() != sentinel_worker());
  }
  //     Request that all workers release their resources.
  void release_all_resources();
  //     Ask if a particular worker should release its resources.
  bool should_release_resources(uint which); // Predicate.
  //     Note the release of resources by the argument worker.
  void note_release(uint which);
  // Constants.
  //     A sentinel worker identifier.
  static uint sentinel_worker() {
    return (uint) -1;                   // Why isn't there a max_uint?
  }
  // Request that all GCTaskThreads switch to a new trap table.
  void request_new_gc_mode(bool gc_mode);
  
  //     Execute the task queue and wait for the completion.
  void execute_and_wait(GCTaskQueue* list);
  
  void print_task_time_stamps();
  void print_threads_on(outputStream* st);
  void threads_do(ThreadClosure* tc);

protected:
  // Constructors.  Clients use factory, but there might be subclasses.
  //     Create a GCTaskManager with the appropriate number of workers.
  GCTaskManager(uint workers);
  //     Create a GCTaskManager for the GPGCVerifyHeap function.
  GCTaskManager(uint workers, bool gpgc_verify);
  //     Create a GCTaskManager that calls back when there's no more work.
  GCTaskManager(uint workers, NotifyDoneClosure* ndc);
  //     Make virtual if necessary.
  ~GCTaskManager();
  // Accessors.
  uint workers() const {
    return _workers;
  }
  NotifyDoneClosure* notify_done_closure() const {
    return _ndc;
  }
  SynchronizedGCTaskQueue* queue() const {
    return _queue;
  }
  NoopGCTask* noop_task() const {
    return _noop_task;
  }
  InactiveThreadGCTask* inactive_thread_task() const {
    return _inactive_thread_task;
  }
  //     Bounds-checking per-thread data accessors.
  GCTaskThread* thread(uint which);
  void set_thread(uint which, GCTaskThread* value);
  bool resource_flag(uint which);
  void set_resource_flag(uint which, bool value);
  // Modifier methods with some semantics.
  //     Is any worker blocking handing out new tasks?
  uint blocking_worker() const {
    return _blocking_worker;
  }
GCTask*blocking_task()const{
    return _blocking_task;
  }
  void set_blocking_worker(uint value, GCTask* task) {
    _blocking_worker = value;
_blocking_task=task;
  }
  void set_unblocked() {
set_blocking_worker(sentinel_worker(),NULL);
  }
  //     Count of busy workers.
  void reset_busy_workers() {
    _busy_workers = 0;
  }
  uint increment_busy_workers();
  uint decrement_busy_workers();
  //     Count of tasks delivered to workers.
  uint delivered_tasks() const {
    return _delivered_tasks;
  }
  void increment_delivered_tasks() {
    _delivered_tasks += 1;
  }
  void reset_delivered_tasks() {
    _delivered_tasks = 0;
  }
  //     Count of tasks completed by workers.
  uint completed_tasks() const {
    return _completed_tasks;
  }
  void increment_completed_tasks() {
    _completed_tasks += 1;
  }
  void reset_completed_tasks() {
    _completed_tasks = 0;
  }
  //     Count of barrier tasks completed.
  uint barriers() const {
    return _barriers;
  }
  void increment_barriers() {
    _barriers += 1;
  }
  void reset_barriers() {
    _barriers = 0;
  }
  //     Count of how many times the queue has emptied.
  uint emptied_queue() const {
    return _emptied_queue;
  }
  void increment_emptied_queue() {
    _emptied_queue += 1;
  }
  void reset_emptied_queue() {
    _emptied_queue = 0;
  }
  //     Count of the number of noop tasks we've handed out,
  //     e.g., to handle resource release requests.
  uint noop_tasks() const {
    return _noop_tasks;
  }
  void increment_noop_tasks() {
    _noop_tasks += 1;
  }
  void reset_noop_tasks() {
    _noop_tasks = 0;
  }
  //     Count of the number of inactive thread tasks we've handed out.
uint inactive_thread_tasks()const{
    return _inactive_thread_tasks;
  }
  void increment_inactive_thread_tasks() {
    _inactive_thread_tasks += 1;
  }
  void reset_inactive_thread_tasks() {
    _inactive_thread_tasks = 0;
  }
  // Other methods.
  void initialize();
};

// 
// Some exemplary GCTasks.
// 

// A noop task that does nothing, 
// except take us around the GCTaskThread loop.
class NoopGCTask : public GCTask {
private:
  const bool _is_c_heap_obj;            // Is this a CHeapObj?
public:

const char*name(){return"NoopGCTask";}
  // Factory create and destroy methods.
  static NoopGCTask* create();
  static NoopGCTask* create_on_c_heap();
  static void destroy(NoopGCTask* that);
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which) {
    // Nothing to do.
  }
protected:
  // Constructor.
  NoopGCTask(bool on_c_heap) :
    GCTask(GCTask::Kind::noop_task),
    _is_c_heap_obj(on_c_heap) {
    // Nothing to do.
  }
  // Destructor-like method.
  void destruct();
  // Accessors.
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
};

// An inactive thread task blocks a GCTaskThread until the GCTaskManager's active
// thread count is set to a new number.  Used to tie up excess GCTaskThreads when
// we don't want them all to run.
class InactiveThreadGCTask:public GCTask{
private:
  const bool _is_c_heap_obj;  // Is this a CHeapObj?
public:
const char*name(){return"InactiveThreadGCTask";}
  // Factory create and destroy methods.
static InactiveThreadGCTask*create();
static InactiveThreadGCTask*create_on_c_heap();
static void destroy(InactiveThreadGCTask*that);
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.
InactiveThreadGCTask(bool on_c_heap):
GCTask(GCTask::Kind::inactive_thread_task),
    _is_c_heap_obj(on_c_heap) {
    // Nothing to do.
  }
  // Destructor-like method.
  void destruct();
  // Accessors.
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
};

// A BarrierGCTask blocks other tasks from starting, 
// and waits until it is the only task running.
class BarrierGCTask : public GCTask {
private:
  long _first_blocked_tick;
  long _last_blocked_tick;
  long _total_ticks;
  long _workers;
public:
const char*name(){return"BarrierGCTask";}
  // Factory create and destroy methods.
  static BarrierGCTask* create() {
    return new BarrierGCTask();
  }
  static void destroy(BarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
  virtual void worker_hit_barrier();
  long first_blocked_tick()   { return _first_blocked_tick; }
  long average_blocked_tick() { return _total_ticks / _workers; }
  long last_blocked_tick()    { return _last_blocked_tick; }
  long blocked_workers()      { return _workers; }
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  BarrierGCTask() :
    GCTask(GCTask::Kind::barrier_task) {
    _first_blocked_tick = _last_blocked_tick = _total_ticks = _workers = 0;
  }
  // Destructor-like method.
  void destruct();
  // Methods.
  //     Wait for this to be the only task running.
  void do_it_internal(GCTaskManager* manager, uint which);
};

// A ReleasingBarrierGCTask is a BarrierGCTask 
// that tells all the tasks to release their resource areas.
class ReleasingBarrierGCTask : public BarrierGCTask {
public:

const char*name(){return"ReleasingBarrierGCTask";}

  // Factory create and destroy methods.
  static ReleasingBarrierGCTask* create() {
    return new ReleasingBarrierGCTask();
  }
  static void destroy(ReleasingBarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  ReleasingBarrierGCTask() :
    BarrierGCTask() {
    // Nothing to do.
  }
  // Destructor-like method.
  void destruct();
}; 

// A NotifyingBarrierGCTask is a BarrierGCTask 
// that calls a notification method when it is the only task running.
class NotifyingBarrierGCTask : public BarrierGCTask {
private:
  // Instance state.
  NotifyDoneClosure* _ndc;              // The callback object.
public:
const char*name(){return"NotifyingBarrierGCTask";}

  // Factory create and destroy methods.
  static NotifyingBarrierGCTask* create(NotifyDoneClosure* ndc) {
    return new NotifyingBarrierGCTask(ndc);
  }
  static void destroy(NotifyingBarrierGCTask* that) {
    if (that != NULL) {
      that->destruct();
      delete that;
    }
  }
  // Methods from GCTask.
  void do_it(GCTaskManager* manager, uint which);
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  NotifyingBarrierGCTask(NotifyDoneClosure* ndc) :
    BarrierGCTask(),
    _ndc(ndc) {
    assert(notify_done_closure() != NULL, "can't notify on NULL");
  }
  // Destructor-like method.
  void destruct();
  // Accessor.
  NotifyDoneClosure* notify_done_closure() const { return _ndc; }
};

// A WaitForBarrierGCTask is a BarrierGCTask 
// with a method you can call to wait until 
// the BarrierGCTask is done.
// This may cover many of the uses of NotifyingBarrierGCTasks.
class WaitForBarrierGCTask : public BarrierGCTask {
private:
  // Instance state.
WaitLock*_monitor;//Guard and notify changes.
  bool       _should_wait;              // true=>wait, false=>proceed.
  const bool _is_c_heap_obj;            // Was allocated on the heap.
public:
virtual const char*name(){return(char*)"waitfor-barrier-task";}

  // Factory create and destroy methods.
  static WaitForBarrierGCTask* create();
  static WaitForBarrierGCTask* create_on_c_heap();
  static void destroy(WaitForBarrierGCTask* that);
  // Methods.
  void     do_it(GCTaskManager* manager, uint which);
  void     wait_for();
protected:
  // Constructor.  Clients use factory, but there might be subclasses.
  WaitForBarrierGCTask(bool on_c_heap);
  // Destructor-like method.
  void destruct();
  // Accessors.
WaitLock*monitor()const{
    return _monitor;
  }
  bool should_wait() const {
    return _should_wait;
  }
  void set_should_wait(bool value) {
    _should_wait = value;
  }
  bool is_c_heap_obj() {
    return _is_c_heap_obj;
  }
};

class MonitorSupply : public AllStatic {
private:
  // State.
  //     The list of available Monitor*'s.
static GrowableArray<WaitLock*>*_freelist;
public:
  // Reserve a WaitLock*.
static WaitLock*reserve();
  // Release a WaitLock*.
static void release(WaitLock*instance);
private:
  // Accessors.
static GrowableArray<WaitLock*>*freelist(){
    return _freelist;
  }
};

#endif // GCTASKMANAGER_HPP
