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


#include "ciConstant.hpp"
#include "ciConstantPoolCache.hpp"
#include "ciEnv.hpp"
#include "ciField.hpp"
#include "ciInstanceKlass.hpp"
#include "ciInstanceKlassKlass.hpp"
#include "ciUtilities.hpp"
#include "commonAsm.hpp"
#include "fieldDescriptor.hpp"
#include "freezeAndMelt.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"

// ciInstanceKlass
//
// This class represents a klassOop in the HotSpot virtual machine
// whose Klass part in an instanceKlass.

// ------------------------------------------------------------------
// ciInstanceKlass::ciInstanceKlass
//
// Loaded instance klass.
ciInstanceKlass::ciInstanceKlass(FAMPtr old_ciik):ciKlass(old_ciik){
assert(old_ciik,"Got NULL!");
  FAM->mapNewToOldPtr(this, old_ciik);

  AccessFlags af;
  af.set_flags_raw(FAM->getInt("((struct ciInstanceKlass*)%p)->_flags->_flags", old_ciik));
  ciFlags flags(af);
  _flags = flags;

_has_finalizer=af.has_finalizer();
  _has_subklass         = FAM->getInt("((struct ciInstanceKlass*)%p)->_has_subklass", old_ciik);
  _is_initialized       = FAM->getInt("((struct ciInstanceKlass*)%p)->_is_initialized", old_ciik);
  _is_linked            = FAM->getInt("((struct ciInstanceKlass*)%p)->_is_linked", old_ciik);
  _cached_size_helper   = FAM->getInt("((struct ciInstanceKlass*)%p)->_cached_size_helper", old_ciik);
  _nonstatic_field_size = FAM->getInt("((struct ciInstanceKlass*)%p)->_nonstatic_field_size", old_ciik);
  Unimplemented();
  //_nonstatic_fields     = FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_nonstatic_field_size", old_ciik);

  _loader            = (jobject)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_loader", old_ciik);
  _protection_domain = (jobject)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_protection_domain", old_ciik);

  _is_shared            = FAM->getInt("((struct ciInstanceKlass*)%p)->_is_shared", old_ciik);

  // These pointers will be fixed up later
  _super  = (ciInstanceKlass*)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_super", old_ciik);
  _java_mirror = (ciInstance*)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_java_mirror", old_ciik);

  GrowableArray<FAMPtr> old_tis;
  int count=0;
  while(true) {
    FAMPtr old_ti = FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_transitive_interfaces[%d]", old_ciik, count);
    if (old_ti == 0) {
      break;
    }
    old_tis.append(old_ti);
    count++;
  }
  Arena* arena = CURRENT_ENV->arena();
  _transitive_interfaces = (ciInstanceKlass**)arena->Amalloc((old_tis.length()+1)*sizeof(ciInstanceKlass*));
for(int i=0;i<old_tis.length();i++){
    _transitive_interfaces[i] = (ciInstanceKlass*)old_tis.at(i);
  }
  _transitive_interfaces[old_tis.length()] = NULL;

  _cached_implementor = (ciInstanceKlass*)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_cached_implementor", old_ciik);

  _field_cache = (ciConstantPoolCache*)FAM->getOldPtr("((struct ciInstanceKlass*)%p)->_field_cache", old_ciik);
}

void ciInstanceKlass::fixupFAMPointers() {
  ciKlass::fixupFAMPointers();

  _super  = (ciInstanceKlass*)FAM->getNewFromOldPtr((FAMPtr)_super);
  _java_mirror  = (ciInstance*)FAM->getNewFromOldPtr((FAMPtr)_java_mirror);

  int i=0;
  while(true) {
    FAMPtr old_tis = (FAMPtr)_transitive_interfaces[i];
    if (old_tis == 0) break;
    _transitive_interfaces[i] = (ciInstanceKlass*)FAM->getNewFromOldPtr((FAMPtr)old_tis);
    i++;
  }

  if ((intptr_t)_cached_implementor > 0x0) {
    _cached_implementor  = (ciInstanceKlass*)FAM->getNewFromOldPtr((FAMPtr)_cached_implementor);
  }

  Arena* arena = CURRENT_ENV->arena();
  _field_cache = new (arena) ciConstantPoolCache(arena, (FAMPtr)_field_cache);
}

ciInstanceKlass::ciInstanceKlass(KlassHandle h_k) : ciKlass(h_k) {
  assert(get_Klass()->oop_is_instance(), "wrong type");
  instanceKlass* ik = get_instanceKlass();

  AccessFlags access_flags = ik->access_flags();
  _flags = ciFlags(access_flags);
  _has_finalizer = access_flags.has_finalizer();
  _has_subklass = ik->subklass() != NULL;
  _is_initialized = ik->is_initialized();
  // Next line must follow and use the result of the previous line:
  _is_linked = _is_initialized || ik->is_linked();
  _nonstatic_field_size = ik->nonstatic_field_size();
  _nonstatic_fields = NULL; // initialized lazily by compute_nonstatic_fields:

  Thread *thread = Thread::current();
  if (ciObjectFactory::is_initialized()) {
    _loader = JNIHandles::make_local(thread, ik->class_loader());
    _protection_domain = JNIHandles::make_local(thread,
                                                ik->protection_domain());
    _is_shared = false;
  } else {
    Handle h_loader(thread, ik->class_loader());
    Handle h_protection_domain(thread, ik->protection_domain());
    _loader = JNIHandles::make_global(h_loader);
    _protection_domain = JNIHandles::make_global(h_protection_domain);
    _is_shared = true;
  }
  
  // Lazy fields get filled in only upon request.
  _super  = NULL;
  _java_mirror = NULL;
_transitive_interfaces=NULL;
  _cached_implementor = (ciInstanceKlass*)-2; // Implementor not cached yet

  if (is_shared()) {
    if (h_k() != SystemDictionary::object_klass()) {
      super();
    }
    java_mirror();
set_unique_concrete_subklass_impl(NULL);
  }

  _field_cache = NULL;
}

// Version for unloaded classes:
ciInstanceKlass::ciInstanceKlass(ciSymbol* name,
                                 jobject loader, jobject protection_domain)
  : ciKlass(name, ciInstanceKlassKlass::make())
{
  assert(name->byte_at(0) != '[', "not an instance klass");
  _is_initialized = false;
  _is_linked = false;
  _is_shared = false;
  _loader = loader;
  _protection_domain = protection_domain;
  _field_cache = NULL;
  _nonstatic_fields = NULL; // initialized lazily by compute_nonstatic_fields:
  _is_shared = false;
  _super = NULL;
  _java_mirror = NULL;
_transitive_interfaces=NULL;
  _cached_implementor = (ciInstanceKlass*)-2; // Implementor not cached yet
}

ciInstanceKlassExtras::ciInstanceKlassExtras() {
  Arena* arena = CURRENT_ENV->arena();
  _FAM_is_unresolved_klass     = new (arena) GrowableArray<bool>       (arena, 4, 0, NULL);
_FAM_cpool_index_to_method=new(arena)GrowableArray<ciMethod*>(arena,4,0,NULL);
_FAM_cpool_index_to_klass=new(arena)GrowableArray<ciKlass*>(arena,4,0,NULL);
_FAM_cpool_index_to_field=new(arena)GrowableArray<ciField*>(arena,4,0,NULL);
_FAM_cpool_index_to_constant=new(arena)GrowableArray<ciConstant*>(arena,4,0,NULL);
_FAM_cpool_index_to_klassref=new(arena)GrowableArray<int>(arena,4,0,0);
_FAM_field_by_offset=new(arena)GrowableArray<ciField*>(arena,4,0,NULL);
  _FAM_is_unresolved_string    = new (arena) GrowableArray<bool>       (arena, 4, 0, 0);
  _is_subtype_of               = new (arena) Dict(cmpkey, hashkey, arena);
  _is_subclass_of              = new (arena) Dict(cmpkey, hashkey, arena);
  _least_common_ancestor       = new (arena) Dict(cmpkey, hashkey, arena);
}

ciInstanceKlassExtras::ciInstanceKlassExtras(FAMPtr old_extras) {
  Arena* arena = CURRENT_ENV->arena();
  FAM->mapNewToOldPtr(this, old_extras);
  int citkr_len                 = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_klassref->_len", old_extras);
  _FAM_cpool_index_to_klassref = new (arena) GrowableArray<int>(arena, citkr_len, 0, 0);
for(int i=0;i<citkr_len;i++){
    int klassref = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_klassref->_data[%d]", old_extras, i);
    _FAM_cpool_index_to_klassref->append(klassref);
  }

  int us_len                = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_is_unresolved_string->_len", old_extras);
  _FAM_is_unresolved_string = new (arena) GrowableArray<bool>(arena, us_len, 0, 0);
for(int i=0;i<us_len;i++){
    bool ret = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_is_unresolved_string->_data[%d]", old_extras, i);
    _FAM_is_unresolved_string->append(ret);
  }

  int citc_len                 = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_constant->_len", old_extras);
_FAM_cpool_index_to_constant=new(arena)GrowableArray<ciConstant*>(arena,citc_len,0,NULL);
for(int i=0;i<citc_len;i++){
    FAMPtr old_cic = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_constant->_data[%d]", old_extras, i);
    if (old_cic == 0) {
_FAM_cpool_index_to_constant->append(NULL);
    } else {
      _FAM_cpool_index_to_constant->append(new (arena) ciConstant(old_cic));
    }
  }

  int iuk_len                = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_is_unresolved_klass->_len", old_extras);
  _FAM_is_unresolved_klass   = new (arena) GrowableArray<bool>(arena, iuk_len, 0, 0);
for(int i=0;i<iuk_len;i++){
    bool ret = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_is_unresolved_klass->_data[%d]", old_extras, i);
    _FAM_is_unresolved_klass->append(ret);
  }

  int citm_len               = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_method->_len", old_extras);
_FAM_cpool_index_to_method=new(arena)GrowableArray<ciMethod*>(arena,citm_len,0,NULL);
for(int i=0;i<citm_len;i++){
    FAMPtr old_cim = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_method->_data[%d]", old_extras, i);
    if (old_cim == 0) {
_FAM_cpool_index_to_method->append(NULL);
    } else {
      ciMethod* cim = (ciMethod*)FAM->getNewFromOldPtr(old_cim);
      _FAM_cpool_index_to_method->append(cim);
    }
  }

  int citk_len               = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_klass->_len", old_extras);
_FAM_cpool_index_to_klass=new(arena)GrowableArray<ciKlass*>(arena,citk_len,0,NULL);
for(int i=0;i<citk_len;i++){
    FAMPtr raw_entry = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_klass->_data[%d]", old_extras, i);
    FAMPtr old_cik = raw_entry & (~(1L<<63));
    if (old_cik == 0) {
_FAM_cpool_index_to_klass->append(NULL);
    } else {
      ciKlass* cik = (ciKlass*)FAM->getNewFromOldPtr(old_cik);
      cik = (ciKlass*)((uintptr_t)cik | (raw_entry & (1L<<63)));
      _FAM_cpool_index_to_klass->append(cik);
    }
  }

  int citf_len               = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_field->_len", old_extras);
_FAM_cpool_index_to_field=new(arena)GrowableArray<ciField*>(arena,citf_len,0,NULL);
for(int i=0;i<citf_len;i++){
    FAMPtr old_cif = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_FAM_cpool_index_to_field->_data[%d]", old_extras, i);
    if (old_cif == 0) {
_FAM_cpool_index_to_field->append(NULL);
    } else {
      _FAM_cpool_index_to_field->append(new ciField(old_cif));
    }
  }

  int fbo_len          = FAM->getInt("((struct ciInstanceKlassExtras*)%p)->_FAM_field_by_offset->_len", old_extras);
_FAM_field_by_offset=new(arena)GrowableArray<ciField*>(arena,fbo_len,0,NULL);
for(int i=0;i<fbo_len;i++){
    FAMPtr old_cif = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_FAM_field_by_offset->_data[%d]", old_extras, i);
    if (old_cif == 0) {
_FAM_field_by_offset->append(NULL);
    } else {
      _FAM_field_by_offset->append(new ciField(old_cif));
    }
  }

  FAMPtr old_st = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_is_subtype_of", old_extras);
  _is_subtype_of = new (CURRENT_ENV->arena()) Dict(cmpkey, hashkey, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_st); i.test(); ++i ) { 
    ciKlass* k = (ciKlass*)FAM->getNewFromOldPtr(i._key);
    _is_subtype_of->Insert(k, (void*)i._value);
  }

  FAMPtr old_sc = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_is_subclass_of", old_extras);
  _is_subclass_of = new (CURRENT_ENV->arena()) Dict(cmpkey, hashkey, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_sc); i.test(); ++i ) { 
    ciKlass* k = (ciKlass*)FAM->getNewFromOldPtr(i._key);
    _is_subclass_of->Insert(k, (void*)i._value);
  }

  FAMPtr old_lca = FAM->getOldPtr("((struct ciInstanceKlassExtras*)%p)->_least_common_ancestor", old_extras);
  _least_common_ancestor = new (CURRENT_ENV->arena()) Dict(cmpkey, hashkey, CURRENT_ENV->arena());
  for( DictFAMI i(FAM, old_lca); i.test(); ++i ) { 
    ciKlass* lha = (ciKlass*)FAM->getNewFromOldPtr(i._key);
    ciKlass* rha = (ciKlass*)FAM->getNewFromOldPtr(i._value);
    _least_common_ancestor->Insert(lha, rha);
  }
}


// ------------------------------------------------------------------
// ciInstanceKlass::compute_shared_is_initialized
bool ciInstanceKlass::compute_shared_is_initialized() {
  GUARDED_VM_ENTRY(
    instanceKlass* ik = get_instanceKlass();
    _is_initialized = ik->is_initialized();
    return _is_initialized;
  )
}

// ------------------------------------------------------------------
// ciInstanceKlass::compute_shared_is_linked
bool ciInstanceKlass::compute_shared_is_linked() {
  GUARDED_VM_ENTRY(
    instanceKlass* ik = get_instanceKlass();
    _is_linked = ik->is_linked();
    return _is_linked;
  )
}

// ------------------------------------------------------------------
// ciInstanceKlass::compute_shared_has_subklass
bool ciInstanceKlass::compute_shared_has_subklass() {
  GUARDED_VM_ENTRY(
    instanceKlass* ik = get_instanceKlass();
    _has_subklass = ik->subklass() != NULL;
    return _has_subklass;
  )
}

bool ciInstanceKlass::get_is_unresolved_klass(int index) {
assert(FAM,"");
  return CURRENT_ENV->get_ciik_extras(this)->_FAM_is_unresolved_klass->at(index);
}

void ciInstanceKlass::add_is_unresolved_klass(int index, bool is_resolved) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_is_unresolved_klass->at_put_grow(index, is_resolved);
}

bool ciInstanceKlass::get_is_unresolved_string(int index) {
assert(FAM,"");
  return CURRENT_ENV->get_ciik_extras(this)->_FAM_is_unresolved_string->at(index);
}

void ciInstanceKlass::add_is_unresolved_string(int index, bool is_resolved) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_is_unresolved_string->at_put_grow(index, is_resolved);
}


int ciInstanceKlass::get_klassref_by_cpool_index(int index) {
assert(FAM,"");
  return CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_klassref->at(index);
}

void ciInstanceKlass::add_klassref_by_cpool_index(int index, int klassref) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_klassref->at_put_grow(index, klassref);
}

ciMethod* ciInstanceKlass::get_method_by_cpool_index(int index) {
assert(FAM,"");
  ciMethod* method = CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_method->at(index);
assert(method,"");
  return method;
}

void ciInstanceKlass::add_method_by_cpool_index(int index, ciMethod* method) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_method->at_put_grow(index, method);
}

ciKlass* ciInstanceKlass::get_klass_by_cpool_index(int index, bool &is_accessible) {
assert(FAM,"");
  uintptr_t entry = (uintptr_t)CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_klass->at(index);
  ciKlass* klass = (ciKlass*)(entry&(~(1L<<63)));
assert(klass,"");
  is_accessible = (entry>>63)&0x1;
  return klass;
}

void ciInstanceKlass::add_klass_by_cpool_index(int index, ciKlass* klass, bool &is_accessible) {
  ciInstanceKlassExtras* extras = CURRENT_ENV->get_ciik_extras(this);
  extras->_FAM_cpool_index_to_klass->at_put_grow(index, (ciKlass*)((((uintptr_t)klass)&(~(1L<<63)))|((long)is_accessible<<63)));
}

ciField* ciInstanceKlass::get_field_by_cpool_index(int index) {
assert(FAM,"");
  ciField* field = CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_field->at(index);
assert(field,"");
  return field;
}

void ciInstanceKlass::add_field_by_cpool_index(int index, ciField* field) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_field->at_put_grow(index, field);
}

ciConstant* ciInstanceKlass::get_constant_by_cpool_index(int index) {
assert(FAM,"");
  ciConstant* constant = CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_constant->at(index);
assert(constant,"");
  return constant;
}

void ciInstanceKlass::add_constant_by_cpool_index(int index, ciConstant* constant) {
  CURRENT_ENV->get_ciik_extras(this)->_FAM_cpool_index_to_constant->at_put_grow(index, constant);
}

ciFlags ciInstanceKlass::flags() const {
    assert(FAM || is_loaded(), "must be loaded");
    return _flags;
}

bool ciInstanceKlass::has_finalizer() const {
  assert(FAM || is_loaded(), "must be loaded");
  return _has_finalizer;
}

bool ciInstanceKlass::has_subklass(){
  assert(FAM || is_loaded(), "must be loaded");
  if (_is_shared && !_has_subklass) {
    if (flags().is_final()) {
      return false;
    } else {
      return compute_shared_has_subklass();
    }
  }
  return _has_subklass;
}

jint ciInstanceKlass::size_helper()  {
  _cached_size_helper = (Klass::layout_helper_size_in_bytes(layout_helper()) >> LogHeapWordSize);
  return _cached_size_helper;
}

jint ciInstanceKlass::nonstatic_field_size()  {
  assert(FAM || is_loaded(), "must be loaded");
  return _nonstatic_field_size;
}

// ------------------------------------------------------------------
// ciInstanceKlass::loader
oop ciInstanceKlass::loader()const{
  ASSERT_IN_VM;
  return JNIHandles::resolve(_loader);
}

// ------------------------------------------------------------------
// ciInstanceKlass::loader_handle
jobject ciInstanceKlass::loader_handle() {
  return _loader;
}

// ------------------------------------------------------------------
// ciInstanceKlass::protection_domain
oop ciInstanceKlass::protection_domain()const{
  ASSERT_IN_VM;
  return JNIHandles::resolve(_protection_domain);
}

// ------------------------------------------------------------------
// ciInstanceKlass::protection_domain_handle
jobject ciInstanceKlass::protection_domain_handle() {
  return _protection_domain;
}

// ------------------------------------------------------------------
// ciInstanceKlass::field_cache
//
// Get the field cache associated with this klass.
ciConstantPoolCache* ciInstanceKlass::field_cache() {
  if (is_shared()) {
    return NULL;
  }
  if (_field_cache == NULL) {
    assert(!is_java_lang_Object(), "Object has no fields");
    Arena* arena = CURRENT_ENV->arena();
    _field_cache = new (arena) ciConstantPoolCache(arena, 5);
  }
  return _field_cache;
}

// ------------------------------------------------------------------
// ciInstanceKlass::get_canonical_holder
//
ciInstanceKlass* ciInstanceKlass::get_canonical_holder(int offset) {
  #ifdef ASSERT
  if (!(offset >= 0 && offset < layout_helper())) {
    tty->print("*** get_canonical_holder(%d) on ", offset);
    this->print(tty);
    tty->print_cr(" ***");
  };
  assert(offset >= 0 && offset < layout_helper(), "offset must be tame");
  #endif

  if (offset < (instanceOopDesc::header_size() * wordSize)) {
    // All header offsets belong properly to java/lang/Object.
    return CURRENT_ENV->Object_klass();
  }
  
  ciInstanceKlass* self = this;
  for (;;) {
assert(FAM||self->is_loaded(),"must be loaded to have size");
    ciInstanceKlass* super = self->super();
    if (super == NULL || !super->contains_field_offset(offset)) {
      return self;
    } else {
      self = super;  // return super->get_canonical_holder(offset)
    }
  }
}

// ------------------------------------------------------------------
// ciInstanceKlass::is_java_lang_Object
//
// Is this klass java.lang.Object?
bool ciInstanceKlass::is_java_lang_Object() {
  return equals(CURRENT_ENV->Object_klass());
}

// ------------------------------------------------------------------
// ciInstanceKlass::uses_default_loader
bool ciInstanceKlass::uses_default_loader() {
  if (FAM) {
return _loader==NULL;
  }
  VM_ENTRY_MARK;
  return loader() == NULL;
}

// ------------------------------------------------------------------
// ciInstanceKlass::print_impl
//
// Implementation of the print method.
void ciInstanceKlass::print_impl(outputStream*out)const{
ciKlass::print_impl(out);
oop l,pd;
if(FAM){
    // Temporary cut-out to allow printing to proceed
l=NULL;
pd=NULL;
  } else {
ThreadInVMfromUnknown tivmfu;
    l = loader(); 
    pd = protection_domain(); 
  }
  if( l ) out->print(" Custom_loader=%p", l);
  if( pd) out->print(" Custom_domain=%p",pd);
  if (is_loaded()) {
out->print(" loaded=true initialized=%s finalized=%s subklass=%s size=%d flags=",
               bool_to_str(_is_initialized),
               bool_to_str(has_finalizer()),
bool_to_str(_has_subklass),
               layout_helper());

    _flags.print_klass_flags(out);

    if (_super) {
out->print(" super=");
      _super->print_name_on(out);
    }
    if (_java_mirror) {
out->print(" mirror=PRESENT");
    }
    if( _cached_implementor != (ciInstanceKlass*)-2 ) { // Cached value?
out->print(" impl=");
      if( _cached_implementor == 0 ) out->print("NONE");
      else if( _cached_implementor == (ciInstanceKlass*)-1 ) out->print("MANY");
      else _cached_implementor->print_name_on(out);
    }
  } else {
out->print(" loaded=false");
  }
}

// ------------------------------------------------------------------
// ciInstanceKlass::super
//
// Get the superklass of this klass.
ciInstanceKlass* ciInstanceKlass::super() {
  if (FAM) {
    return _super;
  }
  assert(is_loaded(), "must be loaded");
  if (_super == NULL && !is_java_lang_Object()) {
    GUARDED_VM_ENTRY(
      klassOop super_klass = get_instanceKlass()->super();
      _super = CURRENT_ENV->get_object(super_klass)->as_instance_klass();
    )
  }
  return _super;
}

// ------------------------------------------------------------------
// ciInstanceKlass::java_mirror
//
// Get the instance of java.lang.Class corresponding to this klass.
ciInstance* ciInstanceKlass::java_mirror() {
  assert(is_loaded(), "must be loaded");
  if (_java_mirror == NULL) {
    _java_mirror = ciKlass::java_mirror();
  }
  return _java_mirror;
}

// ------------------------------------------------------------------
// ciInstanceKlass::transitive_interfaces
//
// Get transitive interface list 
ciInstanceKlass** ciInstanceKlass::trans_inter_impl() {
  GUARDED_VM_ENTRY(
    objArrayOop trans = get_instanceKlass()->transitive_interfaces();
int len=trans->length();
    ciEnv *E = ciEnv::current(thread);
    _transitive_interfaces = (ciInstanceKlass**)E->arena()->Amalloc((len+1)*sizeof(ciInstanceKlass*));
    for( int i = 0; i< len; i++ ) 
_transitive_interfaces[i]=
        E->get_object(trans->obj_at(i))->as_instance_klass();
_transitive_interfaces[len]=NULL;
  );
  return _transitive_interfaces;
}

// ------------------------------------------------------------------
// ciInstanceKlass::unique_concrete_subklass
ciInstanceKlass* ciInstanceKlass::compute_unique_concrete_subklass_impl(CommonAsm *casm) {
  if (!is_loaded()) return this; // No change if class is not loaded
  instanceKlass* ik = get_instanceKlass();
  intptr_t rawimpl = ik->raw_implementor();
  int nof = instanceKlass::nof_implementors(rawimpl);
  if( nof == 2 )                // Many implementors!
    return (ciInstanceKlass*)-1; // Report back 'many'
  if( nof == 0 ) {     // NO implementors!  Must be abstract/interface
    casm->assert_no_implementation(this);
    return NULL;                
  }
  // Compilation depends on 'this' not getting more concrete implementors
  if( !is_final() ) {
    if( !casm ) return this; // Startup-time shared ciKlass assumes pessimistic class load
    casm->assert_leaf_type(this);
  }

  // Get the single best implementor
  klassOop uoop = ik->implementor(rawimpl).as_klassOop(); // Single best implementor
instanceKlass*uik=instanceKlass::cast(uoop);
  if( uik == ik ) return this;  // Self is it!
  // Make sure we cache a consistent set of dependencies here.  It would be
  // possible, e.g., for 'uci' to both be abstract and be the single best
  // implementor at the moment (because it has 2 concrete children), but as
  // soon as we release the jvm_lock class-unloading might remove a subklass
  // and allow the remaining child of 'uci' to be the single best implementor.
  // The CI must cache and return consistent results.
  ciInstanceKlass *uci = CURRENT_ENV->get_object(uoop)->as_instance_klass();
  uci->set_unique_concrete_subklass(casm);
  return uci->nof_implementors(casm) == 2 ? uci : uci->_cached_implementor;
}

void ciInstanceKlass::set_unique_concrete_subklass_impl(CommonAsm *casm) { 
  if (FAM) return;
  GUARDED_VM_ENTRY(_cached_implementor = compute_unique_concrete_subklass_impl(casm);)
}

// ------------------------------------------------------------------
// ciInstanceKlass::has_finalizable_subclass
bool ciInstanceKlass::has_finalizable_subclass(CommonAsm*casm){
  if (!is_loaded())     return true;
  VM_ENTRY_MARK;
if(get_instanceKlass()->find_finalizable_subclass()!=NULL)
    return true;                // Found one!
  // Did not find one; add a dependency to force deopt if a subklass with a
  // finalizer loads.
  casm->assert_has_no_finalizable_subclasses(this);
  return false;
}

bool ciInstanceKlass::has_finalizable_subclass_query() const {
  if (!is_loaded())     return true;
  return get_instanceKlass()->find_finalizable_subclass();
}

static int sort_field_by_offset(ciField** a, ciField** b) {
  return (*a)->offset_in_bytes() - (*b)->offset_in_bytes();
  // (no worries about 32-bit overflow...)
}


// Non-cached query, used to re-check dependencies before code install.
int ciInstanceKlass::nof_implementors_query() const{
  return get_instanceKlass()->nof_implementors();
}

// ------------------------------------------------------------------
// ciInstanceKlass::get_field_by_offset
ciField* ciInstanceKlass::get_field_by_offset(int field_offset, bool is_static) {
  if (FAM) {
    return CURRENT_ENV->get_ciik_extras(this)->_FAM_field_by_offset->at(field_offset);
  }
  VM_ENTRY_MARK;
  instanceKlass* k = get_instanceKlass();
  fieldDescriptor fd;
  if (!k->find_field_from_offset(field_offset, is_static, &fd)) {
    CURRENT_ENV->get_ciik_extras(this)->_FAM_field_by_offset->at_put_grow(field_offset, NULL);
    return NULL;
  }
  ciField* field = new (CURRENT_THREAD_ENV->arena()) ciField(&fd);
  CURRENT_ENV->get_ciik_extras(this)->_FAM_field_by_offset->at_put_grow(field_offset, field);
  return field;
}

// ------------------------------------------------------------------
// ciInstanceKlass::compute_nonstatic_fields
int ciInstanceKlass::compute_nonstatic_fields() {
  assert(is_loaded(), "must be loaded");

  if (_nonstatic_fields != NULL)
    return _nonstatic_fields->length();

  // Size in bytes of my fields, including inherited fields.
  // About equal to size_helper() - sizeof(oopDesc).
  int fsize = nonstatic_field_size() * wordSize;
  if (fsize == 0) {     // easy shortcut
    Arena* arena = CURRENT_ENV->arena();
    _nonstatic_fields = new (arena) GrowableArray<ciField*>(arena, 0, 0, NULL);
    return 0;
  }
  assert(!is_java_lang_Object(), "bootstrap OK");

  ciInstanceKlass* super = this->super();
  int      super_fsize = 0;
  int      super_flen  = 0;
  GrowableArray<ciField*>* super_fields = NULL;
  if (super != NULL) {
    super_fsize  = super->nonstatic_field_size() * wordSize;
    super_flen   = super->nof_nonstatic_fields();
    super_fields = super->_nonstatic_fields;
    assert(super_flen == 0 || super_fields != NULL, "first get nof_fields");
  }

  // See if I am no larger than my super; if so, I can use his fields.
  if (fsize == super_fsize) {
    _nonstatic_fields = super_fields;
    return super_fields->length();
  }

  GrowableArray<ciField*>* fields = NULL;
  GUARDED_VM_ENTRY({
      fields = compute_nonstatic_fields_impl(super_fields);
    });

  if (fields == NULL) {
    // This can happen if this class (java.lang.Class) has invisible fields.
    _nonstatic_fields = super_fields;
    return super_fields->length();
  }

  int flen = fields->length();

  // Now sort them by offset, ascending.
  // (In principle, they could mix with superclass fields.)
  fields->sort(sort_field_by_offset);
#ifdef ASSERT
  int last_offset = sizeof(oopDesc);
  for (int i = 0; i < fields->length(); i++) {
    ciField* field = fields->at(i);
    int offset = field->offset_in_bytes();
    int size   = (field->_type == NULL) ? oopSize : field->size_in_bytes();
    assert(last_offset <= offset, "no field overlap");
    if (last_offset > (int)sizeof(oopDesc))
      assert((offset - last_offset) < BytesPerLong, "no big holes");
    // Note:  Two consecutive T_BYTE fields will be separated by wordSize-1
    // padding bytes if one of them is declared by a superclass.
    // This is a minor inefficiency classFileParser.cpp.
    last_offset = offset + size;
  }
  assert(last_offset <= (int)sizeof(oopDesc) + fsize, "no overflow");
#endif

  _nonstatic_fields = fields;
  return flen;
}

GrowableArray<ciField*>*
ciInstanceKlass::compute_nonstatic_fields_impl(GrowableArray<ciField*>*
                                               super_fields) {
  ASSERT_IN_VM;
  Arena* arena = CURRENT_ENV->arena();
  int flen = 0;
  GrowableArray<ciField*>* fields = NULL;
  instanceKlass* k = get_instanceKlass();
  typeArrayOop fields_array = k->fields();
  for (int pass = 0; pass <= 1; pass++) {
    for (int i = 0, alen = fields_array->length(); i < alen; i += instanceKlass::next_offset) {
      fieldDescriptor fd;
      fd.initialize(k->as_klassOop(), i);
      if (fd.is_static())  continue;
      if (pass == 0) {
        flen += 1;
      } else {
        ciField* field = new (arena) ciField(&fd);
        fields->append(field);
      }
    }

    // Between passes, allocate the array:
    if (pass == 0) {
      if (flen == 0) {
        return NULL;  // return nothing if none are locally declared
      }
      if (super_fields != NULL) {
        flen += super_fields->length();
      }
      fields = new (arena) GrowableArray<ciField*>(arena, flen, 0, NULL);
      if (super_fields != NULL) {
        fields->appendAll(super_fields);
      }
    }
  }
  assert(fields->length() == flen, "sanity");
  return fields;
}

// ------------------------------------------------------------------
// ciInstanceKlass::find_method
//
// Find a method in this klass.
ciMethod* ciInstanceKlass::find_method(ciSymbol* name, ciSymbol* signature) {
  if (FAM) {
    Thread *thread = Thread::current();
    return ciEnv::current(thread)->_factory->get_ciMethod(name, signature);
  }
  VM_ENTRY_MARK;
  instanceKlass* k = get_instanceKlass();
  symbolOop name_sym = name->get_symbolOop();
  symbolOop sig_sym= signature->get_symbolOop();

  methodOop m = k->find_method(name_sym, sig_sym);
  if (m == NULL)  return NULL;

  return CURRENT_THREAD_ENV->get_object(m)->as_method();
}

// ------------------------------------------------------------------
// ciInstanceKlass::is_leaf_type
bool ciInstanceKlass::is_leaf_type(CommonAsm *casm) {
  assert(is_loaded(), "must be loaded");
  if (is_shared()) {
    return is_final();  // approximately correct
  } else {
return!_has_subklass&&(nof_implementors(casm)==1);
  }
}

bool ciInstanceKlass::is_leaf_type_query(){
  assert(is_loaded(), "must be loaded");
  if (is_shared()) {
    return is_final();  // approximately correct
  } else {
    return !_has_subklass && (nof_implementors_query() == 1);
  }
}

