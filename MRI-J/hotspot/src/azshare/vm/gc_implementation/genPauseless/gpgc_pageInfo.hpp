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

#ifndef GPGC_PAGEINFO_HPP
#define GPGC_PAGEINFO_HPP

#include "memRegion.hpp"

class GPGC_ObjectRelocation;
class oopDesc;

class GPGC_PageInfo VALUE_OBJ_CLASS_SPEC
{
  public:
    enum States {
      InvalidState = 0,
      Unmapped     = 1,
      Allocating   = 2,
      Allocated    = 3,
      Relocating   = 4,
      Relocated    = 5
    };
    enum Gens {
      InvalidGen     = 0x00,
      NewGen         = 0x01,
      OldGen         = 0x02,
      PermGen        = 0x04,

      NewGenMask     = 0x01,
      OldAndPermMask = 0x06
    };
    enum GenStateConsts {
      StateMask      = 0x0F,
      GenShift       = 4
    };
    // The flags field encodes a number of things:
    //   - individual flags
    //   - new space_id for relocated heapRefs
    //   - A page count, meaning selected by PinnedFlag or one of FreeStats flags:
    //      - PinnedFlag             : the count of the number of different objects pinned
    //      - UnmapFreeStatsFlag     : the number of pages that will be freed when this page is unmapped
    //      - UnshatterFreeStatsFlag : the number of source pages that will be freed when this page is unshattered
    //
    // bits:    63-------------------32 31-------------24 23-----------0
    //          |------ count --------| |- reloc space -| |-- flags ---|
    enum Flags {
      NoFlag                 = 0x00,
      NoRelocateFlag         = 0x01,
      TLABFlag               = 0x02,
      NoRelocateTLABFlags    = (NoRelocateFlag | TLABFlag),
      PinnedFlag             = 0x04,

      UnmapFreeStatsFlag     = 0x10,
      UnshatterFreeStatsFlag = 0x20,
      FirstUnshatterFlag     = 0x40,
      AllFreeStatFlags       = (UnmapFreeStatsFlag | UnshatterFreeStatsFlag | FirstUnshatterFlag),

      RelocateSpaceMask      = 0xFF,   // New space_id for relocated heapRefs is stored here.
      RelocateSpaceShift     = 24,     // Number of bits to shift mask right

      CountShift             = 32,
      AllButCountMask        = 0xFFFFFFFF,

      ForceEnumTo64Bits      = 0xFFFFFFFFFFFFFFFFUL
    };

  private:
    uint8_t   volatile  _gen_n_state; // Combined page state and generation, together for atomicity.
    int32_t   volatile  _size;        // MultiSpace: block size; OneSpace: relocations len 
    uint64_t  volatile  _raw_stats;   // Page population count: live words & objects
    PageNum   volatile  _ll_next;     // Forward pointer for linked list, or mirror page for relocating pages.
    intptr_t  volatile  _object_data; // GPGC_ObjectRelocation* for OnePageSpace
    int64_t   volatile  _time;        // Page timestamp info
    uint64_t  volatile  _flags;       // Flags from Flags enum
    HeapWord* volatile  _top;         // First unallocated word in the object area of the page
    uint64_t            _unused;      // Reserved for future use.  Needed to pad size to 64 bytes.

  public:
    void initialize();

    void set_gen_and_state(Gens gen, States state) { _gen_n_state = (gen << GenShift) | state; }
    void set_just_state   (States state)           { _gen_n_state = (_gen_n_state & ~StateMask) | state; }
    void set_just_gen     (Gens gen)               { _gen_n_state = (gen << GenShift) | (_gen_n_state & StateMask); }

    void zero_raw_stats()                          { _raw_stats = 0; }
    void set_raw_stats(uint64_t raw_stats)         { _raw_stats = raw_stats; }
    void set_ll_next(PageNum page)                 { _ll_next = page; }
    void set_relocations(GPGC_ObjectRelocation* r) { _object_data = (intptr_t) r; }
    void set_time(int64_t time)                    { _time = time; }
    void set_reloc_len(long len)                   { assert0(is_int32(len));   _size = len; }
    void set_block_size(long pages)                { assert0(is_int32(pages)); _size = pages; }
    void set_flags_non_atomic(uint64_t flags)      { _flags = flags; assert0(flags_count()==0); }
void set_top(HeapWord*addr){_top=addr;}

    inline void     set_relocate_space(long space);

    inline void     atomic_set_flag  (Flags flag, Flags assert_not_flag);
    inline void     atomic_clear_flag(Flags flag);

    inline void     atomic_increment_flags_count(uint64_t delta);
    inline uint64_t atomic_decrement_flags_count();

    inline void reset_unmap_free_stats();
    inline void reset_unshatter_free_stats();

    void increment_free_on_unmap(intptr_t delta)   { assert0((_flags&UnmapFreeStatsFlag));     atomic_increment_flags_count(uint64_t(delta)); }
    void increment_free_on_unshatter()             { assert0((_flags&UnshatterFreeStatsFlag)); atomic_increment_flags_count(1UL); }
    bool set_first_unshatter();

    void atomic_add_pinned();
    void atomic_subtract_pinned();

    inline void add_live_object(long word_size);

    static States decode_state(uint64_t gen_n_state) { return States(gen_n_state & StateMask); }
    static Gens   decode_gen  (uint64_t gen_n_state) { return Gens  (gen_n_state >> GenShift); }

    uint64_t               gen_and_state() const     { return _gen_n_state; }
    States                 just_state() const        { return decode_state(gen_and_state()); }
    Gens                   just_gen() const          { return decode_gen(gen_and_state()); }

    intptr_t               unmap_free_count() const     { assert0(_flags&UnmapFreeStatsFlag);     return flags_count(); }
    intptr_t               unshatter_free_count() const { assert0(_flags&UnshatterFreeStatsFlag); return flags_count(); }

    uint64_t               raw_stats() const        { return _raw_stats; }
    PageNum                ll_next() const          { return _ll_next; }
    GPGC_ObjectRelocation* relocations() const      { return (GPGC_ObjectRelocation*) _object_data; }
    int64_t                time() const             { return _time; }
    long                   reloc_len() const        { return _size; }
    long                   block_size() const       { return _size; }
    uint64_t               flags() const            { return _flags; }
    uint64_t               flags_count() const      { return (_flags >> CountShift); }
    HeapWord*              top() const              { return _top; }

    uint64_t               relocate_space() const   { return (_flags >> RelocateSpaceShift) & RelocateSpaceMask; }

    HeapWord**             top_addr() const         { return (HeapWord**)&_top; }

    // Extract data from raw object stats:
    static uint32_t  live_words_from_raw(uint64_t raw) { return (uint32_t)(raw & 0xFFFFFFFFUL); }
    static uint32_t  live_objs_from_raw (uint64_t raw) { return (uint32_t)(raw >> 32);          }

  private:
static MemRegion _reserved_region;
    static volatile bool* _real_array_pages_mapped;
    static volatile bool* _array_pages_mapped;
    static GPGC_PageInfo* _page_info;

  public:
    static void                  initialize_info_array ();
    static bool                  expand_pages_in_use   (PageNum page);

    inline static GPGC_PageInfo* page_info_base        ()  { return _page_info; }
    inline static long           page_info_size        ()  { return sizeof(GPGC_PageInfo); }

    inline static GPGC_PageInfo* page_info_unchecked   (PageNum page);
    inline static GPGC_PageInfo* page_info             (PageNum page);
    inline static bool           info_exists           (PageNum page);

    inline static GPGC_PageInfo* page_info             (oopDesc *obj);

    inline void   clear_block_state                     (PageNum page, long pages, Gens gen);
    
    inline void   restore_block_state            (PageNum page);
};

#endif // GPGC_PAGEINFO_HPP
