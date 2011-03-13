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

#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "handles.hpp"
#include "interfaceSupport.hpp"
#include "vmTags.hpp"

#include "prefetch_os_pd.inline.hpp"

long GPGC_NMT::_desired_new_nmt_flag = 0;
long GPGC_NMT::_desired_old_nmt_flag = 0;
long GPGC_NMT::_upcoming_new_nmt_flag = 0;
long GPGC_NMT::_upcoming_old_nmt_flag = 0;
int32_t GPGC_NMT::_new_trap_enabled  = false;
int32_t GPGC_NMT::_old_trap_enabled  = false;


GCT_LEAF(void, GPGC_NMT, new_space_nmt_buffer_full, (Thread* thread))
{
assert(UseLVBs,"Shouldn't be taking NMT traps if not UseLVBs");
  assert0(thread->get_new_gen_ref_buffer());





  GPGC_GCManagerNewStrong::push_mutator_stack_to_full( thread->get_new_gen_ref_buffer_addr() );
}
GCT_END


GCT_LEAF(void, GPGC_NMT, old_space_nmt_buffer_full, (Thread* thread))
{
assert(UseLVBs,"Shouldn't be taking NMT traps if not UseLVBs");
  assert0(thread->get_old_gen_ref_buffer());





  GPGC_GCManagerOldStrong::push_mutator_stack_to_full( thread->get_old_gen_ref_buffer_addr() );
}
GCT_END


void GPGC_NMT::sanity_check_trapped_ref(objectRef ref)
{
  assert0(ref.is_heap());
  assert0(!GPGC_ReadTrapArray::is_remap_trapped(ref));

  if ( ref.is_new() ) {
    long sanity = GPGC_NewCollector::mutator_ref_sanity_check();
    if ( sanity != GPGC_Collector::NoSanityCheck ) {
      bool strong_live = GPGC_Marks::is_any_marked_strong_live(ref.as_oop());
      bool final_live  = GPGC_Marks::is_any_marked_final_live(ref.as_oop());
      switch (sanity) {
        case GPGC_Collector::NoMutatorRefs:
          fatal2("NewGC NMT trap outside marking, strong live %d, final live %d", strong_live, final_live);
          break;
        case GPGC_Collector::MustBeFinalLive:
          // Normal marking is done, but ref processing is still in progress.  Only StrongLive or FinalLive objects expected.
          if ( ! (strong_live || final_live) ) {
            fatal2("NewGC non live mutator ref, strong live %d, final live %d", strong_live, final_live);
          }
          break;
        default:
ShouldNotReachHere();//Unknown sanity check state.
      }
    }
  }
  else {
    long sanity = GPGC_OldCollector::mutator_ref_sanity_check();
    if ( sanity != GPGC_Collector::NoSanityCheck ) {
      bool strong_live = GPGC_Marks::is_any_marked_strong_live(ref.as_oop());
      bool final_live  = GPGC_Marks::is_any_marked_final_live(ref.as_oop());
      switch (sanity) {
        case GPGC_Collector::NoMutatorRefs:
          fatal2("OldGC NMT trap outside marking, strong live %d, final live %d", strong_live, final_live);
          break;
        case GPGC_Collector::MustBeFinalLive:
          // Normal marking is done, but ref processing is still in progress.  Only StrongLive or FinalLive objects expected.
          if ( ! (strong_live || final_live) ) {
            fatal2("OldGC non live mutator ref, strong live %d, final live %d", strong_live, final_live);
          }
          break;
        case GPGC_Collector::MustBeStrongLive:
          if ( ! strong_live ) {
fatal1("OldGC non StrongLive mutator ref, final live %d",final_live);
          }
          break;
        default:
ShouldNotReachHere();//Unknown sanity check state.
      }
    }
  }
}


GCT_LEAF(void, GPGC_NMT, sanity_check, (Thread* thread, objectRef ref))
{
  sanity_check_trapped_ref(ref);
}
GCT_END
