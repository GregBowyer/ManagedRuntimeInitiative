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


#include "arguments.hpp"
#include "artaObjects.hpp"
#include "cardTableExtension.hpp"
#include "codeProfile.hpp"
#include "collectedHeap.hpp"
#include "constMethodKlass.hpp"
#include "gcLocker.hpp"
#include "javaClasses.hpp"
#include "markSweep.hpp"
#include "methodKlass.hpp"
#include "methodOop.hpp"
#include "mutexLocker.hpp"
#include "oopTable.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "stubRoutines.hpp"
#include "tickProfiler.hpp"
#include "universe.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.inline.hpp"
#include "thread_os.inline.hpp"
#include "universe.inline.hpp"

#include "oop.inline2.hpp"

klassOop methodKlass::create_klass(TRAPS) {
  methodKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());  
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),methodKlass_kid,CHECK_NULL);
  // Make sure size calculation is right
assert(k()->size()==header_size(),"wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), methodKlass_kid);
  return k();
}


int methodKlass::oop_size(oop obj) const {
  assert(obj->is_method(), "must be method oop");
  return methodOop(obj)->object_size();
}


int methodKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_method()), obj->_klass may not be available.
  return methodOop(obj)->object_size();
}


bool methodKlass::oop_is_parsable(oop obj) const {
  assert(obj->is_method(), "must be method oop");
  return methodOop(obj)->object_is_parsable();
}


methodOop methodKlass::allocate(constMethodHandle xconst,
                                AccessFlags access_flags, TRAPS) {
  int size = methodOopDesc::object_size(access_flags.is_native());
  KlassHandle h_k(THREAD, as_klassOop());
  assert(xconst()->is_parsable(), "possible publication protocol violation");
  methodOop m = (methodOop)CollectedHeap::permanent_obj_allocate(h_k, size, CHECK_NULL);
  assert(!m->is_parsable(), "not expecting parsability yet.");

  No_Safepoint_Verifier no_safepoint;  // until m becomes parsable below
  m->set_constMethod(xconst());
  m->set_access_flags(access_flags);
  m->set_method_size(size);
  m->set_name_index(0);
  m->set_signature_index(0);
  m->set_constants(NULL);
  m->set_max_stack(0);
  m->set_max_locals(0);
  m->clear_intrinsic_id_cache();
  m->set_vtable_index(methodOopDesc::garbage_vtable_index);  

  // Fix and bury in methodOop 
  m->set_interpreter_entry(NULL); // sets i2i entry and from_int
m->_i2c_entry=NULL;
m->_c2i_entry=NULL;
  m->_from_compiled_entry = StubRoutines::lazy_c2i_entry(); // WRITE _from_compiled_entry
  m->_from_interpreted_entry = m->_i2i_entry;
  m->_codeRef = nullRef;
  m->_codeRef_list = nullRef;
  m->_objectId = null_kid;
  m->invocation_counter()->init();
  m->backedge_counter()->init();
  m->_bci2cpd_mapping = NULL;   // Not considered for compilation
  m->clear_number_of_breakpoints();
  assert(m->is_parsable(), "must be parsable here.");
  assert(m->size() == size, "wrong size for object");
  // We should not publish an uprasable object's reference
  // into one that is parsable, since that presents problems
  // for the concurrent parallel marking and precleaning phases
  // of concurrent gc (CMS).
  xconst->set_method(m);
  return m;
}


int methodKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert (obj->is_method(), "object must be method");
  methodOop m = methodOop(obj);
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves
  blk->do_oop(m->adr_constMethod());
  blk->do_oop(m->adr_constants());
blk->do_oop(m->adr_codeRef());
blk->do_oop(m->adr_codeRef_list());

  return size;
}

int methodKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert (obj->is_method(), "object must be method");
  methodOop m = methodOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves.
  objectRef* adr;
  adr = m->adr_constMethod();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_constants();
  if (mr.contains(adr)) blk->do_oop(adr);
adr=m->adr_codeRef();
  if (mr.contains(adr)) blk->do_oop(adr);
adr=m->adr_codeRef_list();
  if (mr.contains(adr)) blk->do_oop(adr);

  return size;
}


int methodKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_method(), "should be method");
  methodOop m = methodOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves.
  MarkSweep::adjust_pointer(m->adr_constMethod());
  MarkSweep::adjust_pointer(m->adr_constants());
MarkSweep::adjust_pointer(m->adr_codeRef());
MarkSweep::adjust_pointer(m->adr_codeRef_list());

  return size;
}

void methodKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(!pm->depth_first(), "invariant");
  assert(obj->is_method(), "should be method");
  methodOop m = methodOop(obj);

  heapRef* adr;
  adr = m->adr_constMethod();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
  adr = m->adr_constants();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_codeRef();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_codeRef_list();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
}

void methodKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(pm->depth_first(), "invariant");
  assert(obj->is_method(), "should be method");
  methodOop m = methodOop(obj);

  heapRef* adr;
  adr = m->adr_constMethod();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
  adr = m->adr_constants();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_codeRef();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_codeRef_list();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
}

int methodKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_method(), "should be method");
  methodOop m = methodOop(obj);
  PSParallelCompact::adjust_pointer(m->adr_constMethod());
  PSParallelCompact::adjust_pointer(m->adr_constants());
PSParallelCompact::adjust_pointer(m->adr_codeRef());
PSParallelCompact::adjust_pointer(m->adr_codeRef_list());

  return m->object_size();
}

int methodKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				     HeapWord* beg_addr, HeapWord* end_addr) {
  assert(obj->is_method(), "should be method");

  heapRef* p;
  methodOop m = methodOop(obj);

  p = m->adr_constMethod();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
  p = m->adr_constants();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
p=m->adr_codeRef();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);
p=m->adr_codeRef_list();
  PSParallelCompact::adjust_pointer(p, beg_addr, end_addr);

  return m->object_size();
}

void methodKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  assert0(obj->is_method());
  methodOop m = methodOop(obj);
  if (ref) {
    xmlElement xe(xb, "object_ref");
    int mid = m->method_id();
    if (mid == methodOopDesc::methodId_invalid_id) {
      // If there was an methodId overflow back off to the generic object id mechanism.
      xb->name_value_item("id", xb->object_pool()->append_oop(m));
    } else {
      xb->name_value_item("mid", mid);
    }
    { xmlElement xe(xb, "name", xmlElement::delayed_LF);
instanceKlass*klass=instanceKlass::cast(m->method_holder());
      xb->print("%s.%s", klass->external_name(), m->name()->as_C_string());
    }
    xb->name_value_item("signature", m->signature()->as_C_string());
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}

void methodKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  assert0(obj->is_method());
  methodOop m = methodOop(obj);
  xmlElement xe(xb, "method");
  { xmlElement xe(xb, "holder");
    m->method_holder()->print_xml_on(xb, true);
  }
  { xmlElement xe(xb, "constants");
    m->constants()->print_xml_on(xb, true);
  }
  xb->name_value_item("name", m->name()->as_C_string());
  xb->name_value_item("signature", m->signature()->as_C_string());
xb->name_value_item("max_stack",m->max_stack());
xb->name_value_item("max_locals",m->max_locals());
  if (Arguments::mode() != Arguments::_int  /* ie NOT -Xint */ ) {  
    CodeProfile *cp = m->codeprofile(true);
    if( cp ) {
      cp->print_xml_on(m,xb,true);
      cp->free(CodeProfile::_misc_freed);
    }
  }
  if (m->checked_exceptions_length() > 0) {
    xmlElement xe(xb, "exceptions");
    CheckedExceptionElement* table = m->checked_exceptions_start();
    for (int i = 0; i < m->checked_exceptions_length(); i++) {
xb->name_value_item("throws",m->constants()->printable_name_at(table[i].class_cp_index));
    }
  }
  if (m->has_linenumber_table()) {
    xmlElement xe(xb, "line_numbers");
    u_char* table = m->compressed_linenumber_table();
    CompressedLineNumberReadStream stream(table);
    while (stream.read_pair()) {
      xmlElement xe(xb, "lineno");
      xb->name_value_item("line", stream.line());
      xb->name_value_item("bci", stream.bci());
    }
  }
if(m->has_localvariable_table()){
    xmlElement xe(xb, "local_vars");
    LocalVariableTableElement* table = m->localvariable_table_start();
    for (int i = 0; i < m->localvariable_table_length(); i++) {
      int bci = table[i].start_bci;
      int len = table[i].length;
      const char* name = m->constants()->printable_name_at(table[i].name_cp_index);
      const char* desc = m->constants()->printable_name_at(table[i].descriptor_cp_index);
      int slot = table[i].slot;
      xmlElement xe(xb, "localvar");
      xb->name_value_item("desc", desc);
      xb->name_value_item("name", name);
      xb->name_value_item("bci", bci);
      xb->name_value_item("len", len);
      xb->name_value_item("slot", slot);
    }
  }
  methodCodeOop mcr = m->codeRef().as_methodCodeOop();
  if( mcr ) mcr->print_xml_on(xb, true);
}

#ifndef PRODUCT

// Printing

void methodKlass::oop_print_on(oop obj, outputStream* st) {
  ResourceMark rm;
  assert(obj->is_method(), "must be method");
  Klass::oop_print_on(obj, st);
  methodOop m = methodOop(obj);
  st->print   (" - method holder:     ");    m->method_holder()->print_value_on(st); st->cr();
st->print(" - constants:         "PTR_FORMAT" ",(address)m->constants());
  m->constants()->print_value_on(st); st->cr();
  st->print   (" - access:            0x%x  ", m->access_flags().as_int()); m->access_flags().print_on(st); st->cr();
  st->print   (" - name:              ");    m->name()->print_value_on(st); st->cr();
  st->print   (" - signature:         ");    m->signature()->print_value_on(st); st->cr();
  st->print_cr(" - max stack:         %d",   m->max_stack());
  st->print_cr(" - max locals:        %d",   m->max_locals());
  st->print_cr(" - size of params:    %d",   m->size_of_parameters());
  st->print_cr(" - method size:       %d",   m->method_size());
  st->print_cr(" - vtable index:      %d",   m->_vtable_index);
  st->print_cr(" - code size:         %d",   m->code_size());
st->print_cr(" - code start:        "PTR_FORMAT,m->code_base());
st->print_cr(" - code end (excl):   "PTR_FORMAT,m->code_base()+m->code_size());
  st->print_cr(" - checked ex length: %d",   m->checked_exceptions_length());
  if (m->checked_exceptions_length() > 0) {
    CheckedExceptionElement* table = m->checked_exceptions_start();
st->print_cr(" - checked ex start:  "PTR_FORMAT,table);
    if (Verbose) {
      for (int i = 0; i < m->checked_exceptions_length(); i++) {
        st->print_cr("   - throws %s", m->constants()->printable_name_at(table[i].class_cp_index));
      }
    }
  }
  if (m->has_linenumber_table()) {
    u_char* table = m->compressed_linenumber_table();
st->print_cr(" - linenumber start:  "PTR_FORMAT,table);
    if (Verbose) {
      CompressedLineNumberReadStream stream(table);
      while (stream.read_pair()) {
        st->print_cr("   - line %d: %d", stream.line(), stream.bci());
      }
    }
  }
  st->print_cr(" - localvar length:   %d",   m->localvariable_table_length());
  if (m->localvariable_table_length() > 0) {
    LocalVariableTableElement* table = m->localvariable_table_start();
st->print_cr(" - localvar start:    "PTR_FORMAT,table);
    if (Verbose) {
      for (int i = 0; i < m->localvariable_table_length(); i++) {
        int bci = table[i].start_bci;
        int len = table[i].length;
        const char* name = m->constants()->printable_name_at(table[i].name_cp_index);
        const char* desc = m->constants()->printable_name_at(table[i].descriptor_cp_index);
        int slot = table[i].slot;
        st->print_cr("   - %s %s bci=%d len=%d slot=%d", desc, name, bci, len, slot);
      }
    }
  }
  st->print   (" - compiled code: ");
  methodCodeRef mcr = m->codeRef();
  if( mcr.not_null() ) mcr.as_methodCodeOop()->print_value_on(st);
  st->cr();
}


void methodKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_method(), "must be method");
  Klass::oop_print_value_on(obj, st);
  methodOop m = methodOop(obj);
  st->print(" ");
  m->name()->print_value_on(st);
  st->print(" ");
  m->signature()->print_value_on(st);
  st->print(" in ");
  m->method_holder()->print_value_on(st);
}

#endif // PRODUCT

const char* methodKlass::internal_name() const {
  return "{method}";
}


// Verification

void methodKlass::oop_verify_on(oop obj, outputStream* st) {
  Klass::oop_verify_on(obj, st);
  guarantee(obj->is_method(), "object must be method");
  if (!obj->partially_loaded()) {
    methodOop m = methodOop(obj);
    guarantee(m->is_perm(),  "should be in permspace");
    guarantee(m->name()->is_perm(), "should be in permspace");
    guarantee(m->name()->is_symbol(), "should be symbol");
    guarantee(m->signature()->is_perm(), "should be in permspace");
    guarantee(m->signature()->is_symbol(), "should be symbol");
    guarantee(m->constants()->is_perm(), "should be in permspace");
    guarantee(m->constants()->is_constantPool(), "should be constant pool");
    guarantee(m->constMethod()->is_constMethod(), "should be constMethodOop");
    guarantee(m->constMethod()->is_perm(), "should be in permspace");
  }
}

bool methodKlass::oop_partially_loaded(oop obj) const {
  assert(obj->is_method(), "object must be method");
  methodOop m = methodOop(obj);
  constMethodOop xconst = m->constMethod();
  assert(xconst != NULL, "const method must be set");
  constMethodKlass* ck = constMethodKlass::cast(xconst->klass());
  return ck->oop_partially_loaded(xconst);
}


void methodKlass::oop_set_partially_loaded(oop obj) {
  assert(obj->is_method(), "object must be method");
  methodOop m = methodOop(obj);
  constMethodOop xconst = m->constMethod();
  assert(xconst != NULL, "const method must be set");
  constMethodKlass* ck = constMethodKlass::cast(xconst->klass());
  ck->oop_set_partially_loaded(xconst);
}

