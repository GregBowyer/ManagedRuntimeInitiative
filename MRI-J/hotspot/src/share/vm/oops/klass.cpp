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


#include "artaObjects.hpp"
#include "collectedHeap.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_newCollector.hpp"
#include "gpgc_nmt.hpp"
#include "gpgc_oldCollector.hpp"
#include "gpgc_thread.hpp"
#include "instanceKlass.hpp"
#include "klass.hpp"
#include "methodOop.hpp"
#include "objArrayOop.hpp"
#include "oopFactory.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "systemDictionary.hpp"
#include "tickProfiler.hpp"
#include "vmSymbols.hpp"
#include "xmlBuffer.hpp"

#include "atomic_os_pd.inline.hpp"
#include "collectedHeap.inline.hpp"
#include "frame.inline.hpp"
#include "gpgc_oldCollector.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "markWord.inline.hpp"
#include "mutex.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "os_os.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "thread_os.inline.hpp"

#include "oop.inline2.hpp"

// Must not be inlined virtual call
void Klass_vtbl::unused_initial_virtual() { }
void Klass     ::unused_initial_virtual() { }


bool Klass::is_subclass_of(klassOop k) const {
  // Run up the super chain and check
  klassOop t = as_klassOop();
    
  if (t == k) return true;
  t = Klass::cast(t)->super();

  while (t != NULL) {  
    if (t == k) return true;
    t = Klass::cast(t)->super();
  }
  return false;
}

bool Klass::search_secondary_supers(klassOop k) const {
  // Put some extra logic here out-of-line, before the search proper.
  // This cuts down the size of the inline method.

  // This is necessary, since I am never in my own secondary_super list.
  if (this->as_klassOop() == k)
    return true;
  // Scan the array-of-objects for a match
  int cnt = secondary_supers()->length();
  for (int i = 0; i < cnt; i++) {
    if (secondary_supers()->obj_at(i) == k) {
      ((Klass*)this)->set_secondary_super_cache(k->klass_part()->klassId());
      return true;
    }
  }
  return false;
}

// Find LCA in class heirarchy
Klass *Klass::LCA( Klass *k2 ) {
  Klass *k1 = this;
  if( k1->is_interface() && !k2->is_interface() )
    return k2->LCA(k1);       // Get interface to 2nd parm
if(k2->is_interface()&&!k1->is_interface()){
    // LCA of oop vs interface.  
    if( k1->is_subtype_of(k2->as_klassOop()) ) return k2;
    objArrayOop oao = ((instanceKlass*)k2)->transitive_interfaces();
int len=oao->length();
    for( int i=0; i<len; i++ ) {
      Klass *k = Klass::cast((klassOop)oao->obj_at(i));
      if( k1->is_subtype_of(k->as_klassOop()) )
        return k;
    }
    // Object does not implement any interface, so fall to Object
    // by falling into the next code.
  }

  while( 1 ) {
    if( k1->is_subtype_of(k2->as_klassOop()) ) return k2;
    if( k2->is_subtype_of(k1->as_klassOop()) ) return k1;
    k1 = k1->super()->klass_part();
    k2 = k2->super()->klass_part();
  }
}


// Find any finalizable subclass of this class, or NULL if none.  Used by C2
// to allow compiled allocation of newInstance idioms where the exact class
// isn't known until runtime.
const Klass* Klass::find_finalizable_subclass() const {
if(is_interface())return NULL;
  if (has_finalizer()) return this;
Klass*k=subklass();
while(k!=NULL){
    const Klass* result = k->find_finalizable_subclass();
    if (result != NULL) return result;
    k = k->next_sibling();
  }
  return NULL;
}


void Klass::check_valid_for_instantiation(bool throwError, TRAPS) {
  ResourceMark rm(THREAD);
  THROW_MSG(throwError ? vmSymbols::java_lang_InstantiationError()
	    : vmSymbols::java_lang_InstantiationException(), external_name());
}


void Klass::copy_array(arrayOop s, int src_pos, arrayOop d, int dst_pos, int length, TRAPS) {
  THROW(vmSymbols::java_lang_ArrayStoreException());
}


void Klass::initialize(TRAPS) {
  ShouldNotReachHere();
}

bool Klass::compute_is_subtype_of(klassOop k) {
  assert(k->is_klass(), "argument must be a class");
  return is_subclass_of(k);
}


methodOop Klass::uncached_lookup_method(symbolOop name, symbolOop signature) const {
#ifdef ASSERT
  tty->print_cr("Error: uncached_lookup_method called on a klass oop."
                " Likely error: reflection method does not correctly"
                " wrap return value in a mirror object.");
#endif
  ShouldNotReachHere();
  return NULL;
}

klassOop Klass::base_create_klass_oop(KlassHandle& klass, int size, 
                                      const Klass_vtbl& vtbl, juint klassId, TRAPS) {
  // allocate and initialize vtable
  Klass*   kl = (Klass*) vtbl.allocate_permanent(klass, size, CHECK_NULL);
  klassOop k  = kl->as_klassOop();
  if( klassId == 0 ) {
    KlassTable::registerKlass(kl);
  } else {
    KlassTable::bindReservedKlassId(k,klassId);
  }

  { // Preinitialize supertype information.
    // A later call to initialize_supers() may update these settings:
    kl->set_super(NULL);
    for (juint i = 0; i < Klass::primary_super_limit(); i++) {
      kl->_primary_supers_kids[i] = 0;
    }
    kl->set_secondary_supers(NULL);
    kl->_primary_supers_kids[0] = klassId;
kl->set_super_check_offset(primary_supers_kids_offset_in_bytes()+sizeof(oopDesc));
  }

  kl->set_java_mirror(NULL);
  kl->set_modifier_flags(0);
  kl->set_layout_helper(Klass::_lh_neutral_value);
  kl->set_name(NULL);
  AccessFlags af;
  af.set_flags(0);
  kl->set_access_flags(af);
  kl->init_subklass();
  kl->init_next_sibling();
  kl->set_alloc_count(0);
  kl->set_alloc_size(0);
  kl->reset_sma_successes();
  kl->reset_sma_failures();

  return k;
}

KlassHandle Klass::base_create_klass(KlassHandle& klass, int size,
const Klass_vtbl&vtbl,juint klassId,TRAPS){
klassOop ek=base_create_klass_oop(klass,size,vtbl,klassId,THREAD);
  return KlassHandle(THREAD, ek);
}

void Klass_vtbl::post_new_init_klass(KlassHandle& klass, 
				     klassOop new_klass,
				     int size) const {
  assert(!new_klass->klass_part()->null_vtbl(), "Not a complete klass");
  CollectedHeap::post_allocation_install_obj_klass(klass, new_klass, size);
}

Klass* Klass_vtbl::klass_allocate(KlassHandle& klass, int size, TRAPS) const {
  // The vtable pointer is installed during the execution of 
  // constructors in the call to permanent_obj_allocate().  Delay
  // the installation of the klass pointer into the new klass "k"
  // until after the vtable pointer has been installed (i.e., until
  // after the return of permanent_obj_allocate().
  klassOop k = 
    (klassOop) CollectedHeap::permanent_obj_allocate_no_klass_install(klass,
      size, CHECK_NULL);
  return k->klass_part();
}

jint Klass::array_layout_helper(BasicType etype) {
  assert(etype >= T_BOOLEAN && etype <= T_OBJECT, "valid etype");
  // Note that T_ARRAY is not allowed here.
  int  hsize = arrayOopDesc::base_offset_in_bytes(etype);
  int  esize = type2aelembytes[etype];
  bool isobj = (etype == T_OBJECT);
  int  tag   =  isobj ? _lh_array_tag_obj_value : _lh_array_tag_type_value;
  int lh = array_layout_helper(tag, hsize, etype, exact_log2(esize));

  assert(lh < (int)_lh_neutral_value, "must look like an array layout");
  assert(layout_helper_is_javaArray(lh), "correct kind");
  assert(layout_helper_is_objArray(lh) == isobj, "correct kind");
  assert(layout_helper_is_typeArray(lh) == !isobj, "correct kind");
  assert(layout_helper_header_size(lh) == hsize, "correct decode");
  assert(layout_helper_element_type(lh) == etype, "correct decode");
  assert(1 << layout_helper_log2_element_size(lh) == esize, "correct decode");

  return lh;
}

bool Klass::can_be_primary_super_slow() const {
  if (super() == NULL)
    return true;
  else if (super()->klass_part()->super_depth() >= primary_super_limit()-1)
    return false;
  else
    return true;
}

void Klass::initialize_supers(klassOop k, TRAPS) {
  if (FastSuperclassLimit == 0) {
    // None of the other machinery matters.
    set_super(k);
    return;
  }
  if (k == NULL) {
    set_super(NULL);
    _primary_supers_kids[0] = klassId();
    assert(super_depth() == 0, "Object must already be initialized properly");
  } else if (k != super() || k == SystemDictionary::object_klass()) {
    assert(super() == NULL || super() == SystemDictionary::object_klass(),
	   "initialize this only once to a non-trivial value");
    set_super(k);
    Klass* sup = k->klass_part();
    int sup_depth = sup->super_depth();
    juint my_depth  = MIN2(sup_depth + 1, (int)primary_super_limit());
    if (!can_be_primary_super_slow())
      my_depth = primary_super_limit();
    for (juint i = 0; i < my_depth; i++) {
      _primary_supers_kids[i] = sup->_primary_supers_kids[i];
    }
juint*super_check_cell;
    if (my_depth < primary_super_limit()) {
      _primary_supers_kids[my_depth] = klassId();
super_check_cell=&_primary_supers_kids[my_depth];
    } else {
      // Overflow of the primary_supers array forces me to be secondary.
super_check_cell=&_secondary_super_kid_cache;
    }
    set_super_check_offset((address)super_check_cell - (address) this->as_klassOop());

#ifdef ASSERT
    {
      juint j = super_depth();
      assert(j == my_depth, "computed accessor gets right answer");
      klassOop t = as_klassOop();
      while (!Klass::cast(t)->can_be_primary_super()) {
	t = Klass::cast(t)->super();
	j = Klass::cast(t)->super_depth();
      }
      for (juint j1 = j+1; j1 < primary_super_limit(); j1++) {
assert(primary_super_of_depth(j1)==0,"super list padding");
      }
      while (t != NULL) {
assert(primary_super_of_depth(j)==Klass::cast(t)->klassId(),"super list initialization");
	t = Klass::cast(t)->super();
	--j;
      }
      assert(j == (juint)-1, "correct depth count");
    }
#endif
  }

  if (secondary_supers() == NULL) {
    KlassHandle this_kh (THREAD, this);

    // Now compute the list of secondary supertypes.
    // Secondaries can occasionally be on the super chain,
    // if the inline "_primary_supers" array overflows.
    int extras = 0;
    klassOop p;
    for (p = super(); !(p == NULL || p->klass_part()->can_be_primary_super()); p = p->klass_part()->super()) {
      ++extras;
    }

    // Compute the "real" non-extra secondaries.
    objArrayOop secondary_oops = compute_secondary_supers(extras, CHECK);
    objArrayHandle secondaries (THREAD, secondary_oops);

    // Store the extra secondaries in the first array positions:
    int fillp = extras;
    for (p = this_kh->super(); !(p == NULL || p->klass_part()->can_be_primary_super()); p = p->klass_part()->super()) {
      int i;                    // Scan for overflow primaries being duplicates of 2nd'arys

      // This happens frequently for very deeply nested arrays: the
      // primary superclass chain overflows into the secondary.  The
      // secondary list contains the element_klass's secondaries with
      // an extra array dimension added.  If the element_klass's
      // secondary list already contains some primary overflows, they
      // (with the extra level of array-ness) will collide with the
      // normal primary superclass overflows.
      for( i = extras; i < secondaries->length(); i++ )
        if( secondaries->obj_at(i) == p )
          break;
      if( i < secondaries->length() )
        continue;               // It's a dup, don't put it in
      secondaries->obj_at_put(--fillp, p);
    }
    // See if we had some dup's, so the array has holes in it.
    if( fillp > 0 ) {
      // Pack the array.  Drop the old secondaries array on the floor
      // and let GC reclaim it.
      objArrayOop s2 = oopFactory::new_system_objArray(secondaries->length() - fillp, CHECK);
      for( int i = 0; i < s2->length(); i++ )
        s2->obj_at_put( i, secondaries->obj_at(i+fillp) );
      secondaries = objArrayHandle(THREAD, s2);
    }

  #ifdef ASSERT
    if (secondaries() != Universe::the_array_interfaces_array()) {
      // We must not copy any NULL placeholders left over from bootstrap.
      for (int j = 0; j < secondaries->length(); j++) {
	assert(secondaries->obj_at(j) != NULL, "correct bootstrapping order");
      }
    }
  #endif

    this_kh->set_secondary_supers(secondaries());
  }
}

objArrayOop Klass::compute_secondary_supers(int num_extra_slots, TRAPS) {
  assert(num_extra_slots == 0, "override for complex klasses");
  return Universe::the_empty_system_obj_array();
}


// this accessor deals with concurrent klass revisiting
klassRef Klass::subklass_ref() const {
  // TODO: Is it safe for GC threads to be using this method?
  // assert0(Thread::current()->is_Java_thread());

  if ( UseGenPauselessGC ) {
    long state = GPGC_OldCollector::collection_state();
    if( state>=GPGC_Collector::ConcurrentRefProcessing
      && state<=GPGC_Collector::ConcurrentWeakMarking )
    {
      heapRef* adr_subklass = (heapRef*)&_subklass;
      klassRef sub_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_subklass).raw_value());
      while( sub_klassref.not_null()
        && !GPGC_Marks::is_any_marked_strong_live(sub_klassref.as_oop())
        && !GPGC_Marks::is_any_marked_final_live(sub_klassref.as_oop()) )
      {
        // first subklass not alive, find first one alive
        adr_subklass = sub_klassref.as_klassOop()->klass_part()->adr_next_sibling();
        sub_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_subklass).raw_value());
      }
      return sub_klassref.is_null() ? klassRef() : sub_klassref;
    }
  }

#ifdef ASSERT
  assert(is_valid_klassref(lvb_klassRef(&_subklass)),"invalid _subklass");
#endif
  return _subklass.is_null() ? klassRef() : lvb_klassRef(&_subklass);
}


// this accessor deals with concurrent klass revisiting
Klass* Klass::subklass() const {
klassOop sub_klassOop=subklass_oop();

return sub_klassOop==NULL?NULL:Klass::cast(sub_klassOop);
}


heapRef* Klass::adr_subklass() const {
  heapRef* adr_subklass = (heapRef*)&_subklass;
  if ( UseGenPauselessGC ) {
    long state = GPGC_OldCollector::collection_state();
    if( state>=GPGC_Collector::ConcurrentRefProcessing
      && state<=GPGC_Collector::ConcurrentWeakMarking )
    {
      klassRef sub_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_subklass).raw_value());
      while( sub_klassref.not_null()
        && !GPGC_Marks::is_any_marked_strong_live(sub_klassref.as_oop())
        && !GPGC_Marks::is_any_marked_final_live(sub_klassref.as_oop()) )
      {
        // first subklass not alive, find first one alive
        adr_subklass = sub_klassref.as_klassOop()->klass_part()->adr_next_sibling();
        sub_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_subklass).raw_value());
      }
      return adr_subklass;
    }
  } 
#ifdef ASSERT
  assert(is_valid_klassref(lvb_klassRef((klassRef*)adr_subklass)),"invalid _subklass");
#endif
  return adr_subklass; 
}

instanceKlass* Klass::superklass() const {
  assert(super() == NULL || super()->klass_part()->oop_is_instance(), "must be instance klass");
return super()==NULL?NULL:instanceKlass::cast(super());
}

bool Klass::is_valid_klassref(klassRef klass_ref){
  if ( UseGenPauselessGC ) {
    long state = GPGC_OldCollector::collection_state();
    if ( state>GPGC_Collector::ConcurrentWeakMarking || state==GPGC_Collector::NotCollecting ) {
      return ( (klass_ref.is_null()) ||  GPGC_NMT::has_desired_nmt(klass_ref) );
    }
  }
  return true;
}


// accessor that needs to deal with klass revisiting
klassRef Klass::next_sibling_ref() const {
  // TODO: Is it safe for GC threads to be using this method?
  // assert0(Thread::current()->is_Java_thread());

  if ( UseGenPauselessGC ) {
    long state = GPGC_OldCollector::collection_state();
    if( state>=GPGC_Collector::ConcurrentRefProcessing
      && state<=GPGC_Collector::ConcurrentWeakMarking )
    {
      heapRef* adr_next_sibling = (heapRef*)&_next_sibling;
      klassRef next_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_next_sibling).raw_value());
      while( next_klassref.not_null()
        && !GPGC_Marks::is_any_marked_strong_live(next_klassref.as_oop())
        && !GPGC_Marks::is_any_marked_final_live(next_klassref.as_oop()) )
      {
        // first sibling not alive, find first one alive
        adr_next_sibling = (heapRef*)&next_klassref.as_klassOop()->klass_part()->_next_sibling;
        next_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_next_sibling).raw_value());
      }
      return next_klassref.is_null() ? klassRef() : next_klassref;
    }
  } 
  assert(is_valid_klassref(lvb_klassRef(&_next_sibling)),"invalid _next_sibling");
  return _next_sibling.is_null() ? klassRef() : lvb_klassRef(&_next_sibling);
}


// accessor that needs to deal with klass revisiting
Klass* Klass::next_sibling() const {
  klassOop next_siblingOop = next_sibling_oop();

return next_siblingOop==NULL?NULL:Klass::cast(next_siblingOop);
}


heapRef* Klass::adr_next_sibling() const {
  heapRef* adr_next_sibling = (heapRef*)&_next_sibling;
  if (  UseGenPauselessGC ) {
    long state = GPGC_OldCollector::collection_state();
    if( state>=GPGC_Collector::ConcurrentRefProcessing
      && state<=GPGC_Collector::ConcurrentWeakMarking )
    {
      klassRef next_klassref = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_next_sibling).raw_value());
      while( next_klassref.not_null()
        && !GPGC_Marks::is_any_marked_strong_live(next_klassref.as_oop())
        && !GPGC_Marks::is_any_marked_final_live(next_klassref.as_oop()) )
      {
        // first sibling not alive, find first one alive
        adr_next_sibling = (heapRef*)&next_klassref.as_klassOop()->klass_part()->_next_sibling;
        next_klassref    = klassRef(GPGC_OldCollector::remap_and_nmt_without_cas(adr_next_sibling).raw_value());
      }
      return adr_next_sibling;
    }
  } 
  assert(is_valid_klassref(lvb_klassRef((klassRef*)adr_next_sibling)),"invalid _next_sibling");
  return adr_next_sibling; 
}

void Klass::init_subklass(){
  ref_store_without_check(&_subklass, klassRef((uint64_t)NULL));
}

void Klass::init_next_sibling(){
  ref_store_without_check(&_next_sibling, klassRef((uint64_t)NULL));
}

void Klass::set_subklass(klassOop s) {
  // assert that we are holding the compile_lock
#ifdef ASSERT
  if ( UseGenPauselessGC ) {
    Thread* owner_thread = Compile_lock.owner();
    Thread* current_thread = Thread::current();
    if ( owner_thread ) {
      if  ( GPGC_Thread::old_gc_thread() == owner_thread ) {
        assert(GPGC_OldCollector::collection_state() == GPGC_Collector::ConcurrentWeakMarking, "The only time the old collector owns the compile lock is when revisiting klasses");
      } else {
        assert( current_thread == owner_thread, "the current thread should be holding the lock ");
      }
    }
  }
#endif
  assert(s != as_klassOop(), "sanity check");
  ref_store_without_check(&_subklass, klassRef(s));
}

void Klass::set_next_sibling(klassOop s) {
#ifdef ASSERT
  if ( UseGenPauselessGC ) {
    Thread* owner_thread = Compile_lock.owner();
    Thread* current_thread = Thread::current();
    if ( owner_thread ) {
      if  ( GPGC_Thread::old_gc_thread() == owner_thread ) {
        assert(GPGC_OldCollector::collection_state() == GPGC_Collector::ConcurrentWeakMarking, "The only time the old collector owns the compile lock is when revisiting klasses");
      } else {
        assert( current_thread == owner_thread, "the current thread should be holding the lock ");
      }
    }
  }
#endif
  assert(s != as_klassOop(), "sanity check");
  ref_store_without_check(&_next_sibling, klassRef(s));
}

//  we will hold the compile lock, so should be ok to do it with concurrent revisiting
void Klass::append_to_sibling_list() {
  debug_only(if (!SharedSkipVerify) as_klassOop()->verify();)
  // add ourselves to superklass' subklass list
  instanceKlass* super = superklass();
  if (super == NULL) return;        // special case: class Object
assert(
         (!super->is_interface()    // interfaces cannot be supers
          && (super->superklass() == NULL || !is_interface())),
         "an interface can only be a subklass of Object");
  klassOop prev_first_subklass =  super->subklass_oop();
  if (prev_first_subklass != NULL) {
    // set our sibling to be the superklass' previous first subklass
    set_next_sibling(prev_first_subklass);
  }
  // make ourselves the superklass' first subklass
  super->set_subklass(as_klassOop());
  debug_only(if (!SharedSkipVerify) as_klassOop()->verify();)
}


void Klass::follow_weak_klass_links( BoolObjectClosure* is_alive, OopClosure* keep_alive) {
  // This klass is alive but the subklass and siblings are not followed/updated.
  // We update the subklass link and the subklass' sibling links here. 
  // Our own sibling link will be updated by our superclass (which must be alive 
  // since we are).
  assert(is_alive->do_object_b(as_klassOop()), "just checking, this should be live");
  if (ClassUnloading) {
    klassOop sub =  subklass_oop();
    if (sub != NULL && !is_alive->do_object_b(sub)) {
      // first subklass not alive, find first one alive
      do {
        sub = sub->klass_part()->next_sibling_oop();

      } while (sub != NULL && !is_alive->do_object_b(sub));
      set_subklass(sub);
    }
    // now update the subklass' sibling list
    while (sub != NULL) {
      klassOop next = sub->klass_part()->next_sibling_oop();
      if (next != NULL && !is_alive->do_object_b(next)) {
        // first sibling not alive, find first one alive
        do {
          next = next->klass_part()->next_sibling_oop();
        } while (next != NULL && !is_alive->do_object_b(next));
        sub->klass_part()->set_next_sibling(next);
      }
      sub = next;
    }
  } else {
    // Always follow subklass and sibling link. This will prevent any klasses from 
    // being unloaded (all classes are transitively linked from java.lang.Object).
    keep_alive->do_oop(adr_subklass());
    keep_alive->do_oop(adr_next_sibling());
  }
}


void Klass::GPGC_follow_weak_klass_links(){
  // This klass is alive but the subklass and siblings are not followed/updated.
  // We update the subklass link and the subklass' sibling links here. 
  // Our own sibling link will be updated by our superclass (which must be alive 
  // since we are).
  assert(GPGC_Marks::is_any_marked_strong_live(as_klassOop()), "just checking");
  if (ClassUnloading) {
    objectRef sub_ref = GPGC_OldCollector::remap_and_nmt_only((heapRef *)&_subklass);
    klassOop  sub     = (klassOop) sub_ref.as_oop();
    if (sub_ref.not_null() && !GPGC_Marks::is_old_marked_strong_live(sub_ref)) {
      // first subklass not alive, find first one alive
      do {
        sub_ref = GPGC_OldCollector::remap_and_nmt_only((heapRef*) &(sub->klass_part()->_next_sibling));
        sub     = (klassOop) sub_ref.as_oop();
      } while (sub_ref.not_null() && !GPGC_Marks::is_old_marked_strong_live(sub_ref));
      set_subklass(sub);
    }
    // now update the subklass' sibling list
    while (sub != NULL) {
      objectRef next_ref = GPGC_OldCollector::remap_and_nmt_only((heapRef*) &(sub->klass_part()->_next_sibling));
      klassOop  next     = (klassOop) next_ref.as_oop();
      if (next_ref.not_null() && !GPGC_Marks::is_old_marked_strong_live(next_ref)) {
        // first sibling not alive, find first one alive
        do {
          next_ref = GPGC_OldCollector::remap_and_nmt_only((heapRef *) &(next->klass_part()->_next_sibling));
          next     = (klassOop) next_ref.as_oop();
        } while (next_ref.not_null() && !GPGC_Marks::is_old_marked_strong_live(next_ref));
        sub->klass_part()->set_next_sibling(next);
      }
      sub = next;
    }
  } else {
    // Always follow subklass and sibling link. This will prevent any klasses from 
    // being unloaded (all classes are transitively linked from java.lang.Object).
    GPGC_OldCollector::mark_to_live(adr_subklass());
    GPGC_OldCollector::mark_to_live(adr_next_sibling());
  }
}


void Klass::GPGC_sweep_weak_methodCodes(GPGC_GCManagerOldStrong* gcm) {
  // Only interested in classes with methods.
}

// this function doesn't seem to be used currently; if you plan to use it make sure to take into consideration how it interacts with concurrent klass revisiting
void Klass::remove_unshareable_info() {
  if ( UseGenPauselessGC ) { ShouldNotReachHere(); }
  if (oop_is_instance()) {
    instanceKlass* ik = (instanceKlass*)this;
    if (ik->is_linked()) {
      ik->unlink_class();
    }
  }
  set_subklass(NULL);
  set_next_sibling(NULL);
}


klassOop Klass::array_klass_or_null(int rank) {
  EXCEPTION_MARK;
  // No exception can be thrown by array_klass_impl when called with or_null == true. 
  // (In anycase, the execption mark will fail if it do so)
  return array_klass_impl(klassRef(as_klassOop()),true, rank, THREAD).as_klassOop();
}


klassOop Klass::array_klass_or_null() {
  EXCEPTION_MARK;
  // No exception can be thrown by array_klass_impl when called with or_null == true. 
  // (In anycase, the execption mark will fail if it do so)
  return array_klass_impl(klassRef(as_klassOop()),true, THREAD).as_klassOop(); 
}


klassRef Klass::array_klass_impl(klassRef thsi,bool or_null,int rank,TRAPS){
  fatal("array_klass should be dispatched to instanceKlass, objArrayKlass or typeArrayKlass");
  return klassRef();
}


klassRef Klass::array_klass_impl(klassRef thsi, bool or_null, TRAPS) {
  fatal("array_klass should be dispatched to instanceKlass, objArrayKlass or typeArrayKlass");
  return klassRef();
}


void Klass::with_array_klasses_do(void f(klassOop k)) {
  f(as_klassOop());
}


const char* Klass::external_name() const {
return name()?name()->as_klass_external_name():NULL;
}


char* Klass::signature_name() const {
  return name() ? name()->as_C_string() : NULL;
}

const char*Klass::pretty_name()const{
  return name() ? name()->as_klass_pretty_name() : NULL;
}

// Unless overridden, modifier_flags is 0.
jint Klass::compute_modifier_flags(TRAPS) const {
  return 0;
}

// Unless overridden, jvmti_class_status has no flags set.
jint Klass::jvmti_class_status() const {
  return 0;
}

#ifndef PRODUCT

// Printing

void Klass::oop_print_on(oop obj, outputStream* st) {
  // print title
  st->print_cr("%s ", internal_name());
  obj->print_address_on(st);
  
  // print class
  st->print(" - klass: ");
  obj->klass()->print_value_on(st);
  st->cr();
}


void Klass::oop_print_value_on(oop obj, outputStream* st) {
  // print title
  st->print("%s", internal_name());
  obj->print_address_on(st);
}

#endif

// Verification

void Klass::oop_verify_on(oop obj, outputStream* st) {
  guarantee(obj->is_oop(),  "should be oop");  
  guarantee(obj->klass()->is_perm(),  "should be in permspace");
  guarantee(obj->klass()->is_klass(), "klass field is not a klass");
}

void Klass::oop_verify_old_oop(oop obj, objectRef* p, bool allow_dirty) {
  /* $$$ I think this functionality should be handled by verification of

  RememberedSet::verify_old_oop(obj, p, allow_dirty, false);

  the card table. */
}

#ifndef PRODUCT

void Klass::verify_vtable_index(int i) {
  assert(oop_is_instance() || oop_is_array(), "only instanceKlass and arrayKlass have vtables");
  if (oop_is_instance()) {
    assert(i>=0 && i<((instanceKlass*)this)->vtable_length()/vtableEntry::size(), "index out of bounds");
  } else {
    assert(i>=0 && i<((arrayKlass*)this)->vtable_length()/vtableEntry::size(), "index out of bounds");
  }
}

#endif

void ContendedStackEntry::add_blocked_ticks(int64_t val) {
Atomic::add_ptr(val,&_ticks);
  int64_t cur_max = _max_ticks;
  while (cur_max < val) {
    Atomic::cmpxchg_ptr(val, &_max_ticks, cur_max);
    cur_max = _max_ticks;
  }
}

void ContendedStackEntry::print_xml_on(xmlBuffer *xb) {
  int64_t freq = os::elapsed_frequency()/1000;
  xmlElement cs(xb, "contended_stack");
  xb->name_value_item("count", _count);
  xb->name_value_item("time", _ticks/freq);
for(int i=0;i<ProfileLockContentionDepth;i++){
    int mid = _stack[2*i];
    int bci = _stack[2*i +1];
    methodOop moop = methodOopDesc::from_method_id(mid);
if(moop!=NULL){
      xmlElement cse(xb, "contended_stack_element");
      moop->print_xml_on(xb, true);
if(!moop->is_native()){
        xmlElement xe(xb, "line_info", xmlElement::delayed_LF);
instanceKlass*klass=instanceKlass::cast(moop->method_holder());
        xb->print(klass->source_file_name() ? klass->source_file_name()->as_C_string() : "unknown source");
int line_number=moop->line_number_from_bci(bci);
        if (line_number != -1) {
          xb->print(":%d", line_number);
        }
      }
      xb->name_value_item("bci", bci);
    }
  }
}

ContendedStackEntry* Klass::add_contention_stack(int hash, int stack[]) {
  ContendedStackEntry *entry = _contentions;
  Atomic::membar(); // make sure a new entry has been completely written out. Racing additions to the list below...
while(entry!=NULL){
    bool found = true;
    if (hash == entry->_hash) {
      for (int i = 0; i < 2*ProfileLockContentionDepth; i++) {
        if (stack[i] != entry->_stack[i]) {
          found = false;
          break;
        }
      }
      if (found) {
        Atomic::inc_ptr(&entry->_count);
        return entry;
      }
    }
    entry = entry->_next;
  }
  
  // Search the existing contentions again while holding a lock.
  // Add new entry if not found, otherwise increment existing entry.
MutexLocker ml(ContendedStack_lock);
entry=_contentions;
while(entry!=NULL){
    bool found = true;
    if (hash == entry->_hash) {
      for (int i = 0; i < 2*ProfileLockContentionDepth; i++) {
        if (stack[i] != entry->_stack[i]) {
          found = false;
          break;
        }
      }
      if (found) {
        Atomic::inc_ptr(&entry->_count);
        return entry;
      }
    }
    entry = entry->_next;
  }
entry=new ContendedStackEntry();
  entry->_count = 1;
  entry->_ticks = 0;
  entry->_max_ticks = 0;
  entry->_hash  = hash;
  entry->_stack = (int*)malloc(2*sizeof(int)*ProfileLockContentionDepth);
  for (int i = 0; i < 2*ProfileLockContentionDepth; i++) {
    entry->_stack[i] = stack[i];
  }
  entry->_next = _contentions;
  Atomic::membar(); // Force the newly created entry into memory before making it visible.
_contentions=entry;
  
  return entry;
}


void Klass::oop_print_xml_on(oop obj, xmlBuffer *xb, bool ref) {
  if (ref) {
    xmlElement xe(xb, "object_ref");
    xb->name_value_item("id", xb->object_pool()->append_oop(obj));
    xb->name_value_item("name", "internal klass");
  } else {  
    oop_print_xml_on_as_object(obj, xb);
  }
}


void Klass::oop_print_xml_on_as_object(oop obj,xmlBuffer*xb){
  xb->name_value_item("error", "browsing is not enabled for this type of object");
}


class ContendedStackTreeNode:public ResourceObj{
 private:
ContendedStackTreeNode*_next;
  ContendedStackTreeNode *_child;
  
  int64_t _count;
  int64_t _ticks;
  int     _mid;
  int     _bci;
  
 public:
  ContendedStackTreeNode(int mid, int bci) {
    _next = _child = NULL;
    _count = 0;
    _ticks = 0;
_mid=mid;
    _bci   = bci;
  }
  
  ContendedStackTreeNode* child() {
    return _child;
  }
  
  ContendedStackTreeNode* find(int mid, int bci) {
ContendedStackTreeNode*cur=this;
    while ((cur != NULL) && ((cur->_mid != mid) || (cur->_bci != bci))) {
      cur = cur->_next;
    }
    return cur;
  }
  
  ContendedStackTreeNode* add_child(int mid, int bci) {
    ContendedStackTreeNode* new_node = new ContendedStackTreeNode(mid, bci);
    
    new_node->_next = _child;
    _child = new_node;
    
    return new_node;
  }
  
  ContendedStackTreeNode* add_sibling(int mid, int bci) {
    ContendedStackTreeNode* new_node = new ContendedStackTreeNode(mid, bci);

new_node->_next=_next;
_next=new_node;
    
    return new_node;
  }
  
  void update_count(int64_t val) {
    _count += val;
  }
  
  void update_ticks(int64_t val) {
    _ticks += val;
  }
  
  void print_xml_on(xmlBuffer *xb) {
    if (_mid != 0) {
      int64_t freq = os::elapsed_frequency()/1000;
      xmlElement xe(xb, "contended-stack-tree-node");
      xb->name_value_item("count", _count);
      xb->name_value_item("time", _ticks/freq);
      methodOop moop = methodOopDesc::from_method_id(_mid);
if(moop!=NULL){
        moop->print_xml_on(xb, true);
if(!moop->is_native()){
          { xmlElement xe(xb, "line_info", xmlElement::delayed_LF);
instanceKlass*klass=instanceKlass::cast(moop->method_holder());
            xb->print(klass->source_file_name() ? klass->source_file_name()->as_C_string() : "unknown source");
int line_number=moop->line_number_from_bci(_bci);
            if (line_number != -1) {
              xb->print(":%d", line_number);
            }
          }
          xb->name_value_item("bci", _bci);
        } else {
          xmlElement xe(xb, "line_info", xmlElement::delayed_LF);
xb->print("native method");
        }
      } else {
        xb->name_value_item("unknown-mid", _mid);
      }
if(_child!=NULL){
        _child->print_xml_on(xb);
      }
}else if(_child!=NULL){
      _child->print_xml_on(xb);
    }
if(_next!=NULL){
      _next->print_xml_on(xb);
    }
  }
};


void Klass::print_lock_xml_on(xmlBuffer *xb) {
  int64_t freq = os::elapsed_frequency()/1000;
  xmlElement ls(xb, "lock_stats");
  
  ContendedStackTreeNode *root = new ContendedStackTreeNode(-1,0);
  int64_t blocks    = 0;
  int64_t ticks     = 0;
  int64_t max_ticks = 0;
  
  for (ContendedStackEntry *cur = _contentions; cur != NULL; cur = cur->_next) {
    cur->print_xml_on(xb);
    
    blocks += cur->_count;
    ticks  += cur->_ticks;
    if (max_ticks < cur->_max_ticks) {
      max_ticks = cur->_max_ticks;
    }    
    
    ContendedStackTreeNode* parent = root;
    root->update_count(cur->_count);
    root->update_ticks(cur->_ticks);
for(int i=0;i<ProfileLockContentionDepth;i++){
      int mid = cur->_stack[2*i];
      int bci = cur->_stack[2*i + 1];
      ContendedStackTreeNode* node = parent->child();
if(node!=NULL){
        node = node->find(mid, bci);
      } 
if(node==NULL){
        node = parent->add_child(mid, bci);
      }
      node->update_count(cur->_count);
      node->update_ticks(cur->_ticks);
parent=node;
    }
  }
  
if(_contentions!=NULL){
    xmlElement xe(xb, "contended-stack-tree");
    root->print_xml_on(xb);
  }
  
  this->as_klassOop()->print_xml_on(xb, true);
  xb->name_value_item("klass_sma_successes", klass_sma_successes());
  xb->name_value_item("klass_sma_failures", klass_sma_failures());
  xb->name_value_item("contended_lock_attempts", blocks);
  xb->name_value_item("contended_max_time", max_ticks/freq);
  xb->name_value_item("contended_total_time", ticks/freq);
  xb->name_value_item("wait_count", _wait_count);
  xb->name_value_item("wait_max_time", _wait_max_ticks/freq);
  xb->name_value_item("wait_total_time", _wait_total_ticks/freq);
  xb->name_value_item("freq", freq);
  
}


// NOTE: Enabled in product mode, because contented locks mostly show
// up in large apps running product builds.
int Klass::gather_lock_contention( Klass *K, struct lock_contention_data *data, int cnt) {
  while( true ) {
    if (K->_contentions != NULL) {
      int64_t blocks    = 0;
      int64_t ticks     = 0;
      int64_t max_ticks = 0;
      for (ContendedStackEntry *cur = K->_contentions; cur != NULL; cur = cur->_next) {
        blocks += cur->_count;
        ticks  += cur->_ticks;
        if (max_ticks < cur->_max_ticks) {
          max_ticks = cur->_max_ticks;
        }
      }
      data[cnt]._blocks           = blocks;
      data[cnt]._cum_blocks       = 0;
      data[cnt]._max_ticks        = max_ticks;
      data[cnt]._total_ticks      = ticks;
      data[cnt]._wait_count       = K->_wait_count;
      data[cnt]._wait_max_ticks   = K->_wait_max_ticks;
      data[cnt]._wait_total_ticks = K->_wait_total_ticks;
      data[cnt]._kid              = K->_klassId;
      data[cnt]._name             = K->name() ? K->external_name() : "null";
      cnt++;
      if( cnt >= 1000 ) return cnt;
    }
Klass*sub_klass=K->subklass();
if(sub_klass!=NULL){
      cnt = gather_lock_contention(sub_klass, data, cnt);
    }
    K = K->next_sibling();
if(K==NULL)
      return cnt;
  }
}
