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
#ifndef COMPILEBROKER_HPP
#define COMPILEBROKER_HPP


// Compile Broker
//
// Controls compile jobs.  This is a classic multiple producer/consumer
// problem.  Standard JavaThreads produce compile jobs as the run, and place
// them on a priority queue.  One or more Compiler threads pull compile jobs
// from queue.  Once the job has completed, zero or more producers
// (JavaThreads) are woken up to begin executing the new code.
//
// Some other constraints:
//
// During startup and large phase shifts, the compile queue can get quite
// backed up; this means the JavaThreads end up running longer in the
// interpreter.  E.g., during the SpecJAppServer2002 warmup period, roughly
// 4000 C2 compiles need to happen in a few minutes.  With only two C2Compiler
// threads the queue routinely has a backlog of 30+ jobs.  This implementation
// allows C2Compiler threads to be added as the queue backs up.  Since these
// threads are fairly heavyweight (4Meg stacks, plus local working area can be
// 10+ Meg for really big allocations) they need to scale back when idle.
// This is implies a fairly classic worker pool style arrangement.
//
// C2 jobs routinely take more than a few milliseconds, so standard mixed mode
// (non -Xbatch) allows JavaThreads to only stall for 20millis before giving
// up and running on in the interpreter.  A better technique might be to
// guesstimate time-to-code-compiled and then choose to stall or run right
// away.  In any case, when a compile job completes some JavaThreads might
// need to be woken up.  However I don't expect there to be a lot of pending
// JavaThreads (since they don't wait very long).
// 
// Because I expect the number of waiting JavaThreads to max out at around 10,
// and the number of C2Compile threads to also max out around 10, and the time
// between needing to hold and global CompileTask_lock to be short (<<1milli),
// I'm going with a single global lock.
//
// More than 1 JavaThread can request the same method be compiled (indeed it's
// common during phase-shifts, when 20 or so worker threads all start beating
// on the same work queue).  Hence JavaThread producers will do double-checked
// locking when checking for code.  If the JavaThread wants some code to
// compile and it's missing, it will
// (a) take the CompileTask_lock, fence, then 
// (b)   check for installed code or currently in-progress, or job in queue
// (c)   If it finds code, then 
// (d)     simply unlock & go execute the code.  
// (e)   If it does not find a job (in queue or being compiled) then
// (f)     it needs to create a job and insert it into the queue.  
// (g)   Decide if another Compile thread is needed (queue depth, time to complete,etc)
// (h)     If so, spawn a Compile thread.
// (i)   Now that it has a job it decides to wait or not.  
// (j)   If not waiting, simply unlock and carry on in the interpreter.  
// (k)   If waiting, then bump the job's reference count and 
// (l) notifyAll()  // wake up sleeping compiler threads?
// (m) wait(CompileTask_lock, timeout).
// (n)   When awaking, check for job done.  If not, wait again.
// (o)   Lower the reference count (and at zero and job is done, free() the job),
// (p)   Fence and unlock.  
// (q) Carry on, (either with compiled code or not, depending on the job).
//
// Queue insertion is a priority-based thing.  Small jobs complete quicker and
// allow JavaThreads to execute faster, hence they should be given priority
// over larger jobs.  OSR jobs represent a highest priority job and should be
// given to the first CompileThread that comes along (and probably indicate
// the need for another CompileThread to be spawned right away).
//
// Wait timeouts also need some intelligence: threads requesting OSRs can
// carry on immediately in the interpreter, they will constant check for code
// being ready.  Other threads can guessitmate time-to-completion and compare
// that against the standard timeout (20msec) and choose to wait or not.  With
// -Xbatch all threads wait.
//
// Compile threads are the Consumer threads, taking compile jobs off the queue
// and creating code for the JavaThreads.
// (a) Do any startup init, then
// (b) take the CompileTask_lock and fence.
// -----
// (c)   If queue is empty then
// (d) wait(CompileTask_lock,timeout).  On awaking, fence & check:
// (e)   If queue empty 
// (f)     If timeout and not last CompileThread then
// (g)       fence, unlock and self-destruct.
// (g)     Else adjust timeout and go to (d)
// (h)   Else
// (i)   Pull job from queue
// (j)   Mark as in-progress
// (k)   fence, unlock.
// (l) Compile the job (while not locked).  If compile succeeds, install code.
// (n) Take lock, fence.
// (m)   Mark as done (must inside locked region, as a producer can now delete).
// (o)   If ref-count is zero, free() job.
// (p)   Else notifyall(CompileTask_lock) to wake up sleeping JavaThreads
// (q)   Loop to (c)
//

// One CompileTask for each compilation request in progress.  The job is
// either waiting to be compiled or being compiled.  Once it has finished, it
// is removed from all queues and deleted by the last refering thread.  Tasks
// are deleted when they are in state 'done', as set by a CompileThread, and
// when their ref count falls to 0.  The ref-count is the count of JavaThreads
// waiting on this task.

#include "abstractCompiler.hpp"
#include "allocation.hpp"
#include "handles.hpp"
#include "mutexLocker.hpp"
#include "thread.hpp"

class CompileTask:public CHeapObj{
public:
  CompileTask *_next;
  enum state { 
    waiting_osr,       // OSR job waiting in queue
    waiting,           // Normal job waiting in queue
    in_progress,       // Some CompilerThread is working on it
    done               // Compilation completed; delete when ref-count goes to zero
  };
  short _state;        // 
  short _ref_count;    // Num producers with a ptr to this task
  short _score;        // Priority-score, smaller is better.
  unsigned short _compile_id;   // Short identifier
  unsigned short _osr_bci;       
  static int _global_ids;
jobject _method;
jobject _nmethodOop;

  int compile_id() const { return _compile_id; }
  bool is_waiting() const { return _state == waiting_osr || _state == waiting; }
  bool is_in_progress() const { return _state == in_progress; }
  bool is_osr() const { return _osr_bci != (unsigned short)InvocationEntryBci; }
  int osr_bci() const { return is_osr() ? _osr_bci : InvocationEntryBci; }
  
  // Called by producer threads
  CompileTask( int score, int osr_bci, int compile_id, methodHandle method );
  ~CompileTask();
  void producer_lower_ref_and_delete_if_unused();
  int producer_compute_score( methodHandle to_be_compiled, methodHandle hot );

  // Called by consumer threads.  Also gather time-to-compile stats
  bool consumer_set_state_done_and_delete_if_unused(CompileTask**pt);

  static int compare_scores(CompileTask **a, CompileTask **b) { return (*a)->_score - (*b)->_score; }

  const char* state_name() const;
  const char* method_name() const;

  void to_xml(azprof::Request *req, azprof::Response *res);

#ifndef PRODUCT
  long _tick_created;  // Used to decide how long this task has been waiting
  long _tick_compile_began;  // How long to compile task
  void print     ( int cb_id ); // One-liner of state
#endif
  void print_line( int cb_id, const char *msg ); // One-liner of state
};


class CompileBroker:public CHeapObj{
  friend class ClassLoader;
public:
  // CompileBroker, one for each kind of Compiler (C1 & C2).
  static CompileBroker _c1;
  static CompileBroker _c2;
int _id;//1 or 2
  int num_cx_threads() const { return _num_cx_threads; }

  // Priority queue of jobs needing doing.  Jobs are sorted by state, then by task score.
  // State forces OSR jobs first then normal jobs, then in-progress jobs last.
CompileTask*_queue;
  int total_tasks() const;
  int waiting_tasks() const;    // Public for CompileTheWorld control
  bool get_single_thread_compiler_mode() const { return _get_to_single_compiler_thread_mode; }

private:
  CompileBroker( int id, AbstractCompiler *abs ) : _id(id), _abstract_compiler(abs), _get_to_single_compiler_thread_mode(false) { }
  // For discovery of compiler features
AbstractCompiler*_abstract_compiler;

  static jlong _c1_total_compilation_ticks;
  static jlong _c2_total_compilation_ticks;

  static bool _has_printed_code_cache_full;

  // Number of compiler threads
  int _num_cx_threads; // Likely 1, but could spike up to ~10 or so
  int _num_idle_cx_threads;

  // Scan the queue for a CompileTask for this method.  Return the matching 
  // CompileTask if it exists
  CompileTask *scan_for_task( methodHandle to_be_compiled, int osr_bci );
  // Choose next task to compile, or NULL if none waiting
CompileTask*choose_next_task();
  
  bool _get_to_single_compiler_thread_mode; // under low memory conditions
  bool should_get_to_single_compiler_thread_mode();

  // These calls are only made by producer threads, i.e. regular JavaThreads,
  // and never by consumer threads, i.e. CompilerThreads.
private:
  void producer_create_compiler_thread( JavaThread *THREAD );
  bool producer_create_compiler_thread_impl( JavaThread *THREAD, char *name, int tid );
  void producer_add_task_impl( methodHandle to_be_compiled, methodHandle hot, int osr_bci );
public:
  void producer_add_task     ( methodHandle to_be_compiled, methodHandle hot, int osr_bci ) {
    assert0( !JavaThread::current()->is_Compiler_thread() );
    producer_add_task_impl(to_be_compiled,hot, osr_bci);
  }

  // These calls are only made by consumer threads, i.e. CompilerThreads, and 
  // never by producer threads, i.e., regular JavaThreads.
private:
  void consumer_main_loop_impl( );
public:
  void consumer_main_loop     ( ) {
assert0(Thread::current()->is_Compiler_thread());
    // Take the CompileTask_Lock here, where's its obviously unlocked on all exit paths.
MutexLockerAllowGC ml(CompileTask_lock,JavaThread::current());
    stat_gather();                  // Gather current CompileBroker stats
    consumer_main_loop_impl( );
    // This CompilerThread will self-destruct on exit.
    _num_cx_threads--;
    _num_idle_cx_threads--;
    stat_gather();                  // Gather current CompileBroker stats
  }

  // Set _should_block.
  // Call this from the VM, with Threads_lock held and a safepoint requested.
  static void set_should_block();

  // Call this from the compiler at convenient points, to poll for _should_block.
  static void maybe_block();

  // Return total compilation ticks
  static jlong total_compilation_ticks() {
    return _c1_total_compilation_ticks + _c2_total_compilation_ticks;
  }

  // Print compile task queues as XML.
  static void all_to_xml(azprof::Request *req, azprof::Response *res);
  void to_xml(azprof::Request *req, azprof::Response *res, int limit);

  // Debugging aids
  // Gather stats while holding the CompileTask_lock at interesting points.  
  // Also print if asked.
void stat_gather()PRODUCT_RETURN;
  // Big bulky print at VM exit
  void print_statistics() PRODUCT_RETURN;
#ifndef PRODUCT
  // Stat gathering fields
  int _queue_probes;      // Number of times queue data is gathered
  int _queue_depth_total; // Cumlative sums of queue depth
  int _num_threads_total; // Cumlative sums of active compiler threads

  int _jobs_total;        // Total number of compiles attempted
  int _time_in_queue;     // How long jobs waited before starting compilation
  int _time_to_compile;   // How long to compile this job
  int _producer_time_waiting;// How long total producers waited

  int _num_producers;        // Count of waiting producer threads
  int _producer_spurious_wakeups; 
  int _consumer_spurious_wakeups;
  int _num_consumers_total;  // Count of compiler threads ever created

  // Print current queue depths, job status, compiler thread status
  void print();
#endif
  void print_times();           // For CITime

  static void print_compiler_threads_on(outputStream* st);
};

#endif // COMPILEBROKER_HPP
