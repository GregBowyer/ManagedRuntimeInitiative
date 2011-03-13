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


#include "allocation.hpp"
#include "arguments.hpp"
#include "atomic.hpp"
#include "gpgc_pageBudget.hpp"
#include "gpgc_heap.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_stats.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "responseStream.hpp"
#include "tickProfiler.hpp"
#include "timer.hpp"
#include "vm_version.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "mutex.inline.hpp"
#include "os_os.inline.hpp"
#include "thread_os.inline.hpp"

static long max_heap              = 0;
static long print_gc_header_count = 0;

GPGC_HistoricalCycleStats* GPGC_HistoricalCycleStats::_history       = NULL;
size_t                     GPGC_HistoricalCycleStats::_history_count = 0;

void GPGC_HistoricalCycleStats::initialize(){
  guarantee(UseGenPauselessGC, "GPGC_HistoricalCycleStats requires UseGenPauselessGC");
  if (PrintGCHistory > 0) {
MutexLocker ml(GcHistory_lock);
    _history = NEW_C_HEAP_ARRAY(GPGC_HistoricalCycleStats, PrintGCHistory);
    memset(_history, 0, sizeof(GPGC_HistoricalCycleStats) * PrintGCHistory);
  }
}

#define GATHER_GPGC_CYCLE_NUM_STATS(field_type, cond, expr) \
  field_type sum = 0; \
  field_type sumsq = 0; \
  size_t k = 0; \
  GPGC_HistoricalCycleStats *cur = NULL; \
  GPGC_HistoricalCycleStats *next = NULL; \
  while (k < len) { \
    GPGC_HistoricalCycleStats *s = &_history[(pos + (k++)) % PrintGCHistory]; \
    if (s->_type[0] == type[0]) { \
      cur = s; \
      break; \
    } \
  } \
  while (cur != NULL) { \
    while (k < len) { \
      GPGC_HistoricalCycleStats *s = &_history[(pos + (k++)) % PrintGCHistory]; \
      if (s->_type[0] == type[0]) { \
        next = s; \
        break; \
      } \
    } \
    if (!(cond)) break; \
    field_type x = (expr); \
    sum += x; \
    sumsq += x*x; \
    if (x < min) min = x; \
    if (x > max) max = x; \
cur=next;\
    next = NULL; \
  } \
  double mean = ((double) sum)/count; \
  double stddev = sqrt(((double) sumsq)/count - mean*mean);

#ifdef AZ_PROFILER
#define PRINT_GPGC_CYCLE_LONG_STATS(field_name, tag_name) { \
  long min = LONG_MAX, max = LONG_MIN; \
  GATHER_GPGC_CYCLE_NUM_STATS(long, true, cur->field_name); \
  azprof::Xml tag(res, tag_name); \
  azprof::Xml::leaf(res, "min", (int64_t) min); \
  azprof::Xml::leaf(res, "max", (int64_t) max); \
  azprof::Xml::leaf(res, "mean", mean); \
  azprof::Xml::leaf(res, "stddev", stddev); \
}

#define _PRINT_GPGC_CYCLE_DBL_STATS(cond, expr, tag_name) { \
  double min = DBL_MAX, max = DBL_MIN; \
  GATHER_GPGC_CYCLE_NUM_STATS(double, cond, expr); \
  azprof::Xml tag(res, tag_name); \
  azprof::Xml::leaf(res, "min", min); \
  azprof::Xml::leaf(res, "max", max); \
  azprof::Xml::leaf(res, "mean", mean); \
  azprof::Xml::leaf(res, "stddev", stddev); \
}

#define PRINT_GPGC_CYCLE_DBL_STATS(field_name, tag_name) \
  _PRINT_GPGC_CYCLE_DBL_STATS(true, cur->field_name, tag_name)

#define PRINT_GPGC_CYCLE_BOOL_STATS(field_name, tag_name) { \
  int n = 0; \
  for (size_t k = 0; k < len; k++) { \
    GPGC_HistoricalCycleStats& cycle_stats = _history[(pos + k) % PrintGCHistory]; \
    if ((cycle_stats._type[0] == type[0]) && (cycle_stats.field_name)) ++n; \
  } \
  azprof::Xml tag(res, tag_name); \
  azprof::Xml::leaf(res, "percentage", ((double) n)/len); \
}
#endif // AZ_PROFILER

void GPGC_HistoricalCycleStats::print_summary_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  guarantee(UseGenPauselessGC, "GPGC_HistoricalCycleStats requires UseGenPauselessGC");
MutexLocker ml(GcHistory_lock);
  size_t pos, len;
  if (_history_count < PrintGCHistory) {
    pos = 0;
len=_history_count;
  } else {
    pos = _history_count % PrintGCHistory;
len=PrintGCHistory;
  }
  azprof::Xml tag(res, "gpgc-cycle-summary");
  GCLogMessage::print_warning_summary_xml(req, res);
  azprof::Xml::leaf(res, "cycle-count", (int64_t) _history_count);
  azprof::Xml::leaf(res, "cycle-history-length", (int64_t) len);
  azprof::Xml::leaf(res, "new-gc-threads", (int64_t) GenPauselessNewThreads);
  azprof::Xml::leaf(res, "old-gc-threads", (int64_t) GenPauselessOldThreads);
  if (len > 0) {
    long dt = os::ticks_to_millis(os::elapsed_counter() - _history[pos]._ticks)/1000;
    azprof::Xml::leaf(res, "duration", (int64_t) dt);
  }
  type_print_summary_xml(req, res, pos, len, "New");
  type_print_summary_xml(req, res, pos, len, "Old");
#endif // AZ_PROFILER
}

void GPGC_HistoricalCycleStats::type_print_summary_xml(
  azprof::Request *req, azprof::Response *res, size_t pos, size_t len, const char *type
) {
#ifdef AZ_PROFILER
  int count = 0;
  for (size_t k = 0; k < len; k++) {
    GPGC_HistoricalCycleStats& cycle_stats = _history[(pos + k) % PrintGCHistory];
    if (cycle_stats._type[0] ==  type[0]) ++count;
  }
  azprof::Xml tag(res, "gpgc-cycle-type-summary");
  azprof::Xml::leaf(res, "name", type);
  azprof::Xml::leaf(res, "count", (int64_t) count);
  _PRINT_GPGC_CYCLE_DBL_STATS(
next!=NULL,
      os::ticks_to_millis(next->_ticks - cur->_ticks)/1000.0,
"cycle-interval");
  _PRINT_GPGC_CYCLE_DBL_STATS(
next!=NULL,
      (1000.0 * (cur->_pause1_duration + cur->_pause2_duration + cur->_pause3_duration + cur->_pause4_duration)) / os::ticks_to_millis(next->_ticks - cur->_ticks),
"cycle-pause-ratio");
  PRINT_GPGC_CYCLE_LONG_STATS(_max_heap, "max-heap");
  PRINT_GPGC_CYCLE_LONG_STATS(_peak_non_pause_used, "peak-committed-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_committed_used, "committed-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_peak_pause_used, "peak-pause-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_unreturned_pause, "unreturned-pause");
  PRINT_GPGC_CYCLE_LONG_STATS(_new_gen_used, "new-gen-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_old_gen_used, "old-gen-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_perm_gen_used, "perm-gen-used");
  PRINT_GPGC_CYCLE_LONG_STATS(_gen_live, "gen-live");
  PRINT_GPGC_CYCLE_LONG_STATS(_gen_frag, "gen-frag");
  PRINT_GPGC_CYCLE_LONG_STATS(_garbage_found, "garbage-found");
  PRINT_GPGC_CYCLE_LONG_STATS(_garbage_collected, "garbage-collected");
  PRINT_GPGC_CYCLE_LONG_STATS(_sideband_limited, "sideband-limited");
  PRINT_GPGC_CYCLE_LONG_STATS(_pages_promoted_to_old, "pages-promoted-to-old");
  PRINT_GPGC_CYCLE_LONG_STATS(_pages_relocated, "pages-relocated");
  PRINT_GPGC_CYCLE_LONG_STATS(_norelocate_pages, "no-relocate-pages");
  PRINT_GPGC_CYCLE_LONG_STATS(_reloc_spike_pages, "reloc-spike-pages");
  PRINT_GPGC_CYCLE_LONG_STATS(_small_space_page_count, "small-space-page-count");
  PRINT_GPGC_CYCLE_LONG_STATS(_mid_space_page_count, "mid-space-page-count");
  PRINT_GPGC_CYCLE_LONG_STATS(_large_space_page_count, "large-space-page-count");
  PRINT_GPGC_CYCLE_DBL_STATS(_pause1_duration, "pause1-duration");
  PRINT_GPGC_CYCLE_DBL_STATS(_pause2_duration, "pause2-duration");
  PRINT_GPGC_CYCLE_DBL_STATS(_pause3_duration, "pause3-duration");
  PRINT_GPGC_CYCLE_LONG_STATS(_pause4_count, "pause4-count");
  PRINT_GPGC_CYCLE_DBL_STATS(_pause4_duration, "pause4-duration");
  PRINT_GPGC_CYCLE_DBL_STATS(_pause4_max, "pause4-max");
  PRINT_GPGC_CYCLE_DBL_STATS(_intercycle_seconds, "intercycle-seconds");
  PRINT_GPGC_CYCLE_DBL_STATS(_intercycle_alloc_rate, "intercycle-alloc-rate");
  PRINT_GPGC_CYCLE_DBL_STATS(_intercycle_perm_alloc_rate, "intercycle-perm-alloc-rate");
  PRINT_GPGC_CYCLE_DBL_STATS(_intracycle_seconds, "intracycle-seconds");
  PRINT_GPGC_CYCLE_DBL_STATS(_intracycle_alloc_rate, "intracycle-alloc-rate");
  PRINT_GPGC_CYCLE_DBL_STATS(_intracycle_perm_alloc_rate, "intracycle-perm-alloc-rate");
  PRINT_GPGC_CYCLE_BOOL_STATS(_grant_mode, "grant-mode");
  PRINT_GPGC_CYCLE_BOOL_STATS(_pause_mode, "pause-mode");
  PRINT_GPGC_CYCLE_BOOL_STATS(_cycle_past_committed, "cycle-past-committed");
  PRINT_GPGC_CYCLE_BOOL_STATS(_cycle_used_pause, "cycle-used-pause");
  PRINT_GPGC_CYCLE_BOOL_STATS(_cycle_pause_exceeded, "cycle-pause-exceeded");
  PRINT_GPGC_CYCLE_LONG_STATS(_total_threads, "total-threads");
  PRINT_GPGC_CYCLE_LONG_STATS(_threads_delayed, "threads-delayed");
  PRINT_GPGC_CYCLE_DBL_STATS(_average_thread_delay, "average-thread-delay");
  PRINT_GPGC_CYCLE_DBL_STATS(_max_thread_delay, "max-thread-delay");
#endif // AZ_PROFILER
}

void GPGC_HistoricalCycleStats::print_history_xml(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  guarantee(UseGenPauselessGC, "GPGC_HistoricalCycleStats requires UseGenPauselessGC");
MutexLocker ml(GcHistory_lock);
  size_t pos, len;
  if (_history_count < PrintGCHistory) {
    pos = 0;
len=_history_count;
  } else {
    pos = _history_count % PrintGCHistory;
len=PrintGCHistory;
  }
  azprof::Xml tag(res, "gpgc-cycle-history");
  azprof::Xml::leaf(res, "cycle-count", (int64_t) _history_count);
  azprof::Xml::leaf(res, "cycle-history-length", (int64_t) len);
  azprof::Xml::leaf(res, "new-gc-threads", (int64_t) GenPauselessNewThreads);
  azprof::Xml::leaf(res, "old-gc-threads", (int64_t) GenPauselessOldThreads);
  if (len > 0) {
    long dt = os::ticks_to_millis(os::elapsed_counter() - _history[pos]._ticks)/1000;
    azprof::Xml::leaf(res, "duration", (int64_t) dt);
  }
  GCLogMessage::print_warning_history_xml(req, res);
  for (size_t k = 0; k < len; k++) {
    _history[(pos + k) % PrintGCHistory].print_xml(req, res);
  }
#endif // AZ_PROFILER
}

void GPGC_HistoricalCycleStats::print_history_txt(azprof::Request *req, azprof::Response *res) {
#ifdef AZ_PROFILER
  guarantee(UseGenPauselessGC, "GPGC_HistoricalCycleStats requires UseGenPauselessGC");
  responseStream stream(res);
  res->ok("text/plain", -1);
res->end_header();
print_history_txt(&stream);
#endif // AZ_PROFILER
}

void GPGC_HistoricalCycleStats::print_history_txt(outputStream *stream) {
  guarantee(UseGenPauselessGC, "GPGC_HistoricalCycleStats requires UseGenPauselessGC");
MutexLocker ml(GcHistory_lock);
  size_t pos, len;
  if (_history_count < PrintGCHistory) {
    pos = 0;
len=_history_count;
  } else {
    pos = _history_count % PrintGCHistory;
len=PrintGCHistory;
  }
  for (size_t k = 0; k < len; k++) {
    const GPGC_HistoricalCycleStats& s = _history[(pos + k) % PrintGCHistory];
    stream->time_stamp().update_to(os::elapsed_counter() - (s._ticks - gclog_or_tty->time_stamp().ticks()));
    s.print_txt(stream);
  }
}

void GPGC_HistoricalCycleStats::record(bool enabled, outputStream *stream, const GPGC_HistoricalCycleStats& s) {
  if (enabled) s.print_txt(stream);
MutexLocker ml(GcHistory_lock);
  if (_history) memcpy(_history + ((_history_count++) % PrintGCHistory), &s, sizeof(GPGC_HistoricalCycleStats));
}

void GPGC_HistoricalCycleStats::print_xml(azprof::Request *req, azprof::Response *res) const {
#ifdef AZ_PROFILER
  azprof::Xml tag(res, "gpgc-cycle");
  azprof::Xml::leaf(res, "type", _type);
  azprof::Xml::leaf(res, "label", _label);
  azprof::Xml::leaf(res, "total-cycles", (int64_t) _total_cycles);
  azprof::Xml::leaf(res, "cycle", (int64_t) _cycle);
  azprof::Xml::leaf(res, "max-heap", (int64_t) _max_heap);
  azprof::Xml::leaf(res, "peak-committed-used", (int64_t) _peak_non_pause_used);
  azprof::Xml::leaf(res, "committed-used", (int64_t) _committed_used);
  azprof::Xml::leaf(res, "peak-pause-used", (int64_t) _peak_pause_used);
  azprof::Xml::leaf(res, "unreturned-pause", (int64_t) _unreturned_pause);
  azprof::Xml::leaf(res, "new-gen-used", (int64_t) _new_gen_used);
  azprof::Xml::leaf(res, "old-gen-used", (int64_t) _old_gen_used);
  azprof::Xml::leaf(res, "perm-gen-used", (int64_t) _perm_gen_used);
  azprof::Xml::leaf(res, "gen-live", (int64_t) _gen_live);
  azprof::Xml::leaf(res, "gen-frag", (int64_t) _gen_frag);
  azprof::Xml::leaf(res, "garbage-found", (int64_t) _garbage_found);
  azprof::Xml::leaf(res, "garbage-collected", (int64_t) _garbage_collected);
  azprof::Xml::leaf(res, "sideband-limited", (int64_t) _sideband_limited);
  azprof::Xml::leaf(res, "pages-promoted-to-old", (int64_t) _pages_promoted_to_old);
  azprof::Xml::leaf(res, "pages-relocated", (int64_t) _pages_relocated);
  azprof::Xml::leaf(res, "no-relocate-pages", (int64_t) _norelocate_pages);
  azprof::Xml::leaf(res, "reloc-spike-pages", (int64_t) _reloc_spike_pages);
  azprof::Xml::leaf(res, "small-space-page-count", (int64_t) _small_space_page_count);
  azprof::Xml::leaf(res, "mid-space-page-count", (int64_t) _mid_space_page_count);
  azprof::Xml::leaf(res, "large-space-page-count", (int64_t) _large_space_page_count);
  azprof::Xml::leaf(res, "pause1-start-secs", _pause1_start_secs);
  azprof::Xml::leaf(res, "pause1-duration", _pause1_duration);
  azprof::Xml::leaf(res, "pause2-start-secs", _pause2_start_secs);
  azprof::Xml::leaf(res, "pause2-duration", _pause2_duration);
  azprof::Xml::leaf(res, "pause3-start-secs", _pause3_start_secs);
  azprof::Xml::leaf(res, "pause3-duration", _pause3_duration);
  azprof::Xml::leaf(res, "pause4-count", (int64_t) _pause4_count);
  azprof::Xml::leaf(res, "pause4-start-secs", _pause4_start_secs);
  azprof::Xml::leaf(res, "pause4-duration", _pause4_duration);
  azprof::Xml::leaf(res, "pause4-max", _pause4_max);
  azprof::Xml::leaf(res, "intercycle-seconds", _intercycle_seconds);
  azprof::Xml::leaf(res, "intercycle-alloc-rate", _intercycle_alloc_rate);
  azprof::Xml::leaf(res, "intercycle-perm-alloc-rate", _intercycle_perm_alloc_rate);
  azprof::Xml::leaf(res, "intracycle-seconds", _intracycle_seconds);
  azprof::Xml::leaf(res, "intracycle-alloc-rate", _intracycle_alloc_rate);
  azprof::Xml::leaf(res, "intracycle-perm-alloc-rate", _intracycle_perm_alloc_rate);
  azprof::Xml::leaf(res, "gc-thread-count", (int64_t) _gc_thread_count);
  azprof::Xml::leaf(res, "grant-mode", _grant_mode ? "true" : "false");
  azprof::Xml::leaf(res, "pause-mode", _pause_mode ? "true" : "false");
  azprof::Xml::leaf(res, "cycle-past-committed", _cycle_past_committed ? "true" : "false");
  azprof::Xml::leaf(res, "cycle-used-pause", _cycle_used_pause ? "true" : "false");
  azprof::Xml::leaf(res, "cycle-pause-exceeded", _cycle_pause_exceeded ? "true" : "false");
  azprof::Xml::leaf(res, "total-threads", (int64_t) _total_threads);
  azprof::Xml::leaf(res, "threads-delayed", (int64_t) _threads_delayed);
  azprof::Xml::leaf(res, "average-thread-delay", _average_thread_delay);
  azprof::Xml::leaf(res, "max-thread-delay", _max_thread_delay);
#endif // AZ_PROFILER
}

void GPGC_HistoricalCycleStats::print_txt(outputStream *stream) const {

#define BUFLEN 64

char thrds_delayed[BUFLEN];
char average_thread_delay[BUFLEN];
char max_thread_delay[BUFLEN];

if(strcmp(_type,"Old")==0){
    jio_snprintf(thrds_delayed,        BUFLEN,   "%s", "  - ");
    jio_snprintf(average_thread_delay, BUFLEN,   "%s", "  - ");
    jio_snprintf(max_thread_delay,     BUFLEN,   "%s", "  - ");
  } else {
    jio_snprintf(thrds_delayed,        BUFLEN, "%lld", _threads_delayed);
    jio_snprintf(average_thread_delay, BUFLEN, "%.3f", _average_thread_delay);
    jio_snprintf(max_thread_delay,     BUFLEN, "%.3f", _max_thread_delay);
  }

  GCLogMessage::log_b(true, stream,
"GC %s %s %lld-%lld : %lld %lld %lld %lld %lld : %lld %lld %lld : %lld %lld %lld %lld %lld : %lld %lld %lld %lld %lld %lld %lld : %.3f %.1f %.1f %.3f %.1f %.1f : %lld %c%c %d%d%d : %lld %s %s %s : %.3f %.3f %.3f %.3f %.3f %.3f %lld %.3f %.3f %.3f ",
      _type, _label, _total_cycles, _cycle,

      _max_heap, _peak_non_pause_used, _committed_used, _peak_pause_used, _unreturned_pause,

      _new_gen_used, _old_gen_used, _perm_gen_used,

      _gen_live, _gen_frag, _garbage_found, _garbage_collected, _sideband_limited,

      _pages_promoted_to_old, _pages_relocated,  _norelocate_pages, _reloc_spike_pages, _small_space_page_count, _mid_space_page_count, _large_space_page_count,

      _intercycle_seconds, _intercycle_alloc_rate, _intercycle_perm_alloc_rate, _intracycle_seconds, _intracycle_alloc_rate, _intracycle_perm_alloc_rate,

      _gc_thread_count, _grant_mode ? 'g' : '-', _pause_mode ? 'p' : '-', _cycle_past_committed?1:0, _cycle_used_pause?1:0, _cycle_pause_exceeded?1:0,

      _total_threads, thrds_delayed, average_thread_delay, max_thread_delay,

      _pause1_start_secs, _pause1_duration, _pause2_start_secs, _pause2_duration, _pause3_start_secs, _pause3_duration,
      _pause4_count, _pause4_start_secs, _pause4_duration, _pause4_max);
}

GPGC_Stats::GPGC_Stats() {
}

void GPGC_Stats::init_stats() {
  // Print an initial timestamp and other info as a troubleshooting convenience
  char      stime[64];
struct tm date;
  time_t    secs   = os::javaTimeMillis() / 1000;
  int       result = 0;
  
  if (localtime_r(&secs, &date) != NULL) {
    result = strftime(stime, sizeof(stime), "%a %b %d %X %Z %Y", &date);
  }
  if (!result) {
sprintf(stime,"%.2lfs",os::elapsedTime());
  }

  max_heap = GPGC_Heap::heap()->max_heap_size_specified() / M;

  int64_t balance         = 0;
  int64_t balance_min     = 0;
  int64_t balance_max     = 0;
  size_t  allocated       = 0;
  size_t  allocated_min   = 0;
  size_t  allocated_max   = 0;
  size_t  maximum         = 0;
  long    cheap_committed = 0;
  long    cheap_max       = 0;
  long    java_committed  = 0;
  long    java_max        = 0;
  long    pause_max       = 0;

if(os::use_azmem()){
    os::memory_account_get_stats(os::CHEAP_COMMITTED_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

    cheap_committed  = (balance + allocated) / M;
    cheap_max        = (maximum) / M;

    os::memory_account_get_stats(os::JAVA_COMMITTED_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

    java_committed   = (balance + allocated) / M;
    java_max         = (maximum) / M;

    os::memory_account_get_stats(os::JAVA_PAUSE_MEMORY_ACCOUNT,
                                 &balance, &balance_min, &balance_max, &allocated, &allocated_min, &allocated_max, &maximum);

    pause_max        = (maximum) / M;
  }


  BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGC || PrintGCDetails, false, gclog_or_tty);

  GCLogMessage::log_b(m.enabled(), m.stream(), "GCH :           version : %s (%s %s)",
                    Abstract_VM_Version::vm_name(),
                    Abstract_VM_Version::vm_release(),
                    Abstract_VM_Version::vm_info_string());
  GCLogMessage::log_b(m.enabled(), m.stream(), "GCH :        start time : %s", stime);
  GCLogMessage::log_b(m.enabled(), m.stream(), "GCH :             ncpus : %d", os::processor_count());
if(os::use_azmem()){
    GCLogMessage::log_b(m.enabled(), m.stream(),
"GCH : mem accounts (MB) : (C) %lld / %lld -- (J) %lld / %lld -- (P) %lld",
         cheap_committed, cheap_max, java_committed, java_max, pause_max);
  }
  GCLogMessage::log_b(m.enabled(), m.stream(), "GCH :     jvm arguments : %s", Arguments::jvm_args());
}


GPGC_CycleStats::GPGC_CycleStats() {
  memset(this, 0, sizeof(GPGC_CycleStats));

  // Initialize these here to get meaningful tick info for the first GC cycle
_last_cycle_end_tick=os::elapsed_counter();

  _cycle_end_tick                   = _last_cycle_end_tick;
}

void GPGC_CycleStats::reset(){
  long end_tick                     = _cycle_end_tick;
  long end_words_in_use             = _cycle_end_words_in_use;
  long end_new_words_in_use         = _cycle_end_new_words_in_use;
  long end_old_words_in_use         = _cycle_end_old_words_in_use;
  long end_perm_words_in_use        = _cycle_end_perm_words_in_use;

  memset(this, 0, sizeof(GPGC_CycleStats));

  _last_cycle_end_tick              = end_tick;
  _last_cycle_end_words_in_use      = end_words_in_use;
  _last_cycle_end_new_words_in_use  = end_new_words_in_use;
  _last_cycle_end_old_words_in_use  = end_old_words_in_use;
  _last_cycle_end_perm_words_in_use = end_perm_words_in_use;
}

void GPGC_CycleStats::start_cycle() {
_cycle_start_tick=os::elapsed_counter();
  _cycle_start_words_in_use         = GPGC_Space::total_words_used();
  _cycle_start_new_words_in_use     = GPGC_Space::new_gen_words_used();
  _cycle_start_old_words_in_use     = GPGC_Space::old_gen_words_used();
  _cycle_start_perm_words_in_use    = GPGC_Space::perm_gen_words_used();
}

void GPGC_CycleStats::end_cycle() {
  _cycle_end_words_in_use           = GPGC_Space::total_words_used();
  _cycle_end_new_words_in_use       = GPGC_Space::new_gen_words_used();
  _cycle_end_old_words_in_use       = GPGC_Space::old_gen_words_used();
  _cycle_end_perm_words_in_use      = GPGC_Space::perm_gen_words_used();
_cycle_end_tick=os::elapsed_counter();
  _cycle_ticks                      = _cycle_end_tick - _cycle_start_tick;

  size_t peak_committed, peak_grant, peak_pause;

  if ( GPGC_Heap::heap()->gc_stats()->new_gc_cycle_stats() == this ) {
    GPGC_Space::newgc_page_and_frag_words_count(&_cycle_end_gen_small_pages, &_cycle_end_gen_mid_pages, &_cycle_end_gen_large_pages,
                                                &_cycle_end_gen_small_frag_words, &_cycle_end_gen_mid_frag_words, &_cycle_end_gen_large_frag_words);
    GPGC_PageBudget::report_newgc_peak_usage(&peak_committed, &peak_grant, &peak_pause);
  } else {
    assert0(GPGC_Heap::heap()->gc_stats()->old_gc_cycle_stats() == this);
    GPGC_Space::oldgc_page_and_frag_words_count(&_cycle_end_gen_small_pages, &_cycle_end_gen_mid_pages, &_cycle_end_gen_large_pages,
                                                &_cycle_end_gen_small_frag_words, &_cycle_end_gen_mid_frag_words, &_cycle_end_gen_large_frag_words);
    GPGC_PageBudget::report_oldgc_peak_usage(&peak_committed, &peak_grant, &peak_pause);
  }

  _cycle_end_gen_frag_words         = _cycle_end_gen_small_frag_words + _cycle_end_gen_mid_frag_words + _cycle_end_gen_large_frag_words;

  _peak_committed_used              = peak_committed;
  _peak_grant_used                  = peak_grant;
  _peak_non_pause_used              = _peak_committed_used + _peak_grant_used;
  _peak_pause_used                  = peak_pause;

  // TODO: should cross check with azmem's sense of how much memory is in use:
  _committed_used                   = GPGC_PageBudget::normal_pages_used();

  _cycle_past_committed             = _peak_grant_used > 0;
  _cycle_used_pause                 = _peak_pause_used > 0;
  _cycle_pause_exceeded             = GPGC_PageBudget::pause_allocation_failed();
}

void GPGC_CycleStats::start_pause1(long start_ticks) {
  _pause1_start_tick                = start_ticks;
}

void GPGC_CycleStats::end_pause1(long pause_ticks) {
  _pause1_ticks                     = pause_ticks;
}

void GPGC_CycleStats::end_pause1() {
  _pause1_ticks                     = os::elapsed_counter() - _pause1_start_tick;
}

void GPGC_CycleStats::start_pause2(long start_ticks) {
  _pause2_start_tick                = start_ticks;
}

void GPGC_CycleStats::end_pause2(long pause_ticks) {
  _pause2_ticks                     = pause_ticks;
}

void GPGC_CycleStats::start_pause3(long start_ticks) {
  _pause3_start_tick                = start_ticks;
}

void GPGC_CycleStats::end_pause3(long pause_ticks) {
  _pause3_ticks                     = pause_ticks;
}

void GPGC_CycleStats::start_relocate() {
_relocate_start_tick=os::elapsed_counter();
}

void GPGC_CycleStats::heap_at_page_sort(long heap_words) {
  _page_sort_words_in_use           = heap_words;
}

void GPGC_CycleStats::heap_after_reloc_setup(long words_found,
                                             long words_to_collect,
                                             long sideband_limited_words) {
  _garbage_words_found              = words_found;
  _garbage_words_collected          = words_to_collect;
  _sideband_words_limited           = sideband_limited_words;
}

void GPGC_CycleStats::start_pause4(long start_ticks) {
  if (_pause4_start_tick == 0) {
    _first_pause4_start_tick        = start_ticks;
  }

  _pause4_start_tick                = start_ticks;
}

void GPGC_CycleStats::end_pause4(long pause_ticks) {
  _pause4_ticks                     += pause_ticks;
  _pause4_count++;
  Atomic::record_peak(pause_ticks, &_pause4_max);
}

void GPGC_CycleStats::heap_at_pause4(long heap_words, long live_object_words, long fragmented_words) {
  _pause4_words_in_use              = heap_words;
  _pause4_live_words_in_use         = live_object_words;
  _pause4_fragmented_words          = fragmented_words;
}

void GPGC_CycleStats::heap_at_end_relocate(long collection_delta_words,
                                           long peak_relocation_page_count,
                                           long norelocate_pages,
                                           long promoted_pages,
                                           long relocated_pages) {
  _collection_delta_words           = collection_delta_words;
  _reloc_spike_pages                = peak_relocation_page_count;
  _norelocate_pages                 = norelocate_pages;
  _pages_promoted_to_old            = promoted_pages;
  _pages_relocated                  = relocated_pages;
}

void GPGC_CycleStats::threads_delayed(long threads, double average_delay,
                                      double max_delay) {
_threads_delayed=threads;
  _average_thread_delay             = average_delay;
  _max_thread_delay                 = max_delay;
}

void GPGC_CycleStats::verbose_gc_log(const char* type, const char* label) {
  double tick_frequency             = os::elapsed_frequency();
  long   start_of_time_tick         = gclog_or_tty->get_time_stamp()->ticks();

  double pause1_start_secs          = 0.0;
  if (_pause1_start_tick > 0) {
    pause1_start_secs               = (_pause1_start_tick - start_of_time_tick)  / tick_frequency;
  }
  double pause2_start_secs          = 0.0;
  if (_pause2_start_tick > 0) {
    pause2_start_secs               = (_pause2_start_tick - start_of_time_tick)  / tick_frequency;
  }
  double pause3_start_secs          = 0.0;
  if (_pause3_start_tick > 0) {
    pause3_start_secs               = (_pause3_start_tick - start_of_time_tick)  / tick_frequency;
  }
  double pause4_start_secs          = 0.0;
  if (_pause4_count > 0) {
    pause4_start_secs               = (_first_pause4_start_tick - start_of_time_tick)  / tick_frequency;
  }
  double pause1_duration            = _pause1_ticks                              / tick_frequency;
  double pause2_duration            = _pause2_ticks                              / tick_frequency;
  double pause3_duration            = _pause3_ticks                              / tick_frequency;
  double pause4_duration            = _pause4_ticks                              / tick_frequency;
  double pause4_max                 = _pause4_max                                / tick_frequency;

  long   garbage_found              = (_garbage_words_found         << LogBytesPerWord) / M;
  long   garbage_collected          = (_garbage_words_collected     << LogBytesPerWord) / M;
  long   sideband_limited           = (_sideband_words_limited      << LogBytesPerWord) / M;


  long   cur_total_cycle            = 0;
  long   cur_cycle                  = 0;
  long   gc_thread_count            = 0;

  long   intercycle_gen_words       = 0;
  long   intracycle_gen_words       = 0;

  bool   new_collector              = false;

if(strcmp(type,"Old")==0){
    cur_total_cycle                 = GPGC_OldCollector::current_total_cycle();
    cur_cycle                       = GPGC_OldCollector::current_old_cycle();

    gc_thread_count                 = GenPauselessOldThreads;

    intercycle_gen_words            = _cycle_start_old_words_in_use  - _last_cycle_end_old_words_in_use;
    intracycle_gen_words            = _cycle_end_old_words_in_use    - _cycle_start_old_words_in_use;
  } else {
    cur_total_cycle                 = GPGC_NewCollector::current_total_cycle();
    cur_cycle                       = GPGC_NewCollector::current_new_cycle();

    gc_thread_count                 = GenPauselessNewThreads;

    intercycle_gen_words            = _cycle_start_new_words_in_use  - _last_cycle_end_new_words_in_use;
    intracycle_gen_words            = _cycle_end_new_words_in_use    - _cycle_start_new_words_in_use;

    print_gc_header_count++;
    new_collector                   = true;
  }

  long   gen_live                   = (_pause4_live_words_in_use    << LogBytesPerWord) / M;
  long   gen_frag                   = (_cycle_end_gen_frag_words    << LogBytesPerWord) / M;

  long   end_new_gen_used           = (_cycle_end_new_words_in_use  << LogBytesPerWord) / M;
  long   end_old_gen_used           = (_cycle_end_old_words_in_use  << LogBytesPerWord) / M;
  long   end_perm_gen_used          = (_cycle_end_perm_words_in_use << LogBytesPerWord) / M;

  long   intercycle_perm_gen_words  = _cycle_start_perm_words_in_use - _last_cycle_end_perm_words_in_use;
  long   intracycle_perm_gen_words  = _cycle_end_perm_words_in_use   - _cycle_start_perm_words_in_use;

  long   alloc_bytes                = 0;
  double intercycle_seconds         = (_cycle_start_tick - _last_cycle_end_tick) / tick_frequency;
  alloc_bytes                       = intercycle_gen_words << LogBytesPerWord;
  double intercycle_alloc_rate      = alloc_bytes / (M * intercycle_seconds);
  alloc_bytes                       = intercycle_perm_gen_words << LogBytesPerWord;
  double intercycle_perm_alloc_rate = alloc_bytes / (M * intercycle_seconds);

  double intracycle_seconds         = _cycle_ticks                               / tick_frequency;
  alloc_bytes                       = (intracycle_gen_words + _collection_delta_words) << LogBytesPerWord;
  _intracycle_alloc_rate            = alloc_bytes / (M * intracycle_seconds);
  alloc_bytes                       = (intracycle_perm_gen_words) << LogBytesPerWord;
  double intracycle_perm_alloc_rate = alloc_bytes / (M * intracycle_seconds);

  // If the perm gen gets collected and the alloc rate is negative, we just set
  // the alloc-rate to zero.
  // TODO - Determine whether we can account for the collection when the value
  // is greater than zero but much less than what the allocation rate was.
  if (intracycle_perm_alloc_rate < 0) {
    intracycle_perm_alloc_rate = 0.0;
  }

  if (intercycle_perm_alloc_rate < 0) {
    intercycle_perm_alloc_rate = 0.0;
  }


  bool   grant_mode                 = false;
  bool   pause_mode                 = false;

  if (GPGCOverflowMemory!=0 && GPGC_PageBudget::leak_into_grant_enabled()) {
    // We log it here if grant is enabled.  Note that we may not be using grant.
    // _cycle_past_committed will determine whether we actually dipped into grant.
    grant_mode = true;
  } else if (GPGCPausePreventionMemory!=0 && GPGC_PageBudget::pause_account_enabled()) {
    // We never actually use pause if leak_into_grant_enabled(), and we only want to
    // log it if we might use it.
    pause_mode = true;
  }


  {
    BufferedLoggerMark m(NOTAG, Log::M_GC, PrintGC || PrintGCDetails, false, gclog_or_tty);

    if (new_collector && (print_gc_header_count - 1) % 15 == 0) {
      GCLogMessage::log_b(m.enabled(), m.stream(),
"GCH                          :         heap usage  MB     :   gens MB    :        heap info MB         :              pages                 :  pause 1   pause 2   pause3              pause 4              :        intercycle               intracycle        :      gc       :    threads  delay   ");
        GCLogMessage::log_b(m.enabled(), m.stream(),
"GCH type label tot_ct-cyc_ct : max peak cur ppause upause : new old perm : live frag found cllctd lmtd : prom reloc noreloc rspike sm md lg : start dur start dur start dur p4_count p4_start p4_dur p4_max : sec alloc-rate perm-rate sec alloc-rate perm-rate : thrds md diag : tot delayed avg max ");
    }

    GPGC_HistoricalCycleStats cycle_stats = {
        os::elapsed_counter(), type, label, cur_total_cycle, cur_cycle,

        max_heap, _peak_non_pause_used, _committed_used, _peak_pause_used, _unreturned_pause,

        end_new_gen_used, end_old_gen_used, end_perm_gen_used,

        gen_live, gen_frag, garbage_found, garbage_collected, sideband_limited,

        _pages_promoted_to_old, _pages_relocated,  _norelocate_pages, _reloc_spike_pages, _cycle_end_gen_small_pages, _cycle_end_gen_mid_pages, _cycle_end_gen_large_pages,

        pause1_start_secs, pause1_duration, pause2_start_secs, pause2_duration, pause3_start_secs, pause3_duration,
        _pause4_count, pause4_start_secs, pause4_duration, pause4_max,

        intercycle_seconds, intercycle_alloc_rate, intercycle_perm_alloc_rate, intracycle_seconds, _intracycle_alloc_rate, intracycle_perm_alloc_rate,

        gc_thread_count, grant_mode, pause_mode, _cycle_past_committed, _cycle_used_pause, _cycle_pause_exceeded,

        Threads::number_of_threads(), _threads_delayed, _average_thread_delay, _max_thread_delay
    };
    GPGC_HistoricalCycleStats::record(m.enabled(), m.stream(), cycle_stats);
  }

  if ( _threads_delayed>0 && GPGCDieWhenThreadDelayed ) {
    // Diagnostic mode option: Exit the AVM when a JavaThread is delayed waiting on a GC cycle.  This is
    // used to run tests that verify GPGC is keeping up with the application's allocation rate.
gclog_or_tty->flush();
    ShouldNotReachHere();
  }
}
