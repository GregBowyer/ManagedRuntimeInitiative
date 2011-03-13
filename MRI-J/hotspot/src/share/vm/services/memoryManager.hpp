/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef MEMORYMANAGER_HPP
#define MEMORYMANAGER_HPP


#include "memoryUsage.hpp"
#include "instanceKlass.hpp"
#include "timer.hpp"

// A memory manager is responsible for managing one or more memory pools.
// The garbage collector is one type of memory managers responsible
// for reclaiming memory occupied by unreachable objects.  A Java virtual
// machine may have one or more memory managers.   It may
// add or remove memory managers during execution.
// A memory pool can be managed by more than one memory managers.

class MemoryPool;
class GCMemoryManager;
class OopClosure;

class MemoryManager : public CHeapObj {
private:
  enum {
    max_num_pools = 10
  };

  MemoryPool* _pools[max_num_pools];
  int         _num_pools;

protected:
  volatile instanceRef _memory_mgr_obj;

public:
  enum Name {
    Abstract,
    CodeCache,
    Copy,
    MarkSweepCompact,
    PGCScavenge,
    PGCFull,
    GPGCNew,
    GPGCOld,
    PSScavenge,
    PSMarkSweep   
  };

  MemoryManager();

  int num_memory_pools() const           { return _num_pools; }
  MemoryPool* get_memory_pool(int index) {
    assert(index >= 0 && index < _num_pools, "Invalid index");
    return _pools[index];
  }

  void add_pool(MemoryPool* pool);

  bool is_manager(instanceHandle mh)     { return mh() == lvb_instanceRef((instanceRef*)&_memory_mgr_obj).as_instanceOop(); }

  virtual instanceOop get_memory_manager_instance(TRAPS);
  virtual MemoryManager::Name kind()     { return MemoryManager::Abstract; }
  virtual bool is_gc_memory_manager()    { return false; }
  virtual const char* name() = 0;

  // GC support
  void oops_do(OopClosure* f);

  // Static factory methods to get a memory manager of a specific type
  static MemoryManager*   get_code_cache_memory_manager();
  static GCMemoryManager* get_copy_memory_manager();
  static GCMemoryManager* get_msc_memory_manager();
static GCMemoryManager*get_pgcScavenge_memory_manager();
static GCMemoryManager*get_pgcFull_memory_manager();
static GCMemoryManager*get_gpgc_newgc_memory_manager();
static GCMemoryManager*get_gpgc_oldgc_memory_manager();
  static GCMemoryManager* get_psScavenge_memory_manager();
  static GCMemoryManager* get_psMarkSweep_memory_manager();

};

class CodeCacheMemoryManager : public MemoryManager {
private:
public:
  CodeCacheMemoryManager() : MemoryManager() {}

  MemoryManager::Name kind() { return MemoryManager::CodeCache; }
  const char* name()         { return "CodeCacheManager"; }
};

class GCStatInfo : public CHeapObj {
private:
  size_t _index;
  jlong  _start_time;
  jlong  _end_time;

  // We keep memory usage of all memory pools
  MemoryUsage* _before_gc_usage_array;
  MemoryUsage* _after_gc_usage_array;
  int          _usage_array_size;

  void set_gc_usage(int pool_index, MemoryUsage, bool before_gc);

public:
  GCStatInfo(int num_pools);
  ~GCStatInfo();

  size_t gc_index()               { return _index; }
  jlong  start_time()             { return _start_time; }
  jlong  end_time()               { return _end_time; }
  int    usage_array_size()       { return _usage_array_size; }
  MemoryUsage before_gc_usage_for_pool(int pool_index) {
    assert(pool_index >= 0 && pool_index < _usage_array_size, "Range checking");
    return _before_gc_usage_array[pool_index];
  }
  MemoryUsage after_gc_usage_for_pool(int pool_index) {
    assert(pool_index >= 0 && pool_index < _usage_array_size, "Range checking");
    return _after_gc_usage_array[pool_index];
  }

  void set_index(size_t index)    { _index = index; }
  void set_start_time(jlong time) { _start_time = time; }
  void set_end_time(jlong time)   { _end_time = time; }
  void set_before_gc_usage(int pool_index, MemoryUsage usage) {
    assert(pool_index >= 0 && pool_index < _usage_array_size, "Range checking");
    set_gc_usage(pool_index, usage, true /* before gc */);
  }
  void set_after_gc_usage(int pool_index, MemoryUsage usage) {
    assert(pool_index >= 0 && pool_index < _usage_array_size, "Range checking");
    set_gc_usage(pool_index, usage, false /* after gc */);
  }

  void copy_stat(GCStatInfo* stat);
};

class GCMemoryManager : public MemoryManager {
private:
  // TODO: We should unify the GCCounter and GCMemoryManager statistic
  size_t       _num_collections;
  elapsedTimer _accumulated_timer;
  elapsedTimer _gc_timer;         // for measuring every GC duration
  GCStatInfo*  _last_gc_stat;
  int          _num_gc_threads;
public:
  GCMemoryManager();
  ~GCMemoryManager();

  void   initialize_gc_stat_info();

  bool   is_gc_memory_manager()         { return true; }
  jlong  gc_time_ms()                   { return _accumulated_timer.milliseconds(); }
  size_t gc_count()                     { return _num_collections; }
  int    num_gc_threads()               { return _num_gc_threads; }
  void   set_num_gc_threads(int count)  { _num_gc_threads = count; }

  void   gc_begin();
  void   gc_end();

  void        reset_gc_stat()   { _num_collections = 0; _accumulated_timer.reset(); }
  GCStatInfo* last_gc_stat()    { return _last_gc_stat; }

  virtual MemoryManager::Name kind() = 0;
};

// These subclasses of GCMemoryManager are defined to include
// GC-specific information.
// TODO: Add GC-specific information 
class CopyMemoryManager : public GCMemoryManager {
private:
public:
  CopyMemoryManager() : GCMemoryManager() {}

  MemoryManager::Name kind() { return MemoryManager::Copy; }
  const char* name()         { return "Copy"; }
};

class MSCMemoryManager : public GCMemoryManager {
private:
public:
  MSCMemoryManager() : GCMemoryManager() {}

  MemoryManager::Name kind() { return MemoryManager::MarkSweepCompact; }
  const char* name()         { return "MarkSweepCompact"; }

};

class PGCScavengeMemoryManager:public GCMemoryManager{
private:
public:
PGCScavengeMemoryManager():GCMemoryManager(){}

MemoryManager::Name kind(){return MemoryManager::PGCScavenge;}
const char*name(){return"PGC Scavenge";}

};

class PGCFullMemoryManager:public GCMemoryManager{
private:
public:
PGCFullMemoryManager():GCMemoryManager(){}

MemoryManager::Name kind(){return MemoryManager::PGCFull;}
const char*name(){return"PGC Full";}

};

class GPGCNewGCMemoryManager:public GCMemoryManager{
private:
public:
GPGCNewGCMemoryManager():GCMemoryManager(){}

MemoryManager::Name kind(){return MemoryManager::GPGCNew;}
const char*name(){return"GPGC New";}

};

class GPGCOldGCMemoryManager:public GCMemoryManager{
private:
public:
GPGCOldGCMemoryManager():GCMemoryManager(){}

MemoryManager::Name kind(){return MemoryManager::GPGCOld;}
const char*name(){return"GPGC Old";}

};

class PSScavengeMemoryManager : public GCMemoryManager {
private:
public:
  PSScavengeMemoryManager() : GCMemoryManager() {}

  MemoryManager::Name kind() { return MemoryManager::PSScavenge; }
  const char* name()         { return "PS Scavenge"; }

};

class PSMarkSweepMemoryManager : public GCMemoryManager {
private:
public:
  PSMarkSweepMemoryManager() : GCMemoryManager() {}

  MemoryManager::Name kind() { return MemoryManager::PSMarkSweep; }
  const char* name()         { return "PS MarkSweep"; }
};

#endif // MEMORYMANAGER_HPP
