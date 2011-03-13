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


#include "assembler_pd.hpp"
#include "barrierSet.hpp"
#include "cardTableModRefBS.hpp"
#include "cardTableRS.hpp"
#include "codeBlob.hpp"
#include "codeCache.hpp"
#include "collectedHeap.hpp"
#include "defNewGeneration.hpp"
#include "deoptimization.hpp"
#include "genRemSet.hpp"
#include "generation.hpp"
#include "gpgc_cardTable.hpp"
#include "gpgc_readTrapArray.hpp"
#include "interpreter_pd.hpp"
#include "methodOop.hpp"
#include "objectRef_pd.hpp"
#include "oopTable.hpp"
#include "os.hpp"
#include "ostream.hpp"
#include "regmask.hpp"
#include "sharedHeap.hpp"
#include "sharedRuntime.hpp"
#include "space.hpp"
#include "vm_version_x86_64.hpp"
#include "universe.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "genOopClosures.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// --- patch_branches
// Patch all branches.  May grow code if we need to flip from short branches to long ones.
void Assembler::patch_branches(){
  patch_branches_impl();
  //static long total_jmps, total_long_jmps, total_labels, total_forward_jmps, total_forward_long_jmps;
  //const int len = _bra_idx.length();
  //total_labels += len;
  //for( int i=0; i<len; i++ ) {
  //  if( _bra_idx.at(i) >= 0 ) { // branch (not label nor alignment)
  //    total_jmps++;
  //    total_labels--;
  //    int op = *((address)_blob+_bra_pcs.at(i));
  //    if( op==0x0F || op==0xE9 ) // big branch opcode?
  //      total_long_jmps++;
  //    if( _bra_pcs.at(i) < _bra_pcs.at(_bra_idx.at(i)) ) { // forward branch
  //      total_forward_jmps++;
  //      if( op==0x0F || op==0xE9 ) // big branch opcode?
  //        total_forward_long_jmps++;
  //    }
  //  }
  //}    
  //if( total_jmps && total_forward_jmps )
  //  tty->print_cr("Assembler: %d jmps, %d long jmps (%d%%), %d forward jmps (%d%%), %d forward long jmps (%d%% of forward jmps), %d labels & aligns", 
  //                total_jmps, 
  //                total_long_jmps, 100*total_long_jmps/total_jmps, 
  //                total_forward_jmps, 100*total_forward_jmps/total_jmps, 
  //                total_forward_long_jmps, 100*total_forward_long_jmps/total_forward_jmps, 
  //                total_labels);
}

static bool isJcc    (int op)           { return (op&0xF0) == 0x70; }
static bool isJccLong(int op1, int op2) { return op1 == 0x0F && isJcc(op2-0x10); }
static bool isJmp    (int op)           { return op == 0xEB; }
static bool isJmpLong(int op)           { return op == 0xE9; }
static bool isJmpMem (int op1, int op2) { return op1 == 0xFF && (op2&(0x7<<3)) == (0x4<<3); }
static bool isRcxJmp (int op)           { return op >= 0xE0 && op <= 0xE3; } // e.g. loopnz, jrcxz ..
static bool isSt8i   (int op1, int op2) { return (op1&0xF8) == 0x48 && op2 == 0xC7; }
static bool isMov8i  (int op)           { return (op&0xF8) == 0xB8; }

// --- patch_branches_impl
// Patch branches to appropriate lables, possibly grow code if we needed.
// This code not only patches branches but respects alignment requests, ...
void Assembler::patch_branches_impl(){
  assert0( _unpatched_branches == true );
  _unpatched_branches = false;
  const int len = _bra_pcs.length(); // Number of branches, labels, alignments to process
  if( len == 0 ) return;
  _bra_pcs.push(_bra_pcs.at(len-1)); // Duplicate the last entry
  ResourceMark rm;

  // value of first recorded pc, branches to before this will never be adjusted
  const int first_pc = _bra_pcs.at(0);

  // All branches have been assembled as short branches so far.
#ifdef ASSERT
  { int last_pc = 0;
    for( int i=0; i<len; i++ ) {
      int pc  = _bra_pcs.at(i); // instruction offset
      int idx = _bra_idx.at(i); // target of branch or meta data
      assert( pc >= first_pc || idx == LABEL, "invalid pc offset" );
      if( idx < 0 ) { // non-branch operation
        switch( idx ) {
        case EOL: assert( idx == EOL, "all labels should be bound before patching");
        case LABEL  : break; // expected target of branch
        case ALIGN8 : break; // expected alignment request
        case ALIGN16: break; // expected alignment request
        case ALIGN32: break; // expected alignment request
        case IC     : break; // expected alignment request
        case CALL   : break; // expected alignment request
default:assert(false,"unexpected index value");
        }
      } else if( pc < _code_ends_data_begins ) { // branch operation
        assert( _bra_idx.at(idx) == LABEL, "branches must refer to labels");
        assert( last_pc == 0 || last_pc < pc, "pc values should be monotically increasing" );
last_pc=pc;
        address ip = (address)_blob+pc;
int op=ip[0];
        int op2 = ip[1];
        if( isSt8i(op,op2) || isMov8i(op) || isJmpMem(op, op2) ) {
          int *dp = (int*)(&ip[isMov8i(op) ? 1 : isSt8i(op, op2) ? 7 : 3]);
assert(*dp==999,"instructions requiring a PC relative offset should have a payload of 999");
        } else if( isJccLong(op,op2) || isJmpLong(op) ) {
          assert( *((int*)(&ip[isJmpLong(op) ? 1 : 2])) == 0, "long branch payload should be 0" );
        } else {
          assert( isJcc(op) || isJmp(op) || isRcxJmp(op), "expected short branch" );
assert(op2==0,"branch payload expected to be 0");
        }
      } else { // data
        address dp = (address)_blob+pc;
        intptr_t val = *((intptr_t*)dp);
        if( val != 999 ) tty->print("Unexpected data payload during branch patching %p at %d (%d)", (void*)val, pc, _code_ends_data_begins );
assert(val==999,"PC relative data payload of 999 expected");
      }
    }
  }
#endif
  // Adjusted pc values
  int apc[len+1];
  for( int i=0; i<len+1; i++ )
    apc[i] = _bra_pcs.at(i);    // Initial value, assuming all branches are short, etc
  // record of instruction growth, initialize to 0 => no growth
  signed char lgs[len];
  memset(lgs,0,len*sizeof(char));

  // Scan for short-vs-long branches
  // This is an O(n^2) implementation and a O(n) one exists... and isn't even
  // all that hard.  But I expect the outer loop to trip only 2 or 3 times,
  // The interpreter has 600 or 700 branches and only trips 3 times.
  int rolling_adjustment;      // extra bytes required for long branches and alignment
  int code_rolling_adjustment=0; // extra bytes required in just the code portion of the code blob (ie pre _code_ends_data_begins)
  { bool progress = true;   // true when there are still potential short branches to make long
    int max_adj[len];       // adjustment associated with alignment, recorded over iterations
                            // so that increases in value can cause recomputation of long branches...
    bzero(max_adj,len*sizeof(int));      // all adjustments are initially zero
    while( progress ) {
progress=false;//terminate loop if no adjustments to size are made
      rolling_adjustment = 0; // accumulated total increase in size to the code blob
for(int i=0;i<len;i++){//For all labels, branches, etc
        // Record adjusted pc for this operation.  Some Labels have PCs that
        // pre-exist this whole region and are never adjusted.  All other PCs
        // must be monotonically increasing and partake of the rolling adjustment.
        int soff = _bra_pcs.at(i);
        apc[i]  = soff <  first_pc ? soff : soff + rolling_adjustment;
        // Calculate if the branch or alignment will adjust size
        int idx  = _bra_idx.at(i);   // What are we looking at?
        assert( soff >= first_pc || idx == LABEL, "invalid pc offset" );
        if( idx == LABEL ) {         // Labels do not need adjusting
        } else if( idx < LABEL ) {   // Alignment request
          int adj;
          if( idx == IC ) {          // Funky inline-cache alignment?
            adj = NativeInlineCache::alignment_padding((address)apc[i]);
          } else if ( idx == CALL ) {// Align the displacement of a call site
            adj = NativeCall::alignment_padding((address)apc[i]);
          } else {                   // Normal power-of-2 alignment
            int x = 1 << -idx;       // mask to align to
            adj = x-1 - ((apc[i]-1)&(x-1)); // round-up amount
          }
          if (adj > max_adj[i]) {
            max_adj[i] = adj;
            progress = true;
          }
          rolling_adjustment += adj; // Will need to pad with this much NOP
        } else {
          // Must be a bound jump to a label.  Check out the size
          assert0( _bra_idx.at(idx) == LABEL ); // branches refer to labels
          int op = ((address)_blob)[soff];      // Branch opcode
          if( lgs[i] != 0 ) {         // Already big?
            rolling_adjustment += lgs[i];
          } else {
            int branch_pc= apc[i]+2; // Offset of instruction after the branch op, if it remains a 2-byte'r
            int label_pc;            // Where the label is (backwards only) or was (last go 'round)
            // If it is a forward branch, then we know that the label_pc will be
            // atleast moved down by 'rolling_adjustment'. If it is a backward branch
            // 'apc[idx]' would be updated before we reach apc[i].
            if ( idx > i ) {
              label_pc = (apc[idx] != _bra_pcs.at(idx)) ? apc[idx] : (_bra_pcs.at(idx) + rolling_adjustment);
            } else {
              label_pc = apc[idx];
            }
            int op2 = ((address)_blob)[soff+1]; // optional secondary opcode byte
            if( isSt8i(op,op2) || isMov8i(op) || isJmpMem(op, op2) || // st8i, mov8i, jmp8 4-byte pc relative immediate do not change size, but need patching
               isJccLong(op,op2) || isJmpLong(op)) { // Already forced large jump?
              // do nothing
            } else if( soff < _code_ends_data_begins ) {
              assert0( isJcc(op) || isJmp(op) || isRcxJmp(op) );
              int d = (label_pc - branch_pc);
              if( !is_int8(d) ) { // more than 1 byte of branch offset
                // Oops, found a short branch that needs to be long
                assert( !isRcxJmp(op), "Jrcx/Loop cannot be expanded" );
                progress = true;    // need to re-check offsets
                int adj = (isJmp(op) ? 5 : 6) -2; // increase in instruction size - branch was recorded as a 2-byte op
                lgs[i] = (char)adj;
                rolling_adjustment += adj;
              }
            } else {
#ifdef ASSERT
              address dp = (address)_blob+soff;
              assert( *((intptr_t*)dp) == 999, "PC holder payload of 999 expected" );
#endif
            }
          }
        }
        if( soff < _code_ends_data_begins )
          code_rolling_adjustment = rolling_adjustment;
      } // end for
    } // while( progress )
    apc[len] = _bra_pcs.at(len) + rolling_adjustment; // record any final length
  }
  // Grow space in the assembler for the longer branches.
  // Might move the underlying _blob.
  grow( rolling_adjustment );

  // Now that we've decided which branches need to be long, slide all code to
  // make space.  Roll backwards though the apc array looking for code that
  // stretched.  Move code backwards.
  { int last = _pc - (address)_blob; // end of last move, initialized to offset of the end of the blob
for(int i=len;i>=1;i--){
      intptr_t opc = _bra_pcs.at(i); // original pc for this branch, label or alignment
      if( opc      < first_pc ) {        // ignore pre-existing Label target
      } else if( false && opc == apc[i] ) {       // early break out: no shuffle needed.
        break;
      } else {                           // shuffle
        // Slight efficiency hack: instead of moving each chunk of code between
        // branch/label points, look to see if the next move (going backwards)
        // has the exact same size.  If so, lump the moves together.
        int this_move_size = apc[i  ]- opc;
        // Find size of move prior to this one
        int next_move_size = 0;
        intptr_t soff = first_pc; // offset start of what to move forward
        intptr_t doff = first_pc; // offset start of destination of move
        for(int j=i-1; j>=1; j--) {
          if( apc[j] >= first_pc ) {
            next_move_size = apc[j]-_bra_pcs.at(j);
soff=_bra_pcs.at(j);
            doff = apc[j];
            break;
          }
        }
        if( this_move_size == next_move_size ) {
          // Skip this move and roll into next
        } else {
          // Now we move the code leading up to this branch/label
          address src = (address)_blob + soff;
          address dst = (address)_blob + doff + (this_move_size-next_move_size);
          intptr_t sz = last - soff;
memmove(dst,src,sz);
last=soff;
        }
      }
    }
  }
  _pc += rolling_adjustment;
  _code_ends_data_begins += code_rolling_adjustment;

  // Patch all branch offsets as needed
  for( int i=0; i<len; i++ ) {
int idx=_bra_idx.at(i);
    if( idx == LABEL ) {        // Labels do not need adjusting
    } else if( idx < LABEL ) {         // Alignment?
      int adj; // amount to adjust by
      if( idx == IC ) {         // Funky inline-cache alignment?
        adj = NativeInlineCache::alignment_padding((address)apc[i]);
      } else if ( idx == CALL ) {// call alignment
        adj = NativeCall::alignment_padding((address)apc[i]);
      } else {                   // Normal power-of-2 alignment
        int x = 1 << -idx;       // mask to align to
        adj = x-1 - ((apc[i]-1)&(x-1)); // round-up amount
      }
      if( adj ) {                // Sometimes get lucky and need no padding
        // Ucky code to emit a proper sized nop instruction
address savepc=_pc;
        _pc = (address)_blob+apc[i];
        nop(adj);
_pc=savepc;
      }
    } else {
      // Not a label, not alignment; must be a branch, mov8i, st8i or PC relative data
      assert( _bra_idx.at(idx) == LABEL, "branch must refer to a label");
      address opp = (address)_blob+apc[i];
int op=opp[0];
      int op2 = opp[1];
      int label_pc = apc[idx];    // Where to branch to
      if( lgs[i] == 0 ) {         // Not an adjusted branch
        // Is it an instruction with a 4 byte pc relative offset that needs patching?
        if( isMov8i(op) || isSt8i(op, op2) || isJmpMem(op, op2) ) {
          int *dp = (int*)(&opp[isMov8i(op) ? 1 : isSt8i(op, op2) ? 7 : 3]);
assert(*dp==999,"expected payload of 999");
          *dp = (intptr_t)((char*)_blob+label_pc); // patch the store, move or table base
        } else if( *((int*)opp) == 999) { // PC relative data
          assert0( apc[i] >= _code_ends_data_begins );
          intptr_t *dp = (intptr_t*)opp;
          intptr_t val = (intptr_t)((char*)_blob + apc[idx]); // calculate adjusted data value from index
          *dp = val;
        } else if( isJmpLong(op) || isJccLong(op,op2) ) { // An already-forced-long branch?
          int branch_pc= apc[i] + (isJmpLong(op) ? 5: 6); // offset of where to branch from (+5 or 6 for size of opcode and displacement)
          int *dp = (int*)&opp[isJmpLong(op) ? 1 : 2]; // Then do a big patch
assert(*dp==0,"expected payload of 0");
          *dp = (label_pc-branch_pc);
        } else {
          assert0( isJcc(op) || isJmp(op) || isRcxJmp(op) );
          // Must be a short branch, conveniently the same length in all cases
          int branch_pc= apc[i]+2; // Where to branch from (+2 for the 2-byte branch op)
          address dp = (address)_blob+branch_pc;
          int d = (label_pc - branch_pc);
          assert( is_int8(d), "miscalculated short branch" );
assert(dp[-1]==0,"expected payload of 0");
          dp[-1] = d;               // patch 1 byte branch
        }
      } else {                 // An adjust branch
        // Need to switch to a short branch to a long-branch opcode
        int branch_pc = apc[i]+lgs[i]; // Offset of original opcode shifted forward
        address ip = (address)_blob+branch_pc;
        int op = ip[0];                // original opcode
        assert0( isJcc(op) || isJmp(op) );
assert(ip[1]==0,"expected payload of 0");
        if( isJmp(op) ) {
          ip = (ip - 5) + 2; // set ip to start of the new instruction
          ip[0] = 0xE9;      // 5-byte long branch
          ip++;              // set to offset location
        } else {
          ip = (ip - 6) + 2; // set ip to start of the new instruction
          ip[0] = 0x0F;      // 6-byte long branch
          ip[1] = op+0x10;
ip+=2;//set to offset location
        }
        int *dp = (int*)ip;
        branch_pc += 2;         // set to next instruction for displacement math
        int d = label_pc - branch_pc; // displacement
        *((int*)dp) = d;     // full int offset (misaligned store is OK)
      }
    }
  } // end for
  if( rolling_adjustment != 0 ) {
    // if something was adjusted, handle all relocations
    bump_rel_pcs(&_relative_relos, apc, true, true);
    if (_debuginfo) bump_rel_pcs(_debuginfo->rel_pcs(), apc, false, false);
    if (_NPEinfo  ) bump_rel_pcs(_NPEinfo  ->rel_pcs(), apc, false, true);
    if (_NPEinfo  ) bump_rel_pcs(_NPEinfo  ->stuffs() , apc, false, true);
    if (_oopmaps  ) bump_rel_pcs(_oopmaps  ->rel_pcs(), apc, false, false);
    if (_deopt_sled_relpc != 0) {
      // need to move deopt sled forward, the end of the blob looks like:
      //   (code)+ [optional align8] (optional data)*
      // the data doesn't change the rolling adjustment, but the last alignment
      // does and it occurs before the _code_ends_data_begins marker, use
      // code_rolling_adjustment to take this into account
      _deopt_sled_relpc += code_rolling_adjustment;
      assert(NativeJump::is_jump_at((address)_blob + _deopt_sled_relpc + 4 /* Size of add8i */), "Deopt sled end not in the right place");
    }
for(int i=1;i<len;i++)//adjust the branch start addresses
      _bra_pcs.at_put(i,apc[i]);
  }
#ifdef ASSERT 
  // assert that all branches marked long are indeed long and those marked short
  // are short.
  { int last_pc = 0;
    for( int i = 0; i < len; i ++) {
      int pc = _bra_pcs.at(i);
      int idx = _bra_idx.at(i); // target of branch or meta data
      assert( pc >= first_pc || idx == LABEL, "invalid pc offset" );
      if( idx < 0 ) { // non-branch operation
        switch( idx ) {
        case EOL: assert( idx == EOL, "all labels should be bound before patching");
        case LABEL  : break; // expected target of branch
        case ALIGN8 : break; // expected alignment request
        case ALIGN16: break; // expected alignment request
        case ALIGN32: break; // expected alignment request
        case IC     : break; // expected alignment request
        case CALL   : break; // expected alignment request
default:assert(false,"unexpected index value");
        }
      } else if( pc < _code_ends_data_begins ) { // branch operation
        assert( _bra_idx.at(idx) == LABEL, "branches must refer to labels");
        assert( last_pc == 0 || last_pc < pc, "pc values should be monotically increasing" );
last_pc=pc;
        address ip = (address)_blob+pc;
int op=ip[0];
        int op2 = ip[1];
        assert( isSt8i(op,op2) || isMov8i(op) || isJmpMem(op, op2) ||
                isJccLong(op,op2) || isJmpLong(op) ||
                isJcc(op) || isJmp(op) || isRcxJmp(op), "unexpected branch opcode" );
        int branch_pc = pc+2;
        int label_pc = _bra_pcs.at(idx);
        int distance = (label_pc - branch_pc);
        if( is_int8(distance) && lgs[i] != 0 ) { /* short marked long */
        // Cool idea, but must weaken this assert.  It's legit AND POSSIBLE for
        // a branch to be forced long during one or more expansion passes, only
        // to end up being able to fit as a short op.  It all depends on the
        // alignment nops getting inserted.  Basically, the problem of picking
        // optimal branch offsets with alignment ops is NOT monotonic, and does
        // not have a "single best solution" and probably does not have a linear
        // time optimal solution.  So sometimes this heuristic loses out.
        //assert0( false );
        } else if ( !is_int8(distance) && lgs[i] == 0) { /* long marked short */
          assert0( !isJcc(op) && !isJmp(op) && !isRcxJmp(op) );
        }
      } else { // data
        intptr_t data = *((intptr_t*)((address)_blob+pc));
        assert( _blob->contains((void*)data), "PC relative payload expected to resolve within code blob" );
      }
    }
  }
#endif
}

// --- bump_rel_pcs
// Move all relocations to account for expanded branch ops.  Ties around
// alignment ops are more interesting: sometimes we want the relocations to
// follow the inserted nops and sometimes we want the relocations to stay
// before the nops.  Labels & alignment ops are the canonical obvious easy
// case: if the alignment is after a Label, then the nops come after the Label
// and similarly if the alignment is before the Label the nops come before.
//
// If an alignment op ties with debug info (e.g., follows a 'call') we want
// any inserted nops to leave the debug info tight after the call: the
// nops come after the debug info.
// 
// If an alignment op ties with null-pointer info (e.g. a memory op which
// might take a hardware NPE), then the NPE offset needs to stick with the
// faulting instruction: the nops come before the NPE info.
//
// If skip_alignment is set on encountering relocation due to alignment, we
// skip bumping.  Not true for _debuginfo and _oopmaps that are info held
// before the nops added for alignment.  
void Assembler::bump_rel_pcs(GrowableArray<intptr_t> *rel_pcs, int *apc, bool adjust_bra, bool skip_alignment) {
  int j=0;
if(rel_pcs==NULL)return;
for(int i=0;i<rel_pcs->length();i++){
int rel_pc=rel_pcs->at(i);
    if( i==0 || rel_pcs->at(i-1) > rel_pc ) j=0;
    while( j < _bra_pcs.length()-1 && _bra_pcs.at(j) < rel_pc )
j++;//account for sliding things around

    if ( skip_alignment &&
         _bra_pcs.at(j)==rel_pc && 
         _bra_idx.at(j)<LABEL && 
         j+1 < _bra_pcs.length()-1 && 
         _bra_pcs.at(j+1)==rel_pc ) {
      assert0( _bra_idx.at(j+1) >= LABEL ); // 2 alignments in a row
      j++;
    }
    int slide = j>0?(apc[j]-_bra_pcs.at(j)):0;
    rel_pc += slide;
rel_pcs->at_put(i,rel_pc);
    if (adjust_bra) {
      int *pc = (int*)((char*)_blob+rel_pc);
      *pc += -slide;              // adjust relative offset by the slide
      assert0( !_blob->contains((address)(*pc+pc+4)) ); // expecting it to be a relative value OUTSIDE the blob
    }
  }
}

// --- grow_impl
// Just forward to the base grow_impl, but adjust the relative_relos
// if the underlying blob moves.
void Assembler::grow_impl( int sz ) {
  CodeBlob *old = _blob;
  CommonAsm::grow_impl(sz);     // Forward to the base grow_impl
  // See if the underlying blob moved and we have relative_relos?
  if( old != _blob && _relative_relos.length() ) {
    // The relative_relos are now all messed up, because they are
    // relative to the blob and the blob moved.  Fix 'em.
    int delta = (intptr_t)old - (intptr_t)_blob;
for(int i=0;i<_relative_relos.length();i++){
      int* disp = (int*)((address)_blob+_relative_relos.at(i));
      *disp += delta;
    }
  }
}


// --- reset_branches
void Assembler::reset_branches(){
  CommonAsm::reset_branches();
  if( !_movable ) { // If not movable, reset relos as well - no more code sliding
_relative_relos.clear();
  }
}

// --- has_variant_branches
bool Assembler::has_variant_branches() const {
  for( int i=0; i<_bra_idx.length(); i++ )
    if( _bra_idx.at(i) > LABEL )
      return true;
  return false;
}

// --- xjmp
// Record a variant-size jump
void Assembler::xjmp( int opx, Label &L ) {
  L.add_jmp(this);
  emit1(opx);                   // short-branch variant
  emit1(0x00);                  // no offset yet
}

// --- yjmp
// Record 1-byte limit jump
void Assembler::yjmp( int opx, Label &L, const char *f, int l ) {
  L.add_jmp(this);
  emit1(opx);                   // short-branch variant
  emit1(0x00);                  // no offset yet
  // Record f & l for length failure?
}

// --- jmp
// Record a forced-big jmp
void Assembler::jmp2(Label&L){
  L.add_jmp(this);
  emit1(0xE9);
  emit4(0x00);                  // no offset yet
}

// --- jmp
// Record a forced-big jmp
void Assembler::jne2(Label&L){
  L.add_jmp(this);
  emit1(0x0F);
  emit1(0x85);
  emit4(0x00);                  // no offset yet
}

// --- st8i
// Store the PC as an immediate value.
void Assembler::st8i( Register base, int off, Label &thepc ) {
  thepc.add_jmp(this);
  st8i(base, off, 999);
}

// --- bind
// Bind to a distant address in the same blob
void Assembler::bind( Label &L, address adr ) {
  assert0( _blob->contains(adr) );
  L.bind(this, adr - (address)_blob);
}

void Assembler::addr_nop_4() {
  // 4 bytes: NOP DWORD PTR [EAX+0]
  emit1(0x0F);
  emit1(0x1F);
emit1(0x40);//emit_rm(cbuf, 0x1, EAX_enc, EAX_enc);
emit1(0);//8-bits offset (1 byte)
}

void Assembler::addr_nop_5() {
  // 5 bytes: NOP DWORD PTR [EAX+EAX*0+0] 8-bits offset 
  emit1(0x0F);
  emit1(0x1F);
emit1(0x44);//emit_rm(cbuf, 0x1, EAX_enc, 0x4);
emit1(0x00);//emit_rm(cbuf, 0x0, EAX_enc, EAX_enc);
emit1(0);//8-bits offset (1 byte)
}

void Assembler::addr_nop_7() {
  // 7 bytes: NOP DWORD PTR [EAX+0] 32-bits offset
  emit1(0x0F);
  emit1(0x1F);
emit1(0x80);//emit_rm(cbuf, 0x2, EAX_enc, EAX_enc);
emit4(0);//32-bits offset (4 bytes)
}

void Assembler::addr_nop_8() {
  // 8 bytes: NOP DWORD PTR [EAX+EAX*0+0] 32-bits offset
  emit1(0x0F);
  emit1(0x1F);
emit1(0x84);//emit_rm(cbuf, 0x2, EAX_enc, 0x4);
emit1(0x00);//emit_rm(cbuf, 0x0, EAX_enc, EAX_enc);
emit4(0);//32-bits offset (4 bytes)
}

void Assembler::nop(int i) {
  assert(i > 0, " ");
//  if (UseAddressNop && VM_Version::is_intel()) {
    //
    // Using multi-bytes nops "0x0F 0x1F [address]" for Intel
    //  1: 0x90
    //  2: 0x66 0x90
    //  3: 0x66 0x66 0x90 (don't use "0x0F 0x1F 0x00" - need patching safe padding)
    //  4: 0x0F 0x1F 0x40 0x00
    //  5: 0x0F 0x1F 0x44 0x00 0x00
    //  6: 0x66 0x0F 0x1F 0x44 0x00 0x00
    //  7: 0x0F 0x1F 0x80 0x00 0x00 0x00 0x00
    //  8: 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00
    //  9: 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00
    // 10: 0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00
    // 11: 0x66 0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00

    // The rest coding is Intel specific - don't use consecutive address nops

    // 12: 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00 0x66 0x66 0x66 0x90
    // 13: 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00 0x66 0x66 0x66 0x90
    // 14: 0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00 0x66 0x66 0x66 0x90
    // 15: 0x66 0x66 0x66 0x0F 0x1F 0x84 0x00 0x00 0x00 0x00 0x00 0x66 0x66 0x66 0x90

    while(i >= 15) {
      // For Intel don't generate consecutive addess nops (mix with regular nops)
      i -= 15;
emit1(0x66);//size prefix
emit1(0x66);//size prefix
emit1(0x66);//size prefix
      addr_nop_8();
emit1(0x66);//size prefix
emit1(0x66);//size prefix
emit1(0x66);//size prefix
emit1(0x90);//nop
    }
    switch (i) {
      case 14:
emit1(0x66);//size prefix
      case 13:
emit1(0x66);//size prefix
      case 12:
        addr_nop_8();
emit1(0x66);//size prefix
emit1(0x66);//size prefix
emit1(0x66);//size prefix
emit1(0x90);//nop
        break;
      case 11:
emit1(0x66);//size prefix
      case 10:
emit1(0x66);//size prefix
      case 9:
emit1(0x66);//size prefix
      case 8:
        addr_nop_8();
        break;
      case 7:
        addr_nop_7();
        break;
      case 6:
emit1(0x66);//size prefix
      case 5:
        addr_nop_5();
        break;
      case 4: 
        addr_nop_4();
        break;
      case 3: 
        // Don't use "0x0F 0x1F 0x00" - need patching safe padding
emit1(0x66);//size prefix
      case 2:
emit1(0x66);//size prefix
      case 1:
emit1(0x90);//nop
        break;
      default:
        assert(i == 0, " ");
    }
    return;

  // Using nops with size prefixes "0x66 0x90".
  // From AMD Optimization Guide:
  //  1: 0x90
  //  2: 0x66 0x90
  //  3: 0x66 0x66 0x90
  //  4: 0x66 0x66 0x66 0x90
  //  5: 0x66 0x66 0x90 0x66 0x90
  //  6: 0x66 0x66 0x90 0x66 0x66 0x90
  //  7: 0x66 0x66 0x66 0x90 0x66 0x66 0x90
  //  8: 0x66 0x66 0x66 0x90 0x66 0x66 0x66 0x90
  //  9: 0x66 0x66 0x90 0x66 0x66 0x90 0x66 0x66 0x90
  // 10: 0x66 0x66 0x66 0x90 0x66 0x66 0x90 0x66 0x66 0x90
  // 
  while(i > 12) {
    i -= 4;
emit1(0x66);//size prefix
    emit1(0x66);
    emit1(0x66);
emit1(0x90);//nop
  }
  // 1 - 12 nops
  if(i > 8) {
    if(i > 9) {
      i -= 1;
      emit1(0x66);
    }
    i -= 3;
    emit1(0x66);
    emit1(0x66);
    emit1(0x90);
  }
  // 1 - 8 nops
  if(i > 4) {
    if(i > 6) {
      i -= 1;
      emit1(0x66);
    }
    i -= 3;
    emit1(0x66);
    emit1(0x66);
    emit1(0x90);
  }
  switch (i) {
    case 4:
      emit1(0x66);
    case 3:
      emit1(0x66);
    case 2:
      emit1(0x66);
    case 1:
      emit1(0x90);
      break;
    default:
      assert(i == 0, " ");
  }
}

// --- mov8, load a 32-bit constant pointer, zero extended
void Assembler::mov8i( Register r, const char *ptr ) {
  uint32_t x = (uint32_t)(intptr_t)ptr;
  if( (const char *)x == ptr ) { // pointer must be 32 bits
    mov8u(r,x);
  } else {
    mov8i(r,(intptr_t)ptr);
  }
}

// --- mov8i, load a 32-bit PC
void Assembler::mov8i( Register dst, Label &thepc ) {
  thepc.add_jmp(this);
  dst = emit_regprefix(dst);    // Optional prefix byte (ugh)
  emit1(0xB8|dst);
  emit4(999);                   // filler immediate until patching
}

// --- mov8i, load a 64-bit value
void Assembler::mov8i(Register dst,  int64_t simm, bool can_destroy_flags ) {
  if        ( is_int32(simm) )   { // sign extendable 32-bit value
    mov8i(dst,(int32_t)simm, can_destroy_flags);
  } else if ( (simm >> 32) == 0) { // high 32bits are 0
    mov8u(dst,(uint32_t)simm, can_destroy_flags);
  } else {                         // full 64bit immediate
emit_regprefix8(dst);
    emit1(0xB8|dst);
    emit8(simm);
  }
}

// --- mov8i, load a 32-bit value
void Assembler::mov8i(Register dst,  int32_t simm, bool can_destroy_flags ) {
  if( simm >= 0 ) { // Zero-extended forms have a shorter syntax
    mov8u(dst,(uint32_t)simm, can_destroy_flags);
  } else {
    // Need a 64-bit prefix
emit_regprefix8(dst);
    emit1(0xC7);
    emit_opreg(0, dst);
    emit4(simm);
  }
}

// --- mov8u, load a 32-bit value
void Assembler::mov8u(Register dst, uint32_t zimm, bool can_destroy_flags ) { 
  if( can_destroy_flags && zimm == 0 ) {
    xor4(dst,dst);              // 32-bit xor - by default will zero-extends to 64 bits
    return;
  }
  // Zero-extended to 64 bits by default
  dst = emit_regprefix(dst);
  emit1(0xB8|dst);
  emit4(zimm); 
}

// --- emit_regbaseoff
// Emit the SIB byte with 'reg' for base+offset addressing mode.
// Optimize for off==0, is_int8(off), and is_int32(off).
// No relocations or pc-relative offsets or 64-bit offsets.
void Assembler::emit_regbaseoff( Register reg, Register base, intptr_t off ) {
  assert0( reg < 8 && base < 8 );  // already accounted for prefixes on these
  assert0( reg != noreg );
  assert0(is_int32(off));
  if( base == noreg ) {         // offset-no-base mode?
    emit1(0x00 | reg << 3 | 4 );// modrm:: 00 src  100 (has SIB)
    emit1(0x25);                // SIB: 00 scale, 100 (no index)101 (no base)
    emit4(off);                 // address
  } else if( off == 0 && base != RBP ) { // No offset - reg, [base]
    // NB in the case of RBP a 0byte displacement must be used
    emit1(0x00 | reg << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
  } else if( is_int8(off) ) {      // 8-bit offset - reg, [base]+off8
    emit1(0x40 | reg << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
    emit1(off);
  } else {                         // 32-bit offset - reg, [base]+off32
    emit1(0x80 | reg << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
    emit4(off);
  }
}

// --- emit_opbaseoff
// Emit the SIB byte with op and base+offset addressing mode.
// Optimize for off==0, is_int8(off), and is_int32(off).
// No relocations or pc-relative offsets or 64-bit offsets.
void Assembler::emit_opbaseoff( int op, Register base, intptr_t off ) {
  assert0( op < 8 && base < 8 );  // already accounted for prefixes on these
  assert0(is_int32(off));
  if( base == noreg ) {         // offset-no-base mode?
    emit1(0x00 | op << 3 | 4 ); // modrm:: 00 src  100 (has SIB)
    emit1(0x25);                // SIB: 00 scale, 100 (no index)101 (no base)
    emit4(off);                 // address
  } else if( off == 0 && base != RBP ) { // No offset - reg, [base]
    // NB in the case of RBP a 0byte displacement must be used
    emit1(0x00 | op << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
  } else if( is_int8(off) ) {      // 8-bit offset - reg, [base]+off8
    emit1(0x40 | op << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
    emit1(off);
  } else {                         // 32-bit offset - reg, [base]+off32
    emit1(0x80 | op << 3 | base);
    if( base == RSP ) emit1(0x24); // RSP requires a SIB byte 00 100 100
    emit4(off);
  }
}

// --- emit_opbaseoffindexscale
// Emit the SIB byte with op and base+offset+index<<shift addressing mode.
// No Optimizations yet.
// No relocations or pc-relative offsets or 64-bit offsets.
void Assembler::emit_opbaseoffindexscale( int op, Register base, intptr_t off, Register index, int scale, int prefix ) {
  assert0( op < 8 && base < 8 && index < 8 );  // already accounted for prefixes on these
  assert0( scale >=0 && scale <=3 );
  assert0( index != RSP || (prefix&REX_X)==REX_X ); // RSP is not allowed as an index, but R12 IS
  assert0(is_int32(off));
  assert0( base != noreg );

if(index==noreg){
    emit_opbaseoff( op, base, off );
  } else {  
    if( off == 0 && base != RBP ) { // No offset - reg, [base+index<<scale]
      // NB in the case of RBP a 0byte displacement must be used
      emit1(0x00 | op << 3 | RSP);
      emit1(scale<<6 | index<<3 | base);
    } else if( is_int8(off) ) {      // 8-bit offset - reg, [base+index<<scale]+off8
      emit1(0x40 | op << 3 | RSP);
      emit1(scale<<6 | index<<3 | base);
      emit1(off);
    } else {                         // 32-bit offset - reg, [base+index<<scale]+off32
      assert0(is_int32(off));
      emit1(0x80 | op << 3 | RSP);
      emit1(scale<<6 | index<<3 | base);
      emit4(off);
    }
  }
}


// --- emit_fancy
// Emit the SIB byte with 'reg' for base+offset+index<<shift addressing mode.
// Optimize for off==0, is_int8(off), and is_int32(off).
// No relocations or pc-relative offsets or 64-bit offsets.
void Assembler::emit_fancy( Register reg, Register base, intptr_t off, Register index, int shift, int prefix ) {
  assert0( reg < 8 && base < 8 && index < 8); // already accounted for prefixes on these
  assert0( shift >=0 && shift <=3 );
  assert0( index != RSP || (prefix&REX_X)==REX_X ); // RSP is not allowed as an index, but R12 IS
  assert0( reg != noreg );

if(index==noreg){
    emit_regbaseoff( reg, base, off );
  } else {
    if( base == noreg ) {                  // No base - reg, [index<<shift]+off32
      assert0(is_int32(off));
      // NB the 00/no displacement modifier is overloaded for this and the following case
      emit1(0x00 | reg << 3 | RSP);
      emit1(shift<<6 | index<<3 | RBP);
      emit4(off);
    } else if( off == 0 && base != RBP ) { // No offset - reg, [base+index<<shift]
      // NB in the case of RBP a 0byte displacement must be used
      emit1(0x00 | reg << 3 | RSP);
      emit1(shift<<6 | index<<3 | base);
    } else if( is_int8(off) ) {            // 8-bit offset - reg, [base+index<<shift]+off8
      emit1(0x40 | reg << 3 | RSP);
      emit1(shift<<6 | index<<3 | base);
      emit1(off);
    } else {                               // 32-bit offset - reg, [base+index<<shift]+off32
      assert0(is_int32(off));
      emit1(0x80 | reg << 3 | RSP);
      emit1(shift<<6 | index<<3 | base);
      emit4(off);
    }
  }
}

// --- int_reg4i
// Common int op reg w/immediate
void Assembler::int_reg4i(Register dst, intptr_t imm32, int op, int opax) { 
  assert0( is_int32(imm32) );
  if( dst == RAX && !is_int8(imm32) ) {
    emit1(opax); emit4(imm32);
  } else {
    emit_regprefix4(dst); // REX_B as needed.
    if( is_int8(imm32) ) { emit1(0x83); emit_opreg(op,dst);  emit1(imm32); }
    else                 { emit1(0x81); emit_opreg(op,dst);  emit4(imm32); }
  }
}

// --- int_reg8i
// Common int op reg w/immediate
void Assembler::int_reg8i(Register dst, intptr_t imm32, int op, int opax) { 
  assert0( is_int32(imm32) );
  if( dst == RAX && !is_int8(imm32) ) {
    emit1(REX_W); emit1(opax); emit4(imm32);
  } else {
    emit_regprefix8(dst); // emit REX_W always, but also REX_B as needed.
    if( is_int8(imm32) ) { emit1(0x83); emit_opreg(op,dst);  emit1(imm32); }
    else                 { emit1(0x81); emit_opreg(op,dst);  emit4(imm32); }
  }
}

// --- int_mem4i
// Common int op 32-bit memory w/immediate
void Assembler::int_mem4i(Register base, int off, intptr_t imm32, int op)  {
  assert0( is_int32(imm32) );
emit_regprefix4(base);
  if( is_int8(imm32) ) { emit1(0x83); emit_regbaseoff(Register(op),base,off);  emit1(imm32); } 
  else                 { emit1(0x81); emit_regbaseoff(Register(op),base,off);  emit4(imm32); }
}

// --- int_mem8i
// Common int op 64-bit memory w/immediate
void Assembler::int_mem8i(Register base, int off, intptr_t imm32, int op)  {
  assert0( is_int32(imm32) );
emit_regprefix8(base);
  if( is_int8(imm32) ) { emit1(0x83); emit_regbaseoff(Register(op),base,off);  emit1(imm32); } 
  else                 { emit1(0x81); emit_regbaseoff(Register(op),base,off);  emit4(imm32); }
}

// --- int_mem4i
// Common int op 32-bit complex memory w/immediate
void Assembler::int_mem4i(Register base, int off, Register index, int scale, intptr_t imm32, int op)  {
  assert0( is_int32(imm32) );
  Register none = Register(op);
  int prefix = emit_regprefix4(base,none,index);
  if( is_int8(imm32) ) { emit1(0x83); emit_fancy(Register(op),base,off,index,scale,prefix);  emit1(imm32); } 
  else                 { emit1(0x81); emit_fancy(Register(op),base,off,index,scale,prefix);  emit4(imm32); }
}

// --- int_mem8i
// Common int op 64-bit complex memory w/immediate
void Assembler::int_mem8i(Register base, int off, Register index, int scale, intptr_t imm32, int op)  {
  assert0( is_int32(imm32) );  
  Register none = Register(op);
  int prefix = emit_regprefix8(base,none,index);
  if( is_int8(imm32) ) { emit1(0x83); emit_fancy(Register(op),base,off,index,scale,prefix);  emit1(imm32); } 
  else                 { emit1(0x81); emit_fancy(Register(op),base,off,index,scale,prefix);  emit4(imm32); }
}

// --- not4 w/complex address FROM MEMORY
void Assembler::not4(Register base, intptr_t off, Register index, int scale) {
  Register none = Register(2);
  int prefix = emit_regprefix4(base,none,index);
  emit1(0xF7);
  emit_fancy(Register(0x2),base,off,index,scale,prefix);
}

// --- not8 w/complex address FROM MEMORY
void Assembler::not8(Register base, intptr_t off, Register index, int scale) {
  Register none = Register(2);
  int prefix = emit_regprefix8(base,none,index);
  emit1(0xF7);
  emit_fancy(Register(0x2),base,off,index,scale,prefix);
}

// --- xor4i
void Assembler::xor4i(Register dst, long imm32) {
  if (imm32 == -1) { not4(dst); }
  else             { int_reg4i(dst, imm32, 6, 0x35); }
}

// --- xor8i
void Assembler::xor8i(Register dst, long imm32) {
  if (imm32 == -1) { not8(dst); }
  else             { int_reg8i(dst, imm32, 6, 0x35); }
}

// --- xor4i
void Assembler::xor4i(Register base, int off, long imm32) {
  if (imm32 == -1) { not4(base, off); }
  else             { int_mem4i(base,off,imm32,6); }
}

// --- xor8i
void Assembler::xor8i(Register base, int off, long imm32) {
  if (imm32 == -1) { not8(base, off); }
  else             { int_mem8i(base,off,imm32,6); }
}

// --- xor4i w/complex address FROM MEMORY
void Assembler::xor4i(Register base, int off, Register index, int scale, long imm32) {
  if (imm32 == -1) { not4(base, off, index, scale); }
  else             { int_mem4i(base,off,index,scale,imm32,6); }
}

// --- xor8i w/complex address FROM MEMORY
void Assembler::xor8i(Register base, int off, Register index, int scale, long imm32) {
  if (imm32 == -1) { not8(base, off, index, scale); }
  else             { int_mem8i(base,off,index,scale,imm32,6); }
}

// --- mul4i
// dst = src*imm32
void Assembler::mul4i(Register dst, Register src, long imm32) {
  assert0( is_int32(imm32) );
emit_regprefix4(src,dst);
  if ( is_int8(imm32) ) { emit1(0x6B); emit_regreg(dst,src); emit1(imm32); }
  else                  { emit1(0x69); emit_regreg(dst,src); emit4(imm32); }
}

// --- mul8i
// dst = src*imm32
void Assembler::mul8i(Register dst, Register src, long imm32) {
  assert0( is_int32(imm32) );
emit_regprefix8(src,dst);
  if ( is_int8(imm32) ) { emit1(0x6B); emit_regreg(dst,src); emit1(imm32); }
  else                  { emit1(0x69); emit_regreg(dst,src); emit4(imm32); }
}


// --- store up to a 1-byte immediate into 1-byte memory
void Assembler::st1i(Register base, int off, int imm ) {
  assert0( is_int8(imm) || is_uint8(imm) );
  base = emit_regprefix(base);
  emit1(0xC6); 
  emit_regbaseoff(Register(0),base,off);  
  emit1(imm);
}

// --- store up to a 1-byte immediate into 1-byte memory
void Assembler::st1i(Register base, int off, Register index, int scale, int imm ) {
  Register dummy = RAX;
  int prefix = emit_regprefix1(base,dummy,index);
  emit1(0xC6); 
  emit_fancy(Register(0),base,off,index,scale,prefix);
  emit1(imm);
}

// --- store up to a 1-byte immediate into 1-byte memory
void Assembler::st1i(Register base, address off, int imm ) {
  int ioff = (int)(intptr_t)off;
  assert0( (address)ioff == off );
  st1i(base,ioff,imm);
}

// --- store up to a 2-byte immediate into 2-byte memory
void Assembler::st2i(Register base, int off, int imm ) {
  assert0( is_int16(imm) );
emit_regprefix2(base);
  emit1(0xC7); 
  emit_regbaseoff(Register(0),base,off);  
  emit2(imm);
}

// --- store up to a 1-byte immediate into 1-byte memory
void Assembler::st2i(Register base, int off, Register index, int scale, int imm ) {
  Register none = Register(0);
  int prefix = emit_regprefix2(base,none,index); 
  emit1(0xC7); 
  emit_fancy(Register(0),base,off,index,scale,prefix);
  emit2(imm);
}

// --- store up to a 4-byte immediate into 4-byte memory
void Assembler::st4i(Register base, int off, long imm32 ) {
  assert0( is_int32(imm32) );
emit_regprefix4(base);
  emit1(0xC7); 
  emit_regbaseoff(Register(0),base,off);  
emit4(imm32);
}

// --- store up to a 1-byte immediate into 4-byte memory
void Assembler::st4i(Register base, int off, Register index, int scale, long imm32 ) {
  Register none = Register(0);
  int prefix = emit_regprefix4(base,none,index); 
  emit1(0xC7); 
  emit_fancy(Register(0),base,off,index,scale,prefix);
emit4(imm32);
}

// --- store up to a 4-byte immediate into 8-byte memory
void Assembler::st8i(Register base, int off, long imm32 ) {
  assert0( is_int32(imm32) );
emit_regprefix8(base);
  emit1(0xC7); 
  emit_regbaseoff(Register(0),base,off);  
emit4(imm32);
}

// --- store up to a 4-byte immediate into 8-byte memory
void Assembler::st8i(Register base, int off, Register index, int scale, long imm32 ) {
  assert0( is_int32(imm32) );
  Register none = Register(0);
  int prefix = emit_regprefix8(base,none,index); 
  emit1(0xC7); 
  emit_fancy(Register(0),base,off,index,scale,prefix);
emit4(imm32);
}

// --- pushi
// Push an immediate value onto the stack
void Assembler::pushi( long imm32 ) {
if(is_int8(imm32)){
    emit1(0x6A); emit1(imm32);
  } else if( is_int16(imm32) ) {
    emit1(0x66); emit1(0x6B); emit2(imm32);
  } else {
    assert0( is_int32(imm32) );
    emit1(0x68); emit4(imm32);
  }
}


// --- ldz1 w/complex address
void Assembler::ldz1(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index); // No need for wide-prefix, despite 64-bit result
  emit2(0xB60F);                                // opcodes specify 1-byte load
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- lds1 w/complex address
void Assembler::lds1(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index); // Must be wide to sign-extend across
  emit2(0xBE0F);                                // opcodes specify 1-byte load
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- ldz2 w/complex address
void Assembler::ldz2(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index); // No need for wide-prefix, despite 64-bit result
  emit2(0xB70F);                                // opcodes specify 2-byte load
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- lds2 w/complex address
void Assembler::lds2(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index); // Must be wide to sign-extend across
  emit2(0xBF0F);                                // opcodes specify 2-byte load
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- ldz4 w/complex address
void Assembler::ldz4(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x8B);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- lds4 w/complex address
void Assembler::lds4(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x63);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- ld4 w/complex address
void Assembler::ld4(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x8B);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- ld8 w/complex address
void Assembler::ld8(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x8B);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- ld8 w/absolute address
void Assembler::ld8(Register dst,address low_adr){
  if( dst != RAX || is_int32((intptr_t)low_adr) ) {
    assert( is_int32((intptr_t)low_adr), "only RAX can handle a moffs64" );
Register dummy=noreg;
    emit_regprefix8(dummy,dst);
    emit1(0x8B);
    emit1(0x04|dst<<3);
    emit1(0x25);
    emit4((intptr_t)low_adr);
  } else {
Register dummy=noreg;
    emit_regprefix8(dummy);
    emit1(0xA1);
    emit8((intptr_t)low_adr);
  }
}

// --- st1 w/complex address
void Assembler::st1(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix_bytereg(base,src,index);
  emit1(0x88); 
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- st2 w/complex address
void Assembler::st2(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix2(base,src,index); 
  emit1(0x89); 
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- st4 w/complex address
void Assembler::st4(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index); 
  emit1(0x89); 
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- st8 w/complex address
void Assembler::st8(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index); 
  emit1(0x89); 
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- st8 w/absolute address
void Assembler::st8(address low_adr,Register src){
  if( src != RAX || is_int32((intptr_t)low_adr) ) {
    assert( is_int32((intptr_t)low_adr), "only RAX can handle a moffs64" );
Register dummy=noreg;
    emit_regprefix8(dummy,src);
    emit1(0x89);
    emit1(0x04|src<<3);
    emit1(0x25);
    emit4((intptr_t)low_adr);
  } else {
Register dummy=noreg;
    emit_regprefix8(dummy);
    emit1(0xA3);
    emit8((intptr_t)low_adr);
  }
}

// --- lea w/complex address
void Assembler::lea4(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x8D);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- lea w/complex address
void Assembler::lea(Register dst,  Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x8D);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- cas8 w/complex address
void Assembler::cas8(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x0F);
  emit1(0xB1);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- cas4 w/complex address
void Assembler::cas4(Register base, int off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x0F);
  emit1(0xB1);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- movsx84
void Assembler::movsx84(Register dst,Register src){
  if( (dst != src) || (dst != RAX) ) {
emit_regprefix8(src,dst);
    emit1(0x63  );
emit_regreg(dst,src);
  } else {
    emit1(REX_W); // cheaper encoding for RAX
    emit1(0x98);
  }
}

// --- movsx42
void Assembler::movsx42(Register dst,Register src){
  if( (dst != src) || (dst != RAX) ) {
emit_regprefix4(src,dst);
    emit2(0xBF0F);
emit_regreg(dst,src);
  } else {
    emit1(0x98); // cheaper encoding for RAX
  }
}

// --- add4 w/complex address FROM MEMORY
void Assembler::add4(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x03);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- add4 w/complex address TO MEMORY
void Assembler::add4(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x01);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- neg4 w/complex address FROM MEMORY
void Assembler::neg4(Register base, intptr_t off, Register index, int scale) {
  Register none = Register(3);
  int prefix = emit_regprefix4(base,none,index);
  emit1(0xF7);
  emit_fancy(Register(0x3),base,off,index,scale,prefix);
}

// --- neg8 w/complex address FROM MEMORY
void Assembler::neg8(Register base, intptr_t off, Register index, int scale) {
  Register none = Register(3);
  int prefix = emit_regprefix8(base,none,index);
  emit1(0xF7);
  emit_fancy(Register(0x3),base,off,index,scale,prefix);
}

// --- mul4 w/complex address FROM MEMORY
void Assembler::mul4(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x0F);
  emit1(0xAF);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- mul8 w/complex address FROM MEMORY
void Assembler::mul8(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x0F);
  emit1(0xAF);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- mul_4 w/complex address FROM MEMORY
void Assembler::mul4i(Register dst, Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x69); // mul4
  emit_fancy(dst,base,off,index,scale,prefix);
emit4(imm32);
}

// --- mul_8 w/complex address FROM MEMORY
void Assembler::mul8i(Register dst, Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x69); // mul8
  emit_fancy(dst,base,off,index,scale,prefix);
emit4(imm32);
}

// --- or_4 w/complex address FROM MEMORY
void Assembler::or_4(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x0b);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- or_4 w/complex address TO MEMORY
void Assembler::or_4(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x09);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- or_4 w/complex address TO MEMORY
void Assembler::or_4i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(1);  // or4
  int prefix = emit_regprefix4(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // or4
  emit_fancy(Register(1),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- or_8 w/complex address TO MEMORY
void Assembler::or_8i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(1);  // or8
  int prefix = emit_regprefix8(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // or8
  emit_fancy(Register(1),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- and8 w/complex address TO MEMORY
void Assembler::and8i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(4);  // and8
  int prefix = emit_regprefix8(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // and8
  emit_fancy(Register(4),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- and4 w/complex address TO MEMORY
void Assembler::and4i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(4);  // and4
  int prefix = emit_regprefix4(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // and4
  emit_fancy(Register(4),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- xor4 w/complex address FROM MEMORY
void Assembler::xor4(Register dst, Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x33);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- xor4 w/complex address TO MEMORY
void Assembler::xor4(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x31);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- and4 w/complex address TO MEMORY
void Assembler::and4(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x21);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- add8 w/address TO MEMORY
void Assembler::add8(Register base, intptr_t off, Register src) {
  emit_regprefix8(base,src);
  emit1(0x01);
  emit_regbaseoff(src,base,off);
}

// --- add8 w/complex address TO MEMORY
void Assembler::add8(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x01);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- sub8 w/complex address TO MEMORY
void Assembler::sub8(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x29);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- or_8 w/complex address TO MEMORY
void Assembler::or_8(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x09);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- or_8 w/complex address FROM MEMORY
void Assembler::or_8(Register dst, Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x0B);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- xor8 w/complex address FROM MEMORY
void Assembler::xor8(Register dst, Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x33);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- xor8 w/complex address TO MEMORY
void Assembler::xor8(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x31);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- and8 w/complex address TO MEMORY
void Assembler::and8(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x21);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- add8 w/complex address FROM MEMORY
void Assembler::add8(Register dst, Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x03);
  emit_fancy(dst,base,off,index,scale,prefix);
}

void Assembler::add4i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(0);  // add4
  int prefix = emit_regprefix4(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // add4
  emit_fancy(Register(0),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

void Assembler::add8i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(0);  // add4
  int prefix = emit_regprefix8(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // add4
  emit_fancy(Register(0),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- and4 w/complex address FROM MEMORY
void Assembler::and4(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix4(base,dst,index);
  emit1(0x23);
  emit_fancy(dst,base,off,index,scale,prefix);
}
void Assembler::and8(Register dst,  Register base, intptr_t off, Register index, int scale) {
  int prefix = emit_regprefix8(base,dst,index);
  emit1(0x23);
  emit_fancy(dst,base,off,index,scale,prefix);
}

// --- jmp w/complex address
void Assembler::jmp8(Register base, int off, Register index, int scale) {
  Register dummy=RAX;
  int prefix = emit_regprefix4(base,dummy,index);
  emit1(0xFF);
  emit_fancy(Register(0x4),base,off,index,scale,prefix);
}

// --- jmp w/complex address
void Assembler::jmp8(Register base, Label &L, Register index, int scale) {
  Register dummy=RAX;
  int prefix = emit_regprefix4(base,dummy,index);
  L.add_jmp(this);
  emit1(0xFF);
  emit_fancy(Register(0x4),base,999,index,scale,prefix);
}

// --- testi
// Compare against an immediate.  Annoyingly, lots of optimizable forms.
void Assembler::testi(Register dst, long imm32)  {
  if( is_uint8(imm32) && (char)imm32 >= 0/*sign bit set does not set flags proper for > or < tests*/) {
    if( dst == RAX ) emit1(0xA8); // Short form in RAX
    else {
emit_regprefix_bytereg(dst);
      emit1(0xF6);
      emit_opreg(0,dst);
    }
emit1(imm32);

  } else if( is_uint16(imm32) && (short)imm32 >= 0 /*sign bit set does not set flags proper for > or < tests*/) {
    emit1(0x66);                  // 16-bit prefix before REX
    if( dst == RAX ) emit1(0xA9); // Short form in RAX
    else {
emit_regprefix4(dst);
      emit1(0xF7);
      emit_opreg(0,dst);
    }
emit2(imm32);

  } else if( is_uint32(imm32) ) {
    if( dst == RAX ) emit1(0xA9); // Short form in RAX
    else {
emit_regprefix4(dst);
      emit1(0xF7);
      emit_opreg(0,dst);
    }
emit4(imm32);

  } else if( is_int32(imm32) ) {  
    Register dst_tmp = dst;
    emit_regprefix8(dst);       // Always a REX
    if( dst_tmp == RAX ) emit1(0xA9); // Short form in RAX
    else {
      emit1(0xF7);
      emit_opreg(0,dst);
    }
emit4(imm32);

  } else {
    assert0(false);             // Should be 32-bit immediate
  }

}


// --- inc4 w/complex address
void Assembler::inc4(Register base, int off, Register index, int scale) {
  Register dummy=RAX;
  int prefix = emit_regprefix4(base,dummy,index);
  emit1(0xFF);
  emit_fancy(Register(0x0),base,off,index,scale,prefix);
}


// --- and1i - and byte, with byte result
void Assembler::and1i(Register dst,int imm8){
  assert0( is_int8(imm8) );
  if( dst == RAX ) {
    emit1(0x24);
  } else {
emit_regprefix_bytereg(dst);
    emit1(0x80);
    emit_opreg(4,dst);
  }
emit1(imm8);
}

// --- or_1i - and byte, with byte result
void Assembler::or_1i(Register dst,int imm8){
  assert0( is_int8(imm8) );
  if( dst == RAX ) {
    emit1(0x0C);
  } else {
emit_regprefix_bytereg(dst);
    emit1(0x80);
    emit_opreg(1,dst);
  }
emit1(imm8);
}

// --- cmp1i - compare byte
void Assembler::cmp1i(Register dst,int imm8){
  assert0( is_int8(imm8) );
  if( dst == RAX ) {
    emit1(0x3C);
  } else {
emit_regprefix_bytereg(dst);
    emit1(0x80);
    emit_opreg(7,dst);
  }
emit1(imm8);
}

// --- test1i - and byte, with byte result
void Assembler::test1i(Register dst,int imm8){
  assert0( is_uint8(imm8) );
  if( dst == RAX ) {
    emit1(0xA8);
  } else {
emit_regprefix_bytereg(dst);
    emit1(0xF6);
    emit_opreg(0,dst);
  }
emit1(imm8);
}

// --- test1i - and byte, with byte result w/complex address
void Assembler::test1i(Register base, int off, Register index, int scale, int imm8) {
  assert0( is_int8(imm8) );
  Register dummy=RAX;
  int prefix = emit_regprefix1(base,dummy,index);
  emit1(0xF6);
  emit_fancy(Register(0x0),base,off,index,scale,prefix);
emit1(imm8);
}

// --- test4i - and imm32 against register
void Assembler::test4i(Register dst, long imm32) {
  assert0( is_int32(imm32) );
  if( dst == RAX ) {
    emit1(0xA9);
  } else {
emit_regprefix_bytereg(dst);
    emit1(0xF7);
    emit_opreg(0,dst);
  }
emit4(imm32);
}

// --- test4i - and imm32 against memory
void Assembler::test4i(Register base, int off, Register index, int scale, long imm32) {
  assert0( is_int32(imm32) );
  Register dummy=RAX;
  int prefix = emit_regprefix4(base,dummy,index);
  emit1(0xF7);
  emit_fancy(Register(0x0),base,off,index,scale,prefix);
emit4(imm32);
}

// --- cmp1i - compare byte address
void Assembler::cmp1i(Register base, address off, int imm8) {
  assert0( is_int8(imm8) || is_uint8(imm8));
  int ioff = (int)(intptr_t)off;
  assert0( (address)ioff == off );
  base = emit_regprefix(base);
  emit1(0x80);
  emit_regbaseoff(Register(0x7),base,ioff);
emit1(imm8);
}

// --- cmp1i - compare byte w/complex address
void Assembler::cmp1i(Register base, int off, Register index, int scale, int imm8) {
  assert0( is_int8(imm8) );
  Register dummy=RAX;
  int prefix = emit_regprefix1(base,dummy,index);
  emit1(0x80);
  emit_fancy(Register(0x7),base,off,index,scale,prefix);
emit1(imm8);
}

// --- cmp4i
// Compare imm32 against memory
void Assembler::cmp4i(address ptr, long imm32) {
  assert0( is_int32(imm32) );
  emit1(is_int8(imm32) ? 0x83 : 0x81); // cmp
  emit_regbaseoff(Register(0x7),noreg,(intptr_t)ptr);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- cmp4i - compare 4bytes w/complex address
void Assembler::cmp4i(Register base, int off, Register index, int scale, long imm32) {
  assert0( is_int32(imm32) );
  Register dummy=RAX;
  int prefix = emit_regprefix4(base,dummy,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // cmp
  emit_fancy(Register(0x7),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- cmp8i - compare 8bytes w/complex address
void Assembler::cmp8i(Register base, int off, Register index, int scale, long imm32) {
  assert0( is_int32(imm32) );
  Register dummy=RAX;
  int prefix = emit_regprefix8(base,dummy,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); // cmp
  emit_fancy(Register(0x7),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- cmp4
// Compare reg against memory
void Assembler::cmp4(Register src, address ptr) {
  Register dummy=RAX;
  emit_regprefix4(dummy, src);
  emit1(0x3B); // cmp
  emit_regbaseoff(src,noreg,(intptr_t)ptr);
}

// --- cmp4
// Compare Freg against memory
void Assembler::cmp4(FRegister src, address ptr) {
emit_regprefix4(src);
  emit1(0x0F); // cmp
  emit1(0x2E);
  emit_regbaseoff((Register)src,noreg,(intptr_t)ptr);
}

// --- cmp4 - compare 4 bytes w/complex address
void Assembler::cmp4(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x3B);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- cmp8 - compare 8 bytes w/complex address
void Assembler::cmp8(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x3B);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- sub4 - subtract 4 bytes w/complex address FROM MEMORY
void Assembler::sub4(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x2B);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- sub4 - subtract 4 bytes w/complex address TO MEMORY
void Assembler::sub4(Register base, intptr_t off, Register index, int scale, Register src) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x29);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- sub4 - subtract 4 bytes w/complex address TO MEMORY
void Assembler::sub4i(Register base, intptr_t off, Register index, int scale, int imm32) {
  assert0( is_int32(imm32) );
  Register none = Register(5);  // sub4
  int prefix = emit_regprefix4(base,none,index);
  emit1(is_int8(imm32) ? 0x83 : 0x81); 
  emit_fancy(Register(5),base,off,index,scale,prefix);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- sub8 - subtract 8 bytes w/complex address
void Assembler::sub8(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x2B);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- test4 - and/mask 4 bytes w/complex address
void Assembler::test4(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix4(base,src,index);
  emit1(0x85);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- test8 - and/mask 8 bytes w/complex address
void Assembler::test8(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x85);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- xchg - exchange 8 bytes w/complex address
void Assembler::xchg(Register src, Register base, int off, Register index, int scale) {
  int prefix = emit_regprefix8(base,src,index);
  emit1(0x87);
  emit_fancy(src,base,off,index,scale,prefix);
}

// --- inc8 - +1 to 8 bytes w/complex address
void Assembler::inc8(Register base, int off, Register index, int scale) {
  Register dummy=RAX;
  int prefix = emit_regprefix1(base,dummy,index);
  emit1(0xFF);
  emit_fancy(Register(0x0),base,off,index,scale,prefix);
}

// --- add2i
// Add imm16 to 2-byte memory
void Assembler::add2i(Register base, intptr_t off, int imm16) {
  assert0( is_int16(imm16) );
  emit1(0x66);                         // Size prefix
  emit1(is_int8(imm16) ? 0x83 : 0x81); // add4
  emit_regbaseoff(Register(0x0),base,off);
  if( is_int8(imm16) ) emit1(imm16);
  else                 emit2(imm16);
}

// --- add4i
// Add imm32 to 4-byte memory
void Assembler::add4i(address ptr, long imm32) {
  assert0( is_int32(imm32) );
  emit1(is_int8(imm32) ? 0x83 : 0x81); // add4
  emit_regbaseoff(Register(0x0),noreg,(intptr_t)ptr);
  if( is_int8(imm32) ) emit1(imm32);
  else                 emit4(imm32);
}

// --- shf
// Shift op helper
void Assembler::shf( Register dst, int imm56, int op ) {
  if( imm56 == 0 ) {            
ShouldNotReachHere();//nothing for no-shift - don't continue silently in case a REX prefix was already generated
  } else if( imm56 == 1 ) {
    emit1( 0xD1 );
    emit_opreg(op,dst);
  } else {
    emit1( 0xC1 );
    emit_opreg(op,dst);
    emit1( imm56 );
  }
}

// --- shf
// Shift op helper
void Assembler::shf( Register base, intptr_t off, Register index, int scale, int imm56, int op ) {
  if( imm56 == 0 ) {            
ShouldNotReachHere();//nothing for no-shift - don't continue silently in case a REX prefix was already generated
  } else if( imm56 == 1 ) {
    emit1( 0xD1 );
    emit_opbaseoffindexscale(op,base,off,index,scale,0);
  } else {
    emit1( 0xC1 );
    emit_opbaseoffindexscale(op,base,off,index,scale,0);
    emit1( imm56 );
  }
}

// --- rol4
// Rotate left by register
void Assembler::rol4(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix4(dst);
  emit1( 0xD3 );
  emit_opreg(0,dst);
}

// --- rol4
// Rotate left by register
void Assembler::rol4( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(0);
  int prefix = emit_regprefix4(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(0,base,off,index,scale,prefix);
}

// --- rol8
// Rotate left by register
void Assembler::rol8(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix8(dst);
  emit1( 0xD3 );
  emit_opreg(0,dst);
}

// --- rol8
// Rotate left by register
void Assembler::rol8( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(0);
  int prefix = emit_regprefix8(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(0,base,off,index,scale,prefix);
}

// --- ror4
// Rotate right by register
void Assembler::ror4(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix4(dst);
  emit1( 0xD3 );
  emit_opreg(1,dst);
}

// --- ror4
// Rotate right by register
void Assembler::ror4( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(1);
  int prefix = emit_regprefix4(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(1,base,off,index,scale,prefix);
}

// --- ror8
// Rotate right by register
void Assembler::ror8(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix8(dst);
  emit1( 0xD3 );
  emit_opreg(1,dst);
}

// --- ror8
// Rotate right by register
void Assembler::ror8( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(1);
  int prefix = emit_regprefix8(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(1,base,off,index,scale,prefix);
}

// --- shl4
// Shift left by register
void Assembler::shl4(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix4(dst);
  emit1( 0xD3 );
  emit_opreg(4,dst);
}

// --- shl4
// Shift left by register
void Assembler::shl4( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(4);
  int prefix = emit_regprefix4(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(4,base,off,index,scale,prefix);
}

// --- shl8
// Shift left by register
void Assembler::shl8(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix8(dst);
  emit1( 0xD3 );
  emit_opreg(4,dst);
}

// --- shl8
// Shift left by register
void Assembler::shl8( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(4);
  int prefix = emit_regprefix8(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(4,base,off,index,scale,prefix);
}

// --- shr4
// Shift right by register
void Assembler::shr4(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix4(dst);
  emit1( 0xD3 );
  emit_opreg(5,dst);
}

// --- shr4
// Shift right by register
void Assembler::shr4( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(5);
  int prefix = emit_regprefix4(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(5,base,off,index,scale,prefix);
}

// --- shr8
// Shift right by register
void Assembler::shr8(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix8(dst);
  emit1( 0xD3 );
  emit_opreg(5,dst);
}

// --- shr8
// Shift right by register
void Assembler::shr8( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(5);
  int prefix = emit_regprefix8(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(5,base,off,index,scale,prefix);
}

// --- sar4
// Shift arithmetic right by register
void Assembler::sar4(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix4(dst);
  emit1( 0xD3 );
  emit_opreg(7,dst);
}

// --- sar4
// Shift arithmetic right by register
void Assembler::sar4( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(7);
  int prefix = emit_regprefix4(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(7,base,off,index,scale,prefix);
}

// --- sar8
// Shift arithmetic right by register
void Assembler::sar8(Register dst,Register rcx){
  assert0( rcx == RCX );        // only shift by RCX register
emit_regprefix8(dst);
  emit1( 0xD3 );
  emit_opreg(7,dst);
}

// --- sar8
// Shift arithmetic right by register
void Assembler::sar8( Register base, intptr_t off, Register index, int scale, Register rcx ) {
  assert0( rcx == RCX );        // only shift by RCX register
  Register none = Register(7);
  int prefix = emit_regprefix8(base,none,index);
  emit1( 0xD3 );
  emit_opbaseoffindexscale(7,base,off,index,scale,prefix);
}

// --- btx4i
// Bit-test 4-byte immediate-vs-memory
void Assembler::btx4i(Register base, int off, int imm5 ) { 
  assert0( is_uint5(imm5) );
emit_regprefix4(base);
  emit2( 0xBA0F ); 
  emit_regbaseoff((Register)4,base,off);
  emit1( imm5 ); 
}

// --- bts4i
// Bit-test-set 4-byte immediate-vs-memory
void Assembler::bts4i(Register base, int off, int imm5 ) { 
  assert0( is_uint5(imm5) );
emit_regprefix4(base);
  emit2( 0xBA0F ); 
  emit_regbaseoff((Register)5,base,off);
  emit1( imm5 ); 
}

// --- btx8
// Bit-test bit index in bitmap at address base+off
void Assembler::btx8(Register base, int off, Register bit) {
  emit_regprefix8(base, bit);
  emit1(0x0F);                // Needs an opcode extension
  emit1(0xA3);                // BT opcode
  emit_regbaseoff(bit, base, off);
}

// --- bts8
// Bit-test-and-set bit index in bitmap at address base+off
void Assembler::bts8(Register base, int off, Register bit) {
  emit_regprefix8(base, bit);
  emit1(0x0F);                // Needs an opcode extension
  emit1(0xAB);                // BTS opcode
  emit_regbaseoff(bit, base, off);
}

// --- btx4i
// Bit-test 4-byte immediate-vs-memory
void Assembler::btx4i(address adr, int imm5 ) { 
  assert0( is_uint5(imm5) );
  assert0( is_int32((intptr_t)adr) );
  emit2( 0xBA0F ); 
  emit_regbaseoff(Register(4),noreg,(intptr_t)adr);
  emit1( imm5 ); 
}

// --- xorf
// XOR FP register - also a fast FP zero
void Assembler::xorf(FRegister dst,FRegister src){
  // This is the XORPS opcode
emit_regprefix4(src,dst);
  emit1(0x0F);
  emit1(0x57);
emit_regreg(dst,src);
}

// --- xord
// XOR FP register - also a fast FP zero
void Assembler::xord(FRegister dst,FRegister src){
  // This is the XORPD opcode
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x57);
emit_regreg(dst,src);
}

// --- xor16
// XOR FP register - also a fast FP zero
void Assembler::xor16(FRegister dst,FRegister src){
  // This is the PXOR opcode
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0xEF);
emit_regreg(dst,src);
}

// --- xor16
// XOR FP register with memory
void Assembler::xor16( FRegister dst, Register base, int off, Register index, int scale) {
  Register rdst = freg2reg(dst);
  // emit PXOR xmm, m128
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rdst,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0xEF);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- xorf
// XOR FP register - useful for manipulating the FP sign bit
void Assembler::xorf( FRegister dst, address ptr ) {
  assert (((intptr_t)ptr&7) == 0, "unaligned quadword")
  // This is the XORPS opcode
emit_regprefix4(dst);
  emit1(0x0F);
  emit1(0x57);
  emit_regbaseoff((Register)dst, noreg, (intptr_t)ptr);
}

// --- xord
// XOR FP register - useful for manipulating the FP sign bit
void Assembler::xord( FRegister dst, address ptr ) {
  assert (((intptr_t)ptr&7) == 0, "unaligned quadword")
  // This is the XORPD opcode
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x57);
  emit_regbaseoff((Register)dst, noreg, (intptr_t)ptr);
}

// --- andf
// AND FP register - useful for manipulating the FP sign bit
void Assembler::andf( FRegister dst, address ptr ) {
  assert (((intptr_t)ptr&7) == 0, "unaligned quadword")
  // This is the ANDPS opcode
emit_regprefix4(dst);
  emit1(0x0F);
  emit1(0x54);
  emit_regbaseoff((Register)dst, noreg, (intptr_t)ptr);
}

// --- andd
// AND FP register - useful for manipulating the FP sign bit
void Assembler::andd( FRegister dst, address ptr ) {
  assert (((intptr_t)ptr&7) == 0, "unaligned quadword")
  // This is the ANDPD opcode
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x54);
  emit_regbaseoff((Register)dst, noreg, (intptr_t)ptr);
}

void Assembler::pcmpeqb(FRegister dst,FRegister src){
  emit1(0x66);
emit_regprefix4(src,dst);
  emit1(0x0F);
  emit1(0x74);
emit_regreg(dst,src);
}


void Assembler::pmovmskb(Register dst,FRegister src){
  emit1(0x66);
emit_regprefix4(src,dst);
  emit1(0x0F);
  emit1(0xD7);
emit_regreg(dst,src);
}

// --- test16
// test 16bytes of FP register and set flags
void Assembler::test16(FRegister dst,FRegister src){
  // This is the PTEST opcode
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x38);
  emit1(0x17);
emit_regreg(dst,src);
}

// --- mov4
// fp move between FP registers
void Assembler::mov4(FRegister dst,FRegister src){
  // this is the MOVAPS opcode, so the dst 128 bits will be overwritten. This
  // opcode avoids a partial register stall on Nehalem (Intel Optimization
  // Manual section 3.5.2.4).
emit_regprefix4(src,dst);
  emit1( 0x0F);
  emit1( 0x28);
emit_regreg(dst,src);
}

// --- mov8
// fp move between FP registers
void Assembler::mov8(FRegister dst,FRegister src){
  // this is the MOVAPD opcode, so the dst 128 bits will be overwritten. This
  // opcode avoids a partial register stall on Nehalem (Intel Optimization
  // Manual section 3.5.2.4).
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x28);
emit_regreg(dst,src);
}

// --- mov4
// aligned 4b fp move into FP register
void Assembler::mov4(FRegister dst,Register src){
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x6E);
emit_regreg(dst,src);
}

// --- mov4
// aligned 4b fp move from FP register
void Assembler::mov4(Register dst,FRegister src){
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix4(dst,src); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x7E);
emit_regreg(src,dst);
}

// --- mov8
// aligned 8b fp move into FP register
void Assembler::mov8(FRegister dst,Register src){
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix8(src,dst); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x6E);
emit_regreg(dst,src);
}

// --- mov8
// aligned 8b fp move from FP register
void Assembler::mov8(Register dst,FRegister src){
  emit1( 0x66);  // Not really a size prefix, just another opcode byte
  emit_regprefix8(dst,src); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x7E);
emit_regreg(src,dst);
}

// --- ld4
// aligned 4b fp load into FP register
void Assembler::ld4( FRegister dst, Register base, int off ) {
  // emit movss xmm, m32
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(base,dst); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit_regbaseoff((Register)dst,base,off);
}

// --- ld4
// aligned 4b fp load into FP register
void Assembler::ld4( FRegister dst, Register base, int off, Register index, int scale ) {
  Register rdst = freg2reg(dst);
  // emit movss xmm, m32
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rdst,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- ld4
// aligned 4b fp load into FP register
void Assembler::ld4( FRegister dst, address low_adr) {
  assert0( is_int32((intptr_t)low_adr));
  // emit movss xmm, m64
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(dst); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit1(0x04|dst<<3);
  emit1(0x25);
  emit4((intptr_t)low_adr);
}

// --- ld8
// aligned 8b fp load into FP register
void Assembler::ld8( FRegister dst, Register base, int off ) {
  // emit movsd xmm, m64
  emit1(0xF2);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(base,dst); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit_regbaseoff((Register)dst,base,off);
}

// --- ld8
// aligned 8b fp load into FP register
void Assembler::ld8( FRegister dst, Register base, int off, Register index, int scale ) {
  Register rdst = freg2reg(dst);
  // emit movsd xmm, m64
  emit1(0xF2);   // Not really a rep prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rdst,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- ld8
// aligned 8b fp load into FP register
void Assembler::ld8( FRegister dst, address low_adr) {
  assert0( is_int32((intptr_t)low_adr));
  // emit movsd xmm, m64
  emit1(0xF2);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(dst); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x10);
  emit1(0x04|dst<<3);
  emit1(0x25);
  emit4((intptr_t)low_adr);
}

// --- ld16u
// unaligned 16b load
void Assembler::ld16u( FRegister dst, Register base, int off, Register index, int scale) {
  Register rdst = freg2reg(dst);
  // emit movdqu xmm, m128
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rdst,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x6F);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- st4
// aligned 4b fp store from FP register
void Assembler::st4( Register base, int off, FRegister src ) {
  // emit movss m32, xmm
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(base,src); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x11);
  emit_regbaseoff((Register)src,base,off);
}

// --- st4
// aligned 4b fp store from FP register
void Assembler::st4  (Register base, int off, Register index, int scale, FRegister src) {
  Register rsrc = freg2reg(src);
  // emit movss m32, xmm
  emit1(0xF3);   // Not really a rep prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rsrc,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x11);
  emit_fancy(rsrc,base,off,index,scale,prefix);
}

// --- st8
// aligned 8b fp store from FP register
void Assembler::st8( Register base, int off, FRegister src ) {
  // emit movsd m64, xmm
  emit1(0xF2);   // Not really a rep prefix, just another opcode byte
  emit_regprefix4(base,src); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x11);
  emit_regbaseoff((Register)src,base,off);
}

// --- st8
// aligned 8b fp store from FP register
void Assembler::st8  (Register base, int off, Register index, int scale, FRegister src) {
  Register rsrc = freg2reg(src);
  // emit movss m64, xmm
  emit1(0xF2);   // Not really a rep prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rsrc,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x11);
  emit_fancy(rsrc,base,off,index,scale,prefix);
}

// --- st16
// aligned 16b store
void Assembler::st16( Register base, int off, FRegister src ) {
  emit1(0x66);                  // Not really a size prefix, just another opcode byte
  emit_regprefix4(base,src);    // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x7F);
  emit_regbaseoff((Register)src,base,off);
}

void Assembler::bswap2(Register reg){
if(is_abcdx(reg)){
    // xchg high reg with low reg (e.g. xchg ah, al)
    emit1(0x86); emit1(0xC0 | (reg+4)<<3 | reg);
  } else {
    // 16bit ror reg, 8
    emit1(0x66); // size override
ror4i(reg,8);
  }
}

// --- cmp4
// compare 2 FP registers
void Assembler::cmp4(FRegister dst,FRegister src){
emit_regprefix4(src,dst);
  emit1( 0x0F);
  emit1( 0x2E);
emit_regreg(dst,src);
}

// --- cmp4
// aligned 4b fp cmp into FP register
void Assembler::cmp4( FRegister dst, Register base, int off ) {
  emit_regprefix4(base,dst);
  emit1(0x0F);
  emit1(0x2E);
  emit_regbaseoff((Register)dst,base,off);
}

// --- cmp4
// aligned 4b fp cmp into FP register
void Assembler::cmp4( FRegister dst, Register base, int off, Register index, int scale ) {
  Register rdst = freg2reg(dst);
  int prefix = emit_regprefix4(base,rdst,index);
  emit1(0x0F);
  emit1(0x2E);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- cmp8
// compare 2 FP registers
void Assembler::cmp8(FRegister dst,FRegister src){
  emit1( 0x66);             // Not really a size prefix, just another opcode byte
  emit_regprefix4(src,dst); // note funky placement of REX vs SIZE prefix
  emit1( 0x0F);
  emit1( 0x2E);
emit_regreg(dst,src);
}

// --- cmp8
// Compare Freg against memory
void Assembler::cmp8(FRegister src, address ptr) {
  assert0( is_int32((intptr_t)ptr));
  emit1(0x66);
emit_regprefix4(src);
  emit1(0x0F); // cmp
  emit1(0x2E);
  emit1(0x04 | src<<3 | 4 );         // modrm:: 00 src  100 (has SIB)
  emit1(0x25);                       // SIB: 00 scale, 100 (no index)101 (no base)
  emit4((intptr_t)ptr);              // address
}

// --- cmp8
// aligned 8b fp cmp into FP register
void Assembler::cmp8( FRegister dst, Register base, int off ) {
  emit1(0x66);               // Not really a size prefix, just another opcode byte
  emit_regprefix4(base,dst); // note funky placement of REX vs SIZE prefix
  emit1(0x0F);
  emit1(0x2E);
  emit_regbaseoff((Register)dst,base,off);
}

// --- cmp8
// aligned 8b fp cmp into FP register
void Assembler::cmp8( FRegister dst, Register base, int off, Register index, int scale ) {
  Register rdst = freg2reg(dst);
  emit1(0x66);               // Not really a size prefix, just another opcode byte
  int prefix = emit_regprefix4(base,rdst,index); // note funky placement of REX vs SSE prefix
  emit1(0x0F);
  emit1(0x2E);
  emit_fancy(rdst,base,off,index,scale,prefix);
}

// --- cvt_i2f
// Convert integer to float
void Assembler::cvt_i2f(FRegister dst,Register src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
emit_regreg(dst,src);
}

void Assembler::cvt_i2f( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- cvt_f2i
// Convert float to integer
void Assembler::cvt_f2i(Register dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2C);
emit_regreg(dst,src);
}

// --- cvt_f2l
// Convert float to long
void Assembler::cvt_f2l(Register dst,FRegister src){
  emit1(0xF3);
  emit_regprefix8(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2C);
emit_regreg(dst,src);
}

// --- cvt_i2d
// Convert integer to double
void Assembler::cvt_i2d(FRegister dst,Register src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
emit_regreg(dst,src);
}

void Assembler::cvt_i2d( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- cvt_l2d
// Convert long to double
void Assembler::cvt_l2d(FRegister dst,Register src){
  emit1(0xF2);
  emit_regprefix8(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
emit_regreg(dst,src);
}

void Assembler::cvt_l2d( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix8(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- cvt_l2f
// Convert long to float
void Assembler::cvt_l2f(FRegister dst,Register src){
  emit1(0xF3);
  emit_regprefix8(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
emit_regreg(dst,src);
}

void Assembler::cvt_l2f( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF3);
  int prefix = emit_regprefix8(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- cvt_d2i
// Convert double to integer
void Assembler::cvt_d2i(Register dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2C);
emit_regreg(dst,src);
}

// --- cvt_d2l
// Convert double to long
void Assembler::cvt_d2l(Register dst,FRegister src){
  emit1(0xF2);
  emit_regprefix8(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x2C);
emit_regreg(dst,src);
}

// --- cvt_f2d
// Convert float to double
void Assembler::cvt_f2d(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
emit_regreg(dst,src);
}

void Assembler::cvt_f2d( FRegister dst, Register base, int off) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
  emit_regbaseoff((Register)dst,base,off);
}

void Assembler::cvt_f2d( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- cvt_d2f
// Convert double to float
void Assembler::cvt_d2f(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
emit_regreg(dst,src);
}

// --- cvt_d2f
// Convert double to float
void Assembler::cvt_d2f( FRegister dst, Register base, int off ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
  emit_regbaseoff((Register)dst,base,off);
}

// --- cvt_d2f
// Convert double to float
void Assembler::cvt_d2f( FRegister dst, Register base, int off, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5A);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- addf
// Add float
void Assembler::addf( FRegister dst,  Register base, int offset ) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x58);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- addf
// Add float
void Assembler::addf(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x58);
emit_regreg(dst,src);
}

// --- addf
// Add float
void Assembler::addf(FRegister dst,  Register base, int off, Register index, int scale) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x58);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- mulf
// Multiply float
void Assembler::mulf( FRegister dst,  Register base, int offset ) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x59);
  emit_regbaseoff((Register)dst,base,offset);
}

void Assembler::mulf(FRegister dst,  Register base, int off, Register index, int scale) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x59);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- mulf
// Multiply float
void Assembler::mulf(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x59);
emit_regreg(dst,src);
}

// --- mulf 
// Multiply FREG with value in memory
void Assembler::mulf( FRegister dst,  address ptr ) {
  emit1(0xF3);
  emit_regprefix4(dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x59);
  emit_regbaseoff((Register)dst,noreg,(intptr_t)ptr);
}

// --- subf
// Subtract float
void Assembler::subf( FRegister dst,  Register base, int offset ) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5C);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- subf
// Subtract float
void Assembler::subf(FRegister dst,  Register base, int off, Register index, int scale) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5C);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- subf
// Subtract float
void Assembler::subf(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5C);
emit_regreg(dst,src);
}

// --- divf
// Divide float
void Assembler::divf( FRegister dst,  Register base, int offset ) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5E);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- divf
// Divide float
void Assembler::divf(FRegister dst,  Register base, int off, Register index, int scale) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5E);
  emit_fancy((Register)dst,base,off,index,scale,prefix);
}

// --- divf
// Divide float
void Assembler::divf(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F3 prefix
  emit1(0x0F);
  emit1(0x5E);
emit_regreg(dst,src);
}

// --- addd
// Add double
void Assembler::addd( FRegister dst,  Register base, int offset ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x58);
  emit_regbaseoff((Register)dst,base,offset);
}

void Assembler::addd( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x58);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- muld
// Multiply double
void Assembler::muld( FRegister dst,  Register base, int offset ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x59);
  emit_regbaseoff((Register)dst,base,offset);
}

void Assembler::muld( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x59);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- subd
// Subtract double
void Assembler::subd( FRegister dst,  Register base, int offset ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5C);
  emit_regbaseoff((Register)dst,base,offset);
}

void Assembler::subd( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5C);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- divd
// Divide double
void Assembler::divd( FRegister dst,  Register base, int offset ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5E);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- divd
// Divide double
void Assembler::divd( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5E);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- sqrtd
// Double square root
void Assembler::sqrtd( FRegister dst,  Register base, int offset ) {
  emit1(0xF2);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- sqrtd
// Double square root
void Assembler::sqrtd( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF2);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- sqrts
// Float square root
void Assembler::sqrts( FRegister dst,  Register base, int offset ) {
  emit1(0xF3);
  emit_regprefix4(base,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
  emit_regbaseoff((Register)dst,base,offset);
}

// --- sqrts
// Float square root
void Assembler::sqrts( FRegister dst,  Register base, int offset, Register index, int scale ) {
  emit1(0xF3);
  int prefix = emit_regprefix4(base,dst,index); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
  emit_fancy((Register)dst,base,offset,index,scale,prefix);
}

// --- addd
// Add double
void Assembler::addd(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x58);
emit_regreg(dst,src);
}

// --- muld
// Multiply double
void Assembler::muld(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x59);
emit_regreg(dst,src);
}

// --- subd
// Subtract double
void Assembler::subd(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5C);
emit_regreg(dst,src);
}

// --- divd
// Divide double
void Assembler::divd(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x5E);
emit_regreg(dst,src);
}

// --- sqrtd
// Double square root
void Assembler::sqrtd(FRegister dst,FRegister src){
  emit1(0xF2);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
emit_regreg(dst,src);
}

// --- sqrts
// Float square root
void Assembler::sqrts(FRegister dst,FRegister src){
  emit1(0xF3);
  emit_regprefix4(src,dst); // note funky placement of REX vs F2 prefix
  emit1(0x0F);
  emit1(0x51);
emit_regreg(dst,src);
}

// --- jeq
// jeq to target in another CodeBlob
void Assembler::jeq( address longjmp ) {
  assert0( longjmp != NULL );   // init ordering problems
  if( _blob->contains(longjmp) ) {
Label l;
    bind(l,longjmp);
    jeq(l);
  } else {
    emit1(0x0F);
    emit1(0x74+0x10);  // long-branch opcode derived from short-branch opcode
    emit_relative_offset(longjmp);
  }
}

// --- jne
// jne to target in another CodeBlob
void Assembler::jne( address longjmp ) {
  assert0( longjmp != NULL );   // init ordering problems
  if( _blob->contains(longjmp) ) {
Label l;
    bind(l,longjmp);
    jne(l);
  } else {
    emit1(0x0F);
    emit1(0x75+0x10);  // long-branch opcode derived from short-branch opcode
    emit_relative_offset(longjmp);
  }
}

// --- jae
// jae to target in another CodeBlob
void Assembler::jae( address longjmp ) {
  assert0( longjmp != NULL );   // init ordering problems
  if( _blob->contains(longjmp) ) {
Label l;
    bind(l,longjmp);
    jae(l);
  } else {
    emit1(0x0F);
    emit1(0x73+0x10);  // long-branch opcode derived from short-branch opcode
    emit_relative_offset(longjmp);
  }
}

// --- jmp
// jmp to target in another CodeBlob
void Assembler::jmp( address longjmp ) {
  assert0( longjmp != NULL );   // init ordering problems
  if( _blob->contains(longjmp) ) {
Label l;
    bind(l,longjmp);
jmp(l);
  } else {
    emit1(0xE9);  // long-branch opcode
    emit_relative_offset(longjmp);
  }
}


// --- align
// Align to the given power of 2 with breakpoint ops.
void Assembler::align(intx x) {
  assert0( is_power_of_2(x) );
  int lg2 = log2_intptr(x);
  assert0( lg2 >= 3 );          // at least align to 8
  if( has_variant_branches() ) {
    _bra_pcs.push(rel_pc());
    _bra_idx.push(-lg2);        // Record an alignment request
  } else {                      // Else just do it now
    int pad = x-1 - ((rel_pc()-1) & (x-1) );
    if( pad ) nop(pad);
  }
}

void Assembler::align_with_nops(int modulus) {
align(modulus);
}

// --- getthr
// Get current Thread into Rthr
void MacroAssembler::getthr(Register thr){
  move8(thr,RSP);
  and8i(thr,~(BytesPerThreadStack-1));
}

// --- gettid
// Get current Thread-ID into Rthr.  Never hand out zero.
void MacroAssembler::gettid(Register thr){
  move8(thr,RSP);
  shr8i(thr,LogBytesPerThreadStack);
  // Stacks are at (at the moment, see azul_mmap.h): 0x1000 0000 0000 and are
  // 2Megs in size (X86 large tlb page), which is a shift of 21.  So the first
  // thread ID returned is: (0x1000 0000 0000 >> 21), or: 0x800000.
}

// --- lvb
// Read Barrier.  
int MacroAssembler::lvb(RInOuts, Register dst, Register tmp, RKeepIns, Register base, int off, bool can_be_null) {
  return lvb(RInOuts::a,dst,tmp,RKeepIns::a,base,off,noreg,0,can_be_null);
}


// --- lvb
// Read Barrier.
int MacroAssembler::lvb(RInOuts, Register dst, Register tmp, RKeepIns, Register base, int off, Register index, int scale, bool can_be_null) {
  int end = -1;
  assert0( tmp != noreg );
  assert_different_registers(tmp, base, index, true);
  Label done;
  // 'dst' is already loaded.
  // The "fast" portion of the LVB check
  if( can_be_null && (RefPoisoning || UseLVBs) ) null_chk(dst,done);
  if( RefPoisoning ) always_unpoison(dst); // Remove poison

  // Read Barriers!  'dst' is known not-null.
  if( UseLVBs ) {
    mov8(tmp, dst);            // Need some of the high 4 bytes so a 8-byte move
    shr8i(tmp, objectRef::offset_in_page);
    and4i(tmp, GPGC_ReadTrapArray::read_barrier_index_mask);
    // the array will be allocated in the lower 4GB
    int rdbar_ary = (intptr_t)GPGC_ReadTrapArray::address_of_read_barrier_array();
    test1i(noreg, rdbar_ary, tmp, 0, GPGC_ReadTrapArray::trap_mask);
    jze  (done);                // trap flag not set

    // We fail the fast test and must go slow.  Blows no registers.
    // Value is not poisoned and not null.
    st8  (RSP, LVB_Value-LVB_Framesize, dst);        // value loaded
if(index==noreg){
      if( base == noreg ) { mov8i(dst, off, false);              } // !destroy_flags so that we don't simplify to xor (for c1 patching)
      else                { lea  (dst, base, off);               }
    } else                { lea  (dst, base, off, index, scale); }
end=rel_pc();
    st8  (RSP, LVB_TAVA-LVB_Framesize, dst);        // address loaded from
    call (StubRoutines::x86::handler_for_GCLdValueTr_entry()); //lvb trap.. does both
    ld8  (dst, RSP, LVB_Result-LVB_Framesize);
  } else {
#ifdef ASSERT
destroy(tmp);
#endif
  }
  bind(done);
  return end == -1 ? 0 : end-4; // rel_pc of any offset to be patched, or 0.
}

// --- find_oop_in_oop_array
// Roop_to_find - oop to find (will be stripped)
// Roop_array   - stripped array to find it in
// RCX_count    - number of elements in the array (must be RCX)
// Rtmp         - temp register
// not_found    - label branched to if oop is not found, if found then fall through
void MacroAssembler::find_oop_in_oop_array(RKeepIns, Register Roop_to_find,
                                           RInOuts, Register Roop_array,
                                           Register RCX_count, Register Rtmp, Register Rtmp2, Label& not_found) {
  assert (RCX_count == RCX, "register assumptions must be valid");
  Label found, count_zero;

  // if RCX == 0 then goto not_found (NB JavaSoft assume RCX is never 0 for subtype checks)
  jrcxz1(count_zero); // don't branch directly in case not_found isn't within 1 byte
  // Point Roop_array at first element of array
add8i(Roop_array,arrayOopDesc::base_offset_in_bytes(T_OBJECT));
  // Perform half of the strip of metadata bits for compares
  shl8i (Roop_to_find,64-objectRef::unknown_bits);
  assert_different_registers(Rtmp, Roop_to_find, RCX_count, Roop_array);
  { Label loop;
    bind  (loop);
    ldref_lvb (RInOuts::a, Rtmp, Rtmp2, RKeepIns::a, Roop_array, 0, false);
    // Strip second half of the metadata bits for compares
    shl8i     (Rtmp,64-objectRef::unknown_bits);
    add8i     (Roop_array,8);
    cmp8      (Roop_to_find, Rtmp);
    jloopne1  (loop);  // RCX--; if (Roop_to_find != Rtmp && RCX > 0) goto loop
    jeq       (found); //        if (Roop_to_find == Rtmp) goto found
  }
  // fix up metadata and leave
  shr8i (Roop_to_find,64-objectRef::unknown_bits);
bind(count_zero);
jmp(not_found);
bind(found);
  shr8i (Roop_to_find,64-objectRef::unknown_bits);  
}

// --- cvta2va
// Strip metadata bits
void MacroAssembler::cvta2va(Register ref){
  if( MultiMapMetaData ) {
    // Meta data is multi-mapped and does not need stripping
  } else {
    // clear the top meta data bits. NB the following isn't possible using a mask as Intel
    // immediates are too short:
    // and8i(ref, (1l << objectRef::unknown_bits)-1);
    shl8i(ref,64-objectRef::unknown_bits);
    shr8i(ref,64-objectRef::unknown_bits);
  }
}

// --- ref2kid
// Strip to the KID field and set zero flag, may cause NPE if !KIDInRef
void MacroAssembler::ref2kid(Register ref){
  if( KIDInRef ) {
    shr8i(ref,objectRef::klass_id_shift);
  } else {
    cvta2va(ref);
    ldz4(ref,ref,oopDesc::mark_offset_in_bytes()+4);
    shr4i(ref,markWord::kid_shift-32);
  }
}

void MacroAssembler::ref2kid(Register dst,Register ref){
  if( KIDInRef ) {
    move8(dst,ref);
    shr8i(dst,objectRef::klass_id_shift);
  } else if( !MultiMapMetaData ) {
    cvta2va(dst,ref);
    ldz4(dst,dst,oopDesc::mark_offset_in_bytes()+4);
    shr4i(dst,markWord::kid_shift-32);
  } else {
    // avoid redundant move when multi-mapping
    ldz4(dst,ref,oopDesc::mark_offset_in_bytes()+4);
    shr4i(dst,markWord::kid_shift-32);
  }
}

// Get KID bits and hide NPEs, in event of NPE dst holds null
void MacroAssembler::ref2kid_no_npe(Register dst,Register ref){
  if( KIDInRef ) {
    ref2kid(dst, ref); // can't throw NPE
  } else {
    // Set up NPE handler to ignore an NPE in the load, can't avoid move
    // with MultiMapMetaData as must guarantee dst == null if NPE occurs
    cvta2va(dst, ref);
    const int npe_off = offset();
    ldz4(dst, dst, oopDesc::mark_offset_in_bytes()+4);
    shr4i(dst,markWord::kid_shift-32);
    add_implicit_exception( npe_off,  offset() );
  }
}

// --- pre_write_barrier
// Write barrier (ref store check) to be performed prior to write/CAS.
// The store-check can trigger generational card-marking, and also SBA escapes.
// SBA escapes can trigger GC.  Hence this macro can GC and is an interpreter
// GC point.  If a safepoint happens it must be taken BEFORE the store
// bytecode, because the store cannot have happened yet (and the store cannot
// happen until GC makes space for the SBA escape).

// Rtmp0, Rtmp1 - Temps that are crushed
// Rbase  - Base REF address being stored into (Non-NULL)
// off    - Displacement from base ref
// Rindex - Register holding index for array stores or noreg
// scale  - Scale to apply to Rindex
// Rval   - Value REF being stored (Non-NULL)
// safe_to_store - Label branched to when it is safe to store.
//                 With SBA the fall-through must perform the SBA escape.
//                 Without SBA the fall-through performs the write.
void MacroAssembler::pre_write_barrier(RInOuts, Register Rtmp0, Register Rtmp1,
                                       RKeepIns, Register Rbase, int off,  Register Rindex, int scale, Register Rval,
Label&safe_to_store){
  // Ensure clobbering of Rtmp0 and Rtmp1 doesn't mean we clobber input values
  assert_different_registers(Rbase , Rtmp0, Rtmp1);
  assert_different_registers(Rval  , Rtmp0, Rtmp1);
  assert_different_registers(Rindex, Rtmp0, Rtmp1);
Label stack_escape;

  // The full-blown write-barrier.  Generally it is safe to store older
  // space-ids into younger space-ids.  It's also safe to store same-gen
  // values into each other, except for stack values which require a stack
  // frame-id check.
  //         OLD_GEN(3) << YOUNG_GEN(2) << STACK(1)
  move8(Rtmp0,Rbase);         // Rtmp0 = spaceid(base)
  move8(Rtmp1,Rval);          // Rtmp1 = spaceid(val)
  shr8i(Rtmp0,objectRef::space_shift);
  shr8i(Rtmp1,objectRef::space_shift);
  if( KIDInRef ) {
    and1i(Rtmp0,objectRef::space_mask );
    and1i(Rtmp1,objectRef::space_mask );
    cmp1 (Rtmp0,Rtmp1);         // spaceid(base) cmp spaceid(val) ?
  } else {
    cmp4 (Rtmp0,Rtmp1);         // spaceid(base) cmp spaceid(val) ?
  }
  if( !UseSBA ) {
    jle  (safe_to_store);       // if (spaceid(base) <= spaceid(val)) goto safe_to_store
    // Else base is larger spaceid (older) than value and needs to be marked
  } else {
Label not_same_space;
    jne  (not_same_space);      // if (spaceid(base) != spaceid(val)) goto not_same_space
    // If this isn't stack space then stores to the same heap generation are always safe
    cmp1i(Rtmp0,objectRef::stack_space_id);
    jne  (safe_to_store);       // spaceid(base) == spaceid(val) == old/young heap
    // Check stack references are correctly ordered
    cvta2va(Rtmp0,Rbase);
    cvta2va(Rtmp1,Rval);
    cmp8 (Rtmp0,Rtmp1);         // See if base_adr > value_adr
    jae  (safe_to_store);       // Addresses ordered properly, no need for write barrier on stack
    ldz1 (Rtmp0,Rtmp0,-sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_fid)); // Load base FID into Rtmp0
    cmp1 (Rtmp0,Rtmp1,-sizeof(SBAPreHeader)+offset_of(SBAPreHeader,_fid)); // Compare against value fid
    likely()->jae(safe_to_store); // Addresses ordered properly, no need for write barrier on stack
    jmp  (stack_escape);        // FIDs not properly ordered
    // Space IDs vary
bind(not_same_space);
    likely()->jlt(safe_to_store); // if (spaceid(base) < spaceid(val)) goto safe_to_store
    // Fall-through => spaceid(base) > spaceid(val)
    // Definitely wrong-order store, but check for stack-vs-heap
    cmp1i(Rtmp1,objectRef::stack_space_id);
    jeq  (stack_escape);          // if (spaceid(val) == stack_space_id) goto stack_escape
    // Else base is larger spaceid (older) than value and needs to be marked
  }

  // young-gen/old-gen mark.

  // First: strip off the metadata from the base manually - even if we are
  // living in a MultiMapMetaData world.  The cardmark math cannot handle
  // metadata.
  if( Rindex == noreg ) lea  (Rtmp0,Rbase,off);
  else                  lea  (Rtmp0,Rbase,off,Rindex,scale);
  BarrierSet* bs = Universe::heap()->barrier_set();
  if (bs->kind() == BarrierSet::GenPauselessBarrier) {
    HeapWord* bitmap_base  = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bitmap);
    HeapWord* bytemap_base = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bytemap);

    shl8i  (Rtmp0,64-objectRef::unknown_bits);
    shr8i  (Rtmp0,64-objectRef::unknown_bits+ LogBytesPerWord); // Rtmp0 = bit index in cardmark bitmap
    mov8i  (Rtmp1,intptr_t(bitmap_base)); // Rtmp1 = bitmap base
    btx8   (Rtmp1,0,Rtmp0);               // Test bit Rtmp0 in bitmap at addr Rtmp1
    jbl    (safe_to_store);               // bit already set, jump to store code.
    locked()->bts8(Rtmp1,0,Rtmp0);        // Atomic set bit Rtmp0 in bitmap at addr Rtmp1
    mov8i  (Rtmp1,intptr_t(bytemap_base));// Rtmp1 = bytemap base
    shr8i  (Rtmp0,LogWordsPerGPGCCardMarkSummaryByte); // Rtmp0 = byte index in summary bytemap
    st1i   (Rtmp1,0,Rtmp0,0,1);           // Store 1 into byte at Rtmp1[Rtmp0]
  } else if (bs->kind() == BarrierSet::CardTableModRef) {
    CardTableModRefBS* ct = (CardTableModRefBS*)bs;
    assert(sizeof(*ct->byte_map_base) == sizeof(jbyte), "adjust this code");
    // I assume we use precise cardmarks, even with SBA and even for plain objects.
    assert0(ct->precision()==ct->Precise);
    shl8i  (Rtmp0,64-objectRef::unknown_bits);
    shr8i  (Rtmp0,64-objectRef::unknown_bits+ CardTableModRefBS::card_shift);
    // Filtered cardmark: do not over-mark.  Repeated storing of zero
    // by many cpus forces the cache-line to ping-pong around.  It's
    // cheaper to test-then-set instead of unconditionally writing in
    // the bad case (but makes the normal-case slightly slower).
    assert0(is_int32((intptr_t)ct->byte_map_base));
    add8i  (Rtmp0, (long)ct->byte_map_base);
    cmp1i  (Rtmp0, 0 , 0);
    jeq    (safe_to_store); // Already marked
    st1i   (Rtmp0, 0 ,0);
  } else {
    should_not_reach_here("No barriers defined");
  }
  if (UseSBA) jmp  (safe_to_store);
  // All done - SBA fall-through to SBA escape, otherwise proceed with store
bind(stack_escape);
}

// --- pre_write_barrier_HEAP
// Write barrier (ref store check) to be performed prior to write/CAS.
// Not for SBA.  The store-check can trigger generational card-marking.
// Not a Safepoint.  No GC.
// Rtmp0, Rtmp1 - Temps that are crushed
// Rdst   - Base REF address being stored into (Non-NULL)
// Rsrc   - Value REF being stored (Non-NULL)
void MacroAssembler::pre_write_barrier_HEAP(RInOuts, Register Rtmp0, Register Rtmp1,
                                            RKeepIns, Register Rdst, Register Rsrc ) {
  // Ensure clobbering of Rtmp0 and Rtmp1 doesn't mean we clobber input values
  assert_different_registers(Rsrc, Rdst, Rtmp0, Rtmp1);
Label safe_to_store;
  // The full-blown write-barrier.  Generally it is safe to store older
  // space-ids into younger space-ids.  It's also safe to store same-gen
  // values into each other, except for stack values which require a stack
  // frame-id check.
  //         OLD_GEN(3) << YOUNG_GEN(2) << STACK(1)
  move8(Rtmp0,Rdst);         // Rtmp0 = spaceid(dst )
  move8(Rtmp1,Rsrc);         // Rtmp1 = spaceid(src )
  shr8i(Rtmp0,objectRef::space_shift);
  shr8i(Rtmp1,objectRef::space_shift);
  if( KIDInRef ) {
    and1i(Rtmp0,objectRef::space_mask );
    and1i(Rtmp1,objectRef::space_mask );
    cmp1 (Rtmp0,Rtmp1);         // spaceid(base) cmp spaceid(val) ?
  } else {
    cmp4 (Rtmp0,Rtmp1);         // spaceid(base) cmp spaceid(val) ?
  }
  jle  (safe_to_store); // if (spaceid(base) <= spaceid(val)) goto safe_to_store

  // young-gen/old-gen mark.

  // First: strip off the metadata from the base manually - even if we are
  // living in a MultiMapMetaData world.  The cardmark math cannot handle
  // metadata.
  move8(Rtmp0,Rdst);
  BarrierSet* bs = Universe::heap()->barrier_set();
  if (bs->kind() == BarrierSet::GenPauselessBarrier) {
    HeapWord* bitmap_base  = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bitmap);
    HeapWord* bytemap_base = GPGC_Layout::PageNum_to_addr(GPGC_Layout::start_of_cardmark_bytemap);

    shl8i  (Rtmp0,64-objectRef::unknown_bits);
    shr8i  (Rtmp0,64-objectRef::unknown_bits+ LogBytesPerWord); // Rtmp0 = bit index in cardmark bitmap
    mov8i  (Rtmp1,intptr_t(bitmap_base)); // Rtmp1 = bitmap base
    btx8   (Rtmp1,0,Rtmp0);               // Test bit Rtmp0 in bitmap at addr Rtmp1
    jbl    (safe_to_store);               // bit already set, jump to store code.
    locked()->bts8(Rtmp1,0,Rtmp0);        // Atomic set bit Rtmp0 in bitmap at addr Rtmp1
    mov8i  (Rtmp1,intptr_t(bytemap_base));// Rtmp1 = bytemap base
    shr8i  (Rtmp0,LogWordsPerGPGCCardMarkSummaryByte); // Rtmp0 = byte index in summary bytemap
    st1i   (Rtmp1,0,Rtmp0,0,1);           // Store 1 into byte at Rtmp1[Rtmp0]
  } else if (bs->kind() == BarrierSet::CardTableModRef) {
    CardTableModRefBS* ct = (CardTableModRefBS*)bs;
    assert(sizeof(*ct->byte_map_base) == sizeof(jbyte), "adjust this code");
    // I assume we use precise cardmarks, even for plain objects.
    assert0(ct->precision()==ct->Precise);
    shl8i  (Rtmp0,64-objectRef::unknown_bits);
    shr8i  (Rtmp0,64-objectRef::unknown_bits+ CardTableModRefBS::card_shift);
    // Filtered cardmark: do not over-mark.  Repeated storing of zero
    // by many cpus forces the cache-line to ping-pong around.  It's
    // cheaper to test-then-set instead of unconditionally writing in
    // the bad case (but makes the normal-case slightly slower).
    assert0(is_int32((intptr_t)ct->byte_map_base));
    add8i  (Rtmp0, (long)ct->byte_map_base);
    cmp1i  (Rtmp0, 0 , 0);
    jeq    (safe_to_store); // Already marked
    st1i   (Rtmp0, 0 ,0);
  } else {
    should_not_reach_here("No barriers defined");
  }
bind(safe_to_store);
}


// --- call_VM_native
// call_VM implementation for native-wrapper and compiled runtime calls.
// Normal interpreter call_VM's need to check for a pending exception on exit,
// and they set last_Java_sp on entry (to allow stack crawls).  They retain
// their jvm_lock.  Natives set _jvm_lock to free it on entry, and must
// reclaim it on exit.  The PC/RSP setup is the same between them.  No
// interpreter registers are saved or restored here.
//
// Thread is set on entry, all arguments are in the standard C ABI -
// RDI, RSI, RDX, RCX, R08.  Caller's saved values are in RBX & R12-R15.
// This routine can blow R09, R10, R11.
//
// Does NOT do stack-alignment to a 16b boundary.  That is done by I2C adapters.
int MacroAssembler::call_native(address entry_point, Register THR, int thr_offset, Label *nativefuncaddrpc) {
  // NOTE: If ITR is enabled, the right number of instructions must be provided
  // to set_mstate's 3rd parameter 'offset'
  if (UseITR) Unimplemented();

  // Slap out the post-call PC into TLS for GC to find.
Label retpc;
  st8i(THR, in_bytes(JavaThread::last_Java_pc_offset()), retpc);
  // Writing to the jvm_lock frees it, allowing a GC cycle instantly.  We must
  // fence out the PC before unlocking - lest the GC thread get the jvm_lock
  // and read a stale PC.  No fencing needed on X86 - strong memory model.
  st8 (THR, thr_offset, RSP);
  if (nativefuncaddrpc) { // label provided (ie JNI call) align mov8i in case register_native wants to modify target
    bind(*nativefuncaddrpc);
    align(8);
    mov8i(R11, (intptr_t)entry_point);
    call (R11);                 // Call by register to native code    
  }
  else {
    if( is_int32(entry_point- (address)_blob) ) {
      call(entry_point);          // Call by displacement
    } else {
      mov8i(R11, (intptr_t)entry_point);
      call (R11);                 // Call by register
    }
  }
  bind(retpc);                  // Call returns here
  return retpc.rel_pc(this);
}


// --- call_VM_compiled
// Call into the VM from compiled code.  Preserves ALL registers (except RAX &
// F00), and allows for GC and deopt on values in registers.  Assumes aligned
// stack.  Will pass RDI as Thread; other args should be placed in RSI, RDX,
// etc as normal for C++ calls. Optional stackArgs can be loaded from the stack
// from immediately below the return PC address.
int MacroAssembler::call_VM_compiled(address entry_point, uint32_t save_mask, Register arg1, Register arg2, Register arg3, Register arg4, int stackArgs) {
  assert0( (31-RAX)*8 == frame::xRAX ); // offset math is wrong
  assert0( (31-R15)*8 == frame::xR15 ); // offset math is wrong
  assert0( (31-F00)*8 == frame::xF00 ); // offset math is wrong
  assert0( (31-F15)*8 == frame::xF15 ); // offset math is wrong

  // We get 'call'd here, so we expect a return address already on the stack
  // (and the stack was 16b aligned before the call)
  save_mask &= ~(1<<RSP);       // Do not save RSP on the stack
  add8i(RSP,-(frame::runtime_stub_frame_size-8));

  if (stackArgs == 0) {
    // Save all requested GPR registers
for(int i=0;i<16;i++)
      if( (1<<i)&save_mask )
        st8(RSP,(31-i)*8,( Register)i);
  } else {
assert(stackArgs<=2,"only support for upto 2 stack args currently");
assert(arg1==noreg,"stack args don't support also passing in regs");
    // Save all requested GPR registers except the stack slots with incoming args
    for( int i = stackArgs-1; i<16; i++ )
      if( (1<<i)&save_mask )
        st8(RSP,(31-i)*8,( Register)i);
    // Load incoming args and save requested GPR registers
    arg1 = RSI;
    ld8(arg1,RSP,frame::xPAD); // no store necessary as xPAD is crud
    if (stackArgs > 1) {
      arg2 = RDX;
      ld8(arg2,RSP,frame::xRAX);
      if( (1<<RAX)&save_mask )
        st8(RSP,frame::xRAX,RAX);
    }
  }
  // Save all requested FPR registers
for(int i=16;i<32;i++)
    if( (1<<i)&save_mask )
      st8(RSP,(31-i)*8,(FRegister)i);

  // Shuffle any args
  if( arg1 == retadr_arg ) { 
    ld8(RSI,RSP,frame::xRetPC); // Load return address as arg1
    arg1 = RSI;                 // arg1 is now in RSI
    save_mask &= ~(1<<RAX);     // Save but do not restore RAX; we need the return value
  }
  if( arg2 == retadr_arg ) {    // Special case for uncommon_trap stub
    save_mask &= ~(1<<RAX);     // Save but do not restore RAX; we need the return value
  }
  if( arg4 != noreg ) { move8(R08,arg4); arg4 = R08; }
  if( arg3 != noreg ) { move8(RCX,arg3); arg3 = RCX; }
  if( arg1 == RDX ) {
    // swapped args; must use a temp?
    if( arg2 == RSI   ) { mov8(RAX,RSI ); arg2 = RAX; } 
    move8(RSI,arg1); arg1 = RSI;
  }
  if( arg2 != noreg && arg2 != retadr_arg ) move8(RDX,arg2);
  if( arg1 != noreg ) move8(RSI,arg1);
  getthr(RDI);                  // arg0 for the call
  // We do NOT set RBP for the caller: the caller will save/restore RBP and
  // set his own frame-pointer.  If we must set RBP here, then we must also
  // save/restore it for OUR caller.  Leaving it alone keeps the value correct
  // for our caller.  Stack crawls do no need it, because they grok the
  // runtime stub frame directly.

  // Slap out the post-call PC into TLS for GC to find.
Label retpc;
  st8i(RDI, in_bytes(JavaThread::last_Java_pc_offset()), retpc);
  // Prepare for a stack crawl (but do not allow one immediately - the
  // _jvm_lock is not freed so a GC can't happen yet).  These two stores must
  // happen in-order.  No fencing needed on X86 - strong memory model.
  st8 (RDI, in_bytes(JavaThread::last_Java_sp_offset()), RSP);

  // Do the call
  if( is_int32(entry_point- (address)_blob) ) {
    call(entry_point);          // Call by displacement to C++ code
  } else {
    mov8i(RAX, (intptr_t)entry_point);
    call (RAX);                 // Call by register to C++ code    
  }
  bind(retpc);                  // Call returns here
  add_empty_oopmap(rel_pc());   // Known empty oop-map.  Stack crawls understand runtime stub frames

  // Restore all requested GPR registers
for(int i=0;i<16;i++)
    if( (1<<i)&save_mask )
      ld8(( Register)i, RSP,(31-i)*8);
  // Restore all requested FPR registers
for(int i=16;i<32;i++)
    if( (1<<i)&save_mask )
      ld8((FRegister)i, RSP,(31-i)*8);

  add8i(RSP, (frame::runtime_stub_frame_size-8));
  return retpc.rel_pc(this);
}


// --- call_VM_plain
// Call into the VM with no exception check on return.
void MacroAssembler::call_VM_plain(address entry_point, Register arg1, Register arg2, Register arg3, Register arg4, Register arg5 ) {
  if( UseITR ) Unimplemented();

  // now setup call args
  // RDI - C arg0 - Thread*
  // RSI - C arg1 - arg1 passed in
  // RDX - C arg2 - arg2 passed in
  // RCX - C arg3 - arg3 passed in
  assert0( arg2 != RSI && arg3 != RSI && arg4 != RSI && arg5 != RSI );
  assert0(                arg3 != RDX && arg4 != RDX && arg5 != RDX );
  assert0(                               arg4 != RCX && arg5 != RCX );
  assert0(                                              arg5 != R08 );
  if( arg1 != noreg ) move8(RSI,arg1);
  if( arg2 != noreg ) move8(RDX,arg2);
  if( arg3 != noreg ) move8(RCX,arg3);
  if( arg4 != noreg ) move8(R08,arg4);
  if( arg5 != noreg ) move8(R09,arg5);
  getthr(RDI);                  // arg0 for the call
  call_native(entry_point,RDI,in_bytes(JavaThread::last_Java_sp_offset()),NULL);
}

// --- call_VM_leaf
// No stack crawls; just standard C code.  Blows standard C registers,
// including RCX_TOS, RDX, RSI_BCP & R08_CPC.
// Passed arg moves into RDI.  No thread passed by default.
void MacroAssembler::call_VM_leaf(address adr, Register arg0, Register arg1, Register arg2, Register arg3, Register arg4, Register arg5 ) {
  // check arg setup won't blow args being passed
  assert0( arg1 != RDI && arg2 != RDI && arg3 != RDI && arg4 != RDI && arg5 != RDI );
  assert0(                arg2 != RSI && arg3 != RSI && arg4 != RSI && arg5 != RSI );
  assert0(                               arg3 != RDX && arg4 != RDX && arg5 != RDX );
  assert0(                                              arg4 != RCX && arg5 != RCX );
  assert0(                                                             arg5 != R08 );
  // setup args
  if( arg0 != noreg ) move8(RDI,arg0);
  if( arg1 != noreg ) move8(RSI,arg1);
  if( arg2 != noreg ) move8(RDX,arg2);
  if( arg3 != noreg ) move8(RCX,arg3);
  if( arg4 != noreg ) move8(R08,arg4);
  if( arg5 != noreg ) move8(R09,arg5);

call(adr);
}

// --- fast_path_lock
// Fast-path lock attempt.  Does not mod 'ref'.  Blows RAX and the
// temps.  May fall into the slow-path or branch to the fast_locked.
// Sets RAX to the loaded markWord on the slow-path.
// Sets R09 to the shifted thread-id on the slow-path.
int MacroAssembler::fast_path_lock( RInOuts, Register tmp0, Register Ro9, Register Rax, RKeepIns, Register Rref, Label &fast_locked ) {
  assert0( Rax == RAX );
  assert0( Ro9 == R09 );
  assert_different_registers(RAX,R09,tmp0,Rref);
Label slow_lock;
  // Lock the object. 
#ifdef ASSERT
  if( RefPoisoning ) {
    Label ok;
    btx8i(Rref,objectRef::reserved_shift); // Verify ref has poison by stuffing bit 60 into carry flag
    jnb  (ok);        // Jump if carry clear: not poisoned
    should_not_reach_here("did not remove poison before lock attempt");
    bind (ok);
  }
#endif // !ASSERT
  if( UseSBA ) {
Label load_mark_word;
    // Test for stack objects; they never inflate and are pre-biased
    assert0( objectRef::stack_space_id == 0x01 ); // change code to match!!!
    btx8i(Rref,objectRef::space_shift+0); // Set CF from stack-space-id bit0
    jnb  (load_mark_word);        // Jump if CF is clear, ie. space-id bit pattern is X0
    btx8i(Rref,objectRef::space_shift+1); // Set CF from stack-space-id bit1
    jnb  (fast_locked);           // Jump if CF is clear, ie. space-id bit pattern is 01
bind(load_mark_word);
  }
  cvta2va(tmp0, Rref);          // Strip metadata
  // load markWord; also does a null-check on tmp0/Rref
  int null_chk_offset = rel_pc();
  ld8  (Rax,tmp0,oopDesc::mark_offset_in_bytes()); 
  gettid(R09);                  // load thread-id
  shl8i(R09,markWord::hash_shift);
  cmp4 (Rax,R09);               // Check for biased-to-self
  jeq  (fast_locked);           // Yeah, biased to self!
  test4(Rax,Rax);               // Check for unlocked
  jne  (slow_lock); // Oops - locked for somebody else (or heavyweight locked or hashed)
  // Unlocked!
  or_8 (R09,Rax);        // Set R09 to the markWord OR'd with ThreadID
  // Atomically compare Rax vs [tmp0+mark_offset], if equal set R09 into [tmp0+mark_offset]
  locked()->cas8(tmp0,oopDesc::mark_offset_in_bytes(),R09);
  jeq  (fast_locked);
  // CAS failed.  Rax holds the reloaded memory word
  or_4 (R09,R09); // zero out hi order markWord bits: low bits holds shifted TID
bind(slow_lock);
  // RAX : markWord from lock attempt
  // R09 : shifted thread ID
  // Rref: objectRef to lock
  return null_chk_offset;
}


// --- fast_path_unlock
// Fast-path unlock attempt.  Strips 'ref'.  May fall into the fast-path
// or branch to the slow_unlock.  Loads Rmark for the slow-path.
void MacroAssembler::fast_path_unlock( RInOuts, Register Rref, Register Rmark, Register Rtmp, Label &slow_unlock ) {
  assert_different_registers( Rref, Rtmp );
  Label load_mark_word, fast_unlocked;
#ifdef ASSERT
  if( RefPoisoning ) {
    Label ok;
    btx8i(Rref,objectRef::reserved_shift); // Verify ref has poison by stuffing bit 60 into carry flag
    jnb  (ok);                  // Jump if carry clear: not poisoned
    should_not_reach_here("did not remove poison before unlock attempt");
    bind (ok);
  }
#endif // !ASSERT
  if( UseSBA ) {
    // Test for stack objects; they never inflate and are pre-biased
    assert0( objectRef::stack_space_id == 0x01 ); // change code to match!!!
    btx8i(Rref,objectRef::space_shift+0); // Set CF from stack-space-id bit0
    jnb  (load_mark_word);        // Jump if CF is clear, ie. space-id bit pattern is X0
    btx8i(Rref,objectRef::space_shift+1); // Set CF from stack-space-id bit1
    jnb  (fast_unlocked);         // Jump if CF is clear, ie. space-id bit pattern is 01
  }
bind(load_mark_word);
  gettid(Rtmp);                 // load thread-id
  shl8i (Rtmp,markWord::hash_shift);
  cvta2va(Rref);
  ldz4  (Rmark,Rref,oopDesc::mark_offset_in_bytes()); 
  cmp4  (Rtmp,Rmark);
  jne   (slow_unlock); // slow-path!
bind(fast_unlocked);
}


// --- check_and_handle_popframe
void MacroAssembler::check_and_handle_popframe(){
  Unimplemented();
}


// --- pushall
// Push all 15 regs, not SP, not flags, not floats
void MacroAssembler::pushall(){
push(RAX);
push(RCX);
push(RDX);
push(RBX);
  //push(RSP);
push(RBP);
push(RSI);
push(RDI);
push(R08);
push(R09);
push(R10);
push(R11);
push(R12);
push(R13);
push(R14);
push(R15);
}

// --- popall
// Pop all 15 regs, not SP, not flags, not floats
void MacroAssembler::popall(){
  pop(R15);
  pop(R14);
  pop(R13);
  pop(R12);
  pop(R11);
  pop(R10);
  pop(R09);
  pop(R08);
  pop(RDI);
  pop(RSI);
pop(RBP);
  //pop(RSP);
  pop(RBX);
  pop(RDX);
  pop(RCX);
pop(RAX);
}

// --- stop_helper
// String to print just follows the return address in the caller's code
void MacroAssembler::stop_helper(const char*msg){
  if (ShowMessageBoxOnError) {
    if (!os::message_box("Internal Error (assembler code)", msg, "Do you want to debug?")) {
      os::abort();
    }
  } else {
tty->print_cr("#\n# HotSpot Internal Error (assembler code): %s",msg);
tty->flush();
  }
}

// --- stop
// Gen code to print a message & break; if restarted the program continues
// with no registers clobbered.  'msg' is permanent (leaked) C heap storage.
void MacroAssembler::stop( const char *msg ) {
  pushf();                      // Push flags
  pushall();                    // Push all registers
  mov8i(RDI,msg);               // Pass message into RDI
call(CAST_FROM_FN_PTR(address,MacroAssembler::stop_helper));
  popall();                     // Pop all registers
  popf();                       // Pop flags
  os_breakpoint();              // Wait for debugger
}

// --- verify_not_null_oop
// stop if specified register is null or not an oop, blows flags
void MacroAssembler::verify_not_null_oop(Register reg,OopVerificationReason reason){
if(should_verify_oop(reason)){
Label not_null;
test8(reg,reg);
    jnz  (not_null);
stop("non-null oop expected");
    bind (not_null);
    verify_oop(reg, reason);
  }
}

// --- verify_oop ------------------------------------------------------------
void MacroAssembler::verify_oop(Register oop, OopVerificationReason reason) {
if(should_verify_oop(reason)){
    // For performance reasons, avoid verification if it appears this register
    // was just verified. This is a quick a dirty test that isn't thread safe.
    // In any eventuality it doesn't matter if we falsely miss or place a test,
    // and the likelihood of this is low.
    static address  _pc_at_last_verify_oop = 0;
    static Register _reg_at_last_verify_oop = noreg;
    if (_pc_at_last_verify_oop == _pc && _reg_at_last_verify_oop == oop) {
      return;
    }
    // Place oop in correct register and call
    if (oop != RAX) xchg8(RAX, oop);
    call (StubRoutines::verify_oop_subroutine_entry());
    if (oop != RAX) xchg8(RAX, oop);
    // Record information about last register verified
    _reg_at_last_verify_oop = oop;
_pc_at_last_verify_oop=_pc;
  }
}

void MacroAssembler::verify_oop(VOopReg::VR oop, OopVerificationReason reason) {
if(should_verify_oop(reason)){
    if (VOopReg::is_stack(oop)) {
      xchg (RAX, RSP, VOopReg::reg2stk(oop));
      verify_oop(RAX, reason);
      xchg (RAX, RSP, VOopReg::reg2stk(oop));
    } else {
      verify_oop((Register)oop, reason);
    }
  }
}

int MacroAssembler::verify_oop(Register Rbase, int off, Register Rindex, int scale, OopVerificationReason reason) {
if(should_verify_oop(reason)){
    int regs = (1<<Rbase);
    if( Rindex != noreg ) regs |= (1<<Rindex);
    const Register Rtmp1 = Register(find_lowest_bit(~regs)); // find first free reg
push(Rtmp1);
    regs |= (1<<Rtmp1);
    const Register Rtmp2 = Register(find_lowest_bit(~regs)); // find first free reg
push(Rtmp2);
    int npeoff = rel_pc(); // offset for NPE info
    // NB An LVB trap may see an uncrawlable stack here (due to pushes)
    ldref_lvb( RInOuts::a, Rtmp1, Rtmp2, RKeepIns::a, Rbase, off, Rindex, scale, true );
    verify_oop(Rtmp1, reason);
    pop  (Rtmp2);
pop(Rtmp1);
    // restore registers
    return npeoff;
  } else {
    return 0;
  }
}

// Small helper function to try and verify that we're properly LVB'ing the klassRefs of objectRefs.
void MacroAssembler::verify_ref_klass(Register oop){
  // Place oop in correct register and call
  if (oop != RAX) xchg8(RAX, oop);
  call (StubRoutines::verify_ref_klass_subroutine_entry());
  if (oop != RAX) xchg8(RAX, oop);
}

// --- verify_kid
// Verify KID is in range
void MacroAssembler::verify_kid(Register Rkid){
#ifdef ASSERT
  Label valid_kid, fail;
  cmp4i (Rkid, booleanArrayKlass_kid);
  jlt  (fail);
  address max_klass_id_addr = (address)KlassTable::max_klassId_Addr();
  if(is_int32((int64_t)max_klass_id_addr)) {
    cmp4(Rkid, max_klass_id_addr);
  } else {
    Register Rtmp = ( Rkid == RAX ) ? RBX : RAX;
push(Rtmp);
    mov8i(Rtmp, (int64_t)max_klass_id_addr);
    cmp4(Rkid, Rtmp, 0);
    pop(Rtmp);
  }
  jle  (valid_kid);
bind(fail);
stop("Unexpected KID value");
bind(valid_kid);
#endif
  return;
}

class VerifyOopMapClosure:public BitMapClosure{
 private:
   MacroAssembler *_masm;
   MacroAssembler::OopVerificationReason _reason;
 public:
   VerifyOopMapClosure(MacroAssembler *masm, MacroAssembler::OopVerificationReason reason) : _masm(masm), _reason(reason) {}

  // Callback when bit in map is set
  virtual void do_bit(size_t offset) {
    _masm->verify_oop((VOopReg::VR)offset, _reason);
  }
};

// Verify a collection of oops
void MacroAssembler::verify_oopmap(const OopMap2 *map, OopVerificationReason reason ) {
if(should_verify_oop(reason)){
    const BitMap *d = &map->_data;
    if( !_oopmaps ) _oopmaps = new OopMapBuilder(d->size());
if(!d->is_empty()){
      VerifyOopMapClosure oop_verifier(this, reason);
      d->iterate(&oop_verifier);
    }
  }
}

// --- make_ic_label ---------------------------------------------------------
// Make an inline-cache-failed branch target.
// Cleaned up when 'baking' into a CodeBlob.
void MacroAssembler::make_ic_slowpath( Label *die ) {
  _ic_slow .push(die);
  Label *Ldebug = new Label(this); // Bind debug info here (just after IC call)
  _ic_debug.push(Ldebug);
}

// --- pre_bake --------------------------------------------------------------
// Clean-up inline-cache-failed branch targets
void MacroAssembler::pre_bake(){
for(int i=0;i<_ic_slow.length();i++){
    // We jump here if an inline-cache fails.
    // We then forward on to resolve_and_patch_call
    Label *l = _ic_slow.at(i);
    bind (*l);
    Label *ldbg = _ic_debug.at(i);
    mov8i(RAX, *ldbg);          // push a return address
push(RAX);
    jmp  (StubRoutines::resolve_and_patch_call_entry());
  }

  // For C1 & C2, append a deopt sled
  if( _type == CodeBlob::c1 || _type == CodeBlob::c2 ) {
    _deopt_sled_relpc = rel_pc();
    add8i(RSP,-8);              // Re-push the return address
    jmp(StubRoutines::deopt_asm_entry());
    os_breakpoint();            // Wait for debugger
  }
}

// --- oop_from_OopTable -----------------------------------------------------
// Recovering klassRef from kid or object from oid
void MacroAssembler::oop_from_OopTable(RInOuts, Register dst, Register tmp, int idx) {
assert(idx!=0,"unexpected load of null from oop table");
  intptr_t low_adr = (intptr_t)KlassTable::getKlassTableBase()+(idx<<3);
  ldref_lvb(RInOuts::a, dst, tmp, RKeepIns::a, noreg, low_adr, false);
  verify_not_null_oop(dst, OopVerify_OopTableLoad);
  record_constant_oop(idx);
}

// --- oop_from_OopTable -----------------------------------------------------
// Recovering klassRef from kid or object from oid
void MacroAssembler::oop_from_OopTable(RInOuts, Register dst, Register tmp, RKeepIns, Register idx) {
  intptr_t low_adr = (intptr_t)KlassTable::getKlassTableBase();
  ldref_lvb(RInOuts::a, dst, tmp, RKeepIns::a, noreg, low_adr, idx, 3, false);
  verify_oop(dst, OopVerify_OopTableLoad);
}

// --- kid2klass
// Loads the klassRef by looking up the KID in the KlassTable.  Using
// the KlassTable avoids the mandatory cache-miss on loading from the
// object header word.
void MacroAssembler::kid2klass(RInOuts, Register dst, Register tmp, RKeepIns, Register kid) {
  intptr_t ktb_adr = (intptr_t)KlassTable::getKlassTableBase();
  ldref_lvb(RInOuts::a, dst, tmp, RKeepIns::a, noreg, ktb_adr, kid, 3, true/*a null KID produces a NULL klass*/);
  verify_oop(dst, OopVerify_OopTableLoad);
}

// --- kid2klass
// Loads the klassRef by looking up the KID in the KlassTable.  Using
// the KlassTable avoids the mandatory cache-miss on loading from the
// object header word.
void MacroAssembler::kid2klass(RInOuts, Register dst, Register tmp, int kid) {
  assert0 (KlassTable::is_valid_klassId(kid));
  intptr_t ktb_adr = (intptr_t)KlassTable::getKlassTableBase()+(kid<<3);
  ldref_lvb(RInOuts::a, dst, tmp, RKeepIns::a, noreg, ktb_adr, true/*a null KID produces a NULL klass*/);
  verify_not_null_oop(dst, OopVerify_OopTableLoad);
  record_constant_oop(kid);
}

// --- ref2klass
// Loads objectRef's klassRef by looking up the objectRef KID in the
// KlassTable.  Using the KlassTable avoids the mandatory cache-miss
// on loading from the object header word.
//
// Input:
//   klass_ref: unused
//   ref:       objectRef from which to load a klassRef
// Output:
//   klass_ref: klassRef of the input objectRef
//   ref:       KID of the input objectRef
void MacroAssembler::ref2klass(RInOuts, Register klass_ref, Register tmp, Register ref) {
  ref2kid(ref);
  kid2klass(RInOuts::a, klass_ref, tmp, RKeepIns::a, ref);
}

void MacroAssembler::ref2klass_no_npe(RInOuts, Register klass_ref, Register tmp, Register ref) {
  ref2kid_no_npe(ref, ref);
  kid2klass(RInOuts::a, klass_ref, tmp, RKeepIns::a, ref);
}

// -- compare_klasses
// Compare 2 KIDs, setting flags.
void MacroAssembler::compare_klasses(RInOuts, Register scratch, RKeepIns, Register ref1, Register ref2) {
  if( KIDInRef ) {
    mov8   (scratch, ref1);
    xor8   (scratch, ref2);
    ref2kid(scratch);
  } else if( MultiMapMetaData ) {
    ldz4   (scratch, ref1,oopDesc::mark_offset_in_bytes()+4);
    xor4   (scratch, ref2,oopDesc::mark_offset_in_bytes()+4);
    shr4i  (scratch, markWord::kid_shift-32);
  } else {
    mov8   (scratch, ref1);
    cvta2va(scratch);
    ldz4   (scratch, scratch,oopDesc::mark_offset_in_bytes()+4);
push(ref2);
    cvta2va(ref2);
    xor4   (scratch, ref2   ,oopDesc::mark_offset_in_bytes()+4);
    shr4i  (scratch, markWord::kid_shift-32);
pop(ref2);
  }
}


// --- corrected_idiv4 -------------------------------------------------------
int MacroAssembler::corrected_idiv4(Register reg){
  // Full implementation of Java idiv and irem; checks for special
  // case as described in JVM spec., p.243 & p.271.  The function
  // returns the (pc) offset of the idivl instruction - may be needed
  // for implicit exceptions.
  //
  //         normal case                           special case
  //
  // input : eax: dividend                         min_int
  //         reg: divisor   (may not be eax/edx)   -1
  //
  // output: eax: quotient  (= eax idiv reg)       min_int
  //         edx: remainder (= eax irem reg)       0
assert(reg!=RAX&&reg!=RDX,"reg cannot be rax or rdx register");
  const int min_int = 0x80000000;
  Label normal_case, special_case;
  
  // check for special case
  cmp4i(RAX, min_int);
  jne(normal_case);
  xor4(RDX, RDX); // prepare edx for possible special case (where
                  // remainder = 0)
cmp4i(reg,-1);
  jeq(special_case);

  // handle normal case
  bind(normal_case);
  cdq4();
int idivl_offset=rel_pc();
div4(reg);

  // normal and special case exit
  bind(special_case);

  return idivl_offset;
}

int MacroAssembler::corrected_idiv8(Register reg){
  // Full implementation of Java ldiv and lrem; checks for special
  // case as described in JVM spec., p.243 & p.271.  The function
  // returns the (pc) offset of the idivl instruction - may be needed
  // for implicit exceptions.
  //
  //         normal case                           special case
  //
  // input : rax: dividend                         min_jlong
  //         reg: divisor   (may not be eax/edx)   -1
  //
  // output: rax: quotient  (= rax idiv reg)       min_jlong
  //         rdx: remainder (= rax irem reg)       0
assert(reg!=RAX&&reg!=RDX,"reg cannot be rax or rdx register");
  address min_jlong_addr = long_constant(min_jlong);
assert(min_jlong_addr!=NULL,"min jlong address required");
  Label normal_case, special_case;
  
  // check for special case
  cmp8(RAX, min_jlong_addr);
  jne(normal_case);
  xor4(RDX, RDX); // prepare rdx for possible special case (where
                  // remainder = 0)
cmp8i(reg,-1);
  jeq(special_case);

  // handle normal case
  bind(normal_case);
  cdq8();
  int idivq_offset = rel_pc();
div8(reg);

  // normal and special case exit
  bind(special_case);

  return idivq_offset;
}


static const jfloat jfloat_constants[] ={
    0.0f,        // used by C2 for FP compare
    1.0f, 2.0f,  // cases where bytecodes exist
    0.5f,        // somewhat popular
    (jfloat)max_jint,   // used for f2i
    (jfloat)max_jlong,  // used for f2l
};

address MacroAssembler::float_constant(jfloat constant) {
  jint as_int_bits = jint_cast(constant);
  for (uint i=0; i < sizeof(jfloat_constants)/sizeof(jfloat_constants[0]); i++) {
    if (as_int_bits == jint_cast(jfloat_constants[i])) {
return(address)&jfloat_constants[i];
    }
  }
  return NULL;
}

// Generate float constant into register
void MacroAssembler::float_constant(FRegister dest, jfloat constant, Register scratch) {
  jint as_int_bits = jint_cast(constant);
  if (as_int_bits == 0) {
    xorf(dest, dest);
  } else {
    address adr = float_constant(constant);
if(adr!=NULL){
      ld4 (dest, adr);
    } else {
      assert0 (scratch != noreg);
      mov8u(scratch, as_int_bits);
      mov4 (dest, scratch);
    }
  }
}

static const jdouble jdouble_constants[] ={
    1.0, 2.0, // cases where bytecodes exist
    0.5, // somewhat popular
    ((jdouble)max_jint)+0.5,   // used for d2i
    ((jdouble)max_jlong)+0.5,  // used for d2l
};

address MacroAssembler::double_constant(jdouble constant) {
  jlong as_long_bits = jlong_cast(constant);
  for (uint i=0; i < sizeof(jdouble_constants)/sizeof(jdouble_constants[0]); i++) {
    if (as_long_bits == jlong_cast(jdouble_constants[i])) {
return(address)&jdouble_constants[i];
    }
  }
  return NULL;
}

// Generate double constant into register
void MacroAssembler::double_constant(FRegister dest, jdouble constant, Register scratch) {
  jlong as_long_bits = jlong_cast(constant);
  if (as_long_bits == 0) {
    xord(dest, dest);
  } else {
    address adr = double_constant(constant);
if(adr!=NULL){
      ld8 (dest, adr);
    } else {
      assert0 (scratch != noreg);
      mov8i(scratch, (int64_t)as_long_bits);
      mov8 (dest, scratch);
    }
  }
}

#define MAX_QUADWORD_CONSTANTS 4
static const jlong quadword_constants[MAX_QUADWORD_CONSTANTS*2] ={
    CONST64(0x7FFFFFFF7FFFFFFF), CONST64(0x7FFFFFFF7FFFFFFF), // absf mask
    CONST64(0x8000000080000000), CONST64(0x8000000080000000), // negf mask
    CONST64(0x7FFFFFFFFFFFFFFF), CONST64(0x7FFFFFFFFFFFFFFF), // absd mask
    CONST64(0x8000000000000000), CONST64(0x8000000000000000), // negd mask
};

// Get 16byte aligned quadword constant's address or NULL
address MacroAssembler::quadword_constant(jlong lo, jlong hi) {
for(int i=0;i<MAX_QUADWORD_CONSTANTS;i++){
    if ((lo == quadword_constants[i*2]) && (hi == quadword_constants[(i*2)+1])) {
      address result = (address)&quadword_constants[i*2];
      assert (((intptr_t)result&7) == 0, "unaligned quadword")
      return result;
    }
  }
  return NULL;
}

address MacroAssembler::long_constant(jlong constant) {
  for (int i=0; i < MAX_QUADWORD_CONSTANTS*2; i++) {
    if (constant == quadword_constants[i]) {
      address result = (address)&quadword_constants[i];
      return result;
    }
  }
  return NULL;
}

// push value onto x87 stack
void Assembler::x87_ld4  (Register base, int off) {
  // Intel mnemonic: fld m32fp
  emit1(0xD9);
  emit_regbaseoff((Register)0,base,off);
}
// push value onto x87 stack
void Assembler::x87_ld8  (Register base, int off) {
  // Intel mnemonic: fld m64fp
  emit1(0xDD);
  emit_regbaseoff((Register)0,base,off);
}
// pop value from x87 stack
void Assembler::x87_st4p (Register base, int off) {
  // Intel mnemonic: fstp m32fp
  emit1(0xD9);
  emit_regbaseoff((Register)3,base,off);
}
// pop value from x87 stack
void Assembler::x87_st8p (Register base, int off) {
  // Intel mnemonic: fstp m64fp
  emit1(0xDD);
  emit_regbaseoff((Register)3,base,off);
}
// partial remainder
void Assembler::x87_prem(){
  // Intel mnemonic: fprem
  emit1(0xD9);
  emit1(0xF8);
}
// store status word
void Assembler::x87_stsw (Register base, int off) {
  // Intel mnemonic: fstsw m2byte
  emit1(0x9B);
  emit1(0xDD);
  emit_regbaseoff((Register)7,base,off);
}
// undocumented free stack and pop operation
void Assembler::x87_freep(){
  // Intel mnemonic: ffreep ST(0)
  emit1(0xDF);
  emit1(0xC0+0 /* ST(0) */);
}

// push log_10(2) onto x87 stack
void Assembler::x87_fldlg2(){
  emit1(0xD9);
  emit1(0xEC);
}

// push log_e(2) onto x87 stack
void Assembler::x87_fldln2(){
  emit1(0xD9);
  emit1(0xED);
}

// st(1):= st(1)*log_2(st(0)); pop st(0)
void Assembler::x87_fyl2x(){
  emit1(0xD9);
  emit1(0xF1);
}

void Assembler::cpuid() {
  emit1(0x0F);
  emit1(0xA2);
}

// 32-bit floating point remainder using x87, destroys the contents of SP-8
void MacroAssembler::remf(FRegister dst,FRegister src){
  st4      (RSP,-8, src);
  x87_ld4  (RSP,-8);     // ST(0) == src
  st4      (RSP,-8, dst);
  x87_ld4  (RSP,-8);     // ST(0) == dst, ST(1) == src
  Label retry(this);
  x87_prem ();           // ST(0)  := ST(0) % ST(1)
  x87_stsw (RSP,-8);     // [SP-8] := status word
  and4i    (RSP,-8, 0x400);
  jne      (retry);      // check remainder is complete
  x87_st4p (RSP,-8);     // pop result
  x87_freep();           // pop x87
  ld4      (dst, RSP,-8);// load result
}

// 64-bit floating point remainder using x87, destroys the contents of SP-8
void MacroAssembler::remd(FRegister dst,FRegister src){
  st8      (RSP,-8, src);
  x87_ld8  (RSP,-8);     // ST(0) == src
  st8      (RSP,-8, dst);
  x87_ld8  (RSP,-8);     // ST(0) == dst, ST(1) == src
  Label retry(this);
  x87_prem ();           // ST(0)  := ST(0) % ST(1)
  x87_stsw (RSP,-8);     // [SP-8] := status word
  and4i    (RSP,-8, 0x400);
  jne      (retry);      // check remainder is complete
  x87_st8p (RSP,-8);     // pop result
  x87_freep();           // pop x87
  ld8      (dst, RSP,-8);// load result
}

// 64-bit floating point log using x87, destroys the contents of SP-8
void MacroAssembler::flog(FRegister dst,FRegister src){
  st8      (RSP,-8, src);
  x87_fldln2();          // ST(0) == log_e(2)
  x87_ld8  (RSP,-8);     // ST(0) == src, ST(1) == log_e(2)
  x87_fyl2x();           // ST(1):= ST(1)*log_2(ST(0)); pop ST(0)
  x87_st8p (RSP,-8);     // pop result
  ld8      (dst, RSP,-8);// load result
}

// 64-bit floating point log10 using x87, destroys the contents of SP-8
void MacroAssembler::flog10(FRegister dst,FRegister src){
  st8      (RSP,-8, src);
  x87_fldlg2();          // ST(0) == log_10(2)
  x87_ld8  (RSP,-8);     // ST(0) == src, ST(1) == log_10(2)
  x87_fyl2x();           // ST(1):= ST(1)*log_2(ST(0)); pop ST(0)
  x87_st8p (RSP,-8);     // pop result
  ld8      (dst, RSP,-8);// load result
}

// 32-bit floating point negate using SSE xor
void MacroAssembler::negf(FRegister dst){
  address addr = quadword_constant(CONST64(0x8000000080000000), CONST64(0x8000000080000000));
assert(addr!=NULL,"missing negate mask");
  xorf (dst, addr);
}

// 64-bit floating point negate using SSE xor
void MacroAssembler::negd(FRegister dst){
  address addr = quadword_constant(CONST64(0x8000000000000000), CONST64(0x8000000000000000));
assert(addr!=NULL,"missing negate mask");
  xord (dst, addr);
}

// 32-bit floating point absolute using SSE xor
void MacroAssembler::absf(FRegister dst){
  address addr = quadword_constant(CONST64(0x7FFFFFFF7FFFFFFF), CONST64(0x7FFFFFFF7FFFFFFF));
assert(addr!=NULL,"missing abs mask");
  andf (dst, addr);
}

// 64-bit floating point absolute using SSE xor
void MacroAssembler::absd(FRegister dst){
  address addr = quadword_constant(CONST64(0x7FFFFFFFFFFFFFFF), CONST64(0x7FFFFFFFFFFFFFFF));
assert(addr!=NULL,"missing abs mask");
  andd (dst, addr);
}

void MacroAssembler::corrected_d2i(Register dest, FRegister src, Register scratch) {
  assert0( dest != scratch );
Label NaN;
  mov8i  (dest, 0);
  mov8i  (scratch, 0);
  address addr = double_constant(((jdouble)max_jint)+0.5);
  assert0(addr != NULL);
  cmp8   (src, addr);
  jpe    (NaN);
  setae  (scratch);       // scratch = (src >= max_jint) ? 1 : 0
  cvt_d2i(dest, src);     // dest    = (int)src
  sub4   (dest, scratch); // dest   -= (src >= max_jint) ? 1 : 0
bind(NaN);
}

void MacroAssembler::corrected_d2l(Register dest, FRegister src, Register scratch) {
  assert0( dest != scratch );
Label NaN;
  mov8i  (dest, 0);
  mov8i  (scratch, 0);
  address addr = double_constant(((jdouble)max_jlong)+0.5);
  assert0(addr != NULL);
  cmp8   (src, addr);
  jpe    (NaN);
  setae  (scratch);       // scratch = (src >= max_jlong) ? 1 : 0
  cvt_d2l(dest, src);     // dest    = (long)src
  sub8   (dest, scratch); // dest   -= (src >= max_jlong) ? 1 : 0
bind(NaN);
}

void MacroAssembler::corrected_f2i(Register dest, FRegister src, Register scratch) {
  assert0( dest != scratch );
Label NaN;
  mov8i  (dest, 0);
  mov8i  (scratch, 0);
  address addr = float_constant((jfloat)max_jint);
  assert0(addr != NULL);
  cmp4   (src, addr);
  jpe    (NaN);
  setae  (scratch);       // scratch = (src >= max_jint) ? 1 : 0
  cvt_f2i(dest, src);     // dest    = (int)src
  sub4   (dest, scratch); // dest   -= (src >= max_jint) ? 1 : 0
bind(NaN);
}

void MacroAssembler::corrected_f2l(Register dest, FRegister src, Register scratch) {
  assert0( dest != scratch );
Label NaN;
  mov8i  (dest, 0);
  mov8i  (scratch, 0);
  address addr = float_constant((jfloat)max_jlong);
  assert0(addr != NULL);
  cmp4   (src, addr);
  jpe    (NaN);
  setae  (scratch);       // scratch = (src >= max_jlong) ? 1 : 0
  cvt_f2l(dest, src);     // dest    = (long)src
  sub8   (dest, scratch); // dest   -= (src >= max_jlong) ? 1 : 0
bind(NaN);
}

void MacroAssembler::fcmp_helper(Register dest, FRegister left, FRegister right, bool is_float, int unordered_result) {
Label unordered;
  // set up the result to 0 or 1
  mov8i(dest, unordered_result == 1 ? 1 : 0);
  if (is_float) {
    cmp4 (left,right);          // perform comparison
  } else {
    cmp8 (left,right);          // perform comparison
  }
  if (unordered_result == 1) {
    jpe  (unordered);        // unordered?
  }
  seta (dest);                // RCX  = value1 > value2 ? 1 : 0
  sbb8i(dest,0);              // RCX -= value1 < value2 ? 1 : 0
  if (unordered_result == 1) {
bind(unordered);
  }
}

void MacroAssembler::fcmp(Register dest, FRegister left, FRegister right, int unordered_result) {
  assert0(unordered_result == -1 || unordered_result == 1);
  fcmp_helper(dest, left, right, true, unordered_result);
}

void MacroAssembler::dcmp(Register dest, FRegister left, FRegister right, int unordered_result) {
  assert0(unordered_result == -1 || unordered_result == 1);
  fcmp_helper(dest, left, right, false, unordered_result);
}

void MacroAssembler::fcmp_helper_mem(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, bool is_float, int unordered_result) {
Label unordered;
  if (is_float) {
    cmp4 (left,base,off,index,scale);          // perform comparison
  } else {
    cmp8 (left,base,off,index,scale);          // perform comparison
  }
  // set up the result to 0 or 1
  mov8i(dest, unordered_result == 1 ? 1 : 0, false /* cannot blow flags*/);
  if (unordered_result == 1) {
    jpe  (unordered);        // unordered?
  }
  seta (dest);                // RCX  = value1 > value2 ? 1 : 0
  sbb8i(dest,0);              // RCX -= value1 < value2 ? 1 : 0
  if (unordered_result == 1) {
bind(unordered);
  }
}

void MacroAssembler::fcmp(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, int unordered_result) {
  assert0(unordered_result == -1 || unordered_result == 1);
  fcmp_helper_mem(dest, left, base, off, index, scale, true, unordered_result);
}

void MacroAssembler::dcmp(Register dest, FRegister left, Register base, intptr_t off, Register index, int scale, int unordered_result) {
  assert0(unordered_result == -1 || unordered_result == 1);
  fcmp_helper_mem(dest, left, base, off, index, scale, false, unordered_result);
}

void MacroAssembler::cmov4eqi(Register dst, int x, int y) {
  if        (x==y) {          // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setz(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setnz(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setz(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1) {       // x and y differ by 1 case
setnz(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setnz   (dst);          // dst = eq ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = eq ? 0 : -1
    and4i   (dst, y);       // dst = eq ? 0 : y
  } else {
    setz    (dst);                   // dst = eq ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = eq ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = eq ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = eq ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4nei(Register dst, int x, int y) {
  if        (x==y) {          // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setnz(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setz(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setnz(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1) {       // x and y differ by 1 case
setz(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setz    (dst);          // dst = ne ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = ne ? 0 : -1
    and4i   (dst, y);       // dst = ne ? 0 : y
  } else {
    setnz   (dst);                   // dst = ne ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = ne ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = ne ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = ne ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4lti(Register dst, int x, int y) {
  if        (x==y) {          // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setlt(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setge(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setlt(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1) {       // x and y differ by 1 case
setge(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setge   (dst);          // dst = lt ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = lt ? 0 : -1
    and4i   (dst, y);       // dst = lt ? 0 : y
  } else {
    setlt   (dst);                   // dst = lt ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = lt ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = lt ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = lt ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4lei(Register dst, int x, int y) {
  if        (x==y) {         // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setle(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setgt(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setle(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1) {       // x and y differ by 1 case
setgt(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setgt   (dst);          // dst = le ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = le ? 0 : -1
    and4i   (dst, y);       // dst = le ? 0 : y
  } else {
    setle   (dst);                   // dst = le ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = le ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = le ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = le ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4gei(Register dst, int x, int y) {
  if        (x==y) {         // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setge(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setlt(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setge(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1) {       // x and y differ by 1 case
setlt(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setlt   (dst);          // dst = ge ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = ge ? 0 : -1
    and4i   (dst, y);       // dst = ge ? 0 : y
  } else {
    setge   (dst);                   // dst = ge ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = ge ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = ge ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = ge ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4gti(Register dst, int x, int y) {
  if        (x==y) {         // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setgt(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setle(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setgt(dst);
movzx41(dst,dst);
    add4i   (dst, y);
  } else if (y==x+1)   {     // x and y differ by 1 case
setle(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    setle   (dst);          // dst = gt ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);          // dst = gt ? 0 : -1
    and4i   (dst, y);       // dst = gt ? 0 : y
  } else {
    setgt   (dst);                   // dst = gt ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = gt ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = gt ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = gt ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4bei(Register dst, int x, int y) {
  untested("cmov4be"); Untested();
  if        (x==y) {         // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setbe(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
seta(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setbe(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (y==x+1) {       // x and y differ by 1 case
seta(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==0) {
    seta    (dst);           // dst = be ? 0 : 1
movzx41(dst,dst);
    neg4    (dst);           // dst = be ? 0 : -1
    and4i   (dst, y);        // dst = be ? 0 : y
  } else {
    setbe   (dst);                   // dst = be ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = be ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = be ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = be ? (x-y)+y : y
  }
}

void MacroAssembler::cmov4aei(Register dst, int x, int y) {
  untested("cmov4ae"); Untested();
  if        (x==y) {         // not really a cmove
    mov8u   (dst, x);
  } else if (x==1 && y==0) { // common boolean case
setae(dst);
movzx41(dst,dst);
  } else if (x==0 && y==1) { // common boolean case
setbl(dst);
movzx41(dst,dst);
  } else if (x==y+1) {       // x and y differ by 1 case
setae(dst);
movzx41(dst,dst);
    add4i   (dst, x);
  } else if (x==y-1) {       // x and y differ by 1 case
    sbb4    (dst,dst);       // dst = dst - dst - CF => ae ? 0 : -1
    add4i   (dst, y);
  } else if (x==0) {
    sbb4    (dst,dst);       // dst = dst - dst - CF => ae ? 0 : -1
    and4i   (dst, y);        // dst = ae ? 0 : y
  } else {
    setae   (dst);                   // dst = ae ? 1 : 0
movzx41(dst,dst);
    neg4    (dst);                   // dst = ae ? -1 : 0
    if ((x-y)!=-1) and4i (dst, x-y); // dst = ae ? (x-y) : 0
    if (    y!=0 ) add4i (dst, y);   // dst = ae ? (x-y)+y : y
  }
}

NativeMovConstReg* Assembler::nativeMovConstReg_before(int rel_pc) {
  NativeMovConstReg* test = (NativeMovConstReg*)((char*)_blob + rel_pc - NativeMovConstReg::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

NativeMovRegMem* Assembler::nativeMovRegMem_before(int rel_pc) {
  NativeMovRegMem* test = (NativeMovRegMem*)((char*)_blob + rel_pc - NativeMovRegMem::instruction_offset);
#ifdef ASSERT
  test->verify();
#endif
  return test;
}

// --- add_to_allocated_objects ----------------------------------------------
void MacroAssembler::add_to_allocated_objects( RInOuts, Register Rtmp, RKeepIns, Register Rkid, Register Rsiz, Register Rthr) {
  Label klass_id_within_bounds, rows_allocated;

#ifdef ASSERT
  cmp4 (Rkid, Rthr, in_bytes(Thread::allocated_objects_offset() + AllocatedObjects::size_offset()));
  jlt  (klass_id_within_bounds);
  should_not_reach_here("klass id out of bounds of allocated objects profile");
bind(klass_id_within_bounds);
#endif

  ld8  (Rtmp, Rthr, in_bytes(Thread::allocated_objects_offset() + AllocatedObjects::rows_offset()));
  test8(Rtmp, Rtmp);
  jne  (rows_allocated);
  unimplemented("allocation counting needs to make a leaf call");
  call ((address)AllocatedObjects::allocate_rows_for_current_thread);
  ld8  (Rtmp, Rthr, in_bytes(Thread::allocated_objects_offset() + AllocatedObjects::rows_offset()));

bind(rows_allocated);
  shl8i(Rkid, exact_log2(sizeof(AllocatedObjects::Row)));
  inc8 (Rtmp, in_bytes(AllocatedObjects::Row::count_offset()), Rkid, 0 );
  add8 (Rtmp, in_bytes(AllocatedObjects::Row::bytes_offset()), Rkid, 0, Rsiz);
  shr8i(Rkid, exact_log2(sizeof(AllocatedObjects::Row)));
}

// --- Compare 2 primitive arrays for equality, fall-through to success
void MacroAssembler::prim_arrays_equal(int element_size, RInOuts, Register Ra1, Register Ra2,
Register Rtmp1,Register Rtmp2,
                                       FRegister Rxmm1, FRegister Rxmm2, Label &fail) {
Label success;
  // Fast compare of lengths and (possibly) first elements, done first as this is a hot failure route
  if( element_size < 8) {
    ld8  (Rtmp1, Ra1, arrayOopDesc::length_offset_in_bytes()); // Rtmp1 = length | 4bytes
    cmp8 (Rtmp1, Ra2, arrayOopDesc::length_offset_in_bytes()); // cmp Rtmp1, [length | 4bytes]
  } else {
    ldz4  (Rtmp1, Ra1, arrayOopDesc::length_offset_in_bytes()); // Rtmp1 = length
    cmp4 (Rtmp1, Ra2, arrayOopDesc::length_offset_in_bytes()); // cmp Rtmp1, [length]
  }
  jne (fail);
  // Set up Rlength, compute number of 16 and 8 byte loops
  // Element Size:
  //   1         2         4        8
  // Length  | Length  | Length | Length |Num 16byte compares | Num 8byte compares
  // 0 .. 4  | 0 .. 2  | 0 .. 1 |   0    | 0                  | 0
  // 5 .. 12 | 3 .. 6  | 2 .. 3 |   1    | 0                  | 1
  // 13.. 20 | 7 .. 10 | 4 .. 5 |   2    | 1                  | 0
  // 21.. 28 | 11.. 14 | 6 .. 7 |   3    | 1                  | 1
  // 29.. 36 | 15.. 18 | 8 .. 9 |   4    | 2                  | 0
  // 37.. 44 | 19.. 22 | 10..11 |   5    | 2                  | 1
  // Element Size: 1 | 2 | 4 | 8
  // Adjust:       3 | 1 | 0 | 0
  // Shift:        4 | 3 | 2 | 1
  // 16byte compares =  (length + adjust) >>  shift
  // 8byte compares  = ((length + adjust) >> (shift-1)) & 1
  int adjust, shift;
  switch( element_size ) {
  case 1: adjust = 3; shift = 4; break;
  case 2: adjust = 1; shift = 3; break;
  case 4: adjust = 0; shift = 2; break;
  case 8: adjust = 0; shift = 1; break;
  default: ShouldNotReachHere(); return;
  }
Register Rlength=Rtmp1;
  if( element_size == 4 ) mov4 (Rlength, Rlength); // zero out first elements leaving length
  if( adjust != 0 ) add4i(Rlength, adjust);  // Adjust length as given above and zero first elements
  // Set up Roffset to be 16byte chunk following length less 16 for initial increment
Register Roffset=Rtmp2;
  mov8i(Roffset, (arrayOopDesc::length_offset_in_bytes()+8) - 16);
  shr4i(Rlength, shift);  // Rlength = (length+adjust)>>shift, set CF to last bit shifted out, ie (length+1)>>2
Label skip8byte;
  jnb (skip8byte);  // skip initial 8 byte compare

  // use GPRs to avoid partial register use, we'll reinitialize Roffset if compare
  // succeeds
  ld8  (Rtmp2, Ra1, arrayOopDesc::length_offset_in_bytes()+8); // Rtmp2 = bytes 5 to 12
  cmp8 (Rtmp2, Ra2, arrayOopDesc::length_offset_in_bytes()+8); // cmp Rtmp2, [bytes 5 to 12]
  jne  (fail);
  mov8i(Roffset, (arrayOopDesc::length_offset_in_bytes()+8) - 16 + 8);

bind(skip8byte);
  // check if we've fully compared the arrays
  if( Rlength != RCX ) {
    test4(Rlength, Rlength);
    jze  (success);
  } else {
    jrcxz1(success); // short branch, success path is only 8 instructions away
  }
  if( Rxmm2 < Rxmm1 ) { // swap xmms to possibly cheapen encoding below
    FRegister tmp = Rxmm1;
    Rxmm1 = Rxmm2;
Rxmm2=tmp;
  }
  add8(Ra1, Roffset);
  add8(Ra2, Roffset);
  Label loop16(this); // loop for a count that is divisible by 16
  add8i(Ra1, 16); // avoid loop carried dependency - ala Intel optimization guide example 10-16
  add8i(Ra2, 16); // avoid loop carried dependency - ala Intel optimization guide example 10-16
  // TODO: unaligned load as we only have 8byte alignment guarantee in heap
  ld16u (Rxmm1, Ra1, 0, noreg, 0);
  ld16u (Rxmm2, Ra2, 0, noreg, 0);
  // TODO: we could use pcmpistri below to save an instruction, but that implicitly blows RCX
if(VM_Version::supports_ptest()){
    xor16 (Rxmm1,Rxmm2);   // don't use memory operand as it must be 16byte aligned
    test16(Rxmm1, Rxmm1);
    // We could use the below 2 instructions:
    // if (Rlength == RCX) { jloope1(loop16); jne(fail); }
    // but favor: (as in optimization guides Intel example 12-4, AMD 6.7)
    jnz  (fail);        // bail if 16byte compare failed
  } else {
    pcmpeqb (Rxmm1, Rxmm2);     // 16 bytes compared; equal ==> 0xFF, not equal ==> 0x00
    pmovmskb(Rtmp2, Rxmm1);     // move hi bit per byte; all 16 bytes equal ==> 0xFFFF
    inc2    (Rtmp2);            // 0xFFFF ==> 0/Z, XXXX ==> XXXX/NZ
    jnz     (fail);
  }
  dec4 (Rlength);     // Rlength--, ZF == (Rlength == 0)
  jnz  (loop16);      // back edge
bind(success);
}

void cpuid();
