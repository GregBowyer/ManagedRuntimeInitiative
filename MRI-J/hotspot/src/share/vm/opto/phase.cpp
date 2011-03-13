/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "compile.hpp"
#include "compileBroker.hpp"
#include "phase.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

#ifndef PRODUCT
int Phase::_total_bytes_compiled = 0;

elapsedTimer Phase::_t_totalCompilation;
elapsedTimer Phase::_t_methodCompilation;
#endif

// The next timers used for LogCompilation
elapsedTimer Phase::_t_parser;
elapsedTimer Phase::_t_optimizer;
elapsedTimer   Phase::_t_idealLoop;
elapsedTimer   Phase::_t_ccp;
elapsedTimer Phase::_t_matcher;
elapsedTimer Phase::_t_registerAllocation;
elapsedTimer Phase::_t_output;

#ifndef PRODUCT
elapsedTimer Phase::_t_graphReshaping;
elapsedTimer Phase::_t_scheduler;
elapsedTimer Phase::_t_removeEmptyBlocks;
elapsedTimer Phase::_t_macroExpand;
elapsedTimer Phase::_t_peephole;
elapsedTimer Phase::_t_codeGeneration;
elapsedTimer Phase::_t_registerMethod;
elapsedTimer Phase::_t_temporaryTimer1;
elapsedTimer Phase::_t_temporaryTimer2;

// Subtimers for _t_optimizer 
elapsedTimer   Phase::_t_iterGVN;
elapsedTimer   Phase::_t_iterGVN2;

// Subtimers for _t_registerAllocation 
elapsedTimer   Phase::_t_ctorChaitin;
elapsedTimer   Phase::_t_buildIFGphysical;
elapsedTimer   Phase::_t_computeLive;
elapsedTimer   Phase::_t_regAllocSplit;
elapsedTimer   Phase::_t_postAllocCopyRemoval;
elapsedTimer   Phase::_t_fixupSpills;

// Subtimers for _t_output 
elapsedTimer   Phase::_t_instrSched;
elapsedTimer   Phase::_t_buildOopMaps;
#endif

//------------------------------Phase------------------------------------------
Phase::Phase( PhaseNumber pnum ) : _pnum(pnum), C( pnum == Compiler ? NULL : Compile::current()) { 
  // Poll for requests from shutdown mechanism to quiesce comiler (4448539, 4448544).
  // This is an effective place to poll, since the compiler is full of phases.
  // In particular, every inlining site uses a recursively created Parse phase.
  CompileBroker::maybe_block();
}

#ifndef PRODUCT
static const double minimum_reported_time             = 0.0001; // seconds
static const double expected_method_compile_coverage  = 0.97;   // %
static const double minimum_meaningful_method_compile = 2.00;   // seconds

void Phase::print_timers() {
C2OUT->print_cr("Accumulated compiler times:");
C2OUT->print_cr("---------------------------");
C2OUT->print_cr("  Total compilation: %3.3f sec.",Phase::_t_totalCompilation.seconds());
C2OUT->print("    method compilation : %3.3f sec",Phase::_t_methodCompilation.seconds());
C2OUT->print("/%d bytes",_total_bytes_compiled);
C2OUT->print_cr(" (%3.0f bytes per sec) ",Phase::_total_bytes_compiled/Phase::_t_methodCompilation.seconds());
C2OUT->print_cr("  Phases:");
C2OUT->print_cr("    parse        : %3.3f sec",Phase::_t_parser.seconds());
C2OUT->print_cr("    optimizer    : %3.3f sec",Phase::_t_optimizer.seconds());
C2OUT->print_cr("      iterGVN      : %3.3f sec",Phase::_t_iterGVN.seconds());
C2OUT->print_cr("      idealLoop    : %3.3f sec",Phase::_t_idealLoop.seconds());
C2OUT->print_cr("      ccp          : %3.3f sec",Phase::_t_ccp.seconds());
C2OUT->print_cr("      iterGVN2     : %3.3f sec",Phase::_t_iterGVN2.seconds());
C2OUT->print_cr("      graphReshape : %3.3f sec",Phase::_t_graphReshaping.seconds());
  double optimizer_subtotal = Phase::_t_iterGVN.seconds() + 
    Phase::_t_idealLoop.seconds() + Phase::_t_ccp.seconds() + 
    Phase::_t_graphReshaping.seconds();
  double percent_of_optimizer = ((optimizer_subtotal == 0.0) ? 0.0 : (optimizer_subtotal / Phase::_t_optimizer.seconds() * 100.0));
C2OUT->print_cr("      subtotal     : %3.3f sec,  %3.2f %%",optimizer_subtotal,percent_of_optimizer);
C2OUT->print_cr("    matcher      : %3.3f sec",Phase::_t_matcher.seconds());
C2OUT->print_cr("    scheduler    : %3.3f sec",Phase::_t_scheduler.seconds());
C2OUT->print_cr("    regalloc     : %3.3f sec",Phase::_t_registerAllocation.seconds());
C2OUT->print_cr("      ctorChaitin  : %3.3f sec",Phase::_t_ctorChaitin.seconds());
C2OUT->print_cr("      buildIFG     : %3.3f sec",Phase::_t_buildIFGphysical.seconds());
C2OUT->print_cr("      computeLive  : %3.3f sec",Phase::_t_computeLive.seconds());
C2OUT->print_cr("      regAllocSplit: %3.3f sec",Phase::_t_regAllocSplit.seconds());
C2OUT->print_cr("      postAllocCopyRemoval: %3.3f sec",Phase::_t_postAllocCopyRemoval.seconds());
C2OUT->print_cr("      fixupSpills  : %3.3f sec",Phase::_t_fixupSpills.seconds());
  double regalloc_subtotal = Phase::_t_ctorChaitin.seconds() + 
    Phase::_t_buildIFGphysical.seconds() + Phase::_t_computeLive.seconds() + 
    Phase::_t_regAllocSplit.seconds()    + Phase::_t_fixupSpills.seconds() +
    Phase::_t_postAllocCopyRemoval.seconds();
  double percent_of_regalloc = ((regalloc_subtotal == 0.0) ? 0.0 : (regalloc_subtotal / Phase::_t_registerAllocation.seconds() * 100.0));
C2OUT->print_cr("      subtotal     : %3.3f sec,  %3.2f %%",regalloc_subtotal,percent_of_regalloc);
C2OUT->print_cr("    macroExpand  : %3.3f sec",Phase::_t_macroExpand.seconds());
C2OUT->print_cr("    removeEmpty  : %3.3f sec",Phase::_t_removeEmptyBlocks.seconds());
C2OUT->print_cr("    peephole     : %3.3f sec",Phase::_t_peephole.seconds());
C2OUT->print_cr("    codeGen      : %3.3f sec",Phase::_t_codeGeneration.seconds());
C2OUT->print_cr("    install_code : %3.3f sec",Phase::_t_registerMethod.seconds());
C2OUT->print_cr("    ------------ : ----------");
  double phase_subtotal = Phase::_t_parser.seconds() + 
    Phase::_t_optimizer.seconds() + Phase::_t_graphReshaping.seconds() + 
    Phase::_t_matcher.seconds() + Phase::_t_scheduler.seconds() + 
    Phase::_t_registerAllocation.seconds() + Phase::_t_removeEmptyBlocks.seconds() +
    Phase::_t_macroExpand.seconds() + Phase::_t_peephole.seconds() + 
    Phase::_t_codeGeneration.seconds() + Phase::_t_registerMethod.seconds();
  double percent_of_method_compile = ((phase_subtotal == 0.0) ? 0.0 : phase_subtotal / Phase::_t_methodCompilation.seconds()) * 100.0;
  // counters inside Compile::CodeGen include time for adapters and stubs
  // so phase-total can be greater than 100%
C2OUT->print_cr("    total        : %3.3f sec,  %3.2f %%",phase_subtotal,percent_of_method_compile);

  assert( percent_of_method_compile > expected_method_compile_coverage || 
          phase_subtotal < minimum_meaningful_method_compile, 
          "Must account for method compilation");

C2OUT->print_cr("    output    : %3.3f sec",Phase::_t_output.seconds());
C2OUT->print_cr("      isched    : %3.3f sec",Phase::_t_instrSched.seconds());
C2OUT->print_cr("      bldOopMaps: %3.3f sec",Phase::_t_buildOopMaps.seconds());
  if( Phase::_t_temporaryTimer1.seconds() > minimum_reported_time ) {
C2OUT->cr();
C2OUT->print_cr("    temporaryTimer1: %3.3f sec",Phase::_t_temporaryTimer1.seconds());
  }
  if( Phase::_t_temporaryTimer2.seconds() > minimum_reported_time ) {
C2OUT->cr();
C2OUT->print_cr("    temporaryTimer2: %3.3f sec",Phase::_t_temporaryTimer2.seconds());
  }
}
#endif
