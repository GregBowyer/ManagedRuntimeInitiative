/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef OS_OS_HPP
#define OS_OS_HPP


#include <sys/types.h>

// Linux_OS defines the interface to Linux and Linux derived operating systems

  // Ok to sleep while holding the jvm_lock (doing so prevents GC).
  // Used by various debug printouts and is the core implementation.
  static int sleep_jvmlock_ok(jlong ms, bool interruptible);

  static pid_t _process_id;
  // Returns the current process id
  static pid_t process_id() { return _process_id; }

  static int _vm_page_size;
  static int _vm_large_page_size;
  static int _vm_zero_page_size;

  static void set_abort_on_out_of_memory(bool b);

  // Minimum stack size a thread can be created with (allowing
  // the VM to completely create the thread and enter user code)
  static size_t min_stack_allowed;

  // Maximum stack size a thread can be created with - any larger
  // and stack extension support will not currently work.
  static size_t max_stack_allowed;

  // Returns the byte size of a large virtual memory page
  static address vm_large_page_address_mask() { return (address)~((long)_vm_large_page_size-1); }

  // %%% Following should be promoted to os.hpp:
  // Trace number of created threads
  static intptr_t _os_thread_limit;
  static volatile intptr_t _os_thread_count;

  // For debugging: a unique id for every thread
  static volatile intptr_t _unique_thread_id;

  static inline void tlb_resync();

  static void fund_memory_accounts(size_t java_committed);

  // az_mmap accounting:
  static intptr_t* _az_mmap_balance;
  static intptr_t* _az_mmap_peak;

static void az_mmap_print_on(outputStream*st);

class Linux_OS {
  friend class os;

private:

#ifdef AZ_PROXIED
  static int _proxy_process_id;
#endif // AZ_PROXIED

  static jlong _allocation_id;

  static julong  _physical_memory;
  static jlong   _last_sync_millis;
  static jlong   _last_sync_elapsed_count;
  static int     _last_sync_latch;
static address _jvm_start_address;
static address _jvm_end_address;
  static bool    _flush_overdraft_only_supported;

  static bool    _use_azmem;
  static bool    _use_azsched;

public:

  static void initialize_system_info();

  // perf counter incrementers used by _INTERRUPTIBLE 
  
  static void bump_interrupted_before_count();
  static void bump_interrupted_during_count();

static julong physical_memory() { return _physical_memory; }

  static jlong allocation_id()    { return _allocation_id;   }

#if   (AZNIX_API_VERSION >= 200) /* .ko ioctl interface */
  static pid_t this_process_specifier() { return process_id(); }
#elif (AZNIX_API_VERSION >= 100) /* syscalls interface */
  static jlong this_process_specifier() { return allocation_id(); }
#else
#error AZNIX_API_VERSION must be defined
#endif

  static pid_t gettid();

  // Initialize the System.currentTimeMillis timer.  This allows a
  // very cheap & multi-core coherent implementation of Java's CTM.
  static void init_System_CTM_timer();

void set_abort_on_out_of_memory(bool b);
  void exit_out_of_memory();

  static bool is_sig_ignored(jint sig);
  static void set_use_azmem(bool azmem)     { _use_azmem = azmem; }
  static bool use_azmem()                   { return _use_azmem; }
  static void set_use_azsched(bool azsched) { _use_azsched = azsched; }
  static bool use_azsched()                 { return _use_azsched; }
};



class PlatformEvent : public CHeapObj {
public:       // TODO-FIXME: make dtor private
  ~PlatformEvent() { guarantee (0, "invariant") ; }
  PlatformEvent() {  Unimplemented();  }
};

class PlatformParker : public CHeapObj {
public:       // TODO-FIXME: make dtor private
  ~PlatformParker() { guarantee (0, "invariant") ; }
  PlatformParker() {  Unimplemented();  }
};

#endif // OS_OS_HPP
