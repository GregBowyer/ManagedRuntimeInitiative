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
#ifndef LOADERCONSTRAINTS_HPP
#define LOADERCONSTRAINTS_HPP

#include "handles.hpp"
#include "hashtable.hpp"
#include "klass.hpp"
#include "symbolOop.hpp"
class Dictionary;
class LoaderConstraintEntry;

class LoaderConstraintTable : public Hashtable {
  friend class VMStructs;
private:

  enum Constants {
    _loader_constraint_size = 107,                     // number of entries in constraint table
    _nof_buckets            = 1009                     // number of buckets in hash table
  };

  LoaderConstraintEntry** find_loader_constraint(symbolHandle name,
                                                 Handle loader);

public:

  LoaderConstraintTable(int nof_buckets);

  LoaderConstraintEntry* new_entry(unsigned int hash, symbolOop name,
                                   klassOop klass, int num_loaders,
                                   int max_loaders);

  LoaderConstraintEntry* bucket(int i) const {
    return (LoaderConstraintEntry*)Hashtable::bucket(i);
  }

  LoaderConstraintEntry** bucket_addr(int i) const {
    return (LoaderConstraintEntry**)Hashtable::bucket_addr(i);
  }

  // GC support
  void oops_do(OopClosure* f);
  void always_strong_classes_do(OopClosure* blk);

  // Check class loader constraints
  bool add_entry(symbolHandle name, klassOop klass1, Handle loader1,
                                    klassOop klass2, Handle loader2);

  void check_signature_loaders(symbolHandle signature, Handle loader1,
                               Handle loader2, bool is_method, TRAPS);

  klassOop find_constrained_klass(symbolHandle name, Handle loader);
  klassOop find_constrained_elem_klass(symbolHandle name, symbolHandle elem_name,
                                       Handle loader, TRAPS);


  // Class loader constraints

  void ensure_loader_constraint_capacity(LoaderConstraintEntry *p, int nfree);
  void extend_loader_constraint(LoaderConstraintEntry* p, Handle loader,
                                klassOop klass);
  void merge_loader_constraints(LoaderConstraintEntry** pp1,
                                LoaderConstraintEntry** pp2, klassOop klass);

  bool check_or_update(instanceKlassHandle k, Handle loader,
                              symbolHandle name);

  
  void purge_loader_constraints(BoolObjectClosure* is_alive);
  void GPGC_purge_loader_constraints_section(long section, long sections);

  void verify(Dictionary* dictionary);
#ifndef PRODUCT
  void print( outputStream *str ) const;
#endif
};

class LoaderConstraintEntry : public HashtableEntry {
  friend class LoaderConstraintTable;
private:
  symbolRef              _name;                   // class name
  int                    _num_loaders;            // both num & max loaders 
                                                  // may increase over time
  int                    _max_loaders;
objectRef*_loaders;//initiating loaders, this array gets bigger over time
  unsigned int*          _loader_hash_keys;       // hashes are used to avoid lvb-ing otherwise unreferenced old loaders
public:

  klassRef klass() const       { return  klassRef(literal().raw_value()); }
  klassRef* klass_addr()       { return (klassRef*) literal_addr(); }
  void set_klass(klassRef k) { set_literal(k); }

  LoaderConstraintEntry* next() {
    return (LoaderConstraintEntry*)HashtableEntry::next();
  }

  LoaderConstraintEntry** next_addr() {
    return (LoaderConstraintEntry**)HashtableEntry::next_addr();
  }
  void set_next(LoaderConstraintEntry* next) {
    HashtableEntry::set_next(next);
  }

  symbolRef name() const { return lvb_symbolRef(&_name); }
  symbolRef* name_addr() { return &_name; }
  void set_name(symbolRef name) { _name = name; }

  int num_loaders() const { return _num_loaders; }
  void set_num_loaders(int i) { _num_loaders = i; }

  int max_loaders() const { return _max_loaders; }
  void set_max_loaders(int i) { _max_loaders = i; }

  objectRef* loaders() { return _loaders; }
  void set_loaders(objectRef* loaders) { _loaders = loaders; }

  objectRef loader(int i) const { return lvb_ref(&(_loaders[i])); }
  objectRef GC_loader(int i) { return ALWAYS_UNPOISON_OBJECTREF(_loaders[i]); }
  objectRef* loader_addr(int i) { return &_loaders[i]; }
  void set_loader(int i, objectRef p) { _loaders[i] = p; }
  unsigned int loader_hash(int i) { return _loader_hash_keys[i]; }

  bool tst_set_klass( klassRef ko ) {
    // One-shot CAS; don't care WHO succeeds, just that it is not null when done
    if( !(ko.is_null()) && klass().is_null() )
      Atomic::cmpxchg_ptr( ko.raw_value(), (intptr_t*) klass_addr(), NULL );
    // Return FALSE if classes do not match
    return klass().as_klassOop() == UNPOISON_KLASSREF(ko).as_klassOop();
  }

#ifndef PRODUCT
  void print( outputStream *str ) const;
#endif
};

#endif // LOADERCONSTRAINTS_HPP
