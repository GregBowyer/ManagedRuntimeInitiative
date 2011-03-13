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


#include "invertedVirtualspace.hpp"
#include "gpgc_readTrapArray.hpp"
#include "gpgc_marks.hpp"
#include "gpgc_oldCollector.hpp"
#include "jniHandles.hpp"
#include "mutexLocker.hpp"
#include "nmt.hpp"
#include "oopTable.hpp"

#include "prefetch_os_pd.inline.hpp"
#include "stackRef_pd.inline.hpp"
#include "oop.inline.hpp"

// Global reference to constant table.
CodeCacheOopTable* CodeCacheOopTable::_ccot;


// Constructor.
CodeCacheOopTable::CodeCacheOopTable(InvertedVirtualSpace *vs) {
_vs=vs;
  _table_lock = &CodeCacheOopTable_lock;
  _table = ((objectRef*) vs->high());
  _grows_down = true;
  _growth_size_in_bytes = ReservedSpace::page_align_size_up(1*M);
  _top_committed_index = 0;
  _top_table_index = 0x400;
  _top_dispensed_index = 0x1000;
  _max_index = vs->reserved_size() >> LogBytesPerWord;
  assert(_vs->committed_size() == 0, "vs should be unused");
}


// Initialize the global constants table.
void CodeCacheOopTable::init(InvertedVirtualSpace *vs) {
  _ccot = new CodeCacheOopTable(vs);
  // start out with some memory
  _ccot->grow();
}


// Override behavior of OopTable to intercept klasses.
intptr_t CodeCacheOopTable::put(objectRef r) {
 oop o = r.as_oop();
  if (o->is_klass()) {
    return Klass::cast((klassOop)o)->klassId();
  } else {
    return OopTable::put(r);
  }
}


// Recast oop handle to reference before adding to table.
int CodeCacheOopTable::putOop(jobject h){
  objectRef r = JNIHandles::resolve_as_ref(h);
return putOop(r);
}


// Global add to either klass or constants table.
int CodeCacheOopTable::putOop(objectRef r) {
assert(r.not_null(),"use putBlankOop for inserting null objectRefs");
  assert0(NMT::has_desired_nmt(r));
  return _ccot->put(r);
}


// Global fetch of either klass (positive) or constant (negative.)
objectRef CodeCacheOopTable::getOopAt(int i) {
  return _ccot->get(i);
}


// GC

void CodeCacheOopTable::oops_do(OopClosure*f){
  _ccot->oops_do_impl(f);
}

void CodeCacheOopTable::unlink(BoolObjectClosure*is_alive){
  _ccot->unlink_impl(is_alive);
}

void CodeCacheOopTable::GPGC_unlink(GPGC_GCManagerNewStrong* gcm, long _section, long _sections) {
  _ccot->GPGC_unlink_impl(gcm, _section, _sections);
}

void CodeCacheOopTable::GPGC_unlink(GPGC_GCManagerOldStrong* gcm, long _section, long _sections) {
  _ccot->GPGC_unlink_impl(gcm, _section, _sections);
}

void CodeCacheOopTable::GPGC_unlink_impl(GPGC_GCManagerNewStrong* gcm, long _section, long _sections) {
  // This code wasn't used before, and wasn't right.  It's even less right now, because it needs to
  // handle concurrent unlinking and free list creation.
  Unimplemented();
}

void CodeCacheOopTable::GPGC_unlink_impl(GPGC_GCManagerOldStrong* gcm, long _section, long _sections) {
assert(_table!=NULL,"CodeCacheOopTable not initialized");

  // TODO: striping really ought to be by cache-line.
  for ( intptr_t lo = lowestUsedIndex(), hi = highestUsedIndex(), i = hi - _section; i >= lo; i = i - _sections ) {
objectRef*addr=getAddr(i);
    objectRef  ref  = ALWAYS_UNPOISON_OBJECTREF(*addr);
    
if(ref.not_null()){
      if ( ref.is_new() ) {
Unimplemented();//I think I'm doing the right thing here, but the compilers don't
                          // put NewGen refs in the CodeCacheOopTable, so I can't test this out.
                          // If you hit this Unimplemented(), talk to me.
                          // 4/21/06 - Michael Wolf

                          // 6/26/06 - MAW: See the note above in the NewGen unlink function.
        // Ignore NewGen refs, they're handled by the NewCollector.
        continue;
      }

      assert0(ref.is_old());
      // TODO: maw: check NMT state, as in PauselessLVB::remapped_only()?
      if (GPGC_ReadTrapArray::is_remap_trapped(ref)) {
        assert0(GPGC_ReadTrapArray::is_old_gc_remap_trapped(ref));
        ref = GPGC_Collector::get_forwarded_object(heapRef(ref));
      }

      assert(ref.as_oop()->is_oop(), "not oop"); // Is this safe, even if the oop is a dead object?
      
      if (!GPGC_Marks::is_old_marked_strong_live(ref)) {
        _table[i] = ALWAYS_POISON_OBJECTREF(nullRef);
      } else {
        // unlike unlink(), we need to make sure objectRef's are marked through to get consistent NMT bits.
        // TODO: we could be faster here if we used the already relocated ref we have.
        GPGC_OldCollector::mark_to_live((heapRef*)addr);
      }
    }
  }
}

void CodeCacheOopTable::print(){
  //  tty->print_cr("CodeCacheOopTable: top dispensed index = %d", _ccot->_top_dispensed_index);
  printf("CodeCacheOopTable: top dispensed index = %ld\n", _ccot->_top_dispensed_index);
  printf("CodeCacheOopTable: population = %ld\n", _ccot->population());
}
