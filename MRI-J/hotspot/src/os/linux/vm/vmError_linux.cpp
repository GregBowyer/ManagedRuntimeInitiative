/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "os.hpp"
#include "ostream.hpp"
#include "vmError.hpp"
#include "os/exceptions.h"

#include "allocation.inline.hpp"

#include <sys/types.h>
#if 0 // FIXME - Remove this if 0 when unistd.h is removed from the proxy directory
#include <sys/wait.h>
#endif // 0
#include <signal.h>

extern char** environ;

char* do_you_want_to_debug(const char *message) {
static char buf[256];

  jio_snprintf(buf, sizeof(buf),
"%s\n\nTo debug, use 'azgdb'; attach to process %lld, then switch to thread ?\n"
              "Otherwise, press RETURN to abort...",
              message, os::process_id());

  return buf;
}

bool VMError::show_message_box(char *buf, int buflen) {
  bool yes;
  error_string(buf, buflen);
  int len = (int)strlen(buf);
  char *p = &buf[len];

  jio_snprintf(p, buflen - len, do_you_want_to_debug(""));

  yes = os::message_box(_message != NULL ? _message : "Unexpected Error", buf, "Do you want to debug the problem?");

  if (yes) {
    // if the user wants to debug a debugger better be attached as the message box suggests
    BREAKPOINT;
  }
  return yes;
}

extern "C" address_t jvm_exception_handler(int signum, siginfo_t* si, ucontext_t* uc);
extern "C" address_t jvm_unexpected_exception_handler(int signum, siginfo_t* si, ucontext_t* uc);

void VMError::reset_signal_handlers() {
  // If we get here, we want to get out of the way of certain exceptions in future,
  // since this could be triggered by running out of memory.
  // Keep address fault handling for now
  // int rc = exception_unregister_chained_handler(SIGSEGV, jvm_address_fault_exception_handler);
  // guarantee(rc, "Error unregistering jvm_address_fault_exception_handler");

  //
  // Unimplemented();
  //
  // (11/18/09 bean) I removed the call to Unimplemented() in order to implement 
  // reset_signal_handlers for the x86 platform.  From looking at old code, it looks like 
  // the correct behavior should be to unregister SIGBUS, SEGV, etc handlers and register 
  // the jvm_unexpected_exception_handler.
  //
  // Now, let's consider what this method's real purpose is.  The native hotspot code removes
  // the SIGSEGV and SIGBUS handlers and replaces them with the crash_handler().  The only thing
  // crash_handler() does is to call VMError::report_and_die().  And report_and_die() has a 
  // mechanism to handle recursive exceptions.   So this implies that the purpose of this
  // method is remove any "smart" handlers that recover from signals and instead, just treat
  // signals as fatal errors and die as quickly as possible.
  //
  // So, here's the plan ...  (thunder rolls, ominous sounds come from deep below the earth)
  //
  // We unregister the jvm_exception_handler from SIGSEGV and SIGBUS and replace it with
  // jvm_unexpected_exception_handler.  Note that we're not removing the first two 
  // SIGSEGV handlers, StackSegVHandle which is in slot two and <NULL> in slot one.  
  // [apparently, we've reserved the slot for now?]
  //
    sys_return_t rc;
    rc = exception_unregister_chained_handler(SIGSEGV,jvm_exception_handler);
    guarantee(rc==SYSERR_NONE, "Error unregistering jvm_exception_handler for SIGSEGV in VMError::reset_signal_handlers()" );

    rc = exception_register_chained_handler(SIGSEGV,jvm_unexpected_exception_handler,AZUL_EXCPT_HNDLR_THIRD);
    guarantee(rc==SYSERR_NONE, "Error registering jvm_unexpected_exception_handler for SIGSEGV in VMError::reset_signal_handlers()" );

    rc = exception_unregister_chained_handler(SIGBUS,jvm_exception_handler);
    guarantee(rc==SYSERR_NONE, "Error unregistering jvm_exception_handler for SIGBUS in VMError::reset_signal_handlers()" );

    rc = exception_register_chained_handler(SIGBUS,jvm_unexpected_exception_handler,AZUL_EXCPT_HNDLR_FIRST);
    guarantee(rc==SYSERR_NONE, "Error registering jvm_unexpected_exception_handler for SIGBUS in VMError::reset_signal_handlers()" );

}
