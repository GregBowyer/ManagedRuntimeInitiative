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


#include "callnode.hpp"
#include "locknode.hpp"
#include "loopnode.hpp"
#include "memnode.hpp"
#include "phaseX.hpp"
#include "runtime.hpp"
#include "stubRoutines.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

//------------------------------lock_coarsen----------------------------------
// Coarsen locks: convert an Unlock/.../Lock sequence into a coarsened lock.
// No coarsening "endlessly" (around loops) as that can cause starvation
// issues.  No coarsening such that a new path has to acquire a lock - as this
// could lead to deadlock.  I do allow inserting an Unlock on a new path as
// this much flexibility appears to allow a lot more unlock/lock sequences to
// be removed.  Unfortunately this also means I need a beefy memory-edge
// rewrite routine.  Sequences like:
//
//      Unlock
//      store X
//      if( P ) ...load X...
//      Lock
//
// Become:
//
//      store X
//      if( P ) { Unlock...load X... }
//      
void PhaseIdealLoop::lock_coarsen(){
  if( !CoarsenLocks ) return;
  while( _locks.size() > 2 ) { // Need some locks to coarsen!
    Node *sample = _locks[0];
Node*obj=sample->in(TypeFunc::Parms+0);
    // Get locks & unlocks matching obj.  Make sure there are enough to
    // bother: need at least 2 of each or there's no chance to optimize
    // anything.  This test will probably cut out 90% of the useless cases.
uint lk_cnt=0;
uint un_cnt=0;
for(uint i=0;i<_locks.size();i++){
Node*x=_locks[i];
      if( x->in(TypeFunc::Parms+0) == obj ) {
        if( x->is_Lock() ) lk_cnt++;
        else               un_cnt++;
      }      
    }
    // If we have 2 more of both locks & unlocks, maybe we can coarsen
    if( lk_cnt >= 2 && un_cnt >= 2 ) // If there are enough to bother trying
try_to_coarsen(obj);
    // Remove all these locks from the worklist; coarsening is over for them
for(uint i=0;i<_locks.size();i++){
Node*x=_locks[i];
      if( x->in(TypeFunc::Parms+0) == obj )
        _locks.remove(i--);
    }
  }
}


//-------------------------scan_for_unlock------------------------------------
// Scan backwards for a matching unlock.  If found we can hoist this lock to
// abut the unlock and cancel both.  First we scan for the "unlocked-locked"
// region, the area between any unlocks and the lock they will cancel.  Nodes
// in this region will end up being inside the new larger locked area.
static bool scan_for_unlock( Node *n, Node *obj, int limit, VectorSet &visited, VectorSet &ulregion ) {
  if( !n ) return false;        // Known bad
  if( visited.test_set(n->_idx) ) // If already came here
    return ulregion.test(n->_idx); // Return same answer
  if( limit == 0 ) return false; // Too deep
  switch( n->Opcode() ) {
  case Op_Loop:
  case Op_CountedLoop:
  case Op_CountedLoopEnd:     
case Op_Con://TOP, or dead
  case Op_Start:
  case Op_Parm:
  case Op_CProj:
  case Op_CatchProj:      // Unknown Java call: may lock internally, must fail
  case Op_Jump:             // Multi-way jump, would need unlocks on all paths
  case Op_JumpProj:
  case Op_Lock:                 // Nested lock elision anybody?
  case Op_MemBarAcquire:
  case Op_MemBarRelease:
  case Op_MemBarVolatile:
  case Op_MemBarCPUOrder:
    return false;

  case Op_Unlock:
    if( n->in(TypeFunc::Parms+0) != obj )
      return false;             // Oops mis-matching unlock/lock pair
    break;
  case Op_Region: 
    if( n->req() == 1 || !n->in(0) ) 
      return false;             // Known bad
    // Any 'bad' input means that should we lower the Unlock across the
    // Region, we would need to insert a matching Lock on the other 'bad'
    // inputs.  But inserting a Lock where one didn't exist before is unsafe -
    // we could deadlock.
for(uint i=1;i<n->req();i++)
      if( !scan_for_unlock(n->in(i),obj, limit-1, visited,ulregion) )
        return false; // Any bad input means we cannot lower unlock past Region
    break;                      // All is good, so Region is good as well
  case Op_CallRuntime:
    if( ((CallRuntimeNode*)n)->_entry_point != CAST_FROM_FN_PTR(address,OptoRuntime::multianewarray1_Java) &&
        ((CallRuntimeNode*)n)->_entry_point != CAST_FROM_FN_PTR(address,OptoRuntime::multianewarray2_Java) &&
        ((CallRuntimeNode*)n)->_entry_point != CAST_FROM_FN_PTR(address,OptoRuntime::multianewarray3_Java) &&
        ((CallRuntimeNode*)n)->_entry_point != CAST_FROM_FN_PTR(address,OptoRuntime::multianewarray4_Java) &&
        ((CallRuntimeNode*)n)->_entry_point != CAST_FROM_FN_PTR(address,OptoRuntime::multianewarray5_Java) ) 
      return false;             // Unknown VM call
    // Must be a multi-new-array allocation
    // Fall into next case
  case Op_Allocate:              // Ok to skip past these if I record in the 
  case Op_AllocateArray:         // debug info to unlock the extra lock.
  case Op_SafePoint:
    if( ((SafePointNode*)n)->_extra_lock ) {
if(PrintOpto)C2OUT->print_cr("Double-nested coarsening not enabled");
      return false;
    }
    // Fall into next case
  case Op_If:
  case Op_Proj:
  case Op_IfTrue:
  case Op_IfFalse:
    if( !scan_for_unlock(n->in(0),obj, limit-1, visited,ulregion) )
      return false;
    break;
  case Op_CallLeaf: 
  case Op_CallLeafNoFP: 
    assert0( n->as_CallLeaf()->entry_point() != StubRoutines::unlock_c2_entry() );
    // Unrelated Leaf calls always OK to lock across
    if( !scan_for_unlock(n->in(0),obj, limit-1, visited,ulregion) )
      return false;
    break;

  default:
#ifndef PRODUCT
C2OUT->print("OOPS: ");n->dump();
#endif
    ShouldNotReachHere();
  }
ulregion.set(n->_idx);//Must be OK!
  return true;
}

static void print_unlock( const char *msg, Node *n, VectorSet &visited ) {
  if( PrintOpto && n->in(0) ) {
    C2OUT->print(msg);  
n->dump();
  }
}

//-------------------------find_base_mem_def----------------------------------
// Find the Phat memory arriving at this ctrl Node.  Do it via crawling the
// idom tree, inserting Phi's if we need to merge multiple Phat memories all
// exiting the ulregion from different places.
Node *PhaseIdealLoop::find_base_mem_def(Node *ctrl, VectorSet &ulregion, Node_Array &base_mem_defs) {
  Node *x = 0;                  // Cached value
  Node *orig_ctrl = ctrl;       // For loading cache, ala Tarjan U-F style
  // Crawl the idom tree to find either the in/out point or the interior def
  // point.
  while( true ) {
    x = base_mem_defs[ctrl->_idx]; // Check cache
    if( x ) break;              // Cache hit, quit scanning
    // What kind of thing is ctrl?
int cop=ctrl->Opcode();
    // One side of if/then/else?
    if( cop == Op_IfFalse || cop == Op_IfTrue ) {
      // Crossing the in/out ul-region edge?
Node*next=idom(ctrl);
      if( !ulregion.test(ctrl->_idx) && 
           ulregion.test(next->_idx) ) {
        // Must be a standard exit point: a place where we need to insert a new Unlock
        //C2OUT->print("Enter ULRegion at "); ctrl->dump();
        x = MergeMemNode::make(C, find_base_mem_def(next, ulregion, base_mem_defs)); 
register_new_node(x,ctrl);
        break;
      }
    } else if( ctrl->is_Region() ) { // Includes, Region, Loop, CountedLoop
      // Merge point?  Check for prior phi; check for unique common input
      for (DUIterator_Fast umax, u = ctrl->fast_outs(umax); u < umax; u++) {
Node*phi=ctrl->fast_out(u);
        if( phi->is_Phi() && phi->as_Phi()->adr_type() == TypePtr::BOTTOM ) {
          base_mem_defs.map(ctrl->_idx,phi); // Set early, in case looping on self
x=phi;
          break;                // Prior Phi!  Use it.
        }
      }
      // If no Phi exists, make one eagerly and map it.  This prevents funny
      // looping patterns around irreducible loops - where the use comes from
      // a single def in the u-l region, but multiple exits from the u-l
      // region fall into different in-edges into an irreducible loop.
      if( !x ) {                // Make a Phi here
PhiNode*phi=new(C,ctrl->req())PhiNode(ctrl,Type::MEMORY,TypePtr::BOTTOM);
        base_mem_defs.map(ctrl->_idx,phi); // Set early, in case looping on self
for(uint i=1;i<ctrl->req();i++)//Set all inputs recursively
          phi->set_req(i,find_base_mem_def(ctrl->in(i), ulregion, base_mem_defs));
register_new_node(phi,ctrl);
x=phi;
      }
      //C2OUT->print("Need to merge at  "); ctrl->dump();
      break;
    } else if( cop == Op_If || cop == Op_Proj || cop == Op_CountedLoopEnd ||
               cop == Op_JumpProj || cop == Op_Jump ||
               cop == Op_Catch || cop == Op_CatchProj ||
               cop == Op_SafePoint || cop == Op_Parm ||
               // Allocate defines a private narrow slice, not fat memory
               cop == Op_Allocate || cop == Op_AllocateArray ) {
      // just do a simple walk
}else if(ctrl->is_Call()){
      if( !ctrl->is_CallLeaf() || ctrl->is_Unlock() ) {
        // It is an Unlock at u-l-region start, or some unknown call outside
        // the u-l region.  The call's fat-memory out is the base memory for
        // this path.
        x = ctrl->as_Call()->proj_out( TypeFunc::Memory );
        break;
      }
    } else if( cop == Op_Start || cop == Op_StartOSR || ctrl->is_MemBar() ) {
      x = ((MultiNode*)ctrl)->proj_out( TypeFunc::Memory );
      break;
    } else {
#ifndef PRODUCT
ctrl->dump(2);
#endif
      ShouldNotReachHere();
    }
    ctrl = idom(ctrl);          // Walk up idom tree.
  }

  // Repeat the idom crawl, setting the cache all along
assert0(x);
  assert0( !x->is_Phi() || x->in(0) );
  while( orig_ctrl != ctrl ) {
    base_mem_defs.map(orig_ctrl->_idx,x);
    orig_ctrl = idom(orig_ctrl);
  }
  base_mem_defs.map(orig_ctrl->_idx,x);
  return x;
}

void PhaseIdealLoop::set_narrow_slice(Node *ctrl, Node *def,int aix, Node_Array &base_mem_defs, VectorSet& visited) {
if(visited.test_set(def->_idx))return;

  Node *base = base_mem_defs[ctrl->_idx];
if(base->is_MergeMem()){
    ((MergeMemNode*)base)->set_memory_at(aix,def);
}else if(base->is_Phi()){
Node*r=base->in(0);
for(uint i=1;i<r->req();i++)
      set_narrow_slice(r->in(i),def,aix,base_mem_defs,visited);
  } else {
    ShouldNotReachHere();
  }
}


//-------------------------scan_mem_unlock------------------------------------
// Scan all users (recursively) to see if there is a memory DEF in the u-l
// region and a memory USE outside it.  
bool PhaseIdealLoop::scan_mem_unlock( Node *def, VectorSet &visited, VectorSet &ulregion, Node_Array &base_mem_defs ) {
  if( def->is_CFG() ) return false;
  if( visited.test_set(def->_idx) ) return false; // Already been here?
  if( !ulregion.test(get_ctrl(def)->_idx) ) return false; // Not a DEF in the u-l region
  if( def->bottom_type() != Type::MEMORY ) return false; // Does not DEF memory
int aix=C->get_alias_index(def->adr_type());

  // Scan all users.  Since calls to find_base_mem_def can remove users of DEF
  // I have a classic problem with modifying a collection (the def->out edges)
  // while iterating over the collection.  Solve it by repeated iteration with
  // a progress flag.
  DUIterator i;                 // Iterator scope outside while(progress) loop for new Sun asserts
  bool progress = true;
while(progress){//While progress
    progress = false;
    // Iterate over the outedges
for(i=def->outs();def->has_out(i);i++){
Node*use=def->out(i);
      for( uint j=1; j<use->req(); j++ ) {
        if( use->in(j) != def ) continue;
        Node *c = use->is_Phi() 
          ? use->in(0)->in(j)   // Phi uses come from the matching Region input path
          : (has_ctrl(use) ? get_ctrl(use) : use->in(0)); // Other uses from their block
        if( ulregion.test(c->_idx) ) { // Inside the unlock-lock region?
          // USE is inside and might be a recursive memory DEF that needs scanning.
          if( scan_mem_unlock( use, visited, ulregion, base_mem_defs ) )
            progress = true;
        } else {                // USE is outside
          // Recursively find the correct u-l-region exit.
          if( !base_mem_defs[c->_idx] ) // Check find_base_mem_def's cache
            progress = true; // Miss in cache means progress
          Node *x = find_base_mem_def(c, ulregion, base_mem_defs);
          // Use by the MergeMem we inserted just for this purpose is OK.
          if( use != x || use->is_Phi() ) {
            if( use->in(j) != x ) {
              _igvn.hash_delete(use);
use->set_req(j,x);
              _igvn._worklist.push(use);
              progress = true;
            }
            // We also found a potential narrow-slice user of 'def'.  All
            // the u-l region MergeMem exits should pick up a use of it.
            if( aix != Compile::AliasIdxBot && C->alias_type(aix)->is_rewritable()) {
ResourceArea*ra=Thread::current()->resource_area();
              VectorSet visited_for_slice(ra);
              set_narrow_slice(c,def,aix,base_mem_defs,visited_for_slice);
              progress = true;
            }
          }
        }
      }
      if( progress ) {          // Progress means we need to re-run
        i = def->refresh_out_pos(i); // reset for crazy new Sun asserts
      } else {
        assert0( def->out(i) );    // strictly for new internal Sun asserts
      }
    }
  }
  return true;
}

//-------------------------remove_unlock--------------------------------------
// (1) Actually remove the matching Unlocks & Locks
// (2) Insert Unlocks on any paths which exit the U-L region not through Locks.
//     No Locks need to be inserted by design (to hard to move debug info).
// (3) All memory edges DEF'd in the U-L region and USE'd outside it need to
//     be passed through the any NEW Unlock.  Such values already passed 
//     through the OLD Unlock, but the old one is going away.
//
void PhaseIdealLoop::remove_unlock( Node *n, VectorSet &visited, VectorSet &ulregion, Node *obj, Node_Array &base_mem_defs ) {
  if( !ulregion.test(n->_idx) ) return; // Outside the unlocked-lock region
  if( visited.test_set(n->_idx) ) return; // Already been here?

  switch( n->Opcode() ) {
  case Op_Region:
for(uint i=1;i<n->req();i++)
      remove_unlock(n->in(i), visited, ulregion, obj, base_mem_defs);
    print_unlock( "REGN: ", n, visited );  
    break;
  case Op_IfTrue:
  case Op_IfFalse: {
    remove_unlock(n->in(0), visited, ulregion, obj, base_mem_defs);
    // See if one arm of the If is in the new locked region and the other arm
    // out.  The exiting path needs an Unlock.
    IfNode *iff = (IfNode*)n->in(0);
    ProjNode *other = iff->proj_out(1-((ProjNode*)n)->_con);
if(!ulregion.test(other->_idx)){
      Node *mm = find_base_mem_def(other, ulregion, base_mem_defs);
      assert0( mm->is_MergeMem() );
      // New Unlock on this u-l region exit
      const TypeFunc *tf = OptoRuntime::complete_monitor_exit_Type();
UnlockNode*unlk=new(C,tf->domain()->cnt())UnlockNode(C,tf);
      ProjNode *cproj = new (C, 1) ProjNode(unlk,TypeFunc::Control);
      ProjNode *mproj = new (C, 1) ProjNode(unlk,TypeFunc::Memory );
      // Memory and Control users of the original exit control now come from
      // the Unlock call instead.
      _igvn.subsume_node_keep_old( other, cproj );
      _igvn.subsume_node_keep_old( mm   , mproj );
      // Standard Unlock args
unlk->init_req(TypeFunc::Control,other);
      unlk->init_req( TypeFunc::I_O      , C->top() ); // does no i/o
unlk->init_req(TypeFunc::Memory,mm);//the lock-exit memory state
      unlk->init_req( TypeFunc::ReturnAdr, C->top() );
      unlk->init_req( TypeFunc::FramePtr , C->top() );
unlk->init_req(TypeFunc::Parms+0,obj);
      // Update the loop-nest and idom info
IdealLoopTree*lock_loop=get_loop(other);
      set_loop(unlk ,lock_loop);
      set_loop(cproj,lock_loop);
      set_idom(unlk ,other,dom_depth(other));
      set_idom(cproj, unlk,dom_depth(other));
      if( !lock_loop->_child ) {
lock_loop->_body.push(unlk);
lock_loop->_body.push(cproj);
      }
_igvn.register_new_node_with_optimizer(unlk);
_igvn.register_new_node_with_optimizer(cproj);
      register_new_node(mproj,unlk);
      print_unlock( "EXIT: ", other, visited );
      print_unlock( "++UN: ", mm   , visited );
      print_unlock( "++UN: ", unlk , visited );
      print_unlock( "++UN: ", cproj, visited );
      print_unlock( "++UN: ", mproj, visited );
      // Original CFG-user of the exit point is now coming from the new Unlock
      // node; update the idom info also
for(DUIterator_Fast imax,i=cproj->fast_outs(imax);i<imax;i++){
Node*cfgout=cproj->fast_out(i);
        if( !cfgout->is_CFG() ) { // Non-CFG user?
          set_ctrl( cfgout, cproj ); // Must be a data node with a control edge
        } else if( idom(cfgout) == other ) {
          set_idom(cfgout,cproj,dom_depth(cfgout));
          print_unlock( "OLD : ", cfgout, visited );
        }
      }
    }
    print_unlock( "    : ", n, visited );  
    break;
  }

  case Op_If:                // Hoist through standard IF, if I see both sides
  case Op_Proj:
    remove_unlock(n->in(0), visited, ulregion, obj, base_mem_defs);
    print_unlock( "    : ", n, visited );  
    break;
  case Op_Allocate: // Ok to skip past these if I record in the debug info
  case Op_AllocateArray:
  case Op_CallRuntime:
  case Op_SafePoint: {
    SafePointNode *call = (SafePointNode*)n;
    call->_extra_lock = true;
JVMState*jvms=call->jvms();
    jvms->set_endoff( jvms->endoff()+1 );
    call->add_req(obj);
    remove_unlock(n->in(0), visited, ulregion, obj, base_mem_defs);
    print_unlock( "NEW : ", n, visited );  
    break;
  }

  case Op_CallLeaf:
  case Op_CallLeafNoFP:
    assert0( n->as_CallLeaf()->entry_point() != StubRoutines::unlock_c2_entry() );
    remove_unlock(n->in(0), visited, ulregion, obj, base_mem_defs);
    print_unlock("LEAF: ", n, visited );
    break;

  case Op_Unlock: {
UnlockNode*unlk=n->as_Unlock();
    ProjNode *ctlu = unlk->proj_out(TypeFunc::Control);
ProjNode*memu=unlk->proj_out(TypeFunc::Memory);
Node*unlk_ctl=unlk->in(TypeFunc::Control);
Node*unlk_mem=unlk->in(TypeFunc::Memory);
    assert0( unlk_mem && (!unlk_mem->is_Phi() || unlk_mem->in(0)) );

    print_unlock( "OLD : ", unlk_ctl, visited );  
    print_unlock( "--UN: ",    n, visited );  
    print_unlock( "--UN: ", ctlu, visited );
    base_mem_defs.map( unlk_ctl->_idx, unlk_mem );
    base_mem_defs.map( unlk    ->_idx, unlk_mem );
    base_mem_defs.map( ctlu    ->_idx, unlk_mem );
    scan_mem_unlock( memu, visited, ulregion, base_mem_defs );
    _igvn.add_users_to_worklist(ctlu);
_igvn.add_users_to_worklist(memu);
_igvn.hash_delete(memu);
_igvn.subsume_node(memu,unlk_mem);
    lazy_replace     ( unlk, unlk_ctl );
    lazy_replace_proj( ctlu, unlk_ctl );
    // The Unlock is going away, but we still need to scan his users for mem
    // ops def'd inside the u-l region but use'd outside.
    n = unlk_mem;               // Scan the unlk_mem for outside-region users
    break;
  }
  default:
    //C2OUT->print("OOPS: ");  n->dump();
    ShouldNotReachHere();
  }
  // Scan all users (recursively) to see if there is a memory DEF in the u-l
  // region and a memory USE outside it.
  for (DUIterator_Fast imax, i = n->fast_outs(imax); i < imax; i++) {
    scan_mem_unlock( n->fast_out(i), visited, ulregion, base_mem_defs );
  }
}

//------------------------------try_to_coarsen--------------------------------
void PhaseIdealLoop::try_to_coarsen(Node*obj){
  // Scan up from the Locks until we find something we cannot coarsen across
  // (e.g., a foreign call which might take locks) or a matching Unlock.
  // Everything visited is marked with the 'visited' Vectorset.  Everything
  // both visited AND in the known good unlock-lock coarsen region is marked
  // with the 'ulregion' Vectorset.
ResourceArea*ra=Thread::current()->resource_area();
VectorSet visited(ra);
  VectorSet ulregion(ra);

  bool find_ul_region = true;
  while( find_ul_region ) {     // While need to find the ul-region
    find_ul_region = false;
    visited.Clear();
ulregion.Clear();
    bool found_unlock = false;  // Not found an unlock to remove yet
for(uint i=0;i<_locks.size();i++){
Node*x=_locks[i];
      if( x->in(TypeFunc::Parms+0) == obj && 
          x->is_Lock() ) {
        if( scan_for_unlock( x->in(0), obj, 20, visited, ulregion ) ) {
          ulregion.set(x->_idx); // This counts as 'in' the u-l region
          found_unlock = true;
        } else {                // This lock cannot be coarsened
          _locks.remove(i--);   // Remove this Lock from the set
          find_ul_region = true; // Need to recompute the u-l region
        }
      }
    }

    if( !found_unlock ) return;
  }


  // Great, we found some locks to coarsen.  We need to do a few things:
  // (1) Actually remove the matching Unlocks & Locks
  // (2) Insert Unlocks on any paths which exit the U-L region not through Locks.
  //     No Locks need to be inserted by design (too hard to move debug info).
  // (3) All memory edges DEF'd in the U-L region and USE'd outside it need to
  //     be passed through any NEW Unlocks.  

if(PrintOpto)C2OUT->print_cr("Found aggressive lock hoist canceling unlocks");
  visited.Clear();
  Node_Array base_mem_defs(ra);
for(uint i=0;i<_locks.size();i++){
Node*x=_locks[i];
    if( x->in(TypeFunc::Parms+0) == obj && 
        x->is_Lock() &&
        ulregion.test(x->_idx) ) {
      remove_unlock( x->in(0), visited, ulregion, obj, base_mem_defs );
LockNode*lock=x->as_Lock();
      ProjNode *ctru = lock->proj_out(TypeFunc::Control);
ProjNode*memu=lock->proj_out(TypeFunc::Memory);
      print_unlock("--LK: ", lock, visited );
      print_unlock("--LK: ", ctru, visited );
      _igvn.add_users_to_worklist(ctru);
      _igvn.add_users_to_worklist(memu);
Node*c=lock->in(TypeFunc::Control);
      lazy_replace_proj(ctru, c);
      lazy_replace_proj(memu, lock->in(TypeFunc::Memory ));
      lazy_replace     (lock, c);
    }
  }
}
