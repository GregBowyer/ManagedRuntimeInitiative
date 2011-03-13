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


#include "codeCache.hpp" 
#include "collectedHeap.hpp" 
#include "genCollectedHeap.hpp" 
#include "init.hpp" 
#include "interfaceSupport.hpp" 
#include "ostream.hpp" 
#include "psMarkSweep.hpp" 
#include "psParallelCompact.hpp" 
#include "stubRoutines.hpp" 
#include "universe.hpp" 
#include "vframe.hpp" 

#include "allocation.inline.hpp"
#include "frame.inline.hpp"
#include "oop.inline.hpp"

// Implementation of InterfaceSupport

#ifdef ASSERT

long InterfaceSupport::_number_of_calls       = 0;
long InterfaceSupport::_scavenge_alot_counter = 1;
long InterfaceSupport::_fullgc_alot_counter   = 1;
long InterfaceSupport::_fullgc_alot_invocation = 0;

void InterfaceSupport::trace(const char* result_type, const char* func, const char* header) {
  tty->print_cr("%6ld  %s%s", _number_of_calls, func, header);
}

void InterfaceSupport::gc_alot() {
  if( !StubRoutines::safepoint_trap_handler() ) return; // vm init order
  Thread *thread = Thread::current();
  if (thread->is_VM_thread()) return; // Avoid concurrent calls
  // Check for new, not quite initialized thread. A thread in new mode cannot initiate a GC.
  JavaThread *current_thread = (JavaThread *)thread;
  if (current_thread->active_handles() == NULL) return; 

  if (is_init_completed()) {

    if (++_fullgc_alot_invocation < FullGCALotStart) {
      return;
    }

    // Use this line if you want to block at a specific point,
    // e.g. one number_of_calls/scavenge/gc before you got into problems
    if (FullGCALot) _fullgc_alot_counter--;

    // Check if we should force a full gc
    if (_fullgc_alot_counter == 0) {
      // Release dummy so objects are forced to move
      if (!Universe::release_fullgc_alot_dummy()) {
        warning("FullGCALot: Unable to release more dummies at bottom of heap");
      }
      HandleMark hm(thread);
      Universe::heap()->collect(GCCause::_full_gc_alot);

      int invocations = 0;
      // Gack. This is ugly. It would be better to refactor all this stuff in some way,
      // maybe delegating the entire decision to the heap...
if(Universe::heap()->kind()==CollectedHeap::ParallelScavengeHeap){
        if (UseParallelOldGC) {
          invocations = PSParallelCompact::total_invocations();
        } else {
invocations=PSMarkSweep::total_invocations();
        }
      } else if (Universe::heap()->kind() == CollectedHeap::GenCollectedHeap) {
        invocations = GenCollectedHeap::heap()->perm_gen()->stat_record()->invocations;
      } else {
        // It seems likely Pauseless will never use FullGCALot/ScavengeALot
guarantee(false,"Unsupported heap type");
      }

      // Compute new interval
      if (FullGCALotInterval > 1) {
        _fullgc_alot_counter = 1+(long)((double)FullGCALotInterval*os::random()/(max_jint+1.0));
        if (PrintGCDetails && Verbose) {
tty->print_cr("Full gc no: %u\tInterval: %ld",invocations,
                        _fullgc_alot_counter);
        }
      } else {
        _fullgc_alot_counter = 1;
      }
      // Print progress message
      if (invocations % 100 == 0) {
        if (PrintGCDetails && Verbose) tty->print_cr("Full gc no: %u", invocations);
      }
    } else {
      if (ScavengeALot) _scavenge_alot_counter--;
      // Check if we should force a scavenge
      if (_scavenge_alot_counter == 0) {
        HandleMark hm(thread);
        Universe::heap()->collect(GCCause::_scavenge_alot);
        unsigned int invocations = Universe::heap()->total_collections() - Universe::heap()->total_full_collections();
        // Compute new interval
        if (ScavengeALotInterval > 1) {
          _scavenge_alot_counter = 1+(long)((double)ScavengeALotInterval*os::random()/(max_jint+1.0));
          if (PrintGCDetails && Verbose) {
tty->print_cr("Scavenge no: %u\tInterval: %ld",invocations,
                          _scavenge_alot_counter);
          }
        } else {
          _scavenge_alot_counter = 1;
        }
        // Print progress message
        if (invocations % 1000 == 0) {
          if (PrintGCDetails && Verbose) tty->print_cr("Scavenge no: %u", invocations);
        }
      }
    }
  }
}


void InterfaceSupport::walk_stack() {
  JavaThread* thread = JavaThread::current();
  SBAArea *sba = thread->sba_area();
  if( sba ) sba->verify();
  int i = 0;
  for( vframe vf(thread); !vf.done(); vf.next() ) {
    if( ++i > 50 ) break;       // Cutoff deep walks
  }
}


void InterfaceSupport::stress_derived_pointers() {
  JavaThread* thread = JavaThread::current();
  if (!is_init_completed()) return;
  ResourceMark rm(thread);
  Unimplemented();
  // FIXME -- reimplement for x86...
}


void InterfaceSupport::verify_stack() {
  JavaThread* thread = JavaThread::current();
  ResourceMark rm(thread);
  // disabled because it throws warnings that oop maps should only be accessed
  // in VM thread or during debugging
  
  if (!thread->has_pending_exception()) {
    // verification does not work if there are pending exceptions
    StackFrameStream sfs(thread);  
    // FIXME - Check that the CodeBlob for this frame is "reasonable"
    for (; !sfs.is_done(); sfs.next()) {
sfs.current()->verify(thread);
    }
  }
}


void InterfaceSupport::verify_last_frame() {
  JavaThread* thread = JavaThread::current();
  ResourceMark rm(thread);
  frame fr = thread->last_frame();
fr.verify(thread);
}


#endif // ASSERT


void InterfaceSupport_init() {
#ifdef ASSERT
  if (ScavengeALot || FullGCALot) {
    srand(ScavengeALotInterval * FullGCALotInterval);
  }
#endif
}

