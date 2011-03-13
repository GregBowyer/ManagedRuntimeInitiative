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
#include "ostream.hpp"
#include "smaHeuristic.hpp"
#include "systemDictionary.hpp"
#include "vmThread.hpp"
#include "vm_operations.hpp"

#include "allocation.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"


// This closure iterates over the monitors and collects stats on which
// monitors are succeeding/failing speculation.  Additionally, we make some 
// generic policy decisions: turn off speculation on monitors that have been
// failing speculation for the last quantum.


class SmaHeuristicMonitorClosure:public MonitorClosure{

public:
  SmaHeuristicMonitorClosure() {}

  
  void do_monitor(ObjectMonitor* mid) {
    oop object = mid->object();
    const char* name;

    bool     advise_spec            = mid->advise_speculation();
    intptr_t sma_success_indicator  = mid->sma_success_indicator();

    mid->set_sma_success_indicator(0); // reset for next time

    if (object != NULL) {
Klass*k=Klass::cast(object->klass());

      if (advise_spec) {
        if (sma_success_indicator <= 0) {
          mid->set_advise_speculation(false);  // speculation wasn't working.  give up for a while.

          // we take '0' as a probable indication of inactivity --
          // don't count it against SMA.  so, only increase abandon
          // count if < 0
          if (sma_success_indicator < 0) {
            object->incr_sma_abandons();
k->increment_klass_sma_failures();
          }

          if (SMATraceModeChanges) {
            tty->print_cr("[SMA mode change %p (speculate -> lock) @ time %.2f]",  mid, os::elapsedTime());
            if (object->sma_abandons() == markWord::max_sma) {
              tty->print_cr("[**** After %d tries, completely gave up on sma on monitor %p ****]", markWord::max_sma, mid);
            }
          }

        } else { // was speculating successfully
k->increment_klass_sma_successes();
        }

        
      } else { // was not speculating
        markWord *mark = mid->object()->mark();
        // we haven't been speculating.  it's been 60 seconds -- let's
        // try again, if the heuristic wants us to
        if (SmaHeuristic::current()->should_speculate(mid, mark, object)) {
          if (SMATraceModeChanges) {
            tty->print_cr("[SMA mode change %p (lock -> speculate) %% time %.2f]", mid, os::elapsedTime());
          }
          mid->set_advise_speculation(true);
        }
      }
    }
  }

};


// --------------- VM_SmaHeuristicAdjuster -----------------------

class VM_SmaHeuristicAdjuster:public VM_Operation{
  // iterate over the monitors and adjust the speculation advises based on a simple heuristic

 public:
  virtual void doit() {
    // iterate
    SmaHeuristicMonitorClosure cl;

ObjectSynchronizer::monitors_iterate(&cl);
  }

VMOp_Type type()const{return VMOp_SmaHeuristicAdjuster;}
  virtual bool        allow_nested_vm_operations() const { return false; }
  
  // we don't need a safepoint
virtual Mode evaluation_mode()const{return _no_safepoint;}
};



// ---------------- SmaHeuristicUpdaterTask --------------------


void SmaHeuristicUpdaterTask::task(){
  ResourceMark rm;
VM_SmaHeuristicAdjuster op;
  VMThread::execute(&op);
}


void SmaHeuristicUpdaterTask::engage(){
  if (!is_active()) {
    // start up the periodic task
    _the_task = new SmaHeuristicUpdaterTask(SMAAdviseReevaluationInterval*1000);
_the_task->enroll();
  }
}

void SmaHeuristicUpdaterTask::disengage(){
  if (!is_active())
    return;
  
  _the_task->disenroll();
  delete _the_task;
_the_task=NULL;
}


SmaHeuristicUpdaterTask* SmaHeuristicUpdaterTask::_the_task = NULL;


// ---------------- SmaHeuristicSamplerTask --------------------


uint64_t SmaHeuristicSamplerTask::smaPerfEvtInterval = 0;
uint64_t SmaHeuristicSamplerTask::smaPerfEvtCount = 0;
uint64_t *SmaHeuristicSamplerTask::smaPerfEvt0PassTotals = NULL;
uint64_t *SmaHeuristicSamplerTask::smaPerfEvt1PassTotals = NULL;
uint64_t *SmaHeuristicSamplerTask::smaPerfEvt0FailTotals = NULL;
uint64_t *SmaHeuristicSamplerTask::smaPerfEvt1FailTotals = NULL;
uint64_t SmaHeuristicSamplerTask::smaPerfEvtReported = 0;

void SmaHeuristicSamplerTask::task(){
  if (!smaPerfEvt0PassTotals ||
      (uint64_t)SMASamplerStart >= ++smaPerfEvtInterval ||
      smaPerfEvtCount >= (uint64_t)SMASamplerLength) return;

  ResourceMark rm;
  uint64_t smaPerfEvt0PassSum = 0;
  uint64_t smaPerfEvt1PassSum = 0;
  uint64_t smaPerfEvt0FailSum = 0;
  uint64_t smaPerfEvt1FailSum = 0;
  
  for (JavaThread *jt = Threads::first(); jt != NULL; jt = jt->next()) {
    smaPerfEvt0PassSum += jt->_sma_perf_evt0_pass;
    smaPerfEvt1PassSum += jt->_sma_perf_evt1_pass;
    smaPerfEvt0FailSum += jt->_sma_perf_evt0_fail;
    smaPerfEvt1FailSum += jt->_sma_perf_evt1_fail;
jt->reset_sma_stats();
  }
  
  smaPerfEvt0PassTotals[smaPerfEvtCount] = smaPerfEvt0PassSum;
  smaPerfEvt1PassTotals[smaPerfEvtCount] = smaPerfEvt1PassSum;
  smaPerfEvt0FailTotals[smaPerfEvtCount] = smaPerfEvt0FailSum;
  smaPerfEvt1FailTotals[smaPerfEvtCount] = smaPerfEvt1FailSum;
  ++smaPerfEvtCount;
}

void SmaHeuristicSamplerTask::engage(){
  if (!is_active() && SMASamplerInterval) {
    smaPerfEvt0PassTotals = new uint64_t[SMASamplerLength];
    smaPerfEvt1PassTotals = new uint64_t[SMASamplerLength];
    smaPerfEvt0FailTotals = new uint64_t[SMASamplerLength];
    smaPerfEvt1FailTotals = new uint64_t[SMASamplerLength];
    
    // start up the periodic task
    _the_task = new SmaHeuristicSamplerTask(SMASamplerInterval*1000);
_the_task->enroll();
  }
}

void SmaHeuristicSamplerTask::disengage(){
  if (!is_active())
    return;
  
  _the_task->disenroll();
  delete _the_task;
_the_task=NULL;
  
  delete smaPerfEvt0PassTotals;
  delete smaPerfEvt1PassTotals;
  delete smaPerfEvt0FailTotals;
  delete smaPerfEvt1FailTotals;
}

void SmaHeuristicSamplerTask::report() {
  uint64_t count = smaPerfEvtCount;
  uint64_t reported = smaPerfEvtReported;
  
  if (!SMASamplerInterval || reported >= count) return;
  
  if (SMASamplerCombineSF) {
tty->print_cr("Evt0      \tEvt1");
    
    for (uint64_t i = reported; i < count; ++i)
      tty->print_cr("%10lu\t%10lu", smaPerfEvt0PassTotals[i] + smaPerfEvt0FailTotals[i],
                                    smaPerfEvt1PassTotals[i] + smaPerfEvt1FailTotals[i]);
  } else {
tty->print_cr("Evt0 Pass \tEvt1 Pass \tEvt0 Fail \tEvt1 Fail");
    
    for (uint64_t i = reported; i < count; ++i)
      tty->print_cr("%10lu\t%10lu\t%10lu\t%10lu", smaPerfEvt0PassTotals[i], smaPerfEvt1PassTotals[i],
                                                  smaPerfEvt0FailTotals[i], smaPerfEvt1FailTotals[i]);
  }
                                                    
smaPerfEvtReported=count;
}

SmaHeuristicSamplerTask* SmaHeuristicSamplerTask::_the_task = NULL;


// -------------------- SMA Heuristics --------------------------


bool SmaBasicHeuristic::should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj) {
assert(UseSMA,"should not call this guy in non-SMA mode");
assert(hdr!=NULL,"hdr should not be NULL");

  bool result;
  bool gave_up = ((hdr == NULL) || (hdr->sma() == markWord::max_sma));
  result = !gave_up;
  return result;
}

bool SmaHashtableHeuristic::should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj) {
assert(UseSMA,"should not call this guy in non-SMA mode");
assert(hdr!=NULL,"hdr should not be NULL");

  bool result;
  bool gave_up = ((hdr == NULL) || (hdr->sma() == markWord::max_sma));

klassOop obj_klass=obj->klass();
  bool favored_class = 
    (obj_klass == SystemDictionary::hashtable_klass()) ||  // java.util.Hashtable
    (obj_klass == SystemDictionary::class_klass());        // java.lang.Class

  result = (!gave_up && favored_class);
  return result;
}

bool SmaClassBasedHeuristic::should_speculate(ObjectMonitor* mid, markWord *hdr, oop obj) {
assert(UseSMA,"should not call this guy in non-SMA mode");
assert(hdr!=NULL,"hdr should not be NULL");
  
  bool gave_up = ((hdr == NULL) || (hdr->sma() == markWord::max_sma));
  if (gave_up) return false;
  
  Klass* k  = Klass::cast(obj->klass());
  uint64_t klass_successes = k->klass_sma_successes();
  uint64_t klass_failures  = k->klass_sma_failures();

  if ((klass_failures + klass_successes) < (uint64_t)min_sample_size) {
    return true; // we haven't seen enough sample points yet.  give SMA a chance.
  } 
  float success_fraction = ((float)klass_successes/(klass_successes+klass_failures));
  return (success_fraction >= success_threshold);
}




SmaHeuristic* SmaHeuristic::_current = NULL;

void SmaHeuristic::initialize(){
if(strcmp(SMAHeuristic,"hashtables")==0){
    _current = new SmaHashtableHeuristic();
}else if(strcmp(SMAHeuristic,"basic")==0){
    _current = new SmaBasicHeuristic();
}else if(strcmp(SMAHeuristic,"classbased")==0){
    _current = new SmaClassBasedHeuristic();
  } else if (strcmp(SMAHeuristic, "") == 0) {
    _current = new SmaClassBasedHeuristic();
  } else { 
vm_exit_during_initialization("Invalid value specified for SMAHeuristic.");
  }
}


