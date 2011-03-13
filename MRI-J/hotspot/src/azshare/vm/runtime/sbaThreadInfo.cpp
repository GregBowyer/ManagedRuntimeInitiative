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


#include "arrayOop.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "copy.hpp"
#include "handles.hpp"
#include "log.hpp"
#include "modules.hpp"
#include "os.hpp"
#include "sbaThreadInfo.hpp"
#include "stubCodeGenerator.hpp"
#include "thread.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"

#include "oop.inline2.hpp"

size_t    SBAArea::_bytes_per_sba; // Size of each SBA area in bytes
int       SBAArea::_freebits_cnt;  // Count of BITS in free_map
uint64_t *SBAArea::_free_map;      // 1-bit-per-sba free_map

// --- initialize
void SBAArea::initialize(){
  // Amount of virtual address space reserved for SBA
  size_t sba_va_bytes = __SBA_AREA_END_ADDR__ - __SBA_AREA_START_ADDR__;
  // Space needed per SBA area - Copy-collector: need twice as much area as size
  SBAArea::_freebits_cnt = sba_va_bytes / (SBAStackSize*M*2);
  SBAArea::_bytes_per_sba = sba_va_bytes / SBAArea::_freebits_cnt;
  int freewords = round_to(SBAArea::_freebits_cnt,64) / 64; // Round up to CAS'able chunks
  SBAArea::_free_map = (uint64_t*)os::malloc(freewords*8); // CAS'able chunks
  memset( SBAArea::_free_map,0,freewords*8);               // bitmap of free pages
  char *base = os::reserve_memory(sba_va_bytes, (char*)__SBA_AREA_START_ADDR__, true, false, MultiMapMetaData);
  assert( base == (char*)__SBA_AREA_START_ADDR__, "Reserving SBA memory failed" );
  if( MultiMapMetaData ) {
    assert( objectRef::stack_space_id == 1, "expect singular SBA meta-data bit at least significant bit of space id" );
    if( !os::alias_memory_reserve((char*)__SBA_AREA_START_ADDR__, objectRef::space_shift, 1, sba_va_bytes) ) {
Unimplemented();//alias failed
    }
  }
}

address SBAArea::get_reserved() {
  // Copying-collector: we copy From-space into To-space and hence need twice
  // max-stack-size.
  uint64_t twice_max = SBAStackSize*M*2; // Copy-collector: need twice as much area

  // The below code is based on the codeCache lock free blob allocation code.
  // As single bits are set the problems with bits crossing CAS'able chunks
  // doesn't apply.

  int freewords = _freebits_cnt / 64; // Round up to CAS'able chunks

  // Search word-by-word for a single word with a free bit.
char*heap=NULL;
for(int i=0;i<freewords;i++){
    for( int shift=0; shift < 64; shift++ ) {
      uint64_t bits = _free_map[i];
      if( (bits & (1<<shift))==0 &&
          (uint64_t)Atomic::cmpxchg_ptr(bits|(1<<shift),(intptr_t*)&_free_map[i],bits) == bits ) {
        int64_t sbanum = (i*64)+shift;
        assert0( 0 <= sbanum && sbanum < _freebits_cnt );
        char *heap = (char*)__SBA_AREA_START_ADDR__ + (sbanum*_bytes_per_sba);
        if( ((uintptr_t)heap >> stackRef::va_bits) != 0 || // Does not fit in the VA bits?
            (((uintptr_t)heap+0)>>32) != (((uintptr_t)heap+twice_max-1)>>32) ) {
          // Area spans a 4G boundary?  Then one ABase setting will not cover the
          // whole space. The current memory map shouldn't allow this.
          Unimplemented();
        }
return(address)heap;
      }
    }
  }
  // Out of Memory?  Or just bad fragmentation?
  Unimplemented();
  return NULL;
}


SBAArea::SBAArea( JavaThread *jt ) : _jt(jt), _reserved_heap(get_reserved()) {
  assert0( jt == Thread::current() );
  _committed_bytes = 0;         // No bytes committed yet
  _fid_no_cap = 0;              // FID at creation time

  _ticks_at_last_gc = os::elapsed_counter(); // "as if" we just did a stack GC

  _stack_collections = 0;
  _moved_objs = 0;
  _moved_bytes = _moved_heap_bytes = 0;
  _gc_moved_klasses_and_moops = false;
  _last_verify = 0;
  _heap_escape_events  = _stack_escape_events  = 0;
  _heap_escape_objects = _stack_escape_objects = 0;
  _heap_escape_bytes   = _stack_escape_bytes   = 0;


  // Arrange the initial allocation space: the first page of the heap
  setup_alloc_space(_reserved_heap,InitialStackSize);
}

#define VERIFY_FENCE 0xbaadbaad

// Setup space for thread-local allocation here.  

// (1) Commit the space from start to start+size.  If we have already
// committed past the end of start+size, and start > _reserved_heap then we
// assume we are shrinking the SBA heap, and we uncommit the extra space.
// (2) Slap down the verify-fence
// (3) Setup jt->sba_top=stackRef(start)
// (4) Setup jt->sba_max=stackRef(start+size);
void SBAArea::setup_alloc_space( address start, intptr_t size) {
  // Commit the space from start to start+size.  If we have already committed
  // past the end of start+size, and start > _reserved_heap then we assume we
  // are shrinking the SBA heap and we uncommit the extra space.  The
  // fast-path here is that the needed space has already been committed and no
  // new os::commit_memory call is needed.
  address com_end = _reserved_heap+_committed_bytes;
  address new_end = start+size;
  assert0( new_end <= _reserved_heap + SBAStackSize*M*2 );
  if( start > _reserved_heap && new_end < com_end && com_end-new_end >= (uint)M ) {
    os::uncommit_memory( (char*)new_end, com_end-new_end, Modules::SBA );
    _committed_bytes = new_end-_reserved_heap;
  } else if( new_end > com_end ) {
    bool result = os::commit_memory((char*)com_end, new_end-com_end, Modules::SBA, false/*do not bother to zero*/);
    if( !result )
      vm_exit_out_of_memory(new_end-com_end,"Unable to extend the SBA stack");
    _committed_bytes = new_end-_reserved_heap;
    if( MultiMapMetaData ) {
      assert( objectRef::stack_space_id == 1, "expect singular SBA meta-data bit at least significant bit of space id" );
      if( !os::alias_memory_commit((char*)com_end, objectRef::space_shift, 1, new_end-com_end) ) {
Unimplemented();//alias failed
      }
    }
  }
  _start = start;
  if( VerifySBA ) memset( start, 0x99, size-K );

  // Lay down some initial CLZs
  // TODO: clear_tlab_for_allocation returns the (potentially) new value for start, as incremental object zeroing could
  // have safepoints where the location of the block being zeroed moves.
  Unimplemented();
  ThreadLocalAllocBuffer::clear_tlab_for_allocation( (HeapWord*)start, (HeapWord*)new_end, 1, CollectedHeap::ZeroMemory );

  // Start of area, as a StackRef
  int fid = _jt->curr_sbafid(); 
  stackRef top = stackRef( (oop)start, 0, 0, fid );
  _jt->_sba_top = top;

  // End of area, as a HeapWord
  long* end = (long*)(start+size);
  // Find last usable word, before the CLZ-overrun padding area
  long *last_usable = &end[-(TLABPrefetchSize/sizeof(HeapWord)+HeapWordsPerCacheLine)];
  // Slap down the verify fence as the last word and zero the CLZ overflow (for asserts)
  bzero( last_usable, TLABPrefetchSize+BytesPerCacheLine );
  end[-1] = VERIFY_FENCE;
  // End of area, as a StackRef
  stackRef max = stackRef( (oop)last_usable, 0, 0, fid );
  _jt->_sba_max = max;
}


// Nuke (OS UNmap it) this SBAArea, including the 'this' pointer
SBAArea::~SBAArea() {
  os::uncommit_memory( (char*)_reserved_heap, _committed_bytes, Modules::SBA );
  // Atomically remove the free map bit.  Removing the bit marks the
  // SBAArea as dead.  Must be done atomically because of races with other
  // threads.
  int freebit = ((address)_reserved_heap - (address)__SBA_AREA_START_ADDR__)/_bytes_per_sba;
  int freeword = freebit/64;
  int shift = freebit - (freeword*64);
  uint64_t bits = _free_map[freeword];
  assert0( (bits & (1LL<<shift)) != 0 );
  while( (uint64_t)Atomic::cmpxchg_ptr(bits&~(1LL<<shift),(intptr_t*)&_free_map[freeword],bits) != bits ) {
    assert0( (_free_map[freeword] & (1LL<<shift)) == 0 ); // racy sanity check bit was cleared, may give false failure
  }
}


// Allocate in the stack.  Called via JNI Clone & NewInstance, or
// slow-path interpreter new_xxx or slow-path compiled milli-code or
// JVM_FillInStackTrace and other VM allocation sites.
stackRef SBAArea::allocate_impl(klassRef klass, size_t size_in_words, int length, intptr_t sba_hint) {
#ifdef ASSERT
  { uint64_t *a = (uint64_t*)(_jt->_sba_top.as_oop()); // Includes FID and space-id bits
    assert0(a[0] == 0);           // Should be pre-zeroed
    // Verify the CLZ area before allocation
    int extra_bytes = (((intptr_t)a + (BytesPerCacheLine-1)) & -BytesPerCacheLine) - (intptr_t)a;
    int clz_word_count = (TLABZeroRegion + extra_bytes) / sizeof(HeapWord);
for(int i=0;i<clz_word_count;i++)
      assert0( a[i] == 0 );
    assert0( !Klass::cast(klass.as_klassOop())->has_finalizer() );
  }
#endif
  // sba_hint can be false (never stack allocate, tested in the hpp file),
  // true (always stack allocate), or an RPC.
  if( sba_hint != true && 
      !SBAPreHeader::should_stack_allocate((address)sba_hint) ) {
Untested("Caller is VM code with passed PC hint and should not stack allocate");
    return nullRef;
  }

  long size_in_bytes = size_in_words << LogBytesPerWord;
  size_in_bytes += sizeof(SBAPreHeader); // Add space for the SBA Preheader  

  // If there is a limit to the max convenient size of any object.  If we need
  // to escape an object, it will need to fit entirely in a max-sized TLAB.
  // Thus if (max_size>0) then check against that max.
int max_size_in_words=Universe::heap()->max_tlab_size();
  if( max_size_in_words > 0 && size_in_words > (uint)max_size_in_words )
    return nullRef;

  // Check for needing a GC
  stackRef ref = _jt->_sba_top; // Includes FID and space-id bits
  stackRef newtop = stackRef(ref.raw_value()+size_in_bytes);
  if( newtop.raw_value() > _jt->_sba_max.raw_value() ) { // Need a GC!
    collect(size_in_bytes);
    ref = _jt->_sba_top;         // Reload after moved by GC
    newtop = stackRef(ref.raw_value()+size_in_bytes);
    if( newtop.raw_value() > _jt->_sba_max.raw_value() ) 
      return nullRef;           // It's Just Too Big
  }

  // Allocate space (bump-pointer allocation)
  _jt->_sba_top = newtop;

  // Declare the pre-header and object
  address x = ref.as_address(_jt);
  SBAPreHeader *pre = (SBAPreHeader*)x;
  oop obj = (oopDesc*)(x+sizeof(SBAPreHeader));

  // Fill in the pre-header; requires finding the allocation site
  int fid = _jt->curr_sbafid();
  if( sba_hint == true ) {
frame fr=_jt->last_frame();
    while( true ) {
      if( fr.is_interpreted_frame() ) { // Called from interpreter?
        methodOop moop = fr.interpreter_frame_method();
        int bci = fr.interpreter_frame_bci();
        pre->init( fid, moop, bci ); // Pre-header indicates Java bytecode
        break;
      }
      // Called from compiled code
CodeBlob*cb=CodeCache::find_blob(fr.pc());
      // Passing a NULL reciever to a v-call throws a new
      // stack-allocated NPE in the vtable stub.
      assert0( cb->is_methodCode() || cb->is_runtime_stub() );
      if( !cb->is_native_method() ) { // No native wrappers
        pre->init( fid, fr.pc() );  // Pre-header indicates millicode allocation site
        break;
      }
      fr = fr.sender();         // Hint in the native caller instead
    }
  } else {                        // Pre-built hint
    assert0( sba_hint != false ); // Always hint either RPC or 'default'
    pre->init( fid, (address)sba_hint ); // Pre-header indicates allocation site
  }

  // Initialize the object proper
  // see CollectedHeap::post_allocation_setup_common()
  int kid = Klass::cast((klassOopDesc*)klass.as_oop())->klassId();
  obj->set_mark(markWord::prototype_with_kid(kid));
  // Note that I hard-zero the next few cache lines instead of CLZ'ing them.
  // It's hard to get the CLZ sequence right, so we'll let the fast ASM
  // routine do that.  This is supposed to be the infrequent slow-path.
  HeapWord *fields = (HeapWord*)((address)obj + oopDesc::header_size_in_bytes());
  HeapWord *end    = (HeapWord*)(_jt->sba_max_adr());
  int field_len = size_in_bytes - oopDesc::header_size_in_bytes();
  // TODO: clear_tlab_for_allocation returns the (potentially) new value for start, as incremental object zeroing could
  // have safepoints where the location of the block being zeroed moves.
  Unimplemented();
  ThreadLocalAllocBuffer::clear_tlab_for_allocation(fields, end, field_len>>LogBytesPerWord, CollectedHeap::ZeroMemory);
if(obj->is_array()){
    ((arrayOop)obj)->set_length(length);
if(obj->is_objArray()){
      klassOop element_klass = objArrayKlass::cast((klassOop)klass.as_oop())->element_klass();
      int ekid;
      if( element_klass == NULL ) { // universe genesis not complete, array is a system_objArray
        assert( ((klassOop)klass.as_oop())->klass_part()->klassId() == systemObjArrayKlass_kid,
"can only create system_objArray's during genesis");
        ekid = 0; // system object arrays don't have an element klass
      } else {
        ekid = objArrayKlass::cast((klassOop)klass.as_oop())->element_klass()->klass_part()->klassId();
      }
      ((objArrayOop)obj)->set_ekid(ekid);
    }
  } else {
    assert0( obj->is_oop() );
  }

  // Convert the oop to a stackRef
  uint64_t nmt = objectRef::discover_nmt(objectRef::stack_space_id, 0);
  stackRef sref = stackRef( obj, (uint64_t) kid, nmt, fid );

  // Collect some info
  StackBasedAllocation::collect_allocation_statistics(fid, size_in_bytes );

#ifdef ASSERT
  { uint64_t *a = (uint64_t*)(_jt->_sba_top.as_oop());
    assert0(a[0] == 0);           // Should be pre-zeroed
    assert0(TLABPrefetchSize==0 || a[(TLABZeroRegion/sizeof(uint64_t))-1] == 0); // Should be pre-zeroed
  }
#endif
  return sref;
}

bool SBAArea::was_used() const {
  return !_jt->sba_used() || _stack_collections > 0;
}

void SBAArea::print_to_xml_statistics(xmlBuffer *xb) const {
assert(xb,"SBAArea::print_statistics() called with NULL xmlBuffer pointer.");
  intptr_t in_use = _jt->sba_used();
  intptr_t max    = _jt->sba_max_adr() - _start;
  xb->name_value_item("in_use",in_use);
  xb->name_value_item("max",    max  );
  xb->name_value_item("fid",    _jt->curr_sbafid() );
  xb->name_value_item( "heap_escape_events",  _heap_escape_events );
  xb->name_value_item("stack_escape_events", _stack_escape_events );
  xb->name_value_item( "heap_escape_objects", _heap_escape_objects);
  xb->name_value_item("stack_escape_objects",_stack_escape_objects);
  xb->name_value_item( "heap_escape_bytes" ,  _heap_escape_bytes  );
  xb->name_value_item("stack_escape_bytes" , _stack_escape_bytes  );
  xb->name_value_item("gc_num"        , _stack_collections  );
  xb->name_value_item("gc_moved_objs" , _moved_objs );
  xb->name_value_item("gc_moved_bytes", _moved_bytes );
  xb->name_value_item("gc_heapified"  , _moved_heap_bytes  );
}

void SBAArea::print_statistics(outputStream*st)const{
  if (!st) st=tty;
  intptr_t in_use = _jt->sba_used();
  intptr_t max    = _jt->sba_max_adr() - _start;
tty->print("stack is %ldK (%ld%% full), fid=%d, Escapes: heap=%d (%d objs, %dK), stack=%d (%d objs, %dK), ",
             max>>10, in_use*100/max, _jt->curr_sbafid(), 
             _heap_escape_events,  _heap_escape_objects,  _heap_escape_bytes>>10, 
             _stack_escape_events, _stack_escape_objects, _stack_escape_bytes>>10);
tty->print("GCs=%d Survivors: %d objs, %ldK (%ldK heapified)",
             _stack_collections, _moved_objs, _moved_bytes>>10, _moved_heap_bytes>>10);
}

// Return TRUE if has no stackRefs.  May return FALSE when in fact it has no stackRefs.
static bool has_no_stack_refs( oop tmp, int sz ) {
  objectRef *p = ((objectRef*)tmp) + oopDesc::header_size();
  sz -= oopDesc::header_size();
  for( int i=0; i<sz; i++ ) {
    objectRef ref = (RefPoisoning && p->is_poisoned()) ? ALWAYS_UNPOISON_OBJECTREF(*p) : *p;
    if( ref.is_stack() ) return false;
    p++;
  }
  return true;
}


// Push a new SBA frame
void SBAArea::push_frame() {
  // Verify sane before pushing
if(VerifySBA)verify();
  _fid_top[_fid_no_cap] = _jt->sba_used();
  _fid_no_cap++;
  if( _fid_no_cap > SBA_MAX_FID ) { 
    Untested("");               // No push at max frames
    return;
  }
  _jt->_curr_fid = _fid_no_cap;
  _jt->_sba_top.set_sbafid(_fid_no_cap);
  _jt->_sba_max.set_sbafid(_fid_no_cap);
}

void SBAArea::pop_frame( ) {
  // Verify sane before popping
if(VerifySBA)verify();
  int fid_no_cap = _fid_no_cap-1;
  if( fid_no_cap >= SBA_MAX_FID ) { _fid_no_cap = fid_no_cap; Untested(""); return; } // No pop at max frames

  // The 'cheap pop' optimization relies on only 1 object escaping.  
  // The 'check_escape' call can cause a stack GC.
  check_escape(fid_no_cap,(objectRef*)((address)_jt+in_bytes(JavaThread::pending_exception_offset())) );
  // After any stack GCs, find the old prior-frame 'top' after popping
  _fid_no_cap = fid_no_cap;     // Store back the decremented frame
  _jt->_curr_fid = _fid_no_cap;
  address oldtop = _start + _fid_top[fid_no_cap];

  stackRef top = stackRef( (oop)oldtop, 0, 0, fid_no_cap );
  _jt->_sba_top = top;           // Restore old
  _jt->_sba_max.set_sbafid(fid_no_cap);

  // Rezero the CLZ pipe area without using CLZs since it is all hot in cache
  HeapWord *clzend = (HeapWord*)round_down((intptr_t)oldtop+sizeof(SBAPreHeader)+BytesPerCacheLine-1,BytesPerCacheLine);
  Copy::fill_to_words((HeapWord*)oldtop, (clzend-(HeapWord*)oldtop)+TLABZeroRegion);

  // Verify sane AFTER popping: in particular, no missed escapes
if(VerifySBA)verify();
}

// See if the value is escaping, and fixup if needed.  The new_fid value has
// just been decremented by one.  Used to check various VM variables.
void SBAArea::check_escape( int new_fid, objectRef *p ) {
  assert0( new_fid != HEAP_FID ); // VM Heap escapes checked elsewhere
  if( p->is_null() ) return;
  if( !p->is_stack() ) return;
  stackRef o = *(stackRef*)p;
  oop tmp = o.as_oop();
  int fid = o.preheader()->fid();
  assert0( fid <= new_fid+1 );  // Better not have already escaped!
  if( fid <= new_fid ) return;  // No escape today!

  // Something is doing a stack escape.  Since we are frame-popping no
  // instances exist on the thread stack - we hold the only instance in the
  // passed-in address.  If the object has no internal stack refs, then its
  // escape does not drag along anything else.  In this case we can do a
  // 'cheap escape': we can simply move/copy the object to the start of the
  // prior frame and adjust its FID as-if it was allocated in the prior frame
  // directly.  No stack-crawl to find/adjust ptrs.
  int sz = tmp->size();
  if( has_no_stack_refs( tmp, sz ) ) {
    // The 'cheap escape' is on!
    address oldtop = _start + _fid_top[new_fid]; // Prior frame top
    int bytes = sz*sizeof(SBAPreHeader)+sizeof(SBAPreHeader);
    // Notice I use memmove in case the old and new locations overlap
    memmove(oldtop,((address)tmp)-sizeof(SBAPreHeader),bytes);
    ((stackRef*)p)->set_va((HeapWord*)(oldtop+sizeof(SBAPreHeader)));
    _fid_top[new_fid] += bytes;
    ((SBAPreHeader*)oldtop)->set_fid(new_fid);
    // Note that '*p' is not poisoned so I do not need to do a
    // poisoning-update of the new_fid.
    ((stackRef*)p)->set_sbafid(new_fid);
    return;
  }
  StackBasedAllocation::do_escape( _jt, o, new_fid, 0, "pop frame" );
}

// -------------------------------------
// GC Pass 1: Compute size of live data, live per FID (new FID tops).

// Hidden invariant: I need the objects packed in FID order (for escapes and
// partial/generational GCs).  I walk the thread stack for roots, and that
// walk visits the MOSTLY objects in FID order (smallest FID first or oldest
// first) and thus MOSTLY copies the live objects in FID order.  BUT there can
// be old frame objects only reached by young objects and not by any stack
// root, i.e., Frame-order is NOT FID-order.
class SBAFindLive: public SbaClosure {
public:
  uint *const _fid_sz;

  SBAFindLive( JavaThread *jt, uint *fid_sz ) : SbaClosure(jt), _fid_sz(fid_sz) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null() ) return; // Ignore NULL, obviously
    objectRef r = UNPOISON_OBJECTREF(*p,p);
    if( !r.is_stack() ) return; // No collection of heap-based objects

    oop tmp = r.as_oop();
    SBAPreHeader *pre = stackRef::preheader(tmp);
    assert0( !pre->is_dead() );
    assert0( _jt->sba_is_in((address)pre) );
    if( test_set(tmp) ) return; // Already visited & marked.

    // Compute size
    int size_in_words = tmp->size();
    int size_in_bytes = size_in_words<<LogBytesPerWord;
    // Accumulate per-FID size
    _fid_sz[pre->fid()] += size_in_bytes + sizeof(SBAPreHeader);
    tmp->oop_iterate(this);   // Iterate OLD object
  }
  void do_derived_oop(objectRef* base, objectRef* derived) { 
Untested("RefPoisoning");
    if( !base->is_stack() ) return;
    // Derived ptrs are handled before the regular do_oop on the base in
    // OopMap (I assume the other GC parts depend on that ordering).
    intptr_t offset = derived->raw_value() - base->raw_value();
    *(intptr_t*)derived = offset;
  }
};

// -------------------------------------
// GC Phase 2: Decide if we should grow or shrink the SBA area
size_t SBAArea::policy_resize( int alloc_bytes, size_t tospace_bytes, size_t total_live, BufferedLoggerMark *blm ) {
  // Not sure I need this invariant, but keeping things a power-of-2 seems to make sense
  assert0( (tospace_bytes & -tospace_bytes) == tospace_bytes ); // Enforce power-of-2
  jlong old_ticks = _ticks_at_last_gc;
_ticks_at_last_gc=os::elapsed_counter();
  double secs_since_last_gc = (_ticks_at_last_gc - old_ticks)/(double)os::elapsed_frequency();

  if( VerboseSBA || PrintGC )
    blm->out("[Stack GC thrd " PTR_FORMAT " (%3.3fsecs inter-GC) ", _jt, secs_since_last_gc);

  // Policy resize next heap
  double resize_gc_time = 3.0;
  debug_only( resize_gc_time = 12.0 ); // Debug code is low, give more time
  if (UseITR) {
    resize_gc_time *= 5;
  }
  // Leftover from last collection (an estimation of leftover from this
  // collection) plus the allocation which triggered this GC remains greater
  // than 1/4th of tospace (remains > 1/8 after doubling tospace)?
  size_t target_space = total_live+alloc_bytes;
  if( (tospace_bytes>>2) < target_space ) {
    // Grow until we think the new allocation will fit in the new tospace with
    // plenty of spare.
    while( (tospace_bytes>>2) < target_space )
      tospace_bytes <<= 1;

  } else if( secs_since_last_gc < resize_gc_time && // time between GC's is too short?
             alloc_bytes ) {    // And this not a forced (heap-escape) GC?
    // GC's are too frequent.  Grow heap to slow down time between GCs.
    tospace_bytes <<= 1;

  } else if( secs_since_last_gc > 4.0*resize_gc_time && // time between GC's is too long?
             // AND Est'd new usage < 1/8th current size?
             (tospace_bytes>>3) > target_space ) {
    // GC's are long-time apart and we are not needing tons of space.  Return
    // unneeded space by shrinking heap.
    tospace_bytes >>= 1;
  }
  // Hard limits on stack heap size
  if( tospace_bytes < InitialStackSize ) tospace_bytes = InitialStackSize; 
  if( tospace_bytes >   M*SBAStackSize ) tospace_bytes = M*SBAStackSize;
  // Not sure I need this invariant, but keeping things a power-of-2 seems to make sense
  assert0( (tospace_bytes & -tospace_bytes) == tospace_bytes ); // Enforce power-of-2
  return tospace_bytes;
}


// -------------------------------------
// GC Pass 3: Crawl stack and all live objects.  Copy/compact the live
// objects.  Leave forwarding ptrs behind.
class SBACopyCompact: public SbaClosure {
  uint *const _fid_end;
public:
  int _excess_live_words;
  HeapWord *_heap_adr;          // Place to put excessive bytes
  SBACopyCompact( SbaClosure *old_visit_bits, uint *fid_end, int excess_live_words, HeapWord *heap_adr ) : SbaClosure(old_visit_bits), _fid_end(fid_end), _excess_live_words(excess_live_words), _heap_adr(heap_adr) { }
  void copy_and_forward( stackRef *p ) {
    objectRef r = UNPOISON_OBJECTREF(*p,p);
    oop tmp = r.as_oop();       // Old object location
    SBAPreHeader *pre = stackRef::preheader(tmp);
    assert0(  _jt->sba_is_in_or_oldgen((address)pre) ); // Only copy out oldgen objects
    assert0( !_jt->sba_is_in          ((address)pre) );
    // If a FullGC has moved methodOops, revalidate the hints.  Zap any broken hints.
    SBAArea *sba = _jt->sba_area();
    if( sba->_gc_moved_klasses_and_moops )
      pre->validate_hint();

    // Copy then forward the object
    int size_in_words = tmp->size();
    int size_in_bytes = size_in_words<<LogBytesPerWord;

    // See if we can put this object with no internal ptrs straight into the
    // heap.  TypeArrays can sometimes be big, so this can really save
    // stack-allocation space.
HeapWord*adr;
    if( size_in_words+1 < _excess_live_words &&
        (tmp->is_typeArray() || has_no_stack_refs(tmp,size_in_words)) ) {
      // This object is thrown to the heap
      adr = _heap_adr;
      _heap_adr              += size_in_words;
      _excess_live_words     -= size_in_words;
      sba->_moved_heap_bytes += size_in_bytes;
      ((heapRef*)&r)->set_value_base((oop)adr,objectRef::new_space_id,r.klass_id(),objectRef::discover_nmt(objectRef::new_space_id,(oop)adr));
Universe::heap()->tlab_allocation_mark_new_oop(adr);
    } else {
      // Copy/Compact this object
      int fid = pre->fid();     // Get which FID area to place object
      int fid_top = sba->_fid_top[fid];
      adr = (HeapWord*)(sba->_start+fid_top);
      assert0( fid_top+size_in_bytes+sizeof(SBAPreHeader) <= (uint)_fid_end[fid] );
      sba->_fid_top[fid] = fid_top+size_in_bytes+sizeof(SBAPreHeader);
      sba->_moved_bytes += sizeof(SBAPreHeader);
      *adr++ = ((HeapWord*)tmp)[-1]; // copy pre-header to new location
      ((stackRef*)&r)->set_va(adr);     // New ref to the new location
    }
    // Store new pointer down over old
    *(objectRef*)p = POISON_OBJECTREF(r,p); // If it was poisoned, store it back poisoned
    // Copy object
Copy::aligned_disjoint_words((HeapWord*)tmp,adr,size_in_words);
#ifdef ASSERT
    julong* fields = (julong*)tmp + oopDesc::header_size();
    julong* limit = fields + size_in_words-oopDesc::header_size();
    fields++;                   // leave ary length alive so 'print' can parse stackheap
while(fields<limit){
      assert0( *fields != badHeapWordVal ); // Can throw false positives
      *fields++ = badHeapWordVal;
    }
#endif
    sba->_moved_objs++;
    sba->_moved_bytes += size_in_bytes;
    // Make tmp forward to it's new location by stomping over the old pre-header.
    pre->forward(r);
    // Recursively walk objects copied into TO-space
    ((oop)adr)->oop_iterate(this);
  }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return;
    objectRef r = UNPOISON_OBJECTREF(*p,p);
    if( !r.is_stack() ) return; // No updating of heap-based objects

    SBAPreHeader *pre = ((stackRef)r).preheader();
    assert0( !pre->is_dead() );
    // Derived pointers fixed up a few bases.  Ignore those; they have already been done.
    if( !_jt->sba_is_in((address)pre) ) {
      // Update the TO-SPACE ptr to refer to the new TO-SPACE address.
      if( pre->is_forward() ) {  // Has target has been forwarded?  
        *p = POISON_OBJECTREF(pre->get_forward(),p);
      } else {
        copy_and_forward((stackRef*)p);
      }
    }
  }
  void do_derived_oop(objectRef* base, objectRef* derived) {
Untested("RefPoisoning");
    if( !base->is_stack() ) return;
    intptr_t offset = *(intptr_t*)derived;
    oop base_oop = base->as_oop();
    SBAPreHeader *pre = stackRef::preheader(base_oop);
    if( !_jt->sba_is_in((address)base_oop) && !pre->is_forward() ) 
      copy_and_forward((stackRef*)base);
    // Update derived ptr to new location
    if( pre->is_forward() ) {
      objectRef new_base_ref =  pre->get_forward();
      assert0( new_base_ref.is_stack() );
      base_oop = new_base_ref.as_oop();
      pre = stackRef::preheader(base_oop);
    }
    int kid = objectRef::discover_klass_id(base_oop);
    int nmt = objectRef::discover_nmt(objectRef::stack_space_id, base_oop);
    uint64_t va = ((uint64_t)base_oop)+offset;
    Unimplemented();
    stackRef dref = stackRef((oop)va, kid, nmt, pre->fid());
    *derived = dref;
  }
};

// -------------------------------------
// Thread-Local Garbage Collection!
void SBAArea::collect( int alloc_bytes ) {
  _stack_collections++;
  ResourceMark rm;
elapsedTimer timer_gc;
timer_gc.start();
  _last_verify = 0;             // Do the expensive verify
  if( VerifySBA ) verify();     // Verify before doing GC
  uint fid_sz[SBA_MAX_FID+1];   // Live data bytes per-FID
  int capfid = capped_fid();
  for( int i=0; i<=capfid; i++ ) fid_sz[i] = 0;
  size_t old_heap_bytes = _moved_heap_bytes;
  BufferedLoggerMark blm(NOTAG,0,VerboseSBA || PrintGC );

  // ---
  // Pass 1: Compute size of live data, live per FID (new FID tops).

  // Hidden invariant: I need the objects packed in FID order (for escapes and
  // partial/generational GCs).  I walk the thread stack for roots, and that
  // walk MOSTLY visits objects in FID order (smallest FID first or oldest
  // first) and thus MOSTLY copies the live objects in FID order.  BUT there
  // can be old frame objects only reached by young objects and not by any
  // stack root, i.e., Frame-order is NOT FID-order.
elapsedTimer timer_findlive;
timer_findlive.start();
  SBAFindLive FL_closure(_jt,fid_sz);
  _jt->oops_do_stackok( &FL_closure, SBA_ROOT_FID );

  // Now I have per-FID sizes, plus some forwarding ptrs to heap space (for
  // large typearrays) plus visit bits to determine live-from-dead, plus max
  // FID in use.  
  int64_t total_live = 0;
  for( int i=0; i<=capfid; i++ )
    total_live += fid_sz[i];    // Compute total live
timer_findlive.stop();

  // ---
  // Phase 2: Resize the heap
elapsedTimer timer_resize;
timer_resize.start();
  // Current size of heap.  FROM and TO spaces will be power-of-2 sized and
  // power-of-2 aligned, but they may be different powers.  We are always
  // guarenteed that all things in FROM space will either fit between _start
  // and _reserved_heap or after _start+max and the end of the reserved area.
  size_t frspace_bytes  = _jt->sba_max_adr() - _start + TLABPrefetchSize + BytesPerCacheLine;
  size_t frspace_start = _start-_reserved_heap;
  assert0( is_power_of_2(frspace_bytes) );
  assert0( is_power_of_2(frspace_start) || !frspace_start );
  assert0( frspace_bytes <= frspace_start || _start==_reserved_heap); // Incorrect prior placement?
  int frspace_start_log = log2_intptr(frspace_start);
  size_t old_used_bytes = _jt->sba_used();
  int used_size_log = log2_intptr(old_used_bytes)+1;
  _fid_top[capfid] = old_used_bytes;
  // New size of heap
  size_t tospace_bytes = policy_resize( alloc_bytes, frspace_bytes, total_live, &blm );
  int tospace_size_log = log2_intptr(tospace_bytes);
  if( (frspace_bytes != tospace_bytes) && (VerboseSBA || PrintGC) ) 
    blm.out(" adjusting heap to %dK ",tospace_bytes>>10);
  // Allocate the new TO-space.  
  address tospace_start = _reserved_heap;
  if( _start == _reserved_heap ) { // Need to flip to area past _reserved_heap
    tospace_start += used_size_log     <= tospace_size_log ? (1L<<tospace_size_log) : (1L<<used_size_log);
  } else {
    tospace_start += frspace_start_log <  tospace_size_log ? (1L<<tospace_size_log) : 0;
  }
  assert0( tospace_start+tospace_bytes <= _start ||
           _jt->sba_top_adr() <= tospace_start ); // No overlap of spaces
  setup_alloc_space( tospace_start, tospace_bytes );

  // Record FID area STARTS in the new TO-space, not the FID area TOPs!
  // Convert fid sizes in fid_sz to fid-ends in fid_sz.
  _fid_top[0] = 0;      // FID-area START
for(int i=1;i<=capfid;i++){
    _fid_top[i] = fid_sz[i-1];  // FID-area START
    fid_sz[i] += fid_sz[i-1];   // Convert fid_sz into fid_end
  }
timer_resize.stop();

  // ---
  // Pass 3: Crawl stack and all live objects.  Copy/compact the live objects.
  // Leave forwarding ptrs behind.
elapsedTimer timer_copycompact;
timer_copycompact.start();
  // New top of the new TO-space, for use in "_jt->sba_is_in(xxx)" calls.
  stackRef top = stackRef( (oop)(_start+total_live), 0, 0, (intptr_t)capfid );
  _jt->_sba_top = top;

  // See if we need to throw some stuff to the heap to make forward progress.
  // Required if everything is live, i.e., a stack GC will not free any space.
  int64_t excess_live_words = (int64_t)((total_live - (M*SBAStackSize>>3)))>>LogBytesPerWord;
  int64_t max_chunk = Universe::heap()->max_tlab_size() ? (Universe::heap()->max_tlab_size()-17) : (M*SBAStackSize)>>LogBytesPerWord;
  if( excess_live_words < 0 ) excess_live_words = 0;
  if( excess_live_words > max_chunk ) excess_live_words = max_chunk;
HeapWord*heap_adr=NULL;
  if( excess_live_words > 0 ) {
    heap_adr = CollectedHeap::allocate_chunk_from_tlab(_jt, excess_live_words, CollectedHeap::DontZeroMemory);
    if( !heap_adr ) {           // Failed to allocate any space?
      bool gc_overhead_limit_was_exceeded;
      heap_adr = Universe::heap()->mem_allocate( excess_live_words, false, false, &gc_overhead_limit_was_exceeded );
      if( !heap_adr ) {         // Still no space?
        excess_live_words = 0;
NEEDS_CLEANUP;//Might ponder throwing an OOM here?
      }
    }
  }
  SBACopyCompact CC_closure(&FL_closure,fid_sz,excess_live_words, heap_adr);
  _jt->oops_do_stackok( &CC_closure, SBA_ROOT_FID );
  // We moved some stuff to heap, so not all FID areas got completely used.
  // Fill in the unused space.  It will be reclaimed on the next stack GC.
for(int i=0;i<capfid;i++){
    if( _fid_top[i] < fid_sz[i] ) { // Did not copy everything that was live?
      HeapWord *adr = (HeapWord*)(_start+_fid_top[i]);
      ((SBAPreHeader*)adr)->init(i,0);
      ((SBAPreHeader*)adr)->zap_hint();
      adr++;                    // Skip the SBAPreHeader
      MemRegion mr(adr, (fid_sz[i]-_fid_top[i] - sizeof(SBAPreHeader))>>LogBytesPerWord);
      mr.fill();                // Make chunk heap-parseable in case of a GC
_fid_top[i]=fid_sz[i];
    }
  }
  // We moved some stuff to heap, so not all FID areas got completely used.
  // Reclaim space at the stack-top, by rolling back.
  if( _fid_top[capfid] < fid_sz[capfid] ) {
    total_live -= fid_sz[capfid] - _fid_top[capfid];
    fid_sz[capfid] = _fid_top[capfid];
    // Tighten up the SBA top
    stackRef top = stackRef( (oop)(_start+total_live), 0, 0, capfid );
    _jt->_sba_top = top;
  }
  // Any unused excess_live space needs to be filled as well
  MemRegion mr(CC_closure._heap_adr, CC_closure._excess_live_words);
  mr.fill();                // Make chunk heap-parseable in case of a GC
timer_copycompact.stop();

  // Clear the indicator that a FullGC has moved/removed klasses & methodOops.
  _gc_moved_klasses_and_moops = false;
  // Force any objects moved to heap to be coherent - as soon as this thread
  // starts running those objects might be made visible top other threads.
  Atomic::membar();

  // Restart the CLZ pipeline
  uint64_t *clz = (uint64_t*)(_start+total_live);
  while( (((intptr_t)clz) & (BytesPerCacheLine-1)) != 0 )
    *clz++ = 0;
  for( uint i=0; i<TLABPrefetchSize/HeapWordsPerCacheLine; i++ ) {
    Prefetch::overwrite(clz, 0);
    clz += HeapWordsPerCacheLine;
  }


  // Stats
timer_gc.stop();
  if( VerboseSBA || PrintGC ) {
    blm.out("%dK->%dK (heapify %dK) in %1.5f sec (LV %1.5f, CC %1.5f)]",
            (old_used_bytes+1023)>>10,
            (total_live+1023)>>10,
            (_moved_heap_bytes -old_heap_bytes+1023)>>10,
            timer_gc.seconds(),
            timer_findlive.seconds(),
            timer_copycompact.seconds() );
  }
  if( alloc_bytes ) {          // Heap escapes have 0 alloc bytes and can cause GCs
    Atomic::add_ptr(timer_gc         .ticks(),&StackBasedAllocation::_stack_gc_ticks            );
    Atomic::add_ptr(timer_findlive   .ticks(),&StackBasedAllocation::_stack_gc_findlive_ticks   );
    Atomic::add_ptr(timer_resize     .ticks(),&StackBasedAllocation::_stack_gc_resize_ticks     );
    Atomic::add_ptr(timer_copycompact.ticks(),&StackBasedAllocation::_stack_gc_copycompact_ticks);
  }
  if( VerifySBA ) {
for(int i=0;i<=capfid;i++){
      assert0( _fid_top[i] == fid_sz[i] ); // Did not copy everything that was live?
    }
    _last_verify = os::elapsed_counter(); // Do expensive verify now
    SBAPreHeader *p = (SBAPreHeader *)_start;
address end_adr=_jt->sba_top_adr();
    int fid = 0;
    while( (address)p < end_adr ) {
      oop q = (oop)(p+1);
      size_t sz = q->size()<<LogBytesPerWord;
      // Nothing in the compacted heap should be dead or forwarded
      assert0( !p->is_dead() && !p->is_forward() && p->fid() >= fid );
      if( p->fid() > fid ) fid = p->fid(); // FID monotonically grows
      p = (SBAPreHeader*)((address)q + sz);
    }
    verify(); // Verify after doing GC
  }
}

// If the heap is not parsable, do a stack-GC to force it to be parseable
// (by removing all dead objects which might have stale klass refs).  The
// extra argument is carried live across the GC as a service.
objectRef SBAArea::make_heap_parsable( stackRef escapee ) {
  if( !_gc_moved_klasses_and_moops ) return escapee;
  // A recent FullGC has killed off klasses (so dead SBA objects have stale
  // klass ptrs and cannot have size() called against them, so I cannot parse
  // the SBA heap) and methodOop hints may have been removed as well.  Note
  // that I cannot give up the _jvm_lock between the collect(0) call and a
  // following sweep, lest a FullGC make the heap unparseable again.

  // Handlize escapee.  This might be the ONLY reference to the escapee who
  // might otherwise appear dead in this collection.
  Handle h(_jt,escapee);
  // Force a collection to remove dead SBA objects and stale methodOop refs.
  collect(0);           
  // Reload after stack GC moved
  return h.as_ref();
}

// Print stack
extern "C" void hsfind1(intptr_t x, bool print_pc, xmlBuffer *xb, Thread *thr);

// -------------------------------------
// Warning - this is BIG
void SBAArea::print_on(const outputStream* st) const {
tty->print_cr("   Address          Data        FID");
  HeapWord *top = (HeapWord*)_jt->sba_top_adr();
HeapWord*start=(HeapWord*)_start;
  HeapWord *pre = start;
HeapWord*skipping=0;
  int mode = 0;                 // No fancy printing yet
  intptr_t dup1 = (*(intptr_t*)top)+1; // Definitely a different word that is not equal to '*top'
  intptr_t dup2 = dup1;
  int fid = 0;
  int skip_fid = 0;
  int maxfid = _jt->curr_sbafid();
for(HeapWord*p=start;p<top;p++){
    while( p >= (fid==maxfid ? top : (HeapWord*)(_start+_fid_top[fid])) )
      fid++;
    if( p >= top-2 ) dup1 = dup2+1; // Force unequal so last few rows print ok

    // Check for long streams of duplicate data, ala zeroed arrays or
    // in debug mode, 0xbaadbaad stompage.
    if( dup1 == dup2 && dup2 == *(intptr_t*)p && p != pre && 
        (mode==0 || mode==4) ) {
      // Mode 0 means print out an initial "..."
      // Mode 4 means NO PRINTOUT for as long as the duplication continues.
      if( mode == 0 ) { mode=4; skip_fid = fid; skipping = p; }
    } else {
      if( mode==4 ) {
        mode=0;   // Aha! Differences.  Switch off mode==4==noprinting
        if( p-1 == skipping ) { 
          // No skipping at all!
        } else {
          if( p-2 != skipping ) // No real skipping
tty->print_cr("... skipping ...");
          tty->print("%12.12lx 0x%16.16lx %2d ",(long unsigned int)(p-1),dup2,skip_fid);
          hsfind1(dup2,false/*pc lookup*/, NULL, _jt);
        }
      }
      dup1 = (intptr_t)*(HeapWord**)p;
      tty->print("%12.12lx 0x%16.16lx %2d ",(long unsigned int)p,dup1,fid);
    }
    if( p == pre ) {
      ((SBAPreHeader*)pre)->print();
      oop o = (oop)(pre+1);
      pre = ((HeapWord*)o)+o->size();
      mode = 1;
    } else if( mode == 1 ) {
      ResourceMark rm;
      tty->print_cr("mark, kid=%d, %s",((oop)p)->mark()->kid(), 
#ifdef PRODUCT
"no klassnames in product mode"
#else
                    ((oop)p)->blueprint()->internal_name()
#endif
                    );
      mode = 0;
    } else if( mode == 0 ) {
      hsfind1(dup1,false/*pc lookup*/, NULL, _jt);
    }
    dup1 = dup2;                // Roll-forward the dup checks
    dup2 = *(intptr_t*)p;
  }
}


// -------------------------------------
class SbaVerify: public SbaClosure {
  int _pfid;                    // Parent's FID
 public:
  SbaVerify( JavaThread *jt ) : SbaClosure(jt), _pfid(SBA_MAX_FID) { }
  virtual void do_oop(objectRef* p) {
    if(  p->is_null()  ) return;
    objectRef r = UNPOISON_OBJECTREF(*p,p);
    if( !r.is_stack() ) return; // No updating of heap-based objects, and no peeking in case a normal gc is in progress
    stackRef stk = (stackRef)r;
    oop tmp = r.as_oop();
    assert0(tmp->is_oop());
    guarantee( _jt->sba_is_in((address)tmp), "Object is in proper thread" );
    assert0( r.klass_id() == tmp->blueprint()->klassId() );

    SBAPreHeader *pre = stackRef::preheader(tmp);
    guarantee( !pre->is_dead(), "No ptrs to dead" );
    int myfid = pre->fid();
    assert( myfid <= _pfid, "Only point to older frames" );
    if( test_set(tmp) ) return;
    SBAArea *sba = _jt->sba_area();
    uint32_t off = (address)tmp - sba->_start;
    assert0( (myfid ? sba->_fid_top[myfid-1] : 0) <= off );
    assert0( off < (myfid == _jt->curr_sbafid() ? _jt->sba_used() : sba->_fid_top[myfid]) );
    
    int save_fid = _pfid;       // Save the old FID
    _pfid = myfid;              // 'p' is now the parent object
    tmp->oop_iterate(this);     // Verify contents of 'p'
    _pfid = save_fid;           // Restore old FID
  }
  void do_derived_oop(objectRef* base_ptr, objectRef* derived_ptr) {
    if(  base_ptr->is_null()  ) return;
Untested("refpoisoning death");
    if( !base_ptr->is_stack() ) return; // No updating of heap-based objects
    oop tmp = base_ptr->as_oop();
    SBAPreHeader *pre = stackRef::preheader(tmp);
    guarantee( !pre->is_dead(), "No ptrs to dead" );
    int myfid = stackRef(*base_ptr).preheader()->fid();
    guarantee( myfid <= _pfid, "Only point to older frames" );
    guarantee( pre->fid() == myfid, "Matching FIDs" );
    guarantee( _jt->sba_is_in((address)tmp), "Object is in proper thread" );
    oop dtmp = derived_ptr->as_oop();
    guarantee( _jt->sba_is_in((address)dtmp), "Object is in proper thread" );
    intptr_t offset = derived_ptr->raw_value() - base_ptr->raw_value();
    guarantee( -100 < offset && offset < (tmp->size()<<LogBytesPerWord)+100, "offset out of control" );
  }
};

void SBAArea::verify(){
  intptr_t *end = (intptr_t*)_jt->sba_max_adr();
  intptr_t *top = (intptr_t*)_jt->sba_top_adr();
  assert0( top <= end );
  assert0( end[0] == 0 );       // Verify the CLZ overrun area is intact
  assert0( TLABPrefetchSize==0 || end[(TLABZeroRegion/sizeof(HeapWord))-1] == 0 );
  assert0( end[(TLABPrefetchSize/sizeof(HeapWord))+HeapWordsPerCacheLine-1] == VERIFY_FENCE );
  // Verify the zero region after allocation
  int extra_bytes = (((intptr_t)top + (BytesPerCacheLine-1)) & -BytesPerCacheLine) - (intptr_t)top;
  int clz_word_count = (TLABZeroRegion + extra_bytes) / sizeof(HeapWord);
for(int i=0;i<clz_word_count;i++)
    assert0( top[i] == 0 );

  assert0( is_valid_fid( _jt->curr_sbafid() ) );

  // Verify stack-ordering, if legal right now.
  // Can't walk after a GC has crushed the size info of dead-klass objects.
  long now = os::elapsed_counter();
  if( !_gc_moved_klasses_and_moops && 
      (double)(now -_last_verify)/ (double)os::elapsed_frequency() > 1.0 ) {
    _last_verify = now;         // Throttle expensive verify frequency
    SBAPreHeader *p = (SBAPreHeader *)_start;
    SBAPreHeader *top_adr = (SBAPreHeader *)top;
    int fid = 0;                // Initial FID
    while( p < top_adr ) {      // Sweep entire SBA space
      oop q = (oop)(p+1);
      size_t sz = q->size()<<LogBytesPerWord;
      assert0( !p->is_forward() );
      if( !p->is_dead() && p->fid() != fid ) {
        assert0( fid < p->fid() ); // FID monotonically grows
        fid = p->fid();
      } 
      p = (SBAPreHeader*)((address)q + sz);
    }
  }
  // Now do the visit checks
  ResourceMark rm;
  SbaVerify V(_jt);
  _jt->oops_do_stackok( &V, SBA_ROOT_FID );
}

// Scale factor: convert a byte-offset into the Stack Area into a bit-index.
int SbaClosure::scale( uint byteoffset ) {
  assert0( byteoffset >= 0 && byteoffset <= M*SBAStackSize );
  // objects are at least 2 HeapWords in size (1 for preheader, 1 for header)
  assert0( 1 + oopDesc::header_size() == 2 );
  return byteoffset >> (LogBytesPerWord + 1);
}

SbaClosure::SbaClosure(JavaThread *jt) : _jt(jt), 
  _v_size((scale(jt->sba_used()+wordSize)+7)>>3),
  _visit(NULL), _lo(_v_size) {
}

SbaClosure::SbaClosure(SbaClosure *old) : _jt(old->_jt),
  _v_size(old->_v_size),
  _visit( old->_visit ), _lo(old->_lo) {
  // We avoid re-zeroing by having every other use of the visit bits flips sense
  //bzero(_visit,_v_size);
}

// Convert an SBA address into a dense offset, and check the bitarray for liveness.
int SbaClosure::test( oop p ) {
  if( !_visit ) return 0;       // Never visited at all
  int bit_idx = scale((address)p - _jt->sba_area()->_start);
  int byt_idx = bit_idx>>3;
  assert0( byt_idx < _v_size );
  if( byt_idx < _lo ) return 0; // In the lazily-cleared portion
  char *adr = &_visit[byt_idx];
  char mask = 1<<(bit_idx&7);
  return *adr & mask;
}

// Convert an SBA address into a dense offset, and set+check the bitarray for liveness.
int SbaClosure::test_set( oop p ) {
  if( !_visit ) {       // Oops, must make space to record the bit-set
    _visit = (char*)_jt->resource_area()->Amalloc(_v_size+BytesPerCacheLine); // UNZEROed space
    _visit = (char*)(((intptr_t)(_visit+BytesPerCacheLine-1)) & -BytesPerCacheLine);
    while( _lo & (BytesPerCacheLine-1) ) _visit[--_lo] = 0; // Zero to a cache-line
    // Prefetch-zero the prior line bits
    if( _lo ) 
      Prefetch::overwrite(&_visit[_lo-BytesPerCacheLine], 0);
  }
  int bit_idx = scale((address)p - _jt->sba_area()->_start);
  int byt_idx = bit_idx>>3;
  assert0( byt_idx < _v_size );
  if( byt_idx < _lo ) {         // Oops, landing in the lazily-zeroed space
    while( byt_idx < _lo ) {
      // Oh thats right: we already pre-CLZed the prior line
      _lo -= BytesPerCacheLine;
      // Prefetch-zero the prior line bits
      if( _lo ) Prefetch::overwrite(&_visit[_lo-BytesPerCacheLine], 0);
    }
  }
  char *adr = &_visit[byt_idx];
  char mask = 1<<(bit_idx&7);
  int res = *adr & mask;
  *adr |= mask;
  return res;
}

int SbaClosure::test_clr( oop p ) {
  if( !_visit ) return 0;       // Never visited before
  int bit_idx = scale((address)p - _jt->sba_area()->_start);
  int byt_idx = bit_idx>>3;
  assert0( byt_idx < _v_size );
  if( byt_idx < _lo ) return 0; // In the lazily-cleared portion
  char *adr = &_visit[byt_idx];
  char mask = 1<<(bit_idx&7);
  int res = *adr & mask;
  *adr &= ~mask;
  return res;
}
