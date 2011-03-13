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


#include "collectedHeap.hpp"
#include "copy.hpp"
#include "gcLocker.hpp"
#include "handles.hpp"
#include "interfaceSupport.hpp"
#include "jniHandles.hpp"
#include "log.hpp"
#include "mutexLocker.hpp"
#include "nmt.hpp"
#include "oop.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "sbaThreadInfo.hpp"
#include "sharedRuntime.hpp"
#include "stackBasedAllocation.hpp"
#include "thread.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

#define STACK_VA_MASK  0xffffffff
#define STACK_VA_BITS  32
#define STACK_SID      1
#define SPACE_SHIFT    61
#define FID_SHIFT      STACK_VA_BITS


//--------------------------------------------------------------
// Heap Escapes, Pass 1: Compute reachable sizes
// This closure is started on the root escaping object and it visits all
// reachable stack objects, marking them, counting them, hinting them and
// summing their sizes.
class SBAComputeEscapingSizes: public SbaClosure {
 public:
  int *const _sizes;
  int const _max_chunk;         // Max size chunk in words
  int _oldest_escaping_fid;     // Oldest frame where we saw escaping objects
int _size;//Total size of escaping objects in words
  uint _chunk_idx;              // Index of next empty chunk
  SBAComputeEscapingSizes( JavaThread *jt) : SbaClosure(jt), _max_chunk(Universe::heap()->max_tlab_size() ? Universe::heap()->max_tlab_size() : M*SBAStackSize+1), _oldest_escaping_fid(SBA_MAX_FID), _size(0), _chunk_idx(0), _sizes(NEW_RESOURCE_ARRAY(int,2*SBAStackSize+1)) { 
    bzero( (int*)_sizes, (int)(sizeof(int)*(2*SBAStackSize+1)));
  }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return; // Ignore NULL, obviously
    objectRef ref = UNPOISON_OBJECTREF(*p,p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects

    oop tmp = ref.as_oop();
    SBAPreHeader *pre = stackRef::preheader(tmp);
    int fid = pre->fid();

    assert0( !pre->is_dead() );
    if( test_set(tmp) ) return; // Already visited & marked.
    _jt->sba_area()->_heap_escape_objects++; // Stats

    // Compute oldest escaping object; this limits the stack crawl for
    // pointer-fixup.  We know objects in older frames contain no pointers to
    // younger frames and thus need no fixup.
    if( fid < _oldest_escaping_fid ) _oldest_escaping_fid = fid;

    // Compute size of moving objects
    int size_in_words = tmp->size(); // Woohoo!  The actual real work of this closure!
_size+=size_in_words;
    // Find an empty chunk using first-fit.  If the object does not fit in a
    // chunk, also check to see if the chunk is very little space left
    // (1/256th or 1/2%) and if so, quit trying to stuff more stuff into that
    // chunk by advancing the _chunk_idx.
    // Cannot leave exactly 1 word left in chunk, because that cannot be made
    // heap-parseable.  Want to allow an exactly-full chunk because for large
    // single objects we make chunks that are the exact size.
    while( _sizes[_chunk_idx] + size_in_words >= _max_chunk-1 &&
           (_max_chunk-_sizes[_chunk_idx]) < (_max_chunk>>8) ) // Chunk very full?
      _chunk_idx++;             // Quit trying to add more to it
    // Now scan partially full chunks for first-fit.  Quit scan when it fits
    // or we find a totally empty chunk (and we don't fit because the object
    // is huge).
    uint idx = _chunk_idx;
    while( _sizes[idx] + size_in_words >= _max_chunk-1 && _sizes[idx] )
idx++;//Skip to next chunk
    // Update chunk fullness
    assert0( idx < sizeof(_sizes)/sizeof(_sizes[0]) );
    _sizes[idx] += size_in_words;

    // For all escaping objects, update their hint bits
    pre->update_allocation_site( HEAP_FID );

    // Tail recursion
tmp->oop_iterate(this);
  }
};

//--------------------------------------------------------------
// Heap Escapes, Pass 2: Move heap escapees to the new space
// This closure is started on the root escaping object and it visits all
// marked reachable objects, moving them, clearing their marks and leaving
// forward pointers behind.
class SBAMoveEscapingObjectsHeap: public SbaClosure {
private:
  int const _len;               // Number of chunks
  int _chunk_idx;               // Current check we allocate in
  HeapWord **_chunks;           // Start of each chunk

public:
  int *_sizes;                  // Size of each chunk
  SBAMoveEscapingObjectsHeap( SbaClosure *old_visit_bits, int *sizes, HeapWord **chunks, int len ) : SbaClosure(old_visit_bits), _sizes(sizes), _chunks(chunks), _len(len), _chunk_idx(0) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return; // Ignore NULL, obviously
    objectRef ref = UNPOISON_OBJECTREF(*p,p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects

    oop tmp = ref.as_oop();
    assert0(tmp->is_oop());
    // All escaping objects are marked, so we clear the mark to indicate we've
    // visited them on this pass.
    if( !test_clr(tmp) ) return; // Already visited & marked.
    SBAPreHeader *pre = stackRef::preheader(tmp);
    assert0( !pre->is_dead() );

    // if the current tlab is full, cycle to next
    int size_in_words = tmp->size();

    // Match the scan done in the prior SBAComputeEscapingSizes.
    while( _sizes[_chunk_idx] == 0 ) _chunk_idx++; // We used these chunks up
int idx=_chunk_idx;
    while( size_in_words > _sizes[idx] ) idx++;
    assert0( idx < _len );

    HeapWord* newobj = _chunks[idx];
    _sizes [idx] -= size_in_words;
    _chunks[idx] += size_in_words;
    // Found a marked object, not yet visited on Pass 2.
    // Decide if it is moving or not and leave a forwarding ptr behind.
    // Copy object body
Copy::aligned_disjoint_words((HeapWord*)tmp,newobj,size_in_words);
Universe::heap()->tlab_allocation_mark_new_oop(newobj);
    // Make tmp forward to it's new location
oop newoop=oop(newobj);
    tmp->forward_to_ref( objectRef(newoop) );
    assert0( tmp->forwarded_ref().not_null() ); // Must look like it has been forwarded
    tmp->oop_iterate(this);     // Recursion over all moving objects
    // Deaden after recursion.  I need the oop body for the recursion and
    // deaden crushes the space.
    pre->deaden();         // Make a skipable hole in the SBA allocation space
#ifdef ASSERT
    julong* fields = (julong*)tmp + oopDesc::header_size();
    julong* limit = fields + size_in_words-oopDesc::header_size();
    fields++;                   // leave ary length alive
    while( fields < limit ) *fields++ = badHeapWordVal;
#endif
    // Rather expensively we need to check the stack for biased locks,
    // in case the object moving to the heap needs to be bias-locked.
    // Locked stack objects may not be bias-locked.
    if( !newoop->is_self_locked() && _jt->count_locks(tmp) ) { // must check for lock
      // oops: object is not self-biased but has active locks.
      // We want to make it appear in the heap pre-biased (and not recursively lcked);
      newoop->slow_lock(INF_ENTER,_jt);
      // Assert that this is a spec-lock.
      assert0( newoop->mark()->is_self_spec_locked() || newoop->mark()->monitor()->is_self_spec_locked() );
    }
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {Unimplemented();}  
};

//--------------------------------------------------------------
// Heap Escapes, Passes 1.5 & 2.5: Eager Escape matching PreHeaders
class SBAEagerEscape: public SbaClosure {
  SbaClosure *const _closure;
  int64_t const _pre;           // Pre-header to match
public:
int _cnt;//Number of eager escape roots
  SBAEagerEscape( SbaClosure *closure, int64_t match_pre ) : SbaClosure(closure->_jt), _closure(closure), _pre(match_pre), _cnt(0) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return; // Ignore NULL, obviously
    objectRef ref = UNPOISON_OBJECTREF(*p,p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects
    oop tmp = ref.as_oop();
    SBAPreHeader *pre = stackRef::preheader(tmp);
    if( pre->is_dead() ) return;
    if( test_set(tmp)  ) return; // Already been there, done that
    SBAPreHeader match = *pre;
    match.set_fid(0);
    if( *(int64_t*)&match == _pre ) {
_cnt++;//No need to walk objects that are escaping
      _closure->do_oop(p);      // Do the escape work
    } else                      // Non-escapers: walk to see if we can reach 
      tmp->oop_iterate(this);   //   somebody needing Eager-Escaping.
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) { }
};

//--------------------------------------------------------------
// Heap Escapes, Pass 3: Update forwarding pointers
class SBAUpdateForwardingEscape: public SbaClosure {
public:
  SBAUpdateForwardingEscape( SbaClosure *old_visit_bits ) : SbaClosure(old_visit_bits) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return; // Ignore NULL, obviously
    objectRef ref = UNPOISON_OBJECTREF(*p,p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects

    oop oldtmp = ref.as_oop();
    assert0(oldtmp->is_oop());
    SBAPreHeader *pre = stackRef::preheader(oldtmp);
    int fid = pre->fid();
    oop newtmp = oldtmp;        // Same as old, if not moving

    // Found a pointer to dead object, meaning it got moved in this escape.
if(pre->is_dead()){
      assert0( oldtmp->is_forwarded() && oldtmp->forwarded_ref().not_null() ); // Must look like it has been forwarded
      // Found a forwarded pointer, update our pointer
      objectRef newref = oldtmp->forwarded_ref();
      POISON_AND_STORE_REF(p,newref);
      // Since the object moved, we'll recurse over his moved body doing more forwarding
      newtmp = newref.as_oop();
      assert0(newtmp->is_oop());
    } else if( pre->fid() != fid ) { // Wrong fid?
      // Found an object whose fid changed but the object did not move
      assert0( pre->fid() < fid );   // Better only escape to older frames
      *p = objectRef(oldtmp);  // Reset proper FID in the ref
#ifdef FIDS_IN_REF
      assert0( pre->fid() == stackRef(*p).sbafid() );
#endif
      assert0(p->as_oop() == newtmp);
    } else {
      // not involved object
    }

    if( test_set(oldtmp) ) return; // Already been there, done that
    
    assert0(NMT::has_desired_nmt(UNPOISON_OBJECTREF(*p,p)));
newtmp->oop_iterate(this);
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
    StackBasedAllocation::forward_derived_stack_oop(base_ptr, derived_ptr);
  }
};


//--------------------------------------------------------------
// An attempt was made to escape a stack object to the heap.  The escape has
// not happened yet.  Move the escapee (and all reachable objects) to the heap
// as needed.  The caller is responsible for completing the store.
// Pass 1: Compute size of objects being moved.  Done before allocation so we
// have only 1 GC point.  Then do a single heap allocation for all.
// Pass 2: Move the objects, leaving forwarding pointers behind.
// Pass 3: Update all via the forwarding pointers.
objectRef StackBasedAllocation::do_heap_escape(JavaThread* thread, stackRef escapee) {
  SBAArea *sba = thread->sba_area();
  sba->_heap_escape_events++;

  // ------
  // Pass 1: Compute total size of objects being moved, by visiting all
  // reachable objects from this object (those FIDs are equal or smaller than
  // this object's FID).  Scan until we see heap or older stack objects.  With
  // this size in hand, we can do a single allocation (which may GC and move
  // the original escape address).  Also adjust hints.
  int old_heap_escape_objects = sba->_heap_escape_objects;
  SBAComputeEscapingSizes CES_closure(thread);
  CES_closure.do_oop(&escapee);

  // Check for needing to eagerly escape: if we made a skillion stack objects
  // and then discover we are going to escape them all, one by one, we are
  // much better off to just eagerly escape them all.
  SBAPreHeader *pre = escapee.preheader();
  SBAPreHeader match_pre = *pre;
  int eager_escape_idx = pre->get_escape_idx(true);
  if( SBAPreHeader::_decayed_escapes[eager_escape_idx] > 4+(SBAEscapeThreshold<<1) ) {
    // ---
    // Pass 1.5: Eagerly escape all objects with the same pre-header as the escapee.
    // Requires a full live-set walk.
    BufferedLoggerMark m( NOTAG, 0, PrintGC );
    m.out("Repeated escape of ");
    pre->print(m);
    match_pre.set_fid(0);
    SBAEagerEscape EE_closure( &CES_closure, *(int64_t*)&match_pre );
    thread->oops_do_stackok( &EE_closure, SBA_ROOT_FID );
    if( EE_closure._cnt <= 1 )  // Too few remaining to bother?
      eager_escape_idx = -1;    // Then do not run pass 2.5 of Eager Escape
  } else {
    eager_escape_idx = -1;      // No eager escape
  }

  size_t esc_bytes = CES_closure._size << LogBytesPerWord;
  sba->_heap_escape_bytes += esc_bytes;

  // Quick optimization: did we escape all objects from the escapee to
  // stack-top?  If so, we can roll-back the stack-top to the escapee once the
  // escape-work is done.
address scan=(address)pre;
address end=thread->sba_top_adr();
  if( !sba->_gc_moved_klasses_and_moops ) { // Only valid if we can parse the heap
while(scan<end){
      scan += sizeof(SBAPreHeader);
oop o=(oop)scan;
      if( !CES_closure.test(o) ) break; // Not visited, must be live
      scan += (o->size()<<LogBytesPerWord);
    }
  }
  bool escape_top = scan == end;
  
  if( VerboseSBA && CES_closure._sizes[1] > 0 )
tty->print_cr("need multiple tlabs");
  
  // ------
  // All allocations, which may cause (multiple) GCs and move values.  It may
  // move the escapee, or the escapee could be the last reference to a live
  // object and need his fields walked during the GC to keep him alive.
Handle h(thread,escapee);
  
#ifdef ASSERT
  // We do allocation here and a GC is possible.  Force it.
  InterfaceSupport::check_gc_alot();
  // Escape-to-heap requires heap allocation which may GC and may thus get
  // confused about how to handle a pending exception (I suppose whenever it
  // wants to throw an OOM instead - but then I cannot complete the escape and
  // need to OOM instead of escape!)
  assert0( !thread->has_pending_exception() ); 
#endif // !ASSERT
  // Pre-allocate enough space to move all objects without further heap GCs.
  // This may require many TLABs; the last one is short-sized.  The common
  // case is that we only use the last short-sized chunk.
  int const n_chunks = sizeof(CES_closure._sizes)/sizeof(CES_closure._sizes[0]);
  Handle *handles[n_chunks];
  
  uint old_gc_count = Universe::heap()->total_collections();
  for( int i=0; CES_closure._sizes[i] > 0; i++ ) {
    int alloc_size_in_words = CES_closure._sizes[i];
    HeapWord *C = CollectedHeap::allocate_chunk_from_tlab(thread, alloc_size_in_words, CollectedHeap::DontZeroMemory);
    if( !C ) {                  // Large chunks fail and come here
      bool gc_overhead_limit_was_exceeded;
      C = Universe::heap()->mem_allocate(alloc_size_in_words, false, false, &gc_overhead_limit_was_exceeded );
    }
NEEDS_CLEANUP;//this is the wrong way to handle the out of memory error
MemRegion mr((HeapWord*)C,alloc_size_in_words);
    mr.fill();                // Make chunk heap-parseable in case of a GC
    StackBasedAllocation::collect_heap_allocation_statistics( alloc_size_in_words<<LogHeapWordSize );
    handles[i] = new Handle(thread,(oop)C);
    if( VerboseSBA && old_gc_count != Universe::heap()->total_collections() )
tty->print_cr("GC Collection count increased during escape processing");
    //if( VerboseSBA )
    //  tty->print("Allocated heap from 0x%llx to 0x%llx, ",C,C+(alloc_size_in_words<<LogBytesPerWord));
  }
  // Reload after a potential GC
  escapee = h.as_ref();         // Reload after GC
  HeapWord *chunks[n_chunks];
  for( int i=0; CES_closure._sizes[i] > 0; i++ ) {
    chunks[i] = (HeapWord*)(*handles[i])();
    delete handles[i];
  }
  
  // No more GCs allowed while we move objects and update pointers
  No_GC_Verifier nogc;          // Do not expect a GC anywhere after here
  
  // ------
  // Pass 2: Move escaping objects, by visiting all marked objects and copying
  // them to the new space.  Leave forwarding pointers behind.  Clears marks.
  SBAMoveEscapingObjectsHeap MEO_closure( &CES_closure, CES_closure._sizes, chunks, n_chunks );
  MEO_closure.do_oop(&escapee);
  if( eager_escape_idx != -1 ) { // Doing Eager Escapes?
    // ---
    // Pass 2.5: Eagerly escape all objects with the same pre-header as the escapee.
    // Requires a full live-set walk.
    BufferedLoggerMark m( NOTAG, 0, VerboseSBA );
    m.out("=== EAGER_ESCAPE pass 2.5 ");
    SBAEagerEscape EE_closure( &MEO_closure, *(int64_t*)&match_pre );
    thread->oops_do_stackok( &EE_closure, SBA_ROOT_FID );
    m.out(" -- Escaped %d", EE_closure._cnt);
  }

#ifdef ASSERT
for(int i=0;i<n_chunks;i++)
    assert0( MEO_closure._sizes[i] == 0 );
#endif
  
  // ------
  // Pass 3: Update via forwarding pointers all live objects in younger frames
  // (older frames have no pointers to younger frames).  Part of the update is
  // to make the pointers change not just location (the object moved) but also
  // the space id bits (object is now in heap not stack).
  SBAUpdateForwardingEscape UFE_closure( &CES_closure);
  thread->oops_do_stackok( &UFE_closure, CES_closure._oldest_escaping_fid );
  
  // Quicky optimization: if I just escaped the most-recent object, I can
  // roll-back 'top'.
  if( escape_top ) {
    if( VerboseSBA ) tty->print("...rollback top to %p (%ld bytes) ",pre,thread->sba_top_adr()-(address)pre);
    stackRef top = stackRef( (oop)pre, 0, 0, sba->capped_fid() );
    thread->_sba_top = top;
    uint32_t off = (address)pre - sba->_start;
    for( int fid = thread->curr_sbafid()-1; fid >= 0 && sba->_fid_top[fid] > off; fid-- )
      sba->_fid_top[fid] = off;
    HeapWord *clzend = (HeapWord*)round_down((uintptr_t)pre+sizeof(SBAPreHeader)+BytesPerCacheLine-1,BytesPerCacheLine);
    Copy::fill_to_words((HeapWord*)pre, (clzend-(HeapWord*)pre)+TLABZeroRegion/sizeof(HeapWord));
  }
  // Final step: make the new object coherent to other threads before publication.
  Atomic::membar();
  
  if( VerboseSBA && 
      ((sba->_heap_escape_objects-old_heap_escape_objects) > 1 || esc_bytes > 2*K ) )
    tty->print("...escape %d objects (%zd bytes)", sba->_heap_escape_objects - old_heap_escape_objects, esc_bytes);

  return h.as_ref();            // Return the moved/escaped address
}


//--------------------------------------------------------------
// Stack Escapes, Pass 2: Update interior pointers to new FID
class SBAUpdateInteriorFIDs:public OopClosure{
public:
  int const _escape_to_fid;     // The FID of the escape-to frame
  oop _last; // Last address (inclusive) that is moving into the escape-to frame
  SBAUpdateInteriorFIDs( int escape_to_fid, oop last ) : _escape_to_fid(escape_to_fid), _last(last) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return; // Ignore NULL, obviously
objectRef ref=ALWAYS_UNPOISON_OBJECTREF(*p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects
    if( ref.as_oop() > _last ) 
      _last = ref.as_oop();
#ifdef FIDS_IN_REF
    // The FID needs to be updated in each pointer
    stackRef sref = (stackRef)ref;
    int fid = sref.sbafid();
    if( fid <= _escape_to_fid ) return;
    sref.set_sbafid(_escape_to_fid);
    *p = ALWAYS_POISON_OBJECTREF(sref);
#endif
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) { ShouldNotReachHere(); }
};

//--------------------------------------------------------------
// Stack Escapes, Pass 3: Update any A-stack ptrs.
class SBAUpdateFIDsInStack: public SbaClosure {
public:
  SBAUpdateFIDsInStack( JavaThread *jt ) : SbaClosure(jt) { }
  virtual void do_oop(objectRef* p) {
#ifdef FIDS_IN_REF
    if(  p->is_null()  ) return; // Ignore NULL, obviously
    // Must handle direct stack values (not poisoned)
    // and the thread-local handles (which are poisoned).
    objectRef ref = PERMISSIVE_UNPOISON(*p,p);
    if( !ref.is_stack() ) return; // No escaping of heap-based objects
    stackRef *s = (stackRef*)p;
    stackRef sref = (stackRef)ref;
    SBAPreHeader *pre = sref.preheader();
    int pre_fid = pre->fid();
    int self_fid = sref.sbafid();
    if( self_fid != pre_fid ) {
      assert0( self_fid > pre_fid ); // Only lowering FID values
      sref.set_sbafid(pre_fid);
      *p = SHOULD_BE_POISONED(s) ? (objectRef)ALWAYS_POISON_OBJECTREF(sref) : (objectRef)sref;
    }
#endif
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) { 
    StackBasedAllocation::forward_derived_stack_oop(base_ptr, derived_ptr);
  }
};

//--------------------------------------------------------------
// An attempt was made to escape a stack object to an older frame.  The escape
// has not happened yet.  Leave the escapee in place, but renumber its FID
// (and all reachable objects) to the escape-to FID as needed.  The caller is
// responsible for completing the store.
// Pass 1: Convert all objects from the escape-to frame down to the last
// address escaping into objects in the escape-to frame.  Basically we inflate
// the escape-to frame to include all the escaping addresses.
// Pass 2: Fixup the A-stack addresses to match
objectRef StackBasedAllocation::do_stack_escape(JavaThread* thread, stackRef escapee, int escape_to_fid) {
  SBAArea *sba = thread->sba_area();
  sba->_stack_escape_events++;
  int escapee_fid= escapee.preheader()->fid();
  int old_objs = sba->_stack_escape_objects;
  int old_bytes= sba->_stack_escape_bytes;

  // After making the stack heap parseable we cannot allow any klasses to
  // unload (which might make the stack heap unparsable), so we do not allow a
  // full-gc to know that anything in our stack heap is not reachable by
  // disallowing a GC cycle.
  No_GC_Verifier nogc;          // Do not expect a GC anywhere after here

  objectRef eref = sba->make_heap_parsable(escapee);
  if( !eref.is_stack() ) {      // Some type arrays got moved to the heap,
    if( VerifySBA ) sba->verify();
    return eref;                // effectively doing the escape processing.
  }
  escapee = eref;

  // ------
  // Pass 1: Sweep from the end of the escape-to frame to the largest escapee
  // address.  Use the preheader to skip dead objects, and use the object size
  // info to find the next object - which requires the stack heap to be
  // parsable.  Scan each object internally for pointers that need to have
  // their FIDs adjusted.  Notice this is a sweep, not a recursive scan.
  SBAUpdateInteriorFIDs UIFID_closure( escape_to_fid, escapee.as_oop() );
  SBAPreHeader *p = (SBAPreHeader *)(sba->_start+sba->_fid_top[escape_to_fid]);
  while( (oop)(p+1) <= UIFID_closure._last ) {
    oop q = (oop)(p+1);
    size_t sz = q->size()<<LogBytesPerWord;
if(!p->is_dead()){
      assert0( !p->is_forward() );
      p->set_fid(escape_to_fid); // Adjust the escape-to fid
      q->oop_iterate(&UIFID_closure);
      sba->_stack_escape_objects++;
      sba->_stack_escape_bytes  += sz;
    } 
    p = (SBAPreHeader*)((address)q + sz);
  }

  // ------
  // Pass 2: Update any A-stack ptrs
  Handle h(thread,escapee); // Handlize because he will then get FID renumbered
  SBAUpdateFIDsInStack UFIS_closure( thread );
  thread->oops_do_stackok( &UFIS_closure, escape_to_fid );

  // Update the frame offsets
  int frame_off = (address)UIFID_closure._last + wordSize*UIFID_closure._last->size() - sba->_start;
for(int i=escape_to_fid;i<escapee_fid;i++)
    sba->_fid_top[i] = frame_off;

  if( VerboseSBA && (sba->_stack_escape_objects-old_objs)>1 )
tty->print("...escape %d objects (%d bytes)",
               sba->_stack_escape_objects - old_objs, 
               sba->_stack_escape_bytes   - old_bytes );

  return h.as_ref();
}

//--------------------------------------------------------------
// An attempt was made to escape a stack object to the heap or an older frame.
// The escape has not happened yet.  Move the escapee (and all reachable objects) 
// to the heap or update FIDs.  The caller is responsible for completing the store.
objectRef StackBasedAllocation::do_escape(JavaThread* thread, stackRef escapee, int escape_to_fid, int kid, const char *reason) {
jlong start_tick=os::elapsed_counter();
  ResourceMark rm(thread);
  ResetNoHandleMark rnm; // Might be called from LEAF/QUICK ENTRY
  HandleMark   hm(thread);
  assert0(escapee.is_stack());
  SBAPreHeader *pre = escapee.preheader();

  SBAArea *sba = thread->sba_area();
  if (VerifySBA) sba->verify();

  assert0( (uint64_t)escapee.as_oop() == (((uint64_t)(thread->sba_area()->ABase()) << stackRef::va_bits) | escapee.va()) );

#ifndef PRODUCT
  if( VerboseSBA ) {
    ResourceMark rm;
sba->print();
    tty->print("Escaping 0x%lx %s ", escapee.raw_value(), escapee.as_oop()->blueprint()->internal_name() );
pre->print();
    tty->print( escape_to_fid == HEAP_FID ? " to heap by " : " to FID %d by ",escape_to_fid);
    const char *strkid;
    switch( kid ) {
    case 0: strkid=""; break;
    case 1: strkid="unresolved kid"; break;
    case 2: strkid="unlinked kid"; break;
    case 3: strkid="system kid"; break;
    default:
      klassRef k = KlassTable::getKlassByKlassId(kid);
      strkid = Klass::cast(k.as_klassOop())->internal_name();
      break;
    }
tty->print("%s into a %s",reason,strkid);
  }
#endif

  // Do the escape handling
  objectRef result = (escape_to_fid == HEAP_FID) 
    ? do_heap_escape (thread,escapee)
    : do_stack_escape(thread,escapee,escape_to_fid);

  if( VerifySBA ) sba->verify();
  assert0( !result.is_stack() || stackRef(result).preheader()->fid() == escape_to_fid );

  jlong ticks = os::elapsed_counter() - start_tick;
Atomic::add_ptr(ticks,&_escape_processing_ticks);
  if( VerboseSBA ) 
  //  tty->print_cr(" taking %2.5f msecs", 1000.0*(double)ticks/(double)os::elapsed_frequency());
    tty->cr();
  return result;
}


void StackBasedAllocation::ensure_in_heap(JavaThread* thread, Handle escapee, const char *reason) {
  if( escapee.as_ref().is_stack() )
    do_escape(thread,escapee.as_ref(),HEAP_FID, 0, reason);
}

void StackBasedAllocation::ensure_in_heap(JavaThread* thread, jobject escapee, const char *reason) {
  objectRef eref = JNIHandles::resolve_as_ref(escapee);
  if( eref.is_stack() )
    do_escape(thread,eref,HEAP_FID, 0, reason);
}


void StackBasedAllocation::forward_derived_stack_oop(objectRef*base,objectRef*derived){
  if( !base->is_stack() ) return;
  // Derived ptrs are handled before the regular do_oop on the base in
  // OopMap (I assume the other GC parts depend on that ordering).  
  intptr_t offset = derived->raw_value() - base->raw_value();
  oop oldbase = base->as_oop();
  SBAPreHeader *pre = stackRef::preheader(oldbase);
  // Found a pointer to dead object, meaning it got moved in this escape.
if(pre->is_dead()){
    assert0( oldbase->is_forwarded() && oldbase->forwarded_ref().not_null()); // Must look like it's been forwarded
    // Found a forwarded pointer, update our derived pointer
    *derived = stackRef( oldbase->forwarded_ref().raw_value() + offset );
//  } else if( pre->fid() != stackRef(*base).sbafid() ) { // Wrong fid?
//    Untested("update fid of derived ptrs");
//    assert0( !oldbase->is_forwarded() );
//    // Found an object whose fid changed but the object did not move
//    assert0( pre->fid() < stackRef(*base).sbafid() );   // Better only escape to older frames
//    ((stackRef*)derived)->set_sbafid(pre->fid());
//    assert0( pre->fid() == stackRef(*derived).sbafid() );
  } else {
    assert0( !oldbase->is_forwarded() );
    // not involved object
  }
}

void StackBasedAllocation::do_nothing_derived(objectRef* base, objectRef* derived) { }

void StackBasedAllocation::out_of_memory_error(const char* msg) {
NEEDS_CLEANUP;//should throw OutOfMemory
  fatal(msg);
}


intptr_t StackBasedAllocation::_allocation_statistics[SBA_MAX_FID+2];
intptr_t StackBasedAllocation::_escape_processing_ticks;
intptr_t StackBasedAllocation::_stack_gc_ticks;
intptr_t StackBasedAllocation::_stack_gc_findlive_ticks;
intptr_t StackBasedAllocation::_stack_gc_resize_ticks;
intptr_t StackBasedAllocation::_stack_gc_copycompact_ticks ;

// Called during VM startup
void sba_init() {
StackBasedAllocation::initialize();
SBAArea::initialize();
}

void StackBasedAllocation::initialize(){
  bzero( _allocation_statistics, sizeof(_allocation_statistics) );
}


static int idx2bytes( int i ) { return ((2+(i&1))<<(i>>1))>>1; }
static int compute_index(int x){
  // Convert X into the log of X plus half.
  // we have buckets for length 0, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, ...
  // and the bucket index is    0, 1, 2, 3, 4, 5, 6,  7,  8,  9, 10, 11, 12, ... 
  if( x < 4 ) return x;
  int lg2 = log2_intptr(x);
  int delta = x - (1<<lg2);
  int idx = (lg2<<1) + (( delta+delta < (1<<lg2) ) ? 0 : 1);
  assert( idx2bytes(idx) <= x && x < idx2bytes(idx+1), "sanity" );
  if( idx > 127 ) idx = 127;    // Cap it for huge frames
  return idx;
}

void StackBasedAllocation::collect_allocation_statistics( int fid, int size_in_bytes ) {
  if( PrintSBAStatistics ) {
    assert0( HEAP_FID == -1 );
    Atomic::add_ptr( size_in_bytes, &_allocation_statistics[fid+1] );
  }
}

bool is_valid_fid(uint64_t fid) { return (fid >= SBA_ROOT_FID) && (fid <= SBA_MAX_FID); }

// --- print_thread_statistics
static void print_thread_statistics(outputStream*st){
st->print_cr(">>>>>>>>>>>>> SBA Statistics per thread:");
  // For all threads...pretty-print
  for( JavaThread* thread = Threads::first(); thread; thread = thread->next() ) {
    if( !thread->sba_area() ||      // Ignore the never-been-SBA-d threads
        !thread->sba_area()->was_used() ) // Ignore idle/empty threads
      continue;                 
    ResourceMark rm;
    const char *name = thread->get_thread_name();
    if( !name ) name = "(NULL)";
st->print("Thread "PTR_FORMAT" ",thread);
    thread->sba_area()->print_statistics(st);
st->print_cr(" \"%s\"",name);
  }
}

void StackBasedAllocation::print_statistics(outputStream*st){

  // --- Print per-thread info
  if( UseSBA ) {
    // Take threads-lock
    MutexLockerAllowGC ml( Threads_lock.owned_by_self() ? NULL : &Threads_lock, 1 );
print_thread_statistics(st);
  }

  // --- Print per-FID info
st->print_cr(">>>>>>>>>>>>> SBA Statistics per FID:");
  // Compute total allocations across all FIDs
  // We need to make a local copy of the data, because the data is changing in
  // real-time and I need a consistent snapshot to compute totals.
  const int len = sizeof(_allocation_statistics)/sizeof(_allocation_statistics[0]);
  intptr_t data[SBA_MAX_FID+2]; // Local copy
  assert0( len == sizeof(data)/sizeof(data[0]) );
  intptr_t total_bytes = 0;
  for (int i = 0; i < len; i++) {
data[i]=_allocation_statistics[i];
    total_bytes += data[i];
  }
  intptr_t cutoff = total_bytes / 100;
  intptr_t accounted_for = 0;

  assert0( HEAP_FID == -1 );
  accounted_for += data[0]; // Heap stuff
  st->print_cr("heap:   %8ldK %5.2f%%", data[0]>>10, 100 * (double)data[0]/(double)total_bytes);

  for (int i = 1; i < len; i++) {
    if( data[i] > cutoff ) {
      accounted_for += data[i];
      st->print_cr("FID %3d %8ldK %5.2f%%", i-1, data[i]>>10,
                      100.0 * (double)data[i]/(double)total_bytes);
    }
  }
  intptr_t leftovers = total_bytes - accounted_for;

if(leftovers>cutoff){
    st->print_cr("other frames: %ldK %5.2f%%", leftovers>>10, 100 * (double)leftovers/(double)total_bytes);
  }
  st->print_cr("TOTAL:  %8ldK", total_bytes>>10);

  // --- Print some global info
  intptr_t ept_msec = (1000*_escape_processing_ticks)/os::elapsed_frequency();
  intptr_t gc_msec  = (1000*_stack_gc_ticks)/os::elapsed_frequency();
  intptr_t gc_findlive_msec  = (1000*_stack_gc_findlive_ticks   )/os::elapsed_frequency();
  intptr_t gc_resize_msec    = (1000*_stack_gc_resize_ticks     )/os::elapsed_frequency();
  intptr_t gc_copycompact_msec=(1000*_stack_gc_copycompact_ticks)/os::elapsed_frequency();
st->print_cr("Escape processing took %ldmsec",ept_msec);
st->print_cr("Total stack GC took %5ldmsec",gc_msec);
st->print_cr("  GC findlive       %5ldmsec",gc_findlive_msec);
st->print_cr("  GC resize         %5ldmsec",gc_resize_msec);
st->print_cr("  GC copycompact    %5ldmsec",gc_copycompact_msec);
}


//---------xml output
static void print_to_xml_thread_statistics( xmlBuffer *xb ) {
assert(xb,"StackBasedAllocation::print_to_xml_thread_statistics() passed NULL xmlBuffer pointer.");
xb->print_raw("<SBA_allocations_per_thread>");
  // For all threads...pretty-print
  for( JavaThread* thread = Threads::first(); thread; thread = thread->next() ) {
    if( !thread->sba_area() ||      // Ignore the never-been-SBA-d threads
        !thread->sba_area()->was_used() ) // Ignore idle/empty threads
      continue;                 
    ResourceMark rm;
    const char *name = thread->get_thread_name();
    if( !name ) name = "(NULL)";
    xmlElement xf(xb,"thread_ref_sba",xmlElement::delayed_LF);
    xb->name_value_item("id", thread->unique_id());
    xb->name_value_item("name",name);
    thread->sba_area()->print_to_xml_statistics(xb);
  }
xb->print_raw("</SBA_allocations_per_thread>");
}

void StackBasedAllocation::print_to_xml_statistics( xmlBuffer *xb ) {

  // --- Print per-thread info
  if( UseSBA ) {
    // Take threads-lock
    MutexLockerAllowGC ml( Threads_lock.owned_by_self() ? NULL : &Threads_lock, 1 );
    print_to_xml_thread_statistics(xb);
  }

  // --- Print per-FID info
xb->print_raw("<SBA_allocations_per_fid>");
  // Compute total allocations across all FIDs
  // We need to make a local copy of the data, because the data is changing in
  // real-time and I need a consistent snapshot to compute totals.
  const int len = sizeof(_allocation_statistics)/sizeof(_allocation_statistics[0]);
  intptr_t data[SBA_MAX_FID+2]; // Local copy
  assert0( len == sizeof(data)/sizeof(data[0]) );
  intptr_t total_bytes = 0;
  for (int i = 0; i < len; i++) {
data[i]=_allocation_statistics[i];
    total_bytes += data[i];
  }
  intptr_t cutoff = total_bytes / 100;
  intptr_t accounted_for = 0;

  assert0( HEAP_FID == -1 );
  accounted_for += data[0]; // Heap stuff

  {
    xb->name_value_item("total",total_bytes);
    xmlElement xf(xb,"FID_item",xmlElement::delayed_LF);
    xb->name_value_item("name","heap");
    xb->name_value_item("alloc",data[0]);
  }

  for (int i = 1; i < len; i++) {
    if( data[i] > cutoff ) {
      accounted_for += data[i];
      xmlElement xf(xb,"FID_item",xmlElement::delayed_LF);
      xb->name_value_item("name",i-1);
      xb->name_value_item("alloc",data[i]);
    }
  }
  intptr_t leftovers = total_bytes - accounted_for;

if(leftovers>cutoff){
    xmlElement xf(xb,"FID_item",xmlElement::delayed_LF);
    xb->name_value_item("name","other");
    xb->name_value_item("alloc",leftovers);
  }

  {
    xmlElement xf(xb,"FID_item",xmlElement::delayed_LF);
    xb->name_value_item("name","TOTAL");
    xb->name_value_item("alloc",total_bytes);
  }

xb->print_raw("</SBA_allocations_per_fid>");

  // --- Print some global info
  intptr_t ept_msec = (1000*_escape_processing_ticks)/os::elapsed_frequency();
  intptr_t gc_msec  = (1000*_stack_gc_ticks)/os::elapsed_frequency();
  intptr_t gc_findlive_msec  = (1000*_stack_gc_findlive_ticks   )/os::elapsed_frequency();
  intptr_t gc_resize_msec    = (1000*_stack_gc_resize_ticks     )/os::elapsed_frequency();
  intptr_t gc_copycompact_msec=(1000*_stack_gc_copycompact_ticks)/os::elapsed_frequency();

  xb->name_value_item("escape_msec",ept_msec);
  xb->name_value_item("gc_msec",gc_msec);
  xb->name_value_item("gc_findlive_msec",gc_findlive_msec);
  xb->name_value_item("gc_resize_msec",gc_resize_msec);
  xb->name_value_item("gc_copycompact_msec" ,gc_copycompact_msec );
}


