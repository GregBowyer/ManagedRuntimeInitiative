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


#include "allocation.hpp"
#include "arguments.hpp"
#include "bytecodeHistogram.hpp"
#include "c1_Compilation.hpp"
#include "c1_Runtime1.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "compilationPolicy.hpp"
#include "compileBroker.hpp"
#include "compiledIC.hpp"
#include "deoptimization.hpp"
#include "globals.hpp"
#include "growableArray.hpp"
#include "histogram.hpp"
#include "hpi.hpp"
#include "init.hpp"
#include "interpreter.hpp"
#include "java.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "loopnode.hpp"
#include "memprofiler.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "parse.hpp"
#include "phaseX.hpp"
#include "psAdaptiveSizePolicy.hpp"
#include "regalloc.hpp"
#include "sharedRuntime.hpp"
#include "statSampler.hpp"
#include "statistics.hpp"
#include "symbolTable.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

#ifndef PRODUCT

void print_bytecode_count() {
  if (CountBytecodes || TraceBytecodes || StopInterpreterAt) {
tty->print_cr("[BytecodeCounter::counter_value = %lld]",BytecodeCounter::counter_value());
  }
}

AllocStats alloc_stats;



// General statistics printing (profiling ...)

// ------------------------------------------------------------------
// ------------- memcpy profiling --------------
/*
extern "C" {
int memcpy_size_histogram[128+32];
int dst_align[32];
int src_or_dst_align[32];
int src_xor_dst_align[32];
int src_or_dst_or_len_align[32];
jlong memcpy_misalign_bytes;
static void print_memcpy_size_histo(int size, int cnt, jlong total_bytes, jlong invokes) {
  if( cnt==0 ) return;
  char buf[40];
  sprintf(buf,"%d",size);
  int len = strlen(buf);
  int pad = (len<10) ? (10-len) : 0;
  jlong x = (jlong)size*(jlong)cnt;
  printf("%d x %*d = %10lld, %5.2f%% of bytes, %5.2f%% of invokes\n",size,pad,cnt,x,
         (double)  x*100.0/(double)total_bytes,
         (double)cnt*100.0/(double)invokes
         );
}
static jlong print_align( int cnt, jlong cum, jlong invokes ) {
  if( cnt ) printf(" %8d %6.2f%%",cnt, (double)cum*100.0/invokes);
  else      printf("                %%");
  cum -= cnt;
  if( cum < 0 ) cum = 0;
  return cum;
}
}
*/
// ------------------------------------------------------------------

void print_statistics() {
  
#ifdef ASSERT

  if (CountJNICalls) {
    extern Histogram *JNIHistogram;
JNIHistogram->print(tty);
  }

  if (CountJVMCalls) {
    extern Histogram *JVMHistogram;
JVMHistogram->print(tty);
  }

#endif

  Statistics::stats_print();
  
  if (MemProfiling) {
    MemProfiler::disengage();
  }

  if (CITime) {
CompileBroker::_c1.print_times();
    CompileBroker::_c2.print_times();
  }
  if( PrintStatistics ) {
    SharedRuntime::print_statistics();
CompiledIC::print_statistics();
    Deoptimization::print_statistics();
    if (UseC1) Runtime1::print_statistics();
    if (UseC2) {
Parse::print_statistics();
PhaseCCP::print_statistics();
PhaseRegAlloc::print_statistics();
PhasePeephole::print_statistics();
PhaseIdealLoop::print_statistics();
      if (TimeLivenessAnalysis) MethodLiveness::print_times();
      if (TimeCompiler) Compile::print_timers();
    }
    os::print_statistics();
if(TimeCompilationPolicy)CompilationPolicy::print_time();
#ifndef PRODUCT
    if (LogCompilerOutput) {
      if (UseC1) tty->print_cr("C1 Log Output Size: %ld", Compilation::_c1outputsize);
      if (UseC2) tty->print_cr("C2 Log Output Size: %ld", Compile::_c2outputsize);
    }
#endif
  }
  if (ProfilerCheckIntervals) {
    PeriodicTask::print_intervals();
  }
  if( ProfileMMU ) {
    { 
      MutexLockerAllowGC mu(Threads_lock, JavaThread::current());
      for( JavaThread* X = Threads::first(); X; X = X->next() )
        if( X->mmu() ) X->mmu()->fold_into_global();
    }
    MMU::print(NULL);
  }
  if (PrintSymbolTableSizeHistogram) {
    SymbolTable::print_histogram();
  }
  if (CountBytecodes || TraceBytecodes || StopInterpreterAt) {
    BytecodeCounter::print();
  }  
  if (PrintCodeCache) {
    assert0(Thread::current()->is_Java_thread() && ((JavaThread*)Thread::current())->jvm_locked_by_self());
    CodeCache::print();
    CodeCache::print_internals();
  }

  if (PrintLockContentionAtExit) {
    MutexLocker::print_lock_contention(NULL);
    AzLock::print_lock_hold_times(NULL);
  }

  if (PrintClassStatistics) {
    SystemDictionary::print_class_statistics();
  }
  if (PrintMethodStatistics) {
    SystemDictionary::print_method_statistics();
  }

  print_bytecode_count();

  if (PrintSystemDictionaryAtExit) {
    SystemDictionary::print();
  }

//---------------------------------
/* Turn on for memcpy profiling.  
   See matching code in //azul/main-dev/gnu/newlib-1.11.0/newlib/libc/machine/azul/memcpy.S
  printf("memcpy Size histogram\n");
  jlong total_bytes = 0;
  for( int i=0; i<128; i++ )
    total_bytes += (jlong)i*(jlong)memcpy_size_histogram[i];
  for( int i=7; i<32; i++ )
    total_bytes += (1LL<<i)*(jlong)memcpy_size_histogram[128+i-7];
  jlong invokes = 0;
  for( int i=0; i<sizeof(memcpy_size_histogram)/sizeof(memcpy_size_histogram[0]); i++ )
    invokes += memcpy_size_histogram[i];
  printf("bytes   cnt\n");
  for( int i=0; i<128; i++ )
    print_memcpy_size_histo(i,memcpy_size_histogram[i],total_bytes, invokes);
  for( int i=7; i<32; i++ )
    print_memcpy_size_histo(1L<<i,memcpy_size_histogram[128+(i-7)],total_bytes, invokes);
  printf("\n");
  fflush(stdout);

  printf("memcpy Alignment histogram\n");
  printf("alignment       dst            src|dst          src^dst       src|dst|len\n");
  jlong cum_dst=invokes, cum_src_dst=invokes, cum_src_xor=invokes, cum_src_len=invokes;
  for( int i=0; i<32; i++ ) {
    if( dst_align[i] ||
        src_or_dst_align[i]  ||
        src_xor_dst_align[i]  ||
        src_or_dst_or_len_align[i] ) {
      printf("%8d",1L<<i);
      cum_dst     = print_align(               dst_align[i], cum_dst    , invokes );
      cum_src_dst = print_align(        src_or_dst_align[i], cum_src_dst, invokes );
      cum_src_xor = print_align(       src_xor_dst_align[i], cum_src_xor, invokes );
      cum_src_len = print_align( src_or_dst_or_len_align[i], cum_src_len, invokes );
      printf("\n");
    }
  }
  printf("total word-misaligned bytes moved: %lld  (%5.2f%%)", 
         memcpy_misalign_bytes, (double)memcpy_misalign_bytes/(double)total_bytes);
  printf("\n");
*/
  fflush(stdout);
}

#else // PRODUCT MODE STATISTICS

void print_statistics() {

  if (CITime) {
CompileBroker::_c1.print_times();
    CompileBroker::_c2.print_times();
  }

  if( ProfileMMU ) {
    MutexLockerAllowGC mu(Threads_lock, JavaThread::current());
    for( JavaThread* X = Threads::first(); X; X = X->next() )
      if( X->mmu() ) X->mmu()->fold_into_global();
    MMU::print(NULL);
  }

  if (PrintLockContentionAtExit) {
    MutexLocker::print_lock_contention(NULL);
    AzLock::print_lock_hold_times(NULL);
  }

}

#endif


// Helper class for registering on_exit calls through JVM_OnExit

extern "C" {
    typedef void (*__exit_proc)(void);
}

class ExitProc : public CHeapObj {
 private:
  __exit_proc _proc;
  // void (*_proc)(void);
  ExitProc* _next;
 public:
  // ExitProc(void (*proc)(void)) {
  ExitProc(__exit_proc proc) {
    _proc = proc;
    _next = NULL;
  }
  void evaluate()               { _proc(); }
  ExitProc* next() const        { return _next; }
  void set_next(ExitProc* next) { _next = next; }
};


// Linked list of registered on_exit procedures

static ExitProc* exit_procs = NULL;


extern "C" {
  void register_on_exit_function(void (*func)(void)) {
    ExitProc *entry = new ExitProc(func);
    // Classic vm does not throw an exception in case the allocation failed, 
    if (entry != NULL) {
      entry->set_next(exit_procs);
      exit_procs = entry;
    }
  }
}

extern int64_t _eventhorizon_stack_leak_counter;

// Note: before_exit() can be executed only once, if more than one threads
//       are trying to shutdown the VM at the same time, only one thread
//       can run before_exit() and all other threads must wait.
void before_exit(JavaThread * thread) {
  #define BEFORE_EXIT_NOT_RUN 0
  #define BEFORE_EXIT_RUNNING 1
  #define BEFORE_EXIT_DONE    2
  static jint volatile _before_exit_status = BEFORE_EXIT_NOT_RUN;

  // Note: don't use a AzLock to guard the entire before_exit(), as
  // JVMTI post_thread_end_event and post_vm_death_event will run native code. 
  // A CAS or OSMutex would work just fine but then we need to manipulate 
  // thread state for Safepoint. Here we use Monitor wait() and notify_all() 
  // for synchronization.
  { MutexLocker ml(BeforeExit_lock);
    switch (_before_exit_status) {
    case BEFORE_EXIT_NOT_RUN:
      _before_exit_status = BEFORE_EXIT_RUNNING;
      break;
    case BEFORE_EXIT_RUNNING:
      while (_before_exit_status == BEFORE_EXIT_RUNNING) {
BeforeExit_lock.wait();
      }
      assert(_before_exit_status == BEFORE_EXIT_DONE, "invalid state");
      return;
    case BEFORE_EXIT_DONE:
      return;
    }
  }

  // The only difference between this and Win32's _onexit procs is that 
  // this version is invoked before any threads get killed.
  ExitProc* current = exit_procs;
  while (current != NULL) {
    ExitProc* next = current->next();
    current->evaluate();
    delete current;
    current = next;
  }
 
  // Hang forever on exit if we're reporting an error.
  if (ShowMessageBoxOnError && is_error_reported()) {
    os::infinite_sleep();
  }

  // Terminate watcher thread - must before disenrolling any periodic task
  WatcherThread::stop();

  if (UseITR) {
    InstructionTraceThreads::stopAllThreads();
  }

  if (PrintProfileAtExit) {
TickProfiler::print();
  }

  // shut down the StatSampler task
  StatSampler::disengage();
  StatSampler::destroy();

  // shut down the TimeMillisUpdateTask
  if (CacheTimeMillis) {
    TimeMillisUpdateTask::disengage();
  }

  // Print GC/heap related information.
  if (PrintGCDetails) {
    Universe::print();
    AdaptiveSizePolicyOutput(0);
  }

  if (JvmtiExport::should_post_thread_life()) {
    JvmtiExport::post_thread_end(thread);
  }
  // Always call even when there are not JVMTI environments yet, since environments
  // may be attached late and JVMTI must track phases of VM execution
  JvmtiExport::post_vm_death();

  // Terminate the signal thread
  // Note: we don't wait until it actually dies.
  os::terminate_signal_thread();

  print_statistics();
  if (UseSBA && PrintSBAStatistics) { // Not instead PRODUCT...
    StackBasedAllocation::print_statistics(NULL);
  }
  if ( !UseGenPauselessGC ) {
    Universe::heap()->print_tracing_info();
  }

  { MutexLocker ml(BeforeExit_lock);
    _before_exit_status = BEFORE_EXIT_DONE;
BeforeExit_lock.notify_all();
  }

  #undef BEFORE_EXIT_NOT_RUN
  #undef BEFORE_EXIT_RUNNING
  #undef BEFORE_EXIT_DONE
}

void vm_exit(int code) {  
  exit_hook_t exit_hook = Arguments::exit_hook();
  Thread* thread = Thread::current();

  if (thread == NULL) {
    // we have serious problems -- just exit
if(exit_hook!=NULL){
exit_hook(code);
    } else {
      vm_direct_exit(code);
    }
  }

  if (VMThread::vm_thread() != NULL) {
    // Fire off a VM_Exit operation to bring VM to a safepoint and exit
    VM_Exit op(code);
    VMThread::execute(&op);
    // should never reach here; but in case something wrong with VM Thread.
    vm_direct_exit(code);
  } else {
    // VM thread is gone, just exit
if(exit_hook!=NULL){
exit_hook(code);
    } else {
      vm_direct_exit(code);
    }
  }
  ShouldNotReachHere();
}

void notify_vm_shutdown() {
}

void vm_direct_exit(int code) {
  notify_vm_shutdown();
  ::exit(code);
}

void vm_perform_shutdown_actions() {
  // Warning: do not call 'exit_globals()' here. All threads are still running.
  // Calling 'exit_globals()' will disable thread-local-storage and cause all
  // kinds of assertions to trigger in debug mode.
  if (is_init_completed()) {
    Thread* thread = Thread::current();
    if (thread->is_Java_thread()) {
      JavaThread *jt = (JavaThread*)thread;
if(jt->jvm_locked_by_self()){
        // We are leaving the VM, unlock the JVM lock like we would if we were
        // running in native code.  (in case any OS exit handlers call back to
        // the VM)
jt->jvm_unlock_self();
      }
    }
  }
  notify_vm_shutdown();
}

void vm_shutdown()
{
  vm_perform_shutdown_actions();
  os::shutdown();
}

void vm_abort(bool out_of_memory) {
  vm_perform_shutdown_actions();
if(out_of_memory==true){
os::abort(false,true);
  }
#ifdef ASSERT
  os::abort(true); // No core dump in product/optimized build.
#else
  if ( ForceCoreDumpInAbort ) {
    // Override "politeness" and force a core dump to ease debugging
    os::abort(true);
  } else {
    os::abort(false);
  }
#endif
  ShouldNotReachHere();
}

void vm_notify_during_shutdown(const char* error, const char* message) {
  if (error != NULL) { 
    tty->print_cr("Error occurred during initialization of VM");
    tty->print("%s", error);
    if (message != NULL) {
      tty->print_cr(": %s", message);
    }
    else {
      tty->cr();
    }
  }
}

void vm_exit_during_initialization(Handle exception) {
  tty->print_cr("Error occurred during initialization of VM");
  // If there are exceptions on this thread it must be cleared
  // first and here. Any future calls to EXCEPTION_MARK requires
  // that no pending exceptions exist.
  Thread *THREAD = Thread::current();
  if (HAS_PENDING_EXCEPTION) {
    CLEAR_PENDING_EXCEPTION;
  }
  java_lang_Throwable::print(exception, tty);
  tty->cr();
  java_lang_Throwable::print_stack_trace(exception(), tty);
  tty->cr();
  vm_notify_during_shutdown(NULL, NULL);
vm_abort(false);
}

void vm_exit_during_initialization(symbolHandle ex, const char* message) {
  ResourceMark rm;
  vm_notify_during_shutdown(ex->as_C_string(), message);
vm_abort(false);
}

void vm_exit_during_initialization(const char* error, const char* message) {
  vm_notify_during_shutdown(error, message);
vm_abort(false);
}

void vm_shutdown_during_initialization(const char* error, const char* message) {
  vm_notify_during_shutdown(error, message);
  vm_shutdown();
}

jdk_version_info JDK_Version::_version_info = {0};
bool JDK_Version::_pre_jdk16_version = false;
int  JDK_Version::_jdk_version = 0;

void JDK_Version::initialize() {
  void *lib_handle = os::native_java_library();
  jdk_version_info_fn_t func = 
    CAST_TO_FN_PTR(jdk_version_info_fn_t, hpi::dll_lookup(lib_handle, "JDK_GetVersionInfo0"));

  if (func == NULL) {
    // JDK older than 1.6
    _pre_jdk16_version = true;
    return;
  }

  if (func != NULL) {
    (*func)(&_version_info, sizeof(_version_info));
  }
  if (jdk_major_version() == 1) {
    _jdk_version = jdk_minor_version();
  } else {
    // If the release version string is changed to n.x.x (e.g. 7.0.0) in a future release
    _jdk_version = jdk_major_version();
  }
}

void JDK_Version_init() {
  JDK_Version::initialize();
}
