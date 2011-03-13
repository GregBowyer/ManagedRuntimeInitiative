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


// Thread-Local Edens support

#include "collectedHeap.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_pageInfo.hpp"
#include "init.hpp"
#include "os_os.inline.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"
#include "thread.hpp"
#include "threadLocalAllocBuffer.hpp"
#include "typeArrayOop.hpp"
#include "universe.hpp"
#include "vmTags.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

NoTLABParkingMark::NoTLABParkingMark()  { Thread::current()->increment_no_tlab_parking(); }
NoTLABParkingMark::~NoTLABParkingMark() { Thread::current()->decrement_no_tlab_parking(); }


// static member initialization
unsigned         ThreadLocalAllocBuffer::_target_refills = 0;
GlobalTLABStats* ThreadLocalAllocBuffer::_global_stats   = NULL;
ThreadLocalAllocBuffer*ThreadLocalAllocBuffer::_parking_area=NULL;
#if __USE_BOTH_PARKING_SPACES
ThreadLocalAllocBuffer*ThreadLocalAllocBuffer::_parking_area2=NULL;
#endif
uint64_t* ThreadLocalAllocBuffer::_parking_area_locks = NULL;

void ThreadLocalAllocBuffer::clear_before_allocation() {

  // pauseless gc always uses 1mb tlabs so it is not necessary  
  // to compute these stats
  if ( !UseGenPauselessGC ) {
    _slow_refill_waste += (unsigned)remaining();
  }
  make_parsable(true);   // also retire the TLAB  
}

void ThreadLocalAllocBuffer::accumulate_statistics_before_gc() {
  global_stats()->initialize();

  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) {
    thread->tlab().accumulate_statistics();
    thread->tlab().initialize_statistics();
  }

  // Publish new stats if some allocation occurred.
  if (global_stats()->allocation() != 0) {
    global_stats()->publish();
    if (PrintTLAB) {
      global_stats()->print();
    }
  }
}

void ThreadLocalAllocBuffer::accumulate_statistics() {
  size_t capacity = Universe::heap()->tlab_capacity(myThread()) / HeapWordSize;
  size_t unused   = Universe::heap()->unsafe_max_tlab_alloc(myThread()) / HeapWordSize;
  size_t used     = capacity - unused;

  // Update allocation history if a reasonable amount of eden was allocated.
  bool update_allocation_history = used > 0.5 * capacity;

  _gc_waste += (unsigned)remaining();

  if (PrintTLAB && (_number_of_refills > 0 || Verbose)) {
    print_stats("gc");
  }

  if (_number_of_refills > 0) {

    if (update_allocation_history) {
      // Average the fraction of eden allocated in a tlab by this
      // thread for use in the next resize operation.
      // _gc_waste is not subtracted because it's included in
      // "used".
      size_t allocation = _number_of_refills * desired_size();
      double alloc_frac = allocation / (double) used;
      _allocation_fraction.sample(alloc_frac);
    }
    global_stats()->update_allocating_threads();
    global_stats()->update_number_of_refills(_number_of_refills);
    global_stats()->update_allocation(_number_of_refills * desired_size());
    global_stats()->update_gc_waste(_gc_waste);
    global_stats()->update_slow_refill_waste(_slow_refill_waste);
    global_stats()->update_fast_refill_waste(_fast_refill_waste);

  } else {
    assert(_number_of_refills == 0 && _fast_refill_waste == 0 &&
           _slow_refill_waste == 0 && _gc_waste          == 0,
           "tlab stats == 0");
  }
  global_stats()->update_slow_allocations(_slow_allocations);
}

// Fills the current tlab with a dummy filler array to create
// an illusion of a contiguous Eden and optionally retires the tlab.
// Waste accounting should be done in caller as appropriate; see,
// for example, clear_before_allocation().
void ThreadLocalAllocBuffer::make_parsable(bool retire) {
  // Don't do parking ops if there's logging
  NoTLABParkingMark parking_mark;

DEBUG_ONLY(Thread*self=Thread::current();)
  assert0( self->no_tlab_parking() || !self->is_Java_thread() );
  if (end() != NULL) {
    invariants();
    //time stamp the page for promotion heuristics
    if (UseGenPauselessGC) {
      // TODO: Drag this stuff into a GPGC file, so this file doesn't need GPGC internal code.
      // TODO: Verify that if we haven't retired the TLAB, the info->top being below some future TLAB
      //       allocated object isn't a problem.
      PageNum        page = GPGC_Layout::addr_to_PageNum((void *)start());
      GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
      info->set_time(os::elapsed_counter());
HeapWord*top_addr=top();
      info->set_top(top_addr);
      // GPGC doesn't fill the TLAB, because it's hard to get a valid klass for the dummy object.
    } else {
      MemRegion mr(top(), hard_end());
      mr.fill();
    }

    if (retire || ZeroTLAB) {  // "Reset" the TLAB
      set_start(NULL);
      set_top(NULL);
      set_end(NULL);
set_real_end(NULL);
    }
  }
  assert(!(retire || ZeroTLAB)  ||
(start()==NULL&&end()==NULL&&real_end()==NULL&&top()==NULL),
         "TLAB must be reset");

#ifdef ASSERT
  if (PrintTLAB2) {
tty->print_cr("## parks: %d   failed:[ cant lock: %d   no tlab: %d   space full: %d ]   unparks: %d   failed:[ cant lock: %d   space empty: %d   didnt park: %d ]",
      _successful_parks, _failed_parks_cant_lock, _failed_parks_notlab, _failed_parks_space_full, 
      _successful_unparks, _failed_unparks_cant_lock, _failed_unparks_space_empty, _failed_unparks_didnt_park );
  }
  _successful_parks = 0;
  _failed_parks_cant_lock = 0;
  _failed_parks_notlab = 0;
  _failed_parks_space_full = 0;
  _successful_unparks = 0;
  _failed_unparks_cant_lock = 0;
  _failed_unparks_space_empty = 0;
  _failed_unparks_didnt_park = 0;
#endif  
}

// --- zero_filler -----------------------------------------------------------
// If we made TLABs parseable by inserting filler objects AND did not retire
// the TLAB, then after verification we need to go back and zero over those
// filler objects again - AVM allocation assumes some amount of pre-zeroing
// has already happened.
void ThreadLocalAllocBuffer::zero_filler(){
  if( end() ) {
    if (UseGenPauselessGC) {
      // Nothing to do for UseGenPauselessGC, as we're not writing filler objects
      // into the TLAB in make_parsable().
    } else {
      intptr_t *adr = (intptr_t*)top();
      adr[0]=0;                 // zero over bogus object header
      adr[1]=0;
    }
  }
}

void ThreadLocalAllocBuffer::resize_all_tlabs() {
  for(JavaThread *thread = Threads::first(); thread; thread = thread->next()) {
    thread->tlab().resize();
  }
}

void ThreadLocalAllocBuffer::resize() {
assert(!Thread::current()->is_Java_thread(),"JavaThreads might need a NoTLABParkingMark aruond log points?");
  if (ResizeTLAB) {
    // Compute the next tlab size using expected allocation amount
    size_t alloc = (size_t)(_allocation_fraction.average() *
                            (Universe::heap()->tlab_capacity(myThread()) / HeapWordSize));
    size_t new_size = alloc / _target_refills;

    new_size = MIN2(MAX2(new_size, min_size()), max_size());

    size_t aligned_new_size = align_object_size(new_size);

    if (PrintTLAB && Verbose) {
gclog_or_tty->print("TLAB new size: thread: "PTR_FORMAT" [id: %2d]"
                          " refills %d  alloc: %8.6f desired_size: " SIZE_FORMAT " -> " SIZE_FORMAT "\n",
                          myThread(), myThread()->osthread()->thread_id(),
                          _target_refills, _allocation_fraction.average(), desired_size(), aligned_new_size);
    }
    set_desired_size(aligned_new_size);

    set_refill_waste_limit(initial_refill_waste_limit());
  }
}

void ThreadLocalAllocBuffer::initialize_statistics() {
    _number_of_refills = 0;
    _fast_refill_waste = 0;
    _slow_refill_waste = 0;
    _gc_waste          = 0;
    _slow_allocations  = 0;
}

HeapWord* ThreadLocalAllocBuffer::new_tlab(HeapWord* start,
                                           size_t    first_obj_words,
                                           size_t    new_tlab_words,
                                           bool      zero_mem) {
  assert0(JavaThread::current()->no_tlab_parking());
  _number_of_refills++;
  if (PrintTLAB && Verbose) {
    print_stats("fill");
  }
  assert(first_obj_words <= new_tlab_words - alignment_reserve(), "size too small");

  start = initialize(start, first_obj_words, new_tlab_words, zero_mem);

  return start;
}

HeapWord* ThreadLocalAllocBuffer::initialize(HeapWord* new_start,
                                             size_t    first_obj_words,
                                             size_t    new_tlab_words,
                                             bool      zero_mem) {
  assert0(JavaThread::current()->no_tlab_parking());
  assert0(start() == NULL);

  // We calculate an end value that doesn't let the fast path ASM allocation code allocate an
  // object larger than MaxTLABObjectAllocationWords.  When fast path allocations fail over
  // to the slow path, the TLAB code will check to see if the real end has been hit, and if
  // not it will extend the "fake" end.
  HeapWord* top      = new_start + first_obj_words;
  HeapWord* fake_end = top + MaxTLABObjectAllocationWords;
  HeapWord* real_end = new_start + new_tlab_words - alignment_reserve();

  if ( fake_end > real_end ) {
    fake_end = real_end;
  }

set_start(new_start);
  set_top     (top);
set_end(fake_end);
  set_real_end(real_end);

  // Initialize the CLZ pipeline for a new TLAB:
  HeapWord* clz_end = (HeapWord*)round_down(uintptr_t(new_start)+BytesPerCacheLine-1, BytesPerCacheLine)+TLABZeroRegion;
  size_t    words   = clz_end - new_start;

Copy::zero_to_words(new_start,words);

  // Reset amount of internal fragmentation
  if ( !UseGenPauselessGC ) {
    set_refill_waste_limit(initial_refill_waste_limit());
  }

  // Initialize the first object that's been allocated in the new TLAB:
  new_start = clear_tlab_for_allocation(new_start, hard_end(), first_obj_words, zero_mem);

  // We might be pointed at a different TLAB after clear_tlab_for_allocation() returns, but it should still
  // meet the invariants:
  invariants();

  return new_start;
}

void ThreadLocalAllocBuffer::initialize() {
  // Don't do parking ops if there's logging inside the initialize() function:
  NoTLABParkingMark parking_mark;

  set_start(NULL);  
  set_top(NULL);
set_end(NULL);
set_real_end(NULL);
  invariants();

  // Pauseless GC has fixed 1MB TLABs so these computations are unnecessary
  if ( !UseGenPauselessGC ) {
    set_desired_size(initial_desired_size());

    // Following check is needed because at startup the main (primordial)
    // thread is initialized before the heap is.  The initialization for
    // this thread is redone in startup_initialization below.
    if (Universe::heap() != NULL) {
      size_t capacity   = Universe::heap()->tlab_capacity(myThread()) / HeapWordSize;
      double alloc_frac = desired_size() * target_refills() / (double) capacity;
      _allocation_fraction.sample(alloc_frac);
    }

    set_refill_waste_limit(initial_refill_waste_limit());
  
    initialize_statistics();
  }

#ifdef ASSERT
  _successful_parks = 0;
  _failed_parks_cant_lock = 0;
  _failed_parks_notlab = 0;
  _failed_parks_space_full = 0;
  _successful_unparks = 0;
  _failed_unparks_cant_lock = 0;
  _failed_unparks_space_empty = 0;
  _failed_unparks_didnt_park = 0;
#endif
}

#ifdef AZ_PROXIED
void ThreadLocalAllocBuffer::parking_callback(proxy_blocking_call_state_t state) {
if(state==PROXY_BLOCKING_CALL_ENTER)
    JVM_ParkTLAB();
  else if (state == PROXY_BLOCKING_CALL_EXIT)
    JVM_UnparkTLAB();
}
#endif AZ_PROXIED

void ThreadLocalAllocBuffer::startup_initialization() {

  // Assuming each thread's active tlab is, on average,
  // 1/2 full at a GC
  _target_refills = 100 / (2 * TLABWasteTargetPercent);
  _target_refills = MAX2(_target_refills, (unsigned)1U);

  _global_stats = new GlobalTLABStats();
  
  _parking_area_locks = new uint64_t[os::maximum_processor_count()];
  _parking_area = new ThreadLocalAllocBuffer[os::maximum_processor_count()];
  guarantee((_parking_area_locks != NULL) && (_parking_area != NULL), "Could not allocate parking structures");
  for (int j = 0; j < os::maximum_processor_count(); j++) {
_parking_area_locks[j]=0;
    _parking_area[j]. initialize();
  }
#if __USE_BOTH_PARKING_SPACES
  _parking_area2 = new ThreadLocalAllocBuffer[os::maximum_processor_count()];
guarantee(_parking_area2!=NULL,"Could not allocate parking structures");
  for (int j = 0; j < os::maximum_processor_count(); j++) {
    _parking_area2[j]. initialize();
  }
#endif

  // During jvm startup, the main (primordial) thread is initialized
  // before the heap is initialized.  So reinitialize it now.
  guarantee(Thread::current()->is_Java_thread(), "tlab initialization thread not Java thread");
  Thread::current()->tlab().initialize();

  if (PrintTLAB && Verbose) {
    gclog_or_tty->print("TLAB min: " SIZE_FORMAT " initial: " SIZE_FORMAT " max: " SIZE_FORMAT "\n",
                        min_size(), Thread::current()->tlab().initial_desired_size(), max_size());
  }
}

size_t ThreadLocalAllocBuffer::initial_desired_size() {
  size_t init_sz;

  if (TLABSize > 0) {
init_sz=MIN2((size_t)(TLABSize/HeapWordSize),max_size());
  } else if (global_stats() == NULL) {
    // Startup issue - main thread initialized before heap initialized.
    init_sz = min_size();
  } else {
    // Initial size is a function of the average number of allocating threads.
    unsigned nof_threads = global_stats()->allocating_threads_avg();

    init_sz  = (Universe::heap()->tlab_capacity(myThread()) / HeapWordSize) /
                      (nof_threads * target_refills());
    init_sz = align_object_size(init_sz);
    init_sz = MIN2(MAX2(init_sz, min_size()), max_size());
  }
  return init_sz;
}

const size_t ThreadLocalAllocBuffer::max_size() {

  // TLABs can't be bigger than we can fill with a int[Integer.MAX_VALUE].
  // This restriction could be removed by enabling filling with multiple arrays.
  // If we compute that the reasonable way as
  //    header_size + ((sizeof(jint) * max_jint) / HeapWordSize)
  // we'll overflow on the multiply, so we do the divide first.
  // We actually lose a little by dividing first,
  // but that just makes the TLAB  somewhat smaller than the biggest array,
  // which is fine, since we'll be able to fill that.

  size_t unaligned_max_size = round_to(typeArrayOopDesc::header_size_T_INT(), HeapWordSize) +
                              sizeof(jint) *
                              ((juint) max_jint / (size_t) HeapWordSize);
  return align_size_down(unaligned_max_size, MinObjAlignment);
}

void ThreadLocalAllocBuffer::print_stats(const char* tag) {
  Thread* thrd = myThread();
  size_t waste = _gc_waste + _slow_refill_waste + _fast_refill_waste;
  size_t alloc = _number_of_refills * _desired_size;
  double waste_percent = alloc == 0 ? 0.0 :
                      100.0 * waste / alloc;
  size_t tlab_used  = Universe::heap()->tlab_capacity(thrd) -
                      Universe::heap()->unsafe_max_tlab_alloc(thrd);
gclog_or_tty->print("TLAB: %s thread: "PTR_FORMAT" [id: %2d]"
                      " desired_size: " SIZE_FORMAT "KB"
                      " slow allocs: %d  refill waste: " SIZE_FORMAT "B"
                      " alloc:%8.5f %8.0fKB refills: %d waste %4.1f%% gc: %dB"
                      " slow: %dB fast: %dB\n",
                      tag, thrd, thrd->osthread()->thread_id(),
                      _desired_size / (K / HeapWordSize),
                      _slow_allocations, _refill_waste_limit * HeapWordSize,
                      _allocation_fraction.average(),
                      _allocation_fraction.average() * tlab_used / K,
                      _number_of_refills, waste_percent,
                      _gc_waste * HeapWordSize,
                      _slow_refill_waste * HeapWordSize,
                      _fast_refill_waste * HeapWordSize);
}

void ThreadLocalAllocBuffer::verify() {
  HeapWord* p = start();
  HeapWord* t = top();
  HeapWord* prev_p = NULL;
  while (p < t) {
    oop(p)->verify();
    prev_p = p;
    p += oop(p)->size();
  }
  guarantee(p == top(), "end of last object must match end of space");
}

bool ThreadLocalAllocBuffer::park() {
  // Park per thread tick buffers to avoid holding onto old ticks.
    //  ThreadLocalProfileBuffer::park();

  if ( !is_init_completed() ) return false;
  JavaThread *self = JavaThread::current();
  TickProfiler::meta_tick(tlab_park_tick);
  // compiler threads may not park
  if ( self->is_Compiler_thread() ) return false;
  
  // Do not mess with parking if we may be at safepoint - all the  
  // tlab structs will be cleared for GCs. 
  if ( self->jvm_locked_by_VM() ) return false;
  
  // Don't park if inside a NoTLABParkingMark
  if ( self->no_tlab_parking() ) return false;

  // Don't do parking ops if there's logging inside the park() function:
  NoTLABParkingMark parking_mark;

  uint mycpu = (uint) os::current_cpu_number();
  ThreadLocalAllocBuffer* space = & _parking_area[mycpu];

#if __USE_BOTH_PARKING_SPACES
  if (space->start() != NULL) {
    // try the other space
    space = & _parking_area2[mycpu];
  }
#endif
  if (space->start() != NULL) {
    return false;
  }
  // maybe there is no tlab to park.
  if (start() == real_end()) {
    return false;
  }
  
  // Need to protect against a safepoint happening while we are in the middle of parking,
  // tlab structs will be cleared for GCs. If we find the current thread already 
  // holds his jvm lock when we get here he is probably making a blocking proxy call from within
  // the VM. So it is safe to park at that point. If we can take the free jvm lock that works
  // as well.
  bool self_lock;
  bool was_locked_by_self = self->jvm_locked_by_self();
  if (was_locked_by_self == false) {
    bool self_lock = self->jvm_lock_self_attempt();
    if ( self_lock == false ) {
      // we must be re-entering this function via preemption interrupt 
      // or maybe a safepoint just began...
DEBUG_ONLY(_failed_parks_cant_lock++;)
      return false;
    }
  }
  
  intptr_t parked_space_lock = Atomic::cmpxchg_ptr( (intptr_t) self,  (intptr_t*) & _parking_area_locks[mycpu], 0 );
  if ( parked_space_lock != 0 ) {
    // we lost to modify this slot - we could have a preemption interrupt run on
    // a different cpu than the one the thread was running on in normal user time
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
DEBUG_ONLY(_failed_parks_cant_lock++;)
    return false;
  }

  // maybe there is no tlab to park.
  if (start() == real_end()) {
    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
DEBUG_ONLY(_failed_parks_notlab++;)
    return false;
  }
  
  assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );
  assert0( _parking_area != NULL );
  
  // If the space is empty we can park here
  if (space->start() == NULL) {
    space->set_start(start());
    space->set_top(top());
    space->set_end(end());
    space->set_real_end(real_end());
    space->set_desired_size(desired_size());

    set_start(NULL);
    set_top(NULL);
set_end(NULL);
set_real_end(NULL);
    set_desired_size(0);

    // Pauseless always uses 1mb tlabs so it s not necessary to drag 
    // along all these stats
    if ( !UseGenPauselessGC ) {
      space->_number_of_refills = number_of_refills();
      space->_fast_refill_waste = fast_refill_waste();
      space->_slow_refill_waste = slow_refill_waste();
      space->_gc_waste = gc_waste();
      space->_slow_allocations = slow_allocations();
      
      _number_of_refills = 0;
      _fast_refill_waste = 0;
      _slow_refill_waste = 0;
      _gc_waste          = 0;
      _slow_allocations  = 0;
    }
    
DEBUG_ONLY(_successful_parks++;)
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );

    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
    return true;
  } else {
    // there was already something parked here
DEBUG_ONLY(_failed_parks_space_full++;)
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
    return false;
  }
}

bool ThreadLocalAllocBuffer::unpark() {
    //  ThreadLocalProfileBuffer::unpark();

  if ( !is_init_completed() ) return false;
  JavaThread *self = JavaThread::current();
  TickProfiler::meta_tick(tlab_unpark_tick);
  // compiler threads may not park
  if ( self->is_Compiler_thread() ) return false;
  // Do not mess with parking if we may be at safepoint - all the  
  // tlab structs will be cleared for GCs. 
  if ( self->jvm_locked_by_VM() ) return false;
  // Don't unpark when inside a NoTLABParkingMark:
  if ( self->no_tlab_parking() ) return false;

  // Don't do parking ops if there's logging inside the unpark() function:
  NoTLABParkingMark parking_mark;

  uint mycpu = (uint) os::current_cpu_number();
  ThreadLocalAllocBuffer* space = & _parking_area[mycpu];

  // pre-check that there is a tlab to steal before locking
#if __USE_BOTH_PARKING_SPACES
  if (space->start() == NULL) { 
    space = & _parking_area2[mycpu];
  }
#endif
  if (space->start() == NULL) { 
    return false;
  }

  // Need to protect against a safepoint happening while we are in the middle of parking,
  // tlab structs will be cleared for GCs. If we find the current thread already 
  // holds his jvm lock when we get here he is probably making a blocking proxy call from within
  // the VM. So it is safe to park at that point. If we can take the free jvm lock that works
  // as well.
  bool self_lock;
  bool was_locked_by_self = self->jvm_locked_by_self();
  if (was_locked_by_self == false) {
    bool self_lock = self->jvm_lock_self_attempt();
    if ( self_lock == false ) {
      // we must be re-entering this function via preemption interrupt 
      // or maybe a safepoint just began...
DEBUG_ONLY(_failed_parks_cant_lock++;)
      return false;
    }
  }


  intptr_t parked_space_lock = Atomic::cmpxchg_ptr( (intptr_t) self,  (intptr_t*) & _parking_area_locks[mycpu], 0 );
  if ( parked_space_lock != 0 ) {
    // we lost to modify this slot - we could have a preemption interrupt run on
    // a different cpu than the one the thread was running on in normal user time
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
DEBUG_ONLY(_failed_unparks_cant_lock++;)
    Atomic::membar();
    return false;
  }

  assert0( _parking_area != NULL );
  assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );
      
  // If we still have a real tlab at this point just unlock and continue
if(start()!=NULL){
    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
DEBUG_ONLY(_failed_unparks_didnt_park++;)
    return false;
  }
  
  // if this is null there is no parked tlab here, just unlock and continue
  if (space->start() != NULL) {  
    set_start(space->start());
    set_top(space->top());
    set_end(space->end());
    set_real_end(space->real_end());
    set_desired_size(space->desired_size());
    
    space->set_start(NULL);
    space->set_top(NULL);
    space->set_end(NULL);
    space->set_real_end(NULL);
    space->set_desired_size(0);

    if ( !UseGenPauselessGC ) {
      _number_of_refills = space->number_of_refills();
      _fast_refill_waste = space->fast_refill_waste();
      _slow_refill_waste = space->slow_refill_waste();
      _gc_waste = space->gc_waste();
      _slow_allocations = space->slow_allocations();
      
      space->_number_of_refills = 0;
      space->_fast_refill_waste = 0;
      space->_slow_refill_waste = 0;
      space->_gc_waste          = 0;
      space->_slow_allocations  = 0;
    }
    
DEBUG_ONLY(_successful_unparks++;)
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );

    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
    return true;
  } else {
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );
    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    if ( ! was_locked_by_self ) self->jvm_unlock_self();
    Atomic::membar();
DEBUG_ONLY(_failed_unparks_space_empty++;)
    return false;
  }
}

HeapWord* ThreadLocalAllocBuffer::unpark_and_allocate(size_t size, bool zero_mem) {
JavaThread*self=JavaThread::current();
  
  // Don't do parking ops if there's logging inside the unpark_and_allocate() function:
  assert0(self->no_tlab_parking());

  // We will only arrive here during the normal allocation path
guarantee(JavaThread::current()->jvm_locked_by_self(),"Trying to steal parked TLAB without holding jvm_lock");

  uint mycpu = (uint) os::current_cpu_number();
  ThreadLocalAllocBuffer* space = & _parking_area[mycpu];  
  
  // pre-check that there is a tlab to steal
#if __USE_BOTH_PARKING_SPACES
  if (space->start() == NULL ) {
    // look in the other space
    space = & _parking_area2[mycpu];
  }
#endif
  if (space->start() == NULL) {  
    return NULL;
  }
  
  intptr_t parked_space_lock = Atomic::cmpxchg_ptr( (intptr_t) self,  (intptr_t*) & _parking_area_locks[mycpu], 0 );
  if ( parked_space_lock != 0 ) {
    // we lost to modify this slot
DEBUG_ONLY(_failed_unparks_cant_lock++;)
    return NULL;
  }

  assert0( _parking_area != NULL );
  assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );

  // if this is null there is no parked tlab here, just unlock and continue
  if (space->start() != NULL) {  
HeapWord*obj=NULL;
    size_t parked_remainder = space->real_end() - space->top();
    
    if (parked_remainder < size) {
      // There is not enough space left for this object so empty this space
      space->make_parsable(true);
      _parking_area_locks[mycpu] = 0;
      Atomic::membar();
      return NULL;
    }
    
    set_start(space->start());
    set_top(space->top());
    set_end(space->end());
    set_real_end(space->real_end());
    set_desired_size(space->desired_size());

    space->set_start(NULL);
    space->set_top(NULL);
    space->set_end(NULL);
    space->set_real_end(NULL);
    space->set_desired_size(0);

    if ( !UseGenPauselessGC ) {
      _number_of_refills = space->number_of_refills();
      _fast_refill_waste = space->fast_refill_waste();
      _slow_refill_waste = space->slow_refill_waste();
      _gc_waste = space->gc_waste();
      _slow_allocations = space->slow_allocations();
      
      space->_number_of_refills = 0;
      space->_fast_refill_waste = 0;
      space->_slow_refill_waste = 0;
      space->_gc_waste          = 0;
      space->_slow_allocations  = 0;
    }
    
    // Allocate from the unparked TLAB.
    obj = allocate(size, zero_mem);

assert(obj!=NULL,"alloc failed after passing size check for TLAB");
    
DEBUG_ONLY(_successful_unparks++;)
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );

    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    Atomic::membar();
    return obj;
  } else {
    assert0( (start() <= top()) && (top() <= end()) && (end() <= real_end()) );
    // we are done, tlabs are all consistent, unlock the space lock
    _parking_area_locks[mycpu] = 0;
    Atomic::membar();
DEBUG_ONLY(_failed_unparks_space_empty++;)
    return NULL;
  }
}

void ThreadLocalAllocBuffer::reset_parking_area(){
for(int i=0;i<os::maximum_processor_count();i++){
  
    if ( (_parking_area[i]).start() != (HeapWord*) NULL ) {
      uint parked_waste   =  (_parking_area[i]).real_end() - (_parking_area[i]).top();
      assert0( (_parking_area[i]).start() <= (_parking_area[i]).top() );
      assert0( (_parking_area[i]).top() <= (_parking_area[i]).end() );
      assert0( (_parking_area[i]).end() <= (_parking_area[i]).real_end() );
      if (PrintTLAB2) {
        tty->print_cr( "# reset_parking_area  : parked on %d: start: " PTR_FORMAT " waste: %d ", i,
          (_parking_area[i]).start(), parked_waste );
      }
    }
#if __USE_BOTH_PARKING_SPACES
    if ( (_parking_area2[i]).start() != (HeapWord*) NULL ) {
      uint parked_waste   =  (_parking_area2[i]).real_end() - (_parking_area2[i]).top();
      assert0( (_parking_area2[i]).start() <= (_parking_area2[i]).top() );
      assert0( (_parking_area2[i]).top() <= (_parking_area2[i]).end() );
      assert0( (_parking_area2[i]).end() <= (_parking_area2[i]).real_end() );
      if (PrintTLAB2) {
tty->print_cr("# reset_parking_area2 : parked on %d: start: %llx waste: %d ",i,
          (_parking_area2[i]).start(), parked_waste );
      }
    }
#endif

    (_parking_area [i]).make_parsable(true);
#if __USE_BOTH_PARKING_SPACES
    (_parking_area2[i]).make_parsable(true);
#endif
_parking_area_locks[i]=0;
  }
}


size_t ThreadLocalAllocBuffer::alignment_reserve(){
  size_t array_header_size = (size_t)(typeArrayOopDesc::header_size_T_INT()+HeapWordSize-1)>>LogHeapWordSize;
  size_t prefetch_size = 0;
  if (TLABPrefetchSize > 0) {
    // When we are clz'ing the TLAB, the minimum chunk that can be clz'd is
    // BytesPerCacheLine.  The TLAB may not be BytesPerCacheLine
    // aligned, though.  To make sure that we always have enough space, we add
    // BytesPerCacheLine to the alignment_reserve
    prefetch_size = TLABPrefetchSize + BytesPerCacheLine;
  }
  
  return align_object_size(MAX2(array_header_size, prefetch_size));
} 

Thread* ThreadLocalAllocBuffer::myThread() {
  return (Thread*)(((char *)this) +
                   in_bytes(start_offset()) -
                   in_bytes(Thread::tlab_start_offset()));
}


GlobalTLABStats::GlobalTLABStats() :
  _allocating_threads_avg(TLABAllocationWeight) {

  initialize();

  _allocating_threads_avg.sample(1); // One allocating thread at startup
}

void GlobalTLABStats::initialize() {
  // Clear counters summarizing info from all threads
  _allocating_threads      = 0;
  _total_refills           = 0;
  _max_refills             = 0;
  _total_allocation        = 0;
  _total_gc_waste          = 0;
  _max_gc_waste            = 0;
  _total_slow_refill_waste = 0;
  _max_slow_refill_waste   = 0;
  _total_fast_refill_waste = 0;
  _max_fast_refill_waste   = 0;
  _total_slow_allocations  = 0;
  _max_slow_allocations    = 0;
}

void GlobalTLABStats::publish() {
  _allocating_threads_avg.sample(_allocating_threads);
}

void GlobalTLABStats::print() {
  size_t waste = _total_gc_waste + _total_slow_refill_waste + _total_fast_refill_waste;
  double waste_percent = _total_allocation == 0 ? 0.0 :
                         100.0 * waste / _total_allocation;
  gclog_or_tty->print("TLAB totals: thrds: %d  refills: %d max: %d"
                      " slow allocs: %d max %d waste: %4.1f%%"
                      " gc: " SIZE_FORMAT "B max: " SIZE_FORMAT "B"
                      " slow: " SIZE_FORMAT "B max: " SIZE_FORMAT "B"
                      " fast: " SIZE_FORMAT "B max: " SIZE_FORMAT "B\n",
                      _allocating_threads,
                      _total_refills, _max_refills,
                      _total_slow_allocations, _max_slow_allocations,
                      waste_percent,
                      _total_gc_waste * HeapWordSize,
                      _max_gc_waste * HeapWordSize,
                      _total_slow_refill_waste * HeapWordSize,
                      _max_slow_refill_waste * HeapWordSize,
                      _total_fast_refill_waste * HeapWordSize,
                      _max_fast_refill_waste * HeapWordSize);
}
