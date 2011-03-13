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


#include "gpgc_pageAudit.hpp"
#include "gpgc_layout.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

MemRegion       GPGC_PageAudit::_reserved_region;
volatile bool*  GPGC_PageAudit::_real_array_pages_mapped = NULL;
volatile bool*  GPGC_PageAudit::_array_pages_mapped      = NULL;
GPGC_PageAudit* GPGC_PageAudit::_page_audit              = (GPGC_PageAudit*)0x1;  // should crash if used


void GPGC_PageAudit::initialize_page_audit()
{
  if ( GPGCPageAuditTrail ) {
    guarantee(SIZEOF_GPGC_PAGE_AUDIT == sizeof(GPGC_PageAudit), "GPGC_PageAudit size changed");

    // The reservation of memory actually happens in GPGC_Heap::initialize(), as part of the overall
    // reservation of GPGC's address space.

    char*  reservation_start       = (char*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_page_audit );
    long   reservation_large_pages = GPGC_Layout::end_of_page_audit - GPGC_Layout::start_of_page_audit;
    long   reservation_data_pages  = reservation_large_pages << LogDataPagesPerGPGCPage;
    size_t reservation_words       = reservation_data_pages  << LogWordsPerGPGCDataPage;

    _reserved_region = MemRegion((HeapWord*)reservation_start, reservation_words);

    // Create array to track which pages in the GPGC_PageAudit virtual memory range are mapped in.
    _real_array_pages_mapped = NEW_C_HEAP_ARRAY(bool, reservation_data_pages);
    for ( long i=0; i<reservation_data_pages; i++ ) {
_real_array_pages_mapped[i]=false;
    }
    _array_pages_mapped = _real_array_pages_mapped - (GPGC_Layout::start_of_page_audit << LogDataPagesPerGPGCPage);
    _page_audit         = (GPGC_PageAudit*) _reserved_region.start();

    // Make sure all GPGC_PageAudit's fit in the reserved address space.
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::start_of_small_space), "Bad GPGC_PageAudit memory range: small space start");
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::end_of_small_space-1), "Bad GPGC_PageAudit memory range: small space end");
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::start_of_mid_space),   "Bad GPGC_PageAudit memory range: mid space start");
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::end_of_mid_space-1),   "Bad GPGC_PageAudit memory range: mid space end");
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::start_of_large_space), "Bad GPGC_PageAudit memory range: large space start");
    guarantee( _reserved_region.contains(_page_audit+GPGC_Layout::end_of_large_space-1), "Bad GPGC_PageAudit memory range: large space end");
  }
}


bool GPGC_PageAudit::expand_pages_in_use(PageNum start_page, PageNum end_page)
{
  assert0(GPGCPageAuditTrail);
  assert0(GPGC_PageInfo_lock.owned_by_self());

  long     pages      = end_page - start_page;
  intptr_t start_addr = intptr_t( page_audit(start_page) );
  intptr_t end_addr   = intptr_t( page_audit(end_page)   );
  size_t   byte_size  = end_addr - start_addr;

  assert0( byte_size == sizeof(GPGC_PageAudit)*pages );

  PageNum audit_start_data_page = GPGC_Layout::addr_to_DataPageNum((void*)start_addr);
  PageNum audit_end_data_page   = GPGC_Layout::addr_to_DataPageNum((void*)(end_addr-1));

  for ( PageNum data_page=audit_start_data_page; data_page<=audit_end_data_page; data_page++ ) {
    if ( ! _array_pages_mapped[data_page] ) {
      if ( ! os::commit_memory((char*)GPGC_Layout::DataPageNum_to_addr(data_page), BytesPerGPGCDataPage, Modules::GPGC_PageAudit) ) {
        if ( GPGCTracePageSpace ) gclog_or_tty->print_cr("Unable to expand page_audit array");
        return false;
      }
      _array_pages_mapped[data_page] = true;
    }
  }

  return true;
}
