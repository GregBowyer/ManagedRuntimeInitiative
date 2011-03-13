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
#ifndef GPGC_SAFEPOINT_HPP
#define GPGC_SAFEPOINT_HPP


#include "vm_operations.hpp"

class GPGC_Safepoint:AllStatic{
  private:
    enum States {
      NotAtSafepoint    = 0,
      AtSafepoint       = 1,
    };

    static          VM_VMThreadSafepoint* _vm_thread_safepoint_op;
    static volatile intptr_t              _state;

    static volatile bool                  _new_safepoint;
    static volatile bool                  _old_safepoint;

    static volatile long                  _last_safepoint_end_time;

    static volatile bool                  _callbacks_completed;

  public:
    static bool is_at_safepoint     ();
    static bool is_at_new_safepoint ();

    static void begin               (TimeDetailTracer*    tdt,
                                     bool                 clean_vm_thread           = false,
                                     SafepointEndCallback end_callback              = NULL,
                                     void*                user_data                 = NULL,
                                     bool                 safepoint_other_collector = false);

    static void end                 (TimeDetailTracer* tdt);

    static void set_callbacks_completed() { _callbacks_completed = true; }

    // called by thread requesting the checkpoint
    static void do_checkpoint       (JavaThreadClosure* jtc, SafepointTimes* times);
};

#endif // GPGC_SAFEPOINT_HPP

