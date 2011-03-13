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


#include "bitOps.hpp"
#include "copy.hpp"
#include "cycleCounts.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_markIterator.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_space.hpp"
#include "java.hpp"
#include "klassIds.hpp"
#include "log.hpp"
#include "modules.hpp"
#include "os.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "gpgc_relocation.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.gpgc.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"


uintptr_t* GPGC_PageRelocation::_bottom         = NULL;
uintptr_t* GPGC_PageRelocation::_top            = NULL;

uintptr_t* GPGC_PageRelocation::_new_gc_end     = NULL;
uintptr_t* GPGC_PageRelocation::_max_new_gc     = NULL;

uintptr_t* GPGC_PageRelocation::_min_old_gc     = NULL;
uintptr_t* GPGC_PageRelocation::_trigger_old_gc = NULL;
uintptr_t* GPGC_PageRelocation::_old_gc_end     = NULL;



void GPGC_PageRelocation::initialize_relocation_space(long pages) {
  if ( (GPGC_Layout::start_of_object_forwarding+pages) > GPGC_Layout::end_of_object_forwarding ) {
vm_exit_during_initialization("Insufficient virtual memory: Unable to reserve object relocation space.");
  }

  _bottom = (uintptr_t*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_object_forwarding );
  _top    = (uintptr_t*) GPGC_Layout::PageNum_to_addr( GPGC_Layout::start_of_object_forwarding + pages );
  size_t reservation_bytes = pages << LogBytesPerGPGCPage;

  if ( ! os::commit_memory((char*)_bottom, reservation_bytes, Modules::GPGC_ObjectForwarding) )  {
vm_exit_during_initialization("Unable to allocate object relocation array space");
  }

_new_gc_end=_bottom;
_old_gc_end=_top;

  if ( GPGCMaxSidebandPercent <  50 ) { GPGCMaxSidebandPercent =  50; }
  if ( GPGCMaxSidebandPercent > 100 ) { GPGCMaxSidebandPercent = 100; }

  if ( GPGCOldGCSidebandTrigger > GPGCMaxSidebandPercent ) { GPGCOldGCSidebandTrigger = GPGCMaxSidebandPercent; }
  if ( GPGCOldGCSidebandTrigger < 0 )                      { GPGCOldGCSidebandTrigger = 0;                      }

  size_t total_words  = pages << LogWordsPerGPGCPage;
  size_t max_per_gc   = (total_words * GPGCMaxSidebandPercent  ) / 100;
  size_t trigger_size = (total_words * GPGCOldGCSidebandTrigger) / 100;

  _max_new_gc     = _new_gc_end + max_per_gc;
  _min_old_gc     = _old_gc_end - max_per_gc;
  _trigger_old_gc = _old_gc_end - trigger_size;

  assert0( _bottom         <  _max_new_gc );
  assert0( _max_new_gc     <= _top );
  assert0( _bottom         <= _min_old_gc );
  assert0( _min_old_gc     <  _top );
  assert0( _min_old_gc     <= _trigger_old_gc );
  assert0( _trigger_old_gc <= _top );
}


void GPGC_PageRelocation::new_gc_reset_relocation_space() {
_new_gc_end=_bottom;
}

void GPGC_PageRelocation::old_gc_reset_relocation_space() {
_old_gc_end=_top;
}


bool GPGC_PageRelocation::new_gc_get_sideband_space(GPGC_PageInfo* info, long live_objs) {
  GPGC_ObjectRelocation* start      = (GPGC_ObjectRelocation*)_new_gc_end; 
  uint32_t               pad        = (live_objs * SideBandSpacePadding) / 100;
  intptr_t               idx        = live_objs + pad;
  int32_t                first_bit  = BitOps::first_bit(uintptr_t(live_objs));
  long                   pow_of_2   = is_power_of_2(live_objs) ? live_objs:long(nth_bit(first_bit+1));
  long                   table_size = (idx > pow_of_2) ? long(nth_bit(first_bit+2)) : pow_of_2;
  uintptr_t*             new_end    = (uintptr_t*) &start[table_size];

  if ( new_end > _old_gc_end ) return false;
  if ( new_end > _max_new_gc ) return false;

  assert0(start!=NULL);
  assert0(table_size>0);

  // This is safe because sideband allocation is single threaded in a collector, and
  // locked between collectors via the SidebandAlloc interlock.
  _new_gc_end = new_end;
  info->set_relocations(start);
  info->set_reloc_len(table_size);
memset(start,0,table_size*sizeof(uintptr_t));

  return true;
}

bool GPGC_PageRelocation::old_gc_get_sideband_space(GPGC_PageInfo* info, long live_objs) {
  uint32_t               pad        = (live_objs * SideBandSpacePadding) / 100;
  intptr_t               idx        = live_objs + pad;
  int32_t                first_bit  = BitOps::first_bit(uintptr_t(live_objs));
  long                   pow_of_2   = is_power_of_2(live_objs) ? live_objs : long(nth_bit(first_bit+1));
  long                   table_size = (idx > pow_of_2) ? long(nth_bit(first_bit+2)) : pow_of_2;

  GPGC_ObjectRelocation* start      = & ((GPGC_ObjectRelocation*)_old_gc_end)[ -1 * table_size ]; 
  uintptr_t*             new_end    = (uintptr_t*) start;

  if ( new_end < _new_gc_end ) return false;
  if ( new_end < _min_old_gc ) return false;

  assert0(start!=NULL);
  assert0(table_size>0);

  _old_gc_end = new_end;
  info->set_relocations(start);
  info->set_reloc_len(table_size);
memset(start,0,table_size*sizeof(uintptr_t));

  return true;
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_object(PageNum page, GPGC_PageInfo* info, oop obj) {
  long                   offset_words = (intptr_t(obj) - intptr_t(GPGC_Layout::PageNum_to_addr(page))) >> LogBytesPerWord;
  GPGC_ObjectRelocation* relocations  = info->relocations();
  long                   idx          = find(relocations, info, offset_words);
  return &relocations[idx];
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_small_object(GPGC_PageInfo* info, oop obj) {
  long offset_words = GPGC_ObjectRelocation::oop_to_offset_words_in_page(obj);
  return find_object_generic(info, offset_words);
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_mid_object(GPGC_PageInfo* info, oop obj) {
  long offset_words = GPGC_ObjectRelocation::oop_to_offset_words_in_block(obj);
  return find_object_generic(info, offset_words);
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_object_from_interior_ptr_generic(GPGC_PageInfo* info, long offset_words) {
  GPGC_ObjectRelocation* relocations = info->relocations();
  long                   idx         = find_interior_ptr(relocations, info, offset_words); 

  if ( idx < 0 ) { return NULL; }

  return &relocations[idx];
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_small_object_from_interior_ptr(GPGC_PageInfo* info, intptr_t ptr) {
  long offset_words = GPGC_ObjectRelocation::ptr_to_offset_words_in_page(ptr);
  return find_object_from_interior_ptr_generic(info, offset_words);
}


GPGC_ObjectRelocation* GPGC_PageRelocation::find_mid_object_from_interior_ptr(GPGC_PageInfo* info, intptr_t ptr) {
  long offset_words = GPGC_ObjectRelocation::ptr_to_offset_words_in_block(ptr);
  return find_object_from_interior_ptr_generic(info, offset_words);
}


void GPGC_PageRelocation::initialize_page(PageNum page, long target_space)
{
  GPGC_MarkIterator::init_live_obj_relocation(page);

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(page);
  info->set_relocate_space(target_space);
}


void GPGC_PageRelocation::sideband_forwarding_init(long work_unit, GPGC_PopulationArray* relocation_array, long space_id)
{
  uint32_t max_cursor = relocation_array->max_cursor();

  uint64_t cursor;
  uint64_t new_cursor;

  while (true) {
    // Claim a chunk of the array to process:
    do {
      cursor = relocation_array->cursor();
      if ( cursor >= max_cursor ) { return; }
      new_cursor = cursor + work_unit;
    } while ( jlong(cursor) != Atomic::cmpxchg(jlong(new_cursor), (jlong*)relocation_array->cursor_addr(), jlong(cursor)) );

    // Determine the end of the chunk we just claimed.
    if ( new_cursor > max_cursor ) {
      new_cursor = max_cursor;
    }

    // Process the pages just claimed:
    for ( ; cursor<new_cursor; cursor++ ) {
      PageNum page = relocation_array->page(cursor);
      initialize_page(page, space_id);
    }
  }
}


void GPGC_RelocationPage::get_relocation_page(GPGC_PageInfo::Gens generation, GPGC_PageInfo::Gens source_gen, long page_time)
{
  assert0(generation==GPGC_PageInfo::OldGen || generation==GPGC_PageInfo::PermGen || generation==GPGC_PageInfo::NewGen);

  PageNum page = GPGC_Space::alloc_small_relocation_page(generation, source_gen);

  // TODO: Need to guarantee we don't get NoPage for now.  Eventually we need to write code to ensure that
  // the collector can make forward progress.  Currently allocating relocation pages can fail due to being
  // out of c-heap memory.
  guarantee(page != NoPage, "Unable to allocate relocation page: possibly out of C-heap memory.");
  
  GPGC_PageInfo* relocation_page_info = GPGC_PageInfo::page_info(page);

  relocation_page_info->set_time(page_time);
  
  // TODO: we should use the pageinfo->top() for this:
  set_end         (GPGC_Layout::PageNum_to_addr(page+1));
  set_top         (GPGC_Layout::PageNum_to_addr(page));
  set_current_page(page);
}


oop GPGC_RelocationPage::allocate(long word_size)
{
HeapWord*top=_top;
HeapWord*new_top=top+word_size;

  if ( new_top > _end ) {
return(oop)NULL;
  }

  _top = new_top;

return(oop)top;
}


void GPGC_RelocBuffer::set_generation(GPGC_PageInfo::Gens gen, GPGC_PageInfo::Gens source_gen)
{
  reset();

  _source_gen = source_gen;
_generation=gen;
}


oop GPGC_RelocBuffer::allocate(long word_size, long page_time)
{
  assert0(word_size <= WordsPerGPGCPage); // Don't presently support copying of mid-space objects for Tall
  assert0(!_prime.empty());

  oop result = _prime.allocate(word_size);
  if ( result == NULL ) {
    if ( _secondary.empty() ) {
      get_relocation_page(&_secondary, page_time);
      increment_page_count();
      result = _secondary.allocate(word_size);
    } else {
      result = _secondary.allocate(word_size);
      if ( result == NULL ) {
        _prime.close_page();

        _prime.set_top         ( _secondary.top() );
        _prime.set_end         ( _secondary.end() );
        _prime.set_current_page( _secondary.current_page() );

        get_relocation_page(&_secondary, page_time);
        increment_page_count();

        result = _secondary.allocate(word_size);
      }
    }
  }

  assert0(result != NULL);

  return result;
}


void GPGC_RemapBuffer::reset()
{
  _generation             = GPGC_PageInfo::InvalidGen;
  _remap_target_page      = NoPage;
  _source_mid_space_pages = 0;
  _target_mid_space_pages = 0;
  _large_source_pages     = 0;
  _small_pages_remapped   = 0;
}


PageNum GPGC_RemapBuffer::new_remap_target_page(long page_time, GPGC_PageInfo::Gens source_gen, int64_t stripe)
{
  long source_space_id = (source_gen==GPGC_PageInfo::NewGen) ? objectRef::new_space_id : objectRef::old_space_id;

  _remap_target_page = GPGC_Space::alloc_mid_remapping_page(_generation, source_space_id);

  // Track all mid space target pages, so we can heal them back to large pages after shattering them to small.
  _mid_space_targets->atomic_add_page(_remap_target_page, stripe, source_gen);

  GPGC_PageInfo* info = GPGC_PageInfo::page_info(_remap_target_page);
  info->set_time(page_time);

  _target_mid_space_pages ++;

  return _remap_target_page;
}


void GPGC_RemapBuffer::new_source_page(GPGC_PageInfo* info)
{
  _source_mid_space_pages ++;

  uintptr_t top           = intptr_t(info->top());
  uintptr_t base          = (top >> LogWordsPerMidSpaceBlock) << LogWordsPerMidSpaceBlock;
  uint64_t  byte_size     = top - base;
  uint64_t  alloced_pages = (byte_size + BytesPerGPGCPage - 1) >> LogBytesPerGPGCPage;

  _large_source_pages += alloced_pages;
}


void GPGC_PageRelocation::gc_relocate_mid_page(PageNum old_page, GPGC_RemapBuffer* remap_buffer, bool mark_copy, int64_t stripe)
{
  GPGC_PageInfo*         info         = GPGC_PageInfo::page_info(old_page);
  GPGC_ObjectRelocation* relocations  = info->relocations();
  long                   reloc_len    = info->reloc_len();
  long                   page_time    = info->time();

  assert0(relocations != NULL);
  assert0(reloc_len != 0);

  // Protect the source page so that we get traps when the live objects are accessed after the remap.
  if ( PageHealing ) {
    long pages = GPGC_Layout::addr_to_PageNum(info->top()-1) - old_page + 1;

    {
      CycleCounter cc(ProfileMemoryTime, &CycleCounts::protect_memory);
      os::batched_protect_memory((char*)GPGC_Layout::PageNum_to_addr(old_page), pages << LogBytesPerGPGCPage);
    }
  }

  // Prep the remap buffer as needed.
  if ( remap_buffer->remap_target_page() == NoPage ) {
    remap_buffer->new_remap_target_page(page_time, info->just_gen(), stripe);
  }

  remap_buffer->new_source_page(info);

  // Reset the count of pages that will be immediately freed when this source page is unmapped.
  info->reset_unmap_free_stats();

  // Remap the live objs in the page in address order:
  GPGC_MarkIterator::remap_live_objs(old_page, remap_buffer, mark_copy, stripe);
}


void GPGC_PageRelocation::gc_mid_obj_remap_book_keeping(HeapWord* source_addr, long word_size, HeapWord* target_addr, PageNum next_obj_first_page)
{
  // Remap book keeping: For each page inside the remap target mid space page block, we track
  // how many source pages will be freed when the target page is unshattered.  We rely on
  // unshatters being done in stripe order.
  
  HeapWord*      source_end_addr   = source_addr + word_size;
  
  PageNum        source_page       = GPGC_Layout::addr_to_PageNum(source_addr);
  PageNum        source_last_page  = GPGC_Layout::addr_to_PageNum(source_end_addr - 1);

  PageNum        target_page       = GPGC_Layout::addr_to_PageNum(target_addr);
  GPGC_PageInfo* target_info       = GPGC_PageInfo::page_info_unchecked(target_page);

  assert0( GPGC_Layout::mid_space_page(source_page) );
  assert0( next_obj_first_page==NoPage || next_obj_first_page>=source_last_page );

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceRemap, false, gclog_or_tty);
  GCLogMessage::log_b(m.enabled(), m.stream(), "GPGCTraceRemap mid: 0x%lX-0x%lX to 0x%lX-0x%lx : %ld words, %ld small pages",
                                               source_addr, source_end_addr, target_addr, (target_addr+word_size),
                                               word_size, (word_size>>LogWordsPerSmallPage));

  while ( source_addr < source_end_addr ) {
    HeapWord* local_page_target_end   = GPGC_Layout::PageNum_to_addr(target_page+1);
    HeapWord* local_page_source_end   = (source_page==source_last_page) ? source_end_addr
                                                                        : GPGC_Layout::PageNum_to_addr(source_page+1);

    long      local_page_target_words = local_page_target_end - target_addr;
    long      local_page_source_words = local_page_source_end - source_addr;
    long      advance_words           = MIN2(local_page_target_words, local_page_source_words);

    HeapWord* new_source_addr         = source_addr + advance_words;
    char*     unshatter_comment       = "";

    if ( new_source_addr == local_page_source_end ) {
      if ( new_source_addr==source_end_addr && source_last_page==next_obj_first_page ) {
        // We've completed the last source page of the object, but the next object along starts in the same source page,
        // so we can't yet increment the unshatter count on the target page.
        unshatter_comment = " : next object in same source page";
      } else {
        // When we complete a source page, the last target page it went into gets a +1 unshatter free count.
        assert0(target_info == GPGC_PageInfo::page_info_unchecked(target_page));
        target_info->increment_free_on_unshatter();
        unshatter_comment = " : unshatter +1 free, completed last object in source page";
      }
    }

    GCLogMessage::log_b(m.enabled(), m.stream(), "GPGCTraceRemap    :   remap 0x%lX-0x%lX (0x%lX) -> 0x%lX-0x%lx (0x%lX)%s",
                                                 source_addr, (source_addr+advance_words), source_page,
                                                 target_addr, (target_addr+advance_words), target_page,
                                                 unshatter_comment);

    source_addr  = new_source_addr;
    target_addr += advance_words;

    if ( source_addr == local_page_source_end ) {
      source_page ++;
    }
    if ( target_addr == local_page_target_end ) {
      target_page ++;
      target_info ++;
    }
  }

  if ( next_obj_first_page == NoPage ) {
    // If there is no following live object in the source, account for trailing empty pages freed on unmap.
    PageNum        source_base_page = GPGC_Layout::addr_to_MidSpaceBasePageNum(source_addr);
    GPGC_PageInfo* source_info      = GPGC_PageInfo::page_info(source_base_page);
HeapWord*source_top=source_info->top();
    PageNum        last_page        = GPGC_Layout::addr_to_PageNum(source_top - 1);

    if ( last_page > source_last_page ) {
      long unmap_count = last_page - source_last_page;

      source_info->increment_free_on_unmap(unmap_count);

      GCLogMessage::log_b(m.enabled(), m.stream(), "GPGCTraceRemap unmap +%ld free, free pages from source end page 0x%lX to last page in block 0x%lX",
                                                   unmap_count, source_last_page, last_page);
    }
  }
  else if ( source_last_page+1 < next_obj_first_page ) {
    // If there are fully empty pages between the last page of this object and the first page
    // of the next live object, then account for pages freed on unmap.
    PageNum        source_base_page = GPGC_Layout::addr_to_MidSpaceBasePageNum(source_addr);
    GPGC_PageInfo* source_info      = GPGC_PageInfo::page_info(source_base_page);
    long           unmap_count      = next_obj_first_page - source_last_page - 1;

    source_info->increment_free_on_unmap(unmap_count);

    GCLogMessage::log_b(m.enabled(), m.stream(), "GPGCTraceRemap unmap +%ld free, free pages from source end page 0x%lX to next obj first page 0x%lX",
                                                 unmap_count, source_last_page, next_obj_first_page);
  }
}


// This method relocates an object with a gc thread during a full GC, where normal
// heap size limits briefly do not apply.  The enables the pauseless collector to
// guarantee forward progress.  (though you still have to worry about being out of
// committed memory)
void GPGC_ObjectRelocation::gc_relocate_mid_object(PageNum             old_page,
                                                   GPGC_RemapBuffer*   remap_buffer,
                                                   bool                mark_copy,
                                                   int64_t             stripe,
                                                   PageNum             next_obj_first_page)
{
assert0(Thread::current()->is_GC_task_thread());
  assert0( GPGC_Layout::mid_space_page(old_page) );

  GPGC_PageInfo*      info         = GPGC_PageInfo::page_info(old_page);
  GPGC_PageInfo::Gens old_gen      = info->just_gen();
  long                new_space_id = info->relocate_space();
  long                page_time    = info->time();

  uint64_t            record       = get_record();
  oop                 old_obj      = (oop)(intptr_t(GPGC_Layout::PageNum_to_addr(old_page)) + (decode_old_oop(record)<<LogBytesPerWord));

  assert0( new_space_id==objectRef::new_space_id || new_space_id==objectRef::old_space_id );
  assert0( decode_state(record) == Unclaimed );

  // First get a valid klass for the object.
  klassRef klass_ref  = GPGC_OldCollector::relocate_obj_klass(old_obj);
  Klass*   klass      = klass_ref.as_klassOop()->klass_part();

  // Calculate space for the object.
  long     word_size  = old_obj->GC_size_given_klass(klass);
  long     alloc_size = align_size_up(word_size, GPGC_Layout::mid_space_object_size_alignment_word_size());

  // Check the remap target page.
  PageNum        remap_target_page = remap_buffer->remap_target_page();
  GPGC_PageInfo* target_info       = GPGC_PageInfo::page_info(remap_target_page);
  HeapWord*      remap_target_end  = GPGC_Layout::PageNum_to_addr(remap_target_page + PagesPerMidSpaceBlock);
HeapWord*target_addr=target_info->top();

  if ( (target_addr+alloc_size) > remap_target_end ) {
    // We need a new mid space relocation target page.
    remap_target_page = remap_buffer->new_remap_target_page(page_time, info->just_gen(), stripe);
    target_info       = GPGC_PageInfo::page_info(remap_target_page);
    target_addr       = target_info->top();
  }

  // Book keeping: We track page deallocation stats on remap source and target pages.
  // Source pages: track the number of pages freed immediately when we az_munmap() the source page block.
  // Target pages: track the number of pages freed when we unshatter each target page.
  GPGC_PageRelocation::gc_mid_obj_remap_book_keeping((HeapWord*)old_obj, alloc_size, target_addr, next_obj_first_page);

  // Remap the object.
  {
    CycleCounter cc(ProfileMemoryTime, &CycleCounts::relocate_memory);
    os::batched_relocate_memory((char*)old_obj, (char*)target_addr, alloc_size<<LogBytesPerWord, true);
  }

  target_info->set_top(target_addr + alloc_size);

  remap_buffer->add_small_pages_remapped(alloc_size >> LogWordsPerSmallPage);

oop new_obj=(oop)target_addr;

  // Record promotion stats.
  if ( (ProfileLiveObjects) &&
       (old_gen      == GPGC_PageInfo::NewGen) &&
       (new_space_id == objectRef::old_space_id) )
  {
    Thread::current()->live_objects()->add(klass->klassId(), KlassIds::new2old_root, 1, word_size);
  }

  // Make the new object if needed.
  if ( mark_copy ) {
    bool marked = GPGC_Marks::atomic_mark_live_if_dead(new_obj);
assert(marked,"no one else should have been able to mark a copied object");

    GPGC_Marks::set_markid        (new_obj, 0x81);
    GPGC_Marks::set_marked_through(new_obj);
  }

  // Mark the object as relocated:
  uint64_t new_record = relocated_mid_record(new_obj, old_obj);

  set_record(new_record);

  // Object is now relocated and available for any thread.
  // Card-mark table updates happen later.
}


void GPGC_PageRelocation::gc_relocate_small_page(PageNum page, GPGC_RelocBuffer* relocation_buffer, bool mark_copy)
{
  GPGC_PageInfo*         info         = GPGC_PageInfo::page_info(page);
  GPGC_ObjectRelocation* relocations  = info->relocations();
  long                   reloc_len    = info->reloc_len();
  long                   new_space_id = info->relocate_space();
  GPGC_PageInfo::Gens    old_gen      = info->just_gen();

  assert0(relocations != NULL);
  assert0(reloc_len != 0);
  assert0(new_space_id==objectRef::new_space_id || new_space_id==objectRef::old_space_id);

  // Relocate every object within the sideband forwarding array for this page.
  for ( long i=0; i<reloc_len; i++ ) {
    if (relocations[i].get_record() != 0 ) {
      relocations[i].gc_relocate_small_object(page, old_gen, new_space_id, relocation_buffer, info->time(), mark_copy);
      // Relocated OldGen and PermGen pages are cardmarked right away.
      if ( old_gen != GPGC_PageInfo::NewGen ) {
        relocations[i].old_gc_update_cardmark();
      }
    }
  }

  // Set _top in PageInfo for the pages in the relocation_buffer
  assert0(!relocation_buffer->_prime.empty());
  relocation_buffer->_prime.close_page();
  if (!relocation_buffer->_secondary.empty()) {
    relocation_buffer->_secondary.close_page();
  }

#ifdef ASSERT
  GPGC_RelocationPage prime_page   = relocation_buffer->_prime;
  HeapWord* current_page_base = GPGC_Layout::PageNum_to_addr(prime_page.current_page());
  HeapWord* current_page_end  = GPGC_Layout::PageNum_to_addr(prime_page.current_page() + 1);

  guarantee1(prime_page.top() >= current_page_base &&
             prime_page.top() <= current_page_end,
             "top not in prime page - relocation_buffer 0x%llx", relocation_buffer);

  GPGC_RelocationPage secondary_page = relocation_buffer->_secondary;
  current_page_base = GPGC_Layout::PageNum_to_addr(secondary_page.current_page());
  current_page_end  = GPGC_Layout::PageNum_to_addr(secondary_page.current_page() + 1);

  guarantee1(secondary_page.empty() ||
             (secondary_page.top() >= current_page_base &&
              secondary_page.top() <= current_page_end),
             "top not in secondary page - relocation_buffer 0x%llx", relocation_buffer);
#endif // ASSERT
}


// This method relocates an object with a gc thread during a full GC, where normal
// heap size limits briefly do not apply.  The enables the pauseless collector to
// guarantee forward progress.  (though you still have to worry about being out of
// committed memory)
void GPGC_ObjectRelocation::gc_relocate_small_object(PageNum old_page,
                                                     GPGC_PageInfo::Gens old_gen,
                                                     long new_space_id,
                                                     GPGC_RelocBuffer* relocation_buffer,
                                                     long page_time,
                                                     bool mark_copy)
{
  // Different pathway needed for GC threads that are relocating.
assert0(Thread::current()->is_GC_task_thread());
  assert0( GPGC_Layout::small_space_page(old_page) );

  uint64_t record = get_record();
  long     state  = decode_state(record);

  while ( state != Relocated ) {
    if ( state != Claimed ) {
      // Attempt to claim relocation rights for the object.
      uint64_t new_record = claimed_record(record);
      uint64_t result = (uint64_t) Atomic::cmpxchg(jlong(new_record), (jlong*)&_record, jlong(record));

if(result==record){
        // Object successfully claimed for relocation.
        oop      old_obj    = (oop)(intptr_t(GPGC_Layout::PageNum_to_addr(old_page)) + (decode_old_oop(record)<<LogBytesPerWord));
        oop      mirror_obj = (oop)(intptr_t(old_obj) + (GPGC_Layout::heap_mirror_offset << LogBytesPerGPGCPage));
        
        // First get a valid klass for the object.
        klassRef klass_ref  = GPGC_OldCollector::relocate_obj_klass(mirror_obj);
        Klass*   klass      = klass_ref.as_klassOop()->klass_part();

        // Get space for the object.
        long     word_size  = mirror_obj->GC_size_given_klass(klass);
        oop      new_obj    = relocation_buffer->allocate(word_size, page_time);

        // Record promotion stats.
        if ( (ProfileLiveObjects) &&
             (old_gen      == GPGC_PageInfo::NewGen) &&
             (new_space_id == objectRef::old_space_id) )
        {
          Thread::current()->live_objects()->add(klass->klassId(), KlassIds::new2old_root, 1, word_size);
        }

        // Now copy the object.
Copy::aligned_disjoint_words((HeapWord*)mirror_obj,(HeapWord*)new_obj,word_size);

        // Make the new object if needed.
        if ( mark_copy ) {
          bool marked = GPGC_Marks::atomic_mark_live_if_dead(new_obj);
assert(marked,"no one else should have been able to mark a copied object");
          GPGC_Marks::set_markid(new_obj, 0x82);
          GPGC_Marks::set_marked_through(new_obj);
        }

        // Make sure the copy is committed before we proceed.
        Atomic::write_barrier();

        // Mark the object as relocated:
        new_record = relocated_small_record(new_obj, old_obj);

        set_record(new_record);

        // Object is now relocated and available for any thread.
        // Card-mark table updates happen later.

        return;
      }
      // Some other thread beat us to claiming relocation rights.
    }

    // We wait until whomever claimed the relocation rights is done.
    record = get_record();
    state  = decode_state(record);

    // TODO: maw: yield on big spin counts? 
  }

  // If we're OldGC, and a mutator relocated the object for us, we need a read barrier before trying
  // to card-mark the copied object.
  // TODO: maw: only do this if OldGC
  Atomic::read_barrier();
}


heapRef GPGC_ObjectRelocation::mutator_relocate_object(long new_space_id,
                                                       long page_time,
                                                       GPGC_PageInfo::Gens source_gen,
                                                       heapRef old_ref)
{
  assert0( !Thread::current()->is_GC_task_thread() );
  assert0( old_ref.is_heap() );

  uint64_t  record  = get_record();
  long      state   = decode_state(record);
oop new_obj;

  while ( state != Relocated ) {
    // We don't currently support mutator copying of mid-space objects.  They should have already
    // been remapped by GC threads during initial relocation setup.
    assert0(GPGC_Layout::small_space_addr(old_ref.as_oop()));

    if ( state != Claimed ) {
      // Attempt to claim relocation rights for the object.
      uint64_t new_record = claimed_record(record);
      uint64_t result = (uint64_t) Atomic::cmpxchg(jlong(new_record), (jlong*)&_record, jlong(record));

if(result==record){
        // Object successfully claimed for relocation.
        oop      old_obj    = old_ref.as_oop();
        oop      mirror_obj = (oop)(intptr_t(old_obj) + (GPGC_Layout::heap_mirror_offset << LogBytesPerGPGCPage));

        // Get a valid klass for the object.
        klassRef klass_ref  = GPGC_OldCollector::relocate_obj_klass(mirror_obj);
        Klass*   klass      = klass_ref.as_klassOop()->klass_part();

        // Determine how big the object is.
        long     word_size  = mirror_obj->GC_size_given_klass(klass);

        // Attempt to get space for the object.
oop new_obj;
        if ( new_space_id == objectRef::new_space_id ) {
          new_obj = (oop) GPGC_Space::new_gen_allocate_for_relocate(source_gen, word_size, page_time);
        } else if ( source_gen == GPGC_PageInfo::PermGen ) {
          new_obj = (oop) GPGC_Space::perm_gen_allocate_for_relocate(source_gen, word_size, page_time);
        } else {
          new_obj = (oop) GPGC_Space::old_gen_allocate_for_relocate(source_gen, word_size, page_time);
        }
 
if(new_obj!=NULL){
          // Record promotion stats.
          if ( ProfileLiveObjects && old_ref.is_new() && ( new_space_id == objectRef::old_space_id ) ) {
            Thread::current()->live_objects()->add(klass->klassId(), KlassIds::new2old_root, 1, word_size);
          }

          // Now copy the object.
Copy::aligned_disjoint_words((HeapWord*)mirror_obj,(HeapWord*)new_obj,word_size);
          *(new_obj->klass_addr()) = POISON_KLASSREF(klass_ref);

          // Mark the copied object live if needed.
          if ( new_space_id==objectRef::old_space_id && GPGC_OldCollector::should_mark_new_objects_live() ) {
            bool marked = GPGC_Marks::atomic_mark_live_if_dead(new_obj);
assert(marked,"no one else should have been able to mark a copied object");
            GPGC_Marks::set_markid(new_obj, 0x83);
            GPGC_Marks::set_marked_through(new_obj);
          }

          // Make sure the copy is committed before we proceed.
          Atomic::write_barrier();

          // Mark the object as relocated:
          new_record = relocated_small_record(new_obj, old_obj);
          set_record(new_record);

          if ( old_ref.is_old() ) {
            // There's a race with NewGC when a mutator relocates an old-space object while NewGC is trying
            // to cardmark scan.  We make the race go away by having mutators LVB all new-space refs in
            // objects just copied.
            Thread* thread = Thread::current();
            assert0(thread->is_gc_mode());
thread->set_gc_mode(false);

new_obj->GPGC_mutator_update_cardmark();

thread->set_gc_mode(true);
          }

          // Object is now relocated and available for any thread.  Card-mark table updates happen later.
          return new_ref_internal(new_space_id, old_ref, new_obj);
        }

        // Revert the state back to Unclaimed.
set_record(record);

        // TODO: maw: repeated alloc failures should have a progressively longer sleep.
        // Otherwise lots of mutators could starve out the GC thread trying to relocate.
      }
    }

    // Give another thread a chance to update the record.
os::yield_all();

    record = get_record();
    state  = decode_state(record);
  }

  // We see that someone has relocated the object; make sure that we don't read state from
  // the new object location that preceeds it being relocated.
  Atomic::read_barrier();

  new_obj = decode_new_oop(record);

  return new_ref_internal(new_space_id, old_ref, new_obj);
}


// Update the card mark table for the objects in a page that's been relocated in OldGen
// by the OldGC collector.
void GPGC_ObjectRelocation::old_gc_update_cardmark()
{
assert0(Thread::current()->is_GC_task_thread());
  // TODO: assert OldGC task thread

  uint64_t record  = get_record();
  oop      new_obj = decode_new_oop(record);

  assert0(decode_state(record) == Relocated);

new_obj->GPGC_oldgc_update_cardmark();

  if ( GPGC_OldCollector::should_mark_new_objects_live() ) {
    if ( GPGC_Marks::atomic_mark_live_if_dead(new_obj) ) {
      GPGC_Marks::set_markid(new_obj, 0x84);
      GPGC_Marks::set_marked_through(new_obj);
    }
  }
}
