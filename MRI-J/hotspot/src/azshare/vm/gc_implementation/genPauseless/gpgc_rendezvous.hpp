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
#ifndef GPGC_RENDEZVOUS_HPP
#define GPGC_RENDEZVOUS_HPP

// The GPGC_Rendezvous class helps the NewGC and OldGC threads synchronize their activities.

class GPGC_Rendezvous : public AllStatic
{
  // Coordinate taking of the GC_locker concurrent_gc lock.
  private:
    static long _gc_locker_concurrent;
  public:
    static void start_gc_locker_concurrent();
    static void end_gc_locker_concurrent();

  // Coordinate waiting for an OldGC cycle to start.
  private:
    static long _old_gc_pending;
  public:
    static void trigger_old_gc();
    static void wait_for_old_gc();

  // Coordinate marking setup completion and being ready to start root marking.
  private:
    static long _ready_for_root_marking;
  public:
    static void start_root_marking();

  // Coordinate ending the initial marking safepoint after root marking.
  private:
    static long _ready_for_end_marking_safepoint;
  public:
    static void end_marking_safepoint();

  // For non-concurrent collection modes, we need both the new and the
  // old cycles to coordinate the end of a safepoint.  This is used for
  // debug modes, and JVMTI heap iteration.
  private:
    static long _ready_for_end_coordinated_safepoint;
    static long _wait_for_end_coordinated_safepoint;
  public:
    static void end_coordinated_safepoint_prepare();
    static void end_coordinated_safepoint();

  // For JVMTI, we need both the new and old cycles to complete for a sync operation.
  // Coordinate ending the cycle
  private:
    static bool _should_coordinate_gc_cycle_end;
    static long _ready_for_gc_cycle_end;
  public:
    static void set_should_coordinate_gc_cycle_end(bool should_coordinate) { _should_coordinate_gc_cycle_end = should_coordinate; }
    static bool should_coordinate_gc_cycle_end() { return _should_coordinate_gc_cycle_end; }
    static void end_gc_cycle();

  // Coordinate NewGC cardmark scanning with OldGC relocation
  private:
    static volatile bool _suspend_relocating;
    static          long _active_relocating_threads;
  public:
    static void start_relocating_threads     (long threads);
    static void check_suspend_relocating     ();
    static void relocating_thread_done       ();
    static void verify_no_relocating_threads ();
    static void verify_relocating_threads    ();
    static void request_suspend_relocation   ();
    static void resume_relocation            ();
};

#endif // GPGC_RENDEZVOUS_HPP

