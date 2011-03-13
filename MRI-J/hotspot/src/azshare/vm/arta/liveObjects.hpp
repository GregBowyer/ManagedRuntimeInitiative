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


#ifndef LIVEOBJECTS_HPP
#define LIVEOBJECTS_HPP

#include "klassIds.hpp"
#include "mutex.hpp"
#include "pgcTaskManager.hpp"
class OopClosure;

#define FOREACH_LIVE_OBJECTS_REFERANT_COMPARE(m) \
  m(0, str, a->name(),          b->name()) \
  m(1, int, a->bytes(),         b->bytes()) \
  m(2, int, a->count(),         b->count()) \
  m(3, dbl, a->average_bytes(), b->average_bytes())

#define DECLARE_LIVE_OBJECTS_REFERANT_COMPARE(num, type, expr_a, expr_b) \
  static int cmp##num##a(const Referant*, const Referant*); \
  static int cmp##num##d(const Referant*, const Referant*);

#define FOREACH_LIVE_OBJECTS_REFERRER_COMPARE(m) \
  m(0, str, a->name(),          b->name()) \
  m(1, int, a->bytes(),         b->bytes()) \
  m(2, int, a->count(),         b->count()) \
  m(3, dbl, a->average_bytes(), b->average_bytes())

#define DECLARE_LIVE_OBJECTS_REFERRER_COMPARE(num, type, expr_a, expr_b) \
  static int cmp##num##a(const Referrer*, const Referrer*); \
  static int cmp##num##d(const Referrer*, const Referrer*);

typedef int (*QsortCmp)(const void*, const void*);

// Live objects profile. A hash table mapping reference chain Klass IDs to
// object counts and sizes.
class LiveObjects:public CHeapObj{
public:
  // Individual live object profile row.
class Row VALUE_OBJ_CLASS_SPEC{
  public:
    bool is_used() const { return !klass_ids().is_null(); }

    KlassIds klass_ids() const { return _klass_ids; }
    void set_klass_ids(KlassIds klass_ids) {
assert(_klass_ids.is_null(),"klass Ids already set");
        _klass_ids = klass_ids;
    }

    uint64_t count()             const { return _count; }
    void     set_count(uint64_t count) { _count = count; }

    uint64_t words()             const { return _words; }
    void     set_words(uint64_t words) { _words = words; }

    void add(uint64_t count, uint64_t words) {
      assert(words >= count, "object words less than count");
_count+=count;
_words+=words;
    }

  private:
    KlassIds _klass_ids;
    uint64_t _count;
    uint64_t _words;
  };

  static void init();

  // Access to published profiles.
  static LiveObjects* acquire_if_not_null(LiveObjects **ptr);
  static void         release_if_not_null(LiveObjects *objs);
  static void         add_all_if_not_null(LiveObjects *dst, LiveObjects **ptr);
  static LiveObjects* publish(LiveObjects *objs, LiveObjects **ptr);

  // Shared profile for all the mutator threads which have exited. Updated while
  // holding the Threads_lock.
  static LiveObjects* dead_threads() { return _dead_threads; }

  // Concurrently sum per-thread profiles and publish the resulting profile.
  static LiveObjects* sum_new_gc_marked(PGCTaskManager *mgr);
  static LiveObjects* sum_promoted(PGCTaskManager *mgr);
  static LiveObjects* sum_old_gc_marked(PGCTaskManager *mgr);

  // Traverses any refs in published profiles.
  static void oops_do(OopClosure *f);

  // Services an ARTA request.
  static void to_xml(azprof::Request *req, azprof::Response *res);
  static void to_csv(azprof::Request *req, azprof::Response *res);

  LiveObjects(int capacity = _default_capacity);
  LiveObjects(const LiveObjects *other);
  ~LiveObjects();

  // Reference counting to safely access profiles by non-GC threads.
  LiveObjects* acquire();
  void release();

  int size()     const { return _size; }
  int capacity() const { return _capacity; }

  Row* at(int i) {
    assert((0 <= i) && (i < capacity()), "row index out-of-range");
return&_rows[i];
  }
  const Row* at(int i) const {
    assert((0 <= i) && (i < capacity()), "row index out-of-range");
return&_rows[i];
  }

  // Scans the profile to find the max Klass ID in use.
  int max_klass_id() const;

  // Non thread-safe update of profile. May expand the profile.
  void add(KlassIds klass_ids, uint64_t count, uint64_t words) {
    //assert0( klass_ids.referrer()!=0 );
    //assert0( klass_ids.referant()!=0 );
    find(klass_ids)->add(count, words);
  }
  void add(int referant_klass_id, int referrer_klass_id, uint64_t count, uint64_t words) {
    add(KlassIds(referant_klass_id, referrer_klass_id), count, words);
  }

  // Add all the stats from the given profile to this profile. May expand this
  // profile.
  void add_all(LiveObjects *objs);

  // Zero all the stats in the profile.
  void reset();

   // Sums the referant object count and words across all rows.
  void total(uint64_t *count, uint64_t *words) const;

private:
  enum {
    _default_capacity = 8
  };

  // Stack of profiles used while summing them concurrently in parallel.
class Stack:public CHeapObj{
  public:
    Stack(int capacity);

    int size()     const { return _size; }
    int capacity() const { return _capacity; }

    // Attempts to pop more profiles, but if no more are available then pushes
    // what is given. Returns the new number of profiles.
    int pop_or_push(LiveObjects **array, int size, int capacity);

    // Push a single profile.
    void push(LiveObjects *objs);

    // Sums and resets a stack of profiles by running GC tasks.
    LiveObjects* sum_and_reset(PGCTaskManager *mgr);

  private:
    AzLock        _mutex;
LiveObjects**_array;
    int           _size;
    int           _capacity;
  };

  // Sums and resets a stack of profiles. Profiles are popped from a stack,
  // summed into another profile, and then reset.
  class SumAndResetTask : public PGCTask {
  public:
    // Number of profiles to operator on at a time.
    enum { chunk_size = 4 };

    SumAndResetTask(Stack *stack);

virtual const char*name(){return"sum-live-objects-task";}
    virtual void  do_it(uint64_t which);

  private:
    Stack       *_stack;
    LiveObjects *_array[chunk_size];
    int          _size;
  };

  // Summary of live object stats used while displaying them as XML or CSV. Two
  // levels of referants and their referrers.
class Summary VALUE_OBJ_CLASS_SPEC{
  public:
    // A referrer entry in a referant.
    class Referrer {
    public:
      FOREACH_LIVE_OBJECTS_REFERRER_COMPARE(DECLARE_LIVE_OBJECTS_REFERRER_COMPARE)
      static QsortCmp qsort_cmp(int sort, int order);

      Referrer(const char *name, uint64_t count, uint64_t words) :
        _name(name),
        _count(count),
        _words(words) {}

      const char* name()          const { return _name; }
      uint64_t    count()         const { return _count; }
      uint64_t    bytes()         const { return _words << LogHeapWordSize; }
      double      average_bytes() const { return ((double) bytes()) / count(); }

      void to_xml(azprof::Response *res) const;

    private:
      const char *_name;
      uint64_t    _count;
      uint64_t    _words;
    };

    // A referant with a list of referrers.
    class Referant {
    public:
      FOREACH_LIVE_OBJECTS_REFERANT_COMPARE(DECLARE_LIVE_OBJECTS_REFERANT_COMPARE)
      static QsortCmp qsort_cmp(int sort, int order);

      Referant(const char *name) :
_name(name),
        _count(0),
        _words(0),
_referrers(NULL),
        _referrer_count(0),
        _referrer_capacity(0) {}

      const char* name()               const { return _name; }
      void        set_name(const char *name) { _name = name; }
      uint64_t    count()              const { return _count; }
      uint64_t    bytes()              const { return _words << LogHeapWordSize; }
      double      average_bytes()      const { return ((double) bytes()) / count(); }

      int referrers() const { return _referrer_count; }

      const Referrer* referrer(int i) const {
        assert0((0 <= i) && (i < _referrer_count));
return&_referrers[i];
      }

      void add(const char *name, uint64_t count, uint64_t words);

      void qsort(int sort, int order);

      void to_xml(azprof::Response *res) const;
      void to_csv(azprof::Response *res) const;

    private:
      const char *_name;
      uint64_t    _count;
      uint64_t    _words;
      Referrer   *_referrers;
      int         _referrer_count;
      int         _referrer_capacity;
    };

    Summary(LiveObjects *objs, int start, int stride, int sort, int order);

    int start()  const { return _start; }
    int stride() const { return _stride; }

    int referants() const { return _referants_size; }

    const Referant* referant(int i) const {
      assert0((0 <= i) && (i < referants()));
return&_referants[i];
    }

    uint64_t total_count() const { return _total_count; }
    uint64_t total_bytes() const { return _total_words << LogHeapWordSize; }

    void to_xml(azprof::Response *res) const;
    void to_csv(azprof::Response *res) const;

  private:
    const char* name(int klass_id);

    Referant    *_referants;
    int          _referants_size;
    const char **_names;
    int          _names_size;
    int          _start;
    int          _stride;
    uint64_t     _total_count;
    uint64_t     _total_words;
  };

  // Mark the current GC thread blocked before we might block so the VM can
  // shutdown cleanly.
  static void begin_blocking();
  static void end_blocking();

  // Traverses any refs in a published profile if it's not null.
  static void oops_do_if_not_null(OopClosure *f, LiveObjects **ptr);

  // Whether the profile has reached its capacity and needs to be grown.
  bool is_full() const { return size() >= (capacity()/2); }

  // Finds the row for the given Klass IDs.
  Row* find(KlassIds klass_ids) {
    int i = klass_ids.hash() & (capacity()-1);
    while (true) {
Row*row=at(i);
      if (row->klass_ids() == klass_ids) {
        return row;
      } else if (!row->is_used()) {
        return find_slow_path(klass_ids, row);
      } else {
        i = (i+1) & (capacity()-1);
      }
    }
  }

  // Slow path to facilitate inlining.
  Row* find_slow_path(KlassIds klass_ids, Row *row);

  // Doubles the capacity of the profile.
  void grow();

  static LiveObjects *_last_new_gc_marked;
  static LiveObjects *_last_promoted;
  static LiveObjects *_last_old_gc_marked;
  static LiveObjects *_dead_threads;

  Row *_rows;
  int  _capacity;
  int  _size;
  int  _ref_count;
};

#endif // LIVEOBJECTS_HPP
