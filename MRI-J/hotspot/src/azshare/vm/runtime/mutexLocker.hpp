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
#ifndef MUTEXLOCKER_HPP
#define MUTEXLOCKER_HPP

#include "mutex.hpp"

// AzLocks used in the VM.

// rank 0
extern AzLock   ThreadCritical_lock; // ThreadCritical as a mutex

// rank 1
extern AzLock   tty_lock;        // try not to interleave debugging output

// rank 2
extern AzLock   GPGC_CardTable_lock; // coordinate GPGC_CardTable
extern AzLock   GPGC_PageInfo_lock; // coordinate GPGC_PageInfo
extern AzLock   GPGC_MetaData_lock; // coordinate GPGC_MetaData
extern AzLock   GPGC_PeakMem_lock; // coordinate GPGC peak memory accounting
extern AzLock   JNIHandleBlockFreeList_lock; // handles are used by VM thread
extern WaitLock LowMemory_lock;  // a lock used for low memory detection
extern WaitLock ObjAllocPost_lock; // a lock used to synchronize VMThread JVM/PI OBJ_ALLOC event posting
extern AzLock   Patching_lock;   // used for C1 code patching.

// rank 3
extern WaitLock AllocatedObjects_lock; // allocated object profiles
extern WaitLock BeforeExit_lock; // guard cleanups and shutdown hooks
extern AzLock   BytecodeTrace_lock; // lock to make bytecode tracing mt safe
extern AzLock   ContendedStack_lock; // a lock used to protect creation of contended stack entries
extern AzLock   ExceptionCache_lock; // synchronize exception cache updates
extern AzLock   ExpandHeap_lock; // Used during compilation by VM thread
extern AzLock   FullGCALot_lock; // a lock to make FullGCALot MT safe
extern WaitLock FullGCCount_lock; 
extern AzLock   GcHistory_lock;  // synchronize GC history buffers used by ARTA
extern AzLock   GPGC_NewGen_relocation_lock; // 
extern AzLock   GPGC_OldGen_relocation_lock; // 
extern AzLock   GPGC_PermGen_relocation_lock; // 
extern WaitLock GPGC_TaskManagerNotify_lock; // coordinate between collector & task threads 
extern AzLock   JNIGlobalHandle_lock; // locks JNIHandleBlockFreeList_lock
extern AzLock   JmethodIdCreation_lock; //
extern AzLock   JvmdiFrame_lock; // Used to protect jvmdi jframeID counter increment. This lock is used in leaf routine so should rank should be less than all other locks used in thread.
extern AzLock   KlassTable_lock; // klass table (Azul)
extern WaitLock LiveObjects_lock; // live object profiles
extern WaitLock LiveObjectsGrow_lock; // live object profiles
extern WaitLock MemoryFlush_lock; // coordinate GPGC and PGC cycles
extern AzLock   PackageTable_lock; // class loader package table
extern AzLock   ParGCRareEvent_lock; // Synchronizes various (rare) parallel GC ops.
extern AzLock   PerfDataManager_lock; // used for synchronized access to PerfDataManager resources
extern AzLock   PerfDataMemAlloc_lock; // used for allocating PerfData memory for performance data
extern AzLock   ProfileObjects_lock; //
extern AzLock   ArtaObjects_lock; // guard the list of life ArtaObjectPools
extern AzLock   Statistics_lock; // guard the life statistics queue updates (not updating statistics themselves)
extern AzLock   StringTable_lock; // interned string table
extern AzLock   SymbolTable_lock; // symbol table

// rank 4
extern AzLock   CodeCache_lock;  // All Things touching the CodeCache
extern AzLock   CodeCacheOopTable_lock; //  cc oop table (Azul)
extern AzLock   DerivedPointerTableGC_lock; // protect the derived pointer table
extern AzLock   JvmdiCachedFrame_lock; // Used to protect jvmdi cached frame.  JvmdiFrame_lock may be acquired holding this lock so its rank is greater than the JvmdiFrame_lock.
extern AzLock   JvmtiTagHashmap_lock; // Used by JvmtiTagHashmap
extern AzLock   MonitorSupply_lock; // protecting free-list of monitors for GC worker threads
extern MethodStripedMutex OsrList_locks; // serial _nm_list activity

// rank 5
extern AzLock   GPGC_MultiPageSpace_lock;           // 
extern AzLock   GPGC_NewGen_small_allocation_lock;  // Allocation ops in NewGen Small space
extern AzLock   GPGC_NewGen_mid_allocation_lock;    // Allocation ops in NewGen Mid space
extern AzLock   GPGC_OldGen_small_allocation_lock;  // Allocation ops in OldGen Small space
extern AzLock   GPGC_OldGen_mid_allocation_lock;    // Allocation ops in OldGen Mid space
extern AzLock   GPGC_PermGen_small_allocation_lock; // Allocation ops in PermGen Small space
extern AzLock   GPGC_PermGen_mid_allocation_lock;   // Allocation ops in PermGen Mid space
extern WaitLock SystemDictionary_lock;              // lookups done by VM thread

// rank 6
extern AzLock   AdapterHandlerLibrary_lock; // Creating I2C adapters
extern AddressStripedMutex CompiledIC_locks; // striped lock used to guard compiled IC patching and access
extern WaitLock JNICritical_lock; // while entering and exiting JNI critical regions, allows GC to sometimes get in
extern WaitLock VMOperationQueue_lock; // VM_thread allowed to block on these     
extern WaitLock VMTerminator_lock; // guard termination of the vm

// rank 7
extern WaitLock GPGC_Interlock_lock; // coordinate GPGC 
extern WaitLock GPGC_Rendezvous_lock; // coordinate GPGC threads
extern WaitLock GPGC_VerifyNotify_lock; // coordinate GPGCVerifyHeap
extern WaitLock GPGC_VerifyTask_lock; // coordinate GPGCVerifyHeap
extern WaitLock Safepoint_lock;  // safepoint abstraction
extern WaitLock VMThreadCallbackEnd_lock; //  coordinate the callback end in a VMThread safepoint by concurrent collectors
extern WaitLock VMThreadSafepoint_lock; // safepoint the VMThread by concurrent collectors

// rank 8
// Bogus rank to give a rank number to all the jvm_locks

// rank 9
extern AzLock   JfieldIdCreation_lock; // Used by VM_JVMPIPostObjAlloc VM_Operation
extern WaitLock Terminator_lock; // guard termination of the vm

// rank 10
extern WaitLock VMThreadSafepointEnd_lock; // safepoint the VMThread by concurrent collectors

// rank 11
extern WaitLock PauselessGC_lock; // coordinate GPGC and PGC cycles

// rank 12
extern WaitLock ITR_lock;        // notify ITR helper threads of incoming data
extern WaitLock JvmtiPendingEvent_lock; // Used by JvmtiCodeBlobEvents
extern WaitLock Notify_lock;     // synchronize the start-up of the vm
extern WaitLock SLT_lock;        // Used in PGC and GPGC for locking PLL lock
extern WaitLock VMOperationRequest_lock; // Threads waiting for a vm_operation to terminate
extern AzLock   WLMuxer_lock;    // synchronizes access to the Weblogic muxer

// rank 13

// rank 14
extern AzLock   Management_lock; // used for JVM management

extern WaitLock Threads_lock; // Held when adding or remove or iterating over all threads
// rank 16
extern AzLock   Heap_lock;       // 

// rank 17
extern WaitLock FAMTrap_lock; // trapping compile threads we don't want during FAM

// rank 18
extern AzLock   JNICachedItableIndex_lock;
extern AzLock*  JvmtiThreadState_lock; // Used by JvmtiThreadState/JvmtiEventController
extern AzLock   MultiArray_lock; // locks SymbolTable_lock

// rank 19
extern WaitLock Compile_lock; // held when Compilation is updating code (used to block CodeCache traversal, CHA updates, etc)

// rank 20
extern WaitLock CompileTask_lock; // adding or removing Compile tasks
extern WaitLock CompileThread_lock; // a lock held by compile threads during compilation system initialization
extern AzLock   OldGC_ConcurrentRefProcessing_lock;

//rank 21
extern AzLock   NewGC_ConcurrentRefProcessing_lock;

// rank 25  - skip a few
extern AzLock   JVM_RAW_lock; // bogus lock to get proper ranks for jvm_raw_lock

// A MutexLocker provides mutual exclusion with respect to a given mutex for
// the scope which contains the locker.  The lock is an OS lock, not an object
// lock, and the two do not interoperate.  Do not use Mutex-based locks to
// lock on Java objects because they will not be respected if a that object is
// locked using the Java locking mechanism (although as of this writing
// ObjectSynchronizer delegates to Mutex so it would work).
//
// We assume throughout the VM that MutexLocker constructors and friends all
// lock using JMM locking semantics.

// Print all mutexes/monitors that are currently owned by a thread; called
// by fatal error handler.
void print_owned_locks_on_error(outputStream* st);

char*lock_name(AzLock*mutex);


// A MutexLocker will lock and unlock a lock in a controlled fashion.  
// It uses the C++ destructor to guarentee an unlock on all paths.  
// The lock can be NULL (no locking action).  This version will assert
// if used with a lock ranked above "safepoint"
class MutexLocker : StackObj {
  AzLock* const _mutex;
 public:
  // Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  MutexLocker(AzLock& mutex);
  MutexLocker(AzLock* mutex, int dummy);
  MutexLocker(AzLock& mutex, Thread *thr );
~MutexLocker(){if(_mutex)_mutex->unlock();}
  // Print lock contention info for ALL locks
static void print_lock_contention(outputStream*st);
  static void print_to_xml_lock_contention(xmlBuffer *xb);
};

// Same as above, but allows nested locking
class MutexLockerNested : StackObj {
AzLock*_mutex;
 public:
  // Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  MutexLockerNested(AzLock& mutex);
~MutexLockerNested(){if(_mutex)_mutex->unlock();}
  // Print lock contention info for ALL locks
};

class StripedMutexLocker:public MutexLockerNested{
public:
  StripedMutexLocker( AddressStripedMutex &asmx, const address pc          ) : MutexLockerNested( asmx.get_lock(pc  ) ) { }
  StripedMutexLocker( MethodStripedMutex  &msmx, const methodOopDesc *moop ) : MutexLockerNested( msmx.get_lock(moop) ) { }
};


// Same as the above, but makes it explicit that a GC is happening.
class MutexLockerAllowGC : StackObj {
  AzLock* const _mutex;
 public:
  // Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  MutexLockerAllowGC(AzLock& mutex, JavaThread *thr );
  MutexLockerAllowGC(AzLock* mutex, int dummy );
~MutexLockerAllowGC(){if(_mutex)_mutex->unlock();}
  // Print lock contention info for ALL locks
};

// A MutexUnlocker temporarily exits a previously entered mutex for the scope
// which contains the unlocker.  Further constrained to be a lock which never
// checks for safepoint, so that exiting the Unlocker scope does not silently
// allow a GC when attempting to re-acquire the lock - or else a high-rank
// lock with will GC on scope-exit (i.e. when the destructor relocks).
class MutexUnlocker_GC_on_Relock:StackObj{
public:
  AzLock *const _mutex;
  const bool _allow_gc;
  MutexUnlocker_GC_on_Relock( AzLock& mutex ) : _mutex(&mutex), _allow_gc(false) { _mutex->unlock(); }
  // Allow a low-rank lock to release/reacquire the JVMLock possibly allowing a GC
  MutexUnlocker_GC_on_Relock( AzLock& mutex, int dummy ) : _mutex(&mutex), _allow_gc(true) { _mutex->unlock(); }
  // Must NOT be inlined, so the RPC asm macro gets the correct RPC for lock hold time.
  ~MutexUnlocker_GC_on_Relock();
};

class MutexUnlocker : MutexUnlocker_GC_on_Relock {
public:
  MutexUnlocker( AzLock& mutex ) : MutexUnlocker_GC_on_Relock(mutex) { assert0( _mutex->_rank < AzLock::JVMLOCK_rank );  }
};

// A GCMutexLocker is usually initialized with a mutex that is
// automatically acquired in order to do GC.  The function that
// synchronizes using a GCMutexLocker may be called both during and between
// GC's.  Thus, it must acquire the mutex if GC is not in progress, but not
// if GC is in progress (since the mutex is already held on its behalf.)
class GCMutexLocker : public StackObj {
  AzLock* const _mutex;
public:
  GCMutexLocker(AzLock& mutex);
~GCMutexLocker(){if(_mutex)_mutex->unlock();}
};


struct lock_contention_data {
  int64_t _blocks, _cum_blocks, _max_ticks, _total_ticks;
  int64_t _wait_count, _wait_max_ticks, _wait_total_ticks;
  intptr_t _kid;
  const char *_name;
};

// for debugging: check that we're already owning this lock (or are at a safepoint)
#ifdef ASSERT
void assert_locked_or_gc_safepoint(const AzLock& lock);
void assert_locked_or_safepoint(const AzLock& lock);
void assert_lock_strong(const AzLock& lock);
#else
#define assert_locked_or_gc_safepoint(lock);
#define assert_locked_or_safepoint(lock)
#define assert_lock_strong(lock)
#endif

#endif // MUTEXLOCKER_HPP
