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
#ifndef COMPILE_HPP
#define COMPILE_HPP


#include "assembler_pd.hpp"
#include "c2_globals.hpp"
#include "ciField.hpp"
#include "phase.hpp"
#include "regmask.hpp"
#include "resourceArea.hpp"

class Bundle;
class C2Compiler;
class CPData_Invoke;
class CallGenerator;
class MacroAssembler;
class ConnectionGraph;
class InlineTree;
class JVMState;
class MachSafePointNode;
class Matcher;
class Node;
class Node_Notes;
class PhaseCFG;
class PhaseGVN;
class PhaseRegAlloc;
class RootNode;
class StartNode;
class TypeFunc;
class TypePtr;
class Unique_Node_List;
class WarmCallInfo;

//------------------------------Compile----------------------------------------
// This class defines a top-level Compiler invocation. 

class Compile : public Phase {
 public:
  // Fixed alias indexes.  (See also MergeMemNode.)
  enum {
    AliasIdxTop = 1,  // pseudo-index, aliases to nothing (used as sentinel value)
    AliasIdxBot = 2,  // pseudo-index, aliases to everything
    AliasIdxRaw = 3   // hard-wired index for TypeRawPtr::BOTTOM
  };

  // Variant of TraceTime(NULL, &_t_accumulator, TimeCompiler);
  // Integrated with logging.  If logging is turned on, and dolog is true,
  // then brackets are put into the log, with time stamps and node counts.
  // (The time collection itself is always conditionalized on TimeCompiler.)
  class TracePhase : public TraceTime {
    const bool _dolog;
   public:
    TracePhase(const char* name, elapsedTimer* accumulator, bool dolog);
    ~TracePhase();
  };

  // Information per category of alias (memory slice)
  class AliasType {
   private:
    friend class Compile;

    int             _index;         // unique index, used with MergeMemNode
    const TypePtr*  _adr_type;      // normalized address type
    ciField*        _field;         // relevant instance field, or null if none
    bool            _is_rewritable; // false if the memory is write-once only
    int             _general_index; // if this is type is an instance, the general
                                    // type that this is an instance of

    void Init(int i, const TypePtr* at);

   public:
    int             index()         const { return _index; }
    const TypePtr*  adr_type()      const { return _adr_type; }
    ciField*        field()         const { return _field; }
    bool            is_rewritable() const { return _is_rewritable; }
    bool            is_volatile()   const { return (_field ? _field->is_volatile() : false); }
    int             general_index() const { return (_general_index != 0) ? _general_index : _index; }

    void set_rewritable(bool z) { _is_rewritable = z; }
    void set_field(ciField* f) {
      assert(!_field,"");
      _field = f;
      if (f->is_final())  _is_rewritable = false;
    }

    void print_on(outputStream* st) PRODUCT_RETURN;
  };

  enum {
    logAliasCacheSize = 6,
    AliasCacheSize = (1<<logAliasCacheSize)
  };
  struct AliasCacheEntry { const TypePtr* _adr_type; int _index; };  // simple duple type

 private:
  // Fixed parameters to this compilation.
  const int             _compile_id;
  const bool            _subsume_loads;         // Load can be matched as part of a larger op.
  ciMethod*             _method;                // The method being compiled.
  int                   _entry_bci;             // entry bci for osr methods.
  const TypeFunc*       _tf;                    // My kind of signature
  InlineTree*           _ilt;                   // Ditto (temporary).

  // Control of this compilation.
  int                   _num_loop_opts;         // Number of iterations for doing loop optimiztions
  int                   _max_inline_size;       // Max inline size for this compilation
  int                   _freq_inline_size;      // Max hot method inline size for this compilation
  int                   _major_progress;        // Count of something big happening
  bool                  _deopt_happens;         // TRUE if de-optimization CAN happen
  bool                  _has_loops;             // True if the method _may_ have some loops
  bool                  _loops_found;           // True if any loops are marked with LoopNodes
  bool                  _has_split_ifs;         // True if the method _may_ have some split-if
  bool                  _has_unsafe_access;     // True if the method _may_ produce faults in unsafe loads or stores.
  bool                  _do_inlining;           // True if we intend to do inlining
  int                   _AliasLevel;            // Locally-adjusted version of AliasLevel flag.
  bool                  _print_assembly;        // True if we should dump assembly code for this compilation
#ifndef PRODUCT
  bool                  _trace_opto_output;
#endif

  // Compilation environment.
ResourceArea _comp_arena;//Arena with lifetime equivalent to Compile
  ciEnv*                _env;                   // CI interface
  const char*           _failure_reason;        // for record_failure/failing pattern
  bool                  _failure_retry_compile; // Should we retry compiling with C2
  GrowableArray<CallGenerator*>* _intrinsics;   // List of intrinsics.
  ConnectionGraph*      _congraph;

  // Node management
  uint                  _unique;                // Counter for unique Node indices
  debug_only(static int _debug_idx;)            // Monotonic counter (not reset), use -XX:BreakAtNode=<idx>
ResourceArea _node_arena;//Arena for new-space Nodes
ResourceArea _old_arena;//Arena for old-space Nodes, lifetime during xform
  RootNode*             _root;                  // Unique root of compilation, or NULL after bail-out.
  Node*                 _top;                   // Unique top node.  (Reset by various phases.)

  Node*                 _immutable_memory;      // Initial memory state

  // Blocked array of debugging and profiling information,
  // tracked per node.
  enum { _log2_node_notes_block_size = 8,
         _node_notes_block_size = (1<<_log2_node_notes_block_size)
  };
  GrowableArray<Node_Notes*>* _node_note_array;
  Node_Notes*           _default_node_notes;  // default notes for new nodes

  // After parsing and every bulk phase we hang onto the Root instruction.
  // The RootNode instruction is where the whole program begins.  It produces
  // the initial Control and BOTTOM for everybody else.

  // Type management
  Arena                 _Compile_types;         // Arena for all types
  Arena*                _type_arena;            // Alias for _Compile_types except in Initialize_shared()
  Dict*                 _type_dict;             // Intern table
  size_t                _type_last_size;        // Last allocation size (see Type::operator new/delete)
  ciMethod*             _last_tf_m;             // Cache for
  const TypeFunc*       _last_tf;               //  TypeFunc::make
  AliasType**           _alias_types;           // List of alias types seen so far.
  int                   _num_alias_types;       // Logical length of _alias_types
  int                   _max_alias_types;       // Physical length of _alias_types
  AliasCacheEntry       _alias_cache[AliasCacheSize]; // Gets aliases w/o data structure walking

  // Parsing, optimization
  PhaseGVN*             _initial_gvn;           // Results of parse-time PhaseGVN
  Unique_Node_List*     _for_igvn;              // Initial work-list for next round of Iterative GVN
  WarmCallInfo*         _warm_calls;            // Sorted work-list for heat-based inlining.

  // Matching, CFG layout, allocation, code generation
  PhaseCFG*             _cfg;                   // Results of CFG finding
  bool                  _select_24_bit_instr;   // We selected an instruction with a 24-bit result
  bool                  _in_24_bit_fp_mode;     // We are emitting instructions with 24-bit results
  bool                  _has_java_calls;        // True if the method has java calls 
  Matcher*              _matcher;               // Engine to map ideal to machine instructions
  PhaseRegAlloc*        _regalloc;              // Results of register allocation.
  RegMask               _FIRST_STACK_mask;      // All stack slots usable for spills (depends on frame layout)
ResourceArea*_indexSet_arena;//control IndexSet allocation within PhaseChaitin
  void*                 _indexSet_free_block_list; // free list of IndexSet bit blocks

  uint                  _node_bundling_limit;
  Bundle*               _node_bundling_base;    // Information for instruction bundling

  int                   _first_block_size;      // Size of unvalidated entry point code / OSR poison code

 public:
  int                   _framesize_bytes;       // Size of total frame

  // Accessors

  // The Compile instance currently active in this (compiler) thread.
  static Compile* current() { return C2CompilerThread::current()->_compile; }

  // ID for this compilation.  Useful for setting breakpoints in the debugger.
  int               compile_id() const          { return _compile_id; }

  // Does this compilation allow instructions to subsume loads?  User
  // instructions that subsume a load may result in an unschedulable
  // instruction sequence.
  bool              subsume_loads() const       { return _subsume_loads; }

  // Other fixed compilation parameters.
  ciMethod*         method() const              { return _method; }
  int               entry_bci() const           { return _entry_bci; }
  bool              is_osr_compilation() const  { return _entry_bci != InvocationEntryBci; }
  int               _active_monitor_count;  // Count of active monitors at OSR entry
  const TypeFunc*   tf() const                  { assert(_tf!=NULL, ""); return _tf; }
  void         init_tf(const TypeFunc* tf)      { assert(_tf==NULL, ""); _tf = tf; }
  InlineTree*       ilt() const                 { return _ilt; }

  // Control of this compilation.
  int               major_progress() const      { return _major_progress; }
  void          set_major_progress()            { _major_progress++; }
  void        clear_major_progress()            { _major_progress = 0; }
  void      restore_major_progress(int old)     { _major_progress = old; }  
  int               num_loop_opts() const       { return _num_loop_opts; }
  void          set_num_loop_opts(int n)        { _num_loop_opts = n; }
  int               max_inline_size() const     { return _max_inline_size; }
  void          set_freq_inline_size(int n)     { _freq_inline_size = n; }
  int               freq_inline_size() const    { return _freq_inline_size; }
  void          set_max_inline_size(int n)      { _max_inline_size = n; }
  bool              deopt_happens() const       { return _deopt_happens; }
  bool              has_loops() const           { return _has_loops; }
  void          set_has_loops(bool z)           { _has_loops = z; }
  bool              loops_found() const         { return _loops_found; }
void set_loops_found(bool z){_loops_found=z;}
  bool              has_split_ifs() const       { return _has_split_ifs; }
  void          set_has_split_ifs(bool z)       { _has_split_ifs = z; }
  bool              has_unsafe_access() const   { return _has_unsafe_access; }
  void          set_has_unsafe_access(bool z)   { _has_unsafe_access = z; }
  bool              do_inlining() const         { return _do_inlining; }
  void          set_do_inlining(bool z)         { _do_inlining = z; }
  int               AliasLevel() const          { return _AliasLevel; }
  bool              print_assembly() const       { return _print_assembly; }
  void          set_print_assembly(bool z)       { _print_assembly = z; }
  // check the CompilerOracle for special behaviours for this compile
  bool          method_has_option(const char * option) {
    return method() != NULL && method()->has_option(option);
  }
#ifndef PRODUCT
  bool          trace_opto_output() const       { return _trace_opto_output; }
#endif
  ConnectionGraph* congraph()                   { return _congraph;}

  // Compilation environment.
ResourceArea*comp_arena(){return&_comp_arena;}
  ciEnv*            env() const                 { return _env; }
  bool              failing() const             { return _env->failing() || _failure_reason != NULL; }
const char*failure_reason()const{return _failure_reason;}
  bool              failure_retry_compile() const    { return _failure_retry_compile; }
  bool              failure_reason_is(const char* r) { return (r==_failure_reason) || (r!=NULL && _failure_reason!=NULL && strcmp(r, _failure_reason)==0); }

  void record_failure(const char* reason, bool retry_compile);
void record_method_not_compilable(const char*reason){
    env()->record_method_not_compilable(reason);
    // Record failure reason.
record_failure(reason,false);
  }
  void record_method_not_compilable_all_tiers(const char* reason) { 
    record_method_not_compilable(reason);
  }

  bool check_node_count(uint margin, const char* reason) {
    if (unique() + margin > (uint)MaxNodeLimit) {
      record_method_not_compilable(reason);
      return true;
    } else {
      return false;
    }
  }

  // Node management
  uint              unique() const              { return _unique; }
  uint         next_unique()                    { return _unique++; }
  void          set_unique(uint i)              { _unique = i; }
  static int        debug_idx()                 { return debug_only(_debug_idx)+0; }
  static void   set_debug_idx(int i)            { debug_only(_debug_idx = i); }
ResourceArea*node_arena(){return&_node_arena;}
ResourceArea*old_arena(){return&_old_arena;}
  RootNode*         root() const                { return _root; }
  void          set_root(RootNode* r)           { _root = r; }
  StartNode*        start() const;              // (Derived from root.)
  void         init_start(StartNode* s);
  Node*             immutable_memory();

  // Handy undefined Node
  Node*             top() const                 { return _top; }

  // these are used by guys who need to know about creation and transformation of top:
  Node*             cached_top_node()           { return _top; }
  void          set_cached_top_node(Node* tn);

  GrowableArray<Node_Notes*>* node_note_array() const { return _node_note_array; }
  void set_node_note_array(GrowableArray<Node_Notes*>* arr) { _node_note_array = arr; }
  Node_Notes* default_node_notes() const        { return _default_node_notes; }
  void    set_default_node_notes(Node_Notes* n) { _default_node_notes = n; }

  Node_Notes*       node_notes_at(int idx) {
    return locate_node_notes(_node_note_array, idx, false);
  }
  inline bool   set_node_notes_at(int idx, Node_Notes* value);

  // Copy notes from source to dest, if they exist.
  // Overwrite dest only if source provides something.
  // Return true if information was moved.
  bool copy_node_notes_to(Node* dest, Node* source);

  // Workhorse function to sort out the blocked Node_Notes array:
  inline Node_Notes* locate_node_notes(GrowableArray<Node_Notes*>* arr,
                                       int idx, bool can_grow = false);

  void grow_node_notes(GrowableArray<Node_Notes*>* arr, int grow_by);

  // Type management
  Arena*            type_arena()                { return _type_arena; }
  Dict*             type_dict()                 { return _type_dict; }
  size_t            type_last_size()            { return _type_last_size; }
  int               num_alias_types()           { return _num_alias_types; }

  void          init_type_arena()                       { _type_arena = &_Compile_types; }
  void          set_type_arena(Arena* a)                { _type_arena = a; }
  void          set_type_dict(Dict* d)                  { _type_dict = d; }
  void          set_type_last_size(size_t sz)           { _type_last_size = sz; }

  const TypeFunc* last_tf(ciMethod* m) {
    return (m == _last_tf_m) ? _last_tf : NULL;
  }
  void set_last_tf(ciMethod* m, const TypeFunc* tf) {
    assert(m != NULL || tf == NULL, "");
    _last_tf_m = m;
    _last_tf = tf;
  }

  AliasType*        alias_type(int                idx)  { assert(idx < num_alias_types(), "oob"); return _alias_types[idx]; }
  AliasType*        alias_type(const TypePtr* adr_type) { return find_alias_type(adr_type, false); }
  bool         have_alias_type(const TypePtr* adr_type);
  AliasType*        alias_type(ciField*         field);

  int               get_alias_index(const TypePtr* at)  { return alias_type(at)->index(); }
  const TypePtr*    get_adr_type(uint aidx)             { return alias_type(aidx)->adr_type(); }
  int               get_general_index(uint aidx)        { return alias_type(aidx)->general_index(); }

  // Building nodes
  void              rethrow_exceptions(JVMState* jvms);
  void              return_values(JVMState* jvms);
  JVMState*         build_start_state(StartNode* start, const TypeFunc* tf);

  // Decide how to build a call.
  // The profile factor is a discount to apply to this site's interp. profile.
  CallGenerator*    call_generator(ciMethod* call_method, int vtable_index, bool call_is_virtual, JVMState* jvms, bool allow_inline, float profile_factor, CodeProfile *callee_cp, int callee_cp_inloff, CPData_Invoke* c2_caller_cpd, CPData_Invoke *caller_cpd);

  // Parsing, optimization
  PhaseGVN*         initial_gvn()               { return _initial_gvn; }
  Unique_Node_List* for_igvn()                  { return _for_igvn; }
  inline void       record_for_igvn(Node* n);   // Body is after class Unique_Node_List.
  void              record_for_escape_analysis(Node* n);
  void          set_initial_gvn(PhaseGVN *gvn)           { _initial_gvn = gvn; }
  void          set_for_igvn(Unique_Node_List *for_igvn) { _for_igvn = for_igvn; }

  void              identify_useful_nodes(Unique_Node_List &useful);
  void              remove_useless_nodes  (Unique_Node_List &useful);

  WarmCallInfo*     warm_calls() const          { return _warm_calls; }
  void          set_warm_calls(WarmCallInfo* l) { _warm_calls = l; }
  WarmCallInfo* pop_warm_call();

  // Matching, CFG layout, allocation, code generation
  PhaseCFG*         cfg()                       { return _cfg; }
  bool              select_24_bit_instr() const { return _select_24_bit_instr; }
  bool              in_24_bit_fp_mode() const   { return _in_24_bit_fp_mode; }
  bool              has_java_calls() const      { return _has_java_calls; }
  Matcher*          matcher()                   { return _matcher; }
  PhaseRegAlloc*    regalloc()                  { return _regalloc; }
  RegMask&          FIRST_STACK_mask()          { return _FIRST_STACK_mask; }
ResourceArea*indexSet_arena(){return _indexSet_arena;}
  void*             indexSet_free_block_list()  { return _indexSet_free_block_list; }
  uint              node_bundling_limit()       { return _node_bundling_limit; }
  Bundle*           node_bundling_base()        { return _node_bundling_base; }
  void          set_node_bundling_limit(uint n) { _node_bundling_limit = n; }
  void          set_node_bundling_base(Bundle* b) { _node_bundling_base = b; }
  bool          starts_bundle(const Node *n) const;

  void          set_matcher(Matcher* m)                 { _matcher = m; }
//void          set_regalloc(PhaseRegAlloc* ra)           { _regalloc = ra; }
void set_indexSet_arena(ResourceArea*a){_indexSet_arena=a;}
  void          set_indexSet_free_block_list(void* p)   { _indexSet_free_block_list = p; }

  // Remember if this compilation changes hardware mode to 24-bit precision
  void set_24_bit_selection_and_mode(bool selection, bool mode) {
    _select_24_bit_instr = selection;
    _in_24_bit_fp_mode   = mode;
  }

  int               first_block_size()          { return _first_block_size; }

  void set_has_java_calls(bool z) { _has_java_calls = z; }

  MacroAssembler _masm;         // assembler for this compilation

  // Major entry point.  Given a Scope, compile the associated method.
  // For normal compilations, entry_bci is InvocationEntryBci.  For on stack
  // replacement, entry_bci indicates the bytecode for which to compile a
  // continuation.
Compile(ciEnv*ci_env,const C2Compiler*compiler,ciMethod*target,
int entry_bci,bool subsume_loads,
GrowableArray<const ciInstanceKlass*>*ciks,
          GrowableArray<const ciMethod       *>* cms );
  
  // Free code profile if not nmethod not registered
  ~Compile();

  // Are we compiling a method?
  bool has_method() { return method() != NULL; }

  // Maybe print some information about this compile.
  void print_compile_messages();

  // Final graph reshaping, a post-pass after the regular optimizer is done.
  bool final_graph_reshaping();

  // returns true if adr is completely contained in the given alias category
  bool must_alias(const TypePtr* adr, int alias_idx);

  // returns true if adr overlaps with the given alias category
  bool can_alias(const TypePtr* adr, int alias_idx);
  
  // Driver for converting compiler's IR into machine code bits
  void Output();

  // Accessors for node bundling info.
  Bundle* node_bundling(const Node *n);
  bool valid_bundle_info(const Node *n);

  // Schedule and Bundle the instructions
  void ScheduleAndBundle();

  // Build OopMaps for each GC point
  void BuildOopMaps();

  // Write out basic block data to code buffer
  void Fill_buffer();

  // Stack slots that may be unused by the calling convention but must
  // otherwise be preserved.  On Intel this includes the return address.
  // On PowerPC it includes the 4 words holding the old TOC & LR glue.
  uint in_preserve_stack_slots();

  // "Top of Stack" slots that may be unused by the calling convention but must
  // otherwise be preserved.  
  static uint out_preserve_stack_slots();

  CodeProfile* c2_cp() { return _cp; }

 private:
  // Phase control:
  void Init(int aliaslevel);                     // Prepare for a single compilation
  int  Inline_Warm();                            // Find more inlining work.
  void Finish_Warm();                            // Give up on further inlines.
  void Optimize();                               // Given a graph, optimize it
  void Code_Gen();                               // Generate code from a graph

  // Management of the AliasType table.
  void grow_alias_types();
  AliasCacheEntry* probe_alias_cache(const TypePtr* adr_type);
  const TypePtr *flatten_alias_type(const TypePtr* adr_type) const;
  AliasType* find_alias_type(const TypePtr* adr_type, bool no_create);

  void verify_top(Node*) const PRODUCT_RETURN;

  // Intrinsic setup.
  void           register_library_intrinsics();                            // initializer
  CallGenerator* make_vm_intrinsic(ciMethod* m, bool is_virtual);          // constructor
  int            intrinsic_insertion_index(ciMethod* m, bool is_virtual);  // helper
  CallGenerator* find_intrinsic(ciMethod* m, bool is_virtual);             // query fn
  void           register_intrinsic(CallGenerator* cg);                    // update fn

#ifndef PRODUCT
  static juint  _intrinsic_hist_count[vmIntrinsics::ID_LIMIT];
  static jubyte _intrinsic_hist_flags[vmIntrinsics::ID_LIMIT];
#endif

 public:

  // Note:  Histogram array size is about 1 Kb.
  enum {                        // flag bits:
    _intrinsic_worked = 1,      // succeeded at least once
    _intrinsic_failed = 2,      // tried it but it failed
    _intrinsic_disabled = 4,    // was requested but disabled (e.g., -XX:-InlineUnsafeOps)
    _intrinsic_virtual = 8,     // was seen in the virtual form (rare)
    _intrinsic_both = 16        // was seen in the non-virtual form (usual)
  };
  // Update histogram.  Return boolean if this is a first-time occurrence.
  static bool gather_intrinsic_statistics(vmIntrinsics::ID id,
                                          bool is_virtual, int flags) PRODUCT_RETURN0;
  static void print_intrinsic_statistics() PRODUCT_RETURN;

  // In case the nmethod is not registered, we need to free the code profile 
  // here
  CodeProfile *_cp;

  // There may be temporary code profiles created for callees that haven't 
  // code profiles yet. They cannot be freed in doCall as the GraphKit is still
  // using CPData_Invoke. So we have to wait until the compiler is done to free
  // those temporarily created code profiles.
  CodeProfile **_cloned_cp;
  int _max_cloned_cp, _num_cloned_cp;

  // Graph verification code
  // Walk the node list, verifying that there is a one-to-one
  // correspondence between Use-Def edges and Def-Use edges
  // The option no_dead_code enables stronger checks that the
  // graph is strongly connected from root in both directions.
  void verify_graph_edges(bool no_dead_code = false) PRODUCT_RETURN;

  // Print bytecodes, including the scope inlining tree
  void print_codes();

  // End-of-run dumps.
  static void print_statistics() PRODUCT_RETURN;

  // Dump formatted assembly
  void dump_asm(int *pcs = NULL, uint pc_limit = 0) PRODUCT_RETURN;
  void dump_pc(int *pcs, int pc_limit, Node *n);

  // Verify ADLC assumptions during startup
  static void adlc_verification() PRODUCT_RETURN;

  // Definitions of pd methods
  static void pd_compiler2_init();

  int _split_ctr;               // Used to debug split-if
  void record_cloned_cp(CodeProfile *cp);

 public:
  outputStream* out() const { return _c2output; }
#define C2OUT (Thread::current()->is_C2Compiler_thread() ? ((C2CompilerThread*)Thread::current())->_compile->_c2output : tty)

outputStream*_c2output;
#ifndef PRODUCT
  static long _c2outputsize;
#endif
};

#endif // COMPILE_HPP
