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
#ifndef HEAPITERATOR_HPP
#define HEAPITERATOR_HPP


#include "allocation.hpp"
#include "thread.hpp"
class ObjectClosure;
class PGCTaskManager;

// Azul - Make the heap iteration multi-threaded
//

class HeapIterator:public StackObj{
 private:
  static PGCTaskManager*   _heap_iterator_task_manager;
  static bool              _should_initialize;

 public:
  static void              initialize            ();
  static void              set_should_initialize () { _should_initialize = true; }
  static bool              should_initialize     () { return _should_initialize; }
  static void              heap_iterate          (ObjectClosure* closure);
};

class HeapIteratorThread:public NamedThread{
 private:
  uint _id;

 public:
  HeapIteratorThread(uint id);

  uint id() { return _id; }
  bool is_heap_iterator_thread() const { return true; }
  virtual void run(); 

  // Printing
  void print() const;
  void print_xml_on(xmlBuffer *xb, bool ref);
};
#endif
