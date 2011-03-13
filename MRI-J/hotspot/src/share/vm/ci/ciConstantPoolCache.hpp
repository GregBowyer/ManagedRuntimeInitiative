/*
 * Copyright 1999-2000 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CICONSTANTPOOLCACHE_HPP
#define CICONSTANTPOOLCACHE_HPP


#include "allocation.hpp"
#include "ciUtilities.hpp"
#include "resourceArea.hpp"
#include "freezeAndMelt.hpp"

// ciConstantPoolCache
//
// The class caches indexed constant pool lookups.
//
// Usage note: this klass has nothing to do with constantPoolCacheOop.
class ciConstantPoolCache : public ResourceObj {
private:
  GrowableArray<intptr_t>*   _keys;
  GrowableArray<void*>* _elements;

  int find(int index);

public:
  ciConstantPoolCache(Arena* arena, int expected_size);
  ciConstantPoolCache(Arena* arena, FAMPtr old_cicpc);

  // Get the element associated with some index.
  void* get(int index);
	
  // Associate an element with an index.
  void insert(int index, void* element);

  void print();
};

#endif // CICONSTANTPOOLCACHE_HPP
