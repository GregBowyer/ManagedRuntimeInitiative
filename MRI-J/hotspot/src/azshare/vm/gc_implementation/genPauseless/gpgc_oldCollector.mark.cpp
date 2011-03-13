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


#include "atomic.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_space.hpp"
#include "gpgc_traps.hpp"
#include "javaClasses.hpp"
#include "klassIds.hpp"

#include "atomic_os_pd.inline.hpp"
#include "gctrap_os_pd.inline.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "gpgc_pageInfo.inline.hpp"
#include "oop.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

// Initialize marking at the start of each mark-remap phase.
void GPGC_OldCollector::marking_init() {
  // During marking, we want to enable GCLdValueTr traps, which will catch unremapped
  // objectRefs to objects that have been relocated.
Thread*current=Thread::current();
  assert0(current->is_GenPauselessGC_thread());

  current->set_gc_mode(false);
current->set_gcthread_lvb_trap_vector();
  GPGC_Heap::old_gc_task_manager()->request_new_gc_mode(false);
}


// Cleanup marking at the end of each mark-remap phase.
void GPGC_OldCollector::marking_cleanup() {
}


void GPGC_OldCollector::mark_to_live(objectRef* ref_addr) {
objectRef new_ref;

  bool result = objectRef_needed_nmt(ref_addr, new_ref);

  assert( new_ref.is_null() || new_ref.is_old(), "only use mark_to_live() when it's certain that the ref is in old_space" );
  assert( result==false || GPGC_Marks::is_old_marked_strong_live(new_ref),
"expected reference to live object, actually found dead object");
}


void GPGC_OldCollector::mark_to_live_or_new(objectRef* ref_addr) {
objectRef new_ref;

  bool result = objectRef_needed_nmt(ref_addr, new_ref);

  assert( result==false || (new_ref.is_old() && GPGC_Marks::is_old_marked_strong_live(new_ref)),
"expected reference to live object, actually found dead object");
}


// This is a special case marker for SymbolTable and StringTable garbage collection.
// It's intended to mark live a WeakRef object that has been kept alive by a concurrent
// mutator looking it up in a hash table.  Calling this on an oop that has internal
// objectRefs will result in heap corruption!  Use it only for SymbolOops and instances
// of strings.
void GPGC_OldCollector::mark_leaf_guaranteed(oop obj){
  assert0( Thread::current()->is_GC_task_thread() || Thread::current()->is_GenPauselessGC_thread() );

  // TODO: maw: assert that the obj's klass is SymbolKlass.

  if ( ! GPGC_Marks::atomic_mark_live_if_dead(obj) ) {
    // This is only for use on leaf objects where no one else should have
    // been able to mark it live ahead of us.
    ShouldNotReachHere();
  }

  GPGC_Marks::set_markid(obj, 0x70);
  GPGC_Marks::set_marked_through(obj);

  // When we mark a leaf object live, we need to make sure the klassRef is properly NMTed.
  mark_to_live(obj->klass_addr());

int size=obj->size();
  GPGC_Space::add_live_object(obj, size);
  if (ProfileLiveObjects) {
    Thread::current()->live_objects()->add(obj->blueprint()->klassId(), KlassIds::string_intern_root, 1, size);
  }
}


// This is a special case marker for SymbolTable and StringTable garbage collection.
// It's intended to mark live a WeakRef object that has been kept alive by a concurrent
// mutator looking it up in a hash table.  Calling this on an oop that has internal
// objectRefs will result in heap corruption!  Use it only for SymbolOops and instances
// of strings.
void GPGC_OldCollector::mark_leaf_conditional(oop obj){
  assert0( Thread::current()->is_GC_task_thread() || Thread::current()->is_GenPauselessGC_thread() );

  if ( GPGC_Marks::atomic_mark_live_if_dead(obj) ) {
    GPGC_Marks::set_markid(obj, 0x71);
    GPGC_Marks::set_marked_through(obj);

    // When we mark a leaf object live, we need to make sure the klassRef is properly NMTed.
    mark_to_live(obj->klass_addr());

int size=obj->size();
    GPGC_Space::add_live_object(obj, size);
    if (ProfileLiveObjects) {
      Thread::current()->live_objects()->add(obj->blueprint()->klassId(), KlassIds::string_intern_root, 1, size);
    }
  }
}


void GPGC_OldCollector::mutator_mark_leaf_conditional(oop obj){
assert0(Thread::current()->is_Java_thread());
  assert0(GPGC_Space::is_in_old_or_perm(obj));

  if ( GPGC_Marks::atomic_mark_live_if_dead(obj) ) {
    GPGC_Marks::set_markid(obj, 0x72);
    GPGC_Marks::set_marked_through(obj);

    objectRef r = GPGC_OldCollector::remap_and_nmt_only(obj->klass_addr());
    Klass*    k = klassOop(r.as_oop())->klass_part();

    // Allow us to get the size without taking any traps
    int size = obj->GC_size_given_klass(k);
    GPGC_Space::add_live_object(obj, size);
    if (ProfileLiveObjects) {
      Thread::current()->live_objects()->add(k->klassId(), KlassIds::string_intern_root, 1, size);
    }
  } else { 
    // Always make sure the klassRef is properly NMTed even if we did not mark it ourselves.
    GPGC_OldCollector::remap_and_nmt_only(obj->klass_addr());
  }
}


void GPGC_OldCollector::mark_leaf_string_guaranteed(oop string) {
  // Mark the string object alive.
  GPGC_OldCollector::mark_leaf_guaranteed(string);

  assert0(java_lang_String::is_instance(string));

  // If needed, update the NMT and remap the objectRef.
  objectRef* value_ref_addr = java_lang_String::value_addr(string);
objectRef value_ref;
  objectRef_needed_nmt(value_ref_addr, value_ref);

  // Mark the value typeArray of chars alive
  GPGC_OldCollector::mark_leaf_conditional(value_ref.as_oop());
}


void GPGC_OldCollector::mark_leaf_string_conditional(oop string) {
  // Mark the string object alive.
  GPGC_OldCollector::mark_leaf_conditional(string);

  assert0(java_lang_String::is_instance(string));

  // If needed, update the NMT and remap the objectRef.
  objectRef* value_ref_addr = java_lang_String::value_addr(string);
objectRef value_ref;
  objectRef_needed_nmt(value_ref_addr, value_ref);

  // Mark the value typeArray of chars alive
  GPGC_OldCollector::mark_leaf_conditional(value_ref.as_oop());
}


void GPGC_OldCollector::mutator_mark_leaf_string_conditional(oop string) {
assert0(Thread::current()->is_Java_thread());

  // Mark the string object alive.
  GPGC_OldCollector::mutator_mark_leaf_conditional(string);

  assert0(java_lang_String::is_instance(string));

  // If needed, update the NMT and remap the objectRef for value char[].
  objectRef value_ref = GPGC_OldCollector::remap_and_nmt_only(java_lang_String::value_addr(string));

  // Mark the value typeArray of chars alive
  GPGC_OldCollector::mutator_mark_leaf_conditional(value_ref.as_oop());
}
