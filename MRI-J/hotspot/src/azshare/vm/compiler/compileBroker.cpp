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


#include "c1_Compiler.hpp"
#include "c2compiler.hpp"
#include "ciEnv.hpp"
#include "classLoader.hpp"
#include "codeCache.hpp"
#include "compileBroker.hpp"
#include "compilerOracle.hpp"
#include "init.hpp"
#include "interfaceSupport.hpp"
#include "javaClasses.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "oopFactory.hpp"
#include "safepoint.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"

CompileBroker CompileBroker::_c1(1,new Compiler());
CompileBroker CompileBroker::_c2(2,new C2Compiler());
int CompileTask::_global_ids;
jlong CompileBroker::_c1_total_compilation_ticks = 0;
jlong CompileBroker::_c2_total_compilation_ticks = 0;

CompileTask::CompileTask( int score, int osr_bci, int compile_id, methodHandle method ) :
  _next(NULL), 
  _state(osr_bci == InvocationEntryBci ? CompileTask::waiting : CompileTask::waiting_osr), 
  _ref_count(0), 
  _score(score), 
  _compile_id(compile_id), 
_osr_bci(osr_bci),
#ifndef PRODUCT
  _tick_created(os::elapsed_counter()),
#endif
  _method(JNIHandles::make_global(method)) 
{
assert0(JavaThread::current());
  assert0( score > 0 );
}

CompileTask::~CompileTask() {
  assert0( _ref_count == 0 && _state == done );
  JNIHandles::destroy_global(_method);  
  _next = (CompileTask*)0xdeadbeaf;
}

// Called by consumer threads.  Also gather time-to-compile stats
// Return TRUE if deleted the task (hence NO waiting JavaThreads).
bool CompileTask::consumer_set_state_done_and_delete_if_unused(CompileTask **pt) {
assert_lock_strong(CompileTask_lock);
_state=done;

  while( *pt != this )          // Unlink from queue
    pt = &((*pt)->_next);
  *pt = (*pt)->_next;

  if( _ref_count ) return false;
  delete this;
  return true;
}

const char* CompileTask::state_name() const {
  switch (_state) {
  case waiting_osr: return "waiting_osr";
  case waiting:     return "waiting";
  case in_progress: return "in_progress";
  case done:        return "done";
  default:          ShouldNotReachHere(); return NULL;
  }
}

const char* CompileTask::method_name() const {
methodOop oop=(methodOop)JNIHandles::resolve(_method);
  Klass *klass = Klass::cast(oop->constants()->pool_holder());
  const char *klass_name = klass->external_name();
const char*method_name=oop->name()->as_C_string();
  size_t name_size = strlen(klass_name) + strlen(method_name) + 2;
  char *name = NEW_RESOURCE_ARRAY(char, name_size);
  snprintf(name, name_size, "%s.%s", klass_name, method_name);
  return name;
}

void CompileTask::to_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "compile-task");
  azprof::Xml::leaf(res, "method", method_name());
  azprof::Xml::leaf(res, "state", state_name());
  azprof::Xml::leaf(res, "score", _score);
#endif // AZ_PROFILER
}

#ifndef PRODUCT
void CompileTask::print( int cb_id ) {
  ResourceMark rm; 
  double count = (double) (os::elapsed_counter() - _tick_created);
  double freq  = (double) os::elapsed_frequency();
  double secs = count/freq;
methodOop moop=(methodOop)JNIHandles::resolve(_method);
  tty->print_cr(!is_osr()
?"%4d %c%c%c%d %s (%d bytes) score=%d, waiting=%4.1fms %s"
                : "%4d %c%c%c%d %s (%d bytes) score=%d, waiting=%4.1fms %s @%d",
                _compile_id, 
                is_osr() ? '%' : ' ',
                moop->is_synchronized() ? 's' : ' ',
                moop->has_exception_handler() ? '!' : ' ',
                cb_id,
                method_name(), moop->code_size(),
                _score, secs*1000.0,
                state_name(),
                osr_bci());
}
#endif

// ------------------------------------------------------------------
// CompileTask::print_line
void CompileTask::print_line( int cb_id, const char *msg ) {
  ResourceMark rm();
  methodHandle method((methodOop)JNIHandles::resolve(_method));
  BufferedLoggerMark lm(NOTAG, Log::M_COMPILE | Log::L_LO, PrintCompilation);

  lm.out("%4d", _compile_id);   // print compilation number
  lm.out(" %c%c%c%d",           // print method attributes
         is_osr()                        ? '%' : ' ',
         method->is_synchronized()       ? 's' : ' ',
         method->has_exception_handler() ? '!' : ' ',
         cb_id);
  method->print_short_name(lm.stream()); // print method name
  // print osr_bci if any
  if( is_osr() ) lm.out(" @ %d", osr_bci());
  lm.out(" (%d score)", _score); // print method size
  lm.out(msg);
}

// ------------------------------------------------------------------
// CompileBroker::set_should_block
//
// Set _should_block.
// Call this from the VM, with Threads_lock held and a safepoint requested.
void CompileBroker::set_should_block() {
assert_lock_strong(Threads_lock);
  assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint already");
#ifndef PRODUCT
if(PrintCompilation&&Verbose)
tty->print_cr("notifying compiler thread pool to block");
#endif
}

// ------------------------------------------------------------------
// CompileBroker::maybe_block
//
// Call this from the compiler at convenient points, to poll for _should_block.
void CompileBroker::maybe_block() {
  // The transition to "in VM" from native attempts to take the Compiler
  // thread's JVM lock.  If it's not free (i.e. a Safepoint is in progress)
  // the Compiler thread will block.  Otherwise it takes the JVM lock and then
  // immediately releases it.
  ThreadInVMfromNative tivfn(JavaThread::current());
}

// Count of waiting compile jobs
int CompileBroker::waiting_tasks() const {
assert_lock_strong(CompileTask_lock);
  int depth = 0;
  for( CompileTask *task = _queue; task; task = task->_next )
    if( task->is_waiting() )
      depth++;
  return depth;
}

// Depth of the queue, waiting and compiling tasks
int CompileBroker::total_tasks() const {
assert_lock_strong(CompileTask_lock);
  int depth = 0;
  for( CompileTask *task = _queue; task; task = task->_next )
      depth++;
  return depth;
}

bool CompileBroker::should_get_to_single_compiler_thread_mode(){
  // Check whether there is enough committed memory left.  Otherwise check
  // that we have at least one compiler thread and let the other compiler
  // threads exit.  Compiler threads may be killed out of order.  So we need
  // to make sure that we do not create new compiler threads, once we have
  // started to kill, until we reach the single thread mode.
  if( CompileTheWorld ) return false;
  if( os::process_low_on_memory(100*M) ) { // Still low on memory?
    if( CIPrintCompileQueue && !_get_to_single_compiler_thread_mode ) 
tty->print_cr("Low on memory: going to a single compiler thread");
    //guarantee(0,"perf runs: do not silently go to single-compiler mode");
    return (_get_to_single_compiler_thread_mode = true);
  }
  if( CIPrintCompileQueue && _get_to_single_compiler_thread_mode ) 
tty->print_cr("Clearing single-compiler-thread mode");
  return (_get_to_single_compiler_thread_mode = false);
}

bool CompileBroker::_has_printed_code_cache_full=false;

void CompileBroker::consumer_main_loop_impl(){
CompilerThread*cthread=CompilerThread::current();

  if (!ciObjectFactory::is_initialized()) {
    ASSERT_IN_VM;
MutexLockerAllowGC only_one(CompileThread_lock,cthread);
    if (!ciObjectFactory::is_initialized()) {
      ciObjectFactory::initialize();
    }
  }

  // Main CompileThread loop
  long wait_start_time = os::elapsed_counter();
  while( 1 ) {
    if (cthread->_tid != 0 && should_get_to_single_compiler_thread_mode()) {
      if (CIPrintCompileQueue) {
tty->print_cr("|  Killing C%dCompilerThread%d; have %d threads (%d idle). Running low on memory.",
                      _id, cthread->_tid, _num_cx_threads, _num_idle_cx_threads);
os::print_memory_info(tty);
      }
      break;
    }
    // (c)   If queue is empty then
    CompileTask *task = choose_next_task();
    if( !task ) {
      // (d) wait(CompileLock,timeout).  On awaking, fence & check:
      // Compute time to wait - a total of 2 sec (2000000 usec) since last compilation.
      long waited_already_ticks = os::elapsed_counter() - wait_start_time;
      bool timeout = ((waited_already_ticks>>31) == 0 )
        ? CompileTask_lock.wait_micros(2000000L - os::ticks_to_micros(waited_already_ticks), false)
        : true;                 // More than 2^31 ticks?
      if (cthread->_tid != 0 && should_get_to_single_compiler_thread_mode()) {
        if (CIPrintCompileQueue) {
tty->print_cr("|  Killing C%dCompilerThread%d; have %d threads (%d idle). Running low on memory.",
                        _id, cthread->_tid, _num_cx_threads, _num_idle_cx_threads);
os::print_memory_info(tty);
        }
        break;
      }
      // (e)   If queue STILL empty 
      task = choose_next_task();
      if( !task ) {
        // (f)     If timeout and not last CompileThread then
        if( timeout ) {
          if( _num_cx_threads > 2 && cthread->_tid == _num_cx_threads-1 ) {
            if (CIPrintCompileQueue) {
              waited_already_ticks = os::elapsed_counter() - wait_start_time;
tty->print_cr("|  Killing C%dCompilerThread%d; have %d threads (%d idle) and this one has waited %ldms",
                            _id, cthread->_tid, _num_cx_threads, _num_idle_cx_threads, os::ticks_to_millis(waited_already_ticks));
            }
            // (g)       fence, unlock and self-destruct.
            break;
          }
          // (g)     Else adjust timeout and go to (d)
wait_start_time=os::elapsed_counter();
        }
        continue;             // Timeout will recompute remaining time
      }
    }
    // (h)   Else
ResourceMark rm(cthread);

    // (i)   Pull job from queue
    assert0( task && task->is_waiting() );
    _num_idle_cx_threads--;     // No longer idle

    // (j)   Mark as in-progress
    task->_state = CompileTask::in_progress;
    cthread->_task = task;
    
    assert0( !CodeCache::is_almost_full() || CompileTheWorld );

    // Check for CodeCache full
    if (CodeCache::is_full()) {
      // The CodeCache is full.  Print out warning and disable compilation.
      UseInterpreter = true;    // Allow execution in -Xcomp mode
#ifndef PRODUCT
      if (!_has_printed_code_cache_full) {
        _has_printed_code_cache_full = true;
warning("[compileBroker.cpp] CodeCache is full.  Compilers have been disabled.");
      }
      if (CompileTheWorld || ExitOnFullCodeCache) {
before_exit(cthread);
        exit_globals(); // will delete tty
        vm_direct_exit(CompileTheWorld ? 0 : 1);
      }
#endif
    } else { // (k)   fence, unlock.

      // I must read the SystemDictionary while holding the Compile_lock.
      // There is a race between installing compiled code and loading a class
      // which invalidates the code.  Holding the Compile_lock here means I
      // start the compile with a known state of the SystemDictionary.  When
      // the compile finishes and I prepare to install the code, I again hold
      // the Compile_lock and thus can compare the SystemDictionary states to
      // see if my compile has been ruined.
      int system_dictionary_modification_counter;
      { MutexLockerAllowGC mx(Compile_lock, cthread);
      system_dictionary_modification_counter = SystemDictionary::number_of_modifications();
      }

#ifndef PRODUCT
task->_tick_compile_began=os::elapsed_counter();
#endif

      // Need this HandleMark so we flush the method at the earliest possible time.
HandleMark hm(cthread);

      methodHandle method(cthread, (methodOop)JNIHandles::resolve(task->_method));

      // Allocate a new block for JNI handles.
      // Inlined code from jni_PushLocalFrame()
JNIHandleBlock*java_handles=cthread->active_handles();
JNIHandleBlock*compile_handles=JNIHandleBlock::allocate_block(cthread);
      assert(compile_handles != NULL && java_handles != NULL, "should not be NULL");
      compile_handles->set_pop_frame_link(java_handles);  // make sure java handles get gc'd.
cthread->set_active_handles(compile_handles);

      bool is_osr = task->is_osr();

      if (PrintCompilation) task->print_line(_id,"");

      // (l) Compile the job (while not locked).  If compile succeeds, install code.
      MutexUnlocker_GC_on_Relock mux(CompileTask_lock); // Must release before entering native code

      bool break_at =           // Should we halt in the debugger?
        CompilerOracle::should_break_at(method) ||
        (task->_compile_id == CIBreakAt);
      const char* failure_reason = NULL; // Not bailed out
      elapsedTimer time;

      bool retrying_compile = false;
      while (true) {
        TraceTime t1("compilation", &time);
        // Compiles normally run in "native" mode - GC allowed during compilation.
ThreadToNativeFromVM ttn(cthread);
        // GC ALLOWED FROM HERE ON....
HandleMark handle_mark(cthread);

#ifndef PRODUCT
        if (UseFreezeAndMelt) {
          // We want to melt a frozen compile, so hijack this one!
          ClassLoader::freeze_and_melt(system_dictionary_modification_counter);
        }
#endif

        ciEnv ci_env(task, system_dictionary_modification_counter, NULL);
        ci_env.set_break_at_compile(break_at);

        ciMethod* target = ci_env.get_method_from_handle(task->_method);
        if (CIPrintMethodCodes) target->print_codes(tty);

        _abstract_compiler->compile_method(&ci_env, target, task->osr_bci(), retrying_compile);

        if( ci_env.failing() ) {
          failure_reason     = ci_env.failure_reason();
if(ci_env.failure_retry_compile_immediately()){
            if (retrying_compile) {
              target->set_not_compilable(_abstract_compiler->is_c2_compiler() ? 2 : 1);
              break;
            }
            retrying_compile = true;
failure_reason=NULL;
            continue;
          }
          if (!ci_env.failure_retry_compile()) {
            target->set_not_compilable(_abstract_compiler->is_c2_compiler() ? 2 : 1);
          }         
        }
        
        break;

      } // Back into VM mode, no GC allowed

      // Release our JNI handle block
cthread->set_active_handles(java_handles);
      compile_handles->set_pop_frame_link(NULL);
JNIHandleBlock::release_block(compile_handles,cthread);//may block

      if (failure_reason != NULL) // Compiler failed?
        Log::log3(NOTAG, Log::M_COMPILE, PrintCompilation, "%3d   COMPILE SKIPPED: %s", task->compile_id(), failure_reason);

      if (_abstract_compiler->is_c2_compiler()) {      
        Atomic::add_ptr((intptr_t) time.ticks(), (intptr_t *) &_c2_total_compilation_ticks);
      } else {
        Atomic::add_ptr((intptr_t) time.ticks(), (intptr_t *) &_c1_total_compilation_ticks);
      }

      // (n) Take lock, fence.
    } // MutexUnlocker ends, so retake CompileTask_lock and GC

    // (m)   Mark as done (must inside locked region, as a producer can now delete).
    // (o)   If ref-count is zero, free() job.
    if( !task->consumer_set_state_done_and_delete_if_unused(&_queue) ) 
      // (p)   Else notifyall(CompileLock) to wake up sleeping JavaThreads
CompileTask_lock.notify_all();

    stat_gather();
    cthread->_task = NULL;
    _num_idle_cx_threads++;
    // (q)   Loop to (c)
wait_start_time=os::elapsed_counter();
  }

  // CompileThread is going away after leaving the main loop
}

// Scan the queue for a CompileTask for this method.  Return the matching
// CompileTask if it exists.
CompileTask *CompileBroker::scan_for_task( methodHandle to_be_compiled, int osr_bci ) {
assert_lock_strong(CompileTask_lock);
  // Scan queue for the job.  There can be several OSR variants so check the
  // BCI as well.  We need to scan the whole queue for a hit, since we're not
  // sorted by the method we're looking for.
  CompileTask *normal = NULL;
  for( CompileTask *task = _queue; task; task = task->_next ) {
    if( JNIHandles::resolve(task->_method) == to_be_compiled() ) {
      if( task->osr_bci() == osr_bci )
        return task;            // Return exact match
      if( !task->is_osr() )
        normal = task;          // Record any normal-entry task found
    }
  }
  // If we're looking for an osr task and found a normal-entry task instead,
  // the caller may just want to blow off the OSR and wait for the normal task
  // to complete instead.
  return normal;                // Return NULL or a normal-entry task
}

// Choose best task, if any
CompileTask*CompileBroker::choose_next_task(){
assert_lock_strong(CompileTask_lock);
  CompileTask *best = NULL;
  for( CompileTask *task = _queue; task; task = task->_next ) {
    if( !task->is_waiting() ) continue;
    if( CompileTheWorld ) return task; // For CTW, pick first waiting task in order
    if( !best ||
        best->_state > task->_state ||
        (best->_state == task->_state &&
         best->_score >  task->_score) )
best=task;
  }
  return best;
}

void CompileBroker::producer_create_compiler_thread(JavaThread*thread){
  EXCEPTION_MARK;

  // Create a name for our thread.
  char name_buffer[256];
  int tid = _num_cx_threads++; // Get a unique name while holding the compile lock
  _num_idle_cx_threads++;
  sprintf(name_buffer, "C%dCompilerThread%d", _id, tid);

  // Must enable the interpreter for the recursive call into Java.  Only
  // really an issue for -Xcomp runs.  Otherwise, atomically make it negative
  // which will put -Xcomp on hold until it's back to zero.
  jint x;
  do x = UseInterpreter;        // Read once per CAS attempt
  while( x != 1 &&              // If set to TRUE, leave it at TRUE forever
         x != Atomic::cmpxchg(x-1,&UseInterpreter,x) );

  // Do work inside a MutexUnlocker - that work might fail.  Cleanup
  // needs to happen with the lock held again.
  bool success = producer_create_compiler_thread_impl(thread,&name_buffer[0], tid);

  // Atomically increment to cancel the above decrement.  Re-enables -Xcomp
  // mode when no more compile threads are being created.
  do x = UseInterpreter;        // Read once per CAS attempt
  while( x != 1 &&              // If set to TRUE, leave it at TRUE forever
         x != Atomic::cmpxchg(x+1,&UseInterpreter,x) );

  if( !success ) {              // Failed?
    _num_cx_threads--;          // While holding CompileTask_lock, indicate no success
    _num_idle_cx_threads--;     // No Compiler thread created
    if( HAS_PENDING_EXCEPTION ) {
      if( CIPrintCompileQueue ) {
        ResourceMark rm;
tty->print_cr("|  Cannot create new C%d thread, got Java exception %s",_id,
instanceKlass::cast(PENDING_EXCEPTION->klass())->external_name());
      }
CLEAR_PENDING_EXCEPTION;//Clear out any junk exception made in the attempt
    }
  }
}

bool CompileBroker::producer_create_compiler_thread_impl(JavaThread *THREAD, char *name_buffer, int tid) {
  MutexUnlocker_GC_on_Relock mux(CompileTask_lock); // Must release before allocation, and for calling Java code
  // Since CompilerThreads are JavaThreads, they need a mirror ThreadObj and a Java-visible name
klassOop koop=SystemDictionary::resolve_or_fail(vmSymbolHandles::java_lang_Thread(),true,CHECK_0);
instanceKlassHandle klass(THREAD,koop);
  if( !klass->is_initialized() ) return false; // Too early in -Xcomp
  instanceHandle jthread = klass->allocate_instance_handle(false,CHECK_0);
  typeArrayOop name_oop = oopFactory::new_charArray(name_buffer, false/*No SBA*/, CHECK_0);
typeArrayHandle name(THREAD,name_oop);
  // Initialize jthread and put it into the system threadGroup.
  // Do not call Java based initializer here, because that can mess with the
  // stack of the current Java thread. For example if a compile threshold
  // overflow happens at a bad time, then you can run into lock ordering
  // problems.
  Handle thread_group (THREAD,  Universe::system_thread_group());
java_lang_Thread::set_name(jthread(),name());
  java_lang_Thread::set_threadGroup(jthread(), thread_group());

  // Create the Compile thread while NOT holding the CompileTask_lock
  MutexLockerAllowGC mu(Threads_lock, THREAD);
  CompilerThread *compiler_thread = (this == &_c1) 
    ? (CompilerThread*)new (ttype::compiler_thread) C1CompilerThread(tid) 
    : (CompilerThread*)new (ttype::compiler_thread) C2CompilerThread(tid);
  
  if (!compiler_thread)         // Allocation failed?
    return false;             // Reclaims CompileTask_lock, and can GC

  // At this point it may be possible that no osthread was created for the
  // JavaThread due to lack of memory or we reached os::_os_thread_limit
  // Let some other compiler thread compile this task or let somebody else
  // create another compiler thread later if possible
if(compiler_thread->osthread()==NULL){
    if (CIPrintCompileQueue) {
      Unimplemented();
      //tty->print_cr("|  Cannot create new C%d thread, process thread count/limit = %d/%d", _id, 
      //              os::_os_thread_count, os::_os_thread_limit );
      //os::print_memory_info(tty);
    }
    delete compiler_thread;
    return false;             // Reclaims CompileTask_lock, and can GC
  }
java_lang_Thread::set_thread(jthread(),compiler_thread);
compiler_thread->set_threadObj(jthread());
java_lang_Thread::set_daemon(jthread());
  
  compiler_thread->set_os_priority(CompilerThreadPriority);

  Threads::add(compiler_thread, false);
Thread::start(compiler_thread);

  // destructor reclaims the CompileTask_lock on exit, and can GC
  return true;
}


void CompileBroker::producer_add_task_impl( methodHandle method, methodHandle hot, int osr_bci ) {
  // (a) take the CompileLock, fence, then 
  assert(method->method_holder()->klass_part()->oop_is_instance(), "not an instance method");
  assert(osr_bci == InvocationEntryBci || (0 <= osr_bci && osr_bci < method->code_size()), "bci out of range");
  assert(!method->is_abstract() && !method->is_native(), "cannot compile abstract/native methods");
  assert(!instanceKlass::cast(method->method_holder())->is_not_initialized(), "method holder must be initialized");
  JavaThread *THREAD = JavaThread::current(); // Cache thread
  bool is_osr = osr_bci != InvocationEntryBci;

  // Fast check for common case.  Real check is done below while holding the lock.
  if( is_osr ) {
    if( method()->lookup_osr_for(osr_bci) )  return;
  } else if( this == &_c1 ) {
    if( method()->lookup_c1() ) return;
  } else {
    if( method()->lookup_c2() ) return;
  }

  // The method may be explicitly excluded by the user.
  bool quietly;
  if (CompilerOracle::should_exclude(method, quietly)) {
    method->set_not_compilable(this==&_c1 ? 1 : 2);
    if( !quietly ) {
      ResourceMark rm;
      BufferedLoggerMark lm(NOTAG, Log::M_COMPILE | Log::L_LO, true);
      lm.out("### Excluding compile: ");
      method->print_short_name(lm.stream());
    }
  }

  if( this==&_c1 ? !method->is_c1_compilable() : !method->is_c2_compilable() )
    return;                     // We failed to compile this method already

  // Never compile a method if breakpoints are present in it
if(method()->number_of_breakpoints()>0)
    return;

  // Some compilers may not support on stack replacement.
  // Some methods may have failed OSR already.
  if( is_osr ) {
    if( !CICompileOSR || !_abstract_compiler->supports_osr() )
      return;
  }

  // If the pending list lock is held by the thread requesting compilation, we
  // have to enable background compilation.  The compiler will deadlock if GC
  // is necessary during this compilation, since the pending_list_lock has to
  // be acquired before GC can be run. 

  // Make this check before allocating a new thread or string, since that will
  // cause an allocation of a java.lang.Thread for the new compiler thread. If
  // that allocation blocks for GC, we would be deadlocked.
  oop plloop = java_lang_ref_Reference::pending_list_lock();
  bool holds_pending_lock = (plloop!=NULL && ObjectSynchronizer::current_thread_holds_lock(THREAD, plloop));

  // See if the job is already in progress
  bool in_progress = false;
  { MutexLockerAllowGC ml(CompileTask_lock, THREAD);
  in_progress = scan_for_task( method, osr_bci ) != NULL;
  }

  // some prerequisites that are compiler specific: preload string constants
  // before compiling C2, but only if this task has not already been created.
  if( !in_progress && this == &_c2 ) {
    // Cannot allocate any strings, so just exit (and compile this method later).
    if( holds_pending_lock ) return;
    // Must be done while NOT holding the CompileTask_lock.  Since it's
    // moderately expensive do it after any cutouts above.
method->constants()->resolve_string_constants(THREAD);
    // Resolve all classes seen in the signature of the method we are compiling.
    bool sig_is_loaded = methodOopDesc::load_signature_classes(method, THREAD);
    if (HAS_PENDING_EXCEPTION || !sig_is_loaded) {
      CLEAR_PENDING_EXCEPTION; 
      method->set_not_compilable(2);
      return;
    }
  }

  // JVMTI -- post_compile_event requires jmethod_id() that may require
  // a lock the compiling thread can not acquire.  Prefetch it here.
  if (JvmtiExport::should_post_compiled_method_load() ) { 
    method->jmethod_id(); 
  }

  if (HAS_PENDING_EXCEPTION) {
    // We can get here if we hit a stack overflow in one of the
    // above calls to java.  In that case, we just drop the compile
    // request because we cannot allocate its nmethodoop if
    // there is a pending exception.  It can always be retriggered later.
    return;
  }

  // Compute score for the lookup
int score=method()->code_size();
  if( method() != hot() && hot.not_null() ) 
    score += hot->code_size();
  bool background = (this == &_c1 ? C1BackgroundCompilation : C2BackgroundCompilation);

  // Take the CompileLock here, where's its obviously unlocked on all exit paths.
MutexLockerAllowGC ml(CompileTask_lock,THREAD);

  stat_gather();                // Gather current CompileBroker stats

  // (b)   check for installed code or currently in-progress, or job in queue
  // Must check while holding the CompileTask_lock, lest somebody sneak it in
  // between the check and the taking of the lock.
  // (c)   If it finds code, then 
  // (d)     simply unlock & go execute the code.  
  if( is_osr ) {
    if( method()->lookup_osr_for(osr_bci) ) return;
  } else if( this == &_c1 ) {
    if( method()->lookup_c1() ) return;
  } else {
    if( method()->lookup_c2() ) return;
  }

  // See if we're already compiling this job
  CompileTask *task = scan_for_task( method, osr_bci );

  // Check for pending normal compile with an OSR request
  if( task && task->osr_bci() != osr_bci ) {
    // We've found a pending normal compile - and we've got an OSR request.
    // If we deny the OSR we'll eventually (hopefully) get to call the
    // normally compiled code later.  Since OSR's trigger fairly soon after
    // normal compile requests we have to be careful about flooding the system
    // with both normal and OSR requests for the same method.  So once we've
    // got a normal compile request we'll deny OSR requests unless they're
    // really needed.
    if( method()->invocation_count()*50 > method()->backedge_count() )
      return;                   // Deny the OSR request
    task = NULL;
  }

  // (e)   If it does not find a job (in queue or being compiled) then
  if( !task ) {
    // Assign a unique compile id (under the CompileTask_lock).
    int compile_id = ++CompileTask::_global_ids;
    // Out of range of CIStart+CIStop?
    if (compile_id < CIStart || CIStop <= compile_id) {
      method->set_not_compilable(this==&_c1?1:2); 
      return;
    }
    
    // (f)     it needs to create a job and insert it into the queue.  
assert_lock_strong(CompileTask_lock);
    task = new CompileTask( score, osr_bci, compile_id, method );
    task->_next = _queue;
_queue=task;
  } else {
    // Found prior task and since we're asking again for it, raise it's priority
    task->_score -= 1;          // Raise priority by lowering score
    if( background )  return;   // If not waiting, simply unlock and carry on in the interpreter.
  }
  assert0( JNIHandles::resolve(task->_method) == method() );

#ifndef PRODUCT
  if (CIPrintCompileQueue) {
assert_lock_strong(CompileTask_lock);
    tty->print_cr("+---C%d COMPILE QUEUE---- (%d tasks; %d compile threads) @ time %9.03fs",_id, total_tasks(), _num_cx_threads, os::elapsedTime());
    for( CompileTask *task = _queue; task; task = task->_next ) {
      tty->print("| ");
      task->print(_id);
    }
tty->print_cr("+---================----");
  }
#endif

  // Cannot allocate java thread, so just exit (and compile this method in the background).
  if( holds_pending_lock ) return;

  // (g)   Decide if another Compile thread is needed (queue depth, time to complete,etc)
  if (_num_cx_threads == 0 || // Spawn if no compiler thread
        (_num_idle_cx_threads <= 1 && waiting_tasks()>0 &&
        _num_cx_threads < CIMaxCompilerThreads &&
        // Check whether there is enough committed memory left
        !should_get_to_single_compiler_thread_mode() &&
        // Check whether thread count is a near os::linux::_os_thread_limit
        os::_os_thread_count < os::_os_thread_limit - 10
        )
     ) {
    // (h)     If so, spawn a Compile thread.
    if (CIPrintCompileQueue) {
tty->print_cr("|  Creating new C%d thread, only have %d and have %d waiting jobs",
                    _id, _num_cx_threads, waiting_tasks() );
    }
    task->_ref_count++;    // Prevent deleting job from another thread
    producer_create_compiler_thread(THREAD); // Releases & re-grabs CompileLock
    task->_ref_count--;    // Prevent deleting job from another thread
  } else {
    // (l) notifyAll()  // wake up sleeping compiler threads
CompileTask_lock.notify_all();
  }

  // (i)   Now that it has a job it decides to wait or not.  
  if( background )  return;// (j)   If not waiting, simply unlock and carry on in the interpreter.
  if( _num_cx_threads == 0 ) return; // -Xcomp startup: must run on in interpreter

  // -Xbatch jobs wait here
  // (k)   If waiting, then bump the job's reference count and 
  task->_ref_count++;    // Prevent deleting job from another thread

  do { 
    // (m) wait(CompileLock, timeout).
    CompileTask_lock.wait_micros(2000000L/*wait awhile; sometimes we miss notifys so keep rechecking*/, false);
    // (n)   When awaking, check for job done.  If not, wait again.
  } while( task->_state != CompileTask::done );

  // (o)   Lower the reference count (and at zero and job is done, free() the job),
  --task->_ref_count;
  if( task->_ref_count == 0 && task->_state == CompileTask::done )
    delete task;

  // (p)   Fence and unlock.  
  // (q) Carry on, (either with compiled code or not, depending on the job).
}

void CompileBroker::all_to_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  int limit = req->int32_parameter_by_name("limit");
  if (limit == 0) limit = 10;

  // Called both by Java and non-Java threads
  MutexLockerAllowGC ml(&CompileTask_lock, 1);
  azprof::Xml tag(res, "compile-brokers");

  int total_thread_count = _c1.num_cx_threads() + 
                           _c2.num_cx_threads();
  azprof::Xml::leaf(res, "total-thread-count", total_thread_count);
  if (UseC2) {
    azprof::Xml tag(res, "compile-broker");
    { azprof::Xml::leaf(res, "name", "Server");
      _c2.to_xml(req, res, limit);
    }
  }
  if (UseC1) {
    azprof::Xml tag(res, "compile-broker");
    { azprof::Xml::leaf(res, "name", "Client");
      _c1.to_xml(req, res, limit);
    }
  }
#endif // AZ_PROFILER
}

void CompileBroker::to_xml(azprof::Request *req, azprof::Response *res, int limit) {
#ifdef AZ_PROFILER
assert_lock_strong(CompileTask_lock);

  GrowableArray<CompileTask*> in_progress_tasks(8);
  GrowableArray<CompileTask*> waiting_tasks(8);
  for (CompileTask *task = _queue; task; task = task->_next) {
if(task->is_in_progress()){
      in_progress_tasks.append(task);
    } else if (task->is_waiting()) {
      waiting_tasks.append(task);
    }
  }
  in_progress_tasks.sort(CompileTask::compare_scores);
  waiting_tasks.sort(CompileTask::compare_scores);

  azprof::Xml::leaf(res, "thread-count", _num_cx_threads);
  azprof::Xml::leaf(res, "idle-thread-count", _num_idle_cx_threads);
  azprof::Xml::leaf(res, "single-thread-mode", _get_to_single_compiler_thread_mode ? "true" : "false");
  { azprof::Xml tag(res, "compile-tasks");
    azprof::Xml::leaf(res, "state", "Compiling");
    azprof::Xml::leaf(res, "count", in_progress_tasks.length());
    azprof::Xml::leaf(res, "limit", limit);
    int n = MIN2(in_progress_tasks.length(), limit);
    for (int i = 0; i < n; i++) in_progress_tasks.at(i)->to_xml(req, res);
  }
  { azprof::Xml tag(res, "compile-tasks");
    azprof::Xml::leaf(res, "state", "Waiting");
    azprof::Xml::leaf(res, "count", waiting_tasks.length());
    azprof::Xml::leaf(res, "limit", limit);
    int n = MIN2(waiting_tasks.length(), limit);
    for (int i = 0; i < n; i++) waiting_tasks.at(i)->to_xml(req, res);
  }
#endif // AZ_PROFILER
}

void CompileBroker::print_times() {
  Unimplemented();
}

#ifndef PRODUCT
void CompileBroker::stat_gather(){
  _queue_probes++;              // Depth computed 1 more time
  // Cumlative sum of queue depth (waiting tasks) and active compiler threads
  _queue_depth_total += waiting_tasks();
  _num_threads_total += _num_cx_threads;
}
#endif

void CompileBroker::print_compiler_threads_on(outputStream*st){
#ifndef PRODUCT
  st->print_cr("Compiler thread printing unimplemented.");
  st->cr();
#endif
}
