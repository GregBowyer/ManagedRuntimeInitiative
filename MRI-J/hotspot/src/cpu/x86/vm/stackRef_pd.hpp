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
#ifndef STACKREF_PD_HPP
#define STACKREF_PD_HPP

#include "methodOop.hpp"
#include "objectRef_pd.hpp"
#include "oopTable.hpp"

class BufferedLoggerMark;
class JavaThread;
class OopClosure;

class SBAPreHeader {
public:
  // Prefixed before every stack object is a word of SBA data.
  // It contains the allocation-site-id and FID.
  uint8_t  _fid;                // frame id
  uint8_t  _ispc;               // is a pc hint or a method object-id hint
  uint16_t _bci;                // bci for method hints
  int32_t  _moid;               // either pc or method object-id
  bool is_moop_hint() const { return _ispc == 0 && _moid != 0; }
  bool is_pc_hint() const { return _ispc == 1; }
  bool is_dead   () const { return _ispc == 2; }
  bool is_forward() const { return _ispc == 3; }
  void deaden () { _ispc = 2; }
  void zap_hint(){ _ispc = 0; _bci = 0; _moid = 0; }
  void forward(objectRef adr){ _ispc = 3; (((oop)this)+1)->forward_to_ref(adr); }
  objectRef get_forward() const { return (((oop)this)+1)->forwarded_ref(); }
  void validate_hint();        // Zap any broken allocation-site hints
  int fid( ) const { return _fid; }
  void set_fid( int fid ) { _fid = fid; }
  methodOop moop() const { return (methodOop)CodeCacheOopTable::getOopAt(_moid).as_oop(); }
  address pc() const { return (address)_moid; }
  void set_moop( methodOop moop ) { _moid = moop->objectId(); }

  void verify() const PRODUCT_RETURN;
void print()const PRODUCT_RETURN;
  void print (BufferedLoggerMark &m) const;

  void init(int fid, methodOop moop, int bci ); // Allocation site is interpreted
  void init(int fid, address pc ); // Allocation site is compiled
  void oops_do(OopClosure* f);
  void update_allocation_site( int escape_to_fid );

  // Gate how fast we move 'hot' allocation sites back to being stack-local.  Track repeated
  // failed attempts to 'lift' sites from heap-allocation to stack allocation.
  static const int SITES_LOG_SIZE = 12;
  static const int SITES_MASK = (1<<SITES_LOG_SIZE)-1;
  static int64_t _escape_sites[1<<SITES_LOG_SIZE];
  static int64_t _decayed_escapes[1<<SITES_LOG_SIZE];
  static int     _fails[1<<SITES_LOG_SIZE];
  static void decay_escape_sites();
  static bool should_stack_allocate( address pc );

  int get_escape_idx( bool claim_slot );

};

// A stackRef is a specific type of objectRef. It can only be a stack_space_id space type. 

// stackRefs have the following layout:
//
// Bit 63: nmt
// Bit 62: space id (must be either 01)
// Bit 61: space id (must be either 01)
// Bit 60: reserved
// Bit 59: klass id
// ...
// Bit 41: klass id
// Bit 40: virtual address
// ...
// Bit  0: virtual address 
class stackRef : public objectRef {
 public:
  enum {
    va_bits                 = 42,
    va_shift                = 0,
    va_mask                 = (address_word)right_n_bits(va_bits),
    va_mask_in_place        = (address_word)va_mask << va_shift,
    frame_id_bits           = 9 // no FID bits in X86 stackRef but enum used elsewhere
  };

  //
  // Static methods
  //

  inline static uint64_t discover_space_id(const void* addr);
  inline static uint64_t discover_frame_id(oop o);
  
  //
  // Constructors
  //

  stackRef() {
    _objref = 0; // 0 is always mapped to NULL
  }
  
  stackRef(const objectRef& ref) {
    assert (ref.is_null() || ref.is_stack(), "Must be a stackRef");
    _objref = ref._objref;
  }

  stackRef(const oop o) {
set_value(o);
  }

  // Danger Will Robinson! This constructor does not do any error or assert checks.
  stackRef(const uint64_t raw_value) {
    _objref = raw_value;
  }
  // Standard "know it all" constructor
  inline stackRef( const oop o, uint64_t kid, uint64_t nmt, uint64_t fid);

  //
  // Overloaded operators (BLEH!)
  //

  // I would have liked to disallow all assignments of stackRef = objectRef,
  // but the assumption that this could be done is already heavily embedded in
  // the code.  Second best choice is to hope we catch bad assignments with
  // the asserts.

  void operator = (const objectRef& right) {
assert(right.is_null()||right.is_stack(),"Must be a stackRef");
    _objref = right._objref;
  }

  void operator = (const objectRef& right) volatile {
assert(right.is_null()||right.is_stack(),"Must be a stackRef");
    _objref = right._objref;
  }

  //
  // Accessor methods (in const and volatile const forms)
  //

  inline void set_value_base(const oop o, uint64_t klass_id, uint64_t nmt );
  inline void set_value(oop o);

  inline void set_sbafid(long fid) {/*X86 does not use FID in REF bits*/ }
#ifdef FIDS_IN_REF
  inline int  sbafid() const       { return preheader()->fid(); /* TODO properly encode FID in ref */ }
#endif

  // Note that this simply masks out the va bits. It does no checks
  // or asserts. Derived pointers use this to extract misaligned values.
  inline uint64_t va() {
    return (uint64_t)(_objref & va_mask);
  }

  //
  // Conversion methods
  //
  address as_address(const JavaThread*const jt) const;
  inline void set_va(HeapWord *va);

  inline static SBAPreHeader *preheader(oop stack_obj_as_oop) { return ((SBAPreHeader *)stack_obj_as_oop) -1; }
  inline SBAPreHeader *preheader() const { return preheader(as_oop()); }
};

#endif // STACKREF_PD_HPP
