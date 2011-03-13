/*
 * Copyright 1997-2006 Sun Microsystems, Inc.  All Rights Reserved.
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
#ifndef SYMBOLTABLE_HPP
#define SYMBOLTABLE_HPP

#include "hashtable.hpp"

// The symbol table holds all symbolOops and corresponding interned strings.
// symbolOops and literal strings should be canonicalized.
//
// The interned strings are created lazily.
//
// It is implemented as an open hash table with a fixed number of buckets.
//
// %note:
//  - symbolTableEntrys are allocated in blocks to reduce the space overhead.

// -*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-
//
// Pauseless GC garbage collects the string and symbol tables
// concurrently with them being accessed and added to by mutators.  You
// better be sure you understand how this works before you change things
// in here.
//
// -*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-*-WARNING-



// Concurrent SymbolTable lockless GC design notes:
//
// 1.  Each SymbolOop has this added to it:
//
//    jlong _strength;
//
// The _strength field has one of three values: StrongRef, WeakRef, and
// DeadRef.  The underlying value of StrongRef and WeakRef mirrors the
// current value of the NMT bit, where StrongRef==NMT, and WeakRef==1-NMT.
// The value of StrongRef and WeakRef thus alternates every GC cycle.
// 
// 2. Inserts of symbolOops into a hash chain must now be done with a CAS,
// since concurrent collector threads may be unlinking symbolOops from the
// chain at the same time.
//
// 3. At the end of a GC cycle, all SymbolOops in the SymbolTable must have
// their _strength field set to StrongRef.  Since the value of StrongRef and WeakRef
// flip-flops each GC cycle, this means that at the start of each new GC cycle,
// all SymbolOops in the SymbolTable become _strength == WeakRef.
// 
// 4. When a mutator looks up a SymbolOop in the SymbolTable, it does the
// normal hash chain traversal.  After it finds the SymbolOop, it has to check
// the _strength field.  If it's StrongRef, the lookup function returns the discovered
// SymbolOop.  If it's DeadRef, it pretends it didn't find the SymbolOop.  If it's
// WeakRef, it tries to CAS it to StrongRef.  Return a result depending on the ending
// value of _strength being StrongRef or DeadRef.
// 
// 5. When a mutator adds a SymbolOop, it's added with _strength = StrongRef.
// Note that SymbolOops used to only exist once in the hash chain.  This scheme
// allows two SymbolOops to exist in the chain, once with DeadRef and once with
// StrongRef.
// 
// 6. After the final mark pause is done, the collector concurrently scans the
// SymbolTable.  Any time it finds a SymbolOop whose _strength == WeakRef, it checks
// the mark bit of the SymbolOop.  If it's live, it sets _strength = StrongRef,
// no CAS needed.  If it's dead, it CASs _strength to DeadRef.  If the CAS fails,
// a mutator revived the SymbolOop, and the collector marks it live.  If the CAS
// succeeds, the SymbolOop is unlinked with a CAS of the prior SymbolOop's
// next pointer.  Only one GC thread per hash bucket, so no fear of unlink races.
// 
// 7.  A page cannot be deallocated until after the next safepoint, as a mutator
// might have found a SymbolOop but not yet had a chance to check it's _strength
// field.  A safepoint passing ensures that there's no half-way looked up SymbolOop
// sitting in a mutator.  Really we don't need a safepoint, we just need what Cliff
// has called a Checkpoint in his GC paper.  Since SymbolOops are always allocated
// in Perm, we have to defer the deallocation of totally empty perm gen pages to
// after the relocation safepoint, instead of the current behavior of deallocating them
// before the relocation safepoint.
// 
// 8. The SymbolTable_lock continues to exist, with it's usage unchanged.  It is
// only to keep two mutators from racing and adding the same SymbolOop twice.
// 
// In short, by switch to a tri-state mark  of strong/weak/dead, we can use
// a CAS to manage object unlinking, and avoid the need for locks.
 

class BoolObjectClosure;


class SymbolTable:public WeakHashtable{

private:
  // The symbol table
  static SymbolTable* _the_table;

  // Adding elements    
  symbolOop basic_add(int index, u1* name, int len,
                      unsigned int hashValue, TRAPS);
  bool basic_add(constantPoolHandle cp, int names_count,
                 const char** names, int* lengths, int* cp_indices,
                 unsigned int* hashValues, TRAPS);

  // Table size
  enum {
    symbol_table_size = 20011
  };

  symbolOop lookup(int index, const char* name, int len, unsigned int hash);

  SymbolTable()
    : WeakHashtable(symbol_table_size, sizeof (WeakHashtableEntry)) {}

  SymbolTable(HashtableBucket* t, int number_of_entries)
:WeakHashtable(symbol_table_size,sizeof(WeakHashtableEntry),t,
                number_of_entries) {}

  symbolOop ensure_strong_ref(WeakHashtableEntry* e, symbolOop symbol);

public:
  enum {
    symbol_alloc_batch_size = 8
  };

  // The symbol table
static inline SymbolTable*the_table(){return _the_table;}

static inline void create_table(){
    assert(_the_table == NULL, "One symbol table allowed.");
    _the_table = new SymbolTable();
  }

  static inline void create_table(HashtableBucket* t, int length,
                           int number_of_entries) {
    assert(_the_table == NULL, "One symbol table allowed.");
    assert(length == symbol_table_size * sizeof(HashtableBucket),
           "bad shared symbol size.");
    _the_table = new SymbolTable(t, number_of_entries);
  }

  static symbolOop lookup(const char* name, int len, TRAPS);
  // lookup only, won't add. Also calculate hash.
  static symbolOop lookup_only(const char* name, int len, unsigned int& hash);
  // Only copy to C string to be added if lookup failed.
  static symbolOop lookup(symbolHandle sym, int begin, int end, TRAPS);

  static void add(constantPoolHandle cp, int names_count,
                  const char** names, int* lengths, int* cp_indices,
                  unsigned int* hashValues, TRAPS);

  // GC support
  //   Delete pointers to otherwise-unreachable objects.
  static void unlink(BoolObjectClosure* cl) {
the_table()->WeakHashtable::unlink(cl);
  }

  static void GPGC_unlink_section     (long section, long sections);
  static void GC_release_pending_free ();

  // Invoke "f->do_oop" on the locations of all oops in the table.
  static void oops_do(OopClosure* f) {
the_table()->WeakHashtable::oops_do(f);
  }

  // Symbol lookup
  static symbolOop lookup(int index, const char* name, int len, TRAPS);

  // Needed for preloading classes in signatures when compiling.
  // Returns the symbol is already present in symbol table, otherwise
  // NULL.  NO ALLOCATION IS GUARANTEED!
  static symbolOop probe(const char* name, int len);

  // Histogram
  static void print_histogram()     PRODUCT_RETURN;

  // Debugging
  static void verify();

  static void GPGC_verify_strength(int index, jlong expected) PRODUCT_RETURN;
static void GPGC_verify_strong_refs()PRODUCT_RETURN;

  static void GC_verify_marks(WeakOopClosure* verify);

  // Sharing
  static void copy_buckets(char** top, char*end) {
the_table()->WeakHashtable::copy_buckets(top,end);
  }
  static void copy_table(char** top, char*end) {
the_table()->WeakHashtable::copy_table(top,end);
  }
  static void reverse(void* boundary = NULL) {
((WeakHashtable*)the_table())->reverse(boundary);
  }
};


// Concurrent StringTable lockless GC design notes:
//
// 1.  Each stringTableEntry has this added to it:
//
//    jlong _strength;
//
// The _strength field has one of three values: StrongRef, WeakRef, and
// DeadRef.  The underlying value of StrongRef and WeakRef mirrors the
// current value of the NMT bit, where StrongRef==NMT, and WeakRef==1-NMT.
// The value of StrongRef and WeakRef thus alternates every GC cycle.
// 
// 2. Inserts of a stringTableEntry into a hash chain must now be done with a CAS,
// since concurrent collector threads may be unlinking stringTableEntrys from the
// chain at the same time.
//
// 3. At the end of a GC cycle, all stringTableEntrys in the StringTable must have
// their _strength field set to StrongRef.  Since the value of StrongRef and WeakRef
// flip-flops each GC cycle, this means that at the start of each new GC cycle,
// all stringTableEntrys in the StringTable become _strength == WeakRef.
// 
// 4. When a mutator looks up a String in the StringTable, it does the normal hash
// chain traversal.  After it finds the String, it has to check the _strength field
// of the stringTableEntry.  If it's StrongRef, the lookup function returns the
// discovered String.  If it's DeadRef, it pretends it didn't find the String.  If
// it's WeakRef, it tries to CAS it to StrongRef.  Return a result depending on the
// ending value of _strength being StrongRef or DeadRef.
// 
// 5. When a mutator adds a String, its stringTableEntry is added with
// _strength = StrongRef.  Note that Strings used to only exist once in the hash
// chain.  This scheme allows two Strings to exist in the chain, once with DeadRef
// and once with StrongRef.
// 
// 6. After the final mark pause is done, the collector concurrently scans the
// StringTable.  Any time it finds a string whose _strength == WeakRef, it checks
// the mark bit of the string.  If it's live, it sets _strength = StrongRef,
// no CAS needed.  If it's dead, it CASs _strength to DeadRef.  If the CAS fails,
// a mutator revived the string, and the collector marks it live.  If the CAS
// succeeds, the stringTableEntry is unlinked with a CAS of the prior stringTableEntry's
// next pointer.  Only one GC thread per hash bucket, so no fear of unlink races.
// 
// 7.  A page containing unlinked strings cannot be deallocated until after the next
// safepoint, as a mutator might have found a string but not yet had a chance to
// check it's _strength field.  A safepoint passing ensures that there's no half-way
// looked up string sitting in a mutator.  Really we don't need a safepoint, we just
// need what Cliff has called a Checkpoint in his GC paper.  Since strings in
// StringTable are always allocated in Perm, we solve the problem by deferring the
// deallocation of totally empty perm gen pages to after the relocation safepoint,
// instead of the current behavior of deallocating them before the relocation safepoint.
// We also have to delay reuse of the stringTableEntrys until after the next safepoint,
// which is done by throwing them on a pending_deallocation list.
//
// 8. The StringTable_lock continues to exist, with its usage unchanged.  It is
// only to keep two mutators from racing and adding the same string twice.
// 
// In short, by having a tri-state mark of strong/weak/dead, we can use
// a CAS to manage object unlinking, and avoid the need for locks.


class StringTable:public WeakHashtable{

private:
  // The string table
  static StringTable* _the_table;

  static oop intern(Handle string_or_null, jchar* chars, int length, TRAPS);
  oop basic_add(int index, Handle string_or_null, jchar* name, int len,
                unsigned int hashValue, TRAPS);

  // Table size
  enum {
    string_table_size = 1009
  };

  oop lookup(int index, jchar* chars, int length, unsigned int hashValue);

StringTable():WeakHashtable(string_table_size,sizeof(WeakHashtableEntry)){}

  StringTable(HashtableBucket* t, int number_of_entries)
:WeakHashtable(string_table_size,sizeof(WeakHashtableEntry),t,
                number_of_entries) {}
  
  bool ensure_strong_ref(WeakHashtableEntry* e, oop literal_string);

public:
  // The string table
  static StringTable* the_table() { return _the_table; }

  static void create_table() {
    assert(_the_table == NULL, "One string table allowed.");
    _the_table = new StringTable();
  }

  static void create_table(HashtableBucket* t, int length,
                           int number_of_entries) {
    assert(_the_table == NULL, "One string table allowed.");
    assert(length == string_table_size * sizeof(HashtableBucket),
           "bad shared string size.");
    _the_table = new StringTable(t, number_of_entries);
  }


  static int hash_string(jchar* s, int len);


  // GC support
  //   Delete pointers to otherwise-unreachable objects.
  static void unlink(BoolObjectClosure* cl) {
the_table()->WeakHashtable::unlink(cl);
  }

  static void GPGC_unlink_section     (long section, long sections);
  static void GC_release_pending_free ();

  // Invoke "f->do_oop" on the locations of all oops in the table.
  static void oops_do(OopClosure* f) {
the_table()->WeakHashtable::oops_do(f);
  }

  // Probing
  static oop lookup(symbolOop symbol);

  // Interning
  static oop intern(symbolOop symbol, TRAPS);
  static oop intern(oop string, TRAPS);
  static oop intern(const char *utf8_string, TRAPS);

  // Debugging
  static void verify();

  static void GPGC_verify_strength(jint index, jlong expected) PRODUCT_RETURN;
static void GPGC_verify_strong_refs()PRODUCT_RETURN;

  static void GC_verify_marks(WeakOopClosure* verify);

  // Sharing
  static void copy_buckets(char** top, char*end) {
the_table()->WeakHashtable::copy_buckets(top,end);
  }
  static void copy_table(char** top, char*end) {
the_table()->WeakHashtable::copy_table(top,end);
  }
  static void reverse() {
    ((BasicHashtable*)the_table())->reverse();
  }
};

#endif // SYMBOLTABLE_HPP
