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


#include "gpgc_oldCollector.hpp"
#include "invertedVirtualspace.hpp"
#include "java.hpp"
#include "mutexLocker.hpp"
#include "oopTable.hpp"
#include "safepoint.hpp"
#include "synchronizer.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "virtualspace.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

// This method returns a hash table index based on the identity hash id of
// an object.  This provides a less than 4 * log2(table_size) worst case search
// of the table.
// 
// The raw index in constructed by combining a portion of the identity hash id with
// an attempt counter.  The attempt counter is split into two parts; the iteration
// (lower 2 bits) and the level (remainder.)  The level indicates how much of the
// identity hash id is used o compute the index.  As the level increases more bits
// are used (initially 8 bits.)  A high 1 bit is prepended to these bits to force
// the use of a new portion of the table. The iteration is appended to the low end
// of the index to force adjacent values into the same cache line. Finally, the
// index is biased by the minimum value that can be generated (base 1.)
//
//    +----------------------------------------------+
//    |                 ... 0 | 1 | ...xxxxxxxx | yy |
//    +----------------------------------------------+
//
//    xxxxxxxx - bits from identity hash id, based on level
//    yy       - iteration
//
intptr_t OopTable::getRawIndex(intptr_t id_hash, intptr_t attempt) {
  // log2(size) of the data cache in addresses
  const intptr_t data_cache_size = 2LL;
  // minimum bit width of bits extracted from identity hash id
  const intptr_t min_bit_width = 8LL;

  // Remove sign extension
  id_hash &= markWord::hash_mask;
  
  // iteration is the slot in the data cache
  intptr_t iteration = attempt & ((1LL << data_cache_size) - 1LL);
  // number of bits in level
  intptr_t shift = (attempt >> data_cache_size) + min_bit_width;
  
  // level is the width of the bits used from the identity hash id
  intptr_t level = 1LL << shift;
  // index in hash table
  intptr_t index = ((level + (id_hash & (level - 1LL))) << data_cache_size);
  // add in cache line iteration
  index += iteration;
  // correct bias
  index -= ((1LL << (min_bit_width + data_cache_size)) - 1LL);
  
  // If we run into a pathological situation, start filling empty
  // slots linearly from the end (least likely to be used.)
  if (shift > markWord::hash_mask || 0 > index || index >= _max_index) {
    index = _max_index - attempt;
guarantee(0<=index,"Oop table is full");
  }
  
  return index;
}


// Null out slot at the specified index.
void OopTable::free(intptr_t i) {
  _table[i] = ALWAYS_POISON_OBJECTREF(nullRef);
}


// Add an entry to the table using the hash table algorithm.
intptr_t OopTable::put(objectRef r) {
  // This may be called from a JRT_LEAF routine.
  debug_only(ResetNoHandleMark rnhm;)
  HandleMark hm;

  // no checking on the ref
  JavaThread *jt = JavaThread::current();
  assert(jt->jvm_locked_by_self(), "JavaThreads must hold their jvm_lock when adding to the oop table");
assert(_table!=NULL,"oop table not initialized");
  
  // it may take several attempts to get the entry in the table
  intptr_t attempt = 0;
  
  // make sure we are not adding klasses
oop obj=r.as_oop();
assert(!obj->is_klass(),"should not be using this put for klasses");
  
  // get the identity hash id
  intptr_t id_hash = ObjectSynchronizer::FastHashCode(obj);

  // try until we can get find a slot 
  while (true) {
    // get the current attempt index
    intptr_t raw_index = getRawIndex(id_hash, attempt++);
    
    // if we have to, grow the table until the raw index fits
    while (_top_table_index <= raw_index) {
      MutexLocker ml(*_table_lock);
      if (_top_table_index <= raw_index) grow();
    }
    
    // create a the real index
    intptr_t index = _grows_down ? -raw_index : raw_index;
    
    // we already have this value in the table
    objectRef o = ALWAYS_UNPOISON_OBJECTREF(_table[index]);
    if (o.not_null() && LVB::mutator_relocate(o).as_oop() == obj) return index;
    
    // attempt to CAS the existing null entry with the new value
    intptr_t null_value = nullRef.raw_value();
    intptr_t old_value = _table[index].raw_value();
    intptr_t new_value = ALWAYS_POISON_OBJECTREF(r).raw_value();
    
    if (old_value == null_value && null_value == Atomic::cmpxchg_ptr(new_value, (intptr_t*)&_table[index], null_value)) {
      // update top dispensed entry.  if other threads are racing then the one with the max value wins
      for (intptr_t top = _top_dispensed_index;
           top < raw_index && top != Atomic::cmpxchg(raw_index, (volatile jlong*)&_top_dispensed_index, top);
           top = _top_dispensed_index) {}
      
      // return the result
      return index;
    }
    
    // try again
  }
  
  // never reach here
  return 0;
}


// --- get -------------------------------------------------------------------
// Return the entry at the index.  Works even when the index is negative (constants table.)
objectRef OopTable::get(int i) {
assert(_table!=NULL,"oop table not initialized");
  return GPGC_OldCollector::get_weak_ref(&_table[i]);
}

// --- grow ------------------------------------------------------------------
// Commit more reserved memory for the table. 
void OopTable::grow(){
  // grow hash table by power of 2
  intptr_t top_table_index = _top_table_index << 1;
  intptr_t top_committed_index = _top_committed_index;
assert(top_table_index,"oop table has grown too large");
  
  // add pages until large enough
  while (top_table_index > top_committed_index) {
    // grow by a page or by what ever is left over
    size_t growBy = MIN2 ((size_t)_growth_size_in_bytes, _vs->uncommitted_size());
    
    // attempt to grow
    if (growBy && _vs->expand_by(growBy, false)) {
      intptr_t old_size = top_committed_index << LogBytesPerWord;
      
      // clear memory (is this really necessary? memory from the system should be already be zero)
      if (_grows_down) {
        memset((address)_table - (old_size + growBy), 0, growBy);
      } else {
        memset((address)_table + old_size, 0, growBy);
      }
       
      top_committed_index += growBy >> LogBytesPerWord;
    } else {
fatal("Out of space for oop table");
    }
  }
  
  // commit changes
  _top_table_index = top_table_index;
  _top_committed_index = top_committed_index;
  Atomic::write_barrier();
}


// GC

void OopTable::oops_do_impl(OopClosure*f){
  // This is a dangerous call.  It should only be called inside a safepoint, and
  // when the caller is sure that concurrent GPGC threads aren't doing an OopTable::unlink.
  // Otherwise, the contents of table entries may suddenly change to non objectRef values,
  // and cause the closure to crash.
  assert0(SafepointSynchronize::is_at_safepoint());
assert(_table!=NULL,"oop table not initialized");

  for (intptr_t i = lowestUsedIndex(), hi = highestUsedIndex(); i <= hi; i++) {
    objectRef* addr = &_table[i];
    objectRef  ref  = ALWAYS_UNPOISON_OBJECTREF(*addr);
    // Screen out NULL and free list entries.
if(ref.not_null()){
      f->do_oop(addr);
    }
  }
}


void OopTable::unlink_impl(BoolObjectClosure*is_alive){
  assert0(SafepointSynchronize::is_at_safepoint());
assert(_table!=NULL,"oop table not initialized");
  assert0(!UseGenPauselessGC);

  for (intptr_t i = lowestUsedIndex(), hi = highestUsedIndex(); i <= hi; i++) {
    objectRef* addr = &_table[i];
    objectRef  ref = ALWAYS_UNPOISON_OBJECTREF(*addr);
    
if(ref.not_null()){
      assert(ref.as_oop()->is_oop(), "not oop"); // Is this safe, even if the oop is a dead object?
      if (!is_alive->do_object_b(ref.as_oop())) {
free(i);
      }
    }
  }
}


static InvertedVirtualSpace ivs_ccOopTable;
static VirtualSpace         vs_klassTable;

void oopTable_init() {

  size_t size_klassTable = ReservedSpace::page_align_size_up(sizeof(klassRef) * max_kid);
  size_t size_ccOopTable = ReservedSpace::page_align_size_up(sizeof(objectRef) * 8 * M);
  size_t size_oopTables  = size_klassTable + size_ccOopTable;
  // memory has already been reserved
  ReservedSpace rs((char*) __OOP_TABLE_START_ADDR__, size_oopTables);
  
if(!rs.is_reserved()){
vm_exit_during_initialization("Cannot reserve enough space for oop tables");
  }
  
  ReservedSpace rs_oopTable = rs.first_part(size_ccOopTable);
ReservedSpace rs_klassTable=rs.last_part(size_ccOopTable);
  
  if (!vs_klassTable.initialize(rs_klassTable, 0)) {
vm_exit_during_initialization("Cannot commit memory for klassTable");
  }
  if (!ivs_ccOopTable.initialize(rs_oopTable, 0)) {
vm_exit_during_initialization("Cannot commit memory for oopTable");
  }
  
  CodeCacheOopTable::init(&ivs_ccOopTable);
  KlassTable       ::init(& vs_klassTable);
}


void oopTable_exit() {
  if (PrintOopTableAtExit) {
CodeCacheOopTable::print();
KlassTable::print();
  }
}
