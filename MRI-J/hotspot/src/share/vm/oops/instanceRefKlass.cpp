/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "cardTableExtension.hpp"
#include "collectedHeap.hpp"
#include "genCollectedHeap.hpp"
#include "instanceRefKlass.hpp"
#include "javaClasses.hpp"
#include "markSweep.hpp"
#include "ostream.hpp"
#include "preserveException.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "psScavenge.hpp"
#include "referenceProcessor.hpp"
#include "surrogateLockerThread.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "universe.inline.hpp"

int instanceRefKlass::oop_adjust_pointers(oop obj) {
  int size = size_helper();
  instanceKlass::oop_adjust_pointers(obj);
  
  heapRef* referent_addr = (heapRef*)java_lang_ref_Reference::referent_addr(obj);
  assert0(objectRef::is_null_or_heap(referent_addr));
  MarkSweep::adjust_pointer(referent_addr);

  heapRef* next_addr = (heapRef*)java_lang_ref_Reference::next_addr(obj);
  assert0(objectRef::is_null_or_heap(next_addr));
  MarkSweep::adjust_pointer(next_addr);

  heapRef* discovered_addr = (heapRef*)java_lang_ref_Reference::discovered_addr(obj);
  assert0(objectRef::is_null_or_heap(discovered_addr));
  MarkSweep::adjust_pointer(discovered_addr);

#ifdef ASSERT
  if(TraceReferenceGC && PrintGCDetails) {
    gclog_or_tty->print_cr("instanceRefKlass::oop_adjust_pointers obj "
PTR_FORMAT,(address)obj);
gclog_or_tty->print_cr("     referent_addr/* "PTR_FORMAT" / "
PTR_FORMAT,referent_addr,
			   referent_addr ? ALWAYS_UNPOISON_OBJECTREF(*referent_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     next_addr/* "PTR_FORMAT" / "
PTR_FORMAT,next_addr,
			   next_addr ? ALWAYS_UNPOISON_OBJECTREF(*next_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     discovered_addr/* "PTR_FORMAT" / "
PTR_FORMAT,discovered_addr,
			   discovered_addr ? ALWAYS_UNPOISON_OBJECTREF(*discovered_addr).as_oop() : NULL);
  }
#endif

  return size;
}

#define InstanceRefKlass_OOP_OOP_ITERATE_DEFN(OopClosureType, nv_suffix)        \
                                                                                \
int instanceRefKlass::                                                          \
oop_oop_iterate##nv_suffix(oop obj, OopClosureType* closure) {                  \
  /* Get size before changing pointers */                                       \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::irk);\
                                                                                \
  int size = instanceKlass::oop_oop_iterate##nv_suffix(obj, closure);           \
                                                                                \
  objectRef* referent_addr = java_lang_ref_Reference::referent_addr(obj);       \
oop referent=ALWAYS_UNPOISON_OBJECTREF(*referent_addr).as_oop();\
  if (referent != NULL) {                                                       \
    ReferenceProcessor* rp = closure->_ref_processor;                           \
if(!UseGenPauselessGC&&!referent->is_gc_marked()&&\
(rp!=NULL)&&rp->discover_reference(obj,reference_type())){\
      return size;                                                              \
    } else {                                                                    \
      /* treat referent as normal oop */                                        \
      SpecializationStats::record_do_oop_call##nv_suffix(SpecializationStats::irk);\
      closure->do_oop##nv_suffix(referent_addr);                                \
    }                                                                           \
  }                                                                             \
                                                                                \
  /* treat next as normal oop */                                                \
  objectRef* next_addr = java_lang_ref_Reference::next_addr(obj);               \
  SpecializationStats::record_do_oop_call##nv_suffix(SpecializationStats::irk); \
  closure->do_oop##nv_suffix(next_addr);                                        \
  return size;                                                                  \
}

#define InstanceRefKlass_OOP_OOP_ITERATE_DEFN_m(OopClosureType, nv_suffix)      \
                                                                                \
int instanceRefKlass::                                                          \
oop_oop_iterate##nv_suffix##_m(oop obj,                                         \
                               OopClosureType* closure,                         \
                               MemRegion mr) {                                  \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::irk);\
                                                                                \
  int size = instanceKlass::oop_oop_iterate##nv_suffix##_m(obj, closure, mr);   \
                                                                                \
  objectRef* referent_addr = java_lang_ref_Reference::referent_addr(obj);       \
oop referent=ALWAYS_UNPOISON_OBJECTREF(*referent_addr).as_oop();\
  if (referent != NULL && mr.contains(referent_addr)) {                         \
    ReferenceProcessor* rp = closure->_ref_processor;                           \
if(!UseGenPauselessGC&&!referent->is_gc_marked()&&\
(rp!=NULL)&&rp->discover_reference(obj,reference_type())){\
      return size;                                                              \
    } else {                                                                    \
      /* treat referent as normal oop */                                        \
      SpecializationStats::record_do_oop_call##nv_suffix(SpecializationStats::irk);\
      closure->do_oop##nv_suffix(referent_addr);                                \
    }                                                                           \
  }                                                                             \
                                                                                \
  /* treat next as normal oop */                                                \
  objectRef* next_addr = java_lang_ref_Reference::next_addr(obj);               \
  if (mr.contains(next_addr)) {                                                 \
    SpecializationStats::record_do_oop_call##nv_suffix(SpecializationStats::irk);\
    closure->do_oop##nv_suffix(next_addr);                                      \
  }                                                                             \
  return size;                                                                  \
}

ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceRefKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_3(InstanceRefKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceRefKlass_OOP_OOP_ITERATE_DEFN_m)
ALL_OOP_OOP_ITERATE_CLOSURES_3(InstanceRefKlass_OOP_OOP_ITERATE_DEFN_m)


void instanceRefKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(!pm->depth_first(), "invariant");
heapRef*referent_addr=(heapRef*)java_lang_ref_Reference::referent_addr(obj);
assert0(objectRef::is_null_or_heap(referent_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*referent_addr))) {
    ReferenceProcessor* rp = PSScavenge::reference_processor();
    if (rp->discover_reference(obj, reference_type())) {
      // reference already enqueued, referent and next will be traversed later
      instanceKlass::oop_copy_contents(pm, obj);
      return;
    } else {
      // treat referent as normal oop
      pm->claim_or_forward_breadth(referent_addr);
    }
  }
  // treat next as normal oop
heapRef*next_addr=(heapRef*)java_lang_ref_Reference::next_addr(obj);
  assert0(objectRef::is_null_or_heap(next_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*next_addr))) {
    pm->claim_or_forward_breadth(next_addr);
  }
  instanceKlass::oop_copy_contents(pm, obj);
}

void instanceRefKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(pm->depth_first(), "invariant");
  heapRef* referent_addr = java_lang_ref_Reference::referent_addr(obj);
  assert0(objectRef::is_null_or_heap(referent_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*referent_addr))) {
    ReferenceProcessor* rp = PSScavenge::reference_processor();
    if (rp->discover_reference(obj, reference_type())) {
      // reference already enqueued, referent and next will be traversed later
      instanceKlass::oop_push_contents(pm, obj);
      return;
    } else {
      // treat referent as normal oop
      pm->claim_or_forward_depth(referent_addr);
    }
  }
  // treat next as normal oop
  heapRef* next_addr = java_lang_ref_Reference::next_addr(obj);
  assert0(objectRef::is_null_or_heap(next_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*next_addr))) {
    pm->claim_or_forward_depth(next_addr);
  }
  instanceKlass::oop_push_contents(pm, obj);
}

int instanceRefKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  instanceKlass::oop_update_pointers(cm, obj);
  
  heapRef* referent_addr = (heapRef*)java_lang_ref_Reference::referent_addr(obj);
  PSParallelCompact::adjust_pointer(referent_addr);
heapRef*next_addr=(heapRef*)java_lang_ref_Reference::next_addr(obj);
  PSParallelCompact::adjust_pointer(next_addr);
heapRef*discovered_addr=(heapRef*)java_lang_ref_Reference::discovered_addr(obj);
  PSParallelCompact::adjust_pointer(discovered_addr);

#ifdef ASSERT
  if(TraceReferenceGC && PrintGCDetails) {
    gclog_or_tty->print_cr("instanceRefKlass::oop_update_pointers obj "
PTR_FORMAT,(oopDesc*)obj);
gclog_or_tty->print_cr("     referent_addr/* "PTR_FORMAT" / "
PTR_FORMAT,referent_addr,
			   referent_addr ? ALWAYS_UNPOISON_OBJECTREF(*referent_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     next_addr/* "PTR_FORMAT" / "
PTR_FORMAT,next_addr,
			   next_addr ? ALWAYS_UNPOISON_OBJECTREF(*next_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     discovered_addr/* "PTR_FORMAT" / "
PTR_FORMAT,discovered_addr,
		   discovered_addr ? ALWAYS_UNPOISON_OBJECTREF(*discovered_addr).as_oop() : NULL);
  }
#endif

  return size_helper();
}

int
instanceRefKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				      HeapWord* beg_addr, HeapWord* end_addr) {
  instanceKlass::oop_update_pointers(cm, obj, beg_addr, end_addr);
  
  heapRef* p;
heapRef*referent_addr=p=(heapRef*)java_lang_ref_Reference::referent_addr(obj);
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
heapRef*next_addr=p=(heapRef*)java_lang_ref_Reference::next_addr(obj);
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
heapRef*discovered_addr=p=(heapRef*)java_lang_ref_Reference::discovered_addr(obj);
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);

#ifdef ASSERT
  if(TraceReferenceGC && PrintGCDetails) {
    gclog_or_tty->print_cr("instanceRefKlass::oop_update_pointers obj "
PTR_FORMAT,(oopDesc*)obj);
gclog_or_tty->print_cr("     referent_addr/* "PTR_FORMAT" / "
PTR_FORMAT,referent_addr,
			   referent_addr ? ALWAYS_UNPOISON_OBJECTREF(*referent_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     next_addr/* "PTR_FORMAT" / "
PTR_FORMAT,next_addr,
			   next_addr ? ALWAYS_UNPOISON_OBJECTREF(*next_addr).as_oop() : NULL);
gclog_or_tty->print_cr("     discovered_addr/* "PTR_FORMAT" / "
PTR_FORMAT,discovered_addr,
		   discovered_addr ? ALWAYS_UNPOISON_OBJECTREF(*discovered_addr).as_oop() : NULL);
  }
#endif

  return size_helper();
}

void instanceRefKlass::update_nonstatic_oop_maps(klassOop k) {
  // Clear the nonstatic oop-map entries corresponding to referent and discovered field.
  // For collectors other than GPGC, also clear the entry for the next field.
  // The discovered field is used only by the garbage collector and is treated specially.
  // The referent and next fields are also treated specially by the garbage collector.
  instanceKlass* ik = instanceKlass::cast(k);

  // Check that we have the right class
  debug_only(static bool first_time = true);
  assert(k == SystemDictionary::reference_klass() && first_time,
         "Invalid update of maps");
  debug_only(first_time = false);
  assert(ik->nonstatic_oop_map_size() == 1, "just checking");

  OopMapBlock* map = ik->start_of_nonstatic_oop_maps();

  // Check that the current map is (1,5) - currently points at field with
  // offset 1 (words) and has 5 map entries.
  debug_only(int offset = java_lang_ref_Reference::referent_offset);
  debug_only(int length = ((java_lang_ref_Reference::discovered_offset - 
    java_lang_ref_Reference::referent_offset)/wordSize) + 1);

  {
//    assert(map->offset() == offset && map->length() == length,
//           "just checking");

    assert(java_lang_ref_Reference::hc_pending_offset == 1 &&
           java_lang_ref_Reference::hc_queue_offset   == 2 &&
           java_lang_ref_Reference::hc_next_offset    == 3,
"java.lang.ref.Reference layout changed.");

    if ( UseGenPauselessGC ) {
      // Update map to (2,3) - point to offset of 2 (words) with 3 map entries.
      // The pending, queue, and next fields will be in the OopMap.
      // The referent and discovered fields are excluded.
map->set_offset(java_lang_ref_Reference::pending_offset);
map->set_length(3);
    } else {
      // Update map to (2,2) - point to offset of 2 (words) with 2 map entries.
      // The pending and queue fields will be in the OopMap, though pending isn't used.
      // The referent, next, and discovered fields are excluded.
map->set_offset(java_lang_ref_Reference::pending_offset);
map->set_length(2);
    }
  }
}


// Verification
#ifndef PRODUCT
void instanceRefKlass::oop_verify_on(oop obj, outputStream* st) {
  instanceKlass::oop_verify_on(obj, st);
  // Verify referent field
  oop referent = java_lang_ref_Reference::referent(obj);

  // We should make this general to all heaps
  GenCollectedHeap* gch = NULL;
  if (Universe::heap()->kind() == CollectedHeap::GenCollectedHeap)
    gch = GenCollectedHeap::heap();
  
  if (referent != NULL) {
    guarantee(referent->is_oop(), "referent field heap failed");
    if (gch != NULL && !gch->is_in_youngest(obj))
      // We do a specific remembered set check here since the referent
      // field is not part of the oop mask and therefore skipped by the
      // regular verify code.
      obj->verify_old_oop(java_lang_ref_Reference::referent_addr(obj), true);
  }
  // Verify next field
  oop next = java_lang_ref_Reference::next(obj);
  if (next != NULL) {
    guarantee(next->is_oop(), "next field verify failed");    
    guarantee(next->is_instanceRef(), "next field verify failed");
    if (gch != NULL && !gch->is_in_youngest(obj)) {
      // We do a specific remembered set check here since the next field is
      // not part of the oop mask and therefore skipped by the regular
      // verify code.
      obj->verify_old_oop(java_lang_ref_Reference::next_addr(obj), true);
    }
  }
}
#endif

void instanceRefKlass::acquire_pending_list_lock(){
  // we may enter this with pending exception set
  PRESERVE_EXCEPTION_MARK;  // exceptions are never thrown, needed for TRAPS argument
  assert0( !UseGenPauselessGC
           || Thread::current() == (Thread *)SurrogateLockerThread::slt()) ;

  heapRef pllRef = lvb_ref(java_lang_ref_Reference::pending_list_lock_addr());
  oop pll = pllRef.as_oop();

  // Always inflate PLL, in order to guarantee balanced lock/unlock.
  // With biased locking, if a JavaObject is lock-biased, no recursion counting
  // is performed in order to ensure balanced locking. For majority of the cases
  // recursive lock count can be determined by stack crawling.
  // Need special case if the object is to be locked in native C++ code:
  // 1. We use ObjectLocker as a StackObject, if we still have recursion.
  // 2. In case of PLL, when accessed through SurrogateLockerThread, multiple
  //    GC threads can cause PLL to be locked in a loop multiple times.
  // That said, incase of PLL, once its inflated, we can do reference
  // counting to enable balanced lock/unlock.
  
  pll->lock(INF_JNI_ENTER,THREAD);
  // Reload after locking allows GC
  pllRef = lvb_ref(java_lang_ref_Reference::pending_list_lock_addr());
  pll = pllRef.as_oop();
ObjectMonitor*mon=ObjectSynchronizer::inflate(pllRef,INF_JNI_ENTER);

  // unbias this lock, i.e. already locked by current thread.
  mon->unbias_locked();

#ifdef ASSERT
  pllRef = lvb_ref(java_lang_ref_Reference::pending_list_lock_addr());
  pll = pllRef.as_oop();
  assert0(pll->is_self_locked());
#endif

  if (HAS_PENDING_EXCEPTION) CLEAR_PENDING_EXCEPTION;
}

void instanceRefKlass::release_and_notify_pending_list_lock() {
  // we may enter this with pending exception set
  PRESERVE_EXCEPTION_MARK;  // exceptions are never thrown, needed for TRAPS argument
  assert0( !UseGenPauselessGC
           || Thread::current() == (Thread *)SurrogateLockerThread::slt()) ;

  heapRef pllRef = lvb_ref(java_lang_ref_Reference::pending_list_lock_addr());
  oop pll = pllRef.as_oop();

  // See instanceRefKlass::acquire_pending_list_lock(), where we always
  // guarantee to inflate PLL anytime its locked.
  assert0(pll->mark()->has_monitor() );
  assert0(pll->is_self_locked());
  ObjectMonitor *mon = pll->mark()->monitor();

  // Notify waiters on pending lists lock if there is any reference.
  if (java_lang_ref_Reference::pending_list() != NULL) {
ObjectSynchronizer::notifyall(pllRef,THREAD);
  }

pll->unlock();
  if (HAS_PENDING_EXCEPTION) CLEAR_PENDING_EXCEPTION;
}
