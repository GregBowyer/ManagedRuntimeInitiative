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
#ifndef DEOPTIMIZATION_HPP
#define DEOPTIMIZATION_HPP


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.


#include "allocation.hpp"
#include "codeBlob.hpp"
#include "thread.hpp"
struct IFrame;

class Deoptimization : AllStatic {
 public:
  // What condition caused the deoptimization?
   enum DeoptReason {
     Reason_BAD,
     Reason_deopt,               // Real Deoptimization in-progress!
Reason_unloaded,//Class not loaded at time of compile
Reason_unreached,//Profile data shows zero counts
     Reason_athrow,              // Bytecode _athrow,  record & recompile 
Reason_null_check,//failed null check, record & recompile
Reason_div0_check,//failed div0 check, record & recompile
Reason_range_check,//failed range check, record & recompile
     Reason_range_check_widened, // failed "widened" range check, record & recompile
     Reason_cast_check,          // failed optimized checkcast, record & recompile
Reason_unhandled,//unimplemented,     do not recompile
     Reason_array_store_check,   // failed optimized checkcast, record & recompile
Reason_uninitialized,//uninitialized class, recompile after interpreter executes
     Reason_unloaded_array_class,// array-class not loaded, recompile after interpreter loads
     Reason_unexpected_klass,    // type prediction failure, invalidate profile and recompile
     Reason_unexpected_null_cast,// cast-never-null failed; record failure and recompile
     Reason_intrinsic_check,     // Some intrinsic failed; do not intrinsify
     Reason_install_async,       // Forced deopt to install async exception
     Reason_new_exception,       // Throwing an exception on return from a new
     Reason_jvmti,               // Cutout for JVMTI
     Reason_static_in_clinit,    // Cutout for static field ref in <clinit>
     Reason_stuck_in_loop,       // For breaking out of large C1 loops so we can hop up to a C2 OSR
     Reason_for_volatile,        // patching failed on volatile field; record & recompile
     Reason_max
   };

  // Performs an uncommon trap for compiled code.
  // The top most compiler frame is converted into interpreter frames
  static address uncommon_trap(JavaThread* thread, jint deopt_index);

  // Performs a deoptimization for compiled code.
  // The top most compiler frame is converted into interpreter frames
static address deoptimize(JavaThread*thread);

  // Record the failure reason and decide if we should recompile the nmethod.
  static bool policy_for_recompile( JavaThread *thread, const CodeBlob *cb, int deopt_index );

  // Build an array of interpreter control frames (which the ASM code will
  // inject after all C frames are popped off - plus the top level compile
  // frame).  Helps the ASM code repack the 1 compiled frame into many
  // interpreter (or C1) frames.  Takes in the current thread and a vframe;
  // the vframe is pointing and the virtual Java frame needing to be repacked.
  // It takes in the callee (which this frame is busy trying to call in it's
  // inlined code), and an array of IFrames.
  static void build_repack_buffer( JavaThread *thread, frame fr, IFrame *buf, const DebugMap *dm, const DebugScope *ds, intptr_t* jexstk, objectRef *lckstk, bool is_deopt, bool is_c1, bool is_youngest);

  // Fetch return values from a deopt-in-progress
  static intptr_t pd_fetch_return_values( JavaThread *thread, BasicType return_type );

  static void print_stat_line( int i, long total );
  static void print_statistics();
  static void print_statistics(xmlBuffer* xb);
  static long counters[Reason_max];
  static const char *names[Reason_max];

  static jint total_deoptimization_count();
  static jint deoptimization_count(DeoptReason reason);
  static void increment_deoptimization_count(DeoptReason reason);

  // JVMTI PopFrame support

  // Preserves incoming arguments to the popped frame when it is
  // returning to a deoptimized caller
  static void popframe_preserve_args(JavaThread* thread, int bytes_to_save, void* start_address);

};

class DeoptimizationMarker : StackObj {  // for profiling
  static bool _is_active;  
public:
  DeoptimizationMarker()  { _is_active = true; }
  ~DeoptimizationMarker() { _is_active = false; }
  static bool is_active() { return _is_active; }
};

#endif
