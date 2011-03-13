/*
 * Copyright 1999-2007 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "c1_CFGPrinter.hpp"
#include "c1_Compilation.hpp"
#include "c1_LinearScan.hpp"
#include "c1_LIRAssembler.hpp"
#include "c1_LIRGenerator.hpp"
#include "c1_Instruction.hpp"
#include "c1_IR.hpp"
#include "c1_ValueMap.hpp"
#include "jvmtiExport.hpp"
#include "timer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "markSweep.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"

typedef enum {
  _t_compile,
  _t_setup,
  _t_optimizeIR,
  _t_buildIR,
  _t_emit_lir,
  _t_linearScan,
  _t_lirGeneration,
  _t_lir_schedule,
  _t_codeemit,
  _t_codeinstall,
  max_phase_timers
} TimerName;

static elapsedTimer timers[max_phase_timers];
static int totalInstructionNodes = 0;

class PhaseTraceTime: public TraceTime {
 private:
  JavaThread* _thread;

 public:
  PhaseTraceTime(TimerName timer):
    TraceTime("", &timers[timer], CITime || CITimeEach, Verbose) {
  }
};

// Implementation of Compilation


#ifndef PRODUCT

size_t Compilation::_c1outputsize = 0;

void Compilation::maybe_print_current_instruction() {
  if (_current_instruction != NULL && _last_instruction_printed != _current_instruction) {
    _last_instruction_printed = _current_instruction;
    _current_instruction->print_line();
  }
}
#endif // PRODUCT


void Compilation::initialize() {
  _masm = new C1_MacroAssembler(strdup(method()->name()->as_utf8()), env()->compile_id());
}


void Compilation::build_hir() {
  CHECK_BAILOUT();

  // setup ir
  _hir = new IR(this, method(), osr_bci());
  if (!_hir->is_valid()) {
    bailout("invalid parsing");
    return;
  }

#ifndef PRODUCT
  if (PrintCFGToFile) {
    CFGPrinter::print_cfg(_hir, "After Generation of HIR", true, false);
  }
#endif

#ifndef PRODUCT
if(PrintCFG||PrintCFG0){C1OUT->print_cr("CFG after parsing");_hir->print(true);}
if(PrintIR||PrintIR0){C1OUT->print_cr("IR after parsing");_hir->print(false);}
#endif

  _hir->verify();

  if (UseC1Optimizations) {
    NEEDS_CLEANUP
    // optimization
    PhaseTraceTime timeit(_t_optimizeIR);

    _hir->optimize();
  }

  _hir->verify();

  _hir->split_critical_edges();

#ifndef PRODUCT
if(PrintCFG||PrintCFG1){C1OUT->print_cr("CFG after optimizations");_hir->print(true);}
if(PrintIR||PrintIR1){C1OUT->print_cr("IR after optimizations");_hir->print(false);}
#endif

  _hir->verify();

  // compute block ordering for code generation
  // the control flow must not be changed from here on
  _hir->compute_code();

  if (UseGlobalValueNumbering) {
    ResourceMark rm;
    int instructions = Instruction::number_of_instructions();
    GlobalValueNumbering gvn(_hir);
    assert(instructions == Instruction::number_of_instructions(),
           "shouldn't have created an instructions");
  }

  // compute use counts after global value numbering
  _hir->compute_use_counts();

#ifndef PRODUCT
if(PrintCFG||PrintCFG2){C1OUT->print_cr("CFG before code generation");_hir->code()->print(true);}
if(PrintIR||PrintIR2){C1OUT->print_cr("IR before code generation");_hir->code()->print(false,true);}
#endif

  _hir->verify();
}


void Compilation::emit_lir() {
  CHECK_BAILOUT();
  
  LIRGenerator gen(this, method());
  {
    PhaseTraceTime timeit(_t_lirGeneration);
hir()->iterate_linear_scan_order(&gen._bc);
  }

  CHECK_BAILOUT();

  {
    PhaseTraceTime timeit(_t_linearScan);

    LinearScan* allocator = new LinearScan(hir(), &gen, frame_map());
    set_allocator(allocator);
    // Assign physical registers to LIR operands using a linear scan algorithm.    
    allocator->do_linear_scan();
    CHECK_BAILOUT();

    _max_spills = allocator->max_spills();
    _masm->set_oopmapBuilder(frame_map()->max_oopregname());
  }

  if (BailoutAfterLIR) {
    if (PrintLIR && !bailed_out()) {
      print_LIR(hir()->code());
    }
    bailout("Bailing out because of -XX:+BailoutAfterLIR");
  }
}


void Compilation::emit_code_epilog(LIR_Assembler* assembler) {
  CHECK_BAILOUT();

  // generate code or slow cases
  assembler->emit_slow_case_stubs();
  CHECK_BAILOUT();

  // generate exception adapters
  assembler->emit_exception_entries(exception_info_list());
  CHECK_BAILOUT();

  // generate code for exception handler
  assembler->emit_exception_handler();
  CHECK_BAILOUT();

  // Azul deopt handler emitted in pre_bake
  //assembler->emit_deopt_handler();
  //CHECK_BAILOUT();
}


int Compilation::emit_code_body() {
  LIR_Assembler lir_asm(this);

  lir_asm.emit_code(hir()->code());
  CHECK_BAILOUT_(0);

  emit_code_epilog(&lir_asm);
  CHECK_BAILOUT_(0);

  generate_exception_handler_table();

  return frame_map()->framesize();
}


int Compilation::compile_java_method() {
  assert(!method()->is_native(), "should not reach here");

  if (BailoutOnExceptionHandlers) {
    if (method()->has_exception_handlers()) {
      bailout("linear scan can't handle exception handlers");
    }
  }

  CHECK_BAILOUT_(no_frame_size);

  {
    PhaseTraceTime timeit(_t_buildIR);
  build_hir();
  }
  if (BailoutAfterHIR) {
    BAILOUT_("Bailing out because of -XX:+BailoutAfterHIR", no_frame_size);
  }


  {
    PhaseTraceTime timeit(_t_emit_lir);

    _frame_map = new FrameMap(method(), hir()->number_of_locks(), 0);
    emit_lir();
  }
  CHECK_BAILOUT_(no_frame_size);

  {
    PhaseTraceTime timeit(_t_codeemit);
    return emit_code_body();
  }
}

void Compilation::install_code(int frame_size) {
  // frame_size is in 32-bit words so adjust it intptr_t words
  assert(frame_size == frame_map()->framesize(), "must match");
  assert(in_bytes(frame_map()->framesize_in_bytes()) % sizeof(intptr_t) == 0, "must be at least pointer aligned");
  _masm->pre_bake(); // append slow-path code for various compiler bits
  _env->register_method(
     method(),
     osr_bci(),
     _masm,
     _cp,                      // CodeProfile
     frame_map()->framesize_in_bytes(),
     _generic_exception_handler,
     has_unsafe_access()
                        );
}


void Compilation::compile_method() {
  // setup compilation
  initialize();

if(!method()->is_c1_compilable()){
    // Prevent race condition 6328518.
    // This can happen if the method is obsolete or breakpointed.
    bailout("Bailing out because method is not compilable");
    return;
  }

  if (JvmtiExport::can_hotswap_or_post_breakpoint()) {
    // We are here to add a note that if the bytecodes for this method were
    // changed under our feet the method being compiled now should be recompiled.
    // For now we don't support hot swapping of bytecode.
    //Untested();
  }

  if (method()->break_at_execute()) {
    BREAKPOINT;
  }

#ifndef PRODUCT
  if (PrintCFGToFile) {
    CFGPrinter::print_compilation(this);
  }
#endif

  // compile method
  int frame_size = compile_java_method();

if(method()->break_c1_after_codegen()){
tty->print("### [c1] Breaking at end of codegen: ");
method()->print_short_name(tty);
tty->cr();
    BREAKPOINT;
  }

  if (LogCompilerOutput && _c1output!=tty) {
    // Trim compiler logging
    _cp->set_debug_output((stringStream*)_c1output);
  }

  if (FAM) {
    Unimplemented();
    //masm()->_oopmaps->print(tty);
    return;
  }

  // bailout if method couldn't be compiled
  // Note: make sure we mark the method as not compilable!
  CHECK_BAILOUT();

  if (InstallMethods) {
    // install code
    PhaseTraceTime timeit(_t_codeinstall);
    install_code(frame_size);
  }
  if( PrintC1Assembly || method()->should_print_assembly() )
_masm->print(tty);
  totalInstructionNodes += Instruction::number_of_instructions();
}


void Compilation::generate_exception_handler_table() {
  // Generate an ExceptionHandlerTable from the exception handler
  // information accumulated during the compilation.
  ExceptionInfoList* info_list = exception_info_list();

  for (int i = 0; i < info_list->length(); i++) {
    ExceptionInfo* info = info_list->at(i);
    XHandlers* handlers = info->exception_handlers();
    // Get the base of the stack of inlined DebugScopes at this PC offset
    DebugScopeBuilder *dsb = masm()->get_dbg(info->pco());
    
    for (int i = 0; i < handlers->length(); i++) {
      XHandler* handler = handlers->handler_at(i);
assert(handler->entry_lbl(),"must have been generated");

      // Find the correct DebugScope for the named inlining depth
      int d = handler->scope_count();
      DebugScopeBuilder *dsb2 = dsb;
      while( d-- ) {  dsb2 = dsb2->caller(); }
      dsb2->add_exception(masm(),handler->handler_bci(),handler->entry_lbl());

      // stop processing once we hit a catch any
      if (handler->is_catch_all()) {
        assert(i == handlers->length() - 1, "catch all must be last handler");
      }
    }
  }
}


Compilation::Compilation(AbstractCompiler* compiler, ciEnv* env, ciMethod* method, int osr_bci)
: _compiler(compiler)
, _env(env)
, _method(method)
, _osr_bci(osr_bci)
, _hir(NULL)
, _max_spills(-1)
, _frame_map(NULL)
, _masm(NULL)
, _has_exception_handlers(false)
, _has_fpu_code(true)   // pessimistic assumption
, _has_unsafe_access(false)
, _bailout_msg(NULL)
, _exception_info_list(NULL)
, _allocator(NULL)
, _current_instruction(NULL)
#ifndef PRODUCT
, _last_instruction_printed(NULL)
#endif // PRODUCT
, _cpdata_array_offset(0) // initial offset
, _total_cpdata_array_size(method->bci2cpd_map()->maxsize()) // profile size for top-level data
{
  PhaseTraceTime timeit(_t_compile);

  // Allocate a new copy of code profile. If we clone the existing cp, the
  // _total_cpdata_array_size set above needs to be reset accordingly.
  _cp = CodeProfile::make(method);
  NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_c1_in_use, 1);)

  C1CompilerThread::current()->_compile = this;

  outputStream* tout = LogCompilerOutput ? new (ResourceObj::C_HEAP) stringStream(false) : NULL;
  _c1output = tout ? tout : tty;

  _exception_info_list = new ExceptionInfoList();
  compile_method();
}

Compilation::~Compilation() {
  assert0(_cp);
  // compilation has failed and we need to free the cp here since it is not
  // attached to anything (methodOop or nmethod)
if(CURRENT_ENV->failing()){
    _cp->free(CodeProfile::_c1_failure);
  }
}

int Compilation::inline_and_push_cpdata(int bci, ciMethod* callee, ciMethod *caller) {
  int inloff = _cpdata_array_offset; // Current CP inline-offset
  int oldsize = _cpdata_array_offset = _total_cpdata_array_size; // New one starts at the end
  _cp = _cp->grow_to_inline( oldsize, inloff, caller, bci, callee );
  _total_cpdata_array_size += callee->bci2cpd_map()->maxsize(); // add profile size for top-level method
  return inloff;
}

void Compilation::revert_codeprofile(CodeProfile* oldcp) { 
  // Free the current codeprofile and replace it with oldcp
  _cp->free(CodeProfile::_inline_decision_freed);
  _cp = oldcp; 
}


void Compilation::add_exception_handlers_for_pco(int pco, XHandlers* exception_handlers) {
#ifndef PRODUCT
  if (PrintExceptionHandlers && Verbose) {
C1OUT->print_cr("  added exception scope for pco %d",pco);
  }
#endif
  // Note: we do not have program counters for these exception handlers yet
  exception_info_list()->push(new ExceptionInfo(pco, exception_handlers));
}


void Compilation::notice_inlined_method(ciMethod* method) {
  _env->notice_inlined_method(method);
}


void Compilation::bailout(const char* msg) {
  assert(msg != NULL, "bailout message must exist");
  if (!bailed_out()) {
    // keep first bailout message
if(PrintBailouts)C1OUT->print_cr("compilation bailout: %s",msg);
    _bailout_msg = msg;
  }
  method()->set_not_compilable(1);
}


void Compilation::print_timers() {
  // C1OUT->print_cr("    Native methods         : %6.3f s, Average : %2.3f", CompileBroker::_t_native_compilation.seconds(), CompileBroker::_t_native_compilation.seconds() / CompileBroker::_total_native_compile_count);
  float total = timers[_t_setup].seconds() + timers[_t_buildIR].seconds() + timers[_t_emit_lir].seconds() + timers[_t_lir_schedule].seconds() + timers[_t_codeemit].seconds() + timers[_t_codeinstall].seconds();


C1OUT->print_cr("    Detailed C1 Timings");
C1OUT->print_cr("       Setup time:        %6.3f s (%4.1f%%)",timers[_t_setup].seconds(),(timers[_t_setup].seconds()/total)*100.0);
C1OUT->print_cr("       Build IR:          %6.3f s (%4.1f%%)",timers[_t_buildIR].seconds(),(timers[_t_buildIR].seconds()/total)*100.0);
C1OUT->print_cr("         Optimize:           %6.3f s (%4.1f%%)",timers[_t_optimizeIR].seconds(),(timers[_t_optimizeIR].seconds()/total)*100.0);
C1OUT->print_cr("       Emit LIR:          %6.3f s (%4.1f%%)",timers[_t_emit_lir].seconds(),(timers[_t_emit_lir].seconds()/total)*100.0);
C1OUT->print_cr("         LIR Gen:          %6.3f s (%4.1f%%)",timers[_t_lirGeneration].seconds(),(timers[_t_lirGeneration].seconds()/total)*100.0);
C1OUT->print_cr("         Linear Scan:      %6.3f s (%4.1f%%)",timers[_t_linearScan].seconds(),(timers[_t_linearScan].seconds()/total)*100.0);
  NOT_PRODUCT(LinearScan::print_timers(timers[_t_linearScan].seconds()));
C1OUT->print_cr("       LIR Schedule:      %6.3f s (%4.1f%%)",timers[_t_lir_schedule].seconds(),(timers[_t_lir_schedule].seconds()/total)*100.0);
C1OUT->print_cr("       Code Emission:     %6.3f s (%4.1f%%)",timers[_t_codeemit].seconds(),(timers[_t_codeemit].seconds()/total)*100.0);
C1OUT->print_cr("       Code Installation: %6.3f s (%4.1f%%)",timers[_t_codeinstall].seconds(),(timers[_t_codeinstall].seconds()/total)*100.0);
C1OUT->print_cr("       Instruction Nodes: %6d nodes",totalInstructionNodes);

  NOT_PRODUCT(LinearScan::print_statistics());
}


#ifndef PRODUCT
void Compilation::compile_only_this_method() {
  ResourceMark rm;
Untested();
fileStream stream("c1_compile_only");
  stream.print_cr("# c1 compile only directives");
  compile_only_this_scope(&stream, hir()->top_scope());
}


void Compilation::compile_only_this_scope(outputStream* st, IRScope* scope) {
  st->print("CompileOnly=");
  scope->method()->holder()->name()->print_symbol_on(st);
  st->print(".");
  scope->method()->name()->print_symbol_on(st);
  st->cr();
}


void Compilation::exclude_this_method() {
fileStream stream(".hotspot_compiler");
  stream.print("exclude ");
  method()->holder()->name()->print_symbol_on(&stream);
  stream.print(" ");
  method()->name()->print_symbol_on(&stream);
  stream.cr();
  stream.cr();
}
#endif
