/*
 * Copyright 2002-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PSSCAVENGE_HPP
#define PSSCAVENGE_HPP


#include "collectorCounters.hpp"
#include "growableArray.hpp"
#include "heapRef_pd.hpp"
#include "markWord.hpp"
#include "timer.hpp"
class CardTableExtension;
class GCTaskManager;
class GCTaskQueue;
class MutableSpace;
class OopStack;
class PSPromotionManager;
class ParallelScavengeHeap;
class ReferenceProcessor;
class PSIsAliveClosure;
class PSRefProcTaskExecutor;

class PSScavenge: AllStatic {
  friend class PSIsAliveClosure;
  friend class PSKeepAliveClosure;
  friend class PSPromotionManager;

 enum ScavengeSkippedCause {
   not_skipped = 0,
   to_space_not_empty,
   promoted_too_large,
   full_follows_scavenge
 };

  // Saved value of to_space->top(), used to prevent objects in to_space from
  // being rescanned.
  static HeapWord* _to_space_top_before_gc;

  // Number of consecutive attempts to scavenge that were skipped
  static int		    _consecutive_skipped_scavenges;


 protected:
  // Flags/counters
  static ReferenceProcessor* _ref_processor;        // Reference processor for scavenging.
  static PSIsAliveClosure    _is_alive_closure;     // Closure used for reference processing
  static CardTableExtension* _card_table;           // We cache the card table for fast access.
  static bool                _survivor_overflow;    // Overflow this collection
  static int                 _tenuring_threshold;   // tenuring threshold for next scavenge
static elapsedTimer _accumulated_gc_time;//total time spent on scavenge
  static elapsedTimer        _accumulated_undo_time;         // total time spent on promotion undo  
  static HeapWord*           _young_generation_boundary; // The lowest address possible for the young_gen.
                                                         // This is used to decide if an oop should be scavenged, 
                                                         // cards should be marked, etc.
static GrowableArray<markWord*>*_preserved_mark_stack;//List of marks to be restored after failed promotion
  static GrowableArray<oop>*     _preserved_oop_stack;  // List of oops that need their mark restored.
  static CollectorCounters*      _counters;         // collector performance counters
  static unsigned int            _total_invocations;         // Tracks the number of scavenge invocations
  static unsigned int            _total_promotion_failures;  // Tracks the number of promotion undos

  static void clean_up_failed_promotion();

  static bool should_attempt_scavenge();

  static HeapWord* to_space_top_before_gc() { return _to_space_top_before_gc; }
  static inline void save_to_space_top_before_gc();

  // Private accessors
  static CardTableExtension* const card_table()       { assert(_card_table != NULL, "Sanity"); return _card_table; }

 public:
  // Accessors
  static int              tenuring_threshold()  { return _tenuring_threshold; }
  static elapsedTimer*    accumulated_gc_time()       { return &_accumulated_gc_time; }
  static elapsedTimer*    accumulated_undo_time()     { return &_accumulated_undo_time; }
  static bool             promotion_failed()
    { return _preserved_mark_stack != NULL; }
  static int		  consecutive_skipped_scavenges() 
    { return _consecutive_skipped_scavenges; }

  // Performance Counters
  static CollectorCounters* counters()           { return _counters; }

  // Used by scavenge_contents && psMarkSweep
  static ReferenceProcessor* const reference_processor() {
    assert(_ref_processor != NULL, "Sanity"); 
    return _ref_processor;
  }
  // Used to add tasks
  static GCTaskManager* const gc_task_manager();
  // The promotion managers tell us if they encountered overflow
  static void set_survivor_overflow(bool state) {
    _survivor_overflow = state; 
  }
  // Adaptive size policy support.  When the young generation/old generation
  // boundary moves, _young_generation_boundary must be reset
  static void set_young_generation_boundary(HeapWord* v) {
    _young_generation_boundary = v;
  }

  // Called by parallelScavengeHeap to init the tenuring threshold
  static void initialize();

  // Scavenge entry point
  static void invoke();
  // Return true is a collection was done.  Return
  // false if the collection was skipped.
  static bool invoke_no_policy();

  // If an attempt to promote fails, this method is invoked
static void oop_promotion_failed(oop obj,markWord*obj_mark);

static inline bool should_scavenge(heapRef obj);

  // These call should_scavenge() above and, if it returns true, also check that
  // the object was not newly copied into to_space.  The version with the bool
  // argument is a convenience wrapper that fetches the to_space pointer from
  // the heap and calls the other version (if the arg is true).
  static inline bool should_scavenge(heapRef p, MutableSpace* to_space);
  static inline bool should_scavenge(heapRef p, bool check_to_space);

  inline static void copy_and_push_safe_barrier(PSPromotionManager* pm, heapRef* p);

  // Is an object in the young generation
  // This assumes that the HeapWord argument is in the heap, 
  // so it only checks one side of the complete predicate.
  inline static bool is_obj_in_young(HeapWord* o) {
    const bool result = (o >= _young_generation_boundary);
    return result;
  }
};

#endif // PSSCAVENGE_HPP

