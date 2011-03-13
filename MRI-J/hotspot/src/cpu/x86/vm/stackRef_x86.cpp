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


#include "bytecodes.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "log.hpp"
#include "methodOop.hpp"
#include "nativeInst_pd.hpp"
#include "resourceArea.hpp"
#include "stackBasedAllocation.hpp"

#include "allocation.inline.hpp"
#include "oop.inline.hpp"
#include "stackRef_pd.inline.hpp"

// Over-write all fields, used in initial allocation of an object
void SBAPreHeader::init(int fid, methodOop moop, int bci ) {
  _ispc = 0;
  _bci = bci;
  _fid = fid;
  set_moop(moop);
  assert0( is_moop_hint() );
if(VerifySBA)verify();
}

void SBAPreHeader::init(int fid, address pc ) {
  _ispc = 1;
  _bci = 0;
  _moid = (intptr_t)pc;
  _fid = fid;
  assert0( is_pc_hint() );
if(VerifySBA)verify();
}

void SBAPreHeader::oops_do(OopClosure*f){
NEEDS_CLEANUP;//Need weak-link handling for methodOop, or nmethod unloading handling
  if( _ispc ) return;           // PC, so no methodOop
  // We'd like to treat methodOops as weak-refs and only zap if already dying.
  // The problem is that requires another pass - and I've got a large SBA area
  // needing collecting.
  if( !moop() )
    zap_hint();
}

// Zap any broken allocation-site hints.  FullGC can remove or unload
// methodOops, and methodCodeOops can be removed as well.
void SBAPreHeader::validate_hint() {
  if( is_pc_hint() ) {
    // Hints go stale.  Code can be unloaded, and the code space reused for new code.
    // Make sure we are seeing a true allocation site before proceeding.
CodeBlob*blob=CodeCache::find_blob(pc());
    if( !blob || !blob->is_java_method() || // Code has moved out from under us
        !blob->contains(pc()) ||
        !NativeCall::is_call_before(pc()) || // Could be deoptimized or simply wrong nmethod
        !NativeAllocationTemplate::at(nativeCall_before(pc())) ) {
      zap_hint();
    }
  } else if( is_moop_hint() ) {
methodOop m=moop();
    assert0( moop()->is_oop() );
address bcp=m->bcp_from(_bci);
Bytecodes::Code bc=(Bytecodes::Code)(*bcp);
    switch( bc ) {
case Bytecodes::_new:
case Bytecodes::_newarray:
case Bytecodes::_anewarray:
case Bytecodes::_multianewarray:
case Bytecodes::_new_heap:
case Bytecodes::_newarray_heap:
case Bytecodes::_anewarray_heap:
    case Bytecodes::_invokespecial: // CLONE callsite
    case Bytecodes::_invokestatic: // e.g. j/l/reflect/Array/newArray
      break;
    default:
      zap_hint();
      break;
    }
  }
}


// Heuristics to control flipping allocation sites to stack-allocation or
// heap-allocation.  If we heap-allocate too often we lose out on the SBA
// benefits.  If we stack-allocate too often we cause too many escapes.

// Record escaping allocation sites (by recording SBAPreHeaders), record the
// count of escapes decayed over time, and the number of times we flipped
// from stack-to-heap allocation.

// If the decayed-escape-count is low (few escapes-over-time), then we leave a
// site as stack-allocation despite an escape.  If the decayed-escape-count is
// large, we flip the site to doing heap allocations, we record the failure
// and we pre-inflate the decayed-escape-count by the scaled fail count.  This
// keeps the normal decay process from flipping the site back to stack
// allocation with an exponential back-off.
int64_t SBAPreHeader::_escape_sites[1<<SITES_LOG_SIZE];
int64_t SBAPreHeader::_decayed_escapes[1<<SITES_LOG_SIZE];
int     SBAPreHeader::_fails[1<<SITES_LOG_SIZE];

// Compute the hash-table index for this SBAPreHeader
int SBAPreHeader::get_escape_idx( bool claim_slot ) {
  int64_t x = *(int64_t*)this;    // The KEY
  int idx = (_moid^_bci) & SITES_MASK; // Initial hash value
  int reprobe = idx|1;          // Reprobe with any relatively prime value, i.e. an odd number
  int low = idx;                // Least-escaping site
  for( int i=0; i<9/*reprobe limit*/; i++ ) {
    if( _escape_sites[idx] == 0 ) { // Empty slot? 
      if( !claim_slot ) return -1;
      _escape_sites[idx] = x;   // Claim it, racey, sloppy, who cares?
    }
    if( _escape_sites[idx] == x ) // Hit?
      return idx;
    // Else collision; reprobe.  First gather least-escaping site.
    if( _fails[idx] < _fails[low] ||
        (_fails[idx] == _fails[low]  &&
         _decayed_escapes[idx] < _decayed_escapes[low]) )
      low = idx;                // Collect slot with fewest failures
    idx = (idx+reprobe)&SITES_MASK;
  }

//  int cnt=0;
//  for( int idx=0; idx<(1<<SITES_LOG_SIZE); idx++ ) 
//    if( _escape_sites[idx] )
//      cnt++;
//  tty->print_cr("Long collision chain, table is %d/%d: ", cnt,1<<SITES_LOG_SIZE);
//  idx = (_moid^_bci) & SITES_MASK; // Initial hash value
//  for( int i=0; i<9/*reprobe limit*/; i++ ) {
//    SBAPreHeader *pre = ((SBAPreHeader*)(&_escape_sites[idx]));
//    tty->print("idx=%4d  orig=%4d  ",idx,(pre->_moid^pre->_bci)&1023);
//    tty->print("escapes=%3d  fails=%d ",_decayed_escapes[idx],_fails[idx]);
//    tty->print("%s SBA=", idx==low?"LOW":"   ");
//    pre->print();
//    idx = (idx+reprobe)&SITES_MASK;
//  }

  // No hit, nor any free slots, so replace lowest guy
  if( !claim_slot ) return -1;
  _escape_sites[low] = x;
  _decayed_escapes[low] = 0;
  _fails[low] = 0;
  return low;
}

// Should a JVM call with this return-PC stack-allocate or not?
bool SBAPreHeader::should_stack_allocate(address pc){
  SBAPreHeader pre;
  pre.init(1,pc);
  int idx = pre.get_escape_idx(false); // get idx; claim slot in table
  if( idx == -1 ) return true;       // Not in table now, so never escaped yet
  return _decayed_escapes[idx] < SBAEscapeThreshold;
}

void SBAPreHeader::update_allocation_site( int escape_to_fid ) {
  // Currently we only support 2 allocation-site flavors: stack or heap.  We
  // have no way to record, hint or allocate further up the stack so we
  // ignore stack hints.  Heap hints will flip a stack-hint allocation site to
  // heap-hint, but not vice-versa: monotonically allocation sites start
  // heap-hinting.
  if( escape_to_fid > HEAP_FID ) return;
  validate_hint();              // Validate the allocation-site hint
  if( !UseSBAHints ) return;    // Don't bother flipping any site from stack-to-heap
  // Only bother updating allocation site if we recorded one
  if( !is_pc_hint() && !is_moop_hint() )
    return;

  // Record one escape; see if we should flip this site from stack-to-heap
  int idx = get_escape_idx(true);   // Get a place to record escapes
  if( _decayed_escapes[idx]++ < SBAEscapeThreshold )
    return;

  // Flip to heap-allocation
  if( is_pc_hint() ) {
    NativeAllocationTemplate *nat = NativeAllocationTemplate::at(nativeCall_before(pc()));
    if( !nat ) Unimplemented(); // Some JVM call needs to heap-only?
    if( !nat->set_heap() )      // Set the allocation site to be heap-only
      return;
  } else {                      // MethodOop+BCI allocation site
    // Find a 'new' bytecode and flip it into a 'new_heap' bytecode
    address bcp = moop()->bcp_from(_bci);
    switch( (Bytecodes::Code)(*bcp) ) {
    case Bytecodes::_new:        *bcp = Bytecodes::_new_heap      ; break;
    case Bytecodes::_newarray:   *bcp = Bytecodes::_newarray_heap ; break;
    case Bytecodes::_anewarray:  *bcp = Bytecodes::_anewarray_heap; break;
    case Bytecodes::_multianewarray:  
      //tty->print_cr("No way to patch multianewarray (need a multanewarray bytecode hint)");
      return;
    default:
      return;                   // Already patched stack->heap
    }
  }

  if( VerboseSBA ) {
    BufferedLoggerMark m( NOTAG, 0, VerboseSBA );
    m.out("=== SET_HEAP %d escapes, %d failed priors for ",_decayed_escapes[idx],_fails[idx]);
print(m);
    m.out("\n");
  }

  // Bump the fails count.  Raising the fails count makes a reversion to
  // stack-allocation happen exponentially less often.
  int f = _fails[idx];
  _fails[idx] = f+1;
  // Add back the threshold, doubled for each prior fail.  Future decays will
  // halve the count again - but the count won't drop below the threshold for
  // 'f' future decays.
  _decayed_escapes[idx] += (SBAEscapeThreshold<<f);
}


// Called every 5 seconds by the WatcherThread.  Decay escape-counts (divide
// by 2).  If the count goes low enough, convert the allocation site back to
// stack allocation - and try stack allocating awhile again.
void SBAPreHeader::decay_escape_sites() {
  for( int idx=0; idx<(1<<SITES_LOG_SIZE); idx++ ) {
    if( _escape_sites[idx] == 0 ) continue;
    int64_t d = (_decayed_escapes[idx]+1)>>1;
    _decayed_escapes[idx] = d; // Decay!
    SBAPreHeader *pre = (SBAPreHeader*)&_escape_sites[idx];
    if( pre->is_moop_hint() && !pre->moop() ) // MethodOop+BCI allocation site
      pre->zap_hint();          // GC has crushed the methd oop info
    if( d <= SBAEscapeThreshold ) { // If decaying to the point of retrying stack allocation
      pre->validate_hint();   // Validate that the nmethod is still there
      // Flip to stack-allocation
      if( pre->is_pc_hint() ) {
        NativeAllocationTemplate *nat = NativeAllocationTemplate::at(nativeCall_before(pre->pc()));
        nat->set_stack();       // Set the allocation site to be stack-only
      }
    }
  }
}

#ifndef PRODUCT
void SBAPreHeader::verify()const{
  int selectors = 0;
  if( is_dead() ) selectors++;
  if( is_forward() ) selectors++;
  if( is_moop_hint() ) selectors++;
  if( is_pc_hint() ) selectors++; 
  if( _ispc == 0 && _moid == 0 ) selectors++; // no hint
  assert0( selectors == 1 );
  if( is_moop_hint() ) {
methodOop m=moop();
    assert0( m->is_oop() );
  }
}

void SBAPreHeader::print()const{
  ResourceMark rm;
  BufferedLoggerMark m( NOTAG, 0, true );
print(m);
}
#endif // !PRODUCT

void SBAPreHeader::print( BufferedLoggerMark &m ) const {
  if( is_dead() ) 
    m.out("[fid:%d dead ]",_fid);
  else if( is_forward() )
    m.out("[forward:%p ]",get_forward().raw_value());
  else if( is_pc_hint() ) {
    m.out("[fid:%d pc:0x%x",_fid,_moid);
CodeBlob*cb=CodeCache::find_blob(pc());
    if( cb ) {
      m.out(" in %s", cb->name());
    }
    m.out(" ]");
  } else if( is_moop_hint() ) {
    m.out("[fid:%d moop:",_fid);
    moop()->print_short_name(m.stream());
    m.out(":%d ]",_bci);
  } else if( _ispc == 0 && _moid == 0 )
    m.out("[fid:%d NO HINT ]", _fid);
  else
    m.out("[BROKEN fid:%d moop:%x bci:%d ]",_fid,_moid,_bci);
}
