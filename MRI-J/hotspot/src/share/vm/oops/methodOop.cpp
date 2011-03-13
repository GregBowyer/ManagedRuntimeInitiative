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



#include "bytecodeStream.hpp"
#include "bytecodeTracer.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "gcLocker.hpp"
#include "handles.hpp"
#include "interpreter_pd.hpp"
#include "interpreterRuntime.hpp"
#include "jvmtiExport.hpp"
#include "markWord.hpp"
#include "methodOop.hpp"
#include "methodCodeKlass.hpp"
#include "methodCodeOop.hpp"
#include "mutexLocker.hpp"
#include "nativeLookup.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "preserveException.hpp"
#include "sharedRuntime.hpp"
#include "signature.hpp"
#include "stubRoutines.hpp"
#include "synchronizer.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"

#include "atomic_os_pd.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

// Implementation of methodOopDesc

char*methodOopDesc::name_as_C_string(){
  return name_as_C_string(Klass::cast(constants()->pool_holder()), name());
}

char*methodOopDesc::name_as_C_string(char*buf,int size){
  return name_as_C_string(Klass::cast(constants()->pool_holder()), name(), buf, size);
}

char* methodOopDesc::name_and_sig_as_C_string() const {
  return name_and_sig_as_C_string(Klass::cast(constants()->pool_holder()), name(), signature());
}

char*methodOopDesc::name_and_sig_as_C_string(char*buf,int size)const{
  return name_and_sig_as_C_string(Klass::cast(constants()->pool_holder()), name(), signature(), buf, size);
}

char* methodOopDesc::name_as_C_string(Klass* klass, symbolOop method_name) {
  const char* klass_name = klass->external_name();
  int klass_name_len  = (int)strlen(klass_name);
  int method_name_len = method_name->utf8_length();
  int len             = klass_name_len + 1 + method_name_len;
  char* dest          = NEW_RESOURCE_ARRAY(char, len + 1);
  strcpy(dest, klass_name);
  dest[klass_name_len] = '.';
  strcpy(&dest[klass_name_len + 1], method_name->as_C_string());
  dest[len] = 0;
  return dest;
}

char* methodOopDesc::name_as_C_string(Klass* klass, symbolOop method_name, char* buf, int size) {
  symbolOop klass_name = klass->name();
  klass_name->as_klass_external_name(buf, size);
  int len = (int)strlen(buf);

  if (len < size - 1) {
    buf[len++] = '.';
method_name->as_C_string(&(buf[len]),size-len);
  }

  return buf;
}

char* methodOopDesc::name_and_sig_as_C_string(Klass* klass, symbolOop method_name, symbolOop signature) {
  const char* klass_name = klass->external_name();
  int klass_name_len  = (int)strlen(klass_name);
  int method_name_len = method_name->utf8_length();
  int len             = klass_name_len + 1 + method_name_len + signature->utf8_length();
  char* dest          = NEW_RESOURCE_ARRAY(char, len + 1);
  strcpy(dest, klass_name);
  dest[klass_name_len] = '.';
  strcpy(&dest[klass_name_len + 1], method_name->as_C_string());
  strcpy(&dest[klass_name_len + 1 + method_name_len], signature->as_C_string());
  dest[len] = 0;
  return dest;
}

char* methodOopDesc::name_and_sig_as_C_string(Klass* klass, symbolOop method_name, symbolOop signature, char* buf, int size) {
  symbolOop klass_name = klass->name();
  klass_name->as_klass_external_name(buf, size);
  int len = (int)strlen(buf);

  if (len < size - 1) {
    buf[len++] = '.';

    method_name->as_C_string(&(buf[len]), size - len);
    len = (int)strlen(buf);

    signature->as_C_string(&(buf[len]), size - len);
  }

  return buf;
}

int  methodOopDesc::fast_exception_handler_bci_for(KlassHandle ex_klass, int throw_bci, TRAPS) {
  // exception table holds quadruple entries of the form (beg_bci, end_bci, handler_bci, klass_index)
  const int beg_bci_offset     = 0;
  const int end_bci_offset     = 1;
  const int handler_bci_offset = 2;
  const int klass_index_offset = 3;
  const int entry_size         = 4;
  // access exception table
typeArrayHandle table(constMethod()->exception_tableRef());
  int length = table->length();
  assert(length % entry_size == 0, "exception table format has changed");
  // iterate through all entries sequentially
constantPoolHandle pool(constantsRef());
  for (int i = 0; i < length; i += entry_size) {
    int beg_bci = table->int_at(i + beg_bci_offset);
    int end_bci = table->int_at(i + end_bci_offset);
    assert(beg_bci <= end_bci, "inconsistent exception table");
    if (beg_bci <= throw_bci && throw_bci < end_bci) {
      // exception handler bci range covers throw_bci => investigate further
      int handler_bci = table->int_at(i + handler_bci_offset);
      int klass_index = table->int_at(i + klass_index_offset);
      if (klass_index == 0) {
        return handler_bci;
      } else if (ex_klass.is_null()) {
        return handler_bci;
      } else {
        // we know the exception class => get the constraint class
        // this may require loading of the constraint class; if verification
        // fails or some other exception occurs, return handler_bci
	klassOop k = pool->klass_at(klass_index, CHECK_(handler_bci));
	KlassHandle klass = KlassHandle(THREAD, k);
        assert(klass.not_null(), "klass not loaded");
        if (ex_klass->is_subtype_of(klass())) {
          return handler_bci;
        }
      }
    }
  }

  return InvocationEntryBci;
}

methodOop methodOopDesc::method_from_bcp(address bcp) {
  debug_only(static int count = 0; count++);
  assert(Universe::heap()->is_in_permanent(bcp), "bcp not in perm_gen");
  // TO DO: this may be unsafe in some configurations
  // For example, PGCGeneration::block_start(bcp) unconditionally returns
  // NULL. To make the missing functionality explicit we might want to signal
  // Unimplemented in the pgc version of block start. But doing that will cause
  // inconvenience to debugging macros which are GC-type neutral and can 
  // tolerate incomplete information.
  HeapWord* p = Universe::heap()->block_start(bcp);
  assert(Universe::heap()->block_is_obj(p), "must be obj");
  assert(oop(p)->is_constMethod(), "not a method");
  return constMethodOop(p)->method();
}


int methodOopDesc::bci_from(address bcp) const {  
assert((is_native()&&bcp==code_base())||contains(bcp),"bcp doesn't belong to this method");
  return bcp - code_base();
}


address methodOopDesc::bcp_from(int bci) const {
  assert((is_native() && bci == 0)  || (!is_native() && 0 <= bci && bci < code_size()), "illegal bci");
  address bcp = code_base() + bci;
assert((is_native()&&bcp==code_base())||contains(bcp),"bcp doesn't belong to this method");
  return bcp;
}


int methodOopDesc::object_size(bool is_native) {
  // If native, then include pointers for native_function and signature_handler
  int extra_bytes = (is_native) ? 2*sizeof(address*) : 0;
  int extra_words = align_size_up(extra_bytes, BytesPerWord) / BytesPerWord;
return header_size()+extra_words;
}


symbolOop methodOopDesc::klass_name() const {
  klassOop k = method_holder();
  assert(k->is_klass(), "must be klass");
  instanceKlass* ik = (instanceKlass*) k->klass_part();
  return ik->name();
}


void methodOopDesc::set_method_id(int index) {
  int kid = instanceKlass::cast(method_holder())->klassId();
  if (((kid & methodId_kid_mask) != kid) || ((index & methodId_index_mask) != index)) {
    _method_id = methodId_invalid_id;
    return;
  }
  _method_id = (kid << methodId_kid_shift) | (index << methodId_index_shift) | (methodId_type << methodId_type_shift);
}


methodOop methodOopDesc::from_method_id(int id) {
  if ((id == methodId_invalid_id) || (((id >> methodId_type_shift) & methodId_type_mask) != methodId_type)) {
    return NULL;
  }
  int kid = (id >> methodId_kid_shift) & methodId_kid_mask;
  int idx = (id >> methodId_index_shift) & methodId_index_mask;
  instanceKlass* ik = instanceKlass::cast(klassOop(KlassTable::getKlassByKlassId(kid).as_oop()));
  return methodOop(ik->methods()->obj_at(idx));
}


void methodOopDesc::set_interpreter_kind() {
  int kind = Interpreter::method_kind(methodOop(this));
  assert(kind != Interpreter::invalid,
         "interpreter entry must be valid");
  set_interpreter_kind(kind);
}


// Attempt to return method oop to original state.  Clear any pointers
// (to objects outside the shared spaces).  We won't be able to predict
// where they should point in a new JVM.  Further initialize some
// entries now in order allow them to be write protected later.

void methodOopDesc::remove_unshareable_info() {
  Unimplemented();
}

methodCodeOop methodOopDesc::lookup_c1() const {
  for( methodCodeOop mco = codeRef_list_head().as_methodCodeOop(); mco; mco = mco->next().as_methodCodeOop() )
    if( mco->_blob->is_c1_method() )
      return mco;
  return NULL;
}

methodCodeRef methodOopDesc::lookup_c1_ref() const {
  for( methodCodeRef mcr = codeRef_list_head(); mcr.not_null(); mcr = mcr.as_methodCodeOop()->next() )
    if( mcr.as_methodCodeOop()->_blob->is_c1_method() )
      return mcr;
  return methodCodeRef((uint64_t)0);
}

methodCodeOop methodOopDesc::lookup_c2() const {
  for( methodCodeOop mco = codeRef_list_head().as_methodCodeOop(); mco; mco = mco->next().as_methodCodeOop() )
    if( mco->_blob->is_c2_method() )
      return mco;
  return NULL;
}

methodCodeOop methodOopDesc::lookup_osr_for(int bci) const { // A C2 methodCodeOop at this BCI or NULL
  for( methodCodeOop mco = codeRef_list_head().as_methodCodeOop(); mco; mco = mco->next().as_methodCodeOop() )
    if( mco->_blob->is_c2_method() &&
        mco->_entry_bci==bci ) {
      Untested();
      return mco;
    }
  return NULL;
}


bool methodOopDesc::was_executed_more_than(int n) const {
  // If the method has a compiled C1 method, then we assume the interpreter
  // invocation counter got reset at the C1 threshold, and that the actual
  // execution count is thus the sum of the CURRENT interpreter invocation
  // count plus the C1 threshold plus any C1 counts.
  const methodCodeOop mco = codeRef().as_methodCodeOop();
  if( mco && mco->_blob->is_c2_method() )
    return mco->get_codeprofile()->invoke_count() > n;

  // If the method has compiled code we therefore assume it has
  // be excuted more than n times.
if(is_accessor()||is_empty_method()||mco)
    // interpreter doesn't bump invocation counter of trivial methods
    // C2 compiler does not bump invocation counter of compiled methods
    return true;

  return invocation_count() > n;
}

#ifndef PRODUCT
void methodOopDesc::print_invocation_count() const {
  Unimplemented();
  //if (is_static()) tty->print("static ");
  //if (is_final()) tty->print("final ");
  //if (is_synchronized()) tty->print("synchronized ");
  //if (is_native()) tty->print("native ");
  //method_holder()->klass_part()->name()->print_symbol_on(tty);
  //tty->print(".");
  //name()->print_symbol_on(tty);
  //signature()->print_symbol_on(tty);

  //if (WizardMode) {
  //  // dump the size of the byte codes
  //  tty->print(" {%d}", code_size());
  //}
  //tty->cr();

  //tty->print_cr ("  invocation_counter:           %8d ", invocation_count());
  //tty->print_cr ("  backedge_counter:             %8d ", backedge_count());
  //if (CountCompiledCalls) {
  //  tty->print_cr ("  compiled_invocation_count: %8d ", compiled_invocation_count());
  //}

}
#endif



void methodOopDesc::cleanup_inline_caches() {
  // The current system doesn't use inline caches in the interpreter
  // => nothing to do (keep this method around for future use)
}


const char* methodOopDesc::localvariable_name(int slot, int bci, bool &out_of_scope) const {
  const char* ret_val = NULL;
  out_of_scope = true;
  
  if (has_localvariable_table()) {
    LocalVariableTableElement* table = localvariable_table_start();
for(int i=0;i<localvariable_table_length();i++){
      const char* cur_name = constants()->printable_name_at(table[i].name_cp_index);
      bool is_long = strcmp("J", (cur_name == NULL) ? "" : cur_name) == 0;
      if ((!is_long && (slot == table[i].slot)) || (is_long && (slot == (table[i].slot+1)))) {
int local_bci=table[i].start_bci;
int local_len=table[i].length;
        if ((local_bci <= bci) && (bci < (local_bci + local_len))) {
          // Found ideal hit.
          out_of_scope = false;
          return cur_name;
        } else {
if(ret_val==NULL){
            ret_val = cur_name;
          } else {
            size_t sr = strlen(ret_val);
            size_t sn = strlen(cur_name);
            char* new_val = NEW_RESOURCE_ARRAY(char, sr + sn + 3);
            sprintf(new_val, "%s, %s", ret_val, cur_name);
            ret_val = new_val;
          }
        }
      }
    }
  }
  return ret_val;
}


void methodOopDesc::compute_size_of_parameters(Thread *thread) {
  symbolHandle h_signature(thread, signature());
  ArgumentSizeComputer asc(h_signature);
  set_size_of_parameters(asc.size() + (is_static() ? 0 : 1));
}


BasicType methodOopDesc::result_type() const {
  ResultTypeFinder rtf(signature());
  return rtf.type();
}


// FAM has a hacked copy of this code in ciMethod::is_empty_method().  Any changes should be made in both functions.
bool methodOopDesc::is_empty_method() const {
  return  code_size() == 1
      && *code_base() == Bytecodes::_return;
}


// FAM has a hacked copy of this code in ciMethod::is_vanilla_constructor().  Any changes should be made in both functions.
bool methodOopDesc::is_vanilla_constructor() const {
  // Returns true if this method is a vanilla constructor, i.e. an "<init>" "()V" method
  // which only calls the superclass vanilla constructor and possibly does stores of
  // zero constants to local fields:
  //
  //   aload_0
  //   invokespecial
  //   indexbyte1
  //   indexbyte2
  // 
  // followed by an (optional) sequence of:
  //
  //   aload_0
  //   aconst_null / iconst_0 / fconst_0 / dconst_0
  //   putfield
  //   indexbyte1
  //   indexbyte2
  //
  // followed by:
  //
  //   return

  assert(name() == vmSymbols::object_initializer_name(),    "Should only be called for default constructors");
  assert(signature() == vmSymbols::void_method_signature(), "Should only be called for default constructors");
  int size = code_size();
  // Check if size match
  if (size == 0 || size % 5 != 0) return false;
  address cb = code_base();
  int last = size - 1;
  if (cb[0] != Bytecodes::_aload_0 || cb[1] != Bytecodes::_invokespecial || cb[last] != Bytecodes::_return) {
    // Does not call superclass default constructor
    return false;
  }
  // Check optional sequence
  for (int i = 4; i < last; i += 5) {
    if (cb[i] != Bytecodes::_aload_0) return false;
    if (!Bytecodes::is_zero_const(Bytecodes::cast(cb[i+1]))) return false;
    if (cb[i+2] != Bytecodes::_putfield) return false;
  }
  return true;
}


bool methodOopDesc::compute_has_loops_flag() {  
  BytecodeStream bcs(methodOop(this));
  Bytecodes::Code bc;
    
  while ((bc = bcs.next()) >= 0) {    
    switch( bc ) {        
      case Bytecodes::_ifeq: 
      case Bytecodes::_ifnull: 
      case Bytecodes::_iflt: 
      case Bytecodes::_ifle: 
      case Bytecodes::_ifne: 
      case Bytecodes::_ifnonnull: 
      case Bytecodes::_ifgt: 
      case Bytecodes::_ifge: 
      case Bytecodes::_if_icmpeq: 
      case Bytecodes::_if_icmpne: 
      case Bytecodes::_if_icmplt: 
      case Bytecodes::_if_icmpgt: 
      case Bytecodes::_if_icmple: 
      case Bytecodes::_if_icmpge: 
      case Bytecodes::_if_acmpeq:
      case Bytecodes::_if_acmpne:
      case Bytecodes::_goto: 
      case Bytecodes::_jsr:     
        if( bcs.dest() < bcs.next_bci() ) _access_flags.set_has_loops();
        break;

      case Bytecodes::_goto_w:       
      case Bytecodes::_jsr_w:        
        if( bcs.dest_w() < bcs.next_bci() ) _access_flags.set_has_loops(); 
        break;
    }  
  }
  _access_flags.set_loops_flag_init();
  return _access_flags.has_loops(); 
}


bool methodOopDesc::is_final_method() const {
  // %%% Should return true for private methods also,
  // since there is no way to override them.
  return is_final() || Klass::cast(method_holder())->is_final();
}


bool methodOopDesc::is_strict_method() const {
  return is_strict();
}


bool methodOopDesc::can_be_statically_bound() const {
  if (is_final_method())  return true;
  return vtable_index() == nonvirtual_vtable_index;
}


bool methodOopDesc::is_accessor() const {
  if (code_size() != 5) return false;
  if (size_of_parameters() != 1) return false;
  if (Bytecodes::java_code_at(code_base()+0) != Bytecodes::_aload_0 ) return false;
  if (Bytecodes::java_code_at(code_base()+1) != Bytecodes::_getfield) return false;
  Bytecodes::Code ret_bc = Bytecodes::java_code_at(code_base()+4);
if(ret_bc!=Bytecodes::_areturn&&
ret_bc!=Bytecodes::_ireturn&&
      ret_bc != Bytecodes::_freturn &&
      ret_bc != Bytecodes::_dreturn &&
      ret_bc != Bytecodes::_lreturn ) return false;
  return true;
}


bool methodOopDesc::is_initializer() const {
  return name() == vmSymbols::object_initializer_name() || name() == vmSymbols::class_initializer_name();
}


objArrayHandle methodOopDesc::resolved_checked_exceptions_impl(methodOop this_oop, intptr_t sba_hint, TRAPS) {
  int length = this_oop->checked_exceptions_length();
  if (length == 0) {  // common case
    return objArrayHandle(THREAD, Universe::the_empty_class_klass_array());
  } else {
    methodHandle h_this(THREAD, this_oop);
objArrayOop m_oop=oopFactory::new_objArray(SystemDictionary::class_klass(),length,sba_hint,CHECK_(objArrayHandle()));
    objArrayHandle mirrors (THREAD, m_oop);
    for (int i = 0; i < length; i++) {
      CheckedExceptionElement* table = h_this->checked_exceptions_start(); // recompute on each iteration, not gc safe
      klassOop k = h_this->constants()->klass_at(table[i].class_cp_index, CHECK_(objArrayHandle()));
      assert(Klass::cast(k)->is_subclass_of(SystemDictionary::throwable_klass()), "invalid exception class");
      mirrors->obj_at_put(i, Klass::cast(k)->java_mirror());
    }
    return mirrors;
  }
};


int methodOopDesc::line_number_from_bci(int bci) const {
if(bci==InvocationEntryBci)bci=0;
  assert(bci == 0 || (0 <= bci && bci < code_size()), "illegal bci");
  int best_bci  =  0;
  int best_line = -1;

  if (has_linenumber_table()) {
    // The line numbers are a short array of 2-tuples [start_pc, line_number].
    // Not necessarily sorted and not necessarily one-to-one.
    CompressedLineNumberReadStream stream(compressed_linenumber_table());
    while (stream.read_pair()) {
      if (stream.bci() == bci) {
        // perfect match
        return stream.line();
      } else {
        // update best_bci/line
        if (stream.bci() < bci && stream.bci() >= best_bci) {
          best_bci  = stream.bci();
          best_line = stream.line();
        }
      }
    }
  }
  return best_line;
}


bool methodOopDesc::is_klass_loaded_by_klass_index(int klass_index) const {
if(constants()->tag_at(klass_index).is_unresolved_klass()){
    Thread *thread = Thread::current();
symbolHandle klass_name(thread,constants()->klass_name_at(klass_index));
    Handle loader(thread, instanceKlass::cast(method_holder())->class_loader());
    Handle prot  (thread, Klass::cast(method_holder())->protection_domain());
    return SystemDictionary::find(klass_name, loader, prot, thread) != NULL;
  } else {
    return true;
  }
}


bool methodOopDesc::is_klass_loaded(int refinfo_index, bool must_be_resolved) const {
int klass_index=constants()->klass_ref_index_at(refinfo_index);
  if (must_be_resolved) {
    // Make sure klass is resolved in constantpool.   
    if (constants()->tag_at(klass_index).is_unresolved_klass()) return false;
  }  
  return is_klass_loaded_by_klass_index(klass_index);
}

// Get C heap compiler structs as needed.
// We have already seen that _bci2cpd_mapping is NULL.
BCI2CPD_mapping *methodOopDesc::bci2cpd_impl() {
  BCI2CPD_mapping *bci2cpd = NEW_C_HEAP_OBJ(BCI2CPD_mapping);
  bci2cpd->init(this);          // Create the bci-to-cpd mapping
  Atomic::write_barrier();      // Force coherent before publish
  // Attempt to install.  We're racing with other compile threads.
  if( NULL == Atomic::cmpxchg_ptr( bci2cpd, &_bci2cpd_mapping, NULL ) ) {
    return bci2cpd;             // Return the requested mapping
  }
  // Install failed.  Reclaim storage and try again - likely somebody
  // else beat us to the punch.
  bci2cpd->free();
  FREE_C_HEAP_ARRAY(BCI2CPD_mapping,bci2cpd);
  return bci2cpd_map();         // Recursively call again to get answer
}

// Find the code profile structure by default order
CodeProfile * methodOopDesc::codeprofile(bool clone) {
  StripedMutexLocker ml(OsrList_locks,this);
  methodCodeOop best = NULL;
  for( methodCodeOop mco = codeRef_list_head().as_methodCodeOop(); mco; mco = mco->next().as_methodCodeOop() ) {
    // Return the code profile hung off the first active c1 MCO if available
    if ( mco->_blob->is_c1_method() ) { 
best=mco;
      break;
    }
    // Saw a c2 before c1. Will return the code profile seen on the last c2
    // if there is no c1 mco.
    if( best == NULL || best->is_osr_method() || !mco->is_osr_method() )
best=mco;
  }
  // If no mco's yet, return the default one if available
  CodeProfile *cp = best ? best->get_codeprofile() : _codeprofile;
  return (cp && clone) ? cp->clone() : cp;
}

// Do a quick check to see whether this method has code profile or not. 
// This is mainly for xmlStream::method(), where it is too late to acquire the
// OsrList_lock due to the ordering issue with tty_lock. All it cares is whether
// there is a code profile or not.
bool methodOopDesc::has_codeprofile(){
  Unimplemented();
  return false;

  //if (_codeprofile || _nm_list) return true;
  //return false;
}

// Query the specified code profile counter
int methodOopDesc::get_codeprofile_count(int kind) {
  StripedMutexLocker ml(OsrList_locks,this);
  CodeProfile *cp = codeprofile(false);
  if (!cp) return 0;
  switch (kind) {
  case CodeProfile::_throwout:
    return cp->throwout_count();
  case CodeProfile::_invoke:
    return cp->invoke_count();
  default:
    Unimplemented();
    return 0;
  }
}

int methodOopDesc::get_codeprofile_size() {
  Unimplemented();
  return 0;

  //StripedMutexLocker ml(OsrList_locks,this);
  //int size = 0;
  //if (_codeprofile)
  //  size += _codeprofile->size();
  //for (nmethod *nm = _nm_list; nm; nm = nm->_link) {
  //  size += nm->get_codeprofile()->size();
  //}
  //return size;
}

// Increment the specified code profile counter
void methodOopDesc::incr_codeprofile_count(int kind) {
  StripedMutexLocker ml(OsrList_locks,this);
  CodeProfile *cp = codeprofile(false);
  // If no code profile created yet, make one now
  if( !cp ) cp = set_codeprofile(CodeProfile::make(this), NULL);
  switch (kind) {
  case CodeProfile::_throwout:
cp->throwout_count_incr();
    return;
  default:
    Unimplemented();
  }
}

CodeProfile *methodOopDesc::set_codeprofile( CodeProfile *cp, CodeProfile *expected ) {
  Atomic::write_barrier();      // Force coherent before publish
  // Attempt to install.  We're racing with other compile threads.
  if( expected == Atomic::cmpxchg_ptr( cp, &_codeprofile, expected ) ) {
    NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_moop_attached, 1);)
  }
  else {
    cp->free(CodeProfile::_race_freed);
  }
  return _codeprofile;
}

// We must be busy unloading this methodOop
void methodOopDesc::free_profiling_structures(){
  Unimplemented();

  //assert0( has_bci2cpd_map() );
  //if( _codeprofile ) {
  //  NOT_PRODUCT(CodeProfile::update_alive_stats(CodeProfile::_moop_attached, -1);)
  //  _codeprofile->free(CodeProfile::_moop_freed);
  //  _codeprofile = NULL;
  //}
  //_bci2cpd_mapping->free();
  //FREE_C_HEAP_ARRAY(BCI2CPD_mapping,_bci2cpd_mapping);
  //_bci2cpd_mapping = NULL;
}



// Takes a lock so it allows GC
void methodOopDesc::set_native_function(address function, bool post_event_flag) {
  assert(function != NULL, "use clear_native_function to unregister natives");

  // We can see racers trying to place the same native function into place. Once
  // is plenty. 
  address* native_function = native_function_addr();
  address current = *native_function;
  if (current == function) return;
  if (post_event_flag && JvmtiExport::should_post_native_method_bind() &&
      function != NULL) {
    // native_method_throw_unsatisfied_link_error_entry() should only
    // be passed when post_event_flag is false.
    assert(function !=
      SharedRuntime::native_method_throw_unsatisfied_link_error_entry(),
      "post_event_flag mis-match");

    // post the bind event, and possible change the bind function
    JvmtiExport::post_native_method_bind(this, &function);
  }
  // This function can be called more than once.  We must make sure that we
  // always use the latest registered method -> check if a stub already has
  // been generated.  If so, we have to change the call target.  Done under a
  // lock so multiple threads racing to set different native addresses will
  // end up with a consistent set of: method->native_function, method->code.
  methodHandle thsi(this); // expensively construct a REF
  ObjectLocker ol(thsi.as_ref());
  // Under lock, check for concurrent attempts to set to the same function.
  // Common when a thread pool slams the same piece of new code.
  if( *thsi->native_function_addr() == function ) return;
  *thsi->native_function_addr() = function; // WRITE
  methodCodeRef mcref = thsi->codeRef(); // Put it into local variable to guard against concurrent updates
if(mcref.not_null()){
    thsi->overwrite_native_function_address_in_wrapper((int64_t)function);
  }  
}


bool methodOopDesc::has_native_function() const {
  address func = native_function();
  return (func != NULL && func != SharedRuntime::native_method_throw_unsatisfied_link_error_entry());
}


void methodOopDesc::clear_native_function() {
  set_native_function(
    SharedRuntime::native_method_throw_unsatisfied_link_error_entry(),
    !native_bind_event_is_interesting);
  clear_codeRef();
  _codeRef_list = nullRef;
}

int methodOopDesc::objectId() {
  if ( _objectId == null_kid ) {
    _objectId = CodeCacheOopTable::putOop(objectRef(this));
  }
  return _objectId;
}

// --- insert_methodCode -----------------------------------------------------
// Linked list of active methodCodes for this method; native wrappers, C1, C2,
// & OSR methodCodes are all here.  Unlike the _codeRef field below, this
// field is not read by racing Java executor threads in generated trampolines.
// Note that this list must exist even in a CORE build, because native methods
// have generated wrappers.
//
// The list could be updated via non-blocking CAS operations, but the locks
// are easier to understand.  If they get hot we'll revisit this.
void methodOopDesc::insert_methodCode( methodCodeRef mcr ) {
  StripedMutexLocker ml(OsrList_locks,this);
  methodCodeOop mco = mcr.as_methodCodeOop();
  mco->set_next(codeRef_list_head()); // Besides setting _next, also LVB the _codeRef_list field
  methodCodeRef *adr = (methodCodeRef*)&_codeRef_list;
  ref_store( this, adr, mcr );
  // non-locking version
  //methodCodeRef cmp_value = *adr;
  //ref_check_without_store(this,adr,mcr);
  //while( Atomic::cmpxchg_ptr(ALWAYS_POISON_OBJECTREF(mcr).raw_value(),(intptr_t*)adr,cmp_value.raw_value()) != (intptr_t)cmp_value.raw_value() ) 
  //  Unimplemented();
}

// --- remove_methodCode -----------------------------------------------------
// Linked list of active methodCodes for this method; native wrappers, C1, C2,
// & OSR methodCodes are all here.  Unlike the _codeRef field below, this
// field is not read by racing Java executor threads in generated trampolines.
// Note that this list must exist even in a CORE build, because native methods
// have generated wrappers.
void methodOopDesc::remove_methodCode( methodCodeRef mcr ) {
  StripedMutexLocker ml(OsrList_locks,this);
  // Scan and find the prior methodCodeRef to this one.
  heapRef *pmcr = adr_codeRef_list();
  heapRef base = this;
  methodCodeRef old = codeRef_list_head();
  while( old != mcr ) {
    // It is possible for the methodCodeRef-removal to be attempted twice.
    // Once if we hit an uncommon-trap in the OSR code (so the code is made
    // not-entrant, but 27 threads might still be in the code so it must
    // remain GC-able), and again when class-loading invalidates the code (so
    // the 27 threads must all deopt and quit running the OSR code).
    if( old.is_null() ) return; // Already removed
base=old;
    pmcr = old.as_methodCodeOop()->adr_next();
    old =  old.as_methodCodeOop()->    next();
  }

  unlink_methodCode(base, pmcr, mcr.as_methodCodeOop());
}

// --- unlink_methodCode -----------------------------------------------------
// Remove this methodCodeRef from the linked list.
// Handle the profile data.
void methodOopDesc::unlink_methodCode( heapRef base, heapRef *prev, methodCodeOop mco ) {
  assert_lock_strong(OsrList_locks.get_lock(this));
  // Remove from the linked list.
  ref_store( base, prev, mco->next() );
  *mco->adr_next() = nullRef;   // set to null for polite GC
  // If we are about to remove the last methodCodeRef, make sure that the moop has a
  // copy of the code profile structure. If not, we may get an assert in the
  // deopt code (find_nested_cpdata) which requires the callee moop to have a
  // cp so that it can record the heroic bits.
  
  // If there is an old codeProfile on the methodOop, we leak it now - we need
  // to keep all the heroic sticky bits in the last methodCodeRef and we're losing
  // the last methodCodeRef.  We have no way of knowing if somebody has loaded the
  // methodOop's codeProfile for reading only.  This leak should be *very*
  // rare.  This needing the last codeProfile is more an issue with very tight
  // heaps where repeated GC cycles get aggressive about flushing methodCodeRefs.
  // Native methodCodeRefs don't have profiles.
  if (prev->is_null() && mco->get_codeprofile()) 
    set_codeprofile(mco->get_codeprofile()->clone(), _codeprofile);
}

// --- set_methodCode
// Unlike the generic insert_methodCode version, this one stops if there is
// code already - we only need one copy of the native wrapper.  TRUE if this
// mcr is installed, FALSE otherwise.  Native wrappers are made racily, so
// launching a bunch of threads which all hit the same native routinely races
// here.
bool methodOopDesc::set_methodCode( methodCodeRef mcr ) {
  ref_check_without_store(this,(objectRef*)&_codeRef_list,mcr);
  if( _codeRef_list.not_null() ) return false;
  return Atomic::cmpxchg_ptr(ALWAYS_POISON_OBJECTREF(mcr).raw_value(),(intptr_t*)&_codeRef_list,0) == 0;
}

// --- make_i2c_adapters_if_needed
// If the method needs i2c adapters, make them.
void methodOopDesc::make_i2c_adapters_if_needed(){
if(_i2c_entry==NULL){
    // Adapters for compiled code are made here.  They are fairly small
    // (generally < 100 bytes) and quick to make (and cached and shared) so
    // making them shouldn't be too expensive.
    AdapterHandlerLibrary::AdapterHandler *ah = AdapterHandlerLibrary::get_create_adapter(this);
    // In theory, 2 threads could get here at the same time, both would get
    // the same adapter (it's cached even) and attempt to set the method
    // fields to the same values.  Since they set them the same, it's OK to
    // race and cheaper than taking a lock.
    _i2c_entry = ah->_i2c;
  }
}

// --- make_c2i_adapters_if_needed
address methodOopDesc::make_c2i_adapters_if_needed(){
  address c2i_entry = _c2i_entry; // READ _c2i_entry
  if( !c2i_entry ) {
    // Adapters for compiled code are made here.  They are fairly small
    // (generally < 100 bytes) and quick to make (and cached and shared) so
    // making them shouldn't be too expensive.
    AdapterHandlerLibrary::AdapterHandler *ah = AdapterHandlerLibrary::get_create_adapter(this);
    // In theory, 2 threads could get here at the same time, both would get
    // the same adapter (it's cached even) and attempt to set the method
    // fields to the same values.  Since they set them the same, it's OK to
    // race and cheaper than taking a lock.
    //
    // One data point with customer corefiles showed that 5% of MethodStubBlobs generated
    // were duplicates...less than 35Kb in a 256Mb CodeCache.  Losing entries could be 
    // eliminated by CASing _c2i_entry with losers returning their copy of the stub to
    // the free list.
    //
    _c2i_entry = c2i_entry = MethodStubBlob::generate(this, ah->_c2i); // WRITE _c2i_entry
  }
  return c2i_entry;
}


// --- from_compiled_entry
// If the method needs c2i adapters, make them.  Used by lazy_c2i adapters
// which in turn is called by the compiled vtable stubs.
address methodOopDesc::from_compiled_entry(){
  address c2i_entry = make_c2i_adapters_if_needed();
  methodCodeRef code = codeRef(); // READ _code
  assert0( code.as_oop()->is_oop_or_null() );
  address from = _from_compiled_entry = // WRITE _from_compiled_entry
    code.not_null() ? code.as_methodCodeOop()->_blob->code_begins() : c2i_entry;
  // Should not be the lazy_c2i_entry anymore.  
  assert0( from && from != StubRoutines::lazy_c2i_entry() );
  return from;
}

// --- make_native_wrapper_if_needed
// If the method is a native, make its wrapper.
// Can call Java code.  Can throw exceptions.  Can GC.
void methodOopDesc::make_native_wrapper_if_needed(TRAPS) {
  if( !is_native() ) return;
  if( codeRef().not_null() )
    return;			// Already has a wrapper

  methodHandle mh(THREAD,this);
  // lookup native function entry point if it doesn't exist.  Lookup does
  // string object allocation, can call Java code and can GC.  It can exit
  // with a pending exception if the lookup failed.
  bool in_base_library;
NativeLookup::lookup(mh,in_base_library,CHECK);

  // Make the native wrapper now.  Wrapper generation can cause GC.
  AdapterHandlerLibrary::create_native_wrapper(mh, CHECK);  
}

// Called when the method_holder is getting linked.
void methodOopDesc::link_method(){
  // Setup interpreter entrypoint
  assert(_i2i_entry == NULL, "should only be called once");
  set_interpreter_entry(Interpreter::entry_for_method(this));
assert(_i2i_entry!=NULL,"interpreter entry must be non-null");
  _from_interpreted_entry = _i2i_entry;
}

// Demote and clear out the codeRef
void methodOopDesc::clear_codeRef(){
  address c2i_entry = make_c2i_adapters_if_needed();
  // Invariant: do only a single read & write to _code
  methodCodeRef code = codeRef();
  if (is_native()) {
code=nullRef;
  } else if (UseC1 && UseC2) {
    if (code.as_methodCodeOop()->_blob->is_c2_method()) { // Attempt demotion
      code = lookup_c1_ref();
      // We need to profile a little while and then kick another C2.
      // Hopefully we already adjusted the invoke counter to avoid endless C1
      // overflow or no C1 overflow.
    } else {
code=nullRef;
    }
  } else {
code=nullRef;
  }
  if (is_native()) {
    set_code_helper(code, StubRoutines::lazy_c2i_entry(), _i2i_entry);
}else if(code.is_null()){
    set_code_helper(code, c2i_entry                     , _i2i_entry);
  } else{
    set_code_helper(code, code.as_methodCodeOop()->_blob->code_begins(), _from_interpreted_entry);
  }
}

// --- set_code_helper
// Set the _codeRef field, which instantly means the code might execute in
// another thread.  Ordering is tricky, hence this subroutine.
void methodOopDesc::set_code_helper(methodCodeRef code, address from_compiled_entry, address from_interpreted_entry) {
  // These writes must happen in this order, because the interpreter will
  // directly jump to from_interpreted_entry which jumps to an i2c adapter
  // which jumps to _from_compiled_entry.
  ref_store(this, &_codeRef,code); // Assign before allowing compiled code to exec // WRITE _codeRef
  Atomic::write_barrier();      // Force visibility ordering
  // As soon as these are set, method->_codeRef can instantly execute.
  _from_compiled_entry = from_compiled_entry; // WRITE _from_compiled_entry
  // Make sure that _from_compiled_entry is published before the 
  // _from_interpreted_entry, otherwise an interpreter thread may see the 
  // updated i2c adapter prematurely and end up in lazy_c2i.
  Atomic::write_barrier();      // Force visibility ordering
_from_interpreted_entry=from_interpreted_entry;
}

// --- set_codeRef
// Set as the active code a selection from the _codeRef_list.  Instantly it
// can execute, so better have any needed I2C adapters ready.
void methodOopDesc::set_codeRef( methodCodeRef mcr ) {
assert(mcr.not_null(),"use clear_code to remove code");
  methodCodeOop mco = mcr.as_methodCodeOop();

  methodCodeOop old_code;
  { No_GC_Verifier nog();
    make_i2c_adapters_if_needed(); // Make I2C adapters if needed

    // A new CodeBlob has been made available.  Do not let a late-arriving C1
    // method stomp over a valid C2 method.  Otherwise install the code (even
    // over prior code).
    old_code = codeRef().as_methodCodeOop(); // READ _codeRef
    if( mco->_blob->is_c1_method() && old_code && old_code->_blob->is_c2_method() )
      return;                   // old code is valid C2 code, new stuff is late C1 stuff
    // Setting code is tricky, because of memory ordering constraints.
    // Do it once in a shared subroutine.
    set_code_helper(mcr, mco->_blob->code_begins(), _i2c_entry);
  } // No_GC_Verifier
}


methodHandle methodOopDesc:: clone_with_new_data(methodHandle m, u_char* new_code, int new_code_length, 
                                                u_char* new_compressed_linenumber_table, int new_compressed_linenumber_size, TRAPS) {
  // Code below does not work for native methods - they should never get rewritten anyway
  assert(!m->is_native(), "cannot rewrite native methods");
  // Allocate new methodOop
  AccessFlags flags = m->access_flags();
  int checked_exceptions_len = m->checked_exceptions_length();
  int localvariable_len = m->localvariable_table_length();
  methodOop newm_oop = oopFactory::new_method(new_code_length, flags, new_compressed_linenumber_size, localvariable_len, checked_exceptions_len, CHECK_(methodHandle()));
  methodHandle newm (THREAD, newm_oop);
  int new_method_size = newm->method_size();
  // Create a shallow copy of methodOopDesc part, but be careful to preserve the new constMethodOop
  constMethodOop newcm = newm->constMethod();
  int new_const_method_size = newm->constMethod()->object_size();
  memcpy(newm(), m(), sizeof(methodOopDesc));
  // Create shallow copy of constMethodOopDesc, but be careful to preserve the methodOop
  memcpy(newcm, m->constMethod(), sizeof(constMethodOopDesc));
  // Reset correct method/const method, method size, and parameter info
  newcm->set_method(newm());
  newm->set_constMethod(newcm);
  assert(newcm->method() == newm(), "check");
  newm->constMethod()->set_code_size(new_code_length);
  newm->constMethod()->set_constMethod_size(new_const_method_size);
  // objectRef's need to be copied with an LVB!
  ref_store_without_check( &(newm->_constants),       lvb_ref(&(m->_constants))       );
  ref_store_without_check( newm->constMethod()->adr_exception_table(), lvb_ref(m->constMethod()->adr_exception_table()) );
  newm->set_method_size(new_method_size);
  assert(newm->code_size() == new_code_length, "check");
  assert(newm->checked_exceptions_length() == checked_exceptions_len, "check");
  assert(newm->localvariable_table_length() == localvariable_len, "check");
  // Copy new byte codes
  memcpy(newm->code_base(), new_code, new_code_length);
  // Copy line number table
  if (new_compressed_linenumber_size > 0) {
    memcpy(newm->compressed_linenumber_table(),
           new_compressed_linenumber_table,
           new_compressed_linenumber_size);
  }
  // Copy checked_exceptions
  if (checked_exceptions_len > 0) {
    memcpy(newm->checked_exceptions_start(),
           m->checked_exceptions_start(),
           checked_exceptions_len * sizeof(CheckedExceptionElement));
  }
  // Copy local variable number table
  if (localvariable_len > 0) {
    memcpy(newm->localvariable_table_start(),
           m->localvariable_table_start(),
           localvariable_len * sizeof(LocalVariableTableElement));
  }
  return newm;
}

vmIntrinsics::ID methodOopDesc::compute_intrinsic_id() const {
  assert(vmIntrinsics::_none == 0, "correct coding of default case");
  const uintptr_t max_cache_uint = right_n_bits((int)(sizeof(_intrinsic_id_cache) * BitsPerByte));
  assert((uintptr_t)vmIntrinsics::ID_LIMIT <= max_cache_uint, "else fix cache size");
  // if loader is not the default loader (i.e., != NULL), we can't know the intrinsics
  // because we are not loading from core libraries
  if (instanceKlass::cast(method_holder())->class_loader() != NULL) return vmIntrinsics::_none;

  // see if the klass name is well-known:
  symbolOop klass_name    = instanceKlass::cast(method_holder())->name();
  vmSymbols::SID klass_id = vmSymbols::find_sid(klass_name);
  if (klass_id == vmSymbols::NO_SID)  return vmIntrinsics::_none;

  // ditto for method and signature:
  vmSymbols::SID  name_id = vmSymbols::find_sid(name());
  if (name_id  == vmSymbols::NO_SID)  return vmIntrinsics::_none;
  vmSymbols::SID   sig_id = vmSymbols::find_sid(signature());
  if (sig_id   == vmSymbols::NO_SID)  return vmIntrinsics::_none;
  jshort flags = access_flags().as_short();

  // A few slightly irregular cases:
  switch (klass_id) {
  case vmSymbols::VM_SYMBOL_ENUM_NAME(java_lang_StrictMath):
    // Second chance: check in regular Math.
    switch (name_id) {
    case vmSymbols::VM_SYMBOL_ENUM_NAME(min_name):
    case vmSymbols::VM_SYMBOL_ENUM_NAME(max_name):
    case vmSymbols::VM_SYMBOL_ENUM_NAME(sqrt_name):
      // pretend it is the corresponding method in the non-strict class:
      klass_id = vmSymbols::VM_SYMBOL_ENUM_NAME(java_lang_Math);
      break;
    }
  }

  // return intrinsic id if any
  return vmIntrinsics::find_id(klass_id, name_id, sig_id, flags);
}


// These two methods are static since a GC may move the methodOopDesc
bool methodOopDesc::load_signature_classes(methodHandle m, TRAPS) {
  bool sig_is_loaded = true;
  Handle class_loader(THREAD, instanceKlass::cast(m->method_holder())->class_loader());
  Handle protection_domain(THREAD, Klass::cast(m->method_holder())->protection_domain());
  symbolHandle signature(THREAD, m->signature());
  for(SignatureStream ss(signature); !ss.is_done(); ss.next()) {
    if (ss.is_object()) {
      symbolOop sym = ss.as_symbol(CHECK_(false));
      symbolHandle name (THREAD, sym);
      klassOop klass = SystemDictionary::resolve_or_null(name, class_loader,
                                             protection_domain, THREAD);
      // We are loading classes eagerly. If a ClassNotFoundException was generated,
      // be sure to ignore it.
      if (HAS_PENDING_EXCEPTION) {
        if (PENDING_EXCEPTION->is_a(SystemDictionary::classNotFoundException_klass())) {
          CLEAR_PENDING_EXCEPTION;
        } else {
          return false;
        }
      }
      if( klass == NULL) { sig_is_loaded = false; }
    }
  }
  return sig_is_loaded;
}

bool methodOopDesc::has_unloaded_classes_in_signature(methodHandle m, TRAPS) {
  Handle class_loader(THREAD, instanceKlass::cast(m->method_holder())->class_loader());
  Handle protection_domain(THREAD, Klass::cast(m->method_holder())->protection_domain());
  symbolHandle signature(THREAD, m->signature());
  for(SignatureStream ss(signature); !ss.is_done(); ss.next()) {
    if (ss.type() == T_OBJECT) {
      symbolHandle name(THREAD, ss.as_symbol_or_null());
      if (name() == NULL) return true;   
      klassOop klass = SystemDictionary::find(name, class_loader, protection_domain, THREAD);
      if (klass == NULL) return true;
    }
  }
  return false;
}


// Exposed so field engineers can debug VM
void methodOopDesc::print_short_name(outputStream*st)const{
  ResourceMark rm;
#ifdef PRODUCT
  st->print(" %s::", method_holder()->klass_part()->external_name());
#else
  st->print(" %s::", method_holder()->klass_part()->internal_name());
#endif
  name()->print_symbol_on(st);
}


extern "C" {
  static int method_compare(methodRef* a, methodRef* b) {
return lvb_methodRef(a).as_methodOop()->name()->fast_compare(lvb_methodRef(b).as_methodOop()->name());
  }

  // Prevent qsort from reordering a previous valid sort by
  // considering the address of the methodOops if two methods
  // would otherwise compare as equal.  Required to preserve
  // optimal access order in the shared archive.  Slower than
  // method_compare, only used for shared archive creation.
  static int method_compare_idempotent(methodRef* a, methodRef* b) {
    int i = method_compare(a, b);
    if (i != 0) return i;
    return ( a < b ? -1 : (a == b ? 0 : 1));
  }

  typedef int (*compareFn)(const void*, const void*);
}


// This is only done during class loading, so it is OK to assume method_idnum matches the methods() array
static void reorder_based_on_method_index(objArrayOop methods,
                                          objArrayOop annotations,
                                          oop* temp_array) {
  if (annotations == NULL) {
    return;
  }

  int length = methods->length();
  int i;
  // Copy to temp array
  memcpy(temp_array, annotations->obj_at_addr(0), length * sizeof(oop));

  // Copy back using old method indices
  for (i = 0; i < length; i++) {
    methodOop m = (methodOop) methods->obj_at(i);
    objectRef ar = lvb_ref((objectRef*)&temp_array[m->method_idnum()]);
    annotations->obj_at_put(i, ar.as_oop());
  }
}

// This is only done during class loading, so it is OK to assume method_idnum matches the methods() array
void methodOopDesc::sort_methods(objArrayOop methods,
                                 objArrayOop methods_annotations,
                                 objArrayOop methods_parameter_annotations,
                                 objArrayOop methods_default_annotations,
                                 bool idempotent) {
  int length = methods->length();
  if (length > 1) {
    // Remember current method ordering so we can reorder annotations
    int i;
    for (i = 0; i < length; i++) {
      methodOop m = (methodOop) methods->obj_at(i);
      m->set_method_idnum(i);
    }

    compareFn compare = (compareFn) (idempotent ? method_compare_idempotent : method_compare);
    qsort(methods->obj_at_addr(0), length, oopSize, compare);
    
    // Sort annotations if necessary
    assert(methods_annotations == NULL           || methods_annotations->length() == methods->length(), "");
    assert(methods_parameter_annotations == NULL || methods_parameter_annotations->length() == methods->length(), "");
    assert(methods_default_annotations == NULL   || methods_default_annotations->length() == methods->length(), "");
    if (methods_annotations != NULL ||
        methods_parameter_annotations != NULL ||
        methods_default_annotations != NULL) {
      // Allocate temporary storage
      oop* temp_array = NEW_RESOURCE_ARRAY(oop, length);
      reorder_based_on_method_index(methods, methods_annotations, temp_array);
      reorder_based_on_method_index(methods, methods_parameter_annotations, temp_array);
      reorder_based_on_method_index(methods, methods_default_annotations, temp_array);
    }

    // Reset method ordering
    for (i = 0; i < length; i++) {
      methodOop m = (methodOop) methods->obj_at(i);
      m->set_method_idnum(i);
    }
  }
}


//-----------------------------------------------------------------------------------
// Non-product code

#ifndef PRODUCT
class SignatureTypePrinter : public SignatureTypeNames {
 private:
  outputStream* _st;
  bool _use_separator;

  void type_name(const char* name) {
    if (_use_separator) _st->print(", ");
    _st->print(name);
    _use_separator = true;
  }

 public:
  SignatureTypePrinter(symbolHandle signature, outputStream* st) : SignatureTypeNames(signature) {
    _st = st;
    _use_separator = false;
  }

  void print_parameters()              { _use_separator = false; iterate_parameters(); }
  void print_returntype()              { _use_separator = false; iterate_returntype(); }
};


void methodOopDesc::print_name(outputStream* st) {
  Thread *thread = Thread::current();
  HandleMark hm;
  ResourceMark rm(thread);
  SignatureTypePrinter sig(signature(), st);
  st->print("%s ", is_static() ? "static" : "virtual");
if(is_final())st->print("final ");
if(is_accessor())st->print("accessor ");
  sig.print_returntype();
  st->print(" %s.", method_holder()->klass_part()->internal_name());
  name()->print_symbol_on(st);
  st->print("(");
  sig.print_parameters();
  st->print(")");
}


void methodOopDesc::print_codes(int from,int to,outputStream*out)const{
  if( !out ) out = tty;
  Thread *thread = Thread::current();
  ResourceMark rm(thread);
  PRESERVE_EXCEPTION_MARK;
  methodHandle mh (thread, (methodOop)this);
  BytecodeStream s(mh);
  s.set_interval(from, to);
  BytecodeTracer::set_closure(BytecodeTracer::std_closure());
while(s.next()>=0)BytecodeTracer::trace(mh,s.bcp(),out);
}
#endif // not PRODUCT


void methodOopDesc::print_codes(outputStream *out, CodeProfile *cp) const {
  if (cp) {
    cp->print(this, out);
  }
  else
    print_codes(0, code_size(), out);
}

// Simple compression of line number tables. We use a regular compressed stream, except that we compress deltas
// between (bci,line) pairs since they are smaller. If (bci delta, line delta) fits in (5-bit unsigned, 3-bit unsigned)
// we save it as one byte, otherwise we write a 0xFF escape character and use regular compression. 0x0 is used
// as end-of-stream terminator.

void CompressedLineNumberWriteStream::write_pair_regular(int bci_delta, int line_delta) {
  // bci and line number does not compress into single byte.
  // Write out escape character and use regular compression for bci and line number.
  write_byte((jubyte)0xFF);
  write_signed_int(bci_delta);
  write_signed_int(line_delta);
}

CompressedLineNumberReadStream::CompressedLineNumberReadStream(u_char* buffer) : CompressedReadStream(buffer) {
  _bci = 0;
  _line = 0;
};


bool CompressedLineNumberReadStream::read_pair() {
  jubyte next = read_byte();
  // Check for terminator
  if (next == 0) return false;
  if (next == 0xFF) {
    // Escape character, regular compression used
    _bci  += read_signed_int();
    _line += read_signed_int();
  } else {
    // Single byte compression used
    _bci  += next >> 3;
    _line += next & 0x7;
  }
  return true;
}


Bytecodes::Code methodOopDesc::orig_bytecode_at(int bci) {
  BreakpointInfo* bp = instanceKlass::cast(method_holder())->breakpoints();
  for (; bp != NULL; bp = bp->next()) {
    if (bp->match(this, bci)) {
      return bp->orig_bytecode();
    }
  }
  ShouldNotReachHere();
  return Bytecodes::_shouldnotreachhere;
}

void methodOopDesc::set_orig_bytecode_at(int bci, Bytecodes::Code code) {
  assert(code != Bytecodes::_breakpoint, "cannot patch breakpoints this way");
  BreakpointInfo* bp = instanceKlass::cast(method_holder())->breakpoints();
  for (; bp != NULL; bp = bp->next()) {
    if (bp->match(this, bci)) {
      bp->set_orig_bytecode(code);
      // and continue, in case there is more than one
    }
  }
}

void methodOopDesc::set_breakpoint(int bci) {
  instanceKlass* ik = instanceKlass::cast(method_holder());
  BreakpointInfo *bp = new BreakpointInfo(this, bci);
  bp->set_next(ik->breakpoints());
  ik->set_breakpoints(bp);
  // do this last:
  bp->set(this);
}

static void clear_matches(methodOop m, int bci) {
  instanceKlass* ik = instanceKlass::cast(m->method_holder());
  BreakpointInfo* prev_bp = NULL;
  BreakpointInfo* next_bp;
  for (BreakpointInfo* bp = ik->breakpoints(); bp != NULL; bp = next_bp) {
    next_bp = bp->next();
    // bci value of InvocationEntryBci is used to delete all breakpoints in method m (ex: clear_all_breakpoint).
    if (bci >= 0 ? bp->match(m, bci) : bp->match(m)) {
      // do this first:
      bp->clear(m);
      // unhook it
      if (prev_bp != NULL)
        prev_bp->set_next(next_bp);
      else
        ik->set_breakpoints(next_bp);
      delete bp;
      // When class is redefined JVMTI sets breakpoint in all versions of EMCP methods
      // at same location. So we have multiple matching (method_index and bci)
      // BreakpointInfo nodes in BreakpointInfo list. We should just delete one
      // breakpoint for clear_breakpoint request and keep all other method versions
      // BreakpointInfo for future clear_breakpoint request.
      // bcivalue of InvocationEntryBci is used to clear all breakpoints (see clear_all_breakpoints)
      // which is being called when class is unloaded. We delete all the Breakpoint
      // information for all versions of method. We may not correctly restore the original
      // bytecode in all method versions, but that is ok. Because the class is being unloaded
      // so these methods won't be used anymore.
      if (bci >= 0) {
        break;
      }
    } else {
      // This one is a keeper.
      prev_bp = bp;
    }
  }
}

void methodOopDesc::clear_breakpoint(int bci) {
  assert(bci >= 0, "");
  clear_matches(this, bci);
}

void methodOopDesc::clear_all_breakpoints() {
  clear_matches(this, -1);
}


BreakpointInfo::BreakpointInfo(methodOop m, int bci) {
  _bci = bci;
  _name_index = m->name_index();
  _signature_index = m->signature_index();

  //_orig_bytecode = (Bytecodes::Code) *m->bcp_from(_bci);
  if (_orig_bytecode == Bytecodes::_breakpoint)
    _orig_bytecode = m->orig_bytecode_at(_bci);
  _next = NULL;
}

void BreakpointInfo::set(methodOop method) {
  //*method->bcp_from(_bci) = Bytecodes::_breakpoint;
  method->incr_number_of_breakpoints();
  SystemDictionary::notice_modification();
  Unimplemented();
  //// Deoptimize all code that has inlined copies of this method
  //if (UseGenPauselessGC) {
  //  instanceKlass::cast(method->method_holder())->GPGC_deoptimize_nmethods_dependent_on_method(method);
  //} else {
  //  instanceKlass::cast(method->method_holder())->deoptimize_nmethods_dependent_on_method(method);
  //}
}

void BreakpointInfo::clear(methodOop method) {
  *method->bcp_from(_bci) = orig_bytecode();
  assert(method->number_of_breakpoints() > 0, "must not go negative");
  method->decr_number_of_breakpoints();
}
