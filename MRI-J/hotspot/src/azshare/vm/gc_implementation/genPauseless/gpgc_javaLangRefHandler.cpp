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


#include "exceptions.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_newCollector.inline.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_slt.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"
#include "java.hpp"
#include "javaClasses.hpp"
#include "jniHandles.hpp"
#include "os.hpp"
#include "pgcTaskManager.hpp"
#include "prefetch.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"


bool GPGC_JavaLangRefHandler::_should_clear_referent[4] = { true, true, false, false };


GPGC_JavaLangRefHandler::GPGC_JavaLangRefHandler(uint64_t gen, uint64_t space_id) {
  EXCEPTION_MARK;

  // Initialize the master soft ref clock.
  java_lang_ref_SoftReference::set_clock(os::javaTimeMillis());

  if (HAS_PENDING_EXCEPTION) {
      Handle ex(THREAD, PENDING_EXCEPTION);
      vm_exit_during_initialization(ex);
  }

  assert0( (gen == GPGC_PageInfo::NewGen && space_id==objectRef::new_space_id) ||
           (gen == GPGC_PageInfo::OldGen && space_id==objectRef::old_space_id) );

_scan_generation=gen;
  _scan_space_id   = space_id;

  _capture_jlrs     = false;
  _new_pending_jlrs = false;

_soft_ref_policy=NULL;

  _captured_ref_lists[REF_SOFT]    = NULL;
  _captured_ref_lists[REF_WEAK]    = NULL;
  _captured_ref_lists[REF_FINAL]   = NULL;
  _captured_ref_lists[REF_PHANTOM] = NULL;

  assert0( _should_clear_referent[REF_SOFT]    == true  );
  assert0( _should_clear_referent[REF_WEAK]    == true  );
  assert0( _should_clear_referent[REF_FINAL]   == false );
  assert0( _should_clear_referent[REF_PHANTOM] == false );
}


void GPGC_JavaLangRefHandler::pre_mark(ReferencePolicy* policy) {
  assert0(soft_ref_policy() == NULL);
  set_soft_ref_policy(policy);

  assert0(!is_capture_on());
  assert0(none_captured());
}


void GPGC_JavaLangRefHandler::set_jlr_capture(bool flag) {
  // If we've flipped off the normal handling of java.lang.refs, don't alter the capture state:
  if ( ! RegisterReferences ) { return; }

_capture_jlrs=flag;
}


void GPGC_JavaLangRefHandler::acquire_pending_list_lock(){
assert(!new_pending_jlrs(),"Shouldn't have new pending java.lang.refs before we've acquired the pending list lock.");
  GPGC_SLT::acquire_lock();
}


void GPGC_JavaLangRefHandler::release_pending_list_lock() {
  GPGC_SLT::release_lock();
  set_new_pending_jlrs(false);
}


bool GPGC_JavaLangRefHandler::none_captured() {
  // Check saved refs
  if ( _captured_ref_lists[REF_SOFT]    != NULL ||
       _captured_ref_lists[REF_WEAK]    != NULL ||
       _captured_ref_lists[REF_FINAL]   != NULL ||
       _captured_ref_lists[REF_PHANTOM] != NULL)
  {
    return false;
  }

  if (is_capture_on()) {
    return false;
  }

  return true;
}


void GPGC_JavaLangRefHandler::mark_roots(GPGC_GCManagerNewStrong* gcm)
{
  // There are no new-space roots in this class to hand to NewGC.
  assert(GPGC_Space::is_in_permanent(java_lang_ref_Reference::pending_list_addr()),
"Head of pending refs list ought to be in PermGen.");
}


void GPGC_JavaLangRefHandler::mark_roots(GPGC_GCManagerOldStrong* gcm)
{
  instanceKlass* ik  = instanceKlass::cast(SystemDictionary::reference_klass());
  int            kid = ik->klassId();

  objectRef* pending_list_addr = java_lang_ref_Reference::pending_list_addr();
  GPGC_OldCollector::mark_and_push(gcm, pending_list_addr, kid);
}


bool GPGC_JavaLangRefHandler::capture_java_lang_ref(GPGC_ThreadRefLists* thread_lists, oop obj, ReferenceType ref_type)
{
  // During strong-live marking, we capture any java lang refs we find.  Later when we're
  // converting final-live objects to strong-live, we don't capture any java lang refs found.
  if ( ! _capture_jlrs ) { return false; }

#ifdef ASSERT
  {
    // This method doesn't ever need to look at the referent, except for sanity checks.

    // Inter-space references are treated as strong-refs.  When we're processing a reference in
    // one space, we can't tell if the referent in the other space is already marked live or not,
    // so we don't have a choice.  The caller should have already screened out inter-space
    // references.

    // We don't explicitly screen out NULL referents here, because the caller already did it.
    // There's always a race with JavaThreads calling java.lang.ref.Reference.clear(), so we
    // just have to deal with the occasional NULL referent, including having asserts that are
    // tolerant of NULL.
    
    objectRef* referent_addr = java_lang_ref_Reference::referent_addr(obj);
    objectRef  referent      = GPGC_Collector::remap_only(referent_addr);

    assert(referent.is_null() || GPGC_Space::is_in(referent.as_oop()), "bad referent");
    assert(referent.is_null() || referent.space_id()==scan_space_id(), "unexpected referent space");
  }
#endif // ASSERT

  // java.lang.ref.Reference subclasses other than the four standard java library subclasses
  // are treated like standard java objects.
  if ( ref_type == REF_OTHER ) { return false; }

  // All referents at this point are non NULL and not yet marked StrongLive, though
  // that could change at any moment.
  //
  // Reference handling rules:
  //
  //   SoftRefs: If SoftRefPolicy says don't clear, don't enqueue the reference.
  // 
  //   WeakRefs: Always enqueue the reference.
  //
  //   FinalRefs: If previously selected for finalization (non-null pending or next field), the
  //              referent is considered strong live.  Otherwise, enqueue the reference.
  //
  //   PhantomRefs: Always enqueue the reference.

  // Assert the obj is in the space this GPGC_JavaLangRefHandler is handling, because the object
  // traversal code should have already screened out objects in the wrong space.
  assert0( (uint64_t)GPGC_PageInfo::page_info(GPGC_Layout::addr_to_PageNum(obj))->just_gen() == scan_generation() );

objectRef*pending_addr=java_lang_ref_Reference::pending_addr(obj);
  objectRef* next_addr       = java_lang_ref_Reference::next_addr(obj);
  objectRef* discovered_addr = java_lang_ref_Reference::discovered_addr(obj);

  assert(discovered_addr->is_null(), "bad discovered field");

  switch (ref_type) {
    case REF_SOFT:
      assert(pending_addr->is_null(), "Found non-null pending in un-cleared SoftRef");
      // The exepensive part of handling soft refs is traversing the object graph behind
      // them.  To avoid this, GPGC decides if it will keep a softref alive during concurrent
      // marking.  // During the processing of captured refs, no soft refs will be revived.
      if ( ! soft_ref_policy()->should_clear_reference(obj) ) {
        return false;
      }
      break;
    case REF_WEAK:
      assert(pending_addr->is_null(), "Found non-null pending in un-cleared WeakRef");
      break;
    case REF_FINAL:
      if ( pending_addr->not_null() || next_addr->not_null() ) {
        // TODO: if a ref that has ever been pending forever more has a non-null pending,
        //       we could perhaps stop checking the next field.  This only works if we can
        //       be sure nothing besides GC and the ReferenceHandler would ever enqueue an
        //       instance of FinalReference.
        // XXX If a JavaThread can enqueue a FinalRef, the processing phase is probably
        //     wrong for FinalRefs.
  
        // Final referents previously selected for finalization are considered StrongLive.
        return false;
      }
      break;
    case REF_PHANTOM:
      // No special handling for phantom refs
      break;
    default:
      // Unexpected ref_type if we get this far.
      ShouldNotReachHere();
  }

  // Get the thread local captured ref list for the type of ref we're capturing,
  // and push it onto the global list of lists if it's full.
  if ( GPGCJLRsPerWorkUnit == thread_lists->capturedRefCount(ref_type) ) { 
    save_captured_refs(thread_lists, ref_type);
  }
  objectRef* capture_list = thread_lists->capturedRefsAddr(ref_type);

  // Push the JLR objectRef onto the head of the thread local captured ref list.
  objectRef list_head = *capture_list;
objectRef jlr=objectRef(obj);

  *capture_list = ALWAYS_POISON_OBJECTREF(jlr);

if(list_head.not_null()){
    *discovered_addr = list_head;
  } else {
    *discovered_addr = ALWAYS_POISON_OBJECTREF(jlr);
  }
  thread_lists->incrementRefCount(ref_type);

  assert(jlr.space_id()==scan_space_id(), "Captured JLR in wrong scan space");

  if ( TraceReferenceGC ) {
      gclog_or_tty->print_cr("Captured JLR " PTR_FORMAT " of type %d", obj, ref_type);
  }

  return true;
}


void GPGC_JavaLangRefHandler::save_ref_lists(GPGC_ThreadRefLists* thread_lists) {
  if ( thread_lists->capturedRefCount(REF_SOFT)    != 0 ) { save_captured_refs(thread_lists, REF_SOFT); }
  if ( thread_lists->capturedRefCount(REF_WEAK)    != 0 ) { save_captured_refs(thread_lists, REF_WEAK); }
  if ( thread_lists->capturedRefCount(REF_FINAL)   != 0 ) { save_captured_refs(thread_lists, REF_FINAL); }
  if ( thread_lists->capturedRefCount(REF_PHANTOM) != 0 ) { save_captured_refs(thread_lists, REF_PHANTOM); }
}


void GPGC_JavaLangRefHandler::save_captured_refs(GPGC_ThreadRefLists* thread_lists, ReferenceType ref_type)
{
  objectRef captured_head = thread_lists->capturedRefsHead(ref_type);

if(captured_head.not_null()){
    *thread_lists->capturedRefsAddr(ref_type) = nullRef;
    thread_lists->resetRefCount(ref_type);
  
    // Now CAS the data onto the saved lists
    GPGC_ReferenceList* new_list = new GPGC_ReferenceList(captured_head, ref_type);

    GPGC_ReferenceList* old_head_list;
    do {
      old_head_list = *captured_ref_lists(ref_type);
      new_list->set_next(old_head_list);
    } while ( intptr_t(old_head_list) != Atomic::cmpxchg_ptr((intptr_t) new_list,
                                                             (intptr_t*)captured_ref_lists(ref_type),
                                                             (intptr_t) old_head_list) );
  }
}


void GPGC_JavaLangRefHandler::do_refs_parallel(ReferenceType ref_type)
{
  if ( _captured_ref_lists[ref_type] == NULL ) { return; }

  GPGC_ReferenceList* captured_list = _captured_ref_lists[ref_type];
PGCTaskQueue*q=PGCTaskQueue::create();
  PGCTaskManager*     manager;

  _captured_ref_lists[ref_type] = NULL;

  if ( _scan_generation == GPGC_PageInfo::NewGen ) {
    manager = GPGC_Heap::new_gc_task_manager();
while(captured_list!=NULL){
q->enqueue(new GPGC_NewGC_RefsTask(captured_list));
      captured_list = captured_list->next();
    }
  } else {
    manager = GPGC_Heap::old_gc_task_manager();
while(captured_list!=NULL){
q->enqueue(new GPGC_OldGC_RefsTask(captured_list));
      captured_list = captured_list->next();
    }
  }

  GPGC_Collector::run_task_queue(manager, q);
}


void GPGC_JavaLangRefHandler::do_soft_refs_parallel()
{
  assert0(soft_ref_policy() != NULL);
set_soft_ref_policy(NULL);

  do_refs_parallel(REF_SOFT);

  // Soft ref clock (always do this after soft ref processing!)
  update_softref_clock();
}


void GPGC_JavaLangRefHandler::do_weak_refs_parallel()
{
  do_refs_parallel(REF_WEAK);
}


void GPGC_JavaLangRefHandler::do_final_refs_parallel()
{
  do_refs_parallel(REF_FINAL);
}


void GPGC_JavaLangRefHandler::do_jni_weak_refs()
{
  if ( _scan_generation == GPGC_PageInfo::NewGen ) {
    JNIHandles::GPGC_weak_oops_do(true);
  } else {
    JNIHandles::GPGC_weak_oops_do(false);
  }
}


void GPGC_JavaLangRefHandler::do_phantom_refs_parallel()
{
  do_refs_parallel(REF_PHANTOM);
}


void GPGC_JavaLangRefHandler::update_softref_clock()
{
  // Update (advance) the soft ref master clock field. This must be done
  // after processing the soft ref list.
  jlong now = os::javaTimeMillis();
  jlong clock = java_lang_ref_SoftReference::clock();
  NOT_PRODUCT(
    if (now < clock) {
      warning("time warp: %d to %d", clock, now);
    }
  );
  // In product mode, protect ourselves from system time being adjusted
  // externally and going backward; see note in the implementation of
  // GenCollectedHeap::time_since_last_gc() for the right way to fix
  // this uniformly throughout the VM; see bug-id 4741166. XXX
  if (now > clock) {
    java_lang_ref_SoftReference::set_clock(now);
  }
  // Else leave clock stalled at its old value until time progresses
  // past clock value.
}


void GPGC_JavaLangRefHandler::do_captured_ref_list(GPGC_GCManagerMark* gcm, GPGC_ReferenceList* list)
{
  assert(!GPGC_JavaLangRefHandler::is_capture_on(), "JLR capture should be off during processing");
  // assert0(Thread::current()->is_gc_mode());

  objectRef*    captured_jlrs_addr = list->refs_list();
  ReferenceType ref_type           = list->ref_type();
  objectRef     this_jlr           = nullRef;
  objectRef     next_jlr           = ALWAYS_UNPOISON_OBJECTREF(*captured_jlrs_addr);
  objectRef*    prev_pending_addr  = captured_jlrs_addr;
  objectRef     last_valid_jlr     = nullRef;

  assert0( next_jlr.not_null() );

  // Each captured ref list is a linked list, where the tail JLR on the linked list points to itself.
  // We walked the captured ref list, and cull all JLRs which aren't going to be put on the pending
  // list.
  while (this_jlr != next_jlr) {
    this_jlr = next_jlr;

    oop this_jlr_as_oop = this_jlr.as_oop();
    assert(this_jlr_as_oop->is_instanceRef(), "should be reference object");

    objectRef* this_jlr_referent_addr   = java_lang_ref_Reference::referent_addr  (this_jlr_as_oop);
    objectRef* this_jlr_pending_addr    = java_lang_ref_Reference::pending_addr   (this_jlr_as_oop);
objectRef*this_jlr_discovered_addr=java_lang_ref_Reference::discovered_addr(this_jlr_as_oop);

    next_jlr = ALWAYS_UNPOISON_OBJECTREF(*this_jlr_discovered_addr);
    oop next_jlr_as_oop = next_jlr.as_oop();

    Prefetch::write(next_jlr_as_oop, 0);
Prefetch::write(next_jlr_as_oop,BytesPerCacheLine);

    *this_jlr_discovered_addr = ALWAYS_POISON_OBJECTREF(nullRef);

    objectRef referent = ALWAYS_UNPOISON_OBJECTREF(*this_jlr_referent_addr);

    assert(Universe::heap()->is_in_or_null(referent.as_oop()), "Wrong oop found in java.lang.Reference object");

if(referent.is_null()){
      // Referent has been cleared since it the JLR was captured by the JLR handler.  Don't put the JLR on the pending list.
      *prev_pending_addr = ALWAYS_POISON_OBJECTREF(next_jlr);
      if (TraceReferenceGC) {
        gclog_or_tty->print_cr("Skipping concurrent cleared JLR " INTPTR_FORMAT " of type %d", this_jlr.raw_value(), ref_type);
      }
      continue;
    }
 
    referent = GPGC_Collector::remap(referent);
    assert0( this_jlr.space_id() == referent.space_id() );

    if (GPGC_Marks::is_any_marked_strong_live(referent.as_oop())) {
      // Someone has a strong reference to the referent.
      // Update the referent objectRef metadata, then skip the JLR.
      gcm->update_live_referent(this_jlr_referent_addr, this_jlr.klass_id());
      *prev_pending_addr = ALWAYS_POISON_OBJECTREF(next_jlr);
      if (TraceReferenceGC) {
        gclog_or_tty->print_cr("Skipping StrongLive referent JLR " INTPTR_FORMAT " of type %d", this_jlr.raw_value(), ref_type);
      }
      continue;
    }

    // At this point we have a non-null, non StrongLive referent.

    if (should_clear_referent(ref_type)) {
      // If we're looking at a SoftRef or a WeakRef, then should_clear_referent() will return true.
      // For both JLR types, we clear the referent and enqueue to the pending list.  And we assert that
      // the JLR hasn't previously been pending.
      assert0(ref_type==REF_SOFT || ref_type==REF_WEAK);
      assert(this_jlr_pending_addr->is_null(), "Clearing SoftRef or WeakRef that's already been on pending list!");

      *this_jlr_referent_addr = ALWAYS_POISON_OBJECTREF(nullRef);
    }
    else {
      // If should_clear_referent() returned false, we're looking at a FinalRef or a PhantomRef.

      if (ref_type == REF_PHANTOM) {
        // PhantomRefs will have been captured regardless of their being previously pending or not.
        // This is so that phantom referents are not marked through StrongLive until stronger forms of
        // references have first been processed.
        //
        // Here, we've arrived at a phantom-live referent, and a JLR that may have already been
        // pending.
        //
        // We throw the referent onto the marking stack for future marking, but don't mark it live yet.
        // We enqueue the PhantomRef onto the pending list if it's not already pending.
        gcm->push_referent_without_mark(this_jlr_referent_addr, this_jlr.klass_id());

        if (this_jlr_pending_addr->not_null()) {
          // Already handed off to the ReferenceHandler thread, so we skip making it pending again.
          *prev_pending_addr = ALWAYS_POISON_OBJECTREF(next_jlr);
          if (TraceReferenceGC) {
            gclog_or_tty->print_cr("Skipping pending phantom JLR " INTPTR_FORMAT " of type %d", this_jlr.raw_value(), ref_type);
          }
          continue;
        }
      }
      else {
        assert0(ref_type==REF_FINAL);

        // Previously finalizable FinalRefs should already have been screened out during initial JLR capture.
        // So here we can assert that the FinalRef has never been pending.
        assert(this_jlr_pending_addr->is_null(), "Found FinalRef that's already been on pending list!");

        // We push the referent onto the marking stack for StrongLive marking.  It's okay to mark it
        // live right now, because there can't be multiple Final JLRs pointing to the same referent,
        // and we won't do a strack drain and mark traversal until after all Final JLRs have been
        // looked at.
        gcm->mark_and_push_referent(this_jlr_referent_addr, this_jlr.klass_id());
      }
    }

    // XXX Do we still need to make sure java.lang.ref.Reference.enqueue() can't enqueue a
    //     ref without handing the referent to GC for marking?

    if (TraceReferenceGC) {
      gclog_or_tty->print_cr("Linking new pending JLR " INTPTR_FORMAT " of type %d", this_jlr.raw_value(), ref_type);
    }

    last_valid_jlr = this_jlr;

    // optimistically link this JLR's pending field to the next object in the list.  We'll relink
    // if we skip the next object in the list.
    *this_jlr_pending_addr = ALWAYS_POISON_OBJECTREF(next_jlr);
    prev_pending_addr      = this_jlr_pending_addr;
  }

  // We've now walked the original captured refs list.  The captured_jlrs_addr now points to just the JLRs that
  // should be handed to the ReferenceHandler thread(s).  The JLRs on the list are now linked together via
  // their pending field, and their discovered field should be set to nullRef.  The tail JLR on the list will
  // have it's pending field set to itself.  If no JLRs were selected to be pending, then last_valid_jlr will
  // equal nullRef.

if(last_valid_jlr.not_null()){
    oop last_valid_jlr_as_oop = last_valid_jlr.as_oop();
    assert(last_valid_jlr_as_oop->is_instanceRef(), "should be reference object");

    objectRef* last_valid_jlr_pending_addr = java_lang_ref_Reference::pending_addr(last_valid_jlr_as_oop);
    assert(ALWAYS_UNPOISON_OBJECTREF(*last_valid_jlr_pending_addr) == next_jlr, "Expecting to see the tail JLR pending pointing to next_jlr");

    objectRef* pending_list_addr = java_lang_ref_Reference::pending_list_addr();

    DEBUG_ONLY( objectRef pending_list_value = ALWAYS_UNPOISON_OBJECTREF(*pending_list_addr); )
    assert0(pending_list_value.is_null() || pending_list_value.is_heap());

    objectRef oldTopOfQueue = ALWAYS_UNPOISON_OBJECTREF(Atomic::xchg_ptr(captured_jlrs_addr->raw_value(),
                                                                         (intptr_t*)pending_list_addr));

    if ( ALWAYS_UNPOISON_OBJECTREF(*captured_jlrs_addr).is_new() ) {
      assert(GPGC_Space::is_in_permanent(pending_list_addr), "Head of pending refs list ought to be in PermGen.");
      // The pending_list_addr is in PermGen, and we wrote a new_space ref, so we need to card mark it.
      GPGC_Marks::atomic_attempt_card_mark(pending_list_addr);
    }

if(oldTopOfQueue.is_null()){
      // If the prior pending list was empty, the tail of the list we just pushed on should point to itself.
      *last_valid_jlr_pending_addr = ALWAYS_POISON_OBJECTREF(last_valid_jlr);
      GPGC_Marks::set_nmt_update_markid(last_valid_jlr_pending_addr, 0xC0);
    } else {
      if ( last_valid_jlr.is_new() && oldTopOfQueue.is_old() ) {
        // The old top of the queue must already be NMT'ed, via RefProcessor::mark_roots().
        guarantee(GPGC_NMT::has_desired_old_nmt(oldTopOfQueue), "Un-NMT'ed oldTopOfQueue");
        assert(GPGC_Collector::is_new_collector_thread(Thread::current()), "expecting a NewGC thread");

        *last_valid_jlr_pending_addr = ALWAYS_POISON_OBJECTREF(oldTopOfQueue);
        GPGC_Marks::set_nmt_update_markid(last_valid_jlr_pending_addr, 0xC1);
      } else {
        *last_valid_jlr_pending_addr = ALWAYS_POISON_OBJECTREF(oldTopOfQueue);
        GPGC_Marks::set_nmt_update_markid(last_valid_jlr_pending_addr, 0xC2);

        if ( oldTopOfQueue.is_new() && last_valid_jlr.is_old() ) {
          // If we're taking a NewGen oldTopOfQueue and storing it into an OldGen queue tail, we
          // need to card mark, and ensure the NewGC doesn't race and miss it.
          assert(GPGC_Collector::is_old_collector_thread(Thread::current()), "expecting a OldGC thread");
          assert(GPGC_Interlock::interlock_held_by_OldGC(GPGC_Interlock::NewGC_CardMarkScan),
"NewGC shouldn't card-mark scan while OldGC is relinking the pending list");

          GPGC_Marks::atomic_attempt_card_mark(last_valid_jlr_pending_addr);
        }
      }
    }

    // Record if we've added new pending JLRs.
    set_new_pending_jlrs(true);
  } else {
    // If the were no JLRs selected to be pending, we expect the captured_jlrs_addr to contain the value next_jlr.
    assert0(ALWAYS_UNPOISON_OBJECTREF(*captured_jlrs_addr) == next_jlr);
  }
}
