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

#include "atomic_os_pd.inline.hpp"
#include "heapRefBuffer.hpp"
#include "ostream.hpp"
#include "thread.hpp"


bool HeapRefBuffer::record_ref(intptr_t raw_value, int referrer_klass_id) {
  //assert0( referrer_klass_id != 0 );
  Entry *entry = &_entries[_top++];
  entry->set_referrer_klass_id(referrer_klass_id);
  // no barrier between writes: there is already a low-frequency race between
  // reading raw_value and reading klass_id and a write-barrier here does not
  // close the race - but might fool people into believe the race is closed.
  // The race is that remote work stealers (swap_remote) only have a fuzzy
  // notion of '_top' and might steal from a slot that is busily being written
  // by the local thread, hence might see various stale versions of
  // referrer_klass_id - which will generally degrade the quality of referrer
  // klassid data, but only very slightly.
  entry->set_raw_value(raw_value);
  return _top != EntryCount;
}

bool HeapRefBuffer::record_ref_from_lvb(intptr_t raw_value, int referrer_klass_id) {
  //assert0( referrer_klass_id != 0 );
  Entry *entry = &_entries[_top++];
  entry->set_referrer_klass_id(referrer_klass_id);
  // no barrier between writes: there is already a low-frequency race between
  // reading raw_value and reading klass_id and a write-barrier here does not
  // close the race - but might fool people into believe the race is closed.
  // The race is that remote work stealers (swap_remote) only have a fuzzy
  // notion of '_top' and might steal from a slot that is busily being written
  // by the local thread, hence might see various stale versions of
  // referrer_klass_id - which will generally degrade the quality of referrer
  // klassid data, but only very slightly.
  entry->set_raw_value(raw_value);
  return _top != EntryCount;
}

bool HeapRefBuffer::swap_local(intptr_t& raw_value, int& referrer_klass_id) {
  while( true ) {
    if ( is_empty() ) return false;
    Entry *entry = &_entries[--_top];



    raw_value = Atomic::xchg_ptr(0, (volatile intptr_t*) entry->raw_value_addr());
    if( raw_value != 0 ) {
      // no barrier between reads: there is already a low-frequency race between
      // reading raw_value and reading klass_id and a read-barrier here does not
      // close the race.
      referrer_klass_id = entry->referrer_klass_id();
      // assert0( referrer_klass_id != 0 );
      return true;
    }
  } 
}


bool HeapRefBuffer::swap_remote(intptr_t& raw_value, int& referrer_klass_id, uint64_t index) {
  assert0(index < EntryCount);
  Entry *entry = &_entries[index];
  raw_value = Atomic::xchg_ptr(0, (volatile intptr_t*) entry->raw_value_addr());

  referrer_klass_id = entry->referrer_klass_id();
  // no barrier between reads: there is already a low-frequency race between
  // reading raw_value and reading klass_id and a read-barrier here does not
  // close the race.
  return raw_value != 0;
}


void HeapRefBufferList::push(HeapRefBuffer *q) {
  HeapRefBuffer* old_addr;
  intptr_t old_head;
  intptr_t new_head;
  uint64_t old_tag;
  long spin_counter=0;
  assert0(intptr_t(intptr_t(q) & AddressMaskInPlace) == intptr_t(q));
  guarantee(q->next() == 0, "the queue invariant");
  do {
old_head=_head;
    old_addr =(HeapRefBuffer*)decode_address(old_head);
    old_tag = decode_tag(old_head);
    old_tag++;
    old_tag &= TagMask;
    q->set_next(intptr_t(old_addr));
    new_head = (intptr_t(q) | (old_tag << TagShift));
    Atomic::membar();
    spin_counter ++;
  } while (old_head != Atomic::cmpxchg_ptr(new_head, &_head, old_head));
  if ( (spin_counter > 3) && PrintGCDetails ) {
    const char* thread_name = (Thread::current()->is_Java_thread()) ? "Java Thread" : (Thread::current()->is_VM_thread()) ? "VM Thread" : "GC Thread" ;
    gclog_or_tty->print_cr("%s spun "INTX_FORMAT" times pushing into the  %s", thread_name, spin_counter, _name);
  }
}


bool HeapRefBufferList::grab(HeapRefBuffer** q) {
  intptr_t new_head;
  HeapRefBuffer* old_addr;
  intptr_t old_head;
  long spin_counter=0;
  do {
old_head=_head;
    old_addr = (HeapRefBuffer*)decode_address(old_head);
if(old_addr==NULL){
      return false;
    }
    new_head = (intptr_t)old_addr->next();
    new_head |= (old_head & TagMaskInPlace);
    assert0(decode_tag(new_head) == decode_tag(old_head));
    spin_counter ++;
  } while ((intptr_t)old_head != Atomic::cmpxchg_ptr(new_head, &_head, old_head));
  *q = old_addr;
  (*q)->set_next(NULL);
  if ( (spin_counter>3) && PrintGCDetails ) {
    const char* thread_name = (Thread::current()->is_Java_thread()) ? "Java Thread" : (Thread::current()->is_VM_thread()) ? "VM Thread" : "GC Thread" ;
    gclog_or_tty->print_cr("%s spun "INTX_FORMAT" times grabbing from the  %s", thread_name, spin_counter, _name);
  }
  return true;
}
