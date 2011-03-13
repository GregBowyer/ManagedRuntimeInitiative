/*
 * Copyright 2005-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef MACRO_HPP
#define MACRO_HPP

#include "phase.hpp"
class AllocateArrayNode;
class AllocateNode;
class CallNode;
class LockNode;
class Node;
class PhaseIterGVN;
class ProjNode;
class TypeFunc;
class UnlockNode;

class PhaseMacroExpand : public Phase {
private:
  PhaseIterGVN &_igvn;

Node*transform_later(Node*n);

  void expand_allocate(AllocateNode *alloc);
  void expand_lock_node(LockNode *lock);
  void expand_unlock_node(UnlockNode *unlock);
  void expand_safepoint_node(SafePointNode *safe);
  Node* opt_iff(Node* region, Node* iff);
public:
  PhaseMacroExpand(PhaseIterGVN &igvn) : Phase(Macro_Expand), _igvn(igvn) {}
  bool expand_macro_nodes();
};

//------------------------------FastAllocNode---------------------------------
class FastAllocNode:public TypeNode{
public:
  FastAllocNode( Node *kid_node, Node *size_in_bytes, const TypeOopPtr *tp, Node *length );
  virtual int Opcode() const;
};


#endif // MACRO_HPP
