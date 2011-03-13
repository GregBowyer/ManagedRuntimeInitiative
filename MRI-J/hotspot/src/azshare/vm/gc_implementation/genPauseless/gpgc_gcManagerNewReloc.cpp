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

#include "gpgc_gcManagerNewReloc.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"
#include "gpgc_relocation.hpp"

GPGC_GCManagerNewReloc**GPGC_GCManagerNewReloc::_manager_array=NULL;
long                     GPGC_GCManagerNewReloc::_manager_count = 0;


//*****
//*****  Static Methods
//*****

void GPGC_GCManagerNewReloc::initialize()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"Sanity");

assert(_manager_array==NULL,"Attempted to initialize twice.");

  _manager_count = GenPauselessNewThreads;
  _manager_array = NEW_C_HEAP_ARRAY(GPGC_GCManagerNewReloc*, _manager_count);

  for ( long i=0; i<_manager_count; i++ ) {
    _manager_array[i] = new GPGC_GCManagerNewReloc(i);
  }
}


void GPGC_GCManagerNewReloc::set_generations()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewReloc* gcm = get_manager(i);

    gcm->new_relocation_buffer()->set_generation(GPGC_PageInfo::NewGen, GPGC_PageInfo::NewGen);
    gcm->old_relocation_buffer()->set_generation(GPGC_PageInfo::OldGen, GPGC_PageInfo::NewGen);
  }
}


void GPGC_GCManagerNewReloc::reset_remap_buffers()
{
  GPGC_RemapTargetArray* mid_space_targets = GPGC_NewCollector::page_pops()->mid_space_targets();

  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewReloc* gcm = get_manager(i);

    gcm->new_remap_buffer()->reset();
    gcm->old_remap_buffer()->reset();

    gcm->new_remap_buffer()->set_generation(GPGC_PageInfo::NewGen);
    gcm->old_remap_buffer()->set_generation(GPGC_PageInfo::OldGen);

    gcm->new_remap_buffer()->set_mid_space_targets(mid_space_targets);
    gcm->old_remap_buffer()->set_mid_space_targets(mid_space_targets);
  }
}


void GPGC_GCManagerNewReloc::total_page_counts(long* new_pages, long* old_pages)
{
  long new_result = 0;
  long old_result = 0;

  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewReloc* gcm = get_manager(i);

    new_result += gcm->new_relocation_buffer()->page_count();
    old_result += gcm->old_relocation_buffer()->page_count();
  }

  *new_pages = new_result;
  *old_pages = old_result;
}



//*****
//*****  Instance Methods
//*****

GPGC_GCManagerNewReloc::GPGC_GCManagerNewReloc(long manager_number)
  : GPGC_GCManager(manager_number)
{
  _new_relocation_buffer = new GPGC_RelocBuffer();
  _old_relocation_buffer = new GPGC_RelocBuffer();

  _new_remap_buffer = new GPGC_RemapBuffer();
  _old_remap_buffer = new GPGC_RemapBuffer();
}
