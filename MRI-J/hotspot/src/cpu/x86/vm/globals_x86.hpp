/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GLOBALS_PD_HPP
#define GLOBALS_PD_HPP

#include "os/thread.h"



#include "c1_globals_pd.hpp"
#include "c2_globals_pd.hpp"

//
// Sets the default values for platform dependent flags used by the runtime system.
// (see globals.hpp)
//

define_pd_global(bool,  DontYieldALot,               true);  // yield no more than 100 times per second
define_pd_global(bool,  ProfileInterpreter,          false); // Not implemented

define_pd_global(bool,  ImplicitNullChecks,          true);  // Generate code for implicit null checks
define_pd_global(bool,  UncommonNullCast,            true);  // Uncommon-trap NULLs past to check cast

define_pd_global(intx,  CodeEntryAlignment,    16);
// Step size bytes between executing prefetch instructions (>=32).
// TODO: compute using CPUID
define_pd_global(uintx, PrefetchStride, BytesPerCacheLine);
// The distance ahead (in bytes) of the start of an allocated object prefetched
// to ensure initializing a mark word doesn't stall
define_pd_global(uintx,TLABPrefetchSize,1024);
// A region zeroed after an allocated object, zeroed to remove a dependence
// between zeroing and accessing. NB. if the cache line at the top of the TLAB
// is only partially full then it will be filled with zeroes
define_pd_global(uintx,TLABZeroRegion,64);
define_pd_global(uintx, NewSize, ScaleForWordSize((2048 * K) + (2 * (64 * K))));
define_pd_global(intx,  SurvivorRatio,         8);
define_pd_global(intx,InlineFrequencyCount,350);//Callsites with more counts get inlined up to the larger C2FreqInlineSize

define_pd_global(intx,StackYellowPages,1);

define_pd_global(intx,PreInflateSpin,400);//Determined by running design center

define_pd_global(intx,  PrefetchCopyIntervalInBytes, 512);	// Values from UltraSparc 3
define_pd_global(intx,PrefetchScanIntervalInBytes,256);
define_pd_global(intx,PrefetchFieldsAhead,1);

define_pd_global(bool,  UseSBA,                      false);  // FIXME Default to true once it works...
define_pd_global(bool,UseSMA,false);

define_pd_global(bool,ResizeTLAB,true);
define_pd_global(bool,  RewriteBytecodes,         true );
define_pd_global(bool,  RewriteFrequentPairs,     true );
define_pd_global(bool,  UseOnStackReplacement,    false);
define_pd_global(uintx, TLABSize,                    0 );
define_pd_global(intx,NewRatio,2);
define_pd_global(intx,  InlineClassNatives,       true );
define_pd_global(intx,  InlineUnsafeOps,          true );
define_pd_global(intx,NewSizeThreadIncrease,4*K);
define_pd_global(uintx,PermSize,12*M);
define_pd_global(uintx,MaxPermSize,128*M);
define_pd_global(intx,ReservedCodeCacheSize,128*M);

define_pd_global(bool,UseTickProfiler,true);

#endif // GLOBALS_PD_HPP
