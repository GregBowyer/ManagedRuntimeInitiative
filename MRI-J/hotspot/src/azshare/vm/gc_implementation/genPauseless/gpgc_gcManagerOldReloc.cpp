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

#include "gpgc_gcManagerOldReloc.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_population.hpp"


GPGC_GCManagerOldReloc**GPGC_GCManagerOldReloc::_manager_array=NULL;
long                     GPGC_GCManagerOldReloc::_manager_count = 0;


//*****
//*****  Static Methods
//*****

void GPGC_GCManagerOldReloc::initialize()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"Sanity");

assert(_manager_array==NULL,"Attempted to initialize twice.");

  _manager_count = GenPauselessOldThreads;
  _manager_array = NEW_C_HEAP_ARRAY(GPGC_GCManagerOldReloc*, _manager_count);

  for ( long i=0; i<_manager_count; i++ ) {
    _manager_array[i] = new GPGC_GCManagerOldReloc(i);
  }
}


void GPGC_GCManagerOldReloc::set_generations(GPGC_PageInfo::Gens gen)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldReloc* gcm = get_manager(i);

    gcm->relocation_buffer()->set_generation(gen, gen);
  }
}


long GPGC_GCManagerOldReloc::total_page_count()
{
  long count = 0;

  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldReloc* gcm = get_manager(i);

    count += gcm->relocation_buffer()->page_count();
  }

  return count;
}


void GPGC_GCManagerOldReloc::reset_remap_buffer(GPGC_PageInfo::Gens gen)
{
  GPGC_RemapTargetArray* mid_space_targets = GPGC_OldCollector::page_pops()->mid_space_targets();

  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerOldReloc* gcm = get_manager(i);

    gcm->remap_buffer()->reset();
    gcm->remap_buffer()->set_generation(gen);
    gcm->remap_buffer()->set_mid_space_targets(mid_space_targets);
  }
}


//*****
//*****  Instance Methods
//*****

GPGC_GCManagerOldReloc::GPGC_GCManagerOldReloc(long manager_number)
  : GPGC_GCManager(manager_number)
{
  _relocation_buffer = new GPGC_RelocBuffer();
  _remap_buffer      = new GPGC_RemapBuffer();
}
