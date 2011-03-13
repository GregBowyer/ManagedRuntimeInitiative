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
#ifndef STATISTICS_HPP
#define STATISTICS_HPP

#include "allocation.hpp"
#include "ostream.hpp"

class Statistics:public CHeapObj{
 private:
  // Allow queueing of different statistics modules.
Statistics*_next;

Statistics*next()const{return _next;}
void set_next(Statistics*next){_next=next;}
  
  // The global list of statistics.
  static Statistics* _stats;

 public:
  virtual const char* name() = 0;
  virtual void reset() = 0;
  
  virtual void print_xml_on(xmlBuffer* xb, bool ref) = 0;
  
  // Handle registration and removal.
  static void register_statistics(Statistics* stats);
  static void unregister_statistics(Statistics* stats);

  // Printing of all registered statistics.
  static void stats_print()   { stats_print_on(tty); }
static void stats_print_on(outputStream*st);
  static void stats_print_xml_on(xmlBuffer* xb, bool ref);
};

// Define a couple statistic classes here if they don't happen to have a better home.

class SystemDictionaryStats: Statistics {
 private:
  static SystemDictionaryStats* _singleton;

 public:
  SystemDictionaryStats() {
  }

  const char* name() {
return"SystemDictionary Stats";
  }

  void reset();

  void print_on(outputStream* st) const;
  void print() const {
    print_on(tty);
  }
  void print_xml_on(xmlBuffer* xb, bool ref);

  static void init() {
    _singleton = new SystemDictionaryStats();
    Statistics::register_statistics(_singleton);
  }
};


class ThreadCountStats: Statistics {
private:
  static ThreadCountStats* _singleton;
  
public:
  ThreadCountStats() {
  }
  
  const char* name() {
return"Thread Count Stats";
  }
  
  void reset();
  
  void print_on(outputStream* st) const;
  void print() const {
    print_on(tty);
  }
  void print_xml_on(xmlBuffer* xb, bool ref);
  
  static void init() {
    _singleton = new ThreadCountStats();
    Statistics::register_statistics(_singleton);
  }
};

#endif // STATISTICS_HPP
