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
#include "gcLocker.hpp"
#include "gpgc_marker.hpp"
#include "handles.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "symbolKlass.hpp"
#include "symbolOop.hpp"
#include "symbolTable.hpp"
#include "universe.hpp"
#include "vmSymbols.hpp"

#include "collectedHeap.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"

symbolOop symbolKlass::allocate_symbol(u1* name, int len, TRAPS) {
  // Don't allow symbol oops to be created which cannot fit in a symbolOop.
  if (len > symbolOopDesc::max_length()) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(), 
                "name is too long to represent");
  }
  int size = symbolOopDesc::object_size(len);
  symbolKlassHandle h_k(THREAD, as_klassOop());
  symbolOop sym = (symbolOop)
    CollectedHeap::permanent_obj_allocate(h_k, size, CHECK_NULL);
  assert(!sym->is_parsable(), "not expecting parsability yet.");
  No_Safepoint_Verifier no_safepoint;
  sym->set_utf8_length(len);
  for (int i = 0; i < len; i++) {
    sym->byte_at_put(i, name[i]);
  }
  // Let the first emptySymbol be created and
  // ensure only one is ever created.
  assert(sym->is_parsable() || Universe::emptySymbol() == NULL,
         "should be parsable here.");
  return sym;
}

bool symbolKlass::allocate_symbols(int names_count, const char** names,
                                   int* lengths, symbolOop* sym_oops, TRAPS) {
if(UseGenPauselessGC||UseParallelGC){
    // GenPauselessGC: We can't allocate symbols in batches, because that will
    // give us a mid space object block containing a bunch of smaller misaligned
    // objects.
    //
    // Concurrent GC needs to mark all the allocated symbol oops after
    // the remark phase which isn't done below (except the first symbol oop).
    // So return false which will let the symbols be allocated one by one.
    // The parallel collector uses an object start array to find the
    // start of objects on a dirty card.  The object start array is not
    // updated for the start of each symbol so is not precise.  During
    // object array verification this causes a verification failure.
    // In a product build this causes extra searching for the start of
    // a symbol.  As with the concurrent collector a return of false will
    // cause each symbol to be allocated separately and in the case
    // of the parallel collector will cause the object
    // start array to be updated.
    return false;
  }

  assert(names_count > 0, "can't allocate 0 symbols");

  int total_size = 0;
  int i, sizes[SymbolTable::symbol_alloc_batch_size];
  for (i=0; i<names_count; i++) {
    int len = lengths[i];
    if (len > symbolOopDesc::max_length()) {
      return false;
    }
    int sz = symbolOopDesc::object_size(len);
    sizes[i] = sz * HeapWordSize;
    total_size += sz;
  }
  symbolKlassHandle h_k(THREAD, as_klassOop());
  HeapWord* base = Universe::heap()->permanent_mem_allocate(total_size);
  if (base == NULL) {
    return false;
  }

  // CAN'T take any safepoint during the initialization of the symbol oops !
  No_Safepoint_Verifier nosafepoint;

  klassOop sk = h_k();
  int kid = Klass::cast(sk)->klassId();
  assert0(KlassTable::is_valid_klassId(kid));
  int pos = 0;
  int arr_index = sizes[0];
for(i=1;i<names_count;i++){
    GPGC_Marker::perm_gen_mark_new_obj((HeapWord *) ((char *)base + arr_index));
arr_index+=sizes[i];
  }
  for (i=0; i<names_count; i++) {
    symbolOop s = (symbolOop) (((char*)base) + pos);
    s->set_mark(markWord::prototype_with_kid(kid));
    s->set_utf8_length(lengths[i]);
    const char* name = names[i];
    for (int j=0; j<lengths[i]; j++) {
      s->byte_at_put(j, name[j]);
    }

    assert(s->is_parsable(), "should be parsable here.");

    sym_oops[i] = s;
    pos += sizes[i];
  }
  return true;
}

klassOop symbolKlass::create_klass(TRAPS) {
  symbolKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),symbolKlass_kid,CHECK_NULL);
  // Make sure size calculation is right
assert(k()->size()==header_size(),"wrong size for object");
//  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), symbolKlass_kid);
  return k();
}

int symbolKlass::oop_size(oop obj) const { 
  assert(obj->is_symbol(),"must be a symbol");
  symbolOop s = symbolOop(obj);
  int size = s->object_size();
  return size; 
}

int symbolKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_symbol()), obj->_klass may not be available.
  symbolOop s = symbolOop(obj);
  int size = s->object_size();
  return size; 
}

bool symbolKlass::oop_is_parsable(oop obj) const {
  assert(obj->is_symbol(),"must be a symbol");
  symbolOop s = symbolOop(obj);
  return s->object_is_parsable();
}


int symbolKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_symbol(), "object must be symbol");
  symbolOop s = symbolOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = s->object_size();
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::symbolKlassObj never moves.
  return size;
}


int symbolKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert(obj->is_symbol(), "object must be symbol");
  symbolOop s = symbolOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = s->object_size();
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::symbolKlassObj never moves.
  return size;
}


int symbolKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_symbol(), "should be symbol");
  symbolOop s = symbolOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = s->object_size();
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::symbolKlassObj never moves.
  return size;
}


void symbolKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_symbol(), "should be symbol");
}

void symbolKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_symbol(), "should be symbol");
}

int symbolKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_symbol(), "should be symbol");
  return symbolOop(obj)->object_size();
}

int symbolKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				     HeapWord* beg_addr, HeapWord* end_addr) {
  assert(obj->is_symbol(), "should be symbol");
  return symbolOop(obj)->object_size();
}

#ifndef PRODUCT
// Printing

void symbolKlass::oop_print_on(oop obj, outputStream* st) {
  st->print("Symbol: '");
  symbolOop sym = symbolOop(obj);
  for (int i = 0; i < sym->utf8_length(); i++) {
    st->print("%c", sym->byte_at(i));
  }
  st->print("'");
}

void symbolKlass::oop_print_value_on(oop obj, outputStream* st) {
  symbolOop sym = symbolOop(obj);
  st->print("'");
  for (int i = 0; i < sym->utf8_length(); i++) {
    st->print("%c", sym->byte_at(i));
  }
  st->print("'");
}

#endif //PRODUCT

const char* symbolKlass::internal_name() const {
  return "{symbol}";
}
