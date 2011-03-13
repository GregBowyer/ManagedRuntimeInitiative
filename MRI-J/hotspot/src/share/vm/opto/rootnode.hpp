/*
 * Copyright 1997-2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef ROOTNODE_HPP
#define ROOTNODE_HPP


#include "loopnode.hpp"

//------------------------------RootNode---------------------------------------
// The one-and-only before-all-else and after-all-else RootNode.  The RootNode
// represents what happens if the user runs the whole program repeatedly.  The
// RootNode produces the initial values of I/O and memory for the program or
// procedure start.
class RootNode : public LoopNode {
public:
  RootNode( ) : LoopNode(0,0) {
    init_class_id(Class_Root);
    del_req(2);
    del_req(1);
  }
  virtual int   Opcode() const;
  virtual const Node *is_block_proj() const { return this; }
  virtual const Type *bottom_type() const { return Type::BOTTOM; }
  virtual Node *Identity( PhaseTransform *phase ) { return this; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const { return Type::BOTTOM; }
};

//------------------------------HaltNode---------------------------------------
// Throw an exception & die
class HaltNode : public Node {
public:
const char*_msg;
HaltNode(Node*ctrl,Node*frameptr,const char*msg);
  virtual int Opcode() const;
  virtual bool  pinned() const { return true; };
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual const Type *bottom_type() const;
  virtual bool  is_CFG() const { return true; }
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual const Node *is_block_proj() const { return this; }
  virtual const RegMask &out_RegMask() const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const { return 0; }
  virtual void  dump_spec(outputStream *st) const;
};

#endif // ROOTNODE_HPP
