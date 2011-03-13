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


#include "ad_pd.hpp"
#include "arguments.hpp"
#include "block.hpp"
#include "chaitin.hpp"
#include "callGenerator.hpp"
#include "callnode.hpp"
#include "cfgnode.hpp"
#include "ciEnv.hpp"
#include "ciTypeArrayKlass.hpp"
#include "compile.hpp"
#include "connode.hpp"
#include "copy.hpp"
#include "divnode.hpp"
#include "escape.hpp"
#include "globals.hpp"
#include "graphKit.hpp"
#include "interfaceSupport.hpp"
#include "loopnode.hpp"
#include "javaClasses.hpp"
#include "macro.hpp"
#include "memnode.hpp"
#include "mulnode.hpp"
#include "objArrayKlass.hpp"
#include "parse.hpp"
#include "phaseX.hpp"
#include "regalloc.hpp"
#include "rootnode.hpp"
#include "stubRoutines.hpp"
#include "vmThread.hpp"
#include "vectornode.hpp"

#include "atomic_os_pd.inline.hpp"
#include "auditTrail.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "thread_os.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"

/// Support for intrinsics.

// Return the index at which m must be inserted (or already exists).
// The sort order is by the address of the ciMethod, with is_virtual as minor key.
int Compile::intrinsic_insertion_index(ciMethod* m, bool is_virtual) {
#ifdef ASSERT
  for (int i = 1; i < _intrinsics->length(); i++) {
    CallGenerator* cg1 = _intrinsics->at(i-1);
    CallGenerator* cg2 = _intrinsics->at(i);
    assert(cg1->method() != cg2->method()
           ? cg1->method()     < cg2->method()
           : cg1->is_virtual() < cg2->is_virtual(),
           "compiler intrinsics list must stay sorted");
  }
#endif
  // Binary search sorted list, in decreasing intervals [lo, hi].
  int lo = 0, hi = _intrinsics->length()-1;
  while (lo <= hi) {
    int mid = (uint)(hi + lo) / 2;
    ciMethod* mid_m = _intrinsics->at(mid)->method();
    if (m < mid_m) {
      hi = mid-1;
    } else if (m > mid_m) {
      lo = mid+1;
    } else {
      // look at minor sort key
      bool mid_virt = _intrinsics->at(mid)->is_virtual();
      if (is_virtual < mid_virt) {
        hi = mid-1;
      } else if (is_virtual > mid_virt) {
        lo = mid+1;
      } else {
        return mid;  // exact match
      }
    }
  }
  return lo;  // inexact match
}

void Compile::register_intrinsic(CallGenerator* cg) {
  if (_intrinsics == NULL) {
    _intrinsics = new GrowableArray<CallGenerator*>(60);
  }
  // This code is stolen from ciObjectFactory::insert.
  // Really, GrowableArray should have methods for
  // insert_at, remove_at, and binary_search.
  int len = _intrinsics->length();
  int index = intrinsic_insertion_index(cg->method(), cg->is_virtual());
  if (index == len) {
    _intrinsics->append(cg);
  } else {
#ifdef ASSERT
    CallGenerator* oldcg = _intrinsics->at(index);
    assert(oldcg->method() != cg->method() || oldcg->is_virtual() != cg->is_virtual(), "don't register twice");
#endif
    _intrinsics->append(_intrinsics->at(len-1));
    int pos;
    for (pos = len-2; pos >= index; pos--) {
      _intrinsics->at_put(pos+1,_intrinsics->at(pos));
    }
    _intrinsics->at_put(index, cg);
  }
  assert(find_intrinsic(cg->method(), cg->is_virtual()) == cg, "registration worked");
}

CallGenerator* Compile::find_intrinsic(ciMethod* m, bool is_virtual) {
assert(FAM||m->is_loaded(),"don't try this on unloaded methods");
  if (_intrinsics != NULL) {
    int index = intrinsic_insertion_index(m, is_virtual);
    if (index < _intrinsics->length()
        && _intrinsics->at(index)->method() == m
        && _intrinsics->at(index)->is_virtual() == is_virtual) {
      return _intrinsics->at(index);
    }
  }
  // Lazily create intrinsics for intrinsic IDs well-known in the runtime.
  if (m->intrinsic_id() != vmIntrinsics::_none) {
    CallGenerator* cg = make_vm_intrinsic(m, is_virtual);
    if (cg != NULL) {
      // Save it for next time:
      register_intrinsic(cg);
      return cg;
    } else {
      gather_intrinsic_statistics(m->intrinsic_id(), is_virtual, _intrinsic_disabled);
    }
  }
  return NULL;
}

// Compile:: register_library_intrinsics and make_vm_intrinsic are defined
// in library_call.cpp.


#ifndef PRODUCT
// statistics gathering...

juint  Compile::_intrinsic_hist_count[vmIntrinsics::ID_LIMIT] = {0};
jubyte Compile::_intrinsic_hist_flags[vmIntrinsics::ID_LIMIT] = {0};

bool Compile::gather_intrinsic_statistics(vmIntrinsics::ID id, bool is_virtual, int flags) {
  assert(id > vmIntrinsics::_none && id < vmIntrinsics::ID_LIMIT, "oob");
  int oflags = _intrinsic_hist_flags[id];
  assert(flags != 0, "what happened?");
  if (is_virtual) {
    flags |= _intrinsic_virtual;
  }
  bool changed = (flags != oflags);
  if ((flags & _intrinsic_worked) != 0) {
    juint count = (_intrinsic_hist_count[id] += 1);
    if (count == 1) {
      changed = true;           // first time
    }
    // increment the overall count also:
    _intrinsic_hist_count[vmIntrinsics::_none] += 1;
  }
  if (changed) {
    if (((oflags ^ flags) & _intrinsic_virtual) != 0) {
      // Something changed about the intrinsic's virtuality.
      if ((flags & _intrinsic_virtual) != 0) {
        // This is the first use of this intrinsic as a virtual call.
        if (oflags != 0) {
          // We already saw it as a non-virtual, so note both cases.
          flags |= _intrinsic_both;
        }
      } else if ((oflags & _intrinsic_both) == 0) {
        // This is the first use of this intrinsic as a non-virtual
        flags |= _intrinsic_both;
      }
    }
    _intrinsic_hist_flags[id] = (jubyte) (oflags | flags);
  }
  // update the overall flags also:
  _intrinsic_hist_flags[vmIntrinsics::_none] |= (jubyte) flags;
  return changed;
}

static char* format_flags(int flags, char* buf) {
  buf[0] = 0;
  if ((flags & Compile::_intrinsic_worked) != 0)    strcat(buf, ",worked");
  if ((flags & Compile::_intrinsic_failed) != 0)    strcat(buf, ",failed");
  if ((flags & Compile::_intrinsic_disabled) != 0)  strcat(buf, ",disabled");
  if ((flags & Compile::_intrinsic_virtual) != 0)   strcat(buf, ",virtual");
  if ((flags & Compile::_intrinsic_both) != 0)      strcat(buf, ",nonvirtual");
  if (buf[0] == 0)  strcat(buf, ",");
  assert(buf[0] == ',', "must be");
  return &buf[1];
}

void Compile::print_intrinsic_statistics() {
  char flagsbuf[100];
  ttyLocker ttyl;
  tty->print_cr("Compiler intrinsic usage:");
  juint total = _intrinsic_hist_count[vmIntrinsics::_none];
  if (total == 0)  total = 1;  // avoid div0 in case of no successes
  #define PRINT_STAT_LINE(name, c, f) \
    tty->print_cr("  %4d (%4.1f%%) %s (%s)", (int)(c), ((c) * 100.0) / total, name, f);
  for (int index = 1 + (int)vmIntrinsics::_none; index < (int)vmIntrinsics::ID_LIMIT; index++) {
    vmIntrinsics::ID id = (vmIntrinsics::ID) index;
    int   flags = _intrinsic_hist_flags[id];
    juint count = _intrinsic_hist_count[id];
    if ((flags | count) != 0) {
      PRINT_STAT_LINE(vmIntrinsics::name_at(id), count, format_flags(flags, flagsbuf));
    }
  }
  PRINT_STAT_LINE("total", total, format_flags(_intrinsic_hist_flags[vmIntrinsics::_none], flagsbuf));
}

void Compile::print_statistics() {
  { ttyLocker ttyl;
    Parse::print_statistics();
    PhaseCCP::print_statistics();
    PhaseRegAlloc::print_statistics();
    //Scheduling::print_statistics();
    PhasePeephole::print_statistics();
    PhaseIdealLoop::print_statistics();
  }
  if (_intrinsic_hist_flags[vmIntrinsics::_none] != 0) {
    // put this under its own <statistics> element.
    print_intrinsic_statistics();
  }
}
#endif //PRODUCT

// Support for bundling info
Bundle* Compile::node_bundling(const Node *n) {
  assert(valid_bundle_info(n), "oob");
  return &_node_bundling_base[n->_idx];
}

bool Compile::valid_bundle_info(const Node *n) {
  return (_node_bundling_limit > n->_idx);
}


// Identify all nodes that are reachable from below, useful.
// Use breadth-first pass that records state in a Unique_Node_List,
// recursive traversal is slower.
void Compile::identify_useful_nodes(Unique_Node_List &useful) {
  int estimated_worklist_size = unique();
  useful.map( estimated_worklist_size, NULL );  // preallocate space

  // Initialize worklist
  if (root() != NULL)     { useful.push(root()); }
  // If 'top' is cached, declare it useful to preserve cached node
  if( cached_top_node() ) { useful.push(cached_top_node()); }

  // Push all useful nodes onto the list, breadthfirst
  for( uint next = 0; next < useful.size(); ++next ) {
    assert( next < unique(), "Unique useful nodes < total nodes");
    Node *n  = useful.at(next);
    uint max = n->len();
    for( uint i = 0; i < max; ++i ) {
      Node *m = n->in(i);
      if( m == NULL ) continue;
      useful.push(m);
    }
  }
}

// Disconnect all useless nodes by disconnecting those at the boundary.
void Compile::remove_useless_nodes(Unique_Node_List &useful) {
  uint next = 0;
  while( next < useful.size() ) {
    Node *n = useful.at(next++);
    // Use raw traversal of out edges since this code removes out edges 
    int max = n->outcnt();
    for (int j = 0; j < max; ++j ) {
      Node* child = n->raw_out(j);
      if( ! useful.member(child) ) {
        assert( !child->is_top() || child != top(),
                "If top is cached in Compile object it is in useful list");
        // Only need to remove this out-edge to the useless node
        n->raw_del_out(j);
        --j;
        --max;
      }
    }
    if (n->outcnt() == 1 && n->has_special_unique_user()) {
      record_for_igvn( n->unique_out() );
    }
    if (n->is_Allocate() && n->outcnt() < 3 ) record_for_igvn( n );
  }
  debug_only(verify_graph_edges(true/*check for no_dead_code*/);)
}

// ============================================================================
//------------------------------CompileWrapper---------------------------------
class CompileWrapper : public StackObj {
  Compile *const _compile;
 public:
  CompileWrapper(Compile* compile);

  ~CompileWrapper();
};
  
CompileWrapper::CompileWrapper(Compile* compile) : _compile(compile) {
  C2CompilerThread *c2 = C2CompilerThread::current();
  assert0( !c2->_compile );
  c2->_compile = compile;
  assert(compile == Compile::current(), "sanity");

  compile->set_type_dict(NULL);
  compile->set_type_last_size(0);
  compile->set_last_tf(NULL, NULL);
  compile->set_indexSet_arena(NULL);
  compile->set_indexSet_free_block_list(NULL);
  compile->init_type_arena();
  Type::Initialize(compile);
}
CompileWrapper::~CompileWrapper() {
  C2CompilerThread *c2 = C2CompilerThread::current();
  assert0( c2->_compile );
  c2->_compile = NULL;
}


//----------------------------print_compile_messages---------------------------
void Compile::print_compile_messages() {
  // Check if recompiling
  if (_subsume_loads == false && PrintOpto) {
    // Recompiling without allowing machine instructions to subsume loads
C2OUT->print_cr("*********************************************************");
C2OUT->print_cr("** Bailout: Recompile without subsuming loads          **");
C2OUT->print_cr("*********************************************************");
  }
  if (env()->break_at_compile()) {
    // Open the debugger when compiing this method.
C2OUT->print("### Breaking when compiling: ");
method()->print_short_name(C2OUT);
C2OUT->cr();
    BREAKPOINT;
  }

  if( PrintOpto ) {
    if (is_osr_compilation()) {
C2OUT->print("[OSR]%3d",_compile_id);
    } else {
C2OUT->print("%3d",_compile_id);
    }
  }
}


void  Compile::record_for_escape_analysis(Node* n) {
  if (_congraph != NULL)
    _congraph->record_for_escape_analysis(n);
}


// ============================================================================
//------------------------------Compile standard-------------------------------
debug_only( int Compile::_debug_idx = 100000; )
#ifndef PRODUCT
long Compile::_c2outputsize = 0;
#endif

// Compile a method.  entry_bci is -1 for normal compilations and indicates
// the continuation bci for on stack replacement.

Compile::Compile( ciEnv* ci_env, const C2Compiler* compiler, ciMethod* target, int osr_bci, bool subsume_loads, 
                  GrowableArray<const ciInstanceKlass*>* ciks,
                  GrowableArray<const ciMethod       *>* cms )
                : Phase(Compiler),
                  _env(ci_env),
                  _compile_id(ci_env->compile_id()),
                  _method(target),
                  _entry_bci(osr_bci),
                  _initial_gvn(NULL),
                  _for_igvn(NULL),
                  _warm_calls(NULL),
                  _subsume_loads(subsume_loads),
                  _failure_reason(NULL),
                  _node_bundling_limit(0),
                  _node_bundling_base(NULL),
#ifndef PRODUCT
                  _trace_opto_output(TraceOptoOutput || method()->has_option("TraceOptoOutput")),
#endif
_congraph(NULL),
                  _masm(comp_arena(),CodeBlob::c2, ci_env->compile_id(), strdup(target->name()->as_utf8()))
{

  outputStream* tout = LogCompilerOutput ? new (ResourceObj::C_HEAP) stringStream(false) : NULL;
  _c2output = tout ? tout : tty;
  CompileWrapper cw(this);
if(ciks!=NULL){
    const int len = ciks->length();
    for( int i=0; i<len; i++ ) {
      _masm._ciks.push(ciks->at(i));
      _masm._cms .push(cms ->at(i));
    }
  }
#ifndef PRODUCT
  if (TimeCompiler2) {
C2OUT->print(" ");
    target->holder()->name()->print(C2OUT);
C2OUT->print(".");
    target->print_short_name(C2OUT);
C2OUT->print("  ");
  }

  TraceTime t1("Total compilation time", &_t_totalCompilation, TimeCompiler, TimeCompiler2);
  TraceTime t2(NULL, &_t_methodCompilation, TimeCompiler, false);
#endif
  set_print_assembly(PrintOptoAssembly || _method->should_print_assembly());

  Init(::AliasLevel);

  print_compile_messages();

  if (UseOldInlining || PrintCompilation NOT_PRODUCT( || PrintOpto) )
    _ilt = InlineTree::build_inline_tree_root();
  else
    _ilt = NULL;

  // Even if NO memory addresses are used, MergeMem nodes must have at least 1 slice
  assert(num_alias_types() >= AliasIdxRaw, "");

#define MINIMUM_NODE_HASH  1023
  // Node list that Iterative GVN will start with
  Unique_Node_List for_igvn(comp_arena());
  set_for_igvn(&for_igvn);
  
  // GVN that will be run immediately on new nodes
  uint estimated_size = method()->code_size()*4+64;
  estimated_size = (estimated_size < MINIMUM_NODE_HASH ? MINIMUM_NODE_HASH : estimated_size);
  PhaseGVN gvn(node_arena(), estimated_size);
  set_initial_gvn(&gvn);

  if (DoEscapeAnalysis)
    _congraph = new ConnectionGraph(this);

  // See if we can find a CodeProfile lying around. 
  CodeProfile *codeprofile = method()->codeprofile(true);

if(codeprofile==NULL){
    // No cp yet, create a new one which will be attached to the mco at the end
codeprofile=CodeProfile::make(method());
  }

  if (!AllowEndlessDeopt && method()->bci2cpd_map()->deopt_count() >= method()->bci2cpd_map()->endless_deopt_count()) {
    record_failure("endlessly deopting", false);
  }

  NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_c2_in_use, 1);)
  _cp = codeprofile;

  if (CompileTheWorld && UseC1 && UseC2) {
    GUARDED_VM_ENTRY( _cp->artificially_populate_code_profile(_method->get_methodOop()); );
  }

if(method()->should_disable_inlining()){
C2OUT->print("Disabling inlining for method: ");
method()->print_short_name(C2OUT);
C2OUT->cr();
  }

  { // Scope for timing the parser
TraceTime t3("parse",&_t_parser,true);

    // Put top into the hash table ASAP.
    initial_gvn()->transform_no_reclaim(top());

    // Set up tf(), start(), and find a CallGenerator.
    CallGenerator* cg;
    if (is_osr_compilation()) {
      const TypeTuple *domain = StartOSRNode::osr_domain();
      const TypeTuple *range = TypeTuple::make_range(method()->signature());
      init_tf(TypeFunc::make(domain, range));
      StartNode* s = new (this, 2) StartOSRNode(root(), domain);
      initial_gvn()->set_type_bottom(s);
      init_start(s);
cg=CallGenerator::for_osr(method(),codeprofile,0,entry_bci());
    } else {
      // Normal case.
      init_tf(TypeFunc::make(method()));
      StartNode* s = new (this, 2) StartNode(root(), tf()->domain());
      initial_gvn()->set_type_bottom(s);
      init_start(s);
      int invoke_count = UseC1 ? method()->get_codeprofile_count(CodeProfile::_invoke) : method()->invocation_count();
      if( invoke_count == 0 ) invoke_count = 1;
      float past_uses = (float)invoke_count;
      float expected_uses = past_uses;
cg=CallGenerator::for_inline(method(),codeprofile,0,expected_uses);
    }
    if (failing())  return;
    if (cg == NULL) {
      record_method_not_compilable_all_tiers("cannot parse method");
      return;
    }
    JVMState* jvms = build_start_state(start(), tf());
    CPData_Invoke cpdi;         // Make a bogus invoke-profile for the top-level
    bzero(&cpdi,sizeof(cpdi));
    if ((jvms = cg->generate(jvms, &cpdi, false)) == NULL) {
      record_method_not_compilable("method parse failed");
      return;
    }
    GraphKit kit(jvms);

    if (!kit.stopped()) {
      // Accept return values, and transfer control we know not where.
      // This is done by a special, unique ReturnNode bound to root.
      return_values(kit.jvms());
    }

    if (kit.has_exceptions()) {
      // Any exceptions that escape from this call must be rethrown
      // to whatever caller is dynamically above us on the stack.
      // This is done by a special, unique RethrowNode bound to root.
      rethrow_exceptions(kit.transfer_exceptions_into_jvms());
    }

    // Remove clutter produced by parsing.
    if (!failing()) {
      ResourceMark rm;
      PhaseRemoveUseless pru(initial_gvn(), &for_igvn);
    }
  }

  // Note:  Large methods are capped off in do_one_bytecode().
  if (failing())  return;

  for (;;) {
    int successes = Inline_Warm();
    if (failing())  return;
    if (successes == 0)  break;
  }

  // Drain the list.
  Finish_Warm();
  if (failing())  return;
  NOT_PRODUCT( verify_graph_edges(); )

  // Perform escape analysis
  if (_congraph != NULL) {
    _congraph->compute_escape();
#ifndef PRODUCT
    if (PrintEscapeAnalysis) {
      _congraph->dump();
    }
#endif
  }
  // Now optimize
  Optimize();
  if (failing())  return;
  NOT_PRODUCT( verify_graph_edges(); )

#ifndef PRODUCT
  if (PrintIdeal) {
    root()->dump(9999);
  }
#endif

  // Now generate code
  Code_Gen();
  if (failing()) return;
  assert0( _root );

  if (FAM) {
    return;
  }

  if (LogCompilerOutput && _c2output!=tty) {
    codeprofile->set_debug_output((stringStream*)_c2output);
  }

  // Check if we want to skip execution of all compiled code.  
  {
#ifndef PRODUCT
    if (OptoNoExecute) {
      record_method_not_compilable("+OptoNoExecute");  // Flag as failed
      return;
    }
    TracePhase t2("install_code", &_t_registerMethod, TimeCompiler);
#endif

    assert0( !is_osr_compilation() || _first_block_size == 0 );
    Label do_not_use_this(&_masm);      // C1 needs a label here, but C2 does not
    env()->register_method(_method, _entry_bci,
                           &_masm, codeprofile,
in_ByteSize(_framesize_bytes),
                           do_not_use_this,
                           has_unsafe_access()
                           );
  }

  if (LogCompilerOutput && _c2output!=tty) {
_c2output->print_cr("+++ END OF OUTPUT +++");
    size_t newsize = _c2output->size_to_fit();
#ifndef PRODUCT
Atomic::add_ptr(newsize,&_c2outputsize);
#endif
  }
}

Compile::~Compile() {
  // Make sure that there is a cp allocated during compilation
  assert0(_cp);

  // current cp not attached to any MethodCodeOop - compilation must have
  // aborted and we need to free the cp here
  if (failing()) {
    _cp->free(CodeProfile::_c2_failure);
  }

for(int i=0;i<_num_cloned_cp;i++){
    NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_c2_cloned, -1);)
    if (_cloned_cp[i]) _cloned_cp[i]->free(CodeProfile::_c2_clone_freed);
  }

  NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_c2_in_use, -1);)
}

void Compile::print_codes() { 
  // _cp is the current one guiding the c2 compilation
  _method->print_codes(C2OUT, _cp);
}

//------------------------------Init-------------------------------------------
// Prepare for a single compilation
void Compile::Init(int aliaslevel) {
  _unique  = 0;
  _regalloc = NULL;

  _tf      = NULL;  // filled in later
  _top     = NULL;  // cached later
  _matcher = NULL;  // filled in later
  _cfg     = NULL;  // filled in later

  set_24_bit_selection_and_mode(Use24BitFP, false);

  _node_note_array = NULL;
  _default_node_notes = NULL;

  _immutable_memory = NULL; // filled in at first inquiry

  // Globally visible Nodes
  // First set TOP to NULL to give safe behavior during creation of RootNode
  set_cached_top_node(NULL);
  set_root(new (this, 3) RootNode());
  // Now that you have a Root to point to, create the real TOP
  set_cached_top_node( new (this, 1) ConNode(Type::TOP) );

  set_has_split_ifs(false);
  set_has_loops(has_method() && method()->has_loops()); // first approximation
  set_loops_found(false);
  _deopt_happens = true;  // start out assuming the worst
  _major_progress = true; // start out assuming good things will happen
  set_has_unsafe_access(false);
  _split_ctr = 0;

  // Compilation level related initialization
  set_num_loop_opts(LoopOptsCount);
  set_do_inlining (Inline);
set_max_inline_size(C2MaxInlineSize);
set_freq_inline_size(C2FreqInlineSize);

  // // -- Initialize types before each compile --
  // // Update cached type information
  // if( _method && _method->constants() ) 
  //   Type::update_loaded_types(_method, _method->constants());

  // Init alias_type map.
  if (!DoEscapeAnalysis && aliaslevel == 3)
    aliaslevel = 2;  // No unique types without escape analysis
  _AliasLevel = aliaslevel;
  const int grow_ats = 16;
  _max_alias_types = grow_ats;
  _alias_types   = NEW_ARENA_ARRAY(comp_arena(), AliasType*, grow_ats);
  AliasType* ats = NEW_ARENA_ARRAY(comp_arena(), AliasType,  grow_ats);
  Copy::zero_to_bytes(ats, sizeof(AliasType)*grow_ats);
  {
    for (int i = 0; i < grow_ats; i++)  _alias_types[i] = &ats[i];
  }

  // Init cloned cp array
  _max_cloned_cp = 4;
  _num_cloned_cp = 0;
  _cloned_cp   = NEW_ARENA_ARRAY(comp_arena(), CodeProfile*, _max_cloned_cp);
  Copy::zero_to_bytes(_cloned_cp, sizeof(CodeProfile*)*_max_cloned_cp);

  // Initialize the first few types.
  _alias_types[AliasIdxTop]->Init(AliasIdxTop, NULL);
  _alias_types[AliasIdxBot]->Init(AliasIdxBot, TypePtr::BOTTOM);
  _alias_types[AliasIdxRaw]->Init(AliasIdxRaw, TypeRawPtr::BOTTOM);
  _num_alias_types = AliasIdxRaw+1;
  // Zero out the alias type cache.
  Copy::zero_to_bytes(_alias_cache, sizeof(_alias_cache));
  // A NULL adr_type hits in the cache right away.  Preload the right answer.
  probe_alias_cache(NULL)->_index = AliasIdxTop;

  _intrinsics = NULL;
  register_library_intrinsics();
}

//---------------------------init_start----------------------------------------
// Install the StartNode on this compile object.
void Compile::init_start(StartNode* s) {
  if (failing())
    return; // already failing
  assert(s == start(), "");
}

StartNode* Compile::start() const {
  assert(!failing(), "");
  for (DUIterator_Fast imax, i = root()->fast_outs(imax); i < imax; i++) {
    Node* start = root()->fast_out(i);
    if( start->is_Start() ) 
      return start->as_Start();
  }
  ShouldNotReachHere();
  return NULL;
}

//-------------------------------immutable_memory-------------------------------------
// Access immutable memory
Node* Compile::immutable_memory() {
  if (_immutable_memory != NULL) {
    return _immutable_memory;
  }
  StartNode* s = start();
  for (DUIterator_Fast imax, i = s->fast_outs(imax); true; i++) {
    Node *p = s->fast_out(i);
    if (p != s && p->as_Proj()->_con == TypeFunc::Memory) {
      _immutable_memory = p;
      return _immutable_memory;
    }
  }
  ShouldNotReachHere();
  return NULL;
}

//----------------------set_cached_top_node------------------------------------
// Install the cached top node, and make sure Node::is_top works correctly.
void Compile::set_cached_top_node(Node* tn) {
  if (tn != NULL)  verify_top(tn);
  Node* old_top = _top;
  _top = tn;
  // Calling Node::setup_is_top allows the nodes the chance to adjust
  // their _out arrays.
  if (_top != NULL)     _top->setup_is_top();
  if (old_top != NULL)  old_top->setup_is_top();
  assert(_top == NULL || top()->is_top(), "");
}


#ifndef PRODUCT
void Compile::verify_top(Node* tn) const {
  if (tn != NULL) {
    assert(tn->is_Con(), "top node must be a constant");
    assert(((ConNode*)tn)->type() == Type::TOP, "top node must have correct type");
    assert(tn->in(0) != NULL, "must have live top node");
  }
}
#endif


///-------------------Managing Per-Node Debug & Profile Info-------------------

void Compile::grow_node_notes(GrowableArray<Node_Notes*>* arr, int grow_by) {
  guarantee(arr != NULL, "");
  int num_blocks = arr->length();
  if (grow_by < num_blocks)  grow_by = num_blocks;
  int num_notes = grow_by * _node_notes_block_size;
  Node_Notes* notes = NEW_ARENA_ARRAY(node_arena(), Node_Notes, num_notes);
  Copy::zero_to_bytes(notes, num_notes * sizeof(Node_Notes));
  while (num_notes > 0) {
    arr->append(notes);
    notes     += _node_notes_block_size;
    num_notes -= _node_notes_block_size;
  }
  assert(num_notes == 0, "exact multiple, please");
}

bool Compile::copy_node_notes_to(Node* dest, Node* source) {
  if (source == NULL || dest == NULL)  return false;

  if (dest->is_Con())
    return false;               // Do not push debug info onto constants.

#ifdef ASSERT
  // Leave a bread crumb trail pointing to the original node:
  if (dest != NULL && dest != source && dest->debug_orig() == NULL) {
    dest->set_debug_orig(source);
  }
#endif

  if (node_note_array() == NULL)
    return false;               // Not collecting any notes now.

  // This is a copy onto a pre-existing node, which may already have notes.
  // If both nodes have notes, do not overwrite any pre-existing notes.
  Node_Notes* source_notes = node_notes_at(source->_idx);
  if (source_notes == NULL || source_notes->is_clear())  return false;
  Node_Notes* dest_notes   = node_notes_at(dest->_idx);
  if (dest_notes == NULL || dest_notes->is_clear()) {
    return set_node_notes_at(dest->_idx, source_notes);
  }

  Node_Notes merged_notes = (*source_notes);
  // The order of operations here ensures that dest notes will win...
  merged_notes.update_from(dest_notes);
  return set_node_notes_at(dest->_idx, &merged_notes);
}


//------------------------------flatten_alias_type-----------------------------
const TypePtr *Compile::flatten_alias_type( const TypePtr *tj ) const {
  int offset = tj->offset();
  TypePtr::PTR ptr = tj->ptr();

  // Process weird unsafe references.
  if (offset == Type::OffsetBot && (tj->isa_instptr() /*|| tj->isa_klassptr()*/)) {
    assert(InlineUnsafeOps, "indeterminate pointers come only from unsafe ops");
tj=TypeInstPtr::BOTTOM;
    ptr = tj->ptr();
    offset = tj->offset();
  }

  // Array pointers need some flattening
  const TypeAryPtr *ta = tj->isa_aryptr();
  if( ta && _AliasLevel >= 2 ) {
    // For arrays indexed by constant indices, we flatten the alias
    // space to include all of the array body.  Only the header, klass
    // and array length can be accessed un-aliased.
    if( offset != Type::OffsetBot ) {
      if( ta->const_oop() ) { // methodDataOop or methodOop
        offset = Type::OffsetBot;   // Flatten constant access into array body
tj=ta=TypeAryPtr::make(ptr,ta->const_oop(),ta->ary(),ta->klass(),Type::OffsetBot);
      } else if( offset == arrayOopDesc::length_offset_in_bytes() ) {
        // range is OK as-is.
        tj = ta = TypeAryPtr::RANGE; 
      } else if( offset == oopDesc::mark_offset_in_bytes() ) {
        tj = TypeInstPtr::MARK;
        ta = TypeAryPtr::RANGE; // generic ignored junk
        ptr = TypePtr::BotPTR;
      } else {                  // Random constant offset into array body 
        offset = Type::OffsetBot;   // Flatten constant access into array body
tj=ta=TypeAryPtr::make(ptr,ta->ary(),ta->klass(),Type::OffsetBot);
      }
    }
    // Arrays of fixed size alias with arrays of unknown size.
    if (ta->size() != TypeInt::POS) {
      const TypeAry *tary = TypeAry::make(ta->elem(), TypeInt::POS);
tj=ta=TypeAryPtr::make(ptr,ta->const_oop(),tary,ta->klass(),offset);
    }
    // Arrays of known objects become arrays of unknown objects.
if((ta->elem()->isa_oopptr()&&ta->elem()!=TypeInstPtr::OBJECT)||
ta->elem()==TypePtr::NULL_PTR){
const TypeAry*tary=TypeAry::make(TypeInstPtr::OBJECT,ta->size());
tj=ta=TypeAryPtr::make(ptr,ta->const_oop(),tary,NULL,offset);
    }
    // Arrays of bytes and of booleans both use 'bastore' and 'baload' so
    // cannot be distinguished by bytecode alone.
    if (ta->elem() == TypeInt::BOOL) {
      const TypeAry *tary = TypeAry::make(TypeInt::BYTE, ta->size());
      ciKlass* aklass = ciTypeArrayKlass::make(T_BYTE);
tj=ta=TypeAryPtr::make(ptr,ta->const_oop(),tary,aklass,offset);
    }
    // During the 2nd round of IterGVN, NotNull castings are removed.
    // Make sure the Bottom and NotNull variants alias the same.
    // Also, make sure exact and non-exact variants alias the same.
    if( ptr == TypePtr::NotNull || ta->klass_is_exact() ) {
      if (ta->const_oop()) {
tj=ta=TypeAryPtr::make(TypePtr::Constant,ta->const_oop(),ta->ary(),ta->klass(),offset);
      } else {
tj=ta=TypeAryPtr::make(TypePtr::BotPTR,ta->ary(),ta->klass(),offset);
      }
    }
  }

  // Oop pointers need some flattening
  const TypeInstPtr *to = tj->isa_instptr();
if(to&&_AliasLevel>=2&&to!=TypeInstPtr::BOTTOM){
    // No constant oop pointers (such as Strings); they alias with unknown strings.

    // During the 2nd round of IterGVN, NotNull castings are removed.
    // Make sure the Bottom and NotNull variants alias the same.

    // Make sure exact and non-exact variants alias the same.  
    // This means no "extra" interfaces beyond what the exact class implements.

    ciInstanceKlass *k = to->klass()->as_instance_klass();
    tj = to = TypeInstPtr::make(TypePtr::BotPTR,k,false,0,offset,k->transitive_interfaces());

    // Canonicalize the holder of this field
    if (offset >= 0 && offset < oopDesc::header_size() * wordSize) {
      // First handle header references such as a LoadKlassNode, even if the
      // object's klass is unloaded at compile time (4965979).
tj=to=TypeInstPtr::make(TypePtr::BotPTR,env()->Object_klass(),false,NULL,offset/*, to->instance_id()*/);
    } else if (offset < 0 || offset >= k->size_helper() * wordSize) {
      to = NULL;
tj=TypeInstPtr::BOTTOM;
      offset = tj->offset();
    } else {
      ciInstanceKlass *canonical_holder = k->get_canonical_holder(offset);
      if (!k->equals(canonical_holder) || tj->offset() != offset) {
tj=to=TypeInstPtr::make(TypePtr::BotPTR,canonical_holder,false,NULL,offset/*, to->instance_id()*/);
      }
    }
  }

  // Klass pointers to object array klasses need some flattening
  const TypeKlassPtr *tk = tj->isa_klassptr();
  if( tk ) {
    // If we are referencing a field within a Klass, we need
    // to assume the worst case of an Object.  Both exact and
    // inexact types must flatten to the same alias class.
    // Since the flattened result for a klass is defined to be 
    // precisely java.lang.Object, use a constant ptr.
    if ( offset == Type::OffsetBot || (offset >= 0 && (size_t)offset < sizeof(Klass)) ) {

tj=tk=TypeKlassPtr::make(TypePtr::NotNull,
                                   TypeKlassPtr::OBJECT->klass(),
                                   offset);
    }

    ciKlass* klass = tk->klass();
    if( klass->is_obj_array_klass() ) {
      ciKlass* k = TypeAryPtr::OOPS->klass();
      if( !k || !k->is_loaded() )                  // Only fails for some -Xcomp runs
k=TypeInstPtr::OBJECT->klass();
      tj = tk = TypeKlassPtr::make( TypePtr::NotNull, k, offset );
    }

    // Check for precise loads from the primary supertype array and force them
    // to the supertype cache alias index.  Check for generic array loads from
    // the primary supertype array and also force them to the supertype cache
    // alias index.  Since the same load can reach both, we need to merge
    // these 2 disparate memories into the same alias class.  Since the
    // primary supertype array is read-only, there's no chance of confusion
    // where we bypass an array load and an array store.
uint off2=offset-(sizeof(oopDesc)+Klass::primary_supers_kids_offset_in_bytes());
    if( offset == Type::OffsetBot ||
off2<Klass::primary_super_limit()*sizeof(juint)){
offset=sizeof(oopDesc)+Klass::secondary_super_kid_cache_offset_in_bytes();
      tj = tk = TypeKlassPtr::make( TypePtr::NotNull, tk->klass(), offset );
    }
  }

  // Flatten all Raw pointers together.
  if (tj->base() == Type::RawPtr)
    tj = TypeRawPtr::BOTTOM;

if(tj->base()==Type::AnyPtr){
    if( ptr == TypePtr::Null ) return tj; // Allow "NULL+32" style stuff for (soon to be) dead memory ops
    tj = TypePtr::BOTTOM;      // An error, which the caller must check for.
  }

  // Flatten all to bottom for now
  switch( _AliasLevel ) {
  case 0: 
    tj = TypePtr::BOTTOM; 
    break;
  case 1:                       // Flatten to: oop, static, field or array
    switch (tj->base()) {
    //case Type::AryPtr: tj = TypeAryPtr::RANGE;    break;
    case Type::RawPtr:   tj = TypeRawPtr::BOTTOM;   break;
    case Type::AryPtr:   // do not distinguish arrays at all
case Type::InstPtr:tj=TypeInstPtr::OBJECT;break;
    case Type::KlassPtr: tj = TypeKlassPtr::OBJECT; break;
    case Type::AnyPtr:   tj = TypePtr::BOTTOM;      break;  // caller checks it
    default: ShouldNotReachHere();
    }
    break;
  case 2:                       // No collasping at level 2; keep all splits
  case 3:                       // No collasping at level 3; keep all splits
    break;
  default:
    Unimplemented();
  } 

  offset = tj->offset();
  assert( offset != Type::OffsetTop, "Offset has fallen from constant" );
  
  assert( (offset != Type::OffsetBot && tj->base() != Type::AryPtr) ||
          (offset == Type::OffsetBot && tj->base() == Type::AryPtr) ||
(offset==Type::OffsetBot&&tj->base()==Type::OopPtr)||
(offset==Type::OffsetBot&&tj==TypeInstPtr::BOTTOM)||
          (offset == Type::OffsetBot && tj == TypePtr::BOTTOM) ||
(offset==Type::OffsetBot&&tj==TypeRawPtr::BOTTOM)||
          (offset == oopDesc::mark_offset_in_bytes() && tj->base() == Type::AryPtr) ||
          (offset == arrayOopDesc::length_offset_in_bytes() && tj->base() == Type::AryPtr)  , 
          "For oops, klasses, raw offset must be constant; for arrays the offset is never known" );
  assert( tj->ptr() != TypePtr::TopPTR &&
          tj->ptr() != TypePtr::AnyNull &&          
          tj->ptr() != TypePtr::Null, "No imprecise addresses" );

  return tj;
}

void Compile::AliasType::Init(int i, const TypePtr* at) {
  _index = i;
  _adr_type = at;
  _field = NULL;
  _is_rewritable = true; // default
  const TypeOopPtr *atoop = (at != NULL) ? at->isa_oopptr() : NULL;
  if (atoop != NULL && atoop->is_instance()) {
    const TypeOopPtr *gt = atoop->cast_to_instance(TypeOopPtr::UNKNOWN_INSTANCE);
    _general_index = Compile::current()->get_alias_index(gt);
  } else {
    _general_index = 0;
  }
}

//---------------------------------print_on------------------------------------
#ifndef PRODUCT
void Compile::AliasType::print_on(outputStream* st) {
  if (index() < 10)
        st->print("@ <%d> ", index());
  else  st->print("@ <%d>",  index());
  st->print(is_rewritable() ? "   " : " RO");
  if( !adr_type() ) { st->print(" NULL"); return; }
  int offset = adr_type()->offset();
  if (offset == Type::OffsetBot)
        st->print(" +any");
  else  st->print(" +%-3d", offset);
  st->print(" in ");
  adr_type()->dump_on(st);
  const TypeOopPtr* tjp = adr_type()->isa_oopptr();
  if (field() != NULL && tjp) {
    if (tjp->klass()  != field()->holder() ||
        tjp->offset() != field()->offset_in_bytes()) {
      st->print(" != ");
      field()->print();
      st->print(" ***");
    }
  }
}

void print_alias_types() {
  Compile* C = Compile::current();
C2OUT->print_cr("--- Alias types, AliasIdxBot .. %d",C->num_alias_types()-1);
  for (int idx = Compile::AliasIdxBot; idx < C->num_alias_types(); idx++) {
C->alias_type(idx)->print_on(C2OUT);
C2OUT->cr();
  }
}
#endif


//----------------------------probe_alias_cache--------------------------------
Compile::AliasCacheEntry* Compile::probe_alias_cache(const TypePtr* adr_type) {
  intptr_t key = (intptr_t) adr_type;
  key ^= key >> logAliasCacheSize;
  return &_alias_cache[key & right_n_bits(logAliasCacheSize)];
}


//-----------------------------grow_alias_types--------------------------------
void Compile::grow_alias_types() {
  const int old_ats  = _max_alias_types; // how many before?
  const int new_ats  = old_ats;          // how many more?
  const int grow_ats = old_ats+new_ats;  // how many now?
  _max_alias_types = grow_ats;
  _alias_types =  REALLOC_ARENA_ARRAY(comp_arena(), AliasType*, _alias_types, old_ats, grow_ats);
  AliasType* ats =    NEW_ARENA_ARRAY(comp_arena(), AliasType, new_ats);
  Copy::zero_to_bytes(ats, sizeof(AliasType)*new_ats);
  for (int i = 0; i < new_ats; i++)  _alias_types[old_ats+i] = &ats[i];
}


//--------------------------------find_alias_type------------------------------
Compile::AliasType* Compile::find_alias_type(const TypePtr* adr_type, bool no_create) {
  if (_AliasLevel == 0)
    return alias_type(AliasIdxBot);

  AliasCacheEntry* ace = probe_alias_cache(adr_type);
  if (ace->_adr_type == adr_type) {
    return alias_type(ace->_index);
  }

  // Handle special cases.
  if (adr_type == NULL)             return alias_type(AliasIdxTop);
  if (adr_type == TypePtr::BOTTOM)  return alias_type(AliasIdxBot);

  // Do it the slow way.
  const TypePtr* flat = flatten_alias_type(adr_type);

#ifdef ASSERT
  assert(flat == flatten_alias_type(flat), "idempotent");
  assert(flat != TypePtr::BOTTOM,     "cannot alias-analyze an untyped ptr");
  if (flat->isa_oopptr() && !flat->isa_klassptr()) {
    const TypeOopPtr* foop = flat->is_oopptr();
    const TypePtr* xoop = foop->cast_to_exactness(!foop->klass_is_exact())->is_ptr();
    assert(foop == flatten_alias_type(xoop), "exactness must not affect alias type");
  }
  assert(flat == flatten_alias_type(flat), "exact bit doesn't matter");
#endif

  int idx = AliasIdxTop;
  for (int i = 0; i < num_alias_types(); i++) {
    if (alias_type(i)->adr_type() == flat) {
      idx = i;
      break;
    }
  }

  if (idx == AliasIdxTop) {
    if (no_create)  return NULL;
    // Grow the array if necessary.
    if (_num_alias_types == _max_alias_types)  grow_alias_types();
    // Add a new alias type.
    idx = _num_alias_types++;
    _alias_types[idx]->Init(idx, flat);
    if (flat == TypeAryPtr::RANGE)   alias_type(idx)->set_rewritable(false);
    if (flat->isa_instptr()) {
      if (flat->offset() == java_lang_Class::klass_offset_in_bytes()
          && flat->is_instptr()->klass() == env()->Class_klass())
        alias_type(idx)->set_rewritable(false);
    }
    if (flat->isa_klassptr()) {
      if (flat->offset() == Klass::super_check_offset_offset_in_bytes() + (int)sizeof(oopDesc))
        alias_type(idx)->set_rewritable(false);
      if (flat->offset() == Klass::modifier_flags_offset_in_bytes() + (int)sizeof(oopDesc))
        alias_type(idx)->set_rewritable(false);
      if (flat->offset() == Klass::access_flags_offset_in_bytes() + (int)sizeof(oopDesc))
        alias_type(idx)->set_rewritable(false);
      if (flat->offset() == Klass::java_mirror_offset_in_bytes() + (int)sizeof(oopDesc))
        alias_type(idx)->set_rewritable(false);
    }
    // %%% (We would like to finalize JavaThread::threadObj_offset(),
    // but the base pointer type is not distinctive enough to identify
    // references into JavaThread.)

    // Check for final instance fields.
    const TypeInstPtr* tinst = flat->isa_instptr();
    if (tinst && tinst->offset() >= oopDesc::header_size() * wordSize) {
      ciInstanceKlass *k = tinst->klass()->as_instance_klass();
      ciField* field = k->get_field_by_offset(tinst->offset(), false);
      // Set field() and is_rewritable() attributes.
      if (field != NULL)  alias_type(idx)->set_field(field);
    }
    const TypeKlassPtr* tklass = flat->isa_klassptr();
    // Check for final static fields.
    if (tklass && tklass->klass()->is_instance_klass()) {
      ciInstanceKlass *k = tklass->klass()->as_instance_klass();
      ciField* field = k->get_field_by_offset(tklass->offset(), true);
      // Set field() and is_rewritable() attributes.
if(field!=NULL){
        alias_type(idx)->set_field(field);
        ciInstanceKlass *field_holder = field->holder();
        if( method()->is_static() &&
            method()->holder()->is_subclass_of(field_holder) ) {
          if (method()->name() == ciSymbol::class_initializer_name()) {
            alias_type(idx)->set_rewritable(true);
          }
        }
      }
    }
  }

  // Fill the cache for next time.
  ace->_adr_type = adr_type;
  ace->_index    = idx;
  assert(alias_type(adr_type) == alias_type(idx),  "type must be installed");

  // Might as well try to fill the cache for the flattened version, too.
  AliasCacheEntry* face = probe_alias_cache(flat);
  if (face->_adr_type == NULL) {
    face->_adr_type = flat;
    face->_index    = idx;
    assert(alias_type(flat) == alias_type(idx), "flat type must work too");
  }

  return alias_type(idx);
}


Compile::AliasType* Compile::alias_type(ciField* field) {
  const TypeOopPtr* t;
  if (field->is_static())
    t = TypeKlassPtr::make(field->holder());
  else
t=TypeOopPtr::make_from_klass_raw(field->holder())->is_oopptr();
  AliasType* atp = alias_type(t->add_offset(field->offset_in_bytes()));
assert(field->is_final()==!atp->is_rewritable()||
         (field->is_final() &&  // Final fields are re-writable in clinits
          method()->is_static() &&
          method()->holder()->is_subclass_of(field->holder()) &&
          method()->is_initializer()),
"must get the rewritable bits correct");
  return atp;
}


//------------------------------have_alias_type--------------------------------
bool Compile::have_alias_type(const TypePtr* adr_type) {
  AliasCacheEntry* ace = probe_alias_cache(adr_type);
  if (ace->_adr_type == adr_type) {
    return true;
  }

  // Handle special cases.
  if (adr_type == NULL)             return true;
  if (adr_type == TypePtr::BOTTOM)  return true;

  return find_alias_type(adr_type, true) != NULL;
}

//-----------------------------must_alias--------------------------------------
// True if all values of the given address type are in the given alias category.
bool Compile::must_alias(const TypePtr* adr_type, int alias_idx) {
  if (alias_idx == AliasIdxBot)         return true;  // the universal category
  if (adr_type == NULL)                 return true;  // NULL serves as TypePtr::TOP
  if (alias_idx == AliasIdxTop)         return false; // the empty category
  if (adr_type->base() == Type::AnyPtr) return false; // TypePtr::BOTTOM or its twins

  // the only remaining possible overlap is identity
  int adr_idx = get_alias_index(adr_type);
  assert(adr_idx != AliasIdxBot && adr_idx != AliasIdxTop, "");
  assert(adr_idx == alias_idx ||
(alias_type(alias_idx)->adr_type()!=TypeInstPtr::BOTTOM
&&adr_type!=TypeInstPtr::BOTTOM),
         "should not be testing for overlap with an unsafe pointer");
  return adr_idx == alias_idx;
}

//------------------------------can_alias--------------------------------------
// True if any values of the given address type are in the given alias category.
bool Compile::can_alias(const TypePtr* adr_type, int alias_idx) {
  if (alias_idx == AliasIdxTop)         return false; // the empty category
  if (adr_type == NULL)                 return false; // NULL serves as TypePtr::TOP
  if (alias_idx == AliasIdxBot)         return true;  // the universal category
  if (adr_type->base() == Type::AnyPtr) return true;  // TypePtr::BOTTOM or its twins

  // the only remaining possible overlap is identity
  int adr_idx = get_alias_index(adr_type);
  assert(adr_idx != AliasIdxBot && adr_idx != AliasIdxTop, "");
  return adr_idx == alias_idx;
}



//---------------------------pop_warm_call-------------------------------------
WarmCallInfo* Compile::pop_warm_call() {
  WarmCallInfo* wci = _warm_calls;
  if (wci != NULL)  _warm_calls = wci->remove_from(wci);
  return wci;
}

//----------------------------Inline_Warm--------------------------------------
int Compile::Inline_Warm() {
  // If there is room, try to inline some more warm call sites.
  // %%% Do a graph index compaction pass when we think we're out of space?
  if (!InlineWarmCalls)  return 0;

  int calls_made_hot = 0;
  int room_to_grow   = NodeCountInliningCutoff - unique();
  int amount_to_grow = MIN2(room_to_grow, (int)NodeCountInliningStep);
  int amount_grown   = 0;
  WarmCallInfo* call;
  while (amount_to_grow > 0 && (call = pop_warm_call()) != NULL) {
    int est_size = (int)call->size();
    if (est_size > (room_to_grow - amount_grown)) {
      // This one won't fit anyway.  Get rid of it.
      call->make_cold();
      continue;
    }
    call->make_hot();
    calls_made_hot++;
    amount_grown   += est_size;
    amount_to_grow -= est_size;
  }

  if (calls_made_hot > 0)  set_major_progress();
  return calls_made_hot;
}


//----------------------------Finish_Warm--------------------------------------
void Compile::Finish_Warm() {
  if (!InlineWarmCalls)  return;
  if (failing())  return;
  if (warm_calls() == NULL)  return;

  // Clean up loose ends, if we are out of space for inlining.
  WarmCallInfo* call;
  while ((call = pop_warm_call()) != NULL) {
    call->make_cold();
  }
}


//------------------------------Optimize---------------------------------------
// Given a graph, optimize it.
void Compile::Optimize() {
TraceTime t1("optimizer",&_t_optimizer,true);

#ifndef PRODUCT
  if (env()->break_at_compile()) {
    BREAKPOINT;
  }

#endif

  int          loop_opts_cnt=0;

  NOT_PRODUCT( verify_graph_edges(); )

 {
  // Iterative Global Value Numbering, including ideal transforms
  // Initialize IterGVN with types and values from parse-time GVN
  PhaseIterGVN igvn(initial_gvn());
  {
    NOT_PRODUCT( TracePhase t2("iterGVN", &_t_iterGVN, TimeCompiler); )
    igvn.optimize();
  }
  if (failing())  return;

  // get rid of the connection graph since it's information is not
  // updated by optimizations
  _congraph = NULL;


  // Loop transforms on the ideal graph.  Range Check Elimination,
  // peeling, unrolling, etc.
  bool should_disable_loopopts = (DisableLoopOptimizations || method()->should_disable_loopopts());
  if (!DisableLoopOptimizations && should_disable_loopopts) {
C2OUT->print("Disabling loopopts for method: ");
method()->print_short_name(C2OUT);
C2OUT->cr();
  }

  if (!should_disable_loopopts) {
    // Set loop opts counter
    loop_opts_cnt = num_loop_opts();
    if((loop_opts_cnt > 0) && (has_loops() || has_split_ifs())) {
      {
        TracePhase t2("idealLoop", &_t_idealLoop, true);
PhaseIdealLoop ideal_loop(igvn);
ideal_loop.do_loop_opts(NULL,true,false);
        loop_opts_cnt--;
        if (failing())  return;
      }
      // Loop opts pass if partial peeling occurred in previous pass
      if(PartialPeelLoop && major_progress() && (loop_opts_cnt > 0)) {
        TracePhase t3("idealLoop", &_t_idealLoop, true);
PhaseIdealLoop ideal_loop(igvn);
        ideal_loop.do_loop_opts( NULL, false, false);
        loop_opts_cnt--;
        if (failing())  return;
      }
      // Loop opts pass for loop-unrolling before CCP
      if(major_progress() && (loop_opts_cnt > 0)) {
        TracePhase t4("idealLoop", &_t_idealLoop, true);
PhaseIdealLoop ideal_loop(igvn);
        ideal_loop.do_loop_opts( NULL, false, false);
        loop_opts_cnt--;
        if (failing())  return;
      }
    }
  }

  // Conditional Constant Propagation;
  PhaseCCP ccp( &igvn ); 
  assert( true, "Break here to ccp.dump_nodes_and_types(_root,999,1)");
  {
TraceTime t2("ccp",&_t_ccp,true);
    ccp.do_transform();
  }
  assert( true, "Break here to ccp.dump_old2new_map()");
    
  // Iterative Global Value Numbering, including ideal transforms
  {
    NOT_PRODUCT( TracePhase t2("iterGVN2", &_t_iterGVN2, TimeCompiler); )
    igvn = ccp;
    igvn.optimize();
  }
  if (failing())  return;

  if (!should_disable_loopopts) {
    // Loop transforms on the ideal graph.  Range Check Elimination,
    // peeling, unrolling, etc.
    if(loop_opts_cnt > 0) {
int cnt=0;//Force a minimum number of go-'rounds for small methods
      while((major_progress() || cnt<2) && (loop_opts_cnt > 0)) {      
        TracePhase t2("idealLoop", &_t_idealLoop, true);
assert(cnt<40,"infinite cycle in loop optimization");
cnt++;
PhaseIdealLoop ideal_loop(igvn);
ideal_loop.do_loop_opts(NULL,true,true);
        loop_opts_cnt--;
        if (failing())  return;
      }
    }
  }
  {
    NOT_PRODUCT( TracePhase t2("macroExpand", &_t_macroExpand, TimeCompiler); )
    PhaseMacroExpand  mex(igvn);
    if (mex.expand_macro_nodes()) {
      assert(failing(), "must bail out w/ explicit message");
      return;
    }
  }

 } // (End scope of igvn; run destructor if necessary for asserts.)

  // A method with only infinite loops has no edges entering loops from root
  {
    NOT_PRODUCT( TracePhase t2("graphReshape", &_t_graphReshaping, TimeCompiler); )
    if (final_graph_reshaping()) {
      assert(failing(), "must bail out w/ explicit message");
      return;
    }
  }
}


//------------------------------Code_Gen---------------------------------------
// Given a graph, generate code for it
void Compile::Code_Gen() {
  if (failing())  return;

  // Perform instruction selection.  You might think we could reclaim Matcher
  // memory PDQ, but actually the Matcher is used in generating spill code.
  // Internals of the Matcher (including some VectorSets) must remain live
  // for awhile - thus I cannot reclaim Matcher memory lest a VectorSet usage
  // set a bit in reclaimed memory.

  // In debug mode can dump m._nodes.dump() for mapping of ideal to machine
  // nodes.  Mapping is only valid at the root of each matched subtree.
  NOT_PRODUCT( verify_graph_edges(); )

  Node_List proj_list;
  Matcher m(proj_list);
  _matcher = &m;
  {
TraceTime t2("matcher",&_t_matcher,true);
    m.match();
  }
  // In debug mode can dump m._nodes.dump() for mapping of ideal to machine
  // nodes.  Mapping is only valid at the root of each matched subtree.
  NOT_PRODUCT( verify_graph_edges(); )

  // If you have too many nodes, or if matching has failed, bail out
  check_node_count(0, "out of nodes matching instructions");
  if (failing())  return;

  // Build a proper-looking CFG
  PhaseCFG cfg(node_arena(), root(), m);
  _cfg = &cfg;
  {
    NOT_PRODUCT( TracePhase t2("scheduler", &_t_scheduler, TimeCompiler); )
    cfg.Dominators();
    if (failing())  return;

    NOT_PRODUCT( verify_graph_edges(); )

    cfg.Estimate_Block_Frequency();
    cfg.GlobalCodeMotion(m,unique(),proj_list);

    if (failing())  return;
    NOT_PRODUCT( verify_graph_edges(); )

    debug_only( cfg.verify(); )
  }
  NOT_PRODUCT( verify_graph_edges(); )

  PhaseChaitin regalloc(unique(),cfg,m);
  _regalloc = &regalloc;
  {
TraceTime t2("regalloc",&_t_registerAllocation,true);
    // Perform any platform dependent preallocation actions.  This is used,
    // for example, to avoid taking an implicit null pointer exception
    // using the frame pointer on win95.
    _regalloc->pd_preallocate_hook();

    // Perform register allocation.  After Chaitin, use-def chains are
    // no longer accurate (at spill code) and so must be ignored.
    // Node->LRG->reg mappings are still accurate.
    _regalloc->Register_Allocate();

    // Perform any platform dependent preallocation actions.  This is used,   
    // for example, to renumber stack spills such that push/pop on X86 work.
_regalloc->pd_postallocate_hook();

    // Bail out if the allocator builds too many nodes
    if (failing())  return;
  }

  // Prior to register allocation we kept empty basic blocks in case the
  // the allocator needed a place to spill.  After register allocation we 
  // are not adding any new instructions.  If any basic block is empty, we 
  // can now safely remove it.
  {
    NOT_PRODUCT( TracePhase t2("removeEmpty", &_t_removeEmptyBlocks, TimeCompiler); )
    cfg.RemoveEmpty();
  }

  // Perform any platform dependent postallocation verifications.
  debug_only( _regalloc->pd_postallocate_verify_hook(); )

  // Apply peephole optimizations
  if( OptoPeephole ) {
    NOT_PRODUCT( TracePhase t2("peephole", &_t_peephole, TimeCompiler); )
    PhasePeephole peep( _regalloc, cfg);
    peep.do_transform();
  }

  // Convert Nodes to instruction bits in a buffer
  {
    // %%%% workspace merge brought two timers together for one job
TraceTime t2a("output",&_t_output,true);
    NOT_PRODUCT( TraceTime t2b(NULL, &_t_codeGeneration, TimeCompiler, false); )
    Output();
  }

  // He's dead, Jim.
  _framesize_bytes = _regalloc->_framesize<<3; // copy out before exiting scope
  _cfg     = (PhaseCFG*)0xdeadbeef;
  _regalloc= (PhaseChaitin*)0xdeadbeef;
}


//------------------------------dump_asm---------------------------------------
// Dump formatted assembly
#ifndef PRODUCT
void Compile::dump_asm(int *pcs, uint pc_limit) {
C2OUT->print_cr("#");
C2OUT->print("#  ");_tf->dump();C2OUT->cr();
C2OUT->print_cr("#");
  _regalloc->dump_frame();
  _masm.decode(C2OUT);
}
#endif

// register information defined by ADLC
extern const char register_save_policy[];
extern const int  register_save_type[];

//------------------------------Final_Reshape_Counts---------------------------
// This class defines counters to help identify when a method 
// may/must be executed using hardware with only 24-bit precision.
struct Final_Reshape_Counts : public StackObj {
  int  _call_count;             // count non-inlined 'common' calls 
  int  _float_count;            // count float ops requiring 24-bit precision
  int  _double_count;           // count double ops requiring more precision
  int  _java_call_count;        // count non-inlined 'java' calls 
  VectorSet _visited;           // Visitation flags
VectorSet _post_visited;//Visitation flags
  Node_List _tests;             // Set of IfNodes & PCTableNodes

  Final_Reshape_Counts() : 
    _call_count(0), _float_count(0), _double_count(0), _java_call_count(0),
_visited(Thread::current()->resource_area()),
_post_visited(Thread::current()->resource_area()){}

  void inc_call_count  () { _call_count  ++; }
  void inc_float_count () { _float_count ++; }
  void inc_double_count() { _double_count++; }
  void inc_java_call_count() { _java_call_count++; }

  int  get_call_count  () const { return _call_count  ; }
  int  get_float_count () const { return _float_count ; }
  int  get_double_count() const { return _double_count; }
  int  get_java_call_count() const { return _java_call_count; }
};
static void final_graph_reshaping_walk( Compile*C, Node_Stack &, Node *n, Final_Reshape_Counts &fpu );

static bool oop_offset_is_sane(const TypeInstPtr* tp) {
  ciInstanceKlass *k = tp->klass()->as_instance_klass();
  // Make sure the offset goes inside the instance layout.
  return (uint)tp->offset() < (uint)(oopDesc::header_size() + k->nonstatic_field_size())*wordSize;
  // Note that OffsetBot and OffsetTop are very negative.
}

// The inputs to this Phi all have the same opcode.
// Attempt some collapse.
static Node*do_tail_cse(Node*phi){
  Node *r = phi->in(0);         // Phis controlling region
  Node *z = phi->in(1)->clone();// Clone a sample replacement guy
  if( z->in(0) ) {              // If a sample guy has a control edge THEN
    z->set_req(0,r);            // the replacement gets a control edge from the merge
    for( uint j=1; j<r->req(); j++ ) // Kill off inputs of CSE'd nodes
      phi->in(j)->set_req(0,NULL);
  }
  // Now make all the inputs to z make sense
for(uint i=1;i<z->req();i++){
    // If all the inputs to X[i] are equal, set Z[i] to that value.
    // If they differ, make a Phi (or use an existing one if possible).
Node*sample=phi->in(1)->in(i);
    uint j;
for(j=2;j<r->req();j++)
      if( phi->in(j)->in(i) != sample )
        break;                  // Oops, some inputs differ, need a Phi
    // If all inputs are the same, then the clone has the correct input edge already
    if( j != r->req() ) {       // Hard: need a Phi (and one may pre-exist)
      TypeNode *nphi = PhiNode::make(r,sample);
      for( ; j<r->req(); j++ ) {
        sample = phi->in(j)->in(i);
nphi->set_req(j,sample);
nphi->set_type(nphi->type()->meet(sample->bottom_type()));
      }
      if( nphi->type()->singleton() ) { // Rarely, see a constant here, eagerly fold Phi
        nphi = ConNode::make(Compile::current(), nphi->type());
      } else {
        // Must use a prior Phi if one exists
        for( DUIterator_Fast rmax, rx = r->fast_outs(rmax); rx < rmax; rx++ ) {
Node*p=r->fast_out(rx);
          if( !p->is_Phi() ) continue;
          if( p == nphi ) continue;
for(j=1;j<r->req();j++)
            if( p->in(j) != nphi->in(j) )
              break;
if(j==r->req()){//Found a matching existing Phi
            nphi->destruct();     // Reclaim the temp new phi
            nphi = p->as_Phi();
            break;                
          }
        }
      }
      z->set_req(i,nphi);       // Set the proper merged value into z
    } // Else z already had a correct input
    for( j=1; j<r->req(); j++ ) // Kill off inputs of CSE'd nodes
      if( phi->in(j)->outcnt() == 1 )
        phi->in(j)->set_req(i,NULL);
  } // For all inputs to z
  phi->replace_by(z);           // Now insert z into the graph
for(uint i=0;i<phi->req();i++)
phi->set_req(i,NULL);//Aggressively kill phi
  return z;
}

// See if Node x matches Node a close enough for tail-merging
static bool match_tail_cse( Node *x, int aop, Node *a, Node *adr, Node *load_phi_alias, Node *ctrl ) {
  return 
    x->req() == a->req() &&     // Same input count
    x->Opcode() == aop &&       // Same opcode
    (x->cmp(*a) || x->is_Store() || x->is_MemBar()) && // Same "cmp" - handle constants
    (!x->in(0) || x->in(0) == ctrl ) && // Same control input
    (x->outcnt() == 1 || aop == Op_AddP) && // Must have a single local user (the Phi) or be willing to clone
    // Also require addresses be equal so the memory op will
    // fold up base+offset forms.
    (!adr || x->in(MemNode::Address) == adr) &&
    (!load_phi_alias || !x->is_Load() || x->in(MemNode::Memory) == load_phi_alias);
}

static bool fold_phi( PhiNode *u ) {
if(u->outcnt()>0){
Node*n=u->in(1);
for(uint i=2;i<u->req();i++)
      if( u->in(i) != n )    // Found more than 1 unique input to Phi?
        return false;           // Did NOT fold phi
    u->replace_by(n);
  }
for(uint i=0;i<u->req();i++)
u->set_req(i,NULL);//Aggressively kill phi
  return true;                  // Did fold phi
}

static bool check_tail_cse( PhiNode *phi, Node_Stack &nstack ) {
  Node *r = phi->in(0);         // The controlling Region
  if( r->is_Loop() ) return false; // Phi not at loop head
  Compile *C = Compile::current();
  if( (r->outcnt()<<1)+C->unique() >= MaxNodeLimit ) return false; // Too much inflation

  // Find the most popular Opcode into this Phi
  int maxcnt = -1;
Node*x=NULL;
  int op = -1;
Node*match_ctrl=NULL;
PhiNode*load_phi_alias=NULL;
for(uint i=1;i<phi->req();i++){
    if( (int)(phi->req()-i) <= maxcnt ) break; // If no chance of a higher maxop, break
Node*a=phi->in(i);//Sample user to CSE
    if( a->outcnt()!=1 ) continue; // Single user is Phi
    int aop = a->Opcode();
    if( aop == Op_Phi || aop == Op_ArrayCopy || aop == Op_MergeMem || aop == Op_Conv2B || 
        a->is_Proj() || a->is_CMove() )
      continue;                 // Sample user not a candidate for CSE
    AddPNode *adr = NULL;       // Address math is also shared
    PhiNode *load_phi_alias_a = NULL;;
if(a->is_Mem()){
      adr = a->in(MemNode::Address)->is_AddP() ? a->in(MemNode::Address)->as_AddP() : NULL;
      if( a->is_Load() ) { // Loads need to check for anti-deps before sinking
        LoadNode *la = a->as_Load();
uint load_alias_idx=C->get_alias_index(la->adr_type());

        // First find an aliasing memory Phi.  There may be none, in which
        // case there's no store in the same basic block as the sinking load
        // which can interfere.  There may be a fat Phi, or a skinny Phi or
        // both - if both then the skinny Phi supercedes.
        for( DUIterator_Fast jmax, j = r->fast_outs(jmax); j < jmax; j++) {
          Node *u = r->fast_out(j);
          if( u->is_Phi() && C->can_alias(u->adr_type(), load_alias_idx) ) {
            load_phi_alias_a = u->as_Phi(); // Aliasing Phi - capture it
            if( u->as_Phi()->adr_type() == la->adr_type() ) 
              break;            // It's also skinny - so stop searching
          }
        }
        // If we found an aliasing Phi, see if the load comes from the Phi
        // inputs.  If so, then there's no chance of an anti-dep and it is OK
        // to sink the load.
        if( load_phi_alias_a && 
            la->in(MemNode::Memory) != load_phi_alias_a->in(i) ) 
          continue;             // Do not consider this load - possible anti-dep
      }
    }
    Node *match_ctrl_a = (a->in(0) == r->in(i)) ? NULL : a->in(0);
    int cntop = 0;              // Count instances of "aop"
for(uint j=1;j<phi->req();j++)
      if( match_tail_cse( phi->in(j), aop, a, adr, 
                          load_phi_alias_a ? load_phi_alias_a->in(j) : NULL, 
                          // Control is either matching each Region input OR all same (OR null)
                          match_ctrl_a     ? match_ctrl_a            : r->in(j) ) )
        cntop++;                // Count instances of "aop"
    if( cntop > maxcnt ) {      // Larger count for this op?
      maxcnt = cntop;           // New max op
x=a;
      op = aop;
      match_ctrl = match_ctrl_a;
      load_phi_alias = load_phi_alias_a;
    }
  }

  if( maxcnt < 2 ) return false; // No good: popular Opcode isn't popular enough

  // Need to split out a private merge point?  That means the replicated op
  // "a" doesn't apply on all paths, just most of them.  Make a private merge
  // point where "a" happens on all paths and tail-merge it there.
  if( maxcnt != (int)(phi->req()-1) ) {
    // Private merge Region
RegionNode*rx=new(C,1)RegionNode(1);
    r->ins_req(1,rx);           // Attach the new private merge to the old one
    // Clone each original PhiNode, put 'em up front where I can find 'em
for(DUIterator_Fast jmax,j=r->fast_outs(jmax);j<jmax;j++){
Node*u=r->fast_out(j);
if(u->is_Phi()){
        u->ins_req(1,new (C, 1) PhiNode(rx,u->as_Phi()->type(),u->as_Phi()->adr_type()));
nstack.push(u,1);
      }
    }
    // Now visit all inputs to "phi" and decide if they go to the private
    // split or not.
    Node *adr = x->is_Mem() ? (x->in(MemNode::Address)->is_AddP()?x->in(MemNode::Address):NULL) : NULL; // Address math is also shared
for(uint k=2;k<r->req();k++){
      assert0( r->in(1) == rx );
      if( match_tail_cse( phi->in(k), op, x, adr, 
                          load_phi_alias ? load_phi_alias->in(k) : NULL,
                          // Control is either matching each Region input OR all same (OR null)
                          match_ctrl     ? match_ctrl            : r->in(k) ) ) {
        // Move this path to the private merge
rx->add_req(r->in(k));
        r->del_req(k);
for(DUIterator_Fast jmax,j=r->fast_outs(jmax);j<jmax;j++){
Node*u=r->fast_out(j);
if(u->is_Phi()){
            PhiNode *px = u->in(1)->as_Phi();
            assert0( px->in(0) == rx );
            px->add_req(u->in(k));
            u->del_req(k);
          }
        }
        k--;                    // Rerun same iteration
      }
    }
    assert0( r ->req() >= 3 ); // Expect at least the new split and one other
    assert0( rx->req() >= 2 ); // Expect at least 2 merging inputs
    // Check for making useless Phi's after the split and remove 'em.
    uint i;
    for (DUIterator_Fast jmax, j = r->fast_outs(jmax); j < jmax; j++) {
      Node *u = r->fast_out(j);
      if( u->is_Phi() && fold_phi(u->as_Phi()) ) {  // Attempt to fold useless phi
        --j; --jmax;            // After removing phi, adjust DUiterator
      }
    }
    // Check for making useless Phi's after the split and remove 'em.
for(DUIterator_Fast jmax,j=rx->fast_outs(jmax);j<jmax;j++){
Node*u=rx->fast_out(j);
      if( u->is_Phi() && fold_phi(u->as_Phi()) ) {  // Attempt to fold useless phi
        --j; --jmax;            // After removing phi, adjust DUiterator
      }
    }
    return false;
  }
  
  return true;
}

static void clone_call_arguments( Node *n, Final_Reshape_Counts &fpu, Node_Stack &nstack ) {
  assert( n->is_Call(), "" );
  CallNode *call = n->as_Call();
  // Count call sites where the FP mode bit would have to be flipped.
  // Do not count uncommon runtime calls:
  // uncommon_trap, _complete_monitor_locking, _complete_monitor_unlocking, 
  // _new_Java, _new_typeArray, _new_objArray, _rethrow_Java, ...
  if( !call->is_CallStaticJava() || !call->as_CallStaticJava()->_name ) {
    fpu.inc_call_count();   // Count the call site
  } else {                  // See if uncommon argument is shared
for(uint i=TypeFunc::Parms;i<call->req();i++){
Node*m=call->in(i);
      int mop = m->Opcode();
      // Clone shared simple arguments to uncommon calls, item (1).
if(m->outcnt()>1&&
!m->is_Proj()&&
mop!=Op_CheckCastPP&&
          !m->is_Mem() &&
          (!m->in(0) || m->is_Con()) ) {
        call->set_req( i, m->clone() );
nstack.push(call,i);
      }
    }
  }
}

static void push_new( Node *n, Node_Stack &nstack, Final_Reshape_Counts &fpu ) {
nstack.push(n,0);
  fpu._visited.set(n->_idx);
}


//------------------------------final_graph_reshaping_impl----------------------
// Implement items 1-5 from final_graph_reshaping below.
static void final_graph_reshaping_impl(Compile*C,Node_Stack&nstack,Node*n,Final_Reshape_Counts&fpu){
  if( n->outcnt() == 0 ) return; // dead anyways

  uint nop = n->Opcode();

  // Check for 2-input instruction with "last use" on right input.
  // Swap to left input.  Implements item (2).
  if( n->req() == 3 &&          // two-input instruction
      n->in(1)->outcnt() > 1 && // left use is NOT a last use
      n->in(2)->outcnt() == 1 &&// right use IS a last use
      !n->in(2)->is_Con() ) {   // right use is not a constant
    // Check for commutative opcode
    switch( nop ) {
    case Op_AddI:  case Op_AddF:  case Op_AddD:  case Op_AddL:
    case Op_MulI:  case Op_MulF:  case Op_MulD:  case Op_MulL:
    case Op_AndL:  case Op_XorL:  case Op_OrL: 
    case Op_AndI:  case Op_XorI:  case Op_OrI: {
      // Move "last use" input to left by swapping inputs
      n->swap_edges(1, 2);
      break;
    }
    default:
      break;
    }
  }

  // Count FPU ops and common calls, implements item (3)
  switch( nop ) {
  // Count all float operations that may use FPU
  case Op_AddF:
  case Op_SubF:
  case Op_MulF:
  case Op_DivF:
  case Op_NegF:
  case Op_ModF:
  case Op_ConvI2F:
  case Op_ConF:
  case Op_CmpF:
  case Op_CmpF3:
  // case Op_ConvL2F: // longs are split into 32-bit halves
    fpu.inc_float_count();
    break;

  case Op_ConvF2D:
  case Op_ConvD2F:
    fpu.inc_float_count();
    fpu.inc_double_count();
    break;

  // Count all double operations that may use FPU
  case Op_AddD:
  case Op_SubD:
  case Op_MulD:
  case Op_DivD:
  case Op_NegD:
  case Op_ModD:
  case Op_ConvI2D:
  case Op_ConvD2I:
  // case Op_ConvL2D: // handled by leaf call
  // case Op_ConvD2L: // handled by leaf call
  case Op_ConD:
  case Op_CmpD:
  case Op_CmpD3:
    fpu.inc_double_count();
    break;
  case Op_Opaque1:              // Remove Opaque Nodes before matching
  case Op_Opaque2:              // Remove Opaque Nodes before matching
    n->replace_by(n->in(1));
    break;
  case Op_Phi: {              // Clone initializing constants to loops
PhiNode*nphi=n->as_Phi();
    Node* x = nphi->is_copy();  // Remove self-copies
    if (x == nphi)  x = Compile::current()->top();  // degenerate self-loop
if(x!=NULL){
n->replace_by(x);
      break;
    }
Node*n0=n->in(0);//The controlling Region
Node*n1=n->in(1);
    if( n0->is_Loop() &&   // These will split & rematerialize anyways
        n1->is_Con() &&    // so we save the allocator some work here.
        n1->outcnt() > 1 ) {
      n->set_req( 1, (n1=n1->clone()) );
nstack.push(n,1);
    }
#ifdef ASSERT
for(uint i=1;i<n->req();i++)//no dead phi inputs now
      assert0( !n->in(i)->is_top() );
#endif //!ASSERT
    // Check for tail-merge opportunities
    if( check_tail_cse(nphi,nstack) ) {
      n = do_tail_cse(nphi);
      push_new(n, nstack, fpu);
    }
    break;
  }

  case Op_CallStaticJava:
  case Op_CallDynamicJava:
    fpu.inc_java_call_count(); // Count java call site;
  case Op_Allocate:
  case Op_AllocateArray:
  case Op_CallLeaf:
case Op_CallLeafNoFP:
  case Op_CallRuntime:
  case Op_Lock:
  case Op_Unlock:
    clone_call_arguments(n,fpu,nstack);
    break;

  case Op_StoreD:
  case Op_LoadD:
    fpu.inc_double_count();
    goto handle_mem;
  case Op_StoreF:
  case Op_LoadF:
    fpu.inc_float_count();
    goto handle_mem;

  case Op_StoreB:
  case Op_StoreC:
  case Op_StoreI:
  case Op_StoreL:
  case Op_CompareAndSwapI:
  case Op_CompareAndSwapL:
  case Op_CompareAndSwapP:
  case Op_StoreP:
  case Op_LoadB:
  case Op_LoadC:
  case Op_LoadI:
  case Op_LoadL:
  case Op_LoadRange:
  case Op_LoadS: {
  handle_mem:
#ifdef ASSERT
    if( VerifyOptoOopOffsets ) {
      assert( n->is_Mem(), "" );
      MemNode *mem  = (MemNode*)n;
      // Check to see if address types have grounded out somehow.
      const TypeInstPtr *tp = mem->in(MemNode::Address)->bottom_type()->isa_instptr();
      assert( !tp || oop_offset_is_sane(tp), "" );
    }
#endif
    break;
  }
  case Op_ConP: {
    // Compute the index off the KlassTable
    const TypeOopPtr *to = n->bottom_type()->isa_oopptr();
    if( !to ) break;
    if( to->above_centerline() ) { // The occasional weird dead path
      // Can happen if the type system spots an impossible situation but the
      // sub-type check code fails to prove it can't happen, e.g., casting an
      // exact object into an incompatible interface.
      ((ConPNode*)n)->set_type(TypePtr::NULL_PTR);
      break;
    }
ciObject*cio=to->const_oop();
    int idx = cio->is_klass() ? ((ciKlass*)cio)->klassId() : ciEnv::get_OopTable_index(cio->encoding());
    C->_masm.record_constant_oop(idx);
    break;
  }
  case Op_CmpP: 
  case Op_LoadP:
  case Op_LoadPLocked:
  case Op_LoadKlass: 
    break;

  case Op_If: {
    fpu._tests.push(n);         // Collect CFG split points
    break;
  }

  case Op_CountedLoopEnd: {
    fpu._tests.push(n);		// Collect CFG split points
    break;
  }
  case Op_AddP: {               // Assert sane base pointers
    // Assert is too strong.  Code in the loop opts purposely clones base pointers
    // to allow code to sink out of loops.
    //const Node *addp = n->in(AddPNode::Address);
    //assert( !addp->is_AddP() || 
    //        addp->in(AddPNode::Base)->is_top() || // Top OK for allocation
    //        addp->in(AddPNode::Base) == n->in(AddPNode::Base), 
    //        "Base pointers must match" );
    break;
  }

  case Op_ModI:
    if (UseDivMod) {
      // Check if a%b and a/b both exist
      Node* d = n->find_similar(Op_DivI);
      if (d) {
        // Replace them with a fused divmod if supported
        Compile* C = Compile::current();
        if (Matcher::has_match_rule(Op_DivModI)) {
          DivModINode* divmod = DivModINode::make(C, n);
          d->replace_by(divmod->div_proj());
          n->replace_by(divmod->mod_proj());
          push_new(divmod->div_proj(),nstack,fpu);
          push_new(divmod->mod_proj(),nstack,fpu);
        } else {
          // replace a%b with a-((a/b)*b)
          Node* mult = new (C, 3) MulINode(d, d->in(2));
          Node* sub  = new (C, 3) SubINode(d->in(1), mult);
          n->replace_by( sub );
          push_new(mult,nstack,fpu);
          push_new(sub ,nstack,fpu);
        }
      }
    }
    break;

  case Op_ModL:
    if (UseDivMod) {
      // Check if a%b and a/b both exist
      Node* d = n->find_similar(Op_DivL);
      if (d) {
        // Replace them with a fused divmod if supported
        Compile* C = Compile::current();
        if (Matcher::has_match_rule(Op_DivModL)) {
          DivModLNode* divmod = DivModLNode::make(C, n);
          d->replace_by(divmod->div_proj());
          n->replace_by(divmod->mod_proj());
          push_new(divmod->div_proj(),nstack,fpu);
          push_new(divmod->mod_proj(),nstack,fpu);
        } else {
          // replace a%b with a-((a/b)*b)
          Node* mult = new (C, 3) MulLNode(d, d->in(2));
          Node* sub  = new (C, 3) SubLNode(d->in(1), mult);
          push_new(mult,nstack,fpu);
          push_new(sub ,nstack,fpu);
        }
      }
    }
    break;

  case Op_Load16B:
  case Op_Load8B:
  case Op_Load4B:
  case Op_Load8S:
  case Op_Load4S:
  case Op_Load2S:
  case Op_Load8C:
  case Op_Load4C:
  case Op_Load2C:
  case Op_Load4I:
  case Op_Load2I:
  case Op_Load2L:
  case Op_Load4F:
  case Op_Load2F:
  case Op_Load2D:
  case Op_Store16B:
  case Op_Store8B:
  case Op_Store4B:
  case Op_Store8C:
  case Op_Store4C:
  case Op_Store2C:
  case Op_Store4I:
  case Op_Store2I:
  case Op_Store2L:
  case Op_Store4F:
  case Op_Store2F:
  case Op_Store2D:
    break;

  case Op_PackB:
  case Op_PackS:
  case Op_PackC:
  case Op_PackI:
  case Op_PackF:
  case Op_PackL:
  case Op_PackD:
    if (n->req()-1 > 2) {
      // Replace many operand PackNodes with a binary tree for matching
      PackNode* p = (PackNode*) n;
      Node* btp = p->binaryTreePack(Compile::current(), 1, n->req());
      n->replace_by(btp);
    }
    break;

  case Op_AndI: {
    // Masking sign bits off of a Byte?  Let the matcher use an unsigned load.
    // Matcher won't match ops with different control inputs, so encourage the
    // match by giving the mask op the same control as the load.
Node*ld=n->in(1);
    jint mask;
    if( ld->Opcode() == Op_LoadB &&
        !n->in(0) && ld->in(0) &&
n->in(2)->is_Con()&&
        (mask = n->in(2)->get_int()) &&
        mask == 0x000000FF ) 
n->set_req(0,ld->in(0));
    break;
  }

  default: 
    assert( !n->is_Call(), "" );
    assert( !n->is_Mem(), "" );
    if( n->is_If() || n->is_PCTable() ) 
      fpu._tests.push(n);       // Collect CFG split points
    break;
  }
}

//------------------------------final_graph_reshaping_walk---------------------
// Replacing Opaque nodes with their input in final_graph_reshaping_impl(),
// requires that the walk visits a node's inputs before visiting the node.
static void final_graph_reshaping_walk(Compile*C,Node_Stack&nstack,Node*root,Final_Reshape_Counts&fpu){
  fpu._visited.set(root->_idx); // first, mark node as visited
  uint cnt = root->req();
  Node *n = root;
  uint  i = 0;
  while (true) {
    if (i < cnt) {
      // Place all non-visited non-null inputs onto stack
      Node* m = n->in(i);
      ++i;
      if (m != NULL && !fpu._visited.test_set(m->_idx)) {
        cnt = m->req();
        nstack.push(n, i); // put on stack parent and next input's index 
        n = m;
        i = 0;
      }
    } else {
      // Now do post-visit work
fpu._post_visited.set(n->_idx);//first, mark node as post-visited
      final_graph_reshaping_impl( C, nstack, n, fpu );
      if (nstack.is_empty())
        break;             // finished
      n = nstack.node();   // Get node from stack 
      cnt = n->req();
      i = (nstack.nreq() == cnt) ? nstack.index() : 0; // reset if node changed size mid-walk
      nstack.pop();        // Shift to the next node on stack
    }
  }
}

//------------------------------final_graph_reshaping--------------------------
// Final Graph Reshaping.  
//
// (1) Clone simple inputs to uncommon calls, so they can be scheduled late
//     and not commoned up and forced early.  Must come after regular 
//     optimizations to avoid GVN undoing the cloning.  Clone constant
//     inputs to Loop Phis; these will be split by the allocator anyways.
//     Remove Opaque nodes.
// (2) Move last-uses by commutative operations to the left input to encourage
//     Intel update-in-place two-address operations and better register usage
//     on RISCs.  Must come after regular optimizations to avoid GVN Ideal
//     calls canonicalizing them back.
// (3) Count the number of double-precision FP ops, single-precision FP ops
//     and call sites.  On Intel, we can get correct rounding either by
//     forcing singles to memory (requires extra stores and loads after each
//     FP bytecode) or we can set a rounding mode bit (requires setting and
//     clearing the mode bit around call sites).  The mode bit is only used
//     if the relative frequency of single FP ops to calls is low enough.
//     This is a key transform for SPEC mpeg_audio.
// (4) Detect infinite loops; blobs of code reachable from above but not 
//     below.  Several of the Code_Gen algorithms fail on such code shapes,
//     so we simply bail out.  Happens a lot in ZKM.jar, but also happens
//     from time to time in other codes (such as -Xcomp finalizer loops, etc).
//     Detection is by looking for IfNodes where only 1 projection is
//     reachable from below or CatchNodes missing some targets.
// (5) Assert for insane oop offsets in debug mode.
// (6) Look for simple tail-equal sequences to reduce code size

bool Compile::final_graph_reshaping() {
  // an infinite loop may have been eliminated by the optimizer,
  // in which case the graph will be empty.
  if (root()->req() == 1) {
    record_method_not_compilable("trivial infinite loop");
    return true;
  }

  Final_Reshape_Counts fpu;

  // Visit everybody reachable!
  // Allocate stack of size C->unique()/2 to avoid frequent realloc
  Node_Stack nstack(unique() >> 1);
final_graph_reshaping_walk(this,nstack,root(),fpu);

#ifdef ASSERT
  // Verify all got visited
Node_Stack nstack_verify(unique()>>1);
VectorSet vverify(Thread::current()->resource_area());
Node*n=root();
vverify.set(n->_idx);
uint cnt=n->req();
  uint  i = 0;
  while (true) {
    if (i < cnt) {
Node*m=n->in(i++);
      if( m && !vverify.test_set(m->_idx)) {
        cnt = m->req();
        nstack.push(n, i); // put on stack parent and next input's index 
        n = m;
        i = 0;
      }
    } else {
      assert0( fpu._visited.test(n->_idx) && fpu._post_visited.test(n->_idx) );
      if (nstack.is_empty())  break;
      n = nstack.node();   // Get node from stack 
      i = nstack.index();
      nstack.pop();        // Shift to the next node on stack
      cnt = n->req();
    }
  }
#endif


  // Check for unreachable (from below) code (i.e., infinite loops).
  for( uint i = 0; i < fpu._tests.size(); i++ ) {
    Node *n = fpu._tests[i];
assert(n->is_PCTable()||n->is_If()||n->is_RangeCheck(),"either PCTables or IfNodes");
    // Get number of CFG targets; 2 for IfNodes or _size for PCTables.
    // Note that PCTables include exception targets after calls.
uint expected_kids=n->is_PCTable()?n->as_PCTable()->_size:(n->is_If()?2:n->outcnt());
    if (n->outcnt() != expected_kids) {
      // Check for a few special cases.  Rethrow Nodes never take the
      // 'fall-thru' path, so expected kids is 1 less.
      if (n->is_PCTable() && n->in(0) && n->in(0)->in(0)) {
        if (n->in(0)->in(0)->is_Call()) {
          CallNode *call = n->in(0)->in(0)->as_Call();
if(call->entry_point()==StubRoutines::forward_exception_entry2()||
              // Also check for a null-receiver-must-throw scenario.
              (call->is_CallDynamicJava() && call->in(TypeFunc::Parms+0)->bottom_type() == TypePtr::NULL_PTR))
            expected_kids--;    // Rethrow always has 1 less kid
        }
      }
      // Recheck with a better notion of 'expected_kids'
      if (n->outcnt() != expected_kids) {
        record_method_not_compilable("malformed control flow");
        return true;            // Not all targets reachable!
      }
    }
    // Check that I actually visited all kids.  Unreached kids
    // must be infinite loops.
for(DUIterator_Fast jmax,j=n->fast_outs(jmax);j<jmax;j++){
Node*k=n->fast_out(j);
if(!fpu._visited.test(k->_idx)&&k->is_CFG()){
        record_method_not_compilable("infinite loop");
        return true;            // Found unvisited kid; must be unreach
      }
    }
  }

  // If original bytecodes contained a mixture of floats and doubles
  // check if the optimizer has made it homogenous, item (3).
  if( Use24BitFPMode && Use24BitFP && 
      fpu.get_float_count() > 32 && 
      fpu.get_double_count() == 0 &&
      (10 * fpu.get_call_count() < fpu.get_float_count()) ) {
    set_24_bit_selection_and_mode( false,  true );
  }

  set_has_java_calls(fpu.get_java_call_count() > 0);

  // No infinite loops, no reason to bail out.
  return false;
}

#ifndef PRODUCT
//------------------------------verify_graph_edges---------------------------
// Walk the Graph and verify that there is a one-to-one correspondence
// between Use-Def edges and Def-Use edges in the graph.
void Compile::verify_graph_edges(bool no_dead_code) {
  if (VerifyGraphEdges) {
    ResourceArea *area = Thread::current()->resource_area();
    Unique_Node_List visited(area);
    // Call recursive graph walk to check edges
    _root->verify_edges(visited);
    if (no_dead_code) {
      // Now make sure that no visited node is used by an unvisited node.
      bool dead_nodes = 0;
      Unique_Node_List checked(area);
      while (visited.size() > 0) {
        Node* n = visited.pop();
        checked.push(n);
        for (uint i = 0; i < n->outcnt(); i++) {
          Node* use = n->raw_out(i);
          if (checked.member(use))  continue;  // already checked
          if (visited.member(use))  continue;  // already in the graph
          if (use->is_Con())        continue;  // a dead ConNode is OK
          // At this point, we have found a dead node which is DU-reachable.
          if (dead_nodes++ == 0)
C2OUT->print_cr("*** Dead nodes reachable via DU edges:");
          use->dump(2);
C2OUT->print_cr("---");
          checked.push(use);  // No repeats; pretend it is now checked.
        }
      }
      assert(dead_nodes == 0, "using nodes must be reachable from root");
    }
  }
}
#endif

// The Compile object keeps track of failure reasons separately from the ciEnv.
// This is required because there is not quite a 1-1 relation between the
// ciEnv and its compilation task and the Compile object.  Note that one
// ciEnv might use two Compile objects, if C2Compiler::compile_method decides
// to backtrack and retry without subsuming loads.  Other than this backtracking
// behavior, the Compile's failure reason is quietly copied up to the ciEnv
// by the logic in C2Compiler.
void Compile::record_failure(const char*reason,bool retry_compile){
  if (_failure_reason == NULL) {
    // Record the first failure reason.
    _failure_reason = reason;
    _failure_retry_compile = retry_compile;    
  }
  _root = NULL;  // flush the graph, too
}

Compile::TracePhase::TracePhase(const char* name, elapsedTimer* accumulator, bool dolog)
:TraceTime(NULL,accumulator,false NOT_PRODUCT(||TimeCompiler),false),_dolog(dolog)
{
  if( _dolog && LogCompilerOutput ) {
    C2OUT->print("< <phase name='%s' nodes='%d'", name, Compile::current()->unique());
C2OUT->stamp();
C2OUT->print(">");
  }
}

Compile::TracePhase::~TracePhase() {
  if( _dolog && LogCompilerOutput ) {
    C2OUT->print_cr("</phase nodes='%d'> >", Compile::current()->unique());
  }
}

void Compile::record_cloned_cp(CodeProfile *cp) {
  if (_max_cloned_cp == _num_cloned_cp) {
     const int grow_by = 4;
    _cloned_cp =  REALLOC_ARENA_ARRAY(comp_arena(), CodeProfile*, _cloned_cp, _max_cloned_cp, _max_cloned_cp+grow_by);
    Copy::zero_to_bytes(_cloned_cp+_max_cloned_cp, 
			sizeof(CodeProfile*)*grow_by);
    _max_cloned_cp += grow_by;
  }
  NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_c2_cloned, 1);)
  _cloned_cp[_num_cloned_cp++] = cp;
}
