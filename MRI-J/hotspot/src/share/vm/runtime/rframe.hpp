/*
 * Copyright 1997-2000 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef RFRAME_HPP
#define RFRAME_HPP


#include "allocation.hpp"
#include "frame.hpp"
#include "thread.hpp"
#include "codeBlob.hpp"

// rframes ("recompiler frames") decorate stack frames with some extra information
// needed by the recompiler.  The recompiler views the stack (at the time of recompilation) 
// as a list of rframes.

class RFrame : public ResourceObj {
 protected:
  const frame _fr;                  // my frame
  JavaThread* const _thread;        // thread where frame resides.
  RFrame* _caller;                  // caller / callee rframes (or NULL)
  RFrame*const _callee;
  const int _num;                   // stack frame number (0 = most recent)
  int _invocations;                 // current invocation estimate (for this frame)
                                    // (i.e., how often was this frame called)
  int _distance;                    // recompilation search "distance" (measured in # of interpreted frames)

  RFrame(frame fr, JavaThread* thread, RFrame*const callee);
  virtual void init() = 0;          // compute invocations, loopDepth, etc.
  void print(const char* name);

 public:

  static RFrame* new_RFrame(frame fr, JavaThread* thread, RFrame*const callee);

  virtual bool is_interpreted() const     { return false; }
  virtual bool is_compiled() const        { return false; }
  virtual bool is_c1_compiled() const     { return false; }
  virtual bool is_c2_compiled() const     { return false; }
  int distance() const                    { return _distance; }
  void set_distance(int d);
  int invocations() const                 { return _invocations; }
  int num() const                         { return _num; }
  frame fr() const                        { return _fr; }
  JavaThread* thread() const              { return _thread; }
  virtual int cost() const = 0;           // estimated inlining cost (size)
  virtual methodHandle top_method() const  = 0;
virtual CodeBlob*cb()const{ShouldNotCallThis();return NULL;}

  RFrame* caller();
  RFrame* callee() const                  { return _callee; }
  RFrame* parent() const;                 // rframe containing lexical scope (if any)
  virtual void print()                    = 0;

//  static int computeSends(methodOop m);
//  static int computeSends(nmethod* nm);
//  static int computeCumulSends(methodOop m);
//  static int computeCumulSends(nmethod* nm);
};

class CompiledRFrame : public RFrame {    // frame containing a compiled method
 protected:
  CodeBlob*    _cb;
  methodHandle _method;                   // top method

  CompiledRFrame(frame fr, JavaThread* thread, RFrame*const  callee);
  void init();
  friend class RFrame;

 public:
CompiledRFrame(frame fr,JavaThread*thread);//for codeblob triggering its counter (callee == NULL)
  bool is_compiled() const                 { return true; }
  methodHandle top_method() const          { return _method; }
  CodeBlob* cb() const                     { return _cb; }
  int cost() const;
  void print();

  bool is_c1_compiled() const              { return _cb->is_c1_method(); }
  bool is_c2_compiled() const              { return _cb->is_c2_method(); }
};

class InterpretedRFrame : public RFrame {    // interpreter frame
 protected:
  methodHandle   _method;
 
  InterpretedRFrame(frame fr, JavaThread* thread, RFrame*const  callee);
  void init();
  friend class RFrame;

 public:
  InterpretedRFrame(frame fr, JavaThread* thread, methodHandle m); // constructor for method triggering its invocation counter
  bool is_interpreted() const                { return true; }
  methodHandle top_method() const            { return _method; }
  int cost() const;
  void print();
};

#endif // RFRAME_HPP
