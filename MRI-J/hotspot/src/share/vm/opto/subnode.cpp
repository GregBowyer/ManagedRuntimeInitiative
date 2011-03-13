/*
 * Copyright 1997-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


// Portions of code courtesy of Clifford Click

// Optimization - Graph Style

#include "addnode.hpp"
#include "assembler_pd.hpp"
#include "cfgnode.hpp"
#include "ciObjArrayKlass.hpp"
#include "connode.hpp"
#include "loopnode.hpp"
#include "machnode.hpp"
#include "math.h"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "phaseX.hpp"
#include "sharedRuntime.hpp"
#include "subnode.hpp"

//=============================================================================
//------------------------------Identity---------------------------------------
// If right input is a constant 0, return the left input.  
Node *SubNode::Identity( PhaseTransform *phase ) {
  assert(in(1) != this, "Must already have called Value");
  assert(in(2) != this, "Must already have called Value");

  // Remove double negation
  const Type *zero = add_id();
  if( phase->type( in(1) )->higher_equal( zero ) &&
      in(2)->Opcode() == Opcode() &&
      phase->type( in(2)->in(1) )->higher_equal( zero ) ) {
    return in(2)->in(2);
  }

  // Convert "(X+Y) - Y" into X
  if( in(1)->Opcode() == Op_AddI ) {
    if( phase->eqv(in(1)->in(2),in(2)) )
      return in(1)->in(1);
    // Also catch: "(X + Opaque2(Y)) - Y".  In this case, 'Y' is a loop-varying
    // trip counter and X is likely to be loop-invariant (that's how O2 Nodes
    // are originally used, although the optimizer sometimes jiggers things).
    // This folding through an O2 removes a loop-exit use of a loop-varying
    // value and generally lowers register pressure in and around the loop.
    if( in(1)->in(2)->Opcode() == Op_Opaque2 &&
        phase->eqv(in(1)->in(2)->in(1),in(2)) )
      return in(1)->in(1);
  }

  return ( phase->type( in(2) )->higher_equal( zero ) ) ? in(1) : this;
}

//------------------------------Value------------------------------------------
// A subtract node differences it's two inputs.  
const Type *SubNode::Value( PhaseTransform *phase ) const {
  const Node* in1 = in(1);
  const Node* in2 = in(2);
  // Either input is TOP ==> the result is TOP
  const Type* t1 = (in1 == this) ? Type::TOP : phase->type(in1);
  if( t1 == Type::TOP ) return Type::TOP;
  const Type* t2 = (in2 == this) ? Type::TOP : phase->type(in2);
  if( t2 == Type::TOP ) return Type::TOP;

  // Not correct for SubFnode and AddFNode (must check for infinity)
  // Equal?  Subtract is zero
  if (phase->eqv_uncast(in1, in2))  return add_id();

  // Either input is BOTTOM ==> the result is the local BOTTOM
  if( t1 == Type::BOTTOM || t2 == Type::BOTTOM ) 
    return bottom_type();

  return sub(t1,t2);            // Local flavor of type subtraction

}

//=============================================================================

//------------------------------Helper function--------------------------------
static bool ok_to_convert(Node* inc, Node* iv) {
    // Do not collapse (x+c0)-y if "+" is a loop increment, because the
    // "-" is loop invariant and collapsing extends the live-range of "x"
    // to overlap with the "+", forcing another register to be used in
    // the loop.
    // This test will be clearer with '&&' (apply DeMorgan's rule)
    // but I like the early cutouts that happen here.
    const PhiNode *phi;
    if( ( !inc->in(1)->is_Phi() ||
          !(phi=inc->in(1)->as_Phi()) ||
          phi->is_copy() ||
          !phi->region()->is_CountedLoop() ||
          inc != phi->region()->as_CountedLoop()->incr() )
       &&
        // Do not collapse (x+c0)-iv if "iv" is a loop induction variable,
        // because "x" maybe invariant.
        ( !iv->is_loop_iv() )
      ) { 
      return true;
    } else {
      return false;
    }
}
//------------------------------Ideal------------------------------------------
Node *SubINode::Ideal(PhaseGVN *phase, bool can_reshape){
  Node *in1 = in(1);
  Node *in2 = in(2);
  uint op1 = in1->Opcode();
  uint op2 = in2->Opcode();

#ifdef ASSERT
  // Check for dead loop
  if( phase->eqv( in1, this ) || phase->eqv( in2, this ) ||
((op1==Op_AddI||op1==Op_SubI)&&
      ( phase->eqv( in1->in(1), this ) || phase->eqv( in1->in(2), this ) ||
phase->eqv(in1->in(1),in1)||phase->eqv(in1->in(2),in1))))
    assert(false, "dead loop in SubINode::Ideal");
#endif

  const Type *t2 = phase->type( in2 );
  if( t2 == Type::TOP ) return NULL;
  // Convert "x-c0" into "x+ -c0".
  if( t2->base() == Type::Int ){        // Might be bottom or top...
    const TypeInt *i = t2->is_int();
    if( i->is_con() )
      return new (phase->C, 3) AddINode(in1, phase->intcon(-i->get_con()));
  }

  // Convert "(x+c0) - y" into (x-y) + c0"
  // Do not collapse (x+c0)-y if "+" is a loop increment or 
  // if "y" is a loop induction variable.
  if( op1 == Op_AddI && ok_to_convert(in1, in2) ) {
    const Type *tadd = phase->type( in1->in(2) );
    if( tadd->singleton() && tadd != Type::TOP ) {
      Node *sub2 = phase->transform( new (phase->C, 3) SubINode( in1->in(1), in2 ));
      return new (phase->C, 3) AddINode( sub2, in1->in(2) );
    }
  }


  // Convert "x - (y+c0)" into "(x-y) - c0"
  // Need the same check as in above optimization but reversed.
  if (op2 == Op_AddI && ok_to_convert(in2, in1)) {
    Node* in21 = in2->in(1);
    Node* in22 = in2->in(2);
    const TypeInt* tcon = phase->type(in22)->isa_int();
    if (tcon != NULL && tcon->is_con()) {
      Node* sub2 = phase->transform( new (phase->C, 3) SubINode(in1, in21) );
      Node* neg_c0 = phase->intcon(- tcon->get_con());
      return new (phase->C, 3) AddINode(sub2, neg_c0);
    }
  }

  const Type *t1 = phase->type( in1 );
  if( t1 == Type::TOP ) return NULL;

#ifdef ASSERT
  // Check for dead loop
  if( ( op2 == Op_AddI || op2 == Op_SubI ) && 
      ( phase->eqv( in2->in(1), this ) || phase->eqv( in2->in(2), this ) ||
        phase->eqv( in2->in(1), in2  ) || phase->eqv( in2->in(2), in2  ) ) )
    assert(false, "dead loop in SubINode::Ideal");
#endif

  // Convert "x - (x+y)" into "-y"
  if( op2 == Op_AddI &&
      phase->eqv( in1, in2->in(1) ) )
    return new (phase->C, 3) SubINode( phase->intcon(0),in2->in(2));
  // Convert "(x-y) - x" into "-y"
  if( op1 == Op_SubI &&
      phase->eqv( in1->in(1), in2 ) )
    return new (phase->C, 3) SubINode( phase->intcon(0),in1->in(2));
  // Convert "(Opaque2(x)-y) - x" into "-y".  Here it's OK to "peek" through
  // an Opaque2 node because the final result no longer relies on the
  // Opaque2's input.
  if( op1 == Op_SubI &&
in1->in(1)->Opcode()==Op_Opaque2&&
      phase->eqv( in1->in(1)->in(1), in2 ) )
    return new (phase->C, 3) SubINode( phase->intcon(0),in1->in(2));
  // Convert "x - (y+x)" into "-y"
  if( op2 == Op_AddI &&
      phase->eqv( in1, in2->in(2) ) )
    return new (phase->C, 3) SubINode( phase->intcon(0),in2->in(1));

  // Convert "0 - (x-y)" into "y-x"
  if( t1 == TypeInt::ZERO && op2 == Op_SubI ) 
    return new (phase->C, 3) SubINode( in2->in(2), in2->in(1) );

  // Convert "0 - (x+con)" into "-con-x"
  jint con;
  if( t1 == TypeInt::ZERO && op2 == Op_AddI &&
      (con = in2->in(2)->find_int_con(0)) != 0 )
    return new (phase->C, 3) SubINode( phase->intcon(-con), in2->in(1) );

  // Convert "(X+A) - (X+B)" into "A - B"
  if( op1 == Op_AddI && op2 == Op_AddI && in1->in(1) == in2->in(1) )
    return new (phase->C, 3) SubINode( in1->in(2), in2->in(2) );

  // Convert "(A+X) - (B+X)" into "A - B"
if(op1==Op_AddI&&op2==Op_AddI&&in1->in(2)==in2->in(2)&&
in1!=in1->in(1)&&
      in2 != in2->in(1) )
    return new (phase->C, 3) SubINode( in1->in(1), in2->in(1) );

  // Convert "A-(B-C)" into (A+C)-B", since add is commutative and generally
  // nicer to optimize than subtract.
  if( op2 == Op_SubI && in2->outcnt() == 1) {
    Node *add1 = phase->transform( new (phase->C, 3) AddINode( in1, in2->in(2) ) );
    return new (phase->C, 3) SubINode( add1, in2->in(1) );
  }

  return NULL;
}

//------------------------------sub--------------------------------------------
// A subtract node differences it's two inputs.  
const Type *SubINode::sub( const Type *t1, const Type *t2 ) const {
  const TypeInt *r0 = t1->is_int(); // Handy access
  const TypeInt *r1 = t2->is_int();
  int32 lo = r0->_lo - r1->_hi;
  int32 hi = r0->_hi - r1->_lo;

  // We next check for 32-bit overflow.
  // If that happens, we just assume all integers are possible.
  if( (((r0->_lo ^ r1->_hi) >= 0) ||    // lo ends have same signs OR
       ((r0->_lo ^      lo) >= 0)) &&   // lo results have same signs AND
      (((r0->_hi ^ r1->_lo) >= 0) ||    // hi ends have same signs OR
       ((r0->_hi ^      hi) >= 0)) )    // hi results have same signs
    return TypeInt::make(lo,hi,MAX2(r0->_widen,r1->_widen));
  else                          // Overflow; assume all integers
    return TypeInt::INT;
}

//=============================================================================
//------------------------------Ideal------------------------------------------
Node *SubLNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node *in1 = in(1);
  Node *in2 = in(2);
  uint op1 = in1->Opcode();
  uint op2 = in2->Opcode();

#ifdef ASSERT
  // Check for dead loop
  if( phase->eqv( in1, this ) || phase->eqv( in2, this ) ||
((op1==Op_AddL||op1==Op_SubL)&&
      ( phase->eqv( in1->in(1), this ) || phase->eqv( in1->in(2), this ) ||
phase->eqv(in1->in(1),in1)||phase->eqv(in1->in(2),in1))))
    assert(false, "dead loop in SubLNode::Ideal");
#endif

  if( phase->type( in2 ) == Type::TOP ) return NULL;
  const TypeLong *i = phase->type( in2 )->isa_long();
  // Convert "x-c0" into "x+ -c0".
  if( i &&                      // Might be bottom or top...
      i->is_con() )
    return new (phase->C, 3) AddLNode(in1, phase->longcon(-i->get_con()));

  // Convert "(x+c0) - y" into (x-y) + c0"
  // Do not collapse (x+c0)-y if "+" is a loop increment or 
  // if "y" is a loop induction variable.
  if( op1 == Op_AddL && ok_to_convert(in1, in2) ) {
    Node *in11 = in1->in(1);
    const Type *tadd = phase->type( in1->in(2) );
    if( tadd->singleton() && tadd != Type::TOP ) {
      Node *sub2 = phase->transform( new (phase->C, 3) SubLNode( in11, in2 ));
      return new (phase->C, 3) AddLNode( sub2, in1->in(2) );
    }
  }

  // Convert "x - (y+c0)" into "(x-y) - c0"
  // Need the same check as in above optimization but reversed.
  if (op2 == Op_AddL && ok_to_convert(in2, in1)) {
    Node* in21 = in2->in(1);
    Node* in22 = in2->in(2);
    const TypeLong* tcon = phase->type(in22)->isa_long();
    if (tcon != NULL && tcon->is_con()) {
      Node* sub2 = phase->transform( new (phase->C, 3) SubLNode(in1, in21) );
      Node* neg_c0 = phase->longcon(- tcon->get_con());
      return new (phase->C, 3) AddLNode(sub2, neg_c0);
    }
  }
                       
  const Type *t1 = phase->type( in1 );
  if( t1 == Type::TOP ) return NULL;

#ifdef ASSERT
  // Check for dead loop
  if( ( op2 == Op_AddL || op2 == Op_SubL ) && 
      ( phase->eqv( in2->in(1), this ) || phase->eqv( in2->in(2), this ) ||
        phase->eqv( in2->in(1), in2  ) || phase->eqv( in2->in(2), in2  ) ) )
    assert(false, "dead loop in SubLNode::Ideal");
#endif

  // Convert "x - (x+y)" into "-y"
  if( op2 == Op_AddL &&
      phase->eqv( in1, in2->in(1) ) )
    return new (phase->C, 3) SubLNode( phase->makecon(TypeLong::ZERO), in2->in(2));
  // Convert "x - (y+x)" into "-y"
  if( op2 == Op_AddL &&
      phase->eqv( in1, in2->in(2) ) )
    return new (phase->C, 3) SubLNode( phase->makecon(TypeLong::ZERO),in2->in(1));

  // Convert "0 - (x-y)" into "y-x"
  if( phase->type( in1 ) == TypeLong::ZERO && op2 == Op_SubL ) 
    return new (phase->C, 3) SubLNode( in2->in(2), in2->in(1) );

  // Convert "(X+A) - (X+B)" into "A - B"
  if( op1 == Op_AddL && op2 == Op_AddL && in1->in(1) == in2->in(1) )
    return new (phase->C, 3) SubLNode( in1->in(2), in2->in(2) );

  // Convert "(A+X) - (B+X)" into "A - B"
  if( op1 == Op_AddL && op2 == Op_AddL && in1->in(2) == in2->in(2) )
    return new (phase->C, 3) SubLNode( in1->in(1), in2->in(1) );

  // Convert "A-(B-C)" into (A+C)-B"
  if( op2 == Op_SubL && in2->outcnt() == 1) {
    Node *add1 = phase->transform( new (phase->C, 3) AddLNode( in1, in2->in(2) ) );
    return new (phase->C, 3) SubLNode( add1, in2->in(1) );
  }

  return NULL;
}

//------------------------------sub--------------------------------------------
// A subtract node differences it's two inputs.  
const Type *SubLNode::sub( const Type *t1, const Type *t2 ) const {
  const TypeLong *r0 = t1->is_long(); // Handy access
  const TypeLong *r1 = t2->is_long();
  jlong lo = r0->_lo - r1->_hi;
  jlong hi = r0->_hi - r1->_lo;

  // We next check for 32-bit overflow.
  // If that happens, we just assume all integers are possible.
  if( (((r0->_lo ^ r1->_hi) >= 0) ||    // lo ends have same signs OR
       ((r0->_lo ^      lo) >= 0)) &&   // lo results have same signs AND
      (((r0->_hi ^ r1->_lo) >= 0) ||    // hi ends have same signs OR
       ((r0->_hi ^      hi) >= 0)) )    // hi results have same signs
    return TypeLong::make(lo,hi,MAX2(r0->_widen,r1->_widen));
  else                          // Overflow; assume all integers
    return TypeLong::LONG;
}

//=============================================================================
//------------------------------Value------------------------------------------
// A subtract node differences its two inputs.  
const Type *SubFPNode::Value( PhaseTransform *phase ) const {
  const Node* in1 = in(1);
  const Node* in2 = in(2);
  // Either input is TOP ==> the result is TOP
  const Type* t1 = (in1 == this) ? Type::TOP : phase->type(in1);
  if( t1 == Type::TOP ) return Type::TOP;
  const Type* t2 = (in2 == this) ? Type::TOP : phase->type(in2);
  if( t2 == Type::TOP ) return Type::TOP;

  // if both operands are infinity of same sign, the result is NaN; do
  // not replace with zero
  if( (t1->is_finite() && t2->is_finite()) ) {
    if( phase->eqv(in1, in2) ) return add_id();
  }

  // Either input is BOTTOM ==> the result is the local BOTTOM
  const Type *bot = bottom_type();
  if( (t1 == bot) || (t2 == bot) ||
      (t1 == Type::BOTTOM) || (t2 == Type::BOTTOM) ) 
    return bot;

  return sub(t1,t2);            // Local flavor of type subtraction
}


//=============================================================================
//------------------------------Ideal------------------------------------------
Node *SubFNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  const Type *t2 = phase->type( in(2) );
  // Convert "x-c0" into "x+ -c0".
  if( t2->base() == Type::FloatCon ) {  // Might be bottom or top...
    // return new (phase->C, 3) AddFNode(in(1), phase->makecon( TypeF::make(-t2->getf()) ) );
  }
                       
  // Not associative because of boundary conditions (infinity)
  if( IdealizedNumerics && !phase->C->method()->is_strict() ) {
    // Convert "x - (x+y)" into "-y"
    if( in(2)->is_Add() &&
        phase->eqv(in(1),in(2)->in(1) ) ) 
      return new (phase->C, 3) SubFNode( phase->makecon(TypeF::ZERO),in(2)->in(2));
  }

  // Cannot replace 0.0-X with -X because a 'fsub' bytecode computes
  // 0.0-0.0 as +0.0, while a 'fneg' bytecode computes -0.0.
  //if( phase->type(in(1)) == TypeF::ZERO )
  //return new (phase->C, 2) NegFNode(in(2));

  return NULL;
}

//------------------------------sub--------------------------------------------
// A subtract node differences its two inputs.  
const Type *SubFNode::sub( const Type *t1, const Type *t2 ) const {
  // no folding if one of operands is infinity or NaN, do not do constant folding
  if( g_isfinite(t1->getf()) && g_isfinite(t2->getf()) ) {
    return TypeF::make( t1->getf() - t2->getf() );
  } 
  else if( g_isnan(t1->getf()) ) {
    return t1;
  } 
  else if( g_isnan(t2->getf()) ) {
    return t2;
  } 
  else {
    return Type::FLOAT;
  }
}

//=============================================================================
//------------------------------Ideal------------------------------------------
Node *SubDNode::Ideal(PhaseGVN *phase, bool can_reshape){
  const Type *t2 = phase->type( in(2) );
  // Convert "x-c0" into "x+ -c0".
  if( t2->base() == Type::DoubleCon ) { // Might be bottom or top...
    // return new (phase->C, 3) AddDNode(in(1), phase->makecon( TypeD::make(-t2->getd()) ) );
  }
                       
  // Not associative because of boundary conditions (infinity)
  if( IdealizedNumerics && !phase->C->method()->is_strict() ) { 
    // Convert "x - (x+y)" into "-y"
    if( in(2)->is_Add() &&
        phase->eqv(in(1),in(2)->in(1) ) ) 
      return new (phase->C, 3) SubDNode( phase->makecon(TypeD::ZERO),in(2)->in(2));
  }

  // Cannot replace 0.0-X with -X because a 'dsub' bytecode computes
  // 0.0-0.0 as +0.0, while a 'dneg' bytecode computes -0.0.
  //if( phase->type(in(1)) == TypeD::ZERO )
  //return new (phase->C, 2) NegDNode(in(2));

  return NULL;
}

//------------------------------sub--------------------------------------------
// A subtract node differences its two inputs.  
const Type *SubDNode::sub( const Type *t1, const Type *t2 ) const {
  // no folding if one of operands is infinity or NaN, do not do constant folding
  if( g_isfinite(t1->getd()) && g_isfinite(t2->getd()) ) {
    return TypeD::make( t1->getd() - t2->getd() );
  }
  else if( g_isnan(t1->getd()) ) {
    return t1;
  }
  else if( g_isnan(t2->getd()) ) {
    return t2;
  }
  else {
    return Type::DOUBLE;
  }
}

//=============================================================================
//------------------------------Idealize---------------------------------------
// Unlike SubNodes, compare must still flatten return value to the
// range -1, 0, 1.
// And optimizations like those for (X + Y) - X fail if overflow happens.
Node *CmpNode::Identity( PhaseTransform *phase ) {
  return this;
}

//=============================================================================
//------------------------------cmp--------------------------------------------
// Simplify a CmpI (compare 2 integers) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpINode::sub( const Type *t1, const Type *t2 ) const {
  const TypeInt *r0 = t1->is_int(); // Handy access
  const TypeInt *r1 = t2->is_int();

  if( r0->_hi < r1->_lo )       // Range is always low?
    return TypeInt::CC_LT;
  else if( r0->_lo > r1->_hi )  // Range is always high?
    return TypeInt::CC_GT;

  else if( r0->is_con() && r1->is_con() ) { // comparing constants?
    assert(r0->get_con() == r1->get_con(), "must be equal");
    return TypeInt::CC_EQ;      // Equal results.
  } 

  // Check for special case in Hashtable::get - the hash index is
  // mod'ed to the table size so the following range check is useless.
  // Check for: (X Mod Y) CmpU Y, where the mod result and Y both have
  // to be positive.
  if( r0->_lo >= 0 && r1->_lo >= 0 && 
      in(1)->Opcode() == Op_ModI &&
      in(1)->in(2) == in(2) )
    return TypeInt::CC_LT;

  if( r0->_hi == r1->_lo ) // Range is never high?
    return TypeInt::CC_LE;
  else if( r0->_lo == r1->_hi ) // Range is never low?
    return TypeInt::CC_GE;
  return TypeInt::CC;           // else use worst case results
}

// Simplify a CmpU (compare 2 integers) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpUNode::sub( const Type *t1, const Type *t2 ) const {
  assert(!t1->isa_ptr(), "obsolete usage of CmpU");

  // comparing two unsigned ints
  const TypeInt *r0 = t1->is_int();   // Handy access
  const TypeInt *r1 = t2->is_int();

  // Current installed version
  // Compare ranges for non-overlap
  juint lo0 = r0->_lo;
  juint hi0 = r0->_hi;
  juint lo1 = r1->_lo;
  juint hi1 = r1->_hi;

  // If either one has both negative and positive values,
  // it therefore contains both 0 and -1, and since [0..-1] is the
  // full unsigned range, the type must act as an unsigned bottom.
  bool bot0 = ((jint)(lo0 ^ hi0) < 0);
  bool bot1 = ((jint)(lo1 ^ hi1) < 0);

  if (bot0 || bot1) {
    // All unsigned values are LE -1 and GE 0.
    if (lo0 == 0 && hi0 == 0) {
      return TypeInt::CC_LE;            //   0 <= bot
    } else if (lo1 == 0 && hi1 == 0) {
      return TypeInt::CC_GE;            // bot >= 0
    }
  } else {
    // We can use ranges of the form [lo..hi] if signs are the same.
    assert(lo0 <= hi0 && lo1 <= hi1, "unsigned ranges are valid");
    // results are reversed, '-' > '+' for unsigned compare
    if (hi0 < lo1) {
      return TypeInt::CC_LT;            // smaller
    } else if (lo0 > hi1) {
      return TypeInt::CC_GT;            // greater
    } else if (hi0 == lo1 && lo0 == hi1) {
      return TypeInt::CC_EQ;            // Equal results
    } else if (lo0 >= hi1) {
      return TypeInt::CC_GE;
    } else if (hi0 <= lo1) {
      // Check for special case in Hashtable::get.  (See below.)
      if ((jint)lo0 >= 0 && (jint)lo1 >= 0 && 
          in(1)->Opcode() == Op_ModI &&
          in(1)->in(2) == in(2) )
        return TypeInt::CC_LT;
      return TypeInt::CC_LE;
    }
  }
  // Check for special case in Hashtable::get - the hash index is
  // mod'ed to the table size so the following range check is useless.
  // Check for: (X Mod Y) CmpU Y, where the mod result and Y both have
  // to be positive.
  // (This is a gross hack, since the sub method never
  // looks at the structure of the node in any other case.)
  if ((jint)lo0 >= 0 && (jint)lo1 >= 0 && 
      in(1)->Opcode() == Op_ModI &&
      in(1)->in(2)->uncast() == in(2)->uncast())
    return TypeInt::CC_LT;
  return TypeInt::CC;                   // else use worst case results
}

//------------------------------Idealize---------------------------------------
Node *CmpINode::Ideal( PhaseGVN *phase, bool can_reshape ) {
  if (phase->type(in(2))->higher_equal(TypeInt::ZERO)) {
    switch (in(1)->Opcode()) {
    case Op_CmpL3:              // Collapse a CmpL3/CmpI into a CmpL
      return new (phase->C, 3) CmpLNode(in(1)->in(1),in(1)->in(2));
    case Op_CmpF3:              // Collapse a CmpF3/CmpI into a CmpF
      return new (phase->C, 3) CmpFNode(in(1)->in(1),in(1)->in(2));
    case Op_CmpD3:              // Collapse a CmpD3/CmpI into a CmpD
      return new (phase->C, 3) CmpDNode(in(1)->in(1),in(1)->in(2));
    //case Op_SubI:
      // If (x - y) cannot overflow, then ((x - y) <?> 0)
      // can be turned into (x <?> y).
      // This is handled (with more general cases) by Ideal_sub_algebra.
    }
  }
  return NULL;                  // No change
}

//------------------------------Idealize---------------------------------------
Node*CmpUNode::Ideal(PhaseGVN*phase,bool can_reshape){
  // If both inputs are positive integers, then this is the same as a normal
  // signed compare.  Normal compares are also highly optimized and appear in
  // user code controlling loop trip counts.  This lets a user loop guard
  // cover a range-check sometimes.  Often, one of the inputs is a LoadRange
  // and is known positive, while the other input is a constant.
  if( phase->type(in(1))->higher_equal(TypeInt::POS) &&
      phase->type(in(2))->higher_equal(TypeInt::POS) )
    return new (phase->C, 3) CmpINode(in(1),in(2));
  return NULL;			// No change
}


//=============================================================================
// Simplify a CmpL (compare 2 longs ) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpLNode::sub( const Type *t1, const Type *t2 ) const {
  const TypeLong *r0 = t1->is_long(); // Handy access
  const TypeLong *r1 = t2->is_long();

  if( r0->_hi < r1->_lo )       // Range is always low?
    return TypeInt::CC_LT;
  else if( r0->_lo > r1->_hi )  // Range is always high?
    return TypeInt::CC_GT;

  else if( r0->is_con() && r1->is_con() ) { // comparing constants?
    assert(r0->get_con() == r1->get_con(), "must be equal");
    return TypeInt::CC_EQ;      // Equal results.
  } else if( r0->_hi == r1->_lo ) // Range is never high?
    return TypeInt::CC_LE;
  else if( r0->_lo == r1->_hi ) // Range is never low?
    return TypeInt::CC_GE;
  return TypeInt::CC;           // else use worst case results
}


//------------------------------Ideal------------------------------------------
// Check for comparing two ConvI2L's or long-constants that fit in int range.
Node*CmpLNode::Ideal(PhaseGVN*phase,bool can_reshape){
const Type*t1=phase->type(in(1));
  const Type *t2 = phase->type(in(2));
  if( Opcode() != Op_CmpL3 &&   // Normal CmpL?
      t1->meet(TypeLong::INT) == TypeLong::INT && // Both inputs in integer range?
      t2->meet(TypeLong::INT) == TypeLong::INT &&
      // Ignore if both inputs are constants 'cause the Value call will fold
      !(t1->singleton() && t2->singleton()) ) {
    Node *left = phase->transform(new (phase->C, 2) ConvL2INode(in(1)));
    Node *rigt = phase->transform(new (phase->C, 2) ConvL2INode(in(2)));
    return new (phase->C, 3) CmpINode(left,rigt);
  }
return CmpNode::Ideal(phase,can_reshape);
}

//=============================================================================
static const Type *compare_klass( const TypeOopPtr *p0, const TypeOopPtr *p1 ) {
  // See if neither subclasses the other, or if the class on top
  // is precise.  In either of these cases, the compare must fail.
  ciKlass* klass0 = p0->klass();
  ciKlass* klass1 = p1->klass();
  if( p0->isa_klassptr() ) {    // KID-ness agrees
    assert0( p0->is_klassptr()->_is_kid == p1->is_klassptr()->_is_kid );
  }
  if( p0 == p1 && p0->singleton() ) {              
    // Equal pointer constants (klasses, nulls, etc.)
    return TypeInt::CC_EQ;    
  } else if( klass0->equals(klass1)   || // if types are unequal but klasses are
             !klass0->is_java_klass() || // types not part of Java language?
             !klass1->is_java_klass()) { // types not part of Java language?
    // Do nothing; we know nothing for imprecise types
  } else if( klass0->is_subtype_of(klass1) ) {
    // If klass1's type is PRECISE, then we can fail.
    if( p1->klass_is_exact() ) return TypeInt::CC_GT;
  } else if( klass1->is_subtype_of(klass0) ) {
    // If klass0's type is PRECISE, then we can fail.
    if( p0->klass_is_exact() ) return TypeInt::CC_GT;
  } else {                    // Neither subtypes the other
    return TypeInt::CC_GT;    // ...so always fail
  }

  if( klass0->is_java_klass() && klass1->is_java_klass() ) {
    const Type *pj = p0->join(p1);
if(pj->empty())
return TypeInt::CC_GT;
  }

  return 0;                     // No definitive compare
}

//------------------------------sub--------------------------------------------
// Simplify an CmpP (compare 2 pointers) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpPNode::sub( const Type *t1, const Type *t2 ) const {
  const TypePtr *r0 = t1->is_ptr(); // Handy access
  const TypePtr *r1 = t2->is_ptr();
        
  // Undefined inputs makes for an undefined result
  if( TypePtr::above_centerline(r0->_ptr) ||
      TypePtr::above_centerline(r1->_ptr) ) 
    return Type::TOP;

  if (r0 == r1 && r0->singleton()) {
    // Equal pointer constants (klasses, nulls, etc.)
    return TypeInt::CC_EQ;
  }

  // See if it is 2 unrelated classes.
  const TypeOopPtr* p0 = r0->isa_oopptr();
  const TypeOopPtr* p1 = r1->isa_oopptr();
  if (p0 && p1) {
    const Type *result = compare_klass(p0,p1);
    if( result ) return result;
  }

  // Unknown inputs makes an unknown result
  if( r0->singleton() ) {
    intptr_t bits0 = r0->get_con();
    if( r1->singleton() ) 
      return bits0 == r1->get_con() ? TypeInt::CC_EQ : TypeInt::CC_GT;
    return ( r1->_ptr == TypePtr::NotNull && bits0==0 ) ? TypeInt::CC_GT : TypeInt::CC;
  } else if( r1->singleton() ) {
    intptr_t bits1 = r1->get_con();
    return ( r0->_ptr == TypePtr::NotNull && bits1==0 ) ? TypeInt::CC_GT : TypeInt::CC;
  } else 
    return TypeInt::CC;
}

//------------------------------Ideal------------------------------------------
// Check for the case of comparing an unknown klass loaded from the primary
// super-type array vs a known klass with no subtypes.  This amounts to
// checking to see an unknown klass subtypes a known klass with no subtypes;
// this only happens on an exact match.  We can shorten this test by 1 load.
Node *CmpPNode::Ideal( PhaseGVN *phase, bool can_reshape ) {
  // Ignore if NULL or TOP appears.
const Type*tt1=phase->type(in(1));
const Type*tt2=phase->type(in(2));
  if( tt1 == TypePtr::NULL_PTR || tt1 == Type::TOP ) return NULL;
  if( tt2 == TypePtr::NULL_PTR || tt2 == Type::TOP ) return NULL;
  if( in(1) == in(2) ) return NULL; // Will optimize via SubNode::Value
  if( tt1->singleton() && tt2->singleton() ) return NULL; // Value call again...

  // See if we are comparing 2 klass-typed things, when we could compare 2
  // kid-typed things.
  if( tt1->isa_klassptr() && !tt1->is_klassptr()->_is_kid ) {
    assert0( !tt2->is_klassptr()->_is_kid ); // both are klasses; neither is a kid-type
    if( (in(1)->Opcode() == Op_KID2Klass || tt1->singleton()) &&
        (in(2)->Opcode() == Op_KID2Klass || tt2->singleton()) ) {
      Node *n1 = in(1)->Opcode() == Op_KID2Klass ? in(1)->in(1) : phase->makecon(tt1->is_klassptr()->cast_to_kid(true));
      Node *n2 = in(2)->Opcode() == Op_KID2Klass ? in(2)->in(1) : phase->makecon(tt2->is_klassptr()->cast_to_kid(true));
      return new (phase->C,3) CmpPNode(n1,n2);
    }
  }

  // Constant pointer on right?
  const TypeKlassPtr* t2 = phase->type(in(2))->isa_klassptr();
  if (t2 == NULL || !t2->klass_is_exact())
    return NULL;
  // Get the constant klass we are comparing to.
  ciKlass* superklass = t2->klass();

  // Now check for LoadKlass on left.
  Node* ldk1 = in(1);
  // Azul does not use a klass word.  Instead we use a GetKID node when
  // fetching a Klass ID from a plain object.
  if (ldk1->Opcode() == Op_GetKID) {
    // We are inspecting an object's concrete class.
    // Short-circuit the check if the query is abstract.
    if (superklass->is_interface() ||
        superklass->is_abstract()) {
      // Make it come out always false:
      this->set_req(2, phase->makecon(TypePtr::NULL_PTR));
      return this;
    }
  }

  if (ldk1->Opcode() != Op_LoadKlass)
    return NULL;
  // Take apart the address of the LoadKlass:
  Node* adr1 = ldk1->in(MemNode::Address);
  intptr_t con2 = 0;
  Node* ldk2 = AddPNode::Ideal_base_and_offset(adr1, phase, con2);
  if (ldk2 == NULL)
    return NULL;

  // Check for a LoadKlass from primary supertype array.
  // Any nested loadklass from loadklass+con must be from the p.s. array.
  if (ldk2->Opcode() != Op_LoadKlass)
    return NULL;

  // Verify that we understand the situation
  if (con2 != (intptr_t) superklass->super_check_offset())
    return NULL;                // Might be element-klass loading from array klass

  // Verify that we understand the situation: since the superklass is a
  // constant, its super_check_offset is a compile-time constant and we are
  // loading a Klass from inside the sub-Klass at this offset.
  if (con2 != (intptr_t) superklass->super_check_offset())
    return NULL;                // Might be element-klass loading from array klass

  // Verify we are looking into the P.S.array or the 2nd ary cache
  // (for very deep hierarchies) and not some weirdo class-mirror thingy.
int pso=sizeof(oopDesc)+Klass::primary_supers_kids_offset_in_bytes();
  assert0( (con2 >= pso &&
            con2 <  pso + (int)HeapWordSize*Klass::primary_super_limit()) ||
           con2 == (int)sizeof(oopDesc)+Klass::secondary_super_kid_cache_offset_in_bytes() );

  // Object arrays must have their base element have no subtypes
  while (superklass->is_obj_array_klass()) {
    ciType* elem = superklass->as_obj_array_klass()->element_type();
    superklass = elem->as_klass();
  }
  // If 'superklass' has no subklasses and is not an interface, then we are
  // assured that the only input which will pass the type check is
  // 'superklass' itself.  Note that it's no good to have only a single
  // concrete implementor of an abstract superklass; we can get here with
  // "isAssignableFrom" with any 2 Classes in the entire VM.  i.e., just
  // because all instances of Foo's must be Bar's does not mean all subtype
  // tests involve only Bars; we can ask if some unknown Class X
  // isAssignableFrom a Foo and X might be either Class-Foo or Class-Bar.
  if (superklass->is_instance_klass()) {
    ciInstanceKlass* ik = superklass->as_instance_klass();
    if (ik->has_subklass() || ik->is_interface())  return NULL;
    // Superklass has no subklasses, but might have 0 or 1 implementors
    // depending on if it's abstract or not.  Record a dependency on
    // however many implementors it has such that adding a new one
    // will force a deopt.
    int nof = ik->nof_implementors(&phase->C->_masm);
    assert0( nof < 2 );
  }
  
  // Bypass the dependent load, and compare directly.  This will often trigger
  // the CmpP_of_Klass-into-CmpI_of_KID optimization.
  this->set_req(1,ldk2);
  this->set_req(2,phase->makecon(t2->cast_to_kid(false)));

  return this;
}

//=============================================================================
//------------------------------Value------------------------------------------
// Simplify an CmpF (compare 2 floats ) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpFNode::Value( PhaseTransform *phase ) const {
  const Node* in1 = in(1);
  const Node* in2 = in(2);
  // Either input is TOP ==> the result is TOP
  const Type* t1 = (in1 == this) ? Type::TOP : phase->type(in1);
  if( t1 == Type::TOP ) return Type::TOP;
  const Type* t2 = (in2 == this) ? Type::TOP : phase->type(in2);
  if( t2 == Type::TOP ) return Type::TOP;

  // Not constants?  Don't know squat - even if they are the same 
  // value!  If they are NaN's they compare to LT instead of EQ.
  const TypeF *tf1 = t1->isa_float_constant();
  const TypeF *tf2 = t2->isa_float_constant();
  if( !tf1 || !tf2 ) return TypeInt::CC;

  // This implements the Java bytecode fcmpl, so unordered returns -1.
  if( tf1->is_nan() || tf2->is_nan() )
    return TypeInt::CC_LT;

  if( tf1->_f < tf2->_f ) return TypeInt::CC_LT;
  if( tf1->_f > tf2->_f ) return TypeInt::CC_GT;
  assert( tf1->_f == tf2->_f, "do not understand FP behavior" );
  return TypeInt::CC_EQ;
}


//=============================================================================
//------------------------------Value------------------------------------------
// Simplify an CmpD (compare 2 doubles ) node, based on local information.
// If both inputs are constants, compare them.  
const Type *CmpDNode::Value( PhaseTransform *phase ) const {
  const Node* in1 = in(1);
  const Node* in2 = in(2);
  // Either input is TOP ==> the result is TOP
  const Type* t1 = (in1 == this) ? Type::TOP : phase->type(in1);
  if( t1 == Type::TOP ) return Type::TOP;
  const Type* t2 = (in2 == this) ? Type::TOP : phase->type(in2);
  if( t2 == Type::TOP ) return Type::TOP;

  // Not constants?  Don't know squat - even if they are the same 
  // value!  If they are NaN's they compare to LT instead of EQ.
  const TypeD *td1 = t1->isa_double_constant();
  const TypeD *td2 = t2->isa_double_constant();
  if( !td1 || !td2 ) return TypeInt::CC;

  // This implements the Java bytecode dcmpl, so unordered returns -1.
  if( td1->is_nan() || td2->is_nan() )
    return TypeInt::CC_LT;

  if( td1->_d < td2->_d ) return TypeInt::CC_LT;
  if( td1->_d > td2->_d ) return TypeInt::CC_GT;
  assert( td1->_d == td2->_d, "do not understand FP behavior" );
  return TypeInt::CC_EQ;
}

//------------------------------Ideal------------------------------------------
Node *CmpDNode::Ideal(PhaseGVN *phase, bool can_reshape){
  // Check if we can change this to a CmpF and remove a ConvD2F operation.
  // Change  (CMPD (F2D (float)) (ConD value))
  // To      (CMPF      (float)  (ConF value))
  // Valid when 'value' does not lose precision as a float.
  // Benefits: eliminates conversion, does not require 24-bit mode

  // NaNs prevent commuting operands.  This transform works regardless of the 
  // order of ConD and ConvF2D inputs by preserving the original order.
  int idx_f2d = 1;              // ConvF2D on left side?
  if( in(idx_f2d)->Opcode() != Op_ConvF2D ) 
    idx_f2d = 2;                // No, swap to check for reversed args
  int idx_con = 3-idx_f2d;      // Check for the constant on other input

  if( ConvertCmpD2CmpF &&
      in(idx_f2d)->Opcode() == Op_ConvF2D &&
      in(idx_con)->Opcode() == Op_ConD ) {
    const TypeD *t2 = in(idx_con)->bottom_type()->is_double_constant();
    double t2_value_as_double = t2->_d;
    float  t2_value_as_float  = (float)t2_value_as_double;
    if( t2_value_as_double == (double)t2_value_as_float ) {
      // Test value can be represented as a float
      // Eliminate the conversion to double and create new comparison
      Node *new_in1 = in(idx_f2d)->in(1);
      Node *new_in2 = phase->makecon( TypeF::make(t2_value_as_float) );
      if( idx_f2d != 1 ) {      // Must flip args to match original order
        Node *tmp = new_in1;
        new_in1 = new_in2;
        new_in2 = tmp;
      }
      CmpFNode *new_cmp = (Opcode() == Op_CmpD3) 
        ? new (phase->C, 3) CmpF3Node( new_in1, new_in2 ) 
        : new (phase->C, 3) CmpFNode ( new_in1, new_in2 ) ;
      return new_cmp;           // Changed to CmpFNode
    }
    // Testing value required the precision of a double
  }
  return NULL;                  // No change
}


//=============================================================================
//------------------------------cc2logical-------------------------------------
// Convert a condition code type to a logical type
const Type *BoolTest::cc2logical( const Type *CC ) const {
  if( CC == Type::TOP ) return Type::TOP;
  if( CC->base() != Type::Int ) return TypeInt::BOOL; // Bottom or worse
  const TypeInt *ti = CC->is_int();
  if( ti->is_con() ) {          // Only 1 kind of condition codes set?
    // Match low order 2 bits
    int tmp = ((ti->get_con()&3) == (_test&3)) ? 1 : 0; 
    if( _test & 4 ) tmp = 1-tmp;     // Optionally complement result
    return TypeInt::make(tmp);       // Boolean result
  }
 
  if( CC == TypeInt::CC_GE ) {
    if( _test == ge ) return TypeInt::ONE;
    if( _test == lt ) return TypeInt::ZERO;
  }
  if( CC == TypeInt::CC_LE ) {
    if( _test == le ) return TypeInt::ONE;
    if( _test == gt ) return TypeInt::ZERO;
  }

  return TypeInt::BOOL;
}

//------------------------------dump_spec-------------------------------------
// Print special per-node info
void BoolTest::dump_on(outputStream *st) const {
  const char *msg[] = {"eq","gt","??","lt","ne","le","??","ge"};
  st->print(msg[_test]);
}

//=============================================================================
uint BoolNode::hash() const { return (Node::hash() << 3)|(_test._test+1); } 
uint BoolNode::size_of() const { return sizeof(BoolNode); }

//------------------------------operator==-------------------------------------
uint BoolNode::cmp( const Node &n ) const {
  const BoolNode *b = (const BoolNode *)&n; // Cast up
  return (_test._test == b->_test._test);
}

//------------------------------clone_cmp--------------------------------------
// Clone a compare/bool tree
static Node *clone_cmp( Node *cmp, Node *cmp1, Node *cmp2, PhaseGVN *gvn, BoolTest::mask test ) {
  Node *ncmp = cmp->clone();
  ncmp->set_req(1,cmp1);
  ncmp->set_req(2,cmp2);
  ncmp = gvn->transform( ncmp );
  return new (gvn->C, 2) BoolNode( ncmp, test );
}

//-------------------------------make_predicate--------------------------------
Node* BoolNode::make_predicate(Node* test_value, PhaseGVN* phase) {
  if (test_value->is_Con())   return test_value;
  if (test_value->is_Bool())  return test_value;
  Compile* C = phase->C;
  if (test_value->is_CMove() &&
      test_value->in(CMoveNode::Condition)->is_Bool()) {
    BoolNode*   bol   = test_value->in(CMoveNode::Condition)->as_Bool();
    const Type* ftype = phase->type(test_value->in(CMoveNode::IfFalse));
    const Type* ttype = phase->type(test_value->in(CMoveNode::IfTrue));
    if (ftype == TypeInt::ZERO && !TypeInt::ZERO->higher_equal(ttype)) {
      return bol;
    } else if (ttype == TypeInt::ZERO && !TypeInt::ZERO->higher_equal(ftype)) {
      return phase->transform( bol->negate(phase) );
    }
    // Else fall through.  The CMove gets in the way of the test.
    // It should be the case that make_predicate(bol->as_int_value()) == bol.
  }
  Node* cmp = new (C, 3) CmpINode(test_value, phase->intcon(0));
  cmp = phase->transform(cmp);
  Node* bol = new (C, 2) BoolNode(cmp, BoolTest::ne);
  return phase->transform(bol);
}

//--------------------------------as_int_value---------------------------------
Node* BoolNode::as_int_value(PhaseGVN* phase) {
  // Inverse to make_predicate.  The CMove probably boils down to a Conv2B.
  Node* cmov = CMoveNode::make(phase->C, NULL, this,
                               phase->intcon(0), phase->intcon(1),
                               TypeInt::BOOL);
  return phase->transform(cmov);
}

//----------------------------------negate-------------------------------------
BoolNode* BoolNode::negate(PhaseGVN* phase) {
  Compile* C = phase->C;
  return new (C, 2) BoolNode(in(1), _test.negate());
}


//------------------------------Ideal------------------------------------------
Node *BoolNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  // Change "bool tst (cmp con x)" into "bool ~tst (cmp x con)".
  // This moves the constant to the right.  Helps value-numbering.
  Node *cmp = in(1);
  if( !cmp->is_Sub() ) return NULL;
  int cop = cmp->Opcode();
assert0(cop!=Op_FastLock&&cop!=Op_FastUnlock);
  Node *cmp1 = cmp->in(1);
  Node *cmp2 = cmp->in(2);
  if( !cmp1 ) return NULL;

  // Do not muck with counted loop ends, as they expect to understand
  // the sign (for count-down vs count-up loops).
  for( DUIterator_Fast imax, i = this->fast_outs(imax); i < imax; i++ ) {
    if( this->fast_out(i)->Opcode() == Op_CountedLoopEnd )
      return NULL;
  }

  // Constant on left?
  Node *con = cmp1;
  uint op2 = cmp2->Opcode();
  // Move constants to the right of compare's to canonicalize.
  // Do not muck with Opaque1 nodes, as this indicates a loop
  // guard that cannot change shape.
  if( con->is_Con() && !cmp2->is_Con() && op2 != Op_Opaque1 &&
      // Because of NaN's, CmpD and CmpF are not commutative
      cop != Op_CmpD && cop != Op_CmpF &&
      // Protect against swapping inputs to a compare when it is used by a
      // counted loop exit, which requires maintaining the loop-limit as in(2)
      !is_counted_loop_exit_test() ) {
    // Ok, commute the constant to the right of the cmp node.
    // Clone the Node, getting a new Node of the same class
    cmp = cmp->clone();
    // Swap inputs to the clone
    cmp->swap_edges(1, 2);
    cmp = phase->transform( cmp );
    return new (phase->C, 2) BoolNode( cmp, _test.commute() );
  }

  // Change "bool le (cmp pos 0)" into "bool eq (cmp pos 0)"
  const TypeInt *cmp2_type = phase->type(cmp2)->isa_int();
  if (cmp2_type == NULL)  return NULL;
  if( _test._test == BoolTest::le &&
      cmp2_type == TypeInt::ZERO &&
      phase->type(cmp1)->higher_equal(TypeInt::make(0,max_jint,Type::WidenMax)) )
    return new (phase->C, 2) BoolNode(cmp, BoolTest::eq);

  bool is_eqne = (_test._test == BoolTest::eq || _test._test == BoolTest::ne);
  bool is_cmpI_vs_0 = (cmp2_type == TypeInt ::ZERO);
  bool is_cmpL_vs_0 = (phase->type(cmp2) == TypeLong::ZERO);

  // Change "bool eq/ne (cmp (xor X 1) 0)" into "bool ne/eq (cmp X 0)".
  // The XOR-1 is an idiom used to flip the sense of a bool.  We flip the
  // test instead.
  int cmp1_op = cmp1->Opcode();
  Node *j_xor = cmp1;
  if( is_cmpI_vs_0 && is_eqne &&
      cmp1_op == Op_XorI &&
      j_xor->in(1) != j_xor &&          // An xor of itself is dead
phase->type(j_xor->in(2))==TypeInt::ONE){
    Node *ncmp = phase->transform(new (phase->C, 3) CmpINode(j_xor->in(1),cmp2));
    return new (phase->C, 2) BoolNode( ncmp, _test.negate() );
  }
  
  // Change "bool eq/ne (cmp (Conv2B X) 0)" into "bool eq/ne (cmp X 0)".
  // This is a standard idiom for branching on a boolean value.
  Node *c2b = cmp1;
  if( is_cmpI_vs_0 && is_eqne &&
      cmp1_op == Op_Conv2B ) {
    Node *ncmp = phase->transform(phase->type(c2b->in(1))->isa_int()
       ? (Node*)new (phase->C, 3) CmpINode(c2b->in(1),cmp2)
       : (Node*)new (phase->C, 3) CmpPNode(c2b->in(1),phase->makecon(TypePtr::NULL_PTR))
    );
    return new (phase->C, 2) BoolNode( ncmp, _test._test );
  }

  const int limit = 1<<7;     // X86 limit
  // Change "bool eq/ne (cmp (AndX           Y         big_bitmask        ) 0)" into 
  //        "bool eq/ne (cmp (AndX (URShiftX Y shift) (big_bitmask>>shift)) 0)"
  // On most machines, making a large integer constant is more expensive than
  // a shift.  If we are making a large bitmask and then testing for equal/
  // not-equal zero, we do not care where the bits land.  Instead of making a
  // large mask with high-order bits, we try to shift the tested value right
  // then mask with just low order bits.
  jint intcon;
  if( is_cmpI_vs_0 && is_eqne &&
cmp1_op==Op_AndI&&
cmp1->in(2)->is_Con()&&
      ( intcon = cmp1->in(2)->get_int() ) ) {
    juint lobit = intcon & ~(intcon-1);
    int shift = 0;
    while( (1LL<<shift) != lobit ) shift++;
    jint newint = intcon>>shift;
    if( (intcon >= limit || intcon < -limit) &&
        (newint < limit && newint >= -limit) ) {
      Node *urshifti = phase->transform( new (phase->C, 3) URShiftINode( cmp1->in(1), phase->intcon(shift) ) );
Node*nand=phase->transform(new(phase->C,3)AndINode(urshifti,phase->intcon(newint)));
Node*ncmp=phase->transform(new(phase->C,3)CmpINode(nand,cmp2));
set_req(1,ncmp);
      return this;
    }
  }
  // Same thing in a LONG variant
  if( is_cmpL_vs_0 && is_eqne &&
cmp1_op==Op_AndL&&
      phase->type(cmp1->in(2))->singleton() ) {
    jlong longcon = cmp1->in(2)->get_long();
    jlong lobit = longcon & ~(longcon-1);
    int shift = 0;
    while( (1LL<<shift) != lobit ) shift++;
    jlong newlong = longcon>>shift;
    if( (longcon >= limit || longcon < -limit) &&
        (newlong < limit && newlong >= -limit) ) {
      Node *urshifti = phase->transform( new (phase->C, 3) URShiftLNode( cmp1->in(1), phase->intcon(shift) ) );
Node*nand=phase->transform(new(phase->C,3)AndLNode(urshifti,phase->longcon(newlong)));
Node*ncmp=phase->transform(new(phase->C,3)CmpLNode(nand,cmp2));
set_req(1,ncmp);
      return this;
    }
  }

  // Comparing a SubI against a zero is equal to comparing the SubI
  // arguments directly.  This only works for eq and ne comparisons
  // due to possible integer overflow.
  if( is_cmpI_vs_0 && is_eqne &&
      cmp1_op == Op_SubI ) {
    Node *ncmp = phase->transform( new (phase->C, 3) CmpINode(cmp1->in(1),cmp1->in(2)));
    return new (phase->C, 2) BoolNode( ncmp, _test._test );
  }

  // Change (-A vs 0) into (A vs 0) by commuting the test.  Disallow in the
  // most general case because negating 0x80000000 does nothing.  Needed for
  // the CmpF3/SubI/CmpI idiom.
  if( is_cmpI_vs_0 && 
      cmp1->Opcode() == Op_SubI &&
      phase->type( cmp1->in(1) ) == TypeInt::ZERO &&
      phase->type( cmp1->in(2) )->higher_equal(TypeInt::SYMINT) ) {
    Node *ncmp = phase->transform( new (phase->C, 3) CmpINode(cmp1->in(2),cmp2));
    return new (phase->C, 2) BoolNode( ncmp, _test.commute() );
  }

  //  The transformation below is not valid for either signed or unsigned 
  //  comparisons due to wraparound concerns at MAX_VALUE and MIN_VALUE.  
  //  This transformation can be resurrected when we are able to  
  //  make inferences about the range of values being subtracted from
  //  (or added to) relative to the wraparound point.
  //
  //    // Remove +/-1's if possible.  
  //    // "X <= Y-1" becomes "X <  Y"
  //    // "X+1 <= Y" becomes "X <  Y"
  //    // "X <  Y+1" becomes "X <= Y"
  //    // "X-1 <  Y" becomes "X <= Y"
  //    // Do not this to compares off of the counted-loop-end.  These guys are
  //    // checking the trip counter and they want to use the post-incremented 
  //    // counter.  If they use the PRE-incremented counter, then the counter has
  //    // to be incremented in a private block on a loop backedge.
  //    if( du && du->cnt(this) && du->out(this)[0]->Opcode() == Op_CountedLoopEnd )
  //      return NULL;
  //  #ifndef PRODUCT
  //    // Do not do this in a wash GVN pass during verification.
  //    // Gets triggered by too many simple optimizations to be bothered with
  //    // re-trying it again and again.
  //    if( !phase->allow_progress() ) return NULL;
  //  #endif
  //    // Not valid for unsigned compare because of corner cases in involving zero.
  //    // For example, replacing "X-1 <u Y" with "X <=u Y" fails to throw an
  //    // exception in case X is 0 (because 0-1 turns into 4billion unsigned but
  //    // "0 <=u Y" is always true).
  //    if( cmp->Opcode() == Op_CmpU ) return NULL;
  //    int cmp2_op = cmp2->Opcode();
  //    if( _test._test == BoolTest::le ) {
  //      if( cmp1_op == Op_AddI &&
  //          phase->type( cmp1->in(2) ) == TypeInt::ONE ) 
  //        return clone_cmp( cmp, cmp1->in(1), cmp2, phase, BoolTest::lt );
  //      else if( cmp2_op == Op_AddI &&
  //         phase->type( cmp2->in(2) ) == TypeInt::MINUS_1 )
  //        return clone_cmp( cmp, cmp1, cmp2->in(1), phase, BoolTest::lt );
  //    } else if( _test._test == BoolTest::lt ) {
  //      if( cmp1_op == Op_AddI &&
  //          phase->type( cmp1->in(2) ) == TypeInt::MINUS_1 )
  //        return clone_cmp( cmp, cmp1->in(1), cmp2, phase, BoolTest::le );
  //      else if( cmp2_op == Op_AddI &&
  //         phase->type( cmp2->in(2) ) == TypeInt::ONE )
  //        return clone_cmp( cmp, cmp1, cmp2->in(1), phase, BoolTest::le );
  //    }
    
  return NULL;
}

//------------------------------Value------------------------------------------
// Simplify a Bool (convert condition codes to boolean (1 or 0)) node,
// based on local information.   If the input is constant, do it.
const Type *BoolNode::Value( PhaseTransform *phase ) const {
  return _test.cc2logical( phase->type( in(1) ) );
}

//------------------------------dump_spec--------------------------------------
// Dump special per-node info
void BoolNode::dump_spec(outputStream *st) const {
  st->print("[");
  _test.dump_on(st);
  st->print("]");
}

//------------------------------is_counted_loop_exit_test--------------------------------------
// Returns true if node is used by a counted loop node.
bool BoolNode::is_counted_loop_exit_test() {
  for( DUIterator_Fast imax, i = fast_outs(imax); i < imax; i++ ) {
    Node* use = fast_out(i);
    if (use->is_CountedLoopEnd()) {
      return true;
    }
  }
  return false;
}

//=============================================================================
//------------------------------NegNode----------------------------------------
Node *NegFNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  if( in(1)->Opcode() == Op_SubF )
    return new (phase->C, 3) SubFNode( in(1)->in(2), in(1)->in(1) );
  return NULL;
}

Node *NegDNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  if( in(1)->Opcode() == Op_SubD )
    return new (phase->C, 3) SubDNode( in(1)->in(2), in(1)->in(1) );
  return NULL;
}


//=============================================================================
//------------------------------Value------------------------------------------
// Compute sqrt
const Type *SqrtDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( sqrt( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute sqrt
const Type*SqrtFNode::Value(PhaseTransform*phase)const{
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
if(t1->base()!=Type::FloatCon)return Type::FLOAT;
  float f = t1->getf();
  if( f < 0.0 ) return Type::FLOAT;
  return TypeF::make( sqrtf( f ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute cos
const Type *CosDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dcos( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute sin
const Type *SinDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dsin( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute tan
const Type *TanDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dtan( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute log
const Type *LogDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dlog( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute log10
const Type *Log10DNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dlog10( d ) );
}

//=============================================================================
//------------------------------Value------------------------------------------
// Compute exp
const Type *ExpDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d = t1->getd();
  if( d < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dexp( d ) );
}


//=============================================================================
//------------------------------Value------------------------------------------
// Compute pow
const Type *PowDNode::Value( PhaseTransform *phase ) const {
  const Type *t1 = phase->type( in(1) );
  if( t1 == Type::TOP ) return Type::TOP;
  if( t1->base() != Type::DoubleCon ) return Type::DOUBLE;
  const Type *t2 = phase->type( in(2) );
  if( t2 == Type::TOP ) return Type::TOP;
  if( t2->base() != Type::DoubleCon ) return Type::DOUBLE;
  double d1 = t1->getd();
  double d2 = t2->getd();
  if( d1 < 0.0 ) return Type::DOUBLE;
  if( d2 < 0.0 ) return Type::DOUBLE;
  return TypeD::make( SharedRuntime::dpow( d1, d2 ) );
}

