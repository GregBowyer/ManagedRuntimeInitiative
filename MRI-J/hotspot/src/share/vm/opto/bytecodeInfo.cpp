/*
 * Copyright 1998-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "callGenerator.hpp"
#include "parse.hpp"

// These variables are declared in parse1.cpp
extern int  explicit_null_checks_inserted;
extern int  explicit_null_checks_elided;
extern int  explicit_null_checks_inserted_old;
extern int  explicit_null_checks_elided_old;
extern int  nodes_created_old;
extern int  nodes_created;
extern int  methods_parsed_old;
extern int  methods_parsed;
extern int  methods_seen;
extern int  methods_seen_old;


//=============================================================================
//------------------------------InlineTree-------------------------------------
InlineTree::InlineTree( Compile* c, const InlineTree *caller_tree, ciMethod* callee, JVMState* caller_jvms, int caller_bci, float site_invoke_ratio )
: C(c), _caller_jvms(caller_jvms),
  _caller_tree((InlineTree*)caller_tree),
  _method(callee), _site_invoke_ratio(site_invoke_ratio), 
  _count_inline_bcs(method()->code_size()) {
  NOT_PRODUCT(_count_inlines = 0;)
  if (_caller_jvms != NULL) {
    // Keep a private copy of the caller_jvms:
    _caller_jvms = new (C) JVMState(caller_jvms->method(), caller_tree->caller_jvms());
    _caller_jvms->set_bci(caller_jvms->bci());
  }
  assert(_caller_jvms->same_calls_as(caller_jvms), "consistent JVMS");
  assert((caller_tree == NULL ? 0 : caller_tree->inline_depth() + 1) == inline_depth(), "correct (redundant) depth parameter");
  assert(caller_bci == this->caller_bci(), "correct (redundant) bci parameter");
  if (UseOldInlining) {
    // Update hierarchical counts, count_inline_bcs() and count_inlines()
    InlineTree *caller = (InlineTree *)caller_tree;
    for( ; caller != NULL; caller = ((InlineTree *)(caller->caller_tree())) ) {
      caller->_count_inline_bcs += count_inline_bcs();
      NOT_PRODUCT(caller->_count_inlines++;)
    }
  }
}

InlineTree::InlineTree(Compile* c, ciMethod* callee_method, JVMState* caller_jvms, float site_invoke_ratio)
: C(c), _caller_jvms(caller_jvms), _caller_tree(NULL),
  _method(callee_method), _site_invoke_ratio(site_invoke_ratio),
  _count_inline_bcs(method()->code_size()) {
  NOT_PRODUCT(_count_inlines = 0;)
  assert(!UseOldInlining, "do not use for old stuff");
}



static void print_indent(int depth) {
  outputStream* out = Compile::current()->out();
out->print("      ");
for(int i=depth;i!=0;--i)out->print("  ");
}

// positive filter: should send be inlined?  returns NULL, if yes, or rejection msg 
InliningFailureID InlineTree::shouldInline(ciMethod*callee_method,int caller_bci,int call_site_count,WarmCallInfo*wci_result)const{
  // positive filter: should send be inlined?  returns NULL (--> yes)
  // or rejection msg
  outputStream* out = Compile::current()->out();

  // Allows targeted inlining
  if(callee_method->should_inline()) {
    *wci_result = *(WarmCallInfo::always_hot());
    if ( PrintC2Inlining || PrintInlining) {
      print_indent(inline_depth());
out->print_cr("Compiler oracle requested to inline: ");
    }
    return IF_COMPILERORACLEREQUEST;
  }

  int size = callee_method->code_size();

  // Check for too many throws (and not too huge)
  int throwout_count = callee_method->get_codeprofile_count(CodeProfile::_throwout);
  if( throwout_count > InlineThrowCount && size < InlineThrowMaxSize ) {
    wci_result->set_profit(wci_result->profit() * 100);
    if ( PrintC2Inlining || PrintInlining ) {
      print_indent(inline_depth());
out->print_cr("Inlined method with many throws (throws=%d):",throwout_count);
    }
    return IF_NOFAILURE;
  }

  if (!UseOldInlining) {
return IF_NOFAILURE;//size and frequency are represented in a new way
  }

int max_size=C2MaxInlineSize;
int invoke_count=method()->invocation_count();
  if( UseC1 ) invoke_count += method()->get_codeprofile_count(CodeProfile::_invoke);
  if( invoke_count == 0 ) invoke_count = 1; // happens in -Xcomp sometimes
  int freq = call_site_count/invoke_count;

  // Increase the max size if the call is frequent
  if( freq >= InlineFrequencyRatio ) {
    // Very high caller_invoke to call_site ratio implies the caller is
    // spinning a hot loop around the callee.  Allow inlining of very large
    // methods in this case.
    max_size = C2FreqInlineSize>>1; // div-by-2 because loop below always trips at least once
    int r = InlineFrequencyRatio;
    while( freq >= r ) {
      r *= InlineFrequencyRatio;
      max_size <<= 1;
      if( max_size >= 65536 ) 
        return IF_NOFAILURE;    // sometimes 'freq' is insanely large; could be MAX_INT
    }
  } else if( call_site_count >= (invoke_count*((double)InlineFrequencyCount/C2CompileThreshold)) ) {
max_size=C2FreqInlineSize;
  } else {
//Not hot.  Check for medium-sized pre-existing methodCodeOop at cold sites.
if(callee_method->has_c2_code()&&callee_method->instructions_size()>InlineSmallCode/4){
      return IF_ALREADYCOMPILEDMEDIUMMETHOD;
    }
  }

  if( size > max_size ) {
if(max_size>C2MaxInlineSize){
      return IF_HOTMETHODTOOBIG;
    } else {
      return IF_TOOBIG;
    }
  }

  return IF_NOFAILURE;                  // Ok to inline
}


// negative filter: should send NOT be inlined?  returns NULL, ok to inline, or rejection msg 
InliningFailureID InlineTree::shouldNotInline(ciMethod*callee_method,int call_site_count,WarmCallInfo*wci_result)const{
  // negative filter: should send NOT be inlined?  returns NULL (--> inline) or rejection msg 
  if (!UseOldInlining) {
    InliningFailureID fail = IF_NOFAILURE;
    if (callee_method->is_abstract())               fail = IF_ABSTRACTMETHOD;
    // note: we allow ik->is_abstract()
if(!callee_method->holder()->is_initialized())fail=IF_METHODHOLDERNOTINIT;
if(callee_method->is_native())fail=IF_NATIVEMETHOD;

    if (fail) {
      *wci_result = *(WarmCallInfo::always_cold());
      return fail;
    }

    if (callee_method->has_unloaded_classes_in_signature()) {
      wci_result->set_profit(wci_result->profit() * 0.1);
    }

    // don't inline exception code unless the top method belongs to an
    // exception class
    if (callee_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
      ciMethod* top_method = caller_jvms() ? caller_jvms()->of_depth(1)->method() : method();
      if (!top_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
        wci_result->set_profit(wci_result->profit() * 0.1);
      }
    }

if(callee_method->has_c2_code()&&callee_method->instructions_size()>InlineSmallCode){
      wci_result->set_profit(wci_result->profit() * 0.1);
      // %%% adjust wci_result->size()? 
    }

    return IF_NOFAILURE;
  }

  if (callee_method->is_abstract())               return IF_ABSTRACTMETHOD;
  // note: we allow ik->is_abstract()
if(!callee_method->holder()->is_initialized())return IF_METHODHOLDERNOTINIT;
if(callee_method->is_native())return IF_NATIVEMETHOD;

if(callee_method->has_c2_code()&&callee_method->instructions_size()>InlineSmallCode)
return IF_ALREADYCOMPILEDBIGMETHOD;

  // don't inline exception code unless the top method belongs to an
  // exception class
  if (caller_tree() != NULL &&
      callee_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
    const InlineTree *top = this;
    while (top->caller_tree() != NULL) top = top->caller_tree();
    ciInstanceKlass* k = top->method()->holder();
    if (!k->is_subclass_of(C->env()->Throwable_klass()))
      return IF_EXCEPTIONMETHOD;
  }

  if (callee_method->has_unloaded_classes_in_signature()) return IF_UNLOADEDSIGNATURECLASS;

  // use frequency-based objections only for non-trivial methods
if(callee_method->code_size()<=MaxTrivialSize)return IF_NOFAILURE;
  if (UseInterpreter && !CompileTheWorld) { // don't use counts with -Xcomp or CTW
    if (!callee_method->has_c2_code() && call_site_count <= 0) return IF_NEVEREXECUTED;
    int call_site_est = UseC1   // Have profiling info?
      ? call_site_count         // Use profiling
      :  (CompileTheWorld ? C2CompileThreshold : callee_method->invocation_count());
    if( call_site_est <= MIN2(MinInliningThreshold, C2CompileThreshold >> 1)) 
      return IF_EXECUTEDLTMININLININGTHRESHOLD;
  }

  return IF_NOFAILURE;
}

//-----------------------------try_to_inline-----------------------------------
// return NULL if ok, reason for not inlining otherwise
// Relocated from "InliningClosure::try_to_inline"
InliningFailureID InlineTree::try_to_inline(ciMethod*callee_method,int caller_bci,int call_site_count,WarmCallInfo*wci_result){
  ciMethod* caller_method = method();

  if (caller_method->should_disable_inlining()) return IF_COMPILERORACLEREQUEST;

  // Old algorithm had funny accumulating BC-size counters
  if (UseOldInlining && ClipInlining
      && (int)count_inline_bcs() >= DesiredMethodLimit) {
    return IF_SIZEGTDESIREDMETHODLIMIT;
  }

  InliningFailureID msg = IF_NOFAILURE;
if((msg=shouldInline(callee_method,caller_bci,call_site_count,wci_result))!=IF_NOFAILURE)return msg;
  if ((msg = shouldNotInline(callee_method,          call_site_count, wci_result)) != IF_NOFAILURE) return msg;

  bool is_accessor = InlineAccessors && callee_method->is_accessor();

  // suppress a few checks for accessors and trivial methods
  if (!is_accessor && callee_method->code_size() > MaxTrivialSize) {
    // don't inline into giant methods
if(C->unique()>(uint)NodeCountInliningCutoff)return IF_NODECOUNTINLININGCUTOFF;
  }

  if (!C->do_inlining() && InlineAccessors && !is_accessor) return IF_NOTANACCESSOR;

  if( inline_depth() > MaxInlineLevel )           return IF_INLININGTOODEEP;
  if( method() == callee_method &&
inline_depth()>MaxRecursiveInlineLevel)return IF_RECURSIVELYINLININGTOODEEP;

  int size = callee_method->code_size();

  if (UseOldInlining && ClipInlining
      && (int)count_inline_bcs() + size >= DesiredMethodLimit) {
    return IF_SIZEGTDESIREDMETHODLIMIT;
  } 
  
  // ok, inline this method
  return IF_NOFAILURE;
}

//------------------------------pass_initial_checks----------------------------
bool pass_initial_checks(ciMethod* caller_method, int caller_bci, ciMethod* callee_method) {
  ciInstanceKlass *callee_holder = callee_method ? callee_method->holder() : NULL;
  // Check if a callee_method was suggested
  if( callee_method == NULL )            return false;
  // Check if klass of callee_method is loaded
  if( !callee_holder->is_loaded() )      return false;
  if( !callee_holder->is_initialized() ) return false;
  if( !UseInterpreter || CompileTheWorld /* running Xcomp or CTW */ ) {
    // Checks that constant pool's call site has been visited
    // stricter than callee_holder->is_initialized()
    ciBytecodeStream iter(caller_method);
    iter.force_bci(caller_bci);
    int index = iter.get_index_big();
if(!caller_method->is_klass_loaded(index)){
      return false;
    }
    // Try to do constant pool resolution if running Xcomp
    Bytecodes::Code call_bc = iter.cur_bc();
    if( !caller_method->check_call(index, call_bc == Bytecodes::_invokestatic) ) {
      return false;
    }
  }
  // We will attempt to see if a class/field/etc got properly loaded.  If it
  // did not, it may attempt to throw an exception during our probing.  Catch
  // and ignore such exceptions and do not attempt to compile the method.
  if( callee_method->should_exclude() )  return false;

  return true;
}

#ifndef PRODUCT
//------------------------------print_inlining---------------------------------
// Really, the failure_msg can be a success message also.
void InlineTree::print_inlining(ciMethod*callee_method,int caller_bci,InliningFailureID failure_id)const{
  outputStream* out = Compile::current()->out();
  const char* failure_msg = InliningFailureID2Name[failure_id];
  print_indent(inline_depth());
out->print("@ %d  ",caller_bci);
  if( callee_method ) callee_method->print_short_name(out);
else out->print(" callee not monotonic or profiled");
out->print("  %s",(failure_msg?failure_msg:"inline"));
out->cr();
}
#endif

//------------------------------ok_to_inline-----------------------------------
WarmCallInfo* InlineTree::ok_to_inline( CPData_Invoke *c2_caller_cpd, CPData_Invoke *caller_cpd, ciMethod* callee_method, JVMState* jvms, int call_site_count, WarmCallInfo* initial_wci) {
  debug_only( assert(caller_cpd->is_Invoke(), "Not an invoke cpdata!") );
  debug_only( assert(!c2_caller_cpd || c2_caller_cpd->is_Invoke(), "Not an invoke cpdata!") );
  assert(callee_method != NULL, "caller checks for optimized virtual!");
#ifdef ASSERT
  // Make sure the incoming jvms has the same information content as me.
  // This means that we can eventually make this whole class AllStatic.
  if (jvms->caller() == NULL) {
    assert(_caller_jvms == NULL, "redundant instance state");
  } else {
    assert(_caller_jvms->same_calls_as(jvms->caller()), "redundant instance state");
  }
  assert(_method == jvms->method(), "redundant instance state");
#endif
  InliningFailureID failure_msg = IF_NOFAILURE;
  int         caller_bci    = jvms->bci();
  ciMethod   *caller_method = jvms->method();

  if( !pass_initial_checks(caller_method, caller_bci, callee_method)) {
    failure_msg = IF_FAILEDINITIALCHECKS;
    if( PrintC2Inlining || PrintInlining ) {
      print_inlining( callee_method, caller_bci, failure_msg);
    }
    if (c2_caller_cpd) c2_caller_cpd->_inlining_failure_id = failure_msg;
    return NULL;
  }

  // Check if inlining policy says no.
  WarmCallInfo wci = *(initial_wci);
failure_msg=try_to_inline(callee_method,caller_bci,call_site_count,&wci);

  if (UseOldInlining && InlineWarmCalls
&&(PrintOpto||PrintC2Inlining||PrintInlining)){
    bool cold = wci.is_cold();
    bool hot  = !cold && wci.is_hot();
bool old_cold=(failure_msg!=IF_NOFAILURE);
  }
  if (UseOldInlining) {
if(failure_msg==IF_NOFAILURE)
      wci = *(WarmCallInfo::always_hot());
    else
      wci = *(WarmCallInfo::always_cold());
  }
  if (!InlineWarmCalls) {
    if (!wci.is_cold() && !wci.is_hot()) {
      // Do not inline the warm calls.
      wci = *(WarmCallInfo::always_cold());
    }
  }

  if (!wci.is_cold()) {
    // In -UseOldInlining, the failure_msg may also be a success message.
    if (failure_msg == IF_NOFAILURE)  failure_msg = IF_INLINEHOT;

    if (c2_caller_cpd) c2_caller_cpd->_inlining_failure_id = failure_msg;
    // Inline!
if(PrintC2Inlining||PrintInlining)print_inlining(callee_method,caller_bci,failure_msg);
    if (UseOldInlining)
build_inline_tree_for_callee(caller_cpd,callee_method,jvms,caller_bci);
    if (InlineWarmCalls && !wci.is_hot())
      return new (C) WarmCallInfo(wci);  // copy to heap
    return WarmCallInfo::always_hot();
  }

  // Do not inline
if(failure_msg==IF_NOFAILURE)failure_msg=IF_TOOCOLDTOINLINE;
if(PrintC2Inlining||PrintInlining)print_inlining(callee_method,caller_bci,failure_msg);

  if (c2_caller_cpd) c2_caller_cpd->_inlining_failure_id = failure_msg;

  return NULL;
}

//------------------------------build_inline_tree_for_callee-------------------
InlineTree *InlineTree::build_inline_tree_for_callee( CPData_Invoke *caller_cpd, ciMethod* callee_method, JVMState* caller_jvms, int caller_bci) {
  float recur_frequency = _site_invoke_ratio;
  if( !caller_cpd->is_inlined() ) { // If profile data is inlined, then caller_counts==invoke_counts
    int invcnt = UseC1 ? method()->get_codeprofile_count(CodeProfile::_invoke) : method()->invocation_count();
    int count  = caller_cpd->site_count();
    if( invcnt != 0 ) {
      float freq = (float)count / (float)invcnt;
      // Call-site count / invocation count, scaled recursively.
      // Always between 0.0 and 1.0.  Represents the percentage of the method's
      // total execution time used at this call site.
      recur_frequency *= freq;
    } else {                    // -Xcomp or OSR?  No call-site profile data
      recur_frequency *= 0.5f;  // Assume 1/2 of all time is from this call site
    }
  }
 
  // Attempt inlining.
  InlineTree* old_ilt = callee_at(caller_bci, callee_method);
  if (old_ilt != NULL) {
    return old_ilt;
  }
  InlineTree *ilt = new InlineTree( C, this, callee_method, caller_jvms, caller_bci, recur_frequency );
  _subtrees.append( ilt );

  NOT_PRODUCT( _count_inlines += 1; )

  return ilt;
}


//---------------------------------------callee_at-----------------------------
InlineTree *InlineTree::callee_at(int bci, ciMethod* callee) const {
  for (int i = 0; i < _subtrees.length(); i++) {
    InlineTree* sub = _subtrees.at(i);
    if (sub->caller_bci() == bci && callee == sub->method()) {
      return sub;
    }
  }
  return NULL;
}


//------------------------------build_inline_tree_root-------------------------
InlineTree *InlineTree::build_inline_tree_root() {
  Compile* C = Compile::current();

  // Root of inline tree
InlineTree*ilt=new InlineTree(C,NULL,C->method(),NULL,InvocationEntryBci,1.0F);

  return ilt;
}


//-------------------------find_subtree_from_root-----------------------------
// Given a jvms, which determines a call chain from the root method,
// find the corresponding inline tree.
// Note: This method will be removed or replaced as InlineTree goes away.
InlineTree* InlineTree::find_subtree_from_root(InlineTree* root, JVMState* jvms, ciMethod* callee, bool create_if_not_found) {
  InlineTree* iltp = root;
  uint depth = jvms && jvms->has_method() ? jvms->depth() : 0;
  for (uint d = 1; d <= depth; d++) {
    JVMState* jvmsp  = jvms->of_depth(d);
    // Select the corresponding subtree for this bci.
    assert(jvmsp->method() == iltp->method(), "tree still in sync");
    ciMethod* d_callee = (d == depth) ? callee : jvms->of_depth(d+1)->method();
    InlineTree* sub = iltp->callee_at(jvmsp->bci(), d_callee);
    if (!sub) {
      if (create_if_not_found && d == depth) {
        Unimplemented();
        Untested("");
        CPData_Invoke caller_cpd;
        return iltp->build_inline_tree_for_callee(&caller_cpd, d_callee, jvmsp, jvmsp->bci());
      }
      assert(sub != NULL, "should be a sub-ilt here");
      return NULL;
    }
    iltp = sub;
  }
  return iltp;
}

// ----------------------------------------------------------------------------
#ifndef PRODUCT

static void per_method_stats() {
  outputStream* out = Compile::current()->out();
  // Compute difference between this method's cumulative totals and old totals
  int explicit_null_checks_cur = explicit_null_checks_inserted - explicit_null_checks_inserted_old;
  int elided_null_checks_cur = explicit_null_checks_elided - explicit_null_checks_elided_old;

  // Print differences
  if( explicit_null_checks_cur )
out->print_cr("XXX Explicit NULL checks inserted: %d",explicit_null_checks_cur);
  if( elided_null_checks_cur )
out->print_cr("XXX Explicit NULL checks removed at parse time: %d",elided_null_checks_cur);

  // Store the current cumulative totals
  nodes_created_old = nodes_created;
  methods_parsed_old = methods_parsed;
  methods_seen_old = methods_seen;
  explicit_null_checks_inserted_old = explicit_null_checks_inserted;
  explicit_null_checks_elided_old = explicit_null_checks_elided;
}  

#endif
