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
#ifndef CODEPROFILE_HPP
#define CODEPROFILE_HPP


#include "bytecodes.hpp"
#include "freezeAndMelt.hpp"
#include "globalDefinitions.hpp"
#include "invocationCounter.hpp"
#include "methodOop.hpp"
#include "ostream.hpp"
#include "sizes.hpp"

class methodRef;
class ciMethod;
class C1_MacroAssembler;

#ifdef ASSERT
#define CPMAGIC 
#else
#undef CPMAGIC
#endif

// Code Profile Data
//
// CPDatas are variable sized; for many bytecodes they are zero-sized.  They
// otherwise are large enough to hold all interesting flags and counters for a
// single bytecode.
//
// Methods have an array of bytecodes and a matching array of CPData's.  The
// CPDatas for one method are packed end-to-end.  The mapping between BCI and
// CPData is complex so a BCI_CPD_Map is lazily-built (and only requires a
// scan over the bytecodes to build).  A BCI_CPD_Map is really a simple int
// array mapping BCI to matching CPData offset.  The first offset for bci zero
// is always a zero offset; the last offset for the max bci is the total
// CPData-array size.
// 
// A compilation is a collection of inlined methods.  Each inlined method has
// its own copy of a CPData-array, jammed together in a larger CPData-array.
// The Compilers are responsible for maintaining the mappings between inlined
// method BCI and matching CPData.  The first CPData in this large
// CPData-array is always the CPData matching the first interesting bytecode
// in the top-level method.
//
// CPData_Invoke's hold invoke bytecodes' profile data.  For inlined invokes,
// the CPData_Invoke also holds the offset to the inlined methods' CPData-
// array, as well as an objectID (OID) to name the inlined method without
// requiring GC.
//
// The jammed-CPData-array isn't needed for compilation - but only when code
// actually executes.  Hence the final buffer is built just before the codeblob
// is installed and doesn't exist during compilation.
//
// During compilation, the compiler will maintain the CPData offset for the
// current bci, as well as the max jammed-CPData-array size.  This size is
// lazily grown as inline opportunities are discovered.  

struct CPData_Null;
struct CPData_Invoke;
struct CPData_Jump;
struct CPData_Branch;

// Abstract class of profiling data structures.  Each interesting bytecode has
// it's own profile data class.  All CPDatas can be initialized by bulk
// zero'ing.  The class is abstract, but NO v-table.  We know the type based
// on the bytecode.
struct CPData {
  static const u1 cp_type[];
public:
bool saw_null(Bytecodes::Code c);
  void print_line( Bytecodes::Code c, outputStream *st );

  static bool    is_Null  ( Bytecodes::Code c ) { return cp_type[c]&1; }
  static bool    is_Invoke( Bytecodes::Code c ) { return cp_type[c]&2; }
  static bool    is_Jump  ( Bytecodes::Code c ) { return cp_type[c]&4; }
  static bool    is_Branch( Bytecodes::Code c ) { return cp_type[c]&8; }
  CPData_Null  *isa_Null  ( Bytecodes::Code c ) { return is_Null  (c) ? (CPData_Null  *)this : 0; }
  CPData_Invoke*isa_Invoke( Bytecodes::Code c ) { return is_Invoke(c) ? (CPData_Invoke*)this : 0; }
  CPData_Jump  *isa_Jump  ( Bytecodes::Code c ) { return is_Jump  (c) ? (CPData_Jump  *)this : 0; }
  CPData_Branch*isa_Branch( Bytecodes::Code c ) { return is_Branch(c) ? (CPData_Branch*)this : 0; }
  CPData_Null  * as_Null  ( Bytecodes::Code c ) { assert0( is_Null  (c)); return (CPData_Null  *)this; }
  CPData_Invoke* as_Invoke( Bytecodes::Code c ) { assert0( is_Invoke(c)); return (CPData_Invoke*)this; }
  CPData_Jump  * as_Jump  ( Bytecodes::Code c ) { assert0( is_Jump  (c)); return (CPData_Jump  *)this; }
  CPData_Branch* as_Branch( Bytecodes::Code c ) { assert0( is_Branch(c)); return (CPData_Branch*)this; }

  // Magic byte to help identify the start of a cpdata struct
#ifdef CPMAGIC
u1 _magic;
  enum { _magicbase = 0xf0 };
  bool is_CPData();
  bool is_Null();
  bool is_Invoke();
  bool is_Jump();
  bool is_Branch();
  static int magic_offset() { return in_bytes(byte_offset_of(CPData, _magic)); }
#else
  bool is_CPData();
  bool is_Null();
  bool is_Invoke();
  bool is_Jump();
  bool is_Branch();
#endif
};

// The ever-popular null-check.  It can fail, and then we need to compile a
// real check in instead of an implicit check via a memory op.  
// Note that aastore has four (4) failure flags:
// (1) see a null array
// (2) fail to cast to declared type
// (3) fail standard range check (will be throwing)
// (4) fail widened range check (maybe throws, maybe not)
// (5) Can fail for unloaded classes, but a trip through interpreter fixes that
struct CPData_Null : public CPData {
  union {
    struct { 
      u1 _null:1,               // Ever seen a null
         _fail:1,               // Ever seen a heroic opt fail
         _rchk:1,               // Ever seen a range check fail
         _rchk_wide:1,          // Ever seen a range-check-widening fail
         _poly_inlining_fail:1; // Ever see highly optimistic polymorphic call inlining fail
    };
    u1 _bitdata;                // To take address-of for offset math
  };

  static int size() { return sizeof(CPData_Null); }
  static int log_min_align() { return 0; }
  bool saw_null() const { return _null; }
  bool did_fail() const { return _fail; }
  bool did_rchk() const { return _rchk; }
  bool did_rchk_wide() const { return _rchk_wide; }
  bool did_poly_inlining_fail() const { return _poly_inlining_fail; }
void print_line(outputStream*st);
  static int bitdata_offset() { return in_bytes(byte_offset_of(CPData_Null, _bitdata)); }
};

inline bool CPData::saw_null( Bytecodes::Code c ) { return as_Null(c)->saw_null(); }

struct CPData_Jump : public CPData {
  int _reached;                 // Count of times this jump is reached

  static int size() { return sizeof(CPData_Jump); }
  static int log_min_align() { return 2; }
void print_line(outputStream*st);
  int reached() const { return _reached; }
};

struct CPData_Branch : public CPData {
  int _nottaken;                // Count of times this jump is not taken
  int _taken;                   // Count of times this jump is taken

  static int size() { return sizeof(CPData_Branch); }
  static int log_min_align() { return 2; }
void print_line(outputStream*st);
  int nottaken() const { return _nottaken; }
  int taken()    const { return _taken;    }
  int reached()  const { return _taken + _nottaken; }

  static int nottaken_offset() { return in_bytes(byte_offset_of(CPData_Branch, _nottaken)); }
  static int taken_offset()    { return in_bytes(byte_offset_of(CPData_Branch, _taken));    }
};

// Invoke bytecodes
struct CPData_Invoke : public CPData_Null {
  int _inlined_method_oid;      // Object ID for the inlined method (or 0)
  int _cpd_offset;              // Offset to the inlined method's CPData-array
  int _site_count;              // Times this call-site has been invoked
  int _inlining_failure_id;     // ID of inlining failure message
  enum {
    NumCalleeHistogramEntries = 3
  };

  // The profiling code generated by C1 makes assumptions about the following
  // three variables type and position.  These KIDs are not GC'd and thus may
  // point to dead klasses (NULL's in the klassTable at these KIDs), or even
  // recycled KIDs which can very rarely return illegal or inappropriate
  // klasses for this callsite.  The user of these KIDs (C2) needs to make
  // sure the returned values are sane.
  int _callee_histogram_klassids[NumCalleeHistogramEntries];
  int _callee_histogram_count[NumCalleeHistogramEntries];
  int _callee_histogram_num_overflowed;

  static int size() { return sizeof(CPData_Invoke); }
  static int log_min_align() { return 2; }
void print_line(outputStream*st);

  // The following two functions are racey since _inlined_method_oid is not
  // known to GC and thus may point to a recycled entry in the method table.
methodRef inlined_method()const;
  int inlined_method_oid() const { return _inlined_method_oid; }

  int is_inlined() const { return _inlined_method_oid != 0; }
  int cpd_offset() const { return _cpd_offset; }
  void set_inlined_method( ciMethod *m, int cpd_offset );
  int site_count() const { return _site_count; }

  int num_callees() const { 
    if (_callee_histogram_num_overflowed > 0) {
      return NumCalleeHistogramEntries+1;
    }
    int count=0;
for(uint i=0;i<NumCalleeHistogramEntries;i++){
if(_callee_histogram_count[i]>0){
        count++;
      }
    }
    return count;
  }


  int callee_count() const { 
    int sum=_callee_histogram_num_overflowed;
for(uint i=0;i<NumCalleeHistogramEntries;i++){
      sum += _callee_histogram_count[i];
    }
    return sum;
  }

  int get_largest_callee_index() const {
assert(NumCalleeHistogramEntries>=1,"Cant call this");
    int max=_callee_histogram_count[0];
    int idx=0;
for(uint i=1;i<NumCalleeHistogramEntries;i++){
      if(_callee_histogram_count[i] > max) {
        max = _callee_histogram_count[i];
idx=i;
      }
    }
    return idx;
  }

  int get_callee_overflow() const {
    return _callee_histogram_num_overflowed;
  }

  // Gen count to count invokes, receivers, etc.
  static ByteSize invoke_count_offset() { return byte_offset_of(CPData_Invoke, _site_count); }

  static int callee_histogram_klassids_offset() { return in_bytes(byte_offset_of(CPData_Invoke, _callee_histogram_klassids[0])); }
  static int callee_histogram_count_offset() { return in_bytes(byte_offset_of(CPData_Invoke, _callee_histogram_count[0])); }
  static int callee_histogram_num_overflowed_offset() { return in_bytes(byte_offset_of(CPData_Invoke, _callee_histogram_num_overflowed)); }
};


// An array of variable-sized CPDatas, representing profiling data for the
// inlined tree of methods.  One per codeblob (NOT per-method!).  Data follows
// top-level struct.
class CodeProfile {
  // _invoke_count and _backedge_count should be colocated on the same cache line
  InvocationCounter _invoke_count;             // Invocation count for main entry point
  InvocationCounter _backedge_count;           // Count of all backedges hit
  u4 _throwout_count; // Count of times method was exited via exception
  u4 _size;  // Size in bytes of entire CodeProfile structure
  stringStream* _debug_output; // Once assigned, the CodeProfile owns _debug_output and is responsible for freeing it
  void print_impl( const methodOopDesc* const moop, int inloff, int depth, int depthmax=-1, outputStream* out=tty ) const;
  void print_to_xml_impl( const methodOopDesc* const moop, int inloff, int depth, xmlBuffer *xb, int depthmax=-1 ) const;
  // We have three types of counters tracking the lifetime of code profiles:
  // allocated, alive, and freed. When we don't have a leak, the following 
  // expression should be zero: #allocated - #alive - #freed == 0. These numbers
  // are binned in three arrays respectively:
  //   _allocation_stats[_max_allocation_kinds];
  //   _alive_stats[_max_alive_kinds];
  //   _free_stats[_max_free_kinds];

  // Support for interprocedural escape analysis, from Thomas Kotzmann.
  intx              _eflags;          // flags on escape information
  intx              _arg_local;       // bit set of non-escaping arguments
  intx              _arg_stack;       // bit set of stack-allocatable arguments
  intx              _arg_returned;    // bit set of returned arguments

public:
  // allocation 
  enum {
    _newly_allocated = 0,
    _cloned,
    _max_allocation_kinds,
  };

  // alive
  enum {
    _cb_attached = 0,
    _moop_attached,
    _c1_in_use,
    _c2_in_use,
    _c2_cloned,
    _max_alive_kinds,
  };

  // freed
  enum {
    _moop_freed = 0,
    _race_freed,
    _cb_freed,
    _c1_failure,
    _c2_failure,
    _c2_clone_freed,
    _inline_decision_freed,
    _temporary_freed,
    _misc_freed,
    _max_free_kinds,
  };
#ifndef PRODUCT
  static intptr_t _allocation_stats[_max_allocation_kinds];
  static intptr_t _alive_stats[_max_alive_kinds];
  static intptr_t _free_stats[_max_free_kinds];
#endif

  // Specify the type of counter query. With the counter type we can take a 
  // quick peek of counter values without going through the expensive process
  // of cloning the code profile structure hung off methodcodeoops.
  enum counters {
      _throwout,
      _invoke,
  };

  // Convert base offset (offset to an inlined CodeProfile) plus a normal
  // bytecode-directed offset into a CPData pointer.
  CPData *cpdoff_to_cpd( int baseoff, int cpdoff ) const {
    return (CPData*)(((char*)this)+baseoff+cpdoff+sizeof(CodeProfile));
  }

  // Make and nuke CodeProfiles
  static CodeProfile *make( methodOop moop );
  static CodeProfile *make( ciMethod *moop );
  static CodeProfile *make( FAMPtr old_cp );
  void free(int kind) const { 
    if (_debug_output && _debug_output != tty) {
      delete _debug_output;
    }
#ifndef PRODUCT
    guarantee(kind < _max_free_kinds, "unknown free kind");
    Atomic::inc_ptr(&_free_stats[kind]); 
#endif
    memset( (void*)this, 0xDB, _size );
    FREE_C_HEAP_ARRAY(CodeProfile,this);
  }

  void set_debug_output(stringStream* out);
  stringStream* get_debug_output() const;

  void artificially_populate_code_profile( methodOopDesc *moop );

  // Clone the current code profile for a newly created methodcodeoop to use
CodeProfile*clone();
  CodeProfile *clone_into_arena(Arena* arena);

  CodeProfile *grow( int oldsize, int newsize );
  // Inline CodeProfile for callee; record it in the CPData_Invoke
  // found in caller's CP (which starts at inloff) at the given bci.
  CodeProfile *grow_to_inline( int oldsize, int inloff, ciMethod *caller, int bci, ciMethod *callee );

  // Read invoke_count; gen code to bump invoke_count
  int total_count() const { return invoke_count() + backedge_count(); }
  int invoke_count() const { return _invoke_count.count(); }
int backedge_count()const{return _backedge_count.count();}
  u4 throwout_count() const { return _throwout_count; }
  void throwout_count_incr() { _throwout_count++; }
  static int   invoke_count_offset_in_bytes() { return in_bytes(byte_offset_of(CodeProfile,   _invoke_count)); }
  static int backedge_count_offset_in_bytes() { return in_bytes(byte_offset_of(CodeProfile, _backedge_count)); }
  static int throwout_count_offset_in_bytes() { return in_bytes(byte_offset_of(CodeProfile, _throwout_count)); }

  // Reset invoke counter to 0
  void reset_invoke_count() { _invoke_count.reset(); }
  void reset_backedge_count() { _backedge_count.reset(); }
  void reset_invoke_count_to(unsigned x) { _invoke_count.reset_to(x); }

  // Support for interprocedural escape analysis, from Thomas Kotzmann.

  // NOT multi-threaded-update safe, so compilers should insert escape bits
  // into their own private CodeProfiles individually (or I need to show the
  // data is idempotent, or using locking to read & write).
  enum EscapeFlag {
    estimated    = 1 << 0,
    return_local = 1 << 1
  };

  intx eflags()                                  { Unimplemented(); return _eflags; }
  intx arg_local()                               { Unimplemented(); return _arg_local; }
  intx arg_stack()                               { Unimplemented(); return _arg_stack; }
  intx arg_returned()                            { Unimplemented(); return _arg_returned; }

  void set_eflags(intx v)                        { Unimplemented(); _eflags = v; }
  void set_arg_local(intx v)                     { Unimplemented(); _arg_local = v; }
  void set_arg_stack(intx v)                     { Unimplemented(); _arg_stack = v; }
  void set_arg_returned(intx v)                  { Unimplemented(); _arg_returned = v; }

  void clear_escape_info()                       { Unimplemented(); _eflags = _arg_local = _arg_stack = _arg_returned = 0; }
  bool has_escape_info();
  void update_escape_info();

  void set_eflag  (EscapeFlag f) { _eflags |=  f; }
  void clear_eflag(EscapeFlag f) { _eflags &= ~f; }
  bool eflag_set(EscapeFlag f) const { return (_eflags & f) != 0; }

  void set_arg_local(int i);
  void set_arg_stack(int i);
  void set_arg_returned(int i);

  bool is_arg_local(int i) const;
  bool is_arg_stack(int i) const;
  bool is_arg_returned(int i) const;

u4 size()const{return _size;}

  // Given a thread stack, find the nested CPData at the leaf.
  static CPData *find_nested_cpdata( JavaThread *thread, vframe vf, methodCodeOop mco, /*outgoing arg*/ CodeProfile* *p_cp, bool check_deopt);

  // Printing
  void print( const methodOopDesc* const moop, outputStream *out ) const;
  void print_xml_on(const methodOopDesc* const moop, xmlBuffer* xb, bool ref, int depthmax=-1) const;
  ~CodeProfile();
#ifndef PRODUCT
  static void update_allocation_stats(int kind) { 
    guarantee(kind < _max_allocation_kinds, "unknown allocation kind");
    Atomic::inc_ptr(&_allocation_stats[kind]); 
  }
  static void update_alive_stats(int kind, int v) { 
    guarantee(kind < _max_alive_kinds, "unknown alive kind");
    Atomic::add_ptr(intptr_t(v),&_alive_stats[kind]); 
  }
#endif
#ifdef ASSERT
  void installMagic(ciMethod* method, int offset=0);
  void installMagic(methodOop method, int offset=0);
#endif
  // Data lives from end of 'this' to forever
};

// A mapping from BCI to CPData-offsets for this method only (no inlining).
// This structure is lazily built by the compilers for any method that gets
// considered for compilation.  When methods are unloaded the structure is
// freed.
class BCI2CPD_mapping {
  const jint *_mapping;         // Matching BCI to CPD-offset mapping
  jint _codesize;               // Number of bytecodes in the mapping
  jint _deopt_count;            // Used to assert on endless deopt
  jlong _last_deopt_tick;       // Tick of last deopt
public:
  enum { unprofiled_bytecode_offset = -1 };
  void init( methodOop moop );  // Init this Mapping
  void init( FAMPtr old_map );  // Melt this mapping
  void free();                  // Free this mapping

  int bci_to_cpdoff( int bci ) const { return bci == InvocationEntryBci ? unprofiled_bytecode_offset : _mapping[bci]; } 
  // Top-level-only (no inline) convenience CPData access fcn
  CPData *cpdata( CodeProfile *cp, int bci ) const {
    int cpdoff = bci_to_cpdoff( bci );
    assert0( cpdoff != unprofiled_bytecode_offset );
    return cp ? cp->cpdoff_to_cpd(0,cpdoff) : NULL;
  }
  int maxsize() const { return _mapping[_codesize]; }

  // Deopt assert
  void did_deopt(const CodeProfile *const cp, const methodOopDesc* const moop, const int bci);
  jint deopt_count() const { return _deopt_count; }
  jint endless_deopt_count() const { return _codesize+100; }

  jint codesize() const { return _codesize; }
};

#endif // CODEPROFILE_HPP
