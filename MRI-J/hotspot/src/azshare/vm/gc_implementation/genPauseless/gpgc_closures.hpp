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


#ifndef GPGC_CLOSURES_HPP
#define GPGC_CLOSURES_HPP


#include "thread.hpp"


class GPGC_UnshatteredPageTrapCountClosure: public JavaThreadClosure {
  private:
    intptr_t _threads;
    intptr_t _traps;
    intptr_t _max_thread_traps;

  public:
    GPGC_UnshatteredPageTrapCountClosure() : _threads(0), _traps(0), _max_thread_traps(0) {}

void do_java_thread(JavaThread*jt);

    intptr_t get_threads()                 { return _threads; }
    intptr_t get_total_traps()             { return _traps; }
    intptr_t get_max_single_thread_traps() { return _max_thread_traps; }
};


#endif // GPGC_CLOSURES_HPP
