// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.
#ifndef COMMONASM_HPP
#define COMMONASM_HPP

#include "bitMap.hpp"
#include "codeBlob.hpp"
#include "handles.hpp"
#include "growableArray.hpp"
#include "ostream.hpp"
#include "pcMap.hpp"
#include "register_pd.hpp"
#include "vreg.hpp"

class CommonAsm;
class DebugInfoBuilder;
class DebugMap;
class DebugScopeBuilder;
class Label;
class OopClosure;
class OopMap2;
class OopMapBuilder;
class PC2PCMapBuilder;
class RegMap;
class ciConstant;
class ciInstanceKlass;
class ciMethod;
class outputStream;
class pcMap;

// --- PCMapBuilder
// Map PC-offsets (relative to the start of the CodeBlob) to 'stuff'.
// Actually, collect the pc-offsets & the 'stuff' in a nice-to-collect manner.
// Then on request present a dense/compact pcMap.  This annoying 2-stage setup
// is because we expect to need a lot of pcMaps, with a lot of variable-sized
// structures (variable-per-map but fixed within a map) - and we're trying to
// save some space (and support fast-lookup).  In all cases, the PC's are
// relative-pc's so we can move the CodeBlob without messing around here.
class PCMapBuilder:public ResourceObj{
  // 1 for stuffs that fit in a word, larger when a resource array is used to
  // form a larger entry
  const int _words_per_stuff;
  // Compute the number of bits needed for each 'stuff'.  Example: small
  // oop-maps might need only 16 or 32 bits-per-mapping, while pointers will
  // need 64 bits-per-mapping.  Used by build_pcMap.
  int bits_per_stuff() const;
  // Minor speedup for inserting lots of bits at the same rel_pc
  int _cached_idx;
protected:
GrowableArray<intptr_t>*_rel_pcs;
GrowableArray<intptr_t>*_stuff;
  // Add a pc-to-stuff mapping.  Dup's not allowed.
  void add_mapping( int rel_pc, intptr_t stuff );
  // Add a pc-to-bit mapping.  Dup PC's ARE allowed & expected.  Dup bits are not allowed.
  void add_bit( int rel_pc, int bit );
  // Override to print
  virtual void stuff_print_on( outputStream *st, intptr_t stuff ) const = 0;
  // Map rel_pc to some other rel_pc.  Used e.g. for NPE mappings.
  inline void add_mapping( int rel_pc, int rel_pc2 ) { add_mapping(rel_pc, (intptr_t)rel_pc2); }
  // Map rel_pc to some pointer.
  inline void add_mapping( int rel_pc, void *stuff ) { add_mapping(rel_pc, (intptr_t)stuff  ); }
  // Use the mapping
  intptr_t get_mapping( int rel_pc ) const;

  // Build a pcMap.  Empties the contents of this PCMapBuilder.
  pcMap *build_pcMap( );

public:
  const char *const _name;
  inline PCMapBuilder(const char *name)                      : _cached_idx(0), _rel_pcs(NULL), _stuff(NULL), _name(name), _words_per_stuff (1) { }
  inline PCMapBuilder(const char *name, int words_per_stuff) : _cached_idx(0), _rel_pcs(NULL), _stuff(NULL), _name(name), _words_per_stuff (words_per_stuff) {
assert(words_per_stuff>=1,"not possible");
  }

  // Hand out the rel_pc list to the CommonAsm to do relocation
  GrowableArray<intptr_t> *rel_pcs() const { return _rel_pcs; }
  GrowableArray<intptr_t> *stuffs () const { return _stuff; }
  // Return the relpc of the last map added.  Used to fetch oopmap addresses
  // during stub generation
  int last_relpc() const { return _rel_pcs->at(_rel_pcs->length()-1); }
  // Print 'stuff' with no newlines
  void print( const CodeBlob *blob, outputStream *st ) const;
};

// --- RegMap
// Compressed form of a OopMapBuilder - a pcMap that knows it's holding register maps.
// Used for OopMaps.
class RegMap:public CHeapObj{
  const char *const _name;
  const pcMap *const _regs;
public:
  RegMap( const char *name, const pcMap *regs ) : _name(name), _regs(regs) { }

  // Get the unique sole oop in this oopmap
  VOopReg::VR get_sole_oop( int rel_pc ) const;
  // Do a OopClosure across any oops in this frame at this rel_pc
  void do_closure( int rel_pc, frame fr, OopClosure *f ) const;
  // Test one register for being oop-ish
  bool is_oop( int rel_pc, VOopReg::VR vreg ) const;

  // Print all the register maps
  void print( const CodeBlob *, outputStream *st ) const;
  // Print just one register map
  void print( int rel_pc, outputStream *st ) const;
  int size() const { return _regs->size(); }
};

// --- RegMapBuilder
// A collection of relative_pc -to- register-set mappings.

// Example w/OopMap.  Add an OopMap at this PC.  The OopMap is defined by a
// bitmap.  The low bits map 1-to-1 with the registers.  The upper bits
// refer to stack-slots and are offset'd by the number of registers, then
// scaled by 8.  Example: assume REG_OOP_COUNT is 16 (ala X86-64) and RAX, RBX
// and stk+8 and stk+24 are all holding oops.  Then we'd call add_oopmap
// like this:
//        add_mapping( rel_pc, RAX );
//        add_mapping( rel_pc, RBX );
//        add_mapping( rel_pc, VOopReg::stk2reg( 8) );
//        add_mapping( rel_pc, VOopReg::stk2reg(24) );
// and our constructed bitmap would look like this:
//        1<<0 | 1<<3 | 1<<(1+16) | 1<<(3+16) == 0x0a0009
class RegMapBuilder : public PCMapBuilder {
  // Print 'stuff' with no newlines
  virtual void stuff_print_on( outputStream *st, intptr_t stuff ) const;
public:
  RegMapBuilder(const char *name, int max_regs) : PCMapBuilder(name, (max_regs+BitsPerWord-1)/BitsPerWord) {}
  
  // Map a rel_pc to some VOopReg::VR (register location).
  // Used e.g. for OopMaps or LockMaps.
  // Made public here and wraps the private version.
  void add_mapping( int rel_pc, VOopReg::VR reg ) { PCMapBuilder::add_bit(rel_pc,(int)reg); }
  void add_empty( int rel_pc ) { PCMapBuilder::add_mapping(rel_pc,0); }
  
  // Build a RegMap.  Empties the contents of this RegMapBuilder.
  const RegMap *build_regMap() {
    pcMap *regs = build_pcMap();
if(regs==NULL)return NULL;
    return new RegMap(_name,regs);
  }
};

// --- OopMapBuilder
struct  OopMapBuilder : public RegMapBuilder {  OopMapBuilder(int max_regs=BitsPerWord) : RegMapBuilder( "OopMaps", max_regs) {} };

// --- OopMap2
// Like an OopMap in the basic ABI, but name changed to force all instances to
// be updated.  Really just collects a set of VOopReg::VR locations for
// later handing to a OopMapBuilder.  Allows building up an OopMap without
// knowing the PC where it goes (yet).
class OopMap2:public ResourceObj{
  friend class CommonAsm;
  friend class MacroAssembler;
BitMap _data;
public:
  GrowableArray<VReg::VR> _base_derived_pairs; // A array of pairs 
  GrowableArray<VReg::VR> _callee_save_pairs;  // A array of pairs 
  OopMap2() : _data(63), _base_derived_pairs() { _data.clear(); }
  inline void add( VOopReg::VR voreg ) { if( (uint)voreg >= _data.size() ) _data.resize(voreg<<1); _data.set_bit(voreg); }
  bool at( VOopReg::VR voreg ) const { return (uint)voreg < _data.size() ? _data.at(voreg) : 0; }
  void print( outputStream *st );
  bool equals( const OopMap2 *om ) const { return _data.is_same(om->_data); }
  void add_derived_oop( VReg::VR base, VReg::VR derived );
  void add_callee_save_pair( VReg::VR src, VReg::VR dst );
};

//------------------------------DebugScopeValue------------------------------------------
// Naming convention for values appearing in a debug scope.
// The value encodes:
// - registers - values held on the stack that are either 4 or 8byte quantities,
//               the notion of register comes from VReg
// - constants - literal constants indexes into a debug map, the oop bit shows
//               that an extra indirection is needed through the oop table to
//               get the value
// - a special invalid entry for values that aren't live
// MSB                                    LSB
// [Const/Reg] [4/8byte or int/oop] [Payload]
// < Type    > < Kind             > < Value >
// DebugScopeValues fit in 16bits.
class DebugScopeValue VALUE_OBJ_CLASS_SPEC{
  enum Shift {
    type = 15,
    kind = 14,
    value = 0
  };
  enum Mask {
    type_mask = 1 << type,
    kind_mask = 1 << kind,
    value_mask = (1 << kind) - 1
  };
public:
  enum Name {
    Bad = -1
  };
  static bool is_valid(DebugScopeValue::Name v) { return v != Bad; }
  static bool is_vreg(DebugScopeValue::Name v) { return is_valid(v) && ((v & type_mask) == 0); }
  static bool is_const(DebugScopeValue::Name v) { return is_valid(v) && ((v & type_mask) != 0); }

  static DebugScopeValue::Name from_vreg( VReg::VR r, bool is8byte ) {
    assert0( r > VReg::Bad && (int)r < (int)value_mask );
    return Name((0 << type) | ((is8byte ? 1 : 0) << kind) | (r & value_mask));
  }
  static VReg::VR to_vreg( DebugScopeValue::Name v ) {
    assert0( is_vreg(v) );
    return VReg::VR(v & value_mask);
  }
  static bool is_vreg_8byte(DebugScopeValue::Name v) {
    assert0( is_vreg(v) );
    return (v & kind_mask) != 0;
  }

  static DebugScopeValue::Name from_index( int index, bool is_oop ) {
    assert0( index >= 0 && index < value_mask );
    return Name((1 << type) | ((is_oop ? 1 : 0) << kind) | (index & value_mask));
  }
  static int to_index( DebugScopeValue::Name v ) {
    assert0( is_const(v) );
    return v & value_mask;
  }
  static bool is_const_oop(DebugScopeValue::Name v) {
    assert0( is_const(v) );
    return (v & kind_mask) != 0;
  }
};

// --- DebugScope
// Compressed form of a DebugScopeBuilder - a tower of inlined call sites with
// JVM state mappings at each inline layer.
class DebugScope:public CHeapObj{
  const DebugScopeValue::Name *const _regs; // List of locations for each local/stack/lock.
  const DebugScope *const _caller; // if null this is a root debug, else it is inlined into caller
  const int _mid;               // Method object-id
const int _bci;//my bci (NOT my caller's bci)
  const uint _stk;              // stack depth
  const uint _maxlocks;         // max locks (so far).
  const uint _callee_save_pairs;// List of callee-save src/dst pairs
  const uint _base_derived_pairs;// List of base/derived pairs
  const bool _extra_lock;       // C2 coarsened the last lock across this deopt-point
  const bool _reexecute;        // The debug info is before (vs after) the _bci
  const bool _inline_cache;     // true if this is a leaf-debug and the call-site is an inline-cache
  const PC2PCMap *_bci2pc_map;  // Holds mappings from exception-handler-bcis to JIT'd handlers
public:

  DebugScope( const DebugScope *caller, int mid, int bci, uint stk, uint maxlocks,
              uint callee_save_pairs, uint base_derived_pairs, 
              bool extra_lock, bool reexecute, bool inline_cache,
              DebugScopeValue::Name *regs, const PC2PCMap *bci2pc_map ) :
  _regs(regs), _caller(caller), _mid(mid), _bci(bci), _stk(stk), _maxlocks(maxlocks),
  _callee_save_pairs(callee_save_pairs), 
  _base_derived_pairs(base_derived_pairs),
  _extra_lock(extra_lock), _reexecute(reexecute),
  _inline_cache(inline_cache), _bci2pc_map(bci2pc_map) {
  }
  const DebugScope *caller() const { return _caller; }
methodRef method()const{
    objectRef o = CodeCacheOopTable::getOopAt(_mid);
    assert0( o.as_oop()->is_method() );
    return *(methodRef*)&o;
  }
  int bci() const { return _bci; }
  bool should_reexecute() const { return _reexecute; }
  bool is_extra_lock() const { return _extra_lock; }
  bool is_inline_cache() const { return _inline_cache; }
  int find_handler( int bci ) const; // find an exception-handler rel_pc for this bci
  // Count locked instances of this oop
  int count_locks ( const DebugMap *dm, const frame fr, const oop o ) const;
  // Allow iteration over all the locked oops & stack elements.
  // Required for deopt & various reflective inspections.
  uint numlocals() const { return method().as_methodOop()->max_locals(); }
  uint numstk   () const { return _stk; }
  uint numlocks () const { return _maxlocks; }
  DebugScopeValue::Name get_local( unsigned int num ) const;
  DebugScopeValue::Name get_expr ( unsigned int num ) const;
  DebugScopeValue::Name get_lock ( unsigned int num ) const;
  void gc_base_derived( frame fr, OopClosure *f ) const;
  // Restore callee-save registers using the debug info, into a call_VM_compiled frame.
  void restore_callee_saves(frame src, frame dst) const;

  void print( outputStream *st ) const { print( NULL, st); }
  void print( const DebugMap *dm, outputStream *st ) const;
  void print_value( DebugScopeValue::Name rname, const DebugMap *dm, outputStream *st ) const;
void print_inline_tree(outputStream*st)const;//short printout in tree format
  void print_compiler_info( VReg::VR vreg, char *buf ) const;
};

// --- DebugMap
// Compressed form of a DebugInfoBuilder - a pcMap that knows it's holding debug info.
class DebugMap:public CHeapObj{
  const pcMap *const _info;
  const intptr_t *const _cons;
  const int _cons_len;
public:
  DebugMap( const pcMap *info, int cons_len, const intptr_t *cons ) : _info(info), _cons_len(cons_len), _cons(cons) { }
  const DebugScope *get(int rel_pc) const { return (DebugScope*)(_info->get(rel_pc)); }
  int tablesize() const        { return _info->_maxidx; }
  int next_idx(int idx) const  { return _info->next_idx(idx); }
  int get_relpc(int idx) const { return _info->get_relpc(idx); }
  int find_slot(int rel_pc) const { return _info->find_slot(rel_pc); }
  // Support for constants in debug info
  intptr_t get_value( const DebugScopeValue::Name, const frame fr ) const;
  // Print just one debug info
  void print( int rel_pc, outputStream *st ) const;
  // Print them all
void print(outputStream*st)const;
  int size_in_bytes() const { return sizeof(*this)+_info->size()+(sizeof(intptr_t)*_cons_len); }
  void clean_inline_caches(const CodeBlob *cb) const;
#ifdef ASSERT
  void verify_value( DebugScopeValue::Name vreg ) const;
  void verify() const;
#endif
};

// --- DebugInfoBuilder
// Collect "debug info" - a mapping from the Java Virtual Machine state to
// values; "values" are either in machine Registers or on the stack, or are
// constants or (very commonly) dead.  So a Map from (Java Locals 0-N, Java
// Stack 0-N, Locks) to (VReg::VR, Constants, Dead).
class DebugInfoBuilder : public PCMapBuilder {
  void stuff_print_on( outputStream *st, intptr_t stuff ) const;
public:
  enum JVM_Part { JLocal, JStack, JLock }; // parts of the JVM state named in debug info
  DebugInfoBuilder() : PCMapBuilder("DebugInfo") { }
  void add_dbg( int rel_pc, const DebugScopeBuilder *dbgscope ) { add_mapping(rel_pc,(void*)dbgscope); }
  DebugScopeBuilder *get(int rel_pc) const { return (DebugScopeBuilder*)PCMapBuilder::get_mapping(rel_pc); }

  // Build a compressed-form of this DebugInfoBuilder.  Empties this struct;
  // all DebugScopeBuilders "die" after this (but they are reclaimed using the
  // ResourceObj mechanism).
  const DebugMap *build_debugMap(CommonAsm *_asm);
};

// --- DebugScopeBuilder
// All the info needed for a single inlining call-site.  Includes a parent
// pointer to find the debug info for the caller.  This is the "fluffy"
// uncompressed version and is directly build by the compilers and passed to
// the DebugInfoBuilder.  It gets compressed when the DebugInfoBuilder is told
// to "bake" the data structure, and gets uncompressed for e.g. deoptimization
// or debug printing of compiled CodeBlobs.
class DebugScopeBuilder:public ResourceObj{
  const DebugScope * _compressed;// Compressed form of this guy
  DebugScopeBuilder *const _caller;    // if null this is a root debug, else it is inlined into caller

  // info on this method
  const int _objectId;
  const int _max_locals;
  const int _max_stack;         // max stack for method
  const uint _maxlocks;         // max locks (so far).
  uint _maxstks;                // max stacks used (so far)
  uint _callee_save_pairs;      // List of callee-save src/dst pairs
const int _bci;//my bci (NOT my caller's bci)
  bool _extra_lock;             // True if C2 coarsened a lock across this deopt site
  bool _reexecute;              // The debug info is before (vs after) the _bci
  bool _inline_cache;           // True if this is an inline-cache site (must also be a leaf-debug)
  VReg::VR *_regs;           // List of locations for each local/stack/lock.  Might be -1 for dead, -2 for non-oop const, -3 for oop table idx const
  intptr_t    *_cons;           // Constants or oop table indexes
  BitMap       _8byte;          // Is the location 4 or 8 byte
  const GrowableArray<VReg::VR> *_base_derived_pairs;
  // A mapping from exception-bcis to handler Labels.  Used when the runtime
  // exception lookup determines that the handler@bci should be used to handle
  // this exception; the runtime will vector to the corresponding JIT'd handler.
GrowableArray<int>*_ex_bcis;
  GrowableArray<const Label *> *_ex_labels;

  friend class DebugMap;
  DebugScopeValue::Name compress_reg( int index, GrowableArray<intptr_t> *shared_cons );   // Translate from builder to debug scope encoding using shared constants
  void make_compressed( CommonAsm *_asm, GrowableArray<intptr_t> *shared_cons ); // Make the compressed form
public:
  DebugScopeBuilder( DebugScopeBuilder *caller, 
		     const int objectId, 
                     const int max_locals,
                     const int max_stack,
                     int maxlocks,
		     int bci );

DebugScopeBuilder*caller()const{return _caller;}
methodRef method()const{
    objectRef o = CodeCacheOopTable::getOopAt(_objectId);
    assert0( o.as_oop()->is_method() );
    return *(methodRef*)&o;
  }
  void add_empty( DebugInfoBuilder::JVM_Part part, uint idx );
  void add_vreg( DebugInfoBuilder::JVM_Part part, uint idx, VReg::VR vreg, bool is8bytes );
  void add_const_int( DebugInfoBuilder::JVM_Part part, uint idx, intptr_t val, bool is_oop=false );
  void add_const_oop( DebugInfoBuilder::JVM_Part part, uint idx, int oop_tbl_idx ) {
    assert0( CodeCacheOopTable::is_valid_index(oop_tbl_idx) );
    add_const_int(part, idx, oop_tbl_idx, true);
  }
  // long helpers
  void add_vreg_long( DebugInfoBuilder::JVM_Part part, uint idx, VReg::VR vreg ) {
    add_vreg     (part, idx+1, vreg, true);
    add_const_int(part, idx,   (intptr_t)frame::double_slot_primitive_type_empty_slot_id << 32);
  }
  void add_const_long( DebugInfoBuilder::JVM_Part part, uint idx, intptr_t val ) {
    add_const_int(part, idx+1, val);
    add_const_int(part, idx,  (intptr_t)frame::double_slot_primitive_type_empty_slot_id << 32);
  }
  void add_callee_save_pair( VReg::VR src, VReg::VR dst );
  void set_base_derived( const GrowableArray<VReg::VR>* bdpairs ) { _base_derived_pairs= bdpairs; }
  void set_extra_lock  () { _extra_lock   = true; }
  void set_should_reexecute() { _reexecute = true; }
  void set_inline_cache() { _inline_cache = true; }
  int size_jlocals() const;
  int size_jstack() const;
  int size_locks() const;
  void add_exception( CommonAsm* _asm, int bci, const Label *lbl);

  // True if these two are structurally equal.
  bool equals( const DebugScopeBuilder *dbg ) const;

  const DebugScope *get_compressed(CommonAsm *_asm, GrowableArray<intptr_t> *shared_cons) {
    if( !_compressed ) make_compressed(_asm, shared_cons);
    return _compressed;
  }

  // Pretty print debug info for this scope only
void print(outputStream*st)const;
};

// --- PC2PCMap
class PC2PCMap:public CHeapObj{
  const pcMap *const _maps;
public:
  PC2PCMap( const pcMap *maps ) : _maps(maps) { }
  // Print all the PC2PC maps
  void print( const CodeBlob *blob, outputStream *st ) const { _maps->print(blob,st); }
  // Print just one PC2PC map
  void print( int rel_pc, const CodeBlob *blob, outputStream *st ) const;
  int get(int rel_pc) const { return _maps->get(rel_pc); }
  int size() const { return _maps->size(); }
};

// --- PC2PCMapBuilder
// Map PC's to PC's.  Generally used to map implicit null-pointer exception
// PCs to the corresponding handler PC.
class PC2PCMapBuilder : public PCMapBuilder {
  // Print 'stuff' with no newlines
  virtual void stuff_print_on( outputStream *st, intptr_t stuff ) const;
public:
  PC2PCMapBuilder() : PCMapBuilder("PC2PCMap") {}
  
  void add_mapping( int rel_pc1, int rel_pc2 ) { PCMapBuilder::add_mapping(rel_pc1,rel_pc2); }
  
  // Build a PC2PCMap.  Empties the contents of this PC2PCMapBuilder.
  const PC2PCMap *build_PC2PCMap() { return new PC2PCMap(build_pcMap()); }
};

// --- CommonAsm
// Common support for Azul assemblers.

class CommonAsm:public ResourceObj{
  friend class BitMap;          

protected:
  CodeBlob::BlobType _type;     // Intended type of this blob
  const int _compile_id;        // For compiled blobs, the compilation number
  const int _entry_bci;         // Usually InvocationEntryBci but different for OSRs
  int _deopt_sled_relpc;        // Start of deopt-sled for compiled methods

  // Code is placed at (*_pc++) up to _blob->end()
  // _pc is inside of _blob, until _blob runs out.
  // At that point, we grab a new larger blob, and copy the code from
  // here to there - meaning the code better be entirely PC-relative,
  // or the PC-relative bits all mentioned in relocations.
  CodeBlob *_blob;   // Current codeblob where we're placing code
address _pc;//Moving cursor of where the assembler is putting code; bump-ptr allocation of code space
  bool      _movable;// Can we move the code?  False if we already handed out an absolute address

  // Branches are all recorded during assembly, and offsets are patched in at
  // the very end.  Support for variant-sized branches is done by each cpu's
  // assembler as needed.  There's a PD-specific patch routine called.  Here
  // we just record the branches and labels in a dense list.  We record the
  // rel_pc and an index.  If the index is -2, this is a Label (a branch
  // target).  If the index is positive, then the index refers to either a
  // Label, or the index refers to another branch - forming a linked list of
  // branches which eventually ends at a branch with index -1 (index 0 is a
  // valid index).  Values BELOW -2 represent alignment requirements: -3 means
  // align to (1<<3)==8, -4 means align to (1<<4)==16 and so forth.  -99 is
  // the special meaning: inline-cache alignment.  Inline-caches have to have
  // various parts NOT cross 16-byte CAS'able boundaries. Calls want to be
  // 4-byte aligned 1-byte after the call instruction to support patching.
  friend class Label;
  enum { EOL=-1, LABEL=-2, ALIGN8=-3, ALIGN16=-4, ALIGN32=-5, IC=-99, CALL=-100 };
  GrowableArray<intptr_t> _bra_pcs;  // relative PC of the branch or label op
  GrowableArray<intptr_t> _bra_idx;  // tells label from branch from alignment
  bool _unpatched_branches;
  address abs_pc( int idx ) const;

public:
  CommonAsm( Arena *arena, CodeBlob::BlobType type, int compile_id,                const char *const name );
  CommonAsm( Arena *arena, CodeBlob::BlobType type, int compile_id, int entry_bci, const char *const name );
  CommonAsm( CodeBlob *cb, address pc );
  inline CodeBlob *blob() const { assert0( true || !_movable ); return _blob; }

const char*const _name;//Name of blob

  // Hands out the absolute current PC value - which means that the Assembler
  // is not allowed to replace a full CodeBlob with a new larger CodeBlob in a
  // different place.  Normally the Assembler deals with pc_offsets or
  // relative pcs and nobody can tell if the Assembler shuffles the absolute
  // locations about.  If you ask for this, then the code can no longer move.
  // This means that the assembly needs to be completed either in the current
  // CodeBlob or that the current CodeBlob can grow in-place - which is
  // certainly true at start of the VM when stubs are made, and we rely on it
  // by asking for lots of absolute_pc's during stub construction.
address pc();

  // Report back an offset that is relative to the blob start.  If the blob
  // moves, the relative_pc remains accurate.
  inline int rel_pc() const { return _pc - (address)_blob; }

  // Align code with nops
  virtual void align(intx)=0;

  // Make sure at least 'sz' bytes exist in the current CodeBlob.  Must be
  // tested before actually placing code down - it's test-n-bump allocation.
  // User 1st test, then must place code down AND bump the _pc.
  inline void grow(int sz) { if( _pc+sz <= _blob->end() ) return; grow_impl(sz); }
  virtual void grow_impl( int sz );     // Grow blob by given size


  inline void emit1( int b ) { // Emit 1 byte into CodeBlob, growing as needed
    if( _pc+1 > _blob->end() ) grow(1);
    *_pc++ = b;
  }

  inline void emit2( int s ) { // Emit 2 bytes into CodeBlob, growing as needed
    if( _pc+2 > _blob->end() ) grow(2);
    *(int16_t*)_pc = s; // Mis-aligned write, works on X86.  Must be aligned on other targets
_pc+=2;
  }

  inline void emit4( int32_t x ) {   // Emit 4-bytes into CodeBlob, growing as needed
    if( _pc+4 > _blob->end() ) grow(4);
    *(int32_t*)_pc = x; // Mis-aligned write, works on X86.  Must be aligned on other targets
_pc+=4;
  }

  inline void emit8( int64_t x ) {   // Emit 8-bytes into CodeBlob, growing as needed
    if( _pc+8 > _blob->end() ) grow(8);
    *(int64_t*)_pc = x; // Mis-aligned write, works on X86.  Must be aligned on other targets
    _pc += 8;
  }

void a_byte(int x){
emit1(x);
  }

  int _code_ends_data_begins;   // End of actual code
  void code_ends_data_begins() { _code_ends_data_begins = rel_pc(); }

  // --- Comments!
  // Record a comment at the current PC, and spit it back out during decode
void block_comment(const char*str);

  // --- Dependencies!
  // Record dependencies on this blob: things that if they change will force
  // this code to be invalidated.  Dependencies are stored as Klass/Method
  // pairs with some special values for special kinds of dependencies.

  // (1) If class is not an interface and method is not null, then all
  // concrete subclasses must only see this method.  The compiler may have
  // de-virtualized (and inlined) the only target method.  It is legal to have
  // abstract subclasses with no concrete implementors that overrode the
  // method since there are no objects which can call the overridden method.

  // (2) If class is not an interface and has only ONE concrete (non-abstract)
  // self+subclass, then the method is null.  The compiler may have shorted a
  // subtype check into a simple klass pointer-compare.  This condition is
  // stronger than (1) above and if both are needed, (2) can replace (1).

  // (3) If class is abstract with ZERO concrete subclasses, then the method
  // must be the ZeroImplementors sentinel.  Abstract subclasses do not
  // matter.  The compiler may have decided loads of such values must always
  // be null.

  // (4) If class is an interface with ZERO implementors then the method must
  // be the ZeroImplementors sentinel.  Sub-interfaces do not matter, except
  // that they (transitively) allow implementors.  The compiler may have
  // decided loads of such values must always be null.

  // (5) If class is an interface with ONE implementor, then the method must
  // be null.  Sub-interfaces do not matter, except that they (transitively)
  // allow implementors.  The compiler may have decided that the interface
  // objects are really all of the single implementor type.
  GrowableArray<const ciInstanceKlass*> _ciks;
  GrowableArray<const ciMethod       *> _cms ;
  static const ciMethod* ZeroImplementors;
  static const ciMethod* NoFinalizers;
  void add_dependent_impl   (const ciInstanceKlass* cik, const ciMethod* cm);
  int  add_dependent_impl_at(const ciInstanceKlass* cik, const ciMethod* cm);

  // Assert that this klass has NO concrete implementation: either it is an
  // abstract class (or interface) with no implementations or the klass is
  // unloaded.  Loading from such a field always returns a NULL.
  void assert_no_implementation(const ciInstanceKlass*cik) { add_dependent_impl(cik,ZeroImplementors); }

  // Assert that this instanceKlass has no concrete children and is not an
  // interface (but could still be abstract).  Used to make non-final classes
  // into final classes.
  void assert_leaf_type(const ciInstanceKlass*cik) { add_dependent_impl(cik,NULL); }

  // Assert that this instanceKlass has no finalizable subclasses: constructors 
  // for this klass which inline down to Object.<init> do (or do not) need to
  // register the new object before running the <init>s.
  void assert_has_no_finalizable_subclasses(const ciInstanceKlass*cik) { add_dependent_impl(cik,NoFinalizers); }

  // Assert that this method is not overridden in any subklass.
  // Used to make a non-final method into a final method.
  void assert_unique_concrete_method(const ciInstanceKlass *cik, const ciMethod*cm);

  static int count_call_targets(instanceKlass* current, methodOop moop, int found);

  // Verify the current dependencies still hold. 
  bool check_dependencies() const;
  // Insert the dependencies into instanceKlasses.  
  int insert_dependencies() const;

  // Nicely print them
  void print_dependencies() const;

  // --- OopMaps!
  // OopMap support.  Add an OopMap at this PC.  The OopMap is defined by a
  // bitmap.  The low bits map 1-to-1 with the registers.  The upper bits
  // refer to stack-slots and are offset'd by the number of registers, then
  // scaled by 8.  Example: assume REG_OOP_COUNT is 16 (ala X86-64) and RAX, RBX
  // and stk+8 and stk+24 are all holding oops.  Then we'd call add_oopmap
  // like this:
  //        add_mapping( rel_pc, RAX );
  //        add_mapping( rel_pc, RBX );
  //        add_mapping( rel_pc, VOopReg::stk2reg( 8) );
  //        add_mapping( rel_pc, VOopReg::stk2reg(24) );
  void add_oop( int rel_pc, VOopReg::VR reg );
  void add_oopmap( int rel_pc, const OopMap2 *map ); // add an entire map
  void add_empty_oopmap( address abs_pc );
  void add_empty_oopmap( int rel_pc );
  void set_oopmapBuilder( int maxoop ) { _oopmaps = new OopMapBuilder(maxoop); }
  const OopMapBuilder* oopmapBuilder() const { return _oopmaps; }
  inline address last_oopmap_adr() const { return (address)_blob + _oopmaps->last_relpc(); }
  

private:  // hide API to prevent unaware use that could fall into the 64 bit default size limitation
  inline void add_oop( int rel_pc, Register reg ) { add_oop(rel_pc, VOopReg::VR(reg)); }

protected:
  OopMapBuilder *_oopmaps;      // only set if any oopmaps actually made

  // --- Debug Info!
  // Add a DebugScopeBuilder at this PC.
public:
  void add_dbg( int rel_pc, const DebugScopeBuilder *dbgscope );
  DebugScopeBuilder *get_dbg( int rel_pc ) const { return _debuginfo->get(rel_pc); }
protected:
  DebugInfoBuilder *_debuginfo;

  // --- ImplicitExceptionMap
  // Gather implicit-exception points: mappings from (possibly faulting) PCs
  // to the associated handler.
public:
  void add_implicit_exception( int faulting_pc, int handler_pc );
protected:
  PC2PCMapBuilder *_NPEinfo;

  // --- Compile-Time Constant Oops
  // List of objects statically referenced in the Blob.  These will be loaded
  // from the CodeCacheOopTable and so only the index appears in the code -
  // but the object has to be kept alive during construction.  The CI will
  // keep these alive until 'baking'.  For static native wrappers, the KID is
  // for the klass that the native is being compiled for.  During "baking" the
  // oops will be copied into the methodCodeOop and kept properly alive via GC.
public:
  void record_constant_oop( int oidx );
private:
GrowableArray<int>_oop_indices;
public:

  // --- Install Code
  // Append slow-path code for various compiler bits at the assembly-end
  virtual void pre_bake() = 0;

  // Patch all branch targets.  May grow code if target supports variable size branches.
  virtual void patch_branches() = 0;
  virtual void reset_branches();
  virtual bool has_variant_branches() const = 0;

  // Make a methodCodeOop.  Stuff the blob into it.  Compress the bulky
  // PCMapBuilder forms and stuff them into it.  Reset all innnards; prevent
  // the masm from being used again.
  methodCodeRef bake_into_codeOop(methodRef mref, int framesize_bytes, const CodeProfile *profile, bool has_unsafe, TRAPS);
  // Used only on startup - before Universe is up and we can make
  // methodCodeOops at all.  Basically, used by the stub-gen stuff.
  // Only OopMaps are supported; not e.g. any sort of exception tables.
  void bake_into_CodeBlob(int framesize_bytes);

  // Set the code start location to the current pc; used in case the
  // code is aligned past the CodeBlob's earliest starting point.
  void set_code_start();

  // Debugging
void decode(outputStream*st)const;//disassemble blob to tty
void print(outputStream*st)const;

  // Legacy
  int offset() const { return rel_pc(); }
  void flush() { reset_branches(); }
};

// --- Label
// Label objects allow forward references.  This class replaces the old Sun Label class.
class Label:public ResourceObj{
  // Refers to a CommonAsm::_bra_* index.  -1 indicates the end of a linked
  // list of pending branches.  Other values refer to a label index
  // (_bra_idx[_idx] == -2) OR a linked-list head of pending branches
  // (_bra_idx[_idx] != -2).
  int _idx;
public:
  Label() : _idx(-1) {}
  Label( CommonAsm*_asm ) : _idx(-1) { bind(_asm,_asm->rel_pc()); }
  void add_jmp(CommonAsm*_asm);
  void bind   (CommonAsm*_asm, int rel_pc);
  bool is_bound(CommonAsm *_asm) const;
  address abs_pc(CommonAsm*_asm) const { return _asm->abs_pc(_idx); } // Hand out an absolute PC at this label
  int     rel_pc(CommonAsm*_asm) const { return _asm->_bra_pcs.at(_idx); } // Hand out a relative PC at this label
};

// ---
// Make a simple one-shot single-entry assembly chunk
struct ScopedAsm {
  CommonAsm *const _asm;
Label l;
  ScopedAsm(CommonAsm *a) : _asm(a), l() { a->align(CodeEntryAlignment); l.bind(a,a->rel_pc()); }
  address abs_pc() { _asm->patch_branches(); return l.abs_pc(_asm); }
  ~ScopedAsm() { _asm->reset_branches(); }
};

#endif // COMMONASM_HPP
