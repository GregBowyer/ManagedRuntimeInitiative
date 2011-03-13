/*
 * Copyright 1998-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


// Optimization - Graph Style

#include "ad_pd.hpp"
#include "assembler_pd.hpp"
#include "block.hpp"
#include "c2compiler.hpp"
#include "cfgnode.hpp"
#include "machnode.hpp"
#include "matcher.hpp"
#include "memnode.hpp"
#include "opcodes.hpp"
#include "type.hpp"

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

//------------------------------implicit_null_check----------------------------
// Detect implicit-null-check opportunities.  Basically, find NULL checks 
// with suitable memory ops nearby.  Use the memory op to do the NULL check.
// I can generate a memory op if there is not one nearby.
// The proj is the control projection for the not-null case.
// The val is the pointer being checked for nullness.
void Block::implicit_null_check(PhaseCFG*cfg,Node*proj,Node*val){
  // Assume if null check need for 0 offset then always needed
  // Intel solaris doesn't support any null checks yet and no
  // mechanism exists (yet) to set the switches at an os_cpu level
  if( !ImplicitNullChecks || MacroAssembler::needs_explicit_null_check(0)) return;

  // Make sure the ptr-is-null path appears to be uncommon!
  float f = end()->as_MachIf()->_prob;
  if( proj->Opcode() == Op_IfTrue ) f = 1.0f - f;
  if( f > PROB_UNLIKELY_MAG(4) ) return;

  uint bidx = 0;                // Capture index of value into memop
  bool was_store, was_range, was_loadplock;    // Memory op is a store op
  Compile *C = Compile::current();

  // Search the successor block for a load or store who's base value is also
  // the tested value.  There may be several.
  Node_List *out = new Node_List(Thread::current()->resource_area());
  MachNode *best = NULL;        // Best found so far
  for (DUIterator i = val->outs(); val->has_out(i); i++) {
    Node *m = val->out(i);
    if( !m->is_Mach() ) continue;
    MachNode *mach = m->as_Mach();
    was_store = was_range = was_loadplock = false;
    switch( mach->ideal_Opcode() ) {
    case Op_LoadB:
    case Op_LoadC:
    case Op_LoadD:
    case Op_LoadF:
    case Op_LoadI:
    case Op_LoadL:
    case Op_LoadP:
    case Op_LoadS:
    case Op_LoadKlass:
    case Op_LoadRange:
      break;
    case Op_LoadPLocked:
      was_loadplock = true;
      break;
    case Op_StoreP:
      if( RefPoisoning )        // Too hard to do right; 
        continue;               // poisoning logic messes up where null check happens
    case Op_StoreB:
    case Op_StoreC:
    case Op_StoreD:
    case Op_StoreF:
    case Op_StoreI:
    case Op_StoreL:
      was_store = true;         // Memory op is a store op
      // Stores will have their address in slot 2 (memory in slot 1).
      // If the value being nul-checked is in another slot, it means we
      // are storing the checked value, which does NOT check the value!
      if( mach->in(2) != val ) continue;
      break;                    // Found a memory op?
    case Op_StrComp:
    case Op_StrEquals:
      // Not a legit memory op for implicit null check regardless of 
      // embedded loads
      continue;
    case Op_GetKID:
      if( !KIDInRef ) break;    // Must do a load hence null-check
      continue;                 // Does not do a load
    default:                    // Also check for embedded loads
      if( !mach->needs_anti_dependence_check() )
        continue;               // Not an memory op; skip it
      break;
    }
    // check if the offset is not too high for implicit exception
    {
      intptr_t offset = 0;
      const TypePtr *adr_type = NULL;  // Do not need this return value here
      const Node* base = mach->get_base_and_disp(offset, adr_type);
      if (base == NULL || base == NodeSentinel) {
        // cannot reason about it; is probably not implicit null exception
        // Do not be confused with storing a value into pending_exception.
        // The TLS store has no address input (it's implicitly R29) and so
        // input #2 is really the value field.
        if( base == NULL && offset == in_bytes(JavaThread::pending_exception_offset()) )
          continue;
      } else {
        const TypePtr* tptr = base->bottom_type()->is_ptr();
        // Give up if offset is not a compile-time constant
        if( offset == Type::OffsetBot || tptr->_offset == Type::OffsetBot )
          continue;
        offset += tptr->_offset; // correct if base is offseted
        if( MacroAssembler::needs_explicit_null_check(offset) ) 
          continue;             // Give up is reference is beyond 4K page size
      }
    }

    // Check ctrl input to see if the null-check dominates the memory op
    Block *cb = cfg->_bbs[mach->_idx];
Node*last_cb_ctrl=cb->_nodes[0];
    cb = cb->_idom;             // Always hoist at least 1 block
//Stores can be hoisted only one block
    // Same issue with RangeCheck: it has control flow.
    // LoadPLocked: cannot hoist past any safepoint which may deflate pre-loaded header
    if( !was_store && !was_range && !was_loadplock ) {
      while( cb->_dom_depth > _dom_depth ) {
        last_cb_ctrl = cb->_nodes[0];
        cb = cb->_idom;         // Hoist loads as far as we want
      }
#if 0
      // SUN's change here appears to gut the usefulness of this optimization.
      // The spiller should be fixed instead
      while( cb->_dom_depth > (_dom_depth + 1) ) {
        last_cb_ctrl = cb->_nodes[0];
        cb = cb->_idom;         // Hoist loads as far as we want
      }
      // The non-null-block should dominate the memory op, too. Live
      // range spilling will insert a spill in the non-null-block if it is
      // needs to spill the memory op for an implicit null check.
      if (cb->_dom_depth == (_dom_depth + 1)) {
        if (cb != not_null_block) continue;
        cb = cb->_idom;
      }
#endif
    }
    if( cb != this ) continue;
    // This whole routine attempts to hoist a LoadNode up above a null-check,
    // to allow the LoadNode to do the null-check "for free" in the hardware.
    // However, conceptually the LoadNode only returns a value if the test
    // passes; if the base address is NULL the LoadNode never completes
    // (faults instead).  In other words, the LoadNode's value is only DEF'd
    // on one arm of the conditional.  If the loaded value is then used below
    // the merge point of both paths, we potentially have a USE with no DEF.
    // The register allocator can end up spilling the LoadNode's result, past
    // the NULL-check, and thus down one arm only of the conditional.  Later
    // the allocator asserts because there is no DEF on the other arm.  Fixed
    // by not hoisting if we would hoist above the post-dominating point.
    if( last_cb_ctrl->is_Region() && last_cb_ctrl->req() > 2 ) continue;

    // Found a memory user; see if it can be hoisted to check-block
    uint vidx = 0;              // Capture index of value into memop
    uint j;
    for( j = mach->req()-1; j > 0; j-- ) {
      if( mach->in(j) == val ) vidx = j;
      // Block of memory-op input
      Block *inb = cfg->_bbs[mach->in(j)->_idx];
      Block *b = this;          // Start from nul check
      while( b != inb && b->_dom_depth > inb->_dom_depth )
        b = b->_idom;           // search upwards for input
      // See if input dominates null check
      if( b != inb )
        break;
    }
    if( j > 0 ) 
      continue;
    Block *mb = cfg->_bbs[mach->_idx]; 
    // Hoisting stores requires more checks for the anti-dependence case.
    // Give up hoisting if we have to move the store past any load.
    if( was_store ) {
      Block *b = mb;            // Start searching here for a local load
      // mach use (faulting) trying to hoist
      // n might be blocker to hoisting
      while( b != this ) {
        uint k;
        for( k = 1; k < b->_nodes.size(); k++ ) {
          Node *n = b->_nodes[k];
          if( n->needs_anti_dependence_check() && 
              n->in(LoadNode::Memory) == mach->in(StoreNode::Memory) )
            break;              // Found anti-dependent load
        }
        if( k < b->_nodes.size() )
          break;                // Found anti-dependent load
        // Make sure control does not do a merge (would have to check allpaths)
        if( b->num_preds() != 2 ) break;
        b = cfg->_bbs[b->pred(1)->_idx]; // Move up to predecessor block
      }
      if( b != this ) continue;
    }

    // This requires the block is empty
    if( was_range ) {
      if( mb->num_preds() != 2 ) continue; // Merge in between?
uint i;//See if I can hoist all in-between
for(i=1;i<mb->end_idx();i++)
        if( mb->_nodes[i]->in(0) == mb->_nodes[0] )
          break;                // Control-dependent on the null check
      if( i < mb->end_idx() )   // If control-dependent on the null check
        continue;               // then I cannot fold
    }

    // Make sure this memory op is not already being used for a NullCheck
    Node *e = mb->end();
    if( e->is_MachNullCheck() && e->in(1) == mach )
      continue;                 // Already being used as a NULL check

    // Found a candidate!  Pick one with least dom depth - the highest 
    // in the dom tree should be closest to the null check.
    if( !best || 
(cfg->_bbs[mach->_idx]->_dom_depth<cfg->_bbs[best->_idx]->_dom_depth)||
        (must_clone[best->ideal_Opcode()] > must_clone[mach->ideal_Opcode()]) ) {
      best = mach;
      bidx = vidx;

    }
  }
  // No candidate!
  if( !best ) return;

  // ---- Found an implicit null check
  extern int implicit_null_checks;
  implicit_null_checks++;

  // Hoist the memory candidate up to the end of the test block.
  Block *old_block = cfg->_bbs[best->_idx];
  old_block->find_remove(best);
  add_inst(best);
  cfg->_bbs.map(best->_idx,this);

  // Move the control dependence
  if (best->in(0) && best->in(0) == old_block->_nodes[0])
    best->set_req(0, _nodes[0]);

  // Check for flag-killing projections that also need to be hoisted
  // Should be DU safe because no edge updates.
  for (DUIterator_Fast jmax, j = best->fast_outs(jmax); j < jmax; j++) {
    Node* n = best->fast_out(j);
    if( n->Opcode() == Op_MachProj ) {
      cfg->_bbs[n->_idx]->find_remove(n);
      add_inst(n);
      cfg->_bbs.map(n->_idx,this);
    }
  }

  // proj==Op_True --> ne test; proj==Op_False --> eq test.
  // One of two graph shapes got matched:
  //   (IfTrue  (If (Bool NE (CmpP ptr NULL))))
  //   (IfFalse (If (Bool EQ (CmpP ptr NULL))))
  // NULL checks are always branch-if-eq.  If we see a IfTrue projection
  // then we are replacing a 'ne' test with a 'eq' NULL check test.
  // We need to flip the projections to keep the same semantics.
  if( proj->Opcode() == Op_IfTrue ) {
    // Swap order of projections in basic block to swap branch targets
    Node *tmp1 = _nodes[end_idx()+1];
    Node *tmp2 = _nodes[end_idx()+2];
    _nodes.map(end_idx()+1, tmp2);
    _nodes.map(end_idx()+2, tmp1);    
    Node *tmp = new (C, 1) Node(C->top()); // Use not NULL input
    tmp1->replace_by(tmp);
    tmp2->replace_by(tmp1);
    tmp->replace_by(tmp2);
    tmp->destruct();
  }

  // Remove the existing null check; use a new implicit null check instead.
  // Since schedule-local needs precise def-use info, we need to correct
  // it as well.
  Node *old_tst = proj->in(0);
  MachNode *nul_chk = new (C) MachNullCheckNode(old_tst->in(0),best,bidx);
  _nodes.map(end_idx(),nul_chk);
  cfg->_bbs.map(nul_chk->_idx,this);
  // Redirect users of old_test to nul_chk
  for (DUIterator_Last i2min, i2 = old_tst->last_outs(i2min); i2 >= i2min; --i2)
    old_tst->last_out(i2)->set_req(0, nul_chk);
  // Clean-up any dead code
  for (uint i3 = 0; i3 < old_tst->req(); i3++)
    old_tst->set_req(i3, NULL);

  cfg->latency_from_uses(nul_chk);
  cfg->latency_from_uses(best);
}


//------------------------------select-----------------------------------------
// Select a nice fellow from the worklist to schedule next. If there is only
// one choice, then use it. Projections take top priority for correctness
// reasons - if I see a projection, then it is next.  There are a number of
// other special cases, for instructions that consume condition codes, et al.
// These are chosen immediately. Some instructions are required to immediately
// precede the last instruction in the block, and these are taken last. Of the
// remaining cases (most), choose the instruction with the greatest latency
// (that is, the most number of pseudo-cycles required to the end of the
// routine). If there is a tie, choose the instruction with the most inputs.
Node *Block::select(PhaseCFG *cfg, Node_List &worklist, int *ready_cnt, VectorSet &next_call, uint sched_slot) {

  // If only a single entry on the stack, use it
  uint cnt = worklist.size();
  if (cnt == 1) {
    Node *n = worklist[0];
    worklist.map(0,worklist.pop());
    return n;
  }

  // Some common v-calls hoisted
Node*e=end();//End instruction in block

  uint choice  = 0; // Bigger is most important
  uint latency = 0; // Bigger is scheduled first
jint score=0;//Bigger is better
uint idx=0;//Index in worklist

  bool saw_lvb = false;

  for( uint i=0; i<cnt; i++ ) { // Inspect entire worklist
    // Order in worklist is used to break ties.
    // See caller for how this is used to delay scheduling
    // of induction variable increments to after the other
    // uses of the phi are scheduled.
    Node *n = worklist[i];      // Get Node on worklist

int iop=n->is_Mach()?n->as_Mach()->ideal_Opcode():n->Opcode();
    if( n->is_Proj() ||         // Projections always win
iop==Op_Con||//So does constant 'Top'
        iop == Op_CheckCastPP || // No point in delaying these
        iop == Op_EscapeMemory || // No point in delaying these
        iop == Op_ArrayCopy     // Schedule soonest to keep together
        ) {  
      assert( iop != Op_ArrayCopy || 
(n->in(1)->Opcode()==Op_ArrayCopySrc&&
               cfg->_bbs[n->in(1)->_idx] == this), "ArrayCopy mis-scheduled" );
      worklist.map(i,worklist.pop());
      return n;
    }

    // Final call in a block must be adjacent to 'catch'
    if( e->is_Catch() && e->in(0)->in(0) == n )
      continue;

    // Memory op for an implicit null check has to be at the end of the block
    if( e->is_MachNullCheck() && e->in(1) == n )
      continue;

    uint n_choice  = 2;

    // Delay ArrayCopySrc as long as possible, so it will schedule
    // next to ArrayCopy.  We cannot let an aliasing Store appear
    // between an ArrayCopySrc and it's associated ArrayCopy.
if(n->Opcode()==Op_ArrayCopySrc){
      assert0( iop == Op_ArrayCopySrc );
      n_choice = 1;
    }

    // See if this instruction is consumed by a branch. If so, then (as the
    // branch is the last instruction in the basic block) force it to the
    // end of the basic block
    if ( must_clone[iop] ) {
      // See if any use is a branch
      bool found_machif = false;

      for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
        Node* use = n->fast_out(j);

        // The use is a conditional branch, make them adjacent
        if (use->is_MachIf() && cfg->_bbs[use->_idx]==this ) {
          found_machif = true;
          break;
        }

        // More than this instruction pending for successor to be ready,
        // don't choose this if other opportunities are ready
        if (ready_cnt[use->_idx] > 1 && use->is_Mach() && use->ideal_reg() == Op_RegFlags)
          n_choice = 1;
      }
  
      // loop terminated, prefer not to use this instruction
      if (found_machif)
        continue;
    }

    // See if this has a predecessor that is "must_clone", i.e. sets the
    // condition code. If so, choose this first
    for (uint j = 0; j < n->req() ; j++) {
      Node *inn = n->in(j);
      if (inn) {
        if (inn->is_Mach() && must_clone[inn->as_Mach()->ideal_Opcode()] ) {
          n_choice = 3;
          break;
        }
      }
    }

    // Do not let LVBs cross safepoints.  LVBs are ready as soon as their oop
    // load has been scheduled.  Hence if an oop load has been scheduled, an
    // LVB is ready.  If both an LVB and a GC safepoint are ready, always pick
    // the LVB first.  Annoyingly this doesn't pop out from just using some
    // kind of absolute 'choice' or 'score' - because the situation isn't
    // transitive: "score(LD8)==score(SAFE)" and "score(LD8)==score(LVB)" and
    // "score(LVB) > score(SAFE)".
    if( n->is_Mach() && n->as_Mach()->is_MachLVB() ) {
      saw_lvb = true;
      if( worklist[idx]->jvms() ) // Is current winner a GC Safepoint?
        n_choice = choice+1;  // Then force this LVB to win over the safepoint
    }

    if( saw_lvb && n->jvms() )  // There's a ready LVB Out There somewhere
      continue;                 // Do NOT pick this safepoint

    // Break 'choice' ties with latency.  This is latency from here to the end
    // of the method; larger implies a longer execution path (and thus more
    // important to schedule sooner).
    uint n_latency = cfg->_node_latency.at_grow(n->_idx);
    // Break 'latency' ties by avoiding scheduling uses directly after defs
    // until the expected def/use latency is covered.
    jint local_stall = 0;
    for( uint j=0; j<n->req(); j++ ) {
      // Find a local d/u edge
Node*x=n->in(j);
      if( !x ) continue;
if(cfg->_bbs[x->_idx]!=this)continue;
      // Estimate latency along this edge
      int def_lat = cfg->_node_latency.at_grow(x->_idx);
      int x_slot = find_node(x);
      def_lat -= (sched_slot - x_slot); // def had some latency covered
      int stall_for_x = def_lat - n_latency;
      if( stall_for_x > local_stall ) local_stall = stall_for_x;
    }
    jint n_score = -local_stall; // Score: bigger is scheduled sooner

    // Keep best latency found
    if( choice < n_choice ||
        ( choice == n_choice &&
          ( latency < n_latency ||
            ( latency == n_latency &&
              ( score < n_score ))))) {
      choice  = n_choice;
      latency = n_latency;
      score   = n_score;
      idx     = i;               // Also keep index in worklist
    }
  } // End of for all ready nodes in worklist

  Node *n = worklist[idx];      // Get the winner

  worklist.map(idx,worklist.pop());     // Compress worklist
  return n;
}


//------------------------------set_next_call----------------------------------
void Block::set_next_call( Node *n, VectorSet &next_call, Block_Array &bbs ) {
  if( next_call.test_set(n->_idx) ) return;
  for( uint i=0; i<n->len(); i++ ) {
    Node *m = n->in(i);
    if( !m ) continue;  // must see all nodes in block that precede call
    if( bbs[m->_idx] == this ) 
      set_next_call( m, next_call, bbs );
  }
}

//------------------------------needed_for_next_call---------------------------
// Set the flag 'next_call' for each Node that is needed for the next call to
// be scheduled.  This flag lets me bias scheduling so Nodes needed for the 
// next subroutine call get priority - basically it moves things NOT needed
// for the next call till after the call.  This prevents me from trying to 
// carry lots of stuff live across a call.
void Block::needed_for_next_call(Node *this_call, VectorSet &next_call, Block_Array &bbs) {
  // Find the next control-defining Node in this block
  Node* call = NULL;
  for (DUIterator_Fast imax, i = this_call->fast_outs(imax); i < imax; i++) {
    Node* m = this_call->fast_out(i);
    if( bbs[m->_idx] == this && // Local-block user
        m != this_call &&       // Not self-start node
        m->is_Call() )
      call = m;
      break;
  }
  if (call == NULL)  return;    // No next call (e.g., block end is near)
  // Set next-call for all inputs to this call
  set_next_call(call, next_call, bbs);
}

//------------------------------sched_call-------------------------------------
uint Block::sched_call( Matcher &matcher, Block_Array &bbs, uint node_cnt, Node_List &worklist, int *ready_cnt, MachCallNode *mcall, VectorSet &next_call ) {
  RegMask regs;

  // Schedule all the users of the call right now.  All the users are
  // projection Nodes, so they must be scheduled next to the call.
  // Collect all the defined registers.
  for (DUIterator_Fast imax, i = mcall->fast_outs(imax); i < imax; i++) {
    Node* n = mcall->fast_out(i);
    assert( n->Opcode()==Op_MachProj, "" );
    --ready_cnt[n->_idx];
    assert( !ready_cnt[n->_idx], "" );
    // Schedule next to call
    _nodes.map(node_cnt++, n);
    // Collect defined registers
    regs.OR(n->out_RegMask());
    // Check for scheduling the next control-definer
    if( n->bottom_type() == Type::CONTROL ) 
      // Warm up next pile of heuristic bits
      needed_for_next_call(n, next_call, bbs);

    // Children of projections are now all ready
    for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
      Node* m = n->fast_out(j); // Get user
      if( bbs[m->_idx] != this ) continue;
      if( m->is_Phi() ) continue;
      if( !--ready_cnt[m->_idx] ) 
        worklist.push(m);
    }
  
  }

  // Act as if the call defines the Frame Pointer.
  // Certainly the FP is alive and well after the call.
  regs.Insert(matcher.c_frame_pointer());

  // Set all registers killed and not already defined by the call.
  uint r_cnt = mcall->tf()->range()->cnt();
  int op = mcall->ideal_Opcode();
  MachProjNode *proj = new (matcher.C, 1) MachProjNode( mcall, r_cnt+1, RegMask::Empty, MachProjNode::fat_proj );
  bbs.map(proj->_idx,this);
  _nodes.insert(node_cnt++, proj);

  for( OptoReg::Name r = OptoReg::Name(0); r < _last_Mach_Reg; r=OptoReg::add(r,1) ) {
    if( !regs.Member(r) ) {     // Not already defined by the call  
      // Save-on-call register?
      if( (matcher._register_save_policy[r] == 'C') ||
          (matcher._register_save_policy[r] == 'A') ||
          0 ) {
        proj->_rout.Insert(r);
      }
    }
  }

  return node_cnt;
}


//------------------------------schedule_local---------------------------------
// Topological sort within a block.  Someday become a real scheduler.
bool Block::schedule_local(PhaseCFG *cfg, Matcher &matcher, int *ready_cnt, VectorSet &next_call) {
  // Already "sorted" are the block start Node (as the first entry), and
  // the block-ending Node and any trailing control projections.  We leave
  // these alone.  PhiNodes and ParmNodes are made to follow the block start
  // Node.  Everything else gets topo-sorted.

#ifndef PRODUCT
    if (cfg->trace_opto_pipelining()) {
      C2OUT->print("# --- schedule_local B%d, before: ---\n", _pre_order);
      for (uint i = 0;i < _nodes.size();i++) {
C2OUT->print("# ");
        _nodes[i]->fast_dump();
      }
C2OUT->print("#\n");
    }
#endif

  // RootNode is already sorted
  if( _nodes.size() == 1 ) return true;

  // Move PhiNodes and ParmNodes from 1 to cnt up to the start
  uint node_cnt = end_idx();
  uint phi_cnt = 1;
  uint i;
  for( i = 1; i<node_cnt; i++ ) { // Scan for Phi
    Node *n = _nodes[i];
    if( n->is_Phi() ||          // Found a PhiNode or ParmNode
        (n->is_Proj()  && n->in(0) == head()) ) {
      // Move guy at 'phi_cnt' to the end; makes a hole at phi_cnt
      _nodes.map(i,_nodes[phi_cnt]);
      _nodes.map(phi_cnt++,n);  // swap Phi/Parm up front
    } else {                    // All others
      // Count block-local inputs to 'n'
      uint cnt = n->len();      // Input count
      uint local = 0;
      for( uint j=0; j<cnt; j++ ) {
        Node *m = n->in(j);
        if( m && cfg->_bbs[m->_idx] == this && !m->is_top() )
          local++;              // One more block-local input
      }
      ready_cnt[n->_idx] = local; // Count em up

      // A few node types require changing a required edge to a precedence edge
      // before allocation.
      if( n->is_Mach() && n->as_Mach()->ideal_Opcode() == Op_MemBarAcquire ) {
        Node *x = n->in(TypeFunc::Parms);
        n->del_req(TypeFunc::Parms);
        n->add_prec(x);
      }
    }
  }
  for(uint i2=i; i2<_nodes.size(); i2++ ) // Trailing guys get zapped count
    ready_cnt[_nodes[i2]->_idx] = 0;

  // All the prescheduled guys do not hold back internal nodes
  uint i3;
  for(i3 = 0; i3<phi_cnt; i3++ ) {  // For all pre-scheduled
    Node *n = _nodes[i3];       // Get pre-scheduled
    for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
      Node* m = n->fast_out(j);
      if( cfg->_bbs[m->_idx] ==this ) // Local-block user
        ready_cnt[m->_idx]--;   // Fix ready count
    }
  }

  Node_List delay;
  // Make a worklist
  Node_List worklist;
  for(uint i4=i3; i4<node_cnt; i4++ ) {    // Put ready guys on worklist
    Node *m = _nodes[i4];    
    if( !ready_cnt[m->_idx] ) {   // Zero ready count?
      if (m->is_iteratively_computed()) {
        // Push induction variable increments last to allow other uses
        // of the phi to be scheduled first. The select() method breaks
        // ties in scheduling by worklist order.
        delay.push(m);
      } else {
        worklist.push(m);         // Then on to worklist!
      }
    }
  }
  while (delay.size()) {
    Node* d = delay.pop();
    worklist.push(d);
  }

  // Warm up the 'next_call' heuristic bits
  needed_for_next_call(_nodes[0], next_call, cfg->_bbs);

#ifndef PRODUCT
    if (cfg->trace_opto_pipelining()) {
      for (uint j=0; j<_nodes.size(); j++) {
        Node     *n = _nodes[j];
        int     idx = n->_idx;
C2OUT->print("#   ready cnt:%3d  ",ready_cnt[idx]);
C2OUT->print("latency:%3d  ",cfg->_node_latency.at_grow(idx));
C2OUT->print("%4d: %s\n",idx,n->Name());
      }
    }
#endif

  // Pull from worklist and schedule
  while( worklist.size() ) {    // Worklist is not ready

#ifndef PRODUCT
    if (cfg->trace_opto_pipelining()) {
C2OUT->print("#    ready list:");
      for( uint i=0; i<worklist.size(); i++ ) { // Inspect entire worklist
        Node *n = worklist[i];      // Get Node on worklist
C2OUT->print(" %d",n->_idx);
      }
C2OUT->print("\n");
    }
#endif

    // Select and pop a ready guy from worklist
    Node* n = select(cfg, worklist, ready_cnt, next_call, phi_cnt);
    _nodes.map(phi_cnt++,n);    // Schedule him next

#ifndef PRODUCT
    if (cfg->trace_opto_pipelining()) {
C2OUT->print("#  select %d: %s",n->_idx,n->Name());
C2OUT->print(", latency:%d",cfg->_node_latency.at_grow(n->_idx));
      n->dump();
      if (Verbose) {
C2OUT->print("#    ready list:");
        for( uint i=0; i<worklist.size(); i++ ) { // Inspect entire worklist
          Node *n = worklist[i];      // Get Node on worklist
C2OUT->print(" %d",n->_idx);
        }
C2OUT->print("\n");
      }
    }

#endif
    if( n->is_MachCall() ) {
      MachCallNode *mcall = n->as_MachCall();
      phi_cnt = sched_call(matcher, cfg->_bbs, phi_cnt, worklist, ready_cnt, mcall, next_call);
      continue;
    }
    // Children are now all ready
    for (DUIterator_Fast i5max, i5 = n->fast_outs(i5max); i5 < i5max; i5++) {
      Node* m = n->fast_out(i5); // Get user
      if( cfg->_bbs[m->_idx] != this ) continue;
      if( m->is_Phi() ) continue;
      if( !--ready_cnt[m->_idx] ) 
        worklist.push(m);
    }
  }

  if( phi_cnt != end_idx() ) {
    // did not schedule all.  Retry, Bailout, or Die
    Compile* C = matcher.C;
    if (C->subsume_loads() == true && !C->failing()) {
      // Retry with subsume_loads == false
      // If this is the first failure, the sentinel string will "stick"
      // to the Compile object, and the C2Compiler will see it and retry.
C->record_failure(C2Compiler::retry_no_subsuming_loads(),true);
    }
    // assert( phi_cnt == end_idx(), "did not schedule all" );
    return false;
  }

#ifndef PRODUCT
  if (cfg->trace_opto_pipelining()) {
C2OUT->print("#\n");
C2OUT->print("# after schedule_local\n");
    for (uint i = 0;i < _nodes.size();i++) {
C2OUT->print("# ");
      _nodes[i]->fast_dump();
    }
C2OUT->print("\n");
  }
#endif


  return true;
}

//--------------------------catch_cleanup_fix_all_inputs-----------------------
static void catch_cleanup_fix_all_inputs(Node *use, Node *old_def, Node *new_def) {
  for (uint l = 0; l < use->len(); l++) {
    if (use->in(l) == old_def) {
      if (l < use->req()) {
        use->set_req(l, new_def);
      } else {
        use->rm_prec(l);
        use->add_prec(new_def);
        l--;
      }
    }
  }
}

//------------------------------catch_cleanup_find_cloned_def------------------
static Node *catch_cleanup_find_cloned_def(Block *use_blk, Node *def, Block *def_blk, Block_Array &bbs, int n_clone_idx) {
  assert( use_blk != def_blk, "Inter-block cleanup only");
  
  // The use is some block below the Catch.  Find and return the clone of the def
  // that dominates the use. If there is no clone in a dominating block, then
  // create a phi for the def in a dominating block.
  
  // Find which successor block dominates this use.  The successor
  // blocks must all be single-entry (from the Catch only; I will have
  // split blocks to make this so), hence they all dominate.
  while( use_blk->_dom_depth > def_blk->_dom_depth+1 )
    use_blk = use_blk->_idom;
  
  // Find the successor
  Node *fixup = NULL;

  uint j;
  for( j = 0; j < def_blk->_num_succs; j++ )
    if( use_blk == def_blk->_succs[j] ) 
      break;

  if( j == def_blk->_num_succs ) {
    // Block at same level in dom-tree is not a successor.  It needs a 
    // PhiNode, the PhiNode uses from the def and IT's uses need fixup.
    Node_Array inputs = new Node_List(Thread::current()->resource_area());
    for(uint k = 1; k < use_blk->num_preds(); k++) {
      inputs.map(k, catch_cleanup_find_cloned_def(bbs[use_blk->pred(k)->_idx], def, def_blk, bbs, n_clone_idx));
    }

    // Check to see if the use_blk already has an identical phi inserted.  
    // If it exists, it will be at the first position since all uses of a 
    // def are processed together.
    Node *phi = use_blk->_nodes[1];
    if( phi->is_Phi() ) {
      fixup = phi;
      for (uint k = 1; k < use_blk->num_preds(); k++) {
        if (phi->in(k) != inputs[k]) {
          // Not a match
          fixup = NULL;
          break;
        }
      }
    }

    // If an existing PhiNode was not found, make a new one.
    if (fixup == NULL) {
      Node *new_phi = PhiNode::make(use_blk->head(), def);
      use_blk->_nodes.insert(1, new_phi);
      bbs.map(new_phi->_idx, use_blk);
      for (uint k = 1; k < use_blk->num_preds(); k++) {
        new_phi->set_req(k, inputs[k]);
      }
      fixup = new_phi;
    }
    
  } else {
    // Found the use just below the Catch.  Make it use the clone.
    fixup = use_blk->_nodes[n_clone_idx];
  }

  return fixup;
}

//--------------------------catch_cleanup_intra_block--------------------------
// Fix all input edges in use that reference "def".  The use is in the same
// block as the def and both have been cloned in each successor block.
static void catch_cleanup_intra_block(Node *use, Node *def, Block *blk, int beg, int n_clone_idx) {

  // Both the use and def have been cloned. For each successor block,
  // get the clone of the use, and make its input the clone of the def
  // found in that block.

  uint use_idx = blk->find_node(use);
  uint offset_idx = use_idx - beg;
  for( uint k = 0; k < blk->_num_succs; k++ ) {
    // Get clone in each successor block
    Block *sb = blk->_succs[k];
    Node *clone = sb->_nodes[offset_idx+1];
    assert( clone->Opcode() == use->Opcode(), "" );

    // Make use-clone reference the def-clone
    catch_cleanup_fix_all_inputs(clone, def, sb->_nodes[n_clone_idx]);
  }
}

//------------------------------catch_cleanup_inter_block---------------------
// Fix all input edges in use that reference "def".  The use is in a different
// block than the def.
static void catch_cleanup_inter_block(Node *use, Block *use_blk, Node *def, Block *def_blk, Block_Array &bbs, int n_clone_idx) {
  if( !use_blk ) return;        // Can happen if the use is a precedence edge

  Node *new_def = catch_cleanup_find_cloned_def(use_blk, def, def_blk, bbs, n_clone_idx);
  catch_cleanup_fix_all_inputs(use, def, new_def);
}

//------------------------------call_catch_cleanup-----------------------------
// If we inserted any instructions between a Call and his CatchNode,
// clone the instructions on all paths below the Catch.
void Block::call_catch_cleanup(Block_Array &bbs) {

  // End of region to clone
  uint end = end_idx();
  if( !_nodes[end]->is_Catch() ) return;
  // Start of region to clone
  uint beg = end;
  while( _nodes[beg-1]->Opcode() != Op_MachProj || 
        !_nodes[beg-1]->in(0)->is_Call() ) {
    beg--;
    assert(beg > 0,"Catch cleanup walking beyond block boundary");
  }
  if( beg == end ) return;

  // Clone along all Catch output paths.  Clone area between the 'beg' and
  // 'end' indices.
  for( uint i = 0; i < _num_succs; i++ ) {
    Block *sb = _succs[i];
    // Clone the entire area; ignoring the edge fixup for now.
    for( uint j = end; j > beg; j-- ) {
      Node *clone = _nodes[j-1]->clone();
      sb->_nodes.insert( 1, clone );
      bbs.map(clone->_idx,sb);
    }
  }


  // Fixup edges.  Check the def-use info per cloned Node
  for(uint i2 = beg; i2 < end; i2++ ) {
    uint n_clone_idx = i2-beg+1; // Index of clone of n in each successor block
    Node *n = _nodes[i2];        // Node that got cloned
    // Need DU safe iterator because of edge manipulation in calls.
    Unique_Node_List *out = new Unique_Node_List(Thread::current()->resource_area());
    for (DUIterator_Fast j1max, j1 = n->fast_outs(j1max); j1 < j1max; j1++) {
      out->push(n->fast_out(j1));
    }
    uint max = out->size();
    for (uint j = 0; j < max; j++) {// For all users
      Node *use = out->pop();
      Block *buse = bbs[use->_idx];
      if( use->is_Phi() ) {
        for( uint k = 1; k < use->req(); k++ )
          if( use->in(k) == n ) {
            Node *fixup = catch_cleanup_find_cloned_def(bbs[buse->pred(k)->_idx], n, this, bbs, n_clone_idx);
            use->set_req(k, fixup);
          }
      } else {
        if (this == buse) {
          catch_cleanup_intra_block(use, n, this, beg, n_clone_idx);
        } else {
          catch_cleanup_inter_block(use, buse, n, this, bbs, n_clone_idx);
        }
      }
    } // End for all users

  } // End of for all Nodes in cloned area

  // Remove the now-dead cloned ops
  for(uint i3 = beg; i3 < end; i3++ ) {
    _nodes[beg]->disconnect_inputs(NULL);
    _nodes.remove(beg);
  }

}

