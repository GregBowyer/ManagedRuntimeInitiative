/*
 * Copyright 1999-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef C1_CODESTUBS_HPP
#define C1_CODESTUBS_HPP


#include "c1_Compilation.hpp"

#include "c1_Defs.hpp"
#include "c1_FrameMap.hpp"
#include "c1_globals.hpp"
#include "c1_LIR.hpp"
#include "c1_MacroAssembler.hpp"
#include "c1_Runtime1.hpp"
#include "nativeInst_pd.hpp"
#include "ostream.hpp"

class CodeEmitInfo;
class LIR_Assembler;
class LIR_OpVisitState;

// CodeStubs are little 'out-of-line' pieces of code that
// usually handle slow cases of operations. All code stubs
// are collected and code is emitted at the end of the
// nmethod.

class CodeStub: public CompilationResourceObj {
 protected:
  Label _entry;                                  // label at the stub entry point
  Label _continuation;                           // label where stub continues, if any

 public:
  CodeStub() {}

  // code generation
  virtual void emit_code(LIR_Assembler* e) = 0;
  virtual CodeEmitInfo* info() const             { return NULL; }
  virtual bool is_exception_throw_stub() const   { return false; }
  virtual bool is_range_check_stub() const       { return false; }
  virtual bool is_divbyzero_stub() const         { return false; }
  virtual bool is_patching_stub() const          { return false; }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const = 0;
#endif

  // label access
  Label* entry()                                 { return &_entry; }
  Label* continuation()                          { return &_continuation; }
  // for LIR
  virtual void visit(LIR_OpVisitState* visit) {
#ifndef PRODUCT
    if (LIRTracePeephole && Verbose) {
      tty->print("no visitor for ");
      print_name(tty);
      tty->cr();
    }
#endif
  }
};


define_array(CodeStubArray, CodeStub*)
define_stack(_CodeStubList, CodeStubArray)

class CodeStubList: public _CodeStubList {
 public:
  CodeStubList(): _CodeStubList() {}

  void append(CodeStub* stub) {
    if (!contains(stub)) {
      _CodeStubList::append(stub);
    }
  }
};

#ifdef TIERED
class CounterOverflowStub: public CodeStub {
 private:
  CodeEmitInfo* _info;
  int           _bci;

public:
  CounterOverflowStub(CodeEmitInfo* info, int bci) : _info(info), _bci(bci) {
  }

  virtual void emit_code(LIR_Assembler* e);

  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
  }

#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("CounterOverflowStub"); }
#endif // PRODUCT

};
#endif // TIERED

class ConversionStub: public CodeStub {
 private:
  Bytecodes::Code _bytecode;
  LIR_Opr         _input;
  LIR_Opr         _result; 

  static float float_zero;
  static double double_zero;
 public:
  ConversionStub(Bytecodes::Code bytecode, LIR_Opr input, LIR_Opr result)
    : _bytecode(bytecode), _input(input), _result(result) {
  }

  Bytecodes::Code bytecode() { return _bytecode; }
  LIR_Opr         input()    { return _input; }
  LIR_Opr         result()   { return _result; }

  virtual void emit_code(LIR_Assembler* e);
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case();
    visitor->do_input(_input);
    visitor->do_output(_result);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("ConversionStub"); }
#endif // PRODUCT
};


// Throws ArrayIndexOutOfBoundsException by default but can be
// configured to throw IndexOutOfBoundsException in constructor
class RangeCheckStub: public CodeStub {
 private:
  CodeEmitInfo* _info;
  LIR_Opr       _index;
  bool          _throw_index_out_of_bounds_exception;

 public:
  RangeCheckStub(CodeEmitInfo* info, LIR_Opr index, bool throw_index_out_of_bounds_exception = false);
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual bool is_range_check_stub() const       { return true; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
    visitor->do_input(_index);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("RangeCheckStub"); }
#endif // PRODUCT
};


class DivByZeroStub: public CodeStub {
 private:
  CodeEmitInfo* _info;
  int           _offset;

 public:
  DivByZeroStub(CodeEmitInfo* info)
    : _info(info), _offset(-1) {
  }
  DivByZeroStub(int offset, CodeEmitInfo* info)
    : _info(info), _offset(offset) {
  }
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual bool is_divbyzero_stub() const         { return true; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("DivByZeroStub"); }
#endif // PRODUCT
};


class ImplicitNullCheckStub: public CodeStub {
 private:
  CodeEmitInfo* _info;
  int           _offset;

 public:
  ImplicitNullCheckStub(int offset, CodeEmitInfo* info)
    : _info(info), _offset(offset) {
  }
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("ImplicitNullCheckStub"); }
#endif // PRODUCT
};


class NewInstanceStub: public CodeStub {
 private:
  ciInstanceKlass* _klass;
  LIR_Opr          _result;
  CodeEmitInfo*    _info;

 public:
NewInstanceStub(LIR_Opr result,ciInstanceKlass*klass,CodeEmitInfo*info);
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
    visitor->do_output(_result);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("NewInstanceStub"); }
#endif // PRODUCT
};


class NewTypeArrayStub: public CodeStub {
 private:
  LIR_Opr       _length;
  LIR_Opr       _result;
  CodeEmitInfo* _info;

 public:
NewTypeArrayStub(LIR_Opr length,LIR_Opr result,CodeEmitInfo*info);
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
    visitor->do_input(_length);
    assert(_result->is_valid(), "must be valid"); visitor->do_output(_result);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("NewTypeArrayStub"); }
#endif // PRODUCT
};


class NewObjectArrayStub: public CodeStub {
 private:
  LIR_Opr        _length;
  LIR_Opr        _result;
  CodeEmitInfo*  _info;

 public:
NewObjectArrayStub(LIR_Opr length,LIR_Opr result,CodeEmitInfo*info);
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
    visitor->do_input(_length);
    assert(_result->is_valid(), "must be valid"); visitor->do_output(_result);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("NewObjectArrayStub"); }
#endif // PRODUCT
};


class MonitorAccessStub: public CodeStub {
 protected:
  LIR_Opr _obj_reg;

 public:
MonitorAccessStub(LIR_Opr obj_reg){
    _obj_reg  = obj_reg;
  }

#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("MonitorAccessStub"); }
#endif // PRODUCT
};


class MonitorEnterStub: public MonitorAccessStub {
 private:
  CodeEmitInfo* _info;

 public:
MonitorEnterStub(LIR_Opr obj_reg,CodeEmitInfo*info);

  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_input(_obj_reg);
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("MonitorEnterStub"); }
#endif // PRODUCT
};


class MonitorExitStub: public MonitorAccessStub {
LIR_Opr _mark_reg;
 public:
  MonitorExitStub(LIR_Opr obj_reg, LIR_Opr mark_reg);

  virtual void emit_code(LIR_Assembler* e);
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_input(_obj_reg);
visitor->do_temp(_mark_reg);
    visitor->do_slow_case();
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("MonitorExitStub"); }
#endif // PRODUCT
};

// Shared Patching Logic
class PatchingStub: public CodeStub {
public:
  Label          _start;        // start of ld/lvb needing patching
  Label          _patch_code; // enter runtime to do patching for this stub
  int            _patch_offset1; // load or store op to patch
  int            _patch_offset2; // lvb to patch
  CodeEmitInfo* const _info;
  void align_patch_site(MacroAssembler* masm);

  PatchingStub(MacroAssembler* masm, CodeEmitInfo* info) : _patch_offset1(0), _patch_offset2(0), _info(info) { }

  virtual bool is_patching_stub() const         { return true; }
  virtual void visit(LIR_OpVisitState* visitor) { visitor->do_slow_case(_info); }
  virtual void emit_code(LIR_Assembler* e) = 0;
  virtual const char *name() const = 0;
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print(name()); }
#endif // PRODUCT
};

// LoadKlass Patching
class LK_PatchingStub: public PatchingStub {
public:
Label _thread_test;
Label _post_patch;
  const Register _obj;  // register that will hold loaded value from klass table
  const Register _tmp1; // temp register
  const Register _tmp2; // temp register
  LK_PatchingStub(MacroAssembler* masm, Register obj, Register tmp1, Register tmp2, CodeEmitInfo* info);
  void init();
  virtual void emit_code(LIR_Assembler* e);
virtual const char*name()const{return"LK_PatchingStub";}
};
  
// AccessField Patching
class AF_PatchingStub: public PatchingStub {
public:
  AF_PatchingStub(MacroAssembler* masm, CodeEmitInfo* info);
  void init();
  virtual void emit_code(LIR_Assembler* e);
virtual const char*name()const{return"AF_PatchingStub";}
};


class UncommonTrapStub:public CodeStub{
 private:
  CodeEmitInfo*    _info;
  int              _deopt_flavor;

 public:
  UncommonTrapStub(int deopt_flavor, CodeEmitInfo* info):
    _deopt_flavor(deopt_flavor), _info(info) {
    FrameMap::rdi_opr->set_destroyed();
  }

  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_temp(FrameMap::rdi_opr);
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
virtual void print_name(outputStream*out)const{out->print("UncommonTrapStub");}
#endif // PRODUCT
};



class SimpleExceptionStub: public CodeStub {
 private:
  CodeEmitInfo*    _info;
  LIR_Opr          _obj;
  Runtime1::StubID _stub;

 public:
  SimpleExceptionStub(Runtime1::StubID stub, LIR_Opr obj, CodeEmitInfo* info):
    _info(info), _obj(obj), _stub(stub) {
  }

LIR_Opr obj()const{return _obj;}
  virtual void emit_code(LIR_Assembler* e);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual void visit(LIR_OpVisitState* visitor) {
    if (_obj->is_valid()) { visitor->do_input(_obj); visitor->do_temp(_obj); }
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("SimpleExceptionStub"); }
#endif // PRODUCT
};

class ArrayStoreExceptionStub: public CodeStub {
 private:
  CodeEmitInfo* _info;

 public:
  ArrayStoreExceptionStub(CodeEmitInfo* info);
  virtual void emit_code(LIR_Assembler* emit);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual void visit(LIR_OpVisitState* visitor) {
    visitor->do_slow_case(_info);
  }
#ifndef PRODUCT
  virtual void print_name(outputStream* out) const { out->print("ArrayStoreExceptionStub"); }
#endif // PRODUCT
};

class SafepointStub:public CodeStub{
 private:
  CodeEmitInfo* _info;

 public:
SafepointStub(CodeEmitInfo*info);
  virtual void emit_code(LIR_Assembler* emit);
  virtual CodeEmitInfo* info() const             { return _info; }
  virtual bool is_exception_throw_stub() const   { return true; }
  virtual void visit(LIR_OpVisitState* visitor)  { visitor->do_slow_case(_info); }
#ifndef PRODUCT
virtual void print_name(outputStream*out)const{out->print("SafepointStub");}
#endif // PRODUCT
};

#endif // C1_CODESTUBS_HPP
