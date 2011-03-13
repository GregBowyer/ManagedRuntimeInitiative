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


#include "gpgc_heap.hpp" 
#include "gpgc_interlock.hpp" 
#include "gpgc_oldCollector.hpp" 
#include "gpgc_oldCollector.hpp" 
#include "gpgc_safepoint.hpp" 
#include "gpgc_space.hpp" 
#include "gpgc_verifyClosure.hpp" 
#include "log.hpp" 
#include "timer.hpp" 

#include "gpgc_pageInfo.inline.hpp"

// Object Mark Verification Safepoint
void GPGC_OldCollector::mark_remap_verify_mark(GPGC_Heap* heap, const char* label)
{
  if ( GPGCVerifyHeap )
  {
    {
      BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
      TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: mark verification setup",
                           label, _current_total_cycle, _current_old_cycle);
m.enter();

      // Mark verification can't happen while the other collector is mid-batch, relocating, clearing mark bitmaps, or flipping the NMT state.
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "O:");
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::NewGC_MarkClearing, "O:");
      // Mark verification can't happen while the other collector is verifying marks.
      GPGC_Interlock::acquire_interlock(&tdt, GPGC_Interlock::VerifyingMarking, "O:");
    }


    {
      BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
      TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: mark verification pause"
" "SIZE_FORMAT"M"
"("SIZE_FORMAT"M)",
                           label, _current_total_cycle, _current_old_cycle, heap->used()/M, heap->capacity()/M);
m.enter();

      if (_start_mark_verify_safepoint) GPGC_Safepoint::begin(&tdt);

      assert0( GPGC_Safepoint::is_at_safepoint() );
      set_collection_state(MarkingVerification);

      {
        DetailTracer dt(&tdt, false, "O: Clear verify marks");
        GPGC_Space::clear_verify_marks();
      }

      GPGC_VerifyClosure::verify_marking(&tdt, true, false, "O:");

      if (_end_mark_verify_safepoint) GPGC_Safepoint::end(&tdt);
    }


    {
      BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
      TimeDetailTracer tdt(m.enabled(), PrintGCDetails, true, m.stream(),
"%s GC cycle %d-%d: mark verification cleanup",
                           label, _current_total_cycle, _current_old_cycle);
m.enter();

      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::VerifyingMarking, "O:");
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewGC_MarkClearing, "O:");
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::NewGC_Relocate, "O:");
      GPGC_Interlock::release_interlock(&tdt, GPGC_Interlock::BatchedMemoryOps, "O:");
    }
  }
}
