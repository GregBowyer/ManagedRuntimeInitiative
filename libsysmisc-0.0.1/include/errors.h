// Copyright 2003 Azul Systems, Inc. All rights reserved.
// AZUL Systems PROPRIETARY/CONFIDENTIAL. Use is subject to license terms
//
// errors.h - Error codes for Aztek OS Services

#ifndef _OS_ERRORS_H_
#define _OS_ERRORS_H_ 1

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYSERR_NONE               0
#define	SYSERR_INVALID_HANDLE     EBADF
#define	SYSERR_STALE_HANDLE       EBADF
#define	SYSERR_INVALID_ADDRESS    EFAULT
#define	SYSERR_PROTECTION_FAILURE EFAULT
#define	SYSERR_INVALID_ARGUMENT   EINVAL
#define	SYSERR_RESOURCE_LIMIT     ENOMEM
#define	SYSERR_NO_PRIVILEGE       EACCES
#define	SYSERR_TIMED_OUT          ETIMEDOUT
#define	SYSERR_TRY_AGAIN          EAGAIN
#define	SYSERR_INVALID_OPERATION  EOPNOTSUPP
#define	SYSERR_UNIMPLEMENTED      ENOSYS
#define	SYSERR_NO_MEMORY          ENOMEM
#define	SYSERR_INVALID_STATE      EINVAL
#define	SYSERR_NOT_FOUND          ENOENT
#define	SYSERR_NO_KERNEL_MEMORY   ENOMEM
#define	SYSERR_EXISTS             EEXIST
#define	SYSERR_KERNEL_LIMIT       ENOMEM
#define SYSERR_RECALLED           EAGAIN

typedef int sys_return_t;

// The mapping from error code to error string will be provided by libos.
extern const char *error_message(sys_return_t code);

// Print out the error string corresponding to the error code along with the caller-provided message.
extern void error_print(const char *msg, sys_return_t code);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // _OS_ERRORS_H_
