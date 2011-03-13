/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef AGETABLE_HPP
#define AGETABLE_HPP


/* Copyright 1992 Sun Microsystems, Inc. and Stanford University.
   See the LICENSE file for license information. */
#include "markWord.hpp"
#include "perfData.hpp"

// Age table for adaptive feedback-mediated tenuring (scavenging)
//
// Note: all sizes are in oops

class ageTable VALUE_OBJ_CLASS_SPEC {
 public:
  // constants
enum{table_size=markWord::max_age+1};

  // instance variables
  size_t sizes[table_size];

  // constructor.  "global" indicates that this is the global age table
  // (as opposed to gc-thread-local)
  ageTable(bool global = true);

  // clear table
  void clear();

  // add entry
  void add(oop p, size_t oop_size) {
    int age = p->age();
    assert(age > 0 && age < table_size, "invalid age of object");
    sizes[age] += oop_size; 
  }

  // Merge another age table with the current one.  Used
  // for parallel young generation gc.
  void merge(ageTable* subTable);

  // calculate new tenuring threshold based on age information
  int compute_tenuring_threshold(size_t survivor_capacity);

 private:
  PerfVariable* _perf_sizes[table_size];
};

#endif // AGETABLE_HPP

