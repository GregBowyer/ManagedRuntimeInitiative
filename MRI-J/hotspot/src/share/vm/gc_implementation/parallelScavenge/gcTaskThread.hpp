/*
 * Copyright 2002-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef GCTASKTHREAD_HPP
#define GCTASKTHREAD_HPP


#include "thread.hpp"
// Forward declarations of classes defined here.
class GCTaskThread;
class GCTaskTimeStamp;

// Declarations of classes referenced in this file via pointer.
class GCTaskManager;

class GCTaskThread : public WorkerThread {
public:
  GCTaskTimeStamp* _time_stamps;
  uint _time_stamp_index;
private:
  // Instance state.
  GCTaskManager* _manager;              // Manager for worker.

  // State for GPGC:
  long           _thread_type;
  long           _thread_number;
  long           _wakeup_count;
  
  // Trap handler.
  bool          _new_gc_mode_requested;
  bool          _new_gc_mode;

  // Pre-allocated page for pauseless GC
  PageNum       _preallocated_page;

 public:
  // Factory create and destroy methods.
  static GCTaskThread* create(GCTaskManager* manager,
uint which){
    return new (ttype::java_thread) GCTaskThread(manager, which);
  }
  static void destroy(GCTaskThread* manager) {
    if (manager != NULL) {
      delete manager;
    }
  }
  // Methods from Thread.
  bool is_GC_thread() const {
    return true;
  }
  
  bool is_GC_task_thread() const {
    return true;
  }
  virtual void run();
  // Methods.
  void start();

  void print_task_time_stamps();
  void print_on(outputStream* st) const;
  void print() const				    { print_on(tty); }
  
  // Accessors.
  uint which() const {
    return id();
  }
  bool new_gc_mode_requested() {
    return _new_gc_mode_requested;
  }
  bool new_gc_mode () {
    return _new_gc_mode;
  }

  // The GCTaskThread to update its trap table before running another GCTask.
  void request_new_gc_mode(bool gc_mode);

  void set_new_gc_mode_requested (bool new_gc_mode_requested) {
    _new_gc_mode_requested = new_gc_mode_requested;
  }

  // Pre-allocated page for GPGC and PGC.
  void    set_preallocated_page(PageNum page) { _preallocated_page = page; }
  PageNum get_preallocated_page()             { return _preallocated_page; }

  // Thread info for GPGC:
  void    set_thread_type      (long t)       { _thread_type = t; }
  void    set_thread_number    (long n)       { _thread_number = n; }
  void    set_wakeup_count     (long c)       { _wakeup_count = c; }
  long    thread_type          ()             { return _thread_type; }
  long    thread_number        ()             { return _thread_number; }
  long    wakeup_count         ()             { return _wakeup_count; }
  GCTaskTimeStamp* time_stamp_at(uint index);

protected:
  // Constructor.  Clients use factory, but there could be subclasses.
GCTaskThread(GCTaskManager*manager,uint which);
  // Destructor: virtual destructor because of virtual methods.
  virtual ~GCTaskThread();
  // Accessors.
  GCTaskManager* manager() const {
    return _manager;
  }
};

class GCTaskTimeStamp : public CHeapObj
{
 private:
  jlong  _entry_time;
  jlong  _exit_time;
const char*_name;

 public:
  jlong entry_time()              { return _entry_time; }
  jlong exit_time()               { return _exit_time; }
  const char* name() const        { return (const char*)_name; }

  void set_entry_time(jlong time) { _entry_time = time; }
  void set_exit_time(jlong time)  { _exit_time = time; }
void set_name(const char*name){_name=name;}
};

#endif // GCTASKTHREAD_HPP
