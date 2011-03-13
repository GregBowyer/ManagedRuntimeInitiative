/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "constantPoolKlass.hpp"
#include "constantPoolOop.hpp"
#include "gpgc_collector.hpp"
#include "instanceKlass.hpp"
#include "markSweep.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "psParallelCompact.hpp"
#include "typeArrayOop.hpp"

#include "collectedHeap.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "universe.inline.hpp"

#include "oop.inline2.hpp"

constantPoolOop constantPoolKlass::allocate(int length, TRAPS) {
  int size = constantPoolOopDesc::object_size(length);
  KlassHandle klass (THREAD, as_klassOop());
  constantPoolOop c = 
    (constantPoolOop)CollectedHeap::permanent_array_allocate(klass, size, length, CHECK_NULL);

  c->set_tags(NULL);
  c->set_cache(NULL);
  c->set_pool_holder(NULL);
  // only set to non-zero if constant pool is merged by RedefineClasses
  c->set_orig_length(0);
  // all fields are initialized; needed for GC

  // initialize tag array
  // Note: cannot introduce constant pool handle before since it is not
  //       completely initialized (no class) -> would cause assertion failure
  constantPoolHandle pool (THREAD, c);
  typeArrayOop t_oop = oopFactory::new_permanent_byteArray(length, CHECK_NULL);
  typeArrayHandle tags (THREAD, t_oop);
  for (int index = 0; index < length; index++) {
    tags()->byte_at_put(index, JVM_CONSTANT_Invalid);
  }
  pool->set_tags(tags());

  return pool();
}

klassOop constantPoolKlass::create_klass(TRAPS) {
  constantPoolKlass o;
  KlassHandle klassklass(THREAD, Universe::arrayKlassKlassObj());  
arrayKlassHandle k=base_create_array_klass(o.vtbl_value(),header_size(),klassklass,constantPoolKlass_kid,CHECK_NULL);
  arrayKlassHandle super (THREAD, k->super());
  complete_create_array_klass(k, super, CHECK_NULL);
  KlassTable::bindReservedKlassId(k(), constantPoolKlass_kid);
  return k();
}


int constantPoolKlass::oop_size(oop obj) const { 
  assert(obj->is_constantPool(), "must be constantPool");
  return constantPoolOop(obj)->object_size();
}


int constantPoolKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_constantPool()), obj->_klass may not be available.
  return constantPoolOop(obj)->object_size();
}


int constantPoolKlass::GPGC_verify_oop_oop_iterate(oop obj,OopClosure*blk){
  assert (obj->is_constantPool(), "obj must be constant pool");
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolKlassObj never moves.
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    typeArrayOop tags = (typeArrayOop) GPGC_Collector::remap_only(cp->tags_addr()).as_oop();
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (constantPoolOopDesc::is_pointer_entry(tags,i)) {
        blk->do_oop(base);
      }
      base++;
    }
  }
  blk->do_oop(cp->tags_addr());
  blk->do_oop(cp->cache_addr());
  blk->do_oop(cp->pool_holder_addr());
  return size;
}


int constantPoolKlass::oop_adjust_pointers(oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolKlassObj never moves.

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i< cp->length();  i++) {
      if (cp->is_pointer_entry(i)) {
        MarkSweep::adjust_pointer(base); 
      } 
      base++;
    }
  }
MarkSweep::adjust_pointer((heapRef*)cp->tags_addr());
  MarkSweep::adjust_pointer(cp->cache_addr());
  MarkSweep::adjust_pointer(cp->pool_holder_addr());
  return size;
}


int constantPoolKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolKlassObj never moves.
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (cp->is_pointer_entry(i)) {
        blk->do_oop(base);
      } 
      base++;
    }
  }
  blk->do_oop(cp->tags_addr());
  blk->do_oop(cp->cache_addr());
  blk->do_oop(cp->pool_holder_addr());
  return size;
}


int constantPoolKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::constantPoolKlassObj never moves.
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (mr.contains(base)) {
        if (cp->is_pointer_entry(i)) {
          blk->do_oop(base);
        }
      }
      base++;
    }
  }
  heapRef* addr;
addr=(heapRef*)cp->tags_addr();
  blk->do_oop(addr);
  addr = cp->cache_addr();
  blk->do_oop(addr);
  addr = cp->pool_holder_addr();
  blk->do_oop(addr);
  return size;
}

int constantPoolKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i < cp->length(); ++i, ++base) {
      if (cp->is_pointer_entry(i)) {
        PSParallelCompact::adjust_pointer(base); 
      }
    }
  }
PSParallelCompact::adjust_pointer((heapRef*)cp->tags_addr());
  PSParallelCompact::adjust_pointer(cp->cache_addr());
  PSParallelCompact::adjust_pointer(cp->pool_holder_addr());
  return cp->object_size();
}

int
constantPoolKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				       HeapWord* beg_addr, HeapWord* end_addr) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    heapRef* base = (heapRef*)cp->base();
    heapRef* const beg_oop = MAX2((heapRef*)beg_addr, base);
    heapRef* const end_oop = MIN2((heapRef*)end_addr, base + cp->length());
    const size_t beg_idx = pointer_delta(beg_oop, base, sizeof(heapRef*));
    const size_t end_idx = pointer_delta(end_oop, base, sizeof(heapRef*));
    for (size_t cur_idx = beg_idx; cur_idx < end_idx; ++cur_idx, ++base) {
      if (cp->is_pointer_entry(int(cur_idx))) {
        PSParallelCompact::adjust_pointer(base);
      }
    }
  }

  heapRef* p;
p=(heapRef*)cp->tags_addr();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
  p = cp->cache_addr();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
  p = cp->pool_holder_addr();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);

  return cp->object_size();
}
void constantPoolKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_constantPool(), "should be constant pool");
}

void constantPoolKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_constantPool(), "should be constant pool");
}

#ifndef PRODUCT

// Printing

void constantPoolKlass::oop_print_on(oop obj, outputStream* st) {
  EXCEPTION_MARK;
  oop anObj;
  assert(obj->is_constantPool(), "must be constantPool");
  arrayKlass::oop_print_on(obj, st);
  constantPoolOop cp = constantPoolOop(obj);  

  // Temp. remove cache so we can do lookups with original indicies.
  constantPoolCacheHandle cache (THREAD, cp->cache());
  cp->set_cache(NULL);

  for (int index = 1; index < cp->length(); index++) {      // Index 0 is unused
    st->print(" - %3d : ", index);
    cp->tag_at(index).print_on(st);
    st->print(" : ");
    switch (cp->tag_at(index).value()) {
      case JVM_CONSTANT_Class :
        { anObj = cp->klass_at(index, CATCH);
          anObj->print_value_on(st);
st->print(" {%p}",(address)anObj);
        }
        break;
      case JVM_CONSTANT_Fieldref :
      case JVM_CONSTANT_Methodref :
      case JVM_CONSTANT_InterfaceMethodref :        
        st->print("klass_index=%d", cp->klass_ref_index_at(index));
        st->print(" name_and_type_index=%d", cp->name_and_type_ref_index_at(index));        
        break;
      case JVM_CONSTANT_UnresolvedString :
anObj=cp->unresolved_string_at(index);
        anObj->print_value_on(st);
st->print(" {%p}",anObj);
        break;
      case JVM_CONSTANT_String :
        anObj = cp->string_at(index, CATCH);
        anObj->print_value_on(st);
st->print(" {%p}",(address)anObj);
        break;
      case JVM_CONSTANT_Integer :
        st->print("%d", cp->int_at(index));
        break;
      case JVM_CONSTANT_Float :
        st->print("%f", cp->float_at(index));
        break;
      case JVM_CONSTANT_Long :
        st->print_jlong(cp->long_at(index));
        index++;   // Skip entry following eigth-byte constant
        break;
      case JVM_CONSTANT_Double :
        st->print("%lf", cp->double_at(index));
        index++;   // Skip entry following eigth-byte constant
        break;
      case JVM_CONSTANT_NameAndType :        
        st->print("name_index=%d", cp->name_ref_index_at(index));
        st->print(" signature_index=%d", cp->signature_ref_index_at(index));
        break;
      case JVM_CONSTANT_Utf8 :
        cp->symbol_at(index)->print_value_on(st);
        break;
      case JVM_CONSTANT_UnresolvedClass :		// fall-through
      case JVM_CONSTANT_UnresolvedClassInError: {
        // unresolved_klass_at requires lock or safe world.
heapRef*p=cp->obj_at_addr(index);
        oop entry = UNPOISON_OBJECTREF(*p,p).as_oop();
        entry->print_value_on(st);
        }
        break;
      default:
        ShouldNotReachHere();
        break;
    }
    st->cr();
  }
  st->cr();

  // Restore cache
  cp->set_cache(cache());
}


#endif

const char* constantPoolKlass::internal_name() const {
  return "{constant pool}";
}

// Verification

void constantPoolKlass::oop_verify_on(oop obj, outputStream* st) {
  Klass::oop_verify_on(obj, st);
  guarantee(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);  
  guarantee(cp->is_perm(), "should be in permspace");
  if (!cp->partially_loaded()) {
    heapRef* base = (heapRef*)cp->base();
    for (int i = 0; i< cp->length();  i++) {
      if (cp->tag_at(i).is_klass()) {
guarantee(lvb_ref(base).as_oop()->is_perm(),"should be in permspace");
guarantee(lvb_ref(base).as_oop()->is_klass(),"should be klass");
      }
      if (cp->tag_at(i).is_unresolved_klass()) {
guarantee(lvb_ref(base).as_oop()->is_perm(),"should be in permspace");
guarantee(lvb_ref(base).as_oop()->is_symbol()||lvb_ref(base).as_oop()->is_klass(),"should be symbol or klass");
      }
      if (cp->tag_at(i).is_symbol()) {
guarantee(lvb_ref(base).as_oop()->is_perm(),"should be in permspace");
guarantee(lvb_ref(base).as_oop()->is_symbol(),"should be symbol");
      }
      if (cp->tag_at(i).is_unresolved_string()) {
guarantee(lvb_ref(base).as_oop()->is_perm(),"should be in permspace");
guarantee(lvb_ref(base).as_oop()->is_symbol()||lvb_ref(base).as_oop()->is_instance(),"should be symbol or instance");
      }
      if (cp->tag_at(i).is_string()) {
guarantee(lvb_ref(base).as_oop()->is_perm(),"should be in permspace");
guarantee(lvb_ref(base).as_oop()->is_instance(),"should be instance");
      }
      base++;
    }
    guarantee(cp->tags()->is_perm(),         "should be in permspace");
    guarantee(cp->tags()->is_typeArray(),    "should be type array");
    if (cp->cache() != NULL) {
      // Note: cache() can be NULL before a class is completely setup or
      // in temporary constant pools used during constant pool merging
      guarantee(cp->cache()->is_perm(),              "should be in permspace");
      guarantee(cp->cache()->is_constantPoolCache(), "should be constant pool cache");
    }
    if (cp->pool_holder() != NULL) {
      // Note: pool_holder() can be NULL in temporary constant pools
      // used during constant pool merging
      guarantee(cp->pool_holder()->is_perm(),  "should be in permspace");
      guarantee(cp->pool_holder()->is_klass(), "should be klass");
    }
  }
}

bool constantPoolKlass::oop_partially_loaded(oop obj) const {
  assert(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);
  return cp->tags() == NULL || cp->pool_holder() == (klassOop) cp;   // Check whether pool holder points to self
}


void constantPoolKlass::oop_set_partially_loaded(oop obj) {
  assert(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);
  assert(cp->pool_holder() == NULL, "just checking");
  cp->set_pool_holder((klassOop) cp);   // Temporarily set pool holder to point to self
}

#ifndef PRODUCT
// CompileTheWorld support. Preload all classes loaded references in the passed in constantpool
void constantPoolKlass::preload_and_initialize_all_classes(oop obj, TRAPS) {
  guarantee(obj->is_constantPool(), "object must be constant pool");
  constantPoolHandle cp(THREAD, (constantPoolOop)obj);  
  guarantee(!cp->partially_loaded(), "must be fully loaded");
    
  Handle ex;                    // Exception during pre-loading, if any
  for (int i = 0; i< cp->length();  i++) {    
    if (cp->tag_at(i).is_unresolved_klass()) {
      // This will force loading of the class
klassOop klass=cp->klass_at(i,THREAD);
      if( !HAS_PENDING_EXCEPTION && klass->is_instance() ) {
        // Force initialization of class
instanceKlass::cast(klass)->initialize(THREAD);
      }
      // Clear any pending exception and keep pre-loading.
      // Report only the last exception.
      if( HAS_PENDING_EXCEPTION ) {
ex=PENDING_EXCEPTION;
        CLEAR_PENDING_EXCEPTION;
      }
    }
  }
  if( ex.not_null() ) 
THROW_HANDLE(ex);
}

#endif
