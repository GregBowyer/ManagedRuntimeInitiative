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

// lock.h - Azul locks

#ifndef _OS_LOCK_H_
#define _OS_LOCK_H_ 1

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OS_SPIN_LOCK_T                  pthread_mutex_t
#define OS_SPIN_LOCK_INITIALIZER        PTHREAD_MUTEX_INITIALIZER

#define OS_SPIN_LOCK_INIT(this_lock)    pthread_mutex_init(this_lock, NULL)
#define OS_SPIN_TRYLOCK(this_lock)      pthread_mutex_trylock(this_lock)
#define OS_SPIN_LOCK(this_lock)         pthread_mutex_lock(this_lock)
#define OS_SPIN_UNLOCK(this_lock)       pthread_mutex_unlock(this_lock)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _OS_LOCK_H_
