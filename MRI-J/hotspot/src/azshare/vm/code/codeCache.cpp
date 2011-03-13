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


#include "codeCache.hpp"
#include "commonAsm.hpp"
#include "globals.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "memoryService.hpp"
#include "modules.hpp"
#include "os.hpp"
#include "safepoint.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

void *    CodeCache::_codecache;    // The Whole Shebang
size_t    CodeCache::_used_in_bytes;// Approximate (racey) bytes-in-use
int       CodeCache::_freebits_cnt; // Count of BITS in free_map
uint64_t *CodeCache::_free_map;     // 1-bit-per-page free_map
uint64_t *CodeCache::_start_map;    // Map of starting pages of CodeBlobs
CodeBlob *CodeCache::_aging_list;   // Free'd CodeBlobs that have begun to age
uint64_t  CodeCache::_aging_began;  // Time when aging began, in os::ticks
CodeBlob *CodeCache::_freed_list;   // Things deleted, but not yet on the aging list
address   CodeCache::_caller_must_gc_args_0; // Places where the *Caller* (not Callee) ...
address   CodeCache::_caller_must_gc_args_1; // ...must gc the incoming arguments.

// --- codeCache_init
void codeCache_init() {
  // MMAP the whole thing at once.  It will be lazily backed by real pages
  // as-needed.  Keep it in the low 32-bits (for 32-bit PC pointers).  Of
  // course, do not require any backing mapping since most of this space will
  // never be used.
  size_t sz = round_to(ReservedCodeCacheSize,os::vm_page_size());
  // memory has already been reserved as part of oopTable_init()
  CodeCache::_codecache =  (void *)__CODE_CACHE_START_ADDR__;
  //_codecache = mmap(NULL,sz,PROT_NONE,MAP_PRIVATE|MAP_32BIT|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
  if( CodeCache::_codecache == NULL ) Unimplemented();
  if( (((intptr_t)CodeCache::_codecache) >> 31) != 0 ) Unimplemented(); // not allocated in the low 2GB?

  CodeCache::_freebits_cnt = sz>>LogBytesPerSmallPage; // Exactly and only bits for the CodeCache
  int freewords = round_to(CodeCache::_freebits_cnt,64)/64;  // Round up to CAS'able chunks
  CodeCache::_free_map = (uint64_t*)os::malloc(freewords*8); // CAS'able chunks
  memset( CodeCache::_free_map,0,freewords*8);    // bitmap of free pages
  CodeCache::_start_map= (uint64_t*)os::malloc(freewords*8); // CAS'able chunks
  memset(CodeCache::_start_map,0,freewords*8);    // bitmap of CodeBlob page starts

  CodeCache::_aging_list = NULL;           // Nothing aging
  CodeCache::_freed_list = NULL;           // Nothing freed
  CodeCache::_aging_began= os::elapsed_counter();

  // MBean connection to CodeCache...
MemoryService::add_code_cache_memory_pool();

  // --- GDB Support
  // Initialize the list of GDB-visible symbols
  HotspotToGdbSymbolTable.version = 1; // Initial version
  HotspotToGdbSymbolTable.generationNumber = 1;
  HotspotToGdbSymbolTable.numEntries = 0;
  HotspotToGdbSymbolTable.maxEntries = 1;
  HotspotToGdbSymbolTable.symbolsAddress = (hotspot_to_gdb_symbol_t*)malloc( HotspotToGdbSymbolTable.maxEntries * sizeof(hotspot_to_gdb_symbol_t));
}

// --- malloc_CodeBLob
// Create a new Blob with this minimum size.  Since the new blob does
// not appear in any CodeOop, it is immune to being GC'd.
CodeBlob* CodeCache::malloc_CodeBlob( CodeBlob::BlobType type, int min_sz ) {
#ifdef ASSERT
  if( _aging_list != NULL ) Unimplemented(); // Need to move aged things around
#endif

  // This algorithm is a lock-free malloc; it finds a bit-run of the correct
  // size in a single word of the _free_map, and CAS's the bitrun set.  I
  // don't have implemented a version which handles bitruns that split across
  // CAS boundaries so this version might suffer from fragmentation.

  // Number of pages I need - which is also the number of bits in a row I need
  int pgs = (min_sz+BytesPerSmallPage-1)>>LogBytesPerSmallPage;
  if( pgs >= 64 )   // Don't have the multi-word version implemented; 
Unimplemented();//...this version is good for 64*vm_page_size bytes

  uint64_t mask = (1LL<<pgs)-1; // Mask of bits we need to find free (& CAS set) in one go
  int freewords = _freebits_cnt/64; // Round up to CAS'able chunks

  // Search word-by-word for a single word with enuf bits free.
  // (Doesn't search the last partial word.)
for(int i=0;i<freewords;i++){
    for( int shift=0; shift<=(64-pgs); shift++ ) {
      uint64_t bits = _free_map[i];
      if( (bits & (mask<<shift))==0 &&
          (uint64_t)Atomic::cmpxchg_ptr(bits|(mask<<shift),(intptr_t*)&_free_map[i],bits) == bits )
        return allow_exec( type, i, shift, pgs );
    }
  }
  // Out of Memory?  Or just bad fragmentation?
  Unimplemented();
  return NULL;
}

// --- allow_exec
// We just claimed OS page number 'pgnum' for a run of 'pgs'.  This pages
// should all be untouchable right now - make them read/write/exec now,
// then return the 1st page as a new CodeBlob.
CodeBlob *CodeCache::allow_exec( CodeBlob::BlobType type, int wordnum, int shift, int64_t pgs ) {
  // Compute the CodeBlob start address
  assert0( 0 <= shift && shift < 64 );
  int64_t pgnum = (wordnum*64)+shift;
  assert0( 0 <= pgnum && pgnum < _freebits_cnt );
  char *adr = (char*)_codecache + (pgnum<<LogBytesPerSmallPage);
  // Make the memory read/write/exec-able
  int64_t size_in_bytes = pgs<<LogBytesPerSmallPage;
  if( !os::commit_memory(adr, size_in_bytes, Modules::CodeCache, false) )
Unimplemented();//should not fail
  // Bump memory-in-use stats.  Not atomic with allocation, but close.
  Atomic::add_ptr(pgs<<LogBytesPerSmallPage,&_used_in_bytes);
  // Make a CodeBlob *here*
  CodeBlob *cb = new (adr) CodeBlob(type,size_in_bytes);
  
#ifndef PRODUCT
  if( PrintCodeCache ) {
    ResourceMark rm;
tty->print_cr("Alloc blob %p",cb);
  }
#endif

  // Set the start of this CodeBlob in the _start_map.  Must be done atomically 
  // because other threads will be setting other bits in the same word.
  // Must be done after init'ing the blob, so concurrent GPGC threads so
  // only well-formed CodeBlobs.
  uint64_t bits = _start_map[wordnum];
  assert0( (bits & (1LL<<shift))==0 );
  while( (uint64_t)Atomic::cmpxchg_ptr(bits|(1LL<<shift),(intptr_t*)&_start_map[wordnum],bits) != bits ) {
    bits = _start_map[wordnum];
    assert0( (bits & (1LL<<shift))==0 );
  }

  // GDB Support - info GDB of this blob
  if( type != CodeBlob::runtime_stubs ) {
MutexLocker ml(ThreadCritical_lock);
    hotspot_to_gdb_symbol_t *hsgdb = get_new_hsgdb();
    assert0( (char*)(uint32_t)(intptr_t)adr == adr ); // no truncation
    hsgdb->startPC = (uint32_t)(intptr_t)adr;
    hsgdb->codeBytes = size_in_bytes;
    hsgdb->frameSize = 0;          // To-Be-Filled-In
    hsgdb->nameAddress = cb->name(); // To-Be-Filled-In-Better
    hsgdb->nameLength = strlen(hsgdb->nameAddress);
    hsgdb->savedRBP = false;       // To-Be-Filled-In
    hsgdb->pad1 = hsgdb->pad2 = hsgdb->pad3 = 0;
    cb->_gdb_idx = HotspotToGdbSymbolTable.numEntries-1;
  } else {
    cb->_gdb_idx = 0xffff; // Runtime stubs do their own GDB hooks because framesizes differ across the blob
  }
  return cb;
}

// --- extend
// Extend this blob in-place, or report failure.  Grabs pages 1 bit at a time
// from the freemap, so partial success is possible - but we only report
// success if we get all pages.  No (true) leak on failure; we will have
// extended the CodeBlob - just not all the way.
bool CodeCache::extend( CodeBlob *blob, int min_sz ) {
  assert0( blob->_size_in_bytes < min_sz ); // expect to be growing
  int old_pgs = blob->_size_in_bytes>>LogBytesPerSmallPage;
  int new_pgs = (min_sz+BytesPerSmallPage-1)>>LogBytesPerSmallPage;
  int need_pgs = new_pgs - old_pgs; // Number of OS pages to claim
  // Page # (counting from 1st page in CodeCache) to claim
  int64_t pgnum = (blob->end() - (address)_codecache)>>LogBytesPerSmallPage;
for(int i=0;i<need_pgs;i++){//Loop grabbing pages 1 at a time
    int64_t wordnum = pgnum/64;
    int bit = pgnum - (wordnum*64);
    while( true ) {             // Retry if there is contention
      uint64_t bits = _free_map[wordnum];
      if( (bits & (1LL<<bit))!=0 ) return false; // failed to extend; bitmap already in use
      if( (uint64_t)Atomic::cmpxchg_ptr(bits|(1LL<<bit),(intptr_t*)&_free_map[wordnum],bits) == bits )
        break;                  // Success, break out
    }
    // Grabbed next page; increase CodeBlob size
    blob->_size_in_bytes += BytesPerSmallPage;
    if( blob->_gdb_idx != 0xffff ) {
MutexLocker ml(ThreadCritical_lock);
      HotspotToGdbSymbolTable.symbolsAddress[blob->_gdb_idx].codeBytes = blob->_size_in_bytes;
    }
    char *adr = (pgnum<<LogBytesPerSmallPage) + (char *)_codecache;
    if( !os::commit_memory(adr, BytesPerSmallPage, Modules::CodeCache, false) )
Unimplemented();//should not fail
    // Bump memory-in-use stats.  Not atomic with allocation, but close.
Atomic::add_ptr(BytesPerSmallPage,&_used_in_bytes);
    // Keep grabbing pages...
    pgnum++;
  }
debug_only(blob->verify());
  return true;
}

// --- free_CodeBlob
// Free this blob back to the CodeCache.  To help catch lingering uses, the memory
// for it will be protected from further use for awhile.
void CodeCache::free_CodeBlob(CodeBlob*blob){
#ifndef PRODUCT
  if( PrintCodeCache ) {
    ResourceMark rm;
    tty->print_cr("Free blob %p %s",blob,blob->name());
  }
#endif

  // GDB Support: unmap from GDB structures
  assert0( blob->_gdb_idx != 0xffff );
{MutexLocker ml(ThreadCritical_lock);
  // Do a simple compress of the GDB array; copy the last element over the removed one
  HotspotToGdbSymbolTable.numEntries--;
  HotspotToGdbSymbolTable.symbolsAddress[blob->_gdb_idx] = 
    HotspotToGdbSymbolTable.symbolsAddress[HotspotToGdbSymbolTable.numEntries];
  ((CodeBlob*)HotspotToGdbSymbolTable.symbolsAddress[blob->_gdb_idx].startPC)->_gdb_idx = blob->_gdb_idx;
  HotspotToGdbSymbolTable.generationNumber++;
  }

  // Atomically remove the start bit.  Removing the startbit marks the
  // CodeBlob as dead.  Must be done atomically because of races with other
  // threads also hacking the start bitmap.
  int startbit = ((address)blob - (address)_codecache)>>LogBytesPerSmallPage;
  int startword = startbit/64;
  int shift = startbit - (startword*64);
  uint64_t bits = _start_map[startword];
  assert0( (bits & (1LL<<shift)) );
  while( (uint64_t)Atomic::cmpxchg_ptr(bits&~(1LL<<shift),(intptr_t*)&_start_map[startword],bits) != bits ) {
    bits = _start_map[startword];
    assert0( (bits & (1LL<<shift)) );
  }

  // Protect all the pages, to catch lingering users of this CodeBlob.  This
  // is purely a debug-only notion.  In theory, the CodeBlob memory is ready
  // for instant reuse.
  int sz = blob->_size_in_bytes;
  int size_bits = sz>>LogBytesPerSmallPage;
  debug_only(memset(blob,0xcc,sz));
for(int i=0;i<size_bits;i++){
    os::uncommit_memory((char*)blob+(i<<LogBytesPerSmallPage), BytesPerSmallPage, Modules::CodeCache, true);
  }
  
  // Bump memory-in-use stats.  Not atomic with allocation, but close.
  Atomic::add_ptr(-(size_bits<<LogBytesPerSmallPage),&_used_in_bytes);

  // Now I need to remove the freed CodeBlob somewhere, so that I eventually
  // actually DO reuse the storage.  Might require multiple CAS's if the
  // CodeBlob spanned a free_map word boundary.
  while( true ) {
    // Mask of bits we need to find free (& CAS set) in one go.
    // ...|00FF FFFF FFFF FFFF|FFFF FAFB FCFD FEFF|FFFF FFFF ....  _free_map
    //                MASK:   |FFFF 0000 0000 0000|
    //      |<---size_bits----+--->|<---shift---->|
    //      |<---CodeBlob--------->|
    uint64_t mask = ((size_bits < 64) ? ((1LL<<size_bits)-1) : -1LL)<<shift;
    uint64_t bits = _free_map[startword];
    assert0( (bits & mask)==mask );
    while( (uint64_t)Atomic::cmpxchg_ptr(bits&~mask,(intptr_t*)&_free_map[startword],bits) != bits ) {
      bits = _free_map[startword];
      assert0( (bits & mask)==mask );
    }

    // If we didn't get it all, we need to setup for another CAS loop
    // ...|00FF FFFF FFFF FFFF|0000 FAFB FCFD FEFF|FFFF FFFF ....  _free_map
    //    |00FF FFFF FFFF FFFF|   :MASK
    //      |<---size_bits--->|
    //      |<---CodeBlob--------->|
    if( size_bits <= (64-shift) ) break;
    size_bits -= (64-shift);  // Shift over to the next _free_map word
    shift = 0;
    startword++;
  }
}


// --- find_blob -------------------------------------------------------------
static uintptr_t _cc_cache;      // 1-entry cache
CodeBlob*CodeCache::find_blob(void*p){
  // Do a little 1-entry cache lookup.  Has a 85% hit ratio during 213_javac.
  // High 32 bits is the CodeBlob*, low 32 bits is the PC.
  uintptr_t x = _cc_cache;
  uintptr_t px = (intptr_t)p&0x0FFFFFFFFULL;
  if( (x&0x0FFFFFFFFULL) == px ) 
    return (CodeBlob*)(x>>32);

  if( p < _codecache || p > (char*)_codecache+(_freebits_cnt<<LogBytesPerSmallPage) )
    return NULL;
  // a backwards scan in the _start_map
  intptr_t adr = (intptr_t)p & ~(BytesPerSmallPage-1); // round to page start
  while(true) {
    int64_t pgnum = (adr - (intptr_t)_codecache)>>LogBytesPerSmallPage;
    int64_t pgnum_word = pgnum/64;
    uint64_t bitnum = pgnum - (pgnum_word*64);
    if( (_free_map [pgnum_word] & (1LL<<bitnum)) == 0 ) return NULL;
    if( (_start_map[pgnum_word] & (1LL<<bitnum)) != 0 ) break;
    adr -= BytesPerSmallPage;  // Back up a page
  } 
  // cant back up forever, because the 1st page belongs to a blob, so
  // either will back into the 1st blob start (or some blob start) or
  // find and empty page.

  // Set the little 1-entry cache.  Requires a 64-bit atomic write.
  // High 32 bits is the CodeBlob*, low 32 bits is the PC.
  _cc_cache = ((adr<<32)|px);
  return (CodeBlob*)adr;
}

// --- oops_do ---------------------------------------------------------------
// Visit all the MethodStubBlobs and back pointers
void CodeCache::oops_do(OopClosure* f) {
FOR_ALL_CODEBLOBS(cb){
    cb->oops_do(f);
  }
}


// --- oops_do ---------------------------------------------------------------
// Visit all the MethodStubBlobs only.
void CodeCache::MSB_oops_do(OopClosure*f){
FOR_ALL_CODEBLOBS(cb){
if(cb->is_method_stub())
      cb->oops_do(f);
  }
}


// --- unlink ----------------------------------------------------------------
// Remove CodeBlobs who's owning MCO is dead.
void CodeCache::unlink(BoolObjectClosure*is_alive){
FOR_ALL_CODEBLOBS(cb){
    objectRef ref = ALWAYS_UNPOISON_OBJECTREF(cb->_owner);
    if( ref.not_null() &&      // null refs for always-live CodeBlobs
        !is_alive->do_object_b(ref.as_oop()) ) {
      assert0( cb->is_methodCode() || cb->is_vtable_stub() ); // other blob types never freed
      CodeCache::free_CodeBlob(cb);
      methodCodeOopDesc::remove_blob_from_deopt(cb);
    } else if( cb->_type == CodeBlob::methodstub ) {
      MethodStubBlob::unlink(cb->code_begins(),cb->end(),is_alive);
    }
  }
}


// --- GPGC_unlink -----------------------------------------------------------
// Remove CodeBlobs who's owning MCO is dead
void CodeCache::GPGC_unlink(){
FOR_ALL_CODEBLOBS(cb){
    objectRef ref = ALWAYS_UNPOISON_OBJECTREF(cb->_owner);
    if ( ref.not_null() ) {   // null refs for always-live CodeBlobs
      assert(ref.is_old(), "CodeCache should only have old-space owners");

      if (GPGC_ReadTrapArray::is_remap_trapped(ref)) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(ref);
      }

      assert(ref.as_oop()->is_oop(), "not oop"); // Is this safe, even if the oop is a dead object?

      if (!GPGC_Marks::is_old_marked_strong_live(ref)) {
        assert0( cb->is_methodCode() || cb->is_vtable_stub() ); // other blob types never freed
        CodeCache::free_CodeBlob(cb);
        continue;
      }
    }

    // Any CodeBlob we don't unlink, we need to make sure the owner objectRef is marked
    // through to get consistent NMT bits and remapped addresses.
    GPGC_OldCollector::mark_to_live((heapRef*)&(cb->_owner));

    // MethodStubBlob's also have embedded objectRefs that need to be either unlinked or marked through.
    if ( cb->_type == CodeBlob::methodstub ) {
      MethodStubBlob::GPGC_unlink(cb->code_begins(),cb->end());
    }
  }
}


// --- clean_inline_caches ---------------------------------------------------
// Incremental sweeping of inline-caches to 'clean'.  Cleaning prevents bad IC
// predictions from holding onto otherwise dead classes.  Rapid sweeping
// increases resolve_and_patch costs.  Sweeping costs are throttled here.
static long last_sweep_end;
void CodeCache::clean_inline_caches(){
  const long MSEC_BETWEEN_SWEEPS = 
#ifdef DEBUG
    40*1000; // Force a 40-sec interval between allowing caches to be flushed
#else
    10*1000; // Force a 10-sec interval between allowing caches to be flushed
#endif
  if( os::javaTimeMillis() - last_sweep_end < MSEC_BETWEEN_SWEEPS )
    return;

  const int MAX_ICS_FLUSHED_PER_CALL = 1000;// Throttle cost of calling this routine
FOR_ALL_CODEBLOBS(cb){
cb->clean_inline_caches();
  }

last_sweep_end=os::javaTimeMillis();
}

// --- print_xml_on
void CodeCache::print_xml_on(xmlBuffer* xb) {
  xmlElement xe(xb, "codecache");
FOR_ALL_CODEBLOBS(cb){
    cb->print_xml_on(xb,true);
  }
}

// --- verify
// Do a little verification.  Not multi-thread safe, so better not have other
// threads busy doing inserts or we'll get false positives.  
void CodeCache::verify(){
  for( int64_t i=0; i<_freebits_cnt; i++ ) {
    int64_t wordnum = i/64;
    int bitnum  = i - (wordnum*64);
    uint64_t freebits = _free_map [wordnum];
    uint64_t startbits= _start_map[wordnum];
    uint64_t freebit  = freebits  & (1LL<<bitnum);
    uint64_t startbit = startbits & (1LL<<bitnum);
    address adr = (address)_codecache + (i<<LogBytesPerSmallPage);

    assert0( !(startbit && !freebit) ); // not both start of codeblob AND free
    // If not free, should be able to read/write/exec.  Just test read.
    char x;
    if( freebit ) x = *adr; // Read if not free
    // If free, should be untouchable - but I don't really want to find out by
    // taking a fault.

    // If it's the start of a blob, do some more verify.
    if( startbit ) {
      CodeBlob *cb = ((CodeBlob*)adr);
address end=cb->end();
      // Check that all bits in the CodeBlob are not-free
while(adr<end){
        assert0( (_free_map [wordnum] & (1LL<<bitnum)) != 0 ); // all bits not-free
        // Also, no new CodeBlob starts.
        assert0( (_start_map[wordnum] & (1LL<<bitnum)) == 0 || adr == (address)cb );
adr+=BytesPerSmallPage;
        bitnum++;
        if( bitnum == 64 ) {
          wordnum++;
          bitnum = 0;
        }
      }
      // Check the next bit at the end of this blob.  Either it is free, or it
      // is the start of a new blob - but it's not yet-another-page on this blob.
      assert0( ((wordnum*64) + bitnum) >= _freebits_cnt ||   // End of world OR
               (_free_map [wordnum] & (1LL<<bitnum)) == 0 || // It is free OR 
               (_start_map[wordnum] & (1LL<<bitnum)) != 0 ); // It is a new CodeBlob
      
      cb->verify();             // Also do CodeBlob verify
    }
  }
}

// --- print_internals
void CodeCache::print_internals() {
  int blobs=0;
  int code_size=0;
  int padded_code_size=0;
  int oopmap_size=0;
  int lckmap_size=0;
  int dbgmap_size=0;
FOR_ALL_CODEBLOBS(cb){
    blobs++;
code_size+=cb->code_size();
    padded_code_size += cb->_size_in_bytes;
    if( cb->_oop_maps )
      oopmap_size += cb->_oop_maps->size();
    methodCodeOop mcoop = cb->owner().as_methodCodeOop();
    if( mcoop && mcoop->_debuginfo )
      dbgmap_size += mcoop->_debuginfo->size_in_bytes();
  }

tty->print_cr("--- CodeCache Internals");
  tty->print_cr("Blobs: %d   Total Blob Size: %d   Average Blob Size: %d",blobs,padded_code_size,padded_code_size/blobs);
  tty->print_cr("Unpadded code size: %d  Avg unpadded code size: %d",code_size,code_size/blobs);
tty->print_cr("Total OopMap Size: %d   Total LockMap Size: %d",oopmap_size,lckmap_size);
tty->print_cr("Total Debug Info Size: %d",dbgmap_size);
}

// --- print_on
void CodeCache::print_on(outputStream*st){
tty->print_cr("codecache");
FOR_ALL_CODEBLOBS(cb){
cb->print_on(st);
  }
}

void CodeCache::print(){
print_on(tty);
}

//=============================================================================
// --- GDB Support
// Provide enough info so GDB can crawl HotSpot frames, even in a core file.
hotspot_to_gdb_symbol_table_t HotspotToGdbSymbolTable;

// --- get_new_hsgdb
hotspot_to_gdb_symbol_t *CodeCache::get_new_hsgdb() {
assert_lock_strong(ThreadCritical_lock);
  // Grow table if full
  if( HotspotToGdbSymbolTable.numEntries == HotspotToGdbSymbolTable.maxEntries ) {
    HotspotToGdbSymbolTable.maxEntries += (HotspotToGdbSymbolTable.maxEntries)+1; // Grow by 50%
    HotspotToGdbSymbolTable.symbolsAddress = 
      (hotspot_to_gdb_symbol_t *)realloc(HotspotToGdbSymbolTable.symbolsAddress,
                                         HotspotToGdbSymbolTable.maxEntries*sizeof(hotspot_to_gdb_symbol_t));
  }
  HotspotToGdbSymbolTable.generationNumber++;
  return &HotspotToGdbSymbolTable.symbolsAddress[HotspotToGdbSymbolTable.numEntries++];
}
