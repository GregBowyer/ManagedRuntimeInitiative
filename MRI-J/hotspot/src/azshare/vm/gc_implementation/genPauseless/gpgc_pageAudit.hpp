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

#ifndef GPGC_PAGEAUDIT_HPP
#define GPGC_PAGEAUDIT_HPP

#include "allocation.hpp"
#include "atomic.hpp"
#include "os.hpp"
#include "thread.hpp"

#include "thread_os.inline.hpp"


typedef uint64_t PageAuditWhy;


class GPGC_PageAudit VALUE_OBJ_CLASS_SPEC
{
  private:
static MemRegion _reserved_region;
    static volatile bool*  _real_array_pages_mapped;
    static volatile bool*  _array_pages_mapped;
    static GPGC_PageAudit* _page_audit;

  public:
    static void            initialize_page_audit();
    static bool            expand_pages_in_use(PageNum start_page, PageNum end_page);

    static GPGC_PageAudit* page_audit(PageNum page)   { return &_page_audit[page]; }

    inline static void     audit_entry(PageNum page, PageAuditWhy why, long op);

  private:
    enum Consts {
      RingSize = 64,
      RingMask = RingSize-1
    };

    uint64_t  _ring_buffer[RingSize];
    jlong     _next;
uintptr_t _unused_pad;

  public:
    enum Operation {
      GetPreAlloc    = 0x1,
      AllocPage      = 0x2,
      GetForRemap    = 0x3,
      FailedAlloc    = 0x4,
      CloseShared    = 0x5,
      RemapToMirror  = 0x6,
      FreeMirror     = 0x7,
      FreePage       = 0x8,
      Released       = 0x9
    };

    inline void write_audit_entry(uint64_t entry);
};


inline void GPGC_PageAudit::write_audit_entry(uint64_t entry)
{
  if ( GPGCPageAuditTrail ) {
    jlong old_next;
    jlong new_next;

    do {
old_next=_next;
      new_next = (old_next+2) & RingMask;
    } while ( old_next != Atomic::cmpxchg(new_next, &_next, old_next) );

    _ring_buffer[old_next]   = os::elapsed_counter();
    _ring_buffer[old_next+1] = entry;
  }
}


inline void GPGC_PageAudit::audit_entry(PageNum page, PageAuditWhy why, long op)
{
  uint64_t tid   = Thread::current()->reversible_tid();
  uint64_t entry = tid | (uint64_t(op)<<44) | (why<<48);

  //
  // Audit entry format:
  //
  // 0xWWGG OTTT TTTT TTTT
  //
  //  WW: Why code
  //  GG: Generation
  //  O:  GPGC_PageAudit::Operation
  //  T:  Reverisble Thread ID
  //
  //      4 4433 2221 1
  //      8 4062 8406 284
  // 0xWWGG OTTT TTTT TTTT
  page_audit(page)->write_audit_entry(entry);
}

#endif // GPGC_PAGEAUDIT_HPP
