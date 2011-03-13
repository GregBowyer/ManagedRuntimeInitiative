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
#include "ciField.hpp"
#include "ciNullObject.hpp"
#include "fieldDescriptor.hpp"
#include "fieldType.hpp"
#include "interfaceSupport.hpp"
#include "javaClasses.hpp"
#include "linkResolver.hpp"
#include "ostream.hpp"
#include "systemDictionary.hpp"

#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

// ciField
//
// This class represents the result of a field lookup in the VM.
// The lookup may not succeed, in which case the information in
// the ciField will be incomplete.

// ------------------------------------------------------------------
// ciField::ciField
ciField::ciField(ciInstanceKlass* klass, int index): _known_to_link_with(NULL) {
  ASSERT_IN_VM;
Thread*thread=Thread::current();

assert(klass->get_instanceKlass()->is_linked(),"must be linked before using its constant-pool");

  _cp_index = index;
  constantPoolHandle cpool(thread, klass->get_instanceKlass()->constants());

  // Get the field's name, signature, and type.
  symbolHandle name  (thread, cpool->name_ref_at(index));

  int nt_index = cpool->name_and_type_ref_index_at(index);
  int sig_index = cpool->signature_ref_index_at(nt_index);
  symbolHandle signature (thread, cpool->symbol_at(sig_index));

  BasicType field_type = FieldType::basic_type(signature());

  // If the field is a pointer type, get the klass of the
  // field.
  if (field_type == T_OBJECT || field_type == T_ARRAY) {
    bool ignore;
    // This is not really a class reference; the index always refers to the
    // field's type signature, as a symbol.  Linkage checks do not apply.
    _type = ciEnv::current(thread)->get_klass_by_index(klass, sig_index, ignore);
  } else {
    _type = ciType::make(field_type);
  }

  // Get the field's declared holder.
  //
  // Note: we actually create a ciInstanceKlass for this klass,
  // even though we may not need to.
  int holder_index = cpool->klass_ref_index_at(index);
  bool holder_is_accessible;
  ciInstanceKlass* declared_holder =
    ciEnv::current(thread)->get_klass_by_index(klass, holder_index,
                                               holder_is_accessible)
      ->as_instance_klass();

  // The declared holder of this field may not have been loaded.
  // Bail out with partial field information.
  if (!holder_is_accessible) {
    // _cp_index and _type have already been set.
    // The default values for _flags and _constant_value will suffice.
    // We need values for _holder, _offset,  and _is_constant,
    _holder = declared_holder;
    _offset = -1;
    _is_constant = false;
    return;
  }

  instanceKlass* loaded_decl_holder = declared_holder->get_instanceKlass();

  // Perform the field lookup.
  fieldDescriptor field_desc;
  klassOop canonical_holder = 
    loaded_decl_holder->find_field(name(), signature(), &field_desc);
  if (canonical_holder == NULL) {
    // Field lookup failed.  Will be detected by will_link.
    _holder = declared_holder;
    _offset = -1;
    _is_constant = false;
    return;
  }

  assert(canonical_holder == field_desc.field_holder(), "just checking");
  initialize_from(&field_desc);
}

ciField::ciField(fieldDescriptor *fd): _known_to_link_with(NULL) {
  ASSERT_IN_VM;

  _cp_index = -1;

  // Get the field's name, signature, and type.
  ciEnv* env = CURRENT_ENV;
  _name = env->get_object(fd->name())->as_symbol();
  _signature = env->get_object(fd->signature())->as_symbol();

  BasicType field_type = fd->field_type();

  // If the field is a pointer type, get the klass of the
  // field.
  if (field_type == T_OBJECT || field_type == T_ARRAY) {
    _type = NULL;  // must call compute_type on first access
  } else {
    _type = ciType::make(field_type);
  }

  initialize_from(fd);
}

ciField::ciField(FAMPtr old_cif) {
  FAM->mapNewToOldPtr(this, old_cif);

  AccessFlags af;
  af.set_flags_raw(FAM->getInt("((struct ciField*)%p)->_flags->_flags", old_cif));
  ciFlags flags(af);
  _flags = flags;

  FAMPtr old_holder = FAM->getOldPtr("((struct ciField*)%p)->_holder", old_cif);
  _holder = (ciInstanceKlass*)FAM->getNewFromOldPtr(old_holder);

  FAMPtr old_type = FAM->getOldPtr("((struct ciField*)%p)->_type", old_cif);
  _type = (ciType*)FAM->getNewFromOldPtr(old_type);

  _offset = FAM->getInt("((struct ciField*)%p)->_offset", old_cif);
  _is_constant = FAM->getInt("((struct ciField*)%p)->_is_constant", old_cif);

  FAMPtr old_ktlw = FAM->getOldPtr("((struct ciField*)%p)->_known_to_link_with", old_cif);
  _known_to_link_with = (ciInstanceKlass*)FAM->getNewFromOldPtr(old_ktlw);

  FAMPtr old_cv = FAM->getOldPtr("&((struct ciField*)%p)->_constant_value", old_cif);
  ciConstant cic(old_cv);
_constant_value=cic;

  _cp_index = FAM->getInt("((struct ciField*)%p)->_cp_index", old_cif);
}

void ciField::initialize_from(fieldDescriptor* fd) {
  // Get the flags, offset, and canonical holder of the field.
  _flags = ciFlags(fd->access_flags());
  _offset = fd->offset();
  _holder = CURRENT_ENV->get_object(fd->field_holder())->as_instance_klass();
  
  // Check to see if the field is constant.
  if (_holder->is_initialized() &&
      this->is_final() && this->is_static()) {
    // This field just may be constant.  The only cases where it will
    // not be constant are:
    //
    // 1. The field holds a non-perm-space oop.  The field is, strictly
    //    speaking, constant but we cannot embed non-perm-space oops into
    //    generated code.  For the time being we need to consider the
    //    field to be not constant.
    // 2. The field is a *special* static&final field whose value
    //    may change.  The three examples are java.lang.System.in, 
    //    java.lang.System.out, and java.lang.System.err.

    klassOop k = _holder->get_klassOop();
    assert( SystemDictionary::system_klass() != NULL, "Check once per vm");
    if( k == SystemDictionary::system_klass() ) {
      // Check offsets for case 2: System.in, System.out, or System.err 
      if( _offset == java_lang_System::in_offset_in_bytes()  ||
          _offset == java_lang_System::out_offset_in_bytes() ||
          _offset == java_lang_System::err_offset_in_bytes() ) {
        _is_constant = false;
        return;
      }
    }

    _is_constant = true;
    switch(type()->basic_type()) {
    case T_BYTE: 
      _constant_value = ciConstant(type()->basic_type(), k->byte_field(_offset));
      break;
    case T_CHAR: 
      _constant_value = ciConstant(type()->basic_type(), k->char_field(_offset));
      break;
    case T_SHORT: 
      _constant_value = ciConstant(type()->basic_type(), k->short_field(_offset));
      break;
    case T_BOOLEAN:
      _constant_value = ciConstant(type()->basic_type(), k->bool_field(_offset));
      break;
    case T_INT:
      _constant_value = ciConstant(type()->basic_type(), k->int_field(_offset));
      break;
    case T_FLOAT:
      _constant_value = ciConstant(k->float_field(_offset));
      break;
    case T_DOUBLE:
      _constant_value = ciConstant(k->double_field(_offset));
      break;
    case T_LONG:
      _constant_value = ciConstant(k->long_field(_offset));
      break;
    case T_OBJECT:
    case T_ARRAY:
      {
	oop o = k->obj_field(_offset);

	// A field will be "constant" if it is known always to be a non-null
	// reference to an instance of a particular class or array.  This can
	// happen even if the instance or array is not perm.  In such a case,
	// an "unloaded" ciArray or ciInstance is created.  The compiler may
	// be able to use information about the object's class (which is
	// exact) or length.

	if (o == NULL) {
	  _constant_value = ciConstant(type()->basic_type(), ciNullObject::make());
	} else {
	  _constant_value = ciConstant(type()->basic_type(), CURRENT_ENV->get_object(o));
	  assert(_constant_value.as_object() == CURRENT_ENV->get_object(o), "check interning");
	}
      }
    }
  } else {
    _is_constant = false;
  }
}

// ------------------------------------------------------------------
// ciField::compute_type
//
// Lazily compute the type, if it is an instance klass.
ciType* ciField::compute_type() {
  GUARDED_VM_ENTRY(return compute_type_impl();)
}

ciType* ciField::compute_type_impl() {
  ciKlass* type = CURRENT_ENV->get_klass_by_name_impl(_holder, _signature, false);
  if (!type->is_primitive_type() && is_shared()) {
    // We must not cache a pointer to an unshared type, in a shared field.
    bool type_is_also_shared = false;
    if (type->is_type_array_klass()) {
      type_is_also_shared = true;  // int[] etc. are explicitly bootstrapped
    } else if (type->is_instance_klass()) {
      type_is_also_shared = type->as_instance_klass()->is_shared();
    } else {
      // Currently there is no 'shared' query for array types.
      type_is_also_shared = !ciObjectFactory::is_initialized();
    }
    if (!type_is_also_shared)
      return type;              // Bummer.
  }
  _type = type;
  return type;
}


// ------------------------------------------------------------------
// ciField::will_link
//
// Can a specific access to this field be made without causing
// link errors?
bool ciField::will_link(ciInstanceKlass* accessing_klass,
			Bytecodes::Code bc) {
  if (FAM) {
    if (_known_to_link_with == accessing_klass) {
      return true;
    }
    ShouldNotReachHere();
  }
  VM_ENTRY_MARK;
  if (_offset == -1) {
    // at creation we couldn't link to our holder so we need to
    // maintain that stance, otherwise there's no safe way to use this
    // ciField.
    return false;
  }

  if (_known_to_link_with == accessing_klass) {
    return true;
  }

  FieldAccessInfo result;
  constantPoolHandle c_pool(THREAD,
                         accessing_klass->get_instanceKlass()->constants());
  LinkResolver::resolve_field(result, c_pool, _cp_index,
                              Bytecodes::java_code(bc),
			      true, false, KILL_COMPILE_ON_FATAL_(false));
  _known_to_link_with = accessing_klass;
  return true;
}

// ------------------------------------------------------------------
// ciField::print
void ciField::print() {
  tty->print("<ciField ");
  _holder->print_name_on(tty);
  tty->print(".");
_name->print(tty);
  tty->print(" offset=%d type=", _offset);
if(_type!=NULL)_type->print_name(tty);
  else               tty->print("(reference)");
  tty->print(" is_constant=%s", bool_to_str(_is_constant));
  if (_is_constant) {
    tty->print(" constant_value=");
    _constant_value.print();
  }
  tty->print(">");
}


// ------------------------------------------------------------------
// ciField::print_name_on
//
// Print the name of this field
void ciField::print_name_on(outputStream* st) {
  name()->print_symbol_on(st);
}
