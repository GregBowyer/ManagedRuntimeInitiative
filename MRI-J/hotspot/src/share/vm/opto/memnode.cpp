/*
 * Copyright 1997-2008 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "ciInstance.hpp"
#include "ciInstanceKlass.hpp"
#include "ciMethodKlass.hpp"
#include "ciObjArray.hpp"
#include "ciObjArrayKlass.hpp"
#include "connode.hpp"
#include "javaClasses.hpp"
#include "loopnode.hpp"
#include "machnode.hpp"
#include "matcher.hpp"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "objArrayKlass.hpp"
#include "phaseX.hpp"

//=============================================================================
uint MemNode::size_of() const { return sizeof(*this); }

const TypePtr *MemNode::adr_type() const {
  Node* adr = in(Address);
  const TypePtr* cross_check = NULL;
  DEBUG_ONLY(cross_check = _adr_type);
  return calculate_adr_type(adr->bottom_type(), cross_check);
}

void MemNode::dump_spec(outputStream *st) const {
  if (in(Address) == NULL)  return; // node is dead
#ifndef ASSERT
  // fake the missing field
  const TypePtr* _adr_type = NULL;
  if (in(Address) != NULL)
    _adr_type = in(Address)->bottom_type()->isa_ptr();
#endif
  dump_adr_type(this, _adr_type, st);

  Compile* C = Compile::current();
  if( C->alias_type(_adr_type)->is_volatile() )
    st->print(" Volatile!");
}

void MemNode::dump_adr_type(const Node* mem, const TypePtr* adr_type, outputStream *st) {
  st->print(" @");
  if (adr_type == NULL) {
    st->print("NULL");
  } else {
    adr_type->dump_on(st);
    Compile* C = Compile::current();
    Compile::AliasType* atp = NULL;
    if (C->have_alias_type(adr_type))  atp = C->alias_type(adr_type);
    if (atp == NULL)
      st->print(", idx=?\?;");
    else if (atp->index() == Compile::AliasIdxBot)
      st->print(", idx=Bot;");
    else if (atp->index() == Compile::AliasIdxTop)
      st->print(", idx=Top;");
    else if (atp->index() == Compile::AliasIdxRaw)
      st->print(", idx=Raw;");
    else {
      ciField* field = atp->field();
      if (field) {
        st->print(", name=");
        field->print_name_on(st);
      }
      st->print(", idx=%d;", atp->index());
    }
  }
}

extern void print_alias_types();

//--------------------------Ideal_common---------------------------------------
// Look for degenerate control and memory inputs.  Bypass MergeMem inputs.
// Unhook non-raw memories from complete (macro-expanded) initializations.
Node *MemNode::Ideal_common(PhaseGVN *phase, bool can_reshape) {
  // If our control input is a dead region, kill all below the region
  Node *ctl = in(MemNode::Control);
  if (ctl && remove_dead_region(phase, can_reshape)) 
    return this;
  // Allocate looks like a Call but it never alters correctness of loads or
  // stores (it never acts like a null-check).
  if (ctl && ctl->is_Proj() && 
      (ctl->in(0)->Opcode() == Op_Allocate ||
       ctl->in(0)->Opcode() == Op_AllocateArray) ) {
    set_req(MemNode::Control,ctl->in(0)->in(0));
    return this;
  }

  // Ignore if memory is dead, or self-loop
  Node *mem = in(MemNode::Memory);
  if( phase->type( mem ) == Type::TOP ) return NodeSentinel; // caller will return NULL
  assert( mem != this, "dead loop in MemNode::Ideal" );

  Node *address = in(MemNode::Address);
const Type*t_adr=address->Value(phase);
if(t_adr->empty())return NodeSentinel;//caller will return NULL

  // Check for very rare guarenteed NULL input and make no changes.
  // Node must be dead, but we haven't realized it yet.
const TypePtr*tp_adr=t_adr->isa_ptr();
  if( tp_adr && tp_adr->ptr() == TypePtr::Null ) return NodeSentinel; // caller will return NULL

  // Avoid independent memory operations
  Node* old_mem = mem;

  // Azul does not need or use an Initialize node 
  //if (mem->is_Proj() && mem->in(0)->is_Initialize()) {

  // It is possible for the bottom_type of an AddP node to "lift" over time,
  // e.g., if it's base input changes to a constant.  This lifting is not
  // normally possible and does bad things: it means the existing phase->type
  // data is *lower* than the AddPNode's current bottom_type.  The fix would
  // be to not let an AddPNode's bottom_type lift over time, which means it
  // has to record it's initial Parser type internally instead of reading it
  // from the base input.  And *that* means AddPNode needs to become a
  // TypeNode, which is a major structural change.  Instead, I'll check for it
  // here and eagerly lift the phase->type(), but if this problem appears
  // again I'll likely have to do the real fix.
const Type*pt_adr=phase->type(address);
  if( t_adr != pt_adr && t_adr->higher_equal(pt_adr) )
    // An improved Type is available, record it for all other users
    phase->set_type( address, t_adr );

  // Avoid independent memory operations
  const TypePtr *tp = t_adr->is_ptr(); 
  uint alias_idx = phase->C->get_alias_index(tp);
  if( mem->is_MergeMem() ) {
    MergeMemNode* mmem = mem->as_MergeMem();
#ifdef ASSERT
    {
      // Check that current type is consistent with the alias index used during graph construction
      assert(alias_idx >= Compile::AliasIdxRaw, "must not be a bad alias_idx");
      const TypePtr *adr_t =  adr_type();
bool consistent=adr_t==NULL||adr_t->empty()||adr_t->ptr()==TypePtr::Null||phase->C->must_alias(adr_t,alias_idx);
      // Sometimes dead array references collapse to a[-1], a[-2], or a[-3]
      if( !consistent && adr_t != NULL && !adr_t->empty() && 
             tp->isa_aryptr() &&    tp->offset() == Type::OffsetBot &&
          adr_t->isa_aryptr() && adr_t->offset() != Type::OffsetBot && 
          ( adr_t->offset() == arrayOopDesc::length_offset_in_bytes() ||
            adr_t->offset() == oopDesc::mark_offset_in_bytes() ) ) {
        // don't assert if it is dead code.
        consistent = true;
      }
      if( !consistent ) {
C2OUT->print("alias_idx==%d, adr_type()==",alias_idx);if(adr_t==NULL){C2OUT->print("NULL");}else{adr_t->dump();}
C2OUT->cr();
        print_alias_types();
        assert(consistent, "adr_type must match alias idx");
      }
    }
#endif //!ASSERT
    // TypeInstPtr::NOTNULL+any is an OOP with unknown offset - generally
    // means an array I have not precisely typed yet.  Do not do any
    // alias stuff with it any time soon.
const TypeInstPtr*tinst=tp_adr->isa_instptr();
if(tp_adr->base()!=Type::AnyPtr&&
        !(tinst &&
          tinst->klass()->is_java_lang_Object() &&
          tinst->offset() == Type::OffsetBot) ) {
      // compress paths and change unreachable cycles to TOP
      // If not, we can update the input infinitely along a MergeMem cycle
      // Equivalent code in PhiNode::Ideal
      Node* m  = phase->transform(mmem);
      // If tranformed to a MergeMem, get the desired slice
      // Otherwise the returned node represents memory for every slice
      mem = (m->is_MergeMem())? m->as_MergeMem()->memory_at(alias_idx) : m;
      // Update input if it is progress over what we have now
    }
  }
  
  if (mem != old_mem) {
    PhaseIterGVN *igvn = phase->is_IterGVN();
    if( igvn ) set_req_X(MemNode::Memory, mem,igvn);
    else       set_req  (MemNode::Memory, mem);
    return this;
  }

  // Allow unrelated mem ops to bypass simple loops.
if(mem->is_Phi()&&mem->req()==3){
    int mmidx = 2;
    Node *mmem = mem->in(mmidx);
    if( !mmem->is_MergeMem() ) { // Check for MM on the other side
      mmidx = 1;
      mmem = mem->in(mmidx);
    }
    Node *flop = mem->in(3-mmidx);
    if( mmem && mmem->is_MergeMem() && mmem->as_MergeMem()->memory_at(alias_idx) == mem && // simple cycle check
        flop != mem && flop != mmem && // No dead self-loops
        (!flop->is_MergeMem() || ((MergeMemNode*)flop)->memory_at(alias_idx) != mem) ) {
set_req(MemNode::Memory,flop);
      return this;
    }
  }

  // See if this is a memory op against a new private object, by seeing if the
  // op takes its memory from a new-allocation idiom.
  Node *base = address->is_AddP() ? address->in(AddPNode::Base) : address;
  if( mem->Opcode() == Op_EscapeMemory ) {
    // If I can show the base address comes from the new allocation, then I
    // can sharpen the memory input to directly reference the un-escaped new
    // object.  If I can show the base address must predate the new object,
    // then I can sharpen the memory input to bypass the new allocation.
Node*esc=mem;
    Node *callnew_proj = esc->in(1); // Escape[private_mem]->Proj
    // Skip some intervening stores
    while( callnew_proj->is_Store() ) callnew_proj = callnew_proj->in(MemNode::Memory);
Node*callnew=callnew_proj->in(0);
    if( (phase->type(callnew_proj) != Type::TOP) &&
        (base->in(0) == callnew || // Base comes from same Allocate?
         base->Opcode() == Op_Parm) ) { // Must predate the allocation
      if( !callnew_proj->is_Proj() ) return NULL;
      // Can sharpen memory edge to either the Allocate or NOT the Allocate.
      // sharp==1 ==> Allocate (direct use of private unescaped NEW object)
      // sharp==2 ==> Address predates allocation
      int sharp = (base->in(0) == callnew) ? 1 : 2;
      uint alias_idx = phase->C->get_alias_index(t_adr->is_oopptr());
      if( is_Store() ) {
        if( !can_reshape ) {
          // Cannot do it
          // Also only legit if I can show that the ValueIn can hoist above
          // the allocation site.  I do only the trivial case here.
        } else if( in(ValueIn)->is_Con() || in(ValueIn)->Opcode() == Op_Parm ||
                   in(ValueIn)->in(0) == callnew->in(0) ) {
          PhaseIterGVN *igvn = phase->is_IterGVN();
          // Make users of this store point at the EscapeMemory, they have to
          // decide if they are aliased or not on a case-by-case basis.
          // Simply moving this store above the EscapeMemory is wrong: users
          // of this store will mistakenly believe they are also using
          // un-aliased memory.

          // Sharpen the Store's memory edge
          set_req(MemNode::Memory, esc->in(sharp));

          // Make a private EscapeMemory (if needed).  EscapeMemory's can be
          // "fat", and handle multiple alias classes coming from other "fat"
          // sources (say a normal Call prior to the allocation).
          if( esc->outcnt() ) {
            esc = new (phase->C, 3) EscapeMemoryNode(esc->in(1), esc->in(2));
igvn->register_new_node_with_optimizer(esc);
          }
          assert0( !esc->outcnt() ); // No users now
          // Users of this Store now use the EscapeMemory
          igvn->subsume_node_keep_old(this,esc);
          // Escape now uses Store, basically swapping the Store and the Escape.
phase->hash_delete(esc);
          esc->set_req(sharp,this);          

          // See if we can hoist an unrelated Store's control edge (ctl) past
          // the Allocate.  
          if( sharp==2 && callnew == ctl->in(0) ) {
            if( Opcode() == Op_StoreP ) 
Untested("make sure on sparc that the card-mark hoists as well");
Untested("hoist unrelated store control above Allocate");
            set_req(Control,ctl->in(0)->in(0));
          }
          return this;
        }
      } else {                    // Must be a Load
        set_req( MemNode::Memory, esc->in(sharp) );
        return this;
      }
    }
  }

  // let the subclass continue analyzing...
  return NULL;
}

// Helper function for proving some simple control dominations.
// Attempt to prove that control input 'dom' dominates (or equals) 'sub'.
// Already assumes that 'dom' is available at 'sub', and that 'sub'
// is not a constant (dominated by the method's StartNode).
// Used by MemNode::find_previous_store to prove that the
// control input of a memory operation predates (dominates)
// an allocation it wants to look past.
bool MemNode::detect_dominating_control(Node* dom, Node* sub) {
  if (dom == NULL)      return false;
  if (dom->is_Proj())   dom = dom->in(0);
  if (dom->is_Start())  return true; // anything inside the method
  if (dom->is_Root())   return true; // dom 'controls' a constant
  int cnt = 20;                      // detect cycle or too much effort
  while (sub != NULL) {              // walk 'sub' up the chain to 'dom'
    if (--cnt < 0)   return false;   // in a cycle or too complex
    if (sub == dom)  return true;
    if (sub->is_Start())  return false;
    if (sub->is_Root())   return false;
    Node* up = sub->in(0);
    if (sub == up && sub->is_Region()) {
      for (uint i = 1; i < sub->req(); i++) {
        Node* in = sub->in(i);
        if (in != NULL && !in->is_top() && in != sub) {
          up = in; break;            // take any path on the way up to 'dom'
        }
      }
    }
    if (sub == up)  return false;    // some kind of tight cycle
    sub = up;
  }
  return false;
}

//---------------------detect_ptr_independence---------------------------------
// Used by MemNode::find_previous_store to prove that two base
// pointers are never equal.
// The pointers are accompanied by their associated allocations,
// if any, which have been previously discovered by the caller.
bool MemNode::detect_ptr_independence(Node* p1, AllocateNode* a1,
                                      Node* p2, AllocateNode* a2,
                                      PhaseTransform* phase) {
  // Attempt to prove that these two pointers cannot be aliased.
  // They may both manifestly be allocations, and they should differ.
  // Or, if they are not both allocations, they can be distinct constants.
  // Otherwise, one is an allocation and the other a pre-existing value.
  if (a1 == NULL && a2 == NULL) {           // neither an allocation
    return (p1 != p2) && p1->is_Con() && p2->is_Con();
  } else if (a1 != NULL && a2 != NULL) {    // both allocations
    return (a1 != a2);
  } else if (a1 != NULL) {                  // one allocation a1
    // (Note:  p2->is_Con implies p2->in(0)->is_Root, which dominates.)
    return detect_dominating_control(p2->in(0), a1->in(0));
  } else { //(a2 != NULL)                   // one allocation a2
    return detect_dominating_control(p1->in(0), a2->in(0));
  }
  return false;
}


// The logic for reordering loads and stores uses four steps:
// (a) Walk carefully past stores and initializations which we
//     can prove are independent of this load.
// (b) Observe that the next memory state makes an exact match
//     with self (load or store), and locate the relevant store.
// (c) Ensure that, if we were to wire self directly to the store,
//     the optimizer would fold it up somehow.
// (d) Do the rewiring, and return, depending on some other part of
//     the optimizer to fold up the load.
// This routine handles steps (a) and (b).  Steps (c) and (d) are
// specific to loads and stores, so they are handled by the callers.
// (Currently, only LoadNode::Ideal has steps (c), (d).  More later.)
//
Node* MemNode::find_previous_store(PhaseTransform* phase) {
  Node*         ctrl   = in(MemNode::Control);
  Node*         adr    = in(MemNode::Address);
  intptr_t      offset = 0;
  Node*         base   = AddPNode::Ideal_base_and_offset(adr, phase, offset);
  AllocateNode* alloc  = AllocateNode::Ideal_allocation(base, phase);

  if (offset == Type::OffsetBot)
    return NULL;            // cannot unalias unless there are precise offsets

  intptr_t size_in_bytes = memory_size();

  Node* mem = in(MemNode::Memory);   // start searching here...

  int cnt = 50;             // Cycle limiter
  for (;;) {                // While we can dance past unrelated stores...
    if (--cnt < 0)  break;  // Caught in cycle or a complicated dance?

    if (mem->is_Store()) {
      Node* st_adr = mem->in(MemNode::Address);
      intptr_t st_offset = 0;
      Node* st_base = AddPNode::Ideal_base_and_offset(st_adr, phase, st_offset);
      if (st_base == NULL)
        break;              // inscrutable pointer
      if (st_offset != offset && st_offset != Type::OffsetBot) {
        const int MAX_STORE = BytesPerLong;
        if (st_offset >= offset + size_in_bytes ||
            st_offset <= offset - MAX_STORE ||
            st_offset <= offset - mem->as_Store()->memory_size()) {
          // Success:  The offsets are provably independent.
          // (You may ask, why not just test st_offset != offset and be done?
          // The answer is that stores of different sizes can co-exist
          // in the same sequence of RawMem effects.  We sometimes initialize
          // a whole 'tile' of array elements with a single jint or jlong.)
          mem = mem->in(MemNode::Memory);
          continue;           // (a) advance through independent store memory
        }
      }
      if (st_base != base &&
          detect_ptr_independence(base, alloc,
                                  st_base,
                                  AllocateNode::Ideal_allocation(st_base, phase),
                                  phase)) {
        // Success:  The bases are provably independent.
        mem = mem->in(MemNode::Memory);
        continue;           // (a) advance through independent store memory
      }

      // (b) At this point, if the bases or offsets do not agree, we lose,
      // since we have not managed to prove 'this' and 'mem' independent.
      if (st_base == base && st_offset == offset) {
        return mem;         // let caller handle steps (c), (d)
      }

      // Azul does not need or use an Initialize node 
      //} else if (mem->is_Proj() && mem->in(0)->is_Initialize()) {
    }

    // Unless there is an explicit 'continue', we must bail out here,
    // because 'mem' is an inscrutable memory state (e.g., a call).
    break;
  }

  return NULL;              // bail out
}

//----------------------calculate_adr_type-------------------------------------
// Helper function.  Notices when the given type of address hits top or bottom.
// Also, asserts a cross-check of the type against the expected address type.
const TypePtr* MemNode::calculate_adr_type(const Type* t, const TypePtr* cross_check) {
  if (t == Type::TOP)  return NULL; // does not touch memory any more?
#ifdef PRODUCT
  cross_check = NULL;
#else
  if (!VerifyAliases || is_error_reported() || Node::in_dump())  cross_check = NULL;
#endif
  const TypePtr* tp = t->isa_ptr();
  if (tp == NULL) {
    assert(cross_check == NULL || cross_check == TypePtr::BOTTOM, "expected memory type must be wide");
    return TypePtr::BOTTOM;           // touches lots of memory
  } else {
#ifdef ASSERT
    // %%%% [phh] We don't check the alias index if cross_check is
    //            TypeRawPtr::BOTTOM.  Needs to be investigated.
    if (cross_check != NULL &&
        cross_check != TypePtr::BOTTOM &&
        cross_check != TypeRawPtr::BOTTOM) {
      // Recheck the alias index, to see if it has changed (due to a bug).
      Compile* C = Compile::current();
      assert(C->get_alias_index(cross_check) == C->get_alias_index(tp),
             "must stay in the original alias category");
      // The type of the address must be contained in the adr_type,
      // disregarding "null"-ness.
      // (We make an exception for TypeRawPtr::BOTTOM, which is a bit bucket.)
      const TypePtr* tp_notnull = tp->join(TypePtr::NOTNULL)->is_ptr();
      assert(cross_check->meet(tp_notnull) == cross_check,
             "real address must not escape from expected memory type");
    }
#endif
    return tp;
  }
}

//------------------------adr_phi_is_loop_invariant----------------------------
// A helper function for Ideal_DU_postCCP to check if a Phi in a counted 
// loop is loop invariant. Make a quick traversal of Phi and associated 
// CastPP nodes, looking to see if they are a closed group within the loop.
bool MemNode::adr_phi_is_loop_invariant(Node* adr_phi, Node* cast) {
  // The idea is that the phi-nest must boil down to only CastPP nodes
  // with the same data. This implies that any path into the loop already 
  // includes such a CastPP, and so the original cast, whatever its input, 
  // must be covered by an equivalent cast, with an earlier control input.
  ResourceMark rm;

  // The loop entry input of the phi should be the unique dominating 
  // node for every Phi/CastPP in the loop.
  Unique_Node_List closure;
  closure.push(adr_phi->in(LoopNode::EntryControl));

  // Add the phi node and the cast to the worklist.
  Unique_Node_List worklist;
  worklist.push(adr_phi);
  if( cast != NULL ){
    if( !cast->is_ConstraintCast() ) return false;
    worklist.push(cast);
  }

  // Begin recursive walk of phi nodes.
  while( worklist.size() ){
    // Take a node off the worklist
    Node *n = worklist.pop();
    if( !closure.member(n) ){
      // Add it to the closure.
      closure.push(n);
      // Make a sanity check to ensure we don't waste too much time here.
      if( closure.size() > 20) return false;
      // This node is OK if:
      //  - it is a cast of an identical value
      //  - or it is a phi node (then we add its inputs to the worklist)
      // Otherwise, the node is not OK, and we presume the cast is not invariant
      if( n->is_ConstraintCast() ){
        worklist.push(n->in(1));
      } else if( n->is_Phi() ) {
        for( uint i = 1; i < n->req(); i++ ) {
worklist.push(n->in(i));//Check all inputs
        }
      } else {
        return false;
      }
    }
  }

  // Quit when the worklist is empty, and we've found no offending nodes.
  return true;
}

//------------------------------Ideal_DU_postCCP-------------------------------
// Find any cast-away of null-ness and keep its control.  Null cast-aways are
// going away in this pass and we need to make this memory op depend on the
// gating null check.

// I tried to leave the CastPP's in.  This makes the graph more accurate in
// some sense; we get to keep around the knowledge that an oop is not-null
// after some test.  Alas, the CastPP's interfere with GVN (some values are
// the regular oop, some are the CastPP of the oop, all merge at Phi's which
// cannot collapse, etc).  This cost us 10% on SpecJVM, even when I removed
// some of the more trivial cases in the optimizer.  Removing more useless
// Phi's started allowing Loads to illegally float above null checks.  I gave
// up on this approach.  CNC 10/20/2000
Node *MemNode::Ideal_DU_postCCP( PhaseCCP *ccp ) {
  // Need a null check?  Regular static accesses do not because they are 
  // from constant addresses.  Array ops are gated by the range check (which
  // always includes a NULL check).  Just check field ops.
if(in(MemNode::Control))return NULL;

return Ideal_DU_postCCP_shared(ccp,this,in(MemNode::Address));
}

Node *MemNode::Ideal_DU_postCCP_shared( PhaseCCP *ccp, Node *n, Node *adr ) {
  Node *skipped_cast = NULL;
  // Scan upwards for the highest location we can place this memory op.
  while( true ) {
    switch( adr->Opcode() ) {
      
    case Op_AddP:             // No change to NULL-ness, so peek thru AddP's
      adr = adr->in(AddPNode::Base);
      continue;
      
    case Op_CastPP:
      // If the CastPP is useless, just peek on through it.
      if( ccp->type(adr) == ccp->type(adr->in(1)) ) {
        // Remember the cast that we've peeked though. If we peek
        // through more than one, then we end up remembering the highest
        // one, that is, if in a loop, the one closest to the top.
        skipped_cast = adr;
        adr = adr->in(1);
        continue;
      }
      // CastPP is going away in this pass!  We need this memory op to be
      // control-dependent on the test that is guarding the CastPP.
ccp->hash_delete(n);
n->set_req(MemNode::Control,adr->in(0));
ccp->hash_insert(n);
return n;

    case Op_Phi:
      // Attempt to float above a Phi to some dominating point.
      if( adr->in(0) != NULL && adr->in(0)->is_CountedLoop() ) {
        // If we've already peeked through a Cast (which could have set the
        // control), we can't float above a Phi, because the skipped Cast
        // may not be loop invariant.
        if (adr_phi_is_loop_invariant(adr, skipped_cast)) {
          adr = adr->in(1);
          continue;
        }
      }
      
      // Intentional fallthrough!
      
      // No obvious dominating point.  The mem op is pinned below the Phi
      // by the Phi itself.  If the Phi goes away (no true value is merged)
      // then the mem op can float, but not indefinitely.  It must be pinned
      // behind the controls leading to the Phi.
    case Op_CheckCastPP:
      // These usually stick around to change address type, however a
      // useless one can be elided and we still need to pick up a control edge
      if (adr->in(0) == NULL) {
        // This CheckCastPP node has NO control and is likely useless. But we 
        // need check further up the ancestor chain for a control input to keep 
        // the node in place. 4959717.
        skipped_cast = adr;
        adr = adr->in(1);
        continue;
      }
ccp->hash_delete(n);
n->set_req(MemNode::Control,adr->in(0));
ccp->hash_insert(n);
return n;

      // List of "safe" opcodes; those that implicitly block the memory
      // op below any null check.
    case Op_KID2Klass:
    case Op_CastX2P:          // no null checks on native pointers
    case Op_Parm:             // 'this' pointer is not null
    case Op_LoadP:            // Loading from within a klass
    case Op_LoadKlass:        // Loading from within a klass
    case Op_ConP:             // Loading from a klass
    case Op_Con:              // Reading from TLS
    case Op_CMoveP:           // CMoveP is pinned
      break;                  // No progress

    case Op_Proj:             // Direct call to an allocation routine
case Op_SCMemProj://Memory state from CAS ops
#ifdef ASSERT
      {
        assert(adr->as_Proj()->_con == TypeFunc::Parms, "must be return value");
        const Node* call = adr->in(0);
if(call->is_CallRuntime()){
          // multi-array allocate

          // We further presume that this is one of
          // new_instance_Java, new_array_Java, or
          // the like, but do not assert for this.
        } else if( call->is_Allocate() ) {
          // similar case to new_instance_Java, etc.    
        } else if (!call->is_CallLeaf()) {
          // Projections from fetch_oop (OSR) are allowed as well.
          ShouldNotReachHere();
        }
      }
#endif
      break;
    default:
      ShouldNotReachHere();
    }
    break;
  }

  return  NULL;               // No progress
}


//=============================================================================
uint LoadNode::size_of() const { return sizeof(*this); }
uint LoadNode::cmp( const Node &n ) const
{ return !Type::cmp( _type, ((LoadNode&)n)._type ); }
const Type *LoadNode::bottom_type() const { return _type; }
uint LoadNode::ideal_reg() const { 
  return Matcher::base2reg[_type->base()];
}

void LoadNode::dump_spec(outputStream *st) const { 
  MemNode::dump_spec(st);
  st->print(" #"); _type->dump_on(st);
}


//----------------------------LoadNode::make-----------------------------------
// Polymorphic factory method:
Node*LoadNode::make(Compile*C,Node*ctl,Node*mem,Node*adr,const TypePtr*adr_type,const Type*rt,BasicType bt){
  // sanity check the alias category against the created node type
  assert(!(adr_type->isa_aryptr() &&
           adr_type->offset() == arrayOopDesc::length_offset_in_bytes()),
         "use LoadRangeNode instead");
  // If we attempt to load an abstract class w/no implementors, it must be null
if(rt==TypePtr::NULL_PTR)
    return new (C, 1) ConPNode(TypePtr::NULL_PTR);
  switch (bt) {
  case T_BOOLEAN:
  case T_BYTE:    return new (C, 3) LoadBNode(ctl, mem, adr, adr_type, rt->is_int()    );
  case T_INT:     return new (C, 3) LoadINode(ctl, mem, adr, adr_type, rt->is_int()    );
  case T_CHAR:    return new (C, 3) LoadCNode(ctl, mem, adr, adr_type, rt->is_int()    );
  case T_SHORT:   return new (C, 3) LoadSNode(ctl, mem, adr, adr_type, rt->is_int()    );
  case T_LONG:    return new (C, 3) LoadLNode(ctl, mem, adr, adr_type, rt->is_long()   );
  case T_FLOAT:   return new (C, 3) LoadFNode(ctl, mem, adr, adr_type, rt              );
  case T_DOUBLE:  return new (C, 3) LoadDNode(ctl, mem, adr, adr_type, rt              );
  case T_ADDRESS: return new (C, 3) LoadPNode(ctl, mem, adr, adr_type, rt->is_ptr()    );
  case T_OBJECT:  return new (C, 3) LoadPNode(ctl, mem, adr, adr_type, rt->is_oopptr());
  }
  ShouldNotReachHere();
  return (LoadNode*)NULL;
}


//------------------------------hash-------------------------------------------
uint LoadNode::hash() const {
  // unroll addition of interesting fields
  return (uintptr_t)in(Control) + (uintptr_t)in(Memory) + (uintptr_t)in(Address);
}

//------------------------------can_see_stored_value----------------------
// This routine exists to make sure this set of tests is done the same
// everywhere.  We need to make a coordinated change: first LoadNode::Ideal
// will change the graph shape in a way which makes memory alive twice at the
// same time (uses the Oracle model of aliasing), then some
// LoadLNode::Identity will fold things back to the equivalence-class model
// of aliasing.
Node* MemNode::can_see_stored_value(Node* st, PhaseTransform* phase) const {
  Node* ld_adr = in(MemNode::Address);

  const TypeInstPtr* tp = phase->type(ld_adr)->isa_instptr();
  Compile::AliasType* atp = tp != NULL ? phase->C->alias_type(tp) : NULL;
  if (EliminateAutoBox && atp != NULL && atp->index() >= Compile::AliasIdxRaw &&
      atp->field() != NULL && !atp->field()->is_volatile()) {
    uint alias_idx = atp->index();
    bool final = atp->field()->is_final();
    Node* result = NULL;
    Node* current = st;
    // Skip through chains of MemBarNodes checking the MergeMems for
    // new states for the slice of this load.  Stop once any other
    // kind of node is encountered.  Loads from final memory can skip
    // through any kind of MemBar but normal loads shouldn't skip
    // through MemBarAcquire since the could allow them to move out of
    // a synchronized region.
    while (current->is_Proj()) {
      int opc = current->in(0)->Opcode();
      if ((final && opc == Op_MemBarAcquire) ||
          opc == Op_MemBarRelease || opc == Op_MemBarCPUOrder) {
        Node* mem = current->in(0)->in(TypeFunc::Memory);
        if (mem->is_MergeMem()) {
          MergeMemNode* merge = mem->as_MergeMem();
          Node* new_st = merge->memory_at(alias_idx);
          if (new_st == merge->base_memory()) {
            // Keep searching
            current = merge->base_memory();
            continue;
          }
          // Save the new memory state for the slice and fall through
          // to exit.
          result = new_st;
        }
      }
      break;
    }
    if (result != NULL) {
      st = result;
    }
  }


  // Loop around twice in the case Load -> Initialize -> Store.
  // (See PhaseIterGVN::add_users_to_worklist, which knows about this case.)
  for (int trip = 0; trip <= 1; trip++) {

    if (st->is_Store()) {
      Node* st_adr = st->in(MemNode::Address);
if(!phase->eqv_uncast(st_adr,ld_adr)){
        // Try harder before giving up...  Match raw and non-raw pointers.
        intptr_t st_off = 0;
        AllocateNode* alloc = AllocateNode::Ideal_allocation(st_adr, phase, st_off);
        if (alloc == NULL)       return NULL;
        intptr_t ld_off = 0;
        AllocateNode* allo2 = AllocateNode::Ideal_allocation(ld_adr, phase, ld_off);
        if (alloc != allo2)      return NULL;
        if (ld_off != st_off)    return NULL;
        // At this point we have proven something like this setup:
        //  A = Allocate(...)
        //  L = LoadQ(,  AddP(CastPP(, A.Parm),, #Off))
        //  S = StoreQ(, AddP(,        A.Parm  , #Off), V)
        // (Actually, we haven't yet proven the Q's are the same.)
        // In other words, we are loading from a casted version of
        // the same pointer-and-offset that we stored to.
        // Thus, we are able to replace L by V.
      }
      // Now prove that we have a LoadQ matched to a StoreQ, for some Q.
      if (store_Opcode() != st->Opcode())
        return NULL;
      return st->in(MemNode::ValueIn);
    }

    intptr_t offset = 0;  // scratch

    // A load from a freshly-created object always returns zero.
    // (This can happen after LoadNode::Ideal resets the load's memory input
    // to find_captured_store, which returned InitializeNode::zero_memory.)
    if (st->is_Proj() && st->in(0)->is_Allocate() &&
        st->in(0) == AllocateNode::Ideal_allocation(ld_adr, phase, offset) &&
        offset >= st->in(0)->as_Allocate()->minimum_header_size()) {
      // return a zero value for the load's basic type
      // (This is one of the few places where a generic PhaseTransform
      // can create new nodes.  Think of it as lazily manifesting
      // virtually pre-existing constants.)
      return phase->zerocon(memory_type());
    }

    // Azul does not need or use an Initialize node 
    //if (st->is_Proj() && st->in(0)->is_Initialize()) {


    break;
  }

  return NULL;
}

//------------------------------Identity---------------------------------------
// Loads are identity if previous store is to same address
Node *LoadNode::Identity( PhaseTransform *phase ) {
  // If the previous store-maker is the right kind of Store, and the store is
  // to the same address, then we are equal to the value stored.
  Node* mem = in(MemNode::Memory);
  Node* value = can_see_stored_value(mem, phase);
  if( value ) {
    // byte, short & char stores truncate naturally.
    // A load has to load the truncated value which requires
    // some sort of masking operation and that requires an
    // Ideal call instead of an Identity call.
    if (memory_size() < BytesPerInt) {
      // If the input to the store does not fit with the load's result type,
      // it must be truncated via an Ideal call.
      if (!phase->type(value)->higher_equal(phase->type(this)))
        return this;
    }
    // (This works even when value is a Con, but LoadNode::Value
    // usually runs first, producing the singleton type of the Con.)
    return value;
  }

  // Check for direct klass load from a new Object.
if(mem->is_Proj()&&
      (mem->in(0)->Opcode() == Op_Allocate ||
       mem->in(0)->Opcode() == Op_AllocateArray) &&
      // Do not trip up over loads from TLS pending_exception coming after a
      // new-allocation call.
      Compile::current()->get_alias_index(adr_type()) != Compile::AliasIdxRaw ) {
    // Memory & address both coming from Allocate, right?
Node*ptr=in(Address);
    if( ptr->is_AddP() ) {
      Node *base = ptr->in(AddPNode::Base);
      // Make sure the base is coming from the memory edge - it is possible
      // we're aliased against some unrelated Allocate of the same type.
      if( base->is_Proj() && base->in(0) == mem->in(0) &&
          base == ptr->in(AddPNode::Address) ) {
        intptr_t off = 0;
        if( ptr->in(AddPNode::Offset)->get_intptr_t(&off) ) {
          if( off == arrayOopDesc::length_offset_in_bytes() &&
              mem->in(0)->Opcode() == Op_AllocateArray ) 
            return base->in(0)->in(AllocateNode::ALength);
        // Other offsets always return a typed-zero from the Value call.
        }
      }
    }
  }

  return this;
}


// Returns true if the AliasType refers to the field that holds the
// cached box array.  Currently only handles the IntegerCache case.
static bool is_autobox_cache(Compile::AliasType* atp) {
  if (atp != NULL && atp->field() != NULL) {
    ciField* field = atp->field();
    ciSymbol* klass = field->holder()->name();
    if (field->name() == ciSymbol::cache_field_name() &&
        field->holder()->uses_default_loader() &&
        klass == ciSymbol::java_lang_Integer_IntegerCache()) {
      return true;
    }
  }
  return false;
}

// Fetch the base value in the autobox array
static bool fetch_autobox_base(Compile::AliasType* atp, int& cache_offset) {
  if (atp != NULL && atp->field() != NULL) {
    ciField* field = atp->field();
    ciSymbol* klass = field->holder()->name();
    if (field->name() == ciSymbol::cache_field_name() &&
        field->holder()->uses_default_loader() &&
        klass == ciSymbol::java_lang_Integer_IntegerCache()) {
      assert(field->is_constant(), "what?");
      ciObjArray* array = field->constant_value().as_object()->as_obj_array();
      // Fetch the box object at the base of the array and get its value
      ciInstance* box = array->obj_at(0)->as_instance();
      ciInstanceKlass* ik = box->klass()->as_instance_klass();
      if (ik->nof_nonstatic_fields() == 1) {
        // This should be true nonstatic_field_at requires calling
        // nof_nonstatic_fields so check it anyway
        ciConstant c = box->field_value(ik->nonstatic_field_at(0));
        cache_offset = c.as_int();
      }
      return true;
    }
  }
  return false;
}

// Returns true if the AliasType refers to the value field of an
// autobox object.  Currently only handles Integer.
static bool is_autobox_object(Compile::AliasType* atp) {
  if (atp != NULL && atp->field() != NULL) {
    ciField* field = atp->field();
    ciSymbol* klass = field->holder()->name();
    if (field->name() == ciSymbol::value_name() &&
        field->holder()->uses_default_loader() &&
        klass == ciSymbol::java_lang_Integer()) {
      return true;
    }
  }
  return false;
}


// We're loading from an object which has autobox behaviour.
// If this object is result of a valueOf call we'll have a phi
// merging a newly allocated object and a load from the cache.
// We want to replace this load with the original incoming
// argument to the valueOf call.
Node* LoadNode::eliminate_autobox(PhaseGVN* phase) {
  Node* base = in(Address)->in(AddPNode::Base);
  if (base->is_Phi() && base->req() == 3) {
    AllocateNode* allocation = NULL;
    int allocation_index = -1;
    int load_index = -1;
    for (uint i = 1; i < base->req(); i++) {
      allocation = AllocateNode::Ideal_allocation(base->in(i), phase);
      if (allocation != NULL) {
        allocation_index = i;
        load_index = 3 - allocation_index;
        break;
      }
    }
    LoadNode* load = NULL;
    if (allocation != NULL && base->in(load_index)->is_Load()) {
      load = base->in(load_index)->as_Load();
    }
    if (load != NULL && in(Memory)->is_Phi() && in(Memory)->in(0) == base->in(0)) {
      // Push the loads from the phi that comes from valueOf up
      // through it to allow elimination of the loads and the recovery
      // of the original value.
      Node* mem_phi = in(Memory);
      Node* offset = in(Address)->in(AddPNode::Offset);

      Node* in1 = clone();
      Node* in1_addr = in1->in(Address)->clone();
      in1_addr->set_req(AddPNode::Base, base->in(allocation_index));
      in1_addr->set_req(AddPNode::Address, base->in(allocation_index));
      in1_addr->set_req(AddPNode::Offset, offset);
      in1->set_req(0, base->in(allocation_index));
      in1->set_req(Address, in1_addr);
      in1->set_req(Memory, mem_phi->in(allocation_index));

      Node* in2 = clone();
      Node* in2_addr = in2->in(Address)->clone();
      in2_addr->set_req(AddPNode::Base, base->in(load_index));
      in2_addr->set_req(AddPNode::Address, base->in(load_index));
      in2_addr->set_req(AddPNode::Offset, offset);
      in2->set_req(0, base->in(load_index));
      in2->set_req(Address, in2_addr);
      in2->set_req(Memory, mem_phi->in(load_index));

      in1_addr = phase->transform(in1_addr);
      in1 =      phase->transform(in1);
      in2_addr = phase->transform(in2_addr);
      in2 =      phase->transform(in2);

      PhiNode* result = PhiNode::make_blank(base->in(0), this);
      result->set_req(allocation_index, in1);
      result->set_req(load_index, in2);
      return result;
    }
  } else if (base->is_Load()) {
    // Eliminate the load of Integer.value for integers from the cache
    // array by deriving the value from the index into the array.
    // Capture the offset of the load and then reverse the computation.
    Node* load_base = base->in(Address)->in(AddPNode::Base);
    if (load_base != NULL) {
      Compile::AliasType* atp = phase->C->alias_type(load_base->adr_type());
      intptr_t cache_offset;
      int shift = -1;
      Node* cache = NULL;
      if (is_autobox_cache(atp)) {
        shift  = exact_log2(type2aelembytes[T_OBJECT]);
        cache = AddPNode::Ideal_base_and_offset(load_base->in(Address), phase, cache_offset);
      }
      if (cache != NULL && base->in(Address)->is_AddP()) {
        Node* elements[4];
        int count = base->in(Address)->as_AddP()->unpack_offsets(elements, ARRAY_SIZE(elements));
int cache_low=0;
        if (count > 0 && fetch_autobox_base(atp, cache_low)) {
          int offset = arrayOopDesc::base_offset_in_bytes(memory_type()) - (cache_low << shift);
          // Add up all the offsets making of the address of the load
          Node* result = elements[0];
          for (int i = 1; i < count; i++) {
result=phase->transform(new(phase->C,3)AddLNode(result,elements[i]));
          }
          // Remove the constant offset from the address and then
          // remove the scaling of the offset to recover the original index.
result=phase->transform(new(phase->C,3)AddLNode(result,phase->longcon(-offset)));
if(result->Opcode()==Op_LShiftL&&result->in(2)==phase->intcon(shift)){
            // Peel the shift off directly but wrap it in a dummy node
            // since Ideal can't return existing nodes
result=new(phase->C,3)RShiftLNode(result->in(1),phase->intcon(0));
          } else {
result=new(phase->C,3)RShiftLNode(result,phase->intcon(shift));
          }
          result = new (phase->C, 2) ConvL2INode(phase->transform(result));
          return result;
        }
      }
    }
  }
  return NULL;
}


//------------------------------Ideal------------------------------------------
// If the load is from Field memory and the pointer is non-null, we can
// zero out the control input.
// If the offset is constant and the base is an object allocation,
// try to hook me up to the exact initializing store.
Node *LoadNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node* p = MemNode::Ideal_common(phase, can_reshape);
  if (p)  return (p == NodeSentinel) ? NULL : p;

  Node* ctrl    = in(MemNode::Control);
  Node* mem     = in(MemNode::Memory);
  Node* address = in(MemNode::Address);

  // Skip up past a SafePoint control.  Cannot do this for Stores because
  // pointer stores & cardmarks must stay on the same side of a SafePoint.
  if( ctrl != NULL && ctrl->Opcode() == Op_SafePoint && 
phase->C->get_alias_index(phase->type(address)->is_ptr())!=Compile::AliasIdxRaw&&
      // Cannot hoist loads of the mark/lock word past a Safepoint because the
      // Safepoint may deflate the monitor, leaving the loaded lock word
      // pointing to a stale, deflated (and possibly recycled!) monitor.
      (_guarding_test_is_precise || adr_type()->offset() != oopDesc::mark_offset_in_bytes()) ) {
    ctrl = ctrl->in(0);
    set_req(MemNode::Control,ctrl);
  }
  
  // Check for useless control edge in some common special cases
  if (in(MemNode::Control) != NULL) {
    intptr_t ignore = 0;
    Node*    base   = AddPNode::Ideal_base_and_offset(address, phase, ignore);
    if (base != NULL
        && phase->type(base)->higher_equal(TypePtr::NOTNULL)
        && detect_dominating_control(base->in(0), phase->C->start())) {
      // A method-invariant, non-null address (constant or 'this' argument).
      set_req(MemNode::Control, NULL);
    }
  }

  if (EliminateAutoBox && can_reshape && in(Address)->is_AddP()) {
    Node* base = in(Address)->in(AddPNode::Base);
    if (base != NULL) {
      Compile::AliasType* atp = phase->C->alias_type(adr_type());
      if (is_autobox_object(atp)) {
        Node* result = eliminate_autobox(phase);
        if (result != NULL) return result;
      }
    }
  }

  // Check for prior store with a different offset; make Load
  // independent.  Skip through any number of them.  Bail out if the stores
  // are in an endless dead cycle and report no progress.  This is a key
  // transform for Reflection.  However, if after skipping through the Stores
  // we can't then fold up against a prior store do NOT do the transform as
  // this amounts to using the 'Oracle' model of aliasing.  It leaves the same
  // array memory alive twice: once for the hoisted Load and again after the
  // bypassed Store.  This situation only works if EVERYBODY who does
  // anti-dependence work knows how to bypass.  I.e. we need all
  // anti-dependence checks to ask the same Oracle.  Right now, that Oracle is
  // the alias index stuff.  So instead, peek through Stores and IFF we can
  // fold up, do so.
  Node* prev_mem = find_previous_store(phase);
  // Steps (a), (b):  Walk past independent stores to find an exact match.
  if (prev_mem != NULL && prev_mem != in(MemNode::Memory)) {
    // (c) See if we can fold up on the spot, but don't fold up here.
    // Fold-up might require truncation (for LoadB/LoadS/LoadC) or
    // just return a prior value, which is done by Identity calls.
    if (can_see_stored_value(prev_mem, phase)) {
      // Make ready for step (d):
      set_req(MemNode::Memory, prev_mem);
      return this;
    }
  }

  return NULL;                  // No further progress
}

// Helper to recognize certain Klass fields which are invariant across
// some group of array types (e.g., int[] or all T[] where T < Object).
const Type*
LoadNode::load_array_final_field(const TypeKlassPtr *tkls,
                                 ciKlass* klass) const {
  if (tkls->offset() == Klass::modifier_flags_offset_in_bytes() + (int)sizeof(oopDesc)) {
    // The field is Klass::_modifier_flags.  Return its (constant) value.
    // (Folds up the 2nd indirection in aClassConstant.getModifiers().)
    assert(this->Opcode() == Op_LoadI, "must load an int from _modifier_flags");
    return TypeInt::make(klass->modifier_flags());
  }
  if (tkls->offset() == Klass::access_flags_offset_in_bytes() + (int)sizeof(oopDesc)) {
    // The field is Klass::_access_flags.  Return its (constant) value.
    // (Folds up the 2nd indirection in Reflection.getClassAccessFlags(aClassConstant).)
    assert(this->Opcode() == Op_LoadI, "must load an int from _access_flags");
    return TypeInt::make(klass->access_flags());
  }
  if (tkls->offset() == Klass::layout_helper_offset_in_bytes() + (int)sizeof(oopDesc)) {
    // The field is Klass::_layout_helper.  Return its constant value if known.
    assert(this->Opcode() == Op_LoadI, "must load an int from _layout_helper");
    return TypeInt::make(klass->layout_helper());
  }

  // No match.
  return NULL;
}

//------------------------------Value-----------------------------------------
extern int hashCode_vtable_entry_offset;
const Type *LoadNode::Value( PhaseTransform *phase ) const {
  // Either input is TOP ==> the result is TOP
  Node* mem = in(MemNode::Memory);
  const Type *t1 = phase->type(mem);
  if (t1 == Type::TOP)  return Type::TOP;
  Node* adr = in(MemNode::Address);
  const Type *t2 = phase->type( adr );
  if( t2 == Type::TOP ) return Type::TOP;
  const TypePtr* tp = t2->is_ptr();
  if (TypePtr::above_centerline(tp->ptr()))  return Type::TOP;
  int off = tp->offset();
  assert(off != Type::OffsetTop, "case covered by TypePtr::empty");

  // Check for direct loads from a new Object
if(mem->is_Proj()&&
      mem->in(0)->is_Allocate() &&
      // Do not trip up over loads from TLS pending_exception coming after a
      // new-allocation call.
      Compile::current()->get_alias_index(adr_type()) != Compile::AliasIdxRaw ) {
    Node *base = adr->is_AddP() ? adr->in(AddPNode::Base) : adr;
    // Could also be an un-optimized cast-away-null
    if( base->Opcode() == Op_CastPP ) base = base->in(1);
    // Memory & address both coming from Allocate, right?
    assert0( base->is_Proj() && base->in(0) == mem->in(0) );
    if( adr->is_AddP() ) {      // Have some offset?
      intptr_t off = 0;
      if( adr->in(AddPNode::Offset)->get_intptr_t(&off) &&
          adr->in(AddPNode::Base) == adr->in(AddPNode::Address) ) {
        if( off == arrayOopDesc::length_offset_in_bytes() &&
mem->in(0)->Opcode()==Op_AllocateArray){
          return Opcode() == Op_LoadRange // LoadRange's get array ranges
            ? base->in(0)->in(TypeFunc::Parms+3)->bottom_type()
            // It is possible for a dead load from an unrolled loop to use the
            // same offset into the array as the array-length offset.  This
            // code must be dead and the results are undefined in any case.
            : Type::TOP;        // weird aliasing of dead loads
        }
      }
      return zero_type();   // Must be a field or array load from a new object
    } else {                    // No offset load from new object?
      assert0( oopDesc::mark_offset_in_bytes() == 0 );
      // Unless we can prove that the object never escaped, it is Not Correct
      // to optimize-away a load of the markword from a new object - after
      // publishing another thread might immediately change the markword via
      // locking or hashing.
      //return TypeRawPtr::make( (address)markOopDesc::prototype_without_kid() );
      return _type;
    }
  }

  // Try to guess loaded type from pointer type
  if (tp->base() == Type::AryPtr) {
    const Type *t = tp->is_aryptr()->elem();
    // Don't do this for integer types. There is only potential profit if
    // the element type t is lower than _type; that is, for int types, if _type is 
    // more restrictive than t.  This only happens here if one is short and the other
    // char (both 16 bits), and in those cases we've made an intentional decision
    // to use one kind of load over the other. See AndINode::Ideal and 4965907.
    // Also, do not try to narrow the type for a LoadKlass, regardless of offset.
    //
    // Yes, it is possible to encounter an expression like (LoadKlass p1:(AddP x x 8))
    // where the _gvn.type of the AddP is wider than 8.  This occurs when an earlier
    // copy p0 of (AddP x x 8) has been proven equal to p1, and the p0 has been
    // subsumed by p1.  If p1 is on the worklist but has not yet been re-transformed,
    // it is possible that p1 will have a type like Foo*[int+]:NotNull*+any.
    // In fact, that could have been the original type of p1, and p1 could have
    // had an original form like p1:(AddP x x (LShiftL quux 3)), where the
    // expression (LShiftL quux 3) independently optimized to the constant 8.
    if ((t->isa_int() == NULL) && (t->isa_long() == NULL) 
        && Opcode() != Op_LoadKlass) {
      // t might actually be lower than _type, if _type is a unique
      // concrete subclass of abstract class t.
      // Make sure the reference is not into the header, by comparing
      // the offset against the offset of the start of the array's data.
      // Different array types begin at slightly different offsets (12 vs. 16).
      // We choose T_BYTE as an example base type that is least restrictive
      // as to alignment, which will therefore produce the smallest
      // possible base offset.
      const int min_base_off = arrayOopDesc::base_offset_in_bytes(T_BYTE);
      if ((uint)off >= (uint)min_base_off) {  // is the offset beyond the header?
        const Type* jt = t->join(_type);
        // In any case, do not allow the join, per se, to empty out the type.
        if (jt->empty() && !t->empty()) {
          // This can happen if a interface-typed array narrows to a class type.
          jt = _type;
        }
        
        if (EliminateAutoBox) {
          // The pointers in the autobox arrays are always non-null
          Node* base = in(Address)->in(AddPNode::Base);
          if (base != NULL) {
            Compile::AliasType* atp = phase->C->alias_type(base->adr_type());
            if (is_autobox_cache(atp)) {
              return jt->join(TypePtr::NOTNULL)->is_ptr();
            }
          }
        }
        return jt;
      }
    }
  } else if (tp->base() == Type::InstPtr) {
    assert( off != Type::OffsetBot ||
            // arrays can be cast to Objects
            tp->is_oopptr()->klass()->is_java_lang_Object() ||
            // unsafe field access may not have a constant offset
            phase->C->has_unsafe_access(),
            "Field accesses must be precise" );
    // For oop loads, we expect the _type to be precise
  } else if (tp->base() == Type::KlassPtr) {
    assert( off != Type::OffsetBot ||
            // arrays can be cast to Objects
            tp->is_klassptr()->klass()->is_java_lang_Object() ||
            // also allow array-loading from the primary supertype
            // array during subtype checks
            Opcode() == Op_LoadKlass, 
            "Field accesses must be precise" );
    // For klass/static loads, we expect the _type to be precise
  }

  const TypeKlassPtr *tkls = tp->isa_klassptr();
  if (tkls != NULL && !StressReflectiveCode) {
    ciKlass* klass = tkls->klass();
    if (klass->is_loaded() && tkls->klass_is_exact()) {
      // We are loading a field from a Klass metaobject whose identity
      // is known at compile time (the type is "exact" or "precise").
      // Check for fields we know are maintained as constants by the VM.
      if (tkls->offset() == Klass::super_check_offset_offset_in_bytes() + (int)sizeof(oopDesc)) {
        // The field is Klass::_super_check_offset.  Return its (constant) value.
        // (Folds up type checking code.)
        assert(Opcode() == Op_LoadI, "must load an int from _super_check_offset");
        return TypeInt::make(klass->super_check_offset());
      }
      // Compute index into primary_supers array
juint depth=(tkls->offset()-(Klass::primary_supers_kids_offset_in_bytes()+(int)sizeof(oopDesc)))/sizeof(juint);
      // Check for overflowing; use unsigned compare to handle the negative case.
      if( depth < ciKlass::primary_super_limit() ) {
        // The field is an element of Klass::_primary_supers.  Return its (constant) value.
        // (Folds up type checking code.)
        assert(Opcode() == Op_LoadKlass, "must load a klass from _primary_supers");
        ciKlass *ss = klass->super_of_depth(depth);
return ss?TypeKlassPtr::make_kid(ss,true):TypePtr::NULL_PTR;
      }
      const Type* aift = load_array_final_field(tkls, klass);
      if (aift != NULL)  return aift;
      if (tkls->offset() == in_bytes(arrayKlass::component_mirror_offset()) + (int)sizeof(oopDesc)
          && klass->is_array_klass()) {
        // The field is arrayKlass::_component_mirror.  Return its (constant) value.
        // (Folds up aClassConstant.getComponentType, common in Arrays.copyOf.)
        assert(Opcode() == Op_LoadP, "must load an oop from _component_mirror");
        return TypeInstPtr::make(klass->as_array_klass()->component_mirror());
      }
      if (tkls->offset() == Klass::java_mirror_offset_in_bytes() + (int)sizeof(oopDesc)) {
        // The field is Klass::_java_mirror.  Return its (constant) value.
        // (Folds up the 2nd indirection in anObjConstant.getClass().)
        assert(Opcode() == Op_LoadP, "must load an oop from _java_mirror");
        return TypeInstPtr::make(klass->java_mirror());
      }
if(tkls->offset()==Klass::klassId_offset_in_bytes()+(int)sizeof(oopDesc)){
	// The field is Klass::_klassID.  Return its (constant) value.
	// (Folds up the indirection any 'new' idiom).
        return TypeKlassPtr::make_kid(tkls->klass(),true);
      }
    }
    // See if it's a load of hashCode's vtable entry for inlined hashCode calls.
    if (tkls->offset() == hashCode_vtable_entry_offset &&
        hashCode_vtable_entry_offset != 0 ) {
      // Generic unknown hashCode method
      return TypeInstPtr::make(TypePtr::NotNull,ciMethodKlass::make());
    }

    // We can still check if we are loading from the primary_supers array at a
    // shallow enough depth.  Even though the klass is not exact, entries less
    // than or equal to its super depth are correct.
if(klass->is_loaded()&&Opcode()==Op_LoadKlass){
ciType*inner=klass;
      while( inner->is_obj_array_klass() )
        inner = inner->as_obj_array_klass()->base_element_type();
      if( inner->is_instance_klass() &&
          !inner->as_instance_klass()->flags().is_interface() ) {
        // Compute index into primary_supers array
juint depth=(tkls->offset()-(Klass::primary_supers_kids_offset_in_bytes()+(int)sizeof(oopDesc)))/sizeof(juint);
        // Check for overflowing; use unsigned compare to handle the negative case.
        if( depth < ciKlass::primary_super_limit() ) {
          if( depth <= klass->super_depth() ) { // allow self-depth checks to handle self-check case
            // The field is an element of Klass::_primary_supers.  Return its
            // (constant) value.  (Folds up type checking code.)
            ciKlass *ss = klass->super_of_depth(depth);
return ss?TypeKlassPtr::make_kid(ss,true):TypePtr::NULL_PTR;
          } else {
            // If klass is a primary supertype, but we are loading past the end
            // of klass's super_depth, we can still know the resulting type is
            // at least of type klass (actually, it is guarenteed to be a
            // subklass of klass).
            return TypeKlassPtr::make(TypePtr::BotPTR,klass,0,true);
          }
        } // else tkls->offset() is 'bot'
      }
    }

    // If the type is enough to determine that the thing is not an array,
    // we can give the layout_helper a positive interval type.
    // This will help short-circuit some reflective code.
    if (tkls->offset() == Klass::layout_helper_offset_in_bytes() + (int)sizeof(oopDesc)
        && !klass->is_array_klass() // not directly typed as an array
        && !klass->is_interface()  // specifically not Serializable & Cloneable
        && !klass->is_java_lang_Object()   // not the supertype of all T[]
        ) {
      // Note:  When interfaces are reliable, we can narrow the interface
      // test to (klass != Serializable && klass != Cloneable).
      assert(Opcode() == Op_LoadI, "must load an int from _layout_helper");
      jint min_size = Klass::instance_layout_helper(oopDesc::header_size(), false);
      // The key property of this type is that it folds up tests
      // for array-ness, since it proves that the layout_helper is positive.
      // Thus, a generic value like the basic object layout helper works fine.
      return TypeInt::make(min_size, max_jint, Type::WidenMin);
    }
  }

  // If we are loading from a freshly-allocated object, produce a zero,
  // if the load is provably beyond the header of the object.
  // (Also allow a variable load from a fresh array to produce zero.)

    Node* value = can_see_stored_value(mem,phase);
    if (value != NULL && value->is_Con())
      return value->bottom_type();


  return _type;
}

//------------------------------match_edge-------------------------------------
// Do we Match on this edge index or not?  Match only the address.
uint LoadNode::match_edge(uint idx) const {
  return idx == MemNode::Address;
}

//--------------------------LoadBNode::Ideal--------------------------------------
//
//  If the previous store is to the same address as this load,
//  and the value stored was larger than a byte, replace this load
//  with the value stored truncated to a byte.  If no truncation is
//  needed, the replacement is done in LoadNode::Identity().
//
Node *LoadBNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node* mem = in(MemNode::Memory);
  Node* value = can_see_stored_value(mem,phase);
  if( value && !phase->type(value)->higher_equal( _type ) ) {
    Node *result = phase->transform( new (phase->C, 3) LShiftINode(value, phase->intcon(24)) );
    return new (phase->C, 3) RShiftINode(result, phase->intcon(24));
  }
  // Identity call will handle the case where truncation is not needed.
  return LoadNode::Ideal(phase, can_reshape);
}

//--------------------------LoadCNode::Ideal--------------------------------------
//
//  If the previous store is to the same address as this load,
//  and the value stored was larger than a char, replace this load
//  with the value stored truncated to a char.  If no truncation is
//  needed, the replacement is done in LoadNode::Identity().
//
Node *LoadCNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node* mem = in(MemNode::Memory);
  Node* value = can_see_stored_value(mem,phase);
  if( value && !phase->type(value)->higher_equal( _type ) ) 
    return new (phase->C, 3) AndINode(value,phase->intcon(0xFFFF));
  // Identity call will handle the case where truncation is not needed.
  return LoadNode::Ideal(phase, can_reshape);
}

//--------------------------LoadSNode::Ideal--------------------------------------
//
//  If the previous store is to the same address as this load,
//  and the value stored was larger than a short, replace this load
//  with the value stored truncated to a short.  If no truncation is
//  needed, the replacement is done in LoadNode::Identity().
//
Node *LoadSNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node* mem = in(MemNode::Memory);
  Node* value = can_see_stored_value(mem,phase);
  if( value && !phase->type(value)->higher_equal( _type ) ) {
    Node *result = phase->transform( new (phase->C, 3) LShiftINode(value, phase->intcon(16)) );
    return new (phase->C, 3) RShiftINode(result, phase->intcon(16));
  }
  // Identity call will handle the case where truncation is not needed.
  return LoadNode::Ideal(phase, can_reshape);
}

//=============================================================================
//------------------------------Value------------------------------------------
const Type *LoadKlassNode::Value( PhaseTransform *phase ) const {
  // Azul Note: Our objects no longer have a Klass word.  We never LoadKlass
  // from an instance Object.  Instead we might use GetKlass (with a is_kid
  // Type) to get the Klass ID, and convert a Klass ID into a Klass.

  // Either input is TOP ==> the result is TOP
  const Type *t1 = phase->type( in(MemNode::Memory) );
  if (t1 == Type::TOP)  return Type::TOP;
  Node *adr = in(MemNode::Address);
  const Type *t2 = phase->type( adr );
  if (t2 == Type::TOP)  return Type::TOP;
  const TypePtr *tp = t2->is_ptr();
  if (TypePtr::above_centerline(tp->ptr()) ||
      tp->ptr() == TypePtr::Null)  return Type::TOP;

  // See how sharp we can be.
const TypeOopPtr*toop=tp->is_oopptr();
ciKlass*K=toop->klass();
  const TypeOopPtr *xk = NULL;
  if( K ) {                     // mixed primitive arrays will have no _klass
    const TypePtr *tp2 = TypeKlassPtr::make_from_klass_unique(K);
    if( tp2->isa_oopptr() ) {   // Could be no implementors, so NULL is the only possible answer
      xk = tp2->is_oopptr();
      assert0( !(xk->klass_is_exact() && !toop->klass_is_exact()) );// how can we have K be exact but the toop's klass not exact?
      xk = TypeKlassPtr::make((toop->klass_is_exact() ? TypePtr::Constant : TypePtr::NotNull), xk->klass(),0);
    }
  }

  // Return a more precise klass, if possible
const TypeInstPtr*tinst=toop->isa_instptr();
  ciEnv *E = phase->C->env();
  if (tinst != NULL) {
ciInstanceKlass*ik=K->as_instance_klass();
    int offset = tinst->offset();
if(ik==E->Class_klass()
        && (offset == java_lang_Class::klass_offset_in_bytes() ||
            offset == java_lang_Class::array_klass_offset_in_bytes())) {
      // We are loading a special hidden field from a Class mirror object,
      // the field which points to the VM's Klass metaobject.
      ciType* t = tinst->java_mirror_type();
      // java_mirror_type returns non-null for compile-time Class constants.
      if (t != NULL) {
        // constant oop => constant klass
        if (offset == java_lang_Class::array_klass_offset_in_bytes()) {
          return TypeKlassPtr::make(ciArrayKlass::make(t));
        }
        if (!t->is_klass()) {
          // a primitive Class (e.g., int.class) has NULL for a klass field
          return TypePtr::NULL_PTR;
        }
        // (Folds up the 1st indirection in aClassConstant.getModifiers().)
        return TypeKlassPtr::make(t->as_klass());
      }
      // non-constant mirror, so we can't tell what's going on
    }
    if( !ik->is_loaded() )
      return _type;             // Bail out if not loaded
    // Azul uses KIDs, has no klass_offset_in_bytes
    //if (offset == oopDesc::klass_offset_in_bytes()) {
    //return xk;
  }

  // Check for loading klass from an array
  const TypeAryPtr *tary = tp->isa_aryptr();
  if( tary != NULL ) {
    ciKlass *tary_klass = tary->klass();
    // Azul uses KIDs, has no klass_offset_in_bytes
    //if (tary_klass != NULL   // can be NULL when at BOTTOM or TOP
    //    && tary->offset() == oopDesc::klass_offset_in_bytes()) {
  }

  // Check for loading klass from an array klass
const TypeKlassPtr*tkls=toop->isa_klassptr();
  if (tkls != NULL && !StressReflectiveCode) {
ciKlass*klass=K;
    if( !klass->is_loaded() )
      return _type;             // Bail out if not loaded
    if( klass->is_obj_array_klass() &&
        (uint)tkls->offset() == objArrayKlass::element_klass_offset_in_bytes() + sizeof(oopDesc)) {
      ciKlass* elem = klass->as_obj_array_klass()->element_klass();
      // Sharpen the element class
      const TypeOopPtr *t_elem = TypeKlassPtr::make_from_klass_unique(elem)->isa_oopptr();

      // Always returning precise element type is incorrect, 
      // e.g., element type could be object and array may contain strings
      // The array's TypeKlassPtr was declared 'precise' or 'not precise'
      // according to the element type's subclassing.
      return TypeKlassPtr::make((t_elem && t_elem->klass_is_exact()) ? TypePtr::Constant : tkls->ptr(), 
                                // If elem is an interface, only interested in t_elem if it really sharpens
                                (t_elem && t_elem->klass() != E->Object_klass()) ? t_elem->klass() : elem, 0);
    }
    if( klass->is_instance_klass() && tkls->klass_is_exact() &&
        (uint)tkls->offset() == Klass::super_offset_in_bytes() + sizeof(oopDesc)) {
      ciKlass* sup = klass->as_instance_klass()->super();
      // The field is Klass::_super.  Return its (constant) value.
      // (Folds up the 2nd indirection in aClassConstant.getSuperClass().)
      return sup ? TypeKlassPtr::make(sup) : TypePtr::NULL_PTR;
    }
  }

  // Check for loading ekid from an array
  if( tary != NULL && tary->elem()->isa_oopptr() && 
      tary->offset() == objArrayOopDesc::ekid_offset_in_bytes() ) {
    return _type->join(TypeKlassPtr::make_kid(tary->elem()->is_oopptr()->klass(),tary->klass_is_exact()));
  }

  // Bailout case
  return LoadNode::Value(phase);
}

//------------------------------Identity---------------------------------------
// To clean up reflective code, simplify k.java_mirror.as_klass to plain k.
// Also feed through the klass in Allocate(...klass...)._klass.
Node* LoadKlassNode::Identity( PhaseTransform *phase ) {
  Node* x = LoadNode::Identity(phase);
  if (x != this)  return x;

  // Take apart the address into an oop and and offset.
  // Return 'this' if we cannot.
  Node*    adr    = in(MemNode::Address);
  intptr_t offset = 0;
  Node*    base   = AddPNode::Ideal_base_and_offset(adr, phase, offset);
  if (base == NULL)     return this;
  const TypeOopPtr* toop = phase->type(adr)->isa_oopptr();
  if (toop == NULL)     return this;

  // We can fetch the klass directly through an AllocateNode.
  // This works even if the klass is not constant (clone or newArray).
if(offset==oopDesc::mark_offset_in_bytes()){
    Node* allocated_klass = AllocateNode::Ideal_klass(base, phase);
    if (allocated_klass != NULL) {
      return allocated_klass;
    }
  }

  if( base->Opcode() == Op_KID2Klass &&
      offset == Klass::klassId_offset_in_bytes() + (int)sizeof(oopDesc) ) {
    // The field is Klass::_klassID.  Folds up the indirection any 'new' or Array::copyOf idiom.
    return base->in(1);         // Return the original KID
  }

  // Simplify k.java_mirror.as_klass to plain k, where k is a klassOop.
  // Simplify ak.component_mirror.array_klass to plain ak, ak an arrayKlass.
  // See inline_native_Class_query for occurrences of these patterns.
  // Java Example:  x.getClass().isAssignableFrom(y)
  // Java Example:  Array.newInstance(x.getClass().getComponentType(), n)
  //
  // This improves reflective code, often making the Class
  // mirror go completely dead.  (Current exception:  Class
  // mirrors may appear in debug info, but we could clean them out by
  // introducing a new debug info operator for klassOop.java_mirror).
  if (toop->isa_instptr() && toop->klass() == phase->C->env()->Class_klass()
      && (offset == java_lang_Class::klass_offset_in_bytes() ||
          offset == java_lang_Class::array_klass_offset_in_bytes())) {
    // We are loading a special hidden field from a Class mirror,
    // the field which points to its Klass or arrayKlass metaobject.
    if (base->is_Load()) {
      Node* adr2 = base->in(MemNode::Address);
      const TypeKlassPtr* tkls = phase->type(adr2)->isa_klassptr();
      if (tkls != NULL && !tkls->empty()
          && (tkls->klass()->is_instance_klass() ||
              tkls->klass()->is_array_klass())
          && adr2->is_AddP()
          ) {
        int mirror_field = Klass::java_mirror_offset_in_bytes();
        if (offset == java_lang_Class::array_klass_offset_in_bytes()) {
          mirror_field = in_bytes(arrayKlass::component_mirror_offset());
        }
        if (tkls->offset() == mirror_field + (int)sizeof(oopDesc)) {
          return adr2->in(AddPNode::Base);
        }
      }
    }
  }

  return this;
}


//=============================================================================
//------------------------------Identity---------------------------------------
Node*LoadINode::Identity(PhaseTransform*phase){
#ifdef ASSERT  
  // Assert not loading a KID with a LoadI
  // Take apart the address into an oop and and offset.
  Node*    adr    = in(MemNode::Address);
  intptr_t offset = 0;
  Node*    base   = AddPNode::Ideal_base_and_offset(adr, phase, offset);
  assert0( !(base && base->Opcode() == Op_KID2Klass &&
             offset == Klass::klassId_offset_in_bytes() + (int)sizeof(oopDesc)) );
#endif
return LoadNode::Identity(phase);
}

//=============================================================================
//------------------------------Value------------------------------------------
const Type *LoadRangeNode::Value( PhaseTransform *phase ) const {
  // Either input is TOP ==> the result is TOP
  const Type *t1 = phase->type( in(MemNode::Memory) );
  if( t1 == Type::TOP ) return Type::TOP;
  Node *adr = in(MemNode::Address);
  const Type *t2 = phase->type( adr );
  if( t2 == Type::TOP ) return Type::TOP;
  const TypePtr *tp = t2->is_ptr();
  if (TypePtr::above_centerline(tp->ptr()))  return Type::TOP;
  const TypeAryPtr *tap = tp->isa_aryptr();
  if( !tap ) return _type;
  return tap->size();
}

//------------------------------Identity---------------------------------------
// Feed through the length in AllocateArray(...length...)._length.
Node* LoadRangeNode::Identity( PhaseTransform *phase ) {
  Node* x = LoadINode::Identity(phase);
  if (x != this)  return x;

  // Take apart the address into an oop and and offset.
  // Return 'this' if we cannot.
  Node*    adr    = in(MemNode::Address);
  intptr_t offset = 0;
  Node*    base   = AddPNode::Ideal_base_and_offset(adr, phase, offset);
  if (base == NULL)     return this;
  const TypeAryPtr* tary = phase->type(adr)->isa_aryptr();
  if (tary == NULL)     return this;

  // We can fetch the length directly through an AllocateArrayNode.
  // This works even if the length is not constant (clone or newArray).
  if (offset == arrayOopDesc::length_offset_in_bytes()) {
    Node* allocated_length = AllocateArrayNode::Ideal_length(base, phase);
    if (allocated_length != NULL) {
      return allocated_length;
    }
  }

  return this;
}

//=============================================================================
//---------------------------StoreNode::make-----------------------------------
// Polymorphic factory method:
StoreNode* StoreNode::make( Compile *C, Node* ctl, Node* mem, Node* adr, const TypePtr* adr_type, Node* val, BasicType bt ) {
  switch (bt) {
  case T_BOOLEAN:
  case T_BYTE:    return new (C, 4) StoreBNode(ctl, mem, adr, adr_type, val);
  case T_INT:     return new (C, 4) StoreINode(ctl, mem, adr, adr_type, val);
  case T_CHAR:
  case T_SHORT:   return new (C, 4) StoreCNode(ctl, mem, adr, adr_type, val);
  case T_LONG:    return new (C, 4) StoreLNode(ctl, mem, adr, adr_type, val);
  case T_FLOAT:   return new (C, 4) StoreFNode(ctl, mem, adr, adr_type, val);
  case T_DOUBLE:  return new (C, 4) StoreDNode(ctl, mem, adr, adr_type, val);
  case T_ADDRESS:
  case T_OBJECT:  return new (C, 4) StorePNode(ctl, mem, adr, adr_type, val);
  }
  ShouldNotReachHere();
  return (StoreNode*)NULL;
}


//--------------------------bottom_type----------------------------------------
const Type *StoreNode::bottom_type() const {
  return Type::MEMORY;
}

//------------------------------hash-------------------------------------------
uint StoreNode::hash() const {
  // unroll addition of interesting fields
  //return (uintptr_t)in(Control) + (uintptr_t)in(Memory) + (uintptr_t)in(Address) + (uintptr_t)in(ValueIn);

  // Since they are not commoned, do not hash them:
  return NO_HASH;
}

//------------------------------Ideal------------------------------------------
// Change back-to-back Store(, p, x) -> Store(m, p, y) to Store(m, p, x).
// When a store immediately follows a relevant allocation/initialization,
// try to capture it into the initialization, or hoist it above.
static bool kill_back_to_back( PhaseGVN *phase, Node *st, Node *user, int idx ) {
Node*killme=user->in(idx);
  if( !killme ) return false;

if(st->Opcode()!=killme->Opcode())return false;

  // Need to store the same address
  if( !phase->eqv_uncast( killme->in(MemNode::Address), st->in(MemNode::Address) ) ) return false;
    
  // Looking at a dead closed cycle of memory?
  if( killme == killme->in(MemNode::Memory) ||
      user   == killme->in(MemNode::Memory) ) return false;

  // If anybody other than 'this' uses 'mem', we cannot fold 'mem' away.
  // For example, 'mem' might be the final state at a conditional return.
  // Or, 'mem' might be used by some node which is live at the same time
  // 'this' is live, which might be unschedulable.  So, require exactly
  // ONE user, the 'this' store, until such time as we clone 'mem' for
  // each of 'mem's uses (thus making the exactly-1-user-rule hold true).
  if( killme->outcnt() != 1 || 
      killme->as_Store()->memory_size() > st->as_Store()->memory_size()) {
    return false;
  }

phase->hash_delete(user);
  PhaseIterGVN *igvn = phase->is_IterGVN();
  user->set_req_X( idx, killme->in(MemNode::Memory), igvn );
igvn->_worklist.push(user);
  return true;
}

Node *StoreNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  Node* p = MemNode::Ideal_common(phase, can_reshape);
  if (p)  return (p == NodeSentinel) ? NULL : p;

  // Back-to-back stores to same address?  Fold em up.
  // Generally unsafe if I have intervening uses...
  if( !can_reshape ) return NULL; // No can do (parser has undeclared users)
  
  // Look for back-to-back stores and kill the earlier one
  if( kill_back_to_back( phase, this, this, MemNode::Memory ) )
    return this;                // Found & kill back-to-back store!

  // Also check for stores across a Phi
Node*phi=in(MemNode::Memory);
  if( phi->is_Phi() && phi->outcnt() == 1 ) {
    bool killed = false;
for(uint i=1;i<phi->req();i++)
      killed |= kill_back_to_back( phase, this, phi, i );
    if( killed ) return this;   // Some progress, somewhere
  }

  // Capture an unaliased, unconditional, simple store into an initializer.
  // Or, if it is independent of the allocation, hoist it above the allocation.
  //if (ReduceFieldZeroing && can_reshape &&
  //    mem->is_Proj() && mem->in(0)->is_Initialize()) {
  // Azul does not use Initialize nodes; all our zero'ing is done by the millicode.
  //}

  return NULL;                  // No further progress
}

//------------------------------Value-----------------------------------------
const Type *StoreNode::Value( PhaseTransform *phase ) const {
  // Either input is TOP ==> the result is TOP
  const Type *t1 = phase->type( in(MemNode::Memory) );
  if( t1 == Type::TOP ) return Type::TOP;
  const Type *t2 = phase->type( in(MemNode::Address) );
  if( t2 == Type::TOP ) return Type::TOP;
  const Type *t3 = phase->type( in(MemNode::ValueIn) );
  if( t3 == Type::TOP ) return Type::TOP;
  return Type::MEMORY;
}

//------------------------------Identity---------------------------------------
// Remove redundant stores:
//   Store(m, p, Load(m, p)) changes to m.
//   Store(, p, x) -> Store(m, p, x) changes to Store(m, p, x).
Node *StoreNode::Identity( PhaseTransform *phase ) {
  Node* mem = in(MemNode::Memory);
  Node* adr = in(MemNode::Address);
  Node* val = in(MemNode::ValueIn);

  // Load then Store?  Then the Store is useless
  if (val->is_Load() && 
      phase->eqv_uncast( val->in(MemNode::Address), adr ) &&
      phase->eqv_uncast( val->in(MemNode::Memory ), mem ) &&
      val->as_Load()->store_Opcode() == Opcode()) {
    return mem;
  }

  // Two stores in a row of the same value?
  if (mem->is_Store() &&
      phase->eqv_uncast( mem->in(MemNode::Address), adr ) &&
      phase->eqv_uncast( mem->in(MemNode::ValueIn), val ) &&
      mem->Opcode() == Opcode()) {
    return mem;
  }

  // Store of zero anywhere into a freshly-allocated object?
  // Then the store is useless.
  // (It must already have been captured by the InitializeNode.)
if(phase->type(val)->is_zero_type()){
    // a newly allocated object is already all-zeroes everywhere
    if (mem->is_Proj() && mem->in(0)->is_Allocate()) {
      return mem;
    }

    // the store may also apply to zero-bits in an earlier object
    Node* prev_mem = find_previous_store(phase);
    // Steps (a), (b):  Walk past independent stores to find an exact match.
    if (prev_mem != NULL) {
      Node* prev_val = can_see_stored_value(prev_mem, phase);
      if (prev_val != NULL && phase->eqv(prev_val, val)) {
        // prev_val and val might differ by a cast; it would be good
        // to keep the more informative of the two.
        return mem;
      }
    }
  }

  // Zero-store to new memory?
  if( phase->type(val) == zero_type() &&
      mem->is_Proj() &&
      (mem->in(0)->Opcode() == Op_Allocate ||
       mem->in(0)->Opcode() == Op_AllocateArray) &&
      // Do not trip up over NULL stores to TLS pending_exception
      // coming after a new-allocation call.
      Compile::current()->get_alias_index(adr_type()) != Compile::AliasIdxRaw )
    return mem;

  return this;
}

//------------------------------match_edge-------------------------------------
// Do we Match on this edge index or not?  Match only memory & value
uint StoreNode::match_edge(uint idx) const {
  return idx == MemNode::Address || idx == MemNode::ValueIn;
}

//------------------------------cmp--------------------------------------------
// Do not common stores up together.  They generally have to be split
// back up anyways, so do not bother.
uint StoreNode::cmp( const Node &n ) const { 
  return (&n == this);          // Always fail except on self
}

//------------------------------Ideal_masked_input-----------------------------
// Check for a useless mask before a partial-word store
// (StoreB ... (AndI valIn conIa) )
// If (conIa & mask == mask) this simplifies to   
// (StoreB ... (valIn) )
Node *StoreNode::Ideal_masked_input(PhaseGVN *phase, uint mask) {
  Node *val = in(MemNode::ValueIn);
  if( val->Opcode() == Op_AndI ) {
    const TypeInt *t = phase->type( val->in(2) )->isa_int();
    if( t && t->is_con() && (t->get_con() & mask) == mask ) {
      set_req(MemNode::ValueIn, val->in(1));
      return this;
    }
  }
  return NULL;
}


//------------------------------Ideal_sign_extended_input----------------------
// Check for useless sign-extension before a partial-word store
// (StoreB ... (RShiftI _ (LShiftI _ valIn conIL ) conIR) )
// If (conIL == conIR && conIR <= num_bits)  this simplifies to
// (StoreB ... (valIn) )
Node *StoreNode::Ideal_sign_extended_input(PhaseGVN *phase, int num_bits) {
  Node *val = in(MemNode::ValueIn);
  if( val->Opcode() == Op_RShiftI ) {
    const TypeInt *t = phase->type( val->in(2) )->isa_int();
    if( t && t->is_con() && (t->get_con() <= num_bits) ) {
      Node *shl = val->in(1);
      if( shl->Opcode() == Op_LShiftI ) {
        const TypeInt *t2 = phase->type( shl->in(2) )->isa_int();
        if( t2 && t2->is_con() && (t2->get_con() == t->get_con()) ) {
          set_req(MemNode::ValueIn, shl->in(1));
          return this;
        }
      }
    }
  }
  return NULL;
}

//------------------------------value_never_loaded-----------------------------------
// Determine whether there are any possible loads of the value stored.
// For simplicity, we actually check if there are any loads from the
// address stored to, not just for loads of the value stored by this node.
//
bool StoreNode::value_never_loaded( PhaseTransform *phase) const {
  Node *adr = in(Address);
  const TypeOopPtr *adr_oop = phase->type(adr)->isa_oopptr();
  if (adr_oop == NULL)
    return false;
if(!adr_oop->singleton())
    return false; // if not a distinct instance, there may be aliases of the address
  Untested(); // is 'singleton()' the right test?
  for (DUIterator_Fast imax, i = adr->fast_outs(imax); i < imax; i++) {
    Node *use = adr->fast_out(i);
    int opc = use->Opcode();
    if (use->is_Load() || use->is_LoadStore()) {
      return false;
    }
  }
  return true;
}

//=============================================================================
//------------------------------Ideal------------------------------------------
// If the store is from an AND mask that leaves the low bits untouched, then
// we can skip the AND operation.  If the store is from a sign-extension
// (a left shift, then right shift) we can skip both.
Node *StoreBNode::Ideal(PhaseGVN *phase, bool can_reshape){
  Node *progress = StoreNode::Ideal_masked_input(phase, 0xFF);
  if( progress != NULL ) return progress;

  progress = StoreNode::Ideal_sign_extended_input(phase, 24);
  if( progress != NULL ) return progress;

  // Finally check the default case
  return StoreNode::Ideal(phase, can_reshape);
}

//=============================================================================
//------------------------------Ideal------------------------------------------
// If the store is from an AND mask that leaves the low bits untouched, then
// we can skip the AND operation
Node *StoreCNode::Ideal(PhaseGVN *phase, bool can_reshape){
  Node *progress = StoreNode::Ideal_masked_input(phase, 0xFFFF);
  if( progress != NULL ) return progress;

  progress = StoreNode::Ideal_sign_extended_input(phase, 16);
  if( progress != NULL ) return progress;

  // Finally check the default case
  return StoreNode::Ideal(phase, can_reshape);
}

//=============================================================================
//----------------------------------SCMemProjNode------------------------------
const Type * SCMemProjNode::Value( PhaseTransform *phase ) const
{
return bottom_type();//Override ProjNode::Value
}

//=============================================================================
LoadStoreNode::LoadStoreNode( Node *c, Node *mem, Node *adr, Node *val, Node *ex ) : Node(5) {
  init_req(MemNode::Control, c  );
  init_req(MemNode::Memory , mem);
  init_req(MemNode::Address, adr);
  init_req(MemNode::ValueIn, val);
  init_req(         ExpectedIn, ex );
  init_class_id(Class_LoadStore);

}

//------------------------------Idealize---------------------------------------
Node*LoadStoreNode::Ideal(PhaseGVN*phase,bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//=============================================================================
//-------------------------------adr_type--------------------------------------
// Do we Match on this edge index or not?  Do not match memory
const TypePtr* ClearArrayNode::adr_type() const {
  Node *adr = in(3);
  return MemNode::calculate_adr_type(adr->bottom_type());
}

//------------------------------match_edge-------------------------------------
// Do we Match on this edge index or not?  Do not match memory
uint ClearArrayNode::match_edge(uint idx) const {
  return idx > 1;
}

//------------------------------Identity---------------------------------------
// Clearing a zero length array does nothing
Node *ClearArrayNode::Identity( PhaseTransform *phase ) {
  return phase->type(in(2))->higher_equal(TypeInt::ZERO)  ? in(1) : this;
}

//------------------------------Idealize---------------------------------------
// Clearing a short array is faster with stores
Node *ClearArrayNode::Ideal(PhaseGVN *phase, bool can_reshape){
  // Azul only support ClearArray in words, with a 32-bit count of longs to clear
  if( Matcher::init_array_count_is_in_bytes )
    Unimplemented();            
  const int unit = BytesPerLong;
const Type*unitcnt=phase->type(in(2));
const TypeInt*t=unitcnt->isa_int();
  if (!t)  return NULL;
  if (!t->is_con())  return NULL;
  intptr_t raw_count = t->get_con();
  intptr_t size = raw_count;
  if (!Matcher::init_array_count_is_in_bytes) size *= unit;
  // Clearing nothing uses the Identity call.
  // Negative clears are possible on dead ClearArrays
  // (see jck test stmt114.stmt11402.val).
  if (size <= 0 || size % unit != 0)  return NULL;
  intptr_t count = size / unit;
  // Length too long; use fast hardware clear
  if (size > Matcher::init_array_short_size)  return NULL;
  Node *mem = in(1);
  if( phase->type(mem)==Type::TOP ) return NULL;
  Node *adr = in(3);
  const Type* at = phase->type(adr);
  if( at==Type::TOP ) return NULL;
  const TypePtr* atp = at->isa_ptr();
  // adjust atp to be the correct array element address type
  if (atp == NULL)  atp = TypePtr::BOTTOM;
  else              atp = atp->add_offset(Type::OffsetBot);
  // Get base for derived pointer purposes
  if( adr->Opcode() != Op_AddP ) Unimplemented();
  Node *base = adr->in(1);

  Node *zero = phase->makecon(TypeLong::ZERO);
Node*off=phase->longcon(BytesPerLong);
  mem = new (phase->C, 4) StoreLNode(in(0),mem,adr,atp,zero);
  count--;
  while( count-- ) {
    mem = phase->transform(mem);
    adr = phase->transform(new (phase->C, 4) AddPNode(base,adr,off));
    mem = new (phase->C, 4) StoreLNode(in(0),mem,adr,atp,zero);
  }
  return mem;
}

//=============================================================================
//-------------------------------adr_type--------------------------------------
// Do we Match on this edge index or not?  Do not match memory
const TypePtr*ArrayCopySrcNode::adr_type()const{
  return in(3)->bottom_type()->is_ptr()->add_offset(Type::OffsetBot);
}
uint ArrayCopySrcNode::size_of()const{return sizeof(*this);}
uint ArrayCopySrcNode::cmp(const Node&n)const{
  return (&n == this);          // Always fail except on self
}


const TypePtr*ArrayCopyNode::adr_type()const{
  const TypePtr *ta = in(4)->bottom_type()->is_ptr()->add_offset(Type::OffsetBot);
#ifdef ASSERT
Node*x=in(1);//In the middle of split-if, we might interpose
  if( x->is_Phi() ) x = x->in(1); // a Phi between AC & ACS.
  assert( Compile::current()->get_alias_index(x->adr_type()) == 
          Compile::current()->get_alias_index(ta), "src & dst types do not match" );
#endif
  return ta;
}

uint ArrayCopyNode::cmp(const Node&n)const{
  return (&n == this);          // Always fail except on self
}
uint ArrayCopyNode::size_of()const{return sizeof(*this);}

ArrayCopyNode::ArrayCopyNode( Node *ctrl, Node *ACsrc, Node *dstmem, Node *dstlenmem, Node *dst, Node *dst_off, Node *elem_cnt) : Node(7) {
set_req(0,ctrl);
set_req(1,ACsrc);
set_req(2,dstmem);
set_req(3,dstlenmem);
set_req(4,dst);
set_req(5,dst_off);
set_req(6,elem_cnt);
}

//------------------------------match_edge-------------------------------------
// Do we Match on this edge index or not?  Do not match memory
uint ArrayCopyNode::match_edge(uint idx)const{
ShouldNotReachHere();//Not matched
  return 0;
}

//------------------------------Identity---------------------------------------
// Clearing a zero length array does nothing
Node*ArrayCopyNode::Identity(PhaseTransform*phase){
  // Zero-length copy
if(phase->type(in(6))->higher_equal(TypeInt::ZERO)){
    if( PrintOpto )
C2OUT->print_cr("arraycopy has fixed length of 0 at compile time");
return in(2);
  }

  // Useless self-copy
  assert0( in(1)->Opcode() == Op_ArrayCopySrc );
  ArrayCopySrcNode *ac = (ArrayCopySrcNode*)in(1);
Node*src_mem=ac->in(1);
Node*src_base=ac->in(3);
Node*src_idx=ac->in(4);
Node*dst_mem=in(2);
Node*dst_base=in(4);
Node*dst_idx=in(5);
  if( src_mem == dst_mem && src_base == dst_base && src_idx == dst_idx ) {
    if( PrintOpto )
C2OUT->print_cr("useless self arraycopy");
    return dst_mem;
  }

  return this;
}

//------------------------------Idealize---------------------------------------
// Clearing a short array is faster with stores
Node*ArrayCopyNode::Ideal(PhaseGVN*phase,bool can_reshape){
  if( remove_dead_region(phase, can_reshape) ) return this;
  Node *dst_ac  = in(1);  if( phase->type(dst_ac  ) == Type::TOP ) return NULL;
  Node *dst_mem = in(2);  if( phase->type(dst_mem ) == Type::TOP ) return NULL;
  Node *dst_base= in(4);  if( phase->type(dst_base) == Type::TOP ) return NULL;
  Node *dst_idx = in(5);  if( phase->type(dst_idx ) == Type::TOP ) return NULL;
  Node *dst_len = in(6);  if( phase->type(dst_len ) == Type::TOP ) return NULL;

const Type*t=phase->type(dst_len);
  if( !t->singleton() ) return NULL; // Variable sized input
  const long len = t->is_long()->get_con();
  if( PrintOpto )
    C2OUT->print_cr("arraycopy has fixed length of %ld at compile time",len);
  if( len > 10 || len < 0 ) return NULL; // Size too large
  if( len == 0 ) return NULL;   // Let Identity get it

  // Range-checks and compatibility checks have already been passed.
  // Unroll the ArrayCopy guts into loads & stores now.
  assert0( in(1)->Opcode() == Op_ArrayCopySrc );
  ArrayCopySrcNode *ac = (ArrayCopySrcNode*)in(1);
  
  // Do not do this for oop-arrays, as that requires CardMarks.
  // (either that, or actually insert the cardmarks).
  if( ac->_scale == 0 ) return NULL;

  // Compute the source address
  Node *src_mem = ac->in(1);  if( phase->type(src_mem ) == Type::TOP ) return NULL;
  Node *src_base= ac->in(3);  if( phase->type(src_base) == Type::TOP ) return NULL;
  Node *src_idx = ac->in(4);  if( phase->type(src_idx ) == Type::TOP ) return NULL;
  if( src_mem == dst_mem && src_base == dst_base && src_idx == dst_idx ) 
return NULL;//Let Identity get it
  src_idx = phase->transform(new (phase->C, 2) ConvI2LNode(src_idx));
  Node *src_off = phase->transform(new (phase->C, 3) MulLNode( src_idx, phase->longcon(ac->_scale ? ac->_scale : wordSize) ));
  Node *src_adr = phase->transform(new (phase->C, 4) AddPNode(src_base, src_base, src_off));
const TypePtr*src_adr_type=ac->adr_type();

  // Compute the destination address
  dst_idx = phase->transform(new (phase->C, 2) ConvI2LNode(dst_idx));
Node*dst_off=phase->transform(new(phase->C,3)MulLNode(dst_idx,phase->longcon(ac->_scale)));
  Node *dst_adr = phase->transform(new (phase->C, 4) AddPNode(dst_base, dst_base, dst_off));
  // The useless self-copy.  Let identity get it.
  if( src_mem == dst_mem && src_base == dst_base && src_idx == dst_idx ) return NULL;

const TypePtr*dst_adr_type=adr_type();
  int aoff = -99;
  switch( ac->_scale ) {
  case 0: aoff = arrayOopDesc::base_offset_in_bytes(T_OBJECT); break;
  case 1: aoff = arrayOopDesc::base_offset_in_bytes(T_BYTE  ); break;
  case 2: aoff = arrayOopDesc::base_offset_in_bytes(T_CHAR  ); break;
  case 4: aoff = arrayOopDesc::base_offset_in_bytes(T_INT   ); break;
  case 8: aoff = arrayOopDesc::base_offset_in_bytes(T_LONG  ); break;
  default: ShouldNotReachHere();
  }

  // Now load-all, store-all in parallel.  Load-all before store-all handles
  // the case of overlapped self-copy but puts a serious upper bound on how
  // large an ArrayCopy we're willing to completely inline.
Node*st=NULL;
  for( long i=0; i<len; i++ ) {
    Node *off = phase->longcon(i*ac->_scale+aoff);

    Node *src = phase->transform(new (phase->C, 4) AddPNode( src_base, src_adr, off ));
Node*ld=NULL;
    switch( ac->_scale ) {
    case 0: ld = new (phase->C, 3) LoadPNode(0,src_mem,src,src_adr_type,TypeInstPtr::OBJECT); break;
    case 1: ld = new (phase->C, 3) LoadBNode(0,src_mem,src,src_adr_type,TypeInt   ::BYTE  ); break;
    case 2: ld = new (phase->C, 3) LoadCNode(0,src_mem,src,src_adr_type,TypeInt   ::CHAR  ); break;
    case 4: ld = new (phase->C, 3) LoadINode(0,src_mem,src,src_adr_type,TypeInt   ::INT   ); break;
    case 8: ld = new (phase->C, 3) LoadLNode(0,src_mem,src,src_adr_type,TypeLong  ::LONG  ); break;
    default: ShouldNotReachHere();
    }
ld=phase->transform(ld);
    if( i==0 ) st = in(2);      // Load first-time from input dst mem

    Node *dst = phase->transform(new (phase->C, 4) AddPNode( dst_base, dst_adr, off ));
    switch( ac->_scale ) {
    case 0: st = new (phase->C, 4) StorePNode(in(0),st,dst,dst_adr_type,ld); break;
    case 1: st = new (phase->C, 4) StoreBNode(in(0),st,dst,dst_adr_type,ld); break;
    case 2: st = new (phase->C, 4) StoreCNode(in(0),st,dst,dst_adr_type,ld); break;
    case 4: st = new (phase->C, 4) StoreINode(in(0),st,dst,dst_adr_type,ld); break;
    case 8: st = new (phase->C, 4) StoreLNode(in(0),st,dst,dst_adr_type,ld); break;
    default: ShouldNotReachHere();
    }
    st = phase->transform(st);  // Stores are serialized
  }

  return st;
}

//=============================================================================
// Do we match on this edge? No memory edges
uint StrCompNode::match_edge(uint idx) const {
return idx==3||idx==4;
}

//------------------------------Ideal------------------------------------------
// Return a node which is more "ideal" than the current node.  Strip out 
// control copies
Node *StrCompNode::Ideal(PhaseGVN *phase, bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//------------------------------Value------------------------------------------
const Type*StrCompNode::Value(PhaseTransform*phase)const{
  // Any input is TOP ==> the result is TOP
for(uint i=1;i<req();i++)
if(phase->type(in(i))==Type::TOP)
      return Type::TOP;
  return bottom_type();
}


//=============================================================================
// Do we match on this edge? No memory edges
uint StrEqualsNode::match_edge(uint idx)const{
return idx==3||idx==4;
}

//------------------------------Ideal------------------------------------------
// Return a node which is more "ideal" than the current node.  Strip out 
// control copies
Node*StrEqualsNode::Ideal(PhaseGVN*phase,bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

const Type*StrEqualsNode::Value(PhaseTransform*phase)const{
  // Any input is TOP ==> the result is TOP
for(uint i=1;i<req();i++)
if(phase->type(in(i))==Type::TOP)
      return Type::TOP;
  return bottom_type();
}


//=============================================================================
// Do we match on this edge? No memory edges
uint StrIndexOfNode::match_edge(uint idx)const{
  return idx == 5 || idx == 6;
}

//------------------------------Ideal------------------------------------------
// Return a node which is more "ideal" than the current node.  Strip out
// control copies
Node*StrIndexOfNode::Ideal(PhaseGVN*phase,bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//------------------------------Ideal------------------------------------------
// Return a node which is more "ideal" than the current node.  Strip out
// control copies
Node*AryEqNode::Ideal(PhaseGVN*phase,bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}

//=============================================================================
MemBarNode::MemBarNode(Compile* C, int alias_idx, Node* precedent)
  : MultiNode(TypeFunc::Parms + (precedent == NULL? 0: 1)),
    _adr_type(C->get_adr_type(alias_idx))
{
  init_class_id(Class_MemBar);
  Node* top = C->top();
  init_req(TypeFunc::I_O,top);
  init_req(TypeFunc::FramePtr,top);
  init_req(TypeFunc::ReturnAdr,top);
  if (precedent != NULL)
    init_req(TypeFunc::Parms, precedent);
}

//------------------------------cmp--------------------------------------------
uint MemBarNode::hash() const { return NO_HASH; }
uint MemBarNode::cmp( const Node &n ) const { 
  return (&n == this);          // Always fail except on self
}

//------------------------------make-------------------------------------------
MemBarNode* MemBarNode::make(Compile* C, int opcode, int atp, Node* pn) {
  int len = Precedent + (pn == NULL? 0: 1);
  switch (opcode) {
  case Op_MemBarAcquire:   return new(C, len) MemBarAcquireNode(C,  atp, pn);
  case Op_MemBarRelease:   return new(C, len) MemBarReleaseNode(C,  atp, pn);
  case Op_MemBarVolatile:  return new(C, len) MemBarVolatileNode(C, atp, pn);
  case Op_MemBarCPUOrder:  return new(C, len) MemBarCPUOrderNode(C, atp, pn);
  // Azul does not need the Initialize node or step
  //case Op_Initialize:      return new(C, len) InitializeNode(C,     atp, pn);
  default:                 ShouldNotReachHere(); return NULL;
  }
}

//------------------------------Ideal------------------------------------------
// Return a node which is more "ideal" than the current node.  Strip out 
// control copies
Node *MemBarNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  if (remove_dead_region(phase, can_reshape))  return this;
  return NULL;
}

//------------------------------Value------------------------------------------
const Type *MemBarNode::Value( PhaseTransform *phase ) const {
  if( !in(0) ) return Type::TOP;
  if( phase->type(in(0)) == Type::TOP )
    return Type::TOP;
  return bottom_type();
}

//------------------------------match------------------------------------------
// Construct projections for memory.
Node *MemBarNode::match( const ProjNode *proj, const Matcher *m ) {
  switch (proj->_con) {
  case TypeFunc::Control:
  case TypeFunc::Memory:
    return new (m->C, 1) MachProjNode(this,proj->_con,RegMask::Empty,MachProjNode::unmatched_proj);
  }
  ShouldNotReachHere();
  return NULL;
}

//=============================================================================
// 
// SEMANTICS OF MEMORY MERGES:  A MergeMem is a memory state assembled from several
// contributing store or call operations.  Each contributor provides the memory
// state for a particular "alias type" (see Compile::alias_type).  For example,
// if a MergeMem has an input X for alias category #6, then any memory reference
// to alias category #6 may use X as its memory state input, as an exact equivalent
// to using the MergeMem as a whole.
//   Load<6>( MergeMem(<6>: X, ...), p ) <==> Load<6>(X,p)
// 
// (Here, the <N> notation gives the index of the relevant adr_type.)
// 
// In one special case (and more cases in the future), alias categories overlap.
// The special alias category "Bot" (Compile::AliasIdxBot) includes all memory
// states.  Therefore, if a MergeMem has only one contributing input W for Bot,
// it is exactly equivalent to that state W:
//   MergeMem(<Bot>: W) <==> W
// 
// Usually, the merge has more than one input.  In that case, where inputs
// overlap (i.e., one is Bot), the narrower alias type determines the memory
// state for that type, and the wider alias type (Bot) fills in everywhere else:
//   Load<5>( MergeMem(<Bot>: W, <6>: X), p ) <==> Load<5>(W,p)
//   Load<6>( MergeMem(<Bot>: W, <6>: X), p ) <==> Load<6>(X,p)
// 
// A merge can take a "wide" memory state as one of its narrow inputs.
// This simply means that the merge observes out only the relevant parts of
// the wide input.  That is, wide memory states arriving at narrow merge inputs
// are implicitly "filtered" or "sliced" as necessary.  (This is rare.)
// 
// These rules imply that MergeMem nodes may cascade (via their <Bot> links),
// and that memory slices "leak through":
//   MergeMem(<Bot>: MergeMem(<Bot>: W, <7>: Y)) <==> MergeMem(<Bot>: W, <7>: Y)
// 
// But, in such a cascade, repeated memory slices can "block the leak":
//   MergeMem(<Bot>: MergeMem(<Bot>: W, <7>: Y), <7>: Y') <==> MergeMem(<Bot>: W, <7>: Y')
// 
// In the last example, Y is not part of the combined memory state of the
// outermost MergeMem.  The system must, of course, prevent unschedulable
// memory states from arising, so you can be sure that the state Y is somehow
// a precursor to state Y'.
// 
// 
// REPRESENTATION OF MEMORY MERGES: The indexes used to address the Node::in array
// of each MergeMemNode array are exactly the numerical alias indexes, including
// but not limited to AliasIdxTop, AliasIdxBot, and AliasIdxRaw.  The functions
// Compile::alias_type (and kin) produce and manage these indexes.
// 
// By convention, the value of in(AliasIdxTop) (i.e., in(1)) is always the top node.
// (Note that this provides quick access to the top node inside MergeMem methods,
// without the need to reach out via TLS to Compile::current.)
// 
// As a consequence of what was just described, a MergeMem that represents a full
// memory state has an edge in(AliasIdxBot) which is a "wide" memory state,
// containing all alias categories.
// 
// MergeMem nodes never (?) have control inputs, so in(0) is NULL.
// 
// All other edges in(N) (including in(AliasIdxRaw), which is in(3)) are either
// a memory state for the alias type <N>, or else the top node, meaning that
// there is no particular input for that alias type.  Note that the length of
// a MergeMem is variable, and may be extended at any time to accommodate new
// memory states at larger alias indexes.  When merges grow, they are of course
// filled with "top" in the unused in() positions.
//
// This use of top is named "empty_memory()", or "empty_mem" (no-memory) as a variable.
// (Top was chosen because it works smoothly with passes like GCM.)
// 
// For convenience, we hardwire the alias index for TypeRawPtr::BOTTOM.  (It is
// the type of random VM bits like TLS references.)  Since it is always the
// first non-Bot memory slice, some low-level loops use it to initialize an
// index variable:  for (i = AliasIdxRaw; i < req(); i++).
//
// 
// ACCESSORS:  There is a special accessor MergeMemNode::base_memory which returns
// the distinguished "wide" state.  The accessor MergeMemNode::memory_at(N) returns
// the memory state for alias type <N>, or (if there is no particular slice at <N>,
// it returns the base memory.  To prevent bugs, memory_at does not accept <Top>
// or <Bot> indexes.  The iterator MergeMemStream provides robust iteration over
// MergeMem nodes or pairs of such nodes, ensuring that the non-top edges are visited.
// 
// %%%% We may get rid of base_memory as a separate accessor at some point; it isn't
// really that different from the other memory inputs.  An abbreviation called
// "bot_memory()" for "memory_at(AliasIdxBot)" would keep code tidy.
// 
// 
// PARTIAL MEMORY STATES:  During optimization, MergeMem nodes may arise that represent
// partial memory states.  When a Phi splits through a MergeMem, the copy of the Phi
// that "emerges though" the base memory will be marked as excluding the alias types
// of the other (narrow-memory) copies which "emerged through" the narrow edges:
// 
//   Phi<Bot>(U, MergeMem(<Bot>: W, <8>: Y))
//     ==Ideal=>  MergeMem(<Bot>: Phi<Bot-8>(U, W), Phi<8>(U, Y))
// 
// This strange "subtraction" effect is necessary to ensure IGVN convergence.
// (It is currently unimplemented.)  As you can see, the resulting merge is
// actually a disjoint union of memory states, rather than an overlay.
// 

//------------------------------MergeMemNode-----------------------------------
Node* MergeMemNode::make_empty_memory() {
  Node* empty_memory = (Node*) Compile::current()->top();
  assert(empty_memory->is_top(), "correct sentinel identity");
  return empty_memory;
}

MergeMemNode::MergeMemNode(Node *new_base) : Node(1+Compile::AliasIdxRaw) {
  init_class_id(Class_MergeMem);
  // all inputs are nullified in Node::Node(int) 
  // set_input(0, NULL);  // no control input

  // Initialize the edges uniformly to top, for starters.
  Node* empty_mem = make_empty_memory();
  for (uint i = Compile::AliasIdxTop; i < req(); i++) {
    init_req(i,empty_mem);
  }
  assert(empty_memory() == empty_mem, "");

  if( new_base != NULL && new_base->is_MergeMem() ) {
    MergeMemNode* mdef = new_base->as_MergeMem();
    assert(mdef->empty_memory() == empty_mem, "consistent sentinels");
    for (MergeMemStream mms(this, mdef); mms.next_non_empty2(); ) {
      mms.set_memory(mms.memory2());
    }
    assert(base_memory() == mdef->base_memory(), "");
  } else {
    set_base_memory(new_base);
  }
}

// Make a new, untransformed MergeMem with the same base as 'mem'.
// If mem is itself a MergeMem, populate the result with the same edges.
MergeMemNode* MergeMemNode::make(Compile* C, Node* mem) {
  return new(C, 1+Compile::AliasIdxRaw) MergeMemNode(mem);
}

//------------------------------cmp--------------------------------------------
uint MergeMemNode::hash() const { return NO_HASH; }
uint MergeMemNode::cmp( const Node &n ) const { 
  return (&n == this);          // Always fail except on self
}

//------------------------------Identity---------------------------------------
Node* MergeMemNode::Identity(PhaseTransform *phase) {
  // Identity if this merge point does not record any interesting memory 
  // disambiguations.
  Node* base_mem = base_memory();
  Node* empty_mem = empty_memory();
  if (base_mem != empty_mem) {  // Memory path is not dead?
    for (uint i = Compile::AliasIdxRaw; i < req(); i++) {
      Node* mem = in(i);
      if (mem != empty_mem && mem != base_mem) {
        return this;            // Many memory splits; no change
      }
    }
  }
  return base_mem;              // No memory splits; ID on the one true input
}

//------------------------------Value-----------------------------------------
const Type*MergeMemNode::Value(PhaseTransform*phase)const{
  return phase->type( base_memory() ); // TOP if input is TOP, MEMORY otherwise
}

//------------------------------Ideal------------------------------------------
// This method is invoked recursively on chains of MergeMem nodes
Node *MergeMemNode::Ideal(PhaseGVN *phase, bool can_reshape) {
  // Remove chain'd MergeMems
  //
  // This is delicate, because the each "in(i)" (i >= Raw) is interpreted
  // relative to the "in(Bot)".  Since we are patching both at the same time,
  // we have to be careful to read each "in(i)" relative to the old "in(Bot)",
  // but rewrite each "in(i)" relative to the new "in(Bot)".
  Node *progress = NULL;
  PhaseIterGVN *igvn = phase->is_IterGVN();

  Node* old_base = base_memory();
  Node* empty_mem = empty_memory();
  if (old_base == empty_mem)
    return NULL; // Dead memory path.

  MergeMemNode* old_mbase;
  if (old_base != NULL && old_base->is_MergeMem())
    old_mbase = old_base->as_MergeMem();
  else
    old_mbase = NULL;
  Node* new_base = old_base;

  // simplify stacked MergeMems in base memory
  if (old_mbase)  new_base = old_mbase->base_memory();

  // the base memory might contribute new slices beyond my req()
  if (old_mbase)  grow_to_match(old_mbase);

  // Look carefully at the base node if it is a phi.
  PhiNode* phi_base;
  if (new_base != NULL && new_base->is_Phi())
    phi_base = new_base->as_Phi();
  else
    phi_base = NULL;

  Node*    phi_reg = NULL;
  uint     phi_len = (uint)-1;
  if (phi_base != NULL && !phi_base->is_copy()) {
    // do not examine phi if degraded to a copy
    phi_reg = phi_base->region();
    phi_len = phi_base->req();
    // see if the phi is unfinished
    for (uint i = 1; i < phi_len; i++) {
      if (phi_base->in(i) == NULL) {
        // incomplete phi; do not look at it yet!
        phi_reg = NULL;
        phi_len = (uint)-1;
        break;
      }
    }
  }

  // Note:  We do not call verify_sparse on entry, because inputs
  // can normalize to the base_memory via subsume_node or similar
  // mechanisms.  This method repairs that damage.

  assert(!old_mbase || old_mbase->is_empty_memory(empty_mem), "consistent sentinels");
  
  // Look at each slice.
  for (uint i = Compile::AliasIdxRaw; i < req(); i++) {
    Node* old_in = in(i);
    // calculate the old memory value
    Node* old_mem = old_in;
    if (old_mem == empty_mem)  old_mem = old_base;
    assert(old_mem == memory_at(i), "");

    // maybe update (reslice) the old memory value

    // simplify stacked MergeMems
    Node* new_mem = old_mem;
    MergeMemNode* old_mmem;
    if (old_mem != NULL && old_mem->is_MergeMem())
      old_mmem = old_mem->as_MergeMem();
    else
      old_mmem = NULL;
    if (old_mmem == this) {
      // This can happen if loops break up and safepoints disappear.
      // A merge of BotPtr (default) with a RawPtr memory derived from a
      // safepoint can be rewritten to a merge of the same BotPtr with
      // the BotPtr phi coming into the loop.  If that phi disappears
      // also, we can end up with a self-loop of the mergemem.
      // In general, if loops degenerate and memory effects disappear,
      // a mergemem can be left looking at itself.  This simply means
      // that the mergemem's default should be used, since there is
      // no longer any apparent effect on this slice.
      // Note: If a memory slice is a MergeMem cycle, it is unreachable
      //       from start.  Update the input to TOP.
      new_mem = (new_base == this || new_base == empty_mem)? empty_mem : new_base;
    }
    else if (old_mmem != NULL) {
      new_mem = old_mmem->memory_at(i);
    }
    // else preceeding memory was not a MergeMem

    // replace equivalent phis (unfortunately, they do not GVN together)
    if (new_mem != NULL && new_mem != new_base &&
        new_mem->req() == phi_len && new_mem->in(0) == phi_reg) {
      if (new_mem->is_Phi()) {
        PhiNode* phi_mem = new_mem->as_Phi();
        for (uint i = 1; i < phi_len; i++) {
          if (phi_base->in(i) != phi_mem->in(i)) {
            phi_mem = NULL;
            break;
          }
        }
        if (phi_mem != NULL) {
          // equivalent phi nodes; revert to the def
          new_mem = new_base;
        }
      }
    }

    // maybe store down a new value
    Node* new_in = new_mem;
    if (new_in == new_base)  new_in = empty_mem;

    if (new_in != old_in) {
      // Warning:  Do not combine this "if" with the previous "if"
      // A memory slice might have be be rewritten even if it is semantically
      // unchanged, if the base_memory value has changed.
      if( igvn && old_in != this ) set_req_X(i, new_in,igvn);
      else                         set_req  (i, new_in     );
      progress = this;          // Report progress
    }
  }

  if (new_base != old_base) {
    if( igvn && old_base != this ) set_req_X(Compile::AliasIdxBot, new_base,igvn);
    else                           set_req  (Compile::AliasIdxBot, new_base     );
    // Don't use set_base_memory(new_base), because we need to update du.
    assert(base_memory() == new_base, "");
    progress = this;
  }

  if( base_memory() == this ) {
    // a self cycle indicates this memory path is dead
    set_req(Compile::AliasIdxBot, empty_mem);
  }
  // Look for simple cycles of base memory, and allow them to break out of loops
  if( igvn && phi_base && phi_base->req()==3 && phi_base->region()->is_Loop() ) {
Node*mm=phi_base->in(LoopNode::EntryControl);
    if( phi_base->in(LoopNode::LoopBackControl) == this &&
        // Do not roll into another dead cycle
        !(mm->is_MergeMem() && mm->as_MergeMem()->base_memory() == phi_base) &&
        mm != phi_base ) {
set_base_memory(mm);
      progress = this;
    }
  }

  // Resolve external cycles by calling Ideal on a MergeMem base_memory
  // Recursion must occur after the self cycle check above
  if( base_memory()->is_MergeMem() ) {
    MergeMemNode *new_mbase = base_memory()->as_MergeMem();
    Node *m = phase->transform(new_mbase);  // Rollup any cycles
    if( m != NULL && (m->is_top() || 
(m->is_MergeMem()&&m->as_MergeMem()->base_memory()==empty_mem))){
      // propagate rollup of dead cycle to self
      set_req(Compile::AliasIdxBot, empty_mem);
    }
  }

  if( base_memory() == empty_mem ) {
    progress = this;
    // Cut inputs during Parse phase only.
    // During Optimize phase a dead MergeMem node will be subsumed by Top.
    if( !can_reshape ) {
      for (uint i = Compile::AliasIdxRaw; i < req(); i++) {
        if( in(i) != empty_mem ) { set_req(i, empty_mem); }
      }
    }
  }

  if( !progress && base_memory()->is_Phi() && can_reshape ) {
    // Check if PhiNode::Ideal's "Split phis through memory merges"
    // transform should be attempted. Look for this->phi->this cycle.
    uint merge_width = req();
    if (merge_width > Compile::AliasIdxRaw) {
      PhiNode* phi = base_memory()->as_Phi();
      for( uint i = 1; i < phi->req(); ++i ) {// For all paths in
        if (phi->in(i) == this) {
          phase->is_IterGVN()->_worklist.push(phi);
          break;
        }
      }
    }
  }

  assert(verify_sparse(), "please, no dups of base");
  return progress;
}

//-------------------------set_base_memory-------------------------------------
void MergeMemNode::set_base_memory(Node *new_base) {
  Node* empty_mem = empty_memory();
  set_req(Compile::AliasIdxBot, new_base);
  assert(memory_at(req()) == new_base, "must set default memory");
  // Clear out other occurrences of new_base:
  if (new_base != empty_mem) {
    for (uint i = Compile::AliasIdxRaw; i < req(); i++) {
      if (in(i) == new_base)  set_req(i, empty_mem);
    }
  }
}

//------------------------------out_RegMask------------------------------------
const RegMask &MergeMemNode::out_RegMask() const {
  return RegMask::Empty;
}

//------------------------------dump_spec--------------------------------------
void MergeMemNode::dump_spec(outputStream *st) const {
  st->print(" {");
  Node* base_mem = base_memory();
  for( uint i = Compile::AliasIdxRaw; i < req(); i++ ) {
    Node* mem = memory_at(i);
    if (mem == base_mem) { st->print(" -"); continue; }
    st->print( " N%d:", mem->_idx );
    Compile::current()->get_adr_type(i)->dump_on(st);
  }
  st->print(" }");
}


#ifdef ASSERT
static bool might_be_same(Node* a, Node* b) {
  if (a == b)  return true;
  if (!(a->is_Phi() || b->is_Phi()))  return false;
  // phis shift around during optimization
  return true;  // pretty stupid...
}

// verify a narrow slice (either incoming or outgoing)
static void verify_memory_slice(const MergeMemNode* m, int alias_idx, Node* n) {
  if (!VerifyAliases)       return;  // don't bother to verify unless requested
  if (is_error_reported())  return;  // muzzle asserts when debugging an error
  if (Node::in_dump())      return;  // muzzle asserts when printing
  assert(alias_idx >= Compile::AliasIdxRaw, "must not disturb base_memory or sentinel");
  assert(n != NULL, "");
  // Elide intervening MergeMem's
  while (n->is_MergeMem()) {
    n = n->as_MergeMem()->memory_at(alias_idx);
  }
  Compile* C = Compile::current();
  const TypePtr* n_adr_type = n->adr_type();
  if (n == m->empty_memory()) {
    // Implicit copy of base_memory()
  } else if (n_adr_type != TypePtr::BOTTOM) {
    assert(n_adr_type != NULL, "new memory must have a well-defined adr_type");
    assert(C->must_alias(n_adr_type, alias_idx), "new memory must match selected slice");
  } else {
    // A few places like make_runtime_call "know" that VM calls are narrow,
    // and can be used to update only the VM bits stored as TypeRawPtr::BOTTOM.
    bool expected_wide_mem = false;
    if (n == m->base_memory()) {
      expected_wide_mem = true;
    } else if (alias_idx == Compile::AliasIdxRaw ||
               n == m->memory_at(Compile::AliasIdxRaw)) {
      expected_wide_mem = true;
    } else if (!C->alias_type(alias_idx)->is_rewritable()) {
      // memory can "leak through" calls on channels that
      // are write-once.  Allow this also.
      expected_wide_mem = true;
    }
    assert(expected_wide_mem, "expected narrow slice replacement");
  }
}
#else // !ASSERT
#define verify_memory_slice(m,i,n) (0)  // PRODUCT version is no-op
#endif


//-----------------------------memory_at---------------------------------------
Node* MergeMemNode::memory_at(uint alias_idx) const {
assert((alias_idx>=Compile::AliasIdxRaw)||
(alias_idx==Compile::AliasIdxBot&&Compile::current()->AliasLevel()==0),
         "must avoid base_memory and AliasIdxTop");

  // Otherwise, it is a narrow slice.
  Node* n = alias_idx < req() ? in(alias_idx) : empty_memory();
  Compile *C = Compile::current();
  if (is_empty_memory(n)) {
    // the array is sparse; empty slots are the "top" node
    n = base_memory();
    assert(Node::in_dump()
           || n == NULL || n->bottom_type() == Type::TOP
           || n->adr_type() == TypePtr::BOTTOM
           || n->adr_type() == TypeRawPtr::BOTTOM
           || Compile::current()->AliasLevel() == 0,
           "must be a wide memory");
    // AliasLevel == 0 if we are organizing the memory states manually.
    // See verify_memory_slice for comments on TypeRawPtr::BOTTOM.
  } else {
    // make sure the stored slice is sane
#ifdef ASSERT
    if (is_error_reported() || Node::in_dump()) {
    } else if (might_be_same(n, base_memory())) {
      // Give it a pass:  It is a mostly harmless repetition of the base.
      // This can arise normally from node subsumption during optimization.
    } else {
      verify_memory_slice(this, alias_idx, n);
    }
#endif
  }
  return n;
}

//---------------------------set_memory_at-------------------------------------
void MergeMemNode::set_memory_at(uint alias_idx, Node *n) {
  verify_memory_slice(this, alias_idx, n);
  Node* empty_mem = empty_memory();
  if (n == base_memory())  n = empty_mem;  // collapse default
  uint need_req = alias_idx+1;
  if (req() < need_req) {
    if (n == empty_mem)  return;  // already the default, so do not grow me
    // grow the sparse array
    do {
      add_req(empty_mem);
    } while (req() < need_req);
  }
  set_req( alias_idx, n );
}

//--------------------------iteration_setup------------------------------------
void MergeMemNode::iteration_setup(const MergeMemNode* other) {
  if (other != NULL) {
    grow_to_match(other);
    // invariant:  the finite support of mm2 is within mm->req()
#ifdef ASSERT
    for (uint i = req(); i < other->req(); i++) {
      assert(other->is_empty_memory(other->in(i)), "slice left uncovered");
    }
#endif
  }
  // Replace spurious copies of base_memory by top.
  Node* base_mem = base_memory();
  if (base_mem != NULL && !base_mem->is_top()) {
    for (uint i = Compile::AliasIdxBot+1, imax = req(); i < imax; i++) {
      if (in(i) == base_mem)
        set_req(i, empty_memory());
    }
  }
}

//---------------------------grow_to_match-------------------------------------
void MergeMemNode::grow_to_match(const MergeMemNode* other) {
  Node* empty_mem = empty_memory();
  assert(other->is_empty_memory(empty_mem), "consistent sentinels");
  // look for the finite support of the other memory
  for (uint i = other->req(); --i >= req(); ) {
    if (other->in(i) != empty_mem) {
      uint new_len = i+1;
      while (req() < new_len)  add_req(empty_mem);
      break;
    }
  }
}

//---------------------------verify_sparse-------------------------------------
#ifndef PRODUCT
bool MergeMemNode::verify_sparse() const {
  assert(is_empty_memory(make_empty_memory()), "sane sentinel");
  Node* base_mem = base_memory();
  // The following can happen in degenerate cases, since empty==top.
  if (is_empty_memory(base_mem))  return true;
  for (uint i = Compile::AliasIdxRaw; i < req(); i++) {
    assert(in(i) != NULL, "sane slice");
    if (in(i) == base_mem)  return false;  // should have been the sentinel value!
  }
  return true;
}

bool MergeMemStream::match_memory(Node* mem, const MergeMemNode* mm, int idx) {
  Node* n;
  n = mm->in(idx);
  if (mem == n)  return true;  // might be empty_memory()
  n = (idx == Compile::AliasIdxBot)? mm->base_memory(): mm->memory_at(idx);
  if (mem == n)  return true;
  while (n->is_Phi() && (n = n->as_Phi()->is_copy()) != NULL) {
    if (mem == n)  return true;
    if (n == NULL)  break;
  }
  return false;
}
#endif // !PRODUCT


//=============================================================================
//------------------------------EscapeMemory-----------------------------------
Node*EscapeMemoryNode::Ideal(PhaseGVN*phase,bool can_reshape){
  return remove_dead_region(phase, can_reshape) ? this : NULL;
}
