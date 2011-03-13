/*
 * Copyright (c) 2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "psMemoryPool.hpp"
#include "psPermGen.hpp"

PSGenerationPool::PSGenerationPool(PSOldGen* gen, 
                                   const char* name, 
                                   PoolType type,
                                   bool support_usage_threshold) :
  CollectedMemoryPool(name, type, gen->capacity_in_bytes(), 
                      gen->reserved().byte_size(), support_usage_threshold), _gen(gen) {
}

PSGenerationPool::PSGenerationPool(PSPermGen* gen, 
                                   const char* name, 
                                   PoolType type,
                                   bool support_usage_threshold) :
  CollectedMemoryPool(name, type, gen->capacity_in_bytes(), 
                      gen->reserved().byte_size(), support_usage_threshold), _gen(gen) {
}

MemoryUsage PSGenerationPool::get_memory_usage() {
  size_t maxSize   = (available_for_allocation() ? max_size() : 0);
  size_t used      = used_in_bytes();
  size_t committed = _gen->capacity_in_bytes();

  return MemoryUsage(initial_size(), used, committed, maxSize);
}

// The max size of EdenMutableSpacePool =
//     max size of the PSYoungGen - capacity of two survivor spaces
//
// Max size of PS eden space is changing due to ergonomic.
// PSYoungGen, PSOldGen, Eden, Survivor spaces are all resizable.
//
EdenMutableSpacePool::EdenMutableSpacePool(PSYoungGen* gen,
                                           MutableSpace* space,
                                           const char* name, 
                                           PoolType type,
                                           bool support_usage_threshold) :
  CollectedMemoryPool(name, type, space->capacity_in_bytes(), 
                      (gen->max_size() - gen->from_space()->capacity_in_bytes() - gen->to_space()->capacity_in_bytes()), 
                       support_usage_threshold), 
  _gen(gen), _space(space) {
}

MemoryUsage EdenMutableSpacePool::get_memory_usage() {
  size_t maxSize   = (available_for_allocation() ? max_size() : 0);
  size_t used = used_in_bytes();
  size_t committed = _space->capacity_in_bytes();

  return MemoryUsage(initial_size(), used, committed, maxSize);
}

// The max size of SurvivorMutableSpacePool =
//     current capacity of the from-space
//
// PS from and to survivor spaces could have different sizes.
//
SurvivorMutableSpacePool::SurvivorMutableSpacePool(PSYoungGen* gen,
                                                   const char* name, 
                                                   PoolType type,
                                                   bool support_usage_threshold) :
  CollectedMemoryPool(name, type, gen->from_space()->capacity_in_bytes(),
                      gen->from_space()->capacity_in_bytes(),
                      support_usage_threshold), _gen(gen) {
}

MemoryUsage SurvivorMutableSpacePool::get_memory_usage() {
  size_t maxSize = (available_for_allocation() ? max_size() : 0);
  size_t used    = used_in_bytes();
  size_t committed = committed_in_bytes();
  return MemoryUsage(initial_size(), used, committed, maxSize);
}
