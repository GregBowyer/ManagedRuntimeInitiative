// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "allocation.hpp"
#include "vmTags.hpp"

#define NAME_DO(name)\
  XSTR(name),

const char* vmTags::_tag_names[max_tag+1] = {
"no_tag",
  "ProxyIO", // a thread unknown to HotSpot which in practice are proxy, fdc, etc. threads
"vmops_tags_start",
  VMOPS_TAGS_DO(NAME_DO)
"vmops_tags_end",
"vm_tags_start",
  VM_TAGS_DO(NAME_DO)
"vm_tags_end",
"jni_tags_start",
  JNI_TAGS_DO(NAME_DO)
"jni_tags_end",
"jvm_tags_start",
  JVM_TAGS_DO(NAME_DO)
"jvm_tags_end",
"unsafe_tags_start",
  UNSAFE_TAGS_DO(NAME_DO)
"unsafe_tags_end",
"perf_tags_start",
  PERF_TAGS_DO(NAME_DO)
"perf_tags_end",
"irt_tags_start",
  IRT_TAGS_DO(NAME_DO)
"irt_tags_end",
"jrt_tags_start",
  JRT_TAGS_DO(NAME_DO)
"jrt_tags_end",
"max_tag"
};


const char* vmTags::name_for(int tag) {
  return is_native_call(tag) ? "native call" : _tag_names[tag];
}

const char* vmTicks::_tick_names[max_tick+1] = {
"no_tick",
  VM_TICKS_DO(NAME_DO)
"max_tick"
};

const char* vmTicks::name_for(int tick) {
  assert0((tick >= no_tick) && (tick < max_tick));
  return _tick_names[tick];
}

