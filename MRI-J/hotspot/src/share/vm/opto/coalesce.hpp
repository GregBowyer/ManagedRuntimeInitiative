/*
 * Copyright 1997-2003 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef COALESCE_HPP
#define COALESCE_HPP

#include "chaitin.hpp"
class LoopTree;
class LRG;
class LRG_List;
class Matcher;
class PhaseIFG;
class PhaseCFG;

//------------------------------PhaseCoalesce----------------------------------
class PhaseCoalesce : public Phase {
protected:
  PhaseChaitin &_phc;

public:
  // Coalesce copies
  PhaseCoalesce( PhaseChaitin &chaitin ) : Phase(Coalesce), _phc(chaitin) { }

  virtual void verify() = 0;

  // Coalesce copies
  void coalesce_driver( );

  // Coalesce copies in this block
  virtual void coalesce( Block *b ) = 0;

  // Attempt to coalesce live ranges defined by these 2
  void combine_these_two( Node *n1, Node *n2 );

  LRG &lrgs( uint lidx ) { return _phc.lrgs(lidx); }
#ifndef PRODUCT
  // Dump internally name
  void dump( Node *n ) const;
  // Dump whole shebang
  void dump() const;
#endif
};

//------------------------------PhaseAggressiveCoalesce------------------------
// Aggressively, pessimistic coalesce copies.  Aggressive means ignore graph
// colorability; perhaps coalescing to the point of forcing a spill.
// Pessimistic means we cannot coalesce if 2 live ranges interfere.  This
// implies we do not hit a fixed point right away.
class PhaseAggressiveCoalesce : public PhaseCoalesce {
  uint _unique;
public:
  // Coalesce copies
  PhaseAggressiveCoalesce( PhaseChaitin &chaitin ) : PhaseCoalesce(chaitin) {}

  virtual void verify() { };

  // Aggressively coalesce copies in this block
  virtual void coalesce( Block *b );

  // Where I fail to coalesce, manifest virtual copies as the Real Thing
  void insert_copies( Matcher &matcher );

  // Copy insertion needs some smarts in case live ranges overlap
  void insert_copy_with_overlap( Block *b, Node *copy, uint dst_name, uint src_name );
};


//------------------------------PhaseConservativeCoalesce----------------------
// Conservatively, pessimistic coalesce copies.  Conservative means do not
// coalesce if the resultant live range will be uncolorable.  Pessimistic
// means we cannot coalesce if 2 live ranges interfere.  This implies we do
// not hit a fixed point right away.
class PhaseConservativeCoalesce : public PhaseCoalesce {
  IndexSet _ulr;               // Union live range interferences
public:
  // Coalesce copies
  PhaseConservativeCoalesce( PhaseChaitin &chaitin );

  virtual void verify();

  // Conservatively coalesce copies in this block
  virtual void coalesce( Block *b );

  // Coalesce this chain of copies away
  bool copy_copy( Node *dst_copy, Node *src_copy, Block *b, uint bindex );

  void union_helper( Node *lr1_node, Node *lr2_node, uint lr1, uint lr2, Node *src_def, Node *dst_copy, Node *src_copy, Block *b, uint bindex );

  uint compute_separating_interferences(Node *dst_copy, Node *src_copy, Block *b, uint bindex, RegMask &rm, uint rm_size, uint reg_degree, uint lr1, uint lr2);

  void update_ifg(uint lr1, uint lr2, IndexSet *n_lr1, IndexSet *n_lr2);
};

#endif // COALESCE_HPP