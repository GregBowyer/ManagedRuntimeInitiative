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


#include "gpgc_interlock.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_safepoint.hpp"
#include "java.hpp"
#include "modules.hpp"

#include "allocation.inline.hpp"
#include "os_os.inline.hpp"
#include "os/azulmmap.h"


bool GPGC_ReadTrapArray::_initialized=false;
uint8_t* GPGC_ReadTrapArray::_current_allocated_read_barrier_array  = NULL;
uint8_t* GPGC_ReadTrapArray::_read_barrier_array                    = NULL;
uint8_t* GPGC_ReadTrapArray::_dupe_allocated_read_barrier_array     = NULL;
uint8_t* GPGC_ReadTrapArray::_dupe_read_barrier_array               = NULL;
uint64_t GPGC_ReadTrapArray::_rb_size_in_bytes                      = 0;
bool     GPGC_ReadTrapArray::_array_remap_in_progress               = false;

void GPGC_ReadTrapArray::initialize()
{
  if (UseGenPauselessGC) {
    uint64_t array_size = GPGC_Layout::end_of_heap_range - GPGC_Layout::start_of_heap_range;
    _rb_size_in_bytes   = round_to(2 * array_size * sizeof(uint8_t), os::vm_page_size());
    _current_allocated_read_barrier_array =  (uint8_t *)__GPGC_READ_BARRIER_ARRAY_START_ADDR__;
    _dupe_allocated_read_barrier_array    =  (uint8_t *)__GPGC_DUPE_READ_BARRIER_ARRAY_START_ADDR__;

    if ( ! os::commit_memory((char*)_current_allocated_read_barrier_array, _rb_size_in_bytes, Modules::GPGC_ReadTrapArray) ) {
vm_exit_during_initialization("Unable to allocate gpgc-read barrier array");
    }

    if ( ! os::commit_memory((char*)_dupe_allocated_read_barrier_array, _rb_size_in_bytes, Modules::GPGC_ReadTrapArray) ) {
vm_exit_during_initialization("Unable to allocate dupe gpgc-read barrier array");
    }
 
assert(UnTrapped==0,"Initialization is wrong if Untrapped isn't 0");
    memset(_current_allocated_read_barrier_array, 0, _rb_size_in_bytes);
    memset(_dupe_allocated_read_barrier_array, 0, _rb_size_in_bytes);
    _read_barrier_array           = _current_allocated_read_barrier_array - GPGC_Layout::start_of_heap_range;
    _dupe_read_barrier_array      = _dupe_allocated_read_barrier_array - GPGC_Layout::start_of_heap_range;

    _initialized = true;
  }
}


void GPGC_ReadTrapArray::batched_array_swap(TimeDetailTracer* tdt, GPGC_Collector::CollectorAge age)
{
  assert0(GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps));
  assert0(BatchedMemoryOps);

  {
    DetailTracer dt(tdt, false, "%s: Batched swap of read-barrier array", ((age==GPGC_Collector::NewCollector)?"N":"O"));

    os::batched_relocate_memory((char*)_current_allocated_read_barrier_array,
                                (char*)__GPGC_SWAP_READ_BARRIER_ARRAY_ADDR__,
                                _rb_size_in_bytes);
    os::batched_relocate_memory((char*)_dupe_allocated_read_barrier_array,
                                (char*)_current_allocated_read_barrier_array,
                                _rb_size_in_bytes);
    os::batched_relocate_memory((char*)__GPGC_SWAP_READ_BARRIER_ARRAY_ADDR__,
                                (char*)_dupe_allocated_read_barrier_array,
                                _rb_size_in_bytes);
  }

//  if ( !swap_readbarrier_for_relocation ) {
//    GPGC_Collector::commit_batched_memory_ops(tdt, age);
//  }
}


void GPGC_ReadTrapArray::swap_readbarrier_arrays(TimeDetailTracer* tdt, GPGC_Collector::CollectorAge age)
{
  assert0( ! BatchedMemoryOps );
  assert0( GPGC_Safepoint::is_at_safepoint() );

  // TODO: Add assert to check if the dupe-rb_array has the right values for the right page ranges.

  // This function does a non-atomic swap of the read barrier arrays.  It's only safe inside
  // a safepoint.  Since the swap is non-atomic, the other collector may have threads that try
  // and read the read trap array when it doesn't exist.  We set a flag telling the signal handler
  // to retry the read to let the brief window of no barrier array pass.

  assert0(!GPGC_ReadTrapArray::array_remap_in_progress());
  GPGC_ReadTrapArray::set_array_remap_in_progress(true);

  {
    DetailTracer dt(tdt, false, "%s: Remap swap of read-barrier array", ((age==GPGC_Collector::NewCollector)?"N":"O"));

    os::relocate_memory((char*)_current_allocated_read_barrier_array,
                        (char*)__GPGC_SWAP_READ_BARRIER_ARRAY_ADDR__,
                        _rb_size_in_bytes);
    os::relocate_memory((char*)_dupe_allocated_read_barrier_array,
                        (char*)_current_allocated_read_barrier_array,
                        _rb_size_in_bytes);
    os::relocate_memory((char*)__GPGC_SWAP_READ_BARRIER_ARRAY_ADDR__,
                        (char*)_dupe_allocated_read_barrier_array,
                        _rb_size_in_bytes);
  }

  // Let the SEGV signal handler know that the read trap array is expected to exist.
  GPGC_ReadTrapArray::set_array_remap_in_progress(false);
}
