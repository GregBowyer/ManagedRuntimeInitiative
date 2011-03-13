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
#include "constMethodKlass.hpp"
#include "constantPoolKlass.hpp"
#include "cpCacheKlass.hpp"
#include "instanceKlass.hpp"
#include "instanceKlassKlass.hpp"
#include "methodKlass.hpp"
#include "methodCodeKlass.hpp"
#include "objArrayOop.hpp"
#include "oopFactory.hpp"
#include "typeArrayOop.hpp"
#include "utf8.hpp"

#include "allocation.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"

typeArrayOop oopFactory::new_charArray(const char* utf8_str, intptr_t sba_hint, TRAPS) {
  int length = utf8_str == NULL ? 0 : UTF8::unicode_length(utf8_str);
typeArrayOop result=new_charArray(length,sba_hint,CHECK_NULL);
  if (length > 0) {
    UTF8::convert_to_unicode(utf8_str, result->char_at_addr(0), length);
  }
  return result;
}

typeArrayOop oopFactory::new_permanent_charArray(int length, TRAPS) {
  return typeArrayKlass::cast(Universe::charArrayKlassObj())->allocate_permanent(length, THREAD);
}

typeArrayOop oopFactory::new_permanent_byteArray(int length, TRAPS) {
  return typeArrayKlass::cast(Universe::byteArrayKlassObj())->allocate_permanent(length, THREAD);
}


typeArrayOop oopFactory::new_permanent_shortArray(int length, TRAPS) {
  return typeArrayKlass::cast(Universe::shortArrayKlassObj())->allocate_permanent(length, THREAD);
}


typeArrayOop oopFactory::new_permanent_intArray(int length, TRAPS) {
  return typeArrayKlass::cast(Universe::intArrayKlassObj())->allocate_permanent(length, THREAD);
}


typeArrayOop oopFactory::new_typeArray(BasicType type, int length, intptr_t sba_hint, TRAPS) {
  klassOop type_asKlassOop = Universe::typeArrayKlassObj(type);
  typeArrayKlass* type_asArrayKlass = typeArrayKlass::cast(type_asKlassOop);
typeArrayOop result=type_asArrayKlass->allocate(length,sba_hint,THREAD);
  return result;
}


objArrayOop oopFactory::new_objArray(klassOop klass, int length, intptr_t sba_hint, TRAPS) {
  assert(klass->is_klass(), "must be instance class");
  if (klass->klass_part()->oop_is_array()) {
return((arrayKlass*)klass->klass_part())->allocate_arrayArray(1,length,sba_hint,THREAD);
  } else {
    assert (klass->klass_part()->oop_is_instance(), "new object array with klass not an instanceKlass");
return((instanceKlass*)klass->klass_part())->allocate_objArray(1,length,sba_hint,THREAD);
  }
}

objArrayOop oopFactory::new_system_objArray(int length, TRAPS) {
  int size = objArrayOopDesc::object_size(length);
  KlassHandle klass (THREAD, Universe::systemObjArrayKlassObj());
  objArrayOop o = (objArrayOop)
    Universe::heap()->permanent_array_allocate(klass, size, length, CHECK_NULL);
  // initialization not needed, allocated cleared
  return o;
}


constantPoolOop oopFactory::new_constantPool(int length, TRAPS) {
  constantPoolKlass* ck = constantPoolKlass::cast(Universe::constantPoolKlassObj());
  return ck->allocate(length, CHECK_NULL);
}


constantPoolCacheOop oopFactory::new_constantPoolCache(int length, TRAPS) {
  constantPoolCacheKlass* ck = constantPoolCacheKlass::cast(Universe::constantPoolCacheKlassObj());
  return ck->allocate(length, CHECK_NULL);
}


klassOop oopFactory::new_instanceKlass(int vtable_len, int itable_len, int static_field_size, 
                                       int nonstatic_oop_map_size, ReferenceType rt, TRAPS) {
  instanceKlassKlass* ikk = instanceKlassKlass::cast(Universe::instanceKlassKlassObj());
  return ikk->allocate_instance_klass(vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt, CHECK_NULL);
}


constMethodOop oopFactory::new_constMethod(int byte_code_size,
                                           int compressed_line_number_size,
                                           int localvariable_table_length,
                                           int checked_exceptions_length,
                                           TRAPS) {
  klassOop cmkObj = Universe::constMethodKlassObj();
  constMethodKlass* cmk = constMethodKlass::cast(cmkObj);
  return cmk->allocate(byte_code_size, compressed_line_number_size,
                       localvariable_table_length, checked_exceptions_length,
                       CHECK_NULL);
}


methodOop oopFactory::new_method(int byte_code_size, AccessFlags access_flags,
                                 int compressed_line_number_size,
                                 int localvariable_table_length,
                                 int checked_exceptions_length, TRAPS) {
  methodKlass* mk = methodKlass::cast(Universe::methodKlassObj());
  assert(!access_flags.is_native() || byte_code_size == 0,
         "native methods should not contain byte codes");
  constMethodOop cm = new_constMethod(byte_code_size,
                                      compressed_line_number_size,
                                      localvariable_table_length,
                                      checked_exceptions_length, CHECK_NULL);
  constMethodHandle rw(THREAD, cm);
  return mk->allocate(rw, access_flags, CHECK_NULL);
}


methodCodeOop oopFactory::new_methodCode(const CodeBlob *blob, const DebugMap *dbginfo, const PC2PCMap *pc2pcinfo, const CodeProfile *profile, objArrayHandle dep_klasses, objArrayHandle dep_methods, int compile_id, int entry_bci, bool has_unsafe, objArrayHandle srefs, TRAPS) {
  methodCodeKlass* nmk = methodCodeKlass::cast(Universe::methodCodeKlassObj());
  return nmk->allocate(blob, dbginfo, pc2pcinfo, profile, dep_klasses, dep_methods, compile_id, entry_bci, has_unsafe, srefs, CHECK_0);
}

