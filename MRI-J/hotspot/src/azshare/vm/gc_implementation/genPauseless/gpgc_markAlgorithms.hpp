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
#ifndef GPGC_MARKALGORITHMS_HPP
#define GPGC_MARKALGORITHMS_HPP



#include "universe.hpp"
#include "collectedHeap.hpp"
#include "gpgc_gcManagerNewFinal.inline.hpp"
#include "gpgc_gcManagerNewStrong.inline.hpp"
#include "gpgc_gcManagerOldFinal.inline.hpp"
#include "gpgc_gcManagerOldStrong.inline.hpp"


class GPGC_MarkAlgorithm: public AllStatic
{
  public:

    template <class T>
    inline static void mark_through(T* gcm, objectRef ref, int referrer_kid) {
      // The objectRef parameter is either an objectRef to mark through, or a chunk of an object array.
      // Object array chunks have the reserved bit set in the objectRef.
      if ( ref.reserved() )  {
        // Found a chunk, mark it:
	unsigned int chunk_size = ref.non_address();  // The length of the chunk is in the bits that cannot be virtual address.
        objectRef*   chunk_base = (objectRef*) ref.unreserved_address();
        objectRef*   chunk_end  = chunk_base + chunk_size;
        while (chunk_base < chunk_end) {
          heapRef base_ref = PERMISSIVE_UNPOISON(*chunk_base, chunk_base);
if(base_ref.not_null()){
            assert0(base_ref.is_heap());
            // we call mark_and_follow here to avoid excessive marking stack usage
            gcm->mark_and_follow(chunk_base, referrer_kid);
          }
          chunk_base++; 
        }
      } else {
        gcm->mark_through(ref, referrer_kid);
      }
    }


    template <class T>
    static void drain_stacks(T* gcm) {
      assert0( Universe::heap()->kind() == CollectedHeap::GenPauselessHeap );

      while (true) {
        do {
          intptr_t raw_value;
          int      referrer_kid;
          while ( gcm->current_stack()->swap_local(raw_value, referrer_kid) ) {
            GPGC_MarkAlgorithm::mark_through(gcm, objectRef(raw_value), referrer_kid);
          }
        } while ( gcm->get_full_current_stack() );

        assert0(gcm->current_stack()->is_empty());

        HeapRefBuffer* mutator_stack = gcm->get_mutator_stack();
if(mutator_stack==NULL){
          return;
        } else {
          gcm->process_mutator_stack(mutator_stack);
        }
      }
    }


    template <class T>
    static void drain_and_steal_stacks(T* gcm) {
      GPGC_MarkAlgorithm::drain_stacks(gcm);
      assert0(gcm->current_stack()->is_empty());
      gcm->decrement_working_count();

      do {
        while ( true ) {
          while ( gcm->get_full_current_stack() ) {
            gcm->increment_working_count();
            GPGC_MarkAlgorithm::drain_stacks(gcm);
            assert0(gcm->current_stack()->is_empty());
            gcm->decrement_working_count();
          }

          HeapRefBuffer* mutator_stack = gcm->get_mutator_stack();
if(mutator_stack!=NULL){
            gcm->increment_working_count();
            gcm->process_mutator_stack(mutator_stack);
            GPGC_MarkAlgorithm::drain_stacks(gcm);
            gcm->decrement_working_count();
            continue;
          }

          break;
        }

objectRef stolenRef;
        int       referrer_kid;
        if ( gcm->steal_from_remote_thread(stolenRef, referrer_kid) ) {
          //assert0( referrer_kid != 0 );
          gcm->increment_working_count();
          GPGC_MarkAlgorithm::mark_through(gcm, stolenRef, referrer_kid);
          GPGC_MarkAlgorithm::drain_stacks(gcm);
          assert0(gcm->current_stack()->is_empty());
          gcm->decrement_working_count();
        }
      } while ( gcm->working_count() > 0 );
    }
};

#endif // GPGC_MARKALGORITHMS_HPP
