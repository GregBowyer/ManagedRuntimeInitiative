/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "classLoadingService.hpp"
#include "dictionary.hpp"
#include "gpgc_marks.hpp"
#include "jvmtiExport.hpp"
#include "safepoint.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

DictionaryEntry*  Dictionary::_current_class_entry = NULL;
int               Dictionary::_current_class_index =    0;


Dictionary::Dictionary(int table_size)
  : TwoOopHashtable(table_size, sizeof(DictionaryEntry)) {
  _current_class_index = 0;
  _current_class_entry = NULL;
};



Dictionary::Dictionary(int table_size, HashtableBucket* t,
                       int number_of_entries)
  : TwoOopHashtable(table_size, sizeof(DictionaryEntry), t, number_of_entries) {
  _current_class_index = 0;
  _current_class_entry = NULL;
};


DictionaryEntry* Dictionary::new_entry(unsigned int hash, klassOop klass,
                                       oop loader) {
  DictionaryEntry* entry;
entry=(DictionaryEntry*)Hashtable::new_entry(hash,POISON_KLASSREF(klassRef(klass)));
entry->set_loader(ALWAYS_POISON_OBJECTREF(objectRef(loader)));
  entry->set_name(ALWAYS_POISON_OBJECTREF(objectRef(instanceKlass::cast(klass)->name())));
  entry->set_pd_set(NULL);
  return entry;
}


DictionaryEntry* Dictionary::new_entry() {
DictionaryEntry*entry=(DictionaryEntry*)Hashtable::new_entry(0L,nullRef);
entry->set_loader(nullRef);
  entry->set_name(nullRef);
  entry->set_pd_set(NULL);
  return entry;
}


void Dictionary::free_entry(DictionaryEntry* entry) {
  // avoid recursion when deleting linked list
  while (entry->pd_set() != NULL) {
    ProtectionDomainEntry* to_delete = entry->pd_set();
    entry->set_pd_set(to_delete->next());
    delete to_delete;
  }
  Hashtable::free_entry(entry);
}


bool DictionaryEntry::contains_protection_domain(oop protection_domain) const {
#ifdef ASSERT
if(protection_domain==instanceKlass::cast(klass().as_klassOop())->protection_domain()){
    // Ensure this doesn't show up in the pd_set (invariant)
    bool in_pd_set = false;
    for (ProtectionDomainEntry* current = _pd_set; 
                                current != NULL; 
                                current = current->next()) {
if(current->protection_domain().as_oop()==protection_domain){
	in_pd_set = true;
	break;
      }
    }
    if (in_pd_set) {
      assert(false, "A klass's protection domain should not show up "
                    "in its sys. dict. PD set");
    }
  }
#endif /* ASSERT */

  if (protection_domain == instanceKlass::cast(klass().as_klassOop())->protection_domain()) {
    // Succeeds trivially
    return true;
  }

  for (ProtectionDomainEntry* current = _pd_set; 
                              current != NULL; 
                              current = current->next()) {
if(current->protection_domain().as_oop()==protection_domain)return true;
  }
  return false;
}


void DictionaryEntry::add_protection_domain(oop protection_domain) {
  assert_locked_or_safepoint(SystemDictionary_lock);
  if (!contains_protection_domain(protection_domain)) {
    ProtectionDomainEntry* new_head =
                new ProtectionDomainEntry(protection_domain, _pd_set);
    // Warning: Preserve store ordering.  The SystemDictionary is read
    //          without locks.  The new ProtectionDomainEntry must be
    //          complete before other threads can be allowed to see it
    //          via a store to _pd_set.
    OrderAccess::release_store_ptr(&_pd_set, new_head);
  }
}


bool Dictionary::do_unloading(BoolObjectClosure* is_alive) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")
  bool class_was_unloaded = false;
  int  index = 0; // Defined here for portability! Do not move

  // Remove unloadable entries and classes from system dictionary
  // The placeholder array has been handled in always_strong_oops_do.
  DictionaryEntry* probe = NULL;
  for (index = 0; index < table_size(); index++) {
    for (DictionaryEntry** p = bucket_addr(index); *p != NULL; ) {
      probe = *p;
klassOop e=probe->klass().as_klassOop();
oop class_loader=probe->loader().as_oop();

      instanceKlass* ik = instanceKlass::cast(e);

      // Non-unloadable classes were handled in always_strong_oops_do
      if (!is_strongly_reachable(class_loader, e)) {
        // Entry was not visited in phase1 (negated test from phase1)
        assert(class_loader != NULL, "unloading entry with null class loader");
        oop k_def_class_loader = ik->class_loader();

        // Do we need to delete this system dictionary entry?
        bool purge_entry = false;

        // Do we need to delete this system dictionary entry?
        if (!is_alive->do_object_b(class_loader)) {
          // If the loader is not live this entry should always be
          // removed (will never be looked up again). Note that this is
          // not the same as unloading the referred class.
          if (k_def_class_loader == class_loader) {
            // This is the defining entry, so the referred class is about
            // to be unloaded.
            // Notify the debugger, and clean up the class.
            guarantee(!is_alive->do_object_b(e),
                      "klass should not be live if defining loader is not");
            class_was_unloaded = true;
            // notify the debugger
            if (JvmtiExport::should_post_class_unload()) {
              JvmtiExport::post_class_unload(ik->as_klassOop());
            }

            // notify ClassLoadingService of class unload
            ClassLoadingService::notify_class_unloaded(ik);
            ik->dec_implementor(is_alive);

            // Clean up C heap
            ik->release_C_heap_structures();
          }
          // Also remove this system dictionary entry.
          purge_entry = true;

        } else {
          // The loader in this entry is alive. If the klass is dead,
          // the loader must be an initiating loader (rather than the
          // defining loader). Remove this entry.
          if (!is_alive->do_object_b(e)) {
            guarantee(!is_alive->do_object_b(k_def_class_loader),
                      "defining loader should not be live if klass is not");
            // If we get here, the class_loader must not be the defining
            // loader, it must be an initiating one.
            assert(k_def_class_loader != class_loader,
                   "cannot have live defining loader and unreachable klass");

            // Loader is live, but class and its defining loader are dead.
            // Remove the entry. The class is going away.
            purge_entry = true;
          }
        }

        if (purge_entry) {
          *p = probe->next();
          if (probe == _current_class_entry) {
            _current_class_entry = NULL;
          }
          free_entry(probe);
          continue;
        }
      }
      p = probe->next_addr();
    }
  }
  return class_was_unloaded;
}

bool Dictionary::GPGC_unload_section(GPGC_GCManagerOldStrong* gcm, long section, long sections) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")
  bool class_was_unloaded = false;
  int  index = 0; // Defined here for portability! Do not move

  assert0((section>=0) && (section<sections));

  long section_size = table_size() / sections;
  long section_start = section * section_size;
  long section_end = section_start + section_size;
  if ( (section+1) == sections ) {
    section_end = table_size();
  }

  // Remove unloadable entries and classes from system dictionary
  // The placeholder array has been handled in always_strong_oops_do.
  DictionaryEntry* probe = NULL;
  for (index = section_start; index < section_end; index++) {
    for (DictionaryEntry** p = bucket_addr(index); *p != NULL; ) {
      probe = *p;

      objectRef klass_ref      = GPGC_Collector::remap_only(probe->klass_addr());
      objectRef class_loader_r = GPGC_Collector::remap_only(probe->loader_addr());
      klassOop  e              = (klassOop) klass_ref.as_oop();
      oop       class_loader_o = class_loader_r.as_oop();

      assert0(klass_ref.is_old());

      instanceKlass* ik = instanceKlass::cast(e);

      // Non-unloadable classes were handled in always_strong_oops_do
if(!is_strongly_reachable(class_loader_o,e)){
        // Entry was not visited in phase1 (negated test from phase1)
assert(class_loader_o!=NULL,"unloading entry with null class loader");
        oop k_def_class_loader = GPGC_Collector::remap_only(ik->adr_class_loader()).as_oop();

        // Do we need to delete this system dictionary entry?
        bool purge_entry = false;

        // For the purposes of GPGC OldGC, NewGen objects are always live:
        if ( !class_loader_r.is_new()
          && !GPGC_Marks::is_old_marked_strong_live(class_loader_r)
          && !GPGC_Marks::is_old_marked_final_live(class_loader_r) )
        {
          // If the loader is not reachable this entry should always be removed (will never be looked up again).
          // Note that this is not the same as unloading the referred class.
if(k_def_class_loader==class_loader_o){
            // This is the defining entry, so the referred class is about to be unloaded.
            // Notify the debugger, and clean up the class.
            guarantee( !GPGC_Marks::is_old_marked_strong_live(klass_ref)
              && !GPGC_Marks::is_old_marked_final_live(klass_ref),
"klass is marked but should not be if defining loader is not marked");

            class_was_unloaded = true;

            // notify the debugger
            if (JvmtiExport::should_post_class_unload()) {
              JvmtiExport::post_class_unload(ik->as_klassOop());
            }

            // notify ClassLoadingService of class unload
ClassLoadingService::GPGC_notify_class_unloaded(ik);
Atomic::inc_ptr(&_number_of_unloaded_classes);

            // With parallel class unloading, the unloadees are saved and the superklass
            // _implementors are fixed in the next step of the GC.
            // the c-heap-structures are also released at the same time to get the correct dependencies
            gcm->dec_implementors_stack()->push(ik);
          }
          // Also remove this system dictionary entry.
          purge_entry = true;

        } else {
          // The loader in this entry is alive. If the klass is dead, the loader must be an
          // initiating loader (rather than the defining loader). Remove this entry.
          // The klass is always in OldGen, so we don't need the NewGen==live test here.
          if( !GPGC_Marks::is_old_marked_strong_live(klass_ref)
            && !GPGC_Marks::is_old_marked_final_live(klass_ref) )
          {
            // The defining class loader might be in new, so don't really know if it's live:
            // guarantee(!GPGC_Marks::is_marked_live(k_def_class_loader), "defining loader should not be marked if klass is not");
            // If we get here, the class_loader must not be the defining loader, it must be an initiating one.
            assert(k_def_class_loader != class_loader_o, "cannot have live defining loader and unreachable klass");

            // Loader is live, but class and its defining loader are dead.
            // Remove the entry. The class is going away.
            purge_entry = true;
          }
        }

        if (purge_entry) {
          // Purge entry
          *p = probe->next();
          if (probe == _current_class_entry) {
            _current_class_entry = NULL;
          }
          free_entry(probe);
          continue;
        } else {
          GPGC_OldCollector::mark_to_live(probe->klass_addr());
          GPGC_OldCollector::mark_to_live_or_new((heapRef*) probe->loader_addr());
          GPGC_OldCollector::mark_to_live((heapRef*) probe->name_addr());
probe->GPGC_follow_contents();
        }
      }
      p = probe->next_addr();
    }
  }

  return class_was_unloaded;
}


void Dictionary::always_strong_classes_do(OopClosure* blk) {
  // Follow all system classes and temporary placeholders in dictionary
  for (int index = 0; index < table_size(); index++) {
for(DictionaryEntry*probe=bucket(index);probe;probe=probe->next()){
oop e=(probe->klass()).as_oop();
oop class_loader=(probe->loader()).as_oop();
      if (is_strongly_reachable(class_loader, e)) {
        blk->do_oop((objectRef*)probe->klass_addr());
blk->do_oop((objectRef*)probe->name_addr());
        if (class_loader != NULL) {
          blk->do_oop(probe->loader_addr());
        }
        probe->protection_domain_set_oops_do(blk);
      }
    }
  }
}


void Dictionary::GC_always_strong_classes_do(OopClosure*blk){
  assert0(!Thread::current()->is_Java_thread());
  // Follow all system classes and temporary placeholders in dictionary
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry *probe = bucket(index); probe; probe = probe->next()) {
      oop e = (probe->klass()).as_oop();
      oop class_loader = (probe->GC_loader()).as_oop();            
      if (is_strongly_reachable(class_loader, e)) {
        blk->do_oop((objectRef*)probe->klass_addr());
blk->do_oop((objectRef*)probe->name_addr());
        if (class_loader != NULL) {
          blk->do_oop(probe->loader_addr());
        }
        probe->protection_domain_set_oops_do(blk);
      }
    }
  }
}


//   Just the classes from defining class loaders
void Dictionary::classes_do(void f(klassOop)) {
  for (int index = 0; index < table_size(); index++) {
for(DictionaryEntry*probe=bucket(index);probe;probe=probe->next()){
klassOop k=probe->klass().as_klassOop();
if(probe->loader().as_oop()==instanceKlass::cast(k)->class_loader()){
        f(k);
      }
    }
  }
}

// Added for initialize_itable_for_klass to handle exceptions
//   Just the classes from defining class loaders
void Dictionary::classes_do(void f(klassOop, TRAPS), TRAPS) {
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
klassOop k=probe->klass().as_klassOop();
      if (probe->loader() == instanceKlass::cast(k)->class_loader()) {
        f(k, CHECK);
      }
    }
  }
}

// clone of the above function for GC purposes: at this point it is used only by the allocation-profiler
// it uses the non-lvb accessor for the loader
void Dictionary::GC_classes_do(void f(klassOop)){
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index); probe; probe = probe->next()) {
      klassOop k = probe->klass().as_klassOop();
      if (probe->GC_loader().as_oop() == instanceKlass::cast(k)->GC_class_loader()) {
        f(k);
      }
    }
  }
}


//   All classes, and their class loaders
//   (added for helpers that use HandleMarks and ResourceMarks)
// Don't iterate over placeholders
void Dictionary::classes_do(void f(klassOop, oop, TRAPS), TRAPS) {
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
klassOop k=probe->klass().as_klassOop();
f(k,probe->loader().as_oop(),CHECK);
    }
  }
}


//   All classes, and their class loaders
// Don't iterate over placeholders
void Dictionary::classes_do(void f(klassOop, oop)) {
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
klassOop k=probe->klass().as_klassOop();
f(k,probe->loader().as_oop());
    }
  }
}


void Dictionary::oops_do(OopClosure* f) {
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
      f->do_oop((objectRef*)probe->klass_addr());
f->do_oop((objectRef*)probe->name_addr());
      if (probe->loader_addr()->not_null()) {
        f->do_oop(probe->loader_addr());
      }
      probe->protection_domain_set_oops_do(f);
    }
  }
}


void Dictionary::methods_do(void f(methodOop)) {
  for (int index = 0; index < table_size(); index++) {
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
klassOop k=probe->klass().as_klassOop();
if(probe->loader().as_oop()==instanceKlass::cast(k)->class_loader()){
        // only take klass is we have the entry with the defining class loader
        instanceKlass::cast(k)->methods_do(f);
      }
    }
  }
}


klassOop Dictionary::try_get_next_class() {
  while (true) {
    if (_current_class_entry != NULL) {
klassOop k=_current_class_entry->klass().as_klassOop();
      _current_class_entry = _current_class_entry->next();
      return k;
    }
    _current_class_index = (_current_class_index + 1) % table_size();
    _current_class_entry = bucket(_current_class_index);
  }
  // never reached
}


// Add a loaded class to the system dictionary.
// Readers of the SystemDictionary aren't always locked, so _buckets
// is volatile. The store of the next field in the constructor is
// also cast to volatile;  we do this to ensure store order is maintained
// by the compilers.

void Dictionary::add_klass(symbolHandle class_name, Handle class_loader,
                           KlassHandle obj) {
  assert_locked_or_safepoint(SystemDictionary_lock);
  assert(obj() != NULL, "adding NULL obj");
  assert(Klass::cast(obj())->name() == class_name(), "sanity check on name");

  unsigned int hash = compute_hash(class_name, class_loader);
  int index = hash_to_index(hash);
  DictionaryEntry* entry = new_entry(hash, obj(), class_loader());
  add_entry(index, entry);
}


// This routine does not lock the system dictionary.
//
// Since readers don't hold a lock, we must make sure that system
// dictionary entries are only removed at a safepoint (when only one
// thread is running), and are added to in a safe way (all links must
// be updated in an MT-safe manner).
//
// Callers should be aware that an entry could be added just after
// _buckets[index] is read here, so the caller will not see the new entry.
DictionaryEntry* Dictionary::get_entry(int index, unsigned int hash,
                                       symbolHandle class_name,
                                       Handle class_loader) {
  symbolOop name_ = class_name();
  oop loader_ = class_loader();
  debug_only(_lookup_count++);
  for (DictionaryEntry* entry = bucket(index); 
                        entry != NULL; 
                        entry = entry->next()) {
    if (entry->hash() == hash && entry->equals(name_, loader_)) {
      return entry;
    }
    debug_only(_lookup_length++);
  }
  return NULL;
}


klassOop Dictionary::find(int index, unsigned int hash, symbolHandle name,
                          Handle loader, Handle protection_domain, TRAPS) {
  DictionaryEntry* entry = get_entry(index, hash, name, loader);
  if (entry != NULL && entry->is_valid_protection_domain(protection_domain)) {
return entry->klass().as_klassOop();
  } else {
    return NULL;
  }
}


klassOop Dictionary::find_class(int index, unsigned int hash,
                                symbolHandle name, Handle loader) {
  assert_locked_or_safepoint(SystemDictionary_lock);
  assert (index == index_for(name, loader), "incorrect index?");

  DictionaryEntry* entry = get_entry(index, hash, name, loader);
return(entry!=NULL)?entry->klass().as_klassOop():(klassOop)NULL;
}


// Variant of find_class for shared classes.  No locking required, as
// that table is static.

klassOop Dictionary::find_shared_class(int index, unsigned int hash,
                                       symbolHandle name) {
  assert (index == index_for(name, Handle()), "incorrect index?");

  DictionaryEntry* entry = get_entry(index, hash, name, Handle());
return(entry!=NULL)?entry->klass().as_klassOop():(klassOop)NULL;
}


void Dictionary::add_protection_domain(int index, unsigned int hash,
                                       instanceKlassHandle klass,
                                       Handle loader, Handle protection_domain,
                                       TRAPS) {
  symbolHandle klass_name(THREAD, klass->name());
  DictionaryEntry* entry = get_entry(index, hash, klass_name, loader);

  assert(entry != NULL,"entry must be present, we just created it");
  assert(protection_domain() != NULL, 
         "real protection domain should be present");

  entry->add_protection_domain(protection_domain());

  assert(entry->contains_protection_domain(protection_domain()), 
         "now protection domain should be present");
}


bool Dictionary::is_valid_protection_domain(int index, unsigned int hash,
                                            symbolHandle name,
                                            Handle loader,
                                            Handle protection_domain) {
  DictionaryEntry* entry = get_entry(index, hash, name, loader);
  return entry->is_valid_protection_domain(protection_domain);
}


void Dictionary::reorder_dictionary() {

  // Copy all the dictionary entries into a single master list.

  DictionaryEntry* master_list = NULL;
  for (int i = 0; i < table_size(); ++i) {
    DictionaryEntry* p = bucket(i);
    while (p != NULL) {
      DictionaryEntry* tmp;
      tmp = p->next();
      p->set_next(master_list);
      master_list = p;
      p = tmp;
    }
    set_entry(i, NULL);
  }

  // Add the dictionary entries back to the list in the correct buckets.
  Thread *thread = Thread::current();

  while (master_list != NULL) {
    DictionaryEntry* p = master_list;
    master_list = master_list->next();
    p->set_next(NULL);
symbolHandle class_name(thread,instanceKlass::cast((klassOop)(p->klass().as_klassOop()))->name());
unsigned int hash=compute_hash(class_name,Handle(thread,p->loader().as_oop()));
    int index = hash_to_index(hash);
    p->set_hash(hash);
    p->set_next(bucket(index));
    set_entry(index, p);
  }
}


// ----------------------------------------------------------------------------
#ifndef PRODUCT

void Dictionary::print() {
  ResourceMark rm;
  HandleMark   hm;

  tty->print_cr("Java system dictionary (classes=%d)", number_of_entries());
  tty->print_cr("^ indicates that initiating loader is different from "
                "defining loader");

  for (int index = 0; index < table_size(); index++) {    
    for (DictionaryEntry* probe = bucket(index);
                          probe != NULL;
                          probe = probe->next()) {
      if (Verbose) tty->print("%4d: ", index);
klassOop e=probe->klass().as_klassOop();
oop class_loader=probe->loader().as_oop();
      bool is_defining_class = 
         (class_loader == instanceKlass::cast(e)->class_loader());
      tty->print("%s%s", is_defining_class ? " " : "^", 
                   Klass::cast(e)->external_name());
      if (class_loader != NULL) {
        tty->print(", loader ");
        class_loader->print_value();
      }
      tty->cr();
    }
  }
}

#endif

void Dictionary::verify() {
  guarantee(number_of_entries() >= 0, "Verify of system dictionary failed");
  int element_count = 0;
  for (int index = 0; index < table_size(); index++) {
for(DictionaryEntry*probe=bucket(index);probe;probe=probe->next()){
klassOop e=probe->klass().as_klassOop();
oop class_loader=probe->loader().as_oop();
      guarantee(Klass::cast(e)->oop_is_instance(), 
                              "Verify of system dictionary failed");
      // class loader must be present;  a null class loader is the
      // boostrap loader
      guarantee(class_loader == NULL || class_loader->is_instance(), 
                "checking type of class_loader");
      e->verify();
      probe->verify_protection_domain_set();
      element_count++; 
    }
  }
  guarantee(number_of_entries() == element_count,
            "Verify of system dictionary failed");
  debug_only(verify_lookup_length((double)number_of_entries() / table_size()));
}
