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


#include "allocatedObjects.hpp"
#include "allocation.hpp"
#include "atomic.hpp"
#include "growableArray.hpp"
#include "mutexLocker.hpp"
#include "oopTable.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "safepoint.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "vm_operations.hpp"
#include "vmThread.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"


////////////////////////////////////////////////////////////////////////////////
// VM_GrowAllocatedObjects
////////////////////////////////////////////////////////////////////////////////

// VM operation to grow all the mutator allocated object profiles once a Klass
// ID has been registered that would overrun them.
class VM_GrowAllocatedObjects:public VM_Operation{
public:
VM_GrowAllocatedObjects(int size):_size(size){}

VMOp_Type type()const{return VMOp_GrowAllocatedObjects;}
  virtual void doit();
  void grow(AllocatedObjects *objs, int old_size, int new_size);

private:
  int _size;
};


void VM_GrowAllocatedObjects::doit(){
  int old_size = AllocatedObjects::mutator_size();
  int new_size = round_to(_size, 256);

  // Check that the resize is still necessary because redundant VM operations
  // may be enqueued.
  if (new_size > old_size) {
    for (JavaThread *thread = Threads::first(); thread != NULL; thread = thread->next()) {
      grow(thread->allocated_objects(), old_size, new_size);
    }

    grow(AllocatedObjects::dead_threads(), old_size, new_size);

    AllocatedObjects::set_mutator_size(new_size);
  }
}

void VM_GrowAllocatedObjects::grow(AllocatedObjects *objs, int old_size, int new_size) {
  assert0(objs->size() == old_size);
  objs->grow(new_size);
}

////////////////////////////////////////////////////////////////////////////////
// AllocatedObjects::SumTask
////////////////////////////////////////////////////////////////////////////////

AllocatedObjects::SumTask::SumTask(List *input_list, AllocatedObjects *output_objs, int klass_id_offset) :
  _input_list(input_list),
  _output_objs(output_objs),
  _klass_id_offset(klass_id_offset) {}

void AllocatedObjects::SumTask::do_it(uint64_t which) {
  for (int kid = _klass_id_offset; kid < (_klass_id_offset + chunk_size); kid++) {
    uint64_t count = 0, bytes = 0;
for(int i=0;i<_input_list->length();i++){
      const Row *row = _input_list->at(i)->at(kid);
count+=row->count();
      bytes += row->bytes();
    }
    _output_objs->at(kid)->add(count, bytes);
  }
}

////////////////////////////////////////////////////////////////////////////////
// AllocatedObjects::List
////////////////////////////////////////////////////////////////////////////////

AllocatedObjects::List::List(int capacity, int profile_size) :
  _array(NEW_C_HEAP_ARRAY(AllocatedObjects*, capacity)),
  _length(0),
  _capacity(capacity),
  _profile_size(profile_size) {}

AllocatedObjects::List::~List() {
  for (int i = 0; i < length(); i++) {
    delete at(i);
  }
FREE_C_HEAP_ARRAY(AllocatedObjects*,_array);
}

void AllocatedObjects::List::move_to_if_rows_are_allocated(AllocatedObjects *old_objs) {
  assert(length() < capacity(), "allocated objects list overflow");
  assert(old_objs->size() == profile_size(), "allocated object profile size mismatch");

  if (old_objs->are_rows_allocated()) {
    AllocatedObjects *new_objs = new AllocatedObjects(old_objs->size());
    new_objs->move_rows_to(old_objs);
    _array[_length++] = new_objs;
  }
}

AllocatedObjects* AllocatedObjects::List::sum(PGCTaskManager *mgr) {
  AllocatedObjects *objs = new AllocatedObjects(profile_size());
  objs->allocate_rows();
PGCTaskQueue*q=PGCTaskQueue::create();
  for (int k = 0; k < profile_size(); k += SumTask::chunk_size) {
    q->enqueue(new SumTask(this, objs, k));
  }
  mgr->add_list(q);

  return objs;
}

////////////////////////////////////////////////////////////////////////////////
// AllocatedObjects::Summary::Row
////////////////////////////////////////////////////////////////////////////////

void AllocatedObjects::Summary::Row::to_xml(azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "row");
  azprof::Xml::leaf(res, "name", name());
  azprof::Xml::leaf(res, "count", count());
  azprof::Xml::leaf(res, "bytes", bytes());
#endif // AZ_PROFILER
}

void AllocatedObjects::Summary::Row::to_csv(azprof::Response *res) const {
#ifdef AZ_PROFILER
  res->print("%12llu, %10llu, %13.1f, %s\n", bytes(), count(), average_bytes(), name());
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// AllocatedObjects::Summary
////////////////////////////////////////////////////////////////////////////////

#ifdef AZ_PROFILER
#define DEFINE_ALLOCATED_OBJECTS_COMPARE(num, type, expr_a, expr_b) \
  int AllocatedObjects::Summary::cmp##num##a(const Row *a, const Row *b) { \
    return azprof::type##cmpa(expr_a, expr_b); \
  } \
  int AllocatedObjects::Summary::cmp##num##d(const Row *a, const Row *b) { \
    return azprof::type##cmpd(expr_a, expr_b); \
  }

#define ALLOCATED_OBJECTS_QSORT_COMPARE_CASE(num, type, expr_a, expr_b) \
  case num: return (QsortCmp) ((order > 0) ? cmp##num##a : cmp##num##d);

FOREACH_ALLOCATED_OBJECTS_COMPARE(DEFINE_ALLOCATED_OBJECTS_COMPARE)
#endif // AZ_PROFILER

QsortCmp AllocatedObjects::Summary::qsort_cmp(int sort, int order) {
#ifdef AZ_PROFILER
  switch (sort) {
  FOREACH_ALLOCATED_OBJECTS_COMPARE(ALLOCATED_OBJECTS_QSORT_COMPARE_CASE);
  default: return (QsortCmp) ((order > 0) ? cmp0a : cmp0d);
  }
#else // !AZ_PROFILER
  return NULL;
#endif // !AZ_PROFILER
}

AllocatedObjects::Summary::Summary(
  AllocatedObjects *objs, int start, int stride, int sort, int order
) :
  _rows(NEW_RESOURCE_ARRAY(Row, objs->size())),
  _size(0),
  _start(start),
  _stride(stride),
  _total_count(0),
  _total_bytes(0)
{
for(int i=0;i<objs->size();i++){
    AllocatedObjects::Row *row = objs->at(i);
if(row->count()!=0){
      const char *name  = KlassTable::pretty_name(i);
      uint64_t    count = row->count();
      uint64_t    bytes = row->bytes();

      assert0(name != NULL);

      _rows[_size++] = Row(name, count, bytes);
_total_count+=count;
_total_bytes+=bytes;
    }
  }

  ::qsort(_rows, _size, sizeof(Row), qsort_cmp(sort, order));
}

void AllocatedObjects::Summary::to_xml(azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "allocated-objects");
  { azprof::Xml tag(res, "pagination");
    azprof::Xml::leaf(res, "start", start());
    azprof::Xml::leaf(res, "stride", stride());
    azprof::Xml::leaf(res, "count", size());
  }
  { azprof::Xml tag(res, "total");
    azprof::Xml::leaf(res, "count", total_count());
    azprof::Xml::leaf(res, "bytes", total_bytes());
  }
  for (int i = start()-1; i < MIN2(size(), start() + stride() - 1); i++) {
    at(i)->to_xml(res);
  }
#endif // AZ_PROFILER
}

void AllocatedObjects::Summary::to_csv(azprof::Response *res) const {
#ifdef AZ_PROFILER
  res->ok("text/csv", -1);
res->end_header();
res->print("    Size (B),      Count,       Avg (B), Class name\n");
for(int i=0;i<size();i++){
    at(i)->to_csv(res);
  }
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// AllocatedObjects
////////////////////////////////////////////////////////////////////////////////

AllocatedObjects::List *AllocatedObjects::_gathered     = NULL;
AllocatedObjects       *AllocatedObjects::_last         = NULL;
AllocatedObjects       *AllocatedObjects::_dead_threads = NULL;
int                     AllocatedObjects::_mutator_size = 256;

void AllocatedObjects::init(){
  _dead_threads = new AllocatedObjects(AllocatedObjects::mutator_size());
}

AllocatedObjects* AllocatedObjects::acquire_if_not_null(AllocatedObjects **ptr) {
MutexLocker ml(AllocatedObjects_lock);
  AllocatedObjects *objs = *ptr;
if(objs!=NULL){
    objs->acquire();
  }
  return objs;
}

void AllocatedObjects::release_if_not_null(AllocatedObjects *objs) {
if(objs!=NULL){
    objs->release();
  }
}

AllocatedObjects* AllocatedObjects::publish(AllocatedObjects *objs, AllocatedObjects **ptr) {
MutexLocker ml(AllocatedObjects_lock);
  release_if_not_null(*ptr);
*ptr=objs;

  return objs;
}

void AllocatedObjects::new_klass_id_assigned(int klass_id) {
  if (klass_id >= mutator_size()) {
    VM_GrowAllocatedObjects vm_op(klass_id+1);
VMThread::execute(&vm_op);
  }
}

void AllocatedObjects::allocate_rows_for_current_thread() {
  JavaThread::current()->allocated_objects()->allocate_rows();
}

void AllocatedObjects::gather(PGCTaskManager *mgr) {
assert(SafepointSynchronize::is_at_safepoint(),"not at a safepoint");
assert(_gathered==NULL,"list already gathered");

  _gathered = new List(Threads::number_of_threads(), mutator_size());
  for (JavaThread *thread = Threads::first(); thread != NULL; thread = thread->next()) {
    _gathered->move_to_if_rows_are_allocated(thread->allocated_objects());
  }
  _gathered->move_to_if_rows_are_allocated(dead_threads());
}

AllocatedObjects* AllocatedObjects::sum(PGCTaskManager *mgr) {
assert(_gathered!=NULL,"list not gathered");

  AllocatedObjects *objs = _gathered->sum(mgr);
  delete _gathered;
_gathered=NULL;

  return publish(objs, &_last);
}

void AllocatedObjects::oops_do(OopClosure*f){
  AllocatedObjects *objs = acquire_if_not_null(&_last);
if(objs!=NULL){
for(int i=0;i<objs->size();i++){
      if (objs->at(i)->count() != 0) {
        f->do_oop(KlassTable::getKlassAddrByKlassId(i));
      }
    }
    objs->release();
  }
}

AllocatedObjects::AllocatedObjects(int size) :
_rows(NULL),
  _size(size),
  _ref_count(1) {}

AllocatedObjects::~AllocatedObjects() {
  FREE_C_HEAP_ARRAY(Row, _rows);
}

AllocatedObjects* AllocatedObjects::acquire() {
MutexLockerNested ml(AllocatedObjects_lock);
assert(_ref_count>0,"non-positive ref count");
  ++_ref_count;
  return this;
}

void AllocatedObjects::release(){
MutexLockerNested ml(AllocatedObjects_lock);
assert(_ref_count>0,"non-positive ref count");
  if (--_ref_count == 0) {
    delete this;
  }
}

void AllocatedObjects::allocate_rows() {
assert(!are_rows_allocated(),"rows already allocated");
  Row *rows = NEW_C_HEAP_ARRAY(Row, _size);
memset(rows,0,sizeof(Row)*_size);
  Atomic::write_barrier();
  _rows = rows;
}

void AllocatedObjects::deallocate_rows() {
assert(are_rows_allocated(),"rows not allocated");
  FREE_C_HEAP_ARRAY(Row, _size);
_rows=NULL;
}

void AllocatedObjects::move_rows_to(AllocatedObjects *objs) {
assert(objs->_rows!=NULL,"source rows not allocated");
assert(_rows==NULL,"destination rows already allocated");

  _rows = objs->_rows;
_size=objs->_size;
  objs->_rows = NULL;
}

void AllocatedObjects::add_all(AllocatedObjects *other) {
  assert(this->size() == other->size(), "allocated object profile size mismatch");
  if (other->are_rows_allocated()) {
    allocate_rows_if_needed();
    for (int i = 0; i < size(); i++) {
      Row *this_row  = this->at(i);
      Row *other_row = other->at(i);
      this_row->add(other_row->count(), other_row->bytes());
    }
  }
}

void AllocatedObjects::grow(int size) {
if(size>_size){
    if (are_rows_allocated()) {
      _rows = REALLOC_C_HEAP_ARRAY(Row, _rows, size);
      memset(_rows + _size, 0, sizeof(Row)*(size - _size));
    }
    _size = size;
  }
}

void AllocatedObjects::to_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  if (!ProfileAllocatedObjects) {
    azprof::Xml::leaf(
      res, "error",
"No profile is available because -XX:-ProfileAllocatedObjects was specified.");
    return;
  }

  size_t start = req->uint32_parameter_by_name("start");
  if (start < 1) start = 1;

  size_t stride = req->uint32_parameter_by_name("stride");
  if (stride < 1) stride = 50;

  const char *raw_sort = req->parameter_by_name("sort");
  int         sort     = azprof::sort_column(raw_sort, 1);
  int         order    = azprof::sort_order(raw_sort);

  uint64_t thread_id = req->uint64_parameter_by_name("thr");

  if (thread_id == 0) {
    AllocatedObjects *objs = acquire_if_not_null(&_last);
if(objs==NULL){
      azprof::Xml::leaf(
        res, "error",
"No profile is available because a GC cycle has not completed yet.");
      return;
    }

    Summary summary(objs, start, stride, sort, order);
    summary.to_xml(res);

    objs->release();
  } else {
    AllocatedObjects *objs;
    { MutexLockerAllowGC ml(&Threads_lock, 1);

      JavaThread *thread = Threads::by_id_may_gc(thread_id);
      if (thread == NULL) {
        azprof::Xml::leaff(res, "error", "Thread not found (%llu).", thread_id);
        return;
      }

      objs = thread->allocated_objects();
      if (!objs->are_rows_allocated()) {
        azprof::Xml::leaf(
          res, "error",
"No profile is available because the thread has not performed any allocations since the last GC cycle.");
        return;
      }
      Atomic::read_barrier();
    }

    Summary summary(objs, start, stride, sort, order);
    summary.to_xml(res);
  }
#endif // AZ_PROFILER
}

void AllocatedObjects::to_csv(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  if (!ProfileAllocatedObjects) {
res->not_found();
    return;
  }

  const char *raw_sort = req->parameter_by_name("sort");
  int         sort     = azprof::sort_column(raw_sort, 1);
  int         order    = azprof::sort_order(raw_sort);

  uint64_t thread_id = req->uint64_parameter_by_name("thr");

  if (thread_id == 0) {
    AllocatedObjects *objs = acquire_if_not_null(&_last);
if(objs==NULL){
res->not_found();
      return;
    }

    Summary summary(objs, 1, objs->size(), sort, order);
    summary.to_csv(res);

    objs->release();
  } else {
    AllocatedObjects *objs;
    { MutexLockerAllowGC ml(&Threads_lock, 1);

      JavaThread *thread = Threads::by_id_may_gc(thread_id);
      if (thread == NULL) {
res->not_found();
        return;
      }

      objs = thread->allocated_objects();
      if (!objs->are_rows_allocated()) {
res->not_found();
        return;
      }
      Atomic::read_barrier();
    }

    Summary summary(objs, 1, objs->size(), sort, order);
    summary.to_csv(res);
  }
#endif // AZ_PROFILER
}
