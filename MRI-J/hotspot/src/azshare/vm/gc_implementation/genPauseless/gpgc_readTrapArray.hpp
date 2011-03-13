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

#ifndef GPGC_READTRAPARRAY_HPP
#define GPGC_READTRAPARRAY_HPP

#include "allocation.hpp"
#include "objectRef_pd.hpp"
#include "timer.hpp"

#include "gpgc_collector.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_pageInfo.hpp"



/*
 * Trap Invariants: 
 * Trap state for a ref with the correct NMT: Untrapped or  RemapTrapped
 * Trap state for a ref with the Incorrect NMT: NMTtrapped or  NMTRemapTrapped
 */
class GPGC_ReadTrapArray:public AllStatic{
  private:

    static bool      _initialized;

    // One word flag per page to indicate trap type, relocation size and the relocation address.
    static uint8_t* _current_allocated_read_barrier_array;
    static uint8_t* _dupe_allocated_read_barrier_array;
    static uint8_t* _read_barrier_array;
    static uint8_t* _dupe_read_barrier_array;
    static uint64_t _rb_size_in_bytes;
    static bool     _array_remap_in_progress;
  

  public:

    enum BoolFlags {
      InLargeSpace    = true,
      NotInLargeSpace = false,
    };


    enum TrapFlags {
      // Valid page trap states are one of the following:
      UnTrapped              = 0x00,
      NMTTrapped             = 0x01,
      NewGCRemapTrapped      = 0x02,
      NewGCNMTRemapTrapped   = NewGCRemapTrapped | NMTTrapped,       // lvb should never see this state
      OldGCRemapTrapped      = 0x04,
      OldGCNMTRemapTrapped   = OldGCRemapTrapped | NMTTrapped,       // lvb should never see this state
      InvalidTrapState       = NewGCRemapTrapped | OldGCRemapTrapped, // Shouldn't ever be trapped on both gens
      ClearTrapState         = 0x7 // for debug purposes, for pages recycled after relocation
    };

    enum {
      nmt_index_bit   = objectRef::nmt_shift - LogBytesPerGPGCPage
    };

    enum {
      read_barrier_index_mask = (address_word) right_n_bits(objectRef::unknown_bits + objectRef::nmt_bits - LogBytesPerGPGCPage),
      PageNum_from_index_mask = (address_word) right_n_bits(objectRef::unknown_bits - LogBytesPerGPGCPage)
    };

    enum {
      trap_bits       = 3,
      trap_shift      = 0
    };

    enum {
      trap_mask                = (address_word) right_n_bits(trap_bits),
      trap_mask_in_place       = (address_word) trap_mask << trap_shift,
    };


    static bool addr_in_rb_array_range(intptr_t va) {
#ifdef ASSERT
      return (va >= intptr_t(_current_allocated_read_barrier_array) &&
             (va <= intptr_t(_current_allocated_read_barrier_array + _rb_size_in_bytes)));
#endif
      return (va <= intptr_t(_current_allocated_read_barrier_array + _rb_size_in_bytes));
    }

    static uint32_t objectRef_to_read_barrier_index(objectRef ref) {
      assert(!ref.is_poisoned(), "No poisoned refs here");
      assert(ref.is_null() || ref.is_stack() || ref.is_heap(), "Not an oop");
      assert((ref.raw_value()&0x7)==0, "Not aligned");
      return (uint32_t)((ref.raw_value() >> LogBytesPerGPGCPage) & read_barrier_index_mask);
    }

    static uint32_t pagenum_to_read_barrier_index(PageNum page, bool new_gen_page) {
      assert0( GPGC_Layout::page_in_heap_range(page) );
      long nmt_flag = new_gen_page ? GPGC_NMT::desired_new_nmt_flag() : GPGC_NMT::desired_old_nmt_flag();
      return (uint32_t)(page | (nmt_flag << nmt_index_bit));
    }

    static void batched_array_swap      (TimeDetailTracer* tdt, GPGC_Collector::CollectorAge age);
    static void swap_readbarrier_arrays (TimeDetailTracer* tdt, GPGC_Collector::CollectorAge age);

    inline static bool array_remap_in_progress     ()              { return _array_remap_in_progress; }
    inline static void set_array_remap_in_progress (bool rb_remap) { _array_remap_in_progress = rb_remap; }

    inline static void set_dupe_array_entries  (PageNum page,  long pages, bool new_gen_page,   uint8_t trap_state, 
                                                uint8_t mirror_trap_state, bool in_large_space, long    newgen_upcoming_nmt);
    inline static void init_trap               (PageNum page,  long pages, bool new_gen_page,   bool    in_large_space);
    inline static void set_trap_state          (PageNum page,  long pages, bool new_gen_page,   GPGC_PageInfo::States state,
                                                bool    in_large_space,    long _upcoming_nmt);


    inline static void clear_trap_on_page (PageNum page,  long pages);
    inline static void clear_trap_on_block(PageNum block, long pages, bool new_gen_block, bool alloc_failure = false);

   inline static uint64_t trap_flags(uint64_t entry) {
      uint64_t flag =  entry & trap_mask;
      assert0(is_valid_trap_flag(flag));
      return flag; 
    }

    inline static bool is_valid_trap_flag(uint64_t flag){
      return (flag >=0x0) && (flag <=0x5);
    }

    inline static bool is_any_trapped         (objectRef ref, uint64_t* trap_flags);
    inline static bool is_remap_trapped       (objectRef ref);

    inline static bool is_new_gc_remap_trapped(heapRef ref);
    inline static bool is_old_gc_remap_trapped(heapRef ref);

    static void    initialize();
static bool initialized(){return _initialized;}
    
static address address_of_read_barrier_array(){return(address)_read_barrier_array;}
};


inline bool GPGC_ReadTrapArray::is_any_trapped(objectRef ref, uint64_t* trap_flags)
{
  assert0(GPGC_Layout::page_in_heap_range(GPGC_Layout::addr_to_PageNum(ref.as_oop())));

  uint32_t index = objectRef_to_read_barrier_index(ref);
  uint64_t entry  = _read_barrier_array[index];
  bool     result = (entry != UnTrapped);

  assert0((entry&InvalidTrapState) != InvalidTrapState);
  assert0(!((entry == NMTTrapped) && GPGC_NMT::has_desired_nmt(ref)));

  if( trap_flags ) *trap_flags = entry;

  return result;
}


inline bool GPGC_ReadTrapArray::is_remap_trapped(objectRef ref)
{
  assert0(GPGC_Layout::page_in_heap_range(GPGC_Layout::addr_to_PageNum(ref.as_oop())));

  uint32_t index = objectRef_to_read_barrier_index(ref);
  uint64_t entry  = _read_barrier_array[index];
  bool     result = (entry > NMTTrapped);

  assert0((entry&InvalidTrapState) != InvalidTrapState);

  return result;
}


inline bool GPGC_ReadTrapArray::is_new_gc_remap_trapped(heapRef ref)
{
  assert0(GPGC_Layout::page_in_heap_range(GPGC_Layout::addr_to_PageNum(ref.as_oop())));

  uint32_t index  = objectRef_to_read_barrier_index(ref);
  uint64_t entry  = _read_barrier_array[index];
  bool     result = (entry & NewGCRemapTrapped);

  assert0((entry&InvalidTrapState) != InvalidTrapState);

  return result;
}


inline bool GPGC_ReadTrapArray::is_old_gc_remap_trapped(heapRef ref)
{
  assert0(GPGC_Layout::page_in_heap_range(GPGC_Layout::addr_to_PageNum(ref.as_oop())));

  uint32_t index  = objectRef_to_read_barrier_index(ref);
  uint64_t entry  = _read_barrier_array[index];
  bool     result = (entry & OldGCRemapTrapped);

  assert0((entry&InvalidTrapState) != InvalidTrapState);

  return result;
}


#endif //GPGC_READTRAPARRAY_HPP
