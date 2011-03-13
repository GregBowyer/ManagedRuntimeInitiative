/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef STUBROUTINES_PD_HPP
#define STUBROUTINES_PD_HPP


// This file holds the platform specific parts of the StubRoutines
// definition. See stubRoutines.hpp for a description on how to
// extend it.

static bool    returns_to_call_stub(address return_pc)   { return return_pc == _call_stub_return_address; }

struct x86 {
  friend class StubGenerator;

static address _handler_for_null_ptr_exception_entry;
static address _handler_for_array_index_check_entry;
static address _handler_for_nio_protection_entry;
static address _handler_for_GCLdValueTr_entry;
static address _handler_for_GCStValueNMTTr_entry;
static address _handler_for_GCThread_GCLdValueTr_entry;

static address _NativeMethodStub_unpoisoning_entry;

static address _partial_subtype_check;
static address _full_subtype_check;

  // Primitive arraycopy.  These routines assume the caller has already
  // null-checked the src & dst, range-checked the src & dst, and for
  // oop-arrays already done a simple compability check (it usually suffices
  // to see that the dst is of it's declared type).  The src offset, dst
  // offset and length all must be of at least the declared alignment; e.g.
  // char arrays with 0 offsets still need the 2-byte aligned copy to handle
  // the end-case right.
  static address _prim_arraycopy1;  // 1-byte aligned data that possibly overlaps
  static address _prim_arraycopy2;  // 2-byte aligned data that possibly overlaps
  static address _prim_arraycopy4;  // 4-byte aligned data that possibly overlaps
  static address _prim_arraycopy8;  // 8-byte aligned data that possibly overlaps
  static address _prim_arraycopy1_no_overlap;  // 1-byte aligned data that doesn't overlap
  static address _prim_arraycopy2_no_overlap;  // 2-byte aligned data that doesn't overlap
  static address _prim_arraycopy4_no_overlap;  // 4-byte aligned data that doesn't overlap
  static address _prim_arraycopy8_no_overlap;  // 8-byte aligned data that doesn't overlap
  static address _arraycopy_a;  // oop    aligned data
static address _sba_escape_handler;

static address _c1_profile_callee;

static address _c2_lock_entry;
static address _blocking_lock_entry;

  static address handler_for_null_ptr_exception_entry()   { return _handler_for_null_ptr_exception_entry; }
  static address handler_for_array_index_check_entry()    { return _handler_for_array_index_check_entry; }
static address handler_for_nio_protection_entry(){
                                                          #if !defined(AZ_PROXIED)
                                                              ShouldNotReachHere(); return NULL;
                                                          #else  // AZ_PROXIED
                                                              return _handler_for_nio_protection_entry;
                                                          #endif // AZ_PROXIED
  }
  static address handler_for_GCStValueNMTTr_entry()       { return _handler_for_GCStValueNMTTr_entry; }
  static address handler_for_GCThread_GCLdValueTr_entry() { return _handler_for_GCThread_GCLdValueTr_entry; }
static address address_of_handler_for_GCLdValueTr(){return(address)&_handler_for_GCLdValueTr_entry;}

  static address handler_for_GCLdValueTr_entry()          { return _handler_for_GCLdValueTr_entry; }

  static address partial_subtype_check()                  { return _partial_subtype_check; }
  static address full_subtype_check()                     { return _full_subtype_check; }

  static address arraycopy_a()                            { return _arraycopy_a; }

  static address c1_profile_callee()                      { return _c1_profile_callee; }

  static address new_sba( )                               { return _new_sba; }
  static address new_sba_array( )                         { return _new_sba_array; }

  static address sba_escape_handler()                     { return _sba_escape_handler; }

  static address c2_lock_entry()                          { return _c2_lock_entry; }
  static address blocking_lock_entry()                    { return _blocking_lock_entry; }
};

static address prim_arraycopy(int element_size, bool overlap) {
  if (overlap) {
    switch (element_size) {
    case 1: return x86::_prim_arraycopy1;
    case 2: return x86::_prim_arraycopy2;
    case 4: return x86::_prim_arraycopy4;
    case 8: return x86::_prim_arraycopy8;
    default: ShouldNotReachHere(); return NULL;
    }
  } else {
    switch (element_size) {
    case 1: return x86::_prim_arraycopy1_no_overlap;
    case 2: return x86::_prim_arraycopy2_no_overlap;
    case 4: return x86::_prim_arraycopy4_no_overlap;
    case 8: return x86::_prim_arraycopy8_no_overlap;
    default: ShouldNotReachHere(); return NULL;
    }
  }
}

#endif // STUBROUTINES_PD_HPP
