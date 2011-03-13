/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef MEMORYSERVICE_HPP
#define MEMORYSERVICE_HPP

#include "generation.hpp"
#include "memoryUsage.hpp"

// Forward declaration
class CollectedHeap;
class CompactingPermGenGen;
class ContiguousSpace;
class DefNewGeneration;
class GCMemoryManager;
class GPGC_Generation;
class GPGC_Heap;
class GenCollectedHeap;
class Generation;
class MemoryManager;
class MemoryPool;
class PSOldGen;
class PSPermGen;
class PSYoungGen;
class ParallelScavengeHeap;
class PauselessHeap;
class PermanentGenerationSpec;

// VM Monitoring and Management Support

class MemoryService : public AllStatic {
private:
  enum {
    init_pools_list_size = 10,
    init_managers_list_size = 5
  };

  // index for minor and major generations
  enum {
    minor = 0,
    major = 1,
    n_gens = 2
  };

  static GrowableArray<MemoryPool*>*    _pools_list;
  static GrowableArray<MemoryManager*>* _managers_list;

  // memory managers for minor and major GC statistics
  static GCMemoryManager*               _major_gc_manager;
  static GCMemoryManager*               _minor_gc_manager;

  // Code heap memory pool
static MemoryPool*_code_cache_pool;

  static void add_generation_memory_pool(Generation* gen, 
                                         MemoryManager* major_mgr,
                                         MemoryManager* minor_mgr);
  static void add_generation_memory_pool(Generation* gen, 
                                         MemoryManager* major_mgr) {
    add_generation_memory_pool(gen, major_mgr, NULL);
  }

  static void add_compact_perm_gen_memory_pool(CompactingPermGenGen* perm_gen,
                                               MemoryManager* mgr);

  static void add_psYoung_memory_pool(PSYoungGen* gen, 
                                      MemoryManager* major_mgr, 
                                      MemoryManager* minor_mgr);
  static void add_psOld_memory_pool(PSOldGen* gen, 
                                    MemoryManager* mgr);
  static void add_psPerm_memory_pool(PSPermGen* perm, 
                                     MemoryManager* mgr);

  static MemoryPool* add_space(ContiguousSpace* space, 
                               const char* name, 
                               bool is_heap, 
                               size_t max_size,
                               bool support_usage_threshold);
  static MemoryPool* add_survivor_spaces(DefNewGeneration* gen,
                                         const char* name, 
                                         bool is_heap, 
                                         size_t max_size,
                                         bool support_usage_threshold);
  static MemoryPool* add_gen(Generation* gen, 
                             const char* name, 
                             bool is_heap,
                             bool support_usage_threshold);

  static void add_gen_collected_heap_info(GenCollectedHeap* heap);
  static void add_parallel_scavenge_heap_info(ParallelScavengeHeap* heap);
  
  // GenPauselessGC support
  static void add_gen_pauseless_memory_pool(GPGC_Generation* gen,
                                            const char* name,
                                            MemoryManager* major_mgr,
                                            MemoryManager* minor_mgr);

  static void add_gen_pauseless_heap_info(GPGC_Heap* heap);

public:
  static void set_universe_heap(CollectedHeap* heap);
  static void add_code_cache_memory_pool();

  static MemoryPool*    get_memory_pool(instanceHandle pool);
  static MemoryManager* get_memory_manager(instanceHandle mgr);

  static const int num_memory_pools() {
    return _pools_list->length();
  }
  static const int num_memory_managers() {
    return _managers_list->length();
  }

  static MemoryPool* get_memory_pool(int index) {
    return _pools_list->at(index);
  }

  static MemoryManager* get_memory_manager(int index) {
    return _managers_list->at(index);
  }

  static void track_memory_usage();
  static void track_code_cache_memory_usage() {
track_memory_pool_usage(_code_cache_pool);
  }
  static void track_memory_pool_usage(MemoryPool* pool);

  static void gc_begin(bool fullGC);
  static void gc_end(bool fullGC);

  static void oops_do(OopClosure* f);

  static bool get_verbose() { return PrintGC; }
  static bool set_verbose(bool verbose);

  // Create an instance of java/lang/management/MemoryUsage
  static Handle create_MemoryUsage_obj(MemoryUsage usage, TRAPS);
};

class TraceMemoryManagerStats : public StackObj {
private:
  bool         _fullGC;
public:
  TraceMemoryManagerStats(bool fullGC);
  TraceMemoryManagerStats(Generation::Name kind);
  ~TraceMemoryManagerStats();
};

#endif // MEMORYSERVICE_HPP

