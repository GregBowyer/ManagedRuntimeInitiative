/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
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

#ifndef JVM_OS_H
#define JVM_OS_H
/*
// HotSpot integration note:
//
// This is derived from the JDK classic file:
// "$JDK/src/solaris/javavm/export/jvm_md.h":15 (ver. 1.10 98/04/22)
// All local includes have been commented out.
*/


#ifndef JVM_MD_H
#define JVM_MD_H

/*
 * This file is currently collecting system-specific dregs for the
 * JNI conversion, which should be sorted out later.
 */

//#include <dirent.h>		/* For DIR */
//#include <sys/param.h>		/* For MAXPATHLEN */
//#include <unistd.h>		/* For F_OK, R_OK, W_OK */
//#include <sys/int_types.h>	/* for intptr_t types (64 Bit cleanliness) */

#define JNI_ONLOAD_SYMBOLS {"JNI_OnLoad_java"}

#define JNI_ONUNLOAD_SYMBOLS {"JNI_OnUnload_java"}
#define JVM_ONLOAD_SYMBOLS   {"JVM_OnLoad"}
#define AGENT_ONLOAD_SYMBOLS    {"Agent_OnLoad"} 
#define AGENT_ONUNLOAD_SYMBOLS  {"Agent_OnUnload"}
#define AGENT_ONATTACH_SYMBOLS  {"Agent_OnAttach"}

// Owned by the proxy
//#define JNI_LIB_PREFIX "lib"
//#define JNI_LIB_SUFFIX ".so"

#define JVM_MAXPATHLEN MAXPATHLEN

// Not used anywhere inside the JVM ...
//#define JVM_R_OK    R_OK
//#define JVM_W_OK    W_OK
//#define JVM_X_OK    X_OK
//#define JVM_F_OK    F_OK

/*
 * File I/O
 */

//#include <sys/types.h>
//#include <sys/stat.h>
//#include <fcntl.h>
//#include <errno.h>

/* O Flags */

//#define JVM_O_RDONLY     O_RDONLY
//#define JVM_O_WRONLY     O_WRONLY
//#define JVM_O_RDWR       O_RDWR
//#define JVM_O_O_APPEND   O_APPEND
//#define JVM_O_EXCL       O_EXCL
//#define JVM_O_CREAT      O_CREAT

#define SIGBREAK 0  // Not used, just defined here to satisy a call in os.cpp(shared code)

/* Signal definitions */

#define BREAK_SIGNAL     SIGQUIT           /* Thread dumping support.    */
#define INTERRUPT_SIGNAL SIGUSR1           /* Interruptible I/O support. */
#define SHUTDOWN1_SIGNAL SIGHUP            /* Shutdown Hooks support.    */
#define SHUTDOWN2_SIGNAL SIGINT
#define SHUTDOWN3_SIGNAL SIGTERM


#endif /* JVM_MD_H */

#endif // JVM_OS_H
