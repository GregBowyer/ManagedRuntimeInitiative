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
#ifndef SBATHREADINFO_HPP
#define SBATHREADINFO_HPP


 
// Stack Based Allocation

#include "stackBasedAllocation.hpp"
#include "stackRef_pd.hpp"
class BufferedLoggerMark;

// Structure for controlling thread-local and stack-based object allocation.
// Lazily created as JavaThreads actually create their first object.

struct SBAArea {
  static void initialize();
private:
  static size_t    _bytes_per_sba; // Size of each SBA area in bytes
  static int       _freebits_cnt;  // Count of BITS in free_map
  static uint64_t *_free_map;      // 1-bit-per-sba free_map; ZERO means free, ONE means in-use
  static address get_reserved(); // Call os::reserve_memory
public:
  enum { 
    InitialStackSize = 512*K
  };

  JavaThread *const _jt;        // The thread this structure is bound to
  
  // Non-moving thread-local reserved virtual-memory heap area.  Size is SBAStackSize doubled.
  address const _reserved_heap;
  intptr_t _committed_bytes;    // Bytes of space actually backed with physical memory

  uint32_t ABase() const {
  return 0;
  }

  // Start of the current thread-local heap.  This start area 'flip-flops'
  // between the From-space and To-space areas allocated at the start of the
  // reserved heap.  The next allocation address in this space is held in the
  // owning JavaThread's _sba_top field and the end of this space is in the
  // JavaThread's _sba_max field.
address _start;//SBA Thread-Local Object area start

  // Make an initial SBA area
  SBAArea( JavaThread *jt );
  ~SBAArea();
  // Make a new allocation-ready SBA heap at 'start' of size 'size'
  void setup_alloc_space( address start, intptr_t size);

  bool is_in( address x, address top ) const { return _start <= x && x < top; }
  bool is_in_or_oldgen( address x ) const { return _reserved_heap <= x && x < _reserved_heap+_committed_bytes; }

  stackRef top() const;         // Includes FID and space-id
  void set_top(address top);
  
  int _fid_no_cap;              // Endless FID counter, used to balance push/pop
  int capped_fid() const { return MIN2(_fid_no_cap,SBA_MAX_FID-1); }
  void push_frame();            // Push a new SBA frame
  void pop_frame ();            // Pop the last SBA frame off

  // Per-Frame Top: where to roll-back allocation (top) on a frame-pop.
  // Current frame-top is kept in the thread struct.  These hold old pushed
  // frame-tops as offsets from _start.  As an invariant, objects with younger
  // (larger) FIDs cannot be above these offsets.
  uint32_t _fid_top[SBA_MAX_FID];

  // Allocate on behalf of the VM.
  // Return NULL if cannot stack allocate.
  // Use -1 for object allocations, or a positive integer length for arrays
  stackRef allocate( klassRef klass, size_t size_in_words, int length, intptr_t sba_hint) {
    if( !sba_hint ) return nullRef;
    return allocate_impl(klass,size_in_words,length,sba_hint);
  }
  stackRef allocate_impl(klassRef klass, size_t size_in_words, int length, intptr_t sba_hint);

  // Check for escape
  void check_escape( int new_fid, objectRef *p );

  // Do a stack-local GC
void collect(int size_in_bytes);
  size_t policy_resize( int alloc_bytes, size_t tospace_bytes, size_t total_live, BufferedLoggerMark *blm );
  uint64_t _ticks_at_last_gc;   // used to control growing the SBA area
  jint     _stack_collections;  // Number of stack GCs
  uint32_t _moved_objs;         // Total objects moved during stack GCs
  uint64_t _moved_bytes;        // Total bytes moved to TO-space during stack GCs
  uint64_t _moved_heap_bytes;   // Total bytes moved to heap     during stack GCs

  // Derived
  void add_derived_stack_oop(objectRef* base, objectRef* derived);

  // The normal GCs might have done "full" collections - moved methodOops or
  // klasses.  MethodOops are used in allocation-site hints, and klasses of
  // dead SBA objects are not walked and so go stale - and so I cannot call
  // size() on dead SBA objects and thus parse the heap.
  //
  // My fix is to do a Stack GC at the next Escape.  The Stack GC will remove
  // dead SBA objects, so there's no problem parsing the heap.  Also I'll kill
  // off any suspicious-looking methodOop hints.
  void gc_moved_klasses_and_moops() { _gc_moved_klasses_and_moops = true; }
  bool _gc_moved_klasses_and_moops;

  // If the heap is not parsable, do a stack-GC to force it to be parseable
  // (by removing all dead objects which might have stale klass refs).  The
  // extra argument is carried live across the GC as a service.
  objectRef make_heap_parsable( stackRef escapee );

  // verify
  void verify( );
  long _last_verify;            // Throttle expensive verify

  // printing
  bool was_used() const;        // True if any SBA action at all
  void print_to_xml_statistics(xmlBuffer *xb) const;
void print_statistics(outputStream*st)const;
  void print_on(const outputStream* st) const;  
  void print() const { print_on(tty); }         // Warning!  This is Big!

  uint32_t _heap_escape_events,  _stack_escape_events;
  uint32_t _heap_escape_objects, _stack_escape_objects;
  uint32_t _heap_escape_bytes,   _stack_escape_bytes;
};

// Used by the oops-do closures
class SbaClosure:public OopClosure{
protected:
  static int scale(uint byteoffset); // Scale byteoffsets for better bit-packing
  int const _v_size;            // Visit bits size in bytes
  char * _visit;                // Visit bits, lazily allocated and zeroed
  int _lo;
public:

  JavaThread *const _jt;        // Thread running the c

  int test( oop p );            // Test visited bit
  int test_set( oop p );        // Test and set visited bit
  int test_clr( oop p );        // Test and clr visited bit

  SbaClosure( JavaThread *jt);
  SbaClosure( SbaClosure *old);
virtual void do_oop(objectRef*p)=0;
};

#endif // SBATHREADINFO_HPP
