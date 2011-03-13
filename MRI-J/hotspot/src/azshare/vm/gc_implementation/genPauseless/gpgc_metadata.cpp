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


#include "gpgc_layout.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_metadata.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "thread.hpp"

#include "mutex.inline.hpp"


GPGC_SparseMappedSpace* GPGC_Metadata::strong_marks_space  = NULL;
GPGC_SparseMappedSpace* GPGC_Metadata::final_marks_space   = NULL;
GPGC_SparseMappedSpace* GPGC_Metadata::verify_marks_space  = NULL;
GPGC_SparseMappedSpace* GPGC_Metadata::mark_throughs_space = NULL;
GPGC_SparseMappedSpace* GPGC_Metadata::mark_ids_space      = NULL;


void GPGC_Metadata::initialize()
{
  // TODO: Decide if the metadata here shouldn't share the GPGC_MetaData_lock. 

  strong_marks_space = new GPGC_SparseMappedSpace(Modules::GPGC_StrongMarks,
                                                  GPGC_Layout::start_of_strong_mark_bitmap,
                                                  GPGC_Layout::end_of_strong_mark_bitmap,
                                                  LogBytesPerGPGCPage,
                                                  &GPGC_MetaData_lock);
  final_marks_space  = new GPGC_SparseMappedSpace(Modules::GPGC_FinalMarks,
                                                  GPGC_Layout::start_of_final_mark_bitmap,
                                                  GPGC_Layout::end_of_final_mark_bitmap,
                                                  LogBytesPerGPGCPage,
                                                  &GPGC_MetaData_lock);

  if ( GPGCVerifyHeap ) {
    verify_marks_space = new GPGC_SparseMappedSpace(Modules::GPGC_VerifyMarks,
                                                    GPGC_Layout::start_of_verify_mark_bitmap,
                                                    GPGC_Layout::end_of_verify_mark_bitmap,
                                                    LogBytesPerGPGCPage,
                                                    &GPGC_MetaData_lock);
  }

  if ( GPGCAuditTrail ) {
    mark_throughs_space = new GPGC_SparseMappedSpace(Modules::GPGC_MarkThroughs,
                                                     GPGC_Layout::start_of_mark_throughs_bitmap,
                                                     GPGC_Layout::end_of_mark_throughs_bitmap,
                                                     LogBytesPerGPGCPage,
                                                     &GPGC_MetaData_lock);
    mark_ids_space      = new GPGC_SparseMappedSpace(Modules::GPGC_MarkIDs,
                                                     GPGC_Layout::start_of_mark_ids_bytemap,
                                                     GPGC_Layout::end_of_mark_ids_bytemap,
                                                     LogBytesPerGPGCPage,
                                                     &GPGC_MetaData_lock);
  }
}


bool GPGC_Metadata::expand_metadata_for_heap(PageNum start_page, long pages)
{
  PageNum  end_page         = start_page + pages;

  intptr_t start_page_addr  = (intptr_t)GPGC_Layout::PageNum_to_addr(start_page);
  intptr_t end_page_addr    = (intptr_t)GPGC_Layout::PageNum_to_addr(end_page) - BytesPerWord;

  long     start_word_index = GPGC_Marks::bit_index_for_mark(start_page_addr) >> LogBitsPerWord;
  long     end_word_index   = GPGC_Marks::bit_index_for_mark(end_page_addr)   >> LogBitsPerWord;

  if ( ! strong_marks_space->expand(GPGC_Layout::strong_mark_bitmap_base() + start_word_index,
                                    GPGC_Layout::strong_mark_bitmap_base() + end_word_index) ) {
    return false;
  }
  if ( ! final_marks_space->expand(GPGC_Layout::final_mark_bitmap_base() + start_word_index,
                                   GPGC_Layout::final_mark_bitmap_base() + end_word_index) ) {
    return false;
  }

  if ( GPGCVerifyHeap ) {
    if ( ! verify_marks_space->expand(GPGC_Layout::verify_mark_bitmap_base() + start_word_index,
                                      GPGC_Layout::verify_mark_bitmap_base() + end_word_index) ) {
       return false;
    }
  }

  if ( GPGCAuditTrail ) {
    start_word_index = GPGC_Marks::bit_index_for_marked_through(start_page_addr) >> LogBitsPerWord;
    end_word_index   = GPGC_Marks::bit_index_for_marked_through(end_page_addr)   >> LogBitsPerWord;

    if ( ! mark_throughs_space->expand(GPGC_Layout::mark_throughs_bitmap_base() + start_word_index,
                                       GPGC_Layout::mark_throughs_bitmap_base() + end_word_index) ) {
      return false;
    }

    long start_byte_index = GPGC_Marks::byte_index_for_markid(start_page_addr);
    long end_byte_index   = GPGC_Marks::byte_index_for_markid(end_page_addr);

    if ( ! mark_ids_space->expand(GPGC_Layout::mark_ids_bytemap_base() + start_byte_index,
                                  GPGC_Layout::mark_ids_bytemap_base() + end_byte_index) ) {
      return false;
    }
  }

  return true;
}
