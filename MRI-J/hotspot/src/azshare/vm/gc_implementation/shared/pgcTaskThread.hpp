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
#ifndef PGCTASKTHREAD_HPP
#define PGCTASKTHREAD_HPP


#include "gcTaskThread.hpp"

// Forward declarations of classes defined here.

// class taskManager;
class PGCTaskThread : public GCTaskThread {
  public: 
// Performance counters
    long                                _time1;
    long                                _time2;
    long                                _ticks1;
    long                                _ticks2;
    long                                _ticks3;

  private:
    PGCTaskManager* _pgcManager;              // Manager for worker.
    intptr_t  _wakeupCount;
    uint64_t _pgcWhich;
  public: 
    PGCTaskThread(PGCTaskManager* manager, uint64_t which);
    virtual void run();

PGCTaskManager*manager()const{
      return _pgcManager;
    }
    uint64_t which() const {
      return _pgcWhich;
    }
static PGCTaskThread*create(PGCTaskManager*manager,
                                 uint64_t         which) {
      return new (ttype::java_thread) PGCTaskThread(manager, which);
    }
    intptr_t wakeup_count() {
      return _wakeupCount;
    }
    void set_wakeup_count(intptr_t wakeup_count) { _wakeupCount = wakeup_count; }
};

#endif // PGCTASKTHREAD_HPP
