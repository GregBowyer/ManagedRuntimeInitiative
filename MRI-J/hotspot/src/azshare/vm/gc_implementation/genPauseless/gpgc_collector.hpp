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
#ifndef GPGC_COLLECTOR_HPP
#define GPGC_COLLECTOR_HPP


#include "heapRef_pd.hpp"
#include "objectRef_pd.hpp"
#include "safepointTimes.hpp"

class DetailTracer;
class GPGC_VerifyClosure;
class PGCTaskManager;
class PGCTaskQueue;
class TimeDetailTracer;

class GPGC_Collector:AllStatic{
  
  public:
    enum CollectorAge {
      Unknown      = 0,
      NewCollector = 1,
      OldCollector = 2,
      Verifier     = 3
    };

    enum {
      NotCollecting           =  1,
      CollectionStarting      =  2,
      MarkRemapSetup          =  3,
      InitialMarkSafepoint    =  4,
      ConcurrentMarking       =  5,
      WeakRefSafepoint        =  6,
      ConcurrentRefProcessing =  7,
      FinalMarkSafepoint      =  8,
      ConcurrentWeakMarking   =  9,
      MarkingVerification     =  10,
      MarkingCleanup          =  11,
      RelocationSetup         =  12,
      RelocationSafepoint     =  13,
      ConcurrentRelocation    =  14,
      RelocationSetup2        =  15,
      RelocationSafepoint2    =  16,
      ConcurrentRelocation2   =  17,
      CycleCleanup            =  18     // this state has been extended to cover 'flushing memory' as well
    };

    enum MutatorRefSanityCheck {
      NoSanityCheck    = 0,
      MustBeFinalLive  = 1,
      MustBeStrongLive = 2,
      NoMutatorRefs    = 3
    };


  public:
    static void      fixup_derived_oop         (objectRef* derived_ptr, objectRef old_base, objectRef new_base);
static objectRef get_forwarded_object(objectRef old_ref);
static heapRef mutator_relocate_object(heapRef old_ref);

    // Remap & NMT to support GC:
    static objectRef old_remap_nmt_and_rewrite (objectRef* addr);

    // Remapping to support GC:
    static objectRef mutator_relocate_only     (objectRef* addr);
static objectRef mutator_relocate(objectRef ref);
    static objectRef remap_only                (objectRef const* addr);
static objectRef remap(objectRef ref);

    static void      setup_read_trap_array     (TimeDetailTracer* tdt, CollectorAge age);
    static void      commit_batched_memory_ops (TimeDetailTracer* tdt, CollectorAge age);

    static objectRef old_gc_remapped           (objectRef* addr);
    static objectRef perm_remapped_only        (objectRef* addr);

    // Support for card-marking copied objects:
    static bool      no_card_mark              (objectRef* addr);

    inline static void assert_no_card_mark     (objectRef* addr)   { assert0(GPGC_Collector::no_card_mark(addr)); }

    static bool      is_new_collector_thread   (Thread* thread);
    static bool      is_old_collector_thread   (Thread* thread);
    static bool      is_verify_collector_thread(Thread* thread);

    // Task management:
    static void      run_task_queue            (DetailTracer* dt, PGCTaskManager* manager, PGCTaskQueue* q);
    static void      run_task_queue            (PGCTaskManager* manager, PGCTaskQueue* q);

    // Concurrent weak ref processing:
    static objectRef JNI_weak_reference_resolve(objectRef* referent_addr);
static objectRef java_lang_ref_Reference_get(objectRef reference);
static objectRef java_lang_ref_Referent_get_slow_path(objectRef reference);

    // Empty the mutator OldGen NMT ref buffers, and ensure any refs found point to
    // objects already marked StrongLive.
    static void final_clear_mutator_ref_buffers(TimeDetailTracer* tdt, CollectorAge age, const char* tag);

    // Logging
    static void      log_checkpoint_times      (TimeDetailTracer* tdt, SafepointTimes* times, const char* tag, const char* label);

  protected:
    static void      flush_mutator_ref_buffers (TimeDetailTracer* tdt, const char* tag, long& new_gen_refs, long& old_gen_refs);
    static void      mark_all_discovered       (TimeDetailTracer* tdt, CollectorAge age);
    static void      mark_all_final_discovered (TimeDetailTracer* tdt, CollectorAge age);
};

#endif // GPGC_COLLECTOR_HPP
