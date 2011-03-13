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
#ifndef GPGC_STATS_HPP
#define GPGC_STATS_HPP


#include "allocation.hpp"

#ifdef AZ_PROFILER
#include <azprof/azprof_io.hpp>
#endif // AZ_PROFILER

class outputStream;

// Historical Statistics for a single GPGC cycle used for -verbose:gc and by
// ARTA.
struct GPGC_HistoricalCycleStats {
  static void initialize();
  static void print_summary_xml(azprof::Request*, azprof::Response*);
  static void type_print_summary_xml(azprof::Request*, azprof::Response*, size_t pos, size_t len, const char *type);
  static void print_history_xml(azprof::Request*, azprof::Response*);
  static void print_history_txt(azprof::Request*, azprof::Response*);
  static void print_history_txt(outputStream*);
  static void record(bool enabled, outputStream*, const GPGC_HistoricalCycleStats&);

  // A ring buffer of stats which is kept so that a history of GC cycle activity
  // is always available through ARTA whether -verbose:gc was used or not.
  static GPGC_HistoricalCycleStats *_history;
  static size_t _history_count;

  void print_xml(azprof::Request*, azprof::Response*) const;
  void print_txt(outputStream*) const;

  long _ticks;
  const char *_type;
  const char *_label;
  long _total_cycles;
  long _cycle;
  long _max_heap;
  long _peak_non_pause_used;
  long _committed_used;
  long _peak_pause_used;
  long _unreturned_pause;
  long _new_gen_used;
  long _old_gen_used;
  long _perm_gen_used;
  long _gen_live;
  long _gen_frag;
  long _garbage_found;
  long _garbage_collected;
  long _sideband_limited;
  long _pages_promoted_to_old;
  long _pages_relocated;
  long _norelocate_pages;
  long _reloc_spike_pages;
  long _small_space_page_count;
  long _mid_space_page_count;
  long _large_space_page_count;
  double _pause1_start_secs;
  double _pause1_duration;
  double _pause2_start_secs;
  double _pause2_duration;
  double _pause3_start_secs;
  double _pause3_duration;
  long   _pause4_count;
  double _pause4_start_secs;
  double _pause4_duration;
  double _pause4_max;
  double _intercycle_seconds;
  double _intercycle_alloc_rate;
  double _intercycle_perm_alloc_rate;
  double _intracycle_seconds;
  double _intracycle_alloc_rate;
  double _intracycle_perm_alloc_rate;
  long _gc_thread_count;
  bool _grant_mode;
  bool _pause_mode;
  bool _cycle_past_committed;
  bool _cycle_used_pause;
  bool _cycle_pause_exceeded;
  long _total_threads;
  long _threads_delayed;
  double _average_thread_delay;
  double _max_thread_delay;
};

//  This class calculates tracks GC cycle time statistics, for use by the
//  GPGC_Heuristic class.
class GPGC_CycleStats VALUE_OBJ_CLASS_SPEC
{
  private:
    long _cycle_start_tick;
    long _cycle_end_tick;
    long _cycle_ticks;

    long _pause1_start_tick;
    long _pause1_ticks;
    long _pause2_start_tick;
    long _pause2_ticks;
    long _pause3_start_tick;
    long _pause3_ticks;
    long _relocate_start_tick;
    long _pause4_count;
    long _first_pause4_start_tick;
    long _pause4_start_tick;
    long _pause4_ticks;
    long _pause4_max;

    long _cycle_start_words_in_use;
    long _cycle_start_new_words_in_use;
    long _cycle_start_old_words_in_use;
    long _cycle_start_perm_words_in_use;
    long _pause1_words_in_use;
    long _pause2_words_in_use;
    long _pause3_words_in_use;
    long _page_sort_words_in_use;
    long _pause4_words_in_use;
    long _pause4_live_words_in_use;
    long _pause4_fragmented_words;
    long _cycle_end_words_in_use;
    long _cycle_end_new_words_in_use;
    long _cycle_end_old_words_in_use;
    long _cycle_end_perm_words_in_use;

    long _cycle_end_gen_frag_words;
    long _cycle_end_gen_small_frag_words;
    long _cycle_end_gen_mid_frag_words;
    long _cycle_end_gen_large_frag_words;

    long _cycle_end_gen_small_pages;
    long _cycle_end_gen_mid_pages;
    long _cycle_end_gen_large_pages;

    long _committed_used;
    long _unreturned_pause;

    long _peak_committed_used;
    long _peak_grant_used;
    long _peak_non_pause_used;
    long _peak_pause_used;

    long _promoted_to_old_source;
    long _promoted_to_old_target;

    long _last_cycle_end_tick;
    long _last_cycle_end_words_in_use;
    long _last_cycle_end_new_words_in_use;
    long _last_cycle_end_old_words_in_use;
    long _last_cycle_end_perm_words_in_use;

    bool _cycle_used_pause;
    bool _cycle_past_committed;
    bool _cycle_pause_exceeded;

    long _garbage_words_found;
    long _garbage_words_collected;
    long _sideband_words_limited;
    long _norelocate_pages;
    long _pages_promoted_to_old;
    long _pages_relocated;

    long _reloc_spike_pages;

    long _collection_delta_words;

    double _intracycle_alloc_rate;

    // Mutator threads blocked on allocation failure:
    long   _threads_delayed;
    double _average_thread_delay;
    double _max_thread_delay;

  public:
    GPGC_CycleStats();

    void   reset                      ();

    void   start_cycle                ();
    void   end_cycle                  ();
    long   pause1_start_ticks         ()                { return _pause1_start_tick; }
    void   start_pause1               (long start_ticks);
    void   end_pause1                 (long pause_ticks);
    void   end_pause1                 ();
    void   start_pause2               (long start_ticks);
    void   end_pause2                 (long pause_ticks);
    void   start_pause3               (long start_ticks);
    void   end_pause3                 (long pause_ticks);
    void   start_relocate             ();
    void   heap_at_page_sort          (long heap_words);
    void   heap_after_reloc_setup     (long words_found,
                                       long words_to_collect,
                                       long sideband_limited_words);
    void   start_pause4               (long start_ticks);
    void   end_pause4                 (long pause_ticks);
    void   heap_at_pause4             (long heap_words,
                                       long live_object_words,
                                       long fragmented_words);
    void   heap_at_end_relocate       (long collection_delta_words,
                                       long peak_relocation_page_count,
                                       long norelocate_pages,
                                       long promoted_pages,
                                       long relocated_pages);

    long   get_page_sort_words_in_use ()                { return _page_sort_words_in_use; }

    long   cycle_start_tick           ()                { return _cycle_start_tick; }
    long   cycle_ticks                ()                { return _cycle_ticks;      }

    long   pause4_fragmented_words    ()                { return _pause4_fragmented_words; }

    long   start_words_in_use         ()                { return _cycle_start_words_in_use; }
    long   start_old_words_in_use     ()                { return _cycle_start_old_words_in_use; }
    long   start_perm_words_in_use    ()                { return _cycle_start_perm_words_in_use; }

    long   end_new_words_in_use       ()                { return _cycle_end_new_words_in_use; }
    long   end_old_words_in_use       ()                { return _cycle_end_old_words_in_use; }
    long   end_perm_words_in_use      ()                { return _cycle_end_perm_words_in_use; }

    long   peak_committed_used        ()                { return _peak_committed_used; }
    void   set_peak_committed_used    (long peak)       { _peak_committed_used = peak; }

    long   peak_pause_used            ()                { return _peak_pause_used; }
    void   set_peak_pause_used        (long peak)       { _peak_pause_used = peak; }

    long   unreturned_pause           ()                { return _unreturned_pause; }
    void   set_unreturned_pause       (long unreturned) { _unreturned_pause = unreturned; }

    long   pages_promoted_to_old      ()                { return _pages_promoted_to_old; }
    double intracycle_alloc_rate      ()                { return _intracycle_alloc_rate; }

    long   sideband_words_limited     ()                { return _sideband_words_limited; }

    void   threads_delayed            (long threads, double average_delay, double max_delay);

    long   get_threads_delayed        ()                { return _threads_delayed;      }
    double get_average_thread_delay   ()                { return _average_thread_delay; }
    double get_max_thread_delay       ()                { return _max_thread_delay;     }

    void   verbose_gc_log             (const char* type, const char* label);
};


class GPGC_Stats VALUE_OBJ_CLASS_SPEC
{
  private:
    GPGC_CycleStats _new_gc_stats;
    GPGC_CycleStats _old_gc_stats;

  public:
    GPGC_Stats();

    GPGC_CycleStats* new_gc_cycle_stats ()        { return &_new_gc_stats; }
    GPGC_CycleStats* old_gc_cycle_stats ()        { return &_old_gc_stats; }

    void   init_stats                   ();
};

#endif // GPGC_STATS_HPP
