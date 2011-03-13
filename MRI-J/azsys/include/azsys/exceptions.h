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

// exceptions.h - Exception handling subsystem for generic Linux  OS Services

#ifndef _OS_EXCEPTIONS_H_
#define _OS_EXCEPTIONS_H_ 1

#include <signal.h>
#include <ucontext.h>

#include <os/types.h>


#ifdef __cplusplus
extern "C" {
#endif

// User-level chained signal handlers.
//
// Chained signal handlers can return one of three results:
//
//   AZUL_SIGNAL_NOT_HANDLED:      This handler didn't process the signal, invoke the next chained signal handler.
//   AZUL_SIGNAL_RESUME_EXECUTION: The handler processed the signal, resume at the point the signal occurred. 
//   AZUL_SIGNAL_SKIP_INSTRUCTION: The handler processed the signal, resume at the instruction following where the signal occurred.
//   address_t:                    The handler processed the signal, resume at the returned address.
//
// TODO: AZUL_SIGNAL_SKIP_INSTRUCTION is not implemented: This would return at the instruction after the one that
//       triggered the signal.  To do this, I'd need to parse the x86 ASM to figure out the IP of the next
//       instruction along.
//
// Handler order:
//
//   Multiple handlers can be registered for a signal.  (Up to AZUL_MAX_CHAINED_HANDLERS)
//   In order to provide deterministic ordering of which handlers are called,
//   each handler is registered with an order #.  Handlers are invoked, going from
//   order 0 to AZUL_MAX_CHAINED_HANDLERS-1, until a handler returns something other
//   than AZUL_SIGNAL_NOT_HANDLED.
//
//   It is an error to register a handler with a particular order # if there is already a handler
//   registered with that order #.  exception_unregister_chained_handler() should be used to
//   unregister a handler before replacing it with a new handler.
//

// Handler function type:
typedef address_t (*azul_chained_handler_t) (int, siginfo_t *, ucontext_t *);
typedef address_t (*azul_direct_handler_t)  (int, siginfo_t *, ucontext_t *);

// Handler return values:
#define AZUL_SIGNAL_NOT_HANDLED      ((address_t)-1)
#define AZUL_SIGNAL_RESUME_EXECUTION ((address_t)-2)
#define AZUL_SIGNAL_SKIP_INSTRUCTION ((address_t)-3)
#define AZUL_SIGNAL_ERROR            ((address_t)-4)

// Maximum number of handlers that may be registered for a particular signal.
// The maximum order # that can be specified when registering a handler
// is AZUL_MAX_CHAINED_HANDLERS-1.
#define AZUL_MAX_CHAINED_HANDLERS 5

// The Signal Handler array has AZUL_MAX_CHAINED_HANDLERS + 1 entries, the
// last entry being reserved for the "direct" handler for the signal.  You
// may not have both a chained signal handler, and a direct signal handler
// simultaneously present.
#define AZUL_MAX_HANDLERS            (AZUL_MAX_CHAINED_HANDLERS + 1)
#define AZUL_DIRECT_HANDLER          (AZUL_MAX_CHAINED_HANDLERS)  // The last offset in the array.

// The exception handler order is defined a priori.
// Do not change the first entry in this list, nor assign it a value other
// than zero!!  Make sure each successive entry in the list is one greater
// than the next, and that the last entry equals AZUL_MAX_CHAINED_HANDLERS-1.
#define AZUL_EXCPT_HNDLR_FIRST       (0)
#define AZUL_EXCPT_HNDLR_SECOND      (AZUL_EXCPT_HNDLR_FIRST + 1)     
#define AZUL_EXCPT_HNDLR_THIRD       (AZUL_EXCPT_HNDLR_SECOND + 1)     
#define AZUL_EXCPT_HNDLR_FORTH       (AZUL_EXCPT_HNDLR_THIRD + 1)     
#define AZUL_EXCPT_HNDLR_FIFTH       (AZUL_EXCPT_HNDLR_FORTH + 1)     
// This must be the last exception handler define.  Do not exceed 
// AZUL_MAX_CHAINED_HANDLERS-1, This define must equal it.
#define AZUL_EXCPT_HNDLR_LAST        (AZUL_EXCPT_HNDLR_FIFTH + 1)

//
// User-level (chained) signal handlers register/unregister using these functions:
//
extern sys_return_t exception_register_chained_handler  (int signum, azul_chained_handler_t handler, int order);
extern sys_return_t exception_unregister_chained_handler(int signum, azul_chained_handler_t handler);

//
// User-level (direct) signal handlers register/unregister using these functions:
//
extern sys_return_t exception_register_direct_handler  (int signum, azul_direct_handler_t handler);
extern sys_return_t exception_unregister_direct_handler(int signum, azul_direct_handler_t handler);


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
  
#endif // _OS_EXCEPTIONS_H_
