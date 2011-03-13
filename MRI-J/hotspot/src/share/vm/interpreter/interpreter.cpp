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
 

#include "bytecode.hpp"
#include "bytecodeHistogram.hpp"
#include "bytecodeTracer.hpp"
#include "disassembler_pd.hpp"
#include "interp_masm_pd.hpp"
#include "interpreter.hpp"
#include "interpreterRuntime.hpp"
#include "interpreter_pd.hpp"
#include "jvmtiExport.hpp"
#include "templateTable.hpp"
#include "tickProfiler.hpp"
#include "xmlBuffer.hpp"

#include "frame_pd.inline.hpp"

# define __ _masm->


//------------------------------------------------------------------------------------------------------------------------
// Implementation of InterpreterCodelet

InterpreterCodelet*InterpreterCodelet::_list=NULL;
short InterpreterCodelet::_nextid=1;

void InterpreterCodelet::print_on(outputStream*st)const{
  if (PrintInterpreter) {
st->cr();
st->print_cr("----------------------------------------------------------------------");
  }

st->print("%s  ",_description);
  if (_bytecode >= 0 ) st->print("%d %s  ", _bytecode, Bytecodes::name(_bytecode));
st->print_cr("[ "INTPTR_FORMAT" - "INTPTR_FORMAT" ]  %d bytes ",
		(intptr_t)code_begin(), (intptr_t)code_end()-1, code_size());

  if (PrintInterpreter) {
st->cr();
Disassembler::decode(code_begin(),code_end(),st);
  }
}


void InterpreterCodelet::print_xml_on(xmlBuffer *xb, bool ref) const {
  xmlElement xe(xb, ref ? "interpreter_codelet_ref" : "interpreter_codelet");
  xb->name_value_item("id", _id);
  xb->name_value_item("name", _description);
if(Bytecodes::is_defined(_bytecode)){
    xmlElement xe(xb, "bytecode");
    xb->name_value_item("number", _bytecode);
    xb->name_value_item("name", Bytecodes::name(_bytecode));
  }
  if (!ref) {
    ProfileEntry::print_xml_on(xb);
    Disassembler::decode_xml(xb, code_begin(), code_end());
  }
}

void InterpreterCodelet::print_xml_on(xmlBuffer *xb) {
  xmlElement xe(xb, "interpreter_codelet_ref_list");
  for( InterpreterCodelet *codelet = first(); !codelet->done(); codelet = codelet->next()) {
    codelet->print_xml_on(xb, true);
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Implementation of TemplateInterpreter
const CodeBlob *TemplateInterpreter::_blob;

void TemplateInterpreter::initialize(InterpreterMacroAssembler*masm){
  // assertions
  assert((int)Bytecodes::number_of_codes <= (int)DispatchTable::length, 
         "dispatch table too small");

  // make sure 'imported' classes are initialized
  if (CountBytecodes || TraceBytecodes || StopInterpreterAt) BytecodeCounter::reset();

TemplateTable::initialize();

  // generate interpreter
{TraceTime timer("Interpreter generation",TraceStartupTime);
InterpreterGenerator g(masm);
    _blob = masm->blob();
    masm->bake_into_CodeBlob(sizeof(IFrame));
    // GDB Support
    hotspot_to_gdb_symbol_t *hsgdb = &HotspotToGdbSymbolTable.symbolsAddress[_blob->gdb_idx()];
    uint32_t spc = HotspotToGdbSymbolTable.symbolsAddress[_blob->gdb_idx()+1/*the 16-word interpreter frame*/].startPC;
    hsgdb->codeBytes = spc - hsgdb->startPC-1; // Trim size so GDB finds the 16 & 256 word frames
    if (PrintInterpreter) print();
  }

  // initialize dispatch table
  _active_table = _normal_table;
}


void TemplateInterpreter::print(){
  tty->cr();
  tty->print_cr("----------------------------------------------------------------------");
  tty->print_cr("Interpreter");
  tty->cr();
  long sz = code_end() - code_start();
  tty->print_cr("code size        = %6ldK bytes", sz/1024);
  int cnt = 0;
  for( InterpreterCodelet *ic = InterpreterCodelet::first(); !ic->done(); ic = ic->next() ) 
    cnt++;
tty->print_cr("# of codelets    = %6d",cnt);
  tty->print_cr("avg codelet size = %6ld bytes", sz/cnt);
  tty->cr();
  for( InterpreterCodelet *ic = InterpreterCodelet::first(); !ic->done(); ic = ic->next() ) 
ic->print();
  tty->print_cr("----------------------------------------------------------------------");
  tty->cr();
}


//------------------------------------------------------------------------------------------------------------------------
// A CodeletMark serves as an automatic creator/initializer for Codelets

class CodeletMark:StackObj{
 private:
  static InterpreterMacroAssembler *_masm;

 public:
  const InterpreterCodelet *const _ic;
  // Setup for the 1st codelet.
  static void initialize(InterpreterMacroAssembler *masm) { 
_masm=masm;
    // First Codelet starts at the current masm PC.  Masm isn't allowed to
    // relocate the PC - so all codelets must assemble "in place".
    InterpreterCodelet::_list = (InterpreterCodelet*)masm->pc();
  }

  CodeletMark( const char* description, Bytecodes::Code bytecode ) : 
    _ic(_masm->make_codelet(description,bytecode)) {  }  
  
  ~CodeletMark() {
    _masm->align(sizeof(void*));
_masm->patch_branches();
    *(int*)&_ic->_size = (_masm->pc() - (u_char*)_ic);
_masm->reset_branches();
  }
};

InterpreterMacroAssembler *CodeletMark::_masm = NULL;


void interpreter_init() {
  ResourceMark rm;
  InterpreterMacroAssembler masm(CodeBlob::interpreter,"InterpreterBlob");
  CodeletMark::initialize(&masm);
  Interpreter::initialize(&masm);
#ifndef PRODUCT
  if (TraceBytecodes) BytecodeTracer::set_closure(BytecodeTracer::std_closure());
#endif // PRODUCT

  // notify JVMTI profiler
  if (JvmtiExport::should_post_dynamic_code_generated()) {
    JvmtiExport::post_dynamic_code_generated("Interpreter",
					     TemplateInterpreter::code_start(),
					     TemplateInterpreter::code_end());
  }
}


//------------------------------------------------------------------------------------------------------------------------
// Implementation of EntryPoint

EntryPoint::EntryPoint() {
for(int i=0;i<number_of_states;++i){
_entry[i]=NULL;
  }
}


EntryPoint::EntryPoint(address bentry, address centry, address sentry, address aentry, address ientry, address lentry, address fentry, address dentry, address ventry) {
assert(number_of_states==1,"no TosStates supported");
  assert((bentry == centry) && (centry == sentry) && (sentry == aentry) && (aentry == ientry) && (ientry == lentry) && (lentry == fentry) && (fentry == dentry) && (dentry == ventry), "mismatched entry points");
  _entry[0] = bentry;
}


void EntryPoint::set_entry(TosState state, address entry) {
  assert(0 <= state && state < number_of_states, "state out of bounds");
  _entry[state] = entry;
}


address EntryPoint::entry(TosState state) const {
  assert(0 <= state && state < number_of_states, "state out of bounds");
  return _entry[state];
}


void EntryPoint::print() {
  tty->print("[");
  for (int i = 0; i < number_of_states; i++) {
    if (i > 0) tty->print(", ");
    tty->print(INTPTR_FORMAT, (intptr_t)_entry[i]);
  }
tty->print("]");
}


bool EntryPoint::operator == (const EntryPoint& y) {
  int i = number_of_states;
  while (i-- > 0) {
    if (_entry[i] != y._entry[i]) return false;
  }
  return true;
}


//------------------------------------------------------------------------------------------------------------------------
// Implementation of DispatchTable

EntryPoint DispatchTable::entry(int i) const {
  assert(0 <= i && i < length, "index out of bounds");
  return
    EntryPoint(
      _table[btos][i],
      _table[ctos][i],
      _table[stos][i],
      _table[atos][i],
      _table[itos][i],
      _table[ltos][i],
      _table[ftos][i],
      _table[dtos][i],
      _table[vtos][i]
    );
}


void DispatchTable::set_entry(int i, EntryPoint& entry) {
  assert(0 <= i && i < length, "index out of bounds");
  for (int state = 0; state < number_of_states; ++state) {
    _table[state][i] = entry.entry((TosState)state);
  }
}


bool DispatchTable::operator == (DispatchTable& y) {
  int i = length;
  while (i-- > 0) {
    EntryPoint t = y.entry(i); // for compiler compatibility (BugId 4150096)
    if (!(entry(i) == t)) return false;
  }
  return true;
}


//------------------------------------------------------------------------------------------------------------------------
// Implementation of interpreter

address TemplateInterpreter::_remove_activation_entry=NULL;
address TemplateInterpreter::_remove_activation_force_unwind_entry=NULL;
address TemplateInterpreter::_continue_activation_entry=NULL;
address TemplateInterpreter::_remove_activation_preserving_args_entry=NULL;

address TemplateInterpreter::_throw_ArrayIndexOutOfBoundsException_entry=NULL;
address TemplateInterpreter::_throw_ArrayStoreException_entry=NULL;
address TemplateInterpreter::_throw_ArithmeticException_entry=NULL;
address TemplateInterpreter::_throw_ClassCastException_entry=NULL;
address TemplateInterpreter::_throw_NullPointerException_entry=NULL;
address TemplateInterpreter::_throw_StackOverflowError_entry=NULL;
address TemplateInterpreter::_throw_exception_entry=NULL;

EntryPoint TemplateInterpreter::_trace_code;
EntryPoint TemplateInterpreter::_return_entry[TemplateInterpreter::number_of_return_entries];
EntryPoint TemplateInterpreter::_earlyret_entry;
EntryPoint TemplateInterpreter::_safept_entry;
address    TemplateInterpreter::_unpack_and_go;

address    TemplateInterpreter::_return_3_addrs_by_index[TemplateInterpreter::number_of_return_addrs];
address    TemplateInterpreter::_return_5_addrs_by_index[TemplateInterpreter::number_of_return_addrs];

address TemplateInterpreter::_iframe_16_stk_args_entry=NULL;
address TemplateInterpreter::_iframe_256_stk_args_entry=NULL;


DispatchTable TemplateInterpreter::_active_table;
DispatchTable TemplateInterpreter::_normal_table;
DispatchTable TemplateInterpreter::_safept_table;
address    TemplateInterpreter::_wentry_point[DispatchTable::length];

address TemplateInterpreter::_entry_table[TemplateInterpreter::number_of_method_entries];


//------------------------------------------------------------------------------------------------------------------------
// Generation of complete interpreter

TemplateInterpreterGenerator::TemplateInterpreterGenerator(InterpreterMacroAssembler *masm) : _masm(masm) {
_unimplemented_bytecode=NULL;
_illegal_bytecode_sequence=NULL;
}


void TemplateInterpreterGenerator::generate_all(){
  { CodeletMark cm("error exits",Bytecodes::_illegal);
    _unimplemented_bytecode    = generate_error_exit("unimplemented bytecode");
    _illegal_bytecode_sequence = generate_error_exit("illegal bytecode sequence - method not verified");
  }

#ifndef PRODUCT
  if (TraceBytecodes) {
    CodeletMark cm("bytecode tracing support",Bytecodes::_illegal);
    address ep = generate_trace_code(tos);
    Interpreter::_trace_code = EntryPoint(ep, ep, ep, ep, ep, ep, ep, ep, ep);
  }
#endif

  { CodeletMark cm("return entry points",Bytecodes::_illegal);
    for (int i = 0; i < Interpreter::number_of_return_entries; i++) {
      address ep = generate_return_entry_for(tos, i);
      Interpreter::_return_entry[i] = EntryPoint(ep, ep, ep, ep, ep, ep, ep, ep, ep);
    }
  }

  { CodeletMark cm("earlyret entry points",Bytecodes::_illegal);
      address ep = generate_earlyret_entry_for(tos);
      Interpreter::_earlyret_entry = EntryPoint(ep, ep, ep, ep, ep, ep, ep, ep, ep);
  }

  for (int j = 0; j < number_of_states; j++) {
    const TosState states[] = {btos, ctos, stos, itos, ltos, ftos, dtos, atos, vtos};
    Interpreter::_return_3_addrs_by_index[Interpreter::TosState_as_index(states[j])] = Interpreter::return_entry(states[j], 3);
    Interpreter::_return_5_addrs_by_index[Interpreter::TosState_as_index(states[j])] = Interpreter::return_entry(states[j], 5);
  }

  { CodeletMark cm("safepoint entry points",Bytecodes::_illegal);
    address ep = generate_safept_entry_for(tos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint));
    Interpreter::_safept_entry = EntryPoint(ep, ep, ep, ep, ep, ep, ep, ep, ep);
    Interpreter::_unpack_and_go = generate_unpack_and_go();
  }

  { CodeletMark cm("exception handling",Bytecodes::_illegal);
    generate_throw_exception();
  }

  { CodeletMark cm("throw exception entrypoints",Bytecodes::_illegal);
    Interpreter::_throw_ArrayIndexOutOfBoundsException_entry = generate_ArrayIndexOutOfBounds_handler("java/lang/ArrayIndexOutOfBoundsException");
    Interpreter::_throw_ArrayStoreException_entry            = generate_klass_exception_handler("java/lang/ArrayStoreException"                 );
    Interpreter::_throw_ArithmeticException_entry            = generate_exception_handler("java/lang/ArithmeticException"           , "/ by zero");
    Interpreter::_throw_ClassCastException_entry             = generate_ClassCastException_handler();
    Interpreter::_throw_NullPointerException_entry           = generate_exception_handler("java/lang/NullPointerException"          , NULL       );
    Interpreter::_throw_StackOverflowError_entry             = generate_StackOverflowError_handler();
  }
#define method_entry(kind)                                                                    \
  { CodeletMark cm("method entry point (kind = " #kind ")",Bytecodes::_illegal);    \
    Interpreter::_entry_table[Interpreter::kind] = generate_method_entry(Interpreter::kind);  \
  }

  // all non-native method kinds  
  method_entry(zerolocals)
  method_entry(zerolocals_synchronized)
  method_entry(empty)
  method_entry(accessor)
  method_entry(abstract)
  method_entry(java_lang_math_sin  )
  method_entry(java_lang_math_cos  )
  method_entry(java_lang_math_tan  )
  method_entry(java_lang_math_abs  )
  method_entry(java_lang_math_sqrt )
method_entry(java_lang_math_sqrtf)
method_entry(java_lang_math_log)
  method_entry(java_lang_math_log10)

  // all native method kinds
method_entry(native)
method_entry(native_synchronized)

#undef method_entry

  // Bytecodes
  set_entry_points_for_all_bytes();
  set_safepoints_for_all_bytes();

}


//------------------------------------------------------------------------------------------------------------------------

address TemplateInterpreterGenerator::generate_error_exit(const char* msg) {
  ScopedAsm entry(_masm);
  __ stop(msg);
  return entry.abs_pc();
}


//------------------------------------------------------------------------------------------------------------------------

void TemplateInterpreterGenerator::set_entry_points_for_all_bytes() {
  for (int i = 0; i < DispatchTable::length; i++) {
    Bytecodes::Code code = (Bytecodes::Code)i;
    if (Bytecodes::is_defined(code)) {
      set_entry_points(code);
    } else {
      set_unimplemented(i);
    }
  }
}


void TemplateInterpreterGenerator::set_safepoints_for_all_bytes() {
  for (int i = 0; i < DispatchTable::length; i++) {
    Bytecodes::Code code = (Bytecodes::Code)i;
    if (Bytecodes::is_defined(code)) Interpreter::_safept_table.set_entry(code, Interpreter::_safept_entry);
  }
}


void TemplateInterpreterGenerator::set_unimplemented(int i) {
  address e = _unimplemented_bytecode;
  EntryPoint entry(e, e, e, e, e, e, e, e, e);
  Interpreter::_normal_table.set_entry(i, entry);
  Interpreter::_wentry_point[i] = _unimplemented_bytecode;
}


void TemplateInterpreterGenerator::set_entry_points(Bytecodes::Code code) {
  CodeletMark cm(Bytecodes::name(code), code);
  // initialize entry points
  assert(_unimplemented_bytecode    != NULL, "should have been generated before");
  assert(_illegal_bytecode_sequence != NULL, "should have been generated before");
  address bep = _illegal_bytecode_sequence;
  address cep = _illegal_bytecode_sequence;
  address sep = _illegal_bytecode_sequence;
  address aep = _illegal_bytecode_sequence;
  address iep = _illegal_bytecode_sequence;
  address lep = _illegal_bytecode_sequence;
  address fep = _illegal_bytecode_sequence;
  address dep = _illegal_bytecode_sequence;
  address vep = _unimplemented_bytecode;
  address wep = _unimplemented_bytecode;
  // code for short & wide version of bytecode
  if (Bytecodes::is_defined(code)) {
    Template* t = TemplateTable::template_for(code);
    assert(t->is_valid(), "just checking");
    set_short_entry_points(t, bep, cep, sep, aep, iep, lep, fep, dep, vep);
  }
  if (Bytecodes::wide_is_defined(code)) {
    Template* t = TemplateTable::template_for_wide(code);
    assert(t->is_valid(), "just checking");
    set_wide_entry_point(t, wep);
  }
  // set entry points
  EntryPoint entry(bep, cep, sep, aep, iep, lep, fep, dep, vep);
  Interpreter::_normal_table.set_entry(code, entry);
  Interpreter::_wentry_point[code] = wep;
}


void TemplateInterpreterGenerator::set_wide_entry_point(Template* t, address& wep) {
  assert(t->is_valid(), "template must exist");
assert(t->tos_in()==vtos,"only vtos tos_in supported for wide instructions");
__ patch_branches();
__ reset_branches();
  wep = __ pc(); generate_and_dispatch(t);
}


void TemplateInterpreterGenerator::set_short_entry_points(Template* t, address& bep, address& cep, address& sep, address& aep, address& iep, address& lep, address& fep, address& dep, address& vep) {
  assert(t->is_valid(), "template must exist");
vep=__ pc();generate_and_dispatch(t);
  bep = vep; cep = vep; sep = vep; aep = vep; iep = vep; lep = vep; fep = vep; dep = vep;
}


//------------------------------------------------------------------------------------------------------------------------

void TemplateInterpreterGenerator::generate_and_dispatch(Template* t, TosState tos_out) {
#ifndef PRODUCT
  // debugging code
  if (CountBytecodes || TraceBytecodes || StopInterpreterAt > 0) count_bytecode();
  if (StopInterpreterAt > 0)                                     stop_interpreter_at();
#endif // PRODUCT
  if (TraceBytecodes)        trace_bytecode(t);
#ifndef PRODUCT
  __ verify_FPU(1, t->tos_in());
#endif // PRODUCT
  int step;
  if (!t->does_dispatch()) { 
    step = t->is_wide() ? Bytecodes::wide_length_for(t->bytecode()) : Bytecodes::length_for(t->bytecode());
    if (tos_out == ilgl) tos_out = t->tos_out();
    // compute bytecode size
    assert(step > 0, "just checkin'");    
    // setup stuff for dispatching next bytecode 
    __ dispatch_prolog(tos_out, step);
  }
  // generate template
  t->generate(_masm);
  // advance
  if (t->does_dispatch()) {
#ifdef ASSERT
    // make sure execution doesn't go beyond this point if code is broken
__ os_breakpoint();
    __ emit1(0x99);             // some junky thing easy to grep for
#endif // ASSERT
  } else {
    // dispatch to next bytecode
    __ dispatch_epilog(tos_out);
  }
}


//------------------------------------------------------------------------------------------------------------------------
// Entry points

TemplateInterpreter::MethodKind TemplateInterpreter::method_kind(methodHandle m){
  // Abstract method?
  if (m->is_abstract()) return abstract;

  // Native method?
  // Note: This test must come _before_ the test for intrinsic
  //       methods. See also comments below.
  if (m->is_native()) {
    return m->is_synchronized() ? native_synchronized : native;
  } 

  // Synchronized?
  if (m->is_synchronized()) {
    return zerolocals_synchronized;
  } 

  if (RegisterFinalizersAtInit && m->code_size() == 1 &&
      m->intrinsic_id() == vmIntrinsics::_Object_init) {
    // We need to execute the special return bytecode to check for
    // finalizer registration so create a normal frame.
    return zerolocals;
  }

  // Empty method?
  if (m->is_empty_method()) {
    return empty;
  } 
  
  // Accessor method?
  if (m->is_accessor()) {
    assert(m->size_of_parameters() == 1, "fast code for accessors assumes parameter size = 1");
    return accessor;
  }
  
  // Special intrinsic method?
  // Note: This test must come _after_ the test for native methods,
  //       otherwise we will run into problems with JDK 1.2, see also
  //       TemplateInterpreterGenerator::generate_method_entry() for
  //       for details.
  switch (m->intrinsic_id()) {
    case vmIntrinsics::_dsin  : return java_lang_math_sin  ;
    case vmIntrinsics::_dcos  : return java_lang_math_cos  ;
    case vmIntrinsics::_dtan  : return java_lang_math_tan  ;
    case vmIntrinsics::_fsqrt : return java_lang_math_sqrtf;
    case vmIntrinsics::_dabs  : return java_lang_math_abs  ;
    case vmIntrinsics::_dsqrt : return java_lang_math_sqrt ;
    case vmIntrinsics::_dlog  : return java_lang_math_log  ;
    case vmIntrinsics::_dlog10: return java_lang_math_log10;
  }

  // Note: for now: zero locals for all non-empty methods
  return zerolocals;  
}


// Return true if the interpreter can prove that the given bytecode has
// not yet been executed (in Java semantics, not in actual operation).
bool TemplateInterpreter::is_not_reached(methodHandle method,int bci){
  Unimplemented();
  address bcp = method->bcp_from(bci);

  if (!Bytecode_at(bcp)->must_rewrite()) {
    // might have been reached
    return false;
  }

  // the bytecode might not be rewritten if the method is an accessor, etc.
  address ientry = method->interpreter_entry();
if(ientry!=entry_for_kind(TemplateInterpreter::zerolocals)&&
ientry!=entry_for_kind(TemplateInterpreter::zerolocals_synchronized))
    return false;  // interpreter does not run this method!

  // otherwise, we can be sure this bytecode has never been executed
  return true;
}


#ifndef PRODUCT
void TemplateInterpreter::print_method_kind(MethodKind kind){
  switch (kind) {
    case zerolocals             : tty->print("zerolocals"             ); break;
    case zerolocals_synchronized: tty->print("zerolocals_synchronized"); break;
    case native                 : tty->print("native"                 ); break;
    case native_synchronized    : tty->print("native_synchronized"    ); break;
    case empty                  : tty->print("empty"                  ); break;
    case accessor               : tty->print("accessor"               ); break;
    case abstract               : tty->print("abstract"               ); break;
    case java_lang_math_sin     : tty->print("java_lang_math_sin"     ); break;
    case java_lang_math_cos     : tty->print("java_lang_math_cos"     ); break;
    case java_lang_math_tan     : tty->print("java_lang_math_tan"     ); break;
    case java_lang_math_abs     : tty->print("java_lang_math_abs"     ); break;
    case java_lang_math_sqrt    : tty->print("java_lang_math_sqrt"    ); break;
case java_lang_math_sqrtf:tty->print("java_lang_math_sqrtf");break;
    case java_lang_math_log     : tty->print("java_lang_math_log"     ); break;
    case java_lang_math_log10   : tty->print("java_lang_math_log10"   ); break;
    default                     : ShouldNotReachHere();
  }
}
#endif // PRODUCT


address TemplateInterpreter::return_entry(TosState state, int length) {
  guarantee(0 <= length && length < Interpreter::number_of_return_entries, "illegal length");
  return _return_entry[length].entry(state);
}


//------------------------------------------------------------------------------------------------------------------------
// Suport for invokes

int TemplateInterpreter::TosState_as_index(TosState state) {
  assert( state < number_of_states , "Invalid state in TosState_as_index");
  assert(0 <= (int)state && (int)state < TemplateInterpreter::number_of_return_addrs, "index out of bounds");
  return (int)state;
}


//------------------------------------------------------------------------------------------------------------------------
// Safepoint suppport

static inline void copy_table(address* from, address* to, int size) {
  // Copy non-overlapping tables. The copy has to occur word wise for MT safety.
  while (size-- > 0) *to++ = *from++;
}

void TemplateInterpreter::notice_safepoints() {
  if( ((intptr_t*)&_safept_table)[2] != ((intptr_t*)&_active_table)[2] ) {
    // switch to safepoint dispatch table
    copy_table((address*)&_safept_table, (address*)&_active_table, sizeof(_active_table) / sizeof(address));
    assert0( ((intptr_t*)&_safept_table)[2] == ((intptr_t*)&_active_table)[2] );
  }
}


// switch from the dispatch table which notices safepoints back to the
// normal dispatch table.  So that we can notice single stepping points,
// keep the safepoint dispatch table if we are single stepping in JVMTI.
// Note that the should_post_single_step test is exactly as fast as the 
// JvmtiExport::_enabled test and covers both cases.
void TemplateInterpreter::ignore_safepoints() {
  if( ((intptr_t*)&_normal_table)[2] != ((intptr_t*)&_active_table)[2] ) {
    if (!JvmtiExport::should_post_single_step()) {
      // switch to normal dispatch table
      copy_table((address*)&_normal_table, (address*)&_active_table, sizeof(_active_table) / sizeof(address));
      assert0( ((intptr_t*)&_normal_table)[2] == ((intptr_t*)&_active_table)[2] );
    }
  }
}


// --- lookup codelets in the interpreter
InterpreterCodelet* TemplateInterpreter::codelet_containing(address pc) {
  if( !contains(pc) ) return NULL; // fast cutout
  for( InterpreterCodelet *ic = InterpreterCodelet::first(); !ic->done(); ic = ic->next() ) 
    if( ic->code_begin() <= pc && pc < ic->code_end() )
      return ic;
  return NULL;
}

// --- codelet_for_index
InterpreterCodelet* InterpreterCodelet::codelet_for_index(int id) {
  for( InterpreterCodelet *ic = InterpreterCodelet::first(); !ic->done(); ic = ic->next() ) 
    if( ic->_id == id )
      return ic;
  return NULL;
}


// --- codelet_for_name
InterpreterCodelet* InterpreterCodelet::codelet_for_name(const char *name) {
  for( InterpreterCodelet *ic = InterpreterCodelet::first(); !ic->done(); ic = ic->next() ) 
    if( !strcmp(ic->_description,name) )
      return ic;
  return NULL;
}

