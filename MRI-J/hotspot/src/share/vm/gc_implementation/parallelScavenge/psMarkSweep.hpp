/*
 * Copyright 2001-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef PSMARKSWEEP_HPP
#define PSMARKSWEEP_HPP

#include "markSweep.hpp"
#include "timer.hpp"
class CollectorCounters;
class PSAdaptiveSizePolicy;
class PSYoungGen;
class PSOldGen;

class PSMarkSweep : public MarkSweep {
 private:
  static elapsedTimer        _accumulated_time;
  static unsigned int        _total_invocations;
  static jlong               _time_of_last_gc;   // ms
  static CollectorCounters*  _counters;

  // Closure accessors
  static OopClosure* mark_and_push_closure() { return &MarkSweep::mark_and_push_closure; }
  static VoidClosure* follow_stack_closure() { return (VoidClosure*)&MarkSweep::follow_stack_closure; }
  static OopClosure* adjust_pointer_closure() { return (OopClosure*)&MarkSweep::adjust_pointer_closure; }
static OopClosure*adjust_pointer_closure_skipping_CodeCache(){return(OopClosure*)&MarkSweep::adjust_pointer_closure_skipping_CodeCache;}
  static BoolObjectClosure* is_alive_closure() { return (BoolObjectClosure*)&MarkSweep::is_alive; }

 debug_only(public:)  // Used for PSParallelCompact debugging
  // Mark live objects
  static void mark_sweep_phase1(bool clear_all_softrefs);
  // Calculate new addresses
  static void mark_sweep_phase2();
 debug_only(private:) // End used for PSParallelCompact debugging
  // Update pointers
  static void mark_sweep_phase3();
  // Move objects to new positions
  static void mark_sweep_phase4();

 debug_only(public:)  // Used for PSParallelCompact debugging
  // Temporary data structures for traversal and storing/restoring marks
  static void allocate_stacks();
  static void deallocate_stacks();
  static void set_ref_processor(ReferenceProcessor* rp) {  // delete this method
    _ref_processor = rp;
  }
 debug_only(private:) // End used for PSParallelCompact debugging

  // If objects are left in eden after a collection, try to move the boundary
  // and absorb them into the old gen.  Returns true if eden was emptied.
  static bool absorb_live_data_from_eden(PSAdaptiveSizePolicy* size_policy,
					 PSYoungGen* young_gen,
					 PSOldGen* old_gen);

  // Reset time since last full gc
  static void reset_millis_since_last_gc();

 public:
  static void invoke(bool clear_all_softrefs);
  static void invoke_no_policy(bool clear_all_softrefs);

  static void initialize();

  // Public accessors
  static elapsedTimer* accumulated_time() { return &_accumulated_time; }
  static unsigned int total_invocations() { return _total_invocations; }
  static CollectorCounters* counters()    { return _counters; }

  // Time since last full gc (in milliseconds)
  static jlong millis_since_last_gc();
};

#endif // PSMARKSWEEP_HPP
