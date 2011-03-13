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
#ifndef HEAPREFBUFFER_HPP
#define HEAPREFBUFFER_HPP

// HeapRefBuffer is used by Threads to accumulate heapRefs for delivery
// to the garbage collector in batches. 
#include "atomic.hpp"
#include "heapRef_pd.hpp"
#include "sizes.hpp"

class HeapRefBuffer:public CHeapObj{
  // Constants:
  public:
class Entry VALUE_OBJ_CLASS_SPEC{
    public:
      static ByteSize raw_value_offset() { return byte_offset_of(HeapRefBuffer::Entry, _raw_value); }
static ByteSize referrer_klass_id_offset(){
        return byte_offset_of(HeapRefBuffer::Entry, _referrer_klass_id);
      }

      heapRef   ref()                       const { return heapRef(_raw_value); }
      intptr_t  raw_value()                 const { return _raw_value; }
      intptr_t* raw_value_addr()                  { return &_raw_value; }
      void      set_raw_value(intptr_t raw_value) { _raw_value = raw_value; }
      int       referrer_klass_id()         const { return _referrer_klass_id; }
      void      set_referrer_klass_id(int kid)    { _referrer_klass_id = kid; }

    private:
      intptr_t _raw_value;
      int     _referrer_klass_id;
    };

    enum Constants {
      EntryCount = 2048
    };

  // Statics:
  public:
    static ByteSize entries_offset() { return byte_offset_of(HeapRefBuffer, _entries); }
    static ByteSize top_offset()     { return byte_offset_of(HeapRefBuffer, _top); }
    static int      end_index()      { return EntryCount; }

  // Instance fields & methods:
  private:
    long     _top;                      // Index of next empty slot in _entries[]
    Entry    _entries[EntryCount];
    intptr_t _next;

  public:
    HeapRefBuffer() {
      _top = 0;
      _next = 0;
      memset(_entries, 0, sizeof(Entry) * EntryCount);
      Atomic::write_barrier();
    }
~HeapRefBuffer(){ShouldNotReachHere();}

    intptr_t next()                     { return _next; }
    void     set_next(intptr_t next)    { _next = next; }
    Entry*   get_entries()              { return _entries; }
    long     get_top()                  { return _top; }
    void     set_top(long top)          { _top = top; }

bool is_empty(){return _top==0;}
    bool     is_full()                  { return _top == EntryCount; }

    bool     record_ref(intptr_t raw_value, int referrer_klass_id);
    bool     record_ref_from_lvb(intptr_t raw_value, int referrer_klass_id);
    bool     swap_local(intptr_t& raw_value, int& referrer_klass_id);
    bool     swap_remote(intptr_t& raw_value, int& referrer_klass_id, uint64_t index);

    bool pop_local(Entry& entry) {
      if (is_empty()) {
        return false;
      } else {
        entry = _entries[--_top];
        return true;
      }
    }
};


class HeapRefBufferList:public CHeapObj{
  private:
    intptr_t _head;  // Pointer to HeapRefBuffer with tag to avoid ABA problems
    char*    _name;

    // Padding ensures adjacent instances don't have _head fields in the same cache line.
    intptr_t _padding[WordsPerCacheLine-2];

    enum {
      AddressBits = 48,
      TagBits = 16
    };

    enum {
      AddressShift = 0,
      TagShift = AddressBits 
    };

    enum {
      AddressMask        = (address_word)right_n_bits(AddressBits),
      AddressMaskInPlace = (address_word)AddressMask << AddressShift,
      TagMask        = (address_word)right_n_bits(TagBits),
      TagMaskInPlace = (address_word)TagMask << TagShift
    };

  public:
    HeapRefBufferList(char* name) : _head(0), _name(name) {}

    intptr_t head() {
      intptr_t head = _head;
      return decode_address(head);
    }

    bool grab(HeapRefBuffer** q);
    void push(HeapRefBuffer *q);

    long list_length() {
      HeapRefBuffer* ref_buffer = (HeapRefBuffer*)head();
      long           length     = 0;
while(ref_buffer!=NULL){
        length ++;
        ref_buffer = (HeapRefBuffer*)(ref_buffer->next());
      }
      return length;
    }

    bool check_list(HeapRefBuffer *q, uint64_t &size) {
      HeapRefBuffer* old_addr;
      intptr_t old_head;
old_head=_head;
      old_addr = (HeapRefBuffer*)decode_address(old_head);
while(old_addr!=NULL){
        size++;
        old_addr = (HeapRefBuffer*)old_addr->next();
        if (old_addr == q) {
          return true;
        }
      }
      return false;
    }

    intptr_t     decode_address(intptr_t head)  { return (head & AddressMask); }
    uint64_t     decode_tag(intptr_t head)  { return (uint64_t(head & TagMaskInPlace) >> TagShift); }

  private:
    // XXX delete this stuff if we can link without it:
    void     set_head  (intptr_t tmp, uint64_t tag)  {
      tmp &= AddressMaskInPlace; 
      tag = (tag & TagMask ) << TagShift; 
      _head = tmp | tag ;
    }
};
#endif // HEAPREFBUFFER_HPP
