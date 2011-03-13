/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "ciField.hpp"
#include "ciInstance.hpp"
#include "ciNullObject.hpp"
#include "interfaceSupport.hpp"
#include "javaClasses.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"

// ciInstance
//
// This class represents an instanceOop in the HotSpot virtual
// machine.

ciInstance::ciInstance(instanceHandle h_i) : ciObject(h_i), _java_mirror_type(NULL) {
  assert(h_i()->is_instance(), "wrong type");
  _field_offset_to_constant = new (ciEnv::current()->arena()) GrowableArray<ciConstant*>(ciEnv::current()->arena(), 4, 0, NULL);
}

ciInstance::ciInstance(ciKlass* klass) : ciObject(klass), _java_mirror_type(NULL) {
  _field_offset_to_constant = new (ciEnv::current()->arena()) GrowableArray<ciConstant*>(ciEnv::current()->arena(), 4, 0, NULL);
}

ciInstance::ciInstance(FAMPtr old_cii) : ciObject(old_cii) {
  FAM->mapNewToOldPtr(this, old_cii);

  _java_mirror_type = (ciType*)FAM->getOldPtr("((struct ciInstance*)%p)->_java_mirror_type", old_cii);

  int size = FAM->getInt("((struct ciInstance*)%p)->_field_offset_to_constant->_len", old_cii);
  _field_offset_to_constant = new (ciEnv::current()->arena()) GrowableArray<ciConstant*>(ciEnv::current()->arena(), size, 0, NULL);
  for(int i=0; i<size; i++) {
    FAMPtr old_cic = FAM->getOldPtr("((struct ciInstance*)%p)->_field_offset_to_constant->_data[%d]", old_cii, i);
    if (old_cic != 0) {
      _field_offset_to_constant->at_put_grow(i, new (ciEnv::current()->arena()) ciConstant(old_cic));
    }
  }
}

void ciInstance::fixupFAMPointers() {
  ciObject::fixupFAMPointers();

  _java_mirror_type = (ciType*)FAM->getNewFromOldPtr((FAMPtr)_java_mirror_type);
}

// ------------------------------------------------------------------
// ciObject::java_mirror_type
ciType* ciInstance::java_mirror_type() {
  if (_java_mirror_type) return _java_mirror_type;

  VM_ENTRY_MARK;
  oop m = get_oop();
  // Return NULL if it is not java.lang.Class.
  if (m == NULL || m->klass() != SystemDictionary::class_klass()) {
_java_mirror_type=NULL;
    return _java_mirror_type;
  }
  // Return either a primitive type or a klass.
  if (java_lang_Class::is_primitive(m)) {
_java_mirror_type=ciType::make(java_lang_Class::primitive_type(m));
    return _java_mirror_type;
  } else {
    klassOop k = java_lang_Class::as_klassOop(m);
    assert(k != NULL, "");
_java_mirror_type=CURRENT_THREAD_ENV->get_object(k)->as_klass();
    return _java_mirror_type;
  }
}

// ------------------------------------------------------------------
// ciInstance::field_value
//
// Constant value of a field.
ciConstant ciInstance::field_value(ciField* field) {
  int offset = field->offset();
  ciConstant* orig = _field_offset_to_constant->at_grow(offset);
  if (orig) {
    return *orig;
  } 

  assert(is_loaded() &&
         field->holder()->is_loaded() &&
         klass()->is_subclass_of(field->holder()),
         "invalid access");
  VM_ENTRY_MARK;
  ciConstant result;
  oop obj = get_oop();
  assert(obj != NULL, "bad oop");
  BasicType field_btype = field->type()->basic_type();

  switch(field_btype) {
case T_BYTE:{
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->byte_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_CHAR: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->char_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_SHORT: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->short_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_BOOLEAN: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->bool_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_INT: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->int_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_FLOAT: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->float_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_DOUBLE: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->double_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_LONG: {
    ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, obj->long_field(offset));
    _field_offset_to_constant->at_put_grow(offset,cic);
    return *cic;
  }
  case T_OBJECT:
  case T_ARRAY:
    {
      oop o = obj->obj_field(offset);

      // A field will be "constant" if it is known always to be
      // a non-null reference to an instance of a particular class,
      // or to a particular array.  This can happen even if the instance
      // or array is not perm.  In such a case, an "unloaded" ciArray
      // or ciInstance is created.  The compiler may be able to use
      // information about the object's class (which is exact) or length.

      if (o == NULL) {
        ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, ciNullObject::make());
        _field_offset_to_constant->at_put_grow(offset,cic);
        return *cic;
      } else {
        ciConstant* cic = new (ciEnv::current()->arena()) ciConstant(field_btype, CURRENT_ENV->get_object(o));
        _field_offset_to_constant->at_put_grow(offset,cic);
        return *cic;
      }
    }
  }
  ShouldNotReachHere();
  // to shut up the compiler
  return ciConstant();
}

// ------------------------------------------------------------------
// ciInstance::field_value_by_offset
//
// Constant value of a field at the specified offset.
ciConstant ciInstance::field_value_by_offset(int field_offset) {
  ciInstanceKlass* ik = klass()->as_instance_klass();
  ciField* field = ik->get_field_by_offset(field_offset, false);
  return field_value(field);
}

// ------------------------------------------------------------------
// ciInstance::print_impl
//
// Implementation of the print method.
void ciInstance::print_impl(outputStream*out)const{
out->print(" type=");
klass()->print(out);
}
