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
#ifndef REMOTEJNI_HPP
#define REMOTEJNI_HPP

/*
 * remoteJNI.hpp - provides necessary interface for invoking remote native methods
 *
 * This class provides the necessary interface for invoking remote native methods
 */
#include "allocation.hpp"
#include "globalDefinitions.hpp"
#include "handles.hpp"

class remoteJNI_info {
public:
  uint64_t entrypoint() const {return _entrypoint;}
  uint64_t fingerprint() const {return _fingerprint;}
  uint32_t isstatic() const { return _is_static; }
  uint32_t name_off() const {return _name_off;}
  uint32_t sig_off() const {return _sig_off;}
  char* name() const {return ((char*) &_name_off) + name_off();}
  char* signature() const {return ((char*) &_sig_off) + sig_off();}

private:
    uint64_t _entrypoint;
    uint64_t _fingerprint;
uint32_t _is_static;
uint32_t _reserved;
uint32_t _name_off;
uint32_t _sig_off;
};

class remoteJNI:AllStatic{

public:
    static void init_native_lookup(char *jni_name, int args_size);
    static address set_native_entrypoint(methodHandle method, address entry);
    static void call_void_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
    static jint call_jint_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
    static jobject call_jobject_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
    static jlong call_jlong_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
    static jfloat call_jfloat_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
    static jdouble call_jdouble_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...);
};
#endif // REMOTEJNI_HPP
