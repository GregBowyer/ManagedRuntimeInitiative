/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
 

#include "addnode.hpp"
#include "connode.hpp"
#include "divnode.hpp"
#include "loopnode.hpp"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "rootnode.hpp"

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

// To Do in the future:
//
//   1. Use loop header node to keep information about results of first invovcation of policy_prefetch,
// so we don't need to invoke it multiple times
//   2. More cleaning of policy_prefetch
//   3. Check if we should call maximally unroll first. before policy_prefetch
//   4. Test on loops with streams of different strides to see if prefetches are inserted correctly for 
// all streams


//------------------------------is_loop_exit-----------------------------------
// Given an IfNode, return the loop-exiting projection or NULL if both 
// arms remain in the loop.
Node *IdealLoopTree::is_loop_exit(Node *iff) const {
  if( iff->outcnt() != 2 ) return NULL; // Ignore partially dead tests
  PhaseIdealLoop *phase = _phase;
  // Test is an IfNode, has 2 projections.  If BOTH are in the loop
  // we need loop unswitching instead of peeling.
  if( !is_member(phase->get_loop( iff->raw_out(0) )) )
    return iff->raw_out(0);
  if( !is_member(phase->get_loop( iff->raw_out(1) )) )
    return iff->raw_out(1);
  return NULL;
}


//=============================================================================


//------------------------------record_for_igvn----------------------------
// Put loop body on igvn work list
void IdealLoopTree::record_for_igvn() {
  for( uint i = 0; i < _body.size(); i++ ) {
    Node *n = _body.at(i);
    _phase->_igvn._worklist.push(n);
  }
}

//------------------------------compute_profile_trip_cnt----------------------------
// Compute loop trip count from profile data as
//    (backedge_count + loop_exit_count) / loop_exit_count
void IdealLoopTree::compute_profile_trip_cnt( PhaseIdealLoop *phase ) {
  if( !UseC1 ) return;          // No profile data available
  if (!_head->is_CountedLoop()) {
    return;
  }
  CountedLoopNode* head = _head->as_CountedLoop();
  if (head->profile_trip_cnt() != COUNT_UNKNOWN) {
    return; // Already computed
  }
  float trip_cnt = (float)max_jint; // default is big

  Node* back = head->in(LoopNode::LoopBackControl);
  while (back != head) {
    if ((back->Opcode() == Op_IfTrue || back->Opcode() == Op_IfFalse) &&
        back->in(0) &&
back->in(0)->is_If()){
      break;
    }
    back = phase->idom(back);
  }
  if( !(back->in(0)->as_If()->_fcnt != COUNT_UNKNOWN &&
        back->in(0)->as_If()->_prob != PROB_UNKNOWN) )
    return;                     // Loop-exit test is not profiled?

  if (back != head) {
    assert((back->Opcode() == Op_IfTrue || back->Opcode() == Op_IfFalse) &&
           back->in(0), "if-projection exists");
    IfNode* back_if = back->in(0)->as_If();
    float loop_back_cnt = back_if->_fcnt * back_if->_prob;

    // Now compute a loop exit count
    float loop_exit_cnt = 0.0f;
    for( uint i = 0; i < _body.size(); i++ ) {
      Node *n = _body[i];
      if( n->is_If() ) {
        IfNode *iff = n->as_If();
        if( iff->_fcnt != COUNT_UNKNOWN && iff->_prob != PROB_UNKNOWN ) {
          Node *exit = is_loop_exit(iff);
          if( exit ) {
            float exit_prob = iff->_prob;
            if (exit->Opcode() == Op_IfFalse) exit_prob = 1.0 - exit_prob;
            if (exit_prob > PROB_MIN) {
              float exit_cnt = iff->_fcnt * exit_prob;
              loop_exit_cnt += exit_cnt;
            }
          }
        }
      }
    }
    if (loop_exit_cnt > 0.0f) {
      trip_cnt = (loop_back_cnt + loop_exit_cnt) / loop_exit_cnt;
    } else {
      // No exit count so use
      trip_cnt = loop_back_cnt;
    }
  }
#ifndef PRODUCT
  if (TraceProfileTripCount) {
C2OUT->print_cr("compute_profile_trip_cnt  lp: %d cnt: %f\n",head->_idx,trip_cnt);
  }
#endif
  head->set_profile_trip_cnt(trip_cnt);
}

//---------------------is_invariant_addition-----------------------------
// Return nonzero index of invariant operand for an Add or Sub
// of (nonconstant) invariant and variant values. Helper for reassoicate_invariants.
int IdealLoopTree::is_invariant_addition(Node* n, PhaseIdealLoop *phase) {
  int op = n->Opcode();
  if (op == Op_AddI || op == Op_SubI) {
    bool in1_invar = this->is_invariant(n->in(1));
    bool in2_invar = this->is_invariant(n->in(2));
    if (in1_invar && !in2_invar) return 1;
    if (!in1_invar && in2_invar) return 2;
  }
  return 0;
}

//---------------------reassociate_add_sub-----------------------------
// Reassociate invariant add and subtract expressions:
//
// inv1 + (x + inv2)  =>  ( inv1 + inv2) + x
// (x + inv2) + inv1  =>  ( inv1 + inv2) + x
// inv1 + (x - inv2)  =>  ( inv1 - inv2) + x
// inv1 - (inv2 - x)  =>  ( inv1 - inv2) + x
// (x + inv2) - inv1  =>  (-inv1 + inv2) + x
// (x - inv2) + inv1  =>  ( inv1 - inv2) + x
// (x - inv2) - inv1  =>  (-inv1 - inv2) + x
// inv1 + (inv2 - x)  =>  ( inv1 + inv2) - x
// inv1 - (x - inv2)  =>  ( inv1 + inv2) - x
// (inv2 - x) + inv1  =>  ( inv1 + inv2) - x
// (inv2 - x) - inv1  =>  (-inv1 + inv2) - x
// inv1 - (x + inv2)  =>  ( inv1 - inv2) - x
//
Node* IdealLoopTree::reassociate_add_sub(Node* n1, PhaseIdealLoop *phase) {
if((!n1->is_Add()&&!n1->is_Sub())||n1->outcnt()==0)return NULL;
  if (is_invariant(n1)) return NULL;
  int inv1_idx = is_invariant_addition(n1, phase);
  if (!inv1_idx) return NULL;
  // Don't mess with add of constant (igvn moves them to expression tree root.)
  if (n1->is_Add() && n1->in(2)->is_Con()) return NULL;
  Node* inv1 = n1->in(inv1_idx);
  Node* n2 = n1->in(3 - inv1_idx);
  int inv2_idx = is_invariant_addition(n2, phase);
  if (!inv2_idx) return NULL;
  Node* x    = n2->in(3 - inv2_idx);
  Node* inv2 = n2->in(inv2_idx);

  bool neg_x    = n2->is_Sub() && inv2_idx == 1;
  bool neg_inv2 = n2->is_Sub() && inv2_idx == 2;
  bool neg_inv1 = n1->is_Sub() && inv1_idx == 2;
  if (n1->is_Sub() && inv1_idx == 1) {
    neg_x    = !neg_x;
    neg_inv2 = !neg_inv2;
  }
  Node* inv1_c = phase->get_ctrl(inv1);
  Node* inv2_c = phase->get_ctrl(inv2);
  Node* n_inv1;
  if (neg_inv1) {
    Node *zero = phase->_igvn.intcon(0); 
    phase->set_ctrl(zero, phase->C->root());
    n_inv1 = new (phase->C, 3) SubINode(zero, inv1);
    phase->register_new_node(n_inv1, inv1_c);
  } else {
    n_inv1 = inv1;
  }
  Node* inv;
  if (neg_inv2) {
    inv = new (phase->C, 3) SubINode(n_inv1, inv2);
  } else {
    inv = new (phase->C, 3) AddINode(n_inv1, inv2);
  }
phase->register_new_node(inv,phase->get_early_ctrl(inv,true));

  Node* addx;
  if (neg_x) {
    addx = new (phase->C, 3) SubINode(inv, x);
  } else {
    addx = new (phase->C, 3) AddINode(x, inv);
  }
  phase->register_new_node(addx, phase->get_ctrl(x));
  phase->_igvn.hash_delete(n1);
  phase->_igvn.subsume_node(n1, addx);
  return addx;
}

//---------------------reassociate_invariants-----------------------------
// Reassociate invariant expressions:
void IdealLoopTree::reassociate_invariants(PhaseIdealLoop *phase) {
  for (int i = _body.size() - 1; i >= 0; i--) {
    Node *n = _body.at(i);
    for (int j = 0; j < 5; j++) {
      Node* nn = reassociate_add_sub(n, phase);
      if (nn == NULL) break;
      n = nn; // again
    };
  }
}

//------------------------------policy_peeling---------------------------------
// Return TRUE or FALSE if the loop should be peeled or not.  Peel if we can
// make some loop-invariant test (usually a null-check) happen before the loop.
bool IdealLoopTree::policy_peeling( PhaseIdealLoop *phase ) const {
  Node *test = ((IdealLoopTree*)this)->tail();
  int  body_size = ((IdealLoopTree*)this)->_body.size();
  int  uniq      = phase->C->unique();
  // Peeling does loop cloning which can result in O(N^2) node construction,
  // although this is very unusual.
  if( body_size * 4 + uniq > 60000 ) {
    return false;           // too large to safely clone
  }
  while( test != _head ) {      // Scan till run off top of loop
    if( test->is_If() ) {       // Test?
      Node *ctrl = phase->get_ctrl(test->in(1));
      if (ctrl->is_top())
        return false;           // Found dead test on live IF?  No peeling!
      // Standard IF only has one input value to check for loop invariance
      assert( test->Opcode() == Op_If || test->Opcode() == Op_CountedLoopEnd, "Check this code when new subtype is added");
      // Condition is not a member of this loop?
      if( !is_member(phase->get_loop(ctrl)) &&
          is_loop_exit(test) )
        return true;            // Found reason to peel!
    }
    // Walk up dominators to loop _head looking for test which is
    // executed on every path thru loop.
    test = phase->idom(test);
  }

  // Check for a loop-head Phi with loop-invariants on BOTH inputs.
  // Peeling will make this value loop-invariant (remove a Phi).
for(DUIterator_Fast imax,i=_head->fast_outs(imax);i<imax;i++){
Node*phi=_head->fast_out(i);
    if( phi->is_Phi() && 
        phase->get_loop(phase->get_ctrl(phi->in(LoopNode::LoopBackControl))) != this ) 
      return true;
  }

  return false;
}

//------------------------------peeled_dom_test_elim---------------------------
// If we got the effect of peeling, either by actually peeling or by making
// a pre-loop which must execute at least once, we can remove all 
// loop-invariant dominated tests in the main body.
void PhaseIdealLoop::peeled_dom_test_elim( IdealLoopTree *loop, Node_List &old_new ) {
  bool progress = true;
  while( progress ) {
    progress = false;           // Reset for next iteration
    Node *prev = loop->_head->in(LoopNode::LoopBackControl);//loop->tail();
    Node *test = prev->in(0);
    while( test != loop->_head ) { // Scan till run off top of loop
      
      int p_op = prev->Opcode();
      if( (p_op == Op_IfFalse || p_op == Op_IfTrue) &&
          test->is_If() &&      // Test?
          !test->in(1)->is_Con() && // And not already obvious?
          // Condition is not a member of this loop?
          !loop->is_member(get_loop(get_ctrl(test->in(1))))){
        // Walk loop body looking for instances of this test
        for( uint i = 0; i < loop->_body.size(); i++ ) {
          Node *n = loop->_body.at(i);
          if( n->is_If() && n->in(1) == test->in(1) /*&& n != loop->tail()->in(0)*/ ) {
            // IfNode was dominated by version in peeled loop body
            progress = true;
            dominated_by( old_new[prev->_idx], n );
          }
        }
      }
      prev = test;
      test = idom(test);
    } // End of scan tests in loop

  } // End of while( progress )
}

//------------------------------do_peeling-------------------------------------
// Peel the first iteration of the given loop.  
// Step 1: Clone the loop body.  The clone becomes the peeled iteration.
//         The pre-loop illegally has 2 control users (old & new loops).
// Step 2: Make the old-loop fall-in edges point to the peeled iteration.
//         Do this by making the old-loop fall-in edges act as if they came
//         around the loopback from the prior iteration (follow the old-loop
//         backedges) and then map to the new peeled iteration.  This leaves
//         the pre-loop with only 1 user (the new peeled iteration), but the
//         peeled-loop backedge has 2 users.
// Step 3: Cut the backedge on the clone (so its not a loop) and remove the
//         extra backedge user.
void PhaseIdealLoop::do_peeling( IdealLoopTree *loop, Node_List &old_new ) {
  if( PrintOpto ) {
C2OUT->print("Peeling");
    loop->dump_head();
  }
  C->set_major_progress();
  // Peeling a 'main' loop in a pre/main/post situation obfuscates the
  // 'pre' loop from the main and the 'pre' can no longer have it's
  // iterations adjusted.  Therefore, we need to declare this loop as
  // no longer a 'main' loop; it will need new pre and post loops before
  // we can do further RCE.
  Node *h = loop->_head;
  if( h->is_CountedLoop() ) {
    CountedLoopNode *cl = h->as_CountedLoop();
    assert(cl->trip_count() > 0, "peeling a fully unrolled loop");
    cl->set_trip_count(cl->trip_count() - 1);
    if( cl->is_main_loop() ) {
      cl->set_normal_loop();
if(PrintOpto){
C2OUT->print("Peeling a 'main' loop; resetting to 'normal' ");
        loop->dump_head();
      }
    }
  }

  // Step 1: Clone the loop body.  The clone becomes the peeled iteration.
  //         The pre-loop illegally has 2 control users (old & new loops).
clone_loop(loop,old_new,dom_depth(loop->_head),NULL,false);


  // Step 2: Make the old-loop fall-in edges point to the peeled iteration.
  //         Do this by making the old-loop fall-in edges act as if they came
  //         around the loopback from the prior iteration (follow the old-loop
  //         backedges) and then map to the new peeled iteration.  This leaves
  //         the pre-loop with only 1 user (the new peeled iteration), but the
  //         peeled-loop backedge has 2 users.
  for (DUIterator_Fast jmax, j = loop->_head->fast_outs(jmax); j < jmax; j++) {
    Node* old = loop->_head->fast_out(j);
    if( old->in(0) == loop->_head && old->req() == 3 &&
        (old->is_Loop() || old->is_Phi()) ) {
      Node *new_exit_value = old_new[old->in(LoopNode::LoopBackControl)->_idx];
      if( !new_exit_value )     // Backedge value is ALSO loop invariant?
        // The loop body backedge value remains the same.
        new_exit_value = old->in(LoopNode::LoopBackControl);
      _igvn.hash_delete(old);
      old->set_req(LoopNode::EntryControl, new_exit_value);
_igvn._worklist.push(old);//Recheck
    }
  }
  

  // Step 3: Cut the backedge on the clone (so its not a loop) and remove the
  //         extra backedge user.
  Node *nnn = old_new[loop->_head->_idx];
  _igvn.hash_delete(nnn);
  nnn->set_req(LoopNode::LoopBackControl, C->top());
  for (DUIterator_Fast j2max, j2 = nnn->fast_outs(j2max); j2 < j2max; j2++) {
    Node* use = nnn->fast_out(j2);
    if( use->in(0) == nnn && use->req() == 3 && use->is_Phi() ) {
      _igvn.hash_delete(use);
      use->set_req(LoopNode::LoopBackControl, C->top());
    }
  }


  // Step 4: Correct dom-depth info.  Set to loop-head depth.
  int dd = dom_depth(loop->_head);
  set_idom(loop->_head, loop->_head->in(1), dd);
  for (uint j3 = 0; j3 < loop->_body.size(); j3++) {
    Node *old = loop->_body.at(j3);
    Node *nnn = old_new[old->_idx];
    if (!has_ctrl(nnn))
      set_idom(nnn, idom(nnn), dd-1);
    // While we're at it, remove any SafePoints from the peeled code
    if( old->Opcode() == Op_SafePoint ) {
      Node *nnn = old_new[old->_idx];
      lazy_replace(nnn,nnn->in(TypeFunc::Control));
    }
    // The peeled body has unknown counts.
if(old->is_If()){
      // Main loop trips less often (really should subtract off the peeled
      // loop's execution count, i.e., the loop-entry frequency, but this
      // isn't easily known).
IfNode*iff=old->as_If();
      if( iff->_fcnt > 1.0 )
        iff->_fcnt -= 1.0; // Assume loop entered once, so main loop trips once less often
      nnn->as_If()->_fcnt = COUNT_UNKNOWN; // Peeled loop has unknown counts
    }
  }

  // Now force out all loop-invariant dominating tests.  The optimizer 
  // finds some, but we _know_ they are all useless.
  peeled_dom_test_elim(loop,old_new);

  // Check for redundant induction variables and remove.  That is, if 'I' is a
  // major induction variable (cycle of phi and simple-op), and 'J' is a phi
  // that refers to I on it's backedge or some simple-op on I - replace J
  // with direct math on I.  Changes code like:
  //     loop:
  //       ...stuff using I and J
  //       J = I op X;  // J gets old value of I
  //       I = I+1;     // make a new value of I
  //       goto loop;
  // with code like this:
  //     loop:
  //       J = (I-1) op X;
  //       ...stuff using I and J
  //       I = I+1;
  //       goto loop;
  // Thus removing a loop-carried dependence.
  for (DUIterator_Fast jmax, j = loop->_head->fast_outs(jmax); j < jmax; j++) {
Node*J=loop->_head->fast_out(j);
    if( J->in(0) != loop->_head || !J->is_Phi() ) continue;
Node*back=J->in(LoopNode::LoopBackControl);
    // Allow an extra op between J and I
Node*extra=NULL;
    if( back->req() == 3 && !back->is_Phi() && 
        // But extra op must hoist to loop start
        get_ctrl(back) == loop->_head ) {
      extra = back;
      back = back->in(1);
    }
    // See if 'back' is really a major induction variable
    if( back->in(0) != loop->_head || !back->is_Phi() ) continue;
    if( back == J ) continue;   // J is itself a major induction variable
Node*incr=back->in(LoopNode::LoopBackControl);
    if( incr->Opcode() != Op_AddI || is_member(loop,get_ctrl(incr->in(2))) || incr->in(1) != back ) 
      continue;
    // Here I have found a Phi merging some fall-in value and an induction
    // variable.  Since we peeled, the fall-in value is the IV's prior value
    // and the backedge is also the IV's prior value.
Node*prior=new(C,3)SubINode(incr->in(1),incr->in(2));
    register_new_node( prior, loop->_head );
    if( extra ) {
      extra = extra->clone();
      extra->set_req(1,prior);
      register_new_node( extra, loop->_head );
    }
_igvn.hash_delete(J);//Replace dependent induction variable with
    _igvn.subsume_node(J,prior); // Direct use of major induction variable
    --j; --jmax;                // Fixup the DUIterator after delete
  }

  loop->record_for_igvn();
}

//------------------------------policy_maximally_unroll------------------------
// Return exact loop trip count, or 0 if not maximally unrolling
bool IdealLoopTree::policy_maximally_unroll( PhaseIdealLoop *phase ) const {
  CountedLoopNode *cl = _head->as_CountedLoop();

  Node *init_n = cl->init_trip();
  Node *limit_n = cl->limit();

  // Non-constant bounds
  if( init_n   == NULL || !init_n->is_Con()  ||
      limit_n  == NULL || !limit_n->is_Con() ||
      // protect against stride not being a constant
      !cl->stride_is_con() ) {
    return false;
  }
  int init   = init_n->get_int();
  int limit  = limit_n->get_int();
  int span   = limit - init;
  int stride = cl->stride_con();

  if (init >= limit || stride > span) {
    // return a false (no maximally unroll) and the regular unroll/peel
    // route will make a small mess which CCP will fold away.
    return false;
  }
  uint trip_count = span/stride;   // trip_count can be greater than 2 Gig.
  assert( (int)trip_count*stride == span, "must divide evenly" );

  // Real policy: if we maximally unroll, does it get too big?

  // Allow the unrolled mess to get larger than standard loop size.  After
  // all, it will no longer be a loop.  Also, unrolling (and the
  // LoopUnrollLimit) normally happens on loops with range checks removed but
  // that has not happened yet here - but after unrolling the range checks
  // will be 'widened' to cover all the unrolled iterations (similar to what
  // happens with iteration splitting).  So the body size starts out larger
  // than the equivalent RCE'd loop but most of the range checks will fold up
  // in the end.
  uint body_size    = _body.size();
uint unroll_limit=(uint)LoopUnrollLimit*6;
assert((intx)unroll_limit==LoopUnrollLimit*6,"LoopUnrollLimit must fit in 32bits");
  cl->set_trip_count(trip_count);
  if( trip_count <= unroll_limit && body_size <= unroll_limit ) {
    uint new_body_size = body_size * trip_count;
    if (new_body_size <= unroll_limit &&
        body_size == new_body_size / trip_count &&
        // Unrolling can result in a large amount of node construction
        new_body_size < MaxNodeLimit - phase->C->unique()) {
      return true;    // maximally unroll
    }
  }

  return false;               // Do not maximally unroll
}


//------------------------------policy_unroll----------------------------------
// Return TRUE or FALSE if the loop should be unrolled or not.  Unroll if 
// the loop is a CountedLoop and the body is small enough.
bool IdealLoopTree::policy_do_aggressive_loopopts(PhaseIdealLoop*phase)const{

  CountedLoopNode *cl = _head->as_CountedLoop();
  assert( cl->is_normal_loop() || cl->is_main_loop(), "" );
if(!cl->stride())return false;

  // protect against stride not being a constant
  if( !cl->stride_is_con() ) return false;

  // protect against over-unrolling
  if( cl->trip_count() <= 1 ) return false;

  int future_unroll_ct = cl->unrolled_count() * 2;
 
CountedLoopEndNode*cle=cl->loopexit();
  // Check if loop is hot
  if( cle->_fcnt != COUNT_UNKNOWN && cle->_fcnt < 10.0 ) {
    return false;
  }

  return true;
}

// Based on profile data, should we allow this loop to be eventually
// unrolled by a factor of N?  Useful test for cache-line prefetching
// - which should either unroll alot (to fill out a cache-line's worth
// of data) or not at all.
bool IdealLoopTree::policy_unroll_by_N( PhaseIdealLoop *phase, int N ) const {
  CountedLoopNode *cl = _head->as_CountedLoop();
  // Don't unroll if these rounds of unrolling would push us
  // over the expected trip count of the loop.  One is subtracted
  // from the expected trip count because the pre-loop normally
  // executes 1 iteration.
  int future_unroll_ct = cl->unrolled_count() * N;
 
  if (UnrollLimitForProfileCheck > 0 &&
      cl->profile_trip_cnt() != COUNT_UNKNOWN &&
      future_unroll_ct        > UnrollLimitForProfileCheck &&
      (float)future_unroll_ct > cl->profile_trip_cnt() - 1.0) {
    return false;
  }
  
  // When unroll count is greater than LoopUnrollMin, don't unroll if:
  //   the residual iterations are more than 10% of the trip count
  //   and rounds of "unroll,optimize" are not making significant progress
  //   Progress defined as current size less than 20% larger than previous size.
  if (UseSuperWord && cl->node_count_before_unroll() > 0 &&
      future_unroll_ct > LoopUnrollMin &&
      (future_unroll_ct - 1) * 10.0 > cl->profile_trip_cnt() &&
      1.2 * cl->node_count_before_unroll() < (double)_body.size()) {
    return false;
  }

  Node *init_n = cl->init_trip();
  Node *limit_n = cl->limit();
  // Non-constant bounds.
  // Protect against over-unrolling when init or/and limit are not constant
  // (so that trip_count's init value is maxint) but iv range is known.
  if( init_n   == NULL || !init_n->is_Con()  ||
      limit_n  == NULL || !limit_n->is_Con() ) {
    Node* phi = cl->phi();
    if( phi != NULL ) {
      assert(phi->is_Phi() && phi->in(0) == _head, "Counted loop should have iv phi.");
      const TypeInt* iv_type = phase->_igvn.type(phi)->is_int();
      int next_stride = cl->stride_con() * N; // stride after all unrolls
      if( next_stride > 0 ) {
        if( iv_type->_lo + next_stride <= iv_type->_lo || // overflow
            iv_type->_lo + next_stride >  iv_type->_hi ) {
          return false;  // over-unrolling
        }
      } else if( next_stride < 0 ) {
        if( iv_type->_hi + next_stride >= iv_type->_hi || // overflow
            iv_type->_hi + next_stride <  iv_type->_lo ) {
          return false;  // over-unrolling
        }
      }
    }
  }
  return true;
}

bool IdealLoopTree::policy_unroll_small_loops(PhaseIdealLoop*phase)const{
  CountedLoopNode *cl = _head->as_CountedLoop();
  assert( cl->is_normal_loop() || cl->is_main_loop(), "" );
CountedLoopEndNode*cle=cl->loopexit();

  if ( cle->_prob < 0.75 ) {      // Already tripping very few times?
    return false;
  }

  // Does profiling suggestion it is OK to unroll by 2?
  if( !policy_unroll_by_N( phase, 2 ) ) return false;

  // Adjust body_size to determine if we unroll or not
  uint body_size = _body.size();
  // Key test to unroll CaffeineMark's Logic test
  int xors_in_loop = 0;
  int locks_in_loop = 0;
  // Also count ModL, DivL and MulL which expand mightly
  for( uint k = 0; k < _body.size(); k++ ) {
    switch( _body.at(k)->Opcode() ) {
    case Op_Lock: locks_in_loop++; break;
    case Op_XorI: xors_in_loop++; break; // CaffeineMark's Logic test
    case Op_ModL: body_size += 30; break;
    case Op_DivL: body_size += 30; break;
    case Op_MulL: body_size += 10; break;
    }
  }

  // Check for being too big
  if( body_size > (uint)LoopUnrollLimit ) { 
if((xors_in_loop>=4||locks_in_loop>0)&&body_size<(uint)LoopUnrollLimit*4)return true;
    // Normal case: loop too big
    return false;
  }
  
  // Check for stride being a small enough constant
  if( abs(cl->stride_con()) > (1<<3) ) return false;

  // Unroll once!  (Each trip will soon do double iterations)
  return true;
}

//------------------------------policy_align-----------------------------------
// Return TRUE or FALSE if the loop should be cache-line aligned.  Gather the
// expression that does the alignment.  Note that only one array base can be
// aligned in a loop (unless the VM guarentees mutual alignment).  Note that
// if we vectorize short memory ops into longer memory ops, we may want to
// increase alignment.
bool IdealLoopTree::policy_align( PhaseIdealLoop *phase ) const {
  return false;
}

//------------------------------policy_unroll_and_prefetch------------------------
// Return 0 if no prefetch insertion and no unrolling is needed
// Return 1 if prefetch is needed and no unrolling is needed
// Return 2 if no prefetch insertion but unrolling is needed
int IdealLoopTree::policy_unroll_and_prefetch( PhaseIdealLoop *phase) const {
  if( !DoPrefetch ) return 0;   // As-if no streams needing prefetch

  // Find array bases, offsets, and strides:
  //   A + I*x + y
  // We're looking for all (A,x) pairs and keeping the max y, assuming y is
  // smaller than a fraction of L1 (or maybe L2?) cache.  We then compute the
  // number of loop bodies 'z' needed to cover the cache-miss latency and
  // issue prefetches that far ahead:
  //   prefetch A + I*(x+z) + max_y

  static const int max_streams = 20;
  int streams = 0;              // Number of prefetch streams
  Node *array [max_streams];    // Array base for stream
  Node *scaled[max_streams];    // Scaled index for stream
  Node *memop [max_streams];    // Some memop from the stream
  int stride  [max_streams];    // Stride for stream
  int maxoff  [max_streams];    // Max offset for stream
  bool write  [max_streams];    // True if this stream includes writes
  bool has_non_char_op = false; // True if we see any non-character-array refs
Node*zero=phase->_igvn.longcon(0);
  CountedLoopNode *head = _head->as_CountedLoop();
  Node *I = head->phi();        // The basic induction variable
  int iv_stride = head->stride_is_con() ? head->stride_con() : 1;

  int loop_cycles = 0;
  for( uint i = 0; i < _body.size(); i++ ) {
    Node *n = _body[i];
    int n_cycles = 1;           // Assume 1 clk
    switch( n->Opcode() ) {
    case Op_AddD:   n_cycles =  8; break;
    case Op_AddF:   n_cycles =  8; break;
    case Op_AddI:   n_cycles =  1; break;
    case Op_AddP:   n_cycles =  0; break; // Probably folds into addressing math
    case Op_CmpI:   n_cycles =  1; break;
    case Op_CmpU:   n_cycles =  1; break;
    case Op_ConvI2L:n_cycles =  0; break; // Probably folds into addressing math
    case Op_CountedLoopEnd: n_cycles = 1; break; // Easily predicted
    case Op_DivD:   n_cycles = 59; break;
    case Op_DivF:   n_cycles = 29; break;
    case Op_DivI:   n_cycles = 38; break;
    case Op_If:     n_cycles =  2; break; // Not so easy to predict
    case Op_IfFalse:n_cycles =  0; break;
    case Op_IfTrue: n_cycles =  0; break;
    case Op_LShiftL:n_cycles =  1; break;
    case Op_MulD:   n_cycles = 13; break;
    case Op_MulF:   n_cycles =  9; break;
    case Op_MulI:   n_cycles = 10; break;
    case Op_MulL:   n_cycles = 14; break;
    case Op_Phi:    n_cycles =  0; break;
    case Op_SubD:   n_cycles =  8; break;
    case Op_SubF:   n_cycles =  8; break;
    case Op_PrefetchRead :
    case Op_PrefetchWrite: return 0; // Do not insert prefetches if user is already doing so
    }
    loop_cycles += n_cycles;
    if( !n->is_Mem() ) continue;
    MemNode *mem = n->as_Mem();
if(mem->is_Load())
      loop_cycles += 3-n_cycles; // load-use penalty
    if( mem->Opcode() != Op_LoadC &&
        mem->Opcode() != Op_StoreC )
      has_non_char_op = true;
    // Break down the address
    Node *adr = mem->in(MemNode::Address);
    // See if the address is loop-varying.  Loop-invariant addresses happen
    // typically if there is aliasing.  In any case, the same address is
    // accessed on each loop iteration so should hit in cache repeatedly.
    if( this != phase->get_loop(phase->get_ctrl(adr)) )
      continue;                 // No need to prefetch
    if( !adr->is_AddP() )       // Not an AddP?
      continue; 
    // Recognize:
    //   A+((long)(I+inv))       { +inv }
    //   A+((long)(I+inv))* inv  { +inv }
    //   A+((long)(I+inv))<<inv  { +inv }
Node*scale=NULL;
Node*ptr=adr->in(AddPNode::Address);
Node*noff=adr->in(AddPNode::Offset);
    intptr_t off = 0;
    if( this == phase->get_loop(phase->get_ctrl(noff)) ) {
      // 'off' varies, hence it is not "+inv".  
      scale = noff;           // Assume varying offset is really the scaled IV
      noff = zero;              // Assume a zero offset and try again
off=0;//Assume a zero offset and try again
    } else {
      // 'off' is invariant; it is an added offset
      adr = ptr;                // Now breakdown 'ptr' further
      if( !adr->is_AddP() ) {   // Not an AddP?
        // This probably represents a case where I can discover and prefetch a
        // stream with a little more work.  If this is a LoadRange, then I'm 
        // likely looking at the innards of a 2-d array access striding 
        // cross-ways - an ideal prefetch candidate.
        continue; 
      }
ptr=adr->in(AddPNode::Address);
scale=adr->in(AddPNode::Offset);
      noff->get_intptr_t(&off);
    }
    if( ptr->Opcode() == Op_CastPP ) // Prefetch on guarded mem ops
      ptr = ptr->in(1);

    if( this == phase->get_loop(phase->get_ctrl(ptr)) ) {
      // This probably represents a case where I can discover and prefetch a
      // stream with a little more work.  If this is a LoadP, then I'm likely
      // looking at the innards of a 2-d array access striding cross-ways - an
      // ideal prefetch candidate.  
      //Unimplemented(); // What to do with a loop-varying base?
      continue;
    }
    // Having removed an optional invariant offset, look for the scaling.
    Node *convi2l = scale;      // The possible ConvI2L here
    int mul;                    // Stride for this mem-op
    if( scale->Opcode() == Op_MulL ||
        scale->Opcode() == Op_LShiftL ) {
      intptr_t con;
      if( !scale->in(2)->get_intptr_t(&con) ) {
        // A non-constant scale.  This probably represents a case where I can
        // discover and prefetch a stream with a little more work.
        continue;
      }
      convi2l = scale->in(1);   // The possible ConvI2L
      mul = (scale->Opcode() == Op_MulL) ? con : (1<<con);
    } else {
      mul = 1;                  // Stride is 1, scale-point is 'scale'
    }
    // Discover a ConvI2L
    if( convi2l->Opcode() != Op_ConvI2L ) {
      continue;                 // Really expect to see a ConvI2L here
    }
Node*iv=convi2l->in(1);
    // One more round of loop-invariant AddI's
    if( iv->Opcode() == Op_AddI ) {
      if( this == phase->get_loop(phase->get_ctrl(iv->in(2))) ) 
        continue;               // Another loop-varying offset?
      iv = iv->in(1);           // Get to the IV through the add-of-loop-invariant
    }
    if( iv != I ) continue;     // Missed again

    // Found a prefetchable stream.  See if we've seen it already.
    int j;
    for( j = 0; j<streams; j++ ) { // Scan all existing streams
      if( array [j] == ptr &&   // Same base, same stride?
          stride[j] == mul ) {
        if( mem->is_Store() ) write[j] = true; // Note read-only streams
        // Get the largest offset (smallest if striding backwards)
        if( (iv_stride < 0) ^ (off > maxoff[j]) ) 
          maxoff[j] = off;
        break;                  // Already seen the stream, do not add it
      }
    }
    // Add a new stream
    if( j == streams && streams < max_streams) {
      array [streams] = ptr;
      stride[streams] = mul;
      maxoff[streams] = off;
      scaled[streams] = adr;    // The scale point
      memop [streams] = mem;    // The some memory op to hook the prefetch
      write [streams] = mem->is_Store();
      streams++;
    }

  }
  if( streams == 0 ) return 0; // Nothing to prefetch!
  // Assume character-only streams have short trip count and are already in cache
  if( has_non_char_op == false )
    return 0;               // Do not prefetch character-only streams
  
  if( PrintOpto ) C2OUT->print_cr("--- policy_unroll_and_prefetch: loop cycles= %d, IV striding by %d",loop_cycles,iv_stride);

for(int j=0;j<streams;j++){
    intptr_t bytes_per_iter = iv_stride * stride[j];
    // Cutout if we use so little memory per iteration, that we would end up
    // prefetching the same cache line repeatedly.  A better answer here would
    // be to unroll a bit more.
    bool too_little_work = (iv_stride < 0)
      ? (bytes_per_iter > -BytesPerCacheLine)
      : (bytes_per_iter <  BytesPerCacheLine);
    double bytes_per_mem = (double)bytes_per_iter * ((double)CacheMissLatency / (double)loop_cycles);
    int bytes_ahead = (int)bytes_per_mem + maxoff[j];
    int xtra = iv_stride > 0 ? BytesPerCacheLine : -BytesPerCacheLine; // one extra line ahead to be safe
    bytes_ahead = (bytes_ahead + xtra + BytesPerCacheLine-1) & ~(BytesPerCacheLine-1);
    if( PrintOpto ) {
      char buf[200];
char*s=buf;
      const char *name = array[j]->Name();
      s += sprintf(s,"---  %d_%s + I*%d + %d ",array[j]->_idx, name, stride[j], maxoff[j]);
      if( too_little_work ) s += sprintf(s,", not prefetched; too little work per iteration; needs more unrolling");
      C2OUT->print_cr(buf);
    }
  }
  int min_stride = stride[0];
for(int j=1;j<streams;j++)
    if (stride[j] < min_stride) min_stride = stride[j];

  if (loop_cycles > (CacheMissLatency * 2) ) return 1; // Prefetch now, no unrolling
  int unroll_needed = BytesPerCacheLine / abs(min_stride * iv_stride);
  int res = MIN( unroll_needed, 2 );
  if( res == 2 && _body.size() > (uint)LoopUnrollLimit*unroll_needed ) // must unroll before prefetch is profitable?
    return 0;                  // Cutout if LoopUnrollLimit is set too low
  if( res == 2 && !policy_unroll_by_N( phase, unroll_needed ) )
    return 0;                   // Profiling suggests to not bother
  return res;
}

bool IdealLoopTree::do_prefetch(PhaseIdealLoop*phase){
  // Find array bases, offsets, and strides:
  //   A + I*x + y
  // We're looking for all (A,x) pairs and keeping the max y, assuming y is
  // smaller than a fraction of L1 (or maybe L2?) cache.  We then compute the
  // number of loop bodies 'z' needed to cover the cache-miss latency and
  // issue prefetches that far ahead:
  //   prefetch A + I*(x+z) + max_y

  static const int max_streams = 20;
  int streams = 0;              // Number of prefetch streams
  Node *array [max_streams];    // Array base for stream
  Node *scaled[max_streams];    // Scaled index for stream
  Node *memop [max_streams];    // Some memop from the stream
  int stride  [max_streams];    // Stride for stream
  int maxoff  [max_streams];    // Max offset for stream
  bool write  [max_streams];    // True if this stream includes writes
  bool has_non_char_op = false; // True if we see any non-character-array refs
Node*zero=phase->_igvn.longcon(0);
  CountedLoopNode *head = _head->as_CountedLoop();
  Node *I = head->phi();        // The basic induction variable
  int iv_stride = head->stride_is_con() ? head->stride_con() : 1;

  int loop_cycles = 0;
  for( uint i = 0; i < _body.size(); i++ ) {
    Node *n = _body[i];
    int n_cycles = 1;           // Assume 1 clk
    switch( n->Opcode() ) {
    case Op_AddD:   n_cycles =  8; break;
    case Op_AddF:   n_cycles =  8; break;
    case Op_AddI:   n_cycles =  1; break;
    case Op_AddP:   n_cycles =  0; break; // Probably folds into addressing math
    case Op_CmpI:   n_cycles =  1; break;
    case Op_CmpU:   n_cycles =  1; break;
    case Op_ConvI2L:n_cycles =  0; break; // Probably folds into addressing math
    case Op_CountedLoopEnd: n_cycles = 1; break; // Easily predicted
    case Op_DivD:   n_cycles = 59; break;
    case Op_DivF:   n_cycles = 29; break;
    case Op_DivI:   n_cycles = 38; break;
    case Op_If:     n_cycles =  2; break; // Not so easy to predict
    case Op_IfFalse:n_cycles =  0; break;
    case Op_IfTrue: n_cycles =  0; break;
    case Op_LShiftL:n_cycles =  1; break;
    case Op_MulD:   n_cycles = 13; break;
    case Op_MulF:   n_cycles =  9; break;
    case Op_MulI:   n_cycles = 10; break;
    case Op_MulL:   n_cycles = 14; break;
    case Op_Phi:    n_cycles =  0; break;
    case Op_SubD:   n_cycles =  8; break;
    case Op_SubF:   n_cycles =  8; break;
    case Op_PrefetchRead :
    case Op_PrefetchWrite: return false; // Do not insert prefetches if user is already doing so
    }
    loop_cycles += n_cycles;
    if( !n->is_Mem() ) continue;
    MemNode *mem = n->as_Mem();
if(mem->is_Load())
      loop_cycles += 3-n_cycles; // load-use penalty
    if( mem->Opcode() != Op_LoadC &&
        mem->Opcode() != Op_StoreC )
      has_non_char_op = true;
    // Break down the address
    Node *adr = mem->in(MemNode::Address);
    // See if the address is loop-varying.  Loop-invariant addresses happen
    // typically if there is aliasing.  In any case, the same address is
    // accessed on each loop iteration so should hit in cache repeatedly.
    if( this != phase->get_loop(phase->get_ctrl(adr)) )
      continue;                 // No need to prefetch
    if( !adr->is_AddP() )       // Not an AddP?
      continue; 
    // Recognize:
    //   A+((long)(I+inv))       { +inv }
    //   A+((long)(I+inv))* inv  { +inv }
    //   A+((long)(I+inv))<<inv  { +inv }
Node*scale=NULL;
Node*ptr=adr->in(AddPNode::Address);
Node*noff=adr->in(AddPNode::Offset);
    intptr_t off = 0;
    if( this == phase->get_loop(phase->get_ctrl(noff)) ) {
      // 'off' varies, hence it is not "+inv".  
      scale = noff;           // Assume varying offset is really the scaled IV
      noff = zero;              // Assume a zero offset and try again
off=0;//Assume a zero offset and try again
    } else {
      // 'off' is invariant; it is an added offset
      adr = ptr;                // Now breakdown 'ptr' further
      if( !adr->is_AddP() ) {   // Not an AddP?
        // This probably represents a case where I can discover and prefetch a
        // stream with a little more work.  If this is a LoadRange, then I'm 
        // likely looking at the innards of a 2-d array access striding 
        // cross-ways - an ideal prefetch candidate.
        continue; 
      }
ptr=adr->in(AddPNode::Address);
scale=adr->in(AddPNode::Offset);
      noff->get_intptr_t(&off);
    }
    if( ptr->Opcode() == Op_CastPP ) // Prefetch on guarded mem ops
      ptr = ptr->in(1);

    if( this == phase->get_loop(phase->get_ctrl(ptr)) ) {
      // This probably represents a case where I can discover and prefetch a
      // stream with a little more work.  If this is a LoadP, then I'm likely
      // looking at the innards of a 2-d array access striding cross-ways - an
      // ideal prefetch candidate.  
      //Unimplemented(); // What to do with a loop-varying base?
      continue;
    }
    // Having removed an optional invariant offset, look for the scaling.
    Node *convi2l = scale;      // The possible ConvI2L here
    int mul;                    // Stride for this mem-op
    if( scale->Opcode() == Op_MulL ||
        scale->Opcode() == Op_LShiftL ) {
      intptr_t con;
      if( !scale->in(2)->get_intptr_t(&con) ) {
        // A non-constant scale.  This probably represents a case where I can
        // discover and prefetch a stream with a little more work.
        continue;
      }
      convi2l = scale->in(1);   // The possible ConvI2L
      mul = (scale->Opcode() == Op_MulL) ? con : (1<<con);
    } else {
      mul = 1;                  // Stride is 1, scale-point is 'scale'
    }
    // Discover a ConvI2L
    if( convi2l->Opcode() != Op_ConvI2L ) {
      continue;                 // Really expect to see a ConvI2L here
    }
Node*iv=convi2l->in(1);
    // One more round of loop-invariant AddI's
    if( iv->Opcode() == Op_AddI ) {
      if( this == phase->get_loop(phase->get_ctrl(iv->in(2))) ) 
        continue;               // Another loop-varying offset?
      iv = iv->in(1);           // Get to the IV through the add-of-loop-invariant
    }
    if( iv != I ) continue;     // Missed again

    // Found a prefetchable stream.  See if we've seen it already.
    int j;
    for( j = 0; j<streams; j++ ) { // Scan all existing streams
      if( array [j] == ptr &&   // Same base, same stride?
          stride[j] == mul ) {
        if( mem->is_Store() ) write[j] = true; // Note read-only streams
        // Get the largest offset (smallest if striding backwards)
        if( (iv_stride < 0) ^ (off > maxoff[j]) ) 
          maxoff[j] = off;
        break;                  // Already seen the stream, do not add it
      }
    }
    // Add a new stream
    if( j == streams && streams < max_streams) {
      array [streams] = ptr;
      stride[streams] = mul;
      maxoff[streams] = off;
      scaled[streams] = adr;    // The scale point
      memop [streams] = mem;    // The some memory op to hook the prefetch
      write [streams] = mem->is_Store();
      streams++;
    }

  }
  if( streams == 0 ) return false; // Nothing to prefetch!
  // Assume character-only streams have short trip count and are already in cache
  if( has_non_char_op == false )
    return false;               // Do not prefetch character-only streams
  
  if( PrintOpto ) C2OUT->print_cr("--- Policy prefetch: loop cycles= %d, IV striding by %d",loop_cycles,iv_stride);

for(int j=0;j<streams;j++){
    intptr_t bytes_per_iter = iv_stride * stride[j];
    // Cutout if we use so little memory per iteration, that we would end up
    // prefetching the same cache line repeatedly.  A better answer here would
    // be to unroll a bit more.
    bool too_little_work = (iv_stride < 0)
      ? (bytes_per_iter > -BytesPerCacheLine)
      : (bytes_per_iter <  BytesPerCacheLine);
    double bytes_per_mem = (double)bytes_per_iter * ((double)CacheMissLatency / (double)loop_cycles);
    int bytes_ahead = (int)bytes_per_mem + maxoff[j];
    int xtra = iv_stride > 0 ? BytesPerCacheLine : -BytesPerCacheLine; // one extra line ahead to be safe
    bytes_ahead = (bytes_ahead + xtra + BytesPerCacheLine-1) & ~(BytesPerCacheLine-1);
    if( PrintOpto ) {
      char buf[200];
char*s=buf;
      const char *name = array[j]->Name();
      s += sprintf(s,"---  %d_%s + I*%d + %d ",array[j]->_idx, name, stride[j], maxoff[j]);
      s += sprintf(s,"bytes_per_iter=%ld ",bytes_per_iter);
      intptr_t iters_per_miss = (CacheMissLatency + loop_cycles) / loop_cycles;
      s += sprintf(s,"iters_per_miss=%ld ",iters_per_miss);
      s += sprintf(s,"bytes_per_mem =%4.2f ",bytes_per_mem);
      s += sprintf(s,"bytes_ahead =%d ",bytes_ahead);
      s += sprintf(s,write[j]?"write ":"read_only ");
      if( too_little_work ) s += sprintf(s,", not prefetched; too little work per iteration; needs more unrolling");
      C2OUT->print_cr(buf);
    }
    // if( too_little_work ) continue;
    // avoid unrolling too many times for large loops
    if( too_little_work && ! (loop_cycles > (CacheMissLatency * 2) )) continue;
    Node *base = memop[j]->in(MemNode::Address)->in(AddPNode::Base);
    Node *ctrl = phase->get_ctrl(scaled[j]);
    Node *preoff = new (phase->C, 4) AddPNode( base, scaled[j], phase->_igvn.longcon(bytes_ahead) );
phase->register_new_node(preoff,ctrl);
    Node *prefetch = write[j] 
      ? (Node*) new (phase->C, 3) PrefetchWriteNode( memop[j]->in(MemNode::Memory), preoff )
      : (Node*) new (phase->C, 3) PrefetchReadNode ( memop[j]->in(MemNode::Memory), preoff );
    phase->register_new_node(prefetch, phase->get_ctrl(memop[j]));
    phase->_igvn.hash_delete(memop[j]);
    memop[j]->set_req(MemNode::Memory,prefetch);
  }

  return true;
}

//------------------------------policy_range_check-----------------------------
// Return TRUE or FALSE if the loop should be range-check-eliminated.
// Actually we do iteration-splitting, a more powerful form of RCE.
bool IdealLoopTree::policy_range_check( PhaseIdealLoop *phase ) const {
  if( !RangeCheckElimination ) return false;

  CountedLoopNode *cl = _head->as_CountedLoop();
  // If we unrolled with no intention of doing RCE and we later
  // changed our minds, we got no pre-loop.  Either we need to
  // make a new pre-loop, or we gotta disallow RCE.
  if( cl->is_main_no_pre_loop() ) return false; // Disallowed for now.
  if( cl->profile_trip_cnt() != COUNT_UNKNOWN && cl->profile_trip_cnt() <= 4.0 ) return false; // Do not bother!
  Node *trip_counter = cl->phi();

  // Check loop body for tests of trip-counter plus loop-invariant vs
  // loop-invariant.
  for( uint i = 0; i < _body.size(); i++ ) {
    Node *iff = _body[i];
    if( iff->Opcode() == Op_If ) { // Test?

      // Comparing trip+off vs limit
      Node *bol = iff->in(1);
      if( bol->req() != 2 ) continue; // dead constant test
      Node *cmp = bol->in(1);

      Node *rc_exp = cmp->in(1);
      Node *limit = cmp->in(2);

      Node *limit_c = phase->get_ctrl(limit);
      if( limit_c == phase->C->top() ) 
        return false;           // Found dead test on live IF?  No RCE!
      if( is_member(phase->get_loop(limit_c) ) ) {
        // Compare might have operands swapped; commute them
        rc_exp = cmp->in(2);
        limit  = cmp->in(1);
        limit_c = phase->get_ctrl(limit);
        if( is_member(phase->get_loop(limit_c) ) ) 
          continue;             // Both inputs are loop varying; cannot RCE
      }

      if (!phase->is_scaled_iv_plus_offset(rc_exp, trip_counter, NULL, NULL)) {
        continue;
      }
      // Yeah!  Found a test like 'trip+off vs limit'
      // Test is an IfNode, has 2 projections.  If BOTH are in the loop
      // we need loop unswitching instead of iteration splitting.
      if( is_loop_exit(iff) )
        return true;            // Found reason to split iterations
    } // End of is IF
  }

  return false;
}

//------------------------------policy_peel_only-------------------------------
// Return TRUE or FALSE if the loop should NEVER be RCE'd or aligned.  Useful
// for unrolling loops with NO array accesses.
bool IdealLoopTree::policy_peel_only( PhaseIdealLoop *phase ) const {

  for( uint i = 0; i < _body.size(); i++ )
    if( _body[i]->is_Mem() )
      return false;

  // No memory accesses at all!
  return true;
}

//------------------------------clone_up_backedge_goo--------------------------
// If Node n lives in the back_ctrl block and cannot float, we clone a private 
// version of n in preheader_ctrl block and return that, otherwise return n.
Node *PhaseIdealLoop::clone_up_backedge_goo( Node *back_ctrl, Node *preheader_ctrl, Node *n ) {
  if( get_ctrl(n) != back_ctrl ) return n;

  Node *x = NULL;               // If required, a clone of 'n'
  // Check for 'n' being pinned in the backedge.
  if( n->in(0) && n->in(0) == back_ctrl ) {
    x = n->clone();             // Clone a copy of 'n' to preheader
    x->set_req( 0, preheader_ctrl ); // Fix x's control input to preheader
  }

  // Recursive fixup any other input edges into x.
  // If there are no changes we can just return 'n', otherwise
  // we need to clone a private copy and change it.
  for( uint i = 1; i < n->req(); i++ ) {
    Node *g = clone_up_backedge_goo( back_ctrl, preheader_ctrl, n->in(i) );
    if( g != n->in(i) ) {
      if( !x )
        x = n->clone();
      x->set_req(i, g);
    }
  }
  if( x ) {                     // x can legally float to pre-header location
    register_new_node( x, preheader_ctrl );
    return x;
  } else {                      // raise n to cover LCA of uses
    set_ctrl( n, find_non_split_ctrl(back_ctrl->in(0)) );
  }
  return n;
}

//------------------------------insert_pre_post_loops--------------------------
// Insert pre and post loops.  If peel_only is set, the pre-loop can not have
// more iterations added.  It acts as a 'peel' only, no lower-bound RCE, no
// alignment.  Useful to unroll loops that do no array accesses.
void PhaseIdealLoop::insert_pre_post_loops( IdealLoopTree *loop, Node_List &old_new, bool peel_only ) {

  C->set_major_progress();

  // Find common pieces of the loop being guarded with pre & post loops
  CountedLoopNode *main_head = loop->_head->as_CountedLoop();
  assert( main_head->is_normal_loop(), "" );
  CountedLoopEndNode *main_end = main_head->loopexit();
  assert( main_end->outcnt() == 2, "1 true, 1 false path only" );
  uint dd_main_head = dom_depth(main_head);
  uint max = main_head->outcnt();

  Node *pre_header= main_head->in(LoopNode::EntryControl);
  Node *init      = main_head->init_trip();
  Node *incr      = main_end ->incr();
  Node *limit     = main_end ->limit();
  Node *stride    = main_end ->stride();
  Node *cmp       = main_end ->cmp_node();
  BoolTest::mask b_test = main_end->test_trip();

  // Need only 1 user of 'bol' because I will be hacking the loop bounds.
  Node *bol = main_end->in(CountedLoopEndNode::TestValue);
  if( bol->outcnt() != 1 ) {
    bol = bol->clone();
    register_new_node(bol,main_end->in(CountedLoopEndNode::TestControl));
    _igvn.hash_delete(main_end);
    main_end->set_req(CountedLoopEndNode::TestValue, bol);
  }
  // Need only 1 user of 'cmp' because I will be hacking the loop bounds.
  if( cmp->outcnt() != 1 ) {
    cmp = cmp->clone();
    register_new_node(cmp,main_end->in(CountedLoopEndNode::TestControl));
    _igvn.hash_delete(bol);
    bol->set_req(1, cmp);
  }

  //------------------------------
  // Step A: Create Post-Loop.
  Node* main_exit = main_end->proj_out(false);
  assert( main_exit->Opcode() == Op_IfFalse, "" );
  int dd_main_exit = dom_depth(main_exit);

  // Step A1: Clone the loop body.  The clone becomes the post-loop.  The main
  // loop pre-header illegally has 2 control users (old & new loops).
clone_loop(loop,old_new,dd_main_exit,NULL,true);
  assert( old_new[main_end ->_idx]->Opcode() == Op_CountedLoopEnd, "" );
  CountedLoopNode *post_head = old_new[main_head->_idx]->as_CountedLoop();
CountedLoopEndNode*post_end=old_new[main_end->_idx]->as_CountedLoopEnd();
  post_end->_prob = PROB_FAIR;  // Post-loop does not trip often
  post_head->set_post_loop(main_head);

  // Build the main-loop normal exit.
  IfFalseNode *new_main_exit = new (C, 1) IfFalseNode(main_end);
  _igvn.register_new_node_with_optimizer( new_main_exit );
  set_idom(new_main_exit, main_end, dd_main_exit );
  set_loop(new_main_exit, loop->_parent);

  // Step A2: Build a zero-trip guard for the post-loop.  After leaving the
  // main-loop, the post-loop may not execute at all.  We 'opaque' the incr
  // (the main-loop trip-counter exit value) because we will be changing
  // the exit value (via unrolling) so we cannot constant-fold away the zero
  // trip guard until all unrolling is done.
  Node *zer_opaq = new (C, 2) Opaque1Node(incr);
  Node *zer_cmp  = new (C, 3) CmpINode( zer_opaq, limit );
  Node *zer_bol  = new (C, 2) BoolNode( zer_cmp, b_test );
  register_new_node( zer_opaq, new_main_exit );
  register_new_node( zer_cmp , new_main_exit );
  register_new_node( zer_bol , new_main_exit );

  // Build the IfNode
  IfNode *zer_iff = new (C, 2) IfNode( new_main_exit, zer_bol, PROB_FAIR, COUNT_UNKNOWN );
  _igvn.register_new_node_with_optimizer( zer_iff );
  set_idom(zer_iff, new_main_exit, dd_main_exit);
  set_loop(zer_iff, loop->_parent);

  // Plug in the false-path, taken if we need to skip post-loop
  _igvn.hash_delete( main_exit );
  main_exit->set_req(0, zer_iff);
  _igvn._worklist.push(main_exit);
  set_idom(main_exit, zer_iff, dd_main_exit);
  set_idom(main_exit->unique_out(), zer_iff, dd_main_exit);
  // Make the true-path, must enter the post loop
  Node *zer_taken = new (C, 1) IfTrueNode( zer_iff );
  _igvn.register_new_node_with_optimizer( zer_taken );
  set_idom(zer_taken, zer_iff, dd_main_exit);
  set_loop(zer_taken, loop->_parent);
  // Plug in the true path
  _igvn.hash_delete( post_head );
  post_head->set_req(LoopNode::EntryControl, zer_taken);
  set_idom(post_head, zer_taken, dd_main_exit);

  // Step A3: Make the fall-in values to the post-loop come from the
  // fall-out values of the main-loop.
  for (DUIterator_Fast imax, i = main_head->fast_outs(imax); i < imax; i++) {
    Node* main_phi = main_head->fast_out(i);
    if( main_phi->is_Phi() && main_phi->in(0) == main_head && main_phi->outcnt() >0 ) {
      Node *post_phi = old_new[main_phi->_idx];
      assert0( post_phi );
      Node *fallmain  = clone_up_backedge_goo(main_head->back_control(),
                                              post_head->init_control(),
                                              main_phi->in(LoopNode::LoopBackControl));
      _igvn.hash_delete(post_phi);
      post_phi->set_req( LoopNode::EntryControl, fallmain );
    }
  }

  // Make branches in the post-loop all have unknown counts, but keep their
  // same probabilities as the main-loop.
  for( uint i = 0; i < loop->_body.size(); i++ ) {
    if( loop->_body.at(i)->is_If() ) {
      IfNode *iff = loop->_body.at(i)->as_If();
      // Main loop trips less often (really should subtract off the pre- and
      // post-loop's execution count).
      if( iff->_fcnt > 2.0 )
        iff->_fcnt -= 2.0;      // Guesses on pre-loop and post-loop counts
      old_new[iff->_idx]->as_If()->_fcnt = COUNT_UNKNOWN; // post-loop has unknown trip count
    }
  }

  // Update local caches for next stanza
  main_exit = new_main_exit;


  //------------------------------
  // Step B: Create Pre-Loop.

  // Step B1: Clone the loop body.  The clone becomes the pre-loop.  The main
  // loop pre-header illegally has 2 control users (old & new loops).
clone_loop(loop,old_new,dd_main_head,NULL,false);
  CountedLoopNode*    pre_head = old_new[main_head->_idx]->as_CountedLoop();
  CountedLoopEndNode* pre_end  = old_new[main_end ->_idx]->as_CountedLoopEnd();
  pre_end->_prob = PROB_FAIR;   // Pre-loop does not trip often
  pre_head->set_pre_loop(main_head);
  Node *pre_incr = old_new[incr->_idx];

  // Find the pre-loop normal exit.
  Node* pre_exit = pre_end->proj_out(false);
  assert( pre_exit->Opcode() == Op_IfFalse, "" );
  IfFalseNode *new_pre_exit = new (C, 1) IfFalseNode(pre_end);
  _igvn.register_new_node_with_optimizer( new_pre_exit );
  set_idom(new_pre_exit, pre_end, dd_main_head);
  set_loop(new_pre_exit, loop->_parent);

  // Step B2: Build a zero-trip guard for the main-loop.  After leaving the
  // pre-loop, the main-loop may not execute at all.  Later in life this
  // zero-trip guard will become the minimum-trip guard when we unroll
  // the main-loop.
  Node *min_opaq = new (C, 2) Opaque1Node(limit);
  Node *min_cmp  = new (C, 3) CmpINode( pre_incr, min_opaq );
  Node *min_bol  = new (C, 2) BoolNode( min_cmp, b_test );
  register_new_node( min_opaq, new_pre_exit );
  register_new_node( min_cmp , new_pre_exit );
  register_new_node( min_bol , new_pre_exit );

  // Build the IfNode
IfNode*min_iff=new(C,2)IfNode(new_pre_exit,min_bol,PROB_LIKELY_MAG(2),COUNT_UNKNOWN);
  _igvn.register_new_node_with_optimizer( min_iff );
  set_idom(min_iff, new_pre_exit, dd_main_head);
  set_loop(min_iff, loop->_parent);

  // Plug in the false-path, taken if we need to skip main-loop
  _igvn.hash_delete( pre_exit );
  pre_exit->set_req(0, min_iff);
  set_idom(pre_exit, min_iff, dd_main_head);
  set_idom(pre_exit->unique_out(), min_iff, dd_main_head);
  // Make the true-path, must enter the main loop
  Node *min_taken = new (C, 1) IfTrueNode( min_iff );
  _igvn.register_new_node_with_optimizer( min_taken );
  set_idom(min_taken, min_iff, dd_main_head);
  set_loop(min_taken, loop->_parent);
  // Plug in the true path
  _igvn.hash_delete( main_head );
  main_head->set_req(LoopNode::EntryControl, min_taken);
  set_idom(main_head, min_taken, dd_main_head);

  // Step B3: Make the fall-in values to the main-loop come from the
  // fall-out values of the pre-loop.
  for (DUIterator_Fast i2max, i2 = main_head->fast_outs(i2max); i2 < i2max; i2++) {
    Node* main_phi = main_head->fast_out(i2);
    if( main_phi->is_Phi() && main_phi->in(0) == main_head && main_phi->outcnt() > 0 ) {
      Node *pre_phi = old_new[main_phi->_idx];
      Node *fallpre  = clone_up_backedge_goo(pre_head->back_control(),
                                             main_head->init_control(),
                                             pre_phi->in(LoopNode::LoopBackControl));
      _igvn.hash_delete(main_phi);
      main_phi->set_req( LoopNode::EntryControl, fallpre );
    }
  }

  // Make branches in the pre-loop all have unknown counts, but keep their
  // same probabilities as the main-loop.
  for( uint i = 0; i < loop->_body.size(); i++ ) {
    Node *iff = old_new[loop->_body.at(i)->_idx];
    if( iff->is_If() )
      iff->as_If()->_fcnt = COUNT_UNKNOWN;
  }

  // Step B4: Shorten the pre-loop to run only 1 iteration (for now).
  // RCE and alignment may change this later.
  Node *cmp_end = pre_end->cmp_node();
  assert( cmp_end->in(2) == limit, "" );
  Node *pre_limit = new (C, 3) AddINode( init, stride );

  // Save the original loop limit in this Opaque1 node for
  // use by range check elimination.
  Node *pre_opaq  = new (C, 3) Opaque1Node(pre_limit, limit);

  register_new_node( pre_limit, pre_head->in(0) );
  register_new_node( pre_opaq , pre_head->in(0) );

  // Since no other users of pre-loop compare, I can hack limit directly
  assert( cmp_end->outcnt() == 1, "no other users" );
  _igvn.hash_delete(cmp_end);
  cmp_end->set_req(2, peel_only ? pre_limit : pre_opaq);

  // Special case for not-equal loop bounds:
  // Change pre loop test, main loop test, and the
  // main loop guard test to use lt or gt depending on stride
  // direction:
  // positive stride use <
  // negative stride use >

  if (pre_end->in(CountedLoopEndNode::TestValue)->as_Bool()->_test._test == BoolTest::ne) {

    BoolTest::mask new_test = (main_end->stride_con() > 0) ? BoolTest::lt : BoolTest::gt;
    // Modify pre loop end condition
    Node* pre_bol = pre_end->in(CountedLoopEndNode::TestValue)->as_Bool();
    BoolNode* new_bol0 = new (C, 2) BoolNode(pre_bol->in(1), new_test);
    register_new_node( new_bol0, pre_head->in(0) );
    _igvn.hash_delete(pre_end);
    pre_end->set_req(CountedLoopEndNode::TestValue, new_bol0);
    // Modify main loop guard condition
    assert(min_iff->in(CountedLoopEndNode::TestValue) == min_bol, "guard okay");
    BoolNode* new_bol1 = new (C, 2) BoolNode(min_bol->in(1), new_test);
    register_new_node( new_bol1, new_pre_exit );
    _igvn.hash_delete(min_iff);
    min_iff->set_req(CountedLoopEndNode::TestValue, new_bol1);
    // Modify main loop end condition
    BoolNode* main_bol = main_end->in(CountedLoopEndNode::TestValue)->as_Bool();
    BoolNode* new_bol2 = new (C, 2) BoolNode(main_bol->in(1), new_test);
    register_new_node( new_bol2, main_end->in(CountedLoopEndNode::TestControl) );
    _igvn.hash_delete(main_end);
    main_end->set_req(CountedLoopEndNode::TestValue, new_bol2);
  }

  // Flag main loop
  main_head->set_main_loop();
  if( peel_only ) main_head->set_main_no_pre_loop();

  // It's difficult to be precise about the trip-counts
  // for the pre/post loops.  They are usually very short,
  // so guess that 4 trips is a reasonable value.
//CNC - guess 1 is a reasonable value
post_head->set_profile_trip_cnt(1.0);
pre_head->set_profile_trip_cnt(1.0);

  // Now force out all loop-invariant dominating tests.  The optimizer 
  // finds some, but we _know_ they are all useless.
  peeled_dom_test_elim(loop,old_new);
}

//------------------------------is_invariant-----------------------------
// Return true if n is invariant
bool IdealLoopTree::is_invariant(Node* n) const {
  Node *n_c = _phase->get_ctrl(n);
  if (n_c->is_top()) return false;
  return !is_member(_phase->get_loop(n_c));
}


//------------------------------do_unroll--------------------------------------
// Unroll the loop body one step - make each trip do 2 iterations.
void PhaseIdealLoop::do_unroll( IdealLoopTree *loop, Node_List &old_new, bool adjust_min_trip ) {
  assert( LoopUnrollLimit, "" );
if(PrintOpto){
C2OUT->print("Unrolling for prefetching");
    loop->dump_head();
  }
  CountedLoopNode *loop_head = loop->_head->as_CountedLoop();
  CountedLoopEndNode *loop_end = loop_head->loopexit();
  assert( loop_end, "" );

  // Remember loop node count before unrolling to detect
  // if rounds of unroll,optimize are making progress
  loop_head->set_node_count_before_unroll(loop->_body.size());

  Node *ctrl  = loop_head->in(LoopNode::EntryControl);
  Node *limit = loop_head->limit();
  Node *init  = loop_head->init_trip();
  Node *strid = loop_head->stride();

  Node *opaq = NULL;
  if( adjust_min_trip ) {       // If not maximally unrolling, need adjustment
    assert( loop_head->is_main_loop(), "" );
if(ctrl->Opcode()!=Op_IfTrue&&ctrl->Opcode()!=Op_IfFalse)return;//Cannot find pre-loop?
    Node *iff = ctrl->in(0);
    assert( iff->Opcode() == Op_If, "" );
    Node *bol = iff->in(1);
    assert( bol->Opcode() == Op_Bool, "" );
    Node *cmp = bol->in(1);
    assert( cmp->Opcode() == Op_CmpI, "" );
    opaq = cmp->in(2);
    // Occasionally it's possible for a pre-loop Opaque1 node to be
    // optimized away and then another round of loop opts attempted.
    // We can not optimize this particular loop in that case.
    if( opaq->Opcode() != Op_Opaque1 )
      return;                   // Cannot find pre-loop!  Bail out!
    // Occasionally 'opaq' has a constant input allowing opaqs 'get_ctrl' to
    // be set all the way up to the program start.  Force opaq and all inputs
    // to opaq to be set in the min-trip-test block, which is one block before
    // the loop pre-header.
ctrl=iff->in(0);//Set control for opaq and all inputs to opaq
    set_ctrl(opaq,ctrl);        // ...(inputs set below)
  }

  C->set_major_progress();

  // Adjust max trip count. The trip count is intentionally rounded
  // down here (e.g. 15-> 7-> 3-> 1) because if we unwittingly over-unroll,
  // the main, unrolled, part of the loop will never execute as it is protected
  // by the min-trip test.  See bug 4834191 for a case where we over-unrolled
  // and later determined that part of the unrolled loop was dead.
  loop_head->set_trip_count(loop_head->trip_count() / 2);

  // Double the count of original iterations in the unrolled loop body.
  loop_head->double_unrolled_count();

  // -----------
  // Step 2: Cut back the trip counter for an unroll amount of 2.
  // Loop will normally trip (limit - init)/stride_con.  Since it's a
  // CountedLoop this is exact (stride divides limit-init exactly).
  // We are going to double the loop body, so we want to knock off any
  // odd iteration: (trip_cnt & ~1).  Then back compute a new limit.
  Node *span = new (C, 3) SubINode( limit, init );
  register_new_node( span, ctrl );
  Node *trip = new (C, 3) DivINode( 0, span, strid );
  register_new_node( trip, ctrl );
  Node *mtwo = _igvn.intcon(-2);
  set_ctrl(mtwo, C->root());
  Node *rond = new (C, 3) AndINode( trip, mtwo );
  register_new_node( rond, ctrl );
  Node *spn2 = new (C, 3) MulINode( rond, strid );
  register_new_node( spn2, ctrl );
  Node *lim2 = new (C, 3) AddINode( spn2, init );
  register_new_node( lim2, ctrl );

  // Hammer in the new limit
  Node *ctrl2 = loop_end->in(0);
  Node *cmp2 = new (C, 3) CmpINode( loop_head->incr(), lim2 );
  register_new_node( cmp2, ctrl2 );
  Node *bol2 = new (C, 2) BoolNode( cmp2, loop_end->test_trip() );
  register_new_node( bol2, ctrl2 );
  _igvn.hash_delete(loop_end);
  loop_end->set_req(CountedLoopEndNode::TestValue, bol2);

  // Step 3: Find the min-trip test guaranteed before a 'main' loop.
  // Make it a 1-trip test (means at least 2 trips).
  if( adjust_min_trip ) {
    // Guard test uses an 'opaque' node which is not shared.  Hence I
    // can edit it's inputs directly.  Hammer in the new limit for the
    // minimum-trip guard.
    assert( opaq->outcnt() == 1, "" );
    _igvn.hash_delete(opaq);
    opaq->set_req(1, lim2);
  }

  // ---------
  // Step 4: Clone the loop body.  Move it inside the loop.  This loop body 
  // represents the odd iterations; since the loop trips an even number of
  // times its backedge is never taken.  Kill the backedge.
  uint dd = dom_depth(loop_head);
clone_loop(loop,old_new,dd,NULL,false);

  // Make backedges of the clone equal to backedges of the original.
  // Make the fall-in from the original come from the fall-out of the clone.
  for (DUIterator_Fast jmax, j = loop_head->fast_outs(jmax); j < jmax; j++) {
    Node* phi = loop_head->fast_out(j);
    if( phi->is_Phi() && phi->in(0) == loop_head && phi->outcnt() > 0 ) {
      Node *newphi = old_new[phi->_idx];
      _igvn.hash_delete( phi );
      _igvn.hash_delete( newphi );

      phi   ->set_req(LoopNode::   EntryControl, newphi->in(LoopNode::LoopBackControl));
      newphi->set_req(LoopNode::LoopBackControl, phi   ->in(LoopNode::LoopBackControl));
      phi   ->set_req(LoopNode::LoopBackControl, C->top());
    }
  }  
  Node *clone_head = old_new[loop_head->_idx];
  _igvn.hash_delete( clone_head );
  loop_head ->set_req(LoopNode::   EntryControl, clone_head->in(LoopNode::LoopBackControl));
  clone_head->set_req(LoopNode::LoopBackControl, loop_head ->in(LoopNode::LoopBackControl));
  loop_head ->set_req(LoopNode::LoopBackControl, C->top());
  loop->_head = clone_head;     // New loop header

  // Double the unroll ratio
  clone_head->as_CountedLoop()->set_unroll_ratio(loop_head->unroll_ratio()*2);

  set_idom(loop_head,  loop_head ->in(LoopNode::EntryControl), dd);
  set_idom(clone_head, clone_head->in(LoopNode::EntryControl), dd);

  // Kill the clone's backedge
  Node *newcle = old_new[loop_end->_idx];
  _igvn.hash_delete( newcle );
  Node *one = _igvn.intcon(1);
  set_ctrl(one, C->root());
  newcle->set_req(1, one);
  // Force clone into same loop body
  uint max = loop->_body.size();
  for( uint k = 0; k < max; k++ ) {
    Node *old = loop->_body.at(k);
    Node *nnn = old_new[old->_idx];
    loop->_body.push(nnn);
    if (!has_ctrl(old))
      set_loop(nnn, loop);
    // Divide the branch counts in half, splitting the counts between the two loop bodies.
    if( old->is_If() ) { // Split branch counts between the unrolled parts
      if( old->as_If()->_fcnt != COUNT_UNKNOWN ) old->as_If()->_fcnt /= 2.0; 
      if( nnn->as_If()->_fcnt != COUNT_UNKNOWN ) nnn->as_If()->_fcnt /= 2.0;
    }
  }
  // The loop is twice as likely to exit now than before, since each trip does
  // twice as much work (and you run 1/2 as many of them).
  double loop_cnt = loop_end->_fcnt == COUNT_UNKNOWN ? 10.0 : loop_end->_fcnt*2.0; // Note: loop_end->_fcnt has already been halved
  double loop_back_cnt = loop_end->_prob*loop_cnt;
  double loop_exit_cnt = loop_cnt - loop_back_cnt;
  loop_back_cnt /= 2.0;
  if( loop_end->_fcnt != COUNT_UNKNOWN )
    loop_end->_fcnt = loop_back_cnt+loop_exit_cnt;
  assert0( loop_back_cnt + loop_exit_cnt != 0 );
  loop_end->_prob = loop_back_cnt/(loop_back_cnt+loop_exit_cnt);

  loop->record_for_igvn();
}

//------------------------------do_maximally_unroll----------------------------

void PhaseIdealLoop::do_maximally_unroll( IdealLoopTree *loop, Node_List &old_new ) {
  CountedLoopNode *cl = loop->_head->as_CountedLoop();
  assert( cl->trip_count() > 0, "");

  // If loop is tripping an odd number of times, peel odd iteration
  if( (cl->trip_count() & 1) == 1 ) {
    do_peeling( loop, old_new );
  }

  // Now its tripping an even number of times remaining.  Double loop body.
  // Do not adjust pre-guards; they are not needed and do not exist.
  if( cl->trip_count() > 0 ) { 
    do_unroll( loop, old_new, false );
  }
}

//------------------------------dominates_backedge---------------------------------
// Returns true if ctrl is executed on every complete iteration
bool IdealLoopTree::dominates_backedge(Node* ctrl) {
  assert(ctrl->is_CFG(), "must be control");
  Node* backedge = _head->as_Loop()->in(LoopNode::LoopBackControl);
  return _phase->dom_lca_internal(ctrl, backedge) == ctrl;
}

//------------------------------add_constraint---------------------------------
// Constrain the main loop iterations so the condition:
//    scale_con * I + offset  <  limit
// always holds true.  That is, either increase the number of iterations in
// the pre-loop or the post-loop until the condition holds true in the main 
// loop.  Stride, scale, offset and limit are all loop invariant.  Further, 
// stride and scale are constants (offset and limit often are).
void PhaseIdealLoop::add_constraint( int stride_con, int scale_con, Node *offset, Node *limit, Node *pre_ctrl, Node **pre_limit, Node **main_limit ) {

  // Compute "I :: (limit-offset)/scale_con"
  Node *con = new (C, 3) SubINode( limit, offset );
  register_new_node( con, pre_ctrl );
  Node *scale = _igvn.intcon(scale_con);
  set_ctrl(scale, C->root());
  Node *X = new (C, 3) DivINode( 0, con, scale );
  register_new_node( X, pre_ctrl );

  // For positive stride, the pre-loop limit always uses a MAX function 
  // and the main loop a MIN function.  For negative stride these are
  // reversed.  
  
  // Also for positive stride*scale the affine function is increasing, so the 
  // pre-loop must check for underflow and the post-loop for overflow.
  // Negative stride*scale reverses this; pre-loop checks for overflow and
  // post-loop for underflow.
  if( stride_con*scale_con > 0 ) {
    // Compute I < (limit-offset)/scale_con
    // Adjust main-loop last iteration to be MIN/MAX(main_loop,X)
    *main_limit = (stride_con > 0) 
      ? (Node*)(new (C, 3) MinINode( *main_limit, X ))
      : (Node*)(new (C, 3) MaxINode( *main_limit, X ));
    register_new_node( *main_limit, pre_ctrl );

  } else {
    // Compute (limit-offset)/scale_con + SGN(-scale_con) <= I
    // Add the negation of the main-loop constraint to the pre-loop.
    // See footnote [++] below for a derivation of the limit expression.
    Node *incr = _igvn.intcon(scale_con > 0 ? -1 : 1);
    set_ctrl(incr, C->root());
    Node *adj = new (C, 3) AddINode( X, incr );
    register_new_node( adj, pre_ctrl );
    *pre_limit = (scale_con > 0)
      ? (Node*)new (C, 3) MinINode( *pre_limit, adj )
      : (Node*)new (C, 3) MaxINode( *pre_limit, adj );
    register_new_node( *pre_limit, pre_ctrl );

//   [++] Here's the algebra that justifies the pre-loop limit expression:
//   
//   NOT( scale_con * I + offset  <  limit )
//      ==
//   scale_con * I + offset  >=  limit
//      ==
//   SGN(scale_con) * I  >=  (limit-offset)/|scale_con|
//      ==
//   (limit-offset)/|scale_con|   <=  I * SGN(scale_con)
//      ==
//   (limit-offset)/|scale_con|-1  <  I * SGN(scale_con)
//      ==
//   ( if (scale_con > 0) /*common case*/
//       (limit-offset)/scale_con - 1  <  I
//     else  
//       (limit-offset)/scale_con + 1  >  I
//    )
//   ( if (scale_con > 0) /*common case*/
//       (limit-offset)/scale_con + SGN(-scale_con)  <  I
//     else  
//       (limit-offset)/scale_con + SGN(-scale_con)  >  I
  }
}


//------------------------------is_scaled_iv---------------------------------
// Return true if exp is a constant times an induction var
bool PhaseIdealLoop::is_scaled_iv(Node* exp, Node* iv, int* p_scale) {
  if (exp == iv) {
    if (p_scale != NULL) {
      *p_scale = 1;
    }
    return true;
  }
  int opc = exp->Opcode();
  if (opc == Op_MulI) {
    if (exp->in(1) == iv && exp->in(2)->is_Con()) {
      if (p_scale != NULL) {
        *p_scale = exp->in(2)->get_int();
      }
      return true;
    }
    if (exp->in(2) == iv && exp->in(1)->is_Con()) {
      if (p_scale != NULL) {
        *p_scale = exp->in(1)->get_int();
      }
      return true;
    }
  } else if (opc == Op_LShiftI) {
    if (exp->in(1) == iv && exp->in(2)->is_Con()) {
      if (p_scale != NULL) {
        *p_scale = 1 << exp->in(2)->get_int();
      }
      return true;
    }
  }
  return false;
}

//-----------------------------is_scaled_iv_plus_offset------------------------------
// Return true if exp is a simple induction variable expression: k1*iv + (invar + k2)
bool PhaseIdealLoop::is_scaled_iv_plus_offset(Node* exp, Node* iv, int* p_scale, Node** p_offset, int depth) {
  if (is_scaled_iv(exp, iv, p_scale)) {
    if (p_offset != NULL) {
      Node *zero = _igvn.intcon(0); 
      set_ctrl(zero, C->root());
      *p_offset = zero;
    }
    return true;
  }
  int opc = exp->Opcode();
  if (opc == Op_AddI) {
    if (is_scaled_iv(exp->in(1), iv, p_scale)) {
      if (p_offset != NULL) {
        *p_offset = exp->in(2);
      }
      return true;
    }
    if (exp->in(2)->is_Con()) {
      Node* offset2 = NULL;
      if (depth < 2 &&
          is_scaled_iv_plus_offset(exp->in(1), iv, p_scale,
                                   p_offset != NULL ? &offset2 : NULL, depth+1)) {
        if (p_offset != NULL) {
          Node *ctrl_off2 = get_ctrl(offset2);
          Node* offset = new (C, 3) AddINode(offset2, exp->in(2));
          register_new_node(offset, ctrl_off2);
          *p_offset = offset;
        }
        return true;
      }
    }
  } else if (opc == Op_SubI) {
    if (is_scaled_iv(exp->in(1), iv, p_scale)) {
      if (p_offset != NULL) {
        Node *zero = _igvn.intcon(0); 
        set_ctrl(zero, C->root());
        Node *ctrl_off = get_ctrl(exp->in(2));
        Node* offset = new (C, 3) SubINode(zero, exp->in(2));
        register_new_node(offset, ctrl_off);
        *p_offset = offset;
      }
      return true;
    }
    if (is_scaled_iv(exp->in(2), iv, p_scale)) {
      if (p_offset != NULL) {
        *p_scale *= -1;
        *p_offset = exp->in(1);
      }
      return true;
    }
  }
  return false;
}

//------------------------------do_range_check---------------------------------
// Eliminate range-checks and other trip-counter vs loop-invariant tests.
void PhaseIdealLoop::do_range_check( IdealLoopTree *loop, Node_List &old_new ) {
if(PrintOpto){
C2OUT->print("Range Check Elimination ");
    loop->dump_head();
  }
  assert( RangeCheckElimination, "" );
  CountedLoopNode *cl = loop->_head->as_CountedLoop();
  assert( cl->is_main_loop(), "" );

  // Find the trip counter; we are iteration splitting based on it
  Node *trip_counter = cl->phi();
  // Find the main loop limit; we will trim it's iterations 
  // to not ever trip end tests
  Node *main_limit = cl->limit();
  // Find the pre-loop limit; we will expand it's iterations to
  // not ever trip low tests.
  Node *ctrl  = cl->in(LoopNode::EntryControl);
  assert( ctrl->Opcode() == Op_IfTrue || ctrl->Opcode() == Op_IfFalse, "" );
  Node *iffm = ctrl->in(0);
  assert( iffm->Opcode() == Op_If, "" );
  Node *p_f = iffm->in(0);
  assert( p_f->Opcode() == Op_IfFalse, "" );
  CountedLoopEndNode *pre_end = p_f->in(0)->as_CountedLoopEnd();
  assert( pre_end->loopnode()->is_pre_loop(), "" );
  Node *pre_opaq1 = pre_end->limit();
  // Occasionally it's possible for a pre-loop Opaque1 node to be
  // optimized away and then another round of loop opts attempted.
  // We can not optimize this particular loop in that case.
  if( pre_opaq1->Opcode() != Op_Opaque1 )
    return;
  Opaque1Node *pre_opaq = (Opaque1Node*)pre_opaq1;
  Node *pre_limit = pre_opaq->in(1);

  // Where do we put new limit calculations
  Node *pre_ctrl = pre_end->loopnode()->in(LoopNode::EntryControl);

  // Ensure the original loop limit is available from the
  // pre-loop Opaque1 node.
  Node *orig_limit = pre_opaq->original_loop_limit();
  if( orig_limit == NULL || _igvn.type(orig_limit) == Type::TOP )
    return;

  // Need to find the main-loop zero-trip guard
  Node *bolzm = iffm->in(1);
  assert( bolzm->Opcode() == Op_Bool, "" );
  Node *cmpzm = bolzm->in(1);
  assert( cmpzm->is_Cmp(), "" );
  Node *opqzm = cmpzm->in(2);
  if( opqzm->Opcode() != Op_Opaque1 )
    return;
  assert( opqzm->in(1) == main_limit, "do not understand situation" );

  // Must know if its a count-up or count-down loop

  // protect against stride not being a constant
  if ( !cl->stride_is_con() ) {
    return;
  }
  int stride_con = cl->stride_con();
  Node *zero = _igvn.intcon(0); 
  Node *one  = _igvn.intcon(1);
  set_ctrl(zero, C->root());
  set_ctrl(one,  C->root());

  // Range checks that do not dominate the loop backedge (ie.
  // conditionally executed) can lengthen the pre loop limit beyond
  // the original loop limit. To prevent this, the pre limit is
  // (for stride > 0) MINed with the original loop limit (MAXed
  // stride < 0) when some range_check (rc) is conditionally
  // executed.
  bool conditional_rc = false;

  // Check loop body for tests of trip-counter plus loop-invariant vs
  // loop-invariant.
  for( uint i = 0; i < loop->_body.size(); i++ ) {
    Node *iff = loop->_body[i];
    if( iff->Opcode() == Op_If ) { // Test?

      // Test is an IfNode, has 2 projections.  If BOTH are in the loop
      // we need loop unswitching instead of iteration splitting.
      Node *exit = loop->is_loop_exit(iff);
      if( !exit ) continue;
      int flip = (exit->Opcode() == Op_IfTrue) ? 1 : 0;

      // Get boolean condition to test
      Node *i1 = iff->in(1);
      if( !i1->is_Bool() ) continue;
      BoolNode *bol = i1->as_Bool();
      BoolTest b_test = bol->_test;
      // Flip sense of test if exit condition is flipped
      if( flip )
        b_test = b_test.negate();

      // Get compare
      Node *cmp = bol->in(1);

      // Look for trip_counter + offset vs limit
      Node *rc_exp = cmp->in(1);
      Node *limit  = cmp->in(2);
      jint scale_con= 1;        // Assume trip counter not scaled

      Node *limit_c = get_ctrl(limit);
      if( loop->is_member(get_loop(limit_c) ) ) {
        // Compare might have operands swapped; commute them
        b_test = b_test.commute();
        rc_exp = cmp->in(2);
        limit  = cmp->in(1);
        limit_c = get_ctrl(limit);
        if( loop->is_member(get_loop(limit_c) ) ) 
          continue;             // Both inputs are loop varying; cannot RCE
      }
      // Here we know 'limit' is loop invariant

      // 'limit' maybe pinned below the zero trip test (probably from a
      // previous round of rce), in which case, it can't be used in the
      // zero trip test expression which must occur before the zero test's if.
      if( limit_c == ctrl ) {
        continue;  // Don't rce this check but continue looking for other candidates.
      }

      // Check for scaled induction variable plus an offset
      Node *offset = NULL;

      if (!is_scaled_iv_plus_offset(rc_exp, trip_counter, &scale_con, &offset)) {
        continue;
      }

      Node *offset_c = get_ctrl(offset);
      if( loop->is_member( get_loop(offset_c) ) )
        continue;               // Offset is not really loop invariant
      // Here we know 'offset' is loop invariant.

      // As above for the 'limit', the 'offset' maybe pinned below the
      // zero trip test.
      if( offset_c == ctrl ) {
        continue; // Don't rce this check but continue looking for other candidates.
      }

      // At this point we have the expression as:
      //   scale_con * trip_counter + offset :: limit
      // where scale_con, offset and limit are loop invariant.  Trip_counter 
      // monotonically increases by stride_con, a constant.  Both (or either) 
      // stride_con and scale_con can be negative which will flip about the 
      // sense of the test.

      // Adjust pre and main loop limits to guard the correct iteration set
      if( cmp->Opcode() == Op_CmpU ) {// Unsigned compare is really 2 tests
        if( b_test._test == BoolTest::lt ) { // Range checks always use lt
          // The overflow limit: scale*I+offset < limit
          add_constraint( stride_con, scale_con, offset, limit, pre_ctrl, &pre_limit, &main_limit );
          // The underflow limit: 0 <= scale*I+offset.
          // Some math yields: -scale*I-(offset+1) < 0
          Node *plus_one = new (C, 3) AddINode( offset, one );
          register_new_node( plus_one, pre_ctrl );
          Node *neg_offset = new (C, 3) SubINode( zero, plus_one );
          register_new_node( neg_offset, pre_ctrl );
          add_constraint( stride_con, -scale_con, neg_offset, zero, pre_ctrl, &pre_limit, &main_limit );
          if (!conditional_rc) {
            conditional_rc = !loop->dominates_backedge(iff);
          }
        } else {
          if( PrintOpto ) 
C2OUT->print_cr("missed RCE opportunity");
          continue;             // In release mode, ignore it
        }
      } else {                  // Otherwise work on normal compares
        switch( b_test._test ) {
        case BoolTest::ge:      // Convert X >= Y to -X <= -Y
          scale_con = -scale_con;
          offset = new (C, 3) SubINode( zero, offset );
          register_new_node( offset, pre_ctrl );
          limit  = new (C, 3) SubINode( zero, limit  );
          register_new_node( limit, pre_ctrl );
          // Fall into LE case
        case BoolTest::le:      // Convert X <= Y to X < Y+1
          limit = new (C, 3) AddINode( limit, one );
          register_new_node( limit, pre_ctrl );
          // Fall into LT case
        case BoolTest::lt: 
          add_constraint( stride_con, scale_con, offset, limit, pre_ctrl, &pre_limit, &main_limit );
          if (!conditional_rc) {
            conditional_rc = !loop->dominates_backedge(iff);
          }
          break;
        default:
          if( PrintOpto ) 
C2OUT->print_cr("missed RCE opportunity");
          continue;             // Unhandled case
        }
      }

      // Kill the eliminated test
      C->set_major_progress();
      Node *kill_con = _igvn.intcon( 1-flip );
      set_ctrl(kill_con, C->root());
      _igvn.hash_delete(iff);
      iff->set_req(1, kill_con);
      _igvn._worklist.push(iff);
      // Find surviving projection
      assert(iff->is_If(), "");
      ProjNode* dp = ((IfNode*)iff)->proj_out(1-flip);
      // Find loads off the surviving projection; remove their control edge
      for (DUIterator_Fast imax, i = dp->fast_outs(imax); i < imax; i++) {
        Node* cd = dp->fast_out(i); // Control-dependent node
        if( cd->is_Load() ) {   // Loads can now float around in the loop
          _igvn.hash_delete(cd);
          // Allow the load to float around in the loop but NOT before the
          // pre-loop.  It cannot float above the loop because following
          // compiler passes (esp clone-loop) expect loop-invariants to
          // already have been hoisted out.
cd->set_req(0,ctrl);//ctrl; just above loop, not NULL
          if( !is_member(loop,get_early_ctrl(cd,false)) )
cd->set_req(0,cl);//do not let it escape the loop yet
if(is_member(loop,get_ctrl(cd)))//was loop member?
            set_ctrl(cd,cl);
          _igvn._worklist.push(cd);
          --i;
          --imax;
        }
      }

    } // End of is IF

  }

  // Update loop limits
  if (conditional_rc) {
    pre_limit = (stride_con > 0) ? (Node*)new (C,3) MinINode(pre_limit, orig_limit)
                                 : (Node*)new (C,3) MaxINode(pre_limit, orig_limit);
    register_new_node(pre_limit, pre_ctrl);
  }
  _igvn.hash_delete(pre_opaq);
  pre_opaq->set_req(1, pre_limit);

  // Note:: we are making the main loop limit no longer precise;
  // need to round up based on stride.
  if( stride_con != 1 && stride_con != -1 ) { // Cutout for common case
    // "Standard" round-up logic:  ([main_limit-init+(y-1)]/y)*y+init
    // Hopefully, compiler will optimize for powers of 2.
    Node *ctrl = get_ctrl(main_limit);
    Node *stride = cl->stride();
    Node *init = cl->init_trip();
    Node *span = new (C, 3) SubINode(main_limit,init);
    register_new_node(span,ctrl);
    Node *rndup = _igvn.intcon(stride_con + ((stride_con>0)?-1:1));
    Node *add = new (C, 3) AddINode(span,rndup);
    register_new_node(add,ctrl);
    Node *div = new (C, 3) DivINode(0,add,stride);
    register_new_node(div,ctrl);
    Node *mul = new (C, 3) MulINode(div,stride);
    register_new_node(mul,ctrl);
    Node *newlim = new (C, 3) AddINode(mul,init);
    register_new_node(newlim,ctrl);
    main_limit = newlim;
  }

  Node *main_cle = cl->loopexit();
  Node *main_bol = main_cle->in(1);
  // Hacking loop bounds; need private copies of exit test
  if( main_bol->outcnt() > 1 ) {// BoolNode shared?
    _igvn.hash_delete(main_cle);
    main_bol = main_bol->clone();// Clone a private BoolNode
    register_new_node( main_bol, main_cle->in(0) );
    main_cle->set_req(1,main_bol);
  }
  Node *main_cmp = main_bol->in(1);
  if( main_cmp->outcnt() > 1 ) { // CmpNode shared?
    _igvn.hash_delete(main_bol);
    main_cmp = main_cmp->clone();// Clone a private CmpNode
    register_new_node( main_cmp, main_cle->in(0) );
    main_bol->set_req(1,main_cmp);
  }
  // Hack the now-private loop bounds
  _igvn.hash_delete(main_cmp);
  main_cmp->set_req(2, main_limit);
  _igvn._worklist.push(main_cmp);
  // The OpaqueNode is unshared by design
  _igvn.hash_delete(opqzm);
  assert( opqzm->outcnt() == 1, "cannot hack shared node" );
  opqzm->set_req(1,main_limit);
  _igvn._worklist.push(opqzm);
}

//------------------------------DCE_loop_body----------------------------------
// Remove simplistic dead code from loop body
void IdealLoopTree::DCE_loop_body() {
  for( uint i = 0; i < _body.size(); i++ ) 
    if( _body.at(i)->outcnt() == 0 ) 
      _body.map( i--, _body.pop() );
}
  

//------------------------------adjust_loop_exit_prob--------------------------
// Look for loop-exit tests with the 50/50 (or worse) guesses from the parsing stage.
// Replace with a 1-in-10 exit guess.
void IdealLoopTree::adjust_loop_exit_prob( PhaseIdealLoop *phase ) {
  // Pre- and Post-Loops are given short trip counts on purpose.
if(_head->is_CountedLoop()){
CountedLoopNode*cl=_head->as_CountedLoop();
    if( cl->is_pre_loop() || cl->is_post_loop() ) return;
  }
  Node *test = tail();
  while( test != _head ) {
    uint top = test->Opcode();
    if( top == Op_IfTrue || top == Op_IfFalse ) {
      int test_con = ((ProjNode*)test)->_con;
      assert(top == (uint)(test_con? Op_IfTrue: Op_IfFalse), "sanity");
      IfNode *iff = test->in(0)->as_If();
      if( iff->outcnt() == 2 ) {        // Ignore dead tests
        Node *bol = iff->in(1);
        if( bol && bol->req() > 1 && bol->in(1) && 
bol->in(1)->is_LoadStore())
	  return;          // CAS spin-rety loops RARELY take backedge
        // Find the OTHER exit path from the IF
        Node* ex = iff->proj_out(1-test_con);
        float p = iff->_prob;
        if( !phase->is_member( this, ex ) && iff->_fcnt == COUNT_UNKNOWN ) {
          if( top == Op_IfTrue ) {
            if( p < (PROB_FAIR + PROB_UNLIKELY_MAG(3))) {
              iff->_prob = PROB_STATIC_FREQUENT;
            }
          } else {
            if( p > (PROB_FAIR - PROB_UNLIKELY_MAG(3))) {
              iff->_prob = PROB_STATIC_INFREQUENT;
            }
          }
        }
      }
    }
    test = phase->idom(test);
  }
}
  

//------------------------------policy_do_remove_empty_loop--------------------
// Micro-benchmark spamming.  Policy is to always remove empty loops.
// The 'DO' part is to replace the trip counter with the value it will
// have on the last iteration.  This will break the loop.
bool IdealLoopTree::policy_do_remove_empty_loop( PhaseIdealLoop *phase ) {
  // Minimum size must be empty loop
  if( _body.size() > 7/*number of nodes in an empty loop*/ ) return false;

  if( !_head->is_CountedLoop() ) return false;     // Dead loop
  CountedLoopNode *cl = _head->as_CountedLoop();
  if( !cl->loopexit() ) return false; // Malformed loop
  if( !phase->is_member(this,phase->get_ctrl(cl->loopexit()->in(CountedLoopEndNode::TestValue)) ) )
    return false;             // Infinite loop
  if( PrintOpto ) 
C2OUT->print_cr("Removing empty loop");
#ifdef ASSERT
  // Ensure only one phi which is the iv.
  Node* iv = NULL;
  for (DUIterator_Fast imax, i = cl->fast_outs(imax); i < imax; i++) {
    Node* n = cl->fast_out(i);
    if (n->Opcode() == Op_Phi) {
      assert(iv == NULL, "Too many phis" );
      iv = n;
    }
  }
  assert(iv == cl->phi(), "Wrong phi" );
#endif
  // Replace the phi at loop head with the final value of the last
  // iteration.  Then the CountedLoopEnd will collapse (backedge never
  // taken) and all loop-invariant uses of the exit values will be correct.
  Node *phi = cl->phi();
  Node *final = new (phase->C, 3) SubINode( cl->limit(), cl->stride() );
  phase->register_new_node(final,cl->in(LoopNode::EntryControl));
  phase->_igvn.hash_delete(phi);
  phase->_igvn.subsume_node(phi,final);
  phase->C->set_major_progress();
  return true;
}
  

//=============================================================================
//------------------------------iteration_split_impl---------------------------
void IdealLoopTree::iteration_split_impl(PhaseIdealLoop*phase,Node_List&old_new,bool after_cpp){
  // Check and remove empty loops (spam micro-benchmarks)
  if( policy_do_remove_empty_loop(phase) ) 
    return;                     // Here we removed an empty loop

  bool should_peel = policy_peeling(phase); // Should we peel?

  bool should_unswitch = policy_unswitching(phase);

  // Non-counted loops may be peeled; exactly 1 iteration is peeled.
  // This removes loop-invariant tests (usually null checks).
  if( !_head->is_CountedLoop() ) { // Non-counted loop
    if (PartialPeelLoop && phase->partial_peel(this, old_new)) {
      return;
    }
    if( should_peel ) {            // Should we peel?
if(PrintOpto)C2OUT->print_cr("should_peel");
      phase->do_peeling(this,old_new);
    } else if( should_unswitch ) {
      phase->do_unswitching(this, old_new);
    }
    return;
  }
  CountedLoopNode *cl = _head->as_CountedLoop();

  if( !cl->loopexit() ) return; // Ignore various kinds of broken loops

  // Do nothing special to pre- and post- loops
  if( cl->is_pre_loop() || cl->is_post_loop() ) return;

  // Compute loop trip count from profile data
  compute_profile_trip_cnt(phase);
  if ( !policy_do_aggressive_loopopts(phase) ) return;

  // Before attempting fancy unrolling, RCE or alignment, see if we want
  // to completely unroll this loop or do loop unswitching.
  if( cl->is_normal_loop() ) {
    bool should_maximally_unroll =  policy_maximally_unroll(phase);
    if( should_maximally_unroll ) {
      // Here we did some unrolling and peeling.  Eventually we will 
      // completely unroll this loop and it will no longer be a loop.
      phase->do_maximally_unroll(this,old_new);
      return;
    }
    if (should_unswitch) {
      phase->do_unswitching(this, old_new);
      return;
    }
  }

  bool should_unroll = false;
  bool should_prefetch = false;

  int unroll_and_prefetch = policy_unroll_and_prefetch(phase);
  switch( unroll_and_prefetch ) {
  case 0: // No prefetching necessary 
    should_unroll   = policy_unroll_small_loops(phase);
    should_prefetch = false;
    break;
  case 1: // Don't unroll, but insert prefetch
    should_unroll   = false;
    should_prefetch = true;
    break;
  case 2: // Unroll now
    should_unroll   = true;
    should_prefetch = false;
    break;
  default: ShouldNotReachHere();
  }

  // Counted loops may be peeled, may need some iterations run up
  // front for RCE, and may want to align loop refs to a cache
  // line.  Thus we clone a full loop up front whose trip count is
  // at least 1 (if peeling), but may be several more.
        
  // The main loop will start cache-line aligned with at least 1
  // iteration of the unrolled body (zero-trip test required) and
  // will have some range checks removed.
        
  // A post-loop will finish any odd iterations (leftover after
  // unrolling), plus any needed for RCE purposes.

  bool should_rce = policy_range_check(phase);

  bool should_align = policy_align(phase);

  // If not RCE'ing (iteration splitting) or Aligning, then we do not
  // need a pre-loop.  We may still need to peel an initial iteration but
  // we will not be needing an unknown number of pre-iterations.
  //
  // Basically, if may_rce_align reports FALSE first time through, 
  // we will not be able to later do RCE or Aligning on this loop.
  bool may_rce_align = !policy_peel_only(phase) || should_rce || should_align;

  // If we have any of these conditions (RCE, alignment, unrolling) met, then
  // we switch to the pre-/main-/post-loop model.  This model also covers
  // peeling.
  if( should_rce || should_align || should_unroll ) {
    if( cl->is_normal_loop() )  // Convert to 'pre/main/post' loops
      phase->insert_pre_post_loops(this,old_new, !may_rce_align);

    // Adjust the pre- and main-loop limits to let the pre and post loops run
    // with full checks, but the main-loop with no checks.  Remove said
    // checks from the main body.
    if( should_rce ) 
      phase->do_range_check(this,old_new);

    // Double loop body for unrolling.  Adjust the minimum-trip test (will do
    // twice as many iterations as before) and the main body limit (only do
    // an even number of trips).  If we are peeling, we might enable some RCE
    // and we'd rather unroll the post-RCE'd loop SO... do not unroll if
    // peeling.
    if( should_unroll && !should_peel ) 
      phase->do_unroll(this,old_new, true);

    // Adjust the pre-loop limits to align the main body
    // iterations.
    if( should_align )
      Unimplemented();

  } else {                      // Else we have an unchanged counted loop
if(should_peel){//Might want to peel but do nothing else
      phase->do_peeling(this,old_new);
    } else if (should_prefetch) { 
do_prefetch(phase);
    }

    if( cl->is_main_loop() && after_cpp && !phase->C->major_progress() ) {
      // Here I just discovered a main loop that isn't changing.  I am after
      // CCP (which is the last major chance to remove null checks which are
      // preventing hoisting of loop-invariant LoadRange's, which enable
      // range-check elimination).  If there are pre and post loops about, try
      // to max-unroll them.  First, find my pre-loop
      if( !cl->is_main_no_pre_loop() && _parent->_child != this ) {
	IdealLoopTree *pre = _parent->_child;
	while( pre->_next != this ) pre = pre->_next;
Node*prehead=pre->_head;
	if( prehead->is_CountedLoop() && prehead->as_CountedLoop()->is_pre_loop() ) {
CountedLoopNode*pre_cl=prehead->as_CountedLoop();
	  Node *cmp = pre_cl->loopexit()->cmp_node();
	  Node *lim = cmp->in(2); // Remove any Opaque1 on the pre-loop
if(lim->Opcode()==Op_Opaque1){
phase->_igvn.hash_delete(cmp);
phase->_igvn._worklist.push(cmp);
cmp->set_req(2,lim->in(1));
	  }
          pre_cl->set_normal_loop(); // Pre-loop is normal now; profiling should claim low low trip count 
          cl->set_main_no_pre_loop(); // Main-loop has no pre-loop
          bool should_maximally_unroll =  pre->policy_maximally_unroll(phase);
	  if( should_maximally_unroll ) {
	    phase->do_maximally_unroll(pre,old_new);
          }
	}
      } 
      IdealLoopTree *post = _next;
      if( post ) {
Node*post_cl=post->_head;
	if( post_cl->is_CountedLoop() && post_cl->as_CountedLoop()->is_post_loop() ) {
	  Node *cmp = post_cl->as_CountedLoop()->loopexit()->cmp_node();
	  Node *lim = cmp->in(2); // Remove any Opaque1 on the post-loop
if(lim->Opcode()==Op_Opaque1){
phase->_igvn.hash_delete(cmp);
phase->_igvn._worklist.push(cmp);
cmp->set_req(2,lim->in(1));
	  }
          bool should_maximally_unroll =  post->policy_maximally_unroll(phase);
	  if( should_maximally_unroll ) {
	    phase->do_maximally_unroll(post,old_new);
          }
	}
      }
      // Normally I would declare maximally-unrolling a loop "major progress",
      // but in this case the first loop that gets this optimization prevents
      // all the rest.  I.e., in a high loop-count method, if I remove the
      // pre-loop on the first nest, I have to redo the entire loop opts
      // before attempting to remove the pre-loop on the next loop - causing
      // many loop-opts trips.
phase->C->clear_major_progress();
    }
  }
}


//=============================================================================
//------------------------------iteration_split--------------------------------
void IdealLoopTree::iteration_split(PhaseIdealLoop*phase,Node_List&old_new,bool after_ccp){
  // Recursively iteration split nested loops
if(_child)_child->iteration_split(phase,old_new,after_ccp);

  // Clean out prior deadwood
  DCE_loop_body();


  // Look for loop-exit tests with my 50/50 guesses from the Parsing stage.
  // Replace with a 1-in-10 exit guess.
  if( _parent /*not the root loop*/ && 
      !_irreducible && 
      // Also ignore the occasional dead backedge
      !tail()->is_top() ) {
    adjust_loop_exit_prob(phase);
  }


  // Gate unrolling, RCE and peeling efforts.
  if( !_child &&                // If not an inner loop, do not split
      !_irreducible &&
      !tail()->is_top() ) {     // Also ignore the occasional dead backedge
    if (!_has_call) {
iteration_split_impl(phase,old_new,after_ccp);
    } else if (policy_unswitching(phase)) {
      phase->do_unswitching(this, old_new);
    }

    // Look for repeated ConvI2Ls with slightly different offsets.  Force all
    // to be the same, so they common up. 
uint lim=_body.size();
const Type*t=NULL;
for(uint i=0;i<lim;i++){
Node*x=_body[i];
if(x->Opcode()==Op_ConvI2L){
        const Type *t2 = ((ConvI2LNode*)x)->type();
        t = t ? t->meet( t2 ) : t2;
      }
    }
for(uint i=0;i<lim;i++){
Node*x=_body[i];
if(x->Opcode()==Op_ConvI2L){
phase->_igvn.hash_delete(x);
        if( ((ConvI2LNode*)x)->type() != t ) {
          ((ConvI2LNode*)x)->set_type(t);
phase->_igvn._worklist.push(x);
        }
      }
    }
  }

  // Minor offset re-organization to remove loop-fallout uses of 
  // trip counter.
  if( _head->is_CountedLoop() ) phase->reorg_offsets( this );
if(_next)_next->iteration_split(phase,old_new,after_ccp);
}
