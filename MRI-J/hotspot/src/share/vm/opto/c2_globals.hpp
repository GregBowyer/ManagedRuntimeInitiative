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
#ifndef C2_GLOBALS_HPP
#define C2_GLOBALS_HPP

#include "globals.hpp"
#include "c2_globals_pd.hpp"

//
// Defines all globals flags used by the server compiler.
//

#define C2_FLAGS(develop, develop_pd, product, product_pd, diagnostic, notproduct)      \
                                                                            \
product(intx,C2CompileThreshold,10000,\
"Method invocations/branches before (re-)compiling")\
                                                                            \
product(intx,C2OnStackReplacePercentage,140,\
"number of method invocations/branches (expressed as %"\
"of CompileThreshold) before (re-)compiling OSR code")\
                                                                            \
product(intx,C2FreqInlineSize,175,\
"maximum bytecode size of a frequent method to be inlined")\
                                                                            \
product(bool,UseTypeProfile,true,\
"Check interpreter profile for historically monomorphic calls")\
                                                                            \
  product_pd(intx, InteriorEntryAlignment,                                  \
"Code alignment for interior entry points (in bytes)")\
                                                                            \
  product_pd(intx, OptoLoopAlignment,                                       \
          "Align inner loops to zero relative to this modulus")             \
                                                                            \
  notproduct(intx, IndexSetWatch, 0,                                        \
          "Trace all operations on this IndexSet (-1 means all, 0 none)")   \
                                                                            \
  develop(intx, OptoNodeListSize, 4,                                        \
          "Starting allocation size of Node_List data structures")          \
                                                                            \
  develop(intx, OptoBlockListSize, 8,                                       \
          "Starting allocation size of Block_List data structures")         \
                                                                            \
  develop(intx, OptoPeepholeAt, -1,                                         \
          "Apply peephole optimizations to this peephole rule")             \
                                                                            \
  notproduct(bool, PrintIdeal, false,                                       \
          "Print ideal graph before code generation")                       \
                                                                            \
product(bool,PrintOpto,false,\
          "Print compiler2 attempts")                                       \
                                                                            \
  notproduct(bool, PrintOptoInlining, false,                                \
          "Print compiler2 inlining decisions")                             \
                                                                            \
  notproduct(bool, VerifyOpto, false,                                       \
          "Apply more time consuming verification during compilation")      \
                                                                            \
  notproduct(bool, VerifyOptoOopOffsets, false,                             \
          "Check types of base addresses in field references")              \
                                                                            \
  develop(bool, IdealizedNumerics, false,                                   \
"Check performance difference allowing FP associativity and commutativity...")\
                                                                            \
  notproduct(bool, OptoBreakpointOSR, false,                                \
          "insert breakpoint at osr method entry")                          \
                                                                            \
  notproduct(intx, BreakAtNode, 0,                                          \
          "Break at construction of this Node (either _idx or _debug_idx)") \
                                                                            \
  notproduct(bool, OptoNoExecute, false,                                    \
          "Attempt to parse and compile but do not execute generated code") \
                                                                            \
product(bool,PrintOptoAssembly,false,\
          "Print New compiler assembly output")                             \
                                                                            \
  develop_pd(bool, OptoPeephole,                                            \
          "Apply peephole optimizations after register allocation")         \
                                                                            \
  notproduct(bool, PrintParseStatistics, false,                             \
          "Print nodes, transforms and new values made per bytecode parsed")\
                                                                            \
  notproduct(bool, PrintOptoPeephole, false,                                \
          "Print New compiler peephole replacements")                       \
                                                                            \
  develop(bool, PrintCFGBlockFreq, false,                                   \
          "Print CFG block freqencies")                                     \
                                                                            \
                                                                            \
  develop(bool, TraceOptoParse, false,                                      \
          "Trace bytecode parse and control-flow merge")                    \
                                                                            \
  product_pd(intx,  LoopUnrollLimit,                                        \
          "Unroll loop bodies with node count less than this")              \
                                                                            \
product(bool,DoPrefetch,true,\
"Insert prefetches in long running simple loops")\
                                                                            \
  product(intx,  LoopUnrollMin, 4,                                          \
          "Minimum number of unroll loop bodies before checking progress"   \
          "of rounds of unroll,optimize,..")                                \
                                                                            \
  develop(intx, UnrollLimitForProfileCheck, 1,                              \
	  "Don't use profile_trip_cnt() to restrict unrolling until "       \
	  "unrolling would push the number of unrolled iterations above "   \
          "UnrollLimitForProfileCheck. A higher value allows more "         \
          "unrolling. Zero acts as a very large value." )                   \
                                                                            \
  product(intx, MultiArrayExpandLimit, 6,                                   \
          "Maximum number of individual allocations in an inline-expanded " \
          "multianewarray instruction")                                     \
                                                                            \
  notproduct(bool, TraceProfileTripCount, false,                            \
          "Trace profile loop trip count information")                      \
                                                                            \
  develop(bool, OptoCoalesce, true,                                         \
          "Use Conservative Copy Coalescing in the Register Allocator")     \
                                                                            \
  develop(bool, UseUniqueSubclasses, true,                                  \
          "Narrow an abstract reference to the unique concrete subclass")   \
                                                                            \
  develop(bool, UseExactTypes, true,                                        \
          "Use exact types to eliminate array store checks and v-calls")    \
                                                                            \
  develop_pd(intx, RegisterCostAreaRatio,                                   \
          "Spill selection in reg allocator: scale area by (X/64K) before " \
          "adding cost")                                                    \
                                                                            \
product_pd(bool,UseCISCSpill,\
          "Use ADLC supplied cisc instructions during allocation")          \
                                                                            \
  notproduct(bool, VerifyGraphEdges , false,                                \
          "Verify Bi-directional Edges")                                    \
                                                                            \
  notproduct(bool, VerifyDUIterators, true,                                 \
          "Verify the safety of all iterations of Bi-directional Edges")    \
                                                                            \
  notproduct(bool, VerifyHashTableKeys, true,                               \
          "Verify the immutability of keys in the VN hash tables")          \
                                                                            \
  develop_pd(intx, FLOATPRESSURE,                                           \
          "Number of float LRG's that constitute high register pressure")   \
                                                                            \
  develop_pd(intx, INTPRESSURE,                                             \
          "Number of integer LRG's that constitute high register pressure") \
                                                                            \
  notproduct(bool, TraceOptoPipelining, false,                              \
          "Trace pipelining information")                                   \
                                                                            \
  notproduct(bool, TraceOptoOutput, false,                                  \
          "Trace pipelining information")                                   \
                                                                            \
product_pd(bool,OptoSchedulingPre,\
"Instruction Scheduling before register allocation")\
                                                                            \
product_pd(bool,OptoSchedulingPost,\
          "Instruction Scheduling after register allocation")               \
                                                                            \
  product(bool, PartialPeelLoop, true,                                      \
          "Partial peel (rotate) loops")                                    \
                                                                            \
  product(intx, PartialPeelNewPhiDelta, 0,                                  \
          "Additional phis that can be created by partial peeling")         \
                                                                            \
  notproduct(bool, TracePartialPeeling, false,                              \
          "Trace partial peeling (loop rotation) information")              \
                                                                            \
  product(bool, PartialPeelAtUnsignedTests, true,                           \
          "Partial peel at unsigned tests if no signed test exists")        \
                                                                            \
  product(bool, ReassociateInvariants, true,                                \
          "Enable reassociation of expressions with loop invariants.")      \
                                                                            \
  product(bool, LoopUnswitching, true,                                      \
          "Enable loop unswitching (a form of invariant test hoisting)")    \
                                                                            \
  notproduct(bool, TraceLoopUnswitching, false,                             \
          "Trace loop unswitching")                                         \
                                                                            \
  product(bool, UseSuperWord, true,                                         \
          "Transform scalar operations into superword operations")          \
                                                                            \
  develop(bool, SuperWordRTDepCheck, false,                                 \
          "Enable runtime dependency checks.")                              \
                                                                            \
  product(bool, TraceSuperWord, false,                                      \
          "Trace superword transforms")                                     \
                                                                            \
  product_pd(bool, OptoBundling,                                            \
          "Generate nops to fill i-cache lines")                            \
                                                                            \
  product_pd(intx, ConditionalMoveLimit,                                    \
          "Limit of ops to make speculative when using CMOVE")              \
                                                                            \
  product(bool, UseOldInlining, true,                                       \
          "Enable the 1.3 inlining strategy")                               \
                                                                            \
product(bool,InsertMemBarAfterArraycopy,false,\
          "Insert memory barrier after arraycopy call")                     \
                                                                            \
  product(bool, UseBimorphicInlining, true,                                 \
          "Profiling based inlining for two receivers")                     \
                                                                            \
product(bool,InlineFastPathLocking,true,\
"inline a fast/slow-path locking")\
                                                                            \
  product(intx, LoopOptsCount, 43,                                          \
"Set level of loop optimization")\
                                                                            \
  /* controls for heat-based inlining */                                    \
                                                                            \
  develop(intx, NodeCountInliningCutoff, 18000,                             \
          "If parser node generation exceeds limit stop inlining")          \
                                                                            \
  develop(intx, NodeCountInliningStep, 1000,                                \
          "Target size of warm calls inlined between optimization passes")  \
                                                                            \
  develop(bool, InlineWarmCalls, false,                                     \
          "Use a heat-based priority queue to govern inlining")             \
                                                                            \
  develop(intx, HotCallCountThreshold, 999999,                              \
          "large numbers of calls (per method invocation) force hotness")   \
                                                                            \
  develop(intx, HotCallProfitThreshold, 999999,                             \
          "highly profitable inlining opportunities force hotness")         \
                                                                            \
  develop(intx, HotCallTrivialWork, -1,                                     \
          "trivial execution time (no larger than this) forces hotness")    \
                                                                            \
  develop(intx, HotCallTrivialSize, -1,                                     \
          "trivial methods (no larger than this) force calls to be hot")    \
                                                                            \
  develop(intx, WarmCallMinCount, -1,                                       \
          "number of calls (per method invocation) to enable inlining")     \
                                                                            \
  develop(intx, WarmCallMinProfit, -1,                                      \
          "number of calls (per method invocation) to enable inlining")     \
                                                                            \
  develop(intx, WarmCallMaxWork, 999999,                                    \
          "execution time of the largest inlinable method")                 \
                                                                            \
  develop(intx, WarmCallMaxSize, 999999,                                    \
          "size of the largest inlinable method")                           \
                                                                            \
product(intx,MaxNodeLimit,100000,\
          "Maximum number of nodes")                                        \
                                                                            \
  product(intx, NodeLimitFudgeFactor, 1000,                                 \
          "Fudge Factor for certain optimizations")                         \
                                                                            \
  product(bool, UseJumpTables, true,                                        \
          "Use JumpTables instead of a binary search tree for switches")    \
                                                                            \
  product(bool, UseDivMod, true,                                            \
          "Use combined DivMod instruction if available")                   \
                                                                            \
  product(intx, MinJumpTableSize, 18,                                       \
          "Minimum number of targets in a generated jump table")            \
                                                                            \
  product(intx, MaxJumpTableSize, 65000,                                    \
          "Maximum number of targets in a generated jump table")            \
                                                                            \
  product(intx, MaxJumpTableSparseness, 5,                                  \
          "Maximum sparseness for jumptables")                              \
                                                                            \
product(bool,CoarsenLocks,true,\
          "Coarsen locks when possible")                                    \
                                                                            \
  notproduct(bool, PrintLockStatistics, false,                              \
          "Print precise statistics on the dynamic lock usage")             \
                                                                            \
notproduct(bool,PrintCoarsenLocks,false,\
          "Print out when locks are eliminated")                            \
                                                                            \
  product(bool, DoEscapeAnalysis, false,                                    \
          "Perform escape analysis")                                        \
                                                                            \
  notproduct(bool, PrintEscapeAnalysis, false,                              \
          "Print the results of escape analysis")                           \
                                                                            \
product(bool,EliminateAutoBox,true,\
"Private flag to control optimizations for autobox elimination")\
                                                                            \
  product(intx, AutoBoxCacheMax, 128,                                       \
          "Sets max value cached by the java.lang.Integer autobox cache")   \
                                                                            \
  product(intx, MaxLabelRootDepth, 1100, 				    \
          "Maximum times call Label_Root to prevent stack overflow")        \
                                                                            \
product(bool,UncommonTrapDeadBlocks,true,\
"Make blocks not taken uncommon traps")\
                                                                            \
product(bool,C2Breakpoint,false,\
"Sets a breakpoint at entry of each compiled method")\
                                                                            \
  /* Azul-specific */                                                       \
product(bool,KeepSafepointsInCountedLoops,false,\
"Counted loops keep their safepoints on backedges")\
                                                                            \

C2_FLAGS(DECLARE_DEVELOPER_FLAG, DECLARE_PD_DEVELOPER_FLAG, DECLARE_PRODUCT_FLAG, DECLARE_PD_PRODUCT_FLAG, DECLARE_DIAGNOSTIC_FLAG, DECLARE_NOTPRODUCT_FLAG)

#endif // C2_GLOBALS_HPP
