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
#ifndef GPGC_RELOCATION_HPP
#define GPGC_RELOCATION_HPP

#include "gpgc_layout.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_pageInfo.hpp"
#include "gpgc_population.hpp"

#include "atomic.hpp"

class GPGC_Population;
class GPGC_RelocBuffer;
class GPGC_RemapBuffer;
class GPGC_PopulationArray;


class GPGC_ObjectRelocation VALUE_OBJ_CLASS_SPEC{
  //  For space efficiency, object relocation records are stored compactly in
  //  64 bits.
  //  
  //  The original object is stored as a word offset within its page
  //  or block, which takes 22 bits for a 32MB block in mid space.
  //
  //  With 2 bits set aside for state, the new object address is stored in a
  //  40 bit field.  The 40 bit field contains the word index of the object
  //  in memory, which allows for the new object to fall within the low 8 TB
  //  of memory.
  //
  //  Supporting large heap VA ranges than 8 TB will require a new encoding
  //  of this field.
  //
  //  This design results in good space efficiency, at the expense of needing
  //  an oop to heapRef conversion whenever an old heapRef is remapped.

  private:
    enum {
      Unclaimed = 1,
      Claimed   = 2,
      Relocated = 3,
    };

    enum {
      StateBits  = 2,
      OldOopBits = LogWordsPerMidSpaceBlock,
      NewOopBits = (64 - StateBits - OldOopBits)
    };

    enum {
      OffsetInPageMask  = (address_word)right_n_bits(LogBytesPerGPGCPage),
      OffsetInBlockMask = (address_word)right_n_bits(LogBytesPerMidSpaceBlock)
    };

    enum {
      OldOopShift = 0,
      NewOopShift = OldOopBits,
      StateShift  = NewOopShift + NewOopBits
    };

    enum {
      OldOopMask        = (address_word)right_n_bits(OldOopBits),
      OldOopMaskInPlace = (address_word)OldOopMask << OldOopShift,
      NewOopMask        = (address_word)right_n_bits(NewOopBits),
      NewOopMaskInPlace = (address_word)NewOopMask << NewOopShift,
      StateMask         = (address_word)right_n_bits(StateBits),
      StateMaskInPlace  = (address_word)StateMask << StateShift
    };


  private: // Private static methods:
    //
    // Various relocation record transformation methods:
    // 

    static uint64_t new_oop_to_record      (oop new_obj)   { return uint64_t(new_obj) << (NewOopShift - LogBytesPerWord); }
    static uint64_t small_old_oop_to_record(oop small_obj) { return oop_to_offset_words_in_page (small_obj); }
    static uint64_t mid_old_oop_to_record  (oop mid_obj)   { return oop_to_offset_words_in_block(mid_obj);   }

    static uint64_t nostate_record  (uint64_t record)      { return record & ~StateMaskInPlace; }
    static uint64_t claimed_record  (uint64_t record)      { return nostate_record(record) | (uint64_t(Claimed)  <<StateShift); }
    static uint64_t relocated_record(uint64_t record)      { return nostate_record(record) | (uint64_t(Relocated)<<StateShift); }

    static uint64_t relocated_small_record(oop new_obj, oop old_obj) { return (uint64_t(Relocated)<<StateShift)
                                                                              | new_oop_to_record(new_obj)
                                                                              | small_old_oop_to_record(old_obj); }
    static uint64_t relocated_mid_record  (oop new_obj, oop old_obj) { return (uint64_t(Relocated)<<StateShift)
                                                                              | new_oop_to_record(new_obj)
                                                                              | mid_old_oop_to_record(old_obj); }

  private: // Private instance fields and methods:
    volatile uint64_t _record;

    inline void    set_record            (uint64_t record)   { _record = record; }

    inline void    wait_for_relocation   ();
    inline void    common_claim_and_copy (heapRef old_ref, uint64_t record, long state, oop new_obj, long new_space_id);

    inline heapRef new_ref_internal      (long new_space_id, heapRef old_ref, oop new_obj);


  public: // Public static methods:
    static long    decode_old_oop        (uint64_t record)  { return long(record & OldOopMask); }
    static oop     decode_new_oop        (uint64_t record)  { return oop((record & NewOopMaskInPlace) >> (NewOopShift-LogBytesPerWord)); }
    static long    decode_state          (uint64_t record)  { return record >> StateShift; }

    static long    oop_to_offset_words_in_page (oop obj)    { assert0(GPGC_Layout::small_space_addr(obj));
                                                              return (uint64_t(obj) & OffsetInPageMask ) >> LogBytesPerWord; }
    static long    oop_to_offset_words_in_block(oop obj)    { assert0(GPGC_Layout::mid_space_addr(obj));
                                                              return (uint64_t(obj) & OffsetInBlockMask) >> LogBytesPerWord; }
    static long    ptr_to_offset_words_in_page (intptr_t p) { assert0(GPGC_Layout::small_space_addr((void*)p));
                                                              return (p & OffsetInPageMask ) >> LogBytesPerWord; }
    static long    ptr_to_offset_words_in_block(intptr_t p) { assert0(GPGC_Layout::mid_space_addr((void*)p));
                                                              return (p & OffsetInBlockMask) >> LogBytesPerWord; }


  public: // Public instance methods:
    void     init_record_generic (long offset_words) { _record = (uintptr_t(Unclaimed)<<StateShift) | offset_words; }
    void     init_small_record   (oop  old_obj)      { _record = (uintptr_t(Unclaimed)<<StateShift) | small_old_oop_to_record(old_obj); }
    void     init_mid_record     (oop  old_obj)      { _record = (uintptr_t(Unclaimed)<<StateShift) | mid_old_oop_to_record(old_obj); }

    uint64_t get_record          ()            { return _record; }

    long     old_oop_offset_words()            { return decode_old_oop(get_record()); }
    bool     is_relocated        ()            { return decode_state(get_record())==Relocated; }
    oop      new_oop             ()            { return decode_new_oop(get_record()); }


    heapRef  mutator_relocate_object      (long new_space_id, long page_time,
                                           GPGC_PageInfo::Gens source_gen, heapRef old_ref);

    void     gc_relocate_small_object     (PageNum old_page, GPGC_PageInfo::Gens old_gen,
                                           long new_space_id, GPGC_RelocBuffer* relocation_buffer,
                                           long page_time, bool mark_copy);
    void     gc_relocate_mid_object       (PageNum old_page, GPGC_RemapBuffer* remap_buffer, bool mark_copy, int64_t stripe,
                                           PageNum next_obj_first_page);

    void     old_gc_update_cardmark       ();


  public:  // Public inline instance methods:
    inline heapRef  relocated_ref                (long new_space_id, heapRef old_ref);
};


class GPGC_RelocationPage VALUE_OBJ_CLASS_SPEC{
  private:
HeapWord*_end;
    HeapWord* _top;
    PageNum   _current_page;

  public:
    void      reset               ()               { _end = _top = NULL; _current_page = NoPage; }
    bool      empty               ()               { return NULL == _top; }
    void      set_end             (HeapWord* end)  { _end = end; }
    void      set_top             (HeapWord* top)  { _top = top; }
    void      set_current_page    (PageNum page)   { _current_page = page; }

    void      close_page          ()               { GPGC_PageInfo::page_info(_current_page)->set_top(_top); }

    HeapWord* top                 ()               { return _top; }
    HeapWord* end                 ()               { return _end; }
    PageNum   current_page        ()               { return _current_page; }

    void      get_relocation_page (GPGC_PageInfo::Gens generation, GPGC_PageInfo::Gens source_gen, long page_time);
    oop       allocate            (long word_size);
};


class GPGC_RelocBuffer VALUE_OBJ_CLASS_SPEC{
  private:
    long                _page_count;
  public:
    GPGC_PageInfo::Gens _source_gen;
    GPGC_PageInfo::Gens _generation;
    GPGC_RelocationPage _prime;
    GPGC_RelocationPage _secondary;

    void reset               ()                                        { _prime.reset(); _secondary.reset(); _page_count = 0; }
    void increment_page_count()                                        { _page_count ++; }
    long page_count          ()                                        { return _page_count; }

    void get_relocation_page (GPGC_RelocationPage* rp, long page_time) { rp->get_relocation_page(_generation, _source_gen, page_time); }

    void set_generation      (GPGC_PageInfo::Gens gen, GPGC_PageInfo::Gens source_gen);
    oop  allocate            (long word_size, long page_time);
};


class GPGC_RemapBuffer VALUE_OBJ_CLASS_SPEC{
  private:
    GPGC_PageInfo::Gens   _generation;
    PageNum               _remap_target_page;
    uint64_t              _source_mid_space_pages;
    uint64_t              _target_mid_space_pages;
    uint64_t              _large_source_pages;
    uint64_t              _small_pages_remapped;

    GPGC_RemapTargetArray* _mid_space_targets;

  public:
    void    reset                           ();
    PageNum new_remap_target_page           (long page_time, GPGC_PageInfo::Gens source_gen, int64_t stripe);
    void    new_source_page                 (GPGC_PageInfo* info);

    void    set_generation                  (GPGC_PageInfo::Gens gen)  { _generation = gen; }
    void    set_mid_space_targets           (GPGC_RemapTargetArray* a) { _mid_space_targets = a; }
    void    add_small_pages_remapped        (uint64_t pages)           { _small_pages_remapped += pages; }
    PageNum remap_target_page               ()                         { return _remap_target_page; }
};


//
// This class manages relocation of objects in a single page.
//
class GPGC_PageRelocation:public AllStatic{
  private:
    //  Sideband forwarding pointer space is allocated from a double-ended buffer.  NewGC forwarding
    //  is allocated from the bottom, going up.  OldGC forwarding is allocated from the top, going
    //  down.
    //
    static uintptr_t* _bottom;
    static uintptr_t* _top;

    static uintptr_t* _new_gc_end;
    static uintptr_t* _max_new_gc;

    static uintptr_t* _min_old_gc;
    static uintptr_t* _trigger_old_gc;
    static uintptr_t* _old_gc_end;

    static void                   initialize_page                       (PageNum page, long target_space);

    static oop                    pre_allocate                          (GPGC_RelocBuffer* relocation_buffer, long word_size, int64_t original_page_age);

    static GPGC_ObjectRelocation* find_object_from_interior_ptr_generic (GPGC_PageInfo* info, long offset_words);

  public:
    static uintptr_t*  bottom        ()  { return _bottom; }
    static uintptr_t*  top           ()  { return _top;    }
    static uintptr_t*  old_gc_end    ()  { return _old_gc_end; }
    static uintptr_t*  trigger_old_gc()  { return _trigger_old_gc; }

    static void        initialize_relocation_space  (long pages);
    static void        new_gc_reset_relocation_space();
    static void        old_gc_reset_relocation_space();

    static bool        new_gc_get_sideband_space    (GPGC_PageInfo* info, long live_objs);
    static bool        old_gc_get_sideband_space    (GPGC_PageInfo* info, long live_objs);
    static void        sideband_forwarding_init     (long work_unit, GPGC_PopulationArray* relocation_array, long space_id);

    static void        new_gc_initialize_section    (GPGC_Population* page_pops, long stripe_start);
    static void        old_gc_initialize_section    (GPGC_Population* page_pops, long stripe_start);
    static void        init_object                  (GPGC_RelocBuffer* relocation_buffer, GPGC_PageInfo* info, oop obj);

    static void        gc_relocate_mid_page         (PageNum old_page, GPGC_RemapBuffer* remap_buffer,      bool mark_copy, int64_t stripe);
    static void        gc_mid_obj_remap_book_keeping(HeapWord* source_addr, long word_size, HeapWord* target_addr, PageNum next_obj_first_page);

    static void        gc_relocate_small_page       (PageNum old_page, GPGC_RelocBuffer* relocation_buffer, bool mark_copy);

    static inline long insert                       (GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key);
    static inline long find                         (GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key);
    static inline long find_interior_ptr            (GPGC_ObjectRelocation* table, GPGC_PageInfo* info, long key);

    static inline GPGC_ObjectRelocation* find_object_generic                (GPGC_PageInfo* info, long offset_words);

    static        GPGC_ObjectRelocation* find_object                        (PageNum page, GPGC_PageInfo* info, oop obj);
    static        GPGC_ObjectRelocation* find_small_object                  (GPGC_PageInfo* info, oop obj);
    static        GPGC_ObjectRelocation* find_mid_object                    (GPGC_PageInfo* info, oop obj);

    static        GPGC_ObjectRelocation* find_small_object_from_interior_ptr(GPGC_PageInfo* info, intptr_t ptr);
    static        GPGC_ObjectRelocation* find_mid_object_from_interior_ptr  (GPGC_PageInfo* info, intptr_t ptr);

    static inline void            init_object_hash             (GPGC_PageInfo* info, oop obj, long offset_words);

    static inline heapRef         relocated_multi_ref          (heapRef old_ref, PageNum cloned_block);

    static long   new_sideband_bytes_in_use()   { return address(_new_gc_end) - address(_bottom);     }
    static long   old_sideband_bytes_in_use()   { return address(_top)        - address(_old_gc_end); }
    static long   sideband_bytes_free      ()   { return address(_old_gc_end) - address(_new_gc_end); }
};


inline heapRef GPGC_ObjectRelocation::new_ref_internal(long new_space_id, heapRef old_ref, oop new_obj) {
  assert0( (new_space_id==objectRef::new_space_id) || (new_space_id==objectRef::old_space_id) );
heapRef new_ref;
  new_ref.set_value_base(new_obj, new_space_id,
                         (KIDInRef ? old_ref.klass_id() : 0),
                         (new_space_id == objectRef::new_space_id 
                            ? GPGC_NMT::desired_new_nmt_flag() 
                            : GPGC_NMT::desired_old_nmt_flag()) );
  return new_ref;
}


inline heapRef GPGC_ObjectRelocation::relocated_ref(long new_space_id, heapRef old_ref) {
  heapRef result = new_ref_internal( new_space_id, old_ref, decode_new_oop(get_record()) );
  Atomic::read_barrier(); 
  return result;
}


inline heapRef GPGC_PageRelocation::relocated_multi_ref(heapRef old_ref, PageNum cloned_block) {
  // We only support relocating multi-page objects from NewGen to OldGen.
  assert0(old_ref.is_new());

heapRef new_ref;
  oop     new_obj = (oop) GPGC_Layout::PageNum_to_addr(cloned_block);

  new_ref.set_value_base(new_obj, (uint64_t)objectRef::old_space_id, (KIDInRef ? old_ref.klass_id() : 0), GPGC_NMT::desired_old_nmt_flag());

  return new_ref;
}

#endif // GPGC_RELOCATION_HPP
