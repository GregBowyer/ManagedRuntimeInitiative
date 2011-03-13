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

#ifndef GPGC_THREADCLEANER_HPP
#define GPGC_THREADCLEANER_HPP

#include "safepointTimes.hpp"
#include "thread.hpp"

class GPGC_GCManagerNewStrong;
class GPGC_GCManagerOldStrong;
class JavaThreadClosure;
class TimeDetailTracer;


class GPGC_ThreadCleaner:AllStatic{
  private:
    static void checkpoint_and_clean_threads         (JavaThreadClosure* jtc, SafepointTimes* times);

  public:
    static void enable_thread_self_cleaning          (TimeDetailTracer* tdt, JavaThread::SuspendFlags flag);
    static void LVB_thread_stack                     (JavaThread* dirty_thread);
    static void self_clean_vm_thread                 ();
    static void new_gen_checkpoint_and_clean_threads (TimeDetailTracer* tdt, GPGC_GCManagerNewStrong* gcm, const char* tag);
};

#endif // GPGC_THREADCLEANER_HPP
