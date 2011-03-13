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

// thread.c - Shared Linux & (Linux + azul DLKM) thread implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <limits.h>

#include <signal.h>
#include <os/exceptions.h>
#include <os/utilities.h>
#include <os/memory.h>
#include <os/thread.h>

extern long int syscall(long int x, ...);

// These are the assembler routines that modify the stack frame pointers
// when we are creating/deleting the large stack areas.

extern int whack_thread_stack_asm  ( void* info );
extern int whack_main_stack_asm(intptr_t stk, void* JavaMain, void* args1, void* args2);

//******************************************************************************
//
// Thread Stack Handling
// ---------------------
//
// Azul has specific requirements that drive thread stack management algorithms: 
//
// 1) We must be able to quick produce a Thread ID for each thread.
//
// 2) We don't want to devote a register to point to the Java Thread instance
//    for each thread. 
//
// To meet these requirements, we handle stacks as an array of fixed sizes
// stacks in virtual memory.  A thread ID is the index in the array for the
// thread's stack.  We also store the Java Thread instance for each thread
// at the lowest address in each thread's stack region.
//
// With this design, retrieving a pointer to a thread's Thread instance is
// simple:
//
//   thread = (Thread*) ( ((intptr_t)stack_address) & ~(ThreadStackSize - 1));
//
// And calculating a thread ID is also easy:
//
//   thread_id = (((intptr_t)stack_address) - ThreadStackRegionBase) >> LogThreadStackSize;
//
// So, all threads are allocated starting at a given base address.  Each stack
// gets a constant (power of 2) size region of virtual memory.  The bottom
// of that virtual memory region contains the Thread instance for the thread.
//
// Unfortunately, the pthreads library can't manage thread stack layout in this
// fashion.  The Pthreads calls do allow you to specify a specific address for
// stack, but provide no easy means of reclaiming this area on thread exit.  To
// that end we have built a means of allocating our own stack frames, without
// modifying the existing Pthreads lirbary.
//  
// We initialize a large region of virtual memory to use as an array of thread
// stacks.  During thread creation we grab an unused stack from the array of stacks.  
// Each stack is the same power of 2 size, and aligned to the thread stack size. 
//
// The thread creation logic then calls thread_init(), passing it (among
// other things) the address of the stack area it just allocated, the
// address of the starting function to execute when the thread is created,
// and a parameter to pass to the starting function.
//
// We then call the normal pthread_create() function, which results in a new
// thread that allocates its own stack (via malloc) from within the general
// virtual memory pool.  This stack is created to be as small as possible.
//
// Instead of then jumping to the actual thread creation logic, we goto
// a function called whack_thread_stack_asm, which manipulates the stacks and
// swaps context into the newly allocated, large thread stack allocated by
// thread_stack_create().  We "whack" that stack pointer to point at
// this new stack, leaving the old Pthread stack intact.  We manipulate the
// thread stack such that on thread exit, another routine
// (dewhack_thread_stack_asm) will be called that reverts back to the Pthread
// stack, and makes the large stack available for allocation by a future
// call to thread_stack_create().  The Pthread stack memory allocation is
// handled within its own library and we don't worry about it.  We
// specifically allocate as small a Pthread stack frame as possible, though.
//
// There is a separate assembly function (whack_main_stack_asm()) for replacing
// the stack of the initial "proto" thread.  There is only a single
// dewhack_thread_stack_asm() as the main (proto) thread stack doesn't
// need to be "restored" if the AVM is exiting.  Dewhacking the proto thread
// stack could obviously be implmented if we ever felt it to be important.
//
// The thread stack layout for the stacks allocated by us is as follows:
// (address shown are based on current memory allocations and given only for
// reference)
//
//  0x2020,0000     Top of Full Stack/ Bottom of next stack.    
//                  +--------------------------------------------------+
//  0x201F,FFF8     | Top of Stack Addr.                               |  
//                  |  A little info to unwind when this thread exits  |
//                  |  Return Address to the unwind function           |
//                  +--------------------------------------------------+
//  0x201F,FFC8     | Top of useable stack (stack grows down)          |
//                  | first stack frame is here...                     |
//                  ~                                                  ~
//       %RSP==>    | Current Stack Pointer (%RSP)                     |
//                  |                                                  |
//                  | Leaf Area (Temporary Variables)                  |
//                  ~                                                  ~
//     %RSP-128==>  | Interrupt Handler Stack area                     |
//                  ~                                                  ~
//                  |                                                  |
//                  +--------------------------------------------------+
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XX   Unmapped Spare Pages                       XX|
//                  |XX   Lazily Mapped In On Demand                 XX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  +--------------------------------------------------+
//                  ~                                                  ~
//                  | Java Expression Stack Area.                      |
//                  ~                                                  ~
//                  +--------------------------------------------------+
//                  | Azul Stack ARTA Tick Buffer.                     |
//                  +--------------------------------------------------+
//                  | Alternate Stack Area; Used for SIGSEGV.          |
//                  +--------------------------------------------------+
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  |XXX DeadZone: For unrecoverable Stack overflow. XX|
//                  |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX|
//                  +--------------------------------------------------+
//                  | Type Specific Thread Memory based on Thread type |
//                  +--------------------------------------------------+
//                  | Type Stable Memory: Thread ID Class              |
//                  +--------------------------------------------------+
//  0x2000,0000     Bottom of the stack
//
//******************************************************************************

// Set a call-back function, called after any new thread is created and after
// its stack has moved from the initial pThreads stack to the normal large
// libos managed stack.
threadStackCallbackFuncPtr_t _thread_start_callback = NULL;
// Set a call-back function, called when any thread is about to have its
// normal large libos managed stack be reclaimed.
threadStackCallbackFuncPtr_t _thread_death_callback = NULL;

static pthread_attr_t    _pthreadCreateAttrs;

// --- StackSegVHandler ------------------------------------------------------
// Handle SEGVs related to the stack.  Map in more stack space as needed, callback
// on hitting the yellow-zone, die on hitting the red-zone.
address_t StackSegVHandler(int signal, siginfo_t* pSigInfo, ucontext_t* pUserContext) {
  const intptr_t faultAddr = (intptr_t) pSigInfo->si_addr;

  // Hack-fix for apparent kernel bug if we take a SEGV on the alt signal stack
  // whilst inside another unrelated signal handler.  If we just ignore the
  // fault and continue things "appear to work".
  if( faultAddr == 0 && pSigInfo->si_code==SI_KERNEL )
    return AZUL_SIGNAL_RESUME_EXECUTION;
  // GDB randomly produces these when debugging the SEGV handler itself
  if( pSigInfo->si_code == SI_USER )
    return AZUL_SIGNAL_RESUME_EXECUTION;

  // Compute the thread base
  const intptr_t rsp = pUserContext->uc_mcontext.gregs[REG_RSP];  
  const intptr_t stackBase = rsp & ~(thread_stack_size - 1);

  // If RSP is in deadzone1, this is almost certainly a recursive segv.
  os_guarantee( !(stackBase+thread_map_deadzone1 <= rsp && rsp < stackBase+thread_map_deadzone1+thread_size_deadzone1),
                "RSP in deadzone1.  Probably a recursive SEGV exception.");
  
  // Except for deadzone1, there should never be faults in these areas.
  os_guarantee( !(faultAddr >= stackBase+thread_map_local_data && faultAddr < stackBase+thread_map_jex_stack),
                "SEGV in thread-local-data, the deadzone1, the alt-signal-stack, or ARTA buffer");

  // Outside the managed thread stack area?
  if(           !(faultAddr >= stackBase+thread_map_jex_stack  && faultAddr < stackBase+thread_stack_size ) )
    return AZUL_SIGNAL_NOT_HANDLED;

  // See if we hit the red zone.  This happens if both stacks have grown into
  // each other, i.e., their extension counts have walked into each other.
  // We actually blow up if they get within 2 VM_SMALL_PAGES of each other.
  ThreadBase *thrBase = (ThreadBase*)stackBase;
  os_guarantee( thrBase->_usr_stk_ext_cnt - thrBase->_jex_stk_ext_cnt > 2,
                "red zone: the 2 stacks have grown into each other" );

  // Figure out which stack side to grow (the bottom/jex stack up, or the top/C stack down).
  // Absolute last byte mapped-in for the JEX stack
  const intptr_t mapped_jex_end = 
    stackBase+(thrBase->_jex_stk_ext_cnt*VM_SMALL_PAGE_SIZE)-1;
  // Lowest-address mapped-in for the usr stack
  const intptr_t mapped_usr_end =
    stackBase+(thrBase->_usr_stk_ext_cnt*VM_SMALL_PAGE_SIZE);
  // Is the fault closer to the JEX stack end or the USR stack end?
  // Extend the stack that we are closest to.
  intptr_t pagenum;
  intptr_t jexstk_guess;
  if( faultAddr-mapped_jex_end < mapped_usr_end-faultAddr ) {
    pagenum =   thrBase->_jex_stk_ext_cnt++;
    jexstk_guess = faultAddr;
  } else {
    pagenum = --thrBase->_usr_stk_ext_cnt;
    jexstk_guess = mapped_jex_end;
  }
  const intptr_t ext = stackBase+(pagenum<<VM_SMALL_PAGE_SHIFT);

  const void* addr = libos_mmap_wrapper( 
    (void*) ext,                // Start of next stack extension.
    (size_t) VM_SMALL_PAGE_SIZE,// Size of next stack extension.
    PROT_READ|PROT_WRITE,       // Read & write, but not execute
    MAP_NORESERVE |    // no swap backing, might SEGV on thread create for OOM
    MAP_ANONYMOUS |             // No file mapping
    MAP_PRIVATE,                // Not shared with other processes
    -1, 0);                     // Unused fd & offset
  os_guarantee(addr > (void*) 0, "mmap() failed.");

  // Check for entering the yellow zone.  If so, we act like we did not handle
  // it (really we extended the stack).  A further chained exception handler
  // will detect the stack overflow condition which will (eventually) trigger
  // the JVM in reducing his stack consumption.  This call will also unmap
  // excessive pages allocated to the other stack before declaring a yellow-zone
  // stack overflow condition.
  if( thread_stack_yellow_zone_reguard2(rsp,jexstk_guess) )
    return AZUL_SIGNAL_NOT_HANDLED;

  // It's all good.  Let the application resume with a bigger stack
  return AZUL_SIGNAL_RESUME_EXECUTION;
}


// --- thread_stack_yellow_zone_reguard --------------------------------------
// Free-up/reguard/unmap excessive stack pages.
// Returns a result same as "is_in_yellow_zone" called on the current thread:
// If the stack is in the yellow zone, return true.
// If the stack is out of the yellow zone, return false.
int thread_stack_yellow_zone_reguard2( intptr_t rsp, intptr_t jexstk )  {
  rsp = -1000/*pad for self-stack to run mumap calls*/ + rsp;
  ThreadBase *thr = (ThreadBase*)(rsp & ~(thread_stack_size - 1));
  os_assert( (jexstk & ~(thread_stack_size - 1)) == (intptr_t)thr, 
             "jexstk is not in the same stack region as the current stack pointer");

  // See if we can reguard any Java Interpreter Stack pages
  // We have to leave (jex_page+1) accessible, but also allow some hysteresis.
  const int jex_page = (jexstk -(intptr_t)thr)>>VM_SMALL_PAGE_SHIFT;
  while( thr->_jex_stk_ext_cnt > jex_page+4 ) {
    thr->_jex_stk_ext_cnt--;
    const intptr_t ext = (intptr_t)thr + (thr->_jex_stk_ext_cnt<<VM_SMALL_PAGE_SHIFT);
    int rc1 = libos_munmap_wrapper( (void*)ext, VM_SMALL_PAGE_SIZE );
    os_guarantee(rc1 == 0, "Could not unmap User Stack Yellow Zone!");
  }

  // See if we can reguard any User/C Stack pages.
  // We have to leave (usr_page-1) accessible, but also allow some hysteresis.
  const int usr_page = (rsp -(intptr_t)thr)>>VM_SMALL_PAGE_SHIFT;
  while( thr->_usr_stk_ext_cnt < usr_page-4 ) {
    const intptr_t ext = (intptr_t)thr + (thr->_usr_stk_ext_cnt<<VM_SMALL_PAGE_SHIFT);
    thr->_usr_stk_ext_cnt++;
    int rc1 = libos_munmap_wrapper( (void*)ext, VM_SMALL_PAGE_SIZE );
    os_guarantee(rc1 == 0, "Could not unmap User Stack Yellow Zone!");
  }

  // We freed up/guarded/unmapped all the pages we could.  Return value is 
  // whether or not enough pages are freed (really: stacks are far enough 
  // apart) to declare out-of-yellow-zone.
  return is_in_yellow_zone(thr);
}


// --- libos_munmap_wrapper --------------------------------------------------
// munmap but call the az_<mem> versions if requested
int libos_munmap_wrapper(void *start, size_t size) {
  int retval = 0;

  if (os_should_use_azmem()) {
    retval = az_munmap(start, size, MEMORY_ACCOUNT_DEFAULT, MEMORY_DEALLOCATE_NOFLAGS);
  } else {
    retval = munmap(start, size);
  }

  return retval;
}


// --- libos_mmap_wrapper ----------------------------------------------------
// mmap but call the az_<mem> versions if requested
void* libos_mmap_wrapper(void *start, size_t size, int prot , int flags, int fd, off_t offset) {
  void* addr = NULL;

  if (os_should_use_azmem()) {
    int rc = az_mmap(start, size, prot, MEMORY_ACCOUNT_DEFAULT, 0);
    os_guarantee(rc == 0, "az_mmap() returned error");
    addr = start;
  } else {
    addr = mmap(start, size, prot, flags, -1, 0);
  }

  return addr;
}


// --- libos_mreserve_wrapper ------------------------------------------------
// Calls mmap or az_mreserve depending.
void* libos_mreserve_wrapper(void *start, size_t size, int prot , int flags, int fd, off_t offset) {
  void* addr = NULL;

  if (os_should_use_azmem()) {
    int rc = az_mreserve((void*) start, size, 0);               
    os_guarantee( rc == 0, "az_mreserve() returned error");
    addr = start;
  } else {
    addr = mmap( start, size, prot, flags, -1, 0);
  }

  return addr;
}

// --- alternate_stack_create ------------------------------------------------
// Map the alternate signal stack for SEGV handling.
// Make any thread-start user-callback.
// Call the threads' start function.
void alternate_stack_create(intptr_t thr_top, Thread_Start_Function start_function, void* start_param) {
  sys_return_t rc = exception_register_chained_handler(SIGSEGV, StackSegVHandler, AZUL_EXCPT_HNDLR_SECOND);
  os_syscallok(rc, "Could not chain Stack Extension SIGSEGV handler.");
   
  stack_t  stackOverflowSS;
  intptr_t stkBase = ((thr_top-1) & ~(thread_stack_size - 1));
  stackOverflowSS.ss_sp = (void*)(stkBase+thread_map_alt_sig_stack);
  stackOverflowSS.ss_size = thread_size_alt_sig_stack;
  stackOverflowSS.ss_flags = 0;
  rc = sigaltstack( &stackOverflowSS, NULL );
  os_syscallok(rc, "Could not create alternate signal stack for thread");

  if( _thread_start_callback )
    _thread_start_callback( stkBase );

  // Execute the main thread start function.
  if( start_function )
    start_function(start_param);

  // For normal threads, returning from here will go to dewhack_thread_stack_asm
  // and begin freeing the stack.  For the primordial thread, returning from
  // here just carries on normally.
}


// --- alternate_stack_delete ------------------------------------------------
// Unmap the alternate signal stack for SEGV handling.
void alternate_stack_delete(intptr_t thr) {
  stack_t altStackInfo;
  altStackInfo.ss_sp = 0;
  altStackInfo.ss_size = 0;
  altStackInfo.ss_flags = SS_DISABLE;
  int rc = sigaltstack( &altStackInfo, NULL);
  os_guarantee( rc == 0, "Could not deactivate alternate signal stack for thread");
} 


// --- init_single_thread_stack ----------------------------------------------
// Initialize a single thread stack, for the 1st time ever.
// Only called once ever per thread stack.
static void init_single_thread_stack(intptr_t thr) {
  if (os_should_use_azmem()) {
    int rc = az_mmap(  // direct call (no libos_mmap_wrapper() call) to az_mmap().
      (void*)  thr,    // start (low) memory of thread stack. 
      (size_t) thread_stack_size,     // virtual space for entire thread stack
      (int)    PROT_READ|PROT_WRITE,  // Read & write, but not execute
      (int)    MEMORY_ACCOUNT_DEFAULT,// Same account as C heap.
      0);                             // No special flags.
      os_guarantee( rc == 0, "Thread stack region allocation failed: az_mmap() returned invalid address");
  }

  // Zero out this page.  It is not (yet?) guaranteed that the page returned
  // by az_mmap will be a zero page.  There are some bits which must be
  // type-stable and initially zero.
  // TODO:  Remove this step when the kernel folks implement this.
  memset((void*)thr, 0, VM_SMALL_PAGE_SIZE);
  ThreadBase *thrBase = (ThreadBase*)thr;

  // Unmap dead zone 1
  const int ret0 = libos_munmap_wrapper((void*)(thr+thread_map_deadzone1), thread_size_deadzone1);
  os_guarantee(ret0==0, "memory unmap for thread stack deadzone1 failed");

  // Allow at least 1 initial page to start with.
  thrBase->_jex_stk_ext_cnt = (thread_map_jex_stack>>VM_SMALL_PAGE_SHIFT)+ 1;// 1 initial page  for expr stack
  thrBase->_usr_stk_ext_cnt = (thread_stack_size   >>VM_SMALL_PAGE_SHIFT)-16;//16 initial pages for user stack
  const int ret1 = libos_munmap_wrapper( 
    // Unmap from the next page following the jex stack
    (void*)(thr+(thrBase->_jex_stk_ext_cnt<<VM_SMALL_PAGE_SHIFT)),
    // Unmap till the last page before the usr stack
    (thrBase->_usr_stk_ext_cnt-thrBase->_jex_stk_ext_cnt)<<VM_SMALL_PAGE_SHIFT);
  os_guarantee(ret1==0, "memory unmap for thread stack deadzone2 failed");
}


// --- init_all_thread_stacks ------------------------------------------------
//   mmap() (if we are not using UseAzMem) or reserve (+UseAzMem)
//   a giant virtual-memory space for *all* thread stacks.  The space is
//   backed by physical memory on an as-needed basis.  All individual stacks are
//   aligned on power-of-2 regions, and the alignment and size are the same.
//
//   An interior pointer to a stack can thus be used to efficiently calculate the
//   starting (low) address of the stack:
//
//       stack_starint_addr = stack_interior_ptr & ~(thread_stack_size-1)
//
//   Previously used and deallocated thread stacks are kept on a linked list of
//   "free" stacks.  The low 8 bytes in a stack is used to chain the linked list.
//   "deallocated" thread stacks are not munmap()'ed, because application logic
//   expects the low end of a stack to be type stable.
//
//   Never-used stacks are those past the _lastStackInialized address.  Bringing
//   a never-used stack into use requires CAS'ing forward the
//   _lastStackInitialized one thread_stack_size, and then calling
//   init_single_thread_stack() on the new _lastStackInitialized value.
//   Initialization only on demand prevents excessive Linux kernel VMArea
//   fragmentation.

// Local state variables for managing the free pool of thread stacks.
static ThreadBase *volatile _freeThreads = NULL;
static uintptr_t   volatile _lastStackInitialized  = 0;

void init_all_thread_stacks() {
  // Make sure that the region falls on a thread stack size alignment boundary.
  os_guarantee( (__THREAD_STACK_REGION_START_ADDR__ & ~(thread_stack_size-1)) == __THREAD_STACK_REGION_START_ADDR__,
                "Thread stack region base address not aligned to thread stack size");

  // Pre-allocate virtual space for ALL threads stacks.
  void *adr = libos_mreserve_wrapper(
     (void*)__THREAD_STACK_REGION_START_ADDR__, 
     // virtual space for all threads at once
     __THREAD_STACK_REGION_END_ADDR__ - __THREAD_STACK_REGION_START_ADDR__,
     PROT_READ|PROT_WRITE,        // Read & write, but not execute
     MAP_NORESERVE |              // no swap backing, might SEGV for OOM
     MAP_ANONYMOUS |              // No file mapping
     MAP_PRIVATE,                 // Not shared with other processes
     -1, 0);                      // Unused fd & offset

  os_guarantee( adr == (void*)__THREAD_STACK_REGION_START_ADDR__,
                "Thread stack region mmap allocation failed");

  // Free list setup
  _freeThreads = NULL;
  _lastStackInitialized =  __THREAD_STACK_REGION_START_ADDR__;

  // We maintain one pthread_attr for use in creating all pthreads.
  pthread_attr_init(&_pthreadCreateAttrs);
  pthread_attr_setdetachstate(&_pthreadCreateAttrs, PTHREAD_CREATE_DETACHED);

  // Tell pthreads to create the minimum stack. We use this minimal stack 
  // only to run whack_thread_stack_asm() which initializes and jumps to 
  // another stack that the VM manages.
  pthread_attr_setstacksize(&_pthreadCreateAttrs, PTHREAD_STACK_MIN);  
}


// --- thread_stack_create ---------------------------------------------------
// Pull a stack off the free list, if possible.
// Otherwise make a new one from the lazy initialization pile.
void* thread_stack_create( ) {
  // make sure the stack region has been initialized.
  if (_lastStackInitialized == 0 ) {
      init_all_thread_stacks();
  }
  
  // First try and CAS a stack off the free list.
  ThreadBase *head = _freeThreads;
  while( head ) {
    intptr_t next = *(intptr_t*)head;
    if( head == __sync_val_compare_and_swap(&_freeThreads, head, next) )
      return head;
    head = _freeThreads;
  }

  // The free list of stacks is empty.  Try and push out the stack limit.
  uintptr_t last = _lastStackInitialized;
  while( last < __THREAD_STACK_REGION_END_ADDR__ ) {
    uintptr_t next = last+thread_stack_size;
    if( last == __sync_val_compare_and_swap( &_lastStackInitialized, last, next) )
      break;
    last = _lastStackInitialized;
  }
  // Check for completely out of 8Million stacks
  if( last >= __THREAD_STACK_REGION_END_ADDR__ ) return NULL;
  // Init the memory for the 1st time ever.
  init_single_thread_stack(last);
  return (void*)last;
}


// --- thread_stack_delete ---------------------------------------------------
// Make the thread-death callback.  
// Remove the alt-signal stack.
// Put the stack on the free list.
// Leave the type-stable memory bits mapped in (and zero).
void thread_stack_delete(void* rsp) {
  intptr_t stkBase = ((intptr_t)rsp) & ~(thread_stack_size - 1);
  os_assert(((ThreadBase*)stkBase)->_is_Complete_Thread == 0 &&
            ((ThreadBase*)stkBase)->_is_Complete_Java_thread == 0,
            "attempt to delete a thread stack before clearing is_Complete_Thread" );

  if( _thread_death_callback )
    _thread_death_callback( stkBase );

  alternate_stack_delete(stkBase);

  while( 1 ) {
    ThreadBase *old = _freeThreads;
    *(ThreadBase**)stkBase = old;
    if( old == __sync_val_compare_and_swap(&_freeThreads, old, stkBase) )
      return;
  }
}


// --- thread_init -----------------------------------------------------------
// This code is necessary because the current Pthreads library does not allow
// us to readily use our own stack area.  The Pthreads calls do allow you to
// specify a separate area for stack, but provide no easy means of reclaiming
// this area on thread exit.  To that end we have built a means of allocating
// our own stacks, without modifying the exiting Pthreads library, or having to 
// update/port each successive revision.
//
// During thread start we allocate a large stack frame from the previously 
// established stack pool.  We then call the normal pthread_create() function, 
// which results in a new thread that allocates its own stack (via malloc) from 
// within the general virtual memory pool.  We then "whack" that stack pointer 
// to point at this new stack frame leaving the old Pthread stack frame intact.  
// We manipulate the thread stack such that on thread exit, another routine 
// (dewhack_thread_stack_asm) will be called that reverts back to the Pthread 
// stack and returns the large stack frame back to the pool.  The Pthread stack
// frame memory allocation is handled within its own library and we don't
// worry about it.  We specifically allocate as small a Pthread stack frame
// as possible, though.
pthread_t thread_init(intptr_t thr, Thread_Start_Function start_function, void* start_param) {
  void**    pStackTop = (void**)(thr + thread_stack_size);
  void**    pStack    = pStackTop;
  
  // NOTE:  Do not change this logic without an accompanying change in the
  // whack_thread_stack_asm(), which relies on these variables placed on the
  // stack in this arrangement!
  *(--pStack) = start_param;                    // pStackTop -0x08
  *(--pStack) = (void*) start_function;         // pStackTop -0x10
  // *(--pStack) = Future saved RSP register    // pStackTop -0x18
  // *(--pStack) = Future saved RBP register    // pStackTop -0x20

  pthread_t tid = 0;            /* default return value of zero on failure */
  pthread_create(&tid, &_pthreadCreateAttrs, (void *)whack_thread_stack_asm, (void*)pStackTop);
  return tid;
}


// --- thread_create_and_init ------------------------------------------------
// wrapper function for calling thread_stack_create() & thread_init().
pthread_t thread_create_and_init(size_t stack_size, Thread_Start_Function start_function, void *start_param) {
  os_assert(stack_size <= thread_stack_size, "Global thread stack size too small");
  void* thread_stack = thread_stack_create();
  if( thread_stack == NULL ) 
    return (pthread_t)0;
  return thread_init((intptr_t)thread_stack, start_function, start_param);
}
                     

// --- thread_gettid ---------------------------------------------------------
pid_t thread_gettid() {
  return syscall(SYS_gettid);
}


// --- thread_sleep ----------------------------------------------------------
sys_return_t thread_sleep(uint64_t sleep_nanos) {
  useconds_t sleep_micros = (useconds_t)(sleep_nanos / 1000);
  usleep(sleep_micros);
  return SYSERR_NONE;
}

#ifndef AZ_PROXIED
int thread_whack_main_stack(void* JavaMain, void* args) {
  // Thread ID 1's thread stack, which will eventually run the app request.
  intptr_t main_stack = (intptr_t) thread_stack_create();

  // Before we do anything else, we need to create an alternate stack for
  // the main thread.
  // Note: the value passed is the topmost address of the stack, but not in
  // the stack itself.
  // We subtract one and mask this value to get the stack's base address.

  const intptr_t stackAddr = __THREAD_STACK_REGION_START_ADDR__;
  const intptr_t stackTop = stackAddr + thread_stack_size;

  alternate_stack_create(stackTop, NULL, NULL);

  // Switch over to the large thread stack we just created, then start the JVM.
  return whack_main_stack_asm((intptr_t) (main_stack + thread_stack_size), JavaMain, args, NULL);
}
#endif // !AZ_PROXIED
