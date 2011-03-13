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


#include "arrayOop.hpp"
#include "artaObjects.hpp"
#include "barrierSet.hpp"
#include "cardTableExtension.hpp"
#include "collectedHeap.hpp"
#include "copy.hpp"
#include "markSweep.hpp"
#include "mutexLocker.hpp"
#include "objArrayKlass.hpp"
#include "objArrayKlassKlass.hpp"
#include "objArrayOop.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "parallelScavengeHeap.hpp"
#include "psParallelCompact.hpp"
#include "psPromotionManager.hpp"
#include "psScavenge.hpp"
#include "resourceArea.hpp"
#include "sharedRuntime.hpp"
#include "systemDictionary.hpp"
#include "thread.hpp"
#include "vmSymbols.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "barrierSet.inline.hpp"
#include "bitMap.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
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
#include "space.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "threadLocalAllocBuffer.inline.hpp"
#include "thread_os.inline.hpp"

int objArrayKlass::oop_size(oop obj) const {
  assert(obj->is_objArray(), "must be object array");
  return objArrayOop(obj)->object_size();
}

int objArrayKlass::GC_oop_size(oop obj)const{
  // Can't assert(obj->is_objArray()), obj->_klass may not be available.
  return objArrayOop(obj)->object_size();
}

objArrayOop objArrayKlass::allocate(int length, intptr_t sba_hint, TRAPS) {  
  if (length >= 0) {
    if (length <= arrayOopDesc::max_array_length(T_OBJECT)) {
      int size = objArrayOopDesc::object_size(length);
      KlassHandle h_k(THREAD, as_klassOop());
objArrayOop a=NULL;
      assert0( THREAD->is_Java_thread() );
      if( UseSBA )
        a = (objArrayOop)((JavaThread*)THREAD)->sba_area()->allocate(h_k(), size, length, sba_hint).as_oop();
if(a==NULL)
        a = (objArrayOop)CollectedHeap::array_allocate(h_k, size, length, CHECK_NULL);
      assert(a->is_parsable(), "Can't publish unless parsable");
      return a;
    } else {
      THROW_OOP_0(Universe::out_of_memory_error_array_size());
    }
  } else {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
}

oop objArrayKlass::multi_allocate(int rank, jint* sizes, intptr_t sba_hint, TRAPS) { 
  int length = *sizes;
  // Call to lower_dimension uses this pointer, so most be called before a
  // possible GC
  KlassHandle h_lower_dimension(THREAD, lower_dimension());
  // If length < 0 allocate will throw an exception.
objArrayOop array=allocate(length,sba_hint,CHECK_NULL);
  assert(array->is_parsable(), "Don't handlize unless parsable");
  objArrayHandle h_array (THREAD, array);
  if (rank > 1) {
    if (length != 0) {
      for (int index = 0; index < length; index++) {  
        arrayKlass* ak = arrayKlass::cast(h_lower_dimension());
oop sub_array=ak->multi_allocate(rank-1,&sizes[1],sba_hint,CHECK_NULL);
        assert(sub_array->is_parsable(), "Don't publish until parsable");
        h_array->obj_at_put(index, sub_array);
      }
    } else {
      // Since this array dimension has zero length, nothing will be
      // allocated, however the lower dimension values must be checked
      // for illegal values.
      for (int i = 0; i < rank - 1; ++i) {
        sizes += 1;
        if (*sizes < 0) {
          THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
        }
      }
    }
  }
  return h_array();
}

void objArrayKlass::copy_array(arrayOop s, int src_pos, arrayOop d,
                               int dst_pos, int length, TRAPS) {
  assert(s->is_objArray(), "must be obj array");

  if (!d->is_objArray()) {
    THROW(vmSymbols::java_lang_ArrayStoreException());
  }

  // Check is all offsets and lengths are non negative
  if (src_pos < 0 || dst_pos < 0 || length < 0) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }
  // Check if the ranges are valid
  if  ( (((unsigned int) length + (unsigned int) src_pos) > (unsigned int) s->length())
     || (((unsigned int) length + (unsigned int) dst_pos) > (unsigned int) d->length()) ) {
    THROW(vmSymbols::java_lang_ArrayIndexOutOfBoundsException());
  }

#ifndef PRODUCT
  if( PrintStatistics )
    SharedRuntime::collect_arraycopy_stats(length*wordSize);
#endif
  // Special case.  Boundary cases must be checked first
  // This allows the following call: copy_array(s, s.length(), d.length(), 0).
  // This is correct, since the position is supposed to be an 'in between point', i.e., s.length(),
  // points to the right of the last element.
  if (length==0) {
    return;
  }

  // For use only in the fast case
objectRef*src=objArrayOop(s)->obj_at_addr(src_pos);
objectRef*dst=objArrayOop(d)->obj_at_addr(dst_pos);
  const size_t word_len = length * HeapWordsPerOop;

  // For performance reasons, we assume we are using a card marking write
  // barrier.  The assert will fail if this is not the case.
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->has_write_ref_array_opt(), "Barrier set must have ref array opt");

  if (s == d) {
    // Since source and destination are equal we do not need conversion checks
    // nor stack-escape checks.
Copy::conjoint_objectRefs_atomic(src,dst,length);
  } else {
    // We have to make sure all elements conform to the destination array
    klassOop bound = objArrayKlass::cast(d->klass())->element_klass();
    klassOop stype = objArrayKlass::cast(s->klass())->element_klass();
    if (stype == bound || Klass::cast(stype)->is_subtype_of(bound)) {
      // elements are guaranteed to be subtypes, so no check necessary
      if( UseSBA ) {            // Must check for escaping each word
objectRef sref=objectRef(s);
objectRef dref=objectRef(d);
        int s_fid = sref.is_stack() ? stackRef(sref).preheader()->fid() : HEAP_FID;
        int d_fid = dref.is_stack() ? stackRef(dref).preheader()->fid() : HEAP_FID;
        if( s_fid > d_fid ) {
          // Must check for escapes.  May GC at each store.  Move object-by-object.
Handle h_s(THREAD,s);
Handle h_d(THREAD,d);
for(int i=0;i<length;i++){
            objectRef* src = objArrayOop(h_s())->obj_at_addr(src_pos+i);
            objectRef* dst = objArrayOop(h_d())->obj_at_addr(dst_pos+i);
            ref_store( h_d.as_ref(), dst, ALWAYS_UNPOISON_OBJECTREF(*src) ); // Write-barrier and SBA escapes included
          }
          d = arrayOop(h_d());  // Reload, for stack test on exit
        } else {
Copy::conjoint_objectRefs_atomic(src,dst,length);
        }
      } else {
Copy::conjoint_objectRefs_atomic(src,dst,length);
      }
    } else {
      // Must check for escapes.  May GC at each store.  Move object-by-object.
      // slow case: need individual subtype checks
      // note: don't use obj_at_put below because it includes a redundant store check
Handle h_s(THREAD,s);
Handle h_d(THREAD,d);
for(int i=0;i<length;i++){
        objectRef* src = objArrayOop(h_s())->obj_at_addr(src_pos+i);
        objectRef* dst = objArrayOop(h_d())->obj_at_addr(dst_pos+i);
        objectRef element = lvb_ref(src);
        if (element.not_null() && !Klass::cast(element.as_oop()->klass())->is_subtype_of(bound)) {
          THROW(vmSymbols::java_lang_ArrayStoreException());
        }
        ref_store( h_d.as_ref(), dst, element ); // Write-barrier and SBA escapes included
      }
      d = arrayOop(h_d());      // Reload, for stack test on exit
    }
  }

  // Do not cardmark stack-based dest arrays.  Otherwise, it's "free" to
  // cardmark, as the cost to mark is trivial compared to the cost to do
  // element-by-element copy (and those are the only copies that do not need a
  // bulk card-mark).
  if( !objectRef(d).is_stack() )
    bs->write_ref_array(MemRegion((HeapWord*)dst, word_len));
}


klassRef objArrayKlass::array_klass_impl(klassRef thsi,bool or_null,int n,TRAPS){
  objArrayKlassHandle h_this(thsi);
  return array_klass_impl(h_this, or_null, n, CHECK_(klassRef()));
}


klassRef objArrayKlass::array_klass_impl(objArrayKlassHandle this_oop, bool or_null, int n, TRAPS) {  
  
  assert(this_oop->dimension() <= n, "check order of chain");
  int dimension = this_oop->dimension();
  if (dimension == n) 
    return this_oop();

  objArrayKlassHandle ak (THREAD, this_oop->higher_dimension());
  if (ak.is_null()) {    
    if (or_null)  return klassRef();

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
MutexLockerAllowGC mc(Compile_lock,jt);//for vtables
      // Ensure atomic creation of higher dimensions
MutexLockerAllowGC mu(MultiArray_lock,jt);

      // Check if another thread beat us
      ak = objArrayKlassHandle(THREAD, this_oop->higher_dimension());
      if( ak.is_null() ) {

        // Create multi-dim klass object and link them together
        klassOop new_klass = 
          objArrayKlassKlass::cast(Universe::objArrayKlassKlassObj())->
allocate_objArray_klass(dimension+1,this_oop,CHECK_(klassRef()));
        ak = objArrayKlassHandle(THREAD, new_klass);
        this_oop->set_higher_dimension(ak());    
        ak->set_lower_dimension(this_oop());
        assert(ak->oop_is_objArray(), "incorrect initialization of objArrayKlass");
      }
    }
  }

  if (or_null) {
    return ak->array_klass_or_null(n);
  }
  return array_klass(ak.as_klassRef(), n, CHECK_(klassRef()));
}

klassRef objArrayKlass::array_klass_impl(klassRef thsi, bool or_null, TRAPS) {
  return array_klass_impl(thsi, or_null, dimension() +  1, CHECK_(klassRef()));
}

bool objArrayKlass::can_be_primary_super_slow() const {
  if (!bottom_klass()->klass_part()->can_be_primary_super())
    // array of interfaces
    return false;
  else
    return Klass::can_be_primary_super_slow();
}

objArrayOop objArrayKlass::compute_secondary_supers(int num_extra_slots, TRAPS) {
  // interfaces = { cloneable_klass, serializable_klass, elemSuper[], ... };
  objArrayOop es = Klass::cast(element_klass())->secondary_supers();
  objArrayHandle elem_supers (THREAD, es);
  int num_elem_supers = elem_supers.is_null() ? 0 : elem_supers->length();
  int num_secondaries = num_extra_slots + 2 + num_elem_supers;
  if (num_secondaries == 2) {
    // Must share this for correct bootstrapping!
    return Universe::the_array_interfaces_array();
  } else {
    objArrayOop sec_oop = oopFactory::new_system_objArray(num_secondaries, CHECK_NULL);
    objArrayHandle secondaries(THREAD, sec_oop);
    secondaries->obj_at_put(num_extra_slots+0, SystemDictionary::cloneable_klass());
    secondaries->obj_at_put(num_extra_slots+1, SystemDictionary::serializable_klass());
    for (int i = 0; i < num_elem_supers; i++) {
      klassOop elem_super = (klassOop) elem_supers->obj_at(i);
      klassOop array_super = elem_super->klass_part()->array_klass_or_null();
      assert(array_super != NULL, "must already have been created");
      secondaries->obj_at_put(num_extra_slots+2+i, array_super);
    }
    return secondaries();
  }
}

bool objArrayKlass::compute_is_subtype_of(klassOop k) {
  if (!k->klass_part()->oop_is_objArray())
    return arrayKlass::compute_is_subtype_of(k);

  objArrayKlass* oak = objArrayKlass::cast(k);
  return element_klass()->klass_part()->is_subtype_of(oak->element_klass());
}


void objArrayKlass::initialize(TRAPS) {
  Klass::cast(bottom_klass())->initialize(THREAD);  // dispatches to either instanceKlass or typeArrayKlass
}


#define invoke_closure_on(base, closure, nv_suffix) {                                  \
  if (!base->is_null()) {                                                              \
    (closure)->do_oop##nv_suffix(base);                                                \
  }                                                                                    \
}

#define ObjArrayKlass_OOP_OOP_ITERATE_DEFN(OopClosureType, nv_suffix)           \
                                                                                \
int objArrayKlass::oop_oop_iterate##nv_suffix(oop obj,                          \
                                              OopClosureType* closure) {        \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::oa); \
  assert (Thread::current()->is_GenPauselessGC_thread() || obj->is_array(), "obj must be array"); \
  objArrayOop a = objArrayOop(obj);                                             \
  /* Get size before changing pointers. */                                      \
  /* Don't call size() or oop_size() since that is a virtual call. */           \
  int size = a->object_size();                                                  \
  if (closure->do_header()) {                                                   \
    a->oop_iterate_header(closure);                                             \
  }                                                                             \
  objectRef* base         = a->base();                                          \
  objectRef* const end    = base + a->length();                                 \
  const intx field_offset = PrefetchFieldsAhead;                                \
  if (field_offset > 0) {                                                       \
    while (base < end) {                                                        \
      prefetch_beyond(base, end, field_offset, closure->prefetch_style());      \
      invoke_closure_on(base, closure, nv_suffix);                              \
      base++;                                                                   \
    }                                                                           \
  } else {                                                                      \
    while (base < end) {                                                        \
      invoke_closure_on(base, closure, nv_suffix);                              \
      base++;                                                                   \
    }                                                                           \
  }                                                                             \
  return size;                                                                  \
}

#define ObjArrayKlass_OOP_OOP_ITERATE_DEFN_m(OopClosureType, nv_suffix)         \
                                                                                \
int objArrayKlass::oop_oop_iterate##nv_suffix##_m(oop obj,                      \
                                                  OopClosureType* closure,      \
                                                  MemRegion mr) {               \
  SpecializationStats::record_iterate_call##nv_suffix(SpecializationStats::oa); \
  assert(Thread::current()->is_GenPauselessGC_thread() || obj->is_array(), "obj must be array"); \
  objArrayOop a  = objArrayOop(obj);                                            \
  /* Get size before changing pointers. */                                      \
  /* Don't call size() or oop_size() since that is a virtual call */            \
  int size = a->object_size();                                                  \
  if (closure->do_header()) {                                                   \
    a->oop_iterate_header(closure, mr);                                         \
  }                                                                             \
  objectRef* bottom = (objectRef*)mr.start();                                   \
  objectRef* top    = (objectRef*)mr.end();                                     \
  objectRef* base = a->base();                                                  \
  objectRef* end    = base + a->length();                                      \
  if (base < bottom) {                                                          \
    base = bottom;                                                              \
  }                                                                             \
  if (end > top) {                                                              \
    end = top;                                                                  \
  }                                                                             \
  const intx field_offset = PrefetchFieldsAhead;                                \
  if (field_offset > 0) {                                                       \
    while (base < end) {                                                        \
      prefetch_beyond(base, end, field_offset, closure->prefetch_style());      \
      invoke_closure_on(base, closure, nv_suffix);                              \
      base++;                                                                   \
    }                                                                           \
  } else {                                                                      \
    while (base < end) {                                                        \
      invoke_closure_on(base, closure, nv_suffix);                              \
      base++;                                                                   \
    }                                                                           \
  }                                                                             \
  return size;                                                                  \
}

ALL_OOP_OOP_ITERATE_CLOSURES_1(ObjArrayKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_3(ObjArrayKlass_OOP_OOP_ITERATE_DEFN)
ALL_OOP_OOP_ITERATE_CLOSURES_1(ObjArrayKlass_OOP_OOP_ITERATE_DEFN_m)
ALL_OOP_OOP_ITERATE_CLOSURES_3(ObjArrayKlass_OOP_OOP_ITERATE_DEFN_m)

int objArrayKlass::oop_adjust_pointers(oop obj) {
  assert(obj->is_objArray(), "obj must be obj array");
  objArrayOop a = objArrayOop(obj);
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = a->object_size();
heapRef*base=(heapRef*)a->base();
  heapRef* const end = base + a->length();
  while (base < end) {
    assert0(objectRef::is_null_or_heap(base));
    MarkSweep::adjust_pointer(base);
    base++;
  }  
  return size;
}

void objArrayKlass::oop_copy_contents(PSPromotionManager* pm, oop obj) {
  assert(!pm->depth_first(), "invariant");
  assert(obj->is_objArray(), "obj must be obj array");
  // Compute oop range
heapRef*curr=(heapRef*)objArrayOop(obj)->base();
  heapRef* end = curr + objArrayOop(obj)->length();
  //  assert(align_object_size(end - (oop*)obj) == oop_size(obj), "checking size");
  assert(align_object_size(pointer_delta(end, obj, sizeof(oop*)))
	                          == oop_size(obj), "checking size");

  // Iterate over oops
  while (curr < end) {
if(PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*curr))){
      pm->claim_or_forward_breadth(curr);
    }
    ++curr;
  }
}

void objArrayKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(pm->depth_first(), "invariant");
  assert(obj->is_objArray(), "obj must be obj array");
  // Compute oop range
heapRef*curr=(heapRef*)objArrayOop(obj)->base();
  heapRef* end = curr + objArrayOop(obj)->length();
  //  assert(align_object_size(end - (oop*)obj) == oop_size(obj), "checking size");
  assert(align_object_size(pointer_delta(end, obj, sizeof(oop*)))
	                          == oop_size(obj), "checking size");

  // Iterate over oops
  while (curr < end) {
    assert(objectRef::is_null_or_heap(curr), "must be heapRef");
    if (PSScavenge::should_scavenge(ALWAYS_UNPOISON_OBJECTREF(*curr))) {
      pm->claim_or_forward_depth(curr);
    }
    ++curr;
  }
}

int objArrayKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert (obj->is_objArray(), "obj must be obj array");
  objArrayOop a = objArrayOop(obj);

  heapRef* const base = (heapRef*)a->base();
  heapRef* const beg_oop = base;
  heapRef* const end_oop = base + a->length();
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }
  return a->object_size();
}

int objArrayKlass::oop_update_pointers(ParCompactionManager* cm, oop obj,
				       HeapWord* beg_addr, HeapWord* end_addr) {
  assert (obj->is_objArray(), "obj must be obj array");
  objArrayOop a = objArrayOop(obj);

  heapRef* const base = (heapRef*)a->base();
  heapRef* const beg_oop = MAX2((heapRef*)beg_addr, base);
  heapRef* const end_oop = MIN2((heapRef*)end_addr, base + a->length());
  for (heapRef* cur_oop = beg_oop; cur_oop < end_oop; ++cur_oop) {
    PSParallelCompact::adjust_pointer(cur_oop);
  }
  return a->object_size();
}


// JVM support

jint objArrayKlass::compute_modifier_flags(TRAPS) const {
  // The modifier for an objectArray is the same as its element
  if (element_klass() == NULL) {
    assert(Universe::is_bootstrapping(), "partial objArray only at startup");
    return JVM_ACC_ABSTRACT | JVM_ACC_FINAL | JVM_ACC_PUBLIC;
  }
  // Recurse down the element list
  jint element_flags = Klass::cast(element_klass())->compute_modifier_flags(CHECK_0);  

  return (element_flags & (JVM_ACC_PUBLIC | JVM_ACC_PRIVATE | JVM_ACC_PROTECTED))
                        | (JVM_ACC_ABSTRACT | JVM_ACC_FINAL);  
}


#ifndef PRODUCT
// Printing

void objArrayKlass::oop_print_on(oop obj, outputStream* st) {
  arrayKlass::oop_print_on(obj, st);
  assert(obj->is_objArray(), "must be objArray");
  objArrayOop oa = objArrayOop(obj);
  int print_len = MIN2((intx) oa->length(), MaxElementPrintSize);
  for(int index = 0; index < print_len; index++) {
    st->print(" - %3d : ", index);
    oa->obj_at(index)->print_value_on(st);
    st->cr();
  }
  int remaining = oa->length() - print_len;
  if (remaining > 0) {
    tty->print_cr(" - <%d more elements, increase MaxElementPrintSize to print>", remaining);
  }
}


void objArrayKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_objArray(), "must be objArray");
  element_klass()->print_value_on(st);
  st->print("a [%d] [%d] ", objArrayOop(obj)->length(), objArrayOop(obj)->ekid());
  as_klassOop()->klass()->print_value_on(st);
}

#endif // PRODUCT

const char* objArrayKlass::internal_name() const {
  return external_name();
}

// Verification

void objArrayKlass::oop_verify_on(oop obj, outputStream* st) {
  arrayKlass::oop_verify_on(obj, st);
  guarantee(obj->is_objArray(), "must be objArray");
  objArrayOop             oa = objArrayOop(obj);
  objArrayKlass *array_klass = objArrayKlass::cast(oa->klass());
  // TODO: don't ignore system object arrays
  if( array_klass->klassId() == systemObjArrayKlass_kid ) return;
  klassOop     element_klass = array_klass->element_klass();
  guarantee( element_klass->klass_part()->klassId() == oa->ekid(), "ekid disagreement");
  for(int index = 0; index < oa->length(); index++) {
oop element=oa->obj_at(index);
if(element!=NULL){
guarantee(element->is_oop(),"should be oop");
klassOop cur_elem_klass=element->klass();
      guarantee(cur_elem_klass->klass_part()->is_subtype_of(element_klass) || cur_elem_klass->is_klass(), "element type not in agreement with array");
    }
  }
}

void objArrayKlass::oop_verify_old_oop(oop obj, objectRef* p, bool allow_dirty) {
  /* $$$ move into remembered set verification?
  RememberedSet::verify_old_oop(obj, p, allow_dirty, true);
  */
}


void objArrayKlass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
    { xmlElement xn(xb, "name");
      xb->print("%s[%d]", element_klass()->klass_part()->external_name(), ((arrayOop)obj)->length());
    }
  } else {
    oop_print_xml_on_as_object(obj, xb);
  }
}

void objArrayKlass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
objArrayOop oa=objArrayOop(obj);

  xmlElement o(xb, "array");
  { xmlElement xe(xb, "class");
    { xmlElement xn(xb, "name");
      xb->print("%s[%d]", element_klass()->klass_part()->external_name(), ((arrayOop)obj)->length());
    }
  }
  { xmlElement e(xb, "element_class");
    element_klass()->print_xml_on(xb, true);
  }
  { xmlElement l(xb, "length");
    xb->print("%d", oa->length());
  }
  { xmlElement a(xb, "elements");
    int print_len = oa->length();
    for (int index = 0; index < print_len; index++) {
      xmlElement el(xb, "elem");
      xb->name_value_item("idx", index);
      { xmlElement va(xb, "val");
        oa->obj_at(index)->print_xml_on(xb, true);
      }
    }
  }
}
