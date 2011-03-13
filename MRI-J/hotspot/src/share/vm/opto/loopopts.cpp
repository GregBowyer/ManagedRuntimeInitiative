/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "callnode.hpp"
#include "connode.hpp"
#include "loopnode.hpp"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "rootnode.hpp"
#include "subnode.hpp"

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

//=============================================================================
//------------------------------split_thru_phi---------------------------------
// Split Node 'n' through merge point if there is enough win.
Node*PhaseIdealLoop::split_thru_phi(Node*n,Node*region,int policy,bool is_split_if){
  int wins = 0;
  assert( !n->is_CFG(), "" );
  assert( region->is_Region(), "" );
  Node *phi = new (C, region->req()) PhiNode( region, n->bottom_type(), n->bottom_type()==Type::MEMORY ? n->adr_type() : NULL);
  uint old_unique = C->unique();
  for( uint i = 1; i < region->req(); i++ ) {
    Node *x;
    Node* the_clone = NULL;
    if( region->in(i) == C->top() ) {
      x = C->top();             // Dead path?  Use a dead data op
    } else {
      x = n->clone();           // Else clone up the data op
      _igvn.set_type(x,_igvn.type(n));
      the_clone = x;            // Remember for possible deletion.
      // Alter data node to use pre-phi inputs
      if( n->in(0) == region )
        x->set_req( 0, region->in(i) );
      for( uint j = 1; j < n->req(); j++ ) {
        Node *in = n->in(j);
        if( in->is_Phi() && in->in(0) == region )
          x->set_req( j, in->in(i) ); // Use pre-Phi input for the clone
      }
    }
    // Check for a 'win' on some paths
    const Type *t = x->Value(&_igvn);

    bool singleton = t->singleton();

    // A TOP singleton indicates that there are no possible values incoming
    // along a particular edge or the value must be unused if we came to the
    // Region along that edge.  In most cases, this is OK, and the Phi will 
    // be eliminated later in an Ideal call. However, we can't allow this to
    // happen if the singleton occurs on loop entry, as the elimination of
    // the PhiNode may cause the resulting node to migrate back to a previous
    // loop iteration.
    if( singleton && t == Type::TOP ) {
      // Is_Loop() == false does not confirm the absence of a loop (e.g., an
      // irreducible loop may not be indicated by an affirmative is_Loop());
      // therefore, the only top we can split thru a phi is on a backedge of
      // a loop.
      singleton &= region->is_Loop() && (i != LoopNode::EntryControl);
    }

    if( singleton ) {
      wins++;
      x = ((PhaseGVN&)_igvn).makecon(t);
    } else {
      // Special hack for back-to-back stores to the same address, only seen
      // after cloning through phi's.  Cleans up all the exception exit paths.
if(x->is_Store()&&
	  x->in(MemNode::Memory)->Opcode() == x->Opcode() &&
	  x->in(MemNode::Address) == x->in(MemNode::Memory)->in(MemNode::Address) &&
	  x->in(MemNode::Memory)->outcnt() == 2/*node x plus the original user*/ ) {
	x->set_req( MemNode::Memory, x->in(MemNode::Memory)->in(MemNode::Memory) );
        wins++;
        _igvn._worklist.push(x);
      }
      // Common (address-expressions) situation involving multiple constant adds
if(x->is_Add()){
Node*y=x->in(1);
        if( x->in(2)->is_Con() && y->is_Add() &&
            y->in(2)->is_Con() ) {
          wins++;
          _igvn._worklist.push(x);
        }
      }
if(x->is_AddP()){
Node*y=x->in(AddPNode::Address);
        if( x->in(AddPNode::Address+1)->is_Con() && y->is_AddP() &&
            y->in(AddPNode::Address+1)->is_Con() ) {
          wins++;
          _igvn._worklist.push(x);
        }
      }
      // We now call Identity to try to simplify the cloned node.
      // Note that some Identity methods call phase->type(this).
      // Make sure that the type array is big enough for
      // our new node, even though we may throw the node away.
      // (Note: This tweaking with igvn only works because x is a new node.)
      _igvn.set_type(x, t);
      // If x is a TypeNode, capture any more-precise type permanently into Node
      // otherwise it will be not updated during igvn->transform since
      // igvn->type(x) is set to x->Value() already.
x->raise_bottom_type(t);
      Node *y = x->Identity(&_igvn);
      if( y != x ) {
        wins++;
        x = y;
      } else {
        y = _igvn.hash_find(x);
        if( y && !(region->is_Loop() && i==LoopNode::LoopBackControl ) ) {
          if( !is_split_if ) wins++;
          x = y;
        } else {
          // Else x is a new node we are keeping.
          // We do not need register_new_node_with_optimizer
          // because set_type has already been called.
          _igvn._worklist.push(x);
        }
      }
    }
    if (x != the_clone && the_clone != NULL)
      _igvn.remove_dead_node(the_clone);
    phi->set_req( i, x );
  }
  // Too few wins?
  if( wins <= policy ) {
    _igvn.remove_dead_node(phi);
    return NULL;
  }

  // Record Phi
  register_new_node( phi, region );

  for( uint i2 = 1; i2 < phi->req(); i2++ ) {
    Node *x = phi->in(i2);
    // If we commoned up the cloned 'x' with another existing Node,
    // the existing Node picks up a new use.  We need to make the
    // existing Node occur higher up so it dominates its uses.
    Node *old_ctrl;
    IdealLoopTree *old_loop;

    // The occasional new node
    if( x->_idx >= old_unique ) {   // Found a new, unplaced node?
      old_ctrl = x->is_Con() ? C->root() : NULL;
      old_loop = NULL;              // Not in any prior loop
    } else {
      old_ctrl = x->is_Con() ? C->root() : get_ctrl(x);
      old_loop = get_loop(old_ctrl); // Get prior loop
    }
    // New late point must dominate new use
    Node *new_ctrl = dom_lca( old_ctrl, region->in(i2) );
    // Set new location
    set_ctrl(x, new_ctrl);
    IdealLoopTree *new_loop = get_loop( new_ctrl );
    // If changing loop bodies, see if we need to collect into new body
    if( old_loop != new_loop ) {
      if( old_loop && !old_loop->_child )
        old_loop->_body.yank(x);
      if( !new_loop->_child )
        new_loop->_body.push(x);  // Collect body info
    }
  }

  return phi;
}

//------------------------------dominated_by------------------------------------
// Replace the dominated test with an obvious true or false.  Place it on the
// IGVN worklist for later cleanup.  Move control-dependent data Nodes on the
// live path up to the dominating control.
void PhaseIdealLoop::dominated_by( Node *prevdom, Node *iff ) {
if(PrintOpto){
C2OUT->print("dominating test ");
iff->dump();
  }


  // prevdom is the dominating projection of the dominating test.
  assert( iff->is_If(), "" );
  assert( iff->Opcode() == Op_If || iff->Opcode() == Op_CountedLoopEnd, "Check this code when new subtype is added");
  int pop = prevdom->Opcode();
  assert( pop == Op_IfFalse || pop == Op_IfTrue, "" );
  // 'con' is set to true or false to kill the dominated test.
  Node *con = _igvn.makecon(pop == Op_IfTrue ? TypeInt::ONE : TypeInt::ZERO);
  set_ctrl(con, C->root()); // Constant gets a new use
  // Hack the dominated test
  _igvn.hash_delete(iff);
  iff->set_req(1, con);
  _igvn._worklist.push(iff);

  // If I dont have a reachable TRUE and FALSE path following the IfNode then
  // I can assume this path reaches an infinite loop.  In this case it's not
  // important to optimize the data Nodes - either the whole compilation will
  // be tossed or this path (and all data Nodes) will go dead.
  if( iff->outcnt() != 2 ) return;

  // Make control-dependent data Nodes on the live path (path that will remain
  // once the dominated IF is removed) become control-dependent on the
  // dominating projection.
  Node* dp = ((IfNode*)iff)->proj_out(pop == Op_IfTrue);
  IdealLoopTree *old_loop = get_loop(dp);

  for (DUIterator_Fast imax, i = dp->fast_outs(imax); i < imax; i++) {
    Node* cd = dp->fast_out(i); // Control-dependent node
    if( cd->depends_only_on_test() ) {
      assert( cd->in(0) == dp, "" );
      _igvn.hash_delete( cd );
      cd->set_req(0, prevdom);
      set_early_ctrl( cd );
      _igvn._worklist.push(cd);
      IdealLoopTree *new_loop = get_loop(get_ctrl(cd));
      if( old_loop != new_loop ) {
        if( !old_loop->_child ) old_loop->_body.yank(cd);
        if( !new_loop->_child ) new_loop->_body.push(cd);
      }
      --i;
      --imax;
    }
  }
}

//------------------------------has_local_phi_input----------------------------
// Return TRUE if 'n' has Phi inputs from its local block and no other
// block-local inputs (all non-local-phi inputs come from earlier blocks)
Node *PhaseIdealLoop::has_local_phi_input( Node *n ) {
  Node *n_ctrl = get_ctrl(n);
  // See if some inputs come from a Phi in this block, or from before
  // this block.
  uint i;
  for( i = 1; i < n->req(); i++ ) {
    Node *phi = n->in(i);
    if( phi->is_Phi() && phi->in(0) == n_ctrl )
      break;
  }
  if( i >= n->req() )
    return NULL;                // No Phi inputs; nowhere to clone thru

  // Check for inputs created between 'n' and the Phi input.  These
  // must split as well; they have already been given the chance
  // (courtesy of a post-order visit) and since they did not we must
  // recover the 'cost' of splitting them by being very profitable
  // when splitting 'n'.  Since this is unlikely we simply give up.
  for( i = 1; i < n->req(); i++ ) {
    Node *m = n->in(i);
    if( get_ctrl(m) == n_ctrl && !m->is_Phi() ) {
      // We allow the special case of AddP's with no local inputs.
      // This allows us to split-up address expressions.
      if (m->is_AddP() &&
          get_ctrl(m->in(2)) != n_ctrl &&
          get_ctrl(m->in(3)) != n_ctrl) {
        // Move the AddP up to dominating point
        set_ctrl_and_loop(m, find_non_split_ctrl(idom(n_ctrl)));
        continue;
      }
      return NULL;
    }
  }

  return n_ctrl;
}

//------------------------------remix_address_expressions----------------------
// Rework addressing expressions to get the most loop-invariant stuff
// moved out.  We'd like to do all associative operators, but it's especially
// important (common) to do address expressions.
Node *PhaseIdealLoop::remix_address_expressions( Node *n ) {
  if (!has_ctrl(n))  return NULL;
  Node *n_ctrl = get_ctrl(n);
  IdealLoopTree *n_loop = get_loop(n_ctrl);

  // See if 'n' mixes loop-varying and loop-invariant inputs and
  // itself is loop-varying.

  // Only interested in binary ops (and AddP)
  if( n->req() < 3 || n->req() > 4 ) return NULL;

  Node *n1_ctrl = get_ctrl(n->in(                    1));
  Node *n2_ctrl = get_ctrl(n->in(                    2));
  Node *n3_ctrl = get_ctrl(n->in(n->req() == 3 ? 2 : 3));
  IdealLoopTree *n1_loop = get_loop( n1_ctrl );
  IdealLoopTree *n2_loop = get_loop( n2_ctrl );
  IdealLoopTree *n3_loop = get_loop( n3_ctrl );

  // Does one of my inputs spin in a tighter loop than self?
  if( (n_loop->is_member( n1_loop ) && n_loop != n1_loop) ||
      (n_loop->is_member( n2_loop ) && n_loop != n2_loop) ||
      (n_loop->is_member( n3_loop ) && n_loop != n3_loop) )
    return NULL;                // Leave well enough alone

  // Is at least one of my inputs loop-invariant?
  if( n1_loop == n_loop &&
      n2_loop == n_loop &&
      n3_loop == n_loop )
    return NULL;                // No loop-invariant inputs


  int n_op = n->Opcode();

  // Replace expressions like ((V+I) << 2) with (V<<2 + I<<2).
  if( n_op == Op_LShiftI ) {
    // Scale is loop invariant
    Node *scale = n->in(2);
    Node *scale_ctrl = get_ctrl(scale);
    IdealLoopTree *scale_loop = get_loop(scale_ctrl );
    if( n_loop == scale_loop || !scale_loop->is_member( n_loop ) )
      return NULL;
    const TypeInt *scale_t = scale->bottom_type()->isa_int();
    if( scale_t && scale_t->is_con() && scale_t->get_con() >= 16 )
      return NULL;              // Dont bother with byte/short masking
    // Add must vary with loop (else shift would be loop-invariant)
    Node *add = n->in(1);
    Node *add_ctrl = get_ctrl(add);
    IdealLoopTree *add_loop = get_loop(add_ctrl);
    //assert( n_loop == add_loop, "" );
    if( n_loop != add_loop ) return NULL;  // happens w/ evil ZKM loops

    // Convert I-V into I+ (0-V); same for V-I
    if( add->Opcode() == Op_SubI &&
        _igvn.type( add->in(1) ) != TypeInt::ZERO ) {
      Node *zero = _igvn.intcon(0);
      set_ctrl(zero, C->root());
      Node *neg = new (C, 3) SubINode( _igvn.intcon(0), add->in(2) );
      register_new_node( neg, get_ctrl(add->in(2) ) );
      add = new (C, 3) AddINode( add->in(1), neg );
      register_new_node( add, add_ctrl );
    }
    if( add->Opcode() != Op_AddI ) return NULL;
    // See if one add input is loop invariant
    Node *add_var = add->in(1);
    Node *add_var_ctrl = get_ctrl(add_var);
    IdealLoopTree *add_var_loop = get_loop(add_var_ctrl );
    Node *add_invar = add->in(2);
    Node *add_invar_ctrl = get_ctrl(add_invar);
    IdealLoopTree *add_invar_loop = get_loop(add_invar_ctrl );
    if( add_var_loop == n_loop ) {
    } else if( add_invar_loop == n_loop ) {
      // Swap to find the invariant part
      add_invar = add_var;
      add_invar_ctrl = add_var_ctrl;
      add_invar_loop = add_var_loop;
      add_var = add->in(2);
      Node *add_var_ctrl = get_ctrl(add_var);
      IdealLoopTree *add_var_loop = get_loop(add_var_ctrl );
    } else                      // Else neither input is loop invariant
      return NULL;
    if( n_loop == add_invar_loop || !add_invar_loop->is_member( n_loop ) )
      return NULL;              // No invariant part of the add?

    // Yes!  Reshape address expression!
    Node *inv_scale = new (C, 3) LShiftINode( add_invar, scale );
    register_new_node( inv_scale, add_invar_ctrl );
    Node *var_scale = new (C, 3) LShiftINode( add_var, scale );
    register_new_node( var_scale, n_ctrl );
    Node *var_add = new (C, 3) AddINode( var_scale, inv_scale );
    register_new_node( var_add, n_ctrl );
    _igvn.hash_delete( n );
    _igvn.subsume_node( n, var_add );
    return var_add;
  }

  // Replace (I+V) with (V+I)
  if( n_op == Op_AddI ||
      n_op == Op_AddL ||
      n_op == Op_AddF ||
      n_op == Op_AddD ||
      n_op == Op_MulI ||
      n_op == Op_MulL ||
      n_op == Op_MulF ||
      n_op == Op_MulD ) {
    if( n2_loop == n_loop ) {
      assert( n1_loop != n_loop, "" );
      n->swap_edges(1, 2);
    }
  }

  // Replace ((I1 +p V) +p I2) with ((I1 +p I2) +p V),
  // but not if I2 is a constant.
  if( n_op == Op_AddP ) {
    if( n2_loop == n_loop && n3_loop != n_loop ) {
      if( n->in(2)->Opcode() == Op_AddP && !n->in(3)->is_Con() ) {
        Node *n22_ctrl = get_ctrl(n->in(2)->in(2));
        Node *n23_ctrl = get_ctrl(n->in(2)->in(3));
        IdealLoopTree *n22loop = get_loop( n22_ctrl );
        IdealLoopTree *n23_loop = get_loop( n23_ctrl );
        if( n22loop != n_loop && n22loop->is_member(n_loop) &&
            n23_loop == n_loop ) {
          Node *add1 = new (C, 4) AddPNode( n->in(1), n->in(2)->in(2), n->in(3) );
          // Stuff new AddP in the loop preheader
          register_new_node( add1, get_early_ctrl(add1, true) );
          Node *add2 = new (C, 4) AddPNode( n->in(1), add1, n->in(2)->in(3) );
          register_new_node( add2, n_ctrl );
          _igvn.hash_delete( n );
          _igvn.subsume_node( n, add2 );
          return add2;
        }
      }
    }

    // Replace (I1 +p (I2 + V)) with ((I1 +p I2) +p V)
    if( n2_loop != n_loop && n3_loop == n_loop ) {
      if( n->in(3)->Opcode() == Op_AddI ) {
        Node *V = n->in(3)->in(1);
        Node *I = n->in(3)->in(2);
        if( is_member(n_loop,get_ctrl(V)) ) {
        } else {
          Node *tmp = V; V = I; I = tmp;
        }
        if( !is_member(n_loop,get_ctrl(I)) ) {
          Node *add1 = new (C, 4) AddPNode( n->in(1), n->in(2), I );
          // Stuff new AddP in the loop preheader
          register_new_node( add1, n_loop->_head->in(LoopNode::EntryControl) );
          Node *add2 = new (C, 4) AddPNode( n->in(1), add1, V );
          register_new_node( add2, n_ctrl );
          _igvn.hash_delete( n );
          _igvn.subsume_node( n, add2 );
          return add2;
        }
      }
    }
  }

  return NULL;
}

//------------------------------conditional_move-------------------------------
// Attempt to replace a Phi with a conditional move.  We have some pretty
// strict profitability requirements.  All Phis at the merge point must
// be converted, so we can remove the control flow.  We need to limit the
// number of c-moves to a small handful.  All code that was in the side-arms
// of the CFG diamond is now speculatively executed.  This code has to be
// "cheap enough".  We are pretty much limited to CFG diamonds that merge
// 1 or 2 items with a total of 1 or 2 ops executed speculatively.
Node *PhaseIdealLoop::conditional_move( Node *region ) {

  assert( region->is_Region(), "sanity check" );
  if( region->req() != 3 ) return NULL;
  // If the region is dead, bail out now
  if( _igvn.type(region)==Type::TOP ) return NULL;

  // Check for CFG diamond
  Node *lp = region->in(1);
  Node *rp = region->in(2);
  if( !lp || !rp ) return NULL;
  Node *lp_c = lp->in(0);
  if( lp_c == NULL || lp_c != rp->in(0) || !lp_c->is_If() ) return NULL;
  IfNode *iff = lp_c->as_If();

  // Check for highly predictable branch.  No point in CMOV'ing if
  // we are going to predict accurately all the time.
  // %%% This hides patterns produced by utility methods like Math.min.
  if( iff->_prob < PROB_UNLIKELY_MAG(3) ||
      iff->_prob > PROB_LIKELY_MAG(3) )
    return NULL;

  // Check for ops pinned in an arm of the diamond.
  // Can't remove the control flow in this case
  if( lp->outcnt() > 1 ) return NULL;
  if( rp->outcnt() > 1 ) return NULL;

  // Check profitability
  int cost = 0;
  for (DUIterator_Fast imax, i = region->fast_outs(imax); i < imax; i++) {
    Node *out = region->fast_out(i);
    if( !out->is_Phi() ) continue; // Ignore other control edges, etc
    PhiNode* phi = out->as_Phi();
    if( _igvn.type(phi)->empty() ) return NULL; // Bail out if any phi is dead
    switch (phi->type()->basic_type()) {
    case T_LONG:
      cost++;                   // Probably encodes as 2 CMOV's
    case T_INT:                 // These all CMOV fine
    case T_FLOAT:
    case T_DOUBLE:
    case T_ADDRESS:             // (RawPtr)
      cost++;
      break;
    case T_OBJECT: {            // Base oops are OK, but not derived oops
      const TypeOopPtr *tp = phi->type()->isa_oopptr();
      // Derived pointers are Bad (tm): what's the Base (for GC purposes) of a
      // CMOVE'd derived pointer?  It's a CMOVE'd derived base.  Thus
      // CMOVE'ing a derived pointer requires we also CMOVE the base.  If we
      // have a Phi for the base here that we convert to a CMOVE all is well
      // and good.  But if the base is dead, we'll not make a CMOVE.  Later
      // the allocator will have to produce a base by creating a CMOVE of the
      // relevant bases.  This puts the allocator in the business of
      // manufacturing expensive instructions, generally a bad plan.
      // Just Say No to Conditionally-Moved Derived Pointers.
      if( tp && tp->offset() != 0 )
        return NULL;
      cost++;
      break;
    }
    default:
      return NULL;              // In particular, can't do memory or I/O
    }
    // Add in cost any speculative ops
    for( uint j = 1; j < region->req(); j++ ) {
      Node *proj = region->in(j);
      Node *inp = phi->in(j);
      if (get_ctrl(inp) == proj) { // Found local op
        cost++;
        // Check for a chain of dependent ops; these will all become
        // speculative in a CMOV.
        for( uint k = 1; k < inp->req(); k++ )
          if (get_ctrl(inp->in(k)) == proj)
            return NULL;        // Too much speculative goo
      }
    }
    // See if the Phi is used by a Cmp.  This will likely Split-If, a
    // higher-payoff operation.
    for (DUIterator_Fast kmax, k = phi->fast_outs(kmax); k < kmax; k++) {
      Node* use = phi->fast_out(k);
      if( use->is_Cmp() )
        return NULL;
    }
  }
  if( cost >= ConditionalMoveLimit ) return NULL; // Too much goo

  // --------------
  // Now replace all Phis with CMOV's
  Node *cmov_ctrl = iff->in(0);
  uint flip = (lp->Opcode() == Op_IfTrue);
  while( 1 ) {
    PhiNode* phi = NULL;
    for (DUIterator_Fast imax, i = region->fast_outs(imax); i < imax; i++) {
      Node *out = region->fast_out(i);
      if (out->is_Phi()) {
        phi = out->as_Phi();
        break;
      }
    }
    if (phi == NULL)  break;
if(PrintOpto){
C2OUT->print("CMOV ");
region->dump();
    }
    // Move speculative ops
    for( uint j = 1; j < region->req(); j++ ) {
      Node *proj = region->in(j);
      Node *inp = phi->in(j);
      if (get_ctrl(inp) == proj) { // Found local op
if(PrintOpto){
C2OUT->print("  speculate: ");
          inp->dump();
        }
        set_ctrl(inp, cmov_ctrl);
      }
    }
Node*cmov=CMoveNode::make(C,region,iff->in(1),phi->in(1+flip),phi->in(2-flip),_igvn.type(phi));
register_new_node(cmov,region);
    _igvn.hash_delete(phi);
    _igvn.subsume_node( phi, cmov );
#ifndef PRODUCT
    if( VerifyLoopOptimizations ) verify();
#endif
  }

  // The useless CFG diamond will fold up later; see the optimization in
  // RegionNode::Ideal.
  _igvn._worklist.push(region);

  return iff->in(1);
}

//------------------------------split_if_with_blocks_pre-----------------------
// Do the real work in a non-recursive function.  Data nodes want to be
// cloned in the pre-order so they can feed each other nicely.
Node *PhaseIdealLoop::split_if_with_blocks_pre( Node *n ) {
  // Cloning these guys is unlikely to win
  int n_op = n->Opcode();
  if( n_op == Op_MergeMem ) return n;
  if( n->is_Proj() ) return n;
  // Do not clone-up CmpFXXX variations, as these are always
  // followed by a CmpI
  if( n->is_Cmp() ) return n;
  // Attempt to use a conditional move instead of a phi/branch
  if( ConditionalMoveLimit > 0 && n_op == Op_Region ) {
    Node *cmov = conditional_move( n );
    if( cmov ) return cmov;
  }
  if( _igvn.type(n) == Type::TOP ) return n; // Sometimes dead stuff makes it here
  if( n->is_CFG() || n->is_LoadStore() )  return n;
  if( n_op == Op_Opaque1 ||     // Opaque nodes cannot be mod'd
      n_op == Op_Opaque2 ) {
    if( !C->major_progress() )   // If chance of no more loop opts...
      _igvn._worklist.push(n);  // maybe we'll remove them
    return n;
  }

  if( n->is_Con() ) return n;   // No cloning for Con nodes

  Node *n_ctrl = get_ctrl(n);
  if( !n_ctrl ) return n;       // Dead node

  // Attempt to remix address expressions for loop invariants
  Node *m = remix_address_expressions( n );
  if( m ) return m;

  // Determine if the Node has inputs from some local Phi.
  // Returns the block to clone thru.
  Node *n_blk = has_local_phi_input( n );
  if( !n_blk ) return n;
  // Do not clone the trip counter through on a CountedLoop
  // (messes up the canonical shape).
  if( n_blk->is_CountedLoop() ) {
    if( n_op == Op_AddI ||	// Possible trip counter
	n_op == Op_ConvI2L || 	// Possible index expression
	n_op == Op_LShiftI )	// Possible index expression
      return n;
  }

  // Check for having no control input; not pinned.  Allow
  // dominating control.
if(n->is_Phi())return n;
#ifdef ASSERT
  if( n->in(0) &&
n->in(0)!=n_blk&&
      dom_lca(n->in(0),idom(n_blk)) != n->in(0) ) {
C2OUT->print_cr("=== Funny Control ===");
    C2OUT->print   ("=== idom(n_blk)  : "); idom(n_blk)  ->dump();
C2OUT->print("===      n_blk   : ");n_blk->dump();
    C2OUT->print   ("===      n->in(0): ");      n->in(0)->dump();
C2OUT->print("===      n       : ");n->dump();
  }  
#endif
  assert( !n->in(0) ||
	  n->in(0) == n_blk ||
	  dom_lca(n->in(0),idom(n_blk)) == n->in(0), "bad control" );

  // Policy: when is it profitable.  You must get more wins than
  // policy before it is considered profitable.  Policy is usually 0,
  // so 1 win is considered profitable.  Big merges will require big
  // cloning, so get a larger policy.
  int policy = n_blk->req() >> 2;

  // If the loop is a candidate for range check elimination,
  // delay splitting through it's phi until a later loop optimization
  if (n_blk->is_CountedLoop()) {
    IdealLoopTree *lp = get_loop(n_blk);
    if (lp && lp->_rce_candidate) {
      return n;
    }
  }

  // Use same limit as split_if_with_blocks_post
  if( C->unique() > 35000 ) return n; // Method too big

  // Split 'n' through the merge point if it is profitable
Node*phi=split_thru_phi(n,n_blk,policy,false);
  if( !phi ) return n;

  // Found a Phi to split thru!
  // Replace 'n' with the new phi
  _igvn.hash_delete(n);
  _igvn.subsume_node( n, phi );
  // Moved a load around the loop, 'en-registering' something.
  if( n_blk->Opcode() == Op_Loop && n->is_Load() &&
      !phi->in(LoopNode::LoopBackControl)->is_Load() )
    C->set_major_progress();

  return phi;
}

static bool merge_point_too_heavy(Compile* C, Node* region) {
  // Bail out if the region and its phis have too many users.
  int weight = 0;
  for (DUIterator_Fast imax, i = region->fast_outs(imax); i < imax; i++) {
    weight += region->fast_out(i)->outcnt();
  }
  int nodes_left = MaxNodeLimit - C->unique();
  if (weight * 8 > nodes_left) {
    if (PrintOpto)
C2OUT->print_cr("*** Split-if bails out:  %d nodes, region weight %d",C->unique(),weight);
    return true;
  } else {
    return false;
  }
}

static bool merge_point_safe(Node* region) {
  // 4799512: Stop split_if_with_blocks from splitting a block with a ConvI2LNode
  // having a PhiNode input. This sidesteps the dangerous case where the split
  // ConvI2LNode may become TOP if the input Value() does not
  // overlap the ConvI2L range, leaving a node which may not dominate its
  // uses.
  // A better fix for this problem can be found in the BugTraq entry, but
  // expediency for Mantis demands this hack.
  for (DUIterator_Fast imax, i = region->fast_outs(imax); i < imax; i++) {
    Node* n = region->fast_out(i);
    if (n->is_Phi()) {
      for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
        Node* m = n->fast_out(j);
        if (m->Opcode() == Op_ConvI2L) {
          return false;
        }
      }
    }
  }
  return true;
}


//------------------------------place_near_use---------------------------------
// Place some computation next to use but not inside inner loops.
// For inner loop uses move it to the preheader area.
Node *PhaseIdealLoop::place_near_use( Node *useblock ) const {
  IdealLoopTree *u_loop = get_loop( useblock );
  return (u_loop->_irreducible || u_loop->_child)
    ? useblock
    : u_loop->_head->in(LoopNode::EntryControl);
}


//------------------------------split_if_with_blocks_post----------------------
// Do the real work in a non-recursive function.  CFG hackery wants to be
// in the post-order, so it can dirty the I-DOM info and not use the dirtied
// info.
void PhaseIdealLoop::split_if_with_blocks_post( Node *n ) {

  // Cloning Cmp through Phi's involves the split-if transform.
  assert0( !n->is_FastLock() && !n->is_FastUnlock() );
if(n->is_Cmp()){
    if( C->unique() > 35000 ) return; // Method too big

    // Do not do 'split-if' if irreducible loops are present.
    if( _has_irreducible_loops )
      return;

    Node *n_ctrl = get_ctrl(n);
    // Determine if the Node has inputs from some local Phi.
    // Returns the block to clone thru.
    Node *n_blk = has_local_phi_input( n );
    if( n_blk != n_ctrl ) return;

    if( merge_point_too_heavy(C, n_ctrl) )
      return;

    if( n->outcnt() != 1 ) return; // Multiple bool's from 1 compare?
    Node *bol = n->unique_out();
    assert( bol->is_Bool(), "expect a bool here" );
    if( bol->outcnt() != 1 ) return;// Multiple branches from 1 compare?
    Node *iff = bol->unique_out();

    // Check some safety conditions
    if( iff->is_If() ) {        // Classic split-if?
      if( iff->in(0) != n_ctrl ) return; // Compare must be in same blk as if
    } else if (iff->is_CMove()) { // Trying to split-up a CMOVE
      if( get_ctrl(iff->in(2)) == n_ctrl ||
          get_ctrl(iff->in(3)) == n_ctrl )
        return;                 // Inputs not yet split-up
      if ( get_loop(n_ctrl) != get_loop(get_ctrl(iff)) ) {
	return;                 // Loop-invar test gates loop-varying CMOVE
      }
    } else {
      return;  // some other kind of node, such as an Allocate
    }

    if( n_ctrl->req() <= 2 ) return; // Single input Regions will fold away soon
    // Do not do 'split-if' if some paths are dead.  First do dead code
    // elimination and then see if its still profitable.
    for( uint i = 1; i < n_ctrl->req(); i++ )
      if( n_ctrl->in(i) == C->top() )
        return;

    // When is split-if profitable?  Every 'win' on means some control flow
    // goes dead, so it's almost always a win.
    int policy = 0;
    // If trying to do a 'Split-If' at the loop head, it is only
    // profitable if the cmp folds up on BOTH paths.  Otherwise we
    // risk peeling a loop forever - or we can convert a normal Loop into a CountedLoop.
    IdealLoopTree *n_loop = get_loop(n_ctrl);

    // CNC - Disabled, unless it makes a counted-loop.  Requires careful
    // handling of loop body selection for the cloned code.  Also, make sure
    // we check for any input path not being in the same loop as n_ctrl.  For
    // irreducible loops we cannot check for 'n_ctrl->is_Loop()' because the
    // alternative loop entry points won't be converted into LoopNodes.
    bool split_at_loop_head = false;
    for( uint j = 1; j < n_ctrl->req(); j++ )
      if( get_loop(n_ctrl->in(j)) != n_loop )
        split_at_loop_head = true;
    
    // Check for safety of the merge point.
    //if( !merge_point_safe(n_ctrl) ) {
    //  return;
    //}

    // Only allow if we can make a CountedLoop where there wasn't one before
    if( split_at_loop_head ) {
      // A Loop but not already a Counted Loop?
      if( n_ctrl->is_Loop() && !n_ctrl->is_CountedLoop() &&  
          n->Opcode() == Op_CmpI && // with an integer guard at loop head?
          iff->outcnt() == 2 ) { // well-formed if test?
        // See that the loop tail is mis-formed for making a CountedLoop - it
        // ends, e.g., in a Call or Region or Safepoint, or else it ends in a
        // test which is not an integer guard.
Node*back=n_ctrl->in(LoopNode::LoopBackControl);
        int back_op = back->Opcode();
        Node *backiff, *backbol, *backcmp;
        if( back_op != Op_IfTrue ||
            back_op || Op_IfFalse ||
            ((backiff=back->in(0)) &&
             (backbol=backiff->in(1)) &&
             backbol->req()==2 &&
             (backcmp=backbol->in(1)) &&
             backcmp->Opcode() != Op_CmpI) ) {
Node*lim1=get_ctrl(n->in(1));
Node*lim2=get_ctrl(n->in(2));
          // Now check to see that one of the compared values is from the loop
          // block (Phi or increment from Phi is OK) and the other outside
          // (could be swapped so check both ways).
          Node *phi;
          if( (lim1 == n_ctrl && !n_loop->is_member(get_loop(lim2)) && (phi=n->in(1))) ||
              (lim2 == n_ctrl && !n_loop->is_member(get_loop(lim1)) && (phi=n->in(2))) ) {
            if( phi->Opcode() == Op_AddI )
              phi = phi->in(1); // phi or increment of phi
            if( phi->Opcode() == Op_Phi && phi->in(LoopNode::LoopBackControl)->Opcode() == Op_AddI &&
                iff->is_If() ) { // Must have a proper IfNode tail
IfNode*xif=iff->as_If();
              // See that this test gates exiting the loop or not.
              IdealLoopTree *p0 = get_loop(xif->proj_out(0));
              IdealLoopTree *p1 = get_loop(xif->proj_out(1));
              if( (n_loop == p0 && !n_loop->is_member(p1)) ||
                  (n_loop == p1 && !n_loop->is_member(p0)) ) {
                policy= -1;     // Force infinitely profitable policy
                split_at_loop_head = false;
              }
            }
          }
        }
      } 
      // Check to see if we passed the (fairly tight) gate to split at a loop head
      if( split_at_loop_head ) return;
    }

    // Split compare 'n' through the merge point if it is profitable
C->_split_ctr++;
Node*phi=split_thru_phi(n,n_ctrl,policy,true);
    if( !phi ) return;

    // Found a Phi to split thru!
    // Replace 'n' with the new phi
    _igvn.hash_delete(n);
    _igvn.subsume_node( n, phi );
    if( n_loop && !n_loop->_child )
n_loop->_body.yank(n);

    // Now split the bool up thru the phi
Node*bolphi=split_thru_phi(bol,n_ctrl,-1,false);
    _igvn.hash_delete(bol);
    _igvn.subsume_node( bol, bolphi );
    if( n_loop && !n_loop->_child )
n_loop->_body.yank(bol);
    assert( iff->in(1) == bolphi, "" );
    if( bolphi->Value(&_igvn)->singleton() )
      return;

    // Conditional-move?  Must split up now
    if( !iff->is_If() ) {
Node*cmovphi=split_thru_phi(iff,n_ctrl,-1,false);
      _igvn.hash_delete(iff);
      _igvn.subsume_node( iff, cmovphi );
      return;
    }

    // Now split the IF
    do_split_if( iff );
    return;
  }

  // Check for an IF ready to split; one that has its
  // condition codes input coming from a Phi at the block start.
  int n_op = n->Opcode();

  // Check for an IF being dominated by another IF same test
  if( n_op == Op_If ) {
    Node *bol = n->in(1);
    uint max = bol->outcnt();
    // Check for same test used more than once?
    if( n_op == Op_If && max > 1 && bol->is_Bool() ) {
      // Search up IDOMs to see if this IF is dominated.
      Node *cutoff = get_ctrl(bol);

      // Now search up IDOMs till cutoff, looking for a dominating test
      Node *prevdom = n;
      Node *dom = idom(prevdom);
      while( dom != cutoff ) {
        if( dom->req() > 1 && dom->in(1) == bol && prevdom->in(0) == dom ) {
          // Replace the dominated test with an obvious true or false.
          // Place it on the IGVN worklist for later cleanup.
          C->set_major_progress();
          dominated_by( prevdom, n );
#ifndef PRODUCT
          if( VerifyLoopOptimizations ) verify();
#endif
          return;
        }
        prevdom = dom;
        dom = idom(prevdom);
      }
      // That failed, but see if we have a split-if candidate because
      // the test is replicated on one of several merging input paths.
Node*r=n->in(0);
      if( !_has_irreducible_loops && r->is_Region() && !r->is_Loop() ) {
Node*dom=idom(r);
        if( dom->is_If() && dom->in(1) == bol ) {
do_split_if(n);
          return;
        }
for(uint i=1;i<r->req();i++){
          if( r->in(i)->is_Proj() && 
              r->in(i)->in(0)->is_If() &&
              r->in(i)->in(0)->in(1) == bol ) {
            do_split_if( n );   // Split-If candidate!
            return;
          }
        }
      }
    }
  }

  // See if a shared loop-varying computation has no loop-varying uses.
  // Happens if something is only used for JVM state in uncommon trap exits,
  // like various versions of induction variable+offset.  Clone the
  // computation per usage to allow it to sink out of the loop.
if(has_ctrl(n)&&!n->in(0)&&n->outcnt()>1){//n not dead and has no control edge (can float about)
    Node *n_ctrl = get_ctrl(n);
    IdealLoopTree *n_loop = get_loop(n_ctrl);
    if( n_loop != _ltree_root ) {
      DUIterator_Fast imax, i = n->fast_outs(imax);
      for (; i < imax; i++) {
        Node* u = n->fast_out(i);
IdealLoopTree*u_loop=get_loop(has_ctrl(u)?get_ctrl(u):u);
        if( u_loop == n_loop ) break; // Found loop-varying use
        if( n_loop->is_member( u_loop ) ) break; // Found use in inner loop
        if( u->Opcode() == Op_Opaque1 ) break; // Found loop limit, bugfix for 4677003
      }
if(!(i<imax)){//All uses in outer loops!
        // If n is a load, and the late control is the same as the current
        // control, then the cloning of n is a pointless exercise, because
        // GVN will ensure that we end up where we started.
        // 
        // CNC, 3/15/2006 - What the Flock?
        // Exactly this cloning is intended to allow the late-control to
        // change in order to allow the cloned loads to sink out of the loop.
        // if (!n->is_Load() || late_load_ctrl != n_ctrl) {
          for (DUIterator_Last jmin, j = n->last_outs(jmin); j >= jmin; ) {
            Node *u = n->last_out(j); // Clone private computation per use
            _igvn.hash_delete(u);
            _igvn._worklist.push(u);
            Node *x = n->clone(); // Clone computation
            Node *x_ctrl = NULL;
            if( u->is_Phi() ) {
              // Replace all uses of normal nodes.  Replace Phi uses
              // individually, so the seperate Nodes can sink down
              // different paths.
              uint k = 1;
              while( u->in(k) != n ) k++;
              u->set_req( k, x );
              // x goes next to Phi input path
              x_ctrl = u->in(0)->in(k);
              --j;
            } else {              // Normal use
              // Replace all uses
              for( uint k = 0; k < u->req(); k++ ) {
                if( u->in(k) == n ) {
                  u->set_req( k, x );
                  --j;
                }
              }
              x_ctrl = has_ctrl(u) ? get_ctrl(u) : u->in(0);
            }

            // Find control for 'x' next to use but not inside inner loops.
            // For inner loop uses get the preheader area.
            x_ctrl = place_near_use(x_ctrl);

            if (n->is_Load()) {
              // For loads, add a control edge to a CFG node outside of the loop
              // to force them to not combine and return back inside the loop
              // during GVN optimization (4641526).
              //
              // Because we are setting the actual control input, factor in
              // the result from get_late_ctrl() so we respect any
              // anti-dependences. (6233005).
x_ctrl=get_late_ctrl(x,n_ctrl);

              // Don't allow the control input to be a CFG splitting node.
              // Such nodes should only have ProjNodes as outs, e.g. IfNode
              // should only have IfTrueNode and IfFalseNode (4985384).
              x_ctrl = find_non_split_ctrl(x_ctrl);
assert(dom_depth(find_non_split_ctrl(n_ctrl))<=dom_depth(x_ctrl),"n is later than its clone");

              x->set_req(0, x_ctrl);

              if( x_ctrl == n_ctrl ) { // No point!
                // If we end up setting the control right back to its current
                // value, then we didn't get the clone out of the loop and we
                // lost the reason for the transform in the first place.  Unwind
                // and give up.
x->set_req(0,NULL);
              }
            }
            register_new_node(x, x_ctrl);

            // Some institutional knowledge is needed here: 'x' is
            // yanked because if the optimizer runs GVN on it all the
            // cloned x's will common up and undo this optimization and
            // be forced back in the loop.  This is annoying because it
            // makes +VerifyOpto report false-positives on progress.  I
            // tried setting control edges on the x's to force them to
            // not combine, but the matching gets worried when it tries
            // to fold a StoreP and an AddP together (as part of an
            // address expression) and the AddP and StoreP have
            // different controls.
if(!x->in(0))_igvn._worklist.yank(x);
          } // End of for-all-uses-of-n
          _igvn.remove_dead_node(n);
        // } // See what-the-flock above
      }
    }
  }

  // Check for Opaque2's who's loop has disappeared - who's input is in the
  // same loop nest as their output.  Remove 'em, they are no longer useful.
  if( n_op == Op_Opaque2 &&
      n->in(1) != NULL &&
      get_loop(get_ctrl(n)) == get_loop(get_ctrl(n->in(1))) ) {
    _igvn.add_users_to_worklist(n);
    _igvn.hash_delete(n);
    _igvn.subsume_node( n, n->in(1) );
  }

  // Check for Load's (especially LoadRanges) coming from invariant memory and
  // hoist his memory edge at least as high as his address edge.  His memory
  // was not changed by any intervening calls, so in theory we could hoist the
  // memory edge to the start of the function.  However, we'd like to not
  // hoist past the memory's creation point and the address cannot appear
  // until after the object is created.
  if( n->is_Load() && n->in(MemNode::Address) ) {
    Compile::AliasType *atp = C->alias_type(((LoadNode*)n)->adr_type());
    Node *adr = n->in(MemNode::Address);
    if( !atp->is_rewritable() &&
        // Do not hoist System.out (or err, in) as these are really
        // read/write (via JNI) despite the final tag.
        (C->env()->find_system_klass(ciSymbol::java_lang_System()) !=
         adr->bottom_type()->is_oopptr()->klass()) ) {
Node*st=n->in(MemNode::Memory);
      // For global FINAL constants (constant address, constant
      // contents), allow hoisting to the top of the program.
      if( adr->bottom_type()->singleton() ||
          (adr->is_AddP() &&
           adr->in(AddPNode::Address)->bottom_type()->singleton() &&
           adr->in(AddPNode::Offset)->bottom_type()->singleton()) ) {
        // Find initial memory.  Relies on the optimizer V-N'ing.  Easier and
        // quicker than searching through the program structure.
        st = new (C, 1) ParmNode( C->start(), TypeFunc::Memory );
_igvn.register_new_node_with_optimizer(st);
set_ctrl(st,st->in(0));
      }

      Node *adr_base = adr->is_AddP() ? adr->in(AddPNode::Base) : adr;
      if( adr_base->Opcode() == Op_CastPP ) adr_base = adr_base->in(1);
      if( adr_base->is_Proj() ) adr_base = adr_base->in(0);
      int adr_dd = dom_depth(has_ctrl(adr_base)?get_ctrl(adr_base):adr_base);

      int mem_dd = dom_depth(get_ctrl(st));
      int idom_depth = -1;
Node*idom_last=NULL;
int cnt=0;//Infinite loop breaker
      while( mem_dd > adr_dd ) {
        if( cnt++ > 1000 ) {    // Hit infinite loop?
          st = n->in(MemNode::Memory); // Make no changes
          break;
        }
Node*st2=NULL;
        if( st->Opcode() == Op_Parm ) break; // Initial parm
if(st->is_Proj()){
Node*call=st->in(0);
if(call->is_Call()){
            // Do not bypass a not-inlined super-constructor, as it sets final
            // fields.  Annoyingly, must test for de-serializers as well.
if(call->is_CallJava()){
              ciMethod *meth = call->as_CallJava()->method();
              // Also ignore if <init> is part of some diamond side
              if( meth && idom_depth < 0 &&
                  (meth->name() == ciSymbol::object_initializer_name() ||
                   meth->holder()->name() == ciSymbol::java_io_ObjectInputStream()) )
                break;
            }
            st2 = call->in(TypeFunc::Memory); // Bypass other calls in constructors
}else if(call->is_MemBar()){
st2=call->in(TypeFunc::Memory);
          } else {
            ShouldNotReachHere();
          }
}else if(st->is_MergeMem()){
          st2 = st->as_MergeMem()->memory_at(atp->index());
        } else if( st->is_Store() ) {
          Node *st_adr = st->in(MemNode::Address);
          Node *st_base = st_adr->is_AddP() ? st_adr->in(AddPNode::Base) : st_adr;
          if( st_base->Opcode() == Op_CastPP ) st_base = adr_base->in(1);
          if( st_base->is_Proj() ) st_base = st_base->in(0);
          if( st_base == adr_base )
            // We could replace the load with the store'd value but
            // the regular optimizer will do that
            break;
          if( !adr_base->is_Allocate() ||
              !st_base ->is_Allocate() )
            break;              // Must precisely find CallNew's or risk aliasing confusion
          // Must be some init'ing store to same kind of invariant memory
st2=st->in(MemNode::Memory);
        } else if( st->is_Phi() ) {
          // What to do at merge points?  The address's dom_depth is still
          // closer to the root than the memory edge, so it should be legit to
          // go to the immediate dominator.  Always go left, although in
          // theory any direction works as long as I'm not cycling round an
          // infinite irreducible loop.  Left happens to get me out of
          // canonical loops, and the overflow counter will break me out of
          // other loops.  The verifier assures that final field inits are
          // unconditionally visible.
          if( idom_depth < 0 ) {
            // Capture the idom depth; the address come from before here (see
            // cutout below); all we need is the memory edge available at that
            // idom depth - for which we have to crawl up the graph.
            idom_depth = dom_depth(idom(st->in(0)));
idom_last=st;
            if( idom_depth <= adr_dd ) break;
          }
          st2 = st->in(1);      // Always go left
        } else if( st->Opcode() == Op_EscapeMemory ) {
          // If we can prove the address belongs to the EscapeMemory, then the
          // Load is loading from a final field being set in the new object.
          // If we can prove that the address does NOT belong to the object,
          // we can bypass the whole allocation.  Otherwise we're stuck here.
          Node *priv = st->in(1); // Private memory from the Escape
          while( priv->is_Store() ) priv = priv->in(MemNode::Memory);
          if( !priv->is_Proj() ) 
            break; // Cannot find NEW?  Give up
Node*callnew=priv->in(0);
          if( callnew == adr_base ) {
            st2 = st->in(1);    // Address bases match, so use private memory
          } else if( adr_dd <= (int)dom_depth(callnew) ) { // Address predates new?
            st2 = st->in(2);    // Use prior public memory
          } else {
            break;              // Not sure; give up now
          }
        } else {
          ShouldNotReachHere();
        }
assert(st2,"must have a valid next memory");
st=st2;
        mem_dd = dom_depth(get_ctrl(st));
        if( mem_dd <= idom_depth ) idom_depth = -1; // Crawled up to the idom diamond root
      }
      if( idom_depth > 0 )      // We never found up past the CFG diamond?
        st = idom_last;         // Then use last valid memory edge
      // Now allow this Load to float up
      if( st != n->in(MemNode::Memory) ) {
        // Can I hoist this out of a loop?
        Node *early = get_early_ctrl(n, true);
Node*legal=get_ctrl(n);
        Node *least = legal;
IdealLoopTree*old_loop=get_loop(legal);
        while( early != legal ) {
          legal = idom(legal);  // Bump up the IDOM tree
          if( legal == C->root() ) break; // Bad IDOM info?
          // Check for lower nesting depth
          if( get_loop(legal)->_nest < get_loop(least)->_nest )
            least = legal;
        }
        if( legal != C->root() ) { // IDOM info good?
          // Now allow this Load to float up
_igvn.hash_delete(n);
_igvn.add_users_to_worklist(n);
          n->set_req_X(MemNode::Memory,st,&_igvn);
_igvn._worklist.push(n);
          set_ctrl(n, least);     // Set new location
IdealLoopTree*new_loop=get_loop(least);
          // If changing loop bodies, see if we need to collect into new body
          if( old_loop != new_loop ) {
            if( old_loop && !old_loop->_child ) 
old_loop->_body.yank(n);
            if( !new_loop->_child )
new_loop->_body.push(n);//Collect body info
          }
        }
      }
    }
  }
}

//------------------------------split_if_with_blocks---------------------------
// Check for aggressive application of 'split-if' optimization,
// using basic block level info.
void PhaseIdealLoop::split_if_with_blocks( VectorSet &visited, Node_Stack &nstack ) {
  Node *n = C->root();
  visited.set(n->_idx); // first, mark node as visited
  // Do pre-visit work for root
  n = split_if_with_blocks_pre( n );
  uint cnt = n->outcnt();
  uint i   = 0;
  while (true) {
    // Visit all children
    if (i < cnt) {
      Node* use = n->raw_out(i);
      ++i;
      if (use->outcnt() != 0 && !visited.test_set(use->_idx)) {
        // Now do pre-visit work for this use
        use = split_if_with_blocks_pre( use );
        nstack.push(n, i); // Save parent and next use's index.
        n   = use;         // Process all children of current use.
        cnt = use->outcnt();
        i   = 0;
      }
    }
    else {
      // All of n's children have been processed, complete post-processing.
      if (cnt != 0 && !n->is_Con()) {
        assert(has_node(n), "no dead nodes");
        split_if_with_blocks_post( n );
      }
      if (nstack.is_empty()) {
        // Finished all nodes on stack.
        break;
      }
      // Get saved parent node and next use's index. Visit the rest of uses.
      n   = nstack.node();
      cnt = n->outcnt();
      i   = nstack.index();
      nstack.pop();
    }
  }
}


//=============================================================================
//
//                   C L O N E   A   L O O P   B O D Y
//

//------------------------------clone_iff--------------------------------------
// Passed in a Phi merging (recursively) some nearly equivalent Bool/Cmps.
// "Nearly" because all Nodes have been cloned from the original in the loop,
// but the fall-in edges to the Cmp are different.  Clone bool/Cmp pairs
// through the Phi recursively, and return a Bool.
BoolNode *PhaseIdealLoop::clone_iff( PhiNode *phi, IdealLoopTree *loop ) {

  // Convert this Phi into a Phi merging Bools
  uint i;
  for( i = 1; i < phi->req(); i++ ) {
    Node *b = phi->in(i);
    if( b->is_Phi() ) {
      _igvn.hash_delete(phi);
      _igvn._worklist.push(phi);
      phi->set_req(i, clone_iff( b->as_Phi(), loop ));
    } else {
      assert( b->is_Bool(), "" );
    }
  }

  Node *sample_bool = phi->in(1);
  Node *sample_cmp  = sample_bool->in(1);

  // Make Phis to merge the Cmp's inputs.
  int size = phi->in(0)->req();
  PhiNode *phi1 = new (C, size) PhiNode( phi->in(0), Type::TOP );
  PhiNode *phi2 = new (C, size) PhiNode( phi->in(0), Type::TOP );
  bool dif1 = false, dif2 = false;
  for( i = 1; i < phi->req(); i++ ) {
    Node *n1 = phi->in(i)->in(1)->in(1);
    Node *n2 = phi->in(i)->in(1)->in(2);
    phi1->set_req( i, n1 );
    phi2->set_req( i, n2 );
    phi1->set_type( phi1->type()->meet(n1->bottom_type()) );
    phi2->set_type( phi2->type()->meet(n2->bottom_type()) );
    if( phi1->in(1) != phi1->in(i) ) dif1 = true;
    if( phi2->in(1) != phi2->in(i) ) dif2 = true;
  }
  // See if these Phis have been made before.
  // Register with optimizer
Node*hit1=dif1?_igvn.hash_find_insert(phi1):phi1->in(1);
  if( hit1 ) {                  // Hit, toss just made Phi
    _igvn.remove_dead_node(phi1); // Remove new phi
phi1->destruct();
  } else {                      // Miss
hit1=_igvn.register_new_node_with_optimizer(phi1);
  }
Node*hit2=dif2?_igvn.hash_find_insert(phi2):phi2->in(1);
  if( hit2 ) {                  // Hit, toss just made Phi
    _igvn.remove_dead_node(phi2); // Remove new phi
phi2->destruct();
  } else {                      // Miss
hit2=_igvn.register_new_node_with_optimizer(phi2);
  }
  // Register Phis with loop/block info
if(dif1)set_ctrl(hit1,phi->in(0));
if(dif2)set_ctrl(hit2,phi->in(0));
  // Make a new Cmp
  Node *cmp = sample_cmp->clone();
cmp->set_req(1,hit1);
cmp->set_req(2,hit2);
Node*hitcmp=_igvn.hash_find_insert(cmp);
  if( hitcmp ) {
_igvn.remove_dead_node(cmp);
cmp->destruct();
    cmp = hitcmp;
  } else {
    _igvn.register_new_node_with_optimizer(cmp);
  }
set_early_ctrl(cmp);

  // Make a new Bool
  Node *b = sample_bool->clone();
  b->set_req(1,cmp);
Node*hitbol=_igvn.hash_find_insert(b);
  if( hitbol ) {
_igvn.remove_dead_node(b);
b->destruct();
    b = hitbol;
  } else {
    _igvn.register_new_node_with_optimizer(b);
  }
set_early_ctrl(b);

  assert( b->is_Bool(), "" );
  return (BoolNode*)b;
}

//------------------------------clone_bool-------------------------------------
// Passed in a Phi merging (recursively) some nearly equivalent Bool/Cmps.
// "Nearly" because all Nodes have been cloned from the original in the loop,
// but the fall-in edges to the Cmp are different.  Clone bool/Cmp pairs
// through the Phi recursively, and return a Bool.
CmpNode *PhaseIdealLoop::clone_bool( PhiNode *phi, IdealLoopTree *loop ) {
  uint i;
  // Convert this Phi into a Phi merging Bools
  for( i = 1; i < phi->req(); i++ ) {
    Node *b = phi->in(i);
    if( b->is_Phi() ) {
      _igvn.hash_delete(phi);
      _igvn._worklist.push(phi);
      phi->set_req(i, clone_bool( b->as_Phi(), loop ));
    } else {
      assert( b->is_Cmp() || b->is_top(), "inputs are all Cmp or TOP" );
    }
  }

  Node *sample_cmp = phi->in(1);

  // Make Phis to merge the Cmp's inputs.
  int size = phi->in(0)->req();
  PhiNode *phi1 = new (C, size) PhiNode( phi->in(0), Type::TOP );
  PhiNode *phi2 = new (C, size) PhiNode( phi->in(0), Type::TOP );
  for( uint j = 1; j < phi->req(); j++ ) {
    Node *cmp_top = phi->in(j); // Inputs are all Cmp or TOP
    Node *n1, *n2;
    if( cmp_top->is_Cmp() ) {
      n1 = cmp_top->in(1);
      n2 = cmp_top->in(2);
    } else {
      n1 = n2 = cmp_top;
    }
    phi1->set_req( j, n1 );
    phi2->set_req( j, n2 );
    phi1->set_type( phi1->type()->meet(n1->bottom_type()) );
    phi2->set_type( phi2->type()->meet(n2->bottom_type()) );
  }

  // See if these Phis have been made before.
  // Register with optimizer
  Node *hit1 = _igvn.hash_find_insert(phi1);
  if( hit1 ) {                  // Hit, toss just made Phi
    _igvn.remove_dead_node(phi1); // Remove new phi
    assert( hit1->is_Phi(), "" );
    phi1 = (PhiNode*)hit1;      // Use existing phi
  } else {                      // Miss
    _igvn.register_new_node_with_optimizer(phi1);
  }
  Node *hit2 = _igvn.hash_find_insert(phi2);
  if( hit2 ) {                  // Hit, toss just made Phi
    _igvn.remove_dead_node(phi2); // Remove new phi
    assert( hit2->is_Phi(), "" );
    phi2 = (PhiNode*)hit2;      // Use existing phi
  } else {                      // Miss
    _igvn.register_new_node_with_optimizer(phi2);
  }
  // Register Phis with loop/block info
  set_ctrl(phi1, phi->in(0));
  set_ctrl(phi2, phi->in(0));
  // Make a new Cmp
  Node *cmp = sample_cmp->clone();
  cmp->set_req( 1, phi1 );
  cmp->set_req( 2, phi2 );
  _igvn.register_new_node_with_optimizer(cmp);
  set_ctrl(cmp, phi->in(0));

  assert( cmp->is_Cmp(), "" );
  return (CmpNode*)cmp;
}

//------------------------------sink_use---------------------------------------
// If 'use' was in the loop-exit block, it now needs to be sunk
// below the post-loop merge point.
void PhaseIdealLoop::sink_use( Node *use, Node *post_loop ) {
if(!use->is_CFG()&&get_ctrl_no_assert(use)==post_loop->in(2)){
set_ctrl_no_assert(use,post_loop);
    for (DUIterator j = use->outs(); use->has_out(j); j++)
      sink_use(use->out(j), post_loop);
  }
}

//------------------------------copy_idom-------------------------------------
Node **PhaseIdealLoop::copy_idom( ResourceArea *area ) const {
  Node **old_idom = NEW_ARENA_ARRAY( area, Node*, _idom_size );
  memcpy( old_idom, _idom, _idom_size*sizeof(Node*) );
  return old_idom;
}

//------------------------------old_idom--------------------------------------
static Node *old_idom(Node* d, PhaseIdealLoop *phase, Node **old_idom, int old_idom_size, int new_counter) {
  int didx = d->_idx;
  if( didx >= new_counter ) return d->in(2);
  assert0(didx < old_idom_size); 
Node*n=old_idom[didx];
  assert(n != NULL,"Bad immediate dominator info.");
  if( !n->in(0) ) {             // Dead CFG?
    n = phase->get_ctrl_no_assert( n ); // Skip dead CFG nodes
old_idom[didx]=n;//Lazily remove dead CFG nodes from table.
  }
  return n;
}

//------------------------------clone_loop-------------------------------------
//
//                   C L O N E   A   L O O P   B O D Y
//
// This is the basic building block of the loop optimizations.  It clones an
// entire loop body.  It makes an old_new loop body mapping; with this mapping
// you can find the new-loop equivalent to an old-loop node.  All new-loop
// nodes are exactly equal to their old-loop counterparts, all edges are the
// same.  All exits from the old-loop now have a RegionNode that merges the
// equivalent new-loop path.  This is true even for the normal "loop-exit"
// condition.  All uses of loop-invariant old-loop values now come from (one
// or more) Phis that merge their new-loop equivalents.
//
// This operation leaves the graph in an illegal state: there are two valid
// control edges coming from the loop pre-header to both loop bodies.  I'll
// definitely have to hack the graph after running this transform.
//
// From this building block I will further edit edges to perform loop peeling
// or loop unrolling or iteration splitting (Range-Check-Elimination), etc.
//
// Parameter side_by_size_idom:
//   When side_by_size_idom is NULL, the dominator tree is constructed for
//      the clone loop to dominate the original.  Used in construction of
//      pre-main-post loop sequence.
//   When nonnull, the clone and original are side-by-side, both are
//      dominated by the side_by_side_idom node.  Used in construction of
//      unswitched loops.
void PhaseIdealLoop::clone_loop( IdealLoopTree *loop, Node_List &old_new, int dd,
Node*side_by_side_idom,bool clone_after){
  ResourceArea *area = Thread::current()->resource_area();
  uint new_counter = C->unique();

  // Step 0: Copy the original IDOM relationship, used in finding where values
  // flow from inside the loop to outside the loop.
  Node **old_idoms = copy_idom(area);
  uint old_idom_size = _idom_size;
uint loopbodysize=loop->_body.size();

  // Step 1: Clone the loop body.  Make the old->new mapping.
  uint i;
for(i=0;i<loopbodysize;i++){
    Node *old = loop->_body.at(i);
    Node *nnn = old->clone();
    old_new.map( old->_idx, nnn );
    _igvn.register_new_node_with_optimizer(nnn);
  }
  assert0( loopbodysize == loop->_body.size() );


  // Step 2: Fix the edges in the new body.  If the old input is outside the
  // loop use it.  If the old input is INside the loop, use the corresponding
  // new node instead.
for(i=0;i<loopbodysize;i++){
    Node *old = loop->_body.at(i);
    Node *nnn = old_new[old->_idx];
    // Correct edges to the new node
    for( uint j = 0; j < nnn->req(); j++ ) {
        Node *n = nnn->in(j);
        if( n ) {
          IdealLoopTree *old_in_loop = get_loop( has_ctrl(n) ? get_ctrl(n) : n );
          if( loop->is_member( old_in_loop ) )
            nnn->set_req(j, old_new[n->_idx]);
        }
    }
    _igvn.hash_find_insert(nnn);
    // Fix CFG/Loop controlling the new node
    if (has_ctrl(old)) {
set_ctrl_no_assert(nnn,old_new[get_ctrl(old)->_idx]);
    } else {
      set_loop(nnn, loop->_parent);
      if (old->outcnt() > 0) {
        set_idom( nnn, old_new[idom(old)->_idx], dd );
      }
    }
    assert0( loopbodysize == loop->_body.size() );
  }
  Node *newhead = old_new[loop->_head->_idx];
  set_idom(newhead, newhead->in(LoopNode::EntryControl), dd);


  // Step 3: Now fix control uses.  Loop varying control uses have already
  // been fixed up (as part of all input edges in Step 2).  Loop invariant
  // control uses must be either an IfFalse or an IfTrue.  Make a merge
  // point to merge the old and new IfFalse/IfTrue nodes; make the use
  // refer to this.
  Node_List worklist(area);
for(i=0;i<loopbodysize;i++){
    Node* old = loop->_body.at(i);
    if( !old->is_CFG() ) continue;
    Node* nnn = old_new[old->_idx];

    // Copy uses to a worklist, so I can munge the def-use info
    // with impunity.
    for (DUIterator_Fast jmax, j = old->fast_outs(jmax); j < jmax; j++)
      worklist.push(old->fast_out(j));

    while( worklist.size() ) {  // Visit all uses
      Node *use = worklist.pop();
      if (!has_node(use))  continue; // Ignore dead nodes
IdealLoopTree*use_loop=get_loop(has_ctrl(use)?get_ctrl_no_assert(use):use);
      if( !loop->is_member( use_loop ) && use->is_CFG() ) {
        // Both OLD and USE are CFG nodes here.
        assert( use->is_Proj(), "" );

        // Clone the loop exit control projection
        Node *newuse = use->clone();
        newuse->set_req(0,nnn);
        _igvn.register_new_node_with_optimizer(newuse);
        set_loop(newuse, use_loop);
        set_idom(newuse, nnn, dom_depth(nnn) + 1 );

        // We need a Region to merge the exit from the peeled body and the
        // exit from the old loop body.
        RegionNode *r = new (C, 3) RegionNode(3);
        // Map the old use to the new merge point
        old_new.map( use->_idx, r );
        uint dd_r = MIN2(dom_depth(newuse),dom_depth(use));
        assert( dd_r >= dom_depth(dom_lca(newuse,use)), "" );

        // Now finish up 'r'       
        r->set_req( 1, newuse );
        r->set_req( 2,    use );
        _igvn.register_new_node_with_optimizer(r);
        set_loop(r, use_loop);
        set_idom(r, (clone_after ? use : dom_lca(newuse,old_new[loop->_tail->_idx])), dd_r);

        // The original user of 'use' uses 'r' instead.
        for (DUIterator_Fast lmax, l = use->fast_outs(lmax); l < lmax; l++) {
Node*useuse=use->fast_out(l);
if(useuse==r)continue;
          _igvn.hash_delete(useuse);
          _igvn._worklist.push(useuse);
for(uint k=0;k<useuse->req();k++){
            if( useuse->in(k) == use ) {
              useuse->set_req(k, r);
if(useuse->is_CFG())
                set_idom( useuse, 
                          (useuse->is_Region() && !(useuse->is_Loop() && useuse->in(LoopNode::EntryControl)==r)) ? compute_idom(useuse) : r, 
                          dom_depth(useuse) );
              else if( get_ctrl_no_assert(useuse) == use ) 
                set_ctrl( useuse, r);
              break;            // Fixup only 1 edge at a time
            }
          }
          --l; --lmax;          // Repeat while edges need fixing up
        }
        assert0( use->unique_out() == r ); // Should be exactly 1 user of 'use' now

      } // End of if a loop-exit test
      assert0( loopbodysize == loop->_body.size() );
    }
  }

  // Step 4: If loop-invariant use is not control, it must be dominated by a
  // loop exit IfFalse/IfTrue.  Find "proper" loop exit.  Make a Region
  // there if needed.  Make a Phi there merging old and new used values.
  Node_List *split_if_set = NULL;
  Node_List *split_bool_set = NULL;
for(i=0;i<loopbodysize;i++){
    Node* old = loop->_body.at(i);
if(!old->is_CFG())
      handle_invariant_uses(old,loop,old_new,&worklist,old_idoms,old_idom_size,new_counter,loopbodysize,&split_if_set,&split_bool_set);
  }
for(i=0;i<loopbodysize;i++){
    Node* old = loop->_body.at(i);
if(old->is_CFG())
      handle_invariant_uses(old,loop,old_new,&worklist,old_idoms,old_idom_size,new_counter,loopbodysize,&split_if_set,&split_bool_set);
  }

  // Check for IFs that need splitting/cloning.  Happens if an IF outside of
  // the loop uses a condition set in the loop.  The original IF probably
  // takes control from one or more OLD Regions (which in turn get from NEW
  // Regions).  In any case, there will be a set of Phis for each merge point
  // from the IF up to where the original BOOL def exists the loop.
  if( split_if_set ) {
    while( split_if_set->size() ) {
      Node *iff = split_if_set->pop();
Node*phi=iff->in(1);
if(phi->is_Phi()){
        BoolNode *b = clone_iff( phi->as_Phi(), loop );
        _igvn.hash_delete(iff);
        _igvn._worklist.push(iff);
        iff->set_req(1, b);
      }
    }
  }
  if( split_bool_set ) {
    while( split_bool_set->size() ) {
      Node *b = split_bool_set->pop();
      Node *phi = b->in(1);
      CmpNode *cmp = clone_bool( phi->as_Phi(), loop );
      _igvn.hash_delete(b);
      _igvn._worklist.push(b);
      b->set_req(1, cmp);
    }
  }

}

//------------------------------handle_invariant_uses--------------------------
void PhaseIdealLoop::handle_invariant_uses( Node *old, IdealLoopTree *loop, Node_List &old_new, Node_List *worklist, Node **old_idoms, int old_idom_size, uint new_counter, uint loopbodysize, Node_List **split_if_set, Node_List **split_bool_set ) {
  Node* nnn = old_new[old->_idx];
  ResourceArea *area = Thread::current()->resource_area();
  // Copy uses to a worklist, so I can munge the def-use info
  // with impunity.
  for (DUIterator_Fast jmax, j = old->fast_outs(jmax); j < jmax; j++)
worklist->push(old->fast_out(j));

while(worklist->size()){
assert0(loopbodysize==loop->_body.size());
Node*use=worklist->pop();
      if (!has_node(use))  continue; // Ignore dead nodes
      if (use->in(0) == C->top())  continue;
IdealLoopTree*use_loop=get_loop(has_ctrl(use)?get_ctrl_no_assert(use):use);
      // Check for data-use outside of loop - at least one of OLD or USE
      // must not be a CFG node.
      if( !loop->is_member( use_loop ) && (!old->is_CFG() || !use->is_CFG())) {

        // If the Data use is an IF, that means we have an IF outside of the
        // loop that is switching on a condition that is set inside of the
        // loop.  Happens if people set a loop-exit flag; then test the flag
        // in the loop to break the loop, then test is again outside of the
        // loop to determine which way the loop exited.
        if( use->is_If() || use->is_CMove() ) {
          // Since this code is highly unlikely, we lazily build the worklist
          // of such Nodes to go split.
if(!*split_if_set)
*split_if_set=new Node_List(area);
(*split_if_set)->push(use);
        }
        if( use->is_Bool() ) {
if(!*split_bool_set)
*split_bool_set=new Node_List(area);
(*split_bool_set)->push(use);
        }


        // Get "block" use is in
        uint idx = 0;
        while( use->in(idx) != old ) idx++;
Node*prev=use->is_CFG()?use:get_ctrl_no_assert(use);
        assert( !loop->is_member( get_loop( prev ) ), "" );
        Node *cfg = use->is_Phi() ? prev->in(idx) : old_idom(prev,this,old_idoms,old_idom_size,new_counter);
        if (cfg->is_top()) {    // Use is dead?
          _igvn.hash_delete(use);
          _igvn._worklist.push(use);
          use->set_req(idx, C->top());
          continue;
        }

        while( !loop->is_member( get_loop( cfg ) ) ) {
          prev = cfg;
          cfg = old_idom(prev,this,old_idoms,old_idom_size,new_counter);
        }
        // If the use occurs after merging several exits from the loop, then
        // old value must have dominated all those exits.  Since the same old
        // value was used on all those exits we did not need a Phi at this
        // merge point.  NOW we do need a Phi here.  Each loop exit value
        // is now merged with the peeled body exit; each exit gets its own
        // private Phi and those Phis need to be merged here.
        Node *phi;
        if( prev->is_Region() ) {
          if( idx == 0 ) {      // Updating control edge?
            phi = prev;         // Just use existing control
          } else {              // Else need a new Phi
            phi = PhiNode::make( prev, old );
            // Now recursively fix up the new uses of old!
            for( uint i = 1; i < prev->req(); i++ ) {
worklist->push(phi);//Onto worklist once for each 'old' input
            }
          }
        } else {
          // Get new RegionNode merging old and new loop exits
          prev = old_new[prev->_idx];
          assert( prev, "just made this in step 7" );
          if( idx == 0 ) {      // Updating control edge?
            phi = prev;         // Just use existing control
          } else {              // Else need a new Phi
            // Make a new Phi merging data values properly
const Type*t=old->bottom_type();
            const TypePtr* at = NULL;
if(t==Type::MEMORY){
              // For new memory phis, use narrowest slice
              at = old->adr_type(); // Get old value address slice
const TypePtr*ut=use->adr_type();
              if( at == TypePtr::BOTTOM && ut ) at = ut; // If fat, use 'use' slice - it might be narrow
              at = C->alias_type(at)->adr_type(); // Flatten type if needed
            }
phi=PhiNode::make(prev,old,t,at);
            phi->set_req( 1, nnn );
          }
        }
        // If inserting a new Phi, check for prior hits
        if( idx != 0 ) {
          Node *hit = _igvn.hash_find_insert(phi);
          if( hit == NULL ) {
           _igvn.register_new_node_with_optimizer(phi); // Register new phi
          } else {                                      // or
            // Remove the new phi from the graph and use the hit
            _igvn.remove_dead_node(phi);
phi->destruct();//Recover useless new node
            phi = hit;                                  // Use existing phi
          }
          set_ctrl(phi, prev);
        }
        // Make 'use' use the Phi instead of the old loop body exit value
        _igvn.hash_delete(use);
        _igvn._worklist.push(use);
        use->set_req(idx, phi);
if(idx==0){//If picking up the merging control...
          Node *early = get_early_ctrl(use, true);
          set_ctrl_no_assert(use,early);
IdealLoopTree*new_loop=get_loop(early);
          if( new_loop != use_loop ) {
            assert0( new_loop != loop ); // If these are equal I did not ...
if(!use_loop->_child)use_loop->_body.yank(use);
if(!new_loop->_child)new_loop->_body.push(use);
          }
        }
        if( use->_idx >= new_counter ) { // If updating new phis
          // Not needed for correctness, but prevents a weak assert
          // in AddPNode from tripping (when we end up with different
          // base & derived Phis that will become the same after
          // IGVN does CSE).
          Node *hit = _igvn.hash_find_insert(use);
          if( hit )             // Go ahead and re-hash for hits.
            _igvn.subsume_node( use, hit );
        }

        // If 'use' was in the loop-exit block, it now needs to be sunk
        // below the post-loop merge point.
        sink_use( use, prev );
      }
      assert0( loopbodysize == loop->_body.size() );
    }
}


//---------------------- stride_of_possible_iv -------------------------------------
// Looks for an iff/bool/comp with one operand of the compare
// being a cycle involving an add and a phi,
// with an optional truncation (left-shift followed by a right-shift)
// of the add. Returns zero if not an iv.
int PhaseIdealLoop::stride_of_possible_iv(Node* iff) {
  Node* trunc1 = NULL;
  Node* trunc2 = NULL;
  const TypeInt* ttype = NULL;
  if (!iff->is_If() || iff->in(1) == NULL || !iff->in(1)->is_Bool()) {
    return 0;
  }
  BoolNode* bl = iff->in(1)->as_Bool();
  Node* cmp = bl->in(1);
if(!cmp||(cmp->Opcode()!=Op_CmpI&&cmp->Opcode()!=Op_CmpU)){
    return 0;
  }
  // Must have an invariant operand
  if (is_member(get_loop(iff), get_ctrl(cmp->in(2)))) {
    return 0;
  }
  Node* add2 = NULL;
  Node* cmp1 = cmp->in(1);
  if (cmp1->is_Phi()) {
    // (If (Bool (CmpX phi:(Phi ...(Optional-trunc(AddI phi add2))) )))
    Node* phi = cmp1;
    for (uint i = 1; i < phi->req(); i++) {
      Node* in = phi->in(i);
      Node* add = CountedLoopNode::match_incr_with_optional_truncation(in,
                                &trunc1, &trunc2, &ttype);
      if (add && add->in(1) == phi) {
        add2 = add->in(2);
        break;
      }
    }
  } else {
    // (If (Bool (CmpX addtrunc:(Optional-trunc((AddI (Phi ...addtrunc...) add2)) )))
    Node* addtrunc = cmp1;
    Node* add = CountedLoopNode::match_incr_with_optional_truncation(addtrunc,
                                &trunc1, &trunc2, &ttype);
    if (add && add->in(1)->is_Phi()) {
      Node* phi = add->in(1);
      for (uint i = 1; i < phi->req(); i++) {
        if (phi->in(i) == addtrunc) {
          add2 = add->in(2);
          break;
        }
      }
    }
  }
  if (add2 != NULL) {
    const TypeInt* add2t = _igvn.type(add2)->is_int();
    if (add2t->is_con()) {
      return add2t->get_con();
    }
  }
  return 0;
}


//---------------------- stay_in_loop -------------------------------------
// Return the (unique) control output node that's in the loop (if it exists.)
Node* PhaseIdealLoop::stay_in_loop( Node* n, IdealLoopTree *loop) {
  Node* unique = NULL;
  if (!n) return NULL;
  for (DUIterator_Fast imax, i = n->fast_outs(imax); i < imax; i++) {
    Node* use = n->fast_out(i);
    if (!has_ctrl(use) && loop->is_member(get_loop(use))) {
      if (unique != NULL) {
        return NULL;
      }
      unique = use;
    }
  }
  return unique;
}

//------------------------------ register_node -------------------------------------
// Utility to register node "n" with PhaseIdealLoop
void PhaseIdealLoop::register_node(Node* n, IdealLoopTree *loop, Node* pred, int ddepth) {
  _igvn.register_new_node_with_optimizer(n);
  loop->_body.push(n);
  if (n->is_CFG()) {
    set_loop(n, loop);
    set_idom(n, pred, ddepth);
  } else {
    set_ctrl(n, pred);
  }
}

//------------------------------ proj_clone -------------------------------------
// Utility to create an if-projection
ProjNode* PhaseIdealLoop::proj_clone(ProjNode* p, IfNode* iff) {
  ProjNode* c = p->clone()->as_Proj();
  c->set_req(0, iff);
  return c;
}

//------------------------------ short_circuit_if -------------------------------------
// Force the iff control output to be the live_proj
Node* PhaseIdealLoop::short_circuit_if(IfNode* iff, ProjNode* live_proj) {
  int proj_con = live_proj->_con;
  assert(proj_con == 0 || proj_con == 1, "false or true projection");
  Node *con = _igvn.intcon(proj_con);
  set_ctrl(con, C->root());
  if (iff) {
    iff->set_req(1, con);
  }
  return con;
}

//------------------------------ insert_if_before_proj -------------------------------------
// Insert a new if before an if projection (* - new node)
//
// before
//           if(test)
//           /     \
//          v       v
//    other-proj   proj (arg)
//
// after
//           if(test)
//           /     \
//          /       v
//         |      * proj-clone
//         v          |
//    other-proj      v
//                * new_if(relop(cmp[IU](left,right)))
//                  /  \
//                 v    v
//         * new-proj  proj
//         (returned)
//
ProjNode* PhaseIdealLoop::insert_if_before_proj(Node* left, bool Signed, BoolTest::mask relop, Node* right, ProjNode* proj) {
  IfNode* iff = proj->in(0)->as_If();
  IdealLoopTree *loop = get_loop(proj);
  ProjNode *other_proj = iff->proj_out(!proj->is_IfTrue())->as_Proj();
  int ddepth = dom_depth(proj);

  _igvn.hash_delete(iff);
  _igvn._worklist.push(iff);
  _igvn.hash_delete(proj);
  _igvn._worklist.push(proj);

  proj->set_req(0, NULL);  // temporary disconnect
  ProjNode* proj2 = proj_clone(proj, iff);
  register_node(proj2, loop, iff, ddepth);

  Node* cmp = Signed ? (Node*) new (C,3)CmpINode(left, right) : (Node*) new (C,3)CmpUNode(left, right);
  register_node(cmp, loop, proj2, ddepth);

  BoolNode* bol = new (C,2)BoolNode(cmp, relop);
  register_node(bol, loop, proj2, ddepth);

  IfNode* new_if = new (C,2)IfNode(proj2, bol, iff->_prob, iff->_fcnt);
  register_node(new_if, loop, proj2, ddepth);

  proj->set_req(0, new_if); // reattach
  set_idom(proj, new_if, ddepth);

  ProjNode* new_exit = proj_clone(other_proj, new_if)->as_Proj();
  register_node(new_exit, get_loop(other_proj), new_if, ddepth);

  return new_exit;
}

//------------------------------ insert_region_before_proj -------------------------------------
// Insert a region before an if projection (* - new node)
//
// before
//           if(test)
//          /      |
//         v       |
//       proj      v
//               other-proj
//
// after
//           if(test)
//          /      |
//         v       |
// * proj-clone    v
//         |     other-proj
//         v
// * new-region
//         |
//         v
// *      dum_if
//       /     \
//      v       \
// * dum-proj    v
//              proj
//
RegionNode* PhaseIdealLoop::insert_region_before_proj(ProjNode* proj) {
  IfNode* iff = proj->in(0)->as_If();
  IdealLoopTree *loop = get_loop(proj);
  ProjNode *other_proj = iff->proj_out(!proj->is_IfTrue())->as_Proj();
  int ddepth = dom_depth(proj);

  _igvn.hash_delete(iff);
  _igvn._worklist.push(iff);
  _igvn.hash_delete(proj);
  _igvn._worklist.push(proj);

  proj->set_req(0, NULL);  // temporary disconnect
  ProjNode* proj2 = proj_clone(proj, iff);
  register_node(proj2, loop, iff, ddepth);

  RegionNode* reg = new (C,2)RegionNode(2);
  reg->set_req(1, proj2);
  register_node(reg, loop, iff, ddepth);

  IfNode* dum_if = new (C,2)IfNode(reg, short_circuit_if(NULL, proj), iff->_prob, iff->_fcnt);
  register_node(dum_if, loop, reg, ddepth);

  proj->set_req(0, dum_if); // reattach
  set_idom(proj, dum_if, ddepth);

  ProjNode* dum_proj = proj_clone(other_proj, dum_if);
  register_node(dum_proj, loop, dum_if, ddepth);

  return reg;
}

//------------------------------ insert_cmpi_loop_exit -------------------------------------
// Clone a signed compare loop exit from an unsigned compare and
// insert it before the unsigned cmp on the stay-in-loop path.
// All new nodes inserted in the dominator tree between the original
// if and it's projections.  The original if test is replaced with
// a constant to force the stay-in-loop path.
//
// This is done to make sure that the original if and it's projections
// still dominate the same set of control nodes, that the ctrl() relation
// from data nodes to them is preserved, and that their loop nesting is
// preserved.
//
// before
//          if(i <u limit)    unsigned compare loop exit
//         /       |
//        v        v
//   exit-proj   stay-in-loop-proj
//
// after
//          if(stay-in-loop-const)  original if
//         /       |
//        /        v
//       /  if(i <  limit)    new signed test
//      /  /       |
//     /  /        v
//    /  /  if(i <u limit)    new cloned unsigned test
//   /  /   /      |
//   v  v  v       |
//    region       |
//        |        |
//      dum-if     |
//     /  |        |
// ether  |        |
//        v        v
//   exit-proj   stay-in-loop-proj
//
IfNode* PhaseIdealLoop::insert_cmpi_loop_exit(IfNode* if_cmpu, IdealLoopTree *loop) {
  const bool Signed   = true;
  const bool Unsigned = false;

  BoolNode* bol = if_cmpu->in(1)->as_Bool();
  if (bol->_test._test != BoolTest::lt) return NULL;
  CmpNode* cmpu = bol->in(1)->as_Cmp();
  if (cmpu->Opcode() != Op_CmpU) return NULL;
  int stride = stride_of_possible_iv(if_cmpu);
  if (stride == 0) return NULL;

  ProjNode* lp_continue = stay_in_loop(if_cmpu, loop)->as_Proj();
  ProjNode* lp_exit     = if_cmpu->proj_out(!lp_continue->is_IfTrue())->as_Proj();

  Node* limit = NULL;
  if (stride > 0) {
    limit = cmpu->in(2);
  } else {
    limit = _igvn.makecon(TypeInt::ZERO);
    set_ctrl(limit, C->root());
  }
  // Create a new region on the exit path
  RegionNode* reg = insert_region_before_proj(lp_exit);

  // Clone the if-cmpu-true-false using a signed compare
  BoolTest::mask rel_i = stride > 0 ? bol->_test._test : BoolTest::ge;
  ProjNode* cmpi_exit = insert_if_before_proj(cmpu->in(1), Signed, rel_i, limit, lp_continue);
  reg->add_req(cmpi_exit);

  // Clone the if-cmpu-true-false
  BoolTest::mask rel_u = bol->_test._test;
  ProjNode* cmpu_exit = insert_if_before_proj(cmpu->in(1), Unsigned, rel_u, cmpu->in(2), lp_continue);
  reg->add_req(cmpu_exit);

  // Force original if to stay in loop.
  short_circuit_if(if_cmpu, lp_continue);

  return cmpi_exit->in(0)->as_If();
}

//------------------------------ remove_cmpi_loop_exit -------------------------------------
// Remove a previously inserted signed compare loop exit.
void PhaseIdealLoop::remove_cmpi_loop_exit(IfNode* if_cmp, IdealLoopTree *loop) {
  Node* lp_proj = stay_in_loop(if_cmp, loop);
  assert(if_cmp->in(1)->in(1)->Opcode() == Op_CmpI &&
         stay_in_loop(lp_proj, loop)->is_If() &&
         stay_in_loop(lp_proj, loop)->in(1)->in(1)->Opcode() == Op_CmpU, "inserted cmpi before cmpu");
  Node *con = _igvn.makecon(lp_proj->is_IfTrue() ? TypeInt::ONE : TypeInt::ZERO);
  set_ctrl(con, C->root());
  if_cmp->set_req(1, con);
}

//------------------------------ scheduled_nodelist -------------------------------------
// Create a post order schedule of nodes that are in the
// "member" set.  The list is returned in "sched".
// The first node in "sched" is the loop head, followed by
// nodes which have no inputs in the "member" set, and then
// followed by the nodes that have an immediate input dependence
// on a node in "sched".
void PhaseIdealLoop::scheduled_nodelist( IdealLoopTree *loop, VectorSet& member, Node_List &sched ) {

  assert(member.test(loop->_head->_idx), "loop head must be in member set");
  Arena *a = Thread::current()->resource_area();
  VectorSet visited(a);
  Node_Stack nstack(a, loop->_body.size());

  Node* n  = loop->_head;  // top of stack is cached in "n"
  uint idx = 0;
  visited.set(n->_idx);

  // Initially push all with no inputs from within member set
  for(uint i = 0; i < loop->_body.size(); i++ ) {
    Node *elt = loop->_body.at(i);
    if (member.test(elt->_idx)) {
      bool found = false;
      for (uint j = 0; j < elt->req(); j++) {
        Node* def = elt->in(j);
        if (def && member.test(def->_idx) && def != elt) {
          found = true;
          break;
        }
      }
      if (!found && elt != loop->_head) {
        nstack.push(n, idx);
        n = elt;
        assert(!visited.test(n->_idx), "not seen yet");
        visited.set(n->_idx);
      }
    }
  }

  // traverse out's that are in the member set
  while (true) {
    if (idx < n->outcnt()) {
      Node* use = n->raw_out(idx);
      idx++;
      if (!visited.test_set(use->_idx)) {
        if (member.test(use->_idx)) {
          nstack.push(n, idx);
          n = use;
          idx = 0;
        }
      }
    } else {
      // All outputs processed
      sched.push(n);
      if (nstack.is_empty()) break;
      n   = nstack.node();
      idx = nstack.index();
      nstack.pop();
    }
  }
}


//------------------------------ has_use_in_set -------------------------------------
// Has a use in the vector set
bool PhaseIdealLoop::has_use_in_set( Node* n, VectorSet& vset ) {
  for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
    Node* use = n->fast_out(j);
    if (vset.test(use->_idx)) {
      return true;
    }
  }
  return false;
}


//------------------------------ has_use_internal_to_set -------------------------------------
// Has use internal to the vector set (ie. not in a phi at the loop head)
bool PhaseIdealLoop::has_use_internal_to_set( Node* n, VectorSet& vset, IdealLoopTree *loop ) {
  Node* head  = loop->_head;
  for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
    Node* use = n->fast_out(j);
    if (vset.test(use->_idx) && !(use->is_Phi() && use->in(0) == head)) {
      return true;
    }
  }
  return false;
}


//------------------------------ clone_for_use_outside_loop -------------------------------------
// clone "n" for uses that are outside of loop
void PhaseIdealLoop::clone_for_use_outside_loop( IdealLoopTree *loop, Node* n, Node_List& worklist ) {

  assert(worklist.size() == 0, "should be empty");
  for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
    Node* use = n->fast_out(j);
    if( !loop->is_member(get_loop(has_ctrl(use) ? get_ctrl(use) : use)) ) {
      worklist.push(use);
    }
  }
  while( worklist.size() ) {
    Node *use = worklist.pop();
    if (!has_node(use) || use->in(0) == C->top()) continue;
    uint j;
    for (j = 0; j < use->req(); j++) {
      if (use->in(j) == n) break;
    }
    assert(j < use->req(), "must be there");

    // clone "n" and insert it between the inputs of "n" and the use outside the loop
    Node* n_clone = n->clone();
    _igvn.hash_delete(use);
    use->set_req(j, n_clone);
    _igvn._worklist.push(use);
    if (!use->is_Phi()) {
      Node* use_c = has_ctrl(use) ? get_ctrl(use) : use->in(0);
      set_ctrl(n_clone, use_c);
      assert(!loop->is_member(get_loop(use_c)), "should be outside loop");
      get_loop(use_c)->_body.push(n_clone);
    } else {
      // Use in a phi is considered a use in the associated predecessor block
      Node *prevbb = use->in(0)->in(j);
      set_ctrl(n_clone, prevbb);
      assert(!loop->is_member(get_loop(prevbb)), "should be outside loop");
      get_loop(prevbb)->_body.push(n_clone);
    }
    _igvn.register_new_node_with_optimizer(n_clone);
#if !defined(PRODUCT)
    if (TracePartialPeeling) {
C2OUT->print_cr("loop exit cloning old: %d new: %d newbb: %d",n->_idx,n_clone->_idx,get_ctrl(n_clone)->_idx);
    }
#endif
  }
}


//------------------------------ clone_for_special_use_inside_loop -------------------------------------
// clone "n" for special uses that are in the not_peeled region.
// If these def-uses occur in separate blocks, the code generator
// marks the method as not compilable.  For example, if a "BoolNode"
// is in a different basic block than the "IfNode" that uses it, then
// the compilation is aborted in the code generator.
void PhaseIdealLoop::clone_for_special_use_inside_loop( IdealLoopTree *loop, Node* n,
                                                        VectorSet& not_peel, Node_List& sink_list, Node_List& worklist ) {
  if (n->is_Phi() || n->is_Load()) {
    return;
  }
  assert(worklist.size() == 0, "should be empty");
  for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
    Node* use = n->fast_out(j);
    if ( not_peel.test(use->_idx) &&
         (use->is_If() || use->is_CMove() || use->is_Bool()) &&
         use->in(1) == n)  {
      worklist.push(use);
    }
  }
  if (worklist.size() > 0) {
    // clone "n" and insert it between inputs of "n" and the use
    Node* n_clone = n->clone();
    loop->_body.push(n_clone);
    _igvn.register_new_node_with_optimizer(n_clone);
    set_ctrl(n_clone, get_ctrl(n));
    sink_list.push(n_clone);
    not_peel <<= n_clone->_idx;  // add n_clone to not_peel set.
#if !defined(PRODUCT)
    if (TracePartialPeeling) {
C2OUT->print_cr("special not_peeled cloning old: %d new: %d",n->_idx,n_clone->_idx);
    }
#endif
    while( worklist.size() ) {
      Node *use = worklist.pop();
      _igvn.hash_delete(use);
      _igvn._worklist.push(use);
      for (uint j = 1; j < use->req(); j++) {
        if (use->in(j) == n) {
          use->set_req(j, n_clone);
        }
      }
    }
  }
}


//------------------------------ insert_phi_for_loop -------------------------------------
// Insert phi(lp_entry_val, back_edge_val) at use->in(idx) for loop lp if phi does not already exist
void PhaseIdealLoop::insert_phi_for_loop( Node* use, uint idx, Node* lp_entry_val, Node* back_edge_val, LoopNode* lp ) {
  Node *phi = PhiNode::make(lp, back_edge_val);
  phi->set_req(LoopNode::EntryControl, lp_entry_val);
  // Use existing phi if it already exists
  Node *hit = _igvn.hash_find_insert(phi);
  if( hit == NULL ) {
    _igvn.register_new_node_with_optimizer(phi);
    set_ctrl(phi, lp);
  } else {
    // Remove the new phi from the graph and use the hit
    _igvn.remove_dead_node(phi);
    phi = hit;
  }
  _igvn.hash_delete(use);
  _igvn._worklist.push(use);
  use->set_req(idx, phi);
}

#ifdef ASSERT
//------------------------------ is_valid_loop_partition -------------------------------------
// Validate the loop partition sets: peel and not_peel
bool PhaseIdealLoop::is_valid_loop_partition( IdealLoopTree *loop, VectorSet& peel, Node_List& peel_list,
                                              VectorSet& not_peel ) {
  uint i;
  // Check that peel_list entries are in the peel set
  for (i = 0; i < peel_list.size(); i++) {
    if (!peel.test(peel_list.at(i)->_idx)) {
      return false;
    }
  }
  // Check at loop members are in one of peel set or not_peel set
  for (i = 0; i < loop->_body.size(); i++ ) {
    Node *def  = loop->_body.at(i);
    uint di = def->_idx;
    // Check that peel set elements are in peel_list
    if (peel.test(di)) {
      if (not_peel.test(di)) {
        return false;
      }
      // Must be in peel_list also
      bool found = false;
      for (uint j = 0; j < peel_list.size(); j++) {
        if (peel_list.at(j)->_idx == di) {
          found = true;
          break;
        }
      }
      if (!found) {
        return false;
      }
    } else if (not_peel.test(di)) {
      if (peel.test(di)) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

//------------------------------ is_valid_clone_loop_exit_use -------------------------------------
// Ensure a use outside of loop is of the right form
bool PhaseIdealLoop::is_valid_clone_loop_exit_use( IdealLoopTree *loop, Node* use, uint exit_idx) {
  Node *use_c = has_ctrl(use) ? get_ctrl(use) : use;
  return (use->is_Phi() &&
          use_c->is_Region() && use_c->req() == 3 &&
          (use_c->in(exit_idx)->Opcode() == Op_IfTrue ||
           use_c->in(exit_idx)->Opcode() == Op_IfFalse ||
           use_c->in(exit_idx)->Opcode() == Op_JumpProj) &&
          loop->is_member( get_loop( use_c->in(exit_idx)->in(0) ) ) );
}

//------------------------------ is_valid_clone_loop_form -------------------------------------
// Ensure that all uses outside of loop are of the right form
bool PhaseIdealLoop::is_valid_clone_loop_form( IdealLoopTree *loop, Node_List& peel_list,
                                               uint orig_exit_idx, uint clone_exit_idx) {
  uint len = peel_list.size();
  for (uint i = 0; i < len; i++) {
    Node *def = peel_list.at(i);

    for (DUIterator_Fast jmax, j = def->fast_outs(jmax); j < jmax; j++) {
      Node *use = def->fast_out(j);
      Node *use_c = has_ctrl(use) ? get_ctrl(use) : use;
      if (!loop->is_member(get_loop(use_c))) {
        // use is not in the loop, check for correct structure
        if (use->in(0) == def) {
          // Okay
        } else if (!is_valid_clone_loop_exit_use(loop, use, orig_exit_idx)) {
          return false;
        }
      }
    }
  }
  return true;
}
#endif

//------------------------------ partial_peel -------------------------------------
// Partially peel (aka loop rotation) the top portion of a loop (called
// the peel section below) by cloning it and placing one copy just before
// the new loop head and the other copy at the bottom of the new loop.
//
//    before                       after                where it came from
//
//    stmt1                        stmt1
//  loop:                          stmt2                     clone
//    stmt2                        if condA goto exitA       clone
//    if condA goto exitA        new_loop:                   new
//    stmt3                        stmt3                     clone
//    if !condB goto loop          if condB goto exitB       clone
//  exitB:                         stmt2                     orig
//    stmt4                        if !condA goto new_loop   orig
//  exitA:                         goto exitA
//                               exitB:
//                                 stmt4
//                               exitA:
//
// Step 1: find the cut point: an exit test on probable
//         induction variable.
// Step 2: schedule (with cloning) operations in the peel
//         section that can be executed after the cut into
//         the section that is not peeled.  This may need
//         to clone operations into exit blocks.  For
//         instance, a reference to A[i] in the not-peel
//         section and a reference to B[i] in an exit block
//         may cause a left-shift of i by 2 to be placed
//         in the peel block.  This step will clone the left
//         shift into the exit block and sink the left shift
//         from the peel to the not-peel section.
// Step 3: clone the loop, retarget the control, and insert
//         phis for values that are live across the new loop
//         head.  This is very dependent on the graph structure
//         from clone_loop.  It creates region nodes for
//         exit control and associated phi nodes for values
//         flow out of the loop through that exit.  The region
//         node is dominated by the clone's control projection.
//         So the clone's peel section is placed before the
//         new loop head, and the clone's not-peel section is
//         forms the top part of the new loop.  The original
//         peel section forms the tail of the new loop.
// Step 4: update the dominator tree and recompute the
//         dominator depth.
//
//                   orig
//
//                  stmt1
//                    |
//                    v
//                   loop<----+
//                     |      |
//                   stmt2    |
//                     |      |
//                     v      |
//                    ifA     |
//                   / |      |
//                  v  v      |
//               false true   ^  <-- last_peel
//               /     |      |
//              /   ===|==cut |
//             /     stmt3    |  <-- first_not_peel
//            /        |      |
//            |        v      |
//            v       ifB     |
//          exitA:   / \      |
//                  /   \     |
//                 v     v    |
//               false true   |
//               /       \    |
//              /         ----+
//             |
//             v
//           exitB:
//           stmt4
//
//
//            after clone loop
//
//                   stmt1
//                 /       \
//        clone   /         \   orig
//               /           \
//              /             \
//             v               v
//   +---->loop                loop<----+
//   |      |                    |      |
//   |    stmt2                stmt2    |
//   |      |                    |      |
//   |      v                    v      |
//   |      ifA                 ifA     |
//   |      | \                / |      |
//   |      v  v              v  v      |
//   ^    true  false      false true   ^  <-- last_peel
//   |      |   ^   \       /    |      |
//   | cut==|==  \   \     /  ===|==cut |
//   |    stmt3   \   \   /    stmt3    |  <-- first_not_peel
//   |      |    dom   | |       |      |
//   |      v      \  1v v2      v      |
//   |      ifB     regionA     ifB     |
//   |      / \        |       / \      |
//   |     /   \       v      /   \     |
//   |    v     v    exitA:  v     v    |
//   |    true  false      false true   |
//   |    /     ^   \      /       \    |
//   +----       \   \    /         ----+
//               dom  \  /
//                 \  1v v2
//                  regionB
//                     |
//                     v
//                   exitB:
//                   stmt4
//
//
//           after partial peel
//
//                  stmt1
//                 /
//        clone   /             orig
//               /          TOP
//              /             \
//             v               v
//    TOP->region             region----+
//          |                    |      |
//        stmt2                stmt2    |
//          |                    |      |
//          v                    v      |
//          ifA                 ifA     |
//          | \                / |      |
//          v  v              v  v      |
//        true  false      false true   |     <-- last_peel
//          |   ^   \       /    +------|---+
//  +->newloop   \   \     /  === ==cut |   |
//  |     stmt3   \   \   /     TOP     |   |
//  |       |    dom   | |      stmt3   |   | <-- first_not_peel
//  |       v      \  1v v2      v      |   |
//  |       ifB     regionA     ifB     ^   v
//  |       / \        |       / \      |   |
//  |      /   \       v      /   \     |   |
//  |     v     v    exitA:  v     v    |   |
//  |     true  false      false true   |   |
//  |     /     ^   \      /       \    |   |
//  |    |       \   \    /         v   |   |
//  |    |       dom  \  /         TOP  |   |
//  |    |         \  1v v2             |   |
//  ^    v          regionB             |   |
//  |    |             |                |   |
//  |    |             v                ^   v
//  |    |           exitB:             |   |
//  |    |           stmt4              |   |
//  |    +------------>-----------------+   |
//  |                                       |
//  +-----------------<---------------------+
//
//
//              final graph
//
//                  stmt1
//                    |
//                    v
//         ........> ifA clone
//         :        / |
//        dom      /  |
//         :      v   v
//         :  false   true
//         :  |       |
//         :  |     stmt2 clone
//         :  |       |
//         :  |       v
//         :  |    newloop<-----+
//         :  |        |        |
//         :  |     stmt3 clone |
//         :  |        |        |
//         :  |        v        |
//         :  |       ifB       |
//         :  |      / \        |
//         :  |     v   v       |
//         :  |  false true     |
//         :  |   |     |       |
//         :  |   v    stmt2    |
//         :  | exitB:  |       |
//         :  | stmt4   v       |
//         :  |       ifA orig  |
//         :  |      /  \       |
//         :  |     /    \      |
//         :  |    v     v      |
//         :  |  false  true    |
//         :  |  /        \     |
//         :  v  v         -----+
//          RegionA
//             |
//             v
//           exitA
//
bool PhaseIdealLoop::partial_peel( IdealLoopTree *loop, Node_List &old_new ) {

  LoopNode *head  = loop->_head->as_Loop();

  if (head->is_partial_peel_loop() || head->partial_peel_has_failed()) {
    return false;
  }

  // Check for complex exit control
  for(uint ii = 0; ii < loop->_body.size(); ii++ ) {
    Node *n = loop->_body.at(ii);
    int opc = n->Opcode();
    if (n->is_Call()        ||
        opc == Op_Catch     ||
        opc == Op_CatchProj ||
        opc == Op_Jump      ||
        opc == Op_JumpProj) {
#if !defined(PRODUCT)
      if (TracePartialPeeling) {
C2OUT->print_cr("\nExit control too complex: lp: %d",head->_idx);
      }
#endif
      return false;
    }
  }

  int dd = dom_depth(head);

  // Step 1: find cut point

  // Walk up dominators to loop head looking for first loop exit
  // which is executed on every path thru loop.
  IfNode *peel_if = NULL;
  IfNode *peel_if_cmpu = NULL;

  Node *iff = loop->tail();
  while( iff != head ) {
    if( iff->is_If() ) {
      Node *ctrl = get_ctrl(iff->in(1));
      if (ctrl->is_top()) return false; // Dead test on live IF.
      // If loop-varying exit-test, check for induction variable
      if( loop->is_member(get_loop(ctrl)) &&
          loop->is_loop_exit(iff) &&
          is_possible_iv_test(iff)) {
        Node* cmp = iff->in(1)->in(1);
        if (cmp->Opcode() == Op_CmpI) {
          peel_if = iff->as_If();
        } else {
          assert(cmp->Opcode() == Op_CmpU, "must be CmpI or CmpU");
          peel_if_cmpu = iff->as_If();
        }
      }
    }
    iff = idom(iff);
  }
  // Prefer signed compare over unsigned compare.
  IfNode* new_peel_if = NULL;
  if (peel_if == NULL) {
    if (!PartialPeelAtUnsignedTests || peel_if_cmpu == NULL) {
      return false;   // No peel point found
    }
    new_peel_if = insert_cmpi_loop_exit(peel_if_cmpu, loop);
    if (new_peel_if == NULL) {
      return false;   // No peel point found
    }
    peel_if = new_peel_if;
  }
  Node* last_peel        = stay_in_loop(peel_if, loop);
  Node* first_not_peeled = stay_in_loop(last_peel, loop);
  if (first_not_peeled == NULL || first_not_peeled == head) {
    return false;
  }

#if !defined(PRODUCT)
  if (TracePartialPeeling) {
C2OUT->print_cr("before partial peel one iteration");
    Node_List wl;
    Node* t = head->in(2);
    while (true) {
      wl.push(t);
      if (t == head) break;
      t = idom(t);
    }
    while (wl.size() > 0) {
      Node* tt = wl.pop();
      tt->dump();
if(tt==last_peel)C2OUT->print_cr("-- cut --");
    }
  }
#endif
  ResourceArea *area = Thread::current()->resource_area();
  VectorSet peel(area);
  VectorSet not_peel(area);
  Node_List peel_list(area);
  Node_List worklist(area);
  Node_List sink_list(area);

  // Set of cfg nodes to peel are those that are executable from
  // the head through last_peel.
  assert(worklist.size() == 0, "should be empty");
  worklist.push(head);
  peel.set(head->_idx);
  while (worklist.size() > 0) {
    Node *n = worklist.pop();
    if (n != last_peel) {
      for (DUIterator_Fast jmax, j = n->fast_outs(jmax); j < jmax; j++) {
        Node* use = n->fast_out(j);
        if (use->is_CFG() &&
            loop->is_member(get_loop(use)) &&
            !peel.test_set(use->_idx)) {
          worklist.push(use);
        }
      }
    }
  }

  // Set of non-cfg nodes to peel are those that are control
  // dependent on the cfg nodes.
  uint i;
  for(i = 0; i < loop->_body.size(); i++ ) {
    Node *n = loop->_body.at(i);
    Node *n_c = has_ctrl(n) ? get_ctrl(n) : n;
    if (peel.test(n_c->_idx)) {
      peel.set(n->_idx);
    } else {
      not_peel.set(n->_idx);
    }
  }

  // Step 2: move operations from the peeled section down into the
  //         not-peeled section

  // Get a post order schedule of nodes in the peel region
  // Result in right-most operand.
  scheduled_nodelist(loop, peel, peel_list );

  assert(is_valid_loop_partition(loop, peel, peel_list, not_peel), "bad partition");

  // For future check for too many new phis
  uint old_phi_cnt = 0;
  for (DUIterator_Fast jmax, j = head->fast_outs(jmax); j < jmax; j++) {
    Node* use = head->fast_out(j);
    if (use->is_Phi()) old_phi_cnt++;
  }

#if !defined(PRODUCT)
  if (TracePartialPeeling) {
C2OUT->print_cr("\npeeled list");
  }
#endif

  // Evacuate nodes in peel region into the not_peeled region if possible
  uint new_phi_cnt = 0;
  for (i = 0; i < peel_list.size();) {
    Node* n = peel_list.at(i);
#if !defined(PRODUCT)
  if (TracePartialPeeling) n->dump();
#endif
    bool incr = true;
    if ( !n->is_CFG() ) {

      if ( has_use_in_set(n, not_peel) ) {

        // If not used internal to the peeled region,
        // move "n" from peeled to not_peeled region.

        if ( !has_use_internal_to_set(n, peel, loop) ) {

          // if not pinned and not a load (which maybe anti-dependent on a store)
          // and not a CMove (Matcher expects only bool->cmove).
          if ( n->in(0) == NULL && !n->is_Load() && !n->is_CMove() ) {
            clone_for_use_outside_loop( loop, n, worklist );

            sink_list.push(n);
            peel     >>= n->_idx; // delete n from peel set.
            not_peel <<= n->_idx; // add n to not_peel set.
            peel_list.remove(i);
            incr = false;
#if !defined(PRODUCT)
            if (TracePartialPeeling) {
C2OUT->print_cr("sink to not_peeled region: %d newbb: %d",
                            n->_idx, get_ctrl(n)->_idx);
            }
#endif
          }
        } else {
          // Otherwise check for special def-use cases that span
          // the peel/not_peel boundary such as bool->if
          clone_for_special_use_inside_loop( loop, n, not_peel, sink_list, worklist );
          new_phi_cnt++;
        }
      }
    }
    if (incr) i++;
  }

  if (new_phi_cnt > old_phi_cnt + PartialPeelNewPhiDelta) {
#if !defined(PRODUCT)
    if (TracePartialPeeling) {
C2OUT->print_cr("\nToo many new phis: %d  old %d new cmpi: %c",
                    new_phi_cnt, old_phi_cnt, new_peel_if != NULL?'T':'F');
    }
#endif
    if (new_peel_if != NULL) {
      remove_cmpi_loop_exit(new_peel_if, loop);
    }
    // Inhibit more partial peeling on this loop
    assert(!head->is_partial_peel_loop(), "not partial peeled");
    head->mark_partial_peel_failed();
    return false;
  }

  // Step 3: clone loop, retarget control, and insert new phis

  // Create new loop head for new phis and to hang
  // the nodes being moved (sinked) from the peel region.
  LoopNode* new_head = new (C, 3) LoopNode(last_peel, last_peel);
  _igvn.register_new_node_with_optimizer(new_head);
  assert(first_not_peeled->in(0) == last_peel, "last_peel <- first_not_peeled");
  first_not_peeled->set_req(0, new_head);
  set_loop(new_head, loop);
  loop->_body.push(new_head);
  not_peel.set(new_head->_idx);
  set_idom(new_head, last_peel, dom_depth(first_not_peeled));
  set_idom(first_not_peeled, new_head, dom_depth(first_not_peeled));

  while (sink_list.size() > 0) {
    Node* n = sink_list.pop();
    set_ctrl(n, new_head);
  }

  assert(is_valid_loop_partition(loop, peel, peel_list, not_peel), "bad partition");

  clone_loop( loop, old_new, dd, NULL, false );

  const uint clone_exit_idx = 1;
  const uint orig_exit_idx  = 2;
  assert(is_valid_clone_loop_form( loop, peel_list, orig_exit_idx, clone_exit_idx ), "bad clone loop");

  Node* head_clone             = old_new[head->_idx];
  LoopNode* new_head_clone     = old_new[new_head->_idx]->as_Loop();
  Node* orig_tail_clone        = head_clone->in(2);

  // Add phi if "def" node is in peel set and "use" is not

  for(i = 0; i < peel_list.size(); i++ ) {
    Node *def  = peel_list.at(i);
    if (!def->is_CFG()) {
      for (DUIterator_Fast jmax, j = def->fast_outs(jmax); j < jmax; j++) {
        Node *use = def->fast_out(j);
        if (has_node(use) && use->in(0) != C->top() &&
            (!peel.test(use->_idx) ||
             (use->is_Phi() && use->in(0) == head)) ) {
          worklist.push(use);
        }
      }
      while( worklist.size() ) {
        Node *use = worklist.pop();
        for (uint j = 1; j < use->req(); j++) {
          Node* n = use->in(j);
          if (n == def) {

            // "def" is in peel set, "use" is not in peel set
            // or "use" is in the entry boundary (a phi) of the peel set

            Node* use_c = has_ctrl(use) ? get_ctrl(use) : use;

            if ( loop->is_member(get_loop( use_c )) ) {
              // use is in loop
              if (old_new[use->_idx] != NULL) { // null for dead code
                Node* use_clone = old_new[use->_idx];
                _igvn.hash_delete(use);
                use->set_req(j, C->top());
                _igvn._worklist.push(use);
                insert_phi_for_loop( use_clone, j, old_new[def->_idx], def, new_head_clone );
              }
            } else {
              assert(is_valid_clone_loop_exit_use(loop, use, orig_exit_idx), "clone loop format");
              // use is not in the loop, check if the live range includes the cut
              Node* lp_if = use_c->in(orig_exit_idx)->in(0);
              if (not_peel.test(lp_if->_idx)) {
                assert(j == orig_exit_idx, "use from original loop");
                insert_phi_for_loop( use, clone_exit_idx, old_new[def->_idx], def, new_head_clone );
              }
            }
          }
        }
      }
    }
  }

  // Step 3b: retarget control

  // Redirect control to the new loop head if a cloned node in
  // the not_peeled region has control that points into the peeled region.
  // This necessary because the cloned peeled region will be outside
  // the loop.
  //                            from    to
  //          cloned-peeled    <---+
  //    new_head_clone:            |    <--+
  //          cloned-not_peeled  in(0)    in(0)
  //          orig-peeled

  for(i = 0; i < loop->_body.size(); i++ ) {
    Node *n = loop->_body.at(i);
    if (!n->is_CFG()           && n->in(0) != NULL        &&
        not_peel.test(n->_idx) && peel.test(n->in(0)->_idx)) {
      Node* n_clone = old_new[n->_idx];
      _igvn.hash_delete(n_clone);
      n_clone->set_req(0, new_head_clone);
      _igvn._worklist.push(n_clone);
    }
  }

  // Backedge of the surviving new_head (the clone) is original last_peel
  _igvn.hash_delete(new_head_clone);
  new_head_clone->set_req(LoopNode::LoopBackControl, last_peel);
  _igvn._worklist.push(new_head_clone);

  // Cut first node in original not_peel set
  _igvn.hash_delete(new_head);
  new_head->set_req(LoopNode::EntryControl, C->top());
  new_head->set_req(LoopNode::LoopBackControl, C->top());
  _igvn._worklist.push(new_head);

  // Copy head_clone back-branch info to original head
  // and remove original head's loop entry and
  // clone head's back-branch
  _igvn.hash_delete(head);
  _igvn.hash_delete(head_clone);
  head->set_req(LoopNode::EntryControl, head_clone->in(LoopNode::LoopBackControl));
  head->set_req(LoopNode::LoopBackControl, C->top());
  head_clone->set_req(LoopNode::LoopBackControl, C->top());
  _igvn._worklist.push(head);
  _igvn._worklist.push(head_clone);

  // Similarly modify the phis
  for (DUIterator_Fast kmax, k = head->fast_outs(kmax); k < kmax; k++) {
    Node* use = head->fast_out(k);
    if (use->is_Phi() && use->outcnt() > 0) {
      Node* use_clone = old_new[use->_idx];
      _igvn.hash_delete(use);
      _igvn.hash_delete(use_clone);
      use->set_req(LoopNode::EntryControl, use_clone->in(LoopNode::LoopBackControl));
      use->set_req(LoopNode::LoopBackControl, C->top());
      use_clone->set_req(LoopNode::LoopBackControl, C->top());
      _igvn._worklist.push(use);
      _igvn._worklist.push(use_clone);
    }
  }

  // Step 4: update dominator tree and dominator depth

  set_idom(head, orig_tail_clone, dd);
  recompute_dom_depth();

  // Inhibit more partial peeling on this loop
  new_head_clone->set_partial_peel_loop();
  C->set_major_progress();

#if !defined(PRODUCT)
  if (TracePartialPeeling) {
C2OUT->print_cr("\nafter partial peel one iteration");
    Node_List wl(area);
    Node* t = last_peel;
    while (true) {
      wl.push(t);
      if (t == head_clone) break;
      t = idom(t);
    }
    while (wl.size() > 0) {
      Node* tt = wl.pop();
if(tt==head)C2OUT->print_cr("orig head");
else if(tt==new_head_clone)C2OUT->print_cr("new head");
else if(tt==head_clone)C2OUT->print_cr("clone head");
      tt->dump();
    }
  }
#endif
  return true;
}

//------------------------------reorg_offsets----------------------------------
// Reorganize offset computations to lower register pressure.  Mostly
// prevent loop-fallout uses of the pre-incremented trip counter (which are
// then alive with the post-incremented trip counter forcing an extra
// register move)
void PhaseIdealLoop::reorg_offsets( IdealLoopTree *loop ) {

  CountedLoopNode *cl = loop->_head->as_CountedLoop();
  CountedLoopEndNode *cle = cl->loopexit();
if(!cle||cle->outcnt()!=2)return;//The occasional dead loop
  // Find loop exit control
  Node *exit = cle->proj_out(false);
  assert( exit->Opcode() == Op_IfFalse, "" );

  // Check for the special case of folks using the pre-incremented
  // trip-counter on the fall-out path (forces the pre-incremented
  // and post-incremented trip counter to be live at the same time).
  // Fix this by adjusting to use the post-increment trip counter.
Node*phi=cl->phi();//Pre-increment trip counter
  if( !phi ) return;            // Dead infinite loop
Node*opaq=NULL;//Lazily created
Node*post=NULL;
  bool progress = true;
  while (progress) {
    progress = false;
    for (DUIterator_Fast imax, i = phi->fast_outs(imax); i < imax; i++) {
      Node* use = phi->fast_out(i);   // User of trip-counter
      if (!has_ctrl(use))  continue;
      Node *u_ctrl = get_ctrl(use);
      if( use->is_Phi() ) {
        u_ctrl = NULL;
        for( uint j = 1; j < use->req(); j++ )
          if( use->in(j) == phi )
            u_ctrl = dom_lca( u_ctrl, use->in(0)->in(j) );
      }
      IdealLoopTree *u_loop = get_loop(u_ctrl);
      // Look for loop-invariant use
      if( u_loop == loop ) continue;
      if( loop->is_member( u_loop ) ) continue;
      // Check that use is live out the bottom.  Assuming the trip-counter
      // update is right at the bottom, uses of the loop middle are ok.
      if( dom_lca( exit, u_ctrl ) != exit ) continue;
      // protect against stride not being a constant
      if( !cle->stride_is_con() ) continue;
      // Hit!  If u_ctrl and u_loop are deeply nested, we'd like the
      // computation to end up outside as many use-loops as possible.
      while( !u_loop->is_member(loop) ) {
        u_ctrl = u_loop->_head->in(LoopNode::EntryControl);
        u_loop = get_loop(u_ctrl);
      }
      // Refactor use to use the post-incremented tripcounter.
      // Compute a post-increment tripcounter.
if(opaq==NULL){
        opaq = new (C, 2) Opaque2Node( cle->incr() );
        register_new_node( opaq, u_ctrl );
        Node *neg_stride = _igvn.intcon(-cle->stride_con());
        set_ctrl(neg_stride, C->root());
post=new(C,3)AddINode(opaq,neg_stride);
        register_new_node( post, u_ctrl );
      }
      _igvn.hash_delete(use);
      _igvn._worklist.push(use);
      for( uint j = 1; j < use->req(); j++ )
        if( use->in(j) == phi )
          use->set_req(j, post);
      // Since DU info changed, rerun loop
      progress = true;
      break;
    }
  }

}
