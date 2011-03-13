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


#ifndef SURROGATELOCKERTHREAD_HPP
#define SURROGATELOCKERTHREAD_HPP

#include "thread.hpp"


// The SurrogateLockerThread is used by concurrent GC threads for
// manipulating Java monitors, in particular, currently for
// manipulating the pending_list_lock. XXX
class SurrogateLockerThread: public JavaThread {
  friend class VMStructs;
 public:
  enum SLT_msg_type {
    empty = 0,           // no message
    acquirePLL,          // acquire pending list lock
    releaseAndNotifyPLL  // notify and release pending list lock
  };
 private:
  static SurrogateLockerThread* _slt;
  static SurrogateLockerThread* make(TRAPS);

  SLT_msg_type  _buffer;  // communication buffer
  
  SurrogateLockerThread();

 public:
static void makeSurrogateLockerThread(TRAPS);

  static SurrogateLockerThread* slt()           { return _slt; }

  bool is_hidden_from_external_view() const     { return true; }

  void loop(); // main method

  void manipulatePLL(SLT_msg_type msg);

};

#endif // SURROGATELOCKERTHREAD_HPP
