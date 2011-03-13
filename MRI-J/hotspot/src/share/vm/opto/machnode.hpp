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
#ifndef MACHNODE_HPP
#define MACHNODE_HPP


#include "callnode.hpp"
#include "matcher.hpp"
#include "multnode.hpp"
#include "node.hpp"
#include "opcodes.hpp"
#include "type.hpp"

class Block;
class CPData;
class Compile;
class JVMState;
class MachCallDynamicJavaNode;
class MachCallJavaNode;
class MachCallLeafNode;
class MachCallNode;
class MachCallRuntimeNode;
class MachCallStaticJavaNode;
class MachCallVMNode;
class MachEpilogNode;
class MachIfNode;
class MachLVBNode;
class MachNullCheckNode;
class MachOper;
class MachProjNode;
class MachPrologNode;
class MachReturnNode;
class MachSafePointNode;
class MachSpillCopyNode;
class MachTypeNode;
class Matcher;
class OopMap2;
class PhaseRegAlloc;
class RegMask;
class State;
class TypeOopPtr;

//---------------------------MachOper------------------------------------------
class MachOper : public ResourceObj {
public:
  // Allocate right next to the MachNodes in the same arena
  void *operator new( size_t x, Compile* C ) { return C->node_arena()->Amalloc_D(x); }

  // Opcode 
  virtual uint opcode() const = 0;

  // Number of input edges.
  // Generally at least 1
  virtual uint num_edges() const { return 1; }
  // Array of Register masks 
  virtual const RegMask *in_RegMask(int index) const;

  // Methods to output the encoding of the operand

  // Negate conditional branches.  Error for non-branch Nodes
  virtual void negate();

  // Return the value requested
  // result register lookup, corresponding to int_format
virtual Register reg(PhaseRegAlloc*ra_,const Node*node)const;
  // input register lookup, corresponding to ext_format
virtual Register reg(PhaseRegAlloc*ra_,const Node*node,int idx)const;
  virtual intptr_t  constant() const;
  virtual bool constant_is_oop() const;
  virtual jdouble constantD() const;
  virtual jfloat  constantF() const;
  virtual jlong   constantL() const;
  virtual TypeOopPtr *oop() const;
  virtual int  ccode() const;
  // A zero, default, indicates this value is not needed.
  // May need to lookup the base register, as done in int_ and ext_format
virtual Register base(PhaseRegAlloc*ra_,const Node*node,int idx)const;
virtual Register index(PhaseRegAlloc*ra_,const Node*node,int idx)const;
  virtual int  scale() const;
  // Parameters needed to support MEMORY_INTERFACE access to stackSlot
  virtual int  disp (PhaseRegAlloc *ra_, const Node *node, int idx) const;
  // Check for PC-Relative displacement
  virtual bool disp_is_oop() const;
  virtual int  constant_disp() const;   // usu. 0, may return Type::OffsetBot
  virtual int  base_position()  const;  // base edge position, or -1
  virtual int  index_position() const;  // index edge position, or -1

  // Access the TypeKlassPtr of operands with a base==RegI and disp==RegP
  // Only returns non-null value for i486.ad's indOffset32X 
  virtual const TypePtr *disp_as_type() const { return NULL; }

  // Return the label
  virtual Label *label() const;

  // Hash and compare over operands are currently identical
  virtual uint  hash() const;
  virtual uint  cmp( const MachOper &oper ) const;

  // Virtual clone, since I do not know how big the MachOper is.
  virtual MachOper *clone(Compile* C) const = 0;

  // Return ideal Type from simple operands.  Fail for complex operands.
  virtual const Type *type() const;

  // Set an integer offset if we have one, or error otherwise
  virtual void set_con( jint c0 ) { ShouldNotReachHere();  }

  // Return name of operand
  virtual const char    *Name() const { return "???";}
  virtual void dump_spec(outputStream *st) const; // Print per-operand info
};

//------------------------------MachNode---------------------------------------
// Base type for all machine specific nodes.  All node classes generated by the
// ADLC inherit from this class.
class MachNode : public Node {
public:
  MachNode(uint x) : Node(x), _num_opnds(0), _opnds(NULL) { init_class_id(Class_Mach); }
  MachNode() : Node((uint)0), _num_opnds(0), _opnds(NULL) {
    init_class_id(Class_Mach);
  }
  // Required boilerplate
  virtual uint size_of() const { return sizeof(MachNode); }
  virtual int  Opcode() const;          // Always equal to MachNode
  virtual uint rule() const = 0;        // Machine-specific opcode
  // Number of inputs which come before the first operand.
  // Generally at least 1, to skip the Control input
  virtual uint oper_input_base() const { return 1; }

  // Copy inputs and operands to new node of instruction.
  // Called from cisc_version() and short_branch_version().
  // !!!! The method's body is defined in ad_<arch>.cpp file. 
  void fill_new_machnode(MachNode *n, Compile* C) const;

  // Return an equivalent instruction using memory for cisc_operand position
  virtual MachNode *cisc_version(int offset, Compile* C);
  // Modify this instruction's register mask to use stack version for cisc_operand
  virtual void use_cisc_RegMask();

  // First index in _in[] corresponding to operand, or -1 if there is none
  int  operand_index(uint operand) const;

  // Register class input is expected in
  virtual const RegMask &in_RegMask(uint) const;

  // cisc-spillable instructions redefine for use by in_RegMask
  virtual const RegMask *cisc_RegMask() const { return NULL; }

  // If this instruction is a 2-address instruction, then return the
  // index of the input which must match the output.  Not nessecary 
  // for instructions which bind the input and output register to the
  // same singleton regiser (e.g., Intel IDIV which binds AX to be
  // both an input and an output).  It is nessecary when the input and
  // output have choices - but they must use the same choice.
  virtual uint two_adr( ) const { return 0; }

  // Array of complex operand pointers.  Each corresponds to zero or
  // more leafs.  Must be set by MachNode constructor to point to an
  // internal array of MachOpers.  The MachOper array is sized by
  // specific MachNodes described in the ADL.
  uint _num_opnds;
  MachOper **_opnds;
  uint  num_opnds() const { return _num_opnds; }

  // Emit bytes into cbuf
virtual void emit(PhaseRegAlloc*ra_)const;

//Return the alignment required
  // for this instruction (must be a power of 2)
  virtual int   alignment_required() const { return 1; }

  // Return the padding (in bytes) to be emitted before this
  // instruction to properly align it.
  virtual int   compute_padding(int current_offset) const { return 0; }

  // Return number of words used for double constants in this instruction
  virtual int   const_size() const { return 0; }

  // Hash and compare over operands.  Used to do GVN on machine Nodes.
  virtual uint  hash() const;
  virtual uint  cmp( const Node &n ) const;

  // Expand method for MachNode, replaces nodes representing pseudo
  // instructions with a set of nodes which represent real machine
  // instructions and compute the same value.
  virtual MachNode *Expand( State *, Node_List &proj_list ) { return this; }

  // Bottom_type call; value comes from operand0
  virtual const class Type *bottom_type() const { return _opnds[0]->type(); }
  virtual uint ideal_reg() const { const Type *t = _opnds[0]->type(); return t == TypeInt::CC ? Op_RegFlags : Matcher::base2reg[t->base()]; }

  // If this is a memory op, return the base pointer and fixed offset.
  // If there are no such, return NULL.  If there are multiple addresses
  // or the address is indeterminate (rare cases) then return (Node*)-1, 
  // which serves as node bottom.
  // If the offset is not statically determined, set it to Type::OffsetBot.
  // This method is free to ignore stack slots if that helps.
  #define TYPE_PTR_SENTINAL  ((const TypePtr*)-1)
  // Passing TYPE_PTR_SENTINAL as adr_type asks for computation of the adr_type if possible
  const Node* get_base_and_disp(intptr_t &offset, const TypePtr* &adr_type) const;

  // Helper for get_base_and_disp: find the base and index input nodes.
  // Returns the MachOper as determined by memory_operand(), for use, if 
  // needed by the caller. If (MachOper *)-1 is returned, base and index
  // are set to NodeSentinel. If (MachOper *) NULL is returned, base and
  // index are set to NULL.
  const MachOper* memory_inputs(Node* &base, Node* &index) const;

  // Helper for memory_inputs:  Which operand carries the necessary info?
  // By default, returns NULL, which means there is no such operand.
  // If it returns (MachOper*)-1, this means there are multiple memories.
  virtual const MachOper* memory_operand() const { return NULL; }

  // Call "get_base_and_disp" to decide which category of memory is used here.
  virtual const class TypePtr *adr_type() const;

  // Negate conditional branches.  Error for non-branch Nodes
  virtual void negate();

  // Apply peephole rule(s) to this instruction
  virtual MachNode *peephole( Block *block, int block_index, PhaseRegAlloc *ra_, int &deleted, Compile* C );

  // Top-level ideal Opcode matched
  virtual int ideal_Opcode()     const { return Op_Node; }

  // Set the branch inside jump MachNodes.  Error for non-branch Nodes.
virtual void label_set(Label*label);

  // Should we clone rather than spill this instruction?
  bool rematerialize() const;

  // Get the pipeline info
  static const Pipeline *pipeline_class();
  virtual const Pipeline *pipeline() const;

  virtual MachTypeNode       *is_MachType()       { return NULL; }
  virtual const MachLVBNode  *is_MachLVB() const  { return NULL; }

  virtual const char *Name() const = 0; // Machine-specific name
  virtual void dump_spec(outputStream *st) const; // Print per-node info
};

//------------------------------MachIdealNode----------------------------
// Machine specific versions of nodes that must be defined by user.
// These are not converted by matcher from ideal nodes to machine nodes
// but are inserted into the code by the compiler.
class MachIdealNode : public MachNode {
public:
  MachIdealNode( ) {}

  // Define the following defaults for non-matched machine nodes
  virtual uint oper_input_base() const { return 0; }
  virtual uint rule()            const { return 9999999; }
  virtual const class Type *bottom_type() const { return _opnds == NULL ? Type::CONTROL : MachNode::bottom_type(); }
};

//------------------------------MachTypeNode----------------------------------
// Machine Nodes that need to retain a known Type.
class MachTypeNode : public MachNode {
  virtual uint size_of() const { return sizeof(*this); } // Size is bigger
OopMap2*_oop_map;
public:
  MachTypeNode( ) : _oop_map(NULL) { }
  const Type *_bottom_type;

  virtual MachTypeNode *is_MachType() { return this; }
  virtual const class Type *bottom_type() const { return _bottom_type; }
  virtual const RegMask &in_RegMask(uint) const;
  virtual int base_derived_idx() const { ShouldNotReachHere(); return 0; }
  virtual void set_oopmap( OopMap2 *om ) { _oop_map = om; }
  virtual OopMap2 * get_oopmap( ) const { return _oop_map; }
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachBreakpointNode----------------------------
// Machine breakpoint or interrupt Node
class MachBreakpointNode : public MachIdealNode {
public:
  MachBreakpointNode( ) {}
virtual void emit(PhaseRegAlloc*ra_)const;

  virtual const char *Name() const { return "Breakpoint"; }
};

//------------------------------MachPrologNode--------------------------------
// Machine function Prolog Node
class MachPrologNode : public MachIdealNode {
public:
  MachPrologNode( ) {}
virtual void emit(PhaseRegAlloc*ra_)const;
void implementation(PhaseRegAlloc*ra_)const;

  virtual const char *Name() const { return "Prolog"; }
};

//------------------------------MachEpilogNode--------------------------------
// Machine function Epilog Node
class MachEpilogNode : public MachIdealNode {
public:
  MachEpilogNode(bool do_poll = false) : _do_polling(do_poll) {}
virtual void emit(PhaseRegAlloc*ra_)const;
  virtual const Pipeline *pipeline() const;

private:
  bool _do_polling;

public:
  bool do_polling() const { return _do_polling; }

  virtual const char *Name() const { return "Epilog"; }
};

//------------------------------MachNopNode-----------------------------------
// Machine function Nop Node
class MachNopNode : public MachIdealNode {
private:
  int _count;
public:
  MachNopNode( ) : _count(1) {}
  MachNopNode( int count ) : _count(count) {}
virtual void emit(PhaseRegAlloc*ra_)const;

  virtual const class Type *bottom_type() const { return Type::CONTROL; }

  virtual int ideal_Opcode() const { return Op_Con; } // bogus; see output.cpp
  virtual const Pipeline *pipeline() const;
  virtual const char *Name() const { return "Nop"; }
  virtual void dump_spec(outputStream *st) const { } // No per-operand info
};

//------------------------------MachNullNode-----------------------------------
// Machine function Null Node
class MachNullNode:public MachIdealNode{
public:
  MachNullNode( Node *root ) { add_req(root); }
  virtual void emit(PhaseRegAlloc *ra_) const;

virtual uint ideal_reg()const{return Op_RegP;}
virtual const class Type*bottom_type()const{return TypePtr::NULL_PTR;}
  const RegMask &out_RegMask() const { return *Matcher::idealreg2regmask[Op_RegP]; }
  virtual bool is_Con() const { return true; }
  virtual bool rematerialize() const { return true; }

virtual int ideal_Opcode()const{return Op_ConP;}
  virtual const Pipeline *pipeline() const;
virtual const char*Name()const{return"null";}
  virtual void dump_spec(outputStream *st) const { } // No per-operand info
};

//------------------------------MachSpillCopyNode------------------------------
// Machine SpillCopy Node.  Copies 1 or 2 words from any location to any
// location (stack or register).
class MachSpillCopyNode : public MachIdealNode {
  const RegMask *_in;           // RegMask for input
  const RegMask *_out;          // RegMask for output
  const Type *_type;
public:
  MachSpillCopyNode( Node *n, const RegMask &in, const RegMask &out ) : 
    MachIdealNode(), _in(&in), _out(&out), _type(n->bottom_type()) {
    init_class_id(Class_MachSpillCopy);
    init_flags(Flag_is_Copy);
    add_req(NULL);
    add_req(n);
  }
  virtual uint size_of() const { return sizeof(*this); } 
  void set_out_RegMask(const RegMask &out) { _out = &out; }
  void set_in_RegMask(const RegMask &in) { _in = &in; }
  virtual const RegMask &out_RegMask() const { return *_out; }
  virtual const RegMask &in_RegMask(uint) const { return *_in; }
  virtual const class Type *bottom_type() const { return _type; }
  virtual uint ideal_reg() const { return Matcher::base2reg[_type->base()]; }
  virtual uint oper_input_base() const { return 1; }
void implementation(PhaseRegAlloc*ra_)const;

virtual void emit(PhaseRegAlloc*ra_)const;

  virtual const char *Name() const { return "MachSpillCopy"; }
};

//------------------------------MachNullChkNode--------------------------------
// Machine-dependent null-pointer-check Node.  Points a real MachNode that is
// also some kind of memory op.  Turns the indicated MachNode into a
// conditional branch with good latency on the ptr-not-null path and awful
// latency on the pointer-is-null path.  

class MachNullCheckNode : public MachIdealNode {
public:
  const uint _vidx;             // Index of memop being tested
  MachNullCheckNode( Node *ctrl, Node *memop, uint vidx ) : MachIdealNode(), _vidx(vidx) {
    init_class_id(Class_MachNullCheck);
init_flags(Flag_is_Branch);
    add_req(ctrl);
    add_req(memop);
  }

  virtual void emit(PhaseRegAlloc *ra_) const;
  virtual bool pinned() const { return true; };
  virtual void negate() { }
  virtual const class Type *bottom_type() const { return TypeTuple::IFBOTH; }
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual const RegMask &in_RegMask(uint) const;
  virtual const RegMask &out_RegMask() const { return RegMask::Empty; }
  virtual const char *Name() const { return "NullCheck"; }
};

//------------------------------MachLVBNode-----------------------------------
// Azul LVB - Load Value Barrier.  Used to implement fast/good GC algorithms.
// Takes as input a recently loaded ref and the address is was loaded from.
// Returns the cleaned-up ref (may trap internally to do the fixup).  Original
// value at address is also fixed-up.  Inserted at the last moment between
// local and global scheduling - the rule is that no un-LVB'd value can cross
// a GC safepoint.  Since it is inserted so late we have to generate lots of
// junk v-calls normally produced by the ADLC.
class MachLVBNode:public MachIdealNode{
  virtual uint size_of() const { return sizeof(*this); } // Size is bigger
MachOper*_opr[3];
public:
  MachLVBNode( MachOper *dst, MachOper *adr, MachOper *tmp ) : MachIdealNode() {
    assert((UseLVBs && tmp != NULL) || (!UseLVBs && tmp == NULL),
"Improper use of Tmp MachOper");

_opnds=_opr;
    _opr[0] = dst;
    _opr[1] = adr;
    _opr[2] = tmp;
    _num_opnds = (UseLVBs ? 3:2);
  }

virtual uint ideal_reg()const{return Op_RegP;}
  virtual const Type *bottom_type() const { return in(1)->bottom_type(); }
  // Factory called between GCM and LCM to conditionally insert LVBs.
  static void insert( Node *ptr, GrowableArray<uint> &node_latency );
  virtual const MachLVBNode *is_MachLVB() const { return this; }
  virtual uint num_opnds()       const { return (UseLVBs ? 3:2); }
virtual uint oper_input_base()const{return 2;}
  virtual void emit(PhaseRegAlloc *ra_) const;
  virtual void negate() { }
  virtual const MachOper* memory_operand() const { return _opr[1]; }
virtual uint two_adr()const{return 1;}
  virtual const RegMask &in_RegMask(uint) const;
  virtual const RegMask &out_RegMask() const;
  virtual const RegMask &kill_RegMask() const; // Defined in the AD file
  virtual const Pipeline *pipeline() const;
virtual const char*Name()const{return"lvb";}
};


//------------------------------MachProjNode----------------------------------
// Machine-dependent Ideal projections (how is that for an oxymoron).  Really
// just MachNodes made by the Ideal world that replicate simple projections
// but with machine-dependent input & output register masks.  Generally
// produced as part of calling conventions.  Normally I make MachNodes as part
// of the Matcher process, but the Matcher is ill suited to issues involving
// frame handling, so frame handling is all done in the Ideal world with
// occasional callbacks to the machine model for important info.
class MachProjNode : public ProjNode {
public:
  MachProjNode( Node *multi, uint con, const RegMask &out, uint ideal_reg ) : ProjNode(multi,con), _rout(out), _ideal_reg(ideal_reg) {}
  RegMask _rout;
  const uint  _ideal_reg;
  enum projType {
    unmatched_proj = 0,         // Projs for Control, I/O, memory not matched
    fat_proj       = 999        // Projs killing many regs, defined by _rout
  };
  virtual int   Opcode() const;
  virtual const Type *bottom_type() const;
  virtual const TypePtr *adr_type() const;
  virtual const RegMask &in_RegMask(uint) const { return RegMask::Empty; }
  virtual const RegMask &out_RegMask() const { return _rout; }
  virtual uint  ideal_reg() const { return _ideal_reg; }
  // Need size_of() for virtual ProjNode::clone() 
  virtual uint  size_of() const { return sizeof(MachProjNode); }
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachIfNode-------------------------------------
// Machine-specific versions of IfNodes
class MachIfNode : public MachNode {
  virtual uint size_of() const { return sizeof(*this); } // Size is bigger
public:
  float _prob;                  // Probability branch goes either way
  float _fcnt;                  // Frequency counter
  MachIfNode() : MachNode() {
    init_class_id(Class_MachIf);
  }
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachReturnNode--------------------------------
// Machine-specific versions of subroutine returns
class MachReturnNode : public MachNode {
  virtual uint size_of() const; // Size is bigger
public:
  RegMask *_in_rms;             // Input register masks, set during allocation
  ReallocMark _nesting;         // assertion check for reallocations
  const TypePtr* _adr_type;     // memory effects of call or return
  MachReturnNode() : MachNode() {
    init_class_id(Class_MachReturn);
    _adr_type = TypePtr::BOTTOM; // the default: all of memory
  }

  void set_adr_type(const TypePtr* atp) { _adr_type = atp; }

  virtual const RegMask &in_RegMask(uint) const;
  virtual bool pinned() const { return true; };
  virtual const TypePtr *adr_type() const;
};

//------------------------------MachSafePointNode-----------------------------
// Machine-specific versions of safepoints
class MachSafePointNode : public MachReturnNode {
public:
OopMap2*_oop_map;//Array of OopMap info (8-bit char) for GC
  JVMState*       _jvms;        // Pointer to list of JVM State Objects
  uint            _jvmadj;      // Extra delta to jvms indexes (mach. args)
  CPData*         _cpd;         // Interesting profile data for printout
  bool            _extra_lock;  // Last lock in youngest JVMS is coarsened; unlock on deopt
OopMap2*oop_map()const{return _oop_map;}
void set_oop_map(OopMap2*om){_oop_map=om;}

  MachSafePointNode() : MachReturnNode(), _oop_map(NULL), _jvms(NULL), _jvmadj(0), _cpd(0), _extra_lock(false) {
    init_class_id(Class_MachSafePoint);
    init_flags(Flag_is_safepoint_node);
  }

  virtual JVMState* jvms() const { return _jvms; }
  void set_jvms(JVMState* s, CPData *cpd, bool extra_lock) {
    _jvms = s;
    _cpd = cpd;
    _extra_lock = extra_lock;
  }
  bool is_safepoint_node() const { return (flags() & Flag_is_safepoint_node) != 0; }
  virtual const Type    *bottom_type() const;

  virtual const RegMask &in_RegMask(uint) const;

  // Functionality from old debug nodes
  Node *returnadr() const { return in(TypeFunc::ReturnAdr); }
  Node *frameptr () const { return in(TypeFunc::FramePtr); }

  Node *local(const JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(_jvmadj + jvms->locoff() + idx);
  }
  Node *stack(const JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(_jvmadj + jvms->stkoff() + idx);
 }
  Node *monitor_obj(const JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(_jvmadj + jvms->monitor_obj_offset(idx));
  }
  void  set_local(const JVMState* jvms, uint idx, Node *c) {
    assert(verify_jvms(jvms), "jvms must match");
    set_req(_jvmadj + jvms->locoff() + idx, c);
  }
  void  set_stack(const JVMState* jvms, uint idx, Node *c) {
    assert(verify_jvms(jvms), "jvms must match");
    set_req(_jvmadj + jvms->stkoff() + idx, c);
  }
  void  set_monitor(const JVMState* jvms, uint idx, Node *c) {
    assert(verify_jvms(jvms), "jvms must match");
    set_req(_jvmadj + jvms->monoff() + idx, c);
  }
  void add_debug_here(const OopMap2 *omap) const;

  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachCallNode----------------------------------
// Machine-specific versions of subroutine calls
class MachCallNode : public MachSafePointNode {
protected:
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const = 0; // Size is bigger
public:
  const TypeFunc *_tf;        // Function type
  address      _entry_point;  // Address of the method being called
  uint         _argsize;      // Size of argument block on stack

  const TypeFunc* tf()        const { return _tf; }
  const address entry_point() const { return _entry_point; }
  uint argsize()              const { return _argsize; }

  void set_tf(const TypeFunc* tf) { _tf = tf; }
  void set_entry_point(address p) { _entry_point = p; }
  void set_argsize(int s)         { _argsize = s; }

  MachCallNode() : MachSafePointNode() {
    init_class_id(Class_MachCall);
    init_flags(Flag_is_Call);
  }

  virtual const Type *bottom_type() const;
  virtual bool  pinned() const { return false; }
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual const RegMask &in_RegMask(uint) const;

  bool return_value_is_used() const;
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachCallJavaNode------------------------------
// "Base" class for machine-specific versions of subroutine calls
class MachCallJavaNode : public MachCallNode {
protected:
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
  ciMethod* _method;             // Method being direct called
  MachCallJavaNode() : MachCallNode() {
    init_class_id(Class_MachCallJava);
  }
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachCallStaticJavaNode------------------------
// Machine-specific versions of monomorphic subroutine calls
class MachCallStaticJavaNode : public MachCallJavaNode {
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
  const char *_name;            // Runtime wrapper name
  MachCallStaticJavaNode() : MachCallJavaNode() {
    init_class_id(Class_MachCallStaticJava);
  }
  virtual const RegMask &in_RegMask(uint) const;
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachCallDynamicJavaNode------------------------
// Machine-specific versions of possibly megamorphic subroutine calls
class MachCallDynamicJavaNode : public MachCallJavaNode {
  virtual uint size_of() const; // Size is bigger
public:
  int _vtable_index;
  MachCallDynamicJavaNode() : MachCallJavaNode() {
    init_class_id(Class_MachCallDynamicJava);
  }
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachVMNode------------------------------------
// Machine-specific versions of subroutine calls
class MachCallVMNode:public MachCallNode{
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
  const char *_name;            // Printable name, if _method is NULL
MachCallVMNode():MachCallNode(){
init_class_id(Class_MachCallVM);
  }
  virtual void dump_spec(outputStream *st) const;
};

class MachCallRuntimeNode:public MachCallVMNode{
public:
  MachCallRuntimeNode() : MachCallVMNode() { init_class_id(Class_MachCallRuntime); }
};

class MachCallLeafNode:public MachCallVMNode{
public:
  MachCallLeafNode() : MachCallVMNode() { init_class_id(Class_MachCallLeaf); }
};

class MachAllocateNode: public MachCallVMNode {
public:
  MachAllocateNode() : MachCallVMNode() { init_class_id(Class_MachAllocate); }
};

//------------------------------MachHaltNode-----------------------------------
// Machine-specific versions of halt nodes
class MachHaltNode : public MachReturnNode {
public:
  const char *_msg;             // Reason for halt on this path
  virtual JVMState* jvms() const;
  virtual void dump_spec(outputStream *st) const;
};

//------------------------------MachTempNode-----------------------------------
// Node used by the adlc to construct inputs to represent temporary registers
class MachTempNode : public MachNode { 
private:
  MachOper *_opnd_array[1];

public:
  virtual const RegMask &out_RegMask() const { return *_opnds[0]->in_RegMask(0); }
  virtual uint rule() const { return 9999999; }
virtual void emit(PhaseRegAlloc*ra_)const{}

  MachTempNode(MachOper* oper) {
    init_class_id(Class_MachTemp);
    _num_opnds = 1;
    _opnds = _opnd_array;
    add_req(NULL);
    _opnds[0] = oper;
  }
  virtual uint size_of() const { return sizeof(MachTempNode); }
  virtual const char *Name() const { return "MachTemp";}
};


//------------------------------labelOper--------------------------------------
// Machine-independent version of label operand
class labelOper : public MachOper { 
private:
  virtual uint           num_edges() const { return 0; }
public:
  // Supported for fixed size branches
  Label* _label;                // Label for branch(es)
labelOper():_label(0){}
labelOper(Label*label):_label(label){}
labelOper(labelOper*l):_label(l->_label){}
  virtual MachOper *clone(Compile* C) const;
  virtual Label *label() const { return _label; }
  virtual uint           opcode() const;
  virtual uint           hash()   const;
  virtual uint           cmp( const MachOper &oper ) const;
  virtual const char    *Name()   const { return "Label";}
};


//------------------------------methodOper--------------------------------------
// Machine-independent version of method operand
class methodOper : public MachOper { 
private:
  virtual uint           num_edges() const { return 0; }
public:
methodOper(){}
  virtual MachOper *clone(Compile* C) const;
  virtual uint           opcode() const;
  virtual uint           hash()   const;
  virtual uint           cmp( const MachOper &oper ) const;
  virtual const char    *Name()   const { return "Method";}
};
#endif // MACHNODE_HPP
