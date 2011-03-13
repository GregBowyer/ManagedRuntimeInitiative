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
#ifndef GPGC_GCMANAGERNEWSTRONG_HPP
#define GPGC_GCMANAGERNEWSTRONG_HPP



#include "codeBlob.hpp"
#include "gpgc_gcManagerNew.hpp"
#include "gpgc_threadRefLists.hpp"
#include "iterator.hpp"


class GPGC_GCManagerNewStrong;

class GPGC_NewGC_VerifyOldRefsClosure: public OopClosure
{
  protected:
    GPGC_GCManagerNewStrong* _gcm;
  public:
    GPGC_NewGC_VerifyOldRefsClosure(GPGC_GCManagerNewStrong* gcm) : _gcm(gcm) {}
    void do_oop(objectRef* p) {
      objectRef ref = PERMISSIVE_UNPOISON(*p, p);
      assert0(ref.is_null() || ref.is_old());
    }
};


class GPGC_NewGC_MarkPushClosure: public OopClosure
{
  protected:
    GPGC_GCManagerNewStrong* _gcm;
    bool                     _do_derived_oops;
  public:
    GPGC_NewGC_MarkPushClosure(GPGC_GCManagerNewStrong* gcm) : _gcm(gcm), _do_derived_oops(false) {}
    void activate_derived_oops  ()                { _do_derived_oops = true; }
    void deactivate_derived_oops()                { _do_derived_oops = false; }
    bool check_derived_oops     ()                { return _do_derived_oops; }
    void do_oop                 (objectRef* p);
    void do_derived_oop         (objectRef* base_ptr, objectRef* derived_ptr);
};


class GPGC_NewGC_RootMarkPushClosure: public GPGC_NewGC_MarkPushClosure
{
  public:
    GPGC_NewGC_RootMarkPushClosure(GPGC_GCManagerNewStrong* gcm) : GPGC_NewGC_MarkPushClosure(gcm) {}
    void do_oop(objectRef* p);
    void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr);
};


class GPGC_NewToOldGC_RootMarkPushClosure: public GPGC_NewGC_MarkPushClosure
{
  public:
    GPGC_NewToOldGC_RootMarkPushClosure(GPGC_GCManagerNewStrong* gcm) : GPGC_NewGC_MarkPushClosure(gcm) {}
    void do_oop(objectRef* p);
    void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr);
};


class GPGC_GCManagerNewStrong: public GPGC_GCManagerNew
{
  private:
static GPGC_GCManagerNewStrong**_manager_array;
    static long                      _manager_count;
    static HeapRefBufferList**       _free_ref_buffer_list;
    static HeapRefBufferList**       _full_ref_buffer_list;
    static HeapRefBufferList**       _full_mutator_ref_buffer_list;


  public:
    static void                      initialize();
    static void                      push_mutator_stack_to_full(HeapRefBuffer** buffer);

    static GPGC_GCManagerNewStrong*  get_manager(long manager)   { assert0(manager>=0 && manager<_manager_count);
                                                                   return _manager_array[manager];                }
    static HeapRefBufferList**       full_mutator_ref_buffer_list()  { return _full_mutator_ref_buffer_list; }

    static HeapRefBuffer*            alloc_heap_ref_buffer()     { return alloc_stack(_free_ref_buffer_list); }

    static void                      flush_nto_marking_stacks();

    static void                      reset_reference_lists();
    static void                      save_reference_lists();

    // Debuggging:
    static void                      verify_mutator_ref_buffer(HeapRefBuffer* ref_buffer);

    static void                      ensure_all_stacks_are_empty(const char* tag);
    static void                      ensure_all_nto_stacks_are_empty(const char* tag);
    static void                      ensure_mutator_list_is_empty(const char* tag);
    static void                      verify_empty_ref_lists();
static void oops_do_ref_lists(OopClosure*f);

    static long                      count_free_ref_buffers() { return count_heap_ref_buffers(_free_ref_buffer_list); }


  private:
    HeapRefBuffer*                      _new_to_old_marking_stack;
    GPGC_ThreadRefLists                 _reference_lists;

    GPGC_NewGC_MarkPushClosure          _mark_push_closure;
    GPGC_NewGC_RootMarkPushClosure      _new_root_mark_push_closure;
    GPGC_NewToOldGC_RootMarkPushClosure _nto_root_mark_push_closure;


    GPGC_GCManagerNewStrong(long manager_number);


  public:
    HeapRefBuffer*              get_mutator_stack()           { return pop_heap_ref_buffer(_full_mutator_ref_buffer_list); }

    GPGC_ThreadRefLists*        reference_lists()             { return &_reference_lists; }

    GPGC_NewGC_MarkPushClosure* mark_and_push_closure()       { return &_mark_push_closure; }
    GPGC_NewGC_MarkPushClosure* new_root_mark_push_closure()  { return &_new_root_mark_push_closure; }
    GPGC_NewGC_MarkPushClosure* nto_root_mark_push_closure()  { return &_nto_root_mark_push_closure; }

    inline void                 push_ref_to_nto_stack(objectRef ref, int referrer_kid);

    inline void                 mark_and_follow(objectRef* p,   int referrer_kid);
    inline void                 mark_through   (objectRef  ref, int referrer_kid);

    virtual long                manager_count ()              { return _manager_count; }
    virtual GPGC_GCManagerMark* lookup_manager(long manager)  { return GPGC_GCManagerNewStrong::get_manager(manager); }

    virtual void                update_live_referent      (objectRef* referent_addr, int referrer_kid);
    virtual void                mark_and_push_referent    (objectRef* referent_addr, int referrer_kid);
    virtual void                push_referent_without_mark(objectRef* referent_addr, int referrer_kid);

    virtual long                manager_type()  { return TypeNewStrong; }
};

#endif // GPGC_GCMANAGERNEWSTRONG_HPP
