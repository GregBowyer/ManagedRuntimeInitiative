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


#include "classFileStream.hpp"
#include "compactingPermGenGen.hpp"
#include "constantPoolOop.hpp"
#include "cSpaceCounters.hpp"
#include "genRemSet.hpp"
#include "generationCounters.hpp"
#include "instanceKlass.hpp"
#include "systemDictionary.hpp"
#include "universe.hpp"

#include "oop.inline.hpp"

// An ObjectClosure helper: Recursively adjust all pointers in an object
// and all objects by referenced it. Clear marks on objects in order to
// prevent visiting any object twice. This helper is used when the
// RedefineClasses() API has been called.

class AdjustSharedObjectClosure : public ObjectClosure {
public:
  void do_object(oop obj) {
    if (obj->is_shared_readwrite()) {
      if (obj->mark()->is_marked()) {
obj->clear_mark();//Don't revisit this object.
        obj->adjust_pointers();   // Adjust this object's references.
      }
    }
  }
};


// An OopClosure helper: Recursively adjust all pointers in an object
// and all objects by referenced it. Clear marks on objects in order
// to prevent visiting any object twice.

class RecursiveAdjustSharedObjectClosure : public OopClosure {
public:
  void do_oop(objectRef* o) {
    oop obj = UNPOISON_OBJECTREF(*o, o).as_oop();
    if (obj->is_shared_readwrite()) {
      if (obj->mark()->is_marked()) {
obj->clear_mark();//Don't revisit this object.
        obj->oop_iterate(this);   // Recurse - adjust objects referenced.
        obj->adjust_pointers();   // Adjust this object's references.

        // Special case: if a class has a read-only constant pool,
        // then the read-write objects referenced by the pool must
        // have their marks reset.

        if (obj->klass() == Universe::instanceKlassKlassObj()) {
          instanceKlass* ik = instanceKlass::cast((klassOop)obj);
          constantPoolOop cp = ik->constants();
          if (cp->is_shared_readonly()) {
            cp->oop_iterate(this);
          }
        }
      }
    }
  };
};


// We need to go through all placeholders in the system dictionary and
// try to resolve them into shared classes. Other threads might be in
// the process of loading a shared class and have strong roots on
// their stack to the class without having added the class to the
// dictionary yet. This means the class will be marked during phase 1
// but will not be unmarked during the application of the
// RecursiveAdjustSharedObjectClosure to the SystemDictionary. Note
// that we must not call find_shared_class with non-read-only symbols
// as doing so can cause hash codes to be computed, destroying
// forwarding pointers.
class TraversePlaceholdersClosure : public OopClosure {
 public:
  void do_oop(objectRef* o) {
    oop obj = UNPOISON_OBJECTREF(*o, o).as_oop();
    if (obj->klass() == Universe::symbolKlassObj() &&
        obj->is_shared_readonly()) {
      symbolHandle sym((symbolOop) obj);
objectRef k=(objectRef)SystemDictionary::find_shared_class(sym);
if(k.not_null()){
        RecursiveAdjustSharedObjectClosure clo;
        clo.do_oop(&k);
      }
    }
  }
};


void CompactingPermGenGen::initialize_performance_counters() {

  const char* gen_name = "perm";

  // Generation Counters - generation 2, 1 subspace
  _gen_counters = new GenerationCounters(gen_name, 2, 1, &_virtual_space);

  _space_counters = new CSpaceCounters(gen_name, 0,
                                       _virtual_space.reserved_size(),
                                      _the_space, _gen_counters);
}

void CompactingPermGenGen::update_counters() {
  if (UsePerfData) {
    _space_counters->update_all();
    _gen_counters->update_all();
  }
}


CompactingPermGenGen::CompactingPermGenGen(ReservedSpace rs,
                                           ReservedSpace shared_rs,
                                           size_t initial_byte_size,
                                           int level, GenRemSet* remset,
                                           ContiguousSpace* space,
                                           PermanentGenerationSpec* spec_) :
  OneContigSpaceCardGeneration(rs, initial_byte_size, MinPermHeapExpansion,
                               level, remset, space) {

  set_spec(spec_);

  // Break virtual space into address ranges for all spaces.
  {
    shared_end = (HeapWord*)(rs.base() + rs.size());
      misccode_end = shared_end;
      misccode_bottom = shared_end;
      miscdata_end = shared_end;
      miscdata_bottom = shared_end;
      readwrite_end = shared_end;
      readwrite_bottom = shared_end;
      readonly_end = shared_end;
      readonly_bottom = shared_end;
    shared_bottom = shared_end;
    unshared_end = shared_bottom;
  }
  unshared_bottom = (HeapWord*) rs.base();

  // Verify shared and unshared spaces adjacent.
  assert((char*)shared_bottom == rs.base()+rs.size(), "shared space mismatch");
  assert(unshared_end > unshared_bottom, "shared space mismatch");

  // Split reserved memory into pieces.

  ReservedSpace ro_rs   = shared_rs.first_part(0,
                                              false);
  ReservedSpace tmp_rs1 = shared_rs.last_part(0);
  ReservedSpace rw_rs   = tmp_rs1.first_part(0,
                                             false);
  ReservedSpace tmp_rs2 = tmp_rs1.last_part(0);
  ReservedSpace md_rs   = tmp_rs2.first_part(0,
                                             false);
  ReservedSpace mc_rs   = tmp_rs2.last_part(0);

  _shared_space_size = 0;

  // Allocate the unshared (default) space.
  _the_space = new ContigPermSpace(_bts,
               MemRegion(unshared_bottom, heap_word_size(initial_byte_size)));
  if (_the_space == NULL)
    vm_exit_during_initialization("Could not allocate an unshared"
                                  " CompactingPermGen Space");

  {
    _ro_space = NULL;
    _rw_space = NULL;
  }
}


// Do a complete scan of the shared read write space to catch all
// objects which contain references to any younger generation.  Forward
// the pointers.  Avoid space_iterate, as actually visiting all the
// objects in the space will page in more objects than we need.
// Instead, use the system dictionary as strong roots into the read
// write space.
//
// If a RedefineClasses() call has been made, then we have to iterate
// over the entire shared read-write space in order to find all the
// objects that need to be forwarded. For example, it is possible for
// an nmethod to be found and marked in GC phase-1 only for the nmethod
// to be freed by the time we reach GC phase-3. The underlying method
// is still marked, but we can't (easily) find it in GC phase-3 so we
// blow up in GC phase-4. With RedefineClasses() we want replaced code
// (EMCP or obsolete) to go away (i.e., be collectible) once it is no
// longer being executed by any thread so we keep minimal attachments
// to the replaced code. However, we can't guarantee when those EMCP
// or obsolete methods will be collected so they may still be out there
// even after we've severed our minimal attachments.

void CompactingPermGenGen::pre_adjust_pointers() {
}


#ifdef ASSERT
class VerifyMarksClearedClosure : public ObjectClosure {
public:
  void do_object(oop obj) {
assert(!obj->mark()->is_marked(),
           "Shared oop still marked?");
  }
};
#endif


void CompactingPermGenGen::post_compact() {
}


void CompactingPermGenGen::space_iterate(SpaceClosure* blk, bool usedOnly) {
  OneContigSpaceCardGeneration::space_iterate(blk, usedOnly);
}


void CompactingPermGenGen::print_on(outputStream* st) const {
  OneContigSpaceCardGeneration::print_on(st);
    st->print_cr("No shared spaces configured.");
}


// References from the perm gen to the younger generation objects may
// occur in static fields in Java classes or in constant pool references
// to String objects. 

void CompactingPermGenGen::younger_refs_iterate(OopsInGenClosure* blk) {
  OneContigSpaceCardGeneration::younger_refs_iterate(blk);
}


// Shared spaces are addressed in pre_adjust_pointers.
void CompactingPermGenGen::adjust_pointers() {
  the_space()->adjust_pointers();
}


void CompactingPermGenGen::compact() {
  the_space()->compact();
}


size_t CompactingPermGenGen::contiguous_available() const {
  // Don't include shared spaces.
  return OneContigSpaceCardGeneration::contiguous_available()
         - _shared_space_size;
}

size_t CompactingPermGenGen::max_capacity() const {
  // Don't include shared spaces.
assert(_shared_space_size==0,
    "If not used, the size of shared spaces should be 0");
  return OneContigSpaceCardGeneration::max_capacity()
          - _shared_space_size;
}



bool CompactingPermGenGen::grow_by(size_t bytes) {
  // Don't allow _virtual_size to expand into shared spaces.
  size_t max_bytes = _virtual_space.uncommitted_size() - _shared_space_size;
  if (bytes > _shared_space_size) {
    bytes = _shared_space_size;
  }
  return OneContigSpaceCardGeneration::grow_by(bytes);
}


void CompactingPermGenGen::grow_to_reserved() {
  // Don't allow _virtual_size to expand into shared spaces.
  if (_virtual_space.uncommitted_size() > _shared_space_size) {
    size_t remaining_bytes = 
      _virtual_space.uncommitted_size() - _shared_space_size;
    bool success = OneContigSpaceCardGeneration::grow_by(remaining_bytes);
    DEBUG_ONLY(if (!success) warning("grow to reserved failed");)
  }
}


// No young generation references, clear this generation's main space's
// card table entries.  Do NOT clear the card table entries for the
// read-only space (always clear) or the read-write space (valuable
// information).

void CompactingPermGenGen::clear_remembered_set() {
  _rs->clear(MemRegion(the_space()->bottom(), the_space()->end()));
}


// Objects in this generation's main space may have moved, invalidate
// that space's cards.  Do NOT invalidate the card table entries for the
// read-only or read-write spaces, as those objects never move.

void CompactingPermGenGen::invalidate_remembered_set() {
  _rs->invalidate(used_region());
}


void CompactingPermGenGen::verify(bool allow_dirty) {
  the_space()->verify(allow_dirty);
}


HeapWord* CompactingPermGenGen::unshared_bottom;
HeapWord* CompactingPermGenGen::unshared_end;
HeapWord* CompactingPermGenGen::shared_bottom;
HeapWord* CompactingPermGenGen::shared_end;
HeapWord* CompactingPermGenGen::readonly_bottom;
HeapWord* CompactingPermGenGen::readonly_end;
HeapWord* CompactingPermGenGen::readwrite_bottom;
HeapWord* CompactingPermGenGen::readwrite_end;
HeapWord* CompactingPermGenGen::miscdata_bottom;
HeapWord* CompactingPermGenGen::miscdata_end;
HeapWord* CompactingPermGenGen::misccode_bottom;
HeapWord* CompactingPermGenGen::misccode_end;

void** CompactingPermGenGen::_vtbl_list;
