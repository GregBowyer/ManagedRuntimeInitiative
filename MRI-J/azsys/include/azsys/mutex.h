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

// mutex.h - Azul system services mutex and condition variable functions.

#ifndef _OS_MUTEX_H_
#define _OS_MUTEX_H_ 1

#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <os/types.h>
#include <os/errors.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timespec timespec_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t condition_t;

#define MUTEX_TRUE	1
#define MUTEX_FALSE	0

static inline sys_return_t mutex_init(mutex_t *this_mutex)
{ 
    return (sys_return_t)pthread_mutex_init(this_mutex, NULL);
}

static inline sys_return_t mutex_destroy(mutex_t *this_mutex)
{
    return (sys_return_t)pthread_mutex_destroy(this_mutex);
}

// mutex_trylock() returns boolean in aztek.
static inline uint64_t mutex_trylock(mutex_t *this_mutex)
{
    return (pthread_mutex_trylock(this_mutex) == 0 ? MUTEX_TRUE : MUTEX_FALSE);
}

static inline sys_return_t mutex_lock(mutex_t *this_mutex)
{
    return (sys_return_t)pthread_mutex_lock(this_mutex);
}

static inline sys_return_t mutex_unlock(mutex_t *this_mutex)
{
    return (sys_return_t)pthread_mutex_unlock(this_mutex);
}

static inline sys_return_t condition_init(condition_t *this_cond)
{
    return (sys_return_t)pthread_cond_init(this_cond, NULL);
}
static inline sys_return_t condition_destroy(condition_t *this_cond)
{
    return (sys_return_t)pthread_cond_destroy(this_cond);
}
static inline sys_return_t condition_wait(condition_t *this_cond, mutex_t *this_mutex)
{
    return (sys_return_t)pthread_cond_wait(this_cond, this_mutex);
}
static inline sys_return_t condition_signal(condition_t *this_cond)
{
    return (sys_return_t)pthread_cond_signal(this_cond);
}
static inline sys_return_t condition_broadcast(condition_t *this_cond)
{
    return (sys_return_t)pthread_cond_broadcast(this_cond);
}

extern sys_return_t condition_timedwait(condition_t *cv, mutex_t *m, time_interval_t ns);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_MUTEX_H_
