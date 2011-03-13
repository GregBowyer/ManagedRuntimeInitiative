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
#ifndef GPGC_VERIFY_TASKS_HPP
#define GPGC_VERIFY_TASKS_HPP


 
  #include "gcTaskManager.hpp"

class GPGC_Verify_ThreadTask:public GCTask{
  private:
JavaThread*_thread;
  public:
    GPGC_Verify_ThreadTask(JavaThread* jt) : _thread(jt) {}
    const char* name() { return (char *)"gpgc-verify-thread-task"; }
    virtual void do_it(GCTaskManager* manager, uint which);
  
};


class GPGC_Verify_VMThreadTask:public GCTask{
  public:
    const char* name() { return (char *)"gpgc-verify-vmthread-task"; }
    virtual void do_it(GCTaskManager* manager, uint which);
};


class GPGC_Verify_RootsTask:public GCTask{
  public:
    enum RootType {
      symbol_table_strong_refs            = 1,
      string_table_strong_refs            = 2,
universe=3,
jni_handles=4,
object_synchronizer=5,
      management                          = 6,
      jvmti_export                        = 7,
      system_dictionary                   = 8,
      vm_symbols                          = 9,
      code_cache                          = 10,
      symbol_table                        = 11,
      string_table                        = 12,
      newgc_ref_lists                     = 13,
      oldgc_ref_lists                     = 14,
      arta_objects                        = 15,
      klass_table                         = 16,
      code_cache_oop_table                = 17,
      weak_jni_handles                    = 18
    };
  private:
RootType _type;
  public:
    GPGC_Verify_RootsTask(RootType type) : _type(type) {}
    const char* name();
    virtual void do_it(GCTaskManager* manager, uint which);
};
#endif
