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
#ifndef GPGC_HEAP_HPP
#define GPGC_HEAP_HPP


#include "collectedHeap.hpp"
#include "gpgc_space.hpp"
#include "gpgc_stats.hpp"
#include "pgcTaskThread.hpp"


class GPGC_Generation;


class GPGC_Heap:public CollectedHeap{
  private:
static GPGC_Heap*_heap;

    static PGCTaskManager*     _new_gc_task_manager;
    static PGCTaskManager*     _old_gc_task_manager;

    static jlong               _gpgc_time_stamp_promotion_threshold;

    // flag to reduce the rate of successfully posted
    // vm operations when System.gc() is called at a high rate
    static jint                _system_gc_is_pending;

    static bool                _safe_to_iterate;    // Used for JVMTI heap iteration

  public:
    static GPGC_Heap*            heap()                                    { return _heap; }
    static PGCTaskManager* const new_gc_task_manager()                     { return _new_gc_task_manager; }
    static PGCTaskManager* const old_gc_task_manager()                     { return _old_gc_task_manager; }

    static long                  gpgc_time_stamp_promotion_threshold()     { return _gpgc_time_stamp_promotion_threshold; }

    static void                  reset_system_gc_is_pending()              { _system_gc_is_pending = 0; }
    static void                  set_safe_to_iterate(bool safe_to_iterate) { _safe_to_iterate = safe_to_iterate; }
    static bool                  is_safe_to_iterate()                      { return _safe_to_iterate; }


  private:
    long       _actual_collections;
    long       _actual_new_collections;
    long       _actual_old_collections;
    
GPGC_Stats _gc_stats;

    size_t     _max_heap_size_specified;
    size_t     _peak_heap_size_allocated;

    size_t     _last_gc_live_bytes;

    bool       _saved_GPGCNoPermRelocation;

    //GPGC mem-regions
    MemRegion _reserved_heap;                 // 16G-4T
    MemRegion _reserved_heap_mirror;          // 4T-8T
    MemRegion _reserved_heap_structures;      // 12T-14T

  protected:
    size_t     compute_tlab_size(Thread *, size_t requested_free_size, size_t obj_size, size_t alignment_reserve);
HeapWord*allocate_new_tlab(size_t size);

  public:
    GPGC_Heap();

    CollectedHeap::Name kind()        const { return CollectedHeap::GenPauselessHeap; }

    jint initialize();
    void post_initialize();
    void final_initialize();

    long actual_collections()               { return _actual_collections; }
    long actual_new_collections()           { return _actual_new_collections; }
    long actual_old_collections()           { return _actual_old_collections; }
    void increment_actual_collections()     { _actual_collections ++; }
    void increment_actual_new_collections() { _actual_new_collections ++; }
    void increment_actual_old_collections() { _actual_old_collections ++; }


    GPGC_Stats* gc_stats()                  { return &_gc_stats; }

    bool   supports_tlab_allocation() const { return true; }

    // This is the max the heap could ever grow to (less book-keeping?)
    size_t max_heap_size_specified() const { return _max_heap_size_specified; }   // in bytes
    size_t max_capacity() const            { return _peak_heap_size_allocated; }  // Max heap in bytes

    size_t capacity() const                { return capacity_in_words() << LogBytesPerWord; }
    size_t used() const                    { return used_in_words() << LogBytesPerWord; }
    size_t permanent_capacity() const      { return perm_capacity_in_words() << LogBytesPerWord; }
    size_t permanent_used() const          { return perm_used_in_words() << LogBytesPerWord; }

    size_t capacity_in_words() const       { return GPGC_Space::total_words_capacity(); }
    size_t used_in_words() const           { return GPGC_Space::total_words_used(); }
    size_t perm_capacity_in_words() const  { return GPGC_Space::perm_gen_words_capacity(); }
    size_t perm_used_in_words() const      { return GPGC_Space::perm_gen_words_used(); }

    void   set_last_gc_live_bytes(size_t live) { _last_gc_live_bytes = live; }
    size_t last_gc_live_bytes    () const      { return _last_gc_live_bytes; }

    
    size_t tlab_capacity(Thread *) const   { return GPGC_Space::new_gen_words_capacity() << LogBytesPerWord; }
    size_t max_tlab_size()                 { return WordsPerGPGCPage; }

    GPGC_Generation* new_gen()             { return GPGC_Space::generation(GPGC_PageInfo::NewGen); }
    GPGC_Generation* old_gen()             { return GPGC_Space::generation(GPGC_PageInfo::OldGen); }
    GPGC_Generation* perm_gen()            { return GPGC_Space::generation(GPGC_PageInfo::PermGen); }

    bool is_in(const void *p) const           { return GPGC_Space::is_in(p); }
    bool is_in_new(const void *p) const       { return GPGC_Space::is_in_new(p); }
    bool is_in_permanent(const void *p) const { return GPGC_Space::is_in_permanent(p); }
    bool is_permanent(const void *p) const    { return GPGC_Space::is_in_permanent(p); }

    // region tests
    bool is_in_reserved(const void* p) const {
      // shouldnt be checking on heap structures?
assert(!_reserved_heap_structures.contains(p),"dont expect this test on GC data structures");
      return (_reserved_heap.contains(p) || _reserved_heap_mirror.contains(p));
    }

    bool is_in_reserved_or_null(const void* p) const {
      return p == NULL || is_in_reserved(p);
    }



    // Does this heap support heap inspection? (+PrintClassHistogram)
    bool supports_heap_inspection() const     { return false; }

    // Inline contiguous allocation not supported, must use TLABs
    bool supports_inline_contig_alloc() const { return false; }
    HeapWord** top_addr() const               { ShouldNotReachHere(); return NULL; }
    HeapWord** end_addr() const               { ShouldNotReachHere(); return NULL; }

    void tlab_allocation_mark_new_oop(HeapWord* obj);

    // Allocate size words in the new or perm gen.
    HeapWord* mem_allocate          (size_t size, bool is_large_noref, bool is_tlab, bool* gc_overhead_limit_was_exceeded);
HeapWord*permanent_mem_allocate(size_t size);

    virtual HeapWord* incremental_init_obj(HeapWord* obj, size_t word_size);

    size_t large_typearray_limit()         { return FastAllocateSizeLimit; }

    void collect(GCCause::Cause cause);
    void collect_and_iterate(GCCause::Cause cause, SafepointEndCallback end_callback, void* user_data);

    // This interface assumes that it's being called by the
    // vm thread. It collects the heap assuming that the
    // heap lock is already held and that we are executing in
    // the context of the vm thread.
    void collect_as_vm_thread(GCCause::Cause cause);


    // Perm gen iteration functions.
    void permanent_oop_iterate(OopClosure* cl)       { ShouldNotReachHere(); } // SuperClass requires, but no one calls
    void permanent_object_iterate(ObjectClosure* cl);


    jlong millis_since_last_gc();

    void prepare_for_verify()             { ensure_parsability(true); }
    void post_verify()                    { ensure_prezero(); }

    void print() const                    { print_on(tty); }
    void print_on(outputStream* st) const;
    void print_xml_on(xmlBuffer* xb, bool ref) const;

    virtual void print_gc_threads_on(outputStream *st) const;
    virtual void gc_threads_do(ThreadClosure* tc) const;

    void verify(bool allow_dirty, bool silent);

    virtual uint64_t space_id_for_address(const void* addr) const;

    void mprotect()    { guarantee(!MProtectHeapAtSafepoint, "Cannot set MprotectHeapAtSafepoint with GenPauselessGC");
                         ShouldNotReachHere(); }
    void munprotect()  { guarantee(!MProtectHeapAtSafepoint, "Cannot set MProtectHeapAtSafepoint with GenPauselessGC");
                         ShouldNotReachHere(); }

    void accumulate_statistics_all_tlabs()   { CollectedHeap::accumulate_statistics_all_tlabs(); }
    void resize_all_tlabs()                  { CollectedHeap::resize_all_tlabs(); }
};

#endif // GPGC_HEAP_HPP
