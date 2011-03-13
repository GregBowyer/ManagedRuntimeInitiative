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


#include "atomic.hpp"
#include "gpgc_safepoint.hpp"
#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "nativeInst_pd.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

// Mutexes used in the VM (see comment in mutexLocker.hpp).
// They are listed in Rank Order!  

// You must acquire locks from high rank (bottom of list) to low rank (top of
// list).  E.g. ThreadCritical is the lowest rank lock.  It must be acquired
// last, and you cannot acquire any other locks while holding it.

#define defAzLock(  rank,name) AzLock   name##_lock(rank,#name"_lock", true);
#define defWaitLock(rank,name) WaitLock name##_lock(rank,#name"_lock", true);

defAzLock  ( 0, ThreadCritical); // In inner C allocation and a few other antique uses

defAzLock  ( 1, tty );           // No-interleave printing lock, very handy for debugging

defAzLock  ( 2, GPGC_CardTable );
defAzLock  ( 2, GPGC_PageInfo );
defAzLock  ( 2, GPGC_MetaData );
defAzLock  ( 2, GPGC_PeakMem );
defAzLock  ( 2, JNIHandleBlockFreeList ); // handles are used by VM thread
defWaitLock( 2, LowMemory );     // used for low memory detection
defWaitLock( 2, ObjAllocPost );  // a lock used to synchronize VMThread JVM/PI OBJ_ALLOC event posting
defAzLock  ( 2, Patching);       // used for C1 code patching.

defWaitLock( 3, AllocatedObjects ); // 
defWaitLock( 3, BeforeExit );    // 
defAzLock  ( 3, BytecodeTrace);  // lock to make bytecode tracing mt safe
defAzLock  ( 3, ContendedStack );
defAzLock  ( 3, ExceptionCache); // serial profile printing
defAzLock  ( 3, ExpandHeap );    // Used during compilation by VM thread
defAzLock  ( 3, FullGCALot);     // a lock to make FullGCALot MT safe
defWaitLock( 3, FullGCCount);
defAzLock  ( 3, GcHistory);      // 
defAzLock  ( 3, GPGC_NewGen_relocation ); // 
defAzLock  ( 3, GPGC_OldGen_relocation ); // 
defAzLock  ( 3, GPGC_PermGen_relocation ); // 
defWaitLock( 3, GPGC_TaskManagerNotify ); // coordinate between collector & task threads 
defAzLock  ( 3, JNIGlobalHandle); // locks JNIHandleBlockFreeList_lock
defAzLock  ( 3, JmethodIdCreation );
defAzLock  ( 3, JvmdiFrame); // Used to protect jvmdi jframeID counter increment. This lock is used in leaf routine so should rank should be less than all other locks used in thread.
defAzLock  ( 3, KlassTable );    // 
defWaitLock( 3, LiveObjects );   // 
defWaitLock( 3, MemoryFlush );   // coordinate GPGC and PGC cycles
defAzLock  ( 3, PackageTable);   // 
defAzLock  ( 3, ParGCRareEvent ); // 
defAzLock  ( 3, PerfDataManager ); // used for synchronized access to PerfDataManager resources
defAzLock  ( 3, PerfDataMemAlloc ); // used for allocating PerfData memory for performance data
defAzLock  ( 3, ProfileObjects); // 
defAzLock  ( 3, ArtaObjects);    // 
defAzLock  ( 3, Statistics );    // 
defAzLock  ( 3, StringTable );   // 
defAzLock  ( 3, SymbolTable );   // 

defAzLock  ( 4, CodeCache);      // All Things touching the CodeCache
defAzLock  ( 4, CodeCacheOopTable ); // 
defAzLock  ( 4, DerivedPointerTableGC ); // 
defAzLock  ( 4, JvmdiCachedFrame ); // Used to protect jvmdi cached frame.  JvmdiFrame_lock may be acquired holding this lock so its rank is greater than the JvmdiFrame_lock.
defAzLock  ( 4, JvmtiTagHashmap); // Used by JvmtiTagHashmap
defAzLock  ( 4, MonitorSupply ); // protecting free-list of monitors for GC worker threads
MethodStripedMutex OsrList_locks(4/*rank*/,"OsrList",4/*striping*/);// serial OSR activity

defAzLock  ( 5, GPGC_MultiPageSpace );
defAzLock  ( 5, GPGC_NewGen_small_allocation ); // 
defAzLock  ( 5, GPGC_NewGen_mid_allocation ); // 
defAzLock  ( 5, GPGC_OldGen_small_allocation ); // 
defAzLock  ( 5, GPGC_OldGen_mid_allocation ); // 
defAzLock  ( 5, GPGC_PermGen_small_allocation ); // 
defAzLock  ( 5, GPGC_PermGen_mid_allocation ); // 
defAzLock  ( 5, NMethodBucket ); // 
defAzLock  ( 5, PGC_bucket );    // 
defAzLock  ( 5, PGC_MultiPageSpace ); // 
defWaitLock( 5, SystemDictionary ); // lookups done by VM thread

defWaitLock( 6, JNICritical);    // used for JNI critical regions
defWaitLock( 6, VMOperationQueue); // VM_thread allowed to block on these     
defWaitLock( 6, VMTerminator);
AddressStripedMutex CompiledIC_locks(6/*rank*/,"CompiledIC",4/*striping*/);

defWaitLock( 7, GPGC_Interlock ); // coordinate GPGC 
defWaitLock( 7, GPGC_Rendezvous); // coordinate GPGC threads
defWaitLock( 7, GPGC_VerifyNotify); // coordinate GPGCVerifyHeap
defWaitLock( 7, GPGC_VerifyTask); // coordinate GPGCVerifyHeap
defWaitLock( 7, Safepoint);      // locks Threads_lock
defWaitLock( 7, VMThreadCallbackEnd);
defWaitLock( 7, VMThreadSafepoint);

// The rank here is in mutex.hpp to break an include-cycle with thread.hpp
// Bogus lock to give a rank number to all the jvm_locks
// JVM_LOCK at Rank 8 (arbitrary number) goes Here.

defAzLock  ( 9, JfieldIdCreation);  // Used by VM_JVMPIPostObjAlloc VM_Operation, amongst others
defWaitLock( 9, Terminator);

defWaitLock(10, VMThreadSafepointEnd);

defWaitLock(11, PauselessGC );   // coordinate GPGC and PGC cycles

defWaitLock(12, ITR);
defWaitLock(12, JvmtiPendingEvent); // Used by JvmtiCodeBlobEvents
defWaitLock(12, Notify); 
defWaitLock(12, SLT);            // Used in PGC and GPGC for locking PLL lock
defWaitLock(12, VMOperationRequest); 
defAzLock  (12, WLMuxer);

defAzLock  (14, Management);     // used for JVM management

defWaitLock(15, Threads);        // Held when adding or remove or iterating over all threads

defAzLock  (16, Heap);

defWaitLock(17, FAMTrap); 

defAzLock  (18, JNICachedItableIndex);
defAzLock  (18, JvmtiThreadStateStruct); // Used by JvmtiThreadState/JvmtiEventController
AzLock *JvmtiThreadState_lock = &JvmtiThreadStateStruct_lock;
defAzLock  (18, MultiArray);     // locks SymbolTable_lock

defWaitLock(19, Compile); 
defWaitLock(19, CompileThread); 

defWaitLock(20, CompileTask); 
defAzLock  (20, OldGC_ConcurrentRefProcessing); 
defAzLock  (21, NewGC_ConcurrentRefProcessing); 

defAzLock  (25, JVM_RAW);    // bogus lock to get proper ranks for jvm_raw_lock

// One Array of Locks, used to Find Them All and Print Them.  VM locks can be
// created on the fly, so the exact number comes and goes.  Only the static
// locks are declared here.  Using a lock-free algorithm, multiple racing
// threads attempt to insert new AzLocks into this array by scanning for a
// NULL and CAS'ing a NULL into themselves.  Failure to find a NULL implies
// the table is full.  Deletion is the reverse.
static AzLock* _mutex_array[1024];

void AzLock::register_vm_lock( ) const {
  assert0( _rank != AzLock::JVMLOCK_rank && _rank <= JVM_RAW_rank ); // All locks but the JVM lock
  const int len = sizeof(_mutex_array) / sizeof(_mutex_array[0]);
  for( int i=0; i<len; i++ ) {
    if( !_mutex_array[i] &&
        Atomic::cmpxchg_ptr((void*)this,&_mutex_array[i],NULL) == NULL )
      return;
  }
  // Really a dont-care: means some VM locks will not be profiled, which is
  // annoying but not deadly.
  //ShouldNotReachHere();
}

void AzLock::deregister_vm_lock( ) const {
  assert0( _rank != AzLock::JVMLOCK_rank && _rank <= JVM_RAW_rank ); // All locks but the JVM lock
  const int len = sizeof(_mutex_array) / sizeof(_mutex_array[0]);
  for( int i=0; i<len; i++ ) {
    if( _mutex_array[i]==this ) {
_mutex_array[i]=NULL;
      return;
    }
  }
}

MutexLocker::MutexLocker(AzLock& mutex) : _mutex(&mutex) {
  assert( _mutex->_rank < AzLock::JVMLOCK_rank, "You used a locker intended for low-rank locks (ones that block GC on Javathreads but also do not allow a GC across the locking boundary) but you used it with a high-rank lock: you probably need to use MutexLockerAllowGC");
  _mutex->lock_can_block_gc(Thread::current());
  GET_RPC;
  _mutex->_rpc = RPC;           // Record locksite info
}
MutexLocker::MutexLocker(AzLock* mutex, int dummy) : _mutex(mutex) {
  if( !_mutex ) return;
  Thread* thr = Thread::current();
  assert0( _mutex->_rank < AzLock::JVMLOCK_rank || !thr->is_Java_thread() );
  _mutex->lock_can_block_gc(thr);
  GET_RPC;
  _mutex->_rpc = RPC;           // Record locksite info
}
MutexLocker::MutexLocker(AzLock& mutex, Thread *thr) : _mutex(&mutex) {
  assert0( _mutex->_rank < AzLock::JVMLOCK_rank || !thr->is_Java_thread() );
  _mutex->lock_can_block_gc(thr);
  GET_RPC;
  _mutex->_rpc = RPC;            // Record locksite info
}


MutexLockerNested::MutexLockerNested(AzLock& mutex) : _mutex(&mutex) {
  assert0( _mutex->_rank < AzLock::JVMLOCK_rank ); // Check rank before checking for nested
  // Check rank before checking for nested.  If the ranks are misordered but
  // the lock is already owned (in the proper order) then one of two things is
  // true: either the outer acquire always happens - in which case this inner
  // acquire is never needed and should be switched to a assert_lock_strong,
  // OR the outer acquire is conditional, in which case this acquire exposes a
  // potential rank-order problem (and hence deadlock).
  assert0( Thread::current()->chk_lock(_mutex,false) );
  if( _mutex->owned_by_self() ) {
_mutex=NULL;
  } else {
    _mutex->lock_can_block_gc(Thread::current());
    GET_RPC;
    _mutex->_rpc = RPC;         // Record locksite info
  }
}

MutexLockerAllowGC::MutexLockerAllowGC(AzLock& mutex, JavaThread *thr) : _mutex(&mutex) {
  assert0( _mutex->_rank > AzLock::JVMLOCK_rank );
  _mutex->lock_allowing_gc(thr);
  GET_RPC;
  _mutex->_rpc = RPC;            // Record locksite info
}
MutexLockerAllowGC::MutexLockerAllowGC(AzLock* mutex, int dummy ) : _mutex(mutex) {
  if( !_mutex ) return;
  assert0( _mutex->_rank > AzLock::JVMLOCK_rank );
  Thread *t = Thread::current();
  _mutex->lock_common_fastpath(t,t->is_Java_thread());
  GET_RPC;
  _mutex->_rpc = RPC;            // Record locksite info
}

// Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
MutexUnlocker_GC_on_Relock::~MutexUnlocker_GC_on_Relock() {
  if( _mutex->_rank < AzLock::JVMLOCK_rank && !_allow_gc ) _mutex->lock_can_block_gc(Thread::current());
  else _mutex->lock_common_fastpath(JavaThread::current(),true);
  GET_RPC;
  _mutex->_rpc = RPC;
}


#ifdef ASSERT
void assert_locked_or_safepoint(const AzLock& lock) {
  // check if this thread owns the lock (common case)
  if (lock.owned_by_self()) return;
  if (!Universe::is_fully_initialized()) return;
  if (SafepointSynchronize::is_at_safepoint()) return;
  if (Thread::current()->_debug_level > 0) {
    // called from the debugger
    return;
  }
  // see if invoker of VM operation owns it
  VM_Operation* op = VMThread::vm_operation();
  if (op != NULL && op->calling_thread() == lock.owner()) return;
  fatal1("must own lock %s", lock.name());
}


void assert_locked_or_gc_safepoint(const AzLock& lock) {
  assert0(UseGenPauselessGC);
if(GPGC_Safepoint::is_at_safepoint())return;
assert_locked_or_safepoint(lock);
  return;
}


// a stronger assertion than the above
void assert_lock_strong(const AzLock& lock) {
  if (lock.owned_by_self()) return;
  fatal1("must own lock %s", lock.name());
}
#endif

void mutex_init() {  
  assert0( Threads_lock._rank > AzLock::JVMLOCK_rank && AzLock::JVMLOCK_rank > Safepoint_lock._rank );
  assert0( AzLock::JVM_RAW_rank == JVM_RAW_lock._rank );

  // Assert on some perhaps surprising code paths leading to various lock
  // ordering requirements.
  //
  // Hopefully, as people discover strange code paths leading to the lock
  // rankings being incorrect, they will add an assert here and explain the
  // mystery path for future reference.  If something changes such that some
  // call path isn't possible, then these asserts can be removed - they aren't
  // true VM invariants as such, more like a fast-to-fail tests for invariants
  // that are truely held elsewhere.
  assert( Heap_lock._rank > VMOperationRequest_lock._rank, "All VM_GC_Operations take the Heap_lock for the whole VM_operation, hence hold Heap_lock before grabbing the VMOperationRequest_lock" );
  assert( VMOperationRequest_lock._rank > AzLock::JVMLOCK_rank, "All VM_GC_Operations should allow a GC while awaiting the operation to complete" );
  assert( Heap_lock._rank > Threads_lock._rank, "All VM_GC_Operations take the Heap_lock for the whole VM_operation, and a Safepoint is possible when grabbing the VMOperationRequest_lock, and a Safepoint requires the Threads_lock" );

}

GCMutexLocker::GCMutexLocker(AzLock& mutex) : _mutex(SafepointSynchronize::is_at_safepoint() ? NULL : &mutex) {
  if( !_mutex ) return;
  Thread *t = Thread::current();
  assert0( _mutex->_rank < AzLock::JVMLOCK_rank || !t->is_Java_thread() );
  _mutex->lock_can_block_gc(t);
  GET_RPC;
  _mutex->_rpc = RPC;           // Record locksite info
}


// Print all mutexes/monitors that are currently owned by a thread; called
// by fatal error handler.
void print_owned_locks_on_error(outputStream* st) {
st->print("VM AzLock/WaitLock currently owned by a thread: ");
  bool none = true;
  for( uint i = 0; i < sizeof(_mutex_array)/sizeof(_mutex_array[0]); i++ ) {
     // see if it has an owner
     if( _mutex_array[i] && _mutex_array[i]->owner() ) {
       if (none) {
          // print format used by AzLock::print_on_error()
st->print_cr(" ([azlock/os_lock])");
          none = false;
       }
       _mutex_array[i]->print_on_error(st);
       st->cr();
     }
  }
  if (none) st->print_cr("None");
}
extern "C" {
  int sort_helper(const void *a, const void *b) {
    struct lock_contention_data *A = (struct lock_contention_data *)a;
    struct lock_contention_data *B = (struct lock_contention_data *)b;
    int64_t diff = B->_total_ticks - A->_total_ticks;
    if (diff == 0) {
      diff = B->_cum_blocks - A->_cum_blocks;
    }
    return (diff > 0) ? 1 : ((diff == 0) ? 0 : -1);
  }
}

// Print lock contention for ALL locks
void MutexLocker::print_lock_contention(outputStream*st){
  ResourceMark rm;
  const int SIZE = 1000;
  struct lock_contention_data data[SIZE];
  int cnt = 0;

  if (!st) st = tty;
  for (uint i = 0; i < sizeof(_mutex_array)/sizeof(_mutex_array[0]); i++)
if(_mutex_array[i])
      cnt = _mutex_array[i]->gather_lock_contention(data, cnt);

  cnt=Klass::gather_lock_contention(Klass::cast(SystemDictionary::object_klass()),data,cnt);

  qsort(data, cnt, sizeof(data[0]), sort_helper);

  int64_t freq = os::elapsed_frequency()/1000; // ticks per ms
  
st->print_cr("=============== Contended VM Locks ===============");
st->print_cr("                  Name         blocks cum_blocks    max    total  waits:  max    total");
  for( int i=0; i<cnt; i++ )
st->print_cr("%30.30s %6ld  %7ld   %4ldms  %5ldms  %3ld  %5ldms  %5ldms",
                 data[i]._name, data[i]._blocks, data[i]._cum_blocks, 
                 data[i]._max_ticks/freq, data[i]._total_ticks/freq,
                 data[i]._wait_count, data[i]._wait_max_ticks/freq, data[i]._wait_total_ticks/freq);
  st->cr();
}

// To XML lock contention for ALL locks
void MutexLocker::print_to_xml_lock_contention(xmlBuffer *xb) {
  ResourceMark rm;
  const int SIZE = 1000;
  struct lock_contention_data data[SIZE];
  int cnt = 0;

assert(xb,"MuxtexLocker::print_to_xml_lock_contention() passed null XML buffer stream.");
  for (uint i = 0; i < sizeof(_mutex_array)/sizeof(_mutex_array[0]); i++)
if(_mutex_array[i])
      cnt = _mutex_array[i]->gather_lock_contention(data, cnt);

  cnt=Klass::gather_lock_contention(Klass::cast(SystemDictionary::object_klass()),data,cnt);

  qsort(data, cnt, sizeof(data[0]), sort_helper);

  int64_t freq = os::elapsed_frequency()/1000; // ticks per ms
  
  for( int i=0; i<cnt; i++ ) {
    xmlElement xl(xb,"hot_lock");
    xb->name_value_item("name", data[i]._name);
    if (data[i]._kid != 0) {
      xb->name_value_item("kid", data[i]._kid);
    }
    xb->name_value_item("blocks", data[i]._blocks);
    xb->name_value_item("cum_blocks", data[i]._cum_blocks);
    xb->name_value_item("max_time", data[i]._max_ticks/freq);
    xb->name_value_item("total_time", data[i]._total_ticks/freq);
    xb->name_value_item("wait_count", data[i]._wait_count);
    xb->name_value_item("wait_max_time", data[i]._wait_max_ticks/freq);
    xb->name_value_item("wait_total_time", data[i]._wait_total_ticks/freq);
  }
}

