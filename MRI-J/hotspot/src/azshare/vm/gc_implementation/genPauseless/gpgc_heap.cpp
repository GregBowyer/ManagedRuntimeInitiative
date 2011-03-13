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


#include "arguments.hpp"
#include "barrierSet.hpp"
#include "copy.hpp"
#include "cycleCounts.hpp"
#include "gpgc_cardTable.hpp"
#include "gpgc_debug.hpp"
#include "gpgc_gcManagerMark.hpp"
#include "gpgc_gcManagerNewFinal.hpp"
#include "gpgc_gcManagerNewReloc.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldReloc.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_marker.hpp"
#include "gpgc_metadata.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_operation.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_thread.hpp"
#include "gpgc_verifyClosure.hpp"
#include "java.hpp"
#include "jniHandles.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "safepoint.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "handles.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

GPGC_Heap*        GPGC_Heap::_heap                                = NULL;
PGCTaskManager*   GPGC_Heap::_new_gc_task_manager                 = NULL;
PGCTaskManager*   GPGC_Heap::_old_gc_task_manager                 = NULL;
jlong             GPGC_Heap::_gpgc_time_stamp_promotion_threshold = 0;
jint              GPGC_Heap::_system_gc_is_pending                = 0;
bool              GPGC_Heap::_safe_to_iterate                     = true; // Used for JVMTI heap iteration


// This method initializes the various static classes involved in UseGenPauselessGC
// that must be setup prior to assembly code being generated.
void GenPauselessHeap_init()
{

  if ( UseGenPauselessGC ) {
GPGC_Layout::initialize();
GPGC_ReadTrapArray::initialize();
  }
}


GPGC_Heap::GPGC_Heap()
  : _gc_stats()
{}


jint GPGC_Heap::initialize()
{
  // **** TEMPORARY HACK **** //
  //
  // For GPGC bringup efforts, we're wiring some flags to off, so not everyone in the
  // team will have to track the right combinations of options needed to use GPGC.
  {
    ResourceMark rm;
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGC || PrintGCDetails, false, gclog_or_tty);

    if ( GPGCPausePreventionMemory != 0 ) {
      GPGCPausePreventionMemory = 0;
      if ( PrintGCDetails ) {
        GCLogMessage::log_b(m.enabled(), m.stream(), "GCH :           Forcing GPGCPausePreventionMemory to 0.");
      }
    }
  }
 

#ifdef PRODUCT
  // In PRODUCT mode, force the heap verification options to false:
  GPGCVerifyHeap = false;
  GPGCAuditTrail = false;
#endif // PRODUCT

#ifdef ASSERT
  // In DEBUG modes, force capacity verification to true:
  GPGCVerifyCapacity = true;
#endif // ASSERT

  if ( MaxTLABObjectAllocationWords+1 != GPGC_Layout::mid_space_min_object_words() ) {
    MaxTLABObjectAllocationWords = GPGC_Layout::mid_space_min_object_words() - 1;
    GCLogMessage::log_b(PrintGCDetails, gclog_or_tty, "GCH :           Forcing MaxTLABObjectAllocationWords to %lu.", MaxTLABObjectAllocationWords);
  }

  /*
   *  To batch, or not to batch, that is the question.
   *  Whether 'tis nobler in the mind to suffer
   *  The slings and arrows of long pause times,
   *  Or to take arms against a lousy virtual memory interface,
   *  And by opposing end them? To run: to scale.
   */
  if ( ! os::use_azmem() )         { BatchedMemoryOps = false; }
  if ( ! BatchedMemoryOps ) { PageHealing      = false; }

  // Just in case: Make sure we're not trying to size something off parallel GC's worker thread count:
  ParallelGCThreads = 0;

  if ( IncrementalObjInitThresholdWords < GPGC_Layout::mid_space_min_object_words() ) {
    // TODO: Someday we want to make it so that objects that are in the mid-space size range
    // TODO: won't get allocated in small space TLABs even if they fit.  That will avoid
    // TODO: the relocation overhead of small space on larger objects, and also enable us to
    // TODO: do incremental zeroing of all mid space sized objects.

    // Currently, GPGC only supports incremental object zeroing of mid-space and large-space objects.
    IncrementalObjInitThresholdWords = GPGC_Layout::mid_space_min_object_words();
  }

  assert( (badJNIHandleVal & JNIHandles::JNIWeakHandleTag) == 0,
"It's best if JNI weak global handle tags don't clash with the badJNIHandleVal.");
  assert( JNIHandles::JNIWeakHandleTag < 0x8, "JNIWeakHandleTag shouldn't interfere with aligned pointers");

  // Set the number of GC threads, if not otherwise set.  It's permissible to set on the
  // command line either GPGCThreads or (GenPauselessNewThreads && GenPauselessOldThreads),
  // but not both.  It's not permissible to set only one of GenPauselessNewThreads and 
  // GenPauselessOldThreads.

  // TODO: maw: number of GC threads ought to be set adapatively, not by command line options.

  // The argument parsing should have already checked for the legal thread option combinations:
  if ( GPGCThreads ) {
    if ( GenPauselessNewThreads ) {
vm_exit_during_initialization("Cannot set GenPauselessNewThreads when setting GPGCThreads");
    }
    if ( GenPauselessOldThreads ) {
vm_exit_during_initialization("Cannot set GenPauselessOldThreads when setting GPGCThreads");
    }
  } else {
    if ( GenPauselessNewThreads && !GenPauselessOldThreads ) {
vm_exit_during_initialization("Must set GenPauselessOldThreads when setting GenPauselessNewThreads");
    }
    if ( GenPauselessOldThreads && !GenPauselessNewThreads ) {
vm_exit_during_initialization("Must set GenPauselessNewThreads when setting GenPauselessOldThreads");
    }
  }

  if ( GPGCThreads == 0 ) {
    if ( GenPauselessNewThreads ) {
      GPGCThreads = GenPauselessNewThreads + GenPauselessOldThreads;
    } else {
      GPGCThreads = 2;
      GenPauselessNewThreads = 1;
      GenPauselessOldThreads = 1;
    }
  }
  else if ( GPGCThreads < 2 ) {
    // Silently force GPGCThreads to be at least 2 threads: 1 New GC, 1 Old GC
    GPGCThreads = 2;
  }

  if ( GenPauselessNewThreads == 0 ) {
    GenPauselessOldThreads = (uintx)(float(GPGCThreads) * 0.4);
    GenPauselessNewThreads = GPGCThreads - GenPauselessOldThreads;
    if ( GenPauselessNewThreads < 1 ) {
      GenPauselessNewThreads = 1;
      GenPauselessOldThreads --;
    }
    if ( GenPauselessOldThreads < 1 ) {
      GenPauselessOldThreads = 1;
      GenPauselessNewThreads --;
    }
    // The formula give these thread totals for the given GPGCThreads number:
    // For total threads:  10   9   8   7   6   5   4   3   2
    //            New GC:   6   6   5   5   4   3   3   2   1
    //            Old GC:   4   3   3   2   2   2   1   1   1
  }

  if ( GPGCOldHeadroomUsedPercent < 0 ) {
    GPGCOldHeadroomUsedPercent = 0;
  }
  else if ( GPGCOldHeadroomUsedPercent > 100 ) {
    GPGCOldHeadroomUsedPercent = 100;
  }
  
guarantee(GPGCThreads>1,"GPGCThreads ended up less than 2!");
guarantee(GenPauselessNewThreads>0,"GenPauselessNewThreads ended up less than 1!");
guarantee(GenPauselessOldThreads>0,"GenPauselessOldThreads ended up less than 1!");
  guarantee(GPGCThreads==(GenPauselessNewThreads+GenPauselessOldThreads), "GenPauseless thread totals don't add up.");


  if (PrintGCDetails) {
gclog_or_tty->print_cr("GenPauseless GC thread counts:");
gclog_or_tty->print_cr("\tNew GC threads:            %ld",GenPauselessNewThreads);
gclog_or_tty->print_cr("\tOld GC threads:            %ld",GenPauselessOldThreads);
    gclog_or_tty->print_cr("\tCard-Mark Cleaner threads: %d", 1);
    gclog_or_tty->print_cr("\tPlatform:                  %s", os::arch_version());
  }

  // Perm gen relocation isn't enabled until after Universe::fixup_mirrors() iterates
  // over the perm gen objects.
  _saved_GPGCNoPermRelocation = GPGCNoPermRelocation;
  GPGCNoPermRelocation        = true;

  _actual_collections         = 0;
  _actual_new_collections     = 0;
  _actual_old_collections     = 0;

  _gpgc_time_stamp_promotion_threshold = GPGCTimeStampPromotionThreshold * os::elapsed_frequency();

  // GPGC doesn't care what the -Xms parameter was, but we sanity check that it's not bigger
  // than -Xmx, just to catch user errors.
  if ( MaxHeapSize < Arguments::min_heap_size() ) {
vm_exit_during_initialization("Minimum heap size (-Xms) specified to be larger than max heap size (-Xmx).");
  }

  // User's wont know the required alignment, so we just align up the parameters they supply:
  MaxHeapSize = align_size_up(MaxHeapSize, BytesPerGPGCPage);
  MaxPermSize = align_size_up(MaxPermSize, BytesPerGPGCPage);

  // TODO: Stop using MaxPermSize for GPGC:
  size_t max_heap_size = MaxHeapSize + MaxPermSize;

  // Redundent sanity check???
  Universe::check_alignment(max_heap_size, BytesPerGPGCPage, "maximum heap");

  // Reserve address space, and initialize the GPGC_Space.
  // we only initialize this but dont reserve, 
  // GPGC splits the virtual address into the discontiguous segments
  // reserve each segment separately.
  _reserved = _reserved_heap = MemRegion(GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_heap_range),
                                         GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_heap_range));
  _reserved_heap_mirror = MemRegion(GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_heap_mirror),
                                    GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_heap_mirror));
  _reserved_heap_structures = MemRegion(GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_structures),
                                        GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_structures));

if(os::use_azmem()){
    char* reserved_heap_start = (char*) _reserved_heap.start();
    guarantee((intptr_t) reserved_heap_start == __GPGC_HEAP_START_ADDR__, "GPGC heap memory map addr mismatch");
    if ( reserved_heap_start != os::reserve_memory(_reserved_heap.byte_size(), reserved_heap_start, true, true, MultiMapMetaData) ) {
vm_exit_during_initialization("Unable to reserve object heap address space");
    }

    if(MultiMapMetaData) {
      assert(objectRef::nmt_bits+objectRef::nmt_shift == objectRef::space_shift,
"Space and NMT bits aren't adjacent");

      if( !os::alias_memory_reserve((char*)_reserved.start(), objectRef::nmt_shift,
                                    objectRef::nmt_bits + objectRef::space_bits,
                                    _reserved.byte_size()) )
      {
vm_exit_during_initialization("Unable to Multimap space covered by space-id+nmt bits");
      }
    }

    char* reserved_heap_mirror_start = (char*) _reserved_heap_mirror.start();
    guarantee((intptr_t) reserved_heap_mirror_start == __GPGC_HEAP_MIRROR_START_ADDR__, "GPGC heap memory map addr mismatch");
    if ( reserved_heap_mirror_start != os::reserve_memory(_reserved_heap_mirror.byte_size(), reserved_heap_mirror_start, true, true) ) {
vm_exit_during_initialization("Unable to reserve object heap mirror address space");
    }
    char* reserved_heap_structures_start = (char*) _reserved_heap_structures.start();
    guarantee((intptr_t) reserved_heap_structures_start == __GPGC_HEAP_STRUCTURES_START_ADDR__, "GPGC heap memory map addr mismatch");
    if ( reserved_heap_structures_start != os::reserve_memory(_reserved_heap_structures.byte_size(), reserved_heap_structures_start, true, true) ) {
vm_exit_during_initialization("Unable to reserve address space for object heap structures");
    }
  } else {
  // TODO: Stock linux has no ability to reserve memory.  For now we just don't reserve,
  // and hope no one shows up in our memory space.  Future mmaps we do will silently stop
  // any other memory that shows up in our space!  Someday, we want to implement the
  // Tall version of GPGC, which allocates largely static memory for the 3 spaces.
  }

  long max_heap_pages     = max_heap_size >> LogBytesPerGPGCPage;
  long max_sideband_pages = max_heap_pages * GPGCSidebandPagesPercent / 100;

  GPGC_Space::initialize(max_heap_pages);
GPGC_Metadata::initialize();
  GPGC_PageRelocation::initialize_relocation_space(max_sideband_pages);

  // Save the final heap size numbers
  _max_heap_size_specified  = max_heap_size;
  _peak_heap_size_allocated = max_heap_size;

  _last_gc_live_bytes = 0;

  if ( ProfileMemoryTime ) {
    CycleCounts::reset_all();
  }

  _barrier_set = new GPGC_CardTable();
if(_barrier_set==NULL){
vm_exit_during_initialization("Could not create barrier set");
  }
  oopDesc::set_bs(_barrier_set);

  _heap = this;

  // This is a hack: Feed constants through a logging function in a different
  // file, to keep the compiler from optimizing out constant variables, so we
  // can print them out in the debugger:
  GPGC_Debug::log_constant("LogBytesPerCacheLine",                     &LogBytesPerCacheLine);
  GPGC_Debug::log_constant("LogPagesPerGPGCPage",                      &LogPagesPerGPGCPage);
  GPGC_Debug::log_constant("LogPagesPerMidSpaceBlock",                 &LogPagesPerMidSpaceBlock);
  GPGC_Debug::log_constant("LogBytesPerMidSpaceBlock",                 &LogBytesPerMidSpaceBlock);
  GPGC_Debug::log_constant("LogBytesMidSpaceMinObjectSize",            &LogBytesMidSpaceMinObjectSize);
  GPGC_Debug::log_constant("LogBytesLargeSpaceMinObjectSize",          &LogBytesLargeSpaceMinObjectSize);
  GPGC_Debug::log_constant("GPGC_LogBytesMidSpaceObjectSizeAlignment", &GPGC_LogBytesMidSpaceObjectSizeAlignment);
  GPGC_Debug::log_constant("start_of_small_space",                     &GPGC_Layout::start_of_small_space);
  GPGC_Debug::log_constant("end_of_small_space",                       &GPGC_Layout::end_of_small_space);
  GPGC_Debug::log_constant("start_of_mid_space",                       &GPGC_Layout::start_of_mid_space);
  GPGC_Debug::log_constant("end_of_mid_space",                         &GPGC_Layout::end_of_mid_space);
  GPGC_Debug::log_constant("start_of_large_space",                     &GPGC_Layout::start_of_large_space);
  GPGC_Debug::log_constant("end_of_large_space",                       &GPGC_Layout::end_of_large_space);
  GPGC_Debug::log_constant("start_of_heap_range",                      &GPGC_Layout::start_of_heap_range);
  GPGC_Debug::log_constant("end_of_heap_range",                        &GPGC_Layout::end_of_heap_range);
  GPGC_Debug::log_constant("start_of_cardmark_bitmap",                 &GPGC_Layout::start_of_cardmark_bitmap);
  GPGC_Debug::log_constant("end_of_cardmark_bitmap",                   &GPGC_Layout::end_of_cardmark_bitmap);
  GPGC_Debug::log_constant("start_of_cardmark_bytemap",                &GPGC_Layout::start_of_cardmark_bytemap);
  GPGC_Debug::log_constant("end_of_cardmark_bytemap",                  &GPGC_Layout::end_of_cardmark_bytemap);
  GPGC_Debug::log_constant("start_of_page_info",                       &GPGC_Layout::start_of_page_info);
  GPGC_Debug::log_constant("end_of_page_info",                         &GPGC_Layout::end_of_page_info);
  GPGC_Debug::log_constant("start_of_page_audit",                      &GPGC_Layout::start_of_page_audit);
  GPGC_Debug::log_constant("end_of_page_audit",                        &GPGC_Layout::end_of_page_audit);
  GPGC_Debug::log_constant("start_of_object_forwarding",               &GPGC_Layout::start_of_object_forwarding);
  GPGC_Debug::log_constant("end_of_object_forwarding",                 &GPGC_Layout::end_of_object_forwarding);
  GPGC_Debug::log_constant("start_of_structures",                      &GPGC_Layout::start_of_structures);
  GPGC_Debug::log_constant("end_of_structures",                        &GPGC_Layout::end_of_structures);

  return JNI_OK;
}


void GPGC_Heap::post_initialize(){
  assert(GPGC_Layout::addr_to_PageNum(Universe::klassKlassObj()) == GPGC_Layout::start_of_small_space,
"Initial perm gen objects are expected to be in the first page of the small space");


GPGC_GCManagerMark::initialize();

GPGC_NewCollector::initialize();
GPGC_OldCollector::initialize();

GPGC_GCManagerNewStrong::initialize();
GPGC_GCManagerNewFinal::initialize();
GPGC_GCManagerOldStrong::initialize();
GPGC_GCManagerOldFinal::initialize();
GPGC_GCManagerNewReloc::initialize();
GPGC_GCManagerOldReloc::initialize();

GPGC_HistoricalCycleStats::initialize();
GCLogMessage::initialize();
}


class GPGC_SetupGCTaskThreadClosure:public ThreadClosure{
 private:
  long _type;
  long _number;
 public:
  GPGC_SetupGCTaskThreadClosure(long t, long n) : _type(t), _number(n) {}
  virtual void do_thread(Thread *thread) {
    assert0( thread->is_GC_task_thread() );
    GCTaskThread* gtt = (GCTaskThread*) thread;
    gtt->set_thread_type  (_type);
    gtt->set_thread_number(_number++);
    GPGC_PageBudget::preallocate_page(thread);
  }
};


void GPGC_Heap::final_initialize() {
  // The GPGC_Thread and GCTask threads can't be started until the trap_table has been constructed.

  _new_gc_task_manager = PGCTaskManager::create(GenPauselessNewThreads);
  _old_gc_task_manager = PGCTaskManager::create(GenPauselessOldThreads);

  GPGC_Thread::static_initialize();
  GPGC_Thread::start(GPGC_Collector::NewCollector);
  GPGC_Thread::start(GPGC_Collector::OldCollector);

  {
    GPGC_SetupGCTaskThreadClosure stc(GPGC_Collector::NewCollector, 0);
    _new_gc_task_manager->threads_do(&stc);
  }

  {
    GPGC_SetupGCTaskThreadClosure stc(GPGC_Collector::OldCollector, GenPauselessNewThreads);
    _old_gc_task_manager->threads_do(&stc);
  }

GPGC_VerifyClosure::initialize();
}


void genPauselessThreads_init() {
  if (UseGenPauselessGC) {
    GPGC_Heap::heap()->final_initialize();
  }
}


void GPGC_Heap::tlab_allocation_mark_new_oop(HeapWord* obj) {
assert0(obj);
  assert0(!SafepointSynchronize::is_at_safepoint());
  GPGC_Marker::new_gen_mark_new_obj(obj);
}


size_t GPGC_Heap::compute_tlab_size(Thread *, size_t requested_free_size, size_t obj_size, size_t alignment_reserve) {
  // Force the application to always use full page TLABs.
  size_t new_tlab_size    = WordsPerGPGCPage;
  size_t aligned_obj_size = align_object_size(obj_size);  // word aligned

  // Make sure there's enough room for object and filler int[].
  // TODO: Don't think we really need filler arrays at the end of GPGC TLABs.
  size_t obj_plus_filler_size = aligned_obj_size + alignment_reserve;

  if ( new_tlab_size < obj_plus_filler_size ) {
    return 0;
  }

  return new_tlab_size;
}


HeapWord*GPGC_Heap::allocate_new_tlab(size_t size){
  if (PrintTLAB) {
    gclog_or_tty->print_cr("Alloc %ld word TLAB for thread " PTR_FORMAT, size, Thread::current());
  }

  bool gc_overhead_limit_was_exceeded;
  HeapWord* result = mem_allocate(size, false, true/*is_tlab*/, &gc_overhead_limit_was_exceeded);

  return result;
}


HeapWord* GPGC_Heap::mem_allocate(size_t size, bool is_large_noref, bool is_tlab, bool* gc_overhead_limit_was_exceeded) {
  // NOTE: If you make the VMThread start allocating in the object heap, the PauselessGC algorithm
  //       will deadlock when the VMThread fails to allocate and waits for a collection cycle, while
  //       the GPGC_OldCollector will be waiting for the VMThread to come to a safepoint for a GC trap
  //       phase change and ref buffer flush.
assert(Thread::current()->is_Java_thread(),"Only a JavaThread can directly allocate in the object heap");

  HeapWord* result = GPGC_Space::new_gen_allocate(size, is_tlab);

  if ( result == NULL ) {
    // need to complete at least two cycles before determining that OOM can be thrown
    long loop_until = actual_old_collections() + MAX(2, GPGCFailedAllocRetries); 

    while (true) {
      ResourceMark rm;
      BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
      TimeDetailTracer t(m.enabled(), false, false, true, m.stream(),
"Thread 0x%lx asking GC to alloc %ld new words: ",
                         Thread::current(),
                         size);

      GPGC_NewAllocOperation op(size, is_tlab);
      GPGC_Thread::run_alloc_operation(&op);

      result = op.result();

      if ( result != NULL ) {
        if ( actual_new_collections() == op.result_gc_count() ) {
t.print("GC alloc succeeded");
          break;
        } else {
t.print("GC alloc succeeded on wrong cycle");
        }
      } else {
t.print("GC alloc failed");
      }

      // Retry the allocation myself.
      result = GPGC_Space::new_gen_allocate(size, is_tlab);
      if ( result != NULL ) {
t.print(": local retry succeeded");
        break;
      }

      if ( actual_old_collections() >= loop_until ) {
assert(result==NULL,"should have succeeded earlier");
t.print(": enough retries, alloc failing");
        break;
      } else {
t.print(": retrying");
      }
    }
  }

  if ( result != NULL ) {
    DEBUG_ONLY( PageNum result_page = GPGC_Layout::addr_to_BasePageForSpace(result); )
    assert(this->is_in(result), "result not in heap");
    assert(GPGC_PageInfo::page_info(result_page)->just_gen() == GPGC_PageInfo::NewGen, "Alloced in wrong generation");

    if ( ! is_tlab ) {
      GPGC_Marker::new_gen_mark_new_obj(result);
    }
  }

  return result;
}


HeapWord*GPGC_Heap::permanent_mem_allocate(size_t size)
{
  // NOTE: If you make the VMThread start allocating in the object heap, the PauselessGC algorithm
  //       will deadlock when the VMThread fails to allocate and waits for a collection cycle, while
  //       the GPGC_OldCollector will be waiting for the VMThread to come to a safepoint for a GC trap
  //       phase change and ref buffer flush.
assert(Thread::current()->is_Java_thread(),"Only a JavaThread can allocate in the object heap");

  HeapWord* result = GPGC_Space::perm_gen_allocate(size);

  if ( result == NULL ) {
    // need to complete at least two cycles before determining that OOM can be thrown
    long loop_until = actual_old_collections() + MAX(2, GPGCFailedAllocRetries);

    while (true) {
      ResourceMark rm;
      BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGCDetails, false, gclog_or_tty);
      TimeDetailTracer t(m.enabled(), false, false, true, m.stream(),
"Thread 0x%lx asking GC to alloc %ld perm words: ",
                         Thread::current(),
                         size);

      GPGC_PermAllocOperation op(size);
      GPGC_Thread::run_alloc_operation(&op);

      result = op.result();

      if ( result!=NULL ) {
        if ( actual_old_collections()==op.result_gc_count() ) {
t.print("GC alloc succeeded");
          break;
        } else {
t.print("GC alloc succeeded on wrong cycle");
        }
      } else {
t.print("GC alloc failed");
      }

      // Retry the allocation myself
      result = GPGC_Space::perm_gen_allocate(size);
      if ( result != NULL ) {
t.print(": local retry succeeded");
        break;
      }

      if ( actual_old_collections() >= loop_until ) {
assert(result==NULL,"should have succeeded earlier");
t.print(": enough retries, alloc failing");
        break;
      } else {
t.print(": retrying");
      }
    }
  }

  if ( result != NULL ) {
    assert(this->is_in(result), "result not in heap");
    assert(GPGC_PageInfo::page_info((oop)result)->just_gen() == GPGC_PageInfo::PermGen, "Alloced in wrong generation");

    GPGC_Marker::perm_gen_mark_new_obj(result);
  }

  return result;
}


// Incremental zeroing of large objects, with periodic safepoints.
HeapWord* GPGC_Heap::incremental_init_obj(HeapWord* obj, size_t word_size)
{
assert(Thread::current()->is_Java_thread(),"Only a JavaThread can allocate in the object heap");
  // GPGC supports incremental zeroing for multi-page objects only.  The multi-page block is pinned,
  // and then the object is incrementally zeroed, with calls to JavaThread::poll_at_safepoint()
  // periodically.

  const size_t   hs           = oopDesc::header_size();
  PageNum        page         = GPGC_Layout::addr_to_PageNum(obj);
HeapWord*zero_start=obj+hs;
  size_t         zero_words   = word_size - hs;
  JavaThread*    jt           = JavaThread::current();
  
  if ( GPGC_Layout::small_space_page(page) ) {
DEBUG_ONLY(fatal("Small space objects are creeping into incremental_init_obj()");)
jlong start_zero=os::elapsed_counter();
    
    Copy::fill_to_aligned_words(zero_start, zero_words);

    jlong obj_zero_ticks = os::elapsed_counter() - start_zero;
    if ( obj_zero_ticks > jt->get_obj_zero_max_ticks() ) {
      jt->set_obj_zero_max_ticks(obj_zero_ticks, zero_words);
    }

    return obj;
  }

  // Pin down the object so it won't move while we're initializing it.
  if ( GPGC_Layout::mid_space_page(page) ) {
    page = GPGC_Layout::page_to_MidSpaceBasePageNum(page);
  } else {
    assert0( GPGC_Layout::large_space_page(page) );
  }

  GPGC_Space::pin_page(page);

  // Create a handle for the new object, to ensure the garbage collector maintains the
  // block in the right state.  We pretend we're an instance of java.lang.Object, which
  // hopefully won't upset any object profiling too much.
  //
  // TODO-LATER: Make temporary objects be invisible to all object profiling.
  assert(oopDesc::header_size()==1, "Sanity check this code if size of java.lang.Object changes");
oop handle_oop=(oop)obj;
  handle_oop->set_mark(markWord::prototype_with_kid(java_lang_Object_kid));

  ResourceMark rm;
  HandleMark   hm;
  Handle       handle(handle_oop);

  HeapWord*    zero_end     = zero_start + zero_words;

  // We do an initial safepoint, because the internal object allocation overhead and zeroing
  // might have taken some time.
jt->poll_at_safepoint();

  while ( zero_start < zero_end ) {
    size_t incremental_size = (zero_words>IncrementalObjInitThresholdWords) ? IncrementalObjInitThresholdWords : zero_words;
jlong start_zero=os::elapsed_counter();

    HeapWord* zero_addr = zero_start;

    Copy::fill_to_aligned_words(zero_addr, incremental_size);

    zero_start += incremental_size;
    zero_words -= incremental_size;

    jlong obj_zero_ticks = os::elapsed_counter() - start_zero;
    if ( obj_zero_ticks > jt->get_obj_zero_max_ticks() ) {
      jt->set_obj_zero_max_ticks(obj_zero_ticks, incremental_size);
    }

jt->poll_at_safepoint();
  }

  GPGC_Space::unpin_page(page);

  return obj;
}


void GPGC_Heap::collect(GCCause::Cause cause){
assert(!Heap_lock.owned_by_self(),"this thread should not own the Heap_lock");

  switch (cause)
  {
case GCCause::_klass_table_full:
      {
        // Garbage collect, trying to free up some klass IDs.  We ask for a GC cycle
        // and wait for it to complete.

        // TODO: maw: For now, this needs to be a garbage collection cycle without
        // concurrent marking. Once searching the system dictionary stops marking
        // unused classes live, this can be a regular mark.
        GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                          GPGC_CycleOperation::StopTheWorldMarking);
        GPGC_Thread::run_sync_gc_cycle_operation(cause, GPGC_CycleOperation::RunMaxGC, op);
        break;
      }

case GCCause::_jvmti_force_gc:
      // From the JVMTI Specification -
      // Force the VM to perform a garbage collection. The garbage collection is
      // as complete as possible. This function does not cause finalizers to be run.
      // This function does not return until the garbage collection is finished.
      {
        GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                          GPGC_CycleOperation::ConcurrentMarking);
        GPGC_Thread::run_sync_gc_cycle_operation(cause, GPGC_CycleOperation::RunMaxGC, op);
        break;
      }
case GCCause::_jvmti_iterate_over_heap:
      {
        ShouldNotReachHere();
      }
case GCCause::_jvmti_iterate_over_reachable_objects:
      {
        ShouldNotReachHere();
      }
case GCCause::_heap_dump:
      {
        ShouldNotReachHere();
      }
case GCCause::_java_lang_system_gc:
case GCCause::_system_resourcelimit_hit:
      {
        uint is_pending = Atomic::cmpxchg(1, &_system_gc_is_pending, 0);
        // We only enqueue a new collection request if there isn't already a System.gc()
        // in the queue.
        if (is_pending == 0 || !GPGCOptimisticExplicitGC) {
          if (SynchronousExplicitGC) {
            // This does not return until the garbage collection is finished.
            GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                              GPGC_CycleOperation::ConcurrentMarking);
            GPGC_Thread::run_sync_gc_cycle_operation(cause, GPGC_CycleOperation::RunMaxGC, op);
            break;
          } else {
            // Conduct a dont-stop-the-calling thread garbage collection cycle:
            GPGC_Thread::run_async_gc_cycle(cause, GPGC_CycleOperation::RunMaxGC);
            break;
          }
        }
        break;
      }
    default:
      {
        ShouldNotReachHere();
      }
  }
}


// Used by JVMTI IterateOverHeap and IterateOverReachableObject
// And by HeapDumper
void GPGC_Heap::collect_and_iterate(GCCause::Cause cause, SafepointEndCallback end_callback, void* user_data) {
assert(!Heap_lock.owned_by_self(),"this thread should not own the Heap_lock");

  switch (cause)
  {
case GCCause::_jvmti_iterate_over_heap:
      {
        GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                          GPGC_CycleOperation::StopTheWorldMarking);
        op->set_safepoint_end_callback(end_callback);
        op->set_user_data(user_data);
        GPGC_Thread::run_sync_gc_cycle_operation(cause, false, op);
        break;
      }
case GCCause::_jvmti_iterate_over_reachable_objects:
      {
        GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                          GPGC_CycleOperation::StopTheWorldMarking);
        op->set_safepoint_end_callback(end_callback);
        op->set_user_data(user_data);
        GPGC_Thread::run_sync_gc_cycle_operation(cause, false, op);
        break;
      }
case GCCause::_heap_dump:
      {
        GPGC_CycleOperation* op = new GPGC_CycleOperation(GPGC_CycleOperation::RunMaxGC,
                                                          GPGC_CycleOperation::StopTheWorldMarking);
        op->set_safepoint_end_callback(end_callback);
        op->set_user_data(user_data);
        GPGC_Thread::run_sync_gc_cycle_operation(cause, false, op);
        break;
      }
    default:
      {
        ShouldNotReachHere();
      }
  }
}


// This interface assumes that it's being called by the
// vm thread. It collects the heap assuming that the
// heap lock is already held and that we are executing in
// the context of the vm thread.
void GPGC_Heap::collect_as_vm_thread(GCCause::Cause cause){
    ShouldNotReachHere();
}


void GPGC_Heap::permanent_object_iterate(ObjectClosure*cl){
  guarantee(GPGCNoPermRelocation == true, "Cannot iterate perm gen objects while perm gen relocation is enabled");
  GPGC_Space::perm_gen_object_iterate(cl);
  GPGCNoPermRelocation = _saved_GPGCNoPermRelocation;
}


jlong GPGC_Heap::millis_since_last_gc(){
  jlong last_time = MAX( GPGC_OldCollector::millis_since_last_gc(), GPGC_NewCollector::millis_since_last_gc() );
jlong now=os::elapsed_counter();

if(now<last_time){
    return 0;
  }

  jlong ret_val = (now - last_time) * 1000 / os::elapsed_frequency();

  return ret_val;
}


uint64_t GPGC_Heap::space_id_for_address(const void* addr) const {
  uint64_t sid;

  if (GPGC_Space::is_in_new(addr)) {
    sid = (uint64_t)objectRef::new_space_id;
  } else {
    sid = (uint64_t)objectRef::old_space_id;
  }
  return sid;
}


void GPGC_Heap::print_on(outputStream*st)const{
st->print_cr("%s","GenPauselessHeap");
st->print_cr("max "SIZE_FORMAT"M, capacity "SIZE_FORMAT"M, used "SIZE_FORMAT"M",
               max_capacity()/M,
               capacity()/M,
               used()/M);
st->print_cr("SmallSpace [ ("PTR_FORMAT", "PTR_FORMAT") ]",
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_small_space),
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_small_space));
st->print_cr("MidSpace   [ ("PTR_FORMAT", "PTR_FORMAT") ]",
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_mid_space),
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_mid_space));
st->print_cr("MultiSpace [ ("PTR_FORMAT", "PTR_FORMAT") ]",
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_large_space),
               GPGC_Layout::PageNum_to_addr(GPGC_Layout::end_of_large_space));
}


void GPGC_Heap::print_xml_on(xmlBuffer *xb, bool ref) const {
  xmlElement xe(xb, "heap");
  
size_t u=used();
  size_t c = capacity();
  size_t m = max_capacity();
  
  // Make sure that CPMs numbers make sense when graphing.
  c = (u > c) ? u : c;
  m = (c > m) ? c : m;
  
  xb->name_value_item("name", "GenPauselessHeap");
  xb->name_ptr_item  ("id", (void*)this);
  xb->name_value_item("used", u);
  xb->name_value_item("capacity", c);
  xb->name_value_item("max_capacity", m);
  xb->name_value_item("max_capacity_specified", max_heap_size_specified());
  xb->name_value_item("total_collections", total_collections());
  xb->name_value_item("supports_tlab_allocation", supports_tlab_allocation() ? "yes" : "no");
}

void GPGC_Heap::verify(bool allow_dirty,bool silent){
  if ( actual_collections() > 0 ) {
    if (!silent) {
tty->print("single-heap ");
    }
    // GPGC_Space::verify_oops(allow_dirty);
  }
}

void GPGC_Heap::gc_threads_do(ThreadClosure*tc)const{
GPGC_Heap::new_gc_task_manager()->threads_do(tc);
GPGC_Heap::old_gc_task_manager()->threads_do(tc);
}

void GPGC_Heap::print_gc_threads_on(outputStream*st)const{
GPGC_Heap::new_gc_task_manager()->print_threads_on(st);
  GPGC_Heap::old_gc_task_manager()->print_threads_on(st);
}
