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
#ifndef GPGC_OPERATION_HPP
#define GPGC_OPERATION_HPP


#include "allocation.hpp"

class GPGC_Operation:public CHeapObj{
  private:
Thread*_calling_thread;
    long            _start_time;
GPGC_Operation*_next;
GPGC_Operation*_prev;

  public:
    GPGC_Operation();

    Thread* calling_thread()                      { return _calling_thread; }
void set_calling_thread(Thread*thread){_calling_thread=thread;}

    long    start_time()                          { return _start_time; }

GPGC_Operation*next(){return _next;}
    GPGC_Operation* prev()                        { return _prev; }
void set_next(GPGC_Operation*next){_next=next;}
void set_prev(GPGC_Operation*prev){_prev=prev;}

    virtual bool is_cheap_allocated() const       { return false; }

    virtual bool is_alloc_operation()             { return false; }
    virtual bool is_cycle_operation()             { return false; }

    static void  complete_operation(GPGC_Operation* op);
};


class GPGC_AllocOperation: public GPGC_Operation {
  private:
    size_t    _word_size;
    HeapWord* _result;
    long      _result_gc_count;

  public:
    GPGC_AllocOperation(size_t word_size)     { _word_size=word_size; _result=NULL; _result_gc_count=0; }

    virtual bool      is_alloc_operation()    { return true; }

    virtual HeapWord* allocate() = 0;

    size_t    word_size()                     { return _word_size; }
    HeapWord* result()                        { return _result; }
    long      result_gc_count()               { return _result_gc_count; }

    void      set_result(HeapWord* result)    { _result = result; }
    void      set_result_gc_count(long count) { _result_gc_count = count; }
};


// This operation is for when a thread fails to allocate in New Generation.
class GPGC_NewAllocOperation: public GPGC_AllocOperation {
  private:
    bool   _is_tlab;

  public:
    GPGC_NewAllocOperation(size_t word_size, bool is_tlab) : GPGC_AllocOperation(word_size) { _is_tlab=is_tlab; }

    bool is_tlab() { return _is_tlab; }

HeapWord*allocate();
};


// This operation is for when a thread fails to allocate in Perm Generation.
class GPGC_PermAllocOperation: public GPGC_AllocOperation {
  public:
    GPGC_PermAllocOperation(size_t word_size) : GPGC_AllocOperation(word_size) {}

HeapWord*allocate();
};


// This operation is for when a thread requests a GC cycle, and waits for it to complete.
class GPGC_CycleOperation: public GPGC_Operation {
  public:
    enum MaxGCFlags {
      RunFullGC = 0,
      RunMaxGC  = 1
    };
    enum ConcurrentMarkFlags {
      StopTheWorldMarking = 0,
      ConcurrentMarking = 1
    };
    enum SystemGCCauses {
      SystemGCCalled = 0,
      ProxyCallback = 1
    };

  private:
    bool _want_full_gc;
    bool _concurrent_marking;

    // support for JVMTI heap iteration
    SafepointEndCallback _safepoint_end_callback;
    void*                _user_data;

  public:
    GPGC_CycleOperation(bool want_full_gc, bool concurrent_marking)
         :  _want_full_gc(want_full_gc), _concurrent_marking(concurrent_marking), _safepoint_end_callback(NULL), _user_data(NULL)
         {}

    bool is_cheap_allocated() const                  { return true; }

    bool is_cycle_operation()                        { return true; }
    bool want_full_gc()                              { return _want_full_gc; }
    bool concurrent_marking()                        { return _concurrent_marking; }

    // support for JVMTI heap iteration
    SafepointEndCallback safepoint_end_callback()    { return _safepoint_end_callback; }
    void set_safepoint_end_callback(SafepointEndCallback sfpt_end_callback) { _safepoint_end_callback = sfpt_end_callback; }
    void* user_data()                                { return _user_data; }
    void set_user_data(void* user_data)              { _user_data = user_data; }
};


// This operation is for when a thread requests a GC cycle, and doesn't wait for it to complete.
class GPGC_AsyncCycleOperation: public GPGC_CycleOperation {
  public:
    GPGC_AsyncCycleOperation(bool want_full_gc) : GPGC_CycleOperation(want_full_gc, ConcurrentMarking) {}
};


class GPGC_OperationQueue:public CHeapObj{
  private:
    long            _queue_length;
    long            _queue_counter;
GPGC_Operation*_queue;

  public:
    GPGC_OperationQueue ();
    ~GPGC_OperationQueue();

    bool            is_empty    ();
    bool            not_empty   ();
    void            add         (GPGC_Operation* op);
    GPGC_Operation* remove_next ();
};

#endif // GPGC_OPERATION_HPP

