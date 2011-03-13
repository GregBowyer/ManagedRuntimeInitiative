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


#include "cardTableRS.hpp"
#include "collectedHeap.hpp"
#include "defNewGeneration.hpp"
#include "genRemSet.hpp"
#include "generation.hpp"
#include "gpgc_oldCollector.hpp"
#include "javaClasses.hpp"
#include "oop.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "sharedHeap.hpp"
#include "space.hpp"
#include "synchronizer.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "genOopClosures.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "universe.inline.hpp"

BarrierSet* oopDesc::_bs = NULL;

// Lock on this object needs to be acquired.
// May block.  May GC.  May throw exceptions.
void oopDesc::slow_lock(MonitorInflateCallerId reason, TRAPS) {
  // Make a one-shot fast-path attempt to bias-lock an unlocked object.
  intptr_t tid = Thread::current()->reversible_tid();
  markWord *old = mark();       // Current markWord value
  if( old->is_fresh() && cas_set_mark( old->as_biaslocked(tid), old ) == old )
    return;                     // Got it!
  
  // May block.  May GC.  May throw exceptions.
  ObjectSynchronizer::lock(this,reason);
}

void oopDesc::slow_unlock(){
ObjectSynchronizer::unlock(this);
}

#ifdef PRODUCT
void oopDesc::print_on(outputStream* st) const {}
void oopDesc::print_value_on(outputStream* st) const {}
void oopDesc::print_address_on(outputStream* st) const {}
char* oopDesc::print_value_string() { return NULL; }
char* oopDesc::print_string() { return NULL; }
void oopDesc::print(outputStream* st) {}
void oopDesc::print_value()   {}
void oopDesc::print_address() {}
#else
void oopDesc::print_on(outputStream* st) const {
  ResourceMark rm;
  if (this == NULL) {
    st->print_cr("NULL");
  } else {
    blueprint()->oop_print_on(oop(this), st);
  }
}

void oopDesc::print_value_on(outputStream* st) const {
  oop obj = oop(this);
  if (this == NULL) {
    st->print("NULL");
  } else if (java_lang_String::is_instance(obj)) {
    java_lang_String::print(obj, st);
    if (PrintOopAddress) print_address_on(st);
#ifdef ASSERT
}else if(!Universe::heap()->is_in(klass())){
    st->print("### BAD OOP %p ###", (address)obj);
#endif
  } else {
    blueprint()->oop_print_value_on(obj, st);
  }
}

void oopDesc::print_address_on(outputStream* st) const {
if(PrintOopAddress)st->print("{"PTR_FORMAT"}",this);
}

void oopDesc::print(outputStream *out) { ResourceMark rm; print_on(out);         }

void oopDesc::print_value()   { ResourceMark rm; print_value_on(tty);   }

void oopDesc::print_address() { ResourceMark rm; print_address_on(tty); }

char* oopDesc::print_string() { 
  stringStream* st = new stringStream();
  print_on(st);
  return st->as_string();
}

char* oopDesc::print_value_string() { 
  stringStream* st = new stringStream();
  print_value_on(st);
  return st->as_string();
}

#endif // PRODUCT

void oopDesc::verify_on(outputStream* st) {
  if (this != NULL) {
    blueprint()->oop_verify_on(this, st);
  }
}


void oopDesc::verify() { 
  if ( UseGenPauselessGC ) {
    if ( GPGC_OldCollector::collection_state() == GPGC_Collector::ConcurrentWeakMarking ) { 
      return;
    }
  }
  verify_on(tty); 
}


void oopDesc::verify_old_oop(objectRef* p, bool allow_dirty) {
  blueprint()->oop_verify_old_oop(this, p, allow_dirty);
}


bool oopDesc::partially_loaded() {
  return blueprint()->oop_partially_loaded(this);
}


void oopDesc::set_partially_loaded() {
  blueprint()->oop_set_partially_loaded(this);
}


void oopDesc::print_xml_on(xmlBuffer *xb, bool ref) const {
  if (this == NULL) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("name", "null");
  } else {
    blueprint()->oop_print_xml_on((oop)this, xb, ref);
  }
}

intptr_t oopDesc::slow_identity_hash() {
  assert(!is_shared_readonly(), "using identity hash on readonly object?");
  return ObjectSynchronizer::FastHashCode(this);
}

VerifyOopClosure VerifyOopClosure::verify_oop;
