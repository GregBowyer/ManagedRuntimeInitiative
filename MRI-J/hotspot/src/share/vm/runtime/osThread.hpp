/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OSTHREAD_HPP
#define OSTHREAD_HPP

#include "allocation.hpp"
#include "atomic.hpp"
#include "sizes.hpp"
class outputStream;
extern outputStream* tty;
typedef int (*OSThreadStartFunc)(void*);

// The OSThread class holds OS-specific thread information.  

class OSThread: public CHeapObj {
 private:
  OSThreadStartFunc _start_proc;  // Thread start routine
  void* _start_parm;              // Thread start routine parameter
  // Note:  _interrupted must be jint, so that Java intrinsics can access it.
  // The value stored there must be either 0 or 1.  It must be possible
  // for Java to emulate Thread.currentThread().isInterrupted() by performing
  // the double indirection Thread::current()->_osthread->_interrupted.
volatile jint _interrupted;//Thread.isInterrupted state

  // Methods
 public:
  // Constructor
  OSThread(OSThreadStartFunc start_proc, void* start_parm) : _start_proc(start_proc), _start_parm(start_parm), _interrupted(0) {
    pd_initialize();
  }

  // Destructor
  ~OSThread() { pd_destroy(); }

  // Accessors
  OSThreadStartFunc start_proc() const    { return _start_proc; }
  void* start_parm() const                { return _start_parm; }

  jint interrupted() const                { return _interrupted ; }
  jint set_interrupted() { return Atomic::cmpxchg(1,&_interrupted,0); }
  jint clr_interrupted() { return Atomic::cmpxchg(0,&_interrupted,1); }
  // For java intrinsics:
  static ByteSize interrupted_offset()    { return byte_offset_of(OSThread, _interrupted); }

  // Printing
  void print_on(outputStream* st) const;
  void print() const			  { print_on(tty); }
 
  // Platform dependent stuff
  #include "osThread_os.hpp"
};

#endif // OSTHREAD_HPP
