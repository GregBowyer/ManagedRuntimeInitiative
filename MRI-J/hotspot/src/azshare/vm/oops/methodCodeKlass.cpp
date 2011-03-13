// Copyright 2010 Azul Systems, Inc.  All Rights Reserved.
// DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
//
// This code is free software; you can redistribute it and/or modify it under 
// the terms of the GNU General Public License version 2 only, as published by 
// the Free Software Foundation. 
//
// This code is distributed in the hope that it will be useful, but WITHOUT ANY 
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
// details (a copy is included in the LICENSE file that accompanied this code).
//
// You should have received a copy of the GNU General Public License version 2 
// along with this work; if not, write to the Free Software Foundation,Inc., 
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
// 
// Please contact Azul Systems, Inc., 1600 Plymouth Street, Mountain View, 
// CA 94043 USA, or visit www.azulsystems.com if you need additional information 
// or have any questions.

 

#include "cardTableExtension.hpp"
#include "collectedHeap.hpp"
#include "commonAsm.hpp"
#include "handles.hpp"
#include "javaClasses.hpp"
#include "methodCodeKlass.hpp"
#include "methodCodeOop.hpp"
#include "methodKlass.hpp"
#include "methodOop.hpp"
#include "oopFactory.hpp"
#include "oopTable.hpp"
#include "ostream.hpp"
#include "markSweep.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "resourceArea.hpp"
#include "universe.hpp"

#include "collectedHeap.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.inline.hpp"
#include "universe.inline.hpp"

methodCodeOop methodCodeKlass::allocate(const CodeBlob *blob, const DebugMap *debuginfo, const PC2PCMap *pc2pcinfo, const CodeProfile *profile, objArrayHandle dep_klasses, objArrayHandle dep_methods, int compile_id, int entry_bci, bool has_unsafe, objArrayHandle srefs, TRAPS) {
int size=methodCodeOopDesc::object_size();
  KlassHandle h_k(THREAD, as_klassOop());
methodCodeOop mcoop=(methodCodeOop)CollectedHeap::permanent_obj_allocate(h_k,size,CHECK_0);
  *(const CodeBlob**)&mcoop->_blob = blob; // jam-in over 'const' like a C++ constructor would
  *(const DebugMap**)&mcoop->_debuginfo = debuginfo; // jam-in over 'const' like a C++ constructor would
  *(const PC2PCMap**)&mcoop->_NPEinfo = pc2pcinfo; // jam-in over 'const' like a C++ constructor would
  *(const CodeProfile**)&mcoop->_profile = profile; // jam-in over 'const' like a C++ constructor would
  *(int*)&mcoop->_compile_id = compile_id; // jam-in over 'const' like a C++ constructor would
  *(int*)&mcoop->_entry_bci = entry_bci; // jam-in over 'const' like a C++ constructor would
  *(bool*)&mcoop->_has_unsafe = has_unsafe; // jam-in over 'const' like a C++ constructor would

  mcoop->set_static_refs(srefs());
  mcoop->set_dep_klasses(dep_klasses());
  mcoop->set_dep_methods(dep_methods());
objArrayOop mco_call_targets=NULL;
  if( debuginfo ) { 
    methodCodeHandle mch(mcoop);
    mco_call_targets = oopFactory::new_objectArray(debuginfo->tablesize(),false, CHECK_0);
    mcoop = mch();              // reload after GC
  }
  mcoop->set_mco_call_targets(mco_call_targets);
  return mcoop;
}


klassOop methodCodeKlass::create_klass(TRAPS){
methodCodeKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());  
  KlassHandle k = base_create_klass(h_this_klass, header_size(), o.vtbl_value(), methodCodeKlass_kid, CHECK_0);
  // Make sure size calculation is right
  assert(k()->size() == header_size(), "wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_0); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), methodCodeKlass_kid);
  return k();
}


int methodCodeKlass::oop_size(oop obj)const{
assert(obj->is_methodCode(),"must be methodCode oop");
return methodCodeOop(obj)->object_size();
}


int methodCodeKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_methodCode()), obj->_klass may not be available.
return methodCodeOop(obj)->object_size();
}


int methodCodeKlass::oop_adjust_pointers(oop obj){
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves.
MarkSweep::adjust_pointer(m->adr_method());
MarkSweep::adjust_pointer(m->adr_next());
MarkSweep::adjust_pointer(m->adr_static_refs());
MarkSweep::adjust_pointer(m->adr_dep_klasses());
MarkSweep::adjust_pointer(m->adr_dep_methods());
MarkSweep::adjust_pointer(m->adr_mco_call_targets());
MarkSweep::adjust_pointer(m->adr_blob_owner());
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() )
MarkSweep::adjust_pointer(vtable->adr_owner());
  return size;
}


bool methodCodeKlass::oop_is_parsable(oop obj)const{
assert(obj->is_methodCode(),"must be methodCode oop");
  return true;
}


void methodCodeKlass::oop_copy_contents(PSPromotionManager*pm,oop obj){
assert(obj->is_methodCode(),"must be methodCode oop");
  assert(!pm->depth_first(), "invariant");
methodCodeOop m=methodCodeOop(obj);

  heapRef* adr;
adr=m->adr_method();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_next();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_static_refs();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_dep_klasses();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_dep_methods();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_mco_call_targets();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
adr=m->adr_blob_owner();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() ) {
    adr = vtable->adr_owner();
    if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_breadth(adr);
  }
}


void methodCodeKlass::oop_push_contents(PSPromotionManager*pm,oop obj){
assert(obj->is_methodCode(),"must be methodCode oop");
  assert(pm->depth_first(), "invariant");
methodCodeOop m=methodCodeOop(obj);

  heapRef* adr;
adr=m->adr_method();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_next();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_static_refs();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_dep_klasses();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_dep_methods();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_mco_call_targets();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
adr=m->adr_blob_owner();
  if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() ) {
    adr = vtable->adr_owner();
    if ( PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*adr))) pm->claim_or_forward_depth(adr);
  }
}


int methodCodeKlass::oop_update_pointers(ParCompactionManager*cm,oop obj){
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
PSParallelCompact::adjust_pointer(m->adr_method());
PSParallelCompact::adjust_pointer(m->adr_next());
PSParallelCompact::adjust_pointer(m->adr_static_refs());
PSParallelCompact::adjust_pointer(m->adr_dep_klasses());
PSParallelCompact::adjust_pointer(m->adr_dep_methods());
PSParallelCompact::adjust_pointer(m->adr_mco_call_targets());
PSParallelCompact::adjust_pointer(m->adr_blob_owner());
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() )
PSParallelCompact::adjust_pointer(vtable->adr_owner());
return m->object_size();
}


int methodCodeKlass::oop_update_pointers(ParCompactionManager*cm,oop obj,
				     HeapWord* beg_addr, HeapWord* end_addr) {
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  PSParallelCompact::adjust_pointer(m->adr_method     (), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_next       (), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_static_refs(), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_dep_klasses(), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_dep_methods(), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_mco_call_targets(), beg_addr, end_addr);
  PSParallelCompact::adjust_pointer(m->adr_blob_owner (), beg_addr, end_addr);
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() )
    PSParallelCompact::adjust_pointer(vtable->adr_owner (), beg_addr, end_addr);
return m->object_size();
}


int methodCodeKlass::oop_oop_iterate(oop obj,OopClosure*blk){
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  // Get size before changing pointers. 
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves
blk->do_oop(m->adr_method());
blk->do_oop(m->adr_next());
blk->do_oop(m->adr_static_refs());
blk->do_oop(m->adr_dep_klasses());
blk->do_oop(m->adr_dep_methods());
blk->do_oop(m->adr_mco_call_targets());
blk->do_oop(m->adr_blob_owner());
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() )
blk->do_oop(vtable->adr_owner());
  return size;
}


int methodCodeKlass::oop_oop_iterate_m(oop obj,OopClosure*blk,MemRegion mr){
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = m->object_size();  
  // Performance tweak: We skip iterating over the klass pointer since we 
  // know that Universe::methodKlassObj never moves.
  objectRef* adr;
  adr = m->adr_method();     if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_next  ();     if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_static_refs();if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_dep_klasses();if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_dep_methods();if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_mco_call_targets();if (mr.contains(adr)) blk->do_oop(adr);
  adr = m->adr_blob_owner(); if (mr.contains(adr)) blk->do_oop(adr);
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() ) {
    adr = vtable->adr_owner(); if (mr.contains(adr)) blk->do_oop(adr);
  }
  return size;
}


void methodCodeKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  if (m->method     ().not_null()) oop_print_xml_on(m->method().as_methodOop(),   xb, ref);
  if (m->next       ().not_null()) oop_print_xml_on(m->next().as_methodCodeOop(), xb, ref);
  if (m->static_refs().not_null()) oop_print_xml_on(m->static_refs().as_oop(),    xb, ref);
  if (m->dep_klasses().not_null()) oop_print_xml_on(m->dep_klasses().as_oop(),    xb, ref);
  if (m->dep_methods().not_null()) oop_print_xml_on(m->dep_methods().as_oop(),    xb, ref);
  if (m->_blob                   ) m->_blob->print_xml_on(xb, ref, 0);
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() )
    vtable->print_xml_on(xb, ref, 0);
}

void methodCodeKlass::oop_print_xml_on_as_object(oop obj, xmlBuffer *xb) {
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  if (m->method     ().not_null()) oop_print_xml_on_as_object(m->method().as_methodOop(),   xb);
  if (m->next       ().not_null()) oop_print_xml_on_as_object(m->next().as_methodCodeOop(), xb);
  if (m->static_refs().not_null()) oop_print_xml_on_as_object(m->static_refs().as_oop(),    xb);
  if (m->dep_klasses().not_null()) oop_print_xml_on_as_object(m->dep_klasses().as_oop(),    xb);
  if (m->dep_methods().not_null()) oop_print_xml_on_as_object(m->dep_methods().as_oop(),    xb);
  if (m->mco_call_targets().not_null()) oop_print_xml_on_as_object(m->mco_call_targets().as_oop(), xb);
}


const char*methodCodeKlass::internal_name()const{
return"{methodCode}";
}


#ifndef PRODUCT

// Printing

void methodCodeKlass::oop_print_on(oop obj,outputStream*st){
  ResourceMark rm;
assert(obj->is_methodCode(),"must be methodCode oop");
  if( !st ) st = tty;
  Klass::oop_print_on(obj, st);
methodCodeOop m=methodCodeOop(obj);
  methodOop moop = m->method().as_methodOop();
  if( moop ) moop->blueprint()->oop_print_on(moop, st);
  methodCodeOop mcop = m->next().as_methodCodeOop();
  if( mcop ) mcop->blueprint()->oop_print_on(mcop, st);
  oop srs = m->static_refs().as_oop();
  if( srs ) srs->blueprint()->oop_print_on(srs, st);
  oop dk = m->dep_klasses().as_oop();
  if( dk ) dk->blueprint()->oop_print_on(dk, st);
  oop dm = m->dep_methods().as_oop();
  if( dm ) dm->blueprint()->oop_print_on(dm, st);
  oop mc = m->mco_call_targets().as_oop();
  if( mc ) mc->blueprint()->oop_print_on(mc, st);
}


void methodCodeKlass::oop_print_value_on(oop obj,outputStream*st){
Klass::oop_print_value_on(obj,st);
assert(obj->is_methodCode(),"must be methodCode oop");
methodCodeOop m=methodCodeOop(obj);
  methodOop moop = m->method().as_methodOop();
  if( moop ) moop->blueprint()->oop_print_value_on(moop, st);
  methodCodeOop mcop = m->next().as_methodCodeOop();
  if( mcop ) mcop->blueprint()->oop_print_value_on(mcop, st);
  oop srs = m->static_refs().as_oop();
  if( srs ) srs->blueprint()->oop_print_value_on(srs, st);
  oop dk = m->dep_klasses().as_oop();
  if( dk ) dk->blueprint()->oop_print_value_on(dk, st);
  oop dm = m->dep_methods().as_oop();
  if( dm ) dm->blueprint()->oop_print_value_on(dm, st);
  oop mc = m->mco_call_targets().as_oop();
  if( mc ) mc->blueprint()->oop_print_value_on(mc, st);
}


// Verification

void methodCodeKlass::oop_verify_on(oop obj,outputStream*st){
  Klass::oop_verify_on(obj, st);
guarantee(obj->is_methodCode(),"object must be methodCode");
methodCodeOop m=methodCodeOop(obj);
  methodOop moop = m->method().as_methodOop();
  if( moop ) moop->blueprint()->oop_verify_on(moop, st);
  methodCodeOop mcop = m->next().as_methodCodeOop();
  if( mcop ) mcop->blueprint()->oop_verify_on(mcop, st);
  oop srs = m->static_refs().as_oop();
  if( srs ) srs->blueprint()->oop_verify_on(srs, st);
  oop dk = m->dep_klasses().as_oop();
  if( dk ) dk->blueprint()->oop_verify_on(dk, st);
  oop dm = m->dep_methods().as_oop();
  if( dm ) dm->blueprint()->oop_verify_on(dm, st);
  oop mc = m->mco_call_targets().as_oop();
  if( mc ) mc->blueprint()->oop_verify_on(mc, st);
  oop ow = m->_blob ? (m->_blob->owner().as_oop()) : NULL;
  assert0( !ow || ow == m );
  for( CodeBlob *vtable = m->first_vtable_blob(); vtable; vtable = vtable->next_vtable_blob() ) {
    oop vt = vtable->owner().as_oop();
    assert0( !vt || vt == m );
  }
}

#endif // PRODUCT
