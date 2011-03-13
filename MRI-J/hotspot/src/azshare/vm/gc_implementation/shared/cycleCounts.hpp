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

#ifndef CYCLECOUNTS_HPP
#define CYCLECOUNTS_HPP

#include "allocation.hpp"
#include "os.hpp"

class CycleCounts VALUE_OBJ_CLASS_SPEC
{
  public:
    static CycleCounts commit_memory;
    static CycleCounts protect_memory;
    static CycleCounts relocate_memory;
    static CycleCounts uncommit_memory;
    static CycleCounts tlb_resync;
    static CycleCounts unshatter_all;
    static CycleCounts partial_unshatter;
    static CycleCounts mbatch_start;
    static CycleCounts mbatch_commit;

  private:
    long cycles;
    long counts;

    enum Constants {
      MaxBucket = 70
    };

    long bucket_start [MaxBucket];
    long cycle_buckets[MaxBucket];

  public:
    CycleCounts();

    void reset    ();
    void add_datum(long datum);
void print(const char*title);

    static void print_all();
    static void reset_all();
};


class CycleCounter: StackObj
{
  private:
    bool         _count;
    CycleCounts* _cc;
    long         _start_cycle;

  public:
    CycleCounter(bool count, CycleCounts* cc) {
_count=count;
      if (_count) {
        _cc          = cc;
_start_cycle=os::elapsed_counter();
      } else {
        _cc=0;
        _start_cycle=0;
      }
    }

    ~CycleCounter() {
      if (_count) {
        _cc->add_datum(os::elapsed_counter()-_start_cycle);
      }
    }
};

#endif // CYCLECOUNTS_HPP

