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


#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_lvb.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_tlb.hpp"
#include "klassIds.hpp"

#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"


objectRef GPGC_LVB::lvb_ref_fixup(uint64_t trap_flag, objectRef old_ref, objectRef* addr)
{
  assert(trap_flag != GPGC_ReadTrapArray::UnTrapped, "trap checking should already have been done");
assert(old_ref.not_null(),"shouldn't be LVB trapping on null objectRefs");

  Thread* thread = Thread::current();

if(thread->gcthread_lvb_trap_vector()){
    assert0(thread->is_GenPauselessGC_thread() || thread->is_GC_task_thread());

    if ( thread->is_gc_mode() || trap_flag <= GPGC_ReadTrapArray::NMTTrapped ) {
      // If a GC thread is in GC mode, we don't process traps.
      // If the trap is only for NMT mismatch, we don't process the trap.
      return old_ref;
    }

    assert0(trap_flag > GPGC_ReadTrapArray::NMTTrapped);
    // remap a relocation trapped objectRef:
    objectRef remapped_ref = GPGC_TLB::gc_thread_lvb_trap_from_c(thread, old_ref, (heapRef*)addr);
    return remapped_ref;
  } else {
    assert0(thread->is_Java_thread() || thread->is_VM_thread());

    if ( trap_flag > GPGC_ReadTrapArray::NMTTrapped ) {
      // remap trap.. 
      objectRef remapped_ref = GPGC_TLB::lvb_trap_from_c(thread, (heapRef)old_ref, (heapRef*)addr);
      return remapped_ref;
    }

    // NMT trap
    assert( trap_flag == GPGC_ReadTrapArray::NMTTrapped, "found undefined trap type" );
    assert( !GPGC_NMT::has_desired_nmt(old_ref), "mismatch between ref nmt and trap flag state" );

    objectRef new_ref = old_ref;
    new_ref.flip_nmt();
    assert(GPGC_NMT::has_desired_nmt(new_ref), "wrong nmt state after flip");

    intptr_t old_value = POISON_OBJECTREF(old_ref, addr).raw_value();
    intptr_t new_value = POISON_OBJECTREF(new_ref, addr).raw_value();
    if ( old_value == Atomic::cmpxchg_ptr(new_value, (intptr_t *)addr, old_value) ) {
      // Successfully CAS updated the ref in memory.  
      GPGC_Marks::set_nmt_update_markid(addr, 0xD0);
    } else {
      // CAS failed, someone else updated the ref.
      DEBUG_ONLY( objectRef reloaded = *addr; )
      DEBUG_ONLY( reloaded = PERMISSIVE_UNPOISON(reloaded, addr); )
      assert(reloaded.is_null() || GPGC_NMT::has_desired_nmt(reloaded), "wrong nmt state after failed CAS");
    }

    DEBUG_ONLY( GPGC_NMT::sanity_check_trapped_ref(new_ref); )

    // Push NMT corrected ref into the mutator's ref buffer.
    // TODO: It would be nice to know a real referrer klass id is.  We'd have to change the C++ LVB API to pass that along.
    if ( new_ref.is_new() ) {
      if ( ! thread->get_new_gen_ref_buffer()->record_ref_from_lvb(intptr_t(new_ref.raw_value()), KlassIds::jvm_internal_lvb) ) {
        assert0(thread->get_new_gen_ref_buffer()->is_full());
        GPGC_GCManagerNewStrong::push_mutator_stack_to_full(thread->get_new_gen_ref_buffer_addr());
      }
    } else {
      assert0( new_ref.is_old() );
      if ( ! thread->get_old_gen_ref_buffer()->record_ref_from_lvb(intptr_t(new_ref.raw_value()), KlassIds::jvm_internal_lvb) ) {
        assert0(thread->get_old_gen_ref_buffer()->is_full());
        GPGC_GCManagerOldStrong::push_mutator_stack_to_full(thread->get_old_gen_ref_buffer_addr());
      }
    }

    return new_ref;
  }

  ShouldNotReachHere();
  return nullRef;
}
