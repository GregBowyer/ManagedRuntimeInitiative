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
#include "compileBroker.hpp"
#include "debug.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_threadCleaner.hpp"
#include "init.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "threadService.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "xmlBuffer.hpp"

#define VM_OP_NAME_INITIALIZE(name) #name,  

const char* VM_Operation::_names[VM_Operation::VMOp_Terminating] = \
  { VM_OPS_DO(VM_OP_NAME_INITIALIZE) };

void VM_Operation::set_calling_thread(Thread*thread){
  _calling_thread = thread; 
}  


void VM_Operation::evaluate() {
  ResourceMark rm;
  VMTagMark vmt(Thread::current(), tag());
  if (TraceVMOperation) { 
    tty->print("["); 
NOT_PRODUCT(print(tty);)
  }
  doit();
  if (TraceVMOperation) { 
    tty->print_cr("]"); 
  }
}

// Called by fatal error handler.
void VM_Operation::print_on_error(outputStream* st) const {
  st->print("VM_Operation (" PTR_FORMAT "): ", this);
  st->print("%s", name());

  const char* mode;
  switch(evaluation_mode()) {
    case _safepoint      : mode = "safepoint";       break;
    case _no_safepoint   : mode = "no safepoint";    break;
    default              : mode = "unknown";         break;
  }
  st->print(", mode: %s", mode);

  if (calling_thread()) {
    st->print(", requested by thread " PTR_FORMAT, calling_thread());
  }
}

// The following methods are used by [G]PGC
VM_VMThreadSafepoint::VM_VMThreadSafepoint(bool should_clean, SafepointTimes* times, SafepointEndCallback end_callback, void* user_data) : _should_clean_self(should_clean), _times(times), _end_callback(end_callback), _user_data(user_data) {
}

void VM_VMThreadSafepoint::doit()
{
Thread*vmt=Thread::current();
  assert0(vmt->is_VM_thread());

  SafepointSynchronize::begin(_times); // Acquires the Threads_lock

  {
    MutexLocker ml(VMThreadSafepointEnd_lock, vmt);

    { // Tell the safepoint requestor that we're waiting for a safepoint now:
      MutexLocker ml(VMThreadSafepoint_lock, vmt);
VMThreadSafepoint_lock.notify();
    }

    // Wait for the safepoint to be ended.
    // TODO maw: wait with a timeout to sanity check that there's no bug making us wait forever.
    VMThreadSafepointEnd_lock.wait();

if(_end_callback!=NULL){
      Atomic::read_barrier();  // Make sure we notice an updated _should_clean_self
      if (_should_clean_self) {
        if ( UseGenPauselessGC ) {
          GPGC_ThreadCleaner::self_clean_vm_thread();
        }
      }

      (*_end_callback)(_user_data);

      {
        // Notify that the callback is done
        MutexLocker mlc(VMThreadCallbackEnd_lock, vmt);

        if (UseGenPauselessGC) {
          GPGC_Safepoint::set_callbacks_completed();
        } else {
          ShouldNotReachHere();
        }

VMThreadCallbackEnd_lock.notify();
      }
    }
  }

  SafepointSynchronize::end();

if(_end_callback==NULL){
    Atomic::read_barrier();  // Make sure we notice an updated _should_clean_self
    if (_should_clean_self) {
      if ( UseGenPauselessGC ) {
        GPGC_ThreadCleaner::self_clean_vm_thread();
      }
    }
  }
}

void VM_VMThreadSafepoint::safepoint_vm_thread()
{
MutexLocker ml(VMThreadSafepoint_lock,Thread::current());

  VMThread::async_execute(this);

  // Wait for the VMThread to have entered the safepoint.
  // TODO maw: wait with a timeout to sanity check that there's no bug making us wait forever.
  VMThreadSafepoint_lock.wait();
}

void VM_VMThreadSafepoint::restart_vm_thread()
{
  // Tell the VMThread it's time to wake up.
MutexLocker ml(VMThreadSafepointEnd_lock,Thread::current());
VMThreadSafepointEnd_lock.notify();
}


void VM_Deoptimize::doit() {
  BufferedLoggerMark m(NOTAG, Log::M_VM | Log::M_SAFEPOINT | Log::M_DEOPT, TraceDeoptimization);
  m.out("Safepoint for Deopt patching for");
  assert_locked_or_safepoint(Compile_lock);
for(uint i=0;i<methodCodeOopDesc::_deopt_list_len;i++){
    // Re-test the patched_for_deopt flag.  Since this is a VM_Operation, a
    // prior VM_Operation doing a GC may have already happened.  That prior
    // operation can do class-unloading which may force the deopt of some
    // CodeBlobs that otherwise would deopt here.

    const CodeBlob *cb = methodCodeOopDesc::_deopt_list[i];
    // Any chance of a funny narrow race where GC has already decided the MCO
    // & CodeBlob are dead - no need to patch then, but also the deopt_list
    // ends up holding stale CodeBlobs which can be recycled.
cb->verify();
    methodCodeOop mco = cb->owner().as_methodCodeOop();
    assert0( mco->is_oop() );
    if( !mco->_patched_for_deopt ) {
      mco->method().as_methodOop()->print_short_name(m.stream());
      m.out(", ");
cb->pd_patch_for_deopt();
      mco->_patched_for_deopt = true;
    }
  }
  methodCodeOopDesc::_deopt_list_len = 0;  // Reset list for next time
}



void VM_Verify::doit() {
  Universe::verify();
}

bool VM_PrintThreads::doit_prologue() {
  assert(Thread::current()->is_Java_thread(), "just checking");

  // Make sure AbstractOwnableSynchronizer is loaded
  if (JDK_Version::is_gte_jdk16x_version()) {
    java_util_concurrent_locks_AbstractOwnableSynchronizer::initialize(JavaThread::current());
  }

  if (_print_concurrent_locks) {
    // Get Heap_lock if concurrent locks will be dumped
    Heap_lock.lock_allowing_gc(JavaThread::current());
    GET_RPC;
    Heap_lock._rpc = RPC;      // Record locksite info
  }
  return true;
}

void VM_PrintThreads::doit() {
Threads::print_on(_out,true,false,true);
}

void VM_PrintThreads::doit_epilogue() {
  if (_print_concurrent_locks) {
    // Release Heap_lock 
Heap_lock.unlock();
  }
}

void VM_PrintJNI::doit() {
  JNIHandles::print_on(_out);
}

VM_FindDeadlocks::~VM_FindDeadlocks() {
  if (_deadlocks != NULL) {
    DeadlockCycle* cycle = _deadlocks;
    while (cycle != NULL) {
      DeadlockCycle* d = cycle;
      cycle = cycle->next();
      delete d;
    }
  }
}

bool VM_FindDeadlocks::doit_prologue() {
  assert(Thread::current()->is_Java_thread(), "just checking");

  // Load AbstractOwnableSynchronizer class
  if (JDK_Version::is_gte_jdk16x_version()) {
    java_util_concurrent_locks_AbstractOwnableSynchronizer::initialize(JavaThread::current());
  }

  return true;
}

void VM_FindDeadlocks::doit() {
  _deadlocks = ThreadService::find_deadlocks_at_safepoint(_concurrent_locks);
  int num_deadlocks = 0;

  if (_xb != NULL) {    // Do ARTA if xmlbuffer is avail
    for (DeadlockCycle* cycle = _deadlocks;
            cycle != NULL; cycle = cycle->next()) {
      print_xml_on(cycle);
    }
Threads::clear_arta_thread_states();
  } else {              // Otherwise print it
    for (DeadlockCycle* cycle = _deadlocks;
            cycle != NULL; cycle = cycle->next()) {
      num_deadlocks++;
cycle->print_on(tty);
    }

    if (num_deadlocks == 1) {
      _out->print_cr("\nFound 1 deadlock.\n");
      _out->flush();
    } else if (num_deadlocks > 1) {
      _out->print_cr("\nFound %d deadlocks.\n", num_deadlocks);
      _out->flush();
    }
  }
}

void VM_FindDeadlocks::print_xml_on(DeadlockCycle *cycle) {
  xmlElement dc(_xb, "deadlock_cycle");

  JavaThread* currentThread;
  ObjectMonitor* waitingToLockMonitor;
  int len = cycle->num_threads();

for(int i=0;i<len;i++){
    currentThread = cycle->threads()->at(i);
    waitingToLockMonitor = 
         (ObjectMonitor*)currentThread->current_pending_monitor();

    xmlElement xe(_xb, "deadlock_cycle_item");
    currentThread->print_xml_on(_xb, 0, false);

if(waitingToLockMonitor!=NULL){
      oop obj = (oop)waitingToLockMonitor->object();
      if (obj != NULL) {
          obj->print_xml_on(_xb, true);
      }
    }
  }
}

VM_ThreadDump::VM_ThreadDump(ThreadDumpResult* result,
                             int max_depth,
                             bool with_locked_monitors, 
                             bool with_locked_synchronizers) {
  _result = result;
  _num_threads = 0; // 0 indicates all threads
  _threads = NULL;
  _result = result;
  _max_depth = max_depth;
  _with_locked_monitors = with_locked_monitors;
_with_locked_synchronizers=false;
}

VM_ThreadDump::VM_ThreadDump(ThreadDumpResult* result,
                             GrowableArray<instanceHandle>* threads,
                             int num_threads,
                             int max_depth,
                             bool with_locked_monitors, 
                             bool with_locked_synchronizers) {
  _result = result;
  _num_threads = num_threads;
  _threads = threads;
  _result = result;
  _max_depth = max_depth;
  _with_locked_monitors = with_locked_monitors;
_with_locked_synchronizers=false;
}

bool VM_ThreadDump::doit_prologue() {
  assert(Thread::current()->is_Java_thread(), "just checking");

  // Load AbstractOwnableSynchronizer class before taking thread snapshots
  if (JDK_Version::is_gte_jdk16x_version()) {
    java_util_concurrent_locks_AbstractOwnableSynchronizer::initialize(JavaThread::current());
  }

  if (_with_locked_synchronizers) {
    // Acquire Heap_lock to dump concurrent locks
    Heap_lock.lock_allowing_gc(JavaThread::current());
    GET_RPC;
    Heap_lock._rpc = RPC;      // Record locksite info
  }

  return true;
}

void VM_ThreadDump::doit_epilogue() {
  if (_with_locked_synchronizers) {
    // Release Heap_lock 
Heap_lock.unlock();
  }
}

void VM_ThreadDump::doit() {
  ResourceMark rm;

  ConcurrentLocksDump concurrent_locks(true);
  if (_with_locked_synchronizers) {
    concurrent_locks.dump_at_safepoint();
  }

  if (_num_threads == 0) {
    // Snapshot all live threads
    for (JavaThread* jt = Threads::first(); jt != NULL; jt = jt->next()) {
      if (jt->is_exiting() || 
          jt->is_hidden_from_external_view())  {
        // skip terminating threads and hidden threads
        continue;
      } 
      ThreadConcurrentLocks* tcl = NULL;
      if (_with_locked_synchronizers) {
        tcl = concurrent_locks.thread_concurrent_locks(jt);
      }
      ThreadSnapshot* ts = snapshot_thread(jt, tcl);
      _result->add_thread_snapshot(ts);
    }
  } else { 
    // Snapshot threads in the given _threads array
    // A dummy snapshot is created if a thread doesn't exist
    for (int i = 0; i < _num_threads; i++) {
      instanceHandle th = _threads->at(i);
      if (th() == NULL) {
        // skip if the thread doesn't exist 
        // Add a dummy snapshot
        _result->add_thread_snapshot(new ThreadSnapshot());
        continue;
      }

      // Dump thread stack only if the thread is alive and not exiting
      // and not VM internal thread.
      JavaThread* jt = java_lang_Thread::thread(th());
      if (jt == NULL || /* thread not alive */ 
          jt->is_exiting() ||
          jt->is_hidden_from_external_view())  {
        // add a NULL snapshot if skipped
        _result->add_thread_snapshot(new ThreadSnapshot());
        continue;
      }
      ThreadConcurrentLocks* tcl = NULL;      
      if (_with_locked_synchronizers) {
        tcl = concurrent_locks.thread_concurrent_locks(jt);
      }
      ThreadSnapshot* ts = snapshot_thread(jt, tcl);
      _result->add_thread_snapshot(ts);
    }
  }
}

ThreadSnapshot* VM_ThreadDump::snapshot_thread(JavaThread* java_thread, ThreadConcurrentLocks* tcl) {
  ThreadSnapshot* snapshot = new ThreadSnapshot(java_thread);
  snapshot->dump_stack_at_safepoint(_max_depth, _with_locked_monitors);
  snapshot->set_concurrent_locks(tcl);
  return snapshot;
}

volatile bool VM_Exit::_vm_exited = false;
Thread * VM_Exit::_shutdown_thread = NULL;

int VM_Exit::set_vm_exited() {
Thread*thr_cur=Thread::current();

  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint already");

  int num_active = 0;

  _shutdown_thread = thr_cur;
  _vm_exited = true;                                // global flag
  for(JavaThread *thr = Threads::first(); thr != NULL; thr = thr->next())
if(thr!=thr_cur&&!thr->is_hint_blocked()){
      ++num_active;
      thr->set_terminated(JavaThread::_vm_exited);  // per-thread flag
    }

  return num_active;
}

int VM_Exit::wait_for_threads_in_native_to_block() {
  // VM exits at safepoint. This function must be called at the final safepoint
  // to wait for threads in _thread_in_native state to be quiescent.
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint already");

Thread*thr_cur=Thread::current();
  WaitLock timer(CodeCache_lock._rank, "VM_Exit timer", false);

  // Compiler threads need longer wait because they can access VM data directly
  // while in native. If they are active and some structures being used are
  // deleted by the shutdown sequence, they will crash. On the other hand, user 
  // threads must go through native=>Java/VM transitions first to access VM
  // data, and they will be stopped during state transition. In theory, we
  // don't have to wait for user threads to be quiescent, but it's always
  // better to terminate VM when current thread is the only active thread, so
  // wait for user threads too. Numbers are in 10 milliseconds.
  int max_wait_user_thread = 30;                  // at least 300 milliseconds
  int max_wait_compiler_thread = 1000;            // at least 10 seconds

  int max_wait = max_wait_compiler_thread;

  int attempts = 0;
  while (true) {
    int num_active = 0;
    int num_active_compiler_thread = 0;

    for(JavaThread *thr = Threads::first(); thr != NULL; thr = thr->next()) {
      if (thr!=thr_cur && !thr->is_hint_blocked()) { // not blocked but at safepoint?  must be in native
        num_active++;
if(thr->is_Compiler_thread())num_active_compiler_thread++;
      }
    }

    if (num_active == 0) {
       return 0;
    } else if (attempts > max_wait) {
       return num_active;
    } else if (num_active_compiler_thread == 0 && attempts > max_wait_user_thread) {
       return num_active;
    }

    attempts++;

MutexLocker ml(timer);
    timer.wait_micros(10000L, false);
  }
}

void VM_Exit::doit() {
  CompileBroker::set_should_block();

  // Wait for a short period for threads in native to block. Any thread
  // still executing native code after the wait will be stopped at
  // native==>Java/VM barriers.
  // Among 16276 JCK tests, 94% of them come here without any threads still
  // running in native; the other 6% are quiescent within 250ms (Ultra 80).
  wait_for_threads_in_native_to_block();
  
  set_vm_exited();

  // Stop the pauseless gc thread gracefully before we start 
  // freeing memory he may be using!!
  if ( UseGenPauselessGC ) {
    GPGC_Thread::stop_all();
  }

  // cleanup globals resources before exiting. exit_globals() currently
  // cleans up outputStream resources and PerfMemory resources.
  exit_globals();

  // Check for exit hook
  exit_hook_t exit_hook = Arguments::exit_hook();
  if (exit_hook != NULL) {
    // exit hook should exit. 
    exit_hook(_exit_code);
    // ... but if it didn't, we must do it here
    vm_direct_exit(_exit_code);
  } else {
    vm_direct_exit(_exit_code);
  }
}


void VM_Exit::wait_if_vm_exited() {
  if (_vm_exited && 
      Thread::current() != _shutdown_thread) {
    // _vm_exited is set at safepoint, and the Threads_lock is never released
    // we will block here until the process dies
    // Called by both JavaThreads and non-JavaThreads (e.g. GC worker threads)
    MutexLockerAllowGC(&Threads_lock,1);
    ShouldNotReachHere();
  }
}
