/*
 * Copyright 2002-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *  
 */
// This file is a derivative work resulting from (and including) modifications
// made by Azul Systems, Inc.  The date of such changes is 2010.
// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
//
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.


#include "allocation.hpp"
#include "block.hpp"
#include "commonAsm.hpp"
#include "machnode.hpp"
#include "memnode.hpp"
#include "node.hpp"
#include "regalloc.hpp"
#include "rootnode.hpp"
#include "type.hpp"
#include "vreg.hpp"

// The functions in this file builds OopMaps after all scheduling is done.
//
// OopMaps contain a list of all registers and stack-slots containing oops (so
// they can be updated by GC).  OopMaps also contain a list of derived-pointer
// base-pointer pairs.  When the base is moved, the derived pointer moves to
// follow it.  Finally, any registers holding callee-save values are also
// recorded.  These might contain oops, but only the caller knows.
//
// BuildOopMaps implements a simple forward reaching-defs solution.  At each
// GC point we'll have the reaching-def Nodes.  If the reaching Nodes are
// typed as pointers (no offset), then they are oops.  Pointers+offsets are
// derived pointers, and bases can be found from them.  Finally, we'll also
// track reaching callee-save values.  Note that a copy of a callee-save value
// "kills" it's source, so that only 1 copy of a callee-save value is alive at
// a time.
//
// We run a simple bitvector liveness pass to help trim out dead oops.  Due to
// irreducible loops, we can have a reaching def of an oop that only reaches
// along one path and no way to know if it's valid or not on the other path.
// The bitvectors are quite dense and the liveness pass is fast.
//
// At GC points, we consult this information to build OopMaps.  All reaching
// defs typed as oops are added to the OopMap.  Only 1 instance of a
// callee-save register can be recorded.  For derived pointers, we'll have to
// find and record the register holding the base.
//
// The reaching def's is a simple 1-pass worklist approach.  I tried a clever
// breadth-first approach but it was worse (showed O(n^2) in the
// pick-next-block code).
//
// The relevent data is kept in a struct of arrays (it could just as well be
// an array of structs, but the struct-of-arrays is generally a little more
// efficient).  The arrays are indexed by register number (including
// stack-slots as registers) and so is bounded by 200 to 300 elements in
// practice.  One array will map to a reaching def Node (or NULL for
// conflict/dead).  The other array will map to a callee-saved register or
// OptoReg::Bad for not-callee-saved.


//------------------------------OopFlow----------------------------------------
// Structure to pass around 
struct OopFlow : public ResourceObj {
  short *_callees;              // Array mapping register to callee-saved 
  Node **_defs;                 // array mapping register to reaching def
                                // or NULL if dead/conflict
  // OopFlow structs, when not being actively modified, describe the _end_ of
  // this block.
  Block *_b;                    // Block for this struct
  OopFlow *_next;               // Next free OopFlow

  OopFlow( short *callees, Node **defs ) : _callees(callees), _defs(defs),
    _b(NULL), _next(NULL) { }

  // Given reaching-defs for this block start, compute it for this block end
void compute_reach(PhaseRegAlloc*regalloc,int max_reg,Dict*safehash,byte*lvbs);

  // Merge these two OopFlows into the 'this' pointer.
  void merge( OopFlow *flow, int max_reg );

  // Copy a 'flow' over an existing flow
  void clone( OopFlow *flow, int max_size);

  // Make a new OopFlow from scratch
  static OopFlow *make( Arena *A, int max_size );

  // Build an oopmap from the current flow info
OopMap2*build_oop_map(Node*n,int max_reg,PhaseRegAlloc*regalloc,int*live,byte*lvbs);

  // Read-barrier / LVB checks.  Byte-array indexed by Node index.
  enum { 
    has_LVB_init   = 1,         // not_LVB set for this Node yet?
    not_LVB        = 2,         // Node result not LVBed
    needs_LVB_init = 4,         // needs_LVB set for this Node yet?
    needs_LVB      = 8          // does this Node have LVB-required uses?
  };
  static int  is_not_LVBed( Node *n, byte *lvbs );
  static int  is_needs_LVB( Node *n, byte *lvbs );

  // Some oop is reaching a safepoint - it must be LVB'd first
  bool assert_lvb_ok( Node *def, Node *safe, PhaseRegAlloc *regalloc, byte *lvbs );
};

//------------------------------compute_reach----------------------------------
// Given reaching-defs for this block start, compute it for this block end
void OopFlow::compute_reach(PhaseRegAlloc*regalloc,int max_reg,Dict*safehash,byte*lvbs){

  for( uint i=0; i<_b->_nodes.size(); i++ ) {
    Node *n = _b->_nodes[i];

    if( n->jvms() ) {           // Build an OopMap here?
      JVMState *jvms = n->jvms();
      // no map needed for leaf calls
      if( n->is_MachSafePoint() && !n->is_MachCallLeaf() ) {
        int *live = (int*) (*safehash)[n];
        assert( live, "must find live" );
n->as_MachSafePoint()->set_oop_map(build_oop_map(n,max_reg,regalloc,live,lvbs));
      }
    }

    // If SBA is turned on, any random store of an oop into an oop can cause a
    // stack escape and need a stack gc for fixup.  Hence, each oop-oop store
    // needs full GC info.  No de-opt can happen (no need for debug info).
    if( n->base_derived_idx() != 0 ) 
      n->set_oopmap(UseSBA ? build_oop_map(n,max_reg,regalloc, (int*) (*safehash)[n], lvbs) : NULL);

    // Assign new reaching def's.
    // Note that I padded the _defs and _callees arrays so it's legal
    // to index at _defs[OptoReg::Bad].
OptoReg::Name first=regalloc->get_reg(n);
    _defs[first] = n;

    // Pass callee-save info around copies
    int idx = n->is_Copy();
    if( idx ) {                 // Copies move callee-save info
OptoReg::Name old_first=regalloc->get_reg(n->in(idx));
      int tmp_first = _callees[old_first];
      _callees[old_first] = OptoReg::Bad; // callee-save is moved, dead in old location
      _callees[first] = tmp_first;
    } else if( n->is_Phi() ) {  // Phis do not mod callee-saves
assert(_callees[first]==_callees[regalloc->get_reg(n->in(1))],"");
assert(_callees[first]==_callees[regalloc->get_reg(n->in(n->req()-1))],"");
    } else {
      _callees[first] = OptoReg::Bad; // No longer holding a callee-save value

      // Find base case for callee saves
      if( n->is_Proj() && n->in(0)->is_Start() ) {
        if( OptoReg::is_reg(first) &&
            regalloc->_matcher.is_save_on_entry(first) )
          _callees[first] = first;
      }
    }
  }
}

//------------------------------merge------------------------------------------
// Merge the given flow into the 'this' flow
void OopFlow::merge( OopFlow *flow, int max_reg ) {
  assert( _b == NULL, "merging into a happy flow" );
  assert( flow->_b, "this flow is still alive" );
  assert( flow != this, "no self flow" );

  // Do the merge.  If there are any differences, drop to 'bottom' which
  // is OptoReg::Bad or NULL depending.
  for( int i=0; i<max_reg; i++ ) {
    // Merge the callee-save's
    if( _callees[i] != flow->_callees[i] )
      _callees[i] = OptoReg::Bad;
    // Merge the reaching defs
    if( _defs[i] != flow->_defs[i] )
      _defs[i] = NULL;
  }

}

//------------------------------clone------------------------------------------
void OopFlow::clone( OopFlow *flow, int max_size ) {
  _b = flow->_b;
  memcpy( _callees, flow->_callees, sizeof(short)*max_size);
  memcpy( _defs   , flow->_defs   , sizeof(Node*)*max_size);
}

//------------------------------make-------------------------------------------
OopFlow *OopFlow::make( Arena *A, int max_size ) {
  short *callees = NEW_ARENA_ARRAY(A,short,max_size+1);
  Node **defs    = NEW_ARENA_ARRAY(A,Node*,max_size+1);
  debug_only( memset(defs,0,(max_size+1)*sizeof(Node*)) );
  OopFlow *flow = new (A) OopFlow(callees+1, defs+1);
  assert( &flow->_callees[OptoReg::Bad] == callees, "Ok to index at OptoReg::Bad" );
  assert( &flow->_defs   [OptoReg::Bad] == defs   , "Ok to index at OptoReg::Bad" );
  return flow;
}

//------------------------------bit twiddlers----------------------------------
static int get_live_bit( int *live, int reg ) {
  return live[reg>>LogBitsPerInt] &   (1<<(reg&(BitsPerInt-1))); }
static void set_live_bit( int *live, int reg ) {
         live[reg>>LogBitsPerInt] |=  (1<<(reg&(BitsPerInt-1))); }
static void clr_live_bit( int *live, int reg ) {
         live[reg>>LogBitsPerInt] &= ~(1<<(reg&(BitsPerInt-1))); }

//------------------------------build_oop_map----------------------------------
// Build an oopmap from the current flow info
OopMap2*OopFlow::build_oop_map(Node*n,int max_reg,PhaseRegAlloc*regalloc,int*live,byte*lvbs){
int framesize_words=regalloc->_framesize-regalloc->C->in_preserve_stack_slots();
int max_inarg_words=OptoReg::reg2stack(regalloc->_matcher._new_SP);
debug_only(char*dup_check=NEW_RESOURCE_ARRAY(char,REG_COUNT);
memset(dup_check,0,REG_COUNT));

  OopMap2 *omap = new OopMap2( );
  MachCallNode *mcall = n->is_MachCall() ? n->as_MachCall() : NULL;
  JVMState* jvms = n->jvms();
  bool assert_for_derived = false;

  // For all registers do...
  for( int reg=0; reg<max_reg; reg++ ) {
    if( get_live_bit(live,reg) == 0 )
      continue;                 // Ignore if not live
    VReg::VR vreg = OptoReg::as_VReg(OptoReg::Name(reg), framesize_words, max_inarg_words);
    // Some regs (e.g. FP regs) are live but can never be OOPs
    if( VReg::is_reg(vreg) && !(vreg < REG_OOP_COUNT) )  
      continue;                 // Do not bother with FP regs
    VOopReg::VR r = VReg::as_VOopReg(vreg);

    // See if dead (no reaching def).
    Node *def = _defs[reg];     // Get reaching def
    assert( def, "since live better have reaching def" );

    // Classify the reaching def as oop, derived, callee-save, dead, or other
    const Type *t = def->bottom_type();
if(t->isa_oop_ptr()&&//Oop or derived?
        !(t->isa_klassptr() && t->is_klassptr()->_is_kid)) { 
      if( t->is_ptr()->_offset == 0 ) { // Not derived?
        if( mcall ) {
          // Outgoing argument GC mask responsibility belongs to the callee,
          // not the caller.  Inspect the inputs to the call, to see if this
          // live-range is one of them.
          uint cnt = mcall->tf()->domain()->cnt();
          uint j;
          for( j = TypeFunc::Parms; j < cnt; j++)
            if( mcall->in(j) == def )
              break;            // reaching def is an argument oop
          if( j < cnt )         // arg oops dont go in GC map
            continue;           // Continue on to the next register
        }
        if( !is_needs_LVB(def,lvbs) )
          continue;             // Uses are all OK withOUT LVB
        assert0( !is_not_LVBed(def,lvbs) );
omap->add(r);
      } else {                  // Else it's derived.  
        // Find the base of the derived value.
        // For SafePoints, scan the base/derived pair list located at
        // jvms->oopoff().  For UseSBA, we could have a StoreP needing an
        // OopMap.  His base/derived pair list starts after his normal inputs.
        uint sidx = jvms ? jvms->oopoff() : n->base_derived_idx();
        assert0( sidx != 0 );
        // Fast, common case, scan
        uint i;
        for( i = sidx; i < n->req(); i+=2 ) 
          if( n->in(i) == def ) break; // Common case
        if( i == n->req() ) {   // Missed, try a more generous scan
          // Scan again, but this time peek through copies
for(i=sidx;i<n->req();i+=2){
            Node *m = n->in(i); // Get initial derived value
            while( 1 ) {
              Node *d = def;    // Get initial reaching def
              while( 1 ) {      // Follow copies of reaching def to end
                if( m == d ) goto found; // breaks 3 loops
                int idx = d->is_Copy();
                if( !idx ) break;
                d = d->in(idx);     // Link through copy
              }
              int idx = m->is_Copy();
              if( !idx ) break;
              m = m->in(idx);
            }
          }
#ifdef ASSERT
C2OUT->print_cr("Did not find base/derived pair spanning a GC point");
C2OUT->print("Def node: ");def->dump();
C2OUT->print("Def type: ");t->dump();C2OUT->cr();
n->dump(2);
#endif
          guarantee( 0, "must find derived/base pair" );
        }
      found: ;
        Node *base = n->in(i+1); // Base is other half of pair
        if( base->is_Con() ) regalloc->C->record_failure("No derived-from-NULL please", false);
int breg=regalloc->get_reg(base);
        VReg::VR b_vreg = OptoReg::as_VReg(OptoReg::Name(breg), framesize_words, max_inarg_words);
        VOopReg::VR b = VReg::as_VOopReg(b_vreg);

        // I record liveness at safepoints BEFORE I make the inputs live.
        // This is because argument oops are NOT live at a safepoint (or at
        // least they cannot appear in the oopmap).  Thus bases of
        // base/derived pairs might not be in the liveness data but they need
        // to appear in the oopmap.
        if( get_live_bit(live,breg) == 0 ) {// Not live?
          // Flag it, so next derived pointer won't re-insert into oopmap
          set_live_bit(live,breg);
          // Already missed our turn?
if(breg<reg)
omap->add(b);
        }
        assert0( !is_not_LVBed(base,lvbs) ); // oop was LVB'd before safepoint?
        assert_for_derived = true;
        omap->add_derived_oop( b_vreg, vreg);
      }

    } else if( OptoReg::is_valid(OptoReg::Name(_callees[reg]))) { // callee-save?
      // It's a callee-save value
      assert( dup_check[_callees[reg]]==0, "trying to callee save same reg twice" );
debug_only(dup_check[_callees[reg]]=1);
VReg::VR callee=OptoReg::as_VReg(OptoReg::Name(_callees[reg]),framesize_words,max_inarg_words);
      if( vreg != callee )      // Cutout if the callee-save was not moved
        omap->add_callee_save_pair( callee, vreg);

    } else {
      // Other - some reaching non-oop value
      //omap->set_xxx( r);
    }

  }

#ifdef ASSERT
  if( assert_for_derived ) {
    for( int i=0; i<omap->_base_derived_pairs.length(); i+=2 ) {
      VOopReg::VR base = VReg::as_VOopReg(omap->_base_derived_pairs.at(i+0));
      VOopReg::VR deri = VReg::as_VOopReg(omap->_base_derived_pairs.at(i+1));
      assert0(  omap->at(base) ); // Base needs to be in the OopMap
      assert0( !omap->at(deri) ); // Derived must NOT be, as it's not a proper Oop
    }
  }
#endif

  return omap;
}


// Always returns 'needs_LVB'.  Sets needs_LVB into the lvbs array for node n.
// If this is a change (only monotonic change 0->needs_LVB is possible), then
// it's possible that cyclic-checked parent got a 0 for node n, and the parent
// needs to be updated as well.
static int back_flow_needs( Node *n, byte *lvbs) {
  byte bits = lvbs[n->_idx];    // Existing bits
  if( (bits & OopFlow::needs_LVB_init) == 0 ) // Never visited; don't bother now & stop recursion
    return OopFlow::needs_LVB;  // Always always return needs_LVB
  // See if we made any change.  If no change, then just return results.
  if( (bits & OopFlow::needs_LVB) != 0 ) // Change?
    return OopFlow::needs_LVB;  // No change!
  lvbs[n->_idx] = bits | OopFlow::needs_LVB; // Change it.
  // Now backwards-flow the 'needs_LVB' into parents for anybody
  // that had to recursively ask children for a value
  if( n->is_Phi() || n->is_Copy() ||
      (n->is_Mach() && (((MachNode*)n)->ideal_Opcode() == Op_CastPP ||
                        ((MachNode*)n)->ideal_Opcode() == Op_CheckCastPP)) )
for(uint i=1;i<n->req();i++)
      back_flow_needs(n->in(i),lvbs);
  return OopFlow::needs_LVB;
}

//------------------------------is_needs_LVB----------------------------------
// Does this Node have LVB-required uses, such as "escaping" it to the heap
// via call-args or a store, or are all it's uses LVB-safe such as null-ptr
// checks or KID extractions?  This is a backwards-flow problem.  Returns 0
// (no LVB needed) or needs_LVB enum.
int OopFlow::is_needs_LVB( Node *n, byte *lvbs ) {
  byte bits = lvbs[n->_idx];
  if( bits & needs_LVB_init ) return bits & needs_LVB;
  // Set the init flag to close cycles, with the default value to
  // optimistically assume that there are no LVB-required uses.
  lvbs[n->_idx] |= needs_LVB_init;

  // Check all uses are OK without read-barrier
  for( DUIterator_Fast imax, i = n->fast_outs(imax); i < imax; i++ ) {
Node*u=n->fast_out(i);
    if( u->is_Phi() || u->is_Copy() ) { // Phi usage?
      if( is_needs_LVB(u,lvbs) ) // Phi's check recursively
        return back_flow_needs(n,lvbs); // Oops, needs an LVB
      continue;
    }
    int op;
if(u->is_Mach()){
MachNode*um=u->as_Mach();
      // NullChecks test the incoming def's address, not the returned value
      if( um->is_MachLVB() ) {
for(uint i=2;i<um->req();i++)
if(um->in(i)==n)
            return back_flow_needs(n,lvbs); // Oops, expect only use as a 'value' and not as a 'address'
        continue; // 'value' uses are obviously OK
      }
      if( um->is_MachNullCheck() ) continue; 
      if( um->is_MachReturn() ) // Covers MachSafepoint and all MachCalls
        return back_flow_needs(n,lvbs); // Oops, needs an LVB
      op = um->ideal_Opcode();
      // Some memory operations are not rooted at an obvious memory op; "ldu1"
      // looks like a LoadB with an AndI mask of 255 - so is rooted at the
      // AndI.  Rather than trying to find all of these - just assume that if
      // it has a memory operand we need to LVB it.
      if( um->memory_operand() != 0 )
        op = Op_LoadP;
    } else
      op = u->Opcode();
    switch( op ) {
    case Op_If:                 // ptr-vs-null check
    case Op_CmpP:               // 
      if( u->as_Mach()->req() != 2 ) // Only the ctrl-edge and the single ptr edge
        // Comparing 2 ptrs requires LVBs
        return back_flow_needs(n,lvbs); // Oops, needs an LVB
      // Check that we believe it's a compare of 1 ptr vs null
      assert0( u->as_Mach()->num_opnds() == 3 );
      assert0( u->as_Mach()->_opnds[1]->num_edges() == 1 );
      assert0( u->as_Mach()->_opnds[2]->num_edges() == 0 );
      assert0( u->as_Mach()->_opnds[2]->constant() == 0 ||
               u->as_Mach()->_opnds[2]->constant_is_oop() );
      break;
    case Op_Conv2B:             // Another null-check variant
    case Op_MemBarRelease:      // Only happens on precedence edges
    case Op_MemBarAcquire:      // Only happens on precedence edges
    case Op_MemBarVolatile:     // Only happens on precedence edges
case Op_MemBarCPUOrder://Only happens on precedence edges
      break;
    case Op_CheckCastPP:
    case Op_CastPP:
      if( is_needs_LVB(u,lvbs) ) // Check recursively
        return back_flow_needs(n,lvbs); // Oops, needs an LVB
      break;
    case Op_KID2Klass:          // Only consumes a KID
    case Op_MachProj:           // flag/register kills
      return 1;
    case Op_GetKID:
      if( KIDInRef ) break;     // No need for LVB
      // Must load thru ref, so must LVB
      return back_flow_needs(n,lvbs);
    case Op_AddP:
    case Op_ArrayCopy:
    case Op_ArrayCopySrc:
    case Op_CastP2X:            // Unsafe ops need LVB first
    case Op_ClearArray:
case Op_CMoveP://The test input IF its a null check, does not need an LVB.  The other inputs need to recursively check
    case Op_CompareAndSwapI:
    case Op_CompareAndSwapL:
    case Op_CompareAndSwapP:
    case Op_EscapeMemory:
    case Op_FastLock:
    case Op_FastUnlock:
    case Op_CmpL: 
case Op_LoadP://All memory ops look like LoadP
    case Op_CmpI:               // Ideal opcode for partialSubtypeCheck_vs_zero
    case Op_PartialSubtypeCheck:
    case Op_PrefetchRead:
    case Op_PrefetchWrite:
    case Op_StrComp:
    case Op_StrEquals:
      // We sometimes get here for precedence edges which do NOT need LVBs
for(uint i=1;i<u->req();i++)
if(u->in(i)==n)
          return back_flow_needs(n,lvbs); // Oops, needs an LVB
      break;                    // No required edges, so no LVB needed
    default: 
      NOT_PRODUCT( u->dump(6) );
      ShouldNotReachHere();
      ;
    }
  } // For all uses
  return 0;                     // No LVB-required uses!
}

//------------------------------not_LVBed-------------------------------------
// def is a live oop reaching a safepoint.  Check to make sure it has been
// LVB'd.  This is backwards flow problem.  Returns 0 (LVB'd) or not_LVB if
// this Node can return a non-LVBed value.
int OopFlow::is_not_LVBed( Node *n, byte *lvbs ) {
#ifdef ASSERT
  byte bits = lvbs[n->_idx];
  if( bits & has_LVB_init ) return bits & not_LVB;
  // Set the init flag to close cycles, with the default value to
  // optimistically assume that the value has been LVBd
  lvbs[n->_idx] |= has_LVB_init;
  if( !UseLVBs && !RefPoisoning ) return 0; // No LVB required for some GCs

  // Null pointers come pre-LVBed
  if( n->bottom_type() == TypePtr::NULL_PTR ) return 0;

  // Get the pointer's sources' basic opcode
  int op = n->Opcode();
  if( n->is_Mach() ) {
    MachNode *mach = n->as_Mach();
    if( mach->is_MachLVB() )    // LVBs are obviously OK!
      return 0;
    op = mach->ideal_Opcode();
    if( mach->is_Copy() ) {     // Sigh, MachSpillCopy has no opcode
      op = Op_CheckCastPP;
Node*arg=n->in(1);
      if ( arg->is_Mach() && 
           arg->as_Mach()->ideal_Opcode() == Op_LoadP &&
           !arg->adr_type()->isa_oopptr() ) {
        return 0;
      }
    }
  }

  // Now figure out if the source is an LVB'd pointer or not.
  switch( op ) {
  case Op_CastPP:
  case Op_CheckCastPP:
  case Op_CMoveP:
  case Op_Phi:
    // These flavors pass an oop straight through, so require their oop-ish
    // inputs to be recursively LVB'd.  Phi's can cycle, hence the visited bits.
for(uint i=1;i<n->req();i++){
Node*in=n->in(i);
      if( in->bottom_type()->isa_oopptr() ) {
if(in->is_Mach()){
MachNode*ptr=in->as_Mach();
          if (ptr->ideal_Opcode() == Op_LoadP && 
              !ptr->adr_type()->isa_oopptr() )
            continue;
        }
        if( is_not_LVBed(in,lvbs) ) {
          lvbs[n->_idx] |= not_LVB;
          return not_LVB;       // Some input not LVBed, so the result may not be
        }
      }
    }
    return 0;
  case Op_LoadP:
  case Op_LoadKlass:            // These values are NOT normally lvb'd
    if( n->in(MemNode::Memory)->Opcode() == Op_MachProj &&
        n->in(MemNode::Memory)->in(0)->Opcode() == Op_StartOSR &&
        n->adr_type() == TypeRawPtr::BOTTOM &&
        n->req() == 3 ) 
      return 0; // Loaded from OSR buffer; already LVB'd by interpreter
    else if ( !n->adr_type()->isa_oop_ptr() )
      return 0;
    lvbs[n->_idx] |= not_LVB;
    return not_LVB;
case Op_ConP://Loads from the KlassTable are *not* LVB'd yet
    lvbs[n->_idx] |= not_LVB;
    return not_LVB;
  case Op_MachProj: // Results from calls and incoming args are already LVB'd
  case Op_FastAlloc:// New allocations start properly read-barriered
return 0;
  default:
    n->dump(2);
    ShouldNotReachHere();
  }
#endif // ASSERT
  return 0;
}

//------------------------------do_liveness------------------------------------
// Compute backwards liveness on registers
static void do_liveness( PhaseRegAlloc *regalloc, PhaseCFG *cfg, Block_List *worklist, int max_reg_ints, Arena *A, Dict *safehash ) {
  int *live = NEW_ARENA_ARRAY(A, int, (cfg->_num_blocks+1) * max_reg_ints);
  int *tmp_live = &live[cfg->_num_blocks * max_reg_ints];
  Node *root = cfg->C->root();
  // On CISC platforms, get the node representing the stack pointer  that regalloc
  // used for spills
  Node *fp = NodeSentinel;
  if (UseCISCSpill && root->req() > 1) {
    fp = root->in(1)->in(TypeFunc::FramePtr);
  }
  memset( live, 0, cfg->_num_blocks * (max_reg_ints<<LogBytesPerInt) );
  // Push preds onto worklist
  for( uint i=1; i<root->req(); i++ )
    worklist->push(cfg->_bbs[root->in(i)->_idx]);

  // ZKM.jar includes tiny infinite loops which are unreached from below.  If
  // we missed any blocks, we'll retry here after pushing all missed blocks on
  // the worklist.  Normally this outer loop never trips more than once.
  while( 1 ) {

    while( worklist->size() ) { // Standard worklist algorithm
      Block *b = worklist->rpop();
      
      // Copy first successor into my tmp_live space
      int s0num = b->_succs[0]->_pre_order;
      int *t = &live[s0num*max_reg_ints];
      for( int i=0; i<max_reg_ints; i++ )
        tmp_live[i] = t[i];
      
      // OR in the remaining live registers
      for( uint j=1; j<b->_num_succs; j++ ) {
        uint sjnum = b->_succs[j]->_pre_order;
        int *t = &live[sjnum*max_reg_ints];
        for( int i=0; i<max_reg_ints; i++ )
          tmp_live[i] |= t[i];
      }
      
      // Now walk tmp_live up the block backwards, computing live
      for( int k=b->_nodes.size()-1; k>=0; k-- ) {
        Node *n = b->_nodes[k];
        // KILL def'd bits
OptoReg::Name first=regalloc->get_reg(n);
        if( OptoReg::is_valid(first) ) clr_live_bit(tmp_live,first);

        for( uint l=1; l<n->req(); l++ ) {
          Node *def = n->in(l);
if(def){
OptoReg::Name first=regalloc->get_reg(def);
            if( OptoReg::is_valid(first) ) set_live_bit(tmp_live,first);
          }
        }

        // If SBA is turned on, any random store of an oop into an oop can
        // cause a stack escape and need a stack gc for fixup.  Hence, each
        // oop-oop store needs full GC info - including address & value.
        if( UseSBA && n->base_derived_idx() > 0 ) {
if(n->Opcode()==Op_ArrayCopy){
            // Under SBA, ArrayCopy can trigger a stack-gc and hence needs an
            // OopMap - and the OopMap needs to keep both the src and dst
            // arrays alive.  However, the src array isn't alive until the
            // ArrayCopySrc node rolls by.  I can either move the OopMap
            // generation to ArrayCopySrc - but it schedules weirdly - so I'd
            // have to move all code-gen to the ArrayCopySrc which seems like
            // a bad place to do it, or hack here.
Node*ac_src=n->in(1);
Node*src=ac_src->in(3);
            set_live_bit(tmp_live,regalloc->get_reg(src));
          }
          int *n_live = NEW_ARENA_ARRAY(A, int, max_reg_ints);
          for( int l=0; l<max_reg_ints; l++ )
            n_live[l] = tmp_live[l];
          safehash->Insert(n,n_live);
        }

        if( n->jvms() ) {       // Record liveness at safepoint
          // This placement of this stanza means inputs to calls are
          // considered live at the callsite's OopMap.  Argument oops are
          // hence live, but NOT included in the oopmap.  See cutout in
          // build_oop_map.  Debug oops are live (and in OopMap).
          int *n_live = NEW_ARENA_ARRAY(A, int, max_reg_ints);
          for( int l=0; l<max_reg_ints; l++ )
            n_live[l] = tmp_live[l];
          safehash->Insert(n,n_live);
        }
      }
      
      // Now at block top, see if we have any changes.  If so, propagate to
      // prior blocks.
      int *old_live = &live[b->_pre_order*max_reg_ints];
      int l;
      for( l=0; l<max_reg_ints; l++ )
        if( tmp_live[l] != old_live[l] )
          break;
      if( l<max_reg_ints ) {     // Change!
        // Copy in new value
        for( l=0; l<max_reg_ints; l++ )
          old_live[l] = tmp_live[l];
        // Push preds onto worklist
        for( l=1; l<(int)b->num_preds(); l++ )
          worklist->push(cfg->_bbs[b->pred(l)->_idx]);
      }
    }
    
    // Scan for any missing safepoints.  Happens to infinite loops ala ZKM.jar
    uint i;
    for( i=1; i<cfg->_num_blocks; i++ ) {
      Block *b = cfg->_blocks[i];
      uint j;
      for( j=1; j<b->_nodes.size(); j++ )
        if( b->_nodes[j]->jvms() &&
            (*safehash)[b->_nodes[j]] == NULL )
           break;
      if( j<b->_nodes.size() ) break;
    }
    if( i == cfg->_num_blocks )
      break;                    // Got 'em all
    // Force the issue (expensively): recheck everybody
    for( i=1; i<cfg->_num_blocks; i++ )
      worklist->push(cfg->_blocks[i]);
  }

}

//------------------------------BuildOopMaps-----------------------------------
// Collect GC mask info - where are all the OOPs?
void Compile::BuildOopMaps() {
  NOT_PRODUCT( TracePhase t3("bldOopMaps", &_t_buildOopMaps, TimeCompiler); )
  // Can't resource-mark because I need to leave all those OopMaps around,
  // or else I need to resource-mark some arena other than the default.
  // ResourceMark rm;              // Reclaim all OopFlows when done
  int max_reg = _regalloc->_max_reg; // Current array extent

  Arena *A = Thread::current()->resource_area();
  Block_List worklist;          // Worklist of pending blocks
  byte *lvbs = NEW_ARENA_ARRAY(A, byte, _cfg->C->_unique );
  memset( lvbs, 0, _cfg->C->_unique*sizeof(byte) );
  
  int max_reg_ints = round_to(max_reg, BitsPerInt)>>LogBitsPerInt;
  Dict *safehash = NULL;        // Used for assert only
  // Compute a backwards liveness per register.  Needs a bitarray of
  // #blocks x (#registers, rounded up to ints)
  safehash = new Dict(cmpkey,hashkey,A);
  do_liveness( _regalloc, _cfg, &worklist, max_reg_ints, A, safehash );
  OopFlow *free_list = NULL;    // Free, unused

  // Array mapping blocks to completed oopflows
  OopFlow **flows = NEW_ARENA_ARRAY(A, OopFlow*, _cfg->_num_blocks);
  memset( flows, 0, _cfg->_num_blocks*sizeof(OopFlow*) );


  // Do the first block 'by hand' to prime the worklist
  Block *entry = _cfg->_blocks[1];
  OopFlow *rootflow = OopFlow::make(A,max_reg);
  // Initialize to 'bottom' (not 'top')
  memset( rootflow->_callees, OptoReg::Bad, max_reg*sizeof(short) );
  memset( rootflow->_defs   ,            0, max_reg*sizeof(Node*) );
  flows[entry->_pre_order] = rootflow;

  // Do the first block 'by hand' to prime the worklist
  rootflow->_b = entry;
rootflow->compute_reach(_regalloc,max_reg,safehash,lvbs);
  for( uint i=0; i<entry->_num_succs; i++ )
    worklist.push(entry->_succs[i]);

  // Now worklist contains blocks which have some, but perhaps not all,
  // predecessors visited.
  while( worklist.size() ) {
    // Scan for a block with all predecessors visited or any random slob
    // otherwise.  All-preds-visited order allows me to recycle OopFlow
    // structures rapidly and cut down on the memory footprint.
    // Note: not all predecessors might be visited yet (must happen for
    // irreducible loops).  This is OK, since every live value must have the
    // SAME reaching def for the block so any reaching def is OK.
    uint i;

    Block *b = worklist.pop(); 
    // Ignore root block
    if( b == _cfg->_broot ) continue;
    // Block is already done?  Happens if block has several predecessors,
    // he can get on the worklist more than once.
    if( flows[b->_pre_order] ) continue;

    // If this block has a visited predecessor AND that predecessor has this
    // last block as his only undone child, we can move the OopFlow from the
    // pred to this block.  Otherwise we have to grab a new OopFlow.
    OopFlow *flow = NULL;       // Flag for finding optimized flow
    Block *pred = (Block*)0xdeadbeef;
    uint j;
    // Scan this block's preds to find a done predecessor
    for( j=1; j<b->num_preds(); j++ ) {
      Block *p = _cfg->_bbs[b->pred(j)->_idx];
      OopFlow *p_flow = flows[p->_pre_order];
      if( p_flow ) {            // Predecessor is done
        assert( p_flow->_b == p, "cross check" );
        pred = p;               // Record some predecessor
        // If all successors of p are done except for 'b', then we can carry
        // p_flow forward to 'b' without copying, otherwise we have to draw
        // from the free_list and clone data.
        uint k;
        for( k=0; k<p->_num_succs; k++ )
          if( !flows[p->_succs[k]->_pre_order] &&
              p->_succs[k] != b )
            break;

        // Either carry-forward the now-unused OopFlow for b's use
        // or draw a new one from the free list
        if( k==p->_num_succs ) {
          flow = p_flow;
          break;                // Found an ideal pred, use him
        }
      }
    }

    if( flow ) {
      // We have an OopFlow that's the last-use of a predecessor.
      // Carry it forward.
    } else {                    // Draw a new OopFlow from the freelist
      if( !free_list )
        free_list = OopFlow::make(A,max_reg);
      flow = free_list;
      assert( flow->_b == NULL, "oopFlow is not free" );
      free_list = flow->_next;
      flow->_next = NULL;

      // Copy/clone over the data
      flow->clone(flows[pred->_pre_order], max_reg);
    }

    // Mark flow for block.  Blocks can only be flowed over once, because
    // after the first time they are guarded from entering this code again.
    assert( flow->_b == pred, "have some prior flow" );
    flow->_b = NULL;

    // Now push flow forward
    flows[b->_pre_order] = flow;// Mark flow for this block
    flow->_b = b;               
flow->compute_reach(_regalloc,max_reg,safehash,lvbs);

    // Now push children onto worklist
    for( i=0; i<b->_num_succs; i++ )
      worklist.push(b->_succs[i]);

  }
}
