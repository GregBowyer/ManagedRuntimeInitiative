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


#include "allocatedObjects.hpp"
#include "artaObjects.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "gpgc_cardTable.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_gcManagerNewFinal.hpp"
#include "gpgc_gcManagerNewStrong.hpp"
#include "gpgc_gcManagerOldFinal.hpp"
#include "gpgc_gcManagerOldReloc.hpp"
#include "gpgc_gcManagerOldStrong.hpp"
#include "gpgc_javaLangRefHandler.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_markAlgorithms.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_onePageSpace.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.inline.hpp"
#include "gpgc_space.hpp"
#include "gpgc_tasks.hpp"
#include "iterator.hpp"
#include "jniHandles.hpp"
#include "jvmtiExport.hpp"
#include "liveObjects.hpp"
#include "management.hpp"
#include "oopTable.hpp"
#include "safepoint.hpp"
#include "symbolTable.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "universe.hpp"
#include "vmSymbols.hpp"
#include "vmThread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

//
// GPGC_OldGC_MarkRootsTaskConcurrent
//

void GPGC_OldGC_MarkRootsTaskConcurrent::do_it(uint64_t which) {

  GPGC_GCManagerOldStrong* gcm           = GPGC_GCManagerOldStrong::get_manager(which);
  OopClosure*              roots_closure = gcm->mark_and_push_closure();

  SystemDictionary::GPGC_always_strong_oops_do_concurrent(roots_closure);
  if ( ProfileAllocatedObjects) {
    AllocatedObjects::oops_do(roots_closure);
  }
  if ( ProfileLiveObjects) {
    LiveObjects::oops_do(roots_closure);
  }
}


const char*GPGC_OldGC_MarkRootsTaskConcurrent::name(){
  return (char*)"gpgc-old-mark-roots-task-concurrent";
}


//
// GPGC_NewGC_MarkRootsTask
//


class GPGC_VerifyNoNewClosure: public OopClosure
{
  public:
    void do_oop(objectRef* p) {
                                objectRef ref = UNPOISON_OBJECTREF(*p, p);
                                guarantee(!ref.is_new(), "Found NewGen ref where not expected");
                              }
    void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr)   { ShouldNotReachHere(); }
};


void GPGC_NewGC_MarkRootsTask::do_it(uint64_t which) {
  GPGC_GCManagerNewStrong* gcm               = GPGC_GCManagerNewStrong::get_manager(which);
  OopClosure*              new_roots_closure = gcm->new_root_mark_push_closure();
  OopClosure*              nto_roots_closure = gcm->nto_root_mark_push_closure();
  GPGC_VerifyNoNewClosure  verify_no_new;
OopClosure*general_roots_closure;
OopClosure*only_old_roots_closure;

  if ( GPGC_NewCollector::mark_old_space_roots() ) {
    // Doing a NewToOldGC, so use the NewToOldGC root marking closure for most roots:
    general_roots_closure  = nto_roots_closure;
    only_old_roots_closure = nto_roots_closure;
  } else {
    // Doing a NewGC, so use the NewGC root marking closure for all roots:
    general_roots_closure  = new_roots_closure;
    // Ensure that roots that should be only old really are only old:
    only_old_roots_closure = &verify_no_new;
  }

  switch (_root_type) {
    case vm_symbols:          vmSymbols::oops_do         (only_old_roots_closure); break;

    case universe:            Universe::oops_do          (general_roots_closure);  break;
    case jni_handles:         JNIHandles::oops_do        (general_roots_closure);  break;
    case object_synchronizer: ObjectSynchronizer::oops_do(general_roots_closure);  break;
    case jvmti:               JvmtiExport::oops_do       (general_roots_closure);  break;
    case management:          Management::oops_do        (general_roots_closure);  break;

    case system_dictionary:   SystemDictionary::oops_do  (new_roots_closure);      break;
    case arta_objects:        ArtaObjects::oops_do       (new_roots_closure);      break;

default:fatal("Unknown root type");
  }
}


const char*GPGC_NewGC_MarkRootsTask::name(){
  switch (_root_type) {
    case universe:            return (char*)"gpgc-newgc-mark-roots-task-universe";
    case system_dictionary:   return (char*)"gpgc-newgc-mark-roots-task-sysdict";
    case vm_symbols:          return (char*)"gpgc-newgc-mark-roots-task-vmsyms";
    case jni_handles:         return (char*)"gpgc-newgc-mark-roots-task-jnihandles";
    case object_synchronizer: return (char*)"gpgc-newgc-mark-roots-task-synchronizer";
    case jvmti:               return (char*)"gpgc-newgc-mark-roots-task-jvmti";
    case arta_objects:        return (char*)"gpgc-newgc-mark-roots-task-arta-objs";
    case management:          return (char*)"gpgc-newgc-mark-roots-task-management";

    default: { fatal("Unknown root type"); return (char*)"error: Unknown root type"; }
  }
}


//
// GPGC_NewGC_VMThreadMarkTask
//

void GPGC_NewGC_VMThreadMarkTask::do_it(uint64_t which) {
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(which);

  if ( GPGC_NewCollector::mark_old_space_roots() ) {
    // Doing a NewToOldGC, so use the NewToOldGC root marking closure:
    VMThread::vm_thread()->oops_do( gcm->nto_root_mark_push_closure() );
  } else {
    // Doing a NewGC, so use the NewGC root marking closure:
    VMThread::vm_thread()->oops_do( gcm->new_root_mark_push_closure() );
  }

  // The VMThread doesn't have its registers modified by GC, so we don't need to
  // do an os::make_remote_walkable(_vm_thread).
}


//
// GPGC_OldGC_VMThreadMarkTask
//

void GPGC_OldGC_VMThreadMarkTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);

  VMThread::vm_thread()->oops_do( gcm->mark_and_push_closure() );

  // The VMThread doesn't have its registers modified by GC, so we don't need to
  // do an os::make_remote_walkable(_vm_thread).
}


//
// GPGC_InitReadTrapArrayTask
//

void GPGC_InitReadTrapArrayTask::do_it(uint64_t which) {
  for ( PageNum page=_task_start_page; page<_task_end_page; page+=_block_size ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    // skip over the Unmapped pages from the previous cycles 
    if ( state > GPGC_PageInfo::Unmapped ) {
      bool is_new_gen   = (gen == GPGC_PageInfo::NewGen);
      long upcoming_nmt = is_new_gen ? GPGC_NMT::upcoming_new_nmt_flag()
                                     : GPGC_NMT::upcoming_old_nmt_flag();

      GPGC_ReadTrapArray::set_trap_state(page, _block_size, is_new_gen, state, _in_large_space, upcoming_nmt);
    }
  }
}


//
// GPGC_NewGC_UpdateRelocateStatesTask
//

void GPGC_NewGC_UpdateRelocateStatesTask::do_page_array(GPGC_PopulationArray* relocation_array)
{
  uint64_t chunk_size = relocation_array->max_cursor() / _sections;
  uint64_t start      = _section * chunk_size;
  uint64_t end        = start + chunk_size;

  if ( (_section+1) == _sections ) {
    end = relocation_array->max_cursor();
  }

  for ( uint64_t cursor=start; cursor<end; cursor++ ) {
    GPGC_Space::new_gc_relocating_page( relocation_array->page(cursor) );
  }
}

void GPGC_NewGC_UpdateRelocateStatesTask::do_block_array(GPGC_PopulationArray* relocation_array)
{
  uint64_t chunk_size = relocation_array->max_cursor() / _sections;
  uint64_t start      = _section * chunk_size;
  uint64_t end        = start + chunk_size;

  if ( (_section+1) == _sections ) {
    end = relocation_array->max_cursor();
  }

  for ( uint64_t cursor=start; cursor<end; cursor++ ) {
    GPGC_Space::new_gc_relocated_block( relocation_array->page(cursor) );
  }
}

void GPGC_NewGC_UpdateRelocateStatesTask::do_it(uint64_t which) {
  do_page_array(GPGC_NewCollector::page_pops()->small_space_to_new());
  do_page_array(GPGC_NewCollector::page_pops()->small_space_to_old());

  do_page_array(GPGC_NewCollector::page_pops()->mid_space_to_new());
  do_page_array(GPGC_NewCollector::page_pops()->mid_space_to_old());

  do_block_array(GPGC_NewCollector::page_pops()->large_space_to_old());
}



//
// GPGC_OldGC_UpdateRelocateStatesTask
//

void GPGC_OldGC_UpdateRelocateStatesTask::do_page_array(GPGC_PopulationArray* relocation_array)
{
  uint64_t chunk_size = relocation_array->max_cursor() / _sections;
  uint64_t start      = _section * chunk_size;
  uint64_t end        = start + chunk_size;

  if ( (_section+1) == _sections ) {
    end = relocation_array->max_cursor();
  }

  for ( uint64_t cursor=start; cursor<end; cursor++ ) {
    GPGC_Space::old_gc_relocating_page( relocation_array->page(cursor) );
  }
}

void GPGC_OldGC_UpdateRelocateStatesTask::do_block_array(GPGC_PopulationArray* relocation_array)
{
  uint64_t chunk_size = relocation_array->max_cursor() / _sections;
  uint64_t start      = _section * chunk_size;
  uint64_t end        = start + chunk_size;

  if ( (_section+1) == _sections ) {
    end = relocation_array->max_cursor();
  }

  for ( uint64_t cursor=start; cursor<end; cursor++ ) {
    GPGC_Space::old_gc_relocated_block( relocation_array->page(cursor) );
  }
}

void GPGC_OldGC_UpdateRelocateStatesTask::do_it(uint64_t which) {

  GPGC_PopulationArray* small_space = (_gen == GPGC_PageInfo::OldGen) ? GPGC_OldCollector::page_pops()->small_space_in_old()
                                                                      : GPGC_OldCollector::page_pops()->small_space_in_perm();
  
  GPGC_PopulationArray* mid_space   = (_gen == GPGC_PageInfo::OldGen) ? GPGC_OldCollector::page_pops()->mid_space_in_old()
                                                                      : GPGC_OldCollector::page_pops()->mid_space_in_perm();

  do_page_array(small_space);
  do_page_array(mid_space);

  // TODO: eventually we need to deal with large_space_in_* for compaction.
}


//
// GPGC_MultiPageScanCardMarksTask
//

void GPGC_MultiPageScanCardMarksTask::do_it(uint64_t which) {
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(which);

  GPGC_CardTable::scan_page_multi_space(gcm, _page, _chunk, _chunks);
}


//
// GPGC_OnePageScanCardMarksTask
//

void GPGC_OnePageScanCardMarksTask::do_it(uint64_t which) {
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(which);

  // First scan small space.
  PageNum top = GPGC_Space::one_space_small()->start_page() + _section;

  while ( top < GPGC_Space::one_space_small()->top_page() ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(top);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen(gen_n_state);

    if ( gen==GPGC_PageInfo::InvalidGen || gen==GPGC_PageInfo::NewGen ) {
top=top+_sections;
      continue;
    }

    assert0(gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen);

    if ( state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating || state==GPGC_PageInfo::Relocating ) {
      GPGC_CardTable::scan_cardmarks_for_page(gcm, top, state);
    }

top=top+_sections;
  }

  // Next scan mid space:
  top = GPGC_Space::one_space_mid()->start_page() + (_section<<LogPagesPerMidSpaceBlock);
  while ( top < GPGC_Space::one_space_mid()->top_page() ) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(top);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen(gen_n_state);

    if ( gen==GPGC_PageInfo::InvalidGen || gen==GPGC_PageInfo::NewGen ) {
      top = top + (_sections<<LogPagesPerMidSpaceBlock);
      continue;
    }

    assert0(gen==GPGC_PageInfo::OldGen || gen==GPGC_PageInfo::PermGen);
    assert (state!=GPGC_PageInfo::Relocating, "There shouldn't be any Relocating state mid space pages.");

    if ( state==GPGC_PageInfo::Allocated || state==GPGC_PageInfo::Allocating ) {
      PageNum last_page = GPGC_Layout::addr_to_PageNum(info->top()-1);

      assert(last_page < (top+PagesPerMidSpaceBlock), "mid space page top overflow");

      for ( PageNum page=top; page<=last_page; page++ ) {
        GPGC_CardTable::scan_cardmarks_for_page(gcm, page, state);
      }
    }

    top = top + (_sections<<LogPagesPerMidSpaceBlock);
  }
}


//
// GPGC HeapIterateTask
//

void GPGC_OnePageHeapIterateTask::do_it(uint64_t which) {
  // this needs to be reimplemented.. commenting out for now
  // to get it to compile
assert(SafepointSynchronize::is_at_safepoint(),"JvmtiHeapIterate only at a safepoint");

  PageNum start_page = _one_space->start_page();
  PageNum top_page   = _one_space->top_page();

  long section_size     = (top_page - start_page) / _sections;
  assert0(section_size >= 0);
  if (section_size == 0) {
    section_size = 1;
  }
  PageNum section_start = start_page + (_section * section_size);
  PageNum section_end   = section_start + section_size;
  if (section_start > top_page) {
    return;    
  }
  if ((_section + 1) == _sections) {
    section_end = top_page; 
  }
  if (section_end > top_page) {
    section_end = top_page;
  }

  long block_size = _one_space->block_size();

  for (PageNum page = section_start; page < section_end; page=+ block_size) {
    GPGC_PageInfo*        info        = GPGC_PageInfo::page_info(page);
    uint64_t              gen_n_state = info->gen_and_state();
    GPGC_PageInfo::States state       = GPGC_PageInfo::decode_state(gen_n_state);
    GPGC_PageInfo::Gens   gen         = GPGC_PageInfo::decode_gen  (gen_n_state);

    if (state != GPGC_PageInfo::Allocating && state != GPGC_PageInfo::Allocated) continue;

HeapWord*page_top=info->top();
    HeapWord* addr     = GPGC_Layout::PageNum_to_addr(page);

    Atomic::read_barrier();

    while (addr < page_top) {
      // If the object is dead, then its klass would be pointing to 0x7e1ea5ed
      klassOop koop = (klassOop) GPGC_Collector::old_remap_nmt_and_rewrite((objectRef*)(oop(addr)->klass_addr())).as_oop();
Klass*k=koop->klass_part();

      // Pass back only live objects. Passing dead objects seems to cause a
      // lot of misery.
      if (GPGC_Marks::is_any_marked_strong_live(oop(addr))) {
_closure->do_object(oop(addr));
      }

      addr += oop(addr)->GC_size_given_klass(k);
    }
  }
}

void GPGC_MultiPageHeapIterateTask::do_it(uint64_t which) {
assert(SafepointSynchronize::is_at_safepoint(),"JvmtiHeapIterate only at a safepoint");



  GPGC_PageInfo*      info        = GPGC_PageInfo::page_info(_page);
  uint64_t            gen_n_state = info->gen_and_state();
  GPGC_PageInfo::Gens gen         = GPGC_PageInfo::decode_gen  (gen_n_state);
HeapWord*page_top=info->top();
  HeapWord*           addr        = GPGC_Layout::PageNum_to_addr(_page);

  Atomic::read_barrier();

if(addr<page_top){
    // If the object is dead, then its klass would be pointing to 0x7e1ea5ed
    klassOop koop = (klassOop) GPGC_Collector::old_remap_nmt_and_rewrite((objectRef*)(oop(addr)->klass_addr())).as_oop();
Klass*k=koop->klass_part();

    // Pass back only live objects. Passing dead objects seems to cause a
    // lot of misery.
    if (GPGC_Marks::is_any_marked_strong_live(oop(addr))) {
_closure->do_object(oop(addr));
    }

    addr += oop(addr)->GC_size_given_klass(k);
  }

  assert0(addr == page_top);
}


//
// GPGC_NewGC_StrongMarkTask
//

GPGC_NewGC_StrongMarkTask::GPGC_NewGC_StrongMarkTask()
{
  DEBUG_ONLY( long working = GPGC_GCManagerNew::working_count(); )
  assert0( working>=0 && working<(int64_t)GenPauselessNewThreads );
  GPGC_GCManagerNew::increment_working_count();
}

void GPGC_NewGC_StrongMarkTask::do_it(uint64_t which)
{
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(which);
  GPGC_MarkAlgorithm::drain_and_steal_stacks(gcm);
}


//
// GPGC_OldGC_StrongMarkTask
//

GPGC_OldGC_StrongMarkTask::GPGC_OldGC_StrongMarkTask()
{
  DEBUG_ONLY( long working = GPGC_GCManagerOld::working_count(); )
  assert0( working>=0 && working<(int64_t)GenPauselessOldThreads );
  GPGC_GCManagerOld::increment_working_count();
}

void GPGC_OldGC_StrongMarkTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);
  GPGC_MarkAlgorithm::drain_and_steal_stacks(gcm);
}


//
// GPGC_NewGC_FinalMarkTask
//

GPGC_NewGC_FinalMarkTask::GPGC_NewGC_FinalMarkTask()
{
  DEBUG_ONLY( long working = GPGC_GCManagerNew::working_count(); )
  assert0( working>=0 && working<(int64_t)GenPauselessNewThreads );
  GPGC_GCManagerNew::increment_working_count();
}

void GPGC_NewGC_FinalMarkTask::do_it(uint64_t which)
{
  GPGC_GCManagerNewFinal* gcm = GPGC_GCManagerNewFinal::get_manager(which);
  GPGC_MarkAlgorithm::drain_and_steal_stacks(gcm);
}


//
// GPGC_OldGC_FinalMarkTask
//

GPGC_OldGC_FinalMarkTask::GPGC_OldGC_FinalMarkTask()
{
  DEBUG_ONLY( long working = GPGC_GCManagerOld::working_count(); )
  assert0( working>=0 && working<(int64_t)GenPauselessOldThreads );
  GPGC_GCManagerOld::increment_working_count();
}

void GPGC_OldGC_FinalMarkTask::do_it(uint64_t which)
{
  GPGC_GCManagerOldFinal* gcm = GPGC_GCManagerOldFinal::get_manager(which);
  GPGC_MarkAlgorithm::drain_and_steal_stacks(gcm);
}


//
// GPGC_NewGC_SidebandForwardingInitTask
//

void GPGC_NewGC_SidebandForwardingInitTask::do_it(uint64_t which) {
  GPGC_NewCollector::sideband_forwarding_init(_work_unit);
}


//
// GPGC_OldGC_SidebandForwardingInitTask
//

void GPGC_OldGC_SidebandForwardingInitTask::do_it(uint64_t which) {
  GPGC_OldCollector::sideband_forwarding_init(_work_unit);
}


//
// GPGC_OldGC_UnloadDictionarySectionTask
//

void GPGC_OldGC_UnloadDictionarySectionTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);
  SystemDictionary::GPGC_unload_section(gcm, _section, _sections);
}


//
// GPGC_OldGC_UnloadLoaderConstraintSectionTask
//

void GPGC_OldGC_UnloadLoaderConstraintSectionTask::do_it(uint64_t which) {
  SystemDictionary::GPGC_purge_loader_constraints_section(_section, _sections);
}


//
// GPGC_OldGC_StealRevisitKlassTask
//

GPGC_OldGC_StealRevisitKlassTask::GPGC_OldGC_StealRevisitKlassTask()
{
  DEBUG_ONLY( long working = GPGC_GCManagerOld::working_count(); )
  assert0( working>=0 && working<(int64_t)GenPauselessOldThreads );
  GPGC_GCManagerOld::increment_working_count();
}

void GPGC_OldGC_StealRevisitKlassTask::do_it(uint64_t which) {
  // Try and drain the stack once, before we start stealing work.
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);

  gcm->drain_revisit_stack();
assert(gcm->revisit_klass_stack()->size()==0,"stack not empty 1 !");

  // I'm not a working thread anymore when my stacks are drained.
  gcm->decrement_working_count();

Klass*k;
  int    random_seed = 17;

  // This work stealing doesn't stop until everyone is out of work.
  while ( gcm->working_count() > 0 ) {
    if ( GPGC_GCManagerOldStrong::steal_revisit_klass(which, &random_seed, k) ) {
      // I'm a working thread again when I'm able to steal some work:
      gcm->increment_working_count();
      
      gcm->revisit_klass(k);

      // I'm not a working thread anymore when my stacks are drained.
      gcm->decrement_working_count();
    }
  }

assert(gcm->revisit_klass_stack()->size()==0,"stack not empty 2 !");
}


//
// GPGC_OldGC_UnlinkKlassTableTask
//

void GPGC_OldGC_UnlinkKlassTableTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);
  KlassTable::GPGC_unlink(gcm, _section, _sections);
}


//
// GPGC_OldGC_UnlinkCodeCacheOopTask
//

void GPGC_OldGC_UnlinkCodeCacheOopTableTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);
  CodeCacheOopTable::GPGC_unlink(gcm, _section, _sections);
} 


//
// GPGC_OldGC_UnlinkWeakRootSectionTask
//

void GPGC_OldGC_UnlinkWeakRootSectionTask::do_it(uint64_t which) {
  switch (_root_type) {
    case symbol_table:
      SymbolTable::GPGC_unlink_section(_section, _sections);
      break;

    case string_table:
      StringTable::GPGC_unlink_section(_section, _sections);
      break;

    default:
fatal("Unknown root type");
  }
}


//
// GPGC_NewGC_RelocateMidPagesTask
//

void GPGC_NewGC_RelocateMidPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerNewReloc* gcm = GPGC_GCManagerNewReloc::get_manager(which);
  GPGC_NewCollector::relocate_mid_pages(gcm, _work_unit, _stripe);
}


//
// GPGC_NewGC_HealMidPagesTask
//

void GPGC_NewGC_HealMidPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerNewReloc* gcm = GPGC_GCManagerNewReloc::get_manager(which);
  GPGC_NewCollector::heal_mid_pages(gcm, _stripe);
}


//
// GPGC_NewGC_RelocateSmallPagesTask
//

void GPGC_NewGC_RelocateSmallPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerNewReloc* gcm = GPGC_GCManagerNewReloc::get_manager(which);
  GPGC_NewCollector::relocate_small_pages(gcm, _work_unit);
}


//
// GPGC_OldGC_RelocateMidPagesTask
//

void GPGC_OldGC_RelocateMidPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerOldReloc* gcm = GPGC_GCManagerOldReloc::get_manager(which);
  GPGC_OldCollector::relocate_mid_pages(gcm, _work_unit, _relocation_array, _stripe);
}


//
// GPGC_OldGC_HealMidPagesTask
//

void GPGC_OldGC_HealMidPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerOldReloc* gcm = GPGC_GCManagerOldReloc::get_manager(which);
  GPGC_OldCollector::heal_mid_pages(gcm, _stripe);
}


//
// GPGC_OldGC_RelocateSmallPagesTask
//

void GPGC_OldGC_RelocateSmallPagesTask::do_it(uint64_t which) {
  GPGC_GCManagerOldReloc* gcm = GPGC_GCManagerOldReloc::get_manager(which);
  GPGC_OldCollector::relocate_small_pages(gcm, _work_unit, _relocation_array);
}


//
// GPGC_NewGC_CardMarkBlockTask
//

void GPGC_NewGC_CardMarkBlockTask::do_it(uint64_t which) {
  GPGC_PageInfo*     info     = GPGC_PageInfo::page_info(_block);
  oop     new_obj = (oop) GPGC_Layout::PageNum_to_addr(info->ll_next());

new_obj->GPGC_newgc_update_cardmark();

  // Block marking now done when the block is cloned:
  // if ( GPGC_OldCollector::should_mark_new_objects_live() ) {
  //   if ( GPGC_Marks::atomic_mark_live_if_dead(new_obj) ) {
  //     GPGC_Marks::set_markid(new_obj, 0xA0);
  //     GPGC_Marks::set_marked_through(new_obj);
  //   }
  // }
}


//
// GPGC_NewGC_CardMarkPagesTask
//

void GPGC_NewGC_CardMarkPagesTask::do_it(uint64_t which) {
  GPGC_NewCollector::card_mark_pages(_work_unit);
}


//
// GPGC_OldGC_CardMarkPagesTask
//

void GPGC_OldGC_CardMarkPagesTask::do_it(uint64_t which) {
  GPGC_OldCollector::card_mark_pages(_array, _work_unit);
}


//
// GPGC_NewGC_RefsTask
//

void GPGC_NewGC_RefsTask::do_it(uint64_t which) {
  GPGC_GCManagerNewStrong* gcm = GPGC_GCManagerNewStrong::get_manager(which);

  GPGC_NewCollector::jlr_handler()->do_captured_ref_list(gcm, _list);

  // Must remember to free the lists!
  delete _list;
}

const char*GPGC_NewGC_RefsTask::name(){
  switch(_list->ref_type()) {
    case REF_SOFT:
      return (char *)"gpgc-newgc-soft-refs-task";

    case REF_WEAK:
      return (char *)"gpgc-newgc-weak-refs-task";

    case REF_FINAL:
      return (char *)"gpgc-newgc-final-refs-task";

    case REF_PHANTOM:
      return (char *)"gpgc-newgc-phantom-refs-task";
  }

  return (char *)"gpgc-newgc-unknown-refs-task";
}


//
// GPGC_OldGC_RefsTask
//

void GPGC_OldGC_RefsTask::do_it(uint64_t which) {
  GPGC_GCManagerOldStrong* gcm = GPGC_GCManagerOldStrong::get_manager(which);

  GPGC_OldCollector::jlr_handler()->do_captured_ref_list(gcm, _list);

  // Must remember to free the lists!
  delete _list;
}

const char*GPGC_OldGC_RefsTask::name(){
  switch(_list->ref_type()) {
    case REF_SOFT:
      return (char *)"gpgc-oldgc-soft-refs-task";

    case REF_WEAK:
      return (char *)"gpgc-oldgc-weak-refs-task";

    case REF_FINAL:
      return (char *)"gpgc-oldgc-final-refs-task";

    case REF_PHANTOM:
      return (char *)"gpgc-oldgc-phantom-refs-task";
  }

  return (char *)"gpgc-oldgc-unknown-refs-task";
}


//
// GPGC_NewGC_ClearOneSpaceMarksTask
//

void GPGC_NewGC_ClearOneSpaceMarksTask::do_it(uint64_t which) {
  GPGC_Space::new_gc_clear_one_space_marks(_section, _sections);
}


//
// GPGC_NewGC_ClearMultiSpaceMarksTask
//

void GPGC_NewGC_ClearMultiSpaceMarksTask::do_it(uint64_t which) {
  GPGC_Space::new_gc_clear_multi_space_marks();
}


//
// GPGC_OldGC_ClearOneSpaceMarksTask
//

void GPGC_OldGC_ClearOneSpaceMarksTask::do_it(uint64_t which) {
  GPGC_Space::old_gc_clear_one_space_marks(_section, _sections);
}


//
// GPGC_OldGC_ClearMultiSpaceMarksTask
//

void GPGC_OldGC_ClearMultiSpaceMarksTask::do_it(uint64_t which) {
  GPGC_Space::old_gc_clear_multi_space_marks();
}
