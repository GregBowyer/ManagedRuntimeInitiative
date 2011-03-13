/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef OS_OS_INLINE_HPP
#define OS_OS_INLINE_HPP


#include "gpgc_layout.hpp"
#include "modules.hpp"
#include "os.hpp"
#include "ostream.hpp"

#include <errno.h>
#include <sys/mman.h>

#ifdef AZ_PROXIED // FIXME - Remove this when we move to libaznix for azproxied as well
#include <os/memory.h>
#else // !AZ_PROXIED
#include <aznix/az_memory.h>
#endif // !AZ_PROXIED
#include <os/utilities.h>

#include <os/time.h> 

#ifdef AZ_PROXIED
#include <proxy/proxy.h>
#include <proxy/proxy_io.h>

inline const char* os::file_separator()     { return proxy_file_separator(); }
inline const char* os::line_separator()     { return proxy_line_separator(); }
inline const char* os::path_separator()     { return proxy_path_separator(); }
inline const char* os::dll_file_extension() { return proxy_dll_file_extension(); }
inline const char* os::get_temp_directory() { return proxy_get_temp_directory(); }
inline const char* os::get_current_directory(char* buf, int buflen) {
  const char* cwd = proxy_get_user_directory();

  if (buf == NULL || ((int)strlen(cwd) + 1/*'\0'*/) > buflen) return NULL;

  return strncpy(buf, cwd, buflen);
}
#else // !AZ_PROXIED:
inline const char* os::file_separator()     { return "/"; }
inline const char*os::line_separator(){return"/n";}
inline const char* os::path_separator()     { return ":"; }
inline const char*os::dll_file_extension(){return".so";}
inline const char*os::get_temp_directory(){return"/tmp/";}
inline const char* os::get_current_directory(char *buf, int buflen) { return getcwd(buf, buflen); }
#endif // !AZ_PROXIED

inline const char* os::jlong_format_specifier()   { return "%lld"; }
inline const char* os::julong_format_specifier()  { return "%llu"; }

inline int os::maximum_processor_count() {
int count=processor_count();
assert(count>0,"processor count not yet initialized");

if(os::use_azsched()){
    // TODO: With azsched, this is maybe intended to return the CPU count max for the process
    // instead of the system's total CPU count?
  }

  return count;
}

inline int os::current_cpu_number() {
  int slot = 0;                 // TODO: Retrieve this with an x86 CPUID asm instruction?
  assert0(slot < os::maximum_processor_count());
  return slot;
}

// File names are case-sensitive on windows only
inline int os::file_name_strcmp(const char* s1, const char* s2) {
  // Azul note: Needs proxied implementation (but should aggregate with other calls)
  return strcmp(s1, s2);
}

inline int os::vm_page_size() {
assert(_vm_page_size!=0,"must call os::init");
  return _vm_page_size;
}

inline int os::vm_large_page_size() {
assert(_vm_large_page_size!=-1,"must call os::init");
  return _vm_large_page_size;
}

inline int os::vm_zero_page_size() {
assert(_vm_zero_page_size!=-1,"must call os::init");
  return _vm_zero_page_size;
}

inline bool os::commit_memory(char* addr, size_t bytes, int module, bool zero) {
  return os::commit_memory(os::CHEAP_COMMITTED_MEMORY_ACCOUNT, addr, bytes, module, zero);
}

inline bool os::commit_memory(MemoryAccount account, char* addr, size_t bytes, int module, bool zero) {
  if (bytes == 0) {
    // Don't bother the OS with no-ops.
    return true;
  }

  bool result = false;
  char* const req_addr = addr;  // save for printouts
  assert((size_t) addr % os::vm_page_size() == 0, "commit on page boundaries");
  assert(bytes % os::vm_page_size() == 0, "commit in page-sized chunks");

if(os::use_azmem()){
#if 0
  uint64_t flags = MEMORY_ALLOCATE_REQUIRED_ADDRESS
                 | ((account == JAVA_COMMITTED_MEMORY_ACCOUNT ||
                     account == JAVA_PAUSE_MEMORY_ACCOUNT) ? MEMORY_ALLOCATE_HEAP : 0)
                 | (zero ? 0 : MEMORY_ALLOCATE_REQUEST_NOZERO);
#else

    uint64_t flags = ((account == JAVA_COMMITTED_MEMORY_ACCOUNT ||
                       account == JAVA_PAUSE_MEMORY_ACCOUNT) ? AZMM_ALLOCATE_HEAP : 0)
                   | (zero ? 0 : AZMM_ALLOCATE_REQUEST_NOZERO);
    int prot       = PROT_READ | PROT_WRITE
                   |  ((account == CHEAP_COMMITTED_MEMORY_ACCOUNT) ? PROT_EXEC : 0);
#endif
    int ret = az_mmap(addr, bytes, prot, account, flags);
    result = (ret == 0) ? true : false;
  } else {
    void* where = mmap(addr, bytes, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    result = (where != MAP_FAILED);
  }
  
  if ( account==CHEAP_COMMITTED_MEMORY_ACCOUNT ) {
    if ( result==true ) {
      if ( module<0 || module>=Modules::MAX_MODULE ) {
        module = Modules::Unknown;
      }
      Atomic::add_and_record_peak(long(bytes), (long*)&_az_mmap_balance[module], (long*)&_az_mmap_peak[module]);
    } else {
      // Allocation failed: dump c-heap stats.
az_mmap_print_on(tty);
    }
  }

  if (Verbose && (result == false)) {

tty->print_cr("================= os::commit_memory ====================");
tty->print_cr("error: %s",strerror(errno));
tty->print_cr("req. bytes %zd",bytes);
tty->print_cr("req. addr %p",req_addr);
    int64_t balance       = 0;
    int64_t balance_min   = 0;
    int64_t balance_max   = 0;
    size_t  allocated     = 0;
    size_t  allocated_min = 0;
    size_t  allocated_max = 0;
    size_t  total_maximum = 0;
    memory_account_get_stats(account, &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &total_maximum);
tty->print_cr("account %d",account);
tty->print_cr("balance %ld",balance);
tty->print_cr("balance_min %ld",balance_min);
tty->print_cr("balance_max %ld",balance_max);
tty->print_cr("allocated %zd",allocated);
tty->print_cr("allocated_min %zd",allocated_min);
tty->print_cr("allocated_max %zd",allocated_max);
tty->print_cr("total_maximum %zd",total_maximum);
tty->print_cr("========================================================");
  }

  return result;
}

inline bool os::commit_heap_memory(MemoryAccount account, char* addr, size_t bytes, bool allow_overdraft) {
  if (bytes == 0) {
    // Don't bother the OS with no-ops.
    return true;
  }

  bool result = false;
assert(bytes>0,"commit more than zero bytes");
assert((size_t)addr%os::vm_large_page_size()==0,"commit on page boundaries");
assert(bytes%os::vm_large_page_size()==0,"commit in page-sized chunks");

  assert(account==JAVA_COMMITTED_MEMORY_ACCOUNT || account==JAVA_PAUSE_MEMORY_ACCOUNT, "invalid memory account");

if(os::use_azmem()){
    int flags = AZMM_ALLOCATE_HEAP
              | AZMM_ALLOCATE_REQUEST_NOZERO
              | AZMM_LARGE_PAGE_MAPPINGS
              | (allow_overdraft ? 0 : AZMM_ALLOCATE_NO_OVERDRAFT);

    int ret = az_mmap(addr, bytes, (PROT_READ|PROT_WRITE), account, flags);
    result = (ret == 0) ? true : false;
  } else {
    void* where = mmap(addr, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    result = (where != MAP_FAILED);
  }

  if (Verbose && (result == false)) {
tty->print_cr("=============== os::commit_heap_memory =================");
tty->print_cr("error: %s",strerror(errno));
tty->print_cr("req. bytes %zd",bytes);
tty->print_cr("req. addr %p",addr);
    int64_t balance       = 0;
    int64_t balance_min   = 0;
    int64_t balance_max   = 0;
    size_t  allocated     = 0;
    size_t  allocated_min = 0;
    size_t  allocated_max = 0;
    size_t  total_maximum = 0;
    memory_account_get_stats(account, &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &total_maximum);
tty->print_cr("account %d",account);
tty->print_cr("balance %ld",balance);
tty->print_cr("balance_min %ld",balance_min);
tty->print_cr("balance_max %ld",balance_max);
tty->print_cr("allocated %zd",allocated);
tty->print_cr("allocated_min %zd",allocated_min);
tty->print_cr("allocated_max %zd",allocated_max);
tty->print_cr("total_maximum %zd",total_maximum);
tty->print_cr("========================================================");
  }

  return result;
}


inline void os::uncommit_memory(char* addr, size_t bytes, int module, bool tlb_sync) {
  assert((size_t) addr % os::vm_page_size() == 0, "uncommit on page boundaries");
  assert(bytes % os::vm_page_size() == 0, "uncommit in page-sized chunks");

  int result;

if(os::use_azmem()){
    int flags = 0UL;
    if (!tlb_sync) { flags = AZMM_NO_TLB_INVALIDATE; }

    result = az_munmap(addr, bytes, CHEAP_COMMITTED_MEMORY_ACCOUNT, flags);

    if (-1 == result) {
fatal1("az_munmap() failed with errno %d",errno);
    }
  } else {
    result = munmap(addr, bytes);

    if (-1 == result) {
fatal1("munmap() failed with errno %d",errno);
    }
  }

  if ( -1 != result ) {
    if ( module<0 || module>=Modules::MAX_MODULE ) {
      module = Modules::Unknown;
    }
    Atomic::add_ptr(-1*intptr_t(bytes), &_az_mmap_balance[module]);
  }

  return;
}


inline void os::uncommit_heap_memory(char* addr, size_t bytes, bool tlb_sync, bool blind) {
  assert((size_t) addr % os::vm_page_size() == 0, "uncommit on page boundaries");
  assert(bytes % os::vm_page_size() == 0, "uncommit in page-sized chunks");

if(os::use_azmem()){
    int flags = 0;
    if (!tlb_sync) { flags |= AZMM_NO_TLB_INVALIDATE; }
    if (blind)     { flags |= AZMM_BLIND_UNMAP; }

    int result = az_munmap(addr, bytes, JAVA_COMMITTED_MEMORY_ACCOUNT, flags);

    if (-1 == result) {
fatal1("az_munmap() failed with errno %d",errno);
    }
  } else {
    int result = munmap(addr, bytes);

    if (-1 == result) {
fatal1("munmap() failed with errno %d",errno);
    }
  }

  return;
}


inline void os::relocate_memory(char* from, char* to, size_t bytes, bool tlb_sync) {
if(os::use_azmem()){
    int flags  = tlb_sync ? 0 : AZMM_NO_TLB_INVALIDATE;
    int result = az_mremap(from, to, bytes, flags);
    if (-1 == result) {
fatal1("az_mremap() failed with errno %d",errno);
    }
  } else {
    void* addr = mremap(from, bytes, bytes, MREMAP_MAYMOVE|MREMAP_FIXED, to);
    if ( addr == (void*)-1 ) {
fatal1("mremap() failed with errno %d",errno);
    }
    if ( addr != (void*)to ) {
fatal("mremap() didn't obey to address");
    }
  }

  return;
}


inline void os::batched_relocate_memory(char* from, char* to, size_t bytes, bool shatter) {
  if (os::use_azmem() && BatchedMemoryOps) {
    // Batched memory ops are only supported with azmem.
    int flags  = shatter ? AZMM_MAY_SHATTER_LARGE_MAPPINGS : 0;
    int result = az_mbatch_remap(from, to, bytes, flags);
    if (-1 == result) {
fatal1("az_mbatch_remap() failed with errno %d",errno);
    }
  } else {
    os::relocate_memory(from, to, bytes);
  }

  return;
}


inline void os::unshatter_all_memory(char* addr, char* resource_addr)
{
if(os::use_azmem()){
    int prot  = PROT_READ | PROT_WRITE;
    int acct  = JAVA_COMMITTED_MEMORY_ACCOUNT;
    int flags = AZMM_NO_TLB_INVALIDATE;

    assert(az_large_page(resource_addr, 0), "resource_addr isn't a large page going into unshatter");

    int result = az_munshatter(addr, NULL, resource_addr, prot, acct, flags);

    if ( result < 0 ) {
fatal1("az_munshatter() all failed with errno %d",errno);
    }
    else if ( result > 0 ) {
      // TODO: until we can hack in a fix for bug 25958, we're going to reallocate the resource page,
      // and only fatal error if the realloc fails.
      //
      // fatal1("az_munshatter() consumed resource without returning a page: %d", result);
      if ( ! commit_heap_memory(os::JAVA_COMMITTED_MEMORY_ACCOUNT, resource_addr, BytesPerGPGCPage, false) ) {
        fatal2("az_mushatter() consumed resource without returning a page (%d), and couldn't commit a replacement (errno %d)", result, errno);
      }
    }

    assert(az_large_page(resource_addr, 0), "resource_addr isn't a large page coming out of unshatter");

    return;
  }

  // Unshatter is supported only with azmem.
  ShouldNotReachHere();
  return;
}


inline bool os::partial_unshatter_memory(char* addr, char* force_addr)
{
if(os::use_azmem()){
    int prot  = PROT_READ | PROT_WRITE;
    int acct  = JAVA_COMMITTED_MEMORY_ACCOUNT;
    int flags = AZMM_NO_TLB_INVALIDATE;

    // TODO-NOW: handle mutator unshatter using grant or pause memory.

    int result = az_munshatter(addr, force_addr, NULL, prot, acct, flags);

    if ( result < 0 ) {
      if ( errno == ENOMEM ) {
        // ENOMEM is the only expected failure mode.  It will occur if a mutator tries to
        // unshatter a page while the JAVA_COMMITTED_MEMORY_ACCOUNT is out of 2MB pages.
        // If that occurs, the mutator will need to wait for the collector to unshatter the page.
        return false;
      }

fatal1("az_munshatter() partial failed with errno %d",errno);
    }

    return true;
  }

  // Unshatter is supported only with azmem.
  ShouldNotReachHere();
  return false;
}


inline void os::release_memory(char* addr, size_t bytes) {
  if (addr == 0 || bytes == 0) {
    return;
  }

NEEDS_CLEANUP;//These should be large page chunks
assert((size_t)addr%os::vm_page_size()==0,"release_memory on page boundaries");
assert(bytes%os::vm_page_size()==0,"release_memory in page-sized chunks");
  size_t reserved_size = round_to(bytes, os::vm_page_size());

if(os::use_azmem()){
    size_t vm_structs_len = __VM_STRUCTURES_END_ADDR__ - __VM_STRUCTURES_START_ADDR__;

    if ((uintptr_t) addr >= (uintptr_t) __VM_STRUCTURES_START_ADDR__ &&
        (uintptr_t) addr <= (uintptr_t) __VM_STRUCTURES_END_ADDR__ &&
        bytes != vm_structs_len) {
      // Since we az_mreserve vm_structs_len bytes upfront, we cannot az_munreserve
      // just parts of it.
      // The virtualspace destructors for the oopTable get called during process exit
      // and they try to unreserve a smaller chunk within that region.
      return;
    }

    int result = az_munreserve(addr, reserved_size);

    if (-1 == result) {
fatal1("az_munreserve() failed: %s",strerror(errno));
    }
  } else {
    int result = munmap(addr, reserved_size);

    if (-1 == result) {
fatal1("munmap() failed: %s",strerror(errno));
    }
  }

  return;
}


inline void os::gc_protect_memory(char* addr, size_t bytes, bool tlb_sync) {
if(os::use_azmem()){
    int flags  = tlb_sync ? 0 : AZMM_NO_TLB_INVALIDATE;
    int result = az_mprotect(addr, bytes, PROT_NONE, flags);
    if (-1 == result) {
fatal1("az_mprotect() failed: %s",strerror(errno));
    }
  } else {
    bool result = guard_memory(addr, bytes);
    if (false == result) {
fatal("guard_memory() failed");
    }
  }

  return;
}


inline void os::batched_protect_memory(char* addr, size_t bytes) {
if(os::use_azmem()){
    if (BatchedMemoryOps ) {
      int flags  = 0;
      int result = az_mbatch_protect(addr, bytes, PROT_NONE, 0);
      if (-1 == result) {
fatal1("az_mbatch_protect() failed with errno %d",errno);
      }
    } else {
gc_protect_memory(addr,bytes);
    }

    return;
  }

  // Shouldn't be trying to use batched protects without azmem.
  ShouldNotReachHere();
  return;
}


inline void os::gc_unprotect_memory(char* addr, size_t bytes) {
  Unimplemented();
}

inline bool os::memory_account_set_maximum(MemoryAccount account, size_t size) {
  assert(os::use_azmem(), "memory accounts are supported only with azmem");

  int ret = az_pmem_set_account_maximum(Linux_OS::this_process_specifier(), account, size);

  return (ret == 0) ? true : false; 
}

inline bool os::memory_account_deposit(MemoryAccount account, int64_t size) {
  assert(os::use_azmem(), "memory accounts are supported only with azmem");

  assert(account==CHEAP_COMMITTED_MEMORY_ACCOUNT || account==JAVA_COMMITTED_MEMORY_ACCOUNT, "invalid memory account");

  int ret = az_pmem_fund_account(Linux_OS::this_process_specifier(), account, size);

  return (ret == 0) ? true : false; 
}

inline bool os::memory_account_transfer(MemoryAccount dst_account, MemoryAccount src_account, int64_t size) {
  assert(os::use_azmem(), "memory accounts are supported only with azmem");

  int ret = az_pmem_account_transfer(Linux_OS::this_process_specifier(), dst_account, src_account, size);

  return (ret == 0) ? true : false; 
}

inline bool os::guard_stack_memory(char* addr, size_t bytes) {
  return munmap(addr,bytes) == 0;
}

inline bool os::unguard_stack_memory(char* addr, size_t bytes) {
  return mmap(addr,bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0) == addr;
}

inline bool os::uses_stack_guard_pages() {
  return false;
}

inline bool os::allocate_stack_guard_pages() {
  assert(uses_stack_guard_pages(), "sanity check");
  return false;
}

// On Solaris, reservations are made on a page by page basis, nothing to do.
inline void os::split_reserved_memory(char *base, size_t size,
                                      size_t split, bool realloc) {
  // Not needed on Linux
}


// Bang the shadow pages if they need to be touched to be mapped.
inline void os::bang_stack_shadow_pages() {
  // Not needed for our libos threads setup
}

// Support for remote JNI method invocation
inline bool os::is_remote_handle(address handle) {
  return ((jlong)handle & 0x1);
}

inline address os::mark_remote_handle(address handle) {
uintptr_t h=(uintptr_t)handle;
  // Ensure we can use the LSB-marker trick ... bug 10743
  guarantee((h & ~0x1UL) == h, "handle SB is non-zero");
  return (address)(h | 0x1);
}

inline address os::get_real_remote_handle(address handle) {
  return ((address)((jlong)handle & ~0x1));
}


inline jlong os::elapsed_counter() {
  return (jlong) ::system_tick_count();
}

extern jlong performance_frequency;
inline jlong os::elapsed_frequency() { return performance_frequency; }

inline uint64_t os::ticks_to_millis( uint64_t ticks ) { return ::ticks_to_millis(ticks ); }
inline uint64_t os::ticks_to_micros( uint64_t ticks ) { return ::ticks_to_micros(ticks ); }
inline uint64_t os::ticks_to_nanos ( uint64_t ticks ) { return ::ticks_to_nanos (ticks ); }
inline uint64_t os::millis_to_ticks( uint64_t millis) { return ::millis_to_ticks(millis); }
inline uint64_t os::micros_to_ticks( uint64_t micros) { return ::micros_to_ticks(micros); }
inline uint64_t os::nanos_to_ticks ( uint64_t nanos ) { return ::nanos_to_ticks (nanos ); }

inline void os::tlb_resync(){
if(os::use_azmem()){
    if (0 != az_tlb_invalidate()) {
fatal1("az_tlb_invalidate() failed with errno %d",errno);
    }
  }

  return;
}

#endif // OS_OS_INLINE_HPP
