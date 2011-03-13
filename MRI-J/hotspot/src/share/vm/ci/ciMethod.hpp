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
#ifndef CIMETHOD_HPP
#define CIMETHOD_HPP


#include "bytecodes.hpp"
#include "ciFlags.hpp"
#include "ciInstanceKlass.hpp"
#include "ciObject.hpp"
#include "ciSignature.hpp"
#include "methodLiveness.hpp"
#include "vmSymbols.hpp"

class BitMap;
class BCI2CPD_mapping;
class ciMethodBlocks;
class CodeProfile;
class MethodLiveness;


// ciMethod
//
// This class represents a methodOop in the HotSpot virtual
// machine.
class ciMethod : public ciObject {
  friend class CompileBroker;
  CI_PACKAGE_ACCESS
  friend class ciEnv;
  friend class ciExceptionHandlerStream;
  friend class Compilation;	// for C1
  friend class Compile;		// for C2
  friend class Parse;		// for C2 in do_call()
  friend class ClassLoader;
  friend class CommonAsm;

 private:
  // General method information.
  ciFlags          _flags;
  ciSymbol*        _name;
  ciInstanceKlass* _holder;
  ciSignature*     _signature;
  ciMethodBlocks*   _method_blocks;

  // Code attributes.
  int _code_size;
  int _max_stack;
  int _max_locals;
  vmIntrinsics::ID _intrinsic_id;
  int _handler_count;
  int _invocation_count;
  int _objectId;
  BCI2CPD_mapping *_bci2cpd_mapping;
  BCI2CPD_mapping *bci2cpd_impl();

  int  _number_of_breakpoints;

  int  _instructions_size;

  bool _has_unloaded_classes_in_sig, _has_unloaded_classes_in_sig_initialized;
  bool _is_empty_method, _is_empty_method_initialized;
  bool _is_vanilla_constructor, _is_vanilla_constructor_initialized;
  bool _has_linenumber_table, _has_linenumber_table_initialized;
  bool _has_loops;
  bool _has_jsrs;
  bool _is_accessor;
  bool _is_initializer;

  bool _has_c2_code;

  CodeProfile* _codeprofile;

  Dict* _monomorphic_lookup;
  Dict* _resolve_invoke_lookup;
  Dict* _resolve_vtable_lookup;

  bool _compressed_linenumber_table_initialized;
  u_char* _compressed_linenumber_table;

  bool _has_monitors;
  bool _balanced_monitors;
  bool _compilable;
  bool _can_be_statically_bound;

  // Lazy fields, filled in on demand
  address              _code;
  ciExceptionHandler** _exception_handlers;

  // Optional liveness analyzer.
  MethodLiveness* _liveness;
  ciTypeFlow*     _flow;

  ciMethod(methodHandle h_m);
  ciMethod(ciInstanceKlass* holder, ciSymbol* name, ciSymbol* signature);

  ciMethod(FAMPtr old_cim);
  virtual void fixupFAMPointers();

  methodOop get_methodOop() const {
    methodOop m = (methodOop)get_oop();
    assert(m != NULL, "illegal use of unloaded method");
    return m;
  }

  oop loader() const                             { return _holder->loader(); }

  const char* type_string()                      { return "ciMethod"; }

  void print_impl(outputStream* out) const;

  void load_code();

  void check_is_loaded() const;

  void code_at_put(int bci, Bytecodes::Code code) {
    Bytecodes::check(code);
    assert(0 <= bci && bci < code_size(), "valid bci");
    address bcp = _code + bci;
    *bcp = code;
  }

 public:
  // Basic method information.
  ciFlags flags() const                          { check_is_loaded(); return _flags; }
  ciSymbol* name() const                         { return _name; }
  ciInstanceKlass* holder() const                { return _holder; }
  
  // Signature information.
  ciSignature* signature() const                 { return _signature; }
  ciType*      return_type() const               { return _signature->return_type(); }
  int          arg_size_no_receiver() const      { return _signature->size(); }
  int          arg_size() const                  { return _signature->size() + (_flags.is_static() ? 0 : 1); }

  // Method code and related information.
  address code()                                 { if (_code == NULL) load_code(); return _code; }
  int code_size() const                          { check_is_loaded(); return _code_size; }
  int max_stack() const                          { check_is_loaded(); return _max_stack; }
  int max_locals() const                         { check_is_loaded(); return _max_locals; }
  vmIntrinsics::ID intrinsic_id() const          { check_is_loaded(); return _intrinsic_id; }
  bool has_exception_handlers() const            { check_is_loaded(); return _handler_count > 0; }
  int exception_table_length() const             { check_is_loaded(); return _handler_count; }

  Bytecodes::Code java_code_at_bci(int bci) {
    address bcp = code() + bci;
    return Bytecodes::java_code_at(bcp);
  }
  ciMethodBlocks    *get_method_blocks();

  int line_number_from_bci(int bci) const;

  // Runtime information.
  int           vtable_index();
  int           objectId() const { return _objectId; }
  
  address       native_entry();
  address       interpreter_entry();

  // Analysis and profiling.
  //
  // Usage note: liveness_at_bci should be wrapped in ResourceMarks.
bool has_monitor_bytecodes()const{return _has_monitors;}
  bool          has_balanced_monitors();

  const MethodLivenessResult liveness_at_bci(int bci);

  const BitMap  bci_block_start();

  ciTypeFlow*   get_flow_analysis();
  ciTypeFlow*   get_osr_flow_analysis(int osr_bci);  // alternate entry point

  int           invocation_count() const { check_is_loaded(); return _invocation_count; }
 
  // Given a certain calling environment, find the monomorphic target
  // for the call.  Return NULL if the call is not monomorphic in
  // its calling environment.
ciMethod*find_monomorphic_target(ciInstanceKlass*caller,ciInstanceKlass*callee_holder,ciInstanceKlass*actual_receiver);

  // Given a known receiver klass, find the target for the call.  
  // Return NULL if the call has no target or is abstract.
  ciMethod* resolve_invoke(ciKlass* caller, ciKlass* exact_receiver);

  // Find the proper vtable index to invoke this method.
  int resolve_vtable_index(ciKlass* caller, ciKlass* receiver);

  // Compilation directives
  bool will_link(ciKlass* accessing_klass, 
		 ciKlass* declared_method_holder,
		 Bytecodes::Code bc);
  bool should_exclude();
  bool should_inline();
  bool should_disable_inlining();
  bool should_print_assembly();
  bool should_print_ir();
  bool should_disable_loopopts();
  bool break_at_execute();
  bool has_option(const char *option);
  bool break_c1_after_codegen();
  bool break_c2_after_codegen();
  bool is_c1_compilable() const;
  bool is_c2_compilable() const;
  void set_not_compilable(int c12);
  bool has_c2_code() const;
  int  instructions_size();
  bool is_not_reached(int bci);
  bool was_executed_more_than(int times);
  bool has_unloaded_classes_in_signature();
bool is_klass_loaded(int refinfo_index)const;
  bool check_call(int refinfo_index, bool is_static) const;

  // What kind of ciObject is this?
bool is_method()const{return true;}

  // Java access flags
  bool is_public      () const                   { return flags().is_public(); }
  bool is_private     () const                   { return flags().is_private(); }
  bool is_protected   () const                   { return flags().is_protected(); }
  bool is_static      () const                   { return flags().is_static(); }
  bool is_final       () const                   { return flags().is_final(); }
  bool is_synchronized() const                   { return flags().is_synchronized(); }
  bool is_native      () const                   { return flags().is_native(); }
  bool is_interface   () const                   { return flags().is_interface(); }
  bool is_abstract    () const                   { return flags().is_abstract(); }
  bool is_strict      () const                   { return flags().is_strict(); }

  // Other flags
bool is_vanilla_constructor();
  bool is_empty_method();
  bool is_final_method() const                   { return is_final() || holder()->is_final(); }
  bool has_loops      () const                   { check_is_loaded(); return _has_loops; }
  bool has_jsrs       () const                   { check_is_loaded(); return _has_jsrs; }
  bool is_accessor    () const                   { check_is_loaded(); return _is_accessor; }
  bool is_initializer () const                   { check_is_loaded(); return _is_initializer; }
  bool can_be_statically_bound() const           { check_is_loaded(); return _can_be_statically_bound; }

  bool has_linenumber_table();
  u_char* compressed_linenumber_table();

  void print_codes(outputStream *out, CodeProfile *cp=NULL);
void print_codes(int from,int to,outputStream*out);

  // Support for CodeProfile data
  BCI2CPD_mapping *bci2cpd_map() { return _bci2cpd_mapping ? _bci2cpd_mapping : bci2cpd_impl(); }
  // Find out any codeprofile accessible from the moop and don't care about   
  // the origin. Search c1 nmethods first. If not found then try c2 nmethods,
  // then finally the moop itself. The origin is encoded in from:
  // 0:moop  1:c1  2:c2
  CodeProfile *codeprofile (bool clone);
  CodeProfile *set_codeprofile( CodeProfile *cp );

  // Return the number of various counters
  int get_codeprofile_count(int kind);

  // Return the number of breakpoints
  int number_of_breakpoints();

  // Print the name of this method in various incarnations.
void print_name(outputStream*st=tty)const;
void print_short_name(outputStream*st=tty)const;
};

class ciMethodExtras:public ResourceObj{
 public:
  ciMethodExtras();
  ciMethodExtras(FAMPtr old_extras);

  GrowableArray<bool>* _FAM_is_klass_loaded;
  GrowableArray<bool>* _FAM_check_call;
};

#endif // CIMETHOD_HPP
