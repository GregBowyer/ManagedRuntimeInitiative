/*
 * Copyright 2005 Sun Microsystems, Inc.  All Rights Reserved.
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
#include "gpgc_collector.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "mutexLocker.hpp"
#include "resolutionErrors.hpp"
#include "safepoint.hpp"

#include "atomic_os_pd.inline.hpp"
#include "hashtable.inline.hpp"
#include "heapRef_pd.inline.hpp"
#include "objectRef_pd.inline.hpp"
#include "oop.inline.hpp"
#include "orderAccess_os_pd.inline.hpp"
#include "prefetch_os_pd.inline.hpp"

// add new entry to the table
void ResolutionErrorTable::add_entry(int index, unsigned int hash, 
				     constantPoolHandle pool, int cp_index, symbolHandle error)
{
  assert_locked_or_safepoint(SystemDictionary_lock);
  assert(!pool.is_null() && !error.is_null(), "adding NULL obj");

  ResolutionErrorEntry* entry = new_entry(hash, pool(), cp_index, error());
  add_entry(index, entry);
}

// find entry in the table
ResolutionErrorEntry* ResolutionErrorTable::find_entry(int index, unsigned int hash, 
						       constantPoolHandle pool, int cp_index)
{
  assert_locked_or_safepoint(SystemDictionary_lock);

  for (ResolutionErrorEntry *error_probe = bucket(index);
                         error_probe != NULL;
                         error_probe = error_probe->next()) {
  if (error_probe->hash() == hash && error_probe->pool() == pool()) {
      return error_probe;;
    }
  }
  return NULL;
}

// create new error entry
ResolutionErrorEntry* ResolutionErrorTable::new_entry(int hash, constantPoolRef pool, 
						      int cp_index, symbolRef error)
{   
ResolutionErrorEntry*entry=(ResolutionErrorEntry*)Hashtable::new_entry(hash,poison_constantPoolRef(pool));
  entry->set_cp_index(cp_index);
entry->set_error(poison_symbolRef(error));

  return entry;
}

// create resolution error table
ResolutionErrorTable::ResolutionErrorTable(int table_size)
    : Hashtable(table_size, sizeof(ResolutionErrorEntry)) {
}

// GC support
void ResolutionErrorTable::oops_do(OopClosure* f) {
  for (int i = 0; i < table_size(); i++) {
    for (ResolutionErrorEntry* probe = bucket(i); 
                           probe != NULL; 
                           probe = probe->next()) {
assert(probe->pool().not_null(),"resolution error table is corrupt");
assert(probe->error().not_null(),"resolution error table is corrupt");
      probe->oops_do(f);
    }
  }
}

// GC support
void ResolutionErrorEntry::oops_do(OopClosure* blk) {
blk->do_oop(pool_addr());
blk->do_oop(error_addr());
}

// We must keep the symbolOop used in the error alive. The constantPoolOop will
// decide when the entry can be purged.
void ResolutionErrorTable::always_strong_classes_do(OopClosure* blk) {
  for (int i = 0; i < table_size(); i++) {
    for (ResolutionErrorEntry* probe = bucket(i); 
                           probe != NULL; 
                           probe = probe->next()) {
      assert(probe->error() != (symbolOop)NULL, "resolution error table is corrupt");
blk->do_oop(probe->error_addr());
    }	
  }
}

// Remove unloaded entries from the table
void ResolutionErrorTable::purge_resolution_errors(BoolObjectClosure* is_alive) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")
  for (int i = 0; i < table_size(); i++) {
    for (ResolutionErrorEntry** p = bucket_addr(i); *p != NULL; ) {
      ResolutionErrorEntry* entry = *p;
      assert(entry->pool() != (constantPoolOop)NULL, "resolution error table is corrupt");
      constantPoolRef pool = entry->pool();
if(is_alive->do_object_b(pool.as_oop())){
	p = entry->next_addr();
      } else {
	*p = entry->next();
	free_entry(entry);
      }
    }
  }
}

// Remove unloaded entries from the table
void ResolutionErrorTable::GPGC_purge_resolution_errors_section(long section,
                                                                long sections) {
  assert(SafepointSynchronize::is_at_safepoint(), "must be at safepoint")

  assert0((section>=0) && (section<sections));

  long section_size = table_size() / sections;
  long section_start = section * section_size;
  long section_end = section_start + section_size;
  if ( (section+1) == sections ) {
    section_end = table_size();
  }

for(int i=section_start;i<section_end;i++){
    for (ResolutionErrorEntry** p = bucket_addr(i); *p != NULL; ) {
      ResolutionErrorEntry* entry = *p;
      assert(entry->pool() != (constantPoolOop)NULL, "resolution error table is corrupt");
      objectRef pool_ref = GPGC_Collector::perm_remapped_only((heapRef*)entry->pool_addr());

      if( pool_ref.not_null()
        && !GPGC_Marks::is_old_marked_strong_live(pool_ref.as_oop())
        && !GPGC_Marks::is_old_marked_final_live(pool_ref.as_oop()) )
      {
        *p = entry->next();
        free_entry(entry);
      } else {
        GPGC_OldCollector::mark_to_live(entry->pool_addr());
        assert0( GPGC_Marks::is_old_marked_strong_live(entry->error().as_oop())
          || GPGC_Marks::is_old_marked_final_live(entry->error().as_oop()) );

        p = entry->next_addr();
      }
    }
  }
}
