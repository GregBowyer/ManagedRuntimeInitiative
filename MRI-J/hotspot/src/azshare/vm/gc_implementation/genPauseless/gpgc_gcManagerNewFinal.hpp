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
#ifndef GPGC_GCMANAGERNEWFINAL_HPP
#define GPGC_GCMANAGERNEWFINAL_HPP

#include "gpgc_gcManagerNew.hpp"

class GPGC_GCManagerNewFinal: public GPGC_GCManagerNew {
  private:
static GPGC_GCManagerNewFinal**_manager_array;
    static long                     _manager_count;
    static HeapRefBufferList**      _free_ref_buffer_list;
    static HeapRefBufferList**      _full_ref_buffer_list;


  public:
    static void                     initialize();
    static GPGC_GCManagerNewFinal*  get_manager(long manager) { assert0(manager>=0 && manager<_manager_count);
                                                                return _manager_array[manager];                }

    static bool                     all_stacks_are_empty();

    // Debugging:
    static void                     ensure_all_stacks_are_empty(const char* tag);

    static long                     count_free_ref_buffers() { return count_heap_ref_buffers(_free_ref_buffer_list); }


  private:
    GPGC_GCManagerNewFinal(long manager_number)
      : GPGC_GCManagerNew(manager_number, _free_ref_buffer_list, _full_ref_buffer_list) {}


  public:
    HeapRefBuffer*              get_mutator_stack()         { return NULL; }

    inline void                 mark_and_follow(objectRef* p,   int referrer_kid);
    inline void                 mark_through   (objectRef  ref, int referrer_kid);

    virtual long                manager_count ()             { return _manager_count; }
    virtual GPGC_GCManagerMark* lookup_manager(long manager) { return GPGC_GCManagerNewFinal::get_manager(manager); }

    virtual void                update_live_referent      (objectRef* referent_addr, int referrer_kid) { ShouldNotReachHere(); }
    virtual void                mark_and_push_referent    (objectRef* referent_addr, int referrer_kid) { ShouldNotReachHere(); }
    virtual void                push_referent_without_mark(objectRef* referent_addr, int referrer_kid) { ShouldNotReachHere(); }

    virtual long                manager_type()  { return TypeNewFinal; }
};

#endif
