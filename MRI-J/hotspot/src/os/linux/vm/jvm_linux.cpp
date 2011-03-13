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


#include "globalDefinitions.hpp"
#include "jvm_os.h"
#include "os.hpp"

#include <signal.h>

extern "C" {

  // --- JVM_RegisterSignal ------------------------------------------------------
  // Essentially this is the linux version from Sun's HotSpot.
  // In a proxied AVM, this call happens on the proxy.
// sun.misc.Signal ///////////////////////////////////////////////////////////
void*JVM_RegisterSignal(jint sig,void*handler){
  //  ShouldNotReachHere();
  //  return NULL;
  // Copied from classic vm
  // signals_md.c	1.4 98/08/23
  void* newHandler = handler == (void *)2
                   ? os::user_handler()
                   : handler;
  switch (sig) {
    /* The following are already used by the VM. */
    case INTERRUPT_SIGNAL:
    case SIGFPE:
    case SIGILL:
    case SIGSEGV:

    /* The following signal is used by the VM to dump thread stacks unless
       ReduceSignalUsage is set, in which case the user is allowed to set
       his own _native_ handler for this signal; thus, in either case,
       we do not allow JVM_RegisterSignal to change the handler. */
    case BREAK_SIGNAL:
      return (void *)-1;

    /* The following signals are used for Shutdown Hooks support. However, if
       ReduceSignalUsage (-Xrs) is set, Shutdown Hooks must be invoked via 
       System.exit(), Java is not allowed to use these signals, and the the 
       user is allowed to set his own _native_ handler for these signals and
       invoke System.exit() as needed. Terminator.setup() is avoiding 
       registration of these signals when -Xrs is present. 
       - If the HUP signal is ignored (from the nohup) command, then Java
         is not allowed to use this signal.
     */ 

    case SHUTDOWN1_SIGNAL:
    case SHUTDOWN2_SIGNAL:
    case SHUTDOWN3_SIGNAL:
      if (ReduceSignalUsage) return (void*)-1;
if(os::Linux_OS::is_sig_ignored(sig))return(void*)1;
  }

  void* oldHandler = os::signal(sig, newHandler);
  if (oldHandler == os::user_handler()) {
      return (void *)2;
  } else {
      return oldHandler;
  }

}

jboolean JVM_RaiseSignal(jint sig)
{
  // Notify the java signal handler thread
os::signal_notify(sig);
  return JNI_TRUE;
}


  // --- JVM_FindSignal ------------------------------------------------------
  // Essentially this is the linux version from Sun's HotSpot.
  // In a proxied AVM, this call happens on the proxy.

/* 
  All the defined signal names for Linux. 

  NOTE that not all of these names are accepted by our Java implementation

  Via an existing claim by the VM, sigaction restrictions, or
  the "rules of Unix" some of these names will be rejected at runtime.
  For example the VM sets up to handle USR1, sigaction returns EINVAL for 
  STOP, and Linux simply doesn't allow catching of KILL.

  Here are the names currently accepted by a user of sun.misc.Signal with 
  1.4.1 (ignoring potential interaction with use of chaining, etc):

    HUP, INT, TRAP, ABRT, IOT, BUS, USR2, PIPE, ALRM, TERM, STKFLT,
    CLD, CHLD, CONT, TSTP, TTIN, TTOU, URG, XCPU, XFSZ, VTALRM, PROF,
    WINCH, POLL, IO, PWR, SYS

*/

struct siglabel {
  const char *name;
  int   number;
};

struct siglabel siglabels[] = {
  /* derived from /usr/include/bits/signum.h on RH7.2 */
{"HUP",SIGHUP},/*Hangup (POSIX).*/
{"INT",SIGINT},/*Interrupt (ANSI).*/
{"QUIT",SIGQUIT},/*Quit (POSIX).*/
{"ILL",SIGILL},/*Illegal instruction (ANSI).*/
{"TRAP",SIGTRAP},/*Trace trap (POSIX).*/
{"ABRT",SIGABRT},/*Abort (ANSI).*/
{"IOT",SIGIOT},/*IOT trap (4.2 BSD).*/
{"BUS",SIGBUS},/*BUS error (4.2 BSD).*/
{"FPE",SIGFPE},/*Floating-point exception (ANSI).*/
{"KILL",SIGKILL},/*Kill, unblockable (POSIX).*/
{"USR1",SIGUSR1},/*User-defined signal 1 (POSIX).*/
{"SEGV",SIGSEGV},/*Segmentation violation (ANSI).*/
{"USR2",SIGUSR2},/*User-defined signal 2 (POSIX).*/
{"PIPE",SIGPIPE},/*Broken pipe (POSIX).*/
{"ALRM",SIGALRM},/*Alarm clock (POSIX).*/
{"TERM",SIGTERM},/*Termination (ANSI).*/
#ifdef SIGSTKFLT
{"STKFLT",SIGSTKFLT},/*Stack fault.*/
#endif
{"CLD",SIGCLD},/*Same as SIGCHLD (System V).*/
{"CHLD",SIGCHLD},/*Child status has changed (POSIX).*/
{"CONT",SIGCONT},/*Continue (POSIX).*/
{"STOP",SIGSTOP},/*Stop, unblockable (POSIX).*/
{"TSTP",SIGTSTP},/*Keyboard stop (POSIX).*/
{"TTIN",SIGTTIN},/*Background read from tty (POSIX).*/
{"TTOU",SIGTTOU},/*Background write to tty (POSIX).*/
{"URG",SIGURG},/*Urgent condition on socket (4.2 BSD).*/
{"XCPU",SIGXCPU},/*CPU limit exceeded (4.2 BSD).*/
{"XFSZ",SIGXFSZ},/*File size limit exceeded (4.2 BSD).*/
{"VTALRM",SIGVTALRM},/*Virtual alarm clock (4.2 BSD).*/
{"PROF",SIGPROF},/*Profiling alarm clock (4.2 BSD).*/
{"WINCH",SIGWINCH},/*Window size change (4.3 BSD, Sun).*/
{"POLL",SIGPOLL},/*Pollable event occurred (System V).*/
{"IO",SIGIO},/*I/O now possible (4.2 BSD).*/
{"PWR",SIGPWR},/*Power failure restart (System V).*/
#ifdef SIGSYS
{"SYS",SIGSYS}/*Bad system call. Only on some Linuxen!*/
#endif
  };

jint JVM_FindSignal(const char *name) {
//  ShouldNotReachHere();
//  return 0;
  // Essentially this is the linux version from Sun's HotSpot.
  // In a proxied AVM, this call happens on the proxy.
  

  /* find and return the named signal's number */

  for(uint i=0; i<ARRAY_SIZE(siglabels); i++)
    if(!strcmp(name, siglabels[i].name))  
      return siglabels[i].number;

  return -1;
}

}
