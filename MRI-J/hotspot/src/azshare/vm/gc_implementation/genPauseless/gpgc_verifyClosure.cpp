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


#include "constantPoolKlass.hpp"
#include "gcTaskThread.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_interlock.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_markingQueue.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_safepoint.hpp"
#include "gpgc_space.hpp"
#include "gpgc_traps.hpp"
#include "gpgc_verifyClosure.hpp"
#include "gpgc_verify_tasks.hpp"
#include "mutexLocker.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "handles.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

volatile long                      GPGC_VerifyClosure::_working              = 0;
GCTask* volatile                   GPGC_VerifyClosure::_gc_task_stack        = NULL;
GCTaskManager*                     GPGC_VerifyClosure::_verify_task_manager  = NULL;
long                               GPGC_VerifyClosure::_active_workers       = 0;
long                               GPGC_VerifyClosure::_wakeup_count         = 0;
GPGC_VC_PerThread*                 GPGC_VerifyClosure::_per_thread           = NULL;
GPGC_VerifyClosure::WorkList       GPGC_VerifyClosure::_work_lists[]         = {
                                                                                 WorkList(), WorkList(), WorkList(), WorkList(),
                                                                                 WorkList(), WorkList(), WorkList(), WorkList(),
                                                                                 WorkList(), WorkList(), WorkList(), WorkList(),
                                                                                 WorkList(), WorkList(), WorkList(), WorkList()
                                                                               };
bool                               GPGC_VerifyClosure::_ignore_new           = false;
bool                               GPGC_VerifyClosure::_ignore_old           = false;
klassOop GPGC_VerifyClosure::_constantPoolKlassOop=NULL;
GPGC_VerifyClosure::IsAliveClosure GPGC_VerifyClosure::_is_alive             = GPGC_VerifyClosure::IsAliveClosure();


class GPGC_SetupVerifyThreadClosure:public ThreadClosure{
  public:
    virtual void do_thread(Thread* thread) {
      assert0( thread->is_GC_task_thread() );
      GCTaskThread* gtt = (GCTaskThread*) thread;
      gtt->set_thread_type(GPGC_Collector::Verifier);
    }
};
    

void GPGC_VerifyClosure::initialize()
{
  if ( ! GPGCVerifyHeap ) return;

  if ( GPGCVerifyThreads == 0 ) {
    // GPGCVerifyThreads still set to default value of 0.  Which really
    // Means use as many threads as the collector is using.
    GPGCVerifyThreads = GPGCThreads;
  }
  if ( GPGCVerifyThreads < 1 ) {
    GPGCVerifyThreads = 1;
  }

  _working             = 0;
  _verify_task_manager = GCTaskManager::create(GPGCVerifyThreads, true);

  GPGC_SetupVerifyThreadClosure stc;
  _verify_task_manager->threads_do(&stc);

  _per_thread          = NEW_C_HEAP_ARRAY(GPGC_VC_PerThread, GPGCVerifyThreads);

  for ( long i=0; i<GPGCVerifyThreads; i++ ) {
    _per_thread[i]._drain_stack    = GPGC_MarkingQueue::new_block();
    _per_thread[i]._fill_stack_top = 0;
    _per_thread[i]._fill_stack     = GPGC_MarkingQueue::new_block();
    _per_thread[i]._ref_counter    = 0;

    _per_thread[i]._drain_stack->_refs[0] = nullRef;
  }
}


void GPGC_VerifyClosure::reset()
{
  for ( long i=0; i<GPGCVerifyThreads; i++ ) {
    _per_thread[i]._ref_counter = 0;
  }

guarantee(verify_empty_stacks(),"verify marking stack should be empty. (3)");
}


bool GPGC_VerifyClosure::set_verify_mark(oop obj)
{
  long      bit_index   = GPGC_Marks::bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::verify_mark_bitmap_base();

  return GPGC_Marks::atomic_set_bit_in_bitmap(bitmap_base, bit_index);
}


bool GPGC_VerifyClosure::is_verify_marked(oop obj)
{
  long      bit_index   = GPGC_Marks::bit_index_for_mark(intptr_t(obj));
  HeapWord* bitmap_base = GPGC_Layout::verify_mark_bitmap_base();
  
  return GPGC_Marks::is_bit_set_in_bitmap(bitmap_base, bit_index);
}


void GPGC_VerifyClosure::push_to_verify_stack(objectRef ref)
{
  // Verify the oop is an oop:
  {
    //assert0(!check_obj_alignment(o));
    assert0(GPGC_Space::is_in(ref.as_oop()));
    DEBUG_ONLY( klassRef klass = UNPOISON_KLASSREF(*(ref.as_oop()->klass_addr())); )
    assert0(klass.is_old());
    assert0(GPGC_Space::is_in(klass.as_oop()));
  }

  guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "verifier can't push unrelocated ref onto marking stack");

  _data->_ref_counter++;
  _data->_fill_stack->_refs[_data->_fill_stack_top++] = ref;

  if ( _data->_fill_stack_top == GPGC_MarkingQueue::RefBlockLength ) {
    push_marking_stack(_data->_fill_stack);
    _data->_fill_stack     = GPGC_MarkingQueue::new_block();
    _data->_fill_stack_top = 0;
  }
}


void GPGC_VerifyClosure::do_oop(objectRef* p)
{
assert0(Thread::current()->is_GC_task_thread());

  // TODO: assert that both collectors are halted, so objects aren't getting moved
  // while I'm trying to do a verify here.

  objectRef ref     = PERMISSIVE_UNPOISON(*p, p);
  long      mark_id;

  if ( ref.is_null() ) { return; }

  guarantee(ref.is_heap(), "Found non-heap ref in verify");

  // Check the original ref has a space-id that matches the GPGC_PageInfo.
oop obj=ref.as_oop();
  PageNum             trap_page = GPGC_Layout::addr_to_PageNum(obj);
  PageNum             info_page = GPGC_Layout::mid_space_addr(obj) ? GPGC_Layout::addr_to_MidSpaceBasePageNum(obj) : trap_page;
  GPGC_PageInfo*      info      = GPGC_PageInfo::page_info(info_page);
  GPGC_PageInfo::Gens gen       = info->just_gen();

  guarantee((ref.is_new() && gen==GPGC_PageInfo::NewGen) ||
            (ref.is_old() && (gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen)),
"original ref space_id doesn't match PageInfo generation");

  if ( (ref.is_new() && _ignore_new) || (ref.is_old() && _ignore_old) ) {
    // If we find an unremapped ref in a space we're not verifying, remap it.
    if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
      ref = GPGC_Collector::get_forwarded_object(heapRef(ref));
      guarantee(ref.is_heap(), "verifier found ignored ref relocated to non-heap ref");

      obj       = ref.as_oop();
      trap_page = GPGC_Layout::addr_to_PageNum(obj);
      info_page = GPGC_Layout::mid_space_addr(obj) ? GPGC_Layout::addr_to_MidSpaceBasePageNum(obj) : trap_page;
      info      = GPGC_PageInfo::page_info(info_page);
      gen       = info->just_gen();

      guarantee((ref.is_new() && gen==GPGC_PageInfo::NewGen) ||
                (ref.is_old() && (gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen)),
"relocated ignored ref space_id doesn't match PageInfo generation");
      guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "verifier found double relocated ignored ref");
    }
  } else {
    guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "verifier found heapRef not relocated");
  }

  uint32_t ref_kid = ref.klass_id();
  uint32_t mem_kid = ref.as_oop()->mark()->kid();

  guarantee(ref_kid == mem_kid, "KID in objectRef metadata doesn't match KID in object header");

  if ( GPGC_Layout::mid_space_page(info_page) ) {
    uintptr_t aligned = uintptr_t(obj) >> GPGC_LogBytesMidSpaceObjectSizeAlignment
                                       << GPGC_LogBytesMidSpaceObjectSizeAlignment;
    guarantee(uintptr_t(obj) == aligned, "mis-aligned mid space object");
  }

  uint64_t flags = info->flags();

  if ( ref.is_new() ) {
    if ( _ignore_new ) {
      // Do nothing
    } else {
      if ( is_verify_marked(obj) ) { return; }

      // C2 used to not LVB refs until they are needed, so we could see some refs on the stack
      // that don't yet have updated NMT bits.  This shouldn't be true anymore.
      guarantee(GPGC_NMT::has_desired_new_nmt(ref), "Invalid NewGen NMT on ref"); 

      if ( (flags&GPGC_PageInfo::NoRelocateTLABFlags) == GPGC_PageInfo::NoRelocateTLABFlags ) {
        // Objects in NoRelocate TLABs aren't marked, because the allocation millicode doesn't mark them.
        // Do nothing.
      } else {
        guarantee(GPGC_Marks::is_any_marked_strong_live(obj), "found new_space live object marked dead");

        if ( GPGCAuditTrail ) {
          mark_id = GPGC_Marks::get_markid((HeapWord*)obj);  
          guarantee(GPGC_Marks::is_marked_through(obj), "found live new_space heapRef not marked through");
guarantee(mark_id!=0,"no markID for marked new_space oop");
        }
      }

      if (GPGC_Space::is_in_old_or_perm(p)) {
        guarantee(GPGC_Marks::is_card_marked(p), "old->new ref not card marked");
      }
    }
  } else {
    if ( _ignore_old ) {
      PageNum source_trap_page = GPGC_Layout::addr_to_PageNum(p);
      if ( GPGC_Layout::page_in_heap_range(source_trap_page) ) {
        PageNum        source_info_page = GPGC_Layout::mid_space_addr(p) ? GPGC_Layout::addr_to_MidSpaceBasePageNum(p) : source_trap_page;
        GPGC_PageInfo* source_info      = GPGC_PageInfo::page_info(source_info_page);
        if ( source_info->just_gen() == GPGC_PageInfo::NewGen ) {
          guarantee(GPGC_NMT::has_desired_old_nmt(ref), "OldSpace ref from NewGen page has invalid NMT");
        }
      }
    } else {
      if ( is_verify_marked(obj) ) { return; }

      // C2 used to not LVB refs until they are needed, so we could see some refs on the stack
      // that don't yet have updated NMT bits.  This shouldn't be true anymore.
      guarantee(GPGC_NMT::has_desired_old_nmt(ref), "Invalid OldGen NMT on ref"); 

      assert(!(flags & GPGC_PageInfo::TLABFlag), "We don't expect TLABs in old-space");

      guarantee(GPGC_Marks::is_any_marked_strong_live(obj), "found old_space live object marked dead");

      if ( GPGCAuditTrail ) {
        mark_id = GPGC_Marks::get_markid((HeapWord*)obj);  
        guarantee(GPGC_Marks::is_marked_through(obj), "found live old_space heapRef not marked through");
guarantee(mark_id!=0,"no markID for marked old_space oop");
      }
    }
  }

if(set_verify_mark(obj)){
    if ( GPGCVerifyRecursively ) {
      follow_ref(ref);
    } else {
      push_to_verify_stack(ref);
    }
  }
}


void GPGC_VerifyClosure::do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr)
{
  if ( base_ptr->is_stack() ) { return; }

  // Try to validate the derived pointer.
  objectRef base    = *base_ptr;
  objectRef derived = *derived_ptr;

  guarantee(base.not_null() && base.is_heap(), "verifier found invalid base heapRef for derived pointer");

  // Don't check derived oops in a space that's being ignored.
  if ( base.is_new() ) {
    if ( _ignore_new ) { return; }
  } else {
    if ( _ignore_old ) { return; }
  }

  guarantee(!GPGC_ReadTrapArray::is_remap_trapped(base), "verifier found base heapRef not relocated");
  guarantee(derived.space_id()==base.space_id(), "verifier found base and derived objectRef space_id mismatch");

  PageNum base_page = GPGC_Layout::addr_to_PageNum(base.as_oop());
  guarantee(GPGC_PageInfo::info_exists(base_page), "no PageInfo for base heapRef");
  // GPGC_PageInfo* base_info = GPGC_PageInfo::page_info(base_page);

  // Cliff says you can't depend on the derived pointer being anywhere close to the base ptr.
  //
  // Update: The maximum offset for a derived pointer is 2^31 (max array index) words, or 2^34 bytes.
  // We could assert for that easily enough.
  //
  // if (PageSpace::is_multi_page_block(base_page)) {
  //   guarantee((base_page<=derived_page)&&((base_page+base_info->block_size())>derived_page),
  //             "derived not from base block");
  // } else {
  //   guarantee(base_page==derived_page, "derived not from base page");
  // }
}


// Just like do_oop, but don't assert the NMT state of the objectRef.
void GPGC_VerifyClosure::do_weak_oop(objectRef* p)
{
assert0(Thread::current()->is_GC_task_thread());

  // TODO: assert that both collectors are halted, so objects aren't getting moved
  // while I'm trying to do a verify here.

  objectRef ref = PERMISSIVE_UNPOISON(*p, p);
  long      mark_id;

  if ( ref.is_null() ) { return; }

  guarantee(ref.is_heap(), "Found non-heap weak ref in verify");

oop obj=ref.as_oop();
  PageNum   trap_page = GPGC_Layout::addr_to_PageNum(obj);

  if ( (ref.is_new() && _ignore_new) || (ref.is_old() && _ignore_old) ) {
    // If we find an unrelocated ref in a space we're not verifying, relocate it.
    if ( GPGC_ReadTrapArray::is_remap_trapped(ref) ) {
      ref = GPGC_Collector::get_forwarded_object(heapRef(ref));
      guarantee(ref.is_heap(), "verifier found ignored weak ref relocated to non-heap ref");

      obj       = ref.as_oop();
      trap_page = GPGC_Layout::addr_to_PageNum(obj);

      guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "verifier found double relocated ignored weak ref");
    }
  } else {
    guarantee(!GPGC_ReadTrapArray::is_remap_trapped(ref), "verifier found weak heapRef not relocated");
  }

  if ( ref.is_new() ) {
    if ( _ignore_new ) {
      // nothing
    } else {
      if ( is_verify_marked(obj) ) { return; }

      guarantee(GPGC_Marks::is_any_marked_strong_live(obj), "found new_space live weak object marked dead");

      if ( GPGCAuditTrail ) {
        mark_id = GPGC_Marks::get_markid((HeapWord*)obj);  
        guarantee(GPGC_Marks::is_marked_through(obj), "found live new_space weak ref not marked through");
guarantee(mark_id!=0,"no markID for marked new_space weak oop");
      }

      if (GPGC_Space::is_in_old_or_perm(p)) {
        guarantee(GPGC_Marks::is_card_marked(p), "old->new weak ref not card marked");
      }
    }
  } else {
    if ( _ignore_old ) {
    } else {
      if ( is_verify_marked(obj) ) { return; }

      guarantee(GPGC_Marks::is_any_marked_strong_live(obj), "found old_space live weak object marked dead");

      if ( GPGCAuditTrail ) {
        mark_id = GPGC_Marks::get_markid((HeapWord*)obj);  
        guarantee(GPGC_Marks::is_marked_through(obj), "found live old_space weak ref not marked through");
guarantee(mark_id!=0,"no markID for marked old_space weak oop");
      }
    }
  }

if(set_verify_mark(obj)){
    if ( GPGCVerifyRecursively ) {
      follow_ref(ref);
    } else {
      push_to_verify_stack(ref);
    }
  }
}


bool GPGC_VerifyClosure::verify_empty_stacks()
{
  for ( long i=0; i<GPGCVerifyThreads; i++ ) {
    if ( _per_thread[i]._drain_stack->_refs[0].not_null() ) {
      return false;
    }
    if ( _per_thread[i]._fill_stack_top != 0 ) {
      return false;
    }
  }

  for ( long i=0; i<MAX_WORK_LISTS; i++ ) {
    if ( ! _work_lists[i].marking_queue.is_empty() ) {
      return false;
    }
  }

if(_gc_task_stack!=NULL){
    return false;
  }

  return true;
}


void GPGC_VerifyClosure::verify_marking(TimeDetailTracer* tdt, bool ignore_new, bool ignore_old, const char* tag)
{
  Thread* thread = Thread::current();

  guarantee(GPGC_Safepoint::is_at_safepoint(), "GPGCVerifyHeap only at a safepoint");
guarantee(thread->is_GenPauselessGC_thread(),"GPGCVerifyHeap only by GPGC thread");

  _ignore_new = ignore_new;
  _ignore_old = ignore_old;

  {
    DetailTracer dt(tdt, false, "%s Start: RefBlocks: %d requested, %d alloced, %d in free list", tag,
                    GPGC_MarkingQueue::blocks_requested(),
                    GPGC_MarkingQueue::blocks_allocated(),
                    GPGC_MarkingQueue::count_free_blocks());
  }

  reset();

gclog_or_tty->flush();

  ResourceMark rm;
  HandleMark   hm;

  Universe::heap()->ensure_parsability(true);

  if ( _ignore_old ) {
    _constantPoolKlassOop = (klassOop) GPGC_Collector::remap_only(Universe::constantPoolKlassObj_addr()).as_oop();
  }


  // Initial sanity checks before kicking off the parallel verify:
  if ( ! ignore_new ) {
    guarantee(GPGC_NewCollector::jlr_handler()->none_captured(), "NewGC JLR handler has captured refs");
    GPGC_GCManagerNewStrong::verify_empty_ref_lists();
  }
  if ( ! ignore_old ) {
    guarantee(GPGC_OldCollector::jlr_handler()->none_captured(), "OldGC JLR handler has captured refs");
    GPGC_GCManagerOldStrong::verify_empty_ref_lists();
  }


  // Verify strong-live marks:
  if ( GPGCVerifyHeap && !ignore_old ) {
    GPGC_Interlock::acquire_interlock(tdt, GPGC_Interlock::SymbolStringTable_verify, "VerifySymbolsTask:");
  }
 
  {
    DetailTracer dt(tdt, false, "%s strong verify", tag);

    // A task for each thread:
    for ( JavaThread* jt=Threads::first(); jt!=NULL; jt=jt->next() ) {
      push_work_stack(new GPGC_Verify_ThreadTask(jt));
    }
    push_work_stack(new GPGC_Verify_VMThreadTask());

    if ( ! ignore_old ) {
      push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::symbol_table_strong_refs));
      push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::string_table_strong_refs));
    }
  
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::universe));

    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::jni_handles));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::object_synchronizer));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::management));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::jvmti_export));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::system_dictionary));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::vm_symbols));

    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::code_cache));

    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::symbol_table));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::string_table));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::newgc_ref_lists));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::oldgc_ref_lists));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::arta_objects));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::klass_table));
    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::code_cache_oop_table));

    // Wait for the tasks to be completed.
    run_work_stack();

    // old-collector single-threaded verification of the klass-chain
    {
      if ( !_ignore_old ) {
        DetailTracer dt(tdt, false, "%s verify Klass chain", tag);
MutexLocker ml(Compile_lock,Thread::current());
        instanceKlass::gpgc_verify_klass_chain(Klass::cast(SystemDictionary::object_klass()));
      }
    }
  }
  if ( GPGCVerifyHeap && !ignore_old ) {
    GPGC_Interlock::release_interlock(tdt, GPGC_Interlock::SymbolStringTable_verify, "VerifySymbolsTask:");
  }

guarantee(verify_empty_stacks(),"Verify marking stack should be empty. (1)");

  // Verify marks less live than strong-live:
  {
    DetailTracer dt(tdt, false, "%s weak verify", tag);

    push_work_stack(new GPGC_Verify_RootsTask(GPGC_Verify_RootsTask::weak_jni_handles));

    // Wait for the tasks to be completed.
    run_work_stack();
  }

guarantee(verify_empty_stacks(),"Verify marking stack should be empty. (2)");

_constantPoolKlassOop=NULL;

  {
    DetailTracer dt(tdt, false, "%s End: RefBlocks: %d requested, %d alloced, %d in free list", tag,
                    GPGC_MarkingQueue::blocks_requested(),
                    GPGC_MarkingQueue::blocks_allocated(),
                    GPGC_MarkingQueue::count_free_blocks());
  }
}


void GPGC_VerifyClosure::follow_ref(objectRef ref)
{
  oop o = ref.as_oop();

  guarantee(GPGC_Space::is_in(o), "Ref on verify stack not in GPGC_Space");
  guarantee(ref.klass_id() == (uint)o->mark()->kid(), "metadata KID doesn't match object header KID");

  klassRef klassref = UNPOISON_KLASSREF(*(o->klass_addr()));
  guarantee(GPGC_Space::is_in(klassref.as_oop()), "Ref Klass on verify stack not in GPGC_Space");

  o->oop_iterate_header(this);

  if ( _ignore_old ) {
    klassOop klass     = (klassOop) GPGC_Collector::remap_only(o->klass_addr()).as_oop();
Klass*blueprint=klass->klass_part();
    if ( klass == _constantPoolKlassOop ) {
      // o->oop_iterate() for constantPoolKlass has a problem with unremapped
      // components, so there's a special GPGC function to cope.
      ((constantPoolKlass*)blueprint)->GPGC_verify_oop_oop_iterate(o, this);
    } else {
      blueprint->oop_oop_iterate(o, this);
    }
  } else {
o->oop_iterate(this);
  }
}


void GPGC_VerifyClosure::drain_stack()
{
  while (true) {
    // Traverse through the current marking stack.
    for ( long i=0; i<GPGC_MarkingQueue::RefBlockLength; i++ ) {
      objectRef r = _data->_drain_stack->_refs[i];
      if ( r.is_null() ) break;

follow_ref(r);
    }

    // The drain_stack is done.  See if there's more local work to do.
    if ( _data->_fill_stack_top > 0 ) {
      GPGC_MarkingQueue::delete_block(_data->_drain_stack);

      _data->_drain_stack = _data->_fill_stack;
      if ( _data->_fill_stack_top < GPGC_MarkingQueue::RefBlockLength ) {
        _data->_drain_stack->_refs[_data->_fill_stack_top] = nullRef;
      }

      _data->_fill_stack_top = 0;
      _data->_fill_stack     = GPGC_MarkingQueue::new_block();

      continue;
    }

    // drain_stack and fill_stack are both empty.  Try and get a marking stack
    // with work off one of the work lists.
    GPGC_MarkingQueue::RefBlock* stack = pop_marking_stack();
if(stack!=NULL){
      GPGC_MarkingQueue::delete_block(_data->_drain_stack);
      _data->_drain_stack = stack;

      continue;
    }

    _data->_drain_stack->_refs[0] = nullRef;

    // The local stacks are empty, and one sweep through the work lists didn't
    // get a stack with work to do, so return.  Marking my be complete.
    return;
  }
}


bool GPGC_VerifyClosure::get_marking_stack()
{
  assert0( _data->_fill_stack_top == 0 );

  GPGC_MarkingQueue::RefBlock* stack = pop_marking_stack();
if(stack!=NULL){
    increment_working();

    GPGC_MarkingQueue::delete_block(_data->_drain_stack);
    _data->_drain_stack = stack;
 
    return true;
  }

  return false; 
}


GPGC_MarkingQueue::RefBlock* GPGC_VerifyClosure::pop_marking_stack()
{
  GPGC_MarkingQueue::RefBlock* stack;

  long mask   = MAX_WORK_LISTS - 1;
  long cursor = os::elapsed_counter() & mask;  // Pick a random starting point

  for ( long j=0; j<MAX_WORK_LISTS; j++ ) {
    stack = _work_lists[cursor].marking_queue.dequeue_block();
if(stack!=NULL){
      assert0( stack->_next == NULL );
      assert0( stack->_refs[0].not_null() );
      return stack;
    }
    cursor = (cursor+1) & mask;
  }

  return NULL; 
}


void GPGC_VerifyClosure::push_marking_stack(GPGC_MarkingQueue::RefBlock* stack)
{
  long mask  = MAX_WORK_LISTS - 1;
  long indx  = os::elapsed_counter() & mask;  // Pick a random work list

  _work_lists[indx].marking_queue.enqueue_block(stack);
}


void GPGC_VerifyClosure::increment_working()
{
  long old_w;
  long new_w;

  do {
    old_w = _working;
    new_w = old_w + 1;
  } while ( intptr_t(old_w) != Atomic::cmpxchg_ptr(intptr_t(new_w), (intptr_t*)&_working, intptr_t(old_w)) );

guarantee(old_w>=0,"verify working underflow (1)");
  guarantee(new_w<=GPGCVerifyThreads, "verify working overflow (1)");
}


void GPGC_VerifyClosure::decrement_working()
{
  long old_w;
  long new_w;

  do {
    old_w = _working;
    new_w = old_w - 1;
  } while ( intptr_t(old_w) != Atomic::cmpxchg_ptr(intptr_t(new_w), (intptr_t*)&_working, intptr_t(old_w)) );

  guarantee(old_w<=GPGCVerifyThreads, "verify working overflow (2)");
guarantee(new_w>=0,"verify working underflow (2)");
}


void GPGC_VerifyClosure::run_work_stack()
{
  guarantee(GPGC_Safepoint::is_at_safepoint(),  "GPGCVerifyHeap only at a safepoint");
guarantee(Thread::current()->is_GenPauselessGC_thread(),"GPGCVerifyHeap only by GPGC thread");
  // Setup, and notify the verify task threads to go.
  {
MutexLocker ml(GPGC_VerifyTask_lock);

guarantee(_working==0,"working threads when none expected");
guarantee(_active_workers==0,"Active verifiers when none expected");

    _working        = GPGCVerifyThreads;
    _active_workers = GPGCVerifyThreads;

    _wakeup_count ++;

GPGC_VerifyTask_lock.notify_all();
  }

  // Wait for the verify task threads to finish
  {
MutexLocker ml(GPGC_VerifyNotify_lock);
    while ( _active_workers > 0 ) {
GPGC_VerifyNotify_lock.wait();
    }
  }

guarantee(_working==0,"working threads when none expected (2)");
guarantee(_active_workers==0,"Active verifiers when none expected (2)");
}


void GPGC_VerifyClosure::task_thread_loop(GCTaskThread* thread, uint which)
{
thread->set_gc_mode(false);
thread->set_gcthread_lvb_trap_vector();
thread->set_wakeup_count(0);

  while (true) {
    // Once per verification task set, we release all handles and resources allocated:
    HandleMark   hm;
    ResourceMark rm;

    // Wait to be told to start working.
    {
MutexLocker ml(GPGC_VerifyTask_lock);
      while ( thread->wakeup_count() == _wakeup_count ) {
GPGC_VerifyTask_lock.wait();
      }
    }
    thread->set_wakeup_count(_wakeup_count);

    // Grab tasks and execute them until the list is empty
GCTask*task;
    while ( NULL != (task = pop_work_stack()) ) {
      task->do_it(NULL, which);
    }

    // Once there are no more tasks on the work stack, each thread helps drain the marking stacks.
complete_work(which);

    // And then we finish up by letting the GPGC_Thread that kicked it all off know
    // we're done.
    {
MutexLocker ml(GPGC_VerifyNotify_lock);
      _active_workers --;
      if ( _active_workers == 0 ) {
GPGC_VerifyNotify_lock.notify();
      }
    }
  }
}


void GPGC_VerifyClosure::complete_work(uint which)
{
  GPGC_VerifyClosure verify_closure((long)which);

  verify_closure.drain_stack();
  decrement_working();

  while ( _working > 0 ) {
    if ( verify_closure.get_marking_stack() ) {
      verify_closure.drain_stack();
      decrement_working();
    }
  }
}


void GPGC_VerifyClosure::push_work_stack(GCTask* task)
{
  task->set_newer(NULL);
  task->set_older(_gc_task_stack);
_gc_task_stack=task;
}


GCTask* GPGC_VerifyClosure::pop_work_stack()
{
  GCTask* old_top;
  GCTask* new_top;

  do {
    old_top = _gc_task_stack;

if(old_top==NULL){
      return NULL;
    }

    new_top = old_top->older();
  } while ( intptr_t(old_top) != Atomic::cmpxchg_ptr(intptr_t(new_top), (intptr_t*)&_gc_task_stack, intptr_t(old_top)) );

  old_top->set_older(NULL);

  return old_top;
}
