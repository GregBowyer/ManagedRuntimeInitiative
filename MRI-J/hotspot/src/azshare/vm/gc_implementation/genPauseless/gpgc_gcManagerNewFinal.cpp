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

#include "gpgc_gcManagerNewFinal.hpp"
#include "gpgc_heap.hpp"

GPGC_GCManagerNewFinal**GPGC_GCManagerNewFinal::_manager_array=NULL;
long                     GPGC_GCManagerNewFinal::_manager_count        = 0;
HeapRefBufferList**      GPGC_GCManagerNewFinal::_free_ref_buffer_list = NULL;
HeapRefBufferList**      GPGC_GCManagerNewFinal::_full_ref_buffer_list = NULL;


void GPGC_GCManagerNewFinal::initialize()
{
GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
assert(heap->kind()==CollectedHeap::GenPauselessHeap,"Sanity");

assert(_manager_array==NULL,"Attempted to initialize twice.");

  _manager_count        = GenPauselessNewThreads;
  _manager_array        = NEW_C_HEAP_ARRAY(GPGC_GCManagerNewFinal*, _manager_count);
  _free_ref_buffer_list = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);
  _full_ref_buffer_list = NEW_C_HEAP_ARRAY(HeapRefBufferList*, HeapRefBufferListStripes);

for(int i=0;i<HeapRefBufferListStripes;i++){
    _free_ref_buffer_list[i] = new HeapRefBufferList("new final free lists");
    _full_ref_buffer_list[i] = new HeapRefBufferList("new final full lists");
  }

for(int i=0;i<_manager_count;i++){
    _manager_array[i] = new GPGC_GCManagerNewFinal(i);
  }
}


bool GPGC_GCManagerNewFinal::all_stacks_are_empty()
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewFinal* gcm = get_manager(i);
    if ( ! gcm->current_stack()->is_empty() ) {
      return false;
    }
  }

  return GPGC_GCManagerMark::striped_list_is_empty(_full_ref_buffer_list);
}


void GPGC_GCManagerNewFinal::ensure_all_stacks_are_empty(const char* tag)
{
  for ( long i=0; i<_manager_count; i++ ) {
    GPGC_GCManagerNewFinal* gcm = get_manager(i);
    if ( ! gcm->current_stack()->is_empty() ) {
      fatal2("NewFinal current stack isn't empty for manager %d (%s)", i, tag);
    }
  }

  GPGC_GCManagerMark::verify_empty_heap_ref_buffers(_full_ref_buffer_list, "NewFinal", tag);
}
