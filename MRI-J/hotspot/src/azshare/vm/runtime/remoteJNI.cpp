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

/*
 * remoteJNI.cpp - provides necessary interface for invoking remote native methods
 *
 * This class provides the necessary interface for invoking remote native methods
 */


#include "remoteJNI.hpp"
#include "methodOop.hpp"

#ifdef AZ_PROXIED
#include <proxy/proxy_java.h>
#endif // AZ_PROXIED

/*
 * The library lookup method sets the entrypoints of remote methods which are looked up
 * in a thread local space and always returns the address of this entry point resolver
 * method (remoteJNI::set_native_entrypoint). On return from the lookup the VM calls this
 * resolver method which then extracts the real entrypoint from the thread local space
 * and sets the attribute to "remote" in the methodOop
 */
void remoteJNI::init_native_lookup(char *jni_name, int args_size)
{
#ifdef AZ_PROXIED
    proxy_init_localjni_entrypoint(jni_name, args_size);
#endif // AZ_PROXIED
}

address remoteJNI::set_native_entrypoint(methodHandle method, address entrypoint)
{
#ifdef AZ_PROXIED
    int is_local;

    proxy_get_localjni_entrypoint(&is_local, (void *)entrypoint);
    if (!is_local) {
method->set_is_remote_method();
    }
#endif // AZ_PROXIED

return(address)entrypoint;
}


#ifdef AZ_PROXIED

void remoteJNI::call_void_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_void_remotejni_method(env,
                                             rjniP->entrypoint(),
                                             rjniP->fingerprint(),
                                             rjniP->isstatic(),
rjniP->name(),
                                             rjniP->signature(),
                                             args);
if(err!=PROXY_ERROR_NONE){
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
    }
}

jint remoteJNI::call_jint_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
    jint ret;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_jint_remotejni_method(env,
                                             rjniP->entrypoint(),
                                             rjniP->fingerprint(),
                                             rjniP->isstatic(),
rjniP->name(),
                                             rjniP->signature(),
                                             args, &ret);
if(err==PROXY_ERROR_NONE){
        return ret;
    } else {
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
        return 0;
    }
}

jobject remoteJNI::call_jobject_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
jobject ret;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_jobject_remotejni_method(env,
                                                rjniP->entrypoint(),
                                                rjniP->fingerprint(),
                                                rjniP->isstatic(),
rjniP->name(),
                                                rjniP->signature(),
                                                args, &ret);
if(err==PROXY_ERROR_NONE){
        return ret;
    } else {
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
        return 0;
    }
}

jlong remoteJNI::call_jlong_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
    jlong ret;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_jlong_remotejni_method(env,
                                              rjniP->entrypoint(),
                                              rjniP->fingerprint(),
                                              rjniP->isstatic(),
rjniP->name(),
                                              rjniP->signature(),
                                              args, &ret);
if(err==PROXY_ERROR_NONE){
        return ret;
    } else {
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
        return 0;
    }
}

jfloat remoteJNI::call_jfloat_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
jfloat ret;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_jfloat_remotejni_method(env,
                                               rjniP->entrypoint(),
                                               rjniP->fingerprint(),
                                               rjniP->isstatic(),
rjniP->name(),
                                               rjniP->signature(),
                                               args, &ret);
if(err==PROXY_ERROR_NONE){
        return ret;
    } else {
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
        return 0;
    }
}

jdouble remoteJNI::call_jdouble_remotejni_method(JNIEnv *env, remoteJNI_info *rjniP, ...) {
proxy_error_t err;
jdouble ret;
    va_list args;

va_start(args,rjniP);
    err = proxy_invoke_jdouble_remotejni_method(env,
                                                rjniP->entrypoint(),
                                                rjniP->fingerprint(),
                                                rjniP->isstatic(),
rjniP->name(),
                                                rjniP->signature(),
                                                args, &ret);
if(err==PROXY_ERROR_NONE){
        return ret;
    } else {
jclass cls=env->FindClass("java/lang/OutOfMemoryError");

        if (cls != 0) {                                     /* if cls 0 an exception has already been thrown */
            env->ThrowNew(cls, "Unable to run call on proxy due to memory limitation");
        }
        return 0;
    }
}

#endif // AZ_PROXIED
