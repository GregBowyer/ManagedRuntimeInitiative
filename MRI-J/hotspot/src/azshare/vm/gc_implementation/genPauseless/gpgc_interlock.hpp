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
#ifndef GPGC_INTERLOCK_HPP
#define GPGC_INTERLOCK_HPP


#include "allocation.hpp"
#include "timer.hpp"

// The GPGC_Interlock class prevents the NewGC and OldGC threads from getting in each other's way.

class GPGC_Interlock:public StackObj{
  public:
    enum InterlockType {
      TLB_Resync                    = 0,  // Don't do two GPGC_TLB::tlb_resync()s at once.
      OldGC_RelocateOld             = 1,  // No OldGC OldGen relocation concurrent with NewGC scan old-space card-mark tables.
      OldGC_RelocatePerm            = 2,  // No OldGC PermGen relocation concurrent with NewGC.
      NewToOld_RootsFlushed         = 3,  // Don't finish OldGC until NewGC has delivered all roots to OldGC.
      NewGC_Relocate                = 4,  // No NewGC promotion while OldGC does final mark/remap ops
      VerifyingMarking              = 5,  // Don't verify marks while the other GC is verifying marks.
      OldGC_MarkClearing            = 6,  // Don't verify marks while OldGC is clearing marking bitmaps.
      NewGC_MarkClearing            = 7,  // Don't verify marks while NewGC is clearing marking bitmaps.
      NewGC_NMTFlipping             = 8,  // Don't verify marks while NewGC is flipping the NewGen NMT state.
      GPGC_Safepoint                = 9,  // Only one collector should safepoint at a time.
      SidebandAlloc                 = 10, // Only one collector should be allocing sideband space at a time.
      NewGC_CardMarkScan            = 11, // OldGC shouldn't refProcess and update cardmarks while NewGC is scanning them
      NewGC_SystemDictionaryMarking = 12, // OldGC shouldn't unlink the system dictionary while NewGC is marking
      OopTableUnlink                = 13, // Can't verify OopTable while the other collector is unlinking it.
      NewGC_Verifying               = 14, // Don't verify old objects while OldGC is scanning code cache.
      SymbolStringTable_verify      = 15,
      BatchedMemoryOps              = 16, // Only one batched memory op can be in progress at a time.
      
      MaxTypes                      = 17
    };

  private:
    static Thread*       _interlocks[MaxTypes];
    static const char*   _interlock_name[MaxTypes];
static elapsedTimer _timers[MaxTypes];

InterlockType _type;
    TimeDetailTracer*    _tdt;
    const char*          _gc_tag;

    static void acquire_interlock(InterlockType type);

  public:
    static void acquire_interlock(TimeDetailTracer* tdt, InterlockType type, const char* gc_tag);
    static void release_interlock(TimeDetailTracer* tdt, InterlockType type, const char* gc_tag);

    static bool interlock_held_by_self(InterlockType type);
    static bool interlock_held_by_OldGC(InterlockType type);

  public:
    GPGC_Interlock (TimeDetailTracer* tdt, InterlockType type, const char* gc_tag);
    // GPGC_Interlock (InterlockType type);
    ~GPGC_Interlock();
};

#endif // GPGC_INTERLOCK_HPP
