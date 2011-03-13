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
#ifndef LOCKNODE_HPP
#define LOCKNODE_HPP


#include "cfgnode.hpp"
#include "node.hpp"
#include "opcodes.hpp"
#include "type.hpp"

//------------------------------FastLockNode-----------------------------------
class FastLockNode:public IfNode{
public:
  FastLockNode(Node *ctrl, Node *oop) : IfNode(ctrl,oop, PROB_MIN, COUNT_UNKNOWN) {
    init_class_id(Class_FastLock);
  }
  // FastLock and FastUnlockNode do not hash, we need one for each correspoding
  // LockNode/UnLockNode to avoid creating Phi's.
  virtual uint hash() const ;                  // { return NO_HASH; }
  virtual uint cmp( const Node &n ) const ;    // Always fail, except on self
  virtual int Opcode() const;
virtual const Type*Value(PhaseTransform*phase)const;
};


//------------------------------FastUnlockNode---------------------------------
class FastUnlockNode:public IfNode{
public:
  FastUnlockNode(Node *ctrl, Node *oop) : IfNode(ctrl,oop, PROB_MIN, COUNT_UNKNOWN) {
    init_class_id(Class_FastUnlock);
  }
  Node* obj_node() const { return in(1); }


  // FastLock and FastUnlockNode do not hash, we need one for each correspoding
  // LockNode/UnLockNode to avoid creating Phi's.
  virtual uint hash() const ;                  // { return NO_HASH; }
  virtual uint cmp( const Node &n ) const ;    // Always fail, except on self
  virtual int Opcode() const;
virtual const Type*Value(PhaseTransform*phase)const;
};

#endif //  LOCKNODE_HPP
