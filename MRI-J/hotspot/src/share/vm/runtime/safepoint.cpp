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


#include "codeCache.hpp"
#include "gpgc_thread.hpp"
#include "interpreter_pd.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "resourceArea.hpp"
#include "runtimeService.hpp"
#include "safepoint.hpp"
#include "smaHeuristic.hpp"
#include "synchronizer.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

// ----------------------------------------------------------------------------
// Implementation of Safepoint begin/end

double             SafepointSynchronize::_max_time_to_safepoint = 0.0;
double             SafepointSynchronize::_max_safepoint         = 0.0;
double             SafepointSynchronize::_max_checkpoint        = 0.0;
SafepointTimes     SafepointSynchronize::_current_times;
SafepointSynchronize::SynchronizeState volatile SafepointSynchronize::_state = SafepointSynchronize::_not_synchronized;
int SafepointSynchronize::_waiting_to_block=0;
bool               SafepointSynchronize::_priority_boosted      = false;
JavaThreadClosure* SafepointSynchronize::_checkpoint_closure    = NULL;


void SafepointSynchronize::begin(SafepointTimes* times) {
  ResourceMark rm;
  Thread* myThread = Thread::current();

  assert(myThread->is_VM_thread()
    || (UseGenPauselessGC && myThread->is_GenPauselessGC_thread()),
"Only VM thread, PGC, or GPGC thread may initiate a safepoint synchronization");

  // By getting the Threads_lock, we assure that no threads are about to start
  // or exit.  It is released again in SafepointSynchronize::end().
  long acquire_threads_lock = os::elapsed_counter();
  Threads_lock.lock_can_block_gc(myThread);
  GET_RPC;
  Threads_lock._rpc = RPC;
  long threads_lock_acquired = os::elapsed_counter();

  memset(&_current_times, 0, sizeof(_current_times));
  _current_times.acquire_threads_lock  = acquire_threads_lock;
  _current_times.threads_lock_acquired = threads_lock_acquired;
  
  assert( _state == _not_synchronized, "trying to safepoint synchronize with wrong state");     
  int nof_threads = Threads::number_of_threads();

  Log::log3(NOTAG, Log::M_SAFEPOINT, TraceSafepoint, "Safepoint synchronization initiated. (%d)", nof_threads);

  // Array of threads we are waiting on.  This is a compacting array of
  // threads not-yet-VM-locked.  As the VM takes the JVM locks from these
  // threads they are thrown out of the array.  After a wait and timeout, the
  // VM thread will re-visit all these threads and try to get the JVM lock
  // from each.
  JavaThread** threads = NEW_RESOURCE_ARRAY(JavaThread*,nof_threads);
    
  RuntimeService::record_safepoint_begin();

  // Set number of threads to wait for, before we initiate the callbacks 
  {
MutexLocker mx(Safepoint_lock);
    _waiting_to_block = nof_threads; // write

    // After we change our safepoint state the threads start checking in.  There
    // could be 1000's of threads to check-in and some could be slow.  Hence we
    // have 2 latency tolerance mechanisms.  Threads that self-check-in lower a
    // counter.  When it hits 0 the VM thread gets notified.  This is good for
    // the case of 1000's of self-checking-in threads; each thread pays 1 atomic
    // decrement and there's 1 total notify sent to the VM thread.  We also have
    // a spin-loop in the VM, to handle the case where a thread gives up it's
    // JVM lock and enters native code and spends a long time there.  Threads
    // entering native code do not poll and so could transition from Java to
    // native code without telling the VM thread.  The VM thread periodically
    // polls these threads and attempts to take the JVM lock from them.
  
    // At this point, threads will start doing callbacks into the safepoint
    // code.  If it is in native or blocked, the thread will simply block on the
    // barrier.  If it is the vm, it will decrement the _waiting_to_block
    // counter and then block.
    _state = _synchronizing;	// volatile write
  } // Safepoint_lock->unlock();
    
  // Make interpreter safepoint aware
TemplateInterpreter::notice_safepoints();
    
  int nof_waiting_threads=0;

  _current_times.begin_threads_notify = os::elapsed_counter();

  // Iterate through all threads and tell them to self-suspend
for(JavaThread*jt=Threads::first();jt!=NULL;jt=jt->next()){
    // Take a quick shot at getting the VM lock.  If we succeed and made the
    // 1->0 transition (as opposed to a JavaThread making the same transition)
    // we can then decrement the waiting count.

    // Start safepoint timing.
    jt->_safepoint_start = jt->_safepoint_end = os::elapsed_counter();
    
if(jt->jvm_lock_VM_attempt()){
MutexLocker mx(Safepoint_lock);
      assert(_waiting_to_block >  0, "sanity check");
      _waiting_to_block--;
      jt->set_suspend_request_no_polling( JavaThread::safep_suspend );
    } else {
      threads[nof_waiting_threads++] = jt; // Array of threads not yet suspended
      jt->set_suspend_request( JavaThread::safep_suspend );
    }
  }

  _current_times.all_threads_notified = os::elapsed_counter();
  _current_times.total_threads        = nof_waiting_threads;

  // Wait until all threads are stopped    
  { 
MutexLocker mx(Safepoint_lock);

jlong start=os::elapsed_counter();
    jlong time_limit         = start + (SafepointTimeoutDelay * os::elapsed_frequency() / 1000); // SafepointTimeoutDelay is in millis
    bool  time_limit_reached = false;

    while( _waiting_to_block > 0 ) {

      // For all threads not-yet-suspended, try to grab the VM lock on them.
for(int i=0;i<nof_waiting_threads;i++){
        JavaThread *jt = threads[i];
        // Take a quick shot at getting the VM lock.  If we succeed and
        // made the 1->0 transition (as opposed to a JavaThread making
        // the same transition) we can then decrement the waiting count.
if(jt->jvm_lock_VM_attempt()){
          assert(_waiting_to_block >  0, "sanity check");
          _waiting_to_block--;
        }
        if( jt->jvm_locked_by_VM() ) // Compress out already blocked threads
          threads[i--] = threads[--nof_waiting_threads];
      }

      // Got 'em all?
      if( _waiting_to_block == 0 ) break;
      //if( nof_waiting_threads == 0 ) break;

jlong now=os::elapsed_counter();
      if( now>time_limit && !time_limit_reached ) {
        time_limit_reached = true;

        // Print out the thread IDs which didn't stop.
        jlong elapsed = (now - start) * 1000 / os::elapsed_frequency();
        tty->cr();
tty->print_cr("# SafepointSynchronize::begin: Timeout detected:");
tty->print_cr("# SafepointSynchronize::begin: Timed out after %lld ms "
                      "while waiting for threads to stop.", elapsed);
tty->print_cr("# SafepointSynchronize::begin: Threads which "
"did not reach the safepoint:");

for(JavaThread*cur_thread=Threads::first();cur_thread;cur_thread=cur_thread->next()){
          if( !cur_thread->jvm_locked_by_VM() ) {
            tty->print("# %p (unique id %ld) ", cur_thread, cur_thread->reversible_tid());
            cur_thread->print();
            tty->cr();
          }
        }

tty->print_cr("# SafepointSynchronize::begin: (End of list)");
 
        // To debug the long safepoint, specify both DieOnSafepointTimeout &
        // ShowMessageBoxOnError.
        if (DieOnSafepointTimeout) {
          char msg[1024];
          VM_Operation *op = VMThread::vm_operation();
          sprintf(msg,
                 "Safepoint sync time longer than " INT64_FORMAT_W(6)
" ms detected when executing %s.",
                 SafepointTimeoutDelay,
                 op != NULL ? op->name() : "no vm operation");
 
          fatal(msg);
        }
      }
     
      Log::log3(NOTAG, Log::M_SAFEPOINT, TraceSafepoint, "Waiting for %d thread(s) to block", nof_waiting_threads);
      // Wait 500 micros for somebody to wake us up
      Safepoint_lock.wait_micros(500L, false);
    }               

#ifdef ASSERT    
for(JavaThread*jt2=Threads::first();jt2!=NULL;jt2=jt2->next())
      assert( jt2->jvm_locked_by_VM(), "did not get all JVM locks" );
#endif

    // Calculate per thread statistics
for(JavaThread*jt3=Threads::first();jt3!=NULL;jt3=jt3->next()){
      uint64_t start = jt3->_safepoint_start;
      uint64_t end = jt3->_safepoint_end;
      if (start && end && start < end) {
        uint64_t interval = end - start;
        jt3->inc_safepoint_total(interval);
        jt3->inc_safepoint_count();
      
        if (interval) {
          if (!jt3->get_safepoint_min() || jt3->get_safepoint_min() > interval) {
            jt3->set_safepoint_min(interval);
          }
          if (jt3->get_safepoint_max() < interval) {
            jt3->set_safepoint_max(interval);
            jt3->set_safepoint_max_when(start);
            jt3->set_safepoint_max_pc(jt3->get_safepoint_pc());
            jt3->set_safepoint_max_rpc(jt3->get_safepoint_rpc());
          }
        }
    
        jt3->set_safepoint_start(0);
        jt3->set_safepoint_end(0);
      }
    }

    // Record state
    _state = _synchronized;
  
  } // Safepoint_lock->unlock();

  _current_times.safepoint_reached = os::elapsed_counter();

  VM_Operation *op = VMThread::vm_operation();     
  Log::log3(NOTAG, Log::M_SAFEPOINT | Log::ENTER, TraceSafepoint, "Entering safepoint region: %s", (op != NULL) ? op->name() : "no vm operation");

  RuntimeService::record_safepoint_synchronized();

  if (PrintSMAStatistics) {
ObjectSynchronizer::summarize_sma_status();
  }
  
  SmaHeuristicSamplerTask::report();
  
  // Call stuff that needs to be run when a safepoint is just about to be completed
  do_cleanup_tasks();  

  _current_times.cleanup_tasks_complete = os::elapsed_counter();

  // If the caller wanted safepoint start times, copy them out while the Threads_lock is held!
  if (times) {
    memcpy(times, &_current_times, sizeof(*times));
  }

  // Threads_lock still held for the duration of the Safepoint operation
}


// Wake up all threads, so they are ready to resume execution after the safepoint
// operation has been carried out
void SafepointSynchronize::end() {

  Thread* myThread = Thread::current();
  assert(myThread->is_VM_thread()
   || (UseGenPauselessGC && myThread->is_GenPauselessGC_thread()),
"Only VM thread, PGC, or GPGC thread can end a safepoint synchronization");

  assert(_state == _synchronized, "must be synchronized before ending safepoint synchronization");

#ifdef ASSERT    
for(JavaThread*jt=Threads::first();jt!=NULL;jt=jt->next())
    assert( jt->jvm_locked_by_VM(), "did not keep all JVM locks" );
#endif

  // Set to not synchronized, so the threads will not go into the
  // signal_thread_blocked method when they get restarted.
  _state = _not_synchronized;  
  if( os::is_MP() ) Atomic::membar();
  
  Log::log3(NOTAG, Log::M_SAFEPOINT | Log::LEAVE, TraceSafepoint, "Leaving safepoint region");

  _current_times.safepoint_work_done = os::elapsed_counter();

  // Remove safepoint check from interpreter
TemplateInterpreter::ignore_safepoints();
  
  // Start suspended threads
  for(JavaThread *current = Threads::first(); current; current = current->next()) {
    // Clear the self-suspend polling bit
    current->clr_suspend_request(JavaThread::safep_suspend);
    // Allow the thread to take the JVM lock and access naked oops again
current->jvm_unlock_VM();
  }
  
  // A problem occuring on Solaris is when attempting to restart threads the
  // first #cpus - 1 go well, but then the VMThread is preempted when we get
  // to the next one (since it has been running the longest).  We then have to
  // wait for a cpu to become available before we can continue restarting
  // threads.

  // FIXME: This causes the performance of the VM to degrade when active and
  // with large numbers of threads.  Apparently this is due to the synchronous
  // nature of suspending threads.
  if (VMThreadHintNoPreempt) 
    os::hint_no_preempt();
  
  RuntimeService::record_safepoint_end();

  // We capture end_wakeup time just before releasing the Threads_lock, as
  // scheduling turbulence could cause a read of the time after the unlock to
  // be delayed, and another thread could begin a checkpoint immediately, thus
  // stomping our timing data.
  _current_times.wakeup_done = os::elapsed_counter();

  if (PrintSafepointStatistics) {
    print_safepoint_times();
  }

#ifndef PRODUCT
  long start_wakeup = _current_times.safepoint_work_done;
  long end_wakeup   = _current_times.wakeup_done;
#endif // ! PRODUCT

  // Release threads lock, so threads can be created/destroyed again.  It
  // will also start all threads blocked in signal_thread_blocked.
Threads_lock.unlock();

#ifndef PRODUCT
  if( ProfileMMU ) {
jlong end_unlock=os::elapsed_counter();
    jlong d_wakeup = end_wakeup - start_wakeup;
    jlong d_unlock = end_unlock - end_wakeup;
    if( d_wakeup > 533280*10 || d_unlock > 533280*10 ) 
tty->print_cr("### SLOW WAKEUP= %ld ms, SLOW UNLOCK= %ld ms",
                    os::ticks_to_millis(d_wakeup), os::ticks_to_millis(d_unlock));
  }
#endif
}

// Various cleaning tasks that should be done periodically at safepoints
void SafepointSynchronize::do_cleanup_tasks() {
jlong cleanup_time=0;

  // Update fat-monitor pool, since this is a safepoint.
  if (TraceSafepoint) {
    cleanup_time = os::javaTimeNanos();
  }
  
  ObjectSynchronizer::deflate_idle_monitors();
  // Pauseless GC does sweeping as a task during the third safepoint
  if ( !UseGenPauselessGC ) {
CodeCache::clean_inline_caches();
  }
  if (TraceSafepoint) {
tty->print_cr("do_cleanup_tasks takes %6lldms",
                  (os::javaTimeNanos() - cleanup_time) / MICROUNITS);
  }
}


// -------------------------------------------------------------------------------------------------------
// Implementation of Safepoint callback point

void SafepointSynchronize::block(JavaThread *thread) {
  assert(thread != NULL, "thread must be set");
  assert(thread->is_Java_thread(), "not a Java thread");
  
  // If the end time is set then blocking via safepoint irq.
  bool is_irq = thread->_safepoint_start != thread->_safepoint_end;
  // Set end time for all other cases.
  if (!is_irq) thread->_safepoint_end = os::elapsed_counter();
  // Get pc and rpc in code.  IRQs are more interesting one frame further.
  uint64_t pc, rpc;
  thread->self_pc_and_rpc(is_irq ? 3 : 2, &pc, &rpc);
thread->set_safepoint_pc(pc);
  thread->set_safepoint_rpc(rpc);

  // Threads shouldn't block if they are in the middle of printing, but...
  ttyLocker::break_tty_lock_for_safepoint(os::current_thread_id());

  // Only bail from the block() call if the thread is gone from the
  // thread list; starting to exit should still block.
  if (thread->is_terminated()) {
     // block current thread if we come here from native code when VM is gone
     thread->block_if_vm_exited();
     // otherwise do nothing
     return;
  }

  // At this point we no longer hold our own JVM lock.  We need to allow the
  // VM thread to grab the JVM lock; when it gets them all a Safepoint has
  // been achieved (e.g. GC can proceed).  The VM thread is wait()ing on the
  // Safepoint_lock.  We have some options.  The simplist thing to do is
  // notify() the VM thread and let the VM thread scan all JavaThreads and
  // grab free JVM locks (including ours); repeat until the VM thread gets all
  // JVM locks.  We do something a little higher performance: we donate our
  // JVM lock to the VM thread and lower a shared count.  When the count goes
  // to zero we wake up the VM thread.
assert(!thread->jvm_locked_by_self(),"must have released self jvm lock already");

  // Spin until either we or the VM thread lock the JVM lock for the VM.  We
  // do it as a curtesy to the VM thread and because it's likely that all
  // pieces-parts are in our cache and so it is fast.  No one is trying to
  // lock this lock "for self" but we still race with the VM thread and also
  // race with the polling advisory bit so contention is possible.  Even
  // though we may lock it, only the VM thread will unlock it.  The thread
  // that actually succeeds in a "jvm_lock_VM_attempt" is the only thread that
  // can decrement the waiters count.
  while( !thread->jvm_locked_by_VM() ) { // Neither got it yet

    // If we stall right here, it is possible for a GC to come & go & come &
    // go any number of times.

    // Attempt to lock the JVM lock on behalf of the VM thread
if(thread->jvm_lock_VM_attempt()){
      // We did it!  Means we, not the VM thread, made the free->locked_by_VM
      // transition.  We need to inform the VM thread by lowering the
      // waiting_to_block count and doing a notify.
MutexLocker mx(Safepoint_lock);
      // Could be a GC came and went and we are no longer attempting to reach
      // a safepoint.  If so, we can just get out of here.
      if( !is_synchronizing() ) { // No longer safepointing
        // Must free the JVM lock first.  While we hold the Safepoint_lock
        // another GC cannot start (or at least not get very far).  If we let
        // go of the Safepoint_lock a racing GC could find that he already
        // holds the JVM lock and be suprised when it gets freed.
        thread->jvm_unlock_VM();  // Free the JVM lock first
        return;                   // Free the safepoint lock second
      }
      // Decrement the number of threads to wait for and signal vm thread      
      assert(_waiting_to_block >  0, "sanity check");
      _waiting_to_block--;
if(_waiting_to_block==0)//Last thread?  Then wake up VM thread
        Safepoint_lock.notify(); // signal VM thread
      break;
    }
  }

  thread->hint_blocked("at safepoint");

  // We now try to acquire the threads lock.  Since this lock is held by the
  // VM thread during the entire safepoint, the threads will all line up here
  // during the safepoint.
  { MutexLockerAllowGC mx(Threads_lock, thread);
  /* nothing */
  }

thread->hint_unblocked();

  // When we return from here, a full poll happens
}


// Roll all threads forward to a checkpoint and perform the closure action
void SafepointSynchronize::do_checkpoint(JavaThreadClosure *jtc, SafepointTimes* times) {
  ResourceMark rm;
  Thread* myThread = Thread::current();

  assert(myThread->is_VM_thread()
    || (UseGenPauselessGC && myThread->is_GenPauselessGC_thread()),
"Only VM thread, PGC, or GPGC thread may initiate a checkpoint synchronization");

  // Set the blocked state to coordinate with possible VM shutdown
if(myThread->is_GenPauselessGC_thread()){
    ((GPGC_Thread*)myThread)->set_blocked( true );
  }
  // By getting the Threads_lock, we assure that no threads are about to start
  // or exit.  And also block against another thread doing a safepoint or checkpointing.
  long acquire_threads_lock = os::elapsed_counter();
MutexLocker mx(Threads_lock,myThread);
  long threads_lock_acquired = os::elapsed_counter();

  memset(&_current_times, 0, sizeof(_current_times));
  _current_times.acquire_threads_lock  = acquire_threads_lock;
  _current_times.threads_lock_acquired = threads_lock_acquired;
  
  // We got the threads lock, check that we were not racing with VM shutdown
if(myThread->is_GenPauselessGC_thread()){
    if ( GPGC_Thread::should_terminate() ) {
      ((GPGC_Thread*)myThread)->block_if_vm_exited();
    }
    ((GPGC_Thread*)myThread)->set_blocked( false );
  }

assert(jtc!=NULL,"bad checkpoint closure!");
  _checkpoint_closure = jtc;
  _priority_boosted = false;
  
assert(_state==_not_synchronized,"trying to checkpoint while in a safepoint?");
  int nof_threads = Threads::number_of_threads();
  
  // Array of threads we are waiting on.  This is a compacting array of
  // threads not-yet-VM-locked.  As the VM takes the JVM locks from these
  // threads they are thrown out of the array.  After a wait and timeout, the
  // VM thread will re-visit all these threads and try to get the JVM lock
  // from each.
  JavaThread** threads = NEW_RESOURCE_ARRAY(JavaThread*,nof_threads);
    
  // At this point, threads will start doing callbacks into the checkpoint code.  
  _state = _checkpointing;	// volatile write
  if( os::is_MP() ) Atomic::membar();
    
  // Make interpreter safepoint aware
TemplateInterpreter::notice_safepoints();
    
  // Iterate through all threads and tell them to self-suspend
  int nof_waiting_threads=0;
 
  _current_times.begin_threads_notify = os::elapsed_counter();

  // Unlike the safepoint code, quickly set everyone's suspend 
  // flag so we might get more parallelism in running the checkpoint function
for(JavaThread*jt=Threads::first();jt!=NULL;jt=jt->next()){
    threads[nof_waiting_threads++] = jt; // Array of threads not yet suspended
    jt->set_suspend_request( JavaThread::checkp_suspend );
  }

  _current_times.all_threads_notified = os::elapsed_counter();
  _current_times.total_threads        = nof_waiting_threads;

  // Wait until all threads are checkpointed either by this thread or the
  // target thread itself
jlong start=os::elapsed_counter();
  jlong time_limit         = start + (CheckpointTimeoutDelay * os::elapsed_frequency() / 1000); // CheckpointTimeoutDelay is in millis
  bool  time_limit_reached = false;

  int   loop_counter       = 0;

  while( nof_waiting_threads > 0  ) {
    // For all threads not-yet-suspended, try to grab the VM lock on them.
for(int i=0;i<nof_waiting_threads;i++){
      JavaThread *jt = threads[i];
      // Take a quick shot at getting the VM lock.  If we succeed and made the
      // 1->0 transition (as opposed to a JavaThread making the same
      // transition) we can then decrement the waiting count.
if(jt->jvm_lock_VM_attempt()){
        if( jt->test_clr_suspend_request(JavaThread::checkp_suspend) ) {
          // if we get here, run the closure on this guy and mark him done
          long start_ticks = os::elapsed_counter();
          jtc->do_java_thread( jt );
          _current_times.thread_closure += os::elapsed_counter() - start_ticks;
        } else {
          // The thread already cleared the checkp_suspend flag, and so has
          // already run the checkpoint function on itself.
          _current_times.self_checkpoints++;
        }
        // Allow the thread to take the JVM lock and access naked oops again
jt->jvm_unlock_VM();
        // Undo any temporary priority boost that was done to the thread.
        if ( _priority_boosted ) {
          os::set_native_priority(jt, jt->current_priority());
        }
        // Compress out threads we already know have completed the checkpoint.
        threads[i--] = threads[--nof_waiting_threads];
      } else {
        // Couldn't get the JVM lock for the JavaThread.
        if ( (jt->please_self_suspend() & JavaThread::checkp_suspend) == 0 ) {
          // The JavaThread has performed the checkpoint operation on itself.  It
          // might have missed resetting its priority due to a race.
          if ( _priority_boosted ) {
            os::set_native_priority(jt, jt->current_priority());
          }
          // Compress out threads we already know have completed the checkpoint.
          threads[i--] = threads[--nof_waiting_threads];

          _current_times.self_checkpoints++;
        }
      }
    }

    // Got 'em all?
    if( nof_waiting_threads == 0 ) break;

jlong now=os::elapsed_counter();
    if ( now>time_limit && !time_limit_reached ) {
      time_limit_reached = true;

      // Print out the thread IDs which didn't satisfy the checkpoint.
      jlong elapsed = (now - start) * 1000 / os::elapsed_frequency();
tty->print_cr("# SafepointSynchronize::do_checkpoint: Fatal error:");
tty->print_cr("# SafepointSynchronize::do_checkpoint: Timed out after %lld ms while attempting to complete a checkpoint.",elapsed);
tty->print_cr("# SafepointSynchronize::do_checkpoint: Threads which did not complete the checkpoint:");
for(int i=0;i<nof_waiting_threads;i++){
        JavaThread* jt = threads[i];
        tty->print("#%p (unique id %ld)", jt, jt->reversible_tid());
        jt->osthread()->print();
        jt->print_thread_state_on(tty);
        tty->cr();
      }
tty->print_cr("# SafepointSynchronize::do_checkpoint: (End of list)");

      if (DieOnSafepointTimeout) {
        char msg[1024];
        VM_Operation *op = VMThread::vm_operation();
sprintf(msg,
               "Safepoint sync time longer than " INT64_FORMAT_W(6)
" ms detected when executing %s.",
               SafepointTimeoutDelay,
               op != NULL ? op->name() : "no vm operation");

        fatal(msg);
      }
    }

    // After a couple of iterations through the threads array, raise priority of
    // not-checked-in threads to ensure forward progress.
    if ( _priority_boosted==false && loop_counter>1 ) {
      _priority_boosted = true;
for(int i=0;i<nof_waiting_threads;i++){
        JavaThread *jt = threads[i];
        // We want to push up the priority of threads not yet checkpointed.  But we have to be
        // aware of racing with a JavaThread that's changing its own priority.  The race is
        // solved by requiring JavaThreads to hold the Threads_lock when changing their priority.
        // Since the checkpoint operation holds the Threads_lock, the race cannot happen.  Which
        // also implies that that priority changes by JavaThreads stall for the duration of a
        // checkpoint.
os::set_native_priority(jt,CheckpointBoostPriority);
      }

      _current_times.priority_boosted = os::elapsed_counter();
      _current_times.priority_boosts  = nof_waiting_threads;
    }

    // consider busy looping rather than sleep?
    loop_counter++;

os::yield_all();
  }

  _current_times.safepoint_reached = os::elapsed_counter();

  if (PrintSafepointStatistics) {
    print_checkpoint_times();
  }

  // from here down is the code to resume normal execution

  // Set to not synchronized, so the threads will not go into the
  // signal_thread_blocked method when they get restarted.
  _state = _not_synchronized;  
  if( os::is_MP() ) Atomic::membar();

  // Remove safepoint check from interpreter
TemplateInterpreter::ignore_safepoints();

  // we are completely done.
_checkpoint_closure=NULL;

  // If the caller wanted timing stats, copy them out before the Threads_lock is released!
  if (times) {
    memcpy(times, &_current_times, sizeof(*times));
  }
}


// The thread's own chance to run the checkpoint function before the VM thread
void SafepointSynchronize::block_checkpoint(JavaThread*jt){
  _checkpoint_closure->do_java_thread( jt );
}


void SafepointSynchronize::reset_priority_if_needed(JavaThread*thread){
  if ( _priority_boosted ) {
    os::set_native_priority(thread, thread->current_priority());
  }
}



//
// PrintSafepointStatistics support.
// 
void SafepointSynchronize::print_safepoint_times(){
  assert(Threads_lock.owned_by_self(), "Safepoint times may be corrupt if lock isn't held.");

  double freq              = double(os::elapsed_frequency()) / 1000.0;
  double threads_lock_ms   = double(_current_times.threads_lock_acquired  - _current_times.acquire_threads_lock  ) / freq;
  double notify_ms         = double(_current_times.all_threads_notified   - _current_times.begin_threads_notify  ) / freq;
  double wait_ms           = double(_current_times.safepoint_reached      - _current_times.all_threads_notified  ) / freq;
  double cleanup_ms        = double(_current_times.cleanup_tasks_complete - _current_times.safepoint_reached     ) / freq;
  double op_ms             = double(_current_times.safepoint_work_done    - _current_times.cleanup_tasks_complete) / freq;
  double wakeup_ms         = double(_current_times.wakeup_done            - _current_times.safepoint_work_done   ) / freq;

  double time_to_safepoint = double(_current_times.cleanup_tasks_complete - _current_times.threads_lock_acquired ) / freq;
  double total_time        = double(_current_times.wakeup_done            - _current_times.threads_lock_acquired ) / freq;

tty->stamp();
tty->print_cr(": Safepoint: lock %3.3f, notify %3.3f, wait %3.3f, cleanup %3.3f, op %3.3f, wakeup %3.3f millis",
                threads_lock_ms, notify_ms, wait_ms, cleanup_ms, op_ms, wakeup_ms);

  if ( _max_time_to_safepoint < total_time ) { _max_time_to_safepoint = time_to_safepoint; }
  if ( _max_safepoint         < total_time ) { _max_safepoint         = total_time; }
}

void SafepointSynchronize::print_checkpoint_times(){
  assert(Threads_lock.owned_by_self(), "Checkpoint times may be corrupt if lock isn't held.");

  double freq            = double(os::elapsed_frequency()) / 1000.0;
  double threads_lock_ms = double(_current_times.threads_lock_acquired  - _current_times.acquire_threads_lock ) / freq;
  double notify_ms       = double(_current_times.all_threads_notified   - _current_times.begin_threads_notify ) / freq;
  double wait_ms         = double(_current_times.safepoint_reached      - _current_times.all_threads_notified ) / freq;
  double boosted_wait_ms = 0;

  if ( _current_times.priority_boosted != 0 ) {
    boosted_wait_ms      = double(_current_times.safepoint_reached      - _current_times.priority_boosted     ) / freq;
  }

  double closure_ms      = double(_current_times.thread_closure) / freq;
  double total_time      = double(_current_times.safepoint_reached      - _current_times.threads_lock_acquired) / freq;

  tty->stamp();
tty->print_cr(": Checkpoint: lock %3.3f, notify %3.3f, wait %3.3f, boosted wait %3.3f, closure %3.3f millis: "
"threads %ld, boosted %ld, self chkp %ld",
                threads_lock_ms, notify_ms, wait_ms - closure_ms, boosted_wait_ms, closure_ms,
                _current_times.total_threads, _current_times.priority_boosts, _current_times.self_checkpoints);

  if ( _max_checkpoint < total_time ) { _max_checkpoint = total_time; }
}

// This method will be called when VM exits. 
// It tries to summarize the sampling.
void SafepointSynchronize::print_stat_on_exit() {
tty->stamp();
tty->print_cr(": Safepoint max times: safepoint %3.3f ms, time to safepoint %3.3f ms, checkpoint %3.3f ms",
                _max_safepoint, _max_time_to_safepoint, _max_checkpoint);
}

// ------------------------------------------------------------------------------------------------
// Non-product code
#ifndef PRODUCT
void SafepointSynchronize::print_state() {
  if (_state == _not_synchronized) {
    tty->print_cr("not synchronized");
  } else if (_state == _synchronizing || _state == _synchronized) {
    tty->print_cr("State: %s", (_state == _synchronizing) ? "synchronizing" :
                  "synchronized");

    for(JavaThread *cur = Threads::first(); cur; cur = cur->next()) {
      cur->print_thread_state_on(tty);
      tty->cr();
    }
  } 
}

void SafepointSynchronize::safepoint_msg(const char* format, ...) {
  if (ShowSafepointMsgs) {
    va_list ap;
    va_start(ap, format);
    tty->vprint_cr(format, ap);
    va_end(ap);
  }
}

#endif // !PRODUCT
