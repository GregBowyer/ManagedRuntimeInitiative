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

// mutex.c - Azul system services mutex and condition variable functions.

#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

#include <os/types.h>
#include <os/errors.h>
#include <os/mutex.h>

static timespec_t* compute_abstime(timespec_t* abstime, time_interval_t millis) {
    struct timeval now;
    int status;
    time_interval_t seconds;
    time_interval_t max_wait;

    if (millis < 0) {
        millis = 0;
    }
    status = gettimeofday(&now, NULL);
    seconds = millis / 1000;

    max_wait = 50000000;
    millis %= 1000;
    if (seconds > max_wait) {
        seconds = max_wait;
    }
    abstime->tv_sec = now.tv_sec  + seconds;
    long       usec = now.tv_usec + millis * 1000;
    if (usec >= 1000000) {
        abstime->tv_sec += 1;
        usec -= 1000000;
    }
    abstime->tv_nsec = usec * 1000;
    return abstime;
}

sys_return_t
condition_timedwait(condition_t *cv, mutex_t *m, time_interval_t ns)
{
    int status;
    timespec_t abstime;

    status = pthread_cond_timedwait(cv, m, compute_abstime(&abstime, (ns / 1000000)));
    if (status == 0) {
        return SYSERR_NONE;
    } else {
        if (status == ETIMEDOUT) {
            return SYSERR_TIMED_OUT;
        }
        return (sys_return_t)status;
    }
}
