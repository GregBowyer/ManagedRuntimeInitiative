/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef FRAME_INLINE_HPP
#define FRAME_INLINE_HPP

#include "codeCache.hpp"
#include "interpreter_pd.hpp"
#include "stubRoutines.hpp"

// This file holds platform-independant bodies of inline functions for frames.

inline bool frame::is_native_frame()const{
CodeBlob*_cb=CodeCache::find_blob(pc());
return(_cb!=NULL&&_cb->is_native_method());
}

inline bool frame::is_compiled_frame()const{
CodeBlob*_cb=CodeCache::find_blob(pc());
return(_cb!=NULL&&_cb->is_java_method());
}
inline bool frame::is_c1_frame()const{
CodeBlob*_cb=CodeCache::find_blob(pc());
return(_cb!=NULL&&_cb->is_c1_method());
}

inline bool frame::is_runtime_frame()const{
CodeBlob*_cb=CodeCache::find_blob(pc());
  return (_cb != NULL && _cb->is_runtime_stub());
}

inline bool frame::is_interpreted_frame() const { return Interpreter::contains(pc()); }
inline bool frame::is_entry_frame() const { return StubRoutines::returns_to_call_stub(pc()); }
inline bool frame::is_java_frame() const { return is_interpreted_frame() || is_compiled_frame(); }
inline bool frame::is_known_frame() const { return is_interpreted_frame() || is_native_frame() || is_compiled_frame(); }
inline bool frame::is_first_frame() const { return is_entry_frame() && entry_frame_is_first(); }

inline methodOop frame::interpreter_frame_method() const {
  assert(is_interpreted_frame(), "interpreted frame expected");
  methodOop m = lvb_methodRef(interpreter_frame_method_addr()).as_methodOop();
  assert(m->is_perm(), "bad methodOop in interpreter frame");
  assert(m->is_method(), "not a methodOop");
  return m;
}

inline address frame::interpreter_frame_bcp() const { 
  return interpreter_frame_method()->bcp_from(interpreter_frame_bci()); 
}


// If 'reg' refers to a machine register, look it up using the pd-specific
// (hardware-window-aware) register finder.  Otherwise 'reg' refers to a
// stack-slot.
inline intptr_t* frame::reg_to_addr(VReg::VR reg) const {
  return VReg::is_reg(reg)
    // If it is passed in a register, it got spilled somewhere
    ? pd_reg_to_addr(reg)
    // Watch the Math!  sp() is (intptr_t*) so adding the offset scales by intptr_ts.
    : sp()+(VReg::reg2stk(reg)>>3);
}

inline objectRef* frame::reg_to_addr_oop(VOopReg::VR reg) const {
  return (objectRef*)(VOopReg::is_reg(reg)
    // If it is passed in a register, it got spilled somewhere
    ? pd_reg_to_addr(VReg::VR(reg))
    // Watch the Math!  sp() is (intptr_t*) so adding the offset scales by intptr_ts.
    : sp()+(VOopReg::reg2stk(reg)>>3));
}

// here are the platform-dependent bodies:

#include "frame_pd.inline.hpp"
#endif // FRAME_INLINE_HPP
