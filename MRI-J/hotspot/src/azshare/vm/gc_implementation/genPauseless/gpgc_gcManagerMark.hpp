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

#ifndef GPGC_GCMANAGERMARK_HPP
#define GPGC_GCMANAGERMARK_HPP

#include "gpgc_gcManager.hpp"
#include "heapRefBuffer.hpp"
class HeapRefBufferList;

class GPGC_GCManagerMark : public GPGC_GCManager
{
  protected:
    enum {
      HeapRefBufferListStripes = 8 // Must be a power of 2!
    };

    enum {
      TypeNew       = 0x1,
      TypeOld       = 0x2,
      TypeGenMask   = 0xF,
      TypeStrong    = 0x10,
      TypeFinal     = 0x20,
      TypeMarkMask  = 0xF0,
      TypeNewFinal  = TypeNew | TypeFinal,
      TypeNewStrong = TypeNew | TypeStrong,
      TypeOldFinal  = TypeOld | TypeFinal,
      TypeOldStrong = TypeOld | TypeStrong
    };

  private:
    static HeapRefBufferList** _free_mutator_ref_buffer_list;

    static intptr_t       _java_thread_ref_buffers_alloced;
    static intptr_t       _gc_thread_ref_buffers_alloced;


  protected:
    static void           push_heap_ref_buffer  (HeapRefBuffer* ref_buffer, HeapRefBufferList** striped_list);

    static HeapRefBuffer* alloc_stack           (HeapRefBufferList** free_striped_list);

    static inline void    push_ref_to_stack     (objectRef ref, int referrer_kid, HeapRefBuffer** stack,
                                                 HeapRefBufferList** full_striped_list,
                                                 HeapRefBufferList** free_striped_list);

    static bool           striped_list_is_empty (HeapRefBufferList** striped_list);

    static long           count_heap_ref_buffers(HeapRefBufferList** striped_list);

    // Debug:
    static void           verify_empty_heap_ref_buffers(HeapRefBufferList** striped_list, const char* list, const char* tag);


  public:
    static void           initialize();
    static HeapRefBuffer* alloc_mutator_stack()               { return alloc_stack(_free_mutator_ref_buffer_list); }
    static void           free_mutator_stack(HeapRefBuffer* s){ push_heap_ref_buffer(s, _free_mutator_ref_buffer_list); }

    static intptr_t       java_thread_ref_buffers_alloced()   { return _java_thread_ref_buffers_alloced;         }
    static intptr_t       gc_thread_ref_buffers_alloced()     { return _gc_thread_ref_buffers_alloced;           }

    static HeapRefBuffer* pop_heap_ref_buffer   (HeapRefBufferList** striped_list);
    static long           count_free_mutator_ref_buffers()    { return count_heap_ref_buffers(_free_mutator_ref_buffer_list); }


  private:
    HeapRefBufferList**   _free_heap_ref_buffers;
    HeapRefBufferList**   _full_heap_ref_buffers;
    HeapRefBuffer*        _current_marking_stack;

    void                  free_heap_ref_buffer(HeapRefBuffer* ref_buffer);
    HeapRefBuffer*        get_full_heap_ref_buffer();


  protected:
    GPGC_GCManagerMark(long manager_number, HeapRefBufferList** free_list, HeapRefBufferList** full_list);


  public:
    HeapRefBuffer*        current_stack             ()                      { return _current_marking_stack; }
    void                  set_current_stack         (HeapRefBuffer* stack)  { _current_marking_stack = stack; }

    bool                  get_full_current_stack    ();
    HeapRefBuffer*        alloc_stack               ()                      { return alloc_stack(_free_heap_ref_buffers); }

    void                  process_mutator_stack     (HeapRefBuffer* stack);

    bool                  steal_from_remote_thread  (objectRef& result, int& referrer_kid);

    inline void           push_ref_to_stack         (objectRef  ref, int  referrer_kid);
    inline void           push_array_chunk_to_stack (objectRef* chunk_start, long chunk_size, int referrer_kid);
    inline void           push_array_chunk_to_stack (objectRef* chunk_start, long chunk_size);

    virtual long                manager_count       () = 0;
    virtual GPGC_GCManagerMark* lookup_manager      (long manager) = 0;

    virtual void          update_live_referent      (objectRef* referent_addr, int referrer_kid) = 0;
    virtual void          mark_and_push_referent    (objectRef* referent_addr, int referrer_kid) = 0;
    virtual void          push_referent_without_mark(objectRef* referent_addr, int referrer_kid) = 0;

    virtual long          manager_type              () = 0;


    //
    // Subklasses need to implement these methods for the templates to compile:
    //
    //  inline void mark_and_push  (objectRef* p);
    //  inline void mark_and_follow(objectRef* p);
    //  inline void mark_through   (objectRef  ref);
    //  
    //  inline void decrement_working_count();
    //  inline void increment_working_count();
    //  inline long working_count();
};


inline void GPGC_GCManagerMark::push_ref_to_stack(objectRef ref, int referrer_kid, HeapRefBuffer** stackp,
                                                  HeapRefBufferList** full_striped_list, HeapRefBufferList** free_striped_list)
{
  HeapRefBuffer* stack = *stackp;
  if ( ! stack->record_ref(intptr_t(ref.raw_value()), referrer_kid) ) {
    assert0(stack->is_full());
    push_heap_ref_buffer(stack, full_striped_list);
    *stackp = alloc_stack(free_striped_list);
  }
}


inline void GPGC_GCManagerMark::push_ref_to_stack(objectRef ref, int referrer_kid)
{
  push_ref_to_stack(ref, referrer_kid, &_current_marking_stack, _full_heap_ref_buffers, _free_heap_ref_buffers);
}


inline void GPGC_GCManagerMark::push_array_chunk_to_stack(objectRef* chunk_start, long chunk_size, int referrer_kid)
{
  assert((long)(chunk_size&objectRef::non_address_mask)==chunk_size, "array chunk size too large to fit in encoding.");
  assert((uint64_t(chunk_start)&objectRef::unknown_mask)==uint64_t(chunk_start), "array chunk start too large to fit in encoding.");
#if defined(AZ_X86)
  uint64_t chunk_ref = uint64_t(chunk_start) | objectRef::reserved_mask | (chunk_size << objectRef::non_address_shift);
#else
#error Unknown arch
#endif
  push_ref_to_stack(objectRef(chunk_ref), referrer_kid);
}


inline void GPGC_GCManagerMark::push_array_chunk_to_stack(objectRef* chunk_start, long chunk_size)
{
  push_array_chunk_to_stack(chunk_start, chunk_size, 0);
}
#endif // GPGC_GCMANAGERMARK_HPP
