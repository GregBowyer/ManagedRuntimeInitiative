/*
 * Copyright 2000-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_globals.hpp"
#include "c2_globals.hpp"
#include "codeProfile.hpp"
#include "commonAsm.hpp"
#include "compilationPolicy.hpp"
#include "compileBroker.hpp"
#include "compilerOracle.hpp"
#include "frame.inline.hpp"
#include "log.hpp"
#include "nativeInst_pd.hpp"
#include "ostream.hpp"
#include "vframe.hpp"

#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"

elapsedTimer       CompilationPolicy::_accumulated_time;

// Returns true if m must be compiled before executing it.
// This is intended to force compiles for methods (usually for
// debugging) that would otherwise be interpreted for some reason. 
bool CompilationPolicy::mustBeCompiled(methodHandle m,int c12){
if(UseInterpreter)return false;
  if (c12 == 1 && m->lookup_c1()) return false; // c1 already compiled
  if (c12 == 2 && UseC1 && UseC2)         return false; // In tiered Xcomp, only C1 MUST be compiled
  if (c12 == 2 && m->lookup_c2()) return false; // c2 already compiled
  if (!canBeCompiled(m,c12))  return false;

  return true;                  // must compile all methods
}

// Returns true if m is allowed to be compiled   
bool CompilationPolicy::canBeCompiled(methodHandle m,int c12){
if(m->is_abstract()||m->is_native())return false;
  Klass *holder = m->method_holder()->klass_part();
if(holder->oop_is_instance()){
instanceKlass*ik=(instanceKlass*)holder;
    if( c12 == 2 &&             // For C2 only (C1 inserts a golden-thread-check)
        !ik->is_initialized() && // If a class is not initialized, only compile
        m->name() != vmSymbols::class_initializer_name() && // ... <clinit> (for OSR)
        m->name() != vmSymbols::object_initializer_name() ) // or <init>
      return false;
    if( !m->is_static() &&  // Avoid concrete methods with no possible receiver
        ik->nof_implementors() == 0 )
      return false;
  }
  if (DontCompileHugeMethods && m->code_size() > HugeMethodLimit) return false;
  return c12==1 ? m->is_c1_compilable() : m->is_c2_compilable();
}

#ifndef PRODUCT
void CompilationPolicy::print_time() {
  tty->print_cr ("Accumulated compilationPolicy times:");
  tty->print_cr ("---------------------------");
  tty->print_cr ("  Total: %3.3f sec.", _accumulated_time.seconds());
}

static void trace_osr_completion(methodCodeOop osr_mco) {
  if (TraceOnStackReplacement) {
if(osr_mco==NULL)tty->print_cr("compilation failed");
    else tty->print_cr("methodCodeOop " INTPTR_FORMAT, (intptr_t)osr_mco);
  }
}
#endif // !PRODUCT

void CompilationPolicy::method_back_branch_event(methodHandle m, int loop_top_bci) {
if(m->is_c2_compilable()){
    CompileBroker::_c2.producer_add_task(m, m, loop_top_bci);
    NOT_PRODUCT(trace_osr_completion(m->lookup_osr_for(loop_top_bci)));
  }
}

static void trace( int c12, methodHandle m, const char *msg, BufferedLoggerMark *blm ) {
  if( blm )
    blm->out("C%d->%d : %40.40s : %s\n",(UseC1 ? c12-1 : 0),c12,msg, m.is_null() ? "" : m->name_as_C_string());
}


// Consider m for compilation
void CompilationPolicy::method_invocation_event(methodHandle m, int c12, address continue_in_c1) {
  ResourceMark rm;
RFrame*top;
  { 
  BufferedLoggerMark blm(NOTAG,0,TraceCompilationPolicy );
  TraceTime t1( "CompilationPolicy", &_accumulated_time, TimeCompilationPolicy );

  methodCodeOop c1co = m->lookup_c1();
  methodCodeOop c2co = m->lookup_c2();

  // Reset the counters so I quit getting frequency_counter_overflow events.
  if( c12 == 1 || !UseC1 ) {    // Interpreter overflowed?
m->invocation_counter()->reset();
m->backedge_counter()->reset();
  } else {                      // C1 overflowed?
    if (c1co) {
      CodeProfile *cp = c1co->get_codeprofile();
cp->reset_invoke_count();
cp->reset_backedge_count();
    }
  }
  // Out of CodeCache?  Do not bother compiling
  if (CodeCache::is_full())
    return;                     // Do not bother launching a compile!

  methodCodeOop co = c12==1 ? c1co : c2co;
  if( co ) {                    // Quick cutout if we have already got code
char buf[40];
sprintf(buf,"already has C%d code",c12);
    trace( c12, m, buf, &blm);
    return;
  }
  if( !canBeCompiled(m,c12) ) { // Quick cutout if the oracle is denying the compile
    trace( c12, m, "!canBeCompiled", &blm);
    return;
  }

  // trigger is the Java frame that triggered its counter
  JavaThread *thread = JavaThread::current();
frame trigger=thread->last_frame();
  if( !trigger.is_known_frame() ) // Could be a runtimestub
    trigger = trigger.sender();   // Exactly skip the runtimestub if needed
  if( continue_in_c1 ) {        // trigger frame was never built, but is a C1 frame
CodeBlob*cb=CodeCache::find_blob(continue_in_c1);
    trigger = frame(trigger.sp()-(cb->framesize_bytes()>>3),continue_in_c1);
  }
  RFrame* first = trigger.is_compiled_frame() 
    ? (RFrame*)new CompiledRFrame   (trigger, thread)
    : (RFrame*)new InterpretedRFrame(trigger, thread, m);
  top = findTopInlinableFrame(first, c12, TraceCompilationPolicy ? &blm : NULL);
  assert(top != NULL, "findTopInlinableFrame returned null");
  } // End timing & logging
  (c12==1 ? &CompileBroker::_c1 : &CompileBroker::_c2)->producer_add_task(top->top_method(), m, InvocationEntryBci);
}

RFrame* CompilationPolicy::findTopInlinableFrame(RFrame *current, int c12, BufferedLoggerMark *blm) {
  // go up the stack until finding a frame that (probably) won't be inlined
  // into its caller
char buf[100];//for nice tracing messages
  const char* msg = NULL; 
  methodHandle m;
methodHandle next_m;

  while (1) {
    m = current->top_method();
    next_m = methodHandle();    // wipe to null
    // before going up the stack further, check if doing so would get us into
    // compiled code
RFrame*next=current->caller();
    if( !next ) {             // No next frame up the stack?
msg="(no next frame)";
      break;                  // Then compile with current frame
    }
next_m=next->top_method();

    if( !Inline ) {           // Inlining turned off
      msg = "Inlining turned off";
      break;
    }

    if (c12==1 ? !next_m->is_c1_compilable() : !next_m->is_c2_compilable()) { // Did fail to compile this before
      msg = "caller not compilable";
      break;
    }

    methodCodeOop c1co = next_m->lookup_c1();
    methodCodeOop code = next_m->codeRef().as_methodCodeOop();
    int nx = (code!=NULL && code->_blob->is_c2_method()) ? 2 : (c1co!=NULL ? 1 : 0);

    if( c12 <= nx ) {
      // Compiled frame above already decided not to inline;
      // do not recompile him.
      msg = (nx==1) ? "not going up into c1 optimized code" : "not going up into c2 optimized code";
      break;
    }
    if( (UseC1 && nx < c12-1) || (nx < c12-2) ) {
msg="not going up into code with lower optimization level";
      break;
    }

    if ( UseC1 && UseC2 && nx == 1 && c1co->get_codeprofile()->total_count()<MinInliningThreshold ) {
msg="found cold c1 method";
      break;
    }

    // If the caller method is too big or something then we do not want to
    // compile it just to inline a method
    if (!canBeCompiled(next_m,c12)) {
      msg = "caller cannot be compiled";
      break;
    }

    if( next_m->name() == vmSymbols::class_initializer_name() ) {
msg="do not compile <clinit> (OSR ok)";
      break;
    }

    // Find the call-site invoke counts
    address pc = next->fr().pc();
    CodeBlob *cb = next->is_interpreted() ? NULL : CodeCache::find_blob(pc);
    if( cb && cb->is_c2_method() ) {
      // We already checked to see if the current code for the next method is
      // C1 and not C2, but it could be that the actual frame is a not-entrant
      // C2 frame.  Since we already once decided not to inline here, we do
      // not inline again.  A reasonble alternative would be to take this
      // defunct C2 nmethod and "explode" it into a series of C1-sized inlined
      // chunks, and then crawl these virtual C1 frames to see if we can find
      // a better top-level compile point.
msg="not going up into c2 optimized code that was made not-entrant";
      break;
    }

    // See if the call-site has gone megamorphic; megamorphic sites do not inline
    if( cb && next->is_compiled() && NativeCall::is_call_before(pc) ) {
      const DebugScope *ds = cb->debuginfo(pc);
      if( ds && ds->is_inline_cache() ) {
        NativeCall *ncall = nativeCall_before(pc);
        NativeInlineCache *nic = NativeInlineCache::at(ncall);
        if( nic->is_vcall() || nic->is_icall() ) {
msg="callsite is megamorphic";
          break;
        }
      }
    }

    // See if we are an intrinsic method, which always wins big to inline.
    if( m->intrinsic_id() != vmIntrinsics::_none ) {
      trace( c12, m, "intrinsic", blm);
      current = next;
      continue;
    }

    if (next->num() > MaxRecompilationSearchLength) {
      // don't go up too high when searching for recompilees
msg="next->num > MaxRecompilationSearchLength";
      break;
    }
    if (next->distance() > MaxInterpretedSearchLength) {
      // don't go up too high when searching for recompilees
msg="next->distance > MaxInterpretedSearchLength";
      break;
    }

    // Check inlining negative tests
    if ((msg = shouldNotInline(m, c12, buf, blm)) != NULL) 
      break;

    // Compute how frequent this call site is.  We have current method 'm'.
    // If the next method 'next_m' is interpreted, find the call site and
    // check the various invocation counts.
    int caller_cnt = 1;
    int site_cnt = 1;
    if( c1co ) {                // Have a C1 for 'next'
      JavaThread *thread = JavaThread::current();
      vframe vf(thread);          // The basic Java frame
      while( vf.get_frame().id() != next->fr().id() )
        vf.next();                // Skip up inlined frames
      if( vf.get_frame().is_compiled_frame() ) {
        CodeProfile *cp = NULL;   // CodeProfile holding some C1 inlined data
        CPData *cpd = CodeProfile::find_nested_cpdata(thread,vf,c1co,&cp,false);
        if (cpd) {
          assert0( cpd->is_Invoke() );
          site_cnt = ((CPData_Invoke*)cpd)->site_count();
          caller_cnt = cp->invoke_count() + next_m()->invocation_count();
        }
      }
    }

    if( (msg = shouldInline(m, c12, caller_cnt, site_cnt, buf, blm )) != NULL )
      break;

    current = next;
  }

  trace( c12,      m, msg, blm );
  trace( c12, next_m, "----------", blm );
  return current;
}

const char* CompilationPolicy::shouldInline(methodHandle callee, int c12, int caller_cnt, int site_cnt, char *buf, BufferedLoggerMark *blm ) {
  // positive filter: should send be inlined?  returns NULL (--> yes)
  // or rejection msg

  // Allows targeted inlining
  if( CompilerOracle::should_inline(callee) ) {
    trace( c12, callee, "oracle inlined", blm );
    return NULL;
  }

  // 
int size=callee->code_size();
  if( c12 == 1 ) {
if(size>C1MaxInlineSize){
      if( TraceCompilationPolicy ) {
sprintf(buf,"size %d > %ld",size,C1MaxInlineSize);
        return buf;
      }
return"size > C1MaxInlineSize";
    }
    if (TraceCompilationPolicy) {
sprintf(buf,"size %d <= %ld",size,C1MaxInlineSize);
      trace( c12, callee, buf, blm );
    }
    return NULL;
  }

  // Check for too many throws (and not too huge)
  if( callee->get_codeprofile_count(CodeProfile::_throwout) > InlineThrowCount && size < InlineThrowMaxSize ) {
    if (TraceCompilationPolicy) {
sprintf(buf,"throwout %d > %ld and size %d < %ld",
              callee->get_codeprofile_count(CodeProfile::_throwout), InlineThrowCount,
size,InlineThrowMaxSize);
      trace( c12, callee, buf, blm );
    }
    return NULL;
  }
  
if(size<=C2MaxInlineSize){
    if (TraceCompilationPolicy) {
sprintf(buf,"size %d <= %ld",size,C2MaxInlineSize);
      trace( c12, callee, buf, blm );
    }
    return NULL;
  }

if(size>C2FreqInlineSize){
    if( TraceCompilationPolicy ) {
sprintf(buf,"size %d > %ld",size,C2FreqInlineSize);
      return buf;
    }
return"too big even if frequent";
  }

  // bump the max size if the call is frequent
  // set initial caller count state if not seen before...
  if( caller_cnt == 0 ) caller_cnt=1;
  if( site_cnt < InlineFrequencyCount &&
      // Caller counts / call-site counts; 
      // i.e. is this call site a hot call site for method next_m?
      site_cnt/caller_cnt < InlineFrequencyRatio ) {
    if( TraceCompilationPolicy ) {
      sprintf(buf,"sz %d <= %ld, nothotsite=%d, hotsite=%d, callr=%d",size,C2FreqInlineSize, caller_cnt, site_cnt, caller_cnt);
      return buf;
    }
return"too big and not frequent";
  }

  if (TraceCompilationPolicy) {
    sprintf(buf, "sz %d <= %ld hotsite=%d, callr=%d", size, C2FreqInlineSize, site_cnt, caller_cnt);
    trace( c12, callee, buf, blm );
  }
  return NULL;
}


const char* CompilationPolicy::shouldNotInline(methodHandle m, int c12, char *buf, BufferedLoggerMark *blm) {
  // negative filter: should send NOT be inlined?  returns NULL (--> inline) or rejection msg 
  if (CompilerOracle::should_disable_inlining(m)) return "compiler oracle request";
if(m->is_abstract())return"abstract method";
  // note: we allow ik->is_abstract()
if(!instanceKlass::cast(m->method_holder())->is_initialized())return"method holder not initialized";
if(m->is_native())return"native method";
methodCodeOop mco=m->codeRef().as_methodCodeOop();
if(mco!=NULL&&mco->_blob->code_size()>InlineSmallCode){
    if( TraceCompilationPolicy ) {
      sprintf(buf,"CodeBlob size %d > %ld",mco->_blob->code_size(),InlineSmallCode);
      return buf;
    }
return"methodCode size > InlineSmallCode";
  }

  // use frequency-based objections only for non-trivial methods
  if (m->code_size() <= MaxTrivialSize) return NULL;    
  if (UseInterpreter) {     // don't use counts with -Xcomp
if((m->codeRef().is_null())&&m->was_never_executed())return"never executed";
    intx ct = (c12 == 1) ? C1CompileThreshold : C2CompileThreshold;
    intx lim = MIN2(MinInliningThreshold, ct >> 1);
    if (!m->was_executed_more_than(lim)) {
      if( TraceCompilationPolicy ) {
int cnt=m->invocation_count();
        if( mco && mco->_blob->is_c2_method() )
          cnt += mco->get_codeprofile()->invoke_count();
sprintf(buf,"nontrivial and exec %d < %ld times",cnt,lim);
        return buf;
      }
return"nontrivial and exec < MinInliningThreshold times";
}
}
if(methodOopDesc::has_unloaded_classes_in_signature(m,JavaThread::current()))return"unloaded signature classes";

  return NULL;
}

