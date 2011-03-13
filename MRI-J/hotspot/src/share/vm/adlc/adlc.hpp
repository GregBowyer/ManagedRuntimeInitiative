/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


//
// Standard include file for ADLC parser
//

// standard library constants
#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/types.h>

using namespace std;

#define bool    int			

/* Make sure that we have the intptr_t and uintptr_t definitions */
#ifdef _WIN32
#ifndef _INTPTR_T_DEFINED
#ifdef _WIN64
typedef __int64 intptr_t;
#else
typedef int intptr_t;
#endif
#define _INTPTR_T_DEFINED
#endif

#ifndef _UINTPTR_T_DEFINED
#ifdef _WIN64
typedef unsigned __int64 uintptr_t;
#else
typedef unsigned int uintptr_t;
#endif
#define _UINTPTR_T_DEFINED
#endif
#endif // _WIN32

#if defined(__GNUC__)
  #include <stdint.h>
#endif
#ifdef LINUX
  #include <inttypes.h>
#endif // LINUX

// Macros 
#define uint32 unsigned int
#define uint   unsigned int

// Macros
// Debugging note:  Put a breakpoint on "abort".
#if defined(assert)
#undef assert
#endif

#define assert(cond, msg) { if (!(cond)) { fprintf(stderr, "assert fails %s %d: %s\n", __FILE__, __LINE__, msg); abort(); }}
#if !defined(max)
  #define max(a, b)   (((a)>(b)) ? (a) : (b))
#endif

// VM components
#include "opcodes.hpp"

// ADLC components
#include "arena.hpp"
#include "adlcVMDeps.hpp"
#include "filebuff.hpp"
#include "dict2.hpp"
#include "forms.hpp"
#include "formsopt.hpp"
#include "formssel.hpp"
#include "archDesc.hpp"
#include "adlparse.hpp"

// globally define ArchDesc for convenience.  Alternatively every form
// could have a backpointer to the AD but it's too complicated to pass
// it everywhere it needs to be available.
extern ArchDesc* globalAD;
