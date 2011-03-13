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
#ifndef GPGC_PAGEBUDGET_HPP
#define GPGC_PAGEBUDGET_HPP



class GPGC_PageBudget:public AllStatic{
  private:
    // Page usage limits:
    static          long    _committed_page_budget;

    // Allocation modes:
    static volatile bool    _leak_into_grant_enabled;
    static volatile bool    _pause_account_enabled;

    // Page usage stats:
    static          long    _normal_pages_used;
    static          long    _pause_pages_used;

    static          long    _peak_normal_pages;  // For linux peak accounting/azmem cross checking
    static          long    _peak_pause_pages;   // For linux peak accounting/azmem cross checking

    static          size_t  _newgc_peak_normal_bytes;
    static          size_t  _newgc_peak_pause_bytes;
    static          size_t  _oldgc_peak_normal_bytes;
    static          size_t  _oldgc_peak_pause_bytes;

    // Pause allocation stats:
    static volatile long    _azmem_pause_alloced;
    static          bool    _pause_allocation_failed;

    // Pre-allocated pages:
    static          PageNum _preallocated_page_base;
    static          long    _free_preallocated;

  public:
    static void    initialize                   (long committed_budget);
static void preallocate_page(Thread*thread);
    static void    return_pause_pages           ();
    static void    return_grant_pages           ();

    static void    report_newgc_peak_usage      (size_t* committed_result, size_t* grant_result, size_t* pause_result);
    static void    report_oldgc_peak_usage      (size_t* committed_result, size_t* grant_result, size_t* pause_result);

    static long    committed_budget             ()  { return _committed_page_budget; }
    static long    normal_pages_used            ()  { return _normal_pages_used; }
    static long    peak_normal_pages            ()  { return _peak_normal_pages; }
    static long    pause_pages_used             ()  { return _pause_pages_used; }
    static long    peak_pause_pages             ()  { return _peak_pause_pages; }
    static bool    pause_allocation_failed      ()  { return _pause_allocation_failed; }
    static long    azmem_pause_pages            ()  { return _azmem_pause_alloced; }

    static void    enable_leak_into_grant       ();
    static void    disable_leak_into_grant      ();
    static void    enable_pause_account         ();
    static void    disable_pause_account        ();

    static bool    leak_into_grant_enabled      ()  { return _leak_into_grant_enabled; }
    static bool    pause_account_enabled        ()  { return _pause_account_enabled; }

    static bool    allocate_page                (PageNum page);
    static bool    allocate_pages               (PageNum base_page, long pages);
    static void    get_preallocated_page        (PageNum page, bool small_space=true);//change the default value here.. 

    static bool    deallocate_mapped_page       (PageNum page);
    static long    deallocate_mapped_block      (PageNum block, long pages);
    static void    account_for_deallocate       (long pages, const char* label, PageNum page);

    static PageNum preallocated_page_base       ()  { return _preallocated_page_base; }
    static PageNum preallocated_page_for_thread ();

    // Debugging:
    static void    new_gc_verify_preallocated_pages ();
    static void    old_gc_verify_preallocated_pages ();
    static long    pages_used                       ()  { return _normal_pages_used + _pause_pages_used - _free_preallocated; }
    static void    verify_this_thread               ();

  private:
    static void    capture_current_peak         ();
    static void    report_peak_usage            (size_t peak_normal, size_t peak_pause,
                                                 size_t* committed_result, size_t* grant_result, size_t* pause_result);

    static void    increment_normal_pages       (long pages);
    static void    increment_pause_pages        (long pages);

    static bool    use_page_budget              (long pages);
    static void    record_budget_used           (long pages);
    static void    free_page_budget             (long pages);

    static bool    deallocate_preallocated_page (PageNum page);
};

#endif // GPGC_PAGEBUDGET_HPP

