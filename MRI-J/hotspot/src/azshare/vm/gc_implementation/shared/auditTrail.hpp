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
#ifndef AUDITTRAIL_HPP
#define AUDITTRAIL_HPP

//  This class is a debugging support class.  It maintains a ring buffer that
//  and audit trail can be written into.
#include "allocation.hpp"

// AT_PRODUCT is not defined.
#ifdef AT_PRODUCT
#define AT_PRODUCT_RETURN {}
#else
#define AT_PRODUCT_RETURN /*next token must be ;*/
#endif

class JavaThread;


class AuditTrail:public CHeapObj{
  public:
    enum {
      UNDEFINED                 = 0,

      // GPGC_ThreadCleaner::LVB_thread_stack()
      GPGC_START_LVB_CLEAN      = 1,
      GPGC_END_LVB_CLEAN        = 2,

      // GPGC_NewGC_CleanDirtyThreadsClosure::clean_by_gc_thread()
      GPGC_START_NEW_GC_CLEAN   = 3,
      GPGC_END_NEW_GC_CLEAN     = 4,

      // GPGC_OldGC_CleanDirtyThreadsClosure::clean_by_gc_thread()
      GPGC_START_OLD_GC_CLEAN   = 5,
      GPGC_END_OLD_GC_CLEAN     = 6,

      // GPGC_ThreadCleaner::new_gen_checkpoint_and_clean_threads()
      GPGC_START_NEW_CKP_CLEAN  = 7,
      GPGC_END_NEW_CKP_CLEAN    = 8,

      // GPGC_ThreadCleaner::old_gen_checkpoint_and_clean_threads()
      GPGC_START_OLD_CKP_CLEAN  = 9,
      GPGC_END_OLD_CKP_CLEAN    = 10,

      // GPGC_NewCollector::parallel_final_mark()
      GPGC_NEW_GC_SKIP_CLEAN    = 11,

      // GPGC_OldCollector::parallel_final_mark()
      GPGC_OLD_GC_SKIP_CLEAN    = 12,

      // GPGC_NewGC_ThreadMarkTask::do_it()
      GPGC_START_NEW_GC_TASK    = 13,
      GPGC_END_NEW_GC_TASK      = 14,

      // GPGC_OldGC_ThreadMarkTask::do_it()
      GPGC_START_OLD_GC_TASK    = 15,
      GPGC_END_OLD_GC_TASK      = 16,

      // GPGC_OldGC_ThreadCodeblobsMarkTask::do_it()
      GPGC_START_OLD_GC_CB_TASK = 17,
      GPGC_END_OLD_GC_CB_TASK   = 18,

      // frame::oops_do()
      INTERPRETED_FRAME         = 19,
      ENTRY_FRAME               = 20,
      CODEBLOB_FRAME            = 21,

      // OopMapSet::oops_do()
      OOP_MAP_FOUND             = 22,
      DERIVED_OOP_MAP_VALUE     = 23,
      OOP_MAP_VALUE             = 24,

      // GPGC_NewCollector::mark_remap_setup()
      GPGC_START_NEW_GC_CYCLE   = 25,

      // GPGC_NewCollector::collect()
      GPGC_END_NEW_GC_CYCLE     = 26,

      // GPGC_OldCollector::mark_remap_safepoint1()
      GPGC_START_OLD_GC_CYCLE   = 27,

      // GPGC_OldCollector::collect()
      GPGC_END_OLD_GC_CYCLE     = 28,

      // GPGC_Safepoint::begin()
      GPGC_START_SAFEPOINT      = 29,

      // GPGC_Safepoint::end()
      GPGC_END_SAFEPOINT        = 30,

      // JavaThread::initialize()
      JAVA_THREAD_START         = 31,

      // various
      MAKE_SELF_WALKABLE        = 32,
      MAKE_REMOTE_WALKABLE      = 33,

      // GPGC_Verify_ThreadTask::do_it()
      GPGC_START_VERIFY_THREAD  = 34,
      GPGC_END_VERIFY_THREAD    = 35,

      // various
      GPGC_TOGGLE_NMT           = 36,

      // Number of tags:
      TAGS_MAX                  = 37
    };

    static const char* tag_names[TAGS_MAX];

  private:
    jlong          _size;
    volatile jlong _next;
    intptr_t*      _buffer;

    static void unchecked_log_time(intptr_t tag, intptr_t A);

  public:
    AuditTrail (long size);
    ~AuditTrail();
 
    inline void record(intptr_t x);

    static inline void log_time(intptr_t tag, intptr_t A)                                              AT_PRODUCT_RETURN;

    static inline void log_time(JavaThread* thread, intptr_t tag)                                      AT_PRODUCT_RETURN;
    static inline void log_time(JavaThread* thread, intptr_t tag, intptr_t A)                          AT_PRODUCT_RETURN;
    static inline void log_time(JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B)              AT_PRODUCT_RETURN;

    static inline void log     (JavaThread* thread, intptr_t tag, intptr_t A)                          AT_PRODUCT_RETURN;
    static inline void log     (JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B)              AT_PRODUCT_RETURN;
    static inline void log     (JavaThread* thread, intptr_t tag, intptr_t A, intptr_t B, intptr_t C)  AT_PRODUCT_RETURN;

    static inline void log_time(AuditTrail* at, intptr_t tag)                                          AT_PRODUCT_RETURN;
    static inline void log_time(AuditTrail* at, intptr_t tag, intptr_t A, intptr_t B)                  AT_PRODUCT_RETURN;

    static inline void log     (AuditTrail* at, intptr_t tag, intptr_t A, intptr_t B, intptr_t C)      AT_PRODUCT_RETURN;
};

#endif
