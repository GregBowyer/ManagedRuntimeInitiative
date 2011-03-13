/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "java.hpp"
#include "javaClasses.hpp"
#include "reflectionUtils.hpp"
#include "systemDictionary.hpp"

#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"

KlassStream::KlassStream(instanceKlassHandle klass, bool local_only, bool classes_only) {
  _klass = klass;  
  if (classes_only) {
    _interfaces = Universe::the_empty_system_obj_array();
  } else {
    _interfaces = klass->transitive_interfaces();
  }
  _interface_index = _interfaces->length();
  _local_only = local_only;
  _classes_only = classes_only;
}

bool KlassStream::eos() {
  if (index() >= 0) return false;
  if (_local_only) return true;
  if (!_klass->is_interface() && _klass->super() != NULL) {
    // go up superclass chain (not for interfaces)
    _klass = _klass->super();
  } else { 
    if (_interface_index > 0) {
      _klass = klassOop(_interfaces->obj_at(--_interface_index));
    } else {
      return true;
    }
  }
  _index = length();
  next();
  return eos();
}


GrowableArray<FilteredField*> *FilteredFieldsMap::_filtered_fields =
  new (ResourceObj::C_HEAP) GrowableArray<FilteredField*>(3,true);


void FilteredFieldsMap::initialize() {
  int offset;
  offset = java_lang_Throwable::get_backtrace_offset();
  _filtered_fields->append(new FilteredField(SystemDictionary::throwable_klass(), offset));
  // The latest version of vm may be used with old jdk.
  if (JDK_Version::is_gte_jdk16x_version()) {
    // The following class fields do not exist in 
    // previous version of jdk. 
    offset = sun_reflect_ConstantPool::cp_oop_offset();
    _filtered_fields->append(new FilteredField(SystemDictionary::reflect_constant_pool_klass(), offset));
    offset = sun_reflect_UnsafeStaticFieldAccessorImpl::base_offset();
    _filtered_fields->append(new FilteredField(SystemDictionary::reflect_unsafe_static_field_accessor_impl_klass(), offset));
  }
}

int FilteredFieldStream::field_count() {
  int numflds = 0;
  for (;!eos(); next()) {
    numflds++;
  }
  return numflds;
}
