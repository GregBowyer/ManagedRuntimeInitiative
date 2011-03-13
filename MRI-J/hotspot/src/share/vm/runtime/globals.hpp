/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GLOBALS_HPP
#define GLOBALS_HPP


#include "globalDefinitions.hpp"

#include <os/azulmmap.h>

class outputStream;
class xmlBuffer;

// string type alias used only in this file
typedef const char* ccstr;  
typedef const char* ccstrlist;   // represents string arguments which accumulate


enum FlagValueOrigin {
  DEFAULT          = 0,
  COMMAND_LINE     = 1,
  ENVIRON_VAR      = 2,
  CONFIG_FILE      = 3,
  MANAGEMENT       = 4,
  ERGONOMIC        = 5,
  ATTACH_ON_DEMAND = 6,
  INTERNAL         = 99
};

struct Flag {
  const char *type;
  const char *name;
  void*       addr;
  const char *kind;
  const char *doc;
  FlagValueOrigin origin;

  // points to all Flags static array
  static Flag *flags;

  // number of flags
  static size_t numFlags;

  static Flag* find_flag(char* name, size_t length);

  bool is_bool() const        { return strcmp(type, "bool") == 0; }
  bool get_bool() const       { return *((bool*) addr); }
  void set_bool(bool value)   { *((bool*) addr) = value; }

  bool is_intx()  const       { return strcmp(type, "intx")  == 0; }
  intx get_intx() const       { return *((intx*) addr); }
  void set_intx(intx value)   { *((intx*) addr) = value; }

  bool is_uintx() const       { return strcmp(type, "uintx") == 0; }
  uintx get_uintx() const     { return *((uintx*) addr); }
  void set_uintx(uintx value) { *((uintx*) addr) = value; }

  bool is_double() const        { return strcmp(type, "double") == 0; }
  double get_double() const     { return *((double*) addr); }
  void set_double(double value) { *((double*) addr) = value; }

  bool is_ccstr() const          { return strcmp(type, "ccstr") == 0 || strcmp(type, "ccstrlist") == 0; }
  bool ccstr_accumulates() const { return strcmp(type, "ccstrlist") == 0; }
  ccstr get_ccstr() const     { return *((ccstr*) addr); }
  void set_ccstr(ccstr value) { *((ccstr*) addr) = value; }

  bool is_unlocker() const;
  bool is_unlocked() const;
  bool is_writeable() const;
  bool is_external() const;

  void print_on(outputStream* st);
  void print_xml_on(xmlBuffer* xb);
  void print_as_flag(outputStream* st);
};

// debug flags control various aspects of the VM and are global accessible

// use FlagSetting to temporarily change some debug flag
// e.g. FlagSetting fs(DebugThisAndThat, true);   
// restored to previous value upon leaving scope
class FlagSetting {
  bool val;
  bool* flag;
 public:
  FlagSetting(bool& fl, bool newValue) { flag = &fl; val = fl; fl = newValue; }
  ~FlagSetting()                       { *flag = val; }
};


class CounterSetting {
  intx* counter;
 public:
  CounterSetting(intx* cnt) { counter = cnt; (*counter)++; }
  ~CounterSetting()         { (*counter)--; }
};


class IntFlagSetting {
  intx val;
  intx* flag;
 public:
  IntFlagSetting(intx& fl, intx newValue) { flag = &fl; val = fl; fl = newValue; }
  ~IntFlagSetting()                       { *flag = val; }
};


class DoubleFlagSetting {
  double val;
  double* flag;
 public:
  DoubleFlagSetting(double& fl, double newValue) { flag = &fl; val = fl; fl = newValue; }
  ~DoubleFlagSetting()                           { *flag = val; }
};


class CommandLineFlags {
 public:
  static bool boolAt(char* name, size_t len, bool* value);
  static bool boolAt(char* name, bool* value)      { return boolAt(name, strlen(name), value); }
  static bool boolAtPut(char* name, size_t len, bool* value, FlagValueOrigin origin);
  static bool boolAtPut(char* name, bool* value, FlagValueOrigin origin)   { return boolAtPut(name, strlen(name), value, origin); }

  static bool intxAt(char* name, size_t len, intx* value);
  static bool intxAt(char* name, intx* value)      { return intxAt(name, strlen(name), value); }
  static bool intxAtPut(char* name, size_t len, intx* value, FlagValueOrigin origin);
  static bool intxAtPut(char* name, intx* value, FlagValueOrigin origin)   { return intxAtPut(name, strlen(name), value, origin); }

  static bool uintxAt(char* name, size_t len, uintx* value);
  static bool uintxAt(char* name, uintx* value)    { return uintxAt(name, strlen(name), value); }
  static bool uintxAtPut(char* name, size_t len, uintx* value, FlagValueOrigin origin);
  static bool uintxAtPut(char* name, uintx* value, FlagValueOrigin origin) { return uintxAtPut(name, strlen(name), value, origin); }

  static bool doubleAt(char* name, size_t len, double* value);
  static bool doubleAt(char* name, double* value)    { return doubleAt(name, strlen(name), value); }
  static bool doubleAtPut(char* name, size_t len, double* value, FlagValueOrigin origin);
  static bool doubleAtPut(char* name, double* value, FlagValueOrigin origin) { return doubleAtPut(name, strlen(name), value, origin); }

  static bool ccstrAt(char* name, size_t len, ccstr* value);
  static bool ccstrAt(char* name, ccstr* value)    { return ccstrAt(name, strlen(name), value); }
  static bool ccstrAtPut(char* name, size_t len, ccstr* value, FlagValueOrigin origin);
  static bool ccstrAtPut(char* name, ccstr* value, FlagValueOrigin origin) { return ccstrAtPut(name, strlen(name), value, origin); }

  // Returns false if name is not a command line flag.
  static bool wasSetOnCmdline(const char* name, bool* value);
  static void printSetFlags();
  static void print_on(outputStream* st);
  static void print_xml_on(xmlBuffer* xb);

  static void printFlags() PRODUCT_RETURN;
  
  static void verify() PRODUCT_RETURN;
};

// use this for flags that are true by default in the debug version but
// false in the optimized version, and vice versa
#ifdef ASSERT
#define trueInDebug  true
#define falseInDebug false
#else
#define trueInDebug  false
#define falseInDebug true
#endif

// use this for flags that are true per default in the product build
// but false in development builds, and vice versa
#ifdef PRODUCT
#define trueInProduct  true
#define falseInProduct false
#else
#define trueInProduct  false
#define falseInProduct true
#endif

// develop flags are settable / visible only during development and are constant in the PRODUCT version
// product flags are always settable / visible
// notproduct flags are settable / visible only during development and are not declared in the PRODUCT version

// A flag must be declared with one of the following types:
// bool, intx, uintx, ccstr.
// The type "ccstr" is an alias for "const char*" and is used
// only in this file, because the macrology requires single-token type names.

// Note: Diagnostic options not meant for VM tuning or for product modes.
// They are to be used for VM quality assurance or field diagnosis
// of VM bugs.  They are hidden so that users will not be encouraged to
// try them as if they were VM ordinary execution options.  However, they
// are available in the product version of the VM.  Under instruction
// from support engineers, VM customers can turn them on to collect
// diagnostic information about VM problems.  To use a VM diagnostic
// option, you must first specify +UnlockDiagnosticVMOptions.
// (This master switch also affects the behavior of -Xprintflags.)

// manageable flags are writeable external product flags. 
//    They are dynamically writeable through the JDK management interface 
//    (com.sun.management.HotSpotDiagnosticMXBean API) and also through JConsole. 
//    These flags are external exported interface (see CCC).  The list of 
//    manageable flags can be queried programmatically through the management
//    interface.
//
//    A flag can be made as "manageable" only if 
//    - the flag is defined in a CCC as an external exported interface.
//    - the VM implementation supports dynamic setting of the flag.
//      This implies that the VM must *always* query the flag variable
//      and not reuse state related to the flag state at any given time.
//    - you want the flag to be queried programmatically by the customers.
// 
// product_rw flags are writeable internal product flags.
//    They are like "manageable" flags but for internal/private use.
//    The list of product_rw flags are internal/private flags which
//    may be changed/removed in a future release.  It can be set 
//    through the management interface to get/set value
//    when the name of flag is supplied.
// 
//    A flag can be made as "product_rw" only if 
//    - the VM implementation supports dynamic setting of the flag.
//      This implies that the VM must *always* query the flag variable
//      and not reuse state related to the flag state at any given time.
//
// Note that when there is a need to support develop flags to be writeable,
// it can be done in the same way as product_rw.

#define RUNTIME_FLAGS(develop, develop_pd, product, product_pd, diagnostic, notproduct, manageable, product_rw) \
                                                                            \
  product(bool, PrintCommandLineFlags, false,                               \
          "Prints flags that appeared on the command line")                 \
                                                                            \
diagnostic(bool,UnlockDiagnosticVMOptions,true,\
          "Enable processing of flags relating to field diagnostics")       \
                                                                            \
product(bool,ForceCoreDumpInAbort,false,\
"Force core dump in abort overriding politeness code")\
                                                                            \
  product(bool, JavaMonitorsInStackTrace, true,                             \
          "Print info. about Java monitor locks when the stacks are dumped")\
                                                                            \
product(bool,MultiMapMetaData,true,\
"multimap virtual memory such that all metadata bits are valid")\
                                                                            \
product(bool,KIDInRef,false,\
"Put KIDs in Refs")\
                                                                            \
develop_pd(bool,UseLargePages,\
          "Use large page memory")                                          \
                                                                            \
  product(bool, UseNUMA, false,                                             \
          "Use NUMA if available")                                          \
                                                                            \
  product(intx, NUMAChunkResizeWeight, 20,                                  \
          "Percentage (0-100) used to weight the current sample when "      \
          "computing exponentially decaying average for "                   \
          "AdaptiveNUMAChunkSizing")                                        \
                                                                            \
  product(intx, NUMASpaceResizeRate, 1*G,                                   \
          "Do not reallocate more that this amount per collection")         \
                                                                            \
  product(bool, UseAdaptiveNUMAChunkSizing, true,                           \
          "Enable adaptive chunk sizing for NUMA")                          \
                                                                            \
  product(bool, NUMAStats, false,                                           \
          "Print NUMA stats in detailed heap information")                  \
                                                                            \
  product(intx, NUMAPageScanRate, 256,                                      \
          "Maximum number of pages to include in the page scan procedure")  \
                                                                            \
  product(uintx, LargePageSizeInBytes, 0,                                   \
          "Large page size (0 to let VM choose the page size")              \
                                               				    \
  product(uintx, LargePageHeapSizeThreshold, 128*M,                         \
          "Use large pages if max heap is at least this big")		    \
                                               				    \
  product(bool, ForceTimeHighResolution, false,                             \
          "Using high time resolution(For Win32 only)")                     \
                                                                            \
  product(bool, CacheTimeMillis, false,                                     \
          "Cache os::javaTimeMillis with CacheTimeMillisGranularity")       \
                                                                            \
  diagnostic(uintx, CacheTimeMillisGranularity, 50,                         \
          "Granularity for CacheTimeMillis")                                \
                                                                            \
  develop(bool, TraceItables, false,                                        \
          "Trace initialization and use of itables")                        \
                                                                            \
  develop(bool, TraceRelocator, false,                                      \
          "Trace the bytecode relocator")                                   \
                                                                            \
  develop(bool, TraceLongCompiles, false,                                   \
          "Print out every time compilation is longer than "                \
          "a given threashold")                                             \
                                                                            \
  develop(bool, SafepointALot, false,                                       \
          "Generates a lot of safepoints. Works with "                      \
          "GuaranteedSafepointInterval")                                    \
                                                                            \
product(bool,UseC1,true,\
"use C1 compilation")\
                                                                            \
  /* TODO:  set UseC2 to true, when C2 is ready */                          \
product(bool,UseC2,false,\
"use C2 compilation")\
                                                                            \
product(bool,C1BackgroundCompilation,true,\
"Run compiles in the background")\
                                                                            \
product(bool,C2BackgroundCompilation,true,\
"Run compiles in the background")\
                                                                            \
  product(bool, PrintVMQWaitTime, false,                                    \
          "Prints out the waiting time in VM operation queue")              \
                                                                            \
  develop(bool, NoYieldsInMicrolock, false,                                 \
          "Disable yields in microlock")                                    \
                                                                            \
  develop(bool, VerifyStack, false,                                         \
          "Verify stack of each thread when it is entering a runtime call") \
                                                                            \
  notproduct(bool, StressDerivedPointers, false,                            \
          "Force scavenge when a derived pointers is detected on stack "    \
          "after rtm call")                                                 \
                                                                            \
  develop(bool, TraceDerivedPointers, false,                                \
          "Trace traversal of derived pointers on stack")                   \
                                                                            \
  product(bool, PrintJNIResolving, false,                                   \
"Used to implement \"-verbose:jni\" (resolving native methods)")\
                                                                            \
product(bool,PrintJNILoading,false,\
"Used to implement \"-verbose:jni\" (loading libraries)")\
                                                                            \
  notproduct(bool, PrintRewrites, false,                                    \
          "Print methods that are being rewritten")                         \
                                                                            \
  develop(bool, InlineArrayCopy, true,                                     \
          "inline arraycopy native that is known to be part of "            \
          "base library DLL")                                               \
                                                                            \
  develop(bool, InlineObjectHash, true,                                     \
          "inline Object::hashCode() native that is known to be part "      \
          "of base library DLL")                                            \
                                                                            \
  develop(bool, InlineObjectCopy, true,                                     \
          "inline Object.clone and Arrays.copyOf[Range] intrinsics")        \
                                                                            \
  develop(bool, InlineNatives, true,                                        \
          "inline natives that are known to be part of base library DLL")   \
                                                                            \
  develop(bool, InlineMathNatives, true,                                    \
          "inline SinD, CosD, etc.")                                        \
                                                                            \
  develop(bool, InlineClassNatives, true,                                   \
          "inline Class.isInstance, etc")                                   \
                                                                            \
  develop(bool, InlineAtomicLong, true,                                     \
          "inline sun.misc.AtomicLong")                                     \
                                                                            \
  develop(bool, InlineThreadNatives, true,                                  \
          "inline Thread.currentThread, etc")                               \
                                                                            \
  develop(bool, InlineReflectionGetCallerClass, true,                       \
          "inline sun.reflect.Reflection.getCallerClass(), known to be part "\
          "of base library DLL")                                            \
                                                                            \
  develop(bool, InlineUnsafeOps, true,                                      \
          "inline memory ops (native methods) from sun.misc.Unsafe")        \
                                                                            \
develop(bool,InlineConcurrentGetReferent,true,\
"inline java.lang.ref.Reference.getReferent")\
                                                                            \
  develop(bool, ConvertCmpD2CmpF, true,                                     \
          "Convert cmpD to cmpF when one input is constant in float range") \
                                                                            \
  develop(bool, ConvertFloat2IntClipping, true,                             \
          "Convert float2int clipping idiom to integer clipping")           \
                                                                            \
  develop(bool, SpecialStringCompareTo, true,                               \
          "special version of string compareTo")                            \
                                                                            \
  develop(bool, SpecialStringIndexOf, true,                                 \
          "special version of string indexOf")                              \
                                                                            \
develop(bool,SpecialStringEquals,true,\
"special version of string equals")\
                                                                            \
  develop(bool, TraceCallFixup, false,                                      \
          "traces all call fixups")                                         \
                                                                            \
  develop(bool, DeoptimizeALot, false,                                      \
          "deoptimize at every exit from the runtime system")               \
                                                                            \
  develop(ccstrlist, DeoptimizeOnlyAt, "",                                  \
          "a comma separated list of bcis to deoptimize at")                \
                                                                            \
  product(bool, DeoptimizeRandom, false,                                    \
          "deoptimize random frames on random exit from the runtime system")\
                                                                            \
  notproduct(bool, WalkStackALot, false,                                    \
          "trace stack (no print) at every exit from the runtime system")   \
                                                                            \
  develop(bool, Debugging, false,                                           \
          "set when executing debug methods in debug.ccp "                  \
          "(to prevent triggering assertions)")                             \
                                                                            \
  notproduct(bool, StrictSafepointChecks, trueInDebug,                      \
          "Enable strict checks that safepoints cannot happen for threads " \
          "that used No_Safepoint_Verifier")                                \
                                                                            \
  notproduct(bool, VerifyLastFrame, false,                                  \
          "Verify oops on last frame on entry to VM")                       \
                                                                            \
  develop(bool, TraceHandleAllocation, false,                               \
          "Prints out warnings when suspicious many handles are allocated") \
                                                                            \
  product(bool, UseSplitVerifier, true,                                     \
          "use split verifier with StackMapTable attributes")               \
                                                                            \
  product(bool, FailOverToOldVerifier, true,                                \
          "fail over to old verifier when split verifier fails")            \
                                                                            \
  develop(bool, ShowSafepointMsgs, false,                                   \
          "Show msg. about safepoint synch.")                               \
                                                                            \
develop(bool,SafepointTimeout,false,\
          "Time out and warn or fail after SafepointTimeoutDelay "          \
          "milliseconds if failed to reach safepoint")                      \
                                                                            \
product(bool,DieOnSafepointTimeout,true,\
          "Die upon failure to reach safepoint (see SafepointTimeout)")     \
                                                                            \
  /* 50 retries * (5 * current_retry_count) millis = ~6.375 seconds */      \
  /* typically, at most a few retries are needed */                         \
  product(intx, SuspendRetryCount, 50,                                      \
          "Maximum retry count for an external suspend request")            \
                                                                            \
  product(intx, SuspendRetryDelay, 5,                                       \
          "Milliseconds to delay per retry (* current_retry_count)")        \
                                                                            \
product(bool,UseSuspendResumeThreadLists,true,\
"Enable SuspendThreadList and ResumeThreadList")\
                                                                            \
  product(bool, AssertOnSuspendWaitFailure, false,                          \
          "Assert/Guarantee on external suspend wait failure")              \
                                                                            \
  product(bool, TraceSuspendWaitFailures, false,                            \
          "Trace external suspend wait failures")                           \
                                                                            \
  product(bool, MaxFDLimit, true,                                           \
          "Bump the number of file descriptors to max in solaris.")         \
                                                                            \
  product(bool, BytecodeVerificationRemote, true,                           \
          "Enables the Java bytecode verifier for remote classes")          \
                                                                            \
  product(bool, BytecodeVerificationLocal, false,                           \
          "Enables the Java bytecode verifier for local classes")           \
                                                                            \
  develop(bool, ForceFloatExceptions, trueInDebug,                          \
          "Force exceptions on FP stack under/overflow")                    \
                                                                            \
  develop(bool, SoftMatchFailure, trueInProduct,                            \
          "If the DFA fails to match a node, print a message and bail out") \
                                                                            \
  develop(bool, TraceJavaAssertions, false,                                 \
          "Trace java language assertions")                                 \
                                                                            \
  notproduct(bool, CheckAssertionStatusDirectives, false,                   \
          "temporary - see javaClasses.cpp")                                \
                                                                            \
  notproduct(bool, PrintMallocFree, false,                                  \
          "Trace calls to C heap malloc/free allocation")                   \
                                                                            \
  notproduct(bool, PrintOopAddress, false,                                  \
          "Always print the location of the oop")                           \
                                                                            \
  develop(bool, UseMallocOnly, false,                                       \
          "use only malloc/free for allocation (no resource area/arena)")   \
                                                                            \
  develop(bool, PrintMalloc, false,                                         \
          "print all malloc/free calls")                                    \
                                                                            \
  develop(bool, ZapResourceArea, trueInDebug,                               \
          "Zap freed resource/arena space with 0xABABABAB")                 \
                                                                            \
  notproduct(bool, ZapVMHandleArea, trueInDebug,                            \
          "Zap freed VM handle space with 0xBCBCBCBC")                      \
                                                                            \
  develop(bool, ZapJNIHandleArea, trueInDebug,                              \
          "Zap freed JNI handle space with 0xFEFEFEFE")                     \
                                                                            \
  develop(bool, ZapUnusedHeapArea, trueInDebug,                             \
          "Zap unused heap space with 0xBAADBABE")                          \
                                                                            \
  develop(bool, PrintVMMessages, true,                                      \
          "Print vm messages on console")                                   \
									    \
  product(bool, PrintGCApplicationConcurrentTime, false,		    \
	  "Print the time the application has been running") 		    \
									    \
  product(bool, PrintGCApplicationStoppedTime, false,			    \
	  "Print the time the application has been stopped") 		    \
                                                                            \
  develop(bool, Verbose, false,                                             \
          "Prints additional debugging information from other modes")       \
                                                                            \
  develop(bool, PrintMiscellaneous, false,                                  \
          "Prints uncategorized debugging information (requires +Verbose)") \
                                                                            \
product(intx,ProfileLockContentionDepth,5,\
"Collect contended lock performance statistics stacks")\
                                                                            \
product(bool,PrintLockContentionAtExit,false,\
"Print contended lock statistics at exit ")\
                                                                            \
product(bool,ShowMessageBoxOnError,true,\
          "Keep process alive on VM fatal error")                           \
                                                                            \
  product_pd(bool, UseOSErrorReporting,                                     \
          "Let VM fatal error propagate to the OS (ie. WER on Windows)")    \
                                                                            \
  product(bool, SuppressFatalErrorMessage, false,                           \
          "Do NO Fatal Error report [Avoid deadlock]")                      \
                                                                            \
  product(ccstrlist, OnError, "",                                           \
          "Run user-defined commands on fatal error; see VMError.cpp "      \
          "for examples")                                                   \
									    \
  product(ccstrlist, OnOutOfMemoryError, "",                                \
          "Run user-defined commands on first java.lang.OutOfMemoryError")  \
                                                                            \
  manageable(bool, HeapDumpOnOutOfMemoryError, false,                       \
          "Dump heap to file when java.lang.OutOfMemoryError is thrown")    \
                                                                            \
  manageable(ccstr, HeapDumpPath, NULL,                                     \
          "When HeapDumpOnOutOfMemoryError is on, the path (filename or"    \
          "directory) of the dump file (defaults to java_pid<pid>.hprof"    \
          "in the working directory)")                                      \
                                                                            \
product(bool,HeapDumpOnCtrlBreak,false,\
"Dump heap to file in Ctrl-Break handler")\
                                                                            \
  develop(uintx, SegmentedHeapDumpThreshold, 2*G,                           \
          "Generate a segmented heap dump (JAVA PROFILE 1.0.2 format) "     \
          "when the heap usage is larger than this")                        \
                                                                            \
  develop(uintx, HeapDumpSegmentSize, 1*G,                                  \
          "Approximate segment size when generating a segmented heap dump") \
                                                                            \
  develop(bool, BreakAtWarning, false,                                      \
          "Execute breakpoint upon encountering VM warning")                \
                                                                            \
develop(bool,SkipUntested,false,\
"Don't stop when you hit an untested")\
                                                                            \
  develop(bool, TraceVMOperation, false,                                    \
          "Trace vm operations")                                            \
                                                                            \
  develop(bool, UseFakeTimers, false,                                       \
          "Tells whether the VM should use system time or a fake timer")    \
                                                                            \
  diagnostic(bool, LogCompilation, false,                                   \
          "Log compilation activity in detail to hotspot.log or LogFile")   \
                                                                            \
  product(bool, PrintCompilation, false,                                    \
          "Print compilations")                                             \
                                                                            \
product(bool,PrintPromotion,false,\
"Print promotions")\
                                                                            \
product(bool,PrintAssembly,false,\
          "Print assembly code")                                            \
                                                                            \
develop(bool,PrintMethodCodes,false,\
"Print assembly code for methodCodes when generated")\
                                                                            \
develop(bool,PrintNativeMethodCodes,false,\
"Print assembly code for native methodCodes when generated")\
                                                                            \
  develop(bool, PrintDebugInfo, false,                                      \
"Print debug information for all methodCodes when generated")\
                                                                            \
  develop(bool, PrintExceptionHandlers, false,                              \
"Print exception handler tables for all methodCodes when generated")\
                                                                            \
  develop(bool, InterceptOSException, false,                                \
          "Starts debugger when an implicit OS (e.g., NULL) "               \
          "exception happens")                                              \
                                                                            \
develop(bool,PrintStatistics,false,\
"Print runtime statistics")\
                                                                            \
  develop(bool, PrintStubCode, false,                                       \
          "Print generated stub code")                                      \
                                                                            \
  product(bool, StackTraceInThrowable, true,                                \
          "Collect backtrace in throwable when exception happens")          \
                                                                            \
develop(bool,ProfileArrayCopy,false,\
"Collect length and alignment info for arraycopy")\
                                                                            \
  product(bool, OmitStackTraceInFastThrow, true,                            \
          "Omit backtraces for some 'hot' exceptions in optimized code")    \
                                                                            \
  product(bool, ProfilerPrintByteCodeStatistics, false,                     \
          "Prints byte code statictics when dumping profiler output")       \
                                                                            \
  product(bool, ProfilerRecordPC, false,                                    \
          "Collects tick for each 16 byte interval of compiled code")       \
                                                                            \
  product(bool, ProfileVM,  false,                                          \
          "Profiles ticks that fall within VM (either in the VM Thread "    \
          "or VM code called through stubs)")                               \
                                                                            \
  product(bool, ProfileIntervals, false,                                    \
          "Prints profiles for each interval (see ProfileIntervalsTicks)")  \
                                                                            \
  notproduct(bool, ProfilerCheckIntervals, false,                           \
          "Collect and print info on spacing of profiler ticks")            \
                                                                            \
  develop(bool, PrintJVMWarnings, false,                                    \
          "Prints warnings for unimplemented JVM functions")                \
                                                                            \
  notproduct(uintx, WarnOnStalledSpinLock, 0,                               \
          "Prints warnings for stalled SpinLocks")                          \
                                                                            \
  develop(bool, InitializeJavaLangSystem, true,                             \
          "Initialize java.lang.System - turn off for individual "          \
          "method debugging")                                               \
                                                                            \
  develop(bool, InitializeJavaLangString, true,                             \
          "Initialize java.lang.String - turn off for individual "          \
          "method debugging")                                               \
                                                                            \
  develop(bool, InitializeJavaLangExceptionsErrors, true,                   \
          "Initialize various error and exception classes - turn off for "  \
          "individual method debugging")                                    \
                                                                            \
  product(bool, RegisterFinalizersAtInit, true,                             \
          "Register finalizable objects at end of Object.<init> or "        \
          "after allocation.")                                              \
                                                                            \
  develop(bool, RegisterReferences, true,                                   \
          "Tells whether the VM should register soft/weak/final/phantom "   \
          "references")                                                     \
                                                                            \
  develop(bool, IgnoreRewrites, false,                                      \
          "Supress rewrites of bytecodes in the oopmap generator. "         \
          "This is unsafe!")                                                \
                                                                            \
  develop(bool, UsePrivilegedStack, true,                                   \
          "Enable the security JVM functions")                              \
                                                                            \
  develop(bool, IEEEPrecision, true,                                        \
          "Enables IEEE precision (for INTEL only)")                        \
                                                                            \
  develop(bool, ProtectionDomainVerification, true,                         \
          "Verifies protection domain before resolution in system "         \
          "dictionary")                                                     \
                                                                            \
  product(bool, ClassUnloading, true,                                       \
          "Do unloading of classes")                                        \
                                                                            \
  develop(bool, DisableStartThread, false,                                  \
          "Disable starting of additional Java threads "                    \
          "(for debugging only)")                                           \
                                                                            \
  develop(bool, MemProfiling, false,                                        \
          "Write memory usage profiling to log file")                       \
                                                                            \
  notproduct(bool, PrintSystemDictionaryAtExit, false,                      \
          "Prints the system dictionary at exit")                           \
                                                                            \
  diagnostic(bool, UnsyncloadClass, false,                                  \
          "Unstable: VM calls loadClass unsynchronized. Custom classloader "\
          "must call VM synchronized for findClass & defineClass")          \
                                                                            \
  product_pd(bool, DontYieldALot,                                           \
          "Throw away obvious excess yield calls (for SOLARIS only)")       \
                                                                            \
  product(bool, UseBoundThreads, true,                                      \
          "Bind user level threads to kernel threads (for SOLARIS only)")   \
                                                                            \
  develop(bool, UseDetachedThreads, true,                                   \
          "Use detached threads that are recycled upon termination "        \
          "(for SOLARIS only)")                                             \
                                                                            \
  product(bool, UseLWPSynchronization, true,                                \
          "Use LWP-based instead of libthread-based synchronization "       \
          "(SPARC only)")                                                   \
                                                                            \
  product(ccstr, SyncKnobs, "",                                             \
          "(Unstable) Various monitor synchronization tunables")            \
                                                                            \
  product(intx, EmitSync, 0,                                                \
          "(Unsafe,Unstable) "                                              \
          " Controls emission of inline sync fast-path code")               \
                                                                            \
  product(intx, AlwaysInflate, 0, "(Unstable) Force inflation")             \
                                                                            \
  product(intx, Atomics, 0,                                                 \
          "(Unsafe,Unstable) Diagnostic - Controls emission of atomics")    \
                                                                            \
  product(intx, FenceInstruction, 0,                                        \
          "(Unsafe,Unstable) Experimental")                                 \
                                                                            \
  product(intx, SyncFlags, 0, "(Unsafe,Unstable) Experimental Sync flags" ) \
                                                                            \
  product(intx, SyncVerbose, 0, "(Unstable)" )                              \
                                                                            \
  product(intx, ClearFPUAtPark, 0, "(Unsafe,Unstable)" )                    \
                                                                            \
  product(intx, hashCode, 0,                                                \
         "(Unstable) select hashCode generation algorithm" )                \
                                                                            \
  product(intx, WorkAroundNPTLTimedWaitHang, 1,                             \
         "(Unstable, Linux-specific)"                                       \
         " avoid NPTL-FUTEX hang pthread_cond_timedwait" )                  \
                                                                            \
product(bool,UseLinuxPosixThreadCPUClocks,false,\
"Use NPTL pthread_getcpuclockid to pass to clock_gettime ")\
                                                                            \
  product(bool, FilterSpuriousWakeups , true,                               \
	  "Prevent spurious or premature wakeups from object.wait"          \
	  "(Solaris only)")                                                 \
                                                                            \
  develop(bool, UsePthreads, false,                                         \
          "Use pthread-based instead of libthread-based synchronization "   \
          "(SPARC only)")                                                   \
                                                                            \
  product(bool, AdjustConcurrency, false,                                   \
          "call thr_setconcurrency at thread create time to avoid "         \
          "LWP starvation on MP systems (For Solaris Only)")                \
                                                                            \
  develop(bool, UpdateHotSpotCompilerFileOnError, true,                     \
          "Should the system attempt to update the compiler file when "     \
          "an error occurs?")                                               \
                                                                            \
  product(bool, ReduceSignalUsage, false,                                   \
          "Reduce the use of OS signals in Java and/or the VM")             \
                                                                            \
  notproduct(bool, ValidateMarkSweep, false,                                \
          "Do extra validation during MarkSweep collection")                \
                                                                            \
  notproduct(bool, RecordMarkSweepCompaction, false,                        \
          "Enable GC-to-GC recording and querying of compaction during "    \
          "MarkSweep")							    \
                                                                            \
  develop(bool, LoadLineNumberTables, true,                                 \
          "Tells whether the class file parser loads line number tables")   \
                                                                            \
  develop(bool, LoadLocalVariableTables, true,                              \
          "Tells whether the class file parser loads local variable tables")\
                                                                            \
  develop(bool, LoadLocalVariableTypeTables, true,                          \
          "Tells whether the class file parser loads local variable type tables")\
                                                                            \
  product(bool, AllowUserSignalHandlers, false,                             \
          "Do not complain if the application installs signal handlers "    \
          "(Solaris & Linux only)")                                         \
                                                                            \
  product(bool, UseSignalChaining, true,                                    \
          "Use signal-chaining to invoke signal handlers installed "        \
          "by the application (Solaris & Linux only)")                      \
                                                                            \
  product(bool, UseAltSigs, false,                                          \
          "Use alternate signals instead of SIGUSR1 & SIGUSR2 for VM "      \
          "internal signals. (Solaris only)")                               \
                                                                            \
product(bool,UseSpinning,true,\
          "Use spinning in monitor inflation and before entry")             \
                                                                            \
  product(bool, PreSpinYield, false,                                        \
          "Yield before inner spinning loop")                               \
                                                                            \
  product(bool, PostSpinYield, true,                                        \
          "Yield after inner spinning loop")                                \
                                                                            \
product(uintx,ObjectMonitorDeflationThreshold,2*K,\
"Number of active ObjectMonitors before considering forced deflation")\
                                                                            \
product(uintx,ObjectMonitorDeflationInterval,5000,\
"Number of ms between considering forced deflation")\
                                                                            \
product(bool,EnableDeflations,true,\
"Enable deflations at safepoint")\
                                                                            \
product(bool,EnableAggressiveDeflations,true,\
"Enable periodic deflations through forced safepoints")\
                                                                            \
  product(bool, JNIDetachReleasesMonitors, true,                            \
          "JNI DetachCurrentThread releases monitors owned by thread")      \
                                                                            \
  product(bool, RestoreMXCSROnJNICalls, false,                              \
          "Restore MXCSR when returning from JNI calls")                    \
                                                                            \
  product(bool, CheckJNICalls, false,                                       \
          "Verify all arguments to JNI calls")                              \
                                                                            \
product(bool,UseFastJNIAccessors,false,\
"Use optimized versions of Get<Primitive>Field, not compatible with read barriers")\
                                                                            \
  product(bool, EagerXrunInit, false,                                       \
          "Eagerly initialize -Xrun libraries; allows startup profiling, "  \
          " but not all -Xrun libraries may support the state of the VM at this time") \
                                                                            \
  product(bool, PreserveAllAnnotations, false,                              \
          "Preserve RuntimeInvisibleAnnotations as well as RuntimeVisibleAnnotations") \
                                                                            \
  develop(uintx, PreallocatedOutOfMemoryErrorCount, 4,                      \
          "Number of OutOfMemoryErrors preallocated with backtrace")        \
                                                                            \
  product(bool, LazyBootClassLoader, true,                                  \
          "Enable/disable lazy opening of boot class path entries")         \
                                                                            \
  product(intx, FieldsAllocationStyle, 1,                                   \
          "0 - type based with oops first, 1 - with oops last")             \
                                                                            \
  product(bool, CompactFields, true,                                        \
          "Allocate nonstatic fields in gaps between previous fields")      \
                                                                            \
  notproduct(bool, PrintCompactFieldsSavings, false,                        \
          "Print how many words were saved with CompactFields")             \
                                                                            \
                                                                            \
product(bool,PrintPauses,false,\
"Trace safepoint pauses")\
                                                                            \
  notproduct(bool, TraceRuntimeCalls, false,                                \
          "Trace run-time calls")                                           \
                                                                            \
  develop(bool, TraceJNICalls, false,                                       \
          "Trace JNI calls")                                                \
                                                                            \
  notproduct(bool, TraceJVMCalls, false,                                    \
          "Trace JVM calls")                                                \
                                                                            \
  product(ccstr, TraceJVMTI, "",                                            \
          "Trace flags for JVMTI functions and events")                     \
                                                                            \
  /* This option can change an EMCP method into an obsolete method. */      \
  /* This can affect tests that except specific methods to be EMCP. */      \
  /* This option should be used with caution. */                            \
  product(bool, StressLdcRewrite, false,                                    \
          "Force ldc -> ldc_w rewrite during RedefineClasses")              \
                                                                            \
  product(intx, TraceRedefineClasses, 0,                                    \
          "Trace level for JVMTI RedefineClasses")                          \
                                                                            \
  /* change to false by default sometime after Mustang */                   \
  product(bool, VerifyMergedCPBytecodes, true,                              \
          "Verify bytecodes after RedefineClasses constant pool merging")   \
                                                                            \
  develop(bool, TraceJNIHandleAllocation, false,                            \
          "Trace allocation/deallocation of JNI handle blocks")             \
                                                                            \
  develop(bool, TraceThreadEvents, false,                                   \
          "Trace all thread events")                                        \
                                                                            \
  develop(bool, TraceBytecodes, false,                                      \
          "Trace bytecode execution")                                       \
                                                                            \
  develop(bool, TraceClassInitialization, false,                            \
          "Trace class initialization")                                     \
                                                                            \
  develop(bool, TraceExceptions, false,                                     \
          "Trace exceptions")                                               \
                                                                            \
  develop(bool, TraceICs, false,                                            \
          "Trace inline cache changes")                                     \
                                                                            \
  notproduct(bool, TraceInvocationCounterOverflow, false,                   \
          "Trace method invocation counter overflow")                       \
                                                                            \
  develop(bool, TraceNewOopMapGeneration, false,                            \
          "Trace OopMapGeneration")                                         \
                                                                            \
  develop(bool, TraceNewOopMapGenerationDetailed, false,                    \
          "Trace OopMapGeneration: print detailed cell states")             \
                                                                            \
  develop(bool, TimeOopMap, false,                                          \
          "Time calls to GenerateOopMap::compute_map() in sum")             \
                                                                            \
  develop(bool, TimeOopMap2, false,                                         \
          "Time calls to GenerateOopMap::compute_map() individually")       \
                                                                            \
  develop(bool, TraceMonitorMismatch, false,                                \
          "Trace monitor matching failures during OopMapGeneration")        \
                                                                            \
  develop(bool, TraceOopMapRewrites, false,                                 \
          "Trace rewritting of method oops during oop map generation")      \
                                                                            \
  develop(bool, TraceSafepoint, false,                                      \
          "Trace safepoint operations")                                     \
                                                                            \
  develop(bool, TraceICBuffer, false,                                       \
          "Trace usage of IC buffer")                                       \
                                                                            \
  develop(bool, TraceCompiledIC, false,                                     \
          "Trace changes of compiled IC")                                   \
                                                                            \
  develop(bool, TraceStartupTime, false,                                    \
          "Trace setup time")                                               \
                                                                            \
  develop(bool, TraceHPI, false,                                            \
          "Trace Host Porting Interface (HPI)")                             \
                                                                            \
  develop(bool, TraceProtectionDomainVerification, false,                   \
          "Trace protection domain verifcation")                            \
                                                                            \
  product(bool, TraceMonitorInflation, false,                               \
          "Trace monitor inflation in JVM")                                 \
                                                                            \
  develop(bool, TraceClearedExceptions, false,                              \
          "Prints when an exception is forcibly cleared")                   \
                                                                            \
  product(bool, TraceClassResolution, false,                                \
          "Trace all constant pool resolutions (for debugging)")            \
                                                                            \
  /* gc */                                                                  \
                                                                            \
  product(bool, UseSerialGC, false,                                         \
          "Tells whether the VM should use serial garbage collector")       \
                                                                            \
product(bool,MProtectHeapAtSafepoint,false,\
"Mprotect the heap at safepoint start to catch bad writes")\
                                                                            \
product(uintx,MProtectHeapAtSafepointDelayMS,250,\
"Number of ms to wait after mprotecting the heap")\
                                                                            \
  product(bool, UseParallelGC, false,                                       \
          "Use the Parallel Scavenge garbage collector")                    \
                                                                            \
  product(bool, UseParallelOldGC, false,				    \
	  "Use the Parallel Old garbage collector")			    \
                                                                            \
  product(bool, UseParallelOldGCCompacting, true,			    \
	  "In the Parallel Old garbage collector use parallel compaction")  \
                                                                            \
  product(bool, UseParallelDensePrefixUpdate, true,			    \
	  "In the Parallel Old garbage collector use parallel dense"        \
	  " prefix update")                                                 \
                                                                            \
  develop(bool, UseParallelOldGCChunkPointerCalc, true,			    \
	  "In the Parallel Old garbage collector use chucks to calculate"   \
	  " new object locations")                                          \
                                                                            \
  product(uintx, HeapMaximumCompactionInterval, 20,                         \
          "How often should we maximally compact the heap (not allowing "   \
	  "any dead space)")                                                \
                                                                            \
  product(uintx, HeapFirstMaximumCompactionCount, 3,                        \
          "The collection count for the first maximum compaction")          \
                                                                            \
  product(bool, UseMaximumCompactionOnSystemGC, true,	                    \
	  "In the Parallel Old garbage collector maximum compaction for "   \
	  "a system GC")                                                    \
									    \
  product(uintx, ParallelOldDeadWoodLimiterMean, 50,			    \
          "The mean used by the par compact dead wood"			    \
	  "limiter (a number between 0-100).")				    \
                                                                            \
  product(uintx, ParallelOldDeadWoodLimiterStdDev, 80,			    \
	  "The standard deviation used by the par compact dead wood"	    \
	  "limiter (a number between 0-100).")				    \
									    \
  product(bool, UseParallelOldGCDensePrefix, true,                          \
          "Use a dense prefix with the Parallel Old garbage collector")     \
                                                                            \
  product(uintx, ParallelGCThreads, 0,                                      \
          "Number of parallel threads parallel gc will use")                \
                                                                            \
  develop(bool, VerifyParallelOldWithMarkSweep, false,                      \
          "Use the MarkSweep code to verify phases of Parallel Old")        \
                                                                            \
  develop(uintx, VerifyParallelOldWithMarkSweepInterval, 1,                 \
          "Interval at which the MarkSweep code is used to verify "         \
	  "phases of Parallel Old")                                         \
                                                                            \
  develop(bool, ParallelOldMTUnsafeMarkBitMap, false,                       \
          "Use the Parallel Old MT unsafe in marking the bitmap")           \
                                                                            \
  develop(bool, ParallelOldMTUnsafeUpdateLiveData, false,                   \
          "Use the Parallel Old MT unsafe in update of live size")          \
                                                                            \
  develop(bool, TraceChunkTasksQueuing, false,                              \
          "Trace the queuing of the chunk tasks")                           \
                                                                            \
  product(uintx, YoungPLABSize, 4096,                     		    \
          "Size of young gen promotion labs (in HeapWords)")                \
                                                                            \
  product(uintx, OldPLABSize, 1024,                                         \
          "Size of old gen promotion labs (in HeapWords)")                  \
                                                                            \
product(uintx,GCTaskTimeStampEntries,1000,\
          "Number of time stamp entries per gc worker thread")              \
                                                                            \
  product(bool, AlwaysTenure, false,                                        \
          "Always tenure objects in eden. (ParallelGC only)")               \
                                                                            \
  product(bool, NeverTenure, false,                                         \
          "Never tenure objects in eden, May tenure on overflow"            \
          " (ParallelGC only)")                                             \
                                                                            \
  product(bool, ScavengeBeforeFullGC, true,                                 \
          "Scavenge youngest generation before each full GC,"               \
          " used with UseParallelGC")                                       \
                                                                            \
  develop(bool, ScavengeWithObjectsInToSpace, false,			    \
          "Allow scavenges to occur when to_space contains objects.")	    \
                                                                            \
  product(bool, ParallelGCVerbose, false,                                   \
          "Verbose output for parallel GC.")                                \
                                                                            \
  product(intx, ParallelGCBufferWastePct, 10,                               \
          "wasted fraction of parallel allocation buffer.")                 \
                                                                            \
  product(bool, ParallelGCRetainPLAB, true,                                 \
          "Retain parallel allocation buffers across scavenges.")           \
                                                                            \
  product(intx, TargetPLABWastePct, 10,                                     \
          "target wasted space in last buffer as pct of overall allocation")\
                                                                            \
  product(uintx, PLABWeight, 75,				    	    \
	  "Percentage (0-100) used to weight the current sample when"	    \
	  "computing exponentially decaying average for ResizePLAB.")       \
									    \
  product(bool, ResizePLAB, true,                                           \
          "Dynamically resize (survivor space) promotion labs")             \
                                                                            \
  product(bool, PrintPLAB, false,                                           \
          "Print (survivor space) promotion labs sizing decisions")         \
                                                                            \
  product(bool, AlwaysPreTouch, false,                                      \
 	  "It forces all freshly committed pages to be pre-touched.")       \
                                                                            \
  develop(bool, VerifyBlockOffsetArray, false,				    \
          "Do (expensive!) block offset array verification")		    \
                                                                            \
  product(bool, BlockOffsetArrayUseUnallocatedBlock, trueInDebug,           \
          "Maintain _unallocated_block in BlockOffsetArray"                 \
          " (currently applicable only to CMS collector)")       	    \
                                                                            \
  product(intx, RefDiscoveryPolicy, 0,                             	    \
          "Whether reference-based(0) or referent-based(1)")	            \
                                                                            \
  product(bool, ParallelRefProcEnabled, false,                        	    \
          "Enable parallel reference processing whenever possible")         \
                                                                            \
  product(bool, ParallelRefProcBalancingEnabled, true,                      \
          "Enable balancing of reference processing queues")                \
                                                                            \
  notproduct(bool, ScavengeALot, false,                                     \
          "Force scavenge at every Nth exit from the runtime system "       \
          "(N=ScavengeALotInterval)")                                       \
                                                                            \
  develop(bool, FullGCALot, false,                                          \
          "Force full gc at every Nth exit from the runtime system "        \
          "(N=FullGCALotInterval)")                                         \
                                                                            \
  notproduct(bool, GCALotAtAllSafepoints, false,                            \
          "Enforce ScavengeALot/GCALot at all potential safepoints")        \
									    \
product(bool,HandlePromotionFailure,false,\
          "The youngest generation collection does not require"             \
          " a guarantee of full promotion of all live objects.")            \
                                                                            \
  notproduct(bool, PromotionFailureALot, false,                             \
          "Use promotion failure handling on every youngest generation "    \
          "collection")                                                     \
                                                                            \
  develop(uintx, PromotionFailureALotCount, 1000,                           \
          "Number of promotion failures occurring at ParGCAllocBuffer"      \
          "refill attempts (ParNew) or promotion attempts "		    \
	  "(other young collectors) ")                                      \
                                                                            \
  develop(uintx, PromotionFailureALotInterval, 5,                           \
          "Total collections between promotion failures alot")              \
                                                                            \
  develop(intx, WorkStealingSleepMillis, 1,                                 \
          "Sleep time when sleep is used for yields")                       \
                                                                            \
  develop(uintx, WorkStealingYieldsBeforeSleep, 1000,                       \
          "Number of yields before a sleep is done during workstealing")    \
                                                                            \
  product(uintx, PreserveMarkStackSize, 40,				    \
	   "Size for stack used in promotion failure handling")		    \
                                                                            \
  product_pd(bool, ResizeTLAB,                                              \
          "Dynamically resize tlab size for threads")                       \
                                                                            \
  product(bool, ZeroTLAB, false,                                            \
          "Zero out the newly created TLAB")                                \
                                                                            \
product(bool,ParkTLAB,true,\
"Park TLAB on CPU when thread is switched out or will block")\
                                                                            \
  product(bool, PrintTLAB, false,                                           \
          "Print various TLAB related information")                         \
                                                                            \
develop(bool,PrintTLAB2,false,\
"Print more TLAB related information for parking")\
                                                                            \
  product(bool, TLABStats, true,                                            \
          "Print various TLAB related information")                         \
                                                                            \
product(uintx,DefaultMaxRAM,G,\
	  "Maximum real memory size for setting server class heap size")    \
									    \
  product(uintx, DefaultMaxRAMFraction, 4,				    \
	  "Fraction (1/n) of real memory used for server class max heap")   \
									    \
  product(uintx, DefaultInitialRAMFraction, 64,				    \
	  "Fraction (1/n) of real memory used for server class initial heap")  \
									    \
  product(bool, UseAutoGCSelectPolicy, false,                               \
          "Use automatic collection selection policy")                      \
                                                                            \
  product(uintx, AutoGCSelectPauseMillis, 5000,                  	    \
          "Automatic GC selection pause threshhold in ms")                  \
									    \
product(bool,UseAdaptiveSizePolicy,false,\
          "Use adaptive generation sizing policies")                        \
                                                                            \
  product(bool, UsePSAdaptiveSurvivorSizePolicy, true,     		    \
          "Use adaptive survivor sizing policies")                          \
                                                                            \
  product(bool, UseAdaptiveGenerationSizePolicyAtMinorCollection, true,     \
          "Use adaptive young-old sizing policies at minor collections")    \
                                                                            \
  product(bool, UseAdaptiveGenerationSizePolicyAtMajorCollection, true,     \
          "Use adaptive young-old sizing policies at major collections")    \
                                                                            \
  product(bool, UseAdaptiveSizePolicyWithSystemGC, false,   		    \
          "Use statistics from System.GC for adaptive size policy")	    \
                                                                            \
  product(bool, UseAdaptiveGCBoundary, false,				    \
          "Allow young-old boundary to move")    			    \
                                                                            \
  develop(bool, TraceAdaptiveGCBoundary, false,				    \
          "Trace young-old boundary moves")    			            \
                                                                            \
  develop(intx, PSAdaptiveSizePolicyResizeVirtualSpaceAlot, -1,   	    \
          "Resize the virtual spaces of the young or old generations")      \
                                                                            \
  product(uintx, AdaptiveSizeThroughPutPolicy, 0,                           \
          "Policy for changeing generation size for throughput goals")	    \
                                                                            \
  product(uintx, AdaptiveSizePausePolicy, 0,                                \
          "Policy for changing generation size for pause goals")	    \
                                                                            \
  develop(bool, PSAdjustTenuredGenForMinorPause, false,			    \
	  "Adjust tenured generation to achive a minor pause goal")	    \
                                                                            \
  develop(bool, PSAdjustYoungGenForMajorPause, false,			    \
	  "Adjust young generation to achive a major pause goal")	    \
									    \
  product(uintx, AdaptiveSizePolicyInitializingSteps, 20,                   \
          "Number of steps where heuristics is used before data is used")   \
									    \
  develop(uintx, AdaptiveSizePolicyReadyThreshold, 5,                       \
          "Number of collections before the adaptive sizing is started")    \
									    \
  product(uintx, AdaptiveSizePolicyOutputInterval, 0,                       \
          "Collecton interval for printing information, zero => never")     \
                                                                            \
  product(bool, UseAdaptiveSizePolicyFootprintGoal, true,                   \
          "Use adaptive minimum footprint as a goal")			    \
                                                                            \
  product(uintx, AdaptiveSizePolicyWeight, 10,                              \
          "Weight given to exponential resizing, between 0 and 100")        \
                                                                            \
  product(uintx, AdaptiveTimeWeight,       25,                              \
          "Weight given to time in adaptive policy, between 0 and 100")     \
                                                                            \
  product(uintx, PausePadding, 1,                                           \
          "How much buffer to keep for pause time")                  	    \
                                                                            \
  product(uintx, PromotedPadding, 3,                                        \
          "How much buffer to keep for promotion failure")                  \
                                                                            \
  product(uintx, SurvivorPadding, 3,                                        \
          "How much buffer to keep for survivor overflow")                  \
                                                                            \
  product(uintx, AdaptivePermSizeWeight, 20,                                \
          "Weight for perm gen exponential resizing, between 0 and 100")    \
                                                                            \
  product(uintx, PermGenPadding, 3,                                         \
          "How much buffer to keep for perm gen sizing")                    \
                                                                            \
  product(uintx, ThresholdTolerance, 10,                                    \
          "Allowed collection cost difference between generations")         \
                                                                            \
  product(uintx, AdaptiveSizePolicyCollectionCostMargin, 50,                \
          "If collection costs are within margin, reduce both by full delta") \
                                                                            \
  product(uintx, YoungGenerationSizeIncrement, 20,                          \
          "Adaptive size percentage change in young generation")            \
                                                                            \
  product(uintx, YoungGenerationSizeSupplement, 80,                         \
          "Supplement to YoungedGenerationSizeIncrement used at startup")   \
                                                                            \
  product(uintx, YoungGenerationSizeSupplementDecay, 8,                     \
          "Decay factor to YoungedGenerationSizeSupplement")    	    \
                                                                            \
  product(uintx, TenuredGenerationSizeIncrement, 20,                        \
          "Adaptive size percentage change in tenured generation")          \
                                                                            \
  product(uintx, TenuredGenerationSizeSupplement, 80,                       \
          "Supplement to TenuredGenerationSizeIncrement used at startup")   \
                                                                            \
  product(uintx, TenuredGenerationSizeSupplementDecay, 2,                   \
          "Decay factor to TenuredGenerationSizeIncrement")  		    \
                                                                            \
  product(uintx, MaxGCPauseMillis, max_uintx,                        	    \
          "Adaptive size policy maximum GC pause time goal in msec")        \
                                                                            \
  product(uintx, MaxGCMinorPauseMillis, max_uintx,                     	    \
          "Adaptive size policy maximum GC minor pause time goal in msec")  \
                                                                            \
  product(uintx, GCTimeRatio, 99,                     	                    \
          "Adaptive size policy application time to GC time ratio")         \
                                                                            \
  product(uintx, AdaptiveSizeDecrementScaleFactor, 4,                       \
          "Adaptive size scale down factor for shrinking")		    \
                                                                            \
  product(bool, UseAdaptiveSizeDecayMajorGCCost, true,            	    \
          "Adaptive size decays the major cost for long major intervals")   \
                                                                            \
  product(uintx, AdaptiveSizeMajorGCDecayTimeScale, 10,           	    \
          "Time scale over which major costs decay")                        \
                                                                            \
  product(uintx, MinSurvivorRatio, 3,                                       \
          "Minimum ratio of young generation/survivor space size")          \
                                                                            \
  product(uintx, InitialSurvivorRatio, 8,                                   \
          "Initial ratio of eden/survivor space size")                      \
                                                                            \
  product(uintx, BaseFootPrintEstimate, 256*M,             	 	    \
          "Estimate of footprint other than Java Heap")                     \
                                                                            \
  product(bool, UseGCOverheadLimit, true,                                   \
          "Use policy to limit of proportion of time spent in GC "	    \
	  "before an OutOfMemory error is thrown")                    	    \
                                                                            \
  product(uintx, GCTimeLimit, 98,                                           \
          "Limit of proportion of time spent in GC before an OutOfMemory"   \
          "error is thrown (used with GCHeapFreeLimit)")                    \
                                                                            \
  product(uintx, GCHeapFreeLimit, 2,                                        \
          "Minimum percentage of free space after a full GC before an "     \
          "OutOfMemoryError is thrown (used with GCTimeLimit)")             \
									    \
  develop(uintx, AdaptiveSizePolicyGCTimeLimitThreshold, 5,                 \
          "Number of consecutive collections before gc time limit fires")   \
                                                                            \
  product(bool, PrintAdaptiveSizePolicy, false,                             \
          "Print information about AdaptiveSizePolicy")                     \
                                                                            \
  product(intx, PrefetchCopyIntervalInBytes, -1,                            \
          "How far ahead to prefetch destination area (<= 0 means off)")    \
                                                                            \
  product(intx, PrefetchScanIntervalInBytes, -1,                            \
          "How far ahead to prefetch scan area (<= 0 means off)")           \
                                                                            \
  product(intx, PrefetchFieldsAhead, -1,                                    \
          "How many fields ahead to prefetch in oop scan (<= 0 means off)") \
                                                                            \
  develop(bool, UsePrefetchQueue, true,                                     \
          "Use the prefetch queue during PS promotion")                     \
                                                                            \
  diagnostic(bool, VerifyBeforeExit, trueInDebug,                           \
          "Verify system before exiting")                                   \
                                                                            \
  diagnostic(bool, VerifyBeforeGC, false,                                   \
          "Verify memory system before GC")                                 \
                                                                            \
  diagnostic(bool, VerifyAfterGC, false,                                    \
          "Verify memory system after GC")                                  \
                                                                            \
  diagnostic(bool, VerifyDuringGC, false,                                   \
          "Verify memory system during GC (between phases)")                \
                                                                            \
  diagnostic(bool, VerifyRememberedSets, false,                             \
          "Verify GC remembered sets")                                      \
                                                                            \
  diagnostic(bool, VerifyObjectStartArray, true,                            \
          "Verify GC object start array if verify before/after")            \
                                                                            \
product(bool,DisableExplicitGC,true,\
          "Tells whether calling System.gc() does a full GC")               \
                                                                            \
  notproduct(bool, CheckMemoryInitialization, false,                        \
          "Checks memory initialization")                                   \
                                                                            \
  product(bool, CollectGen0First, false,                                    \
          "Collect youngest generation before each full GC")                \
                                                                            \
  product(bool, UseGCTaskAffinity, false,                                   \
          "Use worker affinity when asking for GCTasks")                    \
                                                                            \
  product(uintx, ProcessDistributionStride, 4,                              \
          "Stride through processors when distributing processes")          \
                                                                            \
  /* gc tracing */                                                          \
  manageable(bool, PrintGC, false,                                          \
          "Print message at garbage collect")                               \
                                                                            \
  manageable(bool, PrintGCDetails, false,                                   \
          "Print more details at garbage collect")                          \
                                                                            \
  manageable(bool, PrintGCDateStamps, false,                                \
          "Print date stamps at garbage collect")                           \
                                                                            \
  manageable(bool, PrintGCTimeStamps, false,                                \
          "Print timestamps at garbage collect")                            \
                                                                            \
product(uintx,PrintGCHistory,50,\
"Cycles of historical PrintGC data to save for display by ARTA")\
                                                                            \
product(uintx,GCWarningHistory,50,\
"Number of historical GC warnings to save for display by ARTA")\
                                                                            \
product(bool,ProfileAllocatedObjects,false,\
"Collect allocated object statistics")\
                                                                            \
product(bool,ProfileLiveObjects,false,\
"Collect live object statistics from GC marking")\
                                                                            \
  product(bool, PrintGCTaskTimeStamps, false,                               \
          "Print timestamps for individual gc worker thread tasks")         \
                                                                            \
  develop(intx, ConcGCYieldTimeout, 0,                                      \
          "If non-zero, assert that GC threads yield within this # of ms.") \
                                                                            \
  notproduct(bool, TraceMarkSweep, false,                                   \
          "Trace mark sweep")                                               \
                                                                            \
  product(bool, PrintReferenceGC, false,                                    \
          "Print times spent handling reference objects during GC "         \
          " (enabled only when PrintGCDetails)")                            \
                                                                            \
  develop(bool, TraceReferenceGC, false,                                    \
          "Trace handling of soft/weak/final/phantom references")           \
                                                                            \
  develop(bool, TraceFinalizerRegistration, false,                          \
         "Trace registration of final references")                          \
                                                                            \
  notproduct(bool, TraceScavenge, false,                                    \
          "Trace scavenge")                                                 \
                                                                            \
product(uintx,NegativeJARCacheSize,16,\
"Number of negative cache entries (for class loading) per JAR")\
                                                                            \
  product_rw(bool, TraceClassLoading, false,                                \
          "Trace all classes loaded")                                       \
                                                                            \
  product(bool, TraceClassLoadingPreorder, false,                           \
          "Trace all classes loaded in order referenced (not loaded)")      \
                                                                            \
  product_rw(bool, TraceClassUnloading, false,                              \
          "Trace unloading of classes")                                     \
                                                                            \
  product_rw(bool, TraceLoaderConstraints, false,                           \
          "Trace loader constraints")                                       \
                                                                            \
product(bool,ClassLoadSupersFirst,false,\
"Load super classes before child classes to close a narrow deadlock possibility")\
                                                                            \
  product(bool, TraceGen0Time, false,                                       \
          "Trace accumulated time for Gen 0 collection")                    \
                                                                            \
  product(bool, TraceGen1Time, false,                                       \
          "Trace accumulated time for Gen 1 collection")                    \
                                                                            \
  product(bool, PrintTenuringDistribution, false,                           \
          "Print tenuring age information")                                 \
                                                                            \
  product_rw(bool, PrintHeapAtGC, false,                                    \
          "Print heap layout before and after each GC")                     \
                                                                            \
  product(bool, PrintHeapAtSIGBREAK, true,                                  \
          "Print heap layout in response to SIGBREAK")                      \
									    \
  manageable(bool, PrintClassHistogram, false,			            \
	  "Print a histogram of class instances") 		            \
                                                                            \
  develop(bool, TraceWorkGang, false,                                       \
          "Trace activities of work gangs")                                 \
                                                                            \
  product(bool, TraceParallelOldGCTasks, false,                             \
          "Trace multithreaded GC activity")                                \
                                                                            \
  develop(bool, TraceBlockOffsetTable, false,                               \
          "Print BlockOffsetTable maps")                                    \
                                                                            \
  develop(bool, TraceCardTableModRefBS, false,                              \
          "Print CardTableModRefBS maps")                                   \
                                                                            \
  develop(bool, TraceGCTaskManager, false,                                  \
          "Trace actions of the GC task manager")                           \
                                                                            \
  develop(bool, TraceGCTaskQueue, false,                                    \
          "Trace actions of the GC task queues")                            \
                                                                            \
  develop(bool, TraceGCTaskThread, false,                                   \
          "Trace actions of the GC task threads")                           \
                                                                            \
  product(bool, PrintParallelOldGCPhaseTimes, false,			    \
          "Print the time taken by each parallel old gc phase."		    \
	  "PrintGCDetails must also be enabled.")			    \
                                                                            \
  develop(bool, TraceParallelOldGCMarkingPhase, false,			    \
	  "Trace parallel old gc marking phase")			    \
									    \
  develop(bool, TraceParallelOldGCSummaryPhase, false,			    \
	  "Trace parallel old gc summary phase")			    \
									    \
  develop(bool, TraceParallelOldGCCompactionPhase, false,		    \
	  "Trace parallel old gc compaction phase")			    \
                                                                            \
  develop(bool, TraceParallelOldGCDensePrefix, false,			    \
	  "Trace parallel old gc dense prefix computation")		    \
                                                                            \
  develop(bool, IgnoreLibthreadGPFault, false,                              \
          "Suppress workaround for libthread GP fault")                     \
                                                                            \
  /* JVMTI heap profiling */						    \
									    \
  diagnostic(bool, TraceJVMTIObjectTagging, false,                          \
	  "Trace JVMTI object tagging calls")				    \
									    \
  diagnostic(bool, VerifyBeforeIteration, false,                            \
          "Verify memory system before JVMTI iteration")		    \
                                                                            \
develop(intx,HeapIteratorTaskThreads,1,\
"Number of parallel threads for JVMTI heap iteration. By default"\
"do synchronous single-threaded heap callbacks, hence 1")\
                                                                            \
product(intx,HeapIterationCallbacksTimeout,3,\
"Timeout (in minutes) for JVMTI heap iteration."\
"0 disables heap iteration capabilities. -1 signifies no timeout")\
                                                                            \
product(bool,CanGenerateNativeMethodBindEvents,false,\
"control jvmti capability to generate native method bind events")\
                                                                            \
  /* compiler interface */                                                  \
                                                                            \
product(intx,CIMaxCompilerThreads,10,\
"limit compiler threads")\
                                                                            \
  develop(bool, CIPrintCompileQueue, false,                                 \
          "display the contents of the compile queue whenever a "           \
          "compilation is enqueued")                                        \
                                                                            \
  product(bool, CITime, false,                                              \
          "collect timing information for compilation")                     \
                                                                            \
  develop(bool, CITimeEach, false,                                          \
          "display timing information after each successful compilation")   \
                                                                            \
product(bool,CICompileOSR,true,\
"compile on stack replacement methods if supported by the compiler")\
                                                                            \
  develop(bool, CIPrintMethodCodes, false,                                  \
          "print method bytecodes of the compiled code")                    \
                                                                            \
  develop(bool, CIPrintTypeFlow, false,                                     \
          "print the results of ciTypeFlow analysis")                       \
                                                                            \
  develop(bool, CITraceTypeFlow, false,                                     \
          "detailed per-bytecode tracing of ciTypeFlow analysis")           \
                                                                            \
  develop(intx, CICloneLoopTestLimit, 100,                                  \
          "size limit for blocks heuristically cloned in ciTypeFlow")       \
                                                                            \
  /* compiler */                                                            \
                                                                            \
  develop(bool, Use24BitFPMode, true,                                       \
          "Set 24-bit FPU mode on a per-compile basis ")                    \
                                                                            \
  develop(bool, Use24BitFP, true,                                           \
          "use FP instructions that produce 24-bit precise results")        \
                                                                            \
  develop(bool, UseStrictFP, true,                                          \
          "use strict fp if modifier strictfp is set")                      \
                                                                            \
  develop(bool, GenerateSynchronizationCode, true,                          \
          "generate locking/unlocking code for synchronized methods and "   \
          "monitors")                                                       \
                                                                            \
  develop(bool, GenerateCompilerNullChecks, true,                           \
          "Generate explicit null checks for loads/stores/calls")           \
                                                                            \
  develop_pd(bool, ImplicitNullChecks,                                      \
          "generate code for implicit null checks")                         \
                                                                            \
  product(bool, PrintSafepointStatistics, false,                            \
          "print statistics about safepoint synchronization")               \
                                                                            \
  product(intx, PrintSafepointStatisticsCount, 300,                         \
          "total number of safepoint statistics collected "                 \
          "before printing them out")                                       \
                                                                            \
  product(intx, PrintSafepointStatisticsTimeout,  -1,                       \
          "print safepoint statistics only when safepoint takes"            \
          " more than PrintSafepointSatisticsTimeout in millis")            \
                                                                            \
  develop(bool, InlineAccessors, true,                                      \
          "inline accessor methods (get/set)")                              \
                                                                            \
  product(bool, Inline, true,                                               \
          "enable inlining")                                                \
                                                                            \
  product(bool, ClipInlining, true,                                         \
          "clip inlining if aggregate method exceeds DesiredMethodLimit")   \
                                                                            \
  develop(bool, UseCHA, true,                                               \
          "enable CHA")                                                     \
                                                                            \
  notproduct(bool, TimeCompiler, false,                                     \
          "time the compiler")                                              \
                                                                            \
  notproduct(bool, TimeCompiler2, false,                                    \
          "detailed time the compiler (requires +TimeCompiler)")            \
                                                                            \
  diagnostic(bool, PrintInlining, false,                                    \
          "prints inlining optimizations")                                  \
                                                                            \
develop(bool,UseIntrinsics,true,\
"Turn on/off intrinsification")\
                                                                            \
  diagnostic(bool, PrintIntrinsics, false,                                  \
          "prints attempted and successful inlining of intrinsics")         \
                                                                            \
  diagnostic(ccstrlist, DisableIntrinsic, "",                               \
          "do not expand intrinsics whose (internal) names appear here")    \
                                                                            \
  develop(bool, StressReflectiveCode, false,                                \
          "Use inexact types at allocations, etc., to test reflection")     \
                                                                            \
  develop(bool, EagerInitialization, false,                                 \
          "Eagerly initialize classes if possible")                         \
                                                                            \
develop(bool,PrintC2Inlining,false,\
"prints c2 inlining optimizations")\
                                                                            \
develop(bool,LogCompilerOutput,false,\
"Redirect compiler debuggery to a stringStream (for ARTA consumption)")\
                                                                            \
  product(bool, PrintVMOptions, trueInDebug,                                \
         "print VM flag settings")                                          \
                                                                            \
  diagnostic(bool, SerializeVMOutput, true,                                 \
         "Use a mutex to serialize output to tty and hotspot.log")          \
                                                                            \
  diagnostic(bool, DisplayVMOutput, true,                                   \
         "Display all VM output on the tty, independently of LogVMOutput")  \
                                                                            \
  diagnostic(bool, LogVMOutput, trueInDebug,                                \
         "Save VM output to hotspot.log, or to LogFile")                    \
                                                                            \
  diagnostic(ccstr, LogFile, NULL,                                          \
         "If LogVMOutput is on, save VM output to this file [hotspot.log]") \
                                                                            \
  product(ccstr, ErrorFile, NULL,                                           \
         "If an error occurs, save the error data to this file "            \
         "[default: ./hs_err_pid%p.log] (%p replaced with pid)")            \
                                                                            \
  product(bool, DisplayVMOutputToStderr, false,                             \
         "If DisplayVMOutput is true, display all VM output to stderr")     \
                                                                            \
  product(bool, DisplayVMOutputToStdout, false,                             \
         "If DisplayVMOutput is true, display all VM output to stdout")     \
                                                                            \
  product(bool, UseHeavyMonitors, false,                                    \
          "use heavyweight instead of lightweight Java monitors")           \
                                                                            \
  notproduct(bool, PrintSymbolTableSizeHistogram, false,                    \
          "print histogram of the symbol table")                            \
                                                                            \
develop(bool,PrintCodeProfile,false,\
"print code profile entries on compilaion")\
                                                                            \
  notproduct(bool, ExitVMOnVerifyError, false,                              \
          "standard exit from VM if bytecode verify error "                 \
          "(only in debug mode)")                                           \
                                                                            \
  notproduct(ccstr, AbortVMOnException, NULL,                               \
          "Call fatal if this exception is thrown.  Example: "              \
          "java -XX:AbortVMOnException=java.lang.NullPointerException Foo") \
                                                                            \
  develop(bool, PrintVtables, false,                                        \
          "print vtables when printing klass")                              \
                                                                            \
product(bool,DisableLoopOptimizations,false,\
"disable major loop optimizations")\
                                                                            \
develop(bool,VerifyLoopOptimizations,false,\
          "verify major loop optimizations")                                \
                                                                            \
  product(bool, RangeCheckElimination, true,                                \
          "Split loop iterations to eliminate range checks")                \
                                                                            \
  develop_pd(bool, UncommonNullCast,                                        \
          "track occurrences of null in casts; adjust compiler tactics")    \
                                                                            \
  develop(bool, TypeProfileCasts,  true,                                    \
          "treat casts like calls for purposes of type profiling")          \
                                                                            \
  develop(bool, MonomorphicArrayCheck, true,                                \
          "Uncommon-trap array store checks that require full type check")  \
                                                                            \
develop(bool,UseFreezeAndMelt,false,\
"Use Freeze And Melt")\
                                                                            \
  develop(ccstr, FreezeAndMeltInFile, "",                                   \
"Named FIFO from gdb to hotspot")\
                                                                            \
  develop(ccstr, FreezeAndMeltOutFile, "",                                  \
"Named FIFO from hotspot to gdb")\
                                                                            \
develop(intx,FreezeAndMeltThreadNumber,-1,\
"GDB thread number of the dead compiler thread")\
                                                                            \
develop(intx,FreezeAndMeltCompiler,2,\
"Which compiler to use (1=c1, 2=c2)")\
                                                                            \
  develop(bool, CompileTheWorld, false,                                     \
          "Compile all methods in all classes in bootstrap class path "     \
          "(stress test)")                                                  \
                                                                            \
develop(intx,CompileTheWorldSeed,12345,\
"In tiered mode, CompileTheWorld creates artificial counts "\
"in C2's Code Profile.  This seed controls the RNG")\
                                                                            \
product(bool,AllowEndlessDeopt,false,\
"If enabled and an endless deopt occurs, print the endless deopt "\
"messages.  If disabled, don't print any messages and stop "\
"compiling the faulty method.")\
                                                                            \
  develop(bool, CompileTheWorldPreloadClasses, true,                        \
          "Preload all classes used by a class before start loading")       \
                                                                            \
  notproduct(bool, CompileTheWorldIgnoreInitErrors, false,                  \
          "Compile all methods although class initializer failed")          \
                                                                            \
  develop(bool, TraceIterativeGVN, false,                                   \
          "Print progress during Iterative Global Value Numbering")         \
                                                                            \
  develop(bool, FillDelaySlots, true,                                       \
          "Fill delay slots (on SPARC only)")                               \
                                                                            \
  develop(bool, VerifyIterativeGVN, false,                                  \
          "Verify Def-Use modifications during sparse Iterative Global "    \
          "Value Numbering")                                                \
                                                                            \
  notproduct(bool, TracePhaseCCP, false,                                    \
          "Print progress during Conditional Constant Propagation")         \
                                                                            \
  develop(bool, TimeLivenessAnalysis, false,                                \
          "Time computation of bytecode liveness analysis")                 \
                                                                            \
  develop(bool, TraceLivenessGen, false,                                    \
          "Trace the generation of liveness analysis information")          \
                                                                            \
  notproduct(bool, TraceLivenessQuery, false,                               \
          "Trace queries of liveness analysis information")                 \
                                                                            \
  notproduct(bool, CollectIndexSetStatistics, false,                        \
          "Collect information about IndexSets")                            \
                                                                            \
  develop(bool, PrintDominators, false,                                     \
          "Print out dominator trees for GVN")                              \
                                                                            \
  notproduct(bool, TraceCISCSpill, false,                                   \
          "Trace allocators use of cisc spillable instructions")            \
                                                                            \
  notproduct(bool, TraceSpilling, false,                                    \
          "Trace spilling")                                                 \
                                                                            \
  develop(bool, DeutschShiffmanExceptions, true,                            \
          "Fast check to find exception handler for precisely typed "       \
          "exceptions")                                                     \
                                                                            \
  product(bool, SplitIfBlocks, true,                                        \
          "Clone compares and control flow through merge points to fold "   \
          "some branches")                                                  \
                                                                            \
  develop(intx, FastAllocateSizeLimit, 128*K,                               \
          /* Note:  This value is zero mod 1<<13 for a cheap sparc set. */  \
          "Inline allocations larger than this in doublewords must go slow")\
                                                                            \
  product(bool, AggressiveOpts, false,                                      \
          "Enable aggressive optimizations - see arguments.cpp")            \
                                                                            \
  /* statistics */                                                          \
  develop(bool, CountJNICalls, false,                                       \
          "counts jni method invocations")                                  \
                                                                            \
  notproduct(bool, CountJVMCalls, false,                                    \
          "counts jvm method invocations")                                  \
                                                                            \
  notproduct(bool, ICMissHistogram, false,                                  \
          "produce histogram of IC misses")                                 \
                                                                            \
  notproduct(bool, PrintClassStatistics, false,                             \
          "prints class statistics at end of run")                          \
                                                                            \
  notproduct(bool, PrintMethodStatistics, false,                            \
          "prints method statistics at end of run")                         \
                                                                            \
  /* interpreter */                                                         \
  product_pd(bool, RewriteBytecodes,                                        \
          "Allow rewriting of bytecodes (bytecodes are not immutable)")     \
                                                                            \
  product_pd(bool, RewriteFrequentPairs,                                    \
          "Rewrite frequently used bytecode pairs into a single bytecode")  \
                                                                            \
  product(bool, PrintInterpreter, false,                                    \
          "Prints the generated interpreter code")                          \
                                                                            \
product(jint,UseInterpreter,true,\
          "Use interpreter for non-compiled methods")                       \
                                                                            \
  product(bool, UseFastEmptyMethods, true,                                  \
          "Use fast method entry code for empty methods")                   \
                                                                            \
  product(bool, UseFastAccessorMethods, true,                               \
          "Use fast method entry code for accessor methods")                \
                                                                            \
  product_pd(bool, UseOnStackReplacement,                                   \
           "Use on stack replacement, calls runtime if invoc. counter "     \
           "overflows in loop")                                             \
                                                                            \
  notproduct(bool, TraceOnStackReplacement, false,                          \
          "Trace on stack replacement")                                     \
                                                                            \
  develop(bool, CountBytecodes, false,                                      \
          "Count number of bytecodes executed")                             \
                                                                            \
develop(bool,PrintAdapterHandlers,false,\
"Print code generated for i2c/c2i adapters and native method wrappers")\
                                                                            \
  develop(bool, CheckUnhandledOops, false,                                  \
          "Check for unhandled oops in VM code")                            \
                                                                            \
  develop(bool, VerifyJNIFields, trueInDebug,                               \
          "Verify jfieldIDs for instance fields")                           \
                                                                            \
  notproduct(bool, VerifyJNIEnvThread, false,                               \
          "Verify JNIEnv.thread == Thread::current() when entering VM "     \
          "from JNI")                                                       \
                                                                            \
  develop(bool, VerifyFPU, false,                                           \
          "Verify FPU state (check for NaN's, etc.)")                       \
                                                                            \
  develop(bool, VerifyThread, false,                                        \
          "Watch the thread register for corruption (SPARC only)")          \
                                                                            \
  develop(bool, VerifyActivationFrameSize, false,                           \
          "Verify that activation frame didn't become smaller than its "    \
          "minimal size")                                                   \
                                                                            \
  develop(intx, VerifyOopLevel, trueInDebug ? 1 : 0,                        \
          "Do plausibility checks for oops")                                \
                                                                            \
  develop_pd(bool, ProfileTraps,                                            \
          "Profile deoptimization traps at the bytecode level")             \
                                                                            \
  /* compilation */                                                         \
product(bool,TraceCompilationPolicy,false,\
          "Trace compilation policy")                                       \
                                                                            \
  develop(bool, TimeCompilationPolicy, false,                               \
          "Time the compilation policy")                                    \
                                                                            \
  product(bool, DontCompileHugeMethods, true,                               \
          "don't compile methods > HugeMethodLimit")                        \
                                                                            \
  /* Bytecode escape analysis estimation. */                                \
  product(bool, EstimateArgEscape, true,                                    \
          "Analyze bytecodes to estimate escape state of arguments")        \
                                                                            \
  product(intx, BCEATraceLevel, 0,                                          \
          "How much tracing to do of bytecode escape analysis estimates")   \
                                                                            \
  product(intx, MaxBCEAEstimateLevel, 5,                                    \
          "Maximum number of nested calls that are analyzed by BC EA.")     \
                                                                            \
  product(intx, MaxBCEAEstimateSize, 150,                                   \
          "Maximum bytecode size of a method to be analyzed by BC EA.")     \
                                                                            \
  /* deoptimization */                                                      \
product(bool,TraceDeoptimization,false,\
          "Trace deoptimization")                                           \
                                                                            \
  develop(bool, DebugDeoptimization, false,                                 \
          "Tracing various information while debugging deoptimization")     \
                                                                            \
  product(intx, SelfDestructTimer, 0,                                       \
          "Will cause VM to terminate after a given time (in minutes) "     \
          "(0 means off)")                                                  \
                                                                            \
  product(intx, MaxJavaStackTraceDepth, 1024,                               \
          "Max. no. of lines in the stack trace for Java exceptions "       \
          "(0 means all)")                                                  \
                                                                            \
  develop(intx, GuaranteedSafepointInterval, 1000,                          \
          "Guarantee a safepoint (at least) every so many milliseconds "    \
          "(0 means none)")                                                 \
                                                                            \
product(intx,SafepointTimeoutDelay,100000,\
          "Delay in milliseconds for option SafepointTimeout")              \
                                                                            \
product(intx,CheckpointTimeoutDelay,100000,\
"Delay in milliseconds for option CheckpointTimeout")\
                                                                            \
  notproduct(intx, MemProfilingInterval, 500,                               \
          "Time between each invocation of the MemProfiler")                \
                                                                            \
  develop(intx, MallocCatchPtr, -1,                                         \
          "Hit breakpoint when mallocing/freeing this pointer")             \
                                                                            \
  notproduct(intx, AssertRepeat, 1,                                         \
          "number of times to evaluate expression in assert "               \
          "(to estimate overhead); only works with -DUSE_REPEATED_ASSERTS") \
                                                                            \
  notproduct(ccstrlist, SuppressErrorAt, "",                                \
          "List of assertions (file:line) to muzzle")                       \
                                                                            \
  notproduct(uintx, HandleAllocationLimit, 1024,                            \
          "Threshold for HandleMark allocation when +TraceHandleAllocation "\
          "is used")                                                        \
                                                                            \
  develop(uintx, TotalHandleAllocationLimit, 1024,                          \
          "Threshold for total handle allocation when "                     \
          "+TraceHandleAllocation is used")                                 \
                                                                            \
  develop(intx, StackPrintLimit, 100,                                       \
          "number of stack frames to print in VM-level stack dump")         \
                                                                            \
  notproduct(intx, MaxElementPrintSize, 256,                                \
          "maximum number of elements to print")                            \
                                                                            \
  notproduct(intx, MaxSubklassPrintSize, 4,                                 \
          "maximum number of subklasses to print when printing klass")      \
                                                                            \
  develop(intx, MaxInlineLevel, 9,                                          \
          "maximum number of nested calls that are inlined")                \
                                                                            \
develop(intx,MaxRecursiveInlineLevel,3,\
          "maximum number of nested recursive calls that are inlined")      \
                                                                            \
  develop(intx, InlineSmallCode, 1000,                                      \
          "Only inline already compiled methods if their code size is "     \
          "less than this")                                                 \
                                                                            \
product(intx,C1MaxInlineSize,35,\
          "maximum bytecode size of a method to be inlined")                \
                                                                            \
product(intx,C2MaxInlineSize,75,\
          "maximum bytecode size of a method to be inlined")                \
                                                                            \
  develop(intx, MaxTrivialSize, 6,                                          \
          "maximum bytecode size of a trivial method to be inlined")        \
                                                                            \
  develop(intx, MinInliningThreshold, 250,                                  \
          "min. invocation count a method needs to have to be inlined")     \
                                                                            \
  develop(intx, AlignEntryCode, 4,                                          \
          "aligns entry code to specified value (in bytes)")                \
                                                                            \
  develop(intx, MethodHistogramCutoff, 100,                                 \
          "cutoff value for method invoc. histogram (+CountCalls)")         \
                                                                            \
  develop(intx, ProfilerNumberOfInterpretedMethods, 25,                     \
          "# of interpreted methods to show in profile")                    \
                                                                            \
  develop(intx, ProfilerNumberOfCompiledMethods, 25,                        \
          "# of compiled methods to show in profile")                       \
                                                                            \
  develop(intx, ProfilerNumberOfStubMethods, 25,                            \
          "# of stub methods to show in profile")                           \
                                                                            \
  develop(intx, ProfilerNumberOfRuntimeStubNodes, 25,                       \
          "# of runtime stub nodes to show in profile")                     \
                                                                            \
  product(intx, ProfileIntervalsTicks, 100,                                 \
          "# of ticks between printing of interval profile "                \
          "(+ProfileIntervals)")                                            \
                                                                            \
  notproduct(intx, ScavengeALotInterval,     1,                             \
          "Interval between which scavenge will occur with +ScavengeALot")  \
                                                                            \
  notproduct(intx, FullGCALotInterval,     1,                               \
          "Interval between which full gc will occur with +FullGCALot")     \
                                                                            \
  notproduct(intx, FullGCALotStart,     0,                                  \
          "For which invocation to start FullGCAlot")                       \
                                                                            \
  notproduct(intx, FullGCALotDummies,  32*K,                                \
          "Dummy object allocated with +FullGCALot, forcing all objects "   \
          "to move")                                                        \
                                                                            \
  develop(intx, DontYieldALotInterval,    10,                               \
          "Interval between which yields will be dropped (milliseconds)")   \
                                                                            \
  develop(intx, MinSleepInterval,     1,                                    \
          "Minimum sleep() interval (milliseconds) when "                   \
          "ConvertSleepToYield is off (used for SOLARIS)")                  \
                                                                            \
  develop(intx, ProfilerPCTickThreshold,    15,                             \
          "Number of ticks in a PC buckets to be a hotspot")                \
                                                                            \
  diagnostic(intx, MallocVerifyInterval,     0,                             \
          "if non-zero, verify C heap after every N calls to "              \
          "malloc/realloc/free")                                            \
                                                                            \
  diagnostic(intx, MallocVerifyStart,     0,                                \
          "if non-zero, start verifying C heap after Nth call to "          \
          "malloc/realloc/free")                                            \
                                                                            \
  product(intx, TypeProfileWidth,      2,                                   \
          "number of receiver types to record in call/cast profile")        \
                                                                            \
  develop(intx, BciProfileWidth,      2,                                    \
          "number of return bci's to record in ret profile")                \
                                                                            \
  develop(intx, FreqCountInvocations,  1,                                   \
          "Scaling factor for branch frequencies (deprecated)")             \
                                                                            \
  develop(intx, InlineFrequencyRatio,    20,                                \
          "Ratio of call site execution to caller method invocation")       \
                                                                            \
product_pd(intx,InlineFrequencyCount,\
          "Count of call site execution necessary to trigger frequent "     \
          "inlining")                                                       \
                                                                            \
  develop(intx, InlineThrowCount,    50,                                    \
          "Force inlining of interpreted methods that throw this often")    \
                                                                            \
  develop(intx, InlineThrowMaxSize,   200,                                  \
          "Force inlining of throwing methods smaller than this")           \
                                                                            \
  product(intx, AliasLevel,     3,                                          \
          "0 for no aliasing, 1 for oop/field/static/array split, "         \
          "2 for class split, 3 for unique instances")                      \
                                                                            \
  develop(bool, VerifyAliases, false,                                       \
          "perform extra checks on the results of alias analysis")          \
                                                                            \
  develop(intx, ProfilerNodeSize,  1024,                                    \
          "Size in K to allocate for the Profile Nodes of each thread")     \
                                                                            \
  develop(intx, V8AtomicOperationUnderLockSpinCount,    50,                 \
          "Number of times to spin wait on a v8 atomic operation lock")     \
                                                                            \
product(intx,ReadSpinIterations,500,\
          "Number of read attempts before a yield (spin inner loop)")       \
                                                                            \
product(intx,ReadSpinThrottle,10,\
"Cycles of busy loop before retrying spin")\
                                                                            \
  product_pd(intx, PreInflateSpin,                                          \
          "Number of times to spin wait before inflation")                  \
                                                                            \
product(intx,PreBlockSpin,100,\
          "Number of times to spin in an inflated lock before going to "    \
          "an OS lock")                                                     \
                                                                            \
  /* gc parameters */                                                       \
  product(uintx, MaxHeapSize, 0, /* set in Arguments::set_gc_flags() */     \
          "Default maximum size for object heap (in bytes)")                \
                                                                            \
  product_pd(uintx, NewSize, 						    \
          "Default size of new generation (in bytes)")                      \
                                                                            \
  product(uintx, MaxNewSize, max_uintx,                             	    \
          "Maximum size of new generation (in bytes)")                      \
                                                                            \
  product(uintx, PretenureSizeThreshold, 0,                                 \
          "Max size in bytes of objects allocated in DefNew generation")    \
                                                                            \
  product_pd(uintx, TLABSize,                                               \
          "Default (or starting) size of TLAB (in bytes)")                  \
                                                                            \
  product(uintx, MinTLABSize, 2*K,                                          \
          "Minimum allowed TLAB size (in bytes)")                           \
                                                                            \
product(uintx,MaxTLABObjectAllocationWords,32767,\
"Don't allocate objects this big or larger in a TLAB.")\
                                                                            \
  product(uintx, TLABAllocationWeight, 35,                                  \
          "Allocation averaging weight")                                    \
                                                                            \
  product(uintx, TLABWasteTargetPercent, 1,                                 \
          "Percentage of Eden that can be wasted")                          \
                                                                            \
  product(uintx, TLABRefillWasteFraction,    64,                            \
          "Max TLAB waste at a refill (internal fragmentation)")            \
                                                                            \
  product(uintx, TLABWasteIncrement,    4,                                  \
          "Increment allowed waste at slow allocation")                     \
                                                                            \
product_pd(uintx,TLABPrefetchSize,\
"Number of bytes to prefetch ahead of allocation in TLAB")\
                                                                            \
product_pd(uintx,TLABZeroRegion,\
"Number of bytes to zero ahead of allocation in TLAB")\
                                                                            \
  product_pd(intx, SurvivorRatio,                                           \
          "Ratio of eden/survivor space size")                              \
                                                                            \
  product_pd(intx, NewRatio,                                                \
          "Ratio of new/old generation sizes")                              \
                                                                            \
  product(uintx, MaxLiveObjectEvacuationRatio, 100,                         \
          "Max percent of eden objects that will be live at scavenge")      \
                                                                            \
  product_pd(uintx, NewSizeThreadIncrease,                                  \
          "Additional size added to desired new generation size per "       \
          "non-daemon thread (in bytes)")                                   \
                                                                            \
  product(uintx, OldSize, ScaleForWordSize(4096*K),                         \
          "Default size of tenured generation (in bytes)")                  \
                                                                            \
  product_pd(uintx, PermSize,                                               \
          "Default size of permanent generation (in bytes)")                \
                                                                            \
  product_pd(uintx, MaxPermSize,					    \
          "Maximum size of permanent generation (in bytes)")                \
                                                                            \
  product(uintx, MinHeapFreeRatio,    40,                                   \
          "Min percentage of heap free after GC to avoid expansion")        \
                                                                            \
  product(uintx, MaxHeapFreeRatio,    70,                                   \
          "Max percentage of heap free after GC to avoid shrinking")        \
                                                                            \
product(intx,SoftRefLRUPolicyMSPerMB,100,\
          "Number of milliseconds per MB of free space in the heap")        \
                                                                            \
  product(uintx, MinHeapDeltaBytes, ScaleForWordSize(128*K),                \
          "Min change in heap space due to GC (in bytes)")                  \
                                                                            \
  product(uintx, MinPermHeapExpansion, ScaleForWordSize(256*K),             \
          "Min expansion of permanent heap (in bytes)")                     \
                                                                            \
  product(uintx, MaxPermHeapExpansion, ScaleForWordSize(4*M),               \
          "Max expansion of permanent heap without full GC (in bytes)")     \
                                                                            \
  product(intx, QueuedAllocationWarningCount, 0,                            \
          "Number of times an allocation that queues behind a GC "          \
          "will retry before printing a warning")                           \
                                                                            \
  diagnostic(uintx, VerifyGCStartAt,   0,                                   \
          "GC invoke count where +VerifyBefore/AfterGC kicks in")           \
                                                                            \
  diagnostic(intx, VerifyGCLevel,     0,                                    \
          "Generation level at which to start +VerifyBefore/AfterGC")       \
                                                                            \
  develop(uintx, ExitAfterGCNum,   0,                                       \
          "If non-zero, exit after this GC.")	                              \
                                                                            \
  product(intx, MaxTenuringThreshold, 31, /* use all 5 markWord age bits */ \
          "Maximum value for tenuring threshold")                           \
                                                                            \
  product(intx, InitialTenuringThreshold,     7,                            \
          "Initial value for tenuring threshold")                           \
                                                                            \
  product(intx, TargetSurvivorRatio,    50,                                 \
          "Desired percentage of survivor space used after scavenge")       \
                                                                            \
  product(intx, MarkSweepDeadRatio,     5,                                  \
          "Percentage (0-100) of the old gen allowed as dead wood."	    \
          "Serial mark sweep treats this as both the min and max value."    \
          "CMS uses this value only if it falls back to mark sweep."	    \
          "Par compact uses a variable scale based on the density of the"   \
          "generation and treats this as the max value when the heap is"    \
          "either completely full or completely empty.  Par compact also"   \
          "has a smaller default value; see arguments.cpp.")		    \
                                                                            \
  product(intx, PermMarkSweepDeadRatio,    20,                              \
          "Percentage (0-100) of the perm gen allowed as dead wood."	    \
          "See MarkSweepDeadRatio for collector-specific comments.")	    \
                                                                            \
  product(intx, MarkSweepAlwaysCompactCount,     4,                         \
          "How often should we fully compact the heap (ignoring the dead "  \
          "space parameters)")                                              \
                                                                            \
  develop(uintx, GCExpandToAllocateDelayMillis, 0,                          \
          "Delay in ms between expansion and allocation")                   \
                                                                            \
  product(bool, UseDepthFirstScavengeOrder, true,                           \
          "true: the scavenge order will be depth-first, "                  \
          "false: the scavenge order will be breadth-first")                \
                                                                            \
  product(uintx, GCDrainStackTargetSize, 64,                                \
          "how many entries we'll try to leave on the stack during "        \
          "parallel GC")                                                    \
                                                                            \
  /* stack parameters */                                                    \
  product_pd(intx, StackYellowPages,                                        \
          "Number of yellow zone (recoverable overflows) pages")            \
                                                                            \
  develop_pd(uintx, JVMInvokeMethodSlack,                                   \
          "Stack space (bytes) required for JVM_InvokeMethod to complete")  \
                                                                            \
  /* code cache parameters */                                               \
  product_pd(uintx, ReservedCodeCacheSize,                                  \
          "Reserved code cache size (in bytes) - maximum code cache size")  \
                                                                            \
product(uintx,CodeCacheMinimumFreeSpace,3500*K,\
          "When less than X space left, we stop compiling.")                \
                                                                            \
  notproduct(bool, ExitOnFullCodeCache, false,                              \
          "Exit the VM if we fill the code cache.")                         \
                                                                            \
  notproduct(bool, PrintCodeCache, false,                                   \
          "Print the compiled_code cache when exiting")                     \
                                                                            \
  develop_pd(intx, CodeEntryAlignment,                                      \
          "Code entry alignment for generated code (in bytes)")             \
                                                                            \
  /* data cache parameters */                                               \
develop_pd(uintx,PrefetchStride,\
"Byte size stride between prefetches")\
                                                                            \
  /* interpreter debugging */                                               \
  develop(intx, BinarySwitchThreshold, 5,                                   \
          "Minimal number of lookupswitch entries for rewriting to binary " \
          "switch")                                                         \
                                                                            \
  develop(intx, StopInterpreterAt, 0,                                       \
          "Stops interpreter execution at specified bytecode number")       \
                                                                            \
  develop(intx, TraceBytecodesAt, 0,                                        \
          "Traces bytecodes starting with specified bytecode number")       \
                                                                            \
  /* compiler interface */                                                  \
product(intx,CIStart,0,\
          "the id of the first compilation to permit")                      \
                                                                            \
product(intx,CIStop,0x7FFFFFFF,\
          "the id of the last compilation to permit")                       \
                                                                            \
  develop(intx, CIBreakAt,    -1,                                           \
          "id of compilation to break at")                                  \
                                                                            \
  product(ccstrlist, CompileOnly, "",                                       \
          "List of methods (pkg/class.name) to restrict compilation to")    \
                                                                            \
  product(ccstr, CompileCommandFile, NULL,                                  \
          "Read compiler commands from this file [.hotspot_compiler]")      \
                                                                            \
  product(ccstrlist, CompileCommand, "",                                    \
          "Prepend to .hotspot_compiler; e.g. log,java/lang/String.<init>") \
                                                                            \
  develop(intx, CIFireOOMAt,    -1,                                         \
          "Fire OutOfMemoryErrors throughout CI for testing the compiler "  \
          "(non-negative value throws OOM after this many CI accesses "     \
          "in each compile)")                                               \
                                                                            \
  develop(intx, CIFireOOMAtDelay, -1,                                       \
          "Wait for this many CI accesses to occur in all compiles before " \
          "beginning to throw OutOfMemoryErrors in each compile")           \
                                                                            \
  /* Priorities */                                                          \
  product_pd(bool, UseThreadPriorities,  "Use native thread priorities")    \
                                                                            \
  product(intx, ThreadPriorityPolicy, 0,                                    \
          "0 : Normal.                                                     "\
          "    VM chooses priorities that are appropriate for normal       "\
          "    applications. On Solaris NORM_PRIORITY and above are mapped "\
          "    to normal native priority. Java priorities below NORM_PRIORITY"\
          "    map to lower native priority values. On Windows applications"\
          "    are allowed to use higher native priorities. However, with  "\
          "    ThreadPriorityPolicy=0, VM will not use the highest possible"\
          "    native priority, THREAD_PRIORITY_TIME_CRITICAL, as it may   "\
          "    interfere with system threads. On Linux thread priorities   "\
          "    are ignored because the OS does not support static priority "\
          "    in SCHED_OTHER scheduling class which is the only choice for"\
          "    non-root, non-realtime applications.                        "\
          "1 : Aggressive.                                                 "\
          "    Java thread priorities map over to the entire range of      "\
          "    native thread priorities. Higher Java thread priorities map "\
          "    to higher native thread priorities. This policy should be   "\
          "    used with care, as sometimes it can cause performance       "\
          "    degradation in the application and/or the entire system. On "\
          "    Linux this policy requires root privilege.")                 \
                                                                            \
  product(bool, ThreadPriorityVerbose, false,                               \
          "print priority changes")                                         \
                                                                            \
  product(intx, DefaultThreadPriority, -1,                                  \
          "what native priority threads run at if not specified elsewhere (-1 means no change)") \
                                                                            \
develop_pd(uintx,JavaThreadMaxPriority,\
"OS priority of top priority Java threads")\
develop_pd(uintx,SignalThreadPriority,\
"what priority should the signal dispatcher thread run at")\
develop_pd(uintx,LowMemoryDetectorPriority,\
"what priority should the low memory detector thread run at")\
develop_pd(uintx,CompilerThreadPriority,\
"what priority should compiler threads run at")\
develop_pd(uintx,SurrogateLockerPriority,\
"what priority should the surrogate locker thread run at")\
develop_pd(uintx,CheckpointBoostPriority,\
"temp priority for JavaThreads to force checkpoint completion")\
develop_pd(uintx,GCThreadPriority,\
"priority for concurrent GC coordinating threads")\
develop_pd(uintx,WatcherThreadPriority,\
"what priority should the watcher thread run at")\
develop_pd(uintx,TransportPriority,\
"what priority should transport and rpc threads run at")\
develop_pd(uintx,VMThreadPriority,\
"what priority should the VM thread run at")\
                                                                            \
  product(bool, CompilerThreadHintNoPreempt, true,                          \
          "(Solaris only) Give compiler threads an extra quanta")           \
                                                                            \
  product(bool, VMThreadHintNoPreempt, false,                               \
          "(Solaris only) Give VM thread an extra quanta")                  \
                                                                            \
  product(intx, JavaPriority1_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority2_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority3_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority4_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority5_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority6_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority7_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority8_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority9_To_OSPriority, -1, "Map Java priorities to OS priorities") \
  product(intx, JavaPriority10_To_OSPriority,-1, "Map Java priorities to OS priorities") \
                                                                            \
  /* compiler debugging */                                                  \
  notproduct(intx, CompileTheWorldStartAt,     1,                           \
          "First class to consider when using +CompileTheWorld")            \
                                                                            \
  notproduct(intx, CompileTheWorldStopAt, max_jint,                         \
          "Last class to consider when using +CompileTheWorld")             \
                                                                            \
  develop(intx, NewCodeParameter,      0,                                   \
          "Testing Only: Create a dedicated integer parameter before "      \
          "putback")                                                        \
                                                                            \
  /* new oopmap storage allocation */                                       \
  develop(intx, MinOopMapAllocation,     8,                                 \
          "Minimum number of OopMap entries in an OopMapSet")               \
                                                                            \
  /* Background Compilation */                                              \
  develop(intx, LongCompileThreshold,     50,                               \
          "Used with +TraceLongCompiles")                                   \
                                                                            \
  /* recompilation */                                                       \
  develop(intx, MaxRecompilationSearchLength,    10,                        \
          "max. # frames to inspect searching for recompilee")              \
                                                                            \
develop(intx,MaxInterpretedSearchLength,10,\
          "max. # interp. frames to skip when searching for recompilee")    \
                                                                            \
product(intx,DesiredMethodLimit,8000,\
          "desired max. method size (in bytecodes) after inlining")         \
                                                                            \
product(intx,HugeMethodLimit,8000,\
"don't compile methods larger than this")\
                                                                            \
  /* New JDK 1.4 reflection implementation */                               \
                                                                            \
  develop(bool, VerifyReflectionBytecodes, false,                           \
          "Force verification of 1.4 reflection bytecodes. Does not work "  \
          "in situations like that described in 4486457 or for "            \
          "constructors generated for serialization, so can not be enabled "\
          "in product.")                                                    \
                                                                            \
  product(bool, ReflectionWrapResolutionErrors, true,                       \
          "Temporary flag for transition to AbstractMethodError wrapped "   \
          "in InvocationTargetException. See 6531596")                      \
                                                                            \
                                                                            \
  develop(intx, FastSuperclassLimit, 8,                                     \
          "Depth of hardwired instanceof accelerator array")                \
                                                                            \
  /* Properties for Java libraries  */                                      \
                                                                            \
  product(intx, MaxDirectMemorySize, -1,                                    \
          "Maximum total size of NIO direct-buffer allocations")            \
                                                                            \
  /* Azul specific */                                                       \
                                                                            \
product(bool,UseGenPauselessGC,false,\
"Use Azul's Generational Pauseless Concurrent GC ")\
                                                                            \
product(uintx,SideBandSpacePadding,25,\
"Percent padding added to side-band relocation hash tables for reduced collisions")\
                                                                            \
product(uintx,GenPauselessNewThreads,0,\
"Number of threads used for GPGC of new gen")\
                                                                            \
product(uintx,GenPauselessOldThreads,0,\
"Number of threads used for GPGC of old gen")\
                                                                            \
product(uintx,GPGCSafepointSpacing,50,\
"Minimum number of milliseconds between GPGC safepoints")\
                                                                            \
product(uintx,GPGCTimeStampPromotionThreshold,20,\
"Seconds spent in new-gen before a page is promoted to old-gen")\
                                                                            \
product(uintx,GPGCEmptyStacks,20,\
"Number of stacks added each time we run out of stacks in the free-spill")\
                                                                            \
product(intx,GPGCJLRsPerWorkUnit,1000,\
"Number of java.lang.refs per work unit in parallel JLR handling")\
                                                                            \
product(uintx,IncrementalObjInitThresholdWords,32768,\
"Zero only this many words of new objects between safepoints.")\
                                                                            \
product(intx,IncrementalCloneThresholdWords,16384,\
"Clone only this many words of objects between safepoints.")\
                                                                            \
product(intx,IncrementalArrayCopyThresholdWords,16384,\
"Clone only this many words of objects between safepoints.")\
                                                                            \
product(uintx,GPGCThreads,0,\
"Number of parallel threads GPGC will use, sets all sub options")\
                                                                            \
product(intx,GPGCFailedAllocRetries,2,\
"GPGC: how many times to retry alloc before throwing OOM exception.")\
                                                                            \
product(bool,GPGCOptimisticExplicitGC,true,\
"GPGC: ignore System.gc() calls if one is in progress.")\
                                                                            \
product(intx,GPGCOverflowMemory,-1,\
"GPGC: allocation limit from overflow fund if heap is too small.")\
                                                                            \
product(intx,GPGCPausePreventionMemory,-1,\
"GPGC: allocation limit from pause fund to prevent GC pauses.")\
                                                                            \
product(intx,GPGCSidebandPagesPercent,5,\
"The size of the GPGC sideband array is this % of the object heap size.")\
                                                                            \
product(bool,SynchronousExplicitGC,false,\
"GPGC: System.gc() calls will block the calling thread")\
                                                                            \
product(intx,GPGCMaxSidebandPercent,66,\
"Percent of the GPGC sideband array usable by one collector.")\
                                                                            \
product(intx,GPGCOldGCSidebandTrigger,50,\
"On limited NewGCs, start OldGC if OldGC uses over this percent of sideband.")\
                                                                            \
product(intx,GPGCNewGCIntervalMS,0,\
"If set, GPGC does a NewGC cycle every N milliseconds, instead of using a heuristic.")\
                                                                            \
product(intx,GPGCOldGCInterval,5,\
"If GPGCNewGCIntervalMS set, GPGC does an OldGC cycle every N NewGC cycles.")\
                                                                            \
product(intx,GPGCOldGCIntervalSecs,600,\
"If set, GPGC does an OldGC cycle at least every N seconds.")\
                                                                            \
product(intx,GPGCOldHeadroomUsedPercent,75,\
"% of memory headroom usable by OldGen before an OldGC is started.")\
                                                                            \
product(intx,GPGCHeuristicHalfLifeMins,15,\
"The half-life of a data-point in the GPGC heuristic's model.")\
                                                                            \
product(intx,GPGCHeuristicMinSampleSecs,2,\
"Minimum allocation sample length for the GPGC heuristic.")\
                                                                            \
product(intx,GPGCHeuristicSafetyMargin,41,\
"Safety margin (in percent) used by the GPGC heuristic.")\
                                                                            \
product(intx,GPGCOldGCRelocPagesPerThread,100,\
"Number of pages per thread per GPGC OldGC relocation chunk.")\
                                                                            \
product(bool,GPGCDieWhenThreadDelayed,false,\
"Die when a JavaThread is delayed waiting on a GPGC GC cycle.")\
                                                                            \
develop(bool,GPGCProfileStealMarkTasks,false,\
"Collect and log profile data for GPGC StealMarkTasks.")\
                                                                            \
product(intx,TaskQueueSize,8192,\
"The spill stack size.")\
                                                                            \
product_pd(bool,BatchedMemoryOps,\
"GPGC should use the azmem batched virtual memory syscalls.")\
                                                                            \
product_pd(bool,PageHealing,\
"GPGC should use the azmem page healing memory syscalls.")\
                                                                            \
product(bool,GPGCCollectMidSpace,true,\
"GPGC should collect garbage in mid space pages.")\
                                                                            \
product(bool,GPGCCollectLargeSpace,false,\
"GPGC should collect garbage in large space pages.")\
                                                                            \
product(bool,GPGCVerifyHeap,false,\
"Verify object marks after mark/remap phase of GPGC")\
                                                                            \
product(bool,GPGCVerifyRecursively,false,\
"Use recursion when verifying object marks in GPGC.")\
                                                                            \
product(intx,GPGCVerifyThreads,0,\
"# of threads for GPGCVerifyHeap, 1 for serial. Default same as GPGCThreads.")\
                                                                            \
product(bool,GPGCVerifyCapacity,false,\
"Verify GPGC generation page capacity stats each GC cycle.")\
                                                                            \
product(bool,GPGCAuditTrail,false,\
"Track GPGC object updates for the GPGCVerifyHeap mode")\
                                                                            \
product(bool,GPGCPageAuditTrail,true,\
"Track GPGC page operations")\
                                                                            \
product(intx,GPGCAuditTrailSize,15000,\
"Number of entries in the GPGCAuditTrail ring buffer.")\
                                                                            \
develop(intx,GPGCArrayChunkThreshold,8192,\
"GPGC threshold to begin obj array chunking when marking.")\
                                                                            \
develop(intx,GPGCArrayChunkSize,4096,\
"GPGC size to chunk obj arrays into when marking.")\
                                                                            \
product(intx,GPGCDiagHealDelayMillis,0,\
"ms to sleep before page healing to give mutator traps time to occur.")\
                                                                            \
product(bool,GPGCTraceHeuristic,false,\
"Trace the calculation of the GPGC cycle timing heuristic.")\
                                                                            \
product(bool,GPGCTraceBudget,false,\
"Trace accounting for the GPGC page budget.")\
                                                                            \
product(bool,GPGCTraceRemap,false,\
"Trace GPGC mid space object remapping.")\
                                                                            \
develop(bool,ProfileMemoryTime,false,\
"Track virtual memory system call performance.")\
                                                                            \
product(bool,GPGCTracePageSpace,false,\
"Output trace of PageSpace operation with GPGC.")\
                                                                            \
product(bool,GPGCTraceSparsePages,false,\
"Trace selection of sparse pages during GPGC.")\
                                                                            \
develop(bool,GPGCReuseFreePages,true,\
"Reuse virtual address space after it's been cleared by GPGC.")\
                                                                            \
product(bool,GPGCNoGC,false,\
"Skip both mark/remap and relocation phases of GPGC.")\
                                                                            \
develop(bool,GPGCSafepointAll,false,\
"Maintain safepoint across entire GC cycle of GPGC")\
                                                                            \
product(bool,GPGCSafepointMark,false,\
"Maintain safepoint across full mark/remap phase of GPGC")\
                                                                            \
product(bool,GPGCSafepointRelocate,false,\
"Maintain safepoint across full relocation phase of GPGC")\
                                                                            \
product(bool,GPGCNoRelocation,false,\
"Skip the relocation phase of GPGC.")\
                                                                            \
product(bool,GPGCNoPermRelocation,false,\
"Don't relocate perm gen pages in GPGC's relocation phase.")\
                                                                            \
product(bool,GPGCVerifyThreadStacks,false,\
"Verify our thread stack walking isn't missing any oops.")\
                                                                            \
develop(bool,GPGCKeepPageStats,true,\
"Spend the extra cycles to try and track page usage stats.")\
                                                                            \
product(bool,UseLVBs,false,\
"Generate LVB instructions")\
                                                                            \
develop(bool,VerifyJVMLockAtLVB,false,\
"Before each LVB, verify the thread's JVM lock is held.")\
                                                                            \
develop(bool,RefPoisoning,trueInDebug,\
"Poison stored refs to ensure LVBs are used everywhere.")\
                                                                            \
develop(bool,VerifyRefPoisoning,false,\
"Check values are poisoned when expected, and not when expected.")\
                                                                            \
develop(bool,VerifyRefNMT,false,\
"Check that NMT bits are properly set.  Only works with UseLVBs")\
                                                                            \
develop(bool,PrintGCNMTDetails,false,\
"Log the details of references being NMT trapped.")\
                                                                            \
product(bool,MarkSweepVerifyKIDs,true,\
"Verify KIDs when moving objects in marksweep")\
                                                                            \
develop(bool,PrintOopTableAtExit,false,\
"Prints the oop table at exit")\
                                                                            \
develop(bool,CountOopRefConversions,false,\
"Count oop->ref  ref->oop conversions (slow, so only in debug)")\
                                                                            \
develop(intx,StoreWatchValue,0,\
"Stop when this value is stored")\
                                                                            \
develop(bool,PrintProfileAtExit,false,\
"Prints the tick profile at exit")\
                                                                            \
develop(bool,ProfileMMU,false,\
"Collect Minimum Mutator Utilization, print at exit")\
                                                                            \
  product(intx, LogProfilerChunkSize, (16*M),                               \
"Size of each chunk of data inside the profile")\
                                                                            \
  product(ccstr, ProfileDataFile, "hotspot-profile",                        \
"Default name of profile data output file")\
                                                                            \
product(uintx,LogProfilePeriodicTaskInterval,500,\
"The number of ms between periodic log file flushes")\
                                                                            \
product(bool,TraceCPI,false,\
"Prints the average CPI at VM exit")\
                                                                            \
product(ccstr,TraceCPIFileName,"",\
"Filename to which CPI will be traced")\
                                                                            \
product(uintx,TraceCPICycleCount,5000000,\
"Number of cycles per tick when tracing CPI")\
                                                                            \
product(uintx,TraceCPIInstructionCount,1000000,\
"Number of instructions per tick when tracing CPI")\
                                                                            \
product(uintx,TraceCPIPerfCnt0ControlWord,0x2,\
"First perfcnt register control word")\
                                                                            \
product(uintx,TraceCPIPerfCnt1ControlWord,0x12,\
"Second perfcnt register control word")\
                                                                            \
product_pd(bool,UseTickProfiler,\
"enable TickProfiler at 1000 ticks/second")\
                                                                            \
product(intx,TickProfilerEntryCount,256*K,\
"Number of entries in the tick profiler ring buffer")\
                                                                            \
product(uintx,TickProfilerFrequency,1000,\
"Frequency that threads are profiled in microseconds")\
                                                                            \
product(intx,TickProfilerControlWord,0x2,\
"Value of the percntctl0 register")\
                                                                            \
product(intx,TickProfilerCount1,0,\
"Value of the perfcnt1 register")\
                                                                            \
product(intx,TickProfilerControlWord1,0,\
"Value of the perfcntctl1 register")\
                                                                            \
product(intx,TickProfilerCount4,0,\
"Value of the perfcnt4 register")\
                                                                            \
product(intx,TickProfilerControlWord4,0,\
"Value of the percntctl4 register")\
                                                                            \
product(intx,TickProfilerCount5,0,\
"Value of the perfcnt5 register")\
                                                                            \
product(intx,TickProfilerControlWord5,0,\
"Value of the percntctl5 register")\
                                                                            \
product(intx,TickProfilerCountTLB,0,\
"Value of the software TLB0 miss performance counter")\
                                                                            \
product(intx,TickProfilerCountTLB1,0,\
"Value of the software TLB1 miss performance counter")\
                                                                            \
product(bool,TickProfilerSlaveTicks,false,\
"Record 'slave ticks' in addition to regular ticks, using perfcount1 as the master.")\
                                                                            \
product(ccstr,TickProfilerConfigFile,"",\
"List of perf counter configs to periodically rotate through")\
                                                                            \
product(intx,TickProfilerConfigStartAt,1,\
"First perf counter config entry to periodically rotate through")\
                                                                            \
  product(intx, TickProfilerConfigStopAt, max_jint,                         \
"Last perf counter config entry to periodically rotate through")\
                                                                            \
product(intx,TickProfilerConfigRotationInterval,60000,\
"Interval in milliseconds between perf counter config rotations")\
                                                                            \
product(bool,TickProfileTimeToSafepoint,false,\
"Use tick profiling to find code slow getting to safepoints.")\
                                                                            \
product(intx,ProfileTimeToSafepointMicros,500,\
"Tick in code that takes more than N micros to hit a safepoint.")\
                                                                            \
product(bool,ProfilerLogGC,false,\
"Record -verbose:gc output in external profiler output file")\
                                                                            \
product(intx,ProfilerLogSystemIOInterval,0,\
"Interval in millis between recording system wide I/O stats")\
                                                                            \
product(bool,UseMetaTicks,false,\
"Generate profiler meta ticks")\
                                                                            \
product(bool,OnlySafepointRequestTicks,false,\
"Only record ticks when there is a pending safepoint request")\
                                                                            \
develop(bool,GProfOnSIGQUIT,false,\
"Emit gmon.out when SIGQUIT is received")\
                                                                            \
product_pd(bool,UseSBA,\
"Enable Stack Based Allocation")\
                                                                            \
product(bool,UseSBAHints,true,\
"Enable SBA hints for allocation sites.")\
                                                                            \
product(intx,SBAStackSize,16,\
"Max SBA StackSize in megabytes per thread")\
                                                                            \
product(intx,SBAEscapeThreshold,5,\
"Number of escapes allowed before converting allocation site from stack to heap")\
                                                                            \
product(intx,SBADecayInterval,50000,\
"Milliseconds between decay of SBA escape counts")\
                                                                            \
develop(bool,VerboseSBA,false,\
"Print SBA status messages")\
                                                                            \
develop(bool,VerifySBA,false,\
"Do strict verifications of sba space")\
                                                                            \
product(bool,PrintSBAStatistics,false,\
"Print SBA statistics at end")\
                                                                            \
product(intx,JNILocalHandleCapacity,10000,\
"Per thread JNI local handle capacity")\
                                                                            \
product_pd(bool,UseSMA,\
"Is Speculative Multiaddress Atomicity enabled? This is also know under the marketing name Optimistic Thread Concurrency.")\
                                                                            \
product(intx,SMASpeculationBias,10000000000,\
"How many cycles to fail first before giving up speculating")\
                                                                            \
product(intx,SMACountMaskBits,6,\
"How many bits of mask are applied to the COUNT register")\
                                                                            \
product(intx,SMACountShift,4,\
"Scaling factor for cycles spent in SMA")\
                                                                            \
product(intx,SMARetries,5,\
"How many retries of SMA before thick locking")\
                                                                            \
product(intx,SMAAdviseReevaluationInterval,60,\
"How many seconds have to pass before SMA advice is reevaluated")\
                                                                            \
product(bool,PrintSMAStatistics,false,\
"Print SMA statistics at end")\
                                                                            \
product(bool,ExtendedSMAStatistics,false,\
"Keep additional SMA statistics (requires a special kernel")\
                                                                            \
product(intx,SMAFailureWeight,1,\
"Amount to decrement SMA hint on failure ")\
                                                                            \
product(intx,SMASuccessWeight,1,\
"Amount to increment SMA hint on success")\
                                                                            \
product(bool,SMATraceModeChanges,false,\
"trace transitions of monitors to and from sma mode")\
                                                                            \
product(ccstr,SMAHeuristic,"",\
"choose SMA heuristic type "\
"{basic, hashtables, classbased (default)}")\
                                                                            \
product(intx,SMASamplerInterval,0,\
"Length in seconds of an SMA event sampling interval")\
                                                                            \
product(intx,SMASamplerStart,0,\
"How many intervals have to pass before SMA event sampling starts")\
                                                                            \
product(intx,SMASamplerLength,1000,\
"How many intervals of sampling to make")\
                                                                            \
product(bool,SMASamplerCombineSF,true,\
"Whether to combine success and failure totals in report")\
                                                                            \
product(uintx,SMAPerfEvt0,0,\
"Gather performance (PerfCnt0) info for event"\
"during SMA.")\
                                                                            \
product(uintx,SMAPerfEvt1,0,\
"Gather performance (PerfCnt1) info for event"\
"during SMA.")\
                                                                            \
product(bool,WeblogicNativeIO,false,\
"Replace Weblogic native I/O with an Azul-compatible version")\
                                                                            \
product(bool,AbortOnWeblogicMuxerError,false,\
"Abort when a Weblogic muxer warning or error occurs")\
                                                                            \
  /* Instruction tracing flags  */                                          \
                                                                            \
develop(bool,UseITR,false,\
"Turn instruction tracing on/off")\
                                                                            \
develop(bool,ITRWriteAnnotations,false,\
"Turns writing of annotation records on/off")\
                                                                            \
develop(bool,ITRBasicRecording,true,\
"Turns packaging and writing data to file on/off")\
                                                                            \
develop(bool,ITRRecordToFile,true,\
"Turns writing data to file on/off.  This is has no effect if ITR_basicRecording is off.")\
                                                                            \
develop(bool,ITRPrintDebug,false,\
"Print additional tracing debug info")\
                                                                            \
develop(bool,ITRHijackMemcpy,true,\
"Hijack memcpy for tracing")\
                                                                            \
develop(bool,ITRTraceSpaceID,true,\
"Trace space id bits in objectRef")\
                                                                            \
develop(bool,ITRPCInsteadOfVA,false,\
"Grab ld/st PC instead of VA")\
                                                                            \
develop(intx,ITRMaxThreads,35,\
"Max number of tracing threads that can be spawned")\
                                                                            \
develop(intx,ITRMinThreads,20,\
"Number of tracing threads spawned during VM init")\
                                                                            \
develop(intx,ITRNumArrays,500,\
"Max number of tracing buffers that can be created")\
                                                                            \
develop(intx,ITRArraySize,20,\
"lg of number of records per array")\
                                                                            \
develop(intx,ITRPrefLookahead,20,\
"number of words to prefetch ahead of the current write position in the trace array")\
                                                                            \
develop(uintx,ITRCollectionTimeStart,0,\
"Time [cycles] after execution when collection begins")\
                                                                            \
develop(intx,ITRCollectionTimeLimit,0,\
"Time [cycles] after execution when collection ends")\
                                                                            \
  develop(ccstr, ITROutputPath, "",                                         \
"Path where ITR traces will be placed")\
                                                                            \
develop(bool,ITRCloseOnDeactivation,false,\
"Close the ITR files when deactivation occurs")\
                                                                            \
  develop(ccstr, ITRTraceOnly, "",                                          \
"List of specific thread names to trace")\
                                                                            \
  /* temporary developer defined flags  */                                  \
                                                                            \
product(bool,UseNewCode,true,\
          "Testing Only: Use the new version while testing")                \
                                                                            \
  diagnostic(bool, UseNewCode2, false,                                      \
          "Testing Only: Use the new version while testing")                \
                                                                            \
  diagnostic(bool, UseNewCode3, false,                                      \
          "Testing Only: Use the new version while testing")                \
                                                                            \
develop(intx,UseNewLevel,0,\
"Testing Only: Use the new version while testing")\
                                                                            \
  /* flags for performance data collection */                               \
                                                                            \
product(bool,UsePerfData,false,\
          "Flag to disable jvmstat instrumentation for performance testing" \
          "and problem isolation purposes.")                                \
                                                                            \
  product(bool, PerfDataSaveToFile, false,                                  \
          "Save PerfData memory to hsperfdata_<pid> file on exit")          \
                                                                            \
  product(ccstr, PerfDataSaveFile, NULL,                                    \
          "Save PerfData memory to the specified absolute pathname,"        \
           "%p in the file name if present will be replaced by pid")        \
                                                                            \
  product(intx, PerfDataSamplingInterval, 50 /*ms*/,                        \
          "Data sampling interval in milliseconds")                         \
                                                                            \
  develop(bool, PerfTraceDataCreation, false,                               \
          "Trace creation of Performance Data Entries")                     \
                                                                            \
  develop(bool, PerfTraceMemOps, false,                                     \
          "Trace PerfMemory create/attach/detach calls")                    \
                                                                            \
product(bool,PerfDisableSharedMem,true,\
          "Store performance data in standard memory")                      \
                                                                            \
  product(intx, PerfDataMemorySize, 32*K,                                   \
          "Size of performance data memory region. Will be rounded "        \
          "up to a multiple of the native os page size.")                   \
                                                                            \
  product(intx, PerfMaxStringConstLength, 1024,                             \
          "Maximum PerfStringConstant string length before truncation")     \
                                                                            \
  product(bool, PerfAllowAtExitRegistration, false,                         \
          "Allow registration of atexit() methods")                         \
                                                                            \
  product(bool, PerfBypassFileSystemCheck, false,                           \
          "Bypass Win32 file system criteria checks (Windows Only)")        \
                                                                            \
  product(intx, UnguardOnExecutionViolation, 0,                             \
          "Unguard page and retry on no-execute fault (Win32 only)"         \
          "0=off, 1=conservative, 2=aggressive")                            \
                                                                            \
  /* Serviceability Support */                                              \
                                                                            \
  product(bool, ManagementServer, false,                                    \
          "Create JMX Management Server")                                   \
                                                                            \
  product(bool, DisableAttachMechanism, false,                              \
         "Disable mechanism that allows tools to attach to this VM")        \
                                                                            \
  product(bool, StartAttachListener, false,                                 \
          "Always start Attach Listener at VM startup")                     \
                                                                    	    \
  manageable(bool, PrintConcurrentLocks, false,                             \
          "Print java.util.concurrent locks in thread dump")                \
                                                                    	    \
  product(bool, RelaxAccessControlCheck, false,                             \
          "Relax the access control checks in the verifier")                \
                                                                            \
product(bool,RegisterWithARTA,true,\
"Export HotSpot specific data through ARTA.")\
                                                                            \
product(intx,ARTAPort,0,\
"Specify ARTAPort in non-proxied AVM.")\
                                                                            \
product(bool,ARTADebugging,false,\
"Turn on ARTA monitoring commands useful for VM debugging; "\
"also activates additional perfdata counters")\
                                                                            \
product(uintx,ARTADebugFlags,0,\
"Bitmask used to enable ARTA debugging options")\
                                                                            \
product(intx,ARTAJVMLockTimeout,1000,\
"Timeout in milliseconds before ARTA gives up on trying to "\
"acquire the JVM lock")\
                                                                            \
product(intx,ARTAStringPreviewLength,80,\
"Limit ARTA string preview to <N> Unicode characters")\
                                                                            \
product(intx,HTTPDaemonPort,0,\
"Deprecated - use \"-XX:ARTAPort=<N>\"")\
                                                                            \
product(ccstr,ARTALogLevel,"",\
"Set the log level for the internal ARTA HTTP server. Valid "\
"values are: debug, info, warn, error")\
                                                                            \
product(uintx,ARTAMaxResponseSizeMB,16,\
"Maximum http response size in MB (default = 16MB, min = 2MB)")\
                                                                            \
product(bool,WaitForDebugger,false,\
"Wait on debugger connection (to catch early bugs)")\
                                                                            \
product(bool,PrintBacktraceOnUnexpectedException,true,\
"Print a backtrace when an unexpected exception arises")\
                                                                            \
develop(bool,UseDebugLibrarySuffix,false,\
"Look for lib<foo>_g.so when trying to load libraries")\
                                                                            \
product(bool,OrigJavaUtilZip,true,\
"Use the original java.util.zip package")\
                                                                            \
product(bool,UseHighScaleLib,false,\
"Use high-scale-lib routines (HashMap, ConcurrentHashMap)")\
                                                                            \
product(bool,UseHighScaleLibHashtable,false,\
"Use high-scale-lib routines (Hashtable)")\
                                                                            \
product(bool,Log4J12Optimized,false,\
"Use the optimized log4j 1.2 package")\
                                                                            \
product(bool,UseLockedCollections,false,\
"Use synchronized collections (HashMap, ArrayList) ")\
                                                                            \
product(bool,UseCheckedCollections,true,\
"Common collections (HashMap, ArrayList) throw CMEs")\
                                                                            \
product(intx,ModifiedFreeMemory,0,\
"Use hardcoded value for java.lang.Runtime.freeMemory()")\
                                                                            \
product(bool,GConFDLimit,true,\
"Force GC cycles when the proxy is running out of fds")\
                                                                            \
product(uintx,GConFDLimitThreshold,75,\
"Threshold of open fds at which the GConFDLimit heuristic runs")\
                                                                            \
product(uintx,GConFDLimitScore,75,\
"Minimum score calculated by the heuristic to force a GC cycle")\
                                                                            \
product(uintx,GConFDLimitEmergency,95,\
"Threshold for emergency GC cycles to clean up fds")\
                                                                            \
product(bool,GConFDLimitVerbose,false,\
"Prints out information about the GConFDLimit heuristic")\
                                                                            \
product(bool,AbortOnOOM,false,\
"Cause core dump to occur when internal VM memory is exhausted")\
                                                                            \
product(bool,AllowDynamicCPUCount,false,\
"Allows the value returned by Runtime.availableProcessors() to "\
"fluctuate according to the OS's resource limits")\
                                                                            \
product(intx,AvailableProcessors,0,\
"Overrides value returned by Runtime.availableProcessors()")\
                                                                            \
product(intx,ProcessQuantumMS,-1,\
"Set process thread quantum in milliseconds")\
                                                                            \
product(bool,OverrideVMProperties,false,\
"Allow modifications to immutable VM properties")\
                                                                            \
product(intx,JavaThreadLocalMapInitialCapacity,16,\
"java.lang.ThreadLocal ThreadLocalMap INITIAL_CAPACITY")\
                                                                            \
product(bool,UseAznixSystemCTM,false,\
"Use enhanced azul System.currentTimeMillis")\
                                                                            \
develop(bool,UseCodeBlobSizeCheck,true,\
"Check size of code blobs")\
                                                                            \
  diagnostic(bool, SharedSkipVerify, false,                                 \
          "Skip assert() and verify() which page-in unwanted shared "       \
"objects. ")


/*
 *  Macros for factoring of globals
 */

// Interface macros
#define DECLARE_PRODUCT_FLAG(type, name, value, doc)    extern "C" type name;
#define DECLARE_PD_PRODUCT_FLAG(type, name, doc)        extern "C" type name;
#define DECLARE_DIAGNOSTIC_FLAG(type, name, value, doc) extern "C" type name;
#define DECLARE_MANAGEABLE_FLAG(type, name, value, doc) extern "C" type name;
#define DECLARE_PRODUCT_RW_FLAG(type, name, value, doc) extern "C" type name;
#ifdef PRODUCT
#define DECLARE_DEVELOPER_FLAG(type, name, value, doc)  const type name = value; 
#define DECLARE_PD_DEVELOPER_FLAG(type, name, doc)      const type name = pd_##name; 
#define DECLARE_NOTPRODUCT_FLAG(type, name, value, doc) 
#else
#define DECLARE_DEVELOPER_FLAG(type, name, value, doc)  extern "C" type name;
#define DECLARE_PD_DEVELOPER_FLAG(type, name, doc)      extern "C" type name;
#define DECLARE_NOTPRODUCT_FLAG(type, name, value, doc)  extern "C" type name;
#endif

// Implementation macros
#define MATERIALIZE_PRODUCT_FLAG(type, name, value, doc)   type name = value;
#define MATERIALIZE_PD_PRODUCT_FLAG(type, name, doc)       type name = pd_##name; 
#define MATERIALIZE_DIAGNOSTIC_FLAG(type, name, value, doc) type name = value;
#define MATERIALIZE_MANAGEABLE_FLAG(type, name, value, doc) type name = value; 
#define MATERIALIZE_PRODUCT_RW_FLAG(type, name, value, doc) type name = value;
#ifdef PRODUCT
#define MATERIALIZE_DEVELOPER_FLAG(type, name, value, doc) /* flag name is constant */ 
#define MATERIALIZE_PD_DEVELOPER_FLAG(type, name, doc)     /* flag name is constant */ 
#define MATERIALIZE_NOTPRODUCT_FLAG(type, name, value, doc) 
#else
#define MATERIALIZE_DEVELOPER_FLAG(type, name, value, doc) type name = value; 
#define MATERIALIZE_PD_DEVELOPER_FLAG(type, name, doc)     type name = pd_##name; 
#define MATERIALIZE_NOTPRODUCT_FLAG(type, name, value, doc) type name = value; 
#endif

#include "globals_os.hpp"
#include "globals_os_pd.hpp"
#include "globals_pd.hpp"

RUNTIME_FLAGS(DECLARE_DEVELOPER_FLAG, DECLARE_PD_DEVELOPER_FLAG, DECLARE_PRODUCT_FLAG, DECLARE_PD_PRODUCT_FLAG, DECLARE_DIAGNOSTIC_FLAG, DECLARE_NOTPRODUCT_FLAG, DECLARE_MANAGEABLE_FLAG, DECLARE_PRODUCT_RW_FLAG)

RUNTIME_OS_FLAGS(DECLARE_DEVELOPER_FLAG, DECLARE_PD_DEVELOPER_FLAG, DECLARE_PRODUCT_FLAG, DECLARE_PD_PRODUCT_FLAG, DECLARE_DIAGNOSTIC_FLAG, DECLARE_NOTPRODUCT_FLAG)

#endif // GLOBALS_HPP
