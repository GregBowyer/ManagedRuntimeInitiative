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
#ifndef THREADLOCALALLOCBUFFER_HPP
#define THREADLOCALALLOCBUFFER_HPP

#include "allocation.hpp"
#include "gcUtil.hpp"
#include "perfData.hpp"
class GlobalTLABStats;

class NoTLABParkingMark {
 public: 
  NoTLABParkingMark();
  ~NoTLABParkingMark();
};


// ThreadLocalAllocBuffer: a descriptor for thread-local storage used by
// the threads for allocation.
//            It is thread-private at any time, but maybe multiplexed over
//            time across multiple threads. The park()/unpark() pair is
//            used to make it avaiable for such multiplexing.
class ThreadLocalAllocBuffer: public CHeapObj {
 private:
  HeapWord* _top;                                // address after last allocation
HeapWord*_end;//allocation end visible to asm fast path (excluding alignment_reserve)
  HeapWord* _real_end;                           // actual allocation end (excluding alignment_reserve)
  HeapWord* _start;                              // address of TLAB
  size_t    _desired_size;                       // desired size   (including alignment_reserve)
  size_t    _refill_waste_limit;                 // hold onto tlab if free() is larger than this

  static unsigned _target_refills;               // expected number of refills between GCs

  unsigned  _number_of_refills;
  unsigned  _fast_refill_waste;
  unsigned  _slow_refill_waste;
  unsigned  _gc_waste;
  unsigned  _slow_allocations;
  
// I was experimenting with 2 parking spaces per cpu -
// it is faster in some situations so I want to make more tests later on
#define __USE_BOTH_PARKING_SPACES 0

  static ThreadLocalAllocBuffer *_parking_area;
#if __USE_BOTH_PARKING_SPACES
  static ThreadLocalAllocBuffer *_parking_area2;
#endif
  // There is 1 lock word per parking space, the locker fills
  // it with his thread* 
  static uint64_t *_parking_area_locks;
  
#ifdef ASSERT
  unsigned  _successful_parks;
  unsigned  _failed_parks_cant_lock;
  unsigned  _failed_parks_notlab;
  unsigned  _failed_parks_space_full;
  unsigned  _successful_unparks;
  unsigned  _failed_unparks_cant_lock;
  unsigned  _failed_unparks_space_empty;
  unsigned  _failed_unparks_didnt_park;
#endif
  AdaptiveWeightedAverage _allocation_fraction;  // fraction of eden allocated in tlabs

  void accumulate_statistics();
  void initialize_statistics();

  static HeapWord* inject_new_space_bits( HeapWord *x ) {
    return (HeapWord*)(((uint64_t)objectRef::new_space_id << objectRef::space_shift) | (intptr_t)x );
  }
  
  static HeapWord* clear_space_bits( HeapWord *x ) {
    return (HeapWord*)(~objectRef::space_mask_in_place & (intptr_t)x );
  }

  void set_start(HeapWord* start)                { _start    = inject_new_space_bits(start); }
  void set_end(HeapWord* end)                    { _end      = inject_new_space_bits(end); }
  void set_real_end(HeapWord* real_end)          { _real_end = inject_new_space_bits(real_end); }
  void set_top(HeapWord* top)                    { _top      = inject_new_space_bits(top); }
  void set_desired_size(size_t desired_size)     { _desired_size = desired_size; }  

  void set_refill_waste_limit(size_t waste)      { _refill_waste_limit = waste;  }

  size_t initial_refill_waste_limit()            { return desired_size() / TLABRefillWasteFraction; }
  
  static int    target_refills()                 { return _target_refills; }
  size_t initial_desired_size();

  size_t remaining() const                       { return end() == NULL ? 0 : pointer_delta(hard_end(), top()); }

  // Resize based on amount of allocation, etc.
  void resize();

  void invariants() const {
    assert(top() >= start() && top() <= end() && end() <= real_end(), "invalid tlab");
    // Verify the CLZ area
HeapWord*a=top();
    if( a ) {
      int extra_bytes = (((intptr_t)a + (BytesPerCacheLine-1)) & -BytesPerCacheLine) - (intptr_t)a;
      int clz_word_count = (TLABZeroRegion + extra_bytes) / sizeof(HeapWord);
for(int i=0;i<clz_word_count;i++)
        assert0( ((intptr_t*)a)[i] == 0 );
    }
  }

  HeapWord* initialize(HeapWord* start, size_t first_obj_words, size_t new_tlab_words, bool zero_mem);

  void print_stats(const char* tag);

  Thread* myThread();

  // statistics

  int number_of_refills() const { return _number_of_refills; }
  int fast_refill_waste() const { return _fast_refill_waste; }
  int slow_refill_waste() const { return _slow_refill_waste; }
  int gc_waste() const          { return _gc_waste; }
  int slow_allocations() const  { return _slow_allocations; }

  static GlobalTLABStats* _global_stats;
  static GlobalTLABStats* global_stats() { return _global_stats; }

public:
  ThreadLocalAllocBuffer() : _allocation_fraction(TLABAllocationWeight) {
    // do nothing.  tlabs must be inited by initialize() calls
  }

  static const size_t min_size()                 { return align_object_size(MinTLABSize / HeapWordSize); }
  static const size_t max_size();
  HeapWord* start() const                        { return clear_space_bits(_start); }
  HeapWord* end() const                          { return clear_space_bits(_end); }
  HeapWord* real_end() const                     { return clear_space_bits(_real_end); }
  HeapWord* hard_end() const                     { return real_end() + alignment_reserve(); }
  HeapWord* top() const                          { return clear_space_bits(_top); }
  size_t desired_size() const                    { return _desired_size; }
size_t free()const{return pointer_delta(real_end(),top());}
  // Don't discard tlab if remaining space is larger than this.
  size_t refill_waste_limit() const              { return _refill_waste_limit; }

  // Zero memory just allocated from a TLAB.  Used by allocate() below, and some external callers.
  // This method both relies upon and maintains the CLZ pipeline for the TLAB.  If zero_mem==true,
  // the object itself will also be initialized to zero when it's too big for the CLZ pipeline to
  // have covered it.  Returns the location of obj, which may have moved during incremental initialization.
  inline static HeapWord* clear_tlab_for_allocation(HeapWord* obj, HeapWord* tlab_end, size_t word_size, bool zero_mem);

  // Allocate size HeapWords.  If zero_mem==true, the memory is initialized to zero.
inline HeapWord*allocate(size_t size,bool zero_mem);
  static size_t alignment_reserve();
  static size_t alignment_reserve_in_bytes()     { return alignment_reserve() * HeapWordSize; }

  // Try and extend end(), without passing real_end().
  inline void update_end_from_top();

  // Return tlab size or remaining space in eden such that the
  // space is large enough to hold obj_size and necessary fill space.
  // Otherwise return 0;
  inline size_t compute_size(size_t obj_size);

  // Record slow allocation
  inline void record_slow_allocation(size_t obj_size);

  // Initialization at startup
  static void startup_initialization();

  // Make an in-use tlab parsable, optionally also retiring it.
  void make_parsable(bool retire);
  void zero_filler();           // reverse parsable by zero'ing filler object out
  
  // retire in-use tlab before allocation of a new tlab
  void clear_before_allocation();

  // Accumulate statistics across all tlabs before gc
  static void accumulate_statistics_before_gc();

  // Resize tlabs for all threads
  static void resize_all_tlabs();
  
  // Share TLAB on a per-cpu basis when thread is about to block
  bool park();
  bool unpark();
HeapWord*unpark_and_allocate(size_t size,bool zero_mem);
  
#ifdef AZ_PROXIED
  static void parking_callback(proxy_blocking_call_state_t state);
#endif // AZ_PROXIED
    
  static void reset_parking_area();
  
  HeapWord* new_tlab(HeapWord* start, size_t first_obj_words, size_t new_tlab_words, bool zero_mem);
  void      initialize();

  static size_t refill_waste_limit_increment()   { return TLABWasteIncrement; }

  // Code generation support
  static ByteSize start_offset()                 { return byte_offset_of(ThreadLocalAllocBuffer, _start); }
  static ByteSize end_offset()                   { return byte_offset_of(ThreadLocalAllocBuffer, _end  ); }
  static ByteSize top_offset()                   { return byte_offset_of(ThreadLocalAllocBuffer, _top  ); }
  static ByteSize size_offset()                  { return byte_offset_of(ThreadLocalAllocBuffer, _desired_size ); }  
  static ByteSize refill_waste_limit_offset()    { return byte_offset_of(ThreadLocalAllocBuffer, _refill_waste_limit ); }  

  static ByteSize number_of_refills_offset()     { return byte_offset_of(ThreadLocalAllocBuffer, _number_of_refills ); }
  static ByteSize fast_refill_waste_offset()     { return byte_offset_of(ThreadLocalAllocBuffer, _fast_refill_waste ); }
  static ByteSize slow_allocations_offset()      { return byte_offset_of(ThreadLocalAllocBuffer, _slow_allocations ); }

  void verify();
};

class GlobalTLABStats: public CHeapObj {
private:

  // Accumulate perfdata in private variables because
  // PerfData should be write-only for security reasons
  // (see perfData.hpp)
  unsigned _allocating_threads;
  unsigned _total_refills;
  unsigned _max_refills;
  size_t   _total_allocation;
  size_t   _total_gc_waste;
  size_t   _max_gc_waste;
  size_t   _total_slow_refill_waste;
  size_t   _max_slow_refill_waste;
  size_t   _total_fast_refill_waste;
  size_t   _max_fast_refill_waste;
  unsigned _total_slow_allocations;
  unsigned _max_slow_allocations;
  
  PerfVariable* _perf_allocating_threads;
  PerfVariable* _perf_total_refills;
  PerfVariable* _perf_max_refills;
  PerfVariable* _perf_allocation;
  PerfVariable* _perf_gc_waste;
  PerfVariable* _perf_max_gc_waste;
  PerfVariable* _perf_slow_refill_waste;
  PerfVariable* _perf_max_slow_refill_waste;
  PerfVariable* _perf_fast_refill_waste;
  PerfVariable* _perf_max_fast_refill_waste;
  PerfVariable* _perf_slow_allocations;
  PerfVariable* _perf_max_slow_allocations;

  AdaptiveWeightedAverage _allocating_threads_avg;

public:
  GlobalTLABStats();

  // Initialize all counters
  void initialize();

  // Write all perf counters to the perf_counters
  void publish();

  void print();

  // Accessors
  unsigned allocating_threads_avg() {
    return MAX2((unsigned)(_allocating_threads_avg.average() + 0.5), 1U);
  }

  size_t allocation() {
    return _total_allocation;
  }

  // Update methods

  void update_allocating_threads() {
    _allocating_threads++;
  }
  void update_number_of_refills(unsigned value) {
    _total_refills += value;
    _max_refills    = MAX2(_max_refills, value);
  }
  void update_allocation(size_t value) {
    _total_allocation += value;
  }
  void update_gc_waste(size_t value) {
    _total_gc_waste += value;
    _max_gc_waste    = MAX2(_max_gc_waste, value);
  }
  void update_fast_refill_waste(size_t value) {
    _total_fast_refill_waste += value;
    _max_fast_refill_waste    = MAX2(_max_fast_refill_waste, value);
  }
  void update_slow_refill_waste(size_t value) {
    _total_slow_refill_waste += value;
    _max_slow_refill_waste    = MAX2(_max_slow_refill_waste, value);
  }
  void update_slow_allocations(unsigned value) {
    _total_slow_allocations += value;
    _max_slow_allocations    = MAX2(_max_slow_allocations, value);
  }
};

#endif // THREADLOCALALLOCBUFFER_HPP
