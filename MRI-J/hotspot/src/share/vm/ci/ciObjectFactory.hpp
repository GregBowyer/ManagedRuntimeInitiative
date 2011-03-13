/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CIOBJECTFACTORY_HPP
#define CIOBJECTFACTORY_HPP


#include "allocation.hpp"
#include "ciObject.hpp"
#include "ciUtilities.hpp"
#include "growableArray.hpp"

// ciObjectFactory
//
// This class handles requests for the creation of new instances
// of ciObject and its subclasses.  It contains a caching mechanism
// which ensures that for each oop, at most one ciObject is created.
// This invariant allows efficient implementation of ciObject.
class ciObjectFactory : public ResourceObj {
private:
  static volatile bool _initialized;
  static ciSymbol*                 _shared_ci_symbols[];
  static int                       _shared_ident_limit;

  // FAM only: Some objects, such as statics, are not made through the usual 
  //           get_object mechanism, but still need have their pointers "fixed up."
  //           Only used during FAM runs.
GrowableArray<ciObject*>*_FAM_fixup_list;

  Arena*                    _arena;
  static ciObject** _shared_hashtable; // Common shared hashtable for all compiles
  static int _shared_hashmax;   // Shared hashtable size, a power of 2
  ciObject** _hashtable;        // Private per-compile-thread hashtable
  int _hashmax;                 // Private hashtable size, a power of 2
  int _hashcnt;                 // Entry count in hashtable
  GrowableArray<ciMethod*>* _unloaded_methods;
  GrowableArray<ciKlass*>* _unloaded_klasses;
  GrowableArray<ciReturnAddress*>* _return_addresses;
  int                       _next_ident;

public:
  struct NonPermObject : public ResourceObj {
    ciObject*      _object;
    NonPermObject* _next;

    inline NonPermObject(NonPermObject* &bucket, oop key, ciObject* object);
    ciObject*     object()  { return _object; }
    NonPermObject* &next()  { return _next; }
  };
private:
  enum { NON_PERM_BUCKETS = 61 };
  NonPermObject* _non_perm_bucket[NON_PERM_BUCKETS];
  int _non_perm_count;

  bool is_found_at(int index, oop key, GrowableArray<ciObject*>* objects);
  void insert(int index, ciObject* obj, GrowableArray<ciObject*>* objects);
  ciObject* create_new_object(oop o);
  static bool is_equal(NonPermObject* p, oop key) {
    return p->object()->get_oop() == key;
  }

  NonPermObject* &find_non_perm(oop key);
  void insert_non_perm(NonPermObject* &where, oop key, ciObject* obj);

  void init_ident_of(ciObject* obj);

  Arena* arena() { return _arena; }

  void print_contents_impl();

public:
  static bool is_initialized() { return _initialized; }

  static void initialize();
  void init_shared_objects();
  void init_shared_objects_FAM();

  ciObject* create_from_FAMPtr(FAMPtr fp);

  ciObjectFactory(Arena* arena, int expected_size);
  ciObjectFactory(Arena* arena, FAMPtr old_ciof);

  int populate_table_from_FAM(ciObject** table, FAMPtr old_table_ptr, int old_table_size);

  // Get the ciObject corresponding to some oop.
  ciObject* get(oop key);

  // Get the ciSymbol corresponding to one of the vmSymbols.
  static ciSymbol* vm_symbol_at(int index);

  // Get the ciMethod representing an unloaded/unfound method.
  ciMethod* get_unloaded_method(ciInstanceKlass* holder,
                                ciSymbol*        name,
                                ciSymbol*        signature);

  // Get a ciKlass representing an unloaded klass.
  ciKlass* get_unloaded_klass(ciKlass* accessing_klass,
                              ciSymbol* name,
                              bool create_if_not_found);


  // Get a previously existing ciKlass via KID, or NULL
  ciKlass *get_klass_by_kid(int kid) const;

  ciReturnAddress* get_return_address(int bci);

  void print_contents();
  void print();

  ciSymbol *get_symbol_from_string(const char* str);
ciKlass*get_ciKlass(ciSymbol*klassname);
ciMethod*get_ciMethod(ciSymbol*name,ciSymbol*signature);
  ciObjArrayKlass *get_ciObjArrayKlass_from_element(ciKlass* element_klass);
ciTypeArrayKlass*get_ciTypeArrayKlass(BasicType bt);
};

#endif // CIOBJECTFACTORY_HPP
