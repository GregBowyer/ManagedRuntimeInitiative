// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License version 2 only, as published by
// the Free Software Foundation.
//
// Azul designates this particular file as subject to the "Classpath" exception
// as provided by Azul in the LICENSE file that accompanied this code.
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

#include "jni.h"
#include "jvm.h"

#include "com_azulsystems_misc_VM.h"

/*
 * Class:     com_azulsystems_misc_VM
 * Method:    useLockedCollections0
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_azulsystems_misc_VM_useLockedCollections0(JNIEnv *env, jclass klass) {
    return JVM_GetUseLockedCollections();
}

/*
 * Class:     com_azulsystems_misc_VM
 * Method:    useCheckedCollections0
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_com_azulsystems_misc_VM_useCheckedCollections0(JNIEnv *env, jclass klass) {
    return JVM_GetUseCheckedCollections();
}

/*
 * Class:     com_azulsystems_misc_VM
 * Method:    shallowCopy0
 * Signature: (Ljava/lang/Object;)Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_com_azulsystems_misc_VM_shallowCopy0(JNIEnv *env, jclass klass, jobject obj) {
  return JVM_ShallowCopy(env, obj);
}

/*
 * Class:     com_azulsystems_misc_VM
 * Method:    referenceEquality0
 * Signature: (Ljava/lang/ref/Reference;Ljava/lang/Object;)Z
 */
JNIEXPORT jboolean JNICALL Java_com_azulsystems_misc_VM_referenceEquality0(JNIEnv *env, jclass klass, jobject ref, jobject obj) {
  return JVM_ReferenceEquality(env, ref, obj);
}

/*
 * Class:     com_azulsystems_misc_BlockingHint
 * Method:    set0
 * Signature: (Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Object;)
 */
JNIEXPORT void JNICALL Java_com_azulsystems_misc_BlockingHint_set0(JNIEnv *env, jclass klass, jobject str, jobject lock, jobject sync) {
  JVM_BlockingHint_set(env, str, lock, sync);
}

/*
 * Class:     com_azulsystems_misc_VM
 * Method:    getThreadLocalMapInitialCapacity0
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_azulsystems_misc_VM_getThreadLocalMapInitialCapacity0(JNIEnv *env, jclass klass) {
  return JVM_GetJavaThreadLocalMapInitialCapacity();
}

