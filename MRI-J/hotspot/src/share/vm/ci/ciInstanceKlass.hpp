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
#ifndef CIINSTANCEKLASS_HPP
#define CIINSTANCEKLASS_HPP


#include "ciFlags.hpp"
#include "ciKlass.hpp"
#include "instanceKlass.hpp"

class CommonAsm;

// ciInstanceKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part is an instanceKlass.  It may or may not
// be loaded.
class ciInstanceKlass : public ciKlass {
  CI_PACKAGE_ACCESS
  friend class ciEnv;
  friend class ciMethod;
  friend class ciField;
  friend class ciBytecodeStream;
  friend class ClassLoader;
  friend class CommonAsm;

private:
  bool                   _is_shared;

  jobject                _loader;
  jobject                _protection_domain;

  bool                   _is_initialized;
  bool                   _is_linked;
  bool                   _has_finalizer;
  bool                   _has_subklass;
  ciFlags                _flags;
  jint                   _cached_size_helper;
  jint                   _nonstatic_field_size;
  
  // Lazy fields get filled in only upon request.
  ciInstanceKlass*       _super;
  ciInstance*            _java_mirror;
  ciInstanceKlass**      _transitive_interfaces; // null-terminated list
  // Single unique concrete implementor, for abstract and interface classes.
  // Values are: 
  // -2 - not cached yet
  // -1 - many (2 or more) implementors
  //  0 - zero implementors
  // xx - a valid ciInstanceKlass that is the single concrete implementor
ciInstanceKlass*_cached_implementor;
  ciInstanceKlass* compute_unique_concrete_subklass_impl(CommonAsm *casm);
  void set_unique_concrete_subklass_impl(CommonAsm *casm);
  void set_unique_concrete_subklass(CommonAsm *casm) { 
    if( _cached_implementor == (ciInstanceKlass*)-2 ) set_unique_concrete_subklass_impl(casm); }

  ciConstantPoolCache*   _field_cache;  // cached map index->field
  GrowableArray<ciField*>* _nonstatic_fields;

protected:
  ciInstanceKlass(KlassHandle h_k);
  ciInstanceKlass(FAMPtr old_ciik);
  ciInstanceKlass(ciSymbol* name, jobject loader, jobject protection_domain);

  void fixupFAMPointers();

  instanceKlass* get_instanceKlass() const {
    return (instanceKlass*)get_Klass();
  }

  oop loader() const;
  jobject loader_handle();

oop protection_domain()const;
  jobject protection_domain_handle();

  const char* type_string() { return "ciInstanceKlass"; }

  void print_impl(outputStream *out) const;

  ciConstantPoolCache* field_cache();

  bool is_shared() { return _is_shared; }

  bool compute_shared_is_initialized();
  bool compute_shared_is_linked();
  bool compute_shared_has_subklass();
  int  compute_shared_nof_implementors();
  int  compute_nonstatic_fields();
  GrowableArray<ciField*>* compute_nonstatic_fields_impl(GrowableArray<ciField*>* super_fields);

public:
  // Has this klass been initialized?
  bool                   is_initialized() {
    if (_is_shared && !_is_initialized) {
      return is_loaded() && compute_shared_is_initialized();
    }
    return _is_initialized;
  }
  // Has this klass been linked?
  bool                   is_linked() {
    if (_is_shared && !_is_linked) {
      return is_loaded() && compute_shared_is_linked();
    }
    return _is_linked;
  }

  // General klass information.
ciFlags flags()const;
virtual bool has_finalizer()const;
bool has_subklass();
jint size_helper();
jint nonstatic_field_size();
  ciInstanceKlass*       super();

  ciInstanceKlass* get_canonical_holder(int offset);
  ciField* get_field_by_offset(int field_offset, bool is_static);
  int nof_nonstatic_fields() {
    if (_nonstatic_fields == NULL)
      return compute_nonstatic_fields();
    else
      return _nonstatic_fields->length();
  }
  // nth nonstatic field (presented by ascending address)
  ciField* nonstatic_field_at(int i) {
    assert(_nonstatic_fields != NULL, "");
    return _nonstatic_fields->at(i);
  }

  bool has_finalizable_subclass(CommonAsm *casm);
  bool has_finalizable_subclass_query() const;

  // Single unique concrete implementor, for abstract and interface classes.
  // Values are: 
  // -2 - not cached yet
  // -1 - many (2 or more) implementors
  //  0 - zero implementors
  // xx - a valid ciInstanceKlass that is the single concrete implementor
  // Returned value is NULL for 'no progress' - self is the only valid implementor.
  ciInstanceKlass* unique_concrete_subklass(CommonAsm *casm) {
    set_unique_concrete_subklass(casm);
    return (_cached_implementor == (ciInstanceKlass*)-1) ? NULL : // -1 is MANY
      ((_cached_implementor==this) ? NULL : _cached_implementor); // Self is no-progress
  }
  int nof_implementors(CommonAsm *casm) {
    set_unique_concrete_subklass(casm);
    return (_cached_implementor == (ciInstanceKlass*)-1) ? 2 : // -1 is MANY
      ((_cached_implementor == NULL) ? 0 : 1); // // NULL is 0, other is 1
  }
  int nof_implementors_query() const; // Non-cached
  void assert_implementor_cached() const { assert0( _cached_implementor != (ciInstanceKlass*)-2 ); }
  ciInstanceKlass *raw_implementor() const { return _cached_implementor; }

  bool contains_field_offset(int offset) {
      return (offset/wordSize) >= instanceOopDesc::header_size()
             && (offset/wordSize)-instanceOopDesc::header_size() < nonstatic_field_size();
  }

  // Get the instance of java.lang.Class corresponding to
  // this klass.  This instance is used for locking of
  // synchronized static methods of this klass.
  ciInstance*            java_mirror();

  ciInstanceKlass** transitive_interfaces() { return _transitive_interfaces ? _transitive_interfaces:trans_inter_impl();}
  ciInstanceKlass** trans_inter_impl();

  // Java access flags
bool is_public()const{return flags().is_public();}
bool is_final()const{return flags().is_final();}
bool is_super()const{return flags().is_super();}
bool is_interface()const{return flags().is_interface();}
bool is_abstract()const{return flags().is_abstract();}

  ciMethod* find_method(ciSymbol* name, ciSymbol* signature);
  // Note:  To find a method from name and type strings, use ciSymbol::make,
  // but consider adding to vmSymbols.hpp instead.

  bool is_leaf_type(CommonAsm *casm);
  bool is_leaf_type_query();
  // Is the defining class loader of this class the default loader?
  bool uses_default_loader();

  bool is_java_lang_Object();

  // What kind of ciObject is this?
  bool is_instance_klass() { return true; }
  bool is_java_klass()     { return true; }

  ciMethod* get_method_by_cpool_index(int index);
  void add_method_by_cpool_index(int index, ciMethod* method);

  ciKlass* get_klass_by_cpool_index(int index, bool &is_accessible);
  void add_klass_by_cpool_index(int index, ciKlass* klass, bool &is_accessible);

  ciField* get_field_by_cpool_index(int index);
  void add_field_by_cpool_index(int index, ciField* field);

  ciConstant* get_constant_by_cpool_index(int index);
  void add_constant_by_cpool_index(int index, ciConstant* constant);

int get_klassref_by_cpool_index(int index);
  void add_klassref_by_cpool_index(int index, int klassref);

  bool get_is_unresolved_klass(int index);
  void add_is_unresolved_klass(int index, bool is_unresolved);

  bool get_is_unresolved_string(int index);
  void add_is_unresolved_string(int index, bool is_unresolved);
};

class ciInstanceKlassExtras:public ResourceObj{
 public:
  ciInstanceKlassExtras();
  ciInstanceKlassExtras(FAMPtr old_extras);

  GrowableArray<bool>*        _FAM_is_unresolved_klass;
GrowableArray<ciMethod*>*_FAM_cpool_index_to_method;
GrowableArray<ciKlass*>*_FAM_cpool_index_to_klass;
GrowableArray<ciField*>*_FAM_cpool_index_to_field;
  GrowableArray<ciConstant*>* _FAM_cpool_index_to_constant;
GrowableArray<int>*_FAM_cpool_index_to_klassref;
GrowableArray<ciField*>*_FAM_field_by_offset;
  GrowableArray<bool>*        _FAM_is_unresolved_string;
  Dict*                       _is_subtype_of;
  Dict*                       _is_subclass_of;
  Dict*                       _least_common_ancestor;
};

#endif // CIINSTANCEKLASS_HPP
