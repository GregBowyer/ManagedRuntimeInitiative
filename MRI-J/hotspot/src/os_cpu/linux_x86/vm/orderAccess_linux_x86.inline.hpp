/*
 * Copyright 2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef ORDERACCESS_OS_PD_INLINE_HPP
#define ORDERACCESS_OS_PD_INLINE_HPP


// Implementation of class OrderAccess.

inline void OrderAccess::loadload()   { acquire(); }
inline void OrderAccess::storestore() { release(); }
inline void OrderAccess::loadstore()  { acquire(); }
inline void OrderAccess::storeload(){release();}

inline void OrderAccess::acquire() {
  // Strong ordering on X86 makes ordering cheap
  volatile intptr_t dummy;
  __asm__ volatile ("movq 0(%%rsp), %0" : "=r" (dummy) : : "memory");
}

inline void OrderAccess::release() {
  dummy = 0;
}

inline void OrderAccess::fence() {
  if (os::is_MP()) {
    __asm__ __volatile__ ("mfence":::"memory");
  }
}

inline jbyte    OrderAccess::load_acquire(volatile jbyte*   p) { jbyte   v = *p; acquire(); return v; }
inline jshort   OrderAccess::load_acquire(volatile jshort*  p) { jshort  v = *p; acquire(); return v; }
inline jint     OrderAccess::load_acquire(volatile jint*    p) { jint    v = *p; acquire(); return v; }
inline jlong    OrderAccess::load_acquire(volatile jlong*   p) { jlong   v = *p; acquire(); return v; }
inline jubyte   OrderAccess::load_acquire(volatile jubyte*  p) { jubyte  v = *p; acquire(); return v; }
inline jushort  OrderAccess::load_acquire(volatile jushort* p) { jushort v = *p; acquire(); return v; }
inline juint    OrderAccess::load_acquire(volatile juint*   p) { juint   v = *p; acquire(); return v; }
inline julong   OrderAccess::load_acquire(volatile julong*  p) { julong  v = *p; acquire(); return v; }
inline jfloat   OrderAccess::load_acquire(volatile jfloat*  p) { jfloat  v = *p; acquire(); return v; }
inline jdouble  OrderAccess::load_acquire(volatile jdouble* p) { jdouble v = *p; acquire(); return v; }

inline intptr_t OrderAccess::load_ptr_acquire(volatile intptr_t*   p) { intptr_t v = *p; acquire(); return v; }
inline void*    OrderAccess::load_ptr_acquire(volatile void*       p) { void* v = *(void* volatile *)p; acquire(); return v; }
inline void*    OrderAccess::load_ptr_acquire(const volatile void* p) { void* v = *(void* const volatile *)p; acquire(); return v; }

inline void     OrderAccess::release_store(volatile jbyte*   p, jbyte   v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jshort*  p, jshort  v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jint*    p, jint    v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jlong*   p, jlong   v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jubyte*  p, jubyte  v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jushort* p, jushort v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile juint*   p, juint   v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile julong*  p, julong  v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jfloat*  p, jfloat  v) { release(); *p = v; }
inline void     OrderAccess::release_store(volatile jdouble* p, jdouble v) { release(); *p = v; }

inline void     OrderAccess::release_store_ptr(volatile intptr_t* p, intptr_t v) { release(); *p = v; }
inline void     OrderAccess::release_store_ptr(volatile void*     p, void*    v) { release(); *(void* volatile *)p = v; }

inline void OrderAccess::store_fence(jbyte*p,jbyte v){*p=v;fence();}
inline void     OrderAccess::store_fence(jshort*  p, jshort  v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jint*    p, jint    v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jlong*   p, jlong   v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jubyte*  p, jubyte  v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jushort* p, jushort v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(juint*   p, juint   v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(julong*  p, julong  v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jfloat*  p, jfloat  v) { *p = v; fence(); }
inline void     OrderAccess::store_fence(jdouble* p, jdouble v) { *p = v; fence(); }

inline void     OrderAccess::store_ptr_fence(intptr_t* p, intptr_t v) { *p = v; fence(); }
inline void     OrderAccess::store_ptr_fence(void**    p, void*    v) { *p = v; fence(); }

inline void     OrderAccess::release_store_fence(volatile jbyte*   p, jbyte   v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jshort*  p, jshort  v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jint*    p, jint    v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jlong*   p, jlong   v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jubyte*  p, jubyte  v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jushort* p, jushort v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile juint*   p, juint   v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile julong*  p, julong  v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jfloat*  p, jfloat  v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_fence(volatile jdouble* p, jdouble v) { release(); *p = v; fence(); }

inline void     OrderAccess::release_store_ptr_fence(volatile intptr_t* p, intptr_t v) { release(); *p = v; fence(); }
inline void     OrderAccess::release_store_ptr_fence(volatile void*     p, void*    v) { release(); *(void* volatile *)p = v; fence(); }

#endif // ORDERACCESS_OS_PD_INLINE_HPP
