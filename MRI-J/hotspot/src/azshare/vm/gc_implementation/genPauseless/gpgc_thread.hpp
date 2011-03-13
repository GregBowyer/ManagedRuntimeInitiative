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

#ifndef GPGC_THREAD_HPP
#define GPGC_THREAD_HPP

#include "gpgc_collector.hpp"
#include"gpgc_operation.hpp"

#include "gcCause.hpp"
#include "thread.hpp"

class GPGC_AllocOperation;
class GPGC_AsyncCycleOperation;
class GPGC_CycleOperation;
class GPGC_Operation;
class GPGC_OperationQueue;


class GPGC_Cycle:public CHeapObj{
  private:
    bool                 _gc_cycle;
    bool                 _clear_all;
GCCause::Cause _cause;
    GPGC_OperationQueue* _cycle_op_queue;
    GPGC_OperationQueue* _alloc_op_queue;

    // support for JVMTI/HeapDumper heap iteration
    SafepointEndCallback _safepoint_end_callback;
    void*                _user_data;
    bool                 _heap_iterate_collection;

  public:
    GPGC_Cycle();

    bool                 gc_cycle          ()  { return _gc_cycle; }
    bool                 clear_all         ()  { return _clear_all; }
GCCause::Cause cause(){return _cause;}
    GPGC_OperationQueue* cycle_op_queue    ()  { return _cycle_op_queue; }
    GPGC_OperationQueue* alloc_op_queue    ()  { return _alloc_op_queue; }

    void                 reset             ();
    void                 request_full_gc   (GCCause::Cause cause, bool clear_all);
    void                 cycle_complete    ();
    void                 set_alloc_op_queue(GPGC_OperationQueue* q);

    // support for JVMTI heap iteration
    void                 set_heap_iterate_collection()  { _heap_iterate_collection = true; }
    bool                 is_heap_iterate_collection ()  { return _heap_iterate_collection; }

    SafepointEndCallback safepoint_end_callback      () { return _safepoint_end_callback; }
    void set_safepoint_end_callback(SafepointEndCallback sfpt_end_callback) { _safepoint_end_callback = sfpt_end_callback; }
    void* user_data                                  () { return _user_data; }
    void set_user_data                               (void* user_data) { _user_data = user_data; }
};


class GPGC_Thread:public Thread{
  private:
    static GPGC_Thread*  _new_gc_thread;
    static GPGC_Thread*  _old_gc_thread;

    static bool          _should_terminate;

    static bool          _could_be_running_constant_gc;

    static GPGC_Cycle*   _full_gc_in_progress;
    static GPGC_Cycle*   _full_gc_pending;

    static bool          should_run_full_gc();

    static void          request_full_gc   (GCCause::Cause cause, bool clear_all);
    static void          request_full_gc   (GPGC_AllocOperation* alloc_op);
    static void          request_full_gc   (GCCause::Cause cause, bool clear_all, GPGC_CycleOperation* cycle_op);

  public:
    static void          static_initialize ();

    static void          start             (long collector);
    static void          stop_all          ();
    
    static GPGC_Thread*  new_gc_thread     ()                       { return _new_gc_thread; }
    static GPGC_Thread*  old_gc_thread     ()                       { return _old_gc_thread; }

    static void          set_new_gc_thread (GPGC_Thread* gc_thread) { _new_gc_thread = gc_thread; }
    static void          set_old_gc_thread (GPGC_Thread* gc_thread) { _old_gc_thread = gc_thread; }

    static bool          should_terminate  ()                       { return _should_terminate; }

    // GPGC_Operation support for other threads needing GC cycles.
    static void          run_alloc_operation          (GPGC_AllocOperation* op);
    static void          run_sync_gc_cycle_operation  (GCCause::Cause cause, bool clear_all, GPGC_CycleOperation* op);
    static void          run_async_gc_cycle           (GCCause::Cause cause, bool clear_all);

    // GPGC cycle support for the GPGC heuristic
    static void          heuristic_demands_full_gc    ();
    static void          heuristic_wants_full_gc      (bool clear_all);

    static GPGC_Cycle*   full_gc_in_progress()   { return _full_gc_in_progress; }
    static GPGC_Cycle*   full_gc_pending    ()   { return _full_gc_pending;     }

  private:
    long                 _collector;

    volatile bool        _is_blocked;         // Indicate when blocked, to coordinate VM shutdown.

    // stats for tracking mutator threads blocked on GC.
    long                 _waiters_base_time;
    long                 _waiters_earliest_start_time;
    long                 _waiters_total_start_offset;
    long                 _waiters_thread_count;

    // Pre-allocated page for object relocation:
    long                 _thread_number;
    PageNum              _preallocated_page;

    GPGC_Thread(long collector);
    virtual ~GPGC_Thread() {}

    void reset_waiter_stats();
    void count_op_time     (GPGC_Operation* op);
    void log_waiter_stats  (char* cycle_label);

    void must_allocate     (GPGC_AllocOperation* op);
    bool try_allocate      (GPGC_AllocOperation* op);

    void must_allocate     (GPGC_OperationQueue* alloc_op_queue);
    GPGC_OperationQueue* try_allocate (GPGC_OperationQueue* alloc_op_queue);
    
    void add_alloc_op_queue(GPGC_Cycle* cycle, GPGC_OperationQueue* failed_op_queue);

  public:
    inline bool is_new_collector()           { return _collector == GPGC_Collector::NewCollector; }
    inline bool is_old_collector()           { return _collector == GPGC_Collector::OldCollector; }

    // Blocks self, allowing VM thread to finish VM exit operations 
    void set_blocked        (bool state)     { _is_blocked = state;  if( os::is_MP() ) Atomic::membar(); }
    bool is_blocked         ()               { return _is_blocked; }
    void block_if_vm_exited ();

    void run_new_gc_cycle(SafepointEndCallback safepoint_end_callback, void* user_data);
    void run_full_gc_cycle(SafepointEndCallback safepoint_end_callback, void* user_data);

    bool is_GC_thread() const             { return true; }
    bool is_GenPauselessGC_thread() const { return true; }

    void run();

    void new_collector_loop();
    void old_collector_loop();

    // Pre-allocated page for object relocation.
    long    thread_type()                       { return _collector; }
    long    thread_number()                     { return _thread_number; }
    void    set_preallocated_page(PageNum page) { _preallocated_page = page; }
    PageNum get_preallocated_page()             { return _preallocated_page; }
};

#endif // GPGC_THREAD_HPP
