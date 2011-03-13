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
#ifndef GPGC_VERIFYCLOSURE_HPP
#define GPGC_VERIFYCLOSURE_HPP


#include "gpgc_collector.hpp"
#include "gpgc_markingQueue.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_readTrapArray.hpp"

#include "gcTaskManager.hpp"
#include "timer.hpp"

class GCTask;

class GPGC_VC_PerThread {
public:
  GPGC_MarkingQueue::RefBlock* _drain_stack;
  long                         _fill_stack_top;
  GPGC_MarkingQueue::RefBlock* _fill_stack;
  long                         _ref_counter;
};


// This closure is used to verify objects in the heap are properly marked by the
// collector.  A full heap mark is done, and we verify that all objects found by
// the verifier are also marked live by the collector, unless they're in a space
// being ignored.
class GPGC_VerifyClosure: public WeakOopClosure
{
class IsAliveClosure:public BoolObjectRefClosure{
    public:
      void do_object(oop p)         { ShouldNotReachHere(); }
      bool do_object_b(oop p)       { ShouldNotReachHere(); return false; }
bool do_object_b(objectRef r){
                                      assert0(r.is_heap());
                                      if (GPGC_ReadTrapArray::is_remap_trapped(r)) {
                                        r = GPGC_Collector::get_forwarded_object(heapRef(r));
                                      }
oop obj=r.as_oop();
                                      if (GPGC_VerifyClosure::is_verify_marked(obj)) {
                                        if (r.is_new()) {
                                          if (!GPGC_VerifyClosure::ignore_new()) {
                                            PageNum        page  = GPGC_Layout::addr_to_BasePageForSpace(obj);
                                            GPGC_PageInfo* info  = GPGC_PageInfo::page_info(page);
                                            uint64_t       flags = info->flags();

                                            if ( (flags&GPGC_PageInfo::NoRelocateTLABFlags) == GPGC_PageInfo::NoRelocateTLABFlags ) {
                                              // Objects in NoRelocate TLABs aren't marked, because the allocation millicode doesn't mark them.
                                              // Do nothing.
                                            } else {
                                              guarantee(GPGC_Marks::is_any_marked_strong_live(r.as_oop()), "NewGen ref not marked by collector");
                                            }
                                          }
                                        } else {
                                          guarantee(GPGC_VerifyClosure::ignore_old() ||
                                                    GPGC_Marks::is_any_marked_strong_live(r.as_oop()),
"OldGen ref not marked by collector");
                                        }
                                      }
                                      // We always return true, else verification may clear dead oops.
                                      return true;
                                    }
  };

class WorkList:public StackObj{
    public:
      GPGC_MarkingQueue  marking_queue;
      HeapWord           cache_pad[WordsPerCacheLine-1];
  };

  private:
    enum {
      MAX_WORK_LISTS=16
    };

    static volatile long                _working;
    static GCTask* volatile             _gc_task_stack;
    static GCTaskManager*               _verify_task_manager;
    static long                         _active_workers; 
    static long                         _wakeup_count;

    static GPGC_VC_PerThread*           _per_thread;

    static WorkList                     _work_lists[MAX_WORK_LISTS];

    static bool                         _ignore_new;
    static bool                         _ignore_old;

static klassOop _constantPoolKlassOop;

    static IsAliveClosure               _is_alive;

    static void                         reset             ();

    static void                         run_work_stack    ();
    static void                         complete_work     (uint which);
    static void                         push_work_stack   (GCTask* task);
    static GCTask*                      pop_work_stack    ();
                        
    static void                         increment_working ();
    static void                         decrement_working ();

    static GPGC_MarkingQueue::RefBlock* pop_marking_stack ();
    static void                         push_marking_stack(GPGC_MarkingQueue::RefBlock* stack);

  public:
    static void                         initialize        ();
    static void                         verify_marking(TimeDetailTracer* tdt, bool ignore_new, bool ignore_old, const char* gc_tag);

    static void                         task_thread_loop(GCTaskThread* thread, uint which);

    static IsAliveClosure*              is_alive          ()             { return &_is_alive; }
    static intptr_t*                    working_addr      ()             { return (intptr_t*) &_working; }

    static bool                         ignore_new        ()             { return _ignore_new; }
    static bool                         ignore_old        ()             { return _ignore_old; }

    static bool                         is_verify_marked  (oop obj);
    static bool                         set_verify_mark   (oop obj);

    static bool                         verify_empty_stacks();

  private:

GPGC_VC_PerThread*_data;

    void                                follow_ref(objectRef ref);
    void                                push_to_verify_stack(objectRef ref);

  public:
    GPGC_VerifyClosure(long which) { _data = &_per_thread[which]; }

    bool                                get_marking_stack ();
    void                                drain_stack       ();

    void                                do_oop            (objectRef* p);
    void                                do_derived_oop    (objectRef* base_ptr, objectRef* derived_ptr);
void do_weak_oop(objectRef*p);
};

#endif // GPGC_VERIFYCLOSURE_HPP

