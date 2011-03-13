/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "collectedHeap.hpp"
#include "cpCacheKlass.hpp"
#include "cpCacheOop.hpp"
#include "markSweep.hpp"
#include "oopTable.hpp"
#include "psParallelCompact.hpp"

#include "collectedHeap.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "universe.inline.hpp"

int constantPoolCacheKlass::oop_size(oop obj) const { 
  assert(obj->is_constantPoolCache(), "must be constantPool");
  return constantPoolCacheOop(obj)->object_size();
}


int constantPoolCacheKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_constantPoolCache()), obj->_klass may not be available.
  return constantPoolCacheOop(obj)->object_size();
}


constantPoolCacheOop constantPoolCacheKlass::allocate(int length, TRAPS) {
  // allocate memory
  int size = constantPoolCacheOopDesc::object_size(length);
  KlassHandle klass (THREAD, as_klassOop());
  constantPoolCacheOop cache = (constantPoolCacheOop)
    CollectedHeap::permanent_array_allocate(klass, size, length, CHECK_NULL);
  cache->set_constant_pool(NULL);
  return cache;
}


klassOop constantPoolCacheKlass::create_klass(TRAPS) {
  constantPoolCacheKlass o;
  KlassHandle klassklass(THREAD, Universe::arrayKlassKlassObj());  
arrayKlassHandle k=base_create_array_klass(o.vtbl_value(),header_size(),klassklass,constantPoolCacheKlass_kid,CHECK_NULL);
  KlassHandle super (THREAD, k->super());
  complete_create_array_klass(k, super, CHECK_NULL);
  KlassTable::bindReservedKlassId(k(), constantPoolCacheKlass_kid);
  return k();
}


int constantPoolCacheKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cache->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolCacheKlassObj never moves.
  // iteration over constant pool cache instance variables
  blk->do_oop((objectRef*)cache->constant_pool_addr());
  // iteration over constant pool cache entries
  for (int i = 0; i < cache->length(); i++) cache->entry_at(i)->oop_iterate(blk);
  return size;
}


int constantPoolCacheKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cache->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolCacheKlassObj never moves.
  // iteration over constant pool cache instance variables
  objectRef* addr = (objectRef*)cache->constant_pool_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  // iteration over constant pool cache entries
  for (int i = 0; i < cache->length(); i++) cache->entry_at(i)->oop_iterate_m(blk, mr);
  return size;
}


int constantPoolCacheKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cache->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolCacheKlassObj never moves.
  // Iteration over constant pool cache instance variables
MarkSweep::adjust_pointer(cache->constant_pool_addr());
  // iteration over constant pool cache entries
  for (int i = 0; i < cache->length(); i++)
    cache->entry_at(i)->adjust_pointers();
  return size;
}

void constantPoolCacheKlass::oop_copy_contents(PSPromotionManager* pm, 
					       oop obj) {
  assert(obj->is_constantPoolCache(), "should be constant pool");
}

void constantPoolCacheKlass::oop_push_contents(PSPromotionManager* pm, 
					       oop obj) {
  assert(obj->is_constantPoolCache(), "should be constant pool");
}

int
constantPoolCacheKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;

  // Iteration over constant pool cache instance variables
  PSParallelCompact::adjust_pointer((heapRef*)cache->constant_pool_addr());

  // iteration over constant pool cache entries
  for (int i = 0; i < cache->length(); ++i) {
    cache->entry_at(i)->update_pointers();
  }

  return cache->object_size();
}

int
constantPoolCacheKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
					    HeapWord* beg_addr,
					    HeapWord* end_addr) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;

  // Iteration over constant pool cache instance variables
  heapRef* p;
  p = (heapRef*)cache->constant_pool_addr();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);

  // Iteration over constant pool cache entries
  for (int i = 0; i < cache->length(); ++i) {
    cache->entry_at(i)->update_pointers(beg_addr, end_addr);
  }
  return cache->object_size();
}

#ifndef PRODUCT

void constantPoolCacheKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;
  // super print
  arrayKlass::oop_print_on(obj, st);
  // print constant pool cache entries
  for (int i = 0; i < cache->length(); i++) cache->entry_at(i)->print(st, i);
}

#endif

void constantPoolCacheKlass::oop_verify_on(oop obj, outputStream* st) {
  guarantee(obj->is_constantPoolCache(), "obj must be constant pool cache");
  constantPoolCacheOop cache = (constantPoolCacheOop)obj;
  // super verify
  arrayKlass::oop_verify_on(obj, st);
  // print constant pool cache entries
  for (int i = 0; i < cache->length(); i++) cache->entry_at(i)->verify(st);
}


const char* constantPoolCacheKlass::internal_name() const { 
  return "{constant pool cache}"; 
}

