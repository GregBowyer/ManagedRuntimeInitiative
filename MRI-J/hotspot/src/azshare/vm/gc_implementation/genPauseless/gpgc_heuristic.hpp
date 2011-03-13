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
#ifndef GPGC_HEURISTIC_HPP
#define GPGC_HEURISTIC_HPP

/*
 *  GPGC cycle timing heuristic:
 *
 *  Heap behavior models that feed into heuristic:
 *
 *  - NewGC cycle time.
 *  - NewToOld cycle time.
 *  - OldGC cycle time.
 *  - Allocation rate in NewGen
 *  - Allocation rate in PermGen
 *  - Promotion rate in OldGen
 *
 */

#include "gpgc_stats.hpp"

class GPGC_Heuristic: public AllStatic
{
  private:
    // Current values for the allocation and cycle time models:
    static double _new_gen_alloc_words_per_sec;
    static double _old_gen_alloc_words_per_sec;
    static double _perm_gen_alloc_words_per_sec;

    static double _new_gc_cycle_seconds;
    static double _old_gc_cycle_seconds;

    // Intermediate values for updating the allocation models:
    static double _last_sample_new_gen_words;
    static long   _last_sample_new_gen_ticks;

    static long   _last_sample_old_gen_ticks;

    static double _last_sample_perm_gen_words;
    static long   _last_sample_perm_gen_ticks;

    static size_t _cycle_model_biggest_heap;
    static size_t _cycle_model_biggest_old;


    // Helper functions:
    static double exp_average        (double old_average, double new_value, double alpha);
    static double alpha_for_half_life(double half_life, double interval);

    // Use the GC models to see if it's time for a GC cycle:
    static long   time_until_gc(bool always_log);


  public:
    // Allocation model updates:
    static void   update_new_gen_allocation_model (double MB_per_sec, double measurement_secs);
    static void   update_new_gen_allocation_model ();
    static void   update_old_gen_allocation_model (long promoted_pages);
    static void   update_perm_gen_allocation_model();

    // Cycle time model updates:
    static void   update_new_gc_cycle_model       (GPGC_CycleStats* stats);
    static void   update_old_gc_cycle_model       (GPGC_CycleStats* stats);

    static void   start_allocation_model          ();

    static long   time_to_sleep                   ();
    static bool   need_immediate_old_gc           ();
};
#endif
