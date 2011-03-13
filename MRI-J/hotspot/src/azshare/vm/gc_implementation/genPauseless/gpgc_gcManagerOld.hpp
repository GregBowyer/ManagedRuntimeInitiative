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

#ifndef GPGC_GCMANAGEROLD_HPP
#define GPGC_GCMANAGEROLD_HPP
#include "gpgc_gcManagerMark.hpp"

class GPGC_GCManagerOld: public GPGC_GCManagerMark
{
  private:
    static intptr_t    _working_count;


  protected:
    GPGC_GCManagerOld(long manager_number, HeapRefBufferList** free_list, HeapRefBufferList** full_list)
      : GPGC_GCManagerMark(manager_number, free_list, full_list)
      {}


  public:
           static void reset_working()                 { _working_count = 0; }
           static void set_working_count(long count)   { _working_count = count; }

    inline static void decrement_working_count()       { Atomic::dec_ptr(&_working_count); }
    inline static void increment_working_count()       { Atomic::inc_ptr(&_working_count); }
    inline static long working_count()                 { return _working_count; }


  public:
    virtual long                manager_count () = 0;
    virtual GPGC_GCManagerMark* lookup_manager(long manager) = 0;

    virtual void                update_live_referent      (objectRef* referent_addr, int referrer_kid) = 0;
    virtual void                mark_and_push_referent    (objectRef* referent_addr, int referrer_kid) = 0;
    virtual void                push_referent_without_mark(objectRef* referent_addr, int referrer_kid) = 0;

    virtual long                manager_type() = 0;
};
#endif // GPGC_GCMANAGEROLD_HPP
