/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_GLOBALS_PD_HPP
#define C1_GLOBALS_PD_HPP


//
// Sets the default values for platform dependent flags used by the client compiler.
// (see c1_globals.hpp)
//

#ifndef TIERED
define_pd_global(bool, BackgroundCompilation,        true );
define_pd_global(bool, UseTLAB,                      true );
define_pd_global(bool, InlineIntrinsics,             true );
define_pd_global(bool, PreferInterpreterNativeStubs, false);
define_pd_global(bool, TieredCompilation,            false);
define_pd_global(intx, CompileThreshold,             1500 );
define_pd_global(intx, Tier2CompileThreshold,        1500 );
define_pd_global(intx, Tier3CompileThreshold,        2500 );
define_pd_global(intx, Tier4CompileThreshold,        4500 );

define_pd_global(intx, BackEdgeThreshold,            100000);
define_pd_global(intx, Tier2BackEdgeThreshold,       100000);
define_pd_global(intx, Tier3BackEdgeThreshold,       100000);
define_pd_global(intx, Tier4BackEdgeThreshold,       100000);

define_pd_global(intx, OnStackReplacePercentage,     933  );
define_pd_global(intx, FreqInlineSize,		     325  );
define_pd_global(intx, InitialCodeCacheSize,         160*K);
define_pd_global(intx, CodeCacheExpansionSize,       32*K );
define_pd_global(uintx,CodeCacheMinBlockLength,      1);
define_pd_global(bool, NeverActAsServerClassMachine, true);
define_pd_global(uintx, DefaultMaxRAM,		     1*G);
define_pd_global(bool, CICompileOSR,                 true );
#endif // TIERED
define_pd_global(bool, UseTypeProfile,               false); 


define_pd_global(bool, OptimizeSinglePrecision,      true);
define_pd_global(bool, CSEArrayLength,               false);
define_pd_global(bool, TwoOperandLIRForm,            true);


define_pd_global(intx, SafepointPollOffset, 256);

#endif // C1_GLOBALS_PD_HPP
