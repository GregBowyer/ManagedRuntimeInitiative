/*
 * Copyright 2000-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef REGALLOC_HPP
#define REGALLOC_HPP


#include "matcher.hpp"
#include "node.hpp"
#include "phase.hpp"
#include "regmask.hpp"
#include "vectset.hpp"
class Node;
class Matcher;
class PhaseCFG;

#define  MAX_REG_ALLOCATORS   10

//------------------------------PhaseRegAlloc------------------------------------
// Abstract register allocator
class PhaseRegAlloc : public Phase {
  static void (*_alloc_statistics[MAX_REG_ALLOCATORS])();
  static int _num_allocators;

protected:
  OptoReg::Name *_node_regs;
  uint         _node_regs_max_index;
  VectorSet    _node_oops;         // Mapping from node indices to oopiness

  void alloc_node_regs(int size);  // allocate _node_regs table with at least "size" elements

  PhaseRegAlloc( uint unique, PhaseCFG &cfg, Matcher &matcher,
                 void (*pr_stats)());
public:
  PhaseCFG &_cfg;               // Control flow graph
  uint _framesize;              // Size of frame in stack-slots. not counting preserve area
  OptoReg::Name _max_reg;       // Past largest register seen
  Matcher &_matcher;            // Convert Ideal to MachNodes
  uint node_regs_max_index() const { return _node_regs_max_index; }

  // Get the register associated with the Node
OptoReg::Name get_reg(const Node*n)const{
    debug_only( if( n->_idx >= _node_regs_max_index ) n->dump(); );
    assert( n->_idx < _node_regs_max_index, "Exceeded _node_regs array");
return _node_regs[n->_idx];
  }

  // Do all the real work of allocate
  virtual void Register_Allocate() = 0;


  // notify the register allocator that "node" is a new reference
  // to the value produced by "old_node"
  virtual void add_reference( const Node *node, const Node *old_node) = 0;


  // Set the register associated with a new Node
  void set_bad( uint idx ) {
    assert( idx < _node_regs_max_index, "Exceeded _node_regs array");
_node_regs[idx]=OptoReg::Bad;
  }
void set(uint idx,OptoReg::Name reg){
    assert( idx < _node_regs_max_index, "Exceeded _node_regs array");
_node_regs[idx]=reg;
  }
  // Set and query if a node produces an oop
  void set_oop( const Node *n, bool );
  bool is_oop( const Node *n ) const;

  // Convert a register number to a stack offset
  int reg2offset          ( OptoReg::Name reg ) const;
  int reg2offset_unchecked( OptoReg::Name reg ) const;

  // Convert a stack offset to a register number
  OptoReg::Name offset2reg( int stk_offset ) const;

  // Get the register encoding associated with the Node
Register get_encode(const Node*n)const{
    assert( n->_idx < _node_regs_max_index, "Exceeded _node_regs array");
OptoReg::Name first=_node_regs[n->_idx];
    assert(OptoReg::is_reg(first), "out of range");
    return Matcher::_regEncode[first];
  }  

  // Platform dependent hook for actions before allocation
  void  pd_preallocate_hook();
  // Platform dependent hook for actions after  allocation
  void  pd_postallocate_hook();

#ifdef ASSERT
  // Platform dependent hook for verification after allocation.  Will
  // only get called when compiling with asserts.
  void  pd_postallocate_verify_hook();
#endif

#ifndef PRODUCT
  static int _total_framesize;
  static int _max_framesize;

  virtual void dump_frame() const = 0;
  virtual char *dump_register( const Node *n, char *buf  ) const = 0;
  char *print_reg( OptoReg::Name reg, char *buf ) const;
  static void print_statistics();
#endif
};
#endif // REGALLOC_HPP
