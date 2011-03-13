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

#ifndef GPGC_NMT_HPP
#define GPGC_NMT_HPP

#include "allocation.hpp"
#include "heapRef_pd.hpp"

class GPGC_NMT:public AllStatic{
  private:
    // index into the current read barrier array: <_desired_(new/old)_nmt_flag+Page> 
    static long _desired_new_nmt_flag;
    static long _desired_old_nmt_flag;
    // index into the dupe read barrier array: <_upcoming_(new/old)_nmt_flag+Page> 
    static long _upcoming_new_nmt_flag;
    static long _upcoming_old_nmt_flag;

    // These two are booleans.  We use int32_t to make the size clear, because we're doing
    // 4-byte loads of these fields in the assembly language:
    static int32_t _new_trap_enabled;
    static int32_t _old_trap_enabled;

  public:
static address desired_new_nmt_flag_addr(){return(address)&_desired_new_nmt_flag;}
static address desired_old_nmt_flag_addr(){return(address)&_desired_old_nmt_flag;}

static address new_trap_enabled_addr(){return(address)&_new_trap_enabled;}
static address old_trap_enabled_addr(){return(address)&_old_trap_enabled;}

    static inline long    desired_new_nmt_flag()  { return _desired_new_nmt_flag; }
    static inline long    desired_old_nmt_flag()  { return _desired_old_nmt_flag; }

    static inline long    upcoming_new_nmt_flag()  { return _upcoming_new_nmt_flag; }
    static inline long    upcoming_old_nmt_flag()  { return _upcoming_old_nmt_flag; }

    static inline void    toggle_new_nmt_flag()   { _desired_new_nmt_flag = _desired_new_nmt_flag ^ 0x1; }
    static inline void    toggle_old_nmt_flag()   { _desired_old_nmt_flag = _desired_old_nmt_flag ^ 0x1; }

    static inline void    set_upcoming_nmt(bool old_gen_cycle)   { 
      _upcoming_new_nmt_flag = _upcoming_new_nmt_flag ^ 0x1; 
      if ( old_gen_cycle ) {
        _upcoming_old_nmt_flag = _upcoming_old_nmt_flag ^ 0x1; 
      }
    }

    static inline void    enable_new_trap()       { _new_trap_enabled = true; }
    static inline void    enable_old_trap()       { _old_trap_enabled = true; }

    static inline void    disable_new_trap()      { _new_trap_enabled = false; }
    static inline void    disable_old_trap()      { _old_trap_enabled = false; }

    static inline bool    is_new_trap_enabled()   { return _new_trap_enabled; }
    static inline bool    is_old_trap_enabled()   { return _old_trap_enabled; }

static void new_space_nmt_buffer_full(Thread*thread);
static void old_space_nmt_buffer_full(Thread*thread);

    static inline bool    has_desired_new_nmt(const heapRef &o);
    static inline bool    has_desired_old_nmt(const heapRef &o)  { return o.nmt() == desired_old_nmt_flag(); }

    static inline bool    has_desired_nmt    (const objectRef &o);

    // Debugging:
    static        void    sanity_check_trapped_ref(objectRef ref);
    static        void    sanity_check(Thread* thread, objectRef ref);
};


inline bool GPGC_NMT::has_desired_new_nmt(const heapRef &o)
{
  long nmt = o.nmt();

  return nmt == desired_new_nmt_flag();
}


inline bool GPGC_NMT::has_desired_nmt(const objectRef &o)
{
  if ( o.is_stack() ) {
    return true;
  }
  if ( o.is_new() ) {
return has_desired_new_nmt(o);
  } else {
    assert0( o.is_old() );
return has_desired_old_nmt(o);
  }
}

#endif // GPGC_NMT_HPP
