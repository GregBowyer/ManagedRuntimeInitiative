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
#ifndef PGCTASKMANAGER_HPP
#define PGCTASKMANAGER_HPP


#include "allocation.hpp"

class PGCTask;
class PGCTaskQueue;
class PGCTaskManager;

class PGCTaskThread;
class WaitLock;
class ThreadClosure;

class PGCTask:public ResourceObj{
  private:
    // Instance state.
    PGCTask*          _next;              // Tasks are on singly-linked ... 
  public:
    virtual const char* name() = 0;

    // Abstract do_it method
    virtual void do_it(uint64_t which) = 0;
    // Accessors
PGCTask*next()const{
      return _next;
    }
void set_next(PGCTask*next){
      _next = next;
    }
    void print(const char* message) const PRODUCT_RETURN;
  protected:
    // Constructors: Only create subclasses.
    //     An ordinary PGCTask.
    PGCTask();
    virtual void destruct();
    // Methods.
    void initialize();
};


// The list is not synchronized, because sometimes we want to 
// build up a list and then make it available to other threads.
// See also: SynchronizedGCTaskQueue.
class PGCTaskQueue:public ResourceObj{
private:
  // Instance state.
  PGCTask*   _head;               // Tasks are enqueued at this end.
  const bool _is_c_heap_obj;            // Is this a CHeapObj?
public:
  // Factory create and destroy methods.
  //     Create as ResourceObj.
static PGCTaskQueue*create();
  //     Create as CHeapObj.
static PGCTaskQueue*create_on_c_heap();
  //     Destroyer.
static void destroy(PGCTaskQueue*that);
  // Accessors.
PGCTask*head()const{
    return _head;
  }
  //     These just examine the state of the queue.
  bool is_empty() const {
return(head()==NULL);
  }

  // Methods.
  //     Enqueue one task.
void enqueue(PGCTask*task);
  //     Enqueue a list of tasks.  Empties the argument list.
void enqueue(PGCTaskQueue*list);
  // grab a task from the queue; use CAS to grab 
  PGCTask* grab();
  void initialize();
protected:
  // Constructor. Clients use factory, but there might be subclasses.
PGCTaskQueue(bool on_c_heap);
  // Destructor-like method. 
  // Because ResourceMark doesn't call destructors.
  // This method cleans up like one.
  virtual void destruct();
  // Accessors.
  void set_head(PGCTask* value) {
_head=value;
  }
  bool is_c_heap_obj() const {
    return _is_c_heap_obj;
  }
  // Methods.
  void print(const char* message) const PRODUCT_RETURN;
};


// Top level TaskManager for PGC/GPGC
class PGCTaskManager:public CHeapObj{
  private:
    // Instance state.
    intptr_t                  _workers;               // Total number of workers.
    intptr_t                  _active_threads;        // Number of workers to use for tasks.
    WaitLock*                 _start_monitor;         // Notification of changes.
    WaitLock*                 _notify_monitor;        // Notification of changes.
    PGCTaskQueue*             _queue;                 // a unsynchronized queue of tasks.
PGCTaskThread**_thread;//Array of worker threads.
    intptr_t                  _wakeup_count;          // sync count between threads and manager
    DEBUG_ONLY (
        intptr_t                  _delivered_tasks;       // Count of delivered tasks.
        intptr_t                  _completed_tasks;       // Count of completed tasks.
        intptr_t                  _emptied_queue;         // Times we emptied the queue.
        intptr_t                  _lists_added;           // no of lists added 
    )

  public:
    // Factory create and destroy methods.
static PGCTaskManager*create(uint workers){
return new PGCTaskManager(workers);
    }
static void destroy(PGCTaskManager*that){
      if (that != NULL) {
        delete that;
      }
    }
    // Accessors.
    intptr_t workers() const {
      return _workers;
    }
    intptr_t active_threads() const {
      return _active_threads;
    }
    WaitLock* start_monitor() const {
      return _start_monitor;
    }
    WaitLock* notify_monitor() const {
      return _notify_monitor;
    } 
PGCTaskQueue*queue()const{
      return _queue;
    }
    DEBUG_ONLY (
        //     Count of tasks delivered to workers.
        intptr_t delivered_tasks() const {
           return _delivered_tasks;
        }
        //     Count of how many times the queue has emptied.
        intptr_t emptied_queue() const {
          return _emptied_queue;
        }
        //     Count of tasks completed by workers.
        intptr_t completed_tasks() const {
          return _completed_tasks;
        } 
        intptr_t lists_added() const {
          return _lists_added;
        } 
 
        void reset_completed_tasks() {
          _completed_tasks = 0;
        }
        void reset_delivered_tasks() {
          _delivered_tasks = 0;
        }
        void reset_emptied_queue() {
          _emptied_queue = 0;
        }
        void reset_lists_added() {
          _lists_added = 0;
        }
        void increment_delivered_tasks(); 
        void increment_lists_added() { _lists_added++; } 
        void increment_completed_tasks(); 
        void increment_emptied_queue();
    )

    intptr_t wakeup_count() const {
      return _wakeup_count;
    }

    // Methods.
    //     Change the number of active worker threads.
    void set_active_threads(uint active);
    //     Add a list of tasks.  Removes task from the argument list.
void add_list(PGCTaskQueue*list);
    //     Claim a task for argument worker.
    PGCTask* get_task(uint64_t which);
    // Request that all GCTaskThreads switch to a new trap table.
    void request_new_gc_mode(bool gc_mode);
    //     Note the completion of a task by the argument worker.
    void note_completion(uint64_t which);
    void print_task_time_stamps();
    void print_threads_on(outputStream* st);
    void threads_do(ThreadClosure* tc);
    // Constructors.  Clients use factory, but there might be subclasses.
    //     Create a GCTaskManager with the appropriate number of workers.
PGCTaskManager(uint workers);
    //     Make virtual if necessary.
    ~PGCTaskManager();
    //     Bounds-checking per-thread data accessors.
    PGCTaskThread* thread(uint64_t which);
    void set_thread(uint64_t which, PGCTaskThread* value);

   void increment_wakeup_count() { _wakeup_count++; }
    void decrement_active_threads () {
      _active_threads--; // done inside a lock; no need for atomic-dec
    }
    // Other methods.
    void log_perf_counters();
    void reset_perf_counters(); 
    void initialize();
};

#endif // PGCTASKMANAGER_HPP
