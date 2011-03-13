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

#ifndef GPGC_OLDCOLLECTOR_HPP
#define GPGC_OLDCOLLECTOR_HPP

#include "gpgc_collector.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_relocationSpike.hpp"


class AuditTrail;
class GPGC_CycleStats;
class GPGC_GCManagerOldReloc;
class GPGC_Heap;
class GPGC_OldGC_VerifyClosure;
class GPGC_Population;
class GPGC_JavaLangRefHandler;

class GPGC_OldCollector: GPGC_Collector {
  private:
    static long  _collection_state;
    static long  _mutator_ref_sanity_check;
    static long  _should_mark_new_objs;

    static void  set_collection_state             (long state) { _collection_state     = state; }
    static void  set_mark_new_objects_live        (bool flag)  { _should_mark_new_objs = flag; }

    static long  _current_old_cycle;
    static long  _current_total_cycle;

  public:
    static long  collection_state                 ()           { return _collection_state; }
    static long* collection_state_addr            ()           { return &_collection_state; }
    static bool  is_marking                       ()           { return _collection_state>=InitialMarkSafepoint &&
                                                                        _collection_state<=FinalMarkSafepoint; }
    static bool  is_relocating                    ()           { return _collection_state==RelocationSetup
                                                                     || _collection_state==RelocationSafepoint
                                                                     || _collection_state==ConcurrentRelocation; }

    static long  mutator_ref_sanity_check         ()           { return _mutator_ref_sanity_check; }

    static void  request_collection_start         ()           { assert0(_collection_state==NotCollecting);
                                                                 _collection_state = CollectionStarting;    }

    static bool  should_mark_new_objects_live     ()           { return _should_mark_new_objs; }
    static long* should_mark_new_objects_live_addr()           { return &_should_mark_new_objs; }

    static long  current_old_cycle                ()           { return _current_old_cycle; }
    static long  current_total_cycle              ()           { return _current_total_cycle; }

    static AuditTrail* audit_trail                ()           { return _audit_trail; }

    // Debugging support:
    static void do_derived_oop                    (objectRef* base_ptr, objectRef* derived_ptr);
    static void ensure_base_not_relocating        (objectRef* base_ptr);

  private:     
    static GPGC_JavaLangRefHandler* _jlr_handler;
    static GPGC_CycleStats*         _cycle_stats;
    static GPGC_Population*         _page_pops;
    static GPGC_RelocationSpike     _relocation_spike;

    static long                _last_target_pages;

    static uint64_t            _fragment_page_stripe;
    static uint64_t            _fragment_page_stripe_mask;

    static jlong               _time_of_last_gc;

    static AuditTrail*         _audit_trail;

    // Safepoint control flags, controlled by PGCSafepointMark, etc
    static bool                _start_global_safepoint;
    static bool                _start_1st_mark_remap_safepoint;
    static bool                _end_1st_mark_remap_safepoint;
    static bool                _start_2nd_mark_remap_safepoint;
    static bool                _end_2nd_mark_remap_safepoint;
    static bool                _start_3rd_mark_remap_safepoint;
    static bool                _end_3rd_mark_remap_safepoint;
    static bool                _start_mark_verify_safepoint;
    static bool                _end_mark_verify_safepoint;
    static bool                _end_mark_phase_safepoint;
    static bool                _start_relocation_safepoint;
    static bool                _end_relocation_safepoint;
    static bool                _start_relocation_safepoint2;
    static bool                _end_relocation_safepoint2;
    static bool                _end_global_safepoint;
  
    static void                reset_millis_since_last_gc();

  public:
    static void     initialize                     ();
    static void     reset_concurrency              ();

    static void     collect                        (const char* label, bool clear_all, GPGC_CycleStats* stats);

    // Marking:
    inline static void      mark_and_push     (GPGC_GCManagerOldStrong* gcm, objectRef* ref_addr, int referrer_kid);
    inline static void      mark_and_push     (GPGC_GCManagerOldFinal*  gcm, objectRef* ref_addr);
    inline static void      mark_and_follow   (GPGC_GCManagerOldStrong* gcm, objectRef* ref_addr, int referrer_kid);
    inline static void      mark_and_follow   (GPGC_GCManagerOldFinal*  gcm, objectRef* ref_addr);

    // TODO: review these two functions:
    inline static objectRef remap_and_nmt_only                   (objectRef* ref_addr);
    inline static objectRef remap_and_nmt_without_cas            (objectRef* ref_addr);

    inline static void      unlink_weak_root                     (objectRef* ref_addr, objectRef old_ref);

    static void             revisit_weak_klass_link(GPGC_GCManagerOldStrong* gcm, Klass* k) { gcm->revisit_weak_klass_link(k); }

    static void             mark_to_live                         (objectRef* ref_addr);
    static void             mark_to_live_or_new                  (objectRef* ref_addr);
static void mark_leaf_guaranteed(oop obj);
static void mark_leaf_conditional(oop obj);
static void mutator_mark_leaf_conditional(oop obj);
    static void             mark_leaf_string_guaranteed          (oop string);
    static void             mark_leaf_string_conditional         (oop string);
    static void             mutator_mark_leaf_string_conditional (oop string);

    inline static bool      mark_through_non_strong_ref          (GPGC_GCManagerOldStrong* gcm,
objectRef referent,
                                                                  oop obj, ReferenceType ref_type);

    // Relocation:
    static void             sideband_forwarding_init             (long work_unit);
    static void             remap_perm_gen_memory                (TimeDetailTracer* tdt);
    static void             remap_old_gen_memory                 (TimeDetailTracer* tdt);
    static void             remap_to_mirror_for_relocate         (GPGC_PopulationArray* relocation_array);
    static void             relocate_small_pages                 (GPGC_GCManagerOldReloc* gcm, long work_unit, GPGC_PopulationArray* relocation_array);
    static void             relocate_mid_pages                   (GPGC_GCManagerOldReloc* gcm, long work_unit, GPGC_PopulationArray* relocation_array,
                                                                  int64_t stripe);
    static void             heal_mid_pages                       (GPGC_GCManagerOldReloc* gcm, int64_t stripe);
    static intptr_t         bci_from_relocated_bcp               (intptr_t bcp, methodOop method, constMethodOop constMethod);

static klassRef relocate_obj_klass(oop obj);

    static void             card_mark_pages                      (GPGC_PopulationArray* array, long work_unit);
    inline static void      update_card_mark                     (objectRef* ref_addr);

    static jlong            millis_since_last_gc                 ();

    // Accessors:
    static uint64_t                 fragment_page_stripe_mask    ()  { return _fragment_page_stripe_mask; }
    static uint64_t                 fragment_page_stripe         ()  { return _fragment_page_stripe; }

    static GPGC_JavaLangRefHandler* jlr_handler                  ()  { return _jlr_handler; }
    static GPGC_CycleStats*         cycle_stats                  ()  { return _cycle_stats; }
    static GPGC_Population*         page_pops                    ()  { return _page_pops; }
    static GPGC_RelocationSpike*    relocation_spike             ()  { return &_relocation_spike; }

    static bool                     remap_and_nmt_if_live        (objectRef* ref_addr);
    static objectRef                get_weak_ref                 (objectRef* ref_addr);
static objectRef get_weak_ref(objectRef ref);

  private:
    static inline bool              objectRef_needed_nmt         (objectRef* ref_addr, objectRef& new_ref);
    static inline bool              objectRef_needs_final_mark   (objectRef* ref_addr, objectRef& new_ref);

    //
    // The mark-remap phase of a GC cycle:
    //
    static void    mark_remap_phase                  (const char* label);
    static void    mark_remap_safepoint1             (GPGC_Heap* heap, const char* label);
    static void    mark_remap_concurrent             (GPGC_Heap* heap, const char* label);
    static void    mark_remap_safepoint2             (GPGC_Heap* heap, const char* label);
    static void    mark_remap_concurrent_ref_process (GPGC_Heap* heap, const char* label);
    static void    mark_remap_safepoint3             (GPGC_Heap* heap, const char* label);
    static void    mark_remap_concurrent_weak_roots  (GPGC_Heap* heap, const char* label);
    static void    mark_remap_verify_mark            (GPGC_Heap* heap, const char* label);
    static void    pre_mark_remap_cleanup            (GPGC_Heap* heap, const char* label);
    static void    mark_remap_cleanup                (GPGC_Heap* heap, const char* label);

    static void    disable_NMT_traps                 (TimeDetailTracer* tdt);

    static void    marking_init                      ();
    static void    marking_cleanup                   ();

    static void    parallel_concurrent_mark          (TimeDetailTracer* tdt);
    static void    parallel_flush_and_mark           (TimeDetailTracer* tdt);
    static void    parallel_concurrent_ref_process   (TimeDetailTracer* tdt);
    static void    parallel_final_mark               (TimeDetailTracer* tdt);
    static void    parallel_concurrent_weak_marks    (TimeDetailTracer* tdt);

    //
    // The relocation phase of a GC cycle:
    //
    static void    relocation_phase                  (const char* label);
    static void    relocation_setup                  (GPGC_Heap* heap, const char* label);
    static void    relocation_safepoint_perm         (GPGC_Heap* heap, const char* label);
    static void    relocation_concurrent_perm        (GPGC_Heap* heap, const char* label);
    static void    relocation_setup_old              (GPGC_Heap* heap, const char* label);
    static void    relocation_safepoint_old          (GPGC_Heap* heap, const char* label);
    static void    relocation_concurrent_old         (GPGC_Heap* heap, const char* label);

    static void    prepare_sideband_forwarding       (TimeDetailTracer* tdt);
    static void    update_page_relocation_states     (TimeDetailTracer* tdt, GPGC_PageInfo::Gens gen);
};
#endif // GPGC_OLDCOLLECTOR_HPP
