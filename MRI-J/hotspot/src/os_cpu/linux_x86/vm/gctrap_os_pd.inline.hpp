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
#ifndef GCTRAP_OS_PD_INLINE_HPP
#define GCTRAP_OS_PD_INLINE_HPP



// Support for per-thread GPGC barrier data and methods.

private:
  intptr_t _unshattered_page_trap_count;

public:
  void     reset_unshattered_page_trap_count()     { _unshattered_page_trap_count = 0;    }
  void     increment_unshattered_page_trap_count() { _unshattered_page_trap_count ++;     }
  intptr_t get_unshattered_page_trap_count()       { return _unshattered_page_trap_count; }

private:
  bool     _gcthread_lvb_trap_vector;
  bool     _gc_mode;

public:
  inline void set_gcthread_lvb_trap_vector() { _gcthread_lvb_trap_vector = true;  }
  inline bool gcthread_lvb_trap_vector    () { return _gcthread_lvb_trap_vector;  }

  inline void set_gc_mode(bool flag)         { _gc_mode = flag; }
  inline bool is_gc_mode ()                  { return _gc_mode; }


//
// TODO: Everything below here is obsolete for x86.  Clean them up.
// 
inline void set_trap_table(address trap_table) {
  if( UseGenPauselessGC ) {
    Unimplemented();
  }
}


inline void disable_nmt_traps() {
  if( UseGenPauselessGC ) {
    Unimplemented();
  }
}


inline void enable_nmt_traps() {
  if( UseGenPauselessGC ) {
    Unimplemented();
  }
}


inline bool is_nmt_enabled() {
  if( UseGenPauselessGC ) {
    Unimplemented();
  }
  Unimplemented();
  return false;
}

#endif
