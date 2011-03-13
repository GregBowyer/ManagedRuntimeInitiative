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


#include "gpgc_collector.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_heuristic.hpp"
#include "gpgc_layout.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_relocation.hpp"
#include "gpgc_space.hpp"
#include "gpgc_thread.hpp"
#include "log.hpp"
#include "timer.hpp"

#include "os_os.inline.hpp"

double GPGC_Heuristic::_new_gen_alloc_words_per_sec  = 0.0;
double GPGC_Heuristic::_old_gen_alloc_words_per_sec  = 0.0;
double GPGC_Heuristic::_perm_gen_alloc_words_per_sec = 0.0;

double GPGC_Heuristic::_new_gc_cycle_seconds         = 0.0;
double GPGC_Heuristic::_old_gc_cycle_seconds         = 0.0;

double GPGC_Heuristic::_last_sample_new_gen_words    = 0.0;
long   GPGC_Heuristic::_last_sample_new_gen_ticks    = 0;
long   GPGC_Heuristic::_last_sample_old_gen_ticks    = 0;
double GPGC_Heuristic::_last_sample_perm_gen_words   = 0.0;
long   GPGC_Heuristic::_last_sample_perm_gen_ticks   = 0;

size_t GPGC_Heuristic::_cycle_model_biggest_heap     = 0;
size_t GPGC_Heuristic::_cycle_model_biggest_old      = 0;


static long heuristic_interval_secs = 1;


// Calculate an exponential average.  The 'alpha' parameter says what percent of the
// result is contributed by the prior value.
double GPGC_Heuristic::exp_average(double old_average, double new_value, double alpha)
{
  assert0(alpha>0.0 && alpha<100.0);

  return (alpha*old_average) + ((1.0-alpha)*new_value);
}


void GPGC_Heuristic::start_allocation_model()
{
_last_sample_new_gen_ticks=os::elapsed_counter();
_last_sample_old_gen_ticks=os::elapsed_counter();
_last_sample_perm_gen_ticks=os::elapsed_counter();

  _last_sample_new_gen_words  = GPGC_Space::new_gen_words_used();
  _last_sample_perm_gen_words = GPGC_Space::perm_gen_words_used();
}


// Calculate the alpha to use in an exponential average when a certain data half-life
// is desired and the update rate is at a given interval.   For example: You might
// want to discount half the allocation rate over 1 hour, and the allocation rate
// might be updated every 20 seconds, so you'd call alpha_for_half_life(3600, 20).
double GPGC_Heuristic::alpha_for_half_life(double half_life, double interval)
{
  double updates  = half_life / interval;
  double exponent = 1/updates;
  double alpha    = pow(double(0.5), exponent);

  return alpha;
}


// This should be called by NewGC each time pages are promoted into OldGen.
void GPGC_Heuristic::update_old_gen_allocation_model(long promoted_pages)
{
  long   now           = os::elapsed_counter();
  double frequency     = os::elapsed_frequency();
  double elapsed_secs  = double(now - _last_sample_old_gen_ticks) / frequency;
  double words_per_sec = double(promoted_pages << LogWordsPerGPGCPage) / elapsed_secs;

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  if ( promoted_pages > 0 ) {
    if ( words_per_sec < _old_gen_alloc_words_per_sec ) {
      // TODO: should we ignore alloc rates close to zero, instead of pushing down the alloc model?
      // TODO: should we ignore low alloc rates when mutators were blocked waiting for GC?
      double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_secs);
      double new_model = exp_average(_old_gen_alloc_words_per_sec, words_per_sec, alpha);
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H OldGen: %.1f MB/sec : sample of %.1f MB/sec over %.3f secs, prior model %.1f MB/sec",
                        new_model / (M>>LogBytesPerWord), words_per_sec / (M>>LogBytesPerWord),
                        elapsed_secs, _old_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
      _old_gen_alloc_words_per_sec = new_model;
    } else {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H OldGen: %.1f MB/sec : new high replaces prior model of %.1f MB/sec",
                        words_per_sec / (M>>LogBytesPerWord), _old_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
      _old_gen_alloc_words_per_sec = words_per_sec;
    }
  } else {
    // Don't update the OldGen allocation model for cycles where nothing is promoted.
  }

_last_sample_old_gen_ticks=now;
}


// This should be called by NewGC at the end of each NewGC cycle to report the observed NewGen
// allocation rate during the GC cycle.
void GPGC_Heuristic::update_new_gen_allocation_model(double MB_per_sec, double elapsed_secs)
{
  long   current_words_used = GPGC_Space::new_gen_words_used();
  long   now                = os::elapsed_counter();
  double words_per_sec      = MB_per_sec * (M>>LogBytesPerWord);

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  if ( words_per_sec < _new_gen_alloc_words_per_sec ) {
    // TODO: should we ignore alloc rates close to zero, instead of pushing down the alloc model?
    // TODO: should we ignore low alloc rates when mutators were blocked waiting for GC?
    double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_secs);
    double new_model = exp_average(_new_gen_alloc_words_per_sec, words_per_sec, alpha);
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGen: %.1f MB/sec : sample of %.1f MB/sec over %.3f secs, prior model %.1f MB/sec",
                      new_model / (M>>LogBytesPerWord), MB_per_sec, elapsed_secs,
                      _new_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
    _new_gen_alloc_words_per_sec = new_model;
  } else {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGen: %.1f MB/sec : new high replaces prior model of %.1f MB/sec",
                      MB_per_sec, _new_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
    _new_gen_alloc_words_per_sec = words_per_sec;
  }

_last_sample_new_gen_ticks=now;
  _last_sample_new_gen_words = current_words_used;
}


// This should be called by the NewGC GPGC_Thread periodically while it waits for the GPGC_Heuristic
// to indicate that a NewGC cycle is called for.
void GPGC_Heuristic::update_new_gen_allocation_model()
{
  long   current_words_used = GPGC_Space::new_gen_words_used();
  long   now                = os::elapsed_counter();
  double frequency          = os::elapsed_frequency();
  double elapsed_secs       = double(now - _last_sample_new_gen_ticks) / frequency;
  double words_per_sec      = double(current_words_used - _last_sample_new_gen_words) / elapsed_secs;

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  // Don't update the allocation rate across very short sample intervals.  The allocation_rate will
  // be too volatile over short periods of time. 
  if ( elapsed_secs < GPGCHeuristicMinSampleSecs ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGen: %.1f MB/sec : sample of %.1f MB/sec over %.3f secs; no update, time under %d sec",
                      _new_gen_alloc_words_per_sec / (M>>LogBytesPerWord),
                      words_per_sec                / (M>>LogBytesPerWord),
                      elapsed_secs,
                      GPGCHeuristicMinSampleSecs);
    return;
  }

  if ( words_per_sec < _new_gen_alloc_words_per_sec ) {
    // TODO: should we ignore alloc rates close to zero, instead of pushing down the alloc model?
    // TODO: should we ignore low alloc rates when mutators were blocked waiting for GC?
    double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_secs);
    double new_model = exp_average(_new_gen_alloc_words_per_sec, words_per_sec, alpha);
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGen: %.1f MB/sec : sample of %.1f MB/sec over %.3f secs, prior model %.1f MB/sec",
                      new_model     / (M>>LogBytesPerWord),
                      words_per_sec / (M>>LogBytesPerWord),
                      elapsed_secs,
                      _new_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
    _new_gen_alloc_words_per_sec = new_model;
  } else {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGen: %.1f MB/sec : new high replaces prior model of %.1f MB/sec",
                      words_per_sec                / (M>>LogBytesPerWord),
                      _new_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
    _new_gen_alloc_words_per_sec = words_per_sec;
  }

_last_sample_new_gen_ticks=now;
  _last_sample_new_gen_words = current_words_used;
}


// This should be called by the NewGC GPGC_Thread periodically while it waits for the GPGC_Heuristic
// to indicate that a NewGC cycle is called for.  This function may get called while the OldGC is
// compacting PermGen, and so it will ignore allocation rate samples that are negative.
void GPGC_Heuristic::update_perm_gen_allocation_model()
{
  long   current_words_used = GPGC_Space::perm_gen_words_used();
  long   now                = os::elapsed_counter();
  double frequency          = os::elapsed_frequency();
  double elapsed_secs       = double(now - _last_sample_perm_gen_ticks) / frequency;
  double words_per_sec      = double(current_words_used - _last_sample_perm_gen_words) / elapsed_secs;

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  // Don't update the allocation rate across very short sample intervals.  The allocation_rate will
  // be too volatile over short periods of time. 
  if ( elapsed_secs < GPGCHeuristicMinSampleSecs ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H PermGen: %.1f MB/sec : sample of %1.f MB/sec over %.3f secs; no update, time under %d secs",
                      _perm_gen_alloc_words_per_sec / (M>>LogBytesPerWord),
                      words_per_sec                 / (M>>LogBytesPerWord),
                      elapsed_secs,
                      GPGCHeuristicMinSampleSecs);
    return;
  }

  if ( words_per_sec >= 0.0 ) {
    if ( words_per_sec < _perm_gen_alloc_words_per_sec ) {
      // TODO: should we ignore alloc rates close to zero, instead of pushing down the alloc model?
      // TODO: should we ignore low alloc rates when mutators were blocked waiting for GC?
      double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_secs);
      double new_model = exp_average(_perm_gen_alloc_words_per_sec, words_per_sec, alpha);
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H PermGen: %.1f MB/sec : sample of %.1f MB/sec over %.3f secs, prior model %.1f MB/sec",
                        new_model     / (M>>LogBytesPerWord),
                        words_per_sec / (M>>LogBytesPerWord),
                        elapsed_secs,
                        _perm_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
      _perm_gen_alloc_words_per_sec = new_model;
    } else {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H PermGen: %.1f MB/sec : new high replaces prior model of %.1f MB/sec",
                        words_per_sec                 / (M>>LogBytesPerWord),
                        _perm_gen_alloc_words_per_sec / (M>>LogBytesPerWord));
      _perm_gen_alloc_words_per_sec = words_per_sec;
    }
  } else {
    // Ignore negative alloc rates, they're because OldGC is compacting PermGen.
  }

_last_sample_perm_gen_ticks=now;
  _last_sample_perm_gen_words = current_words_used;
}


// Called by NewGC at the end of each NewGC cycle.
void GPGC_Heuristic::update_new_gc_cycle_model(GPGC_CycleStats* stats)
{
  double frequency       = os::elapsed_frequency();
  double elapsed_seconds = double(stats->cycle_ticks()) / frequency;

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  // Update the NewGC cycle time model.
  if ( elapsed_seconds < _new_gc_cycle_seconds ) {
    double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_seconds);
    double new_model = exp_average(_new_gc_cycle_seconds, elapsed_seconds, alpha);
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGC: %.3f secs : last cycle %.3f secs, prior model %.3f secs",
                      new_model, elapsed_seconds, _new_gc_cycle_seconds);
    _new_gc_cycle_seconds = new_model;
  } else {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H NewGC: %.3f secs : new slow cycle replaces prior model of %.3f secs",
                      elapsed_seconds, _new_gc_cycle_seconds);
    _new_gc_cycle_seconds = elapsed_seconds;
  }

  // Track how full of a heap we've seen so far, so cycles can be triggered
  // to update the cycle time models when we get to a bigger heap than we've
  // seen before.

  size_t starting_heap_words = stats->start_words_in_use();
  if ( starting_heap_words > _cycle_model_biggest_heap ) {
    _cycle_model_biggest_heap = starting_heap_words;
  }
}


// Called by OldGC at the end of each OldGC cycle.
void GPGC_Heuristic::update_old_gc_cycle_model(GPGC_CycleStats* stats)
{
  double frequency       = os::elapsed_frequency();
  double elapsed_seconds = double(stats->cycle_ticks()) / frequency;

  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

  size_t starting_old_words = stats->start_old_words_in_use() + stats->start_perm_words_in_use();
  if ( starting_old_words > _cycle_model_biggest_old ) {
    _cycle_model_biggest_old = starting_old_words;
  }

  if ( elapsed_seconds < _new_gc_cycle_seconds ) {
    double alpha     = alpha_for_half_life(GPGCHeuristicHalfLifeMins*60, elapsed_seconds);
    double new_model = exp_average(_old_gc_cycle_seconds, elapsed_seconds, alpha);
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H OldGC: %.3f secs : last cycle %.3f secs, prior model %.3f secs, starting old %d MB",
                      new_model, elapsed_seconds, _old_gc_cycle_seconds,
                      (starting_old_words << LogBytesPerWord) / M);
    _old_gc_cycle_seconds = new_model;
  } else {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H OldGC: %.3f secs : new slow cycle replaces prior model of %.3f secs, starting old %d MB",
                      elapsed_seconds, _old_gc_cycle_seconds,
                      (starting_old_words << LogBytesPerWord) / M);
    _old_gc_cycle_seconds = elapsed_seconds;
  }
}


// This is called by the NewGC after each cycle.  It should return true if a condition has
// occurred that requires back-to-back NewGC cycles until a new OldGC can be started.
bool GPGC_Heuristic::need_immediate_old_gc()
{
  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  GPGC_CycleStats* cycle_stats = heap->gc_stats()->new_gc_cycle_stats();

  // If the prior NewGC was sideband space limited, and OldGC is using more than 
  // GPGCOldGCSidebandTrigger percent of sideband, trigger an OldGC.
  if ( cycle_stats->sideband_words_limited() > 0 ) {
    long trigger    = long(GPGC_PageRelocation::trigger_old_gc());
    long old_gc_end = long(GPGC_PageRelocation::old_gc_end());
    if ( old_gc_end <= trigger ) {
      long bottom       = long(GPGC_PageRelocation::top());
      long top          = long(GPGC_PageRelocation::bottom());
      long total_size   = top - bottom;
      long used_by_old  = top - old_gc_end;
      long percent_used = (used_by_old * 100) / total_size;

      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H immediate old GC cycle: NewGC limited with OldGC using %d%% of sideband",
                        percent_used);
      return true;
    }
  }

  return false;
}


// Return the number of milliseconds until the new GC cycle should start.
long GPGC_Heuristic::time_until_gc(bool always_log)
{
  BufferedLoggerMark m(NOTAG, Log::M_GC, GPGCTraceHeuristic, false, gclog_or_tty);

GPGC_Heap*heap=(GPGC_Heap*)Universe::heap();
  GPGC_CycleStats* cycle_stats     = heap->gc_stats()->new_gc_cycle_stats();
  GPGC_CycleStats* old_cycle_stats = heap->gc_stats()->old_gc_cycle_stats();

  long committed_budget  = GPGC_PageBudget::committed_budget();
  long normal_pages_used = GPGC_PageBudget::normal_pages_used();
  long pause_pages_used  = GPGC_PageBudget::pause_pages_used();
  long azmem_pause_pages = GPGC_PageBudget::azmem_pause_pages();


  //***                                                            ***//
  //***  Check conditions that demand another OldGC be triggered,  ***//
  //***  waiting until the current one completes if necessary.     ***//
  //***                                                            ***//

  if ( pause_pages_used > 0 ) {
    // If the last GC cycle used more pause pages then it was able to return, we should be
    // immediately starting the next GC cycle.
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H immediate GC cycle: %d pause prevention pages still in use",
                      pause_pages_used);
    GPGC_Thread::heuristic_demands_full_gc();
    return 0;
  }

  //
  //  If the prior GC cycle had app delays because we ran out of memory, immediately trigger a full GC cycle.
  //

  if ( cycle_stats->get_threads_delayed() > 0 ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : prior cycle had %d threads delayed waiting for GC",
                      cycle_stats->get_threads_delayed());
    GPGC_Thread::heuristic_demands_full_gc();
    return 0;
  }


  //***                                                                            ***//
  //***  Check conditions that trigger an OldGC if one isn't already in progress.  ***//
  //***                                                                            ***//

  //
  //  First check for an immediate GC cycles because of excess memory usage.
  // 

  if ( normal_pages_used >= committed_budget ) {
    // When we bump up against the committed page budget, we should be doing back to back GC cycles.
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H immediate GC cycle: %d normal pages allocated, committed budget is %d",
                      normal_pages_used, committed_budget);
    GPGC_Thread::heuristic_wants_full_gc(true);
    return 0;
  }

  size_t     max_heap_words  = heap->max_heap_size_specified() >> LogBytesPerWord;
  size_t     used_old_words  = GPGC_Space::old_gen_words_used() + GPGC_Space::perm_gen_words_used();
  size_t     used_heap_words = GPGC_Space::new_gen_words_used() + used_old_words;

  if ( used_heap_words > max_heap_words ) {
    // We may be using more heap than was originally specified on the command line, due to
    // expanding into the grant or pause-prevention funds.  Which is fine, but we should be
    // running back-to-back GC cycles when that occurs.

    // TODO:  Shouldn't this be caught by the normal_pages_used >= committed_budget test above?
    //        Re-enable this assert after we straighten out the memory accounting in GPGC.
    // assert(false, "shouldn't this be caught by a prior test?");

    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H immediate GC cycle: used (%d MB) is greater than max (%d MB)",
                      (used_heap_words << LogBytesPerWord)/M,
                      (max_heap_words << LogBytesPerWord)/M);
    GPGC_Thread::heuristic_wants_full_gc(true);
    return 0;
  }

  //
  //  Check for an immediate GC cycle due to page budget stats.
  // 

  if ( azmem_pause_pages > 0 ) {
    // If azmem still has pages in the process pool allocated from the global pause fund, then
    // we better run another GC cycle immediately.
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H immediate GC cycle: %d azmem pause pages still allocated",
                      azmem_pause_pages);
    GPGC_Thread::heuristic_wants_full_gc(false);
    return 0;
  }

  //
  //  Check to see if incremental heap usage should trigger a GC cycle, to collect cycle model data.
  //

  if ( _new_gc_cycle_seconds == 0.0 ) {
    // If there's no GC cycle time model, trigger a GC as soon as used >= 10% of the heap.  Trigger
    // a NewToOld GC so we start collecting OldGC stats.
    double percent = double(used_heap_words*100) / double(max_heap_words);
    bool   result  = (percent >= 10.0);
    
    if ( result ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : heap >= 10%% full: %.1f%%",
                        percent);
      GPGC_Thread::heuristic_wants_full_gc(false);
      return 0;
    }

    if ( always_log ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H delay GC : sleep %d seconds, heap < 10%% full: %.1f%%",
                        heuristic_interval_secs, percent);
    }

    return 1000 * heuristic_interval_secs;
  }

  //
  //  Crank the GC cycle time + allocation rate models to see how long until we have to start a new GC cycle.
  //

  double time_to_end_old_gc   = _new_gc_cycle_seconds + _old_gc_cycle_seconds;
  double old_space_alloc_rate = _old_gen_alloc_words_per_sec + _perm_gen_alloc_words_per_sec;
  double future_old_needed    = time_to_end_old_gc * old_space_alloc_rate;

  double safety_margin        = double(GPGCHeuristicSafetyMargin + 100) / 100.0;

  size_t free_heap_words      = max_heap_words - used_heap_words;
  size_t new_gen_free_words   = free_heap_words - (future_old_needed * safety_margin);

  double heap_for_new_gc      = _new_gen_alloc_words_per_sec * _new_gc_cycle_seconds;
  double seconds_to_oom       = double(new_gen_free_words) / _new_gen_alloc_words_per_sec;
  double seconds_to_gc        = seconds_to_oom - (_new_gc_cycle_seconds * safety_margin);
  double cycles_to_oom        = seconds_to_oom / _new_gc_cycle_seconds;

  //
  //  If new_gen_free_words <= 0, immediately trigger a NewToOld GC
  //

  if ( new_gen_free_words <= 0 ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : %d MB FON, %d MB free heap, %d%% safety margin",
                      long(future_old_needed) / (M>>LogBytesPerWord),
                      free_heap_words         / (M>>LogBytesPerWord),
                      GPGCHeuristicSafetyMargin);
    GPGC_Thread::heuristic_wants_full_gc(false);
    return 0;
  }

  //
  //  Check to see if incremental heap usage should trigger a GC cycle, to collect cycle model data.
  //

  if ( used_heap_words > _cycle_model_biggest_heap ) {
    // Even if the cycle model doesn't require a GC, we might need one because the heap has
    // gotten bigger.  If so, trigger a NewToOld GC so we start collecting OldGC stats.
    size_t ten_percent = max_heap_words / 10;
    if ( used_heap_words > (_cycle_model_biggest_heap + ten_percent) ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : heap >= %.1f%% full: %.1f%%, %d MB free",
                        double(_cycle_model_biggest_heap + ten_percent) * 100.0 / double(max_heap_words),
                        double(used_heap_words) * 100.0 / double(max_heap_words),
                        free_heap_words / (M>>LogBytesPerWord));
      GPGC_Thread::heuristic_wants_full_gc(false);
      return 0;
    }
  }

  if ( GPGC_OldCollector::collection_state() == GPGC_Collector::NotCollecting ) {
    // 
    //  If Old gen usage has consumed more than some percent of the headroom memory, start an OldGC cycle.
    //
    size_t last_live_new_words  = cycle_stats->end_new_words_in_use();
    size_t last_live_old_words  = old_cycle_stats->end_old_words_in_use() + old_cycle_stats->end_perm_words_in_use();
    size_t headroom_words       = max_heap_words - last_live_new_words - last_live_old_words;
    size_t threshold_words      = headroom_words * GPGCOldHeadroomUsedPercent / 100;
    
    if ( used_old_words > (last_live_old_words + threshold_words) ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : old gen used %d%% of headroom: %d MB headroom, %d MB live old, %d MB used old",
                        GPGCOldHeadroomUsedPercent,
                        headroom_words      / (M>>LogBytesPerWord),
                        last_live_old_words / (M>>LogBytesPerWord),
                        used_old_words      / (M>>LogBytesPerWord));
      GPGC_Thread::heuristic_wants_full_gc(false);
      return 0;
    }

    //
    // We might be at a new high old-space in use, in which case we want to refine our OldGC stats.
    //
    size_t ten_percent = max_heap_words / 10;
    if ( used_old_words > (_cycle_model_biggest_old + ten_percent) ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : new high old space: %d MB, prior %d MB",
                        used_old_words           / (M>>LogBytesPerWord),
                        _cycle_model_biggest_old / (M>>LogBytesPerWord));
      GPGC_Thread::heuristic_wants_full_gc(false);
      return 0;
    }
  }

  //
  //  Check to see if the user requested minimum OldGC interval has passed, and a simple NewGC cycle
  //  is going to be triggered.
  //

  if ( GPGCOldGCIntervalSecs>0 && seconds_to_gc<1 ) {
    long             now             = os::elapsed_counter();
    double           frequency       = os::elapsed_frequency();
    double           elapsed_secs    = double(now - old_cycle_stats->cycle_start_tick()) / frequency;

    if ( elapsed_secs >= GPGCOldGCIntervalSecs ) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : OldGC interval %d secs, %.1f elapsed: %.1f secs until GC, %d MB free, %d MB FON, %d MB for gc, %d%% safety margin",
                        GPGCOldGCIntervalSecs, elapsed_secs,
                        seconds_to_gc,
                        free_heap_words         / (M>>LogBytesPerWord),
                        long(future_old_needed) / (M>>LogBytesPerWord),
                        long(heap_for_new_gc)   / (M>>LogBytesPerWord),
                        GPGCHeuristicSafetyMargin);
      GPGC_Thread::heuristic_wants_full_gc(false);
return 0;
    }
  }


  //***                                                            ***//
  //***  If none of the above logic caused a GC cycle to trigger,  ***//
  //***  then we're at most going to trigger just a NewGC cycle.   ***//
  //***                                                            ***//

  //
  //  Trigger a GC cycle now if the model predicts less than 1 second until the GC cycle should begin.
  //
  
  if ( seconds_to_gc < 1 ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H start GC : %.1f secs until GC, %d MB free, %d MB FON, %d MB for gc, %d%% safety margin",
                      seconds_to_gc,
                      free_heap_words         / (M>>LogBytesPerWord),
                      long(future_old_needed) / (M>>LogBytesPerWord),
                      long(heap_for_new_gc)   / (M>>LogBytesPerWord),
                      GPGCHeuristicSafetyMargin);
    return 0;  // Run standard NewGC cycle
  }

  //
  //  Return a predicted time until the start of the next GC cycle.
  //

  if ( always_log ) {
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GPGC-H delay GC : %.1f secs until GC, %d MB free, %d MB FON, %d MB for gc, %d%% safety margin, %d MB old",
                      seconds_to_gc,
                      free_heap_words         / (M>>LogBytesPerWord),
                      long(future_old_needed) / (M>>LogBytesPerWord),
                      long(heap_for_new_gc)   / (M>>LogBytesPerWord),
                      GPGCHeuristicSafetyMargin,
                      used_old_words          / (M>>LogBytesPerWord));
  }

  return (1000 * seconds_to_gc);
}


// Calculate the number of milliseconds the PauselessGCThread should sleep
// before reevaluating the GC cycle time huerisitic. 
long GPGC_Heuristic::time_to_sleep()
{
  long millis = time_until_gc(true);

  // See if an immediate GC is required:
  if ( millis <= 0 ) {
    return millis;
  }

  // Wait only half the projected time to start of GC, in case the app behavior changes:
  millis = millis / 2;

  // Don't wait for more than a standard heuristic interval:
  if ( millis > (1000 * heuristic_interval_secs) ) {
    millis = 1000 * heuristic_interval_secs;
  }

  return millis;
}
