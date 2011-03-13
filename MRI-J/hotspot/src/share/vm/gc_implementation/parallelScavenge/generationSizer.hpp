/*
 * Copyright 2001-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GENERATIONSIZER_HPP
#define GENERATIONSIZER_HPP

#include "collectorPolicy.hpp"

// There is a nice batch of tested generation sizing code in
// TwoGenerationCollectorPolicy. Lets reuse it!

class GenerationSizer : public TwoGenerationCollectorPolicy {
 public:
  GenerationSizer() {
    // Partial init only!
    initialize_flags();
    initialize_size_info();
  }

  void initialize_flags() {
    // Do basic sizing work
    this->TwoGenerationCollectorPolicy::initialize_flags();
    
    // If the user hasn't explicitly set the number of worker
    // threads, set the count.
    if (ParallelGCThreads == 0) {
      assert(UseParallelGC, "Setting ParallelGCThreads without UseParallelGC");
      ParallelGCThreads = 8; // Do not use the active_processor_count, its just to far from an optimal value.
guarantee(ParallelGCThreads>0,"Must be at least one gc thread");
    }

    SurvivorRatio += 2; // Emulate the default gc behavior

    if (MinSurvivorRatio < 3) {
      MinSurvivorRatio = 3;
    }

    if (SurvivorRatio < 3) {
      SurvivorRatio = 3;
    }

    if (SurvivorRatio < (int)MinSurvivorRatio) {
      SurvivorRatio = MinSurvivorRatio;
    }
  }

  size_t min_young_gen_size() { return _min_gen0_size; }
  size_t young_gen_size()     { return _initial_gen0_size; }
  size_t max_young_gen_size() { return _max_gen0_size; }

  size_t min_old_gen_size()   { return _min_gen1_size; }
  size_t old_gen_size()       { return _initial_gen1_size; }
  size_t max_old_gen_size()   { return _max_gen1_size; }

  size_t perm_gen_size()      { return PermSize; }
  size_t max_perm_gen_size()  { return MaxPermSize; }
};

#endif // GENERATIONSIZER_HPP

