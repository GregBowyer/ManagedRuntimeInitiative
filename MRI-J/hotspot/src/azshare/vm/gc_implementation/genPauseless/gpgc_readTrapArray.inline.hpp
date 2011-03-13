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


#ifndef GPGC_READTRAPARRAY_INLINE_HPP
#define GPGC_READTRAPARRAY_INLINE_HPP

#include "gpgc_collector.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageAudit.hpp"

#include "cycleCounts.hpp"
#include "os_os.inline.hpp"


inline void GPGC_ReadTrapArray::set_dupe_array_entries(long page, long pages, bool new_gen_page,
                                                       uint8_t trap_state, uint8_t mirror_trap_state, 
                                                       bool in_large_space, long upcoming_nmt)
{
  // Invariant: objects allocated in this page will have the right NMT bit
  // so the index derived from the objectref should have an UnTrapped entry.
  uint32_t index        = uint32_t(page) | ((upcoming_nmt)  << nmt_index_bit);
  uint32_t mirror_index = uint32_t(page) | ((!upcoming_nmt) << nmt_index_bit);

  _dupe_read_barrier_array[index]        = trap_state;
  _dupe_read_barrier_array[mirror_index] = mirror_trap_state;

  if ( in_large_space ) {
    // Large space pages after the first one shouldn't be getting tested for trap state.
    trap_state        = InvalidTrapState;
    mirror_trap_state = InvalidTrapState;
  }

  for ( long i=1; i<pages; i++ ) {
    assert0( GPGC_Layout::page_in_heap_range(page+i) );
    _dupe_read_barrier_array[index + i]        = trap_state;
    _dupe_read_barrier_array[mirror_index + i] = mirror_trap_state;
  }
}


inline void GPGC_ReadTrapArray::init_trap(PageNum page, long pages, bool new_gen_page, bool in_large_space)
{
  long nmt_flag = new_gen_page ? GPGC_NMT::desired_new_nmt_flag() : GPGC_NMT::desired_old_nmt_flag();
  long upcoming_nmt = new_gen_page ? GPGC_NMT::upcoming_new_nmt_flag() : GPGC_NMT::upcoming_old_nmt_flag();

  //Invariant: objects allocated in this page will have the right NMT bit
  // so the index derived from the objectref should have an UnTrapped entry.
  uint32_t nmt_index        = uint32_t(page) | ((nmt_flag)  << nmt_index_bit);
  uint32_t nmt_mirror_index = uint32_t(page) | ((!nmt_flag) << nmt_index_bit);

  uint8_t  trap_state       = UnTrapped;
  uint8_t  mirror_state     = NMTTrapped;

  _read_barrier_array[nmt_index]        = trap_state;
  _read_barrier_array[nmt_mirror_index] = mirror_state;

  if ( in_large_space ) {
    // Large space pages after the first one shouldn't be getting tested for trap state.
    trap_state   = InvalidTrapState;
    mirror_state = InvalidTrapState;
  }

  for ( long i=1; i<pages; i++ ) {
    assert0( GPGC_Layout::page_in_heap_range(page+i) );

    // the mutator visible rb-array entries reflect regular init value
    _read_barrier_array[nmt_index + i]        = trap_state;
    _read_barrier_array[nmt_mirror_index + i] = mirror_state;
  }

  set_dupe_array_entries(page, pages, new_gen_page, UnTrapped, NMTTrapped, in_large_space, upcoming_nmt);

  return;
}


inline void GPGC_ReadTrapArray::set_trap_state(PageNum page, long pages,  bool new_gen_page, 
                                               GPGC_PageInfo::States state, bool in_large_space, 
                                               long upcoming_nmt)
{
uint8_t trap_state=0;
uint8_t mirror_trap_state=0;

  if ( state  <= GPGC_PageInfo::Allocated ) {
    trap_state        = UnTrapped;
    mirror_trap_state = NMTTrapped;
  } else if ( state  <= GPGC_PageInfo::Relocated ) {
    trap_state        = new_gen_page ? NewGCRemapTrapped : OldGCRemapTrapped;
    mirror_trap_state = new_gen_page ? NewGCNMTRemapTrapped : OldGCNMTRemapTrapped ;
  } else {
    ShouldNotReachHere();
    // add asserts here
  }

  // blocks are always initialized in the init-path
  set_dupe_array_entries(page, pages, new_gen_page, trap_state, mirror_trap_state, in_large_space, upcoming_nmt);

#ifdef ASSERT
  long     desired_nmt_flag = new_gen_page ? GPGC_NMT::desired_new_nmt_flag()
                                            : GPGC_NMT::desired_old_nmt_flag();
  uint32_t index            = uint32_t(page) | (( desired_nmt_flag) << nmt_index_bit);
  uint32_t mirror_index     = uint32_t(page) | ((!desired_nmt_flag) << nmt_index_bit);

  assert0( GPGC_Layout::page_in_heap_range(page) );
  assert(((_read_barrier_array[index]        & NMTTrapped) == 0), "ref nmt doesnt match trap state");
  assert(((_read_barrier_array[mirror_index] & NMTTrapped) == NMTTrapped), "ref nmt doesnt match trap state");

  if ( in_large_space ) {
for(uint32_t i=1;i<pages;i++){
      assert0( GPGC_Layout::page_in_heap_range(page+i) );
      assert0( _read_barrier_array[index+i]             == InvalidTrapState );
      assert0( _read_barrier_array[mirror_index+i]      == InvalidTrapState );
      assert0( _dupe_read_barrier_array[index+i]        == InvalidTrapState );
      assert0( _dupe_read_barrier_array[mirror_index+i] == InvalidTrapState );
    }
  } else {
for(uint32_t i=1;i<pages;i++){
      assert0( GPGC_Layout::page_in_heap_range(page+i) );
      assert(((_read_barrier_array[index+i]        & NMTTrapped) == 0),"ref nmt doesnt match trap state");
      assert(((_read_barrier_array[mirror_index+i] & NMTTrapped) == NMTTrapped),"ref nmt doesnt match trap state" );
    }
  }
#endif 
}


inline void GPGC_ReadTrapArray::clear_trap_on_page(PageNum page, long pages)
{
  uint32_t  read_barrier_index        = uint32_t(page) | (0U << nmt_index_bit);
  uint32_t  mirror_read_barrier_index = uint32_t(page) | (1U << nmt_index_bit);

for(uint32_t i=0;i<pages;i++){
    assert0( GPGC_Layout::page_in_heap_range(page+i) );
    _dupe_read_barrier_array[read_barrier_index]          = ClearTrapState; 
    _dupe_read_barrier_array[mirror_read_barrier_index]   = ClearTrapState;
    _read_barrier_array[read_barrier_index+i]             = ClearTrapState;
    _read_barrier_array[mirror_read_barrier_index+i]      = ClearTrapState;
  }
}


inline void GPGC_ReadTrapArray::clear_trap_on_block(PageNum block, long pages, bool new_gen_block, bool alloc_failure)
{
  // we want the nmt state from the prev gc cycle, flip the current nmt bit and then OR
  long     nmt_flag                  = new_gen_block ? GPGC_NMT::desired_new_nmt_flag() : GPGC_NMT::desired_old_nmt_flag();
  uint32_t read_barrier_index        = uint32_t(block) | ((!nmt_flag) << nmt_index_bit); 
  uint32_t mirror_read_barrier_index = uint32_t(block) | (  nmt_flag  << nmt_index_bit); 

  // TODO: add more explicit asserts for the trap state we expect to see in both indicies for the block.
  assert0( GPGC_Layout::page_in_heap_range(block) );
  assert0( (_read_barrier_array[mirror_read_barrier_index] != UnTrapped) || alloc_failure  );

  _read_barrier_array[read_barrier_index]             = ClearTrapState;
  _read_barrier_array[mirror_read_barrier_index]      = ClearTrapState;
  _dupe_read_barrier_array[read_barrier_index]        = ClearTrapState;
  _dupe_read_barrier_array[mirror_read_barrier_index] = ClearTrapState;

  for ( long i=1; i<pages; i++ ) {
    assert0( GPGC_Layout::page_in_heap_range(block+i) );
    assert0( _read_barrier_array[read_barrier_index+i]        == InvalidTrapState );
    assert0( _read_barrier_array[mirror_read_barrier_index+i] == InvalidTrapState );
    assert0( _dupe_read_barrier_array[read_barrier_index+i]        == InvalidTrapState );
    assert0( _dupe_read_barrier_array[mirror_read_barrier_index+i] == InvalidTrapState );
  }
}


#endif // GPGC_READTRAPARRAY_INLINE_HPP
