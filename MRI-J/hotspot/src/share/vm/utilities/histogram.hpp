/*
 * Copyright 1998-2000 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef HISTOGRAM_HPP
#define HISTOGRAM_HPP

#include <growableArray.hpp>

// This class provides a framework for collecting various statistics.
// The current implementation is oriented towards counting invocations
// of various types, but that can be easily changed.
//
// To use it, you need to declare a Histogram*, and a subtype of
// HistogramElement:
//
//  HistogramElement* MyHistogram;
//
//  class MyHistogramElement : public HistogramElement {
//    public:
//      MyHistogramElement(char* name);
//  };
//
//  MyHistogramElement::MyHistogramElement(char* elementName) {
//    _name = elementName;
//
//    if(MyHistogram == NULL)
//      MyHistogram = new Histogram("My Call Counts",100);
//
//    MyHistogram->add_element(this);
//  }
//
//  #define MyCountWrapper(arg) static MyHistogramElement* e = new MyHistogramElement(arg); e->increment_count()
//
// This gives you a simple way to count invocations of specfic functions:
//
// void a_function_that_is_being_counted() {
//   MyCountWrapper("FunctionName");
//   ...
// }
//
// To print the results, invoke print() on your Histogram*.

#ifdef ASSERT

class HistogramElement : public CHeapObj {
 protected:
intptr_t _count;
  const char* _name;

 public:
  HistogramElement();
virtual intptr_t count();
  virtual const char* name();
  virtual void increment_count();
  void print_on(outputStream* st) const;
  virtual int compare(HistogramElement* e1,HistogramElement* e2);
};

class Histogram : public CHeapObj {
 protected:
  GrowableArray<HistogramElement*>* _elements;
  GrowableArray<HistogramElement*>* elements() { return _elements; }
  const char* _title;
  const char* title() { return _title; }
  static int sort_helper(HistogramElement** e1,HistogramElement** e2);
  virtual void print_header(outputStream* st);
  virtual void print_elements(outputStream* st);

 public:
  Histogram(const char* title,int estimatedSize);
  virtual void add_element(HistogramElement* element);
  void print_on(outputStream* st) const;
};

#endif

#endif //  HISTOGRAM_HPP
