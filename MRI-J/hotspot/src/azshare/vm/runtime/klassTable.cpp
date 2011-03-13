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
#include "collectedHeap.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_readTrapArray.hpp"
#include "mutexLocker.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "tickProfiler.hpp"
#include "thread.hpp"
#include "virtualspace.hpp"

#include "atomic_os_pd.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// Global reference to klass table.
KlassTable* KlassTable::_kt;


// Constructor.
KlassTable::KlassTable(VirtualSpace *vs) {
_vs=vs;
  _table_lock = &KlassTable_lock;
  _table = (objectRef*) vs->low();
  _grows_down = false;
  _growth_size_in_bytes = ReservedSpace::page_align_size_up(1*M);
  _top_committed_index = 0;
  _top_table_index = max_reserved_kid + 1;
  _top_dispensed_index = _top_table_index;
  _max_index = vs->reserved_size() >> LogBytesPerWord;
  _first_free = _top_dispensed_index;
  assert(_vs->committed_size() == 0, "vs should be unused");
}
  

// Initialize the global klass table.
void KlassTable::init(VirtualSpace *vs) {
  _kt = new KlassTable(vs);
  _kt->grow();
}


// Returns the kid of klass reference passed in.
intptr_t KlassTable::put(objectRef r) {
  klassOop ko = (klassOop)r.as_oop();
  intptr_t kid = Klass::cast(ko)->klassId();
assert(!kid,"adding an unregistered class to the oop table");
  return kid;
}


// Search for the first available kid.
intptr_t KlassTable::findFreeKid() {
  // Start searching at the hit (most often at _top_dispensed_index + 1)
  intptr_t first = _first_free;
  intptr_t last = _top_dispensed_index;
  
  for (intptr_t kid = first; kid <= last; kid++) {
    // if null entry is found
    if (_table[kid].is_null()) {
      // lazyily update hit
      Atomic::cmpxchg(kid + 1, (volatile jlong*)&_first_free, first);
      return kid;
    }
  }
  
  // assume we can use the next slot
  intptr_t kid = last + 1;
  
  // lazyily update hint
  Atomic::cmpxchg(kid + 1, (volatile jlong*)&_first_free, first);
  
  // grow table if necessary
  while (_top_table_index <= kid) {
    MutexLocker ml(*_table_lock);
    grow();
  }
  
  // update top dispensed entry.  if other threads are racing then the one with the max value wins
  for (intptr_t top = _top_dispensed_index;
       top < kid && top != Atomic::cmpxchg(kid, (volatile jlong*)&_top_dispensed_index, top);
       top = _top_dispensed_index) {}
  
  return kid;
}


// Override freeing so that we can update the first free hint. 
void KlassTable::free(intptr_t i) {
  OopTable::free(i);
  
  // update hint. if other threads are racing then the one with the min value wins
  for (intptr_t first = _first_free;
       i < first && first != Atomic::cmpxchg(i, (volatile jlong*)&_first_free, first);
       first = _first_free) {}
}


// Add a primitive klass to the klass table.
void KlassTable::bindReservedKlassId(klassOop ko, unsigned int kid) {
assert(Thread::current()->is_Java_thread(),"Only JavaThreads can add to the oop table");
assert(JavaThread::current()->jvm_locked_by_self(),"JavaThreads must hold their jvm_lock when adding to the oop table");
assert(_kt->_table!=NULL,"oop table not initialized");
  assert (kid <= max_kid, "kid out of range");
  assert (kid <= max_reserved_kid, "can only bind reserved kids");
  ko->klass_part()->set_klassId(kid);
klassRef kr=klassRef(ko);
  _kt->_table[kid] = ALWAYS_POISON_OBJECTREF(kr);
}


// Add a constructed klass to the klass table.
// ProfileAllocatedObejcts will trigger a GC occasionally.
void KlassTable::registerKlass(Klass*k){
assert(Thread::current()->is_Java_thread(),"Only JavaThreads can add to the oop table");
assert(JavaThread::current()->jvm_locked_by_self(),"JavaThreads must hold their jvm_lock when adding to the oop table");
assert(_kt->_table!=NULL,"oop table not initialized");

  klassRef kr = klassRef(k->as_klassOop());
  intptr_t new_value = ALWAYS_POISON_OBJECTREF(kr).raw_value();

  intptr_t kid;
  while( Atomic::cmpxchg_ptr(new_value, (intptr_t*)&_kt->_table[kid=_kt->findFreeKid()], 0)) ;

  k->set_klassId(kid);
  
  if (ProfileAllocatedObjects) AllocatedObjects::new_klass_id_assigned(kid);
}


// True if the kid is is in the range of valid indices (doesn't do a null check.)
bool KlassTable::is_valid_klassId(int kid) {
if(_kt==NULL){
    // klasstable not yet initialized; very early in bringup
    return (0 < kid) && (kid <= max_reserved_kid);
  }
  return is_valid_index(kid);
}


// Return the Klass name matching the kid.
const char* KlassTable::pretty_name(unsigned int kid) {
  switch (kid) {
case 0:return"__null__";
  case booleanArrayKlass_kid:       return "boolean[]";
  case charArrayKlass_kid:          return "char[]";
  case floatArrayKlass_kid:         return "float[]";
  case doubleArrayKlass_kid:        return "double[]";
  case byteArrayKlass_kid:          return "byte[]";
  case shortArrayKlass_kid:         return "short[]";
  case intArrayKlass_kid:           return "int[]";
  case longArrayKlass_kid:          return "long[]";
  case arrayKlass_kid:              return "Built-in VM arrayKlass";
  case constMethodKlass_kid:        return "Built-in VM constMethodKlass";
  case constantPoolKlass_kid:       return "Built-in VM constantPoolKlass";
  case constantPoolCacheKlass_kid:  return "Built-in VM constantPoolCacheKlass";
  case instanceKlass_kid:           return "Built-in VM instanceKlass";
  case klassKlass_kid:              return "Built-in VM klassKlass";
  case methodKlass_kid:             return "Built-in VM methodKlass";
  case methodCodeKlass_kid:         return "Built-in VM methodCodeKlass";
  case dependencyKlass_kid:         return "Built-in VM dependencyKlass";
  case objArrayKlass_kid:           return "Built-in VM objArrayKlass";
  case symbolKlass_kid:             return "Built-in VM symbolKlass";
  case typeArrayKlass_kid:          return "Built-in VM typeArrayKlass";
  default:
    klassRef r = getKlassByKlassId(kid);
if(r.not_null()){
      Klass *k = klassOop(r.as_oop())->klass_part();
return k->pretty_name();
    } else {
      return NULL;
    }
  }
}


// GC


void KlassTable::oops_do(OopClosure*f){
  _kt->oops_do_impl(f);
}

void KlassTable::unlink(BoolObjectClosure*is_alive){
  _kt->unlink_impl(is_alive);
}

void KlassTable::GPGC_unlink(GPGC_GCManagerOldStrong* gcm, long _section, long _sections) {
  _kt->GPGC_unlink_impl(gcm, _section, _sections);
}

void KlassTable::GPGC_unlink_impl(GPGC_GCManagerOldStrong* gcm, long _section, long _sections) {
assert(_table!=NULL,"KlassTable not initialized");

  // TODO: striping really ought to be by cache-line.
  for ( intptr_t i = lowestUsedIndex() + _section, hi = highestUsedIndex(); i <= hi; i= i + _sections ) {
    objectRef* addr = &_table[i];
    objectRef  ref  = ALWAYS_UNPOISON_OBJECTREF(*addr);
if(ref.not_null()){
      assert(ref.is_old(), "KlassTable should only have old-space refs in it");
      
      if (GPGC_ReadTrapArray::is_remap_trapped(ref)) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(ref);
      }
      
      assert(ref.as_oop()->is_oop(), "not oop"); // Is this safe, even if the oop is a dead object?
      
      if (!GPGC_Marks::is_old_marked_strong_live(ref)) {
free(i);
      } else {
        // unlike unlink(), we need to make sure objectRef's are marked through to get consistent NMT bits.
        // TODO: we could be faster here if we used the already relocated ref we have.
        GPGC_OldCollector::mark_to_live((heapRef*)addr);
      }
    }
  }

  // Only needs to be done once.
  if (_section == 0) {
    // In addition to checking normal KIDs for unlinking, we also need to make sure
    // the reserved KIDs have heapRefs with updated metadata.
  
    // Try and make sure the BasicType definitions haven't changed without this code
    // being updated.
    assert0(T_BOOLEAN==4);
    assert0(T_LONG==11);
  
    for ( intptr_t i=T_BOOLEAN; i<=T_LONG; i++ ) {
      objectRef* addr = &(_table[i]);
      assert0(addr->is_null() || ALWAYS_UNPOISON_OBJECTREF(*addr).is_heap());
      GPGC_OldCollector::mark_to_live((heapRef*)addr);
    }
  }
}

void KlassTable::GPGC_sweep_weak_methodCodes_section(GPGC_GCManagerOldStrong* gcm, long section, long sections) {
  _kt->GPGC_sweep_weak_methodCodes_section_impl(gcm, section, sections);
}

void KlassTable::GPGC_sweep_weak_methodCodes_section_impl(GPGC_GCManagerOldStrong* gcm, long section, long sections) {
assert(_table!=NULL,"KlassTable not initialized");

  // TODO: striping really ought to be by cache-line.
  for (intptr_t kid = _kt->lowestUsedIndex() + section, hi = _kt->highestUsedIndex(); kid <= hi; kid += sections) {
    objectRef* addr = getAddr(kid);
    objectRef  ref  = ALWAYS_UNPOISON_OBJECTREF(*addr);
    
if(ref.not_null()){
      assert(ref.is_old(), "KlassTable should only have old-space refs in it");
      assert(ref.as_oop()->is_klass(), "KlassTable contains non klassOop");
      klassOop k = (klassOop)ref.as_oop();
      k->klass_part()->GPGC_sweep_weak_methodCodes(gcm);
    }
  }
}

void KlassTable::print(){
  printf("KlassTable: top dispensed index = %ld\n", _kt->_top_dispensed_index);
}
