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


#include "ciEnv.hpp"
#include "ciKlass.hpp"
#include "ciUtilities.hpp"
#include "interfaceSupport.hpp"
#include "ostream.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "objArrayKlass.hpp"

// ciKlass
//
// This class represents a klassOop in the HotSpot virtual
// machine.

// ------------------------------------------------------------------
// ciKlass::ciKlass
ciKlass::ciKlass(KlassHandle h_k):ciType(h_k),_klassId(0){
  assert(get_oop()->is_klass(), "wrong type");
  Klass* k = get_Klass();
  _layout_helper = k->layout_helper();
  symbolOop klass_name = k->name();
  assert(klass_name != NULL, "wrong ciKlass constructor");
  _name = CURRENT_ENV->get_object(klass_name)->as_symbol();
  _super_of_depth = NEW_ARENA_ARRAY(CURRENT_ENV->arena(), ciKlass*, Klass::primary_super_limit());
  _java_mirror = NULL;
for(uint i=0;i<Klass::primary_super_limit();i++){
_super_of_depth[i]=NULL;
  }
}

ciKlass::ciKlass(FAMPtr old_cik) : ciType(old_cik), _klassId(0) {
  FAM->mapNewToOldPtr(this, old_cik);
  _name = (ciSymbol*)FAM->getOldPtr("((struct ciKlass*)%p)->_name", old_cik);
  _klassId = FAM->getInt("((struct ciKlass*)%p)->_klassId", old_cik);
  _super_check_offset = FAM->getInt("((struct ciKlass*)%p)->_super_check_offset", old_cik);
  _access_flags   = FAM->getInt("((struct ciKlass*)%p)->_access_flags", old_cik);
  _modifier_flags = FAM->getInt("((struct ciKlass*)%p)->_modifier_flags", old_cik);

  _super_depth    = FAM->getInt("((struct ciKlass*)%p)->_super_depth", old_cik);
  _super_of_depth = NEW_ARENA_ARRAY(CURRENT_ENV->arena(), ciKlass*, Klass::primary_super_limit());
for(uint i=0;i<Klass::primary_super_limit();i++){
    _super_of_depth[i] = (ciKlass*)FAM->getOldPtr("((struct ciKlass*)%p)->_super_of_depth[%d]", old_cik, i);
  }
  _can_be_primary_super = FAM->getInt("((struct ciKlass*)%p)->_can_be_primary_super", old_cik);
  _layout_helper = FAM->getInt("((struct ciKlass*)%p)->_layout_helper", old_cik);

  _java_mirror = (ciInstance*)FAM->getOldPtr("(intptr_t)((struct ciKlass*)%p)->_java_mirror", old_cik);
}

void ciKlass::fixupFAMPointers() {
  ciType::fixupFAMPointers();
  _name = (ciSymbol*)FAM->getNewFromOldPtr((FAMPtr)_name);
for(uint i=0;i<Klass::primary_super_limit();i++){
    _super_of_depth[i] = (ciKlass*)FAM->getNewFromOldPtr((FAMPtr)_super_of_depth[i]);
  }
  _java_mirror = (ciInstance*)FAM->getNewFromOldPtr((FAMPtr)_java_mirror);
}

// ------------------------------------------------------------------
// ciKlass::ciKlass
//
// Nameless klass variant.
ciKlass::ciKlass(KlassHandle h_k,ciSymbol*name):ciType(h_k),_klassId(0){
  assert(get_oop()->is_klass(), "wrong type");
  _name = name;
  _layout_helper = Klass::_lh_neutral_value;
  _super_of_depth = NEW_ARENA_ARRAY(CURRENT_ENV->arena(), ciKlass*, Klass::primary_super_limit());
  _java_mirror = NULL;
for(uint i=0;i<Klass::primary_super_limit();i++){
_super_of_depth[i]=NULL;
  }
}

// ------------------------------------------------------------------
// ciKlass::ciKlass
//
// Unloaded klass variant.
ciKlass::ciKlass(ciSymbol* name, ciKlass* klass) : ciType(klass) {
  _name = name;
  _layout_helper = Klass::_lh_neutral_value;
  _super_of_depth = NEW_ARENA_ARRAY(CURRENT_ENV->arena(), ciKlass*, Klass::primary_super_limit());
  _java_mirror = NULL;
for(uint i=0;i<Klass::primary_super_limit();i++){
_super_of_depth[i]=NULL;
  }
}

unsigned ciKlass::klassId() {
  assert0( FAM || is_loaded() );
  return _klassId ? _klassId : klassId_impl();
}

// ------------------------------------------------------------------
// ciKlass::is_subtype_of
bool ciKlass::is_subtype_of(ciKlass* that) {
  Dict* iso = CURRENT_ENV->get_ciik_extras((ciInstanceKlass*)this)->_is_subtype_of;
  if (iso->contains(that)) {
    return (bool)(*iso)[that];
  }

  // Check to see if the klasses are identical.
  if (this == that) {
    iso->Insert(that, (void*)true);
    return true;
  }
  if( that == CURRENT_ENV->Object_klass() ) {
    iso->Insert(that, (void*)true);
    return true;                // Always a subklass of object
  }
  if( this == CURRENT_ENV->Object_klass() ) {
    iso->Insert(that, (void*)false);
    return false;               // Object never a subtype of something other than Object
  }
  assert(is_loaded() && that->is_loaded(), "must be loaded");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
  klassOop that_klass = that->get_klassOop();
  bool result = this_klass->is_subtype_of(that_klass);

  iso->Insert(that, (void*)result);
  return result;
}

// ------------------------------------------------------------------
// ciKlass::is_subclass_of
bool ciKlass::is_subclass_of(ciKlass* that) {
  Dict* iso = CURRENT_ENV->get_ciik_extras((ciInstanceKlass*)this)->_is_subclass_of;
  if (iso->contains(that)) {
    return (bool)(*iso)[that];
  }

  // Check to see if the klasses are identical.
  if (this == that) {
    iso->Insert(that, (void*)true);
    return true;
  }
  if( that == CURRENT_ENV->Object_klass() ) {
    iso->Insert(that, (void*)true);
    return true;                // Always a subklass of object
  }
  if( this == CURRENT_ENV->Object_klass() ) {
    iso->Insert(that, (void*)false);
    return false;               // Object never a subtype of something other than Object
  }
  assert(is_loaded() && that->is_loaded(), "must be loaded");
  assert(is_java_klass() && that->is_java_klass(), "must be java klasses");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
  klassOop that_klass = that->get_klassOop();
  bool result = this_klass->is_subclass_of(that_klass);

  iso->Insert(that, (void*)result);
  return result;
}

// ------------------------------------------------------------------
// ciKlass::super_depth
juint ciKlass::super_depth() {
  if (FAM) {
    return _super_depth;
  }
  assert(is_loaded(), "must be loaded");
  assert(is_java_klass(), "must be java klasses");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
_super_depth=this_klass->super_depth();
  return _super_depth;
}

// ------------------------------------------------------------------
// ciKlass::super_check_offset
juint ciKlass::super_check_offset() {
  if (FAM) {
    return _super_check_offset;
  }
  assert(is_loaded(), "must be loaded");
  assert(is_java_klass(), "must be java klasses");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
_super_check_offset=this_klass->super_check_offset();
  return _super_check_offset;
}

// ------------------------------------------------------------------
// ciKlass::super_of_depth
ciKlass* ciKlass::super_of_depth(juint i) {
  if (FAM) {
return _super_of_depth[i];
  }
  assert(is_loaded(), "must be loaded");
  assert(is_java_klass(), "must be java klasses");
  if( _super_of_depth[i] ) return _super_of_depth[i];

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
unsigned int kid=this_klass->primary_super_of_depth(i);
if(kid==0)return NULL;
  klassOop super = KlassTable::getKlassByKlassId(kid).as_klassOop();
  _super_of_depth[i] = super ? CURRENT_THREAD_ENV->get_object(super)->as_klass() : NULL;
return _super_of_depth[i];
}

// ------------------------------------------------------------------
// ciKlass::can_be_primary_super
bool ciKlass::can_be_primary_super() {
  if (FAM) {
    return _can_be_primary_super;
  }
  assert(is_loaded(), "must be loaded");
  assert(is_java_klass(), "must be java klasses");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
_can_be_primary_super=this_klass->can_be_primary_super();
  return _can_be_primary_super;
}

// ------------------------------------------------------------------
// ciKlass::least_common_ancestor
//
// Get the shared parent of two klasses.
//
// Implementation note: this method currently goes "over the wall"
// and does all of the work on the VM side.  It could be rewritten
// to use the super() method and do all of the work (aside from the
// lazy computation of super()) in native mode.  This may be
// worthwhile if the compiler is repeatedly requesting the same lca
// computation or possibly if most of the superklasses have already
// been created as ciObjects anyway.  Something to think about...
ciKlass*ciKlass::least_common_ancestor(ciKlass*that){
  Dict* dlca = CURRENT_ENV->get_ciik_extras((ciInstanceKlass*)this)->_least_common_ancestor;
  if (dlca->contains(that)) {
    return (ciKlass*)(*dlca)[that];
  }

assert(!FAM,"In FAM mode, we should have found this in the dictionary");

  if( that == CURRENT_ENV->Object_klass() ) {
    dlca->Insert(that, that);
    return that;
  }
  if( this == CURRENT_ENV->Object_klass() ) {
    dlca->Insert(that, this);
    return this;
  }
  // Check to see if the klasses are identical.
  if (this == that) {
    dlca->Insert(that, this);
    return this;
  }
  assert(is_loaded() && that->is_loaded(), "must be loaded");
  assert(is_java_klass() && that->is_java_klass(), "must be java klasses");

  VM_ENTRY_MARK;
  Klass* this_klass = get_Klass();
  Klass* that_klass = that->get_Klass();
  Klass* lca        = this_klass->LCA(that_klass);

  // Many times the LCA will be either this_klass or that_klass.
  // Treat these as special cases.
  if (lca == that_klass) {
    dlca->Insert(that, that);
    return that;
  }
  if (this_klass == lca) {
    dlca->Insert(that, this);
    return this;
  }

  // Create the ciInstanceKlass for the lca.
  ciKlass* result =
    CURRENT_THREAD_ENV->get_object(lca->as_klassOop())->as_klass();

  dlca->Insert(that, result);
  return result;
}

// ------------------------------------------------------------------
// ciKlass::find_klass
//
// Find a klass using this klass's class loader.
ciKlass* ciKlass::find_klass(ciSymbol* klass_name) {
  assert(is_loaded(), "cannot find_klass through an unloaded klass");
  return CURRENT_ENV->get_klass_by_name(this,
					klass_name, false);
}

// ------------------------------------------------------------------
// ciKlass::java_mirror
ciInstance* ciKlass::java_mirror() {
  if (FAM) {
    return _java_mirror;
  }
  GUARDED_VM_ENTRY(
    oop java_mirror = get_Klass()->java_mirror();
_java_mirror=CURRENT_ENV->get_object(java_mirror)->as_instance();
    return _java_mirror;
  )
}

// ------------------------------------------------------------------
// ciKlass::modifier_flags
jint ciKlass::modifier_flags() {
  if (FAM) {
    return _modifier_flags;
  }
  assert(is_loaded(), "not loaded");
  GUARDED_VM_ENTRY(
_modifier_flags=get_Klass()->modifier_flags();
    return _modifier_flags;
  )
}

// ------------------------------------------------------------------
// ciKlass::access_flags
jint ciKlass::access_flags() {
  if (FAM) {
    return _access_flags;
  }
  assert(is_loaded(), "not loaded");
  GUARDED_VM_ENTRY(
_access_flags=get_Klass()->access_flags().as_int();
    return _access_flags;
  )
}

// ------------------------------------------------------------------
// ciKlass::klassId
unsigned ciKlass::klassId_impl() {
  if (FAM) {
    return _klassId;
  }
  GUARDED_VM_ENTRY( return (_klassId = get_Klass()->klassId()); )
}

unsigned ciKlass::element_klassId() {
  GUARDED_VM_ENTRY(
    return objArrayKlass::cast(get_klassOop())->element_klass()->klass_part()->klassId();
  )
}

// ------------------------------------------------------------------
// ciKlass::klassId
ciKlass *ciKlass::make_from_klassId(int kid) {
  ciKlass *cnew = CURRENT_ENV->get_klass_by_kid(kid);
  if( cnew ) return cnew;       // See if we've loaded this ciKlass before

  VM_ENTRY_MARK;
  klassRef k = KlassTable::getKlassByKlassId(kid);
  // KIDs from CodeProfiles for virtual calls (CPData_Invoke) are not GC'd and
  // sometimes the Klass for a profiled KID has been collected (hence the
  // klassTable has a NULL for this KID) or recycled to a new unrelated Klass.
  // The user of this data (C2) has to make sure the returned value is sane.
if(k.is_null())return NULL;
  ciObject *cio = CURRENT_THREAD_ENV->get_object(k.as_oop());
  assert0( cio->is_klass() && cio == CURRENT_THREAD_ENV->get_klass_by_kid(kid) );
return(ciKlass*)cio;
}

// ------------------------------------------------------------------
// ciKlass::print_impl
//
// Implementation of the print method
void ciKlass::print_impl(outputStream*out)const{
out->print(" name=");
print_name_on(out);
}

// ------------------------------------------------------------------
// ciKlass::print_name
//
// Print the name of this klass
void ciKlass::print_name_on(outputStream*st)const{
  name()->print_symbol_on(st);
}

