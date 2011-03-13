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
#ifndef CODECACHE_HPP
#define CODECACHE_HPP

#include "allocation.hpp"
#include "codeBlob.hpp"
#include "refsHierarchy_pd.hpp"


// Note: this is a complete rewrite of a file with the same name in Sun's distribution.
// As far as I know, no code remained in common.

// The CodeCache implements the code cache for various pieces of generated
// code, e.g., compiled java methods, runtime stubs, transition frames, etc.
// The entries in the CodeCache are all CodeBlob's.
class CodeBlob;
class BoolObjectClosure;
class GPGC_GCManagerOldStrong;
extern "C" {
  typedef struct hotspot_to_gdb_symbol hotspot_to_gdb_symbol_t;
};

// The CodeCache is required to return 32-bit addresses, so PC's can be stored
// in 32 bits.  This is for efficient code on many processors (Sparc & X86 included).

// Code does not move; it's very difficult to do so cheaply (it's do-able by
// stopping all threads and fixing up every call site in the code cache; for a
// large app this might be 50M of code with 1-in-20 instructions being calls).

// Code tends to come in larger chunks (most C2 gen'd methods will be 4K to
// 40K of code), and not very often (once per compile; perhaps a few thousand
// times for a large app).  Hence it's not *crucial* to be multi-threaded
// fast.  There are some code patterns which are very slow to delete and
// small, e.g. methodStubBlobs and native-method wrappers.  These I might use
// different heuristics for.

// I plan on only keeping code in the CodeCache, and keeping all the methodCode
// control logic+structures in the heap.

// Because of a long history of flakiness, especially when removing code, I've
// decided to implement a more robust deletion mechanism.

// ---

// Since I'll be allocating in a handful of large (4k? 16k?) chunks, I plan on
// using a bit-vector free-map.  Allocation consists of CAS'ing bits to claim
// space, then making those pages R/W'able.  Free'd pages will go on the aging
// list first, then when they are actually free'd I'll CAS the bits back on.
// This bitvector approach is fast & simple, but only works well if the search
// for empty pages isn't hugely long.
//
// My current design is to allocate whole CodeBlobs (results of a single
// Assembly or Compile) in VM_PAGE_SIZE aligned regions, and use mmap to allow
// full permissions.  On delete, the region will have all permissions revoked
// and it will sit on a debug-only aging list; after a minute or so has past
// the storage will be moved over to the normal free-map mechanism.
//
// The lifecycle of a CodeBlob will be:  
// - bits CAS'd from _freebits to allocate
// - pages MMAP'd to R/W
// - code written in
// - code "installed"; MMAP'd to R/W/E
// - code in use
// - code freed: linked onto _freed list and MMAP'd to no-permissions
// - After _aging_list is >60sec old
// - - Free (CAS set) bits for all CodeBlobs in _aging_list
// - - Move _freed_list to _aging_list and reset _age_in_msec timer
class CodeCache : AllStatic {
  friend void codeCache_init();
  static void *_codecache;      // The Whole Shebang
  static size_t _used_in_bytes; // Approximate (racey) bytes-in-use
  static int _freebits_cnt;     // Count of BITS in free_map
  static uint64_t *_free_map;   // 1-bit-per-page free_map; ZERO means free, ONE means in-use
  static uint64_t *_start_map;  // Map of starting pages of CodeBlobs
  static CodeBlob *_aging_list; // Free'd CodeBlobs that have begun to age
  static uint64_t  _aging_began;// Time when aging began, in os::ticks
  static CodeBlob *_freed_list; // Things deleted, but not yet on the aging list

  // We just claimed OS pages (wordnum*64+shift) for a run of 'pgs'.  These
  // pages should all be untouchable right now - make them read/write/exec
  // now, and create a Blob there.
  static CodeBlob *allow_exec( CodeBlob::BlobType type, int wordnum, int shift, int64_t pgs );

public:
  // Iteration
  //     FOR_ALL_CODEBLOBS(cb) {
  //        cb->stuff();
  //     }
  //
  // Currently this is only safe at a Safepoint, because otherwise a brand-new
  // just-being-allocated CodeBlob will show up in the free/start maps but not
  // have his memory made readable yet.
#define FOR_ALL_CODEBLOBS(CB) for(CodeBlob *CB, *IDX=0; (intptr_t)IDX<CodeCache::max_idx(); IDX=(CodeBlob*)((intptr_t)IDX+1) ) if( (CB=CodeCache::start_blob((intptr_t)IDX))!=0 ) 
  static inline int max_idx() { return _freebits_cnt; }
  static inline CodeBlob *start_blob(int idx) { 
    return (_start_map[idx>>6] & (1LL<<(idx & 63))) ? (CodeBlob*)((address)_codecache + (idx<<LogBytesPerSmallPage)) : NULL; 
  }
  
  // Create a new Blob in the CodeCache with this minimum size
  static CodeBlob* malloc_CodeBlob( CodeBlob::BlobType type, int min_sz );
  // Stretch the CodeBlob in-place, or report failure if can't reach the required size
  static bool extend( CodeBlob *, int min_sz );
  // Free this blob back to the CodeCache.  To help catch lingering uses, the memory
  // for it will be protected from further use for awhile.
  static void free_CodeBlob(CodeBlob*);

  // Utility functions
static bool contains(void*p){
    uintptr_t bitnum = ((char*)p - (char*)_codecache)>>LogBytesPerSmallPage;
    return (bitnum < (uint)_freebits_cnt) && // unsigned compare to force negative bitnum to fail
      (_free_map[bitnum>>6] & (1LL<<(bitnum&63))); // and location is in-use
  }

  // Find_blob is a little tricky with the lifetimes.  The CodeBlob might
  // disappear at the very next Safepoint, unless the embedded owner oop is
  // held in a Handle across the safepoint.
  static CodeBlob* find_blob(void* start);         

  static CodeBlob* find_blob_unsafe(void* start) {
CodeBlob*result=find_blob(start);
assert(result==NULL||result->contains((address)start),"found wrong CodeBlob");
    return result;   
  } 

  // GC support.  
static void oops_do(OopClosure*f);//visit methodCodeStubs and back ptrs
static void MSB_oops_do(OopClosure*f);//visit methodCodeStubs only
  // Remove CodeBlobs who's owning MCO is dead.
  static void unlink(BoolObjectClosure* is_alive);
  static void GPGC_unlink();

  // Incremental sweeping of inline-caches to 'clean'.  This prevents bad IC
  // predictions from holding onto otherwise dead classes.  Rapid sweeping
  // increases resolve_and_patch costs.  Sweeping costs are throttled here.
  // It is performance-safe to call this routine very often.
  static void clean_inline_caches();

  // A rare couple of places where the *Caller* (not Callee) must gc the
  // incoming arguments.
static address _caller_must_gc_args_0;
static address _caller_must_gc_args_1;

  // Things I think the new CodeCache never needs
  static void do_unloading(BoolObjectClosure* is_alive, OopClosure* keep_alive, bool unloading_occurred) { }

  // Printing and debugging
  static void print_xml_on(xmlBuffer*);
  static void print_on(outputStream *);
  static void print(); 
  static void print_internals();
  static void verify();
  static size_t used_in_bytes() { return _used_in_bytes; }
  static size_t max_capacity_in_bytes() { return ReservedCodeCacheSize; }
  static size_t unallocated_capacity() { return max_capacity_in_bytes() - used_in_bytes(); }
  static bool is_full()        { return unallocated_capacity() < CodeCacheMinimumFreeSpace; }
  static bool is_almost_full() { return unallocated_capacity() < (max_capacity_in_bytes()>>3); }
  static hotspot_to_gdb_symbol_t *get_new_hsgdb();
};


// Tom K hack to GDB

/*************************************************************************/
/* AZUL HOTSPOT TO GDB SYMBOL TABLE                                      */
/*************************************************************************/
extern "C" {
typedef struct hotspot_to_gdb_symbol {
uint32_t startPC;
  uint32_t codeBytes;           // - startPC + codeBytes == endPC
                                // - endPC < (next) startPC
                                // - no two symbols should have the same startPC
uint16_t frameSize;
  const char *nameAddress;      //  Address of symbol name string
  uint16_t nameLength; // strlen() of string (so not including null terminator) 

  uint8_t  savedRBP;            // - true or false.  If true, caller's RBP is
                                // just above the callee's frame.  if false,
                                // the caller's RBP is unsaved.
  uint8_t  pad1;                // Must be zero
  uint8_t  pad2;                // Must be zero
  uint8_t  pad3;                // Must be zero
} hotspot_to_gdb_symbol_t;

typedef struct hotspot_to_gdb_symbol_table {
  uint64_t version;             // Start with 1. 
  uint64_t generationNumber;    // GDB's cache gets blown away and rebuilt
                                // from scratch when this number changes.
                                // Monotonically increasing.
  uint32_t numEntries;          // Number of symbols known by hotspot.
  uint32_t maxEntries;          // For use by HotSpot
  hotspot_to_gdb_symbol_t *symbolsAddress; // Pointer to array.
} hotspot_to_gdb_symbol_table_t;

extern hotspot_to_gdb_symbol_table_t HotspotToGdbSymbolTable;
};



#endif // CODECACHE_HPP
