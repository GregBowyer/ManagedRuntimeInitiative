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


#include "artaObjects.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_marks.hpp"
#include "mutexLocker.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// ---------------- ArtaObjectPool -------------------


ArtaObjectPool::ArtaObjectPool(unsigned int size) {
  // FIXME: make sure size is a power of 16
_buffer_size=size;
  _object_map = NEW_C_HEAP_ARRAY(objectRef, _buffer_size);
  _last_dispensed_id = -1;
  _buffer_full = false;
}

ArtaObjectPool::~ArtaObjectPool() {
  FREE_C_HEAP_ARRAY(objectRef, _object_map);
}


bool ArtaObjectPool::is_id_live(int id) {
  // out of range?
  if (id < 0 || id >= 16*(int)_buffer_size) return false;

  if (!_buffer_full) {
    return (id <= _last_dispensed_id);
  }

  // buffer is full; live ids range from
  // (_l_d_i - _b_s + 1)%(16*_b_s) to _l_d_i
  // the first value may be larger than _l_d_i
  int _oldest_live_id = (_last_dispensed_id-_buffer_size+1) % (16*_buffer_size);

  if (_oldest_live_id < _last_dispensed_id) {
    return (_oldest_live_id <= id) && (id <= _last_dispensed_id);
  } else {
    return (id >= _oldest_live_id) || (id <= _last_dispensed_id);
  }
}


int ArtaObjectPool::append_oop(const oopDesc *o) {
  if( UseSBA && !o->is_oop() && Threads::sba_find_owner((address)o) ) {
    return -1;                  // Valid stack object, but not inspectable by ARTA
  } else {
assert(o->is_oop(),"checking for bad oops");
  }

  _last_dispensed_id =  (_last_dispensed_id+1) % (16*_buffer_size);
assert(_last_dispensed_id>=0,"object id tickets are nonnegative");
  if (!_buffer_full && (uint)_last_dispensed_id == _buffer_size-1) {
    _buffer_full = true;
  }
  unsigned int slot = _last_dispensed_id % _buffer_size;
  objectRef r = objectRef((oop)o);
  POISON_AND_STORE_REF(&_object_map[slot], r);

  return _last_dispensed_id;
}


// client should check is_id_live before calling this
// returns NULL is object referred to has been GCd
oop ArtaObjectPool::get_oop(int id) {
  assert(is_id_live(id), "Object id is no longer valid!");
  objectRef r = lvb_ref(&_object_map[id % _buffer_size]);
  return r.as_oop();
}



// ------------ Garbage collection support ----------------
// the collector needs to know about this pool of "naked oops"
void ArtaObjectPool::oops_do(OopClosure*f){
  int last_index =  (_buffer_full? _buffer_size : _last_dispensed_id+1);
for(int i=0;i<last_index;i++){
    objectRef* r = &_object_map[i];
    if (r->is_null()) continue; // has been collected
f->do_oop(r);
  }
}


void ArtaObjectPool::unlink(BoolObjectClosure*is_alive){
guarantee(SafepointSynchronize::is_at_safepoint(),"must be at safepoint");
  int last_index =  (_buffer_full? _buffer_size : _last_dispensed_id+1);  
for(int i=0;i<last_index;i++){
    objectRef *r = &_object_map[i];
    if (r->is_null()) continue; // has been collected
    oop o = ALWAYS_UNPOISON_OBJECTREF(*r).as_oop();
assert(o->is_oop(),"expecting oop in ARTA objects pool");
if(is_alive->do_object_b(o)){
      *r = nullRef;
    }
  }
}


// Version of unlink() for GenPauselessGC:
void ArtaObjectPool::GPGC_unlink() {

guarantee(SafepointSynchronize::is_at_safepoint(),"must be at safepoint");

  int last_index =  (_buffer_full? _buffer_size : _last_dispensed_id+1);
for(int i=0;i<last_index;i++){
    objectRef *addr = &_object_map[i];
    heapRef    ref  = GPGC_Collector::old_gc_remapped((heapRef*)addr);
    if ( ref.is_null() ) continue;  // has been collected
    if ( ref.is_new() )  continue;  // GPGC_unlink only does OldGen heapRefs
    assert(ref.as_oop()->is_oop(), "expecting oop in ARTA objects pool");
    if (!GPGC_Marks::is_old_marked_strong_live(ref)) {
*addr=nullRef;
    } else {
      // Unlike unlink(), GPGC_unlink() must either zero each objectRef or
      // mark through it, to ensure NMT bit consistency.
      GPGC_OldCollector::mark_to_live((heapRef*)addr);
    }
  }

}



// ---------------------- ArtaObjects -------------------------

ArtaObjectPool* ArtaObjects::_artaObjectPool = NULL;

void ArtaObjects::add(ArtaObjectPool *pool) {
MutexLocker ml(ArtaObjects_lock);
  pool->set_next(_artaObjectPool);
  _artaObjectPool = pool;
}


void ArtaObjects::remove(ArtaObjectPool *pool) {
ArtaObjectPool*prev=NULL;
  ArtaObjectPool *cur  = _artaObjectPool;
MutexLocker ml(ArtaObjects_lock);
  while ((cur != pool) && (cur != NULL)) {
prev=cur;
cur=cur->next();
  }
if(cur==pool){
    // Found it.
    if (prev != NULL) {
prev->set_next(cur->next());
    } else {
_artaObjectPool=cur->next();
    }
cur->set_next(NULL);
  }
}


void ArtaObjects::oops_do(OopClosure*f){
guarantee(SafepointSynchronize::is_at_safepoint(),"must be at safepoint");
  ArtaObjectPool *pool = _artaObjectPool;
while(pool!=NULL){
pool->oops_do(f);
    pool = pool->next();
  }
}


void ArtaObjects::unlink(BoolObjectClosure*is_alive){
guarantee(SafepointSynchronize::is_at_safepoint(),"must be at safepoint");
  ArtaObjectPool *pool = _artaObjectPool;
while(pool!=NULL){
    pool->unlink(is_alive);
    pool = pool->next();
  }
}


void ArtaObjects::GPGC_unlink() {
guarantee(SafepointSynchronize::is_at_safepoint(),"must be at safepoint");
  ArtaObjectPool *pool = _artaObjectPool;
while(pool!=NULL){
pool->GPGC_unlink();
    pool = pool->next();
  }
}


// utility printing routines

void ArtaObjects::oop_print_xml_on(oop o, xmlBuffer *xb, bool ref) {
  if (ref) {
    // objref printing
assert(o->is_oop_or_null(),"o is invalid");
    if (o == NULL) {
      xmlElement xe(xb, "object_ref");
      null_print_xml_on(xb);
      return;
    }
  } else {
    // object printing
assert(o!=NULL,"o is NULL");
assert(o->is_oop(),"o an invalid oop");
  }
  o->print_xml_on(xb, ref);
}


void ArtaObjects::null_print_xml_on(xmlBuffer *xb) {
  xmlElement xe(xb, "null");
}
