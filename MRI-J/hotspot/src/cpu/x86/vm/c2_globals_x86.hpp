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
#ifndef C2_GLOBALS_PD_HPP
#define C2_GLOBALS_PD_HPP



//
// Sets the default values for platform dependent flags used by the server compiler.
// (see c2_globals.hpp).  Alpha-sorted.

define_pd_global(bool, ProfileTraps,                 true);
define_pd_global(bool,StagedCompilation,false);
define_pd_global(intx,Stage2CompileThreshold,15000);
define_pd_global(intx,ConditionalMoveLimit,2);
define_pd_global(intx,FLOATPRESSURE,14);
define_pd_global(intx,INTPRESSURE,14);
define_pd_global(intx, InteriorEntryAlignment,       16);  // = CodeEntryAlignment
define_pd_global(intx,OptoLoopAlignment,8);//= 2*wordSize
define_pd_global(intx, RegisterCostAreaRatio,	     12000);
define_pd_global(intx, LoopUnrollLimit,	     	     50); // Design center runs on 1.3.1

// Peephole and CISC spilling both break the graph, and so makes the
// scheduler sick.
define_pd_global(bool, OptoPeephole,                 true );
define_pd_global(bool, UseCISCSpill,                 true );
define_pd_global(bool, OptoBundling,                 false);
define_pd_global(bool,OptoSchedulingPre,true);
define_pd_global(bool,OptoSchedulingPost,false);

define_pd_global(intx,CodeCacheMinimumFreeSpace,3500*K);

#endif // C2_GLOBALS_PD_HPP

