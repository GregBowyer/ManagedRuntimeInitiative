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

#ifndef ALLOCATEDOBJECTS_HPP
#define ALLOCATEDOBJECTS_HPP

#include "allocation.hpp"
#include "pgcTaskManager.hpp"
#include "sizes.hpp"
class OopClosure;
class PGCTaskManager;

#define FOREACH_ALLOCATED_OBJECTS_COMPARE(m) \
  m(0, str, a->name(),          b->name()) \
  m(1, int, a->bytes(),         b->bytes()) \
  m(2, int, a->count(),         b->count()) \
  m(3, dbl, a->average_bytes(), b->average_bytes())

#define DECLARE_ALLOCATED_OBJECTS_COMPARE(num, type, expr_a, expr_b) \
  static int cmp##num##a(const Row*, const Row*); \
  static int cmp##num##d(const Row*, const Row*);

typedef int (*QsortCmp)(const void*, const void*);

// Allocated objects profile. A table mapping Klass IDs to object counts and
// bytes.
class AllocatedObjects:public CHeapObj{
public:
  // Individual vwallocated object profile row.
class Row VALUE_OBJ_CLASS_SPEC{
  public:
    // For code generation.
    static ByteSize count_offset() { return byte_offset_of(AllocatedObjects::Row, _count); }
    static ByteSize bytes_offset() { return byte_offset_of(AllocatedObjects::Row, _bytes); }

    uint64_t count() const { return _count; }
    uint64_t bytes() const { return _bytes; }

    void add(uint64_t count, uint64_t bytes) {
      assert((bytes >> LogHeapWordSize) >= count, "object words less than count");
_count+=count;
_bytes+=bytes;
    }

  private:
    uint64_t _count;
    uint64_t _bytes;
  };

  static void init();

  // For code generation.
  static ByteSize rows_offset() { return byte_offset_of(AllocatedObjects, _rows); }
  static ByteSize size_offset() { return byte_offset_of(AllocatedObjects, _size); }

  // Access to published profiles.
  static AllocatedObjects* acquire_if_not_null(AllocatedObjects **ptr);
  static void              release_if_not_null(AllocatedObjects *objs);
  static AllocatedObjects* publish(AllocatedObjects *objs, AllocatedObjects **ptr);

  // Called from generated code to allocate the rows for the profile of the
  // current thread.
  static void allocate_rows_for_current_thread();

  // Shared profile for all the mutator threads which have exited. Updated while
  // holding the Threads_lock.
  static AllocatedObjects* dead_threads() { return _dead_threads; }

  // Size of mutator thread profiles. Used when creating new threads, and to
  // determine when profiles for existing threads need to be grown.
  static int  mutator_size()             { return _mutator_size; }
  static void set_mutator_size(int size) { _mutator_size = size; }

  // Called by KlassTable when a new Klass ID is assigned. This could trigger a
  // safepoint to expansion the mutator thread profiles.
  static void new_klass_id_assigned(int klass_id);

  // Called from a safepoint to set all the per-thread profiles asside to later
  // sum concurrently.
  static void gather(PGCTaskManager *mgr);

  // Concurrently sums the per-thread profiles set aside at a safepoint and
  // publishes the resulting profile.
  static AllocatedObjects* sum(PGCTaskManager *mgr);

  // Traverses any refs in published profiles.
  static void oops_do(OopClosure *f);

  // Services an ARTA request.
  static void to_xml(azprof::Request *req, azprof::Response *res);
  static void to_csv(azprof::Request *req, azprof::Response *res);

  AllocatedObjects(int size = 0);
  ~AllocatedObjects();

  // Reference counting to safely access profiles by non-GC threads.
  AllocatedObjects* acquire();
  void release();

int size()const{return _size;}

  const Row* at(int i) const {
assert(are_rows_allocated(),"rows aren't allocated");
    assert((0 <= i) && (i < size()), "row index out of range");
return&_rows[i];
  }
  Row* at(int i) {
assert(are_rows_allocated(),"rows aren't allocated");
    assert((0 <= i) && (i < size()), "row index out of range");
return&_rows[i];
  }

  // Lazily reallocation and transfer of profile rows.
  bool are_rows_allocated() const { return _rows != NULL; }
  void allocate_rows_if_needed()  { if (!are_rows_allocated()) allocate_rows(); }
  void allocate_rows();
  void deallocate_rows();
  void move_rows_to(AllocatedObjects *objs);

  // Update a row in the profile. Allocating the rows if necessary.
  void add(int klass_id, uint64_t count, uint64_t bytes) {
    allocate_rows_if_needed();
    at(klass_id)->add(count, bytes);
  }
  void add_all(AllocatedObjects *objs);

  // Increaset the size of the profile.
void grow(int size);

private:
  // List of profiles used while summing them concurrently in parallel. All the
  // profiles are the same size.
class List:public CHeapObj{
  public:
    List(int capacity, int profile_size);
    ~List();

    int length()       const { return _length; }
    int capacity()     const { return _capacity; }
    int profile_size() const { return _profile_size; }

    AllocatedObjects* at(int i) {
      assert0((0 <= i) && (i < length()));
return _array[i];
    }

    // Moves the rows of the given profile to a new profile in the list if they
    // are allocated. The rows of the profile will be lazily reallocated.
    void move_to_if_rows_are_allocated(AllocatedObjects *objs);

    // Sums and deallocates a list of profiles by running GC tasks.
    AllocatedObjects* sum(PGCTaskManager *mgr);

  private:
AllocatedObjects**_array;
    int                _length;
    int                _capacity;
    int                _profile_size;
  };

  // Sums a list of profiles. Each task takes a range of Klass IDs, sums the
  // stats for those Klass IDs across all the profiles, and updates a shared
  // table with those stats. There is no synchronization between tasks.
  class SumTask : public PGCTask {
  public:
    // Rows at a time that each task works with.
    enum { chunk_size = 128 };

    SumTask(List *input_list, AllocatedObjects *output_objs, int klass_id_offset);

virtual const char*name(){return"sum-allocated-objects-task";}
    virtual void do_it(uint64_t which);

  private:
    List             *_input_list;
    AllocatedObjects *_output_objs;
    int               _klass_id_offset;
  };

  // Summary of object allocation stats used while displaying them as XML or CSV.
class Summary VALUE_OBJ_CLASS_SPEC{
  public:
    class Row {
    public:
      Row(const char *name, uint64_t count, uint64_t bytes) :
        _name(name),
        _count(count),
        _bytes(bytes) {}

      const char* name()  const { return _name; }
      uint64_t    count() const { return _count; }
      uint64_t    bytes() const { return _bytes; }
      double      average_bytes() const { return ((double) bytes()) / count(); }

      void to_xml(azprof::Response *res) const;
      void to_csv(azprof::Response *res) const;

    private:
      const char *_name;
      uint64_t    _count;
      uint64_t    _bytes;
    };

    FOREACH_ALLOCATED_OBJECTS_COMPARE(DECLARE_ALLOCATED_OBJECTS_COMPARE)
    static QsortCmp qsort_cmp(int sort, int order);

    Summary(AllocatedObjects *objs, int start, int stride, int sort, int order);

    int size()   const { return _size; }
    int start()  const { return _start; }
    int stride() const { return _stride; }

    const Row* at(int i) const {
      assert0((0 <= i) && (i < size()));
return&_rows[i];
    }

    uint64_t total_count() const { return _total_count; }
    uint64_t total_bytes() const { return _total_bytes; }

    void to_xml(azprof::Response *res) const;
    void to_csv(azprof::Response *res) const;

  private:
    Row     *_rows;
    int      _size;
    int      _start;
    int      _stride;
    uint64_t _total_count;
    uint64_t _total_bytes;
  };

  static List             *_gathered;
  static AllocatedObjects *_last;
  static AllocatedObjects *_dead_threads;
  static int               _mutator_size;

  Row *_rows;
  int  _size;
  int  _ref_count;
};

#endif // ALLOCATEDOBJECTS_HPP
