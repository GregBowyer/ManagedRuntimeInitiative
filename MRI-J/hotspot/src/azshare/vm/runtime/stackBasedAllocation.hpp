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
#ifndef STACKBASEDALLOCATION_HPP
#define STACKBASEDALLOCATION_HPP


#include "handles.hpp"
#include "stackRef_pd.hpp"

#define SBA_MAX_FID             ((1 << stackRef::frame_id_bits)-1)

// fake frameid used to represent the heap:
#define HEAP_FID                  -1
#define SBA_ROOT_FID              0

#define ESCAPED_FIELD_VALUE       0xEEEEEEEEEEEEEEEE

bool is_valid_fid(uint64_t fid);

class StackBasedAllocation:public AllStatic{
public:
  static intptr_t _allocation_statistics[SBA_MAX_FID+2]; // Access is biased by +1, so HEAP_FID is 0
  static intptr_t _escape_processing_ticks;
  static intptr_t _stack_gc_ticks;
  static intptr_t _stack_gc_findlive_ticks;
  static intptr_t _stack_gc_resize_ticks;
  static intptr_t _stack_gc_copycompact_ticks;
  static void collect_allocation_statistics( int fid, int size_in_bytes );
  static void collect_heap_allocation_statistics( int size_in_bytes ) { 
    if( UseSBA ) collect_allocation_statistics(HEAP_FID,size_in_bytes); }
  
  // these escape the object and followers
  static objectRef       do_escape(JavaThread* thread, stackRef escapee, int escape_to_fid, int kid, const char *reason);
  static objectRef  do_heap_escape(JavaThread* thread, stackRef escapee );
  static objectRef do_stack_escape(JavaThread* thread, stackRef escapee, int escape_to_fid);
  static void ensure_in_heap(JavaThread* thread, jobject escapee, const char *reason);
  static void ensure_in_heap(JavaThread* thread, Handle  escapee, const char *reason);

static void forward_derived_stack_oop(objectRef*base,objectRef*derived);
static void do_nothing_derived(objectRef*base,objectRef*derived);

  static void initialize();
  
  static void out_of_memory_error(const char* msg);

  // Statistics
static void print_statistics(outputStream*st);
  static void print_to_xml_statistics(xmlBuffer *xb);
};

#endif // STACKBASEDALLOCATION_HPP
