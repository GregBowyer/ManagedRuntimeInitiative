/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "cfgnode.hpp"
#include "connode.hpp"
#include "locknode.hpp"
#include "macro.hpp"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "node.hpp"
#include "phaseX.hpp"
#include "runtime.hpp"
#include "sharedRuntime.hpp"
#include "subnode.hpp"

Node*PhaseMacroExpand::transform_later(Node*n){
  // equivalent to _gvn.transform in GraphKit, Ideal, etc.
  _igvn.register_new_node_with_optimizer(n);
  return n;
}

// --- opt_iff ---------------------------------------------------------------
Node* PhaseMacroExpand::opt_iff(Node* region, Node* iff) {
  IfNode *opt_iff = transform_later(iff)->as_If();

  // Fast path taken; set region slot 2
  Node *fast_taken = transform_later( new (C, 1) IfFalseNode(opt_iff) );
  region->init_req(2,fast_taken); // Capture fast-control

  // Fast path not-taken, i.e. slow path
  Node *slow_taken = transform_later( new (C, 1) IfTrueNode(opt_iff) );
  return slow_taken;
}

//=============================================================================
// Fast-Path Allocation
FastAllocNode::FastAllocNode( Node *kid_node, Node *size_in_bytes, const TypeOopPtr *tp, Node *len ): TypeNode(tp,4) {
init_req(1,kid_node);
init_req(2,size_in_bytes);
init_req(3,len);
}

//--- expand_allocation ------------------------------------------------------
void PhaseMacroExpand::expand_allocate(AllocateNode*A){
  // See if we are forced to go slow-path.
  if( A->_entry_point == (address)SharedRuntime::_new )
    return;      // Always go slow-path - required for finalizers, etc

Node*A_ctl=A->proj_out(TypeFunc::Control);
Node*A_mem=A->proj_out(TypeFunc::Memory);
Node*A_oop=A->proj_out(TypeFunc::Parms+0);

  // Inject a fast-path / slow-path diamond.  Fast-path is still milli-code,
  // which returns an oop or null - and does not block, nor GC, nor kill
  // registers.  If we have an allocation failure the fast-path leaves the
  // regs in-place for a slow-path call which DOES block, GC, etc.
Node*ctl=A->in(TypeFunc::Control);
Node*kid=A->in(AllocateNode::KID);
Node*siz=A->in(AllocateNode::AllocSize);
Node*xtr=A->in(AllocateNode::ExtraSlow);
  Node *len = A->is_AllocateArray() ? A->in(AllocateNode::ALength) : C->top();

  if ( !A_oop && ( !A->is_AllocateArray() || ( _igvn.type(A->in(AllocateNode::ALength))->higher_equal(TypeInt::POS) ) ) ) {
tty->print_cr("Dead allocation should be removed earlier");
    Unimplemented();
  }

  // Convert the array element count to a Long value,
  // and fold in the EKID value for oop-arrays.
  const TypeOopPtr *toop = A->_tf->range()->field_at(TypeFunc::Parms+0)->is_ptr()->cast_to_ptr_type(TypePtr::BotPTR)->is_oopptr();
  if( len != C->top() ) {
    assert0( A->is_AllocateArray() );
Node*lenl=transform_later(new(C,2)ConvI2LNode(len));
Node*ekid=A->in(AllocateNode::EKID);
    if( ekid->bottom_type() != TypeInt::ZERO ) {
      // Have an EKID?  Smash the array length and EKID together.
      Node *ekidl  = transform_later(new (C,2) CastP2XNode(0,ekid));
      Node *ekidshf= transform_later(new (C,3) LShiftLNode(ekidl,_igvn.intcon(32)));
      Node *combo  = transform_later(new (C,3)     OrLNode(lenl,ekidshf));
len=combo;
    } else {
len=lenl;
    }
    // Crunch arguments for matching.  The old ExtraSlow argument is used to
    // make more gating control flow in this function, but is not an argument
    // to the runtime call.  Neither is the EKID argument: the slow-path will
    // compute it's own EKID.
    A->set_req(AllocateNode::ExtraSlow,len);
A->set_req(AllocateNode::ALength,C->top());
A->set_req(AllocateNode::EKID,C->top());
  } else {
    A->set_req(AllocateNode::ExtraSlow,_igvn.zerocon(T_INT));
    assert0( !A->is_AllocateArray() );
  }
  
  // Extra slow-path test required?
  RegionNode *region2 = NULL;
  if( xtr->bottom_type() != TypeInt::ZERO ) { // Commonly, no extra test required
    // Extra slow-path tests can be required for fast-pathing reflection
    // allocation (and cloning).  If the new object requires e.g. finalization
    // or further class linking/loading.
    Node *cmp = transform_later(new(C,3)CmpINode(xtr,_igvn.zerocon(T_INT)));
Node*bol=transform_later(new(C,2)BoolNode(cmp,BoolTest::eq));
    Node *iff = new (C, 2) IfNode( ctl, bol, PROB_LIKELY_MAG(5), COUNT_UNKNOWN );
region2=new(C,3)RegionNode(3);
transform_later(region2);
    ctl = opt_iff(region2,iff);
  }

  FastAllocNode *fal = new (C, 4) FastAllocNode(kid,siz,toop,len);
  fal->set_req(0,ctl);
transform_later(fal);
  Node *mem = new (C,1) SCMemProjNode(fal);
transform_later(mem);

  Node *cmp = transform_later(new(C,3)CmpPNode(fal,_igvn.zerocon(T_OBJECT)));
Node*bol=transform_later(new(C,2)BoolNode(cmp,BoolTest::eq));
Node*iff=new(C,2)IfNode(ctl,bol,PROB_UNLIKELY_MAG(5),COUNT_UNKNOWN);
  RegionNode *region = new (C, 3) RegionNode(3);
  transform_later(region);
  Node *slow_path = opt_iff(region,iff);

  // Make the merge point
PhiNode*phimem=new(C,3)PhiNode(region,Type::MEMORY,TypePtr::BOTTOM);
transform_later(phimem);
  phimem->init_req(2,mem);    // Plug in the fast-path

PhiNode*phioop=NULL;
  if (A_oop) {
    phioop = new (C, 3) PhiNode(region,toop);
transform_later(phioop);
    phioop->init_req(2,fal);    // Plug in the fast-path
  }

_igvn.hash_delete(A_ctl);
_igvn.hash_delete(A_mem);
  if (A_oop) _igvn.hash_delete (A_oop);
_igvn.subsume_node_keep_old(A_ctl,region);
  _igvn.subsume_node_keep_old(A_mem,phimem);
  if (A_oop) _igvn.subsume_node_keep_old(A_oop,phioop);

  // Plug in the slow-path
region->init_req(1,A_ctl);
  phimem->init_req(1,A_mem);
  if (A_oop) phioop->init_req(1,A_oop);
  if( xtr->bottom_type() != TypeInt::ZERO ) { // Commonly, no extra test required
region2->init_req(1,slow_path);
    slow_path = region2;
  }
A->set_req(TypeFunc::Control,slow_path);
  // The slow-path call now directly calls into the runtime.
  A->_entry_point = (address)SharedRuntime::_new;
}

//------------------------------expand_lock_node----------------------
void PhaseMacroExpand::expand_lock_node(LockNode *lock) {
  if( !InlineFastPathLocking ) return;

  // If inlining the fast-path locking code, do it now.  Inserts a
  // diamond control-flow, with the call on the slow-path.  Fencing is
  // included in the both the FastLock test (on success) and the
  // slow-path call.  Memory Phi's are inserted at the diamond merge to
  // prevent hoisting mem ops into the fast-path side (where they might
  // bypass the FastUnlock because it does not carry memory edges).
Node*ctl=lock->in(TypeFunc::Control);
Node*obj=lock->in(TypeFunc::Parms+0);
  Node *flock = new (C, 2) FastLockNode( ctl, obj );
  RegionNode *region = new (C, 3) RegionNode(3);
  transform_later(region);
Node*slow_path=opt_iff(region,flock);
  
  // Make the merge point
PhiNode*memphi=new(C,3)PhiNode(region,Type::MEMORY,TypePtr::BOTTOM);
transform_later(memphi);
  Node *mem = lock->in(TypeFunc::Memory);
  memphi->init_req(2,mem);    // Plug in the fast-path
  
Node*lock_ctl=lock->proj_out(TypeFunc::Control);
Node*lock_mem=lock->proj_out(TypeFunc::Memory);
_igvn.hash_delete(lock_ctl);
_igvn.hash_delete(lock_mem);
_igvn.subsume_node_keep_old(lock_ctl,region);
  _igvn.subsume_node_keep_old(lock_mem,memphi);
  // Plug in the slow-path
region->init_req(1,lock_ctl);
  memphi->init_req(1,lock_mem);
lock->set_req(TypeFunc::Control,slow_path);
}

//------------------------------expand_unlock_node----------------------
void PhaseMacroExpand::expand_unlock_node(UnlockNode*lock){
  if( !InlineFastPathLocking ) return;

  // If inlining the fast-path locking code, do it now.  Inserts a
  // diamond control-flow, with the call on the slow-path.  Fencing is
  // included in the both the FastLock test (on success) and the
  // slow-path call.  Memory Phi's are inserted at the diamond merge to
  // prevent hoisting mem ops into the fast-path side (where they might
  // bypass the FastUnlock because it does not carry memory edges).
Node*ctl=lock->in(TypeFunc::Control);
Node*obj=lock->in(TypeFunc::Parms+0);
  Node *flock = new (C, 2) FastUnlockNode( ctl, obj );
  RegionNode *region = new (C, 3) RegionNode(3);
  transform_later(region);
Node*slow_path=opt_iff(region,flock);
  
  // Make the merge point
PhiNode*memphi=new(C,3)PhiNode(region,Type::MEMORY,TypePtr::BOTTOM);
transform_later(memphi);
  Node *mem = lock->in(TypeFunc::Memory);
  memphi->init_req(2,mem);    // Plug in the fast-path
  
Node*lock_ctl=lock->proj_out(TypeFunc::Control);
Node*lock_mem=lock->proj_out(TypeFunc::Memory);
_igvn.hash_delete(lock_ctl);
_igvn.hash_delete(lock_mem);
_igvn.subsume_node_keep_old(lock_ctl,region);
  _igvn.subsume_node_keep_old(lock_mem,memphi);
  // Plug in the slow-path
region->init_req(1,lock_ctl);
  memphi->init_req(1,lock_mem);
lock->set_req(TypeFunc::Control,slow_path);
}

//------------------------------expand_safepoint_node----------------------
void PhaseMacroExpand::expand_safepoint_node(SafePointNode *safe) {
  // Make a fast-path/slow-path diamond around the explicit poll
Node*tls=new(C,1)ThreadLocalNode();
transform_later(tls);
  ConLNode *off = _igvn.longcon(in_bytes(JavaThread::please_self_suspend_offset()));
  Node *adr = new (C, 4) AddPNode( C->top(), tls, off );
transform_later(adr);
  Node *pss = LoadNode::make(C, NULL, safe->in(TypeFunc::Memory), adr, TypeRawPtr::BOTTOM, TypeInt::INT, T_INT);
transform_later(pss);
  Node *cmp = new (C, 3) CmpINode( pss, _igvn.intcon(0) );
transform_later(cmp);
Node*bol=new(C,2)BoolNode(cmp,BoolTest::ne);
transform_later(bol);
  Node *region = new (C, 3) RegionNode(3);
  transform_later(region);
  IfNode*iff= new (C, 2) IfNode(safe->in(TypeFunc::Control), bol, PROB_UNLIKELY_MAG(6), COUNT_UNKNOWN );
  Node *slow_path = opt_iff(region,iff);
_igvn.hash_delete(safe);
safe->set_req(TypeFunc::Control,slow_path);
_igvn.subsume_node_keep_old(safe,region);
region->init_req(1,safe);
}

//------------------------------expand_recur----------------------------
static void expand_recur( Node *n, VectorSet &visited, Node_List &worklist ) {
  if( !n ) return;
  if( visited.test_set(n->_idx) ) return;
  switch (n->class_id()) {
  case Node::Class_Allocate:    // We use millicode instead of inline allocation
  case Node::Class_AllocateArray:
  case Node::Class_Lock:
  case Node::Class_Unlock:
case Node::Class_SafePoint:
//  case Node::Class_GetKlass:
    worklist.push(n);
  }
for(uint i=0;i<n->req();i++)
expand_recur(n->in(i),visited,worklist);
}

//------------------------------expand_macro_nodes----------------------
//  Returns true if a failure occurred.
bool PhaseMacroExpand::expand_macro_nodes() {
ResourceArea*a=Thread::current()->resource_area();
  VectorSet visited(a);
Node_List worklist(a);
  expand_recur(C->top(),visited,worklist);

  // Make sure expansion will not cause node limit to be exceeded.  Worst case is a
  // macro node gets expanded into about 50 nodes.  Allow 50% more for optimization
if(C->check_node_count(worklist.size()*10,"out of nodes before macro expansion"))
    return true;
  // expand "macro" nodes
  // nodes are removed from the macro list as they are processed
  while( worklist.size() ) {
    Node * n = worklist.pop();
    if (_igvn.type(n) == Type::TOP || (n->in(0) != NULL && n->in(0)->is_top()) ) {
      continue;
    }
    switch (n->class_id()) {
//We use millicode instead of inline allocation
case Node::Class_Allocate:expand_allocate(n->as_Allocate());break;
case Node::Class_AllocateArray:expand_allocate(n->as_AllocateArray());break;
case Node::Class_Lock:expand_lock_node(n->as_Lock());break;
case Node::Class_Unlock:expand_unlock_node(n->as_Unlock());break;
case Node::Class_SafePoint:expand_safepoint_node(n->as_SafePoint());break;
//    case Node::Class_GetKlass:       expand_klass(n);                    break;
    default:
      assert(false, "unknown node type in macro list");
    }
    if (C->failing())  return true;
  }
  _igvn.optimize();
  return false;
}

