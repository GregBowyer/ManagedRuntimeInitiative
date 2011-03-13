/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "codeCache.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "ostream.hpp"
#include "rframe.hpp"

#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline2.hpp"

static RFrame*const  noCaller    = (RFrame*) 0x1;		// no caller (i.e., initial frame)
static RFrame*const  noCallerYet = (RFrame*) 0x0;		// caller not yet computed

RFrame::RFrame(frame fr, JavaThread* thread, RFrame*const callee) : 
  _fr(fr), _thread(thread), _callee(callee), _num(callee ? callee->num() + 1 : 0) {
  _caller = (RFrame*)noCallerYet;
  _invocations = 0;
  _distance = 0;
}

void RFrame::set_distance(int d) { 
  assert(is_compiled() || d >= 0, "should be positive");
  _distance = d; 
}

InterpretedRFrame::InterpretedRFrame(frame fr, JavaThread* thread, RFrame*const callee)
: RFrame(fr, thread, callee) {
  _method = methodHandle(thread, fr.interpreter_frame_method());
  init();
}

InterpretedRFrame::InterpretedRFrame(frame fr, JavaThread* thread, methodHandle m)
: RFrame(fr, thread, NULL) {
  assert( fr.interpreter_frame_method() == m(), "" );
  _method = m;
  init();
}

CompiledRFrame::CompiledRFrame(frame fr, JavaThread* thread, RFrame*const  callee)
: RFrame(fr, thread, callee) { 
  init();
}

CompiledRFrame::CompiledRFrame(frame fr, JavaThread* thread)
: RFrame(fr, thread, NULL) { 
  init();
}

RFrame* RFrame::new_RFrame(frame fr, JavaThread* thread, RFrame*const  callee) {
  RFrame* rf;
  int dist = callee ? callee->distance() : -1;
  if (fr.is_interpreted_frame()) {
    rf = new InterpretedRFrame(fr, thread, callee);
    dist++;
  } else {
assert(fr.is_compiled_frame(),"");
rf=new CompiledRFrame(fr,thread,callee);
  }
  rf->set_distance(dist);
  rf->init();
  return rf;
}

RFrame* RFrame::caller() {
  if (_caller != noCallerYet) return (_caller == noCaller) ? NULL : _caller;	// already computed caller
  
  // caller not yet computed; do it now
frame sender=_fr.sender();
  if (sender.is_java_frame()) {
    _caller = new_RFrame(sender, thread(), this);
    return _caller;
  }

  // Real caller is not java related
  _caller = (RFrame*)noCaller;
  return NULL;
}

int InterpretedRFrame::cost() const {
  return _method->code_size();    // fix this
  //return _method->estimated_inline_cost(_receiverKlass);
}

int CompiledRFrame::cost() const {
  Unimplemented();
  return 0;
  //nmethod* nm = top_method()->code();
  //if (nm != NULL) {
  //  return nm->code_size();
  //} else {
  //  return top_method()->code_size();
  //}
}

void CompiledRFrame::init() {
assert(_fr.is_compiled_frame(),"must be compiled");
_cb=CodeCache::find_blob(_fr.pc());
  _method = methodHandle(_cb->owner().as_methodCodeOop()->method());
  assert(_method(), "should have found a method");
  CodeProfile *cp = _cb->owner().as_methodCodeOop()->get_codeprofile();
  _invocations = _method->invocation_count() + _method->backedge_count() + cp->total_count();
}

void InterpretedRFrame::init() {
  _invocations = _method->invocation_count() + _method->backedge_count();
}

void RFrame::print(const char* kind) {
#ifndef PRODUCT
int cnt=0;
  tty->print("%3d %s ", _num, is_c2_compiled() ? "C2" : (is_c1_compiled() ? "C1" : " I"));
  top_method()->print_short_name(tty);
  tty->print_cr(": inv=%5d(%d) cst=%4d", _invocations, cnt, cost());
#endif
}

void CompiledRFrame::print() {
  RFrame::print("comp");
}

void InterpretedRFrame::print() {
  RFrame::print("int.");
}

