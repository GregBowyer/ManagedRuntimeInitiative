/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef CALLNODE_HPP
#define CALLNODE_HPP


// Portions of code courtesy of Clifford Click

#include "bytecode.hpp"
#include "codeProfile.hpp"
#include "escape.hpp"
#include "opcodes.hpp"
#include "multnode.hpp"
#include "node.hpp"
#include "type.hpp"

// Optimization - Graph Style

class Chaitin;
class NamedCounter;
class MultiNode;
class  SafePointNode;
class   CallNode;
class     CallJavaNode;
class       CallStaticJavaNode;
class       CallDynamicJavaNode;
class     CallRuntimeNode;
class       CallLeafNode;
class         CallLeafNoFPNode;
class       AllocateNode;
class         AllocateArrayNode;
class       LockNode;
class       UnlockNode;
class JVMState;
class OopMap2;

//------------------------------StartNode--------------------------------------
// The method start node
class StartNode : public MultiNode {
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
  const TypeTuple *_domain;
  StartNode( Node *root, const TypeTuple *domain ) : MultiNode(2), _domain(domain) {
    init_class_id(Class_Start);
    init_flags(Flag_is_block_start);
    init_req(0,this);
    init_req(1,root);
  }
  virtual int Opcode() const;
  virtual bool pinned() const { return true; };
  virtual const Type *bottom_type() const;
  virtual const TypePtr *adr_type() const { return TypePtr::BOTTOM; }
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
virtual void calling_convention(BasicType*sig_bt,VReg::VR*parm_reg,uint length)const;
  virtual const RegMask &in_RegMask(uint) const;
  virtual Node *match( const ProjNode *proj, const Matcher *m );
  virtual uint ideal_reg() const { return 0; }
  virtual void  dump_spec(outputStream *st) const;
};

//------------------------------StartOSRNode-----------------------------------
// The method start node for on stack replacement code
class StartOSRNode : public StartNode {
public:
  StartOSRNode( Node *root, const TypeTuple *domain ) : StartNode(root, domain) {}
  virtual int   Opcode() const;
  static  const TypeTuple *osr_domain();
};


//------------------------------ParmNode---------------------------------------
// Incoming parameters
class ParmNode : public ProjNode {
  static const char * const names[TypeFunc::Parms+1];
public:
  ParmNode( StartNode *src, uint con ) : ProjNode(src,con) {}
  virtual int Opcode() const;
  virtual bool  is_CFG() const { return (_con == TypeFunc::Control); }
  virtual uint ideal_reg() const;
  virtual void dump_spec(outputStream *st) const;
};


//------------------------------ReturnNode-------------------------------------
// Return from subroutine node
class ReturnNode : public Node {
public:
  ReturnNode( uint edges, Node *cntrl, Node *i_o, Node *memory, Node *retadr, Node *frameptr );
  virtual int Opcode() const;
  virtual bool  is_CFG() const { return true; }
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
  virtual uint match_edge(uint idx) const;
#ifndef PRODUCT
  virtual void dump_req() const;
#endif
};


//------------------------------RethrowNode------------------------------------
// Rethrow of exception at call site.  Ends a procedure before rethrowing;
// ends the current basic block like a ReturnNode.  Restores registers and
// unwinds stack.  Rethrow happens in the caller's method.
class RethrowNode : public Node {
 public:
RethrowNode(Node*cntrl,Node*i_o,Node*memory,Node*frameptr,Node*ret_adr);
  virtual int Opcode() const;
  virtual bool  is_CFG() const { return true; }
  virtual uint hash() const { return NO_HASH; }  // CFG nodes do not hash
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual uint match_edge(uint idx) const;
  virtual uint ideal_reg() const { return NotAMachineReg; }
#ifndef PRODUCT
  virtual void dump_req() const;
#endif
};


//-------------------------------JVMState-------------------------------------
// A linked list of JVMState nodes captures the whole interpreter state,
// plus GC roots, for all active calls at some call site in this compilation
// unit.  (If there is no inlining, then the list has exactly one link.)
// This provides a way to map the optimized program back into the interpreter,
// or to let the GC mark the stack.
class JVMState : public ResourceObj {
private:
  JVMState*         _caller;    // List pointer for forming scope chains
uint _depth;//One more than caller depth, or one.
  uint              _locoff;    // Offset to locals in input edge mapping
  uint              _stkoff;    // Offset to stack in input edge mapping
  uint              _monoff;    // Offset to monitors in input edge mapping
  uint              _endoff;    // Offset to end of input edge mapping
  uint              _sp;        // Jave Expression Stack Pointer for this state
  int               _bci;       // Byte Code Index of this JVM point
  ciMethod*         _method;    // Method Pointer
  SafePointNode*    _map;       // Map node associated with this scope
public:
  friend class Compile;

  // Because JVMState objects live over the entire lifetime of the
  // Compile object, they are allocated into the comp_arena, which
  // does not get resource marked or reset during the compile process
  void *operator new( size_t x, Compile* C ) { return C->comp_arena()->Amalloc(x); }
  void operator delete( void * ) { } // fast deallocation

  // Create a new JVMState, ready for abstract interpretation.
  JVMState(ciMethod* method, JVMState* caller);
  JVMState(int stack_size);  // root state; has a null method

  // Access functions for the JVM
  uint              locoff() const { return _locoff; }
  uint              stkoff() const { return _stkoff; }
  uint              argoff() const { return _stkoff + _sp; }
  uint              monoff() const { return _monoff; }
  uint              endoff() const { return _endoff; }
  uint              oopoff() const { return debug_end(); }

  int            loc_size() const { return _stkoff - _locoff; }
  int            stk_size() const { return _monoff - _stkoff; }
  int            mon_size() const { return _endoff - _monoff; }

  bool        is_loc(uint i) const { return i >= _locoff && i < _stkoff; }
  bool        is_stk(uint i) const { return i >= _stkoff && i < _monoff; }
  bool        is_mon(uint i) const { return i >= _monoff && i < _endoff; }

  uint              sp()     const { return _sp; }
  int               bci()    const { return _bci; }
  bool          has_method() const { return _method != NULL; }
  ciMethod*         method() const { assert(has_method(), ""); return _method; }
  JVMState*         caller() const { return _caller; }
  SafePointNode*    map()    const { return _map; }
  uint              depth()  const { return _depth; }
  uint        debug_start()  const; // returns locoff of root caller
  uint        debug_end()    const; // returns endoff of self
  uint        debug_size()   const { return loc_size() + sp() + mon_size(); }
  uint        debug_depth()  const; // returns sum of debug_size values at all depths

  // Returns the JVM state at the desired depth (1 == root).
  JVMState* of_depth(int d) const;

  // Tells if two JVM states have the same call chain (depth, methods, & bcis).
  bool same_calls_as(const JVMState* that) const;

  // Monitors; Azul note: monitors are just the plain object being locked now, no 'box'
  int  nof_monitors()              const { return mon_size(); }
  int  monitor_depth()             const { return nof_monitors() + (caller() ? caller()->monitor_depth() : 0); }
int monitor_obj_offset(int idx)const{return monoff()+idx;}

  // Initialization functions for the JVM
  void              set_locoff(uint off) { _locoff = off; }
  void              set_stkoff(uint off) { _stkoff = off; }
  void              set_monoff(uint off) { _monoff = off; }
  void              set_endoff(uint off) { _endoff = off; }
  void              set_offsets(uint off) { _locoff = _stkoff = _monoff = _endoff = off; }
  void              set_map(SafePointNode *map) { _map = map; }
  void              set_sp(uint sp) { _sp = sp; }
  void              set_bci(int bci) { _bci = bci; }

  // Miscellaneous utility functions
  JVMState* clone_deep(Compile* C) const;    // recursively clones caller chain
  JVMState* clone_shallow(Compile* C) const; // retains uncloned caller
  Bytecodes::Code bc() const { return Bytecode_at(method()->code() + bci())->java_code(); }

  void      dump_spec(outputStream *st) const;
  void      dump_on(outputStream* st) const;
  void      dump() const {
    dump_on(tty);
  }
};

//------------------------------SafePointNode----------------------------------
// A SafePointNode is a subclass of a MultiNode for convenience (and
// potential code sharing) only - conceptually it is independent of
// the Node semantics.
class SafePointNode : public MultiNode {
  virtual uint           cmp( const Node &n ) const;
  virtual uint           size_of() const;       // Size is bigger

public:
  SafePointNode(uint edges, JVMState* jvms,
                // A plain safepoint advertises no memory effects (NULL):
const TypePtr*adr_type,
                CPData *cpd)
    : MultiNode( edges ),
      _jvms(jvms),
      _oop_map(NULL),
_adr_type(adr_type),
      _cpd(cpd), 
      _extra_lock(false)
  {
    init_class_id(Class_SafePoint);
  }
  
OopMap2*_oop_map;//Array of OopMap info (8-bit char) for GC
  JVMState* const _jvms;      // Pointer to list of JVM State objects
  const TypePtr*  _adr_type;  // What type of memory does this node produce?
  CPData*         _cpd;       // Profile data
  // If we coarsened a lock across a Safepoint, or a CallNew or CallNewArray
  // and we end up deopt'ing we need to know what to unlock.  This bit tells
  // us we added 2 extra edges - essentially a bogus monitor - that needs to
  // be unlocked on deopt.
  bool _extra_lock;

  // Many calls take *all* of memory as input,
  // but some produce a limited subset of that memory as output.
  // The adr_type reports the call's behavior as a store, not a load.

  virtual JVMState* jvms() const { return _jvms; }
  void set_jvms(JVMState* s, CPData *cpd) {
    *(JVMState**)&_jvms = s;  // override const attribute in the accessor
    _cpd = cpd;
  }
OopMap2*oop_map()const{return _oop_map;}
void set_oop_map(OopMap2*om){_oop_map=om;}

  // Functionality from old debug nodes which has changed
  Node *local(JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(jvms->locoff() + idx);
  }
  Node *stack(JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(jvms->stkoff() + idx);
  }
  Node *argument(JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(jvms->argoff() + idx);
  }
  Node *monitor_obj(JVMState* jvms, uint idx) const {
    assert(verify_jvms(jvms), "jvms must match");
    return in(jvms->monitor_obj_offset(idx));
  }

  void  set_local(JVMState* jvms, uint idx, Node *c);

  void  set_stack(JVMState* jvms, uint idx, Node *c) {
    assert(verify_jvms(jvms), "jvms must match");
    set_req(jvms->stkoff() + idx, c);
  }
  void  set_argument(JVMState* jvms, uint idx, Node *c) {
    assert(verify_jvms(jvms), "jvms must match");
    set_req(jvms->argoff() + idx, c);
  }
  void ensure_stack(JVMState* jvms, uint stk_size) {
    assert(verify_jvms(jvms), "jvms must match");
    int grow_by = (int)stk_size - (int)jvms->stk_size();
    if (grow_by > 0)  grow_stack(jvms, grow_by);
  }
  void grow_stack(JVMState* jvms, uint grow_by);
  // Handle monitor stack
  void push_monitor( Node *obj );
  void pop_monitor ();
  Node *peek_monitor_obj() const;

  // Access functions for the JVM
  Node *control  () const { return in(TypeFunc::Control  ); }
  Node *i_o      () const { return in(TypeFunc::I_O      ); }
  Node *memory   () const { return in(TypeFunc::Memory   ); }
  Node *returnadr() const { return in(TypeFunc::ReturnAdr); }
  Node *frameptr () const { return in(TypeFunc::FramePtr ); }

  void set_control  ( Node *c ) { set_req(TypeFunc::Control,c); }
  void set_i_o      ( Node *c ) { set_req(TypeFunc::I_O    ,c); }
  void set_memory   ( Node *c ) { set_req(TypeFunc::Memory ,c); }

  MergeMemNode* merged_memory() const {
    return in(TypeFunc::Memory)->as_MergeMem();
  }

  // The parser marks useless maps as dead when it's done with them:
  bool is_killed() { return in(TypeFunc::Control) == NULL; }

  // Exception states bubbling out of subgraphs such as inlined calls
  // are recorded here.  (There might be more than one, hence the "next".)
  // This feature is used only for safepoints which serve as "maps"
  // for JVM states during parsing, intrinsic expansion, etc.
  SafePointNode*         next_exception() const;
  void               set_next_exception(SafePointNode* n);
  bool                   has_exceptions() const { return next_exception() != NULL; }

  // Standard Node stuff
  virtual int            Opcode() const;
  virtual bool           pinned() const { return true; }
  virtual const Type    *Value( PhaseTransform *phase ) const;
  virtual const Type    *bottom_type() const { return Type::CONTROL; }
  virtual const TypePtr *adr_type() const { return _adr_type; }
  virtual Node          *Ideal(PhaseGVN *phase, bool can_reshape);
  virtual Node          *Identity( PhaseTransform *phase );
  virtual uint           ideal_reg() const { return 0; }
  virtual const RegMask &in_RegMask(uint) const;
  virtual const RegMask &out_RegMask() const;
  virtual uint           match_edge(uint idx) const;

  static  bool           needs_polling_address_input();

  Bytecodes::Code bc() const { return _jvms->bc(); }
  CPData_Null   *get_cpdn() const { return _cpd->as_Null  (bc()); }
  CPData_Invoke *get_cpdi() const { return _cpd->as_Invoke(bc()); }

  virtual void              dump_spec(outputStream *st) const;
};

//------------------------------CallNode---------------------------------------
// Call nodes now subsume the function of debug nodes at callsites, so they
// contain the functionality of a full scope chain of debug nodes.
class CallNode : public SafePointNode {
public:
  const TypeFunc *_tf;        // Function type
  address      _entry_point;  // Address of method being called
  PointsToNode::EscapeState _escape_state;
const bool _cloned_in_citypeflow;//Cloned in ciTypeFlow: the CPData counts are shared

CallNode(const TypeFunc*tf,address addr,const TypePtr*adr_type,
CPData*cpd,bool cloned_in_citypeflow)
:SafePointNode(tf->domain()->cnt(),NULL,adr_type,cpd),
      _tf(tf),
      _entry_point(addr),
      _cloned_in_citypeflow(cloned_in_citypeflow)
  {
    init_class_id(Class_Call);
    init_flags(Flag_is_Call);
    _escape_state = PointsToNode::UnknownEscape;
  }

  const TypeFunc* tf()        const { return _tf; }
  const address entry_point() const { return _entry_point; }

  void set_tf(const TypeFunc* tf) { _tf = tf; }
  void set_entry_point(address p) { _entry_point = p; }

  virtual const Type *bottom_type() const;
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Identity( PhaseTransform *phase ) { return this; }
  virtual uint        cmp( const Node &n ) const;
  virtual uint        size_of() const = 0;
virtual void calling_convention(BasicType*sig_bt,VReg::VR*parm_regs,uint argcnt)const;
  virtual Node       *match( const ProjNode *proj, const Matcher *m );
  virtual uint        ideal_reg() const { return NotAMachineReg; }
  // Are we guaranteed that this node is a safepoint?  Not true for leaf calls and
  // for some macro nodes whose expansion does not have a safepoint on the fast path.
  virtual bool        guaranteed_safepoint()  { return true; }
  // For macro nodes, the JVMState gets modified during expansion, so when cloning
  // the node the JVMState must be cloned.
  virtual void        clone_jvms() { }   // default is not to clone

  virtual uint match_edge(uint idx) const;

  virtual void        dump_req()  const;
  virtual void        dump_spec(outputStream *st) const;
};

//------------------------------CallJavaNode-----------------------------------
// Make a static or dynamic subroutine call node using Java calling
// convention.  (The "Java" calling convention is the compiler's calling
// convention, as opposed to the interpreter's or that of native C.)
class CallJavaNode : public CallNode {
protected:
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger

  ciMethod* _method;            // Method being direct called
public:
  const int       _bci;         // Byte Code Index of call byte code
CallJavaNode(const TypeFunc*tf,address addr,ciMethod*method,int bci,
CPData*cpd,bool cloned_in_citypeflow)
:CallNode(tf,addr,TypePtr::BOTTOM,cpd,cloned_in_citypeflow),
_method(method),_bci(bci)
  {
    init_class_id(Class_CallJava);
  }

  virtual int   Opcode() const = 0;
  ciMethod* method() const                { return _method; }
  void  set_method(ciMethod *m)           { _method = m; }

  virtual void  dump_spec(outputStream *st) const;
};

//------------------------------CallStaticJavaNode-----------------------------
// Make a direct subroutine call using Java calling convention (for static
// calls and optimized virtual calls, plus calls to wrappers for run-time
// routines)
class CallStaticJavaNode : public CallJavaNode {
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
CallStaticJavaNode(const TypeFunc*tf,address addr,ciMethod*method,int bci,
CPData*cpd,bool cloned_in_citypeflow)
:CallJavaNode(tf,addr,method,bci,cpd,cloned_in_citypeflow),_name(NULL){
    init_class_id(Class_CallStaticJava);
  }
  CallStaticJavaNode(const TypeFunc* tf, address addr, const char* name, int bci,
const TypePtr*adr_type,
                     CPData *cpd, bool cloned_in_citypeflow)
    : CallJavaNode(tf, addr, NULL, bci, cpd, cloned_in_citypeflow), _name(name) {
    init_class_id(Class_CallStaticJava);
    // This node calls a runtime stub, which often has narrow memory effects.
    _adr_type = adr_type;
  }  
  const char *_name;            // Runtime wrapper name

  // If this is an uncommon trap, return the request code, else zero.
  int uncommon_trap_request() const;
  static int extract_uncommon_trap_request(const Node* call);

  virtual int         Opcode() const;
  virtual void        dump_spec(outputStream *st) const;
};

//------------------------------CallDynamicJavaNode----------------------------
// Make a dispatched call using Java calling convention.
class CallDynamicJavaNode : public CallJavaNode {
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
CallDynamicJavaNode(const TypeFunc*tf,address addr,ciMethod*method,int vtable_index,int bci,CPData*cpd,bool cloned_in_citypeflow):CallJavaNode(tf,addr,method,bci,cpd,cloned_in_citypeflow),_vtable_index(vtable_index){
    init_class_id(Class_CallDynamicJava);
  }

  int _vtable_index;
  virtual int   Opcode() const;
  virtual void  dump_spec(outputStream *st) const;
};

//------------------------------CallRuntimeNode--------------------------------
// Make a direct subroutine call node into compiled C++ code.
class CallRuntimeNode : public CallNode {
  virtual uint cmp( const Node &n ) const;
  virtual uint size_of() const; // Size is bigger
public:
  CallRuntimeNode(const TypeFunc* tf, address addr, const char* name,
const TypePtr*adr_type,
                  CPData *cpd, bool cloned_in_citypeflow)
    : CallNode(tf, addr, adr_type, cpd,cloned_in_citypeflow),
      _name(name)
  {
    init_class_id(Class_CallRuntime);
  }

  const char *_name;            // Printable name, if _method is NULL
  virtual int   Opcode() const;
virtual void calling_convention(BasicType*sig_bt,VReg::VR*parm_regs,uint argcnt)const;

  virtual void  dump_spec(outputStream *st) const;
};

//------------------------------CallLeafNode-----------------------------------
// Make a direct subroutine call node into compiled C++ code, without
// setting last_Java_pc or last_Java_sp.  No safepoints allowed.
class CallLeafNode : public CallRuntimeNode {
public:
  CallLeafNode(const TypeFunc* tf, address addr, const char* name,
const TypePtr*adr_type,
               CPData *cpd, bool cloned_in_citypeflow)
    : CallRuntimeNode(tf, addr, name, adr_type,cpd,cloned_in_citypeflow)
  {
    init_class_id(Class_CallLeaf);
  }
  virtual int   Opcode() const;
  virtual bool        guaranteed_safepoint()  { return false; }
  virtual void  dump_spec(outputStream *st) const;
};

//------------------------------CallLeafNoFPNode-------------------------------
// CallLeafNode, not using floating point or using it in the same manner as
// the generated code
class CallLeafNoFPNode : public CallLeafNode {
public:
  CallLeafNoFPNode(const TypeFunc* tf, address addr, const char* name,
const TypePtr*adr_type,
                   CPData *cpd, bool cloned_in_citypeflow)
    : CallLeafNode(tf, addr, name, adr_type, cpd,cloned_in_citypeflow)
  {
  }
  virtual int   Opcode() const;
};


//------------------------------AllocateNode-----------------------------------
// High-level memory allocation
//
// Allocate a new object.  KID is passed in as Parm1 and bytesize in Parm1.
// The result type is passed in and is used to build the signature (so the
// returned value has the correct casted type).  This call can GC & deoptimize
// and needs full debug info.  It can throw OutOfMemory and other asynchronous
// exceptions.  The CLZ prefetch pipeline will be setup after allocation, and
// the memory will be coherently fenced - which means the array length store
// is done in the template before the fencing.  The call only creates new
// memory and does not modify any memory, but it reads all of old memory for
// the Safepoint.  The returned memory is a fully-formed pre-zero'd object (I
// may relax this last point and expose the zero'ing so, e.g., arraycopy into
// all of a new array can skip the zero'ing for short arrays).
//
// This call will encode as a short allocation template.  The template may
// include a place for an SBA-style frame hint or seperate call targets may
// encode the hint.  The template may be inlined or out-of-lined (shared).
// For small fixed-size objects, the size may be wired in and the template
// shorter by an op; for variable size and large objects the size will be
// passed in.
class AllocateNode:public CallRuntimeNode{
public:
  enum {
Thr=TypeFunc::Parms+0,
KID=TypeFunc::Parms+1,
    AllocSize   = TypeFunc::Parms + 2,
    sizeHalf    = TypeFunc::Parms + 3,
    ExtraSlow   = TypeFunc::Parms + 4,
    ALength     = TypeFunc::Parms + 5,
    EKID        = TypeFunc::Parms + 6
  };

  AllocateNode( Node *thr, Node *kid_node, Node *size_in_bytes, Node *extra_slow, const TypeOopPtr *oop_type, const TypeFunc *tf );
  virtual int Opcode() const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
  static address     choose_starting_allocation_routine(const TypeOopPtr *oop_type);
  static const char *choose_starting_allocation_name   (const TypeOopPtr *oop_type);

  // Pattern-match a possible usage of AllocateNode.
  // Return null if no allocation is recognized.
  // The operand is the pointer produced by the (possible) allocation.
  // It must be a projection of the Allocate or its subsequent CastPP.
  // (Note:  This function is defined in file graphKit.cpp, near
  // GraphKit::new_instance/new_array, whose output it recognizes.)
  // The 'ptr' may not have an offset unless the 'offset' argument is given.
  static AllocateNode* Ideal_allocation(Node* ptr, PhaseTransform* phase);

  // Fancy version which uses AddPNode::Ideal_base_and_offset to strip
  // an offset, which is reported back to the caller.
  // (Note:  AllocateNode::Ideal_allocation is defined in graphKit.cpp.)
  static AllocateNode* Ideal_allocation(Node* ptr, PhaseTransform* phase,
                                        intptr_t& offset);

  // Dig the klass operand out of a (possible) allocation site.
  static Node* Ideal_klass(Node* ptr, PhaseTransform* phase) {
    AllocateNode* allo = Ideal_allocation(ptr, phase);
return(allo==NULL)?NULL:allo->in(KID);//actually it is the kid
  }

  // Conservatively small estimate of offset of first non-header byte.
  int minimum_header_size() {
    return is_AllocateArray() ? sizeof(arrayOopDesc) : sizeof(oopDesc);
  }

};

class AllocateArrayNode : public AllocateNode {
public:
  AllocateArrayNode( Node *thr, Node *kid_node, Node *size_in_bytes, Node *extra_slow, const TypeOopPtr *oop_type, Node *length, Node *ekid );
  virtual int Opcode() const;

  // Pattern-match a possible usage of AllocateArrayNode.
  // Return null if no allocation is recognized.
  static AllocateArrayNode* Ideal_array_allocation(Node* ptr, PhaseTransform* phase) {
    AllocateNode* allo = Ideal_allocation(ptr, phase);
    return (allo == NULL || !allo->is_AllocateArray())
           ? NULL : allo->as_AllocateArray();
  }

  // Dig the length operand out of a (possible) array allocation site.
  static Node* Ideal_length(Node* ptr, PhaseTransform* phase) {
    AllocateArrayNode* allo = Ideal_array_allocation(ptr, phase);
    return (allo == NULL) ? NULL : allo->in(AllocateNode::ALength);
  }
};

//------------------------------LockNode---------------------------------------
// Call locking millicode
class LockNode:public CallStaticJavaNode{
public:

static const TypeFunc*lock_type();

  LockNode( Compile *C, const TypeFunc *tf );
  virtual int Opcode() const;
  virtual const Type *Value( PhaseTransform *phase ) const;
  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape);
};

//------------------------------UnlockNode-------------------------------------
// Call unlocking millicode.  This call cannot block per-se, nor does it
// allow GC, but it IS a memory fence.
class UnlockNode:public CallLeafNode{
public:
  UnlockNode( Compile *C, const TypeFunc *tf );
  virtual int Opcode() const;
};

#endif // CALLNODE_HPP
