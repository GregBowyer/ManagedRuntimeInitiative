/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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

#include <errno.h>
#include <string.h>
#ifndef   AZ_PROXIED
#include <sys/types.h>
#include <sys/socket.h>
#if defined(__linux__) && !defined(USE_SELECT)
#include <sys/poll.h>
#endif
#include <netinet/tcp.h>        /* Defines TCP_NODELAY, needed for 2.6 */
#include <netinet/in.h>
#ifdef __linux__
#include <netinet/ip.h>
#endif
#include <netdb.h>
#endif /* !AZ_PROXIED */
#include <stdlib.h>

#ifndef   AZ_PROXIED
#ifdef __solaris__
#include <fcntl.h>
#endif
#ifdef __linux__
#include <linux/unistd.h>
#include <linux/sysctl.h>
#endif
#endif /* !AZ_PROXIED */

#include "jvm.h"
#include "jni_util.h"
#include "net_util.h"

#include "java_net_SocketOptions.h"
#include "java_net_PlainSocketImpl.h"

/************************************************************************
 * PlainSocketImpl
 */

static jfieldID IO_fd_fdID;

#if defined(AZ_PROXIED)
// These are declared extern in net_util.h, but not all are used anywhere else.
#endif /* AZ_PROXIED */
jfieldID psi_fdID;
jfieldID psi_addressID;
jfieldID psi_ipaddressID;
jfieldID psi_portID;
jfieldID psi_localportID;
jfieldID psi_timeoutID;
jfieldID psi_trafficClassID;
jfieldID psi_serverSocketID;
jfieldID psi_fdLockID;
jfieldID psi_closePendingID;

#if defined(AZ_PROXIED)
/* a global reference to the java.net.SocketException class. In
 * socketCreate, we ensure that this is initialized. This is to
 * prevent the problem where socketCreate runs out of file
 * descriptors, and is then unable to load the exception class.
 */
static jclass socketExceptionCls;
#endif /* AZ_PROXIED */

/*
 * file descriptor used for dup2
 */
static int marker_fd = -1;


#define SET_NONBLOCKING(fd) {           \
        int flags = fcntl(fd, F_GETFL); \
        flags |= O_NONBLOCK;            \
        fcntl(fd, F_SETFL, flags);      \
}

#define SET_BLOCKING(fd) {              \
        int flags = fcntl(fd, F_GETFL); \
        flags &= ~O_NONBLOCK;           \
        fcntl(fd, F_SETFL, flags);      \
}

#ifndef   AZ_PROXIED
/*
 * Create the marker file descriptor by establishing a loopback connection
 * which we shutdown but do not close the fd. The result is an fd that
 * can be used for read/write.
 */
static int getMarkerFD()
{
    int server_fd, child_fd, connect_fd;
    SOCKADDR him;
    int type, len, port;

    type = AF_INET;
#ifdef AF_INET6
    if (ipv6_available()) {
        type = AF_INET6;
    }
#endif

    /*
     * Create listener on any port
     */
    server_fd = JVM_Socket(type, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return -1;
    }
    if (JVM_Listen(server_fd, 1) == -1) {
        JVM_SocketClose(server_fd);
        return -1;
    }
    len = SOCKADDR_LEN;
    if (JVM_GetSockName(server_fd, (struct sockaddr *)&him, &len) == -1) {
        JVM_SocketClose(server_fd);
        return -1;
    }
    port = NET_GetPortFromSockaddr((struct sockaddr *)&him);

    /*
     * Establish connection from client socket.
     * Server is bound to 0.0.0.0/X or ::/X
     * We connect to 127.0.0.1/X or ::1/X
     */
#ifdef AF_INET6
    if (ipv6_available()) {
        struct sockaddr_in6 *him6 = (struct sockaddr_in6 *)&him;
        jbyte caddr[16];
        memset((char *) caddr, 0, 16);
        caddr[15] = 1;
        memset((char *)him6, 0, sizeof(struct sockaddr_in6));
        memcpy((void *)&(him6->sin6_addr), caddr, sizeof(struct in6_addr) );
        him6->sin6_port = htons((short) port);
        him6->sin6_family = AF_INET6;
        len = sizeof(struct sockaddr_in6) ;
    } else
#endif /* AF_INET6 */
    {
        struct sockaddr_in *him4 = (struct sockaddr_in*)&him;
        memset((char *) him4, 0, sizeof(struct sockaddr_in));
        him4->sin_port = htons((short) port);
        him4->sin_addr.s_addr = (uint32_t) htonl(0x7f000001);
        him4->sin_family = AF_INET;
        len = sizeof(struct sockaddr_in);
    }
    connect_fd = JVM_Socket(type, SOCK_STREAM, 0);
    if (connect_fd < 0) {
        JVM_SocketClose(server_fd);
        return -1;
    }
    if (JVM_Connect(connect_fd, (struct sockaddr *) &him, len) == -1) {
        JVM_SocketClose(server_fd);
        JVM_SocketClose(connect_fd);
        return -1;
    }

    /*
     * Server accepts connection - do in in non-blocking mode to avoid
     * hanging if there's an error (should never happen!!!)
     */
    SET_NONBLOCKING(server_fd);
    len = SOCKADDR_LEN;
    child_fd = JVM_Accept(server_fd, (struct sockaddr *)&him, (jint *)&len);
    if (child_fd == -1) {
        JVM_SocketClose(server_fd);
        JVM_SocketClose(connect_fd);
        return -1;
    }

    /*
     * Finally shutdown connect_fd (any reads to this fd will get
     * EOF; any writes will get an error).
     */
    JVM_SocketShutdown(connect_fd, 2);
    JVM_SocketClose(child_fd);
    JVM_SocketClose(server_fd);

    return connect_fd;
}
#endif /* !AZ_PROXIED */


/*
 * Return the file descriptor given a PlainSocketImpl
 */
static int getFD(JNIEnv *env, jobject this) {
#if defined(AZ_PROXIED)
    int fd = -1;
#endif /* AZ_PROXIED */
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
    CHECK_NULL_RETURN(fdObj, -1);
#ifndef   AZ_PROXIED
    return (*env)->GetIntField(env, fdObj, IO_fd_fdID);
#else  /* AZ_PROXIED */
    fd =  (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    return fd;
#endif /* AZ_PROXIED */
}

/*
 * The initroto function is called whenever PlainSocketImpl is
 * loaded, to cache fieldIds for efficiency. This is called everytime
 * the Java class is loaded.
 *
 * Class:     java_net_PlainSocketImpl
 * Method:    initProto
 * Signature: ()V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_initProto(JNIEnv *env, jclass cls) {
    char *s;

    psi_fdID = (*env)->GetFieldID(env, cls , "fd",
                                  "Ljava/io/FileDescriptor;");
    CHECK_NULL(psi_fdID);
    psi_addressID = (*env)->GetFieldID(env, cls, "address",
                                          "Ljava/net/InetAddress;");
    CHECK_NULL(psi_addressID);
    psi_portID = (*env)->GetFieldID(env, cls, "port", "I");
    CHECK_NULL(psi_portID);
    psi_localportID = (*env)->GetFieldID(env, cls, "localport", "I");
    CHECK_NULL(psi_localportID);
    psi_timeoutID = (*env)->GetFieldID(env, cls, "timeout", "I");
    CHECK_NULL(psi_timeoutID);
    psi_trafficClassID = (*env)->GetFieldID(env, cls, "trafficClass", "I");
    CHECK_NULL(psi_trafficClassID);
    psi_serverSocketID = (*env)->GetFieldID(env, cls, "serverSocket",
                        "Ljava/net/ServerSocket;");
    CHECK_NULL(psi_serverSocketID);
    psi_fdLockID = (*env)->GetFieldID(env, cls, "fdLock",
                                      "Ljava/lang/Object;");
    CHECK_NULL(psi_fdLockID);
    psi_closePendingID = (*env)->GetFieldID(env, cls, "closePending", "Z");
    CHECK_NULL(psi_closePendingID);
#ifndef   AZ_PROXIED
    IO_fd_fdID = NET_GetFileDescriptorID(env);
#else  /* AZ_PROXIED */
    jclass fdcls = (*env)->FindClass(env, "java/io/FileDescriptor");
    CHECK_NULL(fdcls);
    IO_fd_fdID = (*env)->GetFieldID(env, fdcls, "fd", "I");
#endif /* AZ_PROXIED */
    CHECK_NULL(IO_fd_fdID);

#ifndef   AZ_PROXIED
    /* Create the marker fd used for dup2 */
    marker_fd = getMarkerFD();
#else  /* AZ_PROXIED */
    jclass c = (*env)->FindClass(env, "java/net/SocketException");
    CHECK_NULL(c);
    socketExceptionCls = (jclass)(*env)->NewGlobalRef(env, c);
    CHECK_NULL(socketExceptionCls);
#endif /* AZ_PROXIED */
}

#ifndef   AZ_PROXIED
/* a global reference to the java.net.SocketException class. In
 * socketCreate, we ensure that this is initialized. This is to
 * prevent the problem where socketCreate runs out of file
 * descriptors, and is then unable to load the exception class.
 */
static jclass socketExceptionCls;
#endif /* !AZ_PROXIED */

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketCreate
 * Signature: (Z)V */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketCreate(JNIEnv *env, jobject this,
                                           jboolean stream) {
    jobject fdObj, ssObj;
    int fd;
#ifndef   AZ_PROXIED
    int arg = -1;

    if (socketExceptionCls == NULL) {
        jclass c = (*env)->FindClass(env, "java/net/SocketException");
        CHECK_NULL(c);
        socketExceptionCls = (jclass)(*env)->NewGlobalRef(env, c);
        CHECK_NULL(socketExceptionCls);
    }
#endif /* !AZ_PROXIED */
    fdObj = (*env)->GetObjectField(env, this, psi_fdID);

    if (fdObj == NULL) {
#if defined(AZ_PROXIED)
        // Azul Note: Could happen if there's a race creating and closing a socket.
        // Obvious candidate for synchronized...FIXME?
#endif /* AZ_PROXIED */
        (*env)->ThrowNew(env, socketExceptionCls, "null fd object");
        return;
#ifndef   AZ_PROXIED
    }
#ifdef AF_INET6
    if (ipv6_available()) {
        fd = JVM_Socket(AF_INET6, (stream ? SOCK_STREAM: SOCK_DGRAM), 0);
    } else
#endif /* AF_INET6 */
        {
            fd = JVM_Socket(AF_INET, (stream ? SOCK_STREAM: SOCK_DGRAM), 0);
        }
    if (fd == JVM_IO_ERR) {
        /* note: if you run out of fds, you may not be able to load
         * the exception class, and get a NoClassDefFoundError
         * instead.
         */
        NET_ThrowNew(env, errno, "can't create socket");
        return;
    } else {
        (*env)->SetIntField(env, fdObj, IO_fd_fdID, fd);
#endif /* !AZ_PROXIED */
    }

#if defined(AZ_PROXIED)
    proxy_protocol_family_t family = ipv6_available() ? PROXY_FAMILY_INET6 : PROXY_FAMILY_INET;
#endif /* AZ_PROXIED */
    /*
     * If this is a server socket then enable SO_REUSEADDR
     * automatically and set to non blocking.
     */
    ssObj = (*env)->GetObjectField(env, this, psi_serverSocketID);
#if defined(AZ_PROXIED)
    int reuse_address = 0;
    int is_server = 0;
#endif /* AZ_PROXIED */
    if (ssObj != NULL) {
#ifndef   AZ_PROXIED
        int arg = 1;
        SET_NONBLOCKING(fd);
        JVM_SetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&arg,
            sizeof(arg));
#else  /* AZ_PROXIED */
        reuse_address = 1;
        is_server = 1;
#endif /* AZ_PROXIED */
    }
#if defined(AZ_PROXIED)
    int fd = v2dhandle_socket(family, stream ? PROXY_SOCK_STREAM : PROXY_SOCK_DGRAM, reuse_address, is_server);
    if (fd < 0) {
        NET_ThrowNew(env, errno, "can't create socket");
        return;
    } else {
        (*env)->SetIntField(env, fdObj, IO_fd_fdID, fd);
    }
    /* fd1 not used in solaris/linux */
    (*env)->SetObjectField(env, this, psi_fd1ID, 0);
#endif /* AZ_PROXIED */
}

/*
 * inetAddress is the address object passed to the socket connect
 * call.
 *
 * Class:     java_net_PlainSocketImpl
 * Method:    socketConnect
 * Signature: (Ljava/net/InetAddress;I)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketConnect(JNIEnv *env, jobject this,
                                            jobject iaObj, jint port,
                                            jint timeout)
{
    jint localport = (*env)->GetIntField(env, this, psi_localportID);
    int len = 0;

#ifndef   AZ_PROXIED
    /* fdObj is the FileDescriptor field on this */
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    jobject fdLock;
#ifndef   AZ_PROXIED

    jint trafficClass = (*env)->GetIntField(env, this, psi_trafficClassID);

    /* fd is an int field on iaObj */
    jint fd;
#endif /* !AZ_PROXIED */

    SOCKADDR him;
#ifndef   AZ_PROXIED
    /* The result of the connection */
    int connect_rv = -1;

    if (IS_NULL(fdObj)) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
        return;
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    }
#endif /* !AZ_PROXIED */
    if (IS_NULL(iaObj)) {
        JNU_ThrowNullPointerException(env, "inet address argument null.");
        return;
    }

    /* connect */
    if (NET_InetAddressToSockaddr(env, iaObj, port, (struct sockaddr *)&him, &len, JNI_TRUE) != 0) {
      return;
    }

#ifndef   AZ_PROXIED
#ifdef AF_INET6
    if (trafficClass != 0 && ipv6_available()) {
        NET_SetTrafficClass((struct sockaddr *)&him, trafficClass);
    }
#endif /* AF_INET6 */
    if (timeout <= 0) {
        connect_rv = NET_Connect(fd, (struct sockaddr *)&him, len);
#ifdef __solaris__
        if (connect_rv == JVM_IO_ERR && errno == EINPROGRESS ) {

            /* This can happen if a blocking connect is interrupted by a signal.
             * See 6343810.
             */
            while (1) {
                fd_set wr, ex;

                FD_ZERO(&wr);
                FD_SET(fd, &wr);
                FD_ZERO(&ex);
                FD_SET(fd, &ex);

                errno = 0;
                connect_rv = NET_Select(fd+1, 0, &wr, &ex, 0);
                if (connect_rv == JVM_IO_ERR) {
                    if (errno == EINTR) {
                        continue;
                    } else {
                        break;
                    }
                }
                if (connect_rv > 0) {
                    int optlen;
                    /* has connection been established */
                    optlen = sizeof(connect_rv);
                    if (JVM_GetSockOpt(fd, SOL_SOCKET, SO_ERROR,
                                        (void*)&connect_rv, &optlen) <0) {
                        connect_rv = errno;
                    }

                    if (connect_rv != 0) {
                        /* restore errno */
                        errno = connect_rv;
                        connect_rv = JVM_IO_ERR;
                    }
                    break;
                }
            }
        }
#endif   /* __solaris__ */
#else  /* AZ_PROXIED */
    int fd;
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
    if (fdObj != NULL) {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
#endif /* AZ_PROXIED */
    } else {
#ifndef   AZ_PROXIED
        /*
         * A timeout was specified. We put the socket into non-blocking
         * mode, connect, and then wait for the connection to be
         * established, fail, or timeout.
         */
        SET_NONBLOCKING(fd);

        /* no need to use NET_Connect as non-blocking */
        connect_rv = connect(fd, (struct sockaddr *)&him, len);

        /* connection not established immediately */
        if (connect_rv != 0) {
            int optlen;
            jlong prevTime = JVM_CurrentTimeMillis(env, 0);

            if (errno != EINPROGRESS) {
                NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "ConnectException",
                             "connect failed");
                SET_BLOCKING(fd);
                return;
            }

            /*
             * Wait for the connection to be established or a
             * timeout occurs. poll/select needs to handle EINTR in
             * case lwp sig handler redirects any process signals to
             * this thread.
             */
            while (1) {
                jlong newTime;
#ifndef USE_SELECT
                {
                    struct pollfd pfd;
                    pfd.fd = fd;
                    pfd.events = POLLOUT;

                    errno = 0;
                    connect_rv = NET_Poll(&pfd, 1, timeout);
                }
#else
                {
                    fd_set wr, ex;
                    struct timeval t;

                    t.tv_sec = timeout / 1000;
                    t.tv_usec = (timeout % 1000) * 1000;

                    FD_ZERO(&wr);
                    FD_SET(fd, &wr);
                    FD_ZERO(&ex);
                    FD_SET(fd, &ex);

                    errno = 0;
                    connect_rv = NET_Select(fd+1, 0, &wr, &ex, &t);
                }
#endif

                if (connect_rv >= 0) {
                    break;
                }
                if (errno != EINTR) {
                    break;
                }

                /*
                 * The poll was interrupted so adjust timeout and
                 * restart
                 */
                newTime = JVM_CurrentTimeMillis(env, 0);
                timeout -= (newTime - prevTime);
                if (timeout <= 0) {
                    connect_rv = 0;
                    break;
                }
                prevTime = newTime;

            } /* while */

            if (connect_rv == 0) {
                JNU_ThrowByName(env, JNU_JAVANETPKG "SocketTimeoutException",
                            "connect timed out");

                /*
                 * Timeout out but connection may still be established.
                 * At the high level it should be closed immediately but
                 * just in case we make the socket blocking again and
                 * shutdown input & output.
                 */
                SET_BLOCKING(fd);
                JVM_SocketShutdown(fd, 2);
                return;
            }

            /* has connection been established */
            optlen = sizeof(connect_rv);
            if (JVM_GetSockOpt(fd, SOL_SOCKET, SO_ERROR, (void*)&connect_rv,
                               &optlen) <0) {
                connect_rv = errno;
            }
        }

        /* make socket blocking again */
        SET_BLOCKING(fd);

        /* restore errno */
        if (connect_rv != 0) {
            errno = connect_rv;
            connect_rv = JVM_IO_ERR;
        }
    }
#else  /* AZ_PROXIED */
        fd = -1;
    }
    if (fd < 0) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
        return;
    }
#endif /* AZ_PROXIED */

#ifndef   AZ_PROXIED
    /* report the appropriate exception */
    if (connect_rv < 0) {

#ifdef __linux__
        /*
         * Linux/GNU distribution setup /etc/hosts so that
         * InetAddress.getLocalHost gets back the loopback address
         * rather than the host address. Thus a socket can be
         * bound to the loopback address and the connect will
         * fail with EADDRNOTAVAIL. In addition the Linux kernel
         * returns the wrong error in this case - it returns EINVAL
         * instead of EADDRNOTAVAIL. We handle this here so that
         * a more descriptive exception text is used.
         */
        if (connect_rv == JVM_IO_ERR && errno == EINVAL) {
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                "Invalid argument or cannot assign requested address");
            return;
        }
#endif
        if (connect_rv == JVM_IO_INTR) {
            JNU_ThrowByName(env, JNU_JAVAIOPKG "InterruptedIOException",
                            "operation interrupted");
        } else if (errno == EPROTO) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "ProtocolException",
                           "Protocol error");
        } else if (errno == ECONNREFUSED) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "ConnectException",
                           "Connection refused");
        } else if (errno == ETIMEDOUT) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "ConnectException",
                           "Connection timed out");
        } else if (errno == EHOSTUNREACH) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "NoRouteToHostException",
                           "Host unreachable");
        } else if (errno == EADDRNOTAVAIL) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "NoRouteToHostException",
                             "Address not available");
        } else if ((errno == EISCONN) || (errno == EBADF)) {
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                            "Socket closed");
        } else {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException", "connect failed");
        }
        return;
#else  /* AZ_PROXIED */
    if (ipv6_available()) {
        jint trafficClass = (*env)->GetIntField(env, this, psi_trafficClassID);
        if (trafficClass != 0) {
            NET_SetTrafficClass((struct sockaddr *)&him, trafficClass);
        }
    }

    /* The result of the connection */
    proxy_error_t rc = v2dhandle_connect(fd, (struct sockaddr *)&him, timeout);
    if (rc != PROXY_ERROR_NONE) {
	// Report the appropriate exception
	// Note: We should standardize the error code to exception conversion mechanism
	switch(rc) {
	case AZPR_EBADF:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
	    break;
	case AZPR_ETIMEDOUT:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "ConnectException", "Connection timed out");
	    break;
	case PROXY_SOCKETNAME_EXCEPTION:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Error getting socket name");
	    break;
	case AZPR_EPROTO:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "ProtocolException", "Protocol error");
	    break;
	case AZPR_ECONNREFUSED:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "ConnectException", "Connection refused");
	    break;
	case AZPR_EHOSTUNREACH:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "NoRouteToHostException", "Host unreachable");
	    break;
	case AZPR_EADDRNOTAVAIL:
	    JNU_ThrowByName(env, JNU_JAVANETPKG "NoRouteToHostException", "Address not available");
	    break;
	case AZPR_EINTR:
	    JNU_ThrowByName(env, JNU_JAVAIOPKG "InterruptedIOException", "operation interrupted");
	    break;
	default:
	    NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "ConnectException", "connect failed");
	    break;
	}
	return;
#endif /* AZ_PROXIED */
    }

    /*
#ifndef   AZ_PROXIED
     * The socket may have been closed (dup'ed) while we were
     * poll/select. In that case SO_ERROR will return 0 making
     * it appear that the connection has been established.
#else  // AZ_PROXIED
     * The socket may have been closed (dup'ed) while we were in
     * poll/select or on the proxy.
#endif // AZ_PROXIED
     * To avoid any race conditions we therefore grab the
     * fd lock, check if the socket has been closed, and
     * set the various fields whilst holding the lock
     */
    fdLock = (*env)->GetObjectField(env, this, psi_fdLockID);
    (*env)->MonitorEnter(env, fdLock);

    if ((*env)->GetBooleanField(env, this, psi_closePendingID)) {

        /* release fdLock */
        (*env)->MonitorExit(env, fdLock);

        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                            "Socket closed");
        return;
    }

    (*env)->SetIntField(env, fdObj, IO_fd_fdID, fd);

    /* set the remote peer address and port */
    (*env)->SetObjectField(env, this, psi_addressID, iaObj);
    (*env)->SetIntField(env, this, psi_portID, port);

    /*
     * we need to initialize the local port field if bind was called
     * previously to the connect (by the client) then localport field
     * will already be initialized
     */
    if (localport == 0) {
        /* Now that we're a connected socket, let's extract the port number
         * that the system chose for us and store it in the Socket object.
         */
#ifndef   AZ_PROXIED
        len = SOCKADDR_LEN;
        if (JVM_GetSockName(fd, (struct sockaddr *)&him, &len) == -1) {
#else  /* AZ_PROXIED */
        proxy_error_t rc = v2dhandle_getsockname(fd, (struct sockaddr *)&him);
        if (rc != PROXY_ERROR_NONE) {                           /* } */
#endif /* AZ_PROXIED */
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                           "Error getting socket name");
        } else {
            localport = NET_GetPortFromSockaddr((struct sockaddr *)&him);
            (*env)->SetIntField(env, this, psi_localportID, localport);
        }
    }

    /*
     * Finally release fdLock
     */
    (*env)->MonitorExit(env, fdLock);
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketBind
 * Signature: (Ljava/net/InetAddress;I)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketBind(JNIEnv *env, jobject this,
                                         jobject iaObj, jint localport) {

#ifndef   AZ_PROXIED
    /* fdObj is the FileDescriptor field on this */
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    /* fd is an int field on fdObj */
    int fd;
    int len;
    SOCKADDR him;

#ifndef   AZ_PROXIED
    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    fd = getFD(env, this);
    if (fd < 0) {                              /* } */
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return;
#ifndef   AZ_PROXIED
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    }
    if (IS_NULL(iaObj)) {
        JNU_ThrowNullPointerException(env, "iaObj is null.");
        return;
#endif /* !AZ_PROXIED */
    }

    /* bind */
    if (NET_InetAddressToSockaddr(env, iaObj, localport, (struct sockaddr *)&him, &len, JNI_TRUE) != 0) {
      return;
    }

#ifndef   AZ_PROXIED
    if (NET_Bind(fd, (struct sockaddr *)&him, len) < 0) {
        if (errno == EADDRINUSE || errno == EADDRNOTAVAIL ||
            errno == EPERM || errno == EACCES) {
#else  /* AZ_PROXIED */
    proxy_error_t rc = v2dhandle_bind(fd, (struct sockaddr *)&him);
    if (rc != PROXY_ERROR_NONE) {                                     /* } */
        if (rc == AZPR_EADDRINUSE || rc == AZPR_EADDRNOTAVAIL ||
            rc == AZPR_EPERM || rc == AZPR_EACCES) {                  /* } */
#endif /* AZ_PROXIED */
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "BindException",
                           "Bind failed");
#if defined(AZ_PROXIED)
        } else if (rc == PROXY_SOCKETNAME_EXCEPTION) {
	        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Error getting socket name");
#endif /* AZ_PROXIED */
        } else {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                           "Bind failed");
        }
        return;
    }

    /* set the address */
    (*env)->SetObjectField(env, this, psi_addressID, iaObj);

    /* intialize the local port */
    if (localport == 0) {
        /* Now that we're a connected socket, let's extract the port number
         * that the system chose for us and store it in the Socket object.
         */
#ifndef   AZ_PROXIED
        if (JVM_GetSockName(fd, (struct sockaddr *)&him, &len) == -1) {
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                           "Error getting socket name");
            return;
        }
        localport = NET_GetPortFromSockaddr((struct sockaddr *)&him);
        (*env)->SetIntField(env, this, psi_localportID, localport);
    } else {
        (*env)->SetIntField(env, this, psi_localportID, localport);
    }
#else  /* AZ_PROXIED */
        localport = NET_GetPortFromSockaddr((struct sockaddr *)&him);
    }
    (*env)->SetIntField(env, this, psi_localportID, localport);
#endif /* AZ_PROXIED */
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketListen
 * Signature: (I)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketListen (JNIEnv *env, jobject this,
                                            jint count)
{
#ifndef   AZ_PROXIED
    /* this FileDescriptor fd field */
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    /* fdObj's int fd field */
    int fd;

#ifndef   AZ_PROXIED
    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    fd = getFD(env, this);
    if (fd < 0) { 
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return;
#ifndef   AZ_PROXIED
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
#endif /* !AZ_PROXIED */
    }

#ifndef   AZ_PROXIED
    /*
     * Workaround for bugid 4101691 in Solaris 2.6. See 4106600.
     * If listen backlog is Integer.MAX_VALUE then subtract 1.
     */
    if (count == 0x7fffffff)
        count -= 1;

    if (JVM_Listen(fd, count) == JVM_IO_ERR) {
#else  /* AZ_PROXIED */
    proxy_error_t rc = listen(fd, count);
    if (rc != PROXY_ERROR_NONE) {
#endif /* AZ_PROXIED */
        NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                       "Listen failed");
    }
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketAccept
 * Signature: (Ljava/net/SocketImpl;)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketAccept(JNIEnv *env, jobject this,
                                           jobject socket)
{
    /* fields on this */
    int port;
#ifndef   AZ_PROXIED
    jint timeout = (*env)->GetIntField(env, this, psi_timeoutID);
    jlong prevTime = 0;
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#else  /* AZ_PROXIED */

    if (IS_NULL(socket)) {
	JNU_ThrowNullPointerException(env, "socket is null");
	return;
    }
#endif /* AZ_PROXIED */

    /* the FileDescriptor field on socket */
    jobject socketFdObj;
    /* the InetAddress field on socket */
    jobject socketAddressObj;

    /* the ServerSocket fd int field on fdObj */
    jint fd;
#if defined(AZ_PROXIED)
    fd = getFD(env, this);
    if (fd < 0) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
        return;
    }
#endif /* AZ_PROXIED */

    /* accepted fd */
    jint newfd;

#ifndef   AZ_PROXIED
    jthrowable error;
#endif /* !AZ_PROXIED */

    SOCKADDR him;
#ifndef   AZ_PROXIED
    int len;

    len = SOCKADDR_LEN;

    if (IS_NULL(fdObj)) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return;
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    }
    if (IS_NULL(socket)) {
        JNU_ThrowNullPointerException(env, "socket is null");
        return;
    }

    /*
     * accept connection but ignore ECONNABORTED indicating that
     * connection was eagerly accepted by the OS but was reset
     * before accept() was called.
     *
     * If accept timeout in place and timeout is adjusted with
     * each ECONNABORTED or EWOULDBLOCK to ensure that semantics
     * of timeout are preserved.
     */
    for (;;) {
        int ret;

        /* first usage pick up current time */
        if (prevTime == 0 && timeout > 0) {
            prevTime = JVM_CurrentTimeMillis(env, 0);
        }

        /* passing a timeout of 0 to poll will return immediately,
           but in the case of ServerSocket 0 means infinite. */
        if (timeout <= 0) {
            ret = NET_Timeout(fd, -1);
        } else {
            ret = NET_Timeout(fd, timeout);
        }

        if (ret == 0) {
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketTimeoutException",
                            "Accept timed out");
            return;
        } else if (ret == JVM_IO_ERR) {
            if (errno == EBADF) {
               JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
            } else {
               NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException", "Accept failed");
            }
            return;
        } else if (ret == JVM_IO_INTR) {
            JNU_ThrowByName(env, JNU_JAVAIOPKG "InterruptedIOException",
                            "operation interrupted");
            return;
        }

        newfd = NET_Accept(fd, (struct sockaddr *)&him, (jint*)&len);

        /* connection accepted */
        if (newfd >= 0) {
            SET_BLOCKING(newfd);
            break;
        }

        /* non (ECONNABORTED or EWOULDBLOCK) error */
        if (!(errno == ECONNABORTED || errno == EWOULDBLOCK)) {
            break;
        }

        /* ECONNABORTED or EWOULDBLOCK error so adjust timeout if there is one. */
        if (timeout) {
            jlong currTime = JVM_CurrentTimeMillis(env, 0);
            timeout -= (currTime - prevTime);

            if (timeout <= 0) {
                JNU_ThrowByName(env, JNU_JAVANETPKG "SocketTimeoutException",
                                "Accept timed out");
                return;
            }
            prevTime = currTime;
        }
    }

    if (newfd < 0) {
        if (newfd == -2) {
            JNU_ThrowByName(env, JNU_JAVAIOPKG "InterruptedIOException",
                            "operation interrupted");
        } else {
            if (errno == EINVAL) {
                errno = EBADF;
            }
            if (errno == EBADF) {
                JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
            } else {
                NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException", "Accept failed");
            }
        }
        return;
    }
#else  /* AZ_PROXIED */
    jint timeout = (*env)->GetIntField(env, this, psi_timeoutID);

    proxy_error_t rc = v2dhandle_accept(fd, timeout, &new_fd, (struct sockaddr *)&him);
    if (rc == PROXY_ERROR_NONE) {
        /*
         * fill up the remote peer port and address in the new socket structure.
         */
        socketAddressObj = NET_SockaddrToInetAddress(env, (struct sockaddr *)&him, &port);
        if (socketAddressObj == NULL) {
            /* should be pending exception */
            close(new_fd);
            return;
        }

        /*
    	 * Populate SocketImpl.fd.fd
         */
        socketFdObj = (*env)->GetObjectField(env, socket, psi_fdID);
        (*env)->SetIntField(env, socketFdObj, IO_fd_fdID, new_fd);

        (*env)->SetObjectField(env, socket, psi_addressID, socketAddressObj);
        (*env)->SetIntField(env, socket, psi_portID, port);
        /* also fill up the local port information */
        port = (*env)->GetIntField(env, this, psi_localportID);
        (*env)->SetIntField(env, socket, psi_localportID, port);
    } else {
        // Do usual error code to exception translation ...
        // Note to self: Can we have single function do this ?
        switch(rc) {
        case AZPR_ETIMEDOUT:
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketTimeoutException", "Accept timed out");
            break;
        case AZPR_EBADF:
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Socket closed");
            break;
        case AZPR_EINTR:
            JNU_ThrowByName(env, JNU_JAVAIOPKG "InterruptedIOException", "operation interrupted");
            break;
        default:
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException", "Accept failed");
        }
    }
#endif /* AZ_PROXIED */

#ifndef   AZ_PROXIED
    /*
     * fill up the remote peer port and address in the new socket structure.
     */
    socketAddressObj = NET_SockaddrToInetAddress(env, (struct sockaddr *)&him, &port);
    if (socketAddressObj == NULL) {
        /* should be pending exception */
        close(newfd);
        return;
    }

    /*
     * Populate SocketImpl.fd.fd
     */
    socketFdObj = (*env)->GetObjectField(env, socket, psi_fdID);
    (*env)->SetIntField(env, socketFdObj, IO_fd_fdID, newfd);

    (*env)->SetObjectField(env, socket, psi_addressID, socketAddressObj);
    (*env)->SetIntField(env, socket, psi_portID, port);
    /* also fill up the local port information */
     port = (*env)->GetIntField(env, this, psi_localportID);
    (*env)->SetIntField(env, socket, psi_localportID, port);
#endif /* !AZ_PROXIED */
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketAvailable
 * Signature: ()I
 */
JNIEXPORT jint JNICALL
Java_java_net_PlainSocketImpl_socketAvailable(JNIEnv *env, jobject this) {

    jint ret = -1;
#ifndef   AZ_PROXIED
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    jint fd;

#ifndef   AZ_PROXIED
    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    fd = getFD(env, this);
    if (fd < 0) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return -1;
#ifndef   AZ_PROXIED
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
#endif /* !AZ_PROXIED */
    }
    /* JVM_SocketAvailable returns 0 for failure, 1 for success */
#ifndef   AZ_PROXIED
    if (!JVM_SocketAvailable(fd, &ret)){
        if (errno == ECONNRESET) {
#else  /* AZ_PROXIED */
    uint64_t nbytes;
    proxy_error_t rc = v2dhandle_available(fd, &nbytes);
    if (rc == PROXY_ERROR_NONE) {
        ret = nbytes;
    } else if (rc == AZPR_ECONNRESET) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, "sun/net/ConnectionResetException", "");
    } else {
        NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                                         "ioctl FIONREAD failed");
    }
#ifndef   AZ_PROXIED
    }
#endif /* !AZ_PROXIED */
    return ret;
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketClose0
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketClose0(JNIEnv *env, jobject this,
                                          jboolean useDeferredClose) {

    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
    jint fd;

#ifndef   AZ_PROXIED
    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    if (fdObj == NULL) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "socket already closed");
        return;
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    }
    if (fd != -1) {
#ifndef   AZ_PROXIED
        if (useDeferredClose && marker_fd >= 0) {
            NET_Dup2(marker_fd, fd);
        } else {
            (*env)->SetIntField(env, fdObj, IO_fd_fdID, -1);
            NET_SocketClose(fd);
        }
#else  /* AZ_PROXIED */
        // FIXME? revisit code to evaluate javasoft change, marker_fd
        if (useDeferredClose) {
            v2dhandle_preclose(fd);
        } else {
            (*env)->SetIntField(env, fdObj, IO_fd_fdID, -1);
            close(fd);
        }
#endif /* AZ_PROXIED */
    }
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketShutdown
 * Signature: (I)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketShutdown(JNIEnv *env, jobject this,
                                             jint howto)
{

#ifndef   AZ_PROXIED
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    jint fd;

    /*
     * WARNING: THIS NEEDS LOCKING. ALSO: SHOULD WE CHECK for fd being
     * -1 already?
     */
#ifndef   AZ_PROXIED
    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    fd = getFD(env, this);
    if (fd < 0) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "socket already closed");
        return;
#ifndef   AZ_PROXIED
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
    }
    JVM_SocketShutdown(fd, howto);
#else  /* AZ_PROXIED */
    }
    shutdown(fd, howto);
#endif /* AZ_PROXIED */
}


/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketSetOption
 * Signature: (IZLjava/lang/Object;)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketSetOption(JNIEnv *env, jobject this,
                                              jint cmd, jboolean on,
                                              jobject value) {
    int fd;
    int level, optname, optlen;
#ifndef   AZ_PROXIED
    union {
        int i;
        struct linger ling;
    } optval;
#endif /* !AZ_PROXIED */

    /*
     * Check that socket hasn't been closed
     */
    fd = getFD(env, this);
    if (fd < 0) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return;
    }

#ifndef   AZ_PROXIED
    /*
     * SO_TIMEOUT is a no-op on Solaris/Linux
     */
    if (cmd == java_net_SocketOptions_SO_TIMEOUT) {
        return;
    }

    /*
     * Map the Java level socket option to the platform specific
     * level and option name.
     */
    if (NET_MapSocketOption(cmd, &level, &optname)) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Invalid option");
        return;
    }
#else  /* AZ_PROXIED */
    // The calling Java code validates the option so we don't need to.

    uint64_t optval;
#endif /* AZ_PROXIED */

    switch (cmd) {
        case java_net_SocketOptions_SO_SNDBUF :
        case java_net_SocketOptions_SO_RCVBUF :
        case java_net_SocketOptions_SO_LINGER :
        case java_net_SocketOptions_IP_TOS :
            {
                jclass cls;
                jfieldID fid;

                cls = (*env)->FindClass(env, "java/lang/Integer");
                CHECK_NULL(cls);
                fid = (*env)->GetFieldID(env, cls, "value", "I");
                CHECK_NULL(fid);

#ifndef   AZ_PROXIED
                if (cmd == java_net_SocketOptions_SO_LINGER) {
                    if (on) {
                        optval.ling.l_onoff = 1;
                        optval.ling.l_linger = (*env)->GetIntField(env, value, fid);
                    } else {
                        optval.ling.l_onoff = 0;
                        optval.ling.l_linger = 0;
                    }
                    optlen = sizeof(optval.ling);
                } else {
                    optval.i = (*env)->GetIntField(env, value, fid);
                    optlen = sizeof(optval.i);
                }
#else  /* AZ_PROXIED */
            // We will treat a linger time of zero as off, for obvious reasons.
            if (cmd == java_net_SocketOptions_SO_LINGER) {
                if (on) {
                    optval = 1;
                    optval = (uint64_t)((optval << 32) | (*env)->GetIntField(env, value, fid));
                } else {
                    optval = 0;
                }
            } else {
                optval = (uint64_t)(*env)->GetIntField(env, value, fid);
            }
#endif /* AZ_PROXIED */

                break;
            }

        /* Boolean -> int */
        default :
#ifndef   AZ_PROXIED
            optval.i = (on ? 1 : 0);
            optlen = sizeof(optval.i);
#else  /* AZ_PROXIED */
            optval = (uint64_t)(on ? 1 : 0);
#endif /* AZ_PROXIED */

    }

#ifndef   AZ_PROXIED
    if (NET_SetSockOpt(fd, level, optname, (const void *)&optval, optlen) < 0) {
#ifdef __solaris__
        if (errno == EINVAL) {
            // On Solaris setsockopt will set errno to EINVAL if the socket
            // is closed. The default error message is then confusing
            char fullMsg[128];
            jio_snprintf(fullMsg, sizeof(fullMsg), "Invalid option or socket reset by remote peer");
            JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", fullMsg);
            return;
        }
#endif /* __solaris__ */
#else  /* AZ_PROXIED */
    proxy_error_t rc = v2dhandle_setsockopt(fd, cmd, (const uint64_t)optval);
    if (rc != PROXY_ERROR_NONE) {
#endif /* AZ_PROXIED */
        NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                                      "Error setting socket option");
    }
}

/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketGetOption
 * Signature: (I)I
 */
JNIEXPORT jint JNICALL
Java_java_net_PlainSocketImpl_socketGetOption(JNIEnv *env, jobject this,
                                              jint cmd, jobject iaContainerObj) {

    int fd;
    int level, optname, optlen;
    union {
        int i;
        struct linger ling;
    } optval;

    /*
     * Check that socket hasn't been closed
     */
    fd = getFD(env, this);
    if (fd < 0) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException",
                        "Socket closed");
        return -1;
    }

    /*
     * SO_BINDADDR isn't a socket option
     */
    if (cmd == java_net_SocketOptions_SO_BINDADDR) {
        SOCKADDR him;
        int len = 0;
        int port;
        jobject iaObj;
        jclass iaCntrClass;
        jfieldID iaFieldID;

        len = SOCKADDR_LEN;

#ifndef   AZ_PROXIED
        if (getsockname(fd, (struct sockaddr *)&him, &len) < 0) {
#else  /* AZ_PROXIED */
        proxy_error_t rc = v2dhandle_getsockname(fd, (struct sockaddr *)&him);
        if (rc != PROXY_ERROR_NONE) {
#endif /* AZ_PROXIED */
            NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                             "Error getting socket name");
            return -1;
        }
        iaObj = NET_SockaddrToInetAddress(env, (struct sockaddr *)&him, &port);
        CHECK_NULL_RETURN(iaObj, -1);

        iaCntrClass = (*env)->GetObjectClass(env, iaContainerObj);
        iaFieldID = (*env)->GetFieldID(env, iaCntrClass, "addr", "Ljava/net/InetAddress;");
        CHECK_NULL_RETURN(iaFieldID, -1);
        (*env)->SetObjectField(env, iaContainerObj, iaFieldID, iaObj);
        return 0; /* notice change from before */
    }

#ifndef   AZ_PROXIED
    /*
     * Map the Java level socket option to the platform specific
     * level and option name.
     */
    if (NET_MapSocketOption(cmd, &level, &optname)) {
        JNU_ThrowByName(env, JNU_JAVANETPKG "SocketException", "Invalid option");
        return -1;
#else  /* AZ_PROXIED */
    if (cmd == java_net_SocketOptions_SO_TIMEOUT) {
        NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException", "Error getting socket option");
        return -1;                                      // Should not be called to get socket timeout option
#endif /* AZ_PROXIED */
    }

#ifndef   AZ_PROXIED
    /*
     * Args are int except for SO_LINGER
     */
    if (cmd == java_net_SocketOptions_SO_LINGER) {
        optlen = sizeof(optval.ling);
    } else {
        optlen = sizeof(optval.i);
    }
    
    if (NET_GetSockOpt(fd, level, optname, (void *)&optval, &optlen) < 0) {
#else  /* AZ_PROXIED */

    // On the AVM side, all valid options are checked in Java code, so we can just pass them on
    // down to the proxy directly. We do, however, need to know the length of what to send ...

    uint64_t optval;

    /*
     * Args are uint64_t including SO_LINGER
     */

    proxy_error_t rc = v2dhandle_getsockopt(fd, cmd, (uint64_t *)&optval);
    if (rc != PROXY_ERROR_NONE) {
#endif /* AZ_PROXIED */
        NET_ThrowByNameWithLastError(env, JNU_JAVANETPKG "SocketException",
                                      "Error getting socket option");
        return -1;
    }

    switch (cmd) {
        case java_net_SocketOptions_SO_LINGER:
#ifndef   AZ_PROXIED
            return (optval.ling.l_onoff ? optval.ling.l_linger: -1);
#else  /* AZ_PROXIED */
	          return ((optval >> 32) == 0 ? -1 : (jint)(optval & 0xffffffff));
#endif /* AZ_PROXIED */

        case java_net_SocketOptions_SO_SNDBUF:
        case java_net_SocketOptions_SO_RCVBUF:
        case java_net_SocketOptions_IP_TOS:
#ifndef   AZ_PROXIED
            return optval.i;
#else  /* AZ_PROXIED */
            return (jint)optval;
#endif /* AZ_PROXIED */

        default :
#ifndef   AZ_PROXIED
            return (optval.i == 0) ? -1 : 1;
#else  /* AZ_PROXIED */
	    return (optval == 0) ? -1 : 1;
#endif /* AZ_PROXIED */
    }
}


/*
 * Class:     java_net_PlainSocketImpl
 * Method:    socketSendUrgentData
 * Signature: (B)V
 */
JNIEXPORT void JNICALL
Java_java_net_PlainSocketImpl_socketSendUrgentData(JNIEnv *env, jobject this,
                                             jint data) {
    char *buf;
#ifndef   AZ_PROXIED
    /* The fd field */
    jobject fdObj = (*env)->GetObjectField(env, this, psi_fdID);
#endif /* !AZ_PROXIED */
    int n, fd;
    unsigned char d = data & 0xFF;
#ifndef   AZ_PROXIED

    if (IS_NULL(fdObj)) {
#else  /* AZ_PROXIED */
    uint64_t bytes_sent;
    fd = getFD(env, this);

    if (fd < 0) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, "java/net/SocketException", "Socket closed");
        return;
#ifndef   AZ_PROXIED
    } else {
        fd = (*env)->GetIntField(env, fdObj, IO_fd_fdID);
        /* Bug 4086704 - If the Socket associated with this file descriptor
         * was closed (sysCloseFD), the the file descriptor is set to -1.
         */
        if (fd == -1) {
            JNU_ThrowByName(env, "java/net/SocketException", "Socket closed");
            return;
        }
#endif /* !AZ_PROXIED */

    }
#ifndef   AZ_PROXIED
    n = JVM_Send(fd, (char *)&d, 1, MSG_OOB);
    if (n == JVM_IO_ERR) {
#else  /* AZ_PROXIED */
    proxy_error_t rc = proxy_send(fd, (char *)&d, 1, PROXY_MSG_OOB, &bytes_sent);
    if (rc == AZPR_EIO) {
#endif /* AZ_PROXIED */
        NET_ThrowByNameWithLastError(env, "java/io/IOException", "Write failed");
        return;
    }
#ifndef   AZ_PROXIED
    if (n == JVM_IO_INTR) {
#else  /* AZ_PROXIED */
    if (rc == AZPR_EINTR) {
#endif /* AZ_PROXIED */
        JNU_ThrowByName(env, "java/io/InterruptedIOException", 0);
        return;
    }
}
