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


#include "cardTableExtension.hpp"
#include "collectedHeap.hpp"
#include "fieldDescriptor.hpp"
#include "gcLocker.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_oldCollector.hpp"
#include "handles.hpp"
#include "instanceKlass.hpp"
#include "instanceKlassKlass.hpp"
#include "instanceRefKlass.hpp"
#include "javaClasses.hpp"
#include "jvmtiExport.hpp"
#include "markSweep.hpp"
#include "oopTable.hpp"
#include "parallelScavengeHeap.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "psScavenge.hpp"
#include "resourceArea.hpp"
#include "systemDictionary.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "bitMap.inline.hpp"
#include "frame.inline.hpp"
#include "gcLocker.inline.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "handles.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markSweep.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "psPromotionManager.inline.hpp"
#include "psScavenge.inline.hpp"
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "oop.inline2.hpp"

void instanceKlassKlass::unused_initial_virtual() { }

klassOop instanceKlassKlass::create_klass(TRAPS) {
  instanceKlassKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());  
KlassHandle k=base_create_klass(h_this_klass,header_size(),o.vtbl_value(),instanceKlass_kid,CHECK_NULL);
  // Make sure size calculation is right
assert(k()->size()==header_size(),"wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  KlassTable::bindReservedKlassId(k(), instanceKlass_kid);
  return k();
}

int instanceKlassKlass::oop_size(oop obj) const {
  assert(obj->is_klass(), "must be klass");
  return instanceKlass::cast(klassOop(obj))->object_size();
}

int instanceKlassKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_klass()), obj->_klass may not be available.
  return instanceKlass::cast(klassOop(obj))->object_size();
}

bool instanceKlassKlass::oop_is_parsable(oop obj) const {
  assert(obj->is_klass(), "must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  return (!ik->null_vtbl()) && ik->object_is_parsable();
}

void instanceKlassKlass::iterate_c_heap_oops(instanceKlass* ik,
					     OopClosure* closure) {
  // jfieldid_cache does not contains any oops

  if (ik->jni_ids() != NULL) {
    ik->jni_ids()->oops_do(closure);
  }
}


void instanceKlassKlass::GPGC_oldgc_oop_update_cardmark(oop obj){
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(), "must be instance klass");

  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
ik->GPGC_static_fields_update_cardmark();
  {
    // Normally I'd want to do this:
    //
    //    ResourceMark rm;
    //    HandleMark hm;
    //    ik->vtable()->GPGC_oop_update_cardmark();
    //    ik->itable()->GPGC_oop_update_cardmark();
    //
    // But some callers have a NoHandleMark, and vtable and itable inherently use handles.

    // VTable checking, in lieu of ik->vtable()->GPGC_oop_update_cardmark():
    {
      vtableEntry* vtable = (vtableEntry*)ik->start_of_vtable();
      long         length = ik->vtable_length() / vtableEntry::size();
      for ( long i=0; i<length; i++ ) {
        assert0( GPGC_Collector::no_card_mark(vtable[i].method_addr()) );
      }
    }

    // ITable checking, in lieu of ik->itable()->GPGC_oop_update_cardmark():
    {
itableOffsetEntry*offset_entry=(itableOffsetEntry*)ik->start_of_itable();
      // Check that itable is initialized:
      if ( (ik->itable_length()>0) && (offset_entry!=NULL) && (!offset_entry->interface_klass_is_null()) ) {
        // First offset entry points to the first method_entry
        intptr_t* method_entry  = (intptr_t *)(((address)obj) + offset_entry->offset());
intptr_t*end=ik->end_of_itable();
    
        long table_offset      = (intptr_t*)offset_entry - (intptr_t*)obj;
        long size_offset_table = (method_entry - ((intptr_t*)offset_entry)) / itableOffsetEntry::size();
        long size_method_table = (end - method_entry)                       / itableMethodEntry::size();

        assert(table_offset >= 0 && size_offset_table >= 0 && size_method_table >= 0, "wrong computation");

        // offset table
        intptr_t*          vtable_start = (intptr_t*)offset_entry;
        itableOffsetEntry* ioe          = (itableOffsetEntry*)vtable_start;
        for( long i=0; i<size_offset_table; i++ ) {
          assert0( GPGC_Collector::no_card_mark(ioe->interface_addr()) );
          ioe++;
        }

        // method table
        intptr_t*          method_start = vtable_start + size_offset_table * itableOffsetEntry::size();
        itableMethodEntry* ime          = (itableMethodEntry*)method_start;
        for( long j=0; j<size_method_table; j++ ) {
          assert0( GPGC_Collector::no_card_mark(ime->method_addr()) );
          ime++;
        }
      } else {
        // This length of the itable was either zero, or it has not yet been initialized.  Either
        // way, we don't check it.
      }
    }
  }

  assert0(GPGC_Collector::no_card_mark(ik->adr_array_klasses()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_methods()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_method_ordering()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_local_interfaces()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_transitive_interfaces()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_fields()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_constants()));
GPGC_OldCollector::update_card_mark(ik->adr_class_loader());
  assert0(GPGC_Collector::no_card_mark(ik->adr_source_file_name()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_source_debug_extension()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_inner_classes()));
GPGC_OldCollector::update_card_mark(ik->adr_protection_domain());
GPGC_OldCollector::update_card_mark(ik->adr_signers());
  assert0(GPGC_Collector::no_card_mark(ik->adr_generic_signature()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_class_annotations()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_fields_annotations()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_methods_annotations()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_methods_parameter_annotations()));
  assert0(GPGC_Collector::no_card_mark(ik->adr_methods_default_annotations()));
  GPGC_OldCollector::update_card_mark (ik->adr_dependent_mco());

  assert0(GPGC_Collector::no_card_mark(ik->adr_implementor()));

klassKlass::GPGC_verify_no_cardmark(obj);

  // ik->jfieldid_cache() is a CHeapObj, and thus doesn't need to be scanned for cardmark updates.
  // ik->jni_ids() is a CHeapObj, and thus doesn't need to be scanned for cardmark updates.
  // ik->jni_id_map() is a CHeapObj, and thus doesn't need to be scanned for cardmark updates.
}

int instanceKlassKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(), "must be instance klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = ik->object_size();

  ik->iterate_static_fields(blk);
  {
    ResourceMark rm;
    HandleMark hm;
    ik->vtable()->oop_oop_iterate(blk);
    ik->itable()->oop_oop_iterate(blk);
  }

  blk->do_oop(ik->adr_array_klasses());
  blk->do_oop(ik->adr_methods());
  blk->do_oop(ik->adr_method_ordering());
  blk->do_oop(ik->adr_local_interfaces());
  blk->do_oop(ik->adr_transitive_interfaces());
  blk->do_oop(ik->adr_fields());
  blk->do_oop(ik->adr_constants());
  blk->do_oop(ik->adr_class_loader());
  blk->do_oop(ik->adr_protection_domain());
  blk->do_oop(ik->adr_signers());
  blk->do_oop(ik->adr_source_file_name());
  blk->do_oop(ik->adr_source_debug_extension());
  blk->do_oop(ik->adr_inner_classes());
if(ik->implementor_not_null()){
    blk->do_oop(&ik->adr_implementor()[0]);
  }    
  blk->do_oop(ik->adr_generic_signature());
  blk->do_oop(ik->adr_class_annotations());
  blk->do_oop(ik->adr_fields_annotations());
  blk->do_oop(ik->adr_methods_annotations());
  blk->do_oop(ik->adr_methods_parameter_annotations());
  blk->do_oop(ik->adr_methods_default_annotations());
blk->do_oop(ik->adr_dependent_mco());

  klassKlass::oop_oop_iterate(obj, blk);

  // jfieldid_cache does not contains any oops
  return size;
}

int instanceKlassKlass::oop_oop_iterate_m(oop obj, OopClosure* blk,
					   MemRegion mr) {
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(), "must be instance klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = ik->object_size();

  ik->iterate_static_fields(blk, mr);
  { 
    ResourceMark rm;
    HandleMark hm;
    ik->vtable()->oop_oop_iterate_m(blk, mr);
    ik->itable()->oop_oop_iterate_m(blk, mr);
  }

  objectRef* adr;
  adr = ik->adr_array_klasses();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_methods();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_method_ordering();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_local_interfaces();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_transitive_interfaces();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_fields();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_constants();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_class_loader();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_protection_domain();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_signers();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_source_file_name();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_source_debug_extension();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_inner_classes();
  if (mr.contains(adr)) blk->do_oop(adr);
adr=&ik->adr_implementor()[0];
if(mr.contains(adr)){
if(ik->implementor().not_null())blk->do_oop(adr);
  }    
  adr = ik->adr_generic_signature();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_class_annotations();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_fields_annotations();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_methods_annotations();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_methods_parameter_annotations();
  if (mr.contains(adr)) blk->do_oop(adr);
  adr = ik->adr_methods_default_annotations();
  if (mr.contains(adr)) blk->do_oop(adr);
adr=ik->adr_dependent_mco();
  if (mr.contains(adr)) blk->do_oop(adr);

  klassKlass::oop_oop_iterate_m(obj, blk, mr);

  // jfieldid_cache does not contains any oops
  return size;
}

int instanceKlassKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(), "must be instance klass");

  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->adjust_static_fields();
  { 
    ResourceMark rm;
    HandleMark hm;
    ik->vtable()->oop_adjust_pointers();
    ik->itable()->oop_adjust_pointers();
  }

  MarkSweep::adjust_pointer(ik->adr_array_klasses());
  MarkSweep::adjust_pointer(ik->adr_methods());
  MarkSweep::adjust_pointer(ik->adr_method_ordering());
  MarkSweep::adjust_pointer(ik->adr_local_interfaces());
  MarkSweep::adjust_pointer(ik->adr_transitive_interfaces());
  MarkSweep::adjust_pointer(ik->adr_fields());
  MarkSweep::adjust_pointer(ik->adr_constants());
  MarkSweep::adjust_pointer(ik->adr_class_loader());
  MarkSweep::adjust_pointer(ik->adr_protection_domain());
  MarkSweep::adjust_pointer(ik->adr_signers());
  MarkSweep::adjust_pointer(ik->adr_source_file_name());
  MarkSweep::adjust_pointer(ik->adr_source_debug_extension());
  MarkSweep::adjust_pointer(ik->adr_inner_classes());
  if (ik->implementor().not_null()) {
    MarkSweep::adjust_pointer(&ik->adr_implementor()[0]);
  }  
  MarkSweep::adjust_pointer(ik->adr_generic_signature());
  MarkSweep::adjust_pointer(ik->adr_class_annotations());
  MarkSweep::adjust_pointer(ik->adr_fields_annotations());
  MarkSweep::adjust_pointer(ik->adr_methods_annotations());
  MarkSweep::adjust_pointer(ik->adr_methods_parameter_annotations());
  MarkSweep::adjust_pointer(ik->adr_methods_default_annotations());
MarkSweep::adjust_pointer(ik->adr_dependent_mco());

iterate_c_heap_oops(ik,&MarkSweep::adjust_pointer_closure);

  return klassKlass::oop_adjust_pointers(obj);
}

void instanceKlassKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(!pm->depth_first(), "invariant");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->copy_static_fields(pm);

  heapRef* loader_addr = (heapRef*)ik->adr_class_loader();
  assert0(objectRef::is_null_or_heap(loader_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*loader_addr))) {
    pm->claim_or_forward_breadth(loader_addr);
  }

  heapRef* pd_addr = (heapRef*)ik->adr_protection_domain();
  assert0(objectRef::is_null_or_heap(pd_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*pd_addr))) {
    pm->claim_or_forward_breadth(pd_addr);
  }

  heapRef* sg_addr = (heapRef*)ik->adr_signers();
  assert0(objectRef::is_null_or_heap(sg_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*sg_addr))) {
    pm->claim_or_forward_breadth(sg_addr);
  }

  heapRef* dmco_addr = (heapRef*)ik->adr_dependent_mco();
  assert0(objectRef::is_null_or_heap(dmco_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*dmco_addr))) {
pm->claim_or_forward_breadth(dmco_addr);
  }

  klassKlass::oop_copy_contents(pm, obj);
}

void instanceKlassKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(pm->depth_first(), "invariant");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->push_static_fields(pm);

  heapRef* loader_addr = (heapRef*)ik->adr_class_loader();
  assert0(objectRef::is_null_or_heap(loader_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*loader_addr))) {
    pm->claim_or_forward_depth(loader_addr);
  }

  heapRef* pd_addr = (heapRef*)ik->adr_protection_domain();
  assert0(objectRef::is_null_or_heap(pd_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*pd_addr))) {
    pm->claim_or_forward_depth(pd_addr);
  }

  heapRef* sg_addr = (heapRef*)ik->adr_signers();
  assert0(objectRef::is_null_or_heap(sg_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*sg_addr))) {
    pm->claim_or_forward_depth(sg_addr);
  }

  heapRef* dmco_addr = (heapRef*)ik->adr_dependent_mco();
  assert0(objectRef::is_null_or_heap(dmco_addr));
  if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*dmco_addr))) {
pm->claim_or_forward_depth(dmco_addr);
  }

klassKlass::oop_push_contents(pm,obj);
}

int instanceKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(),
	 "must be instance klass");

  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->update_static_fields();
  ik->vtable()->oop_update_pointers(cm);
  ik->itable()->oop_update_pointers(cm);

  // Azul - we need to call implementor() to check if it is -1.
  // So unrolling Javasoft's for loop here.
PSParallelCompact::adjust_pointer(ik->adr_array_klasses());
PSParallelCompact::adjust_pointer(ik->adr_methods());
PSParallelCompact::adjust_pointer(ik->adr_method_ordering());
PSParallelCompact::adjust_pointer(ik->adr_local_interfaces());
PSParallelCompact::adjust_pointer(ik->adr_transitive_interfaces());
PSParallelCompact::adjust_pointer(ik->adr_fields());
PSParallelCompact::adjust_pointer(ik->adr_constants());
PSParallelCompact::adjust_pointer(ik->adr_class_loader());
PSParallelCompact::adjust_pointer(ik->adr_protection_domain());
PSParallelCompact::adjust_pointer(ik->adr_signers());
PSParallelCompact::adjust_pointer(ik->adr_source_file_name());
PSParallelCompact::adjust_pointer(ik->adr_source_debug_extension());
PSParallelCompact::adjust_pointer(ik->adr_inner_classes());
  if (ik->implementor().not_null()) {
PSParallelCompact::adjust_pointer(ik->adr_implementor());
  }  
PSParallelCompact::adjust_pointer(ik->adr_generic_signature());
PSParallelCompact::adjust_pointer(ik->adr_class_annotations());
PSParallelCompact::adjust_pointer(ik->adr_fields_annotations());
PSParallelCompact::adjust_pointer(ik->adr_methods_annotations());
PSParallelCompact::adjust_pointer(ik->adr_methods_parameter_annotations());
PSParallelCompact::adjust_pointer(ik->adr_methods_default_annotations());
PSParallelCompact::adjust_pointer(ik->adr_dependent_mco());

  OopClosure* closure = PSParallelCompact::adjust_root_pointer_closure();
  iterate_c_heap_oops(ik, closure);

  klassKlass::oop_update_pointers(cm, obj);
  return ik->object_size();
}

int instanceKlassKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
					    HeapWord* beg_addr,
					    HeapWord* end_addr) {
  assert(obj->is_klass(),"must be a klass");
  assert(klassOop(obj)->klass_part()->oop_is_instance_slow(),
	 "must be instance klass");

  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->update_static_fields(beg_addr, end_addr);
  ik->vtable()->oop_update_pointers(cm, beg_addr, end_addr);
  ik->itable()->oop_update_pointers(cm, beg_addr, end_addr);

  heapRef* const beg_oop = MAX2((heapRef*)beg_addr, ik->oop_block_beg());
  heapRef* const end_oop = MIN2((heapRef*)end_addr, ik->oop_block_end());
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    // Azul - we need to call implementor() to check if it is -1.
    if (cur_oop == ik->adr_implementor() && ik->implementor().is_null()) {
      continue;
    }
    PSParallelCompact::adjust_pointer(cur_oop);
  }

  // The jfieldid_cache, jni_ids and jni_id_map are allocated from the C heap,
  // and so don't lie within any 'Chunk' boundaries.  Update them when the
  // lowest addressed oop in the instanceKlass 'oop_block' is updated.
  if (beg_oop == ik->oop_block_beg()) {
    OopClosure* closure = PSParallelCompact::adjust_root_pointer_closure();
    iterate_c_heap_oops(ik, closure);
  }

  klassKlass::oop_update_pointers(cm, obj, beg_addr, end_addr);
  return ik->object_size();
}

klassOop instanceKlassKlass::allocate_instance_klass(int vtable_len, int itable_len, int static_field_size, 
                                                     int nonstatic_oop_map_size, ReferenceType rt, TRAPS) {

  int size = instanceKlass::object_size(vtable_len + itable_len + static_field_size + nonstatic_oop_map_size);

  // Allocation
  KlassHandle h_this_klass(THREAD, as_klassOop());  
  KlassHandle k;
  if (rt == REF_NONE) {
    // regular klass
    instanceKlass o;
k=base_create_klass(h_this_klass,size,o.vtbl_value(),0,CHECK_NULL);
  } else {
    // reference klass
    instanceRefKlass o;
k=base_create_klass(h_this_klass,size,o.vtbl_value(),0,CHECK_NULL);
  }
  {
    No_Safepoint_Verifier no_safepoint; // until k becomes parsable
    instanceKlass* ik = (instanceKlass*) k()->klass_part();
    assert(!k()->is_parsable(), "not expecting parsability yet.");

    // The sizes of these these three variables are used for determining the
    // size of the instanceKlassOop. It is critical that these are set to the right
    // sizes before the first GC, i.e., when we allocate the mirror.
    ik->set_vtable_length(vtable_len);  
    ik->set_itable_length(itable_len);  
    ik->set_static_field_size(static_field_size);
    ik->set_nonstatic_oop_map_size(nonstatic_oop_map_size);
    assert(k()->size() == size, "wrong size for object");
  
    ik->set_array_klasses(NULL);
    ik->set_methods(NULL);
    ik->set_method_ordering(NULL);
    ik->set_local_interfaces(NULL);
    ik->set_transitive_interfaces(NULL);
    ik->_implementor = nullRef;
    ik->set_fields(NULL);
    ik->set_constants(NULL);
    ik->set_class_loader(NULL);
    ik->set_protection_domain(NULL);
    ik->set_signers(NULL);
    ik->set_source_file_name(NULL);
    ik->set_source_debug_extension(NULL);
    ik->set_inner_classes(NULL);  
    ik->set_static_oop_field_size(0);
    ik->set_nonstatic_field_size(0);  
    ik->_dependent_mcoRef = nullRef;
    ik->set_init_state(instanceKlass::allocated);
    ik->set_init_thread(NULL);
    ik->set_reference_type(rt);
ik->_jfieldid_cache=NULL;
    ik->set_jni_ids(NULL);
    ik->set_breakpoints(NULL);
    ik->set_generic_signature(NULL);
    ik->release_set_methods_jmethod_ids(NULL);
    ik->release_set_methods_cached_itable_indices(NULL);
    ik->set_class_annotations(NULL);
    ik->set_fields_annotations(NULL);
    ik->set_methods_annotations(NULL);
    ik->set_methods_parameter_annotations(NULL);
    ik->set_methods_default_annotations(NULL);
ik->set_dependent_mcos(NULL);
    ik->set_enclosing_method_indices(0, 0);
    ik->set_jvmti_cached_class_field_map(NULL);
    ik->set_initial_method_idnum(0);
    assert(k()->is_parsable(), "should be parsable here.");
  
    // initialize the non-header words to zero
    intptr_t* p = (intptr_t*)k();
    for (int index = instanceKlass::header_size(); index < size; index++) {
p[index]=0;
    }
  
    // To get verify to work - must be set to partial loaded before first GC point.
    k()->set_partially_loaded();

    assert(k()->is_parsable(), "should be parsable here.");
  }

  // GC can happen here
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  return k();
}


static const char* state_names[] = {
"unparsable_by_gc","allocated","loaded","linked","being_initialized","fully_initialized","initialization_error"
};


void instanceKlassKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  assert(obj->is_klass(), "must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("kid", ik->klassId());
    xb->name_value_item("name", ik->external_name());
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}

class PrintStaticFieldClosureXML:public FieldClosure{
private:
xmlBuffer*_stream;
  
public:
  PrintStaticFieldClosureXML(xmlBuffer* xml_stream) : _stream(xml_stream) {}
  
  void do_field_for(fieldDescriptor* fd, oop obj) {
fd->print_xml_on_for(_stream,obj);
  }
};

void instanceKlassKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  assert(obj->is_klass(), "must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  xmlElement xe(xb, "klass");
  xb->name_value_item("name", ik->external_name());
  { xmlElement xe(xb, "mirror", xmlElement::delayed_LF);
    ik->java_mirror()->print_xml_on(xb, true);
  }
  { xmlElement xe(xb, "super", xmlElement::delayed_LF);
    ik->super()->print_xml_on(xb, true);
  }
  xb->name_value_item("state", state_names[ik->_init_state]);
  xb->name_value_item("instance_size", ik->size_helper()*wordSize);
  xb->name_value_item("klass_size", ik->object_size());
  { xmlElement xe(xb,"flags", xmlElement::delayed_LF);
ik->access_flags().print_on(xb);
  }
  { xmlElement xe(xb,"implementors", xmlElement::delayed_LF);
    int nof = ik->nof_implementors();
    if( nof == 0 ) { 
xb->print_raw("<object_ref><name>NONE</name></object_ref>");
    } else if( nof == 1 ) {       // One is often (uninterestingly) self
      klassOop imp = ik->implementor().as_klassOop();
      if( imp == obj ) xb->print_raw("<object_ref><name>SELF</name></object_ref>");
      else imp->print_xml_on(xb,true); // Ahhh, an interesting JIT optimization allowed here
    } else {
xb->print_raw("<object_ref><name>MANY</name></object_ref>");
    }
  }
  
  { xmlElement xe(xb, "static_fields", xmlElement::delayed_LF);
    PrintStaticFieldClosureXML print_static_fields(xb);
    ik->do_local_static_fields(&print_static_fields, obj);
  }
  
  { xmlElement xe(xb, "loader", xmlElement::delayed_LF);
    ik->class_loader()->print_xml_on(xb, true);
  }
  { xmlElement xe(xb, "constants", xmlElement::delayed_LF);
    ik->constants()->print_xml_on(xb, true);
  }
  if (ik->source_file_name() != NULL) {
    xb->name_value_item("source_file", ik->source_file_name()->as_C_string());
  }
  for( Klass* sub = ik->subklass(); sub; sub = sub->next_sibling()) {
if(!sub->is_interface()){
      xmlElement xe(xb, "subklass", xmlElement::delayed_LF);
      sub->as_klassOop()->print_xml_on(xb,true);
    }
  }
  for( Klass* sub = ik->subklass(); sub; sub = sub->next_sibling()) {
if(sub->is_interface()){
      xmlElement xe(xb, "interface", xmlElement::delayed_LF);
      sub->as_klassOop()->print_xml_on(xb,true);
    }
  }

objArrayOop oao=ik->transitive_interfaces();
for(int i=0;i<oao->length();i++){
    xmlElement xe(xb, "transitiveinterfaces", xmlElement::delayed_LF);
    oao->obj_at(i)->print_xml_on(xb, true);
  }
  
  objArrayOop methods = ik->methods();
  for(int i = 0; i < methods->length(); i++) {
    xmlElement xe(xb, "method", xmlElement::delayed_LF);
    methods->obj_at(i)->print_xml_on(xb, true);
  }
}


#ifndef PRODUCT

// Printing

class PrintStaticFieldClosure:public FieldClosure{
 private:
outputStream*_stream;
  bool          _non_static;
  
 public:
  PrintStaticFieldClosure(outputStream* print_stream, bool non_static) : _stream(print_stream), _non_static(non_static) {}
  
  void do_field_for(fieldDescriptor* fd, oop obj) {
_stream->print("   - ");
    if (_non_static) {
fd->print_on(_stream);
    } else {
fd->print_on_for(_stream,obj);
    }
_stream->cr();
  }
};

void instanceKlassKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  klassKlass::oop_print_on(obj, st);

  st->print(" - instance size:     %d", ik->size_helper());                        st->cr();
  st->print(" - klass size:        %d", ik->object_size());                        st->cr();
  st->print(" - access:            "); ik->access_flags().print_on(st);            st->cr();
  st->print(" - state:             "); st->print_cr(state_names[ik->_init_state]);
  st->print(" - name:              "); ik->name()->print_value_on(st);             st->cr();
  st->print(" - super:             "); ik->super()->print_value_on(st);            st->cr();
  st->print(" - sub:               "); 
  Klass* sub = ik->subklass(); 
  int n;
  for (n = 0; sub != NULL; n++, sub = sub->next_sibling()) {
    if (n < MaxSubklassPrintSize) {       
      sub->as_klassOop()->print_value_on(st); 
      st->print("   "); 
    }
  }
if(n>=MaxSubklassPrintSize)st->print("(%ld more klasses...)",n-MaxSubklassPrintSize);
  st->cr();

  st->print_cr(" - nof implementors:  %d", ik->nof_implementors());
  if (ik->raw_implementor() != -1 && ik->raw_implementor() != 0) {
    st->print(" - implementor:       "); ik->implementor().as_klassOop()->print_value_on(st);st->cr();
  }

  st->print(" - arrays:            "); ik->array_klasses()->print_value_on(st);     st->cr();
  st->print(" - methods:           "); ik->methods()->print_value_on(st);           st->cr();
  if (Verbose) {
    objArrayOop methods = ik->methods();
    for(int i = 0; i < methods->length(); i++) {
      tty->print("%d : ", i); methods->obj_at(i)->print_value(); tty->cr();
    }
  }
  st->print(" - method ordering:   "); ik->method_ordering()->print_value_on(st);       st->cr();
  st->print(" - local interfaces:  "); ik->local_interfaces()->print_value_on(st);      st->cr();
  st->print(" - trans. interfaces: "); ik->transitive_interfaces()->print_value_on(st); st->cr();
  st->print(" - constants:         "); ik->constants()->print_value_on(st);         st->cr();
  st->print(" - class loader:      "); ik->class_loader()->print_value_on(st);      st->cr();
  st->print(" - protection domain: "); ik->protection_domain()->print_value_on(st); st->cr();
  st->print(" - signers:           "); ik->signers()->print_value_on(st);           st->cr();
  if (ik->source_file_name() != NULL) {
    st->print(" - source file:       "); 
    ik->source_file_name()->print_value_on(st);
    st->cr();
  }
  if (ik->source_debug_extension() != NULL) {
    st->print(" - source debug extension:       "); 
    ik->source_debug_extension()->print_value_on(st);
    st->cr();
  }

  st->print_cr(" - previous version:       "); 
  { 
    ResourceMark rm;
    // PreviousVersionInfo objects returned via PreviousVersionWalker
    // contain a GrowableArray of handles. We have to clean up the
    // GrowableArray _after_ the PreviousVersionWalker destructor
    // has destroyed the handles.
    {
      PreviousVersionWalker pvw(ik);
      for (PreviousVersionInfo * pv_info = pvw.next_previous_version();
           pv_info != NULL; pv_info = pvw.next_previous_version()) {
        pv_info->prev_constant_pool_handle()()->print_value_on(st);
      }
      st->cr();
    } // pvw is cleaned up
  } // rm is cleaned up

  if (ik->generic_signature() != NULL) {
    st->print(" - generic signature:            "); 
    ik->generic_signature()->print_value_on(st);
  }
  st->print(" - inner classes:     "); ik->inner_classes()->print_value_on(st);     st->cr();
  st->print(" - java mirror:       "); ik->java_mirror()->print_value_on(st);       st->cr();
st->print(" - vtable length      %d  (start addr: "PTR_FORMAT")",ik->vtable_length(),ik->start_of_vtable());st->cr();
st->print(" - itable length      %d (start addr: "PTR_FORMAT")",ik->itable_length(),ik->start_of_itable());st->cr();
  st->print_cr(" - static fields:");
  PrintStaticFieldClosure print_static_fields(st, false);
  ik->do_local_static_fields(&print_static_fields, obj);
  st->print_cr(" - non-static fields:");
  PrintStaticFieldClosure print_nonstatic_fields(st, true);
  ik->do_nonstatic_fields(&print_nonstatic_fields, NULL);

  st->print(" - static oop maps:     ");
  if (ik->static_oop_field_size() > 0) {
    int first_offset = ik->offset_of_static_fields();
    st->print("%d-%d", first_offset, first_offset + ik->static_oop_field_size() - 1);
  }
  st->cr();

  st->print(" - non-static oop maps: ");
  OopMapBlock* map     = ik->start_of_nonstatic_oop_maps();
  OopMapBlock* end_map = map + ik->nonstatic_oop_map_size();
  while (map < end_map) {
    st->print("%d-%d ", map->offset(), map->offset() + oopSize*(map->length() - 1));
    map++;
  }
  st->cr();
}


void instanceKlassKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_klass(), "must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  ik->name()->print_value_on(st);
}

#endif // PRODUCT

const char* instanceKlassKlass::internal_name() const {
  return "{instance class}";
}

// Verification


class VerifyFieldClosure: public OopClosure {
 public:
  void do_oop(objectRef* p) {
    guarantee(Universe::heap()->is_in(p), "should be in heap");
    objectRef pref = UNPOISON_OBJECTREF(*p,p);
    guarantee(pref.as_oop()->is_oop_or_null(), "should be in heap");
  }
};


void instanceKlassKlass::oop_verify_on(oop obj, outputStream* st) {
  klassKlass::oop_verify_on(obj, st);
  if (!obj->partially_loaded()) {
    Thread *thread = Thread::current();
    ResourceMark rm(thread);
    HandleMark hm(thread);
    instanceKlass* ik = instanceKlass::cast(klassOop(obj));   

#ifndef PRODUCT
    // Avoid redundant verifies
    if (ik->_verify_count == Universe::verify_count()) return;
    ik->_verify_count = Universe::verify_count();
#endif
    // Verify that klass is present in SystemDictionary
    if (ik->is_loaded()) {
      symbolHandle h_name (thread, ik->name());
      Handle h_loader (thread, ik->class_loader());
      Handle h_obj(thread, obj);
      SystemDictionary::verify_obj_klass_present(h_obj, h_name, h_loader);
    }
    
    // Verify static fields
    VerifyFieldClosure blk;
    ik->iterate_static_fields(&blk);

    // Verify vtables
    if (ik->is_linked()) {
      ResourceMark rm(thread);  
      // $$$ This used to be done only for m/s collections.  Doing it
      // always seemed a valid generalization.  (DLD -- 6/00)
      ik->vtable()->verify(st);
    }
  
    // Verify first subklass
    if (ik->subklass_oop() != NULL) { 
      guarantee(ik->subklass_oop()->is_perm(),  "should be in permspace");
      guarantee(ik->subklass_oop()->is_klass(), "should be klass");
    }

    // Verify siblings
    klassOop super = ik->super();
    Klass* sib = ik->next_sibling();
    int sib_count = 0;
    while (sib != NULL) {
      if (sib == ik) {
        fatal1("subclass cycle of length %d", sib_count);
      }
      if (sib_count >= 100000) {
        fatal1("suspiciously long subclass list %d", sib_count);
      }
      guarantee(sib->as_klassOop()->is_klass(), "should be klass");
      guarantee(sib->as_klassOop()->is_perm(),  "should be in permspace");
      guarantee(sib->super() == super, "siblings should have same superklass");
      sib = sib->next_sibling();
    }

    // Verify implementor fields
    if (ik->implementor().not_null()) {
      guarantee(ik->raw_implementor() != -1 && ik->raw_implementor() != 0, "should only have one implementor");
      klassOop im = ik->implementor().as_klassOop();
      guarantee(im->is_perm(),  "should be in permspace");
      guarantee(im->is_klass(), "should be klass");
guarantee(!Klass::cast(im)->is_interface(),"implementors cannot be interfaces");
    }
    
    // Verify local interfaces
    objArrayOop local_interfaces = ik->local_interfaces();
    guarantee(local_interfaces->is_perm(),          "should be in permspace");
    guarantee(local_interfaces->is_objArray(),      "should be obj array");
    int j;
    for (j = 0; j < local_interfaces->length(); j++) {
      oop e = local_interfaces->obj_at(j);
      guarantee(e->is_klass() && Klass::cast(klassOop(e))->is_interface(), "invalid local interface");
    }

    // Verify transitive interfaces
    objArrayOop transitive_interfaces = ik->transitive_interfaces();
    guarantee(transitive_interfaces->is_perm(),          "should be in permspace");
    guarantee(transitive_interfaces->is_objArray(),      "should be obj array");
    for (j = 0; j < transitive_interfaces->length(); j++) {
      oop e = transitive_interfaces->obj_at(j);
      guarantee(e->is_klass() && Klass::cast(klassOop(e))->is_interface(), "invalid transitive interface");
    }

    // Verify methods
    objArrayOop methods = ik->methods();
    guarantee(methods->is_perm(),              "should be in permspace");
    guarantee(methods->is_objArray(),          "should be obj array");
    for (j = 0; j < methods->length(); j++) {
      guarantee(methods->obj_at(j)->is_method(), "non-method in methods array");
    }
    for (j = 0; j < methods->length() - 1; j++) {
      methodOop m1 = methodOop(methods->obj_at(j));
      methodOop m2 = methodOop(methods->obj_at(j + 1));
      guarantee(m1->name()->fast_compare(m2->name()) <= 0, "methods not sorted correctly");
    }
    
    // Verify method ordering
    typeArrayOop method_ordering = ik->method_ordering();
    guarantee(method_ordering->is_perm(),              "should be in permspace");
    guarantee(method_ordering->is_typeArray(),         "should be type array");
    int length = method_ordering->length();
    if (JvmtiExport::can_maintain_original_method_order()) {
      guarantee(length == methods->length(),           "invalid method ordering length");
      jlong sum = 0;
      for (j = 0; j < length; j++) {
        int original_index = method_ordering->int_at(j);
        guarantee(original_index >= 0 && original_index < length, "invalid method ordering index");
        sum += original_index;
      }
      // Verify sum of indices 0,1,...,length-1
      guarantee(sum == ((jlong)length*(length-1))/2,   "invalid method ordering sum");
    } else {
      guarantee(length == 0,                           "invalid method ordering length");
    }

    // Verify JNI static field identifiers
    if (ik->jni_ids() != NULL) {
      ik->jni_ids()->verify(ik->as_klassOop());
    }

    // Verify other fields
    if (ik->array_klasses() != NULL) {
      guarantee(ik->array_klasses()->is_perm(),      "should be in permspace");
      guarantee(ik->array_klasses()->is_klass(),     "should be klass");
    }
    guarantee(ik->fields()->is_perm(),               "should be in permspace");
    guarantee(ik->fields()->is_typeArray(),          "should be type array");
    guarantee(ik->constants()->is_perm(),            "should be in permspace");
    guarantee(ik->constants()->is_constantPool(),    "should be constant pool");
    guarantee(ik->inner_classes()->is_perm(),        "should be in permspace");
    guarantee(ik->inner_classes()->is_typeArray(),   "should be type array");
    if (ik->source_file_name() != NULL) {
      guarantee(ik->source_file_name()->is_perm(),   "should be in permspace");
      guarantee(ik->source_file_name()->is_symbol(), "should be symbol");
    }
    if (ik->source_debug_extension() != NULL) {
      guarantee(ik->source_debug_extension()->is_perm(),   "should be in permspace");
      guarantee(ik->source_debug_extension()->is_symbol(), "should be symbol");
    }
    if (ik->protection_domain() != NULL) {
      guarantee(ik->protection_domain()->is_oop(),  "should be oop");
    }
    if (ik->signers() != NULL) {
      guarantee(ik->signers()->is_objArray(),       "should be obj array");
    }
    if (ik->generic_signature() != NULL) {
      guarantee(ik->generic_signature()->is_perm(),   "should be in permspace");
      guarantee(ik->generic_signature()->is_symbol(), "should be symbol");
    }
    if (ik->class_annotations() != NULL) {
      guarantee(ik->class_annotations()->is_typeArray(), "should be type array");
    }
    if (ik->fields_annotations() != NULL) {
      guarantee(ik->fields_annotations()->is_objArray(), "should be obj array");
    }
    if (ik->methods_annotations() != NULL) {
      guarantee(ik->methods_annotations()->is_objArray(), "should be obj array");
    }
    if (ik->methods_parameter_annotations() != NULL) {
      guarantee(ik->methods_parameter_annotations()->is_objArray(), "should be obj array");
    }
    if (ik->methods_default_annotations() != NULL) {
      guarantee(ik->methods_default_annotations()->is_objArray(), "should be obj array");
    }
if(ik->dependent_mcos()!=NULL){
guarantee(ik->dependent_mcos()->is_objArray(),"should be obj array");
    }
  }
}


bool instanceKlassKlass::oop_partially_loaded(oop obj) const {
  assert(obj->is_klass(), "object must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  assert(ik->oop_is_instance(), "object must be instanceKlass");
  return ik->transitive_interfaces() == (objArrayOop) obj;   // Check whether transitive_interfaces points to self
}


// The transitive_interfaces is the last field set when loading an object.
void instanceKlassKlass::oop_set_partially_loaded(oop obj) {
  assert(obj->is_klass(), "object must be klass");
  instanceKlass* ik = instanceKlass::cast(klassOop(obj));
  // Set the layout helper to a place-holder value, until fuller initialization.
  // (This allows asserts in oop_is_instance to succeed.)
  ik->set_layout_helper(Klass::instance_layout_helper(0, true));
  assert(ik->oop_is_instance(), "object must be instanceKlass");
  assert(ik->transitive_interfaces() == NULL, "just checking");
  ik->set_transitive_interfaces((objArrayOop) obj);   // Temporarily set transitive_interfaces to point to self
}

