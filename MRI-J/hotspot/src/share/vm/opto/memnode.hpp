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
#ifndef MEMNODE_HPP
#define MEMNODE_HPP


// Portions of code courtesy of Clifford Click
#include "multnode.hpp"
#include "node.hpp"
#include "opcodes.hpp"
#include "type.hpp"

class MultiNode;
class PhaseCCP;
class PhaseTransform;

//------------------------------MemNode----------------------------------------
// Load or Store, possibly throwing a NULL pointer exception
class MemNode : public Node {
protected:
#ifdef ASSERT
  const TypePtr* _adr_type;     // What kind of memory is being addressed?
#endif
  virtual uint size_of() const; // Size is bigger (ASSERT only)
public:
  enum { Control,               // When is it safe to do this load?
         Memory,                // Chunk of memory is being loaded from
         Address,               // Actually address, derived from base
ValueIn//Value to store
  };
protected:
  MemNode( Node *c0, Node *c1, Node *c2, const TypePtr* at )
    : Node(c0,c1,c2   ) { 
    init_class_id(Class_Mem);
    debug_only(_adr_type=at; adr_type();)
  }
  MemNode( Node *c0, Node *c1, Node *c2, const TypePtr* at, Node *c3 )
    : Node(c0,c1,c2,c3) {
    init_class_id(Class_Mem);
    debug_only(_adr_type=at; adr_type();)
  }
  MemNode( Node *c0, Node *c1, Node *c2, const TypePtr* at, Node *c3, Node *c4)
    : Node(c0,c1,c2,c3,c4) {
    init_class_id(Class_Mem);
    debug_only(_adr_type=at; adr_type();)
  }

  // Helpers for the optimizer.  Documented in memnode.cpp.
  static bool detect_ptr_independence(Node* p1, AllocateNode* a1,
                                      Node* p2, AllocateNode* a2,
                                      PhaseTransform* phase);
  static bool adr_phi_is_loop_invariant(Node* adr_phi, Node* cast);

public:
  // This one should probably be a phase-specific function:
  static bool detect_dominating_control(Node* dom, Node* sub);

  // Is this Node a MemNode or some descendent?  Default is YES.
  virtual Node *Ideal_DU_postCCP( PhaseCCP *ccp );
  static Node *Ideal_DU_postCCP_shared( PhaseCCP *ccp, Node *n, Node *adr );

  virtual const class TypePtr *adr_type() const;  // returns bottom_type of address
  
  // Shared code for Ideal methods:
  Node *Ideal_common(PhaseGVN *phase, bool can_reshape);  // Return -1 for short-circuit NULL.

  // Helper function for adr_type() implementations.
  static const TypePtr* calculate_adr_type(const Type* t, const TypePtr* cross_check = NULL);

  // Raw access function, to allow copying of adr_type efficiently in
  // product builds and retain the debug info for debug builds.
  const TypePtr *raw_adr_type() const { 
#ifdef ASSERT
    return _adr_type;
#else
    return 0;
#endif
  }

  // Map a load or store opcode to its corresponding store opcode.
  // (Return -1 if unknown.)
  virtual int store_Opcode() const { return -1; }

  // What is the type of the value in memory?  (T_VOID mean "unspecified".)
  virtual BasicType memory_type() const = 0;
  virtual int memory_size() const { return type2aelembytes[memory_type()]; }

  // Search through memory states which precede this node (load or store).
  // Look for an exact match for the address, with no intervening
  // aliased stores.
  Node* find_previous_store(PhaseTransform* phase);

  // Can this node (load or store) accurately see a stored value in
  // the given memory state?  (The state may or may not be in(Memory).)
  Node* can_see_stored_value(Node* st, PhaseTransform* phase) const;

  static void dump_adr_type(const Node* mem, const TypePtr* adr_type, outputStream *st);
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------LoadNode---------------------------------------
// Load value; requires Memory and Address
class LoadNode : public MemNode {
protected:
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
  const Type* const _type;      // What kind of value is loaded?
  bool _guarding_test_is_precise; // The control-edge test is precise
public:
         
  LoadNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const Type *rt )
:MemNode(c,mem,adr,at),_type(rt),_guarding_test_is_precise(true){
    init_class_id(Class_Load);
  }

  // Polymorphic factory method:
static Node*make(Compile*C,Node*c,Node*mem,Node*adr,const TypePtr*at,const Type*rt,BasicType bt);

  virtual uint hash()   const;  // Check the type

  // Handle algebraic identities here.  If we have an identity, return the Node
  // we are equivalent to.  We look for Load of a Store.
  virtual Node *Identity( PhaseTransform *phase );

  // If the load is from Field memory and the pointer is non-null, we can
  // zero out the control input.
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);

  // Recover original value from boxed values
  Node *eliminate_autobox(PhaseGVN *phase);

  // Compute a new Type for this node.  Basically we just do the pre-check,
  // then call the virtual add() to set the type.
  virtual const Type *Value( PhaseTransform *phase ) const;

  virtual uint ideal_reg() const;
  virtual const Type *bottom_type() const;
  // Following method is copied from TypeNode:
  void set_type(const Type* t) {
    assert(t != NULL, "sanity");
    debug_only(uint check_hash = (VerifyHashTableKeys && _hash_lock) ? hash() : NO_HASH);
    *(const Type**)&_type = t;   // cast away const-ness
    // If this node is in the hash table, make sure it doesn't need a rehash.
    assert(check_hash == NO_HASH || check_hash == hash(), "type change must preserve hash code");
  }
  const Type* type() const { assert(_type != NULL, "sanity"); return _type; };

  // Do not match memory edge
  virtual uint match_edge(uint idx) const;

  // Allow Loads to move up to a equivalent dominating test
  virtual bool depends_only_on_test() const { return _guarding_test_is_precise; }
  void guarding_test_no_longer_precise() { _guarding_test_is_precise = false; }

  // Map a load opcode to its corresponding store opcode.
  virtual int store_Opcode() const = 0;
virtual const Type*zero_type()const=0;

  virtual void dump_spec(outputStream *st) const;
protected:
  const Type* load_array_final_field(const TypeKlassPtr *tkls,
                                     ciKlass* klass) const;
};

//------------------------------LoadBNode--------------------------------------
// Load a byte (8bits signed) from memory
class LoadBNode : public LoadNode {
public:
  LoadBNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const TypeInt *ti = TypeInt::BYTE )
    : LoadNode(c,mem,adr,at,ti) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual int store_Opcode() const { return Op_StoreB; }
  virtual BasicType memory_type() const { return T_BYTE; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------LoadCNode--------------------------------------
// Load a char (16bits unsigned) from memory
class LoadCNode : public LoadNode {
public:
  LoadCNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const TypeInt *ti = TypeInt::CHAR ) 
    : LoadNode(c,mem,adr,at,ti) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual int store_Opcode() const { return Op_StoreC; }
  virtual BasicType memory_type() const { return T_CHAR; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------LoadINode--------------------------------------
// Load an integer from memory
class LoadINode : public LoadNode {
public:
  LoadINode( Node *c, Node *mem, Node *adr, const TypePtr* at, const TypeInt *ti = TypeInt::INT ) 
    : LoadNode(c,mem,adr,at,ti) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Identity( PhaseTransform *phase );
  virtual int store_Opcode() const { return Op_StoreI; }
  virtual BasicType memory_type() const { return T_INT; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------LoadRangeNode----------------------------------
// Load an array length from the array
class LoadRangeNode : public LoadINode {
public:
  LoadRangeNode( Node *c, Node *mem, Node *adr, const TypeInt *ti = TypeInt::POS )
    : LoadINode(c,mem,adr,TypeAryPtr::RANGE,ti) {}
  virtual int Opcode() const;
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Identity( PhaseTransform *phase );
  virtual const Type *zero_type() const { ShouldNotReachHere(); return TypeInt::ZERO; }
};

//------------------------------LoadLNode--------------------------------------
// Load a long from memory
class LoadLNode : public LoadNode {
  virtual uint hash() const { return LoadNode::hash() + _require_atomic_access; }
  virtual uint cmp( const Node &n ) const {
    return _require_atomic_access == ((LoadLNode&)n)._require_atomic_access
      && LoadNode::cmp(n);
  }
  virtual uint size_of() const { return sizeof(*this); }
  const bool _require_atomic_access;  // is piecewise load forbidden?

public:
  LoadLNode( Node *c, Node *mem, Node *adr, const TypePtr* at,
             const TypeLong *tl = TypeLong::LONG,
             bool require_atomic_access = false )
    : LoadNode(c,mem,adr,at,tl)
    , _require_atomic_access(require_atomic_access)
  {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegL; }
  virtual int store_Opcode() const { return Op_StoreL; }
  virtual BasicType memory_type() const { return T_LONG; }
  bool require_atomic_access() { return _require_atomic_access; }
  virtual void dump_spec(outputStream *st) const {
    LoadNode::dump_spec(st);
    if (_require_atomic_access)  st->print(" Atomic!");
  }
virtual const Type*zero_type()const{return TypeLong::ZERO;}
};

//------------------------------LoadL_unalignedNode----------------------------
// Load a long from unaligned memory
class LoadL_unalignedNode : public LoadLNode {
public:
  LoadL_unalignedNode( Node *c, Node *mem, Node *adr, const TypePtr* at ) 
    : LoadLNode(c,mem,adr,at) {}
  virtual int Opcode() const;
virtual const Type*zero_type()const{return TypeLong::ZERO;}
};

//------------------------------LoadFNode--------------------------------------
// Load a float (64 bits) from memory
class LoadFNode : public LoadNode {
public:
  LoadFNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const Type *t = Type::FLOAT ) 
    : LoadNode(c,mem,adr,at,t) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegF; }
  virtual int store_Opcode() const { return Op_StoreF; }
  virtual BasicType memory_type() const { return T_FLOAT; }
virtual const Type*zero_type()const{return TypeF::ZERO;}
};

//------------------------------LoadDNode--------------------------------------
// Load a double (64 bits) from memory
class LoadDNode : public LoadNode {
public:
  LoadDNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const Type *t = Type::DOUBLE ) 
    : LoadNode(c,mem,adr,at,t) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegD; }
  virtual int store_Opcode() const { return Op_StoreD; }
  virtual BasicType memory_type() const { return T_DOUBLE; }
virtual const Type*zero_type()const{return TypeD::ZERO;}
};

//------------------------------LoadD_unalignedNode----------------------------
// Load a double from unaligned memory
class LoadD_unalignedNode : public LoadDNode {
public:
  LoadD_unalignedNode( Node *c, Node *mem, Node *adr, const TypePtr* at ) 
    : LoadDNode(c,mem,adr,at) {}
  virtual int Opcode() const;
virtual const Type*zero_type()const{return TypeD::ZERO;}
};

//------------------------------LoadPNode--------------------------------------
// Load a pointer from memory (either object or array)
class LoadPNode : public LoadNode {
public:
  LoadPNode( Node *c, Node *mem, Node *adr, const TypePtr *at, const TypePtr* t ) 
    : LoadNode(c,mem,adr,at,t) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegP; }
  virtual int store_Opcode() const { return Op_StoreP; }
  virtual BasicType memory_type() const { return T_ADDRESS; }
  virtual const Type *zero_type() const { return TypePtr::NULL_PTR; }
};

//------------------------------LoadKlassNode----------------------------------
// Load a Klass from an object
class LoadKlassNode : public LoadPNode {
public:
  LoadKlassNode( Node *c, Node *mem, Node *adr, const TypePtr *at, const TypeKlassPtr *tk = TypeKlassPtr::OBJECT )
    : LoadPNode(c,mem,adr,at,tk) {}
  virtual int Opcode() const;
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Identity( PhaseTransform *phase );
  virtual const Type *zero_type() const { return TypePtr::NULL_PTR; }
};

//------------------------------LoadSNode--------------------------------------
// Load a short (16bits signed) from memory
class LoadSNode : public LoadNode {
public:
  LoadSNode( Node *c, Node *mem, Node *adr, const TypePtr* at, const TypeInt *ti = TypeInt::SHORT ) 
    : LoadNode(c,mem,adr,at,ti) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual int store_Opcode() const { return Op_StoreC; }
  virtual BasicType memory_type() const { return T_SHORT; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------StoreNode--------------------------------------
// Store value; requires Store, Address and Value
class StoreNode : public MemNode {
protected:
  virtual uint cmp( const Node &n ) const;

  Node *Ideal_masked_input       (PhaseGVN *phase, uint mask);
  Node *Ideal_sign_extended_input(PhaseGVN *phase, int  num_bits);

public:
  StoreNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val )
    : MemNode(c,mem,adr,at,val) {
    init_class_id(Class_Store);
  }
  StoreNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val, Node *oop_store )
    : MemNode(c,mem,adr,at,val,oop_store) {
    init_class_id(Class_Store);
  }

  // Polymorphic factory method:
  static StoreNode* make( Compile *C, Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val, BasicType bt );

  virtual uint hash() const;    // Check the type

  // If the store is to Field memory and the pointer is non-null, we can
  // zero out the control input.
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);

  // Compute a new Type for this node.  Basically we just do the pre-check,
  // then call the virtual add() to set the type.
  virtual const Type *Value( PhaseTransform *phase ) const;

  // Check for identity function on memory (Load then Store at same address)
  virtual Node *Identity( PhaseTransform *phase );

  // Do not match memory edge
  virtual uint match_edge(uint idx) const;

  virtual const Type *bottom_type() const;  // returns Type::MEMORY

virtual const Type*zero_type()const=0;

  // Map a store opcode to its corresponding own opcode, trivially.
  virtual int store_Opcode() const { return Opcode(); }

  // have all possible loads of the value stored been optimized away?
  bool value_never_loaded(PhaseTransform *phase) const;
};

//------------------------------StoreBNode-------------------------------------
// Store byte to memory
class StoreBNode : public StoreNode {
public:
  StoreBNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual BasicType memory_type() const { return T_BYTE; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------StoreCNode-------------------------------------
// Store char/short to memory
class StoreCNode : public StoreNode {
public:
  StoreCNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual BasicType memory_type() const { return T_CHAR; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------StoreINode-------------------------------------
// Store int to memory
class StoreINode : public StoreNode {
public:
  StoreINode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual BasicType memory_type() const { return T_INT; }
virtual const Type*zero_type()const{return TypeInt::ZERO;}
};

//------------------------------StoreLNode-------------------------------------
// Store long to memory
class StoreLNode : public StoreNode {
  virtual uint hash() const { return StoreNode::hash() + _require_atomic_access; }
  virtual uint cmp( const Node &n ) const {
    return _require_atomic_access == ((StoreLNode&)n)._require_atomic_access
      && StoreNode::cmp(n);
  }
  virtual uint size_of() const { return sizeof(*this); }
  const bool _require_atomic_access;  // is piecewise store forbidden?

public:
  StoreLNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val,
              bool require_atomic_access = false )
    : StoreNode(c,mem,adr,at,val)
    , _require_atomic_access(require_atomic_access)
  {}
  virtual int Opcode() const;
  virtual BasicType memory_type() const { return T_LONG; }
  bool require_atomic_access() { return _require_atomic_access; }
  virtual void dump_spec(outputStream *st) const {
    StoreNode::dump_spec(st);
    if (_require_atomic_access)  st->print(" Atomic!");
  }
virtual const Type*zero_type()const{return TypeLong::ZERO;}
};

//------------------------------StoreFNode-------------------------------------
// Store float to memory
class StoreFNode : public StoreNode {
public:
  StoreFNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual BasicType memory_type() const { return T_FLOAT; }
virtual const Type*zero_type()const{return TypeF::ZERO;}
};

//------------------------------StoreDNode-------------------------------------
// Store double to memory
class StoreDNode : public StoreNode {
public:
  StoreDNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual BasicType memory_type() const { return T_DOUBLE; }
virtual const Type*zero_type()const{return TypeD::ZERO;}
};

//------------------------------StorePNode-------------------------------------
// Store pointer to memory
class StorePNode : public StoreNode {
public:
  StorePNode( Node *c, Node *mem, Node *adr, const TypePtr* at, Node *val ) : StoreNode(c,mem,adr,at,val) {}
  virtual int Opcode() const;
  virtual BasicType memory_type() const { return T_ADDRESS; }
  virtual const Type *zero_type() const { return TypePtr::NULL_PTR; }
};

//------------------------------LoadPLockedNode---------------------------------
// Load-locked a pointer from memory (either object or array).
// On Sparc & Intel this is implemented as a normal pointer load.
// On PowerPC and friends it's a real load-locked.
class LoadPLockedNode : public LoadPNode {
public:
  LoadPLockedNode( Node *c, Node *mem, Node *adr )
    : LoadPNode(c,mem,adr,TypeRawPtr::BOTTOM, TypeRawPtr::BOTTOM) {}
  virtual int Opcode() const;
  virtual int store_Opcode() const { ShouldNotReachHere(); return 0; }
};

//------------------------------SCMemProjNode---------------------------------------
// This class defines a projection of the memory  state of a store conditional node.
// These nodes return a value, but also update memory.
class SCMemProjNode : public ProjNode {
public:
  enum {SCMEMPROJCON = (uint)-2};
  SCMemProjNode( Node *src) : ProjNode( src, SCMEMPROJCON) { }
  virtual int Opcode() const;
  virtual bool      is_CFG() const  { return false; }
  virtual const Type *bottom_type() const {return Type::MEMORY;}
  virtual const TypePtr *adr_type() const { return in(0)->in(MemNode::Memory)->adr_type();}
  virtual uint ideal_reg() const { return 0;} // memory projections don't have a register
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual void dump_spec(outputStream *st) const {};
};

//------------------------------LoadStoreNode---------------------------
class LoadStoreNode : public Node {
public:
  enum {
    ExpectedIn = MemNode::ValueIn+1 // One more input than MemNode
  };
  LoadStoreNode( Node *c, Node *mem, Node *adr, Node *val, Node *ex);
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual bool depends_only_on_test() const { return false; }
  virtual const Type *bottom_type() const { return TypeInt::BOOL; }
  virtual uint ideal_reg() const { return Op_RegFlags; }
  virtual uint match_edge(uint idx) const { return idx == MemNode::Address || idx == MemNode::ValueIn; }
};

//------------------------------CompareAndSwapLNode---------------------------
class CompareAndSwapLNode : public LoadStoreNode {
public:
  CompareAndSwapLNode( Node *c, Node *mem, Node *adr, Node *val, Node *ex) : LoadStoreNode(c, mem, adr, val, ex) { }
  virtual int Opcode() const;
};


//------------------------------CompareAndSwapINode---------------------------
class CompareAndSwapINode : public LoadStoreNode {
public:
  CompareAndSwapINode( Node *c, Node *mem, Node *adr, Node *val, Node *ex) : LoadStoreNode(c, mem, adr, val, ex) { }
  virtual int Opcode() const;
};


//------------------------------CompareAndSwapPNode---------------------------
class CompareAndSwapPNode : public LoadStoreNode {
public:
  CompareAndSwapPNode( Node *c, Node *mem, Node *adr, Node *val, Node *ex) : LoadStoreNode(c, mem, adr, val, ex) { }
  virtual int Opcode() const;
};

//------------------------------ClearArray-------------------------------------
class ClearArrayNode: public Node {
public:
  ClearArrayNode( Node *ctrl, Node *arymem, Node *word_cnt, Node *base ) : Node(ctrl,arymem,word_cnt,base) {}
  virtual int         Opcode() const;
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  // ClearArray modifies array elements, and so affects only the
  // array memory addressed by the bottom_type of its base address.
  virtual const class TypePtr *adr_type() const;
  virtual Node *Identity( PhaseTransform *phase );
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual uint match_edge(uint idx) const;

  // Clear the given area of an object or array.
  // The start offset must always be aligned mod BytesPerInt.
  // The end offset must always be aligned mod BytesPerLong.
  // Return the new memory.
  static Node* clear_memory(Node* control, Node* mem, Node* dest,
                            intptr_t start_offset,
                            intptr_t end_offset,
                            PhaseGVN* phase);
  static Node* clear_memory(Node* control, Node* mem, Node* dest,
                            intptr_t start_offset,
                            Node* end_offset,
                            PhaseGVN* phase);
  static Node* clear_memory(Node* control, Node* mem, Node* dest,
                            Node* start_offset,
                            Node* end_offset,
                            PhaseGVN* phase);
};

//------------------------------ArrayCopy--------------------------------------
// Primitive arraycopy.  These routines assume the caller has already
// null-checked the src & dst, range-checked the src & dst, and for oop-arrays
// already done a simple compability check (it usually suffices to see that
// the dst is of it's declared type).  The src offset, dst offset and length
// all must be of at least the declared alignment; e.g.  char arrays with 0
// offsets still need the 2-byte aligned copy to handle the end-case right.

// Split into a ArrayCopySrcNode and a regular ArrayCopyNode.  The Src node
// carries the use/anti-dependence for the src memory, and the regular
// ArrayCopy node carries the use/def/anti-dependence of the dst memory.
// Scheduling keeps them together.
class ArrayCopySrcNode:public Node{
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger (ASSERT only)
public:
  const int _scale;             // 0 for oops, 1, 2, 4 or 8 for primitive types
  const int _known_disjoint;    // known disjoint: can skip overlap tests
  ArrayCopySrcNode( Node *srcmem, Node *srclenmem, Node *src, Node *src_off, int scale, int known_disjoint ) : Node(0,srcmem,srclenmem,src,src_off), _scale(scale), _known_disjoint(known_disjoint) { init_flags(Flag_needs_anti_dependence_check); }
  virtual int Opcode() const;
  // The anti-dependence address type for the src memory
  virtual const class TypePtr *adr_type() const;
  virtual void emit(PhaseRegAlloc *ra_) const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual const RegMask &out_RegMask() const { return RegMask::Empty; }
  virtual const RegMask &in_RegMask(uint) const;
};

class ArrayCopyNode:public Node{
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger (ASSERT only)
OopMap2*_oop_map;
public:
  ArrayCopyNode( Node *ctrl, Node *ACsrc, Node *dstmem, Node *dstlenmem, Node *dst, Node *dst_off, Node *elem_cnt);
  virtual int Opcode() const;
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  // The anti-dependence address type for the dst memory
  virtual const class TypePtr *adr_type() const;
  virtual Node *Identity( PhaseTransform *phase );
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual uint match_edge(uint idx) const;
  virtual void emit(PhaseRegAlloc *ra_) const;
  virtual const RegMask &out_RegMask() const { return RegMask::Empty; }
  virtual const RegMask &kill_RegMask() const; // Defined in the AD file
  virtual const RegMask &in_RegMask(uint) const;
  virtual int base_derived_idx() const { return /*0:ctrl,1:ACsrc,2:dstmem,3:dstlenmem,4:dst,5:dst_off,6:elem_cnt*/7; }
  virtual void set_oopmap( OopMap2 *om ) { _oop_map = om; }
  virtual OopMap2 * get_oopmap( ) const { return _oop_map; }
};

//------------------------------StrComp-------------------------------------
class StrCompNode: public Node {
public:
  StrCompNode(Node *control,
              Node* char_array_mem,
              Node* value_mem,
              Node* s1, Node* s2): Node(control,
                                        char_array_mem,
                                        value_mem,
                                        s1, s2) {};
  virtual int Opcode() const;
  virtual const Type* bottom_type() const { return TypeInt::INT; }
  // a StrCompNode (conservatively) aliases with everything:
  virtual const TypePtr* adr_type() const { return TypePtr::BOTTOM; }
  virtual uint match_edge(uint idx) const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
};

//------------------------------StrEquals-------------------------------------
class StrEqualsNode:public Node{
public:
StrEqualsNode(Node*control,
                Node* char_array_mem,
                Node* value_mem,
                Node* s1, Node* s2): Node(control,
                                          char_array_mem,
                                          value_mem,
                                          s1, s2) {};
  virtual int Opcode() const;
  virtual const Type* bottom_type() const { return TypeInt::INT; }
  // a StrEqualsNode (conservatively) aliases with everything:
  virtual const TypePtr* adr_type() const { return TypePtr::BOTTOM; }
  virtual uint match_edge(uint idx) const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
};

//------------------------------StrIndexOf-------------------------------------
class StrIndexOfNode:public Node{
public:
StrIndexOfNode(Node*control,
                 Node* char_array_mem,
                 Node* value_mem,
                 Node* count_mem,
                 Node* offset_mem,
                 Node* s1, Node* s2): Node(control,
                                           char_array_mem,
                                           value_mem,
                                           count_mem,
                                           offset_mem,
                                           s1, s2) {};
  virtual int Opcode() const;
  virtual bool depends_only_on_test() const { return false; }
  virtual const Type* bottom_type() const { return TypeInt::INT; }
  // a StrIndexOfNode (conservatively) aliases with everything:
  virtual const TypePtr* adr_type() const { return TypePtr::BOTTOM; }
  virtual uint match_edge(uint idx) const;
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
};

//------------------------------AryEq---------------------------------------
class AryEqNode:public Node{
public:
  AryEqNode(Node *control, Node* s1, Node* s2): Node(control, s1, s2) {};
  virtual int Opcode() const;
  virtual bool depends_only_on_test() const { return false; }
virtual const Type*bottom_type()const{return TypeInt::BOOL;}
virtual const TypePtr*adr_type()const{return TypeAryPtr::CHARS;}
  virtual uint ideal_reg() const { return Op_RegI; }
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
};

//------------------------------MemBar-----------------------------------------
// There are different flavors of Memory Barriers to match the Java Memory
// Model.  Monitor-enter and volatile-load act as Aquires: no following ref
// can be moved to before them.  We insert a MemBar-Acquire after a FastLock or
// volatile-load.  Monitor-exit and volatile-store act as Release: no
// preceeding ref can be moved to after them.  We insert a MemBar-Release
// before a FastUnlock or volatile-store.  All volatiles need to be
// serialized, so we follow all volatile-stores with a MemBar-Volatile to
// seperate it from any following volatile-load.
class MemBarNode: public MultiNode {
uint hash()const;
  virtual uint cmp( const Node &n ) const ;    // Always fail, except on self

  virtual uint size_of() const { return sizeof(*this); }
  // Memory type this node is serializing.  Usually either rawptr or bottom.
  const TypePtr* _adr_type;

public:
  enum {
    Precedent = TypeFunc::Parms  // optional edge to force precedence
  };
  MemBarNode(Compile* C, int alias_idx, Node* precedent);
  virtual int Opcode() const = 0;
  virtual const class TypePtr *adr_type() const { return _adr_type; }
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual uint match_edge(uint idx) const { return 0; }
  virtual const Type *bottom_type() const { return TypeTuple::MEMBAR; }
  virtual Node *match( const ProjNode *proj, const Matcher *m );
  // Factory method.  Builds a wide or narrow membar.
  // Optional 'precedent' becomes an extra edge if not null.
  static MemBarNode* make(Compile* C, int opcode,
                          int alias_idx = Compile::AliasIdxBot,
                          Node* precedent = NULL);
};

// "Acquire" - no following ref can move before (but earlier refs can
// follow, like an early Load stalled in cache).  Requires multi-cpu
// visibility.  Inserted after a volatile load or FastLock.
class MemBarAcquireNode: public MemBarNode {
public:
  MemBarAcquireNode(Compile* C, int alias_idx, Node* precedent)
    : MemBarNode(C, alias_idx, precedent) {}
  virtual int Opcode() const;
};

// "Release" - no earlier ref can move after (but later refs can move
// up, like a speculative pipelined cache-hitting Load).  Requires
// multi-cpu visibility.  Inserted before a volatile store or FastUnLock.
class MemBarReleaseNode: public MemBarNode {
public:
  MemBarReleaseNode(Compile* C, int alias_idx, Node* precedent)
    : MemBarNode(C, alias_idx, precedent) {}
  virtual int Opcode() const;
};

// Ordering between a volatile store and a following volatile load.
// Requires multi-CPU visibility?
class MemBarVolatileNode: public MemBarNode {
public:
  MemBarVolatileNode(Compile* C, int alias_idx, Node* precedent)
    : MemBarNode(C, alias_idx, precedent) {}
  virtual int Opcode() const;
};

// Ordering within the same CPU.  Used to order unsafe memory references
// inside the compiler when we lack alias info.  Not needed "outside" the
// compiler because the CPU does all the ordering for us.
class MemBarCPUOrderNode: public MemBarNode {
public:
  MemBarCPUOrderNode(Compile* C, int alias_idx, Node* precedent)
    : MemBarNode(C, alias_idx, precedent) {}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return 0; } // not matched in the AD file
};

// Azul does not need the Initialize node or step.
// Our objects are zero/fenced coming out of the milli-code.
// Note that if there are non-zero final fields, Azul still needs 
// proper final-field semantics.
//class InitializeNode: public MemBarNode {

//------------------------------MergeMem---------------------------------------
// (See comment in memnode.cpp near MergeMemNode::MergeMemNode for semantics.)
class MergeMemNode: public Node {
  virtual uint hash() const ;                  // { return NO_HASH; }
  virtual uint cmp( const Node &n ) const ;    // Always fail, except on self
  friend class MergeMemStream;
  MergeMemNode(Node* def);  // clients use MergeMemNode::make

public:
  // If the input is a whole memory state, clone it with all its slices intact.
  // Otherwise, make a new memory state with just that base memory input.
  // In either case, the result is a newly created MergeMem.
  static MergeMemNode* make(Compile* C, Node* base_memory);

  virtual int Opcode() const;
  virtual Node *Identity( PhaseTransform *phase );
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const { return 0; }
  virtual const RegMask &out_RegMask() const;
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  virtual const TypePtr *adr_type() const { return TypePtr::BOTTOM; }
  // sparse accessors
  // Fetch the previously stored "set_memory_at", or else the base memory.
  // (Caller should clone it if it is a phi-nest.)
  Node* memory_at(uint alias_idx) const;
  // set the memory, regardless of its previous value
  void set_memory_at(uint alias_idx, Node* n);
  // the "base" is the memory that provides the non-finite support
  Node* base_memory() const       { return in(Compile::AliasIdxBot); }
  // warning: setting the base can implicitly set any of the other slices too
  void set_base_memory(Node* def);
  // sentinel value which denotes a copy of the base memory:
  Node*   empty_memory() const    { return in(Compile::AliasIdxTop); }
  static Node* make_empty_memory(); // where the sentinel comes from
  bool is_empty_memory(Node* n) const { assert((n == empty_memory()) == n->is_top(), "sanity"); return n->is_top(); }
  // hook for the iterator, to perform any necessary setup
  void iteration_setup(const MergeMemNode* other = NULL);
  // push sentinels until I am at least as long as the other (semantic no-op)
  void grow_to_match(const MergeMemNode* other);
  bool verify_sparse() const PRODUCT_RETURN0;
  virtual void dump_spec(outputStream *st) const;
};

class MergeMemStream : public StackObj {
 private:
  MergeMemNode*       _mm;
  const MergeMemNode* _mm2;  // optional second guy, contributes non-empty iterations
  Node*               _mm_base;  // loop-invariant base memory of _mm
  int                 _idx;
  int                 _cnt;
  Node*               _mem;
  Node*               _mem2;
  int                 _cnt2;

  void init(MergeMemNode* mm, const MergeMemNode* mm2 = NULL) {
    // subsume_node will break sparseness at times, whenever a memory slice
    // folds down to a copy of the base ("fat") memory.  In such a case,
    // the raw edge will update to base, although it should be top.
    // This iterator will recognize either top or base_memory as an
    // "empty" slice.  See is_empty, is_empty2, and next below.
    //
    // The sparseness property is repaired in MergeMemNode::Ideal.
    // As long as access to a MergeMem goes through this iterator
    // or the memory_at accessor, flaws in the sparseness will
    // never be observed.
    //
    // Also, iteration_setup repairs sparseness.
    assert(mm->verify_sparse(), "please, no dups of base");
    assert(mm2==NULL || mm2->verify_sparse(), "please, no dups of base");

    _mm  = mm;
    _mm_base = mm->base_memory();
    _mm2 = mm2;
    _cnt = mm->req();
    _idx = Compile::AliasIdxBot-1; // start at the base memory
    _mem = NULL;
    _mem2 = NULL;
  }

#ifdef ASSERT
  Node* check_memory() const {
    if (at_base_memory())
      return _mm->base_memory();
    else if ((uint)_idx < _mm->req() && !_mm->in(_idx)->is_top())
      return _mm->memory_at(_idx);
    else
      return _mm_base;
  }
  Node* check_memory2() const {
    return at_base_memory()? _mm2->base_memory(): _mm2->memory_at(_idx);
  }
#endif

  static bool match_memory(Node* mem, const MergeMemNode* mm, int idx) PRODUCT_RETURN0;
  void assert_synch() const {
    assert(!_mem || _idx >= _cnt || match_memory(_mem, _mm, _idx),
           "no side-effects except through the stream");
  }

 public:

  // expected usages:
  // for (MergeMemStream mms(mem->is_MergeMem()); next_non_empty(); ) { ... }
  // for (MergeMemStream mms(mem1, mem2); next_non_empty2(); ) { ... }

  // iterate over one merge
  MergeMemStream(MergeMemNode* mm) {
    mm->iteration_setup();
    init(mm);
    debug_only(_cnt2 = 999);
  }
  // iterate in parallel over two merges
  // only iterates through non-empty elements of mm2
  MergeMemStream(MergeMemNode* mm, const MergeMemNode* mm2) {
    assert(mm2, "second argument must be a MergeMem also");
    ((MergeMemNode*)mm2)->iteration_setup();  // update hidden state
    mm->iteration_setup(mm2);
    init(mm, mm2);
    _cnt2 = mm2->req();
  }
#ifdef ASSERT
  ~MergeMemStream() {
    assert_synch();
  }
#endif

  MergeMemNode* all_memory() const {
    return _mm;
  }
  Node* base_memory() const {
    assert(_mm_base == _mm->base_memory(), "no update to base memory, please");
    return _mm_base;
  }
  const MergeMemNode* all_memory2() const {
    assert(_mm2 != NULL, "");
    return _mm2;
  }
  bool at_base_memory() const {
    return _idx == Compile::AliasIdxBot;
  }
  int alias_idx() const {
    assert(_mem, "must call next 1st");
    return _idx;
  }

  const TypePtr* adr_type() const {
    return Compile::current()->get_adr_type(alias_idx());
  }

  const TypePtr* adr_type(Compile* C) const {
    return C->get_adr_type(alias_idx());
  }
  bool is_empty() const {
    assert(_mem, "must call next 1st");
    assert(_mem->is_top() == (_mem==_mm->empty_memory()), "correct sentinel");
    return _mem->is_top();
  }
  bool is_empty2() const {
    assert(_mem2, "must call next 1st");
    assert(_mem2->is_top() == (_mem2==_mm2->empty_memory()), "correct sentinel");
    return _mem2->is_top();
  }
  Node* memory() const {
    assert(!is_empty(), "must not be empty");
    assert_synch();
    return _mem;
  }
  // get the current memory, regardless of empty or non-empty status
  Node* force_memory() const {
    assert(!is_empty() || !at_base_memory(), "");
    // Use _mm_base to defend against updates to _mem->base_memory().
    Node *mem = _mem->is_top() ? _mm_base : _mem;
    assert(mem == check_memory(), "");
    return mem;
  }
  Node* memory2() const {
    assert(_mem2 == check_memory2(), "");
    return _mem2;
  }
  void set_memory(Node* mem) {
    if (at_base_memory()) {
      // Note that this does not change the invariant _mm_base.
      _mm->set_base_memory(mem);
    } else {
      _mm->set_memory_at(_idx, mem);
    }
    _mem = mem;
    assert_synch();
  }

  // Recover from a side effect to the MergeMemNode.
  void set_memory() {
    _mem = _mm->in(_idx);
  }

  bool next()  { return next(false); }
  bool next2() { return next(true); }

  bool next_non_empty()  { return next_non_empty(false); }
  bool next_non_empty2() { return next_non_empty(true); }
  // next_non_empty2 can yield states where is_empty() is true

 private:
  // find the next item, which might be empty
  bool next(bool have_mm2) {
    assert((_mm2 != NULL) == have_mm2, "use other next");
    assert_synch();
    if (++_idx < _cnt) {
      // Note:  This iterator allows _mm to be non-sparse.
      // It behaves the same whether _mem is top or base_memory.
      _mem = _mm->in(_idx);
      if (have_mm2)
        _mem2 = _mm2->in((_idx < _cnt2) ? _idx : Compile::AliasIdxTop);
      return true;
    }
    return false;
  }

  // find the next non-empty item
  bool next_non_empty(bool have_mm2) {
    while (next(have_mm2)) {
      if (!is_empty()) {
        // make sure _mem2 is filled in sensibly
        if (have_mm2 && _mem2->is_top())  _mem2 = _mm2->base_memory();
        return true;
      } else if (have_mm2 && !is_empty2()) {
        return true;   // is_empty() == true
      }
    }
    return false;
  }
};

//------------------------------Prefetch---------------------------------------

// Non-faulting prefetch load.  Prefetch for many reads.
class PrefetchReadNode : public Node {
public:
PrefetchReadNode(Node*mem,Node*adr):Node(0,mem,adr){}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const { return idx==2; }
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  virtual const TypePtr *adr_type() const { return TypePtr::BOTTOM; }
};

// Non-faulting prefetch load.  Prefetch for many reads & many writes.
class PrefetchWriteNode : public Node {
public:
PrefetchWriteNode(Node*mem,Node*adr):Node(0,mem,adr){}
  virtual int Opcode() const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const { return idx==2; }
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  virtual const TypePtr *adr_type() const { return TypePtr::BOTTOM; }
};

//------------------------------EscapeMemory----------------------------------
// New private memory is merging with public already-escaped memory.
class EscapeMemoryNode:public Node{
public:
  EscapeMemoryNode( Node *private_memory, Node *public_memory) : Node(0,private_memory,public_memory) { init_flags(Flag_needs_anti_dependence_check); }
  virtual int Opcode() const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const { return 0; }
  virtual const Type *bottom_type() const { return Type::MEMORY; }
  // This memory contains all fields of some object kind, so we return a
  // very conservative (i.e., fat) memory slice.
  virtual const TypePtr *adr_type() const { return TypePtr::BOTTOM; }
  virtual const RegMask &out_RegMask() const { return RegMask::Empty; }
};

#endif // MEMNODE_HPP
