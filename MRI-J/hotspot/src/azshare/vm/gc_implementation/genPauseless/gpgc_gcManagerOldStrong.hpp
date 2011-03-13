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
#ifndef GPGC_GCMANAGEROLDSTRONG_HPP
#define GPGC_GCMANAGEROLDSTRONG_HPP


#include "codeBlob.hpp"
#include "gpgc_gcManagerOld.hpp"
#include "gpgc_threadRefLists.hpp"
#include "growableArray.hpp"
#include "klass.hpp"
#include "taskqueue.hpp"
class GPGC_GCManagerOldStrong;
class HeapRefBuffer;
class HeapRefBufferList;
class Klass;

class GPGC_OldGC_MarkPushClosure: public OopClosure
{
  private:
    GPGC_GCManagerOldStrong* _gcm;
    bool                     _do_derived_oops;
  public:
    GPGC_OldGC_MarkPushClosure(GPGC_GCManagerOldStrong* gcm) : _gcm(gcm), _do_derived_oops(false) {}
    void activate_derived_oops()   { _do_derived_oops = true; }
    void deactivate_derived_oops() { _do_derived_oops = false; }
    bool check_derived_oops()      { return _do_derived_oops; }
    void do_oop(objectRef* p);
    void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr);
};


class GPGC_GCManagerOldStrong: public GPGC_GCManagerOld
{
  private:
static GPGC_GCManagerOldStrong**_manager_array;
    static long                      _manager_count;
    static HeapRefBufferList**       _free_ref_buffer_list;
    static HeapRefBufferList**       _full_ref_buffer_list;
    static HeapRefBufferList**       _full_mutator_ref_buffer_list;

    static GenericTaskQueueSet<Klass*>*     _revisit_array;

    static GenericTaskQueueSet<Klass*>*     revisit_array() { return _revisit_array; }


  public:
    static void                      initialize();
    static void                      push_mutator_stack_to_full(HeapRefBuffer** buffer);
    static HeapRefBuffer*            pop_mutator_stack_from_full() { return pop_heap_ref_buffer(_full_mutator_ref_buffer_list); }

    static HeapRefBufferList**       free_ref_buffer_list()    { return _free_ref_buffer_list; }
    static HeapRefBufferList**       full_ref_buffer_list()    { return _full_ref_buffer_list; }
    static HeapRefBufferList**       full_mutator_ref_buffer_list()  { return _full_mutator_ref_buffer_list; }

    static GPGC_GCManagerOldStrong*  get_manager(long manager) { assert0(manager>=0 && manager<_manager_count);
                                                                 return _manager_array[manager];                }

    static HeapRefBuffer*            alloc_heap_ref_buffer()   { return alloc_stack(_free_ref_buffer_list); }

    static void                      pre_mark();
    static void                      post_relocate();

    static bool                      steal_revisit_klass(int queue_num, int* seed, Klass*& t);

    static void                      reset_reference_lists();
    static void                      save_reference_lists();

    // Debugging:
    static void                      verify_mutator_ref_buffer(HeapRefBuffer* ref_buffer);

    static void                      ensure_all_stacks_are_empty(const char* tag);
    static void                      ensure_revisit_stacks_are_empty();
    static void                      ensure_mutator_list_is_empty(const char* tag);
    static void                      verify_empty_ref_lists();
static void oops_do_ref_lists(OopClosure*f);

    static long                      count_free_ref_buffers() { return count_heap_ref_buffers(_free_ref_buffer_list); }


  private:
    GPGC_ThreadRefLists              _reference_lists;

    GPGC_OldGC_MarkPushClosure       _mark_push_closure;

    // Klass revisiting:
    GenericTaskQueue<Klass*>         _revisit_klass_stack;
GrowableArray<Klass*>*_revisit_overflow_stack;

    // Implementor adjusting:
    GrowableArray<instanceKlass*>*   _dec_implementors_stack;

    GPGC_GCManagerOldStrong(long manager_number);


  public:
    HeapRefBuffer*                   get_mutator_stack()       { return pop_heap_ref_buffer(_full_mutator_ref_buffer_list); }

    GPGC_ThreadRefLists*             reference_lists()         { return &_reference_lists; }

    GPGC_OldGC_MarkPushClosure*      mark_and_push_closure()   { return &_mark_push_closure; }

    GenericTaskQueue<Klass*>*        revisit_klass_stack()     { return &_revisit_klass_stack; }
GrowableArray<Klass*>*revisit_overflow_stack(){return _revisit_overflow_stack;}
    void                             drain_revisit_stack();
    void                             revisit_klass(Klass* k)   { k->GPGC_follow_weak_klass_links(); }

    GrowableArray<instanceKlass*>*   dec_implementors_stack()  { return _dec_implementors_stack; }

    inline void                      revisit_weak_klass_link(Klass* k);

    inline void                      mark_and_follow(objectRef* p,   int referrer_kid);
    inline void                      mark_through   (objectRef  ref, int referrer_kid);

    virtual long                     manager_count ()             { return _manager_count; }
    virtual GPGC_GCManagerMark*      lookup_manager(long manager) { return GPGC_GCManagerOldStrong::get_manager(manager); }

    virtual void                     update_live_referent      (objectRef* referent_addr, int referrer_kid);
    virtual void                     mark_and_push_referent    (objectRef* referent_addr, int referrer_kid);
    virtual void                     push_referent_without_mark(objectRef* referent_addr, int referrer_kid);

    virtual long                     manager_type()  { return TypeOldStrong; }
};


inline void GPGC_GCManagerOldStrong::revisit_weak_klass_link(Klass* k)
{
  if ( ! revisit_klass_stack()->push(k) ) {
    revisit_overflow_stack()->push(k);
  }
}
#endif
