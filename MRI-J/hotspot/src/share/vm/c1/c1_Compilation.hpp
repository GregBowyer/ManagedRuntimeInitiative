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
#ifndef C1_COMPILATION_HPP
#define C1_COMPILATION_HPP


#include "abstractCompiler.hpp"
#include "array.hpp"
#include "ciEnv.hpp"
#include "codeProfile.hpp"
#include "commonAsm.hpp"

class C1_MacroAssembler;
class CompilationResourceObj;
class ExceptionInfo;
class FrameMap;
class IR;
class IRScope;
class Instruction;
class LIR_Assembler;
class LIR_OprDesc;
class LinearScan;
class XHandlers;
class ciEnv;
class ciMethod;
typedef LIR_OprDesc* LIR_Opr;

define_array(BasicTypeArray, BasicType)
define_stack(BasicTypeList, BasicTypeArray)

define_array(ExceptionInfoArray, ExceptionInfo*)
define_stack(ExceptionInfoList,  ExceptionInfoArray)

class Compilation: public StackObj {
  friend class CompilationResourceObj;
 private:
  // compilation specifics
  AbstractCompiler*  _compiler;
  ciEnv*             _env;
  ciMethod*          _method;
  int                _osr_bci;
  IR*                _hir;
  int                _max_spills;
  FrameMap*          _frame_map;
  C1_MacroAssembler* _masm;
  bool               _has_exception_handlers;
  bool               _has_fpu_code;
  bool               _has_unsafe_access;
  const char*        _bailout_msg;
  ExceptionInfoList* _exception_info_list;
  LinearScan*        _allocator;

  // compilation helpers
  void initialize();
  void build_hir();
  void emit_lir();

  void emit_code_epilog(LIR_Assembler* assembler);
  int  emit_code_body();

  int  compile_java_method();
  void install_code(int frame_size);
  void compile_method();

  void generate_exception_handler_table();

  ExceptionInfoList* exception_info_list() const { return _exception_info_list; }
  
  LinearScan* allocator()                          { return _allocator;      }
  void        set_allocator(LinearScan* allocator) { _allocator = allocator; }

  Instruction*       _current_instruction;       // the instruction currently being processed
#ifndef PRODUCT
  Instruction*       _last_instruction_printed;  // the last instruction printed during traversal
#endif // PRODUCT

  // Offset to the start of the CPData array for this inlined method
  int _cpdata_array_offset;
  // Total size of all inline methods' CPData arrays.  Lazily grows as inlines
  // are discovered.
  int _total_cpdata_array_size;
  CodeProfile *_cp;
public:
  int inline_and_push_cpdata(int bci, ciMethod* callee, ciMethod *caller);
  void pop_cpdata(int old ) { _cpdata_array_offset = old; }
  int get_cpdata() { return _cpdata_array_offset; }
  CodeProfile *codeprofile() const { return _cp; }
  // Used for going back to a previous good codeprofile if inlining fails
  // after the current codeprofile has been stretched.
  void revert_codeprofile(CodeProfile* oldcp);

 public:
  // creation
  Compilation(AbstractCompiler* compiler, ciEnv* env, ciMethod* method, int osr_bci);
  ~Compilation();

  static Compilation* current_compilation()      { return C1CompilerThread::current()->_compile; }

  // accessors
  ciEnv* env() const                             { return _env; }
  AbstractCompiler* compiler() const             { return _compiler; }
  bool has_exception_handlers() const            { return _has_exception_handlers; }
  bool has_fpu_code() const                      { return _has_fpu_code; }
  bool has_unsafe_access() const                 { return _has_unsafe_access; }
  ciMethod* method() const                       { return _method; }
  int osr_bci() const                            { return _osr_bci; }
  bool is_osr_compile() const                    { return osr_bci() >= 0; }
  IR* hir() const                                { return _hir; }
  int max_spills() const                         { return _max_spills; }
  FrameMap* frame_map() const                    { return _frame_map; }
  C1_MacroAssembler* masm() const                { return _masm; }
  Label _generic_exception_handler; // optionally unlock sync meth, and jump to forward_exception

  // setters
  void set_has_exception_handlers(bool f)        { _has_exception_handlers = f; }
  void set_has_fpu_code(bool f)                  { _has_fpu_code = f; }
  void set_has_unsafe_access(bool f)             { _has_unsafe_access = f; }
  // Add a set of exception handlers covering the given PC offset
  void add_exception_handlers_for_pco(int pco, XHandlers* exception_handlers);
  // Statistics gathering
  void notice_inlined_method(ciMethod* method);

  Instruction* current_instruction() const       { return _current_instruction; }
  Instruction* set_current_instruction(Instruction* instr) {
    Instruction* previous = _current_instruction;
    _current_instruction = instr;
    return previous;
  }

#ifndef PRODUCT
  void maybe_print_current_instruction();
#endif // PRODUCT

  // error handling
  void bailout(const char* msg);
  bool bailed_out() const                        { return _bailout_msg != NULL; }
  const char* bailout_msg() const                { return _bailout_msg; }

  // timers
  static void print_timers();

#ifndef PRODUCT
  // debugging support.
  // produces a file named c1compileonly in the current directory with
  // directives to compile only the current method and it's inlines.
  // The file can be passed to the command line option -XX:Flags=<filename>
  void compile_only_this_method();
  void compile_only_this_scope(outputStream* st, IRScope* scope);
  void exclude_this_method();
#endif // PRODUCT

 public:
  outputStream* out() const { return _c1output; }
#define C1OUT (Thread::current()->is_C1Compiler_thread() ? ((C1CompilerThread*)Thread::current())->_compile->_c1output : tty)

outputStream*_c1output;
#ifndef PRODUCT
  static size_t _c1outputsize;
#endif
};


// Macro definitions for unified bailout-support 
// The methods bailout() and bailed_out() are present in all classes
// that might bailout, but forward all calls to Compilation
#define BAILOUT(msg)               { bailout(msg); return;              }
#define BAILOUT_(msg, res)         { bailout(msg); return res;          }

#define CHECK_BAILOUT()            { if (bailed_out()) return;          }
#define CHECK_BAILOUT_(res)        { if (bailed_out()) return res;      }


class InstructionMark: public StackObj {
 private:
  Compilation* _compilation;
  Instruction*  _previous;
  
 public:
  InstructionMark(Compilation* compilation, Instruction* instr) {
    _compilation = compilation;
    _previous = _compilation->set_current_instruction(instr);
  }
  ~InstructionMark() {
    _compilation->set_current_instruction(_previous);
  }
};


//----------------------------------------------------------------------
// Base class for objects allocated by the compiler in the compilation arena
class CompilationResourceObj ALLOCATION_SUPER_CLASS_SPEC {
 public:
void*operator new(size_t size){return ciEnv::current()->arena()->Amalloc(size);}
  void  operator delete(void* p) {} // nothing to do
};


//----------------------------------------------------------------------
// Class for aggregating exception handler information.

// Effectively extends XHandlers class with PC offset of
// potentially exception-throwing instruction.
// This class is used at the end of the compilation to build the
// ExceptionHandlerTable.
class ExceptionInfo: public CompilationResourceObj {
 private:
  int             _pco;                // PC of potentially exception-throwing instruction
  XHandlers*      _exception_handlers; // flat list of exception handlers covering this PC

 public:
  ExceptionInfo(int pco, XHandlers* exception_handlers)
    : _pco(pco)
    , _exception_handlers(exception_handlers)
  { }

  int pco()                                      { return _pco; }
  XHandlers* exception_handlers()                { return _exception_handlers; }
};

#endif // C1_COMPILATION_HPP
