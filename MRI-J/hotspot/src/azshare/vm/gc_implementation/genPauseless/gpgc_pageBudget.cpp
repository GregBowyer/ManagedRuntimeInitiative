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


#include "cycleCounts.hpp"
#include "gcTaskThread.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_population.hpp"
#include "gpgc_thread.hpp"
#include "java.hpp"
#include "mutexLocker.hpp"
#include "os_os.hpp"
#include "ostream.hpp"
#include "thread.hpp"
#include "timer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "os_os.inline.hpp"

         long    GPGC_PageBudget::_committed_page_budget    = 0;

volatile bool    GPGC_PageBudget::_leak_into_grant_enabled  = false;
volatile bool    GPGC_PageBudget::_pause_account_enabled    = true;

         long    GPGC_PageBudget::_normal_pages_used        = 0;
         long    GPGC_PageBudget::_pause_pages_used         = 0;

         long    GPGC_PageBudget::_peak_normal_pages        = 0;
         long    GPGC_PageBudget::_peak_pause_pages         = 0;

         size_t  GPGC_PageBudget::_newgc_peak_normal_bytes  = 0;
         size_t  GPGC_PageBudget::_newgc_peak_pause_bytes   = 0;

         size_t  GPGC_PageBudget::_oldgc_peak_normal_bytes  = 0;
         size_t  GPGC_PageBudget::_oldgc_peak_pause_bytes   = 0;

volatile long    GPGC_PageBudget::_azmem_pause_alloced      = 0;
         bool    GPGC_PageBudget::_pause_allocation_failed  = false;

         PageNum GPGC_PageBudget::_preallocated_page_base   = NoPage;
         long    GPGC_PageBudget::_free_preallocated        = 0;


void GPGC_PageBudget::verify_this_thread()
{
Thread*thread=Thread::current();
if(thread->is_GC_task_thread()){
    assert0(((GCTaskThread*)thread)->get_preallocated_page() != NoPage);
  }
  else if ( thread->is_GenPauselessGC_thread() ) {
    assert0(((GPGC_Thread*)thread)->get_preallocated_page() != NoPage);
  }
  else {
fatal("Must be the GPGC_Thread or a GCTaskThread to verify preallocated page state");
  }
}


void GPGC_PageBudget::initialize(long committed_budget)
{
  assert0(committed_budget > 0);

  _committed_page_budget    = committed_budget;

  _leak_into_grant_enabled  = false;
  _pause_account_enabled    = true;

  _normal_pages_used        = 0;
  _peak_normal_pages        = 0;
  _pause_pages_used         = 0;
  _peak_pause_pages         = 0;
  _azmem_pause_alloced      = 0;
  _pause_allocation_failed  = false;

  _preallocated_page_base   = GPGC_Layout::start_of_preallocated;

  size_t java_committed     = _committed_page_budget << LogBytesPerGPGCPage;

  os::fund_memory_accounts(java_committed);
}


void GPGC_PageBudget::preallocate_page(Thread* thread)
{
  guarantee(thread->is_GC_task_thread() || thread->is_GenPauselessGC_thread(),
"Can only preallocate pages for the GPGC_Thread and GCTaskThreads");
  guarantee(preallocated_page_base() != NoPage, "GPGC_PageBudget not yet initialized");

  long offset=0;

if(thread->is_GC_task_thread()){
    offset = ((GCTaskThread*)thread)->thread_number();
  }
  else if ( thread->is_GenPauselessGC_thread() ) {
    offset = ((GPGC_Thread*)thread)->thread_number();
  }
  else {
fatal("Can only preallocate pages for the GPGC_Thread and GCTaskThreads");
  } 
  
  PageNum page = preallocated_page_base() + offset;

  if ( page >= GPGC_Layout::end_of_preallocated ) {
vm_exit_during_initialization("Too many preallocated pages to fit in reserved space.");
  }

  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::commit_memory);
    if ( ! os::commit_heap_memory(os::JAVA_COMMITTED_MEMORY_ACCOUNT, (char*)GPGC_Layout::PageNum_to_addr(page), BytesPerGPGCPage, false) ) {
vm_exit_during_initialization("Insufficient physical memory: Unable to allocate preallocated heap page.");
    }
  }

if(thread->is_GC_task_thread()){
    guarantee(((GCTaskThread*)thread)->get_preallocated_page() == NoPage, "GCTaskThread already has preallocated page");
    ((GCTaskThread*)thread)->set_preallocated_page(page);
  } else {
    guarantee(((GPGC_Thread*)thread)->get_preallocated_page() == NoPage, "GPGC_Thread already has preallocated page");
    ((GPGC_Thread*)thread)->set_preallocated_page(page);
  }

  GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget +1 for preallocated page 0x%lX", page);
  increment_normal_pages(1);
  Atomic::inc_ptr((intptr_t*)&_free_preallocated);

  if ( _normal_pages_used >= _committed_page_budget ) {
vm_exit_during_initialization("Committed memory is too small for pauseless GC's preallocated relocation pages");
  }
}

 
/*
 * Algorithm for reporting peak usage:
 *
 * Each NewGC cycle, report & clear peak from azmem.
 *   Alternative: clear peak at start of NewGC, report peak within cycle?
 *
 * Each OldGC cycle, report the highest NewGC peak reported since
 *   A: the start of the OldGC cycle?
 *   B: the end of the last OldGC cycle?
 *
 */

void GPGC_PageBudget::capture_current_peak()
{
  assert0(GPGC_PeakMem_lock.owned_by_self());

if(os::use_azmem()){
    // Query azmem for how many pages we actually used from the committed/grant account.
    int64_t balance;
    int64_t balance_min;
    int64_t balance_max;
    size_t  allocated;
    size_t  allocated_min;
    size_t  allocated_max;
    size_t  maximum;

    // Get peak usage for the JAVA_COMMITTED_MEMORY_ACCOUNT:
    os::memory_account_get_stats(os::JAVA_COMMITTED_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max,
                                 &maximum);
    os::memory_account_reset_watermarks(os::JAVA_COMMITTED_MEMORY_ACCOUNT);

    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Java heap usage: balance %ld (%ld-%ld), allocated %zd (%zd-%zd)",
                                                      balance,   balance_min,   balance_max,
                                                      allocated, allocated_min, allocated_max);

    if ( _newgc_peak_normal_bytes < allocated_max ) { _newgc_peak_normal_bytes = allocated_max; }
    if ( _oldgc_peak_normal_bytes < allocated_max ) { _oldgc_peak_normal_bytes = allocated_max; }

    // Get peak usage for the JAVA_PAUSE_MEMORY_ACCOUNT:
    os::memory_account_get_stats(os::JAVA_PAUSE_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max,
                                 &maximum);
    os::memory_account_reset_watermarks(os::JAVA_PAUSE_MEMORY_ACCOUNT);

    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Pause prevention usage: balance %ld (%ld-%ld), allocated %zd (%zd-%zd)",
                                                      balance,   balance_min,   balance_max,
                                                      allocated, allocated_min, allocated_max);

    if ( _newgc_peak_pause_bytes < allocated_max ) { _newgc_peak_pause_bytes = allocated_max; }
    if ( _oldgc_peak_pause_bytes < allocated_max ) { _oldgc_peak_pause_bytes = allocated_max; }

  } else {
    // Only azmem has pause prevention memory:
assert(_peak_pause_pages==0,"Not expecting to see pause memory in use unless we're running with azmem!");

    uint64_t peak_normal_bytes = uint64_t(_peak_normal_pages << LogBytesPerGPGCPage);

    if ( _newgc_peak_normal_bytes < peak_normal_bytes ) { _newgc_peak_normal_bytes = peak_normal_bytes; }
    if ( _oldgc_peak_normal_bytes < peak_normal_bytes ) { _oldgc_peak_normal_bytes = peak_normal_bytes; }
  }
}


void GPGC_PageBudget::report_peak_usage(size_t peak_normal_bytes, size_t peak_pause_bytes,
                                        size_t* committed_result, size_t* grant_result, size_t* pause_result)
{
  // Round up to page size, so we don't under-report grant or pause usage.
  long peak_normal_pages = (peak_normal_bytes + BytesPerGPGCPage - 1) >> LogBytesPerGPGCPage;
  long peak_pause_pages  = (peak_pause_bytes  + BytesPerGPGCPage - 1) >> LogBytesPerGPGCPage;

  if ( peak_normal_pages > _committed_page_budget ) {
    *committed_result = _committed_page_budget;
    *grant_result     = peak_normal_pages - _committed_page_budget;
  } else {
    *committed_result = peak_normal_pages;
    *grant_result     = 0;
  }

  *pause_result = peak_pause_pages;
}


void GPGC_PageBudget::report_newgc_peak_usage(size_t* committed_result, size_t* grant_result, size_t* pause_result)
{
MutexLocker ml(GPGC_PeakMem_lock);

  capture_current_peak();
  report_peak_usage(_newgc_peak_normal_bytes, _newgc_peak_pause_bytes, committed_result, grant_result, pause_result);

  _newgc_peak_normal_bytes = 0;
  _newgc_peak_pause_bytes  = 0;
}


void GPGC_PageBudget::report_oldgc_peak_usage(size_t* committed_result, size_t* grant_result, size_t* pause_result)
{
MutexLocker ml(GPGC_PeakMem_lock);

  capture_current_peak();
  report_peak_usage(_oldgc_peak_normal_bytes, _oldgc_peak_pause_bytes, committed_result, grant_result, pause_result);

  _oldgc_peak_normal_bytes = 0;
  _oldgc_peak_pause_bytes  = 0;
}


// Atomically update the number of pages allocated by the specified amount, and also update the peaks
// if new highs have been hit.
void GPGC_PageBudget::increment_normal_pages(long pages)
{
assert(pages>0,"Only incrementing the counter");
  Atomic::add_and_record_peak(pages, &_normal_pages_used, &_peak_normal_pages);
}


void GPGC_PageBudget::increment_pause_pages(long pages)
{
  Atomic::add_and_record_peak(pages, &_pause_pages_used, &_peak_pause_pages);
}


// Any pages allocation through the pause account are intended for short term use only.  This
// method attempts to return allocated pause pages to the kernel.
void GPGC_PageBudget::return_pause_pages()
{
assert(Thread::current()->is_GenPauselessGC_thread(),"only GPGC_Thread should be flushing deallocated pages");

  // Query azmem for how many pages we actually used from the pause account.
  int64_t balance;
  int64_t balance_min;
  int64_t balance_max;
  size_t  allocated;
  size_t  allocated_min;
  size_t  allocated_max;
  size_t  maximum;
  os::memory_account_get_stats(os::JAVA_PAUSE_MEMORY_ACCOUNT, &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

  long peak_pause_used = allocated / M;

  // And flush the pause account if any pages are in use.
  if ( peak_pause_used > 0 ) {
    size_t flushed;
    size_t allocated;

    os::flush_memory(os::JAVA_PAUSE_MEMORY_ACCOUNT, &flushed, &allocated);

    assert(flushed  %BytesPerGPGCPage==0, "flushed uneven number of pause pages");
    assert(allocated%BytesPerGPGCPage==0, "uneven number of pause pages allocated");

    _azmem_pause_alloced = (allocated - flushed) >> LogBytesPerGPGCPage;

    // if ( _azmem_pause_alloced > 0 ) {
    //   // If we're still using some pause pages after the GC cycle, we don't get to use
    //   // more pause pages until the usage is fully repaid.
    //   if ( _pause_account_enabled ) {
    //     GCLogMessage::log(PrintGCDetails, gclog_or_tty,
    //                       "Pause not fully repaid, disabling use of pause account: unreturned %d pages",
    //                       _azmem_pause_alloced);
    //   }
    //   _pause_account_enabled = false;
    // } else {
    //   if ( !_pause_account_enabled ) { 
    //     GCLogMessage::log(PrintGCDetails, gclog_or_tty, "Pause fully repaid, enabling use of pause account");
    //   }
    //   _pause_account_enabled = true;
    // }
  } else {
    _azmem_pause_alloced = 0;
  }

  _pause_allocation_failed = false;

  GPGC_CycleStats* stats; 

  if ( ((GPGC_Thread*)Thread::current())->is_new_collector() ) {
    stats = GPGC_Heap::heap()->gc_stats()->new_gc_cycle_stats();
  } else {
    assert0( ((GPGC_Thread*)Thread::current())->is_old_collector() );
    stats = GPGC_Heap::heap()->gc_stats()->old_gc_cycle_stats();
  }
  
  stats->set_unreturned_pause(_azmem_pause_alloced);
}


// This function tries to return deallocated pages to azmem's global grant fund.  But we don't want
// to flush memory when it won't be returning grant pages, because that may just cause excess hits
// on azmem's global fund locks.
//
void GPGC_PageBudget::return_grant_pages()
{
assert(Thread::current()->is_GenPauselessGC_thread(),"only GPGC_Thread should be flushing deallocated pages");

  // Query azmem for how many pages we actually used from the committed account
  int64_t balance = 0;
  int64_t balance_min = 0;
  int64_t balance_max = 0;
  size_t  allocated = 0;
  size_t  allocated_min = 0;
  size_t  allocated_max = 0;
  size_t  maximum = 0;
  size_t  flushed = 0;

  os::memory_account_get_stats(os::JAVA_COMMITTED_MEMORY_ACCOUNT,
                               &balance, &balance_min, &balance_max,
                               &allocated, &allocated_min, &allocated_max,
                               &maximum);

  // If page usage exceeded the committed page budget, we flush memory.
  if (balance < 0) {
    os::flush_memory(os::JAVA_COMMITTED_MEMORY_ACCOUNT, &flushed, &allocated);

    GCLogMessage::log_b(PrintGCDetails || PrintGC, gclog_or_tty, "GPGC Warning: Page usage exceeded committed budget - allocated %lld MB flushed %lld MB", allocated / M, flushed / M);
  }
}


void GPGC_PageBudget::enable_leak_into_grant()
{
if(os::use_azmem()){
    // TODO let azmem go into grant
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Not enabling leak-into-grant allocation: unsupported on Linux");
/*
    // TODO need a better mode to test on for grant being enabled.  azmem might not always have grant.
    if ( ! _leak_into_grant_enabled ) {
      GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Enabling leak-into-grant allocation");
      _leak_into_grant_enabled = true;
    }
*/
  } else {
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Not enabling leak-into-grant allocation: unsupported on Linux");
  }
}


void GPGC_PageBudget::disable_leak_into_grant()
{
  if ( _leak_into_grant_enabled ) {
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Disabling leak-into-grant allocation");
    _leak_into_grant_enabled = false;
  }
}


void GPGC_PageBudget::enable_pause_account()
{
  // If there are unpaid pause pages from a prior cycle, then we don't enable future use of the
  // pause account until the prior use is fully repaid.
  if ( _azmem_pause_alloced==0 && !_pause_account_enabled ) {
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Enabling use of pause account");
    _pause_account_enabled = true;
  }
}


void GPGC_PageBudget::disable_pause_account()
{
  if ( _pause_account_enabled ) {
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "Disabling use of pause account");
    _pause_account_enabled = false;
  }
}


void GPGC_PageBudget::free_page_budget(long pages)
{
  assert0(pages>0);

  // Try and return pause account pages first.
  long pause_used = _pause_pages_used;
  while ( pause_used > 0 ) {
    long new_pause_used = pause_used - pages;
    if ( new_pause_used < 0 ) new_pause_used = 0;

    if ( pause_used == Atomic::cmpxchg(new_pause_used, (jlong*)&_pause_pages_used, pause_used) ) {
      // successfully updated the pause_pages_used count.
      long budget_returned = pause_used - new_pause_used;
      pages -= budget_returned;
      break;
    }

    pause_used = _pause_pages_used;
  }

  // Then return normal pages second.
  if ( pages > 0 ) {
    assert(_normal_pages_used > pages, "normal pages used underflow");

    Atomic::add_ptr( (intptr_t)(pages*-1), (intptr_t*)&_normal_pages_used );

assert(_normal_pages_used>=0,"normal pages used underflow");
  }
}


// Try and allocate a page from the OS.  Return true on success, false on failure.
bool GPGC_PageBudget::allocate_page(PageNum page)
{
  char* page_base = (char*) GPGC_Layout::PageNum_to_addr(page);
  bool  alloced; 

  // There are two allocation modes: standard, and leak into grant.
  //
  // In standard mode, we only try and allocated up to the committed limit, and then
  // overflow into the pause fund when necessary.
  //
  // In leak-into-grant mode, we always try to allocate from the committed/grant account,
  // and never fail over to the pause fund.

  if ( _leak_into_grant_enabled ) {
    // Try to allocate via the committed/grant account, and fail if azmem refuses to allocate.
    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::commit_memory);
      // This only allocs from the grant/overflow fund if GPGCOverflowMemory != 0.
      alloced = os::commit_heap_memory(os::JAVA_COMMITTED_MEMORY_ACCOUNT, page_base, BytesPerGPGCPage, (GPGCOverflowMemory!=0));
    }
    if ( alloced ) {
      GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget +1 for leak_into_grant allocate page 0x%lX", page);
      increment_normal_pages(1);
    }
    return alloced;
  }

  // We're not in leak-into-grant mode, so allocate in standard mode: first from committed,
  // then possibly from pause.

  if ( _normal_pages_used < _committed_page_budget )
  {
    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::commit_memory);
      alloced = os::commit_heap_memory(os::JAVA_COMMITTED_MEMORY_ACCOUNT, page_base, BytesPerGPGCPage, false);
    }
    if ( alloced ) {
      GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget +1 for committed_only allocate page 0x%lX", page);
      increment_normal_pages(1);
      return true;
    }
  }

  // If we couldn't allocate, then we might try the pause-prevention account.
  if ( GPGCPausePreventionMemory!=0 && _pause_account_enabled && (!_pause_allocation_failed) ) {
    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::commit_memory);
      alloced = os::commit_heap_memory(os::JAVA_PAUSE_MEMORY_ACCOUNT, page_base, BytesPerGPGCPage, true);
    }
    if ( alloced ) {
      // successfully allocated, record the page used and return.
      increment_pause_pages(1);
      return true;
    } else {
      _pause_allocation_failed = true;
    }
  }

  return false;
}


bool GPGC_PageBudget::allocate_pages(PageNum base_page, long pages)
{
  for ( long index=0; index<pages; index++ ) {
    if ( ! allocate_page(base_page+index) ) {
      // Can't allocate one of the pages we're hoping for.
      if ( index > 0 ) { 
        // Refund any page budget used:
        GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -%ld for deallocate failed partial block at 0x%lX", index, base_page);
free_page_budget(index);

        // Undo any partial allocation done:
        {
          CycleCounter cc(ProfileMemoryTime, &CycleCounts::uncommit_memory);
          os::uncommit_heap_memory((char*)GPGC_Layout::PageNum_to_addr(base_page), index<<LogBytesPerGPGCPage, false);
        }
      }

      // And return failure:  
      return false;
    }
  }

  return true;
}


// Deallocate both physical and virtual page, and update the page budget.
//
// Returns true if the page is actually deallocated to the kernel.
// Returns false if the pages is used to refill a preallocated page slot.
bool GPGC_PageBudget::deallocate_mapped_page(PageNum page)
{
  // Try and return the physical page into the pre-allocated page set:
  if ( deallocate_preallocated_page(page) ) {
    GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -0 for return to preallocated page 0x%lX", page);
    return false; // Not deallocated to kernel
  }

  // Return the page budget:
  GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -1 for deallocate mapped page 0x%lX", page);
  free_page_budget(1);

  // Deallocate the page to azmem
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::uncommit_memory);
    os::uncommit_heap_memory((char*)GPGC_Layout::PageNum_to_addr(page), BytesPerGPGCPage, false/*no-tlb-sync*/);
  }

  return true; // Deallocated to kernel
}


// Deallocate both physical and virtual pages in a block, and update the page budget.
//
// Returns the number of pages actually deallocated to the kernel.  Pages used to refill
// a preallocated page slot don't count.
long GPGC_PageBudget::deallocate_mapped_block(PageNum block, long pages)
{
  assert0( pages > 0 );

  // Try and return physical pages into the pre-allocated page set:
  if ( pages>0 && deallocate_preallocated_page(block) ) {
    GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -0 for return to preallocated first block page 0x%lX", block);
    block ++;
    pages --;
  }

  if ( pages == 0 ) {
    return 0; // No pages deallocated to kernel
  }

  // Return the page budget:
  GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -%ld for deallocate mapped block 0x%lX", pages, block);
  free_page_budget(pages);

  // Deallocate the remaining pages in the block to azmem:
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::uncommit_memory);
    os::uncommit_heap_memory((char*)GPGC_Layout::PageNum_to_addr(block), pages<<LogBytesPerGPGCPage, false/*no-tlb-sync*/);
  }

  return pages;  // Some pages deallocated to the kernel.
}


// Note: callers of this method are expected to have a log message similar to
void GPGC_PageBudget::account_for_deallocate(long pages, const char* label, PageNum page)
{
  GCLogMessage::log_b(GPGCTraceBudget, gclog_or_tty, "GPGC Budget -%ld for %s of page 0x%lX", pages, label, page);

  if ( pages > 0 ) {
    free_page_budget(pages);
  }
}


// Try to get a preallocate page, rather than allocating a new one from the kernel.
void GPGC_PageBudget::get_preallocated_page(PageNum page, bool small_space)
{
  Thread* thread            = Thread::current();
  PageNum preallocated_page = NoPage;

if(thread->is_GC_task_thread()){
    preallocated_page = ((GCTaskThread*)thread)->get_preallocated_page();
    ((GCTaskThread*)thread)->set_preallocated_page(NoPage);
  }
  else if ( thread->is_GenPauselessGC_thread() ) {
    preallocated_page = ((GPGC_Thread*)thread)->get_preallocated_page();
    ((GPGC_Thread*)thread)->set_preallocated_page(NoPage);
  }
  else {
fatal("Only the GPGC_Thread and GCTaskThreads should get preallocated relocation pages");
  }

  guarantee(preallocated_page!=NoPage, "No preallocated page for relocating thread");

  // Relocate the preallocated physical page to the desired virtual address.
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::relocate_memory);
    os::relocate_memory((char*)GPGC_Layout::PageNum_to_addr(preallocated_page),
                        (char*)GPGC_Layout::PageNum_to_addr(page),
                        BytesPerGPGCPage, false/*no tlb-sync*/);
  }

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Got preallocated page 0x%lX for page 0x%lX for thread 0x%p", preallocated_page, page, thread);

  // No change to total memory used, as preallocated pages are counted when initially allocated.
  Atomic::dec_ptr((intptr_t*)&_free_preallocated);
}


//  Make sure that we didn't leak preallocated pages.
void GPGC_PageBudget::new_gc_verify_preallocated_pages()
{
  // TODO: maw: delete this, does nothing.  Or else iterate through the GCTaskThreads.
}


//  Make sure that we didn't leak preallocated pages.
void GPGC_PageBudget::old_gc_verify_preallocated_pages()
{
  // TODO: maw: delete this, does nothing.  Or else iterate through the GCTaskThreads.
}


PageNum GPGC_PageBudget::preallocated_page_for_thread()
{
  Thread* thread            = Thread::current();
  PageNum preallocated_page = NoPage;

if(thread->is_GC_task_thread()){
    preallocated_page = ((GCTaskThread*)thread)->get_preallocated_page();
  }
  else if ( Thread::current()->is_GenPauselessGC_thread() ) {
    preallocated_page = ((GPGC_Thread*)thread)->get_preallocated_page();
  }
  else {
fatal("Only the GPGC_Thread and GCTaskThreads have preallocated pages");
  }

  assert0(preallocated_page != NoPage);

  return preallocated_page;
}


// See if a page that is to be deallocated should be kept as a preallocated page for a GC thread.
bool GPGC_PageBudget::deallocate_preallocated_page(PageNum page)
{
  Thread* thread = Thread::current();

  PageNum preallocated_page=NoPage;

if(thread->is_GC_task_thread()){
    preallocated_page = ((GCTaskThread*)thread)->get_preallocated_page();
    if ( preallocated_page != NoPage ) { return false; }

    long offset       = ((GCTaskThread*)thread)->thread_number();
    preallocated_page = preallocated_page_base() + offset;

    ((GCTaskThread*)thread)->set_preallocated_page(preallocated_page);
  }
  else if ( Thread::current()->is_GenPauselessGC_thread() ) {
    PageNum preallocated_page = ((GPGC_Thread*)thread)->get_preallocated_page();
    if ( preallocated_page != NoPage ) { return false; }

    long offset       = ((GPGC_Thread*)thread)->thread_number();
    preallocated_page = preallocated_page_base() + offset;

    ((GPGC_Thread*)thread)->set_preallocated_page(preallocated_page);
  }
  else {
fatal("Only the GPGC_Thread and GCTaskThreads should be deallocating pages");
  }

  {
    // Relocate the physical page into the preallocated page:
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::relocate_memory);
    os::relocate_memory((char*)GPGC_Layout::PageNum_to_addr(page),
                        (char*)GPGC_Layout::PageNum_to_addr(preallocated_page),
                        BytesPerGPGCPage, false/*no tlb-sync*/);
  }

  Atomic::inc_ptr((intptr_t*)&_free_preallocated);

  if (GPGCTracePageSpace) gclog_or_tty->print_cr("Deallocating page 0x%lX into preallocated page 0x%lX for thread 0x%p", page, preallocated_page, thread);

  return true;
}
