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
#ifndef OBJECTREF_PD_HPP
#define OBJECTREF_PD_HPP


#include "allocation.hpp"
#include "oopsHierarchy.hpp"
class klassRef;

// objectRef poisoning macros.  In debug modes, we xor -1 objectRefs not on the stack,
// to try and find cases of accessing them without an LVB.

#ifdef ASSERT
#  define SHOULD_BE_POISONED(ref_addr)       (objectRef::should_be_poisoned(ref_addr))
#  define VERIFY_POISONING(ref, addr)        (objectRef::verify_poisoning(ref, addr))

#  define POISON_AND_STORE_REF(addr, ref)    (ref_store_without_check((addr), (ref)))

#  define POISON_KLASSREF(r)                 (poison_klassRef(r))
#  define ALWAYS_POISON_OBJECTREF(ref)       (objectRef::poison(ref))
#  define POISON_OBJECTREF(ref, addr)        (objectRef::poison((ref), (addr)))
#  define POISON_SYMBOLREF(r)                (poison_symbolRef(r))
#  define UNPOISON_KLASSREF(r)               (unpoison_klassRef(r))
#  define UNPOISON_METHODREF(r)              (unpoison_methodRef(r))
#  define UNPOISON_OBJECTREF(ref, addr)      (objectRef::unpoison((ref), (addr)))
#  define PERMISSIVE_UNPOISON(ref, addr)     (objectRef::permissive_unpoison((ref), (addr)))
#  define ALWAYS_UNPOISON_OBJECTREF(ref)     (objectRef::unpoison(ref))
#  define UNPOISON_SYMBOLREF(r)              (unpoison_symbolRef(r))
#  define UNPOISON_OBJECTREF_IF_NOT_PTR(r)   (objectRef::unpoison_if_not_ptr(r))
#  define UNPOISON_OBJECTREF_IF_NOT_STACK(r) (objectRef::unpoison_if_not_stack(r))
#else 
#  define SHOULD_BE_POISONED(ref_addr)       (false)
#  define VERIFY_POISONING(ref, addr)        (true)

#  define POISON_AND_STORE_REF(addr, ref)    (*(addr)) = (ref)

#  define POISON_KLASSREF(r)                 (r)
#  define ALWAYS_POISON_OBJECTREF(ref)       (ref)
#  define POISON_OBJECTREF(ref, addr)        (ref)
#  define POISON_SYMBOLREF(r)                (r)
#  define UNPOISON_KLASSREF(r)               (r)
#  define UNPOISON_METHODREF(r)              (r)
#  define UNPOISON_OBJECTREF(ref, addr)      (ref)
#  define PERMISSIVE_UNPOISON(ref, addr)     (ref)
#  define ALWAYS_UNPOISON_OBJECTREF(ref)     (ref)
#  define UNPOISON_SYMBOLREF(r)              (r)
#  define UNPOISON_OBJECTREF_IF_NOT_PTR(r)   (r)
#  define UNPOISON_OBJECTREF_IF_NOT_STACK(r) (*r)
#endif // ASSERT


class objectRef VALUE_OBJ_CLASS_SPEC{
 public:
  intptr_t _objref; // Lots of compiler whining if this isn't public

  enum {
    ptr_space_id = 0x0UL,
    stack_space_id = 0x1UL, // for now.. this is the first frame_id... stack space becomes a range 
    new_space_id = 0x6UL,
    old_space_id = 0x7UL,
    dummy_space_id = 0xABABABABABABABABUL // this exists to force the compiler to make the above 64 bit enums
  };

  // We include the reserved and klass id bits here because they are the same in both
  // heapRef and stackRef types. If this should change, they will need to move to
  // the subtypes.

  //  < Klass-ID (18) | Space-ID (3) | NMT (1) | PageID (21) | Offset Inside Page (21) >
  enum {
    klass_id_bits       = 18,
    space_bits          =  3,
    frame_id_bits       =  4,
    nmt_bits            =  1,
    unknown_bits        = 42,
    reserved_bits       = 1, // used for ref-poisoning + marking array chunks

    non_address_bits    = 16 // x86_64 doesn't implement the top bits of a 64 bit pointer
  };

  enum {
    unknown_shift       = 0,
    reserved_shift      = 0, // could use unknown_shift... will need to modify a lot more files
    nmt_shift           = unknown_bits, // also used as frame_id_shift for stack references in SBA mode
    space_shift         = nmt_shift + nmt_bits,
    klass_id_shift      = space_shift + space_bits,

    non_address_shift   = BitsPerWord - non_address_bits,
  };

  enum {
    page_id_bits        = 21,
    offset_in_page      = 21
  };

  enum {
    unknown_mask                = (address_word)right_n_bits(unknown_bits),
    unknown_mask_in_place       = (address_word)unknown_mask << unknown_shift,
    nmt_mask                    = (address_word)right_n_bits(nmt_bits),
    nmt_mask_in_place           = (address_word)nmt_mask << nmt_shift,
    frame_id_mask               = (address_word)right_n_bits(frame_id_bits),
    frame_id_mask_in_place      = (address_word)frame_id_mask << nmt_shift,
    space_mask                  = (address_word)right_n_bits(space_bits),
    space_mask_in_place         = (address_word)space_mask << space_shift,
    klass_id_mask               = (address_word)right_n_bits(klass_id_bits),
    klass_id_mask_in_place      = (address_word)klass_id_mask << klass_id_shift,
    reserved_mask               = (address_word)right_n_bits(reserved_bits),

    non_address_mask            = (address_word)right_n_bits(non_address_bits),
    non_address_mask_in_place   = (address_word)non_address_mask << non_address_shift,
  };

  //
  // Static methods
  //

  inline static uint64_t discover_klass_id(oop o);
  inline static uint64_t discover_nmt(uint64_t space_id, oop o);

  //
  // Constructors
  //

  objectRef() {
    _objref = 0; // 0 is always mapped to NULL
  }

  // Relatively expensive, has to test for stackRef vs heapRef typing
objectRef(oop o);

  // Used to construct derived pointers (Relatively expensive ref constructor!)
  objectRef(oop base, oop offset);

  objectRef(const uint64_t raw_value) {
    _objref = raw_value;
  }

  //
  // Overloaded operators (BLEH!)
  //

  void operator = (const objectRef& right) {
    _objref = right._objref;
  }

  void operator = (const objectRef& right) volatile {
    _objref = right._objref;
  }

  bool operator == (const objectRef& right) const {
    return _objref == right._objref;
  }

  bool operator != (const objectRef& right) const {
    return _objref != right._objref;
  }

  //
  // Accessor methods (in const and volatile const forms)
  //

  bool is_ptr()   const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) ptr_space_id; }
  // should be asserting SBA mode: will change into a range check
  bool is_stack() const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask) == (uint64_t) stack_space_id; } 
  bool is_new()   const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) new_space_id; }
  bool is_old()   const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) old_space_id; }
  bool is_heap()  const   { return !(is_ptr() || is_stack()); }

  bool is_ptr()   volatile const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) ptr_space_id; }
  // should be asserting SBA mode:
  bool is_stack() volatile const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask) == (uint64_t) stack_space_id; } 
  bool is_new()   volatile const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) new_space_id; }
  bool is_old()   volatile const   { assert0( !RefPoisoning || !is_poisoned() ); return ((_objref >> space_shift) & space_mask)  == (uint64_t) old_space_id; }
  bool is_heap()  volatile const   { return !(is_ptr() || is_stack()); }

  intptr_t raw_value() const {
    return _objref;
  }

  intptr_t raw_value() volatile const {
    return _objref;
  }

  void set_raw_value(uint64_t raw_value) {
    _objref = raw_value;
  }

  bool is_null() const {
    return _objref == 0;
  }

  bool is_null() volatile const {
    return _objref == 0;
  }

  bool not_null() const {
    return _objref != 0;
  }

  bool not_null() volatile const {
    return _objref != 0;
  }

  bool is_null_or_old() volatile const {
    return is_null() || is_old();
  }

  unsigned int nmt() const {
    return (_objref >> nmt_shift) & nmt_mask;
  }

  unsigned int nmt() volatile const {
    return (_objref >> nmt_shift) & nmt_mask;
  }

  void set_nmt(unsigned int nmt) {
    _objref = _objref & ~objectRef::nmt_mask_in_place;
    _objref |= ((intptr_t)nmt) << objectRef::nmt_shift;
  }

  void flip_nmt() {
    _objref ^= nmt_mask_in_place;
  }

  unsigned int space_id() const {
    return (_objref >> space_shift) & space_mask;
  }

  unsigned int space_id() volatile const {
    return (_objref >> space_shift) & space_mask;
  }

  unsigned int reserved() const {
    return (_objref >> reserved_shift) & reserved_mask;
  }

  unsigned int reserved() volatile const {
    return (_objref >> reserved_shift) & reserved_mask;
  }

  inline unsigned int klass_id() const {
    return KIDInRef 
      ? ((_objref >> klass_id_shift) & klass_id_mask) 
      : discover_klass_id(as_oop());
  }

  inline unsigned int klass_id() volatile const {
    return KIDInRef 
      ? ((_objref >> klass_id_shift) & klass_id_mask) 
      : discover_klass_id(as_oop());
  }

  inline klassRef klass() const;

  inline unsigned int non_address() const {
    return (_objref >> non_address_shift) & non_address_mask;
  }

  //
  // Conversion methods
  //

oop as_oop()const{
#ifdef ASSERT
    if( !is_null() && !is_stack() && !is_heap() ) fatal1("Not an oop  - " INTPTR_FORMAT, _objref);
    if( (_objref & 0x7) != 0       && is_heap() ) fatal1("Not aligned - " INTPTR_FORMAT, _objref);
#endif
    return (oop)as_address();
  }

  oop as_oop() volatile const {
    assert(is_null() || is_stack() || is_heap(), "Not an oop");
    assert((_objref & 0x7) == 0 || (!is_heap()), "Not aligned");
    return (oop)as_address();
  }

  address as_address()          const { return (address)(_objref & unknown_mask_in_place);  }
  address as_address() volatile const { return (address)(_objref & unknown_mask_in_place);  }

  address unreserved_address()  const { return (address)(_objref & unknown_mask_in_place & ~reserved_mask); }

  static bool is_null_or_heap(const objectRef* p) {
objectRef ref=*p;
    if ( ref.is_null() ) return true;
    ref = UNPOISON_OBJECTREF(ref, p);
    return ref.is_heap();
  }

  bool is_poisoned() const { 
    return (_objref & reserved_mask) != 0;
  }

  bool is_poisoned() const volatile { 
    return (_objref & reserved_mask) != 0;
  }

  bool is_in_a_stack() const {
    return address(__THREAD_STACK_REGION_START_ADDR__) <= address(this) && address(this) < (address(__THREAD_STACK_REGION_START_ADDR__) + ThreadStackRegionSize);
  }

#ifdef ASSERT
  static bool needs_lvb(const objectRef* addr) {
    // This test excludes only the C stacks and e.g. claims storing into
    // C-heap objects needs_lvb.
    return uintptr_t(addr) < __THREAD_STACK_REGION_START_ADDR__ || (__THREAD_STACK_REGION_START_ADDR__ + ThreadStackRegionSize) <= uintptr_t(addr);
  }

  // An object should be poisoned if it is not in a thread stack.
  static bool should_be_poisoned(const objectRef* addr) {
return needs_lvb(addr);
  }

  static void verify_poisoning(objectRef ref, const objectRef* addr) {
    if ( RefPoisoning && should_be_poisoned(addr) ) {
      assert0( ref.is_null() || ref.is_poisoned() );
    }
  }

static objectRef poison(objectRef ref){
    if ( RefPoisoning && ref.not_null() ) {
      assert0( ! ref.is_poisoned() );
      return objectRef(ref.raw_value() ^ -1);
    }
    return ref;
  }

  static objectRef poison(objectRef ref, const objectRef* addr) {
    if ( RefPoisoning && ref.not_null() && should_be_poisoned(addr) ) {
      assert0( ! ref.is_poisoned() );
      return objectRef(ref.raw_value() ^ -1);
    }
    return ref;
  }

  static objectRef unpoison(objectRef ref, const objectRef* addr) {
    if ( RefPoisoning && ref.not_null() && should_be_poisoned(addr) ) {
      assert0( ref.is_poisoned() );
      return objectRef(ref.raw_value() ^ -1);
    }
    return ref;
  }

  static objectRef permissive_unpoison(objectRef ref, const objectRef* addr) {
    // Unpoison an objectRef.  Be permissive about finding poisoned stack refs, and unpoison
    // them as well.
    if ( RefPoisoning && ref.not_null() ) {
      if ( should_be_poisoned(addr) ) {
        assert0( ref.is_poisoned() );
        return objectRef(ref.raw_value() ^ -1);
      }
      if ( ref.is_poisoned() ) {
        return objectRef(ref.raw_value() ^ -1);
      }
    }
    return ref;
  }

static objectRef unpoison(objectRef ref){
    if ( RefPoisoning && ref.not_null() ) {
      assert0( ref.is_poisoned() );
      return objectRef(ref.raw_value() ^ -1);
    }
    return ref;
  }

static objectRef unpoison_if_not_ptr(objectRef r){
    if (RefPoisoning && !r.is_null()) {
      if (!(r.raw_value() & reserved_mask)) {// we found a ptr.. make sure it is 8 byte aligned
        guarantee(!(r.raw_value() & 0x7), "found a pointer that is not 8 byte aligned");
      } else { // found a non-null ref.. unpoison it
        return objectRef(r.raw_value() ^ -1);
      }
    }
    return r;
  }
#endif //ASSERT
};

const objectRef nullRef = objectRef((uint64_t)NULL);

void update_barrier_set(objectRef*p,objectRef v);

// ref_store is too large to inline now
objectRef ref_check_without_store(objectRef base, objectRef* p, objectRef r);
void ref_store(objectRef base, objectRef* p, objectRef r);
inline void ref_store_without_check(objectRef* p, objectRef r); 
inline void ref_store_without_check(objectRef volatile * p, objectRef r); 

#endif // OBJECTREF_PD_HPP
