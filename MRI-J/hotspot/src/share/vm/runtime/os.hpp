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
#ifndef OS_HPP
#define OS_HPP


#include "exceptions.hpp"

#include <dirent.h>

class JavaThread;
class OSThread;
class Thread;
class methodHandle;

typedef struct _jvmtiTimerInfo jvmtiTimerInfo;

// os defines the interface to operating system; this includes traditional
// OS services (time, I/O) as well as other functionality with system-
// dependent code.

typedef void (*dll_func)(...);

// Platform-independent error return values from OS functions
enum OSReturn {
  OS_OK         =  0,        // Operation was successful
  OS_ERR        = -1,        // Operation failed 
  OS_INTRPT     = -2,        // Operation was interrupted
  OS_TIMEOUT    = -3,        // Operation timed out
  OS_NOMEM      = -5,        // Operation failed for lack of memory
  OS_NORESOURCE = -6         // Operation failed for lack of nonmemory resource
};

enum JavaThreadPriority{//JLS 20.20.1-3
  NoPriority       = -1,     // Initial non-priority value
  MinPriority      =  1,     // Minimum priority
  NormPriority     =  5,     // Normal (non-daemon) priority
NearMaxPriority=9,//High priority
MaxPriority=10//Highest priority
};

typedef uint64_t OSThreadPriority;

// Typedef for structured exception handling support 
typedef void (*java_call_t)(JavaValue* value, methodHandle* method, JavaCallArguments* args, Thread* thread);

class os: AllStatic {
 private:
  static OSThread*          _starting_thread;
  static address            _polling_page;
  static volatile int32_t * _mem_serialize_page;
  static uintptr_t          _serialize_page_mask;
  static volatile jlong     _global_time;
  static volatile int       _global_time_lock;
  static bool               _use_global_time;
 public:

  static void init(void);			// Called before command line parsing
  static jint init_2(void);                    // Called after command line parsing

  // File names are case-insensitive on windows only
  // Override me as needed
  static int    file_name_strcmp(const char* s1, const char* s2);

  static bool getenv(const char* name, char* buffer, int len);
  static bool have_special_privileges();
 
  static jlong  timeofday();
  static void   enable_global_time()   { _use_global_time = true; }
  static void   disable_global_time()  { _use_global_time = false; }
  static jlong  read_global_time();
  static void   update_global_time();
  static jlong  javaTimeMillis();
  static jlong  javaTimeNanos();
  static void   javaTimeNanos_info(jvmtiTimerInfo *info_ptr);
  static void   run_periodic_checks();
  // Azul: el-cheapo read of the current time in millsec.
  static jlong _sys_ctm;
static address System_CTM_addr(){return(address)&_sys_ctm;}


  // Returns the elapsed time in seconds since the vm started.
  static double elapsedTime();

  // Returns real time in seconds since an arbitrary point
  // in the past.
  static bool getTimesSecs(double* process_real_time,
			   double* process_user_time, 
			   double* process_system_time);
    
  // Interface to the performance counter
  static jlong elapsed_counter();
  static jlong elapsed_frequency();

  // Return current local time in a string (YYYY-MM-DD HH:MM:SS). 
  // It is MT safe, but not async-safe, as reading time zone 
  // information may require a lock on some platforms.
  static char* local_time_string(char *buf, size_t buflen);
  // Fill in buffer with current local time as an ISO-8601 string.
  // E.g., YYYY-MM-DDThh:mm:ss.mmm+zzzz.
  // Returns buffer, or NULL if it failed.
  static char* iso8601_time(char* buffer, size_t buffer_length);

  // Interface for detecting multiprocessor system
static bool is_MP(){return true;}
  static julong available_memory();
  static julong physical_memory();
  static void check_heap_size();
  static bool is_server_class_machine() { return true; }

  // number of CPUs
  static int processor_count() {
    return _processor_count;
  }

  // Returns the number of CPUs this process is currently allowed to run on.
  // Note that on some OSes this can change dynamically.
  static inline int maximum_processor_count();
  static int active_processor_count();

  // Returns the unique number for the CPU the current thread is running on with
  // contiguous numbering.
  static int current_cpu_number();

  // Bind processes to processors.
  //     This is a two step procedure:
  //     first you generate a distribution of processes to processors,
  //     then you bind processes according to that distribution.
  // Compute a distribution for number of processes to processors.
  //    Stores the processor id's into the distribution array argument. 
  //    Returns true if it worked, false if it didn't.
  static bool distribute_processes(uint length, uint* distribution);
  // Binds the current process to a processor.
  //    Returns true if it worked, false if it didn't.
  static bool bind_to_processor(uint processor_id);
  // set thread quantum
  static bool set_thread_quantum(int64_t millis);
  static int64_t get_thread_quantum();

  // Interface for stack banging (predetect possible stack overflow for
  // exception processing)  There are guard pages, and above that shadow
  // pages for stack overflow checking.
  static bool uses_stack_guard_pages();
  static bool allocate_stack_guard_pages();
  static void bang_stack_shadow_pages();
  static bool stack_shadow_pages_available(Thread *thread, methodHandle method);

  // OS interface to Virtual Memory
  enum MemoryAccount {
    CHEAP_COMMITTED_MEMORY_ACCOUNT = AZMM_DEFAULT_ACCOUNT, // 0
    JAVA_COMMITTED_MEMORY_ACCOUNT  = AZMM_JHEAP_ACCOUNT,   // 2
    JAVA_PAUSE_MEMORY_ACCOUNT      = AZMM_GCPP_ACCOUNT,    // 3
    nof_MEMORY_ACCOUNTS,
    ALL_ACCOUNTS                   = AZMM_NR_MEMORY_ACCOUNTS
  };
  static bool process_low_on_memory(int mem_threshold);

  static void   redirectStaticFunction(address functionAddr, address destAddr);

  static int    vm_page_size();
  static int    vm_large_page_size();
  static int    vm_zero_page_size();
  static int    vm_allocation_granularity();
  static char*  attempt_reserve_memory_at(size_t bytes, char* addr, bool aliasable=false);
  static void   split_reserved_memory(char *base, size_t size,
                                      size_t split, bool realloc);
  static char*  map_memory(int fd, const char* file_name, size_t file_offset,
                           char *addr, size_t bytes, bool read_only = false,
                           bool allow_exec = false);
  static char*  remap_memory(int fd, const char* file_name, size_t file_offset,
                             char *addr, size_t bytes, bool read_only,
                             bool allow_exec);
  static bool   unmap_memory(char *addr, size_t bytes);
  static void   free_memory(char *addr, size_t bytes);
  static void   realign_memory(char *addr, size_t bytes, size_t alignment_hint);

  // NUMA-specific interface
  static void   numa_make_local(char *addr, size_t bytes);
  static void   numa_make_global(char *addr, size_t bytes);
  static size_t numa_get_groups_num();
  static size_t numa_get_leaf_groups(int *ids, size_t size);
  static bool   numa_topology_changed();
  static int    numa_get_group_id();

  // Page manipulation
  struct page_info {
    size_t size;
    int lgrp_id;
  };
  static bool   get_page_info(char *start, page_info* info);
  static char*  scan_pages(char *start, char* end, page_info* page_expected, page_info* page_found);

static char*reserve_memory_special(size_t bytes);
  static char*  release_memory_special (char* preferred_addr, size_t bytes);
  static char*  reserve_memory       (size_t bytes, char* preferred_addr, bool must_allocate_here, bool batchable=false, bool aliasable=false);
  static bool   commit_memory        (char* addr, size_t bytes, int module, bool zero = true);
  static bool   commit_memory        (MemoryAccount account, char* addr, size_t bytes, int module, bool zero = true);
  static bool   commit_heap_memory   (MemoryAccount account, char* addr, size_t bytes, bool allow_overdraft);
  static void   uncommit_memory      (char* addr, size_t bytes, int module, bool tlb_sync = true);
  static void   uncommit_heap_memory (char* addr, size_t bytes, bool tlb_sync = true, bool blind=false);
  static void   relocate_memory      (char* from, char* to, size_t bytes, bool tlb_sync = true);
static void release_memory(char*addr,size_t bytes);
  static bool   protect_memory       (char* addr, size_t bytes);
static bool unprotect_memory(char*addr,size_t bytes);
  static void   gc_protect_memory    (char* addr, size_t bytes, bool tlb_sync = true);
static void gc_unprotect_memory(char*addr,size_t bytes);
static bool guard_memory(char*addr,size_t bytes);
static bool unguard_memory(char*addr,size_t bytes);
static bool guard_stack_memory(char*addr,size_t bytes);
static bool unguard_stack_memory(char*addr,size_t bytes);
  static bool   alias_memory_reserve (char *addr, int start_bit, int num_bits, size_t size);
  static bool   alias_memory_commit  (char *addr, int start_bit, int num_bits, size_t size);
  // Must never look like an address returned by reserve_memory.
  static inline char* non_memory_address_word() { return (char*) 0; }
  static size_t large_page_size();
  static bool   can_commit_large_page_memory();
  static void   flush_memory(MemoryAccount account, size_t* flushed, size_t* allocated);
  static bool   memory_account_set_maximum(MemoryAccount account, size_t size);
  static bool   memory_account_deposit(MemoryAccount account, int64_t size);
  static bool   memory_account_transfer(MemoryAccount dst_account, MemoryAccount src_account, int64_t size);
  static void   memory_account_get_stats(MemoryAccount account,
                                         int64_t* balance,   int64_t* balance_min,   int64_t* balance_max,
                                         size_t*  allocated, size_t*  allocated_min, size_t*  allocated_max,
                                         size_t*  maximum);
  static void   memory_account_reset_watermarks(MemoryAccount account);

  // Azul virtual memory batched memory ops:
  static void   start_memory_batch();
  static void   batched_relocate_memory(char* from, char* to, size_t bytes, bool shatter=false);
static void batched_protect_memory(char*addr,size_t bytes);
  static void   commit_memory_batch();

  // Azul virtual memory page healing ops:
  static void   unshatter_all_memory    (char* addr, char* resource_addr);
  static bool   partial_unshatter_memory(char* addr, char* force_addr);

  static const char*  memory_account_name(MemoryAccount account);

  static void print_memory_statistics_xml_on(xmlBuffer *xb);

  static bool     using_az_kernel_modules();
  static bool     use_azmem();
  static void     disable_azmem();
  static bool     use_azsched();
  static void     disable_azsched();
  static bool     should_start_suspended();
  static void     set_start_suspended(int ss);
  static int64_t  memcommit();
  static void     set_memcommit(int64_t memcommit);
  static int64_t  memmax();
  static void     set_memmax(int64_t memmax);
#ifndef AZ_PROXIED
  static int      launch_avm(void* javamain, void* javamain_args, void* vm_args);
#endif // !AZ_PROXIED

  // threads

  // Since we use the thread stack for the thread object, we need a stack early on
  static address create_thread_stack(ttype::ThreadType thr_type, size_t stack_size);

  static bool create_thread(Thread* thread,
ttype::ThreadType thr_type,
                            size_t stack_size = 0);
  static bool create_main_thread(JavaThread* thread);
  static bool create_attached_thread(JavaThread* thread);
  static void pd_start_thread(Thread* thread);
  static void start_thread(Thread* thread);

  static void initialize_thread();
  static void free_thread(OSThread* osthread);

  // thread id on Linux/64bit is 64bit, on Windows and Solaris, it's 32bit
  static intx current_thread_id();
  static int current_process_id();
  // hpi::read for calls from non native state
  // For performance, hpi::read is only callable from _thread_in_native
  static size_t read(int fd, void *buf, unsigned int nBytes);

  // Normal os::sleep: do not sleep while holding the jvmlock, as it prevents GC.
static int sleep(Thread*thread,jlong millis,bool interruptible);
  static void infinite_sleep(); // never returns, use with CAUTION
  static void yield();        // Yields to all threads with same priority
  enum YieldResult {
    YIELD_SWITCHED = 1,         // caller descheduled, other ready threads exist & ran
    YIELD_NONEREADY = 0,        // No other runnable/ready threads. 
                                // platform-specific yield return immediately
    YIELD_UNKNOWN = -1          // Unknown: platform doesn't support _SWITCHED or _NONEREADY
    //         // YIELD_SWITCHED and YIELD_NONREADY imply the platform supports a "strong" 
    //             // yield that can be used in lieu of blocking.  
    } ; 
static YieldResult NakedYield();//NOTE: not used by azul
  static void yield_all(int attempts = 0); // Yields to all other threads including lower priority
  static OSThreadPriority os_priority_from_java_priority(JavaThreadPriority java_priority);
static OSReturn get_priority(const Thread*const thread,JavaThreadPriority&priority);
  static void make_self_walkable();
static void make_remote_walkable(Thread*thread);

  static void interrupt(Thread* thread);
static bool is_interrupted(Thread*thread);
static bool is_interrupted_and_clear(Thread*thread);

static frame fetch_frame_from_context(address pc);

  static void breakpoint();

  // Misc Thread Stack functions.
  static void turnOnAzulVirtualMemory();

  static size_t expression_stack_bottom_offset() { return thread_map_jex_stack; }

  // 
  static int message_box(const char* title, const char* message, const char* prompt);

  // run cmd in a separate process and return its exit code; or -1 on failures
  static int fork_and_exec(char *cmd);

  // Set file to send error reports.
  static void set_error_file(const char *logfile);

  // os::exit() is merged with vm_exit()
  // static void exit(int num);

  // Terminate the VM, but don't exit the process
  static void shutdown();

  // Terminate with an error.  Default is to generate a core file on platforms
  // that support such things.  This calls shutdown() and then aborts.
static void abort(bool dump_core=true,bool out_of_memory=false);

  // Die immediately, no exit hook, no abort hook, no cleanup.
  static void die();

  // Reading directories.
  static DIR*           opendir(const char* dirname);
  static int            readdir_buf_size(const char *path);
static struct dirent*readdir(DIR*dirp,struct dirent*dbuf);
  static int            closedir(DIR* dirp);

  // FDs
  static int            getnopenfds(uint32_t* nopenfds);

  // Dynamic library extension
  static const char*    dll_file_extension();

  static const char*    get_temp_directory();
  static const char*    get_current_directory(char *buf, int buflen);

  // Symbol lookup, find nearest function name; basically it implements
  // dladdr() for all platforms. Name of the nearest function is copied
  // to buf. Distance from its base address is returned as offset.
  // If function name is not found, buf[0] is set to '\0' and offset is
  // set to -1.
  static bool dll_address_to_function_name(address addr, char* buf,
int buflen,int*offset,size_t*size);

  // Locate DLL/DSO. On success, full path of the library is copied to
  // buf, and offset is set to be the distance between addr and the 
  // library's base address. On failure, buf[0] is set to '\0' and
  // offset is set to -1.
  static bool dll_address_to_library_name(address addr, char* buf,
int buflen,int*offset,size_t*size);

  // Find out whether the pc is in the static code for jvm.dll/libjvm.so.
  static bool address_is_in_vm(address addr);

  // Loads .dll/.so and 
  // in case of error it checks if .dll/.so was built for the
  // same architecture as Hotspot is running on
  static void* dll_load(const char *name, char *ebuf, int ebuflen);

  // Print out system information; they are called by fatal error handler.
  // Output format may be different on different platforms.
  static void print_os_info(outputStream* st);
  static void print_cpu_info(outputStream* st);
  static void print_memory_info(outputStream* st);
  static void print_dll_info(outputStream* st);
  static void print_environment_variables(outputStream* st, const char** env_list, char* buffer, int len);
  static void print_context(outputStream* st, void* context);
  static void print_siginfo(outputStream* st, void* siginfo);
  static void print_signal_handlers(outputStream* st, char* buf, size_t buflen);
  static void print_date_and_time(outputStream* st);

  // architecture version
  static const char* arch_version();

  // The following two functions are used by fatal error handler to trace 
  // native (C) frames. They are not part of frame.hpp/frame.cpp because 
  // frame.hpp/cpp assume thread is JavaThread, and also because different
  // OS/compiler may have different convention or provide different API to 
  // walk C frames.
  //
  // We don't attempt to become a debugger, so we only follow frames if that 
  // does not require a lookup in the unwind table, which is part of the binary
  // file but may be unsafe to read after a fatal error. So on x86, we can 
  // only walk stack if %ebp is used as frame pointer; on ia64, it's not
  // possible to walk C stack without having the unwind table.
  static bool is_first_C_frame(frame *fr);
  static frame get_sender_for_C_frame(frame *fr);

  // return current frame. pc() and sp() are set to NULL on failure.
  static frame      current_frame();

  static void print_hex_dump(outputStream* st, address start, address end, int unitsize);

  // returns a string to describe the exception/signal; 
  // returns NULL if exception_code is not an OS exception/signal.
  static const char* exception_name(int exception_code, char* buf, size_t buflen);

  // Returns native Java library, loads if necessary
  static void*    native_java_library();

  // Fills in path to jvm.dll/libjvm.so (this info used to find hpi).
  static void     jvm_path(char *buf, jint buflen);

  // JNI names
  static void     print_jni_name_prefix_on(outputStream* st, int args_size);
  static void     print_jni_name_suffix_on(outputStream* st, int args_size);

  // File conventions
  static const char* file_separator();
  static const char* line_separator();
  static const char* path_separator();

  // Init os specific system properties values
  static void init_system_properties_values();

  // IO operations, non-JVM_ version.
  static int stat(const char* path, struct stat* sbuf);
  static bool dir_is_empty(const char* path);

  // IO operations on binary files
  static int create_binary_file(const char* path, bool rewrite_existing);
  static jlong current_file_offset(int fd);
  static jlong seek_to_file_offset(int fd, jlong offset);

  // Platform dependent stuff
#include"os_os.hpp"
  #include "os_os_pd.hpp"

  // Thread Local Storage
  typedef void (*LocalStorageCallback)(void *);

  static thread_key_t allocate_thread_local_storage(LocalStorageCallback destroy);
  static void         thread_local_storage_at_put(thread_key_t index, void* value);
  static void*        thread_local_storage_at(thread_key_t index);
  static void         free_thread_local_storage(thread_key_t index);

  // General allocation (must be MT-safe)
  static void* malloc  (size_t size);
  static void* realloc (void *memblock, size_t size);
  static void  free    (void *memblock);
  static bool  check_heap(bool force = false);      // verify C heap integrity
  static char* strdup(const char *);  // Like strdup

#ifndef PRODUCT
  static int  num_mallocs;            // # of calls to malloc/realloc
  static size_t  alloc_bytes;         // # of bytes allocated
  static int  num_frees;              // # of calls to free
#endif

  // Printing 64 bit integers
  static const char* jlong_format_specifier();
  static const char* julong_format_specifier();

  // Support for signals (see JVM_RaiseSignal, JVM_RegisterSignal)
  static void  signal_init();
  static void  signal_init_pd();
  static void  signal_notify(int signal_number);
  static void* signal(int signal_number, void* handler);
  static void  signal_raise(int signal_number);
  static int   signal_wait();
  static void* user_handler();
  static void  terminate_signal_thread();
  static int   sigexitnum_pd();
  static int   sigbreaknum_pd();

  // random number generation
  static int32_t random_impl(int32_t *seed_addr); // return 31bit pseudorandom number
  static int32_t random();                        // return 31bit pseudorandom number from global seed
  static void init_random(int32_t initval);       // initialize global random sequence

  // Structured OS Exception support
  static void os_exception_wrapper(java_call_t f, JavaValue* value, methodHandle* method, JavaCallArguments* args, Thread* thread);

  // JVMPI/JVMTI & JVM monitoring and management support
  // The thread_cpu_time() and current_thread_cpu_time() are only
  // supported if is_thread_cpu_time_supported() returns true.
  // They are not supported on Solaris T1.

  // Thread CPU Time - return the fast estimate on a platform
  // On Solaris - call gethrvtime (fast) - user time only
  // On Linux   - fast clock_gettime where available - user+sys
  //            - otherwise: very slow /proc fs - user+sys
  // On Windows - GetThreadTimes - user+sys
  static jlong current_thread_cpu_time();
  static jlong thread_cpu_time(Thread* t);
static jlong thread_cpu_time_millis(Thread*t);

  // Thread CPU Time with user_sys_cpu_time parameter.
  //
  // If user_sys_cpu_time is true, user+sys time is returned.
  // Otherwise, only user time is returned
  static jlong current_thread_cpu_time(bool user_sys_cpu_time);
  static jlong thread_cpu_time(Thread* t, bool user_sys_cpu_time);

  // Return a bunch of info about the timers.
  // Note that the returned info for these two functions may be different
  // on some platforms
  static void current_thread_cpu_time_info(jvmtiTimerInfo *info_ptr);
  static void thread_cpu_time_info(jvmtiTimerInfo *info_ptr);

  static bool is_thread_cpu_time_supported();

  // System loadavg support.  Returns -1 if load average cannot be obtained.
  static int loadavg(double loadavg[], int nelem);

  // Hook for os specific jvm options that we don't want to abort on seeing
  static bool obsolete_option(const JavaVMOption *option);

  // Support for remote native library handles
  static bool is_remote_handle(address handle);
static address mark_remote_handle(address handle);
static address get_real_remote_handle(address handle);

  // Some (quick) notions of time
  static uint64_t ticks_to_millis(uint64_t ticks );
  static uint64_t ticks_to_micros(uint64_t ticks );
  static uint64_t ticks_to_nanos (uint64_t ticks );
  static uint64_t millis_to_ticks(uint64_t millis);
  static uint64_t micros_to_ticks(uint64_t micros);
  static uint64_t nanos_to_ticks (uint64_t nanos );
  
  // debugging support (mostly used by debug.cpp)
static bool find(address pc,outputStream*os);//OS specific function to make sense out of an address

  static bool dont_yield();                     // when true, JVM_Yield() is nop
  static void print_statistics();

  // Thread priority helpers (implemented in OS-specific part)
static OSReturn set_native_priority(Thread*thread,OSThreadPriority native_prio);
  static OSReturn get_native_priority(const Thread* const thread, OSThreadPriority* priority_ptr);
  static OSThreadPriority java_to_os_priority[MaxPriority + 2];
  // Hint to the underlying OS that a task switch would not be good.
  // Void return because it's a hint and can fail.
  static void hint_no_preempt();

  // Used at creation if requested by the diagnostic flag PauseAtStartup.  
  // Causes the VM to wait until an external stimulus has been applied 
  // (for Unix, that stimulus is a signal, for Windows, an external 
  // ResumeThread call)
  static void pause();

 protected:
static int32_t _rand_seed;//seed for random number generator
  static int _processor_count;              // number of processors

  static char* format_boot_path(const char* format_string,
                                const char* home,
                                int home_len,
                                char fileSep,
                                char pathSep);
  static bool set_boot_path(char fileSep, char pathSep);
};

// Note that "PAUSE" is almost always used with synchronization
// so arguably we should provide Atomic::SpinPause() instead
// of the global SpinPause() with C linkage.  
// It'd also be eligible for inlining on many platforms. 

extern "C" int SpinPause () ; 
extern "C" int SafeFetch32 (int * adr, int errValue) ; 
extern "C" intptr_t SafeFetchN (intptr_t * adr, intptr_t errValue) ; 

#endif // OS_HPP
