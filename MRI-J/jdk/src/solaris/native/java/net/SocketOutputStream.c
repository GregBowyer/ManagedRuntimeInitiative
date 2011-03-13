/*
 * Copyright 1997-2002 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
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
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#ifndef   AZ_PROXIED
#include <sys/socket.h>
#endif /* AZ_PROXIED */

#include "jni_util.h"
#include "jvm.h"
#include "net_util.h"

#include "java_net_SocketOutputStream.h"

#define min(a, b)       ((a) < (b) ? (a) : (b))

/*
 * SocketOutputStream
 */

static jfieldID IO_fd_fdID;

/*
 * Class:     java_net_SocketOutputStream
 * Method:    init
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_java_net_SocketOutputStream_init(JNIEnv *env, jclass cls) {
#ifndef   AZ_PROXIED
    IO_fd_fdID = NET_GetFileDescriptorID(env);
#else  /* AZ_PROXIED */
    jclass fdcls = (*env)->FindClass(env, "java/io/FileDescriptor");
    CHECK_NULL(fdcls);
    IO_fd_fdID = (*env)->GetFieldID(env, fdcls, "fd", "I");
    CHECK_NULL(IO_fd_fdID);
#endif /* AZ_PROXIED */
}

/*
 * Class:     java_net_SocketOutputStream
 * Method:    socketWrite0
 * Signature: (Ljava/io/FileDescriptor;[BII)V
 */
JNIEXPORT void JNICALL
Java_java_net_SocketOutputStream_socketWrite0(JNIEnv *env, jobject this,
                                              jobject fdObj,
                                              jbyteArray data,
                                              jint off, jint len) {
    char *bufP;
    char BUF[MAX_BUFFER_LEN];
    int buflen;
    int fd;
    jint n = 0;

    if (IS_NULL(fdObj)) {
        JNU_ThrowByName(env, "java/net/SocketException", "Socket closed");
        return;
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
        /* Bug 4086704 - If the Socket associated with this file descriptor
         * was closed (sysCloseFD), the the file descriptor is set to -1.
         */
        if (fd == -1) {
            JNU_ThrowByName(env, "java/net/SocketException", "Socket closed");
            return;
        }

    }

    if (len <= MAX_BUFFER_LEN) {
        bufP = BUF;
        buflen = MAX_BUFFER_LEN;
    } else {
        buflen = min(MAX_HEAP_BUFFER_LEN, len);
        bufP = (char *)malloc((size_t)buflen);

        /* if heap exhausted resort to stack buffer */
        if (bufP == NULL) {
            bufP = BUF;
            buflen = MAX_BUFFER_LEN;
        }
    }

    while(len > 0) {
        int loff = 0;
        int chunkLen = min(buflen, len);
        int llen = chunkLen;
#ifdef  AZ_PROXIED
        uint64_t sent;
        proxy_error_t rc;
#endif /* AZ_PROXIED */

        (*env)->GetByteArrayRegion(env, data, off, chunkLen, (jbyte *)bufP);

        while(llen > 0) {
#ifndef   AZ_PROXIED
            int n = NET_Send(fd, bufP + loff, llen, 0);
            if (n > 0) {
                llen -= n;
                loff += n;
                continue;
            }
            if (n == JVM_IO_INTR) {
#else  /* AZ_PROXIED */
            rc = v2dhandle_send(fd, bufP + loff, llen, 0, &sent);
            if (rc == PROXY_ERROR_NONE) {
                llen -= (int)sent;
                loff += (int)sent;
                continue;
            }
            if (rc == AZPR_EINTR) {           
                                  /* } */
#endif /* AZ_PROXIED */
                JNU_ThrowByName(env, "java/io/InterruptedIOException", 0);
            } else {
#ifndef   AZ_PROXIED
                if (errno == ECONNRESET) {
#else  /* AZ_PROXIED */
                if (rc == AZPR_ECONNRESET) {        
                                           /* } */
#endif /* AZ_PROXIED */
                    JNU_ThrowByName(env, "sun/net/ConnectionResetException",
                        "Connection reset");
                } else {
                    NET_ThrowByNameWithLastError(env, "java/net/SocketException",
                        "Write failed");
                }
            }
            if (bufP != BUF) {
                free(bufP);
            }
            return;
        }
        len -= chunkLen;
        off += chunkLen;
    }

    if (bufP != BUF) {
        free(bufP);
    }
}
