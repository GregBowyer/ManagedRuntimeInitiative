/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "arguments.hpp"
#include "attachListener.hpp"
#include "globals.hpp"
#include "handles.hpp"
#include "heapDumper.hpp"
#include "hpi.hpp"
#include "interfaceSupport.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jvm_os.h"
#include "jvmtiExport.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "vmGCOperations.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"
#include "vm_version_pd.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"

#include <signal.h>
// put OS-includes here
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>


OSThread*         os::_starting_thread    = NULL;
address           os::_polling_page       = NULL;
volatile int32_t* os::_mem_serialize_page = NULL;
uintptr_t         os::_serialize_page_mask = 0;
int32_t os::_rand_seed=1;
int               os::_processor_count    = 0;
volatile jlong    os::_global_time        = 0;
volatile int      os::_global_time_lock   = 0;
bool              os::_use_global_time    = false;
#ifndef PRODUCT
int os::num_mallocs = 0;            // # of calls to malloc/realloc
size_t os::alloc_bytes = 0;         // # of bytes allocated
int os::num_frees = 0;              // # of calls to free
#endif

jlong os::_sys_ctm=0;   // The current time in milliseconds

// Atomic read of a jlong is assured by a seqlock; see update_global_time()
jlong os::read_global_time() {
  return _global_time;
}

//
// NOTE - Assumes only one writer thread!
//
// We use a seqlock to guarantee that jlong _global_time is updated
// atomically on 32-bit platforms.  A locked value is indicated by
// the lock variable LSB == 1.  Readers will initially read the lock
// value, spinning until the LSB == 0.  They then speculatively read
// the global time value, then re-read the lock value to ensure that
// it hasn't changed.  If the lock value has changed, the entire read
// sequence is retried.
//
// Writers simply set the LSB = 1 (i.e. increment the variable),
// update the global time, then release the lock and bump the version
// number (i.e. increment the variable again.)  In this case we don't
// even need a CAS since we ensure there's only one writer.
//
void os::update_global_time() {
  Unimplemented();
  _global_time = timeofday();
}

// Fill in buffer with current local time as an ISO-8601 string.
// E.g., yyyy-mm-ddThh:mm:ss-zzzz.
// Returns buffer, or NULL if it failed.
// This would mostly be a call to 
//     strftime(...., "%Y-%m-%d" "T" "%H:%M:%S" "%z", ....)
// except that on Windows the %z behaves badly, so we do it ourselves.
// Also, people wanted milliseconds on there, 
// and strftime doesn't do milliseconds.
char* os::iso8601_time(char* buffer, size_t buffer_length) {  
  Unimplemented();
  return NULL;
}


OSThreadPriority os::os_priority_from_java_priority(JavaThreadPriority java_priority)
{
  guarantee(java_priority>=MinPriority && java_priority<=MaxPriority, "Invalid Java Thread priority");
  return java_to_os_priority[java_priority];
}


OSReturn os::get_priority(const Thread*const thread,JavaThreadPriority&priority){
  int p;
  OSThreadPriority os_prio;
  OSReturn ret = get_native_priority(thread, &os_prio);
  if (ret != OS_OK) return ret;
  for (p = MaxPriority; p > MinPriority && java_to_os_priority[p] > os_prio; p--) ;
priority=(JavaThreadPriority)p;
  return OS_OK;
}


// --------------------- sun.misc.Signal (optional) ---------------------

// sigexitnum_pd,sigbreaknum_pd are platform-specific signals used for terminating/thread dump the Signal thread.

static void signal_thread_entry(JavaThread* thread, TRAPS) {
  while (true) {

    int sig = os::signal_wait();
    // Azul - FIXME - if the user Ctrl-C's the process during error reporting
    // we should die immediately.
    // if (sig == SIGINT && is_error_reported()) { os::die(); }
    if (sig == os::sigexitnum_pd()) {
       // Terminate the signal thread
       return;
    }

if(sig==os::sigbreaknum_pd()){
        // Check if the signal is a trigger to start the Attach Listener - in that
        // case don't print stack traces.
        if (!DisableAttachMechanism && AttachListener::is_init_trigger()) {
          continue;
        }

        // Print stack traces
        // Any SIGBREAK operations added here should make sure to flush
        // the output stream (e.g. tty->flush()) after output.  See 4803766.
        // Each module also prints an extra carriage return after its output.
        VM_PrintThreads op;
        VMThread::execute(&op);
        VM_PrintJNI jni_op;
        VMThread::execute(&jni_op);
        VM_FindDeadlocks op1(tty);
        VMThread::execute(&op1);
        Universe::print_heap_at_SIGBREAK();
        if (PrintClassHistogram) {
          VM_GC_HeapInspection op1(gclog_or_tty, true /* force full GC before heap inspection */);
          VMThread::execute(&op1);
        }

tty->flush();
       
       if (JvmtiExport::should_post_data_dump()) {
          JvmtiExport::post_data_dump();
       }
    } else {
       if (UseITR) {
Threads::ITR_write_thread_names();
       }
       // Dispatch the signal to java
       HandleMark hm(THREAD);
       klassOop k = SystemDictionary::resolve_or_null(vmSymbolHandles::sun_misc_Signal(), THREAD);
       KlassHandle klass (THREAD, k);
       if (klass.not_null()) {
         JavaValue result(T_VOID);
         JavaCallArguments args;
         args.push_int(sig);
         JavaCalls::call_static(
            &result,
            klass, 
            vmSymbolHandles::dispatch_name(), 
            vmSymbolHandles::int_void_signature(),
            &args,
            THREAD
         );
       }
       CLEAR_PENDING_EXCEPTION;
    }
  }
}


void os::signal_init() {
  if (!ReduceSignalUsage) {
    // Setup JavaThread for processing signals
    EXCEPTION_MARK;
    klassOop k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_Thread(), true, CHECK);
    instanceKlassHandle klass (THREAD, k);
instanceHandle thread_oop=klass->allocate_instance_handle(false/*No SBA*/,CHECK);

    const char thread_name[] = "Signal Dispatcher";
Handle string=java_lang_String::create_from_str(thread_name,false/*No SBA*/,CHECK);

    // Initialize thread_oop to put it into the system threadGroup
    Handle thread_group (THREAD, Universe::system_thread_group());
    JavaValue result(T_VOID);
    JavaCalls::call_special(&result, thread_oop, 
                           klass, 
                           vmSymbolHandles::object_initializer_name(), 
                           vmSymbolHandles::threadgroup_string_void_signature(), 
                           thread_group, 
                           string, 
                           CHECK);  
    
    KlassHandle group(THREAD, SystemDictionary::threadGroup_klass());
    JavaCalls::call_special(&result,
                            thread_group,
                            group,
                            vmSymbolHandles::add_method_name(),
                            vmSymbolHandles::thread_void_signature(),
			    thread_oop,		// ARG 1
                            CHECK);

    os::signal_init_pd();

    { MutexLockerAllowGC mu(Threads_lock,JavaThread::current());
      JavaThread* signal_thread = new (ttype::java_thread) JavaThread(&signal_thread_entry);
                                                                                                                              
      // At this point it may be possible that no osthread was created for the
      // JavaThread due to lack of memory. We would have to throw an exception
      // in that case. However, since this must work and we do not allow
      // exceptions anyway, check and abort if this fails.
      if (signal_thread == NULL || signal_thread->osthread() == NULL) {
        vm_exit_during_initialization("java.lang.OutOfMemoryError",
                                      "unable to create new native thread");
      }

      java_lang_Thread::set_thread(thread_oop(), signal_thread);
      java_lang_Thread::set_daemon(thread_oop());
         
      signal_thread->set_threadObj(thread_oop());
Threads::add(signal_thread,false);

      signal_thread->set_os_priority(SignalThreadPriority);

      Thread::start(signal_thread);
    }
    // Handle ^BREAK
    os::signal(SIGBREAK, os::user_handler());
  }
}


void os::terminate_signal_thread() {
  if (!ReduceSignalUsage)
    signal_notify(sigexitnum_pd());
}


// --------------------- loading libraries ---------------------

typedef jint (JNICALL *JNI_OnLoad_t)(JavaVM *, void *);
extern struct JavaVM_ main_vm;

static void* _native_java_library = NULL;

void* os::native_java_library() {
  if (_native_java_library == NULL) {
    // Azul note: General scheme: "load" local library, and call the JNI_OnLoad function if
    // it exists. That call may itself trigger the loading of the "remote" JNI library on
    // the proxy via a proxy/RPC call.
    char buffer[JVM_MAXPATHLEN];
    char ebuf[1024];

    // Try to load verify dll first. In 1.3 java dll depends on it and is not always
    // able to find it when the loading executable is outside the JDK. 
    // In order to keep working with 1.2 we ignore any loading errors.
hpi::dll_build_name(buffer,sizeof(buffer),Arguments::get_dll_dir(),"verify",UseDebugLibrarySuffix);
    hpi::dll_load(buffer, ebuf, sizeof(ebuf));

    // Load java dll
hpi::dll_build_name(buffer,sizeof(buffer),Arguments::get_dll_dir(),"java",UseDebugLibrarySuffix);
    _native_java_library = hpi::dll_load(buffer, ebuf, sizeof(ebuf));
    if (_native_java_library == NULL) {
      vm_exit_during_initialization("Unable to load native library", ebuf);
    }
    // The JNI_OnLoad handling is normally done by method load in java.lang.ClassLoader$NativeLibrary,
    // but the VM loads the base library explicitly so we have to check for JNI_OnLoad as well
    const char *onLoadSymbols[] = JNI_ONLOAD_SYMBOLS;
    JNI_OnLoad_t JNI_OnLoad = CAST_TO_FN_PTR(JNI_OnLoad_t, hpi::dll_lookup(_native_java_library, onLoadSymbols[0]));
    if (JNI_OnLoad != NULL) {
      JavaThread* thread = JavaThread::current();
      ThreadToNativeFromVM ttn(thread);
      HandleMark hm(thread);
      jint ver = (*JNI_OnLoad)(&main_vm, NULL);
      if (!Threads::is_supported_jni_version_including_1_1(ver)) {
        vm_exit_during_initialization("Unsupported JNI version");
      }
    }
  }
  return _native_java_library;
}

// --------------------- heap allocation utilities ---------------------

char *os::strdup(const char *str) {
  size_t size = strlen(str);
  char *dup_str = (char *)malloc(size + 1);
  if (dup_str == NULL) return NULL;
  strcpy(dup_str, str);
  return dup_str;
}



#ifdef ASSERT
#define space_before             (MallocCushion + sizeof(double))
#define space_after              MallocCushion
#define size_addr_from_base(p)   (size_t*)(p + space_before - sizeof(size_t))
#define size_addr_from_obj(p)    ((size_t*)p - 1)
// MallocCushion: size of extra cushion allocated around objects with +UseMallocOnly
// NB: cannot be debug variable, because these aren't set from the command line until
// *after* the first few allocs already happened
#define MallocCushion            16 
#else
#define space_before             0
#define space_after              0
#define size_addr_from_base(p)   should not use w/o ASSERT
#define size_addr_from_obj(p)    should not use w/o ASSERT
#define MallocCushion            0 
#endif
#define paranoid                 0  /* only set to 1 if you suspect checking code has bug */

#ifdef ASSERT
inline size_t get_size(void* obj) {
  size_t size = *size_addr_from_obj(obj);
  if (size < 0 )
    fatal2("free: size field of object #%p was overwritten (%lu)", obj, size);
  return size;
}

u_char* find_cushion_backwards(u_char* start) {
  u_char* p = start; 
  while (p[ 0] != badResourceValue || p[-1] != badResourceValue ||
         p[-2] != badResourceValue || p[-3] != badResourceValue) p--;
  // ok, we have four consecutive marker bytes; find start
  u_char* q = p - 4;
  while (*q == badResourceValue) q--;
  return q + 1;
}

u_char* find_cushion_forwards(u_char* start) {
  u_char* p = start; 
  while (p[0] != badResourceValue || p[1] != badResourceValue ||
         p[2] != badResourceValue || p[3] != badResourceValue) p++;
  // ok, we have four consecutive marker bytes; find end of cushion
  u_char* q = p + 4;
  while (*q == badResourceValue) q++;
  return q - MallocCushion;
}

void print_neighbor_blocks(void* ptr) {
  // find block allocated before ptr (not entirely crash-proof)
  if (MallocCushion < 4) {
    tty->print_cr("### cannot find previous block (MallocCushion < 4)");
    return;
  }
  u_char* start_of_this_block = (u_char*)ptr - space_before;
  u_char* end_of_prev_block_data = start_of_this_block - space_after -1;
  // look for cushion in front of prev. block
  u_char* start_of_prev_block = find_cushion_backwards(end_of_prev_block_data);
  ptrdiff_t size = *size_addr_from_base(start_of_prev_block);
  u_char* obj = start_of_prev_block + space_before;
  if (size <= 0 ) {
    // start is bad; mayhave been confused by OS data inbetween objects
    // search one more backwards
    start_of_prev_block = find_cushion_backwards(start_of_prev_block);
    size = *size_addr_from_base(start_of_prev_block);
    obj = start_of_prev_block + space_before;  
  }

  if (start_of_prev_block + space_before + size + space_after == start_of_this_block) {
    tty->print_cr("### previous object: %p (%ld bytes)", obj, size);
  } else {
    tty->print_cr("### previous object (not sure if correct): %p (%ld bytes)", obj, size);
  }

  // now find successor block
  u_char* start_of_next_block = (u_char*)ptr + *size_addr_from_obj(ptr) + space_after;
  start_of_next_block = find_cushion_forwards(start_of_next_block);
  u_char* next_obj = start_of_next_block + space_before;
  ptrdiff_t next_size = *size_addr_from_base(start_of_next_block);
  if (start_of_next_block[0] == badResourceValue && 
      start_of_next_block[1] == badResourceValue && 
      start_of_next_block[2] == badResourceValue && 
      start_of_next_block[3] == badResourceValue) {
    tty->print_cr("### next object: %p (%ld bytes)", next_obj, next_size);
  } else {
    tty->print_cr("### next object (not sure if correct): %p (%ld bytes)", next_obj, next_size);
  }
}


void report_heap_error(void* memblock, void* bad, const char* where) {
  tty->print_cr("## nof_mallocs = %d, nof_frees = %d", os::num_mallocs, os::num_frees);
  tty->print_cr("## memory stomp: byte at %p %s object %p", bad, where, memblock);
  print_neighbor_blocks(memblock);
  fatal("memory stomping error");
}

void verify_block(void* memblock) {  
  size_t size = get_size(memblock);
  if (MallocCushion) {
    u_char* ptr = (u_char*)memblock - space_before;
    for (int i = 0; i < MallocCushion; i++) {
      if (ptr[i] != badResourceValue) {
        report_heap_error(memblock, ptr+i, "in front of");
      }
    }
    u_char* end = (u_char*)memblock + size + space_after;
    for (int j = -MallocCushion; j < 0; j++) {
      if (end[j] != badResourceValue) {
        report_heap_error(memblock, end+j, "after");
      }
    }
  }
}
#endif

void* os::malloc(size_t size) {
  NOT_PRODUCT(num_mallocs++);
  NOT_PRODUCT(alloc_bytes += size);

  if (size == 0) {
    // return a valid pointer if size is zero
    // if NULL is returned the calling functions assume out of memory.
    size = 1;
  }

  NOT_PRODUCT(if (MallocVerifyInterval > 0) check_heap());
  u_char* ptr = (u_char*)::malloc(size + space_before + space_after);
#ifdef ASSERT
  if (ptr == NULL) return NULL;
  if (MallocCushion) {
    for (u_char* p = ptr; p < ptr + MallocCushion; p++) *p = (u_char)badResourceValue;
    u_char* end = ptr + space_before + size;
    for (u_char* pq = ptr+MallocCushion; pq < end; pq++) *pq = (u_char)uninitBlockPad;
    for (u_char* q = end; q < end + MallocCushion; q++) *q = (u_char)badResourceValue;
  }
  // put size just before data
  *size_addr_from_base(ptr) = size;
#endif
  u_char* memblock = ptr + space_before;
  if ((intptr_t)memblock == (intptr_t)MallocCatchPtr) {
    tty->print_cr("os::malloc caught, %lu bytes --> %p", size, memblock);
    breakpoint();
  }
  debug_only(if (paranoid) verify_block(memblock));
  if (PrintMalloc && tty != NULL) tty->print_cr("os::malloc %lu bytes --> %p", size, memblock);
  return memblock;
}


void* os::realloc(void *memblock, size_t size) {
  NOT_PRODUCT(num_mallocs++);
  NOT_PRODUCT(alloc_bytes += size);
#ifndef ASSERT
  return ::realloc(memblock, size);
#else
  if (memblock == NULL) {
    return os::malloc(size);
  }
  if ((intptr_t)memblock == (intptr_t)MallocCatchPtr) {
    tty->print_cr("os::realloc caught %p", memblock);
    breakpoint();
  }
  verify_block(memblock);
  NOT_PRODUCT(if (MallocVerifyInterval > 0) check_heap());
  if (size == 0) return NULL;
  // always move the block
  void* ptr = malloc(size);
  if (PrintMalloc) tty->print_cr("os::remalloc %lu bytes, %p --> %p", size, memblock, ptr);
  // Copy to new memory if malloc didn't fail
  if ( ptr != NULL ) {
    memcpy(ptr, memblock, MIN2(size, get_size(memblock)));
    if (paranoid) verify_block(ptr);
    if ((intptr_t)ptr == (intptr_t)MallocCatchPtr) {
      tty->print_cr("os::realloc caught, %lu bytes --> %p", size, ptr);
      breakpoint();
    }
    free(memblock);
  }
  return ptr;
#endif
}


void  os::free(void *memblock) {
  NOT_PRODUCT(num_frees++);
#ifdef ASSERT
  if (memblock == NULL) return;
  if ((intptr_t)memblock == (intptr_t)MallocCatchPtr) {
    if (tty != NULL) tty->print_cr("os::free caught %p", memblock);
    breakpoint();
  }
  verify_block(memblock);
  // Crush memory beneath us
  size_t size = get_size(memblock);
  memset(memblock, badHeapValue, size);
  if (PrintMalloc && tty != NULL)
    // tty->print_cr("os::free %p", memblock);
    fprintf(stderr, "os::free %p\n", memblock);
  NOT_PRODUCT(if (MallocVerifyInterval > 0) check_heap());
  // Added by detlefs.
  if (MallocCushion) {
    u_char* ptr = (u_char*)memblock - space_before;
    for (u_char* p = ptr; p < ptr + MallocCushion; p++) {
      guarantee(*p == badResourceValue,
		"Thing freed should be malloc result.");
      *p = (u_char)freeBlockPad;
    }
    size_t size = get_size(memblock);
    u_char* end = ptr + space_before + size;
    for (u_char* q = end; q < end + MallocCushion; q++) {
      guarantee(*q == badResourceValue,
		"Thing freed should be malloc result.");
      *q = (u_char)freeBlockPad;
    }
  }
#endif
  ::free((char*)memblock - space_before);
}

void os::init_random(int32_t initval) {
  _rand_seed = initval;
}


int32_t os::random_impl(int32_t *seed_addr) {
  /* standard, well-known linear congruential random generator with
   * next_rand = (16807*seed) mod (2**31-1)
   * see
   * (1) "Random Number Generators: Good Ones Are Hard to Find",
   *      S.K. Park and K.W. Miller, Communications of the ACM 31:10 (Oct 1988),
   * (2) "Two Fast Implementations of the 'Minimal Standard' Random 
   *     Number Generator", David G. Carta, Comm. ACM 33, 1 (Jan 1990), pp. 87-88.
   * as well as http://www.firstpr.com.au/dsp/rand31/ with simplified use of Carta's algorithm
   * plus simplifications for Azul because we know 64-bit operations are cheap.
  */
  int64_t seed = *seed_addr;
  int64_t prod = ((int64_t)16807) * seed;
  int64_t two31minus1 = 0x7FFFFFFFLL;
  int64_t q = prod >> 31;
  int64_t p = prod & two31minus1;
  int64_t next_rand = p + q;
  next_rand = (next_rand & two31minus1) + (next_rand >> 31);
  int32_t result = (int32_t)next_rand;
  return (*seed_addr = result);
}


int32_t os::random() {
  return random_impl(&_rand_seed);
}


void os::start_thread(Thread* thread) {
  pd_start_thread(thread);
}


//---------------------------------------------------------------------------
// Helper functions for fatal error handler

void os::print_hex_dump(outputStream* st, address start, address end, int unitsize) {
  assert(unitsize == 1 || unitsize == 2 || unitsize == 4 || unitsize == 8, "just checking");

  int cols = 0;
  int cols_per_line = 0;
  switch (unitsize) {
    case 1: cols_per_line = 16; break;
    case 2: cols_per_line = 8;  break;
    case 4: cols_per_line = 4;  break;
    case 8: cols_per_line = 2;  break;
    default: return;
  }

  address p = start;
  st->print(PTR_FORMAT ":   ", start);
  while (p < end) {
    switch (unitsize) {
      case 1: st->print("%02x", *(u1*)p); break;
      case 2: st->print("%04x", *(u2*)p); break;
      case 4: st->print("%08x", *(u4*)p); break;
case 8:st->print("%016llx",*(u8*)p);break;
    }
    p += unitsize;
    cols++;
    if (cols >= cols_per_line && p < end) {
       cols = 0;
       st->cr();
       st->print(PTR_FORMAT ":   ", p);
    } else {
       st->print(" ");
    }
  }
  st->cr();
}

void os::print_environment_variables(outputStream* st, const char** env_list,
                                     char* buffer, int len) {
  if (env_list) {
    st->print_cr("Environment Variables:");

    for (int i = 0; env_list[i] != NULL; i++) {
      if (getenv(env_list[i], buffer, len)) {
        st->print(env_list[i]);
        st->print("=");
        st->print_cr(buffer);
      }
    }
  }
}

void os::print_cpu_info(outputStream* st) {
  // cpu
  st->print("CPU:");
  st->print("total %d", os::processor_count());
  // It's not safe to query number of active processors after crash
  // st->print("(active %d)", os::active_processor_count());
  st->print(" %s", VM_Version::cpu_features());
  st->cr();
}

void os::print_date_and_time(outputStream *st) {
  char stime[64];
struct tm date;
  time_t secs = os::javaTimeMillis() / 1000;
  int result = 0;
  
  if (localtime_r(&secs, &date) != NULL) {
    result = strftime(stime, sizeof(stime), "%a %b %d %X %Z %Y", &date);
    
    if (result != 0) {
st->print_cr("time: %s",stime);
    }
  }
  
st->print_cr("elapsed time: %.2lf seconds",os::elapsedTime());
}

const char* os::arch_version() {
return VM_Version::architecture_version();
}



// AZUL - is_first_frame will detect whether this is the first entry frame.
// But if we have not made a call to Java yet, then we need to recognize the
// bottom-most frame in the stack whose caller pc should be 0.
bool os::is_first_C_frame(frame* fr) {
  return fr->is_first_frame()/* || (fr->pd_sender().pc() == 0)  cannot call pd_sender on C frames*/;
}

#ifdef ASSERT
extern "C" void test_random() {
  const double m = 2147483647;
  double mean = 0.0, variance = 0.0, t;
  long reps = 10000;
  unsigned long seed = 1;

  tty->print_cr("seed %ld for %ld repeats...", seed, reps);
  os::init_random(seed);
  long num; 
  for (int k = 0; k < reps; k++) {
    num = os::random();
    double u = (double)num / m;
    assert(u >= 0.0 && u <= 1.0, "bad random number!");

    // calculate mean and variance of the random sequence 
    mean += u;
    variance += (u*u);
  }
  mean /= reps;
  variance /= (reps - 1);

  assert(num == 1043618065, "bad seed");
  tty->print_cr("mean of the 1st 10000 numbers: %f", mean);
  tty->print_cr("variance of the 1st 10000 numbers: %f", variance);
  const double eps = 0.0001;
  t = fabsd(mean - 0.5018);
  assert(t < eps, "bad mean");
  t = (variance - 0.3355) < 0.0 ? -(variance - 0.3355) : variance - 0.3355;
  assert(t < eps, "bad variance");
}
#endif


// Set up the boot classpath.

char* os::format_boot_path(const char* format_string,
                           const char* home,
                           int home_len,
                           char fileSep,
                           char pathSep) {
    assert((fileSep == '/' && pathSep == ':') ||
	   (fileSep == '\\' && pathSep == ';'), "unexpected seperator chars");
  
    // Scan the format string to determine the length of the actual
    // boot classpath, and handle platform dependencies as well.
    int formatted_path_len = 0;
    const char* p;
    for (p = format_string; *p != 0; ++p) {
	if (*p == '%') formatted_path_len += home_len - 1;
	++formatted_path_len;
    }

    char* formatted_path = NEW_C_HEAP_ARRAY(char, formatted_path_len + 1);
    if (formatted_path == NULL) {
	return NULL;
    }

    // Create boot classpath from format, substituting separator chars and
    // java home directory.
    char* q = formatted_path;
    for (p = format_string; *p != 0; ++p) {
	switch (*p) {
	case '%':
	    strcpy(q, home);
	    q += home_len;
	    break;
	case '/':
	    *q++ = fileSep;
	    break;
	case ':':
	    *q++ = pathSep;
	    break;
	default:
	    *q++ = *p;
	}
    }
    *q = '\0';

    assert((q - formatted_path) == formatted_path_len, "formatted_path size botched");
    return formatted_path;
}


bool os::set_boot_path(char fileSep, char pathSep) {

    const char* home = Arguments::get_java_home();
    int home_len = (int)strlen(home);

    static const char* meta_index_dir_format = "%/lib/";
    static const char* meta_index_format = "%/lib/meta-index";
    char* meta_index = format_boot_path(meta_index_format, home, home_len, fileSep, pathSep);
    if (meta_index == NULL) return false;
    char* meta_index_dir = format_boot_path(meta_index_dir_format, home, home_len, fileSep, pathSep);
    if (meta_index_dir == NULL) return false;
    Arguments::set_meta_index_path(meta_index, meta_index_dir);

    char classpath_format_buf[512];
char*s=classpath_format_buf;
    memset(s, 0, sizeof(classpath_format_buf));

    if( !OrigJavaUtilZip          ) s += strlen(strcpy(s,"%/lib/java_util_zip.jar:"           ));
    if(  UseHighScaleLib          ) s += strlen(strcpy(s,"%/lib/java_util_concurrent_chm.jar:"));
    if(  UseHighScaleLibHashtable ) s += strlen(strcpy(s,"%/lib/java_util_hashtable.jar:"     ));

    // Any modification to the JAR-file list, for the boot classpath must be
    // aligned with install/install/make/common/Pack.gmk. Note: boot class
    // path class JARs, are stripped for StackMapTable to reduce download size.
    static const char classpath_format[] =
	"%/lib/resources.jar:"
	"%/lib/rt.jar:"
	"%/lib/sunrsasign.jar:"
	"%/lib/jsse.jar:"
	"%/lib/jce.jar:"
        "%/lib/charsets.jar:"
	"%/classes";
    strcat(s,classpath_format);
char*sysclasspath=format_boot_path(classpath_format_buf,home,home_len,fileSep,pathSep);
    if (sysclasspath == NULL) return false;
    Arguments::set_sysclasspath(sysclasspath);

    return true;
}

