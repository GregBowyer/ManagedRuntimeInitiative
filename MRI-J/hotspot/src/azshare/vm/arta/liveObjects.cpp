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


#include "allocation.hpp"
#include "gpgc_thread.hpp"
#include "liveObjects.hpp"
#include "mutexLocker.hpp"
#include "oopTable.hpp"
#include "pgcTaskThread.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "thread_os.inline.hpp"

////////////////////////////////////////////////////////////////////////////////
// LiveObjects::Stack
////////////////////////////////////////////////////////////////////////////////

LiveObjects::Stack::Stack(int capacity) :
  _mutex(LiveObjects_lock._rank, "LiveObjects_Stack_lock", false),
  _array(NEW_RESOURCE_ARRAY(LiveObjects*, capacity)),
  _size(0),
  _capacity(capacity) {}

int LiveObjects::Stack::pop_or_push(LiveObjects **array, int size, int capacity) {
  assert0((0 <= size) && (size <= capacity));
MutexLocker ml(_mutex);
  if (_size > 0) {
    int n = MIN2(_size, capacity - size);
    for (int k = 0; k < n; k++) {
      array[size + k] = _array[_size - k - 1];
    }
_size-=n;
return size+n;
  } else {
    assert((_size + size) <= _capacity, "live object stack overflow");
    for (int k = 0; k < size; k++) {
      _array[_size + k] = array[k];
    }
_size+=size;
    return 0;
  }
}

void LiveObjects::Stack::push(LiveObjects *objs) {
MutexLocker ml(_mutex);
assert(_size<_capacity,"live object stack overflow");
  _array[_size++] = objs;
}

LiveObjects* LiveObjects::Stack::sum_and_reset(PGCTaskManager *mgr) {
#ifdef ASSERT
  // Total the input profiles for verification of the output profile.
  uint64_t input_count = 0, input_words = 0;
  for (int i = 0; i < _size; i++) {
    _array[i]->total(&input_count, &input_words);
  }
#endif

  // Create and execute GC tasks.
PGCTaskQueue*q=PGCTaskQueue::create();
  int n = round_to(_size, SumAndResetTask::chunk_size);
  for (int k = 0; k < n; k += SumAndResetTask::chunk_size) {
    q->enqueue(new SumAndResetTask(this));
  }
  mgr->add_list(q);

  // Return a copy of the final profile containing the sum after resetting it.
  assert0(_size == 1);
  LiveObjects *objs = new LiveObjects(_array[0]);
  _array[0]->reset();

#ifdef ASSERT
  // Verify that the output profile total matches the input profiles total.
  uint64_t output_count = 0, output_words = 0;
  objs->total(&output_count, &output_words);
  assert(
    (output_count == input_count) && (output_words == input_words),
"output profile total don't match input profile total");
#endif

  return objs;
}

////////////////////////////////////////////////////////////////////////////////
// LiveObjects::SumAndResetTask
////////////////////////////////////////////////////////////////////////////////

LiveObjects::SumAndResetTask::SumAndResetTask(Stack *stack) :
  _stack(stack),
  _size(0)
{
  memset(_array, 0, sizeof(LiveObjects*)*chunk_size);

  // Pop an initial chunk up-front to avoid lock contention.
  _size = stack->pop_or_push(_array, 0, chunk_size);
guarantee(_size>0,"no initial profiles for live objects sum task");
}

void LiveObjects::SumAndResetTask::do_it(uint64_t which) {
  // Spin summing profiles until there are no more left on the stack, then push
  // our remaining profile on the stack for another thread to consume.
  do {
for(int i=1;i<_size;i++){
      _array[0]->add_all(_array[i]);
      _array[i]->reset();
    }
    memset(_array+1, 0, sizeof(LiveObjects*)*(chunk_size-1));
  } while ((_size = _stack->pop_or_push(_array, 1, chunk_size)) != 0);
}

////////////////////////////////////////////////////////////////////////////////
// LiveObjects::Summary::Referrer
////////////////////////////////////////////////////////////////////////////////

#ifdef AZ_PROFILER
#define DEFINE_LIVE_OBJECTS_REFERRER_COMPARE(num, type, expr_a, expr_b) \
  int LiveObjects::Summary::Referrer::cmp##num##a(const Referrer *a, const Referrer *b) { \
    return azprof::type##cmpa(expr_a, expr_b); \
  } \
  int LiveObjects::Summary::Referrer::cmp##num##d(const Referrer *a, const Referrer *b) { \
    return azprof::type##cmpd(expr_a, expr_b); \
  }

#define LIVE_OBJECTS_REFERRER_QSORT_COMPARE_CASE(num, type, expr_a, expr_b) \
  case num: return (QsortCmp) ((order > 0) ? cmp##num##a : cmp##num##d);

FOREACH_LIVE_OBJECTS_REFERRER_COMPARE(DEFINE_LIVE_OBJECTS_REFERRER_COMPARE)
#endif // AZ_PROFILER

QsortCmp LiveObjects::Summary::Referrer::qsort_cmp(int sort, int order) {
#ifdef AZ_PROFILER
  switch (sort) {
  FOREACH_LIVE_OBJECTS_REFERRER_COMPARE(LIVE_OBJECTS_REFERRER_QSORT_COMPARE_CASE);
  default: return (QsortCmp) ((order > 0) ? cmp0a : cmp0d);
  }
#else // !AZ_PROFILER
  return NULL;
#endif // !AZ_PROFILER
}

void LiveObjects::Summary::Referrer::to_xml(azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "referrer");
  if (name() != NULL) azprof::Xml::leaf(res, "name", name());
  azprof::Xml::leaf(res, "count", count());
  azprof::Xml::leaf(res, "bytes", bytes());
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// LiveObjects::Summary::Referant
////////////////////////////////////////////////////////////////////////////////

#ifdef AZ_PROFILER
#define DEFINE_LIVE_OBJECTS_REFERANT_COMPARE(num, type, expr_a, expr_b) \
  int LiveObjects::Summary::Referant::cmp##num##a(const Referant *a, const Referant *b) { \
    return azprof::type##cmpa(expr_a, expr_b); \
  } \
  int LiveObjects::Summary::Referant::cmp##num##d(const Referant *a, const Referant *b) { \
    return azprof::type##cmpd(expr_a, expr_b); \
  }

#define LIVE_OBJECTS_REFERANT_QSORT_COMPARE_CASE(num, type, expr_a, expr_b) \
  case num: return (QsortCmp) ((order > 0) ? cmp##num##a : cmp##num##d);

FOREACH_LIVE_OBJECTS_REFERANT_COMPARE(DEFINE_LIVE_OBJECTS_REFERANT_COMPARE)
#endif // AZ_PROFILER

QsortCmp LiveObjects::Summary::Referant::qsort_cmp(int sort, int order) {
#ifdef AZ_PROFILER
  switch (sort) {
  FOREACH_LIVE_OBJECTS_REFERANT_COMPARE(LIVE_OBJECTS_REFERANT_QSORT_COMPARE_CASE);
  default: return (QsortCmp) ((order > 0) ? cmp0a : cmp0d);
  }
#else // !AZ_PROFILER
  return NULL;
#endif // !AZ_PROFILER
}

void LiveObjects::Summary::Referant::add(const char *name, uint64_t count, uint64_t words) {
  if (_referrer_count == _referrer_capacity) {
    int       old_capacity  = _referrer_capacity;
    int       new_capacity  = MAX2(8, 2*old_capacity);
    Referrer *old_referrers = _referrers;
    Referrer *new_referrers = NEW_RESOURCE_ARRAY(Referrer, new_capacity);

    memcpy(new_referrers, old_referrers, sizeof(Referrer)*old_capacity);
    memset(new_referrers + old_capacity, 0, sizeof(Referrer)*(new_capacity - old_capacity));

    _referrers          = new_referrers;
_referrer_capacity=new_capacity;
  }

  _referrers[_referrer_count++] = Referrer(name, count, words);

_count+=count;
_words+=words;
}

void LiveObjects::Summary::Referant::qsort(int sort, int order) {
  ::qsort(_referrers, _referrer_count, sizeof(Referrer), Referrer::qsort_cmp(sort, order));
}

void LiveObjects::Summary::Referant::to_xml(azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "referant");
  azprof::Xml::leaf(res, "name", name());
  azprof::Xml::leaf(res, "count", count());
  azprof::Xml::leaf(res, "bytes", bytes());
for(int i=0;i<referrers();i++){
    referrer(i)->to_xml(res);
  }
#endif // AZ_PROFILER
}

void LiveObjects::Summary::Referant::to_csv(azprof::Response *res) const {
#ifdef AZ_PROFILER
  res->print("%12llu, %10llu, %13.1f, %s\n", bytes(), count(), average_bytes(), name());
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// LiveObjects::Summary
////////////////////////////////////////////////////////////////////////////////

LiveObjects::Summary::Summary(LiveObjects *objs, int start, int stride, int sort, int order) :
_referants(NULL),
  _referants_size(0),
_names(NULL),
  _names_size(0),
  _start(start),
  _stride(stride),
  _total_count(0),
  _total_words(0)
{
  int n = objs->max_klass_id() + 1;

  _referants = NEW_RESOURCE_ARRAY(Referant, n);
  memset(_referants, 0, sizeof(Referant)*n);

  _names      = NEW_RESOURCE_ARRAY(const char*, n);
_names_size=n;
  memset(_names, 0, sizeof(const char*)*n);

  // Build up the referants by indexing by Klass ID into the array.
for(int i=0;i<objs->capacity();i++){
    Row *row = objs->at(i);
if(row->count()!=0){
      KlassIds  ids      = row->klass_ids();
      uint64_t  count    = row->count();
      uint64_t  words    = row->words();

      assert0((0 < ids.referant()) && (ids.referant() < n));
      Referant *referant = &_referants[ids.referant()];

      referant->set_name(name(ids.referant()));
      referant->add(name(ids.referrer()), count, words);

_total_count+=count;
_total_words+=words;
    }
  }

  // Compact the referants and sort their referrers.
  for (int i = 0; i < n; i++) {
    Referant *referant = &_referants[i];
if(referant->count()!=0){
      _referants[_referants_size++] = *referant;
      referant->qsort(sort, order);
    }
  }

  // Sort the referants.
  ::qsort(_referants, _referants_size, sizeof(Referant), Referant::qsort_cmp(sort, order));
}

const char* LiveObjects::Summary::name(int klass_id) {
  if( klass_id < 0 ) {
    static const char* fake_kid_name[-KlassIds::max_fake_kid] = {
      "",                       // -0
      "new2old_root",           // -1 new2old_root
      "old_system_root",        // -2 old_system_root
      "new_system_root",        // -3 new_system_root
      "j_l_ref_root",           // -4 j_l_ref_root
      "new_weak_jni_root",      // -5 new_weak_jni_root
      "string_intern_root",     // -6 string_intern_root
      "cardmark_root",          // -7 cardmark_root
      "jvm_internal_lvb"        // -8 jvm_internal_lvb
    };
    return fake_kid_name[-klass_id];
  }

  assert0((0 <= klass_id) && (klass_id < _names_size));
  if (klass_id != 0) {
const char*name=_names[klass_id];
    if (name == NULL) {
      _names[klass_id] = name = KlassTable::pretty_name(klass_id);
      assert0(name != NULL);
    }
    return name;
  } else {
    return NULL;
  }
}

void LiveObjects::Summary::to_xml(azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "live-objects");
  { azprof::Xml tag(res, "pagination");
    azprof::Xml::leaf(res, "start", start());
    azprof::Xml::leaf(res, "stride", stride());
    azprof::Xml::leaf(res, "count", referants());
  }
  { azprof::Xml tag(res, "total");
    azprof::Xml::leaf(res, "count", total_count());
    azprof::Xml::leaf(res, "bytes", total_bytes());
  }
  for (int i = start()-1; i < MIN2(referants(), start() + stride() - 1); i++) {
    referant(i)->to_xml(res);
  }
#endif // AZ_PROFILER
}

void LiveObjects::Summary::to_csv(azprof::Response *res) const {
#ifdef AZ_PROFILER
  res->ok("text/csv", -1);
res->end_header();
res->print("    Size (B),      Count,       Avg (B), Class name\n");
for(int i=0;i<referants();i++){
    referant(i)->to_csv(res);
  }
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// LiveObjects
////////////////////////////////////////////////////////////////////////////////

LiveObjects *LiveObjects::_last_new_gc_marked = NULL;
LiveObjects *LiveObjects::_last_promoted      = NULL;
LiveObjects *LiveObjects::_last_old_gc_marked = NULL;
LiveObjects *LiveObjects::_dead_threads       = NULL;

void LiveObjects::init(){
  _dead_threads = new LiveObjects();
}

LiveObjects* LiveObjects::publish(LiveObjects *new_objs, LiveObjects **old_objs) {
MutexLocker ml(LiveObjects_lock);
  release_if_not_null(*old_objs);
  *old_objs = new_objs;

  return new_objs;
}

void LiveObjects::begin_blocking() {
  assert0(UseGenPauselessGC);
assert0(Thread::current()->is_GenPauselessGC_thread());
  
GPGC_Thread*thread=(GPGC_Thread*)Thread::current();
  if (thread->should_terminate() || VM_Exit::vm_exited()) {
thread->block_if_vm_exited();
  }
thread->set_blocked(true);
}

void LiveObjects::end_blocking() {
  assert0(UseGenPauselessGC);
assert0(Thread::current()->is_GenPauselessGC_thread());
  
GPGC_Thread*thread=(GPGC_Thread*)Thread::current();
thread->set_blocked(false);
}

LiveObjects* LiveObjects::acquire_if_not_null(LiveObjects **ptr) {
MutexLocker ml(LiveObjects_lock);
  LiveObjects *objs = *ptr;
if(objs!=NULL){
    objs->acquire();
  }
  return objs;
}

void LiveObjects::add_all_if_not_null(LiveObjects *dst, LiveObjects **ptr) {
  LiveObjects *objs = acquire_if_not_null(ptr);
if(objs!=NULL){
    dst->add_all(objs);
  }
  release_if_not_null(objs);
}

void LiveObjects::release_if_not_null(LiveObjects *objs) {
if(objs!=NULL){
    objs->release();
  }
}

LiveObjects* LiveObjects::sum_new_gc_marked(PGCTaskManager *mgr) {
assert0(Thread::current()->is_GenPauselessGC_thread());

  // Sum and reset the GC thread marking stats.
  Stack stack(mgr->workers() + 1);
for(int i=0;i<mgr->workers();i++){
    stack.push(mgr->thread(i)->live_objects());
  }
  stack.push(Thread::current()->live_objects());
  return publish(stack.sum_and_reset(mgr), &_last_new_gc_marked);
}

LiveObjects* LiveObjects::sum_promoted(PGCTaskManager *mgr) {
assert0(Thread::current()->is_GenPauselessGC_thread());

  // Sum and reset the GC thread promotion stats.
  Stack stack(mgr->workers() + 1);
for(int i=0;i<mgr->workers();i++){
    stack.push(mgr->thread(i)->live_objects());
  }
  stack.push(Thread::current()->live_objects());
  LiveObjects *new_objs = stack.sum_and_reset(mgr);

  // Add any previous promotion stats the the new stats, publish the new
  // promotion stats, and release any previous stats.
  LiveObjects *old_objs = acquire_if_not_null(&_last_promoted);
  if (old_objs != NULL) new_objs->add_all(old_objs);
  publish(new_objs, &_last_promoted);
  release_if_not_null(old_objs);

  return new_objs;
}

LiveObjects* LiveObjects::sum_old_gc_marked(PGCTaskManager *mgr) {
assert0(Thread::current()->is_GenPauselessGC_thread());

  // Sum and reset all the mutator thread stats up-front while holding the
  // Threads_lock so that threads don't exit underneath us and free their
  // profiles. Mutator thread stats are limited, so this should be a quick
  // operation.
  LiveObjects mutator;
  begin_blocking();
  { MutexLocker ml1(Threads_lock, Thread::current());
    for (JavaThread *thread = Threads::first(); thread != NULL; thread = thread->next()) {
      // mutator's can grow their individual object profiles concurrently, 
      // co-ordinate with the grow() function
thread->acquire_live_objects();
      mutator.add_all(thread->live_objects());
      thread->live_objects()->reset();
thread->release_live_objects();
    }
    mutator.add_all(dead_threads());
dead_threads()->reset();
  }
  end_blocking();
  { uint64_t count = 0, words = 0;
    mutator.total(&count, &words);
  }

  // Sum and reset the GC thread stats with the mutator stats.
  Stack stack(mgr->workers() + 3);
for(int i=0;i<mgr->workers();i++){
    stack.push(mgr->thread(i)->live_objects());
    { uint64_t count = 0, words = 0;
      mgr->thread(i)->live_objects()->total(&count, &words);
    }
  }
  stack.push(Thread::current()->live_objects());
  { uint64_t count = 0, words = 0;
    Thread::current()->live_objects()->total(&count, &words);
  }

  {
    MutexLocker ml(Heap_lock,Thread::current()); // grab the Heap_lock to co-ordinate with the VM-Thread exit
Thread*vm_thread=VMThread::vm_thread();
    if ( vm_thread ) {
      stack.push(vm_thread->live_objects()); 
      { uint64_t count = 0, words = 0;
        vm_thread->live_objects()->total(&count, &words);
      }
    }
  }
  
  stack.push(&mutator);
  LiveObjects *old_gc_marked = stack.sum_and_reset(mgr);

  // Publish the new old GC marking stats and clear any promotion stats.
  LiveObjects *promoted = new LiveObjects();
{MutexLocker ml(LiveObjects_lock);

    release_if_not_null(_last_promoted);
    _last_promoted = promoted;

    release_if_not_null(_last_old_gc_marked);
    _last_old_gc_marked = old_gc_marked;
  }

  return old_gc_marked;
}

void LiveObjects::oops_do(OopClosure*f){
  oops_do_if_not_null(f, &_last_new_gc_marked);
  oops_do_if_not_null(f, &_last_promoted);
  oops_do_if_not_null(f, &_last_old_gc_marked);
}

void LiveObjects::oops_do_if_not_null(OopClosure *f, LiveObjects **ptr) {
  LiveObjects *objs = acquire_if_not_null(ptr);
if(objs!=NULL){
for(int i=0;i<objs->capacity();i++){
      KlassIds ids = objs->at(i)->klass_ids();
for(int j=0;j<ids.count();j++){
        if (ids.at(j) > 0) { // Some negative KID numbers are used for system-referrer roots
          f->do_oop(KlassTable::getKlassAddrByKlassId(ids.at(j)));
        }
      }
    }
    objs->release();
  }
}

LiveObjects::LiveObjects(int capacity) {
  _rows      = NEW_C_HEAP_ARRAY(Row, capacity);
_capacity=capacity;
  _size      = 0;
  _ref_count = 1;
  memset(_rows, 0, sizeof(Row)*capacity);
}

LiveObjects::LiveObjects(const LiveObjects *other) :
  _rows(NEW_C_HEAP_ARRAY(Row, other->_capacity)),
  _capacity(other->_capacity),
  _size(other->_size),
  _ref_count(1)
{
  memcpy(_rows, other->_rows, sizeof(Row)*other->_capacity);
}

LiveObjects::~LiveObjects() {
  FREE_C_HEAP_ARRAY(Row, _rows);
}

LiveObjects* LiveObjects::acquire() {
MutexLockerNested ml(LiveObjects_lock);
assert(_ref_count>0,"non-positive ref count");
  ++_ref_count;
  return this;
}

void LiveObjects::release(){
MutexLockerNested ml(LiveObjects_lock);
assert(_ref_count>0,"non-positive ref count");
  if (--_ref_count == 0) {
    delete this;
  }
}

int LiveObjects::max_klass_id() const {
  int max = 0;
  if (size() > 0) {
for(int i=0;i<capacity();i++){
      KlassIds ids = at(i)->klass_ids();
for(int j=0;j<ids.count();j++){
        if (ids.at(j) > max) {
max=ids.at(j);
        }
      }
    }
  }
  return max;
}

void LiveObjects::add_all(LiveObjects *objs) {
  if (objs->size() > 0) {
for(int i=0;i<objs->capacity();i++){
      const Row *row = objs->at(i);
      if (row->is_used()) {
        add(row->klass_ids(), row->count(), row->words());
      }
    }
  }
}

void LiveObjects::total(uint64_t *count, uint64_t *words) const {
for(int i=0;i<capacity();i++){
    const Row *row = at(i);
    *count += row->count();
    *words += row->words();
  }
}

LiveObjects::Row* LiveObjects::find_slow_path(KlassIds klass_ids, Row *row) {
  row->set_klass_ids(klass_ids);
  ++_size;

  if (is_full()) {
    grow();
    return find(klass_ids);
  } else {
    return row;
  }
}

void LiveObjects::grow(){
assert(is_full(),"growing but not full");

  JavaThread* thread = (JavaThread*) Thread::current();
thread->acquire_live_objects();
 
  int   old_capacity = _capacity;
  Row  *old_rows     = _rows;
  int   new_capacity = 2*old_capacity;
  Row  *new_rows     = NEW_C_HEAP_ARRAY(Row, new_capacity);
  memset(new_rows, 0, sizeof(Row)*new_capacity);

for(int i=0;i<old_capacity;i++){
    Row *old_row = &old_rows[i];
    if (old_row->is_used()) {
      int j = old_row->klass_ids().hash() & (new_capacity-1);
      while (true) {
        Row *new_row = &new_rows[j];
        assert(!(new_row->klass_ids() == old_row->klass_ids()), "duplicate entry");
        if (!new_row->is_used()) {
          new_row->set_klass_ids(old_row->klass_ids());
          new_row->set_count(old_row->count());
          new_row->set_words(old_row->words());
          break;
        } else {
          j = (j+1) & (new_capacity-1);
        }
      }
    }
  }

  FREE_C_HEAP_ARRAY(Row, old_rows);
  _rows     = new_rows;
  _capacity = new_capacity;
thread->release_live_objects();
}

void LiveObjects::reset(){
  memset(_rows, 0, sizeof(Row)*_capacity);
  _size = 0;
}

void LiveObjects::to_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  if (!UseGenPauselessGC) {
    azprof::Xml::leaf(
      res, "error", "No profile is available because Pauseless GC is not being used.");
    return;
  }
  if (!ProfileLiveObjects) {
    azprof::Xml::leaf(
      res, "error",
"No profile is available because -XX:-ProfileLiveObjects was specified.");
    return;
  }

  size_t start = req->uint32_parameter_by_name("start");
  if (start < 1) start = 1;

  size_t stride = req->uint32_parameter_by_name("stride");
  if (stride < 1) stride = 50;

  const char *raw_sort = req->parameter_by_name("sort");
  int         sort     = azprof::sort_column(raw_sort, 1);
  int         order    = azprof::sort_order(raw_sort);

  LiveObjects objs;
  add_all_if_not_null(&objs, &_last_new_gc_marked);
  add_all_if_not_null(&objs, &_last_promoted);
  add_all_if_not_null(&objs, &_last_old_gc_marked);
if(objs.size()==0){
    azprof::Xml::leaf(
      res, "error",
"No profile is available because a GC cycle has not completed yet.");
    return;
  }

  Summary summary(&objs, start, stride, sort, order);
  summary.to_xml(res);
#endif // AZ_PROFILER
}

void LiveObjects::to_csv(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  if (!(UseGenPauselessGC && ProfileLiveObjects)) {
res->not_found();
    return;
  }

  const char *raw_sort = req->parameter_by_name("sort");
  int         sort     = azprof::sort_column(raw_sort, 1);
  int         order    = azprof::sort_order(raw_sort);

  LiveObjects objs;
  add_all_if_not_null(&objs, &_last_new_gc_marked);
  add_all_if_not_null(&objs, &_last_promoted);
  add_all_if_not_null(&objs, &_last_old_gc_marked);
if(objs.size()==0){
res->not_found();
    return;
  }

  Summary summary(&objs, 1, objs.capacity(), sort, order);
  summary.to_csv(res);
#endif // AZ_PROFILER
}
