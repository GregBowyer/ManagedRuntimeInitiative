/*
 * Copyright 1999-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef HPI_OS_HPP
#define HPI_OS_HPP

#include "mutexLocker.hpp"
#include "ostream.hpp"
//
// Parts of the HPI interface for which the HotSparc does not use the
// HPI (because the interruptible IO mechanisms used are different on Solaris).
//
// AZUL NOTE: We decided to deal with interruptible I/O somewhat differently.
// For Java threads to be interruptible we need to eject them from the blocking
// call and set appropriate flags.
//

#include <netdb.h>
#include <sys/socket.h>

#ifdef AZ_PROXIED
#include <proxy/v2dhandle.h>
#include <proxy/proxy_compat.h>
#else // !AZ_PROXIED
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <poll.h>
#endif // !AZ_PROXIED

extern "C" {
#ifndef AZ_PROXIED
  int open(const char *path, int flags, mode_t mode);
#define O_DELETE 0x10000
  static        int sysOpen(const char *path, int flags, mode_t mode) { 
    const int del = (flags & O_DELETE);
flags&=~O_DELETE;
    int fd = open(path, flags, mode);
    if (del != 0) unlink(path);
    return fd;
  }
#endif
  int fsize(int fd, ssize_t *size);
  int socket_close(int fd);
  int socket_shutdown(int fd, int howto);
  int socket_timeout(int fd, long timeout);
  int socket_available(int fd, jint *bytes);
};

#define HPIDECL(name, names, func, ret_type, ret_fmt, arg_type, arg_print, arg, log) \
  inline ret_type hpi::name arg_type {        \
    ret_type result = ::func arg ;            \
    if (TraceHPI || log) {                    \
      MutexLocker ml(ThreadCritical_lock);    \
      tty->print("hpi::" names "(");          \
      tty->print arg_print ;                  \
      tty->print(") = ");                     \
      tty->print_cr(ret_fmt, result);         \
    }                                         \
    return result;                            \
}

#define HPIDECL_RET(name, names, func, ret_type, ret_fmt, arg_type, arg_print, arg, ret_stmt, log) \
  inline ret_type hpi::name arg_type {        \
    ret_type result = ::func arg ;            \
    if (TraceHPI || log) {                    \
      MutexLocker ml(ThreadCritical_lock);    \
      tty->print("hpi::" names "(");          \
      tty->print arg_print ;                  \
      tty->print(") = ");                     \
      tty->print_cr(ret_fmt, result);         \
    }                                         \
    ret_stmt;                                 \
}

#define HPIDECL_VOID(name, names, func, arg_type, arg_print, arg, log) \
  inline void hpi::name arg_type {            \
    if (TraceHPI || log) {                    \
      tty->print("hpi::" names "(");          \
      tty->print arg_print ;                  \
      tty->print_cr(") = void");              \
    }                                         \
    ::func arg ;                              \
}

#define HPISOCKET_DECL_SOCKLEN(name, names, func, ret_type, ret_fmt, arg_type, arg_print, arg, log) \
  ret_type hpi::name arg_type {               \
    socklen_t socklen = *retlen;              \
    ret_type result = ::func arg ;            \
    *retlen = (int)socklen;                   \
    if (TraceHPI || log) {                    \
      MutexLocker ml(ThreadCritical_lock);    \
      tty->print("hpi::" names "(");          \
      tty->print arg_print ;                  \
      tty->print(") = ");                     \
      tty->print_cr(ret_fmt, result);         \
    }                                         \
    return result;                            \
}


#ifdef AZ_PROXIED
# define HPI_NATIVEPATH_FUNC proxy_nativepath
# define HPI_FILETYPE_FUNC   proxy_filetype
# define HPI_LASTERROR_FUNC  proxy_lasterror
#define HPI_OPEN_FUNC open
#else
# define HPI_NATIVEPATH_FUNC unproxied_nativepath
# define HPI_FILETYPE_FUNC   unproxied_filetype
# define HPI_LASTERROR_FUNC  unproxied_lasterror
# define HPI_OPEN_FUNC       sysOpen

extern "C" {
  extern char *unproxied_nativepath(char *path);
  extern int   unproxied_filetype  (const char *path);
  extern int   unproxied_lasterror (char *buf, int len);
};

#endif // AZ_PROXIED


HPIDECL(native_path, "native_path", HPI_NATIVEPATH_FUNC, char *, "%s",
        (char *path),
        ("path = %s", path),
        (path),
        0);

HPIDECL(file_type, "file_type", HPI_FILETYPE_FUNC, int, "%d",
        (const char *path),
        ("path = %s", path),
        (path),
        0);

HPIDECL(open, "open", HPI_OPEN_FUNC, int, "%d",
        (const char *path, int flags, int mode),
        ("path = %s, flags = %d, mode = %d", path, flags, mode),
        (path, flags, mode),
        0);

HPIDECL(close, "close", close, int, "%d",
        (int fd),
        ("fd = %d", fd),
        (fd),
        0);

HPIDECL(lseek, "lseek", lseek64, jlong, "%lld",
        (int fd, jlong off, int whence),
        ("fd = %d, off = %lld, whence = %d", fd, off, whence),
        (fd, off, whence),
        0);

HPIDECL(ftruncate, "ftruncate", ftruncate64, int, "%d",
        (int fd, jlong length),
        ("fd = %d, length = (a jlong)", fd /*, length */),
        (fd, length),
        0);

HPIDECL(fsync, "fsync", fsync, int, "%d",
        (int fd),
        ("fd = %d", fd),
        (fd),
        0);

HPIDECL(fsize, "fsize", fsize, int, "%d",
        (int fd, jlong *size),
        ("fd = %d, size = %p", fd, size),
        (fd, (ssize_t *)size),
        0);

#ifndef AZ_PROXIED
#undef sockaddr
#endif

// HPI_SocketInterface
#ifdef AZ_PROXIED
HPIDECL(socket_close, "socket_close", socket_close, int, "%d",
        (int fd),
        ("fd = %d", fd),
        (fd),
        0);

HPIDECL(socket_available, "socket_available", socket_available, int, "%d",
        (int fd, jint *pbytes),
        ("fd = %d, pbytes = %p", fd, pbytes),
        (fd, pbytes),
        0);
#else // !AZ_PROXIED
HPIDECL(socket_close, "socket_close", close, int, "%d",
        (int fd),
        ("fd = %d", fd),
        (fd),
        0);

// socket_available
// Linux doc says EINTR not returned, unlike Solaris
//%% note ioctl can return 0 when successful, JVM_SocketAvailable
// is expected to return 0 on failure and 1 on success to the jdk.
HPIDECL_RET(socket_available, "socket_available", ioctl, int, "%d",
            (int fd, jint *pbytes),
("fd = %d, FIONREAD, pbytes = %p",fd,pbytes),
            (fd, FIONREAD, pbytes),
            return (result < 0) ? 0 : 1,
            0);
#endif // !AZ_PROXIED

HPIDECL(socket, "socket", socket, int, "%d",
        (int domain, int type, int protocol),
        ("domain = %d, type = %d, protocol = %d", domain, type, protocol),
        (domain, type, protocol),
        0);

HPIDECL(listen, "listen", listen, int, "%d",
        (int fd, int count),
        ("fd = %d, count = %d", fd, count),
        (fd, count),
        0);

HPIDECL(connect, "connect", connect, int, "%d",
        (int fd,struct sockaddr *him, int len),
        ("fd = %d, him = %p, len = %d", fd, him, len),
        (fd, him, (socklen_t)len),
        0);

HPISOCKET_DECL_SOCKLEN(accept, "accept", accept, int, "%d",
(int fd,struct sockaddr*him,int*retlen),
("fd = %d, him = %p, len = %p",fd,him,retlen),
        (fd, him, &socklen),
        0);

HPIDECL(sendto, "sendto", sendto, int, "%d",
        (int fd, char *buf, int len, int flags,
        struct sockaddr *to, int tolen),
        ("fd = %d, buf = %p, len = %d, flags = %d, to = %p, tolen = %d", fd, buf, len, flags, to, tolen),
        (fd, buf, len, flags, to, (socklen_t)tolen),
        0);

HPISOCKET_DECL_SOCKLEN(recvfrom, "recvfrom", recvfrom, int, "%d",
        (int fd, char *buf, int nbytes, int flags,
struct sockaddr*from,int*retlen),
        ("fd = %d, buf = %p, len = %d, flags = %d, frm = %p, frmlen = %d", fd, buf, nbytes, flags, from, *retlen),
        (fd, buf, nbytes, flags, from, &socklen),
        0);

HPIDECL(recv, "recv", recv, int, "%d",
        (int fd, char *buf, int nBytes, int flags),
        ("fd = %d, buf = %p, nBytes = %d, flags = %d", fd, buf, nBytes, flags),
        (fd, buf, nBytes, flags),
        0);

HPIDECL(send, "send", send, int, "%d",
        (int fd, char *buf, int nBytes, int flags),
        ("fd = %d, buf = %p, nBytes = %d, flags = %d", fd, buf, nBytes, flags),
        (fd, buf, nBytes, flags),
        0);

#ifdef AZ_PROXIED
HPIDECL(timeout, "timeout", socket_timeout, int, "%d",
        (int fd, long timeout),
        ("fd = %d, timeout = %ld", fd, timeout),
        (fd, timeout),
        0);

HPIDECL(socket_shutdown, "socket_shutdown", socket_shutdown, int, "%d",
        (int fd, int howto),
        ("fd = %d, howto = %d", fd, howto),
        (fd, howto),
        0);
#else // !AZ_PROXIED
inline int hpi::timeout(int fd, long timeout) {
  julong prevtime, newtime;
  struct timeval t;

  gettimeofday(&t, NULL);
  prevtime = ((julong)t.tv_sec * 1000)  +  t.tv_usec / 1000;

  for (;;) {
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN | POLLERR;
  
    int res = ::poll(&pfd, 1, timeout);

    if (res == OS_ERR && errno == EINTR) {

      // On Linux any value < 0 means "forever"

      if (timeout >= 0) {
        gettimeofday(&t, NULL);
        newtime = ((julong)t.tv_sec * 1000)  +  t.tv_usec / 1000;
        timeout -= newtime - prevtime;
if(timeout<=0){
if(TraceHPI){
MutexLocker ml(ThreadCritical_lock);
            tty->print_cr("hpi::timeout(fd = %d, timeout = %ld) = %d", fd, timeout, OS_OK);
          }
          return OS_OK;
        }
        prevtime = newtime;
      }
    } else {
      if (TraceHPI) {
MutexLocker ml(ThreadCritical_lock);
        tty->print_cr("hpi::timeout(fd = %d, timeout = %ld) = %d", fd, timeout, res);
      }
      return res;
    }
  }
}

HPIDECL(socket_shutdown, "socket_shutdown", shutdown, int, "%d",
        (int fd, int howto),
        ("fd = %d, howto = %d", fd, howto),
        (fd, howto),
        0);
#endif // !AZ_PROXIED

HPIDECL(bind, "bind", bind, int, "%d",
        (int fd,struct sockaddr *him, int len),
        ("fd = %d, him = %p, len = %d", fd, him, len),
        (fd, him, (socklen_t)len),
        0);

HPISOCKET_DECL_SOCKLEN(get_sock_name, "get_sock_name", getsockname, int, "%d",
(int fd,struct sockaddr*him,int*retlen),
("fd = %d, him = %p, len = %p",fd,him,retlen),
        (fd, him, &socklen),
        0);

HPIDECL(get_host_name, "get_host_name", gethostname, int, "%d",
        (char *hostname, int namelen),
        ("hostname = %p, namelen = %d", hostname, namelen),
        (hostname, namelen),
        0);

HPISOCKET_DECL_SOCKLEN(get_sock_opt, "get_sock_opt", getsockopt, int, "%d",
(int fd,int level,int optname,char*optval,int*retlen),
        ("fd = %d, level = %d, optname = %d, optval = %p, optlen = %p", fd, level, optname, optval, retlen),
        (fd, level, optname, optval, &socklen),
        0);

HPIDECL(set_sock_opt, "set_sock_opt", setsockopt, int, "%d",
        (int fd, int level, int optname, const char *optval, int optlen),
        ("fd = %d, level = %d, optname = %d, optval = %p, optlen = %d", fd, level, optname, optval, optlen),
        (fd, level, optname, optval, (socklen_t)optlen),
        0);

#ifdef _WINDOWS
// Azul change: the below three functions are used only on Windows, but therefore need to exist on the AVM
HPIDECL(get_host_by_addr, "get_host_by_addr", gethostbyaddr, /* struct hostent * */ void *, "(/* struct hostent * */ void *)%p",
        (const char* name, int len, int type),
        ("name = %p, len = %d, type = %d", name, len, type),
        (name, len, type),
        0);

HPIDECL(get_host_by_name, "get_host_by_name", gethostbyname, /* struct hostent * */ void *, "(/* struct hostent * */ void *)%p",
        (char *name),
        ("%s", name),
        (name),
        0);

HPIDECL(get_proto_by_name, "get_proto_by_name", getprotobyname, /* struct protoent * */ void *, "(struct protoent *)%p",
        (char* name),
("name = %p",name),
        (name),
        0);
#endif // _WINDOWS

// HPI_LibraryInterface is not inline

// HPI_SystemInterface
HPIDECL(lasterror, "lasterror", HPI_LASTERROR_FUNC, int, "%d",
        (char *buf, int len),
        ("buf = %p, len = %d", buf, len),
        (buf, len),
        0);

#endif // HPI_OS_HPP

