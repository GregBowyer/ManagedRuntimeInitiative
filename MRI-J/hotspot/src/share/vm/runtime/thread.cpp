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
#include "artaObjects.hpp"
#include "artaQuery.hpp"
#include "artaThreadState.hpp"
#include "attachListener.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "compileBroker.hpp"
#include "defaultStream.hpp"
#include "deoptimization.hpp"
#include "exceptions.hpp"
#include "gcTaskManager.hpp"
#include "gpgc_gcManagerMark.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_threadCleaner.hpp"
#include "gpgc_thread.hpp"
#include "hpi.hpp"
#include "init.hpp"
#include "instructionTraceRecording.hpp"
#include "interfaceSupport.hpp"
#include "interpreter_pd.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "jniHandles.hpp"
#include "jniPeriodicChecker.hpp"
#include "jvm_misc.hpp"
#include "jvm_os.h"
#include "jvmtiExport.hpp"
#include "jvmtiThreadState.hpp"
#include "linkResolver.hpp"
#include "log.hpp"
#include "management.hpp"
#include "markSweep.hpp"
#include "memprofiler.hpp"
#include "modules.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "privilegedStack.hpp"
#include "pcTasks.hpp"
#include "psTasks.hpp"
#include "resourceArea.hpp"
#include "safepoint.hpp"
#include "sharedUserData.hpp"
#include "smaHeuristic.hpp"
#include "statSampler.hpp"
#include "stubCodeGenerator.hpp"
#include "stubRoutines.hpp"
#include "surrogateLockerThread.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "threadCounters.hpp"
#include "threadService.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "utf8.hpp"
#include "vframe.hpp"
#include "vmSymbols.hpp"
#include "vmTags.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"
#include "vm_version_pd.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "os.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

extern outputStream* gclog_or_tty;  // stream for gc log if -Xloggc:<f>, or tty
extern void StubGenerator_install_instrumented_stubs();
// extern void start_arta(); 20100124 Bug 25480 move initialization back to start of backend process

// Class hierarchy
// - Thread
//   - VMThread
//   - WatcherThread
//   - JavaThread
//     - CompilerThread
//       - C1CompilerThread
//       - C2CompilerThread
static const int MMU_BUF_SIZ=(1<<19);// Ring buffer size


// ======= Thread ========

Thread::Thread() {
  // Initialize the tags to intializing.
  set_vm_tag(VM_ThreadStart_tag);
  
  // stack  
  _stack_base   = NULL;
  _stack_size   = 0;
  _lgrp_id      = -1;
  _osthread     = NULL;
  
  _debug_level  = 0;

  _rand_seed = (int32_t)((intptr_t)this ^ (intptr_t)os::elapsed_counter()) | 1; // set the per-thread random seed and ensure that it can never be 0
  
  // allocated data structures
  set_resource_area(new ResourceArea());
  set_handle_area(new HandleArea(NULL));
  set_active_handles(NULL); 
  set_free_handle_block(NULL);
  set_last_handle_mark(NULL);
  set_osthread(NULL);

  // the handle mark links itself to last_handle_mark
  new HandleMark(this);

  // plain initialization  
  debug_only(_owned_locks = NULL;) 
  debug_only(_allow_allocation_count = 0;)
  NOT_PRODUCT(_allow_safepoint_count = 0;)  
  _jvmti_env_iteration_count = 0;
  _no_tlab_parking = 0;
  _vm_operation_started_count = 0;
  _vm_operation_completed_count = 0;
  _thread_counters = new ThreadCounters();
  _current_pending_monitor = NULL;
  _current_pending_monitor_is_from_java = true;
  _current_waiting_monitor = NULL;
_sba_thread=NULL;


_new_gen_ref_buffer=NULL;
_old_gen_ref_buffer=NULL;

  if ( UseGenPauselessGC ) {
    if ( is_init_completed() ) {
      _new_gen_ref_buffer = GPGC_GCManagerMark::alloc_mutator_stack();
      _old_gen_ref_buffer = GPGC_GCManagerMark::alloc_mutator_stack();
    } else {
      _new_gen_ref_buffer = new HeapRefBuffer();
      _old_gen_ref_buffer = new HeapRefBuffer();
    }
  }

  _live_objects = new LiveObjects();

  // thread-specific hashCode stream generator state - Marsaglia shift-xor form
  _hashStateX = os::random() ; 
  _hashStateY = 842502087 ; 
  _hashStateZ = 0x8767 ;    // (int)(3579807591LL & 0xffff) ; 
  _hashStateW = 273326509 ; 

  _self_suspend_monitor = new WaitLock(CodeCache_lock._rank, "self_suspend_monitor", false);

  _profiling_timer_id = 0;

assert(!_is_Complete_Thread,"Already set true?!?!");
  _is_Complete_Thread = true;
  Atomic::membar();
}

void Thread::initialize_thread_local_storage() {
  // set up any platform-specific state.
  os::initialize_thread();

  // by default, threads are not GC mode
  set_gc_mode(false);
}


Thread::~Thread() {
  assert(!owns_locks() || !is_VM_thread(), "must release all locks before blocking");
  _is_Complete_Thread = false;
  Atomic::membar();

  delete _self_suspend_monitor;
  _self_suspend_monitor = 0;

  // deallocate data structures
  delete resource_area();
set_resource_area(NULL);
  // since the handle marks are using the handle area, we have to deallocated the root
  // handle mark before deallocating the thread's handle area,
  assert(last_handle_mark() != NULL, "check we have an element");
  delete last_handle_mark();
  assert(last_handle_mark() == NULL, "check we have reached the end");

  delete handle_area();

  delete _live_objects;
_live_objects=NULL;

  // osthread() can be NULL, if creation of thread failed.
  if (osthread() != NULL) os::free_thread(osthread());

  // We need to make sure we're not leaking unmarked refs when a thread is destroyed.
  if ( UseGenPauselessGC ) {
    guarantee(get_new_gen_ref_buffer()->is_empty(), "Shouldn't have buffered unmarked through refs in Thread destructor.");
    guarantee(get_old_gen_ref_buffer()->is_empty(), "Shouldn't have buffered unmarked through refs in Thread destructor.");
    GPGC_GCManagerMark::free_mutator_stack(get_new_gen_ref_buffer());
    GPGC_GCManagerMark::free_mutator_stack(get_old_gen_ref_buffer());
_new_gen_ref_buffer=NULL;
_old_gen_ref_buffer=NULL;
  }

  // Null out the thread counters before starting to delete them since we may
  // get a tick while deleting ourself.
  ThreadCounters *tmp = _thread_counters;
_thread_counters=NULL;
  delete tmp;
  
  CHECK_UNHANDLED_OOPS_ONLY(if (CheckUnhandledOops) delete unhandled_oops();)
}

// NOTE: dummy function for assertion purpose.
void Thread::run() {
  ShouldNotReachHere();
}

#ifdef ASSERT
// Private method to check for dangling thread pointer
void check_for_dangling_thread_pointer(Thread *thread) {
assert(!thread->is_Java_thread()||Thread::current()==thread||Threads_lock.owned_by_self(),
         "possibility of dangling Thread pointer");
}
#endif


// Hand off any unmarked refs to the garbage collector, and return the number of refs.
void Thread::flush_unmarked_refs(){
  long           new_count=0;
  long           old_count=0;

  if ( UseGenPauselessGC ) {
    new_count = get_new_gen_ref_buffer()->get_top();
    if (new_count != 0 ) { 
      GPGC_GCManagerNewStrong::push_mutator_stack_to_full(get_new_gen_ref_buffer_addr()); 
    }
    assert0(get_new_gen_ref_buffer()->get_top() == 0);

    old_count += get_old_gen_ref_buffer()->get_top();
    if (old_count != 0 ) { 
      GPGC_GCManagerOldStrong::push_mutator_stack_to_full(get_old_gen_ref_buffer_addr()); 
    }
    assert0(get_old_gen_ref_buffer()->get_top() == 0);
  }
  else {
    assert0(get_new_gen_ref_buffer() == NULL);
    assert0(get_old_gen_ref_buffer() == NULL);
    new_count = 0;
    old_count = 0;
  }

  if ( Verbose && PrintGCDetails && UseGenPauselessGC && (new_count>0 || old_count>0) ) {
    gclog_or_tty->print_cr("flushed from thread " PTR_FORMAT ": unmarked through new refs %ld, unmarked through old refs %ld", this, new_count, old_count );
  }

}


#ifndef PRODUCT
// Tracing method for basic thread operations
void Thread::trace(const char* msg, const Thread* const thread) {    
  if (!TraceThreadEvents) return;
  ResourceMark rm;
MutexLocker ml(ThreadCritical_lock);
  const char *name = "non-Java thread";
  int prio = -1;
  if (thread->is_Java_thread() 
      && !thread->is_Compiler_thread()) {
    // The Threads_lock must be held to get information about
    // this thread but may not be in some situations when
    // tracing  thread events.
MutexLockerNested ml(Threads_lock);
    JavaThread* jt = (JavaThread *)thread;
    name = (char *)jt->get_thread_name();
    oop thread_oop = jt->threadObj();
if(thread_oop!=NULL)
      prio = java_lang_Thread::priority(thread_oop); 
  }    
tty->print_cr("Thread::%s "PTR_FORMAT" [%lx] %s (prio: %d)",msg,thread,(long unsigned int)thread->osthread()->thread_id(),name,prio);
}
#endif


JavaThreadPriority Thread::get_priority(const Thread*const thread){
  // TODO: This is lame: We should be returning an OSThreadPriority, not
  // JavaThreadPriority.  There should be a separate call for
  // JavaThread::get_java_priority() for anyone who needs a JavaThreadPriority.
  // Making that change would require cleaning up the thread printing and XML
  // generation, so I'm deferring it.
  trace("get priority", thread);  
JavaThreadPriority priority;
  // Can return an error!
  (void)os::get_priority(thread, priority);  
  assert(MinPriority <= priority && priority <= MaxPriority, "non-Java priority found");
  return priority;
}


void Thread::start(Thread* thread) {
  trace("start", thread);
  // Start is different from resume in that its safety is guaranteed by context or
  // being called from a Java method synchronized on the Thread object.
  if (!DisableStartThread) {
    if (thread->is_Java_thread()) {
      // Initialize the thread state to RUNNABLE before starting this thread.
      // Can not set it after the thread started because we do not know the
      // exact thread state at that time. It could be in MONITOR_WAIT or
      // in SLEEPING or some other state.
      java_lang_Thread::set_thread_status(((JavaThread*)thread)->threadObj(), 
 					  java_lang_Thread::RUNNABLE);
    }
    os::start_thread(thread);
  }
}

void Thread::set_sba_thread(JavaThread*thread){
_sba_thread=thread;
}

void Thread::set_abase(JavaThread*thread){
  assert0( Thread::current() == this );
  // this has no use anymore and probably should not be called
}

// Save & Restore the _sba_thread around some stack-walk.  Allows a one thread
// (VM thread or GC thread or via JVMTI a JavaThread) to directly access
// thread-local stack objects which normally have non-unique (i.e.,
// thread-local) names.
class SetSBAThreadMark:public StackObj{
  bool _self_is_remote;
JavaThread*_old;
 public:
SetSBAThreadMark(JavaThread*jt){
Thread*self=Thread::current();
    _old=0;
    // Never change self-sba; this conflicts with lazy-SBAArea allocation
    _self_is_remote = (self != jt);
    if( UseSBA && _self_is_remote ) { // Is this a remote-thread stack access?
      _old = self->sba_thread();
      self->set_sba_thread(jt);
      self->set_abase     (jt);
    } 
  }
  ~SetSBAThreadMark() {
    if( UseSBA && _self_is_remote ) {
Thread*self=Thread::current();
      self->set_sba_thread(_old);
      self->set_abase     (_old);
    }
  }
};


int64_t Thread::active_handle_count() const {
  CountOopsClosure count_oops;
  active_handles()->oops_do(&count_oops);
return count_oops.count();
}


//=============================================================================
// Came here because a native failed to retake the jvmlock (usually because PAS is set)
void JavaThread::jvm_lock_self_or_suspend_static_nat( JavaThread *t ) { t->jvm_lock_self_or_suspend(); }
// Came here because we fielded a safepoint interrupt
void JavaThread::jvm_lock_self_or_suspend_static_irq( JavaThread *t ) { t->jvm_lock_self_or_suspend(); }

// Grab the jvm-lock for self, or suspend in the attempt.  Because of race
// conditions it is possible to make it out of here with a self-suspend
// request pending.  This just means we barely made it out "under the wire"
// and we get to run a bit before we poll and suspend later.
bool JavaThread::jvm_lock_self_or_suspend() {
  int spin_cnt=0;
  debug_only( if( GCALotAtAllSafepoints ) InterfaceSupport::check_gc_alot() );
  if( _deopt_buffer ) {         // Free this at any convenient moment after the last deopt
    delete _deopt_buffer;
_deopt_buffer=NULL;
  }

  while( 1 ) {
    // I'm about to begin a round of polling.  Clear any "please poll before
    // locking" bit in the jvm_lock.  Note that only the 'self' thread can
    // clear this bit, or we have a race between the self-thread installing a
    // pending-exception (and setting PAS) and exiting a native wrapper and
    // checking for the pending-exception.  Note that is the *only* place this
    // bit is cleared, as a means of controlling the race conditions.
    intptr_t j = _jvm_lock;
    if( (j & 2) == 2 ) {        // Has the "please poll" bit
      while( (j & 2) == 2 &&    // Has the "please poll" bit
             !jvm_lock_update_attempt(j & ~2, j) ) {
        j = _jvm_lock;		// Contention; reload
      }
    }
    // Either the VM thread grabbed the lock, or its free and the "please
    // poll" bit got cleared.  It can be reset at any moment, but only after
    // another polling condition arises.  The polling condition can be
    // instantly cleared (imagine racing j.l.T.suspend/resume requests) so
    // there's not much that can be asserted here.

    // Read the "_please_self_suspend" field only once to get a consistent
    // picture for flags handled when the JVM lock isn't held by the thread.
    // Remember: it's being continously asynchrounously updated by
    // other threads.  We respond to those updates on a "best effort" basis.
    int self_suspend_flags = please_self_suspend();

    // A request was made to profile time-to-safepoint for this thread.  This needs to
    // be checked prior to other flags that might cause time delays (like safep_suspend),
    // or the time-to-safepoint calculation will be off.
    if (self_suspend_flags & ttsp_suspend ) {
      TickProfiler::ttsp_evaluate(this);
      clr_suspend_request(ttsp_suspend);
    }

    // A request was made to come to a Safepoint.  The requestor will clear
    // the safep_suspend bit when it's ready.  Due to race conditions, it
    // could be set again just after exiting "block", so we cannot assert that
    // the condition has cleared.
    if (self_suspend_flags & safep_suspend) {
      mmu_start_pause();
      SafepointSynchronize::block(this);
      clr_suspend_request(prmpt_suspend); // Since we blocked, preemption was also handled
      mmu_end_pause();
    }

    // A request was made to come to externally suspend this thread.  The
    // requestor will clear the suspend bits when it's ready.  Due to race
    // conditions, they could be set again just after exiting "self_suspend",
    // so we cannot assert that the conditions have cleared.
    if (self_suspend_flags & (intrp_suspend|jvmti_suspend|jlang_suspend|print_suspend)) {
      self_suspend();
      continue;			// Re-poll from the get-go
    }

    // The OS requested a cooperative suspend request
    if (self_suspend_flags & prmpt_suspend) {
      mmu_start_pause();
      if (ParkTLAB) {
tlab().park();
      }  
      clr_suspend_request(prmpt_suspend);
      TickProfiler::meta_tick(thread_preempted_tick, 2);
      pd_clr_hardware_poll_at_safepoint();
os::yield();
      TickProfiler::meta_tick(thread_scheduled_tick, 2);
      mmu_end_pause();
      if( mmu() ) {
        jlong last_pause = mmu()->p_len[(mmu()->head-1)&(MMU_BUF_SIZ-1)];
        if( last_pause > 5332800 )
          tty->print_cr("Preempted for %ld ms",os::ticks_to_millis(last_pause));
      }
      if (ParkTLAB) {
tlab().unpark();
      }  
    }


    // We've made one attempt to handle all polling conditions.  More (async)
    // conditions can arise at any time, and there's no guarentee about the
    // timeliness of handling them, so we might as well try to grab the
    // jvm_lock right now.  If it succeeds, then late arriving async polling
    // conditions will have to await the next safepoint poll.
    if( jvm_lock_self_attempt() ) {
      // Got it!

      // Read the _please_self_suspend field only once to get a consistent
      // picture for flags handled when the JVM lock is held by the thread.
      self_suspend_flags = please_self_suspend();
      Atomic::read_barrier();
      // Please DO NOT RE-READ please_self_suspend() or the flags will NOT BE
      // CONSISTENT.  Example: This thread is unlocking jvm for some other
      // reason (e.g. call to wait()) and is attempting to re-lock.  It reads
      // the _please_self_suspend field above and finds that it is clear.
      // Next the self thread takes it's JVM lock and gets here.  In-between,
      // GC has set the 'gpgc_clean_new' AND the 'rstak_suspend' flags.  Here,
      // however, only the gpgc_clean_new is checked - so the rstack is NOT
      // flushed and the gpgc_clean writes are trashed by the rstack hardware.
      bool gpgc_clean_new_stack = self_suspend_flags & gpgc_clean_new;
      bool gpgc_clean_old_stack = self_suspend_flags & gpgc_clean_old;
      bool checkpoint_requested = self_suspend_flags & checkp_suspend;
      bool flush_the_rstack     = self_suspend_flags & rstak_suspend;
      bool gc_pause = gpgc_clean_new_stack | gpgc_clean_old_stack | checkpoint_requested;
      if( gc_pause ) mmu_start_pause();

      // When a thread has grabbed its _jvm_lock, the stack_is_clean flag is cleared to
      // let GC know that the thread's stack may again contain items unseen by GC.
      if (self_suspend_flags & stack_is_clean) {
        // Note: stack_is_clean is only used by PGC, and so can be deleted once we stop supporting PGC.
        clr_suspend_request(stack_is_clean);
      }

      // If the collector cleans our thread stack for us, it'll clear the gpgc_clean_*
      // flags while holding the thread's JVM lock.
      if ( gpgc_clean_new_stack | gpgc_clean_old_stack ) {
        // Our thread stack may have invalid NewGen or OldGen heapRefs, clean up before touching the stack.
        GPGC_ThreadCleaner::LVB_thread_stack(this);
        clr_suspend_request((JavaThread::SuspendFlags)(gpgc_clean_new | gpgc_clean_old));
        flush_the_rstack = true;
      }

      // we need as close as possible to a straightline between setting do_checkpoint
      // and actually executing the checkpoint. We cannot block, loop, or return between
      // the setting of do_checkpoint and the execution of the checkpoint
      if (checkpoint_requested) {
        SafepointSynchronize::block_checkpoint(this);
        // Make sure any result of the checkpoint is visible prior to clearing the flag:
        Atomic::write_barrier();
        clr_suspend_request(checkp_suspend);
        SafepointSynchronize::reset_priority_if_needed(this);
        flush_the_rstack = true;
      }

      if (flush_the_rstack) {    // Somebody's hacked our rstack in memory.
        clr_suspend_request(rstak_suspend);
        // May not need fence after clear, but this isn't performance critical.
        Atomic::write_barrier();
        Atomic::flush_rstack(); // Force hardware to be sync'd with memory.
        AuditTrail::log_time(this, AuditTrail::MAKE_SELF_WALKABLE, 5);
      }
      // Rstack flushing is done: no more rstack writes please - they might be ignored!
      NOT_PRODUCT(if( gc_pause ) mmu_end_pause());
        
      // CREATE (can GC!) and install an asynchronous unsafe exception
      if (self_suspend_flags & unsaf_suspend) {
        clr_suspend_request(unsaf_suspend);
        // May not need fence after clear, but this isn't performance critical.
        Atomic::write_barrier();
        set_pending_unsafe_access_error();
      }
      // Have we hit a stack overflow ?
      if (self_suspend_flags & stack_suspend) {
        clr_suspend_request(stack_suspend);
        // May not need fence after clear, but this isn't performance critical.
        Atomic::write_barrier();
        set_pending_stack_overflow_error();
      }
      // Async suspend just means we need to check the _pending_async_exception
      // field for an asynchronous exception (e.g. Thread.death()).  Compiled
      // code will need to deopt.  The deopt logic will check and throw the
      // exception when we exit from here.
      if (self_suspend_flags & async_suspend) {
        // Do nothing here; We separately always check _pending_async_exception every time we
        // acquire the JVM lock.
        clr_suspend_request(async_suspend);
        // May not need fence after clear, but this isn't performance critical.
        Atomic::write_barrier();
      }
      // Unbias suspend means we hold some locks biased towards ourselves and
      // some other thread would like a chance to own the lock.  We need to
      // revoke the bias for things on the _unbias list.
      if (self_suspend_flags & unbias_suspend ) {
        clr_suspend_request(unbias_suspend);
        // Annoying lock acquire to allow easy linked-list manipulation
        MutexLockerAllowGC mx(Threads_lock, this);
        unbias_all();
      }

if(_pending_async_exception.not_null()){
        // Handle any async exceptions showing up
        move_pending_async_to_pending();
      }
      debug_only( if( WalkStackALot ) InterfaceSupport::walk_stack() );
      return true;
    }

    // None of the above worked.  This means there's no obvious suspend
    // request pending (but always one may come at any moment) and also we
    // couldn't get the _jvm_lock, likely because the VM holds it.  In this
    // case we just want to spin: soon enough either the VM will release the
    // _jvm_lock or it will request self-suspension.
    spin_cnt++;
    if( (spin_cnt & 63) == 0 )
      os::yield_all(spin_cnt>>3); // Give VM thread a chance to release lock
#ifndef PRODUCT
    if( spin_cnt > 90000 ) {    // Why are we heavy-spinning?
      print();
VM_Operation*vm_op=VMThread::vm_operation();
if(vm_op==NULL){
tty->print_cr("No VM_Operation on VMThread.");
      } else {
vm_op->print(tty);
      }
    }
#endif // ! PRODUCT
assert(spin_cnt<100000,"I've been spinning a long time waiting for the _jvm_lock to become free or the VM to request me to self-suspend");
  }
}

void JavaThread::poll_at_safepoint_static(JavaThread*jt){
jt->poll_at_safepoint();
}

// This routine and java_resume below are in a funny race.  A remote thread
// can issue a external suspend followed by an external resume, and we'd like
// the receiving thread to see both or neither.  The suspend is polled and
// hence delayed.  The resume is asynchronous and can in theory blow past the
// pending suspend: this would allow the suspending thread to see a resume
// with no suspend (yet), blow off the resume, suspend and never get another
// resume and so hang.  

// Instead, we inspect the external suspend bits in the while/wait loop below
// while holding the self_suspend lock.  If the bits are clear we do not need
// to wait() (ie. suspend) because a racing resume has cleared the condition
// already.  If we are pre-empted between the is_begin_ext_suspended() test
// and the wait() call then we hold the self-suspend lock.  A racing resume()
// call will hang on the lock until we wait() and then we will not miss the
// corresponding notify().

void JavaThread::self_suspend(){
assert(Thread::current()==this,"self-suspension only");
assert(!owns_locks(),"must release all locks before blocking");
  // We do not hold the JVM lock here (our caller will retake it after
  // all polling conditions are handled).  Hence we cannot use the
  // normal lock() call, as it expects (and properly handles) us to
  // hold the JVM lock.
  MutexLocker ml(*_self_suspend_monitor );
  while( is_being_ext_suspended() || // Spurious wakeups happen, so...
         (please_self_suspend() & print_suspend) )
    _self_suspend_monitor->wait(); // keep waiting.
}

// Resume request from java.lang.Thread.resume (or jvmdi resume).
// Clears all pending java.lang.Thread.suspends (or jvmdi suspends).
void JavaThread::java_resume( enum SuspendFlags f ) {
  assert( f == jvmti_suspend || f == jlang_suspend || f == print_suspend || f == intrp_suspend, "this routine only for resuming from extern" );

  // Spin till the flag clears by someone else, or we get it
  jlong x = please_self_suspend();
  while( 1 ) {
    if( !(x & f) ) return; // No suspend request pending, so nothing to resume
    if( x == Atomic::cmpxchg(x&~f,&_please_self_suspend,x) )
      break;			// Got it!
    x = please_self_suspend();	// Contention, retry
  }
  // If we get here, we did a successful 1->0 transition.  Do not re-read the
  // _please_self_suspend field.  If we did the 1->0 transition, then we are
  // responsible for resuming.  
  if( (x&~f) & (jvmti_suspend|jlang_suspend|print_suspend|intrp_suspend) )
    return;			// Still reasons to stay suspended
  // No reason to stay suspended, so notify him to wake up.
  MutexLocker ml(*_self_suspend_monitor );
_self_suspend_monitor->notify();
}


// Wait for a suspend request to complete (or be cancelled).
// Returns true if the thread is suspended and false otherwise.

//
// Note that the original code was full of race conditions, and so is this
// code.  Conditions can change between this test and what the caller sees.
bool JavaThread::wait_for_suspend_completion(bool retry) {
  
  // Experimental number: cannot allow wait_for_suspend_completion() to
  // run forever or we could hang.
  int SUSPENDRETRYCOUNT=100;
  
  if (!retry) {
    SUSPENDRETRYCOUNT = 1;
  }

for(int i=1;i<SUSPENDRETRYCOUNT;i++){
    if (is_exiting()) // thread is in the process of exiting so the wait is done
      return false;
    if( !is_being_ext_suspended() )
      return false;		// Another thread canceled the suspend
    if( set_jvm_lock_poll_advisory() ) 
      return true;		// Thread in native or possibly VM; will check
				// on exit or while retaking the lock.  In any
				// case, it is safe to walk this thread's stack.

Thread*thr=Thread::current();
    JavaThread *jt = thr->is_Java_thread() ? (JavaThread*)thr : NULL;
    if( jt ) jt->jvm_unlock_self();
os::yield_all(i);//TODO: consider the following, there are issues with this call and Linux
    // // Yield for 5ms for each retry
    // { MutexLocker ml(*_self_suspend_monitor );
    //   _self_suspend_monitor->wait_micros(i*5*1000, true);
    // }
    if( jt ) jt->jvm_lock_self();
  }

  return false;			// Timed out
}

// Release the jvm_lock and sleep (interruptably).  Return os::sleep's result.
int JavaThread::sleep( jlong millis ) {
  ThreadBlockInVM tbivm(this,"sleeping");
  return os::sleep(this,millis,true);
}


void Thread::interrupt(Thread* thread) {
  trace("interrupt", thread);
  debug_only(check_for_dangling_thread_pointer(thread);)
  os::interrupt(thread);
}

bool Thread::is_interrupted(Thread* thread) {
  trace("is_interrupted", thread);  
  debug_only(check_for_dangling_thread_pointer(thread);)
return os::is_interrupted(thread);
}


// GC Support
void Thread::oops_do(OopClosure* f) {
  // We should not set an sba thread here, non java threads should still be able to use this code.
  assert(!UseSBA || !this->is_Java_thread() || !((JavaThread*)this)->sba_area() || 
         this == Thread::current()->sba_thread(), "Java threads must have sba_thread set");

  active_handles()->oops_do(f);
  // Do oop for ThreadShadow
f->do_oop(&_pending_exception);
  handle_area()->oops_do(f);
}

void Thread::methodCodes_do(){
}

void Thread::print_on(outputStream* st) const {
  // get_priority assumes osthread initialized
  if (osthread() != NULL) {
st->print("prio=%d tid="PTR_FORMAT" ",get_priority(this),this);
    osthread()->print_on(st);
  }
}

// Thread::print_on_error() is called by fatal error handler. Don't use
// any lock or allocate memory.
void Thread::print_on_error(outputStream* st, char* buf, int buflen) const {
  if      (is_VM_thread())                  st->print("VMThread");
  else if (is_Compiler_thread())            st->print("CompilerThread");
  else if (is_Java_thread())                st->print("JavaThread");
  else if (is_GC_task_thread())             st->print("GCTaskThread");
  else if (is_Watcher_thread())             st->print("WatcherThread");
  else st->print("Thread");

  st->print(" [stack: " PTR_FORMAT "," PTR_FORMAT "]",
            _stack_base - _stack_size, _stack_base);

  if (osthread()) {
    st->print(" [id=%d]", osthread()->thread_id());
  }
}


void Thread::state_print_xml_on(xmlBuffer *xb, const char *name, bool dostate) {
  ArtaThreadState *state = NULL;

  if (is_Java_thread()) {
JavaThread*java_thread=(JavaThread*)this;
    // Determine the state of the thread up-front so we can filter it out before
    // we've started spitting out any XML.
state=ArtaThreadState::get(java_thread);
  }

xb->name_value_item("id",osthread()->thread_id());
  if (xb->can_show_addrs()) xb->name_ptr_item("address", this);
  xb->name_value_item("name",name);
  xb->name_value_item("priority", Thread::get_priority(this));

  // Note: Originally, in a deadlock condition, the state object
  // sometimes get corrupted. This feature, optional state, was
  // added to mitigate needing the sometimes corrupted state.
  // The corruption has been fixed, but this feature is kept.
  if ((dostate) && (state != NULL)) state->to_xml(xb->response());

  { xmlElement xe(xb, "thread_counters");
    xb->name_value_item("running_ticks", "n/a");
    xb->name_value_item("object_blocked_ticks", os::ticks_to_millis(thread_counters()->object_blocked_ticks()));
    xb->name_value_item("object_wait_ticks", os::ticks_to_millis(thread_counters()->object_wait_ticks()));
    xb->name_value_item("rpc_wait_ticks", os::ticks_to_millis(thread_counters()->rpc_wait_ticks()));
    xb->name_value_item("cpu_wait_ticks", os::ticks_to_millis(thread_counters()->cpu_wait_ticks()));
    xb->name_value_item("cpu_ticks", os::thread_cpu_time_millis(this));
    xb->name_value_item("wall_ticks", os::ticks_to_millis(thread_counters()->wall_ticks()));
  }
}


#ifdef ASSERT
void Thread::print_owned_locks_on(outputStream* st) const {  
AzLock*cur=_owned_locks;
  if (cur == NULL) {
st->print_cr(" (no locks)");
  } else {
    st->print_cr(" Locks owned:");
    bool jvmlock = is_Java_thread() && ((JavaThread*)this)->jvm_locked_by_self();
    while(cur) {
      if( cur->_rank > AzLock::JVMLOCK_rank && jvmlock ) {
        st->print_cr("AzLock: [0x0] jvmlock - rank: %d",AzLock::JVMLOCK_rank);
        jvmlock = false;
      }
      cur->print_on(st);
      cur = cur->next();
    }
  }
}
#endif

#ifndef PRODUCT
// The flag: potential_vm_operation notifies if this particular safepoint state could potential
// invoke the vm-thread (i.e., and oop allocation). In that case, we also have to make sure that
// no threads which allow_vm_block's are held
void Thread::check_for_valid_safepoint_state(bool potential_vm_operation) {  
  // Check if current thread is allowed to block at a safepoint
  if (!(_allow_safepoint_count == 0))
    fatal("Possible safepoint reached by thread that does not allow it");
if(is_Java_thread()&&!((JavaThread*)this)->jvm_locked_by_self())
    fatal("LEAF method calling lock?");
  
#ifdef ASSERT    
if(potential_vm_operation&&is_Java_thread()&&!Universe::is_bootstrapping()){
    // Make sure we do not hold any locks that the VM thread also uses.
    // This could potentially lead to deadlocks      
for(AzLock*cur=_owned_locks;cur;cur=cur->next())
if(cur->_rank<AzLock::JVMLOCK_rank)
        warning("Thread holding lock at safepoint that vm can block on: %s", cur->name());          
  }
if(GCALotAtAllSafepoints)//We could enter a safepoint here and thus have a gc
    InterfaceSupport::check_gc_alot();
#endif
}
#endif

bool Thread::is_in_stack(intptr_t addr) const {
  assert(Thread::current() == this, "is_in_stack can only be called from current thread");
  return (intptr_t)this <= addr && addr < (intptr_t)this+thread_stack_size;
}

bool Thread::set_as_starting_thread() {
 // NOTE: this must be called inside the main thread.
  return os::create_main_thread((JavaThread*)this);
}

static void initialize_class(symbolHandle class_name, TRAPS) {
  klassOop klass = SystemDictionary::resolve_or_fail(class_name, true, CHECK);
  instanceKlass::cast(klass)->initialize(CHECK);
}


// Creates the initial ThreadGroup
static Handle create_initial_thread_group(TRAPS) {
  klassOop k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_ThreadGroup(), true, CHECK_NH);
  instanceKlassHandle klass (THREAD, k);
  
  Handle system_instance = klass->allocate_instance_handle(false/*No SBA*/, CHECK_NH);
  {
    JavaValue result(T_VOID);
    JavaCalls::call_special(&result, 
                            system_instance, 
                            klass, 
                            vmSymbolHandles::object_initializer_name(), 
                            vmSymbolHandles::void_method_signature(), 
                            CHECK_NH);
  }
  Universe::set_system_thread_group(system_instance());

  Handle main_instance = klass->allocate_instance_handle(false/*No SBA*/, CHECK_NH);
  {
    JavaValue result(T_VOID);
Handle string=java_lang_String::create_from_str("main",false/*No SBA*/,CHECK_NH);
    JavaCalls::call_special(&result,
                            main_instance,
                            klass,
                            vmSymbolHandles::object_initializer_name(),
                            vmSymbolHandles::threadgroup_string_void_signature(),
                            system_instance,
                            string,
                            CHECK_NH);
  }
  return main_instance;
}

// Creates the initial Thread
static oop create_initial_thread(Handle thread_group, JavaThread* thread, TRAPS) {  
  klassOop k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_Thread(), true, CHECK_NULL);
  instanceKlassHandle klass (THREAD, k);
instanceHandle thread_oop=klass->allocate_instance_handle(false/*No SBA*/,CHECK_NULL);

  java_lang_Thread::set_thread(thread_oop(), thread);
  thread->set_threadObj(thread_oop());

  { MutexLockerAllowGC mu(Threads_lock,thread);
    thread->set_priority(NormPriority);
  }


  Handle string = java_lang_String::create_from_str("main", false/*No SBA*/, CHECK_NULL);
  
  JavaValue result(T_VOID);
  JavaCalls::call_special(&result, thread_oop, 
                                   klass, 
                                   vmSymbolHandles::object_initializer_name(), 
                                   vmSymbolHandles::threadgroup_string_void_signature(), 
                                   thread_group, 
                                   string, 
                                   CHECK_NULL);  
  return thread_oop();
}

static void call_initializeSystemClass(TRAPS) {
  klassOop k =  SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_System(), true, CHECK);
  instanceKlassHandle klass (THREAD, k);
  
  JavaValue result(T_VOID);
  JavaCalls::call_static(&result, klass, vmSymbolHandles::initializeSystemClass_name(), 
                                         vmSymbolHandles::void_method_signature(), CHECK);
}

static void reset_vm_info_property(TRAPS) {
  // the vm info string
  ResourceMark rm(THREAD);
  const char *vm_info = VM_Version::vm_info_string();
  
  // java.lang.System class
  klassOop k =  SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_System(), true, CHECK);
  instanceKlassHandle klass (THREAD, k);
  
  // setProperty arguments
Handle key_str=java_lang_String::create_from_str("java.vm.info",false/*No SBA*/,CHECK);
Handle value_str=java_lang_String::create_from_str(vm_info,false/*No SBA*/,CHECK);

  // return value
  JavaValue r(T_OBJECT); 

  // public static String setProperty(String key, String value);
  JavaCalls::call_static(&r,
                         klass,
                         vmSymbolHandles::setProperty_name(), 
                         vmSymbolHandles::string_string_string_signature(), 
                         key_str, 
                         value_str, 
                         CHECK);  
}


void JavaThread::allocate_threadObj(Handle thread_group, const char* thread_name, bool daemon, TRAPS) {
  assert(thread_group.not_null(), "thread group should be specified");
  assert(threadObj() == NULL, "should only create Java thread object once");

  klassOop k = SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_Thread(), true, CHECK);
  instanceKlassHandle klass (THREAD, k);
instanceHandle thread_oop=klass->allocate_instance_handle(false/*No SBA*/,CHECK);

  java_lang_Thread::set_thread(thread_oop(), this);
  set_threadObj(thread_oop());

  { MutexLockerAllowGC mu(Threads_lock, this);
    this->set_priority(NormPriority);
  }

  JavaValue result(T_VOID);
  if (thread_name != NULL) {
Handle name=java_lang_String::create_from_str(thread_name,false/*No SBA*/,CHECK);
    // Thread gets assigned specified name and null target
    JavaCalls::call_special(&result,
                            thread_oop,
                            klass, 
                            vmSymbolHandles::object_initializer_name(), 
                            vmSymbolHandles::threadgroup_string_void_signature(),
                            thread_group, // Argument 1                        
                            name,         // Argument 2
                            THREAD);
  } else {
    // Thread gets assigned name "Thread-nnn" and null target
    // (java.lang.Thread doesn't have a constructor taking only a ThreadGroup argument)
    JavaCalls::call_special(&result,
                            thread_oop,
                            klass, 
                            vmSymbolHandles::object_initializer_name(), 
                            vmSymbolHandles::threadgroup_runnable_void_signature(),
                            thread_group, // Argument 1                        
                            Handle(),     // Argument 2
                            THREAD);
  }


  if (daemon) {
      java_lang_Thread::set_daemon(thread_oop());
  }

  if (HAS_PENDING_EXCEPTION) {
    return;
  }
  
  KlassHandle group(this, SystemDictionary::threadGroup_klass());
  Handle threadObj(this, this->threadObj());

  JavaCalls::call_special(&result, 
                         thread_group, 
                         group,
                         vmSymbolHandles::add_method_name(), 
                         vmSymbolHandles::thread_void_signature(), 
                         threadObj,          // Arg 1
                         THREAD);


}

// NamedThread --  non-JavaThread subclasses with multiple
// uniquely named instances should derive from this.
NamedThread::NamedThread() : Thread() {
  _name = NULL;
}

NamedThread::~NamedThread() {
  if (_name != NULL) {
    FREE_C_HEAP_ARRAY(char, _name);
    _name = NULL;
  }
}

void NamedThread::set_name(const char* format, ...) {
  guarantee(_name == NULL, "Only get to set name once.");
  _name = NEW_C_HEAP_ARRAY(char, max_name_len);
  guarantee(_name != NULL, "alloc failure");
  va_list ap;
  va_start(ap, format);  
  jio_vsnprintf(_name, max_name_len, format, ap);
  va_end(ap);
}

// ======= WatcherThread ========

// The watcher thread exists to simulate timer interrupts.  It should
// be replaced by an abstraction over whatever native support for
// timer interrupts exists on the platform.

WatcherThread* WatcherThread::_watcher_thread   = NULL;
bool           WatcherThread::_should_terminate = false;

WatcherThread::WatcherThread() : Thread() {
  assert(watcher_thread() == NULL, "we can only allocate one WatcherThread");
if(os::create_thread(this,ttype::watcher_thread)){
    _watcher_thread = this;
    
os::set_native_priority(this,WatcherThreadPriority);
    if (!DisableStartThread) {
      os::start_thread(this);
    }
  }
}

void WatcherThread::run() {
  assert(this == watcher_thread(), "just checking");

  this->initialize_thread_local_storage();
  this->set_active_handles(JNIHandleBlock::allocate_block());
  while(!_should_terminate) {    
    assert(watcher_thread() == Thread::current(),  "thread consistency check");
    assert(watcher_thread() == this,  "thread consistency check");

    // Calculate how long it'll be until the next PeriodicTask work
    // should be done, and sleep that amount of time.
    const size_t time_to_wait = PeriodicTask::time_to_wait();
    os::sleep(this, time_to_wait, false);

    if (is_error_reported()) {
      // A fatal error has happened, the error handler(VMError::report_and_die)
      // should abort JVM after creating an error log file. However in some
      // rare cases, the error handler itself might deadlock. Here we try to
      // kill JVM if the fatal error handler fails to abort in 2 minutes.
      //
      // This code is in WatcherThread because WatcherThread wakes up 
      // periodically so the fatal error handler doesn't need to do anything; 
      // also because the WatcherThread is less likely to crash than other
      // threads.

      for (;;) {
        if (!ShowMessageBoxOnError 
         && (OnError == NULL || OnError[0] == '\0') 
         && Arguments::abort_hook() == NULL) {
             os::sleep(this, 2 * 60 * 1000, false);
             fdStream err(defaultStream::output_fd());
             err.print_raw_cr("# [ timer expired, abort... ]");
             // skip atexit/vm_exit/vm_abort hooks
             os::die();
        }
 
        // Wake up 5 seconds later, the fatal handler may reset OnError or
        // ShowMessageBoxOnError when it is ready to abort.
        os::sleep(this, 5 * 1000, false);
      }
    }

    PeriodicTask::real_time_tick(time_to_wait);

    // If we have no more tasks left due to dynamic disenrollment,
    // shut down the thread since we don't currently support dynamic enrollment
    if (PeriodicTask::num_tasks() == 0) {
      _should_terminate = true;
    }

    // PauselessGC and GenPauselessGC expect that only the VMThread and JavaThreads will
    // hit LVB traps.  This sanity check makes sure that's so.
    if ( UseLVBs ) {
      if ( UseGenPauselessGC ) {
assert(get_new_gen_ref_buffer()->is_empty(),"Hit NMT LVB on WatcherThread");
assert(get_old_gen_ref_buffer()->is_empty(),"Hit NMT LVB on WatcherThread");
      }
    }
  }
  
  // Signal that it is terminated
  {
MutexLocker mu(Terminator_lock,this);
    _watcher_thread = NULL;
Terminator_lock.notify();
  }
  delete this;
}

void WatcherThread::start() {
  if (watcher_thread() == NULL) {
    _should_terminate = false;
    // Create the single instance of WatcherThread
    new (ttype::watcher_thread) WatcherThread();   
  }
}

void WatcherThread::stop() {
  // it is ok to take late safepoints here, if needed
JavaThread*jt=JavaThread::current();
MutexLockerAllowGC mu(Terminator_lock,jt);
  _should_terminate = true;  
  Atomic::membar();
  while(watcher_thread() != NULL) {
Terminator_lock.wait();
  }  
}

void WatcherThread::print_on(outputStream* st) const {
  st->print("\"%s\" ", name());
  Thread::print_on(st);
  st->cr();
}


void WatcherThread::print_xml_on( xmlBuffer *xb, bool ref ) {
  xmlElement te(xb, ref ? "thread_ref" : "thread");
  state_print_xml_on(xb, "VM Periodic Task Thread");
}


// ======= JavaThread ========

// A JavaThread is a normal Java thread

void JavaThread::initialize() {
  // Initialize fields
  set_threadObj(NULL);  
_anchor.init_to_root();
  set_entry_point(NULL);
  set_jni_functions(jni_functions());

  set_deferred_locals(NULL);
  set_next(NULL);  
  _terminated = _not_terminated;
  _privileged_stack_top = NULL;
  _doing_unsafe_access = false;
  _jvmti_thread_state= NULL;
  _jvmti_get_loaded_classes_closure = NULL;
  _interp_only_mode    = 0;
  _current_priority = os::os_priority_from_java_priority(NormPriority);
_pending_async_exception=nullRef;
  _thread_stat = NULL;
  _thread_stat = new ThreadStatistics();
_deopt_buffer=NULL;
  _hint_blocked = "starting";
  _hint_block_concurrency_msg = nullRef;
  _hint_block_concurrency_lock= nullRef;
  _hint_block_concurrency_sync= nullRef;
  _javaThreadSpillover->_is_finalizer_thread = false;
  _jni_active_critical = 0;

  reset_init_clone_copy_stats();

  _jexstk_top = (intptr_t*)(((address)this) + os::expression_stack_bottom_offset());
  _lckstk_base = _lckstk_top = _lckstk_max = NULL;

  _javaThreadSpillover->_ttsp_tick_time = 0;
  _javaThreadSpillover->_ttsp_profile_entry = NULL;
  if ( TickProfileTimeToSafepoint ) {
    _javaThreadSpillover->_ttsp_profile_entry = ProfileEntry::allocate();
  }

  _javaThreadSpillover->_sba_area = NULL;
  set_sba_thread(this);         // JavaThreads always report self
  _sba_top = nullRef;
  _sba_max = nullRef;
  _curr_fid=0;

  _unbias = NULL;               // List of monitors needing bias-revoke
  _objectLockerList = NULL;     // List of Java locks held by VM code

  _parker = Parker::Allocate(this) ; 

  _saved_registers = new RegisterSaveArea();

  debug_only(_javaThreadSpillover->_java_call_counter = 0);

  // JVMTI PopFrame support

  _popframe_condition = popframe_inactive;
  _popframe_preserved_args = NULL;
  _popframe_preserved_args_size = 0;

  set_stack_cleaning(false);
  set_mmu(ProfileMMU ? new MMU() : NULL);

#ifndef AT_PRODUCT
_audit_trail=NULL;
  if (GPGCAuditTrail) {
    _audit_trail = new AuditTrail(GPGCAuditTrailSize);
    AuditTrail::log_time(this, AuditTrail::JAVA_THREAD_START);
  }
#endif // ! PRODUCT

}


oop JavaThread::threadObj() const {
  // We can only touch references in the JavaThread if the LVB state is OK.
  // Thus we must check the CURRENT thread's LVB-ability - NOT the "this"
  // thread.
Thread*current_thread=Thread::current();
if(current_thread->is_GC_task_thread()){
    assert0(!current_thread->is_stack_cleaning()); // GC threads should never have this flag set.
}else if(current_thread->is_VM_thread()){
    // VMThread has the right UConfig state for LVB when running.
  } else if (current_thread->is_Java_thread()) {
    if (!((JavaThread*)current_thread)->jvm_locked_by_self()) {
      // Must own your _jvm_lock when running a LVB or touching oops.
      return NULL;
    }
  } else {
    // Unclear what thread called this.
    return NULL;
  }
  return lvb_ref(&_threadObj).as_oop();
}


void JavaThread::set_threadObj(oop p){
  _threadObj = objectRef(p);
}

objectRef JavaThread::hint_blocking_concurrency_msg () const { return lvb_ref(&_hint_block_concurrency_msg ); }
objectRef JavaThread::hint_blocking_concurrency_lock() const { return lvb_ref(&_hint_block_concurrency_lock); }
objectRef JavaThread::hint_blocking_concurrency_sync() const { return lvb_ref(&_hint_block_concurrency_sync); }
void JavaThread::set_hint_blocking_concurrency_msg (objectRef ref) { POISON_AND_STORE_REF(&_hint_block_concurrency_msg , ref); }
void JavaThread::set_hint_blocking_concurrency_lock(objectRef ref) { POISON_AND_STORE_REF(&_hint_block_concurrency_lock, ref); }
void JavaThread::set_hint_blocking_concurrency_sync(objectRef ref) { POISON_AND_STORE_REF(&_hint_block_concurrency_sync, ref); }


JavaThread::JavaThread(bool is_attaching) : Thread() {
  _javaThreadSpillover = new JavaThreadSpillover();
  _jvm_lock = 0;
  initialize();
  _is_attaching = is_attaching;

assert(!_is_Complete_Java_thread,"Already set true?!?!");
  _is_Complete_Java_thread = true;
  Atomic::membar();
}


// Attempt to reguard the stack after a stack overflow may have occurred.
// Returns true if it was possible to "reguard" (i.e. unmap both the user and
// expression stack yellow zones) because neither stack's usage was still
// inside those areas, or if the stack was already guarded.  Returns false if
// the user stack usage was within 0x1000 bytes of the user stack yellow zone,
// or if the expression stack usage was still within the expression stack
// yellow zone, in which case the caller should unwind a frame and try again.
bool JavaThread::stack_is_good() {
  // If in the yellow zone, then we have more work to do...
  if( is_in_yellow_zone((ThreadBase*)this) ) 
    // Stack is only good if we can reguard the yellow zone
    return !thread_stack_yellow_zone_reguard((intptr_t)_jexstk_top);
  return true;                  // stack is good!
}


void JavaThread::block_if_vm_exited() {
  if (_terminated == _vm_exited) {
    // _vm_exited is set at safepoint, and Threads_lock is never released
    // we will block here forever
    Threads_lock.lock_allowing_gc(this);
    ShouldNotReachHere();
  }
}


static void compiler_thread_entry(JavaThread* thread, TRAPS);

JavaThread::JavaThread(ThreadFunction entry_point, size_t stack_sz) : Thread() {
  _javaThreadSpillover = new JavaThreadSpillover();
  _jvm_lock = 0;
  initialize();
  _is_attaching = false;
  set_entry_point(entry_point);
  // Create the native thread itself.
  // %note runtime_23
ttype::ThreadType thr_type=ttype::java_thread;
  thr_type = entry_point == &compiler_thread_entry ? ttype::compiler_thread : ttype::java_thread;
  if ( ! os::create_thread(this, thr_type, stack_sz) )
    return;

  // Note: Move log to after creation as otherwise the OS Thread *will* always be NULL ...
  Log::log3(NOTAG, Log::M_THREAD | Log::L_LO, TraceThreadEvents, "creating thread %p", this);
  // WARNING: This Log event is generated within the Threads_lock. Is that a good idea ?

assert(!_is_Complete_Java_thread,"Already set true?!?!");
  _is_Complete_Java_thread = true;
  Atomic::membar();

  // Notes on why an OutOfMemoryError is not thrown here:
  //
  // The _osthread may be NULL here because we ran out of memory (too many threads active).
  // We need to throw an OutOfMemoryError - however we cannot do this here because the caller
  // may hold a lock and all locks must be unlocked before throwing the exception (throwing
  // the exception consists of creating the exception object & initializing it, initialization
  // will leave the VM via a JavaCall and then all locks must be unlocked).
  //
  // The thread is still suspended when we reach here. Thread must be explicit started
  // by creator! Furthermore, the thread must also explicitly be added to the Threads list
  // by calling Threads:add. The reason why this is not done here, is because the thread 
  // object must be fully initialized (take a look at JVM_Start)  
}

JavaThread::~JavaThread() {
  TickProfiler::meta_tick(thread_exit_tick, 0);
  assert0( !_is_Complete_Java_thread ); // already cleaned out by the time we get here

  Log::log3(NOTAG, Log::M_THREAD | Log::L_LO, TraceThreadEvents, "terminate thread %p", this);

  if( UseSBA && PrintSBAStatistics && sba_area() && sba_area()->was_used() ) {
    ResourceMark rm;
    const char *name = get_thread_name();
    if( !name ) name = "(NULL)";
ttyLocker ttylock;
tty->print("Thread "PTR_FORMAT" ",this);
    sba_area()->print_statistics(NULL);
tty->print_cr(" \"%s\"",name);
  }

  // JSR166 -- return the parker to the free list
  Parker::Release(_parker);
  _parker = NULL ; 

#ifndef PRODUCT
  if( mmu() ) {
    delete mmu();                  // Fold local results into global
set_mmu(NULL);
  }
#endif

  delete _saved_registers; // make sure to throw away register save area to prevent leak
_saved_registers=NULL;

  GrowableArray<jvmtiDeferredLocalVariableSet*>* deferred = deferred_locals();
  if (deferred != NULL) {
    // This can only happen if thread is destroyed before deoptimization occurs.
    assert(deferred->length() != 0, "empty array!");
    do {
      jvmtiDeferredLocalVariableSet* dlv = deferred->at(0);
      deferred->remove_at(0);
      // individual jvmtiDeferredLocalVariableSet are CHeapObj's
      delete dlv;
    } while (deferred->length() != 0);
    delete deferred;
  }
  
  // All Java related clean up happens in exit  
  if (_thread_stat != NULL) delete _thread_stat;

  if (jvmti_thread_state() != NULL) {
    JvmtiExport::cleanup_thread(this);
  }
  pd_destroy();

  if( sba_area() ) delete sba_area();
  delete _javaThreadSpillover;
_javaThreadSpillover=NULL;
  
#ifndef AT_PRODUCT
if(_audit_trail!=NULL){
    delete _audit_trail;
_audit_trail=NULL;
  }
#endif // ! PRODUCT
}


// The first routine called by a new Java thread
void JavaThread::run() {
  // initialize thread-local alloc buffer related fields
  this->initialize_tlab();
  this->pd_initialize();

  // used to test validitity of stack trace backs
  this->record_base_of_stack_pointer();
  
  // Initialize thread local storage; set before calling MutexLocker
  this->initialize_thread_local_storage();  

  assert(JavaThread::current() == this, "sanity check");
  assert(!Thread::current()->owns_locks(), "sanity check");

  // This operation might block. We call that after all safepoint checks for a new thread has
  // been completed.
  this->set_active_handles(JNIHandleBlock::allocate_block());
  
  if (JvmtiExport::should_post_thread_life()) {
    JvmtiExport::post_thread_start(this);
  }

  // We call another function to do the rest so we are sure that the stack addresses used
  // from there will be lower than the stack base just computed
  thread_main_inner();

  // Note, thread is no longer valid at this point!
}


void JavaThread::thread_main_inner() {  
  assert(JavaThread::current() == this, "sanity check");
  assert(this->threadObj() != NULL, "just checking");

#ifdef AZ_PROXIED
  if ( ParkTLAB && (! this->is_Compiler_thread()) ) {
    proxy_error_t proxy_err = proxy_register_blocking_callback(&ThreadLocalAllocBuffer::parking_callback);
    if (proxy_err != 0) {
tty->print_cr("Parking callback registration falied");
    }
  }
#endif // AZ_PROXIED

  // Execute thread entry point. If this thread is being asked to restart, 
  // or has been stopped before starting, do not reexecute entry point.
  // Note: Due to JVM_StopThread we can have pending exceptions already!
  if (!this->has_pending_exception() && !java_lang_Thread::is_stillborn(this->threadObj())) {
    // This marks the thread as "Running" as opposed to "Starting".
    hint_unblocked();
    // enter the thread's entry point only if we have no pending exceptions
    HandleMark hm(this);    
    this->entry_point()(this, this);
  }
  
#ifdef AZ_PROXIED
  if ( ParkTLAB && (! this->is_Compiler_thread()) ) {
    proxy_error_t proxy_err = proxy_unregister_blocking_callback();
    if (proxy_err != 0) {
tty->print_cr("Parking callback unregister falied");
    }
  }
#endif // AZ_PROXIED
  
  this->exit(false);
  delete this;
}

  
static void ensure_join(JavaThread* thread) {
  // We do not need to grap the Threads_lock, since we are operating on ourself.
  Handle threadObj(thread, thread->threadObj());
  assert(threadObj.not_null(), "java thread object must exist");
ObjectLocker lock(threadObj());
  // Ignore pending exception (ThreadDeath), since we are exiting anyway
  thread->clear_pending_exception();
  // It is of profound importance that we set the stillborn bit and reset the thread object, 
  // before we do the notify. Since, changing these two variable will make JVM_IsAlive return
  // false. So in case another thread is doing a join on this thread , it will detect that the thread 
  // is dead when it gets notified.
  java_lang_Thread::set_stillborn(threadObj());
  // Thread is exiting. So set thread_status field in  java.lang.Thread class to TERMINATED.
  java_lang_Thread::set_thread_status(threadObj(), java_lang_Thread::TERMINATED);
  java_lang_Thread::set_thread(threadObj(), NULL);
  lock.notify_all(thread);
  // Ignore pending exception (ThreadDeath), since we are exiting anyway
  thread->clear_pending_exception();
}

// For any new cleanup additions, please check to see if they need to be applied to
// cleanup_failed_attach_current_thread as well.
void JavaThread::exit(bool destroy_vm, ExitType exit_type) {
  assert(this == JavaThread::current(),  "thread consistency check");

  if (UseITR) {
    ResourceMark rm; 
    typeArrayOop name = java_lang_Thread::name(threadObj());
    InstructionTraceManager::addIDToThreadName(unique_id(), UNICODE::as_utf8(name->char_at_addr(0), name->length())); 
  }
  
  if (!InitializeJavaLangSystem) return;

  // Compiler threads cannot execute any java code so drop any pending exceptions
  if (this->is_Compiler_thread()) this->clear_pending_exception();

  HandleMark hm(this);
  Handle uncaught_exception(this, this->pending_exception());
  this->clear_pending_exception();
  Handle threadObj(this, this->threadObj());
  assert(threadObj.not_null(), "Java thread object should be created");
  
  // FIXIT: This code should be moved into else part, when reliable 1.2/1.3 check is in place
  {
    EXCEPTION_MARK;
    
    CLEAR_PENDING_EXCEPTION;
  }
  // FIXIT: The is_null check is only so it works better on JDK1.2 VM's. This
  // has to be fixed by a runtime query method
if(!destroy_vm){
    // JSR-166: change call from from ThreadGroup.uncaughtException to
    // java.lang.Thread.dispatchUncaughtException 
    if (uncaught_exception.not_null()) {
      Handle group(this, java_lang_Thread::threadGroup(threadObj()));
Log::log4(NOTAG,Log::M_THREAD|Log::M_EXCEPTION,"uncaught exception INTPTR_FORMAT "" INTPTR_FORMAT "" INTPTR_FORMAT",uncaught_exception(),threadObj(),group());
      { 
        EXCEPTION_MARK;
        // Check if the method Thread.dispatchUncaughtException() exists. If so
        // call it.  Otherwise we have an older library without the JSR-166 changes,
        // so call ThreadGroup.uncaughtException()
        KlassHandle recvrKlass(THREAD, threadObj->klass());
        CallInfo callinfo;
        KlassHandle thread_klass(THREAD, SystemDictionary::thread_klass());
        LinkResolver::resolve_virtual_call(callinfo, threadObj, recvrKlass, thread_klass,
                                           vmSymbolHandles::dispatchUncaughtException_name(),
                                           vmSymbolHandles::throwable_void_signature(),
                                           KlassHandle(), false, false, THREAD);
        CLEAR_PENDING_EXCEPTION;
        methodHandle method = callinfo.selected_method();
        if (method.not_null()) {
          JavaValue result(T_VOID);
          JavaCalls::call_virtual(&result, 
                                  threadObj, thread_klass,
                                  vmSymbolHandles::dispatchUncaughtException_name(), 
                                  vmSymbolHandles::throwable_void_signature(), 
                                  uncaught_exception,
                                  THREAD);
        } else {
          KlassHandle thread_group(THREAD, SystemDictionary::threadGroup_klass());
          JavaValue result(T_VOID);
          JavaCalls::call_virtual(&result, 
                                  group, thread_group,
                                  vmSymbolHandles::uncaughtException_name(), 
                                  vmSymbolHandles::thread_throwable_void_signature(), 
                                  threadObj,           // Arg 1
                                  uncaught_exception,  // Arg 2
                                  THREAD);
        }
        CLEAR_PENDING_EXCEPTION;
      }
    }
 
    // Call Thread.exit(). We try 3 times in case we got another Thread.stop during
    // the execution of the method. If that is not enough, then we don't really care. Thread.stop
    // is deprecated anyhow.
if(!is_hidden_from_external_view()){
int count=3;
      while (java_lang_Thread::threadGroup(threadObj()) != NULL && (count-- > 0)) {
        EXCEPTION_MARK;
        JavaValue result(T_VOID);
        KlassHandle thread_klass(THREAD, SystemDictionary::thread_klass());
        JavaCalls::call_virtual(&result, 
                              threadObj, thread_klass,
                              vmSymbolHandles::exit_method_name(), 
                              vmSymbolHandles::void_method_signature(), 
                              THREAD);  
        CLEAR_PENDING_EXCEPTION;
      }
    }      

    // notify JVMTI
    if (JvmtiExport::should_post_thread_life()) {
      JvmtiExport::post_thread_end(this);
    }

    // no more external suspends are allowed at this point,
    // but there may be an existing pending suspend.
    set_terminated(_thread_exiting);
    ThreadService::current_thread_exiting(this);
    // We have notified the agents that we are exiting, before we go on,
    // we must check for a pending jvmdi suspend request and honor it
    // in order to not surprise the thread that made the suspend request.
    while( please_self_suspend() & (jvmti_suspend|intrp_suspend)) 
      self_suspend();
  } else {
    // before_exit() has already posted JVMTI THREAD_END events
  }
  
  // Delete the proxy jni env before the join so threads who create and join 
  // in a loop don't cause a backlog of not-yet-deleted jni envs
  if (_proxy_jni_environment != 0) {
#ifdef AZ_PROXIED
    proxy_destroyJNIEnv(_proxy_jni_environment);
#else
    Unimplemented();
#endif // AZ_PROXIED
  }
  
  // Notify waiters on thread object. This has to be done after exit() is called
  // on the thread (if the thread is the last thread in a daemon ThreadGroup the
  // group should have the destroyed bit set before waiters are notified).
  ensure_join(this);
  assert(!this->has_pending_exception(), "ensure_join should have cleared");

  // 6282335 JNI DetachCurrentThread spec states that all Java monitors
  // held by this thread must be released.  A detach operation must only
  // get here if there are no Java frames on the stack.  Therefore, any
  // owned monitors at this point MUST be JNI-acquired monitors which are
  // pre-inflated and in the monitor cache.
  //
  // ensure_join() ignores IllegalThreadStateExceptions, and so does this.
  if (exit_type == jni_detach && JNIDetachReleasesMonitors) {
    assert(!this->has_last_Java_frame(), "detaching with Java frames?");
    ObjectSynchronizer::release_monitors_owned_by_thread(this);
    assert(!this->has_pending_exception(), "release_monitors should have cleared");
  }

  // These things needs to be done while we are still a Java Thread. Make sure that thread
  // is in a consistent state, in case GC happens
  assert(_privileged_stack_top == NULL, "must be NULL when we get here");

  if (active_handles() != NULL) {
    JNIHandleBlock* block = active_handles();
    set_active_handles(NULL);
    JNIHandleBlock::release_block(block);
  }

  if (free_handle_block() != NULL) {
    JNIHandleBlock* block = free_handle_block();
    set_free_handle_block(NULL);
    JNIHandleBlock::release_block(block);
  }

  tlab().make_parsable(true);  // retire TLAB

  // Remove from list of active threads list, and notify VM thread if we are the last non-daemon thread 
  Threads::remove(this);  

  // Any safepoint from here on may induce corruption in pauseless GC.
}

void JavaThread::cleanup_failed_attach_current_thread() {
  if (active_handles() != NULL) {
    JNIHandleBlock* block = active_handles();
    set_active_handles(NULL);
    JNIHandleBlock::release_block(block);
  }
  
  if (free_handle_block() != NULL) {
    JNIHandleBlock* block = free_handle_block();
    set_free_handle_block(NULL);
    JNIHandleBlock::release_block(block);
  }
  
  tlab().make_parsable(true);  // retire TLAB, if any
  
  Threads::remove(this);
  delete this;
}


JavaThread* JavaThread::active() {
Thread*thread=Thread::current();
  assert(thread != NULL, "just checking");
  if (thread->is_Java_thread()) {
    return (JavaThread*) thread;
  } else {
    assert(thread->is_VM_thread(), "this must be a vm thread");
    VM_Operation* op = ((VMThread*) thread)->vm_operation();
    JavaThread *ret=op == NULL ? NULL : (JavaThread *)op->calling_thread();
    assert(ret->is_Java_thread(), "must be a Java thread");
    return ret;
  }
}


// JVM support.

// Somebody is throwing an asynchronous error at us, i.e., the "this" pointer
// is likely not JavaThread::current().  Capture the exception into
// _pending_async_exception and set a polling bit.  We'll inspect it the next
// time we poll.  WARNING: The caller owns the Threads_lock (to prevent the
// "this" pointer from disappearing out from under us), so we better not
// block!
void JavaThread::install_async_exception(objectRef java_throwable, bool overwrite_prior, bool remote)  {
assert(JavaThread::current()->jvm_locked_by_self(),
"The JavaThread doing the async exception install is handling naked oops");
  assert( !java_throwable.is_stack(), "cannot throw a stack allocated exception to another thread");

  // Do not throw asynchronous exceptions against the compiler thread
  // (the compiler thread should not be a Java thread -- fix in 1.4.2)
assert(!is_Compiler_thread(),"throwing exceptions against a compiler thread?");

  // This is a change from JDK 1.1, but JDK 1.2 will also do it:
if(java_throwable.as_oop()->is_a(SystemDictionary::threaddeath_klass())){
    java_lang_Thread::set_stillborn(threadObj());
  }

  // Install the error as long as we do not override an existing ThreadDeath
  // exception.  Must CAS it in since many threads can be installing at once.
  objectRef x = _pending_async_exception; // Read only once
  while(((x.is_null()) || (overwrite_prior && !x.as_oop()->is_a(SystemDictionary::threaddeath_klass()))) &&
        (intptr_t)x.raw_value() != Atomic::cmpxchg_ptr((intptr_t)java_throwable.raw_value(), (intptr_t*)&_pending_async_exception, (intptr_t)x.raw_value())) {
    x = _pending_async_exception; // Read only once
  }

  // Now ask the 'this' thread to inspect the freshly installed asynchronous
  // exception.  Due to race conditions, it might have already seen the
  // exception (immediately after a successful CAS above) and cleared the
  // _pending_async_exception field, so we cannot assert much here.
  set_suspend_request(async_suspend);

  // If a remote thread is throwing the async to us, interrupt thread so it
  // will wake up from a potential wait()
  if( remote ) Thread::interrupt(this);  
}

#ifdef ASSERT
// verify the JavaThread has not yet been published in the Threads::list, and
// hence doesn't need protection from concurrent access at this stage
void JavaThread::verify_not_published() {
if(!Threads_lock.owned_by_self()){
MutexLockerAllowGC ml(Threads_lock,this);
   assert( !Threads::includes(this), 
	   "java thread shouldn't have been published yet!");
  }
  else {
   assert( !Threads::includes(this), 
	   "java thread shouldn't have been published yet!");
  }
}
#endif


// Move a pending async exception (at a polling point) into the pending
// exception field.  May require a deoptimization, which can GC.
void JavaThread::move_pending_async_to_pending(){
assert(jvm_locked_by_self(),"handling raw oops");

  // Now install any pending async exception into pending exception
  objectRef x = _pending_async_exception;	// Read only once
  while( (intptr_t)x.raw_value() != Atomic::cmpxchg_ptr((intptr_t)NULL,(intptr_t*)&_pending_async_exception,(intptr_t)x.raw_value()) ) 
    x = _pending_async_exception; // Read only once

  set_pending_exception(x.as_oop(),__FILE__,__LINE__); // no useful file/line info

  // If the topmost frame is a runtime stub, then we are calling into C2
  // OptoRuntime from compiled code.  Some runtime stubs (new, monitor_exit)
  // must deoptimize the caller before continuing, as the compiled exception
  // handler table may not be valid.
  //
  // Async exception installed before calling any Java code
  if( root_Java_frame() ) return;

  frame f = last_frame();
if(f.is_runtime_frame())
    f = f.sender();
  if (f.is_compiled_frame()) {
CodeBlob*codeBlob=CodeCache::find_blob(f.pc());
assert(codeBlob!=NULL,"CodeBlob expected.");
    methodCodeOop mcOop = codeBlob->owner().as_methodCodeOop();

    if( mcOop ) {
      mcOop->deoptimize_now(Deoptimization::Reason_install_async); // can GC
    }
  }
}

// Unsafe code took a fault.  Install an async error at the next safepoint.
void JavaThread::set_pending_unsafe_access_error(){
const char*msg="a fault occurred in a recent unsafe memory access operation";
  
  // Allocate the exception object
  symbolHandle h_name(this, vmSymbols::java_lang_InternalError());
  Handle       h_cause(this, NULL);
  Handle       h_loader(this, NULL);
  Handle       h_protection_domain(this, NULL);
  Handle h_exception = Exceptions::new_exception(this, h_name, msg, h_cause, h_loader, h_protection_domain);

  // Set the async exception in the thread
  // async exception must be heap allocated
  StackBasedAllocation::ensure_in_heap(this,h_exception,"exception with unsafe access");
  install_async_exception(h_exception.as_ref(),true/*overwrite*/, false/*self thread*/);
}

// We hit a stack overflow. Install an async stack overflow exception at the next safepoint.
void JavaThread::set_pending_stack_overflow_error(){
  // Cloned from SharedRuntime::build_StackOverflowError()
  // Special handling for stack overflow: since we don't have any (java) stack
  // space left we use the pre-allocated & pre-initialized StackOverflowError
  // klass to create an stack overflow error instance.  We do not call its
  // constructor for the same reason (it is empty, anyway).
  //
  // SBA may eventually need a very similar function, build_FrameIdOverflowError().
  //
  // get klass
instanceKlass*soe_klass=instanceKlass::cast(SystemDictionary::StackOverflowError_klass());
  assert(soe_klass->is_initialized(), "VM initialization should set up StackOverflowError klass.");
  // alloc an instance - avoid constructor since execution stack is exhausted
  PRESERVE_EXCEPTION_MARK;
  Handle ex = Handle(this, soe_klass->allocate_instance(true/*SBA*/, this));
  // if no other exception is pending, fill in trace for stack overflow...
  if (!has_pending_exception()) {
java_lang_Throwable::fill_in_stack_trace(ex);
  }
  // async exception must be heap allocated
  StackBasedAllocation::ensure_in_heap(this,ex, "exception with stack overflow");
  install_async_exception(ex.as_ref(),true/*overwrite*/, false/*self thread*/);
}

void JavaThread::frames_do(void f(JavaThread *thsi, frame*)) {
  // ignore if there is no stack
  if (root_Java_frame()) return;
  SetSBAThreadMark sba_mark(this);
  // traverse the stack frames. Starts from top frame.  
for(StackFrameStream fst(this);!fst.is_done();fst.next())
f(this,fst.current());
}


// GC support
static void frame_gc_epilogue(JavaThread *thread, frame* f) { f->gc_epilogue(thread); }

void JavaThread::gc_epilogue() {
  frames_do(frame_gc_epilogue);
  if( UseSBA && sba_area() ) sba_area()->gc_moved_klasses_and_moops();
}


static void frame_gc_prologue(JavaThread *thread, frame* f) { f->gc_prologue(thread); }

void JavaThread::gc_prologue() {
  frames_do(frame_gc_prologue);
}

void JavaThread::GPGC_gc_prologue(OopClosure*f){
  // We can't use frames_do(), because we're passing an extra argument.  So below
  // is a modified copy of JavaThread::frames_do().
  // ----------------------------------------------------------------------------

  // ignore if there is no stack
  if (root_Java_frame()) return;
  SetSBAThreadMark sba_mark(this);
  // traverse the stack frames. Starts from top frame.  
  for( StackFrameStream fst(this); !fst.is_done(); fst.next() ) 
    fst.current()->GPGC_gc_prologue(this, f);
}

void JavaThread::GPGC_mutator_gc_prologue(OopClosure*f){
  // We can't use frames_do(), because we're passing an extra argument.  So below
  // is a modified copy of JavaThread::frames_do().
  // ----------------------------------------------------------------------------

  // ignore if there is no stack
  if (root_Java_frame()) return;
  SetSBAThreadMark sba_mark(this);
  // traverse the stack frames. Starts from top frame.  
  for( StackFrameStream fst(this); !fst.is_done(); fst.next() ) 
    fst.current()->GPGC_mutator_gc_prologue(this, f);
}

// Used by JavaThread::oops_do
class SBAIgnoreStackOopsWrapperClosure;
class SBAIgnoreHeader:public OopClosure{
public:
  SBAIgnoreStackOopsWrapperClosure *const _ff;
  SBAIgnoreHeader(SBAIgnoreStackOopsWrapperClosure *ff) : _ff(ff) {}
  virtual void do_oop(objectRef* p);
};

class SBAIgnoreStackOopsWrapperClosure: public SbaClosure {
public:
  OopClosure *const _f;
  SBAIgnoreHeader _ih;
  SBAIgnoreStackOopsWrapperClosure(OopClosure *f, JavaThread *jt) : SbaClosure(jt), _f(f), _ih(this) { }

  virtual void do_oop(objectRef* p) {
    if( p->is_null() ) return;
    objectRef r = UNPOISON_OBJECTREF(*p,p);
    if( !r.is_stack() ) { // Filter out stack oops
_f->do_oop(p);
      return;
    }
    oop tmp = r.as_oop();
    if( test_set(tmp) ) return; // Already been there, done that
    // Recursively walk stackoops
    SBAPreHeader *pre = stackRef::preheader(tmp);
pre->oops_do(_f);
    //if(_f->do_header())         // Must walk the non-stack klass object
    //  tmp->oop_iterate_header(_f);
    int kid = tmp->mark()->kid();
    assert0( KlassTable::is_valid_klassId(kid) );
    klassRef k = KlassTable::getKlassByKlassId(kid);
    assert0( k.as_oop()->is_oop() );
    tmp->oop_iterate(&_ih);
  }
  
  virtual void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
    if( !base_ptr->is_stack() ) {
      _f->do_derived_oop(base_ptr,derived_ptr);
      return;
    }
  }
};
void SBAIgnoreHeader::do_oop(objectRef*p){
  if( KlassTable::contains(p) ) return;
_ff->do_oop(p);
}


void JavaThread::oops_do_impl(OopClosure*f){
  // If we have deferred set_locals there might be oops waiting to be
  // written
  GrowableArray<jvmtiDeferredLocalVariableSet*>* list = deferred_locals();
  if (list != NULL) {
    for (int i = 0; i < list->length(); i++) {
      list->at(i)->oops_do(f);
    }
  }

  // Traverse instance variables at the end since the GC may be moving things
  // around using this function
  f->do_oop(&_threadObj);
f->do_oop(&_pending_async_exception);
f->do_oop(&_hint_block_concurrency_msg);
f->do_oop(&_hint_block_concurrency_lock);
f->do_oop(&_hint_block_concurrency_sync);
  // All the locks!  Mostly this is a really small amount.
  for( objectRef *optr = _lckstk_base; optr < _lckstk_top; optr++ )
f->do_oop(optr);

  // Interpreter tagged stack scan.  This is a bulk scan across all frames
  for( intptr_t *optr = jexstk_base_for_sp((intptr_t*)this); optr < _jexstk_top; optr++ ) {
    switch (frame::tag_at_address(optr)) {
    case frame::single_slot_primitive_type:                               break;
    case frame::double_slot_primitive_type:  optr++;                      break;
    case frame::single_slot_ref_type:        f->do_oop((objectRef*)optr); break;
    default:  ShouldNotReachHere();
    }
  }
  

  if (jvmti_thread_state() != NULL) {
    jvmti_thread_state()->oops_do(f);
  }

  // Traverse the GCHandles
  Thread::oops_do(f);

  if (root_Java_frame()) return;

  // Traverse the privileged stack
if(_privileged_stack_top)_privileged_stack_top->oops_do(f);

  // Traverse the execution stack
  bool do_arg_oops = true;
  for( StackFrameStream fst(this); !fst.is_done(); fst.next() ) {
    if( do_arg_oops )           // Handle youngest caller args?
      fst.current()->oops_arguments_do(f);
    fst.current()->oops_do(this,f);
    // Redo argument oops for each recursive call into the VM
    do_arg_oops = fst.current()->is_entry_frame();
  }

  // Reset register-window-fill engine to pick up good data.
  modified_rstack();
}


// This is the standard oops_do for most of the VM.  It completely hides stack
// oops and exposes heap pointers that are inside of stack objects as "just
// another root", like a Java local or a compiled Java temp.
void JavaThread::oops_do(OopClosure* f) {  
  assert( ( root_Java_frame() && java_call_counter() == 0) ||
          (!root_Java_frame() && java_call_counter()  > 0), "wrong java_sp info!");

  if( UseSBA && sba_area() ) {
    SetSBAThreadMark sba_mark(this);
    SBAIgnoreStackOopsWrapperClosure wrapper(f, this);
    oops_do_impl(&wrapper);
  } else {
oops_do_impl(f);
  }
}


// Oops_do over all the basic JavaThread roots including SBA objects.
// Stop after frame 'fid', as no more forwarding pointers exist.
void JavaThread::oops_do_stackok(SbaClosure* f, int limit_fid) {
  SetSBAThreadMark sba_mark(this);

  // Traverse instance variables at the end since the GC may be moving things
  // around using this function
f->do_oop(&_threadObj);
f->do_oop(&_pending_async_exception);
f->do_oop(&_hint_block_concurrency_msg);
f->do_oop(&_hint_block_concurrency_lock);
f->do_oop(&_hint_block_concurrency_sync);
//All the locks!  Mostly this is a really small amount.
  for( objectRef *optr = _lckstk_base; optr < _lckstk_top; optr++ )
f->do_oop(optr);

  // Interpreter tagged stack scan.  This is a bulk scan across all frames
  for( intptr_t *optr = jexstk_base_for_sp((intptr_t*)this); optr < _jexstk_top; optr++ ) {
    switch (frame::tag_at_address(optr)) {
    case frame::single_slot_primitive_type:                               break;
    case frame::double_slot_primitive_type:  optr++;                      break;
    case frame::single_slot_ref_type:        f->do_oop((objectRef*)optr); break;
    default:  ShouldNotReachHere();
    }
  }
  

  if (jvmti_thread_state() != NULL) {
    jvmti_thread_state()->oops_do(f);
  }

  // Traverse the GCHandles
  Thread::oops_do(f);

  if (root_Java_frame()) return;

  // Traverse the privileged stack
  if( _privileged_stack_top ) _privileged_stack_top->oops_do(f);
  
  // Traverse the execution stack, up to the given frame
  bool do_arg_oops = true;
frame fr=last_frame();
  int fid = curr_sbafid();      // Initial FID
  do {
    if( do_arg_oops )           // Handle youngest caller args?
      fr.oops_arguments_do(f);
    fr.oops_do(this,f);

    if( fr.is_first_frame() ) break;
    // Redo argument oops for each recursive call into the VM
    do_arg_oops = fr.is_entry_frame();
if(fr.is_entry_frame()){
      fid--;                    // Each entry frame drops 1 fid
    }
    fr = fr.sender();           // Next frame up
    // Some frames are pushed without bumping MSP - native frames, and all
    // runtime frames for example.
  } while( fid >= limit_fid );  // Found limit frame?

  // Reset register-window-fill engine to pick up good data.
  modified_rstack();
}

void JavaThread::methodCodes_do(){
  // Traverse the GCHandles
Thread::methodCodes_do();

  assert( (root_Java_frame() && java_call_counter() == 0) ||
          (!root_Java_frame() && java_call_counter() > 0), "wrong java_sp info!");

  if (!root_Java_frame()) {
    // Traverse the execution stack    
    for(StackFrameStream fst(this); !fst.is_done(); fst.next()) {
fst.current()->methodCodes_do();
    }
  } 
}

// --- count_locks -----------------------------------------------------------
// Count times this oop is locked on the stack.  Generally used to inflate a
// biased-lock into a real monitor.  Sometimes called by remote threads.
int JavaThread::count_locks( oop o ) {
  assert0( this == Thread::current() || // Either this-thread is crawling self stack OR...
           this->jvm_locked_by_VM() );  // this-thread has it's stack locked.
  int nested_locks = 0;
  if( !root_Java_frame() )
    for( StackFrameStream fst(this); !fst.is_done(); fst.next() )
      nested_locks += fst.current()->locked(o);
  // Also count VM held locks
  for( const ObjectLocker *ol = _objectLockerList; ol; ol=ol->_ol )
    if( ol->_h() == o )
      nested_locks++;
  // Also count interpreter-locks
  for( objectRef *op = _lckstk_base; op < _lckstk_top; op++ )
    if( lvb_ref(op).as_oop() == o )
      nested_locks++;

  return nested_locks;
}
  

// Printing
void JavaThread::print_thread_state_on(outputStream *st) const {
  intptr_t x = _jvm_lock;	// Read only once
  const char *s = (x == 0) ? "ed by self" : 
    (((x&1)==1) ? "ed by VM" : " is free");
  const char *t = (x&2) ? " (w/poll advisory bit)" : "";

  jint y = _please_self_suspend; // Read only once
  char buf[32*6+1+256];
  buf[0] = '\0';
  if( y & prmpt_suspend ) strcat(buf,"prmpt ");
  if( y & safep_suspend ) strcat(buf,"safep ");
  if( y & jvmti_suspend ) strcat(buf,"jvmti ");
  if( y & jlang_suspend ) strcat(buf,"jlang ");
  if( y & async_suspend ) strcat(buf,"async ");
  if( y & intrp_suspend ) strcat(buf,"intrp ");
  if( y & unsaf_suspend ) strcat(buf,"unsaf ");
  if( y & print_suspend ) strcat(buf,"print ");
  if( y & stack_suspend ) strcat(buf,"stkov ");
  if( y & rstak_suspend ) strcat(buf,"rstak ");
  if( y & checkp_suspend) strcat(buf,"checkp ");
  if( y & stack_is_clean) strcat(buf,"stack_is_clean ");
  if( y & gpgc_clean_new) strcat(buf,"gpgc_clean_new ");
  if( y & gpgc_clean_old) strcat(buf,"gpgc_clean_old ");
  if( y & unbias_suspend) strcat(buf,"unbias ");
  if( !y ) strcat(buf,"NONE ");

  const char *a = has_pending_exception() ? ", has pending exception" : "";
  const char *b = _pending_async_exception.not_null()     ? ", has pending async exception" : "";

  // Use a single call to print_cr, so we get no interleaving with other threads
  st->print("JVM lock%s%s %s, polling bits: %s%s%s", s, t, _hint_blocked, buf, a, b);
};
void JavaThread::print_thread_state() const {
  print_thread_state_on(tty);
};

#ifndef PRODUCT
void JavaThread::ps(int depth) {
  int save = VerifyJVMLockAtLVB;
  VerifyJVMLockAtLVB = 0;
  _debug_level++;
  // This may prevent VM continuation, but it allows heap parsing!
  tlab().make_parsable(false);  // retire TLAB, if any
  if( root_Java_frame() )
tty->print_cr("At thread root, no Java frames");
  else
    last_frame().ps(this,depth); // Print thread stack with intelligence
  tlab().zero_filler();          // recover tlab after printing
  _debug_level--;
  VerifyJVMLockAtLVB = save;
}
#endif // PRODUCT

// Called by Threads::print() for VM_PrintThreads operation
void JavaThread::print_on(outputStream*st){
  ResourceMark rm;
  const char* name = get_thread_name();

  st->print("\"%s\" ", (name != NULL) ? name : "(NULL)");
  oop thread_oop = threadObj();
  if (thread_oop != NULL && java_lang_Thread::is_daemon(thread_oop))  st->print("daemon ");
  Thread::print_on(st);
  // print guess for valid stack memory region (assume 4K pages); helps lock debugging
  if ( this==Thread::current() && _jvm_lock==0 ) {
    if( last_Java_pc() != 0 )	// guess that last_Java_sp is even available
      st->print_cr("[" INTPTR_FORMAT ".." PTR_FORMAT "]", (intptr_t)last_Java_sp() & ~right_n_bits(12), this);
  }
  if (thread_oop != NULL) {
    st->print_cr("   java.lang.Thread.State: %s", java_lang_Thread::thread_status_name(thread_oop));
  }
  print_thread_state_on(st);
}

// Called by fatal error handler. The difference between this and
// JavaThread::print() is that we can't grab lock or allocate memory.
void JavaThread::print_on_error(outputStream* st, char *buf, int buflen) const {
  ThreadInVMfromError tivfe; 
  oop thread_obj = tivfe.safe_to_print() ? threadObj() : NULL;
  if (thread_obj != NULL) {
    st->print("JavaThread \"%s\"",  get_thread_name_string(buf, buflen));
    if (java_lang_Thread::is_daemon(thread_obj)) st->print(" daemon");
  } else {
st->print("JavaThread \"UNKNOWN\"");
  }
  st->print(" [");
  print_thread_state_on(st);
  if (osthread()) {
    st->print(", id=%d", osthread()->thread_id());
  }
  st->print(", stack(" PTR_FORMAT "," PTR_FORMAT ")",
            _stack_base - _stack_size, _stack_base);
  st->print("]");
  return;
}

static void print_xml_line_per_method(JavaThread *thread, vframe vf, xmlBuffer *xb, bool youngest) {
  ResourceMark rm;
  xmlElement xf(xb, "method_info");
  // Cut-n-paste from java_lang_Throwable::print_stack_element
  vf.method()->print_xml_on(xb, true);
  if (vf.method()->is_native()) {
    xb->name_value_item("type", "native method");
  } else {
    if (vf.get_frame().is_interpreted_frame()) {
      xb->name_value_item("type", "interpreter");
    } else if (vf.get_frame().is_compiled_frame()) {
      xb->name_value_item("type", "compiled");
    } else {
	// TODO: show if it's C1 or C2, as in the old commented out code below.
      xb->name_value_item("type", "unknown");
//    } else if (vf.nm()->is_compiled_by_c1()) {
//      xb->name_value_item("type", "client compiler");
//    } else {
//      assert0(vf.nm()->is_compiled_by_c2());
//      xb->name_value_item("type", "server compiler");
    }
  }
if(!vf.method()->is_native()){
    xmlElement xe(xb, "line_info", xmlElement::delayed_LF);
instanceKlass*klass=instanceKlass::cast(vf.method()->method_holder());
    xb->print(klass->source_file_name() ? klass->source_file_name()->as_C_string() : "unknown source");
    int line_number = vf.method()->line_number_from_bci(vf.bci());
    if (line_number != -1) xb->print(":%d",line_number);
  }
  xb->name_value_item("bci",vf.bci());
  xb->name_value_item("invocation_counter",vf.method()->invocation_count());
  xb->name_value_item("backedge_counter",vf.method()->backedge_count());
  if( vf.method()->has_compiled_code() )
    xb->name_value_item("has_code","has code");
  if( vf.sbafid() != -1 ) xb->name_value_item("sbafid",vf.sbafid());
  vf.print_lock_info(thread,youngest, xb);
}

static void print_xml_stack_line_per_method( JavaThread *thread, xmlBuffer *xb, int depth ) {
  bool youngest = true;
  for (vframe vf(thread); !vf.done(); vf.next()) {
    xmlElement sf(xb, "stack_frame");
    print_xml_line_per_method(thread, vf, xb, youngest);
    youngest = false;
  }
}

static void print_xml_stack_line_per_java_element( JavaThread *thread, xmlBuffer *xb ) {
  bool youngest = true;
  for( vframe vf(thread); !vf.done(); vf.next() ) {
    ResourceMark rm;
    xmlElement xf(xb, "stack_frame");
    print_xml_line_per_method(thread, vf, xb, youngest);
    vf.print_xml_on( thread, xb );
    youngest = false;
  }
}

void JavaThread::print_xml_on(xmlBuffer *xb, int detail, bool dostate) {
  bool ref = (detail == 0);
  xmlElement te(xb, ref ? "thread_ref" : "thread");
  const char* name = get_thread_name();
  state_print_xml_on(xb, name ? name : "(NULL)", dostate);

  if (!ref) {
    ArtaObjects::oop_print_xml_on(threadObj(), xb, true);
    
    // Now the stack dump, but must have cooperation from the thread
    // Here the ARTA thread is setting a request on a remote thread
    if (Thread::current() != this) {
      set_suspend_request(print_suspend);
      // Now spin until the remote thread honors the request and suspends
      bool was_VM_locked = jvm_locked_by_VM();
      if( !was_VM_locked ) {
        while (!jvm_lock_VM_attempt()) {
          os::yield_all(1);
        }
      }
      // He's nailed.  Print his stack.
      if (!root_Java_frame()) {
        switch (detail) {
case 1:{
            xmlElement xe(xb,"stack_trace");
            print_xml_stack_line_per_method( this, xb, 0 );
            break;
          }
          case 2: {
            xmlElement xe(xb,"stack_trace");
            print_xml_stack_line_per_java_element(this, xb);
            break;
          }
          default:
            ShouldNotReachHere();
        }
      }
      
      // Resume the sleeping Java thread
      if (!was_VM_locked)
        jvm_unlock_VM();
      java_resume(print_suspend);
    }
  }
}


// Verification

static void frame_verify(JavaThread *thread, frame* f) { f->verify(thread); }

void JavaThread::verify() {
  // Verify oops in the thread.
  oops_do(&VerifyOopClosure::verify_oop);

  // Verify the stack frames.
  frames_do(frame_verify);
}

// CR 6300358 (sub-CR 2137150)
// Most callers of this method assume that it can't return NULL but a
// thread may not have a name whilst it is in the process of attaching to
// the VM - see CR 6412693, and there are places where a JavaThread can be
// seen prior to having it's threadObj set (eg JNI attaching threads and
// if vm exit occurs during initialization). These cases can all be accounted
// for such that this method never returns NULL.
const char* JavaThread::get_thread_name() const {
#ifdef ASSERT
  // early safepoints can hit while current thread does not yet have TLS
  if (!SafepointSynchronize::is_at_safepoint()) {
    Thread *cur = Thread::current();
    if (!(cur->is_Java_thread() && cur == this)) {
      // Current JavaThreads are allowed to get their own name without
      // the Threads_lock.
      assert_locked_or_safepoint(Threads_lock);
    }
  }
#endif // ASSERT
    return get_thread_name_string();
}

// Returns a non-NULL representation of this thread's name, or a suitable
// descriptive string if there is no set name
const char* JavaThread::get_thread_name_string(char* buf, int buflen) const {
  const char* name_str;
  oop thread_obj = threadObj();
  if (thread_obj != NULL) {
    typeArrayOop name = java_lang_Thread::name(thread_obj);
    if (name != NULL) {
      if (buf == NULL) {
	name_str = UNICODE::as_utf8((jchar*) name->base(T_CHAR), name->length());
      }
      else {
	name_str = UNICODE::as_utf8((jchar*) name->base(T_CHAR), name->length(), buf, buflen);
      }
    }
    else if (is_attaching()) { // workaround for 6412693 - see 6404306
      name_str = "<no-name - thread is attaching>";
    }
    else {
      name_str = Thread::name();
    }
  }
  else {
    name_str = Thread::name();
  }
  assert(name_str != NULL, "unexpected NULL thread name");
  return name_str;
}


const char* JavaThread::get_threadgroup_name() const {
  debug_only(if (JavaThread::current() != this) assert_locked_or_safepoint(Threads_lock);)
  oop thread_obj = threadObj();
  if (thread_obj != NULL) {
    oop thread_group = java_lang_Thread::threadGroup(thread_obj);
    if (thread_group != NULL) {
      typeArrayOop name = java_lang_ThreadGroup::name(thread_group);
      // ThreadGroup.name can be null
      if (name != NULL) {
        const char* str = UNICODE::as_utf8((jchar*) name->base(T_CHAR), name->length());
        return str;
      }
    }
  }
  return NULL;
}

const char* JavaThread::get_parent_name() const {
  debug_only(if (JavaThread::current() != this) assert_locked_or_safepoint(Threads_lock);)
  oop thread_obj = threadObj();
  if (thread_obj != NULL) {
    oop thread_group = java_lang_Thread::threadGroup(thread_obj);
    if (thread_group != NULL) {
      oop parent = java_lang_ThreadGroup::parent(thread_group);
      if (parent != NULL) {
        typeArrayOop name = java_lang_ThreadGroup::name(parent);
	// ThreadGroup.name can be null
	if (name != NULL) {
          const char* str = UNICODE::as_utf8((jchar*) name->base(T_CHAR), name->length());
          return str;
	}
      }
    }
  }
  return NULL;
}


void JavaThread::set_priority(JavaThreadPriority java_priority) {
  trace("set priority", this);
assert_lock_strong(Threads_lock);
  debug_only(check_for_dangling_thread_pointer(this);)

  OSThreadPriority os_priority = os::os_priority_from_java_priority(java_priority);

  oop thr_oop = this->threadObj();
  java_lang_Thread::set_priority(thr_oop, java_priority);
  this->_current_priority = os_priority;

  // Can return an error!
  (void)os::set_native_priority(this, os_priority);
}


void JavaThread::set_os_priority(OSThreadPriority os_priority) {
  trace("set priority", this);
assert_lock_strong(Threads_lock);
  debug_only(check_for_dangling_thread_pointer(this);)
  assert(os_priority > JavaThreadMaxPriority, "Only for setting high priorities on JavaThreads");

  oop thr_oop = this->threadObj();
  java_lang_Thread::set_priority(thr_oop, MaxPriority);
  this->_current_priority = os_priority;

  // Can return an error!
  (void)os::set_native_priority(this, os_priority);
}


JavaThreadPriority JavaThread::java_priority()const{
  oop thr_oop = threadObj();
  if (thr_oop == NULL) return NormPriority; // Bootstrapping
JavaThreadPriority priority=java_lang_Thread::priority(thr_oop);
  assert(MinPriority <= priority && priority <= MaxPriority, "sanity check");
  return priority;
}

void JavaThread::prepare(Handle thread_oop) {

assert_lock_strong(Threads_lock);
  // Link Java Thread object <-> C++ Thread

  // Get the C++ thread object (an oop) from the JNI handle (a jthread)
  // and put it into a new Handle.  The Handle "thread_oop" can then
  // be used to pass the C++ thread object to other methods.

  // Set the Java level thread object (jthread) field of the
  // new thread (a JavaThread *) to C++ thread object using the
  // "thread_oop" handle.
  
  // Set the thread field (a JavaThread *) of the
  // oop representing the java_lang_Thread to the new thread (a JavaThread *).

  assert(instanceKlass::cast(thread_oop->klass())->is_linked(), 
    "must be initialized");
  set_threadObj(thread_oop());
  java_lang_Thread::set_thread(thread_oop(), this);

  JavaThreadPriority prio = java_lang_Thread::priority(thread_oop());
  assert(prio != NoPriority, "A valid priority should be present");

  // Push the Java priority down to the native thread; needs Threads_lock
this->set_priority(prio);

  // The java thread should be named by now (at least with its initial name)
  if (UseITR) {
    InstructionTraceThreads::noticeThread(this);
  }

  // Add the new thread to the Threads list and set it in motion. 
  // We must have threads lock in order to call Threads::add. 
  // It is crucial that we do not block before the thread is 
  // added to the Threads list for if a GC happens, then the java_thread oop
  // will not be visited by GC.
Threads::add(this,false);
}

oop JavaThread::current_park_blocker() {
  // Support for JSR-166 locks
  oop thread_oop = threadObj();
  if (thread_oop != NULL && JDK_Version::supports_thread_park_blocker()) {
    return java_lang_Thread::park_blocker(thread_oop);
  }
  return NULL;
}


void JavaThread::print_stack_on(outputStream* st) {  
  if (root_Java_frame()) return;
  ResourceMark rm;
  HandleMark   hm;
  int depth = 0;
  for( vframe vf(this); !vf.done(); vf.next() ) {
    if (depth >= MaxJavaStackTraceDepth) return; // stack too deep
    java_lang_Throwable::print_stack_element(Handle(), vf.method(), vf.bci());
    vf.print_lock_info(this, depth == 0, NULL);
    depth++;
  }
}


// JVMTI PopFrame support
void JavaThread::popframe_preserve_args(ByteSize size_in_bytes, void* start) {
  assert(_popframe_preserved_args == NULL, "should not wipe out old PopFrame preserved arguments");
  if (in_bytes(size_in_bytes) != 0) {
    _popframe_preserved_args = NEW_C_HEAP_ARRAY(char, in_bytes(size_in_bytes));
    _popframe_preserved_args_size = in_bytes(size_in_bytes);
    Copy::conjoint_bytes(start, _popframe_preserved_args, _popframe_preserved_args_size);
  }
}

void* JavaThread::popframe_preserved_args() {
  return _popframe_preserved_args;
}

ByteSize JavaThread::popframe_preserved_args_size() {
  return in_ByteSize(_popframe_preserved_args_size);
}

WordSize JavaThread::popframe_preserved_args_size_in_words() {
  int sz = in_bytes(popframe_preserved_args_size());
  assert(sz % wordSize == 0, "argument size must be multiple of wordSize");
  return in_WordSize(sz / wordSize);
}

void JavaThread::popframe_free_preserved_args() {
  assert(_popframe_preserved_args != NULL, "should not free PopFrame preserved arguments twice");
  FREE_C_HEAP_ARRAY(char, (char*) _popframe_preserved_args);
  _popframe_preserved_args = NULL;
  _popframe_preserved_args_size = 0;
}


#ifndef PRODUCT

void JavaThread::trace_frames() {
  tty->print_cr("[Describe stack]");
  int frame_no = 1;
  for(StackFrameStream fst(this); !fst.is_done(); fst.next()) {
    tty->print("  %d. ", frame_no++);
    fst.current()->print_value_on(tty,this);
    tty->cr();
  }
}


void JavaThread::trace_stack_from( frame fr ) {
  ResourceMark rm;
  HandleMark   hm;
int vframe_no=0;
  for( vframe vf(fr); !vf.done(); vf.next() ) {
    if( ++vframe_no < StackPrintLimit ) 
      vf.print_xml_on(this,NULL);
  }
  if( vframe_no > StackPrintLimit ) 
    tty->print_cr("...< %ld more frames>...", vframe_no - StackPrintLimit);
}


void JavaThread::trace_stack() {
  if (root_Java_frame()) return;
  trace_stack_from(last_frame());
}


#endif // PRODUCT


// Make sure that a thread's stack is coherent, so that it can be walked.  This
// method does not clean a dirty stack.  If pauseless GC is used, the stack cannot
// be safely walked until it is clean.  last_Java_sp() has code to check and clean
// the stack.
//
// This is somewhat tricky: it is ok for the self-thread to do it.  It is also
// OK for a VM thread/GC thread to do it while the VM holds the jvm lock (but
// may require a remote window flush if the Java thread is free-running in
// native code).  Annoyingly, JVMDI/PI can do it as well, but they have to rely
// on the poll-advisory bit being set before the thread escapes from native code
// and reclaims his lock.
intptr_t* JavaThread::make_stack_coherent() {
  intptr_t pd_sp = _jvm_lock;	// Get current JVM lock state once
  if( pd_sp == 0 ) {	// Self-locked, SP in anchor
assert(Thread::current()==this,"Only self Thread can make his own stack coherent while holding the jvm_lock");
    pd_sp = _anchor.last_Java_sp_raw();
    if( !_anchor.is_walkable( pd_sp ) ) {
      os::make_self_walkable(); // just flush windows
      _anchor.record_walkable(); // record walkable in _anchor's last_java_sp
      AuditTrail::log_time(this, AuditTrail::MAKE_SELF_WALKABLE, 1);
    }
  } else {			// Is VM locked or unlocked; must use CAS
    assert( (pd_sp&3) || Thread::current() == this || is_being_ext_suspended(), 
"cannot ask for last_Java_sp remotely with jvm_lock unlocked unless "
"also set forced-poll bit, or thread is indefinitely suspended");
    if( !_anchor.is_walkable( pd_sp ) ) {
Thread*current_thread=Thread::current();
      if( current_thread == this ) {
os::make_self_walkable();
        AuditTrail::log_time(this, AuditTrail::MAKE_SELF_WALKABLE, 2);
      } else {
os::make_remote_walkable(this);
        AuditTrail::log_time(this, AuditTrail::MAKE_REMOTE_WALKABLE, intptr_t(current_thread), 1);
      }
    
      // If we are VM locked, this is all safe.
      
      // Only JVMDI/PI looks at the stack this way:
      // If we are unlocked, then we should have set the polling-advisory bit
      // before inspecting the jvm_lock.  If it is still unlocked, then the
      // running Java thread will block before re-entering Java code and thus
      // allow his stack to be inspected.
      intptr_t pd_sp2 = JavaFrameAnchor::set_walkable(pd_sp);
      while( !jvm_lock_update_attempt(pd_sp2,pd_sp) ) {
        pd_sp = _jvm_lock;	// Reload; possible contention on setting the walkable bit
        assert( (pd_sp&3) || Thread::current() == this || is_being_ext_suspended(), 
"cannot make stack coherent remotely with jvm_lock unlocked unless "
"also set forced-poll bit, or thread is indefinitely suspended");
        pd_sp2 = JavaFrameAnchor::set_walkable(pd_sp);
      }
    } 
  }

  // Stack is now coherent.  But it may be "dirty" with invalid objectRefs when
  // pauseless GC is used.  So the stack cannot be safely walked until it has
  // been cleaned.

  // Do not let the caller see the walkable bit; he doesn't care for it.
  return (intptr_t*)JavaFrameAnchor::clr_walkable(pd_sp&~3);
}


// Get the last Java SP.  As part of this, we make sure the stack is coherent.
// See the comment on make_stack_coherent() for locking requirements when calling
// this method.
intptr_t* JavaThread::last_Java_sp() {
  // Make sure the stack is walkable.
  intptr_t* last_java_sp = make_stack_coherent();

Thread*current_thread=Thread::current();
  int     self_suspend_flags = please_self_suspend();

  // If PauselessGC or GenPauselessGC is running, it may have left a thread stack
  // in a "dirty" state, and set the pgc_clean_stk, gpgc_clean_new, or gpgc_clean_old
  // self suspend flags.  Anyone wanting to walk the stack of that thread has to
  // clean the stack first.
  if ( self_suspend_flags & (gpgc_clean_new|gpgc_clean_old) ) {
    if ( current_thread->is_GenPauselessGC_thread() ||
         current_thread->is_GC_task_thread() )
    {
      assert0( ! current_thread->is_stack_cleaning() );
    } else {
      assert0( current_thread->is_Java_thread() || current_thread->is_VM_thread() );

      if ( ! current_thread->is_stack_cleaning() ) {

if(current_thread->is_Java_thread()){
          if ( current_thread == this ) {
            assert0(current_thread->is_Java_thread());
assert(jvm_locked_by_self(),"Can't clean your own stack without owning your _jvm_lock");
          } else {
            // If one JavaThread is trying to access another, it better hold its own _jvm_lock,
            // and have its NMT state up to date.
            assert(((JavaThread*)current_thread)->jvm_locked_by_self(),
"Must own your own _jvm_lock when cleaning another thread.");
            assert(jvm_locked_by_VM() || is_being_ext_suspended(),
"Must own lock, or thread to be cleaned must be suspended");
          }
        }

        GPGC_ThreadCleaner::LVB_thread_stack(this);
        clr_suspend_request((JavaThread::SuspendFlags)(gpgc_clean_new | gpgc_clean_old));
      }
    }
  }

  // Here we have a walkable stack and the valid SP in hand.
  return last_java_sp;
}


vframe JavaThread::last_java_vframe() { return vframe(this); }

klassOop JavaThread::security_get_caller_class(int depth) {  
  ResourceMark rm;
  vframe vf(this);
vf.security_get_caller_frame(depth);
  return vf.done() ? NULL : vf.method()->method_holder();
}

// --- extend_lckstk
void JavaThread::extend_lckstk_impl(){
  assert0( _lckstk_base <= _lckstk_top && _lckstk_top <= _lckstk_max );
  int delta = _lckstk_top - _lckstk_base;  // units of intptr_t
  int size  = _lckstk_max - _lckstk_base;  // units of intptr_t
  int newsize = size+(size>>1)+1;          // units of intptr_t; grow by 50%
  objectRef *base = _lckstk_base 
    ? REALLOC_C_HEAP_ARRAY(objectRef,_lckstk_base,newsize)
    :     NEW_C_HEAP_ARRAY(objectRef,             newsize);
_lckstk_base=base;
  _lckstk_top  = base+delta;
  _lckstk_max  = base+newsize;
  assert0( _lckstk_base <= _lckstk_top && _lckstk_top <  _lckstk_max );
}


static void compiler_thread_entry(JavaThread* thread, TRAPS) {
  assert(thread->is_Compiler_thread(), "must be compiler thread");
  VMTagMark vmt(thread, VM_CompilerInterface_tag);
  ((CompilerThread*)thread)->cb()->consumer_main_loop();
}

// Create a CompilerThread
CompilerThread::CompilerThread(int tid):JavaThread(&compiler_thread_entry),_tid(tid),_env(NULL),_task(NULL){
}

CompileBroker *C1CompilerThread::cb() const { 
  return &CompileBroker::_c1; 
}

CompileBroker *C2CompilerThread::cb() const {
  return &CompileBroker::_c2;
}

const char *CompilerThread::status_msg() const {
  // We must own the CompileTask_lock to prevent destruction of a CompileTask
  // while we're looking at it.
  // CNC - But causes recursive error msg death when debugging compiler errors
  //assert(CompileTask_lock->owned_by_self(), "must own CompileTask_lock to construct a CompilerThread status");
JavaThread*thr=JavaThread::current();
  CompileTask* tsk = _task; // read CompileTask (if any) only once
  if (!tsk) return "idle";
  bool was_locked = thr->jvm_locked_by_self();
  if (was_locked || thr->jvm_lock_self_attempt()) {
    methodOop moop = (methodOop) JNIHandles::resolve(tsk->_method);
    Klass *klass = Klass::cast(moop->constants()->pool_holder());
const char*k_name=klass->external_name();
const char*m_name=moop->name()->as_C_string();
    if (!was_locked) thr->jvm_unlock_self();
    char* buf = NEW_RESOURCE_ARRAY(char, strlen(k_name)+1+strlen(m_name)+1);
sprintf(buf,"compiling %s.%s",k_name,m_name);
    return buf;
  } else {
return"CompileTask not available.";
  }
}



// ======= Threads ========

// The Threads class links together all active threads, and provides
// operations over all threads.  It is protected by its own AzLock
// lock, which is also used in other contexts to protect thread
// operations from having the thread being operated on from exiting
// and going away unexpectedly (e.g., safepoint synchronization)

JavaThread* Threads::_thread_list = NULL;
int         Threads::_number_of_threads = 0;
int         Threads::_number_of_non_daemon_threads = 0;
int         Threads::_return_code = 0;
size_t      JavaThread::_stack_size_at_create = 0;

// All JavaThreads
#define ALL_JAVA_THREADS(X) for (JavaThread* X = _thread_list; X; X = X->next())

void os_stream();

// All JavaThreads + all non-JavaThreads (i.e., every thread in the system)
void Threads::threads_do(ThreadClosure* tc) {
  assert_locked_or_safepoint(Threads_lock);
  // ALL_JAVA_THREADS iterates through all JavaThreads
  ALL_JAVA_THREADS(p) {
    tc->do_thread(p);
  }
  // Someday we could have a table or list of all non-JavaThreads.
  // For now, just manually iterate through them.
  tc->do_thread(VMThread::vm_thread());
  Universe::heap()->gc_threads_do(tc);
  // The WatcherThread can be stopped outside of the Threads_lock
  WatcherThread* wt = WatcherThread::watcher_thread();
  if (wt) {
tc->do_thread(wt);
  }
  // If CompilerThreads ever become non-JavaThreads, add them here
}

long First_Tick;                // First tick since VM creation

jint Threads::create_vm(JavaVMInitArgs* args, bool* canTryAgain) {
 
  // Check version
  if (!is_supported_jni_version(args->version)) return JNI_EVERSION;

  // Quicky LIBOS layout check.  LIBOS and HotSpot agree on the layout of the
  // first fields of Thread*.
  assert0( offset_of(ThreadShadow, _libos01) == offset_of(ThreadBase, _libos01) );

  // Initialize stuff that cannot be done in static constructors.
  static_init();

  // Initialize the output stream module
  ostream_init();

  // Process java launcher properties.
  Arguments::process_sun_java_launcher_properties(args);

  // Initialize the os module before using TLS
  os::init();
First_Tick=os::elapsed_counter();

  // Initialize system properties.
  Arguments::init_system_properties();

  // Parse arguments
  jint parse_result = Arguments::parse(args);
  if (parse_result != JNI_OK) return parse_result;

  // Wait for debug ?
  if (WaitForDebugger) {
    tty->print_cr("Spinning waiting for a debugger to attach (PID = "INT64_FORMAT").\n", (long) os::process_id());
  }

  while (WaitForDebugger) {
    os::yield_all(1);
  }

  if (UseTickProfiler) {
tickprofiling::init();
  }

#ifdef AZ_PROFILER
#ifdef AZ_PROXIED
  // TODO: perhaps move initialization here for Tall as well.
  // start_arta(); 20100124 Bug 25480 move initialization back to start of backend process
  // But we do need to initialize ARTA's logging level because it's an XX option.
  azprof::Log::set_log_level(ARTALogLevel);
#endif
#endif // AZ_PROFILER

  // Record VM creation timing statistics
  TraceVmCreationTime create_vm_timer;
  create_vm_timer.start();

  // Timing (must come after argument parsing)
  TraceTime timer("Create VM", TraceStartupTime);

  // Initialize the os module after parsing the args
  jint os_init_2_result = os::init_2();
  if (os_init_2_result != JNI_OK) return os_init_2_result;

  // Initialize output stream logging
  ostream_init_log();

  // Convert -Xrun to -agentlib: if there is no JVM_OnLoad
  // Must be before create_vm_init_agents()
  if (Arguments::init_libraries_at_startup()) {
    convert_vm_init_libraries_to_agents();
    // Bug 23852 -  Turn off SMA if we are going to install instrumented stubs
    // for JVMPI. We need this check here to make sure that the instrumented
    // monitor stubs for JVMPI get generated correctly without SMA.
    UseSMA = false;
  }

  // Launch -agentlib/-agentpath and converted -Xrun agents
  if (Arguments::init_agents_at_startup()) {
    create_vm_init_agents();
    // Bug 23852 -  Turn off SMA if we are going to install instrumented stubs
    // for JVMTI. We need this check here to make sure that the instrumented
    // monitor stubs for JVMTI get generated correctly without SMA.
    UseSMA = false;
  }

  // Initialize Threads state
  _thread_list = NULL;
  _number_of_threads = 0;
  _number_of_non_daemon_threads = 0;

  guarantee(sizeof(objectRef) == oopSize, "objectRef size mismatch");

  // Initialize object profiles.
AllocatedObjects::init();
LiveObjects::init();

  // Initialize global data structures and create system classes in heap
  vm_init_globals();

  if (UseSMA) {
SmaHeuristic::initialize();
  }

#ifndef PRODUCT
  if (UseITR) {
    ITRCollectionTimeStart += os::elapsed_counter();
  }
#endif

  // Attach the main thread to this os thread
#if defined(AZUL)
  // The JavaThread *is* the current user thread, but we can't use
  // JavaThread::current() as the thread is not yet initialized and it's
  // critical not to make any virtual calls.
Thread*this_thread=Thread::current();
  guarantee(sizeof(JavaThread) < USER_THREAD_SPECIFIC_DATA_SIZE, "azul pre-allocated threadLS too small");
  // Note: memory is pre-zeroed by OS/crt1.o
  // Use placement allocation so we run the constructor as well.
  JavaThread* main_thread = new (this_thread) JavaThread();
#else // !AZUL:
  JavaThread* main_thread = new JavaThread();
#endif // !AZUL
  VMTagMark vmt(main_thread, VM_Startup_tag);
  
  // HACK: until we can turn on tick profiling for all existing threads, turn
  // it on for the current (proto) thread as well since we end up using it
  // to do work under AZLINUX.
  if (UseTickProfiler) {
      assert0(Thread::current()->_profiling_timer_id == 0);
      tickprofiling::thread_init_callback(reinterpret_cast<intptr_t>(Thread::current()));
  }
  
  // must do this before set_active_handles and initialize_thread_local_storage
  // Note: on solaris initialize_thread_local_storage() will (indirectly)
  // change the stack size recorded here to one based on the java thread 
  // stacksize. This adjusted size is what is used to figure the placement
  // of the guard pages.
main_thread->pd_initialize();
  main_thread->initialize_thread_local_storage();

  main_thread->set_active_handles(JNIHandleBlock::allocate_block());

  if (!main_thread->set_as_starting_thread()) {
    vm_shutdown_during_initialization(
      "Failed necessary internal allocation. Out of swap space");
    delete main_thread;
    *canTryAgain = false; // don't let caller call JNI_CreateJavaVM again
    return JNI_ENOMEM;
  }

  // Must be called before init_globals.
  if (ProfileAllocatedObjects) {
    main_thread->allocated_objects()->grow(AllocatedObjects::mutator_size());
  }

  HandleMark hm;

  // Initialize global modules
  jint status = init_globals();
  if (status != JNI_OK) {
    delete main_thread;
    *canTryAgain = false; // don't let caller call JNI_CreateJavaVM again
    return status;
  }

  { MutexLockerAllowGC mu(Threads_lock,main_thread);
    Threads::add(main_thread, false);
  }

  // Any JVMTI raw monitors entered in onload will transition into
  // real raw monitor. VM is setup enough here for raw monitor enter.
  JvmtiExport::transition_pending_onload_raw_monitors();

  if (VerifyBeforeGC &&
      Universe::heap()->total_collections() >= VerifyGCStartAt) {
    Universe::heap()->prepare_for_verify();
    Universe::verify();   // make sure we're starting with a clean slate
Universe::heap()->post_verify();
  }

  // Create the VMThread  
  { TraceTime timer("Start VMThread", TraceStartupTime);
    VMThread::create();
    Thread* vmthread = VMThread::vm_thread();
    
if(!os::create_thread(vmthread,ttype::vm_thread))
      vm_exit_during_initialization("Cannot create VM thread. Out of system resources.");

    // Wait for the VM thread to become ready, and VMThread::run to initialize
    // xMonitors can have spurious returns, must always check another state flag
    {
MutexLockerAllowGC ml(Notify_lock,main_thread);
      os::start_thread(vmthread);
      while (vmthread->active_handles() == NULL) {
Notify_lock.wait();
      }
    }
  }
  
  assert (Universe::is_fully_initialized(), "not initialized");
  EXCEPTION_MARK;

  // Always call even when there are not JVMTI environments yet, since environments
  // may be attached late and JVMTI must track phases of VM execution
  JvmtiExport::enter_start_phase();

  // Notify JVMTI agents that VM has started (JNI is up) - nop if no agents.
  JvmtiExport::post_vm_start();

  // Must wait to ask for loads of c-heap until java heap is initialized.
  if (UseITR) {
InstructionTraceThreads::initialize();

    bool allocateFromBigHunk=1;
    int tracesPerArray = 1<<ITRArraySize;
    // +1 so that we can align the big hunk
    size_t arraySize = tracesPerArray * InstructionTraceArray::_traceSizeInWords * sizeof(long);
    size_t size = (ITRNumArrays+1) * arraySize;
    char* mapAddress = os::reserve_memory(size, (char*) __ITR_START_ADDR__, true);
if(mapAddress!=NULL){
      // commit memory
      if (!os::commit_memory(mapAddress, size, Modules::ITR)) {
        os::release_memory(mapAddress, size);
        allocateFromBigHunk=0;
      }
    } else {
      allocateFromBigHunk=0;
    }

    // Align map address
    long mask = ~((long)arraySize-1);
    if ( ((long)mapAddress & mask) != (long)mapAddress ) {
      mapAddress = (char*)( ((long)mapAddress & mask) + (long)arraySize );
    }
    if (allocateFromBigHunk) {
tty->print_cr("[ITR] Allocating %zd contiguous bytes for tracing arrays",size);
for(int i=0;i<ITRNumArrays;i++){
        address tempAddress = ((long)i) * arraySize + (address)mapAddress;
        InstructionTraceManager::addEmptyTrace(new InstructionTraceArray(tempAddress));
      }
    } else {
tty->print_cr("[ITR] Could not pre-allocate a large hunk of memory (%zd bytes were requested)",size);
tty->print_cr("[ITR] Falling back on good ol' malloc, will probably crash with OOM (%zd array size requested)",arraySize);
for(int i=0;i<ITRNumArrays;i++){
        InstructionTraceManager::addEmptyTrace(new InstructionTraceArray());
      }
    }

for(int i=0;i<ITRMinThreads;i++){
      Atomic::inc_ptr(&InstructionTraceThreads::_numThreads);
      InstructionTraceThread* srt = new (ttype::insttrace_thread) InstructionTraceThread(InstructionTraceThreads::getUniqueID());
      InstructionTraceThreads::pushInstructionTraceThread(srt);
    }
  }

  {
    TraceTime timer("Initialize java.lang classes", TraceStartupTime);

    if (EagerXrunInit && Arguments::init_libraries_at_startup()) {
      create_vm_init_libraries();
    }

    if (InitializeJavaLangString) {
      initialize_class(vmSymbolHandles::java_lang_String(), CHECK_0);
    } else {
      warning("java.lang.String not initialized");
    }

    // Initialize java_lang.System (needed before creating the thread)
    if (InitializeJavaLangSystem) {      
      initialize_class(vmSymbolHandles::java_lang_System(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_ThreadGroup(), CHECK_0);
      Handle thread_group = create_initial_thread_group(CHECK_0);
      Universe::set_main_thread_group(thread_group());
      initialize_class(vmSymbolHandles::java_lang_Thread(), CHECK_0);
      oop thread_object = create_initial_thread(thread_group, main_thread, CHECK_0);
      main_thread->set_threadObj(thread_object);
      // Set thread status to running since main thread has 
      // been started and running.
      java_lang_Thread::set_thread_status(thread_object, 
					  java_lang_Thread::RUNNABLE);
  
      // The VM preresolve methods to these classes. Make sure that get initialized
      initialize_class(vmSymbolHandles::java_lang_reflect_Method(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_ref_Finalizer(),  CHECK_0);
      // The VM creates & returns objects of this class. Make sure it's initialized.
      initialize_class(vmSymbolHandles::java_lang_Class(), CHECK_0);
      call_initializeSystemClass(CHECK_0);
    } else {
      warning("java.lang.System not initialized");
    }

    // an instance of OutOfMemory exception has been allocated earlier
    if (InitializeJavaLangExceptionsErrors) {
      initialize_class(vmSymbolHandles::java_lang_OutOfMemoryError(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_NullPointerException(), CHECK_0);
initialize_class(vmSymbolHandles::java_lang_ArrayIndexOutOfBoundsException(),CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_ClassCastException(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_ArrayStoreException(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_ArithmeticException(), CHECK_0);
      initialize_class(vmSymbolHandles::java_lang_StackOverflowError(), CHECK_0);      
      initialize_class(vmSymbolHandles::java_lang_IllegalMonitorStateException(), CHECK_0);      
    } else {
      warning("java.lang.OutOfMemoryError has not been initialized");
      warning("java.lang.NullPointerException has not been initialized");
      warning("java.lang.ClassCastException has not been initialized");
      warning("java.lang.ArrayStoreException has not been initialized");
      warning("java.lang.ArithmeticException has not been initialized");
      warning("java.lang.StackOverflowError has not been initialized");
    }
  }
  
  // See        : bugid 4211085.  
  // Background : the static initializer of java.lang.Compiler tries to read 
  //              property"java.compiler" and read & write property "java.vm.info".
  //              When a security manager is installed through the command line
  //              option "-Djava.security.manager", the above properties are not
  //              readable and the static initializer for java.lang.Compiler fails
  //              resulting in a NoClassDefFoundError.  This can happen in any
  //              user code which calls methods in java.lang.Compiler.  
  // Hack :       the hack is to pre-load and initialize this class, so that only
  //              system domains are on the stack when the properties are read.
  //              Currently even the AWT code has calls to methods in java.lang.Compiler.
  //              On the classic VM, java.lang.Compiler is loaded very early to load the JIT.
  // Future Fix : the best fix is to grant everyone permissions to read "java.compiler" and 
  //              read and write"java.vm.info" in the default policy file. See bugid 4211383
  //              Once that is done, we should remove this hack.
  initialize_class(vmSymbolHandles::java_lang_Compiler(), CHECK_0);
  
  // More hackery - the static initializer of java.lang.Compiler adds the string "nojit" to
  // the java.vm.info property if no jit gets loaded through java.lang.Compiler (the hotspot
  // compiler does not get loaded through java.lang.Compiler).  "java -version" with the 
  // hotspot vm says "nojit" all the time which is confusing.  So, we reset it here.
  // This should also be taken out as soon as 4211383 gets fixed.
  reset_vm_info_property(CHECK_0);
  
  quicken_jni_functions();

  // Set flag that basic initialization has completed. Used by exceptions and various 
  // debug stuff, that does not work until all basic classes have been initialized.
  set_init_completed();

  // record VM initialization completion time
  Management::record_vm_init_completed();

  // Compute system loader. Note that this has to occur after set_init_completed, since
  // valid exceptions may be thrown in the process. 
  // Note that we do not use CHECK_0 here since we are inside an EXCEPTION_MARK and 
  // set_init_completed has just been called, causing exceptions not to be shortcut
  // anymore. We call vm_exit_during_initialization directly instead.
  SystemDictionary::compute_java_system_loader(THREAD);
  if (HAS_PENDING_EXCEPTION) {
    vm_exit_during_initialization(Handle(THREAD, PENDING_EXCEPTION));
  }
  
  // Support for GPGC. This should be cleaned up and better encapsulated.
  if (UseGenPauselessGC) {
SurrogateLockerThread::makeSurrogateLockerThread(THREAD);
    if (HAS_PENDING_EXCEPTION) {
      vm_exit_during_initialization(Handle(THREAD, PENDING_EXCEPTION));
    }
  }

  // Always call even when there are not JVMTI environments yet, since environments
  // may be attached late and JVMTI must track phases of VM execution
  JvmtiExport::enter_live_phase();

  // Signal Dispatcher needs to be started before VMInit event is posted
  os::signal_init();

  // Start Attach Listener if +StartAttachListener or it can't be started lazily
  if (!DisableAttachMechanism) {
    if (StartAttachListener || AttachListener::init_at_startup()) {
      AttachListener::init();
    }
  }

  // Launch -Xrun agents
  // Must be done in the JVMTI live phase so that for backward compatibility the JDWP 
  // back-end can launch with -Xdebug -Xrunjdwp.
  if (!EagerXrunInit && Arguments::init_libraries_at_startup()) {
    create_vm_init_libraries();
  }

  // JVMTI will be initialized before the init_globals call. So that should be fine.
  if (StubRoutines::should_install_instrumented_stubs()) {
    StubGenerator_install_instrumented_stubs();
  }

  // Notify JVMTI agents that VM initialization is complete - nop if no agents.
  JvmtiExport::post_vm_initialized();

  Chunk::start_chunk_pool_cleaner_task();
ObjectSynchronizer::start_monitor_deflate_task();
  if( UseSBA ) {
    struct SBA_Decay_Task : public PeriodicTask {
      int x;            // Dial-back decay when running in debug modes
      SBA_Decay_Task() : PeriodicTask(SBADecayInterval) {}
      void task() { 
#ifdef ASSERT  
        if( ((x++)&7) == 0 )    // System is much slower, so decay needs to be much slower
#endif
          SBAPreHeader::decay_escape_sites(); 
      }
    };
    SBA_Decay_Task *dt = new SBA_Decay_Task();
dt->enroll();
  }

  Management::initialize(THREAD);
  if (HAS_PENDING_EXCEPTION) {
    // management agent fails to start possibly due to 
    // configuration problem and is responsible for printing
    // stack trace if appropriate. Simply exit VM.
    vm_exit(1);
  }

  if (MemProfiling)                   MemProfiler::engage();    
  StatSampler::engage();    
  if (CheckJNICalls)                  JniPeriodicChecker::engage();
  if (CacheTimeMillis)                TimeMillisUpdateTask::engage();
  if (UseSMA) {
SmaHeuristicUpdaterTask::engage();
SmaHeuristicSamplerTask::engage();
  }

#ifdef AZ_PROXIED
  // TODO: need to make this work on stock linux before removing this AZ_PROXIED
SharedUserData::engage();
#endif // AZ_PROXIED

  // Start up the WatcherThread if there are any periodic tasks
  // NOTE:  All PeriodicTasks should be registered by now. If they
  //   aren't, late joiners might appear to start slowly (we might
  //   take a while to process their first tick).
  if (PeriodicTask::num_tasks() > 0) {
    WatcherThread::start();
  }

#ifdef AZ_PROFILER
  if (RegisterWithARTA) {
#if !defined(AZ_PROXIED)
    HotSpotServlet::start_arta_noproxy();
#endif
HotSpotServlet::init();
  }
#endif // AZ_PROFILER

  if (UseITR && ITRHijackMemcpy) {
    // Patch other the original memcpy to point to me
    os::redirectStaticFunction((address)&memcpy, StubRoutines::memcpy());
  }

  create_vm_timer.end();
  return JNI_OK;
}

// type for the Agent_OnLoad and JVM_OnLoad entry points
extern "C" {
  typedef jint (JNICALL *OnLoadEntry_t)(JavaVM *, char *, void *);
}
// Find a command line agent library and return its entry point for
//         -agentlib:  -agentpath:   -Xrun
// num_symbol_entries must be passed-in since only the caller knows the number of symbols in the array.
static OnLoadEntry_t lookup_on_load(AgentLibrary* agent, const char *on_load_symbols[], size_t num_symbol_entries) {
  OnLoadEntry_t on_load_entry = NULL;
  void *library = agent->os_lib();  // check if we have looked it up before

  if (library == NULL) {
    char buffer[JVM_MAXPATHLEN];
    char ebuf[1024];
    const char *name = agent->name();

    if (agent->is_absolute_path()) {
      library = hpi::dll_load(name, ebuf, sizeof ebuf);
      if (library == NULL) {
        // If we can't find the agent, exit.
        vm_exit_during_initialization("Could not find agent library in absolute path", name);
      }
    } else {
      // Try to load the agent from the standard dll directory
hpi::dll_build_name(buffer,sizeof(buffer),Arguments::get_dll_dir(),name,UseDebugLibrarySuffix);
      library = hpi::dll_load(buffer, ebuf, sizeof ebuf);
      if (library == NULL) { // Try the local directory
        char ns[1] = {0};
hpi::dll_build_name(buffer,sizeof(buffer),ns,name,UseDebugLibrarySuffix);
        library = hpi::dll_load(buffer, ebuf, sizeof ebuf);
        if (library == NULL) {
          // If we can't find the agent, exit.
          vm_exit_during_initialization("Could not find agent library on the library path or in the local directory", name);
        }
      }
    }
    agent->set_os_lib(library);
  }

  // Find the OnLoad function.
  for (size_t symbol_index = 0; symbol_index < num_symbol_entries; symbol_index++) {
    on_load_entry = CAST_TO_FN_PTR(OnLoadEntry_t, hpi::dll_lookup(library, on_load_symbols[symbol_index]));
    if (on_load_entry != NULL) break;
  }
  return on_load_entry;
}

// Find the JVM_OnLoad entry point
static OnLoadEntry_t lookup_jvm_on_load(AgentLibrary* agent) {
  const char *on_load_symbols[] = JVM_ONLOAD_SYMBOLS;
  return lookup_on_load(agent, on_load_symbols, sizeof(on_load_symbols) / sizeof(char*));
}

// Find the Agent_OnLoad entry point
static OnLoadEntry_t lookup_agent_on_load(AgentLibrary* agent) {
  const char *on_load_symbols[] = AGENT_ONLOAD_SYMBOLS;
  return lookup_on_load(agent, on_load_symbols, sizeof(on_load_symbols) / sizeof(char*));
}

// For backwards compatibility with -Xrun
// Convert libraries with no JVM_OnLoad, but which have Agent_OnLoad to be
// treated like -agentpath:
// Must be called before agent libraries are created
void Threads::convert_vm_init_libraries_to_agents() {
  AgentLibrary* agent;
  AgentLibrary* next;

  for (agent = Arguments::libraries(); agent != NULL; agent = next) {
    next = agent->next();  // cache the next agent now as this agent may get moved off this list
    OnLoadEntry_t on_load_entry = lookup_jvm_on_load(agent);

    // If there is an JVM_OnLoad function it will get called later,
    // otherwise see if there is an Agent_OnLoad
    if (on_load_entry == NULL) {
      on_load_entry = lookup_agent_on_load(agent);
      if (on_load_entry != NULL) {
        // switch it to the agent list -- so that Agent_OnLoad will be called,
        // JVM_OnLoad won't be attempted and Agent_OnUnload will
        Arguments::convert_library_to_agent(agent);
      } else {
        vm_exit_during_initialization("Could not find JVM_OnLoad or Agent_OnLoad function in the library", agent->name());
      }
    }
  }
}

// Create agents for -agentlib:  -agentpath:  and converted -Xrun
// Invokes Agent_OnLoad
// Called very early -- before JavaThreads exist
void Threads::create_vm_init_agents() {
  extern struct JavaVM_ main_vm; 
  AgentLibrary* agent;

  JvmtiExport::enter_onload_phase();
  for (agent = Arguments::agents(); agent != NULL; agent = agent->next()) {
    OnLoadEntry_t  on_load_entry = lookup_agent_on_load(agent);

    if (on_load_entry != NULL) {
      // Invoke the Agent_OnLoad function
      jint err = (*on_load_entry)(&main_vm, agent->options(), NULL);
      if (err != JNI_OK) {
        vm_exit_during_initialization("agent library failed to init", agent->name());
      }
    } else {
      vm_exit_during_initialization("Could not find Agent_OnLoad function in the agent library", agent->name());
    }
  }
  JvmtiExport::enter_primordial_phase();
}

// Called for after the VM is initialized for -Xrun libraries which have not been converted to agent libraries
// Invokes JVM_OnLoad
void Threads::create_vm_init_libraries() {
  // Azul note: Need to do something specific here: trigger proxy loads thereof,
  // though the proxy might already have done them itself.

  extern struct JavaVM_ main_vm;
  AgentLibrary* agent;

  for (agent = Arguments::libraries(); agent != NULL; agent = agent->next()) {
    OnLoadEntry_t on_load_entry = lookup_jvm_on_load(agent);

    if (on_load_entry != NULL) {
      // Invoke the JVM_OnLoad function
      JavaThread* thread = JavaThread::current();
      ThreadToNativeFromVM ttn(thread);
      HandleMark hm(thread);
      jint err = (*on_load_entry)(&main_vm, agent->options(), NULL);
      if (err != JNI_OK) {
        vm_exit_during_initialization("-Xrun library failed to init", agent->name());
      }
    } else {
      vm_exit_during_initialization("Could not find JVM_OnLoad function in -Xrun library", agent->name());
    }
  }
}

// Last thread running calls java.lang.Shutdown.shutdown()
void JavaThread::invoke_shutdown_hooks() {
  HandleMark hm(this);

  // We could get here with a pending exception, if so clear it now.
  if (this->has_pending_exception()) {
    this->clear_pending_exception();
  }

  EXCEPTION_MARK;
  klassOop k =
    SystemDictionary::resolve_or_null(vmSymbolHandles::java_lang_Shutdown(),
                                      THREAD);
  if (k != NULL) {
    // SystemDictionary::resolve_or_null will return null if there was
    // an exception.  If we cannot load the Shutdown class, just don't
    // call Shutdown.shutdown() at all.  This will mean the shutdown hooks
    // and finalizers (if runFinalizersOnExit is set) won't be run.
    // Note that if a shutdown hook was registered or runFinalizersOnExit
    // was called, the Shutdown class would have already been loaded
    // (Runtime.addShutdownHook and runFinalizersOnExit will load it).
    instanceKlassHandle shutdown_klass (THREAD, k);
    JavaValue result(T_VOID);
    JavaCalls::call_static(&result,
                           shutdown_klass,
                           vmSymbolHandles::shutdown_method_name(),
                           vmSymbolHandles::void_method_signature(),
                           THREAD);
  }
  CLEAR_PENDING_EXCEPTION;
}

// Threads::destroy_vm() is normally called from jni_DestroyJavaVM() when 
// the program falls off the end of main(). Another VM exit path is through 
// vm_exit() when the program calls System.exit() to return a value or when 
// there is a serious error in VM. The two shutdown paths are not exactly
// the same, but they share Shutdown.shutdown() at Java level and before_exit()
// and VM_Exit op at VM level.
//
// Shutdown sequence:
//   + Wait until we are the last non-daemon thread to execute
//     <-- every thing is still working at this moment -->
//   + Call java.lang.Shutdown.shutdown(), which will invoke Java level
//        shutdown hooks, run finalizers if finalization-on-exit
//   + Call before_exit(), prepare for VM exit
//      > run VM level shutdown hooks (they are registered through JVM_OnExit(),
//        currently the only user of this mechanism is File.deleteOnExit())
//      > stop flat profiler, StatSampler, watcher thread, CMS threads,
//        post thread end and vm death events to JVMTI,
//        stop signal thread
//   + Call JavaThread::exit(), it will:
//      > release JNI handle blocks, remove stack guard pages
//      > remove this thread from Threads list
//     <-- no more Java code from this thread after this point -->
//   + Stop VM thread, it will bring the remaining VM to a safepoint and stop
//     the compiler threads at safepoint
//     <-- do not use anything that could get blocked by Safepoint -->
//   + Disable tracing at JNI/JVM barriers
//   + Set _vm_exited flag for threads that are still running native code
//   + Delete this thread
//   + Call exit_globals()
//      > deletes tty
//      > deletes PerfMemory resources
//   + Return to caller

bool Threads::destroy_vm() {
  JavaThread* thread = JavaThread::current();

  // Wait until we are the last non-daemon thread to execute
{MutexLockerAllowGC nu(Threads_lock,thread);
    while (Threads::number_of_non_daemon_threads() > 1 )
Threads_lock.wait();
  }

  // Hang forever on exit if we are reporting an error.
  if (ShowMessageBoxOnError && is_error_reported()) {
    os::infinite_sleep();
  }

  // run Java level shutdown hooks
  thread->invoke_shutdown_hooks();

  before_exit(thread);

  thread->exit(true);

  // Stop VM thread.
  {
    // 4945125 The vm thread comes to a safepoint during exit.
    // GC vm_operations can get caught at the safepoint, and the
    // heap is unparseable if they are caught. Grab the Heap_lock
    // to prevent this. The GC vm_operations will not be able to
    // queue until after the vm thread is dead.
MutexLockerAllowGC ml(Heap_lock,thread);

    VMThread::wait_for_vm_thread_exit();
    assert(SafepointSynchronize::is_at_safepoint(), "VM thread should exit at Safepoint");
  }

  // VMThread destroys itself upon exit since otherwise running its destructor
  // on the now non-existent in-stack thread object will cause a crash.
  // This change is non-cpu specific since it should be ok on Solaris/sparc as well.

  // Now, all Java threads are gone except daemon threads. Daemon threads
  // running Java code or in VM are stopped by the Safepoint. However,
  // daemon threads executing native code are still running.  But they
  // will be stopped at native=>Java/VM barriers. Note that we can't 
  // simply kill or suspend them, as it is inherently deadlock-prone.

#ifndef PRODUCT
  // disable function tracing at JNI/JVM barriers
  TraceHPI = false;
  TraceJNICalls = false;
  TraceJVMCalls = false;
  TraceRuntimeCalls = false;
#endif

  VM_Exit::set_vm_exited();

  notify_vm_shutdown();

  // Stop GPGC threads.
  // We are at a safepoint here.  The stop below will force the GC threads to
  // queue up for the Threads_lock.
  // The stop has to be called after VM_Exit::_vm_exited has been set to true
  // and before exit_globals is called.
  if (UseGenPauselessGC) {
    GPGC_Thread::stop_all();
  }

  delete thread;

  // exit_globals() will delete tty
  exit_globals();

  return true;
}


jboolean Threads::is_supported_jni_version_including_1_1(jint version) {
  if (version == JNI_VERSION_1_1) return JNI_TRUE;
  return is_supported_jni_version(version);
}


jboolean Threads::is_supported_jni_version(jint version) {
  if (version == JNI_VERSION_1_2) return JNI_TRUE;
  if (version == JNI_VERSION_1_4) return JNI_TRUE;
  if (version == JNI_VERSION_1_6) return JNI_TRUE;
  return JNI_FALSE;
}


void Threads::add(JavaThread* p, bool force_daemon) {
  // The threads lock must be owned at this point
  assert_locked_or_safepoint(Threads_lock);
  p->set_next(_thread_list);  
  _thread_list = p;
  _number_of_threads++;
  oop threadObj = p->threadObj();
  bool daemon = true;
  // Bootstrapping problem: threadObj can be null for initial
  // JavaThread (or for threads attached via JNI)
  if ((!force_daemon) && (threadObj == NULL || !java_lang_Thread::is_daemon(threadObj))) {
    _number_of_non_daemon_threads++;
    daemon = false;
  }

  ThreadService::add_thread(p, daemon);

  // Set the object allocation profile size to the current value while holding
  // the Threads_lock. The size is only changed at a safepoint.
  if (ProfileAllocatedObjects) {
    p->allocated_objects()->grow(AllocatedObjects::mutator_size());
  }

  // Possible GC point.
  Log::log3(NOTAG, Log::M_THREAD | Log::L_LO, TraceThreadEvents, "Thread added: " INTPTR_FORMAT, p);
}

void Threads::remove(JavaThread* p) {  
  // Extra scope needed for Thread_lock, so we can check
  // that we do not remove thread without safepoint code notice
  { MutexLockerAllowGC ml(Threads_lock,JavaThread::current());
  
    assert(includes(p), "p must be present");

    // Set this bit so Forevermore - other threads will realize we are not
    // going to be doing any more lock-unbiasing.
    p->_is_Complete_Java_thread = false;

    // Clean the unbias list: leave no biased-lock-desirer'ers hanging
p->unbias_all();

    JavaThread* current = _thread_list; 
    JavaThread* prev    = NULL;

    while (current != p) {
      prev    = current;
      current = current->next();
    }

    if (prev) {
      prev->set_next(current->next());
    } else {
      _thread_list = p->next();
    }  

    _number_of_threads--;
    oop threadObj = p->threadObj();
    bool daemon = true;
    if (threadObj == NULL || !java_lang_Thread::is_daemon(threadObj)) {
      _number_of_non_daemon_threads--;
      daemon = false;

      // Only one thread left, do a notify on the Threads_lock so a thread waiting
      // on destroy_vm will wake up.
      if (number_of_non_daemon_threads() == 1) 
Threads_lock.notify_all();
    }    
    ThreadService::remove_thread(p, daemon);

    if (ProfileAllocatedObjects) {
      AllocatedObjects::dead_threads()->add_all(p->allocated_objects());
      LiveObjects::dead_threads()->add_all(p->live_objects());
    }

    // Make sure that safepoint code disregard this thread. This is needed since
    // the thread might mess around with locks after this point. This can cause it
    // to do callbacks into the safepoint code. However, the safepoint code is not aware
    // of this thread since it is removed from the queue.
    p->set_terminated_value();        
    
    // Flush here as the last thing done while holding the Threads_lock.
    // The thread does not stop for safepoints after this point so flushing cannot be later on.
p->flush_unmarked_refs();
  } // unlock Threads_lock 

  // Since Events::log uses a lock, we grab it outside the Threads_lock
  Log::log3(NOTAG, Log::M_THREAD | Log::L_LO, TraceThreadEvents, "Thread exited: " INTPTR_FORMAT, p);
}

// Threads_lock must be held when this is called (or must be called during a safepoint)
bool Threads::includes(JavaThread* p) {  
assert(Threads_lock.is_locked(),"sanity check");
  ALL_JAVA_THREADS(q) {
    if (q == p ) {
      return true;
    }
  }
  return false;
}

// Operations on the Threads list for GC.  These are not explicitly locked,
// but the garbage collector must provide a safe context for them to run.
// In particular, these things should never be called when the Threads_lock
// is held by some other thread. (Note: the Safepoint abstraction also
// uses the Threads_lock to gurantee this property. It also makes sure that  
// all threads gets blocked when exiting or starting).

void Threads::oops_do(OopClosure* f) {
  ALL_JAVA_THREADS(p) {
    p->oops_do(f);
  }
  VMThread::vm_thread()->oops_do(f);
}

// Used by ParallelScavenge
void Threads::create_thread_roots_tasks(GCTaskQueue* q) {
  ALL_JAVA_THREADS(p) {
    q->enqueue(new ThreadRootsTask(p));
  }
  q->enqueue(new ThreadRootsTask(VMThread::vm_thread()));
}

// Used by Parallel Old
void Threads::create_thread_roots_marking_tasks(GCTaskQueue* q) {
  ALL_JAVA_THREADS(p) {
    q->enqueue(new ThreadRootsMarkingTask(p));
  }
  q->enqueue(new ThreadRootsMarkingTask(VMThread::vm_thread()));
}

void Threads::methodCodes_do(){
  ALL_JAVA_THREADS(p) {
p->methodCodes_do();
  }
VMThread::vm_thread()->methodCodes_do();
}

void Threads::gc_epilogue() {
  ALL_JAVA_THREADS(p) {
    p->gc_epilogue();
  }
}

void Threads::gc_prologue() {
  ALL_JAVA_THREADS(p) {    
    p->gc_prologue();
  }
}

// --- sba_find_owner
// Debugging call to lookup an address in all Threads' SBA spaces.
// In general not safe.
JavaThread *Threads::sba_find_owner( address adr ) {
Thread*T=Thread::current();
  JavaThread *jt = T->sba_thread(); // GC thread scanning remote thread?
  if( T == jt && jt->sba_is_in_or_oldgen(adr) )
    return jt;                  // Self-Stack-GC allows either to-space or from-space
  if( jt && jt->sba_is_in(adr) )
    return jt;                  // Remote stack scan is only into to-space
  // No grab of Threads_lock, so may die at random
  ALL_JAVA_THREADS(jt)
    if( jt->sba_area() && jt->sba_is_in(adr) )
      return jt;
  return NULL;
}


// Get count Java threads that are waiting to enter the specified monitor.
GrowableArray<JavaThread*>* Threads::get_pending_threads(int count,
  address monitor, bool doLock) {
  assert(doLock || SafepointSynchronize::is_at_safepoint(),
    "must grab Threads_lock or be at safepoint");
  GrowableArray<JavaThread*>* result = new GrowableArray<JavaThread*>(count);

  int i = 0;
  {
MutexLockerAllowGC ml(doLock?&Threads_lock:NULL,1);
    ALL_JAVA_THREADS(p) {
      if (p->is_Compiler_thread()) continue;

      address pending = (address)p->current_pending_monitor();
      if (pending == monitor) {             // found a match
        if (i < count) result->append(p);   // save the first count matches
        i++;
      }
    }
  }
  return result;
}


JavaThread *Threads::owning_thread_from_monitor_owner(address owner, bool doLock) {
  assert(doLock ||
Threads_lock.owned_by_self()||
         SafepointSynchronize::is_at_safepoint(),
         "must grab Threads_lock or be at safepoint");

  // NULL owner means not locked so we can skip the search
  if (owner == NULL) return NULL;

  {
MutexLockerAllowGC ml(doLock?&Threads_lock:NULL,1);
    ALL_JAVA_THREADS(p) {
      // first, see if owner is the address of a Java thread
      if (owner == (address)p) return p;
    }
  }
  if (UseHeavyMonitors) return NULL;

  //
  // If we didn't find a matching Java thread and we didn't force use of
  // heavyweight monitors, then the owner is the stack address of the
  // Lock Word in the owning Java thread's stack.
  //
  // We can't use Thread::is_lock_owned() or Thread::lock_is_in_stack() because
  // those routines rely on the "current" stack pointer. That would be our
  // stack pointer which is not relevant to the question. Instead we use the
  // highest lock ever entered by the thread and find the thread that is
  // higher than and closest to our target stack address.
  //
  address    least_diff = 0;
  bool       least_diff_initialized = false;
  JavaThread* the_owner = NULL;
  {
MutexLockerAllowGC ml(doLock?&Threads_lock:NULL,1);
    ALL_JAVA_THREADS(q) {
address addr=(address)q;
      if (addr == NULL || addr < owner) continue;  // thread has entered no monitors or is too low
      address diff = (address)(addr - owner);
      if (!least_diff_initialized || diff < least_diff) {
        least_diff_initialized = true;
        least_diff = diff;
        the_owner = q;
      }
    }
  }
  assert(the_owner != NULL, "Did not find owning Java thread for lock word address");
  return the_owner;
}

// Threads::print_on() is called at safepoint by VM_PrintThreads operation. 
void Threads::print_on(outputStream* st, bool print_stacks, bool internal_format, bool print_concurrent_locks) {
  char stime[64];
struct tm date;
  time_t secs = os::javaTimeMillis() / 1000;
  int result = 0;
  
  if (localtime_r(&secs, &date) != NULL) {
    result = strftime(stime, sizeof(stime), "%a %b %d %X %Z %Y", &date);
  }
  
  if (!result) {
sprintf(stime,"%.2lfs",os::elapsedTime());
  }
  
  int64_t quantum = os::get_thread_quantum();

  if (ProcessQuantumMS >= 0) {
st->print_cr("Full thread dump %s (%s %s [%s] [quantum %ldms]):",
                  Abstract_VM_Version::vm_name(),
                  Abstract_VM_Version::vm_release(),
Abstract_VM_Version::vm_info_string(),
                  stime,
                  quantum
                 );
  } else {
st->print_cr("Full thread dump %s (%s %s [%s]):",
                  Abstract_VM_Version::vm_name(),
                  Abstract_VM_Version::vm_release(),
Abstract_VM_Version::vm_info_string(),
                  stime
                 );
  }

  st->cr();

  // Dump concurrent locks
  // we dont support this since this requires a heap crawl and for us to that we need 
  // to go through the call_and_iterate call and have call back functions to make sure 
  // the heap is in the right state. Also heap crawls are not cheap.
  // Will revisit this if this functionality is needed.
#if 0 
  ConcurrentLocksDump concurrent_locks;
  if (print_concurrent_locks) {
    concurrent_locks.dump_at_safepoint();
  }
#endif 

  ALL_JAVA_THREADS(p) {
    ResourceMark rm;
    p->print_on(st);
    if (print_stacks) {
      if (internal_format) {
        p->trace_stack();
      } else {
        p->print_stack_on(st);
      }
    }
    st->cr();

#if 0 
    if (print_concurrent_locks) {
      concurrent_locks.print_locks_on(p, st);
    }
#endif
  }

  VMThread::vm_thread()->print_on(st);
  st->cr();
  Universe::heap()->print_gc_threads_on(st);
  WatcherThread* wt = WatcherThread::watcher_thread();
  if (wt != NULL) wt->print_on(st);
  st->cr();
  CompileBroker::print_compiler_threads_on(st);
  st->flush();
}

// Threads::print_on_error() is called by fatal error handler. It's possible
// that VM is not at safepoint and/or current thread is inside signal handler.
// Don't print stack trace, as the stack may not be walkable. Don't allocate 
// memory (even in resource area), it might deadlock the error handler.
void Threads::print_on_error(outputStream* st, Thread* current, char* buf, int buflen) {
  bool found_current = false;
  st->print_cr("Java Threads: ( => current thread )");
  ALL_JAVA_THREADS(thread) {
    bool is_current = (current == thread);
    found_current = found_current || is_current;

    st->print("%s", is_current ? "=>" : "  ");

    st->print(PTR_FORMAT, thread);
    st->print(" ");
    thread->print_on_error(st, buf, buflen);
    st->cr();
  }
  st->cr();

  st->print_cr("Other Threads:");
  if (VMThread::vm_thread()) {
    bool is_current = (current == VMThread::vm_thread());
    found_current = found_current || is_current;
    st->print("%s", current == VMThread::vm_thread() ? "=>" : "  ");

    st->print(PTR_FORMAT, VMThread::vm_thread());
    st->print(" ");
    VMThread::vm_thread()->print_on_error(st, buf, buflen);
    st->cr();
  }
  WatcherThread* wt = WatcherThread::watcher_thread();
  if (wt != NULL) {
    bool is_current = (current == wt);
    found_current = found_current || is_current;
    st->print("%s", is_current ? "=>" : "  ");

    st->print(PTR_FORMAT, wt);
    st->print(" ");
    wt->print_on_error(st, buf, buflen);
    st->cr();
  }
  if (!found_current) {
    st->cr();
    st->print("=>" PTR_FORMAT " (exited) ", current);
    current->print_on_error(st, buf, buflen);
    st->cr();
  }
}

void Threads::all_threads_print_xml_on(xmlBuffer *xb, int start, int stride, int detail, ThreadFilter *filt) {
  if (detail == 3) {
    frame::all_threads_print_xml_on(xb, start, stride);
  } else {
JavaThread*jt=JavaThread::current();
    MutexLockerAllowGC ctml(CompileTask_lock, jt); // So we can look at CompileTasks while printing a CompilerThread status
MutexLockerAllowGC tml(Threads_lock,jt);

    // Clear any ARTA state which was cached for a thread.
    // This is insurance coming into this code. Should have been cleared at the end of the previous ARTA request.
Threads::clear_arta_thread_states();
    
    // Determine the range of threads to include.
    int noft = number_of_threads();
    if (!((1 <= start) && (start < noft))) start = 1;
    if (stride <= 0) stride = 100;

    xmlElement xe(xb, "all_threads");
    xb->name_value_item("start", start);
    xb->name_value_item("stride", stride);
    int k = 1;
ALL_JAVA_THREADS(jt){
      if (!jt->is_hidden_from_external_view()) {
        if (filt->accept(jt)) {
          if ((start <= k) && (k <= (start + stride))) {
            jt->print_xml_on(xb, detail);
          }
          ++k;
        }
      }
    }
    xb->name_value_item("count", k-1);
    
    // Clear any ARTA state which was cached for a thread.
    // The state is a ResourceObj. Don't want the to survive past this scope.
Threads::clear_arta_thread_states();
  }
}

void Threads::thread_print_xml_on(int64_t thr_id, xmlBuffer *xb, int detail) {
  if (detail == 3) {
    frame::thread_id_print_xml_on(xb, thr_id);
  } else {
    if (VMThread::vm_thread()->osthread()->thread_id() == static_cast<pid_t>(thr_id)) {
      VMThread::vm_thread()->print_xml_on(xb, detail ? false : true);
      return;
    }
    WatcherThread* wt = WatcherThread::watcher_thread();
    if (wt != NULL && wt->osthread()->thread_id() == static_cast<pid_t>(thr_id)) {
      wt->print_xml_on(xb, detail ? false : true);
      return;
    }
JavaThread*jt=JavaThread::current();
    MutexLockerAllowGC ctml(CompileTask_lock,jt); // So we can look at CompileTasks while printing a CompilerThread status
MutexLockerAllowGC tml(Threads_lock,jt);
    
    // Clear any ARTA state which was cached for a thread.
Threads::clear_arta_thread_states();
    
ALL_JAVA_THREADS(t){
      if (t->osthread()->thread_id() == static_cast<pid_t>(thr_id)) {
        t->print_xml_on(xb, detail);
        // Clear any ARTA state which was cached for a thread.
        // The state is a ResourceObj. Don't want the to survive past this scope.
Threads::clear_arta_thread_states();
        return;
      }
    }
    // Clear any ARTA state which was cached for a thread.
    // The state is a ResourceObj. Don't want the to survive past this scope.
Threads::clear_arta_thread_states();
    { xmlElement xe(xb, "error");
xb->print_cr("Unknown Thread ID");
    }
  }
}

static void print_pc_reference_xml_on(const char *name, address pc, xmlBuffer *xb) {
  xmlElement xe(xb, name);
char buffer[64];
  sprintf(buffer, PTR_FORMAT, pc);
  xb->name_value_item("address", buffer);
  
  if (pc) {
    if (Interpreter::contains(pc)) {
InterpreterCodelet*codelet=Interpreter::codelet_containing(pc);
if(codelet!=NULL){
        codelet->print_xml_on(xb, true);
      } else {
        xb->name_value_item("name", "[Unknown interpreter code]");
      }
    } else {
CodeBlob*blob=CodeCache::find_blob(pc);
if(blob!=NULL){
StubCodeDesc*stub;
        if (!blob->is_methodCode() && ((stub = StubCodeDesc::desc_for(pc)) != NULL)) {
          stub->print_xml_on(xb, true);
        } else {
          blob->print_xml_on(xb, true, 0);
        }
      } else {
        char function_name[256];
        int offset;
        size_t size;
        bool found = os::dll_address_to_function_name(pc, function_name, sizeof(function_name), &offset, &size);
        if (found) {
          xb->name_value_item("name", function_name);
        } else {
          xb->name_value_item("name", "[Unknown function]");
        }
      }
    }
  } else {
    xb->name_value_item("name", "(NULL)");
  }
}

void Threads::thread_print_safepoint_xml_on(xmlBuffer *xb) {
  xmlElement xe(xb, "safepoint_stats_list");
JavaThread*jt=JavaThread::current();
  MutexLockerAllowGC ctml(CompileTask_lock,jt); // So we can look at CompileTasks while printing a CompilerThread status
MutexLockerAllowGC tml(Threads_lock,jt);
ALL_JAVA_THREADS(t){
    if (t->get_safepoint_total() != 0) {
      const char* name = t->get_thread_name();
      xmlElement xe(xb, "safepoint_stats_entry");
      xb->name_value_item("thread_name", name ? name : "(NULL)");
      xb->name_value_item("safepoint_total", "%12.3f", os::ticks_to_micros(t->get_safepoint_total()) * 0.001);
      xb->name_value_item("safepoint_count", t->get_safepoint_count());
      xb->name_value_item("safepoint_average", "%12.3f", os::ticks_to_micros(t->get_safepoint_total() / t->get_safepoint_count()) * 0.001);
      xb->name_value_item("safepoint_min", "%12.3f", os::ticks_to_micros(t->get_safepoint_min()) * 0.001);
      xb->name_value_item("safepoint_max", "%12.3f", os::ticks_to_micros(t->get_safepoint_max()) * 0.001);
      print_pc_reference_xml_on("safepoint_max_pc", (address)t->get_safepoint_max_pc(), xb);
      print_pc_reference_xml_on("safepoint_max_rpc", (address)t->get_safepoint_max_rpc(), xb);
    }
  }
}

void Threads::threads_reset_safepoints(){
JavaThread*jt=JavaThread::current();
  MutexLockerAllowGC ctml(CompileTask_lock,jt); // So we can look at CompileTasks while printing a CompilerThread status
MutexLockerAllowGC tml(Threads_lock,jt);
ALL_JAVA_THREADS(t){
t->reset_safepoint_stats();
  }  
}

JavaThread* Threads::by_id_may_gc(int64_t thr_id) {
  MutexLockerAllowGC ml(Threads_lock.owned_by_self() ? NULL : &Threads_lock,1);
ALL_JAVA_THREADS(t){
      if (t->osthread()->thread_id() == static_cast<pid_t>(thr_id)) return t;
  }
  return NULL;
}

const char* Threads::thread_name_may_gc(int64_t thr_id) {
  MutexLockerAllowGC ml(Threads_lock.owned_by_self() ? NULL : &Threads_lock,1);
  JavaThread *t = by_id_may_gc(thr_id);
  return (t != NULL) ? t->get_thread_name() : NULL;
}

void Threads::clear_arta_thread_states(){
assert_lock_strong(Threads_lock);
  for (JavaThread *thread = first(); thread != NULL; thread = thread->next()) {
thread->set_arta_thread_state(NULL);
  }
}

void Threads::ITR_write_thread_names(){
  ResourceMark rm; 
  ALL_JAVA_THREADS(p) {
    typeArrayOop name = java_lang_Thread::name(p->threadObj());
    InstructionTraceManager::addIDToThreadName(p->unique_id(), UNICODE::as_utf8(name->char_at_addr(0), name->length())); 
  }
}


// Lifecycle management for TSM ParkEvents.
// ParkEvents are type-stable (TSM).  
// In our particular implementation they happen to be immortal.
// 
// We manage concurrency on the FreeList with a CAS-based
// detach-modify-reattach idiom that avoids the ABA problems
// that would otherwise be present in a simple CAS-based
// push-pop implementation.   (push-one and pop-all)  
//
// Caveat: Allocate() and Release() may be called from threads
// other than the thread associated with the Event!
// If we need to call Allocate() when running as the thread in 
// question then look for the PD calls to initialize native TLS.  
// Native TLS (Win32/Linux/Solaris) can only be initialized or
// accessed by the associated thread.
// See also pd_initialize().
//
// Note that we could defer associating a ParkEvent with a thread
// until the 1st time the thread calls park().  unpark() calls to
// an unprovisioned thread would be ignored.  The first park() call
// for a thread would allocate and associate a ParkEvent and return
// immediately.  

volatile int ParkEvent::ListLock = 0 ; 
ParkEvent * volatile ParkEvent::FreeList = NULL ; 

ParkEvent*ParkEvent::Allocate(JavaThread*t){
  guarantee (t != NULL, "invariant") ; 
  ParkEvent * ev ; 

  // Start by trying to recycle an existing but unassociated
  // ParkEvent from the global free list.
  for (;;) { 
    ev = FreeList ; 
    if (ev == NULL) break ; 
    // 1: Detach
    // Tantamount to ev = Swap (&FreeList, NULL)
    if (Atomic::cmpxchg_ptr (NULL, &FreeList, ev) != ev) {
       continue ; 
    }

    // We've detached the list.  The list in-hand is now
    // local to this thread.   This thread can operate on the
    // list without risk of interference from other threads.
    // 2: Extract -- pop the 1st element from the list.
    ParkEvent * List = ev->FreeNext ; 
    if (List == NULL) break ; 
    for (;;) { 
        // 3: Try to reattach the residual list
        guarantee (List != NULL, "invariant") ; 
        ParkEvent * Arv =  (ParkEvent *) Atomic::cmpxchg_ptr (List, &FreeList, NULL) ; 
        if (Arv == NULL) break ; 

        // New nodes arrived.  Try to detach the recent arrivals.
        if (Atomic::cmpxchg_ptr (NULL, &FreeList, Arv) != Arv) { 
            continue ; 
        }
        guarantee (Arv != NULL, "invariant") ; 
        // 4: Merge Arv into List
        ParkEvent * Tail = List ; 
        while (Tail->FreeNext != NULL) Tail = Tail->FreeNext ; 
        Tail->FreeNext = Arv ; 
    }
    break ; 
  }

  if (ev != NULL) { 
    guarantee (ev->AssociatedWith == NULL, "invariant") ; 
  } else {
    // Do this the hard way -- materialize a new ParkEvent.
    // In rare cases an allocating thread might detach a long list -- 
    // installing null into FreeList -- and then stall or be obstructed.  
    // A 2nd thread calling Allocate() would see FreeList == null.
    // The list held privately by the 1st thread is unavailable to the 2nd thread.
    // In that case the 2nd thread would have to materialize a new ParkEvent,
    // even though free ParkEvents existed in the system.  In this case we end up 
    // with more ParkEvents in circulation than we need, but the race is 
    // rare and the outcome is benign.  Ideally, the # of extant ParkEvents 
    // is equal to the maximum # of threads that existed at any one time.
    // Because of the race mentioned above, segments of the freelist 
    // can be transiently inaccessible.  At worst we may end up with the 
    // # of ParkEvents in circulation slightly above the ideal.  
    // Note that if we didn't have the TSM/immortal constraint, then
    // when reattaching, above, we could trim the list.  
    ev = new ParkEvent () ; 
    guarantee ((intptr_t(ev) & 0xFF) ==0, "invariant");
  }
  ev->AssociatedWith = t ;          // Associate ev with t
  ev->FreeNext       = NULL ; 
  return ev ; 
}



void ParkEvent::Release (ParkEvent * ev) {
  if (ev == NULL) return ; 
guarantee(ev->FreeNext!=NULL,"invariant");
  ev->AssociatedWith = NULL ; 
  for (;;) { 
    // Push ev onto FreeList
    ParkEvent * List = FreeList ; 
    ev->FreeNext = List ; 
    if (Atomic::cmpxchg_ptr (ev, &FreeList, List) == List) break ; 
  }
}


// 6399321 As a temporary measure we copied & modified the ParkEvent::
// allocate() and release() code for use by Parkers.  The Parker:: forms
// will eventually be removed as we consolide and shift over to ParkEvents
// for both builtin synchronization and JSR166 operations. 

volatile int Parker::ListLock = 0 ;
Parker * volatile Parker::FreeList = NULL ;

Parker * Parker::Allocate (JavaThread * t) {
  guarantee (t != NULL, "invariant") ;
  Parker * p ;

  // Start by trying to recycle an existing but unassociated
  // Parker from the global free list.
  for (;;) {
    p = FreeList ;
    if (p  == NULL) break ;
    // 1: Detach
    // Tantamount to p = Swap (&FreeList, NULL)
    if (Atomic::cmpxchg_ptr (NULL, &FreeList, p) != p) {
       continue ;
    }

    // We've detached the list.  The list in-hand is now
    // local to this thread.   This thread can operate on the
    // list without risk of interference from other threads.
    // 2: Extract -- pop the 1st element from the list.
    Parker * List = p->FreeNext ;
    if (List == NULL) break ;
    for (;;) {
        // 3: Try to reattach the residual list
        guarantee (List != NULL, "invariant") ;
        Parker * Arv =  (Parker *) Atomic::cmpxchg_ptr (List, &FreeList, NULL) ;
        if (Arv == NULL) break ;

        // New nodes arrived.  Try to detach the recent arrivals.
        if (Atomic::cmpxchg_ptr (NULL, &FreeList, Arv) != Arv) {
            continue ;
        }
        guarantee (Arv != NULL, "invariant") ;
        // 4: Merge Arv into List
        Parker * Tail = List ;
        while (Tail->FreeNext != NULL) Tail = Tail->FreeNext ;
        Tail->FreeNext = Arv ;
    }
    break ;
  }

  if (p != NULL) {
    guarantee (p->AssociatedWith == NULL, "invariant") ;
  } else {
    // Do this the hard way -- materialize a new Parker..
    // In rare cases an allocating thread might detach
    // a long list -- installing null into FreeList --and
    // then stall.  Another thread calling Allocate() would see
    // FreeList == null and then invoke the ctor.  In this case we
    // end up with more Parkers in circulation than we need, but
    // the race is rare and the outcome is benign.
    // Ideally, the # of extant Parkers is equal to the
    // maximum # of threads that existed at any one time.
    // Because of the race mentioned above, segments of the
    // freelist can be transiently inaccessible.  At worst
    // we may end up with the # of Parkers in circulation
    // slightly above the ideal.
    p = new Parker() ;
  }
  p->AssociatedWith = t ;          // Associate p with t
  p->FreeNext       = NULL ;
  return p ;
}


void Parker::Release (Parker * p) {
  if (p == NULL) return ;
  guarantee (p->AssociatedWith != NULL, "invariant") ;
  guarantee (p->FreeNext == NULL      , "invariant") ;
  p->AssociatedWith = NULL ;
  for (;;) {
    // Push p onto FreeList
    Parker * List = FreeList ;
    p->FreeNext = List ;
    if (Atomic::cmpxchg_ptr (p, &FreeList, List) == List) break ;
  }
}

//------------------------------------------------------------------------------------------------------------------
// Thread filtering

ThreadFilter ThreadFilter::NIL;

ThreadFilter::ThreadFilter() :
  _group(2),
_name(NULL),
  _status(NULL) {}

ThreadFilter::ThreadFilter(int group, const char *__name, const char *__status) :
  _group(group),
  _name((__name && (strlen(__name) > 0)) ? __name : NULL),
  _status((__status && (strlen(__status) > 0) && (strcmp(__status, "any") != 0)) ? __status : NULL) {}

bool ThreadFilter::accept(bool is_system, const char *name0, const char *status0) {
  return ((group() != 0) || !is_system) &&
         ((group() != 1) || is_system) &&
         (!name() || strstr(name0, name())) &&
         (!status() || (strncasecmp(status0, status(), strlen(status())) == 0));
}

bool ThreadFilter::accept(Thread *thread) {
  return
    accept(thread->is_Java_thread() &&
           java_lang_Thread::threadGroup(((JavaThread*) thread)->threadObj()) == Universe::system_thread_group(),
thread->name(),
           thread->status_msg());
}

//------------------------------------------------------------------------------------------------------------------

void Threads::verify() {  
  ALL_JAVA_THREADS(p) {
    p->verify();
  }
  VMThread* thread = VMThread::vm_thread();
  if (thread != NULL) thread->verify();
}


//------------------------------------------------------------------------------------------------------------------
// Minimum Mutator Utilization
// A class to track MMU - essentially an integration of pause times.
jlong *MMU::INTERVALS;
jlong *MMU::MAXES;
jlong MMU::ALL_TICKS;
jlong MMU::ALL_PAUSE;

MMU::MMU() {
dawn_tick=os::elapsed_counter();
  total_pauses = 0;
  p_beg = NEW_C_HEAP_ARRAY(jlong,MMU_BUF_SIZ);
  p_len = NEW_C_HEAP_ARRAY(jlong,MMU_BUF_SIZ);
  bzero(p_beg,sizeof(jlong)*MMU_BUF_SIZ);
  bzero(p_len,sizeof(jlong)*MMU_BUF_SIZ);
  head = 0;
  tails = NEW_C_HEAP_ARRAY(  int,MAX_IVL);
  pause = NEW_C_HEAP_ARRAY(jlong,MAX_IVL);
  maxes = NEW_C_HEAP_ARRAY(jlong,MAX_IVL);
for(int i=0;i<MAX_IVL;i++){
tails[i]=0;
    pause[i] = maxes[i] = 0;
  }
}

MMU::~MMU() {
  fold_into_global();
  FREE_C_HEAP_ARRAY(jlong,p_beg);
  FREE_C_HEAP_ARRAY(jlong,p_len);
FREE_C_HEAP_ARRAY(int,tails);
  FREE_C_HEAP_ARRAY(jlong,pause);
  FREE_C_HEAP_ARRAY(jlong,maxes);
}

void MMU::start_pause() { 
  if( p_len[head] < 0 ) {       // nested pause?
    p_len[head] = p_len[head]-1;
  } else {
    p_beg[head] = os::elapsed_counter(); 
    p_len[head] = -1; 
  }
}

void MMU::end_pause() {
  if( Thread::current()->is_hidden_from_external_view() ) return;
  if( p_len[head] != -1 ) {     // nested pause?
    p_len[head] = p_len[head]+1;
    return;
  }
  // Collect end time, record pause length in ring buffer
jlong end=os::elapsed_counter();
  jlong pau = end - p_beg[head];
  assert0( pau > 0 );           // Huh?
  total_pauses += pau;
  p_len[head] = pau;
  jlong start = p_beg[head];
  int old_head = head;
  head = (head+1)&(MMU_BUF_SIZ-1);
  // Wrapped ring buffer!  Make it bigger or max interval smaller
  if( head == tails[MAX_IVL-1] ) {
print(NULL);
    tty->print_cr("Oldest tick=%lld now=%lld till_now=%lld",p_beg[head],start,start-p_beg[head]);
tty->print("most recent 10 pauses... ");
for(int i=0;i<10;i++)
tty->print("d-t=%lld len=%lld  ",
                 p_beg[(old_head-10+i)&(MMU_BUF_SIZ-1)]-start,
                 p_len[(old_head-10+i)&(MMU_BUF_SIZ-1)]);
    guarantee( head != tails[MAX_IVL-1], "MMU ring buffer wrapped" ); 
  }

  // Now integrate the new pause into the rolling current pause times.
  bool fold_me = false;
for(int i=0;i<MAX_IVL;i++){
    
    // oldest pause falls off the end of measured area
    int idx = tails[i];
    jlong wend = p_beg[idx]+INTERVALS[i];
    while( wend < start ) {
      pause[i] -= p_len[idx];   // remove oldest pause
      idx = (idx+1)&(MMU_BUF_SIZ-1);
      tails[i] = idx;
      wend = p_beg[idx]+INTERVALS[i];
    }

    // add in newest pause.  first account for fractional part of new
    // pause that fits in timing window.
    jlong frac = wend-start;
    while( frac < pau ) {
      jlong pp = pause[i] + frac; // pause, counting fraction
      assert0( pp >= 0 && pp <= INTERVALS[i] );
      if( pp > maxes[i] ) maxes[i] = pp;
      // Now roll window tail 1 pause forward
      pause[i] -= p_len[idx];   // remove oldest pause
      idx = (idx+1)&(MMU_BUF_SIZ-1);
      tails[i] = idx;
if(idx==head)break;
      wend = p_beg[idx]+INTERVALS[i]; // new window-end
      frac = wend-start;        // new fractional-remaining
    }
    pause[i] += pau;
    assert0( i==0 || pause[i] >= pause[i-1] );
    assert0( pause[i] >= 0 && pause[i] <= INTERVALS[i] );

    if( pause[i] > maxes[i] )
maxes[i]=pause[i];
  }

  if( fold_me ) 
    fold_into_global();
}

void MMU::fold_into_global() {
  if( Thread::current()->is_hidden_from_external_view() ) return;
jlong now=os::elapsed_counter();
  Atomic::add_ptr( now - dawn_tick, (intptr_t*)&ALL_TICKS);
  Atomic::add_ptr( total_pauses,    (intptr_t*)&ALL_PAUSE);
dawn_tick=now;
  total_pauses = 0;
for(int i=0;i<MAX_IVL;i++){
  retry:
    jlong max = maxes[i];
    jlong MAX = MAXES[i];
    // Fold max values both ways: means I raise maxes on each thread
    // which reduces times threads call fold_into_global.
    if( max > MAX && MAX != Atomic::cmpxchg( max, &MAXES[i], MAX ) )  goto retry;
    if( max < MAX && max != Atomic::cmpxchg( MAX, &maxes[i], max ) )  goto retry;
  }
}

void MMU::print( xmlBuffer *xb ) {
  if( xb ) Unimplemented();
for(int i=0;i<MAX_IVL;i++){
tty->print_cr("max pause @ %4ld msec is %3ld msec; MMU= %2lld %%",
                  os::ticks_to_millis(INTERVALS[i]+533281),
                  os::ticks_to_millis(MAXES    [i]+533281),
                  (INTERVALS[i]-MAXES[i])*100/INTERVALS[i]);
  }
tty->print_cr("Average mutator utilization= %2lld%%",
                (ALL_TICKS-ALL_PAUSE)*100/ALL_TICKS);
tty->print_cr("allticks= %lld allpause= %lld ",ALL_TICKS,ALL_PAUSE);
}

//------------------------------------------------------------------------------------------------------------------

thread_key_t Thread::ITRArrayKey = 0;
thread_key_t Thread::ITRCurrentTracePositionKey = 0;

void Thread::initITR(){
  Thread::ITRArrayKey                = os::allocate_thread_local_storage(Thread::cleanupTraceArray);
  Thread::ITRCurrentTracePositionKey = os::allocate_thread_local_storage(NULL);
}

void Thread::cleanupTraceArray(void* array) {
Thread*thread=(Thread*)Thread::current();

  if (UseITR) {
    // Put back remaining array
thread->setCurrentTracePosition(NULL);
    InstructionTraceManager::addFullTrace(thread->getInstructionTraceArray());
  }
}

// One-shot init of globals
void thread_static_init() {
  MMU::INTERVALS = NEW_C_HEAP_ARRAY(jlong, MMU::MAX_IVL);
  MMU::INTERVALS[0] = os::millis_to_ticks(   20);
  MMU::INTERVALS[1] = os::millis_to_ticks(   50); 
  MMU::INTERVALS[2] = os::millis_to_ticks(  100); 
  MMU::INTERVALS[3] = os::millis_to_ticks(  200); 
  MMU::INTERVALS[4] = os::millis_to_ticks(  500); 
  MMU::INTERVALS[5] = os::millis_to_ticks( 1000); 
  MMU::INTERVALS[6] = os::millis_to_ticks( 2000); 

  MMU::MAXES = NEW_C_HEAP_ARRAY(jlong, MMU::MAX_IVL);
for(int i=0;i<MMU::MAX_IVL;i++){
    MMU::MAXES[i] = 0;
  }
}

bool InstructionTraceThread::_should_terminate = 0;
long InstructionTraceThread::_count = 0;

FlatHashSet<const Thread*>* InstructionTraceThreads::doTrace = NULL;
FlatHashSet<const Thread*>* InstructionTraceThreads::doNotTrace = NULL;

InstructionTraceThread::InstructionTraceThread(int id) 
 : Thread(), _id(id), _running(true) {

  InstructionTraceThreads::doNotTrace->add(this);

  Atomic::inc_ptr(&_count);

  // Grab an initial array
  setInstructionTraceArray(InstructionTraceManager::getEmptyTrace());
getInstructionTraceArray()->clear();
  getInstructionTraceArray()->setAssociatedJavaThreadID(unique_id());
  setCurrentTracePosition(getInstructionTraceArray()->getTraces());

  numOps=0;
  numST2=0;
  numST8=0;
  numLD8=0;
  numLVB=0;
  numSVB=0;
  numSpaceIDs0=0;
  numSpaceIDs1=0;
  numSpaceIDs2=0;
  numSpaceIDs3=0;

  EXCEPTION_MARK;

_loggz=NULL;

if(os::create_thread(this,ttype::insttrace_thread)){
    if (!DisableStartThread) {
      os::start_thread(this);
    }
  }
}

InstructionTraceThread::~InstructionTraceThread() {
}

void InstructionTraceThread::run(){
assert(this==Thread::current(),"just checking");

  this->initialize_thread_local_storage();

  static int hasPrinted=0;
  InstructionTraceArray* ata = NULL;
  while(!_should_terminate || ata) {
jlong count=os::elapsed_counter();

    // Check if time to deactivate
    // Deactivation of trace recording doesn't affect how much is recorded.
    // Deactivation only turns off the packaging of instruction data so the
    // rest of the app doesn't have to do as much wasted tracing effort.
    if ( _id == 0 &&
         (count<(jlong)ITRCollectionTimeStart || count>=(jlong)(ITRCollectionTimeStart+ITRCollectionTimeLimit)) &&
         InstructionTraceRecording::isActive() ) {
tty->print_cr("DEACTIVATING!");
      InstructionTraceRecording::deactivate();
      if (ITRCloseOnDeactivation) {
        Atomic::membar();
InstructionTraceThread::shutdown();
      }
    }

    // Get some work
    ata = InstructionTraceManager::getFullTrace();
if(ata==NULL){
MutexLocker mu(ITR_lock);
      if (_should_terminate) continue;
      ata = InstructionTraceManager::getFullTrace();
if(ata==NULL){
        if (_id == 0) {
          ITR_lock.wait_micros(50L*1000, false); // Have root thread check again every 50 milliseconds
          if ( count>=(jlong)ITRCollectionTimeStart && 
               count<(jlong)(ITRCollectionTimeStart+ITRCollectionTimeLimit) &&
               !InstructionTraceRecording::isActive() ) {
            InstructionTraceRecording::activate();
tty->print_cr("ACTIVATING!");
          }
        } else {
          ITR_lock.wait_micros(1000L*1000, false);
        }
        continue;
      }
    }

    // Turn me off to measure performance without writing delays
    if (ITRBasicRecording) {
      if (ITRPrintDebug && !hasPrinted && count>(jlong)ITRCollectionTimeStart && count<(jlong)(ITRCollectionTimeStart+ITRCollectionTimeLimit)) {
        hasPrinted=1;
tty->print_cr("Instruction trace profiling began at count=%lld",count);
      }
if(ITRPrintDebug)tty->print_cr("(%d) Starting to record a trace array...",_id);
      recordTraces(ata->getAssociatedJavaThreadID(), ata->getAssociatedJavaName(), ata->getTraces(), ata->getNumTraces()); 
    }

    if (ITRPrintDebug && (count>(jlong)(ITRCollectionTimeStart+ITRCollectionTimeLimit)) && hasPrinted) {
      hasPrinted=0; 
tty->print_cr("Instruction trace profiling ended before count=%lld",count);
    }

ata->clear();

    // Done with the trace -> put it back
    InstructionTraceManager::addEmptyTrace(ata);

    // PauselessGC and GenPauselessGC expect that only the VMThread and JavaThreads will
    // hit LVB traps.  This sanity check makes sure that's so.
    if ( UseLVBs ) {
      if ( UseGenPauselessGC ) {
assert(get_new_gen_ref_buffer()->is_empty(),"Hit NMT LVB on WatcherThread");
assert(get_old_gen_ref_buffer()->is_empty(),"Hit NMT LVB on WatcherThread");
      }
    }
  }

  // TODO: At this point, the javathreads should not be running.  Steal back the traces they've recorded
  // and write them out

#if 0 // FIXME - need to link against a zip library for gzclose
if(ITRPrintDebug)tty->print_cr("(%d) Before close...",_id);
  // Close file
  if (_loggz) {
    int intrc = gzclose(_loggz); _loggz=NULL;
guarantee(intrc==0,"failed gz close");
  }
#endif // 0

if(ITRPrintDebug)tty->print_cr("(%d) After close",_id);

  InstructionTraceThreads::totalNumOps += numOps;
  InstructionTraceThreads::totalNumST2 += numST2;
  InstructionTraceThreads::totalNumST8 += numST8;
  InstructionTraceThreads::totalNumLD8 += numLD8;
  InstructionTraceThreads::totalNumLVB += numLVB;
  InstructionTraceThreads::totalNumSVB += numSVB;
  InstructionTraceThreads::totalNumSpaceIDs0 += numSpaceIDs0;
  InstructionTraceThreads::totalNumSpaceIDs1 += numSpaceIDs1;
  InstructionTraceThreads::totalNumSpaceIDs2 += numSpaceIDs2;
  InstructionTraceThreads::totalNumSpaceIDs3 += numSpaceIDs3;

  // Signal that it is terminated
{MutexLocker mu(Terminator_lock);
    Atomic::dec_ptr(&_count);
    _running = false;
    Atomic::membar();
Terminator_lock.notify();
if(ITRPrintDebug)tty->print_cr("(%d) After lock and notify...",_id);
  }

  InstructionTraceManager::addIDToThreadName(unique_id(), "<ITR thread>");

  delete this;
}

void InstructionTraceThread::print(){
tty->print("\"Instruction Trace Recorder Thread\" ");
Thread::print();
tty->cr();
}

void InstructionTraceThread::print_xml_on( xmlBuffer *xb, bool ref ) {
  xmlElement te(xb, ref ? "thread_ref" : "thread");
  state_print_xml_on(xb, "Instruction Trace Recorder Thread");
}


#include <fcntl.h>
// Notes:
//  * When ITRPCInsteadOfVA is enabled, VA is replaced with PC
//    except for in annotations.
// Before format:
// STATS:
//  0:40 (41) -- VA
// 41:45 ( 5) -- type
// 46:60 (15) -- immediate or annotationType
// 61:62 ( 2) -- space id
// 63:63 ( 1) -- (FREE) -- guard bit -> better be 0!
// COUNT:
//  0:51 (52) -- count
// 52:63 (12) -- cpuid
// After format:
// STATS:
//  0:40 (41) -- VA
// 41:45 ( 5) -- type
// 46:57 (12) -- cpuid
// 58:59 ( 2) -- space id (relative count if ITRTraceSpaceID is false)
// 60:63 ( 4) -- relative count (if this changes, change numCountBits below)
// COUNT (written if relative count=0,type==ANNOTATION):
//  0:58 (59) -- count
// 59:63 ( 5) -- annotation type
void InstructionTraceThread::recordTraces(int64_t threadID, const char* threadName, long* traces, int numTraces) {
  FlatHashSet<const char*> fhs;
  fhs.contains(threadName);
  unsigned int numCountBits = 4;
  long maxRelativeCount = (1<<numCountBits)-1;
  long lastCount = -maxRelativeCount;

assert(UseITR,"ITR disabled -> should not be here..");

  // Compress
  long* tempTraces = traces;
  long* tempTracesPos = tempTraces;
  jint numTracesProcessed=0;
  for(int i=0; i<numTraces*InstructionTraceArray::_traceSizeInWords; i+=InstructionTraceArray::_traceSizeInWords) {
    long stats = traces[i+0];
    long count = traces[i+1];

    // Skip if this is an empty trace
    if (stats==0 && count==0) {
      continue;
    }

    numTracesProcessed++;

    unsigned int spaceid = (stats>>61)&0x3;
    assert(ITRTraceSpaceID || spaceid==0, "Space ID should be 0 if ITRTraceSpaceID is off!");

    assert(((stats>>63)&0x1)==0, "Guard bit is non 0!!");

    unsigned int type = (stats>>41)&0x1F;
    unsigned int annotationType = 0;
    long imm15 = 0;
    if (!ITRPCInsteadOfVA) {
      imm15 = (((stats>>46)&0x7FFF)<<(64-15))>>(64-15);  // Get imm15 and sign extend
      assert ( imm15>=0 || ((stats>>60)&0x01), "imm15 should be negative" );
      assert ( imm15<0  || !((stats>>60)&0x01), "imm15 should be positive" );
      switch((InstructionTraceManager::Type)type) {
        case InstructionTraceManager::FENCE: break;

        // For annotation, imm15 contains annotation type
        case InstructionTraceManager::ANNOTATION: annotationType=imm15; imm15=0; break;

        case InstructionTraceManager::LADD1: 
        case InstructionTraceManager::LD1: 
        case InstructionTraceManager::LDU1: 
        case InstructionTraceManager::ST1:  imm15<<=0; break;

        case InstructionTraceManager::LADD2: 
        case InstructionTraceManager::LD2: 
        case InstructionTraceManager::LDU2: 
        case InstructionTraceManager::ST2:  imm15<<=1; break;

        case InstructionTraceManager::LADD4: 
        case InstructionTraceManager::LD4: 
        case InstructionTraceManager::LDU4: 
        case InstructionTraceManager::ST4:  imm15<<=2; break;

        case InstructionTraceManager::LD8: 
        case InstructionTraceManager::LDC8: 
        case InstructionTraceManager::ST8: 
        case InstructionTraceManager::STA: 
        case InstructionTraceManager::LADD8: 
        case InstructionTraceManager::CAS8: 
        case InstructionTraceManager::LVB: 
        case InstructionTraceManager::SVB: 
        case InstructionTraceManager::PREF_CLZ: 
        case InstructionTraceManager::PREF_OTHER: imm15<<=3; break;

        case InstructionTraceManager::UNK: 
        case InstructionTraceManager::UNK2: assert(false, "UNK"); break;

default:assert(false,"Undefined type!");
      }
    }

    // Debug
    numOps++;
    if((InstructionTraceManager::Type)type == InstructionTraceManager::ST2) { numST2++; }
    if((InstructionTraceManager::Type)type == InstructionTraceManager::ST8) { numST8++; }
    if((InstructionTraceManager::Type)type == InstructionTraceManager::LD8) { numLD8++; }
    if((InstructionTraceManager::Type)type == InstructionTraceManager::LVB) { numLVB++; }
    if((InstructionTraceManager::Type)type == InstructionTraceManager::SVB) { numSVB++; }
    if ((InstructionTraceManager::Type)type != InstructionTraceManager::FENCE) {
      if(spaceid==0) { numSpaceIDs0++; }
      if(spaceid==1) { numSpaceIDs1++; }
      if(spaceid==2) { numSpaceIDs2++; }
      if(spaceid==3) { numSpaceIDs3++; }
    }

    long address       = ((stats>>0)&0x1FFFFFFFFFFLL) + imm15;

    switch((InstructionTraceManager::Type)type) {
      case InstructionTraceManager::LADD1: 
      case InstructionTraceManager::LD1: 
      case InstructionTraceManager::LDU1: 
      case InstructionTraceManager::ST1:  assert((address%1)==0, "misaligned address"); break;
      case InstructionTraceManager::LADD2: 
      case InstructionTraceManager::LD2: 
      case InstructionTraceManager::LDU2: 
      case InstructionTraceManager::ST2:  assert((address%2)==0, "misaligned address"); break;
      case InstructionTraceManager::LADD4: 
      case InstructionTraceManager::LD4: 
      case InstructionTraceManager::LDU4: 
      case InstructionTraceManager::ST4:  assert((address%4)==0, "misaligned address"); break;
      case InstructionTraceManager::LADD8: 
      case InstructionTraceManager::LD8: 
      case InstructionTraceManager::LDC8: 
      case InstructionTraceManager::ST8: 
      case InstructionTraceManager::STA: 
      case InstructionTraceManager::CAS8: 
      case InstructionTraceManager::SVB: 
      case InstructionTraceManager::LVB: assert(ITRPCInsteadOfVA || (address%8)==0, "misaligned address"); break;
      // pref's do not require alignment
      case InstructionTraceManager::PREF_CLZ: 
      case InstructionTraceManager::PREF_OTHER: 
      case InstructionTraceManager::ANNOTATION:
      case InstructionTraceManager::FENCE: break;
      case InstructionTraceManager::UNK: case InstructionTraceManager::UNK2: assert(false, "UNK"); break;
default:assert(false,"Undefined type!");
    }

    assert((address&0x1FFFFFFFFFFLL) == address, "overflow");
    unsigned int cpuid = (count>>52)&0xFFF;
    // Shouldn't this be 64 bit masked? i.e., no mask?
    unsigned long newcount = (count>>0)&0xFFFFFFFFFFFFFULL;

    long newstats = ((address&0x1FFFFFFFFFFULL)<<0) | (((long)type&0x1F)<<41) | (((long)cpuid&0xFFF)<<46) | (((long)spaceid&0x3)<<58);
    assert(ITRTraceSpaceID || ((newstats>>58)&0x3)==0, "Space ID bits should be 0 if ITRTraceSpaceID is off!");

    if ((long)(newcount-lastCount) > maxRelativeCount || type==(unsigned int)InstructionTraceManager::ANNOTATION) {
      *(tempTracesPos++) = newstats;
      *(tempTracesPos++) = (newcount&0xFFFFFFFFFFFFFULL) | ((long)annotationType<<59);
      if (!ITRPCInsteadOfVA) {
        assert(((newstats>>60)&0xF) == 0, "relative count is non zero!");
        assert((((newstats>>41)&0x1F) != 18) || ((((newstats>>0)&0x1FFFFFFFFFFLL)%8) == 0), "bad ladd8");
        assert (((newstats>>41)&0x1F) == type, "type does not match!");
        if (((newstats>>41)&0x1F) == 18) {
          assert((((newstats&0x1FFFFFFFFFFULL)%8) == 0), "bad ladd8");
        }
      }
    } else {
      *(tempTracesPos++) = newstats | ((newcount-lastCount)<<(64-numCountBits));

      long test = *(tempTracesPos-1);
      if (!ITRPCInsteadOfVA && ((test>>41)&0x1F) == 18) {
        assert((((test&0x1FFFFFFFFFFULL)%8) == 0), "bad ladd8");
      }
    }

    lastCount = newcount;
  }

#if 0 // FIXME - need to link against a zip library for gzdopen
  if (ITRRecordToFile) {
    long intrc;
    // Opening the gz file is lazy because closing a gzFile without writing 
    // to it causes a crash
    if (!_loggz) {
      char filename[1024];
      sprintf(filename, "InstTraces.ITRid%d.raw.gz", _id);
      if( strcmp(ITROutputPath,"")!=0 ) {
        guarantee(strlen(ITROutputPath)+strlen(filename)+1 < 1024, "ITROutputPath is too large!");
        sprintf(filename, "%s/InstTraces.ITRid%d.raw.gz", ITROutputPath, _id);
      }
      int outfd = open(filename, O_CREAT|O_TRUNC|O_RDWR, 0666);
      if (outfd < 0) {
tty->print_cr("[ITR] Error opening file %s for write!",filename);
fatal("[ITR] fatal death");
      }
      char mode[10];
      sprintf (mode, "wb%d", 6);
      _loggz = gzdopen (outfd, mode);
guarantee(_loggz!=NULL,"Couldnt open gz file");

      // Write raw file name header
      // Magic
      int magic = 0xf005ba11;
      intrc = gzwrite (_loggz, &magic, sizeof(int));
      guarantee(intrc==sizeof(int), "Bad # bytes written to mem-op file (out of disk space?) (A)");
      // Mode -- bits to signify what raw file contains
      int fileMode = 0;
      if (ITRPCInsteadOfVA) fileMode |= 0x00000001;
      if (ITRTraceSpaceID)  fileMode |= 0x00000002;
      intrc = gzwrite (_loggz, &fileMode, sizeof(int));
      guarantee(intrc==sizeof(int), "Bad # bytes written to mem-op file (out of disk space?) (B)");
    }

    // Write thread ID
    intrc = gzwrite (_loggz, &threadID, sizeof(long));
    guarantee(intrc==sizeof(long), "Bad # bytes written to mem-op file (out of disk space?) (1)");

    // Write thread name length and name
    long length = strlen(threadName)+1; // +1 for null terminator
    intrc = gzwrite (_loggz, &length, sizeof(int));
    guarantee(intrc==sizeof(int), "Bad # bytes written to mem-op file (out of disk space?) (2)");
    intrc = gzwrite (_loggz, threadName, length*sizeof(char));
    guarantee(intrc==length*(long)sizeof(char), "Bad # bytes written to mem-op file (out of disk space?) (3)");

    // Write number of words of memop data
    long numWords=tempTracesPos-tempTraces;
    intrc = gzwrite (_loggz, &numWords, sizeof(long));
    guarantee(intrc==sizeof(long), "Bad # bytes written to mem-op file (out of disk space?) (4)");

    // Write mem-op data
    intrc = gzwrite (_loggz, tempTraces, sizeof(long)*numWords);
    guarantee(intrc>=0 && intrc==(long)sizeof(long)*numWords, "Bad # bytes written to mem-op file (out of disk space?) (5)");
  }
#endif // 0
}

jlong InstructionTraceThreads::_numThreads = 0;
int InstructionTraceThreads::_uniqueID = 0;
InstructionTraceThread* InstructionTraceThreads::_instructionTraceThreadHead = NULL;

void InstructionTraceThreads::noticeThread(Thread*thread){
  if (doTrace) {
    // Check if this thread should be in the doTrace list
    char* name = NULL;
    // Grab name under lock
    { MutexLockerNested ml(Threads_lock);
      ResourceMark rm;
      char* tempName = thread->name();
      if (tempName) {
        name = new char[strlen(tempName)];
        if (name == NULL) fatal("Could not allocate temp memory");
strcpy(name,tempName);
      }
    }

    if (name && Arguments::TraceThread(name)) {
      doTrace->add(thread);
    }
    delete name;
  }
}

int InstructionTraceThreads::getUniqueID() {
  int id;
  while(1) {
id=_uniqueID;
    if (Atomic::cmpxchg(id+1, (int*)&_uniqueID, id) == id) {
      break;
    }
  } 
  return id;
}

InstructionTraceArray* Thread::stealInstructionTraceArray() {
  Unimplemented();
  return NULL;
//  _ITR_currentTracePosition = NULL;
//
//  InstructionTraceArray *ata = _instructionTraceArray;
//  if (Atomic::cmpxchg_ptr((intptr_t)NULL,
//                          (intptr_t*)&_instructionTraceArray,
//                          (intptr_t)ata) == (intptr_t)ata) {
//    return ata;
//  } else {
//    return NULL;
//  }
}

int InstructionTraceThreads::totalNumOps = 0;
int InstructionTraceThreads::totalNumST2 = 0;
int InstructionTraceThreads::totalNumST8 = 0;
int InstructionTraceThreads::totalNumLD8 = 0;
int InstructionTraceThreads::totalNumLVB = 0;
int InstructionTraceThreads::totalNumSVB = 0;
int InstructionTraceThreads::totalNumSpaceIDs0 = 0;
int InstructionTraceThreads::totalNumSpaceIDs1 = 0;
int InstructionTraceThreads::totalNumSpaceIDs2 = 0;
int InstructionTraceThreads::totalNumSpaceIDs3 = 0;

void InstructionTraceThreads::pushInstructionTraceThread(InstructionTraceThread *srt) {
  while(1) {
    srt->setNext(_instructionTraceThreadHead);
    if (Atomic::cmpxchg_ptr((intptr_t)srt, 
                            (intptr_t*)&_instructionTraceThreadHead, 
                            (intptr_t)srt->next()) == (intptr_t)srt->next()) {
      break;
    }
  }
}

InstructionTraceThread* InstructionTraceThreads::popInstructionTraceThread() {
  InstructionTraceThread* head0;
  while(1) {
    head0 = _instructionTraceThreadHead;  
if(head0==NULL)return NULL;
    if (Atomic::cmpxchg_ptr((intptr_t)head0->next(), 
                            (intptr_t*)&_instructionTraceThreadHead, 
                            (intptr_t)head0) == (intptr_t)head0) {
      break;
    }
  } 
  head0->setNext(NULL);
  return head0;
}

void InstructionTraceThreads::initialize(){
if(Arguments::ITRTraceThreadsEnabled()){
    doTrace = new FlatHashSet<const Thread*>();
  }
  doNotTrace = new FlatHashSet<const Thread*>();
}

void InstructionTraceThreads::stopAllThreads() {
tty->print_cr("VM Exited.  Writing profiling data (this may (will) take a while)...");
  // Okay to traverse thread list (serial access at this point)

  // Kill all threads
InstructionTraceThread::shutdown();
  Atomic::membar();

  while(InstructionTraceThread::getCount() > 0) {




MutexLocker mu1(Terminator_lock);
    if (InstructionTraceThread::getCount() > 0) {
Terminator_lock.wait();
    }
  }

  // Make sure stats are visible
  Atomic::membar();

  if (ITRPrintDebug) {
tty->print_cr("Number of ops: %d",totalNumOps);
tty->print_cr("Number of ST2's: %d",totalNumST2);
tty->print_cr("Number of ST8's: %d",totalNumST8);
tty->print_cr("Number of LD8's: %d",totalNumLD8);
tty->print_cr("Number of LVB's: %d",totalNumLVB);
tty->print_cr("Number of SVB's: %d",totalNumSVB);
    double sum = (double)(totalNumSpaceIDs0+totalNumSpaceIDs1+totalNumSpaceIDs2+totalNumSpaceIDs3);
    tty->print_cr("Number of SpaceID==0's: %d %f", totalNumSpaceIDs0, (double)totalNumSpaceIDs0/sum);
    tty->print_cr("Number of SpaceID==1's: %d %f", totalNumSpaceIDs1, (double)totalNumSpaceIDs1/sum);
    tty->print_cr("Number of SpaceID==2's: %d %f", totalNumSpaceIDs2, (double)totalNumSpaceIDs2/sum);
    tty->print_cr("Number of SpaceID==3's: %d %f", totalNumSpaceIDs3, (double)totalNumSpaceIDs3/sum);
  }
  
  delete doTrace;
  delete doNotTrace;
}

