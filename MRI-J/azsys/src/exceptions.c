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
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <os/exceptions.h>
#include <os/thread.h>
#include <os/utilities.h>
#include <string.h>


//******************************************************************************
//
// We maintain an array of chained handlers for each signal.  There are up to
// AZUL_MAX_HANDLERS signal handlers allowed to be chained together for each signal
// value. Each handler must be registered in advance, and its position defined, before
// receiving the exception.  A default signal handler is invoked, which searches
// this table and passes control on to one or more the signal handlers registered
// in it.
//
// We are installing the "direct" signal handler in the very last position of
// each array, as a means of determing whether the signal is to be handled as
// part of the chained handler mechanism, or "directly" via an independent handler.
//
// If any user of either the chained or direct API registration attempts to install
// a handler, the alternate mechanism is effectively disabled for the signal.
// Trying to install both is an error, therefore if the last entry isn't NULL,
// the rest of the array for that signal must be, and vice versa. 
//
//******************************************************************************
static azul_chained_handler_t registered_signal_handlers[NSIG][AZUL_MAX_HANDLERS] = {
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  0: Unused
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  1: SIGHUP
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  2: SIGINT
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  3: SIGQUIT
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  4: SIGILL
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  5: SIGTRAP
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  6: SIGIOT / SIGABRT
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  7: SIGBUS
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  8: SIGFPE
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal  9: SIGKILL
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 10: SIGUSR1
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 11: SIGSEGV
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 12: SIGUSR2
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 13: SIGPIPE
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 14: SIGALRM
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 15: SIGTERM
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 16: SIGSTKFLT / SIGCLD
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 17: SIGCHLD
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 18: SIGCONT
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 19: SIGSTOP
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 20: SIGTSTP
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 21: SIGTTIN
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 22: SIGTTOU
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 23: SIGURG
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 24: SIGXCPU
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 25: SIGXFSZ
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 26: SIGVTALRM
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 27: SIGPROF
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 28: SIGWINCH
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 29: SIGPOLL / SIGIO
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 30: SIGPWR
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 31: SIGSYS
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 32: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 33: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 34: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 35: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 36: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 37: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 38: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 39: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 40: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 41: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 42: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 43: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 44: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 45: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 46: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 47: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 48: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 49: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 50: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 51: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 52: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 53: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 54: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 55: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 56: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 57: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 58: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 59: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 60: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 61: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 62: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL },  // Signal 63: Realtime signal?
    { NULL, NULL, NULL, NULL, NULL, NULL }   // Signal 64: Realtime signal?
};


//******************************************************************************
//
// FUNC: signal_chain_handler()
//
// SYNOPSIS: The chained signal handler for all signals registered through
//   this API.  It acts as a clearing house, redirecting the signals to the
//   various handlers registered (chained) for that signal.
//
// DESCRIPTION: This function is registered with the kernel as the signal 
//   handler for all signals that have a chained handler.  It gets invoked
//   when a signal occurs. It then searches through the chained handlers 
//   for that signal (in order) until the signal is handled.  If a handler
//   is registered, it is called as it it were the original handler.  When
//   it returns, it can either return AZUL_SIGNAL_NOT_HANDLED, in which case
//   the next handler in the array (if present) will be given control, or
//   AZUL_SIGNAL_SKIP_INSTRUCTION (currently unimplemented) or
//   AZUL_SIGNAL_RESUME_EXECUTION, which simply returns (no additional handlers
//   are invoked) and control returns to the application.
//
//   NOTE: The very last entry of the signal's array is used to store the
//   address of a "direct" handler, should one be in place.  If this entry
//   is non-NULL then it is an error to have a chained signal handler.
//
// PARAMETERS: 
// 
//   signum: INPUT - the OS defined exception signal number.
//   pSigInfo: INPUT - the OS defined signal info structure passed to us during the exception().
//   pUcontext: INPUT - the OS defined user context structure passed to us during the exception().
//
// RETURNS: void.
//   or asserts on error.
//   
//
// BASIC ALGORITHM: 
// 
//   1) Step by step explaination of the algorithm.
//
//
//******************************************************************************
static void signal_chain_handler(int signum, siginfo_t* pSiginfo, ucontext_t* pUcontext) {

  // Setup some asserts because nested SEGVs are really hard to debug.
  os_assert( signum>0 && signum<NSIG, "Invalid signal number in signal_chain_handler()");
  ThreadBase *thr=NULL;
  if( signum == SIGSEGV ) {
    int dummy;
    thr = (ThreadBase*)(((intptr_t)&dummy) & ~(thread_stack_size - 1));
    if( thr->_libos01 ) {
      intptr_t rip = pUcontext->uc_mcontext.gregs[REG_RIP];
      rip += 0;
      const char *str = thr->_libos01==2 ? "nested SEGV in NIO signal handler\n" :"nested SEGV\n";
      int ret = write(2,str,strlen(str));
      ret = ret; // XXX: we don't care the return, but just make the gcc happy.
    }
    thr->_libos01 = 1; // nested SEGV assert flag
  }

  // Check all signal handlers to see if they want to handle this signal.
  address_t result = NULL;
  for( int i=0; i<AZUL_MAX_CHAINED_HANDLERS-1; i++ ) {
    azul_chained_handler_t handler = registered_signal_handlers[signum][i];
    if( handler == NULL ) continue;
    if( signum == SIGSEGV && i==AZUL_EXCPT_HNDLR_FORTH )
      thr->_libos01 = 2; // nested SEGV in NIO assert flag

    result = (*handler)(signum, pSiginfo, pUcontext);

    if( result != AZUL_SIGNAL_NOT_HANDLED ) break; // Somebody handled the signal!
  } // end of for()
  // 'result' is NULL (if nobody handled signal) or some flag or a PC.

  if ( result == AZUL_SIGNAL_SKIP_INSTRUCTION ) {
    os_unimplemented();
  }

  // Convenience return result, since caller can return the RIP just as easily.
  if( result == AZUL_SIGNAL_RESUME_EXECUTION )
    result = (address_t)pUcontext->uc_mcontext.gregs[REG_RIP];

  // No volunteer to handle this signal from the 1st four handlers.
  if( !result || result == AZUL_SIGNAL_NOT_HANDLED ) {
    // The last handler is the handler of last resort and we do not attempt to
    // run it on the alt-sig-stack.  Set up to "call" the last handler -
    // really jump to it.  The ucontext & siginfo structs remain alive on the
    // alt-sig-stack for the moment.
    azul_chained_handler_t handler = registered_signal_handlers[signum][AZUL_MAX_CHAINED_HANDLERS-1];
    if( handler ) {
      ThreadBase *thr= (ThreadBase*)(((intptr_t)&handler) & ~(thread_stack_size - 1));
      // Mimic a push of the faulting address onto the stack
      intptr_t rsp = pUcontext->uc_mcontext.gregs[REG_RSP];
      rsp -= 8;                   // 'push' lowers stack
      *(intptr_t*)rsp = pUcontext->uc_mcontext.gregs[REG_RIP];
      pUcontext->uc_mcontext.gregs[REG_RSP] = rsp;
      // Mimic a long jump to the handler.
      thr->_libos_eva = (intptr_t)pSiginfo->si_addr;
      thr->_libos_epc = pUcontext->uc_mcontext.gregs[REG_RIP];
      result = handler;
    }
  }

  if( !result ) // Nobody likes to handle this signal?  Die now.
    _os_breakpoint();

  // Jam in the continuation PC when the handler unwinds
  pUcontext->uc_mcontext.gregs[REG_RIP] = (greg_t) result;

  // Clear the nested-segv-assert flag
  if( signum == SIGSEGV ) 
    thr->_libos01 = 0;
}


// TODO: exception_register_chained_handler() and exception_unregister_chained_handler()
// TODO: should probably use locks so that they're safe to call concurrently.

//******************************************************************************
//
// FUNC: exception_register_chained_handler()
//
// SYNOPSIS: Register a new chained signal handler for a signal.
//
// DESCRIPTION: Register a new chained signal handler for a signal.  
//   Handlers will normally be called in the order they are registered, but 
//   unregistering and reregistering handlers may result in a different order 
//   if it is specified differently on the second registration call.
//
// PARAMETERS: 
// 
//   signum: INPUT - the OS defined exception signal number.
//   handler: INPUT - the handler fucntion to execute when the exception occurs.
//   order: INPUT - where in the list (for that signum) the handler will be placed.
//
// RETURNS: sys_return_t.
//   SYSERR_NONE: if successful.
//   SYSERR_INVALID_ARGUMENT: invalid signal number or order value.
//   SYSERR_EXISTS: duplicate identical position/function reregistration of a handler.
//
// BASIC ALGORITHM: 
// 
//   Step 1: Check the paramters passed to us.  Make sure the signal number isn't
//           already registered as a "direct" signal handler.
//   Step 2: If this signal is already registered with the same handler, or if the
//           handler slot is already occupied with this specific handler, we're done.
//   Step 3: Insert the handler into it's order # slot.  We need to register the 
//           default signal_chain_handler() with the OS, which will pass this on
//           (in order) to the appropriate handler, so we set up the appropriate
//           information in the structure.  We don't "mask off" any signals
//           we decide to deliver, so we use the entire mask macro sigfillset().  
//   Step 4: For SIGSEGV we also require an alternate stack.  
//   Step 5: Make the signal_chain_handler known to the kernel.  This should never
//           fail, if it does, something is very broken.
//
//
//******************************************************************************

sys_return_t exception_register_chained_handler(int signum, azul_chained_handler_t handler, int order) {
  // Step 1.
  if( signum<1 || signum>=NSIG ) 
    return SYSERR_INVALID_ARGUMENT;

  if( order<0 || order>=AZUL_MAX_CHAINED_HANDLERS )
    return SYSERR_INVALID_ARGUMENT;

  if( registered_signal_handlers[signum][AZUL_DIRECT_HANDLER] != NULL ) 
    return SYSERR_EXISTS;

  // Step 2.
  if (registered_signal_handlers[signum][order] == handler)
    return SYSERR_NONE;

  if( registered_signal_handlers[signum][order] != NULL )
    return SYSERR_EXISTS;

  // Step 3.
  registered_signal_handlers[signum][order] = handler;

  struct sigaction action;
  memset(&action,0,sizeof(action));
  const int res1 = sigfillset(&(action.sa_mask));
  os_guarantee( !res1, "sigfill");

  // Allow SEGVs in all signal handlers including SIGSEGV.  Stack overflows
  // appear as SEGVs and the SEGV handler (while running on the alternate 
  // stack) silently extends the main stack.  If another signal handler 
  // trips over the edge of the stack, we need to handle the SEGV to allow
  // it to complete.  Ignoring the SEGV will silently cause the process to die.
  const int res3 = sigdelset(&(action.sa_mask),SIGSEGV);
  os_guarantee( !res3, "sigdelset");
  action.sa_sigaction = (void(*)(int, siginfo_t*, void*)) signal_chain_handler;
  action.sa_flags     = SA_SIGINFO | SA_RESTART; 
  if( signum == SIGSEGV )
    action.sa_flags |= SA_NODEFER;

  // Step 4.
  if (signum == SIGSEGV)
    action.sa_flags |= SA_ONSTACK;

  // Step 5.
  const int res2 = sigaction(signum, &action, NULL);
  os_guarantee( !res2, "sigaction");

  return SYSERR_NONE;
} // end of exception_register_chained_handler()


//******************************************************************************
//
// FUNC: exception_unregister_chained_handler()
//
// SYNOPSIS: Remove a previously registered chained signal handler for a signal.
//
// DESCRIPTION: Remove a previously registered chained signal handler for a 
//   signal from a specific position in the list.  It's an error if the handler 
//   isn't currently registered.
//
// PARAMETERS: 
//   signum: INPUT - the OS defined exception signal number.
//   handler: INPUT - the handler fucntion to execute when the exception occurs.
//   order: INPUT - where in the list (for that signum) the handler will be placed.
//
// RETURNS: sys_return_t.
//   SYSERR_NONE: if successful.
//   SYSERR_INVALID_ARGUMENT: invalid signal number.
//   SYSERR_NOT_FOUND: no handler present for that signum.
//
// BASIC ALGORITHM: 
// 
//   Find the first handler in the list for that signum and remove it.
//
//******************************************************************************
sys_return_t exception_unregister_chained_handler(int signum, azul_chained_handler_t handler)
{
  if ( signum<1 || signum>=NSIG ) {
    // Invalid signal number.
    os_assert( signum>0 || signum<NSIG, "Invalid signal number in exception_unregister_chained_handler()");
    return SYSERR_INVALID_ARGUMENT;
  }

  // Walk the chained handlers looking for the handler in question.
  int i;
  for ( i=0; i<AZUL_MAX_CHAINED_HANDLERS; i++ ) 
  {
    if ( registered_signal_handlers[signum][i] == handler ) 
    {
      registered_signal_handlers[signum][i] = NULL;

      // TODO:  If the entire array is empty for a particular
      // TODO:  signal, do we want to unregister for it?
      return SYSERR_NONE;
    }
  }

  return SYSERR_NOT_FOUND;
}


// TODO: exception_register_direct_handler() and exception_unregister_direct_handler()
// TODO: should probably use locks so that they're safe to call concurrently.

//******************************************************************************
//
// FUNC: exception_register_direct_handler()
//
// SYNOPSIS: Register a new direct signal handler for a signal.
//
// DESCRIPTION: Register a new direct signal handler for a signal.  
//   This API is provided to allow callers a single, monitored entry point
//   for all exception handlers.  It provides a means of noticing when two
//   separate applications are attempting to register for the same exception,
//   as well as noticing conflicts with chained signal handlers.  We "mask 
//   off" all other signals during its execution, by default, as the expectation
//   is that only high duty cycle, very efficient handlers will be used.
//
// PARAMETERS: 
// 
//   signum: INPUT - the OS defined exception signal number.
//   handler: INPUT - the handler fucntion to execute when the exception occurs.
//
// RETURNS: sys_return_t.
//   SYSERR_NONE: if successful.
//   SYSERR_INVALID_ARGUMENT: invalid signal number.
//   SYSERR_EXISTS: duplicate identical position/function reregistration of a handler.
//
// BASIC ALGORITHM: 
// 
//   Step 1: Check the paramters passed to us.  If this signal is already registered 
//           either as a chained signal handler, or with a prior direct handler, then
//           it is an error,
//   Step 2: Set up the direct signal handler.  We use the empty set, to force masking
//           other other signals being delivered during this handler's execution.
//   Step 3: Make the signal_chain_handler known to the kernel.  This should never
//           fail, if it does, something is very broken.
//
//
//******************************************************************************

sys_return_t exception_register_direct_handler(int signum, azul_direct_handler_t handler) {
  // Step 1.
  if( signum<1 || signum>=NSIG || signum == SIGSEGV)
    return SYSERR_INVALID_ARGUMENT;

  if( registered_signal_handlers[signum][AZUL_DIRECT_HANDLER] ) 
    return SYSERR_EXISTS;

  for( int i=0; i<AZUL_MAX_CHAINED_HANDLERS; i++ ) {
    os_assert( registered_signal_handlers[signum][i], "Signal number already registered as direct handler()");
    if (registered_signal_handlers[signum][i] ) {
      return SYSERR_EXISTS;
    }
  }

  // Step 2.
  registered_signal_handlers[signum][AZUL_DIRECT_HANDLER] = handler;

  struct sigaction action;
  const int res1 = sigfillset(&(action.sa_mask));
  os_guarantee( !res1, "sigfill");
  action.sa_sigaction = (void(*)(int, siginfo_t*, void*)) handler;
  action.sa_flags     = SA_SIGINFO | SA_RESTART; 

  // Step 3.
  const int res2 = sigaction(signum, &action, NULL);
  os_guarantee( !res2, "sigaction");
  return SYSERR_NONE;
} // end of exception_register_direct_handler()


//******************************************************************************
//
// FUNC: exception_unregister_direct_handler()
//
// SYNOPSIS: Remove a previously registered direct signal handler for a signal.
//
// DESCRIPTION: Remove a previously registered direct signal handler for a 
//   signal from a specific position in the list.  It's an error if the handler 
//   isn't currently registered.
//
// PARAMETERS: 
//   signum: INPUT - the OS defined exception signal number.
//   handler: INPUT - the handler fucntion to execute when the exception occurs.
//
// RETURNS: sys_return_t.
//   SYSERR_NONE: if successful.
//   SYSERR_INVALID_ARGUMENT: invalid signal number, or a different direct handler
//     was registered, than the one being freed.
//   SYSERR_NOT_FOUND: no handler present for that signum.
//
// BASIC ALGORITHM: 
// 
//   Find the handler in the list for that signum and remove it.  It is an error
//   if it didn't exist.
//
//******************************************************************************

sys_return_t exception_unregister_direct_handler(int signum, azul_direct_handler_t handler) {
  if ( signum<1 || signum>=NSIG || signum == SIGSEGV)
    return SYSERR_INVALID_ARGUMENT;

  if (registered_signal_handlers[signum][AZUL_DIRECT_HANDLER] != handler) 
    return SYSERR_INVALID_ARGUMENT;

  registered_signal_handlers[signum][AZUL_DIRECT_HANDLER] = NULL;

  struct sigaction action;
  sigemptyset(&(action.sa_mask));
  action.sa_sigaction = (void(*)(int, siginfo_t*, void*)) NULL;
  action.sa_flags     = SA_SIGINFO | SA_RESTART; 

  // Step 3.
  const int res = sigaction(signum, &action, NULL);
  os_guarantee( res == 0, "sigaction failed" );

  return SYSERR_NONE;
}  // end of sys_return_t exception_unregister_direct_handler()


// end of exceptions.c file.
