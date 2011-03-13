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

// errors.c - Error codes for Aztek OS Services

// The mapping from error code to error string is provided here ...

#include <os/errors.h>
#include <stdlib.h>
#include <string.h>
#include <os/log.h>
#include <os/utilities.h>


const char *error_message(sys_return_t code)
{
    return (const char *)strerror(code);
}

// Print out a message something like:
// "Kernel resource limit reached: So said the god of memory allocation"
void error_print(const char *msg, sys_return_t code) {
    log_error(LOG_DEFAULT_FACILITY, code, msg);
}
