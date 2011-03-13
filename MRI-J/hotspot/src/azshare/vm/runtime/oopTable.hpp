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
#ifndef OOPTABLE_HPP
#define OOPTABLE_HPP



#include "debug.hpp"
#include "iterator.hpp"
#include "mutex.hpp"
#include "refsHierarchy_pd.hpp"


class BoolObjectClosure;
class InvertedVirtualSpace;
class KlassHandle;
class VirtualSpace;
class GPGC_GCManagerNewStrong;
class GPGC_GCManagerOldStrong;

//
// This file contains declarations of classes used to manage the klass oop table
// and the code cache constant oop table.
//
//
//                  (Low Memory)
//       +-------------------------------+
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       |          Constants            |
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       |                               |  (-2)
//       |                               |  (-1)
//       +------------(null)-------------+<-- R28_KTB (_table)
//       |                               |  (+1)
//       |                               |  (+2)
//       |                               |
//       |                               |
//       |                               |
//       |           Klasses             |
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       |                               |
//       +-------------------------------+
//                (High Memory)
//
// The two oop tables are allocated in consecutive ranges of memory so they can
// be referenced from a common base address; constants are accessed using
// negative indices and klasses using positive indices.  Index 0 is reserved for
// the null value. In compiled code references are made from a base register,
// R28_KTB.
//
// There are different allocation methods used for finding free slots in each
// table.
//
// The constant table entries are allocated using a log2(N) closed hash table.
// The index for an entry is computed by using the identity hash value for the
// object (being inserted) and an attempt counter.  As the number of attempts
// increases, the number of bits used from the identity increases, thus spreading
// the range as the number of attempts increases.  The lower two bits of the
// index match the attempt counter so that adjacent lookups are in the same cache
// line.
//
// The klass table entries are allocated using a linear assignment.  A lowest
// free slot index is kept to speed up the search.
//
// Both tables use unique entries for each object, ie., an oop will only occur
// once in the table.
//

//
// Base class for both constant and klass tables.
//
class OopTable:public CHeapObj{
  
protected:
  VirtualSpace *_vs;                            // Portion of reserved space used by this table.
  AzLock       *_table_lock;                    // Lock used for growing the table.
  objectRef    *_table;                         // Table itself.
  bool          _grows_down;                    // True indicates that the table grows to low memory.
  int           _growth_size_in_bytes;          // Amount the table should grow (usually a page.)
  
  // These values are always positive even when table grows down.
  volatile long int      _top_committed_index;  // The highest index available for table.
  volatile long int      _top_table_index;      // Top index available for hash look up (power of two)
  volatile long int      _top_dispensed_index;  // The highest index allocated in table.
           intptr_t      _max_index;            // Maximum possible index.
   
  // This method returns a hash table index based on the identity hash id of
  // an object.  This yields a less than 4 * log2(table_size) worst case search
  // of the table.
  intptr_t getRawIndex(intptr_t id_hash, intptr_t attempt);
  
  // Return the count of non-null entries.
  long int population() {
    long int count = 0;
    for (long int i = 1; i < _top_dispensed_index; i++) {
      if (_table[i].is_null()) count++;
    }
    return count;
  }

  // Null out the slot and potentially updating counters.
  virtual void free(intptr_t i);
  
public:

  // Add an entry to the table.
  virtual intptr_t put(objectRef r);
  
  // Used by the compiler runtime to set up the R28_KTB register.
  void* getTableBase() { return (void*)_table; }

  // Return the entry at the index.  Works even when the index is negative (constants table.)
  objectRef get(int i);

  // Return the address of an entry at the index.  Works even when the index is negative (constants table.)
  objectRef* getAddr(int i) {
assert(_table!=NULL,"oop table not initialized");
    return &(_table[i]);
  }

  // Return the index of the entry in lowest memory.
  long int lowestUsedIndex() { return _grows_down ? -_top_dispensed_index : 1; }

  // Return the index of the entry in highest memory.
  long int highestUsedIndex() { return _grows_down ?  -1 : _top_dispensed_index; }

  // Commit more reserved memory for the table. 
  void grow();

  // GC
void oops_do_impl(OopClosure*f);
void unlink_impl(BoolObjectClosure*is_alive);
};


//
// Constant table is managed using a log2(N) closed hash table.  The table grows
// toward low memory.
//
class CodeCacheOopTable : public OopTable {
 private:
  static CodeCacheOopTable *_ccot;      // Global reference to constant table.
  
  // Constructor.
  CodeCacheOopTable(InvertedVirtualSpace *vs);

 public:

  // Initialize the global constants table.
  static void init(InvertedVirtualSpace *vs);
  
  // Override behavior of OopTable to intercept klasses.
  virtual intptr_t put(objectRef r);

  // Recast oop handle to reference before adding to table.
  static int  putOop(jobject h);
  
  // Global add to either klass or constants table.
  static int  putOop(objectRef r);
  
  // Global fetch of either klass (positive) or constant (negative.)
  static objectRef getOopAt(int i);
  
  // Global add to either klass or constants table.
  
  // Global fetch of either klass (positive) or constant (negative.)

  // True if index is in range.  Doesn't check validity of entry.
  static inline bool is_valid_index(int i) { return i >= -_ccot->_top_dispensed_index && i <= -1; }

  // GC
  void GPGC_unlink_impl(GPGC_GCManagerOldStrong* gcm, long _section, long _sections);
  void GPGC_unlink_impl(GPGC_GCManagerNewStrong* gcm, long _section, long _sections);

  static void oops_do(OopClosure *f);
  static void unlink(BoolObjectClosure* is_alive);
  static void GPGC_unlink(GPGC_GCManagerNewStrong* gcm, long _section, long _sections);
  static void GPGC_unlink(GPGC_GCManagerOldStrong* gcm, long _section, long _sections);

  static void print();
};


//
// Klass table is managed using a linearly searched list.  The table grows
// toward high memory.
//
class KlassTable : public OopTable {
 private:
  static KlassTable *_kt;               // Global reference to klass table.
  volatile long int _first_free;        // Hint to find the first free (null) entry.

  // Constructor.
  KlassTable(VirtualSpace *vs);

 public:
 
  // Initialize the global klass table.
  static void init(VirtualSpace *vs);
  
  // Returns the kid of klass reference passed in.
  virtual intptr_t put(objectRef r);
  
  // Search for the first available kid.
  intptr_t findFreeKid();
  
  // Override freeing so that we can update the first free hint. 
  virtual void free(intptr_t i);
  
  // True if index is in range.  Doesn't check validity of entry.
  static inline bool is_valid_index(int i) { return i >= 1 && i <= _kt->_top_dispensed_index; }

  // Add a primitive klass to the klass table.
  static void bindReservedKlassId(klassOop ko, unsigned int kid);
  
  // Add a constructed klass to the klass table.
  static void registerKlass(Klass* k);
  
  // Return the klass reference matching the kid.
  static klassRef getKlassByKlassId(unsigned int kid) {
#ifdef ASSERT
    if( !is_valid_index(kid) ) {
      fatal3("kid (%d) out of range - 1 <= %d <= %d", kid, kid, _kt->_top_dispensed_index);
    }
#endif
    objectRef r = _kt->get(kid);
    return *((klassRef*)&r);
  }
  
  // Return the address to the klass reference matching the kid.
  static klassRef* getKlassAddrByKlassId(unsigned int kid) {
    assert(_kt->is_valid_index(kid), "kid out of range");
    return (klassRef*) _kt->getAddr(kid);
  }

  // Return the base table address used by both tables.
  static void* getKlassTableBase() { return _kt->getTableBase(); }
  
  // Return the top dispensed index of the klass table.
  static long int max_klassId() { return _kt->_top_dispensed_index; }

#ifdef ASSERT
  // Return the address of the current top dispensed index
  static volatile long int* max_klassId_Addr() { return &_kt->_top_dispensed_index; }
#endif

  // True if the kid is is in the range of valid indices (doesn't do a null check.)
  static bool is_valid_klassId(int kid);
  
  // Used by SBA to determine if the address is in the klass table.
static bool contains(void*p){
    return getKlassTableBase() <= p && p <= _kt->getAddr(_kt->highestUsedIndex());
  }
  
  // Return the Klass name matching the kid.
  static const char* pretty_name(unsigned int kid);
  
  // GC
  
  void GPGC_unlink_impl(GPGC_GCManagerOldStrong* gcm, long _section, long _sections);
  void GPGC_sweep_weak_methodCodes_section_impl(GPGC_GCManagerOldStrong* gcm, long section, long sections);

  static void oops_do(OopClosure *f);
  static void unlink(BoolObjectClosure* is_alive);
  static void GPGC_unlink(GPGC_GCManagerOldStrong* gcm, long _section, long _sections);
  static void GPGC_sweep_weak_methodCodes_section(GPGC_GCManagerOldStrong* gcm, long section, long sections);

  static void print();
};
#endif // OOPTABLE_HPP
