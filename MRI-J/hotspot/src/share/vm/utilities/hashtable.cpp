/*
 * Copyright 2003-2005 Sun Microsystems, Inc.  All Rights Reserved.
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


#include "gpgc_collector.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_nmt.hpp"
#include "hashtable.hpp"
#include "safepoint.hpp"
#include "resourceArea.hpp"
#include "ostream.hpp"

#include "allocation.inline.hpp"
#include "atomic_os_pd.inline.hpp"
#include "hashtable.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

// This is a generic hashtable, designed to be used for the symbol
// and string tables.
//
// It is implemented as an open hash table with a fixed number of buckets.
//
// %note:
//  - HashtableEntrys are allocated in blocks to reduce the space overhead.

const jlong DeadRef = -1;

BasicHashtableEntry* BasicHashtable::new_entry(unsigned int hashValue) {
  BasicHashtableEntry* entry;
  
  if (_free_list) {
    entry = _free_list;
    _free_list = _free_list->next();
  } else {
    const int block_size = 500;
    if (_first_free_entry == _end_block) {
      int len = _entry_size * block_size;
      _first_free_entry = NEW_C_HEAP_ARRAY(char, len);
      _end_block = _first_free_entry + len;
    }
    entry = (BasicHashtableEntry*)_first_free_entry;
    _first_free_entry += _entry_size;
  }

  entry->set_hash(hashValue);
  return entry;
}


HashtableEntry*Hashtable::new_entry(unsigned int hashValue,objectRef obj_ref){
  HashtableEntry* entry;

  entry = (HashtableEntry*)BasicHashtable::new_entry(hashValue);
entry->set_literal(obj_ref);//clears literal string field
  return entry;
}


void BasicHashtable::decrement_number_of_entries(int delta) {
  Atomic::add_ptr((jint)delta, &_number_of_entries);
}


void BasicHashtable::append_free_entry(BasicHashtableEntry* entry) {
entry->set_next(_free_list);
_free_list=entry;
}


// GC support

void Hashtable::unlink(BoolObjectClosure* is_alive) {
  // Readers of the table are unlocked, so we should only be removing
  // entries at a safepoint.
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint");
  for (int i = 0; i < table_size(); ++i) {
    for (HashtableEntry** p = bucket_addr(i); *p != NULL; ) {
      HashtableEntry* entry = *p;
      if (entry->is_shared()) {
        break;
      }
assert(entry->literal().not_null(),"just checking");
if(is_alive->do_object_b(entry->literal().as_oop())){
        p = entry->next_addr();
      } else {
        *p = entry->next();
        free_entry(entry);
      }
    }
  }
}


// Scan one section of the SymbolTable hash table, and do a concurrent lockless
// unlink of SymbolOops that aren't live.  See the comment in symbolTable.hpp for
// a discussion of the concurrent lockless algorithm.
void WeakHashtable::GPGC_unlink_section(long section, long sections,
                                        void (*conditional_mark)   (oop obj),
                                        void (*guaranteed_mark)    (oop obj),
                                        void (*fully_mark_live_obj)(oop obj))
{
  jlong StrongRef = GPGC_NMT::desired_old_nmt_flag();
  jlong WeakRef   = 1 - StrongRef;

  assert0((section >= 0) && (section < sections));

  long section_size  = table_size() / sections;
  long section_start = section * section_size;
  long section_end   = section_start + section_size;
  if ( (section + 1) == sections ) {
    section_end = table_size();
  }

  WeakHashtableEntry* local_free_list_head = NULL;
  WeakHashtableEntry* local_free_list_tail = NULL;
  long                local_free_count     = 0;

  for (int i = section_start; i <= section_end - 1; i++) {
WeakHashtableEntry**p=bucket_addr(i);
WeakHashtableEntry*entry;

    // READ(*p)
    while ( NULL != (entry = *p) ) {
      // The literal in this hash node might be in a page that's been relocated.

      // READ/WRITE(entry->_literal) through OPAQUE CALL :
      heapRef ref      = GPGC_Collector::old_remap_nmt_and_rewrite(entry->literal_addr());
oop obj=ref.as_oop();
      // READ (entry->_strength) :
      jlong   strength = entry->strength();

      assert0(ref.is_old());

      if ( strength == StrongRef ) {
        // If the symbol is StrongRef, then a mutator must have looked it up and
        // strengthened it.  It might or might not be marked live, as it might or
        // might not have a ref to it in some other object that the collector found.
        // If it's dead, it gets marked live and the page pop stats updated.
        (*conditional_mark)(obj);

        p = entry->next_addr();
        continue;
      }

      // The collector converts WeakRefs to either DeadRef or a StrongRef.
      assert0( strength == WeakRef );

      // READ(mark_bits(symbol)) :
      if ( GPGC_Marks::is_old_marked_strong_live(ref) ) {
        // Upgrade a WeakRef entry to a StrongRef.
        // WRITE(entry->_strength)
        entry->set_strength(StrongRef);

if(fully_mark_live_obj!=NULL){
          // Just because the mutator marked the object live doesn't mean that the object doesn't have
          // as-yet-unmarked references to other objects.
          (*fully_mark_live_obj)(obj);
        }

        p = entry->next_addr();
        continue;
      }

      // A WeakRef entry that's not marked live should be downgraded to a DeadRef.
      // CAS(entry->_strength) :
      jlong str_result = Atomic::cmpxchg(DeadRef, entry->strength_addr(), WeakRef);

      if ( str_result == StrongRef ) {
        // CAS failed, a mutator beat us to it and made it a StrongRef. 
        // They also may have marked it ahead of us. We mark the symbol
        // live, and move on.
        (*conditional_mark)(obj);

        p = entry->next_addr();
        continue;
      }

      assert0( str_result == WeakRef );
      assert0( entry->strength() == DeadRef );

      // CAS succeeded, entry set to DeadRef, now must unlink entry from the hash chain.

      // Before unlinking the WeakHashtableEntry, we add it to our local list of uneeded entries.
      // After we're done with the section, the local list is CAS'ed onto the global list of
      // WeakHashtableEntrys pending deallocation list.  After a safepoint/checkpoint has passed,
      // the pending_deallocation list can be transferred to the free_list for future hashtable
      // inserts.
if(local_free_list_tail==NULL){
local_free_list_tail=entry;
      }
      entry->set_next_free(local_free_list_head);
local_free_list_head=entry;
      local_free_count ++;

      // READ(entry->_next) :
WeakHashtableEntry*new_next=entry->next();

      if ( p != bucket_addr(i) ) {
        // Unlinking an entry in the middle of a hash chain is easy, just set the
        // prior entry's _next to the current entry's _next, and we're done.  No CAS needed,
        // as mutators only ever set the hash chain head, so there's no potential race. 
        assert0( *p == entry );
        // WRITE(*p) :
*p=new_next;
        // p doesn't get advanced, as it's now pointing at a new node that must be examined.
        continue;
      }

      // Unlinking the entry at the start of the hash chain is a bit more complex,
      // as we're racing with mutators adding new entrys onto the hash chain.

      // CAS(*p) :
      intptr_t result = Atomic::cmpxchg_ptr((intptr_t) new_next, (intptr_t*) p, (intptr_t) entry);

      if ( result != (intptr_t) entry ) {
        // We failed the CAS.  There must be a new entry on the front of hash chain.
        // We have to go down the hash chain looking for the entry prior to the one
        // we're deleting, and then the unlink can be performed with a simple store.
        assert0(p == bucket_addr(i));

        WeakHashtableEntry* p_entry = *p;
assert(p_entry!=entry,"We shouldn't still be on the front of the hash chain");
        do {
p=p_entry->next_addr();
          assert0(p != NULL);
          // READ(*p) :
p_entry=*p;
        } while ( p_entry != entry );

        // Found our place in the hash chain, now unlink the object. 
        // WRITE(*p) :
*p=new_next;
      }

      // p doesn't get advanced, as it's now pointing at a new node that must be examined.
    }
  }

if(local_free_list_head!=NULL){
    assert0(local_free_count!=0);

    // Now that we're done with this section of the Hashtable, CAS our local list of unused
    // entries onto the global pending_deallocation list.

    WeakHashtableEntry* old_global_pending_head;
    intptr_t            cas_result;

    do {
      old_global_pending_head = _pending_free_head;
      local_free_list_tail->set_next_free(old_global_pending_head);

      cas_result = Atomic::cmpxchg_ptr((intptr_t)local_free_list_head,
                                       (intptr_t*)&_pending_free_head,
                                       (intptr_t)old_global_pending_head);
    } while ( cas_result != (intptr_t)old_global_pending_head );

    // If the CAS succeeded with a NULL global list head, then we set the pending_free_tail also.
if(old_global_pending_head==NULL){
      _pending_free_tail = local_free_list_tail;
    }

    // Also decrement the number of entries in the hash table.
    decrement_number_of_entries(local_free_count);
  }
}


void WeakHashtable::GC_release_pending_free() {
while(_pending_free_head!=NULL){
    HashtableEntry* entry = _pending_free_head;
    _pending_free_head = _pending_free_head->get_next_free();
append_free_entry(entry);
  }

  _pending_free_tail = _pending_free_head = NULL;
}


void Hashtable::oops_do(OopClosure* f) {
  for (int i = 0; i < table_size(); ++i) {
    HashtableEntry** p = bucket_addr(i);
    HashtableEntry* entry = bucket(i);
    while (entry != NULL) {
      f->do_oop(entry->literal_addr());

      // Did the closure remove the literal from the table?
if(entry->literal().is_null()){
        assert(!entry->is_shared(), "immutable hashtable entry?");
        *p = entry->next();
        free_entry(entry);
      } else {
        p = entry->next_addr();
      }
      entry = (HashtableEntry*)HashtableEntry::make_ptr(*p);
    }
  }
}


// Reverse the order of elements in the hash buckets.

void BasicHashtable::reverse() {

  for (int i = 0; i < _table_size; ++i) {
    BasicHashtableEntry* new_list = NULL;
    BasicHashtableEntry* p = bucket(i);
    while (p != NULL) {
      BasicHashtableEntry* next = p->next();
      p->set_next(new_list);
      new_list = p;
      p = next;
    }
    *bucket_addr(i) = new_list;
  }
}


// Copy the table to the shared space.

void BasicHashtable::copy_table(char** top, char* end) {

  // Dump the hash table entries.

  intptr_t *plen = (intptr_t*)(*top);
  *top += sizeof(*plen);

  int i;
  for (i = 0; i < _table_size; ++i) {
    for (BasicHashtableEntry** p = _buckets[i].entry_addr();
                              *p != NULL;
                               p = (*p)->next_addr()) {
      if (*top + entry_size() > end) {
        warning("\nThe shared miscellaneous data space is not large "
                "enough to \npreload requested classes.  Use "
                "-XX:SharedMiscDataSize= to increase \nthe initial "
                "size of the miscellaneous data space.\n");
        exit(2);
      }
      *p = (BasicHashtableEntry*)memcpy(*top, *p, entry_size());
      *top += entry_size();
    }
  }
  *plen = (char*)(*top) - (char*)plen - sizeof(*plen);

  // Set the shared bit.

  for (i = 0; i < _table_size; ++i) {
    for (BasicHashtableEntry* p = bucket(i); p != NULL; p = p->next()) {
      p->set_shared();
    }
  }
}



// Reverse the order of elements in the hash buckets.

void Hashtable::reverse(void* boundary) {

  for (int i = 0; i < table_size(); ++i) {
    HashtableEntry* high_list = NULL;
    HashtableEntry* low_list = NULL;
    HashtableEntry* last_low_entry = NULL;
    HashtableEntry* p = bucket(i);
    while (p != NULL) {
      HashtableEntry* next = p->next();
if(p->literal().as_oop()>=boundary){
        p->set_next(high_list);
        high_list = p;
      } else {
        p->set_next(low_list);
        low_list = p;
        if (last_low_entry == NULL) {
          last_low_entry = p;
        }
      }
      p = next;
    }
    if (low_list != NULL) {
      *bucket_addr(i) = low_list;
      last_low_entry->set_next(high_list);
    } else {
      *bucket_addr(i) = high_list;
    }
  }
}


// Dump the hash table buckets.

void BasicHashtable::copy_buckets(char** top, char* end) {
  intptr_t len = _table_size * sizeof(HashtableBucket);
  *(intptr_t*)(*top) = len;
  *top += sizeof(intptr_t);

  *(intptr_t*)(*top) = _number_of_entries;
  *top += sizeof(intptr_t);

  if (*top + len > end) {
    warning("\nThe shared miscellaneous data space is not large "
            "enough to \npreload requested classes.  Use "
            "-XX:SharedMiscDataSize= to increase \nthe initial "
            "size of the miscellaneous data space.\n");
    exit(2);
  }
  _buckets = (HashtableBucket*)memcpy(*top, _buckets, len);
  *top += len;
}


#ifndef PRODUCT

void Hashtable::print() {  
  ResourceMark rm;

  for (int i = 0; i < table_size(); i++) {
    HashtableEntry* entry = bucket(i);
    while(entry != NULL) {
      tty->print("%d : ", i);
entry->literal().as_oop()->print(tty);
      tty->cr();
      entry = entry->next();
    }
  }
}


void BasicHashtable::verify() {
  int count = 0;
  for (int i = 0; i < table_size(); i++) {
    for (BasicHashtableEntry* p = bucket(i); p != NULL; p = p->next()) {
      ++count;
    }
  }
  assert(count == number_of_entries(), "number of hashtable entries incorrect");
}


#endif // PRODUCT


#ifdef ASSERT

void BasicHashtable::verify_lookup_length(double load) {
  if ((double)_lookup_length / (double)_lookup_count > load * 2.0) {
    warning("Performance bug: SystemDictionary lookup_count=%d "
            "lookup_length=%d average=%lf load=%f", 
            _lookup_count, _lookup_length,
            (double) _lookup_length / _lookup_count, load);
  }
}

#endif

WeakHashtableEntry::EnsuredStrength WeakHashtableEntry::ensure_strong_ref() {
  // READ(this->_strength) :
  jlong strength  = this->strength();
  // Collectors besides GPGC don't concurrently collect here, and use a constant
  // value of 0 for StrongRef.
  jlong StrongRef = UseGenPauselessGC ? GPGC_NMT::desired_old_nmt_flag() : 0;
  jlong WeakRef   = 1 - StrongRef;

  if (strength != StrongRef) {
    if (strength == DeadRef) return IsDead;

    // stringTableEntry is WeakRef, try to strengthen it.
    assert(strength == WeakRef, "strength ought to be WeakRef here");

    jlong* strength_addr = this->strength_addr();

    // CAS(this->_strength) :
    jlong result = Atomic::cmpxchg(StrongRef, strength_addr, WeakRef);

if(result==DeadRef){
      // Someone else updated strength to DeadRef, pretend we didn't see this string.
      return IsDead;
    }

    assert0(result == WeakRef || result == StrongRef);

    // If result==WeakRef, we successfully updated to StrongRef.  If result==StrongRef,
    // someone else updated to StrongRef.  Either way, the string is StrongRef, so
    // we're free to use it.
if(result==WeakRef){
      return MustMark;
    } else {
      return IsStrong;
    }
  }
  return IsStrong;
}
