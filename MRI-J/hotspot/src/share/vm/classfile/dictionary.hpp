/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef DICTIONARY_HPP
#define DICTIONARY_HPP

#include "gpgc_oldCollector.hpp"
#include "hashtable.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"

class DictionaryEntry;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// The data structure for the system dictionary (and the shared system
// dictionary).

class Dictionary : public TwoOopHashtable {
private:
  // current iteration index.
  static int                    _current_class_index;
  // pointer to the current hash table entry.
  static DictionaryEntry*       _current_class_entry;
  // number of classes unloaded in this GC cycle
  intptr_t                      _number_of_unloaded_classes;

  DictionaryEntry* get_entry(int index, unsigned int hash,
                             symbolHandle name, Handle loader);

  DictionaryEntry* bucket(int i) {
    return (DictionaryEntry*)Hashtable::bucket(i);
  }

  // The following method is not MT-safe and must be done under lock.
  DictionaryEntry** bucket_addr(int i) {
    return (DictionaryEntry**)Hashtable::bucket_addr(i);
  }

  void add_entry(int index, DictionaryEntry* new_entry) {
    Hashtable::add_entry(index, (HashtableEntry*)new_entry);
  }


public:
  Dictionary(int table_size);
  Dictionary(int table_size, HashtableBucket* t, int number_of_entries);
  
  DictionaryEntry* new_entry(unsigned int hash, klassOop klass, oop loader);

  DictionaryEntry* new_entry();

  void free_entry(DictionaryEntry* entry);

  void add_klass(symbolHandle class_name, Handle class_loader,KlassHandle obj);

  klassOop find_class(int index, unsigned int hash,
                      symbolHandle name, Handle loader);

  klassOop find_shared_class(int index, unsigned int hash, symbolHandle name);

  // Compiler support
  klassOop try_get_next_class();

  // GC support

  void oops_do(OopClosure* f);
  void always_strong_classes_do(OopClosure* blk);
void GC_always_strong_classes_do(OopClosure*blk);
  void classes_do(void f(klassOop));
  void classes_do(void f(klassOop, TRAPS), TRAPS);
void GC_classes_do(void f(klassOop));
  void classes_do(void f(klassOop, oop));
  void classes_do(void f(klassOop, oop, TRAPS), TRAPS);

  void methods_do(void f(methodOop));


  // Classes loaded by the bootstrap loader are always strongly reachable.
  // If we're not doing class unloading, all classes are strongly reachable.
  static bool is_strongly_reachable(oop class_loader, oop klass) {
    assert (klass != NULL, "should have non-null klass");
    return (class_loader == NULL || !ClassUnloading);
  }

  // Unload (that is, break root links to) all unmarked classes and
  // loaders.  Returns "true" iff something was unloaded.
  bool do_unloading(BoolObjectClosure* is_alive);
  bool GPGC_unload_section(GPGC_GCManagerOldStrong* gcm, long section, long sections); // for parallel unloading under GenPauselessHeap
  inline void reset_unloaded_classes()     { _number_of_unloaded_classes = 0; }
  inline intptr_t number_of_unloaded_classes()  { return _number_of_unloaded_classes; }


  // Protection domains
  klassOop find(int index, unsigned int hash, symbolHandle name,
                Handle loader, Handle protection_domain, TRAPS);
  bool is_valid_protection_domain(int index, unsigned int hash,
                                  symbolHandle name, Handle class_loader,
                                  Handle protection_domain);
  void add_protection_domain(int index, unsigned int hash,
                             instanceKlassHandle klass, Handle loader,
                             Handle protection_domain, TRAPS);

  // Sharing support
  void dump(SerializeOopClosure* soc);
  void restore(SerializeOopClosure* soc);
  void reorder_dictionary();

  
#ifndef PRODUCT
  void print();
#endif
  void verify();
};

// The following classes can be in dictionary.cpp, but we need these
// to be in header file so that SA's vmStructs can access.

class ProtectionDomainEntry :public CHeapObj {
 private:
  objectRef              _protection_domain;

 public:
  ProtectionDomainEntry* _next;

  ProtectionDomainEntry(oop protection_domain, ProtectionDomainEntry* next) {
_protection_domain=ALWAYS_POISON_OBJECTREF(objectRef(protection_domain));
    _next              = next;
  }

  ProtectionDomainEntry* next() { return _next; }
objectRef protection_domain(){return lvb_ref(&_protection_domain);}
  objectRef* protection_domain_addr() { return &_protection_domain; }
};

// An entry in the system dictionary, this describes a class as
// { klassOop, loader, protection_domain }.

class DictionaryEntry : public HashtableEntry {
 private:
  // Contains the set of approved protection domains that can access
  // this system dictionary entry.
  ProtectionDomainEntry* _pd_set;
  objectRef              _loader;
objectRef _name;
  unsigned int           _loader_hash;


 public:
  // Tells whether a protection is in the approved set.
  bool contains_protection_domain(oop protection_domain) const;
  // Adds a protection domain to the approved set.
  void add_protection_domain(oop protection_domain);

  klassRef klass() const { return klassRef(literal().raw_value()); }
  klassRef* klass_addr() { return (klassRef*) literal_addr(); }

  DictionaryEntry* next() const {
    return (DictionaryEntry*)HashtableEntry::next();
  }

  DictionaryEntry** next_addr() {
    return (DictionaryEntry**)HashtableEntry::next_addr();
  }

  objectRef loader() const    { return lvb_ref(&_loader); }
  objectRef GC_loader() const { return ALWAYS_UNPOISON_OBJECTREF(_loader); }
  
  void set_loader(objectRef loader) { 
    _loader = loader; 
    if ( loader.is_null() ) {
      _loader_hash = 0;
    } else {
      _loader_hash = ((ALWAYS_UNPOISON_OBJECTREF( loader )).as_oop())->identity_hash();
    }
  }
  
  objectRef*    loader_addr()   { return &_loader;        }
  unsigned int  loader_hash()   { return _loader_hash;    }
  objectRef     name() const    { return lvb_ref(&_name); }
objectRef*name_addr(){return&_name;}
void set_name(objectRef name){_name=name;}
  
  ProtectionDomainEntry* pd_set() const { return _pd_set; }
  void set_pd_set(ProtectionDomainEntry* pd_set) { _pd_set = pd_set; }

  bool has_protection_domain() { return _pd_set != NULL; }

  // Tells whether the initiating class' protection can access the this _klass
  bool is_valid_protection_domain(Handle protection_domain) {
    if (!ProtectionDomainVerification) return true;
    if (!SystemDictionary::has_checkPackageAccess()) return true;

    return protection_domain() == NULL
         ? true
         : contains_protection_domain(protection_domain());
  }

  void GPGC_follow_contents() {
    for (ProtectionDomainEntry* current = _pd_set; 
                                current != NULL; 
                                current = current->_next) {
      objectRef* ref_addr = current->protection_domain_addr();
      assert0(objectRef::is_null_or_heap(ref_addr));
      GPGC_OldCollector::mark_to_live_or_new((heapRef*)ref_addr);
    }
  }

  void protection_domain_set_oops_do(OopClosure* f) {
    for (ProtectionDomainEntry* current = _pd_set;
                                current != NULL;
                                current = current->_next) {
f->do_oop(current->protection_domain_addr());
    }
  }

  void verify_protection_domain_set() {
    for (ProtectionDomainEntry* current = _pd_set;
                                current != NULL;
                                current = current->_next) {
current->protection_domain().as_oop()->verify();
    }
  }

  bool equals(symbolOop class_name, oop class_loader) {
    klassOop klass = (klassOop) literal().as_oop();
    if ((((class_loader == NULL) ? 0 : class_loader->identity_hash()) == loader_hash()) && (name().as_oop() == class_name)) {
      return (loader().as_oop() == class_loader);
    } else {
      return false;
    }
  }

  void print() {
    int count = 0;
    for (ProtectionDomainEntry* current = _pd_set;
                                current != NULL;
                                current = current->_next) {
      count++;
    }
    tty->print_cr("pd set = #%d", count);
  }
};

#endif // DICTIONARY_HPP

