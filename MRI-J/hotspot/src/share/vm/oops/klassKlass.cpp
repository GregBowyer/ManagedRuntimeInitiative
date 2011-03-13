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
#include "handles.hpp"
#include "instanceKlass.hpp"
#include "klassKlass.hpp"
#include "markSweep.hpp"
#include "psParallelCompact.hpp"
#include "oopTable.hpp"
#include "symbolOop.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "universe.inline.hpp"

#include "oop.inline2.hpp"

int klassKlass::oop_size(oop obj) const {
  assert (obj->is_klass(), "must be a klassOop");
  return klassOop(obj)->klass_part()->klass_oop_size();
}

int klassKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_klass()), obj->_klass may not be available.
  return klassOop(obj)->klass_part()->klass_oop_size();
}

klassOop klassKlass::create_klass(TRAPS) {
  KlassHandle h_this_klass;
  klassKlass o;
  // for bootstrapping, handles may not be available yet.
klassOop k=base_create_klass_oop(h_this_klass,header_size(),o.vtbl_value(),klassKlass_kid,CHECK_NULL);
  // Do not try to allocate mirror, java.lang.Class not loaded at this point.
  // See Universe::fixup_mirrors()
  k->set_mark(k->mark()->set_kid(klassKlass_kid));
  KlassTable::bindReservedKlassId(k, klassKlass_kid);
  return k;
}


int klassKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  // Get size before changing pointers
  int size = oop_size(obj);
  Klass* k = Klass::cast(klassOop(obj));
  blk->do_oop(k->adr_super());
  blk->do_oop(k->adr_secondary_supers());
  blk->do_oop(k->adr_java_mirror());
  blk->do_oop(k->adr_name());
  // The following are in the perm gen and are treated
  // specially in a later phase of a perm gen collection; ...
  assert(oop(k)->is_perm(), "should be in perm");
  assert(oop(k->adr_subklass())->is_perm(), "should be in perm");
  assert(oop(k->adr_next_sibling())->is_perm(), "should be in perm");
  // ... don't scan them normally, but remember this klassKlass
  // for later (see, for instance, oop_follow_contents above
  // for what MarkSweep does with it.
  if (blk->should_remember_klasses()) {
    blk->remember_klass(k);
  }
  obj->oop_iterate_header(blk);
  return size;
}


int klassKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  // Get size before changing pointers
  int size = oop_size(obj);
  Klass* k = Klass::cast(klassOop(obj));
  objectRef* adr;
  adr = k->adr_super();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = k->adr_secondary_supers();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = k->adr_java_mirror();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = k->adr_name();
  if (mr.contains(adr)) blk->do_oop(adr);
  // The following are "weak links" in the perm gen and are
  // treated specially in a later phase of a perm gen collection.
  assert(oop(k)->is_perm(), "should be in perm");
  assert(oop(k->adr_subklass())->is_perm(), "should be in perm");
  assert(oop(k->adr_next_sibling())->is_perm(), "should be in perm");
  if (blk->should_remember_klasses()
      && (mr.contains(k->adr_subklass())
          || mr.contains(k->adr_next_sibling()))) {
    blk->remember_klass(k);
  }
  obj->oop_iterate_header(blk, mr);
  return size;
}


int klassKlass::oop_adjust_pointers(oop obj) {
  // Get size before changing pointers
  int size = oop_size(obj);

  Klass* k = Klass::cast(klassOop(obj));

  MarkSweep::adjust_pointer(k->adr_super());
  MarkSweep::adjust_pointer(k->adr_secondary_supers());
  MarkSweep::adjust_pointer(k->adr_java_mirror());
  MarkSweep::adjust_pointer(k->adr_name());
  MarkSweep::adjust_pointer(k->adr_subklass());
  MarkSweep::adjust_pointer(k->adr_next_sibling());
  return size;
}

void klassKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
}

void klassKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
}

int klassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  Klass* k = Klass::cast(klassOop(obj));

  heapRef* const beg_oop = k->oop_block_beg();
  heapRef* const end_oop = k->oop_block_end();
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }

  return oop_size(obj);
}

int klassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				    HeapWord* beg_addr, HeapWord* end_addr) {
  Klass* k = Klass::cast(klassOop(obj));

  heapRef* const beg_oop = MAX2((heapRef*)beg_addr, k->oop_block_beg());
  heapRef* const end_oop = MIN2((heapRef*)end_addr, k->oop_block_end());
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }

  return oop_size(obj);
}


#ifndef PRODUCT

// Printing

void klassKlass::oop_print_on(oop obj, outputStream* st) {
  Klass::oop_print_on(obj, st);
}


void klassKlass::oop_print_value_on(oop obj, outputStream* st) {
  Klass::oop_print_value_on(obj, st);
}

#endif

const char* klassKlass::internal_name() const {
  return "{other class}";
}


// Verification

void klassKlass::oop_verify_on(oop obj, outputStream* st) {
  Klass::oop_verify_on(obj, st);
  guarantee(obj->is_perm(),                      "should be in permspace");
  guarantee(obj->is_klass(),                     "should be klass");

  Klass* k = Klass::cast(klassOop(obj));
  if (k->super() != NULL) {
    guarantee(k->super()->is_perm(),             "should be in permspace");
    guarantee(k->super()->is_klass(),            "should be klass");
  }

  if (k->java_mirror() != NULL || (k->oop_is_instance() && instanceKlass::cast(klassOop(obj))->is_loaded())) {
    guarantee(k->java_mirror() != NULL,          "should be allocated");
    guarantee(k->java_mirror()->is_perm(),       "should be in permspace");
    guarantee(k->java_mirror()->is_instance(),   "should be instance");
  }
  if (k->name() != NULL) {
    guarantee(Universe::heap()->is_in_permanent(k->name()),
	      "should be in permspace");
    guarantee(k->name()->is_symbol(), "should be symbol");
  }
}
