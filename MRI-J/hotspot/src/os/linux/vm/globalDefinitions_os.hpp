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
#ifndef GLOBALDEFINITIONS_OS_HPP
#define GLOBALDEFINITIONS_OS_HPP


#include "jni.h"

// This file holds compiler-dependent includes,
// globally used constants & types, class (forward)
// declarations and a few frequently used utility functions.

# include <ctype.h>
# include <string.h>
# include <stdarg.h>
#if defined(__GNU_CC)
_BEGIN_STD_C
  # include <ieeefp.h>
_END_STD_C
#endif /* __GNU_CC__ */
# include <stddef.h>      // for offsetof
# include <stdio.h>
# include <stdlib.h>
# include <stdarg.h>
# include <stdint.h>		// int64_t etc.
# include <sys/param.h>         // For MAXPATHLEN
# include <unistd.h>		// POSIX I/O etc.
# include <math.h>
# include <float.h>       // DBL_MIN, DBL_MAX, etc.
# include <time.h>

# include <dirent.h>		// opendir etc.
# include <sys/stat.h>		// stat etc.
# include <limits.h>
# include <errno.h>
# include <pthread.h>

#ifndef AZ_PROXIED // FIXME - Need to reconcile az_pgroup.h and process.h
#include <aznix/az_pgroup.h>
#endif // !AZ_PROXIED
# include <os/os.h>
# include <os/profile.h>

#if defined(AZ_PROXIED)
# include <proxy/proxy.h>
# include <proxy/proxy_io.h>
# include <proxy/proxy_network.h>
# include <proxy/proxy_compat.h>
# include <proxy/proxy_java.h>
# include <proxy/proxy_be.h>

# include <azpr/azpr_error.h>
#endif // defined(AZ_PROXIED)

#ifdef AZ_PROFILER
# include <azprof/azprof_io.hpp>
#endif // AZ_PROFILER

// Define various constants for virtual memory page sizes.  Having compiler
// accessible constants is beneficial for fast code in GPGC.
const long     LogBytesPerSmallPage = 12;  // 2^12 = 4 KB page size
const long     LogWordsPerSmallPage = LogBytesPerSmallPage - LogBytesPerWord;
const intptr_t BytesPerSmallPage    = 1 << LogBytesPerSmallPage;
const intptr_t WordsPerSmallPage    = 1 << LogWordsPerSmallPage;

const long     LogBytesPerLargePage = 21;  // 2^21 = 2 MB page size
const long     LogWordsPerLargePage = LogBytesPerLargePage - LogBytesPerWord;
const intptr_t BytesPerLargePage    = 1 << LogBytesPerLargePage;
const intptr_t WordsPerLargePage    = 1 << LogWordsPerLargePage;


// Max thread stack size and alignment are constants used in a variety of place:
const long     LogBytesPerThreadStack = thread_stack_shift;
const intptr_t BytesPerThreadStack    = 1 << LogBytesPerThreadStack;

const intptr_t ThreadStackRegionSize  = (__THREAD_STACK_REGION_END_ADDR__ - __THREAD_STACK_REGION_START_ADDR__);


#ifdef AZ_PROXIED
// Use the proxy-defined MAXPATHLEN instead of the one picked up from unistd.h
#undef MAXPATHLEN
#define MAXPATHLEN PROXY_MAXPATHLEN
#endif // AZ_PROXIED

// Additional Java basic types

typedef unsigned char      jubyte;
typedef unsigned short     jushort;
typedef unsigned int       juint;
typedef unsigned long long julong;

//----------------------------------------------------------------------------------------------------
// Special (possibly not-portable) casts
// Cast floats into same-size integers and vice-versa w/o changing bit-pattern
// %%%%%% These seem like standard C++ to me--how about factoring them out? - Ungar

inline jint    jint_cast   (jfloat  x)           { return *(jint*   )&x; }
inline jlong   jlong_cast  (jdouble x)           { return *(jlong*  )&x; }

inline jfloat  jfloat_cast (jint    x)           { return *(jfloat* )&x; }
inline jdouble jdouble_cast(jlong   x)           { return *(jdouble*)&x; }

//----------------------------------------------------------------------------------------------------
// Constant for jlong (specifying an long long constant is C++ compiler specific)

// Build a 64bit integer constant
#define CONST64(x)  (x ## LL)
#define UCONST64(x) (x ## ULL)

const jlong min_jlong = CONST64(0x8000000000000000);
const jlong max_jlong = CONST64(0x7fffffffffffffff);

//----------------------------------------------------------------------------------------------------
// More basic data types

typedef void *address_t;                // User address space pointer

typedef pthread_key_t thread_key_t;     // Thread local storage keys

//----------------------------------------------------------------------------------------------------
// Debugging

#define DEBUG_EXCEPTION ::abort();

#define BREAKPOINT __asm__ __volatile__("int $3")

// Note that this must be a macro and not a function call: if we nest a call we blow the RPC hint.
#define GET_RPC intptr_t RPC = (intptr_t)__builtin_return_address(0);
#define GET_RSP intptr_t RSP = (intptr_t)__builtin_frame_address(0);

// checking for nanness

inline int g_isnan(float  f) { return isnanf(f); }
inline int g_isnan(double f) { return isnan(f); }

// Checking for finiteness

inline int g_isfinite(jfloat  f)                 { return finite(f); }
inline int g_isfinite(jdouble f)                 { return finite(f); }


// Portability macros
#define PRAGMA_INTERFACE      
#define PRAGMA_IMPLEMENTATION
#define PRAGMA_IMPLEMENTATION_(arg)
// Make VALUE_OBJ_CLASS_SPEC not inherit from any special class. This because gcc tends to
// insert bogus fields into empty subclasses, thus voiding assumptions about class sizes in
// generated native code.
#define VALUE_OBJ_CLASS_SPEC

// Formatting.
#define FORMAT64_MODIFIER "l"

typedef void (*SafepointEndCallback) (void* user_data);

// HACK: gcc warns about applying offsetof() to non-POD object or calculating
//       offset directly when base address is NULL. Use 16 to get around the 
//       warning. gcc-3.4 has an option -Wno-invalid-offsetof to suppress
//       this warning.
#define offset_of(klass,field) (size_t)((intx)&(((klass*)16)->field) - 16)

#ifdef offsetof
# undef offsetof
#endif
#define offsetof(klass,field) offset_of(klass,field)



//----------------Porting-Code------------------------------------------//
// TODO: Rework all this code below.

// From os/thread.h  With the x86 thread management scheme, perhaps this is really closer to 4KB?
#define USER_THREAD_SPECIFIC_DATA_SIZE 1024

#ifdef AZ_PROXIED
// Define an alias for the azpr errorcode type.
#define proxy_error_t azpr_error_t
#endif // AZ_PROXIED

#ifndef AZ_PROFILER
// If AZ_PROFILER is not turned on, define these dummy classes to satisfy compiles.
namespace azprof {
  class Request {
  };

  class Response {
  };
}
#endif // !AZ_PROFILER

#endif // GLOBALDEFINITIONS_OS_HPP
