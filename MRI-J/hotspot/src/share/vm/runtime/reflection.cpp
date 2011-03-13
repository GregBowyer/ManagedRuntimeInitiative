/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "instanceKlass.hpp"
#include "javaCalls.hpp"
#include "javaClasses.hpp"
#include "linkResolver.hpp"
#include "objArrayKlass.hpp"
#include "objArrayOop.hpp"
#include "oopFactory.hpp"
#include "reflection.hpp"
#include "resourceArea.hpp"
#include "signature.hpp"
#include "systemDictionary.hpp"
#include "verifier.hpp"
#include "vmSymbols.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "hashtable.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#define JAVA_1_5_VERSION                  49

oop Reflection::box(jvalue* value, BasicType type, TRAPS) {
  if (type == T_VOID) {
    return NULL;
  }
  if (type == T_OBJECT || type == T_ARRAY) {
    // regular objects are not boxed
return(*((objectRef*)&value->l)).as_oop();
  }
  oop result = java_lang_boxing_object::create(type, value, CHECK_NULL);
  if (result == NULL) {
    THROW_(vmSymbols::java_lang_IllegalArgumentException(), result);
  }
  return result;
}


BasicType Reflection::unbox_for_primitive(objectRef box, jvalue* value, TRAPS) {
if(box.is_null()){
    THROW_(vmSymbols::java_lang_IllegalArgumentException(), T_ILLEGAL);
  }
return java_lang_boxing_object::get_value(box.as_oop(),value);
}

BasicType Reflection::unbox_for_regular_object(objectRef box, jvalue* value) {
  // Note:  box is really the unboxed oop.  It might even be a Short, etc.!
*((objectRef*)&value->l)=box;
  return T_OBJECT;
}


void Reflection::widen(jvalue* value, BasicType current_type, BasicType wide_type, TRAPS) {
  assert(wide_type != current_type, "widen should not be called with identical types");
  switch (wide_type) {
    case T_BOOLEAN:
    case T_BYTE:
    case T_CHAR:
      break;  // fail
    case T_SHORT:
      switch (current_type) {
        case T_BYTE:
          value->s = (jshort) value->b;
	  return;
      }
      break;  // fail
    case T_INT:
      switch (current_type) {
        case T_BYTE:
          value->i = (jint) value->b;
	  return;
        case T_CHAR:
          value->i = (jint) value->c;
	  return;
        case T_SHORT:
          value->i = (jint) value->s;
	  return;
      }
      break;  // fail
    case T_LONG:
      switch (current_type) {
        case T_BYTE:
          value->j = (jlong) value->b;
          return;
        case T_CHAR:
          value->j = (jlong) value->c;
          return;
        case T_SHORT:
          value->j = (jlong) value->s;
          return;
        case T_INT:
          value->j = (jlong) value->i;
          return;
      }
      break;  // fail
    case T_FLOAT:
      switch (current_type) {
        case T_BYTE:
          value->f = (jfloat) value->b; 
          return;
        case T_CHAR:
          value->f = (jfloat) value->c; 
          return;
        case T_SHORT:
          value->f = (jfloat) value->s; 
          return;
        case T_INT:
          value->f = (jfloat) value->i; 
          return;
        case T_LONG:
          value->f = (jfloat) value->j; 
          return;
      }
      break;  // fail
    case T_DOUBLE:
      switch (current_type) {
        case T_BYTE:
          value->d = (jdouble) value->b; 
          return;
        case T_CHAR:
          value->d = (jdouble) value->c; 
          return;
        case T_SHORT:
          value->d = (jdouble) value->s; 
          return;
        case T_INT:
          value->d = (jdouble) value->i; 
          return;
        case T_FLOAT:
          value->d = (jdouble) value->f; 
          return;
        case T_LONG:
          value->d = (jdouble) value->j;
          return;
      }
      break;  // fail
    default:
      break;  // fail
  }
  THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "argument type mismatch");
}


BasicType Reflection::array_get(jvalue* value, arrayOop a, int index, TRAPS) {
  if (!a->is_within_bounds(index)) {
    THROW_(vmSymbols::java_lang_ArrayIndexOutOfBoundsException(), T_ILLEGAL);
  }
  if (a->is_objArray()) {
*((objectRef*)&value->l)=lvb_ref(objArrayOop(a)->obj_at_addr(index));
    return T_OBJECT;
  } else {
    assert(a->is_typeArray(), "just checking");
    BasicType type = typeArrayKlass::cast(a->klass())->element_type();
    switch (type) {
      case T_BOOLEAN:
        value->z = typeArrayOop(a)->bool_at(index);
        break;
      case T_CHAR:
        value->c = typeArrayOop(a)->char_at(index);
        break;
      case T_FLOAT:
        value->f = typeArrayOop(a)->float_at(index);
        break;
      case T_DOUBLE:
        value->d = typeArrayOop(a)->double_at(index);
        break;
      case T_BYTE:
        value->b = typeArrayOop(a)->byte_at(index);
        break;
      case T_SHORT:
        value->s = typeArrayOop(a)->short_at(index);
        break;
      case T_INT:
        value->i = typeArrayOop(a)->int_at(index);
        break;
      case T_LONG:
        value->j = typeArrayOop(a)->long_at(index);
        break;
      default:
        return T_ILLEGAL;
    }
    return type;
  }
}


void Reflection::array_set(jvalue* value, arrayOop a, int index, BasicType value_type, TRAPS) {
  if (!a->is_within_bounds(index)) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  if (a->is_objArray()) {
    if (value_type == T_OBJECT) {
oop obj=(*(objectRef*)&value->l).as_oop();
      if (obj != NULL) {
        klassOop element_klass = objArrayKlass::cast(a->klass())->element_klass();
        if (!obj->is_a(element_klass)) {
          THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "array element type mismatch");
        }
      }
      objArrayOop(a)->obj_at_put(index, obj);
    }
  } else {
    assert(a->is_typeArray(), "just checking");
    BasicType array_type = typeArrayKlass::cast(a->klass())->element_type();
    if (array_type != value_type) {
      // The widen operation can potentially throw an exception, but cannot block,
      // so typeArrayOop a is safe if the call succeeds.
      widen(value, value_type, array_type, CHECK);
    }
    switch (array_type) {
      case T_BOOLEAN:
        typeArrayOop(a)->bool_at_put(index, value->z);
        break;
      case T_CHAR:
        typeArrayOop(a)->char_at_put(index, value->c);
        break;
      case T_FLOAT:
        typeArrayOop(a)->float_at_put(index, value->f);
        break;
      case T_DOUBLE:
        typeArrayOop(a)->double_at_put(index, value->d);
        break;
      case T_BYTE:
        typeArrayOop(a)->byte_at_put(index, value->b);
        break;
      case T_SHORT:
        typeArrayOop(a)->short_at_put(index, value->s);
        break;
      case T_INT:
        typeArrayOop(a)->int_at_put(index, value->i);
        break;
      case T_LONG:
        typeArrayOop(a)->long_at_put(index, value->j);
        break;
      default:
        THROW(vmSymbols::java_lang_IllegalArgumentException());
    }
  }
}


klassOop Reflection::basic_type_mirror_to_arrayklass(oop basic_type_mirror, TRAPS) {
  assert(java_lang_Class::is_primitive(basic_type_mirror), "just checking");
  BasicType type = java_lang_Class::primitive_type(basic_type_mirror);
  if (type == T_VOID) {
    THROW_0(vmSymbols::java_lang_IllegalArgumentException());
  } else {
    return Universe::typeArrayKlassObj(type);
  }
}


oop Reflection:: basic_type_arrayklass_to_mirror(klassOop basic_type_arrayklass, TRAPS) {
  BasicType type = typeArrayKlass::cast(basic_type_arrayklass)->element_type();
  return Universe::java_mirror(type);
}


arrayOop Reflection::reflect_new_array(oop element_mirror, jint length, TRAPS) {
  if (element_mirror == NULL) {
    THROW_0(vmSymbols::java_lang_NullPointerException());
  }
  if (length < 0) {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
  if (java_lang_Class::is_primitive(element_mirror)) {
    klassOop tak = basic_type_mirror_to_arrayklass(element_mirror, CHECK_NULL);
return typeArrayKlass::cast(tak)->allocate(length,true/*SBA*/,THREAD);
  } else {
    klassOop k = java_lang_Class::as_klassOop(element_mirror);
    if (Klass::cast(k)->oop_is_array() && arrayKlass::cast(k)->dimension() >= MAX_DIM) {
      THROW_0(vmSymbols::java_lang_IllegalArgumentException());
    }
return oopFactory::new_objArray(k,length,true/*SBA*/,THREAD);
  }
}


arrayOop Reflection::reflect_new_multi_array(oop element_mirror, typeArrayOop dim_array, TRAPS) {
  assert(dim_array->is_typeArray(), "just checking");
  assert(typeArrayKlass::cast(dim_array->klass())->element_type() == T_INT, "just checking");

  if (element_mirror == NULL) {
    THROW_0(vmSymbols::java_lang_NullPointerException());
  }

  int len = dim_array->length();
  if (len <= 0 || len > MAX_DIM) {
    THROW_0(vmSymbols::java_lang_IllegalArgumentException());
  }

  jint dimensions[MAX_DIM];   // C array copy of intArrayOop
  for (int i = 0; i < len; i++) {
    int d = dim_array->int_at(i);
    if (d < 0) {
      THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
    }
    dimensions[i] = d;
  }

  klassOop klass;
  int dim = len;
  if (java_lang_Class::is_primitive(element_mirror)) {
    klass = basic_type_mirror_to_arrayklass(element_mirror, CHECK_NULL);
  } else {
    klass = java_lang_Class::as_klassOop(element_mirror);
    if (Klass::cast(klass)->oop_is_array()) {
      int k_dim = arrayKlass::cast(klass)->dimension();
      if (k_dim + len > MAX_DIM) {
        THROW_0(vmSymbols::java_lang_IllegalArgumentException());
      }
      dim += k_dim;
    }
  }
klassRef kref=Klass::cast(klass)->array_klass(klass,dim,CHECK_NULL);
klass=kref.as_klassOop();
oop obj=arrayKlass::cast(klass)->multi_allocate(len,dimensions,true/*SBA*/,THREAD);
  assert(obj->is_array(), "just checking");
  return arrayOop(obj);
}


oop Reflection::array_component_type(oop mirror, TRAPS) {
  if (java_lang_Class::is_primitive(mirror)) {
    return NULL;
  }

  klassOop klass = java_lang_Class::as_klassOop(mirror);
  if (!Klass::cast(klass)->oop_is_array()) {
    return NULL;
  }

  oop result = arrayKlass::cast(klass)->component_mirror();
#ifdef ASSERT
  oop result2 = NULL;
  if (arrayKlass::cast(klass)->dimension() == 1) {
    if (Klass::cast(klass)->oop_is_typeArray()) {
      result2 = basic_type_arrayklass_to_mirror(klass, CHECK_NULL);
    } else {
      result2 = Klass::cast(objArrayKlass::cast(klass)->element_klass())->java_mirror();
    }
  } else {
    klassOop lower_dim = arrayKlass::cast(klass)->lower_dimension();
    assert(Klass::cast(lower_dim)->oop_is_array(), "just checking");
    result2 = Klass::cast(lower_dim)->java_mirror();
  }
  assert(result == result2, "results must be consistent");
#endif //ASSERT
  return result;
}


bool Reflection::reflect_check_access(klassOop field_class, AccessFlags acc, klassOop target_class, bool is_method_invoke, TRAPS) {
  // field_class  : declaring class
  // acc          : declared field access
  // target_class : for protected

  // Check if field or method is accessible to client.  Throw an
  // IllegalAccessException and return false if not.

  // The "client" is the class associated with the nearest real frame
  // getCallerClass already skips Method.invoke frames, so pass 0 in 
  // that case (same as classic).
  ResourceMark rm(THREAD);
  assert(THREAD->is_Java_thread(), "sanity check");
  klassOop client_class = ((JavaThread *)THREAD)->security_get_caller_class(is_method_invoke ? 0 : 1);
  
  if (client_class != field_class) {
    if (!verify_class_access(client_class, field_class, false)
        || !verify_field_access(client_class, 
                                field_class, 
                                field_class, 
                                acc, 
                                false)) {     
      THROW_(vmSymbols::java_lang_IllegalAccessException(), false);
    }
  }

  // Additional test for protected members: JLS 6.6.2

  if (acc.is_protected()) {
    if (target_class != client_class) {
      if (!is_same_class_package(client_class, field_class)) {
        if (!Klass::cast(target_class)->is_subclass_of(client_class)) {          
          THROW_(vmSymbols::java_lang_IllegalAccessException(), false);
        }
      }
    }
  }

  // Passed all tests
  return true;
}


bool Reflection::verify_class_access(klassOop current_class, klassOop new_class, bool classloader_only) {    
  // Verify that current_class can access new_class.  If the classloader_only
  // flag is set, we automatically allow any accesses in which current_class
  // doesn't have a classloader.
  if ((current_class == NULL) ||
      (current_class == new_class) ||
      (instanceKlass::cast(new_class)->is_public()) ||
      is_same_class_package(current_class, new_class)) {
    return true;
  }
  // New (1.4) reflection implementation. Allow all accesses from
  // sun/reflect/MagicAccessorImpl subclasses to succeed trivially.
if(Klass::cast(current_class)->is_subclass_of(SystemDictionary::reflect_magic_klass())){
    return true;
  }

  return can_relax_access_check_for(current_class, new_class, classloader_only);
}    

bool Reflection::can_relax_access_check_for(
    klassOop accessor, klassOop accessee, bool classloader_only) {
  instanceKlass* accessor_ik = instanceKlass::cast(accessor);
  instanceKlass* accessee_ik  = instanceKlass::cast(accessee);
if(RelaxAccessControlCheck){
    return classloader_only &&
      Verifier::relax_verify_for(accessor_ik->class_loader()) &&
      accessor_ik->protection_domain() == accessee_ik->protection_domain() &&
      accessor_ik->class_loader() == accessee_ik->class_loader();
  } else {
    return false;
  }
}

bool Reflection::verify_field_access(klassOop current_class, 
                                     klassOop resolved_class,
                                     klassOop field_class, 
                                     AccessFlags access, 
                                     bool classloader_only, 
                                     bool protected_restriction) {  
  // Verify that current_class can access a field of field_class, where that  
  // field's access bits are "access".  We assume that we've already verified 
  // that current_class can access field_class.
  //
  // If the classloader_only flag is set, we automatically allow any accesses
  // in which current_class doesn't have a classloader.
  //
  // "resolved_class" is the runtime type of "field_class". Sometimes we don't
  // need this distinction (e.g. if all we have is the runtime type, or during 
  // class file parsing when we only care about the static type); in that case
  // callers should ensure that resolved_class == field_class.
  //
  if ((current_class == NULL) ||
      (current_class == field_class) ||
      access.is_public()) {
    return true;
  }

  if (access.is_protected()) {
    if (!protected_restriction) {
      // See if current_class is a subclass of field_class
      if (Klass::cast(current_class)->is_subclass_of(field_class)) {
        if (current_class == resolved_class ||
            field_class == resolved_class ||
            Klass::cast(current_class)->is_subclass_of(resolved_class) ||
            Klass::cast(resolved_class)->is_subclass_of(current_class)) {
          return true;
        }
      }
    }
  }

  if (!access.is_private() && is_same_class_package(current_class, field_class)) {
    return true;
  }

  // New (1.4) reflection implementation. Allow all accesses from
  // sun/reflect/MagicAccessorImpl subclasses to succeed trivially.
if(Klass::cast(current_class)->is_subclass_of(SystemDictionary::reflect_magic_klass())){
    return true;
  }

  return can_relax_access_check_for(
    current_class, field_class, classloader_only);
}


bool Reflection::is_same_class_package(klassOop class1, klassOop class2) {
  return instanceKlass::cast(class1)->is_same_class_package(class2);
}
 

// Checks that the 'outer' klass has declared 'inner' as being an inner klass. If not,
// throw an incompatible class change exception
void Reflection::check_for_inner_class(instanceKlassHandle outer, instanceKlassHandle inner, TRAPS) {
  const int inner_class_info_index = 0;
  const int outer_class_info_index = 1;

  typeArrayHandle    icls (THREAD, outer->inner_classes());
  constantPoolHandle cp   (THREAD, outer->constants());
  for(int i = 0; i < icls->length(); i += 4) {
     int ioff = icls->ushort_at(i + inner_class_info_index);
     int ooff = icls->ushort_at(i + outer_class_info_index);         

     if (ioff != 0 && ooff != 0) {
        klassOop o = cp->klass_at(ooff, CHECK);
        if (o == outer()) {
          klassOop i = cp->klass_at(ioff, CHECK);
          if (i == inner()) {
            return;
          }
        }
     }
  }

  // 'inner' not declared as an inner klass in outer
  ResourceMark rm(THREAD);
  Exceptions::fthrow(
    THREAD_AND_LOCATION,
    vmSymbolHandles::java_lang_IncompatibleClassChangeError(), 
    "%s and %s disagree on InnerClasses attribute", 
    outer->external_name(), 
    inner->external_name()
  );      
}

// Utility method converting a single SignatureStream element into java.lang.Class instance

oop get_mirror_from_signature(methodHandle method, SignatureStream* ss, TRAPS) {
  switch (ss->type()) {
    default:
      assert(ss->type() != T_VOID || ss->at_return_type(), "T_VOID should only appear as return type");
      return java_lang_Class::primitive_mirror(ss->type());
    case T_OBJECT:
    case T_ARRAY:
      symbolOop name        = ss->as_symbol(CHECK_NULL);
      oop loader            = instanceKlass::cast(method->method_holder())->class_loader();
      oop protection_domain = instanceKlass::cast(method->method_holder())->protection_domain();
      klassOop k = SystemDictionary::resolve_or_fail(
                                       symbolHandle(THREAD, name),
                                       Handle(THREAD, loader), 
                                       Handle(THREAD, protection_domain),
                                       true, CHECK_NULL);
      return k->klass_part()->java_mirror();
  };
}


objArrayHandle Reflection::get_parameter_types(methodHandle method, int parameter_count, oop* return_type, intptr_t sba_hint, TRAPS) {
  // Allocate array holding parameter types (java.lang.Class instances)
objArrayOop m=oopFactory::new_objArray(SystemDictionary::class_klass(),parameter_count,sba_hint,CHECK_(objArrayHandle()));
  objArrayHandle mirrors (THREAD, m);
  int index = 0;
  // Collect parameter types
  symbolHandle signature (THREAD, method->signature());
  SignatureStream ss(signature);
  while (!ss.at_return_type()) {
    oop mirror = get_mirror_from_signature(method, &ss, CHECK_(objArrayHandle()));
    mirrors->obj_at_put(index++, mirror);
    ss.next();
  }
  assert(index == parameter_count, "invalid parameter count");
  if (return_type != NULL) {
    // Collect return type as well
    assert(ss.at_return_type(), "return type should be present");
    *return_type = get_mirror_from_signature(method, &ss, CHECK_(objArrayHandle()));
  }
  return mirrors;
}

objArrayHandle Reflection::get_exception_types(methodHandle method, intptr_t sba_hint, TRAPS) {
  return method->resolved_checked_exceptions(sba_hint, CHECK_(objArrayHandle()));
}


Handle Reflection::new_type(symbolHandle signature, KlassHandle k, TRAPS) {
  // Basic types
  BasicType type = vmSymbols::signature_type(signature());
  if (type != T_OBJECT) {
    return Handle(THREAD, Universe::java_mirror(type));
  }

  oop loader = instanceKlass::cast(k())->class_loader();
  oop protection_domain = Klass::cast(k())->protection_domain();
  klassOop result = SystemDictionary::resolve_or_fail(signature,
                                    Handle(THREAD, loader),
                                    Handle(THREAD, protection_domain),
                                    true, CHECK_(Handle()));

  oop nt = Klass::cast(result)->java_mirror();
  return Handle(THREAD, nt);
}


oop Reflection::new_method(methodHandle method, bool for_constant_pool_access, intptr_t sba_hint, TRAPS) {
  // In jdk1.2.x, getMethods on an interface erroneously includes <clinit>, thus the complicated assert.
  // Also allow sun.reflect.ConstantPool to refer to <clinit> methods as java.lang.reflect.Methods.
  assert(!method()->is_initializer() ||
(for_constant_pool_access&&method()->is_static()),
"should call new_constructor instead");
  instanceKlassHandle holder (THREAD, method->method_holder());
  int slot = method->method_idnum();

  symbolHandle signature (THREAD, method->signature());
  int parameter_count = ArgumentCount(signature).size();
  oop return_type_oop = NULL;
objArrayHandle parameter_types=get_parameter_types(method,parameter_count,&return_type_oop,sba_hint,CHECK_NULL);
  if (parameter_types.is_null() || return_type_oop == NULL) return NULL;
  
  Handle return_type(THREAD, return_type_oop);

  objArrayHandle exception_types = get_exception_types(method, sba_hint, CHECK_NULL);

  if (exception_types.is_null()) return NULL;
  
  symbolHandle method_name(THREAD, method->name());
  // intern_name is only true with UseNewReflection
  oop name_oop = StringTable::intern(method_name(), CHECK_NULL);
Handle name=Handle(THREAD,name_oop);
  if (name.is_null()) return NULL;

  int modifiers = method->access_flags().as_int() & JVM_RECOGNIZED_METHOD_MODIFIERS;

  Handle mh = java_lang_reflect_Method::create(sba_hint, CHECK_NULL);

  java_lang_reflect_Method::set_clazz(mh(), holder->java_mirror());
  java_lang_reflect_Method::set_slot(mh(), slot);
  java_lang_reflect_Method::set_name(mh(), name());
  java_lang_reflect_Method::set_return_type(mh(), return_type());
  java_lang_reflect_Method::set_parameter_types(mh(), parameter_types());
  java_lang_reflect_Method::set_exception_types(mh(), exception_types());
  java_lang_reflect_Method::set_modifiers(mh(), modifiers);
  java_lang_reflect_Method::set_override(mh(), false);
  if (java_lang_reflect_Method::has_signature_field() &&
      method->generic_signature() != NULL) {
    symbolHandle gs(THREAD, method->generic_signature());
Handle sig=java_lang_String::create_from_symbol(gs,sba_hint,CHECK_NULL);
    java_lang_reflect_Method::set_signature(mh(), sig());
  }
  if (java_lang_reflect_Method::has_annotations_field()) {
    java_lang_reflect_Method::set_annotations(mh(), method->annotations());
  }
  if (java_lang_reflect_Method::has_parameter_annotations_field()) {
    java_lang_reflect_Method::set_parameter_annotations(mh(), method->parameter_annotations());
  }
  if (java_lang_reflect_Method::has_annotation_default_field()) {
    java_lang_reflect_Method::set_annotation_default(mh(), method->annotation_default());
  }
  return mh();
}


oop Reflection::new_constructor(methodHandle method, intptr_t sba_hint, TRAPS) {
  assert(method()->is_initializer(), "should call new_method instead");

  instanceKlassHandle  holder (THREAD, method->method_holder());
  int slot = method->method_idnum();

  symbolHandle signature (THREAD, method->signature());
  int parameter_count = ArgumentCount(signature).size();
objArrayHandle parameter_types=get_parameter_types(method,parameter_count,NULL,sba_hint,CHECK_NULL);
  if (parameter_types.is_null()) return NULL;
  
  objArrayHandle exception_types = get_exception_types(method, sba_hint, CHECK_NULL);
  if (exception_types.is_null()) return NULL;
  
  int modifiers = method->access_flags().as_int() & JVM_RECOGNIZED_METHOD_MODIFIERS;

  Handle ch = java_lang_reflect_Constructor::create(sba_hint, CHECK_NULL);
  
  java_lang_reflect_Constructor::set_clazz(ch(), holder->java_mirror());
  java_lang_reflect_Constructor::set_slot(ch(), slot);
  java_lang_reflect_Constructor::set_parameter_types(ch(), parameter_types());
  java_lang_reflect_Constructor::set_exception_types(ch(), exception_types());
  java_lang_reflect_Constructor::set_modifiers(ch(), modifiers);
  java_lang_reflect_Constructor::set_override(ch(), false);
  if (java_lang_reflect_Constructor::has_signature_field() &&
      method->generic_signature() != NULL) {
    symbolHandle gs(THREAD, method->generic_signature());
Handle sig=java_lang_String::create_from_symbol(gs,sba_hint,CHECK_NULL);
    java_lang_reflect_Constructor::set_signature(ch(), sig());
  }
  if (java_lang_reflect_Constructor::has_annotations_field()) {
    java_lang_reflect_Constructor::set_annotations(ch(), method->annotations());
  }
  if (java_lang_reflect_Constructor::has_parameter_annotations_field()) {
    java_lang_reflect_Constructor::set_parameter_annotations(ch(), method->parameter_annotations());
  }
  return ch();
}


oop Reflection::new_field(fieldDescriptor* fd, intptr_t sba_hint, TRAPS) {
  symbolHandle field_name(THREAD, fd->name());
  oop name_oop = StringTable::intern(field_name(), CHECK_NULL);
Handle name=Handle(THREAD,name_oop);
  symbolHandle signature (THREAD, fd->signature());
  KlassHandle  holder    (THREAD, fd->field_holder());
  Handle type = new_type(signature, holder, CHECK_NULL);
  Handle rh  = java_lang_reflect_Field::create(CHECK_NULL);  

  java_lang_reflect_Field::set_clazz(rh(), Klass::cast(fd->field_holder())->java_mirror());
  java_lang_reflect_Field::set_slot(rh(), fd->index());
  java_lang_reflect_Field::set_name(rh(), name());
  java_lang_reflect_Field::set_type(rh(), type());
  // Note the ACC_ANNOTATION bit, which is a per-class access flag, is never set here.
  java_lang_reflect_Field::set_modifiers(rh(), fd->access_flags().as_int() & JVM_RECOGNIZED_FIELD_MODIFIERS);
  java_lang_reflect_Field::set_override(rh(), false);
  if (java_lang_reflect_Field::has_signature_field() &&
      fd->generic_signature() != NULL) {
    symbolHandle gs(THREAD, fd->generic_signature());
Handle sig=java_lang_String::create_from_symbol(gs,sba_hint,CHECK_NULL);
    java_lang_reflect_Field::set_signature(rh(), sig());
  }
  if (java_lang_reflect_Field::has_annotations_field()) {
    java_lang_reflect_Field::set_annotations(rh(), fd->annotations());
  }
  return rh();
}


methodHandle Reflection::resolve_interface_call(instanceKlassHandle klass, methodHandle method, 
                                                KlassHandle recv_klass, Handle receiver, TRAPS) {
  assert(!method.is_null() , "method should not be null");

  CallInfo info;
  symbolHandle signature (THREAD, method->signature());
  symbolHandle name      (THREAD, method->name());
  LinkResolver::resolve_interface_call(info, receiver, recv_klass, klass, 
                                       name, signature,
                                       KlassHandle(), false, true, 
                                       CHECK_(methodHandle()));
  return info.selected_method();
}


BasicType Reflection::basic_type_mirror_to_basic_type(oop basic_type_mirror, TRAPS) {
  assert(java_lang_Class::is_primitive(basic_type_mirror), "just checking");
  return java_lang_Class::primitive_type(basic_type_mirror);
}

oop Reflection::invoke(instanceKlassHandle klass, methodHandle reflected_method, 
		       Handle receiver, bool override, objArrayHandle ptypes, 
		       BasicType rtype, objArrayHandle args, bool is_method_invoke, TRAPS) {
  ResourceMark rm(THREAD);
  
  methodHandle method;      // actual method to invoke
  KlassHandle target_klass; // target klass, receiver's klass for non-static
  
  // Ensure klass is initialized
  klass->initialize(CHECK_NULL);

  bool is_static = reflected_method->is_static();
  if (is_static) {
    // ignore receiver argument
    method = reflected_method; 
    target_klass = klass;
  } else {
    // check for null receiver
    if (receiver.is_null()) { 
      THROW_0(vmSymbols::java_lang_NullPointerException());
    }
    // Check class of receiver against class declaring method
    if (!receiver->is_a(klass())) { 
      THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "object is not an instance of declaring class");
    }
    // target klass is receiver's klass
    target_klass = KlassHandle(THREAD, receiver->klass()); 
    // no need to resolve if method is private or <init> 
    if (reflected_method->is_private() || reflected_method->name() == vmSymbols::object_initializer_name()) {
      method = reflected_method;
    } else {
      // resolve based on the receiver
      if (instanceKlass::cast(reflected_method->method_holder())->is_interface()) {
        // resolve interface call
        if (ReflectionWrapResolutionErrors) {
          // new default: 6531596
          // Match resolution errors with those thrown due to reflection inlining
          // Linktime resolution & IllegalAccessCheck already done by Class.getMethod()
	  method = resolve_interface_call(klass, reflected_method, target_klass, receiver, THREAD); 
          if (HAS_PENDING_EXCEPTION) {
          // Method resolution threw an exception; wrap it in an InvocationTargetException
            oop resolution_exception = PENDING_EXCEPTION;
            CLEAR_PENDING_EXCEPTION;
            JavaCallArguments args(Handle(THREAD, resolution_exception));
            THROW_ARG_0(vmSymbolHandles::java_lang_reflect_InvocationTargetException(),
                vmSymbolHandles::throwable_void_signature(),
                &args);
          }
        } else {
	  method = resolve_interface_call(klass, reflected_method, target_klass, receiver, CHECK_(NULL));
        }
      }  else {
        // if the method can be overridden, we resolve using the vtable index.
        int index  = reflected_method->vtable_index();
        method = reflected_method;
        if (index != methodOopDesc::nonvirtual_vtable_index) {
          // target_klass might be an arrayKlassOop but all vtables start at
          // the same place. The cast is to avoid virtual call and assertion.
          instanceKlass* inst = (instanceKlass*)target_klass()->klass_part();
          method = methodHandle(THREAD, inst->method_at_vtable(index));
        }
        if (!method.is_null()) {
          // Check for abstract methods as well
          if (method->is_abstract()) {
            // new default: 6531596
            if (ReflectionWrapResolutionErrors) {
              ResourceMark rm(THREAD);
              Handle h_origexception = Exceptions::new_exception(THREAD,
                     vmSymbols::java_lang_AbstractMethodError(),
                     methodOopDesc::name_and_sig_as_C_string(Klass::cast(target_klass()),
                     method->name(),
                     method->signature()));
              JavaCallArguments args(h_origexception);
              THROW_ARG_0(vmSymbolHandles::java_lang_reflect_InvocationTargetException(),
                vmSymbolHandles::throwable_void_signature(),
                &args);
            } else {
              ResourceMark rm(THREAD);
              THROW_MSG_0(vmSymbols::java_lang_AbstractMethodError(),
                        methodOopDesc::name_and_sig_as_C_string(Klass::cast(target_klass()),
                                                                method->name(),
                                                                method->signature()));
            }
          }
        }
      }
    }
  }

  // I believe this is a ShouldNotGetHere case which requires
  // an internal vtable bug. If you ever get this please let Karen know.
  if (method.is_null()) {
    ResourceMark rm(THREAD);
    THROW_MSG_0(vmSymbols::java_lang_NoSuchMethodError(),
                methodOopDesc::name_and_sig_as_C_string(Klass::cast(klass()),
                                                        reflected_method->name(),
                                                        reflected_method->signature()));
  }

#if 0 // pre UseNewReflection code
  // Access checking (unless overridden by Method)
  if (!override) {
    if (!(klass->is_public() && reflected_method->is_public())) {
      bool access = Reflection::reflect_check_access(klass(), reflected_method->access_flags(), target_klass(), is_method_invoke, CHECK_NULL);
      if (!access) {
	return NULL; // exception
      }
    }
  }
#endif

  assert(ptypes->is_objArray(), "just checking");
  int args_len = args.is_null() ? 0 : args->length();
  // Check number of arguments
  if (ptypes->length() != args_len) {
    THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "wrong number of arguments");
  }
  
  // Create object to contain parameters for the JavaCall
  JavaCallArguments java_args(method->size_of_parameters());

  if (!is_static) {
    java_args.push_oop(receiver);
  }

  for (int i = 0; i < args_len; i++) {
    oop type_mirror = ptypes->obj_at(i);
objectRef arg=lvb_ref(args->obj_at_addr(i));
    oop arg_oop = arg.as_oop();
    if (java_lang_Class::is_primitive(type_mirror)) {
      jvalue value;
      BasicType ptype = basic_type_mirror_to_basic_type(type_mirror, CHECK_NULL);
      BasicType atype = unbox_for_primitive(arg, &value, CHECK_NULL);
      if (ptype != atype) {
        widen(&value, atype, ptype, CHECK_NULL);
      }
      switch (ptype) {
        case T_BOOLEAN:     java_args.push_int(value.z);    break;
        case T_CHAR:        java_args.push_int(value.c);    break;          
        case T_BYTE:        java_args.push_int(value.b);    break;          
        case T_SHORT:       java_args.push_int(value.s);    break;          
        case T_INT:         java_args.push_int(value.i);    break;                  
        case T_LONG:        java_args.push_long(value.j);   break;
        case T_FLOAT:       java_args.push_float(value.f);  break;          
        case T_DOUBLE:      java_args.push_double(value.d); break;
        default:
          THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "argument type mismatch");
      }
    } else {
if(arg_oop!=NULL){
        klassOop k = java_lang_Class::as_klassOop(type_mirror);
if(!arg_oop->is_a(k)){
          THROW_MSG_0(vmSymbols::java_lang_IllegalArgumentException(), "argument type mismatch");
        }
      }
Handle arg_handle(THREAD,arg_oop);//Create handle for argument
      java_args.push_oop(arg_handle); // Push handle      
    }
  }

  assert(java_args.size_of_parameters() == method->size_of_parameters(), "just checking");

  // All oops (including receiver) are passed in as Handles.  A potential oop
  // is returned as an oop (i.e., NOT as a handle)
  JavaValue result(rtype);
  JavaCalls::call(&result, method, &java_args, THREAD);

  if (HAS_PENDING_EXCEPTION) {
    // Method threw an exception; wrap it in an InvocationTargetException
    oop target_exception = PENDING_EXCEPTION;
    CLEAR_PENDING_EXCEPTION;
    JavaCallArguments args(Handle(THREAD, target_exception));
    THROW_ARG_0(vmSymbolHandles::java_lang_reflect_InvocationTargetException(),
                vmSymbolHandles::throwable_void_signature(),
                &args);
  } else {
    if (rtype == T_BOOLEAN || rtype == T_BYTE || rtype == T_CHAR || rtype == T_SHORT)
      narrow((jvalue*) result.get_value_addr(), rtype, CHECK_NULL);
    return box((jvalue*) result.get_value_addr(), rtype, CHECK_NULL);
  }
  return NULL;
}


void Reflection::narrow(jvalue* value, BasicType narrow_type, TRAPS) {
  switch (narrow_type) {
    case T_BOOLEAN:
     value->z = (jboolean) value->i;
     return;
    case T_BYTE:
     value->b = (jbyte) value->i;
     return;
    case T_CHAR:
     value->c = (jchar) value->i;
     return;
    case T_SHORT:
     value->s = (jshort) value->i;
     return;
    default:
      break; // fail
   }
  THROW_MSG(vmSymbols::java_lang_IllegalArgumentException(), "argument type mismatch");
}


// This would be nicer if, say, java.lang.reflect.Method was a subclass 
// of java.lang.reflect.Constructor

oop Reflection::invoke_method(oop method_mirror, Handle receiver, objArrayHandle args, TRAPS) {
  oop mirror             = java_lang_reflect_Method::clazz(method_mirror);
  int slot               = java_lang_reflect_Method::slot(method_mirror);
  bool override          = java_lang_reflect_Method::override(method_mirror) != 0;
  objArrayHandle ptypes(THREAD, objArrayOop(java_lang_reflect_Method::parameter_types(method_mirror)));

  oop return_type_mirror = java_lang_reflect_Method::return_type(method_mirror);
  BasicType rtype;
  if (java_lang_Class::is_primitive(return_type_mirror)) { 
    rtype = basic_type_mirror_to_basic_type(return_type_mirror, CHECK_NULL);
  } else {
    rtype = T_OBJECT;
  }

  instanceKlassHandle klass(THREAD, java_lang_Class::as_klassOop(mirror));
  if (!klass->methods()->is_within_bounds(slot)) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(), "invoke");
  }
  methodHandle method(THREAD, methodOop(klass->methods()->obj_at(slot)));

  return invoke(klass, method, receiver, override, ptypes, rtype, args, true, THREAD);
}


oop Reflection::invoke_constructor(oop constructor_mirror, objArrayHandle args, intptr_t sba_hint, TRAPS) {
  oop mirror             = java_lang_reflect_Constructor::clazz(constructor_mirror);
  int slot               = java_lang_reflect_Constructor::slot(constructor_mirror);
  bool override          = java_lang_reflect_Constructor::override(constructor_mirror) != 0;
  objArrayHandle ptypes(THREAD, objArrayOop(java_lang_reflect_Constructor::parameter_types(constructor_mirror)));

  instanceKlassHandle klass(THREAD, java_lang_Class::as_klassOop(mirror));  
  if (!klass->methods()->is_within_bounds(slot)) {
    THROW_MSG_0(vmSymbols::java_lang_InternalError(), "invoke");
  }
  methodHandle method(THREAD, methodOop(klass->methods()->obj_at(slot)));
  assert(method->name() == vmSymbols::object_initializer_name(), "invalid constructor");

  // Make sure klass gets initialize
  klass->initialize(CHECK_NULL);      

  // Create new instance (the receiver)
  klass->check_valid_for_instantiation(false, CHECK_NULL);
Handle receiver=klass->allocate_instance_handle(sba_hint,CHECK_NULL);

  // Ignore result from call and return receiver
  invoke(klass, method, receiver, override, ptypes, T_VOID, args, false, CHECK_NULL);
  return receiver();
}

