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
#ifndef INTERPRETER_HPP
#define INTERPRETER_HPP

#include "bytecodes.hpp"
#include "codeBlob.hpp"
#include "ostream.hpp"
class InterpreterMacroAssembler;

// This file contains the platform-independant parts
// of the interpreter and the interpreter generator.

//------------------------------------------------------------------------------------------------------------------------

// An InterpreterCodelet is a piece of interpreter code. All interpreter code
// is generated into little codelets which contain extra information for
// debugging and printing purposes.  InterpreterCodelets are laid end-to-end
// in the same initial CodeBlob, with a null-sentinel codelet at the end.

class InterpreterCodelet {
 private:
  static InterpreterCodelet *_list; // the list of all codelets
  static short _nextid;
  friend class CodeletMark;
  friend class InterpreterMacroAssembler;
  InterpreterCodelet( const char *desc, Bytecodes::Code bc ) : _description(desc), _size(-9999), _id(_nextid++), _bytecode(bc) {}
 public:
  const char*     const _description; // a description of the codelet, for debugging & printing
  int             const _size;     // the size in bytes of struct+code
  short           const _id;       // Handy for ARTA printing
  Bytecodes::Code const _bytecode; // associated bytecode if any
  u_char                _code[0];  // zero-length array at end of struct marking start of code
  // code for this bytecode goes here...
  // ----

  // Iteration
  static InterpreterCodelet* first() { return _list; }
  InterpreterCodelet* next() const { return (InterpreterCodelet*)((address)this + _size); }
  bool done() { return _size == 0; }

  // Code info
inline address code_begin()const{return(address)round_to((intptr_t)_code,CodeEntryAlignment);}
inline address code_end()const{return(address)this+_size;}
inline int code_size()const{return code_end()-code_begin();}

  // ARTA support
  static InterpreterCodelet* codelet_for_index(int id);
  static InterpreterCodelet* codelet_for_name (const char *name);

  // Debugging
  void print_on(outputStream* st) const;
  inline void print() { print_on(tty); }
  void print_xml_on(xmlBuffer *xb, bool ref) const;
  static void print_xml_on(xmlBuffer*);
};


//------------------------------------------------------------------------------------------------------------------------
// A little wrapper class to group tosca-specific entry points into a unit.
// (tosca = Top-Of-Stack CAche)

class EntryPoint VALUE_OBJ_CLASS_SPEC {
 private:
  address _entry[number_of_states];

 public:
  // Construction
  EntryPoint();
  EntryPoint(address bentry, address centry, address sentry, address aentry, address ientry, address lentry, address fentry, address dentry, address ventry);

  // Attributes
  address entry(TosState state) const;                // return target address for a given tosca state
  void    set_entry(TosState state, address entry);   // set    target address for a given tosca state
  void    print();

  // Comparison
  bool operator == (const EntryPoint& y);             // for debugging only
};


//------------------------------------------------------------------------------------------------------------------------
// A little wrapper class to group tosca-specific dispatch tables into a unit.

class DispatchTable VALUE_OBJ_CLASS_SPEC {
 public:
  enum { length = 1 << BitsPerByte };                 // an entry point for each byte value (also for undefined bytecodes)

 private:
  address _table[number_of_states][length];	      // dispatch tables, indexed by tosca and bytecode

 public:
  // Attributes
  EntryPoint entry(int i) const;                      // return entry point for a given bytecode i
  void       set_entry(int i, EntryPoint& entry);     // set    entry point for a given bytecode i
  address*   table_for(TosState state) 		{ return _table[state]; }
  address*   table_for()			{ return table_for((TosState)0); }
  int	     distance_from(address *table)	{ return table - table_for(); }
  int	     distance_from(TosState state)	{ return distance_from(table_for(state)); }

  // Comparison
  bool operator == (DispatchTable& y);                // for debugging only
};


//------------------------------------------------------------------------------------------------------------------------
// The C++ interface to the bytecode interpreter(s).

class TemplateInterpreter:AllStatic{
  friend class Interpreter;
  static const CodeBlob *_blob; // The final resting place of the interpreter
 public:
  enum MethodKind {        
    zerolocals,                                                 // method needs locals initialization
    zerolocals_synchronized,                                    // method needs locals initialization & is synchronized
    native,                                                     // native method
    native_synchronized,                                        // native method & is synchronized
    empty,                                                      // empty method (code: _return)
    accessor,                                                   // accessor method (code: _aload_0, _getfield, _(a|i)return)
    abstract,                                                   // abstract method (throws an AbstractMethodException)
    java_lang_math_sin,                                         // implementation of java.lang.Math.sin   (x)
    java_lang_math_cos,                                         // implementation of java.lang.Math.cos   (x)
    java_lang_math_tan,                                         // implementation of java.lang.Math.tan   (x)
    java_lang_math_abs,                                         // implementation of java.lang.Math.abs   (x)
    java_lang_math_sqrt,                                        // implementation of java.lang.Math.sqrt  (x)
    java_lang_math_sqrtf,                                       // implementation of java.lang.Math.sqrtf(x)
    java_lang_math_log,                                         // implementation of java.lang.Math.log   (x)
    java_lang_math_log10,                                       // implementation of java.lang.Math.log10 (x)
    number_of_method_entries,
    invalid = -1
  };

  enum SomeConstants {
number_of_return_entries=6,//number of return entry points
number_of_return_addrs=6//number of return addresses
  };    

 protected:
  static address    _remove_activation_preserving_args_entry;   // continuation address when current frame is being popped

static address _throw_ArrayIndexOutOfBoundsException_entry;
static address _throw_ArrayStoreException_entry;
  static address    _throw_ArithmeticException_entry;
static address _throw_ClassCastException_entry;
static address _throw_NullPointerException_entry;
  static address    _throw_StackOverflowError_entry;
static address _throw_exception_entry;

  static address    _remove_activation_entry; // continuation address if an exception is not handled by current frame
  static address    _remove_activation_force_unwind_entry; // continuation address for stack overflow; unlock & pop
  static address    _continue_activation_entry; // continuation address if an exception IS handled by current frame

  static EntryPoint _trace_code;
  static EntryPoint _return_entry[number_of_return_entries];    // entry points to return to from a call
  static EntryPoint _earlyret_entry;                            // entry point to return early from a call
  static EntryPoint _safept_entry;
static address _unpack_and_go;

  static address    _return_3_addrs_by_index[number_of_return_addrs];     // for invokevirtual   return entries
  static address    _return_5_addrs_by_index[number_of_return_addrs];     // for invokeinterface return entries

  static address     _iframe_16_stk_args_entry; // A 'fat' interpreter frame supporting  16 outgoing stack args
  static address    _iframe_256_stk_args_entry; // A 'fat' interpreter frame supporting 256 outgoing stack args
  
  static DispatchTable _active_table;    // the active    dispatch table (used by the interpreter for dispatch)
  static DispatchTable _normal_table;    // the normal    dispatch table (used to set the active table in normal mode)
  static DispatchTable _safept_table;    // the safepoint dispatch table (used to set the active table for safepoints)
  static address       _wentry_point[DispatchTable::length];    // wide instructions only (vtos tosca always)

  // method entry points
  static address    _entry_table[number_of_method_entries];     // entry points for a given method
  
  friend class      TemplateTable;
  friend class      TemplateInterpreterGenerator;
  friend class              InterpreterGenerator;
  friend class      InterpreterMacroAssembler;
  friend class      StubGenerator;  

 public:
  // Initialization/debugging
  static void       initialize(InterpreterMacroAssembler *masm);
  // this only returns whether a pc is within generated code for the interpreter.
  static address    code_start() { return _blob->code_begins(); }
  static address    code_end()   { return _blob->end(); }
static bool contains(address pc){
    const CodeBlob *blob = CodeCache::find_blob(pc);
    return  blob && blob->is_interpreter();  
  }

 public:

  // Method activation
  static MethodKind method_kind(methodHandle m);
  static address    entry_for_kind(MethodKind k)                { assert(0 <= k && k < number_of_method_entries, "illegal kind"); return _entry_table[k]; }
  static address    entry_for_method(methodHandle m)            { return _entry_table[method_kind(m)]; }

  static void       print_method_kind(MethodKind kind)          PRODUCT_RETURN;

  // Runtime support

static address return_entry(TosState state,int length);

  static address    remove_activation_preserving_args_entry()   { return _remove_activation_preserving_args_entry; }

  static address    remove_activation_early_entry(TosState state) { return _earlyret_entry.entry(state); }
  static address    remove_activation_entry()                   { return _remove_activation_entry; }
  static address    remove_activation_force_unwind_entry()      { return _remove_activation_force_unwind_entry; }
  static address    continue_activation_entry()                 { return _continue_activation_entry; }
  static address    throw_exception_entry()                     { return _throw_exception_entry; }
  static address    throw_ArrayIndexOutOfBoundsException_entry(){ return _throw_ArrayIndexOutOfBoundsException_entry; }
  static address    throw_ArithmeticException_entry()           { return _throw_ArithmeticException_entry; }
  static address    throw_ClassCastException_entry()            { return _throw_ClassCastException_entry; }
  static address    throw_NullPointerException_entry()          { return _throw_NullPointerException_entry; }
  static address    throw_StackOverflowError_entry()            { return _throw_StackOverflowError_entry; }
  static address    unpack_and_go()                             { return _unpack_and_go; }

  // Code generation
static address trace_code(TosState state){return _trace_code.entry(state);}
  static address*   dispatch_table(TosState state)              { return _active_table.table_for(state); }
  static address*   dispatch_table()                            { return _active_table.table_for(); }
  static address*   normal_table(TosState state)                { return _normal_table.table_for(state); }
  static address*   normal_table()                              { return _normal_table.table_for(); }
static address*active_table(){return _active_table.table_for();}

  // Support for invokes
  static address*   return_3_addrs_by_index_table()             { return _return_3_addrs_by_index; }
  static address*   return_5_addrs_by_index_table()             { return _return_5_addrs_by_index; }
  static int        TosState_as_index(TosState state);          // computes index into return_3_entry_by_index table

  static address     iframe_16_stk_args_entry()                 { return  _iframe_16_stk_args_entry; }
  static address    iframe_256_stk_args_entry()                 { return _iframe_256_stk_args_entry; }
  
  // Runtime support
  static bool       is_not_reached(methodHandle method, int bci);
  // Safepoint support
  static void       notice_safepoints();                        // stops the thread when reaching a safepoint
  static void       ignore_safepoints();                        // ignores safepoints

  // Debugging/printing
  static InterpreterCodelet* codelet_containing(address pc);
  static void print();          // prints the interpreter code
  static void stubs_do(void f(InterpreterCodelet* s));
};


//------------------------------------------------------------------------------------------------------------------------
// The interpreter generator.

class Template;
class TemplateInterpreterGenerator:public StackObj{
 protected:
  InterpreterMacroAssembler* const _masm;

  // entry points for shared code sequence
  address _unimplemented_bytecode;
  address _illegal_bytecode_sequence;

  // shared code sequences
  address generate_error_exit(const char* msg);
  address generate_StackOverflowError_handler();
  address generate_exception_handler(const char* name, const char* message) {
    return generate_exception_handler_common(name, message, false);
  }
  address generate_klass_exception_handler(const char* name) {
    return generate_exception_handler_common(name, NULL, true);
  }
  address generate_exception_handler_common(const char* name, const char* message, bool pass_oop);
  address generate_ClassCastException_handler();
  address generate_ArrayIndexOutOfBounds_handler(const char* name);
  address generate_return_entry_for(TosState state, int step);
  address generate_earlyret_entry_for(TosState state);
  address generate_safept_entry_for(TosState state, address runtime_entry);
address generate_unpack_and_go();
  void    generate_throw_exception();
  address generate_iframe_stk_args(int extra_words, const char *name);

  // entry point generator
address generate_method_entry(TemplateInterpreter::MethodKind kind);
  
  // Instruction generation
  void generate_and_dispatch (Template* t, TosState tos_out = ilgl);
  void set_vtos_entry_points (Template* t, address& bep, address& cep, address& sep, address& aep, address& iep, address& lep, address& fep, address& dep, address& vep);
  void set_short_entry_points(Template* t, address& bep, address& cep, address& sep, address& aep, address& iep, address& lep, address& fep, address& dep, address& vep);
  void set_wide_entry_point  (Template* t, address& wep);

  void set_entry_points(Bytecodes::Code code);
  void set_unimplemented(int i);
  void set_entry_points_for_all_bytes();
  void set_safepoints_for_all_bytes();

  // Helpers for generate_and_dispatch
  address generate_trace_code(TosState state)   PRODUCT_RETURN0;
  void count_bytecode()                         PRODUCT_RETURN;  
void trace_bytecode(Template*t)PRODUCT_RETURN;
  void stop_interpreter_at()                    PRODUCT_RETURN;

  void generate_all();

 public:
  TemplateInterpreterGenerator(InterpreterMacroAssembler *masm);
};

#endif // INTERPRETER_HPP
