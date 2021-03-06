/*
 * Copyright 2001-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "artaObjects.hpp"
#include "barrierSet.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "collectorCounters.hpp"
#include "gcLocker.hpp"
#include "isGCActiveMark.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "log.hpp"
#include "management.hpp"
#include "memoryService.hpp"
#include "modRefBarrierSet.hpp"
#include "oopTable.hpp"
#include "parallelScavengeHeap.hpp"
#include "psGCAdaptivePolicyCounters.hpp"
#include "psMarkSweep.hpp"
#include "psMarkSweepDecorator.hpp"
#include "psPermGen.hpp"
#include "psScavenge.hpp"
#include "referencePolicy.hpp"
#include "referenceProcessor.hpp"
#include "safepoint.hpp"
#include "sharedHeap.hpp"
#include "symbolTable.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gcLocker.inline.hpp"
#include "handles.inline.hpp"
#include "markWord.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

elapsedTimer        PSMarkSweep::_accumulated_time;
unsigned int        PSMarkSweep::_total_invocations = 0;
jlong               PSMarkSweep::_time_of_last_gc   = 0;
CollectorCounters*  PSMarkSweep::_counters = NULL;

void PSMarkSweep::initialize() {
  MemRegion mr = Universe::heap()->reserved_region();
  _ref_processor = new ReferenceProcessor(mr,
                                          true,    // atomic_discovery
                                          false);  // mt_discovery
  if (!UseParallelOldGC || !VerifyParallelOldWithMarkSweep) {
    _counters = new CollectorCounters("PSMarkSweep", 1);
  }
}

// This method contains all heap specific policy for invoking mark sweep.
// PSMarkSweep::invoke_no_policy() will only attempt to mark-sweep-compact
// the heap. It will do nothing further. If we need to bail out for policy
// reasons, scavenge before full gc, or any other specialized behavior, it
// needs to be added here.
//
// Note that this method should only be called from the vm_thread while
// at a safepoint!
void PSMarkSweep::invoke(bool maximum_heap_compaction) {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at safepoint");
  assert(Thread::current() == (Thread*)VMThread::vm_thread(), "should be in vm thread");
  assert(!Universe::heap()->is_gc_active(), "not reentrant");

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  GCCause::Cause gc_cause = heap->gc_cause();
  PSAdaptiveSizePolicy* policy = heap->size_policy();

  // Before each allocation/collection attempt, find out from the
  // policy object if GCs are, on the whole, taking too long. If so,
  // bail out without attempting a collection.  The exceptions are
  // for explicitly requested GC's.
  if (!policy->gc_time_limit_exceeded() || 
      GCCause::is_user_requested_gc(gc_cause) ||
      GCCause::is_serviceability_requested_gc(gc_cause)) {
    IsGCActiveMark mark;

    if (ScavengeBeforeFullGC) {
      PSScavenge::invoke_no_policy();
    }

    int count = (maximum_heap_compaction)?1:MarkSweepAlwaysCompactCount;
    IntFlagSetting flag_setting(MarkSweepAlwaysCompactCount, count);
    PSMarkSweep::invoke_no_policy(maximum_heap_compaction);
  }
}

// This method contains no policy. You should probably
// be calling invoke() instead. 
void PSMarkSweep::invoke_no_policy(bool clear_all_softrefs) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at a safepoint");
  assert(ref_processor() != NULL, "Sanity");

  if (GC_locker::check_active_before_gc()) {
    return;
  }

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  GCCause::Cause gc_cause = heap->gc_cause();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  PSAdaptiveSizePolicy* size_policy = heap->size_policy();

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();

  // Increment the invocation count
  heap->increment_total_collections(true /* full */);

  // We need to track unique mark sweep invocations as well.
  _total_invocations++;

  AdaptiveSizePolicyOutput(size_policy, heap->total_collections());

  if (PrintHeapAtGC) {
    Universe::print_heap_before_gc();
  }

  // Fill in TLABs
  heap->accumulate_statistics_all_tlabs();
  heap->ensure_parsability(true);  // retire TLABs

  if (VerifyBeforeGC && heap->total_collections() >= VerifyGCStartAt) {
    HandleMark hm;  // Discard invalid handles created during verification
    gclog_or_tty->print(" VerifyBeforeGC:");
    Universe::verify(true);
  }

  // Verify object start arrays
  if (VerifyObjectStartArray && 
      VerifyBeforeGC) {
    old_gen->verify_object_start_array();
    perm_gen->verify_object_start_array();
  }

  // Filled in below to track the state of the young gen after the collection.
  bool eden_empty;
  bool survivors_empty;
  bool young_gen_empty;

  {
    HandleMark hm;
    const bool is_system_gc = gc_cause == GCCause::_java_lang_system_gc;
    // This is useful for debugging but don't change the output the
    // the customer sees.
    const char* gc_cause_str = "Full GC";
    if (is_system_gc && PrintGCDetails) {
      gc_cause_str = "Full GC (System)";
    }
    gclog_or_tty->date_stamp(PrintGC && PrintGCDateStamps);
    TraceCPUTime tcpu(PrintGCDetails, true, gclog_or_tty);
    TraceTime t1(gc_cause_str, PrintGC, !PrintGCDetails, gclog_or_tty);
    TraceCollectorStats tcs(counters());
    TraceMemoryManagerStats tms(true /* Full GC */);

    if (TraceGen1Time) accumulated_time()->start();
  
    // Let the size policy know we're starting
    size_policy->major_collection_begin();

    Threads::gc_prologue();
    
    // Capture heap size before collection for printing.
    size_t prev_used = heap->used();

    // Capture perm gen size before collection for sizing.
    size_t perm_gen_prev_used = perm_gen->used_in_bytes();

    // For PrintGCDetails
    size_t old_gen_prev_used = old_gen->used_in_bytes();
    size_t young_gen_prev_used = young_gen->used_in_bytes();
    
    allocate_stacks();
    
    NOT_PRODUCT(ref_processor()->verify_no_references_recorded());
DerivedPointerTable::clear();

    ref_processor()->enable_discovery();

    mark_sweep_phase1(clear_all_softrefs);

    mark_sweep_phase2();
    
    // Don't add any more derived pointers during phase3
assert(DerivedPointerTable::is_active(),"Sanity");
DerivedPointerTable::set_active(false);

    mark_sweep_phase3();
    
    mark_sweep_phase4();
    
    restore_marks();
    
    deallocate_stacks();
    
    eden_empty = young_gen->eden_space()->is_empty();
    if (!eden_empty) {
      eden_empty = absorb_live_data_from_eden(size_policy, young_gen, old_gen);
    }

    // Update heap occupancy information which is used as
    // input to soft ref clearing policy at the next gc.
    Universe::update_heap_info_at_gc();

    survivors_empty = young_gen->from_space()->is_empty() && 
      young_gen->to_space()->is_empty();
    young_gen_empty = eden_empty && survivors_empty;
    
    BarrierSet* bs = heap->barrier_set();
    if (bs->is_a(BarrierSet::ModRef)) {
      ModRefBarrierSet* modBS = (ModRefBarrierSet*)bs;
      MemRegion old_mr = heap->old_gen()->reserved();
      MemRegion perm_mr = heap->perm_gen()->reserved();
      assert(perm_mr.end() <= old_mr.start(), "Generations out of order");

      if (young_gen_empty) {
        modBS->clear(MemRegion(perm_mr.start(), old_mr.end()));
      } else {
        modBS->invalidate(MemRegion(perm_mr.start(), old_mr.end()));
      }
    }
    
    Threads::gc_epilogue();
    
    DerivedPointerTable::update_pointers();
  
    ref_processor()->enqueue_discovered_references(NULL);

    // Update time of last GC
    reset_millis_since_last_gc();
    
    heap->resize_all_tlabs();
    size_policy->major_collection_end(old_gen->used_in_bytes(), gc_cause);

    if (UseAdaptiveSizePolicy) {

      if (PrintAdaptiveSizePolicy) {
        gclog_or_tty->print("AdaptiveSizeStart: ");
        gclog_or_tty->stamp();
        gclog_or_tty->print_cr(" collection: %d ",
                       heap->total_collections());
	if (Verbose) {
gclog_or_tty->print("old_gen_capacity: %zd young_gen_capacity: %zd"
" perm_gen_capacity: %zd ",
	    old_gen->capacity_in_bytes(), young_gen->capacity_in_bytes(), 
	    perm_gen->capacity_in_bytes());
	}
      }

      // Don't check if the size_policy is ready here.  Let
      // the size_policy check that internally.
      if (UseAdaptiveGenerationSizePolicyAtMajorCollection &&
          ((gc_cause != GCCause::_java_lang_system_gc) ||
            UseAdaptiveSizePolicyWithSystemGC)) {
        // Calculate optimal free space amounts
	assert(young_gen->max_size() > 
	  young_gen->from_space()->capacity_in_bytes() + 
	  young_gen->to_space()->capacity_in_bytes(), 
	  "Sizes of space in young gen are out-of-bounds");
	size_t max_eden_size = young_gen->max_size() - 
	  young_gen->from_space()->capacity_in_bytes() - 
	  young_gen->to_space()->capacity_in_bytes();
        size_policy->compute_generation_free_space(young_gen->used_in_bytes(),
				 young_gen->eden_space()->used_in_bytes(),
                                 old_gen->used_in_bytes(),
                                 perm_gen->used_in_bytes(),
				 young_gen->eden_space()->capacity_in_bytes(),
                                 old_gen->max_gen_size(),
                                 max_eden_size,
                                 true /* full gc*/,
				 gc_cause);

        heap->resize_old_gen(size_policy->calculated_old_free_size_in_bytes());

        // Don't resize the young generation at an major collection.  A
        // desired young generation size may have been calculated but
        // resizing the young generation complicates the code because the
        // resizing of the old generation may have moved the boundary
        // between the young generation and the old generation.  Let the
        // young generation resizing happen at the minor collections.
      }
      if (PrintAdaptiveSizePolicy) {
        gclog_or_tty->print_cr("AdaptiveSizeStop: collection: %d ",
                       heap->total_collections());
      }
    }

    if (UsePerfData) {
      heap->gc_policy_counters()->update_counters();
      heap->gc_policy_counters()->update_old_capacity(
        old_gen->capacity_in_bytes());
      heap->gc_policy_counters()->update_young_capacity(
        young_gen->capacity_in_bytes());
    }

    heap->resize_all_tlabs();

    // We collected the perm gen, so we'll resize it here.
    perm_gen->compute_new_size(perm_gen_prev_used);
    
    if (TraceGen1Time) accumulated_time()->stop();

    if (PrintGC) {
      if (PrintGCDetails) {
        // Don't print a GC timestamp here.  This is after the GC so
        // would be confusing.
        young_gen->print_used_change(young_gen_prev_used);
        old_gen->print_used_change(old_gen_prev_used);
      }
      heap->print_heap_change(prev_used);
      // Do perm gen after heap becase prev_used does
      // not include the perm gen (done this way in the other
      // collectors).
      if (PrintGCDetails) {
        perm_gen->print_used_change(perm_gen_prev_used);
      }
    }

    // Track memory usage and detect low memory
    MemoryService::track_memory_usage();
    heap->update_counters();

    if (PrintGCDetails) {
      if (size_policy->print_gc_time_limit_would_be_exceeded()) {
        if (size_policy->gc_time_limit_exceeded()) {
          gclog_or_tty->print_cr("	GC time is exceeding GCTimeLimit "
"of %ld%%",GCTimeLimit);
        } else {
          gclog_or_tty->print_cr("	GC time would exceed GCTimeLimit "
"of %ld%%",GCTimeLimit);
	}
      }
      size_policy->set_print_gc_time_limit_would_be_exceeded(false);
    }
  }

  if (VerifyAfterGC && heap->total_collections() >= VerifyGCStartAt) {
    HandleMark hm;  // Discard invalid handles created during verification
    gclog_or_tty->print(" VerifyAfterGC:");
    Universe::verify(false);
  }

  // Re-verify object start arrays
  if (VerifyObjectStartArray && 
      VerifyAfterGC) {
    old_gen->verify_object_start_array();
    perm_gen->verify_object_start_array();
  }

  NOT_PRODUCT(ref_processor()->verify_no_references_recorded());

  if (PrintHeapAtGC) {
    Universe::print_heap_after_gc();
  }
}

bool PSMarkSweep::absorb_live_data_from_eden(PSAdaptiveSizePolicy* size_policy,
					     PSYoungGen* young_gen,
					     PSOldGen* old_gen) {
  MutableSpace* const eden_space = young_gen->eden_space();
  assert(!eden_space->is_empty(), "eden must be non-empty");
  assert(young_gen->virtual_space()->alignment() ==
	 old_gen->virtual_space()->alignment(), "alignments do not match");

  if (!(UseAdaptiveSizePolicy && UseAdaptiveGCBoundary)) {
    return false;
  }

  // Both generations must be completely committed.
  if (young_gen->virtual_space()->uncommitted_size() != 0) {
    return false;
  }
  if (old_gen->virtual_space()->uncommitted_size() != 0) {
    return false;
  }

  // Figure out how much to take from eden.  Include the average amount promoted
  // in the total; otherwise the next young gen GC will simply bail out to a
  // full GC.
  const size_t alignment = old_gen->virtual_space()->alignment();
  const size_t eden_used = eden_space->used_in_bytes();
  const size_t promoted = (size_t)(size_policy->avg_promoted()->padded_average());
  const size_t absorb_size = align_size_up(eden_used + promoted, alignment);
  const size_t eden_capacity = eden_space->capacity_in_bytes();

  if (absorb_size >= eden_capacity) {
    return false; // Must leave some space in eden.
  }

  const size_t new_young_size = young_gen->capacity_in_bytes() - absorb_size;
  if (new_young_size < young_gen->min_gen_size()) {
    return false; // Respect young gen minimum size.
  }

  if (TraceAdaptiveGCBoundary && Verbose) {
    gclog_or_tty->print(" absorbing " SIZE_FORMAT "K:  "
			"eden " SIZE_FORMAT "K->" SIZE_FORMAT "K "
			"from " SIZE_FORMAT "K, to " SIZE_FORMAT "K "
			"young_gen " SIZE_FORMAT "K->" SIZE_FORMAT "K ",
			absorb_size / K,
			eden_capacity / K, (eden_capacity - absorb_size) / K,
			young_gen->from_space()->used_in_bytes() / K,
			young_gen->to_space()->used_in_bytes() / K,
			young_gen->capacity_in_bytes() / K, new_young_size / K);
  }

  // Fill the unused part of the old gen.
  MutableSpace* const old_space = old_gen->object_space();
  MemRegion old_gen_unused(old_space->top(), old_space->end());

  // If the unused part of the old gen cannot be filled, skip
  // absorbing eden.
  if (old_gen_unused.word_size() < SharedHeap::min_fill_size()) {
    return false;
  }

  if (!old_gen_unused.is_empty()) {
old_gen_unused.fill();
  }

  // Take the live data from eden and set both top and end in the old gen to
  // eden top.  (Need to set end because reset_after_change() mangles the region
  // from end to virtual_space->high() in debug builds).
  HeapWord* const new_top = eden_space->top();
  old_gen->virtual_space()->expand_into(young_gen->virtual_space(),
					absorb_size);
  young_gen->reset_after_change();
  old_space->set_top(new_top);
  old_space->set_end(new_top);
  old_gen->reset_after_change();

  // Update the object start array for the filler object and the data from eden.
  ObjectStartArray* const start_array = old_gen->start_array();
  HeapWord* const start = old_gen_unused.start();
  for (HeapWord* addr = start; addr < new_top; addr += oop(addr)->size()) {
    start_array->allocate_block(addr);
  }

  // Could update the promoted average here, but it is not typically updated at
  // full GCs and the value to use is unclear.  Something like
  // 
  // cur_promoted_avg + absorb_size / number_of_scavenges_since_last_full_gc.

  size_policy->set_bytes_absorbed_from_eden(absorb_size);
  return true;
}

void PSMarkSweep::allocate_stacks() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSYoungGen* young_gen = heap->young_gen();

  MutableSpace* to_space = young_gen->to_space();
  _preserved_marks = (PreservedMark*)to_space->top();
  _preserved_count = 0;

  // We want to calculate the size in bytes first.
  _preserved_count_max  = pointer_delta(to_space->end(), to_space->top(), sizeof(jbyte));
  // Now divide by the size of a PreservedMark
  _preserved_count_max /= sizeof(PreservedMark);

  // Preallocate these now. The growable arrays are resource objects,
  // even though they store their data in the c-heap. We do not want
  // to risk having an unaccounted for ResourceMark on the stack during
  // dynamic allocation.
_preserved_mark_stack=new(ResourceObj::C_HEAP)GrowableArray<markWord*>(40,true);
_preserved_oop_stack=new(ResourceObj::C_HEAP)GrowableArray<uintptr_t>(40,true);

  _marking_stack = new (ResourceObj::C_HEAP) GrowableArray<oop>(4000, true);

  int size = SystemDictionary::number_of_classes() * 2;
  _revisit_klass_stack = new (ResourceObj::C_HEAP) GrowableArray<Klass*>(size, true);
}


void PSMarkSweep::deallocate_stacks() {
  if (_preserved_oop_stack) {
    delete _preserved_mark_stack;
    _preserved_mark_stack = NULL;
    delete _preserved_oop_stack;
    _preserved_oop_stack = NULL;
  }

  delete _marking_stack;
  delete _revisit_klass_stack;
}

void PSMarkSweep::mark_sweep_phase1(bool clear_all_softrefs) {
  // Recursively traverse all live objects and mark them
  //LoggerMark m(NOTAG, Log::M_GC, "1 mark object");
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails && Verbose, true, gclog_or_tty);
  TraceTime tm("phase 1", m.enabled(), true, m.stream());
m.enter();
  trace(" 1");

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  // General strong roots.
  Universe::oops_do(mark_and_push_closure());
  ReferenceProcessor::oops_do(mark_and_push_closure());
  JNIHandles::oops_do(mark_and_push_closure());   // Global (strong) JNI handles
  Threads::oops_do(mark_and_push_closure());
  ObjectSynchronizer::oops_do(mark_and_push_closure());
ArtaObjects::oops_do(mark_and_push_closure());//Only strong roots during scavenge!
  Management::oops_do(mark_and_push_closure());
  JvmtiExport::oops_do(mark_and_push_closure());
  SystemDictionary::always_strong_oops_do(mark_and_push_closure());
  vmSymbols::oops_do(mark_and_push_closure());

  // Flush marking stack.
  follow_stack();

  // Process reference objects found during marking

  // Skipping the reference processing for VerifyParallelOldWithMarkSweep 
  // affects the marking (makes it different).
  {
    ReferencePolicy *soft_ref_policy;
    if (clear_all_softrefs) {
      soft_ref_policy = new AlwaysClearPolicy();
    } else {
      soft_ref_policy = new LRUMaxHeapPolicy();
    }
    assert(soft_ref_policy != NULL,"No soft reference policy");
    ref_processor()->process_discovered_references(
      soft_ref_policy, is_alive_closure(), mark_and_push_closure(),
      follow_stack_closure(), NULL);
  }

  // Follow system dictionary roots and unload classes
  bool purged_class = SystemDictionary::do_unloading(is_alive_closure());

  // Update subklass/sibling/implementor links of live klasses
  follow_weak_klass_links();
  assert(_marking_stack->is_empty(), "just drained");

  // Visit symbol and interned string tables and delete unmarked oops
  SymbolTable::unlink(is_alive_closure());
  StringTable::unlink(is_alive_closure());

ArtaObjects::unlink(is_alive_closure());
CodeCache::unlink(is_alive_closure());
CodeCacheOopTable::unlink(is_alive_closure());

  assert(_marking_stack->is_empty(), "stack should be empty by now");
}


void PSMarkSweep::mark_sweep_phase2() {
  //LoggerMark m(NOTAG, Log::M_GC, "2 compute new addresses");
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails && Verbose, true, gclog_or_tty);
  TraceTime tm("phase 2", m.enabled(), true, m.stream());
m.enter();
  trace("2");

  // Now all live objects are marked, compute the new object addresses.

  // It is imperative that we traverse perm_gen LAST. If dead space is
  // allowed a range of dead object may get overwritten by a dead int
  // array. If perm_gen is not traversed last a klassOop may get
  // overwritten. This is fine since it is dead, but if the class has dead
  // instances we have to skip them, and in order to find their size we
  // need the klassOop! 
  //
  // It is not required that we traverse spaces in the same order in
  // phase2, phase3 and phase4, but the ValidateMarkSweep live oops
  // tracking expects us to do so. See comment under phase4.

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();

  // Begin compacting into the old gen
  PSMarkSweepDecorator::set_destination_decorator_tenured();

  // This will also compact the young gen spaces.
  old_gen->precompact();

  // We can't unlink unmarked Klasses in the KlassTable until after we've
  // visited all objects which might be instances of dead Klasses. This is
  // because we'll need to look-up their klassOop in the KlassTable via their
  // klassID in the markWord to get their size. Also, we can't do this after we
  // prepare perm gen for compaction since this clears the GC mark in the
  // markWord of all Klasses which aren't moving so all these Klasses would
  // appear dead.
KlassTable::unlink(&is_alive);

  // Compact the perm gen into the perm gen
  PSMarkSweepDecorator::set_destination_decorator_perm_gen();

  perm_gen->precompact();
}

// This should be moved to the shared markSweep code!
class PSAlwaysTrueClosure: public BoolObjectClosure {
public:
  void do_object(oop p) { ShouldNotReachHere(); }
  bool do_object_b(oop p) { return true; }
};
static PSAlwaysTrueClosure always_true;

void PSMarkSweep::mark_sweep_phase3() {
  // Adjust the pointers to reflect the new locations
  //LoggerMark m(NOTAG, Log::M_GC, "3 adjust pointers");
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails && Verbose, true, gclog_or_tty);
  TraceTime tm("phase 3", m.enabled(), true, m.stream());
m.enter();
  trace("3");

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();

  // General strong roots.
Universe::oops_do(adjust_pointer_closure());
ReferenceProcessor::oops_do(adjust_pointer_closure());
JNIHandles::oops_do(adjust_pointer_closure());//Global (strong) JNI handles
Threads::oops_do(adjust_pointer_closure_skipping_CodeCache());
ObjectSynchronizer::oops_do(adjust_pointer_closure());
Management::oops_do(adjust_pointer_closure());
JvmtiExport::oops_do(adjust_pointer_closure());
  // SO_AllClasses
SystemDictionary::oops_do(adjust_pointer_closure());
vmSymbols::oops_do(adjust_pointer_closure());

  // Now adjust pointers in remaining weak roots.  (All of which should
  // have been cleared if they pointed to non-surviving objects.)
  // Global (weak) JNI handles
JNIHandles::weak_oops_do(&always_true,adjust_pointer_closure());

SymbolTable::oops_do(adjust_pointer_closure());
StringTable::oops_do(adjust_pointer_closure());
ref_processor()->weak_oops_do(adjust_pointer_closure());
PSScavenge::reference_processor()->weak_oops_do(adjust_pointer_closure());
  // NOTE! ArtaObjects are not normal roots. During scavenges, they are
  // considered strong roots. During a mark sweep they are weak roots.
ArtaObjects::oops_do(adjust_pointer_closure());
  // Visit pointers NOT visited by the Threads walk.  Normally the Threads
  // walk would visit all the live CodeBlobs reachable from the stack, but it
  // reaches the same blobs many times... and attempts to adjust_pointers
  // many times.  Arrange to adjust the pointers just once.
CodeCache::MSB_oops_do(adjust_pointer_closure());
CodeCacheOopTable::oops_do(adjust_pointer_closure());

  adjust_marks();

  young_gen->adjust_pointers();
  old_gen->adjust_pointers();
  perm_gen->adjust_pointers();

  // Don't adjust the pointers in the KlassTable until last since it must be
  // consulted while adjusting all other pointers.
KlassTable::oops_do(adjust_pointer_closure());
}

void PSMarkSweep::mark_sweep_phase4() {
  //LoggerMark m(NOTAG, Log::M_GC, "4 compact heap");
  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails && Verbose, true, gclog_or_tty);
  TraceTime tm("phase 4", m.enabled(), true, m.stream());
m.enter();
  trace("4");

  // All pointers are now adjusted, move objects accordingly

  // It is imperative that we traverse perm_gen first in phase4. All
  // classes must be allocated earlier than their instances, and traversing
  // perm_gen first makes sure that all klassOops have moved to their new
  // location before any instance does a dispatch through it's klass!
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();

  perm_gen->compact();
  old_gen->compact();
  young_gen->compact();
}

jlong PSMarkSweep::millis_since_last_gc() { 
  jlong ret_val = os::javaTimeMillis() - _time_of_last_gc; 
  // XXX See note in genCollectedHeap::millis_since_last_gc().
  if (ret_val < 0) {
    NOT_PRODUCT(warning("time warp: %d", ret_val);)
    return 0;
  }
  return ret_val;
}

void PSMarkSweep::reset_millis_since_last_gc() { 
  _time_of_last_gc = os::javaTimeMillis(); 
}
