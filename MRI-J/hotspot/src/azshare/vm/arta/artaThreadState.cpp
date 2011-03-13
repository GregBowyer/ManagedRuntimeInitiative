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


#include "javaClasses.hpp"
#include "mutex.hpp"
#include "mutexLocker.hpp"
#include "os_os.hpp"
#include "oop.hpp"
#include "artaThreadState.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "thread.hpp"

#include "atomic_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "oop.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

////////////////////////////////////////////////////////////////////////////////
// ArtaThreadState
////////////////////////////////////////////////////////////////////////////////

const ArtaThreadState* const ArtaThreadState::NIL = (ArtaThreadState*) (uintptr_t) -1;

ArtaThreadState* ArtaThreadState::get(JavaThread *thread) {
ArtaThreadState*state=thread->arta_thread_state();
if(state==NIL){
    // Break a cycle caused by a deadlock or just a race in reading the hint
    // fields. The caller will ignore the owner of its lock to become a leaf and
    // the given thread will become a root.
    return NULL;
}else if(state==NULL){
    // Set the thread state to the sentinel 'NIL' before building the state. If
    // we encounter a 'NIL' while building the state we know there is a cycle.
    thread->set_arta_thread_state((ArtaThreadState*) NIL);
state=new ArtaThreadState(thread);
thread->set_arta_thread_state(state);
    return state;
  } else {
    return state;
  }
}

void ArtaThreadState::all_to_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  int32_t     start  = req->int32_parameter_by_name("start");
  int32_t     stride = req->int32_parameter_by_name("stride");
  int         group  = req->int32_parameter_by_name("bygroup");
  const char *name   = req->parameter_by_name("byname");
  const char *status = req->parameter_by_name("bystatus");

  ArtaThreadFilter filter(group, name, status);

  GrowableArray<ArtaThreadState*> list(64);
  { MutexLockerAllowGC ml(Threads_lock,JavaThread::current());

    // Build a tree of all the threads based on whos waiting to acquire locks
    // owned by who.
    GrowableArray<ArtaThreadState*> roots(64);
    for (JavaThread *thread = Threads::first(); thread != NULL; thread = thread->next()) {
if(!thread->is_hidden_from_external_view()){
        ArtaThreadState *state = get(thread);
        if (state->depth() == 0) roots.append(state);
      }
    }

    // Sorts the roots by the number of children they have. That way highly
    // contented locks show up first in the list.
    roots.sort(compare_roots);

    // Collapse the thread tree into a list and apply the filter.
for(int i=0;i<roots.length();i++){
      roots.at(i)->flatten(list, filter);
    }

    // Clear out the cached thread-state in each thread.
Threads::clear_arta_thread_states();
  }

  // Determine the range of threads to print by normalizing the parameters.
  if (!((1 <= start) && (start < list.length()))) start = 1;
  if (stride <= 0) stride = 250;

  // Print out the range of threads.
  azprof::Xml tag(res, "thread-list");
  azprof::Xml::leaf(res, "start", start);
  azprof::Xml::leaf(res, "stride", stride);
  azprof::Xml::leaf(res, "count", list.length());
  for (int k = 1; k <= list.length(); k++) {
    if ((start <= k) && (k < (start + stride))) {
      list.at(k-1)->to_xml(res);
    }
  }
#endif // AZ_PROFILER
}

int ArtaThreadState::compare_roots(ArtaThreadState **a, ArtaThreadState **b) {
  return ((*a)->child_count() != (*b)->child_count()) ?
           ((*b)->child_count() - (*a)->child_count()) :
           strcasecmp((*a)->name(), (*b)->name());
}

int ArtaThreadState::compare_children(ArtaThreadState **a, ArtaThreadState **b) {
  return strcmp((*a)->name(), (*b)->name());
}

ArtaThreadState::ArtaThreadState(JavaThread *thread) :
  _is_system(false),
_name(NULL),
_message(NULL),
_lock(NULL),
  _lock_id(0),
  _millis(0),
  _depth(0),
  _parent(NULL),
  _children(8)
{
if(thread==NULL)return;

  _is_system = (java_lang_Thread::threadGroup(thread->threadObj()) == Universe::system_thread_group());
  _name      = thread->get_thread_name();
  _id        = thread->osthread()->thread_id();

  // Give precedence to any blocking hint set by Java code.
objectRef msg=thread->hint_blocking_concurrency_msg();
  if( msg.not_null() )
    _message = java_lang_String::as_utf8_string(msg.as_oop());

  // See if a lock is in the hint which indicates any lock being acquired.
objectRef lock=thread->hint_blocking_concurrency_lock();
if(lock.not_null()){
      const symbolOop lock_name = lock.as_oop()->blueprint()->name();
if(lock_name!=NULL){
	  _lock = lock_name->as_klass_external_name();
      } else {
	  _lock = "VM internal klass";
      }
    _lock_id = (uintptr_t) lock.as_oop();
  }

  // See if a synchronizer is in the hint which might indicate who owns any
  // lock being acquired.
objectRef sync=thread->hint_blocking_concurrency_sync();
if(sync.not_null()){
assert(JDK_Version::is_gte_jdk16x_version(),"earlier JDKs use a different interface");
    if( sync.as_oop()->is_a(SystemDictionary::abstract_ownable_synchronizer_klass()) ) {
      oop owner_oop = java_util_concurrent_locks_AbstractOwnableSynchronizer::get_owner_threadObj(sync.as_oop());
      ArtaThreadState *parent = get(java_lang_Thread::thread(owner_oop));
      if (parent != NULL) {
        parent->add_child(this);
      }
    }
  }

  // If a blocking hint wasn't set by Java code use the one set by the VM.
  if (_message == NULL) {
    // Load the blocking hint from the thread. Always set even if not actually
    // blocking.
    _message = thread->status_msg();

    // Find the owner of any monitor the thread is blocked trying to acquire.
    ObjectMonitor *monitor = (ObjectMonitor*) thread->current_pending_monitor();
    if (monitor != NULL) {
      oop lock_oop = monitor->object();

if(lock_oop!=NULL){
        const symbolOop lock_name = lock_oop->blueprint()->name();
if(lock_name!=NULL){
  	      _lock = lock_name->as_klass_external_name();
        } else {
  	      _lock = "VM internal klass";
        }
        _lock_id = (uintptr_t) lock_oop;
  
        JavaThread *owner = Threads::owning_thread_from_monitor_owner((address) monitor->owner(), false);
        if (owner != NULL) {
          ArtaThreadState *parent = get(owner);
          if (parent != NULL) {
            parent->add_child(this);
          }
        }
      }
    }
  }

  // Pull out the counter value from when the hint was set.
  jlong count = thread->time_blocked();
  if (count != 0) {
    _millis = os::ticks_to_millis(os::elapsed_counter() - count);
  }

  // Reconcile potential contradictions between the blocking hint message, lock,
  // and owner. If we're acquiring a monitor or lock then there logically should be
  // a lock and owner. However, since the message, lock, and owner aren't accessed
  // atomically we can see one without the others.
  bool acquiring = (strncasecmp(_message, "acquiring", strlen("acquiring")) == 0);
  bool owned     = ((_lock != NULL) && (_lock_id != 0) && (_parent != NULL));
  if (acquiring && owned) {
    // Everything's consistent because the message starts with "acquiring", we
    // have a lock, and we have an owner.
  } else if ((!acquiring && owned) || (acquiring && !owned)) {
    // We're in an inconsistent state because of a race between loading the
    // message, lock, and/or owner caused by quickly acquiring then releasing a
    // lock.
    char *buf = NEW_RESOURCE_ARRAY(char, strlen(_message) + strlen(" and releasing") + 1);
strcpy(buf,"acquiring and releasing");
    strcpy(buf + strlen("acquiring and releasing"), _message + strlen("acquiring"));
_message=buf;
_lock=NULL;
    _lock_id = 0;
    _parent  = NULL;
    _millis  = 0;
  } else {
    // The message has nothing to do with acquiring monitors or locks. So, clear
    // any lock or owner.
_lock=NULL;
    _lock_id = 0;
_parent=NULL;
  }

  // If the thread is running then it shouldn't have a blocking time.
if(strcmp(_message,"running")==0){
    _millis = 0;
  }
}

void ArtaThreadState::add_child(ArtaThreadState *state) {
  assert0(state->_parent == NULL);
  state->_depth  = _depth + 1;
state->_parent=this;
_children.append(state);
}

void ArtaThreadState::flatten(
  GrowableArray<ArtaThreadState*>& list, ArtaThreadFilter& filter
) {
  // Process this node.
  if (filter.accept(this)) {
list.append(this);
  }

  // Sort the children so that the thread list ordering is stable.
  _children.sort(compare_children);

  // Recursively traverse children.
for(int i=0;i<child_count();i++){
    child_at(i)->flatten(list, filter);
  }
}

void ArtaThreadState::to_xml(azprof::Response *res) {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "thread-state");
  azprof::Xml::leaf(res, "name", name());
  azprof::Xml::leaf(res, "id", id());
  azprof::Xml::leaf(res, "message", message());
if(lock()!=NULL){
    azprof::Xml::leaf(res, "lock", lock());
    azprof::Xml::xleaf(res, "lock_id", lock_id());
  }
  if (millis() != 0) {
    azprof::Xml::leaf(res, "millis", millis());
  }
  if (parent() != NULL) {
    azprof::Xml::leaf(res, "parent", parent()->name());
    azprof::Xml::leaf(res, "parent-id", parent()->id());
  }
  azprof::Xml::leaf(res, "depth", depth());
#endif // AZ_PROFILER
}

////////////////////////////////////////////////////////////////////////////////
// ArtaThreadFilter
////////////////////////////////////////////////////////////////////////////////

ArtaThreadFilter ArtaThreadFilter::NIL;

ArtaThreadFilter::ArtaThreadFilter() :
  _group(2),
_name(NULL),
  _status(NULL) {}

ArtaThreadFilter::ArtaThreadFilter(int group, const char *name, const char *status) :
  _group(group),
  _name((name && (strlen(name) > 0)) ? name : NULL),
  _status((status && (strlen(status) > 0) && (strcmp(status, "any") != 0)) ? status : NULL) {}

bool ArtaThreadFilter::accept(ArtaThreadState *state) {
  return ((group() != 0) || !state->is_system()) &&
         ((group() != 1) || state->is_system()) &&
         (!name()        || strstr(state->name(), name())) &&
         (!status()      || (strncasecmp(state->message(), status(), strlen(status())) == 0));
}
