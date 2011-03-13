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


#include "block.hpp"
#include "cfgnode.hpp"
#include "commonAsm.hpp"
#include "c2compiler.hpp"
#include "machnode.hpp"
#include "memnode.hpp"
#include "regalloc.hpp"

#include "ad_pd.hpp"

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
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

//=============================================================================
// Return the value requested
// result register lookup, corresponding to int_format
Register MachOper::reg(PhaseRegAlloc*ra_,const Node*node)const{
return ra_->get_encode(node);
}
// input register lookup, corresponding to ext_format
Register MachOper::reg(PhaseRegAlloc*ra_,const Node*node,int idx)const{
return ra_->get_encode(node->in(idx));
}
intptr_t  MachOper::constant() const { return 0x00; }
bool MachOper::constant_is_oop() const { return false; }
jdouble MachOper::constantD() const { ShouldNotReachHere(); return 0.0; }
jfloat  MachOper::constantF() const { ShouldNotReachHere(); return 0.0; }
jlong   MachOper::constantL() const { ShouldNotReachHere(); return CONST64(0) ; }
TypeOopPtr *MachOper::oop() const { return NULL; }
int MachOper::ccode() const { return 0x00; }
// A zero, default, indicates this value is not needed.
// May need to lookup the base register, as done in int_ and ext_format
Register MachOper::base(PhaseRegAlloc*ra_,const Node*node,int idx)const{return noreg;}
Register MachOper::index(PhaseRegAlloc*ra_,const Node*node,int idx)const{return noreg;}
int MachOper::scale()  const { return 0x00; }
int MachOper::disp (PhaseRegAlloc *ra_, const Node *node, int idx)  const { return 0x00; }
int MachOper::constant_disp()  const { return 0; }
int MachOper::base_position()  const { return -1; }  // no base input
int MachOper::index_position() const { return -1; }  // no index input
// Check for PC-Relative displacement
bool MachOper::disp_is_oop() const { return false; }
// Return the label
Label*   MachOper::label()  const { ShouldNotReachHere(); return 0; }


//------------------------------negate-----------------------------------------
// Negate conditional branches.  Error for non-branch operands
void MachOper::negate() {
  ShouldNotCallThis();
}

//-----------------------------type--------------------------------------------
const Type *MachOper::type() const {
  return Type::BOTTOM;
}

//------------------------------in_RegMask-------------------------------------
const RegMask *MachOper::in_RegMask(int index) const { 
  ShouldNotReachHere(); 
  return NULL; 
}

//------------------------------dump_spec--------------------------------------
// Print any per-operand special info
void MachOper::dump_spec(outputStream *st) const { }

//------------------------------hash-------------------------------------------
// Print any per-operand special info
uint MachOper::hash() const {
  ShouldNotCallThis();
  return 5;
}

//------------------------------cmp--------------------------------------------
// Print any per-operand special info
uint MachOper::cmp( const MachOper &oper ) const {
  ShouldNotCallThis();
  return opcode() == oper.opcode();
}

//------------------------------hash-------------------------------------------
// Print any per-operand special info
uint labelOper::hash() const {
  return (intptr_t)_label;
}

//------------------------------cmp--------------------------------------------
// Print any per-operand special info
uint labelOper::cmp( const MachOper &oper ) const {
  return (opcode() == oper.opcode()) && (_label == oper.label());
}

//------------------------------hash-------------------------------------------
// Print any per-operand special info
uint methodOper::hash() const {
return 1;
}

//------------------------------cmp--------------------------------------------
// Print any per-operand special info
uint methodOper::cmp( const MachOper &oper ) const {
return(opcode()==oper.opcode());
}


//=============================================================================
//------------------------------MachNode---------------------------------------
        
//------------------------------emit-------------------------------------------
void MachNode::emit(PhaseRegAlloc*ra_)const{
C2OUT->print("missing MachNode emit function: ");
  dump();
  ShouldNotCallThis();
}

//------------------------------hash-------------------------------------------
uint MachNode::hash() const {
  uint no = num_opnds();
  uint sum = rule();
  for( uint i=0; i<no; i++ )
    sum += _opnds[i]->hash();
  return sum+Node::hash();
}

//-----------------------------cmp---------------------------------------------
uint MachNode::cmp( const Node &node ) const {
  MachNode& n = *((Node&)node).as_Mach();
  uint no = num_opnds();
  if( no != n.num_opnds() ) return 0;
  if( rule() != n.rule() ) return 0;
  for( uint i=0; i<no; i++ )    // All operands must match
    if( !_opnds[i]->cmp( *n._opnds[i] ) )
      return 0;                 // mis-matched operands
  return 1;                     // match
}

// Return an equivalent instruction using memory for cisc_operand position
MachNode *MachNode::cisc_version(int offset, Compile* C) {
  ShouldNotCallThis();
  return NULL;
}

void MachNode::use_cisc_RegMask() {
  ShouldNotReachHere();
}


//-----------------------------in_RegMask--------------------------------------
const RegMask &MachNode::in_RegMask( uint idx ) const {
  uint numopnds = num_opnds();        // Virtual call for number of operands
  uint skipped   = oper_input_base(); // Sum of leaves skipped so far
  if( idx < skipped ) {
    assert( ideal_Opcode() == Op_AddP, "expected base ptr here" );
    assert( idx == 1, "expected base ptr here" );
    // debug info can be anywhere
    return *Compile::current()->matcher()->idealreg2spillmask[Op_RegP];
  }
  uint opcnt     = 1;                 // First operand
  uint num_edges = _opnds[1]->num_edges(); // leaves for first operand 
  while( idx >= skipped+num_edges ) {
    skipped += num_edges;
    opcnt++;                          // Bump operand count
    assert( opcnt < numopnds, "Accessing non-existent operand" );
    num_edges = _opnds[opcnt]->num_edges(); // leaves for next operand
  }

  const RegMask *rm = cisc_RegMask();
  if( rm == NULL || (int)opcnt != cisc_operand() ) {
    rm = _opnds[opcnt]->in_RegMask(idx-skipped);
  }
  return *rm;
}

//-----------------------------memory_inputs--------------------------------
const MachOper*  MachNode::memory_inputs(Node* &base, Node* &index) const {
  const MachOper* oper = memory_operand();

  if (oper == (MachOper*)-1) {
    base = NodeSentinel;
    index = NodeSentinel;
  } else {
    base = NULL;
    index = NULL;
    if (oper != NULL) {
      // It has a unique memory operand.  Find its index.
      int oper_idx = num_opnds();
      while (--oper_idx >= 0) {
        if (_opnds[oper_idx] == oper)  break;
      }
      int oper_pos = operand_index(oper_idx);
      int base_pos = oper->base_position();
      if (base_pos >= 0) {
        base = _in[oper_pos+base_pos];
      }
      int index_pos = oper->index_position();
      if (index_pos >= 0) {
        index = _in[oper_pos+index_pos];
      }
    }
  }

  return oper;
}

//-----------------------------get_base_and_disp----------------------------
const Node* MachNode::get_base_and_disp(intptr_t &offset, const TypePtr* &adr_type) const {

  // Find the memory inputs using our helper function
  Node* base;
  Node* index;
  const MachOper* oper = memory_inputs(base, index);

  if (oper == NULL) {
    // Base has been set to NULL
    offset = 0;
  } else if (oper == (MachOper*)-1) {
    // Base has been set to NodeSentinel
    // There is not a unique memory use here.  We will fall to AliasIdxBot.
    offset = Type::OffsetBot;
  } else {
    // Base may be NULL, even if offset turns out to be != 0

    intptr_t disp = oper->constant_disp();
    int scale = oper->scale();
    // Now we have collected every part of the ADLC MEMORY_INTER.
    // See if it adds up to a base + offset.
    if (index != NULL) {
      if (!index->is_Con()) {
        disp = Type::OffsetBot;
      } else if (disp != Type::OffsetBot) {
        const TypeLong* tl = index->bottom_type()->isa_long();        
if(tl==NULL){
Untested("kinda thought I would need TypeLong here instead");
          disp = Type::OffsetBot;  // a random constant??
        } else {
disp+=tl->get_con()<<scale;
        }
      }
    }
    offset = disp;

    // In i486.ad, indOffset32X uses base==RegI and disp==RegP,
    // this will prevent alias analysis without the following support:
    // Lookup the TypePtr used by indOffset32X, a compile-time constant oop,
    // Add the offset determined by the "base", or use Type::OffsetBot.
    if( adr_type == TYPE_PTR_SENTINAL ) {
      const TypePtr *t_disp = oper->disp_as_type();  // only !NULL for indOffset32X
      if (t_disp != NULL) {
        offset = Type::OffsetBot;
        const Type* t_base = base->bottom_type();
        if (t_base->isa_intptr_t()) {
const TypeLong*t_offset=t_base->is_intptr_t();
Untested("kinda thought I would need TypeLong here instead");
          if( t_offset->is_con() ) {
            offset = t_offset->get_con();
          }
        }
        adr_type = t_disp->add_offset(offset);
      }
    }

  }
  return base;
}


//---------------------------------adr_type---------------------------------
const class TypePtr *MachNode::adr_type() const {
  intptr_t offset = 0;
  const TypePtr *adr_type = TYPE_PTR_SENTINAL;  // attempt computing adr_type
  const Node *base = get_base_and_disp(offset, adr_type);
  if( adr_type != TYPE_PTR_SENTINAL ) {
    return adr_type;      // get_base_and_disp has the answer
  }

  // Direct addressing modes have no base node, simply an indirect
  // offset, which is always to raw memory.
  // %%%%% Someday we'd like to allow constant oop offsets which 
  // would let Intel load from static globals in 1 instruction.
  // Currently Intel requires 2 instructions and a register temp.
  if (base == NULL) {
    // NULL base, zero offset means no memory at all (a null pointer!)
    if (offset == 0) {
      return NULL;
    }
    // NULL base, any offset means any pointer whatever
    if (offset == Type::OffsetBot) {
      return TypePtr::BOTTOM;
    }
    // %%% make offset be intptr_t
    assert(!Universe::heap()->is_in_reserved((oop)offset), "must be a raw ptr");
    return TypeRawPtr::BOTTOM;
  }

  // base of -1 with no particular offset means all of memory
  if (base == NodeSentinel)  return TypePtr::BOTTOM;

  const Type* t = base->bottom_type();
if(t->isa_int()&&offset!=0&&offset!=Type::OffsetBot){
    // We cannot assert that the offset does not look oop-ish here.
    // Depending on the heap layout the cardmark base could land
    // inside some oopish region.  It definitely does for Win2K.
    // The sum of cardmark-base plus shift-by-9-oop lands outside
    // the oop-ish area but we can't assert for that statically.
    return TypeRawPtr::BOTTOM;
  }

  const TypePtr *tp = t->isa_ptr();

  // be conservative if we do not recognize the type
  if (tp == NULL) {
    return TypePtr::BOTTOM;
  }
assert(tp->base()!=Type::AnyPtr||
//Weaken this assert for Azul, to allow loads from the KlassTable.
         // The KlassTable is an array of OOPs but is not itself an oop.
         // Loading from it usually produces something that looks like a
         // derived pointer (an offset into the table) but since the table is
         // not an OOP there are no GC implications
         (base->is_Mach() && 
          base->as_Mach()->ideal_Opcode() == Op_KID2Klass),
"not a bare pointer");

  return tp->add_offset(offset);
}


//-----------------------------operand_index---------------------------------
int MachNode::operand_index( uint operand ) const {
  if( operand < 1 )  return -1;
  assert(operand < num_opnds(), "oob");
  if( _opnds[operand]->num_edges() == 0 )  return -1;

  uint skipped   = oper_input_base(); // Sum of leaves skipped so far
  for (uint opcnt = 1; opcnt < operand; opcnt++) {
    uint num_edges = _opnds[opcnt]->num_edges(); // leaves for operand
    skipped += num_edges;
  }
  return skipped;
}


//------------------------------negate-----------------------------------------
// Negate conditional branches.  Error for non-branch Nodes
void MachNode::negate() {
  ShouldNotCallThis();
}

//------------------------------peephole---------------------------------------
// Apply peephole rule(s) to this instruction
MachNode *MachNode::peephole( Block *block, int block_index, PhaseRegAlloc *ra_, int &deleted, Compile* C ) {
  return NULL;
}

//------------------------------label_set--------------------------------------
// Set the Label for a LabelOper, if an operand for this instruction
void MachNode::label_set(Label*label){
  ShouldNotCallThis();
}

//------------------------------rematerialize----------------------------------
bool MachNode::rematerialize() const {
  uint r = rule();              // Match rule
  if( r <  Matcher::_begin_rematerialize ||
      r >= Matcher::_end_rematerialize )
    return false;

  // For 2-address instructions, the input live range is also the output
  // live range.  Remateralizing does not make progress on the that live range.
  if( two_adr() )  return false;

  // Check for rematerializing float constants, or not
  if( !Matcher::rematerialize_float_constants ) {
    int op = ideal_Opcode();
    if( op == Op_ConF || op == Op_ConD )
      return false;
  }

  // Defining flags - can't spill these!  Must remateralize.
if(ideal_reg()==Op_RegFlags){
    // Unless if there is an anti-dependence edge then no way to re-schedule and so bail out.
    if ( Compile::current()->subsume_loads() && needs_anti_dependence_check() ) {
      for (DUIterator_Fast imax, i = fast_outs(imax); i < imax; i++) {
Node*user=fast_out(i);
        // Is there an edge from user to this node.
for(uint j=0;j<user->req();j++){
if(user->in(j)==this){
            // Retry with subsume_loads == false
            // If this is the first failure, the sentinel string will "stick"
            // to the Compile object, and the C2Compiler will see it and retry.
            Compile::current()->record_failure(C2Compiler::retry_no_subsuming_loads(), true);
            return false;
          }
        }
      }
    }
    return true;
  }

  // Stretching lots of inputs - don't do it.
  if( req() > 2 )
    return false;

  // Don't remateralize somebody with bound inputs - it stretches a
  // fixed register lifetime.
  uint idx = oper_input_base();
  if( req() > idx ) {
    const RegMask &rm = in_RegMask(idx);
if(rm.is_bound1())
      return false;
  }

// Cheap on when constant pointers are in oop table
//  // Constant expands to many instructions?
//  if( op == Op_ConP &&
//      num_opnds() > 1 &&
//      _opnds[1]->constant() != 0 &&
//      outcnt() > 2 )
//    return false;
  return true;
}

//------------------------------dump_spec--------------------------------------
// Print any per-operand special info
void MachNode::dump_spec(outputStream *st) const {
  uint cnt = num_opnds();
for(uint i=0;i<cnt;i++){
    _opnds[i]->dump_spec(st);
    st->print(" ");
  }
  const TypePtr *t = adr_type();
if(t&&Compile::current()->alias_type(t)->is_volatile())
    st->print(" Volatile!");
}

//=============================================================================
//-----------------------------in_RegMask--------------------------------------
const RegMask&MachTypeNode::in_RegMask(uint idx)const{
uint bd_idx;
  if( UseSBA && 
      (bd_idx=base_derived_idx()) != 0 && 
      idx >= bd_idx )
    return *Compile::current()->matcher()->idealreg2spillmask[Op_RegP];
  // Otherwise do the default thing.
  return MachNode::in_RegMask(idx);
}

void MachTypeNode::dump_spec(outputStream *st) const {
MachNode::dump_spec(st);
  _bottom_type->dump_on(st);
  if( _oop_map ) _oop_map->print(st);
}

//=============================================================================
void MachNullCheckNode::emit(PhaseRegAlloc*ra_)const{
  // only emits entries in the null-pointer exception handler table
}

const RegMask &MachNullCheckNode::in_RegMask( uint idx ) const {
  if( idx == 0 ) return RegMask::Empty;
  else return in(1)->as_Mach()->out_RegMask();
}

//=============================================================================
//------------------------------insert-----------------------------------------
void MachLVBNode::insert( Node *nptr, GrowableArray<uint> &node_latency ) {
  if( !nptr->is_Mach() ) return;
MachNode*ptr=nptr->as_Mach();
int iop=ptr->ideal_Opcode();
  if( iop != Op_LoadP && iop != Op_LoadKlass && iop != Op_ConP && iop != Op_KID2Klass ) 
    return;

  // Things loaded from the interpreter state do not need (cannot have) an LVB
  // - they got LVB'd going in to the interpreter state.
  if( iop != Op_ConP &&
      ptr->in(MemNode::Memory)->Opcode() == Op_MachProj &&
      ptr->in(MemNode::Memory)->in(0)->Opcode() == Op_StartOSR &&
      ptr->adr_type() == TypeRawPtr::BOTTOM &&
      ptr->req() == 3)
    return;

  // Skip non-oop constants like NULL ptr or Thread* or base of KlassTable
  if( iop == Op_ConP ) {
    if( !nptr->bottom_type()->isa_oopptr() ) return;
  }
  if ( iop == Op_LoadP ) {
    if ( !nptr->adr_type()->isa_oopptr() ) return;
  }

  // No LVBs needed when loading KIDs
  const TypeKlassPtr *tk = nptr->bottom_type()->isa_klassptr();
  if( tk && tk->_is_kid ) return;

  // Not all uses of a loaded pointer need an LVB.  Null checks & kid-extracts
  // do not, nor does debug info (instead we LVB during a de-opt).  We do need
  // to LVB if we want to load/store through the value, or do pointer
  // compares, or pass it out or return it or add to it.  Technically we don't
  // need to LVB before a Phi, but it means the Phi-users need an LVB - which
  // makes the other Phi inputs pay an LVB cost (which makes profitability
  // hard to determine).  Look at the "ptr" users and try to classify them.
  bool has_lvb_required_users = false;
for(DUIterator_Fast imax,i=ptr->fast_outs(imax);i<imax;i++){
Node*u=ptr->fast_out(i);//for each use...
    int uop = u->Opcode();
if(u->is_Mach()){
MachNode*mu=u->as_Mach();
      // NullChecks do not use the loaded value, they really imply a use of
      // the Load's base address
      if( mu->is_MachNullCheck() ) continue;
      if( mu->memory_operand() != 0 ) {
        has_lvb_required_users = true;
        break;
      } 
      uop = ((MachNode*)u)->ideal_Opcode();
    }
    switch( uop ) {
    case Op_AddP:
    case Op_ArrayCopy:
    case Op_ArrayCopySrc:
    case Op_CallLeaf: 
    case Op_CallLeafNoFP: 
    case Op_CastP2X:            // Unsafe ops need LVB first
    case Op_ClearArray:
    case Op_CompareAndSwapI:
    case Op_CompareAndSwapL:
    case Op_CompareAndSwapP: 
    case Op_FastLock:
    case Op_FastUnlock:
    case Op_KID2Klass:
    case Op_LoadB:
    case Op_LoadC:
    case Op_LoadD:
    case Op_LoadF:
    case Op_LoadI:
    case Op_LoadKlass:
    case Op_LoadL:
    case Op_LoadP:
    case Op_LoadRange:
    case Op_LoadS:
    case Op_MemBarAcquire:
    case Op_PartialSubtypeCheck:
    case Op_PrefetchRead: // Interesting case, LVB is probably optional (but required by other uses of ptr)
    case Op_PrefetchWrite:
    case Op_StoreB:
    case Op_StoreC:
    case Op_StoreD:
    case Op_StoreF:
    case Op_StoreI:
    case Op_StoreL:
    case Op_StoreP:
    case Op_StrComp:
    case Op_StrEquals:
      has_lvb_required_users = true;
      break;
    case Op_CastPP:      // No need for LVB yet...but users of these values
    case Op_CheckCastPP: // will need LVB's.  So give it up now and insert the
case Op_Phi://LVB.  If this is too common, I can also search
case Op_CMoveP://users of these ops to see if need LVBs as well.
      has_lvb_required_users = true;
      break;
    case Op_Return:
    case Op_Allocate:
    case Op_AllocateArray:
    case Op_CallRuntime:
    case Op_CallStaticJava:
    case Op_CallDynamicJava:
    case Op_Lock: 
    case Op_Unlock: 
    case Op_SafePoint: 
      // All GC points require things to be LVB'd beforehand, so GC does not
      // need to crawl stacks - faster safepoint pause times.
      has_lvb_required_users = true;
      break;

case Op_If:{//Select the other value being compared against.  NULL
                  // checks do not need the LVB, but other ptr checks do.
      MachIfNode *iff = (MachIfNode*)u;
      if( iff->req() == 2 ) {
        assert0( iff->num_opnds() == 5 );
        assert0( iff->_opnds[2]->num_edges() == 1 );
        assert0( iff->_opnds[3]->num_edges() == 0 );
        assert0( iff->_opnds[3]->constant() == 0 );
        break;                  // This is a ptr-vs-null check
      }
      has_lvb_required_users = true;
      break;
    }
    case Op_CmpP:               // The NMT bits need to be correct to do direct ptr-to-ptr compares
      has_lvb_required_users = true;
      break;
    case Op_GetKID:
      // If KIDs are in the ref itself, we just need to do an extract on the
      // pointer's meta-data.  If the KID is NOT in the ref, we end up doing a
      // load from the header word and that means we must LVB the load first.
      has_lvb_required_users = !KIDInRef;
      break;
    case Op_Conv2B:             // Like a null-ptr check
    case Op_CmpL:
    case Op_CmpI:
      break;
    default:
      NOT_PRODUCT(u->dump(2));
      NOT_PRODUCT(u->dump(-2));
ShouldNotReachHere();//Unexpected case; inspect carefully
    } // End of switch on users of ptr

    if( has_lvb_required_users ) break;
  } // End of for-all-users of ptr

  if( !has_lvb_required_users ) // No LVB needed
    return;

  // --- 
  // Need an LVB.  Insert it now and make all use it.  Since we are so late in
  // the game we have lots of work to do that is normally handled by the ADLC
  // and prior passes.
  Compile *C = Compile::current();
  PhaseCFG *cfg = C->cfg();
  assert0( ptr->num_opnds() >= 2 );
  MachOper *adr = ptr->_opnds[1]; // Replicate the operands
  MachLVBNode *lvb = new (C) MachLVBNode(ptr->_opnds[0],adr,
                                         (UseLVBs ? (new (C) rRegPOper()):NULL) );
  ptr->replace_by(lvb);
  lvb->add_req(0);              // Control
  lvb->add_req(ptr);            // Value being LVB'd

  uint nof_edges = adr->num_edges();
  const int baseAddIdx = 1;
  for( uint index = baseAddIdx; index <= nof_edges; index++ ) {
    if( iop == Op_KID2Klass ) {
      // Constant loads do not have a base address edge, as that will be
      // fixed KlassTable base address. Only operand is kid as 32-bit int.
      //
      // NOTE: Incase UseLVBs is off & RefPoinson is on, we do not have to
      // stretch liveness of kid, but its not an issue, and is left simple
      // in the spirt of keeping code simple and also becuase RefPoison is
      // not a product option.
      assert( adr->num_edges() == 1, "Inconsistent operand edges for kid2klassNode" );
      lvb->add_req(ptr->in(index));
    }
    else {
      lvb->add_req(ptr->in(baseAddIdx+index));
    }
  }

  MachNode* tmpNode = NULL;

  // Create a temp node use, to enable Register Allocator to reserve a tmp register
  // to be used by lvb (a.k.a read barrier) instruction stream generated on expand.
  if( UseLVBs ) {
    tmpNode = new (C) MachTempNode(new (C) rRegPOper());
tmpNode->set_req(0,C->root());
    lvb->add_req(tmpNode);
  }

Block*ptrblk=cfg->_bbs[ptr->_idx];

  // Check for loads & nullchecks
Node*ed=ptrblk->end();
  if( ed->is_Mach() && ed->as_Mach()->is_MachNullCheck() && ed->in(1) == lvb ) {
    // OOps, the nullcheck uses the load not the LVB - it's testing the load's
    // address not the load's value.  Also the effective block for the ptr's
    // result is the non-faulting side of the NullCheck.
    ed->set_req(1,ptr);
    // If end[1+0]==op_false then I want succ[0] else succ[1]
    int eidx = ptrblk->end_idx();
    ptrblk = ptrblk->_succs[(ptrblk->_nodes[eidx+1]->Opcode() == Op_IfFalse) ? 0 : 1];
  }

  Block *lvbblk = ptrblk;
  if( lvb->outcnt() == 1 ) {    // Stall the LVB a bit with a simple hack
    // For a single use, put the LVB by the use instead of the def.  i.e., put
    // the LVB in the use-blk instead of the def-blk.  Don't do this if any GC
    // safepoints intervene.
Node*u=lvb->unique_out();
Block*b=cfg->_bbs[u->_idx];
    lvbblk = b;                 // Last known good block
    while( b != ptrblk ) {      // Scan for safepoints in the middle blocks
      // Skip this hack if we see diamonds and complex control flow
      if( b->num_preds()-1 > 1 ) {
        lvbblk = b = ptrblk;
        break;
      }
      // Skip up one block
b=b->_idom;//Since no complex control flow, _idom is sole pred
      // Scan for GC safepoints
      for( uint j = 0; j < b->_nodes.size(); j++ ) {
        if( b->_nodes[j]->jvms() ) { // Has safepoint?
          lvbblk = b;           // Has safepoint, so LVB cannot go below this block
          break;
        }
      }
    }
  }

  if( tmpNode ) {
    cfg->schedule_node_into_block(tmpNode,lvbblk);
  }

  cfg->schedule_node_into_block(lvb,lvbblk);
  Node *lvb_kill = new (C, 1) MachProjNode( lvb, 10001, lvb->kill_RegMask(), MachProjNode::fat_proj );
  cfg->schedule_node_into_block(lvb_kill,lvbblk);
  // Latency of the load-to-LVB is 3 clks.  If the ptr-load-latency extends
  // past the end of the block, update the ptr/lvb combo to reflect the
  // latency at the block end.
  int last_clk = node_latency.at_grow(lvbblk->end()->_idx);
  int ptr_clk  = node_latency.at_grow(ptr->_idx);
  if( ptr_clk >= last_clk+3 ) {
    node_latency.at_put_grow(lvb->_idx,ptr_clk-3);
  } else {
node_latency.at_put_grow(lvb->_idx,last_clk);
    node_latency.at_put_grow(ptr->_idx,last_clk+3);
  }
}


//=============================================================================
const Type *MachProjNode::bottom_type() const { 
  if( _ideal_reg == fat_proj ) return Type::BOTTOM;
  // Try the normal mechanism first
  const Type *t = in(0)->bottom_type();
  if( t->base() == Type::Tuple ) {
    const TypeTuple *tt = t->is_tuple();
    if (_con < tt->cnt())
      return tt->field_at(_con); 
  }
  // Else use generic type from ideal register set
  assert((uint)_ideal_reg < (uint)_last_machine_leaf && Type::mreg2type[_ideal_reg], "in bounds");
  return Type::mreg2type[_ideal_reg];
}

const TypePtr *MachProjNode::adr_type() const {
  if (bottom_type() == Type::MEMORY) {
    // in(0) might be a narrow MemBar; otherwise we will report TypePtr::BOTTOM
    const TypePtr* adr_type = in(0)->adr_type();
#ifdef ASSERT
    if (!is_error_reported() && !Node::in_dump())
      assert(adr_type != NULL, "source must have adr_type");
#endif
    return adr_type;
  }
  assert(bottom_type()->base() != Type::Memory, "no other memories?");
  return NULL;
}

void MachProjNode::dump_spec(outputStream *st) const {
  ProjNode::dump_spec(st);
  switch (_ideal_reg) {
  case unmatched_proj:  st->print("/unmatched"); break;
case fat_proj:st->print("/fat");break;
  }
}

//=============================================================================
void MachIfNode::dump_spec(outputStream *st) const {
  st->print("P=%f, C=%f",_prob, _fcnt);
}

//=============================================================================
uint MachReturnNode::size_of() const { return sizeof(*this); }

//------------------------------Registers--------------------------------------
const RegMask &MachReturnNode::in_RegMask( uint idx ) const { 
  return _in_rms[idx];
}

const TypePtr *MachReturnNode::adr_type() const {
  // most returns and calls are assumed to consume & modify all of memory
  // the matcher will copy non-wide adr_types from ideal originals
  return _adr_type;
}

//=============================================================================
const Type *MachSafePointNode::bottom_type() const {  return TypeTuple::MEMBAR; }

//------------------------------Registers--------------------------------------
const RegMask &MachSafePointNode::in_RegMask( uint idx ) const { 
  // Values in the domain use the users calling convention, embodied in the
  // _in_rms array of RegMasks.
  if( idx < TypeFunc::Parms ) return _in_rms[idx];
  // Values outside the domain represent debug info
  return *Compile::current()->matcher()->idealreg2spillmask[in(idx)->ideal_reg()];
}

// --- dump_spec -------------------------------------------------------------
void MachSafePointNode::dump_spec(outputStream*st)const{
  if( !jvms() ) return;
  jvms()->dump_spec(st);
  if( _cpd ) _cpd->print_line(jvms()->bc(),st);
}



// --- add_debug --------------------------------------------------------------
static void add_debug( DebugScopeBuilder *dbg, const PhaseRegAlloc *RA, int framesize_words, int max_inarg_words,DebugInfoBuilder::JVM_Part part, int idx, const Node *n ) {
if(n->is_top())return;
const Type*t=n->bottom_type();
  int sz = t->isa_ptr() ? 2/*64-bit VM has 64-bit ptrs*/ : type2size[t->basic_type()];
OptoReg::Name regnum=RA->get_reg(n);
  if( OptoReg::is_valid(regnum) ) {// Got a register/stack?
    if( t==Type::DOUBLE || t->isa_long() ) // Java Doubles & Longs use 2 java words
      dbg->add_vreg_long(part,idx,OptoReg::as_VReg(regnum,framesize_words,max_inarg_words));
    else                        // All others use 1 Java word (but ptrs use 8 bytes)
      dbg->add_vreg     (part,idx,OptoReg::as_VReg(regnum,framesize_words,max_inarg_words),sz==2);
  } else if( t->isa_int() ) {
    dbg->add_const_int(part,idx,t->is_int()->get_con(),false);
}else if(t->isa_oopptr()){
    int oop_index = ciEnv::get_OopTable_index(t->is_oopptr()->const_oop()->encoding());
    dbg->add_const_oop(part,idx,oop_index);
}else if(t==TypePtr::NULL_PTR){
    dbg->add_const_int(part,idx,0,false);
  } else if( t->isa_long() ) {
    dbg->add_const_long(part,idx,t->is_long()->get_con());
  } else if( t->isa_double_constant() ) {
    dbg->add_const_long(part,idx,*(intptr_t*)&t->is_double_constant()->_d);
  } else if( t->base() == Type::RawPtr ) { // raw ptr constant?  Also used for jsr/ret return bcis
    assert0( is_int32(t->is_rawptr()->get_con()) ); // arbitrary 1-slot 64-bit data not handled
    dbg->add_const_int(part,idx,t->is_rawptr()->get_con(),false);
  } else if( t->isa_float_constant() ) {
    dbg->add_const_int(part,idx,*(int*)&t->is_float_constant()->_f);
  } else {
    Unimplemented();
  }
}


// --- add_debug_here ---------------------------------------------------------
void MachSafePointNode::add_debug_here( const OopMap2 *omap2 ) const {
  Compile *C = Compile::current();
  MacroAssembler *masm = &C->_masm;
  const PhaseRegAlloc *RA = C->regalloc();
  const int frsz = RA->_framesize - RA->C->in_preserve_stack_slots();
  const int args = OptoReg::reg2stack(RA->_matcher._new_SP);

  // Loop over the JVMState list to add scope information

  // Visit scopes from oldest to youngest.
  DebugScopeBuilder *caller = NULL;
  for( uint depth = 1; depth <= _jvms->depth(); depth++ ) {
JVMState*jvms=_jvms->of_depth(depth);
    ciMethod* method = jvms->has_method() ? jvms->method() : NULL;
    // Safepoints that do not have method() set only provide oop-map info
    // to support GC; these do not support deoptimization.
    int num_locs = (method == NULL) ? 0 : jvms->loc_size();
    int num_exps = (method == NULL) ? 0 : jvms->stk_size();
    int num_mon  = jvms->nof_monitors();
    assert(method == NULL || jvms->bci() < 0 || num_locs == method->max_locals(),
           "JVMS local count must match that of the method");
    DebugScopeBuilder *dbg = new DebugScopeBuilder(caller, method->objectId(), num_locs, num_exps, num_mon, jvms->bci());

    // Add Local and Expression Stack Information
    for(int i=0; i<num_locs; i++) add_debug(dbg,RA,frsz,args,DebugInfoBuilder::JLocal,i,local      (jvms,i));
    for(int i=0; i<num_exps; i++) add_debug(dbg,RA,frsz,args,DebugInfoBuilder::JStack,i,stack      (jvms,i));
    for(int i=0; i<num_mon ; i++) add_debug(dbg,RA,frsz,args,DebugInfoBuilder::JLock ,i,monitor_obj(jvms,i));

caller=dbg;
  } 

  // Record the inline-cache bit
  if( is_MachCallDynamicJava() ) 
    caller->set_inline_cache();

  // Record any derived pointers.
  // Shallow copy only; the 'compress' step makes a deep copy.
  caller->set_base_derived(&omap2->_base_derived_pairs);

  // Record any callee-saves
  for( int i=0; i<omap2->_callee_save_pairs.length(); i+=2 )
    caller->add_callee_save_pair(omap2->_callee_save_pairs.at(i+0),
                                 omap2->_callee_save_pairs.at(i+1));

  // Install debug info
  masm->add_dbg( masm->rel_pc(), caller );
}


//=============================================================================

uint MachCallNode::cmp( const Node &n ) const
{ return _tf == ((MachCallNode&)n)._tf; }
const Type *MachCallNode::bottom_type() const { return tf()->range(); }
const Type *MachCallNode::Value(PhaseTransform *phase) const { return tf()->range(); }

void MachCallNode::dump_spec(outputStream *st) const { 
  st->print("# "); 
  tf()->dump_on(st);
MachSafePointNode::dump_spec(st);
}


bool MachCallNode::return_value_is_used() const {
  if (tf()->range()->cnt() == TypeFunc::Parms) {
    // void return
    return false;
  }

  // find the projection corresponding to the return value
  for (DUIterator_Fast imax, i = fast_outs(imax); i < imax; i++) {
    Node *use = fast_out(i);
    if (!use->is_Proj()) continue;
    if (use->as_Proj()->_con == TypeFunc::Parms) {
      return true;
    }
  }
  return false;
}


//------------------------------Registers--------------------------------------
const RegMask &MachCallNode::in_RegMask( uint idx ) const { 
  // Values in the domain use the users calling convention, embodied in the
  // _in_rms array of RegMasks.
  if (idx < tf()->domain()->cnt())  return _in_rms[idx];
  // Values outside the domain represent debug info
  return *Compile::current()->matcher()->idealreg2debugmask[in(idx)->ideal_reg()];
}

//=============================================================================
uint MachCallJavaNode::size_of() const { return sizeof(*this); }
uint MachCallJavaNode::cmp( const Node &n ) const { 
  MachCallJavaNode &call = (MachCallJavaNode&)n;
  return MachCallNode::cmp(call) && _method->equals(call._method); 
}
void MachCallJavaNode::dump_spec(outputStream *st) const { 
  if( _method ) {
    _method->print_short_name(st);
    st->print(" ");
  }
  MachCallNode::dump_spec(st);
}

//=============================================================================
uint MachCallStaticJavaNode::size_of() const { return sizeof(*this); }
uint MachCallStaticJavaNode::cmp( const Node &n ) const { 
  MachCallStaticJavaNode &call = (MachCallStaticJavaNode&)n;
  return MachCallJavaNode::cmp(call) && _name == call._name; 
}
void MachCallStaticJavaNode::dump_spec(outputStream *st) const { 
  st->print("Static ");
if(_name)st->print("wrapper for: %s ",_name);
  MachCallJavaNode::dump_spec(st);
}

//------------------------------Registers--------------------------------------
const RegMask&MachCallStaticJavaNode::in_RegMask(uint idx)const{
  // Values in the domain use the users calling convention, embodied in the
  // _in_rms array of RegMasks.
  if (idx < tf()->domain()->cnt())  return _in_rms[idx];
  // Values outside the domain represent debug info
  // For runtime calls, any location is OK for debug info.
  // For normal Java calls, must be limited to the debugmask locations.
  Matcher *M = Compile::current()->matcher();
  // List all the known-to-be-safe-for-debug-info-anywhere runtime calls here.
  bool all_regs_saved = _name && 
    (strcmp(_name,"uncommon_trap") ||
     strcmp(_name,"forward_exception2") ||
     strcmp(_name,"register_finalizer") ||
     0);
  // List ALL the runtime calls here, to make sure we got them all.
  assert0( !_name ||            // Not a runtime call
!strcmp(_name,"uncommon_trap")||
!strcmp(_name,"monitorenter")||
!strcmp(_name,"slow_arraycopy")||
!strcmp(_name,"forward_exception2")||
!strcmp(_name,"register_finalizer")||
           0 );
  RegMask **rms = (all_regs_saved ? M->idealreg2spillmask : M->idealreg2debugmask);
  RegMask *rmp = rms[in(idx)->ideal_reg()];
  assert0( rmp );
  return *rmp;
}

//=============================================================================
uint MachCallDynamicJavaNode::size_of()const{return sizeof(*this);}
void MachCallDynamicJavaNode::dump_spec(outputStream *st) const { 
  st->print("Dynamic ");
  MachCallJavaNode::dump_spec(st);
}
//=============================================================================
uint MachCallVMNode::size_of()const{return sizeof(*this);}
uint MachCallVMNode::cmp(const Node&n)const{
MachCallVMNode&call=(MachCallRuntimeNode&)n;
  return MachCallNode::cmp(call) && !strcmp(_name,call._name);
}
void MachCallVMNode::dump_spec(outputStream*st)const{
  st->print("%s ",_name);
  MachCallNode::dump_spec(st);
}

//=============================================================================
// A shared JVMState for all HaltNodes.  Indicates the start of debug info
// is at TypeFunc::Parms.  Only required for SOE register spill handling - 
// to indicate where the stack-slot-only debug info inputs begin.
// There is no other JVM state needed here.
JVMState jvms_for_throw(0);
JVMState *MachHaltNode::jvms() const {
  return &jvms_for_throw;
}
void MachHaltNode::dump_spec(outputStream*st)const{
  st->print(_msg);
}

