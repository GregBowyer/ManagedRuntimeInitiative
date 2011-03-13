/*
 * Copyright 2003-2004 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef COPY_PD_HPP
#define COPY_PD_HPP


// Inline functions for memory copy and fill.

static inline void pd_conjoint_words(HeapWord* from, HeapWord* to, size_t count) {
  (void)memmove(to, from, count * HeapWordSize);
}

static inline void pd_disjoint_words(HeapWord* from, HeapWord* to, size_t count) {
  (void)memcpy(to, from, count * HeapWordSize);
}

static inline void pd_disjoint_words_atomic(HeapWord* from, HeapWord* to, size_t count) {
  (void)memcpy(to, from, count * HeapWordSize);
}

static inline void pd_aligned_conjoint_words(HeapWord* from, HeapWord* to, size_t count) {
  (void)memmove(to, from, count * HeapWordSize);
}

static inline void pd_aligned_disjoint_words(HeapWord* from, HeapWord* to, size_t count) {
(void)memcpy(to,from,count*HeapWordSize);
}

static inline void pd_conjoint_bytes(void* from, void* to, size_t count) {
(void)memmove(to,from,count);
}

static inline void pd_conjoint_bytes_atomic(void* from, void* to, size_t count) {
(void)memmove(to,from,count);
}

static inline void pd_conjoint_jshorts_atomic(jshort* from, jshort* to, size_t count) {
(void)memmove(to,from,count*BytesPerShort);
}

static inline void pd_conjoint_jints_atomic(jint* from, jint* to, size_t count) {
(void)memmove(to,from,count*BytesPerInt);
}

static inline void pd_conjoint_jlongs_atomic(jlong* from, jlong* to, size_t count) {
(void)memmove(to,from,count*BytesPerLong);
}

static inline void pd_arrayof_conjoint_bytes(HeapWord* from, HeapWord* to, size_t count) {
(void)memmove(to,from,count);
}

static inline void pd_arrayof_conjoint_jshorts(HeapWord* from, HeapWord* to, size_t count) {
(void)memmove(to,from,count*BytesPerShort);
}

static inline void pd_arrayof_conjoint_jints(HeapWord* from, HeapWord* to, size_t count) {
(void)memmove(to,from,count*BytesPerInt);
}

static void pd_arrayof_conjoint_jlongs(HeapWord*from,HeapWord*to,size_t count){
(void)memmove(to,from,count*BytesPerLong);
}

static void pd_fill_to_words(HeapWord* tohw, size_t count, jlong value) {
  // Azul special for fast word fills
for(uint i=0;i<count;i++)
    ((jlong*)tohw)[i]= (jlong)value;
}

static inline void pd_fill_to_aligned_words(HeapWord* tohw, size_t count, jlong value) {
  pd_fill_to_words(tohw, count, value);
}

static inline void pd_fill_to_bytes(void* to, size_t count, jubyte value) {
  memset(to, value, count);
}

static inline void pd_zero_to_words(HeapWord* tohw, size_t count) {
memset(tohw,0,count*HeapWordSize);
}

static inline void pd_zero_to_bytes(void* to, size_t count) {
  memset(to, 0, count);
}

static void pd_conjoint_objectRefs_atomic(objectRef*from,objectRef*to,size_t count){
  assert0( (UseGenPauselessGC && GPGCNoGC) || (!UseGenPauselessGC) );
  memmove(to,from,count<<LogHeapWordSize);
}

#endif // COPY_PD_HPP
