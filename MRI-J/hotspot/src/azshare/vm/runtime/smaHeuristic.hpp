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
#ifndef SMAHEURISTIC_HPP
#define SMAHEURISTIC_HPP


#include "handles.hpp"
#include "synchronizer.hpp"
#include "task.hpp"

class SmaHeuristicUpdaterTask:public PeriodicTask{

public:
SmaHeuristicUpdaterTask(int interval_time):PeriodicTask(interval_time){}
  static void engage();
  static void disengage();
static bool is_active(){return _the_task!=NULL;}

  virtual void task();

private:
  static SmaHeuristicUpdaterTask* _the_task;

};


class SmaHeuristicSamplerTask:public PeriodicTask{

public:
SmaHeuristicSamplerTask(int interval_time):PeriodicTask(interval_time){}
  static void engage();
  static void disengage();
static bool is_active(){return _the_task!=NULL;}
  static void report();

  virtual void task();

private:
  static uint64_t smaPerfEvtInterval;
  static uint64_t smaPerfEvtCount;
  static uint64_t *smaPerfEvt0PassTotals;
  static uint64_t *smaPerfEvt1PassTotals;
  static uint64_t *smaPerfEvt0FailTotals;
  static uint64_t *smaPerfEvt1FailTotals;
  static uint64_t smaPerfEvtReported;

  static SmaHeuristicSamplerTask* _the_task;

};


class SmaHeuristic {
  
public:
  // This method is called:
  //   - when a monitor is inflated
  //   - when a monitor has had speculation deactivated temporarily and after 
  //     some passage of time we want to reevaluate whether to speculate
  // Based on the object and the header word, the heuristic should make a decision 
  // as to whether we should speculate on this monitor again.  
  virtual bool should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj) = 0;  
  virtual const char* name() = 0;
  
private:
  static SmaHeuristic* _current;

public:
  static SmaHeuristic* current()   { return _current; }
  static void initialize();

};



class SmaBasicHeuristic : public SmaHeuristic {
  // this heuristic decides whether to continue speculation based only
  // on the number of times we have previously 'abandoned' sma.  if it
  // exceeds a threshold, we never try sma on the monitor again.
public:
virtual const char*name(){return"'basic' SMA Heuristic";}
  virtual bool should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj);
};



class SmaHashtableHeuristic : public SmaHeuristic {
  // this heuristic only lets hashtables (and a few other known
  // classes) use speculative locking
public:
virtual const char*name(){return"'hashtables' SMA Heuristic";}
  virtual bool should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj);
};


class SmaClassBasedHeuristic : public SmaHeuristic {
  // this heuristic keeps statistics on the success of speculation for
  // different monitors based on their objects' classes.
private:
NEEDS_CLEANUP;//make these parameterizable
  const static float success_threshold = 0.70F;
  const static int   min_sample_size   = 15;

public:
virtual const char*name(){return"'classbased' SMA Heuristic";}
  virtual bool should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj);
};

#endif // SMAHEURISTIC_HPP
