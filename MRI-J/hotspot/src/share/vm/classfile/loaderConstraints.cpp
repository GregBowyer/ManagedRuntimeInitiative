/*
 * Copyright 2003-2006 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "collectedHeap.hpp"
#include "dictionary.hpp"
#include "gpgc_collector.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "loaderConstraints.hpp"
#include "ostream.hpp"
#include "resourceArea.hpp"
#include "safepoint.hpp"
#include "systemDictionary.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "handles.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"

LoaderConstraintTable::LoaderConstraintTable(int nof_buckets)
  : Hashtable(nof_buckets, sizeof(LoaderConstraintEntry)) {};


LoaderConstraintEntry* LoaderConstraintTable::new_entry(
                                 unsigned int hash, symbolOop name,
                                 klassOop klass, int num_loaders,
                                 int max_loaders) {
  LoaderConstraintEntry* entry;
entry=(LoaderConstraintEntry*)Hashtable::new_entry(hash,POISON_KLASSREF(klassRef(klass)));
entry->set_name(POISON_SYMBOLREF(symbolRef(name)));
  entry->set_num_loaders(num_loaders);
  entry->set_max_loaders(max_loaders);
  return entry;
}


void LoaderConstraintTable::oops_do(OopClosure* f) {
  for (int index = 0; index < table_size(); index++) {
    for (LoaderConstraintEntry* probe = bucket(index);
                                probe != NULL;
                                probe = probe->next()) {
      f->do_oop((objectRef*)(probe->name_addr()));
if(probe->klass().not_null()){
        f->do_oop((objectRef*)probe->klass_addr());
      }
      for (int n = 0; n < probe->num_loaders(); n++) {
        if (probe->loader_addr(n)->not_null()) {
          f->do_oop(probe->loader_addr(n));
        }
      }
    }
  }
}

// We must keep the symbolOop used in the name alive.  We'll use the
// loaders to decide if a particular entry can be purged. 
void LoaderConstraintTable::always_strong_classes_do(OopClosure* blk) {
  // We must keep the symbolOop used in the name alive.
  for (int cindex = 0; cindex < table_size(); cindex++) {
    for (LoaderConstraintEntry* lc_probe = bucket(cindex);
                                lc_probe != NULL;
                                lc_probe = lc_probe->next()) {
assert(lc_probe->name().not_null(),"corrupted loader constraint table");
blk->do_oop(lc_probe->name_addr());
    }
  }
}


// The loaderConstraintTable must always be accessed with the
// SystemDictionary lock held. This is true even for readers as
// entries in the table could be being dynamically resized.

LoaderConstraintEntry** LoaderConstraintTable::find_loader_constraint(
                                    symbolHandle name, Handle loader) {

  unsigned int hash = compute_hash(name);
  int index = hash_to_index(hash);
  LoaderConstraintEntry** pp = bucket_addr(index);
LoaderConstraintEntry*p=*pp;//READ linked-list head
  Atomic::read_barrier();       // Force read to complete, before reading head interior
  objectRef l_ref = loader.as_ref();  // De-handlize once
  unsigned long lhash = l_ref.not_null() ? l_ref.as_oop()->identity_hash() : 0;
  while (p) {
if(p->hash()==hash&&
        p->name().as_symbolOop() == name()) {
      int num = p->num_loaders();
      Atomic::read_barrier();
      // Pre-load THEN do a READ-FENCE!
      objectRef    *loaders = p->_loaders;
      unsigned int *lhashes = p->_loader_hash_keys;
      // Cannot use accessor fcns in the loop here!  We must load all array
      // bases, then read-fence THEN we can scan the array contents.  If we
      // load both the array base and array contents without a fence we risk
      // getting stale data if a writer is busy copy/extending the array.
      Atomic::read_barrier();
      for( int i = 0; i < num; i++ )
        if (          lhashes[i]  == lhash &&
             lvb_ref(&loaders[i]) == l_ref )
          return pp;
    }
    pp = p->next_addr();
    p = *pp;                    // READ p->_next
  }
  return pp;
}


void LoaderConstraintTable::purge_loader_constraints(BoolObjectClosure* is_alive) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")
  // Remove unloaded entries from constraint table
  for (int index = 0; index < table_size(); index++) {
    LoaderConstraintEntry** p = bucket_addr(index);
    while(*p) {
      LoaderConstraintEntry* probe = *p;
klassOop klass=(probe->klass()).as_klassOop();
      // Remove klass that is no longer alive
      if (klass != NULL && !is_alive->do_object_b(klass)) {
probe->set_klass(klassRef((uint64_t)NULL));
	if (TraceLoaderConstraints) {
	  ResourceMark rm;
tty->print_cr("[Purging class object from constraint for name %s, loader list:",
                        probe->name().as_symbolOop()->as_C_string());
  	  for (int i = 0; i < probe->num_loaders(); i++) {
	    tty->print_cr("[   [%d]: %s", i, 
			  SystemDictionary::loader_name(probe->loader(i)));
	  }
	}
      }
      // Remove entries no longer alive from loader array
      int n = 0; 
      while (n < probe->num_loaders()) {
if(probe->loader(n).not_null()){
if(!is_alive->do_object_b((probe->loader(n)).as_oop())){
	    if (TraceLoaderConstraints) {
	      ResourceMark rm;
              tty->print_cr("[Purging loader %s from constraint for name %s",
			    SystemDictionary::loader_name(probe->loader(n)),
probe->name().as_symbolOop()->as_C_string()
			    );
	    }

            // Compact array
int num=probe->_num_loaders=probe->_num_loaders-1;
probe->_loaders[n]=probe->_loaders[num];
            probe->_loader_hash_keys[n] = probe->_loader_hash_keys[num];
            probe->_loaders         [num] = nullRef;
            probe->_loader_hash_keys[num] =       0;

	    if (TraceLoaderConstraints) {
	      ResourceMark rm;
              tty->print_cr("[New loader list:");
	      for (int i = 0; i < probe->num_loaders(); i++) {
                tty->print_cr("[   [%d]: %s", i, 
			      SystemDictionary::loader_name(probe->loader(i)));
	      }
	    }

            continue;  // current element replaced, so restart without
                       // incrementing n
          }
        }
        n++;
      }
      // Check whether entry should be purged
      if (probe->num_loaders() < 2) {
	    if (TraceLoaderConstraints) {
	      ResourceMark rm;
	      tty->print("[Purging complete constraint for name %s\n", 
probe->name().as_symbolOop()->as_C_string());
	    }

        // Purge entry
        *p = probe->next();
        FREE_C_HEAP_ARRAY(oop, probe->loaders());
        FREE_C_HEAP_ARRAY(unsigned int, probe->_loader_hash_keys);
        free_entry(probe);
      } else {
#ifdef ASSERT
assert(is_alive->do_object_b(probe->name().as_oop()),"name should be live");
if(probe->klass().not_null()){
assert(is_alive->do_object_b(probe->klass().as_oop()),"klass should be live");
        }
        for (n = 0; n < probe->num_loaders(); n++) {
if(probe->loader(n).not_null()){
assert(is_alive->do_object_b(probe->loader(n).as_oop()),"loader should be live");
          }
        }
#endif
        // Go to next entry
        p = probe->next_addr();
      }
    }
  }
}

void LoaderConstraintTable::GPGC_purge_loader_constraints_section(long section, long sections) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")

  assert0((section>=0) && (section<sections));

  long section_size = table_size() / sections;
  long section_start = section * section_size;
  long section_end = section_start + section_size;
  if ( (section+1) == sections ) {
    section_end = table_size();
  }

  // Remove unloaded entries from constraint table
for(int index=section_start;index<section_end;index++){
    LoaderConstraintEntry** p = bucket_addr(index);
    while(*p) {
      LoaderConstraintEntry* probe = *p;
      objectRef klass_ref = GPGC_Collector::perm_remapped_only((heapRef*)probe->klass_addr());
      // Remove klass that is no longer alive (ie unmarked)
      if( klass_ref.not_null()
        && !GPGC_Marks::is_old_marked_strong_live(klass_ref)
        && !GPGC_Marks::is_old_marked_final_live(klass_ref) )
      {
        probe->set_klass(klassRef((uint64_t)NULL));
      }
      // Remove entries no longer alive (ie unmarked) from loader array
      int n = 0; 
      while (n < probe->num_loaders()) {
        if (probe->GC_loader(n).not_null()) {
          objectRef loaderRef = GPGC_Collector::remap_only((heapRef*)probe->loader_addr(n));
          if( loaderRef.is_old()
            && !GPGC_Marks::is_old_marked_strong_live(loaderRef)
            && !GPGC_Marks::is_old_marked_final_live(loaderRef) )
          {
            // Compact array
            int num = probe->_num_loaders = probe->_num_loaders-1;
            probe->_loaders         [n]   = probe->_loaders         [num];
            probe->_loader_hash_keys[n]   = probe->_loader_hash_keys[num];
            probe->_loaders         [num] = nullRef;
            probe->_loader_hash_keys[num] =       0;


	    if (TraceLoaderConstraints) {
	      ResourceMark rm;
              tty->print_cr("[New loader list:");
	      for (int i = 0; i < probe->num_loaders(); i++) {
                tty->print_cr("[   [%d]: %s", i, 
			      SystemDictionary::loader_name(probe->loader(i)));
	      }
	    }

            continue;  // current element replaced, so restart without 
                       // incrementing n
          }
        }
        n++;
      }
      // Check whether entry should be purged
      if (probe->num_loaders() < 2) {
        // Purge entry
        *p = probe->next();
        FREE_C_HEAP_ARRAY(oop, probe->loaders());
        FREE_C_HEAP_ARRAY(unsigned int, probe->_loader_hash_keys);
        free_entry(probe);
      } else {
        GPGC_OldCollector::mark_to_live(probe->klass_addr());
        GPGC_OldCollector::mark_to_live(probe->name_addr());
        for ( n=0; n<probe->num_loaders(); n++ ) {
          GPGC_OldCollector::mark_to_live_or_new((heapRef*)probe->loader_addr(n));
        }
        // Go to next entry
        p = probe->next_addr();
      }
    }
  }
}

bool LoaderConstraintTable::add_entry(symbolHandle class_name,
                                      klassOop klass1, Handle class_loader1,
                                      klassOop klass2, Handle class_loader2) {
klassOop klass=NULL;//Find a class, any class
  if( klass1 ) klass = klass1;
  if( klass2 ) klass = klass2;

  LoaderConstraintEntry** pp1 = find_loader_constraint(class_name, 
                                                       class_loader1);
  LoaderConstraintEntry *p1 = *pp1; // READ it once, but under lock so no race
klassOop klass3=NULL;
  if( p1 ) klass3 = p1->klass().as_klassOop();
  if( klass3 ) klass = klass3;

  LoaderConstraintEntry** pp2 = find_loader_constraint(class_name, 
                                                       class_loader2);
  LoaderConstraintEntry *p2 = *pp2; // READ it once, but under lock so no race
klassOop klass4=NULL;
  if( p2 ) klass4 = p2->klass().as_klassOop();
  if( klass4 ) klass = klass4;

  // If we found any class, all of them better agree
  if( klass ) {
    if( klass1 && klass != klass1 ) return false;
    if( klass2 && klass != klass2 ) return false;
    if( klass3 && klass != klass3 ) return false;
    if( klass4 && klass != klass4 ) return false;
  }
  
if(p1==NULL&&p2==NULL){
    unsigned int hash = compute_hash(class_name);
    int index = hash_to_index(hash);
    LoaderConstraintEntry* p;
p=new_entry(hash,class_name(),klass,2,4);
p->set_loaders(NEW_C_HEAP_ARRAY(objectRef,4));
    p->_loader_hash_keys = NEW_C_HEAP_ARRAY(unsigned int, 4);
    p->set_name(POISON_SYMBOLREF(symbolRef(class_name())));
    p->set_loader(0, ALWAYS_POISON_OBJECTREF(class_loader1.as_ref()));
    p->_loader_hash_keys[0] = class_loader1.is_null() ? 0 : class_loader1()->identity_hash();
    p->set_loader(1, ALWAYS_POISON_OBJECTREF(class_loader2.as_ref()));
    p->_loader_hash_keys[1] = class_loader2.is_null() ? 0 : class_loader2()->identity_hash();
    p->set_loader(2, nullRef);
    p->set_loader(3, nullRef);

    p->set_klass(POISON_KLASSREF(klassRef(klass)));
    p->set_next(bucket(index));
    Atomic::write_barrier(); // Force 'p' contents out before publishing 'p'
    set_entry(index, p);
}else if(p1==p2){//constraint already imposed
    bool chk = p1->tst_set_klass(POISON_KLASSREF(klassRef(klass)));
assert(chk,"constraints corrupted");
}else if(p1==NULL){
    extend_loader_constraint(p2, class_loader1, klass);
}else if(p2==NULL){
    extend_loader_constraint(p1, class_loader2, klass);
  } else {
    merge_loader_constraints(pp1, pp2, klass);
  }
  return true;                  // All is OK
}


// return true if the constraint was updated, false if the constraint is
// violated
bool LoaderConstraintTable::check_or_update(instanceKlassHandle k,
                                                   Handle loader,
                                                   symbolHandle name) {
  LoaderConstraintEntry* p = *(find_loader_constraint(name, loader));
  if (p && !p->tst_set_klass(POISON_KLASSREF(klassRef(k()))) ) {
    if (TraceLoaderConstraints) {
      ResourceMark rm;
      tty->print("[Constraint check failed for name %s, loader %s: "
		 "the presented class object differs from that stored ]\n",
		 name()->as_C_string(), 
		 SystemDictionary::loader_name(loader()));
    }
    return false;
  } else {
    return true;
  }
}

klassOop LoaderConstraintTable::find_constrained_klass(symbolHandle name,
                                                       Handle loader) {
  LoaderConstraintEntry *p = *(find_loader_constraint(name, loader));
if(p!=NULL&&p->klass().not_null())
return p->klass().as_klassOop();

  // No constraints, or else no klass loaded yet.
  return NULL;
}


klassOop LoaderConstraintTable::find_constrained_elem_klass(symbolHandle name,
                                                            symbolHandle elem_name,
                                                            Handle loader,
                                                            TRAPS) {
  LoaderConstraintEntry *p = *(find_loader_constraint(name, loader));
  if (p != NULL) {
assert(p->klass().is_null(),"Expecting null array klass");

    // The array name has a constraint, but it will not have a class. Check
    // each loader for an associated elem
    for (int i = 0; i < p->num_loaders(); i++) {
      Handle no_protection_domain;

      klassOop k = SystemDictionary::find(elem_name, p->loader(i), no_protection_domain, THREAD);
      if (k != NULL) {
        // Return the first elem klass found.
        return k;
      }
    }
  }

  // No constraints, or else no klass loaded yet.
  return NULL;
}


void LoaderConstraintTable::ensure_loader_constraint_capacity(
                                                     LoaderConstraintEntry *p,
                                                    int nfree) {
assert_lock_strong(SystemDictionary_lock);
  if (p->max_loaders() - p->num_loaders() < nfree) {
    int n = MAX2(nfree + p->num_loaders(), p->num_loaders() * 2);
    objectRef* new_loaders = NEW_C_HEAP_ARRAY(objectRef, n);
    unsigned int* new_keys = NEW_C_HEAP_ARRAY(unsigned int, n);
    for (int i = 0; i < p->num_loaders(); i++) {
      POISON_AND_STORE_REF( &new_loaders[i], p->loader(i) );
      new_keys[i] = p->_loader_hash_keys[i];
    }
    // Racing reader threads are walking through the old arrays using the old
    // _num_loaders.  The new arrays have the old arrays as a prefix.  Cannot
    // install new arrays until we fence out their contents, lest a racing
    // reader using the old _num_loaders fetches into a new array (and expects
    // to see valid contents).
    Atomic::write_barrier();    // Must barrier before installing the new array
    p->set_loaders(new_loaders);
    p->_loader_hash_keys = new_keys;
    // No need to fence here, as _max_loaders is only ever read or written
    // while holding the SystemDictionary lock.
    p->set_max_loaders(n);
    //static intptr_t leaked=0;
    //tty->print_cr("LEAK IS UP TO %d bytes",Atomic::add_ptr(p->_max_loaders*wordSize,&leaked));
    // Cannot free the old C heap until we know there aren't any racing
    // Readers - i.e. at the next Safepoint.  Since tracking them would be
    // annoying I've decided to Leak these arrays.    CNC - 5/3/2005
    // FREE_C_HEAP_ARRAY(objectRef, p->_loaders);
  }

}
 

void LoaderConstraintTable::extend_loader_constraint(LoaderConstraintEntry* p,
                                                     Handle loader,
                                                     klassOop klass) {
  ensure_loader_constraint_capacity(p, 1);
  int num = p->num_loaders();
p->set_loader(num,ALWAYS_POISON_OBJECTREF(objectRef(loader())));
  p->_loader_hash_keys[num] = loader.is_null() ? 0 : loader()->identity_hash();
  Atomic::write_barrier();      // Must fence out good data before increasing count
  p->set_num_loaders(num + 1);
  if (TraceLoaderConstraints) {
    ResourceMark rm;
    tty->print("[Extending constraint for name %s by adding loader[%d]: %s %s",
               UNPOISON_SYMBOLREF(p->name()).as_symbolOop()->as_C_string(),
	       num,
               SystemDictionary::loader_name(loader()),
(p->klass().is_null()?" and setting class object ]\n":" ]\n")
	       );
  }
  bool chk = p->tst_set_klass(POISON_KLASSREF(klassRef(klass)));
assert(chk,"constraints corrupted");
}


void LoaderConstraintTable::merge_loader_constraints(
                                                   LoaderConstraintEntry** pp1,
                                                   LoaderConstraintEntry** pp2,
                                                   klassOop klass) {
assert_lock_strong(SystemDictionary_lock);

  // make sure *pp1 has higher capacity 
  if ((*pp1)->max_loaders() < (*pp2)->max_loaders()) {
    LoaderConstraintEntry** tmp = pp2;
    pp2 = pp1;
    pp1 = tmp;
  }
  
  LoaderConstraintEntry* p1 = *pp1;
  LoaderConstraintEntry* p2 = *pp2;
  
  ensure_loader_constraint_capacity(p1, p2->num_loaders());

  int num = p1->num_loaders();
  for (int i = 0; i < p2->num_loaders(); i++) {
    objectRef p2_loader = p2->loader(i);
    p1->set_loader(num, ALWAYS_POISON_OBJECTREF(p2_loader));
    p1->_loader_hash_keys[num] = p2_loader.is_null() ? 0 : p2_loader.as_oop()->identity_hash();
    num++;
  }
  Atomic::write_barrier();      // Must fence out good data before increasing count
p1->set_num_loaders(num);
  
  if (TraceLoaderConstraints) {
    ResourceMark rm;
    tty->print_cr("[Merged constraints for name %s, new loader list:", 
                  UNPOISON_SYMBOLREF(p1->name()).as_symbolOop()->as_C_string()
		  );
  
    for (int i = 0; i < p1->num_loaders(); i++) {
      tty->print_cr("[   [%d]: %s", i, 
		    SystemDictionary::loader_name(p1->loader(i)));
    }
if(p1->klass().is_null()){
      tty->print_cr("[... and setting class object]");
    }
  }
  
  // p1->klass() will hold NULL if klass, p2->klass(), and old
  // p1->klass() are all NULL.  In addition, all three must have
  // matching non-NULL values, otherwise either the constraints would
  // have been violated, or the constraints had been corrupted (and an
  // assertion would fail).
assert(p2->klass().is_null()||p2->klass().as_klassOop()==klass,"constraints corrupted");

  bool chk = p1->tst_set_klass(POISON_KLASSREF(klassRef(klass)));
assert(chk,"constraints corrupted");

  // Force the LoaderConstraintEntry to be empty for future Readers.  Existing
  // readers will already have loaded p2->_num_loaders and can legally &
  // safely read the _loaders array.  'p2' will be purged at the next
  // Safepoint by the usual class unloading code (which simply inspects for
  // _num_loaders to be small).  If another add_loader_constraint call comes
  // along it might reuse this LCEntry & start bumping _num_loaders up again -
  // but the backing _loaders array will always be safe.
  p2->set_num_loaders(0);         // Force it empty for Readers
                                // Write will be fenced out after Unlock

  // FIXME - Don't we have to delete as well?
  // CNC - Cannot delete these, as racing readers still exist
  // FREE_C_HEAP_ARRAY(oop, p2->loaders());
  // free_entry(p2);
}


void LoaderConstraintTable::verify(Dictionary* dictionary) {
  Thread *thread = Thread::current();
  for (int cindex = 0; cindex < _loader_constraint_size; cindex++) {
    for (LoaderConstraintEntry* probe = bucket(cindex);
                                probe != NULL;
                                probe = probe->next()) {
guarantee(probe->name().as_symbolOop()->is_symbol(),"should be symbol");
if(probe->klass().not_null()){
instanceKlass*ik=instanceKlass::cast(probe->klass().as_klassOop());
guarantee(ik->name()==probe->name().as_symbolOop(),"name should match");
        symbolHandle name (thread, ik->name());
        Handle loader(thread, ik->class_loader());
        unsigned int d_hash = dictionary->compute_hash(name, loader);
        int d_index = dictionary->hash_to_index(d_hash);
        klassOop k = dictionary->find_class(d_index, d_hash, name, loader);
guarantee(k==probe->klass().as_klassOop(),"klass should be in dictionary");
      }
int num=probe->num_loaders();
      Atomic::read_barrier();
for(int n=0;n<num;n++){
        guarantee(probe->loader(n).as_oop()->is_oop_or_null(), "should be oop");
      }
    }
  }
}

#ifndef PRODUCT

// Called with the system dictionary lock held
void LoaderConstraintTable::print(outputStream*str)const{
  ResourceMark rm;

  assert_locked_or_safepoint(SystemDictionary_lock);
str->print_cr("Java loader constraints (entries=%d)",_loader_constraint_size);
  for (int cindex = 0; cindex < _loader_constraint_size; cindex++) {
    for (LoaderConstraintEntry* probe = bucket(cindex);
                                probe != NULL;
                                probe = probe->next()) {
str->print("%4d: ",cindex);
      UNPOISON_SYMBOLREF(probe->name()).as_symbolOop()->print(str);
str->print(" , loaders:");
      for (int n = 0; n < probe->num_loaders(); n++) {
        probe->loader(n).as_oop()->print_value_on(tty);
str->print(", ");
      }
str->cr();
    }
  }
}

void LoaderConstraintEntry::print(outputStream*str)const{
  str->print(" %p ",klass().as_klassOop());
  name().as_symbolOop()->print_symbol_on(str);
str->print(" :");
for(int i=0;i<num_loaders();i++){
    objectRef ldr = loader(i);
    str->print(" %s",SystemDictionary::loader_name(ldr.as_oop()));
  }
str->cr();
}

#endif
