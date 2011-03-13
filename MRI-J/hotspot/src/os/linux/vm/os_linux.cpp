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


#include "arguments.hpp"
#include "arrayOop.hpp"
#include "codeCache.hpp"
#include "defaultStream.hpp"
#include "disassembler_pd.hpp"
#include "interfaceSupport.hpp"
#include "hpi.hpp"
#include "java.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "perfMemory.hpp"
#include "runtimeService.hpp"
#include "sharedUserData.hpp"
#include "stubRoutines.hpp"
#include "thread.hpp"
#include "threadCounters.hpp"
#include "tickProfiler.hpp"
#include "vmError.hpp"
#include "vmTags.hpp"
#include "vmThread.hpp"
#include "vm_version_pd.hpp"
#include "xmlBuffer.hpp"

#include "gpgc_readTrapArray.hpp"
#include "gpgc_interlock.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "gpgc_layout.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// put OS-includes here
#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#else // !AZ_PROXIED:
#include "jvm_os.h"
#endif // !AZ_PROXIED

#include "ciEnv.hpp"

#ifndef AZ_PROXIED
#include <aznix/az_memory.h>
#include <aznix/az_pgroup.h>
#endif // !AZ_PROXIED
#include <os/process.h>
#include <os/config.h>
#include <os/exceptions.h>
#include <os/thread.h>
#include <os/utilities.h>

#ifdef AZ_PROFILER
#include <azprof/azprof_demangle.hpp>
#endif // AZ_PROFILER

// TODO - This should come from a header file.
extern "C" long  __sysconf(int);
extern "C" int __gettimeofday (struct timeval *__restrict __tv, __timezone_ptr_t __tz);
extern "C" const char* avmSymbolIndex_findSymbolByAddress(address_t address, size_t* range, address_t* base);
extern "C" int  whack_main_stack_asm(intptr_t stk, void* create_vm, void* arg1, void* arg2);

#define MAX_PATH (2 * K)

// for timer info max values which include all bits
#define ALL_64_BITS CONST64(0xFFFFFFFFFFFFFFFF)

// Clock ticks per second as reported by sysconf(), and used by times():
static int sysconf_clock_tics_per_sec = 100; // sysconf() will be used to establish real value.

// minimum allowable stack size for thread creation
size_t  os::min_stack_allowed = 2*VM_SMALL_PAGE_SIZE;

// maximum allowable stack size for thread creation
size_t  os::max_stack_allowed = thread_stack_size;

// az_mmap accounting for c-heap memory:
intptr_t* os::_az_mmap_balance = NULL;
intptr_t* os::_az_mmap_peak    = NULL;

// Redirect a function to another function by replacing the first opcode with a new jump inst opcode
// Notes:
//  - jumpInstAddr is a pointer to the replacement jump opcode
//  - Do not step into this function gdb (the text protection games confuse gdb horribly)
void os::redirectStaticFunction(address functionAddr, address destAddr) {
  Unimplemented();
  //address functionAddr_page = (address)(((uintptr_t)functionAddr)&(uintptr_t)vm_large_page_address_mask());
  //int retval = unprotect_memory((char*)functionAddr_page, vm_large_page_size());
  //*((instr_t*)functionAddr) = NativeInstruction::jmp(destAddr);
  //retval = memory_sync(os::current_process(), MEMORY_SYNC_DATA2CODE, sizeof(instr_t), functionAddr);
  //retval = protect_memory((char*)functionAddr_page, vm_large_page_size());
}

pid_t             os::_process_id   = 0;
int os::_vm_page_size=0;
int               os::_vm_large_page_size = -1;
int               os::_vm_zero_page_size  = -1;
intptr_t          os::_os_thread_limit    = 0;
volatile intptr_t os::_os_thread_count    = 0;
volatile intptr_t os::_unique_thread_id   = 1; // skip zero
julong os::Linux_OS::_physical_memory=0;
jlong             os::Linux_OS::_last_sync_millis   = 0;
jlong             os::Linux_OS::_last_sync_elapsed_count = 0;
int               os::Linux_OS::_last_sync_latch    = 0;
address os::Linux_OS::_jvm_start_address=NULL;
address os::Linux_OS::_jvm_end_address=NULL;
bool              os::Linux_OS::_flush_overdraft_only_supported = false;

#ifdef AZ_PROXIED
int               os::Linux_OS::_proxy_process_id   = 0;
#endif // AZ_PROXIED

jlong             os::Linux_OS::_allocation_id      = 0;
bool              os::Linux_OS::_use_azmem          = 0;
bool              os::Linux_OS::_use_azsched        = 0;

static const char *find_symbol_by_address(address_t address, size_t *range, address_t *base) 
{
#ifdef AZ_PROXIED // we can remove this ifdef if we can get dlsym to work
  // dladdr does not work on a static executable. Hence this workaround.
  const char* name = avmSymbolIndex_findSymbolByAddress(address, range, base);
  return name;
#else // !AZ_PROXIED
  Dl_info dlinfo;
  Elf64_Sym *elfinfo;
  
  if (dladdr1(address, &dlinfo, (void **)&elfinfo, RTLD_DL_SYMENT) == 0) return NULL;
if(elfinfo==NULL)return NULL;
  *base = dlinfo.dli_saddr;
  *range = elfinfo->st_size;
  return dlinfo.dli_sname;
#endif // !AZ_PROXIED
}

extern "C" void _init();
address_t lowest_symbol_address() {
  return (address_t)&_init;
}

extern "C" void _fini();
address_t highest_symbol_address() {
  return (address_t)&_fini;
}

// used to see if the elf symbol table includes weak symbols
void _known_weak_symbol() __attribute__ ((weak));
void _known_weak_symbol() {}

void os::Linux_OS::initialize_system_info(){
#ifdef AZ_PROXIED
  // Get the backend pid
  _process_id = ::process_id(0);
  _proxy_process_id = proxy_getpid();
#else // !AZ_PROXIED
_process_id=getpid();
#endif // !AZ_PROXIED

  if (1 == os_should_use_azmem()) {
    Linux_OS::set_use_azmem(true);
    BatchedMemoryOps = true;
    PageHealing = true;
  }

  if (1 == os_should_use_azsched()) {
    Linux_OS::set_use_azsched(true);
  }

  _allocation_id = process_get_allocationid();
guarantee(_allocation_id!=0,"_allocation_id cannot be 0");

  size_t size;
address_t start;
  const char* found_symbol_name = find_symbol_by_address((address_t)_known_weak_symbol, &size, &start);
  if (!found_symbol_name || strstr(found_symbol_name, "known_weak_symbol") == NULL) {
warning("ELF symtab doesn't have weak symbols");
  }

  // The location of the jvm image in memory
  _jvm_start_address = (address)lowest_symbol_address();
  _jvm_end_address   = (address)highest_symbol_address();

  // Azul note: Some or all of these values may be dynamic in future implementations.
  //julong pagesize = system_config(SYSTEM_PAGESIZE);
  _vm_page_size       = BytesPerSmallPage;
  _vm_large_page_size = BytesPerLargePage;
  _vm_zero_page_size  = BytesPerLargePage;

_processor_count=__sysconf(_SC_NPROCESSORS_CONF);
  if (_processor_count <= 0) { vm_exit_during_initialization("sysconf(_SC_NPROCESSORS_CONF) failed"); }

  // TODO - os_linux.cpp in J2SE 6 from Sun has code to verify the /proc filesystem can
  // be found, and issue a warning. This might need to be done for the Tall product.

if(os::use_azmem()){
    int64_t balance       = 0;
    int64_t balance_min   = 0;
    int64_t balance_max   = 0;
    size_t  allocated     = 0;
    size_t  allocated_min = 0;
    size_t  allocated_max = 0;
    size_t  maximum       = 0;

    memory_account_get_stats(CHEAP_COMMITTED_MEMORY_ACCOUNT,
                             &balance, &balance_min, &balance_max,
                             &allocated, &allocated_min, &allocated_max, &maximum);
    // AZUL - Set physical memory to committed. Should not rely on the grant pool.
    //        So this is all we've got.
    _physical_memory = balance + allocated;
  } else {
_physical_memory=(julong)__sysconf(_SC_PHYS_PAGES)*(julong)__sysconf(_SC_PAGESIZE);
  }

  os::_az_mmap_balance = NEW_C_HEAP_ARRAY(intptr_t, Modules::MAX_MODULE);
  os::_az_mmap_peak    = NEW_C_HEAP_ARRAY(intptr_t, Modules::MAX_MODULE);

  memset(os::_az_mmap_balance, 0, sizeof(intptr_t)*Modules::MAX_MODULE);
  memset(os::_az_mmap_peak,    0, sizeof(intptr_t)*Modules::MAX_MODULE);
}  // end of initialize_system_info()

void os::disable_azmem(){
  os_disable_azmem();
}

void os::disable_azsched(){
  os_disable_azsched();
}

bool os::using_az_kernel_modules(){
  if (os::use_azmem() || os::use_azsched()) {
    return true;
  }

  return false;
}

bool os::use_azmem(){
  return Linux_OS::use_azmem();
}

bool os::use_azsched(){
  return Linux_OS::use_azsched();
}

bool os::should_start_suspended(){
  return os_should_start_suspended();
}

void os::set_start_suspended(int ss) {
  os_set_start_suspended(ss);
}

void os::set_memcommit(int64_t memcommit) {
  os_set_memcommit(memcommit);
}

int64_t os::memcommit() {
  return os_memcommit();
}

void os::set_memmax(int64_t memmax) {
  os_set_memmax(memmax);
}

int64_t os::memmax() {
  return os_memmax();
}

#ifndef AZ_PROXIED
// FIXME - This function is called from the java launcher.
// Need to make the c-main CreateJavaVM path work.
int os::launch_avm(void* javamain, void* javamain_args, void* vm_args) {
  init_azsys();

  // Parse the PX options before calling os_setup_avm_launch_environment.
  // TODO - need to parse azul.vm.initargs.pre and .post
  Arguments::parse_px_options((JavaVMInitArgs*) vm_args);

  bool start_suspended = os::should_start_suspended();
  while (start_suspended) {
    usleep(1000L);    // Sleep 1 millisecond
  }

  // Create pgroup, allocationid and setup azmem/azsched, etc.
  os_setup_avm_launch_environment();

  // prepare the linked list of free stack frames. This should happen after
  // os_setup_avm_launch_environment.
  init_all_thread_stacks();

  // Create a large stack and start the VM on that stack
  return thread_whack_main_stack(javamain, javamain_args);
}
#endif // !AZ_PROXIED

// I/O interruption related counters called in _INTERRUPTIBLE

void os::Linux_OS::bump_interrupted_before_count(){
  RuntimeService::record_interrupted_before_count();
}

void os::Linux_OS::bump_interrupted_during_count(){
  RuntimeService::record_interrupted_during_count();
}

julong os::available_memory() {
  int64_t balance       = 0;
  int64_t balance_min   = 0;
  int64_t balance_max   = 0;
  size_t  allocated     = 0;
  size_t  allocated_min = 0;
  size_t  allocated_max = 0;
  size_t  maximum       = 0;

  os::memory_account_get_stats(os::CHEAP_COMMITTED_MEMORY_ACCOUNT,
                               &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

  if (balance < 0) balance = 0;
  
  return (julong) balance;
}

julong os::physical_memory() {
return Linux_OS::physical_memory();
}

void os::check_heap_size(){
  julong memory_balance = available_memory();

  if (MaxHeapSize > memory_balance) {
if(os::use_azmem()){
vm_exit_during_initialization("Java heap is bigger than remaining committed balance",
"MemCommit calculation is out of sync with MaxHeapSize");
    } else {
warning("Java heap is bigger than available memory");
    }
  }
}

int os::active_processor_count() {
  // Find out how many CPUs we have available for use.

  // Get the active cpus on the box:
  long online_cpus = __sysconf(_SC_NPROCESSORS_ONLN);
  assert(online_cpus > 0 && online_cpus <= processor_count(), "sanity check");

if(os::use_azsched()){
#if 0
    // TODO: Need to get cpu allocation from CPM going
    // With AzSched return the minimum of the assigned cpus of the process group
    // (upper limit) and the number of active cpus on the box.

    // Need an azched interface for this.
    Unimplemented();
#endif  // #if 0
  }

  return online_cpus;
}


bool os::distribute_processes(uint length, uint* distribution) {
  bool result = false;
  // Azul note: With azsched, we don't currently support binding threads to processors.
  // However, this might be useful for Parallel Scavenge to help out the scheduler.
  return result;
}

bool os::bind_to_processor(uint processor_id) {
  // Azul note: With azsched, we don't currently support binding threads to processors.
  // However, this might be useful for Parallel Scavenge to help out the scheduler.
  ShouldNotReachHere();
  return false;
}


// Thread Quantum
// azsched/linux do not support setting a thread's quantum which is used
// for scheduling.  Because we might want to implement this in the future,
// I'll leave the code but it will follow these rules.
// (1) Any attempt to change/set thread quanta will succeed but will have
//     no effect and be ignored.  This applies to both commandline options
//     and other programmatic interfaces.
// (2) Any attempt to read/get thread quanta will return a -1.
bool os::set_thread_quantum(int64_t millis) {
  return true;
}

int64_t os::get_thread_quantum() {
  return -1;
}


bool os::getenv(const char*name,char*buffer,int len){
#ifdef AZ_PROXIED
  char* val = ::proxy_getenv(name, buffer, len);
#else // !AZ_PROXIED:
  char* val = ::getenv(name);
#endif
  if ( (val==NULL) || ( ((int)strlen(val)+1)>len) ) {
    if ( len > 0 ) buffer[0] = '\0'; // return a null string
    return false;
  }
strcpy(buffer,val);
  return true;
}


// Return true if user is running as root.

bool os::have_special_privileges() {
  static bool init = false;
  static bool privileges = false;
  if (!init) {
    privileges = (getuid() != geteuid()) || (getgid() != getegid());
    init = true;
  }
  return privileges;
}


DIR* os::opendir(const char* dirname) {
assert(dirname!=NULL,"just checking");
#ifdef AZ_PROXIED
  return ::opendir_r(dirname, /* ignored */ NULL);
#else // !AZ_PROXIED:
  return ::opendir(dirname);
#endif // !AZ_PROXIED:
}

int os::readdir_buf_size(const char *path) {
#ifdef AZ_PROXIED
  return proxy_readdir_buf_size(path);
#else // !AZ_PROXIED:
  return 1000;  // Arbitrary number to make the APIs happy.
#endif // !AZ_PROXIED
}

struct dirent *os::readdir(DIR* dirp, struct dirent *dbuf) {
assert(dirp!=NULL,"just checking");
#ifdef AZ_PROXIED
  return ::readdir_r(dirp, dbuf);
#else // !AZ_PROXIED:
return::readdir(dirp);
#endif // !AZ_PROXIED:
}

int os::closedir(DIR *dirp) {
assert(dirp!=NULL,"just checking");
#ifdef AZ_PROXIED
  int rc = ::closedir_r(dirp);
#else // !AZ_PROXIED:
  int rc = ::closedir(dirp);
#endif // !AZ_PROXIED:
  return rc;
}

static char cpu_arch[] = "amd64";

void os::init_system_properties_values() {
#ifdef AZ_PROXIED
  // Proxy does (most of) this.
  proxy_props_t system_properties;

  proxy_error_t rc = proxy_get_jvm_properties(&system_properties);
  guarantee(rc == PROXY_ERROR_NONE, "proxy_get_jvm_properties failed");

  Arguments::set_dll_dir(system_properties.dll_dir);
  Arguments::set_java_home(system_properties.java_home);
  Arguments::set_library_path(system_properties.library_path);
  Arguments::set_ext_dirs(system_properties.ext_dirs);
  Arguments::set_endorsed_dirs(system_properties.endorsed_dirs);
  
  if (!set_boot_path('/', ':')) {
vm_exit_during_initialization("set_boot_path failed - out of memory");
  }
#else // !AZ_PROXIED:
  //  char arch[12];
  //  sysinfo(SI_ARCHITECTURE, arch, sizeof(arch));

  // The next steps are taken in the product version:
  //
  // Obtain the JAVA_HOME value from the location of libjvm[_g].so.
  // This library should be located at:
  // <JAVA_HOME>/jre/lib/<arch>/{client|server}/libjvm[_g].so.
  //
  // If "/jre/lib/" appears at the right place in the path, then we 
  // assume libjvm[_g].so is installed in a JDK and we use this path. 
  //
  // Otherwise exit with message: "Could not create the Java virtual machine."
  //
  // The following extra steps are taken in the debugging version:
  //
  // If "/jre/lib/" does NOT appear at the right place in the path
  // instead of exit check for $JAVA_HOME environment variable.
  //
  // If it is defined and we are able to locate $JAVA_HOME/jre/lib/<arch>,
  // then we append a fake suffix "hotspot/libjvm[_g].so" to this path so
  // it looks like libjvm[_g].so is installed there
  // <JAVA_HOME>/jre/lib/<arch>/hotspot/libjvm[_g].so.
  //
  // Otherwise exit. 
  //
  // Important note: if the location of libjvm.so changes this 
  // code needs to be changed accordingly.

  // The next few definitions allow the code to be verbatim:
#define malloc(n) (char*)NEW_C_HEAP_ARRAY(char, (n))
#define getenv(n) ::getenv(n)

/*
 * See ld(1):
 *	The linker uses the following search paths to locate required
 *	shared libraries:
 *	  1: ...
 *	  ...
 *	  7: The default directories, normally /lib and /usr/lib.
 */
#define DEFAULT_LIBPATH	"/lib:/usr/lib"

#define EXTENSIONS_DIR	"/lib/ext"
#define ENDORSED_DIR	"/lib/endorsed"
#define REG_DIR		"/usr/java/packages"

  {
    /* sysclasspath, java_home, dll_dir */
    {
      char *home_path;
      char *dll_path;
      char *pslash;
      char buf[MAXPATHLEN];
      os::jvm_path(buf, sizeof(buf));

      // Found the full path to libjvm.so. 
      // Now cut the path to <java_home>/jre if we can. 
      *(strrchr(buf, '/')) = '\0';  /* get rid of /libjvm.so */
      pslash = strrchr(buf, '/');
      if (pslash != NULL)
        *pslash = '\0';           /* get rid of /{client|server|hotspot} */
      dll_path = malloc(strlen(buf) + 1);
      if (dll_path == NULL)
        return;
      strcpy(dll_path, buf);
      Arguments::set_dll_dir(dll_path);

      if (pslash != NULL) {
        pslash = strrchr(buf, '/');
        if (pslash != NULL) {
          *pslash = '\0';       /* get rid of /<arch> */ 
          pslash = strrchr(buf, '/');
          if (pslash != NULL)
            *pslash = '\0';   /* get rid of /lib */
        }
      }

      home_path = malloc(strlen(buf) + 1);
      if (home_path == NULL)
        return;
      strcpy(home_path, buf);
      Arguments::set_java_home(home_path);

      if (!set_boot_path('/', ':'))
        return;
    }

    /*
     * Where to look for native libraries
     *
     * Note: Due to a legacy implementation, most of the library path
     * is set in the launcher.  This was to accomodate linking restrictions
     * on legacy Linux implementations (which are no longer supported).
     * Eventually, all the library path setting will be done here.
     *
     * However, to prevent the proliferation of improperly built native
     * libraries, the new path component /usr/java/packages is added here.
     * Eventually, all the library path setting will be done here.
     */
    {
      char *ld_library_path;

      /*
       * Construct the invariant part of ld_library_path. Note that the
       * space for the colon and the trailing null are provided by the
       * nulls included by the sizeof operator (so actually we allocate
       * a byte more than necessary).
       */
      ld_library_path = (char *) malloc(sizeof(REG_DIR) + sizeof("/lib/") +
          strlen(cpu_arch) + sizeof(DEFAULT_LIBPATH));
      sprintf(ld_library_path, REG_DIR "/lib/%s:" DEFAULT_LIBPATH, cpu_arch);

      /*
       * Get the user setting of LD_LIBRARY_PATH, and prepended it.  It
       * should always exist (until the legacy problem cited above is
       * addressed).
       */
      char *v = getenv("LD_LIBRARY_PATH");
      if (v != NULL) {
        char *t = ld_library_path;
        /* That's +1 for the colon and +1 for the trailing '\0' */
        ld_library_path = (char *) malloc(strlen(v) + 1 + strlen(t) + 1);
        sprintf(ld_library_path, "%s:%s", v, t);
      }
      Arguments::set_library_path(ld_library_path);
    }

    /*
     * Extensions directories.
     *
     * Note that the space for the colon and the trailing null are provided
     * by the nulls included by the sizeof operator (so actually one byte more
     * than necessary is allocated).
     */
    {
      char *buf = malloc(strlen(Arguments::get_java_home()) +
          sizeof(EXTENSIONS_DIR) + sizeof(REG_DIR) + sizeof(EXTENSIONS_DIR));
      sprintf(buf, "%s" EXTENSIONS_DIR ":" REG_DIR EXTENSIONS_DIR,
          Arguments::get_java_home());
      Arguments::set_ext_dirs(buf);
    }

    /* Endorsed standards default directory. */
    {
      char * buf;
      buf = malloc(strlen(Arguments::get_java_home()) + sizeof(ENDORSED_DIR));
      sprintf(buf, "%s" ENDORSED_DIR, Arguments::get_java_home());
      Arguments::set_endorsed_dirs(buf);
    }
  }

#undef malloc
#undef getenv
#undef EXTENSIONS_DIR
#undef ENDORSED_DIR

#endif // !AZ_PROXIED
}

void os::breakpoint() {
  BREAKPOINT;
}

bool os::obsolete_option(const JavaVMOption *option) {
  // Proxy deals with some of this.
  // We may need to filter out options the proxy supports (because it's faking the host JVM) but the AVM does not.
  // Note: Passing arguments that look like options will cause this function to be called.
  // e.g. java -verbose:class -s1 -m _209_db [main class name omitted in error]
  return false;
}


static pthread_t main_thread;

// Thread start routine for all new Java threads
extern "C" {
  static void _start_thread(Thread* thread) {
    // On linux, thread starts immediately and is racey with the call
    // to "pd_start_thread"
    while( !thread->osthread() )
      usleep(100);
    while( thread->thread_counters()->start_time() == 0 )
      usleep(100);

    thread->osthread()->set_thread_id(::thread_gettid());

    thread->run();
  }
}

// Called from create_attached_thread(), create_main_thread() and create_thread().
// libos thread is already created and init'd at this point. It may already be running.
// We need to know the thread type to determine whether the thread should be registered
// for deferred pre-emption or not.
//
// Note: There is a minor asymmetry here. In some cases HotSpot itself creates the thread; in
// other cases, some other layer is responsible and the thread has been attached to the JVM.
static OSThread* create_os_thread(pthread_t pthread_id, ttype::ThreadType thr_type) {
  // Allocate the OSThread object
  OSThread* osthread = new OSThread(NULL, NULL);
if(!osthread)return NULL;

  // Initialize support for Java interrupts
osthread->clr_interrupted();

  // Store info on the Linux thread into the OSThread
  // This may be thread_self() or a handle from thread_create().
osthread->set_pthread_id(pthread_id);
    
  Atomic::add_ptr(1, &os::_os_thread_count);
    
  osthread->set_thread_identifier(Atomic::post_increment( (jlong*)&os::_unique_thread_id));
  
  return osthread;
}


bool os::create_attached_thread(JavaThread* thread) {
#ifdef ASSERT
  thread->verify_not_published();
#endif

pthread_t pthread_handle=pthread_self();
  // Attached threads are always java threads
  OSThread* osthread = create_os_thread(pthread_handle, ttype::java_thread);

  thread->set_osthread(osthread);

  if (osthread == NULL) {
    return false;
  }

  return true;
}

bool os::create_main_thread(JavaThread* thread) 
{
if(_starting_thread==NULL)
  {
    // The main (starting) thread is always a java thread
    _starting_thread = create_os_thread(main_thread, ttype::java_thread);
guarantee(_starting_thread!=NULL,"create_os_thread: _starting_thread failed");
  }
thread->set_osthread(_starting_thread);

  // main_thread does not go through _start_thread, so we need to initialize
  // osthread->_thread_id here
  thread->osthread()->set_thread_id(::thread_gettid());

if(_starting_thread==NULL){
    return false;
  }

  return true;
}


// We do the memory reservation and allocation here so that if we choose, HotSpot can
// micro-manage the stack layout for all the threads it creates.
// This is called as part of the Thread object constructor.  The returned address is
// the location of the Thread instance for the stack we're creating.
address os::create_thread_stack(ttype::ThreadType thr_type, size_t stack_size) 
{
  void* thread_stack = ::thread_stack_create(); // returns a stack, or NULL if out of stacks
return(address)thread_stack;
}


// On Linux, when this is called, the user stack is already setup, since Thread *thread
// already exists. This is allocated when the Thread object is created (see above).
bool os::create_thread(Thread* thread, ttype::ThreadType thr_type, size_t stack_size)  {
  // The thread is returned suspended (in state INITIALIZED), and is started higher up in the call chain

  if (os::_os_thread_count > os::_os_thread_limit) {
    // We got lots of threads. This should not happen - we are not sizing memory correctly.
    // This should lead to an out of memory exception.
    debug_only(warning("Threads limit (current=%d, max=%d) for the process reached. (os_linux.cpp)", os::_os_thread_count, os::_os_thread_limit));
    return false;
  }

  // somebody is trying to set a real stacksize?  We don't allow it.
  if( stack_size != 0 ) { Unimplemented(); }

  // Set thread up for execution
  pthread_t thread_handle = thread_init((intptr_t)thread, (Thread_Start_Function)_start_thread, thread);
  if( thread_handle != (pthread_t)0 ) {
    // Allocate the OSThread object
    OSThread* osthread = create_os_thread(thread_handle, thr_type);
    thread->set_osthread(osthread);
if(osthread)return true;
  }

  // Need to clean up stuff we've allocated so far
  // TODO: If we get here, it's possible thread_init() created a (now running) thread,
  // TODO: and create_os_thread() failed.  How do we cleanup?
  return false;
} // end of create_thread()


// First crack at OS-specific initialization, from inside the new thread.
void os::initialize_thread() {
  // per-thread stuff like user-mode perf counters go here.

  // On X86 there are no special user-mode registers to setup,
  // and no traps to setup - so no StubRoutines trap_table.
}


// Free resources related to the OSThread
void os::free_thread(OSThread* osthread) {
assert(osthread!=NULL,"os::free_thread but osthread not set");
  // We are told to free resources of the argument thread, 
  // but we can only really operate on the current thread.
  // The main thread must take the VMThread down synchronously
  // before the main thread exits and frees up CodeHeap
  guarantee((Thread::current()->osthread() == osthread || (osthread == VMThread::vm_thread()->osthread())), "os::free_thread but not current thread");

  // Discard stack.
  // Usual OS weirdness: how can I free the stack of the thread that is running?
  // This is an OS job: to recycle the current thread's stack
  //free(osthread->stack_base());
  
  delete osthread;
  // thread will exit and clean up its stack accordingly.
  
  // One less thread is executing
  Atomic::dec_ptr(&os::_os_thread_count);
}


void os::pd_start_thread(Thread* thread) {
  // TODO: In old code we called this: thread_resume(thread->osthread()->thread_id());
  // TODO: Not sure if there's an equivalent on Linux.
  ThreadCounters* tc = thread->thread_counters();
  if (tc->start_time() == 0) {
    tc->set_start_time(os::elapsed_counter());
  }
}
  

intx os::current_thread_id() {
  pid_t tid = thread_gettid();
  return (intx)tid;
}

int os::current_process_id() {
#ifdef AZ_PROXIED
  // Azul note: We cached the proxy version
  return Linux_OS::_proxy_process_id;
#else  // Not proxied:
  return process_id();
#endif
}

thread_key_t os::allocate_thread_local_storage(LocalStorageCallback destroy) {
thread_key_t key;
int rslt=pthread_key_create(&key,destroy);
guarantee(rslt==0,"cannot allocate thread local storage");
  return (int)key;
}

// Note: This is currently not used by VM, as we don't destroy TLS key
// on VM exit.
void os::free_thread_local_storage(thread_key_t index) {
  int rslt = pthread_key_delete(index);
guarantee(rslt==0,"pthread_key_delete(): invalid key");
}

void os::thread_local_storage_at_put(thread_key_t index, void* value) {
  int rslt = pthread_setspecific(index, value);
guarantee(rslt==0,"pthread_setspecific failed");
}

void* os::thread_local_storage_at(thread_key_t index) {
return pthread_getspecific(index);
}

static jlong initial_performance_count = 0L;
jlong performance_frequency = 0L;

void init_elapsed_time() {
initial_performance_count=os::elapsed_counter();

  // Get clock rate
  struct sysconf_frequency hz;
  sys_return_t rc = system_configuration(SYSTEM_CONFIGURATION_CPU_FREQUENCY, &hz, sizeof(hz));
  if (rc != SYSERR_NONE) {
vm_exit_during_initialization("Failed to get sytem clock frequency");
  }

  performance_frequency = int64_t(hz.sysc_numerator);

  // Linux reports some stats in "Clock Ticks", which are actually not
  // related to hardware clock ticks in any obvious way.
  sysconf_clock_tics_per_sec = __sysconf(_SC_CLK_TCK);
}

// Time since start-up in seconds to a fine granularity.
// Used by VMSelfDestructTimer and the MemProfiler.
double os::elapsedTime() {
  double count = (double) os::elapsed_counter() - initial_performance_count;
  double freq  = (double) os::elapsed_frequency();
  return count/freq;
}

jlong os::javaTimeNanos() {
  // Fortuantely, the spec for java.lang.System.nanoTime() allows us to avoid
  // ever hitting the proxy (see below) and instead we can use the count
  // register which is synchronized across all cpus (we hope) and is incremented
  // with a system-wide clock.
  //
  // "This method can only be used to measure elapsed time and is not related to
  // any other notion of system or wall-clock time. The value returned
  // represents nanoseconds since some fixed but arbitrary time (perhaps in the
  // future, so values may be negative). This method provides nanosecond
  // precision, but not necessarily nanosecond accuracy. No guarantees are made
  // about how frequently values change." -- 1.6 API Javadocs
  return (jlong) ticks_to_nanos(os::elapsed_counter());
}

void os::javaTimeNanos_info(jvmtiTimerInfo *info_ptr) {
info_ptr->max_value=ALL_64_BITS;//count register uses all 64 bits
  info_ptr->may_skip_backward = false;    // not subject to resetting or drifting
  info_ptr->may_skip_forward = false;     // not subject to resetting or drifting
info_ptr->kind=JVMTI_TIMER_ELAPSED;//elapsed system not per-thread CPU time
}


// Return the real, user, and system times in seconds from an
// arbitrary fixed point in the past.
bool os::getTimesSecs(double* process_real_time,
                      double* process_user_time,
                      double* process_system_time) {
  struct tms ticks;
  clock_t    real_ticks = times(&ticks);

  if (real_ticks == (clock_t) (-1)) {
    return false;
  } else {
double ticks_per_second=(double)sysconf_clock_tics_per_sec;
    *process_user_time      = ((double) ticks.tms_utime) / ticks_per_second;
    *process_system_time    = ((double) ticks.tms_stime) / ticks_per_second;
    *process_real_time      = ((double) real_ticks) / ticks_per_second;

    return true;
  }
}


// Note: os::shutdown() might be called very early during initialization, or
// called from signal handler. Before adding something to os::shutdown(), make
// sure it is async-safe and can handle partially initialized VM.
void os::shutdown() {

  // allow PerfMemory to attempt cleanup of any persistent resources
  perfMemory_exit();

  // flush buffered output, finish log files
  ostream_abort();

  // Check for abort hook
  abort_hook_t abort_hook = Arguments::abort_hook();
  if (abort_hook != NULL) {
    abort_hook();
  }
}

// Note: os::abort() might be called very early during initialization, or
// called from signal handler. Before adding something to os::abort(), make
// sure it is async-safe and can handle partially initialized VM.
void os::abort(bool dump_core,bool out_of_memory){
  os::shutdown();
  if (dump_core) {
#ifndef PRODUCT
    fdStream out(defaultStream::output_fd());
    out.print_raw("Current thread is ");
    char buf[16];
    jio_snprintf(buf, sizeof(buf), UINTX_FORMAT, os::current_thread_id());
    out.print_raw_cr(buf);
    out.print_raw_cr("Dumping core ...");
#endif
    BREAKPOINT;
::abort();//dump core (for debugging)
  }
  if (out_of_memory) {
    Unimplemented();
    // TODO: figure this out:
    // ::exit_out_of_memory();
  } else {
    ::exit(1);
  }
}

// Die immediately, no exit hook, no abort hook, no cleanup.
void os::die() {
  BREAKPOINT;
::abort();//dump core (for debugging)
}

// unused on linux for now.
void os::set_error_file(const char *logfile) {}

// DLL functions

// check if addr is inside libjvm[_g].so
bool os::address_is_in_vm(address addr) {
  // Check if inside libjvm - or any part of this binary.
  if (Linux_OS::_jvm_start_address != NULL && Linux_OS::_jvm_end_address != NULL) {
    if ((Linux_OS::_jvm_start_address <= addr) && (addr <= Linux_OS::_jvm_end_address)) {
      return true;
    }
  }

  // Must be a bogus PC.
  return false;
}

// The below functions can now all be implemented using static symbol lookup, if relatively inefficiently.
bool os::dll_address_to_function_name(address addr, char* buf, int buflen, int * offset, size_t *sym_size) {
address sym_addr;
  const char* symname;

  // Warning: find_symbol_by_address uses the reverse order for 2nd & 3rd args
  symname = find_symbol_by_address(addr,  sym_size, (address_t*)&sym_addr);

  if (symname && *symname) {
    if (buf) jio_snprintf(buf, buflen, "%s", symname);
    if (offset) *offset = (addr - sym_addr);
    return true;
  }
  else {
    if (buf) buf[0] = '\0';
    if (offset) *offset  = -1;
    return false;
  }
}

bool os::dll_address_to_library_name(address addr, char* buf, int buflen, int* offset, size_t *sym_size) {
#ifdef AZ_PROXIED
address sym_addr;
const char*tmp_buf=NULL;
  const char *symname;

  symname = find_symbol_by_address(addr, sym_size, (address_t*)&sym_addr);

  if (symname) {
#if defined (PRODUCT)
    tmp_buf = "avm";
#else
#if defined (FASTDEBUG)
    tmp_buf = "avm_fastdebug";
#else
#if defined (DEBUG)
    tmp_buf = "avm_debug";
#else
    tmp_buf = "avm_optimized";
#endif // defined(DEBUG)
#endif // defined(FASTDEBUG)
#endif // defined(PRODUCT)
    if (buf) jio_snprintf(buf, buflen, "%s", tmp_buf);
    if (offset) *offset = addr - sym_addr;
    return true;
#else // !AZ_PROXIED
  Dl_info dlinfo;

  if (dladdr((void*)addr, &dlinfo)) {
    if (buf) jio_snprintf(buf, buflen, "%s", dlinfo.dli_fname);
    if (offset) *offset = addr - (address)dlinfo.dli_fbase;
    return true;
#endif // !AZ_PROXIED
  } else {
    if (buf) buf[0] = '\0';
    if (offset) *offset = -1;
    return false;
  }
}

// Prints the names and full paths of all opened dynamic libraries
// for current process
void os::print_dll_info(outputStream *st) {
  // Not useful for azul since we are statically bound.
}

bool _print_ascii_file(const char* filename, outputStream* st) {
  Unimplemented();
  return false;
}

void os::print_os_info(outputStream* st) {
st->print("OS: ");

#ifdef AZ_PROXIED
  // Print proxy info?
st->print("Azproxied");
#else // !AZ_PROXIED
  // Print OS version?
  if (using_az_kernel_modules()) {
st->print("Azlinux");
  } else {
    st->print("Linux");
  }
#endif // !AZ_PROXIED

  st->cr();
}

void os::print_siginfo(outputStream* st, void* siginfo) {
st->print_cr("eva: "PTR_FORMAT,siginfo);
}

static void print_signal_handler(outputStream* st, int sig, 
                                 char* buf, int buflen) {
  // Nothing to do here 
}

void os::print_signal_handlers(outputStream* st, char* buf, size_t buflen) {
  // Nothing to do here 
  // st->print_cr("Signal Handlers:");
}

void os::print_memory_info(outputStream* st) {
  int64_t balance       = 0;
  int64_t balance_min   = 0;
  int64_t balance_max   = 0;
  size_t  allocated     = 0;
  size_t  allocated_min = 0;
  size_t  allocated_max = 0;
  size_t  maximum       = 0;

  os::memory_account_get_stats(os::CHEAP_COMMITTED_MEMORY_ACCOUNT,
                               &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

st->print_cr("%s memory statistics: maximum = %ldM  balance = %ldM  allocated = %ldM",
                VM_Version::vm_name(), maximum / M, balance / M, allocated / M);
}

static char saved_jvm_path[MAXPATHLEN] = {0};

// Find the full path to the current module, libjvm.so or libjvm_g.so
void os::jvm_path(char *buf, jint len) {
#ifdef AZ_PROXIED
  const char* path = Arguments::get_dll_dir();
  char tmp_buf[50];
  jio_snprintf(tmp_buf, sizeof(tmp_buf), "/%s/libjvm%s.so", UseC2 ? "server" : "client", debug_only("_g")"");
  jio_snprintf(buf, len, "%s%s", path, tmp_buf);

#else // !AZ_PROXIED
  // Error checking.
  if (len < MAXPATHLEN) {
    assert(false, "must use a large-enough buffer");
    buf[0] = '\0';
    return;
  }
  // Lazy resolve the path to current module.
  if (saved_jvm_path[0] != 0) {
    strcpy(buf, saved_jvm_path);
    return;
  }

  char dli_fname[MAXPATHLEN];
  bool ret = dll_address_to_library_name(
                CAST_FROM_FN_PTR(address, os::jvm_path),
dli_fname,sizeof(dli_fname),NULL,NULL);
  assert(ret != 0, "cannot locate libjvm");
  realpath(dli_fname, buf);

#if 0
  if (strcmp(Arguments::sun_java_launcher(), "gamma") == 0) {
    // Support for the gamma launcher.  Typical value for buf is
    // "<JAVA_HOME>/jre/lib/<arch>/<vmtype>/libjvm.so".  If "/jre/lib/" appears at
    // the right place in the string, then assume we are installed in a JDK and
    // we're done.  Otherwise, check for a JAVA_HOME environment variable and fix
    // up the path so it looks like libjvm.so is installed there (append a
    // fake suffix hotspot/libjvm.so).
    const char *p = buf + strlen(buf) - 1;
    for (int count = 0; p > buf && count < 5; ++count) {
      for (--p; p > buf && *p != '/'; --p)
        /* empty */ ;
    }

    if (strncmp(p, "/jre/lib/", 9) != 0) {
      // Look for JAVA_HOME in the environment.
      char* java_home_var = ::getenv("JAVA_HOME");
      if (java_home_var != NULL && java_home_var[0] != 0) {
        // Check the current module name "libjvm.so" or "libjvm_g.so".
        p = strrchr(buf, '/');
        assert(strstr(p, "/libjvm") == p, "invalid library name");
        p = strstr(p, "_g") ? "_g" : "";

        realpath(java_home_var, buf);
        sprintf(buf + strlen(buf), "/jre/lib/%s", cpu_arch);
        if (0 == access(buf, F_OK)) {
	  // Use current module name "libjvm[_g].so" instead of 
	  // "libjvm"debug_only("_g")".so" since for fastdebug version
	  // we should have "libjvm.so" but debug_only("_g") adds "_g"!
	  // It is used when we are choosing the HPI library's name 
	  // "libhpi[_g].so" in hpi::initialize_get_interface().
	  sprintf(buf + strlen(buf), "/hotspot/libjvm%s.so", p);
        } else {
          // Go back to path of .so
          realpath(dli_fname, buf);
        }
      }
    } 
  }
#endif // 0

  strcpy(saved_jvm_path, buf);

#endif // !AZ_PROXIED
}

 
void os::print_jni_name_prefix_on(outputStream* st, int args_size) {
NEEDS_CLEANUP;//Azul note: Proxy-specific, except for bootstrap libs.
  // (Different for WIN64)
}


void os::print_jni_name_suffix_on(outputStream* st, int args_size) {
NEEDS_CLEANUP;//Azul note: Proxy-specific, except for bootstrap libs.
  // (Different for WIN64)
}


// sun.misc.Signal

// Azul note: Support for sun.misc.Signal is in the proxy

void* os::user_handler() {
  return (void *)NULL;                                      // No handler is registered here
}

void* os::signal(int signal_number, void* handler) {
  return (void *)NULL;                                      // No need to register any handler here
}

void os::signal_raise(int signal_number) {
ShouldNotReachHere();//This method should never be called
}

static int       max_numsignals = 0;                              // maximum number of signals, set by the proxy
static intptr_t *pending_signals;
static sem_t     sig_sem;

int os::sigexitnum_pd() {
assert(max_numsignals>0,"must call os::init");
#ifdef AZ_PROXIED
  return proxy_get_sigexit_num();
#else // !AZ_PROXIED:
  return NSIG;
#endif // !AZ_PROXIED
}

int os::sigbreaknum_pd(){
assert(max_numsignals>0,"must call os::init");
#ifdef AZ_PROXIED
  return proxy_get_sigbreak_num();
#else // !AZ_PROXIED:
  return BREAK_SIGNAL;
#endif // !AZ_PROXIED
}

void os::signal_init_pd() {
  // Initialize signal structures
#ifdef AZ_PROXIED
  max_numsignals = proxy_get_max_numsignals();
#else // !AZ_PROXIED:
  max_numsignals = NSIG;
#endif // !AZ_PROXIED

  pending_signals = (intptr_t *)os::malloc(sizeof(intptr_t)  * (max_numsignals+1));
guarantee(pending_signals!=NULL,"pending_signals creation failed");
  memset(pending_signals, 0, (sizeof(intptr_t) * (max_numsignals+1)));

  int res = sem_init(&sig_sem, 0/*across-threads-not-across-processes*/,0/*init count*/);
guarantee(res==0,"sem_init failed");
}

/*
 * The java signal handler sits on a semaphore waiting for a signal,
 * when one arrives (from the proxy) we notify the java signal handler
 * thread by posting on the semaphore.
 */
void os::signal_notify(int signal_number){
Atomic::inc_ptr(&pending_signals[signal_number]);
  int res = sem_post(&sig_sem);
guarantee(!res,"sem_post failed");
}   

int os::signal_wait() {
  while (true) {
    for (int i = 0; i < (max_numsignals+1); i++) {
      intptr_t n = pending_signals[i];
      if (n > 0 && n == Atomic::cmpxchg(n - 1, (jlong *)&pending_signals[i], n)) {
        return i;
      }
    }

    JavaThread *thread = JavaThread::current();
    ThreadBlockInVM tbivm(thread,"semaphore wait"); // release jvm_lock (allow GC)

    int res = sem_wait(&sig_sem);
    int err = errno;
guarantee(!res,"sema_wait failed");
  }
}

// Virtual Memory

// Solaris allocates memory by pages.
int os::vm_allocation_granularity() {
assert(_vm_page_size!=-1,"must call os::init");
  return _vm_page_size;
}

char* os::reserve_memory_special(size_t bytes) {
  Unimplemented();
  return NULL;
}

char*  os::release_memory_special (char* preferred_addr, size_t bytes) {
  Unimplemented();
  return NULL;
}

// TODO: Use a consistent type for addresses in virtual memory calls.
char* os::reserve_memory(size_t bytes, char* preferred_addr, bool must_allocate_here, bool batchable, bool aliasable) {
  // This is supposed to allocate only virtual memory with azmem
  // but will also allocate physical memory on linux (or perhaps swap!)

  address_t addr = (address_t) preferred_addr;

assert((size_t)addr%os::vm_page_size()==0,"reserve_memory on page boundaries");
assert(bytes%os::vm_page_size()==0,"reserve_memory in page-sized chunks");
  size_t reserved_size = round_to(bytes, BytesPerLargePage);

if(os::use_azmem()){
    guarantee (preferred_addr != NULL && must_allocate_here == true,
"need specific addresses for reservations with azmem");

    int flags = batchable ? AZMM_BATCHABLE : 0;
    flags |= aliasable ? AZMM_ALIASABLE : 0;
    if( flags ) assert( (reserved_size % (1*G)) == 0, "batchable and aliasable memory needs to be in 1GB sizes" );
    int ret   = az_mreserve(addr, reserved_size, flags);

    if (ret < 0) {
warning("os::reserve_memory failed: %s",strerror(errno));
addr=NULL;
    }
  } else {
    if (preferred_addr != NULL && must_allocate_here) {
      void* where = mmap(addr, reserved_size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
      if (where != addr) { // Reserve failed:
        warning("os::reserve_memory failed: where 0x%llx != addr 0x%llx\n", where, addr);
        munmap(where, reserved_size);
addr=NULL;
      }

      return (char*)addr;
    }

if(preferred_addr==NULL){
      void* where = mmap(NULL, reserved_size, PROT_NONE, MAP_PRIVATE|MAP_32BIT|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
if(where==MAP_FAILED){
        // Try again, without 32-bit restriction
        where = mmap(NULL, reserved_size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
if(where==MAP_FAILED){
warning("os::reserve_memory failed: %s\n",strerror(errno));
          return NULL;
        }
      }

      return (char*)where;
    }
  }

  return (char*) addr;
}

// Reserve memory at an arbitrary address, only if that area is
// available (and not reserved for something else).

char* os::attempt_reserve_memory_at(size_t bytes, char* requested_addr, bool aliasable) {
char*addr=NULL;

  addr = reserve_memory(bytes, requested_addr, true, false, aliasable);
if(addr==NULL){
if(!os::use_azmem()){
      // If we are not using azmem, try allocating with must_allocate_here=false
      addr = reserve_memory(bytes, NULL, false, false, aliasable);
    }
  }
  return addr;
}

//  Begin an azmem batch memory operation.  Any failure is considered a fatal error.
//  Only one batched memory operation can be in progress at a time.  It is the
//  responsiblity of the upper level code to not try and start a second operation
//  while a prior batch is in progress.
void os::start_memory_batch(){
if(os::use_azmem()){
    if ( 0 != az_mbatch_start() ) {
      if ( errno == ENOMEM ) {
julong balance=os::available_memory();
tty->print_cr("C-Heap account balance: %llu",balance);
        vm_exit_out_of_memory1(0, "az_mbatch_start() failed with ENOMEM: C-Heap balance %llu", balance);
      } else {
fatal1("az_mbatch_start() failed: %s",strerror(errno));
      }
    }
  } else {
    ShouldNotReachHere();
  }
}

// To support meta-data in object references we wish to ignore certain virtual
// address bits. This routine establishes aliases between an original virtual
// address and virtual addresses where the meta-data bits are set at virtual
// memory reservation time.
// addr      - the virtual address to create aliases for
// start_bit - the location within the virtual address of the meta-data
// num_bits  - the number of meta-data bits to create aliases for
// size      - the size of the region to alias
bool os::alias_memory_reserve(char *addr, int start_bit, int num_bits, size_t size) {
intptr_t iaddr=(intptr_t)addr;
  assert( ((iaddr >> start_bit) & ((1 << num_bits)-1)) == 0, "virtual memory address overlaps meta-data bits");

if(os::use_azmem()){
    for (int i=1; i<(1<<num_bits); i++) {
      intptr_t x = (intptr_t)iaddr + ((intptr_t)i<<start_bit);
      void *adr = (void*)x;
      if (az_mreserve_alias(adr, addr) < 0) {
fatal1("os::alias_memory_reserve failed: %s",strerror(errno));
      }
    }
  }

  return true;
}

// As alias_memory_reserve but called when memory is committed
bool os::alias_memory_commit(char *addr, int start_bit, int num_bits, size_t size) {
intptr_t iaddr=(intptr_t)addr;
  assert( ((iaddr >> start_bit) & ((1 << num_bits)-1)) == 0, "virtual memory address overlaps meta-data bits");

  // if azmem - Alias established when memory was reserved
if(!os::use_azmem()){
    // Create a temp shared-memory segment
    char shm_name[256];
    snprintf(shm_name, 256, "/azul_heap-%p", addr);
    int fd = shm_open(shm_name, (O_RDWR | O_CREAT), 0);
    if (fd<=0) {
perror("creating shared memory segment for heap multi-mapping failed");
      return false;
    }
    int err2 = shm_unlink(shm_name);
    if (err2 < 0) {
perror("unlink of shared memory segment failed");
      return false;
    }
    int err0 = ftruncate64(fd, size);
    if (err0 < 0) {
perror("ftruncate failed");
      return false;
    }

    for (int i = 0; i < (1<<num_bits); i++) {
      intptr_t x = (intptr_t)iaddr + ((intptr_t)i<<start_bit);
      void *adr = (void*)x;
      void *loc = mmap(adr, size, PROT_READ | PROT_WRITE, MAP_SHARED| MAP_FIXED, fd, 0);
if(loc!=adr){
perror("mmap failed");
        return false;
      }
    }

    int err1 = close(fd);
    if (err1 < 0) {
perror("close of shared memory failed");
      return false;
    }
  }

#ifdef ASSERT
  // Validate the memory is multi-mapped
  int *adr1 = (int*)(iaddr + ( 0L<<start_bit));
  *adr1 = 17;
  for (int i = 0; i < (1<<num_bits); i++) {
    intptr_t x = (intptr_t)iaddr + ((intptr_t)i<<start_bit);
    int *adr2 = (int*)x;
    assert0( *adr2 == 17 );
  }
#endif
  return true;
}

//  Commit a azmem batch memory operation started by start_memory_batch().
//  Any failure is considered a fatal error.
void os::commit_memory_batch(){
if(os::use_azmem()){
    if (0 != az_mbatch_commit()) {
fatal1("az_mbatch_commit() failed with errno %d",errno);
    }
  } else {
    ShouldNotReachHere();
  }
}

static bool linux_mprotect(char* addr, size_t size, int prot) {
  // Linux wants the mprotect address argument to be page aligned.
char*bottom=(char*)align_size_down((intptr_t)addr,BytesPerSmallPage);

  // According to SUSv3, mprotect() should only be used with mappings 
  // established by mmap(), and mmap() always maps whole pages. Unaligned
  // 'addr' likely indicates problem in the VM (e.g. trying to change 
  // protection of malloc'ed or statically allocated memory). Check the 
  // caller if you hit this assert.
  assert(addr == bottom, "sanity check");

  size = align_size_up(pointer_delta(addr, bottom, 1) + size, BytesPerSmallPage);
  return ::mprotect(bottom, size, prot) == 0;
}

bool os::protect_memory(char* addr, size_t size) {
  return linux_mprotect(addr, size, PROT_READ);
}

bool os::unprotect_memory(char*addr,size_t size){
  Unimplemented();
  return false;
}

bool os::guard_memory(char* addr, size_t size) {
  return linux_mprotect(addr, size, PROT_NONE);
}

bool os::unguard_memory(char* addr, size_t size) {
  return linux_mprotect(addr, size, PROT_READ|PROT_WRITE|PROT_EXEC);
}

// Large page support

size_t os::large_page_size() {
  if (LargePageSizeInBytes <= 0) {
    LargePageSizeInBytes = os::vm_large_page_size();
  }
  return LargePageSizeInBytes;
}

bool os::can_commit_large_page_memory(){
  return true;
}

// Read calls from inside the vm need to perform state transitions (N/A for the Azul VM)
size_t os::read(int fd, void *buf, unsigned int nBytes) {
return hpi::read(fd,buf,nBytes);
}

// This is a hacked version of sleep to use wall clock time to
// back up semaphore_timedwait, which is not accurate on csim.
int os::sleep_jvmlock_ok(jlong millis, bool interruptible) {
  jlong limit = (jlong) max_intx / 1000000;
  OSThread* osthread = Thread::current()->osthread();

  // Keep looping until the proper delay has been completed.
jlong starttime=os::javaTimeMillis();
  jlong nowtime = starttime;
  // Compute time-to-sleep remaining.
  while( millis > (nowtime - starttime) ) {
    jlong mx = millis - (nowtime - starttime);
    mx = millis > limit ? limit : mx;

    TickProfiler::meta_tick(os_sleep_begin_tick);
    if (interruptible) {
      MutexLocker ml(*Thread::current()->_self_suspend_monitor );
      Thread::current()->_self_suspend_monitor->wait_micros(mx*1000,interruptible);
    } else {
      ::usleep(mx * 1000L); // Sleep time is in microseconds, mx in millis
    }
    TickProfiler::meta_tick(os_sleep_end_tick  );

    if( interruptible && osthread->clr_interrupted() )
      return OS_INTRPT;
nowtime=os::javaTimeMillis();
  }
  return OS_OK;
}

// Normal os::sleep: do not sleep while hold the jvmlock, as it prevents GC.
int os::sleep(Thread* thread, jlong millis, bool interruptible) {
  assert0( !thread->is_Java_thread() || !((JavaThread*)thread)->jvm_locked_by_self() );
return sleep_jvmlock_ok(millis,interruptible);
}

// Sleep forever; naked call to OS-specific sleep; use with CAUTION
void os::infinite_sleep() {
  while (true) {    // sleep forever ...
    usleep(10L * 1000000L); // 10 seconds at a time.
  }
}

// Used to convert frequent JVM_Yield() to nops
// Azul note: Use Win32 implementation.
bool os::dont_yield() {
  return DontYieldALot;
}

void os::yield() {
  sched_yield();
}


void os::yield_all(int attempts) {
  // Yields to all threads.  Since we are forcibly pausing this thread
  // for interesting periods of time, require that the caller handle
  // the jvm_lock well.
  TickProfiler::meta_tick(os_sleep_begin_tick);
  usleep(1000L);    // Sleep 1 millisecond, ignore 'attempts'
  TickProfiler::meta_tick(os_sleep_end_tick);
}


// Azul note: We have timeshare priorities between 0 and 31.
// For now we'll bunch them linearly around the central value and
// leave room at the bottom and top for other threads (in the JVM
// process or other daemons). Spacing them by 2 rather than 1 means
// that boost_priority()/deflate_priority() (below) could have an
// effect (presently they are never called).
OSThreadPriority os::java_to_os_priority[MaxPriority + 2] = {
  19,              // 0 Entry should never be used

   4,              // 1 MinPriority
   3,              // 2
   2,              // 3

   1,              // 4
   0,              // 5 NormPriority
  -1,              // 6

  -2,              // 7
  -3,              // 8
  -4,              // 9 NearMaxPriority

  -5               // 10 MaxPriority
};


OSReturn os::set_native_priority(Thread*thread,OSThreadPriority priority){
  if ( !UseThreadPriorities || ThreadPriorityPolicy == 0 ) return OS_OK;

int ret=setpriority(PRIO_PROCESS,thread->osthread()->thread_id(),priority);
  return (ret == 0) ? OS_OK : OS_ERR;
}


OSReturn os::get_native_priority(const Thread* const thread, OSThreadPriority *priority_ptr) {
  if ( !UseThreadPriorities || ThreadPriorityPolicy == 0 ) {
    *priority_ptr = java_to_os_priority[NormPriority];
    return OS_OK;
  }

  errno = 0;
  *priority_ptr = getpriority(PRIO_PROCESS, thread->osthread()->thread_id());
return(((*priority_ptr!=(OSThreadPriority)(-1))||(errno==0))?OS_OK:OS_ERR);
}


// Hint to the underlying OS that a task switch would not be good.
// Void return because it's a hint and can fail.
void os::hint_no_preempt(){
  // Azul note: Currently a no-op. We likely do not need this as we essentially
  // operate in the reverse mode - the OS will attempt to make us safepoint before 
  // scheduling us out.
}


void os::interrupt(Thread* thread) {
assert(Thread::current()==thread||Threads_lock.owned_by_self(),"possibility of dangling Thread pointer");
  OSThread* osthread = thread->osthread();

  // Set status as interrupted; test old status
if(!osthread->set_interrupted()){
    OrderAccess::fence();
    // Bump thread interruption counter
    RuntimeService::record_thread_interrupt_signaled_count();
  }

  if( !thread->is_Java_thread() )
Unimplemented();//interrupting a non-java thread?

  // For JSR166:  unpark after setting status but before thr_kill -dl
  JavaThread *jt = ((JavaThread*)thread);
  jt->parker()->unpark();
  // Break him out of a sleep as well
  { MutexLocker ml(*jt->_self_suspend_monitor );
    jt->_self_suspend_monitor->notify(); }
  // Need to post / wake-up whatever semaphore this thread is sleeping on.
  // This is a really heavyweight technique, but we assume interrupts are rare
ObjectMonitor*om=jt->current_waiting_monitor();
  if( om ) {           // Heading for a 'wait' or perhaps IN a 'wait'?
    int wnum = om->waiters();
    om->notify_impl(wnum);      // notify as many as needed
  }
}    


bool os::is_interrupted          (Thread* thread) { return thread->osthread()->    interrupted(); }
bool os::is_interrupted_and_clear(Thread* thread) { return thread->osthread()->clr_interrupted(); }

void os::print_statistics() {
}

void os::flush_memory(MemoryAccount account, size_t* flushed, size_t* allocated) {
  // TODO: No memory accounts supported yet.
  *flushed   = 0;
  *allocated = 0;
}


int os::message_box(const char* title, const char* message, const char* prompt) {
  int i;

tty->flush();

  fdStream err(defaultStream::error_fd());
err.flush();
::sleep(1);//wait around for a second to flush other output

  for (i = 0; i < 78; i++) err.print_raw("=");
  err.cr();
  err.print_raw_cr(title);
  for (i = 0; i < 78; i++) err.print_raw("-");
  err.cr();
  err.print_raw_cr(message);
  err.cr();
err.print_raw_cr(prompt);
  err.cr();
  for (i = 0; i < 78; i++) err.print_raw("=");
  err.cr();

err.flush();

  char buf[16];
  // Prevent process from exiting upon "read error" without consuming all CPU
  while (::read(0, buf, sizeof(buf)) <= 0) { ::sleep(100); }

  return buf[0] == 'y' || buf[0] == 'Y';
}



// This does not do anything on Solaris. This is basically a hook for being
// able to use structured exception handling (thread-local exception filters) on, e.g., Win32.
void os::os_exception_wrapper(java_call_t f, JavaValue* value, methodHandle* method, JavaCallArguments* args, Thread* thread) {
  f(value, method, args, thread);  
}


void report_error(const char* file_name, int line_no, const char* title, const char* format, ...);

// Used on Linux to snarf up the caller's state
#include <ucontext.h>

// We get here if we get a native exception or something unexpected
extern "C" address_t jvm_unexpected_exception_handler(int signum, intptr_t epc, intptr_t eva) {
  Thread* thread = Thread::current();

  // Non-Java threads (like transport I/O threads, proxy connection thread) crash on the
  // thread->is_Java_thread() call since they have no vtbl. Bug 4723.
if(thread->is_Complete_Java_thread()){
    JavaThread *jt = (JavaThread*)thread;
if(jt->doing_unsafe_access()){
      // Handoff to the unsafe handler
      jt->_epc = epc;
return StubRoutines::handler_for_unsafe_access();
    }
  }

  if (PrintBacktraceOnUnexpectedException) {
    os_backtrace();
  }
  VMError err(thread, signum, (address) epc, (void*) eva, NULL);
err.report_and_die();
  ShouldNotReachHere();
return NULL;//Keeps compiler happy. Or at least mildly content.
}
extern "C" address_t jvm_unexpected_exception_handler2(int dummy, siginfo_t* si, ucontext_t* uc) {
Thread*t=Thread::current();
  return jvm_unexpected_exception_handler(SIGSEGV,t->_libos_epc,t->_libos_eva);
}


// This is called when the OS (and our registered exception handler) determines the exception
// occurred in page zero. We now need to determine if this was in a Java thread or not and
// take the appropriate action.
extern "C" address_t jvm_java_null_pointer_exception_handler(int signum, intptr_t epc, intptr_t eva) {
  Thread* thread = Thread::current();
if(thread->is_Complete_Java_thread()){
    JavaThread *jt = (JavaThread*)thread;
    bool in_java = jt->jvm_locked_by_self();
    if (in_java && CodeCache::contains((void*)epc)) {
      // Handoff to the Java null pointer exception handler
      jt->_epc = epc;
      return StubRoutines::x86::handler_for_null_ptr_exception_entry();
    }
  }
  // If we're going to die anyway, see if we can generate a useful error message
  return AZUL_SIGNAL_NOT_HANDLED;
}


// If we are a Compiler Thread (should never be in the yellow zone) or if we are
// a Java Thread, and we've reached beyond our full stack and are now in the yellow 
// zone, we need to let the JVM know.  All of the mapping in of memory was handled
// for us in the libos's stack overflow logic already, including the yellow zone
// itself.  Since we now have two separate stacks (a user stack and a java expression
// stack) we check whether we've entered either one of them.  If so, we suspend the
// thread and let it generate the stack overflow exception.
//
extern "C" address_t jvm_stack_overflow_exception_handler(int signum, intptr_t epc, intptr_t fault_address) {
  Thread* thread = Thread::current();
  if( !is_in_yellow_zone((ThreadBase*)thread) ) return AZUL_SIGNAL_NOT_HANDLED;
  if( !thread->is_Complete_Java_thread() )      return AZUL_SIGNAL_NOT_HANDLED;
  JavaThread *jt = (JavaThread*)thread;
if(jt->is_Compiler_thread()){
    ciEnv::current()->record_failure("Compiler thread stack overflowed", false, false);
    return AZUL_SIGNAL_RESUME_EXECUTION;
  }
  jt->set_suspend_request(JavaThread::stack_suspend);
  return AZUL_SIGNAL_RESUME_EXECUTION;
}  // end of jvm_stack_overflow_exception_handler()


extern "C" address_t jvm_gpgc_heap_exception_handler(int signum, intptr_t epc, intptr_t fault_address, int si_code);


// --- jvm_exception_handler
// Top-level breakdown of exception types
extern "C" address_t jvm_exception_handler(int signum, siginfo_t* si, ucontext_t* uc) {
  intptr_t epc = uc->uc_mcontext.gregs[REG_RIP];
  uintptr_t eva = (intptr_t) si->si_addr;

  // GPGC page healing SEGVs, only supported with azmem.
  if (os::use_azmem() && UseGenPauselessGC && GPGC_Layout::is_shattered_address(eva)) {
    // Shattered page exception in GPGC heap:
    return jvm_gpgc_heap_exception_handler(signum, epc, eva, si->si_code);
  }

  // null-ptr base address de-ref
  if( eva < (uintptr_t)BytesPerLargePage ) // Exception in page 0?
    return jvm_java_null_pointer_exception_handler( signum, epc, eva );

  // Stack overflow check
  if( jvm_stack_overflow_exception_handler(signum,epc,eva) == AZUL_SIGNAL_RESUME_EXECUTION )
    return AZUL_SIGNAL_RESUME_EXECUTION;
  
  if ( signum==SIGSEGV) {
    if ( UseGenPauselessGC && Thread::current()->is_GC_thread() && GPGC_ReadTrapArray::addr_in_rb_array_range(eva) && !BatchedMemoryOps ) {
      // Special case handling for read barrier arrays being swapped without an atomic
      // batched memory operation:  there's a brief moment inside the safepoint where
      // there's no current barrier array mapped in.  Since we're in a safepoint, only
      // GC threads should run into a SEGV here.
      if ( GPGC_ReadTrapArray::array_remap_in_progress() ) {
        if ( GPGC_Interlock::interlock_held_by_self(GPGC_Interlock::BatchedMemoryOps) ) {
          // If we're the thread who's doing the swap operation, then bail out.
          return jvm_unexpected_exception_handler(signum, epc, eva);
        }

        // spin wait till the flag is cleared
        do {
struct timespec req;
          req.tv_sec  = 0;
          req.tv_nsec = 200 * 1000; // sleep for 200 microseconds.
          nanosleep(&req, NULL);
          Untested();
        } while ( GPGC_ReadTrapArray::array_remap_in_progress() );
      } else {
        // Remap is supposedly complete: dereference again, and it should work.  If we SEGV here
        // again, the nested SEGV will terminate the process.
        char test_byte = *((char *)eva); 
      }
      return AZUL_SIGNAL_RESUME_EXECUTION;
    }
  }


  // Return and let the chained signal handler pass this along.
  return AZUL_SIGNAL_NOT_HANDLED;

  // Originally the process did this: return jvm_unexpected_exception_handler( signum, epc, eva ); 
  // but since we've implemented a chained signal handler (especially for SIGSEGV) we want to
  // give other processes besides the JVM a chance to examine the interrupt.
}
  

// exception code is derived from estate
const char* os::exception_name(int exception_code, char* buf, size_t size) {
  int signal = exception_code;
  if (signal>0 && signal<NSIG) {
jio_snprintf(buf,size,"%s",sys_siglist[signal]);
    return buf;
  } else {
    return NULL;
  }
}


void set_malloc_abort_on_failure(int) {
  // nothing ported yet....
}


// Look up a hotspot_to_gdb_symbol entry based on a PC.
hotspot_to_gdb_symbol_t * hg_lookup (uint64_t pc)
{
  unsigned i;

  if (HotspotToGdbSymbolTable.version != 1) {
    return NULL;
  }

for(i=0;i<HotspotToGdbSymbolTable.numEntries;i++){
    hotspot_to_gdb_symbol_t *sym = &HotspotToGdbSymbolTable.symbolsAddress[i];

    if ((sym->startPC <= pc) && (pc < (sym->startPC + sym->codeBytes))) {
      return sym;
    }
  }

  return NULL;
}


// Return the name for a hotspot_to_gdb_symbol.
const char * hg_lookup_name (hotspot_to_gdb_symbol_t *sym)
{
  if (! sym->nameAddress) {
return"(Unknown)";
  }

  return sym->nameAddress;
}


// Walk the stack when os_backtrace is called.
// This uses the hotspot_to_gdb_symbol table to traverse the stack.
//
// Warning:  If the table is being modified concurrently with this
//           walk, then this operation is unsafe, and may cause the
//           program to crash.  But seeing as the program was crashing
//           anyway or you wouldn't have gotten here, some likely
//           information is better than no information.
extern "C" void hotspot_os_backtrace_callback(void)
{
  uint64_t rip;
  uint64_t rip_addr;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rbp_addr;
  int frameno;

  char labelbuf[1024];
  hotspot_to_gdb_symbol_t *hsymbol;

  /* @e want a single backtrace from a single thread. `notfirsttime` is 
   * initialized to false at compile/link time and then checked here.
   */
  static bool notfirsttime = false;
  if (notfirsttime) return;
  notfirsttime=true;

  /* Calculate first frame */
  {
    __asm__ __volatile__  (
"lea 0(%%rip), %0\n\t"
      : "=r" (rip)
      );

    rip_addr = -1;

    __asm__ __volatile__  (
"movq %%rsp, %0"
      : "=r" (rsp)
      );

    __asm__ __volatile__  (
"movq %%rbp, %0"
      : "=r" (rbp)
      );

    rbp_addr = -1;
    frameno = 0;
  }

  while (1) {
    uint64_t caller_rip;
    uint64_t caller_rip_addr;
    uint64_t caller_rsp;
    uint64_t caller_rbp;
    uint64_t caller_rbp_addr;

    /* Print current frame */
    {
      hsymbol = hg_lookup (rip);
      if (hsymbol) {
        const char *name = hg_lookup_name (hsymbol);
        snprintf(labelbuf, sizeof(labelbuf), "(%s+%ld)", name, rip - hsymbol->startPC);
      }
      else {
        size_t     range = 0;
        address_t  base  = 0;

        const char* name = find_symbol_by_address((address_t) rip, &range, &base);

if(name!=NULL){
#ifdef AZ_PROFILER
          // TODO: some day demangling should be made to work reliably, and not crash on
          // unusual mangled names.  In the mean time we comment out the buffer to keep
          // the error reporting memory requirements low.  When demangling is turned back
          // on, we need to revise yellow-zone handling to give the regular stack more
          // space, as 4KB isn't enough, and we get a SEGV in the midst of error printing.
          //
          // char demangled[1024];
          // int status = azprof::Demangler::demangle(name, demangled, sizeof(demangled));
          // int status = -1;       // demangling is unreliable or uses too much stack.
          //
          // if (status == 0) {
          //   snprintf(labelbuf, sizeof(labelbuf), "(%s+%ld)", demangled, rip - (uint64_t) base);
          // } else {
          //   snprintf(labelbuf, sizeof(labelbuf), "(%s+%ld)", name, rip - (uint64_t) base);
          // }
#endif // AZ_PROFILER
          snprintf(labelbuf, sizeof(labelbuf), "(%s+%ld)", name, rip - (uint64_t) base);
        } else {
          snprintf(labelbuf, sizeof(labelbuf), "(<unknown>)");
        }
      }
      printf ("%d: rip=0x%016lx @rip=[0x%016lx] %s", frameno, rip, rip_addr, labelbuf);
    }

    /* Calculate next frame */
    {
      if (hsymbol) {
        /* In a java frame */
        if (hsymbol->savedRBP) {
          /* Previous frame is a gcc frame */
          caller_rip_addr = rsp + hsymbol->frameSize - 8;
          caller_rip = * (uint64_t *) caller_rip_addr;
          caller_rsp = rsp + hsymbol->frameSize;
          caller_rbp_addr = rsp + hsymbol->frameSize - 16;
          caller_rbp = * (uint64_t *) caller_rbp_addr;
          //printf ("unwind type gcc (prev) calls java (curr)\n");
printf(" [gcc frame, calls java]");
        }
        else {
          /* Previous frame is a java frame */
          caller_rip_addr = rsp + hsymbol->frameSize - 8;
          caller_rip = * (uint64_t *) caller_rip_addr;
          caller_rsp = rsp + hsymbol->frameSize;
          caller_rbp_addr = 0;
          caller_rbp = 0xabc123;
          //printf ("unwind type java (prev) calls java (curr)\n");
printf(" [java frame, calls java]");
        }
      }
      else {
        /* In a gcc frame */
        caller_rip_addr = rbp + 8;
        caller_rip = * (uint64_t *) caller_rip_addr;
        caller_rsp = rbp + 16;
        caller_rbp_addr = rbp;
        caller_rbp = * (uint64_t *) caller_rbp_addr;
        //printf ("unwind type dont-care (prev) calls gcc (curr)\n");
printf(" [gcc frame, calls gcc]");
      }
      printf("\n");

      /* Check for loop termination cases */
      if (caller_rsp <= rsp) {
        break;
      }

      if (caller_rbp <= rbp) {
        if (caller_rbp_addr) {
          break;
        }
      }

      if (frameno > 500) {
printf("STOP: Too many frames!\n");
        break;
      }

      rip = caller_rip;
      rip_addr = caller_rip_addr;
      rsp = caller_rsp;
      rbp = caller_rbp;
      rbp_addr = caller_rbp_addr;

      frameno++;
    }
  }

  fflush (stdout);
  fsync (1);
}


// this is called _before_ the global arguments have been parsed
void os::init(void) {
  init_elapsed_time();
  init_random(1234567);

  ::set_backtrace_callback(hotspot_os_backtrace_callback);

Linux_OS::initialize_system_info();

main_thread=pthread_self();

  // Constant minimum stack size allowed. It must be at least
  // the minimum of what the OS supports (thr_min_stack()), and
  // enough to allow the thread to get to user bytecode execution.
  // note: Need to work out minimum stack allowed. Perhaps start with hardcoded 1MB ?

  ::set_malloc_abort_on_failure(1);

  guarantee(__sysconf(_SC_PAGESIZE) == BytesPerSmallPage, "Unexpected system page size");
}

// To install functions for atexit system call
extern "C" {
  static void perfMemory_exit_helper() {
    perfMemory_exit();
  }
}

// this is called _after_ the global arguments have been parsed
jint os::init_2(void){
//Set process thread quantum
  // azsched/linux do not support setting a thread's quantum which is used
  // for scheduling.  Because we might want to implement this in the future,
  // I'll leave the code but it will follow these rules.
  // (1) Any attempt to change/set thread quanta will succeed but will have
  //     no effect and be ignored.  This applies to both commandline options
  //     and other programmatic interfaces.
  // (2) Any attempt to read/get thread quanta will return a -1.
  //
  // if (ProcessQuantumMS >= 0) {
  //   bool set_quantum = set_thread_quantum(ProcessQuantumMS);
  //   if (!set_quantum) {
  //     warning("unable to set process thread quantum to %lld", ProcessQuantumMS);
  //   }
  // }

  // Install our exception handlers so we catch null pointer exceptions, memory stompers and stack overflows
  // We use AZUL_EXCPT_HNDLR_THIRD because we want the thread logic to install a defaultSegvHandler() before
  // this one executes, and potentially a SIGSEGV handler specific to the AZUL JVM.
  sys_return_t rc = exception_register_chained_handler(SIGSEGV, jvm_exception_handler, AZUL_EXCPT_HNDLR_THIRD);
  guarantee(rc==SYSERR_NONE, "Error registering jvm_exception_handler");
  // SIGILL handler.
  rc = exception_register_chained_handler(SIGILL, jvm_exception_handler, AZUL_EXCPT_HNDLR_FIRST);
  guarantee(rc==SYSERR_NONE, "Error registering jvm_exception_handler");
  // Null pointer exceptions currently come through as protection faults
  rc = exception_register_chained_handler(SIGBUS, jvm_exception_handler, AZUL_EXCPT_HNDLR_FIRST);
  guarantee(rc==SYSERR_NONE, "Error registering jvm_exception_handler");

  // Call back into the VM for random memory stomps.
  rc = exception_register_chained_handler(SIGSEGV, jvm_unexpected_exception_handler2, AZUL_EXCPT_HNDLR_FIFTH);
  guarantee(rc==SYSERR_NONE, "Error registering jvm_unexpected_exception_handler");

  // Azul has a per-process limit on memory, normally set by CPM. Ask azmem for it.
  // We calculate a maximum thread limit by working out how many threads would occupy half of our entire
  // committed memory with thread stacks. This is probably overkill, but for now we want to be conservative.
  NEEDS_CLEANUP;
  size_t max_address_space = os::physical_memory() / 2;
  size_t tmp_os_thread_limit = max_address_space / thread_stack_size;
  if (tmp_os_thread_limit > (size_t)INT_MAX) {
      tmp_os_thread_limit = (size_t)INT_MAX;
  }
  _os_thread_limit = (intptr_t)tmp_os_thread_limit;

  // Now ensure we have sufficient memory for the heap and threads ...
  
NEEDS_CLEANUP;//This logic is cloned from collectorPolicy.cpp
  //uint64_t initial_heap_size = align_size_up(Arguments::initial_heap_size(), BytesPerLargePage);
  uint64_t max_heap_size = align_size_up(MaxHeapSize, BytesPerLargePage);
  //tty->print_cr("initial heap size = %llu MB", (initial_heap_size / (uint64_t)M));
  //tty->print_cr("max heap size = %llu MB", (max_heap_size / (uint64_t)M));

  // 192M + MaxPermSize of slop for threads, code heap, etc., etc., 20% of slop for heap overhead.
  uint64_t slop_reqd = (192 * M) + MaxPermSize;
  uint64_t memsize_reqd = align_size_up((uint64_t)(max_heap_size * 1.2) + slop_reqd, BytesPerLargePage);
  if (memsize_reqd > os::physical_memory()) {
    warning("Total JVM memory footprint (heap size plus base 256MB footprint) may exceed committed limit of %llu MB.\n", (os::physical_memory() / (uint64_t)M));
  }
  //tty->print_cr("Set: Min %llu MB Committed %llu MB LivePeak %llu MB Max %llu MB", prl.prl_minimum / (uint64_t)M, prl.prl_committed / (uint64_t)M, prl.prl_livepeak / (uint64_t)M, prl.prl_maximum / (uint64_t)M);

  // Initialize the System.currentTimeMillis timer.  This allows a
  // very cheap & multi-core coherent implementation of Java's CTM.
  if( UseAznixSystemCTM )
    os::Linux_OS::init_System_CTM_timer();

  // at-exit methods are called in the reverse order of their registration.
  // In Solaris 7 and earlier, atexit functions are called on return from
  // main or as a result of a call to exit(3C). There can be only 32 of
  // these functions registered and atexit() does not set errno. In Solaris
  // 8 and later, there is no limit to the number of functions registered
  // and atexit() sets errno. In addition, in Solaris 8 and later, atexit
  // functions are called upon dlclose(3DL) in addition to return from main
  // and exit(3C).

  if (PerfAllowAtExitRegistration) {
    // only register atexit functions if PerfAllowAtExitRegistration is set.
    // atexit functions can be delayed until process exit time, which
    // can be problematic for embedded VM situations. Embedded VMs should
    // call DestroyJavaVM() to assure that VM resources are released.

    // note: perfMemory_exit_helper atexit function may be removed in
    // the future if the appropriate cleanup code can be added to the
    // VM_Exit VMOperation's doit method.
    if (atexit(perfMemory_exit_helper) != 0) {
      warning("os::init2 atexit(perfMemory_exit_helper) failed");
    }
  }

#ifdef AZ_PROXIED
  // TODO: need to make this work on stock linux before removing this AZ_PROXIED
SharedUserData::init();
#endif // AZ_PROXIED

  return JNI_OK;
}


// OS interface.

int os::stat(const char *path, struct stat *sbuf) {
return::stat(path,sbuf);
}

static void*malloc_check=NULL;
static int malloc_check_size = 32790;

bool os::check_heap(bool force) {
  // Azul note: This is where we could add a malloc heap check. See os_win32.cpp for an example.
  
  // At each check-point, malloc a new large object, fill with known crud.
  // Then free a prior large object (after verifying the crud).

  int size = malloc_check_size; // Mutate large object size every time
  malloc_check_size = size > 70000 ? 32790 : size+1;
  void *junk = ::malloc(size);  // Create a new large object
  *(int*)junk = size;
  for( int i=sizeof(int); i<size; i++ )
    ((char*)junk)[i] = (char)i; // Fill with crud
  // Set it as the shared global crud object
  void *junk2 = (void*)Atomic::cmpxchg_ptr((intptr_t)junk,(intptr_t*)&malloc_check,(intptr_t)malloc_check);

  // Verify old crud
  if( !junk2 ) return true;
  int size2 = *(int*)junk2;
  for( int i=sizeof(int); i<size2; i++ )
    assert0( ((char*)junk2)[i] == (char)i );
  ::free(junk2);                // Free large cruddy object
  
  return true;
}

// Is a (classpath) directory empty?
bool os::dir_is_empty(const char* path) {
  Unimplemented();
  return false;
}


// create binary file, rewriting existing file if required
int os::create_binary_file(const char* path, bool rewrite_existing) {
  int oflags = O_WRONLY | O_CREAT;
  if (!rewrite_existing) {
    oflags |= O_EXCL;
  }
return::open(path,oflags,S_IREAD|S_IWRITE);
}

// return current position of file pointer
jlong os::current_file_offset(int fd) {
return(jlong)::lseek(fd,0,SEEK_CUR);
}

// move file pointer to the specified offset
jlong os::seek_to_file_offset(int fd, jlong offset) {
return(jlong)::lseek(fd,offset,SEEK_SET);
}


// Map a block of memory.
char* os::map_memory(int fd, const char* file_name, size_t file_offset,
                     char *addr, size_t bytes, bool read_only,
                     bool allow_exec) {
  Unimplemented();
  return NULL;
}


// Unmap a block of memory.
bool os::unmap_memory(char* addr, size_t bytes) {
    Unimplemented();
    return false;
}

static clockid_t thread_cpu_clockid(Thread* thread) {
  pthread_t tid = thread->osthread()->pthread_id();
  clockid_t clockid;

  // Get thread clockid
int rc=pthread_getcpuclockid(tid,&clockid);
  assert(rc == 0, "pthread_getcpuclockid is expected to return 0 code");
  return clockid;
}

// This is the fastest way to get thread cpu time on Linux.
// Returns cpu time (user+sys) for any thread, not only for current.
// POSIX compliant clocks are implemented in the kernels 2.6.16+.
// It might work on 2.6.10+ with a special kernel/glibc patch.
// For reference, please, see IEEE Std 1003.1-2004:
//   http://www.unix.org/single_unix_specification
#define SEC_IN_NANOSECS  1000000000LL
static jlong thread_clock_cpu_time(clockid_t clockid) {
  struct timespec tp;
  int rc = ::clock_gettime(clockid, &tp);
  assert(rc == 0, "clock_gettime is expected to return 0 code");

  return (tp.tv_sec * SEC_IN_NANOSECS) + tp.tv_nsec;
}

#define SEC_IN_MILLISECS  1000LL
#define NANOSECS_IN_MILLISECS 1000000LL
static jlong thread_clock_cpu_time_millis(clockid_t clockid) {
  struct timespec tp;
  int rc = ::clock_gettime(clockid, &tp);
  assert(rc == 0, "clock_gettime is expected to return 0 code");

  return (tp.tv_sec * SEC_IN_MILLISECS) + (tp.tv_nsec / NANOSECS_IN_MILLISECS);
}

// JVMTI & JVM monitoring and management support
// The thread_cpu_time() and current_thread_cpu_time() are only
// supported if is_thread_cpu_time_supported() returns true.

// current_thread_cpu_time(bool) and thread_cpu_time(Thread*, bool) 
// are used by JVM M&M and JVMTI to get user+sys or user CPU time
// of a thread.
//
// current_thread_cpu_time() and thread_cpu_time(Thread*) returns
// the fast estimate available on the platform.
//
// we can use the system call thread_get_statistics to get
// the user time and system time for a thread.  If it is the current
// thread then we can get more accurate information by reading from
// the performance counter directly.
jlong os::current_thread_cpu_time() {
  return thread_clock_cpu_time(CLOCK_THREAD_CPUTIME_ID);
}

jlong os::thread_cpu_time(Thread* thread) {
  // consistent with what current_thread_cpu_time() returns
return thread_clock_cpu_time(thread_cpu_clockid(thread));
}

jlong os::thread_cpu_time_millis(Thread*thread){
  return thread_clock_cpu_time_millis(thread_cpu_clockid(thread));
}

jlong os::current_thread_cpu_time(bool user_sys_cpu_time) {
  if (user_sys_cpu_time && UseLinuxPosixThreadCPUClocks) {
    return current_thread_cpu_time();
  } else {
    // this slow path relies on /proc FS to populate [user+sys] thread CPU time
    // or only [user] thread CPU time. i.e. depending on user_sys_cpu_time.
    return (jlong)slow_thread_cpu_time(Thread::current()->osthread()->thread_id(),
                                       sysconf_clock_tics_per_sec,
                                       user_sys_cpu_time);
  }
}

jlong os::thread_cpu_time(Thread *thread, bool user_sys_cpu_time) {
  if (user_sys_cpu_time && UseLinuxPosixThreadCPUClocks) {
return thread_cpu_time(thread);
  } else {
    // this slow path relies on /proc FS to populate [user+sys] thread CPU time
    // or only [user] thread CPU time. i.e. depending on user_sys_cpu_time.
    return (jlong)slow_thread_cpu_time(thread->osthread()->thread_id(),
                                       sysconf_clock_tics_per_sec,
                                       user_sys_cpu_time);
  }
}

void os::current_thread_cpu_time_info(jvmtiTimerInfo *info_ptr) {
  info_ptr->max_value = ALL_64_BITS;       // will not wrap in less than 64 bits
  info_ptr->may_skip_backward = false;     // elapsed time not wall time
  info_ptr->may_skip_forward = false;      // elapsed time not wall time
info_ptr->kind=JVMTI_TIMER_USER_CPU;//only user time is returned
}

void os::thread_cpu_time_info(jvmtiTimerInfo *info_ptr) {
  info_ptr->max_value = ALL_64_BITS;       // will not wrap in less than 64 bits
  info_ptr->may_skip_backward = false;     // elapsed time not wall time
  info_ptr->may_skip_forward = false;      // elapsed time not wall time
info_ptr->kind=JVMTI_TIMER_USER_CPU;//only user time is returned
}

bool os::is_thread_cpu_time_supported() {
  return true;
}


// --- system_ctm_tick -------------------------------------------------------
// Tick the _sys_ctm clock in a high-frequency interrupt.
static void *his_high_tickiness(void *dummy) {
  int histo[10];
  memset(histo,0,sizeof(histo));
  while( 1 ) {
    jlong millis;
#if defined AZ_PROXIED
    // proxy_time_millis() doesn't touch the proxy in-thread, so we use it if possible.
    // It isn't possible to use the ioprofiler early in program execution - in these cases,
    // we execute proxy_gettimeofday() in-thread.
    millis = proxy_time_millis();
    if( !millis ) {
      // Too early, so go out to proxy
proxy_timeval t;
      const int rc = proxy_gettimeofday(&t, NULL);
      assert0( !rc );
      millis = jlong(t.tv_sec) * 1000  +  jlong(t.tv_usec) / 1000;
    }
#else // !AZ_PROXIED:
    timeval time;
    __gettimeofday(&time, NULL);
    millis = jlong(time.tv_sec) * 1000  +  jlong(time.tv_usec / 1000);
#endif // !AZ_PROXIED
    //int delta = millis - os::_sys_ctm;
    os::_sys_ctm = millis;
    //if( delta < -3 ) delta = -2;
    //if( delta > 7 ) delta = 7;
    //histo[delta+2]++;
    //if( (histo[3]&2047)==0 ) {
    //  for( int i=0; i<10; i++ )
    //    if( histo[i] )
    //      printf("%dmsec=%d ",i-2,histo[i]);
    //  printf("\n");
    //}
    
    // Sleep for a while.  In theory we could sleep for exactly 1 msec, but
    // Nyquist sampling errors will creep in.  Sleep at 1/2 msec so that the
    // _sys_ctm variable is set proper twice per msec.
struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 500000; // 1/2 million nanos or 0.5 millisec
    nanosleep(&ts,NULL);
  }
  return 0;
}

// --- javaTimeMillis --------------------------------------------------------
// Must return millis since Jan 1 1970 for JVM_CurrentTimeMillis
jlong os::javaTimeMillis() {  
  if( UseAznixSystemCTM )
return os::_sys_ctm;
  // The default slow way.
  timeval time;
  const int rc = __gettimeofday(&time, NULL);
  const long msec = jlong(time.tv_sec) * 1000  +  jlong(time.tv_usec / 1000);
  return msec;
}

// --- init_System_CTM_timer -------------------------------------------------
// Initialize the System.currentTimeMillis timer.  This allows a
// very cheap & multi-core coherent implementation of Java's CTM.
static pthread_t the_sys_ctm_thread=0;
void os::Linux_OS::init_System_CTM_timer() {
  assert0( UseAznixSystemCTM );
  assert0( the_sys_ctm_thread==0 ); // init only once

  pthread_attr_t attr;
  const int res1 = pthread_attr_init(&attr);
  const int err1 = errno;
guarantee(!res1,"failed to make the Sys.CTM thread attr");

  const int res0 = pthread_attr_setstacksize(&attr,PTHREAD_STACK_MIN);
  const int err0 = errno;
guarantee(!res0,"failed to make the Sys.CTM thread attr setstacksize");

  const int res3 = pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  const int err3 = errno;
guarantee(!res3,"failed to make the Sys.CTM thread attr setdetachstate");


  const int res2 = pthread_create(&the_sys_ctm_thread, &attr, his_high_tickiness, NULL);
  const int err2 = errno;
guarantee(!res2,"failed to start the Sys.CTM thread");

  pthread_attr_destroy(&attr);

  int mxs = sched_get_priority_max(SCHED_FIFO);
  int mns = sched_get_priority_min(SCHED_FIFO);

  sched_param sparam;
  sparam.sched_priority = mns;
  const int res6 = pthread_setschedparam(the_sys_ctm_thread,SCHED_FIFO, &sparam);
  if( res6 == EPERM ) {
    // With a low priority thread, time will wobble.
    //warning("Unable to set a real-time priority for the timer thread; System.currentTimeMillis time will be wobbly.");
  } else {
    const int err6 = errno;
guarantee(!res6,"failed to set the priority of the Sys.CTM thread");
  }
  // Drop any setuid of root after launching a high-priority thread.
  const int res7 = setuid(getuid());
  const int err7 = errno;
guarantee(!res7,"Unable to drop setuid-root priviledges");
}

int os::loadavg(double loadavg[], int nelem) {
    return ::getloadavg(loadavg, nelem);
}

extern "C" {

int
fork1() {
    return fork();
}
}

//---------------------------------------------------------------------------------
static address same_page(address x, address y) {
  // Code is always in ?? size pages for x86.
  intptr_t page_bits = -(BytesPerLargePage);
  if ((intptr_t(x) & page_bits) == (intptr_t(y) & page_bits))
    return x;
  else if (x > y)
    return (address)(intptr_t(y) | ~page_bits) + 1;
  else
    return (address)(intptr_t(y) & page_bits);
}

bool os::find(address addr, outputStream *os) {
address sym_addr;
  size_t sym_size;
  const char *sym_name;

  sym_name = find_symbol_by_address(addr, &sym_size, (address_t*)&sym_addr);

  if (sym_name) {
    os->print("0x%016lx: ", (long unsigned int)addr);
    os->print("%s+%p", sym_name, addr - (intptr_t)sym_addr);
os->cr();

    if (Verbose) {
      // decode some bytes around the PC
      address begin  = same_page(addr-40, addr);
      address end    = same_page(addr+40, addr);
      address lowest = sym_addr;
      if (!lowest) lowest = (address) addr;
      if (begin < lowest) begin = lowest;
if(sym_addr!=NULL){
        if (end < (sym_addr + sym_size)) end = sym_addr + sym_size;
      }
      Disassembler::decode(begin, end);
    }
    return true;
  }
  return false;
}

static void get_memory_accounts(account_info_t* acct_info) {
  assert(os::use_azmem(), "Memory accounts are available only with azmem");

  unsigned long a_size = sizeof(*acct_info);
  memset(acct_info, 0, a_size);

  az_pmem_get_account_stats(os::Linux_OS::this_process_specifier(), acct_info, &a_size);
}

void os::memory_account_get_stats(MemoryAccount account,
                int64_t* balance,   int64_t* balance_min,   int64_t* balance_max,
                size_t*  allocated, size_t*  allocated_min, size_t*  allocated_max,
                size_t*  maximum) {
if(os::use_azmem()){
    uint64_t acct = account;
    account_info_t acct_info;

    get_memory_accounts((account_info_t*) &acct_info);

    guarantee1(acct_info.ac_count > acct,
"az_pmem_get_account_stats doesn't have entry for account %d",
               account);

    *balance       = acct_info.ac_array[acct].ac_balance;
    *balance_min   = acct_info.ac_array[acct].ac_balance_min;
    *balance_max   = acct_info.ac_array[acct].ac_balance_max;
    *allocated     = acct_info.ac_array[acct].ac_allocated;
    *allocated_min = acct_info.ac_array[acct].ac_allocated_min;
    *allocated_max = acct_info.ac_array[acct].ac_allocated_max;
    *maximum       = acct_info.ac_array[acct].ac_maximum;
  } else {
    // get total memory
    *maximum       = os::physical_memory();

    if (account == os::JAVA_PAUSE_MEMORY_ACCOUNT) {
      *balance       = 0;
      *balance_min   = 0;
      *balance_max   = 0;
      *allocated     = 0;
      *allocated_min = 0;
      *allocated_max = 0;
    } else {
      // get available memory
      // values in struct sysinfo are "unsigned long"
      struct sysinfo si;
      int ret = sysinfo(&si);
      if (ret == 0) {
        *balance   = (int64_t) si.freeram * si.mem_unit;
      } else {
        *balance = -1;
      }

      // allocated memory
      *allocated = *maximum - *balance;

      // no watermarks on stock linux, set to current value:
      *balance_min   = *balance;
      *balance_max   = *balance;
      *allocated_min = *allocated;
      *allocated_max = *allocated;
    }
  }

  return;
}


void os::memory_account_reset_watermarks(MemoryAccount account) {
  // Only azmem has account watermarks
if(os::use_azmem()){
    assert(account==JAVA_COMMITTED_MEMORY_ACCOUNT || account==JAVA_PAUSE_MEMORY_ACCOUNT,
"invalid memory account");
    if (0 != az_pmem_reset_account_watermarks(account)) {
      fatal2("az_pmem_reset_account_watermarks(%d) failed with errno %d", account, errno);
    }
  }
}


// Fund the accounts according to the Java heap size.
void os::fund_memory_accounts(size_t java_committed) {
  // This is a NOP for !azmem,
  // since stock Linux doesn't have azmem style memory accounts.
if(os::use_azmem()){
    int64_t balance         = 0;
    int64_t balance_min     = 0;
    int64_t balance_max     = 0;
    size_t  allocated       = 0;
    size_t  allocated_min   = 0;
    size_t  allocated_max   = 0;
    size_t  flushed         = 0;
    size_t  total_committed = 0;
    size_t  total_maximum   = 0;
    size_t  java_maximum    = 0;
    size_t  cheap_maximum   = 0;
    size_t  pause_maximum   = 0;

    // Get the current CHEAP_COMMITTED paramaters, and calculate the new ones needed for two committed accounts.
    os::memory_account_get_stats(os::CHEAP_COMMITTED_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max,
                                 &allocated, &allocated_min, &allocated_max,
                                 &total_maximum);

    total_committed = balance + allocated;

    if (UseGenPauselessGC ) {
      if ((int64_t)java_committed >= balance) {
vm_exit_during_initialization("Java heap committed is bigger than remaining committed balance");
      }

      // Calculate the memory account maximums to use,
      // keeping in mind GPGCOverflowMemory and GPGCPausePreventionMemory.
      if (GPGCOverflowMemory == 0) {
        java_maximum = java_committed;
      } else {
        java_maximum = (double(java_committed) / double(total_committed)) * total_maximum;
        if (GPGCOverflowMemory > 0) {
          java_maximum = MIN2(java_maximum, size_t(java_committed + GPGCOverflowMemory));
        }
      }
      java_maximum = align_size_down(java_maximum, BytesPerGPGCPage);
      java_maximum = MAX2(size_t(java_committed), java_maximum);

      if (GPGCPausePreventionMemory == 0) {
        pause_maximum = 0;
      } else {
        if (GPGCPausePreventionMemory > 0) {
          pause_maximum = GPGCPausePreventionMemory;
        } else {
          pause_maximum = 256L * G;
        }
      }
      pause_maximum = align_size_down(pause_maximum, BytesPerGPGCPage);

      cheap_maximum = total_maximum - java_maximum;

    } else {
      java_maximum = java_committed;
      cheap_maximum = total_maximum - java_maximum;
      pause_maximum = 0;
    }

    // Fund the memory account for the java heap.
    // TODO: maw: is there a race here, where someone else could steal our committed memory?
    if ( ! os::memory_account_transfer(os::JAVA_COMMITTED_MEMORY_ACCOUNT, os::CHEAP_COMMITTED_MEMORY_ACCOUNT, 1*java_committed) ) {
vm_exit_during_initialization("Memory management: unable to transfer from c-heap memory account to java-heap memory account");
    }

    // Set the account maximums for the memory accounts.
    if ( ! os::memory_account_set_maximum(os::CHEAP_COMMITTED_MEMORY_ACCOUNT, cheap_maximum) ) {
vm_exit_during_initialization("Memory management: unable to set c-heap committed account maximum");
    }
    if ( ! os::memory_account_set_maximum(os::JAVA_COMMITTED_MEMORY_ACCOUNT, java_maximum) ) {
vm_exit_during_initialization("Memory management: unable to set java-heap committed account maximum");
    }
    if ( ! os::memory_account_set_maximum(os::JAVA_PAUSE_MEMORY_ACCOUNT, pause_maximum) ) {
vm_exit_during_initialization("Memory management: unable to set java pause account maximum");
    }

    // TODO: No memory account flushing support yet for azmem, don't know what it looks like yet.
    Linux_OS::_flush_overdraft_only_supported = true;
  }
}


void os::az_mmap_print_on(outputStream*st){
print_memory_info(st);

  intptr_t total_balance = 0;
for(int i=0;i<Modules::MAX_MODULE;i++){
if(_az_mmap_peak[i]>0){
      st->print_cr("C-Heap usage for module #%02d %s: %ld KB (peak %ld KB)", i, Modules::name(i), _az_mmap_balance[i]/K, _az_mmap_peak[i]/K);
      total_balance += _az_mmap_balance[i];
    }
  }
st->print_cr("Total current module consumption: %ld KB",total_balance/K);
st->flush();
}


const char* os::memory_account_name(MemoryAccount account) {
  switch (account) {
case os::CHEAP_COMMITTED_MEMORY_ACCOUNT:
return"VM internal";
case os::JAVA_COMMITTED_MEMORY_ACCOUNT:
return"Java heap";
case os::JAVA_PAUSE_MEMORY_ACCOUNT:
return"Java pause prevention";
    default:
      return "unknown";
  }
}

static const char* memory_fund_name(int fund) {
const char*name="default";

if(os::use_azmem()){
    name = azmm_get_fund_name(fund);
  }

  return name;
}

static void print_memory_fund_xml_on(xmlBuffer *xb, int fund) {
  xmlElement xe(xb, "memory_fund");
  xb->name_value_item("id", fund);
  xb->name_value_item("name", memory_fund_name(fund));
}

void os::print_memory_statistics_xml_on(xmlBuffer *xb) {
  xmlElement xe(xb, "memory_stats");

if(os::use_azmem()){
account_info_t info;
    get_memory_accounts((account_info_t*) &info);
   
    uint64_t n = info.ac_count;
    bool using_grant = false;
  
    for (uint64_t i = 0; i < n; i++) {
      int64_t grant = 0;
      xmlElement xe(xb, "memory_account");
      xb->name_value_item("id", i);
      xb->name_value_item("name", os::memory_account_name((MemoryAccount) i));
      xb->name_value_item("allocated", info.ac_array[i].ac_allocated);
      xb->name_value_item("allocated_min", info.ac_array[i].ac_allocated_min);
      xb->name_value_item("allocated_max", info.ac_array[i].ac_allocated_max);
      xb->name_value_item("balance", info.ac_array[i].ac_balance);
      xb->name_value_item("balance_min", info.ac_array[i].ac_balance_min);
      xb->name_value_item("balance_max", info.ac_array[i].ac_balance_max);
      xb->name_value_item("maximum", info.ac_array[i].ac_maximum);
      // Some user friendly numbers
      xb->name_value_item("committed", info.ac_array[i].ac_allocated + info.ac_array[i].ac_balance);
      if ((info.ac_array[i].ac_overdraft_fund == AZMM_OVERDRAFT_FUND) && (info.ac_array[i].ac_balance < 0)) {
        grant = -info.ac_array[i].ac_balance;
      }
      xb->name_value_item("grant", grant);
      { xmlElement xe(xb, "fund");
        print_memory_fund_xml_on(xb, info.ac_array[i].ac_fund);
      }
      { xmlElement xe(xb, "overdraft_fund");
        print_memory_fund_xml_on(xb, info.ac_array[i].ac_overdraft_fund);
      }
    
      using_grant = using_grant || (grant > 0);
    }
  
    xb->name_value_item("using_grant", using_grant ? "true" : "false");
  }
}

// returns true if available memory is less than mem_threshold (specified in bytes)
// default for mem_threshold is 100 * M
bool os::process_low_on_memory(int mem_threshold) {
  int64_t balance       = 0;
  int64_t balance_min   = 0;
  int64_t balance_max   = 0;
  size_t  allocated     = 0;
  size_t  allocated_min = 0;
  size_t  allocated_max = 0;
  size_t  maximum       = 0;

  memory_account_get_stats(CHEAP_COMMITTED_MEMORY_ACCOUNT,
                           &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

  if (balance < mem_threshold) {
    return true;
  }
  return false;
}


void os::Linux_OS::exit_out_of_memory() {
  ::exit_out_of_memory();
}

void os::Linux_OS::set_abort_on_out_of_memory(bool b) {
  ::set_abort_on_out_of_memory(b);
}

bool os::Linux_OS::is_sig_ignored(jint sig) { return false; }


// NUMA-specific interface.  Copied from x86/linux.
void os::realign_memory(char *addr, size_t bytes, size_t alignment_hint) { }
void os::free_memory(char *addr, size_t bytes)         { }
void os::numa_make_global(char *addr, size_t bytes)    { }
void os::numa_make_local(char *addr, size_t bytes)     { }
bool os::numa_topology_changed()                       { return false; }
size_t os::numa_get_groups_num()                       { return 1; }
int os::numa_get_group_id()                            { return 0; }
size_t os::numa_get_leaf_groups(int *ids, size_t size) {
  if (size > 0) {
    ids[0] = 0;
    return 1;
  }
  return 0;
}

bool os::get_page_info(char *start, page_info* info) {
  return false;
}

char *os::scan_pages(char *start, char* end, page_info* page_expected, page_info* page_found) {
  return end;
}

int os::fork_and_exec(char* cmd) {
  Unimplemented();
  return 0;
}

int os::getnopenfds(uint32_t* nopenfds) {
#ifdef AZ_PROXIED
  if (proxy_getnopenfds(nopenfds) == 0) {
    return 0;
  }
  return -1;
#else // !AZ_PROXIED
  char fd_list_path[64];
  DIR *dirp;
struct dirent*entry;
  
  pid_t pid = getpid();

  snprintf(fd_list_path, sizeof(fd_list_path), "/proc/%d/fd", pid);
  dirp = opendir(fd_list_path);
if(dirp==NULL){
    return -1;
  }
  *nopenfds = 0;
  while (1) {
    errno = 0;
    entry = ::readdir(dirp);
if(entry==NULL){
      if (errno != 0) {
closedir(dirp);
        return -1;
      } else {
        break;
      }
    } else {
      if (entry->d_name[0] != '.') { /* skip "." and ".." */
        ++(*nopenfds);
      }
    }
  }
  if (closedir(dirp)) {
    return -1;
  }
  --(*nopenfds); /* opendir opens an FD so subtract it */
  return 0;
#endif // !AZ_PROXIED
}
// end of file os_linux.cpp
