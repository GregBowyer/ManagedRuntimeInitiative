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
#ifndef THREAD_HPP
#define THREAD_HPP


#include "allocatedObjects.hpp"
#include "atomic.hpp"
#include "atomic_os_pd.inline.hpp"
#include "c1_ThreadLocals.hpp"
#include "exceptions.hpp"
#include "flatHashSet.hpp"
#include "frame.hpp"
#include "growableArray.hpp"
#include "javaFrameAnchor.hpp"
#include "liveObjects.hpp"
#include "orderAccess.hpp"
#include "os.hpp"
#include "osThread.hpp"
#include "sbaThreadInfo.hpp"
#include "stackRef_pd.hpp"
#include "threadLocalAllocBuffer.hpp"
#include "vreg.hpp"

class ArtaThreadState;
class AuditTrail;
class DeferredObjAllocEvent;
class GCTaskQueue;
class HandleArea;
class HeapRefBuffer;
class InstructionTraceArray;
class JNIHandleBlock;
class JvmtiGetLoadedClassesClosure;
class JvmtiThreadState;
class MMU;
class OSThread;
class ObjectMonitor;
class ObjectLocker;
class OopClosure;
class PrivilegedElement;
class ProfileEntry;
class ResourceArea;
class ThreadClosure;
class ThreadFilter;
class ThreadLocalProfileBuffer;
class ThreadProfiler;
class ThreadStatistics;
class ciEnv;
class vframe;
typedef void *voidp;        // stuff to avoid having to include zlib.h
typedef voidp gzFile;       // stuff to avoid having to include zlib.h

class jvmtiDeferredLocalVariableSet;
class ThreadCounters;

extern outputStream *tty;

// Class hierarchy
// - Thread
//   - VMThread
//   - JavaThread
//   - WatcherThread

typedef void (*ThreadFunction)(JavaThread*, TRAPS);



class JavaThreadSpillover {
public:
#ifdef ASSERT
  int _java_call_counter;
#endif


  // For deadlock detection.
  int _depth_first_number;


  ThreadFunction _entry_point;
  
  bool _is_finalizer_thread;

  uint64_t  _safepoint_pc;
  uint64_t  _safepoint_rpc;
  uint64_t  _safepoint_total;
  uint64_t  _safepoint_count;
  uint64_t  _safepoint_min;
  uint64_t  _safepoint_max;
  uint64_t  _safepoint_max_when;
  uint64_t  _safepoint_max_pc;
  uint64_t  _safepoint_max_rpc;

  jlong     _ttsp_tick_time;
  void*     _ttsp_profile_entry;

  SBAArea *_sba_area;           // SBA support
  
  // Safepoint overhead profiling of object init/clone and arraycopy
  jlong  _obj_zero_max_ticks;
  size_t _obj_zero_max_tick_words;
  jlong  _obj_clone_max_ticks;
  size_t _obj_clone_max_tick_words;
  jlong  _arraycopy_max_ticks;
  size_t _arraycopy_max_tick_words;
  
  MMU *_mmu;
};


class Thread: public ThreadShadow {
 private:
  // Exception handling
  // (Note: _pending_exception and friends are in ThreadShadow)
  //objectRef _pending_exception;                // pending exception for current thread
  // const char* _exception_file;                   // file information for exception (debugging only)
  // int         _exception_line;                   // line information for exception (debugging only)

 private:
  ThreadCounters* _thread_counters; // an area for recording thread performance numbers
 public:
  ThreadCounters* thread_counters() const        { return _thread_counters; }

 protected:
  // Debug tracing
  static void trace(const char* msg, const Thread* const thread) PRODUCT_RETURN;

 private:
  // Active_handles points to a block of handles
  JNIHandleBlock* _active_handles;

  // One-element thread local free list
  JNIHandleBlock* _free_handle_block;

  // Point to the last handle mark
  HandleMark* _last_handle_mark;

  public:
   void set_last_handle_mark(HandleMark* mark)   { _last_handle_mark = mark; }
    HandleMark* last_handle_mark() const          { return _last_handle_mark; }

  // debug support for checking if code does allow safepoints or not
  // GC points in the VM can happen because of allocation, invoking a VM operation, or blocking on 
  // mutex, or blocking on an object synchronizer (Java locking).
  // If !allow_safepoint(), then an assertion failure will happen in any of the above cases
  // If !allow_allocation(), then an assertion failure will happen during allocation
  // (Hence, !allow_safepoint() => !allow_allocation()).
  //
  // The two classes No_Safepoint_Verifier and No_Allocation_Verifier are used to set these counters.
  //
  NOT_PRODUCT(int _allow_safepoint_count;)       // If 0, thread allow a safepoint to happen
  debug_only (int _allow_allocation_count;)      // If 0, the thread is allowed to allocate oops.  

  // Record when GC is locked out via the GC_locker mechanism
  CHECK_UNHANDLED_OOPS_ONLY(int _gc_locked_out_count;)
 
private:
  friend class No_Alloc_Verifier;
  friend class No_Safepoint_Verifier;
  friend class Pause_No_Safepoint_Verifier;
  friend class GC_locker;

  ThreadLocalAllocBuffer _tlab;                  // Thread-local eden
int _no_tlab_parking;//For NoTLABParkingMark

long _vm_operation_started_count;//VM_Operation support
long _vm_operation_completed_count;//VM_Operation support

ObjectMonitor*_current_pending_monitor;//ObjectMonitor this thread is waiting to lock
  bool _current_pending_monitor_is_from_java;    // locking is from Java code

  // ObjectMonitor on which this thread called Object.wait()
  ObjectMonitor* volatile _current_waiting_monitor;

 public: 
  int         _debug_level;                      // to prevent assertions from triggering

  // Constructor
  Thread();
  virtual ~Thread();

  // initializtion
  void initialize_thread_local_storage();

  // thread entry point
  virtual void run();

  // Testers
  virtual bool is_VM_thread()       const            { return false; }
  virtual bool is_Java_thread()     const            { return false; }
  virtual bool is_Compiler_thread() const            { return false; }
  virtual bool is_C1Compiler_thread() const          { return false; }
  virtual bool is_C2Compiler_thread() const          { return false; }
  virtual bool is_hidden_from_external_view() const  { return false; }  
  virtual bool is_jvmti_agent_thread() const         { return false; }
  // Is this a GC thread, either VM, GC_task, Pauseless, GenPausless, ...
  // This slightly differs from the Sun definition below.
  virtual bool is_GC_thread() const                  { return false; }
  // True iff the thread can perform GC operations at a safepoint.
  // Generally will be true only of VM thread and parallel GC WorkGang
  // threads.
  virtual bool is_GC_task_thread() const             { return false; }
  virtual bool is_jvmti_heap_iterator_thread() const { return false; }
  virtual bool is_Watcher_thread() const             { return false; }
  virtual bool is_GenPauselessGC_thread() const      { return false; }
  virtual bool is_ITR_thread() const                 { return false; }

  virtual char* name() const { return (char*)"Unknown thread"; }

  // Returns the current thread
#include "threadLS_os_pd.hpp"

  // Common thread operations
static JavaThreadPriority get_priority(const Thread*const thread);
  static void start(Thread* thread);
  static void interrupt(Thread* thr);
static bool is_interrupted(Thread*thr);

//Used by TickProfiler
  inline bool has_safep_suspend();

  // Support for Unhandled Oop detection
#ifdef CHECK_UNHANDLED_OOPS
 private:
  UnhandledOops *_unhandled_oops;
 public:
  UnhandledOops* unhandled_oops()               { return _unhandled_oops; }
  // Mark oop safe for gc.  It may be stack allocated but won't move.
  void allow_unhandled_oop(oop *op)              {
    if (CheckUnhandledOops) unhandled_oops()->allow_unhandled_oop(op);
  }
  // Clear oops at safepoint so crashes point to unhandled oop violator
  void clear_unhandled_oops()                   {
    if (CheckUnhandledOops) unhandled_oops()->clear_unhandled_oops();
  }
  bool is_gc_locked_out() { return _gc_locked_out_count > 0; }
#endif // CHECK_UNHANDLED_OOPS


// Support for per-thread GPGC barrier data and methods.
#include "gctrap_os_pd.inline.hpp"


// Pauseless GC thread stack cleaning:
private:
  bool _stack_cleaning;

public:
void set_stack_cleaning(bool state){_stack_cleaning=state;}
  bool is_stack_cleaning () const     { return _stack_cleaning; }

// The heap ref buffers are used by PauselessGC and GenPauselessGC to keep track of heapRefs
// that haven't yet been given to the collector for marking through.
private:
  HeapRefBuffer* _new_gen_ref_buffer;
  HeapRefBuffer* _old_gen_ref_buffer;
public:
static ByteSize new_gen_ref_buffer_offset(){return byte_offset_of(Thread,_new_gen_ref_buffer);}
static ByteSize old_gen_ref_buffer_offset(){return byte_offset_of(Thread,_old_gen_ref_buffer);}
  void            flush_unmarked_refs();
  HeapRefBuffer*  get_new_gen_ref_buffer()       { return _new_gen_ref_buffer; }
  HeapRefBuffer*  get_old_gen_ref_buffer()       { return _old_gen_ref_buffer; }
  HeapRefBuffer** get_new_gen_ref_buffer_addr()       { return &_new_gen_ref_buffer; }
  HeapRefBuffer** get_old_gen_ref_buffer_addr()       { return &_old_gen_ref_buffer; }

public:
  // Resource area
  ResourceArea* resource_area() const            { return _resource_area; }
  void set_resource_area(ResourceArea* area)     { _resource_area = area; }

  OSThread* osthread() const                     { return _osthread;   }
  void set_osthread(OSThread* thread)            { _osthread = thread; }

  // JNI handle support
  JNIHandleBlock* active_handles() const         { return _active_handles; }
  void set_active_handles(JNIHandleBlock* block) { _active_handles = block; }
  JNIHandleBlock* free_handle_block() const      { return _free_handle_block; }
  void set_free_handle_block(JNIHandleBlock* block) { _free_handle_block = block; }
  int64_t active_handle_count() const;

  // Internal handle support
  HandleArea* handle_area() const                { return _handle_area; }
  void set_handle_area(HandleArea* area)         { _handle_area = area; }

  // Thread-Local Allocation Buffer (TLAB) support
  ThreadLocalAllocBuffer& tlab()                 { return _tlab; }
  void initialize_tlab() {
      tlab().initialize();
  }
		 
  int  no_tlab_parking()                         { return _no_tlab_parking; }
  void increment_no_tlab_parking()               { _no_tlab_parking++;
                                                   assert0(this==Thread::current());
                                                   assert0(_no_tlab_parking > 0); }
  void decrement_no_tlab_parking()               { _no_tlab_parking--;
                                                   assert0(this==Thread::current());
                                                   assert0(_no_tlab_parking >= 0); }

  // VM operation support
long vm_operation_ticket(){return++_vm_operation_started_count;}
long vm_operation_completed_count(){return _vm_operation_completed_count;}
  void increment_vm_operation_completed_count()  { _vm_operation_completed_count++; }
  void self_pc_and_rpc(int depth, uint64_t *result_pc, uint64_t *result_rpc);
  
  // For tracking the heavyweight monitor the thread is pending on.
  ObjectMonitor* current_pending_monitor() {
    return _current_pending_monitor;
  }
  void set_current_pending_monitor(ObjectMonitor* monitor) {
    _current_pending_monitor = monitor;
  }
  void set_current_pending_monitor_is_from_java(bool from_java) {
    _current_pending_monitor_is_from_java = from_java;
  }
  bool current_pending_monitor_is_from_java() {
    return _current_pending_monitor_is_from_java;
  }

  // For tracking the ObjectMonitor on which this thread called Object.wait()
  ObjectMonitor* current_waiting_monitor() {
    return _current_waiting_monitor;
  }
  void set_current_waiting_monitor(ObjectMonitor* monitor) {
    _current_waiting_monitor = monitor;
  }

  // GC support
  // Apply "f->do_oop" to all root oops in "this".
  void oops_do(OopClosure* f);  

  // Sweeper support
  void methodCodes_do();  
  
  // Check if address is in the stack of the thread
  bool is_in_stack(intptr_t addr) const;

  // Sets this thread as starting thread. Returns failure if thread
  // creation fails due to lack of memory, too many threads etc.
  bool set_as_starting_thread();

 protected:
  // OS data associated with the thread
  OSThread* _osthread;  // Platform-specific thread information 

  // Thread local resource area for temporary allocation within the VM
  ResourceArea* _resource_area;

  // Thread local handle area for allocation of handles within the VM
  HandleArea* _handle_area;

  // Support for stack overflow handling, get_thread, etc.  
  address          _stack_base;
  size_t           _stack_size;
  uintptr_t        _self_raw_id;      // used by get_thread (mutable)
  int              _lgrp_id;

  // SBA support. 
  // The sba_thread is used by code doing sba work.  JavaThreads should return
  // their "this" pointer, the VM thread or gc worker threads will return a
  // pointer to the java thread they are examining.  This value is used for
  // the hardware thread-local segment register (
private:
JavaThread*_sba_thread;
public:
  JavaThread* sba_thread() const { return _sba_thread; }
void set_sba_thread(JavaThread*thread);
void set_abase(JavaThread*thread);

  int     lgrp_id() const                 { return _lgrp_id; }
  void    set_lgrp_id(int value)          { _lgrp_id = value; }

  // Printing
  void print_on(outputStream* st) const;
  void print() const { print_on(tty); }
  virtual void print_on_error(outputStream* st, char* buf, int buflen) const;
  void state_print_xml_on(xmlBuffer *xb, const char *name, bool dostate = true);
  virtual const char *status_msg()   const { return "unknown"; }
  virtual jlong       time_blocked() const { return 0; } // Allow JavaThreads to override the start time of blocking

  // Trap handler support: Trap handlers may need a space to stash a few register values
 private:
#define SIZE_TRAP_PRESERVED_REGS 3
  HeapWord    _trap_preserve_regs[SIZE_TRAP_PRESERVED_REGS];   // space for a trap handler to squirrel away a few regs
 public:
static ByteSize trap_preserve_reg_offset(int index){
    assert0(index>=0 && index<SIZE_TRAP_PRESERVED_REGS);
return byte_offset_of(Thread,_trap_preserve_regs[index]);
  }

 // Tracing support
 private:
  intptr_t _vm_tag;   // What operation in the VM is executing.

 public:
  void     set_vm_tag(intptr_t tag) { _vm_tag = tag; }
  intptr_t vm_tag() const           { return _vm_tag; }

  // Returns a value that is unique for the lifespan of this process
  inline int64_t unique_id();
  inline intptr_t reversible_tid(); // Another unique identifier
  enum { reversible_tid_bits = 30 };
  static inline Thread *reverse_tid(intptr_t rtid); // Reverse back to a thread ptr
  
  int32_t _rand_seed;    // per thread random seed

 public:
  int32_t next_random()  { return os::random_impl(&_rand_seed); }

  // Debug-only code
#ifdef ASSERT
protected:
  // Stack of locks owned by thread.
  // Deadlock detection support, plus general debugging.
AzLock*_owned_locks;
  
public:  
  void print_owned_locks_on(outputStream* st) const;
  void print_owned_locks() const               { print_owned_locks_on(tty);    }
AzLock*owned_locks()const{return _owned_locks;}
  bool owns_locks() const    { return owned_locks() != NULL; }
  bool chk_lock(AzLock *, bool allow_gc); // Check general lock ranking rules before acquiring another lock
  void push_lock(AzLock *);      // Push a lock onto this thread's lock-stack
  void pop_lock (AzLock *);      // Pop a lock from this thread's lock-stack

  // Deadlock detection
  bool allow_allocation()    { return _allow_allocation_count == 0; }
#endif
  
  void check_for_valid_safepoint_state(bool potential_vm_operation) PRODUCT_RETURN;

private:
  volatile int _jvmti_env_iteration_count;

public:  
  void entering_jvmti_env_iteration()            { ++_jvmti_env_iteration_count; }
  void leaving_jvmti_env_iteration()             { --_jvmti_env_iteration_count; }
  bool is_inside_jvmti_env_iteration()           { return _jvmti_env_iteration_count > 0; }

  // Code generation
  static ByteSize exception_file_offset()        { return byte_offset_of(Thread, _exception_file   ); }
  static ByteSize exception_line_offset()        { return byte_offset_of(Thread, _exception_line   ); }
  static ByteSize active_handles_offset()        { return byte_offset_of(Thread, _active_handles   ); }
static ByteSize vm_tag_offset(){return byte_offset_of(Thread,_vm_tag);}

  static ByteSize stack_base_offset()            { return byte_offset_of(Thread, _stack_base ); }
  static ByteSize stack_size_offset()            { return byte_offset_of(Thread, _stack_size ); }

#define TLAB_FIELD_OFFSET(name) \
  static ByteSize tlab_##name##_offset()            { return byte_offset_of(Thread, _tlab) + ThreadLocalAllocBuffer::name##_offset(); }

  TLAB_FIELD_OFFSET(start)
  TLAB_FIELD_OFFSET(end)
  TLAB_FIELD_OFFSET(top)
  TLAB_FIELD_OFFSET(size)                   // desired_size
  TLAB_FIELD_OFFSET(refill_waste_limit)
  TLAB_FIELD_OFFSET(number_of_refills)
  TLAB_FIELD_OFFSET(fast_refill_waste)
  TLAB_FIELD_OFFSET(slow_allocations)

#undef TLAB_FIELD_OFFSET

public:
jint _hashStateW;
  jint _hashStateX ;                           // thread-specific hashCode generator state
  jint _hashStateY ; 
  jint _hashStateZ ; 

static thread_key_t ITRArrayKey;
static thread_key_t ITRCurrentTracePositionKey;

  static void initITR();

  static void cleanupTraceArray(void* array);

  // Instruction Trace Recording support
  // Atomically steal the address trace array from the current thread
  // For use by vm destruction to steal back traces that have not been filled
  // (For use ONLY after this thread has stopped running)
  InstructionTraceArray* stealInstructionTraceArray();

  static int ITR_current_trace_position_offset_in_bytes() { if ((int)ITRCurrentTracePositionKey==0) initITR();
                                                            return (int)ITRCurrentTracePositionKey*8; }

  void setCurrentTracePosition(long* pos)                   { os::thread_local_storage_at_put(ITRCurrentTracePositionKey, pos); }

  void setInstructionTraceArray(InstructionTraceArray* ita) { os::thread_local_storage_at_put(ITRArrayKey, ita);  }
  InstructionTraceArray* getInstructionTraceArray()         { return (InstructionTraceArray*)os::thread_local_storage_at(ITRArrayKey); }

 public:
  volatile uint64_t  _sma_successes;
  volatile uint64_t  _sma_failures;
  volatile uint64_t  _sma_perf_evt0_pass;
  volatile uint64_t  _sma_perf_evt1_pass;
  volatile uint64_t  _sma_perf_evt0_fail;
  volatile uint64_t  _sma_perf_evt1_fail;

  static int sma_successes_offset_in_bytes()       { return offset_of(Thread,_sma_successes); }
  static int sma_failures_offset_in_bytes()        { return offset_of(Thread,_sma_failures); }
  static int sma_perf_evt0_pass_offset_in_bytes()  { return offset_of(Thread,_sma_perf_evt0_pass); }
  static int sma_perf_evt1_pass_offset_in_bytes()  { return offset_of(Thread,_sma_perf_evt1_pass); }
  static int sma_perf_evt0_fail_offset_in_bytes()  { return offset_of(Thread,_sma_perf_evt0_fail); }
  static int sma_perf_evt1_fail_offset_in_bytes()  { return offset_of(Thread,_sma_perf_evt1_fail); }

volatile uint32_t _sma_reason;
  static int sma_reason_offset_in_bytes()          { return offset_of(Thread,_sma_reason); }

  inline void reset_sma_stats() {
    _sma_successes = 0;
    _sma_failures  = 0;
    _sma_perf_evt0_pass = 0;
    _sma_perf_evt1_pass = 0;
    _sma_perf_evt0_fail = 0;
    _sma_perf_evt1_fail = 0;
    _sma_reason = 0;
  }

  volatile uint64_t  _safepoint_start;
  volatile uint64_t  _safepoint_end;
  
  uint64_t get_safepoint_start()         const { return _safepoint_start; }
  void set_safepoint_start(uint64_t x)         { _safepoint_start = x; }
  uint64_t get_safepoint_end()           const { return _safepoint_end; }
  void set_safepoint_end(uint64_t x)           { _safepoint_end = x; }
  
  static int safepoint_start_offset_in_bytes()      { return offset_of(Thread,_safepoint_start); }
  static int safepoint_end_offset_in_bytes()        { return offset_of(Thread,_safepoint_end); }

  // Object allocation, marking, and promotion profiling.
 private:
  AllocatedObjects  _allocated_objects;
  LiveObjects*      _live_objects;
 public:

static ByteSize allocated_objects_offset(){return byte_offset_of(Thread,_allocated_objects);}
  AllocatedObjects* allocated_objects()         { return &_allocated_objects; }
  LiveObjects*      live_objects()              { return (LiveObjects*)(intptr_t(_live_objects) & ~OneBit); }
  LiveObjects**     live_objects_addr()         { return &_live_objects; }
  intptr_t          raw_live_objects()          { return intptr_t(_live_objects); }

  inline void acquire_live_objects() {
    intptr_t old_live_objects = intptr_t(live_objects());
    while ( old_live_objects != Atomic::cmpxchg_ptr(old_live_objects | OneBit, (intptr_t*)live_objects_addr(), old_live_objects) ) {
    }
OrderAccess::acquire();
  }

  inline void release_live_objects() {
    // intptr_t live_objects;
    assert(is_set_nth_bit(intptr_t(_live_objects), 0), "should have acquired live_objects");
    OrderAccess::release();
    _live_objects  = (LiveObjects *) (raw_live_objects() & ~OneBit);
  }

  // Once a suspend request is posted to the thread, the thread will
  // self-suspend at some later time.  Meanwhile, somebody else might be busy
  // trying to resume us.  The self-suspension call and resume call are
  // racing.  We need to make sure that if suspend/resume requests come nicely
  // paired that the thread either sees both or neither.  To self-suspend,
  // we wait() on the monitor.  A resuming thread will notify() us.
  WaitLock *_self_suspend_monitor;

    // TODO HACK EPIC_FAIL: this is Linux specific code that shouldn't be here.
    ProfileEntry* _tick_profiling_buffer;
    timer_t _profiling_timer_id;
int32_t _next_profiling_entry_index;
};


// Name support for threads.  non-JavaThread subclasses with multiple
// uniquely named instances should derive from this.
class NamedThread: public Thread {
  friend class VMStructs;
  enum {
    max_name_len = 64
  };
 private:
  char* _name;
 public:
  NamedThread();
virtual~NamedThread();
  // May only be called once per thread.
  void set_name(const char* format, ...);
  virtual char* name() const { return _name == NULL ? (char*)"Unknown Thread" : _name; }
};

// Worker threads are named and have an id of an assigned work.
class WorkerThread: public NamedThread {
private:
  uint _id;
public:
  WorkerThread() : _id(0) { }
  void set_id(uint work_id) { _id = work_id; }
  uint id() const { return _id; }
};

// A single WatcherThread is used for simulating timer interrupts.
class WatcherThread: public Thread {
 public:
  virtual void run();

 private:
  static WatcherThread* _watcher_thread;

  static bool _should_terminate;  
 public:
  enum SomeConstants {
    delay_interval = 10                          // interrupt delay in milliseconds
  };

  // Constructor
  WatcherThread();
  virtual ~WatcherThread() { }

  // Tester
  bool is_Watcher_thread() const                 { return true; }

  // Printing
  char* name() const { return (char*)"VM Periodic Task Thread"; }
  void print_on(outputStream* st) const;
  void print() const { print_on(tty); }
  void print_xml_on(xmlBuffer *xb, bool ref);

  // Returns the single instance of WatcherThread
  static WatcherThread* watcher_thread()         { return _watcher_thread; }

  // Create and start the single instance of WatcherThread, or stop it on shutdown
  static void start();
  static void stop();
};


// Minimum Mutator Utilization
// A class to track MMU - essentially an integration of pause times.

// MMU is the smallest amount of utilization a mutator has at a given interval
// size.  For example, suppose we measure MMU at an interval size of 20msec.
// If we ever have a pause of 20msec or more, then the MMU@20 is 0, because
// there exists an 20msec interval where the mutator makes no progress.  For
// example, suppose we measure MMU@50, with a max pause of 20 and pauses far
// apart.  Then we see (50-20)/50 or 60% minimum mutator utilzation.  Suppose
// just after a 20msec pause, we take a "trap storm" of 1msec pauses with
// 1msec gaps between them.  So the mutator is stalled for 20msec, goes for
// 1msec, stalled for 1, goes for 1, etc.  Basically of the remaining 30msec
// after the pause, the mutator gets 1/2.  Then the MMU@50 is (50-20-1...)/50
// or (50-20-15)/50 or 30%.

// MMU is usually reported as a graph.  We report MMU at a number of intervals
// accumulated over the whole program.  MMU's past a few seconds will be
// extremely high; MMU's less than the maximum pause time will be zero.

// We collect pause start+duration in a ring buffer.  Each time we add a new
// pause to the ring buffer, we'll integrate that pause into the MMU
// calculations over each interval.  MMU calcs are done per-thread to avoid
// synchronization costs, then folded into the global calculation.
class MMU {
public:
  jlong *p_beg;                 // Pause beginning ring buffer, in OS ticks
  jlong *p_len;                 // Pause length    ring buffer, in OS ticks
  jlong dawn_tick;              // thread start tick
  jlong total_pauses;           // thread-local total paused ticks
  int head;                     // Head index into ring buffer; new pauses go here
  static const int MAX_IVL=7;   // Number of different intervals we track
  static jlong *INTERVALS;      // Array of intervals, in OS ticks
  static jlong *MAXES;          // Array of global max pause times for each interval
  static jlong ALL_TICKS;       // Sum of all thread-alive ticks
  static jlong ALL_PAUSE;       // Sum of all thread-paused ticks
  int *tails;                   // Array of tail indices; one per MMU interval
  jlong *pause;                 // Array of rolling pause times for each interval
  jlong *maxes;                 // Array of max pause times for each interval

  MMU();                        // Constructor; build & init arrays
  ~MMU();                       // Destructor; fold local results into global, free arrays

  // Pause starts now
  void start_pause();           // Pause starts now
  void end_pause();             // Pause ends now

  void fold_into_global();      // Fold local results into global

  static void print( xmlBuffer *xb );  // Print global results to either to stdout or xmlBuffer
};


class JavaThread: public Thread {  
 private:  
  JavaThread*    _next;                          // The next thread in the Threads list
  objectRef      _threadObj;                     // The Java level thread object

public:
  // The Java Execution Stack, as separate from the usual call stack.
  // Runs opposite the C stack until they collide.
  intptr_t *     _jexstk_top;   // Current top-of-stack
static ByteSize jexstk_top_offset(){return byte_offset_of(JavaThread,_jexstk_top);}
  static intptr_t *jexstk_base_for_sp(intptr_t*sp) { return (intptr_t*)(((address)Thread::stack_ptr_to_JavaThread(sp))+thread_map_jex_stack); }

  // The per-thread Lock Stack: list of locks currently held by interpreted
  // frames.  Compiled frames still require a stack crawl.
  objectRef *     _lckstk_base;  // Base area
  objectRef *     _lckstk_top;   // Current top-of-stack
  objectRef *     _lckstk_max;   // Limit
static ByteSize lckstk_base_offset(){return byte_offset_of(JavaThread,_lckstk_base);}
static ByteSize lckstk_top_offset(){return byte_offset_of(JavaThread,_lckstk_top);}
static ByteSize lckstk_max_offset(){return byte_offset_of(JavaThread,_lckstk_max);}
  // Extend the current threads' Lock Stack; called as a leaf routine from the
  // interpereter.
  void extend_lckstk_impl( ); 
  static void extend_lckstk( JavaThread *jt ) { return jt->extend_lckstk_impl(); }

#ifdef ASSERT
  int  java_call_counter()                       { return _javaThreadSpillover->_java_call_counter; }
  void inc_java_call_counter()                   { _javaThreadSpillover->_java_call_counter++; }
  void dec_java_call_counter() {
assert(_javaThreadSpillover->_java_call_counter>0,"Invalid nesting of JavaCallWrapper");
_javaThreadSpillover->_java_call_counter--;
  }
#endif  // ifdef ASSERT

  // Polling-Only Self-Suspension, 3/18/2003
  //
  // All threads now self-suspend on demand.  There are no more forced
  // suspensions.  Among other things, this removes a number of complications
  // when threads are stopped at unfortunate places.  Self-suspending threads
  // will force their own register windows to be flushed.  Self-suspending
  // threads never hold any funny locks, especially not the malloc-lock.
  //
private:
  // Threads are asked to stop by setting the per-Thread _please_self_suspend
  // flag.  Each thread is responsible for polling this flag at a Safepoint.
  // If it is set, the thread releases it's _jvm_lock and blocks.

  // The safep_suspend request is expected to be "good citizen" request and
  // the suspending code will restart the thread later.  safep_suspends do
  // NOT nest.  The Thread calls back into SafepointSynchronize::block();

  // External suspend/resume requests come from JVM_SuspendThread,
  // JVM_ResumeThread, JVM/TI SuspendThread, JVM/TI ResumeThread, JVM/PI
  // SuspendThread, and finally JVM/PI ResumeThread.  External suspend
  // requests cause or _jvmti_suspend or _jlang_suspend to be set and external
  // resume requests cause those bits to be cleared.  External suspend
  // requests do not nest on top of other external suspend requests.  The
  // higher level APIs reject suspend requests for already suspended threads.

  // External_suspend requests rely on external customers behaving well.
  // I.e., if multiple external threads fire off repeated suspend & resume
  // requests, the _jxxx_suspend will effectively be randomized.  The thread
  // will wait on the _self_suspend monitor.

  // async_suspend and intrp_suspend are self-starting.  The thread performs
  // the requested action at a Safepoint, clears the bit and restarts.

  // jint _please_self_suspend; - in ThreadShadow to ensure small offset from thread
public:
  enum SuspendFlags { 
    // NOTE: avoid using the sign-bit as cc generates different test code
    //       when the sign-bit is used, and sometimes incorrectly - see CR 6398077
    prmpt_suspend = 1<<0, // Cooperative OS preemption request
    safep_suspend = 1<<1, // Will block on Threads_lock; cleared by VM thread
    jvmti_suspend = 1<<2, // Will block on _self_suspend_monitor; cleared by jvmti thread
    jlang_suspend = 1<<3, // Will block on _self_suspend_monitor; cleared by java thread
    async_suspend = 1<<5, // Throw an async exception ; cleared by self after handling
    intrp_suspend = 1<<6, // Switch to interpreter-only mode.  Used for full-speed debugging.
    unsaf_suspend = 1<<7, // Throw an async unsafe exception
    print_suspend = 1<<8, // Print stack on global XML buffer
    stack_suspend = 1<<10,// Throw a stack overflow exception; cleared by self after handling
    rstak_suspend = 1<<11,// Rstack was written, needs hardware sync'ing
    checkp_suspend= 1<<13,// Observe checkpoint; cleared by VM thd or self after running chkpt function 
    stack_is_clean= 1<<14,// Indicates thread stack is GC clean.  Clear when JavaThread starts to run again
    gpgc_clean_new= 1<<15,// GenPauselessGC needs NewGen refs on stack cleaned; cleared after stack cleaned
    gpgc_clean_old= 1<<16,// GenPauselessGC needs OldGen refs on stack cleaned; cleared after stack cleaned
    unbias_suspend= 1<<17,// Clean the unbias-lock list
    ttsp_suspend  = 1<<18 // Profile time-to-safepoint: see TickProfileTimeToSafepoint option.
  };
  // Atomically read both bits.  The bits are kept together because this
  // allows a polling thread to test & read a single word as part of the
  // self-suspend check.
  int please_self_suspend() const { return _please_self_suspend; }

  // Are either external-suspend bits set?
  int is_being_ext_suspended() const { return _please_self_suspend & (jvmti_suspend|jlang_suspend); }

  // Either set the jvmti bit (0->1 transition) or return an error.
  bool set_jvmti_suspend_request( ) {
    jint x;
    do {
x=_please_self_suspend;
      if (x & jvmti_suspend) { 
        return false; // already suspended
      }
    } while (x != Atomic::cmpxchg(x |jvmti_suspend, &_please_self_suspend, x & ~jvmti_suspend));
    set_jvm_lock_poll_advisory();
    return true;
  }

  // Spin until the bit is atomically set/clear.  Required in case two threads
  // are attempting to set and clear the individual bits in parallel.  Also
  // set the polling advisory bit in _jvm_lock (forces a CAS to fail when
  // attempting to retake the lock, which in turn forces a poll).
  void set_suspend_request( enum SuspendFlags f ) { 
    jint x = _please_self_suspend;
    while( x != Atomic::cmpxchg(x| f,&_please_self_suspend,x) ) 
x=_please_self_suspend;
    set_jvm_lock_poll_advisory();
  }
  void set_suspend_request_no_polling( enum SuspendFlags f ) { 
    jint x = _please_self_suspend;
    while( x != Atomic::cmpxchg(x| f,&_please_self_suspend,x) ) 
x=_please_self_suspend;
    assert0( jvm_locked_by_VM() );
  }

  void clr_suspend_request( enum SuspendFlags f ) { 
    jint x = _please_self_suspend;
    while( x != Atomic::cmpxchg(x&~f,&_please_self_suspend,x) ) 
x=_please_self_suspend;
  }

  // Atomically clear the flag.  If it's already clear, return false.
  // Only return true if we did the 1->0 transition.
  bool test_clr_suspend_request( enum SuspendFlags f ) { 
    jint x = _please_self_suspend;
    while( 1 ) {
      if( (x&f)==0 ) return false; // Somebody else cleared it
      if( x == Atomic::cmpxchg(x&~f,&_please_self_suspend,x) ) 
        return true;            // We cleared it
      x = _please_self_suspend; // Contention, retry
    }
  }

private:

  // Thread state is greatly simplified.  Each Java thread either holds it's
  // own _jvm_lock (preventing GC and other operations only safe at
  // Safepoints) or the VM thread owns the Java thread's _jvm_lock (or briefly
  // nobody holds it).  When a Thread holds the _jvm_lock it is allowed to
  // directly access oops; otherwise it must not touch oops (GC in progress).
  // As part of releasing the _jvm_lock a Thread stores it's own SP into the
  // _jvm_lock.  However, it's stack is not walkable until a register window
  // flush happens.  Threads will do this if they see a suspension request
  // (under the assumption that cross-thread flush requests are expensive),
  // but threads blocked in native calls may not do this for a long time.
  // There is a pd-specific flag for indicating if register window flushing
  // has been performed.
  // 
  // A running thread owns its _jvm_lock if the _jvm_lock is NULL.  It
  // releases the lock by setting it the last-Java-SP value.  The VM thread
  // may take the free lock by setting the low order bit.  Running threads
  // always release the lock before entering native code.  Locking requires a
  // CAS attempt, unlocking is just a non-fenced word write.  The lock can be
  // free with bit 2 set; this indicates that the taker should poll (and clear
  // the bit) before taking the lock.

  volatile jlong _jvm_lock;

public:
  // JVM lock queries: atomically read 1 word
  bool jvm_locked_by_self() const { return _jvm_lock == 0; }
  bool jvm_locked_by_VM  () const { return (_jvm_lock&1) == 1; }
  bool jvm_lock_is_free  () const { intptr_t x = _jvm_lock; return (x != 0) && ((x&1) == 0); }
  bool jvm_lock_is_free_and_poll() const { return (_jvm_lock&2) == 2; }
  // JVM lock updates: atomically read/modify/write one word
  bool jvm_lock_update_attempt(jlong exchange_value, jlong compare_value) { 
    return compare_value == Atomic::cmpxchg(exchange_value,&_jvm_lock,compare_value);
  }
  // Attempt to take the JVM lock for self.  Will fail if contended, or the
  // "polling advisory" bit is set.
  bool jvm_lock_self_attempt() {
assert(Thread::current()==this,"can only self-lock");
    // Verify the jvm_lock is obeying the general lock ranking scheme
    assert0( !_owned_locks || _owned_locks->_rank > AzLock::JVMLOCK_rank );

    // Inline one fast attempt to take the lock.  Note that the asserts are
    // inlined so I only get 1 read of the _jvm_lock field.
    intptr_t x = _jvm_lock;
    _anchor.set_last_Java_sp(x & ~3);
    // Note that we hope to find both low order bits cleared.  If the polling
    // advisory bit is set, the CAS will fail and we will go slow and poll.
    bool result = jvm_lock_update_attempt(0,x & ~3);
    Atomic::read_barrier();     // BARRIER under assumption we've won
    return result;
  }
  // Attempt to take the JVM lock for the VM
  bool jvm_lock_VM_attempt() {
    // Inline one fast attempt to take the lock.  Note that the asserts are
    // inlined so I only get 1 read of the _jvm_lock field.
    intptr_t x = _jvm_lock;
    // We cannot assert on the lock already being locked by the VM
    // because self-suspending JavaThreads will lock-by-VM on behalf
    // of the VM thread so the locked-by-VM state is racing.
    if( (x&1) == 1 ) return false; // Already locked; fail
    if( x==0 ) return false;	// Too late, other guy got it
    // No asserts on the _anchor; _last_Java_sp is expected to be junk here,
    // but the JavaThread can take the lock and set a valid _anchor.
    bool result = jvm_lock_update_attempt(x|1,x);
    Atomic::read_barrier();     // BARRIER under assumption we've won
    return result;
  }
  // JVM unlock self.  Non-blocking, non-failing.  It's fast!
  void jvm_unlock_self( ) {
assert(jvm_locked_by_self(),"better already own the lock!");
    // Verify the jvm_lock is obeying the general lock ranking scheme
    // drat - unlock code in systemDictionary.cpp breaks this invariant
    //assert0( !_owned_locks || _owned_locks->_rank > AzLock::JVMLOCK_rank );

    // need to have self writes be visible to a remote thread.
    Atomic::write_barrier();

assert(_allow_safepoint_count==0,"Possible safepoint reached by thread that does not allow it");

    _jvm_lock = _anchor.last_Java_sp_raw();
    // Since I just unlocked, the VM thread can take the lock before the zap
    // below, so the VM attempts cannot assert on seeing a zapped l.J.sp.
    _anchor.zap_last_Java_sp();	// Expect it to be junk now
  }
  // JVM unlock by VM.  Non-blocking, non-failing.  It's fast!  Leave bit 2
  // set while free'ing, to indicate that we should poll the self-suspend
  // flags before retaking the lock.  Setting this bit will prevent a
  // self-lock CAS from succeeding and the slow-path will do a poll.
  void jvm_unlock_VM() {
assert(jvm_locked_by_VM(),"better already own the lock!");
    // Since the VM thread is about to unlock, the Java thread can take the
    // lock instantly afterwards and can expect to set a valid _anchor.
    //_anchor.zap_last_Java_sp();	// Expect it to be junk now
    // need to have VM writes to a remote thread be visible.
    Atomic::write_barrier();
    // PAS (bit 2) must ONLY be cleared in ONE place, and only by the 'self'
    // thread.  The VM thread can monotonically set it, but never clear it.
    // A VM unlock needs to CAS the unlocked value down, because the 'self'
    // thread maybe racing to be setting the PAS bit. (Azul bug: 12113)
    intptr_t l = _jvm_lock;
    intptr_t pss = _please_self_suspend;
    while( !jvm_lock_update_attempt((l & ~1) | (pss ? 2:0), l)) {
      l = _jvm_lock;
      pss = _please_self_suspend;
    }
  }
  
  // Grab the jvm-lock for self, or suspend in the attempt.  Because of race
  // conditions it is possible to make it out of here with a self-suspend
  // request pending.  This just means we barely made it out "under the wire"
  // and we get to run a bit before we poll and suspend later.
  bool jvm_lock_self_or_suspend();
  // Same as prior function, but called by generated code.  MUST NOT INLINED
  // IN THE HPP FILE!  The system requires that a direct compare of function
  // pointers be able to find this, and inlining makes multiple equivalent
  // (but not equal) versions.
  static void jvm_lock_self_or_suspend_static_nat( JavaThread *t );
  static void jvm_lock_self_or_suspend_static_irq( JavaThread *t );

  // Take the JVM lock (so the we can handle naked oops) or block in the
  // attempt.  Small to allow inlining.
  bool jvm_lock_self() {
    if (!jvm_lock_self_attempt()) { // One fast check or
      return jvm_lock_self_or_suspend();  // Go slow (might as well poll also)
    } else {
      return true;
    }
  }

  // Poll for any funny conditions at a safepoint.
  static void poll_at_safepoint_static( JavaThread * );
  void poll_at_safepoint() {
assert(_allow_safepoint_count==0,"Possible safepoint reached by thread that does not allow it");
    if( please_self_suspend() ) { // Inline 1 check
      jvm_unlock_self();	  // Release jvm lock while checking conditions
      jvm_lock_self_or_suspend(); // Poll & handle funny conditions, then relock
    }
  }

  // Non-atomically with any polled suspend bit, set the jvm_lock
  // polling advisory bit.  Give up if _jvm_lock is NULL (owned by self).
  bool set_jvm_lock_poll_advisory() {
    // Needed I believe on azul hw; Sparc relies on TSO here.  What can happen is
    // some thread attempts to remotely suspend another thread (J/L/Thread
    // Suspend).  After setting some suspend polling bit, the
    // remote thread uses wait_for_suspend_completion() which calls here.  The
    // membar() forces the write of the please_self_suspend bit before the
    // polling advisory bit.  Otherwise it would be possible for the remote
    // thread to witness the PAS bit first, clear it and then not see the
    // suspend bit until some time later, all the while having his _jvm_lock
    // being free (e.g. while waking from a native or GC).  Meanwhile the
    // suspender thread thinks he "got" the remote thread: after all the
    // suspender has set the PSS and PAS bits and the remote thread's JVM lock
    // is free.
    Atomic::membar();
    intptr_t l = _jvm_lock;	// Read once
    while( l != 0 &&	        // While not locked by self (cannot set advisory bit)
	   !jvm_lock_update_attempt(l|2,l) )
      l = _jvm_lock;		// Spin and retry
    pd_set_hardware_poll_at_safepoint();
    return l != 0; // success ?
  }

  bool wait_for_suspend_completion(bool retry = true);

private:
  // !!! Java Frame Anchor !!!
  //
  // It is private on purpose!  You cannot peek at it remotely unless the
  // _jvm_lock is not jvm_locked_by_self() - when it's locked by self the
  // Thread can be free running in Java code with a continously changing
  // _last_Java_sp.  The current thread can crawl his own stack anytime but
  // not a remote thread.  I assert for this when you ask for JavaFrameAnchor
  // functions from the thread.
  JavaFrameAnchor _anchor; // Encapsulation of current java frame and it state
public: 
  // last_Java_sp 
  void verify_anchor() const { debug_only(_anchor.verify();) }
  void zap_last_Java_sp() { debug_only(_anchor.zap_last_Java_sp();) }
  void zap_anchor() { debug_only(_anchor.zap();) }
  bool root_Java_frame() const { 
    intptr_t x = _jvm_lock;	// Read only once
    if( x == 0 ) return _anchor.root_Java_frame();
    return _anchor.clr_walkable(x&~3) == JavaFrameAnchor::no_prior_java_frame;
  }
  void copy_anchor_out( JavaFrameAnchor &a ) const { a = _anchor; /*structure copy*/}
  void copy_anchor_in ( JavaFrameAnchor &a )       { _anchor = a; /*structure copy*/}
  intptr_t* make_stack_coherent(); // Make stack coherent, but don't pauseless GC clean it 
  intptr_t* last_Java_sp();        // Make stack coherent and pauseless GC clean so it can be walked
  address last_Java_pc() const { return _anchor.last_Java_pc(); }
  void modified_rstack() { set_suspend_request(rstak_suspend); }

  // Accessing frames
  frame last_frame() {   
assert(!root_Java_frame(),"we are at stack root, there IS no prior Java frame");
    return _anchor.pd_last_frame(last_Java_sp()); }
  vframe last_java_vframe();

  JNIEnv        _jni_environment;
  uint64_t      _proxy_jni_environment;  // is a ProxyEnv* on the proxy side
  // Because deoptimization is lazy we must save jvmti requests to set locals
  // in compiled frames until we deoptimize and we have an interpreter frame.
  // This holds the pointer to array (yeah like there might be more than one) of
  // description of compiled vframes that have locals that need to be updated.
  GrowableArray<jvmtiDeferredLocalVariableSet*>* _deferred_locals_updates;

  // JavaThread termination support
  enum TerminatedTypes {
    _not_terminated = 0xDEAD - 2,
    _thread_exiting,                             // JavaThread::exit() has been called for this thread
    _thread_terminated,                          // JavaThread is removed from thread list
    _vm_exited                                   // JavaThread is still executing native code, but VM is terminated
                                                 // only VM_Exit can set _vm_exited
  };

  // In general a JavaThread's _terminated field transitions as follows:
  //
  //   _not_terminated => _thread_exiting => _thread_terminated
  //
  // _vm_exited is a special value to cover the case of a JavaThread
  // executing native code after the VM itself is terminated.
  TerminatedTypes       _terminated;

  volatile bool         _doing_unsafe_access;    // Thread may fault due to unsafe access

  //  Flag to mark a JNI thread in the process of attaching - See CR 6404306
  //  This flag is never set true other than at construction, and in that case
  //  is shortly thereafter set false
  volatile bool _is_attaching;

  // Minimum Mutator Utilization
  MMU* mmu()       const { return _javaThreadSpillover->_mmu; }
  void set_mmu(MMU* val) { _javaThreadSpillover->_mmu = val; }
  void mmu_start_pause() { if (mmu() != NULL) { mmu()->start_pause(); } }
  void mmu_end_pause()   { if (mmu() != NULL) { mmu()->end_pause  (); } }

 public:

 private:

  // support for JNI critical regions
  jint    _jni_active_critical;                  // count of entries into JNI critical region

  void initialize();                             // Initialized the instance variables

 public:
  // Constructor
  JavaThread(bool is_attaching = false); // for main thread and JNI attached threads
  JavaThread(ThreadFunction entry_point, size_t stack_size = 0); 
  ~JavaThread();

#ifdef ASSERT
  // verify this JavaThread hasn't be published in the Threads::list yet
  void verify_not_published();
#endif

  //JNI functiontable getter/setter for JVMTI jni function table interception API.
  void set_jni_functions(struct JNINativeInterface_* functionTable) {
    _jni_environment.functions = functionTable;
  }
  struct JNINativeInterface_* get_jni_functions() {
    return (struct JNINativeInterface_ *)_jni_environment.functions;
  }

  // Executes Shutdown.shutdown()
  void invoke_shutdown_hooks();

  // Cleanup on thread exit
  enum ExitType {
    normal_exit,
    jni_detach
  };
  void exit(bool destroy_vm, ExitType exit_type = normal_exit);

  void cleanup_failed_attach_current_thread();

  // Testers
  virtual bool is_Java_thread() const            { return true;  }

private:
  const char *_hint_blocked; // Reason why this JavaThread is blocked
  jlong       _time_blocked; // Time at which this thread was blocked
  // Fields to track last java/lang/concurrency/locks requests
  objectRef   _hint_block_concurrency_msg;  // A j.l.String
  objectRef   _hint_block_concurrency_lock; // A j.u.c.locks.Lock
  objectRef   _hint_block_concurrency_sync; // A j.u.c.locks.Lock.Sync

  // Set while ARTA is creating a thread list. As we're building a lock-acquire
  // tree we cache references to already constructed nodes to avoid look-ups.
  ArtaThreadState *_arta_thread_state;

public:  
  void self_suspend();		// Hang on the self-suspend monitor until notified
  // Hint that we are about to hang on a lock.  Called whenever we believe the
  // thread is about to stall for a long time.
  void hint_blocked(const char *hint) {
assert(!jvm_locked_by_self(),"need to unlock before blocking");
    make_stack_coherent();  // It's more efficient for a thread to make it's own stack coherent.
    set_time_blocked();
    _hint_blocked = hint;
  }
  // Hint that we are done blocking and are free running again
  void hint_unblocked() {
    _hint_blocked = "running";
    _time_blocked = 0;
  }
  bool is_hint_blocked(  const char *s) const { return strcmp(s,_hint_blocked)==0; }
  bool is_hint_blocked() const { return !is_hint_blocked("running"); }
  bool is_hint_locked () const { return _hint_blocked[0]=='a'; }
  bool is_hint_waiting() const { return _hint_blocked[0]=='w'; }
  inline const char *get_hint_blocked() const { return _hint_blocked; }
  inline void set_time_blocked() { _time_blocked = os::elapsed_counter(); }

  // Fields to track last java/lang/concurrency/locks requests
objectRef hint_blocking_concurrency_msg()const;
void set_hint_blocking_concurrency_msg(objectRef);
objectRef hint_blocking_concurrency_lock()const;
void set_hint_blocking_concurrency_lock(objectRef);
objectRef hint_blocking_concurrency_sync()const;
void set_hint_blocking_concurrency_sync(objectRef);

  // Cached ARTA thread state used while creating thread lists.
  ArtaThreadState* arta_thread_state() { return _arta_thread_state; }
  void set_arta_thread_state(ArtaThreadState *state) { _arta_thread_state = state; }

  // Release the jvm_lock and sleep (interruptably).  Return os::sleep's result.
int sleep(jlong millis);

  // Lift a self-suspend request.  Either it's a java.lang.Thread.resume
  // (which cancels all pending java.lang.Thread.suspends) or it's a
  // jvmti-style resume, which only cancels a jvmti-style suspend (and does
  // not nest, but jvmti enforces that).
  void java_resume( enum SuspendFlags f );

  // Thread chain operations
  JavaThread* next() const                { return _next; }
  void set_next(JavaThread* p)            { _next = p; }
  
  // Thread oop. threadObj() can be NULL for initial JavaThread
  // (or for threads attached via JNI)
oop threadObj()const;
void set_threadObj(oop p);
//Not-safe-for-debugging version.  LVBs in debug printouts frequently die.
//Used for JVM_CurrentThread calls.
  objectRef threadRef() const { return lvb_ref(&_threadObj); }

private:
  OSThreadPriority _current_priority;

public:
  OSThreadPriority current_priority() { return _current_priority; }

  void set_priority   (JavaThreadPriority priority);
  void set_os_priority(OSThreadPriority priority);

JavaThreadPriority java_priority()const;//Read from threadObj()

  // Prepare thread and add to priority queue, using the priority of the
  // thread object. Threads_lock must be held while this function is called.
  void prepare(Handle thread_oop);

  ThreadFunction entry_point() const      { return _javaThreadSpillover->_entry_point; }

  // Allocates a new Java level thread object for this thread. thread_name may be NULL.
void allocate_threadObj(Handle thread_group,const char*thread_name,bool daemon,TRAPS);

//Retained for use in classFileParser.cpp
bool has_last_Java_frame()const{return false;}

//Thread.stop and asynchronous exception support
private:
  objectRef _pending_async_exception;	// Installs into pending_exception when polled
public:
  // Installs a pending asynchronous exception to be inspected when polled
  // exception must be heap allocated
  void install_async_exception(objectRef throwable, bool overwrite_prior, bool remote);
  // Installs a pending unsafe exception
  void set_pending_unsafe_access_error(); // unsafe code took a hit
  // Installs a pending stack overflow exception
  void set_pending_stack_overflow_error();
  // Move a pending async exception (at a polling point) into the pending
  // exception field.  May require a deoptimization.
  void move_pending_async_to_pending();
  Chunk *_deopt_buffer;         // Deopt support
    

  void update_pending_exception(oop exception) { 
    assert0(_pending_exception.is_in_a_stack());
    _pending_exception = objectRef(exception); 
    set_jvm_lock_poll_advisory();
  } 
  void update_pending_exception(objectRef exception) { 
    assert0(_pending_exception.is_in_a_stack());
    _pending_exception = exception; 
    set_jvm_lock_poll_advisory();
  } 
  // thread has called JavaThread::exit() or is terminated
  bool is_exiting()                              { return _terminated == _thread_exiting || is_terminated(); }
  // thread is terminated (no longer on the threads list); we compare
  // against the two non-terminated values so that a freed JavaThread
  // will also be considered terminated.
  bool is_terminated()                           { return _terminated != _not_terminated && _terminated != _thread_exiting; }
  void set_terminated(TerminatedTypes t)         { _terminated = t; }
  // special for Threads::remove() which is static:
  void set_terminated_value()                    { _terminated = _thread_terminated; }
  void block_if_vm_exited();

  bool doing_unsafe_access()                     { return _doing_unsafe_access; }
  void set_doing_unsafe_access(bool val)         { _doing_unsafe_access = val; }
    
  // Side structure for defering update of java frame locals until deopt occurs
  GrowableArray<jvmtiDeferredLocalVariableSet*>* deferred_locals() const { return _deferred_locals_updates; }
  void set_deferred_locals(GrowableArray<jvmtiDeferredLocalVariableSet *>* vf) { _deferred_locals_updates = vf; }


  // Stack overflow support
inline size_t stack_available(intptr_t cur_sp);

  // Attempt to reguard the stack after a stack overflow may have occurred.
  // Returns true if it was possible to "reguard" (i.e. unmap both the user
  // and expression stack yellow zones) because neither stack's usage was
  // still inside those areas, or if the stack was already guarded.
  // Returns false if the user stack usage was within 0x1000 bytes of the
  // user stack yellow zone, or if the expression stack usage was still 
  // within the expression stack yellow zone, in which case the caller 
  // should unwind a frame and try again.
  bool stack_is_good();

  // For assembly stub generation
  static ByteSize threadObj_offset()             { return byte_offset_of(JavaThread, _threadObj           ); }
  static ByteSize jni_environment_offset()       { return byte_offset_of(JavaThread, _jni_environment     ); }
static ByteSize proxy_jni_environment_offset(){return byte_offset_of(JavaThread,_proxy_jni_environment);}

  static ByteSize last_Java_sp_offset()          {
    return byte_offset_of(JavaThread, _anchor) + JavaFrameAnchor::last_Java_sp_offset();
  }
  static ByteSize last_Java_pc_offset()          {
    return byte_offset_of(JavaThread, _anchor) + JavaFrameAnchor::last_Java_pc_offset();
  }
  static ByteSize frame_anchor_offset()          {
    return byte_offset_of(JavaThread, _anchor);
  }
static ByteSize jvm_lock_offset(){return byte_offset_of(JavaThread,_jvm_lock);}
static ByteSize please_self_suspend_offset(){return byte_offset_of(JavaThread,_please_self_suspend);}
  static ByteSize osthread_offset()              { return byte_offset_of(JavaThread, _osthread            ); }

static ByteSize time_block_offset(){return byte_offset_of(JavaThread,_time_blocked);}
static ByteSize hint_block_msg_offset(){return byte_offset_of(JavaThread,_hint_block_concurrency_msg);}
static ByteSize hint_block_lock_offset(){return byte_offset_of(JavaThread,_hint_block_concurrency_lock);}
static ByteSize hint_block_sync_offset(){return byte_offset_of(JavaThread,_hint_block_concurrency_sync);}

  // Returns the jni environment for this thread
  JNIEnv* jni_environment()                      { return &_jni_environment; }

  static JavaThread* thread_from_jni_environment(JNIEnv* env) {
    JavaThread *thread_from_jni_env = (JavaThread*)((intptr_t)env - in_bytes(jni_environment_offset()));
    // Only return NULL if thread is off the thread list; starting to
    // exit should not return NULL.
    if (thread_from_jni_env->is_terminated()) {
       thread_from_jni_env->block_if_vm_exited();
       return NULL;
    } else {
       return thread_from_jni_env;
    }
  }

  uint64_t proxy_jni_environment()                { return _proxy_jni_environment; }
  void set_proxy_jni_environment(uint64_t penv)   { _proxy_jni_environment = penv; }

  // Compute depth-first-numbering on lock owners and count of blocked threads
  ushort _num;                  // Dense linear numbering of threads

  // JNI critical regions. These can nest.
  bool in_critical()    { return _jni_active_critical > 0; }
  void enter_critical() { assert(Thread::current() == this,
                                 "this must be current thread");
                          _jni_active_critical++; }
  void exit_critical()  { assert(Thread::current() == this,
                                 "this must be current thread");
                          _jni_active_critical--; 
                          assert(_jni_active_critical >= 0,
                                 "JNI critical nesting problem?"); }

  // For deadlock detection
int depth_first_number(){return _javaThreadSpillover->_depth_first_number;}
void set_depth_first_number(int dfn){_javaThreadSpillover->_depth_first_number=dfn;}

 private:
void set_entry_point(ThreadFunction entry_point){_javaThreadSpillover->_entry_point=entry_point;}

 public:
     
  // Frame iteration; calls the function f for all frames on the stack 
  void frames_do(void f(JavaThread *thsi,frame*));
  
  // Memory operations: crawl all heap roots
  void oops_do(OopClosure* f);
void oops_do_impl(OopClosure*f);

  // Crawl both stack and heap roots.
  void oops_do_stackok(SbaClosure* f, int limit_fid);

  // Sweeper operations
  void methodCodes_do();

  // Memory management operations
  void gc_epilogue();
  void gc_prologue();
void GPGC_gc_prologue(OopClosure*f);
void GPGC_mutator_gc_prologue(OopClosure*f);

  // Count times this oop is locked on the stack.  Generally used to
  // inflate a biased-lock into a real monitor.
  int count_locks( oop );
  ObjectMonitor *_unbias;       // List of ObjectMonitors needing unbiasing
  const ObjectLocker * _objectLockerList; // list of java-locks held by the VM
  void unbias_one(ObjectMonitor*, const char *msg);// unbias one monitor
  void unbias_all();            // unbias all things on the _unbias list
  
  // Misc. operations
  void ps(int depth=9999) PRODUCT_RETURN; // Print thread stack with intelligence
  char* name() const { return (char*)get_thread_name(); }
void print_on(outputStream*st);
  void print() { print_on(tty); }
  void print_xml_on(xmlBuffer *xb, int detail, bool dostate = true);
  virtual const char *status_msg()   const { return get_hint_blocked(); }
  virtual jlong       time_blocked() const { return _time_blocked; }
  void print_value();
void print_thread_state_on(outputStream*os)const;
void print_thread_state()const;
  void print_on_error(outputStream* st, char* buf, int buflen) const;
  void verify();
  const char* get_thread_name() const;
private:
  // factor out low-level mechanics for use in both normal and error cases
  const char* get_thread_name_string(char* buf = NULL, int buflen = 0) const;
public:
  const char* get_threadgroup_name() const;
  const char* get_parent_name() const;

  // Returns method at 'depth' java or native frames down the stack  
  // Used for security checks
  klassOop security_get_caller_class(int depth);  

  // Print stack trace in external format
  void print_stack_on(outputStream* st);
  void print_stack() { print_stack_on(tty); }

  // Print stack traces in various internal formats
  void trace_stack()                             PRODUCT_RETURN;
void trace_stack_from(frame fr)PRODUCT_RETURN;
  void trace_frames()                            PRODUCT_RETURN;

  // Returns the number of stack frames on the stack
  int depth() const;

  bool profile_last_Java_frame(frame* fr);

 public:

  // Static operations
 public:
  // Returns the running thread as a JavaThread
  static inline JavaThread* current();

  // Returns the active Java thread.  Do not use this if you know you are calling
  // from a JavaThread, as it's slower than JavaThread::current.  If called from
  // the VMThread, it also returns the JavaThread that instigated the VMThread's
  // operation.  You may not want that either.
  static JavaThread* active();

 public:
  virtual void run();
  void thread_main_inner();

 private:
  // PRIVILEGED STACK
  PrivilegedElement*  _privileged_stack_top;
 public:
    
  // Returns the privileged_stack information.
  PrivilegedElement* privileged_stack_top() const       { return _privileged_stack_top; }
  void set_privileged_stack_top(PrivilegedElement *e)   { _privileged_stack_top = e; }

 public:
  // Thread local information maintained by JVMTI. 
  void set_jvmti_thread_state(JvmtiThreadState *value)                           { _jvmti_thread_state = value; }
  JvmtiThreadState *jvmti_thread_state() const                                   { return _jvmti_thread_state; }
  static ByteSize jvmti_thread_state_offset()                                    { return byte_offset_of(JavaThread, _jvmti_thread_state); }
  void set_jvmti_get_loaded_classes_closure(JvmtiGetLoadedClassesClosure* value) { _jvmti_get_loaded_classes_closure = value; }
  JvmtiGetLoadedClassesClosure* get_jvmti_get_loaded_classes_closure() const     { return _jvmti_get_loaded_classes_closure; }

  // JVMTI PopFrame support
 private:
  // This is set to popframe_pending to signal that top Java frame should be popped immediately
  int _popframe_condition;

 public:
  // Setting and clearing popframe_condition
  // All of these enumerated values are bits. popframe_pending
  // indicates that a PopFrame() has been requested and not yet been
  // completed. popframe_processing indicates that that PopFrame() is in
  // the process of being completed. popframe_force_deopt_reexecution_bit
  // indicates that special handling is required when returning to a
  // deoptimized caller.
  enum PopCondition {
    popframe_inactive                      = 0x00,
    popframe_pending_bit                   = 0x01,
    popframe_processing_bit                = 0x02,
    popframe_force_deopt_reexecution_bit   = 0x04
  };
  PopCondition popframe_condition()                   { return (PopCondition) _popframe_condition; }
  void set_popframe_condition(PopCondition c)         { _popframe_condition = c; }
  void set_popframe_condition_bit(PopCondition c)     { _popframe_condition |= c; }
  void clear_popframe_condition()                     { _popframe_condition = popframe_inactive; }
  static ByteSize popframe_condition_offset()         { return byte_offset_of(JavaThread, _popframe_condition); }
  bool has_pending_popframe()                         { return (popframe_condition() & popframe_pending_bit) != 0; }
  bool popframe_forcing_deopt_reexecution()           { return (popframe_condition() & popframe_force_deopt_reexecution_bit) != 0; }
  void clear_popframe_forcing_deopt_reexecution()     { _popframe_condition &= ~popframe_force_deopt_reexecution_bit; }

 private:
  // Saved incoming arguments to popped frame.
  // Used only when popped interpreted frame returns to deoptimized frame.
  int      _popframe_preserved_args_size;
  void*    _popframe_preserved_args;

 public:
  void  popframe_preserve_args(ByteSize size_in_bytes, void* start);
  void* popframe_preserved_args();
  ByteSize popframe_preserved_args_size();
  WordSize popframe_preserved_args_size_in_words();
  void  popframe_free_preserved_args();

 private:
  JvmtiThreadState *_jvmti_thread_state;
  JvmtiGetLoadedClassesClosure* _jvmti_get_loaded_classes_closure;

  // Used by the interpreter in fullspeed mode for frame pop, method
  // entry, method exit and single stepping support. This field is
  // only set to non-zero by the VM_EnterInterpOnlyMode VM operation.
  // It can be set to zero asynchronously (i.e., without a VM operation
  // or a lock) so we have to be very careful.
  int               _interp_only_mode;

 public:
  // used by the interpreter for fullspeed debugging support (see above)
  static ByteSize interp_only_mode_offset() { return byte_offset_of(JavaThread, _interp_only_mode); }
  bool is_interp_only_mode()                { return (_interp_only_mode != 0); }
  int get_interp_only_mode()                { return _interp_only_mode; }
  void increment_interp_only_mode()         { ++_interp_only_mode; }
  void decrement_interp_only_mode()         { --_interp_only_mode; }
 private:
  ThreadStatistics *_thread_stat;

 public:
  ThreadStatistics* get_thread_stat() const    { return _thread_stat; }

  // Return a blocker object for which this thread is blocked parking. 
  oop current_park_blocker();

 private:
  static size_t _stack_size_at_create;
 
 public:
  static inline size_t stack_size_at_create(void) { 
    return _stack_size_at_create; 
  }
  static inline void set_stack_size_at_create(size_t value) { 
    _stack_size_at_create = value;
  }
  
  // Machine dependent stuff
#include "thread_pd.hpp"
#include "thread_os_pd.hpp"

  // --- SBA ---
  // Fast access to a few fields.
  stackRef  _sba_top; // Standard top for SBA allocations, includes FID and stack space-id bits
  stackRef  _sba_max; // Standard max for SBA allocations, includes FID and stack space-id bits
  int       _curr_fid;// Current Frame-ID
static ByteSize sba_max_offset(){return byte_offset_of(JavaThread,_sba_max);}
static ByteSize sba_top_offset(){return byte_offset_of(JavaThread,_sba_top);}
static ByteSize curr_fid_offset(){return byte_offset_of(JavaThread,_curr_fid);}
  address sba_top_adr() const { return _sba_top.as_address(this); }
  address sba_max_adr() const { return _sba_max.as_address(this); }
  int sba_used() const { return sba_top_adr() - sba_area()->_start; }
  SBAArea* sba_area() const { return _javaThreadSpillover->_sba_area; } 
  void set_sba(SBAArea *sba)  { _javaThreadSpillover->_sba_area = sba; set_abase(this); }
  // Check self-thread containment
  bool sba_is_in          (address adr) const { return sba_area() ? sba_area()->is_in(adr,_sba_top.as_address(this)) : false; }
  bool sba_is_in_or_oldgen(address adr) const { return sba_area() ? sba_area()->is_in_or_oldgen(adr                ) : false; }
  // Check active-thread containment
  static bool sba_is_in_current(address adr) { 
    JavaThread *jt = Thread::current()->sba_thread();
    return jt && jt->sba_is_in(adr);
  }
  // Convenience to get the current fid
  int curr_sbafid() const { return _curr_fid; }

  // JSR166 per-thread parker
private:
  Parker*    _parker;
public:
  Parker*     parker() { return _parker; }

  // clearing/querying jni attach status
  bool is_attaching() const { return _is_attaching; }
  void set_attached() { _is_attaching = false; OrderAccess::fence(); }

  // for finalizer threads / resource limit tracking.
public:
  void set_finalizer_thread(bool flag) { _javaThreadSpillover->_is_finalizer_thread = flag; }
  bool is_finalizer_thread() { return _javaThreadSpillover->_is_finalizer_thread; }

private:
  JavaThreadSpillover* _javaThreadSpillover;

#ifndef AT_PRODUCT
 private:
  AuditTrail* _audit_trail;
 public:
  AuditTrail* audit_trail()   { return _audit_trail; }
#else  // ! PRODUCT
 public:
  AuditTrail* audit_trail()   { return NULL; }
#endif // ! PRODUCT

 public:
  uint64_t get_safepoint_pc()            const { return _javaThreadSpillover->_safepoint_pc; }
  void set_safepoint_pc(uint64_t x)            { _javaThreadSpillover->_safepoint_pc = x; }
  uint64_t get_safepoint_rpc()           const { return _javaThreadSpillover->_safepoint_rpc; }
  void set_safepoint_rpc(uint64_t x)           { _javaThreadSpillover->_safepoint_rpc = x; }
  uint64_t get_safepoint_total()         const { return _javaThreadSpillover->_safepoint_total; }
  void set_safepoint_total(uint64_t x)         { _javaThreadSpillover->_safepoint_total = x; }
  void inc_safepoint_total(uint64_t x)         { _javaThreadSpillover->_safepoint_total += x; }
  uint64_t get_safepoint_count()         const { return _javaThreadSpillover->_safepoint_count; }
  void set_safepoint_count(uint64_t x)         { _javaThreadSpillover->_safepoint_count = x; }
  void inc_safepoint_count()                   { _javaThreadSpillover->_safepoint_count++; }
  uint64_t get_safepoint_min()           const { return _javaThreadSpillover->_safepoint_min; }
  void set_safepoint_min(uint64_t x)           { _javaThreadSpillover->_safepoint_min = x; }
  uint64_t get_safepoint_max()           const { return _javaThreadSpillover->_safepoint_max; }
  uint64_t get_safepoint_max_when()      const { return _javaThreadSpillover->_safepoint_max_when; }
  void set_safepoint_max_when(uint64_t x)      { _javaThreadSpillover->_safepoint_max_when = x; }
  void set_safepoint_max(uint64_t x)           { _javaThreadSpillover->_safepoint_max = x; }
  uint64_t get_safepoint_max_pc()        const { return _javaThreadSpillover->_safepoint_max_pc; }
  void set_safepoint_max_pc(uint64_t x)        { _javaThreadSpillover->_safepoint_max_pc = x; }
  uint64_t get_safepoint_max_rpc()       const { return _javaThreadSpillover->_safepoint_max_rpc; }
  void set_safepoint_max_rpc(uint64_t x)       { _javaThreadSpillover->_safepoint_max_rpc = x; }

  inline void reset_safepoint_stats() {
    set_safepoint_start(0);
    set_safepoint_end(0);
    set_safepoint_pc(0);
    set_safepoint_rpc(0);
    set_safepoint_total(0);
    set_safepoint_count(0);
    set_safepoint_min(0);
    set_safepoint_max(0);
    set_safepoint_max_when(0);
    set_safepoint_max_pc(0);
    set_safepoint_max_rpc(0);
  }

  jlong ttsp_tick_time()                { return _javaThreadSpillover->_ttsp_tick_time; }
  void  set_ttsp_tick_time(jlong ticks) { _javaThreadSpillover->_ttsp_tick_time = ticks; }
  void* ttsp_profile_entry()            { return _javaThreadSpillover->_ttsp_profile_entry; }

 public:
  jlong  get_obj_zero_max_ticks()                           const { return _javaThreadSpillover->_obj_zero_max_ticks;        }
  size_t get_obj_zero_max_tick_words()                      const { return _javaThreadSpillover->_obj_zero_max_tick_words;   }
  void   set_obj_zero_max_ticks(jlong ticks, size_t words)        { _javaThreadSpillover->_obj_zero_max_ticks = ticks;
                                                                    _javaThreadSpillover->_obj_zero_max_tick_words = words;  }

  jlong  get_obj_clone_max_ticks()                          const { return _javaThreadSpillover->_obj_clone_max_ticks;       }
  size_t get_obj_clone_max_tick_words()                     const { return _javaThreadSpillover->_obj_clone_max_tick_words;  }
  void   set_obj_clone_max_ticks(jlong ticks, size_t words)       { _javaThreadSpillover->_obj_clone_max_ticks = ticks;
                                                                    _javaThreadSpillover->_obj_clone_max_tick_words = words; }

  jlong  get_arraycopy_max_ticks()                          const { return _javaThreadSpillover->_arraycopy_max_ticks;       }
  size_t get_arraycopy_max_tick_words()                     const { return _javaThreadSpillover->_arraycopy_max_tick_words;  }
  void   set_arraycopy_max_ticks(jlong ticks, size_t words)       { _javaThreadSpillover->_arraycopy_max_ticks = ticks;
                                                                    _javaThreadSpillover->_arraycopy_max_tick_words = words; }

  inline void reset_init_clone_copy_stats() {
    set_obj_zero_max_ticks (0, 0);
    set_obj_clone_max_ticks(0, 0);
    set_arraycopy_max_ticks(0, 0);
  }
};


class InstructionTraceThread:public Thread{
 public:
InstructionTraceThread(int id);
  virtual ~InstructionTraceThread();
  virtual void run();

  // Printing
  void print();
  void print_xml_on(xmlBuffer *xb, bool ref);

  static void shutdown() { _should_terminate = true; }
  bool isShutdown() { return !_running; }

  bool is_ITR_thread() const                 { return true; }

 private:
  static bool _should_terminate;

  int   _id;
  gzFile _loggz;

  bool _running;

 public:
  static long getCount() { return _count; }

 private:
  static long _count;

 private:
  void recordTraces(int64_t threadID, const char* threadName, long* traces, int numTraces);

 public:
InstructionTraceThread*next()const{return _next;}
void setNext(InstructionTraceThread*next){_next=next;}

 public:
  int numOps;
  int numST2;
  int numST8;
  int numLD8;
  int numLVB;
  int numSVB;
  int numSpaceIDs0;
  int numSpaceIDs1;
  int numSpaceIDs2;
  int numSpaceIDs3;

 private:
InstructionTraceThread*_next;
};

class InstructionTraceThreads:AllStatic{
 public:
  static void initialize();

  static void pushInstructionTraceThread(InstructionTraceThread *srt);
  static InstructionTraceThread* popInstructionTraceThread();
  static InstructionTraceThread* instructionTraceThreadHead() { return _instructionTraceThreadHead; }

  static void stopAllThreads();

  static int getUniqueID();

  // If doTrace is NULL, all threads not in doNotTrace will be traced.
  // If doTrace is not NULL, all threads in doTrace and NOT in doNotTrace will be traced
  static FlatHashSet<const Thread*>* doTrace;
  static FlatHashSet<const Thread*>* doNotTrace;
static void noticeThread(Thread*thread);

 public:
  static jlong _numThreads;

 private:
  static InstructionTraceThread *_instructionTraceThreadHead;
  static int _uniqueID;

 public:
  // Debug variables
  static int totalNumOps;
  static int totalNumST2;
  static int totalNumST8;
  static int totalNumLD8;
  static int totalNumLVB;
  static int totalNumSVB;
  static int totalNumSpaceIDs0;
  static int totalNumSpaceIDs1;
  static int totalNumSpaceIDs2;
  static int totalNumSpaceIDs3;
};

// Inline implementation of JavaThread::current
inline JavaThread* JavaThread::current() { 
Thread*thread=Thread::current();
  assert(thread != NULL && thread->is_Java_thread(), "just checking");
  return (JavaThread*)thread;
}


// Returns the amount of space remaining between the stack address supplied,
// and the bottom of the user stack, excluding the yellow zone, if present.
inline size_t JavaThread::stack_available(intptr_t cur_sp) {
  return cur_sp - (intptr_t)_jexstk_top - thread_size_yellow_zone;
}


inline bool Thread::has_safep_suspend() {
  return is_Complete_Java_thread() &&
      ((((JavaThread*) this)->please_self_suspend() & JavaThread::safep_suspend) != 0);
}

// A JavaThread for low memory detection support
class LowMemoryDetectorThread : public JavaThread {
  friend class VMStructs;
public:
  LowMemoryDetectorThread(ThreadFunction entry_point) : JavaThread(entry_point) {};

  // Hide this thread from external view.
  bool is_hidden_from_external_view() const      { return true; }
};

// A thread used for Compilation.
class CompileBroker;
class CompileTask;
class CompilerThread : public JavaThread {
 public:
  int _tid;                     // unique compile thread identifier
  CompileTask*       _task;
  ciEnv*             _env;

  CompilerThread(int tid);
  bool is_Compiler_thread() const { return true; }
virtual bool is_hidden_from_external_view()const{return true;}
ciEnv*env()const{return _env;}
void set_env(ciEnv*e){_env=e;}
CompileTask*task()const{return _task;}
  virtual const char *status_msg()   const;
  virtual jlong       time_blocked() const { return 0; }
  virtual CompileBroker *cb() const = 0;
  static inline CompilerThread* current() {
Thread*thread=Thread::current();
    assert0(thread->is_Compiler_thread());
return(CompilerThread*)thread;
  }
};

#define ThreadLocals C1CompilerThread::current()->thread_locals()

class Compilation;
class C1CompilerThread : public CompilerThread {
private:
  C1ThreadLocals* _thread_locals;

public:
Compilation*_compile;

  C1CompilerThread(int tid) : CompilerThread(tid) { 
    _thread_locals = new C1ThreadLocals();
_compile=NULL;
  } 
  ~C1CompilerThread() {
    delete _thread_locals; _thread_locals=NULL;
_compile=NULL;
  }
  bool is_C1Compiler_thread() const { return true; }
  CompileBroker *cb() const;
  static inline C1CompilerThread* current() {
Thread*thread=Thread::current();
    assert0(thread->is_C1Compiler_thread());
return(C1CompilerThread*)thread;
  }
  void set_thread_locals(C1ThreadLocals* const thread_locals) { 
    _thread_locals = thread_locals; 
  }
  C1ThreadLocals* thread_locals() const { return _thread_locals; }
};

class Compile;                  // For controlling C2 compiles
class C2CompilerThread : public CompilerThread {
public:
Compile*_compile;
  C2CompilerThread(int tid) : CompilerThread(tid), _compile(NULL) { } 
  bool is_C2Compiler_thread() const { return true; }
  CompileBroker *cb() const;
  static inline C2CompilerThread* current() {
Thread*thread=Thread::current();
    assert0(thread->is_C2Compiler_thread());
return(C2CompilerThread*)thread;
  }
};



// The active thread queue. It also keeps track of the current used
// thread priorities.
class Threads: AllStatic {
 private:
  static JavaThread* _thread_list;
  static int         _number_of_threads;
  static int         _number_of_non_daemon_threads;
  static int         _return_code;

 public:
  // Thread management
  // force_daemon is a concession to JNI, where we may need to add a
  // thread to the thread list before allocating its thread object
static void add(JavaThread*p,bool force_daemon);
  static void remove(JavaThread* p);
  static bool includes(JavaThread* p);
  static JavaThread* first()                     { return _thread_list; }
  static void threads_do(ThreadClosure* tc);
  
  // Initializes the vm and creates the vm thread
  static jint create_vm(JavaVMInitArgs* args, bool* canTryAgain);
  static void convert_vm_init_libraries_to_agents();
  static void create_vm_init_libraries();
  static void create_vm_init_agents();
  static bool destroy_vm();
  // Supported VM versions via JNI
  // Includes JNI_VERSION_1_1
  static jboolean is_supported_jni_version_including_1_1(jint version);
  // Does not include JNI_VERSION_1_1
  static jboolean is_supported_jni_version(jint version);

  // Garbage collection
  static void follow_other_roots(void f(oop*));

  // Apply "f->do_oop" to all root oops in all threads.
  // This version may only be called by sequential code.
  static void oops_do(OopClosure* f);

  // This creates a list of GCTasks, one per thread.
  static void create_thread_roots_tasks(GCTaskQueue* q);

  // This creates a list of GCTasks, one per thread, for marking objects.
  static void create_thread_roots_marking_tasks(GCTaskQueue* q);


  static void convert_hcode_pointers();
  static void restore_hcode_pointers();

  // Sweeper
  static void methodCodes_do();

  static void gc_epilogue();
  static void gc_prologue();

  // Search all threads for SBA containment; return owning thread.
  // No grab of threads-lock, so may die.
  // Used for debugging only.
  static JavaThread *sba_find_owner(address adr);

  // Verification
  static void verify();
  static void print_on(outputStream* st, bool print_stacks, bool internal_format, bool print_concurrent_locks);
  static void print(bool print_stacks, bool internal_format) {
    // this function is only used by debug.cpp
    print_on(tty, print_stacks, internal_format, false /* no concurrent lock printed */);
  }
  static void print_on_error(outputStream* st, Thread* current, char* buf, int buflen);
  // detail is:
  // 0 for reference/link only, 
  // 1 for 1 line per method
  // 2 for 1 line per Java local/expression-stack
  // 3 for 1 line per word of physical frame 
  static void all_threads_print_xml_on(xmlBuffer *xb, int start, int stride, int detail, ThreadFilter *filt);
  static void thread_print_xml_on(int64_t thr_id, xmlBuffer *xb, int detail);
  static void thread_print_safepoint_xml_on(xmlBuffer *xb);
  static void threads_reset_safepoints();
  static void find_deadlocks(xmlBuffer* xb, GrowableArray<JavaThread*>* deadlock_threads);
  static void clear_arta_thread_states();

  static JavaThread* by_id_may_gc(int64_t thr_id);
  static const char* thread_name_may_gc(int64_t thr_id);

  // Tracing support
  static void ITR_write_thread_names();

  // Get Java threads that are waiting to enter a monitor. If doLock
  // is true, then Threads_lock is grabbed as needed. Otherwise, the
  // VM needs to be at a safepoint.
  static GrowableArray<JavaThread*>* get_pending_threads(int count,
    address monitor, bool doLock);

  // Get owning Java thread from the monitor's owner field. If doLock
  // is true, then Threads_lock is grabbed as needed. Otherwise, the
  // VM needs to be at a safepoint.
  static JavaThread *owning_thread_from_monitor_owner(address owner,
    bool doLock);

  // Number of threads on the active threads list
  static int number_of_threads()                 { return _number_of_threads; }
  // Number of non-daemon threads on the active threads list
  static int number_of_non_daemon_threads()      { return _number_of_non_daemon_threads; }
};

// Thread iterator
class ThreadClosure: public StackObj {
 public:
  virtual void do_thread(Thread* thread) = 0;
};

// JavaThread only iterator
class JavaThreadClosure:public StackObj{
 public:
  virtual void do_java_thread(JavaThread* jt) = 0;
};

class ThreadFilter:public StackObj{
public:
  static ThreadFilter NIL;

  static ThreadFilter* nil() {return &NIL;}

  ThreadFilter();
  ThreadFilter(int group, const char *name, const char *status);

  int group() const { return _group; }
  const char *name() const {return _name;}
  const char *status() const {return _status;}

  bool is_null() const {return !(_name || _status);}
  bool accept(bool is_system, const char *name, const char *status);
  bool accept(Thread *thread);

private:
  int _group;
  const char *_name;
  const char *_status;
};

// ParkEvents are type-stable and immortal.
// 
// Lifecycle: Once a ParkEvent is associated with a thread that ParkEvent remains 
// associated with the thread for the thread's entire lifetime - the relationship is
// stable. A thread will be associated at most one ParkEvent.  When the thread 
// expires, the ParkEvent moves to the EventFreeList.  New threads attempt to allocate from 
// the EventFreeList before creating a new Event.  Type-stability frees us from 
// worrying about stale Event or Thread references in the objectMonitor subsystem.
// (A reference to ParkEvent is always valid, even though the event may no longer be associated 
// with the desired or expected thread.  A key aspect of this design is that the callers of 
// park, unpark, etc must tolerate stale references and spurious wakeups).
//
// Only the "associated" thread can block (park) on the ParkEvent, although
// any other thread can unpark a reachable parkevent.  Park() is allowed to
// return spuriously.  In fact park-unpark a really just an optimization to
// avoid unbounded spinning and surrender the CPU to be a polite system citizen.
// A degenerate albeit "impolite" park-unpark implementation could simply return. 
// See http://blogs.sun.com/dave for more details.  
//
// Eventually I'd like to eliminate Events and ObjectWaiters, both of which serve as
// thread proxies, and simply make the THREAD structure type-stable and persistent.
// Currently, we unpark events associated with threads, but ideally we'd just 
// unpark threads. 
//
// The base-class, PlatformEvent, is platform-specific while the ParkEvent is
// platform-independent.  PlatformEvent provides park(), unpark(), etc.  and
// is abstract -- that is, a PlatformEvent should never be instantiated except
// as part of a ParkEvent.
// Equivalently we could have defined a platform-independent base-class that 
// exported Allocate(), Release(), etc.  The platform-specific class would extend 
// that base-class, adding park(), unpark(), etc.  
//
// A word of caution: The JVM uses 2 very similar constructs:
// 1. ParkEvent are used for Java-level "monitor" synchronization.
// 2. Parkers are used by JSR166-JUC park-unpark. 
//
// We'll want to eventually merge these redundant facilities and use ParkEvent.


class ParkEvent : public os::PlatformEvent { 
  private:
    ParkEvent * FreeNext ; 

    // Current association
    Thread * AssociatedWith ; 
    intptr_t RawThreadIdentity ;        // LWPID etc
    volatile int Incarnation ;

  public:
    // MCS-CLH list linkage and Native Mutex/Monitor
    ParkEvent * volatile ListNext ; 
    ParkEvent * volatile ListPrev ; 
    volatile intptr_t OnList ; 
    volatile int TState ; 

    // diagnostic : keep track of last thread to wake this thread.
    // this is useful for construction of dependency graphs.
    void * LastWaker ; 
   

  private:
    static ParkEvent * volatile FreeList ; 
    static volatile int ListLock ; 

    // It's prudent to mark the dtor as "private"
    // ensuring that it's not visible outside the package.
    // Unfortunately gcc warns about such usage, so
    // we revert to the less desirable "protected" visibility.
    // The other compilers accept private dtors.  

  protected:        // Ensure dtor is never invoked
    ~ParkEvent() { guarantee (0, "invariant") ; }

    ParkEvent() : PlatformEvent() { 
       AssociatedWith = NULL ; 
       FreeNext       = NULL ; 
       ListNext       = NULL ; 
       ListPrev       = NULL ; 
       OnList         = 0 ; 
       TState         = 0 ; 
    }

  public:
static ParkEvent*Allocate(JavaThread*t);
    static void Release (ParkEvent * e) ; 
} ;

#endif // THREAD_HPP
