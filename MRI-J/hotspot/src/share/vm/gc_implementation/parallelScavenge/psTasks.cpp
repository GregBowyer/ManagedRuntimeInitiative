/*
 * Copyright 2002-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "artaObjects.hpp"
#include "cardTableExtension.hpp"
#include "collectedHeap.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "management.hpp"
#include "psOldGen.hpp"
#include "psPromotionManager.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.hpp"
#include "psTasks.hpp"
#include "referenceProcessor.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "oop.psgc.inline.hpp"
#include "psScavenge.inline.hpp"
#include "stackRef_pd.inline.hpp"

//
// ScavengeRootsTask
//

// Define before usep
class PSScavengeRootsClosure: public OopClosure {
 private:
  PSPromotionManager* _promotion_manager;

 public:
  PSScavengeRootsClosure(PSPromotionManager* pm) : _promotion_manager(pm) { }

  void do_oop(objectRef* p) {
    assert(objectRef::is_null_or_heap(p), "must be heapRef");
    do_ref((heapRef*)p);
  }

void do_ref(heapRef*p){
    if (PSScavenge::should_scavenge(UNPOISON_OBJECTREF(*p,p))) {
      // We never card mark roots, maybe call a func without test?
      PSScavenge::copy_and_push_safe_barrier(_promotion_manager, p);
    }
  }
};

void ScavengeRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  PSScavengeRootsClosure roots_closure(pm);
  
  switch (_root_type) {
    case universe:
      Universe::oops_do(&roots_closure);
      ReferenceProcessor::oops_do(&roots_closure);
      break;

    case jni_handles:
      JNIHandles::oops_do(&roots_closure);
      break;

    case object_synchronizer:
      ObjectSynchronizer::oops_do(&roots_closure);
      break;

    case system_dictionary:
      SystemDictionary::oops_do(&roots_closure);
      break;

    case arta_objects:
      // Only strong roots during scavenge!
ArtaObjects::oops_do(&roots_closure);
      break;

    case management:
      Management::oops_do(&roots_closure);
      break;

    case jvmti:
      JvmtiExport::oops_do(&roots_closure);
      break;

    default:
      fatal("Unknown root type");
  }

  // Do the real work
  pm->drain_stacks(false);
}

//
// ThreadRootsTask
//

void ThreadRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
  PSScavengeRootsClosure roots_closure(pm);
  
  if (_java_thread != NULL)
    _java_thread->oops_do(&roots_closure);

  if (_vm_thread != NULL)
    _vm_thread->oops_do(&roots_closure);

  // Do the real work
  pm->drain_stacks(false);
}

//
// StealTask
//

StealTask::StealTask(ParallelTaskTerminator* t) :
  _terminator(t) {}

void StealTask::do_it(GCTaskManager* manager, uint which) {
  assert(Universe::heap()->is_gc_active(), "called outside gc");

  PSPromotionManager* pm = 
    PSPromotionManager::gc_thread_promotion_manager(which);
  pm->drain_stacks(true);
  guarantee(pm->stacks_empty(),
            "stacks should be empty at this point");

  int random_seed = 17;
  if (pm->depth_first()) {
    while(true) {
heapRef*p=0;
      if (PSPromotionManager::steal_depth(which, &random_seed, p)) {
  pm->process_popped_location_depth(p);
	pm->drain_stacks_depth(true);
      } else {
	if (terminator()->offer_termination()) {
	  break;
	}
      }
    }
  } else {
    while(true) {
oop obj=0;
      if (PSPromotionManager::steal_breadth(which, &random_seed, obj)) {
	obj->copy_contents(pm);
	pm->drain_stacks_breadth(true);
      } else {
	if (terminator()->offer_termination()) {
	  break;
	}
      }
    }
  }
  guarantee(pm->stacks_empty(),
            "stacks should be empty at this point");
}


//
// OldToYoungRootsTask
//

void OldToYoungRootsTask::do_it(GCTaskManager* manager, uint which) {
  assert(_gen != NULL, "Sanity");
  assert(_gen->object_space()->contains(_gen_top) || _gen_top == _gen->object_space()->top(), "Sanity");
  assert(_stripe_number < ParallelGCThreads, "Sanity");

  { 
    PSPromotionManager* pm = PSPromotionManager::gc_thread_promotion_manager(which);
    
    assert(Universe::heap()->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
    CardTableExtension* card_table = (CardTableExtension *)Universe::heap()->barrier_set();
    // FIX ME! Assert that card_table is the type we believe it to be.
    
    card_table->scavenge_contents_parallel(_gen->start_array(),
                                           _gen->object_space(),
                                           _gen_top,
                                           pm,
                                           _stripe_number);
    
    // Do the real work
    pm->drain_stacks(false);
  }
}


