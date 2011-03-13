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

#include "mutexLocker.hpp"
#include "statistics.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "thread_os.inline.hpp"

Statistics* Statistics::_stats = NULL;


void Statistics::register_statistics(Statistics* stats) {
  assert0(stats != NULL);
MutexLocker ml(Statistics_lock);
Statistics*prev=NULL;
  Statistics* cur = _stats;
  while (cur != NULL) {
    prev = cur; cur = cur->next();
  }
stats->set_next(NULL);
  if (prev != NULL) {
    assert0(prev->next() == NULL);
prev->set_next(stats);
  } else {
    assert0(_stats == NULL);
    _stats = stats;
  }
}


void Statistics::unregister_statistics(Statistics* stats) {
  Unimplemented();
}


void Statistics::stats_print_on(outputStream*st){
#ifndef PRODUCT
MutexLocker ml(Statistics_lock);
  Statistics *cur = _stats;
  while (cur != NULL) {
    cur->print_on(st);
    cur = cur->next();
  }
#endif
}


void Statistics::stats_print_xml_on(xmlBuffer* xb, bool ref) {
MutexLocker ml(Statistics_lock);
  Statistics *cur = _stats;
  xmlElement x(xb,"statistics");
  while (cur != NULL) {
    xmlElement xe(xb, "stat");
    xb->name_value_item("name", cur->name());
    xb->name_ptr_item("id", cur);
    cur->print_xml_on(xb, ref);
    cur = cur->next();
  }
}


SystemDictionaryStats* SystemDictionaryStats::_singleton = NULL;

void SystemDictionaryStats::reset(){
}

void SystemDictionaryStats::print_on(outputStream*st)const{
}

void SystemDictionaryStats::print_xml_on(xmlBuffer* xb, bool ref) {
  xmlElement xe(xb, "name_value_table");
  xb->name_value_item("loaded_classes", SystemDictionary::number_of_classes());
}

ThreadCountStats* ThreadCountStats::_singleton = NULL;

void ThreadCountStats::reset(){
}

void ThreadCountStats::print_on(outputStream*st)const{
}

void ThreadCountStats::print_xml_on(xmlBuffer* xb, bool ref) {
  xmlElement xe(xb, "name_value_table");
  xb->name_value_item("running_threads", os::_os_thread_count);
}

void statistics_init() {
SystemDictionaryStats::init();
ThreadCountStats::init();
}
