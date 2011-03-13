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
#ifndef OOP_INLINE_HPP
#define OOP_INLINE_HPP


// Implementation of all inlined member functions defined in oop.hpp
// We need a separate file to avoid circular references
#include "arrayKlass.hpp"
#include "arrayOop.hpp"
#include "collectedHeap.hpp"
#include "compactingPermGenGen.hpp"
#include "oopTable.hpp"
#include "orderAccess.hpp"
#include "psMarkSweep.hpp"
#include "psParallelCompact.hpp"
#include "safepoint.hpp"

inline klassOop oopDesc::klass() const {
  return KlassTable::getKlassByKlassId(mark()->kid()).as_klassOop();
}

inline klassRef oopDesc::klass_ref() const {
  return KlassTable::getKlassByKlassId(mark()->kid());
}

inline klassOop oopDesc::GC_remapped_klass() const {
  return (klassOop) LVB::mutator_relocate_only(klass_addr()).as_oop();
}

inline klassRef* oopDesc::klass_addr() const {
  return KlassTable::getKlassAddrByKlassId(mark()->kid());
}

inline void oopDesc::init_mark() { set_mark(markWord::prototype_without_kid()); }
inline void oopDesc::clear_mark    () { set_mark(mark()->clear_and_save_kid()); }
inline void oopDesc::clear_and_mark() { set_mark(mark()->clear_save_kid_and_mark()); }

inline void oopDesc::clear_fwd_ptr_mark(){
  markWord *m = mark();
  assert(KlassTable::is_valid_klassId(m->kid()), "invalid kid in fowarded pointer oop being cleared");
assert(m->decode_pointer()!=NULL,"expected a non-null forwarding pointer in mark");
  set_mark(m->clear_and_save_kid());
}

inline Klass* oopDesc::blueprint()           const { return klass()->klass_part(); }

inline bool oopDesc::is_a(klassOop k)        const { return blueprint()->is_subtype_of(k); }

inline bool oopDesc::is_instance()           const { return blueprint()->oop_is_instance(); }
inline bool oopDesc::is_instanceRef()        const { return blueprint()->oop_is_instanceRef(); }
inline bool oopDesc::is_array()              const { return blueprint()->oop_is_array(); }
inline bool oopDesc::is_objArray()           const { return blueprint()->oop_is_objArray(); }
inline bool oopDesc::is_typeArray()          const { return blueprint()->oop_is_typeArray(); }
inline bool oopDesc::is_javaArray()          const { return blueprint()->oop_is_javaArray(); }
inline bool oopDesc::is_symbol()             const { return blueprint()->oop_is_symbol(); }
inline bool oopDesc::is_klass()              const { return blueprint()->oop_is_klass(); }
inline bool oopDesc::is_thread()             const { return blueprint()->oop_is_thread(); }
inline bool oopDesc::is_method()             const { return blueprint()->oop_is_method(); }
inline bool oopDesc::is_methodCode()const{return blueprint()->oop_is_methodCode();}
inline bool oopDesc::is_dependency()const{return blueprint()->oop_is_dependency();}
inline bool oopDesc::is_constMethod()        const { return blueprint()->oop_is_constMethod(); }
inline bool oopDesc::is_constantPool()       const { return blueprint()->oop_is_constantPool(); }
inline bool oopDesc::is_constantPoolCache()  const { return blueprint()->oop_is_constantPoolCache(); }

inline void*     oopDesc::field_base(int offset)        const { return (void*)&((char*)this)[offset]; }

inline oop*      oopDesc::obj_field_addr(int offset)    const { return (oop*)     field_base(offset); }
inline objectRef*oopDesc::ref_field_addr(int offset)const{return(objectRef*)field_base(offset);}
inline jbyte*    oopDesc::byte_field_addr(int offset)   const { return (jbyte*)   field_base(offset); }
inline jchar*    oopDesc::char_field_addr(int offset)   const { return (jchar*)   field_base(offset); }
inline jboolean* oopDesc::bool_field_addr(int offset)   const { return (jboolean*)field_base(offset); }
inline jint*     oopDesc::int_field_addr(int offset)    const { return (jint*)    field_base(offset); }
inline jshort*   oopDesc::short_field_addr(int offset)  const { return (jshort*)  field_base(offset); }
inline jlong*    oopDesc::long_field_addr(int offset)   const { return (jlong*)   field_base(offset); }
inline jfloat*   oopDesc::float_field_addr(int offset)  const { return (jfloat*)  field_base(offset); }
inline jdouble*  oopDesc::double_field_addr(int offset) const { return (jdouble*) field_base(offset); }

inline oop oopDesc::obj_field(int offset) const                     { return lvb_ref(ref_field_addr(offset)).as_oop(); }
inline objectRef oopDesc::ref_field(int offset) const               { return lvb_ref(ref_field_addr(offset)); }
inline oop oopDesc::GC_remapped_obj_field(int offset) const         { return LVB::mutator_relocate_only(ref_field_addr(offset)).as_oop(); }
inline void oopDesc::obj_field_put(int offset, oop value)           { ref_store(objectRef(this), ref_field_addr(offset), objectRef(value));   }
inline void oopDesc::ref_field_put(int offset, objectRef value)     { ref_store(objectRef(this), ref_field_addr(offset), value);   }
inline void oopDesc::ref_field_put(objectRef thsi, int offset, objectRef value) { ref_store(thsi, thsi.as_oop()->ref_field_addr(offset), value);   }

inline jbyte oopDesc::byte_field(int offset) const                  { return (jbyte) *byte_field_addr(offset);    }
inline void oopDesc::byte_field_put(int offset, jbyte contents)     { *byte_field_addr(offset) = (jint) contents; }

inline jboolean oopDesc::bool_field(int offset) const               { return (jboolean) *bool_field_addr(offset); }
inline void oopDesc::bool_field_put(int offset, jboolean contents)  { *bool_field_addr(offset) = (jint) contents; }

inline jchar oopDesc::char_field(int offset) const                  { return (jchar) *char_field_addr(offset);    }
inline void oopDesc::char_field_put(int offset, jchar contents)     { *char_field_addr(offset) = (jint) contents; }

inline jint oopDesc::int_field(int offset) const                    { return *int_field_addr(offset);        }
inline void oopDesc::int_field_put(int offset, jint contents)       { *int_field_addr(offset) = contents;    }

inline jshort oopDesc::short_field(int offset) const                { return (jshort) *short_field_addr(offset);  }
inline void oopDesc::short_field_put(int offset, jshort contents)   { *short_field_addr(offset) = (jint) contents;}

inline jlong oopDesc::long_field(int offset) const                  { return *long_field_addr(offset);       }
inline void oopDesc::long_field_put(int offset, jlong contents)     { *long_field_addr(offset) = contents;   }

inline jfloat oopDesc::float_field(int offset) const                { return *float_field_addr(offset);      }
inline void oopDesc::float_field_put(int offset, jfloat contents)   { *float_field_addr(offset) = contents;  }

inline jdouble oopDesc::double_field(int offset) const              { return *double_field_addr(offset);     }
inline void oopDesc::double_field_put(int offset, jdouble contents) { *double_field_addr(offset) = contents; }

inline oop oopDesc::obj_field_acquire(int offset) const                     { return lvb_ref(ref_field_addr(offset)).as_oop(); }
inline void oopDesc::release_obj_field_put(int offset, oop value)           { ref_store(objectRef(this), (objectRef*)ref_field_addr(offset), objectRef(value));           }
inline objectRef oopDesc::ref_field_acquire(int offset) const       { return lvb_ref(ref_field_addr(offset)); }
inline void oopDesc::release_ref_field_put(objectRef thsi, int offset, objectRef value) { ref_store(thsi, thsi.as_oop()->ref_field_addr(offset), value); }


inline jbyte oopDesc::byte_field_acquire(int offset) const                  { return OrderAccess::load_acquire(byte_field_addr(offset));     }
inline void oopDesc::release_byte_field_put(int offset, jbyte contents)     { OrderAccess::release_store(byte_field_addr(offset), contents); }

inline jboolean oopDesc::bool_field_acquire(int offset) const               { return OrderAccess::load_acquire(bool_field_addr(offset));     }
inline void oopDesc::release_bool_field_put(int offset, jboolean contents)  { OrderAccess::release_store(bool_field_addr(offset), contents); }

inline jchar oopDesc::char_field_acquire(int offset) const                  { return OrderAccess::load_acquire(char_field_addr(offset));     }
inline void oopDesc::release_char_field_put(int offset, jchar contents)     { OrderAccess::release_store(char_field_addr(offset), contents); }

inline jint oopDesc::int_field_acquire(int offset) const                    { return OrderAccess::load_acquire(int_field_addr(offset));      }
inline void oopDesc::release_int_field_put(int offset, jint contents)       { OrderAccess::release_store(int_field_addr(offset), contents);  }

inline jshort oopDesc::short_field_acquire(int offset) const                { return (jshort)OrderAccess::load_acquire(short_field_addr(offset)); }
inline void oopDesc::release_short_field_put(int offset, jshort contents)   { OrderAccess::release_store(short_field_addr(offset), contents);     }

inline jlong oopDesc::long_field_acquire(int offset) const                  { return OrderAccess::load_acquire(long_field_addr(offset));       }
inline void oopDesc::release_long_field_put(int offset, jlong contents)     { OrderAccess::release_store(long_field_addr(offset), contents);   }

inline jfloat oopDesc::float_field_acquire(int offset) const                { return OrderAccess::load_acquire(float_field_addr(offset));      }
inline void oopDesc::release_float_field_put(int offset, jfloat contents)   { OrderAccess::release_store(float_field_addr(offset), contents);  }

inline jdouble oopDesc::double_field_acquire(int offset) const              { return OrderAccess::load_acquire(double_field_addr(offset));     }
inline void oopDesc::release_double_field_put(int offset, jdouble contents) { OrderAccess::release_store(double_field_addr(offset), contents); }


inline int oopDesc::size_given_klass(Klass* klass)  {
  int lh = klass->layout_helper();
  int s  = lh >> LogHeapWordSize;  // deliver size scaled by wordSize

  // lh is now a value computed at class initialization that may hint
  // at the size.  For instances, this is positive and equal to the
  // size.  For arrays, this is negative and provides log2 of the
  // array element size.  For other oops, it is zero and thus requires
  // a virtual call.
  //
  // We go to all this trouble because the size computation is at the
  // heart of phase 2 of mark-compaction, and called for every object,
  // alive or dead.  So the speed here is equal in importance to the
  // speed of allocation.

  if (lh <= Klass::_lh_neutral_value) {
    // The most common case is instances; fall through if so.
    if (lh < Klass::_lh_neutral_value) {
      // Second most common case is arrays.  We have to fetch the
      // length of the array, shift (multiply) it appropriately, 
      // up to wordSize, add the header, and align to object size.
      size_t size_in_bytes;
      size_t array_length = (size_t) ((arrayOop)this)->length();
      size_in_bytes = array_length << Klass::layout_helper_log2_element_size(lh);
      size_in_bytes += Klass::layout_helper_header_size(lh);

      // This code could be simplified, but by keeping array_header_in_bytes
      // in units of bytes and doing it this way we can round up just once,
      // skipping the intermediate round to HeapWordSize.  Cast the result
      // of round_to to size_t to guarantee unsigned division == right shift.
      s = (int)((size_t)round_to(size_in_bytes, MinObjAlignmentInBytes) / 
	HeapWordSize);
    } else {
      // Must be zero, so bite the bullet and take the virtual call.
      s = klass->oop_size(this);
    }
  }

  assert(s > 0, "Bad size calculated");
  return s;
}

// Same as size_given_klass(), but safe for use by GC during a GC cycle, when
// we can't be sure that oop->_klass is a valid heapRef.
inline int oopDesc::GC_size_given_klass(Klass*klass){
  int lh = klass->layout_helper();
  int s  = lh >> LogHeapWordSize;  // deliver size scaled by wordSize

  // lh is now a value computed at class initialization that may hint
  // at the size.  For instances, this is positive and equal to the
  // size.  For arrays, this is negative and provides log2 of the
  // array element size.  For other oops, it is zero and thus requires
  // a virtual call.
  //
  // We go to all this trouble because the size computation is at the
  // heart of phase 2 of mark-compaction, and called for every object,
  // alive or dead.  So the speed here is equal in importance to the
  // speed of allocation.

  if (lh <= Klass::_lh_neutral_value) {
    // The most common case is instances; fall through if so.
    if (lh < Klass::_lh_neutral_value) {
      // Second most common case is arrays.  We have to fetch the
      // length of the array, shift (multiply) it appropriately, 
      // up to wordSize, add the header, and align to object size.
      size_t size_in_bytes;
      size_t array_length = (size_t) ((arrayOop)this)->length();
      size_in_bytes = array_length << Klass::layout_helper_log2_element_size(lh);
      size_in_bytes += Klass::layout_helper_header_size(lh);

      // This code could be simplified, but by keeping array_header_in_bytes
      // in units of bytes and doing it this way we can round up just once,
      // skipping the intermediate round to HeapWordSize.  Cast the result
      // of round_to to size_t to guarantee unsigned division == right shift.
      s = (int)((size_t)round_to(size_in_bytes, MinObjAlignmentInBytes) / 
	HeapWordSize);
      assert(s == klass->GC_oop_size(this), "wrong array object size");
    } else {
      // Must be zero, so bite the bullet and take the virtual call.
s=klass->GC_oop_size(this);
    }
  }

  assert(s > 0, "Bad size calculated");
  return s;
}


inline int oopDesc::size()  {
  return size_given_klass(blueprint());
}

inline bool oopDesc::is_parsable() {
  return blueprint()->oop_is_parsable(this);
}


// Used only for markSweep, scavenging
inline bool oopDesc::is_gc_marked() const {
  assert0(!UseGenPauselessGC && !UseParallelOldGC);
  return mark()->is_marked();
}

inline bool check_obj_alignment(oop obj) {
  int alignment_size = oopSize; // MOD-by-8 is slow in debug builds!
  return ((uintptr_t)obj & (alignment_size-1)) == 0;
}


// used only for asserts
inline bool oopDesc::is_oop(bool ignore_mark_word) const {
  oop obj = (oop) this;
  if (!check_obj_alignment(obj)) return false;
if(!Universe::heap()->is_in(obj)&&
      !JavaThread::sba_is_in_current((address)obj) ) return false;
  // obj is aligned and accessible in heap
  // try to find metaclass cycle safely without seg faulting on bad input
  // we should reach klassKlassObj by following klass link at most 3 times
  for (int i = 0; i < 3; i++) {
    // Avoid looking up a bogus klassId from the markWord.
    int kid = obj->mark()->kid();
    if (!KlassTable::is_valid_klassId(kid)) return false;
    klassRef* k   = KlassTable::getKlassAddrByKlassId(kid);
    objectRef ref = UNPOISON_OBJECTREF(*k, k);

    // A little help for GPGC: if the klass field is crap do not
    // attempt any GPGC smarts.
    if ( UseLVBs && Thread::current()->is_gc_mode() ) {
      if ( ref.is_null() ) return false;
      if ( !ref.is_heap() ) return false;
    }

    // Since we'll be comparing to Universe::klassKlassObj(), GC threads
    // need to get the forwarded address of a ref here:
    if( (*(intptr_t*)&ref & 0x7) != 0) return false; // test before LVB
    objectRef x = LVB::lvb_or_relocated(k);
    obj = (oop)x.as_address();
    // klass should be aligned and in permspace
    if (!check_obj_alignment(obj)) return false;
    if (!Universe::heap()->is_in_permanent(obj)) return false;
  }
  if (obj != Universe::klassKlassObj()) {
    return false;
  }

  // Header verification: the mark is typically non-NULL. If we're
  // at a safepoint, it must not be null.
  // Outside of a safepoint, the header could be changing (for example,
  // another thread could be inflating a lock on this object).
  if (ignore_mark_word) {
    return true;
  }
  if (mark() != NULL) {
    return true;
  }
  return !SafepointSynchronize::is_at_safepoint();
}


// used only for asserts
inline bool oopDesc::is_oop_or_null(bool ignore_mark_word) const {
  return this == NULL ? true : is_oop(ignore_mark_word);
}

inline void oopDesc::follow_header() { 
MarkSweep::mark_and_push(klass_addr());
}

inline void oopDesc::follow_contents() {
  assert (is_gc_marked(), "should be marked");
  blueprint()->oop_follow_contents(this);
}


// Used by mark-sweep
inline void oopDesc::forward_to_pointer(void* p) {
assert(Universe::heap()->is_in_reserved(p),"forwarding to unknown location");
  markWord *m = mark()->encode_pointer_as_mark(p);
  assert(m->decode_pointer() == p, "encoding must be reversable");
  set_mark(m);
}

// Used by scavenger
inline void oopDesc::forward_to_ref(objectRef p){
  assert(Universe::heap()->is_in_reserved(p.as_oop()) || 
         JavaThread::sba_is_in_current((address)p.as_oop()),
"forwarding to unknown location");
  markWord *m = markWord::encode_ref_as_mark(p);
assert(m->decode_ref()==p,"encoding must be reversable");
  set_mark(m);
}

// Used by parallel scavengers
inline bool oopDesc::cas_forward_to_ref(objectRef p,markWord*compare){
assert(Universe::heap()->is_in_reserved(p.as_oop()),
	 "forwarding to something not in heap");
markWord*m=markWord::encode_ref_as_mark(p);
assert(m->decode_ref()==p,"encoding must be reversable");
  return cas_set_mark(m, compare) == compare;
}

// Note that the forwardee is not the same thing as the displaced_mark.
// The forwardee is used when copying during scavenge and mark-sweep.
// It does need to clear the low two locking- and GC-related bits.
inline void* oopDesc::forwarded_pointer() const           { return mark()->decode_pointer(); }
inline objectRef oopDesc::forwarded_ref() const           { return mark()->decode_ref(); }


inline intptr_t oopDesc::identity_hash() {
  // Fast case; if the hash value is set
  // Note: The mark must be read into local variable to avoid concurrent updates.
markWord*mrk=mark();
return mrk->has_no_hash()?slow_identity_hash():mrk->hash();
}

inline void oopDesc::oop_iterate_header(OopClosure* blk) {
blk->do_oop(klass_addr());
}


inline void oopDesc::oop_iterate_header(OopClosure* blk, MemRegion mr) {
  klassRef *k = klass_addr();
if(mr.contains(k))blk->do_oop(k);
}

inline int oopDesc::adjust_pointers() {
  debug_only(int check_size = size());
  int s = blueprint()->oop_adjust_pointers(this);
  assert(s == check_size, "should be the same");
  return s;
}

#define OOP_ITERATE_DEFN(OopClosureType, nv_suffix)                        \
                                                                           \
inline int oopDesc::oop_iterate(OopClosureType* blk) {                     \
  SpecializationStats::record_call();                                      \
  return blueprint()->oop_oop_iterate##nv_suffix(this, blk);               \
}                                                                          \
                                                                           \
inline int oopDesc::oop_iterate(OopClosureType* blk, MemRegion mr) {       \
  SpecializationStats::record_call();                                      \
  return blueprint()->oop_oop_iterate##nv_suffix##_m(this, blk, mr);       \
}

ALL_OOP_OOP_ITERATE_CLOSURES_1(OOP_ITERATE_DEFN) 
ALL_OOP_OOP_ITERATE_CLOSURES_3(OOP_ITERATE_DEFN) 


inline bool oopDesc::is_shared() const {
  return CompactingPermGenGen::is_shared(this);
}

inline bool oopDesc::is_shared_readonly() const {
  return CompactingPermGenGen::is_shared_readonly(this);
}

inline bool oopDesc::is_shared_readwrite() const {
  return CompactingPermGenGen::is_shared_readwrite(this);
}

#endif // OOP_INLINE_HPP
