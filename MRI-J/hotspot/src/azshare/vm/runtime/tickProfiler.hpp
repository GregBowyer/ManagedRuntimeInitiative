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
#ifndef TICKPROFILER_HPP
#define TICKPROFILER_HPP


#include <signal.h>


#include "atomic.hpp"
#include "os.hpp"
#include "os_os_pd.hpp"
#include "prefetch.hpp"
#include "task.hpp"
#include "vmTags.hpp"
#include "thread.hpp"

class ProfileIterator;


namespace tickprofiling {
    void init();
    void thread_init_callback(intptr_t);
}



class ProfileEntry {
  friend class TickProfiler;
 public:
  enum {
    _rpc_count = 8
  };

 protected:
  // This structure must be cache-line sized - so it can be cleared with 
  // fast hardware cache-line-zero (if possible).  must be cache-line 
  // aligned for the same reason.
  intptr_t _entry;              // 8  // 1st cache line
  intptr_t _u1;                 // 8
  intptr_t _u2;                 // 8
  intptr_t _u3;                 // 8
  uint32_t _rpcs[_rpc_count];   // 8*4 // 2nd cache line

 public:

  enum {
    type_shift = 0,
    type_bits = 3,
    type_mask = right_n_bits(type_bits),

    event_shift = type_shift + type_bits,
    event_bits = 6,
    event_mask = right_n_bits(event_bits),

    cpu_id_shift = event_shift + event_bits,
    cpu_id_bits = 10,
    cpu_id_mask = right_n_bits(cpu_id_bits),

    safep_suspend_shift = cpu_id_shift + cpu_id_bits,
    safep_suspend_bits = 1,
    safep_suspend_mask = right_n_bits(safep_suspend_bits),

    // The code currently uses Thread::reversible_tid() for the thread
    // being profiled. The value is shifted right 21 bits in the
    // current implementation, which means we want 43 bits for a tid
    // here. We end up with 44 since I scaled down the number of
    // processors for the cpu_id.
    thread_id_shift = safep_suspend_shift + safep_suspend_bits,
    thread_id_bits = BitsPerOop - (type_bits + event_bits + cpu_id_bits + safep_suspend_bits),
    thread_id_mask = right_n_bits(thread_id_bits)
  };

  enum EntryTypes {
    perfcnt0_tick  = 0,
    perfcnt1_tick  = 1,
    perfcnt4_tick  = 2,
    perfcnt5_tick  = 3,
    tlb0_tick      = 4,
    tlb1_tick      = 5,
    meta_tick      = 6,
    slave_tick     = 7,
    default_tick   = 0
  };

  static bool is_type(int type) {
    return
      (type == perfcnt0_tick) ||
      (type == perfcnt1_tick) ||
      (type == perfcnt4_tick) ||
      (type == perfcnt5_tick) ||
      (type == tlb0_tick) ||
      (type == tlb1_tick) ||
      (type == meta_tick);
  }

  static bool is_type_enabled(int type) {
    switch (type) {
    case perfcnt0_tick: return UseTickProfiler;
    case meta_tick: return UseMetaTicks;
    default: ShouldNotReachHere(); return false;
    }
  }

  static void print_xml_on(xmlBuffer*);

  static ProfileEntry* allocate();

  void clear()                 { _entry = _u1 = _u2 = _u3 = 0; }

EntryTypes type()const{
    return (EntryTypes) ((_entry >> type_shift) & type_mask);
  }
  int event() const {
    return (int) ((_entry >> event_shift) & event_mask);
  }
  int64_t cpu_id() const {
    return (int64_t) ((_entry >> cpu_id_shift) & cpu_id_mask);
  }
  int64_t thread_id() const {
    return (int64_t) ((_entry >> thread_id_shift) & thread_id_mask);
  }
  bool safep_suspend() const {
    return ((_entry >> safep_suspend_shift) & safep_suspend_mask) != 0;
  }
  jlong    timestamp()    const { return (jlong)_u3; }

  bool is_perfcnt0_tick() const {return type() == perfcnt0_tick;}
  bool is_perfcnt1_tick() const {return type() == perfcnt1_tick;}
  bool is_perfcnt4_tick() const {return type() == perfcnt4_tick;}
  bool is_perfcnt5_tick() const {return type() == perfcnt5_tick;}
  bool is_perfcnt_tick() const {return is_perfcnt0_tick() || is_perfcnt1_tick() || is_perfcnt4_tick() || is_perfcnt5_tick();}
  bool is_tlb0_tick() const {return type() == tlb0_tick;}
  bool is_tlb1_tick() const {return type() == tlb1_tick;}
  bool is_tlb_tick() const {return is_tlb0_tick() || is_tlb1_tick();}
  bool is_meta_tick() const {return type() == meta_tick;}

  const uint32_t* rpcs() const {return _rpcs;}

  // Fast zero of this structure - which should be a cache-line aligned thing.
  inline ProfileEntry* clz() {
    assert0( round_down((intptr_t)this,BytesPerCacheLine) == (intptr_t)this );
#if defined(AZ_X86)
    assert0( sizeof(*this) == BytesPerCacheLine );
    Prefetch::overwrite(this,0); // Just normal on X86
#endif
    return this;
  }

 protected:
  void set_entry(EntryTypes _type, int _event, int64_t _thread_id, int _safep_suspend) {
    int64_t _cpu_id = os::current_cpu_id();
    assert0((_type & type_mask) == _type);
    assert0((_event & event_mask) == _event);
    assert0((_cpu_id & cpu_id_mask) == _cpu_id);
    assert0((_safep_suspend & safep_suspend_mask) == _safep_suspend);
    assert0((_thread_id & thread_id_mask) == _thread_id);
    _entry =
      ((_type & type_mask) << type_shift) |
      ((_event & event_mask) << event_shift) |
      ((_cpu_id & cpu_id_mask) << cpu_id_shift) |
      ((_safep_suspend & safep_suspend_mask) << safep_suspend_shift) |
      ((_thread_id & thread_id_mask) << thread_id_shift);
  }

  void set_rpcs( intptr_t pc, intptr_t sp, intptr_t fp );
};

class UserProfileEntry : public ProfileEntry {
  // intptr_t _entry == thread
  // intptr_t _u1    == pc
  // intptr_t _u2    == tag
  // intptr_t _u3    == timestamp

 public:
  address  pc()           const { return (address)_u1; }
  intptr_t tag()          const { return _u2; }

  void set_values( EntryTypes type, int event, int64_t thread_id, int safep_suspend, intptr_t pc, intptr_t sp, intptr_t fp, intptr_t tag, jlong timestamp )
    {
	assert0((type == perfcnt0_tick) || (type == perfcnt1_tick) || (type == perfcnt4_tick) || (type == perfcnt5_tick));
	set_rpcs(pc,sp,fp);           // Pass in SP for stack crawl start
	set_entry(type, event, thread_id, safep_suspend);
_u1=pc;
_u2=tag;
	_u3 = (intptr_t) timestamp;
	
    }
};

class MetaProfileEntry : public ProfileEntry {
  // intptr_t _entry == thread
  // intptr_t _u1    == meta_tick & meta_info
  // intptr_t _u2    == tag
  // intptr_t _u3    == timestamp

 public:

  enum {
    meta_tick_bits = 16,
    meta_info_bits = BitsPerOop - meta_tick_bits
  };

  enum {
    meta_tick_shift = 0,
    meta_info_shift = meta_tick_shift + meta_tick_bits
  };

  enum {
    meta_tick_mask = right_n_bits(meta_tick_bits),
    meta_info_mask = right_n_bits(meta_info_bits)
  };

  intptr_t tag()          const { return _u2; }
  jlong    timestamp()    const { return _u3; }
  int      meta_tick()    const { return (int)((_u1 >> meta_tick_shift) & meta_tick_mask); }
  intptr_t meta_info()    const { return      ((_u1 >> meta_info_shift) & meta_info_mask); }

    void set_values(int64_t thread_id, int meta, intptr_t info, intptr_t pc, intptr_t sp, intptr_t fp, intptr_t tag, jlong timestamp ) {
	set_rpcs(pc,sp,fp);
	set_entry(ProfileEntry::meta_tick, 0, thread_id, false);
	_u1 = (info << meta_info_shift) | ((intptr_t) meta << meta_tick_shift);
_u2=tag;
	_u3 = (intptr_t)timestamp;
    }
};

class TlbProfileEntry : public ProfileEntry {
  // intptr_t _entry == thread
  // intptr_t _u1    == pc
  // intptr_t _u2    == va
  // intptr_t _u3    == timestamp

 public:
  address  pc()           const { return (address)_u1; }
  intptr_t va()           const { return _u2; }
  jlong    timestamp()    const { return (jlong)_u3; }

  void set_values( EntryTypes type, int64_t thread_id, int safep_suspend, intptr_t pc, intptr_t sp, intptr_t fp, intptr_t va, jlong timestamp );
};

inline void TlbProfileEntry::set_values( EntryTypes type, int64_t thread_id, int safep_suspend, intptr_t pc, intptr_t sp, intptr_t fp, intptr_t va, jlong timestamp ) {
  assert0((type == tlb0_tick) || (type == tlb1_tick));
  set_rpcs(pc,sp,fp);
  set_entry(type, 0, thread_id, safep_suspend);
_u1=pc;
  _u2 = va;
  _u3 = (intptr_t) timestamp;
}

class TickEntryArray; // forward definition of local class

// Global tick profile.
class TickProfiler {
  friend class ExternalProfiler;
  friend class ProfileIterator;

 private:
  intptr_t _next_idx;
  ProfileEntry *const _entries; // Created once in the constructor and never changes.

  // Used by ExternalProfiler
  static intptr_t next_idx()     { return (_profile == NULL)?0 : _profile->_next_idx; } // My kingdom for a lock!

  // this is the running live profile; a "golden object".
  // Set once during startup and never changes again.
  // The "never changes" property is needed so that the repeated reads all
  // return the same non-null value.
  static TickProfiler* _profile;

  // Data collection
  static TickEntryArray* collect_data(ProfileIterator& it, int &tick_count);

  static bool _can_get_thread_cpu_time;

 public:
  // If TickProfilerCount > 0 and TickProfilerControlWord = 0x2 then we can get
  // cpu times.
  static void set_can_get_thread_cpu_time(bool on) { _can_get_thread_cpu_time = on; }
  static bool can_get_thread_cpu_time() { return _can_get_thread_cpu_time; }

  // The perfcnt0, perfcnt1, and tlb entry points are called directly from the
  // perfcounter exception handler
  static void    slave_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva);
  static void perfcnt0_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva);
  static void     ttsp_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva);
  static void     tlb0_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva);
  static void     tlb1_tick(uint64_t estate, uint64_t epc, uint64_t esp, uint64_t efp, uint64_t eva);
  static void     meta_tick(int meta, intptr_t info = 0, intptr_t tag = 0);

  inline static void record_ttsp_tick(JavaThread* jt);
static void ttsp_evaluate(JavaThread*jt);

  TickProfiler();

    /**
     * Move the tick profile global buffer forward the given number of
     * entries and return the previous entry.
     */
    static intptr_t increment_index(intptr_t size) {
	intptr_t new_index = Atomic::add_ptr(size, &_profile->_next_idx);
	new_index &= TickProfilerEntryCount - 1;
	return new_index;
    }

    static TickProfiler* instance() { return _profile; }

  static ProfileEntry* global_entry(int entry_count = 1) {
    // the not-hand-inlined-version
    //return entry_at((Atomic::add_ptr(1, &_profile->_next_idx) - 1) & (length()-1));
    // the hand-inlined version, done for speed I assume
      //    return &_profile->_entries[(Atomic::add_ptr(1, &_profile->_next_idx) - 1) & (TickProfilerEntryCount-1)];

      // and now, the hand-outlined version
      // increment the _next_idx ptr and use the old value
      intptr_t current_idx = Atomic::add_ptr(entry_count, &_profile->_next_idx);
      current_idx -= entry_count;
      // this is ring buffer magic. we have n ProfileEntry slots, and we mask off the high bits of the index it's still valid 
      // when it rolls past n.
      current_idx &= (TickProfilerEntryCount-1); 
      return &_profile->_entries[current_idx];
  }

int length()const{return TickProfilerEntryCount;}

  ProfileEntry* entries() { return _entries; }

  ProfileEntry* entry_at(int idx) {
    assert0(idx < TickProfilerEntryCount);
    return &_entries[idx];
  }

  // Allocate space for the entries and initialize datastructures.
  static void init();
  static bool is_profiling() { return _profile != NULL; }
  static void reset();

  // Printing
  static void print(double tickcutoff = 0.1);
  static void print_timeline();
  static void print_xml_on(xmlBuffer *xb, ProfileIterator& it, double tickcutoff);
};

inline void meta_tick_vmlock_acquire( intptr_t lock ) { TickProfiler::meta_tick(vmlock_acquired_tick,lock); }
inline void meta_tick_vmlock_release( intptr_t lock ) { TickProfiler::meta_tick(vmlock_released_tick,lock); }

class SlaveTickProfileEntry : public ProfileEntry {
  enum {
    index_shift = type_shift + type_bits,
    index_bits = 16,
    index_mask = right_n_bits(index_bits)
  };

  // format for slave ticks:
  // intptr_t _entry: slave_tick
  // intptr_t _u1: 63-32: index,    31-0: perfcnt1
  // intptr_t _u2: 63-32: perfcnt4, 31-0: perfcnt5
  // intptr_t _u3: 63-0:  timestamp

  public:
  void set_values(int64_t index, int64_t pcnt1, int64_t pcnt4, int64_t pcnt5, jlong timestamp)
  {
    _entry = (slave_tick & type_mask) << type_shift;
    _u1 = (index << 32) | pcnt1;
    _u2 = (pcnt4 << 32) | pcnt5;
_u3=timestamp;
  }

};

class ProfileFilter {
public:
  ProfileFilter();

  int type() const {return _type;}
  void set_type(int k) {_type = k;}

  int tag() const {return _tag;}
  void set_tag(int k) {_tag = k;}

  int safep_suspend() const {return _safep_suspend;}
  void set_safep_suspend(int k) {_safep_suspend = k;}

  int64_t thr() const {return _thr;}
  void set_thr(int64_t t) {_thr = t;}

  address low() const {return _low;}
  address hi() const {return _hi;}
  void set_range(address l, address h) {_low = l; _hi = h;}

  size_t matched() const {return _matched;}
  size_t unmatched() const {return _unmatched;}

  bool from_req(azprof::Request*);
  bool from_xb(xmlBuffer*);
  bool matches(ProfileEntry*);
  void print_xml_on(xmlBuffer*);

private:
  int _type;
  int _tag;
  int _safep_suspend;
  int64_t _thr;
address _low;
address _hi;
  size_t _matched;
  size_t _unmatched;
};

class ProfileIterator {
public:
  ProfileIterator();
  ProfileIterator(azprof::Request*);
  ProfileIterator(xmlBuffer*);

  ProfileFilter& filt() {return _filt;}
ProfileEntry*next();

private:
  ProfileFilter _filt;
  int _idx;
};

// A node in a tree built out of the tick RPC data. We take the fixed-depth RPC
// vectors recorded in ticks and form a tree of all the sequences found in ticks
// for an instruction sequence. Annotating each node with the number of ticks
// for which it occurred.
class RpcTreeNode:public ResourceObj{
public:
  static void print_xml(xmlBuffer *xb, address low, address hi, bool invert);

  RpcTreeNode(uint32_t __low, uint32_t __hi) :
    _low(__low), _hi(__hi), _ticks(0), _next(NULL), _child(NULL) {}

  uint32_t low() const {return _low;}
  uint32_t hi() const {return _hi;}
  uint32_t ticks() const {return _ticks;}
RpcTreeNode*next(){return _next;}
  RpcTreeNode* child() {return _child;}

  bool contains(uint32_t pc) const {return (_low <= pc) && (pc <= _hi);}

private:
  static bool pc_to_range(uint32_t pc, uint32_t& low, uint32_t& hi);

  RpcTreeNode* new_sibling(uint32_t pc);
  RpcTreeNode* new_child(uint32_t pc);

  RpcTreeNode* tick(const uint32_t *rpcs, int len, bool invert);
  void print_xml(xmlBuffer *xb);

uint32_t _low;
uint32_t _hi;
uint32_t _ticks;
RpcTreeNode*_next;
  RpcTreeNode *_child;
};

#endif // TICKPROFILER_HPP
