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
#ifndef ARTATHREADSTATE_HPP
#define ARTATHREADSTATE_HPP

#include <allocation.hpp>
#include <growableArray.hpp>
class JavaThread;
class ArtaThreadFilter;

// Displayed state of a thread. A message describing its status (running, sleeping, acquiring a
// lock, etc.), any lock waiting to be acquired, an owner of a lock waiting to be acquired, and
// the duration that the thread has been in this state.
class ArtaThreadState:public ResourceObj{

  // Sentinel used to detect cycles while building a thread state tree.
  static const ArtaThreadState* const NIL;

  bool                              _is_system; // whether the thread is in the system group
const char*_name;//thread name
  uint64_t                          _id;        // thread ID
  const char                       *_message;   // status message explaining the thread state
  const char                       *_lock;      // class name of the lock being acquired
  uintptr_t                         _lock_id;   // address of the lock being acquired
  jlong                             _millis;    // milliseconds spent in this state so far

int _depth;//cached depth of a thread in the tree
  ArtaThreadState                  *_parent;    // thread owning any lock being acquired
  GrowableArray<ArtaThreadState*>   _children;  // threads waiting on this thread

public:
  // Gets the state of a thread. Constructing it if it hasn't been yet. If a
  // cycle is detected then NULL is returned to break it.
static ArtaThreadState*get(JavaThread*thread);

  // Prints the state of all threads.
  static void all_to_xml(azprof::Request *req, azprof::Response *res);

  bool             is_system()   const { return _is_system; }
  const char*      name()        const { return _name; }
  uint64_t         id()          const { return _id; }
  const char*      message()     const { return _message; }
  const char*      lock()        const { return _lock; }
  uintptr_t        lock_id()     const { return _lock_id; }
  jlong            millis()      const { return _millis; }
  int              depth()       const { return _depth; }
ArtaThreadState*parent(){return _parent;}
  int              child_count() const { return _children.length(); }
ArtaThreadState*child_at(int i){return _children.at(i);}

  // Add a thread as a child of another implying that it's waiting to acquire a
  // lock owned by its parent thread.
  void add_child(ArtaThreadState *state);

  // Prints the state of a single thread.
  void to_xml(azprof::Response *res);

private:
  static int compare_roots(ArtaThreadState **a, ArtaThreadState **b);
  static int compare_children(ArtaThreadState **a, ArtaThreadState **b);

  // Construct a threads state inspeciting if it's not null.
ArtaThreadState(JavaThread*thread);

  // Flatten the tree into a list.
  void flatten(GrowableArray<ArtaThreadState*>& list, ArtaThreadFilter& filter);
};

// Filter used to determine which threads to print in a response.
class ArtaThreadFilter:public StackObj{
public:
  static ArtaThreadFilter NIL;

  static ArtaThreadFilter* nil() {return &NIL;}

  ArtaThreadFilter();
  ArtaThreadFilter(int group, const char *name, const char *status);

  int         group()  const { return _group; }
  const char *name()   const { return _name; }
  const char *status() const { return _status; }

  bool accept(ArtaThreadState *state);

private:
  int         _group;
  const char *_name;
  const char *_status;
};

#endif // ARTATHREADSTATE_HPP
